#include "native_winscard.hpp"

#include <filesystem>
#include <mutex>
#include <vector>

namespace px4::winscard {

namespace {

HMODULE native_module = nullptr;
std::once_flag native_module_once;

void LoadNativeModule() noexcept
{
	try {
		HMODULE proxy_module = nullptr;

		/*
		 * System32 版より先に読み込まれたプロキシ DLL をホストプロセスの終了まで維持する
		 * BonDriverProxyEx は最後のクライアントが切断されると B25Decoder.dll ごと解放するため、
		 * System32 版だけが残ると次回の B25Decoder.dll が同名の System32 版へ結び付く
		 */
		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_PIN,
			reinterpret_cast<LPCWSTR>(&LoadNativeModule), &proxy_module))
			return;

		std::vector<wchar_t> system_directory(MAX_PATH);
		UINT length = GetSystemDirectoryW(system_directory.data(),
			static_cast<UINT>(system_directory.size()));
		if (!length)
			return;

		/* 長い Windows ディレクトリでも切り詰めたパスを読み込まない */
		if (length >= system_directory.size()) {
			system_directory.resize(static_cast<std::size_t>(length) + 1);
			length = GetSystemDirectoryW(system_directory.data(),
				static_cast<UINT>(system_directory.size()));
			if (!length || length >= system_directory.size())
				return;
		}

		/* 絶対パスを使い、同名のプロキシ DLL へ再帰的に戻るのを避ける */
		const std::filesystem::path path =
			std::filesystem::path(system_directory.data()) / L"WinSCard.dll";
		native_module = LoadLibraryW(path.c_str());
	} catch (...) {
		/* Windows 標準側を利用できなくても PX4 内蔵リーダーは継続する */
		native_module = nullptr;
	}
}

} // namespace

FARPROC GetNativeFunction(const char *name) noexcept
{
	if (!name)
		return nullptr;
	std::call_once(native_module_once, LoadNativeModule);
	return native_module ? GetProcAddress(native_module, name) : nullptr;
}

} // namespace px4::winscard
