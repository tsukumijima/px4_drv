// SPDX-License-Identifier: GPL-2.0-only
/*
 * 内蔵カードリーダーのデバイスファイル (/dev/px4card/<ID>) の機種共通層
 */

#include <linux/compat.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "px4_card.h"

struct px4_card {
	struct miscdevice misc;
	struct list_head node;
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

/* Windows 版と同じパスを組み立てるため、winusb/pkg/inf にある INF の DeviceInterfaceGUIDs を PID ごとに保持する */
static const struct {
	u16 product;
	const char *guid;
} px4_card_guids[] = {
	{ 0x083f, "{6d78c843-49d8-4bb8-8dfa-286fdee4c8a3}" },
	{ 0x084a, "{87b25983-d116-4ccf-8896-84d022fabd73}" },
	{ 0x023f, "{47fc15e3-6791-4e24-8fbe-c4218eee0ad6}" },
	{ 0x024a, "{60eaf2b6-548f-4aad-ab0b-87055528dfd6}" },
	{ 0x073f, "{3c42fdc6-2438-455e-89db-538011777617}" },
	{ 0x074a, "{c78aa8ba-8476-4047-a089-7c3ed43503a6}" },
	{ 0x024e, "{eeb8e93b-1007-4e3e-8ce9-4f7ebdf3aba1}" },
	{ 0x924e, "{eeb8e93b-1007-4e3e-8ce9-4f7ebdf3aba1}" },
	{ 0x0252, "{bfafd6cd-29f8-46e7-b558-e3b0c493aaba}" },
	{ 0x0253, "{145dce88-a4db-4737-8e5d-d1099814876a}" },
	{ 0x004b, "{1f8fa351-3f75-42c1-b0f1-ec794294448f}" },
	{ 0x084b, "{283639e3-0df2-40bd-a5cc-5051579745a7}" },
	{ 0x0254, "{da23f91d-62ad-401e-812e-a7f054d665af}" },
	{ 0x0854, "{28ec1517-6150-4025-acfb-a5447add0a57}" },
	{ 0x0855, "{a3303a3b-ea2c-467f-beae-8e72fb158714}" },
	{ 0x0052, "{ac1e5fa2-e265-4819-9b29-2be8a44ce4c9}" },
};

static DEFINE_IDA(px4_card_ids);
static LIST_HEAD(px4_cards);
static DEFINE_MUTEX(px4_card_names_lock);

static void px4_card_free(struct kref *ref)
{
	struct px4_card *card = container_of(ref, struct px4_card, refs);

	ida_free(&px4_card_ids, card->id);
	kfree(card->misc.name);
	kfree(card->misc.nodename);
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

char *px4_card_reader_id(struct usb_device *usb)
{
	u16 product = le16_to_cpu(usb->descriptor.idProduct);
	const char *serial = usb->serial;
	const char *guid = NULL;
	char *path, *character, *id;
	u64 hash = 14695981039346656037ULL;
	size_t length = serial ? strlen(serial) : 0;
	size_t i;

	/* シリアル番号は ASCII の英数字・ピリオド・ハイフン・アンダースコアに限り、Windows 版と同じパスの文字列を組み立てる */
	if (!length || length > NAME_MAX || !strcmp(serial, ".") || !strcmp(serial, "..") ||
	    strspn(serial, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") != length)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < ARRAY_SIZE(px4_card_guids); i++) {
		if (px4_card_guids[i].product == product) {
			guid = px4_card_guids[i].guid;
			break;
		}
	}
	/* PX-MLT5U など Windows 版の INF がない機種は、GUID を省いたパスで ID を求める */
	path = kasprintf(GFP_KERNEL, "\\\\?\\usb#vid_%04x&pid_%04x#%s%s%s",
			 le16_to_cpu(usb->descriptor.idVendor), product, usb->serial,
			 guid ? "#" : "", guid ? guid : "");
	if (!path)
		return ERR_PTR(-ENOMEM);

	/*
	 * DriverHost_PX4 の device_base.cpp にある HashDevicePath() と同じく、UTF-16 の各文字の値を FNV-1a に通す
	 * シリアル番号を ASCII に限っているため、小文字のパスの各バイトは UTF-16 の文字の値と一致する
	 */
	for (character = path; *character; character++) {
		hash ^= (u16)tolower(*character);
		hash *= 1099511628211ULL;
	}
	kfree(path);
	id = kasprintf(GFP_KERNEL, "%016llX", (unsigned long long)hash);
	return id ? id : ERR_PTR(-ENOMEM);
}

/*
 * px4_card_names_lock を保持した状態で呼ぶ
 * parent は USB インターフェースのデバイスである必要がある (各機種は px4_usb.c の &intf->dev を渡している)
 */
static int px4_card_set_nodename(struct px4_card *card, struct device *parent)
{
	struct usb_device *usb = interface_to_usbdev(to_usb_interface(parent));
	struct px4_card *entry;
	char *base, *name;
	unsigned int suffix = 0;
	bool duplicate;

	base = px4_card_reader_id(usb);
	/* 欠落・空・不正な番号は USB の接続位置で代用し、シリアル番号を使えない機器も登録する */
	if (IS_ERR(base) && PTR_ERR(base) == -EINVAL)
		base = kasprintf(GFP_KERNEL, "usb-%s", dev_name(&usb->dev));
	if (IS_ERR(base))
		return PTR_ERR(base);
	if (!base)
		return -ENOMEM;

	/*
	 * 同じ ID の別機器でも登録を維持するため、後から登録する側に空いている連番を付ける
	 * 名前の確認から登録・解除まで同じロックで保護し、同時接続でも各機器に別のデバイスファイルを割り当てる
	 */
	do {
		if (suffix)
			name = kasprintf(GFP_KERNEL, "px4card/%s-%u", base, suffix);
		else
			name = kasprintf(GFP_KERNEL, "px4card/%s", base);
		if (!name) {
			kfree(base);
			return -ENOMEM;
		}
		duplicate = false;
		list_for_each_entry(entry, &px4_cards, node) {
			if (!strcmp(entry->misc.nodename, name)) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			kfree(name);
		suffix++;
	} while (duplicate);
	kfree(base);
	card->misc.nodename = name;
	return 0;
}

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

	mutex_lock(&px4_card_names_lock);
	ret = px4_card_set_nodename(card, parent);
	if (!ret)
		ret = misc_register(&card->misc);
	if (!ret)
		list_add(&card->node, &px4_cards);
	mutex_unlock(&px4_card_names_lock);
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
	mutex_lock(&px4_card_names_lock);
	misc_deregister(&card->misc);
	list_del(&card->node);
	mutex_unlock(&px4_card_names_lock);

	mutex_lock(&card->lock);
	px4_card_power_off(card);
	WRITE_ONCE(card->disconnected, true);
	mutex_unlock(&card->lock);

	wake_up_all(&card->wait);
	kref_put(&card->refs, px4_card_free);
}
