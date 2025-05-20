/* $Id: CPUM.cpp 168941 2025-05-20 21:23:52Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU Monitor / Manager.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/** @page pg_cpum CPUM - CPU Monitor / Manager
 *
 * The CPU Monitor / Manager keeps track of all the CPU registers. It is
 * also responsible for lazy FPU handling and some of the context loading
 * in raw mode.
 *
 * There are three CPU contexts, the most important one is the guest one (GC).
 * When running in raw-mode (RC) there is a special hyper context for the VMM
 * part that floats around inside the guest address space. When running in
 * raw-mode, CPUM also maintains a host context for saving and restoring
 * registers across world switches. This latter is done in cooperation with the
 * world switcher (@see pg_vmm).
 *
 * @see grp_cpum
 *
 * @section sec_cpum_fpu        FPU / SSE / AVX / ++ state.
 *
 * TODO: proper write up, currently just some notes.
 *
 * The ring-0 FPU handling per OS:
 *
 *      - 64-bit Windows uses XMM registers in the kernel as part of the calling
 *        convention (Visual C++ doesn't seem to have a way to disable
 *        generating such code either), so CR0.TS/EM are always zero from what I
 *        can tell.  We are also forced to always load/save the guest XMM0-XMM15
 *        registers when entering/leaving guest context.  Interrupt handlers
 *        using FPU/SSE will offically have call save and restore functions
 *        exported by the kernel, if the really really have to use the state.
 *
 *      - 32-bit windows does lazy FPU handling, I think, probably including
 *        lazying saving.  The Windows Internals book states that it's a bad
 *        idea to use the FPU in kernel space. However, it looks like it will
 *        restore the FPU state of the current thread in case of a kernel \#NM.
 *        Interrupt handlers should be same as for 64-bit.
 *
 *      - Darwin allows taking \#NM in kernel space, restoring current thread's
 *        state if I read the code correctly.  It saves the FPU state of the
 *        outgoing thread, and uses CR0.TS to lazily load the state of the
 *        incoming one.  No idea yet how the FPU is treated by interrupt
 *        handlers, i.e. whether they are allowed to disable the state or
 *        something.
 *
 *      - Linux also allows \#NM in kernel space (don't know since when), and
 *        uses CR0.TS for lazy loading.  Saves outgoing thread's state, lazy
 *        loads the incoming unless configured to agressivly load it.  Interrupt
 *        handlers can ask whether they're allowed to use the FPU, and may
 *        freely trash the state if Linux thinks it has saved the thread's state
 *        already.  This is a problem.
 *
 *      - Solaris will, from what I can tell, panic if it gets an \#NM in kernel
 *        context.  When switching threads, the kernel will save the state of
 *        the outgoing thread and lazy load the incoming one using CR0.TS.
 *        There are a few routines in seeblk.s which uses the SSE unit in ring-0
 *        to do stuff, HAT are among the users.  The routines there will
 *        manually clear CR0.TS and save the XMM registers they use only if
 *        CR0.TS was zero upon entry.  They will skip it when not, because as
 *        mentioned above, the FPU state is saved when switching away from a
 *        thread and CR0.TS set to 1, so when CR0.TS is 1 there is nothing to
 *        preserve.  This is a problem if we restore CR0.TS to 1 after loading
 *        the guest state.
 *
 *      - FreeBSD - no idea yet.
 *
 *      - OS/2 does not allow \#NMs in kernel space IIRC.  Does lazy loading,
 *        possibly also lazy saving.  Interrupts must preserve the CR0.TS+EM &
 *        FPU states.
 *
 * Up to r107425 (2016-05-24) we would only temporarily modify CR0.TS/EM while
 * saving and restoring the host and guest states.  The motivation for this
 * change is that we want to be able to emulate SSE instruction in ring-0 (IEM).
 *
 * Starting with that change, we will leave CR0.TS=EM=0 after saving the host
 * state and only restore it once we've restore the host FPU state. This has the
 * accidental side effect of triggering Solaris to preserve XMM registers in
 * sseblk.s. When CR0 was changed by saving the FPU state, CPUM must now inform
 * the VT-x (HMVMX) code about it as it caches the CR0 value in the VMCS.
 *
 *
 * @section sec_cpum_logging        Logging Level Assignments.
 *
 * Following log level assignments:
 *      - Log6 is used for FPU state management.
 *      - Log7 is used for FPU state actualization.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_CPUM
#define CPUM_WITH_NONCONST_HOST_FEATURES
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/cpumctx-v1_6.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/pdmapic.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/hmvmxinline.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmcc.h>

#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/err.h>
#include <VBox/log.h>
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/rand.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  cpumR3LoadPrepCommon(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)  cpumR3LoadDoneCommon(PVM pVM, PSSMHANDLE pSSM);

static DECLCALLBACK(void) cpumR3InfoAll(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoGuestInstr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
#ifdef RT_ARCH_AMD64
static DECLCALLBACK(void) cpumR3InfoHost(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
#endif
#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
static DECLCALLBACK(void) cpumR3InfoCpuFeatHost(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Host CPU features. */
DECL_HIDDEN_DATA(CPUHOSTFEATURES) g_CpumHostFeatures;



#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)

/**
 * Checks for partial/leaky FXSAVE/FXRSTOR handling on AMD CPUs.
 *
 * AMD K7, K8 and newer AMD CPUs do not save/restore the x87 error pointers
 * (last instruction pointer, last data pointer, last opcode) except when the ES
 * bit (Exception Summary) in x87 FSW (FPU Status Word) is set. Thus if we don't
 * clear these registers there is potential, local FPU leakage from a process
 * using the FPU to another.
 *
 * See AMD Instruction Reference for FXSAVE, FXRSTOR.
 *
 * @param   pVM     The cross context VM structure.
 */
static void cpumR3CheckLeakyFpu(PVM pVM)
{
    uint32_t u32CpuVersion = ASMCpuId_EAX(1);
    uint32_t const u32Family = u32CpuVersion >> 8;
    if (   u32Family >= 6      /* K7 and higher */
        && (ASMIsAmdCpu() || ASMIsHygonCpu()) )
    {
        uint32_t cExt = ASMCpuId_EAX(0x80000000);
        if (RTX86IsValidExtRange(cExt))
        {
            uint32_t fExtFeaturesEDX = ASMCpuId_EDX(0x80000001);
            if (fExtFeaturesEDX & X86_CPUID_AMD_FEATURE_EDX_FFXSR)
            {
                for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                {
                    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
                    pVCpu->cpum.s.fUseFlags |= CPUM_USE_FFXSR_LEAKY;
                }
                Log(("CPUM: Host CPU has leaky fxsave/fxrstor behaviour\n"));
            }
        }
    }
}


/**
 * Gets the host hardware-virtualization MSRs.
 *
 * @returns VBox status code.
 * @param   pMsrs       Where to store the MSRs.
 */
static int cpumR3GetX86HostHwvirtMsrs(PSUPHWVIRTMSRS pMsrs)
{
    Assert(pMsrs);

    uint32_t fCaps = 0;
    int rc = SUPR3QueryVTCaps(&fCaps);
    if (RT_SUCCESS(rc))
    {
        if (fCaps & (SUPVTCAPS_VT_X | SUPVTCAPS_AMD_V))
        {
            rc = SUPR3GetHwvirtMsrs(pMsrs, false /* fForceRequery */);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            LogRel(("CPUM: Querying hardware-virtualization MSRs failed. rc=%Rrc\n", rc));
            return rc;
        }

        LogRel(("CPUM: Querying hardware-virtualization capability succeeded but did not find VT-x or AMD-V\n"));
        return VERR_INTERNAL_ERROR_5;
    }

    LogRel(("CPUM: No hardware-virtualization capability detected\n"));
    return VINF_SUCCESS;
}

#endif /* RT_ARCH_X86 || RT_ARCH_AMD64 */


/**
 * Initializes the CPUM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) CPUMR3Init(PVM pVM)
{
    LogFlow(("CPUMR3Init\n"));
    int rc;

    /*
     * Assert alignment, sizes and tables.
     */
    AssertCompileMemberAlignment(VM, cpum.s, 32);
    AssertCompile(sizeof(pVM->cpum.s) <= sizeof(pVM->cpum.padding));
    AssertCompileSizeAlignment(CPUMCTX, 64);
#ifdef VBOX_VMM_TARGET_X86
    AssertCompileSizeAlignment(CPUMCTXMSRS, 64);
#endif
#ifdef RT_ARCH_AMD64
    AssertCompileSizeAlignment(CPUMHOSTCTX, 64);
#endif
    AssertCompileMemberAlignment(VM, cpum, 64);
    AssertCompileMemberAlignment(VMCPU, cpum.s, 64);
#ifdef VBOX_STRICT
# ifdef VBOX_VMM_TARGET_X86
    rc = cpumR3MsrStrictInitChecks();
# elif defined(VBOX_VMM_TARGET_ARMV8)
    rc = cpumR3SysRegStrictInitChecks();
# endif
    AssertRCReturn(rc, rc);
#endif

    /*
     * Gather info about the host CPU.
     */
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    if (!ASMHasCpuId())
    {
        LogRel(("The CPU doesn't support CPUID!\n"));
        return VERR_UNSUPPORTED_CPU;
    }

    pVM->cpum.s.fHostMxCsrMask = CPUMR3DeterminHostMxCsrMask();

    SUPHWVIRTMSRS HostMsrs;
    RT_ZERO(HostMsrs);
    rc = cpumR3GetX86HostHwvirtMsrs(&HostMsrs);
    AssertLogRelRCReturn(rc, rc);
#endif

    /* Use the host features detected by CPUMR0ModuleInit if available. */
    if (pVM->cpum.s.HostFeatures.Common.enmCpuVendor != CPUMCPUVENDOR_INVALID)
        g_CpumHostFeatures.s = pVM->cpum.s.HostFeatures.s;
    else
    {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        rc = CPUMCpuIdCollectLeavesFromX86Host(&pVM->cpum.s.paHostLeavesR3, &pVM->cpum.s.cHostLeaves);
        AssertLogRelRCReturn(rc, rc);

        rc = CPUMCpuIdExplodeFeaturesX86(pVM->cpum.s.paHostLeavesR3, pVM->cpum.s.cHostLeaves, &g_CpumHostFeatures.s);
        AssertLogRelRCReturn(rc, rc);
        if (g_CpumHostFeatures.s.fVmx)
            cpumCpuIdExplodeFeaturesX86VmxFromSupMsrs(&HostMsrs, &g_CpumHostFeatures.s);

#elif defined(RT_ARCH_ARM64)
        rc = CPUMCpuIdCollectIdSysRegsFromArmV8Host(&pVM->cpum.s.paHostIdRegsR3, &pVM->cpum.s.cHostIdRegs);
        AssertLogRelRCReturn(rc, rc);

        rc = CPUMCpuIdExplodeFeaturesArmV8FromSysRegs(pVM->cpum.s.paHostIdRegsR3, pVM->cpum.s.cHostIdRegs, &g_CpumHostFeatures.s);
        AssertLogRelRCReturn(rc, rc);

#else
# error port me
#endif
        AssertLogRelRCReturn(rc, rc);
        pVM->cpum.s.HostFeatures.s = g_CpumHostFeatures.s;
    }
    pVM->cpum.s.GuestFeatures.enmCpuVendor = pVM->cpum.s.HostFeatures.Common.enmCpuVendor; /* a bit bogus for mismatching host/guest */

    /*
     * Check that the CPU supports the minimum features we require.
     */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    if (!pVM->cpum.s.HostFeatures.s.fFxSaveRstor)
        return VMSetError(pVM, VERR_UNSUPPORTED_CPU, RT_SRC_POS, "Host CPU does not support the FXSAVE/FXRSTOR instruction.");
    if (!pVM->cpum.s.HostFeatures.s.fMmx)
        return VMSetError(pVM, VERR_UNSUPPORTED_CPU, RT_SRC_POS, "Host CPU does not support MMX.");
    if (!pVM->cpum.s.HostFeatures.s.fTsc)
        return VMSetError(pVM, VERR_UNSUPPORTED_CPU, RT_SRC_POS, "Host CPU does not support RDTSC.");
#endif


#if defined(VBOX_VMM_TARGET_X86) || defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    /*
     * Figure out which XSAVE/XRSTOR features are available on the host.
     */
    uint64_t fXcr0Host = 0;
    uint64_t fXStateHostMask = 0;
# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    if (   pVM->cpum.s.HostFeatures.s.fXSaveRstor
        && pVM->cpum.s.HostFeatures.s.fOpSysXSaveRstor)
    {
        fXStateHostMask  = fXcr0Host = ASMGetXcr0();
        fXStateHostMask &= XSAVE_C_X87 | XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI;
        AssertLogRelMsgStmt((fXStateHostMask & (XSAVE_C_X87 | XSAVE_C_SSE)) == (XSAVE_C_X87 | XSAVE_C_SSE),
                            ("%#llx\n", fXStateHostMask), fXStateHostMask = 0);
    }
# elif defined(RT_ARCH_ARM64)
    /** @todo r=aeichner Keep AVX/AVX2 disabled for now, too many missing instruction emulations. */
    fXStateHostMask = XSAVE_C_X87 | XSAVE_C_SSE /*| XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI*/;
# endif
# ifdef VBOX_VMM_TARGET_X86
    pVM->cpum.s.fXStateHostMask = fXStateHostMask;
# endif
    LogRel(("CPUM: fXStateHostMask=%#llx; host XCR0=%#llx\n", fXStateHostMask, fXcr0Host));

# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    AssertLogRelReturn(   pVM->cpum.s.HostFeatures.s.cbMaxExtendedState >= sizeof(X86FXSTATE)
                       && pVM->cpum.s.HostFeatures.s.cbMaxExtendedState <= sizeof(pVM->apCpusR3[0]->cpum.s.Host.abXState)
                       , VERR_CPUM_IPE_2);
# endif

    /* Distribute the mask to each VCpu state.  Take this opportunity to init
       the preemption timer as well. */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        pVCpu->cpum.s.Host.fXStateMask       = fXStateHostMask;
# endif
# ifdef VBOX_VMM_TARGET_X86
        pVCpu->cpum.s.hNestedVmxPreemptTimer = NIL_TMTIMERHANDLE;
# endif
    }

# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    /*
     * Check if we need to workaround partial/leaky FPU handling.
     */
    cpumR3CheckLeakyFpu(pVM);
# endif
#endif /* VBOX_VMM_TARGET_X86 || RT_ARCH_X86 || RT_ARCH_AMD64 */


    /*
     * Do target specific initialization.
     */
#ifdef VBOX_VMM_TARGET_X86
# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    rc = cpumR3InitTargetX86(pVM, &HostMsrs);
# else
    rc = cpumR3InitTargetX86(pVM, NULL);
# endif
#else
    rc = cpumR3InitTarget(pVM);
#endif
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register saved state data item.
     */
    rc = SSMR3RegisterInternal(pVM, "cpum", 1, CPUM_SAVED_STATE_VERSION, sizeof(CPUM),
                               NULL, cpumR3LiveExecTarget, NULL,
                               NULL, cpumR3SaveExecTarget, NULL,
                               cpumR3LoadPrepCommon, cpumR3LoadExecTarget, cpumR3LoadDoneCommon);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register info handlers and registers with the debugger facility.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "cpum",             "Displays the all the cpu states.",
                                 &cpumR3InfoAll, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumguest",        "Displays the guest cpu state.",
                                 &cpumR3InfoGuest, DBGFINFO_FLAGS_ALL_EMTS);
#ifdef RT_ARCH_AMD64
    DBGFR3InfoRegisterInternalEx(pVM, "cpumhost",         "Displays the host cpu state.",
                                 &cpumR3InfoHost, DBGFINFO_FLAGS_ALL_EMTS);
#endif
    DBGFR3InfoRegisterInternalEx(pVM, "cpumguestinstr",   "Displays the current guest instruction.",
                                 &cpumR3InfoGuestInstr, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternal(  pVM, "cpuid",            "Displays the guest CPU ID registers.",
                                 &cpumR3CpuIdInfo);
    DBGFR3InfoRegisterInternal(  pVM, "cpuidhost",        "Displays the host CPU ID registers.",
                                 &cpumR3CpuIdInfoHost);
# if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
    DBGFR3InfoRegisterInternal(pVM, "cpufeathost", "Displays the host features.", &cpumR3InfoCpuFeatHost);
#endif

#if  (defined(VBOX_VMM_TARGET_X86)   && !defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86)) \
  || (defined(VBOX_VMM_TARGET_ARMV8) && !defined(RT_ARCH_ARM64) && !defined(RT_ARCH_ARM32))
    /** @todo cpuidhost */
#endif

    rc = cpumR3DbgInitTarget(pVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize the general guest CPU state.
     */
    CPUMR3Reset(pVM);

    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * The CPUM will update the addresses used by the switcher.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3Relocate(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Terminates the CPUM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) CPUMR3Term(PVM pVM)
{
#ifdef VBOX_WITH_CRASHDUMP_MAGIC
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        memset(pVCpu->cpum.s.aMagic, 0, sizeof(pVCpu->cpum.s.aMagic));
        pVCpu->cpum.s.uMagic      = 0;
        pvCpu->cpum.s.Guest.dr[5] = 0;
    }
#endif

    return cpumR3TermTarget(pVM);
}


/**
 * Resets the CPU.
 *
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3Reset(PVM pVM)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        CPUMR3ResetCpu(pVM, pVCpu);

#ifdef VBOX_WITH_CRASHDUMP_MAGIC

        /* Magic marker for searching in crash dumps. */
        strcpy((char *)pVCpu->.cpum.s.aMagic, "CPUMCPU Magic");
        pVCpu->cpum.s.uMagic       = UINT64_C(0xDEADBEEFDEADBEEF);
        pVCpu->cpum.s.Guest->dr[5] = UINT64_C(0xDEADBEEFDEADBEEF);
#endif
    }
}


/**
 * @callback_method_impl{FNSSMINTLOADPREP}
 */
static DECLCALLBACK(int) cpumR3LoadPrepCommon(PVM pVM, PSSMHANDLE pSSM)
{
    NOREF(pSSM);
    pVM->cpum.s.fPendingRestore = true;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTLOADDONE}
 */
static DECLCALLBACK(int) cpumR3LoadDoneCommon(PVM pVM, PSSMHANDLE pSSM)
{
    if (RT_FAILURE(SSMR3HandleGetStatus(pSSM)))
        return VINF_SUCCESS;

    /* just check this since we can. */ /** @todo Add a SSM unit flag for indicating that it's mandatory during a restore.  */
    if (pVM->cpum.s.fPendingRestore)
    {
        LogRel(("CPUM: Missing state!\n"));
        return VERR_INTERNAL_ERROR_2;
    }

    return cpumR3LoadDoneTarget(pVM, pSSM);
}


/**
 * Checks if the CPUM state restore is still pending.
 *
 * @returns true / false.
 * @param   pVM                 The cross context VM structure.
 */
VMMDECL(bool) CPUMR3IsStateRestorePending(PVM pVM)
{
    return pVM->cpum.s.fPendingRestore;
}


/**
 * Display all cpu states and any other cpum info.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoAll(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    cpumR3InfoGuest(pVM, pHlp, pszArgs);
    cpumR3InfoGuestInstr(pVM, pHlp, pszArgs);
#ifdef VBOX_VMM_TARGET_X86
    cpumR3InfoGuestHwvirt(pVM, pHlp, pszArgs);
    cpumR3InfoHyper(pVM, pHlp, pszArgs);
#endif
#ifdef RT_ARCH_AMD64
    cpumR3InfoHost(pVM, pHlp, pszArgs);
#endif
}


/**
 * Parses the info argument.
 *
 * The argument starts with 'verbose', 'terse' or 'default' and then
 * continues with the comment string.
 *
 * @param   pszArgs         The pointer to the argument string.
 * @param   penmType        Where to store the dump type request.
 * @param   ppszComment     Where to store the pointer to the comment string.
 */
DECLHIDDEN(void) cpumR3InfoParseArg(const char *pszArgs, CPUMDUMPTYPE *penmType, const char **ppszComment)
{
    if (!pszArgs)
    {
        *penmType = CPUMDUMPTYPE_DEFAULT;
        *ppszComment = "";
    }
    else
    {
        if (!strncmp(pszArgs, RT_STR_TUPLE("verbose")))
        {
            pszArgs += 7;
            *penmType = CPUMDUMPTYPE_VERBOSE;
        }
        else if (!strncmp(pszArgs, RT_STR_TUPLE("terse")))
        {
            pszArgs += 5;
            *penmType = CPUMDUMPTYPE_TERSE;
        }
        else if (!strncmp(pszArgs, RT_STR_TUPLE("default")))
        {
            pszArgs += 7;
            *penmType = CPUMDUMPTYPE_DEFAULT;
        }
        else
            *penmType = CPUMDUMPTYPE_DEFAULT;
        *ppszComment = RTStrStripL(pszArgs);
    }
}


/**
 * Display the guest cpu state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments.
 */
static DECLCALLBACK(void) cpumR3InfoGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    CPUMDUMPTYPE enmType;
    const char  *pszComment;
    cpumR3InfoParseArg(pszArgs, &enmType, &pszComment);

    PCVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    pHlp->pfnPrintf(pHlp, "Guest CPUM (VCPU %d) state: %s\n", pVCpu->idCpu, pszComment);

    cpumR3InfoOneTarget(pVM, pVCpu, pHlp, enmType);
}


/**
 * Display the current guest instruction
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoGuestInstr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    char szInstruction[256];
    szInstruction[0] = '\0';
    DBGFR3DisasInstrCurrent(pVCpu, szInstruction, sizeof(szInstruction));
    pHlp->pfnPrintf(pHlp, "\nCPUM%u: %s\n\n", pVCpu->idCpu, szInstruction);
}


#if defined(VBOX_VMM_TARGET_X86) || defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
/**
 * Formats the EFLAGS value into mnemonics.
 *
 * @param   pszEFlags   Where to write the mnemonics. (Assumes sufficient buffer space.)
 * @param   efl         The EFLAGS value with both guest hardware and VBox
 *                      internal bits included.
 */
DECLHIDDEN(void) cpumR3InfoFormatFlagsX86(char *pszEFlags, uint32_t efl)
{
    /*
     * Format the flags.
     */
    static const struct
    {
        const char *pszSet; const char *pszClear; uint32_t fFlag;
    }   s_aFlags[] =
    {
        { "vip",NULL, X86_EFL_VIP },
        { "vif",NULL, X86_EFL_VIF },
        { "ac", NULL, X86_EFL_AC },
        { "vm", NULL, X86_EFL_VM },
        { "rf", NULL, X86_EFL_RF },
        { "nt", NULL, X86_EFL_NT },
        { "ov", "nv", X86_EFL_OF },
        { "dn", "up", X86_EFL_DF },
        { "ei", "di", X86_EFL_IF },
        { "tf", NULL, X86_EFL_TF },
        { "nt", "pl", X86_EFL_SF },
        { "nz", "zr", X86_EFL_ZF },
        { "ac", "na", X86_EFL_AF },
        { "po", "pe", X86_EFL_PF },
        { "cy", "nc", X86_EFL_CF },
# ifdef VBOX_VMM_TARGET_X86
        { "inh-ss",  NULL, CPUMCTX_INHIBIT_SHADOW_SS },
        { "inh-sti", NULL, CPUMCTX_INHIBIT_SHADOW_STI },
        { "inh-nmi", NULL, CPUMCTX_INHIBIT_NMI },
# endif
    };
    char *psz = pszEFlags;
    for (unsigned i = 0; i < RT_ELEMENTS(s_aFlags); i++)
    {
        const char *pszAdd = s_aFlags[i].fFlag & efl ? s_aFlags[i].pszSet : s_aFlags[i].pszClear;
        if (pszAdd)
        {
            strcpy(psz, pszAdd);
            psz += strlen(pszAdd);
            *psz++ = ' ';
        }
    }
    psz[-1] = '\0';
}
#endif /* VBOX_VMM_TARGET_X86 || RT_ARCH_X86 || RT_ARCH_AMD64 */


#ifdef RT_ARCH_AMD64
/**
 * Display the host cpu state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoHost(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    CPUMDUMPTYPE enmType;
    const char *pszComment;
    cpumR3InfoParseArg(pszArgs, &enmType, &pszComment);
    pHlp->pfnPrintf(pHlp, "Host CPUM state: %s\n", pszComment);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    PCPUMHOSTCTX pCtx = &pVCpu->cpum.s.Host;

    /*
     * Format the EFLAGS.
     */
    uint64_t efl = pCtx->rflags;
    char szEFlags[80];
    cpumR3InfoFormatFlagsX86(&szEFlags[0], efl);

    /*
     * Format the registers.
     */
    pHlp->pfnPrintf(pHlp,
        "rax=xxxxxxxxxxxxxxxx rbx=%016RX64 rcx=xxxxxxxxxxxxxxxx\n"
        "rdx=xxxxxxxxxxxxxxxx rsi=%016RX64 rdi=%016RX64\n"
        "rip=xxxxxxxxxxxxxxxx rsp=%016RX64 rbp=%016RX64\n"
        " r8=xxxxxxxxxxxxxxxx  r9=xxxxxxxxxxxxxxxx r10=%016RX64\n"
        "r11=%016RX64 r12=%016RX64 r13=%016RX64\n"
        "r14=%016RX64 r15=%016RX64\n"
        "iopl=%d  %31s\n"
        "cs=%04x  ds=%04x  es=%04x  fs=%04x  gs=%04x                   eflags=%08RX64\n"
        "cr0=%016RX64 cr2=xxxxxxxxxxxxxxxx cr3=%016RX64\n"
        "cr4=%016RX64 ldtr=%04x tr=%04x\n"
        "dr[0]=%016RX64 dr[1]=%016RX64 dr[2]=%016RX64\n"
        "dr[3]=%016RX64 dr[6]=%016RX64 dr[7]=%016RX64\n"
        "gdtr=%016RX64:%04x  idtr=%016RX64:%04x\n"
        "SysEnter={cs=%04x eip=%08x esp=%08x}\n"
        "FSbase=%016RX64 GSbase=%016RX64 efer=%08RX64\n"
        ,
        /*pCtx->rax,*/ pCtx->rbx, /*pCtx->rcx,
        pCtx->rdx,*/ pCtx->rsi, pCtx->rdi,
        /*pCtx->rip,*/ pCtx->rsp, pCtx->rbp,
        /*pCtx->r8,  pCtx->r9,*/  pCtx->r10,
        pCtx->r11, pCtx->r12, pCtx->r13,
        pCtx->r14, pCtx->r15,
        X86_EFL_GET_IOPL(efl), szEFlags,
        pCtx->cs, pCtx->ds, pCtx->es, pCtx->fs, pCtx->gs, efl,
        pCtx->cr0, /*pCtx->cr2,*/ pCtx->cr3,
        pCtx->cr4, pCtx->ldtr, pCtx->tr,
        pCtx->dr0, pCtx->dr1, pCtx->dr2,
        pCtx->dr3, pCtx->dr6, pCtx->dr7,
        pCtx->gdtr.uAddr, pCtx->gdtr.cb, pCtx->idtr.uAddr, pCtx->idtr.cb,
        pCtx->SysEnter.cs, pCtx->SysEnter.eip, pCtx->SysEnter.esp,
        pCtx->FSbase, pCtx->GSbase, pCtx->efer);
}
#endif /* RT_ARCH_AMD64 */


#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
/**
 * @callback_method_impl{FNDBGFHANDLERINT,
 *      Handler for the 'cpufeathost' info item.}
 */
static DECLCALLBACK(void) cpumR3InfoCpuFeatHost(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF_PV(pszArgs);
    CPUMR3CpuIdPrintArmV8Features(pHlp, &pVM->cpum.s.HostFeatures.s, "Host", NULL, NULL);
}
#endif /* RT_ARCH_AMD64 || RT_ARCH_ARM32 */


/**
 * Called when the ring-3 init phase completes.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmWhat             Which init phase.
 */
VMMR3DECL(int) CPUMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    switch (enmWhat)
    {
        case VMINITCOMPLETED_RING3:
            return cpumR3InitCompletedRing3Target(pVM);

        default:
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Called when the ring-0 init phases completed.
 *
 * @param   pVM                 The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3LogCpuIdAndMsrFeatures(PVM pVM)
{
    /*
     * Enable log buffering as we're going to log a lot of lines.
     */
    bool const fOldBuffered = RTLogRelSetBuffering(true /*fBuffered*/);

    /*
     * Log the cpuid.
     */
    RTCPUSET OnlineSet;
    LogRel(("CPUM: Logical host processors: %u present, %u max, %u online, online mask: %016RX64\n",
            (unsigned)RTMpGetPresentCount(), (unsigned)RTMpGetCount(), (unsigned)RTMpGetOnlineCount(),
            RTCpuSetToU64(RTMpGetOnlineSet(&OnlineSet)) ));
    RTCPUID cCores = RTMpGetCoreCount();
    if (cCores)
        LogRel(("CPUM: Physical host cores: %u\n", (unsigned)cCores));
    LogRel(("************************ CPUID dump *************************\n"));
    DBGFR3Info(pVM->pUVM,     "cpuid", "verbose", DBGFR3InfoLogRelHlp());
    DBGFR3_INFO_LOG_SAFE(pVM, "cpuid", "verbose"); /* macro */
    LogRel(("********************* End of CPUID dump *********************\n"));

    /*
     * Do target specific logging.
     */
    cpumR3LogCpuIdAndMsrFeaturesTarget(pVM);

#if !(defined(RT_ARCH_AMD64) || defined(RT_ARCH_ARM32)) && !defined(VBOX_VMM_TARGET_ARMV8)
    LogRel(("********************* Host CPU features *********************\n"));
    DBGFR3Info(pVM->pUVM,     "cpufeathost", "", DBGFR3InfoLogRelHlp());
    DBGFR3_INFO_LOG_SAFE(pVM, "cpufeathost", "");
    LogRel(("***************** End of host CPU features ******************\n"));
#endif

#if  (defined(VBOX_VMM_TARGET_X86)   && !defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86)) \
  || (defined(VBOX_VMM_TARGET_ARMV8) && !defined(RT_ARCH_ARM64) && !defined(RT_ARCH_ARM32))
    /*
     * If the host and target differs, dump the host cpuid / features.
     */
    /** @todo cpuidhost   */
    //LogRel(("********************** Host CPUID dump **********************\n"));
    //DBGFR3Info(pVM->pUVM,     "cpuidhost", "verbose", DBGFR3InfoLogRelHlp());
    //DBGFR3_INFO_LOG_SAFE(pVM, "cpuidhost", "verbose");
    //LogRel(("****************** End of host CPUID dump *******************\n"));
#endif

    /*
     * Restore the log buffering state to what it was previously.
     */
    RTLogRelSetBuffering(fOldBuffered);
}

