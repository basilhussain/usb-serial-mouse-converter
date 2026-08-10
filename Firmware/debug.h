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

#ifndef DEBUG_H_
#define DEBUG_H_

#include <stddef.h>

#ifndef DEBUG_VERBOSITY
#define DEBUG_VERBOSITY DEBUG_VERBOSITY_INFO
#endif

typedef enum {
	DEBUG_VERBOSITY_OFF    = 0,
	DEBUG_VERBOSITY_ALWAYS = 1,
	DEBUG_VERBOSITY_ERROR  = 2,
	DEBUG_VERBOSITY_WARN   = 3,
	DEBUG_VERBOSITY_INFO   = 4,
	DEBUG_VERBOSITY_TRACE  = 5
} debug_verbosity_enum_t;

// Macros to print a printf-style formatted message of the relevant verbosity
// level, prefixed with the name of the calling function and terminated with a
// line feed character. Also for printing the bytes of a given data buffer in
// hexadecimal format.

#if DEBUG_VERBOSITY >= DEBUG_VERBOSITY_ALWAYS
#define debug_always(fmt, ...) debug_print(DEBUG_VERBOSITY_ALWAYS, __func__, fmt, ##__VA_ARGS__)
#define debug_hex_always(buf, len) debug_print_hex(DEBUG_VERBOSITY_ALWAYS, __func__, buf, len)
#else
#define debug_always(fmt, ...)
#define debug_hex_always(buf, len)
#endif

#if DEBUG_VERBOSITY >= DEBUG_VERBOSITY_ERROR
#define debug_error(fmt, ...) debug_print(DEBUG_VERBOSITY_ERROR, __func__, fmt, ##__VA_ARGS__)
#define debug_hex_error(buf, len) debug_print_hex(DEBUG_VERBOSITY_ERROR, __func__, buf, len)
#else
#define debug_error(fmt, ...)
#define debug_hex_error(buf, len)
#endif

#if DEBUG_VERBOSITY >= DEBUG_VERBOSITY_WARN
#define debug_warn(fmt, ...) debug_print(DEBUG_VERBOSITY_WARN, __func__, fmt, ##__VA_ARGS__)
#define debug_hex_warn(buf, len) debug_print_hex(DEBUG_VERBOSITY_WARN, __func__, buf, len)
#else
#define debug_warn(fmt, ...)
#define debug_hex_warn(buf, len)
#endif

#if DEBUG_VERBOSITY >= DEBUG_VERBOSITY_INFO
#define debug_info(fmt, ...) debug_print(DEBUG_VERBOSITY_INFO, __func__, fmt, ##__VA_ARGS__)
#define debug_hex_info(buf, len) debug_print_hex(DEBUG_VERBOSITY_INFO, __func__, buf, len)
#else
#define debug_info(fmt, ...)
#define debug_hex_info(buf, len)
#endif

#if DEBUG_VERBOSITY >= DEBUG_VERBOSITY_TRACE
#define debug_trace(fmt, ...) debug_print(DEBUG_VERBOSITY_TRACE, __func__, fmt, ##__VA_ARGS__)
#define debug_hex_trace(buf, len) debug_print_hex(DEBUG_VERBOSITY_TRACE, __func__, buf, len)
#else
#define debug_trace(fmt, ...)
#define debug_hex_trace(buf, len)
#endif

/******************************************************************************/

extern void debug_print(const debug_verbosity_enum_t verbosity, const char *func, const char *format, ...) __attribute__((nonnull(2), null_terminated_string_arg(2), format(__printf__, 3, 0)));
extern void debug_print_hex(const debug_verbosity_enum_t verbosity, const char *func, const void * const buf, const size_t buf_len) __attribute__((nonnull(2,3), null_terminated_string_arg(2)));

#endif // DEBUG_H_
