// SPDX-License-Identifier: GPL-2.0-only
/* 模擬 I2C で ISDB-C・J.83B の設定・停止と失敗時の復帰を検証する
 * 信号ロック、USB 負荷、実際の TS 品質はこの試験の確認範囲外
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cxd2856er.h"
#include "cxd2858er.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

struct mock_bus {
	u8 bank[128];
	u8 regs[3][256][256];
	unsigned int calls;
	unsigned int fail_at;
	bool frozen;
	bool gate_open;
};

static int gate(void *priv, bool open)
{
	((struct mock_bus *)priv)->gate_open = open;
	return 0;
}

static int request(void *priv, const struct i2c_comm_request *req, int num)
{
	struct mock_bus *bus = priv;
	int n, i;

	bus->calls++;
	if (bus->calls == bus->fail_at)
		return -EIO;
	for (n = 0; n < num; n++) {
		int addr = req[n].addr;
		int chip = addr == 0x64 ? 0 : addr == 0x66 ? 1 : 2;
		u8 reg = req[n].data[0];

		if (req[n].req == I2C_READ_REQUEST) {
			CHECK(n > 0);
			reg = req[n - 1].data[0];
			for (i = 0; i < req[n].len; i++)
				req[n].data[i] = bus->regs[chip][bus->bank[addr]][(u8)(reg + i)];
		} else {
			for (i = 1; i < req[n].len; i++) {
				u8 at = (u8)(reg + i - 1);
				if (chip != 2 && at == 0)
					bus->bank[addr] = req[n].data[i];
				else
					bus->regs[chip][bus->bank[addr]][at] = req[n].data[i];
				if (addr == 0x66 && at == 1)
					bus->frozen = req[n].data[i] != 0;
			}
		}
	}
	return 0;
}

static struct mock_bus bus;
static struct device dev = {0};
static struct i2c_comm_master master = {gate, request, &bus};

static void reset(struct cxd2856er_demod *demod, struct cxd2858er_tuner *tuner)
{
	memset(&bus, 0, sizeof(bus));
	memset(demod, 0, sizeof(*demod));
	memset(tuner, 0, sizeof(*tuner));
	demod->dev = &dev;
	demod->i2c = &master;
	demod->i2c_addr.slvx = 0x64;
	demod->i2c_addr.slvt = 0x66;
	demod->state = CXD2856ER_SLEEP_STATE;
	tuner->dev = &dev;
	tuner->i2c = &master;
	tuner->i2c_addr = 0x60;
	tuner->config.xtal = 16000;
}

int main(void)
{
	struct cxd2856er_demod demod;
	struct cxd2858er_tuner tuner;
	union cxd2856er_system_params params;
	unsigned int xtal, mode, sync, ts, count, fail;
	bool locked;
	u16 cnr;

	for (xtal = 16000; xtal <= 24000; xtal += 8000) {
		for (mode = 0; mode < 2; mode++) {
			reset(&demod, &tuner);
			memset(&params, 0, sizeof(params));
			params.isdbc.j83b = mode != 0;
			bus.regs[1][0][0xd1] = 0x02;
			tuner.config.xtal = xtal;
			CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
			CHECK(cxd2858er_set_params_c(&tuner, 697143, params.isdbc.j83b) == 0);
			CHECK(bus.regs[2][0][0x62] == (xtal == 16000 ? 0x10 : 0x18));
			CHECK(bus.regs[2][0][0x69] == (mode ? 0x84 : 0x85));
			CHECK(bus.regs[2][0][0x6e] == (mode ? 0x1b : 0x1a));
			CHECK(bus.regs[2][0][0x6f] == (mode ? 0x02 : 0x1c));
			CHECK(bus.regs[1][0][0xd9] == 0x08);
			CHECK(bus.regs[1][0][0xd1] == (mode ? 2 : 0));
			CHECK(bus.regs[1][0][0x33] == (mode ? 1 : 0));
			CHECK(cxd2856er_sleep(&demod) == 0);
			CHECK(cxd2858er_stop(&tuner) == 0 && !bus.gate_open);
		}
	}
	puts("ISDBC, J83B and 16/24 MHz REF_R: PASS");

	for (mode = 0; mode < 2; mode++) {
		for (sync = 0; sync < 2; sync++) {
			for (ts = 0; ts < 2; ts++) {
				reset(&demod, &tuner);
				demod.j83b_active = mode != 0;
				bus.regs[1][0x40][0x88] = (u8)sync;
				bus.regs[1][0x40][0x10] = ts ? 0x20 : 0;
				CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == 0);
				CHECK(locked == (sync && ts));
				CHECK(bus.calls == (sync ? 3U : 2U));
			}
		}
		for (fail = 1; fail <= 3; fail++) {
			reset(&demod, &tuner);
			demod.j83b_active = mode != 0;
			bus.regs[1][0x40][0x88] = 1;
			bus.regs[1][0x40][0x10] = 0x20;
			bus.fail_at = fail;
			locked = true;
			CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == -EIO);
			CHECK(!locked && bus.calls == fail);
		}
	}
	puts("ISDBC/J83B QAM and TS lock states and I/O failures: PASS");

	for (fail = 0; fail <= 4; fail++) {
		reset(&demod, &tuner);
		bus.regs[1][0x40][0x4c] = 0xe1;
		bus.regs[1][0x40][0x4d] = 0x23;
		bus.fail_at = fail;
		CHECK(cxd2856er_read_cnr_raw_isdbc(&demod, &cnr) == (fail ? -EIO : 0));
		CHECK(bus.frozen == (fail == 4));
		if (!fail)
			CHECK(cnr == 0x123);
	}
	puts("ISDBC C/N raw mask and unfreeze on I/O failure: PASS");

	reset(&demod, &tuner);
	tuner.system = CXD2858ER_ISDB_C_SYSTEM;
	CHECK(cxd2858er_stop(&tuner) == 0);
	CHECK(tuner.system == CXD2858ER_UNSPECIFIED_SYSTEM && !bus.gate_open);
	CHECK(bus.regs[2][0][0x5e] == 0x15 && bus.regs[2][0][0x87] == 0xc0);
	count = bus.calls;
	for (fail = 1; fail <= count; fail++) {
		reset(&demod, &tuner);
		tuner.system = CXD2858ER_ISDB_C_SYSTEM;
		bus.fail_at = fail;
		CHECK(cxd2858er_stop(&tuner) == -EIO);
		CHECK(tuner.system == CXD2858ER_ISDB_C_SYSTEM && !bus.gate_open);
	}
	reset(&demod, &tuner);
	tuner.system = CXD2858ER_ISDB_C_SYSTEM;
	CHECK(cxd2858er_term(&tuner) == 0);
	CHECK(tuner.system == CXD2858ER_UNSPECIFIED_SYSTEM && !bus.gate_open);
	reset(&demod, &tuner);
	tuner.system = CXD2858ER_ISDB_C_SYSTEM;
	CHECK(cxd2858er_set_params_s(&tuner, CXD2858ER_ISDB_S_SYSTEM, 1049480, 28860) == 0);
	CHECK(tuner.system == CXD2858ER_ISDB_S_SYSTEM);
	CHECK(bus.regs[2][0][0x5e] == 0x15 && !bus.gate_open);
	for (fail = 1; fail <= count; fail++) {
		reset(&demod, &tuner);
		tuner.system = CXD2858ER_ISDB_C_SYSTEM;
		bus.fail_at = fail;
		CHECK(cxd2858er_set_params_s(&tuner, CXD2858ER_ISDB_S_SYSTEM, 1049480, 28860) == -EIO);
		CHECK(tuner.system == CXD2858ER_ISDB_C_SYSTEM && !bus.gate_open);
		CHECK(bus.regs[2][0][0x15] == 0);
	}
	puts("Cable tuner stop, term, C to S and stop I/O failure boundaries: PASS");

	reset(&demod, &tuner);
	memset(&params, 0, sizeof(params));
	params.isdbc.j83b = true;
	CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
	CHECK(demod.j83b_active);
	CHECK(bus.regs[0][0][0x17] == 0x08);
	CHECK(bus.regs[1][0x40][0x21] == 0x04);
	CHECK(bus.regs[1][0x40][0xb3] == 0x05);
	CHECK(bus.regs[1][0x40][0x1e] == 0x80);
	CHECK(bus.regs[1][0x40][0x8e] == 0x58);
	CHECK(bus.regs[1][0x41][0xcf] == 0x99);
	CHECK(bus.regs[1][0x10][0xb6] == 0x14);
	CHECK(bus.regs[1][0x10][0xb7] == 0 && bus.regs[1][0x10][0xb8] == 0);
	CHECK(bus.regs[1][0x40][0x14] == 0x10);
	CHECK(memcmp(&bus.regs[1][0x40][0x26], "\x07\x77\x07\x77", 4) == 0);
	count = bus.calls;
	CHECK(cxd2856er_sleep(&demod) == 0 && !demod.j83b_active);
	CHECK(bus.regs[1][0x40][0x21] == 0);
	CHECK(bus.regs[1][0x40][0xb3] == 2);
	CHECK(bus.regs[1][0x40][0x1e] == 0);
	CHECK(bus.regs[1][0x40][0x8e] == 0x0e);
	CHECK(bus.regs[1][0x41][0xcf] == 0x77);
	CHECK(bus.regs[1][0x40][0x14] == 0x1f);
	CHECK(memcmp(&bus.regs[1][0x40][0x26], "\x09\x9a\x00\xee", 4) == 0);

	/* 起動途中で失敗した場合も、地上波選局時に J.83B 固有の設定を解除する必要がある */
	for (fail = 1; fail <= count; fail++) {
		reset(&demod, &tuner);
		memset(&params, 0, sizeof(params));
		params.isdbc.j83b = true;
		bus.fail_at = fail;
		CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == -EIO);
		bus.fail_at = 0;
		params.bandwidth = 6;
		CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_T_SYSTEM, &params) == 0);
		CHECK(!demod.j83b_active);
	}
	printf("J83B register/restore and %u wakeup I/O failure boundaries: PASS\n", count);

	reset(&demod, &tuner);
	memset(&params, 0, sizeof(params));
	params.isdbc.j83b = true;
	CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
	count = bus.calls;
	CHECK(cxd2856er_sleep(&demod) == 0);
	count = bus.calls - count;
	for (fail = 1; fail <= count; fail++) {
		reset(&demod, &tuner);
		memset(&params, 0, sizeof(params));
		params.isdbc.j83b = true;
		CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
		bus.fail_at = bus.calls + fail;
		CHECK(cxd2856er_sleep(&demod) == -EIO && demod.j83b_active);
		bus.fail_at = 0;
		params.isdbc.j83b = false;
		CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
		CHECK(!demod.j83b_active && bus.regs[0][0][0x17] == 4);
	}
	printf("J83B %u sleep I/O failure boundaries and C restore: PASS\n", count);

	reset(&demod, &tuner);
	CHECK(cxd2858er_set_params_c(&tuner, 697143, true) == 0);
	count = bus.calls;
	for (fail = 1; fail <= count; fail++) {
		reset(&demod, &tuner);
		bus.fail_at = fail;
		CHECK(cxd2858er_set_params_c(&tuner, 697143, true) == -EIO);
		CHECK(!bus.gate_open);
	}
	printf("J83B %u tuner I/O failure boundaries and gate release: PASS\n", count);
	return 0;
}
