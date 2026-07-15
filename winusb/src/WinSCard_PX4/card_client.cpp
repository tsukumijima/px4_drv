#include "card_client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include <windows.h>

namespace px4 {

namespace {

void ModuleAnchor() {}

} // namespace

bool CardClient::StartDriverHost() noexcept try
{
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&ModuleAnchor), &module))
		return false;

	std::vector<wchar_t> module_path(MAX_PATH);
	DWORD module_path_length = 0;
	while (true) {
		module_path_length = GetModuleFileNameW(module, module_path.data(),
			static_cast<DWORD>(module_path.size()));
		if (!module_path_length)
			return false;
		/* バッファ長と同じ戻り値は切り詰めを示すため、完全なパスを再取得する */
		if (module_path_length < module_path.size())
			break;
		if (module_path.size() >= 32768)
			return false;
		module_path.resize(std::min<std::size_t>(
			module_path.size() * 2, 32768));
	}

	const std::filesystem::path module_directory =
		std::filesystem::path(module_path.data(),
			module_path.data() + module_path_length).parent_path();
	const std::array<std::filesystem::path, 2> host_candidates = {
		module_directory / L"DriverHost_PX4.exe",
		module_directory / L"BonDriver" / L"DriverHost_PX4.exe",
	};
	std::filesystem::path host_path;

	/* TVTest や EDCB で一般的な BonDriver サブフォルダも検索する */
	for (const auto &candidate : host_candidates) {
		std::error_code error;
		if (std::filesystem::is_regular_file(candidate, error)) {
			host_path = candidate;
			break;
		}
	}
	if (host_path.empty())
		return false;

	STARTUPINFOW startup_info = {};
	PROCESS_INFORMATION process_info = {};
	startup_info.cb = sizeof(startup_info);

	/* パイプが存在しない場合は起動を要求し、多重起動は DriverHost のミューテックスへ任せる */
	if (!CreateProcessW(host_path.c_str(), nullptr, nullptr, nullptr,
		FALSE, CREATE_NO_WINDOW, nullptr, host_path.parent_path().c_str(),
		&startup_info, &process_info))
		return false;

	CloseHandle(process_info.hThread);
	CloseHandle(process_info.hProcess);
	return true;
}
catch (...) {
	return false;
}

bool CardClient::Connect() noexcept try
{
	auto pipe = std::make_unique<PipeClient>();
	PipeClient::PipeClientConfig config = {};
	config.stream_read = false;
	config.timeout = 1000;

	if (!pipe->Connect(L"px4_card_pipe", config, nullptr)) {
		if (!StartDriverHost())
			return false;

		/* 起動イベントより実際のカード用パイプを準備完了の判定に使う */
		const auto deadline = std::chrono::steady_clock::now() +
			std::chrono::seconds(10);
		bool is_connected = false;
		do {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			if (pipe->Connect(L"px4_card_pipe", config, nullptr)) {
				is_connected = true;
				break;
			}
		} while (std::chrono::steady_clock::now() < deadline);
		if (!is_connected)
			return false;
	}

	pipe_ = std::move(pipe);
	card_command::Command command = {};
	command.code = card_command::Code::GET_VERSION;
	if (!Call(command) || command.version != card_command::VERSION) {
		pipe_.reset();
		return false;
	}

	return true;
}
catch (...) {
	pipe_.reset();
	return false;
}

bool CardClient::Call(card_command::Command &command) noexcept
{
	if (!pipe_)
		return false;

	command.status = card_command::Status::REQUEST;
	command.error = card_command::Error::NONE;
	return pipe_->Call(&command, sizeof(command));
}

} // namespace px4
