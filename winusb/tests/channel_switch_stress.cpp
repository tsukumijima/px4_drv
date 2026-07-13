#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <windows.h>

#include "IBonDriver2.h"

namespace {

struct StreamMetrics final {
	std::uint64_t packets = 0;
	std::uint64_t sync_errors = 0;
	std::uint64_t transport_errors = 0;
	std::uint64_t continuity_errors = 0;
};

/* 選局ごとに連続性カウンターを初期化して TS を検査する */
StreamMetrics MeasureStream(IBonDriver2 *bon_driver, unsigned int milliseconds)
{
	StreamMetrics metrics;
	std::array<int, 8192> last_counter;
	last_counter.fill(-1);
	auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(milliseconds);

	while (std::chrono::steady_clock::now() < deadline) {
		bon_driver->WaitTsStream(200);
		while (bon_driver->GetReadyCount()) {
			BYTE *buffer = nullptr;
			DWORD size = 0;
			DWORD remain = 0;
			if (!bon_driver->GetTsStream(&buffer, &size, &remain) || !buffer)
				break;

			/* 破損バッファでも範囲外を読まず188バイト単位で検査する */
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
				/* 188バイトを越える適応フィールドは破損パケットとして除外する */
				if ((adaptation & 0x02) && packet[4] >= 184) {
					metrics.sync_errors++;
					continue;
				}

				bool discontinuity = false;
				if ((adaptation & 0x02) && packet[4] >= 1)
					discontinuity = (packet[5] & 0x80) != 0;
				if (discontinuity)
					last_counter[pid] = -1;

				/* ペイロードを持つ通常パケットだけ連続性を追跡する */
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

void AddMetrics(StreamMetrics &total, const StreamMetrics &current)
{
	total.packets += current.packets;
	total.sync_errors += current.sync_errors;
	total.transport_errors += current.transport_errors;
	total.continuity_errors += current.continuity_errors;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	if (argc != 7) {
		std::fprintf(stderr,
			"Usage: channel_switch_stress.exe <BonDriver.dll> <space> <channel-a> <channel-b> <cycles> <measure-ms>\n");
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
	DWORD channels[] = {
		wcstoul(argv[3], nullptr, 10),
		wcstoul(argv[4], nullptr, 10),
	};
	unsigned int cycles = wcstoul(argv[5], nullptr, 10);
	unsigned int measure_ms = wcstoul(argv[6], nullptr, 10);
	StreamMetrics total;
	unsigned int failed_tunes = 0;
	unsigned int empty_intervals = 0;

	for (unsigned int cycle = 0; cycle < cycles; cycle++) {
		DWORD channel = channels[cycle % 2];
		if (!bon_driver->SetChannel(space, channel)) {
			std::fprintf(stderr, "Failed to tune. cycle: %u, channel: %lu\n",
				cycle, channel);
			failed_tunes++;
			continue;
		}

		/* 受信済みの旧チャンネルを捨て、切り替え直後から検査する */
		bon_driver->PurgeTsStream();
		StreamMetrics current = MeasureStream(bon_driver, measure_ms);
		if (!current.packets)
			empty_intervals++;
		AddMetrics(total, current);
		std::printf(
			"cycle=%u channel=%lu packets=%llu sync_errors=%llu transport_errors=%llu continuity_errors=%llu signal=%.2f\n",
			cycle, channel,
			static_cast<unsigned long long>(current.packets),
			static_cast<unsigned long long>(current.sync_errors),
			static_cast<unsigned long long>(current.transport_errors),
			static_cast<unsigned long long>(current.continuity_errors),
			bon_driver->GetSignalLevel());
	}

	std::printf(
		"total cycles=%u failed_tunes=%u empty_intervals=%u packets=%llu sync_errors=%llu transport_errors=%llu continuity_errors=%llu\n",
		cycles, failed_tunes, empty_intervals,
		static_cast<unsigned long long>(total.packets),
		static_cast<unsigned long long>(total.sync_errors),
		static_cast<unsigned long long>(total.transport_errors),
		static_cast<unsigned long long>(total.continuity_errors));

	bon_driver->CloseTuner();
	bon_driver->Release();
	FreeLibrary(module);
	return failed_tunes == 0 && empty_intervals == 0 && total.sync_errors == 0 &&
		total.transport_errors == 0 && total.continuity_errors == 0 ? 0 : 1;
}
