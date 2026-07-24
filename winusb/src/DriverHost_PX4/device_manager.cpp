// device_manager.cpp

#include "device_manager.hpp"

#include <algorithm>

#include <windows.h>
#include <setupapi.h>

#include "msg.h"
#include "px4_device.hpp"
#include "pxmlt_device.hpp"
#include "isdb2056_device.hpp"
#include "isdbt2071_device.hpp"

namespace px4 {

void DeviceManager::NotifyHandler::Handle(px4::DeviceNotifyType type, const GUID &interface_guid, const wchar_t *path) noexcept
{
	try {
		std::wstring path_lower = path;

		std::transform(path_lower.cbegin(), path_lower.cend(), path_lower.begin(), tolower);

		switch (type) {
		case px4::DeviceNotifyType::ARRIVAL:
			parent_.Add(path_lower, parent_.device_map_.at(interface_guid));
			break;

		case px4::DeviceNotifyType::REMOVE:
			parent_.Remove(path_lower);
			break;
		}
	} catch (const std::out_of_range &) {}
}

DeviceManager::DeviceManager(const px4::DeviceDefinitionSet &device_defs, px4::ReceiverManager &receiver_manager)
	: device_map_(),
	receiver_manager_(receiver_manager),
	mtx_(),
	index_(0),
	card_reader_generation_(1),
	handler_(*this)
{
	auto &all_devs = device_defs.GetAll();

	for (auto it = all_devs.cbegin(); it != all_devs.cend(); ++it) {
		DeviceType type = DeviceType::UNKNOWN;

		if (it->first == L"PX4")
			type = DeviceType::PX4;
		else if (it->first == L"PXMLT")
			type = DeviceType::PXMLT;
		else if (it->first == L"ISDB2056")
			type = DeviceType::ISDB2056;
		else if (it->first == L"ISDBT2071")
			type = DeviceType::ISDBT2071;

		if (type == DeviceType::UNKNOWN)
			continue;

		auto &devs = it->second;

		for (auto it2 = devs.begin(); it2 != devs.end(); ++it2)
			device_map_.emplace(it2->device_interface_guid, std::move(std::pair<DeviceType, px4::DeviceDefinition>(type, *it2)));
	}

	notifier_.reset(new px4::DeviceNotifier(&handler_));

	for (auto it = device_map_.cbegin(); it != device_map_.cend(); ++it)
		Search(it->first, it->second);
}

DeviceManager::~DeviceManager()
{
	notifier_.reset();
}

void DeviceManager::Search(const GUID &guid, const std::pair<DeviceType, px4::DeviceDefinition> &def)
{
	HDEVINFO dev_info;

	dev_info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (dev_info == INVALID_HANDLE_VALUE)
		throw DeviceManagerError("px4::DeviceManager::Search: SetupDiGetClassDevsW() failed.");

	SP_DEVICE_INTERFACE_DATA intf_data;

	intf_data.cbSize = sizeof(intf_data);

	/* 途中で読み飛ばす場合も列挙位置を進めるため、増分を for 文へ持たせる */
	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(dev_info, nullptr, &guid, i, &intf_data); i++) {
		DWORD detail_size = 0;
		SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail_data;

		/*
		 * サイズ問い合わせは必ず失敗し、必要な長さは ERROR_INSUFFICIENT_BUFFER のときだけ返る
		 * 列挙直後の抜去などで別のエラーになった場合は長さが更新されないため、その機器を飛ばす
		 */
		SetupDiGetDeviceInterfaceDetailW(dev_info, &intf_data, nullptr, 0, &detail_size, nullptr);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !detail_size)
			continue;

		detail_data = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(new std::uint8_t[detail_size]);
		detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (SetupDiGetDeviceInterfaceDetailW(dev_info, &intf_data, detail_data, detail_size, nullptr, nullptr)) {
			std::wstring path_lower = detail_data->DevicePath;

			std::transform(path_lower.cbegin(), path_lower.cend(), path_lower.begin(), tolower);
			Add(path_lower, def);
		}

		delete[] reinterpret_cast<std::uint8_t *>(detail_data);
	}

	SetupDiDestroyDeviceInfoList(dev_info);
}


void DeviceManager::Add(const std::wstring &path, const std::pair<DeviceType, px4::DeviceDefinition> &def)
{
	std::lock_guard<std::mutex> lock(mtx_);

	if (Exists(path))
		return;

	/*
	 * DeviceBase のコンストラクタは機器を開けない場合に DeviceError を送出する
	 * 到着通知の Handle() は noexcept のため、ここで捕まえないと1台の初期化失敗で
	 * DriverHost_PX4 全体が終了し、進行中の受信も巻き添えになる
	 */
	try {

	/* 機種追加時に型と DeviceType の対応をこの場で確認できるよう生成分岐を明示する */
	switch (def.first) {
	case px4::DeviceType::PX4:
	{
		auto dev = std::make_shared<Px4Device>(path, def.second, ++index_, receiver_manager_);

		if (!dev->Init()) {
			if (dev->HasCardReader())
				card_reader_generation_.fetch_add(1);
			devices_.emplace(path, std::move(dev));
		}

		break;
	}

	case px4::DeviceType::PXMLT:
	{
		auto dev = std::make_shared<PxMltDevice>(path, def.second, ++index_, receiver_manager_);

		if (!dev->Init()) {
			if (dev->HasCardReader())
				card_reader_generation_.fetch_add(1);
			devices_.emplace(path, std::move(dev));
		}

		break;
	}

	case px4::DeviceType::ISDB2056:
	{
		auto dev = std::make_shared<Isdb2056Device>(path, def.second, ++index_, receiver_manager_);

		if (!dev->Init()) {
			if (dev->HasCardReader())
				card_reader_generation_.fetch_add(1);
			devices_.emplace(path, std::move(dev));
		}

		break;
	}

	case px4::DeviceType::ISDBT2071:
	{
		auto dev = std::make_shared<Isdbt2071Device>(path, def.second, ++index_, receiver_manager_);

		if (!dev->Init()) {
			if (dev->HasCardReader())
				card_reader_generation_.fetch_add(1);
			devices_.emplace(path, std::move(dev));
		}

		break;
	}

	default:
		break;
	}

	} catch (const DeviceError &ex) {
		/* 失敗した1台だけを登録対象から外し、他の機器の列挙と受信は継続する */
		msg_err("px4::DeviceManager::Add: %s\n", ex.what());
	}

	return;
}

void DeviceManager::Remove(const std::wstring &path)
{
	std::lock_guard<std::mutex> lock(mtx_);

	if (!Exists(path))
		return;

	auto device = devices_.at(path);
	device->SetAvailability(false);
	if (device->HasCardReader())
		card_reader_generation_.fetch_add(1);
	devices_.erase(path);
}

bool DeviceManager::Exists(const std::wstring &path) const
{
	return !!devices_.count(path);
}

std::vector<std::wstring> DeviceManager::ListCardReaders() const
{
	std::vector<std::wstring> readers;
	std::lock_guard<std::mutex> lock(mtx_);

	/* ホットプラグ中に消えるデバイス名を呼び出し元へ公開しない */
	for (const auto &entry : devices_) {
		if (entry.second->HasCardReader())
			readers.emplace_back(entry.second->GetCardReaderName());
	}

	std::sort(readers.begin(), readers.end());
	return readers;
}

std::shared_ptr<DeviceBase> DeviceManager::FindCardReader(const std::wstring &name) const
{
	std::lock_guard<std::mutex> lock(mtx_);

	for (const auto &entry : devices_) {
		if (entry.second->HasCardReader() && entry.second->GetCardReaderName() == name)
			return entry.second;
	}

	return nullptr;
}

std::uint64_t DeviceManager::GetCardReaderGeneration() const noexcept
{
	return card_reader_generation_.load();
}

} // namespace px4
