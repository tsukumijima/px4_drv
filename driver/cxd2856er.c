// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony CXD2856ER driver (cxd2856er.c)
 *
 * Copyright (c) 2018-2021 nns779
 *
 * ISDB-C 用に参照した Linux cxd2841er.c (GPL-2.0-or-later):
 * https://github.com/torvalds/linux/blob/v6.12/drivers/media/dvb-frontends/cxd2841er.c
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 *
 * J.83B 用に参照した cxd2878.c:
 * https://github.com/koreapyj/cxd28xx/blob/c5fa27eecbe80633cbb365cc44656daea1c0e2e3/cxd2878.c
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
	demod->isdbc_saved = false;
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

	/* 地上波と同じ TS 出力の設定では 256QAM の TS が IT930x へ届かないため、ISDB-C は公開 DVB-C ドライバーと同じ設定にする */
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xd1,
					   (system == CXD2856ER_ISDB_C_SYSTEM) ? 0x00 : 0x02, 0x03);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg(demod, 0xd9,
				      (system == CXD2856ER_ISDB_C_SYSTEM) ? 0x08 : 0x10);
	if (ret)
		return ret;

	ret = cxd2856er_write_slvt_reg_mask(demod, 0x32, 0x00, 0x01);
	if (ret)
		return ret;

	switch (system) {
	case CXD2856ER_ISDB_T_SYSTEM:
		ret = cxd2856er_write_slvt_reg_mask(demod, 0x33, 0x02, 0x03);
		break;

	case CXD2856ER_ISDB_S_SYSTEM:
	case CXD2856ER_ISDB_C_SYSTEM:
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

/* ISDB-C の起動で書き換え、ISDB-T の起動では書き直されないレジスター (バンクとアドレス) */
static const u8 cxd2856er_isdbc_regs[5][2] = {
	{ 0x10, 0xd2 },
	{ 0x11, 0xa3 },
	{ 0x40, 0x26 },
	{ 0x40, 0x27 },
	{ 0x40, 0xc3 }
};

/* ISDB-C の後の地デジが ISDB-C を使う前と同じ状態で動くよう、ISDB-C の設定前の値を保存する */
static int cxd2856er_save_isdbc_regs(struct cxd2856er_demod *demod)
{
	int ret, i;

	for (i = 0; i < 5; i++) {
		ret = cxd2856er_write_slvt_reg(demod, 0x00, cxd2856er_isdbc_regs[i][0]);
		if (ret)
			return ret;
		ret = cxd2856er_read_slvt_reg(demod, cxd2856er_isdbc_regs[i][1], &demod->isdbc_saved_regs[i]);
		if (ret)
			return ret;
	}

	demod->isdbc_saved = true;
	return 0;
}

static int cxd2856er_restore_isdbc_regs(struct cxd2856er_demod *demod)
{
	int ret, i;

	if (!demod->isdbc_saved)
		return 0;

	for (i = 0; i < 5; i++) {
		ret = cxd2856er_write_slvt_reg(demod, 0x00, cxd2856er_isdbc_regs[i][0]);
		if (!ret)
			ret = cxd2856er_write_slvt_reg(demod, cxd2856er_isdbc_regs[i][1], demod->isdbc_saved_regs[i]);
		/* 失敗したときは保存した値を残し、次に ISDB-T/S を起動するときに書き戻しをやり直す */
		if (ret)
			return ret;
	}

	demod->isdbc_saved = false;
	return 0;
}

/* cxd2878_sleep() の SONY_DTV_SYSTEM_J83B に従い、共通のアナログ停止処理の前に J.83B 固有の設定を戻す */
static int cxd2856er_sleep_j83b(struct cxd2856er_demod *demod)
{
	int ret;
	u8 data[4];
	/* レジスターの設定を戻す間は TS 出力を停止しておく */
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
	case CXD2856ER_ISDB_C_SYSTEM:
		/* 書き戻しに失敗しても停止は続け、保存した値は次の ISDB-T/S の起動で書き戻す */
		cxd2856er_restore_isdbc_regs(demod);
		/* ISDB-C は地上波と同じアナログ受信経路を使うため、ISDB-T と同じ手順で停止する */
		cxd2856er_sleep_isdbt(demod);
		break;

	case CXD2856ER_ISDB_T_SYSTEM:
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

/* cxd2878.c の SLtoACC() と SLtoACC_BandSetting() のうち、5.6 Msps 用の設定を使う
 * IF は SONY_DEMOD_MAKE_IFFREQ_CONFIG と同じく round(3.75 / 48 * 2^24) = 0x140000
 * 48 MHz は復調器内部の基準クロックで、外部水晶の周波数とは異なる
 */
static int cxd2856er_wakeup_j83b(struct cxd2856er_demod *demod)
{
	int ret;
	u8 data[14];
	u8 tmp;

	/* J.83B はレート制御を有効にし、シリアル出力を連続クロック・フルレートにする
	 * パラレル出力では C4/D1 の設定を保持する */
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x00);
	if (ret)
		return ret;
	ret = cxd2856er_read_slvt_reg(demod, 0xc4, &tmp);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xd3, 0x00, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xde, 0x00, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0xda, 0x00, 0x01);
	if (ret)
		return ret;
	if (tmp & 0x80) {
		ret = cxd2856er_write_slvt_reg_mask(demod, 0xc4, 0x01, 0x03);
		if (ret)
			return ret;
		ret = cxd2856er_write_slvt_reg_mask(demod, 0xd1, 0x01, 0x03);
		if (ret)
			return ret;
	}
	ret = cxd2856er_write_slvt_reg(demod, 0xd9, 0x08);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x32, 0x00, 0x01);
	if (ret)
		return ret;
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x33, 0x01, 0x03);
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
	/* 途中で I/O が失敗しても、次回の停止時に J.83B 固有の設定を解除できるよう状態を保持する */
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

static int cxd2856er_wakeup_isdbc(struct cxd2856er_demod *demod)
{
	int ret;
	u8 data[14];

	if (!demod->isdbc_saved) {
		ret = cxd2856er_save_isdbc_regs(demod);
		if (ret)
			return ret;
	}

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
	/* アナログ受信経路の基本設定は CXD2856ER の ISDB-T と共通 */
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

	/* 6 MHz ケーブル信号用のフィルター係数 */
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
	/* チューナーの DVB-C 6 MHz 設定が出力する IF 3.7 MHz に合わせる */
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
	ret = cxd2856er_write_slvt_reg_mask(demod, 0x80, 0x08, 0x1f);
	if (ret)
		return ret;

	return cxd2856er_set_ts_pin_state(demod, true);
}

int cxd2856er_wakeup(struct cxd2856er_demod *demod,
		     enum cxd2856er_system system,
		     union cxd2856er_system_params *params)
{
	int ret = 0;
	const bool j83b = system == CXD2856ER_ISDB_C_SYSTEM &&
		params->isdbc.j83b;

	/* ISDB-C の起動が途中で失敗して停止処理を通らなかった場合も、ISDB-T/S の起動の前に書き戻す
	 * 書き戻せないまま起動すると ISDB-C の設定が残った状態で受信するため、起動を中断する */
	if (system != CXD2856ER_ISDB_C_SYSTEM) {
		ret = cxd2856er_restore_isdbc_regs(demod);
		if (ret)
			return ret;
	}

	/* J.83B と ISDB-C は内部の設定が異なるため、停止状態を経由して再設定する */
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
				ret = cxd2856er_wakeup_isdbc(demod);
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
			cxd2856er_wakeup_isdbc(demod);
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

	*locked = false;
	*unlocked = false;

	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x60);
	if (ret)
		return ret;

	ret = cxd2856er_read_slvt_reg(demod, 0x10, &tmp);
	if (ret)
		return ret;

	*locked = (tmp & 0x01) ? true : false;
	*unlocked = (tmp & 0x10) ? true : false;

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

	*locked = false;
	ret = cxd2856er_write_slvt_reg(demod, 0x00, 0x40);
	if (ret)
		return ret;
	ret = cxd2856er_read_slvt_reg(demod, 0x88, &status);
	if (ret)
		return ret;
	if (!(status & 0x01))
		return 0;
	ret = cxd2856er_read_slvt_reg(demod, 0x10, &status);
	if (ret)
		return ret;
	*locked = (status & 0x20) ? true : false;

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

	/* cxd2841er.c と同様に、SLV-T バンク 0x40 の 0x4c〜0x4d から C/N 推定用の生値を読む */
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
