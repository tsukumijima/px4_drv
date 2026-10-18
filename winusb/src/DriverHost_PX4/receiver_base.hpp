// receiver_base.hpp

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>

#include "type.hpp"
#include "command.hpp"
#include "ringbuffer.hpp"

namespace px4 {

class ReceiverBase {
public:
	struct Parameters {
		px4::SystemType system;
		std::uint32_t freq;
		std::uint32_t bandwidth;
		std::uint32_t symbol_rate;
		std::uint32_t modulation;
		std::uint32_t tsmf_mode;
		std::uint32_t tsmf_relative_stream;
		std::uint32_t tsmf_slot_offset;
		std::uint32_t tsmf_output_tlv;
		std::uint32_t stream_id;
	};

	class StreamBuffer final {
	public:
		struct WriteDiagnostics {
			std::uint64_t rejected_bytes;
			std::uint64_t rejected_writes;
			std::uint64_t purge_write_discarded_bytes;
			std::uint64_t purge_requests;
		};
		StreamBuffer();
		~StreamBuffer();

		void SetThresholdSize(std::size_t size) noexcept;
		bool Alloc(std::size_t size);
		void Start() noexcept;
		void Stop() noexcept;
		void StopRequest() noexcept;
		/* The input side must retain null packets for a TSMF frame.  This
		 * filter runs at the final output stage: after TSMF selection when
		 * enabled, and directly on ordinary TS otherwise. */
		void SetDiscardNullPacketsOnOutput(bool enabled) noexcept;
		/* PID 0x002f is a TSMF multi-frame header.  It is an ISDB-C-only
		 * output filter and is never applied to terrestrial or satellite TS. */
		void SetDiscardTsmfHeaderPacketsOnIsdbCOutput(bool enabled) noexcept;
		void SetTsmfRelativeStream(bool enabled, std::uint8_t stream, std::int32_t slot_offset, bool output_tlv = false) noexcept;
		/* Raw TS packets received from USB since the last reset, counted before
		 * TSMF or output-PID filtering. */
		void ResetInputPacketCount() noexcept;
		std::uint64_t GetInputPacketCount() const noexcept;
		bool IsTsmfStreamReady() noexcept;
		WriteDiagnostics GetWriteDiagnostics() const noexcept;
		bool Write(const void *buf, std::size_t &size) noexcept;
		void NotifyWrite() noexcept;
		void HandleRead(std::size_t buf_size, std::function<bool(const void *buf, std::size_t size)> handler);
		bool Purge() noexcept;

	private:
		bool WriteRingBuffer(const void *buf, std::size_t &size) noexcept;
		bool TsmfSelectPacket(const std::uint8_t *packet) noexcept;
		bool TsmfParseHeader(const std::uint8_t *packet) noexcept;
		bool TsmfExtractTlvPacket(const std::uint8_t *packet, std::size_t &output_size) noexcept;
		bool TsmfAppendTlv(const std::uint8_t *data, std::size_t size) noexcept;

		std::mutex mtx_;
		std::mutex tsmf_mtx_;
		px4::RingBuffer ringbuf_;
		std::atomic_bool stop_;
		std::atomic_uint64_t input_packet_count_;
		std::atomic_uint64_t rejected_bytes_;
		std::atomic_uint64_t rejected_writes_;
		std::atomic_uint64_t purge_write_discarded_bytes_;
		std::atomic_uint64_t purge_requests_;
		std::condition_variable cond_;
		std::size_t write_size_;
		std::size_t threshold_size_;
		bool discard_null_packets_on_output_;
		bool discard_tsmf_header_packets_on_isdbc_output_;
		bool tsmf_enabled_;
		bool tsmf_output_tlv_;
		bool tsmf_logged_header_;
		bool tsmf_logged_tlv_type_mismatch_;
		std::uint8_t tsmf_relative_stream_;
		std::int32_t tsmf_slot_offset_;
		int tsmf_slot_;
		std::array<std::uint8_t, 15> tsmf_stream_enabled_;
		/* TSMF extension stream_type: 0 means a segmented TLV stream. */
		std::array<std::uint8_t, 15> tsmf_stream_type_;
		std::array<std::uint8_t, 52> tsmf_slot_stream_;
		/* A TLV packet can span several selected 188-byte TSMF slots. */
		static constexpr std::size_t TSMF_TLV_MAX_SIZE = 65536;
		std::array<std::uint8_t, TSMF_TLV_MAX_SIZE> tsmf_tlv_buf_;
		std::array<std::uint8_t, TSMF_TLV_MAX_SIZE> tsmf_tlv_output_buf_;
		std::size_t tsmf_tlv_size_;
		std::uint64_t tsmf_tlv_output_count_;
		std::uint64_t tsmf_tlv_output_bytes_;
		/* TSMF diagnostics.  These counters are reset for every tune. */
		std::uint64_t tsmf_input_packets_;
		std::uint64_t tsmf_selected_packets_;
		std::uint64_t tsmf_rejected_packets_;
		std::uint64_t tsmf_header_packets_;
		std::uint64_t tsmf_tei_packets_;
		std::uint64_t tsmf_null_packets_;
		std::uint64_t tsmf_discontinuity_indicators_;
		std::uint64_t tsmf_cc_gaps_;
		std::array<std::uint8_t, 8192> tsmf_last_cc_;
		std::array<std::uint8_t, 8192> tsmf_cc_valid_;
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
	/* 機種固有の正規化を自動 QAM 判定より先に行う。既存機種は変更なし。 */
	virtual int PrepareTune() { return 0; }
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
	std::mutex lock_;
};

class ReceiverError : public std::runtime_error {
public:
	explicit ReceiverError(const std::string &what_arg) : runtime_error(what_arg.c_str()) {};
};

} // namespace px4
