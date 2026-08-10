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
#include <stdarg.h>
#include "libs/nanoprintf/printf.h"
#include "debug.h"

#define ANSI_FG_RED "\x1B[31m"
#define ANSI_FG_GREEN "\x1B[32m"
#define ANSI_FG_YELLOW "\x1B[33m"
#define ANSI_FG_BLUE "\x1B[34m"
#define ANSI_FG_MAGENTA "\x1B[35m"
#define ANSI_FG_CYAN "\x1B[36m"
#define ANSI_FG_WHITE "\x1B[37m"
#define ANSI_RESET "\x1B[0m"

#define HEX_BYTES_PER_LINE 16

/******************************************************************************/

static void debug_print_func_name(const debug_verbosity_enum_t verbosity, const char *func) {
	const char *colour = ANSI_RESET;
	
	// Determine colour for the given verbosity level.
	switch(verbosity) {
		case DEBUG_VERBOSITY_ALWAYS: colour = ANSI_FG_MAGENTA; break;
		case DEBUG_VERBOSITY_ERROR: colour = ANSI_FG_RED; break;
		case DEBUG_VERBOSITY_WARN: colour = ANSI_FG_YELLOW; break;
		case DEBUG_VERBOSITY_INFO: colour = ANSI_FG_CYAN; break;
		case DEBUG_VERBOSITY_TRACE: colour = ANSI_FG_GREEN; break;
		default: break;
	}

	// Output the given function name prefix in the determined colour.
	printf("%s%s%s: ", colour, func, ANSI_RESET);
}

void debug_print(const debug_verbosity_enum_t verbosity, const char *func, const char *format, ...) {
	va_list args;

	if(verbosity <= DEBUG_VERBOSITY) {
		debug_print_func_name(verbosity, func);

		// Format and output rest of the variable arguments.
		va_start(args, format);
		vprintf(format, args);
		va_end(args);

		// Finish with a line-break.
		printf("\n");
	}
}

void debug_print_hex(const debug_verbosity_enum_t verbosity, const char *func, const void * const buf, const size_t buf_len) {
	const uint8_t * const buf_u8 = (uint8_t *)(buf);
	
	if(verbosity <= DEBUG_VERBOSITY) {
		debug_print_func_name(verbosity, func);
		
		if(buf_len > 0) {
			for(size_t i = 0; i < buf_len; i++) {
				printf("%02X ", buf_u8[i]);
				
				// After the requisite number of bytes, begin a new line and
				// re-print the function name.
				if((i + 1) % HEX_BYTES_PER_LINE == 0) {
					printf("\n");
					debug_print_func_name(verbosity, func);
				}
			}
			
			// If we had a remainder non-full line, then ensure there is also a
			// line break after that to finish off.
			if(buf_len % HEX_BYTES_PER_LINE != 0) printf("\n");
		} else {
			printf("[no data]\n");
		}
	}
}
