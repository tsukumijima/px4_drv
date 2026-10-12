#pragma once

#include <cstddef>

namespace px4 {

// コピー型APIには宛先容量がないため、既知のクライアントの確保量内に制限する。
constexpr std::size_t BON_TS_COPY_SIZE = 188 * 256;

template<class Queue>
bool ReadBonTsCopy(Queue &queue, void *destination, std::size_t &size, std::size_t &remain)
{
	size = BON_TS_COPY_SIZE;
	remain = 0;
	return queue.Read(destination, size, remain, false);
}

} // namespace px4
