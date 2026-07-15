#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <winscard.h>

#include "../src/WinSCard_PX4/bcas_atr.hpp"

namespace {

bool CheckResult(const char *operation, LONG actual, LONG expected = SCARD_S_SUCCESS)
{
	if (actual == expected)
		return true;
	std::fprintf(stderr, "%s failed. actual: 0x%08lx, expected: 0x%08lx\n",
		operation, static_cast<unsigned long>(actual),
		static_cast<unsigned long>(expected));
	return false;
}

std::vector<std::wstring> SplitMultiString(const wchar_t *values)
{
	std::vector<std::wstring> result;
	for (const wchar_t *value = values; *value; value += wcslen(value) + 1)
		result.emplace_back(value);
	return result;
}

std::vector<std::wstring> SplitAnsiMultiString(const char *values)
{
	std::vector<std::wstring> result;
	for (const char *value = values; *value; value += strlen(value) + 1) {
		int length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
		if (length <= 0)
			continue;
		std::wstring wide(static_cast<std::size_t>(length), L'\0');
		MultiByteToWideChar(CP_ACP, 0, value, -1, wide.data(), length);
		wide.pop_back();
		result.emplace_back(std::move(wide));
	}
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

bool TestCancel(SCARDCONTEXT context)
{
	/* 初回の PnP 状態を取得し、以後は同じ世代からの変化を待つ */
	SCARD_READERSTATEW initial_state = {};
	initial_state.szReader = L"\\\\?PnP?\\Notification";
	initial_state.dwCurrentState = SCARD_STATE_UNAWARE;
	if (!CheckResult("SCardGetStatusChangeW(PnP initial)",
		SCardGetStatusChangeW(context, 0, &initial_state, 1)))
		return false;
	const DWORD current_pnp_state = initial_state.dwEventState & ~SCARD_STATE_CHANGED;

	std::atomic<unsigned int> ready{ 0 };
	LONG results[2] = {};
	std::thread waiters[2];

	/* 1回の SCardCancel() で同じコンテキストの全待機を解除する */
	for (std::size_t index = 0; index < std::size(waiters); index++) {
		waiters[index] = std::thread([context, current_pnp_state,
			&ready, &results, index]() {
			SCARD_READERSTATEW state = {};
			state.szReader = L"\\\\?PnP?\\Notification";
			state.dwCurrentState = current_pnp_state;
			ready.fetch_add(1);
			results[index] = SCardGetStatusChangeW(context, INFINITE, &state, 1);
		});
	}
	while (ready.load() != std::size(waiters))
		std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	bool succeeded = CheckResult("SCardCancel", SCardCancel(context));
	for (auto &waiter : waiters)
		waiter.join();
	for (LONG result : results)
		succeeded = CheckResult("SCardGetStatusChangeW(cancelled)", result,
			SCARD_E_CANCELLED) && succeeded;

	/* キャンセル後に開始した待機へ過去の要求を持ち越さない */
	SCARD_READERSTATEW state = {};
	state.szReader = L"\\\\?PnP?\\Notification";
	state.dwCurrentState = current_pnp_state;
	succeeded = CheckResult("SCardGetStatusChangeW(after cancel)",
		SCardGetStatusChangeW(context, 0, &state, 1), SCARD_E_TIMEOUT) && succeeded;
	return succeeded;
}

bool TestAttribute(SCARDCONTEXT context, SCARDHANDLE card, DWORD attribute)
{
	DWORD required = 0;
	if (!CheckResult("SCardGetAttrib(size)",
		SCardGetAttrib(card, attribute, nullptr, &required)) || !required)
		return false;

	/* 通常バッファと自動確保の双方で同じ属性値を受け取る */
	std::vector<BYTE> ordinary(required);
	DWORD ordinary_length = required;
	if (!CheckResult("SCardGetAttrib(buffer)",
		SCardGetAttrib(card, attribute, ordinary.data(), &ordinary_length)))
		return false;
	BYTE *automatic = nullptr;
	DWORD automatic_length = SCARD_AUTOALLOCATE;
	if (!CheckResult("SCardGetAttrib(auto)", SCardGetAttrib(card, attribute,
		reinterpret_cast<BYTE *>(&automatic), &automatic_length)))
		return false;
	bool matched = automatic && ordinary_length == automatic_length &&
		memcmp(ordinary.data(), automatic, ordinary_length) == 0;
	if (!matched)
		std::fprintf(stderr, "SCardGetAttrib returned inconsistent buffers. attribute: 0x%08lx\n",
			static_cast<unsigned long>(attribute));
	bool freed = CheckResult("SCardFreeMemory(attribute)",
		SCardFreeMemory(context, automatic));
	return matched && freed;
}

bool TestReaderDevice(SCARDCONTEXT context, const std::wstring &reader,
	bool &is_px4_reader, bool &is_card_present)
{
	/* 内蔵・外付けの双方でデバイス固有 ID が取得でき、同じリーダー名へ逆引きできることを確認する */
	DWORD instance_length = 0;
	LONG result = SCardGetReaderDeviceInstanceIdW(context, reader.c_str(),
		nullptr, &instance_length);
	bool succeeded = CheckResult("SCardGetReaderDeviceInstanceIdW(size)", result);
	if (result != SCARD_S_SUCCESS || !instance_length)
		return false;
	std::vector<wchar_t> instance(instance_length);
	result = SCardGetReaderDeviceInstanceIdW(context, reader.c_str(),
		instance.data(), &instance_length);
	succeeded = CheckResult("SCardGetReaderDeviceInstanceIdW", result) && succeeded;
	if (result != SCARD_S_SUCCESS)
		return false;
	const std::wstring instance_id(instance.data());
	is_px4_reader = instance_id.rfind(L"PX4_WINUSB\\CARD_READER_", 0) == 0;

	/* PX4 の仮想 ID だけは列挙順に依存しない末尾識別子まで検証する */
	if (is_px4_reader) {
		const auto separator = reader.rfind(L'#');
		if (separator == std::wstring::npos || instance_id !=
			L"PX4_WINUSB\\CARD_READER_" + reader.substr(separator + 1)) {
			std::fprintf(stderr, "PX4 reader device instance ID is not stable.\n");
			succeeded = false;
		}
	}
	wchar_t *automatic_instance = nullptr;
	DWORD automatic_instance_length = SCARD_AUTOALLOCATE;
	LONG automatic_result = SCardGetReaderDeviceInstanceIdW(context,
		reader.c_str(), reinterpret_cast<wchar_t *>(&automatic_instance),
		&automatic_instance_length);
	succeeded = CheckResult("SCardGetReaderDeviceInstanceIdW(auto)",
		automatic_result) && succeeded;
	if (automatic_result == SCARD_S_SUCCESS &&
		(result != SCARD_S_SUCCESS || !automatic_instance ||
		automatic_instance_length != instance_length ||
		wcscmp(automatic_instance, instance.data()) != 0)) {
		std::fprintf(stderr, "Reader device instance buffers do not match.\n");
		succeeded = false;
	}
	if (automatic_instance) {
		succeeded = CheckResult("SCardFreeMemory(device instance)",
			SCardFreeMemory(context, automatic_instance)) && succeeded;
	}

	DWORD instance_readers_length = 0;
	result = SCardListReadersWithDeviceInstanceIdW(context, instance.data(),
		nullptr, &instance_readers_length);
	std::vector<wchar_t> instance_readers(instance_readers_length);
	if (result == SCARD_S_SUCCESS) {
		result = SCardListReadersWithDeviceInstanceIdW(context, instance.data(),
			instance_readers.data(), &instance_readers_length);
	}
	succeeded = CheckResult("SCardListReadersWithDeviceInstanceIdW", result) &&
		succeeded;
	if (result == SCARD_S_SUCCESS) {
		const auto matched_readers = SplitMultiString(instance_readers.data());
		if (std::find(matched_readers.begin(), matched_readers.end(), reader) ==
			matched_readers.end()) {
			std::fprintf(stderr, "Reader device instance ID did not resolve back.\n");
			succeeded = false;
		}
	}

	/* 接続前の状態を取得し、カード未挿入の外付けリーダーも転送経路の検証対象に含める */
	SCARD_READERSTATEW reader_state = {};
	reader_state.szReader = reader.c_str();
	reader_state.dwCurrentState = SCARD_STATE_UNAWARE;
	result = SCardGetStatusChangeW(context, 0, &reader_state, 1);
	succeeded = CheckResult("SCardGetStatusChangeW(reader)", result) && succeeded;
	is_card_present = result == SCARD_S_SUCCESS &&
		(reader_state.dwEventState & SCARD_STATE_PRESENT) != 0;
	std::printf("reader=%ls backend=%s card=%s\n", reader.c_str(),
		is_px4_reader ? "px4" : "system", is_card_present ? "present" : "empty");
	return succeeded;
}

bool TestPx4Reader(SCARDCONTEXT context, const std::wstring &reader)
{
	bool succeeded = true;
	LONG result = SCARD_S_SUCCESS;

	SCARDHANDLE card = 0;
	DWORD protocol = 0;
	if (!CheckResult("SCardConnectW", SCardConnectW(context, reader.c_str(),
		SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1, &card, &protocol)))
		return false;
	succeeded = (protocol == SCARD_PROTOCOL_T1) && succeeded;
	DWORD invalid_reconnect_protocol = 0;
	succeeded = CheckResult("SCardReconnect(invalid share mode)",
		SCardReconnect(card, 0xffffffffU, SCARD_PROTOCOL_T1,
			SCARD_LEAVE_CARD, &invalid_reconnect_protocol),
		SCARD_E_INVALID_VALUE) && succeeded;
	succeeded = CheckResult("SCardReconnect(protocol mismatch)",
		SCardReconnect(card, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0,
			SCARD_LEAVE_CARD, &invalid_reconnect_protocol),
		SCARD_E_PROTO_MISMATCH) && succeeded;

	/* 共有接続は併用でき、共有中の排他要求だけを拒否する */
	SCARDHANDLE second_shared = 0;
	DWORD second_protocol = 0;
	succeeded = CheckResult("SCardConnectW(second shared)",
		SCardConnectW(context, reader.c_str(), SCARD_SHARE_SHARED,
			SCARD_PROTOCOL_T1, &second_shared, &second_protocol)) && succeeded;
	SCARDHANDLE conflicting_exclusive = 0;
	DWORD conflicting_protocol = 0;
	succeeded = CheckResult("SCardConnectW(conflicting exclusive)",
		SCardConnectW(context, reader.c_str(), SCARD_SHARE_EXCLUSIVE,
			SCARD_PROTOCOL_T1, &conflicting_exclusive, &conflicting_protocol),
		SCARD_E_SHARING_VIOLATION) && succeeded;
	succeeded = CheckResult("SCardReconnect(conflicting exclusive)",
		SCardReconnect(card, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T1,
			SCARD_LEAVE_CARD, &protocol), SCARD_E_SHARING_VIOLATION) && succeeded;
	if (second_shared)
		succeeded = CheckResult("SCardDisconnect(second shared)",
			SCardDisconnect(second_shared, SCARD_LEAVE_CARD)) && succeeded;
	succeeded = CheckResult("SCardReconnect(exclusive)",
		SCardReconnect(card, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T1,
			SCARD_LEAVE_CARD, &protocol)) && succeeded;
	SCARDHANDLE blocked_shared = 0;
	DWORD blocked_protocol = 0;
	succeeded = CheckResult("SCardConnectW(blocked shared)",
		SCardConnectW(context, reader.c_str(), SCARD_SHARE_SHARED,
			SCARD_PROTOCOL_T1, &blocked_shared, &blocked_protocol),
		SCARD_E_SHARING_VIOLATION) && succeeded;
	succeeded = CheckResult("SCardReconnect(shared)",
		SCardReconnect(card, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
			SCARD_LEAVE_CARD, &protocol)) && succeeded;

	/* SCardStatus() のリーダー名と ATR を自動確保で確認する */
	wchar_t *status_readers = nullptr;
	BYTE *status_atr = nullptr;
	DWORD status_readers_length = SCARD_AUTOALLOCATE;
	DWORD status_atr_length = SCARD_AUTOALLOCATE;
	DWORD state = 0;
	DWORD status_protocol = 0;
	result = SCardStatusW(card, reinterpret_cast<wchar_t *>(&status_readers),
		&status_readers_length, &state, &status_protocol,
		reinterpret_cast<BYTE *>(&status_atr), &status_atr_length);
	succeeded = CheckResult("SCardStatusW", result) && succeeded;
	if (result == SCARD_S_SUCCESS) {
		bool valid_status = status_readers && status_atr && status_atr_length > 0 &&
			status_readers_length == reader.size() + 2 &&
			status_readers[reader.size()] == L'\0' &&
			status_readers[reader.size() + 1] == L'\0' &&
			state == SCARD_SPECIFIC && status_protocol == SCARD_PROTOCOL_T1;
		if (!valid_status)
			std::fprintf(stderr, "SCardStatusW returned invalid status data.\n");
		succeeded = valid_status && succeeded;
	}
	if (status_readers)
		succeeded = CheckResult("SCardFreeMemory(reader)",
			SCardFreeMemory(context, status_readers)) && succeeded;
	if (status_atr)
		succeeded = CheckResult("SCardFreeMemory(atr)",
			SCardFreeMemory(context, status_atr)) && succeeded;

	/* 旧 SCardState() も Windows 固有の列挙値で同じ接続状態を返す */
	BYTE state_atr[SCARD_ATR_LENGTH] = {};
	DWORD state_atr_length = sizeof(state_atr);
	DWORD legacy_state = 0;
	DWORD legacy_protocol = 0;
	result = SCardState(card, &legacy_state, &legacy_protocol,
		state_atr, &state_atr_length);
	succeeded = CheckResult("SCardState", result) && succeeded;
	if (result == SCARD_S_SUCCESS) {
		bool valid_state = legacy_state == SCARD_SPECIFIC &&
			legacy_protocol == SCARD_PROTOCOL_T1 && state_atr_length > 0;
		if (!valid_state)
			std::fprintf(stderr, "SCardState returned invalid state data.\n");
		succeeded = valid_state && succeeded;
	}

	const DWORD attributes[] = {
		SCARD_ATTR_ATR_STRING,
		SCARD_ATTR_CURRENT_PROTOCOL_TYPE,
		SCARD_ATTR_PROTOCOL_TYPES,
		SCARD_ATTR_VENDOR_NAME,
		SCARD_ATTR_VENDOR_IFD_TYPE,
		SCARD_ATTR_VENDOR_IFD_VERSION,
		SCARD_ATTR_DEVICE_FRIENDLY_NAME_A,
		SCARD_ATTR_DEVICE_FRIENDLY_NAME_W,
		SCARD_ATTR_MAXINPUT,
	};
	for (DWORD attribute : attributes)
		succeeded = TestAttribute(context, card, attribute) && succeeded;

	BYTE unsupported[8] = {};
	DWORD unsupported_length = sizeof(unsupported);
	succeeded = CheckResult("SCardGetAttrib(unsupported)",
		SCardGetAttrib(card, SCARD_ATTR_CURRENT_CLK, unsupported,
			&unsupported_length), ERROR_NOT_SUPPORTED) && succeeded;
	succeeded = CheckResult("SCardDisconnect(invalid)",
		SCardDisconnect(card, 0xffffffffU), SCARD_E_INVALID_VALUE) && succeeded;

	/* 不正な切断要求の後も同じハンドルで APDU を送信できることを確認する */
	const BYTE apdu[] = { 0x90, 0x30, 0x00, 0x00, 0x00 };
	BYTE response[128] = {};
	DWORD response_length = sizeof(response);
	result = SCardTransmit(card, SCARD_PCI_T1, apdu, sizeof(apdu), nullptr,
		response, &response_length);
	succeeded = CheckResult("SCardTransmit", result) && succeeded;
	if (result == SCARD_S_SUCCESS) {
		bool valid_response = response_length >= 2 &&
			response[response_length - 2] == 0x90 &&
			response[response_length - 1] == 0x00;
		if (!valid_response)
			std::fprintf(stderr, "SCardTransmit returned an invalid B-CAS response.\n");
		succeeded = valid_response && succeeded;
	}

	/* 小さい受信バッファには実際に必要な応答サイズを返す */
	BYTE short_response[1] = {};
	DWORD short_response_length = sizeof(short_response);
	result = SCardTransmit(card, SCARD_PCI_T1, apdu, sizeof(apdu), nullptr,
		short_response, &short_response_length);
	succeeded = CheckResult("SCardTransmit(short buffer)", result,
		SCARD_E_INSUFFICIENT_BUFFER) && succeeded;
	if (result == SCARD_E_INSUFFICIENT_BUFFER && short_response_length <= 1) {
		std::fprintf(stderr, "SCardTransmit did not return the required buffer size.\n");
		succeeded = false;
	}

	succeeded = CheckResult("SCardBeginTransaction",
		SCardBeginTransaction(card)) && succeeded;
	succeeded = CheckResult("SCardEndTransaction(reset)",
		SCardEndTransaction(card, SCARD_RESET_CARD)) && succeeded;
	succeeded = CheckResult("SCardDisconnect",
		SCardDisconnect(card, SCARD_LEAVE_CARD)) && succeeded;
	std::printf("reader=%ls result=%s\n", reader.c_str(),
		succeeded ? "passed" : "failed");
	return succeeded;
}

bool TestNativeReader(SCARDCONTEXT context, const std::wstring &reader,
	bool is_card_present)
{
	/* Windows に登録済みのカード名を使い、外付けリーダーの検索が System32 まで届くことを確認する */
	wchar_t *card_names = nullptr;
	DWORD card_names_length = SCARD_AUTOALLOCATE;
	LONG result = SCardListCardsW(context, nullptr, nullptr, 0,
		reinterpret_cast<wchar_t *>(&card_names), &card_names_length);
	bool succeeded = CheckResult("SCardListCardsW(native locate)", result);
	if (result == SCARD_S_SUCCESS) {
		const auto names = SplitMultiString(card_names);
		if (!names.empty()) {
			std::vector<wchar_t> locate_names(names.front().begin(),
				names.front().end());
			locate_names.emplace_back(L'\0');
			locate_names.emplace_back(L'\0');
			SCARD_READERSTATEW locate_state = {};
			locate_state.szReader = reader.c_str();
			locate_state.dwCurrentState = SCARD_STATE_UNAWARE;
			result = SCardLocateCardsW(context, locate_names.data(),
				&locate_state, 1);
			succeeded = CheckResult("SCardLocateCardsW(native)", result) &&
				succeeded;
		}
	}
	if (card_names) {
		succeeded = CheckResult("SCardFreeMemory(native card names)",
			SCardFreeMemory(context, card_names)) && succeeded;
	}

	SCARDHANDLE card = 0;
	DWORD protocol = 0;
	result = SCardConnectW(context, reader.c_str(), SCARD_SHARE_SHARED,
		SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &card, &protocol);

	/* 空の外付けリーダーは Windows の標準エラーまで一致すれば転送できている */
	if (!is_card_present) {
		succeeded = CheckResult("SCardConnectW(native empty)", result,
			SCARD_E_NO_SMARTCARD) && succeeded;
		std::printf("reader=%ls result=%s\n", reader.c_str(),
			succeeded ? "passed" : "failed");
		return succeeded;
	}
	if (!CheckResult("SCardConnectW(native)", result))
		return false;

	/* カード挿入時は接続ハンドルも System32 側へ転送されることを状態取得で確認する */
	DWORD state = 0;
	DWORD status_protocol = 0;
	wchar_t *status_readers = nullptr;
	BYTE *status_atr = nullptr;
	DWORD reader_length = SCARD_AUTOALLOCATE;
	DWORD atr_length = SCARD_AUTOALLOCATE;
	succeeded = CheckResult("SCardStatusW(native)", SCardStatusW(card,
		reinterpret_cast<wchar_t *>(&status_readers), &reader_length, &state,
		&status_protocol, reinterpret_cast<BYTE *>(&status_atr), &atr_length)) &&
		succeeded;
	if (!status_readers || reader != status_readers || !status_atr ||
		status_protocol != protocol || state != SCARD_SPECIFIC || !atr_length) {
		std::fprintf(stderr, "SCardStatusW returned invalid native card data.\n");
		succeeded = false;
	}
	const bool is_bcas = px4::winscard::IsBcasAtr(status_atr, atr_length);
	if (status_readers) {
		succeeded = CheckResult("SCardFreeMemory(native reader)",
			SCardFreeMemory(context, status_readers)) && succeeded;
	}
	if (status_atr) {
		succeeded = CheckResult("SCardFreeMemory(native ATR)",
			SCardFreeMemory(context, status_atr)) && succeeded;
	}

	/* B-CAS と確認できたカードだけに固有 APDU を送り、別用途のカードへ干渉しない */
	if (is_bcas) {
		const BYTE apdu[] = { 0x90, 0x30, 0x00, 0x00, 0x00 };
		BYTE response[128] = {};
		DWORD response_length = sizeof(response);
		result = SCardTransmit(card, protocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 :
			SCARD_PCI_T0, apdu, sizeof(apdu), nullptr, response, &response_length);
		succeeded = CheckResult("SCardTransmit(native B-CAS)", result) && succeeded;
		if (result == SCARD_S_SUCCESS && (response_length < 2 ||
			response[response_length - 2] != 0x90 ||
			response[response_length - 1] != 0x00)) {
			std::fprintf(stderr, "External card returned an invalid B-CAS response.\n");
			succeeded = false;
		}
	}
	succeeded = CheckResult("SCardDisconnect(native)",
		SCardDisconnect(card, SCARD_LEAVE_CARD)) && succeeded;
	std::printf("reader=%ls result=%s\n", reader.c_str(),
		succeeded ? "passed" : "failed");
	return succeeded;
}

bool TestAnsiReader(SCARDCONTEXT context, const std::wstring &reader)
{
	const std::string ansi_reader = ToAnsi(reader);
	if (ansi_reader.empty())
		return false;
	SCARDHANDLE card = 0;
	DWORD protocol = 0;
	LONG connect_result = SCardConnectA(context, ansi_reader.c_str(),
		SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &card, &protocol);
	if (!CheckResult("SCardConnectA", connect_result)) {
		std::fwprintf(stderr, L"ANSI connection failed. reader: %ls\n", reader.c_str());
		return false;
	}

	/* ANSI 名で接続したカードも ATR を調べ、B-CAS の場合だけ固有 APDU を確認する */
	BYTE atr[SCARD_ATR_LENGTH] = {};
	DWORD atr_length = sizeof(atr);
	DWORD state = 0;
	DWORD state_protocol = 0;
	LONG result = SCardState(card, &state, &state_protocol, atr, &atr_length);
	bool succeeded = CheckResult("SCardState(ANSI)", result);
	if (result == SCARD_S_SUCCESS && px4::winscard::IsBcasAtr(atr, atr_length)) {
		const BYTE apdu[] = { 0x90, 0x30, 0x00, 0x00, 0x00 };
		BYTE response[128] = {};
		DWORD response_length = sizeof(response);
		result = SCardTransmit(card, protocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 :
			SCARD_PCI_T0, apdu, sizeof(apdu), nullptr, response, &response_length);
		succeeded = CheckResult("SCardTransmit(ANSI B-CAS)", result) && succeeded;
		if (result == SCARD_S_SUCCESS && (response_length < 2 ||
			response[response_length - 2] != 0x90 ||
			response[response_length - 1] != 0x00)) {
			std::fprintf(stderr, "ANSI connection returned an invalid B-CAS response.\n");
			succeeded = false;
		}
	}
	succeeded = CheckResult("SCardDisconnect(ANSI)",
		SCardDisconnect(card, SCARD_LEAVE_CARD)) && succeeded;
	return succeeded;
}

} // namespace

int wmain()
{
	SCARDCONTEXT context = 0;
	if (!CheckResult("SCardEstablishContext", SCardEstablishContext(
		SCARD_SCOPE_SYSTEM, nullptr, nullptr, &context)))
		return 1;

	/* 既知の B-CAS だけを受け入れ、1バイト違う別カードを誤認しないことを確認する */
	bool succeeded = true;
	for (const auto &atr : px4::winscard::BCAS_ATRS)
		succeeded = px4::winscard::IsBcasAtr(atr.data(), atr.size()) && succeeded;
	auto similar_atr = px4::winscard::BCAS_ATRS.back();
	similar_atr.back() ^= 0x01;
	if (px4::winscard::IsBcasAtr(similar_atr.data(), similar_atr.size())) {
		std::fprintf(stderr, "Another ATR was misidentified as B-CAS.\n");
		succeeded = false;
	}

	/* 外部入力の固定長 IPC 境界を越えてもプロセスを終了させない */
	std::wstring long_reader_name(256, L'X');
	SCARDHANDLE invalid_card = 0;
	DWORD invalid_protocol = 0;
	succeeded = CheckResult("SCardConnectW(long reader name)",
		SCardConnectW(context, long_reader_name.c_str(), SCARD_SHARE_SHARED,
			SCARD_PROTOCOL_T1, &invalid_card, &invalid_protocol),
		SCARD_E_UNKNOWN_READER) && succeeded;
	succeeded = CheckResult("SCardLocateCardsW(invalid context)",
		SCardLocateCardsW(0, nullptr, nullptr, 0), SCARD_E_INVALID_HANDLE) &&
		succeeded;
	SCARD_ATRMASK invalid_mask = {};
	invalid_mask.cbAtr = SCARD_ATR_LENGTH + 1;
	succeeded = CheckResult("SCardLocateCardsByATRW(invalid ATR length)",
		SCardLocateCardsByATRW(context, &invalid_mask, 1, nullptr, 0),
		SCARD_E_INVALID_PARAMETER) && succeeded;

	/* context == 0 を許すカード種別 API も System32 と同じ登録情報を返す */
	DWORD card_names_length = 0;
	LONG result = SCardListCardsW(0, nullptr, nullptr, 0, nullptr,
		&card_names_length);
	succeeded = CheckResult("SCardListCardsW(null context size)", result) &&
		succeeded;
	std::vector<wchar_t> card_names(card_names_length);
	if (result == SCARD_S_SUCCESS) {
		result = SCardListCardsW(0, nullptr, nullptr, 0, card_names.data(),
			&card_names_length);
		succeeded = CheckResult("SCardListCardsW(null context)", result) &&
			succeeded;
	}
	const auto registered_card_names = result == SCARD_S_SUCCESS &&
		!card_names.empty() ?
		SplitMultiString(card_names.data()) : std::vector<std::wstring>();
	wchar_t *automatic_card_names = nullptr;
	DWORD automatic_card_names_length = SCARD_AUTOALLOCATE;
	result = SCardListCardsW(0, nullptr, nullptr, 0,
		reinterpret_cast<wchar_t *>(&automatic_card_names),
		&automatic_card_names_length);
	succeeded = CheckResult("SCardListCardsW(null context autoallocate)", result) &&
		succeeded;
	if (automatic_card_names) {
		succeeded = CheckResult("SCardFreeMemory(null context card names)",
			SCardFreeMemory(0, automatic_card_names)) && succeeded;
	}

	/* 存在しないリーダーの状態値も実装差の診断用に表示する */
	SCARD_READERSTATEW unknown = {};
	unknown.szReader = L"PX4 unknown reader";
	unknown.dwCurrentState = SCARD_STATE_UNAWARE;
	LONG unknown_result = SCardGetStatusChangeW(context, 0, &unknown, 1);
	std::printf("unknown_result=0x%08lx unknown_state=0x%08lx\n",
		static_cast<unsigned long>(unknown_result),
		static_cast<unsigned long>(unknown.dwEventState));

	wchar_t *reader_buffer = nullptr;
	DWORD reader_length = SCARD_AUTOALLOCATE;
	result = SCardListReadersW(context, nullptr,
		reinterpret_cast<wchar_t *>(&reader_buffer), &reader_length);
	if (!CheckResult("SCardListReadersW", result)) {
		SCardReleaseContext(context);
		return 1;
	}
	std::vector<std::wstring> readers = SplitMultiString(reader_buffer);
	succeeded = CheckResult("SCardFreeMemory(readers)",
		SCardFreeMemory(context, reader_buffer)) && succeeded;

	/* ANSI 版も同じ固定順序を返し、libaribb25 など従来の利用者から外付けを選択できることを確認する */
	char *ansi_reader_buffer = nullptr;
	DWORD ansi_reader_length = SCARD_AUTOALLOCATE;
	result = SCardListReadersA(context, nullptr,
		reinterpret_cast<char *>(&ansi_reader_buffer), &ansi_reader_length);
	succeeded = CheckResult("SCardListReadersA", result) && succeeded;
	if (result == SCARD_S_SUCCESS &&
		SplitAnsiMultiString(ansi_reader_buffer) != readers) {
		std::fprintf(stderr, "ANSI and Unicode reader lists do not match.\n");
		succeeded = false;
	}
	if (ansi_reader_buffer) {
		succeeded = CheckResult("SCardFreeMemory(ANSI readers)",
			SCardFreeMemory(context, ansi_reader_buffer)) && succeeded;
	}

	/* 未登録のカード名は Windows 標準と同じエラーで拒否する */
	if (!readers.empty()) {
		SCARD_READERSTATEW locate_state = {};
		locate_state.szReader = readers.front().c_str();
		locate_state.dwCurrentState = SCARD_STATE_UNAWARE;
		const wchar_t unknown_cards[] = L"Unknown card\0";
		result = SCardLocateCardsW(context, unknown_cards, &locate_state, 1);
		succeeded = CheckResult("SCardLocateCardsW(unknown card)", result,
			SCARD_E_UNKNOWN_CARD) && succeeded;

		/* 登録済みカード名は全リーダーを同じ ATR 照合処理で検索する */
		if (!registered_card_names.empty()) {
			std::vector<wchar_t> locate_names(registered_card_names.front().begin(),
				registered_card_names.front().end());
			locate_names.emplace_back(L'\0');
			locate_names.emplace_back(L'\0');
			std::vector<SCARD_READERSTATEW> locate_states(readers.size());
			for (std::size_t index = 0; index < readers.size(); index++) {
				locate_states[index].szReader = readers[index].c_str();
				locate_states[index].dwCurrentState = SCARD_STATE_UNAWARE;
			}
			result = SCardLocateCardsW(context, locate_names.data(),
				locate_states.data(), static_cast<DWORD>(locate_states.size()));
			succeeded = CheckResult("SCardLocateCardsW(registered card)", result) &&
				succeeded;
		}
	}

	succeeded = TestCancel(context) && succeeded;
	bool native_reader_seen = false;
	for (const auto &reader : readers) {
		bool is_px4_reader = false;
		bool is_card_present = false;
		bool device_succeeded = TestReaderDevice(context, reader, is_px4_reader,
			is_card_present);
		succeeded = device_succeeded && succeeded;

		/* 一度 System32 側へ移った後に PX4 リーダーが現れた場合は固定順序違反とする */
		if (is_px4_reader && native_reader_seen) {
			std::fprintf(stderr, "PX4 reader was listed after a system reader.\n");
			succeeded = false;
		}
		if (!is_px4_reader)
			native_reader_seen = true;
		if (!device_succeeded)
			continue;

		/* PX4 は B-CAS 通信を、System32 側はカード有無に応じた標準動作を検証する */
		if (is_px4_reader) {
			if (is_card_present) {
				succeeded = TestPx4Reader(context, reader) && succeeded;
				succeeded = TestAnsiReader(context, reader) && succeeded;
			} else {
				std::printf("reader=%ls result=skipped (no card)\n", reader.c_str());
			}
		} else {
			succeeded = TestNativeReader(context, reader, is_card_present) && succeeded;
			if (is_card_present)
				succeeded = TestAnsiReader(context, reader) && succeeded;
		}
	}
	succeeded = CheckResult("SCardReleaseContext",
		SCardReleaseContext(context)) && succeeded;
	std::printf("readers=%zu result=%s\n", readers.size(),
		succeeded ? "passed" : "failed");
	return succeeded && !readers.empty() ? 0 : 1;
}
