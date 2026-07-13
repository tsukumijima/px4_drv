#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <winscard.h>

namespace {

struct ReaderStats final {
	std::wstring name;
	SCARDHANDLE card = 0;
	std::uint64_t successes = 0;
	std::uint64_t failures = 0;
	std::uint64_t slow_responses = 0;
	long long maximum_ms = 0;
	LONG last_error = SCARD_S_SUCCESS;
};

std::vector<std::wstring> SplitReaders(const wchar_t *readers)
{
	std::vector<std::wstring> result;
	for (const wchar_t *reader = readers; reader && *reader;
		reader += std::wcslen(reader) + 1)
		result.emplace_back(reader);
	return result;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	/* 通常利用と高負荷の両方を同じ実行ファイルで再現できるよう送信間隔を指定可能にする */
	if (argc < 2 || argc > 4) {
		std::fprintf(stderr,
			"Usage: card_reader_reliability.exe <seconds> [interval-ms] [reader-filter]\n");
		return 2;
	}
	unsigned long seconds = std::wcstoul(argv[1], nullptr, 10);
	unsigned long interval_ms = argc >= 3 ? std::wcstoul(argv[2], nullptr, 10) : 200;
	const wchar_t *reader_filter = argc == 4 ? argv[3] : nullptr;
	if (!seconds || !interval_ms)
		return 2;

	SCARDCONTEXT context = 0;
	LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &context);
	if (result != SCARD_S_SUCCESS)
		return 3;

	wchar_t *reader_buffer = nullptr;
	DWORD reader_length = SCARD_AUTOALLOCATE;
	result = SCardListReadersW(context, nullptr,
		reinterpret_cast<wchar_t *>(&reader_buffer), &reader_length);
	if (result != SCARD_S_SUCCESS) {
		SCardReleaseContext(context);
		return 4;
	}

	/* 列挙時点で接続できた全リーダーを同じ条件で測定する */
	std::vector<ReaderStats> readers;
	for (const auto &name : SplitReaders(reader_buffer)) {
		/* 機種単独の測定では部分一致したリーダーだけを接続する */
		if (reader_filter && name.find(reader_filter) == std::wstring::npos)
			continue;

		ReaderStats stats;
		stats.name = name;
		DWORD protocol = 0;
		result = SCardConnectW(context, name.c_str(), SCARD_SHARE_SHARED,
			SCARD_PROTOCOL_T1, &stats.card, &protocol);
		if (result == SCARD_S_SUCCESS)
			readers.emplace_back(std::move(stats));
		else
			std::fwprintf(stderr, L"Connect failed. reader: %ls, error: 0x%08lx\n",
				name.c_str(), static_cast<unsigned long>(result));
	}
	SCardFreeMemory(context, reader_buffer);

	/* B-CAS の初期応答を繰り返し、成功数と応答時間をリーダー単位で記録する */
	const BYTE command[] = { 0x90, 0x30, 0x00, 0x00, 0x00 };
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
	while (std::chrono::steady_clock::now() < deadline) {
		for (auto &reader : readers) {
			BYTE response[128] = {};
			DWORD response_length = sizeof(response);
			auto started = std::chrono::steady_clock::now();
			result = SCardTransmit(reader.card, SCARD_PCI_T1, command,
				sizeof(command), nullptr, response, &response_length);
			long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			if (elapsed_ms > reader.maximum_ms)
				reader.maximum_ms = elapsed_ms;
			if (elapsed_ms >= 200)
				reader.slow_responses++;

			bool valid_response = result == SCARD_S_SUCCESS && response_length >= 2 &&
				response[response_length - 2] == 0x90 &&
				response[response_length - 1] == 0x00;
			if (valid_response)
				reader.successes++;
			else {
				reader.failures++;
				reader.last_error = result;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
	}

	/* 1回でも通信に失敗した場合は自動試験から検出できる終了コードを返す */
	bool succeeded = !readers.empty();
	for (auto &reader : readers) {
		std::wprintf(L"reader=%ls success=%llu failure=%llu slow=%llu max_ms=%lld last_error=0x%08lx\n",
			reader.name.c_str(), reader.successes, reader.failures,
			reader.slow_responses, reader.maximum_ms,
			static_cast<unsigned long>(reader.last_error));
		succeeded = reader.failures == 0 && succeeded;
		SCardDisconnect(reader.card, SCARD_LEAVE_CARD);
	}
	SCardReleaseContext(context);
	return succeeded ? 0 : 1;
}
