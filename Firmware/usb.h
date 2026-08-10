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

#ifndef USB_H_
#define USB_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define USB_MAX_CONFIGURATIONS 1
#define USB_MAX_INTERFACES     4
#define USB_MAX_HIDS           4
#define USB_MAX_ENDPOINTS      8
#define USB_MAX_STRINGS        3

#define USB_DEV_CLASS_RESERVED 0x00
#define USB_DEV_CLASS_COMM     0x02
#define USB_DEV_CLASS_HUB      0x09
#define USB_DEV_CLASS_VENDOR   0xFF

#define USB_INTF_CLASS_RESERVED 0x00
#define USB_INTF_CLASS_AUDIO    0x01
#define USB_INTF_CLASS_CDC_CTRL 0x02
#define USB_INTF_CLASS_HID      0x03
#define USB_INTF_CLASS_PHYSICAL 0x05
#define USB_INTF_CLASS_IMAGE    0x06
#define USB_INTF_CLASS_PRINTER  0x07
#define USB_INTF_CLASS_STORAGE  0x08
#define USB_INTF_CLASS_CDC_DATA 0x0A
#define USB_INTF_CLASS_VENDOR   0xFF

#define USB_HID_INTF_SUBCLASS_NONE 0x00
#define USB_HID_INTF_SUBCLASS_BOOT 0x01

#define USB_HID_INTF_PROTOCOL_NONE  0x00
#define USB_HID_INTF_PROTOCOL_KEYBD 0x01
#define USB_HID_INTF_PROTOCOL_MOUSE 0x02

#define USB_CFG_ATTR_SELF_POWERED  0x40
#define USB_CFG_ATTR_REMOTE_WAKEUP 0x20

#define USB_ENDP_ADDR_DIR_MASK 0x80
#define USB_ENDP_ADDR_DIR_OUT  0x00
#define USB_ENDP_ADDR_DIR_IN   0x80
#define USB_ENDP_ADDR_NUM_MASK 0x0F

#define USB_ENDP_ATTR_TFR_TYPE_MASK 0x03
#define USB_ENDP_ATTR_TFR_TYPE_CTRL 0x00
#define USB_ENDP_ATTR_TFR_TYPE_ISO  0x01
#define USB_ENDP_ATTR_TFR_TYPE_BULK 0x02
#define USB_ENDP_ATTR_TFR_TYPE_INT  0x03

#define USB_ENDP_ATTR_SYNC_TYPE_MASK  0x0C
#define USB_ENDP_ATTR_SYNC_TYPE_NONE  0x00
#define USB_ENDP_ATTR_SYNC_TYPE_ASYNC 0x04
#define USB_ENDP_ATTR_SYNC_TYPE_ADAPT 0x08
#define USB_ENDP_ATTR_SYNC_TYPE_SYNCH 0x0C

#define USB_ENDP_ATTR_USAGE_TYPE_MASK 0x30
#define USB_ENDP_ATTR_USAGE_TYPE_DATA 0x00
#define USB_ENDP_ATTR_USAGE_TYPE_FDBK 0x10
#define USB_ENDP_ATTR_USAGE_TYPE_IMPL 0x20
#define USB_ENDP_ATTR_USAGE_TYPE_RSVD 0x30

#define USB_HID_IDLE_DURATION_INDEFINITE 0
#define USB_HID_IDLE_REPORT_ALL 0

/******************************************************************************/

typedef enum {
	USB_FULL_SPEED = 0,
	USB_LOW_SPEED = 1
} usb_speed_enum_t;

typedef enum {
	USB_HID_PROTOCOL_BOOT = 0,
	USB_HID_PROTOCOL_REPORT = 1
} usb_hid_protocol_enum_t;

typedef struct __attribute__((packed)) {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t bcdUSB;
	uint8_t  bDeviceClass;
	uint8_t  bDeviceSubClass;
	uint8_t  bDeviceProtocol;
	uint8_t  bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t  iManufacturer;
	uint8_t  iProduct;
	uint8_t  iSerialNumber;
	uint8_t  bNumConfigurations;
} usb_device_descriptor_t;

typedef struct __attribute__((packed)) {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t wTotalLength;
	uint8_t  bNumInterfaces;
	uint8_t  bConfigurationValue;
	uint8_t  iConfiguration;
	uint8_t  bmAttributes;
	uint8_t  bMaxPower;
} usb_config_descriptor_t;

typedef struct __attribute__((packed)) {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bInterfaceNumber;
	uint8_t  bAlternateSetting;
	uint8_t  bNumEndpoints;
	uint8_t  bInterfaceClass;
	uint8_t  bInterfaceSubClass;
	uint8_t  bInterfaceProtocol;
	uint8_t  iInterface;
} usb_interface_descriptor_t;

typedef struct __attribute__((packed)) {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t bcdHID;
	uint8_t  bCountryCode;
	uint8_t  bNumDescriptors;
	// NOTE: Only the single required Report descriptor is supported here.
	// Optional additional Physical descriptors are not supported.
	uint8_t  bReportDescriptorType;
	uint16_t wReportDescriptorLength;
} usb_hid_descriptor_t;

typedef struct __attribute__((packed)) {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bEndpointAddress;
	uint8_t  bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t  bInterval;
} usb_endpoint_descriptor_t;

typedef struct __attribute__((packed)) {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bString[100];
} usb_string_descriptor_t;

typedef struct {
	size_t dev_descr_index; // index of device descriptor in the storage array
	size_t cfg_node_count;
	size_t cfg_node_indices[USB_MAX_CONFIGURATIONS];
} usb_device_node_t;

typedef struct {
	size_t cfg_descr_index; // index of cfg descriptor in the storage array
	size_t intf_node_count;
	size_t intf_node_indices[USB_MAX_INTERFACES];
} usb_config_node_t;

typedef struct {
	size_t intf_descr_index; // index of interface descriptor in the storage array
	// TODO: do we really need support for multiple hid nodes here? an interface can only have one hid descriptor?
	size_t hid_node_count;
	size_t hid_node_indices[USB_MAX_HIDS];
	size_t endp_node_count;
	size_t endp_node_indices[USB_MAX_ENDPOINTS];
} usb_interface_node_t;

typedef struct {
	size_t hid_descr_index; // index of hid descriptor in the storage array
} usb_hid_node_t;

typedef struct {
	size_t endp_descr_index; // index of endpoint descriptor in the storage array
} usb_endpoint_node_t;

// TODO: add endpoint toggle state somewhere? maybe not needed...
typedef struct {
	bool port_enabled; // enabled, disabled?
	bool port_connected; // connected, disconnected
	usb_speed_enum_t port_speed; // low, full
	uint8_t dev_current_cfg; // currently-selected device configuration
	uint8_t dev_address;
	// Descriptor storage:
	usb_device_descriptor_t dev_descr;
	size_t cfg_descr_count;
	usb_config_descriptor_t cfg_descrs[USB_MAX_CONFIGURATIONS];
	size_t intf_descr_count;
	usb_interface_descriptor_t intf_descrs[USB_MAX_INTERFACES];
	size_t endp_descr_count;
	usb_hid_descriptor_t hid_descrs[USB_MAX_HIDS];
	size_t hid_descr_count;
	usb_endpoint_descriptor_t endp_descrs[USB_MAX_ENDPOINTS];
	size_t str_descr_count;
	usb_string_descriptor_t str_descrs[USB_MAX_STRINGS];
	// Tree hierarchy:
	usb_device_node_t dev_tree;
	usb_config_node_t cfg_trees[USB_MAX_CONFIGURATIONS];
	usb_interface_node_t intf_trees[USB_MAX_INTERFACES];
	usb_hid_node_t hid_trees[USB_MAX_HIDS];
	usb_endpoint_node_t endp_trees[USB_MAX_ENDPOINTS];
} usb_context_t;

// Type for function pointer for callback from the enumeration process where
// a device's descriptors can be evaluated and the device accepted by returning
// a configuration value, or rejected by returning zero.
typedef uint8_t (*usb_device_eval_func_t)(const usb_context_t * const ctx);

/******************************************************************************/

extern void usb_context_init(usb_context_t * const ctx) __attribute__((nonnull(1)));
extern void usb_host_init(void);
extern bool usb_have_device_attached(void);
extern void usb_set_bus_suspend(const bool suspended);
extern bool usb_enum_root_device(usb_context_t * const ctx, const usb_device_eval_func_t eval_dev_fn) __attribute__((nonnull(1,2)));
extern bool usb_hid_get_report_descriptor(usb_context_t * const ctx, const uint8_t intf_num, uint8_t * const out_buf, const size_t out_buf_size, size_t * const out_size) __attribute__((nonnull(1,3,5)));
extern bool usb_hid_set_idle(usb_context_t * const ctx, const uint8_t intf_num, const uint8_t duration, const uint8_t report_id) __attribute__((nonnull(1)));
extern bool usb_hid_set_protocol(usb_context_t * const ctx, const uint8_t intf_num, const usb_hid_protocol_enum_t protocol) __attribute__((nonnull(1)));
extern bool usb_hid_interrupt_in(usb_context_t * const ctx, const uint8_t endp_addr, uint8_t * const out_buf, const size_t out_buf_size, size_t * const out_size) __attribute__((nonnull(1,3)));
extern size_t usb_convert_str_utf16_to_ascii(const uint16_t * const utf16, const size_t utf16_len, uint8_t * const ascii_out, const size_t ascii_out_size) __attribute__((nonnull(1,3)));
extern void usb_dump_descriptors(const usb_context_t * const ctx) __attribute__((nonnull(1)));

#endif // USB_H_
