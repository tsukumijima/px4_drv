// 一時ファイルに書いた行と配布の ChSet で、ISDB-C と J.83B の ChSet の列の解釈と不正な行の扱いを検証する

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <windows.h>

#include "chset.hpp"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace {

std::wstring temp_path;

// CHECK が失敗して exit() で終わっても、一時ファイルを %TEMP% に残さない
void RemoveTemp()
{
	if (!temp_path.empty())
		DeleteFileW(temp_path.c_str());
}

std::wstring WriteTemp(const char *body)
{
	wchar_t dir[MAX_PATH], path[MAX_PATH];

	// 同じ一時ファイルを書き直して使い回し、終了時の削除を1回の登録で済ませる
	if (temp_path.empty()) {
		CHECK(GetTempPathW(MAX_PATH, dir) != 0);
		CHECK(GetTempFileNameW(dir, L"chs", 0, path) != 0);
		temp_path = path;
		std::atexit(RemoveTemp);
	}

	std::ofstream ofs(temp_path, std::ios::binary | std::ios::trunc);
	ofs << body;
	return temp_path;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	// ISDB-C の行は物理チャンネル番号 (0〜112) と TSMF の何本目 (1〜15) の2列で、中心周波数では書けない
	const char *isdbc_body =
		"$CATV\t0\r\n"
		"ch0\t0\t0\t0\t1\r\n"
		"ch112\t0\t1\t112\t15\r\n"
		"ch113\t0\t2\t113\t1\r\n"
		"f93250\t0\t3\t93250\t1\r\n"
		"ts16\t0\t4\t51\t16\r\n"
		"ts0\t0\t5\t51\t0\r\n"
		"extra\t0\t6\t51\t1\t0\r\n"
		"short\t0\t7\t51\r\n"
		"suffix\t0\t8\t51x\t1\r\n"
		"h01\t0\t9\tH01\t1\r\n";
	std::wstring path = WriteTemp(isdbc_body);

	px4::ChannelSet isdbc_loose;
	CHECK(isdbc_loose.Load(path, px4::SystemType::ISDB_C));
	CHECK(isdbc_loose.GetSpaceSystem(0) == px4::SystemType::ISDB_C);

	const auto &ch0 = isdbc_loose.GetChannel(0, 0);
	CHECK(ch0.ptx_ch == 0 && ch0.tsmf_stream == 1);
	const auto &ch112 = isdbc_loose.GetChannel(0, 1);
	CHECK(ch112.ptx_ch == 112 && ch112.tsmf_stream == 15);

	// 不正な行は、厳しくない読み込みでは読み飛ばされる
	for (std::uint32_t id : { 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u })
		CHECK(!isdbc_loose.ExistsChannel(0, id));

	// 厳しい読み込みでは、不正な行が1つでもあれば失敗する
	px4::ChannelSet isdbc_strict(true);
	CHECK(!isdbc_strict.Load(path, px4::SystemType::ISDB_C));
	std::puts("ISDB-C ChSet columns and boundaries: PASS");

	// J.83B の行は中心周波数 (kHz) の1列だけで、TSMF を使わないため後ろに列を持たない
	const char *j83b_body =
		"$J83B\t0\r\n"
		"f90000\t0\t0\t90000\r\n"
		"f770000\t0\t1\t770000\r\n"
		"f89999\t0\t2\t89999\r\n"
		"f770001\t0\t3\t770001\r\n"
		"ch51\t0\t4\t51\r\n"
		"ts1\t0\t5\t93250\t1\r\n"
		"short\t0\t6\r\n";
	path = WriteTemp(j83b_body);

	px4::ChannelSet j83b_loose;
	CHECK(j83b_loose.Load(path, px4::SystemType::J83B));
	CHECK(j83b_loose.GetSpaceSystem(0) == px4::SystemType::J83B);
	CHECK(j83b_loose.GetChannel(0, 0).freq == 90000);
	CHECK(j83b_loose.GetChannel(0, 1).freq == 770000);
	for (std::uint32_t id : { 2u, 3u, 4u, 5u, 6u })
		CHECK(!j83b_loose.ExistsChannel(0, id));

	px4::ChannelSet j83b_strict(true);
	CHECK(!j83b_strict.Load(path, px4::SystemType::J83B));
	std::puts("J.83B ChSet columns and boundaries: PASS");

	// 配布の ChSet は、すべての行が厳しい読み込みで読める
	if (argc >= 3) {
		px4::ChannelSet isdbc_dist(true), j83b_dist(true);
		std::uint32_t isdbc = 0, j83b = 0;

		CHECK(isdbc_dist.Load(argv[1], px4::SystemType::ISDB_C));
		while (isdbc_dist.ExistsChannel(0, isdbc))
			isdbc++;
		CHECK(isdbc == 113 * 4);

		// C52 は物理チャンネル番号 51 で、1〜4本目の行が続く
		const auto &c52_ts2 = isdbc_dist.GetChannel(0, 51 * 4 + 1);
		CHECK(c52_ts2.ptx_ch == 51 && c52_ts2.tsmf_stream == 2);

		CHECK(j83b_dist.Load(argv[2], px4::SystemType::J83B));
		while (j83b_dist.ExistsChannel(0, j83b))
			j83b++;
		CHECK(j83b == 77 + 5);

		CHECK(j83b_dist.GetChannel(0, 0).freq == 93250);
		// H11 は 6.5MHz 刻みの 158.25MHz と、1MHz ずれた 159.25MHz の2行
		CHECK(j83b_dist.GetChannel(0, 10).freq == 158250 && j83b_dist.GetChannel(0, 11).freq == 159250);
		CHECK(j83b_dist.GetChannel(0, j83b - 1).freq == 587250);
		std::puts("Distributed ISDB-C and J.83B ChSet: PASS");
	}

	return 0;
}
