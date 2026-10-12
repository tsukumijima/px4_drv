// SPDX-License-Identifier: GPL-2.0-only
#ifndef __PX4_CARD_H__
#define __PX4_CARD_H__

#include <linux/device.h>
#include "px4_card_ioctl.h"

struct px4_card;

struct px4_card_operations {
	int (*request)(void *context, unsigned int command, struct px4_card_io *io);
	/* 抜去後にも呼ばれるため、USB I/O の成功を参照解放の条件にできない。 */
	void (*close)(void *context);
};

int px4_card_register(struct device *parent,
		     const struct px4_card_operations *ops, void *context,
		     struct px4_card **result);
void px4_card_unregister(struct px4_card *card);

#endif
