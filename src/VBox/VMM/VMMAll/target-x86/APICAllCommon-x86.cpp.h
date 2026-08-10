/* $Id: APICAllCommon-x86.cpp.h 114733 2026-07-21 06:02:36Z alexander.eichner@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller - All-context and R3-context common code.
 */

/*
 * Copyright (C) 2025-2026 Oracle and/or its affiliates.
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


#ifndef VMM_INCLUDED_SRC_VMMAll_APICAllCommon_x86_cpp_h
#define VMM_INCLUDED_SRC_VMMAll_APICAllCommon_x86_cpp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/**
 * Gets the timer shift value.
 *
 * @returns The timer shift value.
 * @param   pXApicPage      The xAPIC page.
 */
DECL_FORCE_INLINE(uint8_t) apicCommonGetTimerShift(PCXAPICPAGE pXApicPage)
{
    /* See Intel spec. 10.5.4 "APIC Timer". */
    uint32_t uShift = pXApicPage->timer_dcr.u.u2DivideValue0 | (pXApicPage->timer_dcr.u.u1DivideValue1 << 2);
    return (uShift + 1) & 7;
}


/**
 * Gets the APIC mode given the base MSR value.
 *
 * @returns The APIC mode.
 * @param   uApicBaseMsr        The APIC Base MSR value.
 */
static XAPICMODE apicCommonGetMode(uint64_t uApicBaseMsr)
{
    uint32_t const  uMode   = (uApicBaseMsr >> 10) & UINT64_C(3);
    XAPICMODE const enmMode = (XAPICMODE)uMode;
#ifdef VBOX_STRICT
    /* Paranoia. */
    switch (uMode)
    {
        case XAPICMODE_DISABLED:
        case XAPICMODE_INVALID:
        case XAPICMODE_XAPIC:
        case XAPICMODE_X2APIC:
            break;
        default:
            AssertMsgFailed(("Invalid mode"));
    }
#endif
    return enmMode;
}


#if defined(VMM_APIC_TEMPLATE_R3_COMMON) || defined(VMM_APIC_TEMPLATE_GET_MODE_NAME)
/**
 * Gets the descriptive APIC mode.
 *
 * @returns The name.
 * @param   enmMode     The xAPIC mode.
 */
static const char *apicCommonGetModeName(XAPICMODE enmMode)
{
    switch (enmMode)
    {
        case XAPICMODE_DISABLED:  return "Disabled";
        case XAPICMODE_XAPIC:     return "xAPIC";
        case XAPICMODE_X2APIC:    return "x2APIC";
        default:                  break;
    }
    return "Invalid";
}
#endif


#if defined(VMM_APIC_TEMPLATE_R3_COMMON) || defined(VMM_APIC_TEMPLATE_GET_DELIVERY_MODE_NAME)
/**
 * Gets the descriptive delivery mode name.
 *
 * @returns The delivery mode name.
 * @param   enmDeliveryMode     The delivery mode.
 */
static const char *apicCommonGetDeliveryModeName(XAPICDELIVERYMODE enmDeliveryMode)
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
#endif


#if defined(VMM_APIC_TEMPLATE_R3_COMMON) || defined(VMM_APIC_TEMPLATE_GET_DEST_MODE_NAME)
/**
 * Gets the descriptive destination mode name.
 *
 * @returns The destination mode name.
 * @param   enmDestMode     The destination mode.
 */
static const char *apicCommonGetDestModeName(XAPICDESTMODE enmDestMode)
{
    switch (enmDestMode)
    {
        case XAPICDESTMODE_PHYSICAL:  return "Physical";
        case XAPICDESTMODE_LOGICAL:   return "Logical";
        default:                      break;
    }
    return "Invalid";
}
#endif


#if defined(VMM_APIC_TEMPLATE_R3_COMMON) || defined(VMM_APIC_TEMPLATE_GET_TRIGGER_MODE_NAME)
/**
 * Gets the descriptive trigger mode name.
 *
 * @returns The trigger mode name.
 * @param   enmTriggerMode     The trigger mode.
 */
static const char *apicCommonGetTriggerModeName(XAPICTRIGGERMODE enmTriggerMode)
{
    switch (enmTriggerMode)
    {
        case XAPICTRIGGERMODE_EDGE:     return "Edge";
        case XAPICTRIGGERMODE_LEVEL:    return "Level";
        default:                        break;
    }
    return "Invalid";
}
#endif


#ifdef VMM_APIC_TEMPLATE_ALL_COMMON
/**
 * @copydoc{PDMAPICBACKENDR3::pfnInitIpi}
 */
static void apicCommonInitIpi(PVMCPUCC pVCpu)
{
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
}


#ifndef VMM_INCLUDED_SRC_include_APICKvmInternal_h
/**
 * Checks if this APIC belongs to a logical destination.
 *
 * @returns @c true if the APIC belongs to the logical destination,
 *          @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fDest   The destination mask.
 *
 * @thread  Any.
 */
static bool apicCommonIsLogicalDest(PVMCPUCC pVCpu, uint32_t fDest)
{
    if (XAPIC_IN_X2APIC_MODE(pVCpu->apic.s.uApicBaseMsr))
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
    Assert(!XAPIC_IN_X2APIC_MODE(pVCpu->apic.s.uApicBaseMsr));
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
 * @param   pVM                 The cross context VM structure.
 * @param   fDestMask           The destination mask.
 * @param   fBroadcastMask      The broadcast mask.
 * @param   enmDestMode         The destination mode.
 * @param   enmDeliveryMode     The delivery mode.
 * @param   pDestCpuSet         The destination CPU set to update.
 */
static void apicCommonGetDestCpuSet(PVMCC pVM, uint32_t fDestMask, uint32_t fBroadcastMask, XAPICDESTMODE enmDestMode,
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
            if (apicCommonIsLogicalDest(pVCpuDst, fDestMask))
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
            if (apicCommonIsLogicalDest(pVCpuDst, fDestMask))
                VMCPUSET_ADD(pDestCpuSet, pVCpuDst->idCpu);
        }
    }
}
#endif /* VMM_INCLUDED_SRC_include_APICKvmInternal_h */


/**
 * Resets the APIC base MSR.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void apicCommonResetBaseMsr(PVMCPUCC pVCpu)
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
    uint64_t uApicBaseMsr = MSR_IA32_APICBASE_ADDR;
    if (pVCpu->idCpu == 0)
        uApicBaseMsr |= MSR_IA32_APICBASE_BSP;

    /* If the VM was configured with no APIC, don't enable xAPIC mode, obviously. */
    if (pVCpu->CTX_SUFF(pVM)->apic.s.enmMaxMode != PDMAPICMODE_NONE)
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
    ASMAtomicWriteU64(&pVCpu->apic.s.uApicBaseMsr, uApicBaseMsr);
}
#endif /* VMM_APIC_TEMPLATE_ALL_COMMON */


#if defined(IN_RING3) && defined(VMM_APIC_TEMPLATE_R3_COMMON)
/**
 * Sets the CPUID feature bits for the APIC mode.
 *
 * @param   pVM             The cross context VM structure.
 * @param   enmMode         The APIC mode.
 */
static void apicR3CommonSetCpuIdFeatureLevel(PVM pVM, PDMAPICMODE enmMode)
{
    /** @todo Merge with apicR3HvSetCpuIdFeatureLevel. */
    switch (enmMode)
    {
        case PDMAPICMODE_NONE:
            CPUMR3ClearGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_X2APIC);
            CPUMR3ClearGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_APIC);
            break;

        case PDMAPICMODE_APIC:
            CPUMR3ClearGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_X2APIC);
            CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_APIC);
            break;

        case PDMAPICMODE_X2APIC:
            CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_APIC);
            CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_X2APIC);
            break;

        default:
            AssertMsgFailed(("Unknown/invalid APIC mode: %d\n", (int)enmMode));
    }
}


/**
 * Helper for dumping an APIC 256-bit sparse register.
 *
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   pHlp            The debug output helper.
 */
static void apicR3CommonDbgInfo256BitReg(volatile const XAPIC256BITREG *pApicReg, PCDBGFINFOHLP pHlp)
{
    /** @todo Merge with APICAll. */

    ssize_t const  cFragments = RT_ELEMENTS(pApicReg->u);
    unsigned const cBitsPerFragment = sizeof(pApicReg->u[0].u32Reg) * 8;
    XAPIC256BITREG ApicReg;
    RT_ZERO(ApicReg);

    pHlp->pfnPrintf(pHlp, "    ");
    for (ssize_t i = cFragments - 1; i >= 0; i--)
    {
        uint32_t const uFragment = pApicReg->u[i].u32Reg;
        ApicReg.u[i].u32Reg = uFragment;
        pHlp->pfnPrintf(pHlp, "%08x", uFragment);
    }
    pHlp->pfnPrintf(pHlp, "\n");

    uint32_t cPending = 0;
    pHlp->pfnPrintf(pHlp, "    Pending:");
    for (ssize_t i = cFragments - 1; i >= 0; i--)
    {
        uint32_t uFragment = ApicReg.u[i].u32Reg;
        if (uFragment)
        {
            do
            {
                unsigned idxSetBit = ASMBitLastSetU32(uFragment);
                --idxSetBit;
                ASMBitClear(&uFragment, idxSetBit);

                idxSetBit += (i * cBitsPerFragment);
                pHlp->pfnPrintf(pHlp, " %#02x", idxSetBit);
                ++cPending;
            } while (uFragment);
        }
    }
    if (!cPending)
        pHlp->pfnPrintf(pHlp, " None");
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * Gets the descriptive destination format name.
 *
 * @returns The destination format name.
 * @param   enmDestFormat       The destination format.
 */
static const char *apicR3CommonGetDestFormatName(XAPICDESTFORMAT enmDestFormat)
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
 * Gets the destination shorthand name.
 *
 * @returns The destination shorthand name.
 * @param   enmDestShorthand     The destination shorthand.
 */
static const char *apicR3CommonGetDestShorthandName(XAPICDESTSHORTHAND enmDestShorthand)
{
    switch (enmDestShorthand)
    {
        case XAPICDESTSHORTHAND_NONE:           return "None";
        case XAPICDESTSHORTHAND_SELF:           return "Self";
        case XAPICDESTSHORTHAND_ALL_INCL_SELF:  return "All including self";
        case XAPICDESTSHORTHAND_ALL_EXCL_SELF:  return "All excluding self";
        default:                                break;
    }
    return "Invalid";
}


static const char *apicR3CommonGetTimerModeName(XAPICTIMERMODE enmTimerMode)
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


static void apicR3CommonDbgInfoLvt(PCVMCPUCC pVCpu, PCDBGFINFOHLP pHlp)
{
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);

    /*
     * Delivery modes available in the LVT entries. They're different (more reserved stuff) from the
     * ICR delivery modes and hence we don't use apicGetDeliveryMode but mostly because we want small,
     * fixed-length strings to fit our formatting needs here.
     */
    static const char * const s_apszLvtDeliveryModes[] =
    {
        "Fixed ",
        "Rsvd  ",
        "SMI   ",
        "Rsvd  ",
        "NMI   ",
        "INIT  ",
        "Rsvd  ",
        "ExtINT"
    };
    /* Delivery Status. */
    static const char * const s_apszLvtDeliveryStatus[] =
    {
        "Idle",
        "Pend"
    };
    const char *pszNotApplicable = "";

    pHlp->pfnPrintf(pHlp, "VCPU[%u] APIC Local Vector Table (LVT):\n", pVCpu->idCpu);
    pHlp->pfnPrintf(pHlp, "lvt     timermode  mask  trigger  rirr  polarity  dlvr_st  dlvr_mode   vector\n");
    /* Timer. */
    {
        /* Timer modes. */
        static const char * const s_apszLvtTimerModes[] =
        {
            "One-shot ",
            "Periodic ",
            "TSC-dline"
        };
        const uint32_t       uLvtTimer         = pXApicPage->lvt_timer.all.u32LvtTimer;
        const XAPICTIMERMODE enmTimerMode      = XAPIC_LVT_GET_TIMER_MODE(uLvtTimer);
        const char          *pszTimerMode      = s_apszLvtTimerModes[enmTimerMode];
        const uint8_t        uMask             = XAPIC_LVT_IS_MASKED(uLvtTimer);
        const uint8_t        uDeliveryStatus   = uLvtTimer & XAPIC_LVT_DELIVERY_STATUS;
        const char          *pszDeliveryStatus = s_apszLvtDeliveryStatus[uDeliveryStatus];
        const uint8_t        uVector           = XAPIC_LVT_GET_VECTOR(uLvtTimer);

        pHlp->pfnPrintf(pHlp, "%-7s  %9s  %u     %5s     %1s   %8s    %4s     %6s    %3u (%#x)\n",
                        "Timer",
                        pszTimerMode,
                        uMask,
                        pszNotApplicable, /* TriggerMode */
                        pszNotApplicable, /* Remote IRR */
                        pszNotApplicable, /* Polarity */
                        pszDeliveryStatus,
                        pszNotApplicable, /* Delivery Mode */
                        uVector,
                        uVector);
    }

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    /* Thermal sensor. */
    {
        uint32_t const uLvtThermal = pXApicPage->lvt_thermal.all.u32LvtThermal;
        const uint8_t           uMask             = XAPIC_LVT_IS_MASKED(uLvtThermal);
        const uint8_t           uDeliveryStatus   = uLvtThermal & XAPIC_LVT_DELIVERY_STATUS;
        const char             *pszDeliveryStatus = s_apszLvtDeliveryStatus[uDeliveryStatus];
        const XAPICDELIVERYMODE enmDeliveryMode   = XAPIC_LVT_GET_DELIVERY_MODE(uLvtThermal);
        const char             *pszDeliveryMode   = s_apszLvtDeliveryModes[enmDeliveryMode];
        const uint8_t           uVector           = XAPIC_LVT_GET_VECTOR(uLvtThermal);

        pHlp->pfnPrintf(pHlp, "%-7s  %9s  %u     %5s     %1s   %8s    %4s     %6s    %3u (%#x)\n",
                        "Thermal",
                        pszNotApplicable, /* Timer mode */
                        uMask,
                        pszNotApplicable, /* TriggerMode */
                        pszNotApplicable, /* Remote IRR */
                        pszNotApplicable, /* Polarity */
                        pszDeliveryStatus,
                        pszDeliveryMode,
                        uVector,
                        uVector);
    }
#endif

    /* Performance Monitor Counters. */
    {
        uint32_t const uLvtPerf = pXApicPage->lvt_thermal.all.u32LvtThermal;
        const uint8_t           uMask             = XAPIC_LVT_IS_MASKED(uLvtPerf);
        const uint8_t           uDeliveryStatus   = uLvtPerf & XAPIC_LVT_DELIVERY_STATUS;
        const char             *pszDeliveryStatus = s_apszLvtDeliveryStatus[uDeliveryStatus];
        const XAPICDELIVERYMODE enmDeliveryMode   = XAPIC_LVT_GET_DELIVERY_MODE(uLvtPerf);
        const char             *pszDeliveryMode   = s_apszLvtDeliveryModes[enmDeliveryMode];
        const uint8_t           uVector           = XAPIC_LVT_GET_VECTOR(uLvtPerf);

        pHlp->pfnPrintf(pHlp, "%-7s  %9s  %u     %5s     %1s   %8s    %4s     %6s    %3u (%#x)\n",
                        "Perf",
                        pszNotApplicable, /* Timer mode */
                        uMask,
                        pszNotApplicable, /* TriggerMode */
                        pszNotApplicable, /* Remote IRR */
                        pszNotApplicable, /* Polarity */
                        pszDeliveryStatus,
                        pszDeliveryMode,
                        uVector,
                        uVector);
    }

    /* LINT0, LINT1. */
    {
        /* LINTx name. */
        static const char * const s_apszLvtLint[] =
        {
            "LINT0",
            "LINT1"
        };
        /* Trigger mode. */
        static const char * const s_apszLvtTriggerModes[] =
        {
            "Edge ",
            "Level"
        };
        /* Polarity. */
        static const char * const s_apszLvtPolarity[] =
        {
            "ActiveHi",
            "ActiveLo"
        };

        uint32_t aLvtLint[2];
        aLvtLint[0] = pXApicPage->lvt_lint0.all.u32LvtLint0;
        aLvtLint[1] = pXApicPage->lvt_lint1.all.u32LvtLint1;
        for (size_t i = 0; i < RT_ELEMENTS(aLvtLint); i++)
        {
            uint32_t const uLvtLint = aLvtLint[i];
            const char             *pszLint           = s_apszLvtLint[i];
            const uint8_t           uMask             = XAPIC_LVT_IS_MASKED(uLvtLint);
            const XAPICTRIGGERMODE  enmTriggerMode    = XAPIC_LVT_GET_TRIGGER_MODE(uLvtLint);
            const char             *pszTriggerMode    = s_apszLvtTriggerModes[enmTriggerMode];
            const uint8_t           uRemoteIrr        = XAPIC_LVT_GET_REMOTE_IRR(uLvtLint);
            const uint8_t           uPolarity         = XAPIC_LVT_GET_POLARITY(uLvtLint);
            const char             *pszPolarity       = s_apszLvtPolarity[uPolarity];
            const uint8_t           uDeliveryStatus   = uLvtLint & XAPIC_LVT_DELIVERY_STATUS;
            const char             *pszDeliveryStatus = s_apszLvtDeliveryStatus[uDeliveryStatus];
            const XAPICDELIVERYMODE enmDeliveryMode   = XAPIC_LVT_GET_DELIVERY_MODE(uLvtLint);
            const char             *pszDeliveryMode   = s_apszLvtDeliveryModes[enmDeliveryMode];
            const uint8_t           uVector           = XAPIC_LVT_GET_VECTOR(uLvtLint);

            pHlp->pfnPrintf(pHlp, "%-7s  %9s  %u     %5s     %u   %8s    %4s     %6s    %3u (%#x)\n",
                            pszLint,
                            pszNotApplicable, /* Timer mode */
                            uMask,
                            pszTriggerMode,
                            uRemoteIrr,
                            pszPolarity,
                            pszDeliveryStatus,
                            pszDeliveryMode,
                            uVector,
                            uVector);
        }
    }

    /* Error. */
    {
        uint32_t const uLvtError = pXApicPage->lvt_thermal.all.u32LvtThermal;
        const uint8_t           uMask             = XAPIC_LVT_IS_MASKED(uLvtError);
        const uint8_t           uDeliveryStatus   = uLvtError & XAPIC_LVT_DELIVERY_STATUS;
        const char             *pszDeliveryStatus = s_apszLvtDeliveryStatus[uDeliveryStatus];
        const XAPICDELIVERYMODE enmDeliveryMode   = XAPIC_LVT_GET_DELIVERY_MODE(uLvtError);
        const char             *pszDeliveryMode   = s_apszLvtDeliveryModes[enmDeliveryMode];
        const uint8_t           uVector           = XAPIC_LVT_GET_VECTOR(uLvtError);

        pHlp->pfnPrintf(pHlp, "%-7s  %9s  %u     %5s     %1s   %8s    %4s     %6s    %3u (%#x)\n",
                        "Error",
                        pszNotApplicable, /* Timer mode */
                        uMask,
                        pszNotApplicable, /* TriggerMode */
                        pszNotApplicable, /* Remote IRR */
                        pszNotApplicable, /* Polarity */
                        pszDeliveryStatus,
                        pszDeliveryMode,
                        uVector,
                        uVector);
    }
}


static void apicR3CommonDbgInfoLvtTimer(PCVMCPUCC pVCpu, PCDBGFINFOHLP pHlp)
{
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    uint32_t const uLvtTimer = pXApicPage->lvt_timer.all.u32LvtTimer;
    pHlp->pfnPrintf(pHlp, "VCPU[%u] Local APIC timer:\n",      pVCpu->idCpu);
    pHlp->pfnPrintf(pHlp, "  ICR              = %#RX32\n",     pXApicPage->timer_icr.u32InitialCount);
    pHlp->pfnPrintf(pHlp, "  CCR              = %#RX32\n",     pXApicPage->timer_ccr.u32CurrentCount);
    pHlp->pfnPrintf(pHlp, "  DCR              = %#RX32\n",     pXApicPage->timer_dcr.all.u32DivideValue);
    pHlp->pfnPrintf(pHlp, "    Timer shift    = %#x\n",        apicCommonGetTimerShift(pXApicPage));
    pHlp->pfnPrintf(pHlp, "LVT Timer          = %#RX32\n",     uLvtTimer);
    pHlp->pfnPrintf(pHlp, "  Vector             = %u (%#x)\n", pXApicPage->lvt_timer.u.u8Vector, pXApicPage->lvt_timer.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery status    = %u\n",       pXApicPage->lvt_timer.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Masked             = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtTimer));
    pHlp->pfnPrintf(pHlp, "  Timer Mode         = %#x (%s)\n", pXApicPage->lvt_timer.u.u2TimerMode,
                    apicR3CommonGetTimerModeName((XAPICTIMERMODE)pXApicPage->lvt_timer.u.u2TimerMode));
}


static void apicR3CommonDbgInfo(PVMCPUCC pVCpu, PCDBGFINFOHLP pHlp, uint64_t uBaseMsr)
{
    PCXAPICPAGE  pXApicPage  = VMCPU_TO_CXAPICPAGE(pVCpu);
    PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpu);

    XAPICMODE const enmMode  = apicCommonGetMode(uBaseMsr);
    bool const   fX2ApicMode = XAPIC_IN_X2APIC_MODE(pVCpu->apic.s.uApicBaseMsr);

    pHlp->pfnPrintf(pHlp, "APIC%u:\n", pVCpu->idCpu);
    pHlp->pfnPrintf(pHlp, "  APIC Base MSR                 = %#RX64 (Addr=%#RX64%s%s%s)\n", uBaseMsr,
                    MSR_IA32_APICBASE_GET_ADDR(uBaseMsr), uBaseMsr & MSR_IA32_APICBASE_EN ? " en" : "",
                    uBaseMsr & MSR_IA32_APICBASE_BSP ? " bsp" : "", uBaseMsr & MSR_IA32_APICBASE_EXTD ? " extd" : "");
    pHlp->pfnPrintf(pHlp, "  Mode                          = %u (%s)\n", enmMode, apicCommonGetModeName(enmMode));
    if (fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "  APIC ID                       = %u (%#x)\n", pX2ApicPage->id.u32ApicId, pX2ApicPage->id.u32ApicId);
    else
        pHlp->pfnPrintf(pHlp, "  APIC ID                       = %u (%#x)\n", pXApicPage->id.u8ApicId, pXApicPage->id.u8ApicId);
    pHlp->pfnPrintf(pHlp, "  Version                       = %#x\n",      pXApicPage->version.all.u32Version);
    pHlp->pfnPrintf(pHlp, "    APIC Version                  = %#x\n",      pXApicPage->version.u.u8Version);
    pHlp->pfnPrintf(pHlp, "    Max LVT entry index (0..N)    = %u\n",       pXApicPage->version.u.u8MaxLvtEntry);
    pHlp->pfnPrintf(pHlp, "    EOI Broadcast supression      = %RTbool\n",  pXApicPage->version.u.fEoiBroadcastSupression);
    if (!fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "  APR                           = %u (%#x)\n", pXApicPage->apr.u8Apr, pXApicPage->apr.u8Apr);
    pHlp->pfnPrintf(pHlp, "  TPR                           = %u (%#x)\n", pXApicPage->tpr.u8Tpr, pXApicPage->tpr.u8Tpr);
    pHlp->pfnPrintf(pHlp, "    Task-priority class           = %#x\n",      XAPIC_TPR_GET_TP(pXApicPage->tpr.u8Tpr) >> 4);
    pHlp->pfnPrintf(pHlp, "    Task-priority subclass        = %#x\n",      XAPIC_TPR_GET_TP_SUBCLASS(pXApicPage->tpr.u8Tpr));
    pHlp->pfnPrintf(pHlp, "  PPR                           = %u (%#x)\n", pXApicPage->ppr.u8Ppr, pXApicPage->ppr.u8Ppr);
    pHlp->pfnPrintf(pHlp, "    Processor-priority class      = %#x\n",      XAPIC_PPR_GET_PP(pXApicPage->ppr.u8Ppr) >> 4);
    pHlp->pfnPrintf(pHlp, "    Processor-priority subclass   = %#x\n",      XAPIC_PPR_GET_PP_SUBCLASS(pXApicPage->ppr.u8Ppr));
    if (!fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "  RRD                           = %u (%#x)\n", pXApicPage->rrd.u32Rrd, pXApicPage->rrd.u32Rrd);
    pHlp->pfnPrintf(pHlp, "  LDR                           = %#x\n",      pXApicPage->ldr.all.u32Ldr);
    pHlp->pfnPrintf(pHlp, "    Logical APIC ID               = %#x\n",      fX2ApicMode ? pX2ApicPage->ldr.u32LogicalApicId
                                                                          : pXApicPage->ldr.u.u8LogicalApicId);
    if (!fX2ApicMode)
    {
        pHlp->pfnPrintf(pHlp, "  DFR                           = %#x\n",  pXApicPage->dfr.all.u32Dfr);
        pHlp->pfnPrintf(pHlp, "    Model                         = %#x (%s)\n", pXApicPage->dfr.u.u4Model,
                        apicR3CommonGetDestFormatName((XAPICDESTFORMAT)pXApicPage->dfr.u.u4Model));
    }
    pHlp->pfnPrintf(pHlp, "  SVR                           = %#x\n", pXApicPage->svr.all.u32Svr);
    pHlp->pfnPrintf(pHlp, "    Vector                        = %u (%#x)\n", pXApicPage->svr.u.u8SpuriousVector,
                                                                          pXApicPage->svr.u.u8SpuriousVector);
    pHlp->pfnPrintf(pHlp, "    Software Enabled              = %RTbool\n",  RT_BOOL(pXApicPage->svr.u.fApicSoftwareEnable));
    pHlp->pfnPrintf(pHlp, "    Supress EOI broadcast         = %RTbool\n",  RT_BOOL(pXApicPage->svr.u.fSupressEoiBroadcast));
    pHlp->pfnPrintf(pHlp, "  ISR\n");
    apicR3CommonDbgInfo256BitReg(&pXApicPage->isr, pHlp);
    pHlp->pfnPrintf(pHlp, "  TMR\n");
    apicR3CommonDbgInfo256BitReg(&pXApicPage->tmr, pHlp);
    pHlp->pfnPrintf(pHlp, "  IRR\n");
    apicR3CommonDbgInfo256BitReg(&pXApicPage->irr, pHlp);
    pHlp->pfnPrintf(pHlp, "  ESR                           = %#x\n",      pXApicPage->esr.all.u32Errors);
    pHlp->pfnPrintf(pHlp, "    Redirectable IPI              = %RTbool\n",  pXApicPage->esr.u.fRedirectableIpi);
    pHlp->pfnPrintf(pHlp, "    Send Illegal Vector           = %RTbool\n",  pXApicPage->esr.u.fSendIllegalVector);
    pHlp->pfnPrintf(pHlp, "    Recv Illegal Vector           = %RTbool\n",  pXApicPage->esr.u.fRcvdIllegalVector);
    pHlp->pfnPrintf(pHlp, "    Illegal Register Address      = %RTbool\n",  pXApicPage->esr.u.fIllegalRegAddr);
    pHlp->pfnPrintf(pHlp, "  ICR Low                       = %#x\n",      pXApicPage->icr_lo.all.u32IcrLo);
    pHlp->pfnPrintf(pHlp, "    Vector                        = %u (%#x)\n", pXApicPage->icr_lo.u.u8Vector,
                                                                            pXApicPage->icr_lo.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "    Delivery Mode                 = %#x (%s)\n", pXApicPage->icr_lo.u.u3DeliveryMode,
                    apicCommonGetDeliveryModeName((XAPICDELIVERYMODE)pXApicPage->icr_lo.u.u3DeliveryMode));
    pHlp->pfnPrintf(pHlp, "    Destination Mode              = %#x (%s)\n", pXApicPage->icr_lo.u.u1DestMode,
                    apicCommonGetDestModeName((XAPICDESTMODE)pXApicPage->icr_lo.u.u1DestMode));
    if (!fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "    Delivery Status               = %u\n",       pXApicPage->icr_lo.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "    Level                         = %u\n",       pXApicPage->icr_lo.u.u1Level);
    pHlp->pfnPrintf(pHlp, "    Trigger Mode                  = %u (%s)\n",  pXApicPage->icr_lo.u.u1TriggerMode,
                    apicCommonGetTriggerModeName((XAPICTRIGGERMODE)pXApicPage->icr_lo.u.u1TriggerMode));
    pHlp->pfnPrintf(pHlp, "    Destination shorthand         = %#x (%s)\n", pXApicPage->icr_lo.u.u2DestShorthand,
                    apicR3CommonGetDestShorthandName((XAPICDESTSHORTHAND)pXApicPage->icr_lo.u.u2DestShorthand));
    pHlp->pfnPrintf(pHlp, "  ICR High                      = %#x\n",      pXApicPage->icr_hi.all.u32IcrHi);
    pHlp->pfnPrintf(pHlp, "    Destination field/mask        = %#x\n",      fX2ApicMode ? pX2ApicPage->icr_hi.u32IcrHi
                                                                         : pXApicPage->icr_hi.u.u8Dest);
}


/*
 * Saved-state related defines (ring-3 only).
 *
 * WARNING!!! The saved-state format since version 5 is identical for the
 * emulated APIC and the NEM Hyper-V APIC on Windows x86 hosts.
 */
/** The current APIC saved state version. */
# define APIC_SAVED_STATE_VERSION                  5
/** VirtualBox 5.1 beta2 - pre fActiveLintX. */
# define APIC_SAVED_STATE_VERSION_VBOX_51_BETA2    4
/** The saved state version used by VirtualBox 5.0 and
 *  earlier.  */
# define APIC_SAVED_STATE_VERSION_VBOX_50          3
/** The saved state version used by VirtualBox v3 and earlier.
 * This does not include the config.  */
# define APIC_SAVED_STATE_VERSION_VBOX_30          2
/** Some ancient version... */
# define APIC_SAVED_STATE_VERSION_ANCIENT          1

/** Saved state field descriptors for XAPICPAGE.
 *  WARNING!!! This is shared between the emulated APIC and the
 *  NEM Hyper-V APIC. */
static const SSMFIELD g_aXApicPageFields[] =
{
    SSMFIELD_ENTRY( XAPICPAGE, id.u8ApicId),
    SSMFIELD_ENTRY( XAPICPAGE, version.all.u32Version),
    SSMFIELD_ENTRY( XAPICPAGE, tpr.u8Tpr),
    SSMFIELD_ENTRY( XAPICPAGE, apr.u8Apr),
    SSMFIELD_ENTRY( XAPICPAGE, ppr.u8Ppr),
    SSMFIELD_ENTRY( XAPICPAGE, ldr.all.u32Ldr),
    SSMFIELD_ENTRY( XAPICPAGE, dfr.all.u32Dfr),
    SSMFIELD_ENTRY( XAPICPAGE, svr.all.u32Svr),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[0].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[1].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[2].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[3].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[4].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[5].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[6].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[7].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[0].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[1].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[2].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[3].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[4].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[5].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[6].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[7].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[0].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[1].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[2].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[3].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[4].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[5].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[6].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[7].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, esr.all.u32Errors),
    SSMFIELD_ENTRY( XAPICPAGE, icr_lo.all.u32IcrLo),
    SSMFIELD_ENTRY( XAPICPAGE, icr_hi.all.u32IcrHi),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_timer.all.u32LvtTimer),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_thermal.all.u32LvtThermal),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_perf.all.u32LvtPerf),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_lint0.all.u32LvtLint0),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_lint1.all.u32LvtLint1),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_error.all.u32LvtError),
    SSMFIELD_ENTRY( XAPICPAGE, timer_icr.u32InitialCount),
    SSMFIELD_ENTRY( XAPICPAGE, timer_ccr.u32CurrentCount),
    SSMFIELD_ENTRY( XAPICPAGE, timer_dcr.all.u32DivideValue),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for X2APICPAGE.
 *  WARNING!!! This is shared between the emulated APIC and the
 *  NEM Hyper-V APIC. */
static const SSMFIELD g_aX2ApicPageFields[] =
{
    SSMFIELD_ENTRY(X2APICPAGE, id.u32ApicId),
    SSMFIELD_ENTRY(X2APICPAGE, version.all.u32Version),
    SSMFIELD_ENTRY(X2APICPAGE, tpr.u8Tpr),
    SSMFIELD_ENTRY(X2APICPAGE, ppr.u8Ppr),
    SSMFIELD_ENTRY(X2APICPAGE, ldr.u32LogicalApicId),
    SSMFIELD_ENTRY(X2APICPAGE, svr.all.u32Svr),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[0].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[1].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[2].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[3].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[4].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[5].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[6].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[7].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[0].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[1].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[2].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[3].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[4].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[5].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[6].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[7].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[0].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[1].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[2].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[3].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[4].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[5].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[6].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[7].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, esr.all.u32Errors),
    SSMFIELD_ENTRY(X2APICPAGE, icr_lo.all.u32IcrLo),
    SSMFIELD_ENTRY(X2APICPAGE, icr_hi.u32IcrHi),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_timer.all.u32LvtTimer),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_thermal.all.u32LvtThermal),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_perf.all.u32LvtPerf),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_lint0.all.u32LvtLint0),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_lint1.all.u32LvtLint1),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_error.all.u32LvtError),
    SSMFIELD_ENTRY(X2APICPAGE, timer_icr.u32InitialCount),
    SSMFIELD_ENTRY(X2APICPAGE, timer_ccr.u32CurrentCount),
    SSMFIELD_ENTRY(X2APICPAGE, timer_dcr.all.u32DivideValue),
    SSMFIELD_ENTRY_TERM()
};
#endif /* IN_RING3 && VMM_APIC_TEMPLATE_R3_COMMON */


#endif /* VMM_INCLUDED_SRC_VMMAll_APICAllCommon_x86_cpp_h */

