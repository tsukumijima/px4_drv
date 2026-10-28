// SPDX-License-Identifier: GPL-2.0-only

#pragma once

/*
 * ISDB-C の物理チャンネル番号 (recpt1 と同じ 0〜112) を中心周波数 (kHz) に変換し、範囲外の番号には -1 を返す
 * Linux 版と WinUSB 版の BonDriver でこの表を共有し、同じ番号を両方で同じ周波数へ合わせる
 * 地上波と異なり、中心周波数に 1/7MHz を加えない
 */
static inline int isdbc_channel_to_khz(int ch)
{
	/* VHF 1-3ch, CATV C13-22ch, VHF 4-12ch, CATV C23-63ch */
	if (ch >= 0 && ch <= 62) {
		/* ARIB STD-B21 付属10 のとおり、帯域が 2MHz ずれている C22ch と VHF 4-7ch だけ 2MHz を加える */
		if (ch >= 12 && ch <= 16)
			return 93000 + ch * 6000 + 2000;

		return 93000 + ch * 6000;
	}

	/* UHF 13-62ch */
	if (ch >= 63 && ch <= 112)
		return 95000 + ch * 6000;

	return -1;
}
