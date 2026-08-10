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
#include "ch32x035.h"
#include "interrupt.h"
#include "debug.h"
#include "tstamp.h"
#include "event.h"
#include "usb.h"

#define USB_ENUM_INITIAL_DELAY_MS 100
#define USB_RESET_PERIOD_MS 12
#define USB_RESET_DELAY_MS 2
#define USB_RESET_RETRIES_MAX 5
#define USB_RESET_RETRY_DELAY_MS 8
#define USB_RESET_RETRY_DELAY_MULT 2
#define USB_TRANSFER_TIMEOUT_MS 1
#define USB_TRANSACT_RETRIES_DEFAULT 10
#define USB_TRANSACT_RETRIES_NONE 0
#define USB_SET_ADDR_DELAY_MS 10
#define USB_PACKET_SIZE_MIN 8
#define USB_PACKET_SIZE_MAX 64
#define USB_ENDP0_PACKET_SIZE_DEFAULT USB_PACKET_SIZE_MAX
#define USB_DEVICE_ADDR_MIN 1
#define USB_DEVICE_ADDR_MAX 127
#define USB_DEVICE_ADDR 62

/******************************************************************************/

// USB packet ID types
typedef enum {
	USB_PID_NULL  = 0x00,
	USB_PID_OUT   = 0x01,
	USB_PID_IN    = 0x09,
	USB_PID_SOF   = 0x05,
	USB_PID_SETUP = 0x0D,
	USB_PID_DATA0 = 0x03,
	USB_PID_DATA1 = 0x0B,
	USB_PID_ACK   = 0x02,
	USB_PID_NAK   = 0x0A,
	USB_PID_STALL = 0x0E,
	USB_PID_NYET  = 0x06,
	USB_PID_PRE   = 0x0C,
	USB_PID_ERR   = 0x0C
} usb_pid_enum_t;

// USB request types
typedef enum {
	USB_REQ_TYP_DIR_MASK     = 0x80,
	USB_REQ_TYP_DIR_HOST_DEV = 0x00,
	USB_REQ_TYP_DIR_DEV_HOST = 0x80,
	USB_REQ_TYP_TYP_MASK     = 0x60,
	USB_REQ_TYP_TYP_STANDARD = 0x00,
	USB_REQ_TYP_TYP_CLASS    = 0x20,
	USB_REQ_TYP_TYP_VENDOR   = 0x40,
	USB_REQ_TYP_TYP_RESERVED = 0x60,
	USB_REQ_TYP_RECIP_MASK   = 0x1F,
	USB_REQ_TYP_RECIP_DEV    = 0x00,
	USB_REQ_TYP_RECIP_IFACE  = 0x01,
	USB_REQ_TYP_RECIP_ENDP   = 0x02,
	USB_REQ_TYP_RECIP_OTHER  = 0x03,
} usb_req_type_enum_t;

// USB standard and HID device request codes
typedef enum {
	USB_GET_STATUS        = 0x00,
	USB_CLEAR_FEATURE     = 0x01,
	USB_SET_FEATURE       = 0x03,
	USB_SET_ADDRESS       = 0x05,
	USB_GET_DESCRIPTOR    = 0x06,
	USB_SET_DESCRIPTOR    = 0x07,
	USB_GET_CONFIGURATION = 0x08,
	USB_SET_CONFIGURATION = 0x09,
	USB_GET_INTERFACE     = 0x0A,
	USB_SET_INTERFACE     = 0x0B,
	USB_SYNCH_FRAME       = 0x0C,
	USB_HID_GET_REPORT    = 0x01,
	USB_HID_GET_IDLE      = 0x02,
	USB_HID_GET_PROTOCOL  = 0x03,
	USB_HID_SET_REPORT    = 0x09,
	USB_HID_SET_IDLE      = 0x0A,
	USB_HID_SET_PROTOCOL  = 0x0B
} usb_request_enum_t;

// USB descriptor types
typedef enum {
	USB_DESCR_TYP_DEVICE  = 0x01,
	USB_DESCR_TYP_CONFIG  = 0x02,
	USB_DESCR_TYP_STRING  = 0x03,
	USB_DESCR_TYP_INTERF  = 0x04,
	USB_DESCR_TYP_ENDP    = 0x05,
	USB_DESCR_TYP_QUALIF  = 0x06,
	USB_DESCR_TYP_SPEED   = 0x07,
	USB_DESCR_TYP_OTG     = 0x09,
	USB_DESCR_TYP_BOS     = 0X0F,
	USB_DESCR_TYP_HID     = 0x21,
	USB_DESCR_TYP_REPORT  = 0x22,
	USB_DESCR_TYP_PHYSIC  = 0x23,
	USB_DESCR_TYP_CS_INTF = 0x24,
	USB_DESCR_TYP_CS_ENDP = 0x25,
	USB_DESCR_TYP_HUB     = 0x29
} usb_descriptor_type_enum_t;

typedef enum {
	USB_DATA_TOG_0 = 0,
	USB_DATA_TOG_1 = 1
} usb_data_toggle_enum_t;

typedef enum {
	PWR_VDD_3V3,
	PWR_VDD_5V0
} pwr_vdd_level_enum_t;

/******************************************************************************/

typedef struct __attribute__((packed)) {
	uint8_t bLength;
	uint8_t bDescriptorType;
} usb_descriptor_stub_t;

typedef struct __attribute__((packed)) {
	// TODO: union with struct containing bitfields for direction, type, recipient?
	/*
	union {
		struct {
			uint8_t direction : 1;
			uint8_t type : 2;
			uint8_t recipient : 5;
		};
		uint8_t bmRequestType;
	};
	*/
	uint8_t bmRequestType;
	uint8_t bRequest;
	union {
		struct {
			union {
				uint8_t bIndex;
				uint8_t bReportId;
			};
			union {
				uint8_t bType;
				uint8_t bReportType;
				uint8_t bDuration;
			};
		};
		uint16_t wValue;
	};
	uint16_t wIndex;
	uint16_t wLength;
} usb_setup_req_t;

/******************************************************************************/

// NOTE: all multi-byte values in requests, responses, and descriptors are in
// little-endian format.
// NOTE: all these setup requests must be word-aligned for DMA purposes.

// Get Device Descriptor command
static const usb_setup_req_t usb_setup_req_get_dev_desc __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_DEV_HOST | USB_REQ_TYP_TYP_STANDARD | USB_REQ_TYP_RECIP_DEV,
	.bRequest      = USB_GET_DESCRIPTOR,
	.bIndex        = 0,
	.bType         = USB_DESCR_TYP_DEVICE,
	.wIndex        = 0,
	.wLength       = sizeof(usb_device_descriptor_t)
};

// Get Configuration Descriptor command
static const usb_setup_req_t usb_setup_req_get_cfg_desc __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_DEV_HOST | USB_REQ_TYP_TYP_STANDARD | USB_REQ_TYP_RECIP_DEV,
	.bRequest      = USB_GET_DESCRIPTOR,
	.bIndex        = 0,
	.bType         = USB_DESCR_TYP_CONFIG,
	.wIndex        = 0,
	.wLength       = sizeof(usb_config_descriptor_t)
};

// Get String Descriptor command
static const usb_setup_req_t usb_setup_req_get_str_desc __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_DEV_HOST | USB_REQ_TYP_TYP_STANDARD | USB_REQ_TYP_RECIP_DEV,
	.bRequest      = USB_GET_DESCRIPTOR,
	.bIndex        = 0, // specify string index here
	.bType         = USB_DESCR_TYP_STRING,
	.wIndex        = 0x0409, // wLANGID for English (US)
	.wLength       = sizeof(usb_string_descriptor_t)
};

// Set Address command
static const usb_setup_req_t usb_setup_req_set_addr __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_HOST_DEV | USB_REQ_TYP_TYP_STANDARD | USB_REQ_TYP_RECIP_DEV,
	.bRequest      = USB_SET_ADDRESS,
	.wValue        = 0, // specify device address here
	.wIndex        = 0,
	.wLength       = 0
};

// Set Configuration command
static const usb_setup_req_t usb_setup_req_set_cfg __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_HOST_DEV | USB_REQ_TYP_TYP_STANDARD | USB_REQ_TYP_RECIP_DEV,
	.bRequest      = USB_SET_CONFIGURATION,
	.wValue        = 0, // specify configuration here (or zero to place devce in addr state)
	.wIndex        = 0,
	.wLength       = 0
};

// Get HID Report Descriptor command
static const usb_setup_req_t usb_setup_req_hid_get_report_desc __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_DEV_HOST | USB_REQ_TYP_TYP_STANDARD | USB_REQ_TYP_RECIP_IFACE,
	.bRequest      = USB_GET_DESCRIPTOR,
	.bIndex        = 0,
	.bType         = USB_DESCR_TYP_REPORT,
	.wIndex        = 0, // specify interface here
	.wLength       = 0 // specify length of report descr here
};

static const usb_setup_req_t usb_setup_req_hid_set_idle __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_HOST_DEV | USB_REQ_TYP_TYP_CLASS | USB_REQ_TYP_RECIP_IFACE,
	.bRequest      = USB_HID_SET_IDLE,
	.bDuration     = 0, // (0 = infinite)
	.bReportId     = 0, // (0 = all reports)
	.wIndex        = 0, // specify interface here
	.wLength       = 0
};

static const usb_setup_req_t usb_setup_req_hid_set_protocol __attribute__((aligned(4))) = {
	.bmRequestType = USB_REQ_TYP_DIR_HOST_DEV | USB_REQ_TYP_TYP_CLASS | USB_REQ_TYP_RECIP_IFACE,
	.bRequest      = USB_HID_SET_PROTOCOL,
	.wValue        = 0, // specify protocol here (0 = boot, 1 = report)
	.wIndex        = 0, // specify interface here
	.wLength       = 0
};

// Buffers for host endpoint data transmit and receive. Must be word-aligned for
// DMA usage.
static uint8_t usb_rx_buf[USB_PACKET_SIZE_MAX] __attribute__((aligned(4)));
static uint8_t usb_tx_buf[USB_PACKET_SIZE_MAX] __attribute__((aligned(4)));

/*****************************************************************************/

static pwr_vdd_level_enum_t pwr_vdd_get_level(void) {
	// Ensure PWR peripheral clock is enabled.
	RCC->APB1PCENR |= RCC_PWREN;
	
	// Record the current PVD threshold.
	const uint16_t pls_prev = PWR->CTLR & PWR_CTLR_PLS;

	// Configure PVD voltage monitoring threshold to be 4V.
	PWR->CTLR = (PWR->CTLR & ~PWR_CTLR_PLS) | PWR_CTLR_PLS_4V02R_4V00F;
	
	// Short delay to allow PVD to stabilise.
	for(uint32_t i = 0; i < 500; i++) __asm volatile("nop");
	
	// If now below the 4V threshold, assume we're running on 3.3V; above, 5V.
	const pwr_vdd_level_enum_t vdd_level = ((PWR->CSR & PWR_CSR_PVDO) ? PWR_VDD_3V3 : PWR_VDD_5V0);

	// Return the PVD threshold to what it previously was.
	PWR->CTLR = (PWR->CTLR & ~PWR_CTLR_PLS) | pls_prev;

    return vdd_level;
}

static void usb_set_self_address(const uint8_t addr) {
	USBFSH->DEV_ADDR = (USBFSH->DEV_ADDR & ~USBFS_DEV_ADDR_MASK_USB_ADDR) | (addr & USBFS_DEV_ADDR_MASK_USB_ADDR);
}

static void usb_set_self_speed(const usb_speed_enum_t speed) {
	switch(speed) {
		case USB_FULL_SPEED:
			USBFSH->BASE_CTRL &= ~USBFS_BASE_CTRL_UC_LOW_SPEED;
			USBFSH->HOST_CTRL &= ~USBFS_HOST_CTRL_UH_LOW_SPEED;
			USBFSH->HOST_SETUP &= ~USBFS_HOST_SETUP_UH_PRE_PID_EN;
			break;
		case USB_LOW_SPEED:
			USBFSH->BASE_CTRL |= USBFS_BASE_CTRL_UC_LOW_SPEED;
			USBFSH->HOST_CTRL |= USBFS_HOST_CTRL_UH_LOW_SPEED;
			USBFSH->HOST_SETUP |= USBFS_HOST_SETUP_UH_PRE_PID_EN;
			break;
	}
}

static void usb_root_hub_device_reset(usb_context_t * const ctx) {
	// Set the host root hub address to zero.
	USBFSH->DEV_ADDR = (USBFSH->DEV_ADDR & ~USBFS_DEV_ADDR_MASK_USB_ADDR) | (0x00 & USBFS_DEV_ADDR_MASK_USB_ADDR);
	ctx->dev_address = 0;
	
	// Default to full speed.
	usb_set_self_speed(USB_FULL_SPEED);
	ctx->port_speed = USB_FULL_SPEED;

	// Force a bus reset, for at least 10 milliseconds. We also need a small
	// delay after.
	USBFSH->HOST_CTRL |= USBFS_HOST_CTRL_UH_BUS_RESET;
	timestamp_delay_ms(USB_RESET_PERIOD_MS);
	USBFSH->HOST_CTRL &= ~USBFS_HOST_CTRL_UH_BUS_RESET;
	timestamp_delay_ms(USB_RESET_DELAY_MS);
}

static bool usb_root_hub_port_enable(usb_context_t * const ctx) {
	// Check whether a USB device is connected.
	if(USBFSH->MIS_ST & USBFS_MIS_ST_UMS_DEV_ATTACH) {
		// Is the host port enabled?
		if(!(USBFSH->HOST_CTRL & USBFS_HOST_CTRL_UH_PORT_EN)) {
			// What speed of device do we have attached, low- or full-speed?
			// This is given by getting the state of the DM line: when DM line
			// is high, low speed device; when DM line is low, full speed
			// device. Set our host speed to match.
			const usb_speed_enum_t speed = ((USBFSH->MIS_ST & USBFS_MIS_ST_UMS_DM_LEVEL) ? USB_LOW_SPEED : USB_FULL_SPEED);
			usb_set_self_speed(speed);
			ctx->port_speed = speed;
		}
		
		// Enable host port and automatic generation of keep-alive/SOF packets.
		USBFSH->HOST_CTRL |= USBFS_HOST_CTRL_UH_PORT_EN;
		USBFSH->HOST_SETUP |= USBFS_HOST_SETUP_UH_SOF_EN;

		// Return indicating that we succeeded, a device was attached.
		ctx->port_enabled = true;
		return true;
	}

	// Return indicating that we failed, a device is not attached (?).
	ctx->port_enabled = false;
	return false;
}

/*
	token_pid: pid of the token packet to send (e.g. SETUP, OUT, IN).
	token_endpoint: endpoint of the device that the token packet is being sent to.
	data_tog: current state of the endpoint's data toggle (i.e. DATA0, DATA1).
*/
static bool usb_transact(const usb_pid_enum_t token_pid, const uint8_t token_endpoint, const usb_data_toggle_enum_t data_tog, uint8_t retries) {
	timestamp_t wait_expiry;
	bool wait_timeout = false;
	usb_pid_enum_t resp_pid;
	
	switch(token_pid) {
		case USB_PID_SETUP:
			// Ignore the given initial data toggle state, because we always
			// must send a DATA0 packet for SETUP. We expect to receive an ACK
			// handshake.
			USBFSH->HOST_TX_CTRL = USBFS_HOST_TX_CTRL_UH_T_TOG_DATA0 | USBFS_HOST_TX_CTRL_UH_T_RES_ACK;
			USBFSH->HOST_RX_CTRL = USBFS_HOST_RX_CTRL_UH_R_TOG_DATA0 | USBFS_HOST_RX_CTRL_UH_R_RES_ACK;
			break;
		case USB_PID_IN:
			// We expect to receive a DATA0/1 packet (which dependant on toggle
			// state given), and respond with an ACK handshake. Also enable
			// auto-toggle.
			USBFSH->HOST_RX_CTRL = ((uint8_t)data_tog << __builtin_ctz(USBFS_HOST_RX_CTRL_UH_R_TOG)) | USBFS_HOST_RX_CTRL_UH_R_AUTO_TOG | USBFS_HOST_RX_CTRL_UH_R_RES_ACK;
			break;
		case USB_PID_OUT:
			// We expect to send a DATA0/1 packet (which dependant on toggle
			// state given), and receive an ACK handshake. Also enable
			// auto-toggle.
			USBFSH->HOST_TX_CTRL = ((uint8_t)data_tog << __builtin_ctz(USBFS_HOST_TX_CTRL_UH_T_TOG)) | USBFS_HOST_TX_CTRL_UH_T_AUTO_TOG | USBFS_HOST_TX_CTRL_UH_T_RES_ACK;
			break;
		default:
			debug_error("unknown token PID 0x%02X", token_pid);
			return false;
			break;
	}

	do {
		debug_trace("sending PID 0x%02X to endpoint %u", token_pid, token_endpoint);
		
		// Set the token PID and endpoint to the given values.
		USBFSH->HOST_EP_PID = ((uint8_t)token_pid << __builtin_ctz(USBFS_HOST_EP_PID_UH_TOKEN_MASK)) | (token_endpoint & USBFS_HOST_EP_PID_UH_ENDP_MASK);
		
		// Allow the transfer to begin by clearing any outstanding transfer
		// completion flag (?).
		USBFSH->INT_FG = USBFS_INT_FG_UIF_TRANSFER;
		
		// Wait up to 1 ms for the transfer to complete.
		timestamp_increment(timestamp_now(&wait_expiry), USB_TRANSFER_TIMEOUT_MS);
		while(!wait_timeout && !(USBFSH->INT_FG & USBFS_INT_FG_UIF_TRANSFER)) {
			wait_timeout = timestamp_compare_to_now(&wait_expiry, TIMESTAMP_CMP_LT);
		}
		
		// Stop the USB transfer.
		USBFSH->HOST_EP_PID = 0;

		// If we timed-out waiting for the transfer to complete and it still now
		// isn't complete, then give up and fail.
		if(wait_timeout && !(USBFSH->INT_FG & USBFS_INT_FG_UIF_TRANSFER)) {
			debug_trace("transfer timeout after %u ms", USB_TRANSFER_TIMEOUT_MS);
			return false;
		}
		
		// For an IN transaction (?), the received data packet was what we
		// expected, so we're finished (?).
		if(USBFSH->INT_ST & USBFS_INT_ST_UIS_TOG_OK) {
			debug_trace("tog ok");
			return true;
		}
		
		// Grab the PID received in response from the device.
		resp_pid = (usb_pid_enum_t)((USBFSH->INT_ST & USBFS_INT_ST_UIS_H_RES_MASK) >> __builtin_ctz(USBFS_INT_ST_UIS_H_RES_MASK));
		
		debug_trace("response PID 0x%02X", resp_pid);
		
		switch(token_pid) {
			case USB_PID_SETUP:
				// We should only ever receive an ACK handshake to SETUP.
				return (resp_pid == USB_PID_ACK);
				break;
			case USB_PID_IN:
				// Should receive either a DATA0/1 data packet, or NAK or STALL
				// handshake.
				if(resp_pid == USB_PID_DATA0 || resp_pid == USB_PID_DATA1) {
					return true;
				} else if(resp_pid == USB_PID_STALL) {
					// Device encountered some kind of problem.
					return false;
				} else if(resp_pid == USB_PID_NAK) {
					// Device has no data to respond with right now, loop round
					// and try again.
				}
				break;
			case USB_PID_OUT:
				// Should receive a handshake of ACK, NAK, or STALL.
				if(resp_pid == USB_PID_ACK) {
					return true;
				} else if(resp_pid == USB_PID_STALL) {
					// Device encountered some kind of problem.
					return false;
				} else if(resp_pid == USB_PID_NAK) {
					// Device is not ready to accept data, loop round and try
					// again.
				}
				break;
			default:
				debug_error("unknown token PID 0x%02X", token_pid);
				return false;
				break;
		}

		if(retries > 0) {
			// Short delay before retrying.
			timestamp_delay_ms(1);

			debug_trace("retrying, %u retries remain", retries);
		}
	} while(retries-- > 0);
	
	// Failed after too many retries.
	return false;
}

static inline usb_data_toggle_enum_t usb_get_rx_data_toggle(void) {
	return (USBFSH->HOST_RX_CTRL & USBFS_HOST_RX_CTRL_UH_R_TOG ? USB_DATA_TOG_1 : USB_DATA_TOG_0);
}

static inline usb_data_toggle_enum_t usb_get_tx_data_toggle(void) {
	return (USBFSH->HOST_TX_CTRL & USBFS_HOST_TX_CTRL_UH_T_TOG ? USB_DATA_TOG_1 : USB_DATA_TOG_0);
}

/*
	setup_req: the setup request struct to be used for the SETUP request.
	data_max_packet_size: maximum size of data packets, in bytes.
	data_buf_size: the capacity of the buffer of data to be sent/received.
	data_buf: pointer to a buffer of data to be received/sent at the data stage.
	data_buf_len: pointer to a length, which will be updated with length of data actually received or sent.
*/
static bool usb_control_transfer(const usb_setup_req_t * const setup_req, const size_t data_max_packet_size, const size_t data_buf_size, uint8_t *data_buf, size_t *data_buf_len) {
	size_t data_remain_len, data_packet_count, data_rx_len, data_tx_len;
	
	// Some sanity checks.
	if(setup_req == NULL) return false;
	if(setup_req->wLength > data_buf_size) return false;
	if(setup_req->wLength > 0 && (data_buf == NULL || data_buf_len == NULL)) return false;
	if(data_max_packet_size < USB_PACKET_SIZE_MIN || data_max_packet_size > USB_PACKET_SIZE_MAX) return false;
	if(data_max_packet_size & (data_max_packet_size - 1)) return false;
	
	debug_trace("setup_req:");
	debug_hex_trace(setup_req, sizeof(usb_setup_req_t));
	debug_trace("data_max_packet_size = %zu, data_buf_size = %zu", data_max_packet_size, data_buf_size);
	
	// -------------------------------------------------------------------------
	// SETUP STAGE
	// -------------------------------------------------------------------------
	
	debug_trace("setup stage");
	
	// First, we transfer a SETUP packet with the given setup request data.
	// Copy it into the TX buffer, set TX length, then make the transfer. The
	// data packet must use a DATA0 PID.
	memcpy(usb_tx_buf, setup_req, sizeof(usb_setup_req_t));
	USBFSH->HOST_TX_LEN = sizeof(usb_setup_req_t);
	if(!usb_transact(USB_PID_SETUP, 0, USB_DATA_TOG_0, USB_TRANSACT_RETRIES_DEFAULT)) return false;

	// Wait a little, because some devices aren't fast enough.
	timestamp_delay_ms(1);

	// -------------------------------------------------------------------------
	// DATA STAGE
	// -------------------------------------------------------------------------
	
	debug_trace("data stage");

	// Calculate how many data packets (if any) we expect the requested amount
	// of data to be split across. Where the data is an exact multiple of the
	// max packet size, then there'll be an extra zero-length packet at the end.
	data_remain_len = setup_req->wLength;
	data_packet_count = (setup_req->wLength > 0 ? (setup_req->wLength / data_max_packet_size) + 1 : 0);
	
	debug_trace("data_remain_len = %zu, data_packet_count = %zu", data_remain_len, data_packet_count);
	
	if(data_buf_len != NULL) *data_buf_len = 0;
	
	// TODO: more checks/guards to limit total data copied to data_buf to data_buf_size?
	
	// Do we actually have any data being transferred at this stage?
	if(data_remain_len > 0) {
		// DEVICE-TO-HOST data transfer
		if((setup_req->bmRequestType & USB_REQ_TYP_DIR_MASK) == USB_REQ_TYP_DIR_DEV_HOST) {
			while(data_packet_count > 0 && data_remain_len > 0) {
				// Receive the next data packet from the device.
				if(!usb_transact(USB_PID_IN, 0, usb_get_rx_data_toggle(), USB_TRANSACT_RETRIES_DEFAULT)) return false;
				
				// How much data did we receive for this IN request? Limit to
				// the maximum packet size.
				data_rx_len = (USBFSH->RX_LEN > data_max_packet_size ? data_max_packet_size : USBFSH->RX_LEN);
				
				debug_trace("data_rx_len = %zu", data_rx_len);
				
				// If we got a zero-length data packet, that signifies the end
				// of data for a total amount that's an exact multiple of
				// maximum packet size.
				if(data_rx_len == 0) break;
				
				// Increment the given buffer length value by the length of what
				// we received, and copy that received data into the given
				// buffer. Also increment the pointer into the buffer by that
				// length.
				if(data_buf_len != NULL) *data_buf_len += data_rx_len;
				if(data_buf != NULL) {
					memcpy(data_buf, usb_rx_buf, data_rx_len);
					data_buf += data_rx_len;
				}
				
				// If we still have more than one data packet outstanding, but
				// we just received a packet smaller than maximum size, then
				// that signifies that the device has prematurely finished
				// sending all the data it has.
				if(data_packet_count > 1 && data_rx_len < data_max_packet_size) break;
				
				data_remain_len -= data_rx_len;
				data_packet_count--;

				debug_trace("data_remain_len = %zu, data_packet_count = %zu", data_remain_len, data_packet_count);

				// Wait a little, because some devices aren't fast enough.
				timestamp_delay_ms(1);
			}
		// HOST-TO-DEVICE data transfer
		} else if((setup_req->bmRequestType & USB_REQ_TYP_DIR_MASK) == USB_REQ_TYP_DIR_HOST_DEV) {
			// TODO: do we need to also here do the extra zero-length packet for total data sizes of exact multiple of max packet size? Probably not.
			while(data_packet_count > 0 && data_remain_len > 0) {
				// How much data are we sending in this OUT request? Limit to
				// the maximum packet size.
				data_tx_len = (data_remain_len > data_max_packet_size ? data_max_packet_size : data_remain_len);
				USBFSH->HOST_TX_LEN = data_tx_len;

				debug_trace("data_tx_len = %zu", data_tx_len);
				
				// Increment the given buffer length value by the length of what
				// we are sending, and copy the data to be sent into the TX
				// buffer. Also increment the pointer into the buffer by that
				// length.
				if(data_buf_len != NULL) *data_buf_len += data_tx_len;
				if(data_buf != NULL) {
					memcpy(usb_tx_buf, data_buf, data_tx_len);
					data_buf += data_tx_len;
				}
				
				// Send the data packet to the device.
				if(!usb_transact(USB_PID_OUT, 0, usb_get_tx_data_toggle(), USB_TRANSACT_RETRIES_DEFAULT)) return false;
				
				data_remain_len -= data_tx_len;
				data_packet_count--;
				
				debug_trace("data_remain_len = %zu, data_packet_count = %zu", data_remain_len, data_packet_count);

				// Wait a little, because some devices aren't fast enough.
				timestamp_delay_ms(1);
			}
		}
	}

	// -------------------------------------------------------------------------
	// STATUS STAGE
	// -------------------------------------------------------------------------
	
	debug_trace("status stage");
	
	// HOST-TO-DEVICE data transfer
	// Additionally, if there was no Data stage, then act as per a previous
	// host-to-device data stage.
	if((setup_req->wLength == 0) || ((setup_req->bmRequestType & USB_REQ_TYP_DIR_MASK) == USB_REQ_TYP_DIR_HOST_DEV)) {
		// The device acknowledges the successful receipt of the data by
		// returning a zero length data packet in response to an IN token. The
		// data packet must use a DATA1 PID.
		if(!usb_transact(USB_PID_IN, 0, USB_DATA_TOG_1, USB_TRANSACT_RETRIES_DEFAULT) || USBFSH->RX_LEN != 0) return false;
	// DEVICE-TO-HOST data transfer
	} else if((setup_req->bmRequestType & USB_REQ_TYP_DIR_MASK) == USB_REQ_TYP_DIR_DEV_HOST) {
		// We acknowledge the successful receipt of the data by sending an OUT
		// token followed by a zero length data packet. The data packet must
		// use a DATA1 PID.
		USBFSH->HOST_TX_LEN = 0;
		if(!usb_transact(USB_PID_OUT, 0, USB_DATA_TOG_1, USB_TRANSACT_RETRIES_DEFAULT)) return false;
	}
	
	return true;
}

static bool usb_interrupt_in_transfer(const uint8_t endp_addr, const size_t data_max_packet_size, const size_t data_buf_size, uint8_t *data_buf, size_t *data_buf_len) {
	// Some sanity checks. Including that given endpoint address is actually an
	// IN endpoint.
	if(data_buf == NULL || data_buf_len == NULL) return false;
	if(data_max_packet_size == 0 || data_max_packet_size > USB_PACKET_SIZE_MAX) return false;
	if((endp_addr & USB_ENDP_ADDR_DIR_MASK) != USB_ENDP_ADDR_DIR_IN) return false;

	// Receive the data packet from the device. Endpoint numbers are only 4
	// bits, so mask the given address to that. We don't care if the device NAKs
	// in response (probably because no new data), so don't retry if it does.
	if(!usb_transact(USB_PID_IN, endp_addr & USB_ENDP_ADDR_NUM_MASK, usb_get_rx_data_toggle(), USB_TRANSACT_RETRIES_NONE)) return false;

	// How much data did we receive for this IN request? Limit to the maximum
	// packet size.
	const size_t data_rx_len = (USBFSH->RX_LEN > data_max_packet_size ? data_max_packet_size : USBFSH->RX_LEN);

	debug_trace("data_rx_len = %zu", data_rx_len);

	if(data_rx_len == 0) return false;

	// Copy the received data into the given buffer.
	*data_buf_len = data_rx_len;
	memcpy(data_buf, usb_rx_buf, (data_rx_len < data_buf_size ? data_rx_len : data_buf_size));

	return true;
}

static void usb_parse_descriptors(usb_context_t * const ctx, const uint8_t * const buf, const size_t buf_size) {
	size_t buf_offset = 0;

	ctx->cfg_descr_count = 0;
	ctx->intf_descr_count = 0;
	ctx->endp_descr_count = 0;
	ctx->dev_tree.cfg_node_count = 0;

	// First, parse the initial device descriptor (which should be in the first
	// 18 bytes).
	if(buf_size < sizeof(usb_device_descriptor_t)) {
		debug_error("buffer too small; expecting %zu, given %zu", sizeof(usb_device_descriptor_t), buf_size);
		return;
	}
	memcpy(&ctx->dev_descr, buf, sizeof(usb_device_descriptor_t));
	ctx->dev_tree.dev_descr_index = 0;
	buf_offset += sizeof(usb_device_descriptor_t);

	// Second, parse the full configuration descriptor, which has appended all
	// the interface descriptors for that configuration, and all the endpoint
	// descriptors for each interface. The first interface descriptor follows
	// the configuration descriptor. The endpoint descriptors for the first
	// interface follow the first interface descriptor. If there are additional
	// interfaces, their interface descriptor and endpoint descriptors follow
	// the first interface’s endpoint descriptors. Any class-specific (e.g. HID)
	// descriptors follow the standard descriptors they extend or modify.
	while(buf_offset + sizeof(usb_descriptor_stub_t) <= buf_size) {
		const usb_descriptor_stub_t *stub = (usb_descriptor_stub_t *)&buf[buf_offset];

		// Quit when we encounter a zero length (i.e. blank space at end of
		// buffer). Also ensure we don't access past the end of the buffer.
		if(stub->bLength == 0 || buf_offset + stub->bLength > buf_size) break;

		switch(stub->bDescriptorType) {
			case USB_DESCR_TYP_CONFIG:
				// Check we haven't maxed-out our storage capacity.
				if(ctx->cfg_descr_count >= USB_MAX_CONFIGURATIONS) {
					debug_error("too many configurations (got %zu, max %zu)", ctx->cfg_descr_count, USB_MAX_CONFIGURATIONS);
					break;
				}
				// Verify the length given matches the descriptor struct size.
				if(stub->bLength != sizeof(ctx->cfg_descrs[ctx->cfg_descr_count])) {
					debug_error("invalid descriptor length (got %u, expect %zu)", stub->bLength, sizeof(ctx->cfg_descrs[ctx->cfg_descr_count]));
					break;
				}
				// Copy the config descriptor data into the storage array.
				memcpy(&ctx->cfg_descrs[ctx->cfg_descr_count], &buf[buf_offset], stub->bLength);
				// Build tree nodes. For this configuration, record index into
				// storage array and set number of interfaces it has to zero.
				// Then record this configuration's node index on the containing
				// device.
				ctx->cfg_trees[ctx->cfg_descr_count].cfg_descr_index = ctx->cfg_descr_count;
				ctx->cfg_trees[ctx->cfg_descr_count].intf_node_count = 0;
				ctx->dev_tree.cfg_node_indices[ctx->dev_tree.cfg_node_count++] = ctx->cfg_descr_count;
				ctx->cfg_descr_count++;
				break;
			case USB_DESCR_TYP_INTERF:
				// Check we haven't maxed-out our storage capacity.
				if(ctx->intf_descr_count >= USB_MAX_INTERFACES) {
					debug_error("too many interfaces (got %zu, max %zu)", ctx->intf_descr_count, USB_MAX_INTERFACES);
					break;
				}
				// Verify the length given matches the descriptor struct size.
				if(stub->bLength != sizeof(ctx->intf_descrs[ctx->intf_descr_count])) {
					debug_error("invalid descriptor length (got %u, expect %zu)", stub->bLength, sizeof(ctx->intf_descrs[ctx->intf_descr_count]));
					break;
				}
				// Copy the interface descriptor data into the storage array.
				memcpy(&ctx->intf_descrs[ctx->intf_descr_count], &buf[buf_offset], stub->bLength);
				// Build tree nodes. For this interface, record index into
				// storage array and set number of endpoints it has to zero.
				// Then record this interface's node index on the containing
				// (i.e. last seen) configuration.
				ctx->intf_trees[ctx->intf_descr_count].intf_descr_index = ctx->intf_descr_count;
				ctx->intf_trees[ctx->intf_descr_count].endp_node_count = 0;
				if(ctx->cfg_descr_count > 0) {
					ctx->cfg_trees[ctx->cfg_descr_count - 1].intf_node_indices[
						ctx->cfg_trees[ctx->cfg_descr_count - 1].intf_node_count++
					] = ctx->intf_descr_count;
				}
				ctx->intf_descr_count++;
				break;
			case USB_DESCR_TYP_HID:
				// Check we haven't maxed-out our storage capacity.
				if(ctx->hid_descr_count >= USB_MAX_HIDS) {
					debug_error("too many hids (got %zu, max %zu)", ctx->hid_descr_count, USB_MAX_HIDS);
					break;
				}
				// Verify the length given matches the descriptor struct size.
				if(stub->bLength != sizeof(ctx->hid_descrs[ctx->hid_descr_count])) {
					debug_error("invalid descriptor length (got %u, expect %zu)", stub->bLength, sizeof(ctx->hid_descrs[ctx->hid_descr_count]));
					break;
				}
				// Copy the HID descriptor data into the storage array.
				memcpy(&ctx->hid_descrs[ctx->hid_descr_count], &buf[buf_offset], stub->bLength);
				// Build tree nodes. For this HID, simply record index into
				// storage array and record this HID's node index on the
				// containing (i.e. last seen) interface.
				ctx->hid_trees[ctx->hid_descr_count].hid_descr_index = ctx->hid_descr_count;
				if(ctx->intf_descr_count > 0) {
					ctx->intf_trees[ctx->intf_descr_count - 1].hid_node_indices[
						ctx->intf_trees[ctx->intf_descr_count - 1].hid_node_count++
					] = ctx->hid_descr_count;
				}
				ctx->hid_descr_count++;
				break;
			case USB_DESCR_TYP_ENDP:
				// Check we haven't maxed-out our storage capacity.
				if(ctx->endp_descr_count >= USB_MAX_ENDPOINTS) {
					debug_error("too many endpoints (got %zu, max %zu)", ctx->endp_descr_count, USB_MAX_ENDPOINTS);
					break;
				}
				// Verify the length given matches the descriptor struct size.
				if(stub->bLength != sizeof(ctx->endp_descrs[ctx->endp_descr_count])) {
					debug_error("invalid descriptor length (got %u, expect %zu)", stub->bLength, sizeof(ctx->endp_descrs[ctx->endp_descr_count]));
					break;
				}
				// Copy the endpoint descriptor data into the storage array.
				memcpy(&ctx->endp_descrs[ctx->endp_descr_count], &buf[buf_offset], stub->bLength);
				// Build tree nodes. For this endpoint, simply record index into
				// storage array and record this endpoint's node index on the
				// containing (i.e. last seen) interface.
				ctx->endp_trees[ctx->endp_descr_count].endp_descr_index = ctx->endp_descr_count;
				if(ctx->intf_descr_count > 0) {
					ctx->intf_trees[ctx->intf_descr_count - 1].endp_node_indices[
						ctx->intf_trees[ctx->intf_descr_count - 1].endp_node_count++
					] = ctx->endp_descr_count;
				}
				ctx->endp_descr_count++;
				break;
			default:
				// Skip any unknown or unhandled descriptor types.
				debug_warn("unhandled descriptor; offset = %zu, len = %u, type = 0x%02X", buf_offset, stub->bLength, stub->bDescriptorType);
				break;
		}

		buf_offset += stub->bLength;
	}
}

static bool usb_get_device_descriptor(usb_context_t * const ctx, uint8_t * const out_buf, const size_t out_buf_size) {
	size_t recvd_size = 0;

	// Clear the output buffer.
	memset(out_buf, 0, out_buf_size);
	
	// Make the control transfer. If this is the first time we query the device
	// for its device descriptor, we assume that the bMaxPacketSize0 in the
	// supplied context is pre-populated with the appropriate value for the
	// speed of device (low or full).
	const bool result = usb_control_transfer(
		&usb_setup_req_get_dev_desc,
		ctx->dev_descr.bMaxPacketSize0,
		out_buf_size,
		out_buf,
		&recvd_size
	);

	debug_trace("result = %u, recvd_size = %zu", result, recvd_size);
	debug_trace("descriptor:");
	debug_hex_trace(out_buf, sizeof(usb_device_descriptor_t));

	// We probably shouldn't check size here, in case of full speed device where
	// max packet size defaults to 64, but device only supports 8 or 16 and so
	// returns less than full dev descriptor (18 bytes).
	/*
	// Return with success if transfer completed and we got back the correct
	// quantity of descriptor data.
	return (result && (recvd_size == sizeof(usb_device_descriptor_t)));
	*/
	return result;
}

static bool usb_get_config_descriptor(usb_context_t * const ctx, const uint8_t cfg_idx, uint8_t * const out_buf, const size_t out_buf_size) {
	usb_setup_req_t req;
	size_t recvd_size = 0;
	bool result;

	// Sanity check.
	// TODO: do we really need to do this check?
	// if(cfg_idx >= USB_MAX_CONFIGURATIONS) return false;

	debug_trace("cfg_idx = %u", cfg_idx);

	// Clear the output buffer.
	memset(out_buf, 0, out_buf_size);
	
	// Copy the template Get Descriptor command for getting a configuration
	// descriptor and set within it that we want the given configuration index,
	// and length such that (for now) we only get the configuration itself - no
	// interfaces, endpoints, etc.
	req = usb_setup_req_get_cfg_desc;
	req.bIndex = cfg_idx;
	req.wLength = sizeof(usb_config_descriptor_t);

	result = usb_control_transfer(
		&req,
		ctx->dev_descr.bMaxPacketSize0,
		out_buf_size,
		out_buf,
		&recvd_size
	);

	debug_trace("result = %u, recvd_size = %zu", result, recvd_size);
	debug_trace("descriptor:");
	debug_hex_trace(out_buf, sizeof(usb_config_descriptor_t));

	// Bail out if we failed at the first transfer.
	if(!result) return false;

	// Sanity-checks, including that the wTotalLength in the returned
	// configuration descriptor is smaller than our given output buffer size.
	if(recvd_size != sizeof(usb_config_descriptor_t)) return false;
	if(((usb_config_descriptor_t *)out_buf)->wTotalLength == 0) return false;
	if(((usb_config_descriptor_t *)out_buf)->wTotalLength > out_buf_size) return false;

	// Now we want the full configuration, so specify the total length in the
	// next Get Descriptor setup request.
	req.wLength = ((usb_config_descriptor_t *)out_buf)->wTotalLength;

	result = usb_control_transfer(
		&req,
		ctx->dev_descr.bMaxPacketSize0,
		out_buf_size,
		out_buf,
		&recvd_size
	);

	debug_trace("result = %u, recvd_size = %zu", result, recvd_size);
	debug_trace("descriptor:");
	debug_hex_trace(out_buf, (recvd_size <= out_buf_size ? recvd_size : out_buf_size));

	// Return with success if transfer completed and we got back enough data for
	// at least a single configuration descriptor plus a single interface
	// descriptor plus a single endpoint descriptor - that's the minimum we can
	// expect.
	return (result && (recvd_size >= sizeof(usb_config_descriptor_t) + sizeof(usb_interface_descriptor_t) + sizeof(usb_endpoint_descriptor_t)));
}

static bool usb_get_string_descriptor(usb_context_t * const ctx, const uint8_t str_idx, uint8_t * const out_buf, const size_t out_buf_size) {
	usb_setup_req_t req;
	size_t recvd_size = 0;
	
	debug_trace("str_idx = %u", str_idx);

	// Clear the output buffer.
	memset(out_buf, 0, out_buf_size);

	// Copy the template Get Descriptor command for getting a string descriptor
	// and set within it that we want the given string index.
	req = usb_setup_req_get_str_desc;
	req.bIndex = str_idx;
	req.wLength = sizeof(usb_string_descriptor_t);

	const bool result = usb_control_transfer(
		&req,
		ctx->dev_descr.bMaxPacketSize0,
		out_buf_size,
		out_buf,
		&recvd_size
	);

	debug_trace("result = %u, recvd_size = %zu", result, recvd_size);
	debug_trace("descriptor:");
	debug_hex_trace(out_buf, (recvd_size <= out_buf_size ? recvd_size : out_buf_size));

	return result;
}

static bool usb_set_address(usb_context_t * const ctx, const uint8_t addr) {
	usb_setup_req_t req;

	// Sanity check that device address is within allowable range.
	if(addr < USB_DEVICE_ADDR_MIN || addr > USB_DEVICE_ADDR_MAX) return false;

	debug_trace("addr = %u", addr);

	// Copy the template Set Address command and set within it the given
	// address in the wValue field.
	req = usb_setup_req_set_addr;
	req.wValue = addr;

	// There is no data to be sent/received on a Set Address control transfer.
	const bool result = usb_control_transfer(&req, ctx->dev_descr.bMaxPacketSize0, 0, NULL, NULL);

	debug_trace("result = %u", result);

	if(result) {
		// Set the given address as the device address to use by the USB
		// peripheral from this point onward.
		usb_set_self_address(addr);

		ctx->dev_address = addr;

		// Wait a short while for the device to process its new address.
		timestamp_delay_ms(USB_SET_ADDR_DELAY_MS);
	}

	return result;
}

static bool usb_set_configuration(usb_context_t * const ctx, const uint8_t cfg_val) {
	usb_setup_req_t req;

	debug_trace("cfg_val = %u", cfg_val);

	// Copy the template Set Configuration command and set within it the given
	// configuration value in the wValue field.
	req = usb_setup_req_set_cfg;
	req.wValue = cfg_val;

	// There is no data to be sent/received on a Set Configuration control
	// transfer.
	const bool result = usb_control_transfer(&req, ctx->dev_descr.bMaxPacketSize0, 0, NULL, NULL);

	debug_trace("result = %u", result);

	if(result) {
		ctx->dev_current_cfg = cfg_val;
	}

	return result;
}

bool usb_hid_get_report_descriptor(usb_context_t * const ctx, const uint8_t intf_num, uint8_t * const out_buf, const size_t out_buf_size, size_t * const out_size) {
	usb_setup_req_t req;

	debug_trace("intf_num = %u", intf_num);

	// Clear the output buffer and default the output size to zero.
	memset(out_buf, 0, out_buf_size);
	*out_size = 0;
	
	// Copy the template Get Descriptor command for getting an HID Report
	// descriptor and set within it the interface number in the wIndex field.
	req = usb_setup_req_hid_get_report_desc;
	req.wIndex = intf_num;
	req.wLength = out_buf_size;

	const bool result = usb_control_transfer(
		&req,
		ctx->dev_descr.bMaxPacketSize0,
		out_buf_size,
		out_buf,
		out_size
	);

	debug_trace("result = %u, out_size = %zu", result, *out_size);
	debug_trace("descriptor:");
	debug_hex_trace(out_buf, (*out_size <= out_buf_size ? *out_size : out_buf_size));

	return result;
}

bool usb_hid_set_idle(usb_context_t * const ctx, const uint8_t intf_num, const uint8_t duration, const uint8_t report_id) {
	usb_setup_req_t req;

	debug_trace("intf_num = %u, duration = %u, report_id = %u", intf_num, duration, report_id);

	// Copy the template HID Set Idle command and set within it the given
	// duration and report ID in their respective fields, as well as interface
	// number in the wIndex field.
	req = usb_setup_req_hid_set_idle;
	req.bDuration = duration;
	req.bReportId = report_id;
	req.wIndex = intf_num;

	// There is no data to be sent/received on an HID Set Idle control transfer.
	const bool result = usb_control_transfer(&req, ctx->dev_descr.bMaxPacketSize0, 0, NULL, NULL);

	debug_trace("result = %u", result);

	return result;
}

bool usb_hid_set_protocol(usb_context_t * const ctx, const uint8_t intf_num, const usb_hid_protocol_enum_t protocol) {
	usb_setup_req_t req;

	debug_trace("intf_num = %u, protocol = %u", intf_num, protocol);

	// Copy the template HID Set Protocol command and set within it the given
	// protocol in the wValue field and interface number in the wIndex field.
	req = usb_setup_req_hid_set_protocol;
	req.wValue = protocol;
	req.wIndex = intf_num;

	// There is no data to be sent/received on an HID Set Protocol control
	// transfer.
	const bool result = usb_control_transfer(&req, ctx->dev_descr.bMaxPacketSize0, 0, NULL, NULL);

	debug_trace("result = %u", result);

	return result;
}

bool usb_hid_interrupt_in(usb_context_t * const ctx, const uint8_t endp_addr, uint8_t * const out_buf, const size_t out_buf_size, size_t * const out_size) {
	size_t max_packet_size = 0;

	// Clear the output buffer and default the output size to zero.
	memset(out_buf, 0, out_buf_size);
	*out_size = 0;

	// Find the endpoint descriptor that has the given endpoint address and grab
	// its maximum packet size.
	for(size_t i = 0; i < ctx->endp_descr_count; i++) {
		if(ctx->endp_descrs[i].bEndpointAddress == endp_addr) {
			max_packet_size = ctx->endp_descrs[i].wMaxPacketSize;
			break;
		}
	}
	if(max_packet_size == 0) return false;

	debug_trace("endp_addr = %u, max_packet_size = %zu", endp_addr, max_packet_size);

	const bool result = usb_interrupt_in_transfer(endp_addr, max_packet_size, out_buf_size, out_buf, out_size);

	debug_trace("result = %u, out_size = %zu", result, *out_size);
	debug_trace("report:");
	debug_hex_trace(out_buf, (*out_size <= out_buf_size ? *out_size : out_buf_size));

	return result;
}

bool usb_have_device_attached(void) {
	// Return whether a USB device is connected or not.
	return (USBFSH->MIS_ST & USBFS_MIS_ST_UMS_DEV_ATTACH);
}

void usb_set_bus_suspend(const bool suspended) {
	// If we need to put the bus in suspend mode, then we disable the automatic
	// generation of keep-alive/SOF packets by the host; otherwise, enable them.
	if(suspended) {
		USBFSH->HOST_SETUP &= ~USBFS_HOST_SETUP_UH_SOF_EN;
		debug_trace("bus keep-alive/sof suspended");
	} else {
		USBFSH->HOST_SETUP |= USBFS_HOST_SETUP_UH_SOF_EN;
		debug_trace("bus keep-alive/sof active");
	}
}

bool usb_enum_root_device(usb_context_t * const ctx, const usb_device_eval_func_t eval_dev_fn) {
	bool result = false;
	uint32_t retry_count, retry_delay_ms;
	uint8_t tmp_buf[256];
	size_t tmp_buf_offset = 0;
	uint8_t cfg_val = 0;

	// Temporarily disable device detection IRQs while we enumerate, because
	// otherwise we'll get spurious interrupts during bus reset.
	USBFSH->INT_EN &= ~USBFS_INT_EN_UIE_DETECT;

	debug_trace("delay %u ms", USB_ENUM_INITIAL_DELAY_MS);

	// Initial wait to allow any connected device to initialise/stabilise.
	timestamp_delay_ms(USB_ENUM_INITIAL_DELAY_MS);

	// Perform the reset process on the device. We issue a bus reset and then
	// attempt to enable the port. If the device did reconnect already, move on;
	// if it didn't reconnect, we try again (multiplying the delay between every
	// retry), but only so many times before giving up.
	for(retry_count = USB_RESET_RETRIES_MAX, retry_delay_ms = USB_RESET_RETRY_DELAY_MS; retry_count > 0; retry_count--, retry_delay_ms *= USB_RESET_RETRY_DELAY_MULT) {
		debug_trace("bus reset");
		usb_root_hub_device_reset(ctx);

		debug_trace("port enable");
		if(usb_root_hub_port_enable(ctx)) break;

		debug_trace("retry count = %lu, delay = %lu", retry_count, retry_delay_ms);

		timestamp_delay_ms(retry_delay_ms);
	}

	// Nope, device wasn't playing ball, give up.
	if(retry_count == 0) goto usb_enum_root_device_exit;
	
	// Max packet size for control transfers needs to be defaulted according to
	// device speed (low/full). Max packet size for low-speed is 8 bytes,
	// full-speed can be 8, 16, 32, or 64 bytes. When full-speed, just set it to
	// 64, then if we receive less we'll always get at least 8 bytes, enough to
	// have the device's actual bMaxPacketSize0 (at offset 7) when reading the
	// device descriptor first time from address zero.
	switch(ctx->port_speed) {
		case USB_LOW_SPEED: ctx->dev_descr.bMaxPacketSize0 = USB_PACKET_SIZE_MIN; break;
		case USB_FULL_SPEED: ctx->dev_descr.bMaxPacketSize0 = USB_PACKET_SIZE_MAX; break;
	}
	debug_trace("port speed = %u, default max packet size = %u", ctx->port_speed, ctx->dev_descr.bMaxPacketSize0);

	debug_trace("get dev descriptor");
	if(!usb_get_device_descriptor(ctx, tmp_buf, sizeof(tmp_buf))) goto usb_enum_root_device_exit;
	
	// Copy the device descriptor just received into the context so that max
	// packet size described therein is used for subsequent control transfers.
	// Doesn't matter if it's partial, as it gets read again in full later.
	memcpy(&ctx->dev_descr, tmp_buf, sizeof(usb_device_descriptor_t));
	debug_trace("dev max packet size = %u", ctx->dev_descr.bMaxPacketSize0);

	debug_trace("set address (%u)", USB_DEVICE_ADDR);
	if(!usb_set_address(ctx, USB_DEVICE_ADDR)) goto usb_enum_root_device_exit;

	debug_trace("get dev descriptor (redux)");
	if(!usb_get_device_descriptor(ctx, &tmp_buf[tmp_buf_offset], sizeof(tmp_buf) - tmp_buf_offset)) goto usb_enum_root_device_exit;
	tmp_buf_offset += sizeof(usb_device_descriptor_t);

	debug_trace("get cfg 0 descriptor");
	if(!usb_get_config_descriptor(ctx, 0, &tmp_buf[tmp_buf_offset], sizeof(tmp_buf) - tmp_buf_offset)) goto usb_enum_root_device_exit;

	debug_trace("parse descriptors");
	usb_parse_descriptors(ctx, tmp_buf, sizeof(tmp_buf));

	debug_trace("get dev str descriptors");
	if(
		ctx->dev_descr.iManufacturer > 0 &&
		ctx->str_descr_count < USB_MAX_STRINGS &&
		usb_get_string_descriptor(ctx, ctx->dev_descr.iManufacturer, (uint8_t *)&ctx->str_descrs[ctx->str_descr_count], sizeof(usb_string_descriptor_t))
	) {
		ctx->str_descr_count++;
	}
	if(
		ctx->dev_descr.iProduct > 0 &&
		ctx->str_descr_count < USB_MAX_STRINGS &&
		usb_get_string_descriptor(ctx, ctx->dev_descr.iProduct, (uint8_t *)&ctx->str_descrs[ctx->str_descr_count], sizeof(usb_string_descriptor_t))
	) {
		ctx->str_descr_count++;
	}
	if(
		ctx->dev_descr.iSerialNumber > 0 &&
		ctx->str_descr_count < USB_MAX_STRINGS &&
		usb_get_string_descriptor(ctx, ctx->dev_descr.iSerialNumber, (uint8_t *)&ctx->str_descrs[ctx->str_descr_count], sizeof(usb_string_descriptor_t))
	) {
		ctx->str_descr_count++;
	}

	// Call the given evaluation function which shall evaluate the device and
	// decide whether it's supported or not, which is indicated by it returning
	// either a non-zero configuration value, or zero for unsupported.
	debug_trace("evaluate dev");
	cfg_val = eval_dev_fn(ctx);

	// Set the device's configuration to whatever the evaluation function
	// returned. If it was zero, then the device should put itself back into
	// 'address' state (i.e. state before a bus address is assigned).
	debug_trace("set cfg (%u)", cfg_val);
	if(!usb_set_configuration(ctx, cfg_val)) goto usb_enum_root_device_exit;

	result = true;

usb_enum_root_device_exit:

	debug_trace("exit, result = %u", result);

	// Clear any device detect flags and re-enable the detection interrupt.
	USBFSH->INT_FG = USBFS_INT_FG_UIF_DETECT;
	USBFSH->INT_EN |= USBFS_INT_EN_UIE_DETECT;

	return result;
}

void usb_dump_descriptors(const usb_context_t * const ctx) {
	uint8_t tmp_str[50];

	debug_info("---- DEVICE DESCRIPTOR ----");
	debug_info("bLength = %u", ctx->dev_descr.bLength);
	debug_info("bDescriptorType = 0x%02X", ctx->dev_descr.bDescriptorType);
	debug_info("bcdUSB = 0x%04X", ctx->dev_descr.bcdUSB);
	debug_info("bDeviceClass = 0x%02X", ctx->dev_descr.bDeviceClass);
	debug_info("bDeviceSubClass = 0x%02X", ctx->dev_descr.bDeviceSubClass);
	debug_info("bDeviceProtocol = 0x%02X", ctx->dev_descr.bDeviceProtocol);
	debug_info("bMaxPacketSize0 = %u", ctx->dev_descr.bMaxPacketSize0);
	debug_info("idVendor = 0x%04X", ctx->dev_descr.idVendor);
	debug_info("idProduct = 0x%04X", ctx->dev_descr.idProduct);
	debug_info("bcdDevice = 0x%04X", ctx->dev_descr.bcdDevice);
	debug_info("iManufacturer = %u", ctx->dev_descr.iManufacturer);
	debug_info("iProduct = %u", ctx->dev_descr.iProduct);
	debug_info("iSerialNumber = %u", ctx->dev_descr.iSerialNumber);
	debug_info("bNumConfigurations = %u", ctx->dev_descr.bNumConfigurations);

	for(size_t i = 0; i < ctx->cfg_descr_count; i++) {
		debug_info("---- CONFIGURATION DESCRIPTOR %zu ----", i);
		debug_info("bLength = %u", ctx->cfg_descrs[i].bLength);
		debug_info("bDescriptorType = 0x%02X", ctx->cfg_descrs[i].bDescriptorType);
		debug_info("wTotalLength = %u", ctx->cfg_descrs[i].wTotalLength);
		debug_info("bNumInterfaces = %u", ctx->cfg_descrs[i].bNumInterfaces);
		debug_info("bConfigurationValue = %u", ctx->cfg_descrs[i].bConfigurationValue);
		debug_info("iConfiguration = %u", ctx->cfg_descrs[i].iConfiguration);
		debug_info("bmAttributes = 0x%02X", ctx->cfg_descrs[i].bmAttributes);
		debug_info("bMaxPower = %u (%u mA)", ctx->cfg_descrs[i].bMaxPower, ctx->cfg_descrs[i].bMaxPower * 2);
	}

	for(size_t i = 0; i < ctx->intf_descr_count; i++) {
		debug_info("---- INTERFACE DESCRIPTOR %zu ----", i);
		debug_info("bLength = %u", ctx->intf_descrs[i].bLength);
		debug_info("bDescriptorType = 0x%02X", ctx->intf_descrs[i].bDescriptorType);
		debug_info("bInterfaceNumber = %u", ctx->intf_descrs[i].bInterfaceNumber);
		debug_info("bAlternateSetting = %u", ctx->intf_descrs[i].bAlternateSetting);
		debug_info("bNumEndpoints = %u", ctx->intf_descrs[i].bNumEndpoints);
		debug_info("bInterfaceClass = 0x%02X", ctx->intf_descrs[i].bInterfaceClass);
		debug_info("bInterfaceSubClass = 0x%02X", ctx->intf_descrs[i].bInterfaceSubClass);
		debug_info("bInterfaceProtocol = 0x%02X", ctx->intf_descrs[i].bInterfaceProtocol);
		debug_info("iInterface = %u", ctx->intf_descrs[i].iInterface);
	}
	
	for(size_t i = 0; i < ctx->hid_descr_count; i++) {
		debug_info("---- HID DESCRIPTOR %zu ----", i);
		debug_info("bLength = %u", ctx->hid_descrs[i].bLength);
		debug_info("bDescriptorType = 0x%02X", ctx->hid_descrs[i].bDescriptorType);
		debug_info("bcdHID = 0x%04X", ctx->hid_descrs[i].bcdHID);
		debug_info("bCountryCode = %u", ctx->hid_descrs[i].bCountryCode);
		debug_info("bNumDescriptors = %u", ctx->hid_descrs[i].bNumDescriptors);
		debug_info("bReportDescriptorType = 0x%02X", ctx->hid_descrs[i].bReportDescriptorType);
		debug_info("wReportDescriptorLength = %u", ctx->hid_descrs[i].wReportDescriptorLength);
	}

	for(size_t i = 0; i < ctx->endp_descr_count; i++) {
		debug_info("---- ENDPOINT DESCRIPTOR %zu ----", i);
		debug_info("bLength = %u", ctx->endp_descrs[i].bLength);
		debug_info("bDescriptorType = 0x%02X", ctx->endp_descrs[i].bDescriptorType);
		debug_info("bEndpointAddress = 0x%02X", ctx->endp_descrs[i].bEndpointAddress);
		debug_info("bmAttributes = 0x%02X", ctx->endp_descrs[i].bmAttributes);
		debug_info("wMaxPacketSize = %u", ctx->endp_descrs[i].wMaxPacketSize);
		debug_info("bInterval = %u", ctx->endp_descrs[i].bInterval);
	}

	for(size_t i = 0; i < ctx->str_descr_count; i++) {
		usb_convert_str_utf16_to_ascii((uint16_t *)ctx->str_descrs[i].bString, ctx->str_descrs[i].bLength, tmp_str, sizeof(tmp_str));

		debug_info("---- STRING DESCRIPTOR %zu ----", i);
		debug_info("bLength = %u", ctx->str_descrs[i].bLength);
		debug_info("bDescriptorType = 0x%02X", ctx->str_descrs[i].bDescriptorType);
		debug_info("bString = \"%s\" (ASCII)", tmp_str);
	}
}

void usb_context_init(usb_context_t * const ctx) {
	// Clear and initialise the supplied context struct.
	memset(ctx, 0, sizeof(usb_context_t));
	ctx->port_enabled = false;
	ctx->port_connected = false;
	ctx->port_speed = USB_FULL_SPEED;
	ctx->dev_descr.bMaxPacketSize0 = USB_ENDP0_PACKET_SIZE_DEFAULT;
}

void usb_host_init(void) {
	// Ensure AFIO and USBFS peripheral clocks are enabled.
	RCC->APB2PCENR |= RCC_AFIOEN;
	RCC->AHBPCENR |= RCC_USBFS;

	// Ensure USB device-mode pull-ups on PC17/UDP and PC16/UDM are disabled,
	// regardless of what the GPIO settings are for those pins.
	AFIO->CTLR = (AFIO->CTLR & ~(AFIO_CTLR_UDM_PUE | AFIO_CTLR_UDP_PUE)) | AFIO_CTLR_UDM_PUE_DISABLE | AFIO_CTLR_UDP_PUE_DISABLE;

	// Depending on the VDD voltage level, we need to configure whether the USB
	// PHY shall run on VDD directly (when VDD is 3.3V) or shall have its 3.3V
	// regulator enabled (when VDD is 5V).
    switch(pwr_vdd_get_level()) {
		case PWR_VDD_5V0: AFIO->CTLR = (AFIO->CTLR & ~AFIO_CTLR_USB_PHY_V33) | AFIO_CTLR_USB_PHY_V33_LDO; break;
		case PWR_VDD_3V3: AFIO->CTLR = (AFIO->CTLR & ~AFIO_CTLR_USB_PHY_V33) | AFIO_CTLR_USB_PHY_V33_DIRECT; break;
    }

	// Enable USB multiplexing!
	AFIO->CTLR |= AFIO_CTLR_USB_IOEN;

	// Enable USB host mode, ensure the UDP/UDM pull-downs aren't disabled, set
	// full-speed mode, but keep the host port disabled for now. Also set the
	// initial device address to zero (which un-enumerated devices use).
	USBFSH->BASE_CTRL = USBFS_BASE_CTRL_UC_HOST_MODE_HOST;
	USBFSH->HOST_CTRL = 0;
	USBFSH->DEV_ADDR = 0x00;

	// Enable host endpoint transmission and reception. Use a single 64-byte
	// buffer each for transmission and reception.
	USBFSH->HOST_EP_MOD = (
		USBFS_HOST_EP_MOD_UH_EP_TX_EN | USBFS_HOST_EP_MOD_UH_EP_TBUF_MOD_SINGLE |
		USBFS_HOST_EP_MOD_UH_EP_RX_EN | USBFS_HOST_EP_MOD_UH_EP_RBUF_MOD_SINGLE
	);

	// Configure host transmit and receive buffer DMA start addresses.
	USBFSH->HOST_RX_DMA = (uint32_t)usb_rx_buf;
	USBFSH->HOST_TX_DMA = (uint32_t)usb_tx_buf;

	// Reset host endpoint transmit and receive control registers.
	USBFSH->HOST_RX_CTRL = 0;
	USBFSH->HOST_TX_CTRL = 0;

	// Enable some kind of auto-pause when transfer completion interrupt flag is
	// not cleared, and enable DMA for USB.
	USBFSH->BASE_CTRL = USBFS_BASE_CTRL_UC_HOST_MODE_HOST | USBFS_BASE_CTRL_UC_INT_BUSY | USBFS_BASE_CTRL_UC_DMA_EN;

	// Keep automatic generation of keep-alive/SOF packets disabled for now so
	// that the bus is in suspend mode by default until a device is connected.
	USBFSH->HOST_SETUP = 0;
	/*
	// Enable host to automatically generate SOF packets.
	USBFSH->HOST_SETUP = USBFS_HOST_SETUP_UH_SOF_EN;
	*/

	// Clear all the interrupt flags (by writing 1) and enable interrupts for
	// device connect/disconnect.
	USBFSH->INT_FG = 0xFF;
	USBFSH->INT_EN = USBFS_INT_EN_UIE_DETECT;
	
	// Enable the USBFS interrupt in the PFIC.
	interrupt_enable(USBFS_IRQn);
}

size_t usb_convert_str_utf16_to_ascii(const uint16_t * const utf16, const size_t utf16_len, uint8_t * const ascii_out, const size_t ascii_out_size) {
	size_t ascii_len = 0;

	for(size_t i = 0; i < utf16_len && i < ascii_out_size; i++) {
		// Substitute non-ASCII UTF-16 characters with a question mark.
		ascii_out[ascii_len++] = (utf16[i] <= 0x7F ? (uint8_t)utf16[i] : '?');
	}
	
	// Null-terminate the output string. If the output buffer has been filled,
	// overwrite the last character with the null (which reduces the length by
	// one).
	if(ascii_len < ascii_out_size) {
		ascii_out[ascii_len] = '\0';
	} else {
		ascii_out[ascii_out_size - 1] = '\0';
		ascii_len--;
	}

	return ascii_len;
}

ISR(USBFS_IRQHandler) {
	// Did we get a detection (i.e. device connect/disconnect) event?
	if(USBFSH->INT_FG & USBFS_INT_FG_UIF_DETECT) {
		if(USBFSH->MIS_ST & USBFS_MIS_ST_UMS_DEV_ATTACH) {
			event_fifo_push((event_t){
				.type = EVENT_TYPE_USB_DEV_CONNECT,
				.param.word = 0
			});
		} else {
			event_fifo_push((event_t){
				.type = EVENT_TYPE_USB_DEV_DISCONNECT,
				.param.word = 0
			});
		}
	}
	
	// Clear all the interrupt flags (by writing '1' to each).
	USBFSH->INT_FG = 0xFF;
}
