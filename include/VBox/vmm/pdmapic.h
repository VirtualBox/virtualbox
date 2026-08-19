/** @file
 * PDM - Pluggable Device Manager, APIC Interface.
 */

/*
 * Copyright (C) 2024-2026 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_vmm_pdmapic_h
#define VBOX_INCLUDED_vmm_pdmapic_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/apic.h>
struct PDMDEVREGCB;

/** @defgroup grp_pdm_apic   The local APIC PDM API
 * @ingroup grp_pdm
 * @{
 */

RT_C_DECLS_BEGIN

/**
 * APIC mode argument for apicR3SetCpuIdFeatureLevel.
 *
 * Also used in saved-states, CFGM don't change existing values.
 */
typedef enum PDMAPICMODE
{
    /** Invalid 0 entry. */
    PDMAPICMODE_INVALID = 0,
    /** No APIC. */
    PDMAPICMODE_NONE,
    /** Standard APIC (X86_CPUID_FEATURE_EDX_APIC). */
    PDMAPICMODE_APIC,
    /** Intel X2APIC (X86_CPUID_FEATURE_ECX_X2APIC). */
    PDMAPICMODE_X2APIC,
    /** The usual 32-bit paranoia. */
    PDMAPICMODE_32BIT_HACK = 0x7fffffff
} PDMAPICMODE;

/**
 * APIC irq argument for pfnSetInterruptFF and pfnClearInterruptFF.
 */
typedef enum PDMAPICIRQ
{
    /** Invalid 0 entry. */
    PDMAPICIRQ_INVALID = 0,
    /** Normal hardware interrupt. */
    PDMAPICIRQ_HARDWARE,
    /** NMI. */
    PDMAPICIRQ_NMI,
    /** SMI. */
    PDMAPICIRQ_SMI,
    /** ExtINT (HW interrupt via PIC). */
    PDMAPICIRQ_EXTINT,
    /** Interrupt arrived, needs to be updated to the IRR. */
    PDMAPICIRQ_UPDATE_PENDING,
    /** The usual 32-bit paranoia. */
    PDMAPICIRQ_32BIT_HACK = 0x7fffffff
} PDMAPICIRQ;

/**
 * The type of PDM APIC backend.
 */
typedef enum PDMAPICBACKENDTYPE
{
    /** None/Invalid PDM APIC backend. */
    PDMAPICBACKENDTYPE_NONE = 0,
    /** VirtualBox backend. */
    PDMAPICBACKENDTYPE_VBOX,
    /** KVM backend. */
    PDMAPICBACKENDTYPE_KVM,
    /** Hyper-V backend. */
    PDMAPICBACKENDTYPE_HYPERV,
    /** Hypervisor.Framework backend. */
    PDMAPICBACKENDTYPE_HVF,
    /** End of valid PDM APIC backend values. */
    PDMAPICBACKENDTYPE_END,
    /** The usual 32-bit paranoia. */
    PDMAPICBACKENDTYPE_32BIT_HACK = 0x7fffffff
} PDMAPICBACKENDTYPE;

/**
 * PDM APIC backend ring-3 API.
 */
typedef struct PDMAPICBACKENDR3
{
    /**
     * Returns whether the APIC is hardware enabled or not.
     *
     * @returns true if enabled, false otherwise.
     * @param   pVCpu           The cross context virtual CPU structure.
     */
    DECLR3CALLBACKMEMBER(bool, pfnIsEnabled, (PCVMCPUCC pVCpu));

    /**
     * Initializes per-VCPU APIC to the state following an INIT reset
     * ("Wait-for-SIPI" state).
     *
     * @param   pVCpu       The cross context virtual CPU structure.
     */
    DECLR3CALLBACKMEMBER(void, pfnInitIpi, (PVMCPUCC pVCpu));

    /**
     * Gets the APIC base MSR (no checks are performed wrt APIC hardware or its
     * state).
     *
     * @returns The base MSR value.
     * @param   pVCpu       The cross context virtual CPU structure.
     */
    DECLR3CALLBACKMEMBER(uint64_t, pfnGetBaseMsrNoCheck, (PCVMCPUCC pVCpu));

    /**
     * Gets the APIC base MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   pu64Value   Where to store the MSR value.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnGetBaseMsr, (PVMCPUCC pVCpu, uint64_t *pu64Value));

    /**
     * Sets the APIC base MSR.
     *
     * @returns VBox status code - no informational ones, esp. not
     *          VINF_CPUM_R3_MSR_WRITE.  Only the following two:
     * @retval  VINF_SUCCESS
     * @retval  VERR_CPUM_RAISE_GP_0
     *
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u64BaseMsr  The value to set.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetBaseMsr, (PVMCPUCC pVCpu, uint64_t u64BaseMsr));

    /**
     * Reads a 32-bit register at a specified offset.
     *
     * @returns The value at the specified offset.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   offReg          The offset of the register being read.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnReadRaw32, (PCVMCPUCC pVCpu, uint16_t offReg));

    /**
     * Reads an APIC MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u32Reg          The MSR being read.
     * @param   pu64Value       Where to store the read value.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnReadMsr, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value));

    /**
     * Writes an APIC MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u32Reg          The MSR being written.
     * @param   u64Value        The value to write.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnWriteMsr, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value));

    /**
     * Gets the APIC TPR (Task Priority Register).
     *
     * @returns VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   pu8Tpr          Where to store the TPR.
     * @param   pfPending       Where to store whether there is a pending interrupt
     *                          (optional, can be NULL).
     * @param   pu8PendingIntr  Where to store the highest-priority pending interrupt
     *                          (optional, can be NULL).
     */
    DECLR3CALLBACKMEMBER(int, pfnGetTpr, (PCVMCPUCC pVCpu, uint8_t *pu8Tpr, bool *pfPending, uint8_t *pu8PendingIntr));

    /**
     * Sets the TPR (Task Priority Register).
     *
     * @retval  VINF_SUCCESS
     * @retval  VERR_CPUM_RAISE_GP_0
     * @retval  VERR_PDM_NO_APIC_INSTANCE
     *
     * @param   pVCpu                   The cross context virtual CPU structure.
     * @param   u8Tpr                   The TPR value to set.
     * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during this
     *                                  write.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetTpr, (PVMCPUCC pVCpu, uint8_t u8Tpr, bool fForceX2ApicBehaviour));

    /**
     * Gets the Interrupt Command Register (ICR), without performing any interface
     * checks.
     *
     * @returns The ICR value.
     * @param   pVCpu           The cross context virtual CPU structure.
     */
    DECLR3CALLBACKMEMBER(uint64_t, pfnGetIcrNoCheck, (PVMCPUCC pVCpu));

    /**
     * Sets the Interrupt Command Register (ICR).
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u64Icr          The ICR (High and Low combined).
     * @param   rcRZ            The return code if the operation cannot be performed
     *                          in the current context.
     *
     * @remarks This function is used by both x2APIC interface and the Hyper-V
     *          interface, see APICHvSetIcr. The Hyper-V spec isn't clear what
     *          happens when invalid bits are set. For the time being, it will
     *          \#GP like a regular x2APIC access.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnSetIcr, (PVMCPUCC pVCpu, uint64_t uIcr, int rcRZ));

    /**
     * Gets the APIC timer frequency.
     *
     * @returns Strict VBox status code.
     * @param   pVM             The cross context VM structure.
     * @param   pu64Value       Where to store the timer frequency.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetTimerFreq, (PVMCC pVM, uint64_t *pu64Value));

    /**
     * Assert/de-assert the local APIC's LINT0/LINT1 interrupt pins.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u8Pin       The interrupt pin (0 for LINT0 or 1 for LINT1).
     * @param   u8Level     The level (0 for low or 1 for high).
     * @param   rcRZ        The return code if the operation cannot be performed in
     *                      the current context.
     *
     * @note    All callers totally ignores the status code!
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnSetLocalInterrupt, (PVMCPUCC pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ));

    /**
     * Gets the next highest-priority interrupt from the APIC, marking it as an
     * "in-service" interrupt.
     *
     * @returns VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   pu8Vector   Where to store the vector.
     * @param   puSrcTag    Where to store the interrupt source tag (debugging).
     */
    DECLR3CALLBACKMEMBER(int, pfnGetInterrupt, (PVMCPUCC pVCpu, uint8_t *pu8Vector, uint32_t *puSrcTag));

    /**
     * Posts an interrupt to a target APIC.
     *
     * This function handles interrupts received from the system bus or
     * interrupts generated locally from the LVT or via a self IPI.
     *
     * Don't use this function to try and deliver ExtINT style interrupts.
     *
     * @returns true if the interrupt was accepted, false otherwise.
     * @param   pVCpu               The cross context virtual CPU structure.
     * @param   uVector             The vector of the interrupt to be posted.
     * @param   fAutoEoi            Whether this interrupt has automatic EOI
     *                              treatment.
     * @param   enmTriggerMode      The trigger mode of the interrupt.
     * @param   uSrcTag             The interrupt source tag (debugging).
     *
     * @thread  Any.
     */
    DECLR3CALLBACKMEMBER(bool, pfnPostInterrupt, (PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, bool fAutoEoi,
                                                  uint32_t uSrcTag));

    /**
     * Updating pending interrupts into the IRR if required.
     *
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLR3CALLBACKMEMBER(void, pfnUpdatePendingInterrupts, (PVMCPUCC pVCpu));

    /**
     * Delivers an interrupt message via the system bus.
     *
     * @returns VBox status code.
     * @param   pVM             The cross context VM structure.
     * @param   uDest           The destination mask.
     * @param   uDestMode       The destination mode.
     * @param   uDeliveryMode   The delivery mode.
     * @param   uVector         The interrupt vector.
     * @param   uPolarity       The interrupt line polarity.
     * @param   uTriggerMode    The trigger mode.
     * @param   uIoApicPin      Set to the pin which generated the interrupt message if originating from the IO-APIC.
     *                          UINT8_MAX if not originating from a pin, i.e. an interrupt message originating from
     *                          a device.
     * @param   uSrcTag         The interrupt source tag (debugging).
     */
    DECLR3CALLBACKMEMBER(int, pfnBusDeliver, (PVMCC pVM, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode, uint8_t uVector,
                                              uint8_t uPolarity, uint8_t uTriggerMode, uint8_t uIoApicPin, uint32_t uSrcTag));

    /**
     * Sets the End-Of-Interrupt (EOI) register.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu                   The cross context virtual CPU structure.
     * @param   uEoi                    The EOI value.
     * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during
     *                                  this write.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnSetEoi, (PVMCPUCC pVCpu, uint32_t uEoi, bool fForceX2ApicBehaviour));

    /**
     * Sets whether Hyper-V compatibility mode (MSR interface) is enabled or not.
     * @see APICR3SetHvCompatMode for details.
     *
     * @returns VBox status code.
     * @param   pVM                 The cross context VM structure.
     * @param   fHyperVCompatMode   Whether the compatibility mode is enabled.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetHvCompatMode, (PVMCC pVM, bool fHyperVCompatMode));

    /**
     * Imports the APIC state.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnImportState, (PVMCPUCC pVCpu));

    /**
     * Exports the APIC state.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnExportState, (PVMCPUCC pVCpu));

    /**
     * Updates the APIC state after a write to the APIC page by hardware.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu   The cross context virtual CPU structure.
     *
     * @note This is a helper for AVIC/APICv when used on AMD or Intel.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnUpdateStateAfterWrite, (PVMCPUCC pVCpu, uint16_t offApicReg));

    /**
     * Sets the End-Of-Interrupt (EOI) register when the vector corresponding
     * to the EOI is given.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   uVector     The vector for the attempted EOI.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnSetEoiFast, (PVMCPUCC pVCpu, uint8_t uVector));

    /** @name Reserved for future (MBZ).
     * @{ */
    DECLR3CALLBACKMEMBER(int, pfnReserved0, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved1, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved2, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved3, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved4, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved5, (void));
    /** @} */
} PDMAPICBACKENDR3;
/** Pointer to ring-3 APIC backend. */
typedef R3PTRTYPE(struct PDMAPICBACKENDR3 *) PPDMAPICBACKENDR3;
/** Const pointer to ring-3 APIC backend. */
typedef R3PTRTYPE(const struct PDMAPICBACKENDR3 *) PCPDMAPICBACKENDR3;
AssertCompileSizeAlignment(PDMAPICBACKENDR3, 8);

/**
 * PDM APIC backend ring-0 API.
 */
typedef struct PDMAPICBACKENDR0
{
    /**
     * Returns whether the APIC is hardware enabled or not.
     *
     * @returns true if enabled, false otherwise.
     * @param   pVCpu           The cross context virtual CPU structure.
     */
    DECLR0CALLBACKMEMBER(bool, pfnIsEnabled, (PCVMCPUCC pVCpu));

    /**
     * Initializes per-VCPU APIC to the state following an INIT reset
     * ("Wait-for-SIPI" state).
     *
     * @param   pVCpu       The cross context virtual CPU structure.
     */
    DECLR0CALLBACKMEMBER(void, pfnInitIpi, (PVMCPUCC pVCpu));

    /**
     * Gets the APIC base MSR (no checks are performed wrt APIC hardware or its
     * state).
     *
     * @returns The base MSR value.
     * @param   pVCpu       The cross context virtual CPU structure.
     */
    DECLR0CALLBACKMEMBER(uint64_t, pfnGetBaseMsrNoCheck, (PCVMCPUCC pVCpu));

    /**
     * Gets the APIC base MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   pu64Value   Where to store the MSR value.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnGetBaseMsr, (PVMCPUCC pVCpu, uint64_t *pu64Value));

    /**
     * Sets the APIC base MSR.
     *
     * @returns VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u64BaseMsr  The value to set.
     */
    DECLR0CALLBACKMEMBER(int, pfnSetBaseMsr, (PVMCPUCC pVCpu, uint64_t u64BaseMsr));

    /**
     * Reads a 32-bit register at a specified offset.
     *
     * @returns The value at the specified offset.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   offReg          The offset of the register being read.
     */
    DECLR0CALLBACKMEMBER(uint32_t, pfnReadRaw32, (PCVMCPUCC pVCpu, uint16_t offReg));

    /**
     * Reads an APIC MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u32Reg          The MSR being read.
     * @param   pu64Value       Where to store the read value.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnReadMsr, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value));

    /**
     * Writes an APIC MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u32Reg          The MSR being written.
     * @param   u64Value        The value to write.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnWriteMsr, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value));

    /**
     * Gets the APIC TPR (Task Priority Register).
     *
     * @returns VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   pu8Tpr          Where to store the TPR.
     * @param   pfPending       Where to store whether there is a pending interrupt
     *                          (optional, can be NULL).
     * @param   pu8PendingIntr  Where to store the highest-priority pending interrupt
     *                          (optional, can be NULL).
     */
    DECLR0CALLBACKMEMBER(int, pfnGetTpr, (PCVMCPUCC pVCpu, uint8_t *pu8Tpr, bool *pfPending, uint8_t *pu8PendingIntr));

    /**
     * Sets the TPR (Task Priority Register).
     *
     * @retval  VINF_SUCCESS
     * @retval  VERR_CPUM_RAISE_GP_0
     * @retval  VERR_PDM_NO_APIC_INSTANCE
     *
     * @param   pVCpu                   The cross context virtual CPU structure.
     * @param   u8Tpr                   The TPR value to set.
     * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during this
     *                                  write.
     */
    DECLR0CALLBACKMEMBER(int, pfnSetTpr, (PVMCPUCC pVCpu, uint8_t u8Tpr, bool fForceX2ApicBehaviour));

    /**
     * Gets the Interrupt Command Register (ICR), without performing any interface
     * checks.
     *
     * @returns The ICR value.
     * @param   pVCpu           The cross context virtual CPU structure.
     */
    DECLR0CALLBACKMEMBER(uint64_t, pfnGetIcrNoCheck, (PVMCPUCC pVCpu));

    /**
     * Sets the Interrupt Command Register (ICR).
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u64Icr          The ICR (High and Low combined).
     * @param   rcRZ            The return code if the operation cannot be performed
     *                          in the current context.
     *
     * @remarks This function is used by both x2APIC interface and the Hyper-V
     *          interface, see APICHvSetIcr. The Hyper-V spec isn't clear what
     *          happens when invalid bits are set. For the time being, it will
     *          \#GP like a regular x2APIC access.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnSetIcr, (PVMCPUCC pVCpu, uint64_t uIcr, int rcRZ));

    /**
     * Gets the APIC timer frequency.
     *
     * @returns Strict VBox status code.
     * @param   pVM             The cross context VM structure.
     * @param   pu64Value       Where to store the timer frequency.
     */
    DECLR0CALLBACKMEMBER(int, pfnGetTimerFreq, (PVMCC pVM, uint64_t *pu64Value));

    /**
     * Assert/de-assert the local APIC's LINT0/LINT1 interrupt pins.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u8Pin       The interrupt pin (0 for LINT0 or 1 for LINT1).
     * @param   u8Level     The level (0 for low or 1 for high).
     * @param   rcRZ        The return code if the operation cannot be performed in
     *                      the current context.
     *
     * @note    All callers totally ignores the status code!
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnSetLocalInterrupt, (PVMCPUCC pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ));

    /**
     * Gets the next highest-priority interrupt from the APIC, marking it as an
     * "in-service" interrupt.
     *
     * @returns VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   pu8Vector   Where to store the vector.
     * @param   puSrcTag    Where to store the interrupt source tag (debugging).
     */
    DECLR0CALLBACKMEMBER(int, pfnGetInterrupt, (PVMCPUCC pVCpu, uint8_t *pu8Vector, uint32_t *puTagSrc));

    /**
     * Posts an interrupt to a target APIC.
     *
     * This function handles interrupts received from the system bus or
     * interrupts generated locally from the LVT or via a self IPI.
     *
     * Don't use this function to try and deliver ExtINT style interrupts.
     *
     * @returns true if the interrupt was accepted, false otherwise.
     * @param   pVCpu               The cross context virtual CPU structure.
     * @param   uVector             The vector of the interrupt to be posted.
     * @param   fAutoEoi            Whether this interrupt has automatic EOI
     *                              treatment.
     * @param   enmTriggerMode      The trigger mode of the interrupt.
     * @param   uSrcTag             The interrupt source tag (debugging).
     *
     * @thread  Any.
     */
    DECLR0CALLBACKMEMBER(bool, pfnPostInterrupt, (PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, bool fAutoEoi,
                                                  uint32_t uSrcTag));

    /**
     * Updating pending interrupts into the IRR if required.
     *
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLR0CALLBACKMEMBER(void, pfnUpdatePendingInterrupts, (PVMCPUCC pVCpu));

    /**
     * Delivers an interrupt message via the system bus.
     *
     * @returns VBox status code.
     * @param   pVM             The cross context VM structure.
     * @param   uDest           The destination mask.
     * @param   uDestMode       The destination mode.
     * @param   uDeliveryMode   The delivery mode.
     * @param   uVector         The interrupt vector.
     * @param   uPolarity       The interrupt line polarity.
     * @param   uTriggerMode    The trigger mode.
     * @param   uIoApicPin      Set to the pin which generated the interrupt message if originating from the IO-APIC.
     *                          UINT8_MAX if not originating from a pin, i.e. an interrupt message originating from
     *                          a device.
     * @param   uSrcTag         The interrupt source tag (debugging).
     */
    DECLR0CALLBACKMEMBER(int, pfnBusDeliver, (PVMCC pVM, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode, uint8_t uVector,
                                              uint8_t uPolarity, uint8_t uTriggerMode, uint8_t uIoApicPin, uint32_t uSrcTag));

    /**
     * Sets the End-Of-Interrupt (EOI) register.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu                   The cross context virtual CPU structure.
     * @param   uEoi                    The EOI value.
     * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during
     *                                  this write.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnSetEoi, (PVMCPUCC pVCpu, uint32_t uEoi, bool fForceX2ApicBehaviour));

    /**
     * Gets the APIC page pointers for the specified VCPU.
     *
     * @returns VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   pHCPhys         Where to store the host-context physical address.
     * @param   pR0Ptr          Where to store the ring-0 address.
     * @param   pR3Ptr          Where to store the ring-3 address (optional).
     */
    DECLR0CALLBACKMEMBER(int, pfnGetApicPageForCpu, (PCVMCPUCC pVCpu, PRTHCPHYS pHCPhys, PRTR0PTR pR0Ptr, PRTR3PTR pR3Ptr));

    /**
     * Imports the APIC state.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnImportState, (PVMCPUCC pVCpu));

    /**
     * Exports the APIC state.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   offApicReg      The APIC register offset which was updated.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnExportState, (PVMCPUCC pVCpu));

    /**
     * Updates the APIC state after a write to the APIC page by hardware.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu   The cross context virtual CPU structure.
     *
     * @note This is a helper for AVIC/APICv when used on AMD or Intel.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnUpdateStateAfterWrite, (PVMCPUCC pVCpu, uint16_t offApicReg));

    /**
     * Sets the End-Of-Interrupt (EOI) register when the vector corresponding
     * to the EOI is given.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   uVector     The vector for the attempted EOI.
     */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnSetEoiFast, (PVMCPUCC pVCpu, uint8_t uVector));

    /** @name Reserved for future (MBZ).
     * @{ */
    DECLR0CALLBACKMEMBER(int, pfnReserved0, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved1, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved2, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved3, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved4, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved5, (void));
    /** @} */
} PDMAPICBACKENDR0;
/** Pointer to ring-0 APIC backend. */
typedef R0PTRTYPE(struct PDMAPICBACKENDR0 *) PPDMAPICBACKENDR0;
/** Const pointer to ring-0 APIC backend. */
typedef R0PTRTYPE(const struct PDMAPICBACKENDR0 *) PCPDMAPICBACKENDR0;
AssertCompileSizeAlignment(PDMAPICBACKENDR0, 8);

/**
 * PDM APIC backend RC API.
 */
typedef struct PDMAPICBACKENDRC
{
    /**
     * Returns whether the APIC is hardware enabled or not.
     *
     * @returns true if enabled, false otherwise.
     * @param   pVCpu           The cross context virtual CPU structure.
     */
    DECLRCCALLBACKMEMBER(bool, pfnIsEnabled, (PCVMCPUCC pVCpu));

    /**
     * Initializes per-VCPU APIC to the state following an INIT reset
     * ("Wait-for-SIPI" state).
     *
     * @param   pVCpu       The cross context virtual CPU structure.
     */
    DECLRCCALLBACKMEMBER(void, pfnInitIpi, (PVMCPUCC pVCpu));

    /**
     * Gets the APIC base MSR (no checks are performed wrt APIC hardware or its
     * state).
     *
     * @returns The base MSR value.
     * @param   pVCpu       The cross context virtual CPU structure.
     */
    DECLRCCALLBACKMEMBER(uint64_t, pfnGetBaseMsrNoCheck, (PCVMCPUCC pVCpu));

    /**
     * Gets the APIC base MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   pu64Value   Where to store the MSR value.
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnGetBaseMsr, (PVMCPUCC pVCpu, uint64_t *pu64Value));

    /**
     * Sets the APIC base MSR.
     *
     * @returns VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u64BaseMsr  The value to set.
     */
    DECLRCCALLBACKMEMBER(int, pfnSetBaseMsr, (PVMCPUCC pVCpu, uint64_t u64BaseMsr));

    /**
     * Reads a 32-bit register at a specified offset.
     *
     * @returns The value at the specified offset.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   offReg          The offset of the register being read.
     */
    DECLRCCALLBACKMEMBER(uint32_t, pfnReadRaw32, (PCVMCPUCC pVCpu, uint16_t offReg));

    /**
     * Reads an APIC MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u32Reg          The MSR being read.
     * @param   pu64Value       Where to store the read value.
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnReadMsr, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value));

    /**
     * Writes an APIC MSR.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u32Reg          The MSR being written.
     * @param   u64Value        The value to write.
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnWriteMsr, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value));

    /**
     * Gets the APIC TPR (Task Priority Register).
     *
     * @returns VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   pu8Tpr          Where to store the TPR.
     * @param   pfPending       Where to store whether there is a pending interrupt
     *                          (optional, can be NULL).
     * @param   pu8PendingIntr  Where to store the highest-priority pending interrupt
     *                          (optional, can be NULL).
     */
    DECLRCCALLBACKMEMBER(int, pfnGetTpr, (PCVMCPUCC pVCpu, uint8_t *pu8Tpr, bool *pfPending, uint8_t *pu8PendingIntr));

    /**
     * Sets the TPR (Task Priority Register).
     *
     * @retval  VINF_SUCCESS
     * @retval  VERR_CPUM_RAISE_GP_0
     * @retval  VERR_PDM_NO_APIC_INSTANCE
     *
     * @param   pVCpu                   The cross context virtual CPU structure.
     * @param   u8Tpr                   The TPR value to set.
     * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during this
     *                                  write.
     */
    DECLRCCALLBACKMEMBER(int, pfnSetTpr, (PVMCPUCC pVCpu, uint8_t u8Tpr, bool fForceX2ApicBehaviour));

    /**
     * Gets the Interrupt Command Register (ICR), without performing any interface
     * checks.
     *
     * @returns The ICR value.
     * @param   pVCpu           The cross context virtual CPU structure.
     */
    DECLRCCALLBACKMEMBER(uint64_t, pfnGetIcrNoCheck, (PVMCPUCC pVCpu));

    /**
     * Sets the Interrupt Command Register (ICR).
     *
     * @returns Strict VBox status code.
     * @param   pVCpu           The cross context virtual CPU structure.
     * @param   u64Icr          The ICR (High and Low combined).
     * @param   rcRZ            The return code if the operation cannot be performed
     *                          in the current context.
     *
     * @remarks This function is used by both x2APIC interface and the Hyper-V
     *          interface, see APICHvSetIcr. The Hyper-V spec isn't clear what
     *          happens when invalid bits are set. For the time being, it will
     *          \#GP like a regular x2APIC access.
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnSetIcr, (PVMCPUCC pVCpu, uint64_t uIcr, int rcRZ));

    /**
     * Gets the APIC timer frequency.
     *
     * @returns Strict VBox status code.
     * @param   pVM             The cross context VM structure.
     * @param   pu64Value       Where to store the timer frequency.
     */
    DECLRCCALLBACKMEMBER(int, pfnGetTimerFreq, (PVMCC pVM, uint64_t *pu64Value));

    /**
     * Assert/de-assert the local APIC's LINT0/LINT1 interrupt pins.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u8Pin       The interrupt pin (0 for LINT0 or 1 for LINT1).
     * @param   u8Level     The level (0 for low or 1 for high).
     * @param   rcRZ        The return code if the operation cannot be performed in
     *                      the current context.
     *
     * @note    All callers totally ignores the status code!
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnSetLocalInterrupt, (PVMCPUCC pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ));

    /**
     * Gets the next highest-priority interrupt from the APIC, marking it as an
     * "in-service" interrupt.
     *
     * @returns VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   pu8Vector   Where to store the vector.
     * @param   puSrcTag    Where to store the interrupt source tag (debugging).
     */
    DECLRCCALLBACKMEMBER(int, pfnGetInterrupt, (PVMCPUCC pVCpu, uint8_t *pu8Vector, uint32_t *puTagSrc));

    /**
     * Posts an interrupt to a target APIC.
     *
     * This function handles interrupts received from the system bus or
     * interrupts generated locally from the LVT or via a self IPI.
     *
     * Don't use this function to try and deliver ExtINT style interrupts.
     *
     * @returns true if the interrupt was accepted, false otherwise.
     * @param   pVCpu               The cross context virtual CPU structure.
     * @param   uVector             The vector of the interrupt to be posted.
     * @param   fAutoEoi            Whether this interrupt has automatic EOI
     *                              treatment.
     * @param   enmTriggerMode      The trigger mode of the interrupt.
     * @param   uSrcTag             The interrupt source tag (debugging).
     *
     * @thread  Any.
     */
    DECLRCCALLBACKMEMBER(bool, pfnPostInterrupt, (PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, bool fAutoEoi,
                                                  uint32_t uSrcTag));

    /**
     * Updating pending interrupts into the IRR if required.
     *
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLRCCALLBACKMEMBER(void, pfnUpdatePendingInterrupts, (PVMCPUCC pVCpu));

    /**
     * Delivers an interrupt message via the system bus.
     *
     * @returns VBox status code.
     * @param   pVM             The cross context VM structure.
     * @param   uDest           The destination mask.
     * @param   uDestMode       The destination mode.
     * @param   uDeliveryMode   The delivery mode.
     * @param   uVector         The interrupt vector.
     * @param   uPolarity       The interrupt line polarity.
     * @param   uTriggerMode    The trigger mode.
     * @param   uIoApicPin      Set to the pin which generated the interrupt message if originating from the IO-APIC.
     *                          UINT8_MAX if not originating from a pin, i.e. an interrupt message originating from
     *                          a device.
     * @param   uSrcTag         The interrupt source tag (debugging).
     */
    DECLRCCALLBACKMEMBER(int, pfnBusDeliver, (PVMCC pVM, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode, uint8_t uVector,
                                              uint8_t uPolarity, uint8_t uTriggerMode, uint8_t uIoApicPin, uint32_t uSrcTag));

    /**
     * Sets the End-Of-Interrupt (EOI) register.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu                   The cross context virtual CPU structure.
     * @param   uEoi                    The EOI value.
     * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during
     *                                  this write.
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnSetEoi, (PVMCPUCC pVCpu, uint32_t uEoi, bool fForceX2ApicBehaviour));

    /**
     * Imports the APIC state.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnImportState, (PVMCPUCC pVCpu));

    /**
     * Exports the APIC state.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu   The cross context virtual CPU structure.
     */
    DECLRCCALLBACKMEMBER(VBOXSTRICTRC, pfnExportState, (PVMCPUCC pVCpu));

    /** @name Reserved for future (MBZ).
     * @{ */
    DECLRCCALLBACKMEMBER(int, pfnReserved0, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved1, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved2, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved3, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved4, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved5, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved6, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved7, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved8, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved9, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved10, (void));
    /** @} */
} PDMAPICBACKENDRC;
/** Pointer to raw-mode context APIC backend. */
typedef RCPTRTYPE(struct PDMAPICBACKENDRC *) PPDMAPICBACKENDRC;
/** Const pointer to raw-mode context APIC backend. */
typedef RCPTRTYPE(const struct PDMAPICBACKENDRC *) PCPDMAPICBACKENDRC;
AssertCompileSizeAlignment(PDMAPICBACKENDRC, 8);
AssertCompile(sizeof(PDMAPICBACKENDR3) == sizeof(PDMAPICBACKENDR0));

/** @typedef PDMAPICBACKENDR3
 * A current context PDM APIC backend. */
/** @typedef PPDMAPICBACKENDR3
 * Pointer to a current context PDM APIC backend. */
/** @typedef PCPDMAPICBACKENDR3
 * Pointer to a const current context PDM APIC backend. */
#if defined(IN_RING3) || defined(DOXYGEN_RUNNING)
typedef PDMAPICBACKENDR3        PDMAPICBACKEND;
typedef PPDMAPICBACKENDR3       PPDMAPICBACKEND;
typedef PCPDMAPICBACKENDR3      PCPDMAPICBACKEND;
#elif defined(IN_RING0)
typedef PDMAPICBACKENDR0        PDMAPICBACKEND;
typedef PPDMAPICBACKENDR0       PPDMAPICBACKEND;
typedef PCPDMAPICBACKENDR0      PCPDMAPICBACKEND;
#elif defined(IN_RC)
typedef PDMAPICBACKENDRC        PDMAPICBACKEND;
typedef PPDMAPICBACKENDRC       PPDMAPICBACKEND;
typedef PCPDMAPICBACKENDRC      PCPDMAPICBACKEND;
#else
# error "Not IN_RING3, IN_RING0 or IN_RC"
#endif

VMM_INT_DECL(int)           PDMApicRegisterBackend(PVMCC pVM, PDMAPICBACKENDTYPE enmBackendType, PCPDMAPICBACKEND pBackend);

VMM_INT_DECL(void)          PDMApicUpdatePendingInterrupts(PVMCPUCC pVCpu);
VMM_INT_DECL(int)           PDMApicGetTpr(PCVMCPUCC pVCpu, uint8_t *pu8Tpr, bool *pfPending, uint8_t *pu8PendingIntr);
VMM_INT_DECL(int)           PDMApicSetTpr(PVMCPUCC pVCpu, uint8_t u8Tpr);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicSetIcr(PVMCPUCC pVCpu, uint64_t uIcr);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicImportState(PVMCPUCC pVCpu);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicExportState(PVMCPUCC pVCpu);
VMM_INT_DECL(bool)          PDMApicIsEnabled(PCVMCPUCC pVCpu);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicReadMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicWriteMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value);
VMM_INT_DECL(int)           PDMApicGetTimerFreq(PVMCC pVM, uint64_t *pu64Value);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicSetLocalInterrupt(PVMCPUCC pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ);
VMM_INT_DECL(uint64_t)      PDMApicGetBaseMsrNoCheck(PCVMCPUCC pVCpu);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicGetBaseMsr(PVMCPUCC pVCpu, uint64_t *pu64Value);
VMM_INT_DECL(int)           PDMApicSetBaseMsr(PVMCPUCC pVCpu, uint64_t u64BaseMsr);
VMM_INT_DECL(int)           PDMApicGetInterrupt(PVMCPUCC pVCpu, uint8_t *pu8Vector, uint32_t *puSrcTag);
VMM_INT_DECL(int)           PDMApicBusDeliver(PVMCC pVM, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode, uint8_t uVector,
                                              uint8_t uPolarity, uint8_t uTriggerMode, uint8_t uIoApicPin, uint32_t uTagSrc);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicUpdateStateAfterWrite(PVMCPUCC pVCpu, uint16_t offApicReg);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicSetEoiFast(PVMCPUCC pVCpu, uint8_t uIsrVector);
#ifdef IN_RING0
VMM_INT_DECL(int)           PDMR0ApicGetApicPageForCpu(PCVMCPUCC pVCpu, PRTHCPHYS pHCPhys, PRTR0PTR pR0Ptr, PRTR3PTR pR3Ptr);
#endif

/** @name Hyper-V interface (Ring-3 and all-context API).
 * @{ */
#ifdef IN_RING3
VMMR3_INT_DECL(int)         PDMR3ApicSetHvCompatMode(PVM pVM, bool fHyperVCompatMode);
#endif
VMM_INT_DECL(void)          PDMApicHvSendInterrupt(PVMCPUCC pVCpu, uint8_t uVector, bool fAutoEoi, XAPICTRIGGERMODE enmTriggerMode);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicHvSetTpr(PVMCPUCC pVCpu, uint8_t uTpr);
VMM_INT_DECL(uint8_t)       PDMApicHvGetTpr(PVMCPUCC pVCpu);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicHvSetIcr(PVMCPUCC pVCpu, uint64_t uIcr);
VMM_INT_DECL(uint64_t)      PDMApicHvGetIcr(PVMCPUCC pVCpu);
VMM_INT_DECL(VBOXSTRICTRC)  PDMApicHvSetEoi(PVMCPUCC pVCpu, uint32_t uEoi);
/** @} */

#ifdef IN_RING3
/** @defgroup grp_pdm_apic_r3  The PDM APIC Host Context Ring-3 API
 * @{
 */
VMMR3_INT_DECL(void)        PDMR3ApicInitIpi(PVMCPU pVCpu);
/** @} */
#endif /* IN_RING3 */

RT_C_DECLS_END

/** @} */

#endif /* !VBOX_INCLUDED_vmm_pdmapic_h */

