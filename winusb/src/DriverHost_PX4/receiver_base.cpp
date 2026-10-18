// receiver_base.cpp

#include "receiver_base.hpp"

#include <algorithm>
#include <chrono>
#include <windows.h>

#include "msg.h"

namespace px4 {

namespace {

std::uint32_t mpeg_crc32(const std::uint8_t *data, std::size_t size) noexcept
{
	std::uint32_t crc = 0xffffffff;

	for (std::size_t i = 0; i < size; i++) {
		crc ^= static_cast<std::uint32_t>(data[i]) << 24;
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc & 0x80000000) ? ((crc << 1) ^ 0x04c11db7) : (crc << 1);
	}

	return crc;
}

} // namespace

ReceiverBase::ReceiverBase(unsigned int options)
	: options_(options),
	lock_()
{
	memset(&params_, 0, sizeof(params_));
	params_.tsmf_tsid = -1;
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

		case px4::command::ParameterType::SYMBOL_RATE:
			param_set.params[i].value = params_.symbol_rate;
			break;

		case px4::command::ParameterType::MODULATION:
			param_set.params[i].value = params_.modulation;
			break;

		case px4::command::ParameterType::TSMF_MODE:
			param_set.params[i].value = params_.tsmf_mode;
			break;

		case px4::command::ParameterType::TSMF_RELATIVE_STREAM:
			param_set.params[i].value = params_.tsmf_relative_stream;
			break;
		case px4::command::ParameterType::TSMF_TSID:
			param_set.params[i].value = static_cast<std::uint32_t>(params_.tsmf_tsid);
			break;

		case px4::command::ParameterType::TSMF_SLOT_OFFSET:
			param_set.params[i].value = params_.tsmf_slot_offset;
			break;

		case px4::command::ParameterType::TSMF_OUTPUT_TLV:
			param_set.params[i].value = params_.tsmf_output_tlv;
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
	/* TSMF parameters are optional and must not leak from the previous tune. */
	params_.tsmf_mode = 0;
	params_.tsmf_relative_stream = 0;
	params_.tsmf_tsid = -1;
	params_.tsmf_slot_offset = 0;
	params_.tsmf_output_tlv = 0;

	for (std::uint32_t i = 0; i < param_set.num; i++) {
		switch (param_set.params[i].type) {
		case px4::command::ParameterType::BANDWIDTH:
			params_.bandwidth = param_set.params[i].value;
			break;

		case px4::command::ParameterType::SYMBOL_RATE:
			params_.symbol_rate = param_set.params[i].value;
			break;

		case px4::command::ParameterType::MODULATION:
			params_.modulation = param_set.params[i].value;
			break;

		case px4::command::ParameterType::TSMF_MODE:
			params_.tsmf_mode = param_set.params[i].value;
			break;

		case px4::command::ParameterType::TSMF_RELATIVE_STREAM:
			params_.tsmf_relative_stream = param_set.params[i].value;
			break;
		case px4::command::ParameterType::TSMF_TSID:
			if (param_set.params[i].value > 65535)
				return false;
			params_.tsmf_tsid = static_cast<std::int32_t>(param_set.params[i].value);
			break;

		case px4::command::ParameterType::TSMF_SLOT_OFFSET:
			params_.tsmf_slot_offset = param_set.params[i].value;
			break;

		case px4::command::ParameterType::TSMF_OUTPUT_TLV:
			params_.tsmf_output_tlv = param_set.params[i].value;
			break;

		case px4::command::ParameterType::STREAM_ID:
			params_.stream_id = param_set.params[i].value;
			break;

		default:
			// unknown parameter type
			return false;
		}
	}

	stream_buf_->SetTsmfRelativeStream(params_.tsmf_mode != 0,
		static_cast<std::uint8_t>(params_.tsmf_relative_stream),
		static_cast<std::int32_t>(params_.tsmf_slot_offset),
		params_.tsmf_output_tlv != 0, params_.tsmf_tsid);

	return true;
}

void ReceiverBase::ClearParameters() noexcept
{
	std::lock_guard<std::mutex> lock(lock_);

	memset(&params_, 0, sizeof(params_));
	params_.tsmf_tsid = -1;
	stream_buf_->SetTsmfRelativeStream(false, 0, 0, false);
}

bool ReceiverBase::Tune(std::uint32_t timeout)
{
	int ret = 0;
	std::lock_guard<std::mutex> lock(lock_);
	// ISDB-C の再試行と入力確認は、選局開始時からの期限を共有する
	const auto tune_begin = std::chrono::steady_clock::now();
	const auto tune_deadline = tune_begin + std::chrono::milliseconds(timeout);
	if (PrepareTune())
		return false;
	const bool isdbc = params_.system == px4::SystemType::ISDB_C;
	const bool isdbc_auto_qam = isdbc && params_.modulation == 0;

	/*
	 * The public BonDriver interface has no ISDB-C modulation selector.
	 * Treat an omitted modulation as an automatic 64QAM -> 256QAM probe.
	 * This matches the practical behaviour of BDA receivers without making
	 * every CATV channel appear twice in the application's channel list.
	 */
	if (isdbc_auto_qam)
		params_.modulation = 64;

	if ((params_.system == px4::SystemType::ISDB_S) && (options_ & RECEIVER_SAT_SET_STREAM_ID_BEFORE_TUNE)) {
		ret = SetStreamId();
		if (ret)
			return false;
	}

	auto tune_once = [&](std::uint32_t attempt_timeout, int &lock_checks, bool &locked) noexcept {
		auto attempt_deadline = tune_begin + std::chrono::milliseconds(attempt_timeout);
		if (isdbc && std::chrono::steady_clock::now() >= attempt_deadline) {
			ret = -ETIMEDOUT;
			return false;
		}
		// 選局に失敗した場合も、以前の TLV 断片を引き継がない
		if (isdbc && params_.tsmf_mode && params_.tsmf_output_tlv)
			stream_buf_->SetTsmfRelativeStream(true,
				static_cast<std::uint8_t>(params_.tsmf_relative_stream),
				static_cast<std::int32_t>(params_.tsmf_slot_offset), true, params_.tsmf_tsid);
		ret = SetFrequency();
		if (ret)
			return false;
		if (!isdbc)
			attempt_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(attempt_timeout);

		/* A previous channel may still have a small amount of bridge data in
		 * flight.  Drain that before opening the raw-TS observation window. */
		if (isdbc) {
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(attempt_deadline - std::chrono::steady_clock::now()).count();
			if (remaining <= 0) {
				ret = -ETIMEDOUT;
				return false;
			}
			Sleep(static_cast<DWORD>((std::min<std::int64_t>)(100, remaining)));
			stream_buf_->ResetInputPacketCount();
			// 再試行では変調方式が変わるため、ヘッダーも受信し直す
			if (params_.tsmf_mode)
				stream_buf_->SetTsmfRelativeStream(true,
					static_cast<std::uint8_t>(params_.tsmf_relative_stream),
					static_cast<std::int32_t>(params_.tsmf_slot_offset),
					params_.tsmf_output_tlv != 0, params_.tsmf_tsid);
		}

		bool input_checked = false;
		for (lock_checks = 0; true; lock_checks++) {
			if (isdbc && std::chrono::steady_clock::now() >= attempt_deadline) {
				ret = -ETIMEDOUT;
				break;
			}
			ret = CheckLock(locked);
			if (ret == -ECANCELED)
				break;

			if (!ret && locked) {
				if (!isdbc)
					break;
				if (!input_checked) {
					// 変調方式が一致しなくてもロックを報告するため、USB 側の入力で受信を確認する
					const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(attempt_deadline - std::chrono::steady_clock::now()).count();
					if (remaining <= 0) {
						ret = -ETIMEDOUT;
						break;
					}
					Sleep(static_cast<DWORD>((std::min<std::int64_t>)(200, remaining)));
					if (stream_buf_->GetInputPacketCount() == 0) {
						msg_info("ISDB-C: %uQAM reported lock but produced no TS packets.\n",
							params_.modulation);
						locked = false;
						ret = -ETIMEDOUT;
						return false;
					}
					input_checked = true;
				}
				if (std::chrono::steady_clock::now() < attempt_deadline &&
					(!params_.tsmf_mode || stream_buf_->IsTsmfStreamReady()))
					break;
			}

			if (std::chrono::steady_clock::now() >= attempt_deadline) {
				ret = -ETIMEDOUT;
				break;
			}

			if (isdbc) {
				const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(attempt_deadline - std::chrono::steady_clock::now()).count();
				if (remaining > 0)
					Sleep(static_cast<DWORD>((std::min<std::int64_t>)(20, remaining)));
			} else {
				Sleep(20);
			}
		}

		return !ret && locked;
	};

	int i = 0;
	bool locked = false;
	std::uint32_t first_timeout = timeout;
	if (isdbc_auto_qam && timeout > 1)
		first_timeout = timeout / 2;

	bool tune_ok = tune_once(first_timeout, i, locked);
	if (!tune_ok && isdbc_auto_qam && ret != -ECANCELED) {
		params_.modulation = 256;
		locked = false;
		msg_info("ISDB-C: 64QAM did not produce a usable TS; retrying 256QAM.\n");
		tune_ok = tune_once(timeout, i, locked);
	}

	if (!tune_ok)
		return false;

	if ((params_.system == px4::SystemType::ISDB_T || params_.system == px4::SystemType::ISDB_C) && (options_ & RECEIVER_WAIT_AFTER_LOCK_TC_T) && (i < 35)) {
		if (isdbc && std::chrono::steady_clock::now() + std::chrono::milliseconds((35 - i) * 10) >= tune_deadline)
			return false;
		Sleep((35 - i) * 10);
	}

	if ((params_.system == px4::SystemType::ISDB_S) && !(options_ & RECEIVER_SAT_SET_STREAM_ID_BEFORE_TUNE)) {
		ret = SetStreamId();
		if (ret)
			return false;
	}

	if (options_ & RECEIVER_WAIT_AFTER_LOCK) {
		if (isdbc && std::chrono::steady_clock::now() + std::chrono::milliseconds(200) >= tune_deadline)
			return false;
		Sleep(200);
	}

	return !isdbc || std::chrono::steady_clock::now() < tune_deadline;
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
	: input_packet_count_(0),
	rejected_bytes_(0),
	rejected_writes_(0),
	purge_write_discarded_bytes_(0),
	purge_requests_(0),
	write_size_(0),
	threshold_size_(0),
	discard_null_packets_on_output_(false),
	discard_tsmf_header_packets_on_isdbc_output_(false),
	tsmf_enabled_(false),
	tsmf_output_tlv_(false),
	tsmf_logged_header_(false),
	tsmf_logged_tlv_type_mismatch_(false),
	tsmf_relative_stream_(0),
	tsmf_tsid_(-1),
	tsmf_slot_offset_(0),
	tsmf_slot_(-1),
	tsmf_stream_enabled_{},
	tsmf_stream_type_{},
	tsmf_slot_stream_{},
	tsmf_tlv_buf_{},
	tsmf_tlv_output_buf_{},
	tsmf_tlv_size_(0),
	tsmf_tlv_output_count_(0),
	tsmf_tlv_output_bytes_(0),
	tsmf_input_packets_(0),
	tsmf_selected_packets_(0),
	tsmf_rejected_packets_(0),
	tsmf_header_packets_(0),
	tsmf_tei_packets_(0),
	tsmf_null_packets_(0),
	tsmf_discontinuity_indicators_(0),
	tsmf_cc_gaps_(0),
	tsmf_last_cc_{},
	tsmf_cc_valid_{}
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
	input_packet_count_ = 0;
	rejected_bytes_ = 0;
	rejected_writes_ = 0;
	purge_write_discarded_bytes_ = 0;
	purge_requests_ = 0;
	{
		std::lock_guard<std::mutex> lock(tsmf_mtx_);
		tsmf_slot_ = -1;
		if (tsmf_tsid_ >= 0)
			tsmf_relative_stream_ = 15;
		tsmf_logged_header_ = false;
		tsmf_logged_tlv_type_mismatch_ = false;
		tsmf_tlv_size_ = 0;
		tsmf_tlv_output_count_ = 0;
		tsmf_tlv_output_bytes_ = 0;
	}

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

void ReceiverBase::StreamBuffer::SetDiscardNullPacketsOnOutput(bool enabled) noexcept
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);
	discard_null_packets_on_output_ = enabled;
}

void ReceiverBase::StreamBuffer::SetDiscardTsmfHeaderPacketsOnIsdbCOutput(bool enabled) noexcept
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);
	discard_tsmf_header_packets_on_isdbc_output_ = enabled;
}

void ReceiverBase::StreamBuffer::SetTsmfRelativeStream(bool enabled, std::uint8_t stream, std::int32_t slot_offset, bool output_tlv, std::int32_t tsid) noexcept
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);

	tsmf_enabled_ = enabled;
	tsmf_output_tlv_ = enabled && output_tlv;
	tsmf_tsid_ = tsid;
	tsmf_relative_stream_ = (tsid >= 0) ? 15 : stream;
	tsmf_slot_offset_ = slot_offset;
	tsmf_slot_ = -1;
	tsmf_logged_header_ = false;
	tsmf_logged_tlv_type_mismatch_ = false;
	tsmf_stream_enabled_.fill(0);
	tsmf_stream_type_.fill(0xff);
	tsmf_slot_stream_.fill(0);
	tsmf_tlv_size_ = 0;
	tsmf_tlv_output_count_ = 0;
	tsmf_tlv_output_bytes_ = 0;
	tsmf_input_packets_ = 0;
	tsmf_selected_packets_ = 0;
	tsmf_rejected_packets_ = 0;
	tsmf_header_packets_ = 0;
	tsmf_tei_packets_ = 0;
	tsmf_null_packets_ = 0;
	tsmf_discontinuity_indicators_ = 0;
	tsmf_cc_gaps_ = 0;
	tsmf_last_cc_.fill(0);
	tsmf_cc_valid_.fill(0);
}

void ReceiverBase::StreamBuffer::ResetInputPacketCount() noexcept
{
	input_packet_count_.store(0, std::memory_order_relaxed);
}

std::uint64_t ReceiverBase::StreamBuffer::GetInputPacketCount() const noexcept
{
	return input_packet_count_.load(std::memory_order_relaxed);
}

bool ReceiverBase::StreamBuffer::IsTsmfStreamReady() noexcept
{
	std::lock_guard<std::mutex> lock(tsmf_mtx_);
	return tsmf_header_packets_ != 0 && tsmf_relative_stream_ < tsmf_stream_enabled_.size() &&
		tsmf_stream_enabled_[tsmf_relative_stream_] != 0;
}

ReceiverBase::StreamBuffer::WriteDiagnostics ReceiverBase::StreamBuffer::GetWriteDiagnostics() const noexcept
{
	return { rejected_bytes_.load(std::memory_order_relaxed),
		rejected_writes_.load(std::memory_order_relaxed),
		purge_write_discarded_bytes_.load(std::memory_order_relaxed),
		purge_requests_.load(std::memory_order_relaxed) };
}

bool ReceiverBase::StreamBuffer::WriteRingBuffer(const void *buf, std::size_t &size) noexcept
{
	const auto requested = size;
	const bool ret = ringbuf_.Write(buf, size);
	// PID選別による除去と区別し、満杯・非稼働での拒否とパージ中の破棄を残す。
	if (size < requested) {
		if (!ret) {
			rejected_bytes_.fetch_add(requested - size, std::memory_order_relaxed);
			rejected_writes_.fetch_add(1, std::memory_order_relaxed);
		} else {
			purge_write_discarded_bytes_.fetch_add(requested - size, std::memory_order_relaxed);
		}
	}
	return ret;
}

bool ReceiverBase::StreamBuffer::Write(const void *buf, std::size_t &size) noexcept
{
	/* Count raw input before any TSMF/null/header filtering. */
	if (size == 188 && buf && static_cast<const std::uint8_t *>(buf)[0] == 0x47)
		input_packet_count_.fetch_add(1, std::memory_order_relaxed);

	{
		std::lock_guard<std::mutex> lock(tsmf_mtx_);
		if (tsmf_enabled_) {
			if (size != 188) {
				size = 0;
				return true;
			}

			const auto *packet = static_cast<const std::uint8_t *>(buf);
			tsmf_input_packets_++;
			if (!TsmfSelectPacket(packet)) {
				tsmf_rejected_packets_++;
				if ((tsmf_input_packets_ % 100000) == 0) {
					msg_info("TSMF stats: relative=%u offset=%d input=%llu selected=%llu rejected=%llu headers=%llu tei=%llu null=%llu discontinuity=%llu cc_gaps=%llu\n",
						static_cast<unsigned int>(tsmf_relative_stream_),
						tsmf_slot_offset_,
						static_cast<unsigned long long>(tsmf_input_packets_),
						static_cast<unsigned long long>(tsmf_selected_packets_),
						static_cast<unsigned long long>(tsmf_rejected_packets_),
						static_cast<unsigned long long>(tsmf_header_packets_),
						static_cast<unsigned long long>(tsmf_tei_packets_),
						static_cast<unsigned long long>(tsmf_null_packets_),
						static_cast<unsigned long long>(tsmf_discontinuity_indicators_),
						static_cast<unsigned long long>(tsmf_cc_gaps_));
				}
				size = 0;
				return true;
			}

			tsmf_selected_packets_++;
			if (packet[1] & 0x80)
				tsmf_tei_packets_++;
			/* This is an output-side continuity check.  Null packets have no
			 * meaningful continuity, and an adaptation-field discontinuity
			 * indicator explicitly resets the counter for its PID. */
			const std::uint16_t pid =
				(static_cast<std::uint16_t>(packet[1] & 0x1f) << 8) | packet[2];
			const std::uint8_t adaptation_control = packet[3] & 0x30;
			bool discontinuity = false;
			if ((adaptation_control & 0x20) != 0 && packet[4] > 0 && packet[4] <= 183)
				discontinuity = (packet[5] & 0x80) != 0;
			if (pid == 0x1fff) {
				tsmf_null_packets_++;
			} else if (discontinuity) {
				tsmf_discontinuity_indicators_++;
				tsmf_cc_valid_[pid] = 0;
			} else if ((adaptation_control & 0x10) != 0) {
				const std::uint8_t cc = packet[3] & 0x0f;
				if (tsmf_cc_valid_[pid] && cc != ((tsmf_last_cc_[pid] + 1) & 0x0f))
					tsmf_cc_gaps_++;
				tsmf_last_cc_[pid] = cc;
				tsmf_cc_valid_[pid] = 1;
			}

			if ((tsmf_input_packets_ % 100000) == 0) {
				msg_info("TSMF stats: relative=%u offset=%d input=%llu selected=%llu rejected=%llu headers=%llu tei=%llu null=%llu discontinuity=%llu cc_gaps=%llu\n",
					static_cast<unsigned int>(tsmf_relative_stream_),
					tsmf_slot_offset_,
					static_cast<unsigned long long>(tsmf_input_packets_),
					static_cast<unsigned long long>(tsmf_selected_packets_),
					static_cast<unsigned long long>(tsmf_rejected_packets_),
					static_cast<unsigned long long>(tsmf_header_packets_),
					static_cast<unsigned long long>(tsmf_tei_packets_),
					static_cast<unsigned long long>(tsmf_null_packets_),
					static_cast<unsigned long long>(tsmf_discontinuity_indicators_),
					static_cast<unsigned long long>(tsmf_cc_gaps_));
			}

			/* A segmented TLV stream is transported in TSMF slots using the
			 * special PID 0x002d layout.  It is not MPEG-TS; reconstruct its
			 * raw byte stream for BonDriver_dantto4k instead of forwarding the
			 * 188-byte container packet. */
			if (tsmf_output_tlv_) {
				const std::size_t stream_index = tsmf_relative_stream_;
				if (stream_index >= tsmf_stream_type_.size() || tsmf_stream_type_[stream_index] != 0) {
					if (!tsmf_logged_tlv_type_mismatch_) {
						msg_warn("TSMF raw TLV output requested for relative stream=%u, but header stream_type=%u (expected 0)\n",
							static_cast<unsigned int>(tsmf_relative_stream_),
							stream_index < tsmf_stream_type_.size() ? tsmf_stream_type_[stream_index] : 0xff);
						tsmf_logged_tlv_type_mismatch_ = true;
					}
					size = 0;
					return true;
				}

				std::size_t output_size = 0;
				if (!TsmfExtractTlvPacket(packet, output_size)) {
					size = 0;
					return true;
				}
				if (!output_size) {
					size = 0;
					return true;
				}

				const bool ret = WriteRingBuffer(tsmf_tlv_output_buf_.data(), output_size);
				write_size_ += output_size;
				tsmf_tlv_output_count_++;
				tsmf_tlv_output_bytes_ += output_size;
				if (tsmf_tlv_output_count_ == 1 || (tsmf_tlv_output_count_ % 256) == 0)
					msg_info("TSMF raw TLV output: fragments=%llu, bytes=%llu (last=%zu)\n",
						static_cast<unsigned long long>(tsmf_tlv_output_count_),
						static_cast<unsigned long long>(tsmf_tlv_output_bytes_), output_size);
				size = output_size;
				return ret;
			}
		}

		/* For TSMF, this point is reached only after a packet has survived
		 * the 52-slot selector.  For ordinary TS, it is the direct output
		 * path.  In both cases PID 0x1fff is now safe padding to discard. */
		if ((discard_null_packets_on_output_ ||
			discard_tsmf_header_packets_on_isdbc_output_) && size == 188) {
			const auto *packet = static_cast<const std::uint8_t *>(buf);
			const std::uint16_t pid =
				(static_cast<std::uint16_t>(packet[1] & 0x1f) << 8) | packet[2];
			if (packet[0] == 0x47 &&
				((discard_null_packets_on_output_ && pid == 0x1fff) ||
				 (discard_tsmf_header_packets_on_isdbc_output_ && pid == 0x002f))) {
				size = 0;
				return true;
			}
		}
	}

	bool ret;

	ret = WriteRingBuffer(buf, size);
	write_size_ += size;

	return ret;
}

bool ReceiverBase::StreamBuffer::TsmfParseHeader(const std::uint8_t *packet) noexcept
{
	const std::uint16_t pid = (static_cast<std::uint16_t>(packet[1]) << 8) | packet[2];
	const std::uint16_t frame_sync = (static_cast<std::uint16_t>(packet[4]) << 8) | packet[5];

	const std::uint8_t frame_type = packet[6] & 0x0f;
	if (packet[0] != 0x47 || pid != 0x002f || (packet[3] & 0xf0) != 0x10 ||
		((frame_sync & 0x1fff) != 0x1a86 && (frame_sync & 0x1fff) != 0x0579) ||
		(packet[6] & 0x10) != 0 || (frame_type != 0x01 && frame_type != 0x02) ||
		mpeg_crc32(&packet[4], 184) != 0)
		return false;

	for (std::size_t i = 0; i < tsmf_stream_enabled_.size(); i++)
		tsmf_stream_enabled_[i] = packet[7 + (i / 8)] & (0x80 >> (i % 8));
	for (std::size_t i = 0; i < tsmf_stream_type_.size(); i++)
		tsmf_stream_type_[i] = (packet[125 + (i / 8)] & (0x80 >> (i % 8))) ? 1 : 0;
	for (std::size_t i = 0; i < tsmf_slot_stream_.size(); i++) {
		const std::uint8_t slot = packet[73 + (i / 2)];
		tsmf_slot_stream_[i] = (i & 1) ? (slot & 0x0f) : (slot >> 4);
	}
	if (tsmf_tsid_ >= 0) {
		std::uint8_t relative = 15;
		for (std::size_t i = 0; i < tsmf_stream_enabled_.size(); i++) {
			const std::uint16_t tsid = (static_cast<std::uint16_t>(packet[9 + i * 4]) << 8) | packet[10 + i * 4];
			if (tsid == tsmf_tsid_) {
				relative = static_cast<std::uint8_t>(i);
				break;
			}
		}
		// 相対 TS 番号の変更前後では、TLV の断片を連結できない
		if (relative != tsmf_relative_stream_)
			tsmf_tlv_size_ = 0;
		tsmf_relative_stream_ = relative;
	}

	tsmf_slot_ = 0;
	tsmf_header_packets_++;
	if (!tsmf_logged_header_) {
		std::uint16_t enabled_mask = 0;
		char slot_map[53];
		for (std::size_t i = 0; i < tsmf_stream_enabled_.size(); i++)
			enabled_mask |= tsmf_stream_enabled_[i] ? (1u << i) : 0;
		for (std::size_t i = 0; i < tsmf_slot_stream_.size(); i++)
			slot_map[i] = "0123456789ABCDEF"[tsmf_slot_stream_[i] & 0x0f];
		slot_map[tsmf_slot_stream_.size()] = '\0';
		msg_info("TSMF header detected: relative stream=%u, slot offset=%d, enabled=0x%04x, slots=%s\n",
			static_cast<unsigned int>(tsmf_relative_stream_), tsmf_slot_offset_, enabled_mask, slot_map);
		if (tsmf_output_tlv_ && tsmf_relative_stream_ < tsmf_stream_type_.size())
			msg_info("TSMF raw TLV mode: relative stream=%u, header stream_type=%u (TLV requires 0)\n",
				static_cast<unsigned int>(tsmf_relative_stream_), tsmf_stream_type_[tsmf_relative_stream_]);
		tsmf_logged_header_ = true;
	}
	return true;
}

bool ReceiverBase::StreamBuffer::TsmfAppendTlv(const std::uint8_t *data, std::size_t size) noexcept
{
	if (size > TSMF_TLV_MAX_SIZE - tsmf_tlv_size_) {
		msg_warn("TSMF TLV packet exceeded 65536 bytes; discarding the incomplete packet\n");
		tsmf_tlv_size_ = 0;
		return false;
	}
	memcpy(tsmf_tlv_buf_.data() + tsmf_tlv_size_, data, size);
	tsmf_tlv_size_ += size;
	return true;
}

bool ReceiverBase::StreamBuffer::TsmfExtractTlvPacket(const std::uint8_t *packet, std::size_t &output_size) noexcept
{
	output_size = 0;
	const std::uint16_t pid =
		(static_cast<std::uint16_t>(packet[1] & 0x1f) << 8) | packet[2];
	/* This deliberately mirrors the segmented-TLV layout used by
	 * BonDriver_BDA: a three-byte prefix for continuations, and a pointer
	 * byte at offset three on a packet-start indication. */
	if (packet[0] != 0x47 || (packet[1] & 0xa0) != 0 || pid != 0x002d) {
		if (packet[1] & 0x80)
			tsmf_tlv_size_ = 0;
		return false;
	}

	const bool packet_start = (packet[1] & 0x40) != 0;
	if (!packet_start) {
		if (tsmf_tlv_size_)
			return TsmfAppendTlv(packet + 3, 185);
		return false;
	}

	const std::size_t tlv_start = static_cast<std::size_t>(packet[3]) + 4;
	if (tlv_start >= 188) {
		tsmf_tlv_size_ = 0;
		return false;
	}
	if (tsmf_tlv_size_) {
		if (!TsmfAppendTlv(packet + 4, tlv_start - 4))
			return false;
		output_size = tsmf_tlv_size_;
		memcpy(tsmf_tlv_output_buf_.data(), tsmf_tlv_buf_.data(), output_size);
	}
	/* The current packet begins the next TLV packet.  Keep it until the
	 * next start indication supplies its final fragment. */
	tsmf_tlv_size_ = 0;
	return TsmfAppendTlv(packet + tlv_start, 188 - tlv_start);
}

bool ReceiverBase::StreamBuffer::TsmfSelectPacket(const std::uint8_t *packet) noexcept
{
	if (TsmfParseHeader(packet))
		return false;

	if (packet[0] != 0x47 || tsmf_slot_ < 0 || tsmf_slot_ >= 52) {
		tsmf_slot_ = -1;
		if (tsmf_tsid_ >= 0)
			tsmf_relative_stream_ = 15;
		tsmf_tlv_size_ = 0;
		return false;
	}

	tsmf_slot_++;
	const std::uint16_t stream = static_cast<std::uint16_t>(tsmf_relative_stream_) + 1;
	int slot = (tsmf_slot_ - 1 + tsmf_slot_offset_) % 52;
	if (slot < 0)
		slot += 52;
	return stream <= 15 && tsmf_stream_enabled_[stream - 1] &&
		tsmf_slot_stream_[slot] == stream;
}

void ReceiverBase::StreamBuffer::NotifyWrite() noexcept
{
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
	purge_requests_.fetch_add(1, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> lock(tsmf_mtx_);
		tsmf_slot_ = -1;
		if (tsmf_tsid_ >= 0)
			tsmf_relative_stream_ = 15;
		tsmf_tlv_size_ = 0;
	}
	return ringbuf_.Purge();
}

} // namespace px4
