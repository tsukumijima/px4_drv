// SPDX-License-Identifier: GPL-2.0-only
/* 模擬 I2C による J.83B 設定・復帰・失敗境界の試験。
 * 信号ロック、USB 負荷、実際の TS 品質はこの試験の確認範囲外。
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
	unsigned int second_fail_at;
	unsigned int unfreeze_attempts;
	bool frozen;
	unsigned long long trace;
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
	if (num == 1 && req[0].addr == 0x66 && req[0].len == 2 &&
		req[0].data[0] == 0x01 && req[0].data[1] == 0)
		bus->unfreeze_attempts++;
	if (bus->calls == bus->fail_at)
		return -EIO;
	if (bus->calls == bus->second_fail_at)
		return -EINVAL;
	for (n = 0; n < num; n++) {
		int addr = req[n].addr;
		int chip = addr == 0x64 ? 0 : addr == 0x66 ? 1 : 2;
		u8 reg = req[n].data[0];
		bus->trace = bus->trace * 1099511628211ULL ^ req[n].req;
		bus->trace = bus->trace * 1099511628211ULL ^ addr;
		if (req[n].req == I2C_READ_REQUEST) {
			CHECK(n > 0);
			reg = req[n - 1].data[0];
			for (i = 0; i < req[n].len; i++)
				req[n].data[i] = bus->regs[chip][bus->bank[addr]][(u8)(reg + i)];
		} else {
			for (i = 1; i < req[n].len; i++) {
				u8 at = reg + i - 1;
				if (chip != 2 && at == 0)
					bus->bank[addr] = req[n].data[i];
				else
					bus->regs[chip][bus->bank[addr]][at] = req[n].data[i];
				if (addr == 0x66 && at == 1)
					bus->frozen = req[n].data[i] != 0;
			}
		}
		for (i = 0; i < req[n].len; i++)
			bus->trace = bus->trace * 1099511628211ULL ^ req[n].data[i];
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

static void monitor_registers(bool ar_locked)
{
	bus.regs[1][0x40][0x88] = ar_locked ? 0x81 : 0x82;
	bus.regs[1][0x40][0x10] = 0xe0;
	bus.regs[1][0x40][0x19] = 0xfc;
	bus.regs[1][0x40][0x49] = 0xfa;
	bus.regs[1][0x40][0x4a] = 0xbc;
	bus.regs[1][0x40][0x4c] = 0xe1;
	bus.regs[1][0x40][0x4d] = 0x23;
	bus.regs[1][0x40][0x15] = 0xff;
	bus.regs[1][0x40][0x16] = 0xff;
	bus.regs[1][0x40][0x1a] = 0xf7;
	bus.regs[1][0x40][0x1b] = 0x77;
	memset(&bus.regs[1][0x40][0x9b], 0xff, 5);
	bus.regs[1][0x40][0x62] = 0xc1;
	bus.regs[1][0x40][0x63] = 0x23;
	bus.regs[1][0x40][0x64] = 0x45;
	bus.regs[1][0x40][0x60] = 0xeb;
	bus.regs[1][0x40][0x5c] = 0xf1;
	bus.regs[1][0x40][0x5d] = 0xfd;
	bus.regs[1][0x40][0x5e] = 0x23;
	bus.regs[1][0x40][0x5f] = 0x45;
	bus.regs[1][0x40][0xea] = 0xab;
	bus.regs[1][0x40][0xeb] = 0xcd;
	bus.regs[1][0x40][0xec] = 0xff;
	bus.regs[1][0x80][0x50] = 0x81;
	bus.regs[1][0x80][0x53] = 0x81;
	bus.regs[1][0x80][0x54] = 0xf6;
	bus.regs[1][0x80][0x55] = 0xff;
}

int main(void)
{
	struct cxd2856er_demod demod;
	struct cxd2858er_tuner tuner;
	union cxd2856er_system_params params;
	unsigned int ref, qam;
	{
		unsigned int xtal, profile;
		for (xtal = 16000; xtal <= 24000; xtal += 8000) {
			for (profile = 0; profile < 4; profile++) {
				reset(&demod, &tuner);
				tuner.config.xtal = xtal;
				tuner.config.isdbc_reference_profile = (profile & 1) != 0;
				tuner.config.experimental_j83b = (profile & 2) != 0;
				CHECK(cxd2858er_set_params_c(&tuner, 697143, 6) == 0);
				CHECK(bus.regs[2][0][0x62] == (xtal == 16000 ? 0x10 : 0x18));
			}
		}
		puts("Cable REF_R for 16/24 MHz crystals and all profile flags: PASS");
	}
	{
		bool locked;
		unsigned int sync, ts, poll, count, fail;
		for (sync = 0; sync < 2; sync++) {
			for (ts = 0; ts < 2; ts++) {
				reset(&demod, &tuner);
				bus.regs[1][0x40][0x88] = (u8)sync;
				bus.regs[1][0x40][0x10] = ts ? 0x20 : 0;
				for (poll = 0; poll < 26; poll++) {
					count = bus.calls;
					CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == 0);
					CHECK(locked == (sync && ts));
					CHECK(bus.calls - count == (sync ? 3U : 2U));
				}
			}
		}
		for (fail = 1; fail <= 3; fail++) {
			reset(&demod, &tuner);
			bus.regs[1][0x40][0x88] = 1;
			bus.regs[1][0x40][0x10] = 0x20;
			bus.fail_at = fail;
			locked = true;
			CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == -EIO);
			CHECK(!locked && bus.calls == fail);
		}
		puts("ISDBC QAM/TS lock states, polling I/O and failure boundaries: PASS");
	}
	{
		struct cxd2856er_j83b_monitor sample, zero = {0};
		unsigned int fail, ar, count;
		reset(&demod, &tuner);
		CHECK(cxd2856er_read_j83b_monitor(&demod, &sample) == -EINVAL);
		CHECK(cxd2856er_read_j83b_monitor(NULL, &sample) == -EINVAL);
		CHECK(cxd2856er_read_j83b_monitor(&demod, NULL) == -EINVAL);
		CHECK(bus.calls == 0);
		for (ar = 0; ar < 2; ar++) {
			reset(&demod, &tuner);
			demod.state = CXD2856ER_ACTIVE_STATE;
			demod.j83b_active = true;
			monitor_registers(ar != 0);
			CHECK(cxd2856er_read_j83b_monitor(&demod, &sample) == 0);
			count = bus.calls;
			CHECK(count == (ar ? 19U : 16U));
			CHECK(sample.qam == 4 && sample.if_agc_raw == 0xabc && sample.cnr_raw == 0x123);
			CHECK(sample.ar_status_raw == (ar ? 0x81 : 0x82) && sample.ts_status_raw == 0xe0);
			CHECK(sample.carrier_offset_raw == (ar ? -1 : 0));
			CHECK(sample.trial_symbol_rate_raw == (ar ? 0x777 : 0));
			CHECK(sample.timing_integral_raw == (ar ? -1 : 0));
			CHECK(sample.sampling_rate_mode_raw == (ar ? 3 : 0));
			CHECK(sample.ber_valid && sample.ber_bit_errors == 0x12345 && sample.ber_period_exp == 11);
			CHECK(sample.per_valid && sample.per_packet_errors == 0x12345 && sample.per_period_exp == 17);
			CHECK(sample.rs_valid && sample.rs_packet_errors == 0xabcd);
			CHECK(sample.vq_status_raw == 0x81 && sample.interleave_valid && sample.interleave_raw == 6);
			CHECK(sample.spectrum_raw == 1 && !bus.frozen && bus.bank[0x66] == 0x40);
			for (fail = 1; fail <= count; fail++) {
				reset(&demod, &tuner);
				demod.state = CXD2856ER_ACTIVE_STATE;
				demod.j83b_active = true;
				monitor_registers(ar != 0);
				memset(&sample, 0xff, sizeof(sample));
				bus.fail_at = fail;
				CHECK(cxd2856er_read_j83b_monitor(&demod, &sample) == -EIO);
				CHECK(memcmp(&sample, &zero, sizeof(sample)) == 0);
				CHECK(bus.unfreeze_attempts == (fail == 1 ? 0U : 1U));
				CHECK(bus.frozen == (fail == count));
			}
		}
		reset(&demod, &tuner);
		demod.state = CXD2856ER_ACTIVE_STATE;
		demod.j83b_active = true;
		monitor_registers(true);
		bus.fail_at = 3;
		bus.second_fail_at = 5;
		CHECK(cxd2856er_read_j83b_monitor(&demod, &sample) == -EIO);
		CHECK(bus.unfreeze_attempts == 1 && memcmp(&sample, &zero, sizeof(sample)) == 0);
		/* 符号拡張の両端と無効ビットを、設定値の変更なしに確認する。 */
		bus.fail_at = bus.second_fail_at = 0;
		bus.regs[1][0x40][0x15] = 0x20;
		bus.regs[1][0x40][0x16] = 0;
		bus.regs[1][0x40][0x9b] = 0x80;
		memset(&bus.regs[1][0x40][0x9c], 0, 3);
		bus.regs[1][0x40][0x62] &= 0x7f;
		bus.regs[1][0x40][0x5d] &= 0xfb;
		bus.regs[1][0x40][0xec] &= 0xfe;
		bus.regs[1][0x80][0x53] &= 0xfe;
		CHECK(cxd2856er_read_j83b_monitor(&demod, &sample) == 0);
		CHECK(sample.carrier_offset_raw == -8192 && sample.timing_integral_raw == (-2147483647 - 1));
		CHECK(!sample.ber_valid && !sample.per_valid && !sample.rs_valid && !sample.interleave_valid);
		bus.regs[1][0x40][0x15] = 0x1f;
		bus.regs[1][0x40][0x16] = 0xff;
		bus.regs[1][0x40][0x9b] = 0x7f;
		memset(&bus.regs[1][0x40][0x9c], 0xff, 3);
		CHECK(cxd2856er_read_j83b_monitor(&demod, &sample) == 0);
		CHECK(sample.carrier_offset_raw == 8191 && sample.timing_integral_raw == 2147483647);
		puts("J83B stages/FEC masks, signed extrema, inactive guard, 19 locked/16 unlocked I/O boundaries: PASS");
	}
	/* J.83B 無効時の各 ISDB-C プロファイルについて、I2C 操作列を比較できるハッシュを出力する。 */
	for (ref = 0; ref < 2; ref++) {
		for (qam = 64; qam <= 256; qam *= 4) {
			reset(&demod, &tuner);
			memset(&params, 0, sizeof(params));
			params.isdbc.symbol_rate = 5274;
			params.isdbc.modulation = qam;
			params.isdbc.experimental_profile = true;
			params.isdbc.reference_profile = ref != 0;
			tuner.config.isdbc_reference_profile = ref != 0;
			CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
			CHECK(cxd2858er_set_params_c(&tuner, 697143, 6) == 0);
			printf("ISDBC ref=%u qam=%u calls=%u trace=%016llx\n", ref, qam, bus.calls, bus.trace);
		}
	}
#ifndef J83B_BASELINE
	{
		unsigned int count, fail, mode;
		/* 地上波・ケーブル共通の RF 経路は、衛星への切り替え前に停止し、停止時のエラーを返す。 */
		for (mode = CXD2858ER_ISDB_T_SYSTEM; mode <= CXD2858ER_ISDB_C_SYSTEM; mode++) {
			if (mode == CXD2858ER_ISDB_S_SYSTEM)
				continue;
			reset(&demod, &tuner);
			tuner.system = mode;
			CHECK(cxd2858er_stop(&tuner) == 0);
			CHECK(tuner.system == CXD2858ER_UNSPECIFIED_SYSTEM && !bus.gate_open);
			CHECK(bus.regs[2][0][0x5e] == 0x15 && bus.regs[2][0][0x87] == 0xc0);
			count = bus.calls;
			CHECK(count > 0);
			for (fail = 1; fail <= count; fail++) {
				reset(&demod, &tuner);
				tuner.system = mode;
				bus.fail_at = fail;
				CHECK(cxd2858er_stop(&tuner) == -EIO);
				CHECK(tuner.system == mode && !bus.gate_open);
			}
			reset(&demod, &tuner);
			tuner.system = mode;
			CHECK(cxd2858er_set_params_s(&tuner, CXD2858ER_ISDB_S_SYSTEM, 1049480, 28860) == 0);
			CHECK(tuner.system == CXD2858ER_ISDB_S_SYSTEM && !bus.gate_open);
			CHECK(bus.regs[2][0][0x5e] == 0x15);
			for (fail = 1; fail <= count; fail++) {
				reset(&demod, &tuner);
				tuner.system = mode;
				bus.fail_at = fail;
				CHECK(cxd2858er_set_params_s(&tuner, CXD2858ER_ISDB_S_SYSTEM, 1049480, 28860) == -EIO);
				CHECK(tuner.system == mode && !bus.gate_open);
				CHECK(bus.regs[2][0][0x15] == 0);
			}
		}
		reset(&demod, &tuner);
		tuner.system = CXD2858ER_ISDB_C_SYSTEM;
		CHECK(cxd2858er_term(&tuner) == 0);
		CHECK(tuner.system == CXD2858ER_UNSPECIFIED_SYSTEM && !bus.gate_open);
		CHECK(bus.regs[2][0][0x5e] == 0x15);
		puts("T/C tuner stop, C term, T/C to S and stop I/O failure boundaries: PASS");
	}
	{
		bool locked;
		unsigned int count, fail;
		reset(&demod, &tuner);
		memset(&params, 0, sizeof(params));
		params.isdbc.experimental_j83b = true;
		tuner.config.experimental_j83b = true;
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
		CHECK(cxd2858er_set_params_c(&tuner, 697143, 6) == 0);
		CHECK(bus.regs[2][0][0x62] == 0x10);
		CHECK(bus.regs[2][0][0x69] == 0x84);
		CHECK(bus.regs[2][0][0x6e] == 0x1b && bus.regs[2][0][0x6f] == 0x02);
		tuner.config.xtal = 24000;
		CHECK(cxd2858er_set_params_c(&tuner, 697143, 6) == 0);
		CHECK(bus.regs[2][0][0x62] == 0x18);
		tuner.config.xtal = 20500;
		CHECK(cxd2858er_set_params_c(&tuner, 697143, 6) == -EINVAL);
		CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == 0 && !locked);
		bus.regs[1][0x40][0x88] = 1;
		CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == 0 && !locked);
		bus.regs[1][0x40][0x10] = 0x20;
		CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == 0 && locked);
		bus.regs[1][0x40][0x88] = 0;
		CHECK(cxd2856er_is_ts_locked_isdbc(&demod, &locked) == 0 && !locked);
		CHECK(cxd2856er_sleep(&demod) == 0 && !demod.j83b_active);
		CHECK(bus.regs[1][0x40][0x21] == 0);
		CHECK(bus.regs[1][0x40][0xb3] == 2);
		CHECK(bus.regs[1][0x40][0x1e] == 0);
		CHECK(bus.regs[1][0x40][0x8e] == 0x0e);
		CHECK(bus.regs[1][0x41][0xcf] == 0x77);
		CHECK(bus.regs[1][0x40][0x14] == 0x1f);
		CHECK(memcmp(&bus.regs[1][0x40][0x26], "\x09\x9a\x00\xee", 4) == 0);
		/* 起動時の各 I/O を失敗させ、次の地上波選局で J.83B 固有の設定を解除できることを確認する。 */
		for (fail = 1; fail <= count; fail++) {
			reset(&demod, &tuner);
			memset(&params, 0, sizeof(params));
			params.isdbc.experimental_j83b = true;
			bus.fail_at = fail;
			CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == -EIO);
			bus.fail_at = 0;
			params.bandwidth = 6;
			CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_T_SYSTEM, &params) == 0);
			CHECK(!demod.j83b_active);
		}
		printf("J83B register/lock/restore and %u wakeup I/O failure boundaries: PASS\n", count);
		/* J.83B の停止時に各 I/O を失敗させ、ISDB-C へ復帰できることを確認する。 */
		reset(&demod, &tuner);
		memset(&params, 0, sizeof(params));
		params.isdbc.experimental_j83b = true;
		CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
		count = bus.calls;
		CHECK(cxd2856er_sleep(&demod) == 0);
		count = bus.calls - count;
		for (fail = 1; fail <= count; fail++) {
			reset(&demod, &tuner);
			memset(&params, 0, sizeof(params));
			params.isdbc.experimental_j83b = true;
			CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
			bus.fail_at = bus.calls + fail;
			CHECK(cxd2856er_sleep(&demod) == -EIO && demod.j83b_active);
			bus.fail_at = 0;
			memset(&params, 0, sizeof(params));
			params.isdbc.symbol_rate = 5274;
			params.isdbc.modulation = 256;
			params.isdbc.experimental_profile = true;
			params.isdbc.reference_profile = true;
			CHECK(cxd2856er_wakeup(&demod, CXD2856ER_ISDB_C_SYSTEM, &params) == 0);
			CHECK(!demod.j83b_active && bus.regs[0][0][0x17] == 4);
		}
		printf("J83B %u sleep I/O failure boundaries and C restore: PASS\n", count);
		reset(&demod, &tuner);
		tuner.config.experimental_j83b = true;
		CHECK(cxd2858er_set_params_c(&tuner, 697143, 6) == 0);
		count = bus.calls;
		for (fail = 1; fail <= count; fail++) {
			reset(&demod, &tuner);
			tuner.config.experimental_j83b = true;
			bus.fail_at = fail;
			CHECK(cxd2858er_set_params_c(&tuner, 697143, 6) == -EIO);
			CHECK(!bus.gate_open);
		}
		printf("J83B %u tuner I/O failure boundaries and gate release: PASS\n", count);
	}
#endif
	return 0;
}
