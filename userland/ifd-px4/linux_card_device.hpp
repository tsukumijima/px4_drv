// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include "card_device.hpp"
#include "px4_card_ioctl.h"

namespace px4 {

class LinuxCardDevice final : public CardDevice {
public:
	~LinuxCardDevice() override;
	int Connect(const std::string &path);
	void Disconnect() noexcept;
	void SetIoDeadline(std::chrono::steady_clock::time_point deadline) override;
	int OpenCard() override;
	void CloseCard() override;
	int PowerOff();
	int DetectCard(bool &detected) override;
	int ResetCard() override;
	int SetCardBaudrate(it930x_uart_baudrate baudrate) override;
	int IsCardDataReady(bool &ready) override;
	int ReadCardData(std::uint8_t *buf, std::uint8_t &len) override;
	int WriteCardData(const std::uint8_t *buf, std::uint8_t len) override;

private:
	int Request(unsigned long command, px4_card_io &io);
	int fd_ = -1;
	std::uint64_t deadline_ns_ = 0;
};

/* VID/PID だけでは同型機を区別できないため、sysfs の親 USB 機器まで照合する。 */
int ResolveCardDevice(const std::string &name, std::string &path,
		      const std::string &sysfs = "/sys/class/misc",
		      const std::string &devdir = "/dev");

} // namespace px4
