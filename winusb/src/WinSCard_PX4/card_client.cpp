#include "card_client.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

#include <windows.h>

namespace px4 {

namespace {

void ModuleAnchor() {}

} // namespace

bool CardClient::StartDriverHost() noexcept
{
	HANDLE startup_event = CreateEventW(nullptr, TRUE, FALSE,
		L"Global\\DriverHost_PX4_StartupEvent");
	if (!startup_event)
		return false;

	if (GetLastError() != ERROR_ALREADY_EXISTS) {
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&ModuleAnchor),
			&module)) {
			CloseHandle(startup_event);
			return false;
		}

		std::vector<wchar_t> module_path(MAX_PATH);
		DWORD module_path_length = 0;
		while (true) {
			module_path_length = GetModuleFileNameW(module, module_path.data(),
				static_cast<DWORD>(module_path.size()));
			if (!module_path_length) {
				CloseHandle(startup_event);
				return false;
			}
			/* バッファ長と同じ戻り値は切り詰めを示すため、完全なパスを再取得する */
			if (module_path_length < module_path.size())
				break;
			if (module_path.size() >= 32768) {
				CloseHandle(startup_event);
				return false;
			}
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
		if (host_path.empty()) {
			CloseHandle(startup_event);
			return false;
		}

		STARTUPINFOW startup_info = {};
		PROCESS_INFORMATION process_info = {};
		startup_info.cb = sizeof(startup_info);

		if (!CreateProcessW(host_path.c_str(), nullptr, nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW, nullptr, host_path.parent_path().c_str(),
			&startup_info, &process_info)) {
			CloseHandle(startup_event);
			return false;
		}

		CloseHandle(process_info.hThread);
		CloseHandle(process_info.hProcess);
	}

	DWORD wait_result = WaitForSingleObject(startup_event, 10000);
	CloseHandle(startup_event);
	return wait_result == WAIT_OBJECT_0;
}

bool CardClient::Connect() noexcept
{
	auto pipe = std::make_unique<PipeClient>();
	PipeClient::PipeClientConfig config = {};
	config.stream_read = false;
	config.timeout = 1000;

	if (!pipe->Connect(L"px4_card_pipe", config, nullptr)) {
		if (!StartDriverHost())
			return false;
		if (!pipe->Connect(L"px4_card_pipe", config, nullptr))
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

bool CardClient::Call(card_command::Command &command) noexcept
{
	if (!pipe_)
		return false;

	command.status = card_command::Status::REQUEST;
	command.error = card_command::Error::NONE;
	return pipe_->Call(&command, sizeof(command));
}

} // namespace px4
