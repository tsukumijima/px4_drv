// SPDX-License-Identifier: GPL-2.0-only
#ifndef __IT930X_UART_H__
#define __IT930X_UART_H__

/* ユーザー空間の IFD ハンドラーと共有する T=1 の実装が、カーネル向けの it930x.h に依存しないよう分けている */
enum it930x_uart_baudrate {
	IT930X_UART_BAUDRATE_9600 = 0,
	IT930X_UART_BAUDRATE_19200 = 1,
	IT930X_UART_BAUDRATE_38400 = 2,
};

#endif
