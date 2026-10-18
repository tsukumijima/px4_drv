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

std::array<unsigned char, 188> MakeTsmfHeader(unsigned int relative, bool enabled, unsigned int slots = 1, unsigned int tsid = 0, unsigned int onid = 0)
{
	std::array<unsigned char, 188> packet{};
	packet[0] = 0x47;
	packet[2] = 0x2f;
	packet[3] = 0x10;
	packet[4] = 0x1a;
	packet[5] = 0x86;
	packet[6] = 1;
	if (enabled) packet[7 + relative / 8] = static_cast<unsigned char>(0x80 >> (relative % 8));
	packet[9 + relative * 4] = static_cast<unsigned char>(tsid >> 8);
	packet[10 + relative * 4] = static_cast<unsigned char>(tsid);
	packet[11 + relative * 4] = static_cast<unsigned char>(onid >> 8);
	packet[12 + relative * 4] = static_cast<unsigned char>(onid);
	for (unsigned int i = 0; i < slots; ++i)
		packet[73 + i / 2] |= static_cast<unsigned char>((relative + 1) << ((i % 2) ? 0 : 4));
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
	enum class Input { None, Raw, Header, Disabled, BadCRC, DelayedHeader, SecondQam, StaleHeader, SecondQamHeader };
	Input input;
	unsigned int input_tsid = 0x1234;
	int input_relative = -1;
	unsigned int attempts = 0;
	unsigned int lock_delay = 0;
	unsigned int frequency_delay = 0;
	int frequency_error = 0;
	int lock_error = 0;
	std::chrono::steady_clock::time_point frequency_begin;

	TuneReceiver(px4::SystemType system, std::uint32_t modulation, int relative, Input value, unsigned int options = 0, bool output_tlv = false)
		: ReceiverBase(options), input(value)
	{
		params_.system = system;
		params_.modulation = modulation;
		params_.tsmf_mode = relative >= 0;
		params_.tsmf_relative_stream = relative >= 0 ? relative : 0;
		params_.tsmf_output_tlv = output_tlv;
		stream_buf_->SetTsmfRelativeStream(relative >= 0, static_cast<std::uint8_t>(params_.tsmf_relative_stream), 0, output_tlv);
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
		return frequency_error;
	}
	int CheckLock(bool &locked) override
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frequency_begin).count();
		locked = elapsed >= lock_delay && !(input == Input::StaleHeader && params_.modulation == 64);
		if (lock_error) return lock_error;
		if (input == Input::None || ((input == Input::SecondQam || input == Input::SecondQamHeader) && params_.modulation != 256)) return 0;
		std::array<unsigned char, 188> packet{};
		packet[0] = 0x47;
		packet[2] = 0x20;
		packet[3] = 0x10;
		if (input == Input::Header || input == Input::Disabled || input == Input::BadCRC || input == Input::SecondQamHeader ||
			(input == Input::DelayedHeader && elapsed >= 400) ||
			(input == Input::StaleHeader && params_.modulation == 64)) {
			packet = MakeTsmfHeader(input_relative >= 0 ? input_relative : params_.tsmf_relative_stream,
				input != Input::Disabled, 1, input_tsid, 0x4321);
			if (input == Input::BadCRC) packet[187] ^= 1;
		}
		std::size_t size = packet.size();
		stream_buf_->Write(packet.data(), size);
		return 0;
	}
};

std::array<unsigned char, 188> MakeTlvPacket(bool start, unsigned char pointer, unsigned char payload)
{
	std::array<unsigned char, 188> packet;
	packet.fill(payload);
	packet[0] = 0x47;
	packet[1] = start ? 0x40 : 0;
	packet[2] = 0x2d;
	if (start) packet[3] = pointer;
	return packet;
}

enum class TlvReset { Normal, TEIStart, TEIContinuation, Pointer184, Pointer255, SyncByte, MissingHeader, Restart, Purge, RetuneFailure };

bool TestTlvReset(TlvReset reason)
{
	TuneReceiver receiver(px4::SystemType::ISDB_C, 256, 0, TuneReceiver::Input::None, 0, true);
	auto stream = receiver.GetStreamBuffer();
	auto write = [&](const std::array<unsigned char, 188> &packet, std::size_t expected) {
		std::size_t size = packet.size();
		return stream->Write(packet.data(), size) && size == expected;
	};
	auto read = [&](const std::vector<unsigned char> &expected) {
		std::vector<unsigned char> output;
		stream->HandleRead(4096, [&](const void *buf, std::size_t size) {
			const auto *bytes = static_cast<const unsigned char *>(buf);
			output.assign(bytes, bytes + size);
			return false;
		});
		return output == expected;
	};
	const auto header = MakeTsmfHeader(0, true, 52);
	const auto continuation = MakeTlvPacket(false, 0, 0x22);
	if (!write(header, 0) || !write(MakeTlvPacket(true, 0, 0x11), 0)) return false;
	if (reason == TlvReset::Normal || reason == TlvReset::MissingHeader) {
		auto null_packet = continuation;
		null_packet[1] = 0x1f;
		null_packet[2] = 0xff;
		for (unsigned int i = 1; i < 52; ++i)
			if (!write(null_packet, 0)) return false;
		if (reason == TlvReset::Normal) {
			if (!write(header, 0) || !write(continuation, 0) || !write(MakeTlvPacket(true, 3, 0x33), 372)) return false;
			std::vector<unsigned char> expected(184, 0x11);
			expected.insert(expected.end(), 185, 0x22);
			expected.insert(expected.end(), 3, 0x33);
			if (!read(expected) || !write(MakeTlvPacket(true, 0, 0x44), 181) || !read(std::vector<unsigned char>(181, 0x33))) return false;
			expected.assign(184, 0x44);
			expected.insert(expected.end(), 183, 0x55);
			if (!write(MakeTlvPacket(true, 183, 0x55), 367) || !read(expected)) return false;
			return write(MakeTlvPacket(true, 0, 0x66), 1) && read(std::vector<unsigned char>(1, 0x55));
		}
	}
	if (reason == TlvReset::Restart) {
		stream->Stop();
		stream->Start();
	} else if (reason == TlvReset::Purge) {
		if (!stream->Purge()) return false;
	} else if (reason == TlvReset::RetuneFailure) {
		receiver.frequency_error = -EIO;
		if (receiver.Tune(1000) || receiver.attempts != 1) return false;
	} else {
		auto invalid = MakeTlvPacket(true, 0, 0x22);
		if (reason == TlvReset::TEIStart) invalid[1] |= 0x80;
		if (reason == TlvReset::TEIContinuation) {
			invalid = continuation;
			invalid[1] |= 0x80;
		}
		if (reason == TlvReset::Pointer184) invalid[3] = 184;
		if (reason == TlvReset::Pointer255) invalid[3] = 255;
		if (reason == TlvReset::SyncByte) invalid[0] = 0x46;
		if (reason == TlvReset::MissingHeader) invalid = continuation;
		if (!write(invalid, 0)) return false;
	}
	if (!write(header, 0) || !write(continuation, 0) || !write(MakeTlvPacket(true, 0, 0x33), 0)) return false;
	return write(MakeTlvPacket(true, 0, 0x44), 184) && read(std::vector<unsigned char>(184, 0x33));
}

bool TestTlvReassembly()
{
	const char *names[] = {"normal frame crossing", "TEI start", "TEI continuation", "pointer 184", "pointer 255", "sync byte", "missing header", "restart", "purge", "retune failure"};
	bool valid = true;
	for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (!TestTlvReset(static_cast<TlvReset>(i))) {
			std::fprintf(stderr, "TLV case failed: %s\n", names[i]);
			valid = false;
		}
	}
	if (valid) std::puts("stream_transfer_test: passed (TLV frame crossing, TEI, pointer boundaries, TSMF resync, restart, purge and failed retune)");
	return valid;
}

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

bool SetTsmfSelection(TuneReceiver &receiver, px4::command::ParameterType type, std::uint32_t selection,
	std::uint32_t modulation = 256, bool tlv = false)
{
	using namespace px4::command;
	alignas(ParameterSet) unsigned char storage[offsetof(ParameterSet, params) + 7 * sizeof(Parameter)]{};
	auto &set = *reinterpret_cast<ParameterSet *>(storage);
	set.system = px4::SystemType::ISDB_C;
	set.freq = 123000;
	set.num = tlv ? 7 : 6;
	set.params[0] = {ParameterType::BANDWIDTH, 6};
	set.params[1] = {ParameterType::SYMBOL_RATE, 5274};
	set.params[2] = {ParameterType::MODULATION, modulation};
	set.params[3] = {ParameterType::TSMF_MODE, 1};
	set.params[4] = {type, selection};
	set.params[5] = {ParameterType::TSMF_SLOT_OFFSET, 0};
	set.params[6] = {ParameterType::TSMF_OUTPUT_TLV, 1};
	return receiver.SetParameters(set);
}

bool TestTsidSelection()
{
	using Type = px4::command::ParameterType;
	using Input = TuneReceiver::Input;
	static_assert(static_cast<unsigned int>(Type::TSMF_TSID) == 8);
	static_assert(static_cast<unsigned int>(Type::STREAM_ID) == 16);
	const Input inputs[] = {Input::Header, Input::Raw, Input::Disabled, Input::BadCRC,
		Input::DelayedHeader, Input::SecondQamHeader, Input::StaleHeader, Input::Header};
	for (unsigned int i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
		TuneReceiver receiver(px4::SystemType::ISDB_C, 256, 0, inputs[i]);
		receiver.input_relative = 14;
		if (i == 7) receiver.input_tsid = 0x5678;
		const bool automatic = i == 5 || i == 6;
		if (!SetTsmfSelection(receiver, Type::TSMF_TSID, 0x1234, automatic ? 0 : 256)) return false;
		const bool expected = i == 0 || i == 4 || i == 5;
		if (receiver.Tune(1000) != expected || receiver.attempts != (automatic ? 2U : 1U)) return false;
	}
	TuneReceiver timeout(px4::SystemType::ISDB_C, 256, 0, Input::Header);
	if (!SetTsmfSelection(timeout, Type::TSMF_TSID, 0x5678)) return false;
	const auto begin = std::chrono::steady_clock::now();
	if (timeout.Tune(200) || std::chrono::steady_clock::now() - begin >= std::chrono::milliseconds(350)) return false;

	TuneReceiver receiver(px4::SystemType::ISDB_C, 256, 0, Input::None);
	if (!SetTsmfSelection(receiver, Type::TSMF_TSID, 0x1234, 256, true)) return false;
	px4::command::ParameterSet get{};
	get.num = 1;
	get.params[0].type = Type::TSMF_TSID;
	if (!receiver.GetParameters(get) || get.params[0].value != 0x1234) return false;
	auto stream = receiver.GetStreamBuffer();
	auto write = [&](const std::array<unsigned char, 188> &packet, std::size_t expected) {
		std::size_t size = packet.size();
		return stream->Write(packet.data(), size) && size == expected;
	};
	auto read = [&](const std::vector<unsigned char> &expected) {
		std::vector<unsigned char> output;
		stream->HandleRead(4096, [&](const void *buf, std::size_t size) {
			const auto *bytes = static_cast<const unsigned char *>(buf);
			output.assign(bytes, bytes + size);
			return false;
		});
		return output == expected;
	};
	auto null_packet = MakeTlvPacket(false, 0, 0);
	null_packet[1] = 0x1f;
	null_packet[2] = 0xff;
	if (stream->IsTsmfStreamReady() || !write(MakeTsmfHeader(0, true, 52, 0x1234, 1), 0) ||
		!stream->IsTsmfStreamReady() || !write(MakeTlvPacket(true, 0, 0x11), 0)) return false;
	for (unsigned int i = 1; i < 52; ++i)
		if (!write(null_packet, 0)) return false;
	if (!write(MakeTsmfHeader(0, true, 52, 0x1234, 2), 0) ||
		!write(MakeTlvPacket(false, 0, 0x22), 0) || !write(MakeTlvPacket(true, 3, 0x33), 372)) return false;
	std::vector<unsigned char> expected(184, 0x11);
	expected.insert(expected.end(), 185, 0x22);
	expected.insert(expected.end(), 3, 0x33);
	if (!read(expected)) return false;
	for (unsigned int i = 2; i < 52; ++i)
		if (!write(null_packet, 0)) return false;
	if (!write(MakeTsmfHeader(14, true, 52, 0x1234), 0) || !stream->IsTsmfStreamReady() ||
		!write(MakeTlvPacket(false, 0, 0x22), 0) || !write(MakeTlvPacket(true, 0, 0x44), 0) ||
		!write(MakeTlvPacket(true, 0, 0x55), 184) || !read(std::vector<unsigned char>(184, 0x44))) return false;
	if (!write(MakeTsmfHeader(0, true, 52, 0x5678), 0) || stream->IsTsmfStreamReady() ||
		!write(MakeTlvPacket(true, 0, 0x66), 0) || !write(MakeTsmfHeader(14, true, 52, 0x1234), 0) ||
		!write(MakeTlvPacket(false, 0, 0x22), 0) || !write(MakeTlvPacket(true, 0, 0x66), 0) ||
		!write(MakeTlvPacket(true, 0, 0x77), 184) || !read(std::vector<unsigned char>(184, 0x66))) return false;
	if (!stream->Purge() || stream->IsTsmfStreamReady()) return false;
	if (!write(MakeTsmfHeader(14, true, 52, 0x1234), 0) || !stream->IsTsmfStreamReady()) return false;
	stream->Stop();
	stream->Start();
	if (stream->IsTsmfStreamReady()) return false;
	if (!SetTsmfSelection(receiver, Type::TSMF_RELATIVE_STREAM, 0)) return false;
	if (!receiver.GetParameters(get) || get.params[0].value != 0xffffffffU) return false;
	const auto raw_packet = MakeTlvPacket(true, 0, 0x88);
	if (!write(MakeTsmfHeader(0, true, 1, 0x5678), 0) || !stream->IsTsmfStreamReady() ||
		!write(raw_packet, 188) || !read(std::vector<unsigned char>(raw_packet.begin(), raw_packet.end()))) return false;
	if (SetTsmfSelection(receiver, Type::TSMF_TSID, 65536)) return false;
	for (unsigned int tsid : {0U, 65535U}) {
		if (!SetTsmfSelection(receiver, Type::TSMF_TSID, tsid) ||
			!write(MakeTsmfHeader(tsid ? 14 : 0, true, 1, tsid), 0) || !stream->IsTsmfStreamReady()) return false;
	}
	std::puts("stream_transfer_test: passed (TSID tune/deadline, ONID wildcard, remap TLV discard, same-map preservation and relative-mode reset)");
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
	if (!TestTsidSelection()) return 6;
	if (!TestTlvReassembly()) return 5;
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
