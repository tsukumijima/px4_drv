// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony CXD2856ER driver (cxd2856er.c)
 *
 * Copyright (c) 2018-2021 nns779
 * J.83B reference portions:
 * Copyright (c) 2022 Sony Semiconductor Solutions Corporation
 * Copyright (c) 2021 Davin zhang <Davin@tbsdtv.com>
 * Copyright (c) 2026 Yoonji Park <koreapyj@dcmys.kr>
 */

#include "print_format.h"
#include "cxd2856er.h"

#ifdef __linux__
#include <linux/delay.h>
#endif

int cxd2856er_read_regs(struct cxd2856er_demod *demod,
			enum cxd2856er_i2c_target target,
			u8 reg, u8 *buf, int len)
{
	u8 b, addr;
	struct i2c_comm_request req[2];

	if (!buf || !len)
		return -EINVAL;

	b = reg;
	addr = (target == CXD2856ER_I2C_SLVT) ? demod->i2c_addr.slvt
					      : demod->i2c_addr.slvx;

	req[0].req = I2C_WRITE_REQUEST;
	req[0].addr = addr;
	req[0].data = &b;
	req[0].len = 1;

	req[1].req = I2C_READ_REQUEST;
	req[1].addr = addr;
	req[1].data = buf;
	req[1].len = len;

	return i2c_comm_master_request(demod->i2c, req, 2);
}

int cxd2856er_write_regs(struct cxd2856er_demod *demod,
			 enum cxd2856er_i2c_target target,
			 u8 reg, u8 *buf, int len)
{
	u8 b[255], addr;
	struct i2c_comm_request req[1];

	if (!buf || !len || len > 254)
		return -EINVAL;

	b[0] = reg;
	memcpy(&b[1], buf, len);

	addr = (target == CXD2856ER_I2C_SLVT) ? demod->i2c_addr.slvt
					      : demod->i2c_addr.slvx;

	req[0].req = I2C_WRITE_REQUEST;
	req[0].addr = addr;
	req[0].data = b;
	req[0].len = 1 + len;

	return i2c_comm_master_request(demod->i2c, req, 1);
}

int cxd2856er_write_reg_mask(struct cxd2856er_demod *demod,
			     enum cxd2856er_i2c_target target,
			     u8 reg, u8 val, u8 mask)
{
	int ret = 0;
	u8 tmp;

	if (!mask)
		return -EINVAL;

	if (mask != 0xff) {
		ret = cxd2856er_read_regs(demod, target, reg, &tmp, 1);
		if (ret)
			return ret;

		tmp &= ~mask;
		tmp |= (val & mask);
	} else {
		tmp = val;
	}

	return cxd2856er_write_regs(demod, target, reg, &tmp, 1);
}

static int cxd2856er_i2c_master_gate_ctrl(void *i2c_priv, bool open)
{
	struct cxd2856er_demod *demod = i2c_priv;

	return cxd2856er_write_slvx_reg(demod, 0x08, (open) ? 0x01: 0x00);
}

static int cxd2856er_i2c_master_request(void *i2c_priv,
					const struct i2c_comm_request *req,
					int num)
{
	struct cxd2856er_demod *demod = i2c_priv;

	/* through */
	return i2c_comm_master_request(demod->i2c, req, num);
}

int cxd2856er_init(struct cxd2856er_demod *demod)
{
	int ret = 0;

	if (!demod->dev || !demod->i2c)
		return -EINVAL;

	if (!demod->i2c_addr.slvx || !demod->i2c_addr.slvt)
		return -EINVAL;

	if (demod->config.xtal != 24000)
		return -EINVAL;

	demod->i2c_master.gate_ctrl = cxd2856er_i2c_master_gate_ctrl;
	demod->i2c_master.request = cxd2856er_i2c_master_request;
	demod->i2c_master.priv = demod;

	demod->j83b_active = false;
	demod->state = CXD2856ER_UNKNOWN_STATE;
	demod->system = CXD2856ER_UNSPECIFIED_SYSTEM;

	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x10, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x18, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x28, 0x13);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x17, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x1d, 0x00);
	if (ret)
		return ret;

	/* 24MHz */
	ret = cxd2856er_write_slvx_reg(demod, 0x14, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x1c, 0x03);
	if (ret)
		return ret;

	msleep(4);

	ret = cxd2856er_write_slvx_reg(demod, 0x50, 0x00);
	if (ret)
		return ret;

	msleep(1);

	ret = cxd2856er_write_slvx_reg(demod, 0x10, 0x00);
	if (ret)
		return ret;

	msleep(1);

	demod->state = CXD2856ER_SLEEP_STATE;

	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod,
				       0x1a, (demod->config.tuner_i2c) ? 0x01 : 0x00);
	if (ret)
		return ret;

	return 0;
}

int cxd2856er_term(struct cxd2856er_demod *demod)
{
	cxd2856er_sleep(demod);

	demod->i2c_master.gate_ctrl = NULL;
	demod->i2c_master.request = NULL;
	demod->i2c_master.priv = NULL;

	return 0;
}

static int cxd2856er_set_ts_clock(struct cxd2856er_demod *demod,
				  enum cxd2856er_system system,
				  bool reference_profile)
{
	int ret = 0;
	u8 tmp;
	u8 ts_clock_period;

	/*
	 * PX-MLT's original ISDB-T path uses 0x10 here.  The corresponding
	 * Sony DVB-C implementation uses 0x08.  ISDB-C 256QAM transports can
	 * exceed the output rate that was observed to work with the inherited
	 * 0x10 setting, so use the faster clock only for the experimental
	 * ISDB-C path.  This register is volatile and is programmed on every
	 * tune.
	 */
	ts_clock_period = (system == CXD2856ER_ISDB_C_SYSTEM) ? 0x08 : 0x10;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_read_slvt_reg(demod, 0xc4, &tmp);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xd3, reference_profile ? 0x00 : 0x01, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xde, 0x00, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xda, 0x00, 0x01);
	if (ret)
		return ret;

	/* cxd2878.c の cxd2878_setTSClkModeAndFreq(): DVB-C は rate control
	 * 有効、serial は continuous/full rate。parallel では C4/D1 を書かない。
	 * 64QAM/256QAM の区別はなく、既存プロファイルだけ従来の値を保持する。 */
	if (!reference_profile || (tmp & 0x80)) {
		ret = cxd2856er_write_slvt_reg_mask(demod, 0xc4,
			reference_profile ? 0x01 : 0x00, 0x03);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg_mask(demod, 0xd1,
			reference_profile ? 0x01 : 0x02, 0x03);
		if (ret)
			return ret;
	}

	ret = cxd2856er_write_slvt_reg(demod, 0xd9, ts_clock_period);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x32, 0x00, 0x01);
	if (ret)
		return ret;

	switch (system) {
	case CXD2856ER_ISDB_T_SYSTEM:
	case CXD2856ER_ISDB_C_SYSTEM:
		ret = cxd2856er_write_slvt_reg_mask(demod, 0x33, reference_profile ? 0x01 : 0x02, 0x03);
		break;

	case CXD2856ER_ISDB_S_SYSTEM:
		ret = cxd2856er_write_slvt_reg_mask(demod, 0x33, 0x00, 0x03);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x32, 0x01, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x66, 0x01, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x66, 0x01, 0x01);
	if (ret)
		return ret;

	return 0;
}

static int cxd2856er_set_ts_pin_state(struct cxd2856er_demod *demod,
				      bool state)
{
	int ret = 0;
	u8 tmp, mask;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_read_slvt_reg(demod, 0xc4, &tmp);
	if (ret)
		return ret;

	if ((tmp & 0x88) == 0x80)
		mask = 0x01;
	else if ((tmp & 0x88) == 0x88)
		mask = 0x80;
	else
		mask = 0xff;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod,
					    0x81, (state) ? 0x00 : 0xff, mask);
	if (ret)
		return ret;

	return 0;
}

/*
 * CXD2856ER's original ISDB-T path uses TSIF clock-select value 2.  Public
 * Sony DVB-C code uses value 0 for both the TS output duty mode and clock
 * select.  64QAM happens to work with the terrestrial setting on this board,
 * but the 256QAM transport is much denser and reaches FEC while producing no
 * bytes at the IT9305.  Test the documented DVB-C TSIF form only for the
 * experimental 256QAM path; the receiver board wiring has to prove whether it
 * accepts it.
 */
static int cxd2856er_set_isdbc_256_tsif(struct cxd2856er_demod *demod)
{
	int ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x32, 0x00, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xd1, 0x00, 0x03);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x33, 0x00, 0x03);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x32, 0x01, 0x01);
	if (ret)
		return ret;

	dev_info(demod->dev,
		 "CXD2856ER ISDB-C: using experimental 256QAM DVB-C TSIF profile (D1/33=0)\n");
	return 0;
}

static int cxd2856er_sleep_isdbt(struct cxd2856er_demod *demod)
{
	int ret = 0;
	u8 data[2];

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xc3, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80, 0x1f, 0x1f);
	if (ret)
		return ret;

	ret = cxd2856er_set_ts_pin_state(demod, false);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x69, 0x05);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x6b, 0x07);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x9d, 0x14);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xd3, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xed, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xe2, 0x4e);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xf2, 0x03);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xde, 0x32);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x15);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xde, 0x03);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x17);
	if (ret)
		return ret;

	data[0] = 0x01;
	data[1] = 0x00;

	ret = cxd2856er_write_slvt_regs(demod, 0x38, data, 2);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x1e);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x73, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x63);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x81, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x18, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x49, 0x33);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x4b, 0x21);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xfe, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x2c, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xa9, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x17, 0x01);
	if (ret)
		return ret;

	return 0;
}

static int cxd2856er_sleep_isdbs(struct cxd2856er_demod *demod)
{
	int ret = 0;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xc3, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80, 0x1f, 0x1f);
	if (ret)
		return ret;

	ret = cxd2856er_set_ts_pin_state(demod, false);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x18, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x6a, 0x11);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x4b, 0x21);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x28, 0x13);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xfe, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x2c, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xa9, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x2d, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x17, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0xa0);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xd7, 0x00);
	if (ret)
		return ret;

	return 0;
}

/* 出典: Sony/TBS cxd2878_sleep() の SONY_DTV_SYSTEM_J83B (GPL-2.0)。
 * 共通のアナログ停止だけでは J.83B 固有値が残るため、既存 T/C の前に戻す。
 */
static int cxd2856er_sleep_j83b(struct cxd2856er_demod *demod)
{
	int ret;
	u8 data[4];
	/* 固有値を戻す前に TS を停止する順序も出典の sleep と揃える。 */
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xc3, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80, 0x1f, 0x1f);
	if (ret)
		return ret;
	ret = cxd2856er_set_ts_pin_state(demod, false);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xa3, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x21, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xc3, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xb3, 0x02);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x1e, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x8e, 0x0e);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x41);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xcf, 0x77);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x14, 0x1f);
	if (ret)
		return ret;
	data[0] = 0x09; data[1] = 0x9a; data[2] = 0x00; data[3] = 0xee;
	ret = cxd2856er_write_slvt_regs(demod, 0x26, data, 4);
	if (ret)
		return ret;
	return cxd2856er_sleep_isdbt(demod);
}

int cxd2856er_sleep(struct cxd2856er_demod *demod)
{
	if (demod->state == CXD2856ER_SLEEP_STATE)
		return -EALREADY;

	if (demod->j83b_active) {
		int ret = cxd2856er_sleep_j83b(demod);
		if (ret)
			return ret;
		demod->j83b_active = false;
		demod->state = CXD2856ER_SLEEP_STATE;
		demod->system = CXD2856ER_UNSPECIFIED_SYSTEM;
		return 0;
	}

	switch (demod->system) {
	case CXD2856ER_ISDB_T_SYSTEM:
	case CXD2856ER_ISDB_C_SYSTEM:
		/*
		 * ISDB-C uses the terrestrial/cable analogue front-end path.  Its
		 * experimental wake-up sequence is based on the same power-down
		 * state as terrestrial mode, so use the established ISDB-T sleep
		 * sequence before a C-to-T/S delivery-system switch.
		 */
		cxd2856er_sleep_isdbt(demod);
		break;

	case CXD2856ER_ISDB_S_SYSTEM:
		cxd2856er_sleep_isdbs(demod);
		break;

	default:
		break;
	}

	demod->state = CXD2856ER_SLEEP_STATE;
	demod->system = CXD2856ER_UNSPECIFIED_SYSTEM;

	return 0;
}

static int cxd2856er_set_bandwidth_isdbt(struct cxd2856er_demod *demod,
					 u32 bandwidth)
{
	int ret = 0;
	u8 data[14];

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;

	switch (bandwidth) {
	case 6:
		data[0] = 0x17;
		data[1] = 0xa0;
		data[2] = 0x80;
		data[3] = 0x00;
		data[4] = 0x00;

		ret = cxd2856er_write_slvt_regs(demod, 0x9f, data, 5);
		if (ret)
			break;

		data[0] = 0x31;
		data[1] = 0xa8;
		data[2] = 0x29;
		data[3] = 0x9b;
		data[4] = 0x27;
		data[5] = 0x9c;
		data[6] = 0x28;
		data[7] = 0x9e;
		data[8] = 0x29;
		data[9] = 0xa4;
		data[10] = 0x29;
		data[11] = 0xa2;
		data[12] = 0x29;
		data[13] = 0xa8;

		ret = cxd2856er_write_slvt_regs(demod, 0xa6, data, 14);
		if (ret)
			break;

		data[0] = 0x12;
		data[1] = 0xee;
		data[2] = 0xef;

		ret = cxd2856er_write_slvt_regs(demod, 0xb6, data, 3);
		if (ret)
			break;

		ret = cxd2856er_write_slvt_reg(demod, 0xd7, 0x04);
		if (ret)
			break;

		data[0] = 0x1f;
		data[1] = 0x79;

		ret = cxd2856er_write_slvt_regs(demod, 0xd9, data, 2);
		if (ret)
			break;

		ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x12);
		if (ret)
			break;

		ret = cxd2856er_write_slvt_reg(demod, 0x71, 0x07);
		if (ret)
			break;

		ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x15);
		if (ret)
			break;

		ret = cxd2856er_write_slvt_reg(demod, 0xbe, 0x02);
		if (ret)
			break;

		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int cxd2856er_wakeup_isdbt(struct cxd2856er_demod *demod,
				  union cxd2856er_system_params *params)
{
	int ret = 0;
	u8 data[3];

	/* Read-only diagnostic counter; it does not alter the ISDB-T setup. */
	demod->isdbt_lock_checks = 0;

	ret = cxd2856er_set_ts_clock(demod, CXD2856ER_ISDB_T_SYSTEM, false);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x17, 0x06);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xa9, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x2c, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x4b, 0x74);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x49, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x18, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x6a, 0x50);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xa5, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x00;

	ret = cxd2856er_write_slvt_regs(demod, 0xce, data, 2);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x69, 0x04);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x6b, 0x03);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x9d, 0x50);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xd3, 0x06);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xed, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xe2, 0xce);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xf2, 0x13);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xde, 0x2e);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x15);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xde, 0x02);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x17);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x03;

	ret = cxd2856er_write_slvt_regs(demod, 0x38, data, 2);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x1e);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x73, 0x68);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x63);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x81, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x03;
	data[2] = 0x3b;

	ret = cxd2856er_write_slvt_regs(demod, 0x33, data, 3);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x60);
	if (ret)
		return ret;

	data[0] = 0xb7;
	data[1] = 0x1b;

	ret = cxd2856er_write_slvt_regs(demod, 0xa8, data, 2);
	if (ret)
		return ret;

	ret = cxd2856er_set_bandwidth_isdbt(demod, params->bandwidth);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80, 0x08, 0x1f);
	if (ret)
		return ret;

	ret = cxd2856er_set_ts_pin_state(demod, true);
	if (ret)
		return ret;

	return 0;
}

static int cxd2856er_wakeup_isdbs(struct cxd2856er_demod *demod)
{
	int ret = 0;
	u8 data[3];

	ret = cxd2856er_set_ts_clock(demod, CXD2856ER_ISDB_S_SYSTEM, false);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x17, 0x0c);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x2d, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xa9, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x2c, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x28, 0x31);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x4b, 0x31);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x6a, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x18, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x20, 0x01);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x00;

	ret = cxd2856er_write_slvt_regs(demod, 0xce, data, 2);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0xae);
	if (ret)
		return ret;

	data[0] = 0x07;
	data[1] = 0x37;
	data[2] = 0x0a;

	ret = cxd2856er_write_slvt_regs(demod, 0x20, data, 3);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0xa0);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xd7, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80, 0x10, 0x1f);
	if (ret)
		return ret;

	ret = cxd2856er_set_ts_pin_state(demod, true);
	if (ret)
		return ret;

	return 0;
}

static int cxd2856er_reset_isdbt(struct cxd2856er_demod *demod,
				 union cxd2856er_system_params *params)
{
	int ret = 0;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xc3, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_set_bandwidth_isdbt(demod, params->bandwidth);
	if (ret)
		return ret;

	return 0;
}

static int cxd2856er_reset_isdbs(struct cxd2856er_demod *demod)
{
	int ret = 0;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xc3, 0x01);
	if (ret)
		return ret;

	return 0;
}

/*
 * CXD2856ER supports ISDB-C in hardware, but its J.83C register sequence is
 * not present in the public driver.  Keep the mode distinct from ISDB-T so a
 * verified sequence can be added without accidentally programming terrestrial
 * settings into a cable channel.
 */
/* Sony/TBS 公開 cxd2878.c (GPL-2.0) の SLtoACC() と
 * SLtoACC_BandSetting(): SONY_DTV_BW_J83B_5_60_MSPS のみを移植。
 * TS 出力は DTV02A-4TS-P で受信済みの DVB-C 参照プロファイルを再利用。
 * J.83B 信号がない環境では非ロックと既存系統への復帰までが試験範囲で、
 * J.83B のロック・TS 品質は未検証。5.06/5.36 Msps は対象外。
 * IF は cxd2878.h の SONY_DEMOD_MAKE_IFFREQ_CONFIG と同じ
 * round(3.75 / 48 * 2^24) = 0x140000。48 MHz は復調器内部の基準であり、
 * 外部水晶 24 MHz を代入する式ではない（DVB-C 3.7 MHz と同じ基準）。
 */
static int cxd2856er_wakeup_j83b(struct cxd2856er_demod *demod)
{
	int ret;
	u8 data[14];

	demod->isdbc_lock_checks = 0;
	dev_info(demod->dev, "CXD2856ER J.83B 5.6 Msps (experimental, unverified)\n");
	ret = cxd2856er_set_ts_clock(demod, CXD2856ER_ISDB_C_SYSTEM, true);
	if (ret)
		return ret;
	/* 途中の I/O 失敗でも次回 sleep が J.83B 固有状態を解除できるよう保持する。 */
	demod->j83b_active = true;
	demod->system = CXD2856ER_ISDB_C_SYSTEM;
	demod->state = CXD2856ER_ACTIVE_STATE;
	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvx_reg(demod, 0x17, 0x08);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xa9, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x2c, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x4b, 0x74);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x49, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvx_reg(demod, 0x18, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x6a, 0x48);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xa5, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x21, 0x04);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xb3, 0x05);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x1e, 0x80);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x8e, 0x58);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x41);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xcf, 0x99);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0xa3, 0x14);
	if (ret)
		return ret;
	data[0] = 0x00; data[1] = 0x03; data[2] = 0x3b;
	ret = cxd2856er_write_slvt_regs(demod, 0x33, data, 3);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x01; data[1] = 0x01;
	ret = cxd2856er_write_slvt_regs(demod, 0xce, data, 2);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;
	data[0] = 0x31; data[1] = 0xa8; data[2] = 0x29; data[3] = 0x9b; data[4] = 0x27; data[5] = 0x9c; data[6] = 0x28; data[7] = 0x9e; data[8] = 0x29; data[9] = 0xa4; data[10] = 0x29; data[11] = 0xa2; data[12] = 0x29; data[13] = 0xa8;
	ret = cxd2856er_write_slvt_regs(demod, 0xa6, data, 14);
	if (ret)
		return ret;
	data[0] = 0x14; data[1] = 0x00; data[2] = 0x00;
	ret = cxd2856er_write_slvt_regs(demod, 0xb6, data, 3);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x14, 0x10);
	if (ret)
		return ret;
	data[0] = 0x07; data[1] = 0x77; data[2] = 0x07; data[3] = 0x77;
	ret = cxd2856er_write_slvt_regs(demod, 0x26, data, 4);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80, 0x08, 0x1f);
	if (ret)
		return ret;
	return cxd2856er_set_ts_pin_state(demod, true);
}

static int cxd2856er_wakeup_isdbc(struct cxd2856er_demod *demod,
					  union cxd2856er_system_params *params)
{
	int ret;
	u8 data[14];
	const bool reference = params->isdbc.reference_profile;

	if (params->isdbc.symbol_rate != 5274 ||
		(params->isdbc.modulation != CXD2856ER_ISDBC_64QAM &&
		 params->isdbc.modulation != CXD2856ER_ISDBC_256QAM))
		return -EINVAL;

	/*
	 * This is an opt-in, DVB-C-derived 6 MHz profile.  It was mapped from
	 * cxd2841er.c after comparing its ISDB-T path with this CXD2856ER driver;
	 * it is not a Sony-verified J.83C sequence.  All writes are volatile and
	 * disappear after power removal, but do not enable it without a receiver
	 * that actually carries 64QAM or 256QAM / 5274 ksym/s
	 * transmodulation.  The candidate sequence is initially shared by the
	 * two QAM orders; a CXD2856ER-specific 256QAM-only register delta has
	 * not yet been independently verified.
	 */
	if (!params->isdbc.experimental_profile)
		return -ENOSYS;

	/* A new tune starts a new diagnostic sample sequence. */
	demod->isdbc_lock_checks = 0;

	dev_info(demod->dev, "CXD2856ER ISDB-C: %s, symbol_rate=%u ksym/s, modulation=%uQAM\n",
		 reference ? "reference profile (Sony/TBS DVB-C 6 MHz)" :
		 "experimental profile v11 (CXD2856ER baseline)",
		 params->isdbc.symbol_rate, params->isdbc.modulation);

	ret = cxd2856er_set_ts_clock(demod, CXD2856ER_ISDB_C_SYSTEM,
		params->isdbc.reference_profile);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvx_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvx_reg(demod, 0x17, 0x04);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	/*
	 * Keep the CXD2856ER terrestrial/cable analogue front-end baseline.
	 * CXD2841ER has a different analogue-clock preamble (0x2f/0x30/0x41/
	 * 0x43), so copying that preamble into CXD2856ER is not justified.
	 * The values below are the CXD2856ER ISDB-T baseline; the cable-specific
	 * differences are applied further down in this function.
	 */
	ret = cxd2856er_write_slvt_reg(demod, 0xa9, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x2c, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x4b, 0x74);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x49, 0x00);
	if (ret)
		return ret;
	/*
	 * The original 64QAM experiment retained the CXD2856ER ISDB-T analogue
	 * front-end values above.  For a forced 256QAM attempt, add the DVB-C
	 * analogue-front-end enable sequence used by Sony's public CXD2841ER
	 * driver.  These registers precede the common 6 MHz cable filters below;
	 * they are volatile and deliberately leave the proven 64QAM path intact.
	 */
	/* SLtoAC() にこの CXD2841ER 由来の追加アナログ設定はない。 */
	if (!reference && params->isdbc.modulation == CXD2856ER_ISDBC_256QAM) {
		data[0] = 0x09;
		data[1] = 0x54;
		ret = cxd2856er_write_slvt_reg(demod, 0x59, 0x00);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg(demod, 0x2f, 0x00);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg(demod, 0x30, 0x00);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg(demod, 0x41, 0x1a);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_regs(demod, 0x43, data, 2);
		if (ret)
			return ret;
	}
	ret = cxd2856er_write_slvx_reg(demod, 0x18, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;
	/* SLtoAC() に D2 の書き込みはない。従来の IFAGC 設定は A/B 用に残す。 */
	if (!reference) {
		ret = cxd2856er_write_slvt_reg_mask(demod, 0xd2, 0x09, 0x1f);
		if (ret)
			return ret;
	}
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xa5, 0x01, reference ? 0xff : 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x6a, 0x48);
	if (ret)
		return ret;

	/* Sony/TBS 公開 cxd2878.c の SLtoAC() と SLtoAC_BandSetting()。
	 * CXD2856=0x090 を含むドライバの DVB-C 6 MHz 経路で、QAM 別の分岐はない。
	 * CXD2856 実機での品質は未確認のため、参照フラグ時だけ両 QAM に適用する。
	 * ALP/TLV 用の bank 01/02/95 は CXD2856 への根拠が不足するため対象外。
	 */
	if (reference) {
		data[0] = 0x00; data[1] = 0x03; data[2] = 0x3b;
		ret = cxd2856er_write_slvt_regs(demod, 0x33, data, 3);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x48);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg(demod, 0x2c, 0x03);
		if (ret)
			return ret;
	}

	/* 6 MHz cable filter settings. */
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;
	data[0] = 0x27; data[1] = 0xa7; data[2] = 0x28; data[3] = 0xb3;
	data[4] = 0x02; data[5] = 0xf0; data[6] = 0x01; data[7] = 0xe8;
	data[8] = 0x00; data[9] = 0xcf; data[10] = 0x00; data[11] = 0xe6;
	data[12] = 0x23; data[13] = 0xa4;
	if (reference) {
		data[0] = 0x31; data[1] = 0xa8; data[2] = 0x29; data[3] = 0x9b;
		data[4] = 0x27; data[5] = 0x9c; data[6] = 0x28; data[7] = 0x9e;
		data[8] = 0x29; data[9] = 0xa4; data[10] = 0x29; data[11] = 0xa2;
		data[12] = 0x29; data[13] = 0xa8;
	}
	ret = cxd2856er_write_slvt_regs(demod, 0xa6, data, 14);
	if (ret)
		return ret;
	/*
	 * The CXD2858ER DVB-C 6 MHz profile outputs a 3.7 MHz IF.  Use the
	 * matching CXD2841ER DVB-C 6 MHz conversion (0x13bbbb); 3.55 MHz is
	 * specific to the ISDB-T tuner profile and cannot lock this signal.
	 */
	/* 参照の SONY_DEMOD_MAKE_IFFREQ_CONFIG(3.7) は四捨五入で 0x13bbbc。
	 * 従来の切り捨て値 0x13bbbb は false 時の比較基準として保持する。 */
	data[0] = 0x13; data[1] = 0xbb; data[2] = reference ? 0xbc : 0xbb;
	ret = cxd2856er_write_slvt_regs(demod, 0xb6, data, 3);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xa3, 0x14, reference ? 0xff : 0x1f);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	if (reference) {
		/* SLtoAC_BandSetting(): 5274 固定値ではなく 6 MHz の探索範囲。 */
		data[0] = 0x07; data[1] = 0x33; data[2] = 0x00; data[3] = 0xee;
		ret = cxd2856er_write_slvt_regs(demod, 0x26, data, 4);
		if (ret)
			return ret;
	} else {
		ret = cxd2856er_write_slvt_reg_mask(demod, 0xc3, 0x00, 0x04);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg_mask(demod, 0x26, 0x08, 0x0f);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg(demod, 0x27, 0x6e);
		if (ret)
			return ret;
	}

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x01;
	data[1] = 0x01;
	ret = cxd2856er_write_slvt_regs(demod, 0xce, data, 2);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80,
		(!reference && params->isdbc.modulation == CXD2856ER_ISDBC_256QAM) ? 0x28 : 0x08,
		0x1f);
	if (ret)
		return ret;
	if (!reference && params->isdbc.modulation == CXD2856ER_ISDBC_256QAM) {
		ret = cxd2856er_set_isdbc_256_tsif(demod);
		if (ret)
			return ret;
	}

	return cxd2856er_set_ts_pin_state(demod, true);
}

int cxd2856er_wakeup(struct cxd2856er_demod *demod,
		     enum cxd2856er_system system,
		     union cxd2856er_system_params *params)
{
	int ret = 0;
	const bool j83b = system == CXD2856ER_ISDB_C_SYSTEM &&
		params->isdbc.experimental_j83b;

	/* 公開系統が同じでも J.83B と DVB-C は別モード。Sony/TBS set_mcns()
	 * と同様に sleep を経由する。J.83B 再選局も完全な列で再構築する。 */
	if (demod->state == CXD2856ER_ACTIVE_STATE && (j83b || demod->j83b_active)) {
		ret = cxd2856er_sleep(demod);
		if (ret)
			return ret;
	}

	if (demod->state == CXD2856ER_ACTIVE_STATE) {
		if (demod->system == system) {
			switch (system) {
			case CXD2856ER_ISDB_T_SYSTEM:
				ret = cxd2856er_reset_isdbt(demod, params);
				break;

			case CXD2856ER_ISDB_S_SYSTEM:
				ret = cxd2856er_reset_isdbs(demod);
				break;

			case CXD2856ER_ISDB_C_SYSTEM:
				ret = cxd2856er_wakeup_isdbc(demod, params);
				break;

			default:
				ret = -EINVAL;
				break;
			}

			return ret;
		}

		cxd2856er_sleep(demod);
	}

	switch (system) {
	case CXD2856ER_ISDB_T_SYSTEM:
		ret = cxd2856er_wakeup_isdbt(demod, params);
		break;

	case CXD2856ER_ISDB_S_SYSTEM:
		ret = cxd2856er_wakeup_isdbs(demod);
		break;

	case CXD2856ER_ISDB_C_SYSTEM:
		ret = j83b ? cxd2856er_wakeup_j83b(demod) :
			cxd2856er_wakeup_isdbc(demod, params);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	if (!ret) {
		demod->system = system;
		demod->state = CXD2856ER_ACTIVE_STATE;
	}

	return ret;
}

int cxd2856er_post_tune(struct cxd2856er_demod *demod)
{
	int ret = 0;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xfe, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xc3, 0x00);
	if (ret)
		return ret;

	return 0;
}

int cxd2856er_set_slot_isdbs(struct cxd2856er_demod *demod, u16 idx)
{
	int ret = 0;
	u8 data[3];

	if (idx >= 8)
		return -EINVAL;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0xc0);
	if (ret)
		return ret;

	data[0] = 0;
	data[1] = (u8)idx;
	data[2] = 1;

	return cxd2856er_write_slvt_regs(demod, 0xe9, data, 3);
}

int cxd2856er_set_tsid_isdbs(struct cxd2856er_demod *demod, u16 tsid)
{
	int ret = 0;
	u8 data[3];

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0xc0);
	if (ret)
		return ret;

	data[0] = (tsid >> 8) & 0xff;
	data[1] = tsid & 0xff;
	data[2] = 0;

	return cxd2856er_write_slvt_regs(demod, 0xe9, data, 3);
}

int cxd2856er_is_ts_locked_isdbt(struct cxd2856er_demod *demod,
				 bool *locked, bool *unlocked)
{
	int ret = 0;
	u8 tmp;
	bool log_sample;

	*locked = false;
	*unlocked = false;
	log_sample = ((demod->isdbt_lock_checks++ % 25) == 0);

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x60);
	if (ret)
		return ret;

	ret = cxd2856er_read_slvt_reg(demod, 0x10, &tmp);
	if (ret)
		return ret;

	*locked = (tmp & 0x01) ? true : false;
	*unlocked = (tmp & 0x10) ? true : false;

	if (log_sample || *locked || *unlocked)
		dev_info(demod->dev, "CXD2856ER ISDB-T: TS lock=%u, unlock=%u, status10=0x%02x\n",
			 (*locked) ? 1 : 0, (*unlocked) ? 1 : 0, tmp);

	return 0;
}

int cxd2856er_is_ts_locked_isdbs(struct cxd2856er_demod *demod, bool *locked)
{
	int ret = 0;
	u8 tmp;

	*locked = false;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0xa0);
	if (ret)
		return ret;

	ret = cxd2856er_read_slvt_reg(demod, 0x12, &tmp);
	if (ret)
		return ret;

	*locked = (tmp & 0x40) ? true : false;

	return 0;
}

int cxd2856er_is_ts_locked_isdbc(struct cxd2856er_demod *demod, bool *locked)
{
	int ret;
	u8 status;
	bool log_sample;

	*locked = false;
	if (demod->j83b_active) {
		/* Sony/TBS read_status: Annex A/B/C 共通の bank 40:88 bit0 と
		 * 10 bit5。FEC 計数は J.83B で意味を確認できていないため読まない。 */
		u8 qam;
		ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
		if (ret)
			return ret;
		ret = cxd2856er_read_slvt_reg(demod, 0x88, &qam);
		if (ret)
			return ret;
		ret = cxd2856er_read_slvt_reg(demod, 0x10, &status);
		if (ret)
			return ret;
		*locked = (qam & 0x01) && (status & 0x20);
		if ((demod->isdbc_lock_checks++ % 25) == 0 || *locked)
			dev_info(demod->dev, "CXD2856ER J.83B: QAM sync=%u, TS lock=%u, status88=0x%02x, status10=0x%02x (unverified)\n",
				 !!(qam & 0x01), *locked, qam, status);
		return 0;
	}
	/* The lock callback runs about every 20 ms.  One sample per 500 ms keeps
	 * the diagnostic useful without drowning the actual tune events in logs. */
	log_sample = ((demod->isdbc_lock_checks++ % 25) == 0);
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_read_slvt_reg(demod, 0x88, &status);
	if (ret)
		return ret;
	if (!(status & 0x01)) {
		if (log_sample)
			dev_info(demod->dev, "CXD2856ER ISDB-C: QAM sync=0, status88=0x%02x\n", status);
		return 0;
	}
	ret = cxd2856er_read_slvt_reg(demod, 0x10, &status);
	if (ret)
		return ret;
	*locked = (status & 0x20) ? true : false;
	/* A successful lock is always interesting, even between diagnostic ticks. */
	if (log_sample || *locked)
		dev_info(demod->dev, "CXD2856ER ISDB-C: QAM sync=1, TS lock=%u, status10=0x%02x\n",
			 (*locked) ? 1 : 0, status);

	return 0;
}

int cxd2856er_read_cnr_raw_isdbt(struct cxd2856er_demod *demod, u16 *value)
{
	int ret = 0;
	u8 tmp[2];

	*value = 0;

	ret = cxd2856er_write_slvt_reg(demod, 0x01, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x60);
	if (ret)
		return ret;

	ret = cxd2856er_read_slvt_regs(demod, 0x28, tmp, 2);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x01, 0x00);
	if (ret)
		return ret;

	*value = (tmp[0] << 8) | tmp[1];

	return 0;
}

int cxd2856er_read_cnr_raw_isdbs(struct cxd2856er_demod *demod, u16 *value)
{
	int ret = 0;
	u8 tmp[3];

	*value = 0;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0xa1);
	if (ret)
		return ret;

	ret = cxd2856er_read_slvt_regs(demod, 0x10, tmp, 3);
	if (ret)
		return ret;

	if (tmp[0] & 0x01)
		*value = ((tmp[1] << 8) & 0x1f) | tmp[2];
	else
		*value = 0x5af;

	return 0;
}

int cxd2856er_read_j83b_monitor(struct cxd2856er_demod *demod,
			      struct cxd2856er_j83b_monitor *value)
{
	int ret, cleanup;
	u8 agc[2], cnr[2], offset[2] = {0}, srate[2] = {0}, timing[5] = {0};
	u8 ber[3], per[4], rs[3], interleave[3];
	u16 offset_bits;
	u32 timing_bits;
	struct cxd2856er_j83b_monitor sample = {0};
	if (!value)
		return -EINVAL;
	memset(value, 0, sizeof(*value));
	if (!demod || demod->state != CXD2856ER_ACTIVE_STATE || !demod->j83b_active)
		return -EINVAL;
	/* CXD2856 向け公開 sony_demod_j83b_monitor.c のレジスタ定義を参照。
	 * 受信設定・計数期間は変更せず、有効ビットと未校正の生値を保存する。 */
	ret = cxd2856er_write_slvt_reg(demod, 0x01, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (!ret)
		ret = cxd2856er_read_slvt_reg(demod, 0x88, &sample.ar_status_raw);
	if (!ret)
		ret = cxd2856er_read_slvt_reg(demod, 0x10, &sample.ts_status_raw);
	if (!ret)
		ret = cxd2856er_read_slvt_reg(demod, 0x19, &sample.qam);
	if (!ret)
		ret = cxd2856er_read_slvt_regs(demod, 0x49, agc, 2);
	if (!ret)
		ret = cxd2856er_read_slvt_regs(demod, 0x4c, cnr, 2);
	/* 周波数・シンボルレート推定は AR 同期成立時だけ意味を持つ。 */
	if (!ret && (sample.ar_status_raw & 0x01))
		ret = cxd2856er_read_slvt_regs(demod, 0x15, offset, 2);
	if (!ret && (sample.ar_status_raw & 0x01))
		ret = cxd2856er_read_slvt_regs(demod, 0x1a, srate, 2);
	if (!ret && (sample.ar_status_raw & 0x01))
		ret = cxd2856er_read_slvt_regs(demod, 0x9b, timing, 5);
	if (!ret)
		ret = cxd2856er_read_slvt_regs(demod, 0x62, ber, 3);
	if (!ret)
		ret = cxd2856er_read_slvt_reg(demod, 0x60, &sample.ber_period_exp);
	if (!ret)
		ret = cxd2856er_read_slvt_regs(demod, 0x5c, per, 4);
	if (!ret)
		ret = cxd2856er_read_slvt_regs(demod, 0xea, rs, 3);
	if (!ret)
		ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x80);
	if (!ret)
		ret = cxd2856er_read_slvt_reg(demod, 0x50, &sample.vq_status_raw);
	if (!ret)
		ret = cxd2856er_read_slvt_regs(demod, 0x53, interleave, 3);
	/* 途中失敗でも元のモニターバンクと freeze 解除を試み、先のエラーを保持する。 */
	cleanup = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (!ret)
		ret = cleanup;
	cleanup = cxd2856er_write_slvt_reg(demod, 0x01, 0x00);
	if (!ret)
		ret = cleanup;
	if (!ret) {
		sample.qam &= 0x07;
		sample.if_agc_raw = ((agc[0] & 0x0f) << 8) | agc[1];
		sample.cnr_raw = ((cnr[0] & 0x1f) << 8) | cnr[1];
		offset_bits = ((offset[0] & 0x3f) << 8) | offset[1];
		sample.carrier_offset_raw = (s16)((s32)offset_bits -
			((offset_bits & 0x2000) ? 0x4000 : 0));
		sample.trial_symbol_rate_raw = ((srate[0] & 0x0f) << 8) | srate[1];
		timing_bits = ((u32)timing[0] << 24) | ((u32)timing[1] << 16) |
			((u32)timing[2] << 8) | timing[3];
		sample.timing_integral_raw = (timing_bits & 0x80000000U) ?
			(-1 - (s32)(~timing_bits)) : (s32)timing_bits;
		sample.sampling_rate_mode_raw = timing[4] & 0x03;
		sample.ber_bit_errors = ((u32)(ber[0] & 0x3f) << 16) |
			((u32)ber[1] << 8) | ber[2];
		sample.ber_period_exp &= 0x1f;
		sample.ber_valid = !!(ber[0] & 0x80);
		sample.per_packet_errors = ((u32)(per[1] & 0x03) << 16) |
			((u32)per[2] << 8) | per[3];
		sample.per_period_exp = per[0] & 0x1f;
		sample.per_valid = !!(per[1] & 0x04);
		sample.rs_packet_errors = ((u16)rs[0] << 8) | rs[1];
		sample.rs_valid = !!(rs[2] & 0x01);
		sample.interleave_raw = interleave[1] & 0x0f;
		sample.interleave_valid = !!(interleave[0] & 0x01);
		sample.spectrum_raw = interleave[2] & 0x01;
		*value = sample;
	}
	return ret;
}

int cxd2856er_read_cnr_raw_isdbc(struct cxd2856er_demod *demod, u16 *value)
{
	int ret;
	u8 data[2];

	*value = 0;

	/*
	 * DVB-C/Annex-C SNR estimate.  This is the same monitor location used by
	 * the public CXD2841ER driver: SLV-T bank 0x40, registers 0x4c-0x4d.
	 */
	ret = cxd2856er_write_slvt_reg(demod, 0x01, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		goto unfreeze;
	ret = cxd2856er_read_slvt_regs(demod, 0x4c, data, 2);
	if (!ret)
		*value = ((data[0] & 0x1f) << 8) | data[1];

unfreeze:
	{
		int unfreeze_ret = cxd2856er_write_slvt_reg(demod, 0x01, 0x00);

		if (!ret)
			ret = unfreeze_ret;
	}

	return ret;
}
