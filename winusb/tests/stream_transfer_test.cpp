#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "receiver_base.hpp"
#include "io_queue.hpp"
#include "bon_ts_copy.hpp"
#include "pipe.hpp"

bool TestWriteDiagnostics()
{
	px4::ReceiverBase::StreamBuffer stream;
	if (!stream.Alloc(188 * 2)) return false;
	stream.Start();
	std::array<unsigned char, 188> packet{};
	packet[0] = 0x47; packet[1] = 0; packet[2] = 0x20; packet[3] = 0x10;
	for (int i = 0; i < 2; ++i) {
		std::size_t size = packet.size();
		if (!stream.Write(packet.data(), size) || size != packet.size()) return false;
	}
	std::size_t size = packet.size();
	if (stream.Write(packet.data(), size) || size != 0) return false;
	auto d = stream.GetWriteDiagnostics();
	if (d.rejected_bytes != 188 || d.rejected_writes != 1 || d.purge_write_discarded_bytes != 0) return false;
	if (!stream.Purge() || stream.GetWriteDiagnostics().purge_requests != 1) return false;
	stream.SetDiscardNullPacketsOnOutput(true);
	packet[1] = 0x1f; packet[2] = 0xff;
	size = packet.size();
	if (!stream.Write(packet.data(), size) || size != 0 || stream.GetWriteDiagnostics().rejected_bytes != 188) return false;
	stream.Stop();
	stream.SetDiscardNullPacketsOnOutput(false);
	packet[1] = 0; packet[2] = 0x20;
	size = packet.size();
	if (stream.Write(packet.data(), size) || stream.GetWriteDiagnostics().rejected_bytes != 376) return false;
	if (!stream.Alloc(188 * 2)) return false;
	stream.Start();
	d = stream.GetWriteDiagnostics();
	stream.Stop();
	return d.rejected_bytes == 0 && d.rejected_writes == 0 && d.purge_write_discarded_bytes == 0 && d.purge_requests == 0;
}

class PipeProvider final : public px4::IoQueue::IoProvider {
public:
	px4::Pipe &pipe;
	HANDLE cancel;
	std::atomic_size_t &consumed;
	PipeProvider(px4::Pipe &value, HANDLE event, std::atomic_size_t &count) : pipe(value), cancel(event), consumed(count) {}
	bool Start() override { return true; }
	void Stop() override {}
	bool Do(void *buf, std::size_t &size) override
	{
		const bool ret = pipe.Read(buf, size, size, cancel);
		if (ret) consumed.fetch_add(size);
		return ret;
	}
};

bool TestNativePipe(bool copy, std::size_t fragment, int sequence)
{
	// 本番の名前付きパイプを使わず、同じ Pipe/IoQueue 実装だけを接続する。
	const auto name = L"\\\\.\\pipe\\px4_stream_fixture_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(sequence);
	HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 188 * 4096, 256, 0, nullptr);
	if (server == INVALID_HANDLE_VALUE) return false;
	HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	if (client == INVALID_HANDLE_VALUE) { CloseHandle(server); return false; }
	OVERLAPPED connect{};
	if (!ConnectNamedPipe(server, &connect) && GetLastError() != ERROR_PIPE_CONNECTED) {
		CloseHandle(server); CloseHandle(client); return false;
	}
	DWORD mode = PIPE_READMODE_BYTE;
	if (!SetNamedPipeHandleState(client, &mode, nullptr, nullptr)) { CloseHandle(server); CloseHandle(client); return false; }
	HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!cancel) { CloseHandle(server); CloseHandle(client); return false; }
	std::vector<unsigned char> input(188 * (1024 * 40 + 1));
	for (std::size_t i = 0; i < input.size(); ++i) input[i] = static_cast<unsigned char>((i * 17 + i / 251) % 253);
	std::atomic_bool writer_ok(true);
	std::atomic_size_t consumed(0);
	std::thread writer([&] {
		px4::Pipe output(server);
		for (std::size_t offset = 0; offset < input.size();) {
			const auto amount = std::min(fragment, input.size() - offset);
			std::size_t written = 0;
			if (!output.Write(input.data() + offset, amount, written, cancel) || written != amount) { writer_ok = false; break; }
			offset += written;
		}
		// サーバーを閉じると未読データが破棄されるため、読み出し後にEOFを届ける。
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (writer_ok && consumed != input.size() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (consumed != input.size()) writer_ok = false;
	});
	bool valid = true;
	{
		px4::Pipe pipe(client);
		PipeProvider provider(pipe, cancel, consumed);
		px4::IoQueue queue(px4::IoQueue::IoOperation::READ, provider, 188 * 1024, 4, 2, 0);
		std::array<unsigned char, px4::BON_TS_COPY_SIZE + 32> guarded;
		std::vector<unsigned char> received;
		received.reserve(input.size());
		queue.Start();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (received.size() < input.size() && std::chrono::steady_clock::now() < deadline) {
			guarded.fill(0xA5);
			void *pointer = guarded.data() + 16;
			std::size_t size = 0, remain = 0;
			const bool available = copy ? px4::ReadBonTsCopy(queue, pointer, size, remain) : queue.ReadBuffer(&pointer, size, remain, false);
			if (!available) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
			if (copy && size > px4::BON_TS_COPY_SIZE) { valid = false; break; }
			for (std::size_t i = 0; i < 16; ++i)
				if (guarded[i] != 0xA5 || guarded[16 + px4::BON_TS_COPY_SIZE + i] != 0xA5) valid = false;
			if (!valid || size > input.size() - received.size()) { valid = false; break; }
			const auto *bytes = static_cast<const unsigned char *>(pointer);
			received.insert(received.end(), bytes, bytes + size);
		}
		SetEvent(cancel);
		queue.Stop();
		valid = valid && received == input;
	}
	writer.join();
	CloseHandle(cancel);
	return valid && writer_ok;
}

int main()
{
	if (!TestWriteDiagnostics()) return 1;
	int sequence = 0;
	for (const auto fragment : {std::size_t(4093), std::size_t(188 * 816)}) {
		if (!TestNativePipe(true, fragment, ++sequence)) return 2;
		if (!TestNativePipe(false, fragment, ++sequence)) return 3;
	}
	std::puts("stream_transfer_test: passed (full/inactive rejection, filter exclusion, purge/reset, real named pipe, copy/pointer order)");
	return 0;
}
