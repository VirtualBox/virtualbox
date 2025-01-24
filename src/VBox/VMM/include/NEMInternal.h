/* $Id: NEMInternal.h 108055 2025-01-24 10:05:32Z knut.osmundsen@oracle.com $ */
/** @file
 * NEM - Internal header file.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_NEMInternal_h
#define VMM_INCLUDED_SRC_include_NEMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/cpum.h> /* For CPUMCPUVENDOR. */
#include <VBox/vmm/stam.h>
#include <VBox/vmm/vmapi.h>
#ifdef RT_OS_WINDOWS
#include <iprt/nt/hyperv.h>
#include <iprt/critsect.h>
#elif defined(RT_OS_DARWIN)
# if defined(VBOX_VMM_TARGET_ARMV8)
#  include <Hypervisor/Hypervisor.h>
# else
#  include "VMXInternal.h"
# endif
#endif

RT_C_DECLS_BEGIN


/** @defgroup grp_nem_int      Internal
 * @ingroup grp_nem
 * @internal
 * @{
 */

#if defined(VBOX_WITH_NATIVE_NEM) && !defined(VBOX_WITH_PGM_NEM_MODE)
# error "VBOX_WITH_NATIVE_NEM requires VBOX_WITH_PGM_NEM_MODE to be defined"
#endif


#ifdef RT_OS_WINDOWS
/*
 * Windows: Code configuration.
 */
/* nothing at the moment */

/**
 * Windows VID I/O control information.
 */
typedef struct NEMWINIOCTL
{
    /** The I/O control function number. */
    uint32_t    uFunction;
    uint32_t    cbInput;
    uint32_t    cbOutput;
} NEMWINIOCTL;

/** @name Windows: Our two-bit physical page state for PGMPAGE
 * @{ */
# define NEM_WIN_PAGE_STATE_NOT_SET     0
# define NEM_WIN_PAGE_STATE_UNMAPPED    1
# define NEM_WIN_PAGE_STATE_READABLE    2
# define NEM_WIN_PAGE_STATE_WRITABLE    3
/** @} */

/** Windows: Checks if a_GCPhys is subject to the limited A20 gate emulation. */
# define NEM_WIN_IS_SUBJECT_TO_A20(a_GCPhys)    ((RTGCPHYS)((a_GCPhys) - _1M) < (RTGCPHYS)_64K)
/** Windows: Checks if a_GCPhys is relevant to the limited A20 gate emulation. */
# define NEM_WIN_IS_RELEVANT_TO_A20(a_GCPhys)    \
    ( ((RTGCPHYS)((a_GCPhys) - _1M) < (RTGCPHYS)_64K) || ((RTGCPHYS)(a_GCPhys) < (RTGCPHYS)_64K) )

/** The CPUMCTX_EXTRN_XXX mask for IEM. */
# define NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM      (  IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_INHIBIT_INT \
                                                  | CPUMCTX_EXTRN_INHIBIT_NMI )
/** The CPUMCTX_EXTRN_XXX mask for IEM when raising exceptions. */
# define NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM_XCPT (IEM_CPUMCTX_EXTRN_XCPT_MASK | NEM_WIN_CPUMCTX_EXTRN_MASK_FOR_IEM)

/** @name Windows: Interrupt window flags (NEM_WIN_INTW_F_XXX).
 * @{  */
# define NEM_WIN_INTW_F_NMI             UINT8_C(0x01)
# define NEM_WIN_INTW_F_REGULAR         UINT8_C(0x02)
# define NEM_WIN_INTW_F_PRIO_MASK       UINT8_C(0x3c)
# define NEM_WIN_INTW_F_PRIO_SHIFT      2
/** @} */

#endif /* RT_OS_WINDOWS */


#ifdef RT_OS_DARWIN
# if !defined(VBOX_VMM_TARGET_ARMV8)
/** vCPU ID declaration to avoid dragging in HV headers here. */
typedef unsigned hv_vcpuid_t;
/** The HV VM memory space ID (ASID). */
typedef unsigned hv_vm_space_t;
# endif


/** @name Darwin: Our two-bit physical page state for PGMPAGE
 * @{ */
# define NEM_DARWIN_PAGE_STATE_UNMAPPED    0
# define NEM_DARWIN_PAGE_STATE_RX          1
# define NEM_DARWIN_PAGE_STATE_RW          2
# define NEM_DARWIN_PAGE_STATE_RWX         3
/** @} */

# if defined(VBOX_VMM_TARGET_ARMV8)
/** The CPUMCTX_EXTRN_XXX mask for IEM. */
#  define NEM_DARWIN_CPUMCTX_EXTRN_MASK_FOR_IEM      (  IEM_CPUMCTX_EXTRN_MUST_MASK )
# else
/** The CPUMCTX_EXTRN_XXX mask for IEM. */
#  define NEM_DARWIN_CPUMCTX_EXTRN_MASK_FOR_IEM      (  IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_INHIBIT_INT \
                                                      | CPUMCTX_EXTRN_INHIBIT_NMI )
#endif

/** The CPUMCTX_EXTRN_XXX mask for IEM when raising exceptions. */
# define NEM_DARWIN_CPUMCTX_EXTRN_MASK_FOR_IEM_XCPT (IEM_CPUMCTX_EXTRN_XCPT_MASK | NEM_DARWIN_CPUMCTX_EXTRN_MASK_FOR_IEM)


# if defined(VBOX_VMM_TARGET_ARMV8)
/**
 * MMIO2 tracking region.
 */
typedef struct
{
    /* Start of the region. */
    RTGCPHYS                    GCPhysStart;
    /** End of the region. */
    RTGCPHYS                    GCPhysLast;
    /** Whether the region was accessed since last time. */
    bool                        fDirty;
} NEMHVMMIO2REGION;
/** Pointer to a MMIO2 tracking region. */
typedef NEMHVMMIO2REGION *PNEMHVMMIO2REGION;
# endif

#endif


/** Trick to make slickedit see the static functions in the template. */
#ifndef IN_SLICKEDIT
# define NEM_TMPL_STATIC static
#else
# define NEM_TMPL_STATIC
#endif


/**
 * Generic NEM exit type enumeration for use with EMHistoryAddExit.
 *
 * On windows we've got two different set of exit types and they are both jumping
 * around the place value wise, so EM can use their values.
 *
 * @note We only have exit types for exits not covered by EM here.
 */
typedef enum NEMEXITTYPE
{
    NEMEXITTYPE_INVALID = 0,

    /* Common: */
    NEMEXITTYPE_INTTERRUPT_WINDOW,
    NEMEXITTYPE_HALT,

    /* Windows: */
    NEMEXITTYPE_UNRECOVERABLE_EXCEPTION,
    NEMEXITTYPE_INVALID_VP_REGISTER_VALUE,
    NEMEXITTYPE_XCPT_UD,
    NEMEXITTYPE_XCPT_DB,
    NEMEXITTYPE_XCPT_BP,
    NEMEXITTYPE_CANCELED,
    NEMEXITTYPE_MEMORY_ACCESS,

    /* Linux: */
    NEMEXITTYPE_INTERNAL_ERROR_EMULATION,
    NEMEXITTYPE_INTERNAL_ERROR_FATAL,
    NEMEXITTYPE_INTERRUPTED,
    NEMEXITTYPE_FAILED_ENTRY,

    /* End of valid types. */
    NEMEXITTYPE_END
} NEMEXITTYPE;


/**
 * NEM VM Instance data.
 */
typedef struct NEM
{
    /** NEM_MAGIC. */
    uint32_t                    u32Magic;

    /** Set if enabled. */
    bool                        fEnabled;
    /** Set if long mode guests are allowed. */
    bool                        fAllow64BitGuests;
    /** Set when the debug facility has breakpoints/events enabled that requires
     *  us to use the debug execution loop. */
    bool                        fUseDebugLoop;

#if defined(VBOX_VMM_TARGET_ARMV8)
    /** The PPI interrupt number of the vTimer. */
    uint32_t                    u32GicPpiVTimer;
#endif

#if defined(RT_OS_LINUX)
    /** The '/dev/kvm' file descriptor.   */
    int32_t                     fdKvm;
    /** The KVM_CREATE_VM file descriptor. */
    int32_t                     fdVm;

    /** KVM_GET_VCPU_MMAP_SIZE. */
    uint32_t                    cbVCpuMmap;
    /** KVM_CAP_NR_MEMSLOTS. */
    uint32_t                    cMaxMemSlots;
# ifdef RT_ARCH_ARM64
    /** KVM_CAP_ARM_VM_IPA_SIZE. */
    uint32_t                    cIpaBits;
# else
    /** KVM_CAP_X86_ROBUST_SINGLESTEP. */
    bool                        fRobustSingleStep;
# endif

    /** Hint where there might be a free slot. */
    uint16_t                    idPrevSlot;
    /** Memory slot ID allocation bitmap. */
    uint64_t                    bmSlotIds[_32K / 8 / sizeof(uint64_t)];

#elif defined(RT_OS_WINDOWS)
    /** Set if we've created the EMTs. */
    bool                        fCreatedEmts : 1;
# if defined(VBOX_VMM_TARGET_ARMV8)
    bool                        fHypercallExit : 1;
    bool                        fGpaAccessFaultExit : 1;
    /** Cache line flush size as a power of two. */
    uint8_t                     cPhysicalAddressWidth;
# else
    /** WHvRunVpExitReasonX64Cpuid is supported. */
    bool                        fExtendedMsrExit : 1;
    /** WHvRunVpExitReasonX64MsrAccess is supported. */
    bool                        fExtendedCpuIdExit : 1;
    /** WHvRunVpExitReasonException is supported. */
    bool                        fExtendedXcptExit : 1;
    /** Copy of WHV_CAPABILITY_FEATURES::SpeculationControl. */
    bool                        fSpeculationControl : 1;
    /** Whether to export/import IA32_SPEC_CTRL. */
    bool                        fDoIa32SpecCtrl : 1;
#  ifdef NEM_WIN_WITH_A20
    /** Set if we've started more than one CPU and cannot mess with A20. */
    bool                        fA20Fixed : 1;
    /** Set if A20 is enabled. */
    bool                        fA20Enabled : 1;
#  endif
# endif
    /** The reported CPU vendor.   */
    CPUMCPUVENDOR               enmCpuVendor;
    /** Cache line flush size as a power of two. */
    uint8_t                     cCacheLineFlushShift;
    /** The result of WHvCapabilityCodeProcessorFeatures. */
    union
    {
        /** 64-bit view. */
        uint64_t                u64;
# ifdef _WINHVAPIDEFS_H_
        /** Interpreed features. */
        WHV_PROCESSOR_FEATURES  u;
# endif
    } uCpuFeatures;

    /** The partition handle. */
# ifdef _WINHVAPIDEFS_H_
    WHV_PARTITION_HANDLE
# else
    RTHCUINTPTR
# endif
                                hPartition;
    /** The device handle for the partition, for use with Vid APIs or direct I/O
     * controls. */
    RTR3PTR                     hPartitionDevice;

    /** Number of currently mapped pages. */
    uint32_t volatile           cMappedPages;
    uint32_t                    u32Padding;
    STAMCOUNTER                 StatMapPage;
    STAMCOUNTER                 StatUnmapPage;
    STAMCOUNTER                 StatMapPageFailed;
    STAMCOUNTER                 StatUnmapPageFailed;
    STAMPROFILE                 StatProfMapGpaRange;
    STAMPROFILE                 StatProfUnmapGpaRange;
    STAMPROFILE                 StatProfMapGpaRangePage;
    STAMPROFILE                 StatProfUnmapGpaRangePage;

    /** Statistics updated by NEMR0UpdateStatistics. */
    struct
    {
        uint64_t                cPagesAvailable;
        uint64_t                cPagesInUse;
    } R0Stats;

#elif defined(RT_OS_DARWIN)
    /** Set if we've created the EMTs. */
    bool                        fCreatedEmts : 1;
    /** Set if hv_vm_create() was called successfully. */
    bool                        fCreatedVm   : 1;
    /** Set if EL2 is enabled. */
    bool                        fEl2Enabled  : 1;
# if defined(VBOX_VMM_TARGET_ARMV8)
    /** @name vTimer related state.
     * @{ */
    /** The counter frequency in Hz as obtained from CNTFRQ_EL0. */
    uint64_t                    u64CntFrqHz;
    /** The vTimer offset programmed. */
    uint64_t                    u64VTimerOff;
    /** Dirty tracking slots. */
    NEMHVMMIO2REGION            aMmio2DirtyTracking[8];
    /** The vCPU config. */
    hv_vcpu_config_t            hVCpuCfg;
    /** @} */
# else
    /** Set if hv_vm_space_create() was called successfully. */
    bool                        fCreatedAsid : 1;
    /** Set if Last Branch Record (LBR) is enabled. */
    bool                        fLbr;
    /** The ASID for this VM (only valid if fCreatedAsid is true). */
    hv_vm_space_t               uVmAsid;
    /** Number of mach time units per NS, for hv_vcpu_run_until(). */
    uint64_t                    cMachTimePerNs;
    /** Pause-loop exiting (PLE) gap in ticks. */
    uint32_t                    cPleGapTicks;
    /** Pause-loop exiting (PLE) window in ticks. */
    uint32_t                    cPleWindowTicks;

    /** The host LBR TOS (top-of-stack) MSR id. */
    uint32_t                    idLbrTosMsr;
    /** The host LBR select MSR id. */
    uint32_t                    idLbrSelectMsr;
    /** The host last event record from IP MSR id. */
    uint32_t                    idLerFromIpMsr;
    /** The host last event record to IP MSR id. */
    uint32_t                    idLerToIpMsr;

    /** The first valid host LBR branch-from-IP stack range. */
    uint32_t                    idLbrFromIpMsrFirst;
    /** The last valid host LBR branch-from-IP stack range. */
    uint32_t                    idLbrFromIpMsrLast;

    /** The first valid host LBR branch-to-IP stack range. */
    uint32_t                    idLbrToIpMsrFirst;
    /** The last valid host LBR branch-to-IP stack range. */
    uint32_t                    idLbrToIpMsrLast;

    /** The first valid host LBR info stack range. */
    uint32_t                    idLbrInfoMsrFirst;
    /** The last valid host LBR info stack range. */
    uint32_t                    idLbrInfoMsrLast;
# endif

    STAMCOUNTER                 StatMapPage;
    STAMCOUNTER                 StatUnmapPage;
    STAMCOUNTER                 StatMapPageFailed;
    STAMCOUNTER                 StatUnmapPageFailed;
#endif /* RT_OS_WINDOWS */
} NEM;
/** Pointer to NEM VM instance data. */
typedef NEM *PNEM;

/** NEM::u32Magic value. */
#define NEM_MAGIC               UINT32_C(0x004d454e)
/** NEM::u32Magic value after termination. */
#define NEM_MAGIC_DEAD          UINT32_C(0xdead1111)


/**
 * NEM VMCPU Instance data.
 */
typedef struct NEMCPU
{
    /** NEMCPU_MAGIC. */
    uint32_t                    u32Magic;
    /** Whether \#UD needs to be intercepted and presented to GIM. */
    bool                        fGIMTrapXcptUD : 1;
    /** Whether \#GP needs to be intercept for mesa driver workaround. */
    bool                        fTrapXcptGpForLovelyMesaDrv: 1;
    /** Whether we should use the debug loop because of single stepping or special
     *  debug breakpoints / events are armed. */
    bool                        fUseDebugLoop : 1;
    /** Whether we're executing a single instruction. */
    bool                        fSingleInstruction : 1;
    /** Set if we using the debug loop and wish to intercept RDTSC. */
    bool                        fDebugWantRdTscExit : 1;
    /** Whether we are currently executing in the debug loop.
     *  Mainly for assertions. */
    bool                        fUsingDebugLoop : 1;
    /** Set if we need to clear the trap flag because of single stepping. */
    bool                        fClearTrapFlag : 1;
    /** Whether we're using the hyper DR7 or guest DR7. */
    bool                        fUsingHyperDR7 : 1;
    /** Whether \#DE needs to be intercepted for GIM. */
    bool                        fGCMTrapXcptDE : 1;
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /** Set if indirect branch prediction barrier on VM exit. */
    bool                        fIbpbOnVmExit : 1;
    /** Set if indirect branch prediction barrier on VM entry. */
    bool                        fIbpbOnVmEntry : 1;
    /** Set if level 1 data cache should be flushed on VM entry. */
    bool                        fL1dFlushOnVmEntry : 1;
    /** Set if level 1 data cache should be flushed on EMT scheduling. */
    bool                        fL1dFlushOnSched : 1;
    /** Set if MDS related buffers should be cleared on VM entry. */
    bool                        fMdsClearOnVmEntry : 1;
    /** Set if MDS related buffers should be cleared on EMT scheduling. */
    bool                        fMdsClearOnSched : 1;
#endif

#if defined(RT_OS_LINUX)
    uint8_t                     abPadding[3];
    /** The KVM VCpu file descriptor. */
    int32_t                     fdVCpu;
    /** Pointer to the KVM_RUN data exchange region. */
    R3PTRTYPE(struct kvm_run *) pRun;
# if defined(VBOX_VMM_TARGET_ARMV8)
    /** The IRQ device levels from device_irq_level. */
    uint64_t                    fIrqDeviceLvls;
    /** Status of the IRQ line when last seen. */
    bool                        fIrqLastSeen;
    /** Status of the FIQ line when last seen. */
    bool                        fFiqLastSeen;
# else
    /** The MSR_IA32_APICBASE value known to KVM. */
    uint64_t                    uKvmApicBase;
#endif

    /** @name Statistics
     * @{ */
    STAMCOUNTER                 StatExitTotal;
    STAMCOUNTER                 StatExitIo;
    STAMCOUNTER                 StatExitMmio;
    STAMCOUNTER                 StatExitSetTpr;
    STAMCOUNTER                 StatExitTprAccess;
    STAMCOUNTER                 StatExitRdMsr;
    STAMCOUNTER                 StatExitWrMsr;
    STAMCOUNTER                 StatExitIrqWindowOpen;
    STAMCOUNTER                 StatExitHalt;
    STAMCOUNTER                 StatExitIntr;
    STAMCOUNTER                 StatExitHypercall;
    STAMCOUNTER                 StatExitDebug;
    STAMCOUNTER                 StatExitBusLock;
    STAMCOUNTER                 StatExitInternalErrorEmulation;
    STAMCOUNTER                 StatExitInternalErrorFatal;
# if 0
    STAMCOUNTER                 StatExitCpuId;
    STAMCOUNTER                 StatExitUnrecoverable;
    STAMCOUNTER                 StatGetMsgTimeout;
    STAMCOUNTER                 StatStopCpuSuccess;
    STAMCOUNTER                 StatStopCpuPending;
    STAMCOUNTER                 StatStopCpuPendingAlerts;
    STAMCOUNTER                 StatStopCpuPendingOdd;
    STAMCOUNTER                 StatCancelChangedState;
    STAMCOUNTER                 StatCancelAlertedThread;
# endif
    STAMCOUNTER                 StatBreakOnCancel;
    STAMCOUNTER                 StatBreakOnFFPre;
    STAMCOUNTER                 StatBreakOnFFPost;
    STAMCOUNTER                 StatBreakOnStatus;
    STAMCOUNTER                 StatFlushExitOnReturn;
    STAMCOUNTER                 StatFlushExitOnReturn1Loop;
    STAMCOUNTER                 StatFlushExitOnReturn2Loops;
    STAMCOUNTER                 StatFlushExitOnReturn3Loops;
    STAMCOUNTER                 StatFlushExitOnReturn4PlusLoops;
    STAMCOUNTER                 StatImportOnDemand;
    STAMCOUNTER                 StatImportOnReturn;
    STAMCOUNTER                 StatImportOnReturnSkipped;
    STAMCOUNTER                 StatImportPendingInterrupt;
    STAMCOUNTER                 StatExportPendingInterrupt;
    STAMCOUNTER                 StatQueryCpuTick;
    /** @} */


#elif defined(RT_OS_WINDOWS)
# ifdef VBOX_VMM_TARGET_ARMV8
    /** Flag whether the ID registers were synced to the guest context
     * (for first guest exec call on the EMT after loading the saved state). */
    bool                        fIdRegsSynced;
# else
    /** The current state of the interrupt windows (NEM_WIN_INTW_F_XXX). */
    uint8_t                     fCurrentInterruptWindows;
    /** The desired state of the interrupt windows (NEM_WIN_INTW_F_XXX). */
    uint8_t                     fDesiredInterruptWindows;
    /** Last copy of HV_X64_VP_EXECUTION_STATE::InterruptShadow. */
    bool                        fLastInterruptShadow : 1;
    uint32_t                    uPadding;
    /** The VID_MSHAGN_F_XXX flags.
     * Either VID_MSHAGN_F_HANDLE_MESSAGE | VID_MSHAGN_F_GET_NEXT_MESSAGE or zero. */
    uint32_t                    fHandleAndGetFlags;
    /** What VidMessageSlotMap returns and is used for passing exit info. */
    RTR3PTR                     pvMsgSlotMapping;
    /** The windows thread handle. */
    RTR3PTR                     hNativeThreadHandle;
# endif

    /** @name Statistics
     * @{ */
    STAMCOUNTER                 StatExitPortIo;
    STAMCOUNTER                 StatExitMemUnmapped;
    STAMCOUNTER                 StatExitMemIntercept;
    STAMCOUNTER                 StatExitHalt;
    STAMCOUNTER                 StatExitInterruptWindow;
    STAMCOUNTER                 StatExitCpuId;
    STAMCOUNTER                 StatExitMsr;
    STAMCOUNTER                 StatExitException;
    STAMCOUNTER                 StatExitExceptionBp;
    STAMCOUNTER                 StatExitExceptionDb;
    STAMCOUNTER                 StatExitExceptionGp;
    STAMCOUNTER                 StatExitExceptionGpMesa;
    STAMCOUNTER                 StatExitExceptionUd;
    STAMCOUNTER                 StatExitExceptionUdHandled;
    STAMCOUNTER                 StatExitUnrecoverable;
    STAMCOUNTER                 StatGetMsgTimeout;
    STAMCOUNTER                 StatStopCpuSuccess;
    STAMCOUNTER                 StatStopCpuPending;
    STAMCOUNTER                 StatStopCpuPendingAlerts;
    STAMCOUNTER                 StatStopCpuPendingOdd;
    STAMCOUNTER                 StatCancelChangedState;
    STAMCOUNTER                 StatCancelAlertedThread;
    STAMCOUNTER                 StatBreakOnCancel;
    STAMCOUNTER                 StatBreakOnFFPre;
    STAMCOUNTER                 StatBreakOnFFPost;
    STAMCOUNTER                 StatBreakOnStatus;
    STAMCOUNTER                 StatImportOnDemand;
    STAMCOUNTER                 StatImportOnReturn;
    STAMCOUNTER                 StatImportOnReturnSkipped;
    STAMCOUNTER                 StatQueryCpuTick;
    /** @} */

#elif defined(RT_OS_DARWIN)
# if defined(VBOX_VMM_TARGET_ARMV8)
    /** The vCPU handle associated with the EMT executing this vCPU. */
    hv_vcpu_t                   hVCpu;
    /** Pointer to the exit information structure. */
    hv_vcpu_exit_t              *pHvExit;
    /** Flag whether an event is pending. */
    bool                        fEventPending;
    /** Flag whether the vTimer got activated and is masked. */
    bool                        fVTimerActivated;
    /** Flag whether to update the vTimer offset. */
    bool                        fVTimerOffUpdate;
    /** Flag whether the ID registers were synced to the guest context
     * (for first guest exec call on the EMT after loading the saved state). */
    bool                        fIdRegsSynced;
# else
    /** The vCPU handle associated with the EMT executing this vCPU. */
    hv_vcpuid_t                 hVCpuId;

    /** @name State shared with the VT-x code.
     * @{ */
    /** An additional error code used for some gurus. */
    uint32_t                    u32HMError;
    /** The last exit-to-ring-3 reason. */
    int32_t                     rcLastExitToR3;
    /** CPU-context changed flags (see HM_CHANGED_xxx). */
    uint64_t                    fCtxChanged;

    /** The guest VMCS information. */
    VMXVMCSINFO                 VmcsInfo;

    /** VT-x data.   */
    struct HMCPUVMX
    {
        /** @name Guest information.
         * @{ */
        /** Guest VMCS information shared with ring-3. */
        VMXVMCSINFOSHARED           VmcsInfo;
        /** Nested-guest VMCS information shared with ring-3. */
        VMXVMCSINFOSHARED           VmcsInfoNstGst;
        /** Whether the nested-guest VMCS was the last current VMCS (shadow copy for ring-3).
         * @see HMR0PERVCPU::vmx.fSwitchedToNstGstVmcs  */
        bool                        fSwitchedToNstGstVmcsCopyForRing3;
        /** Whether the static guest VMCS controls has been merged with the
         *  nested-guest VMCS controls. */
        bool                        fMergedNstGstCtls;
        /** Whether the nested-guest VMCS has been copied to the shadow VMCS. */
        bool                        fCopiedNstGstToShadowVmcs;
        /** Whether flushing the TLB is required due to switching to/from the
         *  nested-guest. */
        bool                        fSwitchedNstGstFlushTlb;
        /** Alignment. */
        bool                        afAlignment0[4];
        /** Cached guest APIC-base MSR for identifying when to map the APIC-access page. */
        uint64_t                    u64GstMsrApicBase;
        /** @} */

        /** @name Error reporting and diagnostics.
         * @{ */
        /** VT-x error-reporting (mainly for ring-3 propagation). */
        struct
        {
            RTCPUID                 idCurrentCpu;
            RTCPUID                 idEnteredCpu;
            RTHCPHYS                HCPhysCurrentVmcs;
            uint32_t                u32VmcsRev;
            uint32_t                u32InstrError;
            uint32_t                u32ExitReason;
            uint32_t                u32GuestIntrState;
        } LastError;
        /** @} */
    } vmx;

    /** Event injection state. */
    HMEVENT                     Event;

    /** Current shadow paging mode for updating CR4.
     * @todo move later (@bugref{9217}).  */
    PGMMODE                     enmShadowMode;
    uint32_t                    u32TemporaryPadding;

    /** The PAE PDPEs used with Nested Paging (only valid when
     *  VMCPU_FF_HM_UPDATE_PAE_PDPES is set). */
    X86PDPE                     aPdpes[4];
    /** Pointer to the VMX statistics. */
    PVMXSTATISTICS              pVmxStats;
# endif

    /** @name Statistics
     * @{ */
    STAMCOUNTER                 StatExitAll;
    STAMCOUNTER                 StatBreakOnCancel;
    STAMCOUNTER                 StatBreakOnFFPre;
    STAMCOUNTER                 StatBreakOnFFPost;
    STAMCOUNTER                 StatBreakOnStatus;
    STAMCOUNTER                 StatImportOnDemand;
    STAMCOUNTER                 StatImportOnReturn;
    STAMCOUNTER                 StatImportOnReturnSkipped;
    STAMCOUNTER                 StatQueryCpuTick;
#ifdef VBOX_WITH_STATISTICS
    STAMPROFILEADV              StatProfGstStateImport;
    STAMPROFILEADV              StatProfGstStateExport;
#endif
    /** @} */

    /** @} */
#endif /* RT_OS_DARWIN */
} NEMCPU;
/** Pointer to NEM VMCPU instance data. */
typedef NEMCPU *PNEMCPU;

/** NEMCPU::u32Magic value. */
#define NEMCPU_MAGIC            UINT32_C(0x4d454e20)
/** NEMCPU::u32Magic value after termination. */
#define NEMCPU_MAGIC_DEAD       UINT32_C(0xdead2222)


#ifdef IN_RING0
# ifdef RT_OS_WINDOWS
/**
 * Windows: Hypercall input/ouput page info.
 */
typedef struct NEMR0HYPERCALLDATA
{
    /** Host physical address of the hypercall input/output page. */
    RTHCPHYS                    HCPhysPage;
    /** Pointer to the hypercall input/output page. */
    uint8_t                    *pbPage;
    /** Handle to the memory object of the hypercall input/output page. */
    RTR0MEMOBJ                  hMemObj;
} NEMR0HYPERCALLDATA;
/** Pointer to a Windows hypercall input/output page info. */
typedef NEMR0HYPERCALLDATA *PNEMR0HYPERCALLDATA;
# endif /* RT_OS_WINDOWS */

/**
 * NEM GVMCPU instance data.
 */
typedef struct NEMR0PERVCPU
{
    uint32_t                    uDummy;
} NEMR0PERVCPU;

/**
 * NEM GVM instance data.
 */
typedef struct NEMR0PERVM
{
    uint32_t                    uDummy;
} NEMR0PERVM;

#endif /* IN_RING*/


#ifdef IN_RING3

int     nemR3DisableCpuIsaExt(PVM pVM, const char *pszIsaExt);

int     nemR3NativeInit(PVM pVM, bool fFallback, bool fForced);
int     nemR3NativeInitAfterCPUM(PVM pVM);
int     nemR3NativeInitCompleted(PVM pVM, VMINITCOMPLETED enmWhat);
int     nemR3NativeTerm(PVM pVM);
void    nemR3NativeReset(PVM pVM);
void    nemR3NativeResetCpu(PVMCPU pVCpu, bool fInitIpi);
VBOXSTRICTRC    nemR3NativeRunGC(PVM pVM, PVMCPU pVCpu);
bool            nemR3NativeCanExecuteGuest(PVM pVM, PVMCPU pVCpu);
bool            nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable);

/**
 * Forced flag notification call from VMEmt.h.
 *
 * This is only called when pVCpu is in the VMCPUSTATE_STARTED_EXEC_NEM state.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the CPU
 *                          to be notified.
 * @param   fFlags          Notification flags, VMNOTIFYFF_FLAGS_XXX.
 */
void            nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags);

/**
 * Called by NEMR3NotifyDebugEventChanged() to let the native backend take the final decision
 * on whether to switch to the debug loop.
 *
 * @returns Final flag whether to switch to the debug loop.
 * @param   pVM             The VM cross context VM structure.
 * @param   fUseDebugLoop   The current value determined by NEMR3NotifyDebugEventChanged().
 * @thread  EMT(0)
 */
DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop);


/**
 * Called by NEMR3NotifyDebugEventChangedPerCpu() to let the native backend take the final decision
 * on whether to switch to the debug loop.
 *
 * @returns Final flag whether to switch to the debug loop.
 * @param   pVM             The VM cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   fUseDebugLoop   The current value determined by NEMR3NotifyDebugEventChangedPerCpu().
 */
DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop);

#endif /* IN_RING3 */

void    nemHCNativeNotifyHandlerPhysicalRegister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhys, RTGCPHYS cb);
void    nemHCNativeNotifyHandlerPhysicalModify(PVMCC pVM, PGMPHYSHANDLERKIND enmKind, RTGCPHYS GCPhysOld,
                                               RTGCPHYS GCPhysNew, RTGCPHYS cb, bool fRestoreAsRAM);
int     nemHCNativeNotifyPhysPageAllocated(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys, uint32_t fPageProt,
                                           PGMPAGETYPE enmType, uint8_t *pu2State);


#ifdef RT_OS_WINDOWS
/** Maximum number of pages we can map in a single NEMR0MapPages call. */
# define NEM_MAX_MAP_PAGES      ((HOST_PAGE_SIZE - RT_UOFFSETOF(HV_INPUT_MAP_GPA_PAGES, PageList)) / sizeof(HV_SPA_PAGE_NUMBER))
/** Maximum number of pages we can unmap in a single NEMR0UnmapPages call. */
# define NEM_MAX_UNMAP_PAGES    4095

#endif
/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_NEMInternal_h */

