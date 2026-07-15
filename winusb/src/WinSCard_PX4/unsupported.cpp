#define WINSCARDDATA __declspec(dllexport)

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include <windows.h>
#include <winscard.h>

#include "native_winscard.hpp"
#include "proxy_support.hpp"

namespace {

#define PX4_SCARD_CATCH \
	catch (const std::bad_alloc &) { return SCARD_E_NO_MEMORY; } \
	catch (...) { return SCARD_F_INTERNAL_ERROR; }

template <typename Function, typename... Args>
LONG CallNative(SCARDCONTEXT context, const char *name, Args... args)
{
	SCARDCONTEXT native_context = 0;
	/* SCardListCards() など context == 0 を許す API は System32 にもそのまま渡す */
	if (context) {
		if (!px4::winscard::GetNativeContext(context, native_context))
			return SCARD_E_INVALID_HANDLE;
		if (!native_context)
			return SCARD_E_NO_SERVICE;
	}
	auto function = px4::winscard::GetNativeFunction<Function>(name);
	return function ? function(native_context, args...) : SCARD_E_NO_SERVICE;
}

template <typename Char>
std::wstring ReaderToWide(const Char *reader)
{
	if (!reader)
		return {};
	if constexpr (std::is_same_v<Char, wchar_t>) {
		return reader;
	} else {
		int length = MultiByteToWideChar(CP_ACP, 0, reader, -1, nullptr, 0);
		if (length <= 0)
			return {};
		std::wstring result(static_cast<std::size_t>(length), L'\0');
		MultiByteToWideChar(CP_ACP, 0, reader, -1, result.data(), length);
		result.pop_back();
		return result;
	}
}

template <typename Char>
bool IsPx4Reader(const Char *reader)
{
	return reader && px4::winscard::IsPx4ReaderName(ReaderToWide(reader));
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

	/* SCARD_AUTOALLOCATE は winscard.cpp と同じ LocalAlloc() で確保する */
	if (supplied == SCARD_AUTOALLOCATE) {
		if (!buffer)
			return SCARD_E_INVALID_PARAMETER;
		destination = static_cast<Char *>(LocalAlloc(LMEM_FIXED,
			required * sizeof(Char)));
		if (!destination)
			return SCARD_E_NO_MEMORY;
		if (!px4::winscard::RegisterProxyAllocation(destination)) {
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
		if (!px4::winscard::RegisterProxyAllocation(allocation)) {
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
LONG QueryNativeCards(SCARDCONTEXT context, LPCBYTE atr, LPCGUID interfaces,
	DWORD interface_count, std::vector<std::basic_string<Char>> &names)
{
	DWORD native_length = 0;
	LONG result;
	if constexpr (std::is_same_v<Char, char>)
		result = CallNative<decltype(&SCardListCardsA)>(context,
			"SCardListCardsA", atr, interfaces, interface_count, nullptr,
			&native_length);
	else
		result = CallNative<decltype(&SCardListCardsW)>(context,
			"SCardListCardsW", atr, interfaces, interface_count, nullptr,
			&native_length);
	if (result != SCARD_S_SUCCESS || !native_length)
		return result;

	std::vector<Char> native_cards(native_length);
	if constexpr (std::is_same_v<Char, char>)
		result = CallNative<decltype(&SCardListCardsA)>(context,
			"SCardListCardsA", atr, interfaces, interface_count,
			native_cards.data(), &native_length);
	else
		result = CallNative<decltype(&SCardListCardsW)>(context,
			"SCardListCardsW", atr, interfaces, interface_count,
			native_cards.data(), &native_length);
	if (result != SCARD_S_SUCCESS)
		return result;

	/* Windows のカード種別データベースが返した MULTI_SZ を文字列単位に分解する */
	for (const Char *name = native_cards.data(); *name;
		name += std::char_traits<Char>::length(name) + 1)
		names.emplace_back(name);
	return SCARD_S_SUCCESS;
}

template <typename Char>
LONG ListCards(SCARDCONTEXT context, LPCBYTE atr, LPCGUID interfaces,
	DWORD interface_count, Char *cards, LPDWORD length) try
{
	std::vector<std::basic_string<Char>> names;
	LONG result = QueryNativeCards(context, atr, interfaces, interface_count,
		names);
	if (result != SCARD_S_SUCCESS)
		return result;
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

	std::vector<std::basic_string<Char>> requested_names;
	for (const Char *name = card_names; *name;
		name += std::char_traits<Char>::length(name) + 1)
		requested_names.emplace_back(name);

	/* 未登録名を含む場合は System32 と同じ SCARD_E_UNKNOWN_CARD を返す */
	std::vector<std::basic_string<Char>> registered_names;
	result = QueryNativeCards<Char>(context, nullptr, nullptr, 0,
		registered_names);
	if (result != SCARD_S_SUCCESS)
		return result;
	for (const auto &name : requested_names) {
		if (std::find(registered_names.begin(), registered_names.end(), name) ==
			registered_names.end())
			return SCARD_E_UNKNOWN_CARD;
	}

	/* 統合済みの状態取得を使い、内蔵・外付けを同じ ATR 照合へ流す */
	if constexpr (std::is_same_v<ReaderState, SCARD_READERSTATEA>)
		result = SCardGetStatusChangeA(context, 0, states, count);
	else
		result = SCardGetStatusChangeW(context, 0, states, count);
	if (result != SCARD_S_SUCCESS)
		return result;

	for (DWORD index = 0; index < count; index++) {
		states[index].dwEventState &= ~SCARD_STATE_ATRMATCH;
		if (!(states[index].dwEventState & SCARD_STATE_PRESENT))
			continue;

		/* 実際の ATR を Windows の登録情報へ問い合わせ、要求された名前との共通項を探す */
		std::vector<std::basic_string<Char>> matching_names;
		result = QueryNativeCards<Char>(context, states[index].rgbAtr, nullptr, 0,
			matching_names);
		if (result != SCARD_S_SUCCESS)
			return result;
		const bool matched = std::any_of(requested_names.begin(),
			requested_names.end(), [&matching_names](const auto &name) {
				return std::find(matching_names.begin(), matching_names.end(),
					name) != matching_names.end();
			});
		if (matched)
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
	if (!IsPx4Reader(reader)) {
		if constexpr (std::is_same_v<Char, char>)
			return CallNative<decltype(&SCardGetDeviceTypeIdA)>(context,
				"SCardGetDeviceTypeIdA", reader, type);
		else
			return CallNative<decltype(&SCardGetDeviceTypeIdW)>(context,
				"SCardGetDeviceTypeIdW", reader, type);
	}
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
	if (!IsPx4Reader(reader)) {
		if constexpr (std::is_same_v<Char, char>)
			return CallNative<decltype(&SCardGetReaderDeviceInstanceIdA)>(context,
				"SCardGetReaderDeviceInstanceIdA", reader, instance, length);
		else
			return CallNative<decltype(&SCardGetReaderDeviceInstanceIdW)>(context,
				"SCardGetReaderDeviceInstanceIdW", reader, instance, length);
	}
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
	const std::basic_string<Char> instance_name(instance);
	const std::basic_string<Char> px4_prefix = []() {
		if constexpr (std::is_same_v<Char, char>)
			return std::basic_string<Char>("PX4_WINUSB\\CARD_READER_");
		else
			return std::basic_string<Char>(L"PX4_WINUSB\\CARD_READER_");
	}();
	if (instance_name.rfind(px4_prefix, 0) != 0) {
		if constexpr (std::is_same_v<Char, char>)
			return CallNative<decltype(&SCardListReadersWithDeviceInstanceIdA)>(
				context, "SCardListReadersWithDeviceInstanceIdA", instance,
				reader_buffer, length);
		else
			return CallNative<decltype(&SCardListReadersWithDeviceInstanceIdW)>(
				context, "SCardListReadersWithDeviceInstanceIdW", instance,
				reader_buffer, length);
	}
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

LONG WINAPI SCardListCardsA(SCARDCONTEXT context, LPCBYTE atr,
	LPCGUID interfaces, DWORD interface_count, LPSTR cards, LPDWORD length)
{
	return ListCards(context, atr, interfaces, interface_count, cards, length);
}
LONG WINAPI SCardListCardsW(SCARDCONTEXT context, LPCBYTE atr,
	LPCGUID interfaces, DWORD interface_count, LPWSTR cards, LPDWORD length)
{
	return ListCards(context, atr, interfaces, interface_count, cards, length);
}
LONG WINAPI SCardListInterfacesA(SCARDCONTEXT context, LPCSTR card_name,
	LPGUID interfaces, LPDWORD count)
{
	return CallNative<decltype(&SCardListInterfacesA)>(context,
		"SCardListInterfacesA", card_name, interfaces, count);
}
LONG WINAPI SCardListInterfacesW(SCARDCONTEXT context, LPCWSTR card_name,
	LPGUID interfaces, LPDWORD count)
{
	return CallNative<decltype(&SCardListInterfacesW)>(context,
		"SCardListInterfacesW", card_name, interfaces, count);
}
LONG WINAPI SCardGetProviderIdA(SCARDCONTEXT context, LPCSTR card,
	LPGUID provider)
{
	return CallNative<decltype(&SCardGetProviderIdA)>(context,
		"SCardGetProviderIdA", card, provider);
}
LONG WINAPI SCardGetProviderIdW(SCARDCONTEXT context, LPCWSTR card,
	LPGUID provider)
{
	return CallNative<decltype(&SCardGetProviderIdW)>(context,
		"SCardGetProviderIdW", card, provider);
}
LONG WINAPI SCardGetCardTypeProviderNameA(SCARDCONTEXT context, LPCSTR card,
	DWORD provider_id, LPSTR name, LPDWORD length)
{
	return CallNative<decltype(&SCardGetCardTypeProviderNameA)>(context,
		"SCardGetCardTypeProviderNameA", card, provider_id, name, length);
}
LONG WINAPI SCardGetCardTypeProviderNameW(SCARDCONTEXT context, LPCWSTR card,
	DWORD provider_id, LPWSTR name, LPDWORD length)
{
	return CallNative<decltype(&SCardGetCardTypeProviderNameW)>(context,
		"SCardGetCardTypeProviderNameW", card, provider_id, name, length);
}

LONG WINAPI SCardIntroduceReaderGroupA(SCARDCONTEXT context, LPCSTR group)
{
	return CallNative<decltype(&SCardIntroduceReaderGroupA)>(context,
		"SCardIntroduceReaderGroupA", group);
}
LONG WINAPI SCardIntroduceReaderGroupW(SCARDCONTEXT context, LPCWSTR group)
{
	return CallNative<decltype(&SCardIntroduceReaderGroupW)>(context,
		"SCardIntroduceReaderGroupW", group);
}
LONG WINAPI SCardForgetReaderGroupA(SCARDCONTEXT context, LPCSTR group)
{
	return CallNative<decltype(&SCardForgetReaderGroupA)>(context,
		"SCardForgetReaderGroupA", group);
}
LONG WINAPI SCardForgetReaderGroupW(SCARDCONTEXT context, LPCWSTR group)
{
	return CallNative<decltype(&SCardForgetReaderGroupW)>(context,
		"SCardForgetReaderGroupW", group);
}
LONG WINAPI SCardIntroduceReaderA(SCARDCONTEXT context, LPCSTR reader,
	LPCSTR device)
{
	return CallNative<decltype(&SCardIntroduceReaderA)>(context,
		"SCardIntroduceReaderA", reader, device);
}
LONG WINAPI SCardIntroduceReaderW(SCARDCONTEXT context, LPCWSTR reader,
	LPCWSTR device)
{
	return CallNative<decltype(&SCardIntroduceReaderW)>(context,
		"SCardIntroduceReaderW", reader, device);
}
LONG WINAPI SCardForgetReaderA(SCARDCONTEXT context, LPCSTR reader)
{
	return CallNative<decltype(&SCardForgetReaderA)>(context,
		"SCardForgetReaderA", reader);
}
LONG WINAPI SCardForgetReaderW(SCARDCONTEXT context, LPCWSTR reader)
{
	return CallNative<decltype(&SCardForgetReaderW)>(context,
		"SCardForgetReaderW", reader);
}
LONG WINAPI SCardAddReaderToGroupA(SCARDCONTEXT context, LPCSTR reader,
	LPCSTR group)
{
	return CallNative<decltype(&SCardAddReaderToGroupA)>(context,
		"SCardAddReaderToGroupA", reader, group);
}
LONG WINAPI SCardAddReaderToGroupW(SCARDCONTEXT context, LPCWSTR reader,
	LPCWSTR group)
{
	return CallNative<decltype(&SCardAddReaderToGroupW)>(context,
		"SCardAddReaderToGroupW", reader, group);
}
LONG WINAPI SCardRemoveReaderFromGroupA(SCARDCONTEXT context, LPCSTR reader,
	LPCSTR group)
{
	return CallNative<decltype(&SCardRemoveReaderFromGroupA)>(context,
		"SCardRemoveReaderFromGroupA", reader, group);
}
LONG WINAPI SCardRemoveReaderFromGroupW(SCARDCONTEXT context, LPCWSTR reader,
	LPCWSTR group)
{
	return CallNative<decltype(&SCardRemoveReaderFromGroupW)>(context,
		"SCardRemoveReaderFromGroupW", reader, group);
}
LONG WINAPI SCardIntroduceCardTypeA(SCARDCONTEXT context, LPCSTR card,
	LPCGUID primary_provider, LPCGUID interfaces, DWORD interface_count,
	LPCBYTE atr, LPCBYTE mask, DWORD atr_length)
{
	return CallNative<decltype(&SCardIntroduceCardTypeA)>(context,
		"SCardIntroduceCardTypeA", card, primary_provider, interfaces,
		interface_count, atr, mask, atr_length);
}
LONG WINAPI SCardIntroduceCardTypeW(SCARDCONTEXT context, LPCWSTR card,
	LPCGUID primary_provider, LPCGUID interfaces, DWORD interface_count,
	LPCBYTE atr, LPCBYTE mask, DWORD atr_length)
{
	return CallNative<decltype(&SCardIntroduceCardTypeW)>(context,
		"SCardIntroduceCardTypeW", card, primary_provider, interfaces,
		interface_count, atr, mask, atr_length);
}
LONG WINAPI SCardSetCardTypeProviderNameA(SCARDCONTEXT context, LPCSTR card,
	DWORD provider_id, LPCSTR provider)
{
	return CallNative<decltype(&SCardSetCardTypeProviderNameA)>(context,
		"SCardSetCardTypeProviderNameA", card, provider_id, provider);
}
LONG WINAPI SCardSetCardTypeProviderNameW(SCARDCONTEXT context, LPCWSTR card,
	DWORD provider_id, LPCWSTR provider)
{
	return CallNative<decltype(&SCardSetCardTypeProviderNameW)>(context,
		"SCardSetCardTypeProviderNameW", card, provider_id, provider);
}
LONG WINAPI SCardForgetCardTypeA(SCARDCONTEXT context, LPCSTR card)
{
	return CallNative<decltype(&SCardForgetCardTypeA)>(context,
		"SCardForgetCardTypeA", card);
}
LONG WINAPI SCardForgetCardTypeW(SCARDCONTEXT context, LPCWSTR card)
{
	return CallNative<decltype(&SCardForgetCardTypeW)>(context,
		"SCardForgetCardTypeW", card);
}

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

LONG WINAPI SCardReadCacheA(SCARDCONTEXT context, UUID *card_id,
	DWORD freshness, LPSTR key, PBYTE data, DWORD *length)
{
	return CallNative<decltype(&SCardReadCacheA)>(context, "SCardReadCacheA",
		card_id, freshness, key, data, length);
}
LONG WINAPI SCardReadCacheW(SCARDCONTEXT context, UUID *card_id,
	DWORD freshness, LPWSTR key, PBYTE data, DWORD *length)
{
	return CallNative<decltype(&SCardReadCacheW)>(context, "SCardReadCacheW",
		card_id, freshness, key, data, length);
}
LONG WINAPI SCardWriteCacheA(SCARDCONTEXT context, UUID *card_id,
	DWORD freshness, LPSTR key, PBYTE data, DWORD length)
{
	return CallNative<decltype(&SCardWriteCacheA)>(context, "SCardWriteCacheA",
		card_id, freshness, key, data, length);
}
LONG WINAPI SCardWriteCacheW(SCARDCONTEXT context, UUID *card_id,
	DWORD freshness, LPWSTR key, PBYTE data, DWORD length)
{
	return CallNative<decltype(&SCardWriteCacheW)>(context, "SCardWriteCacheW",
		card_id, freshness, key, data, length);
}
LONG WINAPI SCardGetReaderIconA(SCARDCONTEXT context, LPCSTR reader,
	LPBYTE icon, LPDWORD length) try
{
	if (IsPx4Reader(reader)) {
		if (length)
			*length = 0;
		return SCARD_E_UNSUPPORTED_FEATURE;
	}
	return CallNative<decltype(&SCardGetReaderIconA)>(context,
		"SCardGetReaderIconA", reader, icon, length);
}
PX4_SCARD_CATCH
LONG WINAPI SCardGetReaderIconW(SCARDCONTEXT context, LPCWSTR reader,
	LPBYTE icon, LPDWORD length) try
{
	if (IsPx4Reader(reader)) {
		if (length)
			*length = 0;
		return SCARD_E_UNSUPPORTED_FEATURE;
	}
	return CallNative<decltype(&SCardGetReaderIconW)>(context,
		"SCardGetReaderIconW", reader, icon, length);
}
PX4_SCARD_CATCH
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
LONG WINAPI SCardAudit(SCARDCONTEXT context, DWORD event)
{
	return CallNative<decltype(&SCardAudit)>(context, "SCardAudit", event);
}

DWORD CALLBACK ClassInstall32(DWORD function, DWORD flags, LPVOID data)
{
	auto install = px4::winscard::GetNativeFunction<decltype(&ClassInstall32)>(
		"ClassInstall32");
	return install ? install(function, flags, data) : ERROR_CALL_NOT_IMPLEMENTED;
}
HANDLE WINAPI SCardAccessNewReaderEvent(void)
{
	auto access = px4::winscard::GetNativeFunction<
		decltype(&SCardAccessNewReaderEvent)>("SCardAccessNewReaderEvent");
	return access ? access() : nullptr;
}
void WINAPI SCardReleaseAllEvents(void)
{
	auto release = px4::winscard::GetNativeFunction<
		decltype(&SCardReleaseAllEvents)>("SCardReleaseAllEvents");
	if (release)
		release();
}
void WINAPI SCardReleaseNewReaderEvent(void)
{
	auto release = px4::winscard::GetNativeFunction<
		decltype(&SCardReleaseNewReaderEvent)>("SCardReleaseNewReaderEvent");
	if (release)
		release();
}
const SCARD_IO_REQUEST * WINAPI SCardPciRaw(void) { return &g_rgSCardRawPci; }
const SCARD_IO_REQUEST * WINAPI SCardPciT0(void) { return &g_rgSCardT0Pci; }
const SCARD_IO_REQUEST * WINAPI SCardPciT1(void) { return &g_rgSCardT1Pci; }

} // extern "C"
