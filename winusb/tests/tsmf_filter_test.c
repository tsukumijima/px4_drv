// SPDX-License-Identifier: GPL-2.0-only
/* 作った TS で、TSMF の何本目の選択・ヘッダーのない搬送波の素通し・分割 TLV の自動判定を検証する
 * 実際の放送のヘッダーの値と受信の欠落はこの試験の確認範囲外
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsmf.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static struct tsmf_filter filter;
static u8 out_buf[1024 * 1024];
static size_t out_size;

static void feed(const u8 *packet)
{
	const u8 *out;
	size_t size = tsmf_filter_packet(&filter, packet, &out);

	CHECK(out_size + size <= sizeof(out_buf));
	memcpy(out_buf + out_size, out, size);
	out_size += size;
}

static void make_packet(u8 *packet, u16 pid, u8 fill)
{
	memset(packet, fill, 188);
	packet[0] = 0x47;
	packet[1] = (u8)(pid >> 8);
	packet[2] = (u8)pid;
	packet[3] = 0x10;
}

/* valid と tlv は相対 TS 番号ごとのビットで、slots は各スロットの相対 TS 番号 (-1 は空き) */
static void make_header(u8 *packet, unsigned int valid, unsigned int tlv, const int *slots)
{
	u32 crc = 0xffffffff;
	int i, bit;

	memset(packet, 0xff, 188);
	packet[0] = 0x47;
	packet[1] = 0x00;
	packet[2] = 0x2f;
	packet[3] = 0x10;
	packet[4] = 0x1a;
	packet[5] = 0x86;
	packet[6] = 0x01;
	memset(&packet[7], 0, 2);
	memset(&packet[125], 0, 2);
	for (i = 0; i < 15; i++) {
		if (valid & (1u << i))
			packet[7 + (i / 8)] |= (u8)(0x80 >> (i % 8));
		/* stream_type のビットは MPEG-2 TS が 1、分割 TLV が 0 */
		if (!(tlv & (1u << i)))
			packet[125 + (i / 8)] |= (u8)(0x80 >> (i % 8));
		packet[9 + (i * 4)] = 0x40;
		packet[10 + (i * 4)] = (u8)i;
	}
	for (i = 0; i < TSMF_SLOT_NUM; i++) {
		u8 v = (u8)(slots[i] + 1);
		if (i & 1)
			packet[73 + (i / 2)] = (u8)((packet[73 + (i / 2)] & 0xf0) | v);
		else
			packet[73 + (i / 2)] = (u8)(v << 4);
	}
	for (i = 4; i < 184; i++) {
		crc ^= (u32)packet[i] << 24;
		for (bit = 0; bit < 8; bit++)
			crc = (crc & 0x80000000) ? ((crc << 1) ^ 0x04c11db7) : (crc << 1);
	}
	packet[184] = (u8)(crc >> 24);
	packet[185] = (u8)(crc >> 16);
	packet[186] = (u8)(crc >> 8);
	packet[187] = (u8)crc;
}

/* 相対 TS 番号 1 と 3 を交互のスロットに載せ、各パケットの5バイト目に相対 TS 番号を書く */
static void feed_frame(const int *slots, unsigned int valid, unsigned int tlv)
{
	u8 packet[188];
	int i;

	make_header(packet, valid, tlv, slots);
	feed(packet);
	for (i = 0; i < TSMF_SLOT_NUM; i++) {
		make_packet(packet, 0x0100, (u8)(slots[i] < 0 ? 0xee : slots[i]));
		feed(packet);
	}
}

int main(void)
{
	int slots[TSMF_SLOT_NUM];
	u8 packet[188];
	size_t i;

	for (i = 0; i < TSMF_SLOT_NUM; i++)
		slots[i] = (i % 3 == 0) ? 1 : (i % 3 == 1) ? 3 : -1;

	/* ヘッダーのない搬送波は、1本目の指定でだけ素通しし、null パケットを除いて52パケットずつ出力する */
	tsmf_filter_init(&filter, 1, true, 0);
	out_size = 0;
	for (i = 0; i + 1 < TSMF_DETECT_PACKETS; i++) {
		make_packet(packet, 0x0100, 0x11);
		feed(packet);
		CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_SEARCHING);
	}
	make_packet(packet, 0x0100, 0x11);
	feed(packet);
	CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_FOUND && out_size == 0);
	for (i = 0; i < TSMF_SLOT_NUM * 2; i++) {
		make_packet(packet, (i % 2) ? 0x1fff : 0x0100, (u8)i);
		feed(packet);
	}
	/* 判断した424パケット目から素通しになるため、最初のフレームはそのパケットから始まる */
	CHECK(out_size == TSMF_SLOT_NUM * 188 && out_buf[4] == 0x11);
	for (i = 188; i < out_size; i += 188)
		CHECK(out_buf[i + 1] == 0x01 && out_buf[i + 2] == 0x00 && out_buf[i + 4] == (u8)((i / 188 - 1) * 2));
	puts("Passthrough without TSMF header and null discard: PASS");

	tsmf_filter_init(&filter, 2, true, 0);
	for (i = 0; i < TSMF_DETECT_PACKETS; i++) {
		make_packet(packet, 0x0100, 0x11);
		feed(packet);
	}
	CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_NOT_FOUND);
	puts("Second stream on carrier without TSMF header fails: PASS");

	/* 相対 TS 番号 0 が無効で 1 と 3 が有効なヘッダーでは、1本目が 1、2本目が 3、3本目はない */
	for (i = 1; i <= 3; i++) {
		size_t n;

		tsmf_filter_init(&filter, (int)i, false, 0);
		out_size = 0;
		feed_frame(slots, (1u << 1) | (1u << 3), 0);
		if (i == 3) {
			CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_NOT_FOUND);
			continue;
		}
		CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_FOUND && out_size == 0);
		/* フレームの52パケットがそろったと次のヘッダーで分かってから出力する */
		feed_frame(slots, (1u << 1) | (1u << 3), 0);
		/* slots は相対 TS 番号 1 を18スロット、3 を17スロットに割り当てている */
		CHECK(out_size == ((i == 1) ? 18 : 17) * 188);
		for (n = 0; n < out_size; n += 188)
			CHECK(out_buf[n + 4] == ((i == 1) ? 1 : 3));
	}
	puts("Nth valid stream selection with relative numbers from 1: PASS");

	/* 何本目の指定は最初のヘッダーで相対 TS 番号に決まり、有効な TS の組が変わっても別の TS へ切り替わらない */
	{
		size_t n;

		tsmf_filter_init(&filter, 2, false, 0);
		feed_frame(slots, (1u << 1) | (1u << 3), 0);
		out_size = 0;
		/* 相対 TS 番号 1 が無効になると、数え直せば2本目はなくなるが、決めた相対 TS 番号 3 を出力し続ける */
		feed_frame(slots, 1u << 3, 0);
		feed_frame(slots, 1u << 3, 0);
		CHECK(out_size == 17 * 188 * 2);
		for (n = 0; n < out_size; n += 188)
			CHECK(out_buf[n + 4] == 3);
		/* 決めた相対 TS 番号の TS が無効になれば、ほかの TS は出力しない */
		out_size = 0;
		feed_frame(slots, 1u << 1, 0);
		feed_frame(slots, 1u << 1, 0);
		CHECK(out_size == 17 * 188 && out_buf[4] == 3);
		out_size = 0;
		feed_frame(slots, 1u << 1, 0);
		CHECK(out_size == 0);
	}
	puts("Selected relative stream is kept when valid streams change: PASS");

	/* 分割 TLV の TS は TLV を取り出し、MPEG-2 TS の TS はそのまま出力する */
	{
		static const int tlv_slots[TSMF_SLOT_NUM] = { 0, 0, 0, 1, -1 };
		int s[TSMF_SLOT_NUM];
		u8 hdr[188];

		for (i = 0; i < TSMF_SLOT_NUM; i++)
			s[i] = (i < 5) ? tlv_slots[i] : -1;

		tsmf_filter_init(&filter, 1, false, 0);
		out_size = 0;
		make_header(hdr, (1u << 0) | (1u << 1), 1u << 0, s);
		feed(hdr);
		CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_FOUND);
		/* 開始パケット (ポインター 0)、継続パケット、次の開始パケット */
		make_packet(packet, 0x002d, 'A');
		packet[1] |= 0x40;
		packet[3] = 0;
		feed(packet);
		make_packet(packet, 0x002d, 'B');
		feed(packet);
		make_packet(packet, 0x002d, 'C');
		packet[1] |= 0x40;
		packet[3] = 0;
		feed(packet);
		make_packet(packet, 0x0100, 'D');
		feed(packet);
		for (i = 4; i < TSMF_SLOT_NUM; i++) {
			make_packet(packet, 0x0100, 0xee);
			feed(packet);
		}
		feed(hdr);
		CHECK(out_size == 184 + 185);
		for (i = 0; i < 184; i++)
			CHECK(out_buf[i] == 'A');
		/* 継続パケットは先頭3バイトの後がすべて TLV */
		CHECK(out_buf[184] == 0x10);
		for (i = 185; i < out_size; i++)
			CHECK(out_buf[i] == 'B');

		/* 受信エラーで PID が壊れたパケット (TEI あり) が混ざっても、分割 TLV の出力を続ける */
		make_packet(packet, 0x0100, 'E');
		packet[1] |= 0x80;
		feed(packet);
		make_packet(packet, 0x002d, 'F');
		feed(packet);
		feed(packet);
		make_packet(packet, 0x0100, 'D');
		feed(packet);
		for (i = 4; i < TSMF_SLOT_NUM; i++) {
			make_packet(packet, 0x0100, 0xee);
			feed(packet);
		}
		feed(hdr);
		CHECK(!filter.tlv_rejected && filter.output_tlv);

		tsmf_filter_init(&filter, 2, false, 0);
		out_size = 0;
		feed(hdr);
		make_packet(packet, 0x002d, 'A');
		feed(packet);
		feed(packet);
		feed(packet);
		make_packet(packet, 0x0100, 'D');
		feed(packet);
		for (i = 4; i < TSMF_SLOT_NUM; i++) {
			make_packet(packet, 0x0100, 0xee);
			feed(packet);
		}
		feed(hdr);
		CHECK(out_size == 188 && out_buf[4] == 'D');
	}
	puts("TLV output decided by stream_type: PASS");

	/* stream_type が分割 TLV を示していても、分割 TLV 以外のパケットを運ぶ TS は MPEG-2 TS としてそのまま出力し続ける */
	tsmf_filter_init(&filter, 1, false, 0);
	out_size = 0;
	feed_frame(slots, (1u << 1) | (1u << 3), 1u << 1);
	feed_frame(slots, (1u << 1) | (1u << 3), 1u << 1);
	CHECK(out_size == 18 * 188 && out_buf[1] == 0x01 && out_buf[4] == 1);
	CHECK(filter.tlv_rejected && !filter.output_tlv);
	feed_frame(slots, (1u << 1) | (1u << 3), 1u << 1);
	CHECK(out_size == 18 * 188 * 2 && !filter.output_tlv);
	puts("TS without divided TLV packets stays MPEG-2 TS despite stream_type: PASS");

	/* 選局し直した直後に届く前の搬送波の TSMF のヘッダーは、判定に使わずに捨てる
	 * 捨てないと、ヘッダーのない新しい搬送波を TSMF と判定し、次のヘッダーを待ち続けて何も出力しない */
	tsmf_filter_init(&filter, 1, false, 816);
	out_size = 0;
	for (i = 0; i < 3; i++)
		feed_frame(slots, (1u << 1) | (1u << 3), 0);
	CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_SEARCHING && out_size == 0);
	for (i = 3 * (TSMF_SLOT_NUM + 1); i < 816 + TSMF_DETECT_PACKETS; i++) {
		make_packet(packet, 0x0100, 0x22);
		feed(packet);
	}
	CHECK(tsmf_filter_get_state(&filter) == TSMF_FILTER_FOUND && filter.passthrough);
	for (i = 0; i < TSMF_SLOT_NUM; i++) {
		make_packet(packet, 0x0100, 0x22);
		feed(packet);
	}
	CHECK(out_size == TSMF_SLOT_NUM * 188 && out_buf[4] == 0x22);
	puts("Stale TSMF header from previous carrier is skipped: PASS");

	/* ヘッダーのない搬送波と判断した後でも、ヘッダーが届けば TSMF の搬送波として選び直す */
	tsmf_filter_init(&filter, 1, false, 0);
	for (i = 0; i < TSMF_DETECT_PACKETS; i++) {
		make_packet(packet, 0x0100, 0x11);
		feed(packet);
	}
	CHECK(filter.passthrough);
	out_size = 0;
	feed_frame(slots, (1u << 1) | (1u << 3), 0);
	CHECK(!filter.passthrough && tsmf_filter_get_state(&filter) == TSMF_FILTER_FOUND);
	out_size = 0;
	feed_frame(slots, (1u << 1) | (1u << 3), 0);
	CHECK(out_size == 18 * 188);
	for (i = 0; i < out_size; i += 188)
		CHECK(out_buf[i + 4] == 1);
	puts("Passthrough switches back to TSMF when a header arrives: PASS");

	/* 一度ヘッダーを受け取った後は、ヘッダーが途切れても素通しにせず、別の TS のパケットを出力しない */
	tsmf_filter_init(&filter, 1, false, 0);
	feed_frame(slots, (1u << 1) | (1u << 3), 0);
	out_size = 0;
	for (i = 0; i < TSMF_DETECT_PACKETS * 2; i++) {
		make_packet(packet, 0x0100, 0x33);
		feed(packet);
	}
	CHECK(!filter.passthrough && tsmf_filter_get_state(&filter) == TSMF_FILTER_FOUND && out_size == 0);
	puts("TSMF carrier stays TSMF when headers are lost: PASS");

	/* CRC が合わないヘッダーでも、ヘッダーの位置に届いたヘッダーの PID のパケットなら、そろった前のフレームは出力する
	 * 直後のフレームは位置の基準となるヘッダーがないため出力せず、次の正しいヘッダーから選び直す */
	{
		u8 broken[188];

		tsmf_filter_init(&filter, 1, false, 0);
		out_size = 0;
		feed_frame(slots, (1u << 1) | (1u << 3), 0);
		make_header(broken, (1u << 1) | (1u << 3), 0, slots);
		broken[100] ^= 0xff;
		feed(broken);
		CHECK(out_size == 18 * 188);
		for (i = 0; i < out_size; i += 188)
			CHECK(out_buf[i + 4] == 1);
		for (i = 0; i < TSMF_SLOT_NUM; i++) {
			make_packet(packet, 0x0100, 1);
			feed(packet);
		}
		feed_frame(slots, (1u << 1) | (1u << 3), 0);
		CHECK(out_size == 18 * 188);
		feed_frame(slots, (1u << 1) | (1u << 3), 0);
		CHECK(out_size == 18 * 188 * 2);
	}
	puts("Frame before a header with a CRC error is output: PASS");

	/* 受信を止めて再開したときは、判断を保ったままフレームの途中の状態だけを捨てる */
	tsmf_filter_init(&filter, 1, false, 816);
	for (i = 0; i < 816 + TSMF_DETECT_PACKETS + 10; i++) {
		make_packet(packet, 0x0100, 0x44);
		feed(packet);
	}
	/* 判断した424パケット目から素通しになるため、たまっているのは11パケット */
	CHECK(filter.passthrough && filter.frame_size == 11 * 188);
	tsmf_filter_resync(&filter);
	CHECK(filter.passthrough && filter.frame_size == 0 && filter.skip_packets == 0);
	out_size = 0;
	for (i = 0; i < TSMF_SLOT_NUM; i++) {
		make_packet(packet, 0x0100, 0x44);
		feed(packet);
	}
	CHECK(out_size == TSMF_SLOT_NUM * 188);
	puts("Resync keeps the carrier decision: PASS");
	return 0;
}
