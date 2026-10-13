// SPDX-License-Identifier: GPL-2.0-only
/*
 * 内蔵カードリーダーのデバイスファイル (/dev/px4cardN) の機種共通層
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "px4_card.h"

struct px4_card {
	struct miscdevice misc;
	struct kref refs;
	/* open・ioctl・close と登録解除を直列化する (機種のロックより先に取得する) */
	struct mutex lock;
	wait_queue_head_t wait;
	struct px4_card_backend backend;
	int id;
	bool opened;
	bool powered;
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

/* 機種のロックを保持した状態で呼ぶ */
static int px4_card_set_power(struct px4_card *card, bool on)
{
	const struct px4_card_backend *backend = &card->backend;
	int ret;

	if (card->powered == on)
		return 0;

	if (!on) {
		/* 電源の停止は成否にかかわらず停止済みとして扱い、チューナー側の参照と食い違わせない */
		card->powered = false;
		return backend->set_power(backend->context, false);
	}

	ret = backend->set_power(backend->context, true);
	if (ret)
		return ret;

	ret = it930x_bcas_init(backend->it930x);
	if (ret) {
		backend->set_power(backend->context, false);
		return ret;
	}

	card->powered = true;
	return 0;
}

/* 機種のロックを保持した状態で呼ぶ */
static int px4_card_request(struct px4_card *card, unsigned int command,
			    struct px4_card_io *io)
{
	struct it930x_bridge *it930x = card->backend.it930x;
	enum it930x_uart_baudrate baudrate;
	bool value = false;
	u8 length = io->length;
	int ret;

	if (!atomic_read(card->backend.available))
		return -ENODEV;

	/* 電源を入れていない UART を操作すると、前回の電源投入時の状態を読み書きしてしまう */
	if (command != PX4_CARD_POWER && command != PX4_CARD_DETECT && !card->powered)
		return -EHOSTDOWN;

	switch (command) {
	case PX4_CARD_POWER:
		return px4_card_set_power(card, io->value);

	case PX4_CARD_DETECT:
		/*
		 * pcscd は電源を入れずにカードの有無を問い合わせ続けるため、検出だけで電源の参照を取ると基板の電源が切れなくなる
		 * GPIO H6 の入力設定は通常 it930x_bcas_init() で行うが、電源を入れる前の検出に備えてここでも設定する (2回目以降はキャッシュで転送を省く)
		 */
		ret = it930x_set_gpio_mode(it930x, 6, IT930X_GPIO_IN, true);
		if (!ret)
			ret = it930x_bcas_detect_card(it930x, &value);
		io->value = value;
		return ret;

	case PX4_CARD_RESET:
		return it930x_bcas_reset_card(it930x);

	case PX4_CARD_SET_BAUDRATE:
		switch (io->value) {
		case 9600:
			baudrate = IT930X_UART_BAUDRATE_9600;
			break;
		case 19200:
			baudrate = IT930X_UART_BAUDRATE_19200;
			break;
		case 38400:
			baudrate = IT930X_UART_BAUDRATE_38400;
			break;
		default:
			return -EINVAL;
		}
		return it930x_bcas_set_baudrate(it930x, baudrate);

	case PX4_CARD_RX_READY:
		ret = it930x_bcas_check_ready(it930x, &value);
		io->value = value;
		return ret;

	case PX4_CARD_READ:
		ret = it930x_bcas_get_data(it930x, io->data, &length);
		io->length = length;
		return ret;

	case PX4_CARD_WRITE:
		return it930x_bcas_send_data(it930x, io->data, length);

	default:
		return -ENOTTY;
	}
}

/* card->lock を保持した状態で呼ぶ */
static void px4_card_power_off(struct px4_card *card)
{
	if (!card->powered)
		return;

	mutex_lock(card->backend.lock);
	px4_card_set_power(card, false);
	mutex_unlock(card->backend.lock);
}

static int px4_card_open(struct inode *inode, struct file *file)
{
	struct px4_card *card = container_of(file->private_data,
					     struct px4_card, misc);
	int ret = 0;

	if (mutex_lock_interruptible(&card->lock))
		return -ERESTARTSYS;

	/* T=1 の連番と再送状態は1つのセッションだけが持てるため、複数のプロセスには開かせない */
	if (card->disconnected) {
		ret = -ENODEV;
	} else if (card->opened) {
		ret = -EBUSY;
	} else {
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
		px4_card_power_off(card);
	card->opened = false;
	mutex_unlock(&card->lock);

	kref_put(&card->refs, px4_card_free);
	return 0;
}

static long px4_card_ioctl(struct file *file, unsigned int command,
			   unsigned long arg)
{
	struct px4_card *card = file->private_data;
	struct px4_card_io io;
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
		if (copy_from_user(&io, ptr, sizeof(io)))
			return -EFAULT;

		/* 使わない項目に値が入っていたら、ABI の取り違えとして拒否する */
		if (io.reserved || io.length > PX4_CARD_MAX_FRAME)
			return -EINVAL;
		if (command == PX4_CARD_POWER ? io.value > 1 :
		    command != PX4_CARD_SET_BAUDRATE && io.value)
			return -EINVAL;
		if ((command == PX4_CARD_READ || command == PX4_CARD_WRITE) != !!io.length)
			return -EINVAL;
		/* 読み出し系のコマンドで、利用者が渡した data をそのまま返さない */
		if (command != PX4_CARD_WRITE)
			memset(io.data, 0, sizeof(io.data));
		break;

	default:
		return -ENOTTY;
	}

	if (mutex_lock_interruptible(&card->lock))
		return -ERESTARTSYS;

	if (card->disconnected) {
		ret = -ENODEV;
	} else if (command == PX4_CARD_GET_INFO) {
		ret = copy_to_user(ptr, &info, sizeof(info)) ? -EFAULT : 0;
	} else if (mutex_lock_interruptible(card->backend.lock)) {
		ret = -ERESTARTSYS;
	} else {
		ret = px4_card_request(card, command, &io);
		mutex_unlock(card->backend.lock);

		if (!ret && (_IOC_DIR(command) & _IOC_READ))
			ret = copy_to_user(ptr, &io, sizeof(io)) ? -EFAULT : 0;
	}

	mutex_unlock(&card->lock);
	return ret;
}

static __poll_t px4_card_poll(struct file *file, poll_table *wait)
{
	struct px4_card *card = file->private_data;

	/* カードの挿抜は pcscd が定期的に問い合わせるため、ここでは USB 切断だけを通知する */
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
		      const struct px4_card_backend *backend,
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
	card->backend = *backend;
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

/*
 * 機種側で利用可能状態を 0 にしてから呼ぶ
 * 開いたままのファイル記述子はこの管理オブジェクトだけを参照し続けるため、機種のオブジェクトは close を待たずに解放できる
 */
void px4_card_unregister(struct px4_card *card)
{
	if (!card)
		return;

	/* misc_open() は misc のロックの下で px4_card_open() を呼ぶため、card->lock を取る前に登録を外す */
	misc_deregister(&card->misc);

	mutex_lock(&card->lock);
	px4_card_power_off(card);
	WRITE_ONCE(card->disconnected, true);
	mutex_unlock(&card->lock);

	wake_up_all(&card->wait);
	kref_put(&card->refs, px4_card_free);
}
