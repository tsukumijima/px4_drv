#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "receiver_base.hpp"
#include "io_queue.hpp"
#include "bon_ts_copy.hpp"
#include "pipe.hpp"

std::array<unsigned char, 188> MakeTsmfHeader(unsigned int relative, bool enabled)
{
	std::array<unsigned char, 188> packet{};
	packet[0] = 0x47;
	packet[2] = 0x2f;
	packet[3] = 0x10;
	packet[4] = 0x1a;
	packet[5] = 0x86;
	packet[6] = 1;
	if (enabled) packet[7 + relative / 8] = static_cast<unsigned char>(0x80 >> (relative % 8));
	packet[73] = static_cast<unsigned char>((relative + 1) << 4);
	std::uint32_t crc = 0xffffffff;
	for (std::size_t i = 4; i < 184; ++i) {
		crc ^= static_cast<std::uint32_t>(packet[i]) << 24;
		for (unsigned int bit = 0; bit < 8; ++bit)
			crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04c11db7 : crc << 1;
	}
	for (unsigned int i = 0; i < 4; ++i)
		packet[184 + i] = static_cast<unsigned char>(crc >> (24 - i * 8));
	return packet;
}

class TuneReceiver final : public px4::ReceiverBase {
public:
	enum class Input { None, Raw, Header, Disabled, BadCRC, DelayedHeader, SecondQam, StaleHeader };
	Input input;
	unsigned int attempts = 0;
	unsigned int lock_delay = 0;
	unsigned int frequency_delay = 0;
	int lock_error = 0;
	std::chrono::steady_clock::time_point frequency_begin;

	TuneReceiver(px4::SystemType system, std::uint32_t modulation, int relative, Input value, unsigned int options = 0)
		: ReceiverBase(options), input(value)
	{
		params_.system = system;
		params_.modulation = modulation;
		params_.tsmf_mode = relative >= 0;
		params_.tsmf_relative_stream = relative >= 0 ? relative : 0;
		stream_buf_->SetTsmfRelativeStream(relative >= 0, static_cast<std::uint8_t>(params_.tsmf_relative_stream), 0);
		if (!stream_buf_->Alloc(188 * 1024)) std::abort();
		stream_buf_->Start();
	}
	~TuneReceiver() { stream_buf_->Stop(); }
	int Open() override { return 0; }
	void Close() override {}
	int SetLnbVoltage(std::int32_t) override { return 0; }
	int SetCapture(bool) override { return 0; }
	int ReadStat(px4::command::StatType, std::int32_t &) override { return 0; }
	int SetStreamId() override { return 0; }
	int SetFrequency() override
	{
		++attempts;
		frequency_begin = std::chrono::steady_clock::now();
		Sleep(frequency_delay);
		return 0;
	}
	int CheckLock(bool &locked) override
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frequency_begin).count();
		locked = elapsed >= lock_delay && !(input == Input::StaleHeader && params_.modulation == 64);
		if (lock_error) return lock_error;
		if (input == Input::None || (input == Input::SecondQam && params_.modulation != 256)) return 0;
		std::array<unsigned char, 188> packet{};
		packet[0] = 0x47;
		packet[2] = 0x20;
		packet[3] = 0x10;
		if (input == Input::Header || input == Input::Disabled || input == Input::BadCRC ||
			(input == Input::DelayedHeader && elapsed >= 400) ||
			(input == Input::StaleHeader && params_.modulation == 64)) {
			packet = MakeTsmfHeader(params_.tsmf_relative_stream, input != Input::Disabled);
			if (input == Input::BadCRC) packet[187] ^= 1;
		}
		std::size_t size = packet.size();
		stream_buf_->Write(packet.data(), size);
		return 0;
	}
};

bool TestTuneInput()
{
	using Input = TuneReceiver::Input;
	using System = px4::SystemType;
	struct TestCase {
		const char *name;
		System system;
		std::uint32_t modulation;
		int relative;
		Input input;
		bool expected;
		unsigned int attempts;
	};
	const TestCase cases[] = {
		{"terrestrial without input", System::ISDB_T, 0, -1, Input::None, true, 1},
		{"satellite without input", System::ISDB_S, 0, -1, Input::None, true, 1},
		{"fixed 64 without input", System::ISDB_C, 64, -1, Input::None, false, 1},
		{"fixed 256 without input", System::ISDB_C, 256, -1, Input::None, false, 1},
		{"fixed 64 with input", System::ISDB_C, 64, -1, Input::Raw, true, 1},
		{"fixed 256 with input", System::ISDB_C, 256, -1, Input::Raw, true, 1},
		{"missing TSMF header", System::ISDB_C, 256, 0, Input::Raw, false, 1},
		{"disabled relative stream", System::ISDB_C, 256, 1, Input::Disabled, false, 1},
		{"TSMF relative 0", System::ISDB_C, 64, 0, Input::Header, true, 1},
		{"TSMF relative 14", System::ISDB_C, 256, 14, Input::Header, true, 1},
		{"bad TSMF CRC", System::ISDB_C, 256, 0, Input::BadCRC, false, 1},
		{"delayed TSMF header", System::ISDB_C, 256, 0, Input::DelayedHeader, true, 1},
		{"automatic 256 fallback", System::ISDB_C, 0, -1, Input::SecondQam, true, 2},
		{"stale header across fallback", System::ISDB_C, 0, 0, Input::StaleHeader, false, 2},
	};
	for (const auto &test : cases) {
		TuneReceiver receiver(test.system, test.modulation, test.relative, test.input);
		if (receiver.Tune(1000) != test.expected || receiver.attempts != test.attempts) {
			std::fprintf(stderr, "Tune case failed: %s\n", test.name);
			return false;
		}
	}
	TuneReceiver timeout(System::ISDB_C, 0, -1, Input::None);
	const auto begin = std::chrono::steady_clock::now();
	if (timeout.Tune(200) || std::chrono::steady_clock::now() - begin >= std::chrono::milliseconds(350)) return false;
	TuneReceiver late_lock(System::ISDB_C, 64, -1, Input::Raw);
	late_lock.lock_delay = 350;
	if (late_lock.Tune(400)) return false;
	TuneReceiver slow_frequency(System::ISDB_C, 64, -1, Input::Raw);
	slow_frequency.frequency_delay = 120;
	if (slow_frequency.Tune(100)) return false;
	TuneReceiver settle(System::ISDB_C, 64, -1, Input::Raw, RECEIVER_WAIT_AFTER_LOCK_TC_T);
	if (settle.Tune(400)) return false;
	TuneReceiver terrestrial_settle(System::ISDB_T, 0, -1, Input::None, RECEIVER_WAIT_AFTER_LOCK_TC_T);
	if (!terrestrial_settle.Tune(1)) return false;
	TuneReceiver satellite_settle(System::ISDB_S, 0, -1, Input::None, RECEIVER_WAIT_AFTER_LOCK);
	if (!satellite_settle.Tune(1)) return false;
	TuneReceiver cancelled(System::ISDB_C, 0, -1, Input::Raw);
	cancelled.lock_error = -ECANCELED;
	if (cancelled.Tune(1000) || cancelled.attempts != 1) return false;
	std::puts("stream_transfer_test: passed (fixed/automatic QAM input, TSMF validity, retry state, absolute tune deadline, T/S and cancellation)");
	return true;
}

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
	if (!TestTuneInput()) return 4;
	if (!TestWriteDiagnostics()) return 1;
	int sequence = 0;
	for (const auto fragment : {std::size_t(4093), std::size_t(188 * 816)}) {
		if (!TestNativePipe(true, fragment, ++sequence)) return 2;
		if (!TestNativePipe(false, fragment, ++sequence)) return 3;
	}
	std::puts("stream_transfer_test: passed (full/inactive rejection, filter exclusion, purge/reset, real named pipe, copy/pointer order)");
	return 0;
}
