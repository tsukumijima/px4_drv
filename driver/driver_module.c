// SPDX-License-Identifier: GPL-2.0-only
/*
 * Module initiator of the driver (driver_module.c)
 *
 * Copyright (c) 2018-2021 nns779
 */

#include "print_format.h"
#include "driver_module.h"

#include <linux/kernel.h>
#include <linux/module.h>

#include "revision.h"
#include "px4_usb.h"
#include "firmware.h"

static int m_init(void)
{
	int ret = 0;

	pr_info(KBUILD_MODNAME
#ifdef PX4_DRV_VERSION
		" version " PX4_DRV_VERSION
#endif
#ifdef REVISION_NUMBER
#if defined(PX4_DRV_VERSION)
		","
#endif
		" rev: " REVISION_NUMBER
#endif
#ifdef COMMIT_HASH
#if defined(PX4_DRV_VERSION) || defined(REVISION_NUMBER)
		","
#endif
		" commit: " COMMIT_HASH
#endif
#ifdef REVISION_NAME
		" @ " REVISION_NAME
#endif
		"\n");

	ret = px4_usb_register();
	if (ret)
		return ret;

	return 0;
}

static void m_cleanup(void)
{
	px4_usb_unregister();
}


module_init(m_init);
module_exit(m_cleanup);

MODULE_VERSION(PX4_DRV_VERSION);
MODULE_AUTHOR("nns779");
MODULE_DESCRIPTION("Unofficial Linux driver for PLEX PX4/PX5/PX-MLT series ISDB-T/S receivers");
MODULE_LICENSE("GPL v2");

MODULE_FIRMWARE(IT930X_FIRMWARE_FILENAME);
