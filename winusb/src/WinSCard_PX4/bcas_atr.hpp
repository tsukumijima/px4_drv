#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include <windows.h>

namespace px4::winscard {

/* B-CAS として登録済みの2種類は履歴バイトが異なり、末尾の TCK も連動して変わる (TODO: 本当かどうか不明だが実際そうらしい) */
inline constexpr std::array<std::array<BYTE, 13>, 2> BCAS_ATRS = {{
	{ 0x3b, 0xf0, 0x12, 0x00, 0xff, 0x91, 0x81, 0xb1,
		0x7c, 0x45, 0x1f, 0x01, 0x9b },
	{ 0x3b, 0xf0, 0x12, 0x00, 0xff, 0x91, 0x81, 0xb1,
		0x7c, 0x45, 0x1f, 0x03, 0x99 },
}};

/**
 * ATR が既知の B-CAS と完全一致するか確認する
 * @param atr 確認する ATR
 * @param atr_length ATR のバイト数
 * @returns B-CAS の ATR と一致する場合は true
 */
inline bool IsBcasAtr(const BYTE *atr, std::size_t atr_length) noexcept
{
	if (!atr || atr_length != BCAS_ATRS.front().size())
		return false;
	return std::any_of(BCAS_ATRS.begin(), BCAS_ATRS.end(),
		[atr](const auto &candidate) {
			return std::equal(candidate.begin(), candidate.end(), atr);
		});
}

} // namespace px4::winscard
