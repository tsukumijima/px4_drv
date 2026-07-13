#pragma once

#include <cstdint>

#include "it930x.h"

namespace px4 {

class CardDevice {
public:
	virtual ~CardDevice() = default;

	virtual int OpenCard() = 0;
	virtual void CloseCard() = 0;
	virtual int DetectCard(bool &detected) = 0;
	virtual int ResetCard() = 0;
	virtual int SetCardBaudrate(::it930x_uart_baudrate baudrate) = 0;
	/* ready は UART に完全なカード応答が揃った場合だけ true を返す */
	virtual int IsCardDataReady(bool &ready) = 0;
	/* len は呼び出し前に buf の容量、成功後に実際の読み取り長を表す */
	virtual int ReadCardData(std::uint8_t *buf, std::uint8_t &len) = 0;
	/* len は buf から送信するバイト数を表す */
	virtual int WriteCardData(const std::uint8_t *buf, std::uint8_t len) = 0;
};

} // namespace px4
