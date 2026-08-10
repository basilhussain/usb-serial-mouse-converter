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

#ifndef UART_MOUSE_H_
#define UART_MOUSE_H_

#include <stddef.h>
#include <stdint.h>

typedef enum {
	UART_MOUSE_FORMAT_7N1,
	UART_MOUSE_FORMAT_8N1,
} uart_mouse_format_enum_t;

extern void uart_mouse_init(const uint32_t pclk2_freq_hz, const uint32_t baud_rate, const uart_mouse_format_enum_t format);
extern void uart_mouse_set_baud_rate(const uint32_t pclk2_freq_hz, const uint32_t baud_rate);
extern void uart_mouse_set_format(const uart_mouse_format_enum_t format);
extern void uart_mouse_transmit_flush(void);
extern int uart_mouse_putchar(int c);
extern void uart_mouse_receive_flush(void);
extern size_t uart_mouse_receive_available(void);
extern int uart_mouse_getchar(void);

#endif // UART_MOUSE_H_
