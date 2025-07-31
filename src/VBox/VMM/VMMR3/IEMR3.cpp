/* $Id: IEMR3.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_EM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/ssm.h>
#include "IEMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/err.h>
#ifdef VBOX_WITH_DEBUGGER
# include <VBox/dbg.h>
#endif

#include <iprt/assert.h>
#include <iprt/getopt.h>
#ifdef IEM_WITH_TLB_TRACE
# include <iprt/mem.h>
#endif
#include <iprt/string.h>

#if defined(VBOX_WITH_IEM_RECOMPILER) && !defined(VBOX_VMM_TARGET_ARMV8)
# include "IEMN8veRecompiler.h"
# include "IEMThreadedFunctions.h"
# include "IEMInline.h"
# ifdef VBOX_VMM_TARGET_X86
#  include "VMMAll/target-x86/IEMInline-x86.h"
# endif
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNDBGFINFOARGVINT iemR3InfoITlb;
static FNDBGFINFOARGVINT iemR3InfoDTlb;
#ifdef IEM_WITH_TLB_TRACE
static FNDBGFINFOARGVINT iemR3InfoTlbTrace;
#endif
#if defined(VBOX_WITH_IEM_RECOMPILER) && !defined(VBOX_VMM_TARGET_ARMV8)
static FNDBGFINFOARGVINT iemR3InfoTb;
static FNDBGFINFOARGVINT iemR3InfoTbTop;
#endif
#ifdef VBOX_WITH_DEBUGGER
static void iemR3RegisterDebuggerCommands(void);
#endif


#if !defined(VBOX_VMM_TARGET_ARMV8)
static const char *iemGetTargetCpuName(uint32_t enmTargetCpu)
{
    switch (enmTargetCpu)
    {
#define CASE_RET_STR(enmValue) case enmValue: return #enmValue + (sizeof("IEMTARGETCPU_") - 1)
        CASE_RET_STR(IEMTARGETCPU_8086);
        CASE_RET_STR(IEMTARGETCPU_V20);
        CASE_RET_STR(IEMTARGETCPU_186);
        CASE_RET_STR(IEMTARGETCPU_286);
        CASE_RET_STR(IEMTARGETCPU_386);
        CASE_RET_STR(IEMTARGETCPU_486);
        CASE_RET_STR(IEMTARGETCPU_PENTIUM);
        CASE_RET_STR(IEMTARGETCPU_PPRO);
        CASE_RET_STR(IEMTARGETCPU_CURRENT);
#undef CASE_RET_STR
        default: return "Unknown";
    }
}
#endif


#if defined(RT_ARCH_ARM64) && defined(_MSC_VER)
# pragma warning(disable:4883) /* profile build: IEMR3.cpp(114) : warning C4883: 'IEMR3Init': function size suppresses optimizations*/
#endif

/**
 * Initializes the interpreted execution manager.
 *
 * This must be called after CPUM as we're quering information from CPUM about
 * the guest and host CPUs.
 *
 * @returns VBox status code.
 * @param   pVM                The cross context VM structure.
 */
VMMR3_INT_DECL(int) IEMR3Init(PVM pVM)
{
    /*
     * Read configuration.
     */
#if (!defined(VBOX_VMM_TARGET_ARMV8) && !defined(VBOX_WITHOUT_CPUID_HOST_CALL)) || defined(VBOX_WITH_IEM_RECOMPILER)
    PCFGMNODE const pIem = CFGMR3GetChild(CFGMR3GetRoot(pVM), "IEM");
    int rc;
#endif

#if defined(VBOX_VMM_TARGET_X86) && !defined(VBOX_WITHOUT_CPUID_HOST_CALL)
    /** @cfgm{/IEM/CpuIdHostCall, boolean, false}
     * Controls whether the custom VBox specific CPUID host call interface is
     * enabled or not. */
# ifdef DEBUG_bird
    rc = CFGMR3QueryBoolDef(pIem, "CpuIdHostCall", &pVM->iem.s.fCpuIdHostCall, true);
# else
    rc = CFGMR3QueryBoolDef(pIem, "CpuIdHostCall", &pVM->iem.s.fCpuIdHostCall, false);
# endif
    AssertLogRelRCReturn(rc, rc);
#endif

#ifdef VBOX_WITH_IEM_RECOMPILER
    /** @cfgm{/IEM/MaxTbCount, uint32_t, 524288}
     * Max number of TBs per EMT. */
    uint32_t cMaxTbs = 0;
    rc = CFGMR3QueryU32Def(pIem, "MaxTbCount", &cMaxTbs, _512K);
    AssertLogRelRCReturn(rc, rc);
    if (cMaxTbs < _16K || cMaxTbs > _8M)
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS,
                          "MaxTbCount value %u (%#x) is out of range (min %u, max %u)", cMaxTbs, cMaxTbs, _16K, _8M);

    /** @cfgm{/IEM/InitialTbCount, uint32_t, 32678}
     * Initial (minimum) number of TBs per EMT in ring-3. */
    uint32_t cInitialTbs = 0;
    rc = CFGMR3QueryU32Def(pIem, "InitialTbCount", &cInitialTbs, RT_MIN(cMaxTbs, _32K));
    AssertLogRelRCReturn(rc, rc);
    if (cInitialTbs < _16K || cInitialTbs > _8M)
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS,
                          "InitialTbCount value %u (%#x) is out of range (min %u, max %u)", cInitialTbs, cInitialTbs, _16K, _8M);

    /* Check that the two values makes sense together. Expect user/api to do
       the right thing or get lost. */
    if (cInitialTbs > cMaxTbs)
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS,
                          "InitialTbCount value %u (%#x) is higher than the MaxTbCount value %u (%#x)",
                          cInitialTbs, cInitialTbs, cMaxTbs, cMaxTbs);

    /** @cfgm{/IEM/MaxExecMem, uint64_t, 512 MiB}
     * Max executable memory for recompiled code per EMT. */
    uint64_t cbMaxExec = 0;
    rc = CFGMR3QueryU64Def(pIem, "MaxExecMem", &cbMaxExec, _512M);
    AssertLogRelRCReturn(rc, rc);
    if (cbMaxExec < _1M || cbMaxExec > 16*_1G64)
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS,
                          "MaxExecMem value %'RU64 (%#RX64) is out of range (min %'RU64, max %'RU64)",
                          cbMaxExec, cbMaxExec, (uint64_t)_1M, 16*_1G64);

    /** @cfgm{/IEM/ExecChunkSize, uint32_t, 0 (auto)}
     * The executable memory allocator chunk size. */
    uint32_t cbChunkExec = 0;
    rc = CFGMR3QueryU32Def(pIem, "ExecChunkSize", &cbChunkExec, 0);
    AssertLogRelRCReturn(rc, rc);
    if (cbChunkExec != 0 && cbChunkExec != UINT32_MAX && (cbChunkExec < _1M || cbChunkExec > _256M))
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS,
                          "ExecChunkSize value %'RU32 (%#RX32) is out of range (min %'RU32, max %'RU32)",
                          cbChunkExec, cbChunkExec, _1M, _256M);

    /** @cfgm{/IEM/InitialExecMemSize, uint64_t, 1}
     * The initial executable memory allocator size (per EMT).  The value is
     * rounded up to the nearest chunk size, so 1 byte means one chunk. */
    uint64_t cbInitialExec = 0;
    rc = CFGMR3QueryU64Def(pIem, "InitialExecMemSize", &cbInitialExec, 0);
    AssertLogRelRCReturn(rc, rc);
    if (cbInitialExec > cbMaxExec)
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS,
                          "InitialExecMemSize value %'RU64 (%#RX64) is out of range (max %'RU64)",
                          cbInitialExec, cbInitialExec, cbMaxExec);

    /** @cfgm{/IEM/NativeRecompileAtUsedCount, uint32_t, 16}
     * The translation block use count value to do native recompilation at.
     * Set to zero to disable native recompilation. */
    uint32_t uTbNativeRecompileAtUsedCount = 16;
    rc = CFGMR3QueryU32Def(pIem, "NativeRecompileAtUsedCount", &uTbNativeRecompileAtUsedCount, 16);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/IEM/HostICacheInvalidationViaHostAPI, bool, false}
     * Whether to use any available host OS API for flushing the instruction cache
     * after completing an translation block. */
    bool fFlag = false;
    rc = CFGMR3QueryBoolDef(pIem, "HostICacheInvalidationViaHostAPI", &fFlag, false);
    AssertLogRelRCReturn(rc, rc);
    uint8_t fHostICacheInvalidation = fFlag ? IEMNATIVE_ICACHE_F_USE_HOST_API : 0;

    /** @cfgm{/IEM/HostICacheInvalidationEndWithIsb, bool, false}
     * Whether to include an ISB in the instruction cache invalidation sequence
     * after completing an translation block. */
    fFlag = false;
    rc = CFGMR3QueryBoolDef(pIem, "HostICacheInvalidationEndWithIsb", &fFlag, false);
    AssertLogRelRCReturn(rc, rc);
    if (fFlag)
        fHostICacheInvalidation |= IEMNATIVE_ICACHE_F_END_WITH_ISH;

#endif /* VBOX_WITH_IEM_RECOMPILER*/

    /*
     * Initialize per-CPU data and register statistics.
     */
#if 1
    uint64_t const uInitialTlbRevision = UINT64_C(0) - (IEMTLB_REVISION_INCR * 200U);
    uint64_t const uInitialTlbPhysRev  = UINT64_C(0) - (IEMTLB_PHYS_REV_INCR * 100U);
#else
    uint64_t const uInitialTlbRevision = UINT64_C(0) + (IEMTLB_REVISION_INCR * 4U);
    uint64_t const uInitialTlbPhysRev  = UINT64_C(0) + (IEMTLB_PHYS_REV_INCR * 4U);
#endif

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU const pVCpu = pVM->apCpusR3[idCpu];
        AssertCompile(sizeof(pVCpu->iem.s) <= sizeof(pVCpu->iem.padding)); /* (tstVMStruct can't do it's job w/o instruction stats) */

        pVCpu->iem.s.CodeTlb.uTlbRevision       = pVCpu->iem.s.DataTlb.uTlbRevision       = uInitialTlbRevision;
#ifdef VBOX_VMM_TARGET_X86
        pVCpu->iem.s.CodeTlb.uTlbRevisionGlobal = pVCpu->iem.s.DataTlb.uTlbRevisionGlobal = uInitialTlbRevision;
        pVCpu->iem.s.CodeTlb.uTlbPhysRev        = pVCpu->iem.s.DataTlb.uTlbPhysRev        = uInitialTlbPhysRev;
        pVCpu->iem.s.CodeTlb.NonGlobalLargePageRange.uFirstTag = UINT64_MAX;
        pVCpu->iem.s.CodeTlb.GlobalLargePageRange.uFirstTag    = UINT64_MAX;
        pVCpu->iem.s.DataTlb.NonGlobalLargePageRange.uFirstTag = UINT64_MAX;
        pVCpu->iem.s.DataTlb.GlobalLargePageRange.uFirstTag    = UINT64_MAX;
#elif defined(VBOX_VMM_TARGET_ARMV8)
        pVCpu->iem.s.CodeTlb.uTlbPhysRevAndStuff0 = pVCpu->iem.s.DataTlb.uTlbPhysRevAndStuff0 = uInitialTlbPhysRev | IEMTLBE_F_NG;
        pVCpu->iem.s.CodeTlb.uTlbPhysRevAndStuff1 = pVCpu->iem.s.DataTlb.uTlbPhysRevAndStuff1 = uInitialTlbPhysRev | IEMTLBE_F_NG;
        pVCpu->iem.s.CodeTlb.LargePageRange.uFirstTag = UINT64_MAX;
        pVCpu->iem.s.DataTlb.LargePageRange.uFirstTag = UINT64_MAX;
#else
# error "port me"
#endif

#ifndef VBOX_VMM_TARGET_ARMV8
        pVCpu->iem.s.cTbsTillNextTimerPoll      = 128;
        pVCpu->iem.s.cTbsTillNextTimerPollPrev  = 128;
#endif

        /*
         * Host and guest CPU information.
         */
        if (idCpu == 0)
        {
            pVCpu->iem.s.enmCpuVendor                     = CPUMGetGuestCpuVendor(pVM);
#if !defined(VBOX_VMM_TARGET_ARMV8)
            pVCpu->iem.s.aidxTargetCpuEflFlavour[0]       =    pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL
                                                            || pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_VIA /*??*/
                                                          ? IEMTARGETCPU_EFL_BEHAVIOR_INTEL : IEMTARGETCPU_EFL_BEHAVIOR_AMD;
# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
            if (pVCpu->iem.s.enmCpuVendor == CPUMGetHostCpuVendor(pVM))
                pVCpu->iem.s.aidxTargetCpuEflFlavour[1]   = IEMTARGETCPU_EFL_BEHAVIOR_NATIVE;
            else
# endif
                pVCpu->iem.s.aidxTargetCpuEflFlavour[1]   = pVCpu->iem.s.aidxTargetCpuEflFlavour[0];
#else
            pVCpu->iem.s.aidxTargetCpuEflFlavour[0]   = IEMTARGETCPU_EFL_BEHAVIOR_NATIVE;
            pVCpu->iem.s.aidxTargetCpuEflFlavour[1]   = pVCpu->iem.s.aidxTargetCpuEflFlavour[0];
#endif

#if !defined(VBOX_VMM_TARGET_ARMV8) && (IEM_CFG_TARGET_CPU == IEMTARGETCPU_DYNAMIC)
            switch (pVM->cpum.ro.GuestFeatures.enmMicroarch)
            {
                case kCpumMicroarch_Intel_8086:     pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_8086; break;
                case kCpumMicroarch_Intel_80186:    pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_186; break;
                case kCpumMicroarch_Intel_80286:    pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_286; break;
                case kCpumMicroarch_Intel_80386:    pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_386; break;
                case kCpumMicroarch_Intel_80486:    pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_486; break;
                case kCpumMicroarch_Intel_P5:       pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_PENTIUM; break;
                case kCpumMicroarch_Intel_P6:       pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_PPRO; break;
                case kCpumMicroarch_NEC_V20:        pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_V20; break;
                case kCpumMicroarch_NEC_V30:        pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_V20; break;
                default:                            pVCpu->iem.s.uTargetCpu = IEMTARGETCPU_CURRENT; break;
            }
            LogRel(("IEM: TargetCpu=%s, Microarch=%s aidxTargetCpuEflFlavour={%d,%d}\n",
                    iemGetTargetCpuName(pVCpu->iem.s.uTargetCpu), CPUMMicroarchName(pVM->cpum.ro.GuestFeatures.enmMicroarch),
                    pVCpu->iem.s.aidxTargetCpuEflFlavour[0], pVCpu->iem.s.aidxTargetCpuEflFlavour[1]));
#else
            LogRel(("IEM: Microarch=%s aidxTargetCpuEflFlavour={%d,%d}\n",
                    CPUMMicroarchName(pVM->cpum.ro.GuestFeatures.enmMicroarch),
                    pVCpu->iem.s.aidxTargetCpuEflFlavour[0], pVCpu->iem.s.aidxTargetCpuEflFlavour[1]));
#endif
        }
        else
        {
            pVCpu->iem.s.enmCpuVendor                     = pVM->apCpusR3[0]->iem.s.enmCpuVendor;
            pVCpu->iem.s.aidxTargetCpuEflFlavour[0]       = pVM->apCpusR3[0]->iem.s.aidxTargetCpuEflFlavour[0];
            pVCpu->iem.s.aidxTargetCpuEflFlavour[1]       = pVM->apCpusR3[0]->iem.s.aidxTargetCpuEflFlavour[1];
#if IEM_CFG_TARGET_CPU == IEMTARGETCPU_DYNAMIC
            pVCpu->iem.s.uTargetCpu                       = pVM->apCpusR3[0]->iem.s.uTargetCpu;
#endif
        }

        /*
         * Mark all buffers free.
         */
        uint32_t iMemMap = RT_ELEMENTS(pVCpu->iem.s.aMemMappings);
        while (iMemMap-- > 0)
            pVCpu->iem.s.aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;

#ifdef VBOX_WITH_IEM_RECOMPILER
        /*
         * Recompiler state and configuration distribution.
         */
        pVCpu->iem.s.uRegFpCtrl                    = IEMNATIVE_SIMD_FP_CTRL_REG_NOT_MODIFIED;
        pVCpu->iem.s.uTbNativeRecompileAtUsedCount = uTbNativeRecompileAtUsedCount;
        pVCpu->iem.s.fHostICacheInvalidation       = fHostICacheInvalidation;
#endif

#ifdef IEM_WITH_TLB_TRACE
        /*
         * Allocate trace buffer.
         */
        pVCpu->iem.s.idxTlbTraceEntry      = 0;
        pVCpu->iem.s.cTlbTraceEntriesShift = 16;
        pVCpu->iem.s.paTlbTraceEntries     = (PIEMTLBTRACEENTRY)RTMemPageAlloc(  RT_BIT_Z(pVCpu->iem.s.cTlbTraceEntriesShift)
                                                                               * sizeof(*pVCpu->iem.s.paTlbTraceEntries));
        AssertLogRelReturn(pVCpu->iem.s.paTlbTraceEntries, VERR_NO_PAGE_MEMORY);
#endif
    }


#ifdef VBOX_WITH_IEM_RECOMPILER
    /*
     * Initialize the TB allocator and cache (/ hash table).
     *
     * This is done by each EMT to try get more optimal thread/numa locality of
     * the allocations.
     */
    rc = VMR3ReqCallWait(pVM, VMCPUID_ALL, (PFNRT)iemTbInit, 6,
                         pVM, cInitialTbs, cMaxTbs, cbInitialExec, cbMaxExec, cbChunkExec);
    AssertLogRelRCReturn(rc, rc);
#endif

    /*
     * Register statistics.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8) && defined(VBOX_WITH_NESTED_HWVIRT_VMX) /* quick fix for stupid structure duplication non-sense */
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        char   szPat[128];
        RT_NOREF_PV(szPat); /* lazy bird */
        char   szVal[128];
        RT_NOREF_PV(szVal); /* lazy bird */

        STAMR3RegisterF(pVM, &pVCpu->iem.s.cInstructions,               STAMTYPE_U32,       STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Instructions interpreted",                     "/IEM/CPU%u/cInstructions", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cLongJumps,                  STAMTYPE_U32,       STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                        "Number of longjmp calls",                      "/IEM/CPU%u/cLongJumps", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cPotentialExits,             STAMTYPE_U32,       STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Potential exits",                              "/IEM/CPU%u/cPotentialExits", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cRetAspectNotImplemented,    STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "VERR_IEM_ASPECT_NOT_IMPLEMENTED",              "/IEM/CPU%u/cRetAspectNotImplemented", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cRetInstrNotImplemented,     STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "VERR_IEM_INSTR_NOT_IMPLEMENTED",               "/IEM/CPU%u/cRetInstrNotImplemented", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cRetInfStatuses,             STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Informational statuses returned",              "/IEM/CPU%u/cRetInfStatuses", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cRetErrStatuses,             STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Error statuses returned",                      "/IEM/CPU%u/cRetErrStatuses", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cPendingCommit,              STAMTYPE_U32,       STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                        "Times RC/R0 had to postpone instruction committing to ring-3", "/IEM/CPU%u/cPendingCommit", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.cMisalignedAtomics,          STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                        "Number of misaligned (for the host) atomic instructions", "/IEM/CPU%u/cMisalignedAtomics", idCpu);

        /* Code TLB: */
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.uTlbRevision,        STAMTYPE_X64,       STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB non-global revision",                 "/IEM/CPU%u/Tlb/Code/RevisionNonGlobal", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.uTlbRevisionGlobal,  STAMTYPE_X64,       STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB global revision",                     "/IEM/CPU%u/Tlb/Code/RevisionGlobal", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlsFlushes,         STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB non-global flushes",                  "/IEM/CPU%u/Tlb/Code/RevisionNonGlobalFlushes", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlsGlobalFlushes,   STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB global flushes",                      "/IEM/CPU%u/Tlb/Code/RevisionGlobalFlushes", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbRevisionRollovers, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB revision rollovers",                  "/IEM/CPU%u/Tlb/Code/RevisionRollovers", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.CodeTlb.uTlbPhysRev, STAMTYPE_X64,       STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB physical revision",                   "/IEM/CPU%u/Tlb/Code/PhysicalRevision", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbPhysRevFlushes,  STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB revision flushes",                    "/IEM/CPU%u/Tlb/Code/PhysicalRevisionFlushes", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbPhysRevRollovers, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB revision rollovers",                  "/IEM/CPU%u/Tlb/Code/PhysicalRevisionRollovers", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbGlobalLargePageCurLoads,  STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB global large page loads since flush", "/IEM/CPU%u/Tlb/Code/LargePageGlobalCurLoads", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.GlobalLargePageRange.uFirstTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB global large page range: lowest tag", "/IEM/CPU%u/Tlb/Code/LargePageGlobalFirstTag", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.GlobalLargePageRange.uLastTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB global large page range: last tag",   "/IEM/CPU%u/Tlb/Code/LargePageGlobalLastTag", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbNonGlobalLargePageCurLoads,  STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB non-global large page loads since flush", "/IEM/CPU%u/Tlb/Code/LargePageNonGlobalCurLoads", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.NonGlobalLargePageRange.uFirstTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB non-global large page range: lowest tag", "/IEM/CPU%u/Tlb/Code/LargePageNonGlobalFirstTag", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.NonGlobalLargePageRange.uLastTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB non-global large page range: last tag",   "/IEM/CPU%u/Tlb/Code/LargePageNonGlobalLastTag", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbInvlPg, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB page invalidation requests",          "/IEM/CPU%u/Tlb/Code/InvlPg", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbInvlPgLargeGlobal, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB page invlpg scanning for global large pages",     "/IEM/CPU%u/Tlb/Code/InvlPg/LargeGlobal", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbInvlPgLargeNonGlobal, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Code TLB page invlpg scanning for non-global large pages", "/IEM/CPU%u/Tlb/Code/InvlPg/LargeNonGlobal", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbCoreMisses,      STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB misses",                              "/IEM/CPU%u/Tlb/Code/Misses", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbCoreGlobalLoads, STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB global loads",                        "/IEM/CPU%u/Tlb/Code/Misses/GlobalLoads", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbSlowCodeReadPath, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB slow read path",                      "/IEM/CPU%u/Tlb/Code/SlowReads", idCpu);
# ifdef IEM_WITH_TLB_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbCoreHits,        STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB hits (non-native)",                   "/IEM/CPU%u/Tlb/Code/Hits/Other", idCpu);
#  if defined(VBOX_WITH_IEM_NATIVE_RECOMPILER)
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeCodeTlbHitsForNewPage, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB native hits on new page",             "/IEM/CPU%u/Tlb/Code/Hits/New-Page", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeCodeTlbHitsForNewPageWithOffset, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB native hits on new page /w offset",   "/IEM/CPU%u/Tlb/Code/Hits/New-Page-With-Offset", idCpu);
#  endif

        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/Tlb/Code/Hits/*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Code TLB hits",
                          "/IEM/CPU%u/Tlb/Code/Hits", idCpu);

        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/Tlb/Code/Hits|/IEM/CPU%u/Tlb/Code/Misses", idCpu, idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Code TLB lookups (sum of hits and misses)",
                          "/IEM/CPU%u/Tlb/Code/AllLookups", idCpu);

        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/Tlb/Code/Misses", idCpu);
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/Tlb/Code/Hits", idCpu);
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PPM, szVal, true, szPat,
                               "Code TLB actual miss rate",             "/IEM/CPU%u/Tlb/Code/RateMisses", idCpu);

#  if defined(VBOX_WITH_IEM_NATIVE_RECOMPILER)
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbNativeMissTag,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB misses in native code: Tag mismatch [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Code/Misses/NativeBreakdown/Tag", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbNativeMissFlagsAndPhysRev,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB misses in native code: Flags or physical revision mistmatch [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Code/Misses/NativeBreakdown/FlagsAndPhysRev", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbNativeMissAlignment,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB misses in native code: Alignment [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Code/Misses/NativeBreakdown/Alignment", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbNativeMissCrossPage,  STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB misses in native code: Cross page [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Code/Misses/NativeBreakdown/CrossPage", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.CodeTlb.cTlbNativeMissNonCanonical,  STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB misses in native code: Non-canonical [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Code/Misses/NativeBreakdown/NonCanonical", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeCodeTlbMissesNewPage, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB native misses on new page",
                        "/IEM/CPU%u/Tlb/Code/Misses/NativeBreakdown2/New-Page", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeCodeTlbMissesNewPageWithOffset, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Code TLB native misses on new page w/ offset",
                        "/IEM/CPU%u/Tlb/Code/Misses/NativeBreakdown2/New-Page-With-Offset", idCpu);
#  endif
# endif /* IEM_WITH_TLB_STATISTICS */

        /* Data TLB organized as best we can... */
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.uTlbRevision,        STAMTYPE_X64,       STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB non-global revision",                 "/IEM/CPU%u/Tlb/Data/RevisionNonGlobal", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.uTlbRevisionGlobal,  STAMTYPE_X64,       STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB global revision",                     "/IEM/CPU%u/Tlb/Data/RevisionGlobal", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlsFlushes,         STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB non-global flushes",                  "/IEM/CPU%u/Tlb/Data/RevisionNonGlobalFlushes", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlsGlobalFlushes,   STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB global flushes",                      "/IEM/CPU%u/Tlb/Data/RevisionGlobalFlushes", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbRevisionRollovers, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB revision rollovers",                  "/IEM/CPU%u/Tlb/Data/RevisionRollovers", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.DataTlb.uTlbPhysRev, STAMTYPE_X64,       STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB physical revision",                   "/IEM/CPU%u/Tlb/Data/PhysicalRevision", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbPhysRevFlushes,  STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB revision flushes",                    "/IEM/CPU%u/Tlb/Data/PhysicalRevisionFlushes", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbPhysRevRollovers, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB revision rollovers",                  "/IEM/CPU%u/Tlb/Data/PhysicalRevisionRollovers", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbGlobalLargePageCurLoads,  STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB global large page loads since flush", "/IEM/CPU%u/Tlb/Data/LargePageGlobalCurLoads", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.GlobalLargePageRange.uFirstTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB global large page range: lowest tag", "/IEM/CPU%u/Tlb/Data/LargePageGlobalFirstTag", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.GlobalLargePageRange.uLastTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB global large page range: last tag",   "/IEM/CPU%u/Tlb/Data/LargePageGlobalLastTag", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbNonGlobalLargePageCurLoads,  STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB non-global large page loads since flush", "/IEM/CPU%u/Tlb/Data/LargePageNonGlobalCurLoads", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.NonGlobalLargePageRange.uFirstTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB non-global large page range: lowest tag", "/IEM/CPU%u/Tlb/Data/LargePageNonGlobalFirstTag", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.NonGlobalLargePageRange.uLastTag, STAMTYPE_X64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB non-global large page range: last tag",   "/IEM/CPU%u/Tlb/Data/LargePageNonGlobalLastTag", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbInvlPg, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB page invalidation requests",          "/IEM/CPU%u/Tlb/Data/InvlPg", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbInvlPgLargeGlobal, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB page invlpg scanning for global large pages",     "/IEM/CPU%u/Tlb/Data/InvlPg/LargeGlobal", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbInvlPgLargeNonGlobal, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Data TLB page invlpg scanning for non-global large pages", "/IEM/CPU%u/Tlb/Data/InvlPg/LargeNonGlobal", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbCoreMisses,      STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB core misses (iemMemMap, direct iemMemMapJmp (not safe path))",
                        "/IEM/CPU%u/Tlb/Data/Misses/Core", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbCoreGlobalLoads, STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB global loads",
                        "/IEM/CPU%u/Tlb/Data/Misses/Core/GlobalLoads", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbSafeReadPath,    STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB safe read path (inline/native misses going to iemMemMapJmp)",
                        "/IEM/CPU%u/Tlb/Data/Misses/Safe/Reads", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbSafeWritePath,   STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB safe write path (inline/native misses going to iemMemMapJmp)",
                        "/IEM/CPU%u/Tlb/Data/Misses/Safe/Writes", idCpu);
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/Tlb/Data/Misses/*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Data TLB misses",
                          "/IEM/CPU%u/Tlb/Data/Misses", idCpu);

        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/Tlb/Data/Misses/Safe/*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Data TLB actual safe path calls (read + write)",
                          "/IEM/CPU%u/Tlb/Data/Misses/Safe", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbSafeHits,        STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB hits in iemMemMapJmp - not part of safe-path total",
                        "/IEM/CPU%u/Tlb/Data/Misses/Safe/SubPartHits", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbSafeMisses,      STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB misses in iemMemMapJmp - not part of safe-path total",
                        "/IEM/CPU%u/Tlb/Data/Misses/Safe/SubPartMisses", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbSafeGlobalLoads, STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB global loads",
                        "/IEM/CPU%u/Tlb/Data/Misses/Safe/SubPartMisses/GlobalLoads", idCpu);

# ifdef IEM_WITH_TLB_STATISTICS
#  ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbNativeMissTag,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB misses in native code: Tag mismatch [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Data/Misses/NativeBreakdown/Tag", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbNativeMissFlagsAndPhysRev,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB misses in native code: Flags or physical revision mistmatch [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Data/Misses/NativeBreakdown/FlagsAndPhysRev", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbNativeMissAlignment,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB misses in native code: Alignment [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Data/Misses/NativeBreakdown/Alignment", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbNativeMissCrossPage,  STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB misses in native code: Cross page [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Data/Misses/NativeBreakdown/CrossPage", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbNativeMissNonCanonical,  STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB misses in native code: Non-canonical [not directly included grand parent sum]",
                        "/IEM/CPU%u/Tlb/Data/Misses/NativeBreakdown/NonCanonical", idCpu);
#  endif
# endif

# ifdef IEM_WITH_TLB_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbCoreHits,        STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB core hits (iemMemMap, direct iemMemMapJmp (not safe path))",
                        "/IEM/CPU%u/Tlb/Data/Hits/Core", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.DataTlb.cTlbInlineCodeHits,  STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB hits in IEMAllMemRWTmplInline.cpp.h",
                        "/IEM/CPU%u/Tlb/Data/Hits/Inline", idCpu);
#  ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeTlbHitsForStack, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB native stack access hits",
                        "/IEM/CPU%u/Tlb/Data/Hits/Native/Stack", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeTlbHitsForFetch, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB native data fetch hits",
                        "/IEM/CPU%u/Tlb/Data/Hits/Native/Fetch", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeTlbHitsForStore, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB native data store hits",
                        "/IEM/CPU%u/Tlb/Data/Hits/Native/Store", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeTlbHitsForMapped, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Data TLB native mapped data hits",
                        "/IEM/CPU%u/Tlb/Data/Hits/Native/Mapped", idCpu);
#  endif
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/Tlb/Data/Hits/*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Data TLB hits",
                          "/IEM/CPU%u/Tlb/Data/Hits", idCpu);

#  ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/Tlb/Data/Hits/Native/*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Data TLB hits from native code",
                          "/IEM/CPU%u/Tlb/Data/Hits/Native", idCpu);
#  endif

        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/Tlb/Data/Hits|/IEM/CPU%u/Tlb/Data/Misses", idCpu, idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Data TLB lookups (sum of hits and misses)",
                          "/IEM/CPU%u/Tlb/Data/AllLookups", idCpu);

        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/Tlb/Data/Misses", idCpu);
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/Tlb/Data/Hits", idCpu);
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PPM, szVal, true, szPat,
                               "Data TLB actual miss rate",             "/IEM/CPU%u/Tlb/Data/RateMisses", idCpu);

# endif /* IEM_WITH_TLB_STATISTICS */


#ifdef VBOX_WITH_IEM_RECOMPILER
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.cTbExecNative,       STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Executed native translation block",            "/IEM/CPU%u/re/cTbExecNative", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.cTbExecThreaded,     STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Executed threaded translation block",          "/IEM/CPU%u/re/cTbExecThreaded", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbThreadedExecBreaks, STAMTYPE_COUNTER,   STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                        "Times threaded TB execution was interrupted/broken off", "/IEM/CPU%u/re/cTbExecThreadedBreaks", idCpu);
# ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbThreadedExecBreaksWithLookup, STAMTYPE_COUNTER,   STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                        "Times threaded TB execution was interrupted/broken off on a call with lookup entries", "/IEM/CPU%u/re/cTbExecThreadedBreaksWithLookup", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbThreadedExecBreaksWithoutLookup, STAMTYPE_COUNTER,   STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                        "Times threaded TB execution was interrupted/broken off on a call without lookup entries", "/IEM/CPU%u/re/cTbExecThreadedBreaksWithoutLookup", idCpu);
# endif

# ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPoll, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                        "Timer polling profiling",                      "/IEM/CPU%u/re/TimerPoll", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPollRun, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                        "Timer polling profiling",                      "/IEM/CPU%u/re/TimerPoll/Running", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPollUnchanged, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Timer polling interval unchanged",             "/IEM/CPU%u/re/TimerPoll/Unchanged", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPollTiny, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Timer polling interval tiny",                  "/IEM/CPU%u/re/TimerPoll/Tiny", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPollDefaultCalc, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Timer polling interval calculated using defaults", "/IEM/CPU%u/re/TimerPoll/DefaultCalc", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPollMax, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Timer polling interval maxed out",             "/IEM/CPU%u/re/TimerPoll/Max", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPollFactorDivision, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_OCCURENCE,
                        "Timer polling factor",                         "/IEM/CPU%u/re/TimerPoll/FactorDivision", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTimerPollFactorMultiplication, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Timer polling factor",                         "/IEM/CPU%u/re/TimerPoll/FactorMultiplication", idCpu);
# endif
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.cTbsTillNextTimerPollPrev, STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Timer polling interval (in TBs)",              "/IEM/CPU%u/re/TimerPollInterval", idCpu);

        PIEMTBALLOCATOR const pTbAllocator = pVCpu->iem.s.pTbAllocatorR3;
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->StatAllocs,         STAMTYPE_COUNTER,   STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                        "Translation block allocations",                "/IEM/CPU%u/re/cTbAllocCalls", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->StatFrees,          STAMTYPE_COUNTER,   STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                        "Translation block frees",                      "/IEM/CPU%u/re/cTbFreeCalls", idCpu);
# ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->StatPrune,          STAMTYPE_PROFILE,   STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                        "Time spent freeing up TBs when full at alloc", "/IEM/CPU%u/re/TbPruningAlloc", idCpu);
# endif
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->StatPruneNative,    STAMTYPE_PROFILE,   STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                        "Time spent freeing up native TBs when out of executable memory", "/IEM/CPU%u/re/ExecMem/TbPruningNative", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->cAllocatedChunks,   STAMTYPE_U16,   STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Populated TB chunks",                          "/IEM/CPU%u/re/cTbChunks", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->cMaxChunks,         STAMTYPE_U8,    STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Max number of TB chunks",                      "/IEM/CPU%u/re/cTbChunksMax", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->cTotalTbs,          STAMTYPE_U32,   STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Total number of TBs in the allocator",         "/IEM/CPU%u/re/cTbTotal", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->cMaxTbs,            STAMTYPE_U32,   STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Max total number of TBs allowed",              "/IEM/CPU%u/re/cTbTotalMax", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->cInUseTbs,          STAMTYPE_U32,   STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of currently allocated TBs",            "/IEM/CPU%u/re/cTbAllocated", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->cNativeTbs,         STAMTYPE_U32,   STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of currently allocated native TBs",     "/IEM/CPU%u/re/cTbAllocatedNative", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbAllocator->cThreadedTbs,       STAMTYPE_U32,   STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of currently allocated threaded TBs",   "/IEM/CPU%u/re/cTbAllocatedThreaded", idCpu);

        PIEMTBCACHE     const pTbCache     = pVCpu->iem.s.pTbCacheR3;
        STAMR3RegisterF(pVM, (void *)&pTbCache->cHash,                  STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Translation block lookup table size",          "/IEM/CPU%u/re/cTbHashTab", idCpu);

        STAMR3RegisterF(pVM, (void *)&pTbCache->cLookupHits,            STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                        "Translation block lookup hits",                "/IEM/CPU%u/re/cTbLookupHits", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbCache->cLookupHitsViaTbLookupTable, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                        "Translation block lookup hits via TB lookup table associated with the previous TB", "/IEM/CPU%u/re/cTbLookupHitsViaTbLookupTable", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbCache->cLookupMisses,          STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                        "Translation block lookup misses",              "/IEM/CPU%u/re/cTbLookupMisses", idCpu);
        STAMR3RegisterF(pVM, (void *)&pTbCache->cCollisions,            STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                        "Translation block hash table collisions",      "/IEM/CPU%u/re/cTbCollisions", idCpu);
# ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, (void *)&pTbCache->StatPrune,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                        "Time spent shortening collision lists",        "/IEM/CPU%u/re/TbPruningCollisions", idCpu);
# endif

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbThreadedCalls, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS_PER_TB,
                        "Calls per threaded translation block",         "/IEM/CPU%u/re/ThrdCallsPerTb", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbInstr,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_INSTR_PER_TB,
                        "Instruction per threaded translation block",   "/IEM/CPU%u/re/ThrdInstrPerTb", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbLookupEntries, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_INSTR_PER_TB,
                        "TB lookup table entries per threaded translation block", "/IEM/CPU%u/re/ThrdLookupEntriesPerTb", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatCheckIrqBreaks,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "TB breaks by CheckIrq",                        "/IEM/CPU%u/re/CheckIrqBreaks", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatCheckTimersBreaks, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "TB breaks by CheckIrq",                        "/IEM/CPU%u/re/CheckTimersBreaks", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatCheckModeBreaks, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "TB breaks by CheckMode",                       "/IEM/CPU%u/re/CheckModeBreaks", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatCheckBranchMisses, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Branch target misses",                         "/IEM/CPU%u/re/CheckTbJmpMisses", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatCheckNeedCsLimChecking, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Needing CS.LIM checking TB after branch or on page crossing", "/IEM/CPU%u/re/CheckTbNeedCsLimChecking", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbLoopFullTbDetected, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Detected loop full TB",  "/IEM/CPU%u/re/LoopFullTbDetected", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbLoopFullTbDetected2, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Detected loop full TB but looping back to before the first TB instruction",
                        "/IEM/CPU%u/re/LoopFullTbDetected2", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbLoopInTbDetected, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Detected loop within TB", "/IEM/CPU%u/re/LoopInTbDetected", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeExecMemInstrBufAllocFailed, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the exec memory allocator failed to allocate a large enough buffer",
                        "/IEM/CPU%u/re/NativeExecMemInstrBufAllocFailed", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeCallsRecompiled, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS_PER_TB,
                        "Number of threaded calls per TB that have been properly recompiled to native code",
                        "/IEM/CPU%u/re/NativeCallsRecompiledPerTb", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeCallsThreaded, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS_PER_TB,
                        "Number of threaded calls per TB that could not be recompiler to native code",
                        "/IEM/CPU%u/re/NativeCallsThreadedPerTb", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeFullyRecompiledTbs, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of threaded calls that could not be recompiler to native code",
                        "/IEM/CPU%u/re/NativeFullyRecompiledTbs", idCpu);

        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatTbNativeCode, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES_PER_TB,
                        "Size of native code per TB",                   "/IEM/CPU%u/re/NativeCodeSizePerTb", idCpu);
        STAMR3RegisterF(pVM, (void *)&pVCpu->iem.s.StatNativeRecompilation, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                        "Profiling iemNativeRecompile()",               "/IEM/CPU%u/re/NativeRecompilation", idCpu);

# ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER
#  ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeRegFindFree, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of calls to iemNativeRegAllocFindFree.",
                        "/IEM/CPU%u/re/NativeRegFindFree", idCpu);
#  endif
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeRegFindFreeVar, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times iemNativeRegAllocFindFree needed to free a variable.",
                        "/IEM/CPU%u/re/NativeRegFindFreeVar", idCpu);
#  ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeRegFindFreeNoVar, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times iemNativeRegAllocFindFree did not needed to free any variables.",
                        "/IEM/CPU%u/re/NativeRegFindFreeNoVar", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeRegFindFreeLivenessUnshadowed, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Times liveness info freeed up shadowed guest registers in iemNativeRegAllocFindFree.",
                        "/IEM/CPU%u/re/NativeRegFindFreeLivenessUnshadowed", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeRegFindFreeLivenessHelped,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Times liveness info helped finding the return register in iemNativeRegAllocFindFree.",
                        "/IEM/CPU%u/re/NativeRegFindFreeLivenessHelped", idCpu);

#   define REG_NATIVE_EFL_GROUP(a_Lower, a_Camel) do { \
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeEflPostponed ## a_Camel, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, \
                        "Postponed all status flag updating, " #a_Lower " instructions", \
                        "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "Postponed", idCpu); \
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeEflSkipped ## a_Camel,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, \
                        "Skipped all status flag updating, " #a_Lower " instructions", \
                        "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "Skipped", idCpu); \
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeEflTotal ## a_Camel,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, \
                        "Total number of " #a_Lower " intructions with status flag updating", \
                        "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "Total", idCpu); \
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "Total", idCpu); \
        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "Postponed", idCpu); \
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, false, szPat, \
                               "Postponed all status flag updating, " #a_Lower " instructions, percentage", \
                               "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "PostponedPct", idCpu); \
        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "Skipped", idCpu); \
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, false, szPat, \
                               "Skipped all status flag updating, " #a_Lower " instructions, percentage", \
                               "/IEM/CPU%u/re/NativeEFlags/" #a_Camel "SkippedPct", idCpu); \
    } while (0)
        REG_NATIVE_EFL_GROUP(arithmetic, Arithmetic);
        REG_NATIVE_EFL_GROUP(logical,    Logical);
        REG_NATIVE_EFL_GROUP(shift,      Shift);
#   undef REG_NATIVE_EFL_GROUP

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeEflPostponedEmits, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                        "Postponed EFLAGS calculation emits", "/IEM/CPU%u/re/NativeEFlags/ZZEmits", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflCfSkippable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Skippable EFLAGS.CF updating",       "/IEM/CPU%u/re/NativeLivenessEFlags/CfSkippable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflPfSkippable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Skippable EFLAGS.PF updating",       "/IEM/CPU%u/re/NativeLivenessEFlags/PfSkippable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflAfSkippable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Skippable EFLAGS.AF updating",       "/IEM/CPU%u/re/NativeLivenessEFlags/AfSkippable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflZfSkippable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Skippable EFLAGS.ZF updating",       "/IEM/CPU%u/re/NativeLivenessEFlags/ZfSkippable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflSfSkippable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Skippable EFLAGS.SF updating",       "/IEM/CPU%u/re/NativeLivenessEFlags/SfSkippable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflOfSkippable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Skippable EFLAGS.OF updating",       "/IEM/CPU%u/re/NativeLivenessEFlags/OfSkippable", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflCfRequired,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Required EFLAGS.CF updating",        "/IEM/CPU%u/re/NativeLivenessEFlags/CfRequired", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflPfRequired,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Required EFLAGS.PF updating",        "/IEM/CPU%u/re/NativeLivenessEFlags/PfRequired", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflAfRequired,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Required EFLAGS.AF updating",        "/IEM/CPU%u/re/NativeLivenessEFlags/AfRequired", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflZfRequired,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Required EFLAGS.ZF updating",        "/IEM/CPU%u/re/NativeLivenessEFlags/ZfRequired", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflSfRequired,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Required EFLAGS.SF updating",        "/IEM/CPU%u/re/NativeLivenessEFlags/SfRequired", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflOfRequired,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Required EFLAGS.OF updating",        "/IEM/CPU%u/re/NativeLivenessEFlags/OfRequired", idCpu);

#   ifdef IEMLIVENESS_EXTENDED_LAYOUT
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflCfDelayable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Maybe delayable EFLAGS.CF updating", "/IEM/CPU%u/re/NativeLivenessEFlags/CfDelayable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflPfDelayable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Maybe delayable EFLAGS.PF updating", "/IEM/CPU%u/re/NativeLivenessEFlags/PfDelayable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflAfDelayable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Maybe delayable EFLAGS.AF updating", "/IEM/CPU%u/re/NativeLivenessEFlags/AfDelayable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflZfDelayable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Maybe delayable EFLAGS.ZF updating", "/IEM/CPU%u/re/NativeLivenessEFlags/ZfDelayable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflSfDelayable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Maybe delayable EFLAGS.SF updating", "/IEM/CPU%u/re/NativeLivenessEFlags/SfDelayable", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeLivenessEflOfDelayable,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Maybe delayable EFLAGS.OF updating", "/IEM/CPU%u/re/NativeLivenessEFlags/OfDelayable", idCpu);
#   endif

        /* Sum up all status bits ('_' is a sorting hack). */
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeLivenessEFlags/?fSkippable*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Total skippable EFLAGS status bit updating",
                          "/IEM/CPU%u/re/NativeLivenessEFlags/totalSkippable", idCpu);

        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeLivenessEFlags/?fRequired*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Total required STATUS status bit updating",
                          "/IEM/CPU%u/re/NativeLivenessEFlags/totalRequired", idCpu);

#   ifdef IEMLIVENESS_EXTENDED_LAYOUT
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeLivenessEFlags/?fDelayable*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Total potentially delayable STATUS status bit updating",
                          "/IEM/CPU%u/re/NativeLivenessEFlags/totalDelayable", idCpu);
#   endif

        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeLivenessEFlags/?f*", idCpu);
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat, "Total STATUS status bit events of any kind",
                          "/IEM/CPU%u/re/NativeLivenessEFlags/totalTotal", idCpu);

        /* Corresponding ratios / percentages of the totals. */
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeLivenessEFlags/totalTotal", idCpu);
        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/re/NativeLivenessEFlags/totalSkippable", idCpu);
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, false, szPat,
                               "Total skippable EFLAGS status bit updating percentage",
                               "/IEM/CPU%u/re/NativeLivenessEFlags/totalSkippablePct", idCpu);

        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeLivenessEFlags/totalTotal", idCpu);
        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/re/NativeLivenessEFlags/totalRequired", idCpu);
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, false, szPat,
                               "Total required EFLAGS status bit updating percentage",
                               "/IEM/CPU%u/re/NativeLivenessEFlags/totalRequiredPct", idCpu);

#   ifdef IEMLIVENESS_EXTENDED_LAYOUT
        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/re/NativeLivenessEFlags/totalDelayable", idCpu);
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, false, szPat,
                               "Total potentially delayable EFLAGS status bit updating percentage",
                               "/IEM/CPU%u/re/NativeLivenessEFlags/totalDelayablePct", idCpu);
#   endif

        /* Ratios of individual bits. */
        size_t const offFlagChar = RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeLivenessEFlags/Cf*", idCpu) - 3;
        Assert(szPat[offFlagChar] == 'C');
        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/re/NativeLivenessEFlags/CfSkippable", idCpu);
        Assert(szVal[offFlagChar] == 'C');
        szPat[offFlagChar] = szVal[offFlagChar] = 'C'; STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, true, szPat, "Skippable EFLAGS.CF updating percentage", "/IEM/CPU%u/re/NativeLivenessEFlags/CfSkippablePct", idCpu);
        szPat[offFlagChar] = szVal[offFlagChar] = 'P'; STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, true, szPat, "Skippable EFLAGS.PF updating percentage", "/IEM/CPU%u/re/NativeLivenessEFlags/PfSkippablePct", idCpu);
        szPat[offFlagChar] = szVal[offFlagChar] = 'A'; STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, true, szPat, "Skippable EFLAGS.AF updating percentage", "/IEM/CPU%u/re/NativeLivenessEFlags/AfSkippablePct", idCpu);
        szPat[offFlagChar] = szVal[offFlagChar] = 'Z'; STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, true, szPat, "Skippable EFLAGS.ZF updating percentage", "/IEM/CPU%u/re/NativeLivenessEFlags/ZfSkippablePct", idCpu);
        szPat[offFlagChar] = szVal[offFlagChar] = 'S'; STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, true, szPat, "Skippable EFLAGS.SF updating percentage", "/IEM/CPU%u/re/NativeLivenessEFlags/SfSkippablePct", idCpu);
        szPat[offFlagChar] = szVal[offFlagChar] = 'O'; STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, true, szPat, "Skippable EFLAGS.OF updating percentage", "/IEM/CPU%u/re/NativeLivenessEFlags/OfSkippablePct", idCpu);

        /* PC updates total and skipped, with PCT ratio. */
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativePcUpdateTotal,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Total RIP updates",   "/IEM/CPU%u/re/NativePcUpdateTotal", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativePcUpdateDelayed, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Delayed RIP updates", "/IEM/CPU%u/re/NativePcUpdateDelayed", idCpu);
        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativePcUpdateTotal", idCpu);
        RTStrPrintf(szVal, sizeof(szVal), "/IEM/CPU%u/re/NativePcUpdateDelayed", idCpu);
        STAMR3RegisterPctOfSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, STAMUNIT_PCT, szVal, false, szPat,
                               "Delayed RIP updating percentage",
                               "/IEM/CPU%u/re/NativePcUpdateDelayed_StatusDelayedPct", idCpu);

#  endif /* VBOX_WITH_STATISTICS */
#  ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeEndIfOtherBranchDirty, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "IEM_MC_ENDIF flushing dirty shadow registers for other branch (not good).",
                        "/IEM/CPU%u/re/NativeEndIfOtherBranchDirty", idCpu);
#  endif
#  ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeSimdRegFindFree, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of calls to iemNativeSimdRegAllocFindFree.",
                        "/IEM/CPU%u/re/NativeSimdRegFindFree", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeSimdRegFindFreeVar, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times iemNativeSimdRegAllocFindFree needed to free a variable.",
                        "/IEM/CPU%u/re/NativeSimdRegFindFreeVar", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeSimdRegFindFreeNoVar, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times iemNativeSimdRegAllocFindFree did not needed to free any variables.",
                        "/IEM/CPU%u/re/NativeSimdRegFindFreeNoVar", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeSimdRegFindFreeLivenessUnshadowed, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Times liveness info freeed up shadowed guest registers in iemNativeSimdRegAllocFindFree.",
                        "/IEM/CPU%u/re/NativeSimdRegFindFreeLivenessUnshadowed", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeSimdRegFindFreeLivenessHelped,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Times liveness info helped finding the return register in iemNativeSimdRegAllocFindFree.",
                        "/IEM/CPU%u/re/NativeSimdRegFindFreeLivenessHelped", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeDeviceNotAvailXcptCheckPotential, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Potential IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE() checks",
                        "/IEM/CPU%u/re/NativeMaybeDeviceNotAvailXcptCheckPotential", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeWaitDeviceNotAvailXcptCheckPotential, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Potential IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE() checks",
                        "/IEM/CPU%u/re/NativeMaybeWaitDeviceNotAvailXcptCheckPotential", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeSseXcptCheckPotential,            STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Potential IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() checks",
                        "/IEM/CPU%u/re/NativeMaybeSseXcptCheckPotential", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeAvxXcptCheckPotential,            STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Potential IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() checks",
                        "/IEM/CPU%u/re/NativeMaybeAvxXcptCheckPotential", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeDeviceNotAvailXcptCheckOmitted,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE() checks omitted",
                        "/IEM/CPU%u/re/NativeMaybeDeviceNotAvailXcptCheckOmitted", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeWaitDeviceNotAvailXcptCheckOmitted,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE() checks omitted",
                        "/IEM/CPU%u/re/NativeMaybeWaitDeviceNotAvailXcptCheckOmitted", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeSseXcptCheckOmitted,              STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() checks omitted",
                        "/IEM/CPU%u/re/NativeMaybeSseXcptCheckOmitted", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeMaybeAvxXcptCheckOmitted,              STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() checks omitted",
                        "/IEM/CPU%u/re/NativeMaybeAvxXcptCheckOmitted", idCpu);

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbFinished, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finishes execution completely",
                        "/IEM/CPU%u/re/NativeTbFinished", idCpu);
#  endif /* VBOX_WITH_STATISTICS */
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitReturnBreak, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished through the ReturnBreak label",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitReturnBreakFF, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished through the ReturnBreak label",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreakFF", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitReturnWithFlags, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished through the ReturnWithFlags label",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnWithFlags", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitReturnOtherStatus, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished with some other status value",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnOtherStatus", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitLongJump, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished via long jump / throw",
                        "/IEM/CPU%u/re/NativeTbExit/LongJumps", idCpu);
        /* These end up returning VINF_IEM_REEXEC_BREAK and are thus already counted under NativeTbExit/ReturnBreak: */
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitObsoleteTb, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished through the ObsoleteTb label",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/ObsoleteTb", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatCheckNeedCsLimChecking, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished through the NeedCsLimChecking label",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/NeedCsLimChecking", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatCheckBranchMisses, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished through the CheckBranchMiss label",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/CheckBranchMiss", idCpu);
        /* Raising stuff will either increment NativeTbExit/LongJumps or NativeTbExit/ReturnOtherStatus
           depending on whether VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP is defined: */
#  ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
#   define RAISE_PREFIX "/IEM/CPU%u/re/NativeTbExit/ReturnOtherStatus/"
#  else
#   define RAISE_PREFIX "/IEM/CPU%u/re/NativeTbExit/LongJumps/"
#  endif
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseDe, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a #DE exception",
                        RAISE_PREFIX "RaiseDe", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseUd, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a #UD exception",
                        RAISE_PREFIX "RaiseUd", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseSseRelated, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a SSE related exception",
                        RAISE_PREFIX "RaiseSseRelated", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseAvxRelated, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a AVX related exception",
                        RAISE_PREFIX "RaiseAvxRelated", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseSseAvxFpRelated, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a SSE/AVX floating point related exception",
                        RAISE_PREFIX "RaiseSseAvxFpRelated", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseNm, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a #NM exception",
                        RAISE_PREFIX "RaiseNm", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseGp0, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a #GP(0) exception",
                        RAISE_PREFIX "RaiseGp0", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseMf, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a #MF exception",
                        RAISE_PREFIX "RaiseMf", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitRaiseXf, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of times the TB finished raising a #XF exception",
                        RAISE_PREFIX "RaiseXf", idCpu);

#  ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitLoopFullTb, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Number of full TB loops.",
                        "/IEM/CPU%u/re/NativeTbExit/LoopFullTb", idCpu);
#  endif

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking1Irq, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #1 with IRQ check succeeded",
                        "/IEM/CPU%u/re/NativeTbExit/DirectLinking1Irq", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking1NoIrq, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #1 w/o IRQ check succeeded",
                        "/IEM/CPU%u/re/NativeTbExit/DirectLinking1NoIrq", idCpu);
#  ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking1NoTb, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #1 failed: No TB in lookup table",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking1NoTb", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking1MismatchGCPhysPc, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #1 failed: GCPhysPc mismatch",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking1MismatchGCPhysPc", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking1MismatchFlags, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #1 failed: TB flags mismatch",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking1MismatchFlags", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking1PendingIrq, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #1 failed: IRQ or FF pending",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking1PendingIrq", idCpu);
#  endif

        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking2Irq, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #2 with IRQ check succeeded",
                        "/IEM/CPU%u/re/NativeTbExit/DirectLinking2Irq", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking2NoIrq, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #2 w/o IRQ check succeeded",
                        "/IEM/CPU%u/re/NativeTbExit/DirectLinking2NoIrq", idCpu);
#  ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking2NoTb, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #2 failed: No TB in lookup table",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking2NoTb", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking2MismatchGCPhysPc, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #2 failed: GCPhysPc mismatch",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking2MismatchGCPhysPc", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking2MismatchFlags, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #2 failed: TB flags mismatch",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking2MismatchFlags", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatNativeTbExitDirectLinking2PendingIrq, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "Direct linking #2 failed: IRQ or FF pending",
                        "/IEM/CPU%u/re/NativeTbExit/ReturnBreak/DirectLinking2PendingIrq", idCpu);
#  endif

        RTStrPrintf(szPat, sizeof(szPat), "/IEM/CPU%u/re/NativeTbExit/*", idCpu); /* only immediate children, no sub folders */
        STAMR3RegisterSum(pVM->pUVM, STAMVISIBILITY_ALWAYS, szPat,
                          "Number of times native TB execution finished before the end (not counting thrown memory++ exceptions)",
                          "/IEM/CPU%u/re/NativeTbExit", idCpu);


# endif /* VBOX_WITH_IEM_NATIVE_RECOMPILER */


# ifdef VBOX_WITH_STATISTICS
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatMemMapJmp,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "iemMemMapJmp calls",                   "/IEM/CPU%u/iemMemMapJmp", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatMemMapNoJmp,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "iemMemMap calls",                      "/IEM/CPU%u/iemMemMapNoJmp", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatMemBounceBufferCrossPage, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "iemMemBounceBufferMapCrossPage calls", "/IEM/CPU%u/iemMemMapBounceBufferCrossPage", idCpu);
        STAMR3RegisterF(pVM, &pVCpu->iem.s.StatMemBounceBufferMapPhys, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                        "iemMemBounceBufferMapPhys calls",      "/IEM/CPU%u/iemMemMapBounceBufferMapPhys", idCpu);
# endif


#endif /* VBOX_WITH_IEM_RECOMPILER */

        for (uint32_t i = 0; i < RT_ELEMENTS(pVCpu->iem.s.aStatXcpts); i++)
            STAMR3RegisterF(pVM, &pVCpu->iem.s.aStatXcpts[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                            "", "/IEM/CPU%u/Exceptions/%02x", idCpu, i);
        for (uint32_t i = 0; i < RT_ELEMENTS(pVCpu->iem.s.aStatInts); i++)
            STAMR3RegisterF(pVM, &pVCpu->iem.s.aStatInts[i], STAMTYPE_U32_RESET, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                            "", "/IEM/CPU%u/Interrupts/%02x", idCpu, i);

# if !defined(VBOX_VMM_TARGET_ARMV8) && defined(VBOX_WITH_STATISTICS) && !defined(DOXYGEN_RUNNING)
        /* Instruction statistics: */
#  define IEM_DO_INSTR_STAT(a_Name, a_szDesc) \
            STAMR3RegisterF(pVM, &pVCpu->iem.s.StatsRZ.a_Name, STAMTYPE_U32_RESET, STAMVISIBILITY_USED, \
                            STAMUNIT_COUNT, a_szDesc, "/IEM/CPU%u/instr-RZ/" #a_Name, idCpu); \
            STAMR3RegisterF(pVM, &pVCpu->iem.s.StatsR3.a_Name, STAMTYPE_U32_RESET, STAMVISIBILITY_USED, \
                            STAMUNIT_COUNT, a_szDesc, "/IEM/CPU%u/instr-R3/" #a_Name, idCpu);
#  include "IEMInstructionStatisticsTmpl.h"
#  undef IEM_DO_INSTR_STAT
# endif

# if defined(VBOX_WITH_STATISTICS) && defined(VBOX_WITH_IEM_RECOMPILER) && !defined(VBOX_VMM_TARGET_ARMV8)
        /* Threaded function statistics: */
        for (unsigned i = 1; i < (unsigned)kIemThreadedFunc_End; i++)
            STAMR3RegisterF(pVM, &pVCpu->iem.s.acThreadedFuncStats[i], STAMTYPE_U32_RESET, STAMVISIBILITY_USED,
                            STAMUNIT_COUNT, NULL, "/IEM/CPU%u/ThrdFuncs/%s", idCpu, g_apszIemThreadedFunctionStats[i]);
# endif


        for (unsigned i = 1; i < RT_ELEMENTS(pVCpu->iem.s.aStatAdHoc); i++)
            STAMR3RegisterF(pVM, &pVCpu->iem.s.aStatAdHoc[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                            STAMUNIT_COUNT, NULL, "/IEM/CPU%u/AdHoc/%02u", idCpu, i);

#endif /* !defined(VBOX_VMM_TARGET_ARMV8) && defined(VBOX_WITH_NESTED_HWVIRT_VMX) - quick fix for stupid structure duplication non-sense */
    }

#if !defined(VBOX_VMM_TARGET_ARMV8) && defined(VBOX_WITH_NESTED_HWVIRT_VMX)
    /*
     * Register the per-VM VMX APIC-access page handler type.
     */
    if (pVM->cpum.ro.GuestFeatures.fVmx)
    {
        rc = PGMR3HandlerPhysicalTypeRegister(pVM, PGMPHYSHANDLERKIND_ALL, PGMPHYSHANDLER_F_NOT_IN_HM,
                                              iemVmxApicAccessPageHandler,
                                              "VMX APIC-access page", &pVM->iem.s.hVmxApicAccessPage);
        AssertLogRelRCReturn(rc, rc);
    }
#endif

    DBGFR3InfoRegisterInternalArgv(pVM, "itlb", "IEM instruction TLB", iemR3InfoITlb, DBGFINFO_FLAGS_RUN_ON_EMT);
    DBGFR3InfoRegisterInternalArgv(pVM, "dtlb", "IEM instruction TLB", iemR3InfoDTlb, DBGFINFO_FLAGS_RUN_ON_EMT);
#ifdef IEM_WITH_TLB_TRACE
    DBGFR3InfoRegisterInternalArgv(pVM, "tlbtrace", "IEM TLB trace log", iemR3InfoTlbTrace, DBGFINFO_FLAGS_RUN_ON_EMT);
#endif
#if defined(VBOX_WITH_IEM_RECOMPILER) && !defined(VBOX_VMM_TARGET_ARMV8)
    DBGFR3InfoRegisterInternalArgv(pVM, "tb",    "IEM translation block", iemR3InfoTb, DBGFINFO_FLAGS_RUN_ON_EMT);
    DBGFR3InfoRegisterInternalArgv(pVM, "tbtop", "IEM translation blocks most used or most recently used",
                                   iemR3InfoTbTop, DBGFINFO_FLAGS_RUN_ON_EMT);
#endif
#ifdef VBOX_WITH_DEBUGGER
    iemR3RegisterDebuggerCommands();
#endif

    return VINF_SUCCESS;
}


VMMR3_INT_DECL(int) IEMR3Term(PVM pVM)
{
    NOREF(pVM);
#ifdef IEM_WITH_TLB_TRACE
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU const pVCpu = pVM->apCpusR3[idCpu];
        RTMemPageFree(pVCpu->iem.s.paTlbTraceEntries,
                      RT_BIT_Z(pVCpu->iem.s.cTlbTraceEntriesShift) * sizeof(*pVCpu->iem.s.paTlbTraceEntries));
    }
#endif
#if defined(VBOX_WITH_IEM_NATIVE_RECOMPILER) && defined(VBOX_WITH_SAVE_THREADED_TBS_FOR_PROFILING)
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        iemThreadedSaveTbForProfilingCleanup(pVM->apCpusR3[idCpu]);
#endif
    return VINF_SUCCESS;
}


VMMR3_INT_DECL(void) IEMR3Relocate(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Gets the name of a generic IEM exit code.
 *
 * @returns Pointer to read only string if @a uExit is known, otherwise NULL.
 * @param   uExit               The IEM exit to name.
 */
VMMR3_INT_DECL(const char *) IEMR3GetExitName(uint32_t uExit)
{
    static const char * const s_apszNames[] =
    {
        /* external interrupts */
        "ExtInt 00h", "ExtInt 01h", "ExtInt 02h", "ExtInt 03h", "ExtInt 04h", "ExtInt 05h", "ExtInt 06h", "ExtInt 07h",
        "ExtInt 08h", "ExtInt 09h", "ExtInt 0ah", "ExtInt 0bh", "ExtInt 0ch", "ExtInt 0dh", "ExtInt 0eh", "ExtInt 0fh",
        "ExtInt 10h", "ExtInt 11h", "ExtInt 12h", "ExtInt 13h", "ExtInt 14h", "ExtInt 15h", "ExtInt 16h", "ExtInt 17h",
        "ExtInt 18h", "ExtInt 19h", "ExtInt 1ah", "ExtInt 1bh", "ExtInt 1ch", "ExtInt 1dh", "ExtInt 1eh", "ExtInt 1fh",
        "ExtInt 20h", "ExtInt 21h", "ExtInt 22h", "ExtInt 23h", "ExtInt 24h", "ExtInt 25h", "ExtInt 26h", "ExtInt 27h",
        "ExtInt 28h", "ExtInt 29h", "ExtInt 2ah", "ExtInt 2bh", "ExtInt 2ch", "ExtInt 2dh", "ExtInt 2eh", "ExtInt 2fh",
        "ExtInt 30h", "ExtInt 31h", "ExtInt 32h", "ExtInt 33h", "ExtInt 34h", "ExtInt 35h", "ExtInt 36h", "ExtInt 37h",
        "ExtInt 38h", "ExtInt 39h", "ExtInt 3ah", "ExtInt 3bh", "ExtInt 3ch", "ExtInt 3dh", "ExtInt 3eh", "ExtInt 3fh",
        "ExtInt 40h", "ExtInt 41h", "ExtInt 42h", "ExtInt 43h", "ExtInt 44h", "ExtInt 45h", "ExtInt 46h", "ExtInt 47h",
        "ExtInt 48h", "ExtInt 49h", "ExtInt 4ah", "ExtInt 4bh", "ExtInt 4ch", "ExtInt 4dh", "ExtInt 4eh", "ExtInt 4fh",
        "ExtInt 50h", "ExtInt 51h", "ExtInt 52h", "ExtInt 53h", "ExtInt 54h", "ExtInt 55h", "ExtInt 56h", "ExtInt 57h",
        "ExtInt 58h", "ExtInt 59h", "ExtInt 5ah", "ExtInt 5bh", "ExtInt 5ch", "ExtInt 5dh", "ExtInt 5eh", "ExtInt 5fh",
        "ExtInt 60h", "ExtInt 61h", "ExtInt 62h", "ExtInt 63h", "ExtInt 64h", "ExtInt 65h", "ExtInt 66h", "ExtInt 67h",
        "ExtInt 68h", "ExtInt 69h", "ExtInt 6ah", "ExtInt 6bh", "ExtInt 6ch", "ExtInt 6dh", "ExtInt 6eh", "ExtInt 6fh",
        "ExtInt 70h", "ExtInt 71h", "ExtInt 72h", "ExtInt 73h", "ExtInt 74h", "ExtInt 75h", "ExtInt 76h", "ExtInt 77h",
        "ExtInt 78h", "ExtInt 79h", "ExtInt 7ah", "ExtInt 7bh", "ExtInt 7ch", "ExtInt 7dh", "ExtInt 7eh", "ExtInt 7fh",
        "ExtInt 80h", "ExtInt 81h", "ExtInt 82h", "ExtInt 83h", "ExtInt 84h", "ExtInt 85h", "ExtInt 86h", "ExtInt 87h",
        "ExtInt 88h", "ExtInt 89h", "ExtInt 8ah", "ExtInt 8bh", "ExtInt 8ch", "ExtInt 8dh", "ExtInt 8eh", "ExtInt 8fh",
        "ExtInt 90h", "ExtInt 91h", "ExtInt 92h", "ExtInt 93h", "ExtInt 94h", "ExtInt 95h", "ExtInt 96h", "ExtInt 97h",
        "ExtInt 98h", "ExtInt 99h", "ExtInt 9ah", "ExtInt 9bh", "ExtInt 9ch", "ExtInt 9dh", "ExtInt 9eh", "ExtInt 9fh",
        "ExtInt a0h", "ExtInt a1h", "ExtInt a2h", "ExtInt a3h", "ExtInt a4h", "ExtInt a5h", "ExtInt a6h", "ExtInt a7h",
        "ExtInt a8h", "ExtInt a9h", "ExtInt aah", "ExtInt abh", "ExtInt ach", "ExtInt adh", "ExtInt aeh", "ExtInt afh",
        "ExtInt b0h", "ExtInt b1h", "ExtInt b2h", "ExtInt b3h", "ExtInt b4h", "ExtInt b5h", "ExtInt b6h", "ExtInt b7h",
        "ExtInt b8h", "ExtInt b9h", "ExtInt bah", "ExtInt bbh", "ExtInt bch", "ExtInt bdh", "ExtInt beh", "ExtInt bfh",
        "ExtInt c0h", "ExtInt c1h", "ExtInt c2h", "ExtInt c3h", "ExtInt c4h", "ExtInt c5h", "ExtInt c6h", "ExtInt c7h",
        "ExtInt c8h", "ExtInt c9h", "ExtInt cah", "ExtInt cbh", "ExtInt cch", "ExtInt cdh", "ExtInt ceh", "ExtInt cfh",
        "ExtInt d0h", "ExtInt d1h", "ExtInt d2h", "ExtInt d3h", "ExtInt d4h", "ExtInt d5h", "ExtInt d6h", "ExtInt d7h",
        "ExtInt d8h", "ExtInt d9h", "ExtInt dah", "ExtInt dbh", "ExtInt dch", "ExtInt ddh", "ExtInt deh", "ExtInt dfh",
        "ExtInt e0h", "ExtInt e1h", "ExtInt e2h", "ExtInt e3h", "ExtInt e4h", "ExtInt e5h", "ExtInt e6h", "ExtInt e7h",
        "ExtInt e8h", "ExtInt e9h", "ExtInt eah", "ExtInt ebh", "ExtInt ech", "ExtInt edh", "ExtInt eeh", "ExtInt efh",
        "ExtInt f0h", "ExtInt f1h", "ExtInt f2h", "ExtInt f3h", "ExtInt f4h", "ExtInt f5h", "ExtInt f6h", "ExtInt f7h",
        "ExtInt f8h", "ExtInt f9h", "ExtInt fah", "ExtInt fbh", "ExtInt fch", "ExtInt fdh", "ExtInt feh", "ExtInt ffh",
        /* software interrups */
        "SoftInt 00h", "SoftInt 01h", "SoftInt 02h", "SoftInt 03h", "SoftInt 04h", "SoftInt 05h", "SoftInt 06h", "SoftInt 07h",
        "SoftInt 08h", "SoftInt 09h", "SoftInt 0ah", "SoftInt 0bh", "SoftInt 0ch", "SoftInt 0dh", "SoftInt 0eh", "SoftInt 0fh",
        "SoftInt 10h", "SoftInt 11h", "SoftInt 12h", "SoftInt 13h", "SoftInt 14h", "SoftInt 15h", "SoftInt 16h", "SoftInt 17h",
        "SoftInt 18h", "SoftInt 19h", "SoftInt 1ah", "SoftInt 1bh", "SoftInt 1ch", "SoftInt 1dh", "SoftInt 1eh", "SoftInt 1fh",
        "SoftInt 20h", "SoftInt 21h", "SoftInt 22h", "SoftInt 23h", "SoftInt 24h", "SoftInt 25h", "SoftInt 26h", "SoftInt 27h",
        "SoftInt 28h", "SoftInt 29h", "SoftInt 2ah", "SoftInt 2bh", "SoftInt 2ch", "SoftInt 2dh", "SoftInt 2eh", "SoftInt 2fh",
        "SoftInt 30h", "SoftInt 31h", "SoftInt 32h", "SoftInt 33h", "SoftInt 34h", "SoftInt 35h", "SoftInt 36h", "SoftInt 37h",
        "SoftInt 38h", "SoftInt 39h", "SoftInt 3ah", "SoftInt 3bh", "SoftInt 3ch", "SoftInt 3dh", "SoftInt 3eh", "SoftInt 3fh",
        "SoftInt 40h", "SoftInt 41h", "SoftInt 42h", "SoftInt 43h", "SoftInt 44h", "SoftInt 45h", "SoftInt 46h", "SoftInt 47h",
        "SoftInt 48h", "SoftInt 49h", "SoftInt 4ah", "SoftInt 4bh", "SoftInt 4ch", "SoftInt 4dh", "SoftInt 4eh", "SoftInt 4fh",
        "SoftInt 50h", "SoftInt 51h", "SoftInt 52h", "SoftInt 53h", "SoftInt 54h", "SoftInt 55h", "SoftInt 56h", "SoftInt 57h",
        "SoftInt 58h", "SoftInt 59h", "SoftInt 5ah", "SoftInt 5bh", "SoftInt 5ch", "SoftInt 5dh", "SoftInt 5eh", "SoftInt 5fh",
        "SoftInt 60h", "SoftInt 61h", "SoftInt 62h", "SoftInt 63h", "SoftInt 64h", "SoftInt 65h", "SoftInt 66h", "SoftInt 67h",
        "SoftInt 68h", "SoftInt 69h", "SoftInt 6ah", "SoftInt 6bh", "SoftInt 6ch", "SoftInt 6dh", "SoftInt 6eh", "SoftInt 6fh",
        "SoftInt 70h", "SoftInt 71h", "SoftInt 72h", "SoftInt 73h", "SoftInt 74h", "SoftInt 75h", "SoftInt 76h", "SoftInt 77h",
        "SoftInt 78h", "SoftInt 79h", "SoftInt 7ah", "SoftInt 7bh", "SoftInt 7ch", "SoftInt 7dh", "SoftInt 7eh", "SoftInt 7fh",
        "SoftInt 80h", "SoftInt 81h", "SoftInt 82h", "SoftInt 83h", "SoftInt 84h", "SoftInt 85h", "SoftInt 86h", "SoftInt 87h",
        "SoftInt 88h", "SoftInt 89h", "SoftInt 8ah", "SoftInt 8bh", "SoftInt 8ch", "SoftInt 8dh", "SoftInt 8eh", "SoftInt 8fh",
        "SoftInt 90h", "SoftInt 91h", "SoftInt 92h", "SoftInt 93h", "SoftInt 94h", "SoftInt 95h", "SoftInt 96h", "SoftInt 97h",
        "SoftInt 98h", "SoftInt 99h", "SoftInt 9ah", "SoftInt 9bh", "SoftInt 9ch", "SoftInt 9dh", "SoftInt 9eh", "SoftInt 9fh",
        "SoftInt a0h", "SoftInt a1h", "SoftInt a2h", "SoftInt a3h", "SoftInt a4h", "SoftInt a5h", "SoftInt a6h", "SoftInt a7h",
        "SoftInt a8h", "SoftInt a9h", "SoftInt aah", "SoftInt abh", "SoftInt ach", "SoftInt adh", "SoftInt aeh", "SoftInt afh",
        "SoftInt b0h", "SoftInt b1h", "SoftInt b2h", "SoftInt b3h", "SoftInt b4h", "SoftInt b5h", "SoftInt b6h", "SoftInt b7h",
        "SoftInt b8h", "SoftInt b9h", "SoftInt bah", "SoftInt bbh", "SoftInt bch", "SoftInt bdh", "SoftInt beh", "SoftInt bfh",
        "SoftInt c0h", "SoftInt c1h", "SoftInt c2h", "SoftInt c3h", "SoftInt c4h", "SoftInt c5h", "SoftInt c6h", "SoftInt c7h",
        "SoftInt c8h", "SoftInt c9h", "SoftInt cah", "SoftInt cbh", "SoftInt cch", "SoftInt cdh", "SoftInt ceh", "SoftInt cfh",
        "SoftInt d0h", "SoftInt d1h", "SoftInt d2h", "SoftInt d3h", "SoftInt d4h", "SoftInt d5h", "SoftInt d6h", "SoftInt d7h",
        "SoftInt d8h", "SoftInt d9h", "SoftInt dah", "SoftInt dbh", "SoftInt dch", "SoftInt ddh", "SoftInt deh", "SoftInt dfh",
        "SoftInt e0h", "SoftInt e1h", "SoftInt e2h", "SoftInt e3h", "SoftInt e4h", "SoftInt e5h", "SoftInt e6h", "SoftInt e7h",
        "SoftInt e8h", "SoftInt e9h", "SoftInt eah", "SoftInt ebh", "SoftInt ech", "SoftInt edh", "SoftInt eeh", "SoftInt efh",
        "SoftInt f0h", "SoftInt f1h", "SoftInt f2h", "SoftInt f3h", "SoftInt f4h", "SoftInt f5h", "SoftInt f6h", "SoftInt f7h",
        "SoftInt f8h", "SoftInt f9h", "SoftInt fah", "SoftInt fbh", "SoftInt fch", "SoftInt fdh", "SoftInt feh", "SoftInt ffh",
    };
    if (uExit < RT_ELEMENTS(s_apszNames))
        return s_apszNames[uExit];
    return NULL;
}


/** Worker for iemR3InfoTlbPrintSlots and iemR3InfoTlbPrintAddress. */
static void iemR3InfoTlbPrintHeader(PVMCPU pVCpu, PCDBGFINFOHLP pHlp, IEMTLB const *pTlb, bool *pfHeader)
{
    if (*pfHeader)
        return;
    pHlp->pfnPrintf(pHlp, "%cTLB for CPU %u:\n", &pVCpu->iem.s.CodeTlb == pTlb ? 'I' : 'D', pVCpu->idCpu);
    *pfHeader = true;
}


#define IEMR3INFOTLB_F_ONLY_VALID   RT_BIT_32(0)
#define IEMR3INFOTLB_F_CHECK        RT_BIT_32(1)

/** Worker for iemR3InfoTlbPrintSlots and iemR3InfoTlbPrintAddress. */
static void iemR3InfoTlbPrintSlot(PVMCPU pVCpu, PCDBGFINFOHLP pHlp, IEMTLB const *pTlb, IEMTLBENTRY const *pTlbe,
                                  uint32_t uSlot, uint32_t fFlags)
{
#ifndef VBOX_VMM_TARGET_ARMV8
    uint64_t const uTlbRevision = !(uSlot & 1) ? pTlb->uTlbRevision : pTlb->uTlbRevisionGlobal;
#else
    uint64_t const uTlbRevision = pTlb->uTlbRevision;
#endif
    if ((fFlags & IEMR3INFOTLB_F_ONLY_VALID) && (pTlbe->uTag & IEMTLB_REVISION_MASK) != uTlbRevision)
        return;

    /* The address needs to be sign extended, thus the shifting fun here.*/
    RTGCPTR const  GCPtr = (RTGCINTPTR)((pTlbe->uTag & ~IEMTLB_REVISION_MASK) << (64 - IEMTLB_TAG_ADDR_WIDTH))
                         >>                                                      (64 - IEMTLB_TAG_ADDR_WIDTH - GUEST_PAGE_SHIFT);
    const char    *pszValid = "";
#ifndef VBOX_VMM_TARGET_ARMV8
    char           szTmp[128];
    if (fFlags & IEMR3INFOTLB_F_CHECK)
    {
#ifdef VBOX_VMM_TARGET_X86
        uint32_t const fInvSlotG = (uint32_t)!(uSlot & 1) << X86_PTE_BIT_G;
#endif
        PGMPTWALKFAST  WalkFast;
        int rc = PGMGstQueryPageFast(pVCpu, GCPtr, 0 /*fFlags - don't check or modify anything */, &WalkFast);
        pszValid = szTmp;
        if (RT_FAILURE(rc))
            switch (rc)
            {
                case VERR_PAGE_TABLE_NOT_PRESENT:
#ifdef VBOX_VMM_TARGET_X86
                    switch ((WalkFast.fFailed & PGM_WALKFAIL_LEVEL_MASK) >> PGM_WALKFAIL_LEVEL_SHIFT)
                    {
                        case 1:  pszValid = " stale(page-not-present)"; break;
                        case 2:  pszValid = " stale(pd-entry-not-present)"; break;
                        case 3:  pszValid = " stale(pdptr-entry-not-present)"; break;
                        case 4:  pszValid = " stale(pml4-entry-not-present)"; break;
                        case 5:  pszValid = " stale(pml5-entry-not-present)"; break;
                        default: pszValid = " stale(VERR_PAGE_TABLE_NOT_PRESENT)"; break;
                    }
                    break;
#else
                    RT_FALL_THRU(); /** @todo  */
#endif
                default: RTStrPrintf(szTmp, sizeof(szTmp), " stale(rc=%d)", rc); break;
            }
        else if (WalkFast.GCPhys != pTlbe->GCPhys)
            RTStrPrintf(szTmp, sizeof(szTmp), " stale(GCPhys=%RGp)", WalkFast.GCPhys);
#ifdef VBOX_VMM_TARGET_X86
        else if (   (~WalkFast.fEffective       & (X86_PTE_RW | X86_PTE_US | X86_PTE_G | X86_PTE_A | X86_PTE_D))
                 == (  (pTlbe->fFlagsAndPhysRev & (  IEMTLBE_F_PT_NO_WRITE | IEMTLBE_F_PT_NO_USER
                                                   | IEMTLBE_F_PT_NO_DIRTY | IEMTLBE_F_PT_NO_ACCESSED))
                     | fInvSlotG ) )
            pszValid = " still-valid";
        else if (   (~WalkFast.fEffective     & (X86_PTE_RW | X86_PTE_US | X86_PTE_G))
                 == ((pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_PT_NO_WRITE | IEMTLBE_F_PT_NO_USER)) | fInvSlotG) )
            switch (  (~WalkFast.fEffective    & (X86_PTE_A | X86_PTE_D))
                    ^ (pTlbe->fFlagsAndPhysRev & (IEMTLBE_F_PT_NO_DIRTY | IEMTLBE_F_PT_NO_ACCESSED)) )
            {
                case X86_PTE_A:
                    pszValid = WalkFast.fEffective & X86_PTE_A ? " still-valid(accessed-now)" : " still-valid(accessed-no-more)";
                    break;
                case X86_PTE_D:
                    pszValid = WalkFast.fEffective & X86_PTE_D ? " still-valid(dirty-now)" : " still-valid(dirty-no-more)";
                    break;
                case X86_PTE_D | X86_PTE_A:
                    RTStrPrintf(szTmp, sizeof(szTmp), " still-valid(%s%s)",
                                (~WalkFast.fEffective & X86_PTE_D) == (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_DIRTY) ? ""
                                : WalkFast.fEffective & X86_PTE_D ? "dirty-now"     : "dirty-no-more",
                                (~WalkFast.fEffective & X86_PTE_A) == (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_ACCESSED) ? ""
                                : WalkFast.fEffective & X86_PTE_A ? " accessed-now" : " accessed-no-more");
                    break;
                default: AssertFailed(); break;
            }
        else
            RTStrPrintf(szTmp, sizeof(szTmp), " stale(%s%s%s%s%s)",
                        (~WalkFast.fEffective & X86_PTE_RW) == (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_WRITE) ? ""
                        : WalkFast.fEffective & X86_PTE_RW ? "writeable-now" : "writable-no-more",
                        (~WalkFast.fEffective & X86_PTE_US) == (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_USER) ? ""
                        : WalkFast.fEffective & X86_PTE_US ? " user-now"     : " user-no-more",
                        (~WalkFast.fEffective & X86_PTE_G)  == fInvSlotG ? ""
                        : WalkFast.fEffective & X86_PTE_G  ? " global-now"   : " global-no-more",
                        (~WalkFast.fEffective & X86_PTE_D)  == (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_DIRTY) ? ""
                        : WalkFast.fEffective & X86_PTE_D  ? " dirty-now"    : " dirty-no-more",
                        (~WalkFast.fEffective & X86_PTE_A)  == (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_ACCESSED) ? ""
                        : WalkFast.fEffective & X86_PTE_A  ? " accessed-now" : " accessed-no-more");
#elif defined(VBOX_VMM_TARGET_ARMV8)
        else
            RTStrPrintf(szTmp, sizeof(szTmp), " stale(todo)");
#else
# error "port me"
#endif
    }
#else
    RT_NOREF(pVCpu);
#endif

#ifdef VBOX_VMM_TARGET_X86
    pHlp->pfnPrintf(pHlp, IEMTLB_SLOT_FMT ": %s %#018RX64 -> %RGp / %p / %#05x %s%s%s%s%s%s%s/%s%s%s%s/%s %s%s\n",
                    uSlot,
                    (pTlbe->uTag & IEMTLB_REVISION_MASK) == uTlbRevision ? "valid  "
                    : (pTlbe->uTag & IEMTLB_REVISION_MASK) == 0          ? "empty  "
                                                                         : "expired",
                    GCPtr, /* -> */
                    pTlbe->GCPhys, /* / */ pTlbe->pbMappingR3,
                    /* / */
                    (uint32_t)(pTlbe->fFlagsAndPhysRev & ~IEMTLBE_F_PHYS_REV),
                    /* */
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_WRITE      ? "R-" : "RW",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_EXEC       ? "-"  : "X",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_ACCESSED   ? "-"  : "A",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_DIRTY      ? "-"  : "D",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_NO_USER       ? "U"  : "S",
                    !(uSlot & 1)                                         ? "-"  : "G",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PT_LARGE_PAGE    ? "4K" : "2M",
                    /* / */
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_NO_WRITE      ? "-"  : "w",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_NO_READ       ? "-"  : "r",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_UNASSIGNED    ? "u"  : "-",
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_CODE_PAGE     ? "c"  : "-",
                    /* / */
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_NO_MAPPINGR3     ? "N"  : "M",
                    (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == pTlb->uTlbPhysRev ? "phys-valid"
                    : (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == 0 ? "phys-empty" : "phys-expired",
                    pszValid);
#elif defined(VBOX_VMM_TARGET_ARMV8)
    static const char * const s_apszRegimes[16] =
    {
        "NsEL10",
        "NsEL10S2",
        "SecEL10",
        "SecEL10S2",
        "RealmEL10S2",
        "NsEL20",
        "SecEL20",
        "RealmEL20",
        "NsEL2",
        "SecEL2",
        "RealmEL2",
        "RootEL3",
    };

    uint64_t const uTlbPhysRevAndStuff = (pTlbe->uTag & RT_BIT_64(IEMTLB_TAG_ADDR_WIDTH - 1)) == 0
                                       ? pTlb->uTlbPhysRevAndStuff0 : pTlb->uTlbPhysRevAndStuff1;
    pHlp->pfnPrintf(pHlp, IEMTLB_SLOT_FMT
                    ": %s %#018RX64 -> %RGp / %p / %#05x U%c%c%c%cP%c%c%c%c%c%c%c/%c%c/%c%c%c%c/%c as:%x vm:%x/%s %s%s\n",
                    uSlot,
                    (pTlbe->uTag & IEMTLB_REVISION_MASK) == uTlbRevision ? "valid  "
                    : (pTlbe->uTag & IEMTLB_REVISION_MASK) == 0          ? "empty  "
                                                                         : "expired",
                    GCPtr, /* -> */
                    pTlbe->GCPhys, /* / */ pTlbe->pbMappingR3,
                    /* / */
                    (uint32_t)(pTlbe->fFlagsAndPhysRev & ~(IEMTLBE_F_PHYS_REV | IEMTLBE_F_S1_ASID | IEMTLBE_F_S2_VMID)),
                    /* */
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_P_NO_READ    ? '-' : 'r',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_P_NO_WRITE   ? '-' : 'w',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_P_NO_EXEC    ? '-' : 'x',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_P_NO_GCS     ? '-' : 's',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_U_NO_READ    ? '-' : 'r',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_U_NO_WRITE   ? '-' : 'w',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_U_NO_EXEC    ? '-' : 'x',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_U_NO_GCS     ? '-' : 's',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_NO_DIRTY     ? '-' : 'D',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_AMEC         ? 'A' : '-',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_EFF_DEVICE       ? 'd' : '-',
                    /* / */
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_NG               ? '-' : 'G',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_GP               ? 'P' : '-',
                    /* / */
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_NO_READ       ? '-'  : 'r',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_NO_WRITE      ? '-'  : 'w',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_UNASSIGNED    ? 'u'  : '-',
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PG_CODE_PAGE     ? 'c'  : '-',
                    /* / */
                    pTlbe->fFlagsAndPhysRev & IEMTLBE_F_NO_MAPPINGR3     ? 'N'  : 'M',
                    /*  as: */
                    (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_S1_ASID) >> IEMTLBE_F_S1_ASID_SHIFT,
                    /*  vm: */
                    (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_S2_VMID) >> IEMTLBE_F_S2_VMID_SHIFT,
                    s_apszRegimes[(pTlbe->fFlagsAndPhysRev & IEMTLBE_F_REGIME_MASK) >> IEMTLBE_F_REGIME_SHIFT],
                    /*  */
                    (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == (uTlbPhysRevAndStuff & IEMTLBE_F_PHYS_REV) ? "phys-valid"
                    : (pTlbe->fFlagsAndPhysRev & IEMTLBE_F_PHYS_REV) == 0 ? "phys-empty" : "phys-expired",
                    pszValid);
#else
# error "port me"
#endif
}


/** Displays one or more TLB slots. */
static void iemR3InfoTlbPrintSlots(PVMCPU pVCpu, PCDBGFINFOHLP pHlp, IEMTLB const *pTlb,
                                   uint32_t uSlot, uint32_t cSlots, uint32_t fFlags, bool *pfHeader)
{
    if (uSlot < RT_ELEMENTS(pTlb->aEntries))
    {
        if (cSlots > RT_ELEMENTS(pTlb->aEntries))
        {
            pHlp->pfnPrintf(pHlp, "error: Too many slots given: %u, adjusting it down to the max (%u)\n",
                            cSlots, RT_ELEMENTS(pTlb->aEntries));
            cSlots = RT_ELEMENTS(pTlb->aEntries);
        }

        iemR3InfoTlbPrintHeader(pVCpu, pHlp, pTlb, pfHeader);
        while (cSlots-- > 0)
        {
            IEMTLBENTRY const Tlbe = pTlb->aEntries[uSlot];
            iemR3InfoTlbPrintSlot(pVCpu, pHlp, pTlb, &Tlbe, uSlot, fFlags);
            uSlot = (uSlot + 1) % RT_ELEMENTS(pTlb->aEntries);
        }
    }
    else
        pHlp->pfnPrintf(pHlp, "error: TLB slot is out of range: %u (%#x), max %u (%#x)\n",
                        uSlot, uSlot, RT_ELEMENTS(pTlb->aEntries) - 1, RT_ELEMENTS(pTlb->aEntries) - 1);
}


/** Displays the TLB slot for the given address. */
static void iemR3InfoTlbPrintAddress(PVMCPU pVCpu, PCDBGFINFOHLP pHlp, IEMTLB const *pTlb,
                                     uint64_t uAddress, uint32_t fFlags, bool *pfHeader)
{
    iemR3InfoTlbPrintHeader(pVCpu, pHlp, pTlb, pfHeader);

    uint64_t const    uTag  = IEMTLB_CALC_TAG_NO_REV(pVCpu, uAddress);
    uint32_t const    uSlot = IEMTLB_TAG_TO_INDEX(uTag);
    AssertCompile(IEMTLB_ENTRY_COUNT_FACTOR == 1 || IEMTLB_ENTRY_COUNT_FACTOR == 2);
    IEMTLBENTRY const Tlbe0 = pTlb->aEntries[uSlot];
#if IEMTLB_ENTRY_COUNT_FACTOR >= 2
    IEMTLBENTRY const Tlbe1 = pTlb->aEntries[uSlot + 1];
#endif
    pHlp->pfnPrintf(pHlp, "Address %#RX64 -> slot %#x - %s\n", uAddress, uSlot,
                    Tlbe0.uTag == (uTag | pTlb->uTlbRevision)  ? "match"
                    : (Tlbe0.uTag & ~IEMTLB_REVISION_MASK) == uTag ? "expired" : "mismatch");
    iemR3InfoTlbPrintSlot(pVCpu, pHlp, pTlb, &Tlbe0, uSlot, fFlags);

#if IEMTLB_ENTRY_COUNT_FACTOR >= 2
    pHlp->pfnPrintf(pHlp, "Address %#RX64 -> slot %#x - %s\n", uAddress, uSlot + 1,
                    Tlbe1.uTag == (uTag | pTlb->uTlbRevisionGlobal)  ? "match"
                    : (Tlbe1.uTag & ~IEMTLB_REVISION_MASK) == uTag ? "expired" : "mismatch");
    iemR3InfoTlbPrintSlot(pVCpu, pHlp, pTlb, &Tlbe1, uSlot + 1, fFlags);
# if IEMTLB_ENTRY_COUNT_FACTOR > 2
#  error IEMTLB_ENTRY_COUNT_FACTOR
# endif
#endif
}


/** Common worker for iemR3InfoDTlb and iemR3InfoITlb. */
static void iemR3InfoTlbCommon(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs, bool fITlb)
{
    /*
     * This is entirely argument driven.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--cpu",          'c', RTGETOPT_REQ_UINT32                          },
        { "--vcpu",         'c', RTGETOPT_REQ_UINT32                          },
        { "--check",        'C', RTGETOPT_REQ_NOTHING                         },
        { "all",            'A', RTGETOPT_REQ_NOTHING                         },
        { "--all",          'A', RTGETOPT_REQ_NOTHING                         },
        { "--address",      'a', RTGETOPT_REQ_UINT64      | RTGETOPT_FLAG_HEX },
        { "--range",        'r', RTGETOPT_REQ_UINT32_PAIR | RTGETOPT_FLAG_HEX },
        { "--slot",         's', RTGETOPT_REQ_UINT32      | RTGETOPT_FLAG_HEX },
        { "--only-valid",   'v', RTGETOPT_REQ_NOTHING                         },
    };

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 0 /*iFirst*/, 0 /*fFlags*/);
    AssertRCReturnVoid(rc);

    uint32_t        cActionArgs  = 0;
    bool            fNeedHeader  = true;
    bool            fAddressMode = true;
    uint32_t        fFlags       = 0;
    PVMCPU const    pVCpuCall    = VMMGetCpu(pVM);
    PVMCPU          pVCpu        = pVCpuCall;
    if (!pVCpu)
        pVCpu = VMMGetCpuById(pVM, 0);

    RTGETOPTUNION   ValueUnion;
    while ((rc = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'c':
                if (ValueUnion.u32 >= pVM->cCpus)
                    pHlp->pfnPrintf(pHlp, "error: Invalid CPU ID: %u\n", ValueUnion.u32);
                else if (!pVCpu || pVCpu->idCpu != ValueUnion.u32)
                {
                    pVCpu = VMMGetCpuById(pVM, ValueUnion.u32);
                    fNeedHeader = true;
                    if (!pVCpuCall || pVCpuCall->idCpu != ValueUnion.u32)
                    {
                        pHlp->pfnPrintf(pHlp, "info: Can't check guest PTs when switching to a different VCpu! Targetting %u, on %u.\n",
                                        ValueUnion.u32, pVCpuCall->idCpu);
                        fFlags &= ~IEMR3INFOTLB_F_CHECK;
                    }
                }
                break;

            case 'C':
                if (!pVCpuCall)
                    pHlp->pfnPrintf(pHlp, "error: Can't check guest PT when not running on an EMT!\n");
                else if (pVCpu != pVCpuCall)
                    pHlp->pfnPrintf(pHlp, "error: Can't check guest PTs when on a different EMT! Targetting %u, on %u.\n",
                                    pVCpu->idCpu, pVCpuCall->idCpu);
                else
                    fFlags |= IEMR3INFOTLB_F_CHECK;
                break;

            case 'a':
                iemR3InfoTlbPrintAddress(pVCpu, pHlp, fITlb ? &pVCpu->iem.s.CodeTlb : &pVCpu->iem.s.DataTlb,
                                         ValueUnion.u64, fFlags, &fNeedHeader);
                fAddressMode = true;
                cActionArgs++;
                break;

            case 'A':
                iemR3InfoTlbPrintSlots(pVCpu, pHlp, fITlb ? &pVCpu->iem.s.CodeTlb : &pVCpu->iem.s.DataTlb,
                                       0, RT_ELEMENTS(pVCpu->iem.s.CodeTlb.aEntries), fFlags, &fNeedHeader);
                cActionArgs++;
                break;

            case 'r':
                iemR3InfoTlbPrintSlots(pVCpu, pHlp, fITlb ? &pVCpu->iem.s.CodeTlb : &pVCpu->iem.s.DataTlb,
                                       ValueUnion.PairU32.uFirst, ValueUnion.PairU32.uSecond, fFlags, &fNeedHeader);
                fAddressMode = false;
                cActionArgs++;
                break;

            case 's':
                iemR3InfoTlbPrintSlots(pVCpu, pHlp, fITlb ? &pVCpu->iem.s.CodeTlb : &pVCpu->iem.s.DataTlb,
                                       ValueUnion.u32, 1, fFlags, &fNeedHeader);
                fAddressMode = false;
                cActionArgs++;
                break;

            case 'v':
                fFlags |= IEMR3INFOTLB_F_ONLY_VALID;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (fAddressMode)
                {
                    uint64_t uAddr;
                    rc = RTStrToUInt64Full(ValueUnion.psz, 16, &uAddr);
                    if (RT_SUCCESS(rc) && rc != VWRN_NUMBER_TOO_BIG)
                        iemR3InfoTlbPrintAddress(pVCpu, pHlp, fITlb ? &pVCpu->iem.s.CodeTlb : &pVCpu->iem.s.DataTlb,
                                                 uAddr, fFlags, &fNeedHeader);
                    else
                        pHlp->pfnPrintf(pHlp, "error: Invalid or malformed guest address '%s': %Rrc\n", ValueUnion.psz, rc);
                }
                else
                {
                    uint32_t uSlot;
                    rc = RTStrToUInt32Full(ValueUnion.psz, 16, &uSlot);
                    if (RT_SUCCESS(rc) && rc != VWRN_NUMBER_TOO_BIG)
                        iemR3InfoTlbPrintSlots(pVCpu, pHlp, fITlb ? &pVCpu->iem.s.CodeTlb : &pVCpu->iem.s.DataTlb,
                                               uSlot, 1, fFlags, &fNeedHeader);
                    else
                        pHlp->pfnPrintf(pHlp, "error: Invalid or malformed TLB slot number '%s': %Rrc\n", ValueUnion.psz, rc);
                }
                cActionArgs++;
                break;

            case 'h':
                pHlp->pfnPrintf(pHlp,
                                "Usage: info %ctlb [options]\n"
                                "\n"
                                "Options:\n"
                                "  -c<n>, --cpu=<n>, --vcpu=<n>\n"
                                "    Selects the CPU which TLBs we're looking at. Default: Caller / 0\n"
                                "  -C,--check\n"
                                "    Check valid entries against guest PTs.\n"
                                "  -A, --all, all\n"
                                "    Display all the TLB entries (default if no other args).\n"
                                "  -a<virt>, --address=<virt>\n"
                                "    Shows the TLB entry for the specified guest virtual address.\n"
                                "  -r<slot:count>, --range=<slot:count>\n"
                                "    Shows the TLB entries for the specified slot range.\n"
                                "  -s<slot>,--slot=<slot>\n"
                                "    Shows the given TLB slot.\n"
                                "  -v,--only-valid\n"
                                "    Only show valid TLB entries (TAG, not phys)\n"
                                "\n"
                                "Non-options are interpreted according to the last -a, -r or -s option,\n"
                                "defaulting to addresses if not preceded by any of those options.\n"
                                , fITlb ? 'i' : 'd');
                return;

            default:
                pHlp->pfnGetOptError(pHlp, rc, &ValueUnion, &State);
                return;
        }
    }

    /*
     * If no action taken, we display all (-A) by default.
     */
    if (!cActionArgs)
        iemR3InfoTlbPrintSlots(pVCpu, pHlp, fITlb ? &pVCpu->iem.s.CodeTlb : &pVCpu->iem.s.DataTlb,
                               0, RT_ELEMENTS(pVCpu->iem.s.CodeTlb.aEntries), fFlags, &fNeedHeader);
}


/**
 * @callback_method_impl{FNDBGFINFOARGVINT, itlb}
 */
static DECLCALLBACK(void) iemR3InfoITlb(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs)
{
    return iemR3InfoTlbCommon(pVM, pHlp, cArgs, papszArgs, true /*fITlb*/);
}


/**
 * @callback_method_impl{FNDBGFINFOARGVINT, dtlb}
 */
static DECLCALLBACK(void) iemR3InfoDTlb(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs)
{
    return iemR3InfoTlbCommon(pVM, pHlp, cArgs, papszArgs, false /*fITlb*/);
}


#ifdef IEM_WITH_TLB_TRACE
/**
 * @callback_method_impl{FNDBGFINFOARGVINT, tlbtrace}
 */
static DECLCALLBACK(void) iemR3InfoTlbTrace(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--cpu",                      'c', RTGETOPT_REQ_UINT32  },
        { "--vcpu",                     'c', RTGETOPT_REQ_UINT32  },
        { "--last",                     'l', RTGETOPT_REQ_UINT32  },
        { "--limit",                    'l', RTGETOPT_REQ_UINT32  },
        { "--stop-at-global-flush",     'g', RTGETOPT_REQ_NOTHING },
        { "--resolve-rip",              'r', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 0 /*iFirst*/, 0 /*fFlags*/);
    AssertRCReturnVoid(rc);

    uint32_t        cLimit             = UINT32_MAX;
    bool            fStopAtGlobalFlush = false;
    bool            fResolveRip        = false;
    PVMCPU const    pVCpuCall          = VMMGetCpu(pVM);
    PVMCPU          pVCpu              = pVCpuCall;
    if (!pVCpu)
        pVCpu = VMMGetCpuById(pVM, 0);

    RTGETOPTUNION   ValueUnion;
    while ((rc = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'c':
                if (ValueUnion.u32 >= pVM->cCpus)
                    pHlp->pfnPrintf(pHlp, "error: Invalid CPU ID: %u\n", ValueUnion.u32);
                else if (!pVCpu || pVCpu->idCpu != ValueUnion.u32)
                    pVCpu = VMMGetCpuById(pVM, ValueUnion.u32);
                break;

            case 'l':
                cLimit = ValueUnion.u32;
                break;

            case 'g':
                fStopAtGlobalFlush = true;
                break;

            case 'r':
                fResolveRip = true;
                break;

            case 'h':
                pHlp->pfnPrintf(pHlp,
                                "Usage: info tlbtrace [options] [n]\n"
                                "\n"
                                "Options:\n"
                                "  -c<n>, --cpu=<n>, --vcpu=<n>\n"
                                "    Selects the CPU which TLB trace we're looking at. Default: Caller / 0\n"
                                "  [n], -l<n>, --last=<n>\n"
                                "    Limit display to the last N entries. Default: all\n"
                                "  -g, --stop-at-global-flush\n"
                                "    Stop after the first global flush entry.\n"
                                "  -r, --resolve-rip\n"
                                "    Resolve symbols for the flattened RIP addresses.\n"
                                );
                return;

            case VINF_GETOPT_NOT_OPTION:
                rc = RTStrToUInt32Full(ValueUnion.psz, 0, &cLimit);
                if (RT_SUCCESS(rc))
                    break;
                pHlp->pfnPrintf(pHlp, "error: failed to convert '%s' to a number: %Rrc\n", ValueUnion.psz, rc);
                return;

            default:
                pHlp->pfnGetOptError(pHlp, rc, &ValueUnion, &State);
                return;
        }
    }

    /*
     * Get the details.
     */
    AssertReturnVoid(pVCpu);
    Assert(pVCpu->iem.s.cTlbTraceEntriesShift <= 28);
    uint32_t            idx         = pVCpu->iem.s.idxTlbTraceEntry;
    uint32_t const      cShift      = RT_MIN(pVCpu->iem.s.cTlbTraceEntriesShift, 28);
    uint32_t const      fMask       = RT_BIT_32(cShift) - 1;
    uint32_t            cLeft       = RT_MIN(RT_MIN(idx, RT_BIT_32(cShift)), cLimit);
    PCIEMTLBTRACEENTRY  paEntries   = pVCpu->iem.s.paTlbTraceEntries;
    if (cLeft && paEntries)
    {
        /*
         * Display the entries.
         */
        pHlp->pfnPrintf(pHlp, "TLB Trace for CPU %u:\n", pVCpu->idCpu);
        while (cLeft-- > 0)
        {
            PCIEMTLBTRACEENTRY const pCur      = &paEntries[--idx & fMask];
            const char              *pszSymbol = "";
            union
            {
                RTDBGSYMBOL Symbol;
                char        ach[sizeof(RTDBGSYMBOL) + 32];
            } uBuf;
            if (fResolveRip)
            {
                RTGCINTPTR  offDisp = 0;
                DBGFADDRESS Addr;
                rc = DBGFR3AsSymbolByAddr(pVM->pUVM, DBGF_AS_GLOBAL, DBGFR3AddrFromFlat(pVM->pUVM, &Addr, pCur->rip),
                                            RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL
                                          | RTDBGSYMADDR_FLAGS_SKIP_ABS
                                          | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                          &offDisp, &uBuf.Symbol, NULL);
                if (RT_SUCCESS(rc))
                {
                    /* Add displacement. */
                    if (offDisp)
                    {
                        size_t const cchName    = strlen(uBuf.Symbol.szName);
                        char * const pszEndName = &uBuf.Symbol.szName[cchName];
                        size_t const cbLeft     = sizeof(uBuf) - sizeof(uBuf.Symbol) + sizeof(uBuf.Symbol.szName) - cchName;
                        if (offDisp > 0)
                            RTStrPrintf(pszEndName, cbLeft, "+%#1RGv", offDisp);
                        else
                            RTStrPrintf(pszEndName, cbLeft, "-%#1RGv", -offDisp);
                    }

                    /* Put a space before it. */
                    AssertCompile(RTASSERT_OFFSET_OF(RTDBGSYMBOL, szName) > 0);
                    char *pszName = uBuf.Symbol.szName;
                    *--pszName = ' ';
                    pszSymbol = pszName;
                }
            }
            static const char *s_apszTlbType[2]  = { "code", "data" };
            static const char *s_apszScanType[4] = { "skipped", "global", "non-global", "both" };
            switch (pCur->enmType)
            {
                case kIemTlbTraceType_InvlPg:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 invlpg %RGv slot=" IEMTLB_SLOT_FMT "%s\n", idx, pCur->rip,
                                    pCur->u64Param, (uint32_t)IEMTLB_ADDR_TO_INDEX(pVCpu, pCur->u64Param), pszSymbol);
                    break;
                case kIemTlbTraceType_EvictSlot:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 evict %s slot=" IEMTLB_SLOT_FMT " %RGv (%#RX64) gcphys=%RGp%s\n",
                                    idx, pCur->rip, s_apszTlbType[pCur->bParam & 1], pCur->u32Param,
                                       (RTGCINTPTR)((pCur->u64Param & ~IEMTLB_REVISION_MASK) << (64 - IEMTLB_TAG_ADDR_WIDTH))
                                    >> (64 - IEMTLB_TAG_ADDR_WIDTH - GUEST_PAGE_SHIFT), pCur->u64Param,
                                    pCur->u64Param2, pszSymbol);
                    break;
                case kIemTlbTraceType_LargeEvictSlot:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 large evict %s slot=" IEMTLB_SLOT_FMT " %RGv (%#RX64) gcphys=%RGp%s\n",
                                    idx, pCur->rip, s_apszTlbType[pCur->bParam & 1], pCur->u32Param,
                                       (RTGCINTPTR)((pCur->u64Param & ~IEMTLB_REVISION_MASK) << (64 - IEMTLB_TAG_ADDR_WIDTH))
                                    >> (64 - IEMTLB_TAG_ADDR_WIDTH - GUEST_PAGE_SHIFT), pCur->u64Param,
                                    pCur->u64Param2, pszSymbol);
                    break;
                case kIemTlbTraceType_LargeScan:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 large scan %s %s%s\n", idx, pCur->rip, s_apszTlbType[pCur->bParam & 1],
                                    s_apszScanType[pCur->u32Param & 3], pszSymbol);
                    break;

                case kIemTlbTraceType_Flush:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 flush %s rev=%#RX64%s\n", idx, pCur->rip,
                                    s_apszTlbType[pCur->bParam & 1], pCur->u64Param, pszSymbol);
                    break;
                case kIemTlbTraceType_FlushGlobal:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 flush %s rev=%#RX64 grev=%#RX64%s\n", idx, pCur->rip,
                                    s_apszTlbType[pCur->bParam & 1], pCur->u64Param, pCur->u64Param2, pszSymbol);
                    if (fStopAtGlobalFlush)
                        return;
                    break;
                case kIemTlbTraceType_Load:
# if IEMTLB_ENTRY_COUNT_FACTOR > 1
                case kIemTlbTraceType_LoadGlobal:
# endif
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 %cload %s %RGv slot=" IEMTLB_SLOT_FMT " gcphys=%RGp fTlb=%#RX32%s\n",
                                    idx, pCur->rip,
                                    pCur->enmType == kIemTlbTraceType_LoadGlobal ? 'g' : 'l', s_apszTlbType[pCur->bParam & 1],
                                    pCur->u64Param,
                                      (uint32_t)IEMTLB_ADDR_TO_INDEX(pVCpu, pCur->u64Param)
                                    | (pCur->enmType == kIemTlbTraceType_LoadGlobal),
                                    (RTGCPTR)pCur->u64Param2, pCur->u32Param, pszSymbol);
                    break;

                case kIemTlbTraceType_Load_Cr0:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 load cr0 %08RX64 (was %08RX64)%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pszSymbol);
                    break;
                case kIemTlbTraceType_Load_Cr3:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 load cr3 %016RX64 (was %016RX64)%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pszSymbol);
                    break;
                case kIemTlbTraceType_Load_Cr4:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 load cr4 %08RX64 (was %08RX64)%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pszSymbol);
                    break;
                case kIemTlbTraceType_Load_Efer:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 load efer %016RX64 (was %016RX64)%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pszSymbol);
                    break;

                case kIemTlbTraceType_Irq:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 irq %#04x flags=%#x eflboth=%#RX64%s\n",
                                    idx, pCur->rip, pCur->bParam, pCur->u32Param,
                                    pCur->u64Param & ((RT_BIT_64(CPUMX86EFLAGS_HW_BITS) - 1) | CPUMX86EFLAGS_INT_MASK_64),
                                    pszSymbol);
                    break;
                case kIemTlbTraceType_Xcpt:
                    if (pCur->u32Param & IEM_XCPT_FLAGS_CR2)
                        pHlp->pfnPrintf(pHlp, "%u: %016RX64 xcpt %#04x flags=%#x errcd=%#x cr2=%RX64%s\n",
                                        idx, pCur->rip, pCur->bParam, pCur->u32Param, pCur->u64Param, pCur->u64Param2, pszSymbol);
                    else if (pCur->u32Param & IEM_XCPT_FLAGS_ERR)
                        pHlp->pfnPrintf(pHlp, "%u: %016RX64 xcpt %#04x flags=%#x errcd=%#x%s\n",
                                        idx, pCur->rip, pCur->bParam, pCur->u32Param, pCur->u64Param, pszSymbol);
                    else
                        pHlp->pfnPrintf(pHlp, "%u: %016RX64 xcpt %#04x flags=%#x%s\n",
                                        idx, pCur->rip, pCur->bParam, pCur->u32Param, pszSymbol);
                    break;
                case kIemTlbTraceType_IRet:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 iret cs:rip=%04x:%016RX64 efl=%08RX32%s\n",
                                    idx, pCur->rip, pCur->u32Param, pCur->u64Param, (uint32_t)pCur->u64Param2, pszSymbol);
                    break;

                case kIemTlbTraceType_Tb_Compile:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 tb comp GCPhysPc=%012RX64%s\n",
                                    idx, pCur->rip, pCur->u64Param, pszSymbol);
                    break;
                case kIemTlbTraceType_Tb_Exec_Threaded:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 tb thrd GCPhysPc=%012RX64 tb=%p used=%u%s\n",
                                    idx, pCur->rip, pCur->u64Param, (uintptr_t)pCur->u64Param2, pCur->u32Param, pszSymbol);
                    break;
                case kIemTlbTraceType_Tb_Exec_Native:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 tb n8ve GCPhysPc=%012RX64 tb=%p used=%u%s\n",
                                    idx, pCur->rip, pCur->u64Param, (uintptr_t)pCur->u64Param2, pCur->u32Param, pszSymbol);
                    break;

                case kIemTlbTraceType_User0:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 user0 %016RX64 %016RX64 %08RX32 %02RX8%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pCur->u32Param, pCur->bParam, pszSymbol);
                    break;
                case kIemTlbTraceType_User1:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 user1 %016RX64 %016RX64 %08RX32 %02RX8%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pCur->u32Param, pCur->bParam, pszSymbol);
                    break;
                case kIemTlbTraceType_User2:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 user2 %016RX64 %016RX64 %08RX32 %02RX8%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pCur->u32Param, pCur->bParam, pszSymbol);
                    break;
                case kIemTlbTraceType_User3:
                    pHlp->pfnPrintf(pHlp, "%u: %016RX64 user3 %016RX64 %016RX64 %08RX32 %02RX8%s\n",
                                    idx, pCur->rip, pCur->u64Param, pCur->u64Param2, pCur->u32Param, pCur->bParam, pszSymbol);
                    break;

                case kIemTlbTraceType_Invalid:
                    pHlp->pfnPrintf(pHlp, "%u: Invalid!\n");
                    break;
            }
        }
    }
    else
        pHlp->pfnPrintf(pHlp, "No trace entries to display\n");
}
#endif /* IEM_WITH_TLB_TRACE */

#if defined(VBOX_WITH_IEM_RECOMPILER) && !defined(VBOX_VMM_TARGET_ARMV8)

/**
 * Get get compile time flat PC for the TB.
 */
DECL_FORCE_INLINE(RTGCPTR) iemR3GetTbFlatPc(PCIEMTB pTb)
{
#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    if (pTb->fFlags & IEMTB_F_TYPE_NATIVE)
    {
        PCIEMTBDBG const pDbgInfo = pTb->pDbgInfo;
        return pDbgInfo ? pDbgInfo->FlatPc : RTGCPTR_MAX;
    }
#endif
    return pTb->FlatPc;
}


/**
 * @callback_method_impl{FNDBGFINFOARGVINT, tb}
 */
static DECLCALLBACK(void) iemR3InfoTb(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--cpu",              'c', RTGETOPT_REQ_UINT32                          },
        { "--vcpu",             'c', RTGETOPT_REQ_UINT32                          },
        { "--addr",             'a', RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX },
        { "--address",          'a', RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX },
        { "--phys",             'p', RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX },
        { "--physical",         'p', RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX },
        { "--phys-addr",        'p', RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX },
        { "--phys-address",     'p', RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX },
        { "--physical-address", 'p', RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX },
        { "--flags",            'f', RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_HEX },
        { "--tb",               't', RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_HEX },
        { "--tb-id",            't', RTGETOPT_REQ_UINT32 },
    };

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 0 /*iFirst*/, 0 /*fFlags*/);
    AssertRCReturnVoid(rc);

    PVMCPU const    pVCpuThis   = VMMGetCpu(pVM);
    PVMCPU          pVCpu       = pVCpuThis ? pVCpuThis : VMMGetCpuById(pVM, 0);
    RTGCPHYS        GCPhysPc    = NIL_RTGCPHYS;
    RTGCPHYS        GCVirt      = NIL_RTGCPTR;
    uint32_t        fFlags      = UINT32_MAX;
    uint32_t        idTb        = UINT32_MAX;

    RTGETOPTUNION ValueUnion;
    while ((rc = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'c':
                if (ValueUnion.u32 >= pVM->cCpus)
                    pHlp->pfnPrintf(pHlp, "error: Invalid CPU ID: %u\n", ValueUnion.u32);
                else if (!pVCpu || pVCpu->idCpu != ValueUnion.u32)
                    pVCpu = VMMGetCpuById(pVM, ValueUnion.u32);
                break;

            case 'a':
                GCVirt   = ValueUnion.u64;
                GCPhysPc = NIL_RTGCPHYS;
                idTb     = UINT32_MAX;
                break;

            case 'p':
                GCVirt   = NIL_RTGCPHYS;
                GCPhysPc = ValueUnion.u64;
                idTb     = UINT32_MAX;
                break;

            case 'f':
                fFlags   = ValueUnion.u32;
                break;

            case 't':
                GCVirt   = NIL_RTGCPHYS;
                GCPhysPc = NIL_RTGCPHYS;
                idTb     = ValueUnion.u32;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                if (   (ValueUnion.psz[0] == 'T' || ValueUnion.psz[0] == 't')
                    && (ValueUnion.psz[1] == 'B' || ValueUnion.psz[1] == 'b')
                    &&  ValueUnion.psz[2] == '#')
                {
                    rc = RTStrToUInt32Full(&ValueUnion.psz[3], 0, &idTb);
                    if (RT_SUCCESS(rc))
                    {
                        GCVirt   = NIL_RTGCPHYS;
                        GCPhysPc = NIL_RTGCPHYS;
                        break;
                    }
                    pHlp->pfnPrintf(pHlp, "error: failed to convert '%s' to TD ID: %Rrc\n", ValueUnion.psz, rc);
                }
                else
                    pHlp->pfnGetOptError(pHlp, rc, &ValueUnion, &State);
                return;
            }

            case 'h':
                pHlp->pfnPrintf(pHlp,
                                "Usage: info tb [options]\n"
                                "\n"
                                "Options:\n"
                                "  -c<n>, --cpu=<n>, --vcpu=<n>\n"
                                "    Selects the CPU which TBs we're looking at. Default: Caller / 0\n"
                                "  -a<virt>, --address=<virt>\n"
                                "    Shows the TB for the specified guest virtual address.\n"
                                "  -p<phys>, --phys=<phys>, --phys-addr=<phys>\n"
                                "    Shows the TB for the specified guest physical address.\n"
                                "  -t<id>, --tb=<id>, --tb-id=<id>, TD#<id>\n"
                                "    Show the TB specified by the identifier/number (from tbtop).\n"
                                "  -f<flags>,--flags=<flags>\n"
                                "    The TB flags value (hex) to use when looking up the TB.\n"
                                "\n"
                                "The default is to use CS:RIP and derive flags from the CPU mode.\n");
                return;

            default:
                pHlp->pfnGetOptError(pHlp, rc, &ValueUnion, &State);
                return;
        }
    }

    /* Currently, only do work on the same EMT. */
    if (pVCpu != pVCpuThis)
    {
        pHlp->pfnPrintf(pHlp, "TODO: Cross EMT calling not supported yet: targeting %u, caller on %d\n",
                        pVCpu->idCpu, pVCpuThis ? (int)pVCpuThis->idCpu : -1);
        return;
    }

    /*
     * Defaults.
     */
    if (GCPhysPc == NIL_RTGCPHYS && idTb == UINT32_MAX)
    {
        if (GCVirt == NIL_RTGCPTR)
            GCVirt = CPUMGetGuestFlatPC(pVCpu);
        rc = PGMPhysGCPtr2GCPhys(pVCpu, GCVirt, &GCPhysPc);
        if (RT_FAILURE(rc))
        {
            pHlp->pfnPrintf(pHlp, "Failed to convert %%%RGv to an guest physical address: %Rrc\n", GCVirt, rc);
            return;
        }
    }
    if (fFlags == UINT32_MAX && idTb == UINT32_MAX)
    {
        /* Note! This is duplicating code in IEMAllThrdRecompiler. */
        fFlags = iemCalcExecFlags(pVCpu);
        if (pVM->cCpus == 1)
            fFlags |= IEM_F_X86_DISREGARD_LOCK;
        if (CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx))
            fFlags |= IEMTB_F_X86_INHIBIT_SHADOW;
        if (CPUMAreInterruptsInhibitedByNmiEx(&pVCpu->cpum.GstCtx))
            fFlags |= IEMTB_F_X86_INHIBIT_NMI;
        if ((IEM_F_MODE_X86_CPUMODE_MASK & fFlags) != IEMMODE_64BIT)
        {
            int64_t const offFromLim = (int64_t)pVCpu->cpum.GstCtx.cs.u32Limit - (int64_t)pVCpu->cpum.GstCtx.eip;
            if (offFromLim < X86_PAGE_SIZE + 16 - (int32_t)(pVCpu->cpum.GstCtx.cs.u64Base & GUEST_PAGE_OFFSET_MASK))
                fFlags |= IEMTB_F_X86_CS_LIM_CHECKS;
        }
    }

    PCIEMTB pTb;
    if (idTb == UINT32_MAX)
    {
        /*
         * Do the lookup...
         *
         * Note! This is also duplicating code in IEMAllThrdRecompiler.  We don't
         *       have much choice since we don't want to increase use counters and
         *       trigger native recompilation.
         */
        fFlags &= IEMTB_F_KEY_MASK;
        IEMTBCACHE const * const pTbCache = pVCpu->iem.s.pTbCacheR3;
        uint32_t const           idxHash  = IEMTBCACHE_HASH(pTbCache, fFlags, GCPhysPc);
        pTb = IEMTBCACHE_PTR_GET_TB(pTbCache->apHash[idxHash]);
        while (pTb)
        {
            if (pTb->GCPhysPc == GCPhysPc)
            {
                if ((pTb->fFlags & IEMTB_F_KEY_MASK) == fFlags)
                {
                    /// @todo if (pTb->x86.fAttr == (uint16_t)pVCpu->cpum.GstCtx.cs.Attr.u)
                    break;
                }
            }
            pTb = pTb->pNext;
        }
        if (!pTb)
            pHlp->pfnPrintf(pHlp, "PC=%RGp fFlags=%#x - no TB found on #%u\n", GCPhysPc, fFlags, pVCpu->idCpu);
    }
    else
    {
        /*
         * Use the TB ID for indexing.
         */
        pTb = NULL;
        PIEMTBALLOCATOR const pTbAllocator = pVCpu->iem.s.pTbAllocatorR3;
        if (pTbAllocator)
        {
            size_t const idxTbChunk   = idTb / pTbAllocator->cTbsPerChunk;
            size_t const idxTbInChunk = idTb % pTbAllocator->cTbsPerChunk;
            if (idxTbChunk < pTbAllocator->cAllocatedChunks)
                pTb = &pTbAllocator->aChunks[idxTbChunk].paTbs[idxTbInChunk];
            else
                pHlp->pfnPrintf(pHlp, "Invalid TB ID: %u (%#x)\n", idTb, idTb);
        }
    }

    if (pTb)
    {
        /*
         * Disassemble according to type.
         */
        size_t const  idxTbChunk = pTb->idxAllocChunk;
        size_t const  idxTbNo    = (pTb - &pVCpu->iem.s.pTbAllocatorR3->aChunks[idxTbChunk].paTbs[0])
                                 + idxTbChunk * pVCpu->iem.s.pTbAllocatorR3->cTbsPerChunk;
        switch (pTb->fFlags & IEMTB_F_TYPE_MASK)
        {
# ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER
            case IEMTB_F_TYPE_NATIVE:
                pHlp->pfnPrintf(pHlp, "PC=%RGp (%%%RGv) fFlags=%#x on #%u: TB#%#zx/%p - native\n",
                                GCPhysPc, iemR3GetTbFlatPc(pTb),  fFlags, pVCpu->idCpu, idxTbNo, pTb);
                iemNativeDisassembleTb(pVCpu, pTb, pHlp);
                break;
# endif

            case IEMTB_F_TYPE_THREADED:
                pHlp->pfnPrintf(pHlp, "PC=%RGp (%%%RGv) fFlags=%#x on #%u: TB#%#zx/%p - threaded\n",
                                GCPhysPc, pTb->FlatPc, fFlags, pVCpu->idCpu, idxTbNo, pTb);
                iemThreadedDisassembleTb(pTb, pHlp);
                break;

            default:
                pHlp->pfnPrintf(pHlp, "PC=%RGp (%%%RGv) fFlags=%#x on #%u: TB#%#zx/%p - ??? %#x\n",
                                GCPhysPc, pTb->FlatPc, fFlags, pVCpu->idCpu, idxTbNo, pTb, pTb->fFlags);
                break;
        }
    }
}


/**
 * @callback_method_impl{FNDBGFINFOARGVINT, tbtop}
 */
static DECLCALLBACK(void) iemR3InfoTbTop(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--cpu",                  'c', RTGETOPT_REQ_UINT32   },
        { "--vcpu",                 'c', RTGETOPT_REQ_UINT32   },
        { "--dis",                  'd', RTGETOPT_REQ_NOTHING  },
        { "--disas",                'd', RTGETOPT_REQ_NOTHING  },
        { "--disasm",               'd', RTGETOPT_REQ_NOTHING  },
        { "--disassemble",          'd', RTGETOPT_REQ_NOTHING  },
        { "--no-dis",               'D', RTGETOPT_REQ_NOTHING  },
        { "--no-disas",             'D', RTGETOPT_REQ_NOTHING  },
        { "--no-disasm",            'D', RTGETOPT_REQ_NOTHING  },
        { "--no-disassemble",       'D', RTGETOPT_REQ_NOTHING  },
        { "--most-freq",            'f', RTGETOPT_REQ_NOTHING  },
        { "--most-frequent",        'f', RTGETOPT_REQ_NOTHING  },
        { "--most-frequently",      'f', RTGETOPT_REQ_NOTHING  },
        { "--most-frequently-used", 'f', RTGETOPT_REQ_NOTHING  },
        { "--most-recent",          'r', RTGETOPT_REQ_NOTHING  },
        { "--most-recently",        'r', RTGETOPT_REQ_NOTHING  },
        { "--most-recently-used",   'r', RTGETOPT_REQ_NOTHING  },
        { "--count",                'n', RTGETOPT_REQ_UINT32   },
    };

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 0 /*iFirst*/, 0 /*fFlags*/);
    AssertRCReturnVoid(rc);

    PVMCPU const    pVCpuThis    = VMMGetCpu(pVM);
    PVMCPU          pVCpu        = pVCpuThis ? pVCpuThis : VMMGetCpuById(pVM, 0);
    enum { kTbTop_MostFrequentlyUsed, kTbTop_MostRececentlyUsed }
                    enmTop       = kTbTop_MostFrequentlyUsed;
    bool            fDisassemble = false;
    uint32_t const  cTopDefault  = 64;
    uint32_t const  cTopMin      = 1;
    uint32_t const  cTopMax      = 1024;
    uint32_t        cTop         = cTopDefault;

    RTGETOPTUNION ValueUnion;
    while ((rc = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'c':
                if (ValueUnion.u32 >= pVM->cCpus)
                    pHlp->pfnPrintf(pHlp, "error: Invalid CPU ID: %u\n", ValueUnion.u32);
                else if (!pVCpu || pVCpu->idCpu != ValueUnion.u32)
                    pVCpu = VMMGetCpuById(pVM, ValueUnion.u32);
                break;

            case 'd':
                fDisassemble = true;
                break;

            case 'D':
                fDisassemble = true;
                break;

            case 'f':
                enmTop = kTbTop_MostFrequentlyUsed;
                break;

            case 'r':
                enmTop = kTbTop_MostRececentlyUsed;
                break;

            case VINF_GETOPT_NOT_OPTION:
                rc = RTStrToUInt32Full(ValueUnion.psz, 0, &cTop);
                if (RT_FAILURE(rc))
                {
                    pHlp->pfnPrintf(pHlp, "error: failed to convert '%s' to a number: %Rrc\n", ValueUnion.psz, rc);
                    return;
                }
                ValueUnion.u32 = cTop;
                RT_FALL_THROUGH();
            case 'n':
                if (!ValueUnion.u32)
                    cTop = cTopDefault;
                else
                {
                    cTop = RT_MAX(RT_MIN(ValueUnion.u32, cTopMax), cTopMin);
                    if (cTop != ValueUnion.u32)
                        pHlp->pfnPrintf(pHlp, "warning: adjusted %u to %u (valid range: [%u..%u], 0 for default (%d))",
                                        ValueUnion.u32, cTop, cTopMin, cTopMax, cTopDefault);
                }
                break;

            case 'h':
                pHlp->pfnPrintf(pHlp,
                                "Usage: info tbtop [options]\n"
                                "\n"
                                "Options:\n"
                                "  -c<n>, --cpu=<n>, --vcpu=<n>\n"
                                "    Selects the CPU which TBs we're looking at. Default: Caller / 0\n"
                                "  -d, --dis[as[m]], --disassemble\n"
                                "    Show full TB disassembly.\n"
                                "  -D, --no-dis[as[m]], --no-disassemble\n"
                                "    Do not show TB diassembly. The default.\n"
                                "  -f, --most-freq[ent[ly[-used]]]\n"
                                "    Shows the most frequently used TBs (IEMTB::cUsed). The default.\n"
                                "  -r, --most-recent[ly[-used]]\n"
                                "    Shows the most recently used TBs (IEMTB::msLastUsed).\n"
                                "  -n<num>, --count=<num>\n"
                                "    The number of TBs to display. Default: %u\n"
                                "    This is also what non-option arguments will be taken as.\n"
                                , cTopDefault);
                return;

            default:
                pHlp->pfnGetOptError(pHlp, rc, &ValueUnion, &State);
                return;
        }
    }

    /* Currently, only do work on the same EMT. */
    if (pVCpu != pVCpuThis)
    {
        pHlp->pfnPrintf(pHlp, "TODO: Cross EMT calling not supported yet: targeting %u, caller on %d\n",
                        pVCpu->idCpu, pVCpuThis ? (int)pVCpuThis->idCpu : -1);
        return;
    }

    /*
     * Collect the data by scanning the TB allocation map.
     */
    struct IEMTBTOPENTRY
    {
        /** Pointer to the translation block. */
        PCIEMTB     pTb;
        /** The sorting key. */
        uint64_t    uSortKey;
    }               aTop[cTopMax] = { { NULL, 0 }, };
    uint32_t        cValid        = 0;
    PIEMTBALLOCATOR pTbAllocator  = pVCpu->iem.s.pTbAllocatorR3;
    if (pTbAllocator)
    {
        uint32_t const cTbsPerChunk = pTbAllocator->cTbsPerChunk;
        for (uint32_t iChunk = 0; iChunk < pTbAllocator->cAllocatedChunks; iChunk++)
        {
            for (uint32_t iTb = 0; iTb < cTbsPerChunk; iTb++)
            {
                PCIEMTB const pTb = &pTbAllocator->aChunks[iChunk].paTbs[iTb];
                AssertContinue(pTb);
                if (pTb->fFlags & IEMTB_F_TYPE_MASK)
                {
                    /* Extract and compose the sort key. */
                    uint64_t const uSortKey = enmTop == kTbTop_MostFrequentlyUsed
                                            ? RT_MAKE_U64(pTb->msLastUsed, pTb->cUsed)
                                            : RT_MAKE_U64(pTb->cUsed, pTb->msLastUsed);

                    /*
                     * Discard the key if it's smaller than the smallest in the table when it is full.
                     */
                    if (   cValid   >= cTop
                        && uSortKey <= aTop[cTop - 1].uSortKey)
                    { /* discard it */ }
                    else
                    {
                        /*
                         * Do binary search to find the insert location
                         */
                        uint32_t idx;
                        if (cValid > 0)
                        {
                            uint32_t idxEnd   = cValid;
                            uint32_t idxStart = 0;
                            idx = cValid / 2;
                            for (;;)
                            {
                                if (uSortKey > aTop[idx].uSortKey)
                                {
                                    if (idx > idxStart)
                                        idxEnd = idx;
                                    else
                                        break;
                                }
                                else if (uSortKey < aTop[idx].uSortKey)
                                {
                                    idx += 1;
                                    if (idx < idxEnd)
                                        idxStart = idx;
                                    else
                                        break;
                                }
                                else
                                {
                                    do
                                        idx++;
                                    while (idx < cValid && uSortKey == aTop[idx].uSortKey);
                                    break;
                                }
                                idx = idxStart + (idxEnd - idxStart) / 2;
                            }
                            AssertContinue(idx < RT_ELEMENTS(aTop));

                            /*
                             * Shift entries as needed.
                             */
                            if (cValid >= cTop)
                            {
                                if (idx != cTop - 1U)
                                    memmove(&aTop[idx + 1], &aTop[idx], (cTop - idx - 1) * sizeof(aTop[0]));
                            }
                            else
                            {
                                if (idx != cValid)
                                    memmove(&aTop[idx + 1], &aTop[idx], (cValid - idx) * sizeof(aTop[0]));
                                cValid++;
                            }
                        }
                        else
                        {
                            /* Special case: The first insertion. */
                            cValid = 1;
                            idx    = 0;
                        }

                        /*
                         * Fill in the new entry.
                         */
                        aTop[idx].uSortKey = uSortKey;
                        aTop[idx].pTb      = pTb;
                    }
                }
            }
        }
    }

    /*
     * Display the result.
     */
    if (cTop > cValid)
        cTop = cValid;
    pHlp->pfnPrintf(pHlp, "Displaying the top %u TBs for CPU #%u ordered by %s:\n",
                    cTop, pVCpu->idCpu, enmTop == kTbTop_MostFrequentlyUsed ? "cUsed" : "msLastUsed");
    if (fDisassemble)
        pHlp->pfnPrintf(pHlp, "================================================================================\n");

    for (uint32_t idx = 0; idx < cTop; idx++)
    {
        if (fDisassemble && idx)
            pHlp->pfnPrintf(pHlp, "\n------------------------------- %u -------------------------------\n", idx);

        PCIEMTB const pTb        = aTop[idx].pTb;
        size_t const  idxTbChunk = pTb->idxAllocChunk;
        Assert(idxTbChunk < pTbAllocator->cAllocatedChunks);
        size_t const  idxTbNo    = (pTb - &pTbAllocator->aChunks[idxTbChunk].paTbs[0])
                                 + idxTbChunk * pTbAllocator->cTbsPerChunk;
        switch (pTb->fFlags & IEMTB_F_TYPE_MASK)
        {
# ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER
            case IEMTB_F_TYPE_NATIVE:
                pHlp->pfnPrintf(pHlp, "TB#%#zx: PC=%RGp (%%%RGv) cUsed=%u msLastUsed=%u fFlags=%#010x - native\n",
                                idxTbNo, pTb->GCPhysPc, iemR3GetTbFlatPc(pTb), pTb->cUsed, pTb->msLastUsed, pTb->fFlags);
                if (fDisassemble)
                    iemNativeDisassembleTb(pVCpu, pTb, pHlp);
                break;
# endif

            case IEMTB_F_TYPE_THREADED:
                pHlp->pfnPrintf(pHlp, "TB#%#zx: PC=%RGp (%%%RGv) cUsed=%u msLastUsed=%u fFlags=%#010x - threaded\n",
                                idxTbNo, pTb->GCPhysPc, pTb->FlatPc, pTb->cUsed, pTb->msLastUsed, pTb->fFlags);
                if (fDisassemble)
                    iemThreadedDisassembleTb(pTb, pHlp);
                break;

            default:
                pHlp->pfnPrintf(pHlp, "TB#%#zx: PC=%RGp (%%%RGv) cUsed=%u msLastUsed=%u fFlags=%#010x - ???\n",
                                idxTbNo, pTb->GCPhysPc, pTb->FlatPc, pTb->cUsed, pTb->msLastUsed, pTb->fFlags);
                break;
        }
    }
}

#endif /* VBOX_WITH_IEM_RECOMPILER && !VBOX_VMM_TARGET_ARMV8 */


#ifdef VBOX_WITH_DEBUGGER

/** @callback_method_impl{FNDBGCCMD,
 * Implements the '.alliem' command. }
 */
static DECLCALLBACK(int) iemR3DbgFlushTlbs(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    VMCPUID idCpu = DBGCCmdHlpGetCurrentCpu(pCmdHlp);
    PVMCPU pVCpu = VMMR3GetCpuByIdU(pUVM, idCpu);
    if (pVCpu)
    {
        VMR3ReqPriorityCallVoidWaitU(pUVM, idCpu, (PFNRT)IEMTlbInvalidateAllGlobal, 1, pVCpu);
        return VINF_SUCCESS;
    }
    RT_NOREF(paArgs, cArgs);
    return DBGCCmdHlpFail(pCmdHlp, pCmd, "failed to get the PVMCPU for the current CPU");
}


/**
 * Called by IEMR3Init to register debugger commands.
 */
static void iemR3RegisterDebuggerCommands(void)
{
    /*
     * Register debugger commands.
     */
    static DBGCCMD const s_aCmds[] =
    {
        {
            /* .pszCmd = */         "iemflushtlb",
            /* .cArgsMin = */       0,
            /* .cArgsMax = */       0,
            /* .paArgDescs = */     NULL,
            /* .cArgDescs = */      0,
            /* .fFlags = */         0,
            /* .pfnHandler = */     iemR3DbgFlushTlbs,
            /* .pszSyntax = */      "",
            /* .pszDescription = */ "Flushed the code and data TLBs"
        },
    };

    int rc = DBGCRegisterCommands(&s_aCmds[0], RT_ELEMENTS(s_aCmds));
    AssertLogRelRC(rc);
}

#endif /* VBOX_WITH_DEBUGGER */

