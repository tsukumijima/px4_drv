// SPDX-License-Identifier: GPL-2.0-only
#include "linux_card_device.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

namespace px4 {

LinuxCardDevice::~LinuxCardDevice()
{
	Disconnect();
}

int LinuxCardDevice::Connect(const std::string &path)
{
	if (fd_ >= 0)
		return -EALREADY;
	fd_ = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (fd_ < 0)
		return -errno;
	px4_card_info info{};
	int ret = ioctl(fd_, PX4_CARD_GET_INFO, &info) < 0 ? -errno : 0;
	if (!ret && (info.abi_version != PX4_CARD_ABI_VERSION ||
		     info.max_frame != PX4_CARD_MAX_FRAME))
		ret = -EPROTONOSUPPORT;
	if (ret)
		Disconnect();
	return ret;
}

void LinuxCardDevice::Disconnect() noexcept
{
	if (fd_ >= 0) {
		/* close の EINTR 後に再試行すると、別の fd を閉じる可能性がある。 */
		close(fd_);
		fd_ = -1;
	}
	deadline_ns_ = 0;
}

void LinuxCardDevice::SetIoDeadline(std::chrono::steady_clock::time_point deadline)
{
	/* steady_clock の epoch を直接使うと、カーネル側と期限の基準がずれる可能性がある。 */
	const auto remaining = deadline - std::chrono::steady_clock::now();
	timespec now{};
	clock_gettime(CLOCK_MONOTONIC, &now);
	const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
	deadline_ns_ = std::uint64_t(now.tv_sec) * 1000000000ULL + now.tv_nsec;
	if (ns > 0)
		deadline_ns_ += std::uint64_t(ns);
}

int LinuxCardDevice::Request(unsigned long command, px4_card_io &io)
{
	if (fd_ < 0)
		return -ENODEV;
	io.deadline_ns = deadline_ns_;
	/* EINTR 時の再実行は、完了済みの USB 操作を二重に発行する恐れがある。 */
	return ioctl(fd_, command, &io) < 0 ? -errno : 0;
}

int LinuxCardDevice::OpenCard()
{
	px4_card_io io{};
	io.value = 1;
	return Request(PX4_CARD_POWER, io);
}

int LinuxCardDevice::PowerOff()
{
	/* 通信失敗時の期限を流用すると、電源解放の ioctl も期限切れで拒否される。 */
	SetIoDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(3));
	px4_card_io io{};
	return Request(PX4_CARD_POWER, io);
}

void LinuxCardDevice::CloseCard()
{
	PowerOff();
}

int LinuxCardDevice::DetectCard(bool &detected)
{
	px4_card_io io{};
	int ret = Request(PX4_CARD_DETECT, io);
	detected = !ret && io.value != 0;
	return ret;
}

int LinuxCardDevice::ResetCard()
{
	px4_card_io io{};
	return Request(PX4_CARD_RESET, io);
}

int LinuxCardDevice::SetCardBaudrate(it930x_uart_baudrate baudrate)
{
	px4_card_io io{};
	switch (baudrate) {
	case IT930X_UART_BAUDRATE_9600: io.value = 9600; break;
	case IT930X_UART_BAUDRATE_19200: io.value = 19200; break;
	case IT930X_UART_BAUDRATE_38400: io.value = 38400; break;
	default: return -EINVAL;
	}
	return Request(PX4_CARD_SET_BAUDRATE, io);
}

int LinuxCardDevice::IsCardDataReady(bool &ready)
{
	px4_card_io io{};
	int ret = Request(PX4_CARD_RX_READY, io);
	ready = !ret && io.value != 0;
	return ret;
}

int LinuxCardDevice::ReadCardData(std::uint8_t *buf, std::uint8_t &len)
{
	if (!buf || !len)
		return -EINVAL;
	px4_card_io io{};
	io.length = len;
	int ret = Request(PX4_CARD_READ, io);
	if (ret)
		return ret;
	if (io.length > len)
		return -EBADMSG;
	std::memcpy(buf, io.data, io.length);
	len = static_cast<std::uint8_t>(io.length);
	return 0;
}

int LinuxCardDevice::WriteCardData(const std::uint8_t *buf, std::uint8_t len)
{
	if (!buf || !len)
		return -EINVAL;
	px4_card_io io{};
	io.length = len;
	std::memcpy(io.data, buf, len);
	return Request(PX4_CARD_WRITE, io);
}

static bool ReadNumber(const std::filesystem::path &path, unsigned int &value,
		       bool hex = false)
{
	std::ifstream stream(path);
	if (hex)
		stream >> std::hex;
	return bool(stream >> value);
}

int ResolveCardDevice(const std::string &name, std::string &path,
		      const std::string &sysfs, const std::string &devdir)
{
	if (!name.empty() && name[0] == '/') {
		path = name;
		return 0;
	}
	unsigned int vendor, product, interface, bus, device;
	int end = 0;
	if (std::sscanf(name.c_str(), "usb:%x/%x:libudev:%u:/dev/bus/usb/%u/%u%n",
		&vendor, &product, &interface, &bus, &device, &end) != 5 ||
	    end != static_cast<int>(name.size()) || interface != 0)
		return -EINVAL;

	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(sysfs, ec)) {
		const auto filename = entry.path().filename().string();
		if (filename.compare(0, 7, "px4card") != 0)
			continue;
		const auto parent = std::filesystem::canonical(entry.path() / "device", ec);
		if (ec)
			continue;
		const auto usb = parent.parent_path();
		unsigned int v, p, b, d;
		if (ReadNumber(usb / "idVendor", v, true) &&
		    ReadNumber(usb / "idProduct", p, true) &&
		    ReadNumber(usb / "busnum", b) && ReadNumber(usb / "devnum", d) &&
		    v == vendor && p == product && b == bus && d == device) {
			path = (std::filesystem::path(devdir) / filename).string();
			return 0;
		}
	}
	return -ENODEV;
}

} // namespace px4
