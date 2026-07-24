#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>

#include <windows.h>

#include "IBonDriver2.h"

namespace {

/*
 * 選局を跨いで前チャンネルの TS が届くかを、チャンネル固有の PID で直接測る
 * 同期バイトや連続性カウンターの検査では、前チャンネルの正当な TS を検出できない
 */
std::set<std::uint16_t> CollectPids(IBonDriver2 *bon_driver, unsigned int milliseconds)
{
	std::set<std::uint16_t> pids;
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

			for (DWORD offset = 0; offset + 188 <= size; offset += 188) {
				const BYTE *packet = buffer + offset;
				if (packet[0] != 0x47 || (packet[1] & 0x80))
					continue;

				std::uint16_t pid = static_cast<std::uint16_t>(
					((packet[1] & 0x1f) << 8) | packet[2]);
				/* 全チャンネル共通の PID は判別に使えないため除く */
				if (pid <= 0x0030 || pid == 0x1fff)
					continue;
				pids.insert(pid);
			}
		}
	}

	return pids;
}

/* 選局直後の測定区間へ、指定の PID がいくつ混ざったかを数える */
struct LeakMetrics final {
	std::uint64_t packets = 0;
	std::uint64_t foreign_packets = 0;
	std::uint64_t first_foreign_offset = 0;
};

LeakMetrics MeasureLeak(IBonDriver2 *bon_driver, unsigned int milliseconds,
			const std::set<std::uint16_t> &foreign_pids)
{
	LeakMetrics metrics;
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

			for (DWORD offset = 0; offset + 188 <= size; offset += 188) {
				const BYTE *packet = buffer + offset;
				metrics.packets++;
				if (packet[0] != 0x47 || (packet[1] & 0x80))
					continue;

				std::uint16_t pid = static_cast<std::uint16_t>(
					((packet[1] & 0x1f) << 8) | packet[2]);
				if (!foreign_pids.count(pid))
					continue;

				if (!metrics.foreign_packets)
					metrics.first_foreign_offset = metrics.packets;
				metrics.foreign_packets++;
			}
		}
	}

	return metrics;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	if (argc != 6) {
		std::fprintf(stderr,
			"Usage: channel_purge_test.exe <BonDriver.dll> <space> <channel-a> <channel-b> <cycles>\n");
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

	/* 各チャンネルに固有の PID を先に集め、相手側にしか現れない PID だけを残す */
	std::set<std::uint16_t> pids[2];
	for (int index = 0; index < 2; index++) {
		if (!bon_driver->SetChannel(space, channels[index])) {
			std::fprintf(stderr, "Failed to tune. channel: %lu\n", channels[index]);
			bon_driver->CloseTuner();
			bon_driver->Release();
			FreeLibrary(module);
			return 6;
		}
		bon_driver->PurgeTsStream();
		pids[index] = CollectPids(bon_driver, 3000);
	}

	std::set<std::uint16_t> unique_pids[2];
	for (int index = 0; index < 2; index++) {
		for (std::uint16_t pid : pids[index]) {
			if (!pids[index ^ 1].count(pid))
				unique_pids[index].insert(pid);
		}
		std::printf("channel=%lu pids=%zu unique=%zu\n", channels[index],
			pids[index].size(), unique_pids[index].size());
	}

	if (unique_pids[0].empty() || unique_pids[1].empty()) {
		std::fprintf(stderr,
			"Both channels must have distinct PIDs to detect a leak.\n");
		bon_driver->CloseTuner();
		bon_driver->Release();
		FreeLibrary(module);
		return 7;
	}

	std::uint64_t total_foreign = 0;
	unsigned int failed_tunes = 0;
	unsigned int measured_cycles = 0;

	for (unsigned int cycle = 0; cycle < cycles; cycle++) {
		int index = cycle % 2;
		int previous = index ^ 1;

		/* 直前のチャンネルを一定時間受信してから切り替え、パイプへ滞留させる */
		if (!bon_driver->SetChannel(space, channels[previous])) {
			failed_tunes++;
			continue;
		}
		bon_driver->PurgeTsStream();
		CollectPids(bon_driver, 2000);

		if (!bon_driver->SetChannel(space, channels[index])) {
			failed_tunes++;
			continue;
		}
		bon_driver->PurgeTsStream();

		/* 切り替え直後に、前チャンネル固有の PID が届かないことを確かめる */
		LeakMetrics metrics = MeasureLeak(bon_driver, 2000, unique_pids[previous]);
		total_foreign += metrics.foreign_packets;
		measured_cycles++;
		std::printf(
			"cycle=%u channel=%lu packets=%llu foreign=%llu first_at=%llu\n",
			cycle, channels[index],
			static_cast<unsigned long long>(metrics.packets),
			static_cast<unsigned long long>(metrics.foreign_packets),
			static_cast<unsigned long long>(metrics.first_foreign_offset));
	}

	std::printf("total cycles=%u measured=%u failed_tunes=%u foreign_packets=%llu\n",
		cycles, measured_cycles, failed_tunes,
		static_cast<unsigned long long>(total_foreign));

	bon_driver->CloseTuner();
	bon_driver->Release();
	FreeLibrary(module);
	/* 選局に失敗して測れなかった周期があれば、混入0でも成功にしない */
	return (!total_foreign && !failed_tunes && measured_cycles == cycles) ? 0 : 1;
}
