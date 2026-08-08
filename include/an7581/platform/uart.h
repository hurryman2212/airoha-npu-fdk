/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_UART_H
#define AN7581_UART_H

#include "an7581/platform/types.h"

#define AN7581_UART_INTERRUPT_SOURCE 22U
#define AN7581_UART_LINE_STATUS_DATA_READY UINT32_C(0x01)
#define AN7581_UART_LINE_STATUS_TRANSMIT_EMPTY UINT32_C(0x20)

void an7581_uart_configure(void);
bool an7581_uart_interrupt_initialize(void);

#endif
