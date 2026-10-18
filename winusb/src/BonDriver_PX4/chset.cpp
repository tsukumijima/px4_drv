// chset.cpp

#include "chset.hpp"

#include <mutex>
#include <fstream>

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
		char *line_head, *tmp_head, *tmp_tail = nullptr;

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

		// Empty separator lines must not be interpreted as channel 0.  In
		// particular, a blank line after a CATV space used to replace C13 (the
		// first channel) with an empty entry.
		line_head = line;
		while (*line_head == ' ' || *line_head == '\t')
			line_head++;

		if (*line_head == '\0' || *line_head == ';') {
			// comment
			continue;
		}

		if (*line_head == '$') {
			// tuning space
			std::wstring name;
			std::uint32_t space_id;

			tmp_head = line_head + 1;

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

			tmp_head = line_head;

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

			// optional RF centre frequency in kHz (sixth field)
			if (tmp_tail) {
				Split(&tmp_head, &tmp_tail, '\t');
				ch.freq_khz = px4::util::atoui32(tmp_head);

				// Optional signed ISDB-C correction in kHz (seventh field).
				if (tmp_tail) {
					Split(&tmp_head, &tmp_tail, '\t');
					ch.isdbc_freq_offset_khz = px4::util::atoi32(tmp_head);
					ch.has_isdbc_freq_offset = true;

					// The eighth field is either a TSMF Relative stream number
					// (0-14), or an ISDB-C constellation (64/256).  The latter
					// form lets a regular channel select its QAM profile without
					// dummy TSMF fields.
					if (tmp_tail) {
						Split(&tmp_head, &tmp_tail, '\t');
						const auto field8 = px4::util::atoui32(tmp_head);
						if (field8 == 64 || field8 == 256) {
							ch.isdbc_modulation = static_cast<std::uint16_t>(field8);
						} else {
							ch.tsmf_relative_stream = static_cast<std::uint8_t>(field8);
							ch.is_tsmf = true;

							// Optional signed TSMF slot-map offset (ninth field).
							if (tmp_tail) {
								Split(&tmp_head, &tmp_tail, '\t');
								ch.tsmf_slot_offset = px4::util::atoi32(tmp_head);

								// Optional modulation after the TSMF fields (tenth field).
								if (tmp_tail) {
									Split(&tmp_head, &tmp_tail, '\t');
									const auto modulation = px4::util::atoui32(tmp_head);
									if (modulation == 64 || modulation == 256)
										ch.isdbc_modulation = static_cast<std::uint16_t>(modulation);

									/* Optional eleventh field: raw TLV output for a TSMF
									 * stream.  It is deliberately opt-in so existing TSMF
									 * entries continue returning ordinary MPEG-TS. */
									if (tmp_tail) {
										Split(&tmp_head, &tmp_tail, '\t');
										ch.tsmf_output_tlv = px4::util::atoui32(tmp_head) == 1;
									}
								}
							}
						}
					}
				}
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

} // namespace px4
