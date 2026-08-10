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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "ch32x035.h"
#include "interrupt.h"
#include "uart.h"
#include "uart_mouse.h"

// How large the UART TX FIFO buffer should be. Note that actual capacity will
// be N-1 due to there being one slot wasted between head and tail.
#ifndef UART_MOUSE_TX_FIFO_SIZE
#define UART_MOUSE_TX_FIFO_SIZE 64
#endif

#if UART_MOUSE_TX_FIFO_SIZE < 2
#error "UART_MOUSE_TX_FIFO_SIZE value must be at least 2"
#endif
#if UART_MOUSE_TX_FIFO_SIZE & (UART_MOUSE_TX_FIFO_SIZE - 1)
#error "UART_MOUSE_TX_FIFO_SIZE value must be a power of 2 (e.g. 16, 32, etc.)"
#endif

// How large the UART RX FIFO buffer should be. Note that actual capacity will
// be N-1 due to there being one slot wasted between head and tail.
#ifndef UART_MOUSE_RX_FIFO_SIZE
#define UART_MOUSE_RX_FIFO_SIZE 8
#endif

#if UART_MOUSE_RX_FIFO_SIZE < 2
#error "UART_MOUSE_RX_FIFO_SIZE value must be at least 2"
#endif
#if UART_MOUSE_RX_FIFO_SIZE & (UART_MOUSE_RX_FIFO_SIZE - 1)
#error "UART_MOUSE_RX_FIFO_SIZE value must be a power of 2 (e.g. 16, 32, etc.)"
#endif

static uart_context_t uart_mouse_ctx;
static uart_mouse_format_enum_t uart_mouse_format;
static uint8_t uart_mouse_tx_fifo[UART_MOUSE_TX_FIFO_SIZE];
static uint8_t uart_mouse_rx_fifo[UART_MOUSE_RX_FIFO_SIZE];

/******************************************************************************/

static uart_format_enum_t uart_mouse_get_uart_format(const uart_mouse_format_enum_t format) {
	// Because our UART only supports 8 data bits (7 is unsupported), we fake
	// 7 data bits by using 8 with half a stop bit. When transmitting, we set
	// the MSb to '1' to make our own stop bit (effectively 1.5). When receiving
	// the as-transmitted stop bit will become the 8th data bit ('1'), and the
	// UART won't do any stop bit sampling, to avoid needing to have any idle
	// time between frames to serve as the stop bit.
	switch(format) {
		default:
		case UART_MOUSE_FORMAT_7N1: return UART_FORMAT_8N05; break;
		case UART_MOUSE_FORMAT_8N1: return UART_FORMAT_8N1; break;
	}
}

void uart_mouse_init(const uint32_t pclk2_freq_hz, const uint32_t baud_rate, const uart_mouse_format_enum_t format) {
	uart_context_init(
		&uart_mouse_ctx,
		USART2, baud_rate, uart_mouse_get_uart_format(format),
		uart_mouse_tx_fifo, sizeof(uart_mouse_tx_fifo) / sizeof(uart_mouse_tx_fifo[0]),
		uart_mouse_rx_fifo, sizeof(uart_mouse_rx_fifo) / sizeof(uart_mouse_rx_fifo[0])
	);
	uart_mouse_ctx.options.blocking_rx = false;
	uart_mouse_ctx.options.binary = true;
	uart_init(&uart_mouse_ctx, pclk2_freq_hz);
	uart_mouse_format = format;
}

void uart_mouse_set_baud_rate(const uint32_t pclk2_freq_hz, const uint32_t baud_rate) {
	uart_set_baud_rate(&uart_mouse_ctx, pclk2_freq_hz, baud_rate);
}

void uart_mouse_set_format(const uart_mouse_format_enum_t format) {
	uart_set_format(&uart_mouse_ctx, uart_mouse_get_uart_format(format));
	uart_mouse_format = format;
}

void uart_mouse_transmit_flush(void) {
	uart_transmit_flush(&uart_mouse_ctx);
}

int uart_mouse_putchar(int c) {
	// When we are doing 7-bit data, because our UART can only do 8-bit, we need
	// to make the MSb of each transmitted byte '1', which effectively makes it
	// 7 bit with an additional stop bit (because there's no difference between
	// a stop bit, a '1', and idle level).
	if(uart_mouse_format == UART_MOUSE_FORMAT_7N1) c |= 0x80;
	
	return uart_putchar(&uart_mouse_ctx, c);
}

void uart_mouse_receive_flush(void) {
	uart_receive_flush(&uart_mouse_ctx);
}

size_t uart_mouse_receive_available(void) {
	return uart_receive_available(&uart_mouse_ctx);
}

int uart_mouse_getchar(void) {
	int c = uart_getchar(&uart_mouse_ctx);
	
	// When we are doing 7-bit data, because our UART can only do 8-bit, we need
	// to unset the MSb of each received byte because its stop bit is
	// interpreted as a '1'. Only do this if uart_getchar() succeeded (not EOF).
	if(c != EOF && uart_mouse_format == UART_MOUSE_FORMAT_7N1) c &= 0x7F;
	
	return c;
}

ISR(USART2_IRQHandler) {
	uart_process_interrupt(&uart_mouse_ctx);
}
