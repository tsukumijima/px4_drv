// device_base.cpp

#include "device_base.hpp"

#include <cstddef>
#include <cinttypes>
#include <cwchar>
#include <vector>

#include "msg.h"
#include "command.hpp"

namespace px4 {

namespace {

std::uint64_t HashDevicePath(const std::wstring &path) noexcept
{
	/* VID、PID、個体情報を含むパス全体から、表示用の固定長 ID を生成する */
	std::uint64_t hash = 14695981039346656037ULL;
	for (wchar_t character : path) {
		hash ^= static_cast<std::uint16_t>(character);
		hash *= 1099511628211ULL;
	}
	return hash;
}

} // namespace

DeviceBase::DeviceBase(const std::wstring &path, const px4::DeviceDefinition &device_def, std::uintptr_t index, px4::ReceiverManager &receiver_manager)
	: device_def_(device_def),
	receiver_manager_(receiver_manager)
{
	strncpy_s(dev_.driver_name, "px4_winusb", sizeof("px4_winusb"));
	sprintf_s(dev_.device_name, "%" PRIuPTR, index);
	wchar_t reader_id[17] = {};
	swprintf_s(reader_id, L"%016llX",
		static_cast<unsigned long long>(HashDevicePath(path)));
	card_reader_name_ = device_def_.name + L" Smart Card Reader (ID: " +
		reader_id + L")";

	usb_dev_.winusb = nullptr;

	usb_dev_.dev = CreateFileW(
		path.c_str(),
		GENERIC_WRITE | GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		nullptr);
	if (usb_dev_.dev == INVALID_HANDLE_VALUE)
		throw DeviceError("px4::DeviceBase::DeviceBase: CreateFileW() failed.");

	if (!WinUsb_Initialize(usb_dev_.dev, &usb_dev_.winusb)) {
		CloseHandle(usb_dev_.dev);
		throw DeviceError("px4::DeviceBase::DeviceBase: WinUsb_Initialize() failed.");
	}

	ULONG size;

	if (!WinUsb_GetDescriptor(
		usb_dev_.winusb,
		USB_DEVICE_DESCRIPTOR_TYPE,
		0x00,
		0x0000,
		reinterpret_cast<PUCHAR>(&usb_dev_.descriptor),
		sizeof(usb_dev_.descriptor),
		&size)
	) {
		WinUsb_Free(usb_dev_.winusb);
		CloseHandle(usb_dev_.dev);
		throw DeviceError("px4::DeviceBase::DeviceBase: WinUsb_GetDescriptor(USB_DEVICE_DESCRIPTOR_TYPE) failed.");
	}

	if (usb_dev_.descriptor.iSerialNumber) {
		const ULONG buffer_size = offsetof(USB_STRING_DESCRIPTOR, bString) +
			(sizeof(wchar_t) * MAXIMUM_USB_STRING_LENGTH);
		std::vector<std::uint8_t> descriptor_buffer(buffer_size);

		if (!WinUsb_GetDescriptor(
			usb_dev_.winusb,
			USB_STRING_DESCRIPTOR_TYPE,
			usb_dev_.descriptor.iSerialNumber,
			0x0409,
			descriptor_buffer.data(),
			buffer_size,
			&size)
		) {
			WinUsb_Free(usb_dev_.winusb);
			CloseHandle(usb_dev_.dev);
			throw DeviceError("px4::DeviceBase::DeviceBase: WinUsb_GetDescriptor(USB_STRING_DESCRIPTOR_TYPE) failed.");
		}

		// USB 文字列は終端文字を持たないため、実際に返された記述子長だけを文字列として保持する
		const auto *serial_descriptor = reinterpret_cast<const USB_STRING_DESCRIPTOR *>(descriptor_buffer.data());
		const ULONG header_size = offsetof(USB_STRING_DESCRIPTOR, bString);
		if ((size < header_size) ||
			(serial_descriptor->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE) ||
			(serial_descriptor->bLength < header_size) ||
			(serial_descriptor->bLength > size) ||
			(((serial_descriptor->bLength - header_size) % sizeof(wchar_t)) != 0)) {
			WinUsb_Free(usb_dev_.winusb);
			CloseHandle(usb_dev_.dev);
			throw DeviceError("px4::DeviceBase::DeviceBase: Invalid USB serial string descriptor.");
		}

		usb_serial_number_.assign(
			serial_descriptor->bString,
			(serial_descriptor->bLength - header_size) / sizeof(wchar_t));
	}
}

DeviceBase::~DeviceBase()
{
	if (usb_dev_.winusb) {
		WinUsb_Free(usb_dev_.winusb);
		usb_dev_.winusb = nullptr;
	}

	if (usb_dev_.dev != INVALID_HANDLE_VALUE) {
		CloseHandle(usb_dev_.dev);
		usb_dev_.dev = INVALID_HANDLE_VALUE;
	}
}

} // namespace px4
