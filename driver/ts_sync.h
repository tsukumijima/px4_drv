// SPDX-License-Identifier: GPL-2.0-only

#pragma once

/* 通常 TS の同期バイトを副作用なしで判定する */
static inline int px4_ts_has_plain_sync(unsigned char value)
{
	return value == 0x47;
}

/* 多重 TS は上位ビットのチューナー番号を除いて同期値を判定する */
static inline int px4_ts_has_tagged_sync(unsigned char value)
{
	return (value & 0x8f) == 0x07;
}
