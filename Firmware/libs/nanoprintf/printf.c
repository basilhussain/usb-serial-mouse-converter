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

// Compile nanoprintf in this translation unit.
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_ALT_FORM_FLAG 0
#define NANOPRINTF_IMPLEMENTATION
#include "nanoprintf.h"

#if !defined(PRINTF_SDI) && !defined(PRINTF_UART)
#error "Output method for printf must be specified - define either PRINTF_UART or PRINTF_SDI"
#endif

#include <stdarg.h>
#include <stddef.h>
#include "printf.h"
#if defined(PRINTF_SDI)
#include "../../sdi.h"
#elif defined(PRINTF_UART)
#include "../../uart_debug.h"
#endif

static void putc_(int c, void *ctx) {
#if defined(PRINTF_SDI)
	sdi_putchar(c);
#elif defined(PRINTF_UART)
	uart_debug_putchar(c);
#endif
}

int printf_(const char* format, ...) {
	int retval;
	va_list args;

	va_start(args, format);
	retval = npf_vpprintf(putc_, NULL, format, args);
	va_end(args);

	return retval;
}

int sprintf_(char* buffer, const char* format, ...) {
	int retval;
	va_list args;

	// Can't provide the size of the destination buffer because we don't know
	// it, so pass size_t maximum value instead.
	va_start(args, format);
	retval = npf_vsnprintf(buffer, SIZE_MAX, format, args);
	va_end(args);

	return retval;
}

int snprintf_(char* buffer, size_t count, const char* format, ...) {
	int retval;
	va_list args;

	va_start(args, format);
	retval = npf_vsnprintf(buffer, count, format, args);
	va_end(args);

	return retval;
}

int vsnprintf_(char* buffer, size_t count, const char* format, va_list args) {
	return npf_vsnprintf(buffer, count, format, args);
}

int vprintf_(const char* format, va_list args) {
	return npf_vpprintf(putc_, NULL, format, args);
}
