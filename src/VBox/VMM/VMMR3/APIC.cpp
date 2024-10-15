/* $Id: APIC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller.
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
#include <VBox/log.h>
#include "APICInternal.h"
#include <VBox/vmm/apic.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vm.h>


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current APIC saved state version. */
#define APIC_SAVED_STATE_VERSION                  5
/** VirtualBox 5.1 beta2 - pre fActiveLintX. */
#define APIC_SAVED_STATE_VERSION_VBOX_51_BETA2    4
/** The saved state version used by VirtualBox 5.0 and
 *  earlier.  */
#define APIC_SAVED_STATE_VERSION_VBOX_50          3
/** The saved state version used by VirtualBox v3 and earlier.
 * This does not include the config.  */
#define APIC_SAVED_STATE_VERSION_VBOX_30          2
/** Some ancient version... */
#define APIC_SAVED_STATE_VERSION_ANCIENT          1

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
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * MSR range supported by the x2APIC.
 * See Intel spec. 10.12.2 "x2APIC Register Availability".
 */
static CPUMMSRRANGE const g_MsrRange_x2Apic = X2APIC_MSRRANGE(MSR_IA32_X2APIC_START, MSR_IA32_X2APIC_END, "x2APIC range");
static CPUMMSRRANGE const g_MsrRange_x2Apic_Invalid = X2APIC_MSRRANGE_INVALID(MSR_IA32_X2APIC_START, MSR_IA32_X2APIC_END, "x2APIC range invalid");
#undef X2APIC_MSRRANGE
#undef X2APIC_MSRRANGE_GP

/** Saved state field descriptors for XAPICPAGE. */
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

/** Saved state field descriptors for X2APICPAGE. */
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


/**
 * Sets the CPUID feature bits for the APIC mode.
 *
 * @param   pVM             The cross context VM structure.
 * @param   enmMode         The APIC mode.
 */
static void apicR3SetCpuIdFeatureLevel(PVM pVM, PDMAPICMODE enmMode)
{
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
 * Receives an INIT IPI.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(void) APICR3InitIpi(PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    LogFlow(("APIC%u: APICR3InitIpi\n", pVCpu->idCpu));
    apicInitIpi(pVCpu);
}


/**
 * Sets whether Hyper-V compatibility mode (MSR interface) is enabled or not.
 *
 * This mode is a hybrid of xAPIC and x2APIC modes, some caveats:
 * 1. MSRs are used even ones that are missing (illegal) in x2APIC like DFR.
 * 2. A single ICR is used by the guest to send IPIs rather than 2 ICR writes.
 * 3. It is unclear what the behaviour will be when invalid bits are set,
 *    currently we follow x2APIC behaviour of causing a \#GP.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   fHyperVCompatMode   Whether the compatibility mode is enabled.
 */
VMMR3_INT_DECL(void) APICR3HvSetCompatMode(PVM pVM, bool fHyperVCompatMode)
{
    Assert(pVM);
    PAPIC pApic = VM_TO_APIC(pVM);
    pApic->fHyperVCompatMode = fHyperVCompatMode;

    if (fHyperVCompatMode)
        LogRel(("APIC: Enabling Hyper-V x2APIC compatibility mode\n"));

    int rc = CPUMR3MsrRangesInsert(pVM, &g_MsrRange_x2Apic);
    AssertLogRelRC(rc);
}


/**
 * Helper for dumping an APIC 256-bit sparse register.
 *
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   pHlp            The debug output helper.
 */
static void apicR3DbgInfo256BitReg(volatile const XAPIC256BITREG *pApicReg, PCDBGFINFOHLP pHlp)
{
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
 * Helper for dumping an APIC pending-interrupt bitmap.
 *
 * @param   pApicPib        The pending-interrupt bitmap.
 * @param   pHlp            The debug output helper.
 */
static void apicR3DbgInfoPib(PCAPICPIB pApicPib, PCDBGFINFOHLP pHlp)
{
    /* Copy the pending-interrupt bitmap as an APIC 256-bit sparse register. */
    XAPIC256BITREG ApicReg;
    RT_ZERO(ApicReg);
    ssize_t const cFragmentsDst = RT_ELEMENTS(ApicReg.u);
    ssize_t const cFragmentsSrc = RT_ELEMENTS(pApicPib->au64VectorBitmap);
    AssertCompile(RT_ELEMENTS(ApicReg.u) == 2 * RT_ELEMENTS(pApicPib->au64VectorBitmap));
    for (ssize_t idxPib = cFragmentsSrc - 1, idxReg = cFragmentsDst - 1; idxPib >= 0; idxPib--, idxReg -= 2)
    {
        uint64_t const uFragment   = pApicPib->au64VectorBitmap[idxPib];
        uint32_t const uFragmentLo = RT_LO_U32(uFragment);
        uint32_t const uFragmentHi = RT_HI_U32(uFragment);
        ApicReg.u[idxReg].u32Reg     = uFragmentHi;
        ApicReg.u[idxReg - 1].u32Reg = uFragmentLo;
    }

    /* Dump it. */
    apicR3DbgInfo256BitReg(&ApicReg, pHlp);
}


/**
 * Dumps basic APIC state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3Info(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    PCAPICCPU    pApicCpu    = VMCPU_TO_APICCPU(pVCpu);
    PCXAPICPAGE  pXApicPage  = VMCPU_TO_CXAPICPAGE(pVCpu);
    PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpu);

    uint64_t const uBaseMsr  = pApicCpu->uApicBaseMsr;
    APICMODE const enmMode   = apicGetMode(uBaseMsr);
    bool const   fX2ApicMode = XAPIC_IN_X2APIC_MODE(pVCpu);

    pHlp->pfnPrintf(pHlp, "APIC%u:\n", pVCpu->idCpu);
    pHlp->pfnPrintf(pHlp, "  APIC Base MSR                 = %#RX64 (Addr=%#RX64%s%s%s)\n", uBaseMsr,
                    MSR_IA32_APICBASE_GET_ADDR(uBaseMsr), uBaseMsr & MSR_IA32_APICBASE_EN ? " en" : "",
                    uBaseMsr & MSR_IA32_APICBASE_BSP ? " bsp" : "", uBaseMsr & MSR_IA32_APICBASE_EXTD ? " extd" : "");
    pHlp->pfnPrintf(pHlp, "  Mode                          = %u (%s)\n", enmMode, apicGetModeName(enmMode));
    if (fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "  APIC ID                       = %u (%#x)\n", pX2ApicPage->id.u32ApicId,
                                                                              pX2ApicPage->id.u32ApicId);
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
                        apicGetDestFormatName((XAPICDESTFORMAT)pXApicPage->dfr.u.u4Model));
    }
    pHlp->pfnPrintf(pHlp, "  SVR                           = %#x\n", pXApicPage->svr.all.u32Svr);
    pHlp->pfnPrintf(pHlp, "    Vector                        = %u (%#x)\n", pXApicPage->svr.u.u8SpuriousVector,
                                                                          pXApicPage->svr.u.u8SpuriousVector);
    pHlp->pfnPrintf(pHlp, "    Software Enabled              = %RTbool\n",  RT_BOOL(pXApicPage->svr.u.fApicSoftwareEnable));
    pHlp->pfnPrintf(pHlp, "    Supress EOI broadcast         = %RTbool\n",  RT_BOOL(pXApicPage->svr.u.fSupressEoiBroadcast));
    pHlp->pfnPrintf(pHlp, "  ISR\n");
    apicR3DbgInfo256BitReg(&pXApicPage->isr, pHlp);
    pHlp->pfnPrintf(pHlp, "  TMR\n");
    apicR3DbgInfo256BitReg(&pXApicPage->tmr, pHlp);
    pHlp->pfnPrintf(pHlp, "  IRR\n");
    apicR3DbgInfo256BitReg(&pXApicPage->irr, pHlp);
    pHlp->pfnPrintf(pHlp, "  PIB\n");
    apicR3DbgInfoPib((PCAPICPIB)pApicCpu->pvApicPibR3, pHlp);
    pHlp->pfnPrintf(pHlp, "  Level PIB\n");
    apicR3DbgInfoPib(&pApicCpu->ApicPibLevel, pHlp);
    pHlp->pfnPrintf(pHlp, "  ESR Internal                  = %#x\n",      pApicCpu->uEsrInternal);
    pHlp->pfnPrintf(pHlp, "  ESR                           = %#x\n",      pXApicPage->esr.all.u32Errors);
    pHlp->pfnPrintf(pHlp, "    Redirectable IPI              = %RTbool\n",  pXApicPage->esr.u.fRedirectableIpi);
    pHlp->pfnPrintf(pHlp, "    Send Illegal Vector           = %RTbool\n",  pXApicPage->esr.u.fSendIllegalVector);
    pHlp->pfnPrintf(pHlp, "    Recv Illegal Vector           = %RTbool\n",  pXApicPage->esr.u.fRcvdIllegalVector);
    pHlp->pfnPrintf(pHlp, "    Illegal Register Address      = %RTbool\n",  pXApicPage->esr.u.fIllegalRegAddr);
    pHlp->pfnPrintf(pHlp, "  ICR Low                       = %#x\n",      pXApicPage->icr_lo.all.u32IcrLo);
    pHlp->pfnPrintf(pHlp, "    Vector                        = %u (%#x)\n", pXApicPage->icr_lo.u.u8Vector,
                                                                            pXApicPage->icr_lo.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "    Delivery Mode                 = %#x (%s)\n", pXApicPage->icr_lo.u.u3DeliveryMode,
                    apicGetDeliveryModeName((XAPICDELIVERYMODE)pXApicPage->icr_lo.u.u3DeliveryMode));
    pHlp->pfnPrintf(pHlp, "    Destination Mode              = %#x (%s)\n", pXApicPage->icr_lo.u.u1DestMode,
                    apicGetDestModeName((XAPICDESTMODE)pXApicPage->icr_lo.u.u1DestMode));
    if (!fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "    Delivery Status               = %u\n",       pXApicPage->icr_lo.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "    Level                         = %u\n",       pXApicPage->icr_lo.u.u1Level);
    pHlp->pfnPrintf(pHlp, "    Trigger Mode                  = %u (%s)\n",  pXApicPage->icr_lo.u.u1TriggerMode,
                    apicGetTriggerModeName((XAPICTRIGGERMODE)pXApicPage->icr_lo.u.u1TriggerMode));
    pHlp->pfnPrintf(pHlp, "    Destination shorthand         = %#x (%s)\n", pXApicPage->icr_lo.u.u2DestShorthand,
                    apicGetDestShorthandName((XAPICDESTSHORTHAND)pXApicPage->icr_lo.u.u2DestShorthand));
    pHlp->pfnPrintf(pHlp, "  ICR High                      = %#x\n",      pXApicPage->icr_hi.all.u32IcrHi);
    pHlp->pfnPrintf(pHlp, "    Destination field/mask        = %#x\n",      fX2ApicMode ? pX2ApicPage->icr_hi.u32IcrHi
                                                                          : pXApicPage->icr_hi.u.u8Dest);
}


/**
 * Helper for dumping the LVT timer.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pHlp    The debug output helper.
 */
static void apicR3InfoLvtTimer(PVMCPU pVCpu, PCDBGFINFOHLP pHlp)
{
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    uint32_t const uLvtTimer = pXApicPage->lvt_timer.all.u32LvtTimer;
    pHlp->pfnPrintf(pHlp, "LVT Timer          = %#RX32\n",   uLvtTimer);
    pHlp->pfnPrintf(pHlp, "  Vector             = %u (%#x)\n", pXApicPage->lvt_timer.u.u8Vector, pXApicPage->lvt_timer.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery status    = %u\n",       pXApicPage->lvt_timer.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Masked             = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtTimer));
    pHlp->pfnPrintf(pHlp, "  Timer Mode         = %#x (%s)\n", pXApicPage->lvt_timer.u.u2TimerMode,
                    apicGetTimerModeName((XAPICTIMERMODE)pXApicPage->lvt_timer.u.u2TimerMode));
}


/**
 * Dumps APIC Local Vector Table (LVT) information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3InfoLvt(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

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


/**
 * Dumps the APIC timer information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) apicR3InfoTimer(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    PCAPICCPU   pApicCpu   = VMCPU_TO_APICCPU(pVCpu);

    pHlp->pfnPrintf(pHlp, "VCPU[%u] Local APIC timer:\n", pVCpu->idCpu);
    pHlp->pfnPrintf(pHlp, "  ICR              = %#RX32\n", pXApicPage->timer_icr.u32InitialCount);
    pHlp->pfnPrintf(pHlp, "  CCR              = %#RX32\n", pXApicPage->timer_ccr.u32CurrentCount);
    pHlp->pfnPrintf(pHlp, "  DCR              = %#RX32\n", pXApicPage->timer_dcr.all.u32DivideValue);
    pHlp->pfnPrintf(pHlp, "    Timer shift    = %#x\n",    apicGetTimerShift(pXApicPage));
    pHlp->pfnPrintf(pHlp, "  Timer initial TS = %#RU64\n", pApicCpu->u64TimerInitial);
    apicR3InfoLvtTimer(pVCpu, pHlp);
}


#ifdef APIC_FUZZY_SSM_COMPAT_TEST

/**
 * Reads a 32-bit register at a specified offset.
 *
 * @returns The value at the specified offset.
 * @param   pXApicPage      The xAPIC page.
 * @param   offReg          The offset of the register being read.
 *
 * @remarks Duplicate of apicReadRaw32()!
 */
static uint32_t apicR3ReadRawR32(PCXAPICPAGE pXApicPage, uint16_t offReg)
{
    Assert(offReg < sizeof(*pXApicPage) - sizeof(uint32_t));
    uint8_t  const *pbXApic =  (const uint8_t *)pXApicPage;
    uint32_t const  uValue  = *(const uint32_t *)(pbXApic + offReg);
    return uValue;
}


/**
 * Helper for dumping per-VCPU APIC state to the release logger.
 *
 * This is primarily concerned about the APIC state relevant for saved-states.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszPrefix   A caller supplied prefix before dumping the state.
 * @param   uVersion    Data layout version.
 */
static void apicR3DumpState(PVMCPU pVCpu, const char *pszPrefix, uint32_t uVersion)
{
    PCAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

    LogRel(("APIC%u: %s (version %u):\n", pVCpu->idCpu, pszPrefix, uVersion));

    switch (uVersion)
    {
        case APIC_SAVED_STATE_VERSION:
        case APIC_SAVED_STATE_VERSION_VBOX_51_BETA2:
        {
            /* The auxiliary state. */
            LogRel(("APIC%u: uApicBaseMsr             = %#RX64\n", pVCpu->idCpu, pApicCpu->uApicBaseMsr));
            LogRel(("APIC%u: uEsrInternal             = %#RX64\n", pVCpu->idCpu, pApicCpu->uEsrInternal));

            /* The timer. */
            LogRel(("APIC%u: u64TimerInitial          = %#RU64\n", pVCpu->idCpu, pApicCpu->u64TimerInitial));
            LogRel(("APIC%u: uHintedTimerInitialCount = %#RU64\n", pVCpu->idCpu, pApicCpu->uHintedTimerInitialCount));
            LogRel(("APIC%u: uHintedTimerShift        = %#RU64\n", pVCpu->idCpu, pApicCpu->uHintedTimerShift));

            PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
            LogRel(("APIC%u: uTimerICR                = %#RX32\n", pVCpu->idCpu, pXApicPage->timer_icr.u32InitialCount));
            LogRel(("APIC%u: uTimerCCR                = %#RX32\n", pVCpu->idCpu, pXApicPage->timer_ccr.u32CurrentCount));

            /* The PIBs. */
            LogRel(("APIC%u: Edge PIB : %.*Rhxs\n", pVCpu->idCpu, sizeof(APICPIB), pApicCpu->pvApicPibR3));
            LogRel(("APIC%u: Level PIB: %.*Rhxs\n", pVCpu->idCpu, sizeof(APICPIB), &pApicCpu->ApicPibLevel));

            /* The LINT0, LINT1 interrupt line active states. */
            LogRel(("APIC%u: fActiveLint0             = %RTbool\n", pVCpu->idCpu, pApicCpu->fActiveLint0));
            LogRel(("APIC%u: fActiveLint1             = %RTbool\n", pVCpu->idCpu, pApicCpu->fActiveLint1));

            /* The APIC page. */
            LogRel(("APIC%u: APIC page: %.*Rhxs\n", pVCpu->idCpu, sizeof(XAPICPAGE), pApicCpu->pvApicPageR3));
            break;
        }

        case APIC_SAVED_STATE_VERSION_VBOX_50:
        case APIC_SAVED_STATE_VERSION_VBOX_30:
        case APIC_SAVED_STATE_VERSION_ANCIENT:
        {
            PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
            LogRel(("APIC%u: uApicBaseMsr             = %#RX32\n", pVCpu->idCpu, RT_LO_U32(pApicCpu->uApicBaseMsr)));
            LogRel(("APIC%u: uId                      = %#RX32\n", pVCpu->idCpu, pXApicPage->id.u8ApicId));
            LogRel(("APIC%u: uPhysId                  = N/A\n",    pVCpu->idCpu));
            LogRel(("APIC%u: uArbId                   = N/A\n",    pVCpu->idCpu));
            LogRel(("APIC%u: uTpr                     = %#RX32\n", pVCpu->idCpu, pXApicPage->tpr.u8Tpr));
            LogRel(("APIC%u: uSvr                     = %#RX32\n", pVCpu->idCpu, pXApicPage->svr.all.u32Svr));
            LogRel(("APIC%u: uLdr                     = %#x\n",    pVCpu->idCpu, pXApicPage->ldr.all.u32Ldr));
            LogRel(("APIC%u: uDfr                     = %#x\n",    pVCpu->idCpu, pXApicPage->dfr.all.u32Dfr));

            for (size_t i = 0; i < 8; i++)
            {
                LogRel(("APIC%u: Isr[%u].u32Reg            = %#RX32\n", pVCpu->idCpu, i, pXApicPage->isr.u[i].u32Reg));
                LogRel(("APIC%u: Tmr[%u].u32Reg            = %#RX32\n", pVCpu->idCpu, i, pXApicPage->tmr.u[i].u32Reg));
                LogRel(("APIC%u: Irr[%u].u32Reg            = %#RX32\n", pVCpu->idCpu, i, pXApicPage->irr.u[i].u32Reg));
            }

            for (size_t i = 0; i < XAPIC_MAX_LVT_ENTRIES_P4; i++)
            {
                uint16_t const offReg = XAPIC_OFF_LVT_START + (i << 4);
                LogRel(("APIC%u: Lvt[%u].u32Reg            = %#RX32\n", pVCpu->idCpu, i, apicR3ReadRawR32(pXApicPage, offReg)));
            }

            LogRel(("APIC%u: uEsr                     = %#RX32\n", pVCpu->idCpu, pXApicPage->esr.all.u32Errors));
            LogRel(("APIC%u: uIcr_Lo                  = %#RX32\n", pVCpu->idCpu, pXApicPage->icr_lo.all.u32IcrLo));
            LogRel(("APIC%u: uIcr_Hi                  = %#RX32\n", pVCpu->idCpu, pXApicPage->icr_hi.all.u32IcrHi));
            LogRel(("APIC%u: uTimerDcr                = %#RX32\n", pVCpu->idCpu, pXApicPage->timer_dcr.all.u32DivideValue));
            LogRel(("APIC%u: uCountShift              = %#RX32\n", pVCpu->idCpu, apicGetTimerShift(pXApicPage)));
            LogRel(("APIC%u: uInitialCount            = %#RX32\n", pVCpu->idCpu, pXApicPage->timer_icr.u32InitialCount));
            LogRel(("APIC%u: u64InitialCountLoadTime  = %#RX64\n", pVCpu->idCpu, pApicCpu->u64TimerInitial));
            LogRel(("APIC%u: u64NextTime / TimerCCR   = %#RX64\n", pVCpu->idCpu, pXApicPage->timer_ccr.u32CurrentCount));
            break;
        }

        default:
        {
            LogRel(("APIC: apicR3DumpState: Invalid/unrecognized saved-state version %u (%#x)\n", uVersion, uVersion));
            break;
        }
    }
}

#endif  /* APIC_FUZZY_SSM_COMPAT_TEST */

/**
 * Worker for saving per-VM APIC data.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVM     The cross context VM structure.
 * @param   pSSM    The SSM handle.
 */
static int apicR3SaveVMData(PPDMDEVINS pDevIns, PVM pVM, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    PAPIC           pApic = VM_TO_APIC(pVM);
    pHlp->pfnSSMPutU32(pSSM,  pVM->cCpus);
    pHlp->pfnSSMPutBool(pSSM, pApic->fIoApicPresent);
    return pHlp->pfnSSMPutU32(pSSM, pApic->enmMaxMode);
}


/**
 * Worker for loading per-VM APIC data.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVM     The cross context VM structure.
 * @param   pSSM    The SSM handle.
 */
static int apicR3LoadVMData(PPDMDEVINS pDevIns, PVM pVM, PSSMHANDLE pSSM)
{
    PAPIC           pApic = VM_TO_APIC(pVM);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    /* Load and verify number of CPUs. */
    uint32_t cCpus;
    int rc = pHlp->pfnSSMGetU32(pSSM, &cCpus);
    AssertRCReturn(rc, rc);
    if (cCpus != pVM->cCpus)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - cCpus: saved=%u config=%u"), cCpus, pVM->cCpus);

    /* Load and verify I/O APIC presence. */
    bool fIoApicPresent;
    rc = pHlp->pfnSSMGetBool(pSSM, &fIoApicPresent);
    AssertRCReturn(rc, rc);
    if (fIoApicPresent != pApic->fIoApicPresent)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fIoApicPresent: saved=%RTbool config=%RTbool"),
                                       fIoApicPresent, pApic->fIoApicPresent);

    /* Load and verify configured max APIC mode. */
    uint32_t uSavedMaxApicMode;
    rc = pHlp->pfnSSMGetU32(pSSM, &uSavedMaxApicMode);
    AssertRCReturn(rc, rc);
    if (uSavedMaxApicMode != (uint32_t)pApic->enmMaxMode)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - uApicMode: saved=%u config=%u"),
                                       uSavedMaxApicMode, pApic->enmMaxMode);
    return VINF_SUCCESS;
}


/**
 * Worker for loading per-VCPU APIC data for legacy (old) saved-states.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pSSM        The SSM handle.
 * @param   uVersion    Data layout version.
 */
static int apicR3LoadLegacyVCpuData(PPDMDEVINS pDevIns, PVMCPU pVCpu, PSSMHANDLE pSSM, uint32_t uVersion)
{
    AssertReturn(uVersion <= APIC_SAVED_STATE_VERSION_VBOX_50, VERR_NOT_SUPPORTED);

    PCPDMDEVHLPR3   pHlp       = pDevIns->pHlpR3;
    PAPICCPU        pApicCpu   = VMCPU_TO_APICCPU(pVCpu);
    PXAPICPAGE      pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    uint32_t uApicBaseLo;
    int rc = pHlp->pfnSSMGetU32(pSSM, &uApicBaseLo);
    AssertRCReturn(rc, rc);
    pApicCpu->uApicBaseMsr = uApicBaseLo;
    Log2(("APIC%u: apicR3LoadLegacyVCpuData: uApicBaseMsr=%#RX64\n", pVCpu->idCpu, pApicCpu->uApicBaseMsr));

    switch (uVersion)
    {
        case APIC_SAVED_STATE_VERSION_VBOX_50:
        case APIC_SAVED_STATE_VERSION_VBOX_30:
        {
            uint32_t uApicId, uPhysApicId, uArbId;
            pHlp->pfnSSMGetU32(pSSM, &uApicId);      pXApicPage->id.u8ApicId = uApicId;
            pHlp->pfnSSMGetU32(pSSM, &uPhysApicId);  NOREF(uPhysApicId); /* PhysId == pVCpu->idCpu */
            pHlp->pfnSSMGetU32(pSSM, &uArbId);       NOREF(uArbId);      /* ArbID is & was unused. */
            break;
        }

        case APIC_SAVED_STATE_VERSION_ANCIENT:
        {
            uint8_t uPhysApicId;
            pHlp->pfnSSMGetU8(pSSM, &pXApicPage->id.u8ApicId);
            pHlp->pfnSSMGetU8(pSSM, &uPhysApicId);   NOREF(uPhysApicId); /* PhysId == pVCpu->idCpu */
            break;
        }

        default:
            return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    uint32_t u32Tpr;
    pHlp->pfnSSMGetU32(pSSM, &u32Tpr);
    pXApicPage->tpr.u8Tpr = u32Tpr & XAPIC_TPR_VALID;

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->svr.all.u32Svr);
    pHlp->pfnSSMGetU8(pSSM,  &pXApicPage->ldr.u.u8LogicalApicId);

    uint8_t uDfr;
    pHlp->pfnSSMGetU8(pSSM,  &uDfr);
    pXApicPage->dfr.u.u4Model = uDfr >> 4;

    AssertCompile(RT_ELEMENTS(pXApicPage->isr.u) == 8);
    AssertCompile(RT_ELEMENTS(pXApicPage->tmr.u) == 8);
    AssertCompile(RT_ELEMENTS(pXApicPage->irr.u) == 8);
    for (size_t i = 0; i < 8; i++)
    {
        pHlp->pfnSSMGetU32(pSSM, &pXApicPage->isr.u[i].u32Reg);
        pHlp->pfnSSMGetU32(pSSM, &pXApicPage->tmr.u[i].u32Reg);
        pHlp->pfnSSMGetU32(pSSM, &pXApicPage->irr.u[i].u32Reg);
    }

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_timer.all.u32LvtTimer);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_thermal.all.u32LvtThermal);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_perf.all.u32LvtPerf);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_lint0.all.u32LvtLint0);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_lint1.all.u32LvtLint1);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->lvt_error.all.u32LvtError);

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->esr.all.u32Errors);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->icr_lo.all.u32IcrLo);
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->icr_hi.all.u32IcrHi);

    uint32_t u32TimerShift;
    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->timer_dcr.all.u32DivideValue);
    pHlp->pfnSSMGetU32(pSSM, &u32TimerShift);
    /*
     * Old implementation may have left the timer shift uninitialized until
     * the timer configuration register was written. Unfortunately zero is
     * also a valid timer shift value, so we're just going to ignore it
     * completely. The shift count can always be derived from the DCR.
     * See @bugref{8245#c98}.
     */
    uint8_t const uTimerShift = apicGetTimerShift(pXApicPage);

    pHlp->pfnSSMGetU32(pSSM, &pXApicPage->timer_icr.u32InitialCount);
    pHlp->pfnSSMGetU64(pSSM, &pApicCpu->u64TimerInitial);
    uint64_t uNextTS;
    rc = pHlp->pfnSSMGetU64(pSSM, &uNextTS);       AssertRCReturn(rc, rc);
    if (uNextTS >= pApicCpu->u64TimerInitial + ((pXApicPage->timer_icr.u32InitialCount + 1) << uTimerShift))
        pXApicPage->timer_ccr.u32CurrentCount = pXApicPage->timer_icr.u32InitialCount;

    rc = PDMDevHlpTimerLoad(pDevIns, pApicCpu->hTimer, pSSM);
    AssertRCReturn(rc, rc);
    Assert(pApicCpu->uHintedTimerInitialCount == 0);
    Assert(pApicCpu->uHintedTimerShift == 0);
    if (PDMDevHlpTimerIsActive(pDevIns, pApicCpu->hTimer))
    {
        uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
        apicHintTimerFreq(pDevIns, pApicCpu, uInitialCount, uTimerShift);
    }

    return rc;
}


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) apicR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVM             pVM  = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);

    LogFlow(("APIC: apicR3SaveExec\n"));

    /* Save per-VM data. */
    int rc = apicR3SaveVMData(pDevIns, pVM, pSSM);
    AssertRCReturn(rc, rc);

    /* Save per-VCPU data.*/
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        PCAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

        /* Update interrupts from the pending-interrupts bitmaps to the IRR. */
        APICUpdatePendingInterrupts(pVCpu);

        /* Save the auxiliary data. */
        pHlp->pfnSSMPutU64(pSSM, pApicCpu->uApicBaseMsr);
        pHlp->pfnSSMPutU32(pSSM, pApicCpu->uEsrInternal);

        /* Save the APIC page. */
        if (XAPIC_IN_X2APIC_MODE(pVCpu))
            pHlp->pfnSSMPutStruct(pSSM, (const void *)pApicCpu->pvApicPageR3, &g_aX2ApicPageFields[0]);
        else
            pHlp->pfnSSMPutStruct(pSSM, (const void *)pApicCpu->pvApicPageR3, &g_aXApicPageFields[0]);

        /* Save the timer. */
        pHlp->pfnSSMPutU64(pSSM, pApicCpu->u64TimerInitial);
        PDMDevHlpTimerSave(pDevIns, pApicCpu->hTimer, pSSM);

        /* Save the LINT0, LINT1 interrupt line states. */
        pHlp->pfnSSMPutBool(pSSM, pApicCpu->fActiveLint0);
        pHlp->pfnSSMPutBool(pSSM, pApicCpu->fActiveLint1);

#if defined(APIC_FUZZY_SSM_COMPAT_TEST) || defined(DEBUG_ramshankar)
        apicR3DumpState(pVCpu, "Saved state", APIC_SAVED_STATE_VERSION);
#endif
    }

#ifdef APIC_FUZZY_SSM_COMPAT_TEST
    /* The state is fuzzy, don't even bother trying to load the guest. */
    return VERR_INVALID_STATE;
#else
    return rc;
#endif
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) apicR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVM             pVM  = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(uPass == SSM_PASS_FINAL, VERR_WRONG_ORDER);

    LogFlow(("APIC: apicR3LoadExec: uVersion=%u uPass=%#x\n", uVersion, uPass));

    /* Weed out invalid versions. */
    if (   uVersion != APIC_SAVED_STATE_VERSION
        && uVersion != APIC_SAVED_STATE_VERSION_VBOX_51_BETA2
        && uVersion != APIC_SAVED_STATE_VERSION_VBOX_50
        && uVersion != APIC_SAVED_STATE_VERSION_VBOX_30
        && uVersion != APIC_SAVED_STATE_VERSION_ANCIENT)
    {
        LogRel(("APIC: apicR3LoadExec: Invalid/unrecognized saved-state version %u (%#x)\n", uVersion, uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    int rc = VINF_SUCCESS;
    if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_30)
    {
        rc = apicR3LoadVMData(pDevIns, pVM, pSSM);
        AssertRCReturn(rc, rc);

        if (uVersion == APIC_SAVED_STATE_VERSION)
        { /* Load any new additional per-VM data. */ }
    }

    /*
     * Restore per CPU state.
     *
     * Note! PDM will restore the VMCPU_FF_INTERRUPT_APIC flag for us.
     *       This code doesn't touch it.  No devices should make us touch
     *       it later during the restore either, only during the 'done' phase.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu    = pVM->apCpusR3[idCpu];
        PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

        if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_50)
        {
            /* Load the auxiliary data. */
            pHlp->pfnSSMGetU64V(pSSM, &pApicCpu->uApicBaseMsr);
            pHlp->pfnSSMGetU32(pSSM, &pApicCpu->uEsrInternal);

            /* Load the APIC page. */
            if (XAPIC_IN_X2APIC_MODE(pVCpu))
                pHlp->pfnSSMGetStruct(pSSM, pApicCpu->pvApicPageR3, &g_aX2ApicPageFields[0]);
            else
                pHlp->pfnSSMGetStruct(pSSM, pApicCpu->pvApicPageR3, &g_aXApicPageFields[0]);

            /* Load the timer. */
            rc = pHlp->pfnSSMGetU64(pSSM, &pApicCpu->u64TimerInitial);     AssertRCReturn(rc, rc);
            rc = PDMDevHlpTimerLoad(pDevIns, pApicCpu->hTimer, pSSM);      AssertRCReturn(rc, rc);
            Assert(pApicCpu->uHintedTimerShift == 0);
            Assert(pApicCpu->uHintedTimerInitialCount == 0);
            if (PDMDevHlpTimerIsActive(pDevIns, pApicCpu->hTimer))
            {
                PCXAPICPAGE    pXApicPage    = VMCPU_TO_CXAPICPAGE(pVCpu);
                uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
                uint8_t const  uTimerShift   = apicGetTimerShift(pXApicPage);
                apicHintTimerFreq(pDevIns, pApicCpu, uInitialCount, uTimerShift);
            }

            /* Load the LINT0, LINT1 interrupt line states. */
            if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_51_BETA2)
            {
                pHlp->pfnSSMGetBoolV(pSSM, &pApicCpu->fActiveLint0);
                pHlp->pfnSSMGetBoolV(pSSM, &pApicCpu->fActiveLint1);
            }
        }
        else
        {
            rc = apicR3LoadLegacyVCpuData(pDevIns, pVCpu, pSSM, uVersion);
            AssertRCReturn(rc, rc);
        }

        /*
         * Check that we're still good wrt restored data, then tell CPUM about the current CPUID[1].EDX[9] visibility.
         */
        rc = pHlp->pfnSSMHandleGetStatus(pSSM);
        AssertRCReturn(rc, rc);
        CPUMSetGuestCpuIdPerCpuApicFeature(pVCpu, RT_BOOL(pApicCpu->uApicBaseMsr & MSR_IA32_APICBASE_EN));

#if defined(APIC_FUZZY_SSM_COMPAT_TEST) || defined(DEBUG_ramshankar)
        apicR3DumpState(pVCpu, "Loaded state", uVersion);
#endif
    }

    return rc;
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
static DECLCALLBACK(void) apicR3TimerCallback(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PVMCPU      pVCpu    = (PVMCPU)pvUser;
    PAPICCPU    pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pApicCpu->hTimer));
    Assert(pVCpu);
    LogFlow(("APIC%u: apicR3TimerCallback\n", pVCpu->idCpu));
    RT_NOREF(pDevIns, hTimer, pApicCpu);

    PXAPICPAGE     pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    uint32_t const uLvtTimer  = pXApicPage->lvt_timer.all.u32LvtTimer;
#ifdef VBOX_WITH_STATISTICS
    STAM_COUNTER_INC(&pApicCpu->StatTimerCallback);
#endif
    if (!XAPIC_LVT_IS_MASKED(uLvtTimer))
    {
        uint8_t uVector = XAPIC_LVT_GET_VECTOR(uLvtTimer);
        Log2(("APIC%u: apicR3TimerCallback: Raising timer interrupt. uVector=%#x\n", pVCpu->idCpu, uVector));
        apicPostInterrupt(pVCpu, uVector, XAPICTRIGGERMODE_EDGE, 0 /* uSrcTag */);
    }

    XAPICTIMERMODE enmTimerMode = XAPIC_LVT_GET_TIMER_MODE(uLvtTimer);
    switch (enmTimerMode)
    {
        case XAPICTIMERMODE_PERIODIC:
        {
            /* The initial-count register determines if the periodic timer is re-armed. */
            uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
            pXApicPage->timer_ccr.u32CurrentCount = uInitialCount;
            if (uInitialCount)
            {
                Log2(("APIC%u: apicR3TimerCallback: Re-arming timer. uInitialCount=%#RX32\n", pVCpu->idCpu, uInitialCount));
                apicStartTimer(pVCpu, uInitialCount);
            }
            break;
        }

        case XAPICTIMERMODE_ONESHOT:
        {
            pXApicPage->timer_ccr.u32CurrentCount = 0;
            break;
        }

        case XAPICTIMERMODE_TSC_DEADLINE:
        {
            /** @todo implement TSC deadline. */
            AssertMsgFailed(("APIC: TSC deadline mode unimplemented\n"));
            break;
        }
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
DECLCALLBACK(void) apicR3Reset(PPDMDEVINS pDevIns)
{
    PVM pVM = PDMDevHlpGetVM(pDevIns);
    VM_ASSERT_EMT0(pVM);
    VM_ASSERT_IS_NOT_RUNNING(pVM);

    LogFlow(("APIC: apicR3Reset\n"));

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpuDest = pVM->apCpusR3[idCpu];
        PAPICCPU pApicCpu  = VMCPU_TO_APICCPU(pVCpuDest);

        if (PDMDevHlpTimerIsActive(pDevIns, pApicCpu->hTimer))
            PDMDevHlpTimerStop(pDevIns, pApicCpu->hTimer);

        apicResetCpu(pVCpuDest, true /* fResetApicBaseMsr */);

        /* Clear the interrupt pending force flag. */
        apicClearInterruptFF(pVCpuDest, PDMAPICIRQ_HARDWARE);
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
DECLCALLBACK(void) apicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    RT_NOREF(pDevIns, offDelta);
}


/**
 * Terminates the APIC state.
 *
 * @param   pVM     The cross context VM structure.
 */
static void apicR3TermState(PVM pVM)
{
    PAPIC pApic = VM_TO_APIC(pVM);
    LogFlow(("APIC: apicR3TermState: pVM=%p\n", pVM));

    /* Unmap and free the PIB. */
    if (pApic->pvApicPibR3 != NIL_RTR3PTR)
    {
        size_t const cPages = pApic->cbApicPib >> HOST_PAGE_SHIFT;
        if (cPages == 1)
            SUPR3PageFreeEx(pApic->pvApicPibR3, cPages);
        else
            SUPR3ContFree(pApic->pvApicPibR3, cPages);
        pApic->pvApicPibR3 = NIL_RTR3PTR;
        pApic->pvApicPibR0 = NIL_RTR0PTR;
    }

    /* Unmap and free the virtual-APIC pages. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu    = pVM->apCpusR3[idCpu];
        PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

        pApicCpu->pvApicPibR3 = NIL_RTR3PTR;
        pApicCpu->pvApicPibR0 = NIL_RTR0PTR;

        if (pApicCpu->pvApicPageR3 != NIL_RTR3PTR)
        {
            SUPR3PageFreeEx(pApicCpu->pvApicPageR3, 1 /* cPages */);
            pApicCpu->pvApicPageR3 = NIL_RTR3PTR;
            pApicCpu->pvApicPageR0 = NIL_RTR0PTR;
        }
    }
}


/**
 * Initializes the APIC state.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
static int apicR3InitState(PVM pVM)
{
    PAPIC pApic = VM_TO_APIC(pVM);
    LogFlow(("APIC: apicR3InitState: pVM=%p\n", pVM));

    /*
     * Allocate and map the pending-interrupt bitmap (PIB).
     *
     * We allocate all the VCPUs' PIBs contiguously in order to save space as
     * physically contiguous allocations are rounded to a multiple of page size.
     */
    Assert(pApic->pvApicPibR3 == NIL_RTR3PTR);
    Assert(pApic->pvApicPibR0 == NIL_RTR0PTR);
    pApic->cbApicPib        = RT_ALIGN_Z(pVM->cCpus * sizeof(APICPIB), HOST_PAGE_SIZE);
    size_t const cHostPages = pApic->cbApicPib >> HOST_PAGE_SHIFT;
    if (cHostPages == 1)
    {
        SUPPAGE SupApicPib;
        RT_ZERO(SupApicPib);
        SupApicPib.Phys = NIL_RTHCPHYS;
        int rc = SUPR3PageAllocEx(1 /* cHostPages */, 0 /* fFlags */, &pApic->pvApicPibR3, &pApic->pvApicPibR0, &SupApicPib);
        if (RT_SUCCESS(rc))
        {
            pApic->HCPhysApicPib = SupApicPib.Phys;
            AssertLogRelReturn(pApic->pvApicPibR3, VERR_INTERNAL_ERROR);
        }
        else
        {
            LogRel(("APIC: Failed to allocate %u bytes for the pending-interrupt bitmap, rc=%Rrc\n", pApic->cbApicPib, rc));
            return rc;
        }
    }
    else
        pApic->pvApicPibR3 = SUPR3ContAlloc(cHostPages, &pApic->pvApicPibR0, &pApic->HCPhysApicPib);

    if (pApic->pvApicPibR3)
    {
        bool const fDriverless = SUPR3IsDriverless();
        AssertLogRelReturn(pApic->pvApicPibR0   != NIL_RTR0PTR  || fDriverless,  VERR_INTERNAL_ERROR);
        AssertLogRelReturn(pApic->HCPhysApicPib != NIL_RTHCPHYS || fDriverless, VERR_INTERNAL_ERROR);

        /* Initialize the PIB. */
        RT_BZERO(pApic->pvApicPibR3, pApic->cbApicPib);

        /*
         * Allocate the map the virtual-APIC pages.
         */
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU   pVCpu    = pVM->apCpusR3[idCpu];
            PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

            SUPPAGE SupApicPage;
            RT_ZERO(SupApicPage);
            SupApicPage.Phys = NIL_RTHCPHYS;

            Assert(pVCpu->idCpu == idCpu);
            Assert(pApicCpu->pvApicPageR3 == NIL_RTR3PTR);
            Assert(pApicCpu->pvApicPageR0 == NIL_RTR0PTR);
            AssertCompile(sizeof(XAPICPAGE) <= HOST_PAGE_SIZE);
            pApicCpu->cbApicPage = sizeof(XAPICPAGE);
            int rc = SUPR3PageAllocEx(1 /* cHostPages */, 0 /* fFlags */, &pApicCpu->pvApicPageR3, &pApicCpu->pvApicPageR0,
                                      &SupApicPage);
            if (RT_SUCCESS(rc))
            {
                AssertLogRelReturn(pApicCpu->pvApicPageR3   != NIL_RTR3PTR  || fDriverless, VERR_INTERNAL_ERROR);
                pApicCpu->HCPhysApicPage = SupApicPage.Phys;
                AssertLogRelReturn(pApicCpu->HCPhysApicPage != NIL_RTHCPHYS || fDriverless, VERR_INTERNAL_ERROR);

                /* Associate the per-VCPU PIB pointers to the per-VM PIB mapping. */
                uint32_t const offApicPib  = idCpu * sizeof(APICPIB);
                pApicCpu->pvApicPibR0      = !fDriverless ? (RTR0PTR)((RTR0UINTPTR)pApic->pvApicPibR0 + offApicPib) : NIL_RTR0PTR;
                pApicCpu->pvApicPibR3      = (RTR3PTR)((RTR3UINTPTR)pApic->pvApicPibR3 + offApicPib);

                /* Initialize the virtual-APIC state. */
                RT_BZERO(pApicCpu->pvApicPageR3, pApicCpu->cbApicPage);
                apicResetCpu(pVCpu, true /* fResetApicBaseMsr */);

#ifdef DEBUG_ramshankar
                Assert(pApicCpu->pvApicPibR3 != NIL_RTR3PTR);
                Assert(pApicCpu->pvApicPibR0 != NIL_RTR0PTR || fDriverless);
                Assert(pApicCpu->pvApicPageR3 != NIL_RTR3PTR);
#endif
            }
            else
            {
                LogRel(("APIC%u: Failed to allocate %u bytes for the virtual-APIC page, rc=%Rrc\n", idCpu, pApicCpu->cbApicPage, rc));
                apicR3TermState(pVM);
                return rc;
            }
        }

#ifdef DEBUG_ramshankar
        Assert(pApic->pvApicPibR3 != NIL_RTR3PTR);
        Assert(pApic->pvApicPibR0 != NIL_RTR0PTR || fDriverless);
#endif
        return VINF_SUCCESS;
    }

    LogRel(("APIC: Failed to allocate %u bytes of physically contiguous memory for the pending-interrupt bitmap\n",
            pApic->cbApicPib));
    return VERR_NO_MEMORY;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
DECLCALLBACK(int) apicR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PVM pVM = PDMDevHlpGetVM(pDevIns);
    LogFlow(("APIC: apicR3Destruct: pVM=%p\n", pVM));

    apicR3TermState(pVM);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnInitComplete}
 */
DECLCALLBACK(int) apicR3InitComplete(PPDMDEVINS pDevIns)
{
    PVM   pVM   = PDMDevHlpGetVM(pDevIns);
    PAPIC pApic = VM_TO_APIC(pVM);

    /*
     * Init APIC settings that rely on HM and CPUM configurations.
     */
    CPUMCPUIDLEAF CpuLeaf;
    int rc = CPUMR3CpuIdGetLeaf(pVM, &CpuLeaf, 1, 0);
    AssertRCReturn(rc, rc);

    pApic->fSupportsTscDeadline = RT_BOOL(CpuLeaf.uEcx & X86_CPUID_FEATURE_ECX_TSCDEADL);
    pApic->fPostedIntrsEnabled  = HMR3IsPostedIntrsEnabled(pVM->pUVM);
    pApic->fVirtApicRegsEnabled = HMR3AreVirtApicRegsEnabled(pVM->pUVM);

    LogRel(("APIC: fPostedIntrsEnabled=%RTbool fVirtApicRegsEnabled=%RTbool fSupportsTscDeadline=%RTbool\n",
            pApic->fPostedIntrsEnabled, pApic->fVirtApicRegsEnabled, pApic->fSupportsTscDeadline));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
DECLCALLBACK(int) apicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PAPICDEV        pApicDev = PDMDEVINS_2_DATA(pDevIns, PAPICDEV);
    PCPDMDEVHLPR3   pHlp     = pDevIns->pHlpR3;
    PVM             pVM      = PDMDevHlpGetVM(pDevIns);
    PAPIC           pApic    = VM_TO_APIC(pVM);
    Assert(iInstance == 0); NOREF(iInstance);

    /*
     * Init the data.
     */
    pApic->pDevInsR3    = pDevIns;
    pApic->fR0Enabled   = pDevIns->fR0Enabled;
    pApic->fRCEnabled   = pDevIns->fRCEnabled;

    /*
     * Validate APIC settings.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Mode|IOAPIC|NumCPUs|MacOSWorkaround", "");

    /** @devcfgm{apic, IOAPIC, bool, true}
     * Indicates whether an I/O APIC is present in the system. */
    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IOAPIC", &pApic->fIoApicPresent, true);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{apic, Mode, PDMAPICMODE, APIC(2)}
     * Max APIC feature level. */
    uint8_t uMaxMode;
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Mode", &uMaxMode, PDMAPICMODE_APIC);
    AssertLogRelRCReturn(rc, rc);
    switch ((PDMAPICMODE)uMaxMode)
    {
        case PDMAPICMODE_NONE:
            LogRel(("APIC: APIC maximum mode configured as 'None', effectively disabled/not-present!\n"));
        case PDMAPICMODE_APIC:
        case PDMAPICMODE_X2APIC:
            break;
        default:
            return VMR3SetError(pVM->pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS, "APIC mode %d unknown.", uMaxMode);
    }
    pApic->enmMaxMode = (PDMAPICMODE)uMaxMode;

    /** @devcfgm{apic, MacOSWorkaround, bool, false}
     * Enables a workaround for incorrect MSR_IA32_X2APIC_ID handling in macOS.
     *
     * Vital code in osfmk/i386/i386_init.c's vstart() routine incorrectly applies a
     * 24 right shift to the ID register value (correct for legacy APIC, but
     * entirely wrong for x2APIC), with the consequence that all CPUs use the same
     * per-cpu data and things panic pretty quickly.   There are some shifty ID
     * reads in lapic_native.c too, but they are for either harmless (assuming boot
     * CPU has ID 0) or are for logging/debugging purposes only. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "MacOSWorkaround", &pApic->fMacOSWorkaround, false);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Disable automatic PDM locking for this device.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register the APIC with PDM.
     */
    rc = PDMDevHlpApicRegister(pDevIns);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Initialize the APIC state.
     */
    if (pApic->enmMaxMode == PDMAPICMODE_X2APIC)
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
    apicR3SetCpuIdFeatureLevel(pVM, pApic->enmMaxMode);

    /* Finally, initialize the state. */
    rc = apicR3InitState(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Register the MMIO range.
     */
    PAPICCPU pApicCpu0 = VMCPU_TO_APICCPU(pVM->apCpusR3[0]);
    RTGCPHYS GCPhysApicBase = MSR_IA32_APICBASE_GET_ADDR(pApicCpu0->uApicBaseMsr);

    rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysApicBase, sizeof(XAPICPAGE), apicWriteMmio, apicReadMmio,
                                   IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "APIC", &pApicDev->hMmio);
    AssertRCReturn(rc, rc);

    /*
     * Create the APIC timers.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu    = pVM->apCpusR3[idCpu];
        PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
        RTStrPrintf(&pApicCpu->szTimerDesc[0], sizeof(pApicCpu->szTimerDesc), "APIC Timer %u", pVCpu->idCpu);
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, apicR3TimerCallback, pVCpu,
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, pApicCpu->szTimerDesc, &pApicCpu->hTimer);
        AssertRCReturn(rc, rc);
    }

    /*
     * Register saved state callbacks.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, APIC_SAVED_STATE_VERSION, sizeof(*pApicDev), apicR3SaveExec, apicR3LoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger info callbacks.
     *
     * We use separate callbacks rather than arguments so they can also be
     * dumped in an automated fashion while collecting crash diagnostics and
     * not just used during live debugging via the VM debugger.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "apic",      "Dumps APIC basic information.", apicR3Info,      DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "apiclvt",   "Dumps APIC LVT information.",   apicR3InfoLvt,   DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "apictimer", "Dumps APIC timer information.", apicR3InfoTimer, DBGFINFO_FLAGS_ALL_EMTS);

    /*
     * Statistics.
     */
#define APIC_REG_COUNTER(a_pvReg, a_pszNameFmt, a_pszDesc) \
        PDMDevHlpSTAMRegisterF(pDevIns, a_pvReg, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, \
                               STAMUNIT_OCCURENCES, a_pszDesc, a_pszNameFmt, idCpu)
#define APIC_PROF_COUNTER(a_pvReg, a_pszNameFmt, a_pszDesc) \
        PDMDevHlpSTAMRegisterF(pDevIns, a_pvReg, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, \
                               STAMUNIT_TICKS_PER_CALL, a_pszDesc, a_pszNameFmt, idCpu)

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu     = pVM->apCpusR3[idCpu];
        PAPICCPU pApicCpu  = VMCPU_TO_APICCPU(pVCpu);

        APIC_REG_COUNTER(&pApicCpu->StatPostIntrCnt,   "%u",  "APIC/VCPU stats / number of apicPostInterrupt calls.");
        for (size_t i = 0; i < RT_ELEMENTS(pApicCpu->aStatVectors); i++)
            PDMDevHlpSTAMRegisterF(pDevIns, &pApicCpu->aStatVectors[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                   "Number of APICPostInterrupt calls for the vector.", "%u/Vectors/%02x", idCpu, i);

#ifdef VBOX_WITH_STATISTICS
        APIC_REG_COUNTER(&pApicCpu->StatMmioReadRZ,    "%u/RZ/MmioRead",    "Number of APIC MMIO reads in RZ.");
        APIC_REG_COUNTER(&pApicCpu->StatMmioWriteRZ,   "%u/RZ/MmioWrite",   "Number of APIC MMIO writes in RZ.");
        APIC_REG_COUNTER(&pApicCpu->StatMsrReadRZ,     "%u/RZ/MsrRead",     "Number of APIC MSR reads in RZ.");
        APIC_REG_COUNTER(&pApicCpu->StatMsrWriteRZ,    "%u/RZ/MsrWrite",    "Number of APIC MSR writes in RZ.");

        APIC_REG_COUNTER(&pApicCpu->StatMmioReadR3,    "%u/R3/MmioRead",    "Number of APIC MMIO reads in R3.");
        APIC_REG_COUNTER(&pApicCpu->StatMmioWriteR3,   "%u/R3/MmioWrite",   "Number of APIC MMIO writes in R3.");
        APIC_REG_COUNTER(&pApicCpu->StatMsrReadR3,     "%u/R3/MsrRead",     "Number of APIC MSR reads in R3.");
        APIC_REG_COUNTER(&pApicCpu->StatMsrWriteR3,    "%u/R3/MsrWrite",    "Number of APIC MSR writes in R3.");

        APIC_REG_COUNTER(&pApicCpu->StatPostIntrAlreadyPending,
                                                       "%u/PostInterruptAlreadyPending", "Number of times an interrupt is already pending.");
        APIC_REG_COUNTER(&pApicCpu->StatTimerCallback, "%u/TimerCallback",  "Number of times the timer callback is invoked.");

        APIC_REG_COUNTER(&pApicCpu->StatTprWrite,      "%u/TprWrite",       "Number of TPR writes.");
        APIC_REG_COUNTER(&pApicCpu->StatTprRead,       "%u/TprRead",        "Number of TPR reads.");
        APIC_REG_COUNTER(&pApicCpu->StatEoiWrite,      "%u/EoiWrite",       "Number of EOI writes.");
        APIC_REG_COUNTER(&pApicCpu->StatMaskedByTpr,   "%u/MaskedByTpr",    "Number of times TPR masks an interrupt in apicGetInterrupt.");
        APIC_REG_COUNTER(&pApicCpu->StatMaskedByPpr,   "%u/MaskedByPpr",    "Number of times PPR masks an interrupt in apicGetInterrupt.");
        APIC_REG_COUNTER(&pApicCpu->StatTimerIcrWrite, "%u/TimerIcrWrite",  "Number of times the timer ICR is written.");
        APIC_REG_COUNTER(&pApicCpu->StatIcrLoWrite,    "%u/IcrLoWrite",     "Number of times the ICR Lo (send IPI) is written.");
        APIC_REG_COUNTER(&pApicCpu->StatIcrHiWrite,    "%u/IcrHiWrite",     "Number of times the ICR Hi is written.");
        APIC_REG_COUNTER(&pApicCpu->StatIcrFullWrite,  "%u/IcrFullWrite",   "Number of times the ICR full (send IPI, x2APIC) is written.");
        APIC_REG_COUNTER(&pApicCpu->StatIdMsrRead,     "%u/IdMsrRead",      "Number of times the APIC-ID MSR is read.");
        APIC_REG_COUNTER(&pApicCpu->StatDcrWrite,      "%u/DcrWrite",       "Number of times the DCR is written.");
        APIC_REG_COUNTER(&pApicCpu->StatDfrWrite,      "%u/DfrWrite",       "Number of times the DFR is written.");
        APIC_REG_COUNTER(&pApicCpu->StatLdrWrite,      "%u/LdrWrite",       "Number of times the LDR is written.");
        APIC_REG_COUNTER(&pApicCpu->StatLvtTimerWrite, "%u/LvtTimerWrite",  "Number of times the LVT timer is written.");

        APIC_PROF_COUNTER(&pApicCpu->StatUpdatePendingIntrs,
                                                       "/PROF/CPU%u/APIC/UpdatePendingInterrupts", "Profiling of APICUpdatePendingInterrupts");
        APIC_PROF_COUNTER(&pApicCpu->StatPostIntr,     "/PROF/CPU%u/APIC/PostInterrupt",  "Profiling of APICPostInterrupt");
#endif
    }

# undef APIC_PROF_COUNTER
# undef APIC_REG_ACCESS_COUNTER

    return VINF_SUCCESS;
}

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

