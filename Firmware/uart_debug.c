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

#include <stdint.h>
#include <stdbool.h>
#include "ch32x035.h"
#include "interrupt.h"
#include "uart.h"
#include "uart_debug.h"

// How large the UART TX FIFO buffer should be. Note that actual capacity will
// be N-1 due to there being one slot wasted between head and tail.
#ifndef UART_DEBUG_TX_FIFO_SIZE
#define UART_DEBUG_TX_FIFO_SIZE 128
#endif

#if UART_DEBUG_TX_FIFO_SIZE < 2
#error "UART_DEBUG_TX_FIFO_SIZE value must be at least 2"
#endif
#if UART_DEBUG_TX_FIFO_SIZE & (UART_DEBUG_TX_FIFO_SIZE - 1)
#error "UART_DEBUG_TX_FIFO_SIZE value must be a power of 2 (e.g. 16, 32, etc.)"
#endif

// How large the UART RX FIFO buffer should be. Note that actual capacity will
// be N-1 due to there being one slot wasted between head and tail.
// NOTE: we don't do RX on the debug connection, so just set to smallest size.
#ifndef UART_DEBUG_RX_FIFO_SIZE
#define UART_DEBUG_RX_FIFO_SIZE 2
#endif

#if UART_DEBUG_RX_FIFO_SIZE < 2
#error "UART_DEBUG_RX_FIFO_SIZE value must be at least 2"
#endif
#if UART_DEBUG_RX_FIFO_SIZE & (UART_DEBUG_RX_FIFO_SIZE - 1)
#error "UART_DEBUG_RX_FIFO_SIZE value must be a power of 2 (e.g. 16, 32, etc.)"
#endif

static uart_context_t uart_debug_ctx;
static uint8_t uart_debug_tx_fifo[UART_DEBUG_TX_FIFO_SIZE];
static uint8_t uart_debug_rx_fifo[UART_DEBUG_RX_FIFO_SIZE];

/******************************************************************************/

void uart_debug_init(const uint32_t pclk2_freq_hz, const uint32_t baud_rate) {
	uart_context_init(
		&uart_debug_ctx,
		USART4, baud_rate, UART_FORMAT_8N1,
		uart_debug_tx_fifo, sizeof(uart_debug_tx_fifo) / sizeof(uart_debug_tx_fifo[0]),
		uart_debug_rx_fifo, sizeof(uart_debug_rx_fifo) / sizeof(uart_debug_rx_fifo[0])
	);
	uart_debug_ctx.options.receiver = false;
	uart_init(&uart_debug_ctx, pclk2_freq_hz);
}

int uart_debug_putchar(const int c) {
	return uart_putchar(&uart_debug_ctx, c);
}

ISR(USART4_IRQHandler) {
	uart_process_interrupt(&uart_debug_ctx);
}
