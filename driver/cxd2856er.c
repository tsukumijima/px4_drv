// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony CXD2856ER driver (cxd2856er.c)
 *
 * Copyright (c) 2018-2021 nns779
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
				  enum cxd2856er_system system)
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

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xd3, 0x01, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xde, 0x00, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xda, 0x00, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xc4, 0x00, 0x03);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0xd1, 0x02, 0x03);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xd9, ts_clock_period);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x32, 0x00, 0x01);
	if (ret)
		return ret;

	switch (system) {
	case CXD2856ER_ISDB_T_SYSTEM:
	case CXD2856ER_ISDB_C_SYSTEM:
		ret = cxd2856er_write_slvt_reg_mask(demod, 0x33, 0x02, 0x03);
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

int cxd2856er_sleep(struct cxd2856er_demod *demod)
{
	if (demod->state == CXD2856ER_SLEEP_STATE)
		return -EALREADY;

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

	ret = cxd2856er_set_ts_clock(demod, CXD2856ER_ISDB_T_SYSTEM);
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

	ret = cxd2856er_set_ts_clock(demod, CXD2856ER_ISDB_S_SYSTEM);
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
static int cxd2856er_wakeup_isdbc(struct cxd2856er_demod *demod,
					  union cxd2856er_system_params *params)
{
	int ret;
	u8 data[14];

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

	dev_info(demod->dev, "CXD2856ER ISDB-C: experimental profile v11 (CXD2856ER baseline), symbol_rate=%u ksym/s, modulation=%uQAM\n",
		 params->isdbc.symbol_rate, params->isdbc.modulation);

	ret = cxd2856er_set_ts_clock(demod, CXD2856ER_ISDB_C_SYSTEM);
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
	if (params->isdbc.modulation == CXD2856ER_ISDBC_256QAM) {
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
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xd2, 0x09, 0x1f);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xa5, 0x01, 0x01);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x6a, 0x48);
	if (ret)
		return ret;

	/* 6 MHz cable filter settings. */
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x10);
	if (ret)
		return ret;
	data[0] = 0x27; data[1] = 0xa7; data[2] = 0x28; data[3] = 0xb3;
	data[4] = 0x02; data[5] = 0xf0; data[6] = 0x01; data[7] = 0xe8;
	data[8] = 0x00; data[9] = 0xcf; data[10] = 0x00; data[11] = 0xe6;
	data[12] = 0x23; data[13] = 0xa4;
	ret = cxd2856er_write_slvt_regs(demod, 0xa6, data, 14);
	if (ret)
		return ret;
	/*
	 * The CXD2858ER DVB-C 6 MHz profile outputs a 3.7 MHz IF.  Use the
	 * matching CXD2841ER DVB-C 6 MHz conversion (0x13bbbb); 3.55 MHz is
	 * specific to the ISDB-T tuner profile and cannot lock this signal.
	 */
	data[0] = 0x13; data[1] = 0xbb; data[2] = 0xbb;
	ret = cxd2856er_write_slvt_regs(demod, 0xb6, data, 3);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x11);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xa3, 0x14, 0x1f);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xc3, 0x00, 0x04);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x26, 0x08, 0x0f);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg(demod, 0x27, 0x6e);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x01;
	data[1] = 0x01;
	ret = cxd2856er_write_slvt_regs(demod, 0xce, data, 2);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80,
		(params->isdbc.modulation == CXD2856ER_ISDBC_256QAM) ? 0x28 : 0x08,
		0x1f);
	if (ret)
		return ret;
	if (params->isdbc.modulation == CXD2856ER_ISDBC_256QAM) {
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
		ret = cxd2856er_wakeup_isdbc(demod, params);
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
	u8 status, agc[2];
	u16 cnr_raw = 0;
	u8 ber[3] = {0, 0, 0};
	u8 packet_error[3] = {0, 0, 0};
	unsigned int monitor_sample;
	bool log_sample;

	*locked = false;
	/* The lock callback runs about every 20 ms.  One sample per 500 ms keeps
	 * the diagnostic useful without drowning the actual tune events in logs. */
	log_sample = ((demod->isdbc_lock_checks++ % 25) == 0);
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_read_slvt_regs(demod, 0x49, agc, 2);
	if (ret)
		return ret;
	ret = cxd2856er_read_slvt_reg(demod, 0x88, &status);
	if (ret)
		return ret;
	if (!(status & 0x01)) {
		if (log_sample) {
			/* This monitor is read-only and does not affect the demodulator.
			 * It lets C43/C45 failures be compared even before TS lock. */
			ret = cxd2856er_read_cnr_raw_isdbc(demod, &cnr_raw);
			if (ret)
				dev_warn(demod->dev, "CXD2856ER ISDB-C: C/N monitor read failed (%d)\n", ret);
			dev_info(demod->dev, "CXD2856ER ISDB-C: QAM sync=0, AGC=0x%03x, CNRraw=0x%04x, status88=0x%02x\n",
				 ((agc[0] & 0x0f) << 8) | agc[1], cnr_raw, status);
		}
		return 0;
	}
	ret = cxd2856er_read_slvt_reg(demod, 0x10, &status);
	if (ret)
		return ret;
	*locked = (status & 0x20) ? true : false;
	/* A successful lock is always interesting, even between diagnostic ticks. */
	dev_info(demod->dev, "CXD2856ER ISDB-C: QAM sync=1, TS lock=%u, AGC=0x%03x, status10=0x%02x\n",
			 (*locked) ? 1 : 0, ((agc[0] & 0x0f) << 8) | agc[1], status);

	/*
	 * Read-only DVB-C monitor values.  They distinguish a demodulator that
	 * has actually accumulated FEC/packet statistics from one that only
	 * reports the coarse lock bits.  This is especially useful for the
	 * experimental J.83C-256QAM path where the USB bridge can otherwise only
	 * report an undifferentiated 0 Mbps result.
	 */
	/*
	 * A caller normally stops polling as soon as TS lock is reported.  The
	 * error counters are not necessarily valid at that instant, so take three
	 * read-only snapshots over 500 ms before returning the successful lock.
	 * This affects the experimental ISDB-C tune latency only; it never writes
	 * a demodulator register.
	 */
	if (log_sample || *locked) {
		for (monitor_sample = 0; monitor_sample < 3; monitor_sample++) {
			if (monitor_sample)
				msleep(250);
			ret = cxd2856er_read_slvt_regs(demod, 0x62, ber, sizeof(ber));
			if (ret)
				return ret;
			ret = cxd2856er_read_slvt_regs(demod, 0xea, packet_error,
							       sizeof(packet_error));
			if (ret)
				return ret;
			dev_info(demod->dev,
				 "CXD2856ER ISDB-C: FEC monitor[%u] ber=%02x:%02x%02x (valid=%u), packet_error=%02x%02x (valid=%u)\n",
				 monitor_sample, ber[0], ber[1], ber[2],
				 (ber[0] & 0x80) ? 1 : 0, packet_error[0],
				 packet_error[1], (packet_error[2] & 0x01) ? 1 : 0);
		}
	}

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
