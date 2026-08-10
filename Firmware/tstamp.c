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
#include "ch32x035.h"
#include "interrupt.h"
#include "tstamp.h"

#define CALLBACK_TIMER_MAX_ENTRIES 8

typedef struct timestamp_interval_call_entry {
	struct timestamp_interval_call_entry *next;
	timestamp_interval_callback_func_t callback;
	uint32_t interval_ms;
	uint32_t interval_remaining_ms;
	bool enabled;
} timestamp_interval_call_entry_t;

typedef struct {
	timestamp_interval_call_entry_t *used_list_head; // Head of active callbacks
	timestamp_interval_call_entry_t *free_list_head; // Head of free pool
	timestamp_interval_call_entry_t entries[CALLBACK_TIMER_MAX_ENTRIES];
} timestamp_interval_call_list_t;

static volatile timestamp_t sys_timestamp = TIMESTAMP_ZERO;
static timestamp_interval_call_list_t interval_call_list;

/******************************************************************************/

static void timestamp_interval_call_list_init(void) {
	interval_call_list.used_list_head = NULL;
	interval_call_list.free_list_head = NULL;

	// Link all entries into the free list.
	for(int i = CALLBACK_TIMER_MAX_ENTRIES - 1; i >= 0; i--) {
		interval_call_list.entries[i].next = interval_call_list.free_list_head;
		interval_call_list.free_list_head = &interval_call_list.entries[i];
	}
}

static void timestamp_interval_call_process(void) {
	timestamp_interval_call_entry_t *entry = interval_call_list.used_list_head;

	while(entry != NULL) {
		if(--entry->interval_remaining_ms == 0) {
			if(entry->enabled) entry->callback();
			entry->interval_remaining_ms = entry->interval_ms;
		}
		entry = entry->next;
	}
}

void timestamp_init(const uint32_t hclk_freq_hz) {
	// Set comparison value appropriate for 1 kHz rate when SysTick running at
	// given HCLK undivided.
	SysTick->CMP = (hclk_freq_hz / 1000) - 1;

	// Zero counter and clear interrupt flag.
	SysTick->CNT = 0;
	SysTick->SR = 0;

	// Configure the SysTick counter for auto-reload, clock source of undivided
	// HCLK, enable its interrupt, up-counting, and start the counter.
	SysTick->CTLR = STK_STRE | STK_STCLK | STK_STIE | STK_STE;

	// Enable the SysTick interrupt in the PFIC.
	interrupt_enable(SysTicK_IRQn);

	timestamp_interval_call_list_init();
}

timestamp_t* timestamp_now(timestamp_t* out) {
	// Wrap with disabling of interrupt to avoid potential epochs and ticks
	// being out of sync in the output value.
	interrupt_atomic_block_individual(SysTicK_IRQn) {
		out->epochs = sys_timestamp.epochs;
		out->ticks = sys_timestamp.ticks;
	}

	return out;
}

timestamp_t* timestamp_increment(timestamp_t* t, const uint32_t incr) {
	// Make a note of original ticks value, before incrementation.
	const uint32_t ticks_orig = t->ticks;

	// If adding the increment caused the ticks to roll-over, then also
	// increment the epoch count.
	t->ticks += incr;
	if(t->ticks < ticks_orig) t->epochs++;

	return t;
}

bool timestamp_compare(const timestamp_t* a, const timestamp_t* b, const timestamp_cmp_enum_t cmp) {
	bool result = false;

	switch(cmp) {
		case TIMESTAMP_CMP_EQ: // a equals b
			result = (a->epochs == b->epochs && a->ticks == b->ticks);
			break;
		case TIMESTAMP_CMP_LT: // a less than b
			result = (a->epochs < b->epochs || (a->epochs == b->epochs && a->ticks < b->ticks));
			break;
		case TIMESTAMP_CMP_LTEQ: // a less than or equal to b
			result = (a->epochs < b->epochs || (a->epochs == b->epochs && a->ticks <= b->ticks));
			break;
		case TIMESTAMP_CMP_GT: // a greater than b
			result = (a->epochs > b->epochs || (a->epochs == b->epochs && a->ticks > b->ticks));
			break;
		case TIMESTAMP_CMP_GTEQ: // a greater than or equal to b
			result = (a->epochs > b->epochs || (a->epochs == b->epochs && a->ticks >= b->ticks));
			break;
	}

	return result;
}

bool timestamp_compare_to_now(const timestamp_t* t, const timestamp_cmp_enum_t cmp) {
	timestamp_t now;

	// In order to access the system timestamp atomically, we must make a copy
	// and compare against that.
	return timestamp_compare(t, timestamp_now(&now), cmp);
}

void timestamp_delay_ms(const uint32_t ms) {
	timestamp_t delay_expiry;

	// Create a timestamp the given number of milliseconds in the future.
	timestamp_increment(timestamp_now(&delay_expiry), ms);

	// Wait until that timestamp expires.
	while(timestamp_compare_to_now(&delay_expiry, TIMESTAMP_CMP_GT));
}

timestamp_interval_call_handle_t timestamp_interval_call_add(const timestamp_interval_callback_func_t callback, const uint32_t interval_ms, const bool enabled) {
	// Check if the list is full.
	if(interval_call_list.free_list_head == NULL) return NULL;
	if(interval_ms == 0) return NULL;

	timestamp_interval_call_entry_t *entry = interval_call_list.free_list_head;
	interval_call_list.free_list_head = entry->next;

	entry->callback = callback;
	entry->interval_ms = interval_ms;
	entry->interval_remaining_ms = interval_ms;
	entry->enabled = enabled;

	entry->next = interval_call_list.used_list_head;
	interval_call_list.used_list_head = entry;

	return (timestamp_interval_call_handle_t)entry;
}

void timestamp_interval_call_remove(timestamp_interval_call_handle_t handle) {
	timestamp_interval_call_entry_t *entry = (timestamp_interval_call_entry_t *)handle;
	timestamp_interval_call_entry_t **p = &interval_call_list.used_list_head;

	while(*p != NULL) {
		if(*p == entry) {
			*p = entry->next;
			// Return entry to free list
			entry->next = interval_call_list.free_list_head;
			interval_call_list.free_list_head = entry;
			break;
		}
		p = &((*p)->next);
	}
}

bool timestamp_interval_call_set_enabled(timestamp_interval_call_handle_t handle, const bool enabled) {
	timestamp_interval_call_entry_t *entry = (timestamp_interval_call_entry_t *)handle;
	const bool prev = entry->enabled;

	entry->enabled = enabled;

	return prev;
}

bool timestamp_interval_call_get_enabled(timestamp_interval_call_handle_t handle) {
	timestamp_interval_call_entry_t *entry = (timestamp_interval_call_entry_t *)handle;

	return entry->enabled;
}

uint32_t timestamp_interval_call_set_interval(timestamp_interval_call_handle_t handle, const uint32_t interval_ms) {
	timestamp_interval_call_entry_t *entry = (timestamp_interval_call_entry_t *)handle;
	const uint32_t prev = entry->interval_ms;

	if(interval_ms > 0) {
		entry->interval_ms = interval_ms;
		// Uncomment if we want the interval change to apply immediately.
		// entry->interval_remaining_ms = interval_ms;
	}

	return prev;
}

uint32_t timestamp_interval_call_get_interval(timestamp_interval_call_handle_t handle) {
	timestamp_interval_call_entry_t *entry = (timestamp_interval_call_entry_t *)handle;

	return entry->interval_ms;
}

ISR(SysTick_Handler) {
	// Make a note of original ticks value, before incrementation.
	const uint32_t ticks_orig = sys_timestamp.ticks;

	// If adding the increment caused the ticks to roll-over, then also
	// increment the epoch count.
	if(++sys_timestamp.ticks < ticks_orig) sys_timestamp.epochs++;

	// Process any periodic interval callbacks.
	timestamp_interval_call_process();

	// Clear the interrupt flag (CNTIF is the only flag in the register).
	SysTick->SR = 0;
}
