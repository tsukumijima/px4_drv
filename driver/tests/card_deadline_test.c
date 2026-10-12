// SPDX-License-Identifier: GPL-2.0-only
/* 実際の制御・電源関数に対し、USB の応答キューと経過時間だけを模擬する。 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint64_t u64;
#define NSEC_PER_MSEC 1000000ULL
#define DIV_ROUND_UP_ULL(n, d) (((n) + (d) - 1) / (d))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define unlikely(x) (x)
#define dev_err(dev, ...) ((void)0)
#define dev_dbg(dev, ...) ((void)0)
#define mutex_lock(p) ((void)0)
#define mutex_unlock(p) ((void)0)
#define atomic_read(p) (*(p))
#define IT930X_GPIO_OUT 2

struct usb_device { int unused; };
struct itedtv_bus {
	void *ctrl_deadline_owner;
	u64 ctrl_deadline_ns;
	void *dev;
	struct { int ctrl_timeout; struct usb_device *dev; } usb;
};
struct it930x_priv { int ctrl_lock; u8 *buf; u8 seq; };
struct it930x_bridge { struct itedtv_bus bus; void *dev; struct it930x_priv *priv; };
struct it930x_ctrl_buf { u8 *buf; u8 len; };
struct pxmlt_device {
	unsigned int open_count;
	bool card_powered;
	int available, lock;
	void *dev;
	struct it930x_bridge it930x;
};

static int task, other_task;
static void *current = &task;
static u64 now_ns, tx_delay_ns, rx_delay_ns;
static unsigned int tx_calls, rx_calls, queued, consumed, gpio_writes;
static int tx_timeout, rx_timeout, rx_error, fail_command;
static u8 responses[64];
static bool gpio[16];
/* GPIO の副作用を USB 送信成功時へ置き、応答の回収より前に反映する。 */
enum { COMMAND_GPIO = 0x100, COMMAND_MODE, COMMAND_UART, COMMAND_QUERY };

static u64 ktime_get_ns(void) { return now_ns; }
static void mdelay(unsigned int ms) { now_ns += ms * NSEC_PER_MSEC; }
static void msleep(unsigned int ms) { mdelay(ms); }
static int usb_sndbulkpipe(struct usb_device *dev, int pipe)
{
	(void)dev;
	return pipe;
}
static int usb_rcvbulkpipe(struct usb_device *dev, int pipe)
{
	(void)dev;
	return pipe;
}

static int usb_bulk_msg(struct usb_device *dev, int pipe, void *buffer,
			int length, int *actual, int timeout)
{
	u8 *buf = buffer;
	(void)dev;
	assert(timeout >= 0);
	if (pipe == 2) {
		int command = (buf[1] << 8) | buf[2];
		tx_calls++;
		tx_timeout = timeout;
		if (command == fail_command)
			return -EIO;
		if (timeout && tx_delay_ns >= (u64)timeout * NSEC_PER_MSEC) {
			mdelay(timeout);
			return -ETIMEDOUT;
		}
		now_ns += tx_delay_ns;
		if (command == COMMAND_GPIO) {
			gpio[buf[4]] = buf[5];
			gpio_writes++;
		}
		assert(queued < sizeof(responses));
		responses[queued++] = buf[3];
		*actual = length;
	} else {
		u8 seq;
		u16 checksum;
		assert(pipe == 0x81);
		rx_calls++;
		rx_timeout = timeout;
		if (rx_error)
			return rx_error;
		if (timeout && rx_delay_ns >= (u64)timeout * NSEC_PER_MSEC) {
			mdelay(timeout);
			return -ETIMEDOUT;
		}
		now_ns += rx_delay_ns;
		assert(consumed < queued);
		seq = responses[consumed++];
		checksum = (u16)~((u16)seq << 8);
		buf[0] = 4;
		buf[1] = seq;
		buf[2] = 0;
		buf[3] = checksum >> 8;
		buf[4] = checksum;
		*actual = 5;
	}
	return 0;
}

#include "bus.inc"
#define itedtv_bus_ctrl_tx itedtv_usb_ctrl_tx
#define itedtv_bus_ctrl_rx itedtv_usb_ctrl_rx
#include "ctrl.inc"

static int it930x_write_gpio(struct it930x_bridge *bridge, int pin, bool value)
{
	u8 data[] = { pin, value };
	struct it930x_ctrl_buf buf = { data, sizeof(data) };
	return it930x_ctrl_msg(bridge, COMMAND_GPIO, &buf, NULL, NULL, false);
}

static int it930x_set_gpio_mode(struct it930x_bridge *bridge, int pin,
			       int mode, bool enable)
{
	(void)pin;
	(void)mode;
	(void)enable;
	return it930x_ctrl_msg(bridge, COMMAND_MODE, NULL, NULL, NULL, false);
}

static int it930x_bcas_init(struct it930x_bridge *bridge)
{
	return it930x_ctrl_msg(bridge, COMMAND_UART, NULL, NULL, NULL, false);
}

#include "power.inc"

static u8 buffer[256];
static struct it930x_priv priv;
static struct pxmlt_device device;

static void reset(void)
{
	memset(&device, 0, sizeof(device));
	memset(&priv, 0, sizeof(priv));
	memset(gpio, 0, sizeof(gpio));
	priv.buf = buffer;
	device.it930x.priv = &priv;
	device.it930x.bus.usb.ctrl_timeout = 3000;
	device.available = 1;
	gpio[7] = true;
	now_ns = NSEC_PER_MSEC;
	tx_delay_ns = rx_delay_ns = 0;
	tx_calls = rx_calls = queued = consumed = gpio_writes = 0;
	rx_error = fail_command = 0;
	current = &task;
}

static void deadline(u64 remaining_ns)
{
	itedtv_bus_set_ctrl_deadline(&device.it930x.bus, now_ns + remaining_ns);
}

static int query(void)
{
	return it930x_ctrl_msg(&device.it930x, COMMAND_QUERY, NULL, NULL, NULL, false);
}

static void assert_following_controls(void)
{
	itedtv_bus_clear_ctrl_deadline(&device.it930x.bus);
	tx_delay_ns = rx_delay_ns = 0;
	for (int i = 0; i < 3; i++)
		assert(query() == 0);
	assert(queued == consumed);
}

static void test_control_deadline(void)
{
	/* 送信前の期限切れでは、新しい USB 要求を出さない。 */
	reset();
	deadline(0);
	assert(query() == -ETIMEDOUT);
	assert(!tx_calls && !rx_calls);
	assert_following_controls();

	/* TX 後の既存1ms待機だけで期限切れになっても応答を残さない。 */
	reset();
	deadline(500000);
	assert(query() == -ETIMEDOUT);
	assert(tx_calls == 1 && rx_calls == 1 && queued == consumed);
	assert_following_controls();

	/* 送信中・応答待ち中にカード期限を越えても、USB の正常応答を回収する。 */
	for (int receive = 0; receive < 2; receive++) {
		reset();
		deadline(3 * NSEC_PER_MSEC);
		if (receive)
			rx_delay_ns = 5 * NSEC_PER_MSEC;
		else
			tx_delay_ns = 5 * NSEC_PER_MSEC;
		assert(query() == -ETIMEDOUT);
		assert(tx_calls == 1 && rx_calls == 1 && queued == consumed);
		assert_following_controls();
	}

	/* カード以外の task の既存タイムアウトは変えない。 */
	reset();
	deadline(0);
	current = &other_task;
	device.it930x.bus.usb.ctrl_timeout = 17;
	assert(query() == 0 && tx_timeout == 17 && rx_timeout == 17);

	/* timeout=0 でもカードの後始末を無期限にしない。 */
	reset();
	device.it930x.bus.usb.ctrl_timeout = 0;
	deadline(500000);
	assert(query() == -ETIMEDOUT);
	assert(tx_timeout == 3000 && rx_timeout == 3000 && queued == consumed);

	/* 本来の USB 切断エラーを期限切れで上書きしない。 */
	reset();
	deadline(500000);
	rx_error = -ENODEV;
	assert(query() == -ENODEV);
	assert(tx_calls == 1 && rx_calls == 1);

	/* 実際に USB が応答しない場合も、設定した時間で待機が終わる。 */
	reset();
	deadline(500000);
	device.it930x.bus.usb.ctrl_timeout = 20;
	rx_delay_ns = 100 * NSEC_PER_MSEC;
	assert(query() == -ETIMEDOUT);
	assert(rx_timeout == 20 && now_ns < 25 * NSEC_PER_MSEC);
	puts("control deadline: passed");
}

static void test_power_rollback(void)
{
	/* 投入途中と投入直後、両方の期限切れを同じ後始末へ通す。 */
	const unsigned int limits[] = { 50, 90 };
	for (unsigned int i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
		reset();
		deadline(limits[i] * NSEC_PER_MSEC);
		assert(pxmlt_card_power(&device, true) == -ETIMEDOUT);
		assert(!device.card_powered && !gpio[2] && gpio[7]);
		assert(queued == consumed);
		pxmlt_card_close(&device);
		assert(!device.card_powered && !gpio[2] && gpio[7]);
	}

	/* UART 初期化失敗でも電源を回収し、元のエラーを返す。 */
	reset();
	deadline(3000 * NSEC_PER_MSEC);
	fail_command = COMMAND_UART;
	assert(pxmlt_card_power(&device, true) == -EIO);
	assert(!device.card_powered && !gpio[2] && gpio[7]);

	/* 受信機が電源を使っている場合、カード失敗で共通電源を切らない。 */
	reset();
	device.open_count = 1;
	gpio[2] = true;
	gpio[7] = false;
	deadline(500000);
	assert(pxmlt_card_power(&device, true) == -ETIMEDOUT);
	assert(!device.card_powered && gpio[2] && !gpio[7] && !gpio_writes);
	pxmlt_card_close(&device);
	assert(gpio[2] && !gpio[7] && !gpio_writes);

	/* 正常なカード単独利用は、close で最後の電源参照を解放する。 */
	reset();
	deadline(3000 * NSEC_PER_MSEC);
	assert(pxmlt_card_power(&device, true) == 0);
	assert(device.card_powered && gpio[2] && !gpio[7]);
	pxmlt_card_close(&device);
	assert(!device.card_powered && !gpio[2] && gpio[7]);
	puts("power rollback: passed");
}

int main(int argc, char **argv)
{
	if (argc == 1 || !strcmp(argv[1], "control"))
		test_control_deadline();
	if (argc == 1 || !strcmp(argv[1], "power"))
		test_power_rollback();
	return 0;
}
