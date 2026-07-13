#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <windows.h>
#include <winscard.h>

#include "IBonDriver2.h"

namespace {

struct StreamMetrics final {
	std::uint64_t packets = 0;
	std::uint64_t sync_errors = 0;
	std::uint64_t transport_errors = 0;
	std::uint64_t continuity_errors = 0;
};

/* TS の連続性を区間ごとに独立して集計する */
StreamMetrics MeasureStream(IBonDriver2 *bon_driver, unsigned int seconds,
	FILE *capture_file = nullptr)
{
	StreamMetrics metrics;
	std::array<int, 8192> last_counter;
	last_counter.fill(-1);
	auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(seconds);

	while (std::chrono::steady_clock::now() < deadline) {
		bon_driver->WaitTsStream(1000);
		while (bon_driver->GetReadyCount()) {
			BYTE *buffer = nullptr;
			DWORD size = 0;
			DWORD remain = 0;
			if (!bon_driver->GetTsStream(&buffer, &size, &remain) || !buffer)
				break;
			if (capture_file && fwrite(buffer, 1, size, capture_file) != size) {
				std::fprintf(stderr, "Failed to write captured TS.\n");
				return metrics;
			}

			/* BonDriver のバッファは188バイト境界だが、破損時も範囲外を読まない */
			for (DWORD offset = 0; offset + 188 <= size; offset += 188) {
				const BYTE *packet = buffer + offset;
				metrics.packets++;
				if (packet[0] != 0x47) {
					metrics.sync_errors++;
					continue;
				}
				if (packet[1] & 0x80)
					metrics.transport_errors++;

				std::uint16_t pid = static_cast<std::uint16_t>(
					((packet[1] & 0x1f) << 8) | packet[2]);
				std::uint8_t adaptation = (packet[3] >> 4) & 0x03;
				std::uint8_t counter = packet[3] & 0x0f;
				if (!adaptation) {
					metrics.sync_errors++;
					continue;
				}

				bool discontinuity = false;
				if ((adaptation & 0x02) && packet[4] >= 1)
					discontinuity = (packet[5] & 0x80) != 0;
				if (discontinuity)
					last_counter[pid] = -1;

				/* ペイロードを持つパケットだけ連続性カウンターが進む */
				if ((adaptation & 0x01) && pid != 0x1fff) {
					if (last_counter[pid] >= 0 &&
						counter != ((last_counter[pid] + 1) & 0x0f))
						metrics.continuity_errors++;
					last_counter[pid] = counter;
				}
			}
		}
	}

	return metrics;
}

void PrintMetrics(const char *name, const StreamMetrics &metrics)
{
	std::printf("%s packets=%llu sync_errors=%llu transport_errors=%llu continuity_errors=%llu\n",
		name,
		static_cast<unsigned long long>(metrics.packets),
		static_cast<unsigned long long>(metrics.sync_errors),
		static_cast<unsigned long long>(metrics.transport_errors),
		static_cast<unsigned long long>(metrics.continuity_errors));
}

/* 指定した内蔵リーダーへカード情報取得 APDU を連続送信する */
void StressCard(std::atomic<bool> &stop, std::atomic<unsigned int> &successes,
	std::atomic<unsigned int> &failures, const wchar_t *reader_pattern)
{
	SCARDCONTEXT context = 0;
	if (SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &context) !=
		SCARD_S_SUCCESS) {
		std::fprintf(stderr, "SCardEstablishContext failed.\n");
		failures++;
		return;
	}

	DWORD readers_length = 0;
	LONG result = SCardListReadersW(context, nullptr, nullptr, &readers_length);
	std::wstring reader;
	if (result == SCARD_S_SUCCESS) {
		std::wstring readers(readers_length, L'\0');
		result = SCardListReadersW(context, nullptr, readers.data(), &readers_length);
		for (const wchar_t *name = readers.c_str(); result == SCARD_S_SUCCESS && *name;
			name += wcslen(name) + 1) {
			if (wcsstr(name, reader_pattern)) {
				reader = name;
				break;
			}
		}
	}
	if (reader.empty()) {
		std::fprintf(stderr, "Matching card reader was not found. result: 0x%08lx\n",
			static_cast<unsigned long>(result));
		failures++;
		SCardReleaseContext(context);
		return;
	}

	SCARDHANDLE card = 0;
	DWORD protocol = 0;
	result = SCardConnectW(context, reader.c_str(), SCARD_SHARE_SHARED,
		SCARD_PROTOCOL_T1, &card, &protocol);
	if (result != SCARD_S_SUCCESS) {
		std::fprintf(stderr, "SCardConnectW failed. result: 0x%08lx\n",
			static_cast<unsigned long>(result));
		failures++;
		SCardReleaseContext(context);
		return;
	}

	const BYTE apdu[] = { 0x90, 0x30, 0x00, 0x00, 0x00 };
	while (!stop.load()) {
		BYTE response[128];
		DWORD response_length = sizeof(response);
		result = SCardTransmit(card, SCARD_PCI_T1, apdu, sizeof(apdu), nullptr,
			response, &response_length);
		if (result == SCARD_S_SUCCESS && response_length >= 2 &&
			response[response_length - 2] == 0x90 &&
			response[response_length - 1] == 0x00)
			successes++;
		else {
			std::fprintf(stderr,
				"SCardTransmit failed. result: 0x%08lx, response_length: %lu\n",
				static_cast<unsigned long>(result), response_length);
			failures++;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	SCardDisconnect(card, SCARD_LEAVE_CARD);
	SCardReleaseContext(context);
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	if (argc < 6 || argc > 8) {
		std::fprintf(stderr,
			"Usage: card_stream_stress.exe <BonDriver.dll> <space> <channel> <baseline-sec> <stress-sec> [capture.ts|-] [reader-substring]\n");
		return 2;
	}

	HMODULE module = LoadLibraryW(argv[1]);
	if (!module) {
		std::fprintf(stderr, "Failed to load BonDriver. error: %lu\n", GetLastError());
		return 3;
	}
	auto create_bon_driver = reinterpret_cast<IBonDriver *(*)()>(
		GetProcAddress(module, "CreateBonDriver"));
	if (!create_bon_driver) {
		std::fprintf(stderr, "CreateBonDriver was not found.\n");
		FreeLibrary(module);
		return 4;
	}

	auto bon_driver = static_cast<IBonDriver2 *>(create_bon_driver());
	if (!bon_driver || !bon_driver->OpenTuner()) {
		std::fprintf(stderr, "Failed to open tuner.\n");
		if (bon_driver)
			bon_driver->Release();
		FreeLibrary(module);
		return 5;
	}
	DWORD space = wcstoul(argv[2], nullptr, 10);
	DWORD channel = wcstoul(argv[3], nullptr, 10);
	if (!bon_driver->SetChannel(space, channel)) {
		std::fprintf(stderr, "Failed to tune. space: %lu, channel: %lu\n", space, channel);
		bon_driver->CloseTuner();
		bon_driver->Release();
		FreeLibrary(module);
		return 6;
	}

	/* 選局直後のバッファを捨て、比較区間へ前の TS を持ち込まない */
	std::this_thread::sleep_for(std::chrono::seconds(3));
	bon_driver->PurgeTsStream();
	FILE *capture_file = nullptr;
	if (argc >= 7 && wcscmp(argv[6], L"-") != 0 &&
		_wfopen_s(&capture_file, argv[6], L"wb") != 0) {
		std::fprintf(stderr, "Failed to open captured TS output.\n");
		bon_driver->CloseTuner();
		bon_driver->Release();
		FreeLibrary(module);
		return 7;
	}
	StreamMetrics baseline = MeasureStream(bon_driver,
		wcstoul(argv[4], nullptr, 10), capture_file);

	std::atomic<bool> stop{ false };
	std::atomic<unsigned int> card_successes{ 0 };
	std::atomic<unsigned int> card_failures{ 0 };
	const wchar_t *reader_pattern = argc == 8 ? argv[7] : L"PX-MLT5PE";
	/* 独立した2つの PC/SC 接続から同じ内蔵カードへ負荷を掛ける */
	std::thread card_thread1(StressCard, std::ref(stop), std::ref(card_successes),
		std::ref(card_failures), reader_pattern);
	std::thread card_thread2(StressCard, std::ref(stop), std::ref(card_successes),
		std::ref(card_failures), reader_pattern);
	StreamMetrics stress = MeasureStream(bon_driver,
		wcstoul(argv[5], nullptr, 10), capture_file);
	stop = true;
	card_thread1.join();
	card_thread2.join();
	if (capture_file)
		fclose(capture_file);

	PrintMetrics("baseline", baseline);
	PrintMetrics("card_stress", stress);
	std::printf("card_apdu successes=%u failures=%u signal=%.2f\n",
		card_successes.load(), card_failures.load(), bon_driver->GetSignalLevel());

	bon_driver->CloseTuner();
	bon_driver->Release();
	FreeLibrary(module);
	return card_failures.load() == 0 && stress.sync_errors == 0 &&
		stress.transport_errors == 0 && stress.continuity_errors == 0 ? 0 : 1;
}
