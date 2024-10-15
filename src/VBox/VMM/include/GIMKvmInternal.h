/* $Id: GIMKvmInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - KVM, Internal header file.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_GIMKvmInternal_h
#define VMM_INCLUDED_SRC_include_GIMKvmInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/gim.h>
#include <VBox/vmm/cpum.h>


/** @name KVM base features.
 * @{
 */
/** Old, deprecated clock source available. */
#define GIM_KVM_BASE_FEAT_CLOCK_OLD                RT_BIT(0)
/** No need for artifical delays on IO operations. */
#define GIM_KVM_BASE_FEAT_NOP_IO_DELAY             RT_BIT(1)
/** MMU op supported (deprecated, unused). */
#define GIM_KVM_BASE_FEAT_MMU_OP                   RT_BIT(2)
/** Clock source available. */
#define GIM_KVM_BASE_FEAT_CLOCK                    RT_BIT(3)
/** Asynchronous page faults supported. */
#define GIM_KVM_BASE_FEAT_ASYNC_PF                 RT_BIT(4)
/** Steal time (VCPU not executing guest code time in ns) available. */
#define GIM_KVM_BASE_FEAT_STEAL_TIME               RT_BIT(5)
/** Paravirtualized EOI (end-of-interrupt) supported. */
#define GIM_KVM_BASE_FEAT_PV_EOI                   RT_BIT(6)
/** Paravirtualized spinlock (unhalting VCPU) supported. */
#define GIM_KVM_BASE_FEAT_PV_UNHALT                RT_BIT(7)
/** The TSC is stable (fixed rate, monotonic). */
#define GIM_KVM_BASE_FEAT_TSC_STABLE               RT_BIT(24)
/** @}  */


/** @name KVM MSRs.
 * @{
 */
/** Start of range 0. */
#define MSR_GIM_KVM_RANGE0_FIRST                   UINT32_C(0x11)
/** Old, deprecated wall clock. */
#define MSR_GIM_KVM_WALL_CLOCK_OLD                 UINT32_C(0x11)
/** Old, deprecated System time. */
#define MSR_GIM_KVM_SYSTEM_TIME_OLD                UINT32_C(0x12)
/** End of range 0. */
#define MSR_GIM_KVM_RANGE0_LAST                    MSR_GIM_KVM_SYSTEM_TIME_OLD

/** Start of range 1. */
#define MSR_GIM_KVM_RANGE1_FIRST                   UINT32_C(0x4b564d00)
/** Wall clock. */
#define MSR_GIM_KVM_WALL_CLOCK                     UINT32_C(0x4b564d00)
/** System time. */
#define MSR_GIM_KVM_SYSTEM_TIME                    UINT32_C(0x4b564d01)
/** Asynchronous page fault. */
#define MSR_GIM_KVM_ASYNC_PF                       UINT32_C(0x4b564d02)
/** Steal time. */
#define MSR_GIM_KVM_STEAL_TIME                     UINT32_C(0x4b564d03)
/** Paravirtualized EOI (end-of-interrupt). */
#define MSR_GIM_KVM_EOI                            UINT32_C(0x4b564d04)
/** End of range 1. */
#define MSR_GIM_KVM_RANGE1_LAST                    MSR_GIM_KVM_EOI

AssertCompile(MSR_GIM_KVM_RANGE0_FIRST <= MSR_GIM_KVM_RANGE0_LAST);
AssertCompile(MSR_GIM_KVM_RANGE1_FIRST <= MSR_GIM_KVM_RANGE1_LAST);
/** @} */

/** KVM page size.  */
#define GIM_KVM_PAGE_SIZE                          0x1000

/**
 * MMIO2 region indices.
 */
/** The system time page(s) region. */
#define GIM_KVM_SYSTEM_TIME_PAGE_REGION_IDX        UINT8_C(0)
/** The steal time page(s) region. */
#define GIM_KVM_STEAL_TIME_PAGE_REGION_IDX         UINT8_C(1)
/** The maximum region index (must be <= UINT8_MAX). */
#define GIM_KVM_REGION_IDX_MAX                     GIM_KVM_STEAL_TIME_PAGE_REGION_IDX

/**
 * KVM system-time structure (GIM_KVM_SYSTEM_TIME_FLAGS_XXX) flags.
 * See "Documentation/virtual/kvm/api.txt".
 */
/** The TSC is stable (monotonic). */
#define GIM_KVM_SYSTEM_TIME_FLAGS_TSC_STABLE       RT_BIT(0)
/** The guest VCPU has been paused by the hypervisor. */
#define GIM_KVM_SYSTEM_TIME_FLAGS_GUEST_PAUSED     RT_BIT(1)
/** */

/** @name KVM MSR - System time (MSR_GIM_KVM_SYSTEM_TIME and
 * MSR_GIM_KVM_SYSTEM_TIME_OLD).
 * @{
 */
/** The system-time enable bit. */
#define MSR_GIM_KVM_SYSTEM_TIME_ENABLE_BIT        RT_BIT_64(0)
/** Whether the system-time struct. is enabled or not. */
#define MSR_GIM_KVM_SYSTEM_TIME_IS_ENABLED(a)     RT_BOOL((a) & MSR_GIM_KVM_SYSTEM_TIME_ENABLE_BIT)
/** Guest-physical address of the system-time struct. */
#define MSR_GIM_KVM_SYSTEM_TIME_GUEST_GPA(a)      ((a) & ~MSR_GIM_KVM_SYSTEM_TIME_ENABLE_BIT)
/** @} */

/** @name KVM MSR - Wall clock (MSR_GIM_KVM_WALL_CLOCK and
 * MSR_GIM_KVM_WALL_CLOCK_OLD).
 * @{
 */
/** Guest-physical address of the wall-clock struct. */
#define MSR_GIM_KVM_WALL_CLOCK_GUEST_GPA(a)        (a)
/** @} */


/** @name KVM Hypercall operations.
 *  @{ */
#define KVM_HYPERCALL_OP_VAPIC_POLL_IRQ            1
#define KVM_HYPERCALL_OP_MMU                       2
#define KVM_HYPERCALL_OP_FEATURES                  3
#define KVM_HYPERCALL_OP_KICK_CPU                  5
/** @} */

/** @name KVM Hypercall return values.
 *  @{ */
/* Return values for hypercalls */
#define KVM_HYPERCALL_RET_SUCCESS                  0
#define KVM_HYPERCALL_RET_ENOSYS                   (uint64_t)(-1000)
#define KVM_HYPERCALL_RET_EFAULT                   (uint64_t)(-14)
#define KVM_HYPERCALL_RET_E2BIG                    (uint64_t)(-7)
#define KVM_HYPERCALL_RET_EPERM                    (uint64_t)(-1)
/** @} */

/**
 * KVM per-VCPU system-time structure.
 */
typedef struct GIMKVMSYSTEMTIME
{
    /** Version (sequence number). */
    uint32_t        u32Version;
    /** Alignment padding. */
    uint32_t        u32Padding0;
    /** TSC time stamp.  */
    uint64_t        u64Tsc;
    /** System time in nanoseconds. */
    uint64_t        u64NanoTS;
    /** TSC to system time scale factor. */
    uint32_t        u32TscScale;
    /** TSC frequency shift.  */
    int8_t          i8TscShift;
    /** Clock source (GIM_KVM_SYSTEM_TIME_FLAGS_XXX) flags. */
    uint8_t         fFlags;
    /** Alignment padding. */
    uint8_t         abPadding0[2];
} GIMKVMSYSTEMTIME;
/** Pointer to KVM system-time struct. */
typedef GIMKVMSYSTEMTIME *PGIMKVMSYSTEMTIME;
/** Pointer to a const KVM system-time struct. */
typedef GIMKVMSYSTEMTIME const *PCGIMKVMSYSTEMTIME;
AssertCompileSize(GIMKVMSYSTEMTIME, 32);


/**
 * KVM per-VM wall-clock structure.
 */
typedef struct GIMKVMWALLCLOCK
{
    /** Version (sequence number). */
    uint32_t        u32Version;
    /** Number of seconds since boot. */
    uint32_t        u32Sec;
    /** Number of nanoseconds since boot. */
    uint32_t        u32Nano;
} GIMKVMWALLCLOCK;
/** Pointer to KVM wall-clock struct. */
typedef GIMKVMWALLCLOCK *PGIMKVMWALLCLOCK;
/** Pointer to a const KVM wall-clock struct. */
typedef GIMKVMWALLCLOCK const *PCGIMKVMWALLCLOCK;
AssertCompileSize(GIMKVMWALLCLOCK, 12);


/**
 * GIM KVM VM instance data.
 * Changes to this must checked against the padding of the gim union in VM!
 */
typedef struct GIMKVM
{
    /** Wall-clock MSR. */
    uint64_t                    u64WallClockMsr;
    /**  CPUID features: Basic. */
    uint32_t                    uBaseFeat;
    /** Whether GIM needs to trap \#UD exceptions. */
    bool                        fTrapXcptUD;
    /** Disassembler opcode of hypercall instruction native for this host CPU. */
    uint16_t                    uOpcodeNative;
    /** Native hypercall opcode bytes.  Use for replacing. */
    uint8_t                     abOpcodeNative[3];
    /** Alignment padding. */
    uint8_t                     abPadding[5];
    /** The TSC frequency (in HZ) reported to the guest. */
    uint64_t                    cTscTicksPerSecond;
} GIMKVM;
/** Pointer to per-VM GIM KVM instance data. */
typedef GIMKVM *PGIMKVM;
/** Pointer to const per-VM GIM KVM instance data. */
typedef GIMKVM const *PCGIMKVM;

/**
 * GIM KVMV VCPU instance data.
 * Changes to this must checked against the padding of the gim union in VMCPU!
 */
typedef struct GIMKVMCPU
{
    /** System-time MSR. */
    uint64_t                    u64SystemTimeMsr;
    /** The guest-physical address of the system-time struct. */
    RTGCPHYS                    GCPhysSystemTime;
    /** The version (sequence number) of the system-time struct. */
    uint32_t                    u32SystemTimeVersion;
    /** The guest TSC value while enabling the system-time MSR. */
    uint64_t                    uTsc;
    /** The guest virtual time while enabling the system-time MSR. */
    uint64_t                    uVirtNanoTS;
    /** The flags of the system-time struct. */
    uint8_t                     fSystemTimeFlags;
} GIMKVMCPU;
/** Pointer to per-VCPU GIM KVM instance data. */
typedef GIMKVMCPU *PGIMKVMCPU;
/** Pointer to const per-VCPU GIM KVM instance data. */
typedef GIMKVMCPU const *PCGIMKVMCPU;


RT_C_DECLS_BEGIN

#ifdef IN_RING3
VMMR3_INT_DECL(int)             gimR3KvmInit(PVM pVM);
VMMR3_INT_DECL(int)             gimR3KvmInitCompleted(PVM pVM);
VMMR3_INT_DECL(int)             gimR3KvmTerm(PVM pVM);
VMMR3_INT_DECL(void)            gimR3KvmRelocate(PVM pVM, RTGCINTPTR offDelta);
VMMR3_INT_DECL(void)            gimR3KvmReset(PVM pVM);
VMMR3_INT_DECL(int)             gimR3KvmSave(PVM pVM, PSSMHANDLE pSSM);
VMMR3_INT_DECL(int)             gimR3KvmLoad(PVM pVM, PSSMHANDLE pSSM);

VMMR3_INT_DECL(int)             gimR3KvmDisableSystemTime(PVM pVM);
VMMR3_INT_DECL(int)             gimR3KvmEnableSystemTime(PVM pVM, PVMCPU pVCpu, uint64_t uMsrSystemTime);
VMMR3_INT_DECL(int)             gimR3KvmEnableWallClock(PVM pVM, RTGCPHYS GCPhysSysTime);
#endif /* IN_RING3 */

VMM_INT_DECL(bool)              gimKvmIsParavirtTscEnabled(PVMCC pVM);
VMM_INT_DECL(bool)              gimKvmAreHypercallsEnabled(PVMCPU pVCpu);
VMM_INT_DECL(VBOXSTRICTRC)      gimKvmHypercall(PVMCPUCC pVCpu, PCPUMCTX pCtx);
#if !defined(VBOX_VMM_TARGET_ARMV8)
VMM_INT_DECL(VBOXSTRICTRC)      gimKvmReadMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue);
VMM_INT_DECL(VBOXSTRICTRC)      gimKvmWriteMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uRawValue);
#endif
VMM_INT_DECL(bool)              gimKvmShouldTrapXcptUD(PVM pVM);
VMM_INT_DECL(VBOXSTRICTRC)      gimKvmXcptUD(PVMCC pVM, PVMCPUCC pVCpu, PCPUMCTX pCtx, PDISSTATE pDis, uint8_t *pcbInstr);
VMM_INT_DECL(VBOXSTRICTRC)      gimKvmHypercallEx(PVMCPUCC pVCpu, PCPUMCTX pCtx, unsigned uDisOpcode, uint8_t cbInstr);

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_GIMKvmInternal_h */

