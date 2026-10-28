// SPDX-License-Identifier: GPL-2.0-only
/*
 * TSMF stream selector definitions (tsmf.h)
 */

#ifndef __TSMF_H__
#define __TSMF_H__

#ifdef __linux__
#include <linux/types.h>
#elif defined(_WIN32) || defined(_WIN64)
#include "misc_win.h"
#endif

#define TSMF_SLOT_NUM		52
#define TSMF_TLV_MAX_SIZE	65536

/* TSMF の TS は、ヘッダーで有効な TS を相対 TS 番号の順に並べ、0 から数えた番号 (TS0〜TS14) で選ぶ
 * BS の TS0・TS1 と同じく、ヘッダーに書かれた相対 TS 番号そのものではなく、有効な TS の並びの中の位置を表す */
#define TSMF_STREAM_MAX		14

/* ヘッダーが届かずに素通しと判断するまでのパケット数 (1フレームは53パケットで、受信の誤りでヘッダーを数回落としても誤判定しない長さ) */
#define TSMF_DETECT_PACKETS	(53 * 8)

enum tsmf_filter_state {
	TSMF_FILTER_SEARCHING,
	TSMF_FILTER_FOUND,
	TSMF_FILTER_NOT_FOUND,
};

struct tsmf_filter {
	int stream_index;
	bool discard_null_packets;
	/* 選局し直した直後は、IT930x の転送1回分に前の搬送波の TS が残っていることがあるため、判定に使わずに捨てるパケット数 */
	unsigned int stale_packets;
	unsigned int skip_packets;
	/* TS0 を指定してヘッダーのない搬送波だった場合は、搬送波の TS をそのまま出力する */
	bool passthrough;
	/* 選局してからヘッダーを1つでも受け取ったか */
	bool header_found;
	/* ヘッダーに指定した TS がないか、TS1 以降の指定でヘッダーのない搬送波だった */
	bool not_found;
	/* ヘッダーを探し始めてから受け取ったパケット数で、TSMF_DETECT_PACKETS で止める */
	unsigned int detect_packets;
	/* ヘッダーの stream_type が分割 TLV の TS は、TS のままでは受信ソフトウェアが扱えないため、TLV を取り出して出力する */
	bool output_tlv;
	/* stream_type が分割 TLV を示す TS が、分割 TLV 以外のパケットを運んでいて MPEG-2 TS と分かったか */
	bool tlv_rejected;
	/* 指定した TS 番号を最初に解決できたヘッダーで決めた相対 TS 番号 (決まるまで -1) */
	int relative;
	/* ヘッダーから求めた、出力する TS の相対 TS 番号 (その TS がヘッダーで無効なら -1) */
	int stream;
	/* ヘッダーの直後から数えた位置で、位置が分からなければ次のヘッダーまで -1 */
	int slot;
	u8 slot_stream[TSMF_SLOT_NUM];
	/* 受信の欠落で位置がずれた別の TS を出力しないよう、次のヘッダーで52パケットがそろったと分かるまで出力しない */
	size_t frame_size;
	u8 frame[TSMF_SLOT_NUM * 188];
	/* TLV パケットは複数の TS パケットにまたがるため、完成するまで蓄積する */
	size_t tlv_size;
	u8 tlv_buf[TSMF_TLV_MAX_SIZE];
	/* 1フレームで完成する TLV は、蓄積中の1つとフレーム内の新しいデータを合わせた量を超えない */
	u8 tlv_out[TSMF_TLV_MAX_SIZE + (TSMF_SLOT_NUM * 188)];
};

#ifdef __cplusplus
extern "C" {
#endif
void tsmf_filter_init(struct tsmf_filter *filter, int stream_index, bool discard_null_packets, unsigned int stale_packets);
size_t tsmf_filter_packet(struct tsmf_filter *filter, const u8 *packet, const u8 **out);
void tsmf_filter_resync(struct tsmf_filter *filter);
enum tsmf_filter_state tsmf_filter_get_state(const struct tsmf_filter *filter);
#ifdef __cplusplus
}
#endif

#endif
