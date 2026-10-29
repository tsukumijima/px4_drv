// SPDX-License-Identifier: GPL-2.0-only
/*
 * PTX driver for PLEX PX-MLT series devices (pxmlt_device.c)
 *
 * Copyright (c) 2018-2021 nns779
 */

#include "print_format.h"
#include "pxmlt_device.h"

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "px4_device_params.h"
#include "firmware.h"
#include "ts_sync.h"

#define PXMLT_DEVICE_TS_SYNC_COUNT	4
#define PXMLT_DEVICE_TS_SYNC_SIZE	(188 * PXMLT_DEVICE_TS_SYNC_COUNT)

struct pxmlt_stream_context {
	struct ptx_chrdev *chrdev[PXMLT_CHRDEV_MAX_NUM];
	u8 remain_buf[PXMLT_DEVICE_TS_SYNC_SIZE];
	size_t remain_len;
};

static int pxmlt_chrdev_set_lnb_voltage(struct ptx_chrdev *chrdev, int voltage);
static void pxmlt_device_release(struct kref *kref);

static int pxmlt_backend_set_power(struct pxmlt_device *pxmlt, bool state)
{
	int ret = 0, i;
	struct it930x_bridge *it930x = &pxmlt->it930x;

	dev_dbg(pxmlt->dev,
		"pxmlt_backend_set_power: %s\n", (state) ? "true" : "false");

	if (!state && !atomic_read(&pxmlt->available))
		return 0;

	if (state) {
		ret = it930x_write_gpio(it930x, 7, false);
		if (ret)
			return ret;

		msleep(80);

		ret = it930x_write_gpio(it930x, 2, true);
		if (ret)
			return ret;

		msleep(20);

		/* 電源を入れた直後のチューナーは、PLEX の公開ドライバーと同じ既定の待機状態から始める */
		for (i = 0; i < pxmlt->chrdevm_num; i++)
			pxmlt->chrdevm[i].cxd2858er.config.ter.rfin_matching = false;
	} else {
		it930x_write_gpio(it930x, 2, false);
		it930x_write_gpio(it930x, 7, true);
	}

	return 0;
}

#if 0
static int pxmlt_backend_init(struct pxmlt_device *pxmlt)
{
	int ret = 0, i;

	for (i = 0; i < pxmlt->chrdevm_num; i++) {
		struct pxmlt_chrdev *chrdevm = &pxmlt->chrdevm[i];

		ret = cxd2856er_init(&chrdevm->cxd2856er);
		if (ret) {
			dev_err(pxmlt->dev,
				"pxmlt_backend_init: cxd2856er_init() failed. (i: %d, ret: %d)\n",
				i, ret);
			break;
		}

		mutex_lock(chrdevm->tuner_lock);
		ret = cxd2858er_init(&chrdevm->cxd2858er);
		mutex_unlock(chrdevm->tuner_lock);

		if (ret) {
			dev_err(pxmlt->dev,
				"pxmlt_backend_init: cxd2858er_init() failed. (i: %d, ret: %d)\n",
				i, ret);
			break;
		}
	}

	return ret;
}

static int pxmlt_backend_term(struct pxmlt_device *pxmlt)
{
	int i;

	for (i = 0; i < pxmlt->chrdevm_num; i++) {
		struct pxmlt_chrdev *chrdevm = &pxmlt->chrdevm[i];

		mutex_lock(chrdevm->tuner_lock);
		cxd2858er_term(&chrdevm->cxd2858er);
		mutex_unlock(chrdevm->tuner_lock);

		cxd2856er_term(&chrdevm->cxd2856er);
	}

	return 0;
}
#endif

static void pxmlt_device_stream_process(struct ptx_chrdev **chrdev,
				      u8 **buf, u32 *len)
{
	u8 *p = *buf;
	u32 remain = *len;

	while (likely(remain)) {
		u32 i;
		bool sync_remain = false;

		for (i = 0; i < PXMLT_DEVICE_TS_SYNC_COUNT; i++) {
			if (likely(((i + 1) * 188) <= remain)) {
				if (unlikely(!px4_ts_has_tagged_sync(p[i * 188])))
					break;
			} else {
				sync_remain = true;
				break;
			}
		}

		if (unlikely(sync_remain))
			break;

		if (unlikely(i < PXMLT_DEVICE_TS_SYNC_COUNT)) {
			p++;
			remain--;
			continue;
		}

		while (likely(remain >= 188 && ((p[0] & 0x8f) == 0x07))) {
			u8 id = (p[0] & 0x70) >> 4;

			if (likely(id && id < 6)) {
				p[0] = 0x47;
				ptx_chrdev_put_stream(chrdev[id - 1], p, 188);
			}

			p += 188;
			remain -= 188;
		}
	}

	*buf = p;
	*len = remain;

	return;
}

static int pxmlt_device_stream_handler(void *context, void *buf, u32 len)
{
	struct pxmlt_stream_context *stream_ctx = context;
	u8 *ctx_remain_buf = stream_ctx->remain_buf;
	u32 ctx_remain_len = stream_ctx->remain_len;
	u8 *p = buf;
	u32 remain = len;

	if (unlikely(ctx_remain_len)) {
		if (likely((ctx_remain_len + len) >= PXMLT_DEVICE_TS_SYNC_SIZE)) {
			u32 t = PXMLT_DEVICE_TS_SYNC_SIZE - ctx_remain_len;

			memcpy(ctx_remain_buf + ctx_remain_len, p, t);
			ctx_remain_len = PXMLT_DEVICE_TS_SYNC_SIZE;

			pxmlt_device_stream_process(stream_ctx->chrdev,
						  &ctx_remain_buf,
						  &ctx_remain_len);
			if (likely(!ctx_remain_len)) {
				p += t;
				remain -= t;
			}

			stream_ctx->remain_len = 0;
		} else {
			memcpy(ctx_remain_buf + ctx_remain_len, p, len);
			stream_ctx->remain_len += len;

			return 0;
		}
	}

	pxmlt_device_stream_process(stream_ctx->chrdev, &p, &remain);

	if (unlikely(remain)) {
		memcpy(stream_ctx->remain_buf, p, remain);
		stream_ctx->remain_len = remain;
	}

	return 0;
}

static int pxmlt_chrdev_init(struct ptx_chrdev *chrdev)
{
	dev_dbg(chrdev->parent->dev, "pxmlt_chrdev_init\n");

	chrdev->params.system = PTX_UNSPECIFIED_SYSTEM;
	return 0;
}

static int pxmlt_chrdev_term(struct ptx_chrdev *chrdev)
{
	dev_dbg(chrdev->parent->dev, "pxmlt_chrdev_term\n");
	return 0;
}

static int pxmlt_chrdev_open(struct ptx_chrdev *chrdev)
{
	int ret = 0;
	struct ptx_chrdev_group *chrdev_group = chrdev->parent;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_open %u:%u\n", chrdev_group->id, chrdev->id);

	mutex_lock(&pxmlt->lock);

	if (!pxmlt->open_count && !pxmlt->card_powered) {
		ret = pxmlt_backend_set_power(pxmlt, true);
		if (ret) {
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_open %u:%u: pxmlt_backend_set_power(true) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, ret);
			goto fail_backend_power;
		}
	}

	ret = cxd2856er_init(&chrdevm->cxd2856er);
	if (ret) {
		dev_err(pxmlt->dev,
			"pxmlt_chrdev_open %u:%u: cxd2856er_init() failed. (ret: %d)\n",
			chrdev_group->id, chrdev->id, ret);
		goto fail_demod_init;
	}

	mutex_lock(chrdevm->tuner_lock);
	ret = cxd2858er_init(&chrdevm->cxd2858er);
	mutex_unlock(chrdevm->tuner_lock);

	if (ret) {
		dev_err(pxmlt->dev,
			"pxmlt_chrdev_open %u:%u: cxd2858er_init() failed. (ret: %d)\n",
			chrdev_group->id, chrdev->id, ret);
		goto fail_tuner_init;
	}

	ret = cxd2856er_write_slvt_reg(&chrdevm->cxd2856er, 0x00, 0x00);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg_mask(&chrdevm->cxd2856er,
					    0xc4, 0x80, 0x88);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg_mask(&chrdevm->cxd2856er,
					    0xc5, 0x01, 0x01);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg_mask(&chrdevm->cxd2856er,
					    0xc6, 0x03, 0x1f);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg(&chrdevm->cxd2856er, 0x00, 0x60);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg_mask(&chrdevm->cxd2856er,
					    0x52, 0x03, 0x1f);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg(&chrdevm->cxd2856er, 0x00, 0x00);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg_mask(&chrdevm->cxd2856er,
					    0xc8, 0x03, 0x1f);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg_mask(&chrdevm->cxd2856er,
					    0xc9, 0x03, 0x1f);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg(&chrdevm->cxd2856er, 0x00, 0xa0);
	if (ret)
		goto fail_backend;

	ret = cxd2856er_write_slvt_reg_mask(&chrdevm->cxd2856er,
					    0xb9, 0x01, 0x01);
	if (ret)
		goto fail_backend;

	pxmlt->open_count++;
	kref_get(&pxmlt->kref);

	mutex_unlock(&pxmlt->lock);
	return 0;

fail_backend:
	mutex_lock(chrdevm->tuner_lock);
	cxd2858er_term(&chrdevm->cxd2858er);
	mutex_unlock(chrdevm->tuner_lock);

fail_tuner_init:
	cxd2856er_term(&chrdevm->cxd2856er);

fail_demod_init:
	if (!pxmlt->open_count && !pxmlt->card_powered)
		pxmlt_backend_set_power(pxmlt, false);

fail_backend_power:
	mutex_unlock(&pxmlt->lock);
	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_open %u:%u: ret: %d\n",
		chrdev_group->id, chrdev->id, ret);
	return ret;
}

static int pxmlt_chrdev_release(struct ptx_chrdev *chrdev)
{
	struct ptx_chrdev_group *chrdev_group = chrdev->parent;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_release %u:%u\n", chrdev_group->id, chrdev->id);

	pxmlt_chrdev_set_lnb_voltage(chrdev, 0);

	mutex_lock(&pxmlt->lock);

	if (!pxmlt->open_count) {
		mutex_unlock(&pxmlt->lock);
		return -EALREADY;
	}

	mutex_lock(chrdevm->tuner_lock);
	cxd2858er_term(&chrdevm->cxd2858er);
	mutex_unlock(chrdevm->tuner_lock);

	cxd2856er_term(&chrdevm->cxd2856er);

	pxmlt->open_count--;
	if (!pxmlt->open_count && !pxmlt->card_powered)
		pxmlt_backend_set_power(pxmlt, false);

	if (kref_put(&pxmlt->kref, pxmlt_device_release))
		return 0;

	mutex_unlock(&pxmlt->lock);
	return 0;
}

static int pxmlt_chrdev_set_null_packet_filter(struct ptx_chrdev *chrdev, bool enable)
{
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;
	struct it930x_pid_filter filter;
	int ret;

	if (!chrdev->discard_null_packets ||
	    chrdevm->null_packet_filter_disabled == !enable)
		return 0;

	filter.block = true;
	filter.num = 1;
	filter.pid[0] = 0x1fff;

	mutex_lock(&pxmlt->lock);
	ret = it930x_set_pid_filter(&pxmlt->it930x, chrdev->id,
				  enable ? &filter : NULL);
	if (!ret)
		chrdevm->null_packet_filter_disabled = !enable;
	mutex_unlock(&pxmlt->lock);

	return ret;
}

static int pxmlt_chrdev_tune(struct ptx_chrdev *chrdev,
			     struct ptx_tune_params *params)
{
	int ret = 0, i;
	struct ptx_chrdev_group *chrdev_group = chrdev->parent;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;
	union cxd2856er_system_params demod_params;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_tune %u:%u\n", chrdev_group->id, chrdev->id);

	memset(&demod_params, 0, sizeof(demod_params));

	if (chrdevm->null_packet_filter_disabled && params->system != PTX_ISDB_C_SYSTEM) {
		ret = pxmlt_chrdev_set_null_packet_filter(chrdev, true);
		if (ret)
			return ret;
	}

	switch (params->system) {
	case PTX_ISDB_T_SYSTEM:
		demod_params.bandwidth = params->bandwidth;

		ret = cxd2856er_wakeup(&chrdevm->cxd2856er,
				       CXD2856ER_ISDB_T_SYSTEM, &demod_params);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_tune %u:%u: cxd2856er_wakeup(CXD2856ER_ISDB_T_SYSTEM) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, ret);

		break;

	case PTX_ISDB_S_SYSTEM:
		demod_params.bandwidth = 0;

		ret = cxd2856er_wakeup(&chrdevm->cxd2856er,
				       CXD2856ER_ISDB_S_SYSTEM, &demod_params);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_tune %u:%u: cxd2856er_wakeup(CXD2856ER_ISDB_S_SYSTEM) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, ret);

		break;

	case PTX_ISDB_C_SYSTEM:
	case PTX_J83B_SYSTEM:
		/* チューナーが待機と動作を切り替えるときに RF 入力の整合が変わり、隣のチューナーで受信中の 256QAM に誤りが出る
		 * 地デジと BS/CS の受信ではこの誤りは出ないため、電源投入から初めて ISDB-C か J.83B を選局するときにだけ、全チューナーを待機中も整合を保つ設定 (rfin_matching) にする
		 * 閉じているチューナーは、256QAM の受信が始まる前にこの設定で初期化し直して待機させる
		 * 開いているチューナーの I2C は、その受信機の復調器のバンクの選択と同じ経路を通るため、ここからは書き込まず、次に待機へ戻るときにこの設定を書き込ませる
		 * 初期化は1台ずつ、開く処理と同じ時間だけデバイスのロックを持って行い、ほかの受信機の操作とカードの通信を長く待たせない */
		for (i = 0; i < pxmlt->chrdevm_num; i++) {
			struct pxmlt_chrdev *other = &pxmlt->chrdevm[i];

			mutex_lock(&pxmlt->lock);

			if (other->cxd2858er.config.ter.rfin_matching) {
				mutex_unlock(&pxmlt->lock);
				continue;
			}

			other->cxd2858er.config.ter.rfin_matching = true;

			if (!atomic_read(&other->chrdev->open)) {
				ret = cxd2856er_init(&other->cxd2856er);
				if (!ret) {
					mutex_lock(other->tuner_lock);
					ret = cxd2858er_init(&other->cxd2858er);
					mutex_unlock(other->tuner_lock);
				}
				cxd2856er_term(&other->cxd2856er);

				if (ret) {
					/* 初期化できなかったチューナーは、次に ISDB-C か J.83B を選局するときに初期化し直す */
					dev_err(pxmlt->dev,
						"pxmlt_chrdev_tune %u:%u: tuner %d initialization failed. (ret: %d)\n",
						chrdev_group->id, chrdev->id, i, ret);
					other->cxd2858er.config.ter.rfin_matching = false;
					ret = 0;
				}
			}

			mutex_unlock(&pxmlt->lock);
		}

		/* TSMF はパケットの位置で TS を選び、ヘッダーの有無も受信した TS から判断するため、ISDB-C では null パケットも受け取る
		 * J.83 Annex B は TSMF を使わないため、地デジと同じくブリッジで除く */
		ret = pxmlt_chrdev_set_null_packet_filter(chrdev,
			params->system == PTX_J83B_SYSTEM);
		if (ret)
			break;

		ret = cxd2856er_wakeup(&chrdevm->cxd2856er,
				       (params->system == PTX_J83B_SYSTEM) ? CXD2856ER_J83B_SYSTEM : CXD2856ER_ISDB_C_SYSTEM,
				       &demod_params);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_tune %u:%u: cxd2856er_wakeup(ISDB-C / J.83B) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, ret);

		break;

	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	mutex_lock(chrdevm->tuner_lock);

	switch (params->system) {
	case PTX_ISDB_T_SYSTEM:
		ret = cxd2858er_set_params_t(&chrdevm->cxd2858er,
					     CXD2858ER_ISDB_T_SYSTEM,
					     params->freq, 6);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_tune %u:%u: cxd2858er_set_params_t(%u, 6) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id,
				params->freq, ret);

		break;

	case PTX_ISDB_S_SYSTEM:
		ret = cxd2858er_set_params_s(&chrdevm->cxd2858er,
					     CXD2858ER_ISDB_S_SYSTEM,
					     params->freq, 28860);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_tune %u:%u: cxd2858er_set_params_s(%u, 28860) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id,
				params->freq, ret);

		break;

	case PTX_ISDB_C_SYSTEM:
	case PTX_J83B_SYSTEM:
		ret = cxd2858er_set_params_c(&chrdevm->cxd2858er,
					     params->freq, params->system == PTX_J83B_SYSTEM);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_tune %u:%u: cxd2858er_set_params_c(%u) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id,
				params->freq, ret);

		break;

	default:
		break;
	}

	mutex_unlock(chrdevm->tuner_lock);

	if (ret)
		return ret;

	ret = cxd2856er_post_tune(&chrdevm->cxd2856er);
	if (ret) {
		dev_err(pxmlt->dev,
			"pxmlt_chrdev_tune %u:%u: cxd2856er_post_tune() failed. (ret: %d)\n",
			chrdev_group->id, chrdev->id, ret);
		return ret;
	}

	return 0;
}

static int pxmlt_chrdev_check_lock(struct ptx_chrdev *chrdev, bool *locked)
{
	int ret = 0;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	bool unlocked = false;

	switch (chrdev->current_system) {
	case PTX_ISDB_T_SYSTEM:
		ret = cxd2856er_is_ts_locked_isdbt(&chrdevm->cxd2856er,
						   locked, &unlocked);
		if (!ret && unlocked)
			ret = -ECANCELED;

		break;

	case PTX_ISDB_S_SYSTEM:
		ret = cxd2856er_is_ts_locked_isdbs(&chrdevm->cxd2856er, locked);
		break;

	/* J.83B も ISDB-C と同じ SLV-T バンク 0x40 のロックの表示を使う */
	case PTX_ISDB_C_SYSTEM:
	case PTX_J83B_SYSTEM:
		ret = cxd2856er_is_ts_locked_isdbc(&chrdevm->cxd2856er, locked);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int pxmlt_chrdev_set_stream_id(struct ptx_chrdev *chrdev, u16 stream_id)
{
	int ret = 0;
	struct ptx_chrdev_group *chrdev_group = chrdev->parent;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_set_stream_id %u:%u\n",
		chrdev_group->id, chrdev->id);

	if (stream_id < 12) {
		ret = cxd2856er_set_slot_isdbs(&chrdevm->cxd2856er, stream_id);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_set_stream_id %u:%u: cxd2856er_set_slot_isdbs(%u) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, stream_id, ret);
	} else {
		ret = cxd2856er_set_tsid_isdbs(&chrdevm->cxd2856er, stream_id);
		if (ret)
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_set_stream_id %u:%u: cxd2856er_set_tsid_isdbs(%u) failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, stream_id, ret);
	}

	return ret;
}

static int pxmlt_chrdev_set_lnb_voltage(struct ptx_chrdev *chrdev, int voltage)
{
	int ret = 0;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_set_lnb_voltage %u:%u voltage: %d\n",
		chrdev->parent->id, chrdev->id, voltage);

	if (voltage != 0 && voltage != 15)
		return -EINVAL;

	if (chrdevm->lnb_power == !!voltage)
		return 0;

	if (!voltage && !atomic_read(&pxmlt->available))
		return 0;

	mutex_lock(&pxmlt->lock);

	if (!voltage)
		pxmlt->lnb_power_count--;

	if (!pxmlt->lnb_power_count) {
		ret = it930x_write_gpio(&pxmlt->it930x, 11, !!voltage);
		if (ret && voltage)
			goto exit;
	}

	if (voltage)
		pxmlt->lnb_power_count++;

	chrdevm->lnb_power = !!voltage;

exit:
	mutex_unlock(&pxmlt->lock);
	return ret;
}

static int pxmlt_chrdev_start_capture(struct ptx_chrdev *chrdev)
{
	int ret = 0;
	struct ptx_chrdev_group *chrdev_group = chrdev->parent;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_start_capture %u:%u\n",
		chrdev_group->id, chrdev->id);

	mutex_lock(&pxmlt->lock);

	if (!pxmlt->streaming_count) {
		struct pxmlt_stream_context *stream_ctx = pxmlt->stream_ctx;

		ret = it930x_purge_psb(&pxmlt->it930x,
				       px4_device_params.psb_purge_timeout);
		if (ret) {
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_start_capture %u:%u: it930x_purge_psb() failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, ret);
			goto exit;
		}

		stream_ctx->remain_len = 0;

		ret = itedtv_bus_start_streaming(&pxmlt->it930x.bus,
						 pxmlt_device_stream_handler,
						 stream_ctx);
		if (ret) {
			dev_err(pxmlt->dev,
				"pxmlt_chrdev_start_capture %u:%u: itedtv_bus_start_streaming() failed. (ret: %d)\n",
				chrdev_group->id, chrdev->id, ret);
			goto exit;
		}
	}

	pxmlt->streaming_count++;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_start_capture %u:%u: streaming_count: %u\n",
		chrdev_group->id, chrdev->id, pxmlt->streaming_count);

exit:
	mutex_unlock(&pxmlt->lock);
	return ret;
}

static int pxmlt_chrdev_stop_capture(struct ptx_chrdev *chrdev)
{
	struct ptx_chrdev_group *chrdev_group = chrdev->parent;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;
	struct pxmlt_device *pxmlt = chrdevm->parent;

	dev_dbg(pxmlt->dev,
		"pxmlt_chrdev_stop_capture %u:%u\n",
		chrdev_group->id, chrdev->id);

	mutex_lock(&pxmlt->lock);

	if (!pxmlt->streaming_count) {
		mutex_unlock(&pxmlt->lock);
		return -EALREADY;
	}

	pxmlt->streaming_count--;
	if (!pxmlt->streaming_count) {
		dev_dbg(pxmlt->dev,
			"pxmlt_chrdev_stop_capture %u:%u: stopping...\n",
			chrdev_group->id, chrdev->id);
		itedtv_bus_stop_streaming(&pxmlt->it930x.bus);
	} else {
		dev_dbg(pxmlt->dev,
			"pxmlt_chrdev_stop_capture %u:%u: streaming_count: %u\n",
			chrdev_group->id, chrdev->id, pxmlt->streaming_count);
	}

	mutex_unlock(&pxmlt->lock);
	return 0;
}

static int pxmlt_chrdev_set_capture(struct ptx_chrdev *chrdev, bool status)
{
	return (status) ? pxmlt_chrdev_start_capture(chrdev)
			: pxmlt_chrdev_stop_capture(chrdev);
}

/* 10 to 34 dB, 0.5 dB unit */
static const struct {
	u16 val;
	u32 cn;
} isdbt_cn_raw_table[] = {
	{ 0x51, 0xb19ff }, { 0x5a, 0x9eecd }, { 0x65, 0x8cd8b },
	{ 0x72, 0x7c302 }, { 0x7f, 0x6f132 }, { 0x8f, 0x6250d },
	{ 0xa0, 0x57a1c }, { 0xb4, 0x4db45 }, { 0xc9, 0x45725 },
	{ 0xe2, 0x3da59 }, { 0xfd, 0x36f9d }, { 0x11c, 0x30e58 },
	{ 0x13f, 0x2b76e }, { 0x166, 0x26abb }, { 0x191, 0x22794 },
	{ 0x1c2, 0x1eac7 }, { 0x1f9, 0x1b4a8 }, { 0x237, 0x1844d },
	{ 0x27c, 0x159a2 }, { 0x2ca, 0x13365 }, { 0x321, 0x11196 },
	{ 0x382, 0xf3ae }, { 0x3f0, 0xd8cb }, { 0x46b, 0xc0fd },
	{ 0x4f4, 0xabf9 }, { 0x58f, 0x9923 }, { 0x63d, 0x8868 },
	{ 0x700, 0x7995 }, { 0x7da, 0x6c78 }, { 0x8cf, 0x60cf },
	{ 0x9e2, 0x5675 }, { 0xb17, 0x4d43 }, { 0xc71, 0x4520 },
	{ 0xdf6, 0x3de4 }, { 0xfaa, 0x377b }, { 0x1193, 0x31cc },
	{ 0x13b8, 0x2cbf }, { 0x1620, 0x2843 }, { 0x18d3, 0x2447 },
	{ 0x1bdb, 0x20bb }, { 0x1f41, 0x1d95 }, { 0x2311, 0x1ac6 },
	{ 0x2758, 0x1846 }, { 0x2c25, 0x160a }, { 0x3188, 0x140c },
	{ 0x3793, 0x1243 }, { 0x3e5b, 0x10ab }, { 0x45f7, 0xf3c },
	{ 0x4e80, 0xdf3 }
};

/* 0 to 20 dB, 0.1 dB unit */
static const struct {
	u16 val;
	u32 cn;
} isdbs_cn_raw_table[] = {
	{ 0x5af, 0x9546 }, { 0x597, 0x94d9 }, { 0x57e, 0x946b },
	{ 0x567, 0x93fc }, { 0x550, 0x938c }, { 0x539, 0x931b },
	{ 0x522, 0x92a8 }, { 0x50c, 0x9235 }, { 0x4f6, 0x91c1 },
	{ 0x4e1, 0x914b }, { 0x4cc, 0x90d5 }, { 0x4b6, 0x905d },
	{ 0x4a1, 0x8fe4 }, { 0x48c, 0x8f6a }, { 0x477, 0x8eef },
	{ 0x463, 0x8e72 }, { 0x44f, 0x8df5 }, { 0x43c, 0x8d76 },
	{ 0x428, 0x8cf5 }, { 0x416, 0x8c74 }, { 0x403, 0x8bf1 },
	{ 0x3ef, 0x8b6c }, { 0x3dc, 0x8ae7 }, { 0x3c9, 0x8a60 },
	{ 0x3b6, 0x89d7 }, { 0x3a4, 0x894d }, { 0x392, 0x88c2 },
	{ 0x381, 0x8835 }, { 0x36f, 0x87a6 }, { 0x35f, 0x8716 },
	{ 0x34e, 0x8685 }, { 0x33d, 0x85f1 }, { 0x32d, 0x855d },
	{ 0x31d, 0x84c6 }, { 0x30d, 0x842e }, { 0x2fd, 0x8394 },
	{ 0x2ee, 0x82f9 }, { 0x2df, 0x825b }, { 0x2d0, 0x81bc },
	{ 0x2c2, 0x811c }, { 0x2b4, 0x8079 }, { 0x2a6, 0x7fd5 },
	{ 0x299, 0x7f2f }, { 0x28c, 0x7e87 }, { 0x27f, 0x7ddd },
	{ 0x272, 0x7d31 }, { 0x265, 0x7c83 }, { 0x259, 0x7bd4 },
	{ 0x24d, 0x7b22 }, { 0x241, 0x7a6f }, { 0x236, 0x79ba },
	{ 0x22b, 0x7903 }, { 0x220, 0x784a }, { 0x215, 0x778f },
	{ 0x20a, 0x76d3 }, { 0x200, 0x7614 }, { 0x1f6, 0x7554 },
	{ 0x1ec, 0x7492 }, { 0x1e2, 0x73ce }, { 0x1d8, 0x7308 },
	{ 0x1cf, 0x7241 }, { 0x1c6, 0x7178 }, { 0x1bc, 0x70ad },
	{ 0x1b3, 0x6fe1 }, { 0x1aa, 0x6f13 }, { 0x1a2, 0x6e44 },
	{ 0x199, 0x6d74 }, { 0x191, 0x6ca2 }, { 0x189, 0x6bcf },
	{ 0x181, 0x6afb }, { 0x179, 0x6a26 }, { 0x171, 0x6950 },
	{ 0x169, 0x687a }, { 0x161, 0x67a2 }, { 0x15a, 0x66ca },
	{ 0x153, 0x65f1 }, { 0x14b, 0x6517 }, { 0x144, 0x643e },
	{ 0x13d, 0x6364 }, { 0x137, 0x628a }, { 0x130, 0x61b0 },
	{ 0x12a, 0x60d6 }, { 0x124, 0x5ffc }, { 0x11e, 0x5f22 },
	{ 0x118, 0x5e49 }, { 0x112, 0x5d70 }, { 0x10c, 0x5c98 },
	{ 0x107, 0x5bc0 }, { 0x101, 0x5ae9 }, { 0xfc, 0x5a13 },
	{ 0xf7, 0x593e }, { 0xf2, 0x5869 }, { 0xec, 0x5796 },
	{ 0xe7, 0x56c4 }, { 0xe2, 0x55f3 }, { 0xdd, 0x5523 },
	{ 0xd8, 0x5454 }, { 0xd4, 0x5387 }, { 0xcf, 0x52bb },
	{ 0xca, 0x51f0 }, { 0xc6, 0x5126 }, { 0xc2, 0x505e },
	{ 0xbe, 0x4f98 }, { 0xb9, 0x4ed3 }, { 0xb5, 0x4e0f },
	{ 0xb1, 0x4d4d }, { 0xae, 0x4c8d }, { 0xaa, 0x4bce },
	{ 0xa6, 0x4b10 }, { 0xa3, 0x4a55 }, { 0x9f, 0x499a },
	{ 0x9b, 0x48e1 }, { 0x98, 0x482a }, { 0x95, 0x4774 },
	{ 0x91, 0x46c0 }, { 0x8e, 0x460d }, { 0x8b, 0x455c },
	{ 0x88, 0x44ac }, { 0x85, 0x43fe }, { 0x82, 0x4352 },
	{ 0x7f, 0x42a6 }, { 0x7c, 0x41fd }, { 0x7a, 0x4154 },
	{ 0x77, 0x40ae }, { 0x74, 0x4008 }, { 0x72, 0x3f64 },
	{ 0x6f, 0x3ec2 }, { 0x6d, 0x3e21 }, { 0x6b, 0x3d81 },
	{ 0x68, 0x3ce3 }, { 0x66, 0x3c46 }, { 0x64, 0x3baa },
	{ 0x61, 0x3b10 }, { 0x5f, 0x3a77 }, { 0x5d, 0x39e0 },
	{ 0x5b, 0x394a }, { 0x59, 0x38b5 }, { 0x57, 0x3821 },
	{ 0x55, 0x378f }, { 0x53, 0x36fe }, { 0x51, 0x366e },
	{ 0x4f, 0x35e0 }, { 0x4e, 0x3553 }, { 0x4c, 0x34c7 },
	{ 0x4a, 0x343c }, { 0x49, 0x33b3 }, { 0x47, 0x332b },
	{ 0x45, 0x32a4 }, { 0x44, 0x321e }, { 0x42, 0x319a },
	{ 0x41, 0x3116 }, { 0x3f, 0x3094 }, { 0x3e, 0x3014 },
	{ 0x3c, 0x2f94 }, { 0x3b, 0x2f16 }, { 0x3a, 0x2e99 },
	{ 0x38, 0x2e1d }, { 0x37, 0x2da2 }, { 0x36, 0x2d29 },
	{ 0x34, 0x2cb0 }, { 0x33, 0x2c39 }, { 0x32, 0x2bc4 },
	{ 0x31, 0x2b4f }, { 0x30, 0x2adc }, { 0x2f, 0x2a6a },
	{ 0x2e, 0x29f9 }, { 0x2d, 0x2989 }, { 0x2c, 0x291a },
	{ 0x2b, 0x28ad }, { 0x2a, 0x2841 }, { 0x29, 0x27d6 },
	{ 0x28, 0x276d }, { 0x27, 0x2705 }, { 0x26, 0x269e },
	{ 0x25, 0x2638 }, { 0x24, 0x25d3 }, { 0x23, 0x2570 },
	{ 0x22, 0x24ad }, { 0x21, 0x244d }, { 0x20, 0x23ef },
	{ 0x1f, 0x2336 }, { 0x1e, 0x22db }, { 0x1d, 0x222a },
	{ 0x1c, 0x217d }, { 0x1b, 0x20d5 }, { 0x1a, 0x2083 },
	{ 0x19, 0x1fe3 }, { 0x18, 0x1f94 }, { 0x17, 0x1efb },
	{ 0x16, 0x1e66 }, { 0x15, 0x1dd6 }, { 0x14, 0x1d90 }
};

/* ISDB-C と J.83B の C/N 推定用の生値 (IREG_SNR_ESTIMATE) を、地デジと同じく PT1/PT3 互換の CNDAT へ換算する表 (10〜50dB、0.1dB 刻み)
 * 各要素は、Sony の sony_demod_isdbc_monitor_SNR() の式 9.5 * ln(24320 / 生値) がその dB 以上になる生値の上限と、recpt1 の地デジの式でその dB になる CNDAT を組にしている */
static const struct {
	u16 val;
	u32 cn;
} isdbc_cn_raw_table[] = {
	{ 0x2128, 0xb3fa1 }, { 0x20cf, 0xafa2b }, { 0x2077, 0xab683 },
	{ 0x2020, 0xa749b }, { 0x1fca, 0xa3467 }, { 0x1f74, 0x9f5d8 },
	{ 0x1f20, 0x9b8e4 }, { 0x1ecd, 0x97d7d }, { 0x1e7a, 0x94398 },
	{ 0x1e28, 0x90b2b }, { 0x1dd8, 0x8d42a }, { 0x1d88, 0x89e8b },
	{ 0x1d38, 0x86a45 }, { 0x1cea, 0x8374c }, { 0x1c9d, 0x80599 },
	{ 0x1c50, 0x7d522 }, { 0x1c04, 0x7a5de }, { 0x1bb9, 0x777c5 },
	{ 0x1b6f, 0x74acf }, { 0x1b25, 0x71ef3 }, { 0x1adc, 0x6f42a },
	{ 0x1a94, 0x6ca6d }, { 0x1a4d, 0x6a1b5 }, { 0x1a06, 0x679fa },
	{ 0x19c1, 0x65336 }, { 0x197c, 0x62d62 }, { 0x1937, 0x60878 },
	{ 0x18f4, 0x5e472 }, { 0x18b1, 0x5c149 }, { 0x186f, 0x59ef9 },
	{ 0x182d, 0x57d7b }, { 0x17ec, 0x55cca }, { 0x17ac, 0x53ce0 },
	{ 0x176d, 0x51dba }, { 0x172e, 0x4ff51 }, { 0x16f0, 0x4e1a0 },
	{ 0x16b2, 0x4c4a4 }, { 0x1675, 0x4a858 }, { 0x1639, 0x48cb7 },
	{ 0x15fe, 0x471bc }, { 0x15c3, 0x45765 }, { 0x1588, 0x43dac },
	{ 0x154f, 0x4248d }, { 0x1516, 0x40c06 }, { 0x14dd, 0x3f412 },
	{ 0x14a5, 0x3dcad }, { 0x146e, 0x3c5d5 }, { 0x1437, 0x3af85 },
	{ 0x1401, 0x399ba }, { 0x13cb, 0x38471 }, { 0x1396, 0x36fa8 },
	{ 0x1362, 0x35b5a }, { 0x132e, 0x34785 }, { 0x12fa, 0x33426 },
	{ 0x12c7, 0x3213a }, { 0x1295, 0x30ebe }, { 0x1263, 0x2fcb0 },
	{ 0x1232, 0x2eb0d }, { 0x1201, 0x2d9d3 }, { 0x11d1, 0x2c8fe },
	{ 0x11a1, 0x2b88e }, { 0x1172, 0x2a87e }, { 0x1143, 0x298cd },
	{ 0x1115, 0x2897a }, { 0x10e7, 0x27a80 }, { 0x10ba, 0x26bdf },
	{ 0x108d, 0x25d95 }, { 0x1060, 0x24f9f }, { 0x1035, 0x241fb },
	{ 0x1009, 0x234a8 }, { 0xfde, 0x227a3 }, { 0xfb4, 0x21aeb },
	{ 0xf89, 0x20e7e }, { 0xf60, 0x2025a }, { 0xf37, 0x1f67e },
	{ 0xf0e, 0x1eae8 }, { 0xee5, 0x1df96 }, { 0xebe, 0x1d487 },
	{ 0xe96, 0x1c9b8 }, { 0xe6f, 0x1bf2a }, { 0xe48, 0x1b4da },
	{ 0xe22, 0x1aac6 }, { 0xdfc, 0x1a0ee }, { 0xdd7, 0x19750 },
	{ 0xdb1, 0x18deb }, { 0xd8d, 0x184bd }, { 0xd68, 0x17bc6 },
	{ 0xd44, 0x17303 }, { 0xd21, 0x16a74 }, { 0xcfe, 0x16217 },
	{ 0xcdb, 0x159ec }, { 0xcb8, 0x151f1 }, { 0xc96, 0x14a25 },
	{ 0xc75, 0x14288 }, { 0xc53, 0x13b17 }, { 0xc32, 0x133d3 },
	{ 0xc11, 0x12cb9 }, { 0xbf1, 0x125c9 }, { 0xbd1, 0x11f03 },
	{ 0xbb1, 0x11865 }, { 0xb92, 0x111ed }, { 0xb73, 0x10b9d },
	{ 0xb54, 0x10571 }, { 0xb36, 0xff6b }, { 0xb18, 0xf988 },
	{ 0xafa, 0xf3c8 }, { 0xadd, 0xee2a }, { 0xac0, 0xe8ae },
	{ 0xaa3, 0xe352 }, { 0xa86, 0xde16 }, { 0xa6a, 0xd8fa },
	{ 0xa4e, 0xd3fc }, { 0xa32, 0xcf1b }, { 0xa17, 0xca58 },
	{ 0x9fc, 0xc5b1 }, { 0x9e1, 0xc126 }, { 0x9c7, 0xbcb7 },
	{ 0x9ad, 0xb861 }, { 0x993, 0xb426 }, { 0x979, 0xb004 },
	{ 0x960, 0xabfb }, { 0x946, 0xa809 }, { 0x92e, 0xa430 },
	{ 0x915, 0xa06e }, { 0x8fd, 0x9cc2 }, { 0x8e5, 0x992c },
	{ 0x8cd, 0x95ac }, { 0x8b5, 0x9241 }, { 0x89e, 0x8eea },
	{ 0x887, 0x8ba8 }, { 0x870, 0x8879 }, { 0x859, 0x855d },
	{ 0x843, 0x8254 }, { 0x82d, 0x7f5e }, { 0x817, 0x7c79 },
	{ 0x801, 0x79a6 }, { 0x7ec, 0x76e4 }, { 0x7d6, 0x7432 },
	{ 0x7c1, 0x7191 }, { 0x7ad, 0x6eff }, { 0x798, 0x6c7e },
	{ 0x784, 0x6a0b }, { 0x76f, 0x67a7 }, { 0x75c, 0x6552 },
	{ 0x748, 0x630b }, { 0x734, 0x60d1 }, { 0x721, 0x5ea5 },
	{ 0x70e, 0x5c86 }, { 0x6fb, 0x5a74 }, { 0x6e8, 0x586f },
	{ 0x6d6, 0x5676 }, { 0x6c3, 0x5489 }, { 0x6b1, 0x52a7 },
	{ 0x69f, 0x50d1 }, { 0x68e, 0x4f06 }, { 0x67c, 0x4d46 },
	{ 0x66b, 0x4b91 }, { 0x659, 0x49e6 }, { 0x648, 0x4845 },
	{ 0x637, 0x46ae }, { 0x627, 0x4520 }, { 0x616, 0x439c },
	{ 0x606, 0x4222 }, { 0x5f6, 0x40b0 }, { 0x5e6, 0x3f47 },
	{ 0x5d6, 0x3de6 }, { 0x5c6, 0x3c8e }, { 0x5b7, 0x3b3e },
	{ 0x5a8, 0x39f6 }, { 0x598, 0x38b6 }, { 0x589, 0x377d },
	{ 0x57b, 0x364b }, { 0x56c, 0x3521 }, { 0x55d, 0x33fe },
	{ 0x54f, 0x32e2 }, { 0x541, 0x31cc }, { 0x533, 0x30bd },
	{ 0x525, 0x2fb5 }, { 0x517, 0x2eb2 }, { 0x509, 0x2db6 },
	{ 0x4fc, 0x2cbf }, { 0x4ee, 0x2bcf }, { 0x4e1, 0x2ae4 },
	{ 0x4d4, 0x29fe }, { 0x4c7, 0x291e }, { 0x4ba, 0x2843 },
	{ 0x4ae, 0x276e }, { 0x4a1, 0x269d }, { 0x495, 0x25d1 },
	{ 0x488, 0x250a }, { 0x47c, 0x2447 }, { 0x470, 0x2389 },
	{ 0x464, 0x22d0 }, { 0x459, 0x221a }, { 0x44d, 0x2169 },
	{ 0x441, 0x20bc }, { 0x436, 0x2013 }, { 0x42b, 0x1f6e },
	{ 0x41f, 0x1ecd }, { 0x414, 0x1e2f }, { 0x409, 0x1d95 },
	{ 0x3ff, 0x1cff }, { 0x3f4, 0x1c6c }, { 0x3e9, 0x1bdc },
	{ 0x3df, 0x1b50 }, { 0x3d4, 0x1ac7 }, { 0x3ca, 0x1a41 },
	{ 0x3c0, 0x19be }, { 0x3b6, 0x193e }, { 0x3ac, 0x18c1 },
	{ 0x3a2, 0x1846 }, { 0x398, 0x17cf }, { 0x38f, 0x175a },
	{ 0x385, 0x16e8 }, { 0x37c, 0x1678 }, { 0x372, 0x160b },
	{ 0x369, 0x15a0 }, { 0x360, 0x1538 }, { 0x357, 0x14d2 },
	{ 0x34e, 0x146e }, { 0x345, 0x140c }, { 0x33c, 0x13ad },
	{ 0x334, 0x1350 }, { 0x32b, 0x12f4 }, { 0x323, 0x129b },
	{ 0x31a, 0x1244 }, { 0x312, 0x11ee }, { 0x30a, 0x119b },
	{ 0x302, 0x1149 }, { 0x2f9, 0x10f9 }, { 0x2f1, 0x10ab },
	{ 0x2ea, 0x105f }, { 0x2e2, 0x1014 }, { 0x2da, 0xfca },
	{ 0x2d2, 0xf83 }, { 0x2cb, 0xf3d }, { 0x2c3, 0xef8 },
	{ 0x2bc, 0xeb5 }, { 0x2b5, 0xe73 }, { 0x2ad, 0xe33 },
	{ 0x2a6, 0xdf3 }, { 0x29f, 0xdb6 }, { 0x298, 0xd79 },
	{ 0x291, 0xd3e }, { 0x28a, 0xd04 }, { 0x283, 0xccc },
	{ 0x27d, 0xc94 }, { 0x276, 0xc5e }, { 0x26f, 0xc28 },
	{ 0x269, 0xbf4 }, { 0x262, 0xbc1 }, { 0x25c, 0xb8f },
	{ 0x256, 0xb5e }, { 0x24f, 0xb2e }, { 0x249, 0xaff },
	{ 0x243, 0xad1 }, { 0x23d, 0xaa4 }, { 0x237, 0xa78 },
	{ 0x231, 0xa4c }, { 0x22b, 0xa22 }, { 0x225, 0x9f8 },
	{ 0x220, 0x9cf }, { 0x21a, 0x9a7 }, { 0x214, 0x980 },
	{ 0x20f, 0x95a }, { 0x209, 0x934 }, { 0x204, 0x90f },
	{ 0x1fe, 0x8eb }, { 0x1f9, 0x8c7 }, { 0x1f4, 0x8a4 },
	{ 0x1ee, 0x882 }, { 0x1e9, 0x861 }, { 0x1e4, 0x840 },
	{ 0x1df, 0x820 }, { 0x1da, 0x800 }, { 0x1d5, 0x7e1 },
	{ 0x1d0, 0x7c2 }, { 0x1cb, 0x7a5 }, { 0x1c6, 0x787 },
	{ 0x1c2, 0x76b }, { 0x1bd, 0x74e }, { 0x1b8, 0x733 },
	{ 0x1b4, 0x718 }, { 0x1af, 0x6fd }, { 0x1ab, 0x6e3 },
	{ 0x1a6, 0x6c9 }, { 0x1a2, 0x6b0 }, { 0x19d, 0x697 },
	{ 0x199, 0x67f }, { 0x195, 0x667 }, { 0x190, 0x64f },
	{ 0x18c, 0x639 }, { 0x188, 0x622 }, { 0x184, 0x60c },
	{ 0x180, 0x5f6 }, { 0x17c, 0x5e1 }, { 0x178, 0x5cc },
	{ 0x174, 0x5b7 }, { 0x170, 0x5a3 }, { 0x16c, 0x58f },
	{ 0x168, 0x57b }, { 0x165, 0x568 }, { 0x161, 0x555 },
	{ 0x15d, 0x543 }, { 0x159, 0x530 }, { 0x156, 0x51e },
	{ 0x152, 0x50d }, { 0x14f, 0x4fb }, { 0x14b, 0x4ea },
	{ 0x148, 0x4da }, { 0x144, 0x4c9 }, { 0x141, 0x4b9 },
	{ 0x13e, 0x4a9 }, { 0x13a, 0x49a }, { 0x137, 0x48a },
	{ 0x134, 0x47b }, { 0x130, 0x46c }, { 0x12d, 0x45e },
	{ 0x12a, 0x44f }, { 0x127, 0x441 }, { 0x124, 0x433 },
	{ 0x121, 0x426 }, { 0x11e, 0x418 }, { 0x11b, 0x40b },
	{ 0x118, 0x3fe }, { 0x115, 0x3f1 }, { 0x112, 0x3e5 },
	{ 0x10f, 0x3d8 }, { 0x10c, 0x3cc }, { 0x109, 0x3c0 },
	{ 0x107, 0x3b4 }, { 0x104, 0x3a9 }, { 0x101, 0x39d },
	{ 0xfe, 0x392 }, { 0xfc, 0x387 }, { 0xf9, 0x37c },
	{ 0xf7, 0x371 }, { 0xf4, 0x367 }, { 0xf1, 0x35c },
	{ 0xef, 0x352 }, { 0xec, 0x348 }, { 0xea, 0x33e },
	{ 0xe7, 0x334 }, { 0xe5, 0x32a }, { 0xe3, 0x321 },
	{ 0xe0, 0x318 }, { 0xde, 0x30e }, { 0xdc, 0x305 },
	{ 0xd9, 0x2fc }, { 0xd7, 0x2f4 }, { 0xd5, 0x2eb },
	{ 0xd2, 0x2e2 }, { 0xd0, 0x2da }, { 0xce, 0x2d2 },
	{ 0xcc, 0x2ca }, { 0xca, 0x2c2 }, { 0xc8, 0x2ba },
	{ 0xc6, 0x2b2 }, { 0xc3, 0x2aa }, { 0xc1, 0x2a3 },
	{ 0xbf, 0x29b }, { 0xbd, 0x294 }, { 0xbb, 0x28d },
	{ 0xb9, 0x285 }, { 0xb7, 0x27e }, { 0xb6, 0x277 },
	{ 0xb4, 0x271 }, { 0xb2, 0x26a }, { 0xb0, 0x263 },
	{ 0xae, 0x25d }, { 0xac, 0x256 }, { 0xaa, 0x250 },
	{ 0xa9, 0x249 }, { 0xa7, 0x243 }, { 0xa5, 0x23d },
	{ 0xa3, 0x237 }, { 0xa2, 0x231 }, { 0xa0, 0x22b },
	{ 0x9e, 0x225 }, { 0x9d, 0x220 }, { 0x9b, 0x21a },
	{ 0x99, 0x214 }, { 0x98, 0x20f }, { 0x96, 0x20a },
	{ 0x95, 0x204 }, { 0x93, 0x1ff }, { 0x91, 0x1fa },
	{ 0x90, 0x1f5 }, { 0x8e, 0x1f0 }, { 0x8d, 0x1eb },
	{ 0x8b, 0x1e6 }, { 0x8a, 0x1e1 }, { 0x89, 0x1dc },
	{ 0x87, 0x1d7 }, { 0x86, 0x1d3 }, { 0x84, 0x1ce },
	{ 0x83, 0x1c9 }, { 0x81, 0x1c5 }, { 0x80, 0x1c0 },
	{ 0x7f, 0x1bc }, { 0x7d, 0x1b8 }
};

static int pxmlt_chrdev_read_cnr_raw(struct ptx_chrdev *chrdev, u32 *value)
{
	int ret = 0;
	struct pxmlt_chrdev *chrdevm = chrdev->priv;

	switch (chrdev->current_system) {
	case PTX_ISDB_T_SYSTEM:
	{
		int i, i_min, i_max;
		u16 val;

		ret = cxd2856er_read_cnr_raw_isdbt(&chrdevm->cxd2856er, &val);
		if (ret)
			break;

		i_min = 0;
		i_max = ARRAY_SIZE(isdbt_cn_raw_table) - 1;

		if (isdbt_cn_raw_table[i_min].val >= val) {
			*value = isdbt_cn_raw_table[i_min].cn;
			break;
		}
		if (isdbt_cn_raw_table[i_max].val <= val) {
			*value = isdbt_cn_raw_table[i_max].cn;
			break;
		}

		while (1) {
			i = i_min + (i_max - i_min) / 2;

			if (isdbt_cn_raw_table[i].val == val) {
				*value = isdbt_cn_raw_table[i].cn;
				break;
			}

			if (isdbt_cn_raw_table[i].val < val)
				i_min = i + 1;
			else
				i_max = i - 1;

			if (i_max < i_min) {
				*value = isdbt_cn_raw_table[i_max].cn;
				break;
			}
		}

		break;
	}

	case PTX_ISDB_S_SYSTEM:
	{
		int i, i_min, i_max;
		u16 val;

		ret = cxd2856er_read_cnr_raw_isdbs(&chrdevm->cxd2856er, &val);
		if (ret)
			break;

		i_min = 0;
		i_max = ARRAY_SIZE(isdbs_cn_raw_table) - 1;

		if (isdbs_cn_raw_table[i_min].val <= val) {
			*value = isdbs_cn_raw_table[i_min].cn;
			break;
		}
		if (isdbs_cn_raw_table[i_max].val >= val) {
			*value = isdbs_cn_raw_table[i_max].cn;
			break;
		}

		while (1) {
			i = i_min + (i_max - i_min) / 2;

			if (isdbs_cn_raw_table[i].val == val) {
				*value = isdbs_cn_raw_table[i].cn;
				break;
			}

			if (isdbs_cn_raw_table[i].val > val)
				i_min = i + 1;
			else
				i_max = i - 1;

			if (i_max < i_min) {
				*value = isdbs_cn_raw_table[i_max].cn;
				break;
			}
		}

		break;
	}

	case PTX_ISDB_C_SYSTEM:
	case PTX_J83B_SYSTEM:
	{
		int i, i_min, i_max;
		u16 val;

		/* 受信ソフトウェアが地デジと同じ recpt1 の式で dB へ換算できるよう、PT1/PT3 互換の CNDAT を返す (J.83B も ISDB-C と同じ式を使う) */
		ret = cxd2856er_read_cnr_raw_isdbc(&chrdevm->cxd2856er, &val);
		if (ret)
			break;

		i_min = 0;
		i_max = ARRAY_SIZE(isdbc_cn_raw_table) - 1;

		/* Sony のコードが信号なしとして扱う 0 は、地デジや BS/CS がロックしていないときと同じく表の最小値にする */
		if (!val || isdbc_cn_raw_table[i_min].val <= val) {
			*value = isdbc_cn_raw_table[i_min].cn;
			break;
		}
		if (isdbc_cn_raw_table[i_max].val >= val) {
			*value = isdbc_cn_raw_table[i_max].cn;
			break;
		}

		while (1) {
			i = i_min + (i_max - i_min) / 2;

			if (isdbc_cn_raw_table[i].val == val) {
				*value = isdbc_cn_raw_table[i].cn;
				break;
			}

			if (isdbc_cn_raw_table[i].val > val)
				i_min = i + 1;
			else
				i_max = i - 1;

			if (i_max < i_min) {
				*value = isdbc_cn_raw_table[i_max].cn;
				break;
			}
		}

		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct ptx_chrdev_operations pxmlt_chrdev_ops = {
	.init = pxmlt_chrdev_init,
	.term = pxmlt_chrdev_term,
	.open = pxmlt_chrdev_open,
	.release = pxmlt_chrdev_release,
	.tune = pxmlt_chrdev_tune,
	.check_lock = pxmlt_chrdev_check_lock,
	.set_stream_id = pxmlt_chrdev_set_stream_id,
	.set_lnb_voltage = pxmlt_chrdev_set_lnb_voltage,
	.set_capture = pxmlt_chrdev_set_capture,
	.read_signal_strength = NULL,
	.read_cnr = NULL,
	.read_cnr_raw = pxmlt_chrdev_read_cnr_raw
};

static const struct {
	u8 i2c_addr;
	u8 i2c_bus;
	u8 port_number;
} pxmlt_device_params[][5] = {
	/* PX-MLT5U */
	{ { 0x65, 3, 4 }, { 0x6c, 1, 3 }, { 0x64, 1, 1 }, { 0x6c, 3, 2 }, { 0x64, 3, 0 } },
	/* PX-MLT5PE */
	{ { 0x65, 3, 0 }, { 0x6c, 1, 1 }, { 0x64, 1, 2 }, { 0x6c, 3, 3 }, { 0x64, 3, 4 } },
	/* PX-MLT8PE3 */
	{ { 0x65, 3, 0 }, { 0x6c, 3, 3 }, { 0x64, 3, 4 }, { 0x00, 0, 1 }, { 0x00, 0, 2 } },
	/* PX-MLT8PE5 */
	{ { 0x65, 1, 0 }, { 0x64, 1, 1 }, { 0x6c, 1, 2 }, { 0x6c, 3, 3 }, { 0x64, 3, 4 } },
	/* ISDB6014 V2.0 (4TS) */
	{ { 0x65, 3, 0 }, { 0x6c, 1, 1 }, { 0x64, 1, 2 }, { 0x64, 3, 4 }, { 0x00, 0, 3 } }
};

static int pxmlt_device_load_config(struct pxmlt_device *pxmlt,
				    enum pxmlt_model model, 
				    struct ptx_chrdev_config *chrdev_config)
{
	int ret = 0, i;
	struct device *dev = pxmlt->dev;
	struct it930x_bridge *it930x = &pxmlt->it930x;
	u8 tmp;

	ret = it930x_read_reg(it930x, 0x4979, &tmp);
	if (ret) {
		dev_err(dev,
			"pxmlt_device_load_config: it930x_read_reg(0x4979) failed. (ret: %d)\n",
			ret);
		return ret;
	} else if (!tmp) {
		dev_warn(dev, "EEPROM error.\n");
		return ret;
	}

	for (i = 0; i < pxmlt->chrdevm_num; i++) {
		struct it930x_stream_input *input = &it930x->config.input[i];
		struct pxmlt_chrdev *chrdevm = &pxmlt->chrdevm[i];

		chrdev_config[i].system_cap = PTX_ISDB_T_SYSTEM | PTX_ISDB_S_SYSTEM | PTX_ISDB_C_SYSTEM | PTX_J83B_SYSTEM;

		input->enable = true;
		input->is_parallel = false;
		input->port_number = pxmlt_device_params[model][i].port_number;
		input->slave_number = i;
		input->i2c_bus = pxmlt_device_params[model][i].i2c_bus;
		input->i2c_addr = pxmlt_device_params[model][i].i2c_addr;
		input->packet_len = 188;
		input->sync_byte = ((i + 1) << 4) | 0x07;	/* 0x17 0x27 0x37 0x47 0x57 */

		chrdevm->cxd2856er.dev = dev;
		chrdevm->cxd2856er.i2c = &it930x->i2c_master[input->i2c_bus - 1];
		chrdevm->cxd2856er.i2c_addr.slvx = input->i2c_addr + 2;
		chrdevm->cxd2856er.i2c_addr.slvt = input->i2c_addr;
		chrdevm->cxd2856er.config.xtal = 24000;
		chrdevm->cxd2856er.config.tuner_i2c = true;

		chrdevm->cxd2858er.dev = dev;
		chrdevm->cxd2858er.i2c = &chrdevm->cxd2856er.i2c_master;
		chrdevm->cxd2858er.i2c_addr = 0x60;
		chrdevm->cxd2858er.config.xtal = 16000;
		chrdevm->cxd2858er.config.ter.lna = true;
		chrdevm->cxd2858er.config.ter.rfin_matching = false;
		chrdevm->cxd2858er.config.sat.lna = true;
	}

	for (i = pxmlt->chrdevm_num; i < PXMLT_CHRDEV_MAX_NUM; i++) {
		it930x->config.input[i].enable = false;
		it930x->config.input[i].port_number = pxmlt_device_params[model][i].port_number;
	}

	return 0;
}

static int pxmlt_card_set_power(void *context, bool on)
{
	struct pxmlt_device *pxmlt = context;
	int ret = 0;

	/* 受信中のチューナーがあれば、基板の電源はチューナー側の参照で入ったままになっている */
	if (!pxmlt->open_count)
		ret = pxmlt_backend_set_power(pxmlt, on);

	/* 停止は成否にかかわらず停止済みとして記録し、投入は成功したときだけ記録する */
	if (!on || !ret)
		pxmlt->card_powered = on;

	return ret;
}

int pxmlt_device_init(struct pxmlt_device *pxmlt, struct device *dev,
		      enum pxmlt_model model,
		      struct ptx_chrdev_context *chrdev_ctx,
		      struct completion *quit_completion)
{
	int ret = 0, i;
	struct it930x_bridge *it930x;
	struct itedtv_bus *bus;
	struct ptx_chrdev_config chrdev_config[PXMLT_CHRDEV_MAX_NUM];
	struct ptx_chrdev_group_config chrdev_group_config;
	struct ptx_chrdev_group *chrdev_group;
	struct pxmlt_stream_context *stream_ctx;

	if (!pxmlt || !dev || !chrdev_ctx || !quit_completion)
		return -EINVAL;

	dev_dbg(dev, "pxmlt_device_init\n");

	get_device(dev);

	mutex_init(&pxmlt->lock);
	kref_init(&pxmlt->kref);
	pxmlt->dev = dev;
	pxmlt->quit_completion = quit_completion;
	pxmlt->open_count = 0;
	pxmlt->card_powered = false;
	pxmlt->card = NULL;
	pxmlt->lnb_power_count = 0;
	pxmlt->streaming_count = 0;
	mutex_init(&pxmlt->tuner_lock[0]);
	mutex_init(&pxmlt->tuner_lock[1]);
	switch (model) {
	case PXMLT8PE3_MODEL:
		pxmlt->chrdevm_num = 3;
		break;

	case ISDB6014_4TS_MODEL:
		pxmlt->chrdevm_num = 4;
		break;

	default:
		pxmlt->chrdevm_num = 5;
		break;
	}

	for (i = 0; i < pxmlt->chrdevm_num; i++) {
		struct pxmlt_chrdev *chrdevm = &pxmlt->chrdevm[i];
		int tuner_lock_idx = (pxmlt_device_params[model][i].i2c_bus == 3) ? 0
										  : 1;

		chrdevm->chrdev = NULL;
		chrdevm->parent = pxmlt;
		chrdevm->lnb_power = false;
		chrdevm->tuner_lock = &pxmlt->tuner_lock[tuner_lock_idx];
	}

	stream_ctx = kzalloc(sizeof(*stream_ctx), GFP_KERNEL);
	if (!stream_ctx) {
		dev_err(pxmlt->dev,
			"pxmlt_device_init: kzalloc(sizeof(*stream_ctx), GFP_KERNEL) failed.\n");
		ret = -ENOMEM;
		goto fail;
	}
	pxmlt->stream_ctx = stream_ctx;

	it930x = &pxmlt->it930x;
	bus = &it930x->bus;

	ret = itedtv_bus_init(bus);
	if (ret)
		goto fail_bus;

	ret = it930x_init(it930x);
	if (ret)
		goto fail_bridge;

	ret = it930x_raise(it930x);
	if (ret)
		goto fail_device;

	ret = pxmlt_device_load_config(pxmlt, model, chrdev_config);
	if (ret)
		goto fail_device;

	for (i = 0; i < pxmlt->chrdevm_num; i++) {
		chrdev_config[i].ops = &pxmlt_chrdev_ops;
		/*
		 * CXD2856ER のロック判定はレジスタを1回読むだけで整定を待たないため、
		 * 復調が安定する前の TS がそのまま流れて選局直後に同期エラーが出る
		 * 他機種と同じくロック後の待機を入れ、地上波の TS を配信する前に整定させる
		 */
		chrdev_config[i].options = PTX_CHRDEV_SAT_SET_STREAM_ID_BEFORE_TUNE |
					   PTX_CHRDEV_WAIT_AFTER_LOCK_TC_T;
		chrdev_config[i].ringbuf_size = 188 * px4_device_params.tsdev_max_packets;
		chrdev_config[i].ringbuf_threshold_size = chrdev_config[i].ringbuf_size / 10;
		chrdev_config[i].priv = &pxmlt->chrdevm[i];
	}

	ret = it930x_load_firmware(it930x, IT930X_FIRMWARE_FILENAME);
	if (ret)
		goto fail_device;

	ret = it930x_init_warm(it930x);
	if (ret)
		goto fail_device;

	/* GPIO */
	ret = it930x_set_gpio_mode(it930x, 7, IT930X_GPIO_OUT, true);
	if (ret)
		goto fail_device;

	ret = it930x_write_gpio(it930x, 7, true);
	if (ret)
		goto fail_device;

	ret = it930x_set_gpio_mode(it930x, 2, IT930X_GPIO_OUT, true);
	if (ret)
		goto fail_device;

	ret = it930x_write_gpio(it930x, 2, false);
	if (ret)
		goto fail_device;

	ret = it930x_set_gpio_mode(it930x, 11, IT930X_GPIO_OUT, true);
	if (ret)
		goto fail_device;

	/* LNB power supply: off */
	ret = it930x_write_gpio(it930x, 11, false);
	if (ret)
		goto fail_device;

	if (px4_device_params.discard_null_packets) {
		struct it930x_pid_filter filter;

		filter.block = true;
		filter.num = 1;
		filter.pid[0] = 0x1fff;

		for (i = 0; i < pxmlt->chrdevm_num; i++) {
			ret = it930x_set_pid_filter(it930x, i, &filter);
			if (ret)
				goto fail_device;
		}
	}

	chrdev_group_config.owner_kref = &pxmlt->kref;
	chrdev_group_config.owner_kref_release = pxmlt_device_release;
	chrdev_group_config.reserved = false;
	chrdev_group_config.minor_base = 0;	/* unused */
	chrdev_group_config.chrdev_num = pxmlt->chrdevm_num;
	chrdev_group_config.chrdev_config = chrdev_config;

	ret = ptx_chrdev_context_add_group(chrdev_ctx, dev,
					   &chrdev_group_config, &chrdev_group);
	if (ret)
		goto fail_chrdev;

	pxmlt->chrdev_group = chrdev_group;

	for (i = 0; i < pxmlt->chrdevm_num; i++) {
		pxmlt->chrdevm[i].chrdev = &chrdev_group->chrdev[i];
		stream_ctx->chrdev[i] = &chrdev_group->chrdev[i];

		/* ブリッジの設定は初期化のときの値で決まるため、実行中にモジュールパラメーターを書き換えても、ここで渡した値を TSMF の分離に使い続ける */
		chrdev_group->chrdev[i].discard_null_packets = px4_device_params.discard_null_packets;
		chrdev_group->chrdev[i].tsmf_stale_packets = it930x->config.xfer_size / 188;
	}

	atomic_set(&pxmlt->available, 1);

	/* PX-MLT8PE は5チューナー側と3チューナー側の2つの USB デバイスで構成され、カードスロットは5チューナー側だけにある */
	if (model != PXMLT8PE3_MODEL) {
		struct px4_card_backend card_backend = {
			.it930x = it930x,
			.lock = &pxmlt->lock,
			.available = &pxmlt->available,
			.set_power = pxmlt_card_set_power,
			.context = pxmlt,
		};

		/* カードリーダーを登録できなくても、チューナーは使えるようにする */
		ret = px4_card_register(dev, &card_backend, &pxmlt->card);
		if (ret)
			dev_warn(dev, "Card reader registration failed: %d\n", ret);
	}
	return 0;

fail_chrdev:

fail_device:
	it930x_term(it930x);

fail_bridge:
	itedtv_bus_term(bus);

fail_bus:
	kfree(pxmlt->stream_ctx);

fail:
	mutex_destroy(&pxmlt->tuner_lock[0]);
	mutex_destroy(&pxmlt->tuner_lock[1]);
	mutex_destroy(&pxmlt->lock);
	put_device(dev);

	return ret;
}

static void pxmlt_device_release(struct kref *kref)
{
	struct pxmlt_device *pxmlt = container_of(kref,
						  struct pxmlt_device, kref);

	dev_dbg(pxmlt->dev, "pxmlt_device_release\n");

	it930x_term(&pxmlt->it930x);
	itedtv_bus_term(&pxmlt->it930x.bus);

	kfree(pxmlt->stream_ctx);

	mutex_destroy(&pxmlt->tuner_lock[0]);
	mutex_destroy(&pxmlt->tuner_lock[1]);
	mutex_destroy(&pxmlt->lock);
	put_device(pxmlt->dev);

	complete(pxmlt->quit_completion);
	return;
}

void pxmlt_device_term(struct pxmlt_device *pxmlt)
{
	dev_dbg(pxmlt->dev, "pxmlt_device_term\n");

	mutex_lock(&pxmlt->lock);
	atomic_xchg(&pxmlt->available, 0);
	mutex_unlock(&pxmlt->lock);
	px4_card_unregister(pxmlt->card);
	pxmlt->card = NULL;
	ptx_chrdev_group_destroy(pxmlt->chrdev_group);

	kref_put(&pxmlt->kref, pxmlt_device_release);
	return;
}
