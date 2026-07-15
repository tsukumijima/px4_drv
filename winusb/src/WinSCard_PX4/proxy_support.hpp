#pragma once

#include <windows.h>
#include <winscard.h>
#include <cstring>
#include <string>
#include <vector>

namespace px4::winscard {

inline constexpr wchar_t PNP_NOTIFICATION[] = L"\\\\?PnP?\\Notification";

/**
 * プロキシのコンテキストから Windows 標準 WinSCard のコンテキストを取得する
 * @param context プロキシが返したコンテキスト
 * @param native_context Windows 標準側のコンテキストの格納先
 * @returns プロキシのコンテキストが有効な場合は true
 */
bool GetNativeContext(SCARDCONTEXT context,
	SCARDCONTEXT &native_context) noexcept;

/**
 * プロキシ自身が確保したメモリを SCardFreeMemory() で識別できるよう登録する
 * @param allocation LocalAlloc() で確保したメモリ
 */
bool RegisterProxyAllocation(LPCVOID allocation) noexcept;

/**
 * ANSI 文字列を Windows の現在のコードページから UTF-16 へ変換する
 * @param value 変換する文字列
 * @returns 変換後の文字列 (変換できない場合は空文字列)
 */
std::wstring ToWide(LPCSTR value);

/**
 * UTF-16 のリーダー名を共通の文字列型へコピーする
 * @param value コピーする文字列
 * @returns コピーした文字列 (nullptr の場合は空文字列)
 */
std::wstring ToWide(LPCWSTR value);

/**
 * UTF-16 文字列を Windows の現在のコードページへ変換する
 * @param value 変換する文字列
 * @returns 変換後の文字列 (変換できない場合は空文字列)
 */
std::string ToAnsi(const std::wstring &value);

/**
 * WinSCard API の文字列出力規則に従って文字列を返す
 * @param value 返す文字列
 * @param buffer 呼び出し元のバッファまたは自動確保したポインタの格納先
 * @param length 呼び出し前はバッファ容量、呼び出し後は必要な文字数
 * @returns WinSCard API の結果コード
 */
template <typename Char>
LONG CopyString(const std::basic_string<Char> &value, Char *buffer,
	LPDWORD length)
{
	if (!length)
		return SCARD_E_INVALID_PARAMETER;
	const DWORD required = static_cast<DWORD>(value.size() + 1);
	const DWORD supplied = *length;
	*length = required;

	/* 自動確保した領域は SCardFreeMemory() で解放できるよう登録する */
	if (supplied == SCARD_AUTOALLOCATE) {
		if (!buffer)
			return SCARD_E_INVALID_PARAMETER;
		auto allocation = static_cast<Char *>(LocalAlloc(LMEM_FIXED,
			required * sizeof(Char)));
		if (!allocation)
			return SCARD_E_NO_MEMORY;
		std::memcpy(allocation, value.c_str(), required * sizeof(Char));
		if (!RegisterProxyAllocation(allocation)) {
			LocalFree(allocation);
			return SCARD_E_NO_MEMORY;
		}
		*reinterpret_cast<Char **>(buffer) = allocation;
		return SCARD_S_SUCCESS;
	}
	if (!buffer)
		return SCARD_S_SUCCESS;
	if (supplied < required)
		return SCARD_E_INSUFFICIENT_BUFFER;
	std::memcpy(buffer, value.c_str(), required * sizeof(Char));
	return SCARD_S_SUCCESS;
}

/**
 * WinSCard API の MULTI_SZ 出力規則に従って文字列一覧を返す
 * @param values 返す文字列一覧
 * @param buffer 呼び出し元のバッファまたは自動確保したポインタの格納先
 * @param length 呼び出し前はバッファ容量、呼び出し後は必要な文字数
 * @returns WinSCard API の結果コード
 */
template <typename Char>
LONG CopyMultiString(const std::vector<std::basic_string<Char>> &values,
	Char *buffer, LPDWORD length)
{
	if (!length)
		return SCARD_E_INVALID_PARAMETER;
	DWORD required = 1;
	for (const auto &value : values)
		required += static_cast<DWORD>(value.size() + 1);
	const DWORD supplied = *length;
	*length = required;
	Char *destination = buffer;

	/* MULTI_SZ も通常の文字列と同じ所有権規則で自動確保する */
	if (supplied == SCARD_AUTOALLOCATE) {
		if (!buffer)
			return SCARD_E_INVALID_PARAMETER;
		destination = static_cast<Char *>(LocalAlloc(LMEM_FIXED,
			required * sizeof(Char)));
		if (!destination)
			return SCARD_E_NO_MEMORY;
		if (!RegisterProxyAllocation(destination)) {
			LocalFree(destination);
			return SCARD_E_NO_MEMORY;
		}
		*reinterpret_cast<Char **>(buffer) = destination;
	} else if (!buffer) {
		return SCARD_S_SUCCESS;
	} else if (supplied < required) {
		return SCARD_E_INSUFFICIENT_BUFFER;
	}

	for (const auto &value : values) {
		std::memcpy(destination, value.c_str(),
			(value.size() + 1) * sizeof(Char));
		destination += value.size() + 1;
	}
	*destination = 0;
	return SCARD_S_SUCCESS;
}

/**
 * 現在接続されている PX4 内蔵カードリーダーの名前か確認する
 * @param reader 確認するリーダー名
 * @returns PX4 内蔵カードリーダーの場合は true
 */
bool IsPx4ReaderName(const std::wstring &reader) noexcept;

/**
 * 現在接続されている PX4 内蔵カードリーダー名を取得する
 * @param readers リーダー名の格納先
 * @returns WinSCard API の結果コード
 */
LONG GetPx4ReaderNames(std::vector<std::wstring> &readers) noexcept;

} // namespace px4::winscard
