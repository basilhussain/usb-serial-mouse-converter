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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ch32x035.h"
#include "interrupt.h"
#include "debug.h"
#include "tstamp.h"
#include "event.h"
#include "uart_mouse.h"
#include "usb.h"
#include "hid.h"
#if defined(PRINTF_SDI)
#include "sdi.h"
#elif defined(PRINTF_UART)
#include "uart_debug.h"
#endif

/*
COMMON PIN-OUT FOR ALL CH32X035:
PA0 - UART_DSR, output push-pull (active-low)
PA1 - UART_RTS, input with internal pull-up (active-low)
PA2 - UART_TX, output push-pull
PA3 - UART_RX, input with internal pull-up
PA5 - RS232_EN, output push-pull
PB0 - DEBUG_UART_TX, output push-pull
PB3 - CONFIG_0, input with internal pull-up (active-low)
PB4 - CONFIG_1, input with internal pull-up (active-low)
PB5 - CONFIG_2, input with internal pull-up (active-low)
PB6 - LED_ACT_SERIAL, output push-pull (active-low)
PB7 - LED_ACT_USB, output push-pull (active-low)
PB9 - USB_PWR_EN, output push-pull (active-high)
PB10 - USB_PWR_FLT, input with internal pull-up (active-low)
PC16 - USBDM, input with internal pull-down (req'd for USB host)
PC17 - USBDP, input with internal pull-down (req'd for USB host)
PC18 - SWDIO, input floating
PC19 - SWCLK, input floating

PIN-OUT FOR CH32X035C8T6:
PA21 - NRST, input with internal pull-up (active-low)

PIN-OUT FOR CH32X035G8R6:
PC3 - NRST, input with internal pull-up (active-low)
*/

// Serial UART debug output baud rate in bits-per-second.
#define UART_DEBUG_BAUD_RATE 256000

// The serial mouse communications baud rate, in bits-per-second.
#define SERIAL_MOUSE_BAUD_RATE 1200

// Switches 1 & 2 are serial mouse type/mode.
// Switch 3 is plug-and-play ident mode on/off.
// Switch 4 is RS-232 port selection (internal/external), but directly switches
// multiplexer and isn't connected to GPIO.
#define CONFIG_SWITCH_MODE_MASK 0b0011
#define CONFIG_SWITCH_PNP_MASK 0b0100

// How long the total period (i.e. off plus on time) of activity LED blinking
// shall be. A fixed 50% duty cycle is used, so off time is equal to on time.
#define LED_ACT_BLINK_PERIOD_MS 200

// These values match bit pattern for mode DIP switch settings.
typedef enum {
	SERIAL_MOUSE_MODE_MICROSOFT       = 0, // 2 buttons
	SERIAL_MOUSE_MODE_MOUSE_SYSTEMS   = 1, // 3 buttons
	SERIAL_MOUSE_MODE_LOGITECH        = 2, // MouseMan, a.k.a. M+, 3 buttons
	SERIAL_MOUSE_MODE_MICROSOFT_WHEEL = 3  // Intellimouse, 3 buttons + wheel
} serial_mouse_mode_enum_t;

// Maximum possible size in bytes of each kind of serial mouse packet.
typedef enum {
	SERIAL_MOUSE_PACKET_SIZE_MICROSOFT       = 3,
	SERIAL_MOUSE_PACKET_SIZE_MOUSE_SYSTEMS   = 5,
	SERIAL_MOUSE_PACKET_SIZE_LOGITECH        = 4,
	SERIAL_MOUSE_PACKET_SIZE_MICROSOFT_WHEEL = 4
} serial_mouse_packet_size_enum_t;

typedef struct {
	const hid_input_t *hid_input;
	size_t bit_offset;
	union {
		bool bin;
		int32_t num;
	} value;
	union {
		bool bin;
		int32_t num;
	} prev_value;
} usb_hid_mouse_input_t;

typedef struct {
	hid_report_composition_t composition;
	usb_hid_mouse_input_t button_1;
	usb_hid_mouse_input_t button_2;
	usb_hid_mouse_input_t button_3;
	usb_hid_mouse_input_t x_axis;
	usb_hid_mouse_input_t y_axis;
	usb_hid_mouse_input_t wheel;
} usb_hid_mouse_inputs_t;

/******************************************************************************/

// USB HID report descriptor for standard mouse 'boot' protocol reports, as
// defined in HID v1.11 specification Appendix B.2.
static const uint8_t hid_boot_mouse_std_report_descr[50] = {
	0x05, 0x01, // Usage Page (Generic Desktop Ctrls)
	0x09, 0x02, // Usage (Mouse)
	0xA1, 0x01, // Collection (Application)
	0x09, 0x01, //   Usage (Pointer)
	0xA1, 0x00, //   Collection (Physical)
	0x95, 0x03, //     Report Count (3)
	0x75, 0x01, //     Report Size (1)
	0x05, 0x09, //     Usage Page (Button)
	0x19, 0x01, //     Usage Minimum (0x01)
	0x29, 0x03, //     Usage Maximum (0x03)
	0x15, 0x00, //     Logical Minimum (0)
	0x25, 0x01, //     Logical Maximum (1)
	0x81, 0x02, //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x95, 0x01, //     Report Count (1)
	0x75, 0x05, //     Report Size (5)
	0x81, 0x03, //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x75, 0x08, //     Report Size (8)
	0x95, 0x02, //     Report Count (2)
	0x05, 0x01, //     Usage Page (Generic Desktop Ctrls)
	0x09, 0x30, //     Usage (X)
	0x09, 0x31, //     Usage (Y)
	0x15, 0x81, //     Logical Minimum (-127)
	0x25, 0x7F, //     Logical Maximum (127)
	0x81, 0x06, //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
	0xC0,       //   End Collection
	0xC0        // End Collection
};

static usb_context_t usb_ctx;
static uint8_t usb_mouse_intf_num = 0, usb_mouse_endp_addr = 0, usb_mouse_endp_interval_ms = 0;
static usb_hid_mouse_inputs_t usb_mouse_inputs;
static timestamp_interval_call_handle_t mouse_interval_call_handle;
static serial_mouse_mode_enum_t serial_mouse_mode = SERIAL_MOUSE_MODE_MICROSOFT;
static uart_mouse_format_enum_t serial_mouse_data_format = UART_MOUSE_FORMAT_7N1;
static bool serial_mouse_pnp = false;
static uint8_t serial_mouse_interval_ms = 0;
static uint32_t led_act_serial_counter = 0, led_act_usb_counter = 0;
static timestamp_interval_call_handle_t led_act_serial_blink_tick_handle, led_act_usb_blink_tick_handle;

/******************************************************************************/

static void clock_init(void) {
	// Ensure HSI is on, and wait for it to be ready, just in case. (But if it
	// isn't on, how're we even running this code? CH32X035 only has HSI.)
	RCC->CTLR |= RCC_HSION;
	while(!(RCC->CTLR & RCC_HSIRDY));
	
	// Configure for no MCO and HCLK equal to the default SYSCLK divided by 6.
	RCC->CFGR0 = RCC_MCO_NOCLOCK | RCC_HPRE_DIV6;

    // Configure flash for 2 wait states.
    FLASH->ACTLR = FLASH_ACTLR_LATENCY_2;

    // Set HCLK to be equal to SYSCLK undivided.
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_HPRE) | RCC_HPRE_DIV1;
}

static void gpio_init_disable_port_unused(GPIO_TypeDef *port) {
	// Set all GPIOs in the given port to be analog inputs (output buffer is
	// disconnected, input Schmitt triggers disabled, pull-up/down resistors are
	// disabled). This ensures that any that are not externally exposed in the
	// chip package, share a pin with another GPIO, or are exposed but later
	// unused, are 'disabled'.
	port->CFGLR = 0;
	port->CFGHR = 0;
	port->CFGXR = 0;
}

static void gpio_init_lock_port_cfg(GPIO_TypeDef *port) {
	// Perform the special locking sequence on the LCKK key bit (write 1,
	// write 0, write 1, read 0, read 1; last is optional) for all pins on the
	// given port.
	port->LCKR = GPIO_LCKK | (UINT32_MAX & ~GPIO_LCKK);
	port->LCKR = (UINT32_MAX & ~GPIO_LCKK);
	port->LCKR = GPIO_LCKK | (UINT32_MAX & ~GPIO_LCKK);
	port->LCKR;
}

static void gpio_init(void) {
	// Enable peripheral clocks for GPIO ports A, B, and C.
	RCC->APB2PCENR |= RCC_IOPAEN | RCC_IOPBEN | RCC_IOPCEN;

	gpio_init_disable_port_unused(GPIOA);
	gpio_init_disable_port_unused(GPIOB);
	gpio_init_disable_port_unused(GPIOC);
	
	// Configure GPIO port A.
	// Pull-up on PA1 (UART_RTS) & PA3 (UART_RX), default PA0 (UART_DSR) to
	// unasserted (high). Default PA5 (RS232_EN) low.
	GPIOA->BSHR = GPIO_BSHR_BS0 | GPIO_BSHR_BS1 | GPIO_BSHR_BS3;
	GPIOA->BCR = GPIO_BCR_BR5;
	GPIOA->CFGLR = (
		GPIO_CFGLR_MODE0_OUTPUT_2M | GPIO_CFGLR_CNF0_OUTPUT_PUSH_PULL |
		GPIO_CFGLR_MODE1_INPUT | GPIO_CFGLR_CNF1_INPUT_PULL |
		GPIO_CFGLR_MODE2_OUTPUT_2M | GPIO_CFGLR_CNF2_OUTPUT_MUX_PUSH_PULL |
		GPIO_CFGLR_MODE3_INPUT | GPIO_CFGLR_CNF3_INPUT_PULL |
		GPIO_CFGLR_MODE5_OUTPUT_2M | GPIO_CFGLR_CNF5_OUTPUT_PUSH_PULL
	);	
#ifdef CH32X035C8T6
	GPIOA->BSXR = GPIO_BSXR_BS21; // Pull-up on PA21 NRST.
	GPIOA->CFGXR = (
		GPIO_CFGXR_MODE21_INPUT | GPIO_CFGXR_CNF21_INPUT_PULL
	);
#endif

	// Configure GPIO port B.
	// Pull-ups on PB3/4/5 (CONFIG_n) and PB10 (USB_PWR_FLT), and default both
	// PB6/7 (ACT LEDs) to off (high), and PB9 (USB_PWR_EN) low.
	GPIOB->BSHR = GPIO_BSHR_BS3 | GPIO_BSHR_BS4 | GPIO_BSHR_BS5 | GPIO_BSHR_BS6 | GPIO_BSHR_BS7 | GPIO_BSHR_BS10;
	GPIOB->BCR = GPIO_BCR_BR9;
	GPIOB->CFGLR = (
		GPIO_CFGLR_MODE0_OUTPUT_2M | GPIO_CFGLR_CNF0_OUTPUT_MUX_PUSH_PULL |
		GPIO_CFGLR_MODE3_INPUT | GPIO_CFGLR_CNF3_INPUT_PULL |
		GPIO_CFGLR_MODE4_INPUT | GPIO_CFGLR_CNF4_INPUT_PULL |
		GPIO_CFGLR_MODE5_INPUT | GPIO_CFGLR_CNF5_INPUT_PULL |
		GPIO_CFGLR_MODE6_OUTPUT_2M | GPIO_CFGLR_CNF6_OUTPUT_PUSH_PULL |
		GPIO_CFGLR_MODE7_OUTPUT_2M | GPIO_CFGLR_CNF7_OUTPUT_PUSH_PULL
	);
	GPIOB->CFGHR = (
		GPIO_CFGHR_MODE9_OUTPUT_2M | GPIO_CFGHR_CNF9_OUTPUT_PUSH_PULL |
		GPIO_CFGHR_MODE10_INPUT | GPIO_CFGHR_CNF10_INPUT_PULL
	);

	// Configure GPIO port C.
#ifdef CH32X035G8R6
	GPIOC->BSHR = GPIO_BSHR_BS3; // Pull-up on PC3 NRST.
	GPIOC->CFGLR = (
		GPIO_CFGLR_MODE3_INPUT | GPIO_CFGLR_CNF3_INPUT_PULL
	);
#endif
	GPIOC->BCR = GPIO_BCR_BR16 | GPIO_BCR_BR17; // Pull-down on PC16 & PC17.
	GPIOC->CFGXR = (
		GPIO_CFGXR_MODE16_INPUT | GPIO_CFGXR_CNF16_INPUT_PULL |
		GPIO_CFGXR_MODE17_INPUT | GPIO_CFGXR_CNF17_INPUT_PULL |
		GPIO_CFGXR_MODE18_INPUT | GPIO_CFGXR_CNF18_INPUT_FLOAT |
		GPIO_CFGXR_MODE19_INPUT | GPIO_CFGXR_CNF19_INPUT_FLOAT
	);
	
	gpio_init_lock_port_cfg(GPIOA);
	gpio_init_lock_port_cfg(GPIOB);
	gpio_init_lock_port_cfg(GPIOC);

	// Configure for both falling-edge and rising-edge interrupts on PA1
	// (UART_RTS) with EXTI channel 1 to catch when RTS is asserted (active-low)
	// and de-asserted. But don't enable the interrupt right now.
	AFIO->EXTICR[0] = (AFIO->EXTICR[0] & ~AFIO_EXTICR1_EXTI1) | AFIO_EXTICR1_EXTI1_PA;
	EXTI->FTENR |= EXTI_FTENR_TR1;
	EXTI->RTENR |= EXTI_RTENR_TR1;
	EXTI->INTENR &= ~EXTI_INTENR_MR1;
	
	// Configure for falling-edge interrupt on PB10 (USB_PWR_FLT) with EXTI
	// channel 10 to catch fault condition on the USB power switch (active-low).
	// But don't enable the interrupt right now.
	AFIO->EXTICR[0] = (AFIO->EXTICR[0] & ~AFIO_EXTICR1_EXTI10) | AFIO_EXTICR1_EXTI10_PB;
	EXTI->FTENR |= EXTI_FTENR_TR10;
	EXTI->INTENR &= ~EXTI_INTENR_MR10;

	// Enable the EXTI interrupts in the PFIC.
	interrupt_enable(EXTI7_0_IRQn);
	interrupt_enable(EXTI15_8_IRQn);
}

static void led_act_serial_enable(const bool enabled) {
	// Enabled or disable the blink counter tick callback as appropriate, and
	// also reset the counter to zero.
	timestamp_interval_call_set_enabled(led_act_serial_blink_tick_handle, enabled);
	led_act_serial_counter = 0;
	
	// Turn on/off the LED immediately.
	if(enabled) {
		GPIOB->BCR = GPIO_BCR_BR6; // Turn on active-low LED.
	} else {
		GPIOB->BSHR = GPIO_BSHR_BS6; // Turn off active-low LED.
	}
}

static void led_act_usb_enable(const bool enabled) {
	// Enabled or disable the blink counter tick callback as appropriate, and
	// also reset the counter to zero.
	timestamp_interval_call_set_enabled(led_act_usb_blink_tick_handle, enabled);
	led_act_usb_counter = 0;
	
	// Turn on/off the LED immediately.
	if(enabled) {
		GPIOB->BCR = GPIO_BCR_BR7; // Turn on active-low LED.
	} else {
		GPIOB->BSHR = GPIO_BSHR_BS7; // Turn off active-low LED.
	}
}

static void led_act_serial_blink(void) {
	if(led_act_serial_counter == 0) led_act_serial_counter = LED_ACT_BLINK_PERIOD_MS;
}

static void led_act_usb_blink(void) {
	if(led_act_usb_counter == 0) led_act_usb_counter = LED_ACT_BLINK_PERIOD_MS;
}

static void led_act_serial_blink_tick(void) {
	if(led_act_serial_counter > 0) led_act_serial_counter--;
	
	if(led_act_serial_counter > (LED_ACT_BLINK_PERIOD_MS / 2)) {
		GPIOB->BSHR = GPIO_BSHR_BS6; // Turn off active-low LED.
	} else {
		GPIOB->BCR = GPIO_BCR_BR6; // Turn on active-low LED.
	}
}

static void led_act_usb_blink_tick(void) {
	if(led_act_usb_counter > 0) led_act_usb_counter--;
	
	if(led_act_usb_counter > (LED_ACT_BLINK_PERIOD_MS / 2)) {
		GPIOB->BSHR = GPIO_BSHR_BS7; // Turn off active-low LED.
	} else {
		GPIOB->BCR = GPIO_BCR_BR7; // Turn on active-low LED.
	}
}

/*
static void debug_tick(void) {
	timestamp_t now;
	timestamp_now(&now);
	debug_info("ticks = %lu", now.ticks);
}
*/

static const char * config_serial_mode_name(const serial_mouse_mode_enum_t mode) {
	static const char * mode_names[] = {
		[SERIAL_MOUSE_MODE_MICROSOFT] = "Microsoft",
		[SERIAL_MOUSE_MODE_MOUSE_SYSTEMS] = "Mouse Systems",
		[SERIAL_MOUSE_MODE_LOGITECH] = "Logitech",
		[SERIAL_MOUSE_MODE_MICROSOFT_WHEEL] = "Microsoft Wheel"
	};
	
	return (mode < (sizeof(mode_names) / sizeof(mode_names[0])) ? mode_names[mode] : "[unknown]");
}

static const char * config_serial_format_name(const uart_mouse_format_enum_t format) {
	static const char * format_names[] = {
		[UART_MOUSE_FORMAT_7N1] = "7N1",
		[UART_MOUSE_FORMAT_8N1] = "8N1"
	};
	
	return (format < (sizeof(format_names) / sizeof(format_names[0])) ? format_names[format] : "[unknown]");
}

static void config_switch_read(serial_mouse_mode_enum_t * const serial_mode, uart_mouse_format_enum_t * const serial_format, bool * const pnp) {
	// Read the switch values. Switches are active-low (off = high, on = low),
	// so invert the values.
	const uint32_t switch_vals = (~GPIOB->INDR & (GPIO_INDR_IDR3 | GPIO_INDR_IDR4 | GPIO_INDR_IDR5)) >> __builtin_ctz(GPIO_INDR_IDR3);
	
	debug_trace("GPIOB = 0x%lX, switch_vals = 0x%lX", GPIOB->INDR, switch_vals);
	
	// Switches 1 & 2 are serial mouse type/mode. Mode enum values correspond
	// directly to switch value bits.
	if(serial_mode != NULL && serial_format != NULL) {
		*serial_mode = (serial_mouse_mode_enum_t)(switch_vals & CONFIG_SWITCH_MODE_MASK);
		
		// Serial format differs depending on mouse mode; select accordingly.
		switch(*serial_mode) {
			case SERIAL_MOUSE_MODE_MICROSOFT:
			case SERIAL_MOUSE_MODE_LOGITECH:
			case SERIAL_MOUSE_MODE_MICROSOFT_WHEEL:
				*serial_format = UART_MOUSE_FORMAT_7N1;
				break;
			case SERIAL_MOUSE_MODE_MOUSE_SYSTEMS:
				*serial_format = UART_MOUSE_FORMAT_8N1;
				break;
		}
		
		debug_info("serial_mode = %s (%lu), serial_format = %s (%lu)", config_serial_mode_name(*serial_mode), *serial_mode, config_serial_format_name(*serial_format), *serial_format);
	}
	
	// Switch 3 is whether PnP data should be included in ident.
	if(pnp != NULL) {
		*pnp = (bool)(switch_vals & CONFIG_SWITCH_PNP_MASK);
		
		debug_info("pnp = %u", *pnp);
	}
}

static inline int32_t serial_mouse_clamp_value(const int32_t value, const int32_t min, const int32_t max) {
	if(value > max) return max;
	if(value < min) return min;
	return value;
}

static size_t serial_mouse_packet_format(const usb_hid_mouse_inputs_t * const inputs, const serial_mouse_mode_enum_t serial_mode, uint8_t * const out_buf, const size_t out_buf_size) {
	size_t out_len = 0;

	if(inputs == NULL || out_buf == NULL) return 0;

	// Clamp X & Y values to range of signed 8-bit values, and wheel to signed
	// 4-bit. Because when otherwise simply truncating to 8 bits, for example,
	// a value of >127 becomes a negative value.
	const int32_t clamped_x_value = serial_mouse_clamp_value(inputs->x_axis.value.num, -128, 127);
	const int32_t clamped_y_value = serial_mouse_clamp_value(inputs->y_axis.value.num, -128, 127);
	const int32_t clamped_wheel_value = serial_mouse_clamp_value(inputs->wheel.value.num, -8, 7);

	switch(serial_mode) {
		case SERIAL_MOUSE_MODE_MICROSOFT:
			if(out_buf_size < SERIAL_MOUSE_PACKET_SIZE_MICROSOFT) break;
			out_buf[out_len++] = 0x40 | ((uint8_t)inputs->button_1.value.bin << 5) | ((uint8_t)inputs->button_2.value.bin << 4) | (((uint8_t)clamped_y_value & 0xC0) >> 4) | (((uint8_t)clamped_x_value & 0xC0) >> 6);
			out_buf[out_len++] = ((uint8_t)clamped_x_value & 0x3F);
			out_buf[out_len++] = ((uint8_t)clamped_y_value & 0x3F);
			break;
		case SERIAL_MOUSE_MODE_MOUSE_SYSTEMS:
			// For the Y axis, we need to sign-invert, because positive values
			// indicate motion upward rather than downward. Fourth and fifth
			// bytes are supposed to be X/Y movement since 3rd and 4th bytes,
			// but we don't have that, so set to zero. Button values have
			// inverted polarity: '0' when pressed.
			if(out_buf_size < SERIAL_MOUSE_PACKET_SIZE_MOUSE_SYSTEMS) break;
			out_buf[out_len++] = 0x80 | ((uint8_t)!inputs->button_1.value.bin << 2) | ((uint8_t)!inputs->button_3.value.bin << 1) | ((uint8_t)!inputs->button_2.value.bin << 0);
			out_buf[out_len++] = (uint8_t)clamped_x_value;
			out_buf[out_len++] = (uint8_t)-clamped_y_value;
			out_buf[out_len++] = 0;
			out_buf[out_len++] = 0;
			break;
		case SERIAL_MOUSE_MODE_LOGITECH:
			// Same as the Microsoft protocol, but when middle button has been
			// either pressed or released, a fourth byte is sent containing only
			// that button's state.
			// TODO: also 4th byte *while* middle button is pressed? - THIS DOESN'T MATTER, AS USB MOUSE ONLY REPORTS ON MID STATE CHANGE
			// TODO: add device type value to 4th byte; bits 0-4, 1 = mouse. - DONE
			if(out_buf_size < SERIAL_MOUSE_PACKET_SIZE_LOGITECH) break;
			out_buf[out_len++] = 0x40 | ((uint8_t)inputs->button_1.value.bin << 5) | ((uint8_t)inputs->button_2.value.bin << 4) | (((uint8_t)clamped_y_value & 0xC0) >> 4) | (((uint8_t)clamped_x_value & 0xC0) >> 6);
			out_buf[out_len++] = ((uint8_t)clamped_x_value & 0x3F);
			out_buf[out_len++] = ((uint8_t)clamped_y_value & 0x3F);
			if(inputs->button_3.value.bin || inputs->button_3.value.bin != inputs->button_3.prev_value.bin) out_buf[out_len++] = ((uint8_t)inputs->button_3.value.bin << 5) | 0x01;
			// if(inputs->button_3.value.bin != inputs->button_3.prev_value.bin) out_buf[out_len++] = ((uint8_t)inputs->button_3.value.bin << 5);
			break;
		case SERIAL_MOUSE_MODE_MICROSOFT_WHEEL:
			// Same as other Microsoft protocol, but with the addition of a 4th
			// byte providing middle button and wheel movement. USB HID wheel
			// values are positive for forward rotation, but this protocol needs
			// backward rotation to be positive values, so must invert value.
			if(out_buf_size < SERIAL_MOUSE_PACKET_SIZE_MICROSOFT_WHEEL) break;
			out_buf[out_len++] = 0x40 | ((uint8_t)inputs->button_1.value.bin << 5) | ((uint8_t)inputs->button_2.value.bin << 4) | (((uint8_t)clamped_y_value & 0xC0) >> 4) | (((uint8_t)clamped_x_value & 0xC0) >> 6);
			out_buf[out_len++] = ((uint8_t)clamped_x_value & 0x3F);
			out_buf[out_len++] = ((uint8_t)clamped_y_value & 0x3F);
			out_buf[out_len++] = ((uint8_t)inputs->button_3.value.bin << 4) | ((uint8_t)-clamped_wheel_value & 0x0F);
			break;
	}

	return out_len;
}

static size_t serial_mouse_packet_size(const serial_mouse_mode_enum_t serial_mode) {
	switch(serial_mode) {
		case SERIAL_MOUSE_MODE_MICROSOFT: return SERIAL_MOUSE_PACKET_SIZE_MICROSOFT;
		case SERIAL_MOUSE_MODE_MOUSE_SYSTEMS: return SERIAL_MOUSE_PACKET_SIZE_MOUSE_SYSTEMS;
		case SERIAL_MOUSE_MODE_LOGITECH: return SERIAL_MOUSE_PACKET_SIZE_LOGITECH;
		case SERIAL_MOUSE_MODE_MICROSOFT_WHEEL: return SERIAL_MOUSE_PACKET_SIZE_MICROSOFT_WHEEL;
	}

	return 0;
}

static size_t serial_mouse_ident(const serial_mouse_mode_enum_t serial_mode, const bool pnp, uint8_t * const out_buf, const size_t out_buf_size) {
	// See "Plug and Play External COM Device Specification, Rev 1.00" section
	// 22. Generic ID values from Windows PnP Device ID list ("devids.txt").
	// All mouse-compatible devices must restrict to a 6-bit character set for
	// all PnP data fields. In that case, all characters values are offset by
	// subtracting 0x20. For example, 0x28 => 0x08.

	static const uint8_t ident_microsoft[] = {
		0x4D // "M"
	};
	static const uint8_t ident_microsoft_pnp[] = {
		// No PnP ID
		/*
		0x08,                   // Begin PnP
		0x01, 0x24,             // PnP Rev, 1.0
		0x30, 0x2E, 0x30,       // EISA Vendor ID, "PNP" (Generic)
		0x10, 0x26, 0x10, 0x11, // Product ID, "0F01" (Microsoft Serial Mouse)
		0x09                    // End PnP
		*/
	};
	/*
	// These don't work, Win95 doesn't automatically recognise mouse. Possibly
	// because Mouse Systems mouse driver is only provided as an extra on the
	// CD-ROM, so Windows doesn't know it has it available.
	static const uint8_t ident_mouse_systems[] = {
		// None
	};
	static const uint8_t ident_mouse_systems_pnp[] = {
		0x08,                   // Begin PnP
		0x01, 0x24,             // PnP Rev, 1.0
		0x30, 0x2E, 0x30,       // EISA Vendor ID, "PNP" (Generic)
		0x10, 0x26, 0x10, 0x14, // Product ID, "0F04" (Mouse Systems Mouse)
		0x09                    // End PnP
	};
	*/
	static const uint8_t ident_logitech[] = {
		0x4D, 0x33 // "M3"
	};
	static const uint8_t ident_logitech_pnp[] = {
		/*
		// TEST
		0x08,                                     // Begin PnP
		0x01, 0x24,                               // PnP Rev, 1.0
		0x28, 0x34, 0x2B,                         // EISA Vendor ID, "HTK" (Holtek)
		0x10, 0x10, 0x10, 0x11,                   // Product ID, "0001" (HT82M13 M+ PnP Mouse Controller)
		0x3C,                                     // Extend Marker
												  // Serial Number (None)
		0x3C,                                     // Extend Marker
		0x2D, 0x2F, 0x35, 0x33, 0x25,             // Class Name, "MOUSE"
		0x3C,                                     // Extend Marker
		0x30, 0x2E, 0x30, 0x10, 0x26, 0x10, 0x23, // Compatible Device IDs, "PNP0F0C" (Microsoft-compatible Serial Mouse)
		0x19, 0x12,                               // Checksum (0x92)
		0x09                                      // End PnP
		*/
		
		0x08,                                     // Begin PnP
		0x01, 0x24,                               // PnP Rev, 1.0
		0x2C, 0x27, 0x29,                         // EISA Vendor ID, "LGI" (Logitech)
		0x18, 0x10, 0x10, 0x11,                   // Product ID, "8001" (Logitech First/Pilot Mouse Serial M34/M35/C43)
		0x3C,                                     // Extend Marker
												  // Serial Number (None)
		0x3C,                                     // Extend Marker
		0x2D, 0x2F, 0x35, 0x33, 0x25,             // Class Name, "MOUSE"
		0x3C,                                     // Extend Marker
		0x30, 0x2E, 0x30, 0x10, 0x26, 0x10, 0x21, // Compatible Device IDs, "PNP0F0A" (Microsoft Plug and Play Mouse)
		0x18, 0x24,                               // Checksum (0x8D)
		0x09                                      // End PnP
		
		/*
		0x08,                                           // Begin PnP
		0x01, 0x24,                                     // PnP Rev, 1.0
		0x2C, 0x27, 0x29,                               // EISA Vendor ID, "LGI" (Logitech)
		0x18, 0x10, 0x10, 0x23,                         // Product ID, "800C" (Logitech MouseMan Serial)
		0x3C,                                           // Extend Marker
		                                                // Serial Number (None)
		0x3C,                                           // Extend Marker
		0x2D, 0x2F, 0x35, 0x33, 0x25,                   // Class Name, "MOUSE"
		0x3C,                                           // Extend Marker
		0x30, 0x2E, 0x30, 0x10, 0x26, 0x10, 0x21,       // Compatible Device IDs, "PNP0F0A" (Microsoft Plug and Play Mouse)
		0x3C,                                           // Extend Marker
		0x2C, 0x4F, 0x47, 0x49, 0x54, 0x45, 0x43, 0x48, // User Name, "Logitech MouseMan Serial"
		0x00, 0x2D, 0x4F, 0x55, 0x53, 0x45, 0x2D, 0x41,
		0x4E, 0x00, 0x33, 0x45, 0x52, 0x49, 0x41, 0x4C,
		0x23, 0x26,                                     // Checksum (0xCF)
		0x09                                            // End PnP
		*/
	};
	static const uint8_t ident_microsoft_wheel[] = {
		0x4D, 0x5A, 0x40, 0x00, 0x00, 0x00 // "MZ@" + 3x null
	};
	static const uint8_t ident_microsoft_wheel_pnp[] = {
		/*
		// TEST - from Holtek HT82M33A datasheet, pg. 5
		0x08,
		0x01, 0x24,
		0x28, 0x34, 0x2B,
		0x10, 0x10, 0x10, 0x11,
		0x3C,
		0x3C,
		0x2D, 0x2F, 0x35, 0x33, 0x25,
		0x3C,
		0x30, 0x2E, 0x30, 0x10, 0x26, 0x10, 0x21,
		0x19, 0x10,
		0x09
		*/
		/*
		// TEST - from https://github.com/Aviancer/amouse/blob/main/shared/mouse.c#L42
		0x08,                                           // Begin PnP
		0x01, 0x24,                                     // PnP Rev, 1.0
		0x2D, 0x33, 0x28,                               // EISA Vendor ID, "MSH" (Microsoft)
		0x10, 0x10, 0x10, 0x11,                         // Product ID, "0001"
		0x3C,                                           // Extend Marker
		0x21, 0x36, 0x29, 0x21, 0x2E, 0x23, 0x25, 0x32, // Serial Number, "AVIANCER"
		0x3C,                                           // Extend Marker
		0x2D, 0x2F, 0x35, 0x33, 0x25,                   // Class Name, "MOUSE"
		0x3C,                                           // Extend Marker
		0x30, 0x2E, 0x30, 0x10, 0x26, 0x10, 0x21,       // Compatible Device IDs, "PNP0F0A" (Microsoft Plug and Play Mouse)
		0x3C,                                           // Extend Marker
		0x2D, 0x29, 0x23, 0x32, 0x2F, 0x33, 0x2F, 0x26, // User Name, "MICROSOFT MOUSE WITH WHEEL"
		0x34, 0x00, 0x2D, 0x2F, 0x35, 0x33, 0x25, 0x00,
		0x37, 0x29, 0x34, 0x28, 0x00, 0x37, 0x28, 0x25,
		0x25, 0x2C,
		0x12, 0x16,                                     // Checksum (0x26)
		0x09                                            // End PnP
		*/
		0x08,                                           // Begin PnP
		0x01, 0x24,                                     // PnP Rev, 1.0
		0x2D, 0x33, 0x28,                               // EISA Vendor ID, "MSH" (Microsoft)
		0x10, 0x10, 0x10, 0x11,                         // Product ID, "0001"
		0x3C,                                           // Extend Marker
		0x10, 0x10, 0x12, 0x13, 0x16, 0x14, 0x16, 0x19, // Serial Number, "00236469"
		0x3C,                                           // Extend Marker
		0x2D, 0x2F, 0x35, 0x33, 0x25,                   // Class Name, "MOUSE"
		0x3C,                                           // Extend Marker
		0x30, 0x2E, 0x30, 0x10, 0x26, 0x10, 0x21,       // Compatible Device IDs, "PNP0F0A" (Microsoft Plug and Play Mouse)
		0x3C,                                           // Extend Marker
		0x2D, 0x29, 0x23, 0x32, 0x2F, 0x33, 0x2F, 0x26, // User Name, "MICROSOFT INTELLIMOUSE - SERIAL VERSION"
		0x34, 0x00, 0x29, 0x2E, 0x34, 0x25, 0x2C, 0x2C,
		0x29, 0x2D, 0x2F, 0x35, 0x33, 0x25, 0x00, 0x0D,
		0x00, 0x33, 0x25, 0x32, 0x29, 0x21, 0x2C, 0x00,
		0x36, 0x25, 0x32, 0x33, 0x29, 0x2F, 0x2E,
		0x16, 0x25,                                     // Checksum (0x6E)
		0x09                                            // End PnP
	};
	const uint8_t *ident = NULL, *ident_pnp = NULL;
	size_t ident_len = SIZE_MAX, ident_pnp_len = SIZE_MAX, out_len = 0;

	if(out_buf == NULL || out_buf_size == 0) return 0;

	switch(serial_mode) {
		case SERIAL_MOUSE_MODE_MICROSOFT:
			ident = ident_microsoft;
			ident_len = sizeof(ident_microsoft);
			ident_pnp = ident_microsoft_pnp;
			ident_pnp_len = sizeof(ident_microsoft_pnp);
			break;
		case SERIAL_MOUSE_MODE_MOUSE_SYSTEMS:
			// No ident for Mouse Systems mouse.
			/*
			ident = ident_mouse_systems;
			ident_len = sizeof(ident_mouse_systems);
			ident_pnp = ident_mouse_systems_pnp;
			ident_pnp_len = sizeof(ident_mouse_systems_pnp);
			*/
			break;
		case SERIAL_MOUSE_MODE_LOGITECH:
			ident = ident_logitech;
			ident_len = sizeof(ident_logitech);
			ident_pnp = ident_logitech_pnp;
			ident_pnp_len = sizeof(ident_logitech_pnp);
			break;
		case SERIAL_MOUSE_MODE_MICROSOFT_WHEEL:
			ident = ident_microsoft_wheel;
			ident_len = sizeof(ident_microsoft_wheel);
			ident_pnp = ident_microsoft_wheel_pnp;
			ident_pnp_len = sizeof(ident_microsoft_wheel_pnp);
			break;
	}

	// Check whether the output buffer is large enough. If so, copy the ident
	// data to it, and optionally plug-and-play data too if required.
	if(out_buf_size >= (ident_len + (pnp ? ident_pnp_len : 0)) && ident != NULL && ident_pnp != NULL) {
		memcpy(out_buf, ident, ident_len);
		out_len += ident_len;
		if(pnp) {
			memcpy(out_buf + ident_len, ident_pnp, ident_pnp_len);
			out_len += ident_pnp_len;
		}
	}

	return out_len;
}

static uint32_t serial_mouse_transmit_time_ms(const uint32_t baud_rate, const uart_mouse_format_enum_t format, const uint8_t byte_count) {
	uint32_t frame_bits_mult;
	
	// When we're doing 7N1, which is actually 7N1.5 (which is actually 8N0.5),
	// we have 9.5 bits per frame (1 start, 7 data, 1.5 stop). Otherwise, for
	// 8N1, we have 10 bits per frame (1 start, 8 data, 1 stop).
	switch(format) {
		default:
		case UART_MOUSE_FORMAT_7N1: frame_bits_mult = 95; break;
		case UART_MOUSE_FORMAT_8N1: frame_bits_mult = 100; break;
	}
	
	// Calculate the total number of bits for the given byte count. Integer
	// division truncates, so round up if there is a fractional remainder of
	// total bits (i.e. bits-per-frame is non-integer).
	const uint32_t total_bits = (((uint32_t)byte_count * frame_bits_mult) / 10) + (((uint32_t)byte_count * frame_bits_mult) % 10 != 0 ? 1 : 0);

	// Calculate the total time in milliseconds to transmit the total number of
	// bits. Integer division truncates, so round up if there is a fractional
	// remainder of a millisecond.
	return ((total_bits * 1000) / baud_rate) + ((total_bits * 1000) % baud_rate != 0 ? 1 : 0);
}

static void serial_mouse_transmit_data(const uint8_t *data, const size_t data_len) {
	if(data != NULL) {
		for(size_t i = 0; i < data_len; i++) {
			uart_mouse_putchar(data[i]);
		}
	}
}

static size_t serial_mouse_receive_data(uint8_t *data, const size_t data_size) {
	size_t received = 0;
	
	if(data != NULL && uart_mouse_receive_available() >= data_size) {
		for(size_t i = 0; i < data_size; i++) {
			const int c = uart_mouse_getchar();
			
			// Mouse UART is in non-blocking mode, so if for some reason the RX
			// FIFO is empty (but it shouldn't be), EOF will be returned, so
			// bail out if that happens.
			if(c == EOF) break;
			
			data[i] = (uint8_t)c;			
			received++;
		}
	}
	
	return received;
}

static void serial_mouse_rs232_trx_enable(const bool enable) {
	// Enable or disable the RS-232 transceiver depending on given argument
	// value. When enabling from a previously shut-down state, add a short delay
	// to allow for V+/V- charge pump outputs to get to full voltage.
	if(enable) {
		GPIOA->BSHR = GPIO_BSHR_BS5;
		timestamp_delay_ms(1);
	} else {
		GPIOA->BCR = GPIO_BCR_BR5;
	}
}

static void serial_mouse_rts_interrupt_enable(const bool enable) {
	// Enable or disable the edge-detect interrupt on UART RTS depending on
	// given argument value.
	if(enable) {
		EXTI->INTENR |= EXTI_INTENR_MR1;
	} else {
		EXTI->INTENR &= ~EXTI_INTENR_MR1;
	}
}

static void serial_mouse_dsr_assert(const bool assert) {
	// Assert (set low) or de-assert (set high) the UART DSR line, depending on
	// given argument value.
	if(assert) {
		GPIOA->BCR = GPIO_BCR_BR0;
	} else {
		GPIOA->BSHR = GPIO_BSHR_BS0;
	}
}

static void serial_mouse_process_logitech_command(void) {
	// Standard configuration command response format:
	//    Bit | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
	// -------|---|---|---|---|---|---|---|
	// Byte 0 | 1 | V5| V4| V3| V2| V1| V0|
	// Byte 1 | 0 |RBR|BRC|EXT|DT2|DT1|DT0|
	// Byte 2 | 0 | B2| B1| B0| P2| P1| P0|
	// Byte 3 | 0 |RES|RES|RES|RES|RES|RES|
	// Fields are as follows:
	// - V5..V0 = Firmware Version Index (000001)
	// - RBR = Report Bit Rate before receiving a command (0 = was in 1200bps)
	// - BRC = Bit Rate Capabilities (0 = only 1200bps)
	// - EXT = Extension to Command set (0 = no extension)
	// - DT2..DT0 = Device Type (001 = mouse)
	// - B2..B0 = Number of buttons (011 = 3 buttons)
	// - P2..P0 = Protocol in use (010 = M+ protocol)
	// - RES = Reserved for future use (0)
	static const uint8_t std_config[] = { 0x41, 0x01, 0x1A, 0x00 };
	// Copyright message. May be up to 127 bytes of ASCII, pre-fixed by CRLF and
	// null-terminated.
	static const uint8_t copyright_str[] = "\r\nUSB to Serial Mouse Converter, (c) 2026 Basil Hussain";
	// Dummy diagnostics test results with all set to 'pass' (1).
	static const uint8_t diagnostics[] = { 0xBF, 0x3F };
	uint8_t cmd_buf[4];
	
	// If we have at least 2 characters of data received on the mouse serial
	// UART then read 2 into a buffer.
	const size_t cmd_buf_len = serial_mouse_receive_data(cmd_buf, 2);
	
	if(cmd_buf_len == 2) {
		debug_info("received:");
		debug_hex_info(cmd_buf, cmd_buf_len);
		
		// Is the first character an asterisk ('*')?
		if(cmd_buf[0] == 0x2A) {
			switch(cmd_buf[1]) {
				case 0x05: // <enq> - Diagnostics
					serial_mouse_transmit_data(diagnostics, sizeof(diagnostics) / sizeof(diagnostics[0]));
					debug_info("diagnostics command ('%c'); response:", cmd_buf[1]);
					debug_hex_info(diagnostics, sizeof(diagnostics) / sizeof(diagnostics[0]));
					break;
				/*
				case 0x23: // '#' - Send device identification
					// TODO: seems to require a 5-byte response:
					// byte 0: "M"
					// byte 1: <ascii digit 0-7> ('3', same as ident? i.e. no. of btns)
					// byte 2: some bit flags in low nibble (some kind of device capabilities/type/proto?)
					// byte 3: apparently unused
					// byte 4: apparently unused
					// It may be we don't need to implement this if we implement '?' command.
					debug_info("device identification");
					break;
				*/
				case 0x3F: // '?' - Send standard configuration
					serial_mouse_transmit_data(std_config, sizeof(std_config) / sizeof(std_config[0]));
					debug_info("standard config command ('%c'); response:", cmd_buf[1]);
					debug_hex_info(std_config, sizeof(std_config) / sizeof(std_config[0]));
					break;
				case 0x58: // 'X' - Select M+ protocol
					debug_info("select M+ protocol command ('%c'); ignoring", cmd_buf[1]);
					break;
				case 0x63: // 'c' - Send copyright and version number in ASCII
					serial_mouse_transmit_data(copyright_str, sizeof(copyright_str) / sizeof(copyright_str[0]));
					debug_info("copyright command ('%c'); response:", cmd_buf[1]);
					debug_hex_info(copyright_str, sizeof(copyright_str) / sizeof(copyright_str[0]));
					break;
				case 0x6E: // 'n' - Set to 1200 baud
					debug_info("set to 1200 baud command ('%c'); ignoring", cmd_buf[1]);
					break;
				default:
					debug_warn("unhandled command '%c' (0x%02X)", cmd_buf[1], cmd_buf[1]);
					break;
			}
		} else {
			debug_warn("non-command, first char not '*'");
		}
	}
}

static void usb_power_enable(const bool enable) {
	if(enable) {
		// Assert (high) the enable signal for the USB port power switch and
		// enable the fault condition interrupt.
		EXTI->INTENR |= EXTI_INTENR_MR10;
		GPIOB->BSHR = GPIO_BSHR_BS9;
		
		// Wait a short period for USB load switch soft-start.
		timestamp_delay_ms(10);
	} else {
		// De-assert (low) the enable signal for the USB port power switch and
		// disable the fault condition interrupt.
		GPIOB->BCR = GPIO_BCR_BR9;
		EXTI->INTENR &= ~EXTI_INTENR_MR10;
	}
}

static uint8_t usb_eval_device(const usb_context_t * const ctx) {
	// Device must be at least USB specification 1.0-compliant.
	if(ctx->dev_descr.bcdUSB < 0x0100) {
		debug_info("invalid USB version");
		return 0;
	}

	// Ensure that the device class and sub-class are specified as being defined
	// by interface, and that it doesn't use a class-specific protocol on a
	// whole-device basis.
	if(
		ctx->dev_descr.bDeviceClass != 0 ||
		ctx->dev_descr.bDeviceSubClass != 0 ||
		ctx->dev_descr.bDeviceProtocol != 0
	) {
		debug_info("device class, sub-class, or protocol not zero");
		return 0;
	}

	// We only want to deal with devices with a single configuration.
	if(ctx->dev_descr.bNumConfigurations != 1 || ctx->dev_tree.cfg_node_count != 1) {
		debug_info("invalid number of configurations; only 1 supported");
		return 0;
	}

	// TODO: maybe have a blacklist of devices based on vendor and product IDs?

	// Get a pointer to the device's 1st configuration tree node and descriptor.
	const usb_config_node_t *cfg_node = &ctx->cfg_trees[ctx->dev_tree.cfg_node_indices[0]];
	const usb_config_descriptor_t *cfg_descr = &ctx->cfg_descrs[cfg_node->cfg_descr_index];

	// Reject if the configuration reports that the device is bus-powered (i.e.
	// not self-powered) and wants to draw in excess of 500mA.
	if(
		((cfg_descr->bmAttributes & USB_CFG_ATTR_SELF_POWERED) == 0) &&
		((cfg_descr->bMaxPower * 2) > 500)
	) {
		debug_info("excess power requirement; greater than 500mA");
		return 0;
	}

	// Check that the configuration has at least one interface.
	if(cfg_descr->bNumInterfaces < 1 || cfg_node->intf_node_count < 1) {
		debug_info("invalid number of interfaces");
		return 0;
	}

	bool has_hid_boot_mouse_intf = false, has_in_intr_endp = false;

	for(size_t i = 0; i < cfg_node->intf_node_count; i++) {
		// Get pointer to this interface's tree node and descriptor.
		const usb_interface_node_t *intf_node = &ctx->intf_trees[cfg_node->intf_node_indices[i]];
		const usb_interface_descriptor_t *intf_descr = &ctx->intf_descrs[intf_node->intf_descr_index];

		// Check if this interface is one that is an HID interface that supports
		// 'boot' protocol for mouse, and that has a single endpoint.
		if(
			intf_descr->bInterfaceClass == USB_INTF_CLASS_HID &&
			intf_descr->bInterfaceSubClass == USB_HID_INTF_SUBCLASS_BOOT &&
			intf_descr->bInterfaceProtocol == USB_HID_INTF_PROTOCOL_MOUSE &&
			intf_descr->bNumEndpoints == 1 &&
			intf_node->endp_node_count == 1
		) {
			has_hid_boot_mouse_intf = true;

			// Get pointer to tree node and descriptor of this interface's 1st
			// endpoint.
			const usb_endpoint_node_t *endp_node = &ctx->endp_trees[intf_node->endp_node_indices[0]];
			const usb_endpoint_descriptor_t *endp_descr = &ctx->endp_descrs[endp_node->endp_descr_index];

			// Check this interface's 1st endpoint and see if it's one that is
			// an IN endpoint with a transfer type of interrupt.
			if(
				(endp_descr->bEndpointAddress & USB_ENDP_ADDR_DIR_MASK) == USB_ENDP_ADDR_DIR_IN &&
				(endp_descr->bmAttributes & USB_ENDP_ATTR_TFR_TYPE_MASK) == USB_ENDP_ATTR_TFR_TYPE_INT &&
				endp_descr->bInterval >= 1
			) {
				has_in_intr_endp = true;
				break;
			}
		}
	}

	if(!has_hid_boot_mouse_intf) {
		debug_info("no HID boot mouse interface found");
		return 0;
	}
	if(!has_in_intr_endp) {
		debug_info("no IN interrupt endpoint found");
		return 0;
	}

	// If not rejected earlier, then we want to use this device, so return the
	// desired configuration's value field. :)
	return cfg_descr->bConfigurationValue;
}

static bool usb_find_hid_boot_mouse_intf(const usb_context_t * const ctx, uint8_t * const intf_num_out, uint8_t * const endp_addr_out, uint8_t * const endp_interval_out) {
	// Get a pointer to the device's 1st configuration tree node.
	const usb_config_node_t *cfg_node = &ctx->cfg_trees[ctx->dev_tree.cfg_node_indices[0]];

	for(size_t i = 0; i < cfg_node->intf_node_count; i++) {
		// Get pointer to this interface's tree node and descriptor.
		const usb_interface_node_t *intf_node = &ctx->intf_trees[cfg_node->intf_node_indices[i]];
		const usb_interface_descriptor_t *intf_descr = &ctx->intf_descrs[intf_node->intf_descr_index];

		// Check if this interface is one that is an HID interface that supports
		// 'boot' protocol for mouse.
		if(
			intf_descr->bInterfaceClass == USB_INTF_CLASS_HID &&
			intf_descr->bInterfaceSubClass == USB_HID_INTF_SUBCLASS_BOOT &&
			intf_descr->bInterfaceProtocol == USB_HID_INTF_PROTOCOL_MOUSE
		) {
			// Get pointer to tree node and descriptor of this interface's 1st
			// endpoint.
			const usb_endpoint_node_t *endp_node = &ctx->endp_trees[intf_node->endp_node_indices[0]];
			const usb_endpoint_descriptor_t *endp_descr = &ctx->endp_descrs[endp_node->endp_descr_index];

			// Check this interface's 1st endpoint and see if it's one that is
			// an IN endpoint with a transfer type of interrupt.
			if(
				(endp_descr->bEndpointAddress & USB_ENDP_ADDR_DIR_MASK) == USB_ENDP_ADDR_DIR_IN &&
				(endp_descr->bmAttributes & USB_ENDP_ATTR_TFR_TYPE_MASK) == USB_ENDP_ATTR_TFR_TYPE_INT
			) {
				*intf_num_out = intf_descr->bInterfaceNumber;
				*endp_addr_out = endp_descr->bEndpointAddress;
				*endp_interval_out = endp_descr->bInterval;
				return true;
			}
		}
	}

	// Couldn't find a matching interface.
	return false;
}

static bool usb_hid_report_descriptor_is_mouse(usb_hid_mouse_inputs_t *inputs) {
	const hid_collection_t *app_collection = NULL;
	const hid_collection_t *phys_collection = NULL;
	const hid_collection_t *logic_collection = NULL;
	const hid_input_t *input;
	
	if(inputs == NULL) return false;
	
	// Default the output args.
	inputs->button_1.hid_input = NULL;
	inputs->button_2.hid_input = NULL;
	inputs->button_3.hid_input = NULL;
	inputs->x_axis.hid_input = NULL;
	inputs->y_axis.hid_input = NULL;
	inputs->wheel.hid_input = NULL;
	
	// Sanity-check the dummy root collection in the given composition.
	if(!inputs->composition.collection_root->is_root) {
		debug_error("root collection isn't actually root");
		return false;
	}
	
	// Look for first-level Application collection with usage page of Generic
	// Desktop Ctrls and usage of Mouse.
	app_collection = hid_find_child_collection(inputs->composition.collection_root, false, 0, HID_COLLECTION_TYPE_APPLICATION, HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_MOUSE);	
	
	if(app_collection == NULL) {
		debug_warn("no qualifying application collection found");
		return false;
	} else {
		debug_trace("found top application collection %p", app_collection);
	}
	
	// Recursively look for lower-level Physical collection that is a descendent
	// of the top-level Application collection that has usage page of Generic
	// Desktop Ctrls and usage of Pointer.
	phys_collection = hid_find_child_collection(app_collection, true, 8, HID_COLLECTION_TYPE_PHYSICAL, HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_POINTER);
	
	if(phys_collection == NULL) {
		debug_warn("no qualifying physical collection found");
		return false;
	} else {
		debug_trace("found descendent physical collection %p", phys_collection);
	}
	
	// Wheel input might be in a Logical collection that is a child of the
	// Physical collection.
	logic_collection = hid_find_child_collection(phys_collection, false, 0, HID_COLLECTION_TYPE_LOGICAL, HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_UNDEFINED);

	if(logic_collection != NULL) {
		debug_trace("found child logical collection %p", logic_collection);
	}
	
	// Check that EITHER the Application or Physical collection contains inputs
	// that represent at least buttons 1 & 2, optionally 3. Some mice put the
	// buttons outside the Physical collection; some put them inside. Not sure
	// which is technically correct (maybe both are!), so accomodate either.
	const hid_collection_t *btns_search_colls[] = { app_collection, phys_collection };
	for(size_t i = 0; i < (sizeof(btns_search_colls) / sizeof(btns_search_colls[0])); i++) {
		input = btns_search_colls[i]->first_input;
		while(input != NULL) {
			if(
				input->usage_page == HID_USAGE_PAGE_BUTTON &&
				input->report_count >= 1 && input->report_size == 1 &&
				input->logical_min == 0 && input->logical_max == 1 &&
				input->flags.is_data && input->flags.is_variable && input->flags.is_absolute &&
				input->flags.is_non_wrap && input->flags.is_linear && input->flags.is_pref_state &&
				input->flags.is_no_null_pos && input->flags.is_bits
			) {
				if(hid_input_has_usage(input, HID_USAGE_BTN_1)) inputs->button_1.hid_input = input;
				if(hid_input_has_usage(input, HID_USAGE_BTN_2)) inputs->button_2.hid_input = input;
				if(hid_input_has_usage(input, HID_USAGE_BTN_3)) inputs->button_3.hid_input = input;
			}
			input = input->next_sibling;
		}
		// TODO: do we really want to always search both collections, even if we already found all buttons?
	}
	
	if(inputs->button_1.hid_input == NULL) {
		debug_warn("no qualifying button 1 input found");
		return false;
	} else {
		inputs->button_1.bit_offset = hid_input_usage_bit_offset(inputs->button_1.hid_input, HID_USAGE_BTN_1);
		debug_trace("found button 1, input = %p, bit_offset = %zu", inputs->button_1.hid_input, inputs->button_1.bit_offset);
	}
	
	if(inputs->button_2.hid_input == NULL) {
		debug_warn("no qualifying button 2 input found");
		return false;
	} else {
		inputs->button_2.bit_offset = hid_input_usage_bit_offset(inputs->button_2.hid_input, HID_USAGE_BTN_2);
		debug_trace("found button 2, input = %p, bit_offset = %zu", inputs->button_2.hid_input, inputs->button_2.bit_offset);
	}
	
	if(inputs->button_3.hid_input == NULL) {
		debug_warn("no qualifying button 3 input found");
		// Optional, so we don't quit here.
	} else {
		inputs->button_3.bit_offset = hid_input_usage_bit_offset(inputs->button_3.hid_input, HID_USAGE_BTN_3);
		debug_trace("found button 3, input = %p, bit_offset = %zu", inputs->button_3.hid_input, inputs->button_3.bit_offset);
	}
	
	// Check that the Physical collection contains inputs representing X and Y
	// axes, and optionally Wheel.
	input = phys_collection->first_input;
	while(input != NULL) {
		if(
			input->usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
			input->report_count >= 1 && input->report_size >= 8 &&
			input->logical_min <= -127 && input->logical_max >= 127 &&
			input->flags.is_data && input->flags.is_variable && input->flags.is_relative &&
			input->flags.is_non_wrap && input->flags.is_linear && input->flags.is_pref_state &&
			input->flags.is_no_null_pos && input->flags.is_bits
		) {
			if(hid_input_has_usage(input, HID_USAGE_X)) inputs->x_axis.hid_input = input;
			if(hid_input_has_usage(input, HID_USAGE_Y)) inputs->y_axis.hid_input = input;
		}
		input = input->next_sibling;
		// TODO: do we really want to continue searching even if we already found all inputs?
	}
	
	if(inputs->x_axis.hid_input == NULL) {
		debug_warn("no qualifying x-axis input found");
		return false;
	} else {
		inputs->x_axis.bit_offset = hid_input_usage_bit_offset(inputs->x_axis.hid_input, HID_USAGE_X);
		debug_trace("found x-axis, input = %p, bit_offset = %zu", inputs->x_axis.hid_input, inputs->x_axis.bit_offset);
	}
	
	if(inputs->y_axis.hid_input == NULL) {
		debug_warn("no qualifying y-axis input found");
		return false;
	} else {
		inputs->y_axis.bit_offset = hid_input_usage_bit_offset(inputs->y_axis.hid_input, HID_USAGE_Y);
		debug_trace("found y-axis, input = %p, bit_offset = %zu", inputs->y_axis.hid_input, inputs->y_axis.bit_offset);
	}
	
	// Look for optional Wheel input. Might be in Physical collection, or maybe
	// a Logical collection inside the Physical collection (typically paired
	// with a Resolution Multiplier feature).
	const hid_collection_t *wheel_search_colls[] = { phys_collection, logic_collection };
	for(size_t i = 0; i < (sizeof(wheel_search_colls) / sizeof(wheel_search_colls[0])); i++) {
		if(wheel_search_colls[i] != NULL) {
			input = wheel_search_colls[i]->first_input;
			while(input != NULL) {
				if(
					input->usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
					hid_input_has_usage(input, HID_USAGE_WHEEL) &&
					input->report_count >= 1 && input->report_size >= 8 &&
					input->logical_min <= -127 && input->logical_max >= 127 &&
					input->flags.is_data && input->flags.is_variable && input->flags.is_relative &&
					input->flags.is_non_wrap && input->flags.is_linear && input->flags.is_pref_state &&
					input->flags.is_no_null_pos && input->flags.is_bits
				) {
					inputs->wheel.hid_input = input;
				}
				input = input->next_sibling;
				// TODO: do we really want to continue searching even if we already found input?
			}
		}
	}

	if(inputs->wheel.hid_input == NULL) {
		debug_warn("no qualifying wheel input found");
		// Optional, so we don't quit here.
	} else {
		inputs->wheel.bit_offset = hid_input_usage_bit_offset(inputs->wheel.hid_input, HID_USAGE_WHEEL);
		debug_trace("found wheel, input = %p, bit_offset = %zu", inputs->wheel.hid_input, inputs->wheel.bit_offset);
	}
	
	return true;
}

static bool usb_hid_read_input_bool_value(const uint8_t *report, const size_t report_len, usb_hid_mouse_input_t *input) {
	if(report != NULL && report_len >= 1 && input != NULL && input->hid_input != NULL) {
		// Only read the input value out of the given report when EITHER: it has
		// no report ID, OR the input has a report ID and it matches the first
		// byte of report. (Note that bit offsets already account for presence
		// or absence of a report ID.)
		if(!input->hid_input->have_report_id || (input->hid_input->have_report_id && report[0] == input->hid_input->report_id)) {
			input->prev_value.bin = input->value.bin;
			input->value.bin = hid_read_report_value_unsigned(report, report_len, input->bit_offset, input->hid_input->report_size);
			return true;
		}
	}
	
	return false;
}

static bool usb_hid_read_input_numeric_value(const uint8_t *report, const size_t report_len, usb_hid_mouse_input_t *input) {
	if(report != NULL && report_len >= 1 && input != NULL && input->hid_input != NULL) {
		// Only read the input value out of the given report when EITHER: it has
		// no report ID, OR the input has a report ID and it matches the first
		// byte of report. (Note that bit offsets already account for presence
		// or absence of a report ID.)
		if(!input->hid_input->have_report_id || (input->hid_input->have_report_id && report[0] == input->hid_input->report_id)) {
			input->prev_value.num = input->value.num;
			input->value.num = hid_read_report_value_signed(report, report_len, input->bit_offset, input->hid_input->report_size);
			return true;
		}
	}
	
	return false;
}

static void event_usb_device_connected(void) {
	uint8_t hid_report_descr_buf[256];
	size_t hid_report_descr_len = 0;
	usb_hid_protocol_enum_t hid_protocol = USB_HID_PROTOCOL_BOOT;
	
	usb_context_init(&usb_ctx);
	usb_ctx.port_connected = true;
	
	debug_info("USB connected");

	led_act_usb_enable(false);
	led_act_serial_enable(false);
	
	// Attempt to enumerate the newly-connected device. We pass in our callback
	// function which evaluates the device and decides whether it's supported.
	if(!usb_enum_root_device(&usb_ctx, usb_eval_device)) {
		debug_error("enumeration failed");
		usb_set_bus_suspend(true);
		return;
	}

	usb_dump_descriptors(&usb_ctx);

	// Grab the number of the interface that supports HID boot mouse, and also
	// grab the associated endpoint address and interrupt interval.
	if(!usb_find_hid_boot_mouse_intf(&usb_ctx, &usb_mouse_intf_num, &usb_mouse_endp_addr, &usb_mouse_endp_interval_ms)) {
		debug_error("couldn't find HID boot mouse interface");
		usb_set_bus_suspend(true);
		return;
	}
	
	// Tell the device to only report when it has new data. Some mice won't
	// support this, so we don't care if it fails.
	usb_hid_set_idle(&usb_ctx, usb_mouse_intf_num, USB_HID_IDLE_DURATION_INDEFINITE, USB_HID_IDLE_REPORT_ALL);

	// Request and parse HID report descriptor. If the report descriptor could
	// not be retrieved, parsed, or it does not appropriately describe a mouse,
	// then fall back to the boot protocol.
	if(usb_hid_get_report_descriptor(&usb_ctx, usb_mouse_intf_num, hid_report_descr_buf, sizeof(hid_report_descr_buf), &hid_report_descr_len)) {
		if(hid_parse_report_descriptor(hid_report_descr_buf, hid_report_descr_len, &usb_mouse_inputs.composition)) {
			hid_dump_report_composition(&usb_mouse_inputs.composition);
			
			if(usb_hid_report_descriptor_is_mouse(&usb_mouse_inputs)) {
				hid_protocol = USB_HID_PROTOCOL_REPORT;
				debug_info("got an HID mouse");
			} else {
				debug_warn("not an HID mouse");
			}
		} else {
			debug_warn("failed to parse report descriptor");
		}
	} else {
		debug_warn("failed to get report descriptor");
	}
	
	// For boot protocol fallback, load standard HID boot protocol descriptor
	// and then proceed as per report.
	if(hid_protocol == USB_HID_PROTOCOL_BOOT) {
		debug_warn("fallback to boot protocol");
		
		if(!hid_parse_report_descriptor(hid_boot_mouse_std_report_descr, sizeof(hid_boot_mouse_std_report_descr), &usb_mouse_inputs.composition)) {
			debug_error("failed to parse standard boot mouse report descriptor");
			usb_set_bus_suspend(true);
			return;
		}
	}
	
	debug_info(
		"inputs: button_1 = %p, button_2 = %p, button_3 = %p",
		usb_mouse_inputs.button_1.hid_input, usb_mouse_inputs.button_2.hid_input, usb_mouse_inputs.button_3.hid_input
	);
	debug_info(
		"inputs: x_axis = %p, y_axis = %p, wheel = %p",
		usb_mouse_inputs.x_axis.hid_input, usb_mouse_inputs.y_axis.hid_input, usb_mouse_inputs.wheel.hid_input
	);
	debug_info(
		"bit offsets: button_1 = %zu, button_2 = %zu, button_3 = %zu",
		usb_mouse_inputs.button_1.bit_offset, usb_mouse_inputs.button_2.bit_offset, usb_mouse_inputs.button_3.bit_offset
	);
	debug_info(
		"bit offsets: x_axis = %zu, y_axis = %zu, wheel = %zu",
		usb_mouse_inputs.x_axis.bit_offset, usb_mouse_inputs.y_axis.bit_offset, usb_mouse_inputs.wheel.bit_offset
	);
	
	// Tell the device to switch to the appropriate protocol (boot or report).
	usb_hid_set_protocol(&usb_ctx, usb_mouse_intf_num, hid_protocol);

	// If the report-polling callback interval requested by the device's
	// interrupt endpoint is longer than the current serial mouse interval, then
	// make that the serial mouse interval.
	if(usb_mouse_endp_interval_ms > serial_mouse_interval_ms) serial_mouse_interval_ms = usb_mouse_endp_interval_ms;
	timestamp_interval_call_set_interval(mouse_interval_call_handle, serial_mouse_interval_ms);

	// Enable the RS-232 transceiver, assert the UART DSR line to signal a
	// serial mouse is present, and enable the edge-detect interrupton UART RTS.
	serial_mouse_rs232_trx_enable(true);
	serial_mouse_dsr_assert(true);
	serial_mouse_rts_interrupt_enable(true);
	
	debug_info("USB mouse init successful");
	
	led_act_usb_enable(true);
}

static void event_usb_device_disconnected(void) {
	usb_ctx.port_connected = false;
	
	debug_info("USB disconnected");
	
	// Disable the report-polling callback - won't work if there's no USB mouse!
	timestamp_interval_call_set_enabled(mouse_interval_call_handle, false);

	// Disable the edge-detect interrupt on UART RTS, de-assert the UART DSR
	// line to signal a serial mouse is no longer present, and shut down the
	// RS-232 transceiver.
	serial_mouse_rts_interrupt_enable(false);
	serial_mouse_dsr_assert(false);
	serial_mouse_rs232_trx_enable(false);
	
	led_act_usb_enable(false);
	led_act_serial_enable(false);
}

static void event_usb_power_fault(void) {
	// USB load switch over-current protection has tripped, so ensure the USB
	// port power remains disabled. USB port power being switched off will lead
	// to a USB device disconnect event, because it'll be as if the USB mouse
	// had been unplugged.
	usb_power_enable(false);
	
	debug_warn("USB over current!");
		
	// TODO: some way of signalling the power fault - flash LEDs?
	// This is a permanent state; require a reset or power cycle to resume
	// proper operation.
}

static void event_usb_mouse_interrupt(void) {
	uint8_t report_buf[20], serial_buf[10];
	size_t report_len = 0;

	led_act_usb_blink();

	// Poll the USB mouse to get its latest report by making an interrupt
	// transfer.
	if(usb_hid_interrupt_in(&usb_ctx, usb_mouse_endp_addr, report_buf, sizeof(report_buf), &report_len)) {
		// Read the mouse input values out of the report.
		const bool have_btn_1_val = usb_hid_read_input_bool_value(report_buf, report_len, &usb_mouse_inputs.button_1);
		const bool have_btn_2_val = usb_hid_read_input_bool_value(report_buf, report_len, &usb_mouse_inputs.button_2);
		const bool have_btn_3_val = usb_hid_read_input_bool_value(report_buf, report_len, &usb_mouse_inputs.button_3);
		const bool have_x_val = usb_hid_read_input_numeric_value(report_buf, report_len, &usb_mouse_inputs.x_axis);
		const bool have_y_val = usb_hid_read_input_numeric_value(report_buf, report_len, &usb_mouse_inputs.y_axis);
		const bool have_wheel_val = usb_hid_read_input_numeric_value(report_buf, report_len, &usb_mouse_inputs.wheel);

		// If we have a value for any of the mouse's inputs then format the
		// values into a serial data packet appropriate for the current serial
		// mouse mode and send it out via the UART.
		if(have_btn_1_val || have_btn_2_val || have_btn_3_val || have_x_val || have_y_val || have_wheel_val) {
			led_act_serial_blink();
			
			const size_t serial_buf_len = serial_mouse_packet_format(
				&usb_mouse_inputs,
				serial_mouse_mode,
				serial_buf,
				sizeof(serial_buf)
			);
			
			serial_mouse_transmit_data(serial_buf, serial_buf_len);

			debug_info(
				"L=%u R=%u M=%u X=% 6d Y=% 6d W=% 4d",
				usb_mouse_inputs.button_1.value.bin,
				usb_mouse_inputs.button_2.value.bin,
				usb_mouse_inputs.button_3.value.bin,
				usb_mouse_inputs.x_axis.value.num,
				usb_mouse_inputs.y_axis.value.num,
				usb_mouse_inputs.wheel.value.num
			);
			debug_hex_info(serial_buf, serial_buf_len);
		}
	}
}

static void event_serial_mouse_enable(void) {
	uint8_t ident_buf[100];
	
	debug_info("enabling");

	// Temporarily disable the edge-detect interrupt on UART RTS.
	serial_mouse_rts_interrupt_enable(false);
	
	// Re-read the current state of the configuration DIP switches, to get mouse
	// mode and PnP mode settings.
	config_switch_read(&serial_mouse_mode, &serial_mouse_data_format, &serial_mouse_pnp);

	// Set serial mouse UART format according to config.
	uart_mouse_set_format(serial_mouse_data_format);
	
	// Re-calculate how long it will take the serial mouse in current mode to
	// transmit a packet, and set that period length as the new interval for
	// polling the USB mouse for reports.
	serial_mouse_interval_ms = serial_mouse_transmit_time_ms(SERIAL_MOUSE_BAUD_RATE, serial_mouse_data_format, serial_mouse_packet_size(serial_mouse_mode));
	timestamp_interval_call_set_interval(mouse_interval_call_handle, serial_mouse_interval_ms);
	
	debug_info("mode = %s (%u), pnp = %u, interval = %lu ms", config_serial_mode_name(serial_mouse_mode), serial_mouse_mode, serial_mouse_pnp, serial_mouse_interval_ms);

	timestamp_delay_ms(10);
	
	uart_mouse_receive_flush();

	// Get the identification response for the current serial mouse mode.
	const size_t ident_buf_len = serial_mouse_ident(serial_mouse_mode, serial_mouse_pnp, ident_buf, sizeof(ident_buf));
	
	if(ident_buf_len > 0) {
		// Send the ident data out via the UART.
		serial_mouse_transmit_data(ident_buf, ident_buf_len);

		debug_info("ident:");
		debug_hex_info(ident_buf, ident_buf_len);

		// Wait for the serial ident data to finish transmission.
		uart_mouse_transmit_flush();
	}

	// After a delay, enable the report-polling callback. Delay is necessary
	// because some mouse drivers or OS will toggle the RTS line multiple times,
	// and we need to ignore that.
	timestamp_delay_ms(200);
	timestamp_interval_call_set_enabled(mouse_interval_call_handle, true);

	// Also re-enable the edge-detect interrupt on UART RTS.
	serial_mouse_rts_interrupt_enable(true);
	
	led_act_serial_enable(true);
}

static void event_serial_mouse_disable(void) {
	debug_info("disabling");
	
	// Temporarily disable the edge-detect interrupt on UART RTS.
	serial_mouse_rts_interrupt_enable(false);

	// Disable the report-polling callback, we don't want USB mouse activity to
	// generate any further serial reporting.
	timestamp_interval_call_set_enabled(mouse_interval_call_handle, false);

	// Wait for any outstanding serial data to finish transmission, and flush
	// any waiting received data after a short delay.
	uart_mouse_transmit_flush();
	timestamp_delay_ms(20);
	uart_mouse_receive_flush();

	// Re-enable the edge-detect interrupt on UART RTS.
	serial_mouse_rts_interrupt_enable(true);
	
	led_act_serial_enable(false);
}

static void usb_mouse_interval_callback(void) {
	event_fifo_push((event_t){
		.type = EVENT_TYPE_USB_MOUSE_INTERRUPT,
		.param.word = 0
	});
}

int main(void) {
	event_t event = { .type = EVENT_TYPE_NONE };

	clock_init();
	gpio_init();
#if defined(PRINTF_SDI)
	sdi_init(HCLK);
#elif defined(PRINTF_UART)
	uart_debug_init(HCLK, UART_DEBUG_BAUD_RATE);
#endif
	timestamp_init(HCLK);
	event_fifo_init();
	uart_mouse_init(HCLK, SERIAL_MOUSE_BAUD_RATE, UART_MOUSE_FORMAT_7N1);
	usb_host_init();

#if defined(PRINTF_SDI)
	timestamp_interval_call_add(sdi_process_queue, 2, true);
#endif
	/*timestamp_interval_call_add(debug_tick, 1000, true);*/
	led_act_serial_blink_tick_handle = timestamp_interval_call_add(led_act_serial_blink_tick, 1, false);
	led_act_usb_blink_tick_handle = timestamp_interval_call_add(led_act_usb_blink_tick, 1, false);
	
	timestamp_delay_ms(2000);

	debug_always("----------------------------------------");
	debug_always("USB TO SERIAL MOUSE CONVERTER");
	debug_always("----------------------------------------");
	debug_always("copyright (c) 2026 Basil Hussain");
	debug_always("built " __DATE__ " at " __TIME__);

	config_switch_read(&serial_mouse_mode, &serial_mouse_data_format, &serial_mouse_pnp);
	
	// Set serial mouse UART format according to config.
	uart_mouse_set_format(serial_mouse_data_format);

	// Calculate how long it will take the serial mouse in current mode to
	// transmit a packet, and set up the periodic callback for that period
	// length for polling the USB mouse for reports. Don't enable the callback
	// just yet, though (will be done later). Put simply, only poll the USB
	// mouse as quickly as we can transmit a full serial data packet.
	serial_mouse_interval_ms = serial_mouse_transmit_time_ms(SERIAL_MOUSE_BAUD_RATE, serial_mouse_data_format, serial_mouse_packet_size(serial_mouse_mode));
	mouse_interval_call_handle = timestamp_interval_call_add(usb_mouse_interval_callback, serial_mouse_interval_ms, false);

	usb_power_enable(true);

	// Do an initial check to see if we already have a USB device attached. If
	// so, manually push a connect event to the queue.
	if(usb_have_device_attached()) {
		event_fifo_push((event_t){
			.type = EVENT_TYPE_USB_DEV_CONNECT,
			.param.word = 0
		});
	}

	while(true) {
		// Attempt to pull the next event from the queue.
		if(event_fifo_pop(&event)) {
			debug_trace("event, type = 0x%lX, param = 0x%lX", event.type, event.param.word);
			
			switch(event.type) {
				case EVENT_TYPE_USB_DEV_CONNECT:
					event_usb_device_connected();
					break;
				case EVENT_TYPE_USB_DEV_DISCONNECT:
					event_usb_device_disconnected();
					break;
				case EVENT_TYPE_USB_PWR_FAULT:
					event_usb_power_fault();
					break;
				case EVENT_TYPE_USB_MOUSE_INTERRUPT:
					event_usb_mouse_interrupt();
					break;
				case EVENT_TYPE_SERIAL_MOUSE_ENABLE:
					event_serial_mouse_enable();
					break;
				case EVENT_TYPE_SERIAL_MOUSE_DISABLE:
					event_serial_mouse_disable();
					break;
				case EVENT_TYPE_NONE:
					// Do nothing.
					break;
				default:
					debug_error("unhandled event, type = 0x%lX, param = 0x%lX", event.type, event.param.word);
					break;
			}
		}
		
		// Later Logitech drivers don't use asterisk commands to probe the mouse
		// to identify it, but instead use Plug-n-Play. So don't bother to
		// process any received serial commands if PnP mode is enabled.
		if(serial_mouse_mode == SERIAL_MOUSE_MODE_LOGITECH && !serial_mouse_pnp) {
			serial_mouse_process_logitech_command();
		}
	}
}

ISR(EXTI7_0_IRQHandler) {
	// Did we get an EXTI interrupt for channel 1 (i.e. PA1)? If so, this means
	// we have UART_RTS toggled. If signal remains low after the interrupt, then
	// RTS was asserted; if high, RTS was de-asserted.
	if(EXTI->INTFR & EXTI_INTF_INTF1) {
		if(GPIOA->INDR & GPIO_INDR_IDR1) {
			event_fifo_push((event_t){
				.type = EVENT_TYPE_SERIAL_MOUSE_DISABLE,
				.param.word = 0
			});
		} else {
			event_fifo_push((event_t){
				.type = EVENT_TYPE_SERIAL_MOUSE_ENABLE,
				.param.word = 0
			});
		}

		// Clear the interrupt flag (by just writing '1').
		EXTI->INTFR = EXTI_INTF_INTF1;
	}
}

ISR(EXTI15_8_IRQHandler) {
	// Did we get an EXTI interrupt for channel 10 (i.e. PB10)? If so, this
	// means we have USB_PWR_FLT asserted. If signal is still low after the
	// falling edge triggered interrupt, raise an event for the error condition.
	if(EXTI->INTFR & EXTI_INTF_INTF10) {
		if(!(GPIOB->INDR & GPIO_INDR_IDR10)) {
			event_fifo_push((event_t){
				.type = EVENT_TYPE_USB_PWR_FAULT,
				.param.word = 0
			});
		}

		// Clear the interrupt flag (by just writing '1').
		EXTI->INTFR = EXTI_INTF_INTF10;
	}
}
