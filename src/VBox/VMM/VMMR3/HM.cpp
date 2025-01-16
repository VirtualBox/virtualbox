/* $Id: HM.cpp 107891 2025-01-16 00:33:11Z knut.osmundsen@oracle.com $ */
/** @file
 * HM - Intel/AMD VM Hardware Support Manager.
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

/** @page pg_hm     HM - Hardware Assisted Virtualization Manager
 *
 * The HM manages guest execution using the VT-x and AMD-V CPU hardware
 * extensions.
 *
 * {summary of what HM does}
 *
 * Hardware assisted virtualization manager was originally abbreviated HWACCM,
 * however that was cumbersome to write and parse for such a central component,
 * so it was shortened to HM when refactoring the code in the 4.3 development
 * cycle.
 *
 * {add sections with more details}
 *
 * @sa @ref grp_hm
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_HM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/gcm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/hm_vmx.h>
#include <VBox/vmm/hm_svm.h>
#include "HMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/param.h>

#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/env.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def HMVMX_REPORT_FEAT
 * Reports VT-x feature to the release log.
 *
 * @param   a_uAllowed1       Mask of allowed-1 feature bits.
 * @param   a_uAllowed0       Mask of allowed-0 feature bits.
 * @param   a_StrDesc         The description string to report.
 * @param   a_Featflag        Mask of the feature to report.
 */
#define HMVMX_REPORT_FEAT(a_uAllowed1, a_uAllowed0, a_StrDesc, a_Featflag) \
    do { \
        if ((a_uAllowed1) & (a_Featflag)) \
        { \
            if ((a_uAllowed0) & (a_Featflag)) \
                LogRel(("HM:   " a_StrDesc " (must be set)\n")); \
            else \
                LogRel(("HM:   " a_StrDesc "\n")); \
        } \
        else \
            LogRel(("HM:   " a_StrDesc " (must be cleared)\n")); \
    } while (0)

/** @def HMVMX_REPORT_ALLOWED_FEAT
 * Reports an allowed VT-x feature to the release log.
 *
 * @param   a_uAllowed1     Mask of allowed-1 feature bits.
 * @param   a_StrDesc       The description string to report.
 * @param   a_FeatFlag      Mask of the feature to report.
 */
#define HMVMX_REPORT_ALLOWED_FEAT(a_uAllowed1, a_StrDesc, a_FeatFlag) \
    do { \
        if ((a_uAllowed1) & (a_FeatFlag)) \
            LogRel(("HM:   " a_StrDesc "\n")); \
        else \
            LogRel(("HM:   " a_StrDesc " not supported\n")); \
    } while (0)

/** @def HMVMX_REPORT_MSR_CAP
 * Reports MSR feature capability.
 *
 * @param   a_MsrCaps           Mask of MSR feature bits.
 * @param   a_StrDesc           The description string to report.
 * @param   a_fCap              Mask of the feature to report.
 */
#define HMVMX_REPORT_MSR_CAP(a_MsrCaps, a_StrDesc, a_fCap) \
    do { \
        if ((a_MsrCaps) & (a_fCap)) \
            LogRel(("HM:   " a_StrDesc "\n")); \
    } while (0)

/** @def HMVMX_LOGREL_FEAT
 * Dumps a feature flag from a bitmap of features to the release log.
 *
 * @param   a_fVal         The value of all the features.
 * @param   a_fMask        The specific bitmask of the feature.
 */
#define HMVMX_LOGREL_FEAT(a_fVal, a_fMask) \
    do { \
        if ((a_fVal) & (a_fMask)) \
            LogRel(("HM:   %s\n",  #a_fMask)); \
    } while (0)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  hmR3Save(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)  hmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
static DECLCALLBACK(void) hmR3InfoSvmNstGstVmcbCache(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) hmR3Info(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) hmR3InfoEventPending(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) hmR3InfoLbr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static int                hmR3InitFinalizeR3(PVM pVM);
static int                hmR3InitFinalizeR0(PVM pVM);
static int                hmR3InitFinalizeR0Intel(PVM pVM);
static int                hmR3InitFinalizeR0Amd(PVM pVM);
static int                hmR3TermCPU(PVM pVM);


#ifdef VBOX_WITH_STATISTICS
/**
 * Returns the name of the hardware exception.
 *
 * @returns The name of the hardware exception.
 * @param   uVector     The exception vector.
 */
static const char *hmR3GetXcptName(uint8_t uVector)
{
    switch (uVector)
    {
        case X86_XCPT_DE:             return "#DE";
        case X86_XCPT_DB:             return "#DB";
        case X86_XCPT_NMI:            return "#NMI";
        case X86_XCPT_BP:             return "#BP";
        case X86_XCPT_OF:             return "#OF";
        case X86_XCPT_BR:             return "#BR";
        case X86_XCPT_UD:             return "#UD";
        case X86_XCPT_NM:             return "#NM";
        case X86_XCPT_DF:             return "#DF";
        case X86_XCPT_CO_SEG_OVERRUN: return "#CO_SEG_OVERRUN";
        case X86_XCPT_TS:             return "#TS";
        case X86_XCPT_NP:             return "#NP";
        case X86_XCPT_SS:             return "#SS";
        case X86_XCPT_GP:             return "#GP";
        case X86_XCPT_PF:             return "#PF";
        case X86_XCPT_MF:             return "#MF";
        case X86_XCPT_AC:             return "#AC";
        case X86_XCPT_MC:             return "#MC";
        case X86_XCPT_XF:             return "#XF";
        case X86_XCPT_VE:             return "#VE";
        case X86_XCPT_CP:             return "#CP";
        case X86_XCPT_VC:             return "#VC";
        case X86_XCPT_SX:             return "#SX";
    }
    return "Reserved";
}
#endif /* VBOX_WITH_STATISTICS */


/**
 * Initializes the HM.
 *
 * This is the very first component to really do init after CFGM so that we can
 * establish the predominant execution engine for the VM prior to initializing
 * other modules.  It takes care of NEM initialization if needed (HM disabled or
 * not available in HW).
 *
 * If VT-x or AMD-V hardware isn't available, HM will try fall back on a native
 * hypervisor API via NEM, and then back on raw-mode if that isn't available
 * either.  The fallback to raw-mode will not happen if /HM/HMForced is set
 * (like for guest using SMP or 64-bit as well as for complicated guest like OS
 * X, OS/2 and others).
 *
 * Note that a lot of the set up work is done in ring-0 and thus postponed till
 * the ring-3 and ring-0 callback to HMR3InitCompleted.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 *
 * @remarks Be careful with what we call here, since most of the VMM components
 *          are uninitialized.
 */
VMMR3_INT_DECL(int) HMR3Init(PVM pVM)
{
    LogFlowFunc(("\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompileMemberAlignment(VM, hm.s, 32);
    AssertCompile(sizeof(pVM->hm.s) <= sizeof(pVM->hm.padding));

    /*
     * Register the saved state data unit.
     */
    int rc = SSMR3RegisterInternal(pVM, "HWACCM", 0, HM_SAVED_STATE_VERSION, sizeof(HM),
                                   NULL, NULL, NULL,
                                   NULL, hmR3Save, NULL,
                                   NULL, hmR3Load, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read configuration.
     */
    PCFGMNODE pCfgHm = CFGMR3GetChild(CFGMR3GetRoot(pVM), "HM/");

    /*
     * Validate the HM settings.
     */
    rc = CFGMR3ValidateConfig(pCfgHm, "/HM/",
                              "HMForced"  /* implied 'true' these days */
                              "|UseNEMInstead"
                              "|FallbackToNEM"
                              "|FallbackToIEM"
                              "|EnableNestedPaging"
                              "|EnableUX"
                              "|EnableLargePages"
                              "|EnableVPID"
                              "|IBPBOnVMExit"
                              "|IBPBOnVMEntry"
                              "|SpecCtrlByHost"
                              "|L1DFlushOnSched"
                              "|L1DFlushOnVMEntry"
                              "|MDSClearOnSched"
                              "|MDSClearOnVMEntry"
                              "|TPRPatchingEnabled"
                              "|64bitEnabled"
                              "|Exclusive"
                              "|MaxResumeLoops"
                              "|VmxPleGap"
                              "|VmxPleWindow"
                              "|VmxLbr"
                              "|UseVmxPreemptTimer"
                              "|SvmPauseFilter"
                              "|SvmPauseFilterThreshold"
                              "|SvmVirtVmsaveVmload"
                              "|SvmVGif"
                              "|LovelyMesaDrvWorkaround"
                              "|MissingOS2TlbFlushWorkaround"
                              "|AlwaysInterceptVmxMovDRx"
                              , "" /* pszValidNodes */, "HM" /* pszWho */, 0 /* uInstance */);
    if (RT_FAILURE(rc))
        return rc;

    /** @cfgm{/HM/HMForced, bool, false}
     * Forces hardware virtualization, no falling back on raw-mode. HM must be
     * enabled, i.e. /HMEnabled must be true. */
    bool const fHMForced = true;
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    AssertRelease(pVM->fHMEnabled);
#else
    AssertRelease(!pVM->fHMEnabled);
#endif

    /** @cfgm{/HM/UseNEMInstead, bool, true}
     * Don't use HM, use NEM instead. */
    bool fUseNEMInstead = false;
    rc = CFGMR3QueryBoolDef(pCfgHm, "UseNEMInstead", &fUseNEMInstead, false);
    AssertRCReturn(rc, rc);
    if (fUseNEMInstead && pVM->fHMEnabled)
    {
        LogRel(("HM: Setting fHMEnabled to false because fUseNEMInstead is set.\n"));
        pVM->fHMEnabled = false;
    }

    /** @cfgm{/HM/FallbackToNEM, bool, true}
     * Enables fallback on NEM. */
    bool fFallbackToNEM = true;
    rc = CFGMR3QueryBoolDef(pCfgHm, "FallbackToNEM", &fFallbackToNEM, true);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/FallbackToIEM, bool, false on AMD64 else true }
     * Enables fallback on NEM. */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    bool fFallbackToIEM = false;
#else
    bool fFallbackToIEM = true;
#endif
    rc = CFGMR3QueryBoolDef(pCfgHm, "FallbackToIEM", &fFallbackToIEM, fFallbackToIEM);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/EnableNestedPaging, bool, false}
     * Enables nested paging (aka extended page tables). */
    bool fAllowNestedPaging = false;
    rc = CFGMR3QueryBoolDef(pCfgHm, "EnableNestedPaging", &fAllowNestedPaging, false);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/EnableUX, bool, true}
     * Enables the VT-x unrestricted execution feature. */
    bool fAllowUnrestricted = true;
    rc = CFGMR3QueryBoolDef(pCfgHm, "EnableUX", &fAllowUnrestricted, true);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/EnableLargePages, bool, false}
     * Enables using large pages (2 MB) for guest memory, thus saving on (nested)
     * page table walking and maybe better TLB hit rate in some cases. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "EnableLargePages", &pVM->hm.s.fLargePages, false);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/EnableVPID, bool, false}
     * Enables the VT-x VPID feature. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "EnableVPID", &pVM->hm.s.vmx.fAllowVpid, false);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/TPRPatchingEnabled, bool, false}
     * Enables TPR patching for 32-bit windows guests with IO-APIC. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "TPRPatchingEnabled", &pVM->hm.s.fTprPatchingAllowed, false);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/64bitEnabled, bool, 32-bit:false, 64-bit:true}
     * Enables AMD64 cpu features.
     * On 32-bit hosts this isn't default and require host CPU support. 64-bit hosts
     * already have the support. */
#ifdef VBOX_WITH_64_BITS_GUESTS
    rc = CFGMR3QueryBoolDef(pCfgHm, "64bitEnabled", &pVM->hm.s.fAllow64BitGuestsCfg, HC_ARCH_BITS == 64);
    AssertLogRelRCReturn(rc, rc);
#else
    pVM->hm.s.fAllow64BitGuestsCfg = false;
#endif

    /** @cfgm{/HM/VmxPleGap, uint32_t, 0}
     * The pause-filter exiting gap in TSC ticks. When the number of ticks between
     * two successive PAUSE instructions exceeds VmxPleGap, the CPU considers the
     * latest PAUSE instruction to be start of a new PAUSE loop.
     */
    rc = CFGMR3QueryU32Def(pCfgHm, "VmxPleGap", &pVM->hm.s.vmx.cPleGapTicks, 0);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/VmxPleWindow, uint32_t, 0}
     * The pause-filter exiting window in TSC ticks. When the number of ticks
     * between the current PAUSE instruction and first PAUSE of a loop exceeds
     * VmxPleWindow, a VM-exit is triggered.
     *
     * Setting VmxPleGap and VmxPleGap to 0 disables pause-filter exiting.
     */
    rc = CFGMR3QueryU32Def(pCfgHm, "VmxPleWindow", &pVM->hm.s.vmx.cPleWindowTicks, 0);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/VmxLbr, bool, false}
     * Whether to enable LBR for the guest. This is disabled by default as it's only
     * useful while debugging and enabling it causes a noticeable performance hit. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "VmxLbr", &pVM->hm.s.vmx.fLbrCfg, false);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/SvmPauseFilterCount, uint16_t, 0}
     * A counter that is decrement each time a PAUSE instruction is executed by the
     * guest. When the counter is 0, a \#VMEXIT is triggered.
     *
     * Setting SvmPauseFilterCount to 0 disables pause-filter exiting.
     */
    rc = CFGMR3QueryU16Def(pCfgHm, "SvmPauseFilter", &pVM->hm.s.svm.cPauseFilter, 0);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/SvmPauseFilterThreshold, uint16_t, 0}
     * The pause filter threshold in ticks. When the elapsed time (in ticks) between
     * two successive PAUSE instructions exceeds SvmPauseFilterThreshold, the
     * PauseFilter count is reset to its initial value. However, if PAUSE is
     * executed PauseFilter times within PauseFilterThreshold ticks, a VM-exit will
     * be triggered.
     *
     * Requires SvmPauseFilterCount to be non-zero for pause-filter threshold to be
     * activated.
     */
    rc = CFGMR3QueryU16Def(pCfgHm, "SvmPauseFilterThreshold", &pVM->hm.s.svm.cPauseFilterThresholdTicks, 0);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/SvmVirtVmsaveVmload, bool, true}
     * Whether to make use of virtualized VMSAVE/VMLOAD feature of the CPU if it's
     * available. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "SvmVirtVmsaveVmload", &pVM->hm.s.svm.fVirtVmsaveVmload, true);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/SvmVGif, bool, true}
     * Whether to make use of Virtual GIF (Global Interrupt Flag) feature of the CPU
     * if it's available. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "SvmVGif", &pVM->hm.s.svm.fVGif, true);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/SvmLbrVirt, bool, false}
     * Whether to make use of the LBR virtualization feature of the CPU if it's
     * available. This is disabled by default as it's only useful while debugging
     * and enabling it causes a small hit to performance. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "SvmLbrVirt", &pVM->hm.s.svm.fLbrVirt, false);
    AssertRCReturn(rc, rc);

    /** @cfgm{/HM/Exclusive, bool}
     * Determines the init method for AMD-V and VT-x. If set to true, HM will do a
     * global init for each host CPU.  If false, we do local init each time we wish
     * to execute guest code.
     *
     * On Windows, default is false due to the higher risk of conflicts with other
     * hypervisors.
     *
     * On Mac OS X, this setting is ignored since the code does not handle local
     * init when it utilizes the OS provided VT-x function, SUPR0EnableVTx().
     */
#if defined(RT_OS_DARWIN)
    pVM->hm.s.fGlobalInit = true;
#else
    rc = CFGMR3QueryBoolDef(pCfgHm, "Exclusive", &pVM->hm.s.fGlobalInit,
# if defined(RT_OS_WINDOWS)
                            false
# else
                            true
# endif
                           );
    AssertLogRelRCReturn(rc, rc);
#endif

    /** @cfgm{/HM/MaxResumeLoops, uint32_t}
     * The number of times to resume guest execution before we forcibly return to
     * ring-3.  The return value of RTThreadPreemptIsPendingTrusty in ring-0
     * determines the default value. */
    rc = CFGMR3QueryU32Def(pCfgHm, "MaxResumeLoops", &pVM->hm.s.cMaxResumeLoopsCfg, 0 /* set by R0 later */);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/UseVmxPreemptTimer, bool}
     * Whether to make use of the VMX-preemption timer feature of the CPU if it's
     * available. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "UseVmxPreemptTimer", &pVM->hm.s.vmx.fUsePreemptTimerCfg, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/IBPBOnVMExit, bool}
     * Costly paranoia setting. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "IBPBOnVMExit", &pVM->hm.s.fIbpbOnVmExit, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/IBPBOnVMEntry, bool}
     * Costly paranoia setting. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "IBPBOnVMEntry", &pVM->hm.s.fIbpbOnVmEntry, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/L1DFlushOnSched, bool, true}
     * CVE-2018-3646 workaround, ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "L1DFlushOnSched", &pVM->hm.s.fL1dFlushOnSched, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/L1DFlushOnVMEntry, bool}
     * CVE-2018-3646 workaround, ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "L1DFlushOnVMEntry", &pVM->hm.s.fL1dFlushOnVmEntry, false);
    AssertLogRelRCReturn(rc, rc);

    /* Disable L1DFlushOnSched if L1DFlushOnVMEntry is enabled. */
    if (pVM->hm.s.fL1dFlushOnVmEntry)
        pVM->hm.s.fL1dFlushOnSched = false;

    /** @cfgm{/HM/SpecCtrlByHost, bool}
     * Another expensive paranoia setting. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "SpecCtrlByHost", &pVM->hm.s.fSpecCtrlByHost, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/MDSClearOnSched, bool, true}
     * CVE-2018-12126, CVE-2018-12130, CVE-2018-12127, CVE-2019-11091 workaround,
     * ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "MDSClearOnSched", &pVM->hm.s.fMdsClearOnSched, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/MDSClearOnVmEntry, bool, false}
     * CVE-2018-12126, CVE-2018-12130, CVE-2018-12127, CVE-2019-11091 workaround,
     * ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "MDSClearOnVmEntry", &pVM->hm.s.fMdsClearOnVmEntry, false);
    AssertLogRelRCReturn(rc, rc);

    /* Disable MDSClearOnSched if MDSClearOnVmEntry is enabled. */
    if (pVM->hm.s.fMdsClearOnVmEntry)
        pVM->hm.s.fMdsClearOnSched = false;

    /** @cfgm{/HM/LovelyMesaDrvWorkaround,bool}
     * Workaround for mesa vmsvga 3d driver making incorrect assumptions about
     * the hypervisor it is running under. */
    bool fMesaWorkaround;
    rc = CFGMR3QueryBoolDef(pCfgHm, "LovelyMesaDrvWorkaround", &fMesaWorkaround, false);
    AssertLogRelRCReturn(rc, rc);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->hm.s.fTrapXcptGpForLovelyMesaDrv = fMesaWorkaround;
    }

    /** @cfgm{/HM/MissingOS2TlbFlushWorkaround,bool}
     * Workaround OS/2 not flushing the TLB after page directory and page table
     * modifications when returning to protected mode from a real mode call
     * (TESTCFG.SYS typically crashes).  See ticketref:20625 for details. */
    rc = CFGMR3QueryBoolDef(pCfgHm, "MissingOS2TlbFlushWorkaround", &pVM->hm.s.fMissingOS2TlbFlushWorkaround, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/HM/AlwaysInterceptVmxMovDRx,int8_t,0}
     * Whether to always intercept MOV DRx when using VMX.
     * The value is a tristate: 1 for always intercepting, -1 for lazy intercept,
     * and 0 for default.  The default means that it's always intercepted when the
     * host DR6 contains bits not known to the guest.
     *
     * With the introduction of transactional synchronization extensions new
     * instructions, aka TSX-NI or RTM, bit 16 in DR6 is cleared to indicate that a
     * \#DB was related to a transaction.  The bit is also cleared when writing zero
     * to it, so guest lazily resetting DR6 by writing 0 to it, ends up with an
     * unexpected value.  Similiarly, bit 11 in DR7 is used to enabled RTM
     * debugging support and therefore writable by the guest.
     *
     * Out of caution/paranoia, we will by default intercept DRx moves when setting
     * DR6 to zero (on the host) doesn't result in 0xffff0ff0 (X86_DR6_RA1_MASK).
     * Note that it seems DR6.RTM remains writable even after the microcode updates
     * disabling TSX. */
    rc = CFGMR3QueryS8Def(pCfgHm, "AlwaysInterceptVmxMovDRx", &pVM->hm.s.vmx.fAlwaysInterceptMovDRxCfg, 0);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Check if VT-x or AMD-v support according to the users wishes.
     */
    /** @todo SUPR3QueryVTCaps won't catch VERR_VMX_IN_VMX_ROOT_MODE or
     *        VERR_SVM_IN_USE. */
    if (pVM->fHMEnabled)
    {
        uint32_t fCaps;
        rc = SUPR3QueryVTCaps(&fCaps);
        if (RT_SUCCESS(rc))
        {
            if (fCaps & SUPVTCAPS_AMD_V)
            {
                pVM->hm.s.svm.fSupported = true;
                LogRel(("HM: HMR3Init: AMD-V%s\n", fCaps & SUPVTCAPS_NESTED_PAGING ? " w/ nested paging" : ""));
                VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_HW_VIRT);
            }
            else if (fCaps & SUPVTCAPS_VT_X)
            {
                const char *pszWhy;
                rc = SUPR3QueryVTxSupported(&pszWhy);
                if (RT_SUCCESS(rc))
                {
                    pVM->hm.s.vmx.fSupported = true;
                    LogRel(("HM: HMR3Init: VT-x%s%s%s\n",
                            fCaps & SUPVTCAPS_NESTED_PAGING ? " w/ nested paging" : "",
                            fCaps & SUPVTCAPS_VTX_UNRESTRICTED_GUEST ? " and unrestricted guest execution" : "",
                            (fCaps & (SUPVTCAPS_NESTED_PAGING | SUPVTCAPS_VTX_UNRESTRICTED_GUEST)) ? " hw support" : ""));
                    VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_HW_VIRT);
                }
                else
                {
                    /*
                     * Before failing, try fallback to NEM if we're allowed to do that.
                     */
                    pVM->fHMEnabled = false;
                    Assert(pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NOT_SET);
                    if (fFallbackToNEM)
                    {
                        LogRel(("HM: HMR3Init: Attempting fall back to NEM: The host kernel does not support VT-x - %s\n", pszWhy));
                        int rc2 = NEMR3Init(pVM, true /*fFallback*/, fHMForced);

                        ASMCompilerBarrier(); /* NEMR3Init may have changed bMainExecutionEngine. */
                        if (   RT_SUCCESS(rc2)
                            && pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NOT_SET)
                            rc = VINF_SUCCESS;
                    }
                    if (RT_FAILURE(rc))
                        return VMSetError(pVM, rc, RT_SRC_POS, "The host kernel does not support VT-x: %s\n", pszWhy);
                }
            }
            else
                AssertLogRelMsgFailedReturn(("SUPR3QueryVTCaps didn't return either AMD-V or VT-x flag set (%#x)!\n", fCaps),
                                            VERR_INTERNAL_ERROR_5);

            /*
             * Disable nested paging and unrestricted guest execution now if they're
             * configured so that CPUM can make decisions based on our configuration.
             */
            if (   fAllowNestedPaging
                && (fCaps & SUPVTCAPS_NESTED_PAGING))
            {
                pVM->hm.s.fNestedPagingCfg = true;
                if (fCaps & SUPVTCAPS_VT_X)
                {
                    if (   fAllowUnrestricted
                        && (fCaps & SUPVTCAPS_VTX_UNRESTRICTED_GUEST))
                        pVM->hm.s.vmx.fUnrestrictedGuestCfg = true;
                    else
                        Assert(!pVM->hm.s.vmx.fUnrestrictedGuestCfg);
                }
            }
            else
                Assert(!pVM->hm.s.fNestedPagingCfg);
        }
        else
        {
            const char *pszMsg;
            switch (rc)
            {
                case VERR_UNSUPPORTED_CPU:          pszMsg = "Unknown CPU, VT-x or AMD-v features cannot be ascertained"; break;
                case VERR_VMX_NO_VMX:               pszMsg = "VT-x is not available";                                     break;
                case VERR_VMX_MSR_VMX_DISABLED:     pszMsg = "VT-x is disabled in the BIOS";                              break;
                case VERR_VMX_MSR_ALL_VMX_DISABLED: pszMsg = "VT-x is disabled in the BIOS for all CPU modes";            break;
                case VERR_VMX_MSR_LOCKING_FAILED:   pszMsg = "Failed to enable and lock VT-x features";                   break;
                case VERR_SVM_NO_SVM:               pszMsg = "AMD-V is not available";                                    break;
                case VERR_SVM_DISABLED:             pszMsg = "AMD-V is disabled in the BIOS (or by the host OS)";         break;
                case VERR_SUP_DRIVERLESS:           pszMsg = "Driverless mode";                                           break;
                default:
                    return VMSetError(pVM, rc, RT_SRC_POS, "SUPR3QueryVTCaps failed with %Rrc", rc);
            }

            /*
             * Before failing, try fallback to NEM if we're allowed to do that.
             */
            pVM->fHMEnabled = false;
            if (fFallbackToNEM)
            {
                LogRel(("HM: HMR3Init: Attempting fall back to NEM: %s\n", pszMsg));
                int rc2 = NEMR3Init(pVM, true /*fFallback*/, fHMForced);
                ASMCompilerBarrier(); /* NEMR3Init may have changed bMainExecutionEngine. */
                if (   RT_SUCCESS(rc2)
                    && pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NOT_SET)
                {
                    rc = VINF_SUCCESS;

                    /* For some reason, HM is in charge or large pages. Make sure to enable them: */
                    PGMSetLargePageUsage(pVM, pVM->hm.s.fLargePages);
                }
            }

            /*
             * Then try fall back on IEM if NEM isn't available and we're allowed to.
             */
            if (RT_FAILURE(rc))
            {
                if (   fFallbackToIEM
                    && (!fFallbackToNEM || rc == VERR_NEM_NOT_AVAILABLE || rc == VERR_SUP_DRIVERLESS))
                {
                    LogRel(("HM: HMR3Init: Falling back on IEM: %s\n", !fFallbackToNEM ? pszMsg : "NEM not available"));
                    VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_IEM);
#ifdef VBOX_WITH_PGM_NEM_MODE
                    PGMR3EnableNemMode(pVM);
#endif
                }
                else
                    return VM_SET_ERROR(pVM, rc, pszMsg);
            }
        }
    }
    else
    {
        /*
         * Disabled HM mean raw-mode, unless NEM is supposed to be used.
         */
        rc = VERR_NEM_NOT_AVAILABLE;
        if (fUseNEMInstead)
        {
            rc = NEMR3Init(pVM, false /*fFallback*/, true);
            ASMCompilerBarrier(); /* NEMR3Init may have changed bMainExecutionEngine. */
            if (RT_SUCCESS(rc))
            {
                /* For some reason, HM is in charge or large pages. Make sure to enable them: */
                PGMSetLargePageUsage(pVM, pVM->hm.s.fLargePages);
            }
            else if (!fFallbackToIEM || rc != VERR_NEM_NOT_AVAILABLE)
                return rc;
        }

        if (fFallbackToIEM && rc == VERR_NEM_NOT_AVAILABLE)
        {
            LogRel(("HM: HMR3Init: Falling back on IEM%s\n", fUseNEMInstead ? ": NEM not available" : ""));
            VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_IEM);
#ifdef VBOX_WITH_PGM_NEM_MODE
            PGMR3EnableNemMode(pVM);
#endif
        }

        if (   pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NOT_SET
            || pVM->bMainExecutionEngine == VM_EXEC_ENGINE_HW_VIRT /* paranoia */)
            return VM_SET_ERROR(pVM, rc, "Misconfigured VM: No guest execution engine available!");
    }

    if (pVM->fHMEnabled)
    {
        /*
         * Register info handlers now that HM is used for sure.
         */
        rc = DBGFR3InfoRegisterInternalEx(pVM, "hm", "Dumps HM info.", hmR3Info, DBGFINFO_FLAGS_ALL_EMTS);
        AssertRCReturn(rc, rc);

        rc = DBGFR3InfoRegisterInternalEx(pVM, "hmeventpending", "Dumps the pending HM event.", hmR3InfoEventPending,
                                          DBGFINFO_FLAGS_ALL_EMTS);
        AssertRCReturn(rc, rc);

        rc = DBGFR3InfoRegisterInternalEx(pVM, "svmvmcbcache", "Dumps the HM SVM nested-guest VMCB cache.",
                                          hmR3InfoSvmNstGstVmcbCache, DBGFINFO_FLAGS_ALL_EMTS);
        AssertRCReturn(rc, rc);

        rc = DBGFR3InfoRegisterInternalEx(pVM, "lbr", "Dumps the HM LBR info.", hmR3InfoLbr, DBGFINFO_FLAGS_ALL_EMTS);
        AssertRCReturn(rc, rc);
    }

    Assert(pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NOT_SET);
    return VINF_SUCCESS;
}


/**
 * Initializes HM components after ring-3 phase has been fully initialized.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int hmR3InitFinalizeR3(PVM pVM)
{
    LogFlowFunc(("\n"));

    if (!HMIsEnabled(pVM))
        return VINF_SUCCESS;

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->hm.s.fActive = false;
        pVCpu->hm.s.fGIMTrapXcptUD = GIMShouldTrapXcptUD(pVCpu);     /* Is safe to call now since GIMR3Init() has completed. */
        pVCpu->hm.s.fGCMTrapXcptDE = GCMIsInterceptingXcptDE(pVCpu); /* Is safe to call now since GCMR3Init() has completed. */
    }

#if defined(RT_ARCH_AMD64) ||defined(RT_ARCH_X86) /* This section is duplicated in nemR3InitFinalizeSpecCtrl. */
    /*
     * Check if L1D flush is needed/possible.
     */
    if (   !g_CpumHostFeatures.s.fFlushCmd
        || g_CpumHostFeatures.s.enmMicroarch <  kCpumMicroarch_Intel_Core7_Nehalem
        || g_CpumHostFeatures.s.enmMicroarch >= kCpumMicroarch_Intel_Core7_End
        || g_CpumHostFeatures.s.fArchVmmNeedNotFlushL1d
        || g_CpumHostFeatures.s.fArchRdclNo)
        pVM->hm.s.fL1dFlushOnSched = pVM->hm.s.fL1dFlushOnVmEntry = false;

    /*
     * Check if MDS flush is needed/possible.
     * On atoms and knight family CPUs, we will only allow clearing on scheduling.
     */
    if (   !g_CpumHostFeatures.s.fMdsClear
        || g_CpumHostFeatures.s.fArchMdsNo)
        pVM->hm.s.fMdsClearOnSched = pVM->hm.s.fMdsClearOnVmEntry = false;
    else if (   (   g_CpumHostFeatures.s.enmMicroarch >=  kCpumMicroarch_Intel_Atom_Airmount
                 && g_CpumHostFeatures.s.enmMicroarch <   kCpumMicroarch_Intel_Atom_End)
             || (   g_CpumHostFeatures.s.enmMicroarch >=  kCpumMicroarch_Intel_Phi_KnightsLanding
                 && g_CpumHostFeatures.s.enmMicroarch <   kCpumMicroarch_Intel_Phi_End))
    {
        if (!pVM->hm.s.fMdsClearOnSched)
             pVM->hm.s.fMdsClearOnSched = pVM->hm.s.fMdsClearOnVmEntry;
        pVM->hm.s.fMdsClearOnVmEntry = false;
    }
    else if (   g_CpumHostFeatures.s.enmMicroarch <  kCpumMicroarch_Intel_Core7_Nehalem
             || g_CpumHostFeatures.s.enmMicroarch >= kCpumMicroarch_Intel_Core7_End)
        pVM->hm.s.fMdsClearOnSched = pVM->hm.s.fMdsClearOnVmEntry = false;
#endif

    /*
     * Statistics.
     */
#ifdef VBOX_WITH_STATISTICS
    STAM_REG(pVM, &pVM->hm.s.StatTprPatchSuccess,      STAMTYPE_COUNTER, "/HM/TPR/Patch/Success",      STAMUNIT_OCCURENCES, "Number of times an instruction was successfully patched.");
    STAM_REG(pVM, &pVM->hm.s.StatTprPatchFailure,      STAMTYPE_COUNTER, "/HM/TPR/Patch/Failed",       STAMUNIT_OCCURENCES, "Number of unsuccessful patch attempts.");
    STAM_REG(pVM, &pVM->hm.s.StatTprReplaceSuccessCr8, STAMTYPE_COUNTER, "/HM/TPR/Replace/SuccessCR8", STAMUNIT_OCCURENCES, "Number of instruction replacements by MOV CR8.");
    STAM_REG(pVM, &pVM->hm.s.StatTprReplaceSuccessVmc, STAMTYPE_COUNTER, "/HM/TPR/Replace/SuccessVMC", STAMUNIT_OCCURENCES, "Number of instruction replacements by VMMCALL.");
    STAM_REG(pVM, &pVM->hm.s.StatTprReplaceFailure,    STAMTYPE_COUNTER, "/HM/TPR/Replace/Failed",     STAMUNIT_OCCURENCES, "Number of unsuccessful replace attempts.");
#endif

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    bool const fCpuSupportsVmx = ASMIsIntelCpu() || ASMIsViaCentaurCpu() || ASMIsShanghaiCpu();
#else
    bool const fCpuSupportsVmx = false;
#endif
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu  = pVM->apCpusR3[idCpu];
        PHMCPU pHmCpu = &pVCpu->hm.s;
        int    rc;

# define HM_REG_STAT(a_pVar, a_enmType, s_enmVisibility, a_enmUnit, a_szNmFmt, a_szDesc) do { \
                rc = STAMR3RegisterF(pVM, a_pVar, a_enmType, s_enmVisibility, a_enmUnit, a_szDesc, a_szNmFmt, idCpu); \
                AssertRC(rc); \
            } while (0)
# define HM_REG_PROFILE(a_pVar, a_szNmFmt, a_szDesc) \
            HM_REG_STAT(a_pVar, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_TICKS_PER_CALL, a_szNmFmt, a_szDesc)

#ifdef VBOX_WITH_STATISTICS
        HM_REG_PROFILE(&pHmCpu->StatPoke,                   "/PROF/CPU%u/HM/Poke", "Profiling of RTMpPokeCpu.");
        HM_REG_PROFILE(&pHmCpu->StatSpinPoke,               "/PROF/CPU%u/HM/PokeWait", "Profiling of poke wait.");
        HM_REG_PROFILE(&pHmCpu->StatSpinPokeFailed,         "/PROF/CPU%u/HM/PokeWaitFailed", "Profiling of poke wait when RTMpPokeCpu fails.");
        HM_REG_PROFILE(&pHmCpu->StatEntry,                  "/PROF/CPU%u/HM/Entry", "Profiling of entry until entering GC.");
        HM_REG_PROFILE(&pHmCpu->StatPreExit,                "/PROF/CPU%u/HM/SwitchFromGC_1", "Profiling of pre-exit processing after returning from GC.");
        HM_REG_PROFILE(&pHmCpu->StatExitHandling,           "/PROF/CPU%u/HM/SwitchFromGC_2", "Profiling of exit handling (longjmps not included!)");
        HM_REG_PROFILE(&pHmCpu->StatExitIO,                 "/PROF/CPU%u/HM/SwitchFromGC_2/IO", "I/O.");
        HM_REG_PROFILE(&pHmCpu->StatExitMovCRx,             "/PROF/CPU%u/HM/SwitchFromGC_2/MovCRx", "MOV CRx.");
        HM_REG_PROFILE(&pHmCpu->StatExitXcptNmi,            "/PROF/CPU%u/HM/SwitchFromGC_2/XcptNmi", "Exceptions, NMIs.");
        HM_REG_PROFILE(&pHmCpu->StatExitVmentry,            "/PROF/CPU%u/HM/SwitchFromGC_2/Vmentry", "VMLAUNCH/VMRESUME on Intel or VMRUN on AMD.");
        HM_REG_PROFILE(&pHmCpu->StatImportGuestState,       "/PROF/CPU%u/HM/ImportGuestState",  "Profiling of importing guest state from hardware after VM-exit.");
        HM_REG_PROFILE(&pHmCpu->StatExportGuestState,       "/PROF/CPU%u/HM/ExportGuestState",  "Profiling of exporting guest state to hardware before VM-entry.");
        HM_REG_PROFILE(&pHmCpu->StatLoadGuestFpuState,      "/PROF/CPU%u/HM/LoadGuestFpuState", "Profiling of CPUMR0LoadGuestFPU.");
        HM_REG_PROFILE(&pHmCpu->StatInGC,                   "/PROF/CPU%u/HM/InGC", "Profiling of execution of guest-code in hardware.");
# ifdef HM_PROFILE_EXIT_DISPATCH
        HM_REG_STAT(&pHmCpu->StatExitDispatch, STAMTYPE_PROFILE_ADV, STAMVISIBILITY_USED, STAMUNIT_TICKS_PER_CALL,
                    "/PROF/CPU%u/HM/ExitDispatch", "Profiling the dispatching of exit handlers.");
# endif
#endif
# define HM_REG_COUNTER(a, b, desc) HM_REG_STAT(a, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, b, desc)

        HM_REG_COUNTER(&pHmCpu->StatImportGuestStateFallback, "/HM/CPU%u/ImportGuestStateFallback", "Times vmxHCImportGuestState took the fallback code path.");
        HM_REG_COUNTER(&pHmCpu->StatReadToTransientFallback,  "/HM/CPU%u/ReadToTransientFallback",  "Times vmxHCReadToTransient took the fallback code path.");
#ifdef VBOX_WITH_STATISTICS
        HM_REG_COUNTER(&pHmCpu->StatExitAll,                "/HM/CPU%u/Exit/All", "Total exits (excludes nested-guest and debug loops exits).");
        HM_REG_COUNTER(&pHmCpu->StatDebugExitAll,           "/HM/CPU%u/Exit/DebugAll", "Total debug-loop exits.");
        HM_REG_COUNTER(&pHmCpu->StatNestedExitAll,          "/HM/CPU%u/ExitNestedGuest/All", "Total nested-guest exits.");
        HM_REG_COUNTER(&pHmCpu->StatExitShadowNM,           "/HM/CPU%u/Exit/Trap/Shw/#NM", "Shadow #NM (device not available, no math co-processor) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestNM,            "/HM/CPU%u/Exit/Trap/Gst/#NM", "Guest #NM (device not available, no math co-processor) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitShadowPF,           "/HM/CPU%u/Exit/Trap/Shw/#PF", "Shadow #PF (page fault) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitShadowPFEM,         "/HM/CPU%u/Exit/Trap/Shw/#PF-EM", "#PF (page fault) exception going back to ring-3 for emulating the instruction.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestPF,            "/HM/CPU%u/Exit/Trap/Gst/#PF", "Guest #PF (page fault) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestUD,            "/HM/CPU%u/Exit/Trap/Gst/#UD", "Guest #UD (undefined opcode) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestSS,            "/HM/CPU%u/Exit/Trap/Gst/#SS", "Guest #SS (stack-segment fault) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestNP,            "/HM/CPU%u/Exit/Trap/Gst/#NP", "Guest #NP (segment not present) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestTS,            "/HM/CPU%u/Exit/Trap/Gst/#TS", "Guest #TS (task switch) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestOF,            "/HM/CPU%u/Exit/Trap/Gst/#OF", "Guest #OF (overflow) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestGP,            "/HM/CPU%u/Exit/Trap/Gst/#GP", "Guest #GP (general protection) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestDE,            "/HM/CPU%u/Exit/Trap/Gst/#DE", "Guest #DE (divide error) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestDF,            "/HM/CPU%u/Exit/Trap/Gst/#DF", "Guest #DF (double fault) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestBR,            "/HM/CPU%u/Exit/Trap/Gst/#BR", "Guest #BR (boundary range exceeded) exception.");
#endif
        HM_REG_COUNTER(&pHmCpu->StatExitGuestAC,            "/HM/CPU%u/Exit/Trap/Gst/#AC", "Guest #AC (alignment check) exception.");
        if (fCpuSupportsVmx)
            HM_REG_COUNTER(&pHmCpu->StatExitGuestACSplitLock, "/HM/CPU%u/Exit/Trap/Gst/#AC-split-lock", "Guest triggered #AC due to split-lock being enabled on the host (interpreted).");
#ifdef VBOX_WITH_STATISTICS
        HM_REG_COUNTER(&pHmCpu->StatExitGuestDB,            "/HM/CPU%u/Exit/Trap/Gst/#DB", "Guest #DB (debug) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestMF,            "/HM/CPU%u/Exit/Trap/Gst/#MF", "Guest #MF (x87 FPU error, math fault) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestBP,            "/HM/CPU%u/Exit/Trap/Gst/#BP", "Guest #BP (breakpoint) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestXF,            "/HM/CPU%u/Exit/Trap/Gst/#XF", "Guest #XF (extended math fault, SIMD FPU) exception.");
        HM_REG_COUNTER(&pHmCpu->StatExitGuestXcpUnk,        "/HM/CPU%u/Exit/Trap/Gst/Other", "Other guest exceptions.");
        HM_REG_COUNTER(&pHmCpu->StatExitRdmsr,              "/HM/CPU%u/Exit/Instr/Rdmsr", "MSR read.");
        HM_REG_COUNTER(&pHmCpu->StatExitWrmsr,              "/HM/CPU%u/Exit/Instr/Wrmsr", "MSR write.");
        HM_REG_COUNTER(&pHmCpu->StatExitDRxWrite,           "/HM/CPU%u/Exit/Instr/DR-Write", "Debug register write.");
        HM_REG_COUNTER(&pHmCpu->StatExitDRxRead,            "/HM/CPU%u/Exit/Instr/DR-Read", "Debug register read.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR0Read,            "/HM/CPU%u/Exit/Instr/CR-Read/CR0", "CR0 read.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR2Read,            "/HM/CPU%u/Exit/Instr/CR-Read/CR2", "CR2 read.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR3Read,            "/HM/CPU%u/Exit/Instr/CR-Read/CR3", "CR3 read.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR4Read,            "/HM/CPU%u/Exit/Instr/CR-Read/CR4", "CR4 read.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR8Read,            "/HM/CPU%u/Exit/Instr/CR-Read/CR8", "CR8 read.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR0Write,           "/HM/CPU%u/Exit/Instr/CR-Write/CR0", "CR0 write.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR2Write,           "/HM/CPU%u/Exit/Instr/CR-Write/CR2", "CR2 write.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR3Write,           "/HM/CPU%u/Exit/Instr/CR-Write/CR3", "CR3 write.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR4Write,           "/HM/CPU%u/Exit/Instr/CR-Write/CR4", "CR4 write.");
        HM_REG_COUNTER(&pHmCpu->StatExitCR8Write,           "/HM/CPU%u/Exit/Instr/CR-Write/CR8", "CR8 write.");
        HM_REG_COUNTER(&pHmCpu->StatExitClts,               "/HM/CPU%u/Exit/Instr/CLTS", "CLTS instruction.");
        HM_REG_COUNTER(&pHmCpu->StatExitLmsw,               "/HM/CPU%u/Exit/Instr/LMSW", "LMSW instruction.");
        HM_REG_COUNTER(&pHmCpu->StatExitXdtrAccess,         "/HM/CPU%u/Exit/Instr/XdtrAccess", "GDTR, IDTR, LDTR access.");
        HM_REG_COUNTER(&pHmCpu->StatExitIOWrite,            "/HM/CPU%u/Exit/Instr/IO/Write", "I/O write.");
        HM_REG_COUNTER(&pHmCpu->StatExitIORead,             "/HM/CPU%u/Exit/Instr/IO/Read", "I/O read.");
        HM_REG_COUNTER(&pHmCpu->StatExitIOStringWrite,      "/HM/CPU%u/Exit/Instr/IO/WriteString", "String I/O write.");
        HM_REG_COUNTER(&pHmCpu->StatExitIOStringRead,       "/HM/CPU%u/Exit/Instr/IO/ReadString", "String I/O read.");
        HM_REG_COUNTER(&pHmCpu->StatExitIntWindow,          "/HM/CPU%u/Exit/IntWindow", "Interrupt-window exit. Guest is ready to receive interrupts.");
        HM_REG_COUNTER(&pHmCpu->StatExitExtInt,             "/HM/CPU%u/Exit/ExtInt", "Physical maskable interrupt (host).");
#endif
        HM_REG_COUNTER(&pHmCpu->StatExitHostNmiInGC,        "/HM/CPU%u/Exit/HostNmiInGC", "Host NMI received while in guest context.");
        HM_REG_COUNTER(&pHmCpu->StatExitHostNmiInGCIpi,     "/HM/CPU%u/Exit/HostNmiInGCIpi", "Host NMI received while in guest context dispatched using IPIs.");
        HM_REG_COUNTER(&pHmCpu->StatExitPreemptTimer,       "/HM/CPU%u/Exit/PreemptTimer", "VMX-preemption timer expired.");
#ifdef VBOX_WITH_STATISTICS
        HM_REG_COUNTER(&pHmCpu->StatExitTprBelowThreshold,  "/HM/CPU%u/Exit/TprBelowThreshold", "TPR lowered below threshold by the guest.");
        HM_REG_COUNTER(&pHmCpu->StatExitTaskSwitch,         "/HM/CPU%u/Exit/TaskSwitch", "Task switch caused through task gate in IDT.");
        HM_REG_COUNTER(&pHmCpu->StatExitApicAccess,         "/HM/CPU%u/Exit/ApicAccess", "APIC access. Guest attempted to access memory at a physical address on the APIC-access page.");

        HM_REG_COUNTER(&pHmCpu->StatSwitchTprMaskedIrq,     "/HM/CPU%u/Switch/TprMaskedIrq", "PDMGetInterrupt() signals TPR masks pending Irq.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchGuestIrq,         "/HM/CPU%u/Switch/IrqPending", "PDMGetInterrupt() cleared behind our back!?!.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchPendingHostIrq,   "/HM/CPU%u/Switch/PendingHostIrq", "Exit to ring-3 due to pending host interrupt before executing guest code.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchHmToR3FF,         "/HM/CPU%u/Switch/HmToR3FF", "Exit to ring-3 due to pending timers, EMT rendezvous, critical section etc.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchVmReq,            "/HM/CPU%u/Switch/VmReq", "Exit to ring-3 due to pending VM requests.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchPgmPoolFlush,     "/HM/CPU%u/Switch/PgmPoolFlush", "Exit to ring-3 due to pending PGM pool flush.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchDma,              "/HM/CPU%u/Switch/PendingDma", "Exit to ring-3 due to pending DMA requests.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchExitToR3,         "/HM/CPU%u/Switch/ExitToR3", "Exit to ring-3 (total).");
        HM_REG_COUNTER(&pHmCpu->StatSwitchLongJmpToR3,      "/HM/CPU%u/Switch/LongJmpToR3", "Longjump to ring-3.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchMaxResumeLoops,   "/HM/CPU%u/Switch/MaxResumeLoops", "Maximum VMRESUME inner-loop counter reached.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchHltToR3,          "/HM/CPU%u/Switch/HltToR3", "HLT causing us to go to ring-3.");
        HM_REG_COUNTER(&pHmCpu->StatSwitchApicAccessToR3,   "/HM/CPU%u/Switch/ApicAccessToR3", "APIC access causing us to go to ring-3.");
#endif
        HM_REG_COUNTER(&pHmCpu->StatSwitchPreempt,          "/HM/CPU%u/Switch/Preempting", "EMT has been preempted while in HM context.");
#ifdef VBOX_WITH_STATISTICS
        HM_REG_COUNTER(&pHmCpu->StatSwitchNstGstVmexit,     "/HM/CPU%u/Switch/NstGstVmexit", "Nested-guest VM-exit occurred.");

        HM_REG_COUNTER(&pHmCpu->StatInjectInterrupt,        "/HM/CPU%u/EventInject/Interrupt", "Injected an external interrupt into the guest.");
        HM_REG_COUNTER(&pHmCpu->StatInjectXcpt,             "/HM/CPU%u/EventInject/Trap", "Injected an exception into the guest.");
        HM_REG_COUNTER(&pHmCpu->StatInjectReflect,          "/HM/CPU%u/EventInject/Reflect", "Reflecting an exception caused due to event injection.");
        HM_REG_COUNTER(&pHmCpu->StatInjectConvertDF,        "/HM/CPU%u/EventInject/ReflectDF", "Injected a converted #DF caused due to event injection.");
        HM_REG_COUNTER(&pHmCpu->StatInjectInterpret,        "/HM/CPU%u/EventInject/Interpret", "Falling back to interpreter for handling exception caused due to event injection.");
        HM_REG_COUNTER(&pHmCpu->StatInjectReflectNPF,       "/HM/CPU%u/EventInject/ReflectNPF", "Reflecting event that caused an EPT violation / nested #PF.");

        HM_REG_COUNTER(&pHmCpu->StatFlushPage,              "/HM/CPU%u/Flush/Page", "Invalidating a guest page on all guest CPUs.");
        HM_REG_COUNTER(&pHmCpu->StatFlushPageManual,        "/HM/CPU%u/Flush/Page/Virt", "Invalidating a guest page using guest-virtual address.");
        HM_REG_COUNTER(&pHmCpu->StatFlushPhysPageManual,    "/HM/CPU%u/Flush/Page/Phys", "Invalidating a guest page using guest-physical address.");
        HM_REG_COUNTER(&pHmCpu->StatFlushTlb,               "/HM/CPU%u/Flush/TLB", "Forcing a full guest-TLB flush (ring-0).");
        HM_REG_COUNTER(&pHmCpu->StatFlushTlbManual,         "/HM/CPU%u/Flush/TLB/Manual", "Request a full guest-TLB flush.");
        HM_REG_COUNTER(&pHmCpu->StatFlushTlbNstGst,         "/HM/CPU%u/Flush/TLB/NestedGuest", "Request a nested-guest-TLB flush.");
        HM_REG_COUNTER(&pHmCpu->StatFlushTlbWorldSwitch,    "/HM/CPU%u/Flush/TLB/CpuSwitch", "Forcing a full guest-TLB flush due to host-CPU reschedule or ASID-limit hit by another guest-VCPU.");
        HM_REG_COUNTER(&pHmCpu->StatNoFlushTlbWorldSwitch,  "/HM/CPU%u/Flush/TLB/Skipped", "No TLB flushing required.");
        HM_REG_COUNTER(&pHmCpu->StatFlushEntire,            "/HM/CPU%u/Flush/TLB/Entire", "Flush the entire TLB (host + guest).");
        HM_REG_COUNTER(&pHmCpu->StatFlushAsid,              "/HM/CPU%u/Flush/TLB/ASID", "Flushed guest-TLB entries for the current VPID.");
        HM_REG_COUNTER(&pHmCpu->StatFlushNestedPaging,      "/HM/CPU%u/Flush/TLB/NestedPaging", "Flushed guest-TLB entries for the current EPT.");
        HM_REG_COUNTER(&pHmCpu->StatFlushTlbInvlpgVirt,     "/HM/CPU%u/Flush/TLB/InvlpgVirt", "Invalidated a guest-TLB entry for a guest-virtual address.");
        HM_REG_COUNTER(&pHmCpu->StatFlushTlbInvlpgPhys,     "/HM/CPU%u/Flush/TLB/InvlpgPhys", "Currently not possible, flushes entire guest-TLB.");
        HM_REG_COUNTER(&pHmCpu->StatTlbShootdown,           "/HM/CPU%u/Flush/Shootdown/Page", "Inter-VCPU request to flush queued guest page.");
        HM_REG_COUNTER(&pHmCpu->StatTlbShootdownFlush,      "/HM/CPU%u/Flush/Shootdown/TLB", "Inter-VCPU request to flush entire guest-TLB.");

        HM_REG_COUNTER(&pHmCpu->StatTscParavirt,            "/HM/CPU%u/TSC/Paravirt", "Paravirtualized TSC in effect.");
        HM_REG_COUNTER(&pHmCpu->StatTscOffset,              "/HM/CPU%u/TSC/Offset", "TSC offsetting is in effect.");
        HM_REG_COUNTER(&pHmCpu->StatTscIntercept,           "/HM/CPU%u/TSC/Intercept", "Intercept TSC accesses.");

        HM_REG_COUNTER(&pHmCpu->StatDRxArmed,               "/HM/CPU%u/Debug/Armed", "Loaded guest-debug state while loading guest-state.");
        HM_REG_COUNTER(&pHmCpu->StatDRxContextSwitch,       "/HM/CPU%u/Debug/ContextSwitch", "Loaded guest-debug state on MOV DRx.");
        HM_REG_COUNTER(&pHmCpu->StatDRxIoCheck,             "/HM/CPU%u/Debug/IOCheck", "Checking for I/O breakpoint.");

        HM_REG_COUNTER(&pHmCpu->StatExportMinimal,          "/HM/CPU%u/Export/Minimal", "VM-entry exporting minimal guest-state.");
        HM_REG_COUNTER(&pHmCpu->StatExportFull,             "/HM/CPU%u/Export/Full", "VM-entry exporting the full guest-state.");
        HM_REG_COUNTER(&pHmCpu->StatLoadGuestFpu,           "/HM/CPU%u/Export/GuestFpu", "VM-entry loading the guest-FPU state.");
        HM_REG_COUNTER(&pHmCpu->StatExportHostState,        "/HM/CPU%u/Export/HostState", "VM-entry exporting host-state.");

        if (fCpuSupportsVmx)
        {
            HM_REG_COUNTER(&pHmCpu->StatVmxWriteHostRip,    "/HM/CPU%u/WriteHostRIP", "Number of VMX_VMCS_HOST_RIP instructions.");
            HM_REG_COUNTER(&pHmCpu->StatVmxWriteHostRsp,    "/HM/CPU%u/WriteHostRSP", "Number of VMX_VMCS_HOST_RSP instructions.");
            HM_REG_COUNTER(&pHmCpu->StatVmxVmLaunch,        "/HM/CPU%u/VMLaunch", "Number of VM-entries using VMLAUNCH.");
            HM_REG_COUNTER(&pHmCpu->StatVmxVmResume,        "/HM/CPU%u/VMResume", "Number of VM-entries using VMRESUME.");
        }

        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadRmSelBase,   "/HM/CPU%u/VMXCheck/RMSelBase", "Could not use VMX due to unsuitable real-mode selector base.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadRmSelLimit,  "/HM/CPU%u/VMXCheck/RMSelLimit", "Could not use VMX due to unsuitable real-mode selector limit.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadRmSelAttr,   "/HM/CPU%u/VMXCheck/RMSelAttrs", "Could not use VMX due to unsuitable real-mode selector attributes.");

        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadV86SelBase,  "/HM/CPU%u/VMXCheck/V86SelBase",  "Could not use VMX due to unsuitable v8086-mode selector base.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadV86SelLimit, "/HM/CPU%u/VMXCheck/V86SelLimit", "Could not use VMX due to unsuitable v8086-mode selector limit.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadV86SelAttr,  "/HM/CPU%u/VMXCheck/V86SelAttrs", "Could not use VMX due to unsuitable v8086-mode selector attributes.");

        HM_REG_COUNTER(&pHmCpu->StatVmxCheckRmOk,           "/HM/CPU%u/VMXCheck/VMX_RM", "VMX execution in real (V86) mode OK.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadSel,         "/HM/CPU%u/VMXCheck/Selector", "Could not use VMX due to unsuitable selector.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckBadRpl,         "/HM/CPU%u/VMXCheck/RPL", "Could not use VMX due to unsuitable RPL.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckPmOk,           "/HM/CPU%u/VMXCheck/VMX_PM", "VMX execution in protected mode OK.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheck1,              "/HM/CPU%u/VMXCheck/1", "Could not use VMX due to unsuitable state and no VMMDev heap");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheck2,              "/HM/CPU%u/VMXCheck/2", "CR0/CR4 fixed stuff.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckDisabled,       "/HM/CPU%u/VMXCheck/Disabled", "VMX is not enabled.");
        HM_REG_COUNTER(&pHmCpu->StatVmxCheckOk,             "/HM/CPU%u/VMXCheck/Ok", "We could do VMX!");
#endif
        if (fCpuSupportsVmx)
        {
            HM_REG_COUNTER(&pHmCpu->StatExitPreemptTimer,                      "/HM/CPU%u/PreemptTimer",                          "VMX-preemption timer fired.");
            HM_REG_COUNTER(&pHmCpu->StatVmxPreemptionReusingDeadline,          "/HM/CPU%u/PreemptTimer/ReusingDeadline",          "VMX-preemption timer arming logic using previously calculated deadline");
            HM_REG_COUNTER(&pHmCpu->StatVmxPreemptionReusingDeadlineExpired,   "/HM/CPU%u/PreemptTimer/ReusingDeadlineExpired",   "VMX-preemption timer arming logic found previous deadline already expired (ignored)");
            HM_REG_COUNTER(&pHmCpu->StatVmxPreemptionRecalcingDeadline,        "/HM/CPU%u/PreemptTimer/RecalcingDeadline",        "VMX-preemption timer arming logic recalculating the deadline (slightly expensive)");
            HM_REG_COUNTER(&pHmCpu->StatVmxPreemptionRecalcingDeadlineExpired, "/HM/CPU%u/PreemptTimer/RecalcingDeadlineExpired", "VMX-preemption timer arming logic found recalculated deadline expired (ignored)");
        }
#ifdef VBOX_WITH_STATISTICS
        /*
         * Guest Exit reason stats.
         */
        if (fCpuSupportsVmx)
        {
            for (int j = 0; j < MAX_EXITREASON_STAT; j++)
            {
                const char *pszExitName = HMGetVmxExitName(j);
                if (pszExitName)
                {
                    rc = STAMR3RegisterF(pVM, &pHmCpu->aStatExitReason[j], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                                         STAMUNIT_OCCURENCES, pszExitName, "/HM/CPU%u/Exit/Reason/%02x", idCpu, j);
                    AssertRCReturn(rc, rc);
                }
            }
        }
        else
        {
            for (int j = 0; j < MAX_EXITREASON_STAT; j++)
            {
                const char *pszExitName = HMGetSvmExitName(j);
                if (pszExitName)
                {
                    rc = STAMR3RegisterF(pVM, &pHmCpu->aStatExitReason[j], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                                         STAMUNIT_OCCURENCES, pszExitName, "/HM/CPU%u/Exit/Reason/%02x", idCpu, j);
                    AssertRC(rc);
                }
            }
        }
        HM_REG_COUNTER(&pHmCpu->StatExitReasonNpf, "/HM/CPU%u/Exit/Reason/#NPF", "Nested page faults");

#if defined(VBOX_WITH_NESTED_HWVIRT_SVM) || defined(VBOX_WITH_NESTED_HWVIRT_VMX)
        /*
         * Nested-guest VM-exit reason stats.
         */
        if (fCpuSupportsVmx)
        {
            for (int j = 0; j < MAX_EXITREASON_STAT; j++)
            {
                const char *pszExitName = HMGetVmxExitName(j);
                if (pszExitName)
                {
                    rc = STAMR3RegisterF(pVM, &pHmCpu->aStatNestedExitReason[j], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                                         STAMUNIT_OCCURENCES, pszExitName, "/HM/CPU%u/ExitNestedGuest/Reason/%02x", idCpu, j);
                    AssertRC(rc);
                }
            }
            HM_REG_COUNTER(&pHmCpu->StatNestedExitACSplitLock, "/HM/CPU%u/ExitNestedGuest/Trap/#AC-split-lock", "Nested-guest triggered #AC due to split-lock being enabled on the host.");
        }
        else
        {
            for (int j = 0; j < MAX_EXITREASON_STAT; j++)
            {
                const char *pszExitName = HMGetSvmExitName(j);
                if (pszExitName)
                {
                    rc = STAMR3RegisterF(pVM, &pHmCpu->aStatNestedExitReason[j], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                                         STAMUNIT_OCCURENCES, pszExitName, "/HM/CPU%u/ExitNestedGuest/Reason/%02x", idCpu, j);
                    AssertRC(rc);
                }
            }
        }
        HM_REG_COUNTER(&pHmCpu->StatNestedExitReasonNpf, "/HM/CPU%u/ExitNestedGuest/Reason/#NPF", "Nested page faults");
#endif

        /*
         * Injected interrupts stats.
         */
        char szDesc[64];
        for (unsigned j = 0; j < RT_ELEMENTS(pHmCpu->aStatInjectedIrqs); j++)
        {
            RTStrPrintf(&szDesc[0], sizeof(szDesc),  "Interrupt %u", j);
            rc = STAMR3RegisterF(pVM, &pHmCpu->aStatInjectedIrqs[j], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                                 STAMUNIT_OCCURENCES, szDesc, "/HM/CPU%u/EventInject/InjectIntr/%02X", idCpu, j);
            AssertRC(rc);
        }

        /*
         * Injected exception stats.
         */
        for (unsigned j = 0; j < RT_ELEMENTS(pHmCpu->aStatInjectedXcpts); j++)
        {
            RTStrPrintf(&szDesc[0], sizeof(szDesc),  "%s exception", hmR3GetXcptName(j));
            rc = STAMR3RegisterF(pVM, &pHmCpu->aStatInjectedXcpts[j], STAMTYPE_COUNTER, STAMVISIBILITY_USED,
                                 STAMUNIT_OCCURENCES, szDesc, "/HM/CPU%u/EventInject/InjectXcpt/%02X", idCpu, j);
            AssertRC(rc);
        }

#endif /* VBOX_WITH_STATISTICS */
#undef HM_REG_COUNTER
#undef HM_REG_PROFILE
#undef HM_REG_STAT
    }

    return VINF_SUCCESS;
}


/**
 * Called when a init phase has completed.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmWhat             The phase that completed.
 */
VMMR3_INT_DECL(int) HMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    switch (enmWhat)
    {
        case VMINITCOMPLETED_RING3:
            return hmR3InitFinalizeR3(pVM);
        case VMINITCOMPLETED_RING0:
            return hmR3InitFinalizeR0(pVM);
        default:
            return VINF_SUCCESS;
    }
}


/**
 * Turns off normal raw mode features.
 *
 * @param   pVM         The cross context VM structure.
 */
static void hmR3DisableRawMode(PVM pVM)
{
/** @todo r=bird: HM shouldn't be doing this crap. */
    /* Reinit the paging mode to force the new shadow mode. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        PGMHCChangeMode(pVM, pVCpu, PGMMODE_REAL, false /* fForce */);
    }
}


/**
 * Initialize VT-x or AMD-V.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int hmR3InitFinalizeR0(PVM pVM)
{
    int rc;

    /*
     * Since HM is in charge of large pages, if large pages isn't supported on Intel CPUs,
     * we must disable it here. Doing it here rather than in hmR3InitFinalizeR0Intel covers
     * the case of informing PGM even when NEM is the execution engine.
     */
    if (   pVM->hm.s.fLargePages
        && pVM->hm.s.vmx.fSupported
        && !(pVM->hm.s.ForR3.vmx.Msrs.u64EptVpidCaps & MSR_IA32_VMX_EPT_VPID_CAP_PDE_2M))
    {
        pVM->hm.s.fLargePages = false;
        PGMSetLargePageUsage(pVM, false);
        LogRel(("HM: Disabled large page support as the CPU doesn't allow EPT PDEs to map 2MB pages\n"));
    }

    if (!HMIsEnabled(pVM))
        return VINF_SUCCESS;

    /*
     * Hack to allow users to work around broken BIOSes that incorrectly set
     * EFER.SVME, which makes us believe somebody else is already using AMD-V.
     */
    if (   !pVM->hm.s.vmx.fSupported
        && !pVM->hm.s.svm.fSupported
        &&  pVM->hm.s.ForR3.rcInit == VERR_SVM_IN_USE /* implies functional AMD-V */
        &&  RTEnvExist("VBOX_HWVIRTEX_IGNORE_SVM_IN_USE"))
    {
        LogRel(("HM: VBOX_HWVIRTEX_IGNORE_SVM_IN_USE active!\n"));
        pVM->hm.s.svm.fSupported        = true;
        pVM->hm.s.svm.fIgnoreInUseError = true;
        pVM->hm.s.ForR3.rcInit = VINF_SUCCESS;
    }

    /*
     * Report ring-0 init errors.
     */
    if (   !pVM->hm.s.vmx.fSupported
        && !pVM->hm.s.svm.fSupported)
    {
        LogRel(("HM: Failed to initialize VT-x / AMD-V: %Rrc\n", pVM->hm.s.ForR3.rcInit));
        LogRel(("HM: VMX MSR_IA32_FEATURE_CONTROL=%RX64\n", pVM->hm.s.ForR3.vmx.u64HostFeatCtrl));
        switch (pVM->hm.s.ForR3.rcInit)
        {
            case VERR_VMX_IN_VMX_ROOT_MODE:
                return VM_SET_ERROR(pVM, VERR_VMX_IN_VMX_ROOT_MODE, "VT-x is being used by another hypervisor");
            case VERR_VMX_NO_VMX:
                return VM_SET_ERROR(pVM, VERR_VMX_NO_VMX, "VT-x is not available");
            case VERR_VMX_MSR_VMX_DISABLED:
                return VM_SET_ERROR(pVM, VERR_VMX_MSR_VMX_DISABLED, "VT-x is disabled in the BIOS");
            case VERR_VMX_MSR_ALL_VMX_DISABLED:
                return VM_SET_ERROR(pVM, VERR_VMX_MSR_ALL_VMX_DISABLED, "VT-x is disabled in the BIOS for all CPU modes");
            case VERR_VMX_MSR_LOCKING_FAILED:
                return VM_SET_ERROR(pVM, VERR_VMX_MSR_LOCKING_FAILED, "Failed to lock VT-x features while trying to enable VT-x");
            case VERR_VMX_MSR_VMX_ENABLE_FAILED:
                return VM_SET_ERROR(pVM, VERR_VMX_MSR_VMX_ENABLE_FAILED, "Failed to enable VT-x features");
            case VERR_VMX_MSR_SMX_VMX_ENABLE_FAILED:
                return VM_SET_ERROR(pVM, VERR_VMX_MSR_SMX_VMX_ENABLE_FAILED, "Failed to enable VT-x features in SMX mode");

            case VERR_SVM_IN_USE:
                return VM_SET_ERROR(pVM, VERR_SVM_IN_USE, "AMD-V is being used by another hypervisor");
            case VERR_SVM_NO_SVM:
                return VM_SET_ERROR(pVM, VERR_SVM_NO_SVM, "AMD-V is not available");
            case VERR_SVM_DISABLED:
                return VM_SET_ERROR(pVM, VERR_SVM_DISABLED, "AMD-V is disabled in the BIOS");
        }
        return VMSetError(pVM, pVM->hm.s.ForR3.rcInit, RT_SRC_POS, "HM ring-0 init failed: %Rrc", pVM->hm.s.ForR3.rcInit);
    }

    /*
     * Enable VT-x or AMD-V on all host CPUs.
     */
    rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), 0 /*idCpu*/, VMMR0_DO_HM_ENABLE, 0, NULL);
    if (RT_FAILURE(rc))
    {
        LogRel(("HM: Failed to enable, error %Rrc\n", rc));
        HMR3CheckError(pVM, rc);
        return rc;
    }

    /*
     * No TPR patching is required when the IO-APIC is not enabled for this VM.
     * (Main should have taken care of this already)
     */
    if (!PDMHasIoApic(pVM))
    {
        Assert(!pVM->hm.s.fTprPatchingAllowed); /* paranoia */
        pVM->hm.s.fTprPatchingAllowed = false;
    }

    LogRel(("HM: fWorldSwitcher=%#x (fIbpbOnVmExit=%RTbool fIbpbOnVmEntry=%RTbool fL1dFlushOnVmEntry=%RTbool); fL1dFlushOnSched=%RTbool fMdsClearOnVmEntry=%RTbool\n",
            pVM->hm.s.ForR3.fWorldSwitcher, pVM->hm.s.fIbpbOnVmExit, pVM->hm.s.fIbpbOnVmEntry, pVM->hm.s.fL1dFlushOnVmEntry,
            pVM->hm.s.fL1dFlushOnSched, pVM->hm.s.fMdsClearOnVmEntry));

    /*
     * Do the vendor specific initialization
     *
     * Note! We disable release log buffering here since we're doing relatively
     *       lot of logging and doesn't want to hit the disk with each LogRel
     *       statement.
     */
    AssertLogRelReturn(!pVM->hm.s.fInitialized, VERR_HM_IPE_5);
    bool fOldBuffered = RTLogRelSetBuffering(true /*fBuffered*/);
    if (pVM->hm.s.vmx.fSupported)
        rc = hmR3InitFinalizeR0Intel(pVM);
    else
        rc = hmR3InitFinalizeR0Amd(pVM);
    LogRel((pVM->hm.s.fGlobalInit ? "HM: VT-x/AMD-V init method: Global\n"
                                  : "HM: VT-x/AMD-V init method: Local\n"));
    RTLogRelSetBuffering(fOldBuffered);
    pVM->hm.s.fInitialized = true;

    return rc;
}


/**
 * @callback_method_impl{FNPDMVMMDEVHEAPNOTIFY}
 */
static DECLCALLBACK(void) hmR3VmmDevHeapNotify(PVM pVM, void *pvAllocation, RTGCPHYS GCPhysAllocation)
{
    NOREF(pVM);
    NOREF(pvAllocation);
    NOREF(GCPhysAllocation);
}


/**
 * Returns a description of the VMCS (and associated regions') memory type given the
 * IA32_VMX_BASIC MSR.
 *
 * @returns The descriptive memory type.
 * @param   uMsrVmxBasic        IA32_VMX_BASIC MSR value.
 */
static const char *hmR3VmxGetMemTypeDesc(uint64_t uMsrVmxBasic)
{
    uint8_t const uMemType = RT_BF_GET(uMsrVmxBasic, VMX_BF_BASIC_VMCS_MEM_TYPE);
    switch (uMemType)
    {
        case VMX_BASIC_MEM_TYPE_WB: return "Write Back (WB)";
        case VMX_BASIC_MEM_TYPE_UC: return "Uncacheable (UC)";
    }
    return "Unknown";
}


/**
 * Returns a single-line description of all the activity-states supported by the CPU
 * given the IA32_VMX_MISC MSR.
 *
 * @returns All supported activity states.
 * @param   uMsrMisc            IA32_VMX_MISC MSR value.
 */
static const char *hmR3VmxGetActivityStateAllDesc(uint64_t uMsrMisc)
{
    static const char * const s_apszActStates[] =
    {
        "",
        " ( HLT )",
        " ( SHUTDOWN )",
        " ( HLT SHUTDOWN )",
        " ( SIPI_WAIT )",
        " ( HLT SIPI_WAIT )",
        " ( SHUTDOWN SIPI_WAIT )",
        " ( HLT SHUTDOWN SIPI_WAIT )"
    };
    uint8_t const idxActStates = RT_BF_GET(uMsrMisc, VMX_BF_MISC_ACTIVITY_STATES);
    Assert(idxActStates < RT_ELEMENTS(s_apszActStates));
    return s_apszActStates[idxActStates];
}


/**
 * Reports MSR_IA32_FEATURE_CONTROL MSR to the log.
 *
 * @param   fFeatMsr    The feature control MSR value.
 */
static void hmR3VmxReportFeatCtlMsr(uint64_t fFeatMsr)
{
    uint64_t const val = fFeatMsr;
    LogRel(("HM: MSR_IA32_FEATURE_CONTROL          = %#RX64\n", val));
    HMVMX_REPORT_MSR_CAP(val, "LOCK",             MSR_IA32_FEATURE_CONTROL_LOCK);
    HMVMX_REPORT_MSR_CAP(val, "SMX_VMXON",        MSR_IA32_FEATURE_CONTROL_SMX_VMXON);
    HMVMX_REPORT_MSR_CAP(val, "VMXON",            MSR_IA32_FEATURE_CONTROL_VMXON);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_LOCAL_FN0", MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_0);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_LOCAL_FN1", MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_1);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_LOCAL_FN2", MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_2);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_LOCAL_FN3", MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_3);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_LOCAL_FN4", MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_4);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_LOCAL_FN5", MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_5);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_LOCAL_FN6", MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_6);
    HMVMX_REPORT_MSR_CAP(val, "SENTER_GLOBAL_EN", MSR_IA32_FEATURE_CONTROL_SENTER_GLOBAL_EN);
    HMVMX_REPORT_MSR_CAP(val, "SGX_LAUNCH_EN",    MSR_IA32_FEATURE_CONTROL_SGX_LAUNCH_EN);
    HMVMX_REPORT_MSR_CAP(val, "SGX_GLOBAL_EN",    MSR_IA32_FEATURE_CONTROL_SGX_GLOBAL_EN);
    HMVMX_REPORT_MSR_CAP(val, "LMCE",             MSR_IA32_FEATURE_CONTROL_LMCE);
    if (!(val & MSR_IA32_FEATURE_CONTROL_LOCK))
        LogRel(("HM:   MSR_IA32_FEATURE_CONTROL lock bit not set, possibly bad hardware!\n"));
}


/**
 * Reports MSR_IA32_VMX_BASIC MSR to the log.
 *
 * @param   uBasicMsr    The VMX basic MSR value.
 */
static void hmR3VmxReportBasicMsr(uint64_t uBasicMsr)
{
    LogRel(("HM: MSR_IA32_VMX_BASIC                = %#RX64\n",     uBasicMsr));
    LogRel(("HM:   VMCS id                           = %#x\n",      RT_BF_GET(uBasicMsr, VMX_BF_BASIC_VMCS_ID)));
    LogRel(("HM:   VMCS size                         = %u bytes\n", RT_BF_GET(uBasicMsr, VMX_BF_BASIC_VMCS_SIZE)));
    LogRel(("HM:   VMCS physical address limit       = %s\n",       RT_BF_GET(uBasicMsr, VMX_BF_BASIC_PHYSADDR_WIDTH) ?
                                                                    "< 4 GB" : "None"));
    LogRel(("HM:   VMCS memory type                  = %s\n",       hmR3VmxGetMemTypeDesc(uBasicMsr)));
    LogRel(("HM:   Dual-monitor treatment support    = %RTbool\n",  RT_BF_GET(uBasicMsr, VMX_BF_BASIC_DUAL_MON)));
    LogRel(("HM:   OUTS & INS instruction-info       = %RTbool\n",  RT_BF_GET(uBasicMsr, VMX_BF_BASIC_VMCS_INS_OUTS)));
    LogRel(("HM:   Supports true-capability MSRs     = %RTbool\n",  RT_BF_GET(uBasicMsr, VMX_BF_BASIC_TRUE_CTLS)));
    LogRel(("HM:   VM-entry Xcpt error-code optional = %RTbool\n",  RT_BF_GET(uBasicMsr, VMX_BF_BASIC_XCPT_ERRCODE)));
}


/**
 * Reports MSR_IA32_PINBASED_CTLS to the log.
 *
 * @param   pVmxMsr    Pointer to the VMX MSR.
 */
static void hmR3VmxReportPinBasedCtlsMsr(PCVMXCTLSMSR pVmxMsr)
{
    uint64_t const fAllowed1 = pVmxMsr->n.allowed1;
    uint64_t const fAllowed0 = pVmxMsr->n.allowed0;
    LogRel(("HM: MSR_IA32_VMX_PINBASED_CTLS        = %#RX64\n", pVmxMsr->u));
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "EXT_INT_EXIT",  VMX_PIN_CTLS_EXT_INT_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "NMI_EXIT",      VMX_PIN_CTLS_NMI_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "VIRTUAL_NMI",   VMX_PIN_CTLS_VIRT_NMI);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "PREEMPT_TIMER", VMX_PIN_CTLS_PREEMPT_TIMER);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "POSTED_INT",    VMX_PIN_CTLS_POSTED_INT);
}


/**
 * Reports MSR_IA32_VMX_PROCBASED_CTLS MSR to the log.
 *
 * @param   pVmxMsr    Pointer to the VMX MSR.
 */
static void hmR3VmxReportProcBasedCtlsMsr(PCVMXCTLSMSR pVmxMsr)
{
    uint64_t const fAllowed1 = pVmxMsr->n.allowed1;
    uint64_t const fAllowed0 = pVmxMsr->n.allowed0;
    LogRel(("HM: MSR_IA32_VMX_PROCBASED_CTLS       = %#RX64\n", pVmxMsr->u));
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "INT_WINDOW_EXIT",    VMX_PROC_CTLS_INT_WINDOW_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "USE_TSC_OFFSETTING", VMX_PROC_CTLS_USE_TSC_OFFSETTING);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "HLT_EXIT",           VMX_PROC_CTLS_HLT_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "INVLPG_EXIT",        VMX_PROC_CTLS_INVLPG_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "MWAIT_EXIT",         VMX_PROC_CTLS_MWAIT_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "RDPMC_EXIT",         VMX_PROC_CTLS_RDPMC_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "RDTSC_EXIT",         VMX_PROC_CTLS_RDTSC_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CR3_LOAD_EXIT",      VMX_PROC_CTLS_CR3_LOAD_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CR3_STORE_EXIT",     VMX_PROC_CTLS_CR3_STORE_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "USE_TERTIARY_CTLS",  VMX_PROC_CTLS_USE_TERTIARY_CTLS);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CR8_LOAD_EXIT",      VMX_PROC_CTLS_CR8_LOAD_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CR8_STORE_EXIT",     VMX_PROC_CTLS_CR8_STORE_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "USE_TPR_SHADOW",     VMX_PROC_CTLS_USE_TPR_SHADOW);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "NMI_WINDOW_EXIT",    VMX_PROC_CTLS_NMI_WINDOW_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "MOV_DR_EXIT",        VMX_PROC_CTLS_MOV_DR_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "UNCOND_IO_EXIT",     VMX_PROC_CTLS_UNCOND_IO_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "USE_IO_BITMAPS",     VMX_PROC_CTLS_USE_IO_BITMAPS);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "MONITOR_TRAP_FLAG",  VMX_PROC_CTLS_MONITOR_TRAP_FLAG);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "USE_MSR_BITMAPS",    VMX_PROC_CTLS_USE_MSR_BITMAPS);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "MONITOR_EXIT",       VMX_PROC_CTLS_MONITOR_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "PAUSE_EXIT",         VMX_PROC_CTLS_PAUSE_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "USE_SECONDARY_CTLS", VMX_PROC_CTLS_USE_SECONDARY_CTLS);
}


/**
 * Reports MSR_IA32_VMX_PROCBASED_CTLS2 MSR to the log.
 *
 * @param   pVmxMsr    Pointer to the VMX MSR.
 */
static void hmR3VmxReportProcBasedCtls2Msr(PCVMXCTLSMSR pVmxMsr)
{
    uint64_t const fAllowed1 = pVmxMsr->n.allowed1;
    uint64_t const fAllowed0 = pVmxMsr->n.allowed0;
    LogRel(("HM: MSR_IA32_VMX_PROCBASED_CTLS2      = %#RX64\n", pVmxMsr->u));
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "VIRT_APIC_ACCESS",    VMX_PROC_CTLS2_VIRT_APIC_ACCESS);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "EPT",                 VMX_PROC_CTLS2_EPT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "DESC_TABLE_EXIT",     VMX_PROC_CTLS2_DESC_TABLE_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "RDTSCP",              VMX_PROC_CTLS2_RDTSCP);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "VIRT_X2APIC_MODE",    VMX_PROC_CTLS2_VIRT_X2APIC_MODE);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "VPID",                VMX_PROC_CTLS2_VPID);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "WBINVD_EXIT",         VMX_PROC_CTLS2_WBINVD_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "UNRESTRICTED_GUEST",  VMX_PROC_CTLS2_UNRESTRICTED_GUEST);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "APIC_REG_VIRT",       VMX_PROC_CTLS2_APIC_REG_VIRT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "VIRT_INT_DELIVERY",   VMX_PROC_CTLS2_VIRT_INT_DELIVERY);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "PAUSE_LOOP_EXIT",     VMX_PROC_CTLS2_PAUSE_LOOP_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "RDRAND_EXIT",         VMX_PROC_CTLS2_RDRAND_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "INVPCID",             VMX_PROC_CTLS2_INVPCID);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "VMFUNC",              VMX_PROC_CTLS2_VMFUNC);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "VMCS_SHADOWING",      VMX_PROC_CTLS2_VMCS_SHADOWING);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "ENCLS_EXIT",          VMX_PROC_CTLS2_ENCLS_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "RDSEED_EXIT",         VMX_PROC_CTLS2_RDSEED_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "PML",                 VMX_PROC_CTLS2_PML);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "EPT_XCPT_VE",         VMX_PROC_CTLS2_EPT_XCPT_VE);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CONCEAL_VMX_FROM_PT", VMX_PROC_CTLS2_CONCEAL_VMX_FROM_PT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "XSAVES_XRSTORS",      VMX_PROC_CTLS2_XSAVES_XRSTORS);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "PASID_TRANSLATE",     VMX_PROC_CTLS2_PASID_TRANSLATE);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "MODE_BASED_EPT_PERM", VMX_PROC_CTLS2_MODE_BASED_EPT_PERM);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "SPP_EPT",             VMX_PROC_CTLS2_SPP_EPT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "PT_EPT",              VMX_PROC_CTLS2_PT_EPT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "TSC_SCALING",         VMX_PROC_CTLS2_TSC_SCALING);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "USER_WAIT_PAUSE",     VMX_PROC_CTLS2_USER_WAIT_PAUSE);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "PCONFIG",             VMX_PROC_CTLS2_PCONFIG);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "ENCLV_EXIT",          VMX_PROC_CTLS2_ENCLV_EXIT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "BUS_LOCK_DETECT",     VMX_PROC_CTLS2_BUS_LOCK_DETECT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "INSTR_TIMEOUT",       VMX_PROC_CTLS2_INSTR_TIMEOUT);
}


/**
 * Reports MSR_IA32_VMX_PROCBASED_CTLS3 MSR to the log.
 *
 * @param   uProcCtls3    The tertiary processor-based VM-execution control MSR.
 */
static void hmR3VmxReportProcBasedCtls3Msr(uint64_t uProcCtls3)
{
    LogRel(("HM: MSR_IA32_VMX_PROCBASED_CTLS3      = %#RX64\n", uProcCtls3));
    LogRel(("HM:   LOADIWKEY_EXIT                    = %RTbool\n", RT_BOOL(uProcCtls3 & VMX_PROC_CTLS3_LOADIWKEY_EXIT)));
    LogRel(("HM:   HLAT                              = %RTbool\n", RT_BOOL(uProcCtls3 & VMX_PROC_CTLS3_HLAT)));
    LogRel(("HM:   EPT_PAGING_WRITE                  = %RTbool\n", RT_BOOL(uProcCtls3 & VMX_PROC_CTLS3_EPT_PAGING_WRITE)));
    LogRel(("HM:   GST_PAGING_VERIFY                 = %RTbool\n", RT_BOOL(uProcCtls3 & VMX_PROC_CTLS3_GST_PAGING_VERIFY)));
    LogRel(("HM:   IPI_VIRT                          = %RTbool\n", RT_BOOL(uProcCtls3 & VMX_PROC_CTLS3_IPI_VIRT)));
    LogRel(("HM:   VIRT_SPEC_CTRL                    = %RTbool\n", RT_BOOL(uProcCtls3 & VMX_PROC_CTLS3_VIRT_SPEC_CTRL)));
}


/**
 * Reports MSR_IA32_VMX_ENTRY_CTLS to the log.
 *
 * @param   pVmxMsr    Pointer to the VMX MSR.
 */
static void hmR3VmxReportEntryCtlsMsr(PCVMXCTLSMSR pVmxMsr)
{
    uint64_t const fAllowed1 = pVmxMsr->n.allowed1;
    uint64_t const fAllowed0 = pVmxMsr->n.allowed0;
    LogRel(("HM: MSR_IA32_VMX_ENTRY_CTLS           = %#RX64\n", pVmxMsr->u));
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_DEBUG",          VMX_ENTRY_CTLS_LOAD_DEBUG);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "IA32E_MODE_GUEST",    VMX_ENTRY_CTLS_IA32E_MODE_GUEST);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "ENTRY_TO_SMM",        VMX_ENTRY_CTLS_ENTRY_TO_SMM);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "DEACTIVATE_DUAL_MON", VMX_ENTRY_CTLS_DEACTIVATE_DUAL_MON);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_PERF_MSR",       VMX_ENTRY_CTLS_LOAD_PERF_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_PAT_MSR",        VMX_ENTRY_CTLS_LOAD_PAT_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_EFER_MSR",       VMX_ENTRY_CTLS_LOAD_EFER_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_BNDCFGS_MSR",    VMX_ENTRY_CTLS_LOAD_BNDCFGS_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CONCEAL_VMX_FROM_PT", VMX_ENTRY_CTLS_CONCEAL_VMX_FROM_PT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_RTIT_CTL_MSR",   VMX_ENTRY_CTLS_LOAD_RTIT_CTL_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_UINV",           VMX_ENTRY_CTLS_LOAD_UINV);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_CET_STATE",      VMX_ENTRY_CTLS_LOAD_CET_STATE);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_LBR_CTL_MSR",    VMX_ENTRY_CTLS_LOAD_LBR_CTL_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_PKRS_MSR",       VMX_ENTRY_CTLS_LOAD_PKRS_MSR);
}


/**
 * Reports MSR_IA32_VMX_EXIT_CTLS to the log.
 *
 * @param   pVmxMsr    Pointer to the VMX MSR.
 */
static void hmR3VmxReportExitCtlsMsr(PCVMXCTLSMSR pVmxMsr)
{
    uint64_t const fAllowed1 = pVmxMsr->n.allowed1;
    uint64_t const fAllowed0 = pVmxMsr->n.allowed0;
    LogRel(("HM: MSR_IA32_VMX_EXIT_CTLS            = %#RX64\n", pVmxMsr->u));
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "SAVE_DEBUG",           VMX_EXIT_CTLS_SAVE_DEBUG);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "HOST_ADDR_SPACE_SIZE", VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_PERF_MSR",        VMX_EXIT_CTLS_LOAD_PERF_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "ACK_EXT_INT",          VMX_EXIT_CTLS_ACK_EXT_INT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "SAVE_PAT_MSR",         VMX_EXIT_CTLS_SAVE_PAT_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_PAT_MSR",         VMX_EXIT_CTLS_LOAD_PAT_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "SAVE_EFER_MSR",        VMX_EXIT_CTLS_SAVE_EFER_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_EFER_MSR",        VMX_EXIT_CTLS_LOAD_EFER_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "SAVE_PREEMPT_TIMER",   VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CLEAR_BNDCFGS_MSR",    VMX_EXIT_CTLS_CLEAR_BNDCFGS_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CONCEAL_VMX_FROM_PT",  VMX_EXIT_CTLS_CONCEAL_VMX_FROM_PT);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CLEAR_RTIT_CTL_MSR",   VMX_EXIT_CTLS_CLEAR_RTIT_CTL_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CLEAR_LBR_CTL_MSR",    VMX_EXIT_CTLS_CLEAR_LBR_CTL_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "CLEAR_UINV",           VMX_EXIT_CTLS_CLEAR_UINV);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_CET_STATE",       VMX_EXIT_CTLS_LOAD_CET_STATE);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "LOAD_PKRS_MSR",        VMX_EXIT_CTLS_LOAD_PKRS_MSR);
    HMVMX_REPORT_FEAT(fAllowed1, fAllowed0, "SAVE_PERF_MSR",        VMX_EXIT_CTLS_SAVE_PERF_MSR);
}


/**
 * Reports MSR_IA32_VMX_EPT_VPID_CAP MSR to the log.
 *
 * @param   fCaps    The VMX EPT/VPID capability MSR value.
 */
static void hmR3VmxReportEptVpidCapsMsr(uint64_t fCaps)
{
    LogRel(("HM: MSR_IA32_VMX_EPT_VPID_CAP         = %#RX64\n", fCaps));
    HMVMX_REPORT_MSR_CAP(fCaps, "RWX_X_ONLY",                            MSR_IA32_VMX_EPT_VPID_CAP_RWX_X_ONLY);
    HMVMX_REPORT_MSR_CAP(fCaps, "PAGE_WALK_LENGTH_4",                    MSR_IA32_VMX_EPT_VPID_CAP_PAGE_WALK_LENGTH_4);
    HMVMX_REPORT_MSR_CAP(fCaps, "PAGE_WALK_LENGTH_5",                    MSR_IA32_VMX_EPT_VPID_CAP_PAGE_WALK_LENGTH_5);
    HMVMX_REPORT_MSR_CAP(fCaps, "MEMTYPE_UC",                            MSR_IA32_VMX_EPT_VPID_CAP_MEMTYPE_UC);
    HMVMX_REPORT_MSR_CAP(fCaps, "MEMTYPE_WB",                            MSR_IA32_VMX_EPT_VPID_CAP_MEMTYPE_WB);
    HMVMX_REPORT_MSR_CAP(fCaps, "PDE_2M",                                MSR_IA32_VMX_EPT_VPID_CAP_PDE_2M);
    HMVMX_REPORT_MSR_CAP(fCaps, "PDPTE_1G",                              MSR_IA32_VMX_EPT_VPID_CAP_PDPTE_1G);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVEPT",                                MSR_IA32_VMX_EPT_VPID_CAP_INVEPT);
    HMVMX_REPORT_MSR_CAP(fCaps, "ACCESS_DIRTY",                          MSR_IA32_VMX_EPT_VPID_CAP_ACCESS_DIRTY);
    HMVMX_REPORT_MSR_CAP(fCaps, "ADVEXITINFO_EPT_VIOLATION",             MSR_IA32_VMX_EPT_VPID_CAP_ADVEXITINFO_EPT_VIOLATION);
    HMVMX_REPORT_MSR_CAP(fCaps, "SUPER_SHW_STACK",                       MSR_IA32_VMX_EPT_VPID_CAP_SUPER_SHW_STACK);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVEPT_SINGLE_CONTEXT",                 MSR_IA32_VMX_EPT_VPID_CAP_INVEPT_SINGLE_CONTEXT);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVEPT_ALL_CONTEXTS",                   MSR_IA32_VMX_EPT_VPID_CAP_INVEPT_ALL_CONTEXTS);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVVPID",                               MSR_IA32_VMX_EPT_VPID_CAP_INVVPID);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVVPID_INDIV_ADDR",                    MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_INDIV_ADDR);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVVPID_SINGLE_CONTEXT",                MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_SINGLE_CONTEXT);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVVPID_ALL_CONTEXTS",                  MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_ALL_CONTEXTS);
    HMVMX_REPORT_MSR_CAP(fCaps, "INVVPID_SINGLE_CONTEXT_RETAIN_GLOBALS", MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_SINGLE_CONTEXT_RETAIN_GLOBALS);
}


/**
 * Reports MSR_IA32_VMX_MISC MSR to the log.
 *
 * @param   pVM      Pointer to the VM.
 * @param   fMisc    The VMX misc. MSR value.
 */
static void hmR3VmxReportMiscMsr(PVM pVM, uint64_t fMisc)
{
    LogRel(("HM: MSR_IA32_VMX_MISC                 = %#RX64\n", fMisc));
    uint8_t const cPreemptTimerShift = RT_BF_GET(fMisc, VMX_BF_MISC_PREEMPT_TIMER_TSC);
    if (cPreemptTimerShift == pVM->hm.s.vmx.cPreemptTimerShift)
        LogRel(("HM:   PREEMPT_TIMER_TSC                 = %#x\n",    cPreemptTimerShift));
    else
    {
        LogRel(("HM:   PREEMPT_TIMER_TSC                 = %#x - erratum detected, using %#x instead\n", cPreemptTimerShift,
                pVM->hm.s.vmx.cPreemptTimerShift));
    }
    LogRel(("HM:   EXIT_SAVE_EFER_LMA                = %RTbool\n",    RT_BF_GET(fMisc, VMX_BF_MISC_EXIT_SAVE_EFER_LMA)));
    LogRel(("HM:   ACTIVITY_STATES                   = %#x%s\n",      RT_BF_GET(fMisc, VMX_BF_MISC_ACTIVITY_STATES),
                                                                      hmR3VmxGetActivityStateAllDesc(fMisc)));
    LogRel(("HM:   INTEL_PT                          = %RTbool\n",    RT_BF_GET(fMisc, VMX_BF_MISC_INTEL_PT)));
    LogRel(("HM:   SMM_READ_SMBASE_MSR               = %RTbool\n",    RT_BF_GET(fMisc, VMX_BF_MISC_SMM_READ_SMBASE_MSR)));
    LogRel(("HM:   CR3_TARGET                        = %#x\n",        RT_BF_GET(fMisc, VMX_BF_MISC_CR3_TARGET)));
    LogRel(("HM:   MAX_MSR                           = %#x ( %u )\n", RT_BF_GET(fMisc, VMX_BF_MISC_MAX_MSRS),
                                                                      VMX_MISC_MAX_MSRS(fMisc)));
    LogRel(("HM:   VMXOFF_BLOCK_SMI                  = %RTbool\n",    RT_BF_GET(fMisc, VMX_BF_MISC_VMXOFF_BLOCK_SMI)));
    LogRel(("HM:   VMWRITE_ALL                       = %RTbool\n",    RT_BF_GET(fMisc, VMX_BF_MISC_VMWRITE_ALL)));
    LogRel(("HM:   ENTRY_INJECT_SOFT_INT             = %#x\n",        RT_BF_GET(fMisc, VMX_BF_MISC_ENTRY_INJECT_SOFT_INT)));
    LogRel(("HM:   MSEG_ID                           = %#x\n",        RT_BF_GET(fMisc, VMX_BF_MISC_MSEG_ID)));
}


/**
 * Reports MSR_IA32_VMX_VMCS_ENUM MSR to the log.
 *
 * @param   uVmcsEnum    The VMX VMCS enum MSR value.
 */
static void hmR3VmxReportVmcsEnumMsr(uint64_t uVmcsEnum)
{
    LogRel(("HM: MSR_IA32_VMX_VMCS_ENUM            = %#RX64\n", uVmcsEnum));
    LogRel(("HM:   HIGHEST_IDX                       = %#x\n", RT_BF_GET(uVmcsEnum, VMX_BF_VMCS_ENUM_HIGHEST_IDX)));
}


/**
 * Reports MSR_IA32_VMX_VMFUNC MSR to the log.
 *
 * @param   uVmFunc    The VMX VMFUNC MSR value.
 */
static void hmR3VmxReportVmFuncMsr(uint64_t uVmFunc)
{
    LogRel(("HM: MSR_IA32_VMX_VMFUNC               = %#RX64\n", uVmFunc));
    HMVMX_REPORT_ALLOWED_FEAT(uVmFunc, "EPTP_SWITCHING", RT_BF_GET(uVmFunc, VMX_BF_VMFUNC_EPTP_SWITCHING));
}


/**
 * Reports VMX CR0, CR4 fixed MSRs.
 *
 * @param   pMsrs    Pointer to the VMX MSRs.
 */
static void hmR3VmxReportCrFixedMsrs(PVMXMSRS pMsrs)
{
    LogRel(("HM: MSR_IA32_VMX_CR0_FIXED0           = %#RX64\n", pMsrs->u64Cr0Fixed0));
    LogRel(("HM: MSR_IA32_VMX_CR0_FIXED1           = %#RX64\n", pMsrs->u64Cr0Fixed1));
    LogRel(("HM: MSR_IA32_VMX_CR4_FIXED0           = %#RX64\n", pMsrs->u64Cr4Fixed0));
    LogRel(("HM: MSR_IA32_VMX_CR4_FIXED1           = %#RX64\n", pMsrs->u64Cr4Fixed1));
}


/**
 * Finish VT-x initialization (after ring-0 init).
 *
 * @returns VBox status code.
 * @param   pVM                The cross context VM structure.
 */
static int hmR3InitFinalizeR0Intel(PVM pVM)
{
    int rc;

    LogFunc(("pVM->hm.s.vmx.fSupported = %d\n", pVM->hm.s.vmx.fSupported));
    AssertLogRelReturn(pVM->hm.s.ForR3.vmx.u64HostFeatCtrl != 0, VERR_HM_IPE_4);

    LogRel(("HM: Using VT-x implementation 3.0\n"));
    LogRel(("HM: Max resume loops                  = %u\n",     pVM->hm.s.cMaxResumeLoopsCfg));
    LogRel(("HM: Host CR0                          = %#RX64\n", pVM->hm.s.ForR3.vmx.u64HostCr0));
    LogRel(("HM: Host CR4                          = %#RX64\n", pVM->hm.s.ForR3.vmx.u64HostCr4));
    LogRel(("HM: Host EFER                         = %#RX64\n", pVM->hm.s.ForR3.vmx.u64HostMsrEfer));
    LogRel(("HM: Host SMM_MONITOR_CTL              = %#RX64\n", pVM->hm.s.ForR3.vmx.u64HostSmmMonitorCtl));
    LogRel(("HM: Host CORE_CAPABILITIES            = %#RX64\n", pVM->hm.s.ForR3.vmx.u64HostCoreCap));
    LogRel(("HM: Host MEMORY_CTRL                  = %#RX64%s\n", pVM->hm.s.ForR3.vmx.u64HostMemoryCtrl,
            pVM->hm.s.ForR3.vmx.u64HostMemoryCtrl & MSR_MEMORY_CTRL_SPLIT_LOCK_DISABLE ? " - split-lock disable!" : ""));
    LogRel(("HM: Host DR6 zero'ed                  = %#RX64%s\n", pVM->hm.s.ForR3.vmx.u64HostDr6Zeroed,
            pVM->hm.s.ForR3.vmx.fAlwaysInterceptMovDRx ? " - always intercept MOV DRx" : ""));

    hmR3VmxReportFeatCtlMsr(pVM->hm.s.ForR3.vmx.u64HostFeatCtrl);
    hmR3VmxReportBasicMsr(pVM->hm.s.ForR3.vmx.Msrs.u64Basic);

    hmR3VmxReportPinBasedCtlsMsr(&pVM->hm.s.ForR3.vmx.Msrs.PinCtls);
    hmR3VmxReportProcBasedCtlsMsr(&pVM->hm.s.ForR3.vmx.Msrs.ProcCtls);
    if (pVM->hm.s.ForR3.vmx.Msrs.ProcCtls.n.allowed1 & VMX_PROC_CTLS_USE_SECONDARY_CTLS)
        hmR3VmxReportProcBasedCtls2Msr(&pVM->hm.s.ForR3.vmx.Msrs.ProcCtls2);
    if (pVM->hm.s.ForR3.vmx.Msrs.ProcCtls.n.allowed1 & VMX_PROC_CTLS_USE_TERTIARY_CTLS)
        hmR3VmxReportProcBasedCtls3Msr(pVM->hm.s.ForR3.vmx.Msrs.u64ProcCtls3);

    hmR3VmxReportEntryCtlsMsr(&pVM->hm.s.ForR3.vmx.Msrs.EntryCtls);
    hmR3VmxReportExitCtlsMsr(&pVM->hm.s.ForR3.vmx.Msrs.ExitCtls);

    if (RT_BF_GET(pVM->hm.s.ForR3.vmx.Msrs.u64Basic, VMX_BF_BASIC_TRUE_CTLS))
    {
        /* We don't extensively dump the true capability MSRs as we don't use them, see @bugref{9180#c5}. */
        LogRel(("HM: MSR_IA32_VMX_TRUE_PINBASED_CTLS   = %#RX64\n", pVM->hm.s.ForR3.vmx.Msrs.TruePinCtls));
        LogRel(("HM: MSR_IA32_VMX_TRUE_PROCBASED_CTLS  = %#RX64\n", pVM->hm.s.ForR3.vmx.Msrs.TrueProcCtls));
        LogRel(("HM: MSR_IA32_VMX_TRUE_ENTRY_CTLS      = %#RX64\n", pVM->hm.s.ForR3.vmx.Msrs.TrueEntryCtls));
        LogRel(("HM: MSR_IA32_VMX_TRUE_EXIT_CTLS       = %#RX64\n", pVM->hm.s.ForR3.vmx.Msrs.TrueExitCtls));
    }

    hmR3VmxReportMiscMsr(pVM, pVM->hm.s.ForR3.vmx.Msrs.u64Misc);
    hmR3VmxReportVmcsEnumMsr(pVM->hm.s.ForR3.vmx.Msrs.u64VmcsEnum);
    if (pVM->hm.s.ForR3.vmx.Msrs.u64EptVpidCaps)
        hmR3VmxReportEptVpidCapsMsr(pVM->hm.s.ForR3.vmx.Msrs.u64EptVpidCaps);
    if (pVM->hm.s.ForR3.vmx.Msrs.u64VmFunc)
        hmR3VmxReportVmFuncMsr(pVM->hm.s.ForR3.vmx.Msrs.u64VmFunc);
    hmR3VmxReportCrFixedMsrs(&pVM->hm.s.ForR3.vmx.Msrs);

#ifdef TODO_9217_VMCSINFO
    LogRel(("HM: APIC-access page physaddr         = %#RHp\n",  pVM->hm.s.vmx.HCPhysApicAccess));
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PCVMXVMCSINFOSHARED pVmcsInfo = &pVM->apCpusR3[idCpu]->hm.s.vmx.VmcsInfo;
        LogRel(("HM: VCPU%3d: MSR bitmap physaddr      = %#RHp\n", idCpu, pVmcsInfo->HCPhysMsrBitmap));
        LogRel(("HM: VCPU%3d: VMCS physaddr            = %#RHp\n", idCpu, pVmcsInfo->HCPhysVmcs));
    }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (pVM->cpum.ro.GuestFeatures.fVmx)
    {
        LogRel(("HM: Nested-guest:\n"));
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PCVMXVMCSINFOSHARED pVmcsInfoNstGst = &pVM->apCpusR3[idCpu]->hm.s.vmx.VmcsInfoNstGst;
            LogRel(("HM: VCPU%3d: MSR bitmap physaddr      = %#RHp\n", idCpu, pVmcsInfoNstGst->HCPhysMsrBitmap));
            LogRel(("HM: VCPU%3d: VMCS physaddr            = %#RHp\n", idCpu, pVmcsInfoNstGst->HCPhysVmcs));
        }
    }
#endif
#endif /* TODO_9217_VMCSINFO */

    /*
     * EPT and unrestricted guest execution are determined in HMR3Init, verify the sanity of that.
     */
    AssertLogRelReturn(   !pVM->hm.s.fNestedPagingCfg
                       || (pVM->hm.s.ForR3.vmx.Msrs.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_EPT),
                       VERR_HM_IPE_1);
    AssertLogRelReturn(   !pVM->hm.s.vmx.fUnrestrictedGuestCfg
                       || (   (pVM->hm.s.ForR3.vmx.Msrs.ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST)
                           && pVM->hm.s.fNestedPagingCfg),
                       VERR_HM_IPE_1);

    /*
     * Disallow RDTSCP in the guest if there is no secondary process-based VM execution controls as otherwise
     * RDTSCP would cause a #UD. There might be no CPUs out there where this happens, as RDTSCP was introduced
     * in Nehalems and secondary VM exec. controls should be supported in all of them, but nonetheless it's Intel...
     */
    if (   !(pVM->hm.s.ForR3.vmx.Msrs.ProcCtls.n.allowed1 & VMX_PROC_CTLS_USE_SECONDARY_CTLS)
        && CPUMR3GetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_RDTSCP))
    {
        CPUMR3ClearGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_RDTSCP);
        LogRel(("HM: Disabled RDTSCP\n"));
    }

    if (!pVM->hm.s.vmx.fUnrestrictedGuestCfg)
    {
        /* Allocate three pages for the TSS we need for real mode emulation. (2 pages for the IO bitmap) */
        rc = PDMR3VmmDevHeapAlloc(pVM, HM_VTX_TOTAL_DEVHEAP_MEM, hmR3VmmDevHeapNotify, (RTR3PTR *)&pVM->hm.s.vmx.pRealModeTSS);
        if (RT_SUCCESS(rc))
        {
            /* The IO bitmap starts right after the virtual interrupt redirection bitmap.
               Refer Intel spec. 20.3.3 "Software Interrupt Handling in Virtual-8086 mode"
               esp. Figure 20-5.*/
            RT_BZERO(pVM->hm.s.vmx.pRealModeTSS, sizeof(*pVM->hm.s.vmx.pRealModeTSS));
            pVM->hm.s.vmx.pRealModeTSS->offIoBitmap = sizeof(*pVM->hm.s.vmx.pRealModeTSS);

            /* Bit set to 0 means software interrupts are redirected to the
               8086 program interrupt handler rather than switching to
               protected-mode handler. */
            memset(pVM->hm.s.vmx.pRealModeTSS->IntRedirBitmap, 0, sizeof(pVM->hm.s.vmx.pRealModeTSS->IntRedirBitmap));

            /* Allow all port IO, so that port IO instructions do not cause
               exceptions and would instead cause a VM-exit (based on VT-x's
               IO bitmap which we currently configure to always cause an exit). */
            memset(pVM->hm.s.vmx.pRealModeTSS + 1, 0, X86_PAGE_SIZE * 2);
            *((unsigned char *)pVM->hm.s.vmx.pRealModeTSS + HM_VTX_TSS_SIZE - 2) = 0xff;

            /*
             * Construct a 1024 element page directory with 4 MB pages for the identity mapped
             * page table used in real and protected mode without paging with EPT.
             */
            pVM->hm.s.vmx.pNonPagingModeEPTPageTable = (PX86PD)((char *)pVM->hm.s.vmx.pRealModeTSS + X86_PAGE_SIZE * 3);
            for (uint32_t i = 0; i < X86_PG_ENTRIES; i++)
            {
                pVM->hm.s.vmx.pNonPagingModeEPTPageTable->a[i].u  = _4M * i;
                pVM->hm.s.vmx.pNonPagingModeEPTPageTable->a[i].u |= X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US
                                                                 |  X86_PDE4M_A | X86_PDE4M_D | X86_PDE4M_PS
                                                                 |  X86_PDE4M_G;
            }

            /* We convert it here every time as PCI regions could be reconfigured. */
            if (PDMVmmDevHeapIsEnabled(pVM))
            {
                RTGCPHYS GCPhys;
                rc = PDMVmmDevHeapR3ToGCPhys(pVM, pVM->hm.s.vmx.pRealModeTSS, &GCPhys);
                AssertRCReturn(rc, rc);
                LogRel(("HM: Real Mode TSS guest physaddr      = %#RGp\n", GCPhys));

                rc = PDMVmmDevHeapR3ToGCPhys(pVM, pVM->hm.s.vmx.pNonPagingModeEPTPageTable, &GCPhys);
                AssertRCReturn(rc, rc);
                LogRel(("HM: Non-Paging Mode EPT CR3           = %#RGp\n", GCPhys));
            }
        }
        else
        {
            LogRel(("HM: No real mode VT-x support (PDMR3VMMDevHeapAlloc returned %Rrc)\n", rc));
            pVM->hm.s.vmx.pRealModeTSS = NULL;
            pVM->hm.s.vmx.pNonPagingModeEPTPageTable = NULL;
            return VMSetError(pVM, rc, RT_SRC_POS,
                              "HM failure: No real mode VT-x support (PDMR3VMMDevHeapAlloc returned %Rrc)", rc);
        }
    }

    LogRel((pVM->hm.s.fAllow64BitGuestsCfg ? "HM: Guest support: 32-bit and 64-bit\n"
                                           : "HM: Guest support: 32-bit only\n"));

    /*
     * Call ring-0 to set up the VM.
     */
    rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), 0 /* idCpu */, VMMR0_DO_HM_SETUP_VM, 0 /* u64Arg */, NULL /* pReqHdr */);
    if (rc != VINF_SUCCESS)
    {
        LogRel(("HM: VMX setup failed with rc=%Rrc!\n", rc));
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[idCpu];
            LogRel(("HM: CPU[%u] Last instruction error  %#x\n", idCpu, pVCpu->hm.s.vmx.LastError.u32InstrError));
            LogRel(("HM: CPU[%u] HM error                %#x (%u)\n", idCpu, pVCpu->hm.s.u32HMError, pVCpu->hm.s.u32HMError));
        }
        HMR3CheckError(pVM, rc);
        return VMSetError(pVM, rc, RT_SRC_POS, "VT-x setup failed: %Rrc", rc);
    }

    LogRel(("HM: Supports VMCS EFER fields         = %RTbool\n", pVM->hm.s.ForR3.vmx.fSupportsVmcsEfer));
    LogRel(("HM: Enabled VMX\n"));
    pVM->hm.s.vmx.fEnabled = true;

    hmR3DisableRawMode(pVM); /** @todo make this go away! */

    /*
     * Log configuration details.
     */
    if (pVM->hm.s.fNestedPagingCfg)
    {
        LogRel(("HM: Enabled nested paging\n"));
        if (pVM->hm.s.ForR3.vmx.enmTlbFlushEpt == VMXTLBFLUSHEPT_SINGLE_CONTEXT)
            LogRel(("HM:   EPT flush type                  = Single context\n"));
        else if (pVM->hm.s.ForR3.vmx.enmTlbFlushEpt == VMXTLBFLUSHEPT_ALL_CONTEXTS)
            LogRel(("HM:   EPT flush type                  = All contexts\n"));
        else if (pVM->hm.s.ForR3.vmx.enmTlbFlushEpt == VMXTLBFLUSHEPT_NOT_SUPPORTED)
            LogRel(("HM:   EPT flush type                  = Not supported\n"));
        else
            LogRel(("HM:   EPT flush type                  = %#x\n", pVM->hm.s.ForR3.vmx.enmTlbFlushEpt));

        if (pVM->hm.s.vmx.fUnrestrictedGuestCfg)
            LogRel(("HM: Enabled unrestricted guest execution\n"));

        if (pVM->hm.s.fLargePages)
        {
            /* Use large (2 MB) pages for our EPT PDEs where possible. */
            PGMSetLargePageUsage(pVM, true);
            LogRel(("HM: Enabled large page support\n"));
        }
    }
    else
        Assert(!pVM->hm.s.vmx.fUnrestrictedGuestCfg);

    if (pVM->hm.s.ForR3.vmx.fVpid)
    {
        LogRel(("HM: Enabled VPID\n"));
        if (pVM->hm.s.ForR3.vmx.enmTlbFlushVpid == VMXTLBFLUSHVPID_INDIV_ADDR)
            LogRel(("HM:   VPID flush type                 = Individual addresses\n"));
        else if (pVM->hm.s.ForR3.vmx.enmTlbFlushVpid == VMXTLBFLUSHVPID_SINGLE_CONTEXT)
            LogRel(("HM:   VPID flush type                 = Single context\n"));
        else if (pVM->hm.s.ForR3.vmx.enmTlbFlushVpid == VMXTLBFLUSHVPID_ALL_CONTEXTS)
            LogRel(("HM:   VPID flush type                 = All contexts\n"));
        else if (pVM->hm.s.ForR3.vmx.enmTlbFlushVpid == VMXTLBFLUSHVPID_SINGLE_CONTEXT_RETAIN_GLOBALS)
            LogRel(("HM:   VPID flush type                 = Single context retain globals\n"));
        else
            LogRel(("HM:   VPID flush type                 = %#x\n", pVM->hm.s.ForR3.vmx.enmTlbFlushVpid));
    }
    else if (pVM->hm.s.ForR3.vmx.enmTlbFlushVpid == VMXTLBFLUSHVPID_NOT_SUPPORTED)
        LogRel(("HM: Ignoring VPID capabilities of CPU\n"));

    if (pVM->hm.s.vmx.fUsePreemptTimerCfg)
        LogRel(("HM: Enabled VMX-preemption timer (cPreemptTimerShift=%u)\n", pVM->hm.s.vmx.cPreemptTimerShift));
    else
        LogRel(("HM: Disabled VMX-preemption timer\n"));

    if (pVM->hm.s.fVirtApicRegs)
        LogRel(("HM: Enabled APIC-register virtualization support\n"));

    if (pVM->hm.s.fPostedIntrs)
        LogRel(("HM: Enabled posted-interrupt processing support\n"));

    if (pVM->hm.s.ForR3.vmx.fUseVmcsShadowing)
    {
        bool const fFullVmcsShadow = RT_BOOL(pVM->hm.s.ForR3.vmx.Msrs.u64Misc & VMX_MISC_VMWRITE_ALL);
        LogRel(("HM: Enabled %s VMCS shadowing\n", fFullVmcsShadow ? "full" : "partial"));
    }

    return VINF_SUCCESS;
}


/**
 * Finish AMD-V initialization (after ring-0 init).
 *
 * @returns VBox status code.
 * @param   pVM                The cross context VM structure.
 */
static int hmR3InitFinalizeR0Amd(PVM pVM)
{
    LogFunc(("pVM->hm.s.svm.fSupported = %d\n", pVM->hm.s.svm.fSupported));

    LogRel(("HM: Using AMD-V implementation 2.0\n"));

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint32_t u32Family;
    uint32_t u32Model;
    uint32_t u32Stepping;
    if (HMIsSubjectToSvmErratum170(&u32Family, &u32Model, &u32Stepping))
        LogRel(("HM: AMD Cpu with erratum 170 family %#x model %#x stepping %#x\n", u32Family, u32Model, u32Stepping));
#endif
    LogRel(("HM: Max resume loops                  = %u\n",     pVM->hm.s.cMaxResumeLoopsCfg));
    LogRel(("HM: AMD HWCR MSR                      = %#RX64\n", pVM->hm.s.ForR3.svm.u64MsrHwcr));
    LogRel(("HM: AMD-V revision                    = %#x\n",    pVM->hm.s.ForR3.svm.u32Rev));
    LogRel(("HM: AMD-V max ASID                    = %RU32\n",  pVM->hm.s.ForR3.uMaxAsid));
    LogRel(("HM: AMD-V features                    = %#x\n",    pVM->hm.s.ForR3.svm.fFeatures));

    /*
     * Enumerate AMD-V features.
     */
    static const struct { uint32_t fFlag; const char *pszName; } s_aSvmFeatures[] =
    {
#define HMSVM_REPORT_FEATURE(a_StrDesc, a_Define) { a_Define, a_StrDesc }
        HMSVM_REPORT_FEATURE("NESTED_PAGING",           X86_CPUID_SVM_FEATURE_EDX_NESTED_PAGING),
        HMSVM_REPORT_FEATURE("LBR_VIRT",                X86_CPUID_SVM_FEATURE_EDX_LBR_VIRT),
        HMSVM_REPORT_FEATURE("SVM_LOCK",                X86_CPUID_SVM_FEATURE_EDX_SVM_LOCK),
        HMSVM_REPORT_FEATURE("NRIP_SAVE",               X86_CPUID_SVM_FEATURE_EDX_NRIP_SAVE),
        HMSVM_REPORT_FEATURE("TSC_RATE_MSR",            X86_CPUID_SVM_FEATURE_EDX_TSC_RATE_MSR),
        HMSVM_REPORT_FEATURE("VMCB_CLEAN",              X86_CPUID_SVM_FEATURE_EDX_VMCB_CLEAN),
        HMSVM_REPORT_FEATURE("FLUSH_BY_ASID",           X86_CPUID_SVM_FEATURE_EDX_FLUSH_BY_ASID),
        HMSVM_REPORT_FEATURE("DECODE_ASSISTS",          X86_CPUID_SVM_FEATURE_EDX_DECODE_ASSISTS),
        HMSVM_REPORT_FEATURE("PAUSE_FILTER",            X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER),
        HMSVM_REPORT_FEATURE("PAUSE_FILTER_THRESHOLD",  X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER_THRESHOLD),
        HMSVM_REPORT_FEATURE("AVIC",                    X86_CPUID_SVM_FEATURE_EDX_AVIC),
        HMSVM_REPORT_FEATURE("VIRT_VMSAVE_VMLOAD",      X86_CPUID_SVM_FEATURE_EDX_VIRT_VMSAVE_VMLOAD),
        HMSVM_REPORT_FEATURE("VGIF",                    X86_CPUID_SVM_FEATURE_EDX_VGIF),
        HMSVM_REPORT_FEATURE("GMET",                    X86_CPUID_SVM_FEATURE_EDX_GMET),
        HMSVM_REPORT_FEATURE("X2AVIC",                  X86_CPUID_SVM_FEATURE_EDX_X2AVIC),
        HMSVM_REPORT_FEATURE("SSSCHECK",                X86_CPUID_SVM_FEATURE_EDX_SSSCHECK),
        HMSVM_REPORT_FEATURE("SPEC_CTRL",               X86_CPUID_SVM_FEATURE_EDX_SPEC_CTRL),
        HMSVM_REPORT_FEATURE("ROGPT",                   X86_CPUID_SVM_FEATURE_EDX_ROGPT),
        HMSVM_REPORT_FEATURE("HOST_MCE_OVERRIDE",       X86_CPUID_SVM_FEATURE_EDX_HOST_MCE_OVERRIDE),
        HMSVM_REPORT_FEATURE("TLBICTL",                 X86_CPUID_SVM_FEATURE_EDX_TLBICTL),
        HMSVM_REPORT_FEATURE("VNMI",                    X86_CPUID_SVM_FEATURE_EDX_VNMI),
        HMSVM_REPORT_FEATURE("IBS_VIRT",                X86_CPUID_SVM_FEATURE_EDX_IBS_VIRT),
        HMSVM_REPORT_FEATURE("EXT_LVT_AVIC_ACCESS_CHG", X86_CPUID_SVM_FEATURE_EDX_EXT_LVT_AVIC_ACCESS_CHG),
        HMSVM_REPORT_FEATURE("NST_VIRT_VMCB_ADDR_CHK",  X86_CPUID_SVM_FEATURE_EDX_NST_VIRT_VMCB_ADDR_CHK),
        HMSVM_REPORT_FEATURE("BUS_LOCK_THRESHOLD",      X86_CPUID_SVM_FEATURE_EDX_BUS_LOCK_THRESHOLD),
#undef HMSVM_REPORT_FEATURE
    };

    uint32_t fSvmFeatures = pVM->hm.s.ForR3.svm.fFeatures;
    for (unsigned i = 0; i < RT_ELEMENTS(s_aSvmFeatures); i++)
        if (fSvmFeatures & s_aSvmFeatures[i].fFlag)
        {
            LogRel(("HM:   %s\n", s_aSvmFeatures[i].pszName));
            fSvmFeatures &= ~s_aSvmFeatures[i].fFlag;
        }
    if (fSvmFeatures)
        for (unsigned iBit = 0; iBit < 32; iBit++)
            if (RT_BIT_32(iBit) & fSvmFeatures)
                LogRel(("HM:   Reserved bit %u\n", iBit));

    /*
     * Nested paging is determined in HMR3Init, verify the sanity of that.
     */
    AssertLogRelReturn(   !pVM->hm.s.fNestedPagingCfg
                       || (pVM->hm.s.ForR3.svm.fFeatures & X86_CPUID_SVM_FEATURE_EDX_NESTED_PAGING),
                       VERR_HM_IPE_1);

#if 0
    /** @todo Add and query IPRT API for host OS support for posted-interrupt IPI
     *        here. */
    if (RTR0IsPostIpiSupport())
        pVM->hm.s.fPostedIntrs = true;
#endif

    /*
     * Determine whether we need to intercept #UD in SVM mode for emulating
     * intel SYSENTER/SYSEXIT on AMD64, as these instructions results in #UD
     * when executed in long-mode.  This is only really applicable when
     * non-default CPU profiles are in effect, i.e. guest vendor differs
     * from the host one.
     */
    if (CPUMGetGuestCpuVendor(pVM) != CPUMGetHostCpuVendor(pVM))
        switch (CPUMGetGuestCpuVendor(pVM))
        {
            case CPUMCPUVENDOR_INTEL:
            case CPUMCPUVENDOR_VIA: /*?*/
            case CPUMCPUVENDOR_SHANGHAI: /*?*/
            {
                switch (CPUMGetHostCpuVendor(pVM))
                {
                    case CPUMCPUVENDOR_AMD:
                    case CPUMCPUVENDOR_HYGON:
                    {
                        if (pVM->hm.s.fAllow64BitGuestsCfg)
                        {
                            LogRel(("HM: Intercepting #UD for emulating SYSENTER/SYSEXIT in long mode.\n"));
                            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                                pVM->apCpusR3[idCpu]->hm.s.svm.fEmulateLongModeSysEnterExit = true;
                        }
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }

    /*
     * Call ring-0 to set up the VM.
     */
    int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), 0 /*idCpu*/, VMMR0_DO_HM_SETUP_VM, 0, NULL);
    if (rc != VINF_SUCCESS)
    {
        AssertMsgFailed(("%Rrc\n", rc));
        LogRel(("HM: AMD-V setup failed with rc=%Rrc!\n", rc));
        return VMSetError(pVM, rc, RT_SRC_POS, "AMD-V setup failed: %Rrc", rc);
    }

    LogRel(("HM: Enabled SVM\n"));
    pVM->hm.s.svm.fEnabled = true;

    if (pVM->hm.s.fNestedPagingCfg)
    {
        LogRel(("HM:   Enabled nested paging\n"));

        /*
         * Enable large pages (2 MB) if applicable.
         */
        if (pVM->hm.s.fLargePages)
        {
            PGMSetLargePageUsage(pVM, true);
            LogRel(("HM:   Enabled large page support\n"));
        }
    }

    if (pVM->hm.s.fVirtApicRegs)
        LogRel(("HM:   Enabled APIC-register virtualization support\n"));

    if (pVM->hm.s.fPostedIntrs)
        LogRel(("HM:   Enabled posted-interrupt processing support\n"));

    hmR3DisableRawMode(pVM);

    LogRel((pVM->hm.s.fTprPatchingAllowed ? "HM: Enabled TPR patching\n"
                                          : "HM: Disabled TPR patching\n"));

    LogRel((pVM->hm.s.fAllow64BitGuestsCfg ? "HM: Guest support: 32-bit and 64-bit\n"
                                           : "HM: Guest support: 32-bit only\n"));
    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) HMR3Relocate(PVM pVM)
{
    /* Fetch the current paging mode during the relocate callback during state loading. */
    if (VMR3GetState(pVM) == VMSTATE_LOADING)
    {
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[idCpu];
            pVCpu->hm.s.enmShadowMode = PGMGetShadowMode(pVCpu);
        }
    }
}


/**
 * Terminates the HM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM itself is, at this point, powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) HMR3Term(PVM pVM)
{
    if (pVM->hm.s.vmx.pRealModeTSS)
    {
        PDMR3VmmDevHeapFree(pVM, pVM->hm.s.vmx.pRealModeTSS);
        pVM->hm.s.vmx.pRealModeTSS       = 0;
    }
    hmR3TermCPU(pVM);
    return 0;
}


/**
 * Terminates the per-VCPU HM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int hmR3TermCPU(PVM pVM)
{
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Resets a virtual CPU.
 *
 * Used by HMR3Reset and CPU hot plugging.
 *
 * @param   pVCpu   The cross context virtual CPU structure to reset.
 */
VMMR3_INT_DECL(void) HMR3ResetCpu(PVMCPU pVCpu)
{
    /* Sync. entire state on VM reset ring-0 re-entry. It's safe to reset
       the HM flags here, all other EMTs are in ring-3. See VMR3Reset(). */
    pVCpu->hm.s.fCtxChanged |= HM_CHANGED_HOST_CONTEXT | HM_CHANGED_ALL_GUEST;

    pVCpu->hm.s.fActive                        = false;
    pVCpu->hm.s.Event.fPending                 = false;
    pVCpu->hm.s.vmx.u64GstMsrApicBase          = 0;
    pVCpu->hm.s.vmx.VmcsInfo.fWasInRealMode    = true;
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fVmx)
        pVCpu->hm.s.vmx.VmcsInfoNstGst.fWasInRealMode    = true;
#endif
}


/**
 * The VM is being reset.
 *
 * For the HM component this means that any GDT/LDT/TSS monitors
 * needs to be removed.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) HMR3Reset(PVM pVM)
{
    LogFlow(("HMR3Reset:\n"));

    if (HMIsEnabled(pVM))
        hmR3DisableRawMode(pVM);

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        HMR3ResetCpu(pVM->apCpusR3[idCpu]);

    /* Clear all patch information. */
    pVM->hm.s.pGuestPatchMem     = 0;
    pVM->hm.s.pFreeGuestPatchMem = 0;
    pVM->hm.s.cbGuestPatchMem    = 0;
    pVM->hm.s.cPatches           = 0;
    pVM->hm.s.PatchTree          = 0;
    pVM->hm.s.fTprPatchingActive = false;
    RT_BZERO(pVM->hm.s.aPatches, sizeof(pVM->hm.s.aPatches));
}


/**
 * Callback to patch a TPR instruction (vmmcall or mov cr8).
 *
 * @returns VBox strict status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser  Unused.
 */
static DECLCALLBACK(VBOXSTRICTRC) hmR3RemovePatches(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    VMCPUID idCpu = (VMCPUID)(uintptr_t)pvUser;

    /* Only execute the handler on the VCPU the original patch request was issued. */
    if (pVCpu->idCpu != idCpu)
        return VINF_SUCCESS;

    Log(("hmR3RemovePatches\n"));
    for (unsigned i = 0; i < pVM->hm.s.cPatches; i++)
    {
        uint8_t         abInstr[15];
        PHMTPRPATCH pPatch = &pVM->hm.s.aPatches[i];
        RTGCPTR         pInstrGC = (RTGCPTR)pPatch->Core.Key;
        int             rc;

#ifdef LOG_ENABLED
        char            szOutput[256];
        rc = DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, CPUMGetGuestCS(pVCpu), pInstrGC, DBGF_DISAS_FLAGS_DEFAULT_MODE,
                                szOutput, sizeof(szOutput), NULL);
        if (RT_SUCCESS(rc))
            Log(("Patched instr: %s\n", szOutput));
#endif

        /* Check if the instruction is still the same. */
        rc = PGMPhysSimpleReadGCPtr(pVCpu, abInstr, pInstrGC, pPatch->cbNewOp);
        if (rc != VINF_SUCCESS)
        {
            Log(("Patched code removed? (rc=%Rrc0\n", rc));
            continue;   /* swapped out or otherwise removed; skip it. */
        }

        if (memcmp(abInstr, pPatch->aNewOpcode, pPatch->cbNewOp))
        {
            Log(("Patched instruction was changed! (rc=%Rrc0\n", rc));
            continue;   /* skip it. */
        }

        rc = PGMPhysSimpleWriteGCPtr(pVCpu, pInstrGC, pPatch->aOpcode, pPatch->cbOp);
        AssertRC(rc);

#ifdef LOG_ENABLED
        rc = DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, CPUMGetGuestCS(pVCpu), pInstrGC, DBGF_DISAS_FLAGS_DEFAULT_MODE,
                                szOutput, sizeof(szOutput), NULL);
        if (RT_SUCCESS(rc))
            Log(("Original instr: %s\n", szOutput));
#endif
    }
    pVM->hm.s.cPatches           = 0;
    pVM->hm.s.PatchTree          = 0;
    pVM->hm.s.pFreeGuestPatchMem = pVM->hm.s.pGuestPatchMem;
    pVM->hm.s.fTprPatchingActive = false;
    return VINF_SUCCESS;
}


/**
 * Worker for enabling patching in a VT-x/AMD-V guest.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       VCPU to execute hmR3RemovePatches on.
 * @param   pPatchMem   Patch memory range.
 * @param   cbPatchMem  Size of the memory range.
 */
static DECLCALLBACK(int) hmR3EnablePatching(PVM pVM, VMCPUID idCpu, RTRCPTR pPatchMem, unsigned cbPatchMem)
{
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE, hmR3RemovePatches, (void *)(uintptr_t)idCpu);
    AssertRC(rc);

    pVM->hm.s.pGuestPatchMem      = pPatchMem;
    pVM->hm.s.pFreeGuestPatchMem  = pPatchMem;
    pVM->hm.s.cbGuestPatchMem     = cbPatchMem;
    return VINF_SUCCESS;
}


/**
 * Enable patching in a VT-x/AMD-V guest
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pPatchMem   Patch memory range.
 * @param   cbPatchMem  Size of the memory range.
 */
VMMR3_INT_DECL(int)  HMR3EnablePatching(PVM pVM, RTGCPTR pPatchMem, unsigned cbPatchMem)
{
    VM_ASSERT_EMT(pVM);
    Log(("HMR3EnablePatching %RGv size %x\n", pPatchMem, cbPatchMem));
    if (pVM->cCpus > 1)
    {
        /* We own the IOM lock here and could cause a deadlock by waiting for a VCPU that is blocking on the IOM lock. */
        int rc = VMR3ReqCallNoWait(pVM, VMCPUID_ANY_QUEUE,
                                   (PFNRT)hmR3EnablePatching, 4, pVM, VMMGetCpuId(pVM), (RTRCPTR)pPatchMem, cbPatchMem);
        AssertRC(rc);
        return rc;
    }
    return hmR3EnablePatching(pVM, VMMGetCpuId(pVM), (RTRCPTR)pPatchMem, cbPatchMem);
}


/**
 * Disable patching in a VT-x/AMD-V guest.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pPatchMem   Patch memory range.
 * @param   cbPatchMem  Size of the memory range.
 */
VMMR3_INT_DECL(int)  HMR3DisablePatching(PVM pVM, RTGCPTR pPatchMem, unsigned cbPatchMem)
{
    Log(("HMR3DisablePatching %RGv size %x\n", pPatchMem, cbPatchMem));
    RT_NOREF2(pPatchMem, cbPatchMem);

    Assert(pVM->hm.s.pGuestPatchMem == pPatchMem);
    Assert(pVM->hm.s.cbGuestPatchMem == cbPatchMem);

    /** @todo Potential deadlock when other VCPUs are waiting on the IOM lock (we own it)!! */
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE, hmR3RemovePatches,
                                (void *)(uintptr_t)VMMGetCpuId(pVM));
    AssertRC(rc);

    pVM->hm.s.pGuestPatchMem      = 0;
    pVM->hm.s.pFreeGuestPatchMem  = 0;
    pVM->hm.s.cbGuestPatchMem     = 0;
    pVM->hm.s.fTprPatchingActive  = false;
    return VINF_SUCCESS;
}


/**
 * Callback to patch a TPR instruction (vmmcall or mov cr8).
 *
 * @returns VBox strict status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser  User specified CPU context.
 *
 */
static DECLCALLBACK(VBOXSTRICTRC) hmR3ReplaceTprInstr(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    /*
     * Only execute the handler on the VCPU the original patch request was
     * issued. (The other CPU(s) might not yet have switched to protected
     * mode, nor have the correct memory context.)
     */
    VMCPUID idCpu = (VMCPUID)(uintptr_t)pvUser;
    if (pVCpu->idCpu != idCpu)
        return VINF_SUCCESS;

    /*
     * We're racing other VCPUs here, so don't try patch the instruction twice
     * and make sure there is still room for our patch record.
     */
    PCPUMCTX    pCtx   = &pVCpu->cpum.GstCtx;
    PHMTPRPATCH pPatch = (PHMTPRPATCH)RTAvloU32Get(&pVM->hm.s.PatchTree, (AVLOU32KEY)pCtx->eip);
    if (pPatch)
    {
        Log(("hmR3ReplaceTprInstr: already patched %RGv\n", pCtx->rip));
        return VINF_SUCCESS;
    }
    uint32_t const idx = pVM->hm.s.cPatches;
    if (idx >= RT_ELEMENTS(pVM->hm.s.aPatches))
    {
        Log(("hmR3ReplaceTprInstr: no available patch slots (%RGv)\n", pCtx->rip));
        return VINF_SUCCESS;
    }
    pPatch = &pVM->hm.s.aPatches[idx];

    Log(("hmR3ReplaceTprInstr: rip=%RGv idxPatch=%u\n", pCtx->rip, idx));

    /*
     * Disassembler the instruction and get cracking.
     */
    DBGFR3_DISAS_INSTR_CUR_LOG(pVCpu, "hmR3ReplaceTprInstr");
    DISSTATE        Dis;
    uint32_t        cbOp;
    int rc = EMInterpretDisasCurrent(pVCpu, &Dis, &cbOp);
    AssertRC(rc);
    if (    rc == VINF_SUCCESS
        &&  Dis.pCurInstr->uOpcode == OP_MOV
        &&  cbOp >= 3)
    {
        static uint8_t const s_abVMMCall[3] = { 0x0f, 0x01, 0xd9 };

        rc = PGMPhysSimpleReadGCPtr(pVCpu, pPatch->aOpcode, pCtx->rip, cbOp);
        AssertRC(rc);

        pPatch->cbOp = cbOp;

        if (Dis.aParams[0].fUse == DISUSE_DISPLACEMENT32)
        {
            /* write. */
            if (Dis.aParams[1].fUse == DISUSE_REG_GEN32)
            {
                pPatch->enmType     = HMTPRINSTR_WRITE_REG;
                pPatch->uSrcOperand = Dis.aParams[1].x86.Base.idxGenReg;
                Log(("hmR3ReplaceTprInstr: HMTPRINSTR_WRITE_REG %u\n", Dis.aParams[1].x86.Base.idxGenReg));
            }
            else
            {
                Assert(Dis.aParams[1].fUse == DISUSE_IMMEDIATE32);
                pPatch->enmType     = HMTPRINSTR_WRITE_IMM;
                pPatch->uSrcOperand = Dis.aParams[1].uValue;
                Log(("hmR3ReplaceTprInstr: HMTPRINSTR_WRITE_IMM %#llx\n", Dis.aParams[1].uValue));
            }
            rc = PGMPhysSimpleWriteGCPtr(pVCpu, pCtx->rip, s_abVMMCall, sizeof(s_abVMMCall));
            AssertRC(rc);

            memcpy(pPatch->aNewOpcode, s_abVMMCall, sizeof(s_abVMMCall));
            pPatch->cbNewOp = sizeof(s_abVMMCall);
            STAM_COUNTER_INC(&pVM->hm.s.StatTprReplaceSuccessVmc);
        }
        else
        {
            /*
             * TPR Read.
             *
             * Found:
             *   mov eax, dword [fffe0080]        (5 bytes)
             * Check if next instruction is:
             *   shr eax, 4
             */
            Assert(Dis.aParams[0].fUse == DISUSE_REG_GEN32);

            uint8_t  const idxMmioReg = Dis.aParams[0].x86.Base.idxGenReg;
            uint8_t  const cbOpMmio   = cbOp;
            uint64_t const uSavedRip  = pCtx->rip;

            pCtx->rip += cbOp;
            rc = EMInterpretDisasCurrent(pVCpu, &Dis, &cbOp);
            DBGFR3_DISAS_INSTR_CUR_LOG(pVCpu, "Following read");
            pCtx->rip = uSavedRip;

            if (    rc == VINF_SUCCESS
                &&  Dis.pCurInstr->uOpcode == OP_SHR
                &&  Dis.aParams[0].fUse == DISUSE_REG_GEN32
                &&  Dis.aParams[0].x86.Base.idxGenReg == idxMmioReg
                &&  Dis.aParams[1].fUse == DISUSE_IMMEDIATE8
                &&  Dis.aParams[1].uValue == 4
                &&  cbOpMmio + cbOp < sizeof(pVM->hm.s.aPatches[idx].aOpcode))
            {
                uint8_t abInstr[15];

                /* Replacing the two instructions above with an AMD-V specific lock-prefixed 32-bit MOV CR8 instruction so as to
                   access CR8 in 32-bit mode and not cause a #VMEXIT. */
                rc = PGMPhysSimpleReadGCPtr(pVCpu, &pPatch->aOpcode, pCtx->rip, cbOpMmio + cbOp);
                AssertRC(rc);

                pPatch->cbOp = cbOpMmio + cbOp;

                /* 0xf0, 0x0f, 0x20, 0xc0 = mov eax, cr8 */
                abInstr[0] = 0xf0;
                abInstr[1] = 0x0f;
                abInstr[2] = 0x20;
                abInstr[3] = 0xc0 | Dis.aParams[0].x86.Base.idxGenReg;
                for (unsigned i = 4; i < pPatch->cbOp; i++)
                    abInstr[i] = 0x90;  /* nop */

                rc = PGMPhysSimpleWriteGCPtr(pVCpu, pCtx->rip, abInstr, pPatch->cbOp);
                AssertRC(rc);

                memcpy(pPatch->aNewOpcode, abInstr, pPatch->cbOp);
                pPatch->cbNewOp = pPatch->cbOp;
                STAM_COUNTER_INC(&pVM->hm.s.StatTprReplaceSuccessCr8);

                Log(("Acceptable read/shr candidate!\n"));
                pPatch->enmType = HMTPRINSTR_READ_SHR4;
            }
            else
            {
                pPatch->enmType     = HMTPRINSTR_READ;
                pPatch->uDstOperand = idxMmioReg;

                rc = PGMPhysSimpleWriteGCPtr(pVCpu, pCtx->rip, s_abVMMCall, sizeof(s_abVMMCall));
                AssertRC(rc);

                memcpy(pPatch->aNewOpcode, s_abVMMCall, sizeof(s_abVMMCall));
                pPatch->cbNewOp = sizeof(s_abVMMCall);
                STAM_COUNTER_INC(&pVM->hm.s.StatTprReplaceSuccessVmc);
                Log(("hmR3ReplaceTprInstr: HMTPRINSTR_READ %u\n", pPatch->uDstOperand));
            }
        }

        pPatch->Core.Key = pCtx->eip;
        rc = RTAvloU32Insert(&pVM->hm.s.PatchTree, &pPatch->Core);
        AssertRC(rc);

        pVM->hm.s.cPatches++;
        return VINF_SUCCESS;
    }

    /*
     * Save invalid patch, so we will not try again.
     */
    Log(("hmR3ReplaceTprInstr: Failed to patch instr!\n"));
    pPatch->Core.Key = pCtx->eip;
    pPatch->enmType  = HMTPRINSTR_INVALID;
    rc = RTAvloU32Insert(&pVM->hm.s.PatchTree, &pPatch->Core);
    AssertRC(rc);
    pVM->hm.s.cPatches++;
    STAM_COUNTER_INC(&pVM->hm.s.StatTprReplaceFailure);
    return VINF_SUCCESS;
}


/**
 * Callback to patch a TPR instruction (jump to generated code).
 *
 * @returns VBox strict status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser  User specified CPU context.
 *
 */
static DECLCALLBACK(VBOXSTRICTRC) hmR3PatchTprInstr(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    /*
     * Only execute the handler on the VCPU the original patch request was
     * issued. (The other CPU(s) might not yet have switched to protected
     * mode, nor have the correct memory context.)
     */
    VMCPUID         idCpu  = (VMCPUID)(uintptr_t)pvUser;
    if (pVCpu->idCpu != idCpu)
        return VINF_SUCCESS;

    /*
     * We're racing other VCPUs here, so don't try patch the instruction twice
     * and make sure there is still room for our patch record.
     */
    PCPUMCTX    pCtx   = &pVCpu->cpum.GstCtx;
    PHMTPRPATCH pPatch = (PHMTPRPATCH)RTAvloU32Get(&pVM->hm.s.PatchTree, (AVLOU32KEY)pCtx->eip);
    if (pPatch)
    {
        Log(("hmR3PatchTprInstr: already patched %RGv\n", pCtx->rip));
        return VINF_SUCCESS;
    }
    uint32_t const  idx = pVM->hm.s.cPatches;
    if (idx >= RT_ELEMENTS(pVM->hm.s.aPatches))
    {
        Log(("hmR3PatchTprInstr: no available patch slots (%RGv)\n", pCtx->rip));
        return VINF_SUCCESS;
    }
    pPatch = &pVM->hm.s.aPatches[idx];

    Log(("hmR3PatchTprInstr: rip=%RGv idxPatch=%u\n", pCtx->rip, idx));
    DBGFR3_DISAS_INSTR_CUR_LOG(pVCpu, "hmR3PatchTprInstr");

    /*
     * Disassemble the instruction and get cracking.
     */
    DISSTATE        Dis;
    uint32_t        cbOp;
    int rc = EMInterpretDisasCurrent(pVCpu, &Dis, &cbOp);
    AssertRC(rc);
    if (    rc == VINF_SUCCESS
        &&  Dis.pCurInstr->uOpcode == OP_MOV
        &&  cbOp >= 5)
    {
        uint8_t         aPatch[64];
        uint32_t        off = 0;

        rc = PGMPhysSimpleReadGCPtr(pVCpu, pPatch->aOpcode, pCtx->rip, cbOp);
        AssertRC(rc);

        pPatch->cbOp    = cbOp;
        pPatch->enmType = HMTPRINSTR_JUMP_REPLACEMENT;

        if (Dis.aParams[0].fUse == DISUSE_DISPLACEMENT32)
        {
            /*
             * TPR write:
             *
             * push ECX                      [51]
             * push EDX                      [52]
             * push EAX                      [50]
             * xor EDX,EDX                   [31 D2]
             * mov EAX,EAX                   [89 C0]
             *  or
             * mov EAX,0000000CCh            [B8 CC 00 00 00]
             * mov ECX,0C0000082h            [B9 82 00 00 C0]
             * wrmsr                         [0F 30]
             * pop EAX                       [58]
             * pop EDX                       [5A]
             * pop ECX                       [59]
             * jmp return_address            [E9 return_address]
             */
            bool fUsesEax = (Dis.aParams[1].fUse == DISUSE_REG_GEN32 && Dis.aParams[1].x86.Base.idxGenReg == DISGREG_EAX);

            aPatch[off++] = 0x51;    /* push ecx */
            aPatch[off++] = 0x52;    /* push edx */
            if (!fUsesEax)
                aPatch[off++] = 0x50;    /* push eax */
            aPatch[off++] = 0x31;    /* xor edx, edx */
            aPatch[off++] = 0xd2;
            if (Dis.aParams[1].fUse == DISUSE_REG_GEN32)
            {
                if (!fUsesEax)
                {
                    aPatch[off++] = 0x89;    /* mov eax, src_reg */
                    aPatch[off++] = MAKE_MODRM(3, Dis.aParams[1].x86.Base.idxGenReg, DISGREG_EAX);
                }
            }
            else
            {
                Assert(Dis.aParams[1].fUse == DISUSE_IMMEDIATE32);
                aPatch[off++] = 0xb8;    /* mov eax, immediate */
                *(uint32_t *)&aPatch[off] = Dis.aParams[1].uValue;
                off += sizeof(uint32_t);
            }
            aPatch[off++] = 0xb9;    /* mov ecx, 0xc0000082 */
            *(uint32_t *)&aPatch[off] = MSR_K8_LSTAR;
            off += sizeof(uint32_t);

            aPatch[off++] = 0x0f;    /* wrmsr */
            aPatch[off++] = 0x30;
            if (!fUsesEax)
                aPatch[off++] = 0x58;    /* pop eax */
            aPatch[off++] = 0x5a;    /* pop edx */
            aPatch[off++] = 0x59;    /* pop ecx */
        }
        else
        {
            /*
             * TPR read:
             *
             * push ECX                      [51]
             * push EDX                      [52]
             * push EAX                      [50]
             * mov ECX,0C0000082h            [B9 82 00 00 C0]
             * rdmsr                         [0F 32]
             * mov EAX,EAX                   [89 C0]
             * pop EAX                       [58]
             * pop EDX                       [5A]
             * pop ECX                       [59]
             * jmp return_address            [E9 return_address]
             */
            Assert(Dis.aParams[0].fUse == DISUSE_REG_GEN32);

            if (Dis.aParams[0].x86.Base.idxGenReg != DISGREG_ECX)
                aPatch[off++] = 0x51;    /* push ecx */
            if (Dis.aParams[0].x86.Base.idxGenReg != DISGREG_EDX )
                aPatch[off++] = 0x52;    /* push edx */
            if (Dis.aParams[0].x86.Base.idxGenReg != DISGREG_EAX)
                aPatch[off++] = 0x50;    /* push eax */

            aPatch[off++] = 0x31;    /* xor edx, edx */
            aPatch[off++] = 0xd2;

            aPatch[off++] = 0xb9;    /* mov ecx, 0xc0000082 */
            *(uint32_t *)&aPatch[off] = MSR_K8_LSTAR;
            off += sizeof(uint32_t);

            aPatch[off++] = 0x0f;    /* rdmsr */
            aPatch[off++] = 0x32;

            if (Dis.aParams[0].x86.Base.idxGenReg != DISGREG_EAX)
            {
                aPatch[off++] = 0x89;    /* mov dst_reg, eax */
                aPatch[off++] = MAKE_MODRM(3, DISGREG_EAX, Dis.aParams[0].x86.Base.idxGenReg);
            }

            if (Dis.aParams[0].x86.Base.idxGenReg != DISGREG_EAX)
                aPatch[off++] = 0x58;    /* pop eax */
            if (Dis.aParams[0].x86.Base.idxGenReg != DISGREG_EDX )
                aPatch[off++] = 0x5a;    /* pop edx */
            if (Dis.aParams[0].x86.Base.idxGenReg != DISGREG_ECX)
                aPatch[off++] = 0x59;    /* pop ecx */
        }
        aPatch[off++] = 0xe9;    /* jmp return_address */
        *(RTRCUINTPTR *)&aPatch[off] = ((RTRCUINTPTR)pCtx->eip + cbOp) - ((RTRCUINTPTR)pVM->hm.s.pFreeGuestPatchMem + off + 4);
        off += sizeof(RTRCUINTPTR);

        if (pVM->hm.s.pFreeGuestPatchMem + off <= pVM->hm.s.pGuestPatchMem + pVM->hm.s.cbGuestPatchMem)
        {
            /* Write new code to the patch buffer. */
            rc = PGMPhysSimpleWriteGCPtr(pVCpu, pVM->hm.s.pFreeGuestPatchMem, aPatch, off);
            AssertRC(rc);

#ifdef LOG_ENABLED
            uint32_t cbCurInstr;
            for (RTGCPTR GCPtrInstr = pVM->hm.s.pFreeGuestPatchMem;
                 GCPtrInstr < pVM->hm.s.pFreeGuestPatchMem + off;
                 GCPtrInstr += RT_MAX(cbCurInstr, 1))
            {
                char     szOutput[256];
                rc = DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, pCtx->cs.Sel, GCPtrInstr, DBGF_DISAS_FLAGS_DEFAULT_MODE,
                                        szOutput, sizeof(szOutput), &cbCurInstr);
                if (RT_SUCCESS(rc))
                    Log(("Patch instr %s\n", szOutput));
                else
                    Log(("%RGv: rc=%Rrc\n", GCPtrInstr, rc));
            }
#endif

            pPatch->aNewOpcode[0] = 0xE9;
            *(RTRCUINTPTR *)&pPatch->aNewOpcode[1] = ((RTRCUINTPTR)pVM->hm.s.pFreeGuestPatchMem) - ((RTRCUINTPTR)pCtx->eip + 5);

            /* Overwrite the TPR instruction with a jump. */
            rc = PGMPhysSimpleWriteGCPtr(pVCpu, pCtx->eip, pPatch->aNewOpcode, 5);
            AssertRC(rc);

            DBGFR3_DISAS_INSTR_CUR_LOG(pVCpu, "Jump");

            pVM->hm.s.pFreeGuestPatchMem += off;
            pPatch->cbNewOp = 5;

            pPatch->Core.Key = pCtx->eip;
            rc = RTAvloU32Insert(&pVM->hm.s.PatchTree, &pPatch->Core);
            AssertRC(rc);

            pVM->hm.s.cPatches++;
            pVM->hm.s.fTprPatchingActive = true;
            STAM_COUNTER_INC(&pVM->hm.s.StatTprPatchSuccess);
            return VINF_SUCCESS;
        }

        Log(("Ran out of space in our patch buffer!\n"));
    }
    else
        Log(("hmR3PatchTprInstr: Failed to patch instr!\n"));


    /*
     * Save invalid patch, so we will not try again.
     */
    pPatch = &pVM->hm.s.aPatches[idx];
    pPatch->Core.Key = pCtx->eip;
    pPatch->enmType  = HMTPRINSTR_INVALID;
    rc = RTAvloU32Insert(&pVM->hm.s.PatchTree, &pPatch->Core);
    AssertRC(rc);
    pVM->hm.s.cPatches++;
    STAM_COUNTER_INC(&pVM->hm.s.StatTprPatchFailure);
    return VINF_SUCCESS;
}


/**
 * Attempt to patch TPR mmio instructions.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(int) HMR3PatchTprInstr(PVM pVM, PVMCPU pVCpu)
{
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONE_BY_ONE,
                                pVM->hm.s.pGuestPatchMem ? hmR3PatchTprInstr : hmR3ReplaceTprInstr,
                                (void *)(uintptr_t)pVCpu->idCpu);
    AssertRC(rc);
    return rc;
}


/**
 * Checks if we need to reschedule due to VMM device heap changes.
 *
 * @returns true if a reschedule is required, otherwise false.
 * @param   pVM         The cross context VM structure.
 * @param   pCtx        VM execution context.
 */
VMMR3_INT_DECL(bool) HMR3IsRescheduleRequired(PVM pVM, PCCPUMCTX pCtx)
{
    /*
     * The VMM device heap is a requirement for emulating real-mode or protected-mode without paging
     * when the unrestricted guest execution feature is missing (VT-x only).
     */
    if (    pVM->hm.s.vmx.fEnabled
        && !pVM->hm.s.vmx.fUnrestrictedGuestCfg
        &&  CPUMIsGuestInRealModeEx(pCtx)
        && !PDMVmmDevHeapIsEnabled(pVM))
        return true;

    return false;
}


/**
 * Noticiation callback from DBGF when interrupt breakpoints or generic debug
 * event settings changes.
 *
 * DBGF will call HMR3NotifyDebugEventChangedPerCpu on each CPU afterwards, this
 * function is just updating the VM globals.
 *
 * @param   pVM         The VM cross context VM structure.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(void) HMR3NotifyDebugEventChanged(PVM pVM)
{
    /* Interrupts. */
    bool fUseDebugLoop = pVM->dbgf.ro.cSoftIntBreakpoints > 0
                      || pVM->dbgf.ro.cHardIntBreakpoints > 0;

    /* CPU Exceptions. */
    for (DBGFEVENTTYPE enmEvent = DBGFEVENT_XCPT_FIRST;
         !fUseDebugLoop && enmEvent <= DBGFEVENT_XCPT_LAST;
         enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
        fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);

    /* Common VM exits. */
    for (DBGFEVENTTYPE enmEvent = DBGFEVENT_EXIT_FIRST;
         !fUseDebugLoop && enmEvent <= DBGFEVENT_EXIT_LAST_COMMON;
         enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
        fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);

    /* Vendor specific VM exits. */
    if (HMR3IsVmxEnabled(pVM->pUVM))
        for (DBGFEVENTTYPE enmEvent = DBGFEVENT_EXIT_VMX_FIRST;
             !fUseDebugLoop && enmEvent <= DBGFEVENT_EXIT_VMX_LAST;
             enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
            fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);
    else
        for (DBGFEVENTTYPE enmEvent = DBGFEVENT_EXIT_SVM_FIRST;
             !fUseDebugLoop && enmEvent <= DBGFEVENT_EXIT_SVM_LAST;
             enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
            fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);

    /* Done. */
    pVM->hm.s.fUseDebugLoop = fUseDebugLoop;
}


/**
 * Follow up notification callback to HMR3NotifyDebugEventChanged for each CPU.
 *
 * HM uses this to combine the decision made by HMR3NotifyDebugEventChanged with
 * per CPU settings.
 *
 * @param   pVM         The VM cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMR3_INT_DECL(void) HMR3NotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu)
{
    pVCpu->hm.s.fUseDebugLoop = pVCpu->hm.s.fSingleInstruction | pVM->hm.s.fUseDebugLoop;
}


#if 0 /* evil */
/**
 * Checks if we are currently using hardware acceleration.
 *
 * @returns true if hardware acceleration is being used, otherwise false.
 * @param   pVCpu        The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(bool) HMR3IsActive(PCVMCPU pVCpu)
{
    return pVCpu->hm.s.fActive;
}
#endif


/**
 * External interface for querying whether hardware acceleration is enabled.
 *
 * @returns true if VT-x or AMD-V is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 * @sa      HMIsEnabled, HMIsEnabledNotMacro.
 */
VMMR3DECL(bool) HMR3IsEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->fHMEnabled; /* Don't use the macro as the GUI may query us very very early. */
}


/**
 * External interface for querying whether VT-x is being used.
 *
 * @returns true if VT-x is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 * @sa      HMR3IsSvmEnabled, HMIsEnabled
 */
VMMR3DECL(bool) HMR3IsVmxEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->hm.s.vmx.fEnabled
        && pVM->hm.s.vmx.fSupported
        && pVM->fHMEnabled;
}


/**
 * External interface for querying whether AMD-V is being used.
 *
 * @returns true if VT-x is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 * @sa      HMR3IsVmxEnabled, HMIsEnabled
 */
VMMR3DECL(bool) HMR3IsSvmEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->hm.s.svm.fEnabled
        && pVM->hm.s.svm.fSupported
        && pVM->fHMEnabled;
}


/**
 * Checks if we are currently using nested paging.
 *
 * @returns true if nested paging is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsNestedPagingActive(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->hm.s.fNestedPagingCfg;
}


/**
 * Checks if virtualized APIC registers are enabled.
 *
 * When enabled this feature allows the hardware to access most of the
 * APIC registers in the virtual-APIC page without causing VM-exits. See
 * Intel spec. 29.1.1 "Virtualized APIC Registers".
 *
 * @returns true if virtualized APIC registers is enabled, otherwise
 *          false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3AreVirtApicRegsEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->hm.s.fVirtApicRegs;
}


/**
 * Checks if APIC posted-interrupt processing is enabled.
 *
 * This returns whether we can deliver interrupts to the guest without
 * leaving guest-context by updating APIC state from host-context.
 *
 * @returns true if APIC posted-interrupt processing is enabled,
 *          otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsPostedIntrsEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->hm.s.fPostedIntrs;
}


/**
 * Checks if we are currently using VPID in VT-x mode.
 *
 * @returns true if VPID is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsVpidActive(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->hm.s.ForR3.vmx.fVpid;
}


/**
 * Checks if we are currently using VT-x unrestricted execution,
 * aka UX.
 *
 * @returns true if UX is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsUXActive(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->hm.s.vmx.fUnrestrictedGuestCfg
        || pVM->hm.s.svm.fSupported;
}


/**
 * Checks if the VMX-preemption timer is being used.
 *
 * @returns true if the VMX-preemption timer is being used, otherwise false.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(bool) HMR3IsVmxPreemptionTimerUsed(PVM pVM)
{
    return HMIsEnabled(pVM)
        && pVM->hm.s.vmx.fEnabled
        && pVM->hm.s.vmx.fUsePreemptTimerCfg;
}


#ifdef TODO_9217_VMCSINFO
/**
 * Helper for HMR3CheckError to log VMCS controls to the release log.
 *
 * @param   idCpu           The Virtual CPU ID.
 * @param   pVmcsInfo       The VMCS info. object.
 */
static void hmR3CheckErrorLogVmcsCtls(VMCPUID idCpu, PCVMXVMCSINFO pVmcsInfo)
{
    LogRel(("HM: CPU[%u] PinCtls              %#RX32\n", idCpu, pVmcsInfo->u32PinCtls));
    {
        uint32_t const u32Val = pVmcsInfo->u32PinCtls;
        HMVMX_LOGREL_FEAT(u32Val, VMX_PIN_CTLS_EXT_INT_EXIT );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PIN_CTLS_NMI_EXIT     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PIN_CTLS_VIRT_NMI     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PIN_CTLS_PREEMPT_TIMER);
        HMVMX_LOGREL_FEAT(u32Val, VMX_PIN_CTLS_POSTED_INT   );
    }
    LogRel(("HM: CPU[%u] ProcCtls             %#RX32\n", idCpu, pVmcsInfo->u32ProcCtls));
    {
        uint32_t const u32Val = pVmcsInfo->u32ProcCtls;
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_INT_WINDOW_EXIT   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_USE_TSC_OFFSETTING);
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_HLT_EXIT          );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_INVLPG_EXIT       );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_MWAIT_EXIT        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_RDPMC_EXIT        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_RDTSC_EXIT        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_CR3_LOAD_EXIT     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_CR3_STORE_EXIT    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_USE_TERTIARY_CTLS );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_CR8_LOAD_EXIT     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_CR8_STORE_EXIT    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_USE_TPR_SHADOW    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_NMI_WINDOW_EXIT   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_MOV_DR_EXIT       );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_UNCOND_IO_EXIT    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_USE_IO_BITMAPS    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_MONITOR_TRAP_FLAG );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_USE_MSR_BITMAPS   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_MONITOR_EXIT      );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_PAUSE_EXIT        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS_USE_SECONDARY_CTLS);
    }
    LogRel(("HM: CPU[%u] ProcCtls2            %#RX32\n", idCpu, pVmcsInfo->u32ProcCtls2));
    {
        uint32_t const u32Val = pVmcsInfo->u32ProcCtls2;
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_VIRT_APIC_ACCESS   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_EPT                );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_DESC_TABLE_EXIT    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_RDTSCP             );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_VIRT_X2APIC_MODE   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_VPID               );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_WBINVD_EXIT        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_UNRESTRICTED_GUEST );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_APIC_REG_VIRT      );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_VIRT_INT_DELIVERY  );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_PAUSE_LOOP_EXIT    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_RDRAND_EXIT        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_INVPCID            );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_VMFUNC             );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_VMCS_SHADOWING     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_ENCLS_EXIT         );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_RDSEED_EXIT        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_PML                );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_EPT_XCPT_VE        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_CONCEAL_VMX_FROM_PT);
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_XSAVES_XRSTORS     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_PASID_TRANSLATE    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_MODE_BASED_EPT_PERM);
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_SPP_EPT            );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_PT_EPT             );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_TSC_SCALING        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_USER_WAIT_PAUSE    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_PCONFIG            );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_ENCLV_EXIT         );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_BUS_LOCK_DETECT    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_PROC_CTLS2_INSTR_TIMEOUT      );
    }
    LogRel(("HM: CPU[%u] EntryCtls            %#RX32\n", idCpu, pVmcsInfo->u32EntryCtls));
    {
        uint32_t const u32Val = pVmcsInfo->u32EntryCtls;
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_DEBUG         );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_IA32E_MODE_GUEST   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_ENTRY_TO_SMM       );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_DEACTIVATE_DUAL_MON);
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_PERF_MSR      );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_PAT_MSR       );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_EFER_MSR      );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_BNDCFGS_MSR   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_CONCEAL_VMX_FROM_PT);
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_RTIT_CTL_MSR  );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_UINV          );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_CET_STATE     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_LBR_CTL_MSR   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_ENTRY_CTLS_LOAD_PKRS_MSR      );
    }
    LogRel(("HM: CPU[%u] ExitCtls             %#RX32\n", idCpu, pVmcsInfo->u32ExitCtls));
    {
        uint32_t const u32Val = pVmcsInfo->u32ExitCtls;
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_SAVE_DEBUG            );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE  );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_LOAD_PERF_MSR         );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_ACK_EXT_INT           );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_SAVE_PAT_MSR          );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_LOAD_PAT_MSR          );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_SAVE_EFER_MSR         );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_LOAD_EFER_MSR         );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_CLEAR_BNDCFGS_MSR     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_CONCEAL_VMX_FROM_PT   );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_CLEAR_RTIT_CTL_MSR    );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_CLEAR_LBR_CTL_MSR     );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_CLEAR_UINV            );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_LOAD_CET_STATE        );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_LOAD_PKRS_MSR         );
        HMVMX_LOGREL_FEAT(u32Val, VMX_EXIT_CTLS_SAVE_PERF_MSR         );
    }
}
#endif


/**
 * Check fatal VT-x/AMD-V error and produce some meaningful
 * log release message.
 *
 * @param   pVM         The cross context VM structure.
 * @param   iStatusCode VBox status code.
 */
VMMR3_INT_DECL(void) HMR3CheckError(PVM pVM, int iStatusCode)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        /** @todo r=ramshankar: Are all EMTs out of ring-0 at this point!? If not, we
         *  might be getting inaccurate values for non-guru'ing EMTs. */
        PVMCPU              pVCpu             = pVM->apCpusR3[idCpu];
#ifdef TODO_9217_VMCSINFO
        PCVMXVMCSINFOSHARED pVmcsInfo         = hmGetVmxActiveVmcsInfoShared(pVCpu);
#endif
        bool const          fNstGstVmcsActive = pVCpu->hm.s.vmx.fSwitchedToNstGstVmcsCopyForRing3;
        switch (iStatusCode)
        {
            case VERR_VMX_INVALID_VMCS_PTR:
            {
                LogRel(("HM: VERR_VMX_INVALID_VMCS_PTR:\n"));
                LogRel(("HM: CPU[%u] %s VMCS active\n", idCpu, fNstGstVmcsActive ? "Nested-guest" : "Guest"));
#ifdef TODO_9217_VMCSINFO
                LogRel(("HM: CPU[%u] Current pointer      %#RHp vs %#RHp\n", idCpu, pVCpu->hm.s.vmx.LastError.HCPhysCurrentVmcs,
                                                                                pVmcsInfo->HCPhysVmcs));
#endif
                LogRel(("HM: CPU[%u] Current VMCS version %#x\n", idCpu, pVCpu->hm.s.vmx.LastError.u32VmcsRev));
                LogRel(("HM: CPU[%u] Entered Host Cpu     %u\n",  idCpu, pVCpu->hm.s.vmx.LastError.idEnteredCpu));
                LogRel(("HM: CPU[%u] Current Host Cpu     %u\n",  idCpu, pVCpu->hm.s.vmx.LastError.idCurrentCpu));
                break;
            }

            case VERR_VMX_UNABLE_TO_START_VM:
            {
                LogRel(("HM: VERR_VMX_UNABLE_TO_START_VM:\n"));
                LogRel(("HM: CPU[%u] %s VMCS active\n", idCpu, fNstGstVmcsActive ? "Nested-guest" : "Guest"));
                LogRel(("HM: CPU[%u] Instruction error    %#x\n", idCpu, pVCpu->hm.s.vmx.LastError.u32InstrError));
                LogRel(("HM: CPU[%u] Exit reason          %#x\n", idCpu, pVCpu->hm.s.vmx.LastError.u32ExitReason));

                if (   pVCpu->hm.s.vmx.LastError.u32InstrError == VMXINSTRERR_VMLAUNCH_NON_CLEAR_VMCS
                    || pVCpu->hm.s.vmx.LastError.u32InstrError == VMXINSTRERR_VMRESUME_NON_LAUNCHED_VMCS)
                {
                    LogRel(("HM: CPU[%u] Entered Host Cpu     %u\n",  idCpu, pVCpu->hm.s.vmx.LastError.idEnteredCpu));
                    LogRel(("HM: CPU[%u] Current Host Cpu     %u\n",  idCpu, pVCpu->hm.s.vmx.LastError.idCurrentCpu));
                }
                else if (pVCpu->hm.s.vmx.LastError.u32InstrError == VMXINSTRERR_VMENTRY_INVALID_CTLS)
                {
#ifdef TODO_9217_VMCSINFO
                    hmR3CheckErrorLogVmcsCtls(idCpu, pVmcsInfo);
                    LogRel(("HM: CPU[%u] HCPhysMsrBitmap      %#RHp\n",  idCpu, pVmcsInfo->HCPhysMsrBitmap));
                    LogRel(("HM: CPU[%u] HCPhysGuestMsrLoad   %#RHp\n",  idCpu, pVmcsInfo->HCPhysGuestMsrLoad));
                    LogRel(("HM: CPU[%u] HCPhysGuestMsrStore  %#RHp\n",  idCpu, pVmcsInfo->HCPhysGuestMsrStore));
                    LogRel(("HM: CPU[%u] HCPhysHostMsrLoad    %#RHp\n",  idCpu, pVmcsInfo->HCPhysHostMsrLoad));
                    LogRel(("HM: CPU[%u] cEntryMsrLoad        %u\n",     idCpu, pVmcsInfo->cEntryMsrLoad));
                    LogRel(("HM: CPU[%u] cExitMsrStore        %u\n",     idCpu, pVmcsInfo->cExitMsrStore));
                    LogRel(("HM: CPU[%u] cExitMsrLoad         %u\n",     idCpu, pVmcsInfo->cExitMsrLoad));
#endif
                }
                /** @todo Log VM-entry event injection control fields
                 *        VMX_VMCS_CTRL_ENTRY_IRQ_INFO, VMX_VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE
                 *        and VMX_VMCS_CTRL_ENTRY_INSTR_LENGTH from the VMCS. */
                break;
            }

            case VERR_VMX_INVALID_GUEST_STATE:
            {
                LogRel(("HM: VERR_VMX_INVALID_GUEST_STATE:\n"));
                LogRel(("HM: CPU[%u] HM error = %#RX32 (%RU32)\n", idCpu, pVCpu->hm.s.u32HMError, pVCpu->hm.s.u32HMError));
                LogRel(("HM: CPU[%u] Guest-intr. state = %#RX32\n", idCpu, pVCpu->hm.s.vmx.LastError.u32GuestIntrState));
#ifdef TODO_9217_VMCSINFO
                hmR3CheckErrorLogVmcsCtls(idCpu, pVmcsInfo);
#endif
                break;
            }

            /* The guru will dump the HM error and exit history. Nothing extra to report for these errors. */
            case VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO:
            case VERR_VMX_INVALID_VMXON_PTR:
            case VERR_VMX_UNEXPECTED_EXIT:
            case VERR_VMX_INVALID_VMCS_FIELD:
            case VERR_SVM_UNKNOWN_EXIT:
            case VERR_SVM_UNEXPECTED_EXIT:
            case VERR_SVM_UNEXPECTED_PATCH_TYPE:
            case VERR_SVM_UNEXPECTED_XCPT_EXIT:
            case VERR_VMX_UNEXPECTED_INTERRUPTION_EXIT_TYPE:
                break;
        }
    }

    if (iStatusCode == VERR_VMX_UNABLE_TO_START_VM)
    {
        LogRel(("HM: VERR_VMX_UNABLE_TO_START_VM: VM-entry allowed-1  %#RX32\n", pVM->hm.s.ForR3.vmx.Msrs.EntryCtls.n.allowed1));
        LogRel(("HM: VERR_VMX_UNABLE_TO_START_VM: VM-entry allowed-0  %#RX32\n", pVM->hm.s.ForR3.vmx.Msrs.EntryCtls.n.allowed0));
    }
    else if (iStatusCode == VERR_VMX_INVALID_VMXON_PTR)
        LogRel(("HM: HCPhysVmxEnableError         = %#RHp\n", pVM->hm.s.ForR3.vmx.HCPhysVmxEnableError));
}


/**
 * Execute state save operation.
 *
 * Save only data that cannot be re-loaded while entering HM ring-0 code. This
 * is because we always save the VM state from ring-3 and thus most HM state
 * will be re-synced dynamically at runtime and don't need to be part of the VM
 * saved state.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) hmR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    Log(("hmR3Save:\n"));

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        Assert(!pVCpu->hm.s.Event.fPending);
        if (pVM->cpum.ro.GuestFeatures.fSvm)
        {
            PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
            SSMR3PutBool(pSSM, pVmcbNstGstCache->fCacheValid);
            SSMR3PutU16(pSSM,  pVmcbNstGstCache->u16InterceptRdCRx);
            SSMR3PutU16(pSSM,  pVmcbNstGstCache->u16InterceptWrCRx);
            SSMR3PutU16(pSSM,  pVmcbNstGstCache->u16InterceptRdDRx);
            SSMR3PutU16(pSSM,  pVmcbNstGstCache->u16InterceptWrDRx);
            SSMR3PutU16(pSSM,  pVmcbNstGstCache->u16PauseFilterThreshold);
            SSMR3PutU16(pSSM,  pVmcbNstGstCache->u16PauseFilterCount);
            SSMR3PutU32(pSSM,  pVmcbNstGstCache->u32InterceptXcpt);
            SSMR3PutU64(pSSM,  pVmcbNstGstCache->u64InterceptCtrl);
            SSMR3PutU64(pSSM,  pVmcbNstGstCache->u64TSCOffset);
            SSMR3PutBool(pSSM, pVmcbNstGstCache->fVIntrMasking);
            SSMR3PutBool(pSSM, pVmcbNstGstCache->fNestedPaging);
            SSMR3PutBool(pSSM, pVmcbNstGstCache->fLbrVirt);
        }
    }

    /* Save the guest patch data. */
    SSMR3PutGCPtr(pSSM, pVM->hm.s.pGuestPatchMem);
    SSMR3PutGCPtr(pSSM, pVM->hm.s.pFreeGuestPatchMem);
    SSMR3PutU32(pSSM, pVM->hm.s.cbGuestPatchMem);

    /* Store all the guest patch records too. */
    int rc = SSMR3PutU32(pSSM, pVM->hm.s.cPatches);
    if (RT_FAILURE(rc))
        return rc;

    for (uint32_t i = 0; i < pVM->hm.s.cPatches; i++)
    {
        AssertCompileSize(HMTPRINSTR, 4);
        PCHMTPRPATCH pPatch = &pVM->hm.s.aPatches[i];
        SSMR3PutU32(pSSM, pPatch->Core.Key);
        SSMR3PutMem(pSSM, pPatch->aOpcode, sizeof(pPatch->aOpcode));
        SSMR3PutU32(pSSM, pPatch->cbOp);
        SSMR3PutMem(pSSM, pPatch->aNewOpcode, sizeof(pPatch->aNewOpcode));
        SSMR3PutU32(pSSM, pPatch->cbNewOp);
        SSMR3PutU32(pSSM, (uint32_t)pPatch->enmType);
        SSMR3PutU32(pSSM, pPatch->uSrcOperand);
        SSMR3PutU32(pSSM, pPatch->uDstOperand);
        SSMR3PutU32(pSSM, pPatch->pJumpTarget);
        rc = SSMR3PutU32(pSSM, pPatch->cFaults);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Execute state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        Data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) hmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    int rc;

    LogFlowFunc(("uVersion=%u\n", uVersion));
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /*
     * Validate version.
     */
    if (   uVersion != HM_SAVED_STATE_VERSION_SVM_NESTED_HWVIRT
        && uVersion != HM_SAVED_STATE_VERSION_TPR_PATCHING
        && uVersion != HM_SAVED_STATE_VERSION_NO_TPR_PATCHING
        && uVersion != HM_SAVED_STATE_VERSION_2_0_X)
    {
        AssertMsgFailed(("hmR3Load: Invalid version uVersion=%d!\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    /*
     * Load per-VCPU state.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        if (uVersion >= HM_SAVED_STATE_VERSION_SVM_NESTED_HWVIRT)
        {
            /* Load the SVM nested hw.virt state if the VM is configured for it. */
            if (pVM->cpum.ro.GuestFeatures.fSvm)
            {
                PSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
                SSMR3GetBool(pSSM, &pVmcbNstGstCache->fCacheValid);
                SSMR3GetU16(pSSM,  &pVmcbNstGstCache->u16InterceptRdCRx);
                SSMR3GetU16(pSSM,  &pVmcbNstGstCache->u16InterceptWrCRx);
                SSMR3GetU16(pSSM,  &pVmcbNstGstCache->u16InterceptRdDRx);
                SSMR3GetU16(pSSM,  &pVmcbNstGstCache->u16InterceptWrDRx);
                SSMR3GetU16(pSSM,  &pVmcbNstGstCache->u16PauseFilterThreshold);
                SSMR3GetU16(pSSM,  &pVmcbNstGstCache->u16PauseFilterCount);
                SSMR3GetU32(pSSM,  &pVmcbNstGstCache->u32InterceptXcpt);
                SSMR3GetU64(pSSM,  &pVmcbNstGstCache->u64InterceptCtrl);
                SSMR3GetU64(pSSM,  &pVmcbNstGstCache->u64TSCOffset);
                SSMR3GetBool(pSSM, &pVmcbNstGstCache->fVIntrMasking);
                SSMR3GetBool(pSSM, &pVmcbNstGstCache->fNestedPaging);
                rc = SSMR3GetBool(pSSM, &pVmcbNstGstCache->fLbrVirt);
                AssertRCReturn(rc, rc);
            }
        }
        else
        {
            /* Pending HM event (obsolete for a long time since TPRM holds the info.) */
            SSMR3GetU32(pSSM, &pVCpu->hm.s.Event.fPending);
            SSMR3GetU32(pSSM, &pVCpu->hm.s.Event.u32ErrCode);
            SSMR3GetU64(pSSM, &pVCpu->hm.s.Event.u64IntInfo);

            /* VMX fWasInRealMode related data. */
            uint32_t uDummy;
            SSMR3GetU32(pSSM, &uDummy);
            SSMR3GetU32(pSSM, &uDummy);
            rc = SSMR3GetU32(pSSM, &uDummy);
            AssertRCReturn(rc, rc);
        }
    }

    /*
     * Load TPR patching data.
     */
    if (uVersion >= HM_SAVED_STATE_VERSION_TPR_PATCHING)
    {
        SSMR3GetGCPtr(pSSM, &pVM->hm.s.pGuestPatchMem);
        SSMR3GetGCPtr(pSSM, &pVM->hm.s.pFreeGuestPatchMem);
        SSMR3GetU32(pSSM, &pVM->hm.s.cbGuestPatchMem);

        /* Fetch all TPR patch records. */
        rc = SSMR3GetU32(pSSM, &pVM->hm.s.cPatches);
        AssertRCReturn(rc, rc);
        for (uint32_t i = 0; i < pVM->hm.s.cPatches; i++)
        {
            PHMTPRPATCH pPatch = &pVM->hm.s.aPatches[i];
            SSMR3GetU32(pSSM, &pPatch->Core.Key);
            SSMR3GetMem(pSSM, pPatch->aOpcode, sizeof(pPatch->aOpcode));
            SSMR3GetU32(pSSM, &pPatch->cbOp);
            SSMR3GetMem(pSSM, pPatch->aNewOpcode, sizeof(pPatch->aNewOpcode));
            SSMR3GetU32(pSSM, &pPatch->cbNewOp);
            SSM_GET_ENUM32_RET(pSSM, pPatch->enmType, HMTPRINSTR);

            if (pPatch->enmType == HMTPRINSTR_JUMP_REPLACEMENT)
                pVM->hm.s.fTprPatchingActive = true;
            Assert(pPatch->enmType == HMTPRINSTR_JUMP_REPLACEMENT || pVM->hm.s.fTprPatchingActive == false);

            SSMR3GetU32(pSSM, &pPatch->uSrcOperand);
            SSMR3GetU32(pSSM, &pPatch->uDstOperand);
            SSMR3GetU32(pSSM, &pPatch->cFaults);
            rc = SSMR3GetU32(pSSM, &pPatch->pJumpTarget);
            AssertRCReturn(rc, rc);

            LogFlow(("hmR3Load: patch %d\n", i));
            LogFlow(("Key       = %x\n", pPatch->Core.Key));
            LogFlow(("cbOp      = %d\n", pPatch->cbOp));
            LogFlow(("cbNewOp   = %d\n", pPatch->cbNewOp));
            LogFlow(("type      = %d\n", pPatch->enmType));
            LogFlow(("srcop     = %d\n", pPatch->uSrcOperand));
            LogFlow(("dstop     = %d\n", pPatch->uDstOperand));
            LogFlow(("cFaults   = %d\n", pPatch->cFaults));
            LogFlow(("target    = %x\n", pPatch->pJumpTarget));

            rc = RTAvloU32Insert(&pVM->hm.s.PatchTree, &pPatch->Core);
            AssertRCReturn(rc, rc);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Displays HM info.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) hmR3Info(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    if (HMIsEnabled(pVM))
    {
        if (pVM->hm.s.vmx.fSupported)
            pHlp->pfnPrintf(pHlp, "CPU[%u]: VT-x info:\n", pVCpu->idCpu);
        else
            pHlp->pfnPrintf(pHlp, "CPU[%u]: AMD-V info:\n", pVCpu->idCpu);
        pHlp->pfnPrintf(pHlp, "  HM error           = %#x (%u)\n", pVCpu->hm.s.u32HMError, pVCpu->hm.s.u32HMError);
        pHlp->pfnPrintf(pHlp, "  rcLastExitToR3     = %Rrc\n", pVCpu->hm.s.rcLastExitToR3);
        if (pVM->hm.s.vmx.fSupported)
        {
            PCVMXVMCSINFOSHARED pVmcsInfoShared   = hmGetVmxActiveVmcsInfoShared(pVCpu);
            bool const          fRealOnV86Active  = pVmcsInfoShared->RealMode.fRealOnV86Active;
            bool const          fNstGstVmcsActive = pVCpu->hm.s.vmx.fSwitchedToNstGstVmcsCopyForRing3;

            pHlp->pfnPrintf(pHlp, "  %s VMCS active\n", fNstGstVmcsActive ? "Nested-guest" : "Guest");
            pHlp->pfnPrintf(pHlp, "    Real-on-v86 active = %RTbool\n", fRealOnV86Active);
            if (fRealOnV86Active)
            {
                pHlp->pfnPrintf(pHlp, "      EFlags  = %#x\n", pVmcsInfoShared->RealMode.Eflags.u32);
                pHlp->pfnPrintf(pHlp, "      Attr CS = %#x\n", pVmcsInfoShared->RealMode.AttrCS.u);
                pHlp->pfnPrintf(pHlp, "      Attr SS = %#x\n", pVmcsInfoShared->RealMode.AttrSS.u);
                pHlp->pfnPrintf(pHlp, "      Attr DS = %#x\n", pVmcsInfoShared->RealMode.AttrDS.u);
                pHlp->pfnPrintf(pHlp, "      Attr ES = %#x\n", pVmcsInfoShared->RealMode.AttrES.u);
                pHlp->pfnPrintf(pHlp, "      Attr FS = %#x\n", pVmcsInfoShared->RealMode.AttrFS.u);
                pHlp->pfnPrintf(pHlp, "      Attr GS = %#x\n", pVmcsInfoShared->RealMode.AttrGS.u);
            }
        }
    }
    else
        pHlp->pfnPrintf(pHlp, "HM is not enabled for this VM!\n");
}


/**
 * Displays the HM Last-Branch-Record info. for the guest.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) hmR3InfoLbr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    if (!HMIsEnabled(pVM))
        pHlp->pfnPrintf(pHlp, "HM is not enabled for this VM!\n");
    else if (HMIsVmxActive(pVM))
    {
        if (pVM->hm.s.vmx.fLbrCfg)
        {
            PCVMXVMCSINFOSHARED pVmcsInfoShared = hmGetVmxActiveVmcsInfoShared(pVCpu);
            uint32_t const      cLbrStack       = pVM->hm.s.ForR3.vmx.idLbrFromIpMsrLast - pVM->hm.s.ForR3.vmx.idLbrFromIpMsrFirst + 1;

            /** @todo r=ramshankar: The index technically varies depending on the CPU, but
             *        0xf should cover everything we support thus far. Fix if necessary
             *        later. */
            uint32_t const idxTopOfStack = pVmcsInfoShared->u64LbrTosMsr & 0xf;
            if (idxTopOfStack > cLbrStack)
            {
                pHlp->pfnPrintf(pHlp, "Top-of-stack LBR MSR seems corrupt (index=%u, msr=%#RX64) expected index < %u\n",
                                idxTopOfStack, pVmcsInfoShared->u64LbrTosMsr, cLbrStack);
                return;
            }

            /*
             * Dump the circular buffer of LBR records starting from the most recent record (contained in idxTopOfStack).
             */
            pHlp->pfnPrintf(pHlp, "CPU[%u]: LBRs (most-recent first)\n", pVCpu->idCpu);
            uint32_t idxCurrent = idxTopOfStack;
            Assert(idxTopOfStack < cLbrStack);
            Assert(RT_ELEMENTS(pVmcsInfoShared->au64LbrFromIpMsr) <= cLbrStack);
            Assert(RT_ELEMENTS(pVmcsInfoShared->au64LbrToIpMsr) <= cLbrStack);
            for (;;)
            {
                if (pVM->hm.s.ForR3.vmx.idLbrToIpMsrFirst)
                    pHlp->pfnPrintf(pHlp, "  Branch (%2u): From IP=%#016RX64 - To IP=%#016RX64\n", idxCurrent,
                                    pVmcsInfoShared->au64LbrFromIpMsr[idxCurrent], pVmcsInfoShared->au64LbrToIpMsr[idxCurrent]);
                else
                    pHlp->pfnPrintf(pHlp, "  Branch (%2u): LBR=%#RX64\n", idxCurrent, pVmcsInfoShared->au64LbrFromIpMsr[idxCurrent]);

                idxCurrent = (idxCurrent - 1) % cLbrStack;
                if (idxCurrent == idxTopOfStack)
                    break;
            }
        }
        else
            pHlp->pfnPrintf(pHlp, "VM not configured to record LBRs for the guest\n");
    }
    else
    {
        Assert(HMIsSvmActive(pVM));
        /** @todo SVM: LBRs (get them from VMCB if possible). */
        pHlp->pfnPrintf(pHlp, "SVM LBR not implemented.\n");
    }
}


/**
 * Displays the HM pending event.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) hmR3InfoEventPending(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    if (HMIsEnabled(pVM))
    {
        pHlp->pfnPrintf(pHlp, "CPU[%u]: HM event (fPending=%RTbool)\n", pVCpu->idCpu, pVCpu->hm.s.Event.fPending);
        if (pVCpu->hm.s.Event.fPending)
        {
            pHlp->pfnPrintf(pHlp, "  u64IntInfo        = %#RX64\n", pVCpu->hm.s.Event.u64IntInfo);
            pHlp->pfnPrintf(pHlp, "  u32ErrCode        = %#RX64\n", pVCpu->hm.s.Event.u32ErrCode);
            pHlp->pfnPrintf(pHlp, "  cbInstr           = %u bytes\n", pVCpu->hm.s.Event.cbInstr);
            pHlp->pfnPrintf(pHlp, "  GCPtrFaultAddress = %#RGp\n", pVCpu->hm.s.Event.GCPtrFaultAddress);
        }
    }
    else
        pHlp->pfnPrintf(pHlp, "HM is not enabled for this VM!\n");
}


/**
 * Displays the SVM nested-guest VMCB cache.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) hmR3InfoSvmNstGstVmcbCache(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    bool const fSvmEnabled = HMR3IsSvmEnabled(pVM->pUVM);
    if (   fSvmEnabled
        && pVM->cpum.ro.GuestFeatures.fSvm)
    {
        PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
        pHlp->pfnPrintf(pHlp, "CPU[%u]: HM SVM nested-guest VMCB cache\n", pVCpu->idCpu);
        pHlp->pfnPrintf(pHlp, "  fCacheValid             = %#RTbool\n", pVmcbNstGstCache->fCacheValid);
        pHlp->pfnPrintf(pHlp, "  u16InterceptRdCRx       = %#RX16\n",   pVmcbNstGstCache->u16InterceptRdCRx);
        pHlp->pfnPrintf(pHlp, "  u16InterceptWrCRx       = %#RX16\n",   pVmcbNstGstCache->u16InterceptWrCRx);
        pHlp->pfnPrintf(pHlp, "  u16InterceptRdDRx       = %#RX16\n",   pVmcbNstGstCache->u16InterceptRdDRx);
        pHlp->pfnPrintf(pHlp, "  u16InterceptWrDRx       = %#RX16\n",   pVmcbNstGstCache->u16InterceptWrDRx);
        pHlp->pfnPrintf(pHlp, "  u16PauseFilterThreshold = %#RX16\n",   pVmcbNstGstCache->u16PauseFilterThreshold);
        pHlp->pfnPrintf(pHlp, "  u16PauseFilterCount     = %#RX16\n",   pVmcbNstGstCache->u16PauseFilterCount);
        pHlp->pfnPrintf(pHlp, "  u32InterceptXcpt        = %#RX32\n",   pVmcbNstGstCache->u32InterceptXcpt);
        pHlp->pfnPrintf(pHlp, "  u64InterceptCtrl        = %#RX64\n",   pVmcbNstGstCache->u64InterceptCtrl);
        pHlp->pfnPrintf(pHlp, "  u64TSCOffset            = %#RX64\n",   pVmcbNstGstCache->u64TSCOffset);
        pHlp->pfnPrintf(pHlp, "  fVIntrMasking           = %RTbool\n",  pVmcbNstGstCache->fVIntrMasking);
        pHlp->pfnPrintf(pHlp, "  fNestedPaging           = %RTbool\n",  pVmcbNstGstCache->fNestedPaging);
        pHlp->pfnPrintf(pHlp, "  fLbrVirt                = %RTbool\n",  pVmcbNstGstCache->fLbrVirt);
    }
    else
    {
        if (!fSvmEnabled)
            pHlp->pfnPrintf(pHlp, "HM SVM is not enabled for this VM!\n");
        else
            pHlp->pfnPrintf(pHlp, "SVM feature is not exposed to the guest!\n");
    }
}

