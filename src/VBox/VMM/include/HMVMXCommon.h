/* $Id: HMVMXCommon.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HM/VMX - Internal header file for sharing common bits between the
 *          VMX template code (which is also used with NEM on darwin) and HM.
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

#ifndef VMM_INCLUDED_SRC_include_HMVMXCommon_h
#define VMM_INCLUDED_SRC_include_HMVMXCommon_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>

RT_C_DECLS_BEGIN


/** @defgroup grp_hm_int       Internal
 * @ingroup grp_hm
 * @internal
 * @{
 */

/** @name HM_CHANGED_XXX
 * HM CPU-context changed flags.
 *
 * These flags are used to keep track of which registers and state has been
 * modified since they were imported back into the guest-CPU context.
 *
 * @{
 */
#define HM_CHANGED_HOST_CONTEXT                  UINT64_C(0x0000000000000001)
#define HM_CHANGED_GUEST_RIP                     UINT64_C(0x0000000000000004)
#define HM_CHANGED_GUEST_RFLAGS                  UINT64_C(0x0000000000000008)

#define HM_CHANGED_GUEST_RAX                     UINT64_C(0x0000000000000010)
#define HM_CHANGED_GUEST_RCX                     UINT64_C(0x0000000000000020)
#define HM_CHANGED_GUEST_RDX                     UINT64_C(0x0000000000000040)
#define HM_CHANGED_GUEST_RBX                     UINT64_C(0x0000000000000080)
#define HM_CHANGED_GUEST_RSP                     UINT64_C(0x0000000000000100)
#define HM_CHANGED_GUEST_RBP                     UINT64_C(0x0000000000000200)
#define HM_CHANGED_GUEST_RSI                     UINT64_C(0x0000000000000400)
#define HM_CHANGED_GUEST_RDI                     UINT64_C(0x0000000000000800)
#define HM_CHANGED_GUEST_R8_R15                  UINT64_C(0x0000000000001000)
#define HM_CHANGED_GUEST_GPRS_MASK               UINT64_C(0x0000000000001ff0)

#define HM_CHANGED_GUEST_ES                      UINT64_C(0x0000000000002000)
#define HM_CHANGED_GUEST_CS                      UINT64_C(0x0000000000004000)
#define HM_CHANGED_GUEST_SS                      UINT64_C(0x0000000000008000)
#define HM_CHANGED_GUEST_DS                      UINT64_C(0x0000000000010000)
#define HM_CHANGED_GUEST_FS                      UINT64_C(0x0000000000020000)
#define HM_CHANGED_GUEST_GS                      UINT64_C(0x0000000000040000)
#define HM_CHANGED_GUEST_SREG_MASK               UINT64_C(0x000000000007e000)

#define HM_CHANGED_GUEST_GDTR                    UINT64_C(0x0000000000080000)
#define HM_CHANGED_GUEST_IDTR                    UINT64_C(0x0000000000100000)
#define HM_CHANGED_GUEST_LDTR                    UINT64_C(0x0000000000200000)
#define HM_CHANGED_GUEST_TR                      UINT64_C(0x0000000000400000)
#define HM_CHANGED_GUEST_TABLE_MASK              UINT64_C(0x0000000000780000)

#define HM_CHANGED_GUEST_CR0                     UINT64_C(0x0000000000800000)
#define HM_CHANGED_GUEST_CR2                     UINT64_C(0x0000000001000000)
#define HM_CHANGED_GUEST_CR3                     UINT64_C(0x0000000002000000)
#define HM_CHANGED_GUEST_CR4                     UINT64_C(0x0000000004000000)
#define HM_CHANGED_GUEST_CR_MASK                 UINT64_C(0x0000000007800000)

#define HM_CHANGED_GUEST_APIC_TPR                UINT64_C(0x0000000008000000)
#define HM_CHANGED_GUEST_EFER_MSR                UINT64_C(0x0000000010000000)

#define HM_CHANGED_GUEST_DR0_DR3                 UINT64_C(0x0000000020000000)
#define HM_CHANGED_GUEST_DR6                     UINT64_C(0x0000000040000000)
#define HM_CHANGED_GUEST_DR7                     UINT64_C(0x0000000080000000)
#define HM_CHANGED_GUEST_DR_MASK                 UINT64_C(0x00000000e0000000)

#define HM_CHANGED_GUEST_X87                     UINT64_C(0x0000000100000000)
#define HM_CHANGED_GUEST_SSE_AVX                 UINT64_C(0x0000000200000000)
#define HM_CHANGED_GUEST_OTHER_XSAVE             UINT64_C(0x0000000400000000)
#define HM_CHANGED_GUEST_XCRx                    UINT64_C(0x0000000800000000)

#define HM_CHANGED_GUEST_KERNEL_GS_BASE          UINT64_C(0x0000001000000000)
#define HM_CHANGED_GUEST_SYSCALL_MSRS            UINT64_C(0x0000002000000000)
#define HM_CHANGED_GUEST_SYSENTER_CS_MSR         UINT64_C(0x0000004000000000)
#define HM_CHANGED_GUEST_SYSENTER_EIP_MSR        UINT64_C(0x0000008000000000)
#define HM_CHANGED_GUEST_SYSENTER_ESP_MSR        UINT64_C(0x0000010000000000)
#define HM_CHANGED_GUEST_SYSENTER_MSR_MASK       UINT64_C(0x000001c000000000)
#define HM_CHANGED_GUEST_TSC_AUX                 UINT64_C(0x0000020000000000)
#define HM_CHANGED_GUEST_OTHER_MSRS              UINT64_C(0x0000040000000000)
#define HM_CHANGED_GUEST_ALL_MSRS                (  HM_CHANGED_GUEST_EFER              \
                                                  | HM_CHANGED_GUEST_KERNEL_GS_BASE    \
                                                  | HM_CHANGED_GUEST_SYSCALL_MSRS      \
                                                  | HM_CHANGED_GUEST_SYSENTER_MSR_MASK \
                                                  | HM_CHANGED_GUEST_TSC_AUX           \
                                                  | HM_CHANGED_GUEST_OTHER_MSRS)

#define HM_CHANGED_GUEST_HWVIRT                  UINT64_C(0x0000080000000000)
#define HM_CHANGED_GUEST_MASK                    UINT64_C(0x00000ffffffffffc)

#define HM_CHANGED_KEEPER_STATE_MASK             UINT64_C(0xffff000000000000)

#define HM_CHANGED_VMX_XCPT_INTERCEPTS           UINT64_C(0x0001000000000000)
#define HM_CHANGED_VMX_GUEST_AUTO_MSRS           UINT64_C(0x0002000000000000)
#define HM_CHANGED_VMX_GUEST_LAZY_MSRS           UINT64_C(0x0004000000000000)
#define HM_CHANGED_VMX_ENTRY_EXIT_CTLS           UINT64_C(0x0008000000000000)
#define HM_CHANGED_VMX_MASK                      UINT64_C(0x000f000000000000)
#define HM_CHANGED_VMX_HOST_GUEST_SHARED_STATE   (  HM_CHANGED_GUEST_DR_MASK \
                                                  | HM_CHANGED_VMX_GUEST_LAZY_MSRS)

#define HM_CHANGED_SVM_XCPT_INTERCEPTS           UINT64_C(0x0001000000000000)
#define HM_CHANGED_SVM_MASK                      UINT64_C(0x0001000000000000)
#define HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE   HM_CHANGED_GUEST_DR_MASK

#define HM_CHANGED_ALL_GUEST                     (  HM_CHANGED_GUEST_MASK \
                                                  | HM_CHANGED_KEEPER_STATE_MASK)

/** Mask of what state might have changed when IEM raised an exception.
 *  This is a based on IEM_CPUMCTX_EXTRN_XCPT_MASK. */
#define HM_CHANGED_RAISED_XCPT_MASK              (  HM_CHANGED_GUEST_GPRS_MASK  \
                                                  | HM_CHANGED_GUEST_RIP        \
                                                  | HM_CHANGED_GUEST_RFLAGS     \
                                                  | HM_CHANGED_GUEST_SS         \
                                                  | HM_CHANGED_GUEST_CS         \
                                                  | HM_CHANGED_GUEST_CR0        \
                                                  | HM_CHANGED_GUEST_CR3        \
                                                  | HM_CHANGED_GUEST_CR4        \
                                                  | HM_CHANGED_GUEST_APIC_TPR   \
                                                  | HM_CHANGED_GUEST_EFER_MSR   \
                                                  | HM_CHANGED_GUEST_DR7        \
                                                  | HM_CHANGED_GUEST_CR2        \
                                                  | HM_CHANGED_GUEST_SREG_MASK  \
                                                  | HM_CHANGED_GUEST_TABLE_MASK)

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
/** Mask of what state might have changed when \#VMEXIT is emulated. */
# define HM_CHANGED_SVM_VMEXIT_MASK              (  HM_CHANGED_GUEST_RSP         \
                                                  | HM_CHANGED_GUEST_RAX         \
                                                  | HM_CHANGED_GUEST_RIP         \
                                                  | HM_CHANGED_GUEST_RFLAGS      \
                                                  | HM_CHANGED_GUEST_CS          \
                                                  | HM_CHANGED_GUEST_SS          \
                                                  | HM_CHANGED_GUEST_DS          \
                                                  | HM_CHANGED_GUEST_ES          \
                                                  | HM_CHANGED_GUEST_GDTR        \
                                                  | HM_CHANGED_GUEST_IDTR        \
                                                  | HM_CHANGED_GUEST_CR_MASK     \
                                                  | HM_CHANGED_GUEST_EFER_MSR    \
                                                  | HM_CHANGED_GUEST_DR6         \
                                                  | HM_CHANGED_GUEST_DR7         \
                                                  | HM_CHANGED_GUEST_OTHER_MSRS  \
                                                  | HM_CHANGED_GUEST_HWVIRT      \
                                                  | HM_CHANGED_SVM_MASK          \
                                                  | HM_CHANGED_GUEST_APIC_TPR)

/** Mask of what state might have changed when VMRUN is emulated. */
# define HM_CHANGED_SVM_VMRUN_MASK               HM_CHANGED_SVM_VMEXIT_MASK
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/** Mask of what state might have changed when VM-exit is emulated.
 *
 *  This is currently unused, but keeping it here in case we can get away a bit more
 *  fine-grained state handling.
 *
 *  @note Update IEM_CPUMCTX_EXTRN_VMX_VMEXIT_MASK when this changes. */
# define HM_CHANGED_VMX_VMEXIT_MASK             (  HM_CHANGED_GUEST_CR0 | HM_CHANGED_GUEST_CR3 | HM_CHANGED_GUEST_CR4 \
                                                 | HM_CHANGED_GUEST_DR7 | HM_CHANGED_GUEST_DR6 \
                                                 | HM_CHANGED_GUEST_EFER_MSR \
                                                 | HM_CHANGED_GUEST_SYSENTER_MSR_MASK \
                                                 | HM_CHANGED_GUEST_OTHER_MSRS    /* for PAT MSR */ \
                                                 | HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RSP | HM_CHANGED_GUEST_RFLAGS \
                                                 | HM_CHANGED_GUEST_SREG_MASK \
                                                 | HM_CHANGED_GUEST_TR \
                                                 | HM_CHANGED_GUEST_LDTR | HM_CHANGED_GUEST_GDTR | HM_CHANGED_GUEST_IDTR \
                                                 | HM_CHANGED_GUEST_HWVIRT )
#endif
/** @} */


/** Maximum number of exit reason statistics counters. */
#define MAX_EXITREASON_STAT                        0x100
#define MASK_EXITREASON_STAT                       0xff
#define MASK_INJECT_IRQ_STAT                       0xff


/**
 * HM event.
 *
 * VT-x and AMD-V common event injection structure.
 */
typedef struct HMEVENT
{
    /** Whether the event is pending. */
    uint32_t        fPending;
    /** The error-code associated with the event. */
    uint32_t        u32ErrCode;
    /** The length of the instruction in bytes (only relevant for software
     *  interrupts or software exceptions). */
    uint32_t        cbInstr;
    /** Alignment. */
    uint32_t        u32Padding;
    /** The encoded event (VM-entry interruption-information for VT-x or EVENTINJ
     *  for SVM). */
    uint64_t        u64IntInfo;
    /** Guest virtual address if this is a page-fault event. */
    RTGCUINTPTR     GCPtrFaultAddress;
} HMEVENT;
/** Pointer to a HMEVENT struct. */
typedef HMEVENT *PHMEVENT;
/** Pointer to a const HMEVENT struct. */
typedef const HMEVENT *PCHMEVENT;
AssertCompileSizeAlignment(HMEVENT, 8);

/** Initializer for a HMEVENT structure with    */
#define HMEVENT_INIT_ONLY_INT_INFO(a_uIntInfo) { 0, 0, 0, 0, (a_uIntInfo), 0 }

/**
 * VMX VMCS information, shared.
 *
 * This structure provides information maintained for and during the executing of a
 * guest (or nested-guest) VMCS (VM control structure) using hardware-assisted VMX.
 *
 * Note! The members here are ordered and aligned based on estimated frequency of
 * usage and grouped to fit within a cache line in hot code paths. Even subtle
 * changes here have a noticeable effect in the bootsector benchmarks. Modify with
 * care.
 */
typedef struct VMXVMCSINFOSHARED
{
    /** @name Real-mode emulation state.
     * @{ */
    /** Set if guest was executing in real mode (extra checks). */
    bool                        fWasInRealMode;
    /** Padding. */
    bool                        afPadding0[7];
    struct
    {
        X86DESCATTR             AttrCS;
        X86DESCATTR             AttrDS;
        X86DESCATTR             AttrES;
        X86DESCATTR             AttrFS;
        X86DESCATTR             AttrGS;
        X86DESCATTR             AttrSS;
        X86EFLAGS               Eflags;
        bool                    fRealOnV86Active;
        bool                    afPadding1[3];
    } RealMode;
    /** @} */

    /** @name LBR MSR data.
     *  @{ */
    /** List of LastBranch-From-IP MSRs. */
    uint64_t                    au64LbrFromIpMsr[32];
    /** List of LastBranch-To-IP MSRs. */
    uint64_t                    au64LbrToIpMsr[32];
    /** List of LastBranch-Info MSRs. */
    uint64_t                    au64LbrInfoMsr[32];
    /** The MSR containing the index to the most recent branch record.  */
    uint64_t                    u64LbrTosMsr;
    /** The MSR containing the last event record from IP value. */
    uint64_t                    u64LerFromIpMsr;
    /** The MSR containing the last event record to IP value. */
    uint64_t                    u64LerToIpMsr;
    /** @} */
} VMXVMCSINFOSHARED;
/** Pointer to a VMXVMCSINFOSHARED struct.  */
typedef VMXVMCSINFOSHARED *PVMXVMCSINFOSHARED;
/** Pointer to a const VMXVMCSINFOSHARED struct. */
typedef const VMXVMCSINFOSHARED *PCVMXVMCSINFOSHARED;
AssertCompileSizeAlignment(VMXVMCSINFOSHARED, 8);


/**
 * VMX VMCS information, ring-0 only.
 *
 * This structure provides information maintained for and during the executing of a
 * guest (or nested-guest) VMCS (VM control structure) using hardware-assisted VMX.
 *
 * Note! The members here are ordered and aligned based on estimated frequency of
 * usage and grouped to fit within a cache line in hot code paths. Even subtle
 * changes here have a noticeable effect in the bootsector benchmarks. Modify with
 * care.
 */
typedef struct VMXVMCSINFO
{
    /** Pointer to the bits we share with ring-3. */
    R3R0PTRTYPE(PVMXVMCSINFOSHARED) pShared;

    /** @name Auxiliary information.
     * @{ */
    /** Host-physical address of the EPTP. */
    RTHCPHYS                    HCPhysEPTP;
    /** The VMCS launch state, see VMX_V_VMCS_LAUNCH_STATE_XXX. */
    uint32_t                    fVmcsState;
    /** The VMCS launch state of the shadow VMCS, see VMX_V_VMCS_LAUNCH_STATE_XXX. */
    uint32_t                    fShadowVmcsState;
    /** The host CPU for which its state has been exported to this VMCS. */
    RTCPUID                     idHostCpuState;
    /** The host CPU on which we last executed this VMCS. */
    RTCPUID                     idHostCpuExec;
    /** Number of guest MSRs in the VM-entry MSR-load area. */
    uint32_t                    cEntryMsrLoad;
    /** Number of guest MSRs in the VM-exit MSR-store area. */
    uint32_t                    cExitMsrStore;
    /** Number of host MSRs in the VM-exit MSR-load area. */
    uint32_t                    cExitMsrLoad;
    /** @} */

    /** @name Cache of execution related VMCS fields.
     *  @{ */
    /** Pin-based VM-execution controls. */
    uint32_t                    u32PinCtls;
    /** Processor-based VM-execution controls. */
    uint32_t                    u32ProcCtls;
    /** Secondary processor-based VM-execution controls. */
    uint32_t                    u32ProcCtls2;
    /** Tertiary processor-based VM-execution controls. */
    uint64_t                    u64ProcCtls3;
    /** VM-entry controls. */
    uint32_t                    u32EntryCtls;
    /** VM-exit controls. */
    uint32_t                    u32ExitCtls;
    /** Exception bitmap. */
    uint32_t                    u32XcptBitmap;
    /** Page-fault exception error-code mask. */
    uint32_t                    u32XcptPFMask;
    /** Page-fault exception error-code match. */
    uint32_t                    u32XcptPFMatch;
    /** Padding. */
    uint32_t                    u32Alignment0;
    /** TSC offset. */
    uint64_t                    u64TscOffset;
    /** VMCS link pointer. */
    uint64_t                    u64VmcsLinkPtr;
    /** CR0 guest/host mask. */
    uint64_t                    u64Cr0Mask;
    /** CR4 guest/host mask. */
    uint64_t                    u64Cr4Mask;
#ifndef IN_NEM_DARWIN
    /** Current VMX_VMCS_HOST_RIP value (only used in HMR0A.asm). */
    uint64_t                    uHostRip;
    /** Current VMX_VMCS_HOST_RSP value (only used in HMR0A.asm). */
    uint64_t                    uHostRsp;
#endif
    /** @} */

    /** @name Host-virtual address of VMCS and related data structures.
     *  @{ */
    /** The VMCS. */
    R3R0PTRTYPE(void *)         pvVmcs;
    /** The shadow VMCS. */
    R3R0PTRTYPE(void *)         pvShadowVmcs;
    /** The virtual-APIC page. */
    R3R0PTRTYPE(uint8_t *)      pbVirtApic;
    /** The MSR bitmap. */
    R3R0PTRTYPE(void *)         pvMsrBitmap;
    /** The VM-entry MSR-load area. */
    R3R0PTRTYPE(void *)         pvGuestMsrLoad;
    /** The VM-exit MSR-store area. */
    R3R0PTRTYPE(void *)         pvGuestMsrStore;
    /** The VM-exit MSR-load area. */
    R3R0PTRTYPE(void *)         pvHostMsrLoad;
    /** @} */

#ifndef IN_NEM_DARWIN
    /** @name Host-physical address of VMCS and related data structures.
     *  @{ */
    /** The VMCS. */
    RTHCPHYS                    HCPhysVmcs;
    /** The shadow VMCS. */
    RTHCPHYS                    HCPhysShadowVmcs;
    /** The virtual APIC page. */
    RTHCPHYS                    HCPhysVirtApic;
    /** The MSR bitmap. */
    RTHCPHYS                    HCPhysMsrBitmap;
    /** The VM-entry MSR-load area. */
    RTHCPHYS                    HCPhysGuestMsrLoad;
    /** The VM-exit MSR-store area. */
    RTHCPHYS                    HCPhysGuestMsrStore;
    /** The VM-exit MSR-load area. */
    RTHCPHYS                    HCPhysHostMsrLoad;
    /** @} */

    /** @name R0-memory objects address for VMCS and related data structures.
     *  @{ */
    /** R0-memory object for VMCS and related data structures. */
    RTR0MEMOBJ                  hMemObj;
    /** @} */
#endif
} VMXVMCSINFO;
/** Pointer to a VMXVMCSINFOR0 struct.  */
typedef VMXVMCSINFO *PVMXVMCSINFO;
/** Pointer to a const VMXVMCSINFO struct. */
typedef const VMXVMCSINFO *PCVMXVMCSINFO;
AssertCompileSizeAlignment(VMXVMCSINFO, 8);
AssertCompileMemberAlignment(VMXVMCSINFO, u32PinCtls,      4);
AssertCompileMemberAlignment(VMXVMCSINFO, u64VmcsLinkPtr,  8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvVmcs,          8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvShadowVmcs,    8);
AssertCompileMemberAlignment(VMXVMCSINFO, pbVirtApic,      8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvMsrBitmap,     8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvGuestMsrLoad,  8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvGuestMsrStore, 8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvHostMsrLoad,   8);
#ifndef IN_NEM_DARWIN
AssertCompileMemberAlignment(VMXVMCSINFO, HCPhysVmcs,      8);
AssertCompileMemberAlignment(VMXVMCSINFO, hMemObj,         8);
#endif

/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_HMVMXCommon_h */

