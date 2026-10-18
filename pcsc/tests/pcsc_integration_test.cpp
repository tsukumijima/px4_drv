// SPDX-License-Identifier: GPL-2.0-only
#include <winscard.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <chrono>
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
	assert(SCardReleaseContext(context) == SCARD_S_SUCCESS);
	std::puts("pcsc_integration_test: passed (reader enumeration, T=1, APDU, sharing, reset, unpower)");
}
