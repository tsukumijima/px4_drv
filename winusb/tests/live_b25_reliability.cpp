#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <windows.h>

#include "IBonDriver2.h"

constexpr WORD TS_INVALID_PID = 0xffffU;

/* libaribb25.dll をリンクせず読み込むため、公開仮想関数の ABI だけを宣言する */
class IB25Decoder {
public:
	virtual const BOOL Initialize(DWORD round = 4) = 0;
	virtual void Release() = 0;
	virtual const BOOL Decode(BYTE *source, const DWORD source_size,
		BYTE **destination, DWORD *destination_size) = 0;
	virtual const BOOL Flush(BYTE **destination, DWORD *destination_size) = 0;
	virtual const BOOL Reset() = 0;
};

class IB25Decoder2 : public IB25Decoder {
public:
	virtual void DiscardNullPacket(bool enable = true) = 0;
	virtual void DiscardScramblePacket(bool enable = true) = 0;
	virtual void EnableEmmProcess(bool enable = true) = 0;
	virtual void SetMulti2Round(std::int32_t round = 4) = 0;
	virtual void SetSimdMode(std::int32_t instruction = 3) = 0;
	virtual const DWORD GetDescramblingState(WORD program_id) = 0;
	virtual void ResetStatistics() = 0;
	virtual const DWORD GetPacketStride() = 0;
	virtual const DWORD GetInputPacketNum(WORD pid = TS_INVALID_PID) = 0;
	virtual const DWORD GetOutputPacketNum(WORD pid = TS_INVALID_PID) = 0;
	virtual const DWORD GetSyncErrNum() = 0;
	virtual const DWORD GetFormatErrNum() = 0;
	virtual const DWORD GetTransportErrNum() = 0;
	virtual const DWORD GetContinuityErrNum(WORD pid = TS_INVALID_PID) = 0;
	virtual const DWORD GetScramblePacketNum(WORD pid = TS_INVALID_PID) = 0;
	virtual const DWORD GetEcmProcessNum() = 0;
	virtual const DWORD GetEmmProcessNum() = 0;
};

namespace {

struct ScrambleMetrics final {
	std::uint64_t packets = 0;
	std::uint64_t scrambled = 0;
	std::uint64_t sync_errors = 0;
};

/* 復号器の統計だけに依存せず、実際に出入りした TS のスクランブルビットを数える */
void AddTsMetrics(const BYTE *buffer, DWORD size, ScrambleMetrics &metrics)
{
	for (DWORD offset = 0; offset + 188 <= size; offset += 188) {
		const BYTE *packet = buffer + offset;
		metrics.packets++;
		if (packet[0] != 0x47) {
			metrics.sync_errors++;
			continue;
		}
		if ((packet[3] & 0xc0) != 0)
			metrics.scrambled++;
	}
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	if (argc != 6) {
		std::fprintf(stderr,
			"Usage: live_b25_reliability.exe <BonDriver.dll> <libaribb25.dll> <space> <channel> <seconds>\n");
		return 2;
	}

	/* TVTest と同じく BonDriver と libaribb25 を動的に読み込む */
	HMODULE bon_module = LoadLibraryW(argv[1]);
	HMODULE b25_module = LoadLibraryW(argv[2]);
	if (!bon_module || !b25_module) {
		std::fprintf(stderr, "Failed to load modules. error: %lu\n", GetLastError());
		return 3;
	}
	auto create_bon_driver = reinterpret_cast<IBonDriver *(*)()>(
		GetProcAddress(bon_module, "CreateBonDriver"));
	auto create_b25_decoder = reinterpret_cast<IB25Decoder2 *(*)()>(
		GetProcAddress(b25_module, "CreateB25Decoder2"));
	if (!create_bon_driver || !create_b25_decoder) {
		std::fprintf(stderr, "Required factory function was not found.\n");
		return 4;
	}

	/* 受信機とカードを先に開き、選局直後の古い TS は捨てる */
	auto bon_driver = static_cast<IBonDriver2 *>(create_bon_driver());
	auto decoder = create_b25_decoder();
	if (!bon_driver || !decoder || !bon_driver->OpenTuner() || !decoder->Initialize()) {
		std::fprintf(stderr, "Failed to initialize tuner or B25 decoder.\n");
		return 5;
	}
	DWORD space = wcstoul(argv[3], nullptr, 10);
	DWORD channel = wcstoul(argv[4], nullptr, 10);
	if (!bon_driver->SetChannel(space, channel)) {
		std::fprintf(stderr, "Failed to tune. space: %lu, channel: %lu\n",
			space, channel);
		return 6;
	}
	decoder->DiscardNullPacket(false);
	decoder->DiscardScramblePacket(false);
	decoder->EnableEmmProcess(false);
	std::this_thread::sleep_for(std::chrono::seconds(3));
	bon_driver->PurgeTsStream();

	ScrambleMetrics input_metrics;
	ScrambleMetrics output_metrics;
	std::uint64_t decode_failures = 0;
	std::uint64_t slow_decodes = 0;
	long long maximum_decode_ms = 0;
	auto started = std::chrono::steady_clock::now();
	auto deadline = started + std::chrono::seconds(wcstoul(argv[5], nullptr, 10));
	auto next_report = started + std::chrono::seconds(1);
	while (std::chrono::steady_clock::now() < deadline) {
		bon_driver->WaitTsStream(1000);
		while (bon_driver->GetReadyCount()) {
			BYTE *input = nullptr;
			DWORD input_size = 0;
			DWORD remain = 0;
			if (!bon_driver->GetTsStream(&input, &input_size, &remain) || !input)
				break;

			/* BonDriver のバッファを到着順のまま復号器へ渡す */
			AddTsMetrics(input, input_size, input_metrics);
			BYTE *output = nullptr;
			DWORD output_size = 0;
			auto decode_started = std::chrono::steady_clock::now();
			bool decode_succeeded = decoder->Decode(input, input_size, &output, &output_size) != FALSE;
			if (!decode_succeeded)
				decode_failures++;
			long long decode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - decode_started).count();
			if (decode_ms > maximum_decode_ms)
				maximum_decode_ms = decode_ms;
			if (decode_ms >= 200) {
				slow_decodes++;
				std::printf(
					"slow_decode elapsed_ms=%lld duration_ms=%lld decode_succeeded=%u input_size=%lu\n",
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - started).count(),
					decode_ms, decode_succeeded ? 1U : 0U, input_size);
				std::fflush(stdout);
			}
			if (output && output_size)
				AddTsMetrics(output, output_size, output_metrics);
		}

		/* 断続的な失敗時刻を後から TS 統計と対応できるよう1秒ごとに出力する */
		auto now = std::chrono::steady_clock::now();
		if (now >= next_report) {
			auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				now - started).count();
			std::printf(
				"elapsed_ms=%lld decode_failures=%llu input_scrambled=%llu output_scrambled=%llu ecm=%lu card_state=%lu\n",
				elapsed_ms,
				static_cast<unsigned long long>(decode_failures),
				static_cast<unsigned long long>(input_metrics.scrambled),
				static_cast<unsigned long long>(output_metrics.scrambled),
				decoder->GetEcmProcessNum(),
				decoder->GetDescramblingState(TS_INVALID_PID));
			std::fflush(stdout);
			next_report += std::chrono::seconds(1);
		}
	}

	/* 終端に残った TS も統計へ含める */
	BYTE *output = nullptr;
	DWORD output_size = 0;
	if (!decoder->Flush(&output, &output_size))
		decode_failures++;
	if (output && output_size)
		AddTsMetrics(output, output_size, output_metrics);
	std::printf(
		"final decode_failures=%llu slow_decodes=%llu maximum_decode_ms=%lld input_packets=%llu input_scrambled=%llu output_packets=%llu output_scrambled=%llu input_sync_errors=%llu output_sync_errors=%llu signal=%.2f\n",
		static_cast<unsigned long long>(decode_failures),
		static_cast<unsigned long long>(slow_decodes), maximum_decode_ms,
		static_cast<unsigned long long>(input_metrics.packets),
		static_cast<unsigned long long>(input_metrics.scrambled),
		static_cast<unsigned long long>(output_metrics.packets),
		static_cast<unsigned long long>(output_metrics.scrambled),
		static_cast<unsigned long long>(input_metrics.sync_errors),
		static_cast<unsigned long long>(output_metrics.sync_errors),
		bon_driver->GetSignalLevel());

	decoder->Release();
	bon_driver->CloseTuner();
	bon_driver->Release();
	FreeLibrary(b25_module);
	FreeLibrary(bon_module);
	return decode_failures == 0 && output_metrics.scrambled == 0 &&
		input_metrics.sync_errors == 0 && output_metrics.sync_errors == 0 ? 0 : 1;
}
