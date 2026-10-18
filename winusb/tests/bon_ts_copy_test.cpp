#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "bon_ts_copy.hpp"
#include "io_queue.hpp"

struct Queue {
	std::vector<unsigned char> input;
	std::size_t offset = 0;
	std::size_t largest_request = 0;
	bool Read(void *destination, std::size_t &size, std::size_t &remain, bool blocking)
	{
		if (blocking) return false;
		largest_request = (std::max)(largest_request, size);
		size = (std::min)(size, input.size() - offset);
		if (size) std::memcpy(destination, input.data() + offset, size);
		offset += size;
		remain = offset < input.size() ? 1 : 0;
		return size != 0;
	}
};

class FragmentProvider final : public px4::IoQueue::IoProvider {
public:
	std::vector<unsigned char> input;
	std::size_t offset = 0;
	std::size_t fragment;
	explicit FragmentProvider(std::size_t length) : fragment(length)
	{
		input.resize(188 * (1024 * 129 + 1));
		for (std::size_t i = 0; i < input.size(); ++i)
			input[i] = static_cast<unsigned char>((i * 17 + i / 251) % 253);
	}
	bool Start() override { return true; }
	void Stop() override {}
	bool Do(void *destination, std::size_t &size) override
	{
		size = (std::min)((std::min)(size, fragment), input.size() - offset);
		if (!size) return false;
		std::memcpy(destination, input.data() + offset, size);
		offset += size;
		return true;
	}
};

bool TestRealQueue(bool copy, std::size_t fragment)
{
	FragmentProvider provider(fragment);
	// 小さいキューでバッファの再利用と書き込み待ちを発生させ、実装本体の分割読み出しを検証する
	px4::IoQueue queue(px4::IoQueue::IoOperation::READ, provider, 188 * 1024, 4, 2, 0);
	std::array<unsigned char, px4::BON_TS_COPY_SIZE + 32> guarded;
	std::vector<unsigned char> received;
	received.reserve(provider.input.size());
	queue.Start();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	bool valid = true;
	while (received.size() < provider.input.size() && std::chrono::steady_clock::now() < deadline) {
		guarded.fill(0xA5);
		void *pointer = guarded.data() + 16;
		std::size_t size = 0, remain = 0;
		const bool available = copy ? px4::ReadBonTsCopy(queue, pointer, size, remain)
			: queue.ReadBuffer(&pointer, size, remain, false);
		if (!available) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		if (copy && size > px4::BON_TS_COPY_SIZE) { valid = false; break; }
		for (std::size_t i = 0; i < 16; ++i)
			if (guarded[i] != 0xA5 || guarded[16 + px4::BON_TS_COPY_SIZE + i] != 0xA5) valid = false;
		if (!valid || size > provider.input.size() - received.size()) { valid = false; break; }
		const auto *bytes = static_cast<const unsigned char *>(pointer);
		received.insert(received.end(), bytes, bytes + size);
	}
	queue.Stop();
	return valid && received == provider.input;
}

int main()
{
	Queue queue;
	queue.input.resize(188 * 1024 + 188);
	for (std::size_t i = 0; i < queue.input.size(); ++i)
		queue.input[i] = static_cast<unsigned char>(i % 251);
	std::array<unsigned char, px4::BON_TS_COPY_SIZE + 32> destination;
	std::vector<unsigned char> received;
	std::size_t size = 0, remain = 0;
	for (int iteration = 0; iteration < 5; ++iteration) {
		destination.fill(0xA5);
		// 出力引数の初期値に依存せず、1024パケットの入力も分割して読む
		size = iteration % 2 ? static_cast<std::size_t>(-1) : 0;
		if (!px4::ReadBonTsCopy(queue, destination.data() + 16, size, remain)) return 1;
		if (size > px4::BON_TS_COPY_SIZE) return 2;
		for (std::size_t i = 0; i < 16; ++i)
			if (destination[i] != 0xA5 || destination[16 + px4::BON_TS_COPY_SIZE + i] != 0xA5) return 3;
		received.insert(received.end(), destination.begin() + 16, destination.begin() + 16 + size);
	}
	if (received != queue.input || remain != 0 || queue.largest_request != px4::BON_TS_COPY_SIZE) return 4;
	if (px4::ReadBonTsCopy(queue, destination.data() + 16, size, remain) || size != 0 || remain != 0) return 5;
	for (const auto fragment : {std::size_t(4093), std::size_t(188 * 816), std::size_t(65536), std::size_t(188 * 4096)}) {
		if (!TestRealQueue(true, fragment)) return 6;
		if (!TestRealQueue(false, fragment)) return 7;
	}
	std::puts("bon_ts_copy_test: passed (bounded copy, guards, split order, short tail, empty)");
	std::puts("bon_ts_copy_test: passed (real IoQueue, copy/pointer, four fragment sizes, backpressure, ordered bytes)");
	return 0;
}
