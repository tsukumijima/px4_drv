#include <cstdio>

#include "ts_sync.h"

namespace {

bool Check(const char *name, bool actual, bool expected)
{
	if (actual == expected)
		return true;
	std::fprintf(stderr, "%s failed. actual: %d, expected: %d\n",
		name, actual ? 1 : 0, expected ? 1 : 0);
	return false;
}

} // namespace

int main()
{
	bool succeeded = true;
	/* 通常 TS は0x47だけを同期バイトとして受理する */
	succeeded = Check("plain valid", px4_ts_has_plain_sync(0x47) != 0, true) && succeeded;
	succeeded = Check("plain corrupted", px4_ts_has_plain_sync(0x46) != 0, false) && succeeded;

	/* 多重 TS は有効なチューナータグを残し、同期値の破損を拒否する */
	succeeded = Check("tagged receiver 1", px4_ts_has_tagged_sync(0x17) != 0, true) && succeeded;
	succeeded = Check("tagged receiver 4", px4_ts_has_tagged_sync(0x47) != 0, true) && succeeded;
	succeeded = Check("tagged receiver 7", px4_ts_has_tagged_sync(0x77) != 0, true) && succeeded;
	succeeded = Check("tagged corrupted", px4_ts_has_tagged_sync(0x46) != 0, false) && succeeded;
	succeeded = Check("tagged error bit", px4_ts_has_tagged_sync(0x87) != 0, false) && succeeded;

	if (succeeded)
		std::puts("TS sync condition test: passed");
	return succeeded ? 0 : 1;
}
