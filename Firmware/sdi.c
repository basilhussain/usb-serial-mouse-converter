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
#include <errno.h>
#include "ch32x035.h"

// See WCH EVT reference implementation:
// https://github.com/openwch/ch32v003/blob/main/EVT/EXAM/SDI_Printf/SDI_Printf/Debug/debug.c

// Another implementation - possibly not using the same protocol? Only for use
// with minichlink?
// https://github.com/cnlohr/ch32v003fun/blob/master/ch32v003fun/ch32v003fun.c#L920

// How large the FIFO buffer should be. Note that actual capacity will be N-1
// due to there being one slot wasted between head and tail.
#ifndef SDI_FIFO_SIZE
#define SDI_FIFO_SIZE 512
#endif

#if SDI_FIFO_SIZE & (SDI_FIFO_SIZE - 1)
#error "SDI_FIFO_SIZE value must be a power of 2 (e.g. 16, 32, etc.)"
#endif

// How many iterations maximum to wait for SDI interface to be free, and how
// many 'packets' maximum we will attempt to send at once through SDI.
#define PROCESS_WAIT_MAX 500
#define PROCESS_SEND_MAX 50

#if PROCESS_WAIT_MAX < 1
#error "PROCESS_WAIT_MAX must be at least 1"
#endif

#if PROCESS_SEND_MAX < 1
#error "PROCESS_SEND_MAX must be at least 1"
#endif

/******************************************************************************/

typedef struct {
	size_t head;
	size_t tail;
	uint8_t buffer[SDI_FIFO_SIZE];
} sdi_fifo_t;

typedef union __attribute__((packed)) {
	uint32_t reg_vals[2];
	struct {
		uint8_t length;
		uint8_t payload[7];
	};
} sdi_data_t;

static volatile sdi_fifo_t sdi_fifo = {
	.head = 0,
	.tail = 0
};

/******************************************************************************/

static inline bool sdi_fifo_is_empty(void) {
	// When head and tail are equal, it means the buffer is empty.
	return (sdi_fifo.head == sdi_fifo.tail);
}

static inline bool sdi_fifo_is_full(void) {
	// If incrementing the head index (as if to push a new value) means it will
	// catch up with the tail, then that means the buffer is full.
	return (((sdi_fifo.head + 1) % SDI_FIFO_SIZE) == sdi_fifo.tail);
}

static inline void sdi_fifo_push(const uint8_t c) {
	// Increment the head index and push the given new value to that position.
	sdi_fifo.head = (sdi_fifo.head + 1) % SDI_FIFO_SIZE;
	sdi_fifo.buffer[sdi_fifo.head] = c;
}

static inline uint8_t sdi_fifo_pop(void) {
	// Increment the tail index and retrieve the value at that position.
	sdi_fifo.tail = (sdi_fifo.tail + 1) % SDI_FIFO_SIZE;
	return sdi_fifo.buffer[sdi_fifo.tail];
}

void sdi_init(const uint32_t sysclk_freq_hz) {
	DBGDAT->DATA0 = 0;

	// TODO: proper wait for 1 ms
	for(uint32_t i = 0; i < 5000; i++) __asm volatile("nop");
}

int sdi_putchar(int c) {
	// When given character is LF, send a CR preceding it.
	if(c == '\n') sdi_putchar('\r');
	
	// Take the given character and, if the FIFO buffer is not currently full,
	// append it.
	if(!sdi_fifo_is_full()) {
		sdi_fifo_push(c);
		return c;
	} else {
		return EOF;
	}
}

void sdi_process_queue(void) {
	size_t wait_count = PROCESS_WAIT_MAX;
	size_t send_count = PROCESS_SEND_MAX;
	sdi_data_t data;

	while(!sdi_fifo_is_empty() && send_count-- > 0) {
		// Wait for interface to be free (signalled by DATA0 being zero). Give
		// up waiting after a certain number of iterations. In case SDI Printf
		// isn't enabled in WCH-LinkUtility, we don't want to hang here
		// indefinitely.
		// TODO: any way to speed this up? Seems like minimum wait time is about 75 usec.
		wait_count = PROCESS_WAIT_MAX;
		while(DBGDAT->DATA0 != 0) {
			if(--wait_count == 0) break;
		}

		// Only bother to attempt sending anything if we didn't give up waiting
		// for the debug interface to acknowledge it's free.
		if(wait_count > 0) {
			// Pull bytes off the FIFO until either the FIFO is empty or we have
			// filled the payload buffer that will be written to the SDI DATAn
			// registers.
			data.length = 0;
			while(!sdi_fifo_is_empty() && data.length < (sizeof(data.payload) / sizeof(data.payload[0]))) {
				data.payload[data.length++] = sdi_fifo_pop();
			}

			// If we have some payload to send, send it now by writing the
			// corresponding word values to the DATAn registers. We must write
			// them in reverse order.
			if(data.length > 0) {
				DBGDAT->DATA1 = data.reg_vals[1];
				DBGDAT->DATA0 = data.reg_vals[0];
			} else {
				break;
			}
		} else {
			break;
		}
	}

	// Return a boolean value indicating whether there is still more data
	// remaining in the FIFO that is yet to be sent.
	// return !sdi_fifo_is_empty();
}
