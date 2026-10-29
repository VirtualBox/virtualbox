/* $Id: CPUMR0.cpp 113526 2026-03-24 08:45:34Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - Host Context Ring 0, only targeting x86.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_CPUM
#define CPUM_WITH_NONCONST_HOST_FEATURES
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vmm/hm.h>

#include <iprt/assert.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/x86.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Host CPU features. */
DECL_HIDDEN_DATA(CPUHOSTFEATURES)   g_CpumHostFeatures;
/** Static storage for host MSRs.
 * @note this isn't really used beyond module init. */
static SUPHWVIRTMSRS                g_CpumHostHwvirtMsrs;

/**
 * CPUID bits to unify among all cores.
 */
static struct
{
    uint32_t uLeaf;  /**< Leaf to check. */
    uint32_t uEcx;   /**< which bits in ecx to unify between CPUs. */
    uint32_t uEdx;   /**< which bits in edx to unify between CPUs. */
}
const g_aCpuidUnifyBits[] =
{
    {
        0x00000001,
        X86_CPUID_FEATURE_ECX_CX16 | X86_CPUID_FEATURE_ECX_MONITOR,
        X86_CPUID_FEATURE_EDX_CX8
    }
};



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  cpumR0SaveHostDebugState(PVMCPUCC pVCpu);


/**
 * Check the CPUID features of this particular CPU and disable relevant features
 * for the guest which do not exist on this CPU.
 *
 * We have seen systems where the X86_CPUID_FEATURE_ECX_MONITOR feature flag is
 * only set on some host CPUs, see @bugref{5436}.
 *
 * @note This function might be called simultaneously on more than one CPU!
 *
 * @param   idCpu       The identifier for the CPU the function is called on.
 * @param   pvUser1     Leaf array.
 * @param   pvUser2     Number of leaves.
 */
static DECLCALLBACK(void) cpumR0CheckCpuid(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PCPUMCPUIDLEAF const paLeaves = (PCPUMCPUIDLEAF)pvUser1;
    uint32_t const       cLeaves  = (uint32_t)(uintptr_t)pvUser2;
    RT_NOREF(idCpu);

    for (uint32_t i = 0; i < RT_ELEMENTS(g_aCpuidUnifyBits); i++)
    {
        PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, g_aCpuidUnifyBits[i].uLeaf, 0);
        if (pLeaf)
        {
            uint32_t uEax, uEbx, uEcx, uEdx;
            ASMCpuIdExSlow(g_aCpuidUnifyBits[i].uLeaf, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);

            ASMAtomicAndU32(&pLeaf->uEcx, uEcx | ~g_aCpuidUnifyBits[i].uEcx);
            ASMAtomicAndU32(&pLeaf->uEdx, uEdx | ~g_aCpuidUnifyBits[i].uEdx);
        }
    }
}


/**
 * Does the Ring-0 CPU initialization once during module load.
 * XXX Host-CPU hot-plugging?
 */
VMMR0_INT_DECL(int) CPUMR0ModuleInit(void)
{
    /*
     * Query the hardware virtualization capabilities of the host CPU first.
     */
    uint32_t fHwCaps = 0;
    int rc = SUPR0GetVTSupport(&fHwCaps);
    AssertLogRelMsg(RT_SUCCESS(rc) || rc == VERR_UNSUPPORTED_CPU || rc == VERR_SVM_NO_SVM || rc == VERR_VMX_NO_VMX,
                    ("SUPR0GetHwvirtMsrs -> %Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        rc = SUPR0GetHwvirtMsrs(&g_CpumHostHwvirtMsrs, fHwCaps, false /*fIgnored*/);
        AssertLogRelRC(rc);
        if (RT_FAILURE(rc))
            RT_ZERO(g_CpumHostHwvirtMsrs);
    }

    /*
     * Collect CPUID leaves.
     */
    PCPUMCPUIDLEAF  paLeaves;
    uint32_t        cLeaves;
    rc = CPUMCpuIdCollectLeavesFromX86Host(&paLeaves, &cLeaves);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Unify/cross check some CPUID feature bits on all available CPU cores
     * and threads.  We've seen CPUs where the monitor support differed.
     */
    RTMpOnAll(cpumR0CheckCpuid, paLeaves, (void *)(uintptr_t)cLeaves);

    /*
     * Populate the host CPU feature global variable.
     */
    rc = CPUMCpuIdExplodeFeaturesX86(paLeaves, cLeaves, &g_CpumHostFeatures.s);
    RTMemFree(paLeaves);
    AssertLogRelRCReturn(rc, rc);
    if (g_CpumHostFeatures.s.fVmx)
        cpumCpuIdExplodeFeaturesX86VmxFromSupMsrs(&g_CpumHostHwvirtMsrs, &g_CpumHostFeatures.s);

    /*
     * Get MSR_IA32_ARCH_CAPABILITIES and expand it into the host feature structure.
     *
     * AMD CPUs doesn't have this register, similar info is available in EBX in
     * CPUID leaf 0x80000008
     */
    if (ASMHasCpuId())
    {
        uint32_t const cStdRange = ASMCpuId_EAX(0);
        if (   RTX86IsValidStdRange(cStdRange)
            && cStdRange >= 7)
        {
            uint32_t const fStdFeaturesEdx = ASMCpuId_EDX(1);
            uint32_t fStdExtFeaturesEdx;
            ASMCpuIdExSlow(7, 0, 0, 0, NULL, NULL, NULL, &fStdExtFeaturesEdx);
            if (   (fStdExtFeaturesEdx & X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP)
                && (fStdFeaturesEdx    & X86_CPUID_FEATURE_EDX_MSR))
                cpumCpuIdExplodeArchCapabilities(&g_CpumHostFeatures.s, true, ASMRdMsr(MSR_IA32_ARCH_CAPABILITIES));
        }
    }

    return VINF_SUCCESS;
}


/**
 * Terminate the module.
 */
VMMR0_INT_DECL(int) CPUMR0ModuleTerm(void)
{
    return VINF_SUCCESS;
}


/**
 * Initializes the CPUM data in the VM structure.
 *
 * @param   pGVM        The global VM structure.
 */
VMMR0_INT_DECL(void) CPUMR0InitPerVMData(PGVM pGVM)
{
    /* Copy the ring-0 host feature set to the shared part so ring-3 can pick it up. */
    pGVM->cpum.s.HostFeatures.s = g_CpumHostFeatures.s;
}


/**
 * Check the CPUID features of this particular CPU and disable relevant features
 * for the guest which do not exist on this CPU. We have seen systems where the
 * X86_CPUID_FEATURE_ECX_MONITOR feature flag is only set on some host CPUs, see
 * @bugref{5436}.
 *
 * @note This function might be called simultaneously on more than one CPU!
 *
 * @param   idCpu       The identifier for the CPU the function is called on.
 * @param   pvUser1     Pointer to the VM structure.
 * @param   pvUser2     Ignored.
 */
static DECLCALLBACK(void) cpumR0CheckCpuidLegacy(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PVMCC     pVM   = (PVMCC)pvUser1;

    NOREF(idCpu); NOREF(pvUser2);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aCpuidUnifyBits); i++)
    {
        /* Note! Cannot use cpumCpuIdGetLeaf from here because we're not
                 necessarily in the VM process context.  So, we using the
                 legacy arrays as temporary storage. */

        uint32_t   uLeaf = g_aCpuidUnifyBits[i].uLeaf;
        PCPUMCPUID pLegacyLeaf;
        if (uLeaf < RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmStd))
            pLegacyLeaf = &pVM->cpum.s.aGuestCpuIdPatmStd[uLeaf];
        else if (uLeaf - UINT32_C(0x80000000) < RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmExt))
            pLegacyLeaf = &pVM->cpum.s.aGuestCpuIdPatmExt[uLeaf - UINT32_C(0x80000000)];
        else if (uLeaf - UINT32_C(0xc0000000) < RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmCentaur))
            pLegacyLeaf = &pVM->cpum.s.aGuestCpuIdPatmCentaur[uLeaf - UINT32_C(0xc0000000)];
        else
            continue;

        uint32_t eax, ebx, ecx, edx;
        ASMCpuIdExSlow(uLeaf, 0, 0, 0, &eax, &ebx, &ecx, &edx);

        ASMAtomicAndU32(&pLegacyLeaf->uEcx, ecx | ~g_aCpuidUnifyBits[i].uEcx);
        ASMAtomicAndU32(&pLegacyLeaf->uEdx, edx | ~g_aCpuidUnifyBits[i].uEdx);
    }
}


/**
 * Does Ring-0 CPUM initialization.
 *
 * This is mainly to check that the Host CPU mode is compatible
 * with VBox.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR0_INT_DECL(int) CPUMR0InitVM(PVMCC pVM)
{
    LogFlow(("CPUMR0Init: %p\n", pVM));
    AssertCompile(sizeof(pVM->aCpus[0].cpum.s.Host.abXState) >= sizeof(pVM->aCpus[0].cpum.s.Guest.abXState));

    /*
     * Check CR0 & CR4 flags.
     */
    uint32_t u32CR0 = ASMGetCR0();
    if ((u32CR0 & (X86_CR0_PE | X86_CR0_PG)) != (X86_CR0_PE | X86_CR0_PG)) /* a bit paranoid perhaps.. */
    {
        Log(("CPUMR0Init: PE or PG not set. cr0=%#x\n", u32CR0));
        return VERR_UNSUPPORTED_CPU_MODE;
    }

    /*
     * Check for sysenter and syscall usage.
     */
    if (ASMHasCpuId())
    {
        /*
         * SYSENTER/SYSEXIT
         *
         * Intel docs claim you should test both the flag and family, model &
         * stepping because some Pentium Pro CPUs have the SEP cpuid flag set,
         * but don't support it.  AMD CPUs may support this feature in legacy
         * mode, they've banned it from long mode.  Since we switch to 32-bit
         * mode when entering raw-mode context the feature would become
         * accessible again on AMD CPUs, so we have to check regardless of
         * host bitness.
         */
        uint32_t u32CpuVersion;
        uint32_t u32Dummy;
        uint32_t fFeatures; /* (Used further down to check for MSRs, so don't clobber.) */
        ASMCpuId(1, &u32CpuVersion, &u32Dummy, &u32Dummy, &fFeatures);
        uint32_t const u32Family   = u32CpuVersion >> 8;
        uint32_t const u32Model    = (u32CpuVersion >> 4) & 0xF;
        uint32_t const u32Stepping = u32CpuVersion & 0xF;
        if (    (fFeatures & X86_CPUID_FEATURE_EDX_SEP)
            &&  (   u32Family   != 6    /* (> pentium pro) */
                 || u32Model    >= 3
                 || u32Stepping >= 3
                 || !ASMIsIntelCpu())
           )
        {
            /*
             * Read the MSR and see if it's in use or not.
             */
            uint32_t u32 = ASMRdMsr_Low(MSR_IA32_SYSENTER_CS);
            if (u32)
            {
                pVM->cpum.s.fHostUseFlags |= CPUM_USE_SYSENTER;
                Log(("CPUMR0Init: host uses sysenter cs=%08x%08x\n", ASMRdMsr_High(MSR_IA32_SYSENTER_CS), u32));
            }
        }

        /*
         * SYSCALL/SYSRET
         *
         * This feature is indicated by the SEP bit returned in EDX by CPUID
         * function 0x80000001.  Intel CPUs only supports this feature in
         * long mode.  Since we're not running 64-bit guests in raw-mode there
         * are no issues with 32-bit intel hosts.
         */
        uint32_t cExt = 0;
        ASMCpuId(0x80000000, &cExt, &u32Dummy, &u32Dummy, &u32Dummy);
        if (RTX86IsValidExtRange(cExt))
        {
            uint32_t fExtFeaturesEDX = ASMCpuId_EDX(0x80000001);
            if (fExtFeaturesEDX & X86_CPUID_EXT_FEATURE_EDX_SYSCALL)
            {
#ifdef RT_ARCH_X86
                if (!ASMIsIntelCpu())
#endif
                {
                    uint64_t fEfer = ASMRdMsr(MSR_K6_EFER);
                    if (fEfer & MSR_K6_EFER_SCE)
                    {
                        pVM->cpum.s.fHostUseFlags |= CPUM_USE_SYSCALL;
                        Log(("CPUMR0Init: host uses syscall\n"));
                    }
                }
            }
        }

        /*
         * Copy MSR_IA32_ARCH_CAPABILITIES bits over into the host and guest feature
         * structure and as well as the guest MSR.
         * Note! We assume this happens after the CPUMR3Init is done, so CPUID bits are settled.
         */
        /** @todo Should add this MSR to CPUMMSRS and expose it via SUPDrv... */
        uint32_t const cStdRange = ASMCpuId_EAX(0);
        if (   RTX86IsValidStdRange(cStdRange)
            && cStdRange >= 7)
        {
            uint32_t fEdxFeatures;
            ASMCpuId_Idx_ECX(7, 0, &u32Dummy, &u32Dummy, &u32Dummy, &fEdxFeatures);
            if (   (fEdxFeatures & X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP)
                && (fFeatures & X86_CPUID_FEATURE_EDX_MSR))
                CPUMCpuIdApplyX86HostArchCapabilities(pVM, true, ASMRdMsr(MSR_IA32_ARCH_CAPABILITIES));
        }

        /*
         * Unify/cross check some CPUID feature bits on all available CPU cores
         * and threads.  We've seen CPUs where the monitor support differed.
         *
         * Because the hyper heap isn't always mapped into ring-0, we cannot
         * access it from a RTMpOnAll callback.  We use the legacy CPUID arrays
         * as temp ring-0 accessible memory instead, ASSUMING that they're all
         * up to date when we get here.
         */
        RTMpOnAll(cpumR0CheckCpuidLegacy, pVM, NULL);

        for (uint32_t i = 0; i < RT_ELEMENTS(g_aCpuidUnifyBits); i++)
        {
            bool            fIgnored;
            uint32_t        uLeaf = g_aCpuidUnifyBits[i].uLeaf;
            PCPUMCPUIDLEAF  pLeaf = cpumCpuIdGetLeafEx(pVM, uLeaf, 0, &fIgnored);
            if (pLeaf)
            {
                PCPUMCPUID pLegacyLeaf;
                if (uLeaf < RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmStd))
                    pLegacyLeaf = &pVM->cpum.s.aGuestCpuIdPatmStd[uLeaf];
                else if (uLeaf - UINT32_C(0x80000000) < RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmExt))
                    pLegacyLeaf = &pVM->cpum.s.aGuestCpuIdPatmExt[uLeaf - UINT32_C(0x80000000)];
                else if (uLeaf - UINT32_C(0xc0000000) < RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmCentaur))
                    pLegacyLeaf = &pVM->cpum.s.aGuestCpuIdPatmCentaur[uLeaf - UINT32_C(0xc0000000)];
                else
                    continue;

                pLeaf->uEcx = pLegacyLeaf->uEcx;
                pLeaf->uEdx = pLegacyLeaf->uEdx;
            }
        }

    }


    /*
     * Check if debug registers are armed.
     * This ASSUMES that DR7.GD is not set, or that it's handled transparently!
     */
    uint32_t u32DR7 = ASMGetDR7();
    if (u32DR7 & X86_DR7_ENABLED_MASK)
    {
        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->cpum.s.fUseFlags |= CPUM_USE_DEBUG_REGS_HOST);
        Log(("CPUMR0Init: host uses debug registers (dr7=%x)\n", u32DR7));
    }

    return VINF_SUCCESS;
}


/**
 * Saves the host-FPU/XMM state (if necessary) and (always) loads the guest-FPU
 * state into the CPU.
 *
 * @returns VINF_SUCCESS on success, host CR0 unmodified.
 * @returns VINF_CPUM_HOST_CR0_MODIFIED on success when the host CR0 was
 *          modified and VT-x needs to update the value in the VMCS.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static int cpumR0LoadGuestFPU(PVMCC pVM, PVMCPUCC pVCpu)
{
    STAM_PROFILE_START(&pVCpu->cpum.s.StatGuestFpuLoadPerf, x);
    STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatGuestFpuLoad);

    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(!(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST));

    /*
     * Prep the host FPU state as required.
     */
    if (!(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_HOST))
    {
        Assert(pVCpu->cpumr0.s.fFpuBegin == 0);
        pVCpu->cpumr0.s.fFpuBegin = SUPR0FpuBegin(VMMR0ThreadCtxHookIsEnabled(pVCpu));
        /** @todo use return value? Currently skipping that to be on the safe side
         *        wrt. extended state (linux). */
    }
    else
    {
        if (pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE)
            SUPR0FpuEnsureCurrent(pVCpu->cpumr0.s.fFpuBegin);
    }

    /*
     * Load the guest state, after first saving (if required) the host state.
     * The leaky fxsave stuff make this extra fun.
     */
    int rc;
    if (!pVM->cpum.s.HostFeatures.s.fLeakyFxSR) /** @todo use ring-0 version? */
    {
        Assert(!(pVCpu->cpum.s.fUseFlags & CPUM_USED_MANUAL_XMM_RESTORE));
        rc = cpumR0SaveHostRestoreGuestFPUState(&pVCpu->cpum.s);
    }
    else
    {
        Assert(!(pVCpu->cpum.s.fUseFlags & CPUM_USED_MANUAL_XMM_RESTORE) || (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_HOST));
        /** @todo r=ramshankar: Can't we used a cached value here
         *        instead of reading the MSR? host EFER doesn't usually
         *        change. */
        uint64_t uHostEfer = ASMRdMsr(MSR_K6_EFER);
        if (!(uHostEfer & MSR_K6_EFER_FFXSR))
            rc = cpumR0SaveHostRestoreGuestFPUState(&pVCpu->cpum.s);
        else
        {
            RTCCUINTREG const uSavedFlags = ASMIntDisableFlags();
            pVCpu->cpum.s.fUseFlags |= CPUM_USED_MANUAL_XMM_RESTORE;
            ASMWrMsr(MSR_K6_EFER, uHostEfer & ~MSR_K6_EFER_FFXSR);
            rc = cpumR0SaveHostRestoreGuestFPUState(&pVCpu->cpum.s);
            ASMWrMsr(MSR_K6_EFER, uHostEfer | MSR_K6_EFER_FFXSR);
            ASMSetFlags(uSavedFlags);
        }
    }
    Assert(   (pVCpu->cpum.s.fUseFlags & (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST | CPUM_USED_FPU_SINCE_REM))
           ==                            (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST | CPUM_USED_FPU_SINCE_REM));
    Assert(pVCpu->cpum.s.Guest.fUsedFpuGuest);

    STAM_PROFILE_STOP(&pVCpu->cpum.s.StatGuestFpuLoadPerf, x);
    return rc;
}


/**
 * SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE mode: Make sure the host has our
 * state loaded (whatever it holds).
 *
 * Linux may kick it out if an interrupt handler or similar wanted to use SIMD
 * instructions for something (typically crypto or raid checksums).
 *
 * @note Must be called with interrupts disabled!
 */
DECL_FORCE_INLINE(void) cpumR0FpuEnsureHostCurrent(PVMCPUCC pVCpu)
{
    if (pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE)
    {
        if (!SUPR0FpuEnsureCurrent(pVCpu->cpumr0.s.fFpuBegin))
        { /* probable */ }
        else
        {
            /** @todo load FPUDS & FPUCS as needed. */
            STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatGuestFpuReload);
        }
    }
}


/**
 * Called by the AMD-V & VT-x code just before running guest code to ensure the
 * guest FPU stat is loaded.
 *
 * @returns VINF_SUCCESS on success, host CR0 unmodified.
 * @returns VINF_CPUM_HOST_CR0_MODIFIED on success when the host CR0 was
 *          modified and VT-x needs to update the value in the VMCS.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fUnlock Whether to unlock the host kernel FPU or not.
 *                  This is mainly for linux hosts where IEM have to lock the
 *                  kernel FPU before it can use it, causing preemption to be
 *                  disabled.
 */
VMMR0_INT_DECL(int) CPUMR0EnsureLoadedGuestFPU(PVMCC pVM, PVMCPUCC pVCpu, bool fUnlock)
{
    Assert(!ASMIntAreEnabled());
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    /*
     * If locked, automatically unlock it, if requested.
     */
    if (fUnlock)
    {
        /** @todo move this to post-execution action. */
        if (   (pVCpu->cpumr0.s.fFpuBegin & (SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE | SUPR0FPU_BEGIN_F_HOST_STATE_LOCKED))
            == (SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE | SUPR0FPU_BEGIN_F_HOST_STATE_LOCKED) )
            pVCpu->cpumr0.s.fFpuBegin = SUPR0FpuUnlock(pVCpu->cpumr0.s.fFpuBegin);
    }

    /*
     * Load the FPU state if not loaded yet.
     */
    Assert(RT_BOOL(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST) == pVCpu->cpum.s.Guest.fUsedFpuGuest);
    if (!(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST))
        return cpumR0LoadGuestFPU(pVM, pVCpu);

    /*
     * If loaded, make sure it's current with the host OS.
     *
     * Linux may kick it out if an interrupt handler or similar wanted to use
     * SIMD instructions for something (typically crypto or raid checksums).
     */
    cpumR0FpuEnsureHostCurrent(pVCpu);

    return VINF_SUCCESS;
}


/**
 * Saves the guest FPU/XMM state if needed, restores the host FPU/XMM state as
 * needed.
 *
 * @returns true if we saved the guest state.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(bool) CPUMR0FpuStateMaybeSaveGuestAndRestoreHost(PVMCPUCC pVCpu)
{
    Assert(pVCpu->CTX_SUFF(pVM)->cpum.s.HostFeatures.s.fFxSaveRstor);
    Assert(ASMGetCR4() & X86_CR4_OSFXSR);

    bool fSavedGuest = false;
    if (pVCpu->cpum.s.fUseFlags & (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST))
    {
        RTCCUINTREG const fIntFlags = ASMIntDisableFlags();

        if (pVCpu->cpum.s.fUseFlags & (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST))
        {
            /* Make sure the FPU stat is ours and that the host hasn't replaced it. */
            if (   (pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE)
                && SUPR0FpuEnsureCurrent(pVCpu->cpumr0.s.fFpuBegin))
            {
                /** @todo load FPUDS & FPUCS as needed. */
                STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatGuestFpuReload);
            }

            /* Do the actual state swapping. */
            fSavedGuest = RT_BOOL(pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST);
            Assert(fSavedGuest == pVCpu->cpum.s.Guest.fUsedFpuGuest);
            if (!(pVCpu->cpum.s.fUseFlags & CPUM_USED_MANUAL_XMM_RESTORE))
                cpumR0SaveGuestRestoreHostFPUState(&pVCpu->cpum.s);
            else
            {
                /* Temporarily clear MSR_K6_EFER_FFXSR or else we'll be unable to
                   save/restore the XMM state with fxsave/fxrstor. */
                uint64_t uHostEfer = ASMRdMsr(MSR_K6_EFER);
                if (uHostEfer & MSR_K6_EFER_FFXSR)
                {
                    RTCCUINTREG const uSavedFlags = ASMIntDisableFlags();
                    ASMWrMsr(MSR_K6_EFER, uHostEfer & ~MSR_K6_EFER_FFXSR);
                    cpumR0SaveGuestRestoreHostFPUState(&pVCpu->cpum.s);
                    ASMWrMsr(MSR_K6_EFER, uHostEfer | MSR_K6_EFER_FFXSR);
                    ASMSetFlags(uSavedFlags);
                }
                else
                    cpumR0SaveGuestRestoreHostFPUState(&pVCpu->cpum.s);
                pVCpu->cpum.s.fUseFlags &= ~CPUM_USED_MANUAL_XMM_RESTORE;
            }

            /* Notify the support driver after loading the host-FPU register state. */
            SUPR0FpuEnd(pVCpu->cpumr0.s.fFpuBegin);
            pVCpu->cpumr0.s.fFpuBegin = 0;
        }

        ASMSetFlags(fIntFlags);
    }

    /* State sanity checks (a bit too much state associated with this stuff, perhaps). */
    Assert(pVCpu->cpumr0.s.fFpuBegin == 0);
    AssertMsg(!(  pVCpu->cpum.s.fUseFlags
                & (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST | CPUM_USED_MANUAL_XMM_RESTORE)), ("%#x\n", pVCpu->cpum.s.fUseFlags));
    Assert(!pVCpu->cpum.s.Guest.fUsedFpuGuest);
    return fSavedGuest;
}


/**
 * Prepares the host FPU/SSE/AVX stuff for IEM action.
 *
 * This will make sure the FPU/SSE/AVX guest state is _not_ loaded in the CPU.
 * This will make sure the FPU/SSE/AVX host state is saved.
 * Finally, it will make sure the FPU/SSE/AVX host features can be safely
 * accessed.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMR0FpuStatePrepareHostCpuForUse(PVMCPUCC pVCpu)
{
    /** @todo
     * On linux, we'll lock the host FPU state here on 6.15+, basically
     * disabling preemption and whatnot.  We'll unlock later when
     * CPUMR0EnsureLoadedGuestFPU is called.  This isn't quite optimal, but
     * the easiest solution for now...
     */
    RTCCUINTREG const fSavedFlags = ASMIntDisableFlags();
    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_FPU_REM;
    switch (pVCpu->cpum.s.fUseFlags & (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST))
    {
        case 0:
            Assert(pVCpu->cpumr0.s.fFpuBegin == 0);
            pVCpu->cpumr0.s.fFpuBegin = SUPR0FpuBegin(VMMR0ThreadCtxHookIsEnabled(pVCpu));
            if (pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE)
            {
                STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatPrepHostFpu0Lock);
                pVCpu->cpumr0.s.fFpuBegin = SUPR0FpuLock(pVCpu->cpumr0.s.fFpuBegin);
            }
            else
                STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatPrepHostFpu0NoLock);

            if (cpumRZSaveHostFPUState(&pVCpu->cpum.s) == VINF_CPUM_HOST_CR0_MODIFIED)
                HMR0NotifyCpumModifiedHostCr0(pVCpu);
            Log6(("CPUMR0FpuStatePrepareHostCpuForUse: #0 - %#x/%#x/%#x\n",
                  (uint32_t)ASMGetCR0(), pVCpu->cpumr0.s.fFpuBegin, pVCpu->cpum.s.fUseFlags));
            break;

        case CPUM_USED_FPU_HOST:
            if (pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE)
            {
                if (!(pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_STATE_LOCKED))
                {
                    pVCpu->cpumr0.s.fFpuBegin = SUPR0FpuLock(pVCpu->cpumr0.s.fFpuBegin);
                    STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatPrepHostFpu1Lock);
                }
                else
                {
                    Assert(!SUPR0FpuEnsureCurrent(pVCpu->cpumr0.s.fFpuBegin));
                    STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatPrepHostFpu1NoLock);
                }
            }
            Log6(("CPUMR0FpuStatePrepareHostCpuForUse: #1 - %#x\n", ASMGetCR0()));
            break;

        case CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST:
            if (pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_MAY_REPLACE_STATE)
            {
                if (!(pVCpu->cpumr0.s.fFpuBegin & SUPR0FPU_BEGIN_F_HOST_STATE_LOCKED))
                {
                    STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatPrepHostFpu2Lock);
                    pVCpu->cpumr0.s.fFpuBegin = SUPR0FpuLock(pVCpu->cpumr0.s.fFpuBegin);
                }
                else
                {
                    STAM_REL_COUNTER_INC(&pVCpu->cpum.s.StatPrepHostFpu2NoLock);
                    Assert(!SUPR0FpuEnsureCurrent(pVCpu->cpumr0.s.fFpuBegin));
                }
            }
            cpumRZSaveGuestFpuState(&pVCpu->cpum.s, true /*fLeaveFpuAccessible*/);
            HMR0NotifyCpumUnloadedGuestFpuState(pVCpu);
            Log6(("CPUMR0FpuStatePrepareHostCpuForUse: #2 - %#x/%#x/%#x\n",
                  (uint32_t)ASMGetCR0(), pVCpu->cpumr0.s.fFpuBegin, pVCpu->cpum.s.fUseFlags));
            break;

        default:
            AssertFailed();
    }
    ASMSetFlags(fSavedFlags);
}


/**
 * Makes sure the FPU/SSE/AVX guest state is saved in CPUMCPU::Guest and will be
 * reloaded before direct use.
 *
 * No promisses about the FPU/SSE/AVX host features are made.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMR0FpuStateActualizeForChange(PVMCPUCC pVCpu)
{
    CPUMR0FpuStatePrepareHostCpuForUse(pVCpu);
}


/**
 * Makes sure the FPU/SSE/AVX state in CPUMCPU::Guest is up to date.
 *
 * This will not cause CPUM_USED_FPU_GUEST to change.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMR0FpuStateActualizeForRead(PVMCPUCC pVCpu)
{
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
    {
        RTCCUINTREG const fSavedFlags = ASMIntDisableFlags();
        if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
        {
            Assert(pVCpu->cpum.s.Guest.fUsedFpuGuest);
            cpumR0FpuEnsureHostCurrent(pVCpu);
            cpumRZSaveGuestFpuState(&pVCpu->cpum.s, false /*fLeaveFpuAccessible*/);
            pVCpu->cpum.s.fUseFlags |= CPUM_USED_FPU_GUEST;
            pVCpu->cpum.s.Guest.fUsedFpuGuest = true;
            Log7(("CPUMR0FpuStateActualizeForRead\n"));
        }
        ASMSetFlags(fSavedFlags);
    }
}


/**
 * Makes sure the XMM0..XMM15 and MXCSR state in CPUMCPU::Guest is up to date.
 *
 * This will not cause CPUM_USED_FPU_GUEST to change.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(void)    CPUMR0FpuStateActualizeSseForRead(PVMCPUCC pVCpu)
{
#if defined(VBOX_WITH_KERNEL_USING_XMM)
    NOREF(pVCpu);
#else
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
    {
        RTCCUINTREG const fSavedFlags = ASMIntDisableFlags();
        if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
        {
            Assert(pVCpu->cpum.s.Guest.fUsedFpuGuest);
            cpumR0FpuEnsureHostCurrent(pVCpu);
            cpumRZSaveGuestSseRegisters(&pVCpu->cpum.s);
            Log7(("CPUMR0FpuStateActualizeSseForRead\n"));
        }
        ASMSetFlags(fSavedFlags);
    }
#endif
}


/**
 * Makes sure the YMM0..YMM15 and MXCSR state in CPUMCPU::Guest is up to date.
 *
 * This will not cause CPUM_USED_FPU_GUEST to change.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(void)    CPUMR0FpuStateActualizeAvxForRead(PVMCPUCC pVCpu)
{
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
    {
        RTCCUINTREG const fSavedFlags = ASMIntDisableFlags();
        if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
        {
            Assert(pVCpu->cpum.s.Guest.fUsedFpuGuest);
            cpumR0FpuEnsureHostCurrent(pVCpu);
            cpumRZSaveGuestAvxRegisters(&pVCpu->cpum.s);
            Log7(("CPUMR0FpuStateActualizeAvxForRead\n"));
        }
        ASMSetFlags(fSavedFlags);
    }
}



/**
 * Saves the host debug state, setting CPUM_USED_HOST_DEBUG_STATE and loading
 * DR7 with safe values.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int cpumR0SaveHostDebugState(PVMCPUCC pVCpu)
{
    /*
     * Save the host state.
     */
    pVCpu->cpum.s.Host.dr0 = ASMGetDR0();
    pVCpu->cpum.s.Host.dr1 = ASMGetDR1();
    pVCpu->cpum.s.Host.dr2 = ASMGetDR2();
    pVCpu->cpum.s.Host.dr3 = ASMGetDR3();
    pVCpu->cpum.s.Host.dr6 = ASMGetDR6();
    /** @todo dr7 might already have been changed to 0x400; don't care right now as it's harmless. */
    pVCpu->cpum.s.Host.dr7 = ASMGetDR7();

    /* Preemption paranoia. */
    ASMAtomicOrU32(&pVCpu->cpum.s.fUseFlags, CPUM_USED_DEBUG_REGS_HOST);

    /*
     * Make sure DR7 is harmless or else we could trigger breakpoints when
     * load guest or hypervisor DRx values later.
     */
    if (pVCpu->cpum.s.Host.dr7 != X86_DR7_INIT_VAL)
        ASMSetDR7(X86_DR7_INIT_VAL);

    return VINF_SUCCESS;
}


/**
 * Saves the guest DRx state residing in host registers and restore the host
 * register values.
 *
 * The guest DRx state is only saved if CPUMR0LoadGuestDebugState was called,
 * since it's assumed that we're shadowing the guest DRx register values
 * accurately when using the combined hypervisor debug register values
 * (CPUMR0LoadHyperDebugState).
 *
 * @returns true if either guest or hypervisor debug registers were loaded.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   fDr6        Whether to include DR6 or not.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(bool) CPUMR0DebugStateMaybeSaveGuestAndRestoreHost(PVMCPUCC pVCpu, bool fDr6)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    bool const fDrXLoaded = RT_BOOL(pVCpu->cpum.s.fUseFlags & (CPUM_USED_DEBUG_REGS_GUEST | CPUM_USED_DEBUG_REGS_HYPER));

    /*
     * Do we need to save the guest DRx registered loaded into host registers?
     * (DR7 and DR6 (if fDr6 is true) are left to the caller.)
     */
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_DEBUG_REGS_GUEST)
    {
        pVCpu->cpum.s.Guest.dr[0] = ASMGetDR0();
        pVCpu->cpum.s.Guest.dr[1] = ASMGetDR1();
        pVCpu->cpum.s.Guest.dr[2] = ASMGetDR2();
        pVCpu->cpum.s.Guest.dr[3] = ASMGetDR3();
        if (fDr6)
            pVCpu->cpum.s.Guest.dr[6] = ASMGetDR6() | X86_DR6_RA1_MASK; /* ASSUMES no guest supprot for TSX-NI / RTM. */
    }
    ASMAtomicAndU32(&pVCpu->cpum.s.fUseFlags, ~(CPUM_USED_DEBUG_REGS_GUEST | CPUM_USED_DEBUG_REGS_HYPER));

    /*
     * Restore the host's debug state. DR0-3, DR6 and only then DR7!
     */
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_DEBUG_REGS_HOST)
    {
        /* A bit of paranoia first... */
        uint64_t uCurDR7 = ASMGetDR7();
        if (uCurDR7 != X86_DR7_INIT_VAL)
            ASMSetDR7(X86_DR7_INIT_VAL);

        ASMSetDR0(pVCpu->cpum.s.Host.dr0);
        ASMSetDR1(pVCpu->cpum.s.Host.dr1);
        ASMSetDR2(pVCpu->cpum.s.Host.dr2);
        ASMSetDR3(pVCpu->cpum.s.Host.dr3);
        /** @todo consider only updating if they differ, esp. DR6. Need to figure how
         *        expensive DRx reads are over DRx writes.  */
        ASMSetDR6(pVCpu->cpum.s.Host.dr6);
        ASMSetDR7(pVCpu->cpum.s.Host.dr7);

        ASMAtomicAndU32(&pVCpu->cpum.s.fUseFlags, ~CPUM_USED_DEBUG_REGS_HOST);
    }

    return fDrXLoaded;
}


/**
 * Saves the guest DRx state if it resides host registers.
 *
 * This does NOT clear any use flags, so the host registers remains loaded with
 * the guest DRx state upon return.  The purpose is only to make sure the values
 * in the CPU context structure is up to date.
 *
 * @returns true if the host registers contains guest values, false if not.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   fDr6        Whether to include DR6 or not.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(bool) CPUMR0DebugStateMaybeSaveGuest(PVMCPUCC pVCpu, bool fDr6)
{
    /*
     * Do we need to save the guest DRx registered loaded into host registers?
     * (DR7 and DR6 (if fDr6 is true) are left to the caller.)
     */
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_DEBUG_REGS_GUEST)
    {
        pVCpu->cpum.s.Guest.dr[0] = ASMGetDR0();
        pVCpu->cpum.s.Guest.dr[1] = ASMGetDR1();
        pVCpu->cpum.s.Guest.dr[2] = ASMGetDR2();
        pVCpu->cpum.s.Guest.dr[3] = ASMGetDR3();
        if (fDr6)
            pVCpu->cpum.s.Guest.dr[6] = ASMGetDR6();
        return true;
    }
    return false;
}


/**
 * Lazily sync in the debug state.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   fDr6        Whether to include DR6 or not.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(void) CPUMR0LoadGuestDebugState(PVMCPUCC pVCpu, bool fDr6)
{
    /*
     * Save the host state and disarm all host BPs.
     */
    cpumR0SaveHostDebugState(pVCpu);
    Assert(ASMGetDR7() == X86_DR7_INIT_VAL);

    /*
     * Activate the guest state DR0-3.
     * DR7 and DR6 (if fDr6 is true) are left to the caller.
     */
    ASMSetDR0(pVCpu->cpum.s.Guest.dr[0]);
    ASMSetDR1(pVCpu->cpum.s.Guest.dr[1]);
    ASMSetDR2(pVCpu->cpum.s.Guest.dr[2]);
    ASMSetDR3(pVCpu->cpum.s.Guest.dr[3]);
    if (fDr6)
        ASMSetDR6(pVCpu->cpum.s.Guest.dr[6]);

    ASMAtomicOrU32(&pVCpu->cpum.s.fUseFlags, CPUM_USED_DEBUG_REGS_GUEST);
}


/**
 * Lazily sync in the hypervisor debug state
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   fDr6        Whether to include DR6 or not.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(void) CPUMR0LoadHyperDebugState(PVMCPUCC pVCpu, bool fDr6)
{
    /*
     * Save the host state and disarm all host BPs.
     */
    cpumR0SaveHostDebugState(pVCpu);
    Assert(ASMGetDR7() == X86_DR7_INIT_VAL);

    /*
     * Make sure the hypervisor values are up to date.
     */
    CPUMRecalcHyperDRx(pVCpu, UINT8_MAX /* no loading, please */);

    /*
     * Activate the guest state DR0-3.
     * DR7 and DR6 (if fDr6 is true) are left to the caller.
     */
    ASMSetDR0(pVCpu->cpum.s.Hyper.dr[0]);
    ASMSetDR1(pVCpu->cpum.s.Hyper.dr[1]);
    ASMSetDR2(pVCpu->cpum.s.Hyper.dr[2]);
    ASMSetDR3(pVCpu->cpum.s.Hyper.dr[3]);
    if (fDr6)
        ASMSetDR6(X86_DR6_INIT_VAL);

    ASMAtomicOrU32(&pVCpu->cpum.s.fUseFlags, CPUM_USED_DEBUG_REGS_HYPER);
}

