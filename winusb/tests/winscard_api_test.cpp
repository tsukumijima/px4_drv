#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <winscard.h>

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

bool TestReader(SCARDCONTEXT context, const std::wstring &reader)
{
	/* デバイス固有 ID がリーダー名の列挙位置に依存せず、逆引きできることを確認する */
	DWORD instance_length = 0;
	LONG result = SCardGetReaderDeviceInstanceIdW(context, reader.c_str(),
		nullptr, &instance_length);
	bool succeeded = CheckResult("SCardGetReaderDeviceInstanceIdW(size)", result);
	std::vector<wchar_t> instance(instance_length);
	if (result == SCARD_S_SUCCESS) {
		result = SCardGetReaderDeviceInstanceIdW(context, reader.c_str(),
			instance.data(), &instance_length);
		succeeded = CheckResult("SCardGetReaderDeviceInstanceIdW", result) &&
			succeeded;
	}
	const auto separator = reader.rfind(L'#');
	if (result == SCARD_S_SUCCESS && (separator == std::wstring::npos ||
		std::wstring(instance.data()) !=
			L"PX4_WINUSB\\CARD_READER_" + reader.substr(separator + 1))) {
		std::fprintf(stderr, "Reader device instance ID is not stable.\n");
		succeeded = false;
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

} // namespace

int wmain()
{
	SCARDCONTEXT context = 0;
	if (!CheckResult("SCardEstablishContext", SCardEstablishContext(
		SCARD_SCOPE_SYSTEM, nullptr, nullptr, &context)))
		return 1;

	/* 外部入力の固定長 IPC 境界を越えてもプロセスを終了させない */
	std::wstring long_reader_name(256, L'X');
	SCARDHANDLE invalid_card = 0;
	DWORD invalid_protocol = 0;
	bool succeeded = CheckResult("SCardConnectW(long reader name)",
		SCardConnectW(context, long_reader_name.c_str(), SCARD_SHARE_SHARED,
			SCARD_PROTOCOL_T1, &invalid_card, &invalid_protocol),
		SCARD_E_UNKNOWN_READER);
	succeeded = CheckResult("SCardLocateCardsW(invalid context)",
		SCardLocateCardsW(0, nullptr, nullptr, 0), SCARD_E_INVALID_HANDLE) &&
		succeeded;
	SCARD_ATRMASK invalid_mask = {};
	invalid_mask.cbAtr = SCARD_ATR_LENGTH + 1;
	succeeded = CheckResult("SCardLocateCardsByATRW(invalid ATR length)",
		SCardLocateCardsByATRW(context, &invalid_mask, 1, nullptr, 0),
		SCARD_E_INVALID_PARAMETER) && succeeded;

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
	LONG result = SCardListReadersW(context, nullptr,
		reinterpret_cast<wchar_t *>(&reader_buffer), &reader_length);
	if (!CheckResult("SCardListReadersW", result)) {
		SCardReleaseContext(context);
		return 1;
	}
	std::vector<std::wstring> readers = SplitMultiString(reader_buffer);
	succeeded = CheckResult("SCardFreeMemory(readers)",
		SCardFreeMemory(context, reader_buffer)) && succeeded;

	/* カード名による検索が未知の種別を B-CAS と誤認しないことを確認する */
	if (!readers.empty()) {
		SCARD_READERSTATEW locate_state = {};
		locate_state.szReader = readers.front().c_str();
		locate_state.dwCurrentState = SCARD_STATE_UNAWARE;
		const wchar_t unknown_cards[] = L"Unknown card\0";
		result = SCardLocateCardsW(context, unknown_cards, &locate_state, 1);
		succeeded = CheckResult("SCardLocateCardsW(unknown card)", result) &&
			succeeded;
		if (result == SCARD_S_SUCCESS &&
			(locate_state.dwEventState & SCARD_STATE_ATRMATCH)) {
			std::fprintf(stderr, "Unknown card unexpectedly matched a reader.\n");
			succeeded = false;
		}

		locate_state = {};
		locate_state.szReader = readers.front().c_str();
		locate_state.dwCurrentState = SCARD_STATE_UNAWARE;
		const wchar_t bcas_cards[] = L"B-CAS\0";
		result = SCardLocateCardsW(context, bcas_cards, &locate_state, 1);
		succeeded = CheckResult("SCardLocateCardsW(B-CAS)", result) && succeeded;
		if (result == SCARD_S_SUCCESS) {
			if (!(locate_state.dwEventState & SCARD_STATE_PRESENT)) {
				std::printf("SCardLocateCardsW did not detect a present card.\n");
			} else if (!(locate_state.dwEventState & SCARD_STATE_ATRMATCH)) {
				std::fprintf(stderr, "B-CAS did not match a present card.\n");
				succeeded = false;
			}
		}
	}

	succeeded = TestCancel(context) && succeeded;
	for (const auto &reader : readers)
		succeeded = TestReader(context, reader) && succeeded;
	succeeded = CheckResult("SCardReleaseContext",
		SCardReleaseContext(context)) && succeeded;
	std::printf("readers=%zu result=%s\n", readers.size(),
		succeeded ? "passed" : "failed");
	return succeeded && !readers.empty() ? 0 : 1;
}
