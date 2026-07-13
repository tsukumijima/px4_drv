#define WINSCARDDATA __declspec(dllexport)

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include <windows.h>
#include <winscard.h>

namespace {

#define PX4_SCARD_CATCH \
	catch (const std::bad_alloc &) { return SCARD_E_NO_MEMORY; } \
	catch (...) { return SCARD_F_INTERNAL_ERROR; }

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

	/* SCARD_AUTOALLOCATE は winscard.cpp と同じ LocalAlloc() で確保する */
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

template <typename Char>
LONG CopyString(const std::basic_string<Char> &value, Char *buffer, LPDWORD length)
{
	if (!length)
		return SCARD_E_INVALID_PARAMETER;
	DWORD required = static_cast<DWORD>(value.size() + 1);
	DWORD supplied = *length;
	*length = required;

	/* SCARD_AUTOALLOCATE はポインタの格納先を受け取り、解放可能な領域を返す */
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
LONG ListReaders(SCARDCONTEXT context,
	std::vector<std::basic_string<Char>> &readers) try
{
	DWORD length = 0;
	LONG result;
	if constexpr (std::is_same_v<Char, char>)
		result = SCardListReadersA(context, nullptr, nullptr, &length);
	else
		result = SCardListReadersW(context, nullptr, nullptr, &length);
	if (result != SCARD_S_SUCCESS)
		return result;

	std::vector<Char> buffer(length);
	if constexpr (std::is_same_v<Char, char>)
		result = SCardListReadersA(context, nullptr, buffer.data(), &length);
	else
		result = SCardListReadersW(context, nullptr, buffer.data(), &length);
	if (result != SCARD_S_SUCCESS)
		return result;

	/* MULTI_SZ の終端まで各リーダー名を取り出す */
	for (const Char *name = buffer.data(); *name; name += std::char_traits<Char>::length(name) + 1)
		readers.emplace_back(name);
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

template <typename Char>
LONG ListCards(SCARDCONTEXT context, Char *cards, LPDWORD length) try
{
	LONG result = SCardIsValidContext(context);
	if (result != SCARD_S_SUCCESS)
		return result;
	std::vector<std::basic_string<Char>> names;
	if constexpr (std::is_same_v<Char, char>)
		names.emplace_back("B-CAS");
	else
		names.emplace_back(L"B-CAS");
	return CopyMultiString(names, cards, length);
}
PX4_SCARD_CATCH

template <typename Char, typename ReaderState>
LONG LocateCards(SCARDCONTEXT context, const Char *card_names,
	ReaderState *states, DWORD count) try
{
	LONG result = SCardIsValidContext(context);
	if (result != SCARD_S_SUCCESS)
		return result;
	if (!card_names || (count && !states))
		return SCARD_E_INVALID_PARAMETER;

	/* 未知のカード種別は無視し、このプロバイダーが公開する B-CAS だけを検索する */
	bool search_bcas = false;
	for (const Char *name = card_names; *name;
		name += std::char_traits<Char>::length(name) + 1) {
		if constexpr (std::is_same_v<Char, char>)
			search_bcas = search_bcas || strcmp(name, "B-CAS") == 0;
		else
			search_bcas = search_bcas || wcscmp(name, L"B-CAS") == 0;
	}

	if constexpr (std::is_same_v<ReaderState, SCARD_READERSTATEA>)
		result = SCardGetStatusChangeA(context, 0, states, count);
	else
		result = SCardGetStatusChangeW(context, 0, states, count);
	if (result != SCARD_S_SUCCESS)
		return result;

	/* 呼び出し元が以前の検索結果を再利用しても古い ATRMATCH を残さない */
	for (DWORD index = 0; index < count; index++) {
		states[index].dwEventState &= ~SCARD_STATE_ATRMATCH;
		if (search_bcas && (states[index].dwEventState & SCARD_STATE_PRESENT))
			states[index].dwEventState |= SCARD_STATE_ATRMATCH;
	}
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

template <typename ReaderState>
LONG LocateCardsByAtr(SCARDCONTEXT context, LPSCARD_ATRMASK masks,
	DWORD mask_count, ReaderState *states, DWORD state_count) try
{
	if ((mask_count && !masks) || (state_count && !states))
		return SCARD_E_INVALID_PARAMETER;
	for (DWORD mask_index = 0; mask_index < mask_count; mask_index++) {
		if (masks[mask_index].cbAtr > SCARD_ATR_LENGTH)
			return SCARD_E_INVALID_PARAMETER;
	}
	LONG result;
	if constexpr (std::is_same_v<ReaderState, SCARD_READERSTATEA>)
		result = SCardGetStatusChangeA(context, 0, states, state_count);
	else
		result = SCardGetStatusChangeW(context, 0, states, state_count);
	if (result != SCARD_S_SUCCESS)
		return result;

	/* ATR とマスクの全バイトが一致したリーダーだけ ATRMATCH にする */
	for (DWORD state_index = 0; state_index < state_count; state_index++) {
		states[state_index].dwEventState &= ~SCARD_STATE_ATRMATCH;
		if (!(states[state_index].dwEventState & SCARD_STATE_PRESENT))
			continue;
		for (DWORD mask_index = 0; mask_index < mask_count; mask_index++) {
			if (masks[mask_index].cbAtr > states[state_index].cbAtr)
				continue;
			bool matched = true;
			for (DWORD byte_index = 0; byte_index < masks[mask_index].cbAtr; byte_index++) {
				BYTE difference = (states[state_index].rgbAtr[byte_index] ^
					masks[mask_index].rgbAtr[byte_index]) &
					masks[mask_index].rgbMask[byte_index];
				if (difference) {
					matched = false;
					break;
				}
			}
			if (matched) {
				states[state_index].dwEventState |= SCARD_STATE_ATRMATCH;
				break;
			}
		}
	}
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

template <typename Char>
LONG GetDeviceType(SCARDCONTEXT context, const Char *reader, LPDWORD type) try
{
	if (!reader || !type)
		return SCARD_E_INVALID_PARAMETER;
	std::vector<std::basic_string<Char>> readers;
	LONG result = ListReaders(context, readers);
	if (result != SCARD_S_SUCCESS)
		return result;
	if (std::find(readers.begin(), readers.end(), reader) == readers.end())
		return SCARD_E_UNKNOWN_READER;
	*type = SCARD_READER_TYPE_USB;
	return SCARD_S_SUCCESS;
}
PX4_SCARD_CATCH

template <typename Char>
LONG CopyDeviceInstance(SCARDCONTEXT context, const Char *reader,
	Char *instance, LPDWORD length) try
{
	if (!reader)
		return SCARD_E_INVALID_PARAMETER;
	std::vector<std::basic_string<Char>> readers;
	LONG result = ListReaders(context, readers);
	if (result != SCARD_S_SUCCESS)
		return result;
	auto entry = std::find(readers.begin(), readers.end(), reader);
	if (entry == readers.end())
		return SCARD_E_UNKNOWN_READER;

	/* リーダー名末尾のデバイスパス由来ハッシュを使い、他機器の増減で ID を変えない */
	const auto separator = entry->rfind(static_cast<Char>('#'));
	if (separator == std::basic_string<Char>::npos ||
		entry->size() - separator - 1 != 16)
		return SCARD_F_INTERNAL_ERROR;
	const auto reader_id = entry->substr(separator + 1);
	std::basic_string<Char> value;
	if constexpr (std::is_same_v<Char, char>)
		value = "PX4_WINUSB\\CARD_READER_" + reader_id;
	else
		value = L"PX4_WINUSB\\CARD_READER_" + reader_id;
	return CopyString(value, instance, length);
}
PX4_SCARD_CATCH

template <typename Char>
LONG ListReadersWithDeviceInstance(SCARDCONTEXT context, const Char *instance,
	Char *reader_buffer, LPDWORD length) try
{
	if (!instance)
		return SCARD_E_INVALID_PARAMETER;
	std::vector<std::basic_string<Char>> readers;
	LONG result = ListReaders(context, readers);
	if (result != SCARD_S_SUCCESS)
		return result;

	std::vector<std::basic_string<Char>> matched;
	for (const auto &reader : readers) {
		DWORD instance_length = 0;
		result = CopyDeviceInstance(context, reader.c_str(),
			static_cast<Char *>(nullptr), &instance_length);
		if (result != SCARD_S_SUCCESS)
			return result;
		std::vector<Char> value(instance_length);
		result = CopyDeviceInstance(context, reader.c_str(), value.data(), &instance_length);
		if (result != SCARD_S_SUCCESS)
			return result;
		if (std::basic_string<Char>(value.data()) == instance)
			matched.emplace_back(reader);
	}
	if (matched.empty())
		return SCARD_E_UNKNOWN_READER;
	return CopyMultiString(matched, reader_buffer, length);
}
PX4_SCARD_CATCH

} // namespace

extern "C" {

LONG WINAPI SCardListCardsA(SCARDCONTEXT context, LPCBYTE, LPCGUID, DWORD,
	LPSTR cards, LPDWORD length) { return ListCards(context, cards, length); }
LONG WINAPI SCardListCardsW(SCARDCONTEXT context, LPCBYTE, LPCGUID, DWORD,
	LPWSTR cards, LPDWORD length) { return ListCards(context, cards, length); }
/* B-CAS に登録済みインターフェイスはなく、長さ0の配列は書き込み対象を持たない */
#pragma warning(suppress: 6101)
LONG WINAPI SCardListInterfacesA(SCARDCONTEXT, LPCSTR card_name, LPGUID,
	LPDWORD count)
{
	if (!card_name || !count)
		return SCARD_E_INVALID_PARAMETER;
	*count = 0;
	return strcmp(card_name, "B-CAS") == 0 ?
		SCARD_S_SUCCESS : SCARD_E_UNKNOWN_CARD;
}
/* B-CAS に登録済みインターフェイスはなく、長さ0の配列は書き込み対象を持たない */
#pragma warning(suppress: 6101)
LONG WINAPI SCardListInterfacesW(SCARDCONTEXT, LPCWSTR card_name, LPGUID,
	LPDWORD count)
{
	if (!card_name || !count)
		return SCARD_E_INVALID_PARAMETER;
	*count = 0;
	return wcscmp(card_name, L"B-CAS") == 0 ?
		SCARD_S_SUCCESS : SCARD_E_UNKNOWN_CARD;
}
LONG WINAPI SCardGetProviderIdA(SCARDCONTEXT, LPCSTR, LPGUID provider)
{
	if (provider)
		*provider = GUID_NULL;
	return SCARD_E_UNSUPPORTED_FEATURE;
}
LONG WINAPI SCardGetProviderIdW(SCARDCONTEXT, LPCWSTR, LPGUID provider)
{
	if (provider)
		*provider = GUID_NULL;
	return SCARD_E_UNSUPPORTED_FEATURE;
}
LONG WINAPI SCardGetCardTypeProviderNameA(SCARDCONTEXT, LPCSTR, DWORD,
	LPSTR name, LPDWORD length)
{
	if (length) {
		if (name && *length)
			*name = '\0';
		*length = 0;
	}
	return SCARD_E_UNSUPPORTED_FEATURE;
}
LONG WINAPI SCardGetCardTypeProviderNameW(SCARDCONTEXT, LPCWSTR, DWORD,
	LPWSTR name, LPDWORD length)
{
	if (length) {
		if (name && *length)
			*name = L'\0';
		*length = 0;
	}
	return SCARD_E_UNSUPPORTED_FEATURE;
}

LONG WINAPI SCardIntroduceReaderGroupA(SCARDCONTEXT,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardIntroduceReaderGroupW(SCARDCONTEXT,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardForgetReaderGroupA(SCARDCONTEXT,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardForgetReaderGroupW(SCARDCONTEXT,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardIntroduceReaderA(SCARDCONTEXT, LPCSTR,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardIntroduceReaderW(SCARDCONTEXT, LPCWSTR,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardForgetReaderA(SCARDCONTEXT,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardForgetReaderW(SCARDCONTEXT,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardAddReaderToGroupA(SCARDCONTEXT, LPCSTR,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardAddReaderToGroupW(SCARDCONTEXT, LPCWSTR,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardRemoveReaderFromGroupA(SCARDCONTEXT, LPCSTR,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardRemoveReaderFromGroupW(SCARDCONTEXT, LPCWSTR,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardIntroduceCardTypeA(SCARDCONTEXT, LPCSTR, LPCGUID, LPCGUID,
	DWORD, LPCBYTE, LPCBYTE, DWORD) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardIntroduceCardTypeW(SCARDCONTEXT, LPCWSTR, LPCGUID, LPCGUID,
	DWORD, LPCBYTE, LPCBYTE, DWORD) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardSetCardTypeProviderNameA(SCARDCONTEXT, LPCSTR, DWORD,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardSetCardTypeProviderNameW(SCARDCONTEXT, LPCWSTR, DWORD,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardForgetCardTypeA(SCARDCONTEXT,
	LPCSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardForgetCardTypeW(SCARDCONTEXT,
	LPCWSTR) { return SCARD_E_UNSUPPORTED_FEATURE; }

LONG WINAPI SCardLocateCardsA(SCARDCONTEXT context, LPCSTR card_names,
	LPSCARD_READERSTATEA states, DWORD count)
{
	return LocateCards(context, card_names, states, count);
}
LONG WINAPI SCardLocateCardsW(SCARDCONTEXT context, LPCWSTR card_names,
	LPSCARD_READERSTATEW states, DWORD count)
{
	return LocateCards(context, card_names, states, count);
}
LONG WINAPI SCardLocateCardsByATRA(SCARDCONTEXT context, LPSCARD_ATRMASK masks,
	DWORD mask_count, LPSCARD_READERSTATEA states, DWORD state_count)
{
	return LocateCardsByAtr(context, masks, mask_count, states, state_count);
}
LONG WINAPI SCardLocateCardsByATRW(SCARDCONTEXT context, LPSCARD_ATRMASK masks,
	DWORD mask_count, LPSCARD_READERSTATEW states, DWORD state_count)
{
	return LocateCardsByAtr(context, masks, mask_count, states, state_count);
}

LONG WINAPI SCardReadCacheA(SCARDCONTEXT, UUID *, DWORD, LPSTR, PBYTE,
	DWORD *length)
{
	if (length)
		*length = 0;
	return SCARD_E_UNSUPPORTED_FEATURE;
}
LONG WINAPI SCardReadCacheW(SCARDCONTEXT, UUID *, DWORD, LPWSTR, PBYTE,
	DWORD *length)
{
	if (length)
		*length = 0;
	return SCARD_E_UNSUPPORTED_FEATURE;
}
LONG WINAPI SCardWriteCacheA(SCARDCONTEXT, UUID *, DWORD, LPSTR, PBYTE,
	DWORD) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardWriteCacheW(SCARDCONTEXT, UUID *, DWORD, LPWSTR, PBYTE,
	DWORD) { return SCARD_E_UNSUPPORTED_FEATURE; }
LONG WINAPI SCardGetReaderIconA(SCARDCONTEXT, LPCSTR, LPBYTE,
	LPDWORD length)
{
	if (length)
		*length = 0;
	return SCARD_E_UNSUPPORTED_FEATURE;
}
LONG WINAPI SCardGetReaderIconW(SCARDCONTEXT, LPCWSTR, LPBYTE,
	LPDWORD length)
{
	if (length)
		*length = 0;
	return SCARD_E_UNSUPPORTED_FEATURE;
}
LONG WINAPI SCardGetDeviceTypeIdA(SCARDCONTEXT context, LPCSTR reader,
	LPDWORD type) { return GetDeviceType(context, reader, type); }
LONG WINAPI SCardGetDeviceTypeIdW(SCARDCONTEXT context, LPCWSTR reader,
	LPDWORD type) { return GetDeviceType(context, reader, type); }
LONG WINAPI SCardGetReaderDeviceInstanceIdA(SCARDCONTEXT context, LPCSTR reader,
	LPSTR instance, LPDWORD length) { return CopyDeviceInstance(context, reader, instance, length); }
LONG WINAPI SCardGetReaderDeviceInstanceIdW(SCARDCONTEXT context, LPCWSTR reader,
	LPWSTR instance, LPDWORD length) { return CopyDeviceInstance(context, reader, instance, length); }
LONG WINAPI SCardListReadersWithDeviceInstanceIdA(SCARDCONTEXT context,
	LPCSTR instance, LPSTR readers, LPDWORD length)
{
	return ListReadersWithDeviceInstance(context, instance, readers, length);
}
LONG WINAPI SCardListReadersWithDeviceInstanceIdW(SCARDCONTEXT context,
	LPCWSTR instance, LPWSTR readers, LPDWORD length)
{
	return ListReadersWithDeviceInstance(context, instance, readers, length);
}
LONG WINAPI SCardAudit(SCARDCONTEXT,
	DWORD) { return SCARD_E_UNSUPPORTED_FEATURE; }

DWORD CALLBACK ClassInstall32(DWORD, DWORD, LPVOID) { return ERROR_CALL_NOT_IMPLEMENTED; }
HANDLE WINAPI SCardAccessNewReaderEvent(void) { return nullptr; }
void WINAPI SCardReleaseAllEvents(void) {}
void WINAPI SCardReleaseNewReaderEvent(void) {}
const SCARD_IO_REQUEST * WINAPI SCardPciRaw(void) { return &g_rgSCardRawPci; }
const SCARD_IO_REQUEST * WINAPI SCardPciT0(void) { return &g_rgSCardT0Pci; }
const SCARD_IO_REQUEST * WINAPI SCardPciT1(void) { return &g_rgSCardT1Pci; }

} // extern "C"
