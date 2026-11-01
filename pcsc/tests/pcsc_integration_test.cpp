// SPDX-License-Identifier: GPL-2.0-only
#include <winscard.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

/* run-pcsc-integration.sh が専用の pcscd と模擬の IFD ハンドラーを起動した環境でだけ実行する */
int main()
{
	SCARDCONTEXT context = 0;
	LONG result;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	do {
		result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &context);
		if (result == SCARD_S_SUCCESS)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	} while (std::chrono::steady_clock::now() < deadline);
	assert(result == SCARD_S_SUCCESS);
	char readers[1024]{};
	DWORD size;
	do {
		size = sizeof(readers);
		result = SCardListReaders(context, nullptr, readers, &size);
		if (result == SCARD_S_SUCCESS)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	} while (std::chrono::steady_clock::now() < deadline);
	assert(result == SCARD_S_SUCCESS);
	assert(std::strncmp(readers, "PX4 isolated test", 17) == 0);
	SCARD_READERSTATE state{};
	state.szReader = readers;
	state.dwCurrentState = SCARD_STATE_UNAWARE;
	assert(SCardGetStatusChange(context, 5000, &state, 1) == SCARD_S_SUCCESS);
	assert(state.dwEventState & SCARD_STATE_PRESENT);
	SCARDHANDLE handle = 0;
	DWORD protocol = 0;
	assert(SCardConnect(context, readers, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
		&handle, &protocol) == SCARD_S_SUCCESS);
	assert(protocol == SCARD_PROTOCOL_T1);
	SCARDHANDLE exclusive = 0;
	DWORD exclusive_protocol = 0;
	assert(SCardConnect(context, readers, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T1,
		&exclusive, &exclusive_protocol) == SCARD_E_SHARING_VIOLATION);
	assert(SCardBeginTransaction(handle) == SCARD_S_SUCCESS);
	unsigned char apdu[] = {0, 0x84, 0, 0, 0}, reply[16]{};
	SCARD_IO_REQUEST received{};
	size = sizeof(reply);
	assert(SCardTransmit(handle, SCARD_PCI_T1, apdu, sizeof(apdu), &received,
		reply, &size) == SCARD_S_SUCCESS);
	assert(size == 2 && reply[0] == 0x90 && reply[1] == 0);
	assert(received.dwProtocol == SCARD_PROTOCOL_T1);
	assert(SCardEndTransaction(handle, SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
	assert(SCardReconnect(handle, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
		SCARD_RESET_CARD, &protocol) == SCARD_S_SUCCESS);
	assert(SCardDisconnect(handle, SCARD_UNPOWER_CARD) == SCARD_S_SUCCESS);

	/* pcscd の状態監視は 400ms ごとに検出するため、注入した障害が監視で消費されるまで待ってから結果を見る */
	const std::string control = std::getenv("PX4_IFD_TEST_CONTROL");
	const auto inject = [&](const char *name, const char *content) {
		const std::string path = control + "/" + name, temporary = path + ".tmp";
		std::ofstream(temporary) << content;
		assert(std::rename(temporary.c_str(), path.c_str()) == 0);
	};
	const auto wait_consumed = [&]() {
		const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::filesystem::exists(control + "/detect-failures")) {
			assert(std::chrono::steady_clock::now() < limit);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1200));
	};
	const auto wait_state = [&](SCARD_READERSTATE &watch, DWORD flag) {
		const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!(watch.dwEventState & flag)) {
			assert(std::chrono::steady_clock::now() < limit);
			watch.dwCurrentState = watch.dwEventState & ~SCARD_STATE_CHANGED;
			const LONG ret = SCardGetStatusChange(context, 1000, &watch, 1);
			assert(ret == SCARD_S_SUCCESS || ret == SCARD_E_TIMEOUT);
		}
	};
	const auto transmit = [&](SCARDHANDLE card) {
		size = sizeof(reply);
		return SCardTransmit(card, SCARD_PCI_T1, apdu, sizeof(apdu), nullptr, reply, &size);
	};
	SCARD_READERSTATE watch{};
	watch.szReader = readers;
	watch.dwCurrentState = SCARD_STATE_UNAWARE;
	assert(SCardGetStatusChange(context, 0, &watch, 1) == SCARD_S_SUCCESS);

	/* 監視の検出が1回だけ失敗しても、pcscd はカードを再初期化せず、既存の接続のまま通信を続けられる */
	assert(SCardConnect(context, readers, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
		&handle, &protocol) == SCARD_S_SUCCESS);
	assert(transmit(handle) == SCARD_S_SUCCESS);
	inject("detect-failures", "1");
	wait_consumed();
	for (int i = 0; i < 3; i++)
		assert(transmit(handle) == SCARD_S_SUCCESS && size == 2);

	/*
	 * 監視の検出が続けて失敗したら、送信しない監視側にもリーダーの異常を通知する
	 * pcscd は既存の接続のプロトコルを未設定へ戻すため、同じ接続での自動復帰は保証せず、接続し直して通信を再開する
	 */
	inject("detect-failing", "");
	wait_state(watch, SCARD_STATE_UNAVAILABLE);
	assert(std::filesystem::remove(control + "/detect-failing"));
	wait_state(watch, SCARD_STATE_PRESENT);
	assert(SCardDisconnect(handle, SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
	assert(SCardConnect(context, readers, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
		&handle, &protocol) == SCARD_S_SUCCESS);
	assert(transmit(handle) == SCARD_S_SUCCESS);

	/* 検出の失敗と同時にカードが抜かれたら、検出し直した結果の抜去を通知し、以前の接続の通信状態を再挿入後のカードへ引き継がない */
	inject("card-absent", "");
	inject("detect-failures", "1");
	wait_state(watch, SCARD_STATE_EMPTY);
	assert(transmit(handle) == SCARD_W_REMOVED_CARD);
	assert(std::filesystem::remove(control + "/card-absent"));
	wait_state(watch, SCARD_STATE_PRESENT);
	assert(transmit(handle) == SCARD_W_REMOVED_CARD);
	assert(SCardDisconnect(handle, SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
	assert(SCardConnect(context, readers, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
		&handle, &protocol) == SCARD_S_SUCCESS);
	assert(transmit(handle) == SCARD_S_SUCCESS && size == 2);
	assert(SCardDisconnect(handle, SCARD_UNPOWER_CARD) == SCARD_S_SUCCESS);

	assert(SCardReleaseContext(context) == SCARD_S_SUCCESS);
	std::puts("pcsc_integration_test: passed (reader enumeration, T=1, APDU, sharing, reset, unpower, detection failures, removal)");
}
