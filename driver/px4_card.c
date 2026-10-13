// SPDX-License-Identifier: GPL-2.0-only
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "px4_card.h"

struct px4_card {
	struct miscdevice misc;
	struct kref refs;
	struct mutex lock;
	wait_queue_head_t wait;
	const struct px4_card_operations *ops;
	void *context;
	int id;
	bool opened;
	bool disconnected;
};

static DEFINE_IDA(px4_card_ids);

static void px4_card_free(struct kref *ref)
{
	struct px4_card *card = container_of(ref, struct px4_card, refs);

	ida_free(&px4_card_ids, card->id);
	kfree(card->misc.name);
	kfree(card);
}

static int px4_card_open(struct inode *inode, struct file *file)
{
	struct px4_card *card = container_of(file->private_data,
					   struct px4_card, misc);
	int ret = 0;

	if (mutex_lock_interruptible(&card->lock))
		return -ERESTARTSYS;
	if (card->disconnected)
		ret = -ENODEV;
	else if (card->opened)
		ret = -EBUSY;
	else {
		kref_get(&card->refs);
		card->opened = true;
		file->private_data = card;
	}
	mutex_unlock(&card->lock);
	return ret;
}

static int px4_card_release(struct inode *inode, struct file *file)
{
	struct px4_card *card = file->private_data;

	mutex_lock(&card->lock);
	if (!card->disconnected)
		card->ops->close(card->context);
	card->opened = false;
	mutex_unlock(&card->lock);
	kref_put(&card->refs, px4_card_free);
	return 0;
}

static long px4_card_ioctl(struct file *file, unsigned int command,
			  unsigned long arg)
{
	struct px4_card *card = file->private_data;
	struct px4_card_io input, output = {};
	struct px4_card_info info = {
		.abi_version = PX4_CARD_ABI_VERSION,
		.max_frame = PX4_CARD_MAX_FRAME,
	};
	void __user *ptr = (void __user *)arg;
	int ret;

	switch (command) {
	case PX4_CARD_GET_INFO:
		break;
	case PX4_CARD_POWER:
	case PX4_CARD_DETECT:
	case PX4_CARD_RESET:
	case PX4_CARD_SET_BAUDRATE:
	case PX4_CARD_RX_READY:
	case PX4_CARD_READ:
	case PX4_CARD_WRITE:
		if (copy_from_user(&input, ptr, sizeof(input)))
			return -EFAULT;
		if (input.reserved || input.length > PX4_CARD_MAX_FRAME)
			return -EINVAL;
		if (command != PX4_CARD_POWER && command != PX4_CARD_SET_BAUDRATE && input.value)
			return -EINVAL;
		if (command == PX4_CARD_READ || command == PX4_CARD_WRITE) {
			if (!input.length)
				return -EINVAL;
		} else if (input.length) {
			return -EINVAL;
		}
		output.value = input.value;
		output.length = input.length;
		if (command == PX4_CARD_WRITE)
			memcpy(output.data, input.data, input.length);
		break;
	default:
		return -ENOTTY;
	}

	if (mutex_lock_interruptible(&card->lock))
		return -ERESTARTSYS;
	if (card->disconnected)
		ret = -ENODEV;
	else if (command == PX4_CARD_GET_INFO)
		ret = copy_to_user(ptr, &info, sizeof(info)) ? -EFAULT : 0;
	else {
		ret = card->ops->request(card->context, command, &output);
		if (!ret && (_IOC_DIR(command) & _IOC_READ))
			ret = copy_to_user(ptr, &output, sizeof(output)) ? -EFAULT : 0;
	}
	mutex_unlock(&card->lock);
	return ret;
}

static __poll_t px4_card_poll(struct file *file, poll_table *wait)
{
	struct px4_card *card = file->private_data;

	poll_wait(file, &card->wait, wait);
	return READ_ONCE(card->disconnected) ? EPOLLERR | EPOLLHUP : 0;
}

static const struct file_operations px4_card_fops = {
	.owner = THIS_MODULE,
	.open = px4_card_open,
	.release = px4_card_release,
	.unlocked_ioctl = px4_card_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
	.poll = px4_card_poll,
};

int px4_card_register(struct device *parent,
		     const struct px4_card_operations *ops, void *context,
		     struct px4_card **result)
{
	struct px4_card *card;
	int ret;

	*result = NULL;
	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	ret = ida_alloc(&px4_card_ids, GFP_KERNEL);
	if (ret < 0) {
		kfree(card);
		return ret;
	}
	card->id = ret;
	kref_init(&card->refs);
	mutex_init(&card->lock);
	init_waitqueue_head(&card->wait);
	card->ops = ops;
	card->context = context;
	card->misc.minor = MISC_DYNAMIC_MINOR;
	card->misc.name = kasprintf(GFP_KERNEL, "px4card%d", card->id);
	card->misc.fops = &px4_card_fops;
	card->misc.parent = parent;
	card->misc.mode = 0600;
	if (!card->misc.name) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = misc_register(&card->misc);
	if (ret)
		goto fail;
	*result = card;
	return 0;
fail:
	kref_put(&card->refs, px4_card_free);
	return ret;
}

void px4_card_unregister(struct px4_card *card)
{
	if (!card)
		return;
	/* open は misc のロックから card->lock の順に取るため、この順序を保つ。 */
	misc_deregister(&card->misc);
	mutex_lock(&card->lock);
	if (card->opened)
		card->ops->close(card->context);
	WRITE_ONCE(card->disconnected, true);
	/*
	 * 古い fd はこの小さな管理オブジェクトだけを保持する。
	 * USB 側が fd の close を待つと、pcscd の抜去処理と待ち合うため切り離す。
	 */
	card->ops = NULL;
	card->context = NULL;
	mutex_unlock(&card->lock);
	wake_up_all(&card->wait);
	kref_put(&card->refs, px4_card_free);
}
