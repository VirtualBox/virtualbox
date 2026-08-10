/* $Id: APICR3Nem-win-x86.cpp 114734 2026-07-21 06:14:23Z alexander.eichner@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller - NEM Hyper-V backend.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_APIC
#include <iprt/nt/nt-and-windows.h>
#include <iprt/nt/hyperv.h>
#include <iprt/mem.h>
#include <WinHvPlatform.h>

#include "APICHvInternal.h"
#include <VBox/log.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/vmcpuset.h>

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_STATISTICS
# define X2APIC_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Ia32X2ApicN, kCpumMsrWrFn_Ia32X2ApicN, 0, 0, 0, 0, 0, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
# define X2APIC_MSRRANGE_INVALID(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_WriteOnly, kCpumMsrWrFn_ReadOnly, 0, 0, 0, 0, UINT64_MAX /*fWrGpMask*/, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define X2APIC_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Ia32X2ApicN, kCpumMsrWrFn_Ia32X2ApicN, 0, 0, 0, 0, 0, a_szName }
# define X2APIC_MSRRANGE_INVALID(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_WriteOnly, kCpumMsrWrFn_ReadOnly, 0, 0, 0, 0, UINT64_MAX /*fWrGpMask*/, a_szName }
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Hyper-V APIC PDM instance data (per-VM).
 */
typedef struct HVAPICDEV
{
    /** Pointer to the PDM device instance. */
    PPDMDEVINSR3            pDevIns;
} HVAPICDEV;
/** Pointer to a Hyper-V APIC PDM instance data. */
typedef HVAPICDEV *PHVAPICDEV;
/** Pointer to a const Hyper-V APIC PDM instance data. */
typedef HVAPICDEV const *PCHVAPICDEV;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern decltype(WHvGetVirtualProcessorState)                     *g_pfnWHvGetVirtualProcessorState;
extern decltype(WHvSetVirtualProcessorState)                     *g_pfnWHvSetVirtualProcessorState;
extern decltype(WHvGetVirtualProcessorInterruptControllerState2) *g_pfnWHvGetVirtualProcessorInterruptControllerState2;
extern decltype(WHvSetVirtualProcessorInterruptControllerState2) *g_pfnWHvSetVirtualProcessorInterruptControllerState2;
extern decltype(WHvRequestInterrupt)                             *g_pfnWHvRequestInterrupt;
extern decltype(WHvSetVirtualProcessorRegisters)                 *g_pfnWHvSetVirtualProcessorRegisters;
extern decltype(WHvGetVirtualProcessorRegisters)                 *g_pfnWHvGetVirtualProcessorRegisters;

/*
 * Let the preprocessor alias the APIs to import variables for better autocompletion.
 */
#ifndef IN_SLICKEDIT
# define WHvGetVirtualProcessorState                     g_pfnWHvGetVirtualProcessorState
# define WHvSetVirtualProcessorState                     g_pfnWHvSetVirtualProcessorState
# define WHvGetVirtualProcessorInterruptControllerState2 g_pfnWHvGetVirtualProcessorInterruptControllerState2
# define WHvSetVirtualProcessorInterruptControllerState2 g_pfnWHvSetVirtualProcessorInterruptControllerState2
# define WHvRequestInterrupt                             g_pfnWHvRequestInterrupt
# define WHvSetVirtualProcessorRegisters                 g_pfnWHvSetVirtualProcessorRegisters
# define WHvGetVirtualProcessorRegisters                 g_pfnWHvGetVirtualProcessorRegisters
#endif

/**
 * MSR range supported by the x2APIC.
 * See Intel spec. 10.12.2 "x2APIC Register Availability".
 */
static CPUMMSRRANGE const g_MsrRange_x2Apic = X2APIC_MSRRANGE(MSR_IA32_X2APIC_START, MSR_IA32_X2APIC_END, "x2APIC range");
static CPUMMSRRANGE const g_MsrRange_x2Apic_Invalid = X2APIC_MSRRANGE_INVALID(MSR_IA32_X2APIC_START, MSR_IA32_X2APIC_END, "x2APIC range invalid");
#undef X2APIC_MSRRANGE
#undef X2APIC_MSRRANGE_GP


/*
 * Instantiate the APIC all-context common code.
 */
#define VMM_APIC_TEMPLATE_ALL_COMMON
#define VMM_APIC_TEMPLATE_R3_COMMON
#include "../VMMAll/target-x86/APICAllCommon-x86.cpp.h"
#undef VMM_APIC_TEMPLATE_ALL_COMMON
#undef VMM_APIC_TEMPLATE_R3_COMMON


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnIsEnabled}
 */
static DECLCALLBACK(bool) apicR3HvIsEnabled(PCVMCPUCC pVCpu)
{
    /*
     * We should never end up here as this is called only from the VMX and SVM
     * code in R0 which we don't run if this is active. However, we still call
     * this statically (within this file).
     */
    PCHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    return RT_BOOL(pHvApicCpu->uApicBaseMsr & MSR_IA32_APICBASE_EN);
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnInitIpi}
 */
static DECLCALLBACK(void) apicR3HvInitIpi(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    apicCommonInitIpi(pVCpu);
}


static VBOXSTRICTRC apicR3HvSendIntr(PVMCC pVM, uint8_t uVector, XAPICDELIVERYMODE enmDeliveryMode, XAPICDESTMODE enmDestMode,
                                     XAPICTRIGGERMODE enmTriggerMode, PCVMCPUSET pDestCpuSet)
{
#if 0
    WHV_INTERRUPT_CONTROL Control;
    RT_ZERO(Control);
    Control.Type            = enmDeliveryMode;
    Control.DestinationMode = enmDestMode;
    Control.TriggerMode     = enmTriggerMode;
    Control.Vector          = uVector;

    PHVAPIC pHvApic = VM_TO_HVAPIC(pVM);
    VMCPUID const cCpus = pVM->cCpus;
    switch (enmDeliveryMode)
    {
        case XAPICDELIVERYMODE_INIT:
        {
            Assert(!VMCPUSET_IS_EMPTY(pDestCpuSet));
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Control.Destination = idCpu;
                    HRESULT const hrc = WHvRequestInterrupt(pHvApic->hPartition, &Control, sizeof(Control));
                    if (FAILED(hrc))
                    {
                        AssertMsgFailed(("Failed to send INIT IPI to CPU %u\n", idCpu));
                        return VERR_APIC_INTR_DISCARDED;
                    }
                }
            break;
        }

        case XAPICDELIVERYMODE_STARTUP:
        {
            Assert(!VMCPUSET_IS_EMPTY(pDestCpuSet));
            static const WHV_REGISTER_NAME s_Name = WHvRegisterInternalActivityState;
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Control.Destination = idCpu;
                    HRESULT hrc = WHvRequestInterrupt(pHvApic->hPartition, &Control, sizeof(Control));
                    if (FAILED(hrc))
                    {
                        AssertMsgFailed(("Failed to send SIPI IPI to CPU %u\n", idCpu));
                        return VERR_APIC_INTR_DISCARDED;
                    }
                    else
                    {
                        WHV_REGISTER_VALUE Reg;
                        RT_ZERO(Reg);
                        {
                            hrc = WHvGetVirtualProcessorRegisters(pHvApic->hPartition, idCpu, &s_Name, 1, &Reg);
                            AssertLogRelMsgReturn(SUCCEEDED(hrc),
                                                  ("WHvGetVirtualProcessorRegisters(%p, 0,{WHvRegisterInternalActivityState}, 1,) -> %Rhrc (Last=%#x/%u)\n",
                                                  pHvApic->hPartition, idCpu, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()),
                                                  VERR_APIC_IPE_0);
                        }

                        LogRelMax(100, ("Internal Activity State %u: %#RX64 -> 0\n", idCpu, Reg.InternalActivity.AsUINT64));
                        if (Reg.InternalActivity.StartupSuspend == 1)
                        {
                            LogRelMax(100, ("Removing StartupSuspend state %u\n", idCpu));
                            Reg.InternalActivity.StartupSuspend = 0;
                            hrc = WHvSetVirtualProcessorRegisters(pHvApic->hPartition, idCpu, &s_Name, 1, &Reg);
                            AssertLogRelMsgReturn(SUCCEEDED(hrc),
                                                  ("WHvSetVirtualProcessorRegisters(%p, 0,{WHvRegisterInternalActivityState}, 1,) -> %Rhrc (Last=%#x/%u)\n",
                                                  pHvApic->hPartition, idCpu, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()),
                                                  VERR_APIC_IPE_1);
                        }
                    }
                }
            break;
        }

        case XAPICDELIVERYMODE_FIXED:
        case XAPICDELIVERYMODE_LOWEST_PRIO:
        case XAPICDELIVERYMODE_SMI:
        case XAPICDELIVERYMODE_NMI:
        case XAPICDELIVERYMODE_EXTINT:
        default:
        {
            AssertReleaseMsgFailed(("APIC/Whv: apicSendIntr: Unexpected delivery mode %#x\n", enmDeliveryMode));
            break;
        }
    }

    return VINF_SUCCESS;

#else
    NOREF(enmTriggerMode);
    NOREF(enmDestMode);

    /** @todo Merge with apicSendIntr. */
    VMCPUID const cCpus = pVM->cCpus;
    switch (enmDeliveryMode)
    {
        case XAPICDELIVERYMODE_INIT:
        {
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Log2(("APIC/WHv: apicSendIntr: Issuing INIT to VCPU%u\n", idCpu));
                    VMMR3SendInitIpi(pVM, idCpu);
                }
            break;
        }

        case XAPICDELIVERYMODE_STARTUP:
        {
            static const WHV_REGISTER_NAME s_Name = WHvRegisterInternalActivityState;
            WHV_REGISTER_VALUE Reg;
            RT_ZERO(Reg);

            PHVAPIC pHvApic = VM_TO_HVAPIC(pVM);
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Log2(("APIC/WHv: apicSendIntr: Issuing SIPI to VCPU%u\n", idCpu));
                    VMMR3SendStartupIpi(pVM, idCpu, uVector);

                    HRESULT const hrc = WHvSetVirtualProcessorRegisters(pHvApic->hPartition, idCpu, &s_Name, 1, &Reg);
                    AssertLogRelMsgReturn(SUCCEEDED(hrc),
                                          ("WHvSetVirtualProcessorRegisters(%p, 0,{WHvRegisterInternalActivityState}, 1,) -> %Rhrc (Last=%#x/%u)\n",
                                          pHvApic->hPartition, idCpu, hrc, RTNtLastStatusValue(), RTNtLastErrorValue()),
                                          VERR_APIC_IPE_1);
                }
            break;
        }

        case XAPICDELIVERYMODE_FIXED:
        case XAPICDELIVERYMODE_LOWEST_PRIO:
        case XAPICDELIVERYMODE_SMI:
        case XAPICDELIVERYMODE_NMI:
        case XAPICDELIVERYMODE_EXTINT:
        default:
        {
            AssertReleaseMsgFailed(("APIC/Whv: apicSendIntr: Unexpected delivery mode %#x\n", enmDeliveryMode));
            break;
        }
    }

    return VINF_SUCCESS;
#endif
}


/**
 * Sends an Interprocessor Interrupt (IPI) using values from the Interrupt
 * Command Register (ICR).
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static VBOXSTRICTRC apicR3HvSendIpi(PVMCPUCC pVCpu)
{
    /** @todo Merge with apicSendIpi. */
    VMCPU_ASSERT_EMT(pVCpu);

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PCHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    PXAPICPAGE  pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    XAPICDELIVERYMODE const  enmDeliveryMode  = (XAPICDELIVERYMODE)pXApicPage->icr_lo.u.u3DeliveryMode;
    XAPICDESTMODE const      enmDestMode      = (XAPICDESTMODE)pXApicPage->icr_lo.u.u1DestMode;
    XAPICINITLEVEL const     enmInitLevel     = (XAPICINITLEVEL)pXApicPage->icr_lo.u.u1Level;
    XAPICTRIGGERMODE const   enmTriggerMode   = (XAPICTRIGGERMODE)pXApicPage->icr_lo.u.u1TriggerMode;
    XAPICDESTSHORTHAND const enmDestShorthand = (XAPICDESTSHORTHAND)pXApicPage->icr_lo.u.u2DestShorthand;
    uint8_t const            uVector          = pXApicPage->icr_lo.u.u8Vector;

    Assert(   enmDeliveryMode == XAPICDELIVERYMODE_INIT
           || enmDeliveryMode == XAPICDELIVERYMODE_STARTUP);

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    /*
     * INIT Level De-assert is not support on Pentium 4 and Xeon processors.
     * Apparently, this also applies to NMI, SMI, lowest-priority and fixed delivery modes,
     * see @bugref{8245#c116}.
     *
     * See AMD spec. 16.5 "Interprocessor Interrupts (IPI)" for a table of valid ICR combinations.
     */
    if (   enmTriggerMode  == XAPICTRIGGERMODE_LEVEL
        && enmInitLevel    == XAPICINITLEVEL_DEASSERT
        && (   enmDeliveryMode == XAPICDELIVERYMODE_FIXED
            || enmDeliveryMode == XAPICDELIVERYMODE_LOWEST_PRIO
            || enmDeliveryMode == XAPICDELIVERYMODE_SMI
            || enmDeliveryMode == XAPICDELIVERYMODE_NMI
            || enmDeliveryMode == XAPICDELIVERYMODE_INIT))
    {
        LogRelMax(10, ("APIC/WHv%u: %#x delivery-mode level de-assert unsupported, ignoring!\n", pVCpu->idCpu, enmDeliveryMode));
        return VINF_SUCCESS;
    }
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

    /*
     * The destination and delivery modes are ignored/by-passed when a destination shorthand is specified.
     * See Intel spec. 10.6.2.3 "Broadcast/Self Delivery Mode".
     */
    VMCPUSET DestCpuSet;
    switch (enmDestShorthand)
    {
        case XAPICDESTSHORTHAND_NONE:
        {
            uint32_t fDest;
            uint32_t fBroadcastMask;
            if (XAPIC_IN_X2APIC_MODE(pHvApicCpu->uApicBaseMsr))
            {
                fDest          = pXApicPage->icr_hi.all.u32IcrHi;
                fBroadcastMask = X2APIC_ID_BROADCAST_MASK;
            }
            else
            {
                fDest          = pXApicPage->icr_hi.u.u8Dest;
                fBroadcastMask = XAPIC_ID_BROADCAST_MASK;
            }
            apicCommonGetDestCpuSet(pVM, fDest, fBroadcastMask, enmDestMode, enmDeliveryMode, &DestCpuSet);
            break;
        }

        case XAPICDESTSHORTHAND_SELF:
            VMCPUSET_EMPTY(&DestCpuSet);
            VMCPUSET_ADD(&DestCpuSet, pVCpu->idCpu);
            break;

        case XAPICDESTSHORTHAND_ALL_INCL_SELF:
            VMCPUSET_FILL(&DestCpuSet);
            break;

        case XAPICDESTSHORTHAND_ALL_EXCL_SELF:
            VMCPUSET_FILL(&DestCpuSet);
            VMCPUSET_DEL(&DestCpuSet, pVCpu->idCpu);
            break;
    }

    return apicR3HvSendIntr(pVM, uVector, enmDeliveryMode, enmDestMode, enmTriggerMode, &DestCpuSet);
}


static void apicR3HvHintTimerFreq(PPDMDEVINS pDevIns, PHVAPICCPU pHvApicCpu, uint32_t uInitialCount, uint8_t uTimerShift)
{
    Assert(pHvApicCpu);

    if (   pHvApicCpu->uHintedTimerInitialCount != uInitialCount
        || pHvApicCpu->uHintedTimerShift        != uTimerShift)
    {
        uint32_t uHz;
        if (uInitialCount)
        {
            uint64_t cTicksPerPeriod = (uint64_t)uInitialCount << uTimerShift;
            uHz = PDMDevHlpTimerGetFreq(pDevIns, pHvApicCpu->hTimer) / cTicksPerPeriod;
        }
        else
            uHz = 0;

        PDMDevHlpTimerSetFrequencyHint(pDevIns, pHvApicCpu->hTimer, uHz);
        pHvApicCpu->uHintedTimerInitialCount = uInitialCount;
        pHvApicCpu->uHintedTimerShift = uTimerShift;
    }
}


/**
 * Starts the APIC timer.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uInitialCount   The timer's Initial-Count Register (ICR), must be >
 *                          0.
 * @thread  Any.
 *
 * @remarks No need to check whether the timer is active prior to calling this
 *          because the underlying TM API handles this, see "TMTIMERSTATE_ACTIVE" in
 *          tmTimerVirtualSyncSetRelative().
 */
static void apicR3HvStartTimer(PVMCPUCC pVCpu, uint32_t uInitialCount)
{
    Assert(pVCpu);
    PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    PPDMDEVINS pDevIns    = VMCPU_TO_DEVINS(pVCpu);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pHvApicCpu->hTimer));
    Assert(uInitialCount > 0);

    PCXAPICPAGE    pXApicPage   = VMCPU_TO_CXAPICPAGE(pVCpu);
    uint8_t  const uTimerShift  = apicCommonGetTimerShift(pXApicPage);
    uint64_t const cTicksToNext = (uint64_t)uInitialCount << uTimerShift;

    Log2(("APIC%u: apicStartTimer: uInitialCount=%#RX32 uTimerShift=%u cTicksToNext=%RU64\n", pVCpu->idCpu, uInitialCount,
          uTimerShift, cTicksToNext));

    /*
     * The assumption here is that the timer doesn't tick during this call
     * and thus setting a relative time to fire next is accurate. The advantage
     * however is updating u64TimerInitial 'atomically' while setting the next
     * tick.
     */
    PDMDevHlpTimerSetRelative(pDevIns, pHvApicCpu->hTimer, cTicksToNext, &pHvApicCpu->u64TimerInitial);
    apicR3HvHintTimerFreq(pDevIns, pHvApicCpu, uInitialCount, uTimerShift);
}


static void apicR3HvStopTimer(PVMCPUCC pVCpu)
{
    Assert(pVCpu);
    PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    PPDMDEVINS pDevIns    = VMCPU_TO_DEVINS(pVCpu);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pHvApicCpu->hTimer));

    Log2(("APIC%u: apicStopTimer\n", pVCpu->idCpu));

    PDMDevHlpTimerStop(pDevIns, pHvApicCpu->hTimer); /* This will reset the hint, no need to explicitly call TMTimerSetFrequencyHint(). */
    pHvApicCpu->uHintedTimerInitialCount = 0;
    pHvApicCpu->uHintedTimerShift = 0;
}


/**
 * @callback_method_impl{FNTMTIMERDEV}
 *
 * @note    pvUser points to the VMCPU.
 *
 * @remarks Currently this function is invoked on the last EMT, see @c
 *          idTimerCpu in tmR3TimerCallback().  However, the code does -not-
 *          rely on this and is designed to work with being invoked on any
 *          thread.
 */
static DECLCALLBACK(void) apicR3HvTimerCallback(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PVMCPU      pVCpu      = (PVMCPU)pvUser;
    PHVAPICCPU  pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pHvApicCpu->hTimer));
    Assert(pVCpu);
    LogFlow(("APIC%u: apicR3TimerCallback\n", pVCpu->idCpu));
    RT_NOREF(pDevIns, hTimer, pHvApicCpu);

    PXAPICPAGE     pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    uint32_t const uLvtTimer  = pXApicPage->lvt_timer.all.u32LvtTimer;
    if (!XAPIC_LVT_IS_MASKED(uLvtTimer))
    {
#if 0
        uint8_t uVector = XAPIC_LVT_GET_VECTOR(uLvtTimer);
        Log2(("APIC%u: apicR3TimerCallback: Raising timer interrupt. uVector=%#x\n", pVCpu->idCpu, uVector));

        PHVAPIC pHvApic = VM_TO_HVAPIC(pVCpu->CTX_SUFF(pVM));
        WHV_INTERRUPT_CONTROL Control; RT_ZERO(Control);
        Control.Type            = WHvX64InterruptTypeFixed;
        Control.DestinationMode = WHvX64InterruptDestinationModePhysical;
        Control.TriggerMode     = WHvX64InterruptTriggerModeEdge;
        Control.Destination     = pVCpu->idCpu;
        Control.Vector          = uVector;
        HRESULT const hrc = WHvRequestInterrupt(pHvApic->hPartition, &Control, sizeof(Control));
        if (FAILED(hrc))
        {
            AssertMsgFailed(("APIC/WHv: Delivering timer interrupt failed: %Rhrc (Last=%#x/%u)\n", hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            LogRelMax(10, ("APIC/WHv: Delivering timer interrupt failed: %Rhrc (Last=%#x/%u)\n", hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
            return;
        }
#endif
    }

    XAPICTIMERMODE enmTimerMode = XAPIC_LVT_GET_TIMER_MODE(uLvtTimer);
    switch (enmTimerMode)
    {
        case XAPICTIMERMODE_PERIODIC:
        {
            STAM_COUNTER_INC(&pHvApicCpu->StatTimerPeriodic);

            /* The initial-count register determines if the periodic timer is re-armed. */
            uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
            pXApicPage->timer_ccr.u32CurrentCount = uInitialCount;
            if (uInitialCount)
            {
                Log2(("APIC%u: apicR3TimerCallback: Re-arming timer. uInitialCount=%#RX32\n", pVCpu->idCpu, uInitialCount));
                apicR3HvStartTimer(pVCpu, uInitialCount);
            }
            break;
        }

        case XAPICTIMERMODE_ONESHOT:
        {
            STAM_COUNTER_INC(&pHvApicCpu->StatTimerOneShot);
            pXApicPage->timer_ccr.u32CurrentCount = 0;
            break;
        }

        case XAPICTIMERMODE_TSC_DEADLINE:
        {
            /** @todo implement TSC deadline. */
            STAM_COUNTER_INC(&pHvApicCpu->StatTimerTscDeadline);
            AssertMsgFailed(("APIC: TSC deadline mode unimplemented\n"));
            break;
        }
    }
}


static VBOXSTRICTRC apicR3HvGetTimerCcr(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, int rcBusy, uint32_t *puValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(puValue);

    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    *puValue = 0;

    /* In TSC-deadline mode, CCR returns 0, see Intel spec. 10.5.4.1 "TSC-Deadline Mode". */
    if (pXApicPage->lvt_timer.u.u2TimerMode == XAPIC_TIMER_MODE_TSC_DEADLINE)
        return VINF_SUCCESS;

    /* If the initial-count register is 0, CCR returns 0 as it cannot exceed the ICR. */
    uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
    if (!uInitialCount)
        return VINF_SUCCESS;

    /*
     * Reading the virtual-sync clock requires locking its timer because it's not
     * a simple atomic operation, see tmVirtualSyncGetEx().
     *
     * We also need to lock before reading the timer CCR, see apicR3TimerCallback().
     */
    PCHVAPICCPU     pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    TMTIMERHANDLE   hTimer     = pHvApicCpu->hTimer;

    VBOXSTRICTRC rc = PDMDevHlpTimerLockClock(pDevIns, hTimer, rcBusy);
    if (rc == VINF_SUCCESS)
    {
        /* If the current-count register is 0, it implies the timer expired. */
        uint32_t const uCurrentCount = pXApicPage->timer_ccr.u32CurrentCount;
        if (uCurrentCount)
        {
            uint64_t const cTicksElapsed = PDMDevHlpTimerGet(pDevIns, hTimer) - pHvApicCpu->u64TimerInitial;
            PDMDevHlpTimerUnlockClock(pDevIns, hTimer);
            uint8_t  const uTimerShift   = apicCommonGetTimerShift(pXApicPage);
            uint64_t const uDelta        = cTicksElapsed >> uTimerShift;
            if (uInitialCount > uDelta)
                *puValue = uInitialCount - uDelta;
        }
        else
            PDMDevHlpTimerUnlockClock(pDevIns, hTimer);
    }
    return rc;
}


static VBOXSTRICTRC apicR3HvSetTimerIcr(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, int rcBusy, uint32_t uInitialCount)
{
    VMCPU_ASSERT_EMT(pVCpu);

    PHVAPIC    pHvApic    = VM_TO_HVAPIC(pVCpu->CTX_SUFF(pVM));
    PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    Log2(("APIC%u: apicSetTimerIcr: uInitialCount=%#RX32\n", pVCpu->idCpu, uInitialCount));
    //STAM_COUNTER_INC(&HvApicCpu->StatTimerIcrWrite);      /** @todo STAM */

    /* In TSC-deadline mode, timer ICR writes are ignored, see Intel spec. 10.5.4.1 "TSC-Deadline Mode". */
    if (   pHvApic->fSupportsTscDeadline
        && pXApicPage->lvt_timer.u.u2TimerMode == XAPIC_TIMER_MODE_TSC_DEADLINE)
        return VINF_SUCCESS;

    /*
     * The timer CCR may be modified by apicR3HvTimerCallback() in parallel,
     * so obtain the lock -before- updating it here to be consistent with the
     * timer ICR. We rely on CCR being consistent in apicR3HvGetTimerCcr().
     */
    TMTIMERHANDLE hTimer = pHvApicCpu->hTimer;
    VBOXSTRICTRC rc = PDMDevHlpTimerLockClock(pDevIns, hTimer, rcBusy);
    if (rc == VINF_SUCCESS)
    {
        pXApicPage->timer_icr.u32InitialCount = uInitialCount;
        pXApicPage->timer_ccr.u32CurrentCount = uInitialCount;
        if (uInitialCount)
            apicR3HvStartTimer(pVCpu, uInitialCount);
        else
            apicR3HvStopTimer(pVCpu);
        PDMDevHlpTimerUnlockClock(pDevIns, hTimer);
    }
    return rc;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetBaseMsr}
 */
static DECLCALLBACK(int) apicR3HvSetBaseMsr(PVMCPUCC pVCpu, uint64_t u64BaseMsr)
{
    RT_NOREF(pVCpu, u64BaseMsr);
    AssertReleaseMsgFailed(("idCpu=%u u64BaseMsr=%#RX64\n", pVCpu->idCpu, u64BaseMsr));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetBaseMsrNoCheck}
 */
static DECLCALLBACK(uint64_t) apicR3HvGetBaseMsrNoCheck(PCVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PCHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    return pHvApicCpu->uApicBaseMsr;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetBaseMsr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvGetBaseMsr(PVMCPUCC pVCpu, uint64_t *pu64Value)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PCHVAPIC pHvApic = VM_TO_HVAPIC(pVCpu->CTX_SUFF(pVM));
    if (pHvApic->enmMaxMode != PDMAPICMODE_NONE)
    {
        *pu64Value = apicR3HvGetBaseMsrNoCheck(pVCpu);
        return VINF_SUCCESS;
    }
    return VERR_CPUM_RAISE_GP_0;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnReadRaw32}
 */
static DECLCALLBACK(uint32_t) apicR3HvReadRaw32(PCVMCPUCC pVCpu, uint16_t offReg)
{
    RT_NOREF(pVCpu, offReg);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return 0;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnReadMsr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvReadMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(u32Reg >= MSR_IA32_X2APIC_ID && u32Reg <= MSR_IA32_X2APIC_SELF_IPI);
    Assert(pu64Value);

    RT_NOREF(pVCpu, u32Reg, pu64Value);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnWriteMsr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvWriteMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(u32Reg >= MSR_IA32_X2APIC_ID && u32Reg <= MSR_IA32_X2APIC_SELF_IPI);

    RT_NOREF(pVCpu, u32Reg, u64Value);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetTpr}
 */
static DECLCALLBACK(int) apicR3HvSetTpr(PVMCPUCC pVCpu, uint8_t u8Tpr, bool fForceX2ApicBehaviour)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    RT_NOREF(fForceX2ApicBehaviour);
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    pXApicPage->tpr.u8Tpr = u8Tpr;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetTpr}
 */
static DECLCALLBACK(int) apicR3HvGetTpr(PCVMCPUCC pVCpu, uint8_t *pu8Tpr, bool *pfPending, uint8_t *pu8PendingIntr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    RT_NOREF(pfPending, pu8PendingIntr);
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    *pu8Tpr = pXApicPage->tpr.u8Tpr;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetIcrNoCheck}
 */
static DECLCALLBACK(uint64_t) apicR3HvGetIcrNoCheck(PVMCPUCC pVCpu)
{
    RT_NOREF(pVCpu);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return 0;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetIcr}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvSetIcr(PVMCPUCC pVCpu, uint64_t u64Icr, int rcRZ)
{
    VMCPU_ASSERT_EMT(pVCpu);
    RT_NOREF(rcRZ);

#if 1
    /* Validate. */
    uint32_t const uLo = RT_LO_U32(u64Icr);
    uint32_t const uHi = RT_HI_U32(u64Icr);
    if (RT_LIKELY(!(uLo & ~XAPIC_ICR_LO_WR_VALID)))
    {
        PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
        pXApicPage->icr_hi.all.u32IcrHi = uHi & XAPIC_ICR_HI_WR_VALID;
        pXApicPage->icr_lo.all.u32IcrLo = uLo & XAPIC_ICR_LO_WR_VALID;
        Assert(!(pXApicPage->icr_hi.all.u32IcrHi & ~XAPIC_ICR_HI_WR_VALID));
        Assert(!(pXApicPage->icr_lo.u.u1DeliveryStatus));
        Assert(!(pXApicPage->esr.all.u32Errors));
        return apicR3HvSendIpi(pVCpu);
    }

    AssertMsgFailed(("Unexpected ICR write failed (%#RX64) in CPU %u\n", u64Icr, pVCpu->idCpu));
    return VERR_APIC_WRITE_INVALID;
#else
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    uint32_t const uLo = RT_LO_U32(u64Icr);
    uint32_t const uHi = RT_HI_U32(u64Icr);
    if (RT_LIKELY(!(uLo & ~XAPIC_ICR_LO_WR_VALID)))
    {
        pXApicPage->icr_hi.all.u32IcrHi = uHi & XAPIC_ICR_HI_WR_VALID;
        pXApicPage->icr_lo.all.u32IcrLo = uLo & XAPIC_ICR_LO_WR_VALID;

        XAPICDELIVERYMODE const  enmDeliveryMode  = (XAPICDELIVERYMODE)pXApicPage->icr_lo.u.u3DeliveryMode;
        XAPICDESTMODE const      enmDestMode      = (XAPICDESTMODE)pXApicPage->icr_lo.u.u1DestMode;
        XAPICTRIGGERMODE const   enmTriggerMode   = (XAPICTRIGGERMODE)pXApicPage->icr_lo.u.u1TriggerMode;
        uint8_t const            uDest            = pXApicPage->icr_hi.u.u8Dest;
        uint8_t const            uVector          = pXApicPage->icr_lo.u.u8Vector;

        Assert(   enmDeliveryMode == XAPICDELIVERYMODE_INIT
               || enmDeliveryMode == XAPICDELIVERYMODE_STARTUP);
        AssertCompile(WHvX64InterruptTypeInit == XAPICDELIVERYMODE_INIT);
        AssertCompile(WHvX64InterruptTypeSipi == XAPICDELIVERYMODE_STARTUP);

        PHVAPIC pHvApic = VM_TO_HVAPIC(pVCpu->CTX_SUFF(pVM));
        WHV_INTERRUPT_CONTROL Control; RT_ZERO(Control);
        Control.Type            = enmDeliveryMode;
        Control.DestinationMode = enmDestMode;
        Control.TriggerMode     = enmTriggerMode;
        Control.Destination     = uDest;
        Control.Vector          = uVector;

        HRESULT const hrc = WHvRequestInterrupt(pHvApic->hPartition, &Control, sizeof(Control));
        if (SUCCEEDED(hrc))
            return VINF_SUCCESS;

        AssertMsgFailed(("Failed to send IPI from CPU %u. Hi=%#RX32 Lo=%#RX32\n", pVCpu->idCpu, uHi, uLo));
        return VERR_APIC_INTR_DISCARDED;
    }

    AssertMsgFailed(("Unexpected ICR write failed (%#RX64) in CPU %u\n", u64Icr, pVCpu->idCpu));
    return VERR_APIC_WRITE_INVALID;
#endif
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetTimerFreq}
 */
static DECLCALLBACK(int) apicR3HvGetTimerFreq(PVMCC pVM, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    Assert(pVM);
    AssertPtrReturn(pu64Value, VERR_INVALID_PARAMETER);

    RT_NOREF(pVM, pu64Value);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VERR_NOT_IMPLEMENTED;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetLocalInterrupt}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvSetLocalInterrupt(PVMCPUCC pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ)
{
    AssertReturn(u8Pin <= 1, VERR_INVALID_PARAMETER);
    AssertReturn(u8Level <= 1, VERR_INVALID_PARAMETER);
    RT_NOREF(rcRZ);

    /* The rest is handled in the NEM backend. */
    if (u8Level)
        VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC);
    else
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_PIC);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnGetInterrupt}
 */
static DECLCALLBACK(int) apicR3HvGetInterrupt(PVMCPUCC pVCpu, uint8_t *pu8Vector, uint32_t *puSrcTag)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pu8Vector);

    RT_NOREF(pVCpu, pu8Vector, puSrcTag);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VERR_APIC_INTR_NOT_PENDING;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnPostInterrupt}
 */
static DECLCALLBACK(bool) apicR3HvPostInterrupt(PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, bool fAutoEoi,
                                                uint32_t uSrcTag)
{
    Assert(pVCpu);
    Assert(uVector > XAPIC_ILLEGAL_VECTOR_END);
    RT_NOREF(fAutoEoi);

    RT_NOREF(pVCpu, uVector, enmTriggerMode, fAutoEoi, uSrcTag);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return false;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnUpdatePendingInterrupts}
 */
static DECLCALLBACK(void) apicR3HvUpdatePendingInterrupts(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    RT_NOREF(pVCpu);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnBusDeliver}
 */
static DECLCALLBACK(int) apicR3HvBusDeliver(PVMCC pVM, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode, uint8_t uVector,
                                            uint8_t uPolarity, uint8_t uTriggerMode, uint8_t uIoApicPin, uint32_t uSrcTag)
{
    RT_NOREF(uPolarity, uIoApicPin, uSrcTag);

    /*
     * If the APIC isn't enabled, do nothing and pretend success.
     */
    if (apicR3HvIsEnabled(pVM->CTX_SUFF(apCpus)[0]))
    { /* likely */ }
    else
        return VINF_SUCCESS;

    /*
     * Deliver the interrupt request via Hyper-V.
     */
    AssertCompile(WHvX64InterruptTypeFixed          == XAPICDELIVERYMODE_FIXED);
    AssertCompile(WHvX64InterruptTypeLowestPriority == XAPICDELIVERYMODE_LOWEST_PRIO);
    AssertCompile(WHvX64InterruptTypeNmi            == XAPICDELIVERYMODE_NMI);
    AssertCompile(WHvX64InterruptTypeInit           == XAPICDELIVERYMODE_INIT);
    AssertCompile(WHvX64InterruptTypeSipi           == XAPICDELIVERYMODE_STARTUP);
#if 0
    AssertCompile(WHvX64InterruptTypeLocalInt1      == XAPICDELIVERYMODE_EXTINT);
#endif
    /*
     * Since WHvX64InterruptTypeLocalInt1 currently does not match the hardware specified value,
     * we need to convert it so the WHvRequestInterrupt call succeeds. Otherwise, SMP VMs typically
     * will fail to boot (for instance, DSL Linux guests run into an MP-BIOS bug kernel panic).
     */
    if (uDeliveryMode == XAPICDELIVERYMODE_EXTINT)
        uDeliveryMode = WHvX64InterruptTypeLocalInt1;

    AssertCompile(XAPICDESTMODE_PHYSICAL == WHvX64InterruptDestinationModePhysical);
    AssertCompile(XAPICDESTMODE_LOGICAL  == WHvX64InterruptDestinationModeLogical);
    AssertCompile(XAPICTRIGGERMODE_EDGE  == WHvX64InterruptTriggerModeEdge);
    AssertCompile(XAPICTRIGGERMODE_LEVEL == WHvX64InterruptTriggerModeLevel);

    Assert(uDeliveryMode != XAPICDELIVERYMODE_NMI);

    PHVAPIC pHvApic = VM_TO_HVAPIC(pVM);
    WHV_INTERRUPT_CONTROL Control; RT_ZERO(Control);
    Control.Type            = uDeliveryMode; /* Matching up except "LocalInt1" and "EXTINT", see above. */
    Control.DestinationMode = uDestMode;
    Control.TriggerMode     = uTriggerMode;
    Control.Destination     = uDest;
    Control.Vector          = uVector;

    HRESULT const hrc = WHvRequestInterrupt(pHvApic->hPartition, &Control, sizeof(Control));
    if (FAILED(hrc))
    {
        AssertMsgFailed(("APIC/WHv: Delivering interrupt failed: %Rhrc (Last=%#x/%u)", hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
        LogRelMax(10, ("APIC/WHv: Delivering interrupt failed: %Rhrc (Last=%#x/%u)", hrc, RTNtLastStatusValue(), RTNtLastErrorValue()));
        return VERR_APIC_INTR_DISCARDED;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetEoi}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvSetEoi(PVMCPUCC pVCpu, uint32_t uEoi, bool fForceX2ApicBehaviour)
{
    VMCPU_ASSERT_EMT(pVCpu);

    RT_NOREF(pVCpu, uEoi, fForceX2ApicBehaviour);
    AssertReleaseMsgFailed(("Unexpected interface call\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnSetHvCompatMode}
 */
static DECLCALLBACK(int) apicR3HvSetHvCompatMode(PVM pVM, bool fHyperVCompatMode)
{
    PHVAPIC pHvApic = VM_TO_HVAPIC(pVM);
    pHvApic->fHyperVCompatMode = fHyperVCompatMode;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMAPICBACKEND,pfnImportState}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvImportState(PVMCPUCC pVCpu)
{
#if 0
    RT_NOREF(pVCpu);
    AssertReleaseFailed();
#else
    PXAPICPAGE    pHvApicPage = (PXAPICPAGE)pVCpu->apic.s.pvHvPageR3;
    PVMCC         pVM         = pVCpu->CTX_SUFF(pVM);
    PHVAPIC       pHvApic     = VM_TO_HVAPIC(pVM);
    VMCPUID const idCpu       = pVCpu->idCpu;
    uint32_t      cbWritten   = 0;
    HRESULT       hrc;

    Assert(WHvGetVirtualProcessorInterruptControllerState2);
    hrc = WHvGetVirtualProcessorInterruptControllerState2(pHvApic->hPartition, idCpu, pHvApicPage, sizeof(*pHvApicPage),
                                                          &cbWritten);
    AssertLogRelMsgReturn(SUCCEEDED(hrc), ("Failed to get the virtual-APIC page. hrc=%Rhrc\n", hrc), VERR_APIC_IPE_0);
    Assert(cbWritten == sizeof(*pHvApicPage));

    memcpy(pVCpu->apic.s.pvApicPageR3, pHvApicPage, sizeof(XAPICPAGE));

    /* Start or stop the APIC timer if needed. */
    //PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    //apicR3HvSetTimerIcr(pDevIns, pVCpu, VINF_SUCCESS, pHvApicPage->timer_icr.u32InitialCount);
#endif
    return VINF_SUCCESS;
}


/**
* @interface_method_impl{PDMAPICBACKEND,pfnExportState}
 */
static DECLCALLBACK(VBOXSTRICTRC) apicR3HvExportState(PVMCPUCC pVCpu)
{
#if 0
    RT_NOREF(pVCpu);
    AssertReleaseFailed();
#else

    RT_NOREF(pVCpu);

#if 1
    PXAPICPAGE pHvApicPage = (PXAPICPAGE)pVCpu->apic.s.pvHvPageR3;
    //{
    //    uint32_t   uCcr    = 0;
    //    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    //    VBOXSTRICTRC rcStrict = apicR3HvGetTimerCcr(pDevIns, pVCpu, VINF_SUCCESS, &uCcr);
    //    if (RT_SUCCESS(rcStrict))
    //    {
    //        PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    //        pXApicPage->timer_ccr.u32CurrentCount = uCcr;
    //
    //        PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
    //        rcStrict = PDMDevHlpTimerLockClock(pDevIns, pHvApicCpu->hTimer, VINF_SUCCESS);
    //        if (RT_SUCCESS(rcStrict))
    //        {
    //            apicR3HvStopTimer(pVCpu);
    //            PDMDevHlpTimerUnlockClock(pDevIns, pHvApicCpu->hTimer);
    //        }
    //    }
    //    else
    //        AssertMsgFailed(("Failed to get timer CCR. rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    //}

    memcpy(pHvApicPage, pVCpu->apic.s.pvApicPageR3, sizeof(XAPICPAGE));

    //PVMCC    pVM        = pVCpu->CTX_SUFF(pVM);
    //PHVAPIC  pHvApic    = VM_TO_HVAPIC(pVM);
    //VMCPUID const idCpu = pVCpu->idCpu;
    //Assert(WHvSetVirtualProcessorInterruptControllerState2);
    //HRESULT hrc = WHvSetVirtualProcessorInterruptControllerState2(pHvApic->hPartition, idCpu, pHvApicPage, sizeof(*pHvApicPage));
    //AssertLogRelMsgReturn(SUCCEEDED(hrc), ("Failed to set the virtual-APIC page. hrc=%Rhrc\n", hrc), VERR_APIC_IPE_1);
#endif


#endif
    return VINF_SUCCESS;
}


/**
 * Dumps basic APIC state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3HvInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    apicR3CommonDbgInfo(pVCpu, pHlp, pVCpu->apic.s.uApicBaseMsr);
}


/**
 * Dumps APIC Local Vector Table (LVT) information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3HvInfoLvt(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    apicR3CommonDbgInfoLvt(pVCpu, pHlp);
}


/**
 * Dumps the APIC timer information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3HvInfoTimer(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    apicR3CommonDbgInfoLvtTimer(pVCpu, pHlp);
}


/**
 * Worker for saving per-VM APIC data.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVM         The cross context VM structure.
 * @param   pSSM        The SSM handle.
 */
static int apicR3HvSaveVMData(PPDMDEVINS pDevIns, PVM pVM, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;
    PHVAPIC       pHvApic = VM_TO_HVAPIC(pVM);
    pHlp->pfnSSMPutU32(pSSM,  pVM->cCpus);
    pHlp->pfnSSMPutBool(pSSM, true /* fIoApicPresent  - unused but stay compatible with emulated APIC. */);
    return pHlp->pfnSSMPutU32(pSSM, pHvApic->enmMaxMode);
}


/**
 * Worker for loading per-VM APIC data.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVM         The cross context VM structure.
 * @param   pSSM        The SSM handle.
 */
static int apicR3HvLoadVMData(PPDMDEVINS pDevIns, PVM pVM, PSSMHANDLE pSSM)
{
    PHVAPIC       pHvApic = VM_TO_HVAPIC(pVM);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    /* Load and verify number of CPUs. */
    uint32_t cCpus;
    int rc = pHlp->pfnSSMGetU32(pSSM, &cCpus);
    AssertRCReturn(rc, rc);
    if (cCpus != pVM->cCpus)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - cCpus: saved=%u config=%u"), cCpus, pVM->cCpus);

    /* Ignore I/O APIC present used by emulated APIC, unused here. */
    bool fIoApicPresent;
    rc = pHlp->pfnSSMGetBool(pSSM, &fIoApicPresent);
    AssertRCReturn(rc, rc);
    NOREF(fIoApicPresent);

    /* Load and verify configured max APIC mode. */
    uint32_t uSavedMaxApicMode;
    rc = pHlp->pfnSSMGetU32(pSSM, &uSavedMaxApicMode);
    AssertRCReturn(rc, rc);
    if (uSavedMaxApicMode != (uint32_t)pHvApic->enmMaxMode)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - uApicMode: saved=%u config=%u"),
                                       uSavedMaxApicMode, pHvApic->enmMaxMode);
    return VINF_SUCCESS;
}


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) apicR3HvSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVM           pVM  = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);

    LogFlow(("APIC/WHv: apicR3HvSaveExec\n"));

    /* Save per-VM data. */
    int rc = apicR3HvSaveVMData(pDevIns, pVM, pSSM);
    AssertRCReturn(rc, rc);

    /* Save per-VCPU data.*/
    VMCC_FOR_EACH_VMCPU(pVM)
    {
        PCHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);

        /* Save the auxiliary data. */
        pHlp->pfnSSMPutU64(pSSM, pHvApicCpu->uApicBaseMsr);
        pHlp->pfnSSMPutU32(pSSM, pHvApicCpu->uEsrInternal);

        /* Save the APIC page. */
        if (XAPIC_IN_X2APIC_MODE(pHvApicCpu->uApicBaseMsr))
            pHlp->pfnSSMPutStruct(pSSM, (const void *)pHvApicCpu->pvApicPageR3, &g_aX2ApicPageFields[0]);
        else
            pHlp->pfnSSMPutStruct(pSSM, (const void *)pHvApicCpu->pvApicPageR3, &g_aXApicPageFields[0]);

        /* Save the timer. */
        pHlp->pfnSSMPutU64(pSSM, pHvApicCpu->u64TimerInitial);
        PDMDevHlpTimerSave(pDevIns, pHvApicCpu->hTimer, pSSM);

        /* Save the LINT0, LINT1 interrupt line states (Hyper-V doesn't give us this data explicitly). */
        pHlp->pfnSSMPutBool(pSSM, false /* ActiveLint0 */);
        pHlp->pfnSSMPutBool(pSSM, false /* ActiveLint1 */);
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);

    /** @todo APIC_FUZZY_SSM_COMPAT_TEST? */
    return rc;
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) apicR3HvLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVM           pVM  = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(uPass == SSM_PASS_FINAL, VERR_WRONG_ORDER);

    LogFlow(("APIC/WHv: apicR3HvLoadExec: uVersion=%u uPass=%#x\n", uVersion, uPass));

    /* Weed out invalid versions. */
    if (uVersion != APIC_SAVED_STATE_VERSION)
    {
        LogRel(("APIC/WHv: apicR3HvLoadExec: Invalid/unrecognized saved-state version %u (%#x)\n", uVersion, uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    int rc = apicR3HvLoadVMData(pDevIns, pVM, pSSM);
    AssertRCReturn(rc, rc);

    /*
     * Restore per CPU state.
     */
    VMCC_FOR_EACH_VMCPU(pVM)
    {
        PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);

        /* Load the auxiliary data. */
        pHlp->pfnSSMGetU64V(pSSM, &pHvApicCpu->uApicBaseMsr);
        pHlp->pfnSSMGetU32(pSSM,  &pHvApicCpu->uEsrInternal);

        /* Load the APIC page. */
        if (XAPIC_IN_X2APIC_MODE(pHvApicCpu->uApicBaseMsr))
            pHlp->pfnSSMGetStruct(pSSM, pHvApicCpu->pvApicPageR3, &g_aX2ApicPageFields[0]);
        else
            pHlp->pfnSSMGetStruct(pSSM, pHvApicCpu->pvApicPageR3, &g_aXApicPageFields[0]);

        /* Load the timer. */
        rc = pHlp->pfnSSMGetU64(pSSM, &pHvApicCpu->u64TimerInitial);     AssertRCReturn(rc, rc);
        rc = PDMDevHlpTimerLoad(pDevIns, pHvApicCpu->hTimer, pSSM);      AssertRCReturn(rc, rc);
        Assert(pHvApicCpu->uHintedTimerShift == 0);
        Assert(pHvApicCpu->uHintedTimerInitialCount == 0);
        if (PDMDevHlpTimerIsActive(pDevIns, pHvApicCpu->hTimer))
        {
            PCXAPICPAGE    pXApicPage    = VMCPU_TO_CXAPICPAGE(pVCpu);
            uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
            uint8_t const  uTimerShift   = apicCommonGetTimerShift(pXApicPage);
            apicR3HvHintTimerFreq(pDevIns, pHvApicCpu, uInitialCount, uTimerShift);
        }

        /* Load the LINT0, LINT1 interrupt line states - unused with Hyper-V. */
        bool volatile fDummy;
        pHlp->pfnSSMGetBoolV(pSSM, &fDummy /*fActiveLint0*/);
        pHlp->pfnSSMGetBoolV(pSSM, &fDummy /*fActiveLint1*/);

        /*
         * Check that we're still good wrt restored data, then tell CPUM about the current CPUID[1].EDX[9] visibility.
         */
        rc = pHlp->pfnSSMHandleGetStatus(pSSM);
        AssertRCReturn(rc, rc);
        CPUMSetGuestCpuIdPerCpuApicFeature(pVCpu, RT_BOOL(pHvApicCpu->uApicBaseMsr & MSR_IA32_APICBASE_EN));
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);

    return rc;
}


/**
 * Initializes per-VCPU APIC to the state following a power-up or hardware
 * reset.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   fResetApicBaseMsr   Whether to reset the APIC base MSR.
 */
static void apicR3HvResetCpu(PVMCPUCC pVCpu, bool fResetApicBaseMsr)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    LogFlow(("APIC/WHv%u: apicR3ResetCpu: fResetApicBaseMsr=%RTbool\n", pVCpu->idCpu, fResetApicBaseMsr));

#ifdef VBOX_STRICT
    /* Verify that the initial APIC ID reported via CPUID matches our VMCPU ID assumption. */
    uint32_t uEax, uEbx, uEcx, uEdx;
    uEax = uEbx = uEcx = uEdx = UINT32_MAX;
    CPUMGetGuestCpuId(pVCpu, 1, 0, -1 /*f64BitMode*/, &uEax, &uEbx, &uEcx, &uEdx);
    Assert(((uEbx >> 24) & 0xff) == pVCpu->idCpu);
#endif

    /*
     * The state following a power-up or reset is a superset of the INIT state.
     * See Intel spec. 10.4.7.3 "Local APIC State After an INIT Reset ('Wait-for-SIPI' State)"
     */
    apicR3HvInitIpi(pVCpu);

    /*
     * The APIC version register is read-only, so just initialize it here.
     * It is not clear from the specs, where exactly it is initialized.
     * The version determines the number of LVT entries and size of the APIC ID (8 bits for P4).
     */
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    pXApicPage->version.u.u8MaxLvtEntry = XAPIC_MAX_LVT_ENTRIES_P4 - 1;
    pXApicPage->version.u.u8Version     = XAPIC_HARDWARE_VERSION_P4;
    AssertCompile(sizeof(pXApicPage->id.u8ApicId) >= XAPIC_APIC_ID_BIT_COUNT_P4 / 8);
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

    /** @todo It isn't clear in the spec. where exactly the default base address
     *        is (re)initialized, atm we do it here in Reset. */
    if (fResetApicBaseMsr)
        apicCommonResetBaseMsr(pVCpu);

    /*
     * Initialize the APIC ID register to xAPIC format.
     */
    RT_BZERO(&pXApicPage->id, sizeof(pXApicPage->id));
    pXApicPage->id.u8ApicId = pVCpu->idCpu;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnInitComplete}
 */
DECLCALLBACK(int) apicR3HvInitComplete(PPDMDEVINS pDevIns)
{
    PVM     pVM     = PDMDevHlpGetVM(pDevIns);
    PHVAPIC pHvApic = VM_TO_HVAPIC(pVM);

    /*
     * Init APIC settings that rely on HM and CPUM configurations.
     */
    CPUMCPUIDLEAF CpuLeaf;
    int rc = CPUMR3CpuIdGetLeaf(pVM, &CpuLeaf, 1, 0);
    AssertRCReturn(rc, rc);

    pHvApic->fSupportsTscDeadline = RT_BOOL(CpuLeaf.uEcx & X86_CPUID_FEATURE_ECX_TSCDEADL);

    LogRel(("APIC/WHv: fSupportsTscDeadline=%RTbool\n", pHvApic->fSupportsTscDeadline));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
DECLCALLBACK(void) apicR3HvReset(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));

    PVM pVM = PDMDevHlpGetVM(pDevIns);
    VM_ASSERT_EMT0(pVM);
    VM_ASSERT_IS_NOT_RUNNING(pVM);
    WHV_PARTITION_HANDLE const hPartition = VM_TO_HVAPIC(pVM)->hPartition;

    VMCC_FOR_EACH_VMCPU(pVM)
    {
        apicR3HvResetCpu(pVCpu, true /*fResetApicBaseMsr*/);

        PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
        if (PDMDevHlpTimerIsActive(pDevIns, pHvApicCpu->hTimer))
            PDMDevHlpTimerStop(pDevIns, pHvApicCpu->hTimer);

        Assert(WHvSetVirtualProcessorInterruptControllerState2);  /* This is already checked in device construct. */
        memcpy(pVCpu->apic.s.pvHvPageR3, pVCpu->apic.s.pvApicPageR3, sizeof(XAPICPAGE));
        HRESULT const hrc = WHvSetVirtualProcessorInterruptControllerState2(hPartition, idCpu, pVCpu->apic.s.pvHvPageR3,
                                                                            sizeof(XAPICPAGE));
        Assert(SUCCEEDED(hrc)); NOREF(hrc);
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);
}


/**
 * Frees all the allocated virtual-APIC pages.
 *
 * @param   pVM     The cross context VM structure.
 */
DECLINLINE(void) apicR3HvFreePages(PVM pVM)
{
    /* Free the Virtual-APIC pages for all VCPUs. */
    {
        PVMCPU     pVCpu0      = pVM->apCpusR3[0];
        PHVAPICCPU pHvApicCpu0 = VMCPU_TO_HVAPICCPU(pVCpu0);

        if (pHvApicCpu0->pvApicPageR3 != NIL_RTR3PTR)
            SUPR3PageFree(pHvApicCpu0->pvApicPageR3, pVM->cCpus);
    }

    /* Reset the pointers for each VCPUs. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU     pVCpu         = pVM->apCpusR3[idCpu];
        PHVAPICCPU pHvApicCpu    = VMCPU_TO_HVAPICCPU(pVCpu);
        pHvApicCpu->pvApicPageR3 = NIL_RTR3PTR;
        pHvApicCpu->pvHvPageR3   = NIL_RTR3PTR;
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
DECLCALLBACK(int) apicR3HvDestruct(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    PVM pVM = PDMDevHlpGetVM(pDevIns);
    apicR3HvFreePages(pVM);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
DECLCALLBACK(int) apicR3HvConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    Assert(iInstance == 0); NOREF(iInstance);

    PHVAPICDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PHVAPICDEV);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;
    PVM           pVM     = PDMDevHlpGetVM(pDevIns);
    PHVAPIC       pHvApic = VM_TO_HVAPIC(pVM);

    /*
     * Init the data.
     */
    pThis->pDevIns     = pDevIns;
    pHvApic->pDevInsR3 = pDevIns;
    int rc = NEMR3WinGetPartitionHandle(pVM, (PRTHCUINTPTR)&pHvApic->hPartition);
    if (RT_FAILURE(rc))
        return VMR3SetError(pVM->pUVM, VERR_APIC_IPE_2, RT_SRC_POS,
                            "Failed to get the Hyper-V VM partition handle. rc=%Rrc", rc);

    /*
     * Validate APIC settings.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Mode|IOAPIC|NumCPUs|MacOSWorkaround", "");

    /** @devcfgm{apic, Mode, PDMAPICMODE, APIC(2)}
     * Max APIC feature level. */
    uint8_t uMaxMode;
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Mode", &uMaxMode, PDMAPICMODE_APIC);
    AssertLogRelRCReturn(rc, rc);
    switch ((PDMAPICMODE)uMaxMode)
    {
        case PDMAPICMODE_NONE:
            LogRel(("APIC/WHv: APIC maximum mode configured as 'None', effectively disabled/not-present!\n"));
            RT_FALL_THROUGH();
        case PDMAPICMODE_APIC:
        case PDMAPICMODE_X2APIC:
            break;
        default:
            return VMR3SetError(pVM->pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS, "APIC mode %d unknown.", uMaxMode);
    }
    pHvApic->enmMaxMode = (PDMAPICMODE)uMaxMode;

    /*
     * Disable automatic PDM locking for this device.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register the APIC with PDM.
     */
    rc = PDMDevHlpIcRegister(pDevIns);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMApicRegisterBackend(pVM, PDMAPICBACKENDTYPE_HYPERV, &g_ApicNemBackend);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Initialize the APIC CPUID state.
     */
    if (pHvApic->enmMaxMode == PDMAPICMODE_X2APIC)
    {
        rc = CPUMR3MsrRangesInsert(pVM, &g_MsrRange_x2Apic);
        AssertLogRelRCReturn(rc, rc);
    }
    else
    {
        /* We currently don't have a function to remove the range, so we register an range which will cause a #GP. */
        rc = CPUMR3MsrRangesInsert(pVM, &g_MsrRange_x2Apic_Invalid);
        AssertLogRelRCReturn(rc, rc);
    }

    /* Tell CPUM about the APIC feature level so it can adjust APICBASE MSR GP mask and CPUID bits. */
    apicR3CommonSetCpuIdFeatureLevel(pVM, pHvApic->enmMaxMode);

    /*
     * Allocate all the virtual-APIC pages.
     */
    AssertCompile(sizeof(XAPICPAGE) <= HOST_PAGE_SIZE);
    AssertCompile((sizeof(XAPICPAGE) % HOST_PAGE_SIZE) == 0);
    size_t const cPages      = pVM->cCpus * 2;
    void        *pvApicPages = NULL;
    rc = SUPR3PageAlloc(cPages, 0 /* fFlags */, &pvApicPages);
    AssertLogRelMsgRCReturn(rc, ("Failed to allocate %u page(s) for the virtual-APIC page(s), rc=%Rrc\n", cPages, rc), rc);

    /* Zero the virtual-APIC pages here, later partially initialized by apicR3HvResetCpu. */
    AssertPtr(pvApicPages);
    RT_BZERO(pvApicPages, cPages * sizeof(XAPICPAGE));

    /*
     * Map the virtual-APIC pages to Hyper-V (for syncing the state).
     */
    VMCC_FOR_EACH_VMCPU(pVM)
    {
        PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);
        Assert(pHvApicCpu->pvApicPageR3 == NIL_RTR3PTR);
        Assert(pHvApicCpu->pvHvPageR3 == NIL_RTR3PTR);

        size_t const offPage     = 2 * idCpu * sizeof(XAPICPAGE);
        pHvApicCpu->pvHvPageR3   = (void *)((uintptr_t)pvApicPages + offPage);
        pHvApicCpu->pvApicPageR3 = (void *)((uintptr_t)pvApicPages + offPage + sizeof(XAPICPAGE));

        Assert(pHvApicCpu->pvHvPageR3 != NIL_RTR3PTR);
        Assert(pHvApicCpu->pvApicPageR3 != NIL_RTR3PTR);

        /* Initialize the APIC page and the APIC base MSR and copy it over to the Hyper-V APIC page. */
        PXAPICPAGE pHvApicPage = (PXAPICPAGE)pHvApicCpu->pvHvPageR3;
        {
            apicR3HvResetCpu(pVCpu, true /*fResetApicBaseMsr*/);
            PCXAPICPAGE pXApicPage = (PCXAPICPAGE)pHvApicCpu->pvApicPageR3;
            Assert(pHvApicPage != pXApicPage);
            memcpy(pHvApicPage, pXApicPage, sizeof(XAPICPAGE));
        }

        /*
         * We cannot use the new API (WHv[Get|Set]VirtualProcessorState) because the format of the
         * virtual-APIC page is different from the hardware-specified format and is not documented
         * thus far. See @bugref{9993#c46}.
         */
#if 0
        if (WHvGetVirtualProcessorState)
        {
            uint32_t cbWritten = 0;
            HRESULT hrc = WHvGetVirtualProcessorState(pHvApic->hPartition, idCpu,
                                                      WHvVirtualProcessorStateTypeInterruptControllerState2, pXApicPage,
                                                      sizeof(*pXApicPage), &cbWritten);

            /* Directly calling this with zero'ed page OR with the hardware-specific page format fails. Hence, the
               above call to fetch the page from Hyper-V first. */
            hrc = WHvSetVirtualProcessorState(pHvApic->hPartition, idCpu,
                                              WHvVirtualProcessorStateTypeInterruptControllerState2, pXApicPage,
                                              sizeof(*pXApicPage));

            /* Later change these to VMR3SetError as it would fail more graceful on failure especially since we call
               external APIs there is always a chance of failure. */
            AssertLogRelMsgReturnStmt(SUCCEEDED(hrc),
                                      ("Failed to get virtual-APIC page hrc=%Rhrc\n", hrc),
                                      apicR3HvFreePages(pVM), VERR_APIC_INIT_FAILED);
            AssertLogRelMsgReturnStmt(cbWritten == sizeof(*pXApicPage),
                                      ("APIC page size mismatch (expected %u, got %u bytes)\n", sizeof(*pXApicPage), cbWritten),
                                      apicR3HvFreePages(pVM), VERR_APIC_INIT_FAILED);
            AssertLogRelMsgReturnStmt(WHvSetVirtualProcessorState,
                                      ("WHvGetVirtualProcessorState without WHvSetVirtualProcessorState!?\n"),
                                      apicR3HvFreePages(pVM), VERR_APIC_INIT_FAILED);

# ifdef DEBUG_ramshankar
            if (idCpu == 0)
            {
                hrc = WHvGetVirtualProcessorState(pHvApic->hPartition, idCpu,
                                                  WHvVirtualProcessorStateTypeInterruptControllerState2, pXApicPage,
                                                  sizeof(*pXApicPage), &cbWritten);
                LogRel(("APIC/WHv: Virtual-APIC page\n%.*Rhxd\n", sizeof(*pXApicPage), pXApicPage));
            }
# endif
        }
        else
#endif
        if (WHvSetVirtualProcessorInterruptControllerState2)
        {
            HRESULT const hrc = WHvSetVirtualProcessorInterruptControllerState2(pHvApic->hPartition, idCpu, pHvApicPage,
                                                                                sizeof(*pHvApicPage));
            if (FAILED(hrc))
            {
                apicR3HvFreePages(pVM);
                return VMR3SetError(pVM->pUVM, VERR_APIC_INIT_FAILED, RT_SRC_POS,
                                    "Failed to set the Hyper-V Virtual-APIC page. %Rhrc (Last=%#x/%u)",
                                    hrc, RTNtLastStatusValue(), RTNtLastErrorValue());
            }
#ifdef DEBUG_ramshankar
            if (idCpu == 0)
            {
                uint32_t cbWritten = 0;
                HRESULT const hrc2 = WHvGetVirtualProcessorInterruptControllerState2(pHvApic->hPartition,
                                                                                     idCpu, pHvApicPage, sizeof(*pHvApicPage),
                                                                                     &cbWritten);
                Assert(SUCCEEDED(hrc2));
                LogRel(("APIC/WHv: Virtual-APIC page\n%.*Rhxd\n", sizeof(*pHvApicPage), pHvApicPage));
                if (memcmp(pHvApicCpu->pvHvPageR3, pHvApicCpu->pvApicPageR3, sizeof(XAPICPAGE)))
                    LogRel(("APIC/WHv: The page content differs after passing it to Hyper-V!\n"));
            }
#endif
        }
        else
        {
            apicR3HvFreePages(pVM);
            return VMR3SetError(pVM->pUVM, VERR_APIC_INIT_FAILED,
                                RT_SRC_POS, "No suitable Hyper-V virtual-APIC page API found.");
        }


        /*
         * Create the APIC timers.
         */
        RTStrPrintf(&pHvApicCpu->szTimerDesc[0], sizeof(pHvApicCpu->szTimerDesc), "APICHv Timer %u", pVCpu->idCpu);
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, apicR3HvTimerCallback, pVCpu,
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0, pHvApicCpu->szTimerDesc, &pHvApicCpu->hTimer);
        AssertRCReturn(rc, rc);
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);

    /*
     * Register debugger info callbacks.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "apic",      "Dumps APIC basic information.", apicR3HvInfo,      DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "apiclvt",   "Dumps APIC LVT information.",   apicR3HvInfoLvt,   DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "apictimer", "Dumps APIC timer information.", apicR3HvInfoTimer, DBGFINFO_FLAGS_ALL_EMTS);

    /*
     * Register saved state callbacks.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, APIC_SAVED_STATE_VERSION, 0 /*cbGuess*/, apicR3HvSaveExec, apicR3HvLoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Statistics.
     */
#ifdef VBOX_WITH_STATISTICS
# define APIC_REG_COUNTER(a_pvReg, a_pszNameFmt, a_pszDesc) \
        PDMDevHlpSTAMRegisterF(pDevIns, a_pvReg, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, \
                               STAMUNIT_OCCURENCES, a_pszDesc, a_pszNameFmt, idCpu)

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU     pVCpu      = pVM->apCpusR3[idCpu];
        PHVAPICCPU pHvApicCpu = VMCPU_TO_HVAPICCPU(pVCpu);

        APIC_REG_COUNTER(&pHvApicCpu->StatTimerOneShot,     "%u/TimerOneShot",     "Number of times the one-shot timer callback is invoked.");
        APIC_REG_COUNTER(&pHvApicCpu->StatTimerPeriodic,    "%u/TimerPeriodic",    "Number of times the periodic timer  callback is invoked.");
        APIC_REG_COUNTER(&pHvApicCpu->StatTimerTscDeadline, "%u/TimerTscDeadline", "Number of times the TSC deadline timers callback is invoked");
    }

# undef APIC_REG_ACCESS_COUNTER
#endif

    return VINF_SUCCESS;
}


/**
 * APIC device registration structure.
 */
const PDMDEVREG g_DeviceAPICNem =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "apic-nem",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(HVAPICDEV),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Advanced Programmable Interrupt Controller - Hyper-V variant",
#if defined(IN_RING3)
    /* .szRCMod = */                "VMMRC.rc",
    /* .szR0Mod = */                "VMMR0.r0",
    /* .pfnConstruct = */           apicR3HvConstruct,
    /* .pfnDestruct = */            apicR3HvDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               apicR3HvReset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        apicR3HvInitComplete,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

/**
 * The Hyper-V APIC backend.
 */
const PDMAPICBACKEND g_ApicNemBackend =
{
    /* .pfnIsEnabled = */               apicR3HvIsEnabled,
    /* .pfnInitIpi = */                 apicR3HvInitIpi,
    /* .pfnGetBaseMsrNoCheck = */       apicR3HvGetBaseMsrNoCheck,
    /* .pfnGetBaseMsr = */              apicR3HvGetBaseMsr,
    /* .pfnSetBaseMsr = */              apicR3HvSetBaseMsr,
    /* .pfnReadRaw32 = */               apicR3HvReadRaw32,
    /* .pfnReadMsr = */                 apicR3HvReadMsr,
    /* .pfnWriteMsr = */                apicR3HvWriteMsr,
    /* .pfnGetTpr = */                  apicR3HvGetTpr,
    /* .pfnSetTpr = */                  apicR3HvSetTpr,
    /* .pfnGetIcrNoCheck = */           apicR3HvGetIcrNoCheck,
    /* .pfnSetIcr = */                  apicR3HvSetIcr,
    /* .pfnGetTimerFreq = */            apicR3HvGetTimerFreq,
    /* .pfnSetLocalInterrupt = */       apicR3HvSetLocalInterrupt,
    /* .pfnGetInterrupt = */            apicR3HvGetInterrupt,
    /* .pfnPostInterrupt = */           apicR3HvPostInterrupt,
    /* .pfnUpdatePendingInterrupts = */ apicR3HvUpdatePendingInterrupts,
    /* .pfnBusDeliver = */              apicR3HvBusDeliver,
    /* .pfnSetEoi = */                  apicR3HvSetEoi,
    /* .pfnSetHvCompatMode = */         apicR3HvSetHvCompatMode,
    /* .pfnImportState = */             apicR3HvImportState,
    /* .pfnExportState = */             apicR3HvExportState,
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

