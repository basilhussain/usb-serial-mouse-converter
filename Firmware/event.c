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
#include <stdbool.h>
#include "event.h"

// How large the event FIFO buffer should be. Note that actual capacity will
// be N-1 due to there being one slot wasted between head and tail.
#ifndef EVENT_FIFO_SIZE
#define EVENT_FIFO_SIZE 16
#endif

#if EVENT_FIFO_SIZE & (EVENT_FIFO_SIZE - 1)
#error "EVENT_FIFO_SIZE value must be a power of 2 (e.g. 16, 32, etc.)"
#endif

typedef struct {
	size_t head;
	size_t tail;
	event_t buffer[EVENT_FIFO_SIZE];
} event_fifo_t;

static event_fifo_t event_fifo = {
	.head = 0,
	.tail = 0
};

/******************************************************************************/

// When head and tail are equal, it means the buffer is empty.
#define fifo_empty() ((bool)(event_fifo.head == event_fifo.tail))

// If incrementing the head index (as if to push a new entry) means it will
// catch up with the tail, then that means the buffer is full.
#define fifo_full() ((bool)(((event_fifo.head + 1) % EVENT_FIFO_SIZE) == event_fifo.tail))

void event_fifo_init(void) {
	// Reset the FIFO indices and counters.
	event_fifo.head = 0;
	event_fifo.tail = 0;
}

bool event_fifo_is_empty(void) {
	return fifo_empty();
}

bool event_fifo_is_full(void) {
	return fifo_full();
}

bool event_fifo_push(const event_t evt) {
	if(fifo_full()) return false;

	// Increment the head index and push the given new event to that position.
	event_fifo.head = (event_fifo.head + 1) % EVENT_FIFO_SIZE;
	event_fifo.buffer[event_fifo.head] = evt;
	
	return true;
}

bool event_fifo_pop(event_t * const out) {
	if(fifo_empty() || out == NULL) return false;

	// Increment the tail index and retrieve the event at that position.
	event_fifo.tail = (event_fifo.tail + 1) % EVENT_FIFO_SIZE;
	*out = event_fifo.buffer[event_fifo.tail];
	
	return true;
}
