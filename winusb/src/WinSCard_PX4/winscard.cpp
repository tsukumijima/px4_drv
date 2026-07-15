#define WINSCARDDATA __declspec(dllexport)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <winscard.h>

#include "card_client.hpp"
#include "native_winscard.hpp"
#include "proxy_support.hpp"

namespace {

using px4::winscard::CopyMultiString;
using px4::winscard::ToAnsi;
using px4::winscard::ToWide;

#define PX4_SCARD_CATCH \
	catch (const std::bad_alloc &) { return SCARD_E_NO_MEMORY; } \
	catch (...) { return SCARD_F_INTERNAL_ERROR; }

struct Context final {
	/* 同じコンテキストで待機中の全スレッドが1回の SCardCancel() を観測する */
	std::atomic<std::uint64_t> cancel_generation{ 0 };
	/* System32 へ直接転送中の待機だけをネイティブ SCardCancel() の対象にする */
	std::atomic<unsigned int> native_wait_count{ 0 };
	SCARDCONTEXT native_context = 0;
};

enum class CardBackend {
	PX4,
	NATIVE,
};

struct Card final {
	SCARDCONTEXT context = 0;
	CardBackend backend = CardBackend::PX4;
	std::wstring reader;
	std::unique_ptr<px4::CardClient> client;
	SCARDHANDLE native_handle = 0;
	std::vector<std::uint8_t> atr;
	DWORD transmit_count = 0;
	std::mutex mutex;
};

std::mutex state_mutex;
std::unordered_map<SCARDCONTEXT, std::shared_ptr<Context>> contexts;
std::unordered_map<SCARDHANDLE, std::shared_ptr<Card>> cards;
std::set<LPCVOID> local_allocations;
constexpr std::uintptr_t FIRST_HANDLE = sizeof(std::uintptr_t) == 8
	? static_cast<std::uintptr_t>(0x5058340000000001ULL)
	: static_cast<std::uintptr_t>(0x50000001UL);
std::atomic<std::uintptr_t> next_handle{ FIRST_HANDLE };

HANDLE StartedEvent()
{
	static HANDLE event = CreateEventW(nullptr, TRUE, TRUE, nullptr);
	return event;
}

std::shared_ptr<Context> FindContext(SCARDCONTEXT handle)
{
	std::lock_guard<std::mutex> lock(state_mutex);
	auto entry = contexts.find(handle);
	return entry == contexts.end() ? nullptr : entry->second;
}

std::shared_ptr<Card> FindCard(SCARDHANDLE handle)
{
	std::lock_guard<std::mutex> lock(state_mutex);
	auto entry = cards.find(handle);
	return entry == cards.end() ? nullptr : entry->second;
}

bool RegisterLocalAllocation(LPCVOID allocation) noexcept
{
	if (!allocation)
		return false;
	try {
		std::lock_guard<std::mutex> lock(state_mutex);
		local_allocations.emplace(allocation);
		return true;
	} catch (...) {
		return false;
	}
}

bool RemoveLocalAllocation(LPCVOID allocation)
{
	std::lock_guard<std::mutex> lock(state_mutex);
	return local_allocations.erase(allocation) != 0;
}

LONG MapError(px4::card_command::Error error)
{
	switch (error) {
	case px4::card_command::Error::INVALID_PARAMETER:
		return SCARD_E_INVALID_PARAMETER;
	case px4::card_command::Error::NO_READER:
		return SCARD_E_UNKNOWN_READER;
	case px4::card_command::Error::NO_CARD:
		return SCARD_E_NO_SMARTCARD;
	case px4::card_command::Error::BUSY:
		return SCARD_E_SHARING_VIOLATION;
	case px4::card_command::Error::REMOVED:
		return SCARD_W_REMOVED_CARD;
	case px4::card_command::Error::TIMEOUT:
		return SCARD_E_TIMEOUT;
	case px4::card_command::Error::PROTOCOL:
		return SCARD_E_NOT_TRANSACTED;
	case px4::card_command::Error::INSUFFICIENT_BUFFER:
		return SCARD_E_INSUFFICIENT_BUFFER;
	case px4::card_command::Error::INTERNAL:
	default:
		return SCARD_F_INTERNAL_ERROR;
	}
}

LONG Call(px4::CardClient &client, px4::card_command::Command &command)
{
	const auto requested_code = command.code;
	if (!client.Call(command))
		return SCARD_E_NO_SERVICE;
	if (command.code != requested_code)
		return SCARD_F_INTERNAL_ERROR;
	if (command.status != px4::card_command::Status::SUCCEEDED)
		return command.status == px4::card_command::Status::FAILED
			? MapError(command.error) : SCARD_F_INTERNAL_ERROR;

	/* 名前付きパイプの応答値を固定配列の上限内で検証してから参照する */
	if (requested_code == px4::card_command::Code::LIST_READERS) {
		if (command.reader_count > px4::card_command::MAX_READERS)
			return SCARD_F_INTERNAL_ERROR;
		for (std::size_t index = 0; index < command.reader_count; index++) {
			if (!wmemchr(command.readers[index], L'\0',
				px4::card_command::MAX_READER_NAME))
				return SCARD_F_INTERNAL_ERROR;
		}
	}
	if ((requested_code == px4::card_command::Code::STATUS ||
		requested_code == px4::card_command::Code::RESET) &&
		command.atr_length > px4::card_command::MAX_ATR_SIZE)
		return SCARD_F_INTERNAL_ERROR;
	if (requested_code == px4::card_command::Code::TRANSMIT &&
		command.data_length > px4::card_command::MAX_DATA_SIZE)
		return SCARD_F_INTERNAL_ERROR;
	return SCARD_S_SUCCESS;
}

LONG QueryPx4Readers(px4::CardClient &client,
	std::vector<std::wstring> &readers,
	std::uint64_t *reader_generation = nullptr) try
{
	px4::card_command::Command command = {};
	command.code = px4::card_command::Code::LIST_READERS;
	LONG result = Call(client, command);
	if (result != SCARD_S_SUCCESS)
		return result;
	if (reader_generation)
		*reader_generation = command.reader_generation;

	readers.reserve(command.reader_count);
	for (std::size_t index = 0; index < command.reader_count; index++)
		readers.emplace_back(command.readers[index]);
	return readers.empty() ? SCARD_E_NO_READERS_AVAILABLE : SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG ListPx4Readers(std::vector<std::wstring> &readers,
	std::uint64_t *reader_generation = nullptr) try
{
	px4::CardClient client;
	if (!client.Connect())
		return SCARD_E_NO_SERVICE;
	return QueryPx4Readers(client, readers, reader_generation);
}
PX4_SCARD_CATCH

LONG ListNativeReaders(SCARDCONTEXT native_context, LPCWSTR groups,
	std::vector<std::wstring> &readers) try
{
	if (!native_context)
		return SCARD_E_NO_SERVICE;
	auto list_readers = px4::winscard::GetNativeFunction<
		decltype(&SCardListReadersW)>("SCardListReadersW");
	if (!list_readers)
		return SCARD_E_NO_SERVICE;

	DWORD length = 0;
	LONG result = list_readers(native_context, groups, nullptr, &length);
	if (result == SCARD_E_NO_READERS_AVAILABLE)
		return result;
	if (result != SCARD_S_SUCCESS)
		return result;
	std::vector<wchar_t> buffer(length);
	result = list_readers(native_context, groups, buffer.data(), &length);
	if (result != SCARD_S_SUCCESS)
		return result;

	/* Windows が返す MULTI_SZ の順序を維持して PX4 リーダーの後ろへ追加する */
	for (const wchar_t *name = buffer.data(); *name;
		name += wcslen(name) + 1)
		readers.emplace_back(name);
	return readers.empty() ? SCARD_E_NO_READERS_AVAILABLE : SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG ListNativeGroups(SCARDCONTEXT native_context,
	std::vector<std::wstring> &groups) try
{
	if (!native_context)
		return SCARD_E_NO_SERVICE;
	auto list_groups = px4::winscard::GetNativeFunction<
		decltype(&SCardListReaderGroupsW)>("SCardListReaderGroupsW");
	if (!list_groups)
		return SCARD_E_NO_SERVICE;
	DWORD length = 0;
	LONG result = list_groups(native_context, nullptr, &length);
	if (result != SCARD_S_SUCCESS)
		return result;
	std::vector<wchar_t> buffer(length);
	result = list_groups(native_context, buffer.data(), &length);
	if (result != SCARD_S_SUCCESS)
		return result;
	for (const wchar_t *name = buffer.data(); *name; name += wcslen(name) + 1)
		groups.emplace_back(name);
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

bool IncludesPx4ReaderGroup(LPCWSTR groups)
{
	/* グループ未指定は全リーダー、PX4 の仮想リーダーは Windows の既定グループに属する */
	if (!groups)
		return true;
	for (const wchar_t *group = groups; *group; group += wcslen(group) + 1) {
		if (wcscmp(group, L"SCard$AllReaders") == 0 ||
			wcscmp(group, L"SCard$DefaultReaders") == 0)
			return true;
	}
	return false;
}

LONG ListCombinedReaders(const std::shared_ptr<Context> &context, LPCWSTR groups,
	std::vector<std::wstring> &readers,
	px4::CardClient *px4_client = nullptr) try
{
	std::vector<std::wstring> px4_readers;
	LONG px4_result = IncludesPx4ReaderGroup(groups) ?
		(px4_client ? QueryPx4Readers(*px4_client, px4_readers) :
			ListPx4Readers(px4_readers)) : SCARD_E_NO_READERS_AVAILABLE;
	if (px4_result == SCARD_S_SUCCESS)
		readers.insert(readers.end(), px4_readers.begin(), px4_readers.end());

	std::vector<std::wstring> native_readers;
	LONG native_result = ListNativeReaders(context->native_context, groups,
		native_readers);
	if (native_result == SCARD_S_SUCCESS) {
		for (const auto &reader : native_readers) {
			/* 同名リーダーは利用者が選んだ PX4 優先規則に従って1件だけ返す */
			if (std::find(readers.begin(), readers.end(), reader) == readers.end())
				readers.emplace_back(reader);
		}
	}

	if (!readers.empty())
		return SCARD_S_SUCCESS;
	if (native_result != SCARD_E_NO_SERVICE &&
		native_result != SCARD_E_NO_READERS_AVAILABLE)
		return native_result;
	if (px4_result != SCARD_E_NO_SERVICE &&
		px4_result != SCARD_E_NO_READERS_AVAILABLE)
		return px4_result;
	return SCARD_E_NO_READERS_AVAILABLE;
}
PX4_SCARD_CATCH

bool IsPx4Reader(const std::wstring &reader) try
{
	std::vector<std::wstring> readers;
	LONG result = ListPx4Readers(readers);
	return result == SCARD_S_SUCCESS &&
		std::find(readers.begin(), readers.end(), reader) != readers.end();
}
catch (...) {
	return false;
}

LONG QueryStatus(px4::CardClient &client, const std::wstring &reader,
		 bool &present, bool &initialized, std::vector<std::uint8_t> &atr) try
{
	if (reader.size() >= px4::card_command::MAX_READER_NAME)
		return SCARD_E_UNKNOWN_READER;
	px4::card_command::Command command = {};
	command.code = px4::card_command::Code::STATUS;
	wcscpy_s(command.reader_name, reader.c_str());
	LONG result = Call(client, command);
	if (result != SCARD_S_SUCCESS)
		return result;
	present = command.card_present;
	initialized = command.card_initialized;
	atr.assign(command.atr, command.atr + command.atr_length);
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

template <typename ReaderState, typename ReaderChar>
LONG GetStatusChange(SCARDCONTEXT context_handle, DWORD timeout,
		     ReaderState *states, DWORD reader_count) try
{
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	if (reader_count && !states)
		return SCARD_E_INVALID_PARAMETER;
	const auto cancel_generation = context->cancel_generation.load();

	std::vector<std::wstring> px4_readers;
	std::unique_ptr<px4::CardClient> px4_client =
		std::make_unique<px4::CardClient>();
	if (!px4_client->Connect()) {
		px4_client.reset();
	} else if (QueryPx4Readers(*px4_client, px4_readers) != SCARD_S_SUCCESS) {
		/* リーダー0台でも接続を保持し、同じ DriverHost で後から追加された機器を検出する */
		px4_readers.clear();
	}

	/* PX4 を含まない監視は Windows 標準へ一括転送し、待機精度と状態値をそのまま維持する */
	bool requires_combined_wait = false;
	for (DWORD index = 0; index < reader_count; index++) {
		if (!states[index].szReader)
			return SCARD_E_INVALID_PARAMETER;
		if (states[index].dwCurrentState & SCARD_STATE_IGNORE)
			continue;

		std::wstring reader;
		if constexpr (std::is_same_v<ReaderChar, char>)
			reader = ToWide(states[index].szReader);
		else
			reader = states[index].szReader;
		if (reader == px4::winscard::PNP_NOTIFICATION ||
			std::find(px4_readers.begin(), px4_readers.end(), reader) !=
			px4_readers.end()) {
			requires_combined_wait = true;
			break;
		}
	}
	if (!requires_combined_wait && context->native_context) {
		context->native_wait_count.fetch_add(1);
		LONG result;
		if constexpr (std::is_same_v<ReaderChar, char>) {
			auto status_change = px4::winscard::GetNativeFunction<
				decltype(&SCardGetStatusChangeA)>("SCardGetStatusChangeA");
			result = status_change ? status_change(context->native_context, timeout,
				states, reader_count) : SCARD_E_NO_SERVICE;
		} else {
			auto status_change = px4::winscard::GetNativeFunction<
				decltype(&SCardGetStatusChangeW)>("SCardGetStatusChangeW");
			result = status_change ? status_change(context->native_context, timeout,
				states, reader_count) : SCARD_E_NO_SERVICE;
		}
		context->native_wait_count.fetch_sub(1);
		return result;
	}
	auto start = std::chrono::steady_clock::now();

	while (true) {
		bool any_changed = false;
		for (DWORD index = 0; index < reader_count; index++) {
			if (!states[index].szReader)
				return SCARD_E_INVALID_PARAMETER;
			if (states[index].dwCurrentState & SCARD_STATE_IGNORE) {
				states[index].dwEventState = SCARD_STATE_IGNORE;
				continue;
			}

			std::wstring reader;
			if constexpr (std::is_same_v<ReaderChar, char>)
				reader = ToWide(states[index].szReader);
			else
				reader = states[index].szReader;
			if (reader == px4::winscard::PNP_NOTIFICATION) {
				std::vector<std::wstring> readers;
				LONG result = ListCombinedReaders(context, nullptr, readers,
					px4_client.get());
				if (result != SCARD_S_SUCCESS && result != SCARD_E_NO_READERS_AVAILABLE)
					return result;
				std::uint32_t generation = 2166136261U;
				for (const auto &listed_reader : readers) {
					for (wchar_t character : listed_reader) {
						generation ^= static_cast<std::uint32_t>(character);
						generation *= 16777619U;
					}
					generation ^= 0;
					generation *= 16777619U;
				}
				/* 複合リーダー一覧のハッシュで PX4 と外付け双方の追加・削除を通知する */
				DWORD event_state = (generation & 0xffffU) << 16;
				DWORD current = states[index].dwCurrentState & ~SCARD_STATE_CHANGED;
				if (states[index].dwCurrentState == SCARD_STATE_UNAWARE ||
					current != event_state) {
					event_state |= SCARD_STATE_CHANGED;
					any_changed = true;
				}
				states[index].dwEventState = event_state;
				continue;
			}

			const bool is_px4 = std::find(px4_readers.begin(), px4_readers.end(),
				reader) != px4_readers.end();
			if (is_px4) {
				bool present = false;
				bool initialized = false;
				std::vector<std::uint8_t> atr;
				LONG result = QueryStatus(*px4_client, reader, present, initialized, atr);
				DWORD event_state;
				/* Windows は不明なリーダーを以後の監視対象から外す */
				if (result == SCARD_E_UNKNOWN_READER)
					event_state = SCARD_STATE_UNKNOWN | SCARD_STATE_IGNORE;
				else if (result != SCARD_S_SUCCESS)
					return result;
				else
					event_state = present ? SCARD_STATE_PRESENT : SCARD_STATE_EMPTY;

				DWORD current = states[index].dwCurrentState & ~SCARD_STATE_CHANGED;
				if (states[index].dwCurrentState == SCARD_STATE_UNAWARE ||
					current != event_state) {
					event_state |= SCARD_STATE_CHANGED;
					any_changed = true;
				}
				states[index].dwEventState = event_state;
				states[index].cbAtr = static_cast<DWORD>(
					std::min<std::size_t>(atr.size(), sizeof(states[index].rgbAtr)));
				if (states[index].cbAtr)
					memcpy(states[index].rgbAtr, atr.data(), states[index].cbAtr);
				continue;
			}

			if (!context->native_context) {
				states[index].dwEventState = SCARD_STATE_UNKNOWN |
					SCARD_STATE_IGNORE | SCARD_STATE_CHANGED;
				states[index].cbAtr = 0;
				any_changed = true;
				continue;
			}

			ReaderState native_state = states[index];
			LONG result;
			if constexpr (std::is_same_v<ReaderChar, char>) {
				auto status_change = px4::winscard::GetNativeFunction<
					decltype(&SCardGetStatusChangeA)>("SCardGetStatusChangeA");
				if (!status_change)
					return SCARD_E_NO_SERVICE;
				result = status_change(context->native_context, 0, &native_state, 1);
			} else {
				auto status_change = px4::winscard::GetNativeFunction<
					decltype(&SCardGetStatusChangeW)>("SCardGetStatusChangeW");
				if (!status_change)
					return SCARD_E_NO_SERVICE;
				result = status_change(context->native_context, 0, &native_state, 1);
			}
			if (result != SCARD_S_SUCCESS && result != SCARD_E_TIMEOUT)
				return result;
			states[index].dwEventState = native_state.dwEventState;
			states[index].cbAtr = native_state.cbAtr;
			memcpy(states[index].rgbAtr, native_state.rgbAtr,
				sizeof(states[index].rgbAtr));
			if (native_state.dwEventState & SCARD_STATE_CHANGED)
				any_changed = true;
		}

		if (any_changed)
			return SCARD_S_SUCCESS;
		if (context->cancel_generation.load() != cancel_generation)
			return SCARD_E_CANCELLED;
		if (timeout == 0)
			return SCARD_E_TIMEOUT;
		if (timeout != INFINITE &&
			std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(timeout))
			return SCARD_E_TIMEOUT;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}
PX4_SCARD_CATCH

template <typename ReaderChar>
LONG ConnectCard(SCARDCONTEXT context_handle, const ReaderChar *reader_name,
		 DWORD share_mode, DWORD preferred_protocols,
		 LPSCARDHANDLE card_handle, LPDWORD active_protocol) try
{
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	if (!reader_name || !card_handle || !active_protocol)
		return SCARD_E_INVALID_PARAMETER;
	*card_handle = 0;
	*active_protocol = SCARD_PROTOCOL_UNDEFINED;

	std::wstring reader;
	if constexpr (std::is_same_v<ReaderChar, char>)
		reader = ToWide(reader_name);
	else
		reader = reader_name;
	if (!IsPx4Reader(reader)) {
		if (!context->native_context)
			return SCARD_E_UNKNOWN_READER;
		SCARDHANDLE native_handle = 0;
		DWORD native_protocol = SCARD_PROTOCOL_UNDEFINED;
		LONG result;
		/* 外付けリーダー名の文字変換規則も、呼ばれた ANSI / Unicode API と一致させる */
		if constexpr (std::is_same_v<ReaderChar, char>) {
			auto native_connect = px4::winscard::GetNativeFunction<
				decltype(&SCardConnectA)>("SCardConnectA");
			if (!native_connect)
				return SCARD_E_NO_SERVICE;
			result = native_connect(context->native_context, reader_name,
				share_mode, preferred_protocols, &native_handle, &native_protocol);
		} else {
			auto native_connect = px4::winscard::GetNativeFunction<
				decltype(&SCardConnectW)>("SCardConnectW");
			if (!native_connect)
				return SCARD_E_NO_SERVICE;
			result = native_connect(context->native_context, reader_name,
				share_mode, preferred_protocols, &native_handle, &native_protocol);
		}
		if (result != SCARD_S_SUCCESS)
			return result;

		try {
			auto card = std::make_shared<Card>();
			card->context = context_handle;
			card->backend = CardBackend::NATIVE;
			card->reader = std::move(reader);
			card->native_handle = native_handle;
			SCARDHANDLE handle = static_cast<SCARDHANDLE>(next_handle.fetch_add(1));
			bool context_released = false;
			{
				std::lock_guard<std::mutex> lock(state_mutex);
				/* 接続中にコンテキストが解放された場合は孤立ハンドルを登録しない */
				context_released = contexts.find(context_handle) == contexts.end();
				if (!context_released)
					cards.emplace(handle, std::move(card));
			}
			if (context_released) {
				auto native_disconnect = px4::winscard::GetNativeFunction<
					decltype(&SCardDisconnect)>("SCardDisconnect");
				if (native_disconnect)
					native_disconnect(native_handle, SCARD_LEAVE_CARD);
				return SCARD_E_INVALID_HANDLE;
			}
			*card_handle = handle;
			*active_protocol = native_protocol;
		} catch (...) {
			/* プロキシハンドルを登録できない場合は System32 側だけに接続を残さない */
			auto native_disconnect = px4::winscard::GetNativeFunction<
				decltype(&SCardDisconnect)>("SCardDisconnect");
			if (native_disconnect)
				native_disconnect(native_handle, SCARD_LEAVE_CARD);
			throw;
		}
		return SCARD_S_SUCCESS;
	}

	if (share_mode != SCARD_SHARE_SHARED && share_mode != SCARD_SHARE_EXCLUSIVE)
		return SCARD_E_INVALID_VALUE;
	if (!(preferred_protocols & SCARD_PROTOCOL_T1))
		return SCARD_E_PROTO_MISMATCH;
	if (reader.size() >= px4::card_command::MAX_READER_NAME)
		return SCARD_E_UNKNOWN_READER;
	auto client = std::make_unique<px4::CardClient>();
	if (!client->Connect())
		return SCARD_E_NO_SERVICE;

	px4::card_command::Command command = {};
	command.code = px4::card_command::Code::CONNECT;
	command.share_mode = share_mode == SCARD_SHARE_EXCLUSIVE
		? static_cast<std::uint32_t>(px4::card_command::ShareMode::EXCLUSIVE)
		: static_cast<std::uint32_t>(px4::card_command::ShareMode::SHARED);
	wcscpy_s(command.reader_name, reader.c_str());
	LONG result = Call(*client, command);
	if (result != SCARD_S_SUCCESS)
		return result;
	auto disconnect = [&client]() noexcept {
		px4::card_command::Command command = {};
		command.code = px4::card_command::Code::DISCONNECT;
		client->Call(command);
	};

	bool present = false;
	bool initialized = false;
	std::vector<std::uint8_t> atr;
	result = QueryStatus(*client, reader, present, initialized, atr);
	if (result != SCARD_S_SUCCESS || !present || !initialized) {
		disconnect();
		return result == SCARD_S_SUCCESS ? SCARD_E_NO_SMARTCARD : result;
	}

	try {
		auto card = std::make_shared<Card>();
		card->context = context_handle;
		card->reader = std::move(reader);
		card->atr = std::move(atr);
		SCARDHANDLE handle = static_cast<SCARDHANDLE>(next_handle.fetch_add(1));
		bool context_released = false;
		{
			std::lock_guard<std::mutex> lock(state_mutex);
			/* 接続中に解放されたコンテキストへカードを登録せず、共有数を後で戻す */
			context_released = contexts.find(context_handle) == contexts.end();
			if (!context_released) {
				/* 挿入失敗時も切断要求を送れるよう client の所有権は登録成功後に移す */
				cards.emplace(handle, card);
				card->client = std::move(client);
			}
		}
		if (context_released) {
			disconnect();
			return SCARD_E_INVALID_HANDLE;
		}
		*card_handle = handle;
		*active_protocol = SCARD_PROTOCOL_T1;
	} catch (...) {
		/* カードサーバーへ接続済みのため、ローカル登録失敗時は共有数を戻す */
		disconnect();
		throw;
	}
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

template <typename Char>
LONG CardStatus(SCARDHANDLE card_handle, Char *reader_names,
		LPDWORD reader_length, LPDWORD state, LPDWORD protocol,
		LPBYTE atr_buffer, LPDWORD atr_length) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	std::lock_guard<std::mutex> lock(card->mutex);
	if (card->backend == CardBackend::NATIVE) {
		if constexpr (std::is_same_v<Char, char>) {
			auto status = px4::winscard::GetNativeFunction<
				decltype(&SCardStatusA)>("SCardStatusA");
			return status ? status(card->native_handle, reader_names,
				reader_length, state, protocol, atr_buffer, atr_length) :
				SCARD_E_NO_SERVICE;
		} else {
			auto status = px4::winscard::GetNativeFunction<
				decltype(&SCardStatusW)>("SCardStatusW");
			return status ? status(card->native_handle, reader_names,
				reader_length, state, protocol, atr_buffer, atr_length) :
				SCARD_E_NO_SERVICE;
		}
	}
	if ((reader_names && !reader_length) || (atr_buffer && !atr_length))
		return SCARD_E_INVALID_PARAMETER;

	bool present = false;
	bool initialized = false;
	std::vector<std::uint8_t> atr;
	LONG result = QueryStatus(*card->client, card->reader, present, initialized, atr);
	if (result != SCARD_S_SUCCESS)
		return result;
	card->atr = atr;

	if (reader_length) {
		/* SCardStatus() のリーダー名は単一文字列でも MULTI_SZ で返す */
		if constexpr (std::is_same_v<Char, char>)
			result = CopyMultiString(std::vector<std::string>{ ToAnsi(card->reader) },
				reader_names, reader_length);
		else
			result = CopyMultiString(std::vector<std::wstring>{ card->reader },
				reader_names, reader_length);
		if (result != SCARD_S_SUCCESS)
			return result;
	}
	if (state)
		*state = initialized ? SCARD_SPECIFIC : (present ? SCARD_PRESENT : SCARD_ABSENT);
	if (protocol)
		*protocol = SCARD_PROTOCOL_T1;
	if (atr_length) {
		DWORD supplied = *atr_length;
		*atr_length = static_cast<DWORD>(atr.size());
		if (supplied == SCARD_AUTOALLOCATE) {
			if (!atr_buffer)
				return SCARD_E_INVALID_PARAMETER;
			auto allocation = static_cast<BYTE *>(LocalAlloc(LMEM_FIXED, atr.size()));
			if (!allocation)
				return SCARD_E_NO_MEMORY;
			memcpy(allocation, atr.data(), atr.size());
			if (!RegisterLocalAllocation(allocation)) {
				LocalFree(allocation);
				return SCARD_E_NO_MEMORY;
			}
			*reinterpret_cast<BYTE **>(atr_buffer) = allocation;
		} else if (atr_buffer) {
			if (supplied < atr.size())
				return SCARD_E_INSUFFICIENT_BUFFER;
			memcpy(atr_buffer, atr.data(), atr.size());
		}
	}
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

} // namespace

namespace px4::winscard {

std::wstring ToWide(LPCSTR value)
{
	if (!value)
		return {};
	const int length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
	if (length <= 0)
		return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	if (!MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), length))
		return {};
	result.pop_back();
	return result;
}

std::wstring ToWide(LPCWSTR value)
{
	return value ? std::wstring(value) : std::wstring();
}

std::string ToAnsi(const std::wstring &value)
{
	const int length = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1,
		nullptr, 0, nullptr, nullptr);
	if (length <= 0)
		return {};
	std::string result(static_cast<std::size_t>(length), '\0');
	if (!WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1,
		result.data(), length, nullptr, nullptr))
		return {};
	result.pop_back();
	return result;
}

bool GetNativeContext(SCARDCONTEXT context_handle,
	SCARDCONTEXT &native_context) noexcept
{
	auto context = FindContext(context_handle);
	if (!context)
		return false;
	native_context = context->native_context;
	return true;
}

bool RegisterProxyAllocation(LPCVOID allocation) noexcept
{
	return RegisterLocalAllocation(allocation);
}

bool IsPx4ReaderName(const std::wstring &reader) noexcept
{
	return IsPx4Reader(reader);
}

LONG GetPx4ReaderNames(std::vector<std::wstring> &readers) noexcept
{
	return ListPx4Readers(readers);
}

} // namespace px4::winscard

extern "C" {

const SCARD_IO_REQUEST g_rgSCardT0Pci = { SCARD_PROTOCOL_T0, sizeof(SCARD_IO_REQUEST) };
const SCARD_IO_REQUEST g_rgSCardT1Pci = { SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST) };
const SCARD_IO_REQUEST g_rgSCardRawPci = { SCARD_PROTOCOL_RAW, sizeof(SCARD_IO_REQUEST) };

LONG WINAPI SCardEstablishContext(DWORD scope, LPCVOID reserved1,
	LPCVOID reserved2, LPSCARDCONTEXT context_handle) try
{
	if (!context_handle)
		return SCARD_E_INVALID_PARAMETER;
	*context_handle = 0;
	if (reserved1 || reserved2 || scope > SCARD_SCOPE_SYSTEM)
		return SCARD_E_INVALID_PARAMETER;
	auto context = std::make_shared<Context>();
	auto native_establish = px4::winscard::GetNativeFunction<
		decltype(&SCardEstablishContext)>("SCardEstablishContext");
	LONG native_result = SCARD_E_NO_SERVICE;
	if (native_establish) {
		SCARDCONTEXT native_context = 0;
		native_result = native_establish(scope, reserved1, reserved2,
			&native_context);
		if (native_result == SCARD_S_SUCCESS)
			context->native_context = native_context;
	}
	if (native_result != SCARD_S_SUCCESS) {
		/* Windows のサービス停止時は、実在する PX4 リーダーがある場合だけ独立して開始する */
		std::vector<std::wstring> px4_readers;
		if (ListPx4Readers(px4_readers) != SCARD_S_SUCCESS)
			return native_result;
	}
	SCARDCONTEXT handle = static_cast<SCARDCONTEXT>(next_handle.fetch_add(1));
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		contexts.emplace(handle, std::move(context));
	}
	*context_handle = handle;
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardReleaseContext(SCARDCONTEXT context_handle) try
{
	std::shared_ptr<Context> released_context;
	std::vector<std::shared_ptr<Card>> released_cards;
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		auto context_entry = contexts.find(context_handle);
		if (context_entry == contexts.end())
			return SCARD_E_INVALID_HANDLE;
		/* 状態変更前に必要容量を確保し、メモリ不足時も再試行可能にする */
		released_cards.reserve(cards.size());
		released_context = context_entry->second;
		contexts.erase(context_entry);
		for (auto entry = cards.begin(); entry != cards.end();) {
			if (entry->second->context == context_handle) {
				released_cards.emplace_back(entry->second);
				entry = cards.erase(entry);
			} else {
				entry++;
			}
		}
	}
	/* コンテキスト解放と競合した状態待機も速やかに終了させる */
	released_context->cancel_generation.fetch_add(1);
	for (const auto &card : released_cards) {
		std::lock_guard<std::mutex> lock(card->mutex);
		if (card->backend == CardBackend::NATIVE) {
			auto disconnect = px4::winscard::GetNativeFunction<
				decltype(&SCardDisconnect)>("SCardDisconnect");
			if (disconnect)
				disconnect(card->native_handle, SCARD_LEAVE_CARD);
		} else {
			px4::card_command::Command command = {};
			command.code = px4::card_command::Code::DISCONNECT;
			card->client->Call(command);
		}
	}
	if (released_context->native_context) {
		auto release_context = px4::winscard::GetNativeFunction<
			decltype(&SCardReleaseContext)>("SCardReleaseContext");
		if (release_context)
			release_context(released_context->native_context);
	}
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardIsValidContext(SCARDCONTEXT context_handle) try
{
	return FindContext(context_handle) ? SCARD_S_SUCCESS : SCARD_E_INVALID_HANDLE;
}
PX4_SCARD_CATCH

LONG WINAPI SCardFreeMemory(SCARDCONTEXT context_handle, LPCVOID memory) try
{
	if (!memory)
		return SCARD_E_INVALID_PARAMETER;
	/* プロキシの確保領域は context == 0 を許すデータベース API からも返される */
	if (RemoveLocalAllocation(memory))
		return LocalFree(const_cast<LPVOID>(memory)) ?
			SCARD_F_INTERNAL_ERROR : SCARD_S_SUCCESS;
	/* context == 0 で Windows が自動確保した領域も同じ値のまま解放する */
	if (!context_handle) {
		auto free_memory = px4::winscard::GetNativeFunction<
			decltype(&SCardFreeMemory)>("SCardFreeMemory");
		return free_memory ? free_memory(0, memory) : SCARD_E_NO_SERVICE;
	}
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	if (!context->native_context)
		return SCARD_E_INVALID_PARAMETER;
	auto free_memory = px4::winscard::GetNativeFunction<
		decltype(&SCardFreeMemory)>("SCardFreeMemory");
	return free_memory ? free_memory(context->native_context, memory) :
		SCARD_E_NO_SERVICE;
}
PX4_SCARD_CATCH

HANDLE WINAPI SCardAccessStartedEvent(void)
{
	auto access = px4::winscard::GetNativeFunction<
		decltype(&SCardAccessStartedEvent)>("SCardAccessStartedEvent");
	/* Windows 標準を読み込めない環境でも内蔵リーダー用コンテキストは開始可能とする */
	return access ? access() : StartedEvent();
}

void WINAPI SCardReleaseStartedEvent(void)
{
	auto release = px4::winscard::GetNativeFunction<
		decltype(&SCardReleaseStartedEvent)>("SCardReleaseStartedEvent");
	if (release)
		release();
}

LONG WINAPI SCardListReaderGroupsA(SCARDCONTEXT context_handle,
	LPSTR groups, LPDWORD length) try
{
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	std::vector<std::wstring> wide_groups{ L"SCard$DefaultReaders" };
	std::vector<std::wstring> native_groups;
	if (ListNativeGroups(context->native_context, native_groups) == SCARD_S_SUCCESS) {
		for (const auto &group : native_groups) {
			if (std::find(wide_groups.begin(), wide_groups.end(), group) == wide_groups.end())
				wide_groups.emplace_back(group);
		}
	}
	std::vector<std::string> ansi_groups;
	for (const auto &group : wide_groups)
		ansi_groups.emplace_back(ToAnsi(group));
	return CopyMultiString(ansi_groups, groups, length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardListReaderGroupsW(SCARDCONTEXT context_handle,
	LPWSTR groups, LPDWORD length) try
{
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	std::vector<std::wstring> result_groups{ L"SCard$DefaultReaders" };
	std::vector<std::wstring> native_groups;
	if (ListNativeGroups(context->native_context, native_groups) == SCARD_S_SUCCESS) {
		for (const auto &group : native_groups) {
			if (std::find(result_groups.begin(), result_groups.end(), group) == result_groups.end())
				result_groups.emplace_back(group);
		}
	}
	return CopyMultiString(result_groups, groups, length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardListReadersA(SCARDCONTEXT context_handle, LPCSTR groups,
	LPSTR reader_buffer, LPDWORD length) try
{
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	std::vector<wchar_t> wide_group_buffer;
	if (groups) {
		for (const char *group = groups; *group; group += strlen(group) + 1) {
			std::wstring wide_group = ToWide(group);
			wide_group_buffer.insert(wide_group_buffer.end(), wide_group.begin(),
				wide_group.end());
			wide_group_buffer.emplace_back(L'\0');
		}
		wide_group_buffer.emplace_back(L'\0');
	}
	std::vector<std::wstring> wide_readers;
	LONG result = ListCombinedReaders(context,
		wide_group_buffer.empty() ? nullptr : wide_group_buffer.data(), wide_readers);
	if (result != SCARD_S_SUCCESS)
		return result;
	std::vector<std::string> readers;
	for (const auto &reader : wide_readers)
		readers.emplace_back(ToAnsi(reader));
	return CopyMultiString(readers, reader_buffer, length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardListReadersW(SCARDCONTEXT context_handle, LPCWSTR groups,
	LPWSTR reader_buffer, LPDWORD length) try
{
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	std::vector<std::wstring> readers;
	LONG result = ListCombinedReaders(context, groups, readers);
	return result == SCARD_S_SUCCESS ? CopyMultiString(readers, reader_buffer, length) : result;
}
PX4_SCARD_CATCH

LONG WINAPI SCardConnectA(SCARDCONTEXT context_handle, LPCSTR reader,
	DWORD share_mode, DWORD protocols, LPSCARDHANDLE card_handle,
	LPDWORD active_protocol)
{
	return ConnectCard(context_handle, reader, share_mode, protocols,
		card_handle, active_protocol);
}

LONG WINAPI SCardConnectW(SCARDCONTEXT context_handle, LPCWSTR reader,
	DWORD share_mode, DWORD protocols, LPSCARDHANDLE card_handle,
	LPDWORD active_protocol)
{
	return ConnectCard(context_handle, reader, share_mode, protocols,
		card_handle, active_protocol);
}

LONG WINAPI SCardDisconnect(SCARDHANDLE card_handle, DWORD disposition) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (card->backend == CardBackend::NATIVE) {
		auto disconnect = px4::winscard::GetNativeFunction<
			decltype(&SCardDisconnect)>("SCardDisconnect");
		if (!disconnect)
			return SCARD_E_NO_SERVICE;
		std::lock_guard<std::mutex> lock(card->mutex);
		LONG result = disconnect(card->native_handle, disposition);
		if (result == SCARD_S_SUCCESS) {
			std::lock_guard<std::mutex> state_lock(state_mutex);
			cards.erase(card_handle);
		}
		return result;
	}
	if (disposition != SCARD_LEAVE_CARD && disposition != SCARD_RESET_CARD &&
		disposition != SCARD_UNPOWER_CARD && disposition != SCARD_EJECT_CARD)
		return SCARD_E_INVALID_VALUE;
	if (disposition == SCARD_UNPOWER_CARD || disposition == SCARD_EJECT_CARD)
		return SCARD_E_UNSUPPORTED_FEATURE;

	{
		std::lock_guard<std::mutex> state_lock(state_mutex);
		auto entry = cards.find(card_handle);
		if (entry == cards.end())
			return SCARD_E_INVALID_HANDLE;
		cards.erase(entry);
	}
	std::lock_guard<std::mutex> lock(card->mutex);
	if (disposition == SCARD_RESET_CARD) {
		px4::card_command::Command reset = {};
		reset.code = px4::card_command::Code::RESET;
		Call(*card->client, reset);
	}
	px4::card_command::Command command = {};
	command.code = px4::card_command::Code::DISCONNECT;
	return Call(*card->client, command);
}
PX4_SCARD_CATCH

LONG WINAPI SCardReconnect(SCARDHANDLE card_handle, DWORD share_mode,
	DWORD protocols, DWORD initialization, LPDWORD active_protocol) try
{
	if (!active_protocol)
		return SCARD_E_INVALID_PARAMETER;
	*active_protocol = SCARD_PROTOCOL_UNDEFINED;
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (card->backend == CardBackend::NATIVE) {
		auto reconnect = px4::winscard::GetNativeFunction<
			decltype(&SCardReconnect)>("SCardReconnect");
		if (!reconnect)
			return SCARD_E_NO_SERVICE;
		std::lock_guard<std::mutex> lock(card->mutex);
		return reconnect(card->native_handle, share_mode, protocols,
			initialization, active_protocol);
	}
	if (share_mode != SCARD_SHARE_SHARED && share_mode != SCARD_SHARE_EXCLUSIVE)
		return SCARD_E_INVALID_VALUE;
	if (!(protocols & SCARD_PROTOCOL_T1))
		return SCARD_E_PROTO_MISMATCH;
	if (initialization != SCARD_LEAVE_CARD && initialization != SCARD_RESET_CARD &&
		initialization != SCARD_UNPOWER_CARD && initialization != SCARD_EJECT_CARD)
		return SCARD_E_INVALID_VALUE;
	if (initialization == SCARD_UNPOWER_CARD || initialization == SCARD_EJECT_CARD)
		return SCARD_E_UNSUPPORTED_FEATURE;
	std::lock_guard<std::mutex> lock(card->mutex);
	px4::card_command::Command reconnect = {};
	reconnect.code = px4::card_command::Code::RECONNECT;
	reconnect.share_mode = share_mode == SCARD_SHARE_EXCLUSIVE
		? static_cast<std::uint32_t>(px4::card_command::ShareMode::EXCLUSIVE)
		: static_cast<std::uint32_t>(px4::card_command::ShareMode::SHARED);
	LONG result = Call(*card->client, reconnect);
	if (result != SCARD_S_SUCCESS)
		return result;
	if (initialization != SCARD_LEAVE_CARD) {
		px4::card_command::Command command = {};
		command.code = px4::card_command::Code::RESET;
		result = Call(*card->client, command);
		if (result != SCARD_S_SUCCESS)
			return result;
		card->atr.assign(command.atr, command.atr + command.atr_length);
	}
	*active_protocol = SCARD_PROTOCOL_T1;
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardBeginTransaction(SCARDHANDLE card_handle) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	std::lock_guard<std::mutex> lock(card->mutex);
	if (card->backend == CardBackend::NATIVE) {
		auto begin = px4::winscard::GetNativeFunction<
			decltype(&SCardBeginTransaction)>("SCardBeginTransaction");
		return begin ? begin(card->native_handle) : SCARD_E_NO_SERVICE;
	}
	px4::card_command::Command command = {};
	command.code = px4::card_command::Code::BEGIN_TRANSACTION;
	return Call(*card->client, command);
}
PX4_SCARD_CATCH

LONG WINAPI SCardEndTransaction(SCARDHANDLE card_handle, DWORD disposition) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (card->backend == CardBackend::NATIVE) {
		auto end = px4::winscard::GetNativeFunction<
			decltype(&SCardEndTransaction)>("SCardEndTransaction");
		if (!end)
			return SCARD_E_NO_SERVICE;
		std::lock_guard<std::mutex> lock(card->mutex);
		return end(card->native_handle, disposition);
	}
	if (disposition != SCARD_LEAVE_CARD && disposition != SCARD_RESET_CARD &&
		disposition != SCARD_UNPOWER_CARD && disposition != SCARD_EJECT_CARD)
		return SCARD_E_INVALID_VALUE;
	std::lock_guard<std::mutex> lock(card->mutex);
	px4::card_command::Command command = {};
	command.code = px4::card_command::Code::END_TRANSACTION;
	command.disposition = static_cast<std::uint32_t>(
		disposition == SCARD_RESET_CARD
			? px4::card_command::Disposition::RESET
			: px4::card_command::Disposition::LEAVE);
	LONG result = Call(*card->client, command);
	/* 未対応の disposition でもカード全体を塞ぐトランザクションは解放する */
	if (result == SCARD_S_SUCCESS &&
		(disposition == SCARD_UNPOWER_CARD || disposition == SCARD_EJECT_CARD))
		return SCARD_E_UNSUPPORTED_FEATURE;
	if (result != SCARD_S_SUCCESS || disposition == SCARD_LEAVE_CARD)
		return result;

	/* RESET は DriverHost が排他状態のまま実行し、応答で更新後の ATR を返す */
	card->atr.assign(command.atr, command.atr + command.atr_length);
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardCancel(SCARDCONTEXT context_handle) try
{
	auto context = FindContext(context_handle);
	if (!context)
		return SCARD_E_INVALID_HANDLE;
	/* 複合待機と Windows 標準へ直接転送した待機の双方を同じ呼び出しで解除する */
	context->cancel_generation.fetch_add(1);
	if (!context->native_context || !context->native_wait_count.load())
		return SCARD_S_SUCCESS;
	auto cancel = px4::winscard::GetNativeFunction<
		decltype(&SCardCancel)>("SCardCancel");
	return cancel ? cancel(context->native_context) : SCARD_E_NO_SERVICE;
}
PX4_SCARD_CATCH

LONG WINAPI SCardGetStatusChangeA(SCARDCONTEXT context_handle, DWORD timeout,
	LPSCARD_READERSTATEA states, DWORD reader_count) try
{
	return GetStatusChange<SCARD_READERSTATEA, char>(context_handle, timeout,
		states, reader_count);
}
PX4_SCARD_CATCH

LONG WINAPI SCardGetStatusChangeW(SCARDCONTEXT context_handle, DWORD timeout,
	LPSCARD_READERSTATEW states, DWORD reader_count) try
{
	return GetStatusChange<SCARD_READERSTATEW, wchar_t>(context_handle, timeout,
		states, reader_count);
}
PX4_SCARD_CATCH

LONG WINAPI SCardStatusA(SCARDHANDLE card_handle, LPSTR reader_names,
	LPDWORD reader_length, LPDWORD state, LPDWORD protocol,
	LPBYTE atr, LPDWORD atr_length) try
{
	return CardStatus(card_handle, reader_names, reader_length, state,
		protocol, atr, atr_length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardStatusW(SCARDHANDLE card_handle, LPWSTR reader_names,
	LPDWORD reader_length, LPDWORD state, LPDWORD protocol,
	LPBYTE atr, LPDWORD atr_length) try
{
	return CardStatus(card_handle, reader_names, reader_length, state,
		protocol, atr, atr_length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardState(SCARDHANDLE card_handle, LPDWORD state,
	LPDWORD protocol, LPBYTE atr, LPDWORD atr_length) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (card->backend == CardBackend::NATIVE) {
		auto native_state = px4::winscard::GetNativeFunction<
			decltype(&SCardState)>("SCardState");
		if (!native_state)
			return SCARD_E_NO_SERVICE;
		std::lock_guard<std::mutex> lock(card->mutex);
		return native_state(card->native_handle, state, protocol, atr, atr_length);
	}
	if (!state || !protocol || !atr_length || (!atr && *atr_length))
		return SCARD_E_INVALID_PARAMETER;
	*state = SCARD_UNKNOWN;
	*protocol = SCARD_PROTOCOL_UNDEFINED;
	return CardStatus<wchar_t>(card_handle, nullptr, nullptr, state,
		protocol, atr, atr_length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardTransmit(SCARDHANDLE card_handle,
	LPCSCARD_IO_REQUEST send_pci, LPCBYTE send_buffer, DWORD send_length,
	LPSCARD_IO_REQUEST recv_pci, LPBYTE recv_buffer, LPDWORD recv_length) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (card->backend == CardBackend::NATIVE) {
		auto transmit = px4::winscard::GetNativeFunction<
			decltype(&SCardTransmit)>("SCardTransmit");
		if (!transmit)
			return SCARD_E_NO_SERVICE;
		std::lock_guard<std::mutex> lock(card->mutex);
		return transmit(card->native_handle, send_pci, send_buffer, send_length,
			recv_pci, recv_buffer, recv_length);
	}
	if (!send_pci || send_pci->dwProtocol != SCARD_PROTOCOL_T1 ||
		!send_buffer || !send_length || !recv_buffer || !recv_length)
		return SCARD_E_INVALID_PARAMETER;
	if (send_length > px4::card_command::MAX_DATA_SIZE)
		return SCARD_E_INVALID_PARAMETER;

	std::lock_guard<std::mutex> lock(card->mutex);
	px4::card_command::Command command = {};
	command.code = px4::card_command::Code::TRANSMIT;
	command.data_length = send_length;
	command.data_capacity = std::min<DWORD>(*recv_length,
		static_cast<DWORD>(px4::card_command::MAX_DATA_SIZE));
	memcpy(command.data, send_buffer, send_length);
	LONG result = Call(*card->client, command);
	if (result != SCARD_S_SUCCESS) {
		if (result == SCARD_E_INSUFFICIENT_BUFFER)
			*recv_length = command.data_length;
		return result;
	}
	if (*recv_length < command.data_length) {
		*recv_length = command.data_length;
		return SCARD_E_INSUFFICIENT_BUFFER;
	}
	memcpy(recv_buffer, command.data, command.data_length);
	*recv_length = command.data_length;
	if (recv_pci) {
		recv_pci->dwProtocol = SCARD_PROTOCOL_T1;
		recv_pci->cbPciLength = sizeof(SCARD_IO_REQUEST);
	}
	card->transmit_count++;
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardGetTransmitCount(SCARDHANDLE card_handle, LPDWORD count) try
{
	if (!count)
		return SCARD_E_INVALID_PARAMETER;
	*count = 0;
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	std::lock_guard<std::mutex> lock(card->mutex);
	if (card->backend == CardBackend::NATIVE) {
		auto get_count = px4::winscard::GetNativeFunction<
			decltype(&SCardGetTransmitCount)>("SCardGetTransmitCount");
		return get_count ? get_count(card->native_handle, count) :
			SCARD_E_NO_SERVICE;
	}
	*count = card->transmit_count;
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardControl(SCARDHANDLE card_handle, DWORD control_code,
	LPCVOID input, DWORD input_length, LPVOID output, DWORD output_length,
	LPDWORD bytes_returned) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (card->backend == CardBackend::NATIVE) {
		auto control = px4::winscard::GetNativeFunction<
			decltype(&SCardControl)>("SCardControl");
		if (!control)
			return SCARD_E_NO_SERVICE;
		std::lock_guard<std::mutex> lock(card->mutex);
		return control(card->native_handle, control_code, input, input_length,
			output, output_length, bytes_returned);
	}
	if (!bytes_returned)
		return SCARD_E_INVALID_PARAMETER;
	*bytes_returned = 0;
	return SCARD_E_UNSUPPORTED_FEATURE;
}
PX4_SCARD_CATCH

LONG WINAPI SCardGetAttrib(SCARDHANDLE card_handle, DWORD attribute,
	LPBYTE buffer, LPDWORD length) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (!length)
		return SCARD_E_INVALID_PARAMETER;
	std::lock_guard<std::mutex> lock(card->mutex);
	if (card->backend == CardBackend::NATIVE) {
		auto get_attrib = px4::winscard::GetNativeFunction<
			decltype(&SCardGetAttrib)>("SCardGetAttrib");
		return get_attrib ? get_attrib(card->native_handle, attribute,
			buffer, length) : SCARD_E_NO_SERVICE;
	}
	std::vector<std::uint8_t> value;
	auto append_dword = [&value](DWORD number) {
		const auto *bytes = reinterpret_cast<const BYTE *>(&number);
		value.assign(bytes, bytes + sizeof(number));
	};
	auto append_ansi = [&value](const std::string &text) {
		const auto *bytes = reinterpret_cast<const BYTE *>(text.c_str());
		value.assign(bytes, bytes + text.size() + 1);
	};
	auto append_wide = [&value](const std::wstring &text) {
		const auto *bytes = reinterpret_cast<const BYTE *>(text.c_str());
		value.assign(bytes, bytes + (text.size() + 1) * sizeof(wchar_t));
	};

	/* 一般的なカード利用ソフトが照会する静的属性を返す */
	if (attribute == SCARD_ATTR_ATR_STRING) {
		value = card->atr;
	} else if (attribute == SCARD_ATTR_CURRENT_PROTOCOL_TYPE ||
		attribute == SCARD_ATTR_PROTOCOL_TYPES) {
		append_dword(SCARD_PROTOCOL_T1);
	} else if (attribute == SCARD_ATTR_VENDOR_NAME) {
		append_ansi(card->reader.find(L"PX-MLT") != std::wstring::npos ?
			"PLEX" : "Digibest");
	} else if (attribute == SCARD_ATTR_VENDOR_IFD_TYPE ||
		attribute == SCARD_ATTR_DEVICE_FRIENDLY_NAME_A ||
		attribute == SCARD_ATTR_DEVICE_SYSTEM_NAME_A) {
		append_ansi(ToAnsi(card->reader));
	} else if (attribute == SCARD_ATTR_DEVICE_FRIENDLY_NAME_W ||
		attribute == SCARD_ATTR_DEVICE_SYSTEM_NAME_W) {
		append_wide(card->reader);
	} else if (attribute == SCARD_ATTR_VENDOR_IFD_VERSION) {
		append_dword(px4::card_command::VERSION);
	} else if (attribute == SCARD_ATTR_MAXINPUT) {
		append_dword(static_cast<DWORD>(px4::card_command::MAX_DATA_SIZE));
	} else {
		return ERROR_NOT_SUPPORTED;
	}
	DWORD supplied = *length;
	*length = static_cast<DWORD>(value.size());
	if (supplied == SCARD_AUTOALLOCATE) {
		if (!buffer)
			return SCARD_E_INVALID_PARAMETER;
		auto allocation = static_cast<BYTE *>(LocalAlloc(LMEM_FIXED, value.size()));
		if (!allocation)
			return SCARD_E_NO_MEMORY;
		memcpy(allocation, value.data(), value.size());
		if (!RegisterLocalAllocation(allocation)) {
			LocalFree(allocation);
			return SCARD_E_NO_MEMORY;
		}
		*reinterpret_cast<BYTE **>(buffer) = allocation;
		return SCARD_S_SUCCESS;
	}
	if (!buffer)
		return SCARD_S_SUCCESS;
	if (supplied < value.size())
		return SCARD_E_INSUFFICIENT_BUFFER;
	memcpy(buffer, value.data(), value.size());
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardSetAttrib(SCARDHANDLE card_handle, DWORD attribute,
	LPCBYTE buffer, DWORD length) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (card->backend == CardBackend::PX4)
		return ERROR_NOT_SUPPORTED;
	auto set_attrib = px4::winscard::GetNativeFunction<
		decltype(&SCardSetAttrib)>("SCardSetAttrib");
	if (!set_attrib)
		return SCARD_E_NO_SERVICE;
	std::lock_guard<std::mutex> lock(card->mutex);
	return set_attrib(card->native_handle, attribute, buffer, length);
}
PX4_SCARD_CATCH

} // extern "C"
