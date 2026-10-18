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

struct tsmf_filter {
	int relative_stream;
	int tsid;
	bool output_tlv;
	bool discard_null_packets;
	/* ヘッダーから求めた、出力する TS の相対 TS 番号 (見つからなければ -1) */
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
void tsmf_filter_init(struct tsmf_filter *filter, int relative_stream, int tsid, bool output_tlv, bool discard_null_packets);
size_t tsmf_filter_packet(struct tsmf_filter *filter, const u8 *packet, const u8 **out);
bool tsmf_filter_found(const struct tsmf_filter *filter);
#ifdef __cplusplus
}
#endif

#endif
