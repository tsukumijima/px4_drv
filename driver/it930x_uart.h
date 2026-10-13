// SPDX-License-Identifier: GPL-2.0-only
#ifndef __IT930X_UART_H__
#define __IT930X_UART_H__

/* it930x.h を直接共有すると、ユーザー空間の状態機械にもカーネル依存が入る。 */
enum it930x_uart_baudrate {
	IT930X_UART_BAUDRATE_9600 = 0,
	IT930X_UART_BAUDRATE_19200 = 1,
	IT930X_UART_BAUDRATE_38400 = 2,
};

#endif
