// SPDX-License-Identifier: GPL-2.0-only
/*
 * TSMF stream selector (tsmf.c)
 *
 * 1つの搬送波に多重化された複数の TS から、TSMF ヘッダーの直後の52パケットの位置で1つの TS を選ぶ
 * ISDB-C の受信はすべてここを通し、ヘッダーのない搬送波は1本目の指定でだけそのまま出力する
 * 処理の組み立ては、radi-sh 氏による BonDriver_BDA の TSMF.cpp に合わせている
 */

#include "tsmf.h"

#ifdef __linux__
#include <linux/string.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <string.h>
#endif

static bool tsmf_stream_flag(const u8 *flags, int stream)
{
	return (flags[stream / 8] & (0x80 >> (stream % 8))) ? true : false;
}

static bool tsmf_is_header(const u8 *packet)
{
	u16 frame_sync = ((packet[4] << 8) | packet[5]) & 0x1fff;
	u8 frame_type = packet[6] & 0x0f;
	u32 crc = 0xffffffff;
	int i, bit;

	if (packet[1] != 0x00 || packet[2] != 0x2f || (packet[3] & 0xf0) != 0x10 ||
	    (frame_sync != 0x1a86 && frame_sync != 0x0579) || (packet[6] & 0x10) ||
	    (frame_type != 0x01 && frame_type != 0x02))
		return false;

	/* CRC_32 を含めて計算した値が0なら、ヘッダーは壊れていない */
	for (i = 4; i < 188; i++) {
		crc ^= (u32)packet[i] << 24;
		for (bit = 0; bit < 8; bit++)
			crc = (crc & 0x80000000) ? ((crc << 1) ^ 0x04c11db7) : (crc << 1);
	}

	return crc ? false : true;
}

static void tsmf_parse_header(struct tsmf_filter *filter, const u8 *packet)
{
	int i, n = 0, stream = -1;
	bool output_tlv = false;

	/* 相対 TS 番号は 0 から詰めて使われるとは限らないため、有効な TS を順に数える */
	for (i = 0; i < 15; i++) {
		if (tsmf_stream_flag(&packet[7], i) && ++n == filter->stream_index) {
			stream = i;
			/* stream_type のビットが 0 の TS は分割 TLV、1 の TS は MPEG-2 TS */
			output_tlv = !tsmf_stream_flag(&packet[125], i);
			break;
		}
	}

	/* 選ぶ TS か形式が変わったときは、TLV の断片を連結しない */
	if (stream != filter->stream || output_tlv != filter->output_tlv)
		filter->tlv_size = 0;

	for (i = 0; i < TSMF_SLOT_NUM; i++)
		filter->slot_stream[i] = (i & 1) ? (packet[73 + (i / 2)] & 0x0f) : (packet[73 + (i / 2)] >> 4);

	filter->header_found = true;
	filter->not_found = (stream < 0);
	filter->stream = stream;
	filter->output_tlv = output_tlv;
	filter->slot = 0;
	filter->frame_size = 0;
}

static bool tsmf_append_tlv(struct tsmf_filter *filter, const u8 *data, size_t size)
{
	if (size > sizeof(filter->tlv_buf) - filter->tlv_size) {
		filter->tlv_size = 0;
		return false;
	}

	memcpy(filter->tlv_buf + filter->tlv_size, data, size);
	filter->tlv_size += size;

	return true;
}

/* 分割 TLV は PID 0x002d で運ばれ、開始パケットは4バイト目のポインターで TLV の境界を示し、継続パケットは先頭3バイトの後がすべて TLV */
static size_t tsmf_extract_tlv(struct tsmf_filter *filter, const u8 *packet, size_t out_size)
{
	size_t start;

	if ((packet[1] & 0xa0) || (((packet[1] & 0x1f) << 8) | packet[2]) != 0x002d) {
		if (packet[1] & 0x80)
			filter->tlv_size = 0;
		return out_size;
	}

	if (!(packet[1] & 0x40)) {
		if (filter->tlv_size)
			tsmf_append_tlv(filter, packet + 3, 185);
		return out_size;
	}

	start = packet[3] + 4;
	if (start >= 188) {
		filter->tlv_size = 0;
		return out_size;
	}

	if (filter->tlv_size && tsmf_append_tlv(filter, packet + 4, start - 4)) {
		memcpy(filter->tlv_out + out_size, filter->tlv_buf, filter->tlv_size);
		out_size += filter->tlv_size;
	}

	filter->tlv_size = 0;
	tsmf_append_tlv(filter, packet + start, 188 - start);

	return out_size;
}

static size_t tsmf_flush_frame(struct tsmf_filter *filter, const u8 **out)
{
	size_t i, size = 0;

	if (!filter->output_tlv) {
		*out = filter->frame;
		return filter->frame_size;
	}

	for (i = 0; i < filter->frame_size; i += 188)
		size = tsmf_extract_tlv(filter, filter->frame + i, size);

	*out = filter->tlv_out;
	return size;
}

void tsmf_filter_init(struct tsmf_filter *filter, int stream_index, bool discard_null_packets, unsigned int stale_packets)
{
	filter->stream_index = stream_index;
	filter->discard_null_packets = discard_null_packets;
	filter->stale_packets = stale_packets;
	filter->skip_packets = stale_packets;
	filter->passthrough = false;
	filter->header_found = false;
	filter->not_found = false;
	filter->detect_packets = 0;
	filter->output_tlv = false;
	filter->stream = -1;
	filter->slot = -1;
	filter->frame_size = 0;
	filter->tlv_size = 0;
}

static bool tsmf_is_null_packet(const u8 *packet)
{
	return ((((packet[1] & 0x1f) << 8) | packet[2]) == 0x1fff) ? true : false;
}

/* ヘッダーのない搬送波の TS は、TSMF のフレームと同じ大きさにまとめて出力し、パケットごとの書き込みを避ける */
static size_t tsmf_passthrough_packet(struct tsmf_filter *filter, const u8 *packet, const u8 **out)
{
	if (filter->discard_null_packets && tsmf_is_null_packet(packet))
		return 0;

	memcpy(filter->frame + filter->frame_size, packet, 188);
	filter->frame_size += 188;

	if (filter->frame_size < sizeof(filter->frame))
		return 0;

	filter->frame_size = 0;
	*out = filter->frame;
	return sizeof(filter->frame);
}

/* 出力するデータを *out に設定してその長さを返し、出力しないパケットでは0を返す */
size_t tsmf_filter_packet(struct tsmf_filter *filter, const u8 *packet, const u8 **out)
{
	size_t size = 0;

	/* 前の搬送波のヘッダーやヘッダーのない TS で、新しい搬送波の判定を誤らないようにする */
	if (filter->skip_packets) {
		filter->skip_packets--;
		return 0;
	}

	if (filter->passthrough)
		return tsmf_passthrough_packet(filter, packet, out);

	if (tsmf_is_header(packet)) {
		/* 前のフレームの52パケットがそろったときだけ、そのフレームで選んだパケットを出力する */
		if (filter->slot == TSMF_SLOT_NUM)
			size = tsmf_flush_frame(filter, out);
		else
			filter->tlv_size = 0;

		tsmf_parse_header(filter, packet);
		return size;
	}

	if (!filter->header_found) {
		/* 一定の長さヘッダーが届かなければ TSMF を使わない搬送波と決め、1本目の指定ならそのまま出力する */
		if (++filter->detect_packets >= TSMF_DETECT_PACKETS) {
			if (filter->stream_index == 1)
				filter->passthrough = true;
			else
				filter->not_found = true;
		}
		return 0;
	}

	if (filter->slot < 0)
		return 0;

	if (filter->slot >= TSMF_SLOT_NUM) {
		/* ヘッダーの欠落でスロットの位置が分からなくなったため、次のヘッダーまで出力しない */
		filter->slot = -1;
		filter->tlv_size = 0;
		return 0;
	}

	if (filter->slot_stream[filter->slot++] != filter->stream + 1 || filter->stream < 0)
		return 0;

	/* TSMF はヘッダーからの位置で TS を選ぶため、ブリッジでは null パケットを除かずに、選んだ後で除く */
	if (filter->discard_null_packets && tsmf_is_null_packet(packet))
		return 0;

	memcpy(filter->frame + filter->frame_size, packet, 188);
	filter->frame_size += 188;

	return 0;
}

enum tsmf_filter_state tsmf_filter_get_state(const struct tsmf_filter *filter)
{
	if (filter->passthrough || filter->stream >= 0)
		return TSMF_FILTER_FOUND;
	if (filter->not_found)
		return TSMF_FILTER_NOT_FOUND;
	return TSMF_FILTER_SEARCHING;
}
