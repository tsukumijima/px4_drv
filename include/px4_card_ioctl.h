// SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note
#ifndef __PX4_CARD_IOCTL_H__
#define __PX4_CARD_IOCTL_H__

#include <linux/ioctl.h>
#include <linux/types.h>

#define PX4_CARD_ABI_VERSION 1
#define PX4_CARD_MAX_FRAME 255

/*
 * ネイティブ型や暗黙の整列に依存すると、32bit/64bit で ioctl の番号や配置が変わる。
 * 同じ ioctl 番号で配置や意味を変えると、旧 IFD と互換性がなくなる。
 */
struct px4_card_info {
	__u32 abi_version;
	__u32 max_frame;
	__u32 reserved[2];
};

struct px4_card_io {
	__aligned_u64 deadline_ns;
	__u32 value;
	__u32 length;
	__u8 data[PX4_CARD_MAX_FRAME];
	__u8 reserved;
};

#define PX4_CARD_GET_INFO _IOR('X', 0x40, struct px4_card_info)
#define PX4_CARD_POWER _IOW('X', 0x41, struct px4_card_io)
#define PX4_CARD_DETECT _IOWR('X', 0x42, struct px4_card_io)
#define PX4_CARD_RESET _IOW('X', 0x43, struct px4_card_io)
#define PX4_CARD_SET_BAUDRATE _IOW('X', 0x44, struct px4_card_io)
#define PX4_CARD_RX_READY _IOWR('X', 0x45, struct px4_card_io)
#define PX4_CARD_READ _IOWR('X', 0x46, struct px4_card_io)
#define PX4_CARD_WRITE _IOW('X', 0x47, struct px4_card_io)

#endif
