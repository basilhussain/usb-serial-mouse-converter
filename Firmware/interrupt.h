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

#ifndef INTERRUPT_H_
#define INTERRUPT_H_

#include <stdint.h>
#include <stdbool.h>
#include "ch32x035.h"

#define INTERRUPT_PRIV_MODE_MACHINE 3
#define INTERRUPT_PRIV_MODE_USER 0

#ifndef INTERRUPT_PRIV_MODE
#define INTERRUPT_PRIV_MODE INTERRUPT_PRIV_MODE_USER
#endif

// Use regular 'interrupt' attribute when PFIC's HPE feature is not enabled.
#define ISR(irq_name, ...) \
	extern void irq_name(void) __attribute__((interrupt, used)) __VA_ARGS__; \
	void irq_name(void)

// Macro for creating a block where during execution of the code inside the
// block, interrupts will be globally disabled, then restored to whatever state
// global interrupts were in beforehand. Should be safe for use inside ISRs (?)
#define interrupt_atomic_block() \
	for( \
		uint32_t __interrupt_atomic_block_mie_saved = __interrupt_save_clear_mie(), __interrupt_atomic_block_loop = 1; \
		__interrupt_atomic_block_loop; \
		__interrupt_restore_mie(__interrupt_atomic_block_mie_saved), __interrupt_atomic_block_loop = 0 \
	)

// Macro for creating a block where during execution of the code inside the
// block, only the given individual interrupt will be disabled, then restored to
// whatever state it was in beforehand. Should be safe for use inside ISRs (?)
#define interrupt_atomic_block_individual(irq) \
	for( \
		uint32_t \
			__interrupt_atomic_block_irqen_saved = (PFIC->ISR[(irq) / 32] & (1 << ((uint32_t)(irq) % 32))), \
			__interrupt_atomic_block_loop = (PFIC->IRER[(irq) / 32] = (1 << ((uint32_t)(irq) % 32))); \
		__interrupt_atomic_block_loop; \
		(PFIC->IENR[(irq) / 32] = __interrupt_atomic_block_irqen_saved), __interrupt_atomic_block_loop = 0 \
	)

// Macros for enabling and disabling a given IRQ (see IRQn_Type enum).
#define interrupt_enable(irq) do { PFIC->IENR[(irq) / 32] = (1 << ((uint32_t)(irq) % 32)); } while(0)
#define interrupt_disable(irq) do { PFIC->IRER[(irq) / 32] = (1 << ((uint32_t)(irq) % 32)); } while(0)

// Macros for setting and clearing a given IRQ as pending (i.e. causing that
// interrupt to be triggered?).
#define interrupt_set_pending(irq) do { PFIC->IPSR[(irq) / 32] = (1 << ((uint32_t)(irq) % 32)); } while(0)
#define interrupt_clear_pending(irq) do { PFIC->IPRR[(irq) / 32] = (1 << ((uint32_t)(irq) % 32)); } while(0)

// Macro for getting the pending or active status of a given IRQ (i.e. is it due
// to execute, or is it currently executing).
#define interrupt_pending(irq) ((bool)((PFIC->IPR[(irq) / 32]) & (1 << ((uint32_t)(irq) % 32))))
#define interrupt_active(irq) ((bool)((PFIC->IACTR[(irq) / 32]) & (1 << ((uint32_t)(irq) % 32))))

// Macros for setting and clearing a given IRQ as being preemptable (i.e. can be
// interrupted during execution by another interrupt).
#define interrupt_set_preempt(irq) do { PFIC->IPRIOR[(irq)] |= (1 << 7); } while(0)
#define interrupt_clear_preempt(irq) do { PFIC->IPRIOR[(irq)] &= ~(1 << 7); } while(0)

// Macros for getting the state of interrupt nesting and HPE - whether they are
// currently enabled or not.
#define interrupt_nesting_enabled() ((bool)(__interrupt_get_intsyscr() & 0x2))
#define interrupt_hpe_enabled() ((bool)(__interrupt_get_intsyscr() & 0x1))

// Macros for enabling and disabling Vector Table Free (VTF) operation for a
// given IRQ. Up to 4 channels are available (0-3). When enabling, specify the
// address of the ISR function. The address must be 2-byte aligned.
#define interrupt_vtf_enable(ch, irq, addr) \
	do { \
		PFIC->VTFIDR[(ch) % 4] = (irq); \
		PFIC->VTFADDR[(ch) % 4] = ((uint32_t)(addr) & PFIC_VTFADDR_ADDR) | PFIC_VTFADDR_EN; \
	} while(0)
#define interrupt_vtf_disable(ch, irq) \
	do { \
		PFIC->VTFADDR[(ch) % 4] &= ~PFIC_VTFADDR_EN; \
	} while(0)

/******************************************************************************/

#if defined(INTERRUPT_PRIV_MODE) && INTERRUPT_PRIV_MODE == INTERRUPT_PRIV_MODE_USER
#define MIE_CSR "0x800" // gintenr
#elif defined(INTERRUPT_PRIV_MODE) && INTERRUPT_PRIV_MODE == INTERRUPT_PRIV_MODE_MACHINE
#define MIE_CSR "mstatus"
#else
#error "INTERRUPT_PRIV_MODE not defined or unknown value"
#endif

#define MIE_MASK 0x8

__attribute__((always_inline)) static inline uint32_t __interrupt_save_clear_mie(void) {
	uint32_t value;

	// Read the CSR register (whether mstatus or gintenr) and clear the MIE flag
	// in a single atomic instruction. CSRRC instruction takes a mask of which
	// bits to be cleared, so pass in appropriate value to clear bit 3 for MIE.
	__asm volatile(
		"csrrc %0, " MIE_CSR ", %1" "\n\t"
		"fence.i"
		: "=r" (value) // Outputs
		: "i" (MIE_MASK) // Inputs
		: // No clobbers
	);

	// Return the value masked to only contain the MIE flag value.
	return value & MIE_MASK;
}

__attribute__((always_inline)) static inline void __interrupt_restore_mie(uint32_t value) {
	// Mask the given argument value to only the MIE flag (bit 3).
	value &= MIE_MASK;

	// Set bits in the CSR register (whether mstatus or gintenr) according to a
	// given mask, which is the argument value.
	// TODO: technically, this can only *set* MIE, and won't clear it if MIE
	// somehow becomes set during the atomic block. But that is unlikely.
	// Perhaps add a CSRC to ensure MIE is cleared first?
	__asm volatile(
		"csrs " MIE_CSR ", %0"
		: // No outputs
		: "r" (value) // Inputs
		: // No clobbers
	);
}

#undef MIE_MASK
#undef MIE_CSR

__attribute__((always_inline)) static inline uint32_t __interrupt_get_intsyscr(void) {
	uint32_t value;

	// Read the INTSYSCR interrupt system control CSR register.
	__asm volatile(
		"csrr %0, 0x804"
		: "=r" (value) // Outputs
		: // No inputs
		: // No clobbers
	);

	return value;
}

#endif // INTERRUPT_H_
