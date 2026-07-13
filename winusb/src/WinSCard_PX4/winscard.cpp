#define WINSCARDDATA __declspec(dllexport)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <winscard.h>

#include "card_client.hpp"

namespace {

#define PX4_SCARD_CATCH \
	catch (const std::bad_alloc &) { return SCARD_E_NO_MEMORY; } \
	catch (...) { return SCARD_F_INTERNAL_ERROR; }

constexpr wchar_t PNP_NOTIFICATION[] = L"\\\\?PnP?\\Notification";

struct Context final {
	/* 同じコンテキストで待機中の全スレッドが1回の SCardCancel() を観測する */
	std::atomic<std::uint64_t> cancel_generation{ 0 };
};

struct Card final {
	SCARDCONTEXT context = 0;
	std::wstring reader;
	std::unique_ptr<px4::CardClient> client;
	std::vector<std::uint8_t> atr;
	DWORD transmit_count = 0;
	std::mutex mutex;
};

std::mutex state_mutex;
std::unordered_map<SCARDCONTEXT, std::shared_ptr<Context>> contexts;
std::unordered_map<SCARDHANDLE, std::shared_ptr<Card>> cards;
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

std::wstring ToWide(LPCSTR value)
{
	if (!value)
		return {};

	int length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
	if (length <= 0)
		return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), length);
	result.pop_back();
	return result;
}

std::string ToAnsi(const std::wstring &value)
{
	int length = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1,
		nullptr, 0, nullptr, nullptr);
	if (length <= 0)
		return {};
	std::string result(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1,
		result.data(), length, nullptr, nullptr);
	result.pop_back();
	return result;
}

template <typename Char>
LONG CopyString(const std::basic_string<Char> &value, Char *buffer, LPDWORD length)
{
	if (!length)
		return SCARD_E_INVALID_PARAMETER;
	DWORD required = static_cast<DWORD>(value.size() + 1);
	DWORD supplied = *length;
	*length = required;

	if (supplied == SCARD_AUTOALLOCATE) {
		if (!buffer)
			return SCARD_E_INVALID_PARAMETER;
		auto allocation = static_cast<Char *>(LocalAlloc(LMEM_FIXED,
			required * sizeof(Char)));
		if (!allocation)
			return SCARD_E_NO_MEMORY;
		memcpy(allocation, value.c_str(), required * sizeof(Char));
		*reinterpret_cast<Char **>(buffer) = allocation;
		return SCARD_S_SUCCESS;
	}
	if (!buffer)
		return SCARD_S_SUCCESS;
	if (supplied < required)
		return SCARD_E_INSUFFICIENT_BUFFER;
	memcpy(buffer, value.c_str(), required * sizeof(Char));
	return SCARD_S_SUCCESS;
}

template <typename Char>
LONG CopyMultiString(const std::vector<std::basic_string<Char>> &values,
			     Char *buffer, LPDWORD length)
{
	if (!length)
		return SCARD_E_INVALID_PARAMETER;
	DWORD required = 1;
	for (const auto &value : values)
		required += static_cast<DWORD>(value.size() + 1);
	DWORD supplied = *length;
	*length = required;
	Char *destination = buffer;

	if (supplied == SCARD_AUTOALLOCATE) {
		if (!buffer)
			return SCARD_E_INVALID_PARAMETER;
		destination = static_cast<Char *>(LocalAlloc(LMEM_FIXED,
			required * sizeof(Char)));
		if (!destination)
			return SCARD_E_NO_MEMORY;
		*reinterpret_cast<Char **>(buffer) = destination;
	} else if (!buffer) {
		return SCARD_S_SUCCESS;
	} else if (supplied < required) {
		return SCARD_E_INSUFFICIENT_BUFFER;
	}

	for (const auto &value : values) {
		memcpy(destination, value.c_str(), (value.size() + 1) * sizeof(Char));
		destination += value.size() + 1;
	}
	*destination = 0;
	return SCARD_S_SUCCESS;
}

LONG ListReaders(std::vector<std::wstring> &readers,
		 std::uint64_t *reader_generation = nullptr) try
{
	px4::CardClient client;
	if (!client.Connect())
		return SCARD_E_NO_SERVICE;
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

	px4::CardClient client;
	if (!client.Connect())
		return SCARD_E_NO_SERVICE;
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
			if (reader == PNP_NOTIFICATION) {
				std::vector<std::wstring> readers;
				std::uint64_t generation = 0;
				LONG result = ListReaders(readers, &generation);
				if (result != SCARD_S_SUCCESS && result != SCARD_E_NO_READERS_AVAILABLE)
					return result;
				/* PC/SC の状態値へ世代を埋め込み、追加と削除の両方を通知する */
				DWORD event_state = static_cast<DWORD>((generation & 0xffffU) << 16);
				DWORD current = states[index].dwCurrentState & ~SCARD_STATE_CHANGED;
				if (states[index].dwCurrentState == SCARD_STATE_UNAWARE ||
					current != event_state) {
					event_state |= SCARD_STATE_CHANGED;
					any_changed = true;
				}
				states[index].dwEventState = event_state;
				continue;
			}

			bool present = false;
			bool initialized = false;
			std::vector<std::uint8_t> atr;
			LONG result = QueryStatus(client, reader, present, initialized, atr);
			DWORD event_state;
			/* Windows は不明なリーダーを以後の監視対象から外す */
			if (result == SCARD_E_UNKNOWN_READER)
				event_state = SCARD_STATE_UNKNOWN | SCARD_STATE_IGNORE;
			else if (result != SCARD_S_SUCCESS)
				return result;
			else
				event_state = present ? SCARD_STATE_PRESENT : SCARD_STATE_EMPTY;

			DWORD current = states[index].dwCurrentState & ~SCARD_STATE_CHANGED;
			if (states[index].dwCurrentState == SCARD_STATE_UNAWARE || current != event_state) {
				event_state |= SCARD_STATE_CHANGED;
				any_changed = true;
			}
			states[index].dwEventState = event_state;
			states[index].cbAtr = static_cast<DWORD>(
				std::min<std::size_t>(atr.size(), sizeof(states[index].rgbAtr)));
			if (states[index].cbAtr)
				memcpy(states[index].rgbAtr, atr.data(), states[index].cbAtr);
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
	if (!FindContext(context_handle))
		return SCARD_E_INVALID_HANDLE;
	if (!reader_name || !card_handle || !active_protocol)
		return SCARD_E_INVALID_PARAMETER;
	if (share_mode != SCARD_SHARE_SHARED && share_mode != SCARD_SHARE_EXCLUSIVE)
		return SCARD_E_INVALID_VALUE;
	if (!(preferred_protocols & SCARD_PROTOCOL_T1))
		return SCARD_E_PROTO_MISMATCH;

	std::wstring reader;
	if constexpr (std::is_same_v<ReaderChar, char>)
		reader = ToWide(reader_name);
	else
		reader = reader_name;
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

	bool present = false;
	bool initialized = false;
	std::vector<std::uint8_t> atr;
	result = QueryStatus(*client, reader, present, initialized, atr);
	if (result != SCARD_S_SUCCESS || !present || !initialized)
		return result == SCARD_S_SUCCESS ? SCARD_E_NO_SMARTCARD : result;

	auto card = std::make_shared<Card>();
	card->context = context_handle;
	card->reader = std::move(reader);
	card->client = std::move(client);
	card->atr = std::move(atr);
	SCARDHANDLE handle = static_cast<SCARDHANDLE>(next_handle.fetch_add(1));
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		cards.emplace(handle, std::move(card));
	}
	*card_handle = handle;
	*active_protocol = SCARD_PROTOCOL_T1;
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
		px4::card_command::Command command = {};
		command.code = px4::card_command::Code::DISCONNECT;
		card->client->Call(command);
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
	if (!FindContext(context_handle))
		return SCARD_E_INVALID_HANDLE;
	if (!memory)
		return SCARD_E_INVALID_PARAMETER;
	return LocalFree(const_cast<LPVOID>(memory)) ? SCARD_F_INTERNAL_ERROR : SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

HANDLE WINAPI SCardAccessStartedEvent(void) { return StartedEvent(); }
void WINAPI SCardReleaseStartedEvent(void) {}

LONG WINAPI SCardListReaderGroupsA(SCARDCONTEXT context_handle,
	LPSTR groups, LPDWORD length) try
{
	if (!FindContext(context_handle))
		return SCARD_E_INVALID_HANDLE;
	return CopyMultiString(std::vector<std::string>{ "SCard$DefaultReaders" }, groups, length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardListReaderGroupsW(SCARDCONTEXT context_handle,
	LPWSTR groups, LPDWORD length) try
{
	if (!FindContext(context_handle))
		return SCARD_E_INVALID_HANDLE;
	return CopyMultiString(std::vector<std::wstring>{ L"SCard$DefaultReaders" }, groups, length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardListReadersA(SCARDCONTEXT context_handle, LPCSTR,
	LPSTR reader_buffer, LPDWORD length) try
{
	if (!FindContext(context_handle))
		return SCARD_E_INVALID_HANDLE;
	std::vector<std::wstring> wide_readers;
	LONG result = ListReaders(wide_readers);
	if (result != SCARD_S_SUCCESS)
		return result;
	std::vector<std::string> readers;
	for (const auto &reader : wide_readers)
		readers.emplace_back(ToAnsi(reader));
	return CopyMultiString(readers, reader_buffer, length);
}
PX4_SCARD_CATCH

LONG WINAPI SCardListReadersW(SCARDCONTEXT context_handle, LPCWSTR,
	LPWSTR reader_buffer, LPDWORD length) try
{
	if (!FindContext(context_handle))
		return SCARD_E_INVALID_HANDLE;
	std::vector<std::wstring> readers;
	LONG result = ListReaders(readers);
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
	if (disposition != SCARD_LEAVE_CARD && disposition != SCARD_RESET_CARD &&
		disposition != SCARD_UNPOWER_CARD && disposition != SCARD_EJECT_CARD)
		return SCARD_E_INVALID_VALUE;
	if (disposition == SCARD_UNPOWER_CARD || disposition == SCARD_EJECT_CARD)
		return SCARD_E_UNSUPPORTED_FEATURE;

	std::shared_ptr<Card> card;
	{
		std::lock_guard<std::mutex> state_lock(state_mutex);
		auto entry = cards.find(card_handle);
		if (entry == cards.end())
			return SCARD_E_INVALID_HANDLE;
		card = entry->second;
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
	context->cancel_generation.fetch_add(1);
	return SCARD_S_SUCCESS;
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
	*count = card->transmit_count;
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

LONG WINAPI SCardControl(SCARDHANDLE card_handle, DWORD, LPCVOID, DWORD,
	LPVOID, DWORD, LPDWORD bytes_returned)
{
	if (!FindCard(card_handle))
		return SCARD_E_INVALID_HANDLE;
	if (bytes_returned)
		*bytes_returned = 0;
	return SCARD_E_UNSUPPORTED_FEATURE;
}

LONG WINAPI SCardGetAttrib(SCARDHANDLE card_handle, DWORD attribute,
	LPBYTE buffer, LPDWORD length) try
{
	auto card = FindCard(card_handle);
	if (!card)
		return SCARD_E_INVALID_HANDLE;
	if (!length)
		return SCARD_E_INVALID_PARAMETER;
	std::lock_guard<std::mutex> lock(card->mutex);
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

LONG WINAPI SCardSetAttrib(SCARDHANDLE card_handle, DWORD, LPCBYTE, DWORD)
{
	return FindCard(card_handle) ? ERROR_NOT_SUPPORTED : SCARD_E_INVALID_HANDLE;
}

} // extern "C"
