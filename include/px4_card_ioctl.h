// SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note
/*
 * 内蔵カードリーダーのデバイスファイル (/dev/px4cardN) の ioctl 定義
 *
 * カーネル側はカード検出・電源・UART の送受信だけを提供し、ATR の解析と T=1 はユーザー空間の IFD ハンドラーが担当する
 * 32bit のユーザー空間からも同じ配置で使えるよう、構造体は固定幅の型だけで構成する
 */

#ifndef __PX4_CARD_IOCTL_H__
#define __PX4_CARD_IOCTL_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/* 構造体の配置や各コマンドの意味を変えたときに上げる */
#define PX4_CARD_ABI_VERSION 1

/* IT930x の UART が1回に送受信できるフレームの上限 */
#define PX4_CARD_MAX_FRAME 255

struct px4_card_info {
	__u32 abi_version;
	__u32 max_frame;
	__u32 reserved[2];
};

/*
 * value と length の意味はコマンドごとに異なり、使わない項目は 0 にする
 *
 * PX4_CARD_POWER: value に 1 (電源を入れて UART を初期化) か 0 (電源を切る) を渡す
 * PX4_CARD_DETECT: value にカードの有無を返す (電源を入れていなくても使える)
 * PX4_CARD_RESET: カードをリセットし、UART を ATR の受信速度 (9600bps) へ戻す
 * PX4_CARD_SET_BAUDRATE: value に 9600、19200、38400 のいずれかを渡す
 * PX4_CARD_RX_READY: value に、カードの応答を受信し終えたかを返す
 * PX4_CARD_READ: length に受信できる最大長を渡し、実際の受信長を length に返す
 * PX4_CARD_WRITE: data の先頭から length バイトを送信する
 */
struct px4_card_io {
	__u32 value;
	__u32 length;
	__u8 data[PX4_CARD_MAX_FRAME];
	__u8 reserved;
};

#define PX4_CARD_GET_INFO _IOR('X', 0x40, struct px4_card_info)
#define PX4_CARD_POWER _IOW('X', 0x41, struct px4_card_io)
#define PX4_CARD_DETECT _IOWR('X', 0x42, struct px4_card_io)
#define PX4_CARD_RESET _IOW('X', 0x43, struct px4_card_io)
#define PX4_CARD_SET_BAUDRATE _IOW('X', 0x44, struct px4_card_io)
#define PX4_CARD_RX_READY _IOWR('X', 0x45, struct px4_card_io)
#define PX4_CARD_READ _IOWR('X', 0x46, struct px4_card_io)
#define PX4_CARD_WRITE _IOW('X', 0x47, struct px4_card_io)

#endif
