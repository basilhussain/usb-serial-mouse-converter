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

/******************************************************************************/

// NOTE: actual FIFO capacity will be N-1 due to there being one slot wasted
// between head and tail.

static inline bool uart_fifo_is_empty(volatile uart_fifo_t * const fifo) {
	// When head and tail are equal, it means the buffer is empty.
	return (fifo->head == fifo->tail);
}

static inline bool uart_fifo_is_full(volatile uart_fifo_t * const fifo) {
	// If incrementing the head index (as if to push a new value) means it will
	// catch up with the tail, then that means the buffer is full.
	return (((fifo->head + 1) % fifo->buffer_size) == fifo->tail);
}

static inline void uart_fifo_push(volatile uart_fifo_t * const fifo, const uint8_t c) {
	// Increment the head index and push the given new value to that position.
	fifo->head = (fifo->head + 1) % fifo->buffer_size;
	fifo->buffer[fifo->head] = c;
}

static inline uint8_t uart_fifo_pop(volatile uart_fifo_t * const fifo) {
	// Increment the tail index and retrieve the value at that position.
	fifo->tail = (fifo->tail + 1) % fifo->buffer_size;
	return fifo->buffer[fifo->tail];
}

static inline uint8_t uart_fifo_peek_tail(volatile uart_fifo_t * const fifo) {
	// Retrieve the value from tail without popping it.
	const size_t tail = (fifo->tail + 1) % fifo->buffer_size;
	return fifo->buffer[tail];
}

static inline uint8_t uart_fifo_peek_head(volatile uart_fifo_t * const fifo) {
	// Retrieve the value from head without popping it.
	return fifo->buffer[fifo->head];
}

static inline size_t uart_fifo_count(volatile uart_fifo_t * const fifo) {
	// Calculate the number of values in the FIFO.
	return (size_t)(fifo->buffer_size + fifo->head - fifo->tail) % fifo->buffer_size;
}

static inline void uart_fifo_flush(volatile uart_fifo_t * const fifo) {
	// Empty the FIFO by resetting the head and tail.
	fifo->head = 0;
	fifo->tail = 0;
}

static uint16_t uart_get_brr(const uart_context_t * const ctx, const uint32_t pclk2_freq_hz) {
	uint32_t div, div_int, div_frac;
	uint16_t brr_val;

	// The baud rate is computed using the following formula:
	// - IntegerDivider = ((PCLKx) / (16 * (USART_InitStruct->USART_BaudRate)))
	// - FractionalDivider = ((IntegerDivider - ((u32) IntegerDivider)) * 16) + 0.5

	// Configure the baud rate divider according to current peripheral clock
	// that the USART is running from.
	div = (pclk2_freq_hz * 25) / (ctx->baud * 4);
	div_int = div / 100;
	div_frac = ((((div - (div_int * 100)) * 16) + 50) / 100);
	brr_val = ((uint16_t)div_int << __builtin_ctz(USART_BRR_DIV_Mantissa)) & USART_BRR_DIV_Mantissa;
	brr_val |= (uint16_t)div_frac & USART_BRR_DIV_Fraction;

	return brr_val;
}

static uint16_t uart_get_ctlr1(const uart_context_t * const ctx) {
	// RX-not-empty interrupt always enabled by default (TX-empty interrupt will
	// be enabled elsewhere as-and-when necessary).
	uint16_t ctlr1_val = USART_CTLR1_UE | USART_CTLR1_RXNEIE;
	
	// Enable receiver and transmitter according to options.
	if(ctx->options.receiver) ctlr1_val |= USART_CTLR1_RE;
	if(ctx->options.transmitter) ctlr1_val |= USART_CTLR1_TE;
	
	// Don't need to do anything for word length, 'M' when cleared is 8 bit.
	
	// Set configuration for parity.
	switch(ctx->format) {
		case UART_FORMAT_8N05:
		case UART_FORMAT_8N1:
		case UART_FORMAT_8N15:
		case UART_FORMAT_8N2:
			// No parity, do nothing (enable flag is already zero).
			break;
		case UART_FORMAT_8O05:
		case UART_FORMAT_8O1:
		case UART_FORMAT_8O15:
		case UART_FORMAT_8O2:
			// Enable odd parity.
			ctlr1_val |= USART_CTLR1_PCE | USART_CTLR1_PS;
			break;
		case UART_FORMAT_8E05:
		case UART_FORMAT_8E1:
		case UART_FORMAT_8E15:
		case UART_FORMAT_8E2:
			// Enable even parity.
			ctlr1_val |= USART_CTLR1_PCE;
			break;
	}

	return ctlr1_val;
}

static uint16_t uart_get_ctlr2(const uart_context_t * const ctx) {
	uint16_t ctlr2_val = 0;
	
	// Set configuration for stop bit length.
	switch(ctx->format) {
		case UART_FORMAT_8N05:
		case UART_FORMAT_8O05:
		case UART_FORMAT_8E05:
			ctlr2_val |= USART_CTLR2_STOP_BITS_0_5;
			break;
		case UART_FORMAT_8N1:
		case UART_FORMAT_8O1:
		case UART_FORMAT_8E1:
			ctlr2_val |= USART_CTLR2_STOP_BITS_1;
			break;
		case UART_FORMAT_8N15:
		case UART_FORMAT_8O15:
		case UART_FORMAT_8E15:
			ctlr2_val |= USART_CTLR2_STOP_BITS_1_5;
			break;
		case UART_FORMAT_8N2:
		case UART_FORMAT_8O2:
		case UART_FORMAT_8E2:
			ctlr2_val |= USART_CTLR2_STOP_BITS_2;
			break;
	}
	
	return ctlr2_val;
}

void uart_context_init(uart_context_t * const ctx, USART_TypeDef * const uart, const uint32_t baud_rate, const uart_format_enum_t format, uint8_t * const tx_fifo_buf, const size_t tx_fifo_buf_size, uint8_t * const rx_fifo_buf, const size_t rx_fifo_buf_size) {
	// Save pointer to given USART peripheral, as well as given baud rate and
	// format.
	ctx->uart = uart;
	ctx->baud = baud_rate;
	ctx->format = format;
	
	// Also save the IRQ number of the given UART.
	switch((uintptr_t)uart) {
		case (uintptr_t)USART1: ctx->uart_irq = USART1_IRQn; break;
		case (uintptr_t)USART2: ctx->uart_irq = USART2_IRQn; break;
		case (uintptr_t)USART3: ctx->uart_irq = USART3_IRQn; break;
		case (uintptr_t)USART4: ctx->uart_irq = USART4_IRQn; break;
	}
	
	// Set default options.
	ctx->options.transmitter = true;
	ctx->options.receiver = true;
	ctx->options.blocking_tx = true;
	ctx->options.blocking_rx = true;
	ctx->options.binary = false;
	
	// Initialise TX FIFO, using given buffer.
	ctx->tx_fifo.buffer = tx_fifo_buf;
	ctx->tx_fifo.buffer_size = tx_fifo_buf_size;
	ctx->tx_fifo.head = 0;
	ctx->tx_fifo.tail = 0;
	
	// Initialise RX FIFO, using given buffer.
	ctx->rx_fifo.buffer = rx_fifo_buf;
	ctx->rx_fifo.buffer_size = rx_fifo_buf_size;
	ctx->rx_fifo.head = 0;
	ctx->rx_fifo.tail = 0;
}

void uart_init(uart_context_t * const ctx, const uint32_t pclk2_freq_hz) {
	// Enable peripheral clock for given USART.
	switch((uintptr_t)ctx->uart) {
		case (uintptr_t)USART1: RCC->APB2PCENR |= RCC_USART1EN; break;
		case (uintptr_t)USART2: RCC->APB1PCENR |= RCC_USART2EN; break;
		case (uintptr_t)USART3: RCC->APB1PCENR |= RCC_USART3EN; break;
		case (uintptr_t)USART4: RCC->APB1PCENR |= RCC_USART4EN; break;
	}
	
	// Set baud rate divider and UART configuration (data length, parity, stop
	// bits, interrupts) according to context. This enables the UART, so we set
	// CTLR1 last.
	ctx->uart->BRR = uart_get_brr(ctx, pclk2_freq_hz);
	ctx->uart->CTLR3 = 0;
	ctx->uart->CTLR2 = uart_get_ctlr2(ctx);
	ctx->uart->CTLR1 = uart_get_ctlr1(ctx);
	
	/*
	// Configure for 8 data bits, no parity, 1 stop bit, no flow control, only
	// RX-not-empty interrupt (TX-empty interrupt will be enabled when necessary
	// later), and also enable receiver and transmitter according to options.
	ctx->uart->CTLR1 = USART_CTLR1_RXNEIE | (ctx->options.receiver ? USART_CTLR1_RE : 0) | (ctx->options.transmitter ? USART_CTLR1_TE : 0);
	ctx->uart->CTLR2 = USART_CTLR2_STOP_BITS_1;
	ctx->uart->CTLR3 = 0;
	
	// Calculate and set the baud rate divider.
	ctx->uart->BRR = uart_calculate_brr(pclk2_freq_hz, ctx->baud);
	
	// Finally, enable the USART.
	ctx->uart->CTLR1 |= USART_CTLR1_UE;
	*/

	// Enable the relevant interrupt for given USART.
	interrupt_enable(ctx->uart_irq);
	/*
	switch((uintptr_t)ctx->uart) {
		case (uintptr_t)USART1: interrupt_enable(USART1_IRQn); break;
		case (uintptr_t)USART2: interrupt_enable(USART2_IRQn); break;
		case (uintptr_t)USART3: interrupt_enable(USART3_IRQn); break;
		case (uintptr_t)USART4: interrupt_enable(USART4_IRQn); break;
	}
	*/
}

void uart_set_baud_rate(uart_context_t * const ctx, const uint32_t pclk2_freq_hz, const uint32_t baud_rate) {
	// Calculate and set the new baud rate divider.
	ctx->baud = baud_rate;
	ctx->uart->BRR = uart_get_brr(ctx, pclk2_freq_hz);
}

void uart_set_format(uart_context_t * const ctx, const uart_format_enum_t format) {
	ctx->format = format;
	ctx->uart->CTLR2 = uart_get_ctlr2(ctx);
	ctx->uart->CTLR1 = uart_get_ctlr1(ctx);
}

void uart_set_transmit_enabled(uart_context_t * const ctx, const bool value) {
	ctx->options.transmitter = value;
	ctx->uart->CTLR1 = uart_get_ctlr1(ctx);
	
	/*
	// Enable or disable the UART transmitter depending on value given.
	if(value) {
		ctx->uart->CTLR1 |= USART_CTLR1_TE;
	} else {
		ctx->uart->CTLR1 &= ~USART_CTLR1_TE;
	}
	
	ctx->options.transmitter = value;
	*/
}

void uart_set_receive_enabled(uart_context_t * const ctx, const bool value) {
	ctx->options.receiver = value;
	ctx->uart->CTLR1 = uart_get_ctlr1(ctx);
	
	/*
	// Enable or disable the UART receiver depending on value given.
	if(value) {
		ctx->uart->CTLR1 |= USART_CTLR1_RE;
	} else {
		ctx->uart->CTLR1 &= ~USART_CTLR1_RE;
	}
	
	ctx->options.receiver = value;
	*/
}

void uart_set_blocking_tx(uart_context_t * const ctx, const bool value) {
	ctx->options.blocking_tx = value;
}

void uart_set_blocking_rx(uart_context_t * const ctx, const bool value) {
	ctx->options.blocking_rx = value;
}

void uart_set_binary(uart_context_t * const ctx, const bool value) {
	ctx->options.binary = value;
}

void uart_transmit_flush(uart_context_t * const ctx) {
	// Wait for UART TXE interrupt to be disabled (i.e. the ISR disabled it
	// because it emptied out the FIFO), and transmit complete (TC) status flag
	// to be set (i.e. UART actually finished transmission).
	while((ctx->uart->CTLR1 & USART_CTLR1_TXEIE) || !(ctx->uart->STATR & USART_STATR_TC));
}

void uart_transmit_break(uart_context_t * const ctx) {
	// Tell the USART to send a break character.
	ctx->uart->CTLR1 |= USART_CTLR1_SBK;

	// Wait for the break to finish transmitting.
	while(ctx->uart->CTLR1 & USART_CTLR1_SBK);
}

int uart_putchar(uart_context_t * const ctx, int c) {
	// When binary mode is not set and character to transmit is LF, send a CR
	// preceding it.
	if(!ctx->options.binary && c == '\n') uart_putchar(ctx, '\r');

	// Are the FIFO buffer and the UART TX data register empty? Note that even
	// without locking, we cannot get a false-positive on emptiness, as we're
	// the sole producer filling the FIFO. Worst case we push to the FIFO even
	// though it was actually empty.
	if(uart_fifo_is_empty(&ctx->tx_fifo) && (ctx->uart->STATR & USART_STATR_TXE)) {
		// If so, just send the character immediately. Note that having read the
		// status register and now writing data register, TC flag will be
		// cleared automatically.
		ctx->uart->DATAR = c & USART_DATAR_DR;
	} else {
		// Otherwise, push the character on to the FIFO and enable UART TXE
		// interrupt (if it's not already) so the buffer can be sent.
		if(ctx->options.blocking_tx) {
			// For blocking behaviour, wait for the FIFO to not be full. Here we
			// could get a false-positive on fullness, but worst case is we just
			// loop around for another check.
			while(uart_fifo_is_full(&ctx->tx_fifo));
			uart_fifo_push(&ctx->tx_fifo, c);
			ctx->uart->CTLR1 |= USART_CTLR1_TXEIE;
		} else {
			// For non-blocking, only push to FIFO if not full; otherwise,
			// return error status with EOF. If false-positive on fullness, we
			// lose the character.
			if(!uart_fifo_is_full(&ctx->tx_fifo)) {
				uart_fifo_push(&ctx->tx_fifo, c);
				ctx->uart->CTLR1 |= USART_CTLR1_TXEIE;
			} else {
				return EOF;
			}
		}
	}

	return c;
}

size_t uart_receive_available(uart_context_t * const ctx) {
	return uart_fifo_count(&ctx->rx_fifo);
}

void uart_receive_flush(uart_context_t * const ctx) {
	// Temporarily disable UART interrupt while flushing the RX FIFO.
	interrupt_atomic_block_individual(ctx->uart_irq) {
		uart_fifo_flush(&ctx->rx_fifo);
	}
}

int uart_getchar(uart_context_t * const ctx) {
	if(ctx->options.blocking_rx) {
		// Blocking behaviour. Wait until the RX FIFO has something in it.
		while(uart_fifo_is_empty(&ctx->rx_fifo));
	} else {
		// Non-blocking behaviour. If the RX FIFO is empty, signal end-of-file.
		if(uart_fifo_is_empty(&ctx->rx_fifo)) return EOF;
	}
	
	// Pop next character off the RX FIFO and return it.
	return uart_fifo_pop(&ctx->rx_fifo);
	
	/*
	if(ctx->options.blocking_rx) {
		// Blocking behaviour. Wait until a character has been received.
		while(!(ctx->uart->STATR & USART_STATR_RXNE));
	} else {
		// Non-blocking behaviour. If nothing received, signal end-of-file.
		if(!(ctx->uart->STATR & USART_STATR_RXNE)) return EOF;
	}

	// Getting to here means we received a character. However, if there was a
	// framing or overrun error, ignore what was received and return error code;
	// otherwise return the byte received.
	if(ctx->uart->STATR & (USART_STATR_ORE | USART_STATR_FE)) {
		(void)ctx->uart->DATAR; // Resets the error flags.
		return EOF;
	} else {
		return ctx->uart->DATAR;
	}
	*/
}

void uart_process_interrupt(uart_context_t * const ctx) {
	// Read the UART status register only once.
	const uint16_t statr = ctx->uart->STATR;
	
	// If we had a TX-empty interrupt, then pop the next byte off the TX FIFO
	// and write it to the UART data register. Note that previous read of status
	// register and then write of data register clears TXE flag. Then if the
	// FIFO is now empty and no more data, disable the TXE interrupt. Note that
	// we cannot get a false-positive on emptiness here, as the producer filling
	// the buffer will not have a chance to execute until this ISR returns.
	if(ctx->uart->CTLR1 & USART_CTLR1_TXEIE && statr & USART_STATR_TXE) {
		ctx->uart->DATAR = uart_fifo_pop(&ctx->tx_fifo) & USART_DATAR_DR;
		if(uart_fifo_is_empty(&ctx->tx_fifo)) ctx->uart->CTLR1 &= ~USART_CTLR1_TXEIE;
	}
	
	// If we had an RX-not-empty interrupt, then read the received byte from the
	// UART data register. Note that previous read of status register and then
	// read of data register clears RXNE, NE, FE, and PE flags. Then if we
	// didn't have a noise, framing or parity error, and the FIFO is not full,
	// push the received byte onto the FIFO.
	if(ctx->uart->CTLR1 & USART_CTLR1_RXNEIE && statr & USART_STATR_RXNE) {
		const int c = ctx->uart->DATAR;
		if(!(statr & (USART_STATR_NE | USART_STATR_FE | USART_STATR_PE)) && !uart_fifo_is_full(&ctx->rx_fifo)) {
			uart_fifo_push(&ctx->rx_fifo, c);
		}
	}
}
