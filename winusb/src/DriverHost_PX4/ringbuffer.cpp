// ringbuffer.cpp

#include "ringbuffer.hpp"

#include <chrono>
#include <cstring>

#include <windows.h>

namespace px4 {

namespace {

/*
 * 利用者が抜けるのは通常なら数マイクロ秒で終わる
 * それでも待ち続ける状態が生じると受信機ごと止まってしまうため、
 * 通常時から大きく離れたこの上限で初期化を諦める
 */
constexpr unsigned int PURGE_TIMEOUT_MS = 1000;

} // namespace

RingBuffer::RingBuffer(std::size_t size)
	: state_(0),
	buf_(nullptr),
	actual_size_(0),
	head_(0),
	tail_(0),
	wait_(false),
	rw_count_(0),
	wait_lock_(),
	wait_cond_()
{
	Alloc(size);
}

RingBuffer::~RingBuffer()
{
	Stop();

	if (buf_)
		VirtualFree(buf_, 0, MEM_RELEASE);
}

bool RingBuffer::Alloc(std::size_t size)
{
	if (state_)
		return false;

	if (buf_ && buf_size_ != size) {
		VirtualFree(buf_, 0, MEM_RELEASE);
		buf_ = nullptr;
		buf_size_ = 0;
	}

	if (!buf_ && size) {
		buf_ = reinterpret_cast<std::uint8_t*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (!buf_)
			return false;

		buf_size_ = size;
	}

	Reset();

	return true;
}

void RingBuffer::Reset() noexcept
{
	actual_size_ = 0;
	head_ = 0;
	tail_ = 0;
}

void RingBuffer::Start() noexcept
{
	int expected = 0;

	state_.compare_exchange_strong(expected, 1);
}

void RingBuffer::Stop() noexcept
{
	state_ = 0;
}

void RingBuffer::NotifyIdle() noexcept
{
	if (--rw_count_ || !wait_)
		return;

	/*
	 * Purge() は wait_lock_ を保持したまま利用者数を確かめ、0 でなければ待機へ入る
	 * 同じロックを取らずに通知すると、その確認から待機に入るまでの間に通知が消え、
	 * 以降は wait_ により Read() と Write() が即座に戻るため誰も通知しなくなる
	 * Linux 版の wait_event() は待機列へ登録してから条件を確かめるためこの穴がない
	 */
	std::lock_guard<std::mutex> lock(wait_lock_);

	wait_cond_.notify_all();
}

bool RingBuffer::Read(void *buf, std::size_t &size) noexcept
{
#if 0
	int expected_state = 1;

	state_.compare_exchange_strong(expected_state, 2);
#endif

	/*
	 * 退避要求を確かめてから利用者数を増やすと、その隙に Purge() が初期化を終えてしまい、
	 * 初期化前の位置と残量で読み書きして actual_size_ が桁借りする
	 * 先に利用者として数えてから確かめれば、Purge() は必ずどちらかを見る
	 */
	++rw_count_;

	if (wait_) {
		NotifyIdle();
		size = 0;
		return true;
	}

	std::size_t actual_size = actual_size_;
	std::intptr_t head = head_;
	std::size_t buf_size = buf_size_;
	std::size_t read_size = (size <= actual_size) ? size : actual_size;

	if (read_size) {
		std::size_t tmp = (head + read_size <= buf_size) ? read_size : (buf_size - head);

		memcpy(buf, buf_ + head, tmp);

		if (tmp < read_size) {
			memcpy(reinterpret_cast<std::uint8_t *>(buf) + tmp, buf_, read_size - tmp);
			head = read_size - tmp;
		} else {
			head = (head + read_size == buf_size) ? 0 : (head + read_size);
		}

		head_ = head;
		actual_size_ -= read_size;
	}

	NotifyIdle();

	size = read_size;

	return true;
}

bool RingBuffer::Write(const void *buf, std::size_t &size) noexcept
{
#if 0
	if (state_ != 2)
		return false;
#else
	if (!state_) {
		size = 0;
		return false;
	}
#endif

	/* 読み出し側と同じ理由で、利用者として数えてから退避要求を確かめる */
	++rw_count_;

	if (wait_) {
		NotifyIdle();
		size = 0;
		return true;
	}

	std::size_t actual_size = actual_size_;
	std::intptr_t tail = tail_;
	std::size_t buf_size = buf_size_;
	std::size_t write_size = (actual_size + size <= buf_size) ? size : (buf_size - actual_size);

	if (write_size) {
		std::size_t tmp = (tail + write_size <= buf_size) ? write_size : (buf_size - tail);

		std::memcpy(buf_ + tail, buf, tmp);

		if (tmp < write_size) {
			std::memcpy(buf_, reinterpret_cast<const std::uint8_t *>(buf) + tmp, write_size - tmp);
			tail = write_size - tmp;
		} else {
			tail = (tail + write_size == buf_size) ? 0 : (tail + write_size);
		}

		tail_ = tail;
		actual_size_ += write_size;
	}

	NotifyIdle();

	bool ret = (size == write_size);

	size = write_size;
	return ret;
}

bool RingBuffer::Purge() noexcept
{
	try {
		bool expected = false;

		if (!wait_.compare_exchange_strong(expected, true))
			return false;

		bool quiesced;

		{
			std::unique_lock<std::mutex> lock(wait_lock_);
			quiesced = wait_cond_.wait_for(lock,
				std::chrono::milliseconds(PURGE_TIMEOUT_MS), [this] {
					return (rw_count_ == 0);
				});
		}

		/*
		 * 諦めた場合に初期化まで行うと、読み書きの最中に位置と残量を書き換える
		 * 前チャンネルの TS が残る代わりに、内容はそのままにして失敗を返す
		 */
		if (quiesced)
			Reset();

		wait_ = false;
		return quiesced;
	} catch (...) {
		return false;
	}
}

} // namespace px4
