// 一時ファイルに書いた行と配布の ChSet で、ISDB-C の ChSet の列の解釈と不正な行の扱いを検証する

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <windows.h>

#include "chset.hpp"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace {

std::wstring WriteTemp(const char *body)
{
	wchar_t dir[MAX_PATH], path[MAX_PATH];

	CHECK(GetTempPathW(MAX_PATH, dir) != 0);
	CHECK(GetTempFileNameW(dir, L"chs", 0, path) != 0);

	std::ofstream ofs(path, std::ios::binary);
	ofs << body;
	return path;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	// 列の数と、第4列の物理チャンネル番号と J.83 Annex B の中心周波数の境界
	const char *body =
		"$CATV\t0\r\n"
		"ch0\t0\t0\t0\t1\r\n"
		"ch112\t0\t1\t112\t15\r\n"
		"ch113\t0\t2\t113\t1\r\n"
		"f89999\t0\t3\t89999\t1\r\n"
		"f90000\t0\t4\t90000\t1\r\n"
		"f770000\t0\t5\t770000\t1\r\n"
		"f770001\t0\t6\t770001\t1\r\n"
		"j83b_ts2\t0\t7\t93250\t2\r\n"
		"ts16\t0\t8\t51\t16\r\n"
		"ts0\t0\t9\t51\t0\r\n"
		"extra\t0\t10\t51\t1\t0\r\n"
		"short\t0\t11\t51\r\n"
		"suffix\t0\t12\t51x\t1\r\n"
		"h01\t0\t13\tH01\t1\r\n";
	std::wstring path = WriteTemp(body);

	px4::ChannelSet loose;
	CHECK(loose.Load(path, px4::SystemType::ISDB_C));
	CHECK(loose.GetSpaceSystem(0) == px4::SystemType::ISDB_C);

	const auto &ch0 = loose.GetChannel(0, 0);
	CHECK(!ch0.j83b && ch0.ptx_ch == 0 && ch0.tsmf_stream == 1);
	const auto &ch112 = loose.GetChannel(0, 1);
	CHECK(!ch112.j83b && ch112.ptx_ch == 112 && ch112.tsmf_stream == 15);
	const auto &f90000 = loose.GetChannel(0, 4);
	CHECK(f90000.j83b && f90000.freq == 90000 && f90000.tsmf_stream == 1);
	const auto &f770000 = loose.GetChannel(0, 5);
	CHECK(f770000.j83b && f770000.freq == 770000);

	// 不正な行は、厳しくない読み込みでは読み飛ばされる
	for (std::uint32_t id : { 2u, 3u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u })
		CHECK(!loose.ExistsChannel(0, id));

	// 厳しい読み込みでは、不正な行が1つでもあれば失敗する
	px4::ChannelSet strict(true);
	CHECK(!strict.Load(path, px4::SystemType::ISDB_C));
	DeleteFileW(path.c_str());
	std::puts("ISDB-C ChSet columns and boundaries: PASS");

	// 配布の ChSet は、すべての行が厳しい読み込みで読める
	if (argc >= 2) {
		px4::ChannelSet dist(true);
		std::uint32_t isdbc = 0, j83b = 0;

		CHECK(dist.Load(argv[1], px4::SystemType::ISDB_C));
		while (dist.ExistsChannel(0, isdbc))
			isdbc++;
		while (dist.ExistsChannel(1, j83b))
			j83b++;
		CHECK(isdbc == 113 * 4 && j83b == 77 + 5);

		// C52 は物理チャンネル番号 51 で、1〜4本目の行が続く
		const auto &c52_ts2 = dist.GetChannel(0, 51 * 4 + 1);
		CHECK(!c52_ts2.j83b && c52_ts2.ptx_ch == 51 && c52_ts2.tsmf_stream == 2);
		const auto &h01 = dist.GetChannel(1, 0);
		CHECK(h01.j83b && h01.freq == 93250);
		// H11 は 6.5MHz 刻みの 158.25MHz と、1MHz ずれた 159.25MHz の2行
		CHECK(dist.GetChannel(1, 10).freq == 158250 && dist.GetChannel(1, 11).freq == 159250);
		CHECK(dist.GetChannel(1, j83b - 1).freq == 587250);
		std::puts("Distributed ISDB-C ChSet: PASS");
	}

	return 0;
}
