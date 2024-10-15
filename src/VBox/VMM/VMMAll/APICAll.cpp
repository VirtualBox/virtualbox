/* $Id: APICAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller - All Contexts.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#define VMCPU_INCL_CPUM_GST_CTX /* for macOS hack */
#include "APICInternal.h"
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcpuset.h>
#ifdef IN_RING0
# include <VBox/vmm/gvmm.h>
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void apicSetInterruptFF(PVMCPUCC pVCpu, PDMAPICIRQ enmType);
static void apicStopTimer(PVMCPUCC pVCpu);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
/** An ordered array of valid LVT masks. */
static const uint32_t g_au32LvtValidMasks[] =
{
    XAPIC_LVT_TIMER_VALID,
    XAPIC_LVT_THERMAL_VALID,
    XAPIC_LVT_PERF_VALID,
    XAPIC_LVT_LINT_VALID,   /* LINT0 */
    XAPIC_LVT_LINT_VALID,   /* LINT1 */
    XAPIC_LVT_ERROR_VALID
};
#endif

#if 0
/** @todo CMCI */
static const uint32_t g_au32LvtExtValidMask[] =
{
    XAPIC_LVT_CMCI_VALID
};
#endif


/**
 * Checks if a vector is set in an APIC 256-bit sparse register.
 *
 * @returns true if the specified vector is set, false otherwise.
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   uVector         The vector to check if set.
 */
DECLINLINE(bool) apicTestVectorInReg(const volatile XAPIC256BITREG *pApicReg, uint8_t uVector)
{
    const volatile uint8_t *pbBitmap = (const volatile uint8_t *)&pApicReg->u[0];
    return ASMBitTest(pbBitmap, (XAPIC_REG256_VECTOR_OFF(uVector) << 3) + XAPIC_REG256_VECTOR_BIT(uVector));
}


/**
 * Sets the vector in an APIC 256-bit sparse register.
 *
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   uVector         The vector to set.
 */
DECLINLINE(void) apicSetVectorInReg(volatile XAPIC256BITREG *pApicReg, uint8_t uVector)
{
    volatile uint8_t *pbBitmap = (volatile uint8_t *)&pApicReg->u[0];
    ASMAtomicBitSet(pbBitmap, (XAPIC_REG256_VECTOR_OFF(uVector) << 3) + XAPIC_REG256_VECTOR_BIT(uVector));
}


/**
 * Clears the vector in an APIC 256-bit sparse register.
 *
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   uVector         The vector to clear.
 */
DECLINLINE(void) apicClearVectorInReg(volatile XAPIC256BITREG *pApicReg, uint8_t uVector)
{
    volatile uint8_t *pbBitmap = (volatile uint8_t *)&pApicReg->u[0];
    ASMAtomicBitClear(pbBitmap, (XAPIC_REG256_VECTOR_OFF(uVector) << 3) + XAPIC_REG256_VECTOR_BIT(uVector));
}


#if 0 /* unused */
/**
 * Checks if a vector is set in an APIC Pending-Interrupt Bitmap (PIB).
 *
 * @returns true if the specified vector is set, false otherwise.
 * @param   pvPib           Opaque pointer to the PIB.
 * @param   uVector         The vector to check if set.
 */
DECLINLINE(bool) apicTestVectorInPib(volatile void *pvPib, uint8_t uVector)
{
    return ASMBitTest(pvPib, uVector);
}
#endif /* unused */


/**
 * Atomically sets the PIB notification bit.
 *
 * @returns non-zero if the bit was already set, 0 otherwise.
 * @param   pApicPib        Pointer to the PIB.
 */
DECLINLINE(uint32_t) apicSetNotificationBitInPib(PAPICPIB pApicPib)
{
    return ASMAtomicXchgU32(&pApicPib->fOutstandingNotification, RT_BIT_32(31));
}


/**
 * Atomically tests and clears the PIB notification bit.
 *
 * @returns non-zero if the bit was already set, 0 otherwise.
 * @param   pApicPib        Pointer to the PIB.
 */
DECLINLINE(uint32_t) apicClearNotificationBitInPib(PAPICPIB pApicPib)
{
    return ASMAtomicXchgU32(&pApicPib->fOutstandingNotification, UINT32_C(0));
}


/**
 * Sets the vector in an APIC Pending-Interrupt Bitmap (PIB).
 *
 * @param   pvPib           Opaque pointer to the PIB.
 * @param   uVector         The vector to set.
 */
DECLINLINE(void) apicSetVectorInPib(volatile void *pvPib, uint8_t uVector)
{
    ASMAtomicBitSet(pvPib, uVector);
}

#if 0 /* unused */
/**
 * Clears the vector in an APIC Pending-Interrupt Bitmap (PIB).
 *
 * @param   pvPib           Opaque pointer to the PIB.
 * @param   uVector         The vector to clear.
 */
DECLINLINE(void) apicClearVectorInPib(volatile void *pvPib, uint8_t uVector)
{
    ASMAtomicBitClear(pvPib, uVector);
}
#endif /* unused */

#if 0 /* unused */
/**
 * Atomically OR's a fragment (32 vectors) into an APIC 256-bit sparse
 * register.
 *
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   idxFragment     The index of the 32-bit fragment in @a
 *                          pApicReg.
 * @param   u32Fragment     The 32-bit vector fragment to OR.
 */
DECLINLINE(void) apicOrVectorsToReg(volatile XAPIC256BITREG *pApicReg, size_t idxFragment, uint32_t u32Fragment)
{
    Assert(idxFragment < RT_ELEMENTS(pApicReg->u));
    ASMAtomicOrU32(&pApicReg->u[idxFragment].u32Reg, u32Fragment);
}
#endif /* unused */


#if 0 /* unused */
/**
 * Atomically AND's a fragment (32 vectors) into an APIC
 * 256-bit sparse register.
 *
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   idxFragment     The index of the 32-bit fragment in @a
 *                          pApicReg.
 * @param   u32Fragment     The 32-bit vector fragment to AND.
 */
DECLINLINE(void) apicAndVectorsToReg(volatile XAPIC256BITREG *pApicReg, size_t idxFragment, uint32_t u32Fragment)
{
    Assert(idxFragment < RT_ELEMENTS(pApicReg->u));
    ASMAtomicAndU32(&pApicReg->u[idxFragment].u32Reg, u32Fragment);
}
#endif /* unused */


/**
 * Reports and returns appropriate error code for invalid MSR accesses.
 *
 * @returns VERR_CPUM_RAISE_GP_0
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u32Reg          The MSR being accessed.
 * @param   enmAccess       The invalid-access type.
 */
static int apicMsrAccessError(PVMCPUCC pVCpu, uint32_t u32Reg, APICMSRACCESS enmAccess)
{
    static struct
    {
        const char *pszBefore;   /* The error message before printing the MSR index */
        const char *pszAfter;    /* The error message after printing the MSR index */
    } const s_aAccess[] =
    {
        /* enmAccess  pszBefore                        pszAfter */
        /* 0 */     { "read MSR",                      " while not in x2APIC mode"    },
        /* 1 */     { "write MSR",                     " while not in x2APIC mode"    },
        /* 2 */     { "read reserved/unknown MSR",     ""                             },
        /* 3 */     { "write reserved/unknown MSR",    ""                             },
        /* 4 */     { "read write-only MSR",           ""                             },
        /* 5 */     { "write read-only MSR",           ""                             },
        /* 6 */     { "read reserved bits of MSR",     ""                             },
        /* 7 */     { "write reserved bits of MSR",    ""                             },
        /* 8 */     { "write an invalid value to MSR", ""                             },
        /* 9 */     { "write MSR",                     " disallowed by configuration" },
        /* 10 */    { "read MSR",                      " disallowed by configuration" },
    };
    AssertCompile(RT_ELEMENTS(s_aAccess) == APICMSRACCESS_COUNT);

    size_t const i = enmAccess;
    Assert(i < RT_ELEMENTS(s_aAccess));
    if (pVCpu->apic.s.cLogMaxAccessError++ < 5)
        LogRel(("APIC%u: Attempt to %s (%#x)%s -> #GP(0)\n", pVCpu->idCpu, s_aAccess[i].pszBefore, u32Reg, s_aAccess[i].pszAfter));
    return VERR_CPUM_RAISE_GP_0;
}


/**
 * Gets the descriptive APIC mode.
 *
 * @returns The name.
 * @param   enmMode     The xAPIC mode.
 */
const char *apicGetModeName(APICMODE enmMode)
{
    switch (enmMode)
    {
        case APICMODE_DISABLED:  return "Disabled";
        case APICMODE_XAPIC:     return "xAPIC";
        case APICMODE_X2APIC:    return "x2APIC";
        default:                 break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive destination format name.
 *
 * @returns The destination format name.
 * @param   enmDestFormat       The destination format.
 */
const char *apicGetDestFormatName(XAPICDESTFORMAT enmDestFormat)
{
    switch (enmDestFormat)
    {
        case XAPICDESTFORMAT_FLAT:      return "Flat";
        case XAPICDESTFORMAT_CLUSTER:   return "Cluster";
        default:                        break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive delivery mode name.
 *
 * @returns The delivery mode name.
 * @param   enmDeliveryMode     The delivery mode.
 */
const char *apicGetDeliveryModeName(XAPICDELIVERYMODE enmDeliveryMode)
{
    switch (enmDeliveryMode)
    {
        case XAPICDELIVERYMODE_FIXED:        return "Fixed";
        case XAPICDELIVERYMODE_LOWEST_PRIO:  return "Lowest-priority";
        case XAPICDELIVERYMODE_SMI:          return "SMI";
        case XAPICDELIVERYMODE_NMI:          return "NMI";
        case XAPICDELIVERYMODE_INIT:         return "INIT";
        case XAPICDELIVERYMODE_STARTUP:      return "SIPI";
        case XAPICDELIVERYMODE_EXTINT:       return "ExtINT";
        default:                             break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive destination mode name.
 *
 * @returns The destination mode name.
 * @param   enmDestMode     The destination mode.
 */
const char *apicGetDestModeName(XAPICDESTMODE enmDestMode)
{
    switch (enmDestMode)
    {
        case XAPICDESTMODE_PHYSICAL:  return "Physical";
        case XAPICDESTMODE_LOGICAL:   return "Logical";
        default:                      break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive trigger mode name.
 *
 * @returns The trigger mode name.
 * @param   enmTriggerMode     The trigger mode.
 */
const char *apicGetTriggerModeName(XAPICTRIGGERMODE enmTriggerMode)
{
    switch (enmTriggerMode)
    {
        case XAPICTRIGGERMODE_EDGE:     return "Edge";
        case XAPICTRIGGERMODE_LEVEL:    return "Level";
        default:                        break;
    }
    return "Invalid";
}


/**
 * Gets the destination shorthand name.
 *
 * @returns The destination shorthand name.
 * @param   enmDestShorthand     The destination shorthand.
 */
const char *apicGetDestShorthandName(XAPICDESTSHORTHAND enmDestShorthand)
{
    switch (enmDestShorthand)
    {
        case XAPICDESTSHORTHAND_NONE:           return "None";
        case XAPICDESTSHORTHAND_SELF:           return "Self";
        case XAPIDDESTSHORTHAND_ALL_INCL_SELF:  return "All including self";
        case XAPICDESTSHORTHAND_ALL_EXCL_SELF:  return "All excluding self";
        default:                                break;
    }
    return "Invalid";
}


/**
 * Gets the timer mode name.
 *
 * @returns The timer mode name.
 * @param   enmTimerMode         The timer mode.
 */
const char *apicGetTimerModeName(XAPICTIMERMODE enmTimerMode)
{
    switch (enmTimerMode)
    {
        case XAPICTIMERMODE_ONESHOT:        return "One-shot";
        case XAPICTIMERMODE_PERIODIC:       return "Periodic";
        case XAPICTIMERMODE_TSC_DEADLINE:   return "TSC deadline";
        default:                            break;
    }
    return "Invalid";
}


/**
 * Gets the APIC mode given the base MSR value.
 *
 * @returns The APIC mode.
 * @param   uApicBaseMsr        The APIC Base MSR value.
 */
APICMODE apicGetMode(uint64_t uApicBaseMsr)
{
    uint32_t const uMode   = (uApicBaseMsr >> 10) & UINT64_C(3);
    APICMODE const enmMode = (APICMODE)uMode;
#ifdef VBOX_STRICT
    /* Paranoia. */
    switch (uMode)
    {
        case APICMODE_DISABLED:
        case APICMODE_INVALID:
        case APICMODE_XAPIC:
        case APICMODE_X2APIC:
            break;
        default:
            AssertMsgFailed(("Invalid mode"));
    }
#endif
    return enmMode;
}


/**
 * Returns whether the APIC is hardware enabled or not.
 *
 * @returns true if enabled, false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) APICIsEnabled(PCVMCPUCC pVCpu)
{
    PCAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    return RT_BOOL(pApicCpu->uApicBaseMsr & MSR_IA32_APICBASE_EN);
}


/**
 * Finds the most significant set bit in an APIC 256-bit sparse register.
 *
 * @returns @a rcNotFound if no bit was set, 0-255 otherwise.
 * @param   pReg            The APIC 256-bit sparse register.
 * @param   rcNotFound      What to return when no bit is set.
 */
static int apicGetHighestSetBitInReg(volatile const XAPIC256BITREG *pReg, int rcNotFound)
{
    ssize_t const  cFragments     = RT_ELEMENTS(pReg->u);
    unsigned const uFragmentShift = 5;
    AssertCompile(1 << uFragmentShift == sizeof(pReg->u[0].u32Reg) * 8);
    for (ssize_t i = cFragments - 1; i >= 0; i--)
    {
        uint32_t const uFragment = pReg->u[i].u32Reg;
        if (uFragment)
        {
            unsigned idxSetBit = ASMBitLastSetU32(uFragment);
            --idxSetBit;
            idxSetBit |= i << uFragmentShift;
            return idxSetBit;
        }
    }
    return rcNotFound;
}


/**
 * Reads a 32-bit register at a specified offset.
 *
 * @returns The value at the specified offset.
 * @param   pXApicPage      The xAPIC page.
 * @param   offReg          The offset of the register being read.
 */
DECLINLINE(uint32_t) apicReadRaw32(PCXAPICPAGE pXApicPage, uint16_t offReg)
{
    Assert(offReg < sizeof(*pXApicPage) - sizeof(uint32_t));
    uint8_t  const *pbXApic =  (const uint8_t *)pXApicPage;
    uint32_t const  uValue  = *(const uint32_t *)(pbXApic + offReg);
    return uValue;
}


/**
 * Writes a 32-bit register at a specified offset.
 *
 * @param   pXApicPage      The xAPIC page.
 * @param   offReg          The offset of the register being written.
 * @param   uReg            The value of the register.
 */
DECLINLINE(void) apicWriteRaw32(PXAPICPAGE pXApicPage, uint16_t offReg, uint32_t uReg)
{
    Assert(offReg < sizeof(*pXApicPage) - sizeof(uint32_t));
    uint8_t *pbXApic = (uint8_t *)pXApicPage;
    *(uint32_t *)(pbXApic + offReg) = uReg;
}


/**
 * Sets an error in the internal ESR of the specified APIC.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uError          The error.
 * @thread  Any.
 */
DECLINLINE(void) apicSetError(PVMCPUCC pVCpu, uint32_t uError)
{
    PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    ASMAtomicOrU32(&pApicCpu->uEsrInternal, uError);
}


/**
 * Clears all errors in the internal ESR.
 *
 * @returns The value of the internal ESR before clearing.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
DECLINLINE(uint32_t) apicClearAllErrors(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    return ASMAtomicXchgU32(&pApicCpu->uEsrInternal, 0);
}


/**
 * Signals the guest if a pending interrupt is ready to be serviced.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void apicSignalNextPendingIntr(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    if (pXApicPage->svr.u.fApicSoftwareEnable)
    {
        int const irrv = apicGetHighestSetBitInReg(&pXApicPage->irr, -1 /* rcNotFound */);
        if (irrv >= 0)
        {
            Assert(irrv <= (int)UINT8_MAX);
            uint8_t const uVector = irrv;
            int const isrv        = apicGetHighestSetBitInReg(&pXApicPage->isr, 0 /* rcNotFound */);
            Assert(isrv <= (int)UINT8_MAX);
            uint8_t const uIsrVec = isrv;

            /* uIsrVect reflects the highest interrupt vector currently serviced (i.e. in ISR),
             * or zero if there's none. We want to report a pending interrupt only if IRR > ISR but
             * regardless of TPR. Hence we can't look at the PPR value, since that also reflects TPR.
             * NB: The APIC emulation will know when ISR changes, but not necessarily when TPR does.
             */
            if (XAPIC_PPR_GET_PP(uVector) > XAPIC_PPR_GET_PP(uIsrVec))
            {
                Log2(("APIC%u: apicSignalNextPendingIntr: Signalling pending interrupt. uVector=%#x\n", pVCpu->idCpu, uVector));
                apicSetInterruptFF(pVCpu, PDMAPICIRQ_HARDWARE);
            }
            else
                Log2(("APIC%u: apicSignalNextPendingIntr: Nothing to signal yet. uVector=%#x uIsrVec=%#x\n", pVCpu->idCpu, uVector, uIsrVec));
        }
    }
    else
    {
        Log2(("APIC%u: apicSignalNextPendingIntr: APIC software-disabled, clearing pending interrupt\n", pVCpu->idCpu));
        apicClearInterruptFF(pVCpu, PDMAPICIRQ_HARDWARE);
    }
}


/**
 * Sets the Spurious-Interrupt Vector Register (SVR).
 *
 * @returns VINF_SUCCESS or VERR_CPUM_RAISE_GP_0.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uSvr            The SVR value.
 */
static int apicSetSvr(PVMCPUCC pVCpu, uint32_t uSvr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    uint32_t   uValidMask = XAPIC_SVR_VALID;
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    if (pXApicPage->version.u.fEoiBroadcastSupression)
        uValidMask |= XAPIC_SVR_SUPRESS_EOI_BROADCAST;

    if (   XAPIC_IN_X2APIC_MODE(pVCpu)
        && (uSvr & ~uValidMask))
        return apicMsrAccessError(pVCpu, MSR_IA32_X2APIC_SVR, APICMSRACCESS_WRITE_RSVD_BITS);

    Log2(("APIC%u: apicSetSvr: uSvr=%#RX32\n", pVCpu->idCpu, uSvr));
    apicWriteRaw32(pXApicPage, XAPIC_OFF_SVR, uSvr);
    if (!pXApicPage->svr.u.fApicSoftwareEnable)
    {
        /** @todo CMCI. */
        pXApicPage->lvt_timer.u.u1Mask   = 1;
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
        pXApicPage->lvt_thermal.u.u1Mask = 1;
#endif
        pXApicPage->lvt_perf.u.u1Mask    = 1;
        pXApicPage->lvt_lint0.u.u1Mask   = 1;
        pXApicPage->lvt_lint1.u.u1Mask   = 1;
        pXApicPage->lvt_error.u.u1Mask   = 1;
    }

    apicSignalNextPendingIntr(pVCpu);
    return VINF_SUCCESS;
}


/**
 * Sends an interrupt to one or more APICs.
 *
 * @returns Strict VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure, can be
 *                              NULL if the source of the interrupt is not an
 *                              APIC (for e.g. a bus).
 * @param   uVector             The interrupt vector.
 * @param   enmTriggerMode      The trigger mode.
 * @param   enmDeliveryMode     The delivery mode.
 * @param   pDestCpuSet         The destination CPU set.
 * @param   pfIntrAccepted      Where to store whether this interrupt was
 *                              accepted by the target APIC(s) or not.
 *                              Optional, can be NULL.
 * @param   uSrcTag             The interrupt source tag (debugging).
 * @param   rcRZ                The return code if the operation cannot be
 *                              performed in the current context.
 */
static VBOXSTRICTRC apicSendIntr(PVMCC pVM, PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode,
                                 XAPICDELIVERYMODE enmDeliveryMode, PCVMCPUSET pDestCpuSet, bool *pfIntrAccepted,
                                 uint32_t uSrcTag, int rcRZ)
{
    AssertCompile(sizeof(pVM->apic.s) <= sizeof(pVM->apic.padding));
    AssertCompile(sizeof(pVCpu->apic.s) <= sizeof(pVCpu->apic.padding));
#ifdef IN_RING0
    AssertCompile(sizeof(pVM->apicr0.s) <= sizeof(pVM->apicr0.padding));
#endif
    VBOXSTRICTRC  rcStrict  = VINF_SUCCESS;
    VMCPUID const cCpus     = pVM->cCpus;
    bool          fAccepted = false;
    switch (enmDeliveryMode)
    {
        case XAPICDELIVERYMODE_FIXED:
        {
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    PVMCPUCC pItVCpu = pVM->CTX_SUFF(apCpus)[idCpu];
                    if (APICIsEnabled(pItVCpu))
                        fAccepted = apicPostInterrupt(pItVCpu, uVector, enmTriggerMode, uSrcTag);
                }
            break;
        }

        case XAPICDELIVERYMODE_LOWEST_PRIO:
        {
            VMCPUID const idCpu = VMCPUSET_FIND_FIRST_PRESENT(pDestCpuSet);
            AssertMsgBreak(idCpu < pVM->cCpus, ("APIC: apicSendIntr: No CPU found for lowest-priority delivery mode! idCpu=%u\n", idCpu));
            PVMCPUCC pVCpuDst = pVM->CTX_SUFF(apCpus)[idCpu];
            if (APICIsEnabled(pVCpuDst))
                fAccepted = apicPostInterrupt(pVCpuDst, uVector, enmTriggerMode, uSrcTag);
            else
                AssertMsgFailed(("APIC: apicSendIntr: Target APIC not enabled in lowest-priority delivery mode! idCpu=%u\n", idCpu));
            break;
        }

        case XAPICDELIVERYMODE_SMI:
        {
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Log2(("APIC: apicSendIntr: Raising SMI on VCPU%u\n", idCpu));
                    apicSetInterruptFF(pVM->CTX_SUFF(apCpus)[idCpu], PDMAPICIRQ_SMI);
                    fAccepted = true;
                }
            break;
        }

        case XAPICDELIVERYMODE_NMI:
        {
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    PVMCPUCC pItVCpu = pVM->CTX_SUFF(apCpus)[idCpu];
                    if (APICIsEnabled(pItVCpu))
                    {
                        Log2(("APIC: apicSendIntr: Raising NMI on VCPU%u\n", idCpu));
                        apicSetInterruptFF(pItVCpu, PDMAPICIRQ_NMI);
                        fAccepted = true;
                    }
                }
            break;
        }

        case XAPICDELIVERYMODE_INIT:
        {
#ifdef IN_RING3
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Log2(("APIC: apicSendIntr: Issuing INIT to VCPU%u\n", idCpu));
                    VMMR3SendInitIpi(pVM, idCpu);
                    fAccepted = true;
                }
#else
            /* We need to return to ring-3 to deliver the INIT. */
            rcStrict = rcRZ;
            fAccepted = true;
#endif
            break;
        }

        case XAPICDELIVERYMODE_STARTUP:
        {
#ifdef IN_RING3
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Log2(("APIC: apicSendIntr: Issuing SIPI to VCPU%u\n", idCpu));
                    VMMR3SendStartupIpi(pVM, idCpu, uVector);
                    fAccepted = true;
                }
#else
            /* We need to return to ring-3 to deliver the SIPI. */
            rcStrict = rcRZ;
            fAccepted = true;
            Log2(("APIC: apicSendIntr: SIPI issued, returning to RZ. rc=%Rrc\n", rcRZ));
#endif
            break;
        }

        case XAPICDELIVERYMODE_EXTINT:
        {
            for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
                if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
                {
                    Log2(("APIC: apicSendIntr: Raising EXTINT on VCPU%u\n", idCpu));
                    apicSetInterruptFF(pVM->CTX_SUFF(apCpus)[idCpu], PDMAPICIRQ_EXTINT);
                    fAccepted = true;
                }
            break;
        }

        default:
        {
            AssertMsgFailed(("APIC: apicSendIntr: Unsupported delivery mode %#x (%s)\n", enmDeliveryMode,
                             apicGetDeliveryModeName(enmDeliveryMode)));
            break;
        }
    }

    /*
     * If an illegal vector is programmed, set the 'send illegal vector' error here if the
     * interrupt is being sent by an APIC.
     *
     * The 'receive illegal vector' will be set on the target APIC when the interrupt
     * gets generated, see apicPostInterrupt().
     *
     * See Intel spec. 10.5.3 "Error Handling".
     */
    if (   rcStrict != rcRZ
        && pVCpu)
    {
        /*
         * Flag only errors when the delivery mode is fixed and not others.
         *
         * Ubuntu 10.04-3 amd64 live CD with 2 VCPUs gets upset as it sends an SIPI to the
         * 2nd VCPU with vector 6 and checks the ESR for no errors, see @bugref{8245#c86}.
         */
        /** @todo The spec says this for LVT, but not explcitly for ICR-lo
         *        but it probably is true. */
        if (enmDeliveryMode == XAPICDELIVERYMODE_FIXED)
        {
            if (RT_UNLIKELY(uVector <= XAPIC_ILLEGAL_VECTOR_END))
                apicSetError(pVCpu, XAPIC_ESR_SEND_ILLEGAL_VECTOR);
        }
    }

    if (pfIntrAccepted)
        *pfIntrAccepted = fAccepted;

    return rcStrict;
}


/**
 * Checks if this APIC belongs to a logical destination.
 *
 * @returns true if the APIC belongs to the logical
 *          destination, false otherwise.
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   fDest                   The destination mask.
 *
 * @thread  Any.
 */
static bool apicIsLogicalDest(PVMCPUCC pVCpu, uint32_t fDest)
{
    if (XAPIC_IN_X2APIC_MODE(pVCpu))
    {
        /*
         * Flat logical mode is not supported in x2APIC mode.
         * In clustered logical mode, the 32-bit logical ID in the LDR is interpreted as follows:
         *    - High 16 bits is the cluster ID.
         *    - Low 16 bits: each bit represents a unique APIC within the cluster.
         */
        PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpu);
        uint32_t const u32Ldr    = pX2ApicPage->ldr.u32LogicalApicId;
        if (X2APIC_LDR_GET_CLUSTER_ID(u32Ldr) == (fDest & X2APIC_LDR_CLUSTER_ID))
            return RT_BOOL(u32Ldr & fDest & X2APIC_LDR_LOGICAL_ID);
        return false;
    }

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    /*
     * In both flat and clustered logical mode, a destination mask of all set bits indicates a broadcast.
     * See AMD spec. 16.6.1 "Receiving System and IPI Interrupts".
     */
    Assert(!XAPIC_IN_X2APIC_MODE(pVCpu));
    if ((fDest & XAPIC_LDR_FLAT_LOGICAL_ID) == XAPIC_LDR_FLAT_LOGICAL_ID)
        return true;

    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    XAPICDESTFORMAT enmDestFormat = (XAPICDESTFORMAT)pXApicPage->dfr.u.u4Model;
    if (enmDestFormat == XAPICDESTFORMAT_FLAT)
    {
        /* The destination mask is interpreted as a bitmap of 8 unique logical APIC IDs. */
        uint8_t const u8Ldr = pXApicPage->ldr.u.u8LogicalApicId;
        return RT_BOOL(u8Ldr & fDest & XAPIC_LDR_FLAT_LOGICAL_ID);
    }

    /*
     * In clustered logical mode, the 8-bit logical ID in the LDR is interpreted as follows:
     *    - High 4 bits is the cluster ID.
     *    - Low 4 bits: each bit represents a unique APIC within the cluster.
     */
    Assert(enmDestFormat == XAPICDESTFORMAT_CLUSTER);
    uint8_t const u8Ldr = pXApicPage->ldr.u.u8LogicalApicId;
    if (XAPIC_LDR_CLUSTERED_GET_CLUSTER_ID(u8Ldr) == (fDest & XAPIC_LDR_CLUSTERED_CLUSTER_ID))
        return RT_BOOL(u8Ldr & fDest & XAPIC_LDR_CLUSTERED_LOGICAL_ID);
    return false;
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
}


/**
 * Figures out the set of destination CPUs for a given destination mode, format
 * and delivery mode setting.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fDestMask       The destination mask.
 * @param   fBroadcastMask  The broadcast mask.
 * @param   enmDestMode     The destination mode.
 * @param   enmDeliveryMode The delivery mode.
 * @param   pDestCpuSet     The destination CPU set to update.
 */
static void apicGetDestCpuSet(PVMCC pVM, uint32_t fDestMask, uint32_t fBroadcastMask, XAPICDESTMODE enmDestMode,
                              XAPICDELIVERYMODE enmDeliveryMode, PVMCPUSET pDestCpuSet)
{
    VMCPUSET_EMPTY(pDestCpuSet);

    /*
     * Physical destination mode only supports either a broadcast or a single target.
     *    - Broadcast with lowest-priority delivery mode is not supported[1], we deliver it
     *      as a regular broadcast like in fixed delivery mode.
     *    - For a single target, lowest-priority delivery mode makes no sense. We deliver
     *      to the target like in fixed delivery mode.
     *
     * [1] See Intel spec. 10.6.2.1 "Physical Destination Mode".
     */
    if (   enmDestMode == XAPICDESTMODE_PHYSICAL
        && enmDeliveryMode == XAPICDELIVERYMODE_LOWEST_PRIO)
    {
        AssertMsgFailed(("APIC: Lowest-priority delivery using physical destination mode!"));
        enmDeliveryMode = XAPICDELIVERYMODE_FIXED;
    }

    uint32_t const cCpus = pVM->cCpus;
    if (enmDeliveryMode == XAPICDELIVERYMODE_LOWEST_PRIO)
    {
        Assert(enmDestMode == XAPICDESTMODE_LOGICAL);
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
        VMCPUID idCpuLowestTpr = NIL_VMCPUID;
        uint8_t u8LowestTpr    = UINT8_C(0xff);
        for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        {
            PVMCPUCC pVCpuDst = pVM->CTX_SUFF(apCpus)[idCpu];
            if (apicIsLogicalDest(pVCpuDst, fDestMask))
            {
                PCXAPICPAGE   pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpuDst);
                uint8_t const u8Tpr      = pXApicPage->tpr.u8Tpr;         /* PAV */

                /*
                 * If there is a tie for lowest priority, the local APIC with the highest ID is chosen.
                 * Hence the use of "<=" in the check below.
                 * See AMD spec. 16.6.2 "Lowest Priority Messages and Arbitration".
                 */
                if (u8Tpr <= u8LowestTpr)
                {
                    u8LowestTpr    = u8Tpr;
                    idCpuLowestTpr = idCpu;
                }
            }
        }
        if (idCpuLowestTpr != NIL_VMCPUID)
            VMCPUSET_ADD(pDestCpuSet, idCpuLowestTpr);
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
        return;
    }

    /*
     * x2APIC:
     *    - In both physical and logical destination mode, a destination mask of 0xffffffff implies a broadcast[1].
     * xAPIC:
     *    - In physical destination mode, a destination mask of 0xff implies a broadcast[2].
     *    - In both flat and clustered logical mode, a destination mask of 0xff implies a broadcast[3].
     *
     * [1] See Intel spec. 10.12.9 "ICR Operation in x2APIC Mode".
     * [2] See Intel spec. 10.6.2.1 "Physical Destination Mode".
     * [2] See AMD spec. 16.6.1 "Receiving System and IPI Interrupts".
     */
    if ((fDestMask & fBroadcastMask) == fBroadcastMask)
    {
        VMCPUSET_FILL(pDestCpuSet);
        return;
    }

    if (enmDestMode == XAPICDESTMODE_PHYSICAL)
    {
        /* The destination mask is interpreted as the physical APIC ID of a single target. */
#if 1
        /* Since our physical APIC ID is read-only to software, set the corresponding bit in the CPU set. */
        if (RT_LIKELY(fDestMask < cCpus))
            VMCPUSET_ADD(pDestCpuSet, fDestMask);
#else
        /* The physical APIC ID may not match our VCPU ID, search through the list of targets. */
        for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        {
            PVMCPUCC pVCpuDst = &pVM->aCpus[idCpu];
            if (XAPIC_IN_X2APIC_MODE(pVCpuDst))
            {
                PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpuDst);
                if (pX2ApicPage->id.u32ApicId == fDestMask)
                    VMCPUSET_ADD(pDestCpuSet, pVCpuDst->idCpu);
            }
            else
            {
                PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpuDst);
                if (pXApicPage->id.u8ApicId == (uint8_t)fDestMask)
                    VMCPUSET_ADD(pDestCpuSet, pVCpuDst->idCpu);
            }
        }
#endif
    }
    else
    {
        Assert(enmDestMode == XAPICDESTMODE_LOGICAL);

        /* A destination mask of all 0's implies no target APICs (since it's interpreted as a bitmap or partial bitmap). */
        if (RT_UNLIKELY(!fDestMask))
            return;

        /* The destination mask is interpreted as a bitmap of software-programmable logical APIC ID of the target APICs. */
        for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        {
            PVMCPUCC pVCpuDst = pVM->CTX_SUFF(apCpus)[idCpu];
            if (apicIsLogicalDest(pVCpuDst, fDestMask))
                VMCPUSET_ADD(pDestCpuSet, pVCpuDst->idCpu);
        }
    }
}


/**
 * Sends an Interprocessor Interrupt (IPI) using values from the Interrupt
 * Command Register (ICR).
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   rcRZ            The return code if the operation cannot be
 *                          performed in the current context.
 */
DECLINLINE(VBOXSTRICTRC) apicSendIpi(PVMCPUCC pVCpu, int rcRZ)
{
    VMCPU_ASSERT_EMT(pVCpu);

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    XAPICDELIVERYMODE const  enmDeliveryMode  = (XAPICDELIVERYMODE)pXApicPage->icr_lo.u.u3DeliveryMode;
    XAPICDESTMODE const      enmDestMode      = (XAPICDESTMODE)pXApicPage->icr_lo.u.u1DestMode;
    XAPICINITLEVEL const     enmInitLevel     = (XAPICINITLEVEL)pXApicPage->icr_lo.u.u1Level;
    XAPICTRIGGERMODE const   enmTriggerMode   = (XAPICTRIGGERMODE)pXApicPage->icr_lo.u.u1TriggerMode;
    XAPICDESTSHORTHAND const enmDestShorthand = (XAPICDESTSHORTHAND)pXApicPage->icr_lo.u.u2DestShorthand;
    uint8_t const            uVector          = pXApicPage->icr_lo.u.u8Vector;

    PX2APICPAGE pX2ApicPage = VMCPU_TO_X2APICPAGE(pVCpu);
    uint32_t const fDest    = XAPIC_IN_X2APIC_MODE(pVCpu) ? pX2ApicPage->icr_hi.u32IcrHi : pXApicPage->icr_hi.u.u8Dest;
    Log5(("apicSendIpi: delivery=%u mode=%u init=%u trigger=%u short=%u vector=%#x fDest=%#x\n",
          enmDeliveryMode, enmDestMode, enmInitLevel, enmTriggerMode, enmDestShorthand, uVector, fDest));

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
        Log2(("APIC%u: %s level de-assert unsupported, ignoring!\n", pVCpu->idCpu, apicGetDeliveryModeName(enmDeliveryMode)));
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
            PVMCC pVM = pVCpu->CTX_SUFF(pVM);
            uint32_t const fBroadcastMask = XAPIC_IN_X2APIC_MODE(pVCpu) ? X2APIC_ID_BROADCAST_MASK : XAPIC_ID_BROADCAST_MASK;
            apicGetDestCpuSet(pVM, fDest, fBroadcastMask, enmDestMode, enmDeliveryMode, &DestCpuSet);
            break;
        }

        case XAPICDESTSHORTHAND_SELF:
        {
            VMCPUSET_EMPTY(&DestCpuSet);
            VMCPUSET_ADD(&DestCpuSet, pVCpu->idCpu);
            break;
        }

        case XAPIDDESTSHORTHAND_ALL_INCL_SELF:
        {
            VMCPUSET_FILL(&DestCpuSet);
            break;
        }

        case XAPICDESTSHORTHAND_ALL_EXCL_SELF:
        {
            VMCPUSET_FILL(&DestCpuSet);
            VMCPUSET_DEL(&DestCpuSet, pVCpu->idCpu);
            break;
        }
    }

    return apicSendIntr(pVCpu->CTX_SUFF(pVM), pVCpu, uVector, enmTriggerMode, enmDeliveryMode, &DestCpuSet,
                        NULL /* pfIntrAccepted */, 0 /* uSrcTag */, rcRZ);
}


/**
 * Sets the Interrupt Command Register (ICR) high dword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uIcrHi          The ICR high dword.
 */
static VBOXSTRICTRC apicSetIcrHi(PVMCPUCC pVCpu, uint32_t uIcrHi)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(!XAPIC_IN_X2APIC_MODE(pVCpu));

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    pXApicPage->icr_hi.all.u32IcrHi = uIcrHi & XAPIC_ICR_HI_DEST;
    STAM_COUNTER_INC(&pVCpu->apic.s.StatIcrHiWrite);
    Log2(("APIC%u: apicSetIcrHi: uIcrHi=%#RX32\n", pVCpu->idCpu, pXApicPage->icr_hi.all.u32IcrHi));

    return VINF_SUCCESS;
}


/**
 * Sets the Interrupt Command Register (ICR) low dword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uIcrLo          The ICR low dword.
 * @param   rcRZ            The return code if the operation cannot be performed
 *                          in the current context.
 * @param   fUpdateStat     Whether to update the ICR low write statistics
 *                          counter.
 */
static VBOXSTRICTRC apicSetIcrLo(PVMCPUCC pVCpu, uint32_t uIcrLo, int rcRZ, bool fUpdateStat)
{
    VMCPU_ASSERT_EMT(pVCpu);

    PXAPICPAGE pXApicPage  = VMCPU_TO_XAPICPAGE(pVCpu);
    pXApicPage->icr_lo.all.u32IcrLo = uIcrLo & XAPIC_ICR_LO_WR_VALID;
    Log2(("APIC%u: apicSetIcrLo: uIcrLo=%#RX32\n", pVCpu->idCpu, pXApicPage->icr_lo.all.u32IcrLo));

    if (fUpdateStat)
        STAM_COUNTER_INC(&pVCpu->apic.s.StatIcrLoWrite);
    RT_NOREF(fUpdateStat);

    return apicSendIpi(pVCpu, rcRZ);
}


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
static VBOXSTRICTRC apicSetIcr(PVMCPUCC pVCpu, uint64_t u64Icr, int rcRZ)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /* Validate. */
    uint32_t const uLo = RT_LO_U32(u64Icr);
    if (RT_LIKELY(!(uLo & ~XAPIC_ICR_LO_WR_VALID)))
    {
        /* Update high dword first, then update the low dword which sends the IPI. */
        PX2APICPAGE pX2ApicPage = VMCPU_TO_X2APICPAGE(pVCpu);
        pX2ApicPage->icr_hi.u32IcrHi = RT_HI_U32(u64Icr);
        STAM_COUNTER_INC(&pVCpu->apic.s.StatIcrFullWrite);
        return apicSetIcrLo(pVCpu, uLo, rcRZ, false /* fUpdateStat */);
    }
    return apicMsrAccessError(pVCpu, MSR_IA32_X2APIC_ICR, APICMSRACCESS_WRITE_RSVD_BITS);
}


/**
 * Sets the Error Status Register (ESR).
 *
 * @returns VINF_SUCCESS or VERR_CPUM_RAISE_GP_0.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uEsr            The ESR value.
 */
static int apicSetEsr(PVMCPUCC pVCpu, uint32_t uEsr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    Log2(("APIC%u: apicSetEsr: uEsr=%#RX32\n", pVCpu->idCpu, uEsr));

    if (   XAPIC_IN_X2APIC_MODE(pVCpu)
        && (uEsr & ~XAPIC_ESR_WO_VALID))
        return apicMsrAccessError(pVCpu, MSR_IA32_X2APIC_ESR, APICMSRACCESS_WRITE_RSVD_BITS);

    /*
     * Writes to the ESR causes the internal state to be updated in the register,
     * clearing the original state. See AMD spec. 16.4.6 "APIC Error Interrupts".
     */
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    pXApicPage->esr.all.u32Errors = apicClearAllErrors(pVCpu);
    return VINF_SUCCESS;
}


/**
 * Updates the Processor Priority Register (PPR).
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void apicUpdatePpr(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /* See Intel spec 10.8.3.1 "Task and Processor Priorities". */
    PXAPICPAGE    pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    uint8_t const uIsrv      = apicGetHighestSetBitInReg(&pXApicPage->isr, 0 /* rcNotFound */);
    uint8_t       uPpr;
    if (XAPIC_TPR_GET_TP(pXApicPage->tpr.u8Tpr) >= XAPIC_PPR_GET_PP(uIsrv))
        uPpr = pXApicPage->tpr.u8Tpr;
    else
        uPpr = XAPIC_PPR_GET_PP(uIsrv);
    pXApicPage->ppr.u8Ppr = uPpr;
}


/**
 * Gets the Processor Priority Register (PPR).
 *
 * @returns The PPR value.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static uint8_t apicGetPpr(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    STAM_COUNTER_INC(&pVCpu->apic.s.StatTprRead);

    /*
     * With virtualized APIC registers or with TPR virtualization, the hardware may
     * update ISR/TPR transparently. We thus re-calculate the PPR which may be out of sync.
     * See Intel spec. 29.2.2 "Virtual-Interrupt Delivery".
     *
     * In all other instances, whenever the TPR or ISR changes, we need to update the PPR
     * as well (e.g. like we do manually in apicR3InitIpi and by calling apicUpdatePpr).
     */
    PCAPIC pApic = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    if (pApic->fVirtApicRegsEnabled)        /** @todo re-think this */
        apicUpdatePpr(pVCpu);
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    return pXApicPage->ppr.u8Ppr;
}


/**
 * Sets the Task Priority Register (TPR).
 *
 * @returns VINF_SUCCESS or VERR_CPUM_RAISE_GP_0.
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   uTpr                    The TPR value.
 * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during
 *                                  this write.
 */
static int apicSetTprEx(PVMCPUCC pVCpu, uint32_t uTpr, bool fForceX2ApicBehaviour)
{
    VMCPU_ASSERT_EMT(pVCpu);

    Log2(("APIC%u: apicSetTprEx: uTpr=%#RX32\n", pVCpu->idCpu, uTpr));
    STAM_COUNTER_INC(&pVCpu->apic.s.StatTprWrite);

    bool const fX2ApicMode = XAPIC_IN_X2APIC_MODE(pVCpu) || fForceX2ApicBehaviour;
    if (   fX2ApicMode
        && (uTpr & ~XAPIC_TPR_VALID))
        return apicMsrAccessError(pVCpu, MSR_IA32_X2APIC_TPR, APICMSRACCESS_WRITE_RSVD_BITS);

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    pXApicPage->tpr.u8Tpr = uTpr;
    apicUpdatePpr(pVCpu);
    apicSignalNextPendingIntr(pVCpu);
    return VINF_SUCCESS;
}


/**
 * Sets the End-Of-Interrupt (EOI) register.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   uEoi                    The EOI value.
 * @param   fForceX2ApicBehaviour   Pretend the APIC is in x2APIC mode during
 *                                  this write.
 */
static VBOXSTRICTRC apicSetEoi(PVMCPUCC pVCpu, uint32_t uEoi, bool fForceX2ApicBehaviour)
{
    VMCPU_ASSERT_EMT(pVCpu);

    Log2(("APIC%u: apicSetEoi: uEoi=%#RX32\n", pVCpu->idCpu, uEoi));
    STAM_COUNTER_INC(&pVCpu->apic.s.StatEoiWrite);

    bool const fX2ApicMode = XAPIC_IN_X2APIC_MODE(pVCpu) || fForceX2ApicBehaviour;
    if (   fX2ApicMode
        && (uEoi & ~XAPIC_EOI_WO_VALID))
        return apicMsrAccessError(pVCpu, MSR_IA32_X2APIC_EOI, APICMSRACCESS_WRITE_RSVD_BITS);

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    int isrv = apicGetHighestSetBitInReg(&pXApicPage->isr, -1 /* rcNotFound */);
    if (isrv >= 0)
    {
        /*
         * Broadcast the EOI to the I/O APIC(s).
         *
         * We'll handle the EOI broadcast first as there is tiny chance we get rescheduled to
         * ring-3 due to contention on the I/O APIC lock. This way we don't mess with the rest
         * of the APIC state and simply restart the EOI write operation from ring-3.
         */
        Assert(isrv <= (int)UINT8_MAX);
        uint8_t const uVector      = isrv;
        bool const fLevelTriggered = apicTestVectorInReg(&pXApicPage->tmr, uVector);
        if (fLevelTriggered)
        {
            PDMIoApicBroadcastEoi(pVCpu->CTX_SUFF(pVM), uVector);

            /*
             * Clear the vector from the TMR.
             *
             * The broadcast to I/O APIC can re-trigger new interrupts to arrive via the bus. However,
             * APICUpdatePendingInterrupts() which updates TMR can only be done from EMT which we
             * currently are on, so no possibility of concurrent updates.
             */
            apicClearVectorInReg(&pXApicPage->tmr, uVector);

            /*
             * Clear the remote IRR bit for level-triggered, fixed mode LINT0 interrupt.
             * The LINT1 pin does not support level-triggered interrupts.
             * See Intel spec. 10.5.1 "Local Vector Table".
             */
            uint32_t const uLvtLint0 = pXApicPage->lvt_lint0.all.u32LvtLint0;
            if (   XAPIC_LVT_GET_REMOTE_IRR(uLvtLint0)
                && XAPIC_LVT_GET_VECTOR(uLvtLint0) == uVector
                && XAPIC_LVT_GET_DELIVERY_MODE(uLvtLint0) == XAPICDELIVERYMODE_FIXED)
            {
                ASMAtomicAndU32((volatile uint32_t *)&pXApicPage->lvt_lint0.all.u32LvtLint0, ~XAPIC_LVT_REMOTE_IRR);
                Log2(("APIC%u: apicSetEoi: Cleared remote-IRR for LINT0. uVector=%#x\n", pVCpu->idCpu, uVector));
            }

            Log2(("APIC%u: apicSetEoi: Cleared level triggered interrupt from TMR. uVector=%#x\n", pVCpu->idCpu, uVector));
        }

        /*
         * Mark interrupt as serviced, update the PPR and signal pending interrupts.
         */
        Log2(("APIC%u: apicSetEoi: Clearing interrupt from ISR. uVector=%#x\n", pVCpu->idCpu, uVector));
        apicClearVectorInReg(&pXApicPage->isr, uVector);
        apicUpdatePpr(pVCpu);
        apicSignalNextPendingIntr(pVCpu);
    }
    else
    {
#ifdef DEBUG_ramshankar
        /** @todo Figure out if this is done intentionally by guests or is a bug
         *   in our emulation. Happened with Win10 SMP VM during reboot after
         *   installation of guest additions with 3D support. */
        AssertMsgFailed(("APIC%u: apicSetEoi: Failed to find any ISR bit\n", pVCpu->idCpu));
#endif
    }

    return VINF_SUCCESS;
}


/**
 * Sets the Logical Destination Register (LDR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uLdr            The LDR value.
 *
 * @remarks LDR is read-only in x2APIC mode.
 */
static VBOXSTRICTRC apicSetLdr(PVMCPUCC pVCpu, uint32_t uLdr)
{
    VMCPU_ASSERT_EMT(pVCpu);
    PCAPIC pApic = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    Assert(!XAPIC_IN_X2APIC_MODE(pVCpu) || pApic->fHyperVCompatMode); RT_NOREF_PV(pApic);

    Log2(("APIC%u: apicSetLdr: uLdr=%#RX32\n", pVCpu->idCpu, uLdr));

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    apicWriteRaw32(pXApicPage, XAPIC_OFF_LDR, uLdr & XAPIC_LDR_VALID);
    STAM_COUNTER_INC(&pVCpu->apic.s.StatLdrWrite);
    return VINF_SUCCESS;
}


/**
 * Sets the Destination Format Register (DFR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uDfr            The DFR value.
 *
 * @remarks DFR is not available in x2APIC mode.
 */
static VBOXSTRICTRC apicSetDfr(PVMCPUCC pVCpu, uint32_t uDfr)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(!XAPIC_IN_X2APIC_MODE(pVCpu));

    uDfr &= XAPIC_DFR_VALID;
    uDfr |= XAPIC_DFR_RSVD_MB1;

    Log2(("APIC%u: apicSetDfr: uDfr=%#RX32\n", pVCpu->idCpu, uDfr));

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    apicWriteRaw32(pXApicPage, XAPIC_OFF_DFR, uDfr);
    STAM_COUNTER_INC(&pVCpu->apic.s.StatDfrWrite);
    return VINF_SUCCESS;
}


/**
 * Sets the Timer Divide Configuration Register (DCR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uTimerDcr       The timer DCR value.
 */
static VBOXSTRICTRC apicSetTimerDcr(PVMCPUCC pVCpu, uint32_t uTimerDcr)
{
    VMCPU_ASSERT_EMT(pVCpu);
    if (   XAPIC_IN_X2APIC_MODE(pVCpu)
        && (uTimerDcr & ~XAPIC_TIMER_DCR_VALID))
        return apicMsrAccessError(pVCpu, MSR_IA32_X2APIC_TIMER_DCR, APICMSRACCESS_WRITE_RSVD_BITS);

    Log2(("APIC%u: apicSetTimerDcr: uTimerDcr=%#RX32\n", pVCpu->idCpu, uTimerDcr));

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    apicWriteRaw32(pXApicPage, XAPIC_OFF_TIMER_DCR, uTimerDcr);
    STAM_COUNTER_INC(&pVCpu->apic.s.StatDcrWrite);
    return VINF_SUCCESS;
}


/**
 * Gets the timer's Current Count Register (CCR).
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   rcBusy          The busy return code for the timer critical section.
 * @param   puValue         Where to store the LVT timer CCR.
 */
static VBOXSTRICTRC apicGetTimerCcr(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, int rcBusy, uint32_t *puValue)
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
    PCAPICCPU       pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    TMTIMERHANDLE   hTimer   = pApicCpu->hTimer;

    VBOXSTRICTRC rc = PDMDevHlpTimerLockClock(pDevIns, hTimer, rcBusy);
    if (rc == VINF_SUCCESS)
    {
        /* If the current-count register is 0, it implies the timer expired. */
        uint32_t const uCurrentCount = pXApicPage->timer_ccr.u32CurrentCount;
        if (uCurrentCount)
        {
            uint64_t const cTicksElapsed = PDMDevHlpTimerGet(pDevIns, hTimer) - pApicCpu->u64TimerInitial;
            PDMDevHlpTimerUnlockClock(pDevIns, hTimer);
            uint8_t  const uTimerShift   = apicGetTimerShift(pXApicPage);
            uint64_t const uDelta        = cTicksElapsed >> uTimerShift;
            if (uInitialCount > uDelta)
                *puValue = uInitialCount - uDelta;
        }
        else
            PDMDevHlpTimerUnlockClock(pDevIns, hTimer);
    }
    return rc;
}


/**
 * Sets the timer's Initial-Count Register (ICR).
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   rcBusy          The busy return code for the timer critical section.
 * @param   uInitialCount   The timer ICR.
 */
static VBOXSTRICTRC apicSetTimerIcr(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, int rcBusy, uint32_t uInitialCount)
{
    VMCPU_ASSERT_EMT(pVCpu);

    PAPIC      pApic      = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    PAPICCPU   pApicCpu   = VMCPU_TO_APICCPU(pVCpu);
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    Log2(("APIC%u: apicSetTimerIcr: uInitialCount=%#RX32\n", pVCpu->idCpu, uInitialCount));
    STAM_COUNTER_INC(&pApicCpu->StatTimerIcrWrite);

    /* In TSC-deadline mode, timer ICR writes are ignored, see Intel spec. 10.5.4.1 "TSC-Deadline Mode". */
    if (   pApic->fSupportsTscDeadline
        && pXApicPage->lvt_timer.u.u2TimerMode == XAPIC_TIMER_MODE_TSC_DEADLINE)
        return VINF_SUCCESS;

    /*
     * The timer CCR may be modified by apicR3TimerCallback() in parallel,
     * so obtain the lock -before- updating it here to be consistent with the
     * timer ICR. We rely on CCR being consistent in apicGetTimerCcr().
     */
    TMTIMERHANDLE hTimer = pApicCpu->hTimer;
    VBOXSTRICTRC rc = PDMDevHlpTimerLockClock(pDevIns, hTimer, rcBusy);
    if (rc == VINF_SUCCESS)
    {
        pXApicPage->timer_icr.u32InitialCount = uInitialCount;
        pXApicPage->timer_ccr.u32CurrentCount = uInitialCount;
        if (uInitialCount)
            apicStartTimer(pVCpu, uInitialCount);
        else
            apicStopTimer(pVCpu);
        PDMDevHlpTimerUnlockClock(pDevIns, hTimer);
    }
    return rc;
}


/**
 * Sets an LVT entry.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offLvt          The LVT entry offset in the xAPIC page.
 * @param   uLvt            The LVT value to set.
 */
static VBOXSTRICTRC apicSetLvtEntry(PVMCPUCC pVCpu, uint16_t offLvt, uint32_t uLvt)
{
    VMCPU_ASSERT_EMT(pVCpu);

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    AssertMsg(   offLvt == XAPIC_OFF_LVT_TIMER
              || offLvt == XAPIC_OFF_LVT_THERMAL
              || offLvt == XAPIC_OFF_LVT_PERF
              || offLvt == XAPIC_OFF_LVT_LINT0
              || offLvt == XAPIC_OFF_LVT_LINT1
              || offLvt == XAPIC_OFF_LVT_ERROR,
             ("APIC%u: apicSetLvtEntry: invalid offset, offLvt=%#RX16, uLvt=%#RX32\n", pVCpu->idCpu, offLvt, uLvt));

    /*
     * If TSC-deadline mode isn't support, ignore the bit in xAPIC mode
     * and raise #GP(0) in x2APIC mode.
     */
    PCAPIC pApic = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    if (offLvt == XAPIC_OFF_LVT_TIMER)
    {
        STAM_COUNTER_INC(&pVCpu->apic.s.StatLvtTimerWrite);
        if (   !pApic->fSupportsTscDeadline
            && (uLvt & XAPIC_LVT_TIMER_TSCDEADLINE))
        {
            if (XAPIC_IN_X2APIC_MODE(pVCpu))
                return apicMsrAccessError(pVCpu, XAPIC_GET_X2APIC_MSR(offLvt), APICMSRACCESS_WRITE_RSVD_BITS);
            uLvt &= ~XAPIC_LVT_TIMER_TSCDEADLINE;
            /** @todo TSC-deadline timer mode transition */
        }
    }

    /*
     * Validate rest of the LVT bits.
     */
    uint16_t const idxLvt = (offLvt - XAPIC_OFF_LVT_START) >> 4;
    AssertReturn(idxLvt < RT_ELEMENTS(g_au32LvtValidMasks), VERR_OUT_OF_RANGE);

    /*
     * For x2APIC, disallow setting of invalid/reserved bits.
     * For xAPIC, mask out invalid/reserved bits (i.e. ignore them).
     */
    if (   XAPIC_IN_X2APIC_MODE(pVCpu)
        && (uLvt & ~g_au32LvtValidMasks[idxLvt]))
        return apicMsrAccessError(pVCpu, XAPIC_GET_X2APIC_MSR(offLvt), APICMSRACCESS_WRITE_RSVD_BITS);

    uLvt &= g_au32LvtValidMasks[idxLvt];

    /*
     * In the software-disabled state, LVT mask-bit must remain set and attempts to clear the mask
     * bit must be ignored. See Intel spec. 10.4.7.2 "Local APIC State After It Has Been Software Disabled".
     */
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    if (!pXApicPage->svr.u.fApicSoftwareEnable)
        uLvt |= XAPIC_LVT_MASK;

    /*
     * It is unclear whether we should signal a 'send illegal vector' error here and ignore updating
     * the LVT entry when the delivery mode is 'fixed'[1] or update it in addition to signalling the
     * error or not signal the error at all. For now, we'll allow setting illegal vectors into the LVT
     * but set the 'send illegal vector' error here. The 'receive illegal vector' error will be set if
     * the interrupt for the vector happens to be generated, see apicPostInterrupt().
     *
     * [1] See Intel spec. 10.5.2 "Valid Interrupt Vectors".
     */
    if (RT_UNLIKELY(   XAPIC_LVT_GET_VECTOR(uLvt) <= XAPIC_ILLEGAL_VECTOR_END
                    && XAPIC_LVT_GET_DELIVERY_MODE(uLvt) == XAPICDELIVERYMODE_FIXED))
        apicSetError(pVCpu, XAPIC_ESR_SEND_ILLEGAL_VECTOR);

    Log2(("APIC%u: apicSetLvtEntry: offLvt=%#RX16 uLvt=%#RX32\n", pVCpu->idCpu, offLvt, uLvt));

    apicWriteRaw32(pXApicPage, offLvt, uLvt);
    return VINF_SUCCESS;
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif  /* XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4 */
}


#if 0
/**
 * Sets an LVT entry in the extended LVT range.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offLvt          The LVT entry offset in the xAPIC page.
 * @param   uValue          The LVT value to set.
 */
static int apicSetLvtExtEntry(PVMCPUCC pVCpu, uint16_t offLvt, uint32_t uLvt)
{
    VMCPU_ASSERT_EMT(pVCpu);
    AssertMsg(offLvt == XAPIC_OFF_CMCI, ("APIC%u: apicSetLvt1Entry: invalid offset %#RX16\n", pVCpu->idCpu, offLvt));

    /** @todo support CMCI. */
    return VERR_NOT_IMPLEMENTED;
}
#endif


/**
 * Hints TM about the APIC timer frequency.
 *
 * @param   pDevIns         The device instance.
 * @param   pApicCpu        The APIC CPU state.
 * @param   uInitialCount   The new initial count.
 * @param   uTimerShift     The new timer shift.
 * @thread  Any.
 */
void apicHintTimerFreq(PPDMDEVINS pDevIns, PAPICCPU pApicCpu, uint32_t uInitialCount, uint8_t uTimerShift)
{
    Assert(pApicCpu);

    if (   pApicCpu->uHintedTimerInitialCount != uInitialCount
        || pApicCpu->uHintedTimerShift        != uTimerShift)
    {
        uint32_t uHz;
        if (uInitialCount)
        {
            uint64_t cTicksPerPeriod = (uint64_t)uInitialCount << uTimerShift;
            uHz = PDMDevHlpTimerGetFreq(pDevIns, pApicCpu->hTimer) / cTicksPerPeriod;
        }
        else
            uHz = 0;

        PDMDevHlpTimerSetFrequencyHint(pDevIns, pApicCpu->hTimer, uHz);
        pApicCpu->uHintedTimerInitialCount = uInitialCount;
        pApicCpu->uHintedTimerShift = uTimerShift;
    }
}


/**
 * Gets the Interrupt Command Register (ICR), without performing any interface
 * checks.
 *
 * @returns The ICR value.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
DECLINLINE(uint64_t) apicGetIcrNoCheck(PVMCPUCC pVCpu)
{
    PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpu);
    uint64_t const uHi  = pX2ApicPage->icr_hi.u32IcrHi;
    uint64_t const uLo  = pX2ApicPage->icr_lo.all.u32IcrLo;
    uint64_t const uIcr = RT_MAKE_U64(uLo, uHi);
    return uIcr;
}


/**
 * Reads an APIC register.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being read.
 * @param   puValue         Where to store the register value.
 */
DECLINLINE(VBOXSTRICTRC) apicReadRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t *puValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(offReg <= XAPIC_OFF_MAX_VALID);

    PXAPICPAGE   pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    uint32_t     uValue = 0;
    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (offReg)
    {
        case XAPIC_OFF_ID:
        case XAPIC_OFF_VERSION:
        case XAPIC_OFF_TPR:
        case XAPIC_OFF_EOI:
        case XAPIC_OFF_RRD:
        case XAPIC_OFF_LDR:
        case XAPIC_OFF_DFR:
        case XAPIC_OFF_SVR:
        case XAPIC_OFF_ISR0:    case XAPIC_OFF_ISR1:    case XAPIC_OFF_ISR2:    case XAPIC_OFF_ISR3:
        case XAPIC_OFF_ISR4:    case XAPIC_OFF_ISR5:    case XAPIC_OFF_ISR6:    case XAPIC_OFF_ISR7:
        case XAPIC_OFF_TMR0:    case XAPIC_OFF_TMR1:    case XAPIC_OFF_TMR2:    case XAPIC_OFF_TMR3:
        case XAPIC_OFF_TMR4:    case XAPIC_OFF_TMR5:    case XAPIC_OFF_TMR6:    case XAPIC_OFF_TMR7:
        case XAPIC_OFF_IRR0:    case XAPIC_OFF_IRR1:    case XAPIC_OFF_IRR2:    case XAPIC_OFF_IRR3:
        case XAPIC_OFF_IRR4:    case XAPIC_OFF_IRR5:    case XAPIC_OFF_IRR6:    case XAPIC_OFF_IRR7:
        case XAPIC_OFF_ESR:
        case XAPIC_OFF_ICR_LO:
        case XAPIC_OFF_ICR_HI:
        case XAPIC_OFF_LVT_TIMER:
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
        case XAPIC_OFF_LVT_THERMAL:
#endif
        case XAPIC_OFF_LVT_PERF:
        case XAPIC_OFF_LVT_LINT0:
        case XAPIC_OFF_LVT_LINT1:
        case XAPIC_OFF_LVT_ERROR:
        case XAPIC_OFF_TIMER_ICR:
        case XAPIC_OFF_TIMER_DCR:
        {
            Assert(   !XAPIC_IN_X2APIC_MODE(pVCpu)
                   || (   offReg != XAPIC_OFF_DFR
                       && offReg != XAPIC_OFF_ICR_HI
                       && offReg != XAPIC_OFF_EOI));
            uValue = apicReadRaw32(pXApicPage, offReg);
            Log2(("APIC%u: apicReadRegister: offReg=%#x uValue=%#x\n", pVCpu->idCpu, offReg, uValue));
            break;
        }

        case XAPIC_OFF_PPR:
        {
            uValue = apicGetPpr(pVCpu);
            break;
        }

        case XAPIC_OFF_TIMER_CCR:
        {
            Assert(!XAPIC_IN_X2APIC_MODE(pVCpu));
            rc = apicGetTimerCcr(pDevIns, pVCpu, VINF_IOM_R3_MMIO_READ, &uValue);
            break;
        }

        case XAPIC_OFF_APR:
        {
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
            /* Unsupported on Pentium 4 and Xeon CPUs, invalid in x2APIC mode. */
            Assert(!XAPIC_IN_X2APIC_MODE(pVCpu));
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
            break;
        }

        default:
        {
            Assert(!XAPIC_IN_X2APIC_MODE(pVCpu));
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "VCPU[%u]: offReg=%#RX16\n", pVCpu->idCpu, offReg);
            apicSetError(pVCpu, XAPIC_ESR_ILLEGAL_REG_ADDRESS);
            break;
        }
    }

    *puValue = uValue;
    return rc;
}


/**
 * Writes an APIC register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being written.
 * @param   uValue          The register value.
 */
DECLINLINE(VBOXSTRICTRC) apicWriteRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t uValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(offReg <= XAPIC_OFF_MAX_VALID);
    Assert(!XAPIC_IN_X2APIC_MODE(pVCpu));

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    switch (offReg)
    {
        case XAPIC_OFF_TPR:
        {
            rcStrict = apicSetTprEx(pVCpu, uValue, false /* fForceX2ApicBehaviour */);
            break;
        }

        case XAPIC_OFF_LVT_TIMER:
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
        case XAPIC_OFF_LVT_THERMAL:
#endif
        case XAPIC_OFF_LVT_PERF:
        case XAPIC_OFF_LVT_LINT0:
        case XAPIC_OFF_LVT_LINT1:
        case XAPIC_OFF_LVT_ERROR:
        {
            rcStrict = apicSetLvtEntry(pVCpu, offReg, uValue);
            break;
        }

        case XAPIC_OFF_TIMER_ICR:
        {
            rcStrict = apicSetTimerIcr(pDevIns, pVCpu, VINF_IOM_R3_MMIO_WRITE, uValue);
            break;
        }

        case XAPIC_OFF_EOI:
        {
            rcStrict = apicSetEoi(pVCpu, uValue, false /* fForceX2ApicBehaviour */);
            break;
        }

        case XAPIC_OFF_LDR:
        {
            rcStrict = apicSetLdr(pVCpu, uValue);
            break;
        }

        case XAPIC_OFF_DFR:
        {
            rcStrict = apicSetDfr(pVCpu, uValue);
            break;
        }

        case XAPIC_OFF_SVR:
        {
            rcStrict = apicSetSvr(pVCpu, uValue);
            break;
        }

        case XAPIC_OFF_ICR_LO:
        {
            rcStrict = apicSetIcrLo(pVCpu, uValue, VINF_IOM_R3_MMIO_WRITE, true /* fUpdateStat */);
            break;
        }

        case XAPIC_OFF_ICR_HI:
        {
            rcStrict = apicSetIcrHi(pVCpu, uValue);
            break;
        }

        case XAPIC_OFF_TIMER_DCR:
        {
            rcStrict = apicSetTimerDcr(pVCpu, uValue);
            break;
        }

        case XAPIC_OFF_ESR:
        {
            rcStrict = apicSetEsr(pVCpu, uValue);
            break;
        }

        case XAPIC_OFF_APR:
        case XAPIC_OFF_RRD:
        {
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
            /* Unsupported on Pentium 4 and Xeon CPUs but writes do -not- set an illegal register access error. */
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
            break;
        }

        /* Read-only, write ignored: */
        case XAPIC_OFF_VERSION:
        case XAPIC_OFF_ID:
            break;

        /* Unavailable/reserved in xAPIC mode: */
        case X2APIC_OFF_SELF_IPI:
        /* Read-only registers: */
        case XAPIC_OFF_PPR:
        case XAPIC_OFF_ISR0:    case XAPIC_OFF_ISR1:    case XAPIC_OFF_ISR2:    case XAPIC_OFF_ISR3:
        case XAPIC_OFF_ISR4:    case XAPIC_OFF_ISR5:    case XAPIC_OFF_ISR6:    case XAPIC_OFF_ISR7:
        case XAPIC_OFF_TMR0:    case XAPIC_OFF_TMR1:    case XAPIC_OFF_TMR2:    case XAPIC_OFF_TMR3:
        case XAPIC_OFF_TMR4:    case XAPIC_OFF_TMR5:    case XAPIC_OFF_TMR6:    case XAPIC_OFF_TMR7:
        case XAPIC_OFF_IRR0:    case XAPIC_OFF_IRR1:    case XAPIC_OFF_IRR2:    case XAPIC_OFF_IRR3:
        case XAPIC_OFF_IRR4:    case XAPIC_OFF_IRR5:    case XAPIC_OFF_IRR6:    case XAPIC_OFF_IRR7:
        case XAPIC_OFF_TIMER_CCR:
        default:
        {
            rcStrict = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "APIC%u: offReg=%#RX16\n", pVCpu->idCpu, offReg);
            apicSetError(pVCpu, XAPIC_ESR_ILLEGAL_REG_ADDRESS);
            break;
        }
    }

    return rcStrict;
}


/**
 * Reads an APIC MSR.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u32Reg          The MSR being read.
 * @param   pu64Value       Where to store the read value.
 */
VMM_INT_DECL(VBOXSTRICTRC) APICReadMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(u32Reg >= MSR_IA32_X2APIC_ID && u32Reg <= MSR_IA32_X2APIC_SELF_IPI);
    Assert(pu64Value);

    /*
     * Is the APIC enabled?
     */
    PCAPIC pApic = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    if (APICIsEnabled(pVCpu))
    { /* likely */ }
    else
        return apicMsrAccessError(pVCpu, u32Reg, pApic->enmMaxMode == PDMAPICMODE_NONE ?
                                                 APICMSRACCESS_READ_DISALLOWED_CONFIG : APICMSRACCESS_READ_RSVD_OR_UNKNOWN);

#ifndef IN_RING3
    if (pApic->CTXALLMID(f,Enabled))
    { /* likely */}
    else
        return VINF_CPUM_R3_MSR_READ;
#endif

    STAM_COUNTER_INC(&pVCpu->apic.s.CTX_SUFF_Z(StatMsrRead));

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    if (RT_LIKELY(   XAPIC_IN_X2APIC_MODE(pVCpu)
                  || pApic->fHyperVCompatMode))
    {
        switch (u32Reg)
        {
            /* Special handling for x2APIC: */
            case MSR_IA32_X2APIC_ICR:
            {
                *pu64Value = apicGetIcrNoCheck(pVCpu);
                break;
            }

            /* Special handling, compatible with xAPIC: */
            case MSR_IA32_X2APIC_TIMER_CCR:
            {
                uint32_t uValue;
                rcStrict = apicGetTimerCcr(VMCPU_TO_DEVINS(pVCpu), pVCpu, VINF_CPUM_R3_MSR_READ, &uValue);
                *pu64Value = uValue;
                break;
            }

            /* Special handling, compatible with xAPIC: */
            case MSR_IA32_X2APIC_PPR:
            {
                *pu64Value = apicGetPpr(pVCpu);
                break;
            }

            /* Raw read, compatible with xAPIC: */
            case MSR_IA32_X2APIC_ID:
            {
                STAM_COUNTER_INC(&pVCpu->apic.s.StatIdMsrRead);
                /* Horrible macOS hack (sample rdmsr addres: 0008:ffffff801686f21a). */
                if (   !pApic->fMacOSWorkaround
                    || pVCpu->cpum.GstCtx.cs.Sel != 8
                    || pVCpu->cpum.GstCtx.rip < UINT64_C(0xffffff8000000000))
                { /* likely */ }
                else
                {
                    PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpu);
                    uint32_t const idApic = pX2ApicPage->id.u32ApicId;
                    *pu64Value = (idApic << 24) | idApic;
                    Log(("APIC: Applying macOS hack to MSR_IA32_X2APIC_ID: %#RX64\n", *pu64Value));
                    break;
                }
                RT_FALL_THRU();
            }
            case MSR_IA32_X2APIC_VERSION:
            case MSR_IA32_X2APIC_TPR:
            case MSR_IA32_X2APIC_LDR:
            case MSR_IA32_X2APIC_SVR:
            case MSR_IA32_X2APIC_ISR0:  case MSR_IA32_X2APIC_ISR1:  case MSR_IA32_X2APIC_ISR2:  case MSR_IA32_X2APIC_ISR3:
            case MSR_IA32_X2APIC_ISR4:  case MSR_IA32_X2APIC_ISR5:  case MSR_IA32_X2APIC_ISR6:  case MSR_IA32_X2APIC_ISR7:
            case MSR_IA32_X2APIC_TMR0:  case MSR_IA32_X2APIC_TMR1:  case MSR_IA32_X2APIC_TMR2:  case MSR_IA32_X2APIC_TMR3:
            case MSR_IA32_X2APIC_TMR4:  case MSR_IA32_X2APIC_TMR5:  case MSR_IA32_X2APIC_TMR6:  case MSR_IA32_X2APIC_TMR7:
            case MSR_IA32_X2APIC_IRR0:  case MSR_IA32_X2APIC_IRR1:  case MSR_IA32_X2APIC_IRR2:  case MSR_IA32_X2APIC_IRR3:
            case MSR_IA32_X2APIC_IRR4:  case MSR_IA32_X2APIC_IRR5:  case MSR_IA32_X2APIC_IRR6:  case MSR_IA32_X2APIC_IRR7:
            case MSR_IA32_X2APIC_ESR:
            case MSR_IA32_X2APIC_LVT_TIMER:
            case MSR_IA32_X2APIC_LVT_THERMAL:
            case MSR_IA32_X2APIC_LVT_PERF:
            case MSR_IA32_X2APIC_LVT_LINT0:
            case MSR_IA32_X2APIC_LVT_LINT1:
            case MSR_IA32_X2APIC_LVT_ERROR:
            case MSR_IA32_X2APIC_TIMER_ICR:
            case MSR_IA32_X2APIC_TIMER_DCR:
            {
                PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
                uint16_t const offReg = X2APIC_GET_XAPIC_OFF(u32Reg);
                *pu64Value = apicReadRaw32(pXApicPage, offReg);
                break;
            }

            /* Write-only MSRs: */
            case MSR_IA32_X2APIC_SELF_IPI:
            case MSR_IA32_X2APIC_EOI:
            {
                rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_READ_WRITE_ONLY);
                break;
            }

            /*
             * Windows guest using Hyper-V x2APIC MSR compatibility mode tries to read the "high"
             * LDR bits, which is quite absurd (as it's a 32-bit register) using this invalid MSR
             * index (0x80E), see @bugref{8382#c175}.
             */
            case MSR_IA32_X2APIC_LDR + 1:
            {
                if (pApic->fHyperVCompatMode)
                    *pu64Value = 0;
                else
                    rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_READ_RSVD_OR_UNKNOWN);
                break;
            }

            /* Reserved MSRs: */
            case MSR_IA32_X2APIC_LVT_CMCI:
            default:
            {
                rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_READ_RSVD_OR_UNKNOWN);
                break;
            }
        }
    }
    else
        rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_INVALID_READ_MODE);

    return rcStrict;
}


/**
 * Writes an APIC MSR.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u32Reg          The MSR being written.
 * @param   u64Value        The value to write.
 */
VMM_INT_DECL(VBOXSTRICTRC) APICWriteMsr(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(u32Reg >= MSR_IA32_X2APIC_ID && u32Reg <= MSR_IA32_X2APIC_SELF_IPI);

    /*
     * Is the APIC enabled?
     */
    PCAPIC pApic = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    if (APICIsEnabled(pVCpu))
    { /* likely */ }
    else
        return apicMsrAccessError(pVCpu, u32Reg, pApic->enmMaxMode == PDMAPICMODE_NONE ?
                                  APICMSRACCESS_WRITE_DISALLOWED_CONFIG : APICMSRACCESS_WRITE_RSVD_OR_UNKNOWN);

#ifndef IN_RING3
    if (pApic->CTXALLMID(f,Enabled))
    { /* likely */ }
    else
        return VINF_CPUM_R3_MSR_WRITE;
#endif

    STAM_COUNTER_INC(&pVCpu->apic.s.CTX_SUFF_Z(StatMsrWrite));

    /*
     * In x2APIC mode, we need to raise #GP(0) for writes to reserved bits, unlike MMIO
     * accesses where they are ignored. Hence, we need to validate each register before
     * invoking the generic/xAPIC write functions.
     *
     * Bits 63:32 of all registers except the ICR are reserved, we'll handle this common
     * case first and handle validating the remaining bits on a per-register basis.
     * See Intel spec. 10.12.1.2 "x2APIC Register Address Space".
     */
    if (   u32Reg != MSR_IA32_X2APIC_ICR
        && RT_HI_U32(u64Value))
        return apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_WRITE_RSVD_BITS);

    uint32_t     u32Value = RT_LO_U32(u64Value);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    if (RT_LIKELY(   XAPIC_IN_X2APIC_MODE(pVCpu)
                  || pApic->fHyperVCompatMode))
    {
        switch (u32Reg)
        {
            case MSR_IA32_X2APIC_TPR:
            {
                rcStrict = apicSetTprEx(pVCpu, u32Value, false /* fForceX2ApicBehaviour */);
                break;
            }

            case MSR_IA32_X2APIC_ICR:
            {
                rcStrict = apicSetIcr(pVCpu, u64Value, VINF_CPUM_R3_MSR_WRITE);
                break;
            }

            case MSR_IA32_X2APIC_SVR:
            {
                rcStrict = apicSetSvr(pVCpu, u32Value);
                break;
            }

            case MSR_IA32_X2APIC_ESR:
            {
                rcStrict = apicSetEsr(pVCpu, u32Value);
                break;
            }

            case MSR_IA32_X2APIC_TIMER_DCR:
            {
                rcStrict = apicSetTimerDcr(pVCpu, u32Value);
                break;
            }

            case MSR_IA32_X2APIC_LVT_TIMER:
            case MSR_IA32_X2APIC_LVT_THERMAL:
            case MSR_IA32_X2APIC_LVT_PERF:
            case MSR_IA32_X2APIC_LVT_LINT0:
            case MSR_IA32_X2APIC_LVT_LINT1:
            case MSR_IA32_X2APIC_LVT_ERROR:
            {
                rcStrict = apicSetLvtEntry(pVCpu, X2APIC_GET_XAPIC_OFF(u32Reg), u32Value);
                break;
            }

            case MSR_IA32_X2APIC_TIMER_ICR:
            {
                rcStrict = apicSetTimerIcr(VMCPU_TO_DEVINS(pVCpu), pVCpu, VINF_CPUM_R3_MSR_WRITE, u32Value);
                break;
            }

            /* Write-only MSRs: */
            case MSR_IA32_X2APIC_SELF_IPI:
            {
                uint8_t const uVector = XAPIC_SELF_IPI_GET_VECTOR(u32Value);
                apicPostInterrupt(pVCpu, uVector, XAPICTRIGGERMODE_EDGE, 0 /* uSrcTag */);
                rcStrict = VINF_SUCCESS;
                break;
            }

            case MSR_IA32_X2APIC_EOI:
            {
                rcStrict = apicSetEoi(pVCpu, u32Value, false /* fForceX2ApicBehaviour */);
                break;
            }

            /*
             * Windows guest using Hyper-V x2APIC MSR compatibility mode tries to write the "high"
             * LDR bits, which is quite absurd (as it's a 32-bit register) using this invalid MSR
             * index (0x80E). The write value was 0xffffffff on a Windows 8.1 64-bit guest. We can
             * safely ignore this nonsense, See @bugref{8382#c7}.
             */
            case MSR_IA32_X2APIC_LDR + 1:
            {
                if (pApic->fHyperVCompatMode)
                    rcStrict = VINF_SUCCESS;
                else
                    rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_WRITE_RSVD_OR_UNKNOWN);
                break;
            }

            /* Special-treament (read-only normally, but not with Hyper-V) */
            case MSR_IA32_X2APIC_LDR:
            {
                if (pApic->fHyperVCompatMode)
                {
                    rcStrict = apicSetLdr(pVCpu, u32Value);
                    break;
                }
            }
            RT_FALL_THRU();
            /* Read-only MSRs: */
            case MSR_IA32_X2APIC_ID:
            case MSR_IA32_X2APIC_VERSION:
            case MSR_IA32_X2APIC_PPR:
            case MSR_IA32_X2APIC_ISR0:  case MSR_IA32_X2APIC_ISR1:  case MSR_IA32_X2APIC_ISR2:  case MSR_IA32_X2APIC_ISR3:
            case MSR_IA32_X2APIC_ISR4:  case MSR_IA32_X2APIC_ISR5:  case MSR_IA32_X2APIC_ISR6:  case MSR_IA32_X2APIC_ISR7:
            case MSR_IA32_X2APIC_TMR0:  case MSR_IA32_X2APIC_TMR1:  case MSR_IA32_X2APIC_TMR2:  case MSR_IA32_X2APIC_TMR3:
            case MSR_IA32_X2APIC_TMR4:  case MSR_IA32_X2APIC_TMR5:  case MSR_IA32_X2APIC_TMR6:  case MSR_IA32_X2APIC_TMR7:
            case MSR_IA32_X2APIC_IRR0:  case MSR_IA32_X2APIC_IRR1:  case MSR_IA32_X2APIC_IRR2:  case MSR_IA32_X2APIC_IRR3:
            case MSR_IA32_X2APIC_IRR4:  case MSR_IA32_X2APIC_IRR5:  case MSR_IA32_X2APIC_IRR6:  case MSR_IA32_X2APIC_IRR7:
            case MSR_IA32_X2APIC_TIMER_CCR:
            {
                rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_WRITE_READ_ONLY);
                break;
            }

            /* Reserved MSRs: */
            case MSR_IA32_X2APIC_LVT_CMCI:
            default:
            {
                rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_WRITE_RSVD_OR_UNKNOWN);
                break;
            }
        }
    }
    else
        rcStrict = apicMsrAccessError(pVCpu, u32Reg, APICMSRACCESS_INVALID_WRITE_MODE);

    return rcStrict;
}


/**
 * Resets the APIC base MSR.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void apicResetBaseMsr(PVMCPUCC pVCpu)
{
    /*
     * Initialize the APIC base MSR. The APIC enable-bit is set upon power-up or reset[1].
     *
     * A Reset (in xAPIC and x2APIC mode) brings up the local APIC in xAPIC mode.
     * An INIT IPI does -not- cause a transition between xAPIC and x2APIC mode[2].
     *
     * [1] See AMD spec. 14.1.3 "Processor Initialization State"
     * [2] See Intel spec. 10.12.5.1 "x2APIC States".
     */
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    /* Construct. */
    PAPICCPU pApicCpu     = VMCPU_TO_APICCPU(pVCpu);
    PAPIC    pApic        = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    uint64_t uApicBaseMsr = MSR_IA32_APICBASE_ADDR;
    if (pVCpu->idCpu == 0)
        uApicBaseMsr |= MSR_IA32_APICBASE_BSP;

    /* If the VM was configured with no APIC, don't enable xAPIC mode, obviously. */
    if (pApic->enmMaxMode != PDMAPICMODE_NONE)
    {
        uApicBaseMsr |= MSR_IA32_APICBASE_EN;

        /*
         * While coming out of a reset the APIC is enabled and in xAPIC mode. If software had previously
         * disabled the APIC (which results in the CPUID bit being cleared as well) we re-enable it here.
         * See Intel spec. 10.12.5.1 "x2APIC States".
         */
        if (CPUMSetGuestCpuIdPerCpuApicFeature(pVCpu, true /*fVisible*/) == false)
            LogRel(("APIC%u: Resetting mode to xAPIC\n", pVCpu->idCpu));
    }

    /* Commit. */
    ASMAtomicWriteU64(&pApicCpu->uApicBaseMsr, uApicBaseMsr);
}


/**
 * Initializes per-VCPU APIC to the state following an INIT reset
 * ("Wait-for-SIPI" state).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
void apicInitIpi(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    /*
     * See Intel spec. 10.4.7.3 "Local APIC State After an INIT Reset (Wait-for-SIPI State)"
     * and AMD spec 16.3.2 "APIC Registers".
     *
     * The reason we don't simply zero out the entire APIC page and only set the non-zero members
     * is because there are some registers that are not touched by the INIT IPI (e.g. version)
     * operation and this function is only a subset of the reset operation.
     */
    RT_ZERO(pXApicPage->irr);
    RT_ZERO(pXApicPage->irr);
    RT_ZERO(pXApicPage->isr);
    RT_ZERO(pXApicPage->tmr);
    RT_ZERO(pXApicPage->icr_hi);
    RT_ZERO(pXApicPage->icr_lo);
    RT_ZERO(pXApicPage->ldr);
    RT_ZERO(pXApicPage->tpr);
    RT_ZERO(pXApicPage->ppr);
    RT_ZERO(pXApicPage->timer_icr);
    RT_ZERO(pXApicPage->timer_ccr);
    RT_ZERO(pXApicPage->timer_dcr);

    pXApicPage->dfr.u.u4Model        = XAPICDESTFORMAT_FLAT;
    pXApicPage->dfr.u.u28ReservedMb1 = UINT32_C(0xfffffff);

    /** @todo CMCI. */

    RT_ZERO(pXApicPage->lvt_timer);
    pXApicPage->lvt_timer.u.u1Mask = 1;

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    RT_ZERO(pXApicPage->lvt_thermal);
    pXApicPage->lvt_thermal.u.u1Mask = 1;
#endif

    RT_ZERO(pXApicPage->lvt_perf);
    pXApicPage->lvt_perf.u.u1Mask = 1;

    RT_ZERO(pXApicPage->lvt_lint0);
    pXApicPage->lvt_lint0.u.u1Mask = 1;

    RT_ZERO(pXApicPage->lvt_lint1);
    pXApicPage->lvt_lint1.u.u1Mask = 1;

    RT_ZERO(pXApicPage->lvt_error);
    pXApicPage->lvt_error.u.u1Mask = 1;

    RT_ZERO(pXApicPage->svr);
    pXApicPage->svr.u.u8SpuriousVector = 0xff;

    /* The self-IPI register is reset to 0. See Intel spec. 10.12.5.1 "x2APIC States" */
    PX2APICPAGE pX2ApicPage = VMCPU_TO_X2APICPAGE(pVCpu);
    RT_ZERO(pX2ApicPage->self_ipi);

    /* Clear the pending-interrupt bitmaps. */
    PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    RT_BZERO(&pApicCpu->ApicPibLevel, sizeof(APICPIB));
    RT_BZERO(pApicCpu->CTX_SUFF(pvApicPib), sizeof(APICPIB));

    /* Clear the interrupt line states for LINT0 and LINT1 pins. */
    pApicCpu->fActiveLint0 = false;
    pApicCpu->fActiveLint1 = false;
}


/**
 * Initializes per-VCPU APIC to the state following a power-up or hardware
 * reset.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   fResetApicBaseMsr   Whether to reset the APIC base MSR.
 */
void apicResetCpu(PVMCPUCC pVCpu, bool fResetApicBaseMsr)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    LogFlow(("APIC%u: apicR3ResetCpu: fResetApicBaseMsr=%RTbool\n", pVCpu->idCpu, fResetApicBaseMsr));

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
    apicInitIpi(pVCpu);

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
        apicResetBaseMsr(pVCpu);

    /*
     * Initialize the APIC ID register to xAPIC format.
     */
    RT_BZERO(&pXApicPage->id, sizeof(pXApicPage->id));
    pXApicPage->id.u8ApicId = pVCpu->idCpu;
}


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
VMM_INT_DECL(int) APICSetBaseMsr(PVMCPUCC pVCpu, uint64_t u64BaseMsr)
{
    Assert(pVCpu);

    PAPICCPU pApicCpu   = VMCPU_TO_APICCPU(pVCpu);
    PAPIC    pApic      = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    APICMODE enmOldMode = apicGetMode(pApicCpu->uApicBaseMsr);
    APICMODE enmNewMode = apicGetMode(u64BaseMsr);
    uint64_t uBaseMsr   = pApicCpu->uApicBaseMsr;

    Log2(("APIC%u: ApicSetBaseMsr: u64BaseMsr=%#RX64 enmNewMode=%s enmOldMode=%s\n", pVCpu->idCpu, u64BaseMsr,
          apicGetModeName(enmNewMode), apicGetModeName(enmOldMode)));

    /*
     * We do not support re-mapping the APIC base address because:
     *    - We'll have to manage all the mappings ourselves in the APIC (reference counting based unmapping etc.)
     *      i.e. we can only unmap the MMIO region if no other APIC is mapped on that location.
     *    - It's unclear how/if IOM can fallback to handling regions as regular memory (if the MMIO
     *      region remains mapped but doesn't belong to the called VCPU's APIC).
     */
    /** @todo Handle per-VCPU APIC base relocation. */
    if (MSR_IA32_APICBASE_GET_ADDR(uBaseMsr) != MSR_IA32_APICBASE_ADDR)
    {
        if (pVCpu->apic.s.cLogMaxSetApicBaseAddr++ < 5)
            LogRel(("APIC%u: Attempt to relocate base to %#RGp, unsupported -> #GP(0)\n", pVCpu->idCpu,
                    MSR_IA32_APICBASE_GET_ADDR(uBaseMsr)));
        return VERR_CPUM_RAISE_GP_0;
    }

    /* Don't allow enabling xAPIC/x2APIC if the VM is configured with the APIC disabled. */
    if (pApic->enmMaxMode == PDMAPICMODE_NONE)
    {
        LogRel(("APIC%u: Disallowing APIC base MSR write as the VM is configured with APIC disabled!\n", pVCpu->idCpu));
        return apicMsrAccessError(pVCpu, MSR_IA32_APICBASE, APICMSRACCESS_WRITE_DISALLOWED_CONFIG);
    }

    /*
     * Act on state transition.
     */
    if (enmNewMode != enmOldMode)
    {
        switch (enmNewMode)
        {
            case APICMODE_DISABLED:
            {
                /*
                 * The APIC state needs to be reset (especially the APIC ID as x2APIC APIC ID bit layout
                 * is different). We can start with a clean slate identical to the state after a power-up/reset.
                 *
                 * See Intel spec. 10.4.3 "Enabling or Disabling the Local APIC".
                 *
                 * We'll also manually manage the APIC base MSR here. We want a single-point of commit
                 * at the end of this function rather than updating it in apicR3ResetCpu. This means we also
                 * need to update the CPUID leaf ourselves.
                 */
                apicResetCpu(pVCpu, false /* fResetApicBaseMsr */);
                uBaseMsr &= ~(MSR_IA32_APICBASE_EN | MSR_IA32_APICBASE_EXTD);
                CPUMSetGuestCpuIdPerCpuApicFeature(pVCpu, false /*fVisible*/);
                LogRel(("APIC%u: Switched mode to disabled\n", pVCpu->idCpu));
                break;
            }

            case APICMODE_XAPIC:
            {
                if (enmOldMode != APICMODE_DISABLED)
                {
                    LogRel(("APIC%u: Can only transition to xAPIC state from disabled state\n", pVCpu->idCpu));
                    return apicMsrAccessError(pVCpu, MSR_IA32_APICBASE, APICMSRACCESS_WRITE_INVALID);
                }

                uBaseMsr |= MSR_IA32_APICBASE_EN;
                CPUMSetGuestCpuIdPerCpuApicFeature(pVCpu, true /*fVisible*/);
                LogRel(("APIC%u: Switched mode to xAPIC\n", pVCpu->idCpu));
                break;
            }

            case APICMODE_X2APIC:
            {
                if (pApic->enmMaxMode != PDMAPICMODE_X2APIC)
                {
                    LogRel(("APIC%u: Disallowing transition to x2APIC mode as the VM is configured with the x2APIC disabled!\n",
                            pVCpu->idCpu));
                    return apicMsrAccessError(pVCpu, MSR_IA32_APICBASE, APICMSRACCESS_WRITE_INVALID);
                }

                if (enmOldMode != APICMODE_XAPIC)
                {
                    LogRel(("APIC%u: Can only transition to x2APIC state from xAPIC state\n", pVCpu->idCpu));
                    return apicMsrAccessError(pVCpu, MSR_IA32_APICBASE, APICMSRACCESS_WRITE_INVALID);
                }

                uBaseMsr |= MSR_IA32_APICBASE_EN | MSR_IA32_APICBASE_EXTD;

                /*
                 * The APIC ID needs updating when entering x2APIC mode.
                 * Software written APIC ID in xAPIC mode isn't preserved.
                 * The APIC ID becomes read-only to software in x2APIC mode.
                 *
                 * See Intel spec. 10.12.5.1 "x2APIC States".
                 */
                PX2APICPAGE pX2ApicPage = VMCPU_TO_X2APICPAGE(pVCpu);
                RT_BZERO(&pX2ApicPage->id, sizeof(pX2ApicPage->id));
                pX2ApicPage->id.u32ApicId = pVCpu->idCpu;

                /*
                 * LDR initialization occurs when entering x2APIC mode.
                 * See Intel spec. 10.12.10.2 "Deriving Logical x2APIC ID from the Local x2APIC ID".
                 */
                pX2ApicPage->ldr.u32LogicalApicId = ((pX2ApicPage->id.u32ApicId & UINT32_C(0xffff0)) << 16)
                                                  | (UINT32_C(1) << pX2ApicPage->id.u32ApicId & UINT32_C(0xf));

                LogRel(("APIC%u: Switched mode to x2APIC\n", pVCpu->idCpu));
                break;
            }

            case APICMODE_INVALID:
            default:
            {
                Log(("APIC%u: Invalid state transition attempted\n", pVCpu->idCpu));
                return apicMsrAccessError(pVCpu, MSR_IA32_APICBASE, APICMSRACCESS_WRITE_INVALID);
            }
        }
    }

    ASMAtomicWriteU64(&pApicCpu->uApicBaseMsr, uBaseMsr);
    return VINF_SUCCESS;
}


/**
 * Gets the APIC base MSR (no checks are performed wrt APIC hardware or its
 * state).
 *
 * @returns The base MSR value.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(uint64_t) APICGetBaseMsrNoCheck(PCVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PCAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    return pApicCpu->uApicBaseMsr;
}


/**
 * Gets the APIC base MSR.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pu64Value   Where to store the MSR value.
 */
VMM_INT_DECL(VBOXSTRICTRC) APICGetBaseMsr(PVMCPUCC pVCpu, uint64_t *pu64Value)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    PCAPIC pApic = VM_TO_APIC(pVCpu->CTX_SUFF(pVM));
    if (pApic->enmMaxMode != PDMAPICMODE_NONE)
    {
        *pu64Value = APICGetBaseMsrNoCheck(pVCpu);
        return VINF_SUCCESS;
    }

    if (pVCpu->apic.s.cLogMaxGetApicBaseAddr++ < 5)
        LogRel(("APIC%u: Reading APIC base MSR (%#x) when there is no APIC -> #GP(0)\n", pVCpu->idCpu, MSR_IA32_APICBASE));
    return VERR_CPUM_RAISE_GP_0;
}


/**
 * Sets the TPR (Task Priority Register).
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_CPUM_RAISE_GP_0
 * @retval  VERR_PDM_NO_APIC_INSTANCE
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   u8Tpr       The TPR value to set.
 */
VMMDECL(int) APICSetTpr(PVMCPUCC pVCpu, uint8_t u8Tpr)
{
    if (APICIsEnabled(pVCpu))
        return apicSetTprEx(pVCpu, u8Tpr, false /* fForceX2ApicBehaviour */);
    return VERR_PDM_NO_APIC_INSTANCE;
}


/**
 * Gets the highest priority pending interrupt.
 *
 * @returns true if any interrupt is pending, false otherwise.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   pu8PendingIntr      Where to store the interrupt vector if the
 *                              interrupt is pending (optional, can be NULL).
 */
static bool apicGetHighestPendingInterrupt(PCVMCPUCC pVCpu, uint8_t *pu8PendingIntr)
{
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    int const irrv = apicGetHighestSetBitInReg(&pXApicPage->irr, -1);
    if (irrv >= 0)
    {
        Assert(irrv <= (int)UINT8_MAX);
        if (pu8PendingIntr)
            *pu8PendingIntr = (uint8_t)irrv;
        return true;
    }
    return false;
}


/**
 * Gets the APIC TPR (Task Priority Register).
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pu8Tpr          Where to store the TPR.
 * @param   pfPending       Where to store whether there is a pending interrupt
 *                          (optional, can be NULL).
 * @param   pu8PendingIntr  Where to store the highest-priority pending
 *                          interrupt (optional, can be NULL).
 */
VMMDECL(int) APICGetTpr(PCVMCPUCC pVCpu, uint8_t *pu8Tpr, bool *pfPending, uint8_t *pu8PendingIntr)
{
    VMCPU_ASSERT_EMT(pVCpu);
    if (APICIsEnabled(pVCpu))
    {
        PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
        if (pfPending)
        {
            /*
             * Just return whatever the highest pending interrupt is in the IRR.
             * The caller is responsible for figuring out if it's masked by the TPR etc.
             */
            *pfPending = apicGetHighestPendingInterrupt(pVCpu, pu8PendingIntr);
        }

        *pu8Tpr = pXApicPage->tpr.u8Tpr;
        return VINF_SUCCESS;
    }

    *pu8Tpr = 0;
    return VERR_PDM_NO_APIC_INSTANCE;
}


/**
 * Gets the APIC timer frequency.
 *
 * @returns Strict VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pu64Value       Where to store the timer frequency.
 */
VMM_INT_DECL(int) APICGetTimerFreq(PVMCC pVM, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    Assert(pVM);
    AssertPtrReturn(pu64Value, VERR_INVALID_PARAMETER);

    PVMCPUCC pVCpu = pVM->CTX_SUFF(apCpus)[0];
    if (APICIsEnabled(pVCpu))
    {
        PCAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
        *pu64Value = PDMDevHlpTimerGetFreq(VMCPU_TO_DEVINS(pVCpu), pApicCpu->hTimer);
        return VINF_SUCCESS;
    }
    return VERR_PDM_NO_APIC_INSTANCE;
}


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
 * @param   uSrcTag         The interrupt source tag (debugging).
 */
VMM_INT_DECL(int) APICBusDeliver(PVMCC pVM, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode, uint8_t uVector,
                                 uint8_t uPolarity, uint8_t uTriggerMode, uint32_t uSrcTag)
{
    NOREF(uPolarity);

    /*
     * If the APIC isn't enabled, do nothing and pretend success.
     */
    if (APICIsEnabled(pVM->CTX_SUFF(apCpus)[0]))
    { /* likely */ }
    else
        return VINF_SUCCESS;

    /*
     * The destination field (mask) in the IO APIC redirectable table entry is 8-bits.
     * Hence, the broadcast mask is 0xff.
     * See IO APIC spec. 3.2.4. "IOREDTBL[23:0] - I/O Redirectable Table Registers".
     */
    XAPICTRIGGERMODE  enmTriggerMode  = (XAPICTRIGGERMODE)uTriggerMode;
    XAPICDELIVERYMODE enmDeliveryMode = (XAPICDELIVERYMODE)uDeliveryMode;
    XAPICDESTMODE     enmDestMode     = (XAPICDESTMODE)uDestMode;
    uint32_t          fDestMask       = uDest;
    uint32_t          fBroadcastMask  = UINT32_C(0xff);

    Log2(("APIC: apicBusDeliver: fDestMask=%#x enmDestMode=%s enmTriggerMode=%s enmDeliveryMode=%s uVector=%#x uSrcTag=%#x\n",
          fDestMask, apicGetDestModeName(enmDestMode), apicGetTriggerModeName(enmTriggerMode),
          apicGetDeliveryModeName(enmDeliveryMode), uVector, uSrcTag));

    bool     fIntrAccepted;
    VMCPUSET DestCpuSet;
    apicGetDestCpuSet(pVM, fDestMask, fBroadcastMask, enmDestMode, enmDeliveryMode, &DestCpuSet);
    VBOXSTRICTRC rcStrict = apicSendIntr(pVM, NULL /* pVCpu */, uVector, enmTriggerMode, enmDeliveryMode, &DestCpuSet,
                                         &fIntrAccepted, uSrcTag, VINF_SUCCESS /* rcRZ */);
    if (fIntrAccepted)
        return VBOXSTRICTRC_VAL(rcStrict);
    return VERR_APIC_INTR_DISCARDED;
}


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
VMM_INT_DECL(VBOXSTRICTRC) APICLocalInterrupt(PVMCPUCC pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ)
{
    AssertReturn(u8Pin <= 1, VERR_INVALID_PARAMETER);
    AssertReturn(u8Level <= 1, VERR_INVALID_PARAMETER);

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;

    /* If the APIC is enabled, the interrupt is subject to LVT programming. */
    if (APICIsEnabled(pVCpu))
    {
        PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);

        /* Pick the LVT entry corresponding to the interrupt pin. */
        static const uint16_t s_au16LvtOffsets[] =
        {
            XAPIC_OFF_LVT_LINT0,
            XAPIC_OFF_LVT_LINT1
        };
        Assert(u8Pin < RT_ELEMENTS(s_au16LvtOffsets));
        uint16_t const offLvt = s_au16LvtOffsets[u8Pin];
        uint32_t const uLvt   = apicReadRaw32(pXApicPage, offLvt);

        /* If software hasn't masked the interrupt in the LVT entry, proceed interrupt processing. */
        if (!XAPIC_LVT_IS_MASKED(uLvt))
        {
            XAPICDELIVERYMODE const enmDeliveryMode = XAPIC_LVT_GET_DELIVERY_MODE(uLvt);
            XAPICTRIGGERMODE        enmTriggerMode  = XAPIC_LVT_GET_TRIGGER_MODE(uLvt);

            switch (enmDeliveryMode)
            {
                case XAPICDELIVERYMODE_INIT:
                {
                    /** @todo won't work in R0/RC because callers don't care about rcRZ. */
                    AssertMsgFailed(("INIT through LINT0/LINT1 is not yet supported\n"));
                }
                RT_FALL_THRU();
                case XAPICDELIVERYMODE_FIXED:
                {
                    PAPICCPU       pApicCpu = VMCPU_TO_APICCPU(pVCpu);
                    uint8_t const  uVector  = XAPIC_LVT_GET_VECTOR(uLvt);
                    bool           fActive  = RT_BOOL(u8Level & 1);
                    bool volatile *pfActiveLine = u8Pin == 0 ? &pApicCpu->fActiveLint0 : &pApicCpu->fActiveLint1;
                    /** @todo Polarity is busted elsewhere, we need to fix that
                     *        first. See @bugref{8386#c7}. */
#if 0
                    uint8_t const u8Polarity = XAPIC_LVT_GET_POLARITY(uLvt);
                    fActive ^= u8Polarity; */
#endif
                    if (!fActive)
                    {
                        ASMAtomicCmpXchgBool(pfActiveLine, false, true);
                        break;
                    }

                    /* Level-sensitive interrupts are not supported for LINT1. See Intel spec. 10.5.1 "Local Vector Table". */
                    if (offLvt == XAPIC_OFF_LVT_LINT1)
                        enmTriggerMode = XAPICTRIGGERMODE_EDGE;
                    /** @todo figure out what "If the local APIC is not used in conjunction with an I/O APIC and fixed
                              delivery mode is selected; the Pentium 4, Intel Xeon, and P6 family processors will always
                              use level-sensitive triggering, regardless if edge-sensitive triggering is selected."
                              means. */

                    bool fSendIntr;
                    if (enmTriggerMode == XAPICTRIGGERMODE_EDGE)
                    {
                        /* Recognize and send the interrupt only on an edge transition. */
                        fSendIntr = ASMAtomicCmpXchgBool(pfActiveLine, true, false);
                    }
                    else
                    {
                        /* For level-triggered interrupts, redundant interrupts are not a problem. */
                        Assert(enmTriggerMode == XAPICTRIGGERMODE_LEVEL);
                        ASMAtomicCmpXchgBool(pfActiveLine, true, false);

                        /* Only when the remote IRR isn't set, set it and send the interrupt. */
                        if (!(pXApicPage->lvt_lint0.all.u32LvtLint0 & XAPIC_LVT_REMOTE_IRR))
                        {
                            Assert(offLvt == XAPIC_OFF_LVT_LINT0);
                            ASMAtomicOrU32((volatile uint32_t *)&pXApicPage->lvt_lint0.all.u32LvtLint0, XAPIC_LVT_REMOTE_IRR);
                            fSendIntr = true;
                        }
                        else
                            fSendIntr = false;
                    }

                    if (fSendIntr)
                    {
                        VMCPUSET DestCpuSet;
                        VMCPUSET_EMPTY(&DestCpuSet);
                        VMCPUSET_ADD(&DestCpuSet, pVCpu->idCpu);
                        rcStrict = apicSendIntr(pVCpu->CTX_SUFF(pVM), pVCpu, uVector, enmTriggerMode, enmDeliveryMode,
                                                &DestCpuSet, NULL /* pfIntrAccepted */, 0 /* uSrcTag */, rcRZ);
                    }
                    break;
                }

                case XAPICDELIVERYMODE_SMI:
                case XAPICDELIVERYMODE_NMI:
                {
                    VMCPUSET DestCpuSet;
                    VMCPUSET_EMPTY(&DestCpuSet);
                    VMCPUSET_ADD(&DestCpuSet, pVCpu->idCpu);
                    uint8_t const uVector = XAPIC_LVT_GET_VECTOR(uLvt);
                    rcStrict = apicSendIntr(pVCpu->CTX_SUFF(pVM), pVCpu, uVector, enmTriggerMode, enmDeliveryMode, &DestCpuSet,
                                            NULL /* pfIntrAccepted */, 0 /* uSrcTag */, rcRZ);
                    break;
                }

                case XAPICDELIVERYMODE_EXTINT:
                {
                    Log2(("APIC%u: apicLocalInterrupt: %s ExtINT through LINT%u\n", pVCpu->idCpu,
                          u8Level ? "Raising" : "Lowering", u8Pin));
                    if (u8Level)
                        apicSetInterruptFF(pVCpu, PDMAPICIRQ_EXTINT);
                    else
                        apicClearInterruptFF(pVCpu, PDMAPICIRQ_EXTINT);
                    break;
                }

                /* Reserved/unknown delivery modes: */
                case XAPICDELIVERYMODE_LOWEST_PRIO:
                case XAPICDELIVERYMODE_STARTUP:
                default:
                {
                    AssertMsgFailed(("APIC%u: LocalInterrupt: Invalid delivery mode %#x (%s) on LINT%d\n", pVCpu->idCpu,
                                     enmDeliveryMode, apicGetDeliveryModeName(enmDeliveryMode), u8Pin));
                    rcStrict = VERR_INTERNAL_ERROR_3;
                    break;
                }
            }
        }
    }
    else
    {
        /* The APIC is hardware disabled. The CPU behaves as though there is no on-chip APIC. */
        if (u8Pin == 0)
        {
            /* LINT0 behaves as an external interrupt pin. */
            Log2(("APIC%u: apicLocalInterrupt: APIC hardware-disabled, %s INTR\n", pVCpu->idCpu,
                  u8Level ? "raising" : "lowering"));
            if (u8Level)
                apicSetInterruptFF(pVCpu, PDMAPICIRQ_EXTINT);
            else
                apicClearInterruptFF(pVCpu, PDMAPICIRQ_EXTINT);
        }
        else
        {
            /* LINT1 behaves as NMI. */
            Log2(("APIC%u: apicLocalInterrupt: APIC hardware-disabled, raising NMI\n", pVCpu->idCpu));
            apicSetInterruptFF(pVCpu, PDMAPICIRQ_NMI);
        }
    }

    return rcStrict;
}


/**
 * Gets the next highest-priority interrupt from the APIC, marking it as an
 * "in-service" interrupt.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pu8Vector   Where to store the vector.
 * @param   puSrcTag    Where to store the interrupt source tag (debugging).
 */
VMM_INT_DECL(int) APICGetInterrupt(PVMCPUCC pVCpu, uint8_t *pu8Vector, uint32_t *puSrcTag)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pu8Vector);

    LogFlow(("APIC%u: apicGetInterrupt:\n", pVCpu->idCpu));

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    bool const fApicHwEnabled = APICIsEnabled(pVCpu);
    if (   fApicHwEnabled
        && pXApicPage->svr.u.fApicSoftwareEnable)
    {
        int const irrv = apicGetHighestSetBitInReg(&pXApicPage->irr, -1);
        if (RT_LIKELY(irrv >= 0))
        {
            Assert(irrv <= (int)UINT8_MAX);
            uint8_t const uVector = irrv;

            /*
             * This can happen if the APIC receives an interrupt when the CPU has interrupts
             * disabled but the TPR is raised by the guest before re-enabling interrupts.
             */
            uint8_t const uTpr = pXApicPage->tpr.u8Tpr;
            if (   uTpr > 0
                && XAPIC_TPR_GET_TP(uVector) <= XAPIC_TPR_GET_TP(uTpr))
            {
                Log2(("APIC%u: apicGetInterrupt: Interrupt masked. uVector=%#x uTpr=%#x SpuriousVector=%#x\n", pVCpu->idCpu,
                      uVector, uTpr, pXApicPage->svr.u.u8SpuriousVector));
                *pu8Vector = uVector;
                *puSrcTag  = 0;
                STAM_COUNTER_INC(&pVCpu->apic.s.StatMaskedByTpr);
                return VERR_APIC_INTR_MASKED_BY_TPR;
            }

            /*
             * The PPR should be up-to-date at this point through apicSetEoi().
             * We're on EMT so no parallel updates possible.
             * Subject the pending vector to PPR prioritization.
             */
            uint8_t const uPpr = pXApicPage->ppr.u8Ppr;
            if (   !uPpr
                || XAPIC_PPR_GET_PP(uVector) > XAPIC_PPR_GET_PP(uPpr))
            {
                apicClearVectorInReg(&pXApicPage->irr, uVector);
                apicSetVectorInReg(&pXApicPage->isr, uVector);
                apicUpdatePpr(pVCpu);
                apicSignalNextPendingIntr(pVCpu);

                /* Retrieve the interrupt source tag associated with this interrupt. */
                PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
                AssertCompile(RT_ELEMENTS(pApicCpu->auSrcTags) > UINT8_MAX);
                *puSrcTag = pApicCpu->auSrcTags[uVector];
                pApicCpu->auSrcTags[uVector] = 0;

                Log2(("APIC%u: apicGetInterrupt: Valid Interrupt. uVector=%#x uSrcTag=%#x\n", pVCpu->idCpu, uVector, *puSrcTag));
                *pu8Vector = uVector;
                return VINF_SUCCESS;
            }

            STAM_COUNTER_INC(&pVCpu->apic.s.StatMaskedByPpr);
            Log2(("APIC%u: apicGetInterrupt: Interrupt's priority is not higher than the PPR. uVector=%#x PPR=%#x\n",
                  pVCpu->idCpu, uVector, uPpr));
        }
        else
            Log2(("APIC%u: apicGetInterrupt: No pending bits in IRR\n", pVCpu->idCpu));
    }
    else
        Log2(("APIC%u: apicGetInterrupt: APIC %s disabled\n", pVCpu->idCpu, !fApicHwEnabled ? "hardware" : "software"));

    *pu8Vector = 0;
    *puSrcTag  = 0;
    return VERR_APIC_INTR_NOT_PENDING;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECLCALLBACK(VBOXSTRICTRC) apicReadMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0xf));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu    = PDMDevHlpGetVMCPU(pDevIns);
    uint16_t offReg   = off & 0xff0;
    uint32_t uValue   = 0;

    STAM_COUNTER_INC(&pVCpu->apic.s.CTX_SUFF_Z(StatMmioRead));

    VBOXSTRICTRC rc = VBOXSTRICTRC_VAL(apicReadRegister(pDevIns, pVCpu, offReg, &uValue));
    *(uint32_t *)pv = uValue;

    Log2(("APIC%u: apicReadMmio: offReg=%#RX16 uValue=%#RX32\n", pVCpu->idCpu, offReg, uValue));
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECLCALLBACK(VBOXSTRICTRC) apicWriteMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0xf));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu    = PDMDevHlpGetVMCPU(pDevIns);
    uint16_t offReg   = off & 0xff0;
    uint32_t uValue   = *(uint32_t *)pv;

    STAM_COUNTER_INC(&pVCpu->apic.s.CTX_SUFF_Z(StatMmioWrite));

    Log2(("APIC%u: apicWriteMmio: offReg=%#RX16 uValue=%#RX32\n", pVCpu->idCpu, offReg, uValue));

    return apicWriteRegister(pDevIns, pVCpu, offReg, uValue);
}


/**
 * Sets the interrupt pending force-flag and pokes the EMT if required.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   enmType         The IRQ type.
 */
static void apicSetInterruptFF(PVMCPUCC pVCpu, PDMAPICIRQ enmType)
{
#ifdef IN_RING3
    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
#endif

    switch (enmType)
    {
        case PDMAPICIRQ_HARDWARE:
            VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
            VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC);
            break;
        case PDMAPICIRQ_UPDATE_PENDING: VMCPU_FF_SET(pVCpu, VMCPU_FF_UPDATE_APIC);    break;
        case PDMAPICIRQ_NMI:            VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI);  break;
        case PDMAPICIRQ_SMI:            VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_SMI);  break;
        case PDMAPICIRQ_EXTINT:         VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_PIC);  break;
        default:
            AssertMsgFailed(("enmType=%d\n", enmType));
            break;
    }

    /*
     * We need to wake up the target CPU if we're not on EMT.
     */
    /** @todo r=bird: Why do we skip this waking up for PDMAPICIRQ_HARDWARE? */
    /** @todo r=bird: We could just use RTThreadNativeSelf() here, couldn't we? */
#if defined(IN_RING0)
    PVMCC   pVM   = pVCpu->CTX_SUFF(pVM);
    VMCPUID idCpu = pVCpu->idCpu;
    if (   enmType != PDMAPICIRQ_HARDWARE
        && VMMGetCpuId(pVM) != idCpu)
    {
        switch (VMCPU_GET_STATE(pVCpu))
        {
            case VMCPUSTATE_STARTED_EXEC:
                Log7Func(("idCpu=%u VMCPUSTATE_STARTED_EXEC\n", idCpu));
                GVMMR0SchedPokeNoGVMNoLock(pVM, idCpu);
                break;

            case VMCPUSTATE_STARTED_HALTED:
                Log7Func(("idCpu=%u VMCPUSTATE_STARTED_HALTED\n", idCpu));
                GVMMR0SchedWakeUpNoGVMNoLock(pVM, idCpu);
                break;

            default:
                Log7Func(("idCpu=%u enmState=%d\n", idCpu, pVCpu->enmState));
                break; /* nothing to do in other states. */
        }
    }
#elif defined(IN_RING3)
    PVMCC   pVM   = pVCpu->CTX_SUFF(pVM);
    VMCPUID idCpu = pVCpu->idCpu;
    if (   enmType != PDMAPICIRQ_HARDWARE
        && VMMGetCpuId(pVM) != idCpu)
    {
        Log7Func(("idCpu=%u enmState=%d\n", idCpu, pVCpu->enmState));
        VMR3NotifyCpuFFU(pVCpu->pUVCpu, VMNOTIFYFF_FLAGS_DONE_REM | VMNOTIFYFF_FLAGS_POKE);
    }
#endif
}


/**
 * Clears the interrupt pending force-flag.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   enmType         The IRQ type.
 */
void apicClearInterruptFF(PVMCPUCC pVCpu, PDMAPICIRQ enmType)
{
#ifdef IN_RING3
    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
#endif

    /* NMI/SMI can't be cleared. */
    switch (enmType)
    {
        case PDMAPICIRQ_HARDWARE:   VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_APIC); break;
        case PDMAPICIRQ_EXTINT:     VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_PIC);  break;
        default:
            AssertMsgFailed(("enmType=%d\n", enmType));
            break;
    }
}


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
 * @param   enmTriggerMode      The trigger mode of the interrupt.
 * @param   uSrcTag             The interrupt source tag (debugging).
 *
 * @thread  Any.
 */
bool apicPostInterrupt(PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, uint32_t uSrcTag)
{
    Assert(pVCpu);
    Assert(uVector > XAPIC_ILLEGAL_VECTOR_END);

    PVMCC    pVM       = pVCpu->CTX_SUFF(pVM);
    PCAPIC   pApic     = VM_TO_APIC(pVM);
    PAPICCPU pApicCpu  = VMCPU_TO_APICCPU(pVCpu);
    bool     fAccepted = true;

    STAM_PROFILE_START(&pApicCpu->StatPostIntr, a);
    STAM_REL_COUNTER_INC(&pApicCpu->StatPostIntrCnt);
    STAM_REL_COUNTER_INC(&pApicCpu->aStatVectors[uVector]);

    /*
     * Only post valid interrupt vectors.
     * See Intel spec. 10.5.2 "Valid Interrupt Vectors".
     */
    if (RT_LIKELY(uVector > XAPIC_ILLEGAL_VECTOR_END))
    {
        /*
         * If the interrupt is already pending in the IRR we can skip the
         * potential expensive operation of poking the guest EMT out of execution.
         */
        PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
        if (!apicTestVectorInReg(&pXApicPage->irr, uVector))     /* PAV */
        {
            /* Update the interrupt source tag (debugging). */
            if (!pApicCpu->auSrcTags[uVector])
                pApicCpu->auSrcTags[uVector]  = uSrcTag;
            else
                pApicCpu->auSrcTags[uVector] |= RT_BIT_32(31);

            Log2(("APIC: apicPostInterrupt: SrcCpu=%u TargetCpu=%u uVector=%#x %s\n",
                  VMMGetCpuId(pVM), pVCpu->idCpu, uVector, enmTriggerMode == XAPICTRIGGERMODE_EDGE ? "edge" : "lvl"));
            if (enmTriggerMode == XAPICTRIGGERMODE_EDGE)
            {
                if (pApic->fPostedIntrsEnabled)
                { /** @todo posted-interrupt call to hardware */ }
                else
                {
                    apicSetVectorInPib(pApicCpu->CTX_SUFF(pvApicPib), uVector);
                    uint32_t const fAlreadySet = apicSetNotificationBitInPib((PAPICPIB)pApicCpu->CTX_SUFF(pvApicPib));
                    if (!fAlreadySet)
                    {
                        Log2(("APIC: apicPostInterrupt: Setting UPDATE_APIC FF for edge-triggered intr. uVector=%#x\n", uVector));
                        apicSetInterruptFF(pVCpu, PDMAPICIRQ_UPDATE_PENDING);
                    }
                }
            }
            else
            {
                /*
                 * Level-triggered interrupts requires updating of the TMR and thus cannot be
                 * delivered asynchronously.
                 */
                apicSetVectorInPib(&pApicCpu->ApicPibLevel, uVector);
                uint32_t const fAlreadySet = apicSetNotificationBitInPib(&pApicCpu->ApicPibLevel);
                if (!fAlreadySet)
                {
                    Log2(("APIC: apicPostInterrupt: Setting UPDATE_APIC FF for level-triggered intr. uVector=%#x\n", uVector));
                    apicSetInterruptFF(pVCpu, PDMAPICIRQ_UPDATE_PENDING);
                }
            }
        }
        else
        {
            Log2(("APIC: apicPostInterrupt: SrcCpu=%u TargetCpu=%u. Vector %#x Already in IRR, skipping\n", VMMGetCpuId(pVM),
                  pVCpu->idCpu, uVector));
            STAM_COUNTER_INC(&pApicCpu->StatPostIntrAlreadyPending);
        }
    }
    else
    {
        fAccepted = false;
        apicSetError(pVCpu, XAPIC_ESR_RECV_ILLEGAL_VECTOR);
    }

    STAM_PROFILE_STOP(&pApicCpu->StatPostIntr, a);
    return fAccepted;
}


/**
 * Starts the APIC timer.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uInitialCount   The timer's Initial-Count Register (ICR), must be >
 *                          0.
 * @thread  Any.
 */
void apicStartTimer(PVMCPUCC pVCpu, uint32_t uInitialCount)
{
    Assert(pVCpu);
    PAPICCPU   pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    PPDMDEVINS pDevIns  = VMCPU_TO_DEVINS(pVCpu);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pApicCpu->hTimer));
    Assert(uInitialCount > 0);

    PCXAPICPAGE    pXApicPage   = APICCPU_TO_CXAPICPAGE(pApicCpu);
    uint8_t  const uTimerShift  = apicGetTimerShift(pXApicPage);
    uint64_t const cTicksToNext = (uint64_t)uInitialCount << uTimerShift;

    Log2(("APIC%u: apicStartTimer: uInitialCount=%#RX32 uTimerShift=%u cTicksToNext=%RU64\n", pVCpu->idCpu, uInitialCount,
          uTimerShift, cTicksToNext));

    /*
     * The assumption here is that the timer doesn't tick during this call
     * and thus setting a relative time to fire next is accurate. The advantage
     * however is updating u64TimerInitial 'atomically' while setting the next
     * tick.
     */
    PDMDevHlpTimerSetRelative(pDevIns, pApicCpu->hTimer, cTicksToNext, &pApicCpu->u64TimerInitial);
    apicHintTimerFreq(pDevIns, pApicCpu, uInitialCount, uTimerShift);
}


/**
 * Stops the APIC timer.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @thread  Any.
 */
static void apicStopTimer(PVMCPUCC pVCpu)
{
    Assert(pVCpu);
    PAPICCPU   pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    PPDMDEVINS pDevIns  = VMCPU_TO_DEVINS(pVCpu);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pApicCpu->hTimer));

    Log2(("APIC%u: apicStopTimer\n", pVCpu->idCpu));

    PDMDevHlpTimerStop(pDevIns, pApicCpu->hTimer); /* This will reset the hint, no need to explicitly call TMTimerSetFrequencyHint(). */
    pApicCpu->uHintedTimerInitialCount = 0;
    pApicCpu->uHintedTimerShift = 0;
}


/**
 * Queues a pending interrupt as in-service.
 *
 * This function should only be needed without virtualized APIC
 * registers. With virtualized APIC registers, it's sufficient to keep
 * the interrupts pending in the IRR as the hardware takes care of
 * virtual interrupt delivery.
 *
 * @returns true if the interrupt was queued to in-service interrupts,
 *          false otherwise.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   u8PendingIntr       The pending interrupt to queue as
 *                              in-service.
 *
 * @remarks This assumes the caller has done the necessary checks and
 *          is ready to take actually service the interrupt (TPR,
 *          interrupt shadow etc.)
 */
VMM_INT_DECL(bool) APICQueueInterruptToService(PVMCPUCC pVCpu, uint8_t u8PendingIntr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    PVMCC pVM   = pVCpu->CTX_SUFF(pVM);
    PAPIC pApic = VM_TO_APIC(pVM);
    Assert(!pApic->fVirtApicRegsEnabled);
    NOREF(pApic);

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    bool const fIsPending = apicTestVectorInReg(&pXApicPage->irr, u8PendingIntr);
    if (fIsPending)
    {
        apicClearVectorInReg(&pXApicPage->irr, u8PendingIntr);
        apicSetVectorInReg(&pXApicPage->isr, u8PendingIntr);
        apicUpdatePpr(pVCpu);
        return true;
    }
    return false;
}


/**
 * De-queues a pending interrupt from in-service.
 *
 * This undoes APICQueueInterruptToService() for premature VM-exits before event
 * injection.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   u8PendingIntr       The pending interrupt to de-queue from
 *                              in-service.
 */
VMM_INT_DECL(void) APICDequeueInterruptFromService(PVMCPUCC pVCpu, uint8_t u8PendingIntr)
{
    VMCPU_ASSERT_EMT(pVCpu);

    PVMCC pVM   = pVCpu->CTX_SUFF(pVM);
    PAPIC pApic = VM_TO_APIC(pVM);
    Assert(!pApic->fVirtApicRegsEnabled);
    NOREF(pApic);

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    bool const fInService = apicTestVectorInReg(&pXApicPage->isr, u8PendingIntr);
    if (fInService)
    {
        apicClearVectorInReg(&pXApicPage->isr, u8PendingIntr);
        apicSetVectorInReg(&pXApicPage->irr, u8PendingIntr);
        apicUpdatePpr(pVCpu);
    }
}


/**
 * Updates pending interrupts from the pending-interrupt bitmaps to the IRR.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 *
 * @note    NEM/win is ASSUMING the an up to date TPR is not required here.
 */
VMMDECL(void) APICUpdatePendingInterrupts(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

    PAPICCPU   pApicCpu         = VMCPU_TO_APICCPU(pVCpu);
    PXAPICPAGE pXApicPage       = VMCPU_TO_XAPICPAGE(pVCpu);
    bool       fHasPendingIntrs = false;

    Log3(("APIC%u: APICUpdatePendingInterrupts:\n", pVCpu->idCpu));
    STAM_PROFILE_START(&pApicCpu->StatUpdatePendingIntrs, a);

    /* Update edge-triggered pending interrupts. */
    PAPICPIB pPib = (PAPICPIB)pApicCpu->CTX_SUFF(pvApicPib);
    for (;;)
    {
        uint32_t const fAlreadySet = apicClearNotificationBitInPib((PAPICPIB)pApicCpu->CTX_SUFF(pvApicPib));
        if (!fAlreadySet)
            break;

        AssertCompile(RT_ELEMENTS(pXApicPage->irr.u) == 2 * RT_ELEMENTS(pPib->au64VectorBitmap));
        for (size_t idxPib = 0, idxReg = 0; idxPib < RT_ELEMENTS(pPib->au64VectorBitmap); idxPib++, idxReg += 2)
        {
            uint64_t const u64Fragment = ASMAtomicXchgU64(&pPib->au64VectorBitmap[idxPib], 0);
            if (u64Fragment)
            {
                uint32_t const u32FragmentLo = RT_LO_U32(u64Fragment);
                uint32_t const u32FragmentHi = RT_HI_U32(u64Fragment);
                Log6Func(("edge[%u/%u]: %'016RX64: irr=%08RX32'%08RX32 |; tmr=%08RX32'%08RX32 &~\n", idxPib, idxReg, u64Fragment,
                          pXApicPage->irr.u[idxReg].u32Reg, pXApicPage->irr.u[idxReg + 1].u32Reg,
                          pXApicPage->tmr.u[idxReg].u32Reg, pXApicPage->tmr.u[idxReg + 1].u32Reg));

                pXApicPage->irr.u[idxReg].u32Reg     |=  u32FragmentLo;
                pXApicPage->irr.u[idxReg + 1].u32Reg |=  u32FragmentHi;

                pXApicPage->tmr.u[idxReg].u32Reg     &= ~u32FragmentLo;
                pXApicPage->tmr.u[idxReg + 1].u32Reg &= ~u32FragmentHi;
                fHasPendingIntrs = true;
            }
        }
    }

    /* Update level-triggered pending interrupts. */
    pPib = (PAPICPIB)&pApicCpu->ApicPibLevel;
    for (;;)
    {
        uint32_t const fAlreadySet = apicClearNotificationBitInPib((PAPICPIB)&pApicCpu->ApicPibLevel);
        if (!fAlreadySet)
            break;

        AssertCompile(RT_ELEMENTS(pXApicPage->irr.u) == 2 * RT_ELEMENTS(pPib->au64VectorBitmap));
        for (size_t idxPib = 0, idxReg = 0; idxPib < RT_ELEMENTS(pPib->au64VectorBitmap); idxPib++, idxReg += 2)
        {
            uint64_t const u64Fragment = ASMAtomicXchgU64(&pPib->au64VectorBitmap[idxPib], 0);
            if (u64Fragment)
            {
                Log6Func(("level[%u/%u]: %'016RX64: irr=%08RX32'%08RX32 |; tmr=%08RX32'%08RX32 |\n", idxPib, idxReg, u64Fragment,
                          pXApicPage->irr.u[idxReg].u32Reg, pXApicPage->irr.u[idxReg + 1].u32Reg,
                          pXApicPage->tmr.u[idxReg].u32Reg, pXApicPage->tmr.u[idxReg + 1].u32Reg));
                uint32_t const u32FragmentLo = RT_LO_U32(u64Fragment);
                uint32_t const u32FragmentHi = RT_HI_U32(u64Fragment);

                pXApicPage->irr.u[idxReg].u32Reg     |= u32FragmentLo;
                pXApicPage->irr.u[idxReg + 1].u32Reg |= u32FragmentHi;

                pXApicPage->tmr.u[idxReg].u32Reg     |= u32FragmentLo;
                pXApicPage->tmr.u[idxReg + 1].u32Reg |= u32FragmentHi;
                fHasPendingIntrs = true;
            }
        }
    }

    STAM_PROFILE_STOP(&pApicCpu->StatUpdatePendingIntrs, a);
    Log3(("APIC%u: APICUpdatePendingInterrupts: fHasPendingIntrs=%RTbool\n", pVCpu->idCpu, fHasPendingIntrs));

    if (   fHasPendingIntrs
        && !VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC))
        apicSignalNextPendingIntr(pVCpu);
}


/**
 * Gets the highest priority pending interrupt.
 *
 * @returns true if any interrupt is pending, false otherwise.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   pu8PendingIntr      Where to store the interrupt vector if the
 *                              interrupt is pending.
 */
VMM_INT_DECL(bool) APICGetHighestPendingInterrupt(PVMCPUCC pVCpu, uint8_t *pu8PendingIntr)
{
    VMCPU_ASSERT_EMT(pVCpu);
    return apicGetHighestPendingInterrupt(pVCpu, pu8PendingIntr);
}


/**
 * Posts an interrupt to a target APIC, Hyper-V interface.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uVector             The vector of the interrupt to be posted.
 * @param   fAutoEoi            Whether this interrupt has automatic EOI
 *                              treatment.
 * @param   enmTriggerMode      The trigger mode of the interrupt.
 *
 * @thread  Any.
 */
VMM_INT_DECL(void) APICHvSendInterrupt(PVMCPUCC pVCpu, uint8_t uVector, bool fAutoEoi, XAPICTRIGGERMODE enmTriggerMode)
{
    Assert(pVCpu);
    Assert(!fAutoEoi);    /** @todo AutoEOI.  */
    RT_NOREF(fAutoEoi);
    apicPostInterrupt(pVCpu, uVector, enmTriggerMode, 0 /* uSrcTag */);
}


/**
 * Sets the Task Priority Register (TPR), Hyper-V interface.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uTpr        The TPR value to set.
 *
 * @remarks Validates like in x2APIC mode.
 */
VMM_INT_DECL(VBOXSTRICTRC) APICHvSetTpr(PVMCPUCC pVCpu, uint8_t uTpr)
{
    Assert(pVCpu);
    VMCPU_ASSERT_EMT(pVCpu);
    return apicSetTprEx(pVCpu, uTpr, true /* fForceX2ApicBehaviour */);
}


/**
 * Gets the Task Priority Register (TPR), Hyper-V interface.
 *
 * @returns The TPR value.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMM_INT_DECL(uint8_t) APICHvGetTpr(PVMCPUCC pVCpu)
{
    Assert(pVCpu);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * The APIC could be operating in xAPIC mode and thus we should not use the apicReadMsr()
     * interface which validates the APIC mode and will throw a #GP(0) if not in x2APIC mode.
     * We could use the apicReadRegister() MMIO interface, but why bother getting the PDMDEVINS
     * pointer, so just directly read the APIC page.
     */
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    return apicReadRaw32(pXApicPage, XAPIC_OFF_TPR);
}


/**
 * Sets the Interrupt Command Register (ICR), Hyper-V interface.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uIcr        The ICR value to set.
 */
VMM_INT_DECL(VBOXSTRICTRC) APICHvSetIcr(PVMCPUCC pVCpu, uint64_t uIcr)
{
    Assert(pVCpu);
    VMCPU_ASSERT_EMT(pVCpu);
    return apicSetIcr(pVCpu, uIcr, VINF_CPUM_R3_MSR_WRITE);
}


/**
 * Gets the Interrupt Command Register (ICR), Hyper-V interface.
 *
 * @returns The ICR value.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMM_INT_DECL(uint64_t) APICHvGetIcr(PVMCPUCC pVCpu)
{
    Assert(pVCpu);
    VMCPU_ASSERT_EMT(pVCpu);
    return apicGetIcrNoCheck(pVCpu);
}


/**
 * Sets the End-Of-Interrupt (EOI) register, Hyper-V interface.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uEoi            The EOI value.
 */
VMM_INT_DECL(VBOXSTRICTRC) APICHvSetEoi(PVMCPUCC pVCpu, uint32_t uEoi)
{
    Assert(pVCpu);
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    return apicSetEoi(pVCpu, uEoi, true /* fForceX2ApicBehaviour */);
}


/**
 * Gets the APIC page pointers for the specified VCPU.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pHCPhys         Where to store the host-context physical address.
 * @param   pR0Ptr          Where to store the ring-0 address.
 * @param   pR3Ptr          Where to store the ring-3 address (optional).
 */
VMM_INT_DECL(int) APICGetApicPageForCpu(PCVMCPUCC pVCpu, PRTHCPHYS pHCPhys, PRTR0PTR pR0Ptr, PRTR3PTR pR3Ptr)
{
    AssertReturn(pVCpu,   VERR_INVALID_PARAMETER);
    AssertReturn(pHCPhys, VERR_INVALID_PARAMETER);
    AssertReturn(pR0Ptr,  VERR_INVALID_PARAMETER);

    Assert(PDMHasApic(pVCpu->CTX_SUFF(pVM)));

    PCAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    *pHCPhys = pApicCpu->HCPhysApicPage;
    *pR0Ptr  = pApicCpu->pvApicPageR0;
    if (pR3Ptr)
        *pR3Ptr  = pApicCpu->pvApicPageR3;
    return VINF_SUCCESS;
}

#ifndef IN_RING3

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) apicRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PAPICDEV pThis = PDMDEVINS_2_DATA(pDevIns, PAPICDEV);
    PVMCC    pVM   = PDMDevHlpGetVM(pDevIns);

    pVM->apicr0.s.pDevInsR0 = pDevIns;

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpApicSetUpContext(pDevIns);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, apicWriteMmio, apicReadMmio, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}
#endif /* !IN_RING3 */

/**
 * APIC device registration structure.
 */
const PDMDEVREG g_DeviceAPIC =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "apic",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_REQUIRE_R0 | PDM_DEVREG_FLAGS_REQUIRE_RC,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(APICDEV),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Advanced Programmable Interrupt Controller",
#if defined(IN_RING3)
    /* .szRCMod = */                "VMMRC.rc",
    /* .szR0Mod = */                "VMMR0.r0",
    /* .pfnConstruct = */           apicR3Construct,
    /* .pfnDestruct = */            apicR3Destruct,
    /* .pfnRelocate = */            apicR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               apicR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        apicR3InitComplete,
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
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           apicRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           apicRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

