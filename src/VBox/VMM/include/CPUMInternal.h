/* $Id: CPUMInternal.h 168915 2025-05-16 21:41:53Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_CPUMInternal_h
#define VMM_INCLUDED_SRC_include_CPUMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_FOR_DTRACE_LIB
# include <VBox/cdefs.h>
# include <VBox/types.h>
# include <VBox/vmm/stam.h>
# include <iprt/x86.h>
# include <VBox/vmm/pgm.h>
#else
# pragma D depends_on library x86.d
# pragma D depends_on library cpumctx.d
# pragma D depends_on library cpum.d

/* Some fudging. */
typedef uint64_t STAMCOUNTER;
#endif




/** @defgroup grp_cpum_int   Internals
 * @ingroup grp_cpum
 * @internal
 * @{
 */

/** Use flags (CPUM::fUseFlags).
 * (Don't forget to sync this with CPUMInternal.mac !)
 * @note Was part of saved state (6.1 and earlier).
 * @{ */
#if defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)

/** Indicates that we've saved the host FPU, SSE, whatever state and that it
 * needs to be restored. */
#define CPUM_USED_FPU_HOST              RT_BIT(0)
/** Indicates that we've loaded the guest FPU, SSE, whatever state and that it
 * needs to be saved.
 * @note Mirrored in CPUMCTX::fUsedFpuGuest for the HM switcher code. */
#define CPUM_USED_FPU_GUEST             RT_BIT(10)
/** Used the guest FPU, SSE or such stuff since last we were in REM.
 * REM syncing is clearing this, lazy FPU is setting it. */
#define CPUM_USED_FPU_SINCE_REM         RT_BIT(1)
/** The XMM state was manually restored. (AMD only) */
#define CPUM_USED_MANUAL_XMM_RESTORE    RT_BIT(2)

/** Host OS is using SYSENTER and we must NULL the CS. */
#define CPUM_USE_SYSENTER               RT_BIT(3)
/** Host OS is using SYSENTER and we must NULL the CS. */
#define CPUM_USE_SYSCALL                RT_BIT(4)

/** Debug registers are used by host and that DR7 and DR6 must be saved and
 *  disabled when switching to raw-mode. */
#define CPUM_USE_DEBUG_REGS_HOST        RT_BIT(5)
/** Records that we've saved the host DRx registers.
 * In ring-0 this means all (DR0-7), while in raw-mode context this means DR0-3
 * since DR6 and DR7 are covered by CPUM_USE_DEBUG_REGS_HOST. */
#define CPUM_USED_DEBUG_REGS_HOST       RT_BIT(6)
/** Set to indicate that we should save host DR0-7 and load the hypervisor debug
 * registers in the raw-mode world switchers. (See CPUMRecalcHyperDRx.) */
#define CPUM_USE_DEBUG_REGS_HYPER       RT_BIT(7)
/** Used in ring-0 to indicate that we have loaded the hypervisor debug
 * registers. */
#define CPUM_USED_DEBUG_REGS_HYPER      RT_BIT(8)
/** Used in ring-0 to indicate that we have loaded the guest debug
 * registers (DR0-3 and maybe DR6) for direct use by the guest.
 * DR7 (and AMD-V DR6) are handled via the VMCB. */
#define CPUM_USED_DEBUG_REGS_GUEST      RT_BIT(9)

/** Host CPU requires fxsave/fxrstor leaky bit handling. */
#define CPUM_USE_FFXSR_LEAKY            RT_BIT(19)
/** Set if the VM supports long-mode. */
#define CPUM_USE_SUPPORTS_LONGMODE      RT_BIT(20)

#endif
/** @} */


/** @name CPUM Saved State Version.
 * @{ */

/** The current saved state version.
 *  @todo AMD64:When bumping to next version, add CPUMCTX::enmHwVirt and
 *        uMicrocodeRevision to the saved state. */
#if defined(VBOX_VMM_TARGET_X86)
# define CPUM_SAVED_STATE_VERSION               CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_4
#elif defined(VBOX_VMM_TARGET_ARMV8)
# define CPUM_SAVED_STATE_VERSION               CPUM_SAVED_STATE_VERSION_ARMV8_V2
#endif

#if defined(VBOX_VMM_TARGET_X86)
/** The saved state version with u32RestoreProcCtls2 for Nested Microsoft
 *  Hyper-V. */
# define CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_4  23
/** The saved state version with more virtual VMCS fields (HLAT prefix size,
 *  PCONFIG-exiting bitmap, HLAT ptr, VM-exit ctls2) and a CPUMCTX field (VM-exit
 *  ctls2 MSR). */
# define CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_3  22
/** The saved state version with PAE PDPEs added. */
# define CPUM_SAVED_STATE_VERSION_PAE_PDPES     21
/** The saved state version with more virtual VMCS fields and CPUMCTX VMX fields. */
# define CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2  20
/** The saved state version including VMX hardware virtualization state. */
# define CPUM_SAVED_STATE_VERSION_HWVIRT_VMX    19
/** The saved state version including SVM hardware virtualization state. */
# define CPUM_SAVED_STATE_VERSION_HWVIRT_SVM    18
/** The saved state version including XSAVE state. */
# define CPUM_SAVED_STATE_VERSION_XSAVE         17
/** The saved state version with good CPUID leaf count. */
# define CPUM_SAVED_STATE_VERSION_GOOD_CPUID_COUNT 16
/** CPUID changes with explode forgetting to update the leaf count on
 * restore, resulting in garbage being saved restoring+saving old states). */
# define CPUM_SAVED_STATE_VERSION_BAD_CPUID_COUNT 15
/** The saved state version before the CPUIDs changes. */
# define CPUM_SAVED_STATE_VERSION_PUT_STRUCT    14
/** The saved state version before using SSMR3PutStruct. */
# define CPUM_SAVED_STATE_VERSION_MEM           13
/** The saved state version before introducing the MSR size field. */
# define CPUM_SAVED_STATE_VERSION_NO_MSR_SIZE   12
/** The saved state version of 3.2, 3.1 and 3.3 trunk before the hidden
 * selector register change (CPUM_CHANGED_HIDDEN_SEL_REGS_INVALID). */
# define CPUM_SAVED_STATE_VERSION_VER3_2        11
/** The saved state version of 3.0 and 3.1 trunk before the teleportation
 * changes. */
# define CPUM_SAVED_STATE_VERSION_VER3_0        10
/** The saved state version for the 2.1 trunk before the MSR changes. */
# define CPUM_SAVED_STATE_VERSION_VER2_1_NOMSR  9
/** The saved state version of 2.0, used for backwards compatibility. */
# define CPUM_SAVED_STATE_VERSION_VER2_0        8
/** The saved state version of 1.6, used for backwards compatibility. */
# define CPUM_SAVED_STATE_VERSION_VER1_6        6
#endif

#if defined(VBOX_VMM_TARGET_ARMV8)
/** Adds ACTLR_EL1 to the ARMv8 saved state. */
# define CPUM_SAVED_STATE_VERSION_ARMV8_V2      2
/** The initial ARMv8 saved state. */
# define CPUM_SAVED_STATE_VERSION_ARMV8_V1      1
#endif
/** @} */


#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86)
/** @name AMD64: XSAVE limits.
 * @{ */
/** Max size we accept for the XSAVE area.
 * @see CPUMCTX::abXSave */
#define CPUM_MAX_XSAVE_AREA_SIZE    (0x4000 - 0x300)
/* Min size we accept for the XSAVE area. */
#define CPUM_MIN_XSAVE_AREA_SIZE    0x240
/** @} */
#endif

/**
 * CPU info
 */
typedef struct CPUMINFO
{
#if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
    /** The number of MSR ranges (CPUMMSRRANGE) in the array pointed to below. */
    uint32_t                    cMsrRanges;
    /** Mask applied to ECX before looking up the MSR for a RDMSR/WRMSR
     * instruction.  Older hardware has been observed to ignore higher bits. */
    uint32_t                    fMsrMask;

    /** MXCSR mask. */
    uint32_t                    fMxCsrMask;

    /** The number of CPUID leaves (CPUMCPUIDLEAF) in the array pointed to below. */
    uint32_t                    cCpuIdLeaves;
    /** The index of the first extended CPUID leaf in the array.
     *  Set to cCpuIdLeaves if none present. */
    uint32_t                    iFirstExtCpuIdLeaf;
    /** How to handle unknown CPUID leaves. */
    CPUMUNKNOWNCPUID            enmUnknownCpuIdMethod;
    /** For use with CPUMUNKNOWNCPUID_DEFAULTS (DB & VM),
     * CPUMUNKNOWNCPUID_LAST_STD_LEAF (VM) and CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX (VM). */
    CPUMCPUID                   DefCpuId;

    /** Scalable bus frequency used for reporting other frequencies. */
    uint64_t                    uScalableBusFreq;

    /** The microcode revision.
     * UINT32_MAX if the one from the CPU database entry is to be used.
     * @see /CPUM/GuestMicrocodeRevision in CFGM. */
    uint32_t                    uMicrocodeRevision;
    uint32_t                    uPadding;

    /** Pointer to the MSR ranges (for compatibility with old hyper heap code). */
    R3PTRTYPE(PCPUMMSRRANGE)    paMsrRangesR3;
    /** Pointer to the CPUID leaves (for compatibility with old hyper heap code). */
    R3PTRTYPE(PCPUMCPUIDLEAF)   paCpuIdLeavesR3;

    /** CPUID leaves. */
    CPUMCPUIDLEAF               aCpuIdLeaves[256];
    /** MSR ranges.
     * @todo This is insane, so might want to move this into a separate
     *       allocation.  The insanity is mainly for more recent AMD CPUs. */
    CPUMMSRRANGE                aMsrRanges[8192];

#elif defined(VBOX_VMM_TARGET_ARMV8)
    /** The number of system register ranges (CPUMSSREGRANGE) in the array pointed to below. */
    uint32_t                    cSysRegRanges;
    uint32_t                    uPadding;

    /** Pointer to the sysrem register ranges. */
    R3PTRTYPE(PCPUMSYSREGRANGE) paSysRegRangesR3;

    /** System register ranges. */
    CPUMSYSREGRANGE             aSysRegRanges[128];
#else
# error "port me"
#endif
} CPUMINFO;
/** Pointer to a CPU info structure. */
typedef CPUMINFO *PCPUMINFO;
/** Pointer to a const CPU info structure. */
typedef CPUMINFO const *CPCPUMINFO;


#ifdef RT_ARCH_AMD64
/**
 * The saved host CPU state.
 */
typedef struct CPUMHOSTCTX
{
    /** The extended state (FPU/SSE/AVX/AVX-2/XXXX). Must be aligned on 64 bytes. */
    union /* no tag */
    {
        X86XSAVEAREA    XState;
        /** Byte view for simple indexing and space allocation.
         * @note Must match or exceed the size of CPUMCTX::abXState. */
        uint8_t         abXState[0x4000 - 0x300];
    } CPUM_UNION_NM(u);

    /** General purpose register, selectors, flags and more
     * @{ */
    /** General purpose register ++
     * { */
    /*uint64_t        rax; - scratch*/
    uint64_t        rbx;
    /*uint64_t        rcx; - scratch*/
    /*uint64_t        rdx; - scratch*/
    uint64_t        rdi;
    uint64_t        rsi;
    uint64_t        rbp;
    uint64_t        rsp;
    /*uint64_t        r8; - scratch*/
    /*uint64_t        r9; - scratch*/
    uint64_t        r10;
    uint64_t        r11;
    uint64_t        r12;
    uint64_t        r13;
    uint64_t        r14;
    uint64_t        r15;
    /*uint64_t        rip; - scratch*/
    uint64_t        rflags;
    /** @} */

    /** Selector registers
     * @{ */
    RTSEL           ss;
    RTSEL           ssPadding;
    RTSEL           gs;
    RTSEL           gsPadding;
    RTSEL           fs;
    RTSEL           fsPadding;
    RTSEL           es;
    RTSEL           esPadding;
    RTSEL           ds;
    RTSEL           dsPadding;
    RTSEL           cs;
    RTSEL           csPadding;
    /** @} */

    /** Control registers.
     * @{ */
    /** The CR0 FPU state in HM mode.  */
    uint64_t        cr0;
    /*uint64_t        cr2; - scratch*/
    uint64_t        cr3;
    uint64_t        cr4;
    uint64_t        cr8;
    /** @} */

    /** Debug registers.
     * @{ */
    uint64_t        dr0;
    uint64_t        dr1;
    uint64_t        dr2;
    uint64_t        dr3;
    uint64_t        dr6;
    uint64_t        dr7;
    /** @} */

    /** Global Descriptor Table register. */
    X86XDTR64       gdtr;
    uint16_t        gdtrPadding;
    /** Interrupt Descriptor Table register. */
    X86XDTR64       idtr;
    uint16_t        idtrPadding;
    /** The task register. */
    RTSEL           ldtr;
    RTSEL           ldtrPadding;
    /** The task register. */
    RTSEL           tr;
    RTSEL           trPadding;

    /** MSRs
     * @{ */
    CPUMSYSENTER    SysEnter;
    uint64_t        FSbase;
    uint64_t        GSbase;
    uint64_t        efer;
    /** @} */

    /** The XCR0 register. */
    uint64_t        xcr0;
    /** The mask to pass to XSAVE/XRSTOR in EDX:EAX.  If zero we use
     *  FXSAVE/FXRSTOR (since bit 0 will always be set, we only need to test it). */
    uint64_t        fXStateMask;

    /* padding to get 64byte aligned size */
    uint8_t         auPadding[24];
# if HC_ARCH_BITS != 64
#  error HC_ARCH_BITS not defined or unsupported
# endif
} CPUMHOSTCTX;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSizeAlignment(CPUMHOSTCTX, 64);
# endif
/** Pointer to the saved host CPU state. */
typedef CPUMHOSTCTX *PCPUMHOSTCTX;
#endif /* RT_ARCH_AMD64 */


#if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
/**
 * The hypervisor context CPU state (just DRx left now).
 */
typedef struct CPUMHYPERCTX
{
    /** Debug registers.
     * @remarks DR4 and DR5 should not be used since they are aliases for
     *          DR6 and DR7 respectively on both AMD and Intel CPUs.
     * @remarks DR8-15 are currently not supported by AMD or Intel, so
     *          neither do we.
     */
    uint64_t        dr[8];
    /** @todo eliminiate the rest.   */
    uint64_t        cr3;
    uint64_t        au64Padding[7];
} CPUMHYPERCTX;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSizeAlignment(CPUMHYPERCTX, 64);
# endif
/** Pointer to the hypervisor context CPU state. */
typedef CPUMHYPERCTX *PCPUMHYPERCTX;
#endif


/**
 * CPUM Data (part of VM)
 */
typedef struct CPUM
{
    /** Guest CPU feature information.
     * Externaly visible via that VM structure, aligned with HostFeatures. */
    CPUMFEATURES            GuestFeatures;
    /** Host CPU feature information.
     * Externaly visible via the VM structure, aligned on 64-byte boundrary. */
    CPUHOSTFEATURES         HostFeatures;

    /** The (more) portable CPUID level. */
    uint8_t                 u8PortableCpuIdLevel;
    /** Indicates that a state restore is pending.
     * This is used to verify load order dependencies (PGM). */
    bool                    fPendingRestore;

#if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
    /** Whether MTRR reads report valid memory types for memory regions. */
    bool                    fMtrrRead;
    /** Whether the guest's writes to MTRRs are implemented. */
    bool                    fMtrrWrite;
    /** Use flags.
     * These flags indicates which CPU features the host uses.
     */
    uint32_t                fHostUseFlags;

    /** XSAVE/XRTOR components we can expose to the guest mask. */
    uint64_t                fXStateGuestMask;
    /** XSAVE/XRSTOR host mask.  Only state components in this mask can be exposed
     * to the guest.  This is 0 if no XSAVE/XRSTOR bits can be exposed. */
    uint64_t                fXStateHostMask;

    /** Random value we store in the reserved RFLAGS bits we don't use ourselves so
     *  we can detect corruption. */
    uint64_t                fReservedRFlagsCookie;

    /** Guest CPU info. */
    CPUMINFO                GuestInfo;

    /** The standard set of CpuId leaves. */
    CPUMCPUID               aGuestCpuIdPatmStd[6];
    /** The extended set of CpuId leaves. */
    CPUMCPUID               aGuestCpuIdPatmExt[10];
    /** The centaur set of CpuId leaves. */
    CPUMCPUID               aGuestCpuIdPatmCentaur[4];

    /** @name MSR statistics.
     * @{ */
    STAMCOUNTER             cMsrWrites;
    STAMCOUNTER             cMsrWritesToIgnoredBits;
    STAMCOUNTER             cMsrWritesRaiseGp;
    STAMCOUNTER             cMsrWritesUnknown;
    STAMCOUNTER             cMsrReads;
    STAMCOUNTER             cMsrReadsRaiseGp;
    STAMCOUNTER             cMsrReadsUnknown;
    /** @} */

#elif defined(VBOX_VMM_TARGET_ARMV8)
    /** The initial exception level (EL) to start the CPU after a reset,
     * should be either ARMV8_AARCH64_EL_1 or ARMV8_AARCH64_EL_2 for nested virtualization. */
    uint8_t                 bResetEl;
    uint8_t                 abPadding0[5];

    /** The reset value of the program counter. */
    uint64_t                u64ResetPc;

    /** Guest CPU info. */
    CPUMINFO                GuestInfo;
    /** Guest CPU ID registers. */
    CPUMARMV8IDREGS         GuestIdRegs;

    /** @name System register statistics.
     * @{ */
    STAMCOUNTER             cSysRegWrites;
    STAMCOUNTER             cSysRegWritesToIgnoredBits;
    STAMCOUNTER             cSysRegWritesRaiseExcp;
    STAMCOUNTER             cSysRegWritesUnknown;
    STAMCOUNTER             cSysRegReads;
    STAMCOUNTER             cSysRegReadsRaiseExcp;
    STAMCOUNTER             cSysRegReadsUnknown;
    /** @} */
#endif

#ifdef RT_ARCH_ARM64
    /** Host system registers used for identification. */
    PSUPARMSYSREGVAL        paHostIdRegs;
    /** Number of registers in paHostSysRegs.   */
    uint32_t                cHostIdRegs;

    /** Host CPU ID registers. */
    CPUMARMV8IDREGS         HostIdRegs;

#elif defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    /** The host MXCSR mask (determined at init). */
    uint32_t                fHostMxCsrMask;
#endif
} CPUM;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileMemberOffset(CPUM, GuestFeatures, 0);
AssertCompileMemberOffset(CPUM, HostFeatures, 64);
AssertCompileMemberOffset(CPUM, u8PortableCpuIdLevel, 128);
#endif
/** Pointer to the CPUM instance data residing in the shared VM structure. */
typedef CPUM *PCPUM;

/**
 * CPUM Data (part of VMCPU)
 */
typedef struct CPUMCPU
{
    /** Guest context.
     * Aligned on a 64-byte boundary. */
    CPUMCTX                 Guest;
#if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
    /** Guest context - misc MSRs
     * Aligned on a 64-byte boundary. */
    CPUMCTXMSRS             GuestMsrs;
#endif
#ifdef RT_ARCH_AMD64
    /** Saved host context.  Only valid while inside RC or HM contexts.
     * Must be aligned on a 64-byte boundary. */
    CPUMHOSTCTX             Host;
#endif

    /** Use flags.
     * These flags indicates both what is to be used and what has been used. */
    uint32_t                fUseFlags;

    /** Changed flags.
     * These flags indicates to REM (and others) which important guest
     * registers which has been changed since last time the flags were cleared.
     * See the CPUM_CHANGED_* defines for what we keep track of.
     *
     * @todo Obsolete, but will probably be refactored so keep it for reference. */
    uint32_t                fChanged;

#if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
    /** Nested VMX: VMX-preemption timer. */
    TMTIMERHANDLE           hNestedVmxPreemptTimer;
    /** Whether the X86_CPUID_FEATURE_EDX_APIC and X86_CPUID_AMD_FEATURE_EDX_APIC
     *  (?) bits are visible or not.  (The APIC is responsible for setting this
     *  when loading state, so we won't save it.) */
    bool                    fCpuIdApicFeatureVisible;
    uint8_t                 abPadding[7+8];

    /** Old hypervisor context, only used for combined DRx values now.
     * Must be aligned on a 64-byte boundary. */
    CPUMHYPERCTX            Hyper;
#endif

#ifdef VBOX_WITH_CRASHDUMP_MAGIC
    uint8_t                 aMagic[56];
    uint64_t                uMagic;
#endif
} CPUMCPU;
#ifndef VBOX_FOR_DTRACE_LIB
# ifdef RT_ARCH_AMD64
AssertCompileMemberAlignment(CPUMCPU, Host, 64);
# endif
# if defined(VBOX_VMM_TARGET_X86)
AssertCompileAdjacentMembers(CPUMCPU, Guest, GuestMsrs); /* HACK ALERT! HMR0A.asm makes this ASSUMPTION in the SVM RUN code! */
# endif
#endif
/** Pointer to the CPUMCPU instance data residing in the shared VMCPU structure. */
typedef CPUMCPU *PCPUMCPU;

#ifndef VBOX_FOR_DTRACE_LIB
RT_C_DECLS_BEGIN

# if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
PCPUMCPUIDLEAF      cpumCpuIdGetLeaf(PVM pVM, uint32_t uLeaf);
PCPUMCPUIDLEAF      cpumCpuIdGetLeafEx(PVM pVM, uint32_t uLeaf, uint32_t uSubLeaf, bool *pfExactSubLeafHit);
# endif
# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86)
PCPUMCPUIDLEAF      cpumCpuIdGetLeafInt(PCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, uint32_t uLeaf, uint32_t uSubLeaf);
PCPUMCPUIDLEAF      cpumCpuIdEnsureSpace(PVM pVM, PCPUMCPUIDLEAF *ppaLeaves, uint32_t cLeaves);
#  ifdef VBOX_STRICT
void                cpumCpuIdAssertOrder(PCPUMCPUIDLEAF paLeaves, uint32_t cLeaves);
#  endif
int                 cpumCpuIdExplodeFeaturesX86(PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, CPUMFEATURESX86 *pFeatures);
void                cpumCpuIdExplodeFeaturesX86SetSummaryBits(CPUMFEATURESX86 *pFeatures);
DECLHIDDEN(void)    cpumCpuIdExplodeFeaturesX86Vmx(struct VMXMSRS const *pVmxMsrs, CPUMFEATURESX86 *pFeatures);
DECLHIDDEN(void)    cpumCpuIdExplodeFeaturesX86VmxFromSupMsrs(PCSUPHWVIRTMSRS pMsrs, CPUMFEATURESX86 *pFeatures);
void                cpumCpuIdExplodeArchCapabilities(CPUMFEATURESX86 *pFeatures, bool fHasArchCap, uint64_t fArchVal);
# endif /* defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64) || defined(VBOX_VMM_TARGET_X86) */
# if defined(RT_ARCH_ARM64) || defined(VBOX_VMM_TARGET_ARMV8)
DECLHIDDEN(int)     cpumCpuIdExplodeFeaturesArmV8FromIdRegs(PCCPUMARMV8IDREGS pIdRegs, CPUMFEATURESARMV8 *pFeatures);
DECLHIDDEN(int)     cpumCpuIdExplodeFeaturesArmV8FromSysRegs(PCSUPARMSYSREGVAL paSysRegs, uint32_t cSysRegs,
                                                             CPUMFEATURESARMV8 *pFeatures);
# endif


# ifdef IN_RING3

/** @name Per-target functions (lives in target specific source files).
 * @{  */
/**
 * Called by CPUMR3Init to do target specific initializations.
 */
#  if defined(VBOX_VMM_TARGET_X86)
DECLHIDDEN(int)     cpumR3InitTargetX86(PVM pVM, PCSUPHWVIRTMSRS pHostMsrs);
#  else
DECLHIDDEN(int)     cpumR3InitTarget(PVM pVM);
#  endif
/**
 * Called by CPUMR3InitCompleted when ring-3 init is completed.
 */
DECLHIDDEN(int)     cpumR3InitCompletedRing3Target(PVM pVM);

/**
 * Performs target specific termination cleanups at the end of CPUMR3Term().
 */
DECLHIDDEN(int)     cpumR3TermTarget(PVM pVM);

/**
 * @callback_method_impl{FNSSMINTLIVEEXEC,
 * Implemented directly in target specific source file -
 * returns VINF_SSM_DONT_CALL_AGAIN.}
 */
DECLCALLBACK(int)   cpumR3LiveExecTarget(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass);

/**
 * @callback_method_impl{FNSSMINTSAVEEXEC,
 * Implemented directly in target specific source file.}
 */
DECLCALLBACK(int)   cpumR3SaveExecTarget(PVM pVM, PSSMHANDLE pSSM);

/**
 * @callback_method_impl{FNSSMINTLOADEXEC}
 */
DECLCALLBACK(int)   cpumR3LoadExecTarget(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);

/**
 * Target specific done-loading function, called by cpumR3LoadDone.
 */
DECLHIDDEN(int)     cpumR3LoadDoneTarget(PVM pVM, PSSMHANDLE pSSM);

/** What kind of cpu info dump to perform.*/
typedef enum CPUMDUMPTYPE { CPUMDUMPTYPE_TERSE = 1, CPUMDUMPTYPE_DEFAULT, CPUMDUMPTYPE_VERBOSE } CPUMDUMPTYPE;
/**
 * Dump guest registers (target specific).
 */
DECLHIDDEN(void)    cpumR3InfoOneTarget(PVM pVM, PCVMCPU pVCpu, PCDBGFINFOHLP pHlp, CPUMDUMPTYPE enmType);

/**
 * Called by CPUMR3LogCpuIdAndMsrFeatures to do target specific work.
 *
 * @param   pVM                 The cross context VM structure.
 */
DECLHIDDEN(void)    cpumR3LogCpuIdAndMsrFeaturesTarget(PVM pVM);

/**
 * Initializes the debugger related sides of the CPUM component.
 *
 * Called by CPUMR3Init.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 */
DECLHIDDEN(int)     cpumR3DbgInitTarget(PVM pVM);
/** @} */


#  if defined(VBOX_VMM_TARGET_X86)
DECLCALLBACK(void)  cpumR3InfoGuestHwvirt(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLCALLBACK(void)  cpumR3InfoHyper(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
#  endif
#  if defined(VBOX_VMM_TARGET_ARMV8)
DECLHIDDEN(int)     cpumR3SysRegStrictInitChecks(void);
#  elif defined(VBOX_VMM_TARGET_X86)
int                 cpumR3InitCpuIdAndMsrs(PVM pVM, PCSUPHWVIRTMSRS pHostMsrs);
DECLHIDDEN(void)    cpumR3InitVmxGuestFeaturesAndMsrs(PVM pVM, PCFGMNODE pCpumCfg, PCSUPHWVIRTMSRS pHostMsrs,
                                                      PVMXMSRS pGuestVmxMsrs);
void                cpumR3CpuIdRing3InitDone(PVM pVM);
#  endif
void                cpumR3SaveCpuId(PVM pVM, PSSMHANDLE pSSM);
#  ifdef VBOX_VMM_TARGET_X86
int                 cpumR3LoadCpuIdX86(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, PCCPUMMSRS pGuestMsrs);
int                 cpumR3LoadCpuIdPre32(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion);
#  elif defined(VBOX_VMM_TARGET_ARMV8)
int                 cpumR3LoadCpuIdArmV8(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion);
DECLCALLBACK(void)  cpumR3CpuFeatInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
#  endif
DECLCALLBACK(void)  cpumR3CpuIdInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLHIDDEN(void)    cpumR3InfoParseArg(const char *pszArgs, CPUMDUMPTYPE *penmType, const char **ppszComment);

int                 cpumR3DbGetCpuInfo(const char *pszName, PCPUMINFO pInfo);
#  ifdef VBOX_VMM_TARGET_X86
DECLHIDDEN(void)    cpumR3InfoFormatFlagsX86(char *pszEFlags, uint32_t efl);
int                 cpumR3MsrRangesInsert(PVM pVM, PCPUMMSRRANGE *ppaMsrRanges, uint32_t *pcMsrRanges, PCCPUMMSRRANGE pNewRange);
DECLHIDDEN(int)     cpumR3MsrReconcileWithCpuId(PVM pVM, bool fForceFlushCmd, bool fForceSpecCtrl);
int                 cpumR3MsrApplyFudge(PVM pVM);
int                 cpumR3MsrRegStats(PVM pVM);
int                 cpumR3MsrStrictInitChecks(void);
PCPUMMSRRANGE       cpumLookupMsrRange(PVM pVM, uint32_t idMsr);
#  endif
# endif /* IN_RING3 */

# ifdef IN_RING0
#  if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
DECLASM(int)        cpumR0SaveHostRestoreGuestFPUState(PCPUMCPU pCPUM);
DECLASM(void)       cpumR0SaveGuestRestoreHostFPUState(PCPUMCPU pCPUM);
#   if ARCH_BITS == 32 && defined(VBOX_WITH_64_BITS_GUESTS)
DECLASM(void)       cpumR0RestoreHostFPUState(PCPUMCPU pCPUM);
#   endif
#  endif
# endif

# if defined(IN_RC) || defined(IN_RING0)
#  if defined(VBOX_VMM_TARGET_X86) /** @todo temporary: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
DECLASM(int)        cpumRZSaveHostFPUState(PCPUMCPU pCPUM);
DECLASM(void)       cpumRZSaveGuestFpuState(PCPUMCPU pCPUM, bool fLeaveFpuAccessible);
DECLASM(void)       cpumRZSaveGuestSseRegisters(PCPUMCPU pCPUM);
DECLASM(void)       cpumRZSaveGuestAvxRegisters(PCPUMCPU pCPUM);
#  endif
# endif

RT_C_DECLS_END
#endif /* !VBOX_FOR_DTRACE_LIB */

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_CPUMInternal_h */

