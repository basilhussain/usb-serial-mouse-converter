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

#ifndef TSTAMP_H_
#define TSTAMP_H_

#include <stdint.h>
#include <stdbool.h>

// At 1 kHz tick rate, should give a ticks timespan of 49 days before roll-over.
// Every time ticks rolls over, epochs is incremented. With 32-bit epochs value,
// we can cover a period of 576 million years. :)
typedef struct {
	uint32_t epochs;
	uint32_t ticks;
} timestamp_t;

typedef enum {
	TIMESTAMP_CMP_EQ,
	TIMESTAMP_CMP_LT,
	TIMESTAMP_CMP_LTEQ,
	TIMESTAMP_CMP_GT,
	TIMESTAMP_CMP_GTEQ
} timestamp_cmp_enum_t;

typedef void (*timestamp_interval_callback_func_t)(void);
typedef void* timestamp_interval_call_handle_t;

#define TIMESTAMP_ZERO ((timestamp_t){ .epochs = 0, .ticks = 0 })
#define TIMESTAMP_MAX ((timestamp_t){ .epochs = UINT32_MAX, .ticks = UINT32_MAX })

extern void timestamp_init(const uint32_t hclk_freq_hz);
extern timestamp_t* timestamp_now(timestamp_t* out) __attribute__((nonnull(1), returns_nonnull));
extern timestamp_t* timestamp_increment(timestamp_t* t, const uint32_t incr) __attribute__((nonnull(1), returns_nonnull));
extern bool timestamp_compare(const timestamp_t* a, const timestamp_t* b, const timestamp_cmp_enum_t cmp) __attribute__((nonnull(1,2)));
extern bool timestamp_compare_to_now(const timestamp_t* t, const timestamp_cmp_enum_t cmp) __attribute__((nonnull(1)));
extern void timestamp_delay_ms(const uint32_t ms);
extern timestamp_interval_call_handle_t timestamp_interval_call_add(const timestamp_interval_callback_func_t callback, const uint32_t interval_ms, const bool enabled) __attribute__((nonnull(1)));
extern void timestamp_interval_call_remove(timestamp_interval_call_handle_t handle) __attribute__((nonnull(1)));
extern bool timestamp_interval_call_set_enabled(timestamp_interval_call_handle_t handle, const bool enabled) __attribute__((nonnull(1)));
extern bool timestamp_interval_call_get_enabled(timestamp_interval_call_handle_t handle) __attribute__((nonnull(1)));
extern uint32_t timestamp_interval_call_set_interval(timestamp_interval_call_handle_t handle, const uint32_t interval_ms) __attribute__((nonnull(1)));
extern uint32_t timestamp_interval_call_get_interval(timestamp_interval_call_handle_t handle) __attribute__((nonnull(1)));

#endif // TSTAMP_H_
