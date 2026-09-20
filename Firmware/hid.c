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
#include <string.h>
#include "libs/nanoprintf/printf.h"
#include "hid.h"
#include "debug.h"

#define LOCAL_USAGES_MAX 8
#define COLLECTION_PARENT_STACK_MAX 8
#define INPUT_PREV_SIBLING_STACK_MAX 8
#define REPORT_IDS_MAX 32
#define REPORT_ID_INPUT_BIT_OFFSET_INIT 8

#define ITEM_PREFIX_LONG      0xFE
#define ITEM_PREFIX_MASK_SIZE 0x03
#define ITEM_PREFIX_MASK_TYPE 0x0C
#define ITEM_PREFIX_MASK_TAG  0xF0

// Item types
#define ITEM_TYPE_MAIN     0
#define ITEM_TYPE_GLOBAL   1
#define ITEM_TYPE_LOCAL    2
#define ITEM_TYPE_RESERVED 3

// Main tags
#define TAG_MAIN_INPUT          0x8
#define TAG_MAIN_OUTPUT         0x9
#define TAG_MAIN_COLLECTION     0xA
#define TAG_MAIN_FEATURE        0xB
#define TAG_MAIN_END_COLLECTION 0xC

// Global tags
#define TAG_GLOBAL_USAGE_PAGE    0x0
#define TAG_GLOBAL_LOGICAL_MIN   0x1
#define TAG_GLOBAL_LOGICAL_MAX   0x2
#define TAG_GLOBAL_PHYSICAL_MIN  0x3
#define TAG_GLOBAL_PHYSICAL_MAX  0x4
#define TAG_GLOBAL_UNIT_EXPONENT 0x5
#define TAG_GLOBAL_UNIT          0x6
#define TAG_GLOBAL_REPORT_SIZE   0x7
#define TAG_GLOBAL_REPORT_ID     0x8
#define TAG_GLOBAL_REPORT_COUNT  0x9
#define TAG_GLOBAL_PUSH          0xA
#define TAG_GLOBAL_POP           0xB

// Local tags
#define TAG_LOCAL_USAGE     0x0
#define TAG_LOCAL_USAGE_MIN 0x1
#define TAG_LOCAL_USAGE_MAX 0x2

// Input flags
#define INPUT_DATA_CONST_MASK  ((uint32_t)0x0001)
#define INPUT_DATA             ((uint32_t)0x0000)
#define INPUT_CONST            ((uint32_t)0x0001)
#define INPUT_TYPE_MASK        ((uint32_t)0x0002)
#define INPUT_ARRAY            ((uint32_t)0x0000)
#define INPUT_VARIABLE         ((uint32_t)0x0002)
#define INPUT_COORDS_MASK      ((uint32_t)0x0004)
#define INPUT_ABSOLUTE         ((uint32_t)0x0000)
#define INPUT_RELATIVE         ((uint32_t)0x0004)
#define INPUT_WRAP_MASK        ((uint32_t)0x0008)
#define INPUT_NO_WRAP          ((uint32_t)0x0000)
#define INPUT_WRAP             ((uint32_t)0x0008)
#define INPUT_LINEAR_MASK      ((uint32_t)0x0010)
#define INPUT_LINEAR           ((uint32_t)0x0000)
#define INPUT_NON_LINEAR       ((uint32_t)0x0010)
#define INPUT_PREF_STATE_MASK  ((uint32_t)0x0020)
#define INPUT_PREF_STATE       ((uint32_t)0x0000)
#define INPUT_NO_PREF_STATE    ((uint32_t)0x0020)
#define INPUT_NULL_POS_MASK    ((uint32_t)0x0040)
#define INPUT_NO_NULL_POS      ((uint32_t)0x0000)
#define INPUT_NULL_POS         ((uint32_t)0x0040)
#define INPUT_RESERVED_MASK    ((uint32_t)0x0080)
#define INPUT_BITS_BYTES_MASK  ((uint32_t)0x0100)
#define INPUT_BITS             ((uint32_t)0x0000)
#define INPUT_BYTES            ((uint32_t)0x0100)

/******************************************************************************/

typedef enum {
	ITEM_FORMAT_SHORT,
	ITEM_FORMAT_LONG
} hid_item_format_enum_t;

typedef struct {
	// NOTE: Members here are ordered to minimise size of struct.
	const uint8_t *data;
	size_t data_size;
	hid_item_format_enum_t format;
	uint8_t type;
	uint8_t tag;
} hid_item_t;

typedef struct {
	// NOTE: Members here are ordered to minimise size of struct.
	// We don't support the full set of globals; only the bare minimum.
	int32_t logical_min;
	int32_t logical_max;
	uint16_t usage_page;
	uint8_t report_size;
	uint8_t report_count;
	uint8_t report_id;
	bool have_report_id;
} hid_globals_t;

typedef struct {
	// NOTE: Members here are ordered to minimise size of struct.
	// We don't support the full set of locals; only the bare minimum.
	size_t usages_count;
	uint16_t usages[LOCAL_USAGES_MAX];
	uint16_t usage_min;
	uint16_t usage_max;
	bool have_usage_min_max;
} hid_locals_t;

typedef struct {
	size_t inputs_bit_offset;
	uint8_t id;
} hid_report_id_t;

// Context structure to hold the state of the parser.
// TODO: maybe try to optimise the size of this struct?
typedef struct {
	// NOTE: Members here are ordered to minimise size of struct.
	hid_report_id_t report_ids[REPORT_IDS_MAX];
	hid_globals_t globals;
	hid_globals_t saved_globals;
	hid_locals_t locals;
	hid_report_composition_t *composition;
	hid_collection_t *collection_parent_stack[COLLECTION_PARENT_STACK_MAX];
	hid_collection_t *collection_prev_sibling;
	hid_collection_t *collection_current;
	hid_input_t *input_prev_sibling_stack[INPUT_PREV_SIBLING_STACK_MAX];
	size_t collection_depth;
	size_t report_ids_count;
	bool have_saved_globals;
} hid_parse_context_t;

/******************************************************************************/

// Sign-extend to 32 bits a value of the given bit width (<= 32).
static int32_t hid_sign_extend(const uint32_t val, const size_t val_bits) {
	// When given bit width is outside the supported range, simply return the
	// value unchanged.
	if(val_bits == 0 || val_bits >= 32) return (int32_t)val;
	
	// Is the given value negative? That is, according to the given bit width,
	// does it have its most-significant bit set? If so, formulate the sign
	// extension bits and prepend them to the value.
	const uint32_t val_sign_mask = (uint32_t)1 << (val_bits - 1);
	if(val & val_sign_mask) {
		return (int32_t)((UINT32_MAX << val_bits) | val);
	} else {
		return (int32_t)val;
	}
}

// Read value from descriptor item data bytes (size_bytes is 0,1,2,4).
static uint32_t hid_read_unsigned_le(const uint8_t *data, size_t size_bytes) {
	uint32_t val = 0;
	if(size_bytes > 4) size_bytes = 4;
	for(size_t i = 0; i < size_bytes; i++) {
		val |= ((uint32_t)data[i]) << (8 * i);
	}
	return val;
}

// Read signed value (two's complement) from item data.
static int32_t hid_read_signed_le(const uint8_t *data, size_t size_bytes) {
	if(size_bytes == 0) return 0;
	return hid_sign_extend(hid_read_unsigned_le(data, size_bytes), size_bytes * 8);
}

static size_t hid_read_item(const uint8_t *descr, const size_t descr_len, hid_item_t *item) {
	size_t size = 0;
	
	if(descr != NULL && item != NULL) {
		// Check if the item is a long item. Long items have the size and tag
		// stored as separate, additional bytes that follow the prefix byte.
		if(descr_len >= 3 && descr[0] == ITEM_PREFIX_LONG) {
			item->format = ITEM_FORMAT_LONG;
			item->data_size = descr[1];
			item->type = ITEM_TYPE_RESERVED;
			item->tag = descr[2];
			item->data = (item->data_size > 0 ? &descr[3] : NULL);
			size = 3 + item->data_size;
		} else if(descr_len >= 1 && descr[0] != ITEM_PREFIX_LONG) {
			const uint8_t size_code = descr[0] & ITEM_PREFIX_MASK_SIZE;
			item->format = ITEM_FORMAT_SHORT;
			item->data_size = (size_code == 3 ? 4 : size_code);
			item->type = (descr[0] & ITEM_PREFIX_MASK_TYPE) >> __builtin_ctz(ITEM_PREFIX_MASK_TYPE);
			item->tag = (descr[0] & ITEM_PREFIX_MASK_TAG) >> __builtin_ctz(ITEM_PREFIX_MASK_TAG);
			item->data = (item->data_size > 0 ? &descr[1] : NULL);
			size = 1 + item->data_size;
		}
	}
	
	// Return the total size of this item, including any data.
	return size;
}

static hid_input_flags_t hid_read_input_flags(const uint32_t value) {
	hid_input_flags_t flags;

	flags.is_data = ((value & INPUT_DATA_CONST_MASK) == INPUT_DATA);
	flags.is_constant = ((value & INPUT_DATA_CONST_MASK) == INPUT_CONST);
	flags.is_array = ((value & INPUT_TYPE_MASK) == INPUT_ARRAY);
	flags.is_variable = ((value & INPUT_TYPE_MASK) == INPUT_VARIABLE);
	flags.is_absolute = ((value & INPUT_COORDS_MASK) == INPUT_ABSOLUTE);
	flags.is_relative = ((value & INPUT_COORDS_MASK) == INPUT_RELATIVE);
	flags.is_non_wrap = ((value & INPUT_WRAP_MASK) == INPUT_NO_WRAP);
	flags.is_wrap = ((value & INPUT_WRAP_MASK) == INPUT_WRAP);
	flags.is_linear = ((value & INPUT_LINEAR_MASK) == INPUT_LINEAR);
	flags.is_non_linear = ((value & INPUT_LINEAR_MASK) == INPUT_NON_LINEAR);
	flags.is_pref_state = ((value & INPUT_PREF_STATE_MASK) == INPUT_PREF_STATE);
	flags.is_no_pref_state = ((value & INPUT_PREF_STATE_MASK) == INPUT_NO_PREF_STATE);
	flags.is_no_null_pos = ((value & INPUT_NULL_POS_MASK) == INPUT_NO_NULL_POS);
	flags.is_null_pos = ((value & INPUT_NULL_POS_MASK) == INPUT_NULL_POS);
	flags.is_bits = ((value & INPUT_BITS_BYTES_MASK) == INPUT_BITS);
	flags.is_bytes = ((value & INPUT_BITS_BYTES_MASK) == INPUT_BYTES);
	
	return flags;
}

static void hid_get_indent_string(char *buf, const size_t buf_size, const size_t depth) {
	const size_t indent_size = 2;
	size_t indent_len;
	
	// Ensure we don't overflow the buffer and leave room for null terminator.
	if(depth > (buf_size / indent_size)) {
		indent_len = buf_size - 1;
	} else {
		indent_len = depth * indent_size;
	}

	/*
	size_t indent_len = depth * indent_size;

	// Ensure we don't overflow the buffer and leave room for null terminator.
	if(indent_len >= buf_size) {
		indent_len = buf_size - 1;
	}
	*/

	// Fill with spaces and null-terminate.
	memset(buf, ' ', indent_len);
	buf[indent_len] = '\0';
}

static void hid_get_comma_string(char *buf, const size_t buf_size, const uint16_t *values, const size_t values_count) {
	size_t buf_offset = 0;
	
	memset(buf, '\0', buf_size);
	
	if(values_count > 0) {
		buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset, "0x%04X", values[0]);
		for(size_t i = 1; i < values_count && buf_offset < buf_size; i++) {
			buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset, ", 0x%04X", values[i]);
		}
		
		buf[buf_size - 1] = '\0';
	}
}

#define TREE_MID "├"
#define TREE_END "└"
/*
#define TREE_MID "|-"
#define TREE_END "\\-"
*/

static void hid_dump_collection(const hid_collection_t *c, const size_t depth) {
	char indent[32], usages_ids[64];
	
	hid_get_indent_string(indent, (sizeof(indent) / sizeof(indent[0])), depth);
	
	const hid_collection_t *coll = c;
	size_t coll_idx = 0;
	
	while(coll != NULL) {
		debug_info("%s%zu.%zu. COLLECTION %p:", indent, depth, coll_idx, coll);
		debug_info("%s" TREE_MID " is_root = %u", indent, coll->is_root);
		debug_info("%s" TREE_MID " parent = %p", indent, coll->parent);
		debug_info("%s" TREE_MID " next_sibling = %p", indent, coll->next_sibling);
		debug_info("%s" TREE_MID " first_child = %p", indent, coll->first_child);
		debug_info("%s" TREE_MID " child_count = %zu", indent, coll->child_count);
		debug_info("%s" TREE_MID " type = 0x%02X", indent, coll->type);
		debug_info("%s" TREE_MID " usage_page = 0x%04X", indent, coll->usage_page);
		debug_info("%s" TREE_MID " usage = 0x%04X", indent, coll->usage);
		debug_info("%s" TREE_MID " first_input = %p", indent, coll->first_input);
		debug_info("%s" TREE_END " input_count = %zu", indent, coll->input_count);
		
		const hid_input_t *input = coll->first_input;
		size_t input_idx = 0;
		
		while(input != NULL) {
			hid_get_comma_string(usages_ids, sizeof(usages_ids), input->usages, input->usages_count);
			
			debug_info("%s  %zu.%zu.%zu. INPUT %p:", indent, depth, coll_idx, input_idx, input);
			debug_info("%s  " TREE_MID " bit_offset = %zu", indent, input->bit_offset);
			debug_info("%s  " TREE_MID " collection = %p", indent, input->collection);
			debug_info("%s  " TREE_MID " next_sibling = %p", indent, input->next_sibling);
			debug_info("%s  " TREE_MID " usage_page = 0x%04X", indent, input->usage_page);
			debug_info("%s  " TREE_MID " usages_count = %zu", indent, input->usages_count);
			debug_info("%s  " TREE_MID " usages = %s", indent, usages_ids);
			debug_info("%s  " TREE_MID " usage_min = 0x%04X", indent, input->usage_min);
			debug_info("%s  " TREE_MID " usage_max = 0x%04X", indent, input->usage_max);
			debug_info("%s  " TREE_MID " have_usage_min_max = %u", indent, input->have_usage_min_max);
			debug_info("%s  " TREE_MID " logical_min = %d", indent, input->logical_min);
			debug_info("%s  " TREE_MID " logical_max = %d", indent, input->logical_max);
			debug_info("%s  " TREE_MID " report_size = %u", indent, input->report_size);
			debug_info("%s  " TREE_MID " report_count = %u", indent, input->report_count);
			debug_info("%s  " TREE_MID " report_id = %u", indent, input->report_id);
			debug_info("%s  " TREE_MID " have_report_id = %u", indent, input->have_report_id);
			debug_info("%s  " TREE_END " flags:", indent);
			debug_info("%s      is_data = %u, is_constant = %u", indent, input->flags.is_data, input->flags.is_constant);
			debug_info("%s      is_array = %u, is_variable = %u", indent, input->flags.is_array, input->flags.is_variable);
			debug_info("%s      is_absolute = %u, is_relative = %u", indent, input->flags.is_absolute, input->flags.is_relative);
			debug_info("%s      is_non_wrap = %u, is_wrap = %u", indent, input->flags.is_non_wrap, input->flags.is_wrap);
			debug_info("%s      is_linear = %u, is_non_linear = %u", indent, input->flags.is_linear, input->flags.is_non_linear);
			debug_info("%s      is_pref_state = %u, is_no_pref_state = %u", indent, input->flags.is_pref_state, input->flags.is_no_pref_state);
			debug_info("%s      is_no_null_pos = %u, is_null_pos = %u", indent, input->flags.is_no_null_pos, input->flags.is_null_pos);
			debug_info("%s      is_bits = %u, is_bytes = %u", indent, input->flags.is_bits, input->flags.is_bytes);

			input = input->next_sibling;
			input_idx++;
		}
		
		if(coll->first_child != NULL) hid_dump_collection(coll->first_child, depth + 1);
		
		coll = coll->next_sibling;
		coll_idx++;
	}
}

#undef TREE_MID
#undef TREE_END

static bool hid_parse_main_input(hid_parse_context_t *ctx, const hid_item_t *item) {
	if(ctx->composition->input_storage_count >= HID_INPUT_STORAGE_MAX) {
		debug_error("not enough input storage");
		return false;
	}
	
	if(ctx->collection_depth >= INPUT_PREV_SIBLING_STACK_MAX) {
		debug_error("not enough input previous sibling stack capacity");
		return false;
	}
	
	if(ctx->locals.usages_count > 0 && ctx->globals.report_count > HID_INPUT_USAGES_MAX) {
		debug_warn("report count exceeds input usage ids storage (count = %zu, max = %u)", ctx->globals.report_count, HID_INPUT_USAGES_MAX);
	}
	
	// Create a new input and add to storage.
	hid_input_t *input = &ctx->composition->input_storage[ctx->composition->input_storage_count++];
	
	// Copy into it all the pertinent global, locals, and other data. Also read
	// the flags from item and set those too.
	input->collection = ctx->collection_current;
	input->flags = hid_read_input_flags(hid_read_unsigned_le(item->data, item->data_size));
	input->usage_page = ctx->globals.usage_page;
	input->usage_min = ctx->locals.usage_min;
	input->usage_max = ctx->locals.usage_max;
	input->have_usage_min_max = ctx->locals.have_usage_min_max;
	input->logical_min = ctx->globals.logical_min;
	input->logical_max = ctx->globals.logical_max;
	input->report_size = ctx->globals.report_size;
	input->report_count = ctx->globals.report_count;
	input->report_id = ctx->globals.report_id;
	input->have_report_id = ctx->globals.have_report_id;
	
	// Copy over the local usages (if any). When doing so, if we have a report
	// count greater than the number of usages, the last usage should be
	// repeated as many times as necessary to make up the count. For example, if
	// report count is 5, but there are only 3 usages, the 3rd usage is repeated
	// twice more.
	input->usages_count = 0;
	if(ctx->locals.usages_count > 0) {
		for(size_t i = 0; i < ctx->globals.report_count && i < HID_INPUT_USAGES_MAX; i++) {
			size_t idx = i;
			if(idx >= LOCAL_USAGES_MAX) idx = LOCAL_USAGES_MAX - 1;
			if(idx >= ctx->locals.usages_count) idx = ctx->locals.usages_count - 1;
			input->usages[i] = ctx->locals.usages[idx];
			input->usages_count++;
		}
	}
	
	// Grab the input bit offset for the report ID associated with this input.
	// When there's not been a report ID encountered, the global value will
	// be zero, so in that case this will find the default 'zero' entry.
	// Then increment that bit offset by the size in bits of the fields
	// represented by this input.
	for(size_t i = 0; i < ctx->report_ids_count && i < REPORT_IDS_MAX; i++) {
		if(ctx->report_ids[i].id == input->report_id) {
			input->bit_offset = ctx->report_ids[i].inputs_bit_offset;
			ctx->report_ids[i].inputs_bit_offset += (size_t)input->report_size * (size_t)input->report_count;
			break;
		}
	}
	
	// If this will be the first input of the current containing collection, set
	// a pointer on the collection to this input, and increment collection's
	// input count. Otherwise, set a pointer on the previous sibling to this
	// input at this collection depth.
	if(ctx->collection_current->input_count++ == 0) {
		ctx->collection_current->first_input = input;
	} else if(ctx->input_prev_sibling_stack[ctx->collection_depth] != NULL) {
		ctx->input_prev_sibling_stack[ctx->collection_depth]->next_sibling = input;
	}
	
	ctx->input_prev_sibling_stack[ctx->collection_depth] = input;
	
	return true;
}

static bool hid_parse_main_collection(hid_parse_context_t *ctx, const hid_item_t *item) {
	if(ctx->composition->collection_storage_count >= HID_COLLECTION_STORAGE_MAX) {
		debug_error("not enough collection storage");
		return false;
	}
	
	if(ctx->collection_depth >= (COLLECTION_PARENT_STACK_MAX - 1)) {
		debug_error("not enough parent stack capacity");
		return false;
	}
	
	const uint8_t collection_type = (uint8_t)hid_read_unsigned_le(item->data, item->data_size);
	
	debug_trace("collection_type = %u", collection_type);
	
	// Create new collection and add to storage, and set its parent to the one
	// at current depth.
	hid_collection_t *coll = &ctx->composition->collection_storage[ctx->composition->collection_storage_count++];
	hid_collection_t *parent = ctx->collection_parent_stack[ctx->collection_depth];
	
	coll->is_root = false;
	coll->parent = parent;
	coll->next_sibling = NULL;
	coll->first_child = NULL;
	coll->child_count = 0;
	coll->type = collection_type;
	coll->usage_page = ctx->globals.usage_page;
	coll->usage = (ctx->locals.usages_count > 0 ? ctx->locals.usages[ctx->locals.usages_count - 1] : 0); // last seen usage

	// If this will be the first child of that parent, set a pointer on the
	// parent to this collection, and increment parent's child count.
	// Otherwise, set a pointer on the previous sibling to this collection.
	if(parent->child_count++ == 0) {
		parent->first_child = coll;
	} else if(ctx->collection_prev_sibling != NULL) {
		ctx->collection_prev_sibling->next_sibling = coll;
	}

	// Make this collection the last seen sibling, as well as the parent for
	// another step down in depth.
	ctx->collection_current = coll;
	ctx->collection_prev_sibling = coll;
	ctx->collection_parent_stack[++ctx->collection_depth] = coll;
	
	return true;
}

static bool hid_parse_main_end_collection(hid_parse_context_t *ctx) {
	if(ctx->collection_depth == 0) {
		debug_error("extraneous end collection");
		return false;
	}
	
	ctx->collection_parent_stack[ctx->collection_depth--] = NULL;
	ctx->collection_prev_sibling = ctx->collection_current;
	ctx->collection_current = ctx->collection_parent_stack[ctx->collection_depth];
	
	return true;
}

static bool hid_parse_main_item(hid_parse_context_t *ctx, const hid_item_t *item) {
	bool result = true;
	
	switch(item->tag) {
		case TAG_MAIN_INPUT:
			debug_trace("main: input");
			result = hid_parse_main_input(ctx, item);
			break;
		case TAG_MAIN_COLLECTION:
			debug_trace("main: collection");
			result = hid_parse_main_collection(ctx, item);
			break;
		case TAG_MAIN_END_COLLECTION:
			debug_trace("main: end collection");
			result = hid_parse_main_end_collection(ctx);
			break;
		case TAG_MAIN_FEATURE:
			debug_warn("unhandled main feature tag");
			break;
		case TAG_MAIN_OUTPUT:
			debug_warn("unhandled main output tag");
			break;
		default:
			debug_warn("unhandled main tag 0x%02X", item->tag);
			break;
	}
	
	// Reset all locals after a main item.
	ctx->locals.usages_count = 0;
	ctx->locals.usage_min = 0;
	ctx->locals.usage_max = 0;
	ctx->locals.have_usage_min_max = false;
	
	return result;
}

static bool hid_parse_global_item(hid_parse_context_t *ctx, const hid_item_t *item) {
	bool new_report_id = true;
	
	switch(item->tag) {
		case TAG_GLOBAL_USAGE_PAGE:
			ctx->globals.usage_page = (uint16_t)hid_read_unsigned_le(item->data, item->data_size);
			debug_trace("global: usage page = 0x%04X", ctx->globals.usage_page);
			break;
		case TAG_GLOBAL_LOGICAL_MIN:
			ctx->globals.logical_min = hid_read_signed_le(item->data, item->data_size);
			debug_trace("global: logical min = %d", ctx->globals.logical_min);
			break;
		case TAG_GLOBAL_LOGICAL_MAX:
			ctx->globals.logical_max = hid_read_signed_le(item->data, item->data_size);
			debug_trace("global: logical max = %d", ctx->globals.logical_max);
			break;
		case TAG_GLOBAL_REPORT_SIZE:
			ctx->globals.report_size = (uint8_t)hid_read_unsigned_le(item->data, item->data_size);
			debug_trace("global: report size = %u", ctx->globals.report_size);
			break;
		case TAG_GLOBAL_REPORT_ID:
			ctx->globals.report_id = (uint8_t)hid_read_unsigned_le(item->data, item->data_size);
			ctx->globals.have_report_id = true;
			debug_trace("global: report id = %u", ctx->globals.report_id);
			// Look to see if we already encountered this report ID. Don't
			// bother to look at the default 'zero' report ID array entry.
			for(size_t i = 1; i < ctx->report_ids_count && i < REPORT_IDS_MAX; i++) {
				if(ctx->report_ids[i].id == ctx->globals.report_id) {
					new_report_id = false;
					debug_trace("existing report id; idx = %zu, inputs_bit_offset = %zu", i, ctx->report_ids[i].inputs_bit_offset);
					break;
				}
			}
			// If we didn't, add it to the list and initialise the inputs bit
			// offset value associated with it.
			if(new_report_id) {
				if(ctx->report_ids_count < REPORT_IDS_MAX) {
					hid_report_id_t *r = &ctx->report_ids[ctx->report_ids_count++];
					r->id = ctx->globals.report_id;
					r->inputs_bit_offset = REPORT_ID_INPUT_BIT_OFFSET_INIT;
					debug_trace("new report id; idx = %zu, inputs_bit_offset = %zu, report_ids_count = %zu", ctx->report_ids_count - 1, r->inputs_bit_offset, ctx->report_ids_count);
				} else {
					debug_error("insufficient report ids storage (count = %zu, max = %u)", ctx->report_ids_count, REPORT_IDS_MAX);
					return false;
				}
			}
			break;
		case TAG_GLOBAL_REPORT_COUNT:
			ctx->globals.report_count = (uint8_t)hid_read_unsigned_le(item->data, item->data_size);
			debug_trace("global: report count = %u", ctx->globals.report_count);
			break;
		case TAG_GLOBAL_PUSH:
			// Save all globals.
			if(!ctx->have_saved_globals) {
				ctx->saved_globals = ctx->globals;
				ctx->have_saved_globals = true;
				debug_trace("global: push");
			} else {
				debug_error("can't push globals; stack depth exceeded");
				return false;
			}
			break;
		case TAG_GLOBAL_POP:
			// Restore all globals (if we have any).
			if(ctx->have_saved_globals) {
				ctx->globals = ctx->saved_globals;
				ctx->have_saved_globals = false;
				debug_trace("global: pop");
			} else {
				debug_error("can't pop globals; stack empty");
				return false;
			}
			break;
		default:
			// Ignore all other global tags, we don't handle them.
			debug_warn("unhandled global tag 0x%02X", item->tag);
			break;
	}
	
	return true;
}

static bool hid_parse_local_item(hid_parse_context_t *ctx, const hid_item_t *item) {
	// TODO: we don't handle Extended usages here. Extended usages are 32-bit,
	// (item->data_size == 4) where the upper 16 bits are Usage Page, and lower
	// 16-bits are Usage ID. The Usage Page of an extended usage is supposed to
	// override the globally-defined Usage Page.
	if(item->data_size == 4) {
		debug_warn("unhandled extended usage (value = 0x%08X)", hid_read_unsigned_le(item->data, item->data_size));
	}
	
	switch(item->tag) {
		case TAG_LOCAL_USAGE:
			if(ctx->locals.usages_count < LOCAL_USAGES_MAX) {
				ctx->locals.usages[ctx->locals.usages_count++] = (uint16_t)hid_read_unsigned_le(item->data, item->data_size);
				debug_trace("local: usage = 0x%04X (count = %zu)", ctx->locals.usages[ctx->locals.usages_count - 1], ctx->locals.usages_count);
			} else {
				debug_error("insufficient local usages storage (count = %zu, max = %u)", ctx->locals.usages_count, LOCAL_USAGES_MAX);
				return false;
			}
			break;
		case TAG_LOCAL_USAGE_MIN:
			ctx->locals.usage_min = (uint16_t)hid_read_unsigned_le(item->data, item->data_size);
			ctx->locals.have_usage_min_max = true;
			debug_trace("local: usage min = 0x%04X", ctx->locals.usage_min);
			break;
		case TAG_LOCAL_USAGE_MAX:
			ctx->locals.usage_max = (uint16_t)hid_read_unsigned_le(item->data, item->data_size);
			ctx->locals.have_usage_min_max = true;
			debug_trace("local: usage max = 0x%04X", ctx->locals.usage_max);
			break;
		default:
			// Ignore all other local tags, we don't handle them.
			debug_warn("unhandled local tag 0x%02X", item->tag);
			break;
	}
	
	return true;
}

bool hid_parse_report_descriptor(const uint8_t *descr, const size_t descr_len, hid_report_composition_t *composition) {
	size_t descr_offset = 0;
	hid_item_t item;

	if(descr == NULL || composition == NULL || descr_len == 0) return false;

	debug_trace("descr_len = %zu", descr_len);
	debug_trace("sizeof(hid_parse_context_t) = %zu", sizeof(hid_parse_context_t));

	// Initialise the parsing context.
	hid_parse_context_t ctx = {
		.composition = composition,
		.globals = {
			.usage_page = 0, .logical_min = 0, .logical_max = 0, .report_size = 8,
			.report_count = 0, .report_id = 0, .have_report_id = false
		},
		.have_saved_globals = false,
		.locals = {
			.usages_count = 0, .usage_min = 0, .usage_max = 0, .have_usage_min_max = false
		},
		.collection_depth = 0,
		.collection_prev_sibling = NULL,
		.collection_current = NULL,
		.report_ids = {{ .id = 0, .inputs_bit_offset = 0 }},
		.report_ids_count = 1
	};
	memset(ctx.composition->collection_storage, 0, sizeof(ctx.composition->collection_storage));
	ctx.composition->collection_storage_count = 0;
	memset(ctx.composition->input_storage, 0, sizeof(ctx.composition->input_storage));
	ctx.composition->input_storage_count = 0;

	// Create dummy root collection (i.e. not actually a collection that exists
	// in the report descriptor) and add to storage.
	ctx.composition->collection_root = &ctx.composition->collection_storage[ctx.composition->collection_storage_count++];
	ctx.composition->collection_root->is_root = true;
	ctx.composition->collection_root->parent = NULL;
	ctx.composition->collection_root->next_sibling = NULL;
	ctx.composition->collection_root->first_child = NULL;
	ctx.composition->collection_root->child_count = 0;
	ctx.collection_parent_stack[0] = ctx.composition->collection_root;

	while(descr_offset < descr_len) {
		const size_t item_size = hid_read_item(&descr[descr_offset], descr_len - descr_offset, &item);
		
		debug_trace("descr_offset = %zu, item_size = %zu", descr_offset, item_size);
		
		// Verify that we read the current item correctly and that its size
		// doesn't go off the end of our report descriptor data. If so, it's
		// invalid, so bail out.
		if(item_size == 0 || (descr_offset + item_size) > descr_len) {
			debug_error("item size zero or larger than remaining descriptor buffer");
			return false;
		}
		
		debug_trace(
			"item: format = %u, type = 0x%02X, tag = 0x%02X, data_size = %zu",
			item.format, item.type, item.tag, item.data_size
		);
		
		if(item.format == ITEM_FORMAT_SHORT) {
			switch(item.type) {
				case ITEM_TYPE_MAIN:
					if(!hid_parse_main_item(&ctx, &item)) return false;
					break;
				case ITEM_TYPE_GLOBAL:
					if(!hid_parse_global_item(&ctx, &item)) return false;
					break;
				case ITEM_TYPE_LOCAL:
					if(!hid_parse_local_item(&ctx, &item)) return false;
					break;
				default:
					debug_error("invalid item type 0x%02X", item.type);
					return false;
					break;
			}
		} else if(item.format == ITEM_FORMAT_LONG) {
			// Skip over long items, because we don't handle those (and also
			// there are no official long item tags defined by HID standard).
			debug_warn("skipping long format item");
		} else {
			debug_error("invalid item format %u", item.format);
			return false;
		}

		descr_offset += item_size;
	}
	
	// TODO: should this be a warning or error?
	if(ctx.collection_depth > 0) {
		debug_warn("unterminated collection after end of descriptor (collection_depth = %zu)", ctx.collection_depth);
	}
	
	return true;
}

void hid_dump_report_composition(const hid_report_composition_t *composition) {
	if(composition->collection_storage_count > 0) {
		if(composition->collection_root->is_root) {
			hid_dump_collection(composition->collection_root, 0);
		} else {
			debug_error("root collection isn't actually root");
		}
	}
}

const hid_collection_t* hid_find_child_collection(const hid_collection_t *parent, const uint8_t type, const uint16_t usage_page, const uint16_t usage) {
	const hid_collection_t *result = NULL;
	
	const hid_collection_t *c = parent->first_child;
	while(c != NULL) {
		// Look for a child collection that has the given type, usage page (or
		// any usage page if we've been given 'undefined'), and usage (or any if
		// given 'undefined' for that too).
		if(
			c->type == type &&
			(usage_page == HID_USAGE_PAGE_UNDEFINED || c->usage_page == usage_page) &&
			(usage == HID_USAGE_UNDEFINED || c->usage == usage)
		) {
			result = c;
			break;
		}
		
		c = c->next_sibling;
	}
	
	return result;
}

const hid_collection_t* hid_find_containing_collection(const hid_input_t *input, const uint8_t type, const uint16_t usage_page, const uint16_t usage) {
	const hid_collection_t *result = NULL;
	
	const hid_collection_t *c = input->collection;
	while(c != NULL && !c->is_root) {
		// Look for a collection that has the given type, usage page (or any
		// usage page if we've been given 'undefined'), and usage (or any if
		// given 'undefined' for that too).
		if(
			c->type == type &&
			(usage_page == HID_USAGE_PAGE_UNDEFINED || c->usage_page == usage_page) &&
			(usage == HID_USAGE_UNDEFINED || c->usage == usage)
		) {
			result = c;
			break;
		}
		
		// Nothing found, continue search with parent collection.
		c = c->parent;
	}

	return result;
}

bool hid_input_is_contained_by_collection(const hid_input_t *input, const hid_collection_t *ancestor) {
	const hid_collection_t *c = input->collection;
	while(c != NULL && !c->is_root) {
		// Do we have matching ancestor collection? If not, continue search with
		// parent collection.
		if(c == ancestor) {
			return true;
		} else {
			c = c->parent;
		}
	}
	
	return false;
}

bool hid_input_has_usage(const hid_input_t *input, const uint16_t usage) {
	// Simply iterate over given input's usages, looking for the given value.
	for(size_t i = 0; i < input->usages_count && i < HID_INPUT_USAGES_MAX; i++) {
		if(input->usages[i] == usage) return true;
	}
	
	// Otherwise, check that given usage value is within input's min and max.
	if(input->have_usage_min_max && input->usage_min <= usage && input->usage_max >= usage) {
		return true;
	}
	
	return false;
}

size_t hid_input_usage_bit_offset(const hid_input_t *input, const uint16_t usage) {
	size_t field_idx = 0, field_bit_offset = 0;
	bool found_usage = false;
	
	if(input != NULL) {
		if(input->have_usage_min_max) {
			// When the given input has usage min and max, iterate over that
			// range looking for the given usage value.
			for(uint16_t u = input->usage_min; u <= input->usage_max; u++) {
				if(u == usage) {
					found_usage = true;
					break;
				}
				field_idx++;
			}
		} else {
			// Otherwise, look in the input's usages list (which should be in
			// order that they appear in the report) for the given usage value.
			for(size_t i = 0; i < input->usages_count && i < HID_INPUT_USAGES_MAX; i++) {
				if(input->usages[i] == usage) {
					found_usage = true;
					break;
				}
				field_idx++;
			}
		}
		
		if(found_usage) {
			// If we found the given usage, calculate the overall bit offset
			// within the report by multiplying the index of the usage within
			// the input (e.g. for usages of X,Y index of Y would be 1) by the
			// input's report size (in bits), and adding the input's starting
			// bit offset.
			field_bit_offset = (field_idx * (size_t)input->report_size) + input->bit_offset;
		}
	}
	
	return field_bit_offset;
}

// Read up to 32 bits from report buffer at arbitrary bit offset (LSB bit 0)
// HID reports are little-endian bitwise (least-significant bit first).
// bit_size must be between 1 and 32.
uint32_t hid_read_report_value_unsigned(const uint8_t *report, const size_t report_len, const uint32_t bit_offset, const uint8_t bit_size) {
    uint32_t val = 0;
	
    if(report != NULL && bit_size > 0 && bit_size <= 32) {
		// Calculate the index within the report of the field's starting byte,
		// and its offset within that byte, as well as the number of bytes it
		// occupies. May only partially occupy several bytes, so round up the
		// number of bytes needed.
		const size_t byte_index = bit_offset / 8;
		const uint8_t byte_bit_offset = bit_offset % 8;
		const size_t bytes_needed = (byte_bit_offset + bit_size + 7) / 8;
		
		// Check the field doesn't overflow our report data buffer.
		if(byte_index + bytes_needed <= report_len) {
			// Grab and concatenate all the bytes the field occupies, then shift
			// it appropriately according to its bit offset.
			for(size_t i = 0; i < bytes_needed; i++) {
				val |= ((uint32_t)report[byte_index + i]) << (8 * i);
			}
			val >>= byte_bit_offset;
			
			// Mask the resulting value to just the size of the field.
			if(bit_size < 32) val &= ((1u << bit_size) - 1u);
		}
	}
	
    return val;
}

int32_t hid_read_report_value_signed(const uint8_t *report, const size_t report_len, const uint32_t bit_offset, const uint8_t bit_size) {
	return hid_sign_extend(hid_read_report_value_unsigned(report, report_len, bit_offset, bit_size), bit_size);
}
