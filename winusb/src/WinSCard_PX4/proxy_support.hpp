#pragma once

#include <windows.h>
#include <winscard.h>
#include <string>

namespace px4::winscard {

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
 * 現在接続されている PX4 内蔵カードリーダーの名前か確認する
 * @param reader 確認するリーダー名
 * @returns PX4 内蔵カードリーダーの場合は true
 */
bool IsPx4ReaderName(const std::wstring &reader) noexcept;

} // namespace px4::winscard
