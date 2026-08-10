/*******************************************************************************
 *
 * USB to Serial Mouse Converter firmware
 * Copyright (c) 2026 Basil Hussain
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 ******************************************************************************/

#ifndef UART_H_
#define UART_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ch32x035.h"

typedef enum {
	UART_FORMAT_8N05, // 8 data bits, no parity, 0.5 stop bits
	UART_FORMAT_8N1,  // 8 data bits, no parity, 1 stop bit
	UART_FORMAT_8N15, // 8 data bits, no parity, 1.5 stop bits
	UART_FORMAT_8N2,  // 8 data bits, no parity, 2 stop bits
	UART_FORMAT_8O05, // 8 data bits, odd parity, 0.5 stop bits
	UART_FORMAT_8O1,  // 8 data bits, odd parity, 1 stop bit
	UART_FORMAT_8O15, // 8 data bits, odd parity, 1.5 stop bits
	UART_FORMAT_8O2,  // 8 data bits, odd parity, 2 stop bits
	UART_FORMAT_8E05, // 8 data bits, even parity, 0.5 stop bits
	UART_FORMAT_8E1,  // 8 data bits, even parity, 1 stop bit
	UART_FORMAT_8E15, // 8 data bits, even parity, 1.5 stop bits
	UART_FORMAT_8E2,  // 8 data bits, even parity, 2 stop bits
} uart_format_enum_t;

typedef struct {
	bool transmitter;
	bool receiver;
	bool blocking_tx;
	bool blocking_rx;
	bool binary;
} uart_options_t;

typedef struct {
	size_t head;
	size_t tail;
	size_t buffer_size;
	uint8_t *buffer;
} uart_fifo_t;

typedef struct {
	volatile uart_fifo_t tx_fifo;
	volatile uart_fifo_t rx_fifo;
	uart_options_t options;
	USART_TypeDef *uart;
	IRQn_Type uart_irq;
	uint32_t baud;
	uart_format_enum_t format;
} uart_context_t;

/******************************************************************************/

extern void uart_context_init(uart_context_t * const ctx, USART_TypeDef * const uart, const uint32_t baud_rate, const uart_format_enum_t format, uint8_t * const tx_fifo_buf, const size_t tx_fifo_buf_size, uint8_t * const rx_fifo_buf, const size_t rx_fifo_buf_size) __attribute__((nonnull(1,2,5,7)));
extern void uart_init(uart_context_t * const ctx, const uint32_t pclk2_freq_hz) __attribute__((nonnull(1)));
extern void uart_set_baud_rate(uart_context_t * const ctx, const uint32_t pclk2_freq_hz, const uint32_t baud_rate) __attribute__((nonnull(1)));
extern void uart_set_format(uart_context_t * const ctx, const uart_format_enum_t format) __attribute__((nonnull(1)));
extern void uart_set_transmit_enabled(uart_context_t * const ctx, const bool value) __attribute__((nonnull(1)));
extern void uart_set_receive_enabled(uart_context_t * const ctx, const bool value) __attribute__((nonnull(1)));
extern void uart_set_blocking_tx(uart_context_t * const ctx, const bool value) __attribute__((nonnull(1)));
extern void uart_set_blocking_rx(uart_context_t * const ctx, const bool value) __attribute__((nonnull(1)));
extern void uart_set_binary(uart_context_t * const ctx, const bool value) __attribute__((nonnull(1)));
extern void uart_transmit_flush(uart_context_t * const ctx) __attribute__((nonnull(1)));
extern void uart_transmit_break(uart_context_t * const ctx) __attribute__((nonnull(1)));
extern int uart_putchar(uart_context_t * const ctx, int c) __attribute__((nonnull(1)));
extern size_t uart_receive_available(uart_context_t * const ctx) __attribute__((nonnull(1)));
extern void uart_receive_flush(uart_context_t * const ctx) __attribute__((nonnull(1)));
extern int uart_getchar(uart_context_t * const ctx) __attribute__((nonnull(1)));
extern void uart_process_interrupt(uart_context_t * const ctx) __attribute__((nonnull(1)));

#endif // UART_H_
