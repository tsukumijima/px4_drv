#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <vector>

#include "smart_card.hpp"

namespace {

class MockCardDevice final : public px4::CardDevice {
public:
	int OpenCard() override
	{
		is_open = true;
		return 0;
	}

	void CloseCard() override
	{
		is_open = false;
	}

	int DetectCard(bool &detected) override
	{
		if (detect_error) {
			int error = detect_error;
			detect_error = 0;
			return error;
		}
		detected = is_present;
		return 0;
	}

	int ResetCard() override
	{
		if (!is_present)
			return -ENODEV;
		reset_count++;
		read_queue.clear();
		read_queue.insert(read_queue.end(), atr_bytes.begin(), atr_bytes.end());
		return 0;
	}

	int SetCardBaudrate(::it930x_uart_baudrate) override
	{
		return 0;
	}

	int IsCardDataReady(bool &ready) override
	{
		if (ready_delay_checks) {
			ready_delay_checks--;
			ready = false;
			read_allowed = false;
			return 0;
		}
		ready = !read_queue.empty();
		read_allowed = ready;
		return 0;
	}

	int ReadCardData(std::uint8_t *buffer, std::uint8_t &length) override
	{
		if (!read_allowed)
			read_called_before_ready = true;
		read_allowed = false;
		std::size_t copy_length = std::min<std::size_t>(length, read_queue.size());
		for (std::size_t index = 0; index < copy_length; index++) {
			buffer[index] = read_queue.front();
			read_queue.pop_front();
		}
		length = static_cast<std::uint8_t>(copy_length);
		return 0;
	}

	int WriteCardData(const std::uint8_t *buffer, std::uint8_t length) override
	{
		if (length < 4)
			return -EINVAL;
		const std::uint8_t pcb = buffer[1];
		const std::uint8_t data_length = buffer[2];
		std::size_t expected_length = static_cast<std::size_t>(data_length) +
			(expected_crc ? 5 : 4);
		if (expected_length != length || !ValidateEdc(buffer, length))
			return -EINVAL;

		/* 初期化 S ブロックは要求データをそのまま応答する */
		if ((pcb & 0xc0) == 0xc0) {
			if (endless_wtx && pcb == 0xe3) {
				const std::uint8_t multiplier = 1;
				QueueBlock(0xc3, &multiplier, 1);
				return 0;
			}
			QueueBlock(static_cast<std::uint8_t>(pcb | 0x20),
				buffer + 3, data_length);
			return 0;
		}

		if (fail_next_apdu) {
			fail_next_apdu = false;
			return -EIO;
		}
		if (endless_wtx) {
			const std::uint8_t multiplier = 1;
			QueueBlock(0xc3, &multiplier, 1);
			return 0;
		}
		const std::uint8_t response[] = { 0x90, 0x00 };
		QueueBlock(0x00, response, sizeof(response));
		if (delay_next_response) {
			delay_next_response = false;
			ready_delay_checks = 3;
		}
		return 0;
	}

	void QueueBytes(std::initializer_list<std::uint8_t> bytes)
	{
		read_queue.insert(read_queue.end(), bytes.begin(), bytes.end());
	}

	void QueueBlock(std::uint8_t pcb, const std::uint8_t *data,
		std::size_t data_length)
	{
		std::vector<std::uint8_t> frame = {
			0x00,
			pcb,
			static_cast<std::uint8_t>(data_length),
		};
		frame.insert(frame.end(), data, data + data_length);
		if (expected_crc) {
			std::uint16_t crc = CalculateCrc(frame.data(), frame.size());
			frame.push_back(static_cast<std::uint8_t>(crc >> 8));
			frame.push_back(static_cast<std::uint8_t>(crc & 0xff));
		} else {
			std::uint8_t lrc = 0;
			for (std::uint8_t value : frame)
				lrc ^= value;
			frame.push_back(lrc);
		}
		read_queue.insert(read_queue.end(), frame.begin(), frame.end());
	}

	bool ValidateEdc(const std::uint8_t *frame, std::size_t length) const
	{
		if (expected_crc) {
			std::uint16_t crc = CalculateCrc(frame, length - 2);
			return frame[length - 2] == static_cast<std::uint8_t>(crc >> 8) &&
				frame[length - 1] == static_cast<std::uint8_t>(crc & 0xff);
		}
		std::uint8_t lrc = 0;
		for (std::size_t index = 0; index < length; index++)
			lrc ^= frame[index];
		return lrc == 0;
	}

	static std::uint16_t CalculateCrc(const std::uint8_t *data, std::size_t length)
	{
		std::uint16_t crc = 0xffff;
		for (std::size_t index = 0; index < length; index++) {
			crc ^= static_cast<std::uint16_t>(data[index]) << 8;
			for (unsigned int bit = 0; bit < 8; bit++)
				crc = (crc & 0x8000) ?
					static_cast<std::uint16_t>((crc << 1) ^ 0x1021) :
					static_cast<std::uint16_t>(crc << 1);
		}
		return crc;
	}

	bool is_open = false;
	bool is_present = false;
	bool fail_next_apdu = false;
	bool endless_wtx = false;
	bool delay_next_response = false;
	bool read_called_before_ready = false;
	bool read_allowed = false;
	unsigned int ready_delay_checks = 0;
	int detect_error = 0;
	unsigned int reset_count = 0;
	/* TA2 / TC2 は T=1 パラメータではないため既定の LRC と IFSC を使う */
	std::vector<std::uint8_t> atr_bytes = { 0x3b, 0x80, 0xd1, 0x40, 0x01, 0x01, 0x11 };
	bool expected_crc = false;
	std::deque<std::uint8_t> read_queue;
};

bool Check(bool condition, const char *message)
{
	if (condition)
		return true;
	std::fprintf(stderr, "%s\n", message);
	return false;
}

} // namespace

int main()
{
	auto device = std::make_shared<MockCardDevice>();
	px4::SmartCard card(device);
	bool succeeded = Check(card.Open() == 0 && device->is_open,
		"Opening an empty reader failed.");

	bool present = true;
	bool initialized = true;
	std::vector<std::uint8_t> atr;
	succeeded = Check(card.GetStatus(present, initialized, atr) == 0 &&
		!present && !initialized && atr.empty(),
		"An empty reader returned stale card state.") && succeeded;

	/* 挿入検出時に ATR と T=1 を初期化する */
	device->is_present = true;
	succeeded = Check(card.GetStatus(present, initialized, atr) == 0 &&
		present && initialized && atr.size() == 7 && device->reset_count == 1,
		"Card insertion did not initialize the session.") && succeeded;

	const std::uint8_t apdu[] = { 0x90, 0x30, 0x00, 0x00, 0x00 };
	std::uint8_t response[16] = {};
	std::size_t response_length = sizeof(response);
	/* UART_RX_READY が立つまで途中フレームを読み出さない */
	device->delay_next_response = true;
	device->read_called_before_ready = false;
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == 0 && response_length == 2 &&
		!device->read_called_before_ready,
		"Initial APDU transmission failed.") && succeeded;

	/* 抜去後は古い ATR と連番を破棄する */
	device->is_present = false;
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == px4::SMART_CARD_NO_MEDIUM,
		"Card removal was not reported.") && succeeded;

	/* 再挿入後の最初の APDU でセッションを自動再構築する */
	device->is_present = true;
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == 0 && device->reset_count == 2,
		"Card reinsertion did not recover automatically.") && succeeded;

	/* 一時的な通信失敗も次の APDU まで壊れた連番を持ち越さない */
	device->fail_next_apdu = true;
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == -EIO,
		"Injected transport failure was not returned.") && succeeded;
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == 0 && device->reset_count == 3,
		"Transport failure did not recover on the next APDU.") && succeeded;

	/* 接触不良による検出エラーも例外や状態破損へ変えない */
	device->detect_error = -EIO;
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == -EIO,
		"Card detection failure was not returned.") && succeeded;
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == 0 && device->reset_count == 4,
		"Card detection failure did not recover.") && succeeded;

	/* 正常な WTX が繰り返されても APDU 全体の期限を延長しない */
	device->endless_wtx = true;
	auto wtx_started = std::chrono::steady_clock::now();
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == -ETIMEDOUT &&
		std::chrono::steady_clock::now() - wtx_started < std::chrono::seconds(5),
		"Repeated WTX requests extended the APDU indefinitely.") && succeeded;
	device->endless_wtx = false;
	response_length = sizeof(response);
	succeeded = Check(card.Transmit(apdu, sizeof(apdu), response,
		response_length) == 0 && device->reset_count == 5,
		"WTX timeout did not invalidate the session.") && succeeded;

	/* TD2 が T=1 を示した後の TA3 / TC3 だけから IFSC と CRC を取得する */
	auto crc_device = std::make_shared<MockCardDevice>();
	crc_device->atr_bytes = { 0x3b, 0x80, 0x91, 0x01, 0x51, 0x40, 0x01, 0x00 };
	crc_device->expected_crc = true;
	crc_device->is_present = true;
	px4::SmartCard crc_card(crc_device);
	bool crc_present = false;
	bool crc_initialized = false;
	std::vector<std::uint8_t> crc_atr;
	succeeded = Check(crc_card.Open() == 0 &&
		crc_card.GetStatus(crc_present, crc_initialized, crc_atr) == 0 &&
		crc_present && crc_initialized && crc_atr.size() == 8,
		"TA3 / TC3 CRC parameters were not applied.") && succeeded;
	crc_card.Close();

	card.Close();
	succeeded = Check(!device->is_open, "Closing the reader failed.") && succeeded;
	std::printf("smart_card_state_test: %s\n", succeeded ? "passed" : "failed");
	return succeeded ? 0 : 1;
}
