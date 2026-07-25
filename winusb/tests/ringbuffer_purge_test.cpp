#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "ringbuffer.hpp"

namespace {

/* 取りこぼしは数十回のパージで現れるため、毎回のビルドを止めない長さに収める */
constexpr unsigned int TEST_DURATION_SECONDS = 10;
/* 進捗が完全に止まってから打ち切るまでの猶予 */
constexpr unsigned int STALL_TIMEOUT_SECONDS = 5;
constexpr std::size_t CHUNK_SIZE = 188 * 16;

} // namespace

int main()
{
	px4::RingBuffer buffer(188 * 4096);

	buffer.Start();

	std::atomic_bool stop{ false };
	std::atomic_uint64_t purge_count{ 0 };

	/*
	 * 利用者数を絶えず 0 と 1 の間で往復させ、Purge() が待機へ入る瞬間と
	 * 最後の利用者が抜ける瞬間を衝突させる
	 * 通知をロックの外で行うと、この衝突で通知が消えて Purge() が戻らなくなる
	 */
	std::thread writer([&buffer, &stop] {
		std::vector<std::uint8_t> data(CHUNK_SIZE, 0x47);

		while (!stop) {
			std::size_t size = data.size();
			buffer.Write(data.data(), size);
		}
	});
	std::thread reader([&buffer, &stop] {
		std::vector<std::uint8_t> data(CHUNK_SIZE);

		while (!stop) {
			std::size_t size = data.size();
			buffer.Read(data.data(), size);
		}
	});
	std::thread purger([&buffer, &stop, &purge_count] {
		while (!stop) {
			buffer.Purge();
			purge_count++;
		}
	});

	/* Purge() が戻らなくなると回数が増えなくなるため、進捗の停止で検出する */
	bool stalled = false;
	std::uint64_t last_count = 0;
	auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(TEST_DURATION_SECONDS);

	while (std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::seconds(STALL_TIMEOUT_SECONDS));

		std::uint64_t current_count = purge_count.load();

		if (current_count == last_count) {
			stalled = true;
			break;
		}
		last_count = current_count;
	}

	stop = true;

	if (stalled) {
		std::fprintf(stderr,
			"Purge() stopped returning after %llu calls.\n",
			static_cast<unsigned long long>(last_count));
		std::printf("ringbuffer_purge_test: failed\n");
		/*
		 * 取りこぼしが起きた場合、purger は Purge() の中で戻らないため join() できない
		 * main を通常どおり抜けると、待機中のスレッドが使っている buffer と
		 * その同期オブジェクトを破棄してしまうため、後始末を行わずに終了する
		 * 判定はここまでで確定しているので、出力だけ流し切ってプロセスごと落とす
		 */
		std::fflush(nullptr);
		std::_Exit(1);
	}

	writer.join();
	reader.join();
	purger.join();

	std::printf("ringbuffer_purge_test: passed (purges=%llu)\n",
		static_cast<unsigned long long>(purge_count.load()));
	return 0;
}
