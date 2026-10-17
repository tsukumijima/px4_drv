// SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note
/*
 * 内蔵カードリーダーのデバイスファイル (/dev/px4card/<ID>) の ioctl 定義
 *
 * カーネル側はカード検出・電源・UART の送受信だけを提供し、ATR の解析と T=1 はユーザー空間の IFD ハンドラーが担当する
 * 各コマンドは DriverHost_PX4 の CardDevice (card_device.hpp) の関数と1対1に対応する
 * カーネルモジュールと IFD ハンドラーは別々に更新されるため、公開した番号の意味は変えず、機能は新しい番号で追加する
 * 古いカーネルモジュールは知らない番号に ENOTTY を返す
 * 電源を入れずに UART を操作するコマンドは EHOSTDOWN を、USB 切断後のコマンドは ENODEV を返す
 * EINTR はハードウェアを操作する前にだけ返すため、同じコマンドをそのまま再実行できる
 */

#ifndef __PX4_CARD_IOCTL_H__
#define __PX4_CARD_IOCTL_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/* IT930x の UART が1回に送受信できるフレームの上限 */
#define PX4_CARD_MAX_FRAME 255

/*
 * PX4_CARD_READ: length に data の容量 (1以上) を渡し、実際の受信長が返る
 * PX4_CARD_WRITE: data の先頭から length バイト (1以上) を1つの UART フレームとして送信する
 */
struct px4_card_frame {
	__u8 length;
	__u8 data[PX4_CARD_MAX_FRAME];
};

/* 基板電源の参照を取って UART を初期化する (電源投入済みなら何もしない) */
#define PX4_CARD_POWER_ON	_IO(0x8d, 0x40)
/* 基板電源の参照を解放する (失敗しても解放済みとして扱い、デバイスファイルを閉じたときも同じ処理を行う) */
#define PX4_CARD_POWER_OFF	_IO(0x8d, 0x41)
/* カードがあれば 1 、なければ 0 を返す (電源を入れていなくても使える) */
#define PX4_CARD_DETECT		_IOR(0x8d, 0x42, __u32)
/* カードをリセットし、UART を ATR の受信速度 (9600bps) へ戻す */
#define PX4_CARD_RESET		_IO(0x8d, 0x43)
/* UART の速度を bps で渡す (9600 、19200 、38400 のいずれか) */
#define PX4_CARD_SET_BAUDRATE	_IOW(0x8d, 0x44, __u32)
/* カードの応答を受信し終えていれば 1 、まだなら 0 を返す */
#define PX4_CARD_RX_READY	_IOR(0x8d, 0x45, __u32)
#define PX4_CARD_READ		_IOWR(0x8d, 0x46, struct px4_card_frame)
#define PX4_CARD_WRITE		_IOW(0x8d, 0x47, struct px4_card_frame)

#endif
