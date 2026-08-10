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

#ifndef EVENT_H_
#define EVENT_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
	EVENT_TYPE_NONE                 = 0x00,
	EVENT_TYPE_USB_DEV_CONNECT      = 0x10,
	EVENT_TYPE_USB_DEV_DISCONNECT   = 0x11,
	EVENT_TYPE_USB_PWR_FAULT        = 0x12,
	EVENT_TYPE_USB_MOUSE_INTERRUPT  = 0x20,
	EVENT_TYPE_SERIAL_MOUSE_ENABLE  = 0x30,
	EVENT_TYPE_SERIAL_MOUSE_DISABLE = 0x31
} event_type_t;

typedef struct {
	event_type_t type;
	union {
		uint8_t bytes[4];
		uint32_t word;
	} param; // General-purpose parameter
} event_t;

// Initialize/reset the internal FIFO. Can be called multiple times.
extern void event_fifo_init(void);

// Push an event onto the FIFO. Returns true on success, false if queue is full.
extern bool event_fifo_push(const event_t evt);

// Pop an event from the FIFO into *out. Returns true if an event was returned,
// false if the queue is empty or out pointer is NULL.
extern bool event_fifo_pop(event_t * const out);

extern bool event_fifo_is_empty(void);
extern bool event_fifo_is_full(void);

#endif /* EVENT_H_ */
