// chset.cpp

#include "chset.hpp"

#include <mutex>
#include <fstream>
#include <cstdlib>
#include <cstring>

#include "util.hpp"

namespace px4 {

bool ChannelSet::Load(const std::wstring &path, px4::SystemType system) noexcept
{
	std::lock_guard<std::shared_mutex> lock(mtx_);
	std::ifstream ifs(path);

	if (!ifs.is_open())
		return false;

	std::unordered_map<std::uint32_t, std::uint32_t> space_id_table;

	while (true) {
		char line[256];
		std::streamsize gcount;
		char *tmp_head, *tmp_tail = nullptr;

		if (!ifs.getline(line, 256))
			break;

		gcount = ifs.gcount();
		if (!gcount)
			continue;

		if (line[gcount] == '\r') {
			// CRLF
			line[gcount] = '\0';
			gcount--;
		}

		if (line[0] == ';') {
			// comment
			continue;
		}

		if (line[0] == '$') {
			// tuning space
			std::wstring name;
			std::uint32_t space_id;

			tmp_head = line + 1;

			// name
			Split(&tmp_head, &tmp_tail, '\t');
			px4::util::ShiftJisToUtf16(tmp_head, name);

			// space
			Split(&tmp_head, &tmp_tail, '\t');
			space_id = px4::util::atoui32(tmp_head);

			space_id_table.emplace(space_id, next_space_id_);

			if (!AddSpace(next_space_id_++, &name, system))
				return false;
		} else {
			// channel
			std::uint32_t space_id, ch_id;
			ChannelInfo ch;

			tmp_head = line;

			// name
			Split(&tmp_head, &tmp_tail, '\t');
			px4::util::ShiftJisToUtf16(tmp_head, ch.name);

			// space
			Split(&tmp_head, &tmp_tail, '\t');
			space_id = px4::util::atoui32(tmp_head);

			try {
				space_id = space_id_table.at(space_id);
			} catch (const std::out_of_range&) {
				if (strict_)
					return false;
				else
					continue;
			}

			// ch
			Split(&tmp_head, &tmp_tail, '\t');
			ch_id = px4::util::atoui32(tmp_head);

			// ptx_ch
			Split(&tmp_head, &tmp_tail, '\t');
			ch.ptx_ch = px4::util::atoui32(tmp_head);

			// tsid
			Split(&tmp_head, &tmp_tail, '\t');
			ch.tsid = static_cast<std::uint16_t>(px4::util::atoui32(tmp_head));

			if (system == px4::SystemType::ISDB_C && !ParseIsdbC(&tmp_head, &tmp_tail, ch)) {
				if (strict_)
					return false;
				else
					continue;
			}

			if (!AddChannel(space_id, ch_id, ch))
				return false;
		}
	}

	return true;
}

bool ChannelSet::Merge(ChannelSet &chset) noexcept
{
	// TODO: Space ID をずらす
	try {
		for (auto it = chset.spaces_.cbegin(); it != chset.spaces_.cend(); ++it)
			spaces_.emplace_back(*it);

		return true;
	} catch (...) {
		return false;
	}
}

bool ChannelSet::ExistsSpace(std::uint32_t space_id) const noexcept
{
	std::shared_lock<std::shared_mutex> lock(mtx_);

	return (spaces_.size() > space_id);
}

const std::wstring& ChannelSet::GetSpaceName(std::uint32_t space_id) const
{
	std::shared_lock<std::shared_mutex> lock(mtx_);

	return spaces_.at(space_id).name;
}

px4::SystemType ChannelSet::GetSpaceSystem(std::uint32_t space_id) const
{
	std::shared_lock<std::shared_mutex> lock(mtx_);

	return spaces_.at(space_id).system;
}

bool ChannelSet::ExistsChannel(std::uint32_t space_id, std::uint32_t ch_id) const noexcept
{
	std::shared_lock<std::shared_mutex> lock(mtx_);

	try {
		return !!spaces_.at(space_id).channels.count(ch_id);
	} catch (const std::out_of_range&) {
		return false;
	}
}

const px4::ChannelSet::ChannelInfo& ChannelSet::GetChannel(std::uint32_t space_id, std::uint32_t ch_id) const
{
	std::shared_lock<std::shared_mutex> lock(mtx_);

	return spaces_.at(space_id).channels.at(ch_id);
}

bool ChannelSet::AddSpace(std::uint32_t space_id, const std::wstring *name, px4::SystemType system) noexcept
{
	if (spaces_.size() > space_id) {
		auto& space = spaces_.at(space_id);

		if (name && space.name.empty())
			space.name = *name;

		if (space.system == px4::SystemType::UNSPECIFIED)
			space.system = system;
	} else if (name) {
		spaces_.emplace_back(SpaceInfo(*name, system));
	} else if (!strict_) {
		spaces_.emplace_back(SpaceInfo(system));
	} else {
		return false;
	}

	return true;
}

bool ChannelSet::AddChannel(std::uint32_t space_id, std::uint32_t ch_id, const ChannelInfo &ch) noexcept
{
	if (!AddSpace(space_id, nullptr))
		return false;

	auto& space = spaces_.at(space_id);

	if (space.channels.count(ch_id))
		space.channels.at(ch_id) = ch;
	else
		space.channels.emplace(ch_id, ch);

	return true;
}

void ChannelSet::Split(char **head, char **tail, char c) noexcept
{
	char *h = *head, *t = *tail;

	if (t)
		h = t + 1;

	t = std::strchr(h, c);
	if (t)
		*t = '\0';

	*head = h;
	*tail = t;

	return;
}

bool ChannelSet::ParseIsdbC(char **head, char **tail, ChannelInfo &ch) noexcept
{
	auto next = [head, tail](long min, long max, long &value) noexcept {
		char *end;

		Split(head, tail, '\t');
		value = std::strtol(*head, &end, 10);
		return end != *head && *end == '\0' && value >= min && value <= max;
	};
	const char *tsid = *head;
	char *end;
	long value;

	// 方式 (0 は J.83 Annex C、J83B は J.83 Annex B)
	if (!*tail)
		return true;
	if (!next(0, 0, value)) {
		if (std::strcmp(*head, "J83B"))
			return false;
		ch.j83b = true;
	}

	// TSMF で多重された TS の選択 (0-14 は相対 TS 番号、-1 は第5列の TSID)
	if (!*tail)
		return true;
	if (ch.j83b || !next(-1, 14, value))
		return false;
	if (value < 0) {
		// 第5列は既存の読み方では16ビットへ丸められるため、TSID で選ぶ行だけ範囲を検査し直す
		value = std::strtol(tsid, &end, 10);
		if (end == tsid || *end != '\0' || value < 0 || value > 0xffff)
			return false;
		ch.tsmf = TsmfSelection::TSID;
	} else {
		ch.tsmf = TsmfSelection::RELATIVE_STREAM;
		ch.tsmf_relative_stream = static_cast<std::uint8_t>(value);
	}

	// 1 なら TSMF の TS を BonDriver_dantto4k 向けの生 TLV で出力する
	if (!*tail)
		return true;
	if (!next(0, 1, value))
		return false;
	ch.tsmf_output_tlv = (value != 0);

	return true;
}

} // namespace px4
