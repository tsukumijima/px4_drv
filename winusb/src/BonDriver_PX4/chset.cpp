// chset.cpp

#include "chset.hpp"

#include <mutex>
#include <fstream>
#include <cerrno>
#include <cctype>
#include <cstdlib>

#include "util.hpp"

namespace px4 {

static bool ParseInteger(const char *str, long long &value) noexcept
{
	char *end;
	errno = 0;
	value = std::strtoll(str, &end, 10);
	const bool converted = end != str;
	while (std::isspace(static_cast<unsigned char>(*end)))
		end++;
	return errno != ERANGE && converted && *end == '\0';
}

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

		// 空行をチャンネル0として読むと、先頭チャンネルが空の項目で上書きされる
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
			const char *tsid_str = tmp_head;
			ch.tsid = static_cast<std::uint16_t>(px4::util::atoui32(tmp_head));

			// 第6列は中心周波数 (kHz) の直接指定
			if (tmp_tail) {
				Split(&tmp_head, &tmp_tail, '\t');
				ch.freq_khz = px4::util::atoui32(tmp_head);

				// 第7列は符号付きの周波数補正値 (kHz)
				if (tmp_tail) {
					Split(&tmp_head, &tmp_tail, '\t');
					ch.isdbc_freq_offset_khz = px4::util::atoi32(tmp_head);
					ch.has_isdbc_freq_offset = true;

					// 第8列は相対 TS 番号 (0〜14)、TSID 指定 (-1)、通常 TS 用の変調方式 (64/256)
					if (tmp_tail) {
						Split(&tmp_head, &tmp_tail, '\t');
						long long field8;
						if (system == px4::SystemType::ISDB_C) {
							if (!ParseInteger(tmp_head, field8) || field8 < -1 ||
								(field8 > 14 && field8 != 64 && field8 != 256)) {
								if (strict_)
									return false;
								continue;
							}
						} else {
							field8 = px4::util::atoui32(tmp_head);
						}
						if (field8 == 64 || field8 == 256) {
							ch.isdbc_modulation = static_cast<std::uint16_t>(field8);
						} else {
							if (field8 == -1) {
								long long tsid;
								if (!ParseInteger(tsid_str, tsid) || tsid < 0 || tsid > 65535) {
									if (strict_)
										return false;
									continue;
								}
								ch.tsid = static_cast<std::uint16_t>(tsid);
								ch.tsmf_by_tsid = true;
							} else {
								ch.tsmf_relative_stream = static_cast<std::uint8_t>(field8);
							}
							ch.is_tsmf = true;

							// 第9列はスロット番号へ加えるオフセット
							if (tmp_tail) {
								Split(&tmp_head, &tmp_tail, '\t');
								if (system == px4::SystemType::ISDB_C) {
									long long value;
									if (!ParseInteger(tmp_head, value) || value < -51 || value > 51) {
										if (strict_)
											return false;
										continue;
									}
									ch.tsmf_slot_offset = static_cast<std::int32_t>(value);
								} else {
									ch.tsmf_slot_offset = px4::util::atoi32(tmp_head);
								}

								// 第10列は TSMF 用の変調方式
								if (tmp_tail) {
									Split(&tmp_head, &tmp_tail, '\t');
									std::uint32_t modulation;
									if (system == px4::SystemType::ISDB_C) {
										long long value;
										if (!ParseInteger(tmp_head, value) ||
											(value != 0 && value != 64 && value != 256)) {
											if (strict_)
												return false;
											continue;
										}
										modulation = static_cast<std::uint32_t>(value);
									} else {
										modulation = px4::util::atoui32(tmp_head);
									}
									if (modulation == 64 || modulation == 256)
										ch.isdbc_modulation = static_cast<std::uint16_t>(modulation);

									// 第11列が1なら生 TLV、それ以外は MPEG-TS を出力
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
