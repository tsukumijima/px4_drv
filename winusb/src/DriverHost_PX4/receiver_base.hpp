// receiver_base.hpp

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>

#include "type.hpp"
#include "command.hpp"
#include "ringbuffer.hpp"
#include "tsmf.h"

namespace px4 {

class ReceiverBase {
public:
	struct Parameters {
		px4::SystemType system;
		std::uint32_t freq;
		std::uint32_t bandwidth;
		std::uint32_t stream_id;
		// 以降は ISDB-C だけで使い、TSMF の TS を指定しない項目は -1
		std::uint32_t symbol_rate;
		std::int32_t tsmf_relative_stream;
		std::int32_t tsmf_tsid;
		bool tsmf_output_tlv;
	};

	class StreamBuffer final {
	public:
		StreamBuffer();
		~StreamBuffer();

		void SetThresholdSize(std::size_t size) noexcept;
		bool Alloc(std::size_t size);
		void Start() noexcept;
		void Stop() noexcept;
		void StopRequest() noexcept;
		bool Write(const void *buf, std::size_t &size) noexcept;
		void NotifyWrite() noexcept;
		void HandleRead(std::size_t buf_size, std::function<bool(const void *buf, std::size_t size)> handler);
		bool Purge() noexcept;
		// TSMF で多重された搬送波の選局から次の選局までの間だけ、指定した TS を選んで出力する
		void StartTsmf(std::int32_t relative_stream, std::int32_t tsid, bool output_tlv, bool discard_null_packets);
		void StopTsmf() noexcept;
		void RestartTsmf() noexcept;
		bool IsTsmfStreamFound() noexcept;

	private:
		bool WriteTsmf(const void *buf, std::size_t &size) noexcept;

		std::mutex mtx_;
		px4::RingBuffer ringbuf_;
		std::atomic_bool stop_;
		std::condition_variable cond_;
		std::size_t write_size_;
		std::size_t threshold_size_;

		std::atomic_bool tsmf_;
		std::mutex tsmf_mtx_;
		// TLV の蓄積領域を含めて大きいため、TSMF を指定した受信機だけに確保する
		std::unique_ptr<tsmf_filter> tsmf_filter_;
	};

	ReceiverBase(unsigned int options);
	virtual ~ReceiverBase();

	// cannot copy
	ReceiverBase(const ReceiverBase &) = delete;
	ReceiverBase& operator=(const ReceiverBase &) = delete;

	// cannot move
	ReceiverBase(ReceiverBase &&) = delete;
	ReceiverBase& operator=(ReceiverBase &&) = delete;

	bool GetParameters(px4::command::ParameterSet &param_set) noexcept;
	bool SetParameters(const px4::command::ParameterSet &param_set) noexcept;
	void ClearParameters() noexcept;
	bool Tune(std::uint32_t timeout);
	bool ReadStats(px4::command::StatSet &stat_set);
	std::shared_ptr<StreamBuffer> GetStreamBuffer() noexcept { return stream_buf_; }

	virtual int Open() = 0;
	virtual void Close() = 0;
	virtual int CheckLock(bool &locked) = 0;
	virtual int SetLnbVoltage(std::int32_t voltage) = 0;
	virtual int SetCapture(bool capture) = 0;
	virtual int ReadStat(px4::command::StatType type, std::int32_t &value) = 0;

protected:
	virtual int SetFrequency() = 0;
	virtual int SetStreamId() = 0;

	unsigned int options_;
#define RECEIVER_SAT_SET_STREAM_ID_BEFORE_TUNE	0x00000010
#define RECEIVER_SAT_SET_STREAM_ID_AFTER_TUNE	0x00000020
#define RECEIVER_WAIT_AFTER_LOCK		0x00000040
#define RECEIVER_WAIT_AFTER_LOCK_TC_T		0x00000080

	Parameters params_;
	std::shared_ptr<StreamBuffer> stream_buf_;

private:
	bool TuneIsdbC(std::uint32_t timeout);

	std::mutex lock_;
};

class ReceiverError : public std::runtime_error {
public:
	explicit ReceiverError(const std::string &what_arg) : runtime_error(what_arg.c_str()) {};
};

} // namespace px4
