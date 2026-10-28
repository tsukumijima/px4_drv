// receiver_base.cpp

#include "receiver_base.hpp"

#include <algorithm>
#include <chrono>
#include <windows.h>

#include "msg.h"

namespace px4 {

ReceiverBase::ReceiverBase(unsigned int options)
	: options_(options),
	lock_()
{
	memset(&params_, 0, sizeof(params_));
	stream_buf_.reset(new StreamBuffer());
}

ReceiverBase::~ReceiverBase()
{

}

bool ReceiverBase::GetParameters(px4::command::ParameterSet &param_set) noexcept
{
	std::lock_guard<std::mutex> lock(lock_);

	param_set.system = params_.system;
	param_set.freq = params_.freq;

	for (std::uint32_t i = 0; i < param_set.num; i++) {
		switch (param_set.params[i].type) {
		case px4::command::ParameterType::BANDWIDTH:
			param_set.params[i].value = params_.bandwidth;
			break;

		case px4::command::ParameterType::TSMF_STREAM:
			param_set.params[i].value = params_.tsmf_stream;
			break;

		case px4::command::ParameterType::STREAM_ID:
			param_set.params[i].value = params_.stream_id;
			break;

		default:
			// unknown parameter type
			return false;
		}
	}

	return true;
}

bool ReceiverBase::SetParameters(const px4::command::ParameterSet &param_set) noexcept
{
	std::lock_guard<std::mutex> lock(lock_);

	params_.system = param_set.system;
	params_.freq = param_set.freq;
	params_.tsmf_stream = 1;

	for (std::uint32_t i = 0; i < param_set.num; i++) {
		switch (param_set.params[i].type) {
		case px4::command::ParameterType::BANDWIDTH:
			params_.bandwidth = param_set.params[i].value;
			break;

		// J.83 Annex B は TSMF を使わないため、ISDB-C だけで受け付ける
		case px4::command::ParameterType::TSMF_STREAM:
			if (params_.system != px4::SystemType::ISDB_C ||
			    param_set.params[i].value < 1 || param_set.params[i].value > TSMF_STREAM_MAX)
				return false;
			params_.tsmf_stream = param_set.params[i].value;
			break;

		case px4::command::ParameterType::STREAM_ID:
			params_.stream_id = param_set.params[i].value;
			break;

		default:
			// unknown parameter type
			return false;
		}
	}

	return true;
}

void ReceiverBase::ClearParameters() noexcept
{
	std::lock_guard<std::mutex> lock(lock_);

	memset(&params_, 0, sizeof(params_));
}

bool ReceiverBase::Tune(std::uint32_t timeout)
{
	int ret = 0;
	std::lock_guard<std::mutex> lock(lock_);

	if (params_.system == px4::SystemType::ISDB_C || params_.system == px4::SystemType::J83B)
		return TuneIsdbC(timeout);

	if ((params_.system == px4::SystemType::ISDB_S) && (options_ & RECEIVER_SAT_SET_STREAM_ID_BEFORE_TUNE)) {
		ret = SetStreamId();
		if (ret)
			return false;
	}

	ret = SetFrequency();
	if (ret)
		return false;

	int i;
	auto begin = std::chrono::steady_clock::now();
	bool locked = false;

	for (i = 0; true; i++) {
		ret = CheckLock(locked);
		if ((!ret && locked) || ret == -ECANCELED)
			break;

		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
		if (duration.count() >= timeout) {
			ret = -ETIMEDOUT;
			break;
		}

		Sleep(20);
	}

	if (ret || !locked)
		return false;

	if ((params_.system == px4::SystemType::ISDB_T) && (options_ & RECEIVER_WAIT_AFTER_LOCK_TC_T) && (i < 35))
		Sleep((35 - i) * 10);

	if ((params_.system == px4::SystemType::ISDB_S) && !(options_ & RECEIVER_SAT_SET_STREAM_ID_BEFORE_TUNE)) {
		ret = SetStreamId();
		if (ret)
			return false;
	}

	if (options_ & RECEIVER_WAIT_AFTER_LOCK)
		Sleep(200);

	return true;
}

bool ReceiverBase::TuneIsdbC(std::uint32_t timeout)
{
	if (SetFrequency())
		return false;

	// Sony の sony_integ_isdbc_WaitTSLock() と sony_integ_j83b_WaitTSLock() は、復調器のロックを1秒で打ち切る
	// 信号のない搬送波を TuneTimeout まで待つと、行の多い ChSet のスキャンが長引くため、ロックまでの待ちだけこの上限にする
	const std::uint32_t lock_timeout = (std::min<std::uint32_t>)(timeout, 1000);
	auto begin = std::chrono::steady_clock::now();
	bool lock_seen = false;

	while (true) {
		bool locked = false;
		int ret = CheckLock(locked);

		if (ret == -ECANCELED)
			return false;

		if (!ret && locked) {
			// ロックより前に届いたヘッダーは前の搬送波のものかもしれないため、ロックしてから TSMF を解析し直す
			if (!lock_seen) {
				lock_seen = true;
				stream_buf_->RestartTsmf();
			} else {
				switch (stream_buf_->GetTsmfState()) {
				case TSMF_FILTER_FOUND:
					return true;
				case TSMF_FILTER_NOT_FOUND:
					// 指定した何本目の TS がない搬送波は、待っても現れないため、スキャンを長引かせずに失敗させる
					return false;
				default:
					break;
				}
			}
		}

		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
		if (duration.count() >= ((lock_seen) ? timeout : lock_timeout))
			return false;

		Sleep(20);
	}
}

bool ReceiverBase::ReadStats(px4::command::StatSet &stat_set)
{
	std::lock_guard<std::mutex> lock(lock_);

	for (std::uint32_t i = 0; i < stat_set.num; i++) {
		if (ReadStat(stat_set.data[i].type, stat_set.data[i].value))
			return false;
	}

	return true;
}

ReceiverBase::StreamBuffer::StreamBuffer()
	: write_size_(0),
	threshold_size_(0),
	tsmf_(false)
{

}

ReceiverBase::StreamBuffer::~StreamBuffer()
{

}

void ReceiverBase::StreamBuffer::SetThresholdSize(std::size_t size) noexcept
{
	threshold_size_ = size;
}

bool ReceiverBase::StreamBuffer::Alloc(std::size_t size)
{
	return ringbuf_.Alloc(size);
}

void ReceiverBase::StreamBuffer::Start() noexcept
{
	stop_ = false;
	write_size_ = 0;

	ringbuf_.Start();
	return;
}

void ReceiverBase::StreamBuffer::Stop() noexcept
{
	ringbuf_.Stop();

	stop_ = true;
	cond_.notify_all();

	return;
}

void ReceiverBase::StreamBuffer::StopRequest() noexcept
{
	stop_ = true;
	cond_.notify_all();

	return;
}

bool ReceiverBase::StreamBuffer::Write(const void *buf, std::size_t &size) noexcept
{
	bool ret;

	if (tsmf_)
		return WriteTsmf(buf, size);

	ret = ringbuf_.Write(buf, size);
	write_size_ += size;

	return ret;
}

void ReceiverBase::StreamBuffer::NotifyWrite() noexcept
{
	// TSMF で選んだ TS は低レートのことがあり、しきい値までためると読み出しが秒単位で遅れるため、出力があるたびに通知する
	if (tsmf_) {
		if (write_size_) {
			cond_.notify_all();
			write_size_ = 0;
		}
		return;
	}

	if (write_size_ >= threshold_size_) {
		cond_.notify_all();
		write_size_ -= threshold_size_;
	}
	return;
}

void ReceiverBase::StreamBuffer::HandleRead(std::size_t buf_size, std::function<bool(const void *buf, std::size_t size)> handler)
{
	stop_ = false;

	std::unique_ptr<std::uint8_t[]> buf(new std::uint8_t[buf_size]);
	std::uint8_t *p = buf.get();
	std::unique_lock<std::mutex> lock(mtx_);

	while (!ringbuf_.IsActive() && !stop_)
		cond_.wait(lock);

	while (true) {
		while (!ringbuf_.GetReadableSize() && !stop_)
			cond_.wait(lock);

		if (stop_)
			break;

		std::size_t size = buf_size;

		if (!ringbuf_.Read(p, size))
			break;

		if (!size)
			continue;

		if (!handler(p, size))
			break;
	}

	return;
}

bool ReceiverBase::StreamBuffer::Purge() noexcept
{
	return ringbuf_.Purge();
}

void ReceiverBase::StreamBuffer::StartTsmf(std::uint32_t stream_index, bool discard_null_packets, unsigned int stale_packets)
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);

	if (!tsmf_filter_)
		tsmf_filter_.reset(new tsmf_filter);

	tsmf_filter_init(tsmf_filter_.get(), static_cast<int>(stream_index), discard_null_packets, stale_packets);
	tsmf_ = true;
}

void ReceiverBase::StreamBuffer::StopTsmf() noexcept
{
	tsmf_ = false;
}

void ReceiverBase::StreamBuffer::RestartTsmf() noexcept
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);

	if (tsmf_)
		tsmf_filter_init(tsmf_filter_.get(), tsmf_filter_->stream_index, tsmf_filter_->discard_null_packets, tsmf_filter_->stale_packets);
}

tsmf_filter_state ReceiverBase::StreamBuffer::GetTsmfState() noexcept
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);

	return (tsmf_) ? tsmf_filter_get_state(tsmf_filter_.get()) : TSMF_FILTER_FOUND;
}

bool ReceiverBase::StreamBuffer::WriteTsmf(const void *buf, std::size_t &size) noexcept
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);
	const std::uint8_t *out;
	bool ret;

	size = tsmf_filter_packet(tsmf_filter_.get(), static_cast<const std::uint8_t *>(buf), &out);
	if (!size)
		return true;

	ret = ringbuf_.Write(out, size);
	write_size_ += size;

	return ret;
}

} // namespace px4
