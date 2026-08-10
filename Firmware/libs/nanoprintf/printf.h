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

#ifndef PRINTF_H_INCLUDED
#define PRINTF_H_INCLUDED

#include <stddef.h>
#include <stdarg.h>

// To avoid conflicts with the regular stdio functions, we override them by
// using macro defines.
#define printf printf_
#define sprintf sprintf_
#define snprintf snprintf_
#define vsnprintf vsnprintf_
#define vprintf vprintf_

extern int printf_(const char* format, ...) __attribute__((format(__printf__, 1, 2)));
extern int sprintf_(char* buffer, const char* format, ...) __attribute__((format(__printf__, 2, 3)));
extern int snprintf_(char* buffer, size_t count, const char* format, ...) __attribute__((format(__printf__, 3, 4)));
extern int vsnprintf_(char* buffer, size_t count, const char* format, va_list args) __attribute__((format(__printf__, 3, 0)));
extern int vprintf_(const char* format, va_list args) __attribute__((format(__printf__, 1, 0)));

#endif // PRINTF_H_INCLUDED
