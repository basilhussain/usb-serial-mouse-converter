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

#ifndef HID_H_
#define HID_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HID_INPUT_USAGES_MAX 8
#define HID_COLLECTION_STORAGE_MAX 8
#define HID_INPUT_STORAGE_MAX 16

typedef enum {
	HID_COLLECTION_TYPE_PHYSICAL       = 0x00,
	HID_COLLECTION_TYPE_APPLICATION    = 0x01,
	HID_COLLECTION_TYPE_LOGICAL        = 0x02,
	HID_COLLECTION_TYPE_REPORT         = 0x03,
	HID_COLLECTION_TYPE_NAMED_ARRAY    = 0x04,
	HID_COLLECTION_TYPE_USAGE_SWITCH   = 0x05,
	HID_COLLECTION_TYPE_USAGE_MODIFIER = 0x06
} hid_collection_type_enum_t;

// Well-known usage pages
typedef enum {
	HID_USAGE_PAGE_UNDEFINED       = 0x00,
	HID_USAGE_PAGE_GENERIC_DESKTOP = 0x01,
	HID_USAGE_PAGE_BUTTON          = 0x09,
	HID_USAGE_PAGE_CONSUMER        = 0x0C
} hid_usage_page_enum_t;

// Well-known usages in the Generic Desktop page
typedef enum {
	HID_USAGE_UNDEFINED       = 0x00,
	HID_USAGE_POINTER         = 0x01,
	HID_USAGE_MOUSE           = 0x02,
	HID_USAGE_JOYSTICK        = 0x04,
	HID_USAGE_GAMEPAD         = 0x05,
	HID_USAGE_KEYBOARD        = 0x06,
	HID_USAGE_KEYPAD          = 0x07,
	HID_USAGE_MULTI_AXIS_CTRL = 0x08,
	HID_USAGE_X               = 0x30,
	HID_USAGE_Y               = 0x31,
	HID_USAGE_Z               = 0x32,
	HID_USAGE_ROT_X           = 0x33,
	HID_USAGE_ROT_Y           = 0x34,
	HID_USAGE_ROT_Z           = 0x35,
	HID_USAGE_SLIDER          = 0x36,
	HID_USAGE_DIAL            = 0x37,
	HID_USAGE_WHEEL           = 0x38,
	HID_USAGE_HAT_SWITCH      = 0x39
} hid_usage_generic_desktop_enum_t;

// Well-known usages in the Button page
typedef enum {
	HID_USAGE_BTN_NONE = 0x00,
	HID_USAGE_BTN_1    = 0x01,
	HID_USAGE_BTN_2    = 0x02,
	HID_USAGE_BTN_3    = 0x03,
	HID_USAGE_BTN_4    = 0x04,
	HID_USAGE_BTN_5    = 0x05,
	HID_USAGE_BTN_6    = 0x06,
	HID_USAGE_BTN_7    = 0x07,
	HID_USAGE_BTN_8    = 0x08,
	HID_USAGE_BTN_9    = 0x09,
	HID_USAGE_BTN_10   = 0x10
} hid_usage_button_enum_t;

typedef struct {
	bool is_data : 1;
	bool is_constant: 1;
	bool is_array : 1;
	bool is_variable : 1;
	bool is_absolute : 1;
	bool is_relative : 1;
	bool is_non_wrap : 1;
	bool is_wrap : 1;
	bool is_linear : 1;
	bool is_non_linear : 1;
	bool is_pref_state : 1;
	bool is_no_pref_state : 1;
	bool is_no_null_pos : 1;
	bool is_null_pos : 1;
	bool is_bits : 1;
	bool is_bytes : 1;
} hid_input_flags_t;

typedef struct hid_input {
	// NOTE: Members here are ordered to minimise size of struct.
	uint16_t usages[HID_INPUT_USAGES_MAX];
	size_t usages_count;
	size_t bit_offset;
	// TODO: add prev_sibling?
	struct hid_input *next_sibling;
	int32_t logical_min;
	int32_t logical_max;
	hid_input_flags_t flags;
	uint16_t usage_page;
	uint16_t usage_min;
	uint16_t usage_max;
	uint8_t report_size;
	uint8_t report_count;
	uint8_t report_id;
	bool have_usage_min_max;
	bool have_report_id;
} hid_input_t;

typedef struct hid_collection {
	// NOTE: Members here are ordered to minimise size of struct.
	struct hid_collection *parent;
	// TODO: add prev_sibling?
	struct hid_collection *next_sibling;
	struct hid_collection *first_child;
	size_t child_count;
	hid_input_t *first_input;
	size_t input_count;
	uint16_t usage_page;
	uint16_t usage;
	uint8_t type;
	bool is_root; // Whether is dummy root collection
} hid_collection_t;

typedef struct {
	// NOTE: Members here are ordered to minimise size of struct.
	hid_collection_t collection_storage[HID_COLLECTION_STORAGE_MAX];
	hid_input_t input_storage[HID_INPUT_STORAGE_MAX];
	hid_collection_t *collection_root;
	size_t collection_storage_count;
	size_t input_storage_count;
} hid_report_composition_t;

extern bool hid_parse_report_descriptor(const uint8_t *descr, const size_t descr_len, hid_report_composition_t *composition);
extern void hid_dump_report_composition(const hid_report_composition_t *composition);
extern const hid_collection_t* hid_find_child_collection(const hid_collection_t *parent, const bool recursive, const uint8_t max_depth, const uint8_t type, const uint16_t usage_page, const uint16_t usage);
extern bool hid_input_has_usage(const hid_input_t *input, const uint16_t usage);
extern size_t hid_input_usage_bit_offset(const hid_input_t *input, const uint16_t usage);
extern uint32_t hid_read_report_value_unsigned(const uint8_t *report, const size_t report_len, const uint32_t bit_offset, const uint8_t bit_size);
extern int32_t hid_read_report_value_signed(const uint8_t *report, const size_t report_len, const uint32_t bit_offset, const uint8_t bit_size);

#endif // HID_H_
