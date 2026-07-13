#pragma once

#include <cstdint>

namespace px4::card_command {

constexpr std::uint32_t VERSION = 0x00020001U;
constexpr std::size_t MAX_READERS = 16;
constexpr std::size_t MAX_READER_NAME = 128;
constexpr std::size_t MAX_ATR_SIZE = 33;
constexpr std::size_t MAX_DATA_SIZE = 512;

enum class Code : std::uint32_t {
	UNDEFINED = 0,
	GET_VERSION,
	LIST_READERS,
	CONNECT,
	RECONNECT,
	DISCONNECT,
	STATUS,
	RESET,
	TRANSMIT,
	BEGIN_TRANSACTION,
	END_TRANSACTION,
};

enum class Status : std::uint32_t {
	REQUEST = 0,
	SUCCEEDED,
	FAILED,
};

enum class ShareMode : std::uint32_t {
	UNDEFINED = 0,
	SHARED,
	EXCLUSIVE,
};

enum class Disposition : std::uint32_t {
	LEAVE = 0,
	RESET,
};

enum class Error : std::uint32_t {
	NONE = 0,
	INVALID_PARAMETER,
	NO_READER,
	NO_CARD,
	BUSY,
	REMOVED,
	TIMEOUT,
	PROTOCOL,
	INSUFFICIENT_BUFFER,
	INTERNAL,
};

struct Command final {
	Code code;
	Status status;
	Error error;
	std::uint32_t version;
	std::uint32_t reader_count;
	std::uint64_t reader_generation;
	wchar_t readers[MAX_READERS][MAX_READER_NAME];
	wchar_t reader_name[MAX_READER_NAME];
	std::uint32_t share_mode;
	std::uint32_t disposition;
	bool card_present;
	bool card_initialized;
	std::uint8_t atr[MAX_ATR_SIZE];
	std::uint32_t atr_length;
	std::uint8_t data[MAX_DATA_SIZE];
	std::uint32_t data_length;
	std::uint32_t data_capacity;
};

} // namespace px4::card_command
