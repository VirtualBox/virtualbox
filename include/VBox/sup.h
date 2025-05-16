/** @file
 * SUP - Support Library. (HDrv)
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_sup_h
#define VBOX_INCLUDED_sup_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <iprt/assert.h>
#include <iprt/stdarg.h>
#include <iprt/cpuset.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#elif defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM64)
# include <iprt/asm-arm.h>
#endif

RT_C_DECLS_BEGIN

struct VTGOBJHDR;
struct VTGPROBELOC;


/** @defgroup   grp_sup     The Support Library API
 * @{
 */

/**
 * Physical page descriptor.
 */
#pragma pack(4) /* space is more important. */
typedef struct SUPPAGE
{
    /** Physical memory address. */
    RTHCPHYS        Phys;
    /** Reserved entry for internal use by the caller. */
    RTHCUINTPTR     uReserved;
} SUPPAGE;
#pragma pack()
/** Pointer to a page descriptor. */
typedef SUPPAGE *PSUPPAGE;
/** Pointer to a const page descriptor. */
typedef const SUPPAGE *PCSUPPAGE;

/**
 * The paging mode.
 *
 * @remarks Users are making assumptions about the order here!
 */
typedef enum SUPPAGINGMODE
{
    /** The usual invalid entry.
     * This is returned by SUPR3GetPagingMode()  */
    SUPPAGINGMODE_INVALID = 0,
    /** Normal 32-bit paging, no global pages */
    SUPPAGINGMODE_32_BIT,
    /** Normal 32-bit paging with global pages. */
    SUPPAGINGMODE_32_BIT_GLOBAL,
    /** PAE mode, no global pages, no NX. */
    SUPPAGINGMODE_PAE,
    /** PAE mode with global pages. */
    SUPPAGINGMODE_PAE_GLOBAL,
    /** PAE mode with NX, no global pages. */
    SUPPAGINGMODE_PAE_NX,
    /** PAE mode with global pages and NX. */
    SUPPAGINGMODE_PAE_GLOBAL_NX,
    /** AMD64 mode, no global pages. */
    SUPPAGINGMODE_AMD64,
    /** AMD64 mode with global pages, no NX. */
    SUPPAGINGMODE_AMD64_GLOBAL,
    /** AMD64 mode with NX, no global pages. */
    SUPPAGINGMODE_AMD64_NX,
    /** AMD64 mode with global pages and NX. */
    SUPPAGINGMODE_AMD64_GLOBAL_NX
} SUPPAGINGMODE;


/** @name Flags returned by SUPR0GetKernelFeatures().
 * @{
 */
/** GDT is read-only. */
#define SUPKERNELFEATURES_GDT_READ_ONLY       RT_BIT(0)
/** SMAP is possibly enabled. */
#define SUPKERNELFEATURES_SMAP                RT_BIT(1)
/** GDT is read-only but the writable GDT can be fetched by SUPR0GetCurrentGdtRw(). */
#define SUPKERNELFEATURES_GDT_NEED_WRITABLE   RT_BIT(2)
/** @} */

/**
 * An VT-x control MSR.
 * @sa  VMXCTLSMSR.
 */
typedef union SUPVMXCTLSMSR
{
    uint64_t            u;
    struct
    {
        /** Bits set here _must_ be set in the corresponding VM-execution controls. */
        uint32_t        allowed0;
        /** Bits cleared here _must_ be cleared in the corresponding VM-execution controls. */
        uint32_t        allowed1;
    } n;
} SUPVMXCTLSMSR;
AssertCompileSize(SUPVMXCTLSMSR, sizeof(uint64_t));

/**
 * Hardware-virtualization MSRs.
 */
typedef struct SUPHWVIRTMSRS
{
    union
    {
        /** @sa VMXMSRS */
        struct
        {
            uint64_t        u64FeatCtrl;
            uint64_t        u64Basic;
            /** Pin-based VM-execution controls. */
            SUPVMXCTLSMSR   PinCtls;
            /** Processor-based VM-execution controls. */
            SUPVMXCTLSMSR   ProcCtls;
            /** Secondary processor-based VM-execution controls. */
            SUPVMXCTLSMSR   ProcCtls2;
            /** VM-exit controls. */
            SUPVMXCTLSMSR   ExitCtls;
            /** VM-entry controls. */
            SUPVMXCTLSMSR   EntryCtls;
            /** True pin-based VM-execution controls. */
            SUPVMXCTLSMSR   TruePinCtls;
            /** True processor-based VM-execution controls. */
            SUPVMXCTLSMSR   TrueProcCtls;
            /** True VM-entry controls. */
            SUPVMXCTLSMSR   TrueEntryCtls;
            /** True VM-exit controls. */
            SUPVMXCTLSMSR   TrueExitCtls;
            uint64_t        u64Misc;
            uint64_t        u64Cr0Fixed0;
            uint64_t        u64Cr0Fixed1;
            uint64_t        u64Cr4Fixed0;
            uint64_t        u64Cr4Fixed1;
            uint64_t        u64VmcsEnum;
            uint64_t        u64VmFunc;
            uint64_t        u64EptVpidCaps;
            uint64_t        u64ProcCtls3;
            uint64_t        u64ExitCtls2;
            uint64_t        au64Reserved[7];
        } vmx;
        struct
        {
            uint64_t        u64MsrHwcr;
            uint64_t        u64MsrSmmAddr;
            uint64_t        u64MsrSmmMask;
            uint64_t        u64Padding[25];
        } svm;
    } u;
} SUPHWVIRTMSRS;
AssertCompileSize(SUPHWVIRTMSRS, 224);
/** Pointer to a hardware-virtualization MSRs struct. */
typedef SUPHWVIRTMSRS *PSUPHWVIRTMSRS;
/** Pointer to a hardware-virtualization MSRs struct. */
typedef const SUPHWVIRTMSRS *PCSUPHWVIRTMSRS;

/**
 * ARM system register value.
 *
 * @note Used by CPUM on non-ARM hosts.
 */
typedef struct SUPARMSYSREGVAL
{
    /** The register value. */
    uint64_t            uValue;
    /** The register number (ARMV8_AARCH64_SYSREG_ID_CREATE). */
    uint32_t            idReg;
    /** SUP_ARM_SYS_REG_VAL_F_XXX. */
    uint32_t            fFlags;
} SUPARMSYSREGVAL;
/** Pointer to an ARM system register value. */
typedef SUPARMSYSREGVAL *PSUPARMSYSREGVAL;
/** Pointer to a const ARM system register value. */
typedef SUPARMSYSREGVAL const *PCSUPARMSYSREGVAL;

/** @name  SUP_ARM_SYS_REG_VAL_F_XXX
 * @note This is mostly for internal CPUM use.
 * @{ */
#define SUP_ARM_SYS_REG_VAL_F_FROM_CPU           UINT32_C(0x00000000) /**< Directly from the CPU. */
#define SUP_ARM_SYS_REG_VAL_F_FROM_DB            UINT32_C(0x00000001) /**< From a CPU database entry. */
#define SUP_ARM_SYS_REG_VAL_F_FROM_REGISTRY      UINT32_C(0x00000004) /**< From windows registry*/
#define SUP_ARM_SYS_REG_VAL_F_FROM_SYSFS         UINT32_C(0x00000005) /**< From linux sysfs. */
#define SUP_ARM_SYS_REG_VAL_F_FROM_USERLAND      UINT32_C(0x00000006) /**< From linux userland MRS emulation, i.e. sanitized. */
#define SUP_ARM_SYS_REG_VAL_F_FROM_MASK          UINT32_C(0x00000007) /**< Register source mask. */
/** @} */

#if defined(RT_ARCH_ARM64) || defined(DOXYGEN_RUNNING)
/** @name SUP_ARM_SYS_REG_F_XXX - Flags for SUPR3ArmQuerySysRegs.
 *  @{ */
/** Include registers with value zero (default is to skip them). */
# define SUP_ARM_SYS_REG_F_INC_ZERO_REG_VAL RT_BIT_32(0)
/** Extended register set (special build).
 * @note currently don't do anything. */
# define SUP_ARM_SYS_REG_F_EXTENDED         RT_BIT_32(1)
/** Valid flags for SUPR3ArmQuerySysRegs. */
# define SUP_ARM_SYS_REG_F_VALID_MASK       UINT32_C(0x00000003)
/** @} */
#endif /* defined(RT_ARCH_ARM64) || defined(DOXYGEN_RUNNING) */

/**
 * Usermode probe context information.
 */
typedef struct SUPDRVTRACERUSRCTX
{
    /** The probe ID from the VTG location record.  */
    uint32_t                idProbe;
    /** 32 if X86, 64 if AMD64. */
    uint8_t                 cBits;
    /** Reserved padding. */
    uint8_t                 abReserved[3];
    /** Data which format is dictated by the cBits member. */
    union
    {
        /** X86 context info. */
        struct
        {
            uint32_t        uVtgProbeLoc;   /**< Location record address. */
            uint32_t        aArgs[20];      /**< Raw arguments. */
            uint32_t        eip;
            uint32_t        eflags;
            uint32_t        eax;
            uint32_t        ecx;
            uint32_t        edx;
            uint32_t        ebx;
            uint32_t        esp;
            uint32_t        ebp;
            uint32_t        esi;
            uint32_t        edi;
            uint16_t        cs;
            uint16_t        ss;
            uint16_t        ds;
            uint16_t        es;
            uint16_t        fs;
            uint16_t        gs;
        } X86;

        /** AMD64 context info. */
        struct
        {
            uint64_t        uVtgProbeLoc;   /**< Location record address. */
            uint64_t        aArgs[10];      /**< Raw arguments. */
            uint64_t        rip;
            uint64_t        rflags;
            uint64_t        rax;
            uint64_t        rcx;
            uint64_t        rdx;
            uint64_t        rbx;
            uint64_t        rsp;
            uint64_t        rbp;
            uint64_t        rsi;
            uint64_t        rdi;
            uint64_t        r8;
            uint64_t        r9;
            uint64_t        r10;
            uint64_t        r11;
            uint64_t        r12;
            uint64_t        r13;
            uint64_t        r14;
            uint64_t        r15;
        } Amd64;
    } u;
} SUPDRVTRACERUSRCTX;
/** Pointer to the usermode probe context information. */
typedef SUPDRVTRACERUSRCTX *PSUPDRVTRACERUSRCTX;
/** Pointer to the const usermode probe context information. */
typedef SUPDRVTRACERUSRCTX const *PCSUPDRVTRACERUSRCTX;

/**
 * The result of a modification operation (SUPMSRPROBEROP_MODIFY or
 * SUPMSRPROBEROP_MODIFY_FASTER).
 */
typedef struct SUPMSRPROBERMODIFYRESULT
{
    /** The MSR value prior to the modifications.  Valid if fBeforeGp is false */
    uint64_t        uBefore;
    /** The value that was written.  Valid if fBeforeGp is false */
    uint64_t        uWritten;
    /** The MSR value after the modifications. Valid if AfterGp is false. */
    uint64_t        uAfter;
    /** Set if we GPed reading the MSR before the modification. */
    bool            fBeforeGp;
    /** Set if we GPed while trying to write the modified value.
     * This is set when fBeforeGp is true. */
    bool            fModifyGp;
    /** Set if we GPed while trying to read the MSR after the modification.
     * This is set when fBeforeGp is true. */
    bool            fAfterGp;
    /** Set if we GPed while trying to restore the MSR after the modification.
     * This is set when fBeforeGp is true. */
    bool            fRestoreGp;
    /** Structure size alignment padding. */
    bool            afReserved[4];
} SUPMSRPROBERMODIFYRESULT, *PSUPMSRPROBERMODIFYRESULT;


/**
 * The CPU state.
 */
typedef enum SUPGIPCPUSTATE
{
    /** Invalid CPU state / unused CPU entry. */
    SUPGIPCPUSTATE_INVALID = 0,
    /** The CPU is not present. */
    SUPGIPCPUSTATE_ABSENT,
    /** The CPU is offline. */
    SUPGIPCPUSTATE_OFFLINE,
    /** The CPU is online. */
    SUPGIPCPUSTATE_ONLINE,
    /** Force 32-bit enum type. */
    SUPGIPCPUSTATE_32_BIT_HACK = 0x7fffffff
} SUPGIPCPUSTATE;

/**
 * Per CPU data.
 */
typedef struct SUPGIPCPU
{
    /** Update transaction number.
     * This number is incremented at the start and end of each update. It follows
     * thusly that odd numbers indicates update in progress, while even numbers
     * indicate stable data. Use this to make sure that the data items you fetch
     * are consistent. */
    volatile uint32_t       u32TransactionId;
    /** The interval in TSC ticks between two NanoTS updates.
     * This is the average interval over the last 2, 4 or 8 updates + a little slack.
     * The slack makes the time go a tiny tiny bit slower and extends the interval enough
     * to avoid ending up with too many 1ns increments. */
    volatile uint32_t       u32UpdateIntervalTSC;
    /** Current nanosecond timestamp. */
    volatile uint64_t       u64NanoTS;
    /** The TSC at the time of u64NanoTS. */
    volatile uint64_t       u64TSC;
    /** Current CPU Frequency. */
    volatile uint64_t       u64CpuHz;
    /** The TSC delta with reference to the master TSC, subtract from RDTSC. */
    volatile int64_t        i64TSCDelta;
    /** Number of errors during updating.
     * Typical errors are under/overflows. */
    volatile uint32_t       cErrors;
    /** Index of the head item in au32TSCHistory. */
    volatile uint32_t       iTSCHistoryHead;
    /** Array of recent TSC interval deltas.
     * The most recent item is at index iTSCHistoryHead.
     * This history is used to calculate u32UpdateIntervalTSC.
     */
    volatile uint32_t       au32TSCHistory[8];
    /** The interval between the last two NanoTS updates. (experiment for now) */
    volatile uint32_t       u32PrevUpdateIntervalNS;

    /** Reserved for future per processor data. */
    volatile uint32_t       u32Reserved;
    /** The TSC value read while doing TSC delta measurements across CPUs. */
    volatile uint64_t       u64TSCSample;
    /** Reserved for future per processor data. */
    volatile uint32_t       au32Reserved1[3];

    /** The CPU state. */
    SUPGIPCPUSTATE volatile enmState;
    /** The host CPU ID of this CPU (the SUPGIPCPU is indexed by APIC ID). */
    RTCPUID                 idCpu;
    /** The CPU set index of this CPU. */
    int16_t                 iCpuSet;
    /** CPU group number (always zero, except on windows). */
    uint16_t                iCpuGroup;
    /** CPU group member number (same as iCpuSet, except on windows). */
    uint16_t                iCpuGroupMember;
    /** The APIC ID of this CPU. */
    uint16_t                idApic;
    /** @todo Add topology/NUMA info. */
    uint32_t                iReservedForNumaNode;
} SUPGIPCPU;
AssertCompileSize(RTCPUID, 4);
AssertCompileSize(SUPGIPCPU, 128);
AssertCompileMemberAlignment(SUPGIPCPU, u64NanoTS, 8);
AssertCompileMemberAlignment(SUPGIPCPU, u64TSC, 8);
AssertCompileMemberAlignment(SUPGIPCPU, u64TSCSample, 8);

/** Pointer to per cpu data.
 * @remark there is no const version of this typedef, see g_pSUPGlobalInfoPage for details. */
typedef SUPGIPCPU *PSUPGIPCPU;

/**
 * CPU group information.
 * @remarks Windows only.
 */
typedef struct SUPGIPCPUGROUP
{
    /** Current number of CPUs in this group. */
    uint16_t volatile       cMembers;
    /** Maximum number of CPUs in the group. */
    uint16_t                cMaxMembers;
    /** The CPU set index of the members. This table has cMaxMembers entries.
     * @note For various reasons, entries from cMembers and up to cMaxMembers are
     *       may change as the host OS does set dynamic assignments during CPU
     *       hotplugging. */
    int16_t                 aiCpuSetIdxs[1];
} SUPGIPCPUGROUP;
/** Pointer to a GIP CPU group structure. */
typedef SUPGIPCPUGROUP *PSUPGIPCPUGROUP;
/** Pointer to a const GIP CPU group structure. */
typedef SUPGIPCPUGROUP const *PCSUPGIPCPUGROUP;

/**
 * The rules concerning the applicability of SUPGIPCPU::i64TscDelta.
 */
typedef enum SUPGIPUSETSCDELTA
{
    /** Value for SUPGIPMODE_ASYNC_TSC. */
    SUPGIPUSETSCDELTA_NOT_APPLICABLE = 0,
    /** The OS specific part of SUPDrv (or the user) claims the TSC is as
     * good as zero. */
    SUPGIPUSETSCDELTA_ZERO_CLAIMED,
    /** The differences in RDTSC output between the CPUs/cores/threads should
     * be considered zero for all practical purposes. */
    SUPGIPUSETSCDELTA_PRACTICALLY_ZERO,
    /** The differences in RDTSC output between the CPUs/cores/threads are a few
     * hundred ticks or less.  (Probably not worth calling ASMGetApicId two times
     * just to apply deltas.) */
    SUPGIPUSETSCDELTA_ROUGHLY_ZERO,
    /** Significant differences in RDTSC output between the CPUs/cores/threads,
     * deltas must be applied. */
    SUPGIPUSETSCDELTA_NOT_ZERO,
    /** End of valid values (exclusive). */
    SUPGIPUSETSCDELTA_END,
    /** Make sure the type is 32-bit sized. */
    SUPGIPUSETSCDELTA_32BIT_HACK = 0x7fffffff
} SUPGIPUSETSCDELTA;


/** @name SUPGIPGETCPU_XXX - methods that aCPUs can be indexed.
 *
 * @note    Linux offers information via selector 0x78, and Windows via selector
 *          0x53.  But since they both support RDTSCP as well, and because most
 *          CPUs now have RDTSCP, we prefer it over LSL.  We can implement more
 *          alternatives if it becomes necessary.
 *
 * @{
 */
/** Use ASMGetApicId (or equivalent) and translate the result via
 *  aiCpuFromApicId. */
#define SUPGIPGETCPU_APIC_ID                        RT_BIT_32(0)
/** Use RDTSCP and translate the first RTCPUSET_MAX_CPUS of ECX via
 * aiCpuFromCpuSetIdx.
 *
 * Linux stores the RTMpCpuId() value in ECX[11:0] and NUMA node number in
 * ECX[12:31].  Solaris only stores RTMpCpuId() in ECX.  On both systems
 * RTMpCpuId() == RTMpCpuIdToSetIndex(RTMpCpuId()).  RTCPUSET_MAX_CPUS is
 * currently 64, 256 or 1024 in size, which lower than
 * 4096, so there shouldn't be any range issues. */
#define SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS       RT_BIT_32(1)
/** Subtract the max IDT size from IDTR.LIMIT, extract the
 * first RTCPUSET_MAX_CPUS and translate it via aiCpuFromCpuSetIdx.
 *
 * Darwin stores the RTMpCpuId() (== RTMpCpuIdToSetIndex(RTMpCpuId()))
 * value in the IDT limit.  The masking is a precaution against what linux
 * does with RDTSCP. */
#define SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS   RT_BIT_32(2)
/** Windows specific RDTSCP variant, where CH gives you the group and CL gives
 * you the CPU number within that group.
 *
 * Use SUPGLOBALINFOPAGE::aidFirstCpuFromCpuGroup to get the group base CPU set
 * index, then translate the sum of thru aiCpuFromCpuSetIdx to find the aCPUs
 * entry.
 *
 * @note The group number is actually 16-bit wide (ECX[23:8]), but we simplify
 *       it since we only support 256 CPUs/groups at the moment.
 */
#define SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL RT_BIT_32(3)
/** Can use CPUID[0xb].EDX and translate the result via aiCpuFromApicId. */
#define SUPGIPGETCPU_APIC_ID_EXT_0B                  RT_BIT_32(4)
/** Can use CPUID[0x8000001e].EAX and translate the result via aiCpuFromApicId. */
#define SUPGIPGETCPU_APIC_ID_EXT_8000001E            RT_BIT_32(5)
/** ARM64: Can use TPIDRRO_EL0 to get the CPU ID */
#define SUPGIPGETCPU_TPIDRRO_EL0                     RT_BIT_32(8)
/** @} */

/** @def SUPGIP_MAX_CPU_GROUPS
 * Maximum number of CPU groups.  */
#if RTCPUSET_MAX_CPUS >= 256
# define SUPGIP_MAX_CPU_GROUPS 256
#else
# define SUPGIP_MAX_CPU_GROUPS RTCPUSET_MAX_CPUS
#endif

/**
 * Global Information Page.
 *
 * This page contains useful information and can be mapped into any
 * process or VM. It can be accessed thru the g_pSUPGlobalInfoPage
 * pointer when a session is open.
 */
typedef struct SUPGLOBALINFOPAGE
{
    /** Magic (SUPGLOBALINFOPAGE_MAGIC). */
    uint32_t            u32Magic;
    /** The GIP version. */
    uint32_t            u32Version;

    /** The GIP update mode, see SUPGIPMODE. */
    uint32_t            u32Mode;
    /** The number of entries in the CPU table.
     * (This can work as RTMpGetArraySize().)  */
    uint16_t            cCpus;
    /** The size of the GIP in pages. */
    uint16_t            cPages;
    /** The update frequency of the of the NanoTS. */
    volatile uint32_t   u32UpdateHz;
    /** The update interval in nanoseconds. (10^9 / u32UpdateHz) */
    volatile uint32_t   u32UpdateIntervalNS;
    /** The timestamp of the last time we update the update frequency. */
    volatile uint64_t   u64NanoTSLastUpdateHz;
    /** The TSC frequency of the system. */
    uint64_t            u64CpuHz;
    /** The number of CPUs that are online. */
    volatile uint16_t   cOnlineCpus;
    /** The number of CPUs present in the system. */
    volatile uint16_t   cPresentCpus;
    /** The highest number of CPUs possible. */
    uint16_t            cPossibleCpus;
    /** The highest number of CPU groups possible. */
    uint16_t            cPossibleCpuGroups;
    /** The max CPU ID (RTMpGetMaxCpuId). */
    RTCPUID             idCpuMax;
    /** The applicability of SUPGIPCPU::i64TscDelta. */
    SUPGIPUSETSCDELTA   enmUseTscDelta;
    /** Mask of SUPGIPGETCPU_XXX values that indicates different ways that aCPU
     * can be accessed from ring-3 and raw-mode context. */
    uint32_t            fGetGipCpu;
    /** GIP flags, see SUPGIP_FLAGS_XXX. */
    volatile uint32_t   fFlags;
    /** The set of online CPUs. */
    RTCPUSET            OnlineCpuSet;
#if RTCPUSET_MAX_CPUS < 1024
    uint64_t            abOnlineCpuSetPadding[(1024 - RTCPUSET_MAX_CPUS) / 64];
#endif
    /** The set of present CPUs. */
    RTCPUSET            PresentCpuSet;
#if RTCPUSET_MAX_CPUS < 1024
    uint64_t            abPresentCpuSetPadding[(1024 - RTCPUSET_MAX_CPUS) / 64];
#endif
    /** The set of possible CPUs. */
    RTCPUSET            PossibleCpuSet;
#if RTCPUSET_MAX_CPUS < 1024
    uint64_t            abPossibleCpuSetPadding[(1024 - RTCPUSET_MAX_CPUS) / 64];
#endif

    /** Padding / reserved space for future data. */
    uint32_t            au32Padding1[48];

    /** Table indexed by the CPU APIC ID to get the CPU table index. */
    uint16_t            aiCpuFromApicId[4096];
    /** CPU set index to CPU table index. */
    uint16_t            aiCpuFromCpuSetIdx[1024];
    /** Table indexed by CPU group to containing offsets to SUPGIPCPUGROUP
     * structures, invalid entries are set to UINT32_MAX.  The offsets are relative
     * to the start of this structure.
     * @note Windows only. The other hosts sets all entries to UINT32_MAX! */
    uint32_t            aoffCpuGroup[SUPGIP_MAX_CPU_GROUPS];

    /** Array of per-cpu data.
     * This is index by ApicId via the aiCpuFromApicId table.
     *
     * The clock and frequency information is updated for all CPUs if @c u32Mode
     * is SUPGIPMODE_ASYNC_TSC. If @c u32Mode is SUPGIPMODE_SYNC_TSC only the first
     * entry is updated. If @c u32Mode is SUPGIPMODE_SYNC_TSC the TSC frequency in
     * @c u64CpuHz is copied to all CPUs. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    SUPGIPCPU           aCPUs[RT_FLEXIBLE_ARRAY];
} SUPGLOBALINFOPAGE;
AssertCompileMemberAlignment(SUPGLOBALINFOPAGE, u64NanoTSLastUpdateHz, 8);
AssertCompileMemberAlignment(SUPGLOBALINFOPAGE, OnlineCpuSet, 64);
AssertCompileMemberAlignment(SUPGLOBALINFOPAGE, PresentCpuSet, 64);
AssertCompileMemberAlignment(SUPGLOBALINFOPAGE, PossibleCpuSet, 64);
#if defined(RT_ARCH_SPARC) || defined(RT_ARCH_SPARC64) /* ?? needed ?? */
AssertCompileMemberAlignment(SUPGLOBALINFOPAGE, aCPUs, 32);
#else
AssertCompileMemberAlignment(SUPGLOBALINFOPAGE, aCPUs, 128);
#endif

/** Pointer to the global info page.
 * @remark there is no const version of this typedef, see g_pSUPGlobalInfoPage for details. */
typedef SUPGLOBALINFOPAGE *PSUPGLOBALINFOPAGE;


/** The value of the SUPGLOBALINFOPAGE::u32Magic field. (Soryo Fuyumi) */
#define SUPGLOBALINFOPAGE_MAGIC     0x19590106
/** The GIP version.
 * Upper 16 bits is the major version. Major version is only changed with
 * incompatible changes in the GIP. */
#define SUPGLOBALINFOPAGE_VERSION   0x000a0000

/**
 * SUPGLOBALINFOPAGE::u32Mode values.
 */
typedef enum SUPGIPMODE
{
    /** The usual invalid null entry. */
    SUPGIPMODE_INVALID = 0,
    /** The TSC of the cores and cpus in the system is in sync. */
    SUPGIPMODE_SYNC_TSC,
    /** Each core has it's own TSC. */
    SUPGIPMODE_ASYNC_TSC,
    /** The TSC of the cores are non-stop and have a constant frequency. */
    SUPGIPMODE_INVARIANT_TSC,
    /** End of valid GIP mode values (exclusive). */
    SUPGIPMODE_END,
    /** The usual 32-bit hack. */
    SUPGIPMODE_32BIT_HACK = 0x7fffffff
} SUPGIPMODE;

/** Pointer to the Global Information Page.
 *
 * This pointer is valid as long as SUPLib has a open session. Anyone using
 * the page must treat this pointer as highly volatile and not trust it beyond
 * one transaction.
 *
 * @remark  The GIP page is read-only to everyone but the support driver and
 *          is actually mapped read only everywhere but in ring-0. However
 *          it is not marked 'const' as this might confuse compilers into
 *          thinking that values doesn't change even if members are marked
 *          as volatile. Thus, there is no PCSUPGLOBALINFOPAGE type.
 */
#if defined(IN_SUP_R3) || defined(IN_SUP_R0)
extern DECLEXPORT(PSUPGLOBALINFOPAGE)   g_pSUPGlobalInfoPage;

#elif !defined(IN_RING0) || defined(RT_OS_WINDOWS) || defined(RT_OS_SOLARIS) || defined(VBOX_WITH_KMOD_WRAPPED_R0_MODS)
extern DECLIMPORT(PSUPGLOBALINFOPAGE)   g_pSUPGlobalInfoPage;

#else /* IN_RING0 && !RT_OS_WINDOWS */
# if !defined(__GNUC__) || defined(RT_OS_DARWIN) || !defined(RT_ARCH_AMD64)
#  define g_pSUPGlobalInfoPage          (&g_SUPGlobalInfoPage)
# else
#  define g_pSUPGlobalInfoPage          (SUPGetGIPHlp())
/** Workaround for ELF+GCC problem on 64-bit hosts.
 * (GCC emits a mov with a R_X86_64_32 reloc, we need R_X86_64_64.) */
DECLINLINE(PSUPGLOBALINFOPAGE) SUPGetGIPHlp(void)
{
    PSUPGLOBALINFOPAGE pGIP;
    __asm__ __volatile__ ("movabs $g_SUPGlobalInfoPage,%0\n\t"
                          : "=a" (pGIP));
    return pGIP;
}
# endif
/** The GIP.
 * We save a level of indirection by exporting the GIP instead of a variable
 * pointing to it. */
extern DECLIMPORT(SUPGLOBALINFOPAGE)    g_SUPGlobalInfoPage;
#endif

/**
 * Gets the GIP pointer.
 *
 * @returns Pointer to the GIP or NULL.
 */
SUPDECL(PSUPGLOBALINFOPAGE)             SUPGetGIP(void);

/** @name SUPGIP_FLAGS_XXX - SUPR3GipSetFlags flags.
 * @{ */
/** Enable GIP test mode. */
#define SUPGIP_FLAGS_TESTING_ENABLE          RT_BIT_32(0)
/** Valid mask of flags that can be set through the ioctl. */
#define SUPGIP_FLAGS_VALID_MASK              RT_BIT_32(0)
/** GIP test mode needs to be checked (e.g. when enabled or being disabled). */
#define SUPGIP_FLAGS_TESTING                 RT_BIT_32(24)
/** Prepare to start GIP test mode. */
#define SUPGIP_FLAGS_TESTING_START           RT_BIT_32(25)
/** Prepare to stop GIP test mode. */
#define SUPGIP_FLAGS_TESTING_STOP            RT_BIT_32(26)
/** @} */

/** @internal  */
SUPDECL(PSUPGIPCPU) SUPGetGipCpuPtrForAsyncMode(PSUPGLOBALINFOPAGE pGip);
SUPDECL(uint64_t) SUPGetCpuHzFromGipForAsyncMode(PSUPGLOBALINFOPAGE pGip);
SUPDECL(bool)     SUPIsTscFreqCompatible(uint64_t uCpuHz, uint64_t *puGipCpuHz, bool fRelax);
SUPDECL(bool)     SUPIsTscFreqCompatibleEx(uint64_t uBaseCpuHz, uint64_t uCpuHz, bool fRelax);


/**
 * Gets CPU entry of the calling CPU.
 *
 * @returns Pointer to the CPU entry on success, NULL on failure.
 * @param   pGip        The GIP pointer.
 */
DECLINLINE(PSUPGIPCPU) SUPGetGipCpuPtr(PSUPGLOBALINFOPAGE pGip)
{
    if (RT_LIKELY(   pGip
                  && pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC))
    {
        switch (pGip->u32Mode)
        {
            case SUPGIPMODE_INVARIANT_TSC:
            case SUPGIPMODE_SYNC_TSC:
                return &pGip->aCPUs[0];
            case SUPGIPMODE_ASYNC_TSC:
                return SUPGetGipCpuPtrForAsyncMode(pGip);
            default: break; /* shut up gcc */
        }
    }
    AssertFailed();
    return NULL;
}

/**
 * Gets the TSC frequency of the calling CPU.
 *
 * @returns TSC frequency, UINT64_MAX on failure (asserted).
 * @param   pGip        The GIP pointer.
 */
DECLINLINE(uint64_t) SUPGetCpuHzFromGip(PSUPGLOBALINFOPAGE pGip)
{
    if (RT_LIKELY(   pGip
                  && pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC))
    {
        switch (pGip->u32Mode)
        {
            case SUPGIPMODE_INVARIANT_TSC:
            case SUPGIPMODE_SYNC_TSC:
                return pGip->aCPUs[0].u64CpuHz;
            case SUPGIPMODE_ASYNC_TSC:
                return SUPGetCpuHzFromGipForAsyncMode(pGip);
            default: break; /* shut up gcc */
        }
    }
    AssertFailed();
    return UINT64_MAX;
}


/**
 * Gets the TSC frequency of the specified CPU.
 *
 * @returns TSC frequency, UINT64_MAX on failure (asserted).
 * @param   pGip        The GIP pointer.
 * @param   iCpuSet     The CPU set index of the CPU in question.
 */
DECLINLINE(uint64_t) SUPGetCpuHzFromGipBySetIndex(PSUPGLOBALINFOPAGE pGip, uint32_t iCpuSet)
{
    if (RT_LIKELY(   pGip
                  && pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC))
    {
        switch (pGip->u32Mode)
        {
            case SUPGIPMODE_INVARIANT_TSC:
            case SUPGIPMODE_SYNC_TSC:
                return pGip->aCPUs[0].u64CpuHz;
            case SUPGIPMODE_ASYNC_TSC:
                if (RT_LIKELY(iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)))
                {
                    uint16_t iCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet];
                    if (RT_LIKELY(iCpu < pGip->cCpus))
                        return pGip->aCPUs[iCpu].u64CpuHz;
                }
                break;
            default: break; /* shut up gcc */
        }
    }
    AssertFailed();
    return UINT64_MAX;
}


/**
 * Gets the pointer to the per CPU data for a CPU given by its set index.
 *
 * @returns Pointer to the corresponding per CPU structure, or NULL if invalid.
 * @param   pGip        The GIP pointer.
 * @param   iCpuSet     The CPU set index of the CPU which we want.
 */
DECLINLINE(PSUPGIPCPU) SUPGetGipCpuBySetIndex(PSUPGLOBALINFOPAGE pGip, uint32_t iCpuSet)
{
    if (RT_LIKELY(   pGip
                  && pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC))
    {
        if (RT_LIKELY(iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)))
        {
            uint16_t iCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet];
            if (RT_LIKELY(iCpu < pGip->cCpus))
                return &pGip->aCPUs[iCpu];
        }
    }
    return NULL;
}


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)

/** @internal */
SUPDECL(uint64_t) SUPReadTscWithDelta(PSUPGLOBALINFOPAGE pGip);

/**
 * Read the host TSC value and applies the TSC delta if appropriate.
 *
 * @returns the TSC value.
 * @remarks Requires GIP to be initialized and valid.
 */
DECLINLINE(uint64_t) SUPReadTsc(void)
{
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (!pGip || pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO)
        return ASMReadTSC();
    return SUPReadTscWithDelta(pGip);
}

#endif /* X86 || AMD64 || ARM64 || ARM32 */

/** @internal */
SUPDECL(int64_t) SUPGetTscDeltaSlow(PSUPGLOBALINFOPAGE pGip);

/**
 * Gets the TSC delta for the current CPU.
 *
 * @returns The TSC delta value (will not return the special INT64_MAX value).
 * @param   pGip    The GIP, NULL is okay in ring-3.
 * @remarks Requires GIP to be initialized and valid if pGip isn't NULL.
 */
DECLINLINE(int64_t) SUPGetTscDelta(PSUPGLOBALINFOPAGE pGip)
{
#ifdef IN_RING3
    if (!pGip || pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO)
#else
    if (pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO)
#endif
        return 0;
    return SUPGetTscDeltaSlow(pGip);
}


/**
 * Gets the TSC delta for a given CPU.
 *
 * @returns The TSC delta value (will not return the special INT64_MAX value).
 * @param   iCpuSet         The CPU set index of the CPU which TSC delta we want.
 * @remarks Requires GIP to be initialized and valid.
 */
DECLINLINE(int64_t) SUPGetTscDeltaByCpuSetIndex(uint32_t iCpuSet)
{
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO)
        return 0;
    if (RT_LIKELY(iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)))
    {
        uint16_t iCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet];
        if (RT_LIKELY(iCpu < pGip->cCpus))
        {
            int64_t iTscDelta = pGip->aCPUs[iCpu].i64TSCDelta;
            if (iTscDelta != INT64_MAX)
                return iTscDelta;
        }
    }
    AssertFailed();
    return 0;
}


/**
 * Checks if the TSC delta is available for a given CPU (if TSC-deltas are
 * relevant).
 *
 * @returns true if it's okay to read the TSC, false otherwise.
 *
 * @param   iCpuSet   The CPU set index of the CPU which TSC delta we check.
 * @remarks Requires GIP to be initialized and valid.
 */
DECLINLINE(bool) SUPIsTscDeltaAvailableForCpuSetIndex(uint32_t iCpuSet)
{
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO)
        return true;
    if (RT_LIKELY(iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)))
    {
        uint16_t iCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet];
        if (RT_LIKELY(iCpu < pGip->cCpus))
        {
            int64_t iTscDelta = pGip->aCPUs[iCpu].i64TSCDelta;
            if (iTscDelta != INT64_MAX)
                return true;
        }
    }
    return false;
}


/**
 * Gets the descriptive GIP mode name.
 *
 * @returns The name.
 * @param   pGip      Pointer to the GIP.
 */
DECLINLINE(const char *) SUPGetGIPModeName(PSUPGLOBALINFOPAGE pGip)
{
    AssertReturn(pGip, NULL);
    switch (pGip->u32Mode)
    {
        case SUPGIPMODE_INVARIANT_TSC:  return "Invariant";
        case SUPGIPMODE_SYNC_TSC:       return "Synchronous";
        case SUPGIPMODE_ASYNC_TSC:      return "Asynchronous";
        case SUPGIPMODE_INVALID:        return "Invalid";
        default:                        return "???";
    }
}


/**
 * Gets the descriptive TSC-delta enum name.
 *
 * @returns The name.
 * @param   pGip      Pointer to the GIP.
 */
DECLINLINE(const char *) SUPGetGIPTscDeltaModeName(PSUPGLOBALINFOPAGE pGip)
{
    AssertReturn(pGip, NULL);
    switch (pGip->enmUseTscDelta)
    {
        case SUPGIPUSETSCDELTA_NOT_APPLICABLE:   return "Not Applicable";
        case SUPGIPUSETSCDELTA_ZERO_CLAIMED:     return "Zero Claimed";
        case SUPGIPUSETSCDELTA_PRACTICALLY_ZERO: return "Practically Zero";
        case SUPGIPUSETSCDELTA_ROUGHLY_ZERO:     return "Roughly Zero";
        case SUPGIPUSETSCDELTA_NOT_ZERO:         return "Not Zero";
        default:                                 return "???";
    }
}


/**
 * Request for generic VMMR0Entry calls.
 */
typedef struct SUPVMMR0REQHDR
{
    /** The magic. (SUPVMMR0REQHDR_MAGIC) */
    uint32_t    u32Magic;
    /** The size of the request. */
    uint32_t    cbReq;
} SUPVMMR0REQHDR;
/** Pointer to a ring-0 request header. */
typedef SUPVMMR0REQHDR *PSUPVMMR0REQHDR;
/** the SUPVMMR0REQHDR::u32Magic value (Ethan Iverson - The Bad Plus). */
#define SUPVMMR0REQHDR_MAGIC        UINT32_C(0x19730211)


/** For the fast ioctl path.
 * @{
 */
/** @see VMMR0_DO_HM_RUN. */
#define SUP_VMMR0_DO_HM_RUN     0
/** @see VMMR0_DO_NEM_RUN */
#define SUP_VMMR0_DO_NEM_RUN    1
/** @see VMMR0_DO_NOP */
#define SUP_VMMR0_DO_NOP        2
/** @} */

/** SUPR3QueryVTCaps capability flags.
 * @{
 */
/** AMD-V support. */
#define SUPVTCAPS_AMD_V                     RT_BIT(0)
/** VT-x support. */
#define SUPVTCAPS_VT_X                      RT_BIT(1)
/** Nested paging is supported. */
#define SUPVTCAPS_NESTED_PAGING             RT_BIT(2)
/** VT-x: Unrestricted guest execution is supported. */
#define SUPVTCAPS_VTX_UNRESTRICTED_GUEST    RT_BIT(3)
/** VT-x: VMCS shadowing is supported. */
#define SUPVTCAPS_VTX_VMCS_SHADOWING        RT_BIT(4)
/** AMD-V: Virtualized VMSAVE/VMLOAD is supported. */
#define SUPVTCAPS_AMDV_VIRT_VMSAVE_VMLOAD   RT_BIT(5)
/** @} */

/**
 * Request for generic FNSUPR0SERVICEREQHANDLER calls.
 */
typedef struct SUPR0SERVICEREQHDR
{
    /** The magic. (SUPR0SERVICEREQHDR_MAGIC) */
    uint32_t    u32Magic;
    /** The size of the request. */
    uint32_t    cbReq;
} SUPR0SERVICEREQHDR;
/** Pointer to a ring-0 service request header. */
typedef SUPR0SERVICEREQHDR *PSUPR0SERVICEREQHDR;
/** the SUPVMMR0REQHDR::u32Magic value (Esbjoern Svensson - E.S.P.).  */
#define SUPR0SERVICEREQHDR_MAGIC    UINT32_C(0x19640416)


/**
 * Creates a single release event semaphore.
 *
 * @returns VBox status code.
 * @param   pSession        The session handle of the caller.
 * @param   phEvent         Where to return the handle to the event semaphore.
 */
SUPDECL(int) SUPSemEventCreate(PSUPDRVSESSION pSession, PSUPSEMEVENT phEvent);

/**
 * Closes a single release event semaphore handle.
 *
 * @returns VBox status code.
 * @retval  VINF_OBJECT_DESTROYED if the semaphore was destroyed.
 * @retval  VINF_SUCCESS if the handle was successfully closed but the semaphore
 *          object remained alive because of other references.
 *
 * @param   pSession            The session handle of the caller.
 * @param   hEvent              The handle. Nil is quietly ignored.
 */
SUPDECL(int) SUPSemEventClose(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent);

/**
 * Signals a single release event semaphore.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEvent              The semaphore handle.
 */
SUPDECL(int) SUPSemEventSignal(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent);

#ifdef IN_RING0
/**
 * Waits on a single release event semaphore, not interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEvent              The semaphore handle.
 * @param   cMillies            The number of milliseconds to wait.
 * @remarks Not available in ring-3.
 */
SUPDECL(int) SUPSemEventWait(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint32_t cMillies);
#endif

/**
 * Waits on a single release event semaphore, interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEvent              The semaphore handle.
 * @param   cMillies            The number of milliseconds to wait.
 */
SUPDECL(int) SUPSemEventWaitNoResume(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint32_t cMillies);

/**
 * Waits on a single release event semaphore, interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEvent              The semaphore handle.
 * @param   uNsTimeout          The deadline given on the RTTimeNanoTS() clock.
 */
SUPDECL(int) SUPSemEventWaitNsAbsIntr(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint64_t uNsTimeout);

/**
 * Waits on a single release event semaphore, interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEvent              The semaphore handle.
 * @param   cNsTimeout          The number of nanoseconds to wait.
 */
SUPDECL(int) SUPSemEventWaitNsRelIntr(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint64_t cNsTimeout);

/**
 * Gets the best timeout resolution that SUPSemEventWaitNsAbsIntr and
 * SUPSemEventWaitNsAbsIntr can do.
 *
 * @returns The resolution in nanoseconds.
 * @param   pSession            The session handle of the caller.
 */
SUPDECL(uint32_t) SUPSemEventGetResolution(PSUPDRVSESSION pSession);


/**
 * Creates a multiple release event semaphore.
 *
 * @returns VBox status code.
 * @param   pSession        The session handle of the caller.
 * @param   phEventMulti    Where to return the handle to the event semaphore.
 */
SUPDECL(int) SUPSemEventMultiCreate(PSUPDRVSESSION pSession, PSUPSEMEVENTMULTI phEventMulti);

/**
 * Closes a multiple release event semaphore handle.
 *
 * @returns VBox status code.
 * @retval  VINF_OBJECT_DESTROYED if the semaphore was destroyed.
 * @retval  VINF_SUCCESS if the handle was successfully closed but the semaphore
 *          object remained alive because of other references.
 *
 * @param   pSession            The session handle of the caller.
 * @param   hEventMulti         The handle. Nil is quietly ignored.
 */
SUPDECL(int) SUPSemEventMultiClose(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti);

/**
 * Signals a multiple release event semaphore.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEventMulti         The semaphore handle.
 */
SUPDECL(int) SUPSemEventMultiSignal(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti);

/**
 * Resets a multiple release event semaphore.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEventMulti         The semaphore handle.
 */
SUPDECL(int) SUPSemEventMultiReset(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti);

#ifdef IN_RING0
/**
 * Waits on a multiple release event semaphore, not interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEventMulti         The semaphore handle.
 * @param   cMillies            The number of milliseconds to wait.
 * @remarks Not available in ring-3.
 */
SUPDECL(int) SUPSemEventMultiWait(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies);
#endif

/**
 * Waits on a multiple release event semaphore, interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEventMulti         The semaphore handle.
 * @param   cMillies            The number of milliseconds to wait.
 */
SUPDECL(int) SUPSemEventMultiWaitNoResume(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies);

/**
 * Waits on a multiple release event semaphore, interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEventMulti         The semaphore handle.
 * @param   uNsTimeout          The deadline given on the RTTimeNanoTS() clock.
 */
SUPDECL(int) SUPSemEventMultiWaitNsAbsIntr(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint64_t uNsTimeout);

/**
 * Waits on a multiple release event semaphore, interruptible.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle of the caller.
 * @param   hEventMulti         The semaphore handle.
 * @param   cNsTimeout          The number of nanoseconds to wait.
 */
SUPDECL(int) SUPSemEventMultiWaitNsRelIntr(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint64_t cNsTimeout);

/**
 * Gets the best timeout resolution that SUPSemEventMultiWaitNsAbsIntr and
 * SUPSemEventMultiWaitNsRelIntr can do.
 *
 * @returns The resolution in nanoseconds.
 * @param   pSession            The session handle of the caller.
 */
SUPDECL(uint32_t) SUPSemEventMultiGetResolution(PSUPDRVSESSION pSession);


#ifdef IN_RING3

/** @defgroup   grp_sup_r3     SUP Host Context Ring-3 API
 * @{
 */

/**
 * Installs the support library.
 *
 * @returns VBox status code.
 */
SUPR3DECL(int) SUPR3Install(void);

/**
 * Uninstalls the support library.
 *
 * @returns VBox status code.
 */
SUPR3DECL(int) SUPR3Uninstall(void);

/**
 * Trusted main entry point.
 *
 * This is exported as "TrustedMain" by the dynamic libraries which contains the
 * "real" application binary for which the hardened stub is built.  The entry
 * point is invoked upon successful initialization of the support library and
 * runtime.
 *
 * @returns main kind of exit code.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   envp            The environment vector.
 */
typedef DECLCALLBACKTYPE(int, FNSUPTRUSTEDMAIN,(int argc, char **argv, char **envp));
/** Pointer to FNSUPTRUSTEDMAIN(). */
typedef FNSUPTRUSTEDMAIN *PFNSUPTRUSTEDMAIN;

/** Which operation failed. */
typedef enum SUPINITOP
{
    /** Invalid. */
    kSupInitOp_Invalid = 0,
    /** Installation integrity error. */
    kSupInitOp_Integrity,
    /** Setuid related. */
    kSupInitOp_RootCheck,
    /** Driver related. */
    kSupInitOp_Driver,
    /** IPRT init related. */
    kSupInitOp_IPRT,
    /** Miscellaneous. */
    kSupInitOp_Misc,
    /** Place holder. */
    kSupInitOp_End
} SUPINITOP;

/**
 * Trusted error entry point, optional.
 *
 * This is exported as "TrustedError" by the dynamic libraries which contains
 * the "real" application binary for which the hardened stub is built. The
 * hardened main() must specify SUPSECMAIN_FLAGS_TRUSTED_ERROR when calling
 * SUPR3HardenedMain.
 *
 * @param   pszWhere        Where the error occurred (function name).
 * @param   enmWhat         Which operation went wrong.
 * @param   rc              The status code.
 * @param   pszMsgFmt       Error message format string.
 * @param   va              The message format arguments.
 */
typedef DECLCALLBACKTYPE(void, FNSUPTRUSTEDERROR,(const char *pszWhere, SUPINITOP enmWhat, int rc,
                                                  const char *pszMsgFmt, va_list va)) RT_IPRT_FORMAT_ATTR(4, 0);
/** Pointer to FNSUPTRUSTEDERROR. */
typedef FNSUPTRUSTEDERROR *PFNSUPTRUSTEDERROR;

/**
 * Secure main.
 *
 * This is used for the set-user-ID-on-execute binaries on unixy systems
 * and when using the open-vboxdrv-via-root-service setup on Windows.
 *
 * This function will perform the integrity checks of the VirtualBox
 * installation, open the support driver, open the root service (later),
 * and load the DLL corresponding to \a pszProgName and execute its main
 * function.
 *
 * @returns Return code appropriate for main().
 *
 * @param   pszProgName     The program name. This will be used to figure out which
 *                          DLL/SO/DYLIB to load and execute.
 * @param   fFlags          SUPSECMAIN_FLAGS_XXX.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   envp            The environment vector.
 */
DECLHIDDEN(int) SUPR3HardenedMain(const char *pszProgName, uint32_t fFlags, int argc, char **argv, char **envp);

/** @name SUPSECMAIN_FLAGS_XXX - SUPR3HardenedMain flags.
 * @{ */
/** Don't open the device. (Intended for VirtualBox without -startvm.) */
#define SUPSECMAIN_FLAGS_DONT_OPEN_DEV              RT_BIT_32(0)
/** The hardened DLL has a "TrustedError" function (see FNSUPTRUSTEDERROR). */
#define SUPSECMAIN_FLAGS_TRUSTED_ERROR              RT_BIT_32(1)
/** Hack for making VirtualBoxVM use VirtualBox.dylib on Mac OS X.
 * @note Not used since 6.0  */
#define SUPSECMAIN_FLAGS_OSX_VM_APP                 RT_BIT_32(2)
/** The first process.
 * @internal  */
#define SUPSECMAIN_FLAGS_FIRST_PROCESS              RT_BIT_32(3)
/** Program binary location mask. */
#define SUPSECMAIN_FLAGS_LOC_MASK                   UINT32_C(0x00000030)
/** Default binary location is the application binary directory.  Does
 * not need to be given explicitly (it's 0).  */
#define SUPSECMAIN_FLAGS_LOC_APP_BIN                UINT32_C(0x00000000)
/** The binary is located in the testcase directory instead of the
 * default application binary directory. */
#define SUPSECMAIN_FLAGS_LOC_TESTCASE               UINT32_C(0x00000010)
/** The binary is located in a nested application bundle under Resources/ in the
 * main Mac OS X application (think Resources/VirtualBoxVM.app).  */
#define SUPSECMAIN_FLAGS_LOC_OSX_HLP_APP            UINT32_C(0x00000020)
/** Force driverless mode. */
#define SUPSECMAIN_FLAGS_DRIVERLESS                 RT_BIT_32(8)
/** Driverless IEM-only mode is allowed, so don't fail fatally just because
 * the VBox support driver is unavailable. */
#define SUPSECMAIN_FLAGS_DRIVERLESS_IEM_ALLOWED     RT_BIT_32(9)
#ifdef VBOX_WITH_DRIVERLESS_NEM_FALLBACK
/** Driverless NEM is a fallback posibility, so don't fail fatally just
 * because the VBox support driver is unavailable.
 * This may imply checking NEM requirements, depending on the host.
 * @note Not supported on Windows. */
# define SUPSECMAIN_FLAGS_DRIVERLESS_NEM_FALLBACK   RT_BIT_32(10)
#endif

/** @} */

/**
 * Initializes the support library.
 *
 * Each successful call to SUPR3Init() or SUPR3InitEx must be countered by a
 * call to SUPR3Term(false).
 *
 * @returns VBox status code.
 * @param   ppSession       Where to store the session handle. Defaults to NULL.
 */
SUPR3DECL(int) SUPR3Init(PSUPDRVSESSION *ppSession);

/**
 * Initializes the support library, extended version.
 *
 * Each successful call to SUPR3Init() or SUPR3InitEx must be countered by a
 * call to SUPR3Term(false).
 *
 * @returns VBox status code.
 * @param   fFlags          SUPR3INIT_F_XXX
 * @param   ppSession       Where to store the session handle. Defaults to NULL.
 */
SUPR3DECL(int) SUPR3InitEx(uint32_t fFlags, PSUPDRVSESSION *ppSession);
/** @name SUPR3INIT_F_XXX - Flags for SUPR3InitEx
 * @{ */
/** Unrestricted access. */
#define SUPR3INIT_F_UNRESTRICTED                RT_BIT_32(0)
/** Limited access (for Main). */
#define SUPR3INIT_F_LIMITED                     RT_BIT_32(1)
/** Force driverless mode. */
#define SUPR3INIT_F_DRIVERLESS                  RT_BIT_32(2)
/** Allow driverless IEM mode if the VBox support driver is unavailable.
 * @see SUPSECMAIN_FLAGS_DRIVERLESS_IEM_ALLOWED */
#define SUPR3INIT_F_DRIVERLESS_IEM_ALLOWED      RT_BIT_32(3)
#ifdef VBOX_WITH_DRIVERLESS_NEM_FALLBACK
/** Allow driverless NEM mode as fallback if the VBox support driver is unavailable.
 * @see SUPSECMAIN_FLAGS_DRIVERLESS_NEM_FALLBACK */
# define SUPR3INIT_F_DRIVERLESS_NEM_FALLBACK    RT_BIT_32(4)
#endif
/** Mask with all the flags that may trigger driverless mode. */
#ifdef VBOX_WITH_DRIVERLESS_NEM_FALLBACK
# define SUPR3INIT_F_DRIVERLESS_MASK            UINT32_C(0x0000001c)
#else
# define SUPR3INIT_F_DRIVERLESS_MASK            UINT32_C(0x0000000c)
#endif
/** @} */

/**
 * Terminates the support library.
 *
 * @returns VBox status code.
 * @param   fForced     Forced termination. This means to ignore the
 *                      init call count and just terminated.
 */
#ifdef __cplusplus
SUPR3DECL(int) SUPR3Term(bool fForced = false);
#else
SUPR3DECL(int) SUPR3Term(int fForced);
#endif

/**
 * Check if the support library is operating in driverless mode.
 *
 * @returns true/false accordingly.
 * @see     SUPR3INIT_F_DRIVERLESS_IEM_ALLOWED,
 *          SUPR3INIT_F_DRIVERLESS_NEM_FALLBACK
 */
SUPR3DECL(bool) SUPR3IsDriverless(void);

/**
 * Sets the ring-0 VM handle for use with fast IOCtls.
 *
 * @returns VBox status code.
 * @param   pVMR0       The ring-0 VM handle.
 *                      NIL_RTR0PTR can be used to unset the handle when the
 *                      VM is about to be destroyed.
 */
SUPR3DECL(int) SUPR3SetVMForFastIOCtl(PVMR0 pVMR0);

/**
 * Calls the HC R0 VMM entry point.
 * See VMMR0Entry() for more details.
 *
 * @returns error code specific to uFunction.
 * @param   pVMR0       Pointer to the Ring-0 (Host Context) mapping of the VM structure.
 * @param   idCpu       The virtual CPU ID.
 * @param   uOperation  Operation to execute.
 * @param   pvArg       Argument.
 */
SUPR3DECL(int) SUPR3CallVMMR0(PVMR0 pVMR0, VMCPUID idCpu, unsigned uOperation, void *pvArg);

/**
 * Variant of SUPR3CallVMMR0, except that this takes the fast ioclt path
 * regardsless of compile-time defaults.
 *
 * @returns VBox status code.
 * @param   pVMR0       The ring-0 VM handle.
 * @param   uOperation  The operation; only the SUP_VMMR0_DO_* ones are valid.
 * @param   idCpu       The virtual CPU ID.
 */
SUPR3DECL(int) SUPR3CallVMMR0Fast(PVMR0 pVMR0, unsigned uOperation, VMCPUID idCpu);

/**
 * Calls the HC R0 VMM entry point, in a safer but slower manner than
 * SUPR3CallVMMR0. When entering using this call the R0 components can call
 * into the host kernel (i.e. use the SUPR0 and RT APIs).
 *
 * See VMMR0Entry() for more details.
 *
 * @returns error code specific to uFunction.
 * @param   pVMR0       Pointer to the Ring-0 (Host Context) mapping of the VM structure.
 * @param   idCpu       The virtual CPU ID.
 * @param   uOperation  Operation to execute.
 * @param   u64Arg      Constant argument.
 * @param   pReqHdr     Pointer to a request header. Optional.
 *                      This will be copied in and out of kernel space. There currently is a size
 *                      limit on this, just below 4KB.
 */
SUPR3DECL(int) SUPR3CallVMMR0Ex(PVMR0 pVMR0, VMCPUID idCpu, unsigned uOperation, uint64_t u64Arg, PSUPVMMR0REQHDR pReqHdr);

/**
 * Calls a ring-0 service.
 *
 * The operation and the request packet is specific to the service.
 *
 * @returns error code specific to uFunction.
 * @param   pszService  The service name.
 * @param   cchService  The length of the service name.
 * @param   uOperation  The request number.
 * @param   u64Arg      Constant argument.
 * @param   pReqHdr     Pointer to a request header. Optional.
 *                      This will be copied in and out of kernel space. There currently is a size
 *                      limit on this, just below 4KB.
 */
SUPR3DECL(int) SUPR3CallR0Service(const char *pszService, size_t cchService, uint32_t uOperation, uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr);

/** Which logger. */
typedef enum SUPLOGGER
{
    SUPLOGGER_DEBUG = 1,
    SUPLOGGER_RELEASE
} SUPLOGGER;

/**
 * Changes the settings of the specified ring-0 logger.
 *
 * @returns VBox status code.
 * @param   enmWhich    Which logger.
 * @param   pszFlags    The flags settings.
 * @param   pszGroups   The groups settings.
 * @param   pszDest     The destination specificier.
 */
SUPR3DECL(int) SUPR3LoggerSettings(SUPLOGGER enmWhich, const char *pszFlags, const char *pszGroups, const char *pszDest);

/**
 * Creates a ring-0 logger instance.
 *
 * @returns VBox status code.
 * @param   enmWhich    Which logger to create.
 * @param   pszFlags    The flags settings.
 * @param   pszGroups   The groups settings.
 * @param   pszDest     The destination specificier.
 */
SUPR3DECL(int) SUPR3LoggerCreate(SUPLOGGER enmWhich, const char *pszFlags, const char *pszGroups, const char *pszDest);

/**
 * Destroys a ring-0 logger instance.
 *
 * @returns VBox status code.
 * @param   enmWhich    Which logger.
 */
SUPR3DECL(int) SUPR3LoggerDestroy(SUPLOGGER enmWhich);

/**
 * Queries the paging mode of the host OS.
 *
 * @returns The paging mode.
 */
SUPR3DECL(SUPPAGINGMODE) SUPR3GetPagingMode(void);

/**
 * Allocate zero-filled pages.
 *
 * Use this to allocate a number of pages suitable for seeding / locking.
 * Call SUPR3PageFree() to free the pages once done with them.
 *
 * @returns VBox status.
 * @param   cPages          Number of pages to allocate.
 * @param   fFlags          SUP_PAGE_ALLOC_F_XXX
 * @param   ppvPages        Where to store the base pointer to the allocated pages.
 */
SUPR3DECL(int) SUPR3PageAlloc(size_t cPages, uint32_t fFlags, void **ppvPages);

/** @name SUP_PAGE_ALLOC_F_XXX - SUPR3PageAlloc flags.
 * @{ */
/** Use large pages if available. */
#define SUP_PAGE_ALLOC_F_LARGE_PAGES    RT_BIT_32(0)
/** Advice that the allocated pages will probably be locked by
 * RTR0MemObjLockUser later, so play nice if needed. */
#define SUP_PAGE_ALLOC_F_FOR_LOCKING    RT_BIT_32(1)
/** Mask of valid flags. */
#define SUP_PAGE_ALLOC_F_VALID_MASK     UINT32_C(0x00000003)
/** @} */

/**
 * Frees pages allocated with SUPR3PageAlloc().
 *
 * @returns VBox status.
 * @param   pvPages         Pointer returned by SUPR3PageAlloc().
 * @param   cPages          Number of pages that was allocated.
 */
SUPR3DECL(int) SUPR3PageFree(void *pvPages, size_t cPages);

/**
 * Allocate non-zeroed, locked, pages with user and, optionally, kernel
 * mappings.
 *
 * Use SUPR3PageFreeEx() to free memory allocated with this function.
 *
 * @returns VBox status code.
 * @param   cPages          The number of pages to allocate.
 * @param   fFlags          Flags, reserved. Must be zero.
 * @param   ppvPages        Where to store the address of the user mapping.
 * @param   pR0Ptr          Where to store the address of the kernel mapping.
 *                          NULL if no kernel mapping is desired.
 * @param   paPages         Where to store the physical addresses of each page.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3PageAllocEx(size_t cPages, uint32_t fFlags, void **ppvPages, PRTR0PTR pR0Ptr, PSUPPAGE paPages);

/**
 * Maps a portion of a ring-3 only allocation into kernel space.
 *
 * @returns VBox status code.
 *
 * @param   pvR3            The address SUPR3PageAllocEx return.
 * @param   off             Offset to start mapping at. Must be page aligned.
 * @param   cb              Number of bytes to map. Must be page aligned.
 * @param   fFlags          Flags, must be zero.
 * @param   pR0Ptr          Where to store the address on success.
 *
 */
SUPR3DECL(int) SUPR3PageMapKernel(void *pvR3, uint32_t off, uint32_t cb, uint32_t fFlags, PRTR0PTR pR0Ptr);

/**
 * Changes the protection of
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if the OS doesn't allow us to change page level
 *          protection. See also RTR0MemObjProtect.
 *
 * @param   pvR3            The ring-3 address SUPR3PageAllocEx returned.
 * @param   R0Ptr           The ring-0 address SUPR3PageAllocEx returned if it
 *                          is desired that the corresponding ring-0 page
 *                          mappings should change protection as well. Pass
 *                          NIL_RTR0PTR if the ring-0 pages should remain
 *                          unaffected.
 * @param   off             Offset to start at which to start chagning the page
 *                          level protection. Must be page aligned.
 * @param   cb              Number of bytes to change. Must be page aligned.
 * @param   fProt           The new page level protection, either a combination
 *                          of RTMEM_PROT_READ, RTMEM_PROT_WRITE and
 *                          RTMEM_PROT_EXEC, or just RTMEM_PROT_NONE.
 */
SUPR3DECL(int) SUPR3PageProtect(void *pvR3, RTR0PTR R0Ptr, uint32_t off, uint32_t cb, uint32_t fProt);

/**
 * Free pages allocated by SUPR3PageAllocEx.
 *
 * @returns VBox status code.
 * @param   pvPages         The address of the user mapping.
 * @param   cPages          The number of pages.
 */
SUPR3DECL(int) SUPR3PageFreeEx(void *pvPages, size_t cPages);

/**
 * Allocated memory with page aligned memory with a contiguous and locked physical
 * memory backing below 4GB.
 *
 * @returns Pointer to the allocated memory (virtual address).
 *          *pHCPhys is set to the physical address of the memory.
 *          If ppvR0 isn't NULL, *ppvR0 is set to the ring-0 mapping.
 *          The returned memory must be freed using SUPR3ContFree().
 * @returns NULL on failure.
 * @param   cPages      Number of pages to allocate.
 * @param   pR0Ptr      Where to store the ring-0 mapping of the allocation. (optional)
 * @param   pHCPhys     Where to store the physical address of the memory block.
 *
 * @remark  This 2nd version of this API exists because we're not able to map the
 *          ring-3 mapping executable on WIN64. This is a serious problem in regard to
 *          the world switchers.
 */
SUPR3DECL(void *) SUPR3ContAlloc(size_t cPages, PRTR0PTR pR0Ptr, PRTHCPHYS pHCPhys);

/**
 * Frees memory allocated with SUPR3ContAlloc().
 *
 * @returns VBox status code.
 * @param   pv          Pointer to the memory block which should be freed.
 * @param   cPages      Number of pages to be freed.
 */
SUPR3DECL(int) SUPR3ContFree(void *pv, size_t cPages);

/**
 * Allocated non contiguous physical memory below 4GB.
 *
 * The memory isn't zeroed.
 *
 * @returns VBox status code.
 * @returns NULL on failure.
 * @param   cPages      Number of pages to allocate.
 * @param   ppvPages    Where to store the pointer to the allocated memory.
 *                      The pointer stored here on success must be passed to
 *                      SUPR3LowFree when the memory should be released.
 * @param   ppvPagesR0  Where to store the ring-0 pointer to the allocated memory. optional.
 * @param   paPages     Where to store the physical addresses of the individual pages.
 */
SUPR3DECL(int) SUPR3LowAlloc(size_t cPages, void **ppvPages, PRTR0PTR ppvPagesR0, PSUPPAGE paPages);

/**
 * Frees memory allocated with SUPR3LowAlloc().
 *
 * @returns VBox status code.
 * @param   pv          Pointer to the memory block which should be freed.
 * @param   cPages      Number of pages that was allocated.
 */
SUPR3DECL(int) SUPR3LowFree(void *pv, size_t cPages);

/**
 * Load a module into R0 HC.
 *
 * This will verify the file integrity in a similar manner as
 * SUPR3HardenedVerifyFile before loading it.
 *
 * @returns VBox status code.
 * @param   pszFilename     The path to the image file.
 * @param   pszModule       The module name. Max 32 bytes.
 * @param   ppvImageBase    Where to store the image address.
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3LoadModule(const char *pszFilename, const char *pszModule, void **ppvImageBase, PRTERRINFO pErrInfo);

/**
 * Load a module into R0 HC.
 *
 * This will verify the file integrity in a similar manner as
 * SUPR3HardenedVerifyFile before loading it.
 *
 * @returns VBox status code.
 * @param   pszFilename         The path to the image file.
 * @param   pszModule           The module name. Max 32 bytes.
 * @param   pszSrvReqHandler    The name of the service request handler entry
 *                              point. See FNSUPR0SERVICEREQHANDLER.
 * @param   ppvImageBase        Where to store the image address.
 */
SUPR3DECL(int) SUPR3LoadServiceModule(const char *pszFilename, const char *pszModule,
                                      const char *pszSrvReqHandler, void **ppvImageBase);

/**
 * Frees a R0 HC module.
 *
 * @returns VBox status code.
 * @param   pvImageBase     The base address of the image to free.
 * @remark  This will not actually 'free' the module, there are of course usage counting.
 */
SUPR3DECL(int) SUPR3FreeModule(void *pvImageBase);

/**
 * Lock down the module loader interface.
 *
 * This will lock down the module loader interface. No new modules can be
 * loaded and all loaded modules can no longer be freed.
 *
 * @returns VBox status code.
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3LockDownLoader(PRTERRINFO pErrInfo);

/**
 * Get the address of a symbol in a ring-0 module.
 *
 * @returns VBox status code.
 * @param   pvImageBase     The base address of the image to search.
 * @param   pszSymbol       Symbol name. If it's value is less than 64k it's treated like a
 *                          ordinal value rather than a string pointer.
 * @param   ppvValue        Where to store the symbol value.
 */
SUPR3DECL(int) SUPR3GetSymbolR0(void *pvImageBase, const char *pszSymbol, void **ppvValue);

/**
 * Load R0 HC VMM code.
 *
 * @returns VBox status code.
 * @deprecated  Use SUPR3LoadModule(pszFilename, "VMMR0.r0", &pvImageBase)
 * @param   pszFilename     Full path to the VMMR0.r0 file (silly).
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3LoadVMM(const char *pszFilename, PRTERRINFO pErrInfo);

/**
 * Unloads R0 HC VMM code.
 *
 * @returns VBox status code.
 * @deprecated  Use SUPR3FreeModule().
 */
SUPR3DECL(int) SUPR3UnloadVMM(void);

/**
 * Get the physical address of the GIP.
 *
 * @returns VBox status code.
 * @param   pHCPhys     Where to store the physical address of the GIP.
 */
SUPR3DECL(int) SUPR3GipGetPhys(PRTHCPHYS pHCPhys);

/**
 * Initializes only the bits relevant for the SUPR3HardenedVerify* APIs.
 *
 * This is for users that don't necessarily need to initialize the whole of
 * SUPLib.  There is no harm in calling this one more time.
 *
 * @returns VBox status code.
 * @remarks Currently not counted, so only call once.
 */
SUPR3DECL(int) SUPR3HardenedVerifyInit(void);

/**
 * Reverses the effect of SUPR3HardenedVerifyInit if SUPR3InitEx hasn't been
 * called.
 *
 * Ignored if the support library was initialized using SUPR3Init or
 * SUPR3InitEx.
 *
 * @returns VBox status code.
 */
SUPR3DECL(int) SUPR3HardenedVerifyTerm(void);

/**
 * Verifies the integrity of a file, and optionally opens it.
 *
 * The integrity check is for whether the file is suitable for loading into
 * the hypervisor or VM process. The integrity check may include verifying
 * the authenticode/elfsign/whatever signature of the file, which can take
 * a little while.
 *
 * @returns VBox status code. On failure it will have printed a LogRel message.
 *
 * @param   pszFilename     The file.
 * @param   pszWhat         For the LogRel on failure.
 * @param   phFile          Where to store the handle to the opened file. This is optional, pass NULL
 *                          if the file should not be opened.
 * @deprecated Write a new one.
 */
SUPR3DECL(int) SUPR3HardenedVerifyFile(const char *pszFilename, const char *pszWhat, PRTFILE phFile);

/**
 * Verifies the integrity of a the current process, including the image
 * location and that the invocation was absolute.
 *
 * This must currently be called after initializing the runtime.  The intended
 * audience is set-uid-to-root applications, root services and similar.
 *
 * @returns VBox status code.  On failure
 *          message.
 * @param   pszArgv0        The first argument to main().
 * @param   fInternal       Set this to @c true if this is an internal
 *                          VirtualBox application.  Otherwise pass @c false.
 * @param   pErrInfo        Where to return extended error information.
 */
SUPR3DECL(int) SUPR3HardenedVerifySelf(const char *pszArgv0, bool fInternal, PRTERRINFO pErrInfo);

/**
 * Verifies the integrity of an installation directory.
 *
 * The integrity check verifies that the directory cannot be tampered with by
 * normal users on the system.  On Unix this translates to root ownership and
 * no symbolic linking.
 *
 * @returns VBox status code. On failure a message will be stored in @a pszErr.
 *
 * @param   pszDirPath      The directory path.
 * @param   fRecursive      Whether the check should be recursive or
 *                          not.  When set, all sub-directores will be checked,
 *                          including files (@a fCheckFiles is ignored).
 * @param   fCheckFiles     Whether to apply the same basic integrity check to
 *                          the files in the directory as the directory itself.
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3HardenedVerifyDir(const char *pszDirPath, bool fRecursive, bool fCheckFiles, PRTERRINFO pErrInfo);

/**
 * Verifies the integrity of a plug-in module.
 *
 * This is similar to SUPR3HardenedLdrLoad, except it does not load the module
 * and that the module does not have to be shipped with VirtualBox.
 *
 * @returns VBox status code. On failure a message will be stored in @a pszErr.
 *
 * @param   pszFilename     The filename of the plug-in module (nothing can be
 *                          omitted here).
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3HardenedVerifyPlugIn(const char *pszFilename, PRTERRINFO pErrInfo);

/**
 * Same as RTLdrLoad() but will verify the files it loads (hardened builds).
 *
 * Will add dll suffix if missing and try load the file.
 *
 * @returns iprt status code.
 * @param   pszFilename     Image filename. This must have a path.
 * @param   phLdrMod        Where to store the handle to the loaded module.
 * @param   fFlags          See RTLDRLOAD_FLAGS_XXX.
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3HardenedLdrLoad(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo);

/**
 * Same as RTLdrLoadAppPriv() but it will verify the files it loads (hardened
 * builds).
 *
 * Will add dll suffix to the file if missing, then look for it in the
 * architecture dependent application directory.
 *
 * @returns iprt status code.
 * @param   pszFilename     Image filename.
 * @param   phLdrMod        Where to store the handle to the loaded module.
 * @param   fFlags          See RTLDRLOAD_FLAGS_XXX.
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3HardenedLdrLoadAppPriv(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo);

/**
 * Same as RTLdrLoad() but will verify the files it loads (hardened builds).
 *
 * This differs from SUPR3HardenedLdrLoad() in that it can load modules from
 * extension packs and anything else safely installed on the system, provided
 * they pass the hardening tests.
 *
 * @returns iprt status code.
 * @param   pszFilename     The full path to the module, with extension.
 * @param   phLdrMod        Where to store the handle to the loaded module.
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
SUPR3DECL(int) SUPR3HardenedLdrLoadPlugIn(const char *pszFilename, PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo);

/**
 * Check if the host kernel can run in VMX root mode.
 *
 * @returns VINF_SUCCESS if supported, error code indicating why if not.
 * @param   ppszWhy         Where to return an explanatory message on failure.
 */
SUPR3DECL(int) SUPR3QueryVTxSupported(const char **ppszWhy);

/**
 * Return VT-x/AMD-V capabilities.
 *
 * @returns VINF_SUCCESS if supported, error code indicating why if not.
 * @param   pfCaps      Pointer to capability dword (out).
 * @todo Intended for main, which means we need to relax the privilege requires
 *       when accessing certain vboxdrv functions.
 */
SUPR3DECL(int) SUPR3QueryVTCaps(uint32_t *pfCaps);

/**
 * Check if NEM is supported when no VT-x/AMD-V is indicated by the CPU.
 *
 * This is really only for the windows case where we're running in a root
 * partition and isn't allowed to use the hardware directly.
 *
 * @returns True if NEM API support, false if not.
 */
SUPR3DECL(bool) SUPR3IsNemSupportedWhenNoVtxOrAmdV(void);

/**
 * Open the tracer.
 *
 * @returns VBox status code.
 * @param   uCookie         Cookie identifying the tracer we expect to talk to.
 * @param   uArg            Tracer specific open argument.
 */
SUPR3DECL(int) SUPR3TracerOpen(uint32_t uCookie, uintptr_t uArg);

/**
 * Closes the tracer.
 *
 * @returns VBox status code.
 */
SUPR3DECL(int) SUPR3TracerClose(void);

/**
 * Perform an I/O request on the tracer.
 *
 * @returns VBox status.
 * @param   uCmd                The tracer command.
 * @param   uArg                The argument.
 * @param   piRetVal            Where to store the tracer return value.
 */
SUPR3DECL(int) SUPR3TracerIoCtl(uintptr_t uCmd, uintptr_t uArg, int32_t *piRetVal);

/**
 * Registers the user module with the tracer.
 *
 * @returns VBox status code.
 * @param   hModNative          Native module handle.  Pass ~(uintptr_t)0 if not
 *                              at hand.
 * @param   pszModule           The module name.
 * @param   pVtgHdr             The VTG header.
 * @param   uVtgHdrAddr         The address to which the VTG header is loaded
 *                              in the relevant execution context.
 * @param   fFlags              See SUP_TRACER_UMOD_FLAGS_XXX
 */
SUPR3DECL(int) SUPR3TracerRegisterModule(uintptr_t hModNative, const char *pszModule, struct VTGOBJHDR *pVtgHdr,
                                         RTUINTPTR uVtgHdrAddr, uint32_t fFlags);

/**
 * Deregisters the user module.
 *
 * @returns VBox status code.
 * @param   pVtgHdr             The VTG header.
 */
SUPR3DECL(int) SUPR3TracerDeregisterModule(struct VTGOBJHDR *pVtgHdr);

/**
 * Fire the probe.
 *
 * @param   pVtgProbeLoc        The probe location record.
 * @param   uArg0               Raw probe argument 0.
 * @param   uArg1               Raw probe argument 1.
 * @param   uArg2               Raw probe argument 2.
 * @param   uArg3               Raw probe argument 3.
 * @param   uArg4               Raw probe argument 4.
 */
SUPDECL(void)  SUPTracerFireProbe(struct VTGPROBELOC *pVtgProbeLoc, uintptr_t uArg0, uintptr_t uArg1, uintptr_t uArg2,
                                  uintptr_t uArg3, uintptr_t uArg4);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(DOXYGEN_RUNNING)

/**
 * Attempts to read the value of an MSR.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR to read.
 * @param   idCpu               The CPU to read it on, NIL_RTCPUID if it doesn't
 *                              matter which CPU.
 * @param   puValue             Where to return the value.
 * @param   pfGp                Where to store the \#GP indicator for the read
 *                              operation.
 * @note    Only available on AMD64 & x86.
 */
SUPR3DECL(int) SUPR3MsrProberRead(uint32_t uMsr, RTCPUID idCpu, uint64_t *puValue, bool *pfGp);

/**
 * Attempts to write to an MSR.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR to write to.
 * @param   idCpu               The CPU to wrtie it on, NIL_RTCPUID if it
 *                              doesn't matter which CPU.
 * @param   uValue              The value to write.
 * @param   pfGp                Where to store the \#GP indicator for the write
 *                              operation.
 * @note    Only available on AMD64 & x86.
 */
SUPR3DECL(int) SUPR3MsrProberWrite(uint32_t uMsr, RTCPUID idCpu, uint64_t uValue, bool *pfGp);

/**
 * Attempts to modify the value of an MSR.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR to modify.
 * @param   idCpu               The CPU to modify it on, NIL_RTCPUID if it
 *                              doesn't matter which CPU.
 * @param   fAndMask            The bits to keep in the current MSR value.
 * @param   fOrMask             The bits to set before writing.
 * @param   pResult             The result buffer.
 * @note    Only available on AMD64 & x86.
 */
SUPR3DECL(int) SUPR3MsrProberModify(uint32_t uMsr, RTCPUID idCpu, uint64_t fAndMask, uint64_t fOrMask,
                                    PSUPMSRPROBERMODIFYRESULT pResult);

/**
 * Attempts to modify the value of an MSR, extended version.
 *
 * @returns VBox status code.
 * @param   uMsr                The MSR to modify.
 * @param   idCpu               The CPU to modify it on, NIL_RTCPUID if it
 *                              doesn't matter which CPU.
 * @param   fAndMask            The bits to keep in the current MSR value.
 * @param   fOrMask             The bits to set before writing.
 * @param   fFaster             If set to @c true some cache/tlb invalidation is
 *                              skipped, otherwise behave like
 *                              SUPR3MsrProberModify.
 * @param   pResult             The result buffer.
 * @note    Only available on AMD64 & x86.
 */
SUPR3DECL(int) SUPR3MsrProberModifyEx(uint32_t uMsr, RTCPUID idCpu, uint64_t fAndMask, uint64_t fOrMask, bool fFaster,
                                      PSUPMSRPROBERMODIFYRESULT pResult);

#endif /* defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(DOXYGEN_RUNNING) */

#if defined(RT_ARCH_ARM64) || defined(DOXYGEN_RUNNING)
/**
 * Gets a collection of ARM system registers useful for identify
 * CPU capatbilites.
 *
 * @returns VBox status code.
 * @param   idCpu               The CPU to query the registers on, NIL_RTCPUID
 *                              if any will do.
 * @param   fFlags              Query flags, SUP_ARM_SYS_REG_F_XXX.
 * @param   cMaxRegs            Maximum number of registers @a paSysRegValues
 *                              may hold.
 * @param   pcRegsReturned      Number of registers returned.
 * @param   pcRegsAvailable     Number of registers available, optional.
 *                              If higher than @a *pcRegsReturned, try again
 *                              with an array of this size to get them all.
 * @param   paSysRegValues      Array where to store the register values and
 *                              associated info.
 */
SUPR3DECL(int) SUPR3ArmQuerySysRegs(RTCPUID idCpu, uint32_t fFlags, uint32_t cMaxRegs,
                                    uint32_t *pcRegsReturned, uint32_t *pcRegsAvailable, PSUPARMSYSREGVAL paSysRegValues);
#endif /* defined(RT_ARCH_ARM64) || defined(DOXYGEN_RUNNING) */

/**
 * Resume built-in keyboard on MacBook Air and Pro hosts.
 *
 * @returns VBox status code.
 */
SUPR3DECL(int) SUPR3ResumeSuspendedKeyboards(void);

/**
 * Measure the TSC-delta for the specified CPU.
 *
 * @returns VBox status code.
 * @param   idCpu               The CPU to measure the TSC-delta for.
 * @param   fAsync              Whether the measurement is asynchronous, returns
 *                              immediately after signalling a measurement
 *                              request.
 * @param   fForce              Whether to perform a measurement even if the
 *                              specified CPU has a (possibly) valid TSC delta.
 * @param   cRetries            Number of times to retry failed delta
 *                              measurements.
 * @param   cMsWaitRetry        Number of milliseconds to wait between retries.
 */
SUPR3DECL(int) SUPR3TscDeltaMeasure(RTCPUID idCpu, bool fAsync, bool fForce, uint8_t cRetries, uint8_t cMsWaitRetry);

/**
 * Reads the delta-adjust TSC value.
 *
 * @returns VBox status code.
 * @param   puTsc           Where to store the read TSC value.
 * @param   pidApic         Where to store the APIC ID of the CPU where the TSC
 *                          was read (optional, can be NULL).
 */
SUPR3DECL(int) SUPR3ReadTsc(uint64_t *puTsc, uint16_t *pidApic);

/**
 * Modifies the GIP flags.
 *
 * @returns VBox status code.
 * @param   fOrMask         The OR mask of the GIP flags, see SUPGIP_FLAGS_XXX.
 * @param   fAndMask        The AND mask of the GIP flags, see SUPGIP_FLAGS_XXX.
 */
SUPR3DECL(int) SUPR3GipSetFlags(uint32_t fOrMask, uint32_t fAndMask);

/**
 * Return processor microcode revision, if applicable.
 *
 * @returns VINF_SUCCESS if supported, error code indicating why if not.
 * @param   puMicrocodeRev  Pointer to microcode revision dword (out).
 */
SUPR3DECL(int) SUPR3QueryMicrocodeRev(uint32_t *puMicrocodeRev);

/**
 * Gets hardware-virtualization MSRs of the CPU, if available.
 *
 * @returns VINF_SUCCESS if available, error code indicating why if not.
 * @param   pHwvirtMsrs     Where to store the hardware-virtualization MSRs.
 * @param   fForceRequery   Whether to force complete re-querying of MSRs (rather
 *                          than fetching cached values when available).
 */
SUPR3DECL(int) SUPR3GetHwvirtMsrs(PSUPHWVIRTMSRS pHwvirtMsrs, bool fForceRequery);

/** @} */
#endif /* IN_RING3 */


/** @name User mode module flags (SUPR3TracerRegisterModule & SUP_IOCTL_TRACER_UMOD_REG).
 * @{ */
/** Executable image. */
#define SUP_TRACER_UMOD_FLAGS_EXE           UINT32_C(1)
/** Shared library (DLL, DYLIB, SO, etc). */
#define SUP_TRACER_UMOD_FLAGS_SHARED        UINT32_C(2)
/** Image type mask. */
#define SUP_TRACER_UMOD_FLAGS_TYPE_MASK     UINT32_C(3)
/** @} */


#ifdef IN_RING0
/** @defgroup   grp_sup_r0     SUP Host Context Ring-0 API
 * @{
 */

/**
 * Security objectype.
 */
typedef enum SUPDRVOBJTYPE
{
    /** The usual invalid object. */
    SUPDRVOBJTYPE_INVALID = 0,
    /** A Virtual Machine instance. */
    SUPDRVOBJTYPE_VM,
    /** Internal network. */
    SUPDRVOBJTYPE_INTERNAL_NETWORK,
    /** Internal network interface. */
    SUPDRVOBJTYPE_INTERNAL_NETWORK_INTERFACE,
    /** Single release event semaphore. */
    SUPDRVOBJTYPE_SEM_EVENT,
    /** Multiple release event semaphore. */
    SUPDRVOBJTYPE_SEM_EVENT_MULTI,
    /** Raw PCI device. */
    SUPDRVOBJTYPE_RAW_PCI_DEVICE,
    /** The first invalid object type in this end. */
    SUPDRVOBJTYPE_END,
    /** The usual 32-bit type size hack. */
    SUPDRVOBJTYPE_32_BIT_HACK = 0x7ffffff
} SUPDRVOBJTYPE;

/**
 * Object destructor callback.
 * This is called for reference counted objectes when the count reaches 0.
 *
 * @param   pvObj       The object pointer.
 * @param   pvUser1     The first user argument.
 * @param   pvUser2     The second user argument.
 */
typedef DECLCALLBACKTYPE(void, FNSUPDRVDESTRUCTOR,(void *pvObj, void *pvUser1, void *pvUser2));
/** Pointer to a FNSUPDRVDESTRUCTOR(). */
typedef FNSUPDRVDESTRUCTOR *PFNSUPDRVDESTRUCTOR;

/**
 * Service request callback function.
 *
 * @returns VBox status code.
 * @param   pSession    The caller's session.
 * @param   uOperation  The operation identifier.
 * @param   u64Arg      64-bit integer argument.
 * @param   pReqHdr     The request header. Input / Output. Optional.
 */
typedef DECLCALLBACKTYPE(int, FNSUPR0SERVICEREQHANDLER,(PSUPDRVSESSION pSession, uint32_t uOperation,
                                                        uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr));
/** Pointer to a FNR0SERVICEREQHANDLER(). */
typedef R0PTRTYPE(FNSUPR0SERVICEREQHANDLER *) PFNSUPR0SERVICEREQHANDLER;

/**
 * Symbol entry for a wrapped module (SUPLDRWRAPPEDMODULE).
 */
typedef struct SUPLDRWRAPMODSYMBOL
{
    /** The symbol namel. */
    const char *pszSymbol;
    /** The symbol address/value. */
    PFNRT       pfnValue;
} SUPLDRWRAPMODSYMBOL;
/** Pointer to a symbol entry for a wrapped module. */
typedef SUPLDRWRAPMODSYMBOL const *PCSUPLDRWRAPMODSYMBOL;

/**
 * Registration structure for SUPR0LdrRegisterWrapperModule.
 *
 * This is used to register a .r0 module when loaded manually as a native kernel
 * module/extension/driver/whatever.
 */
typedef struct SUPLDRWRAPPEDMODULE
{
    /** Magic value (SUPLDRWRAPPEDMODULE_MAGIC). */
    uint32_t                    uMagic;
    /** The structure version. */
    uint16_t                    uVersion;
    /** SUPLDRWRAPPEDMODULE_F_XXX.   */
    uint16_t                    fFlags;

    /** As close as possible to the start of the image. */
    void                       *pvImageStart;
    /** As close as possible to the end of the image. */
    void                       *pvImageEnd;

    /** @name Standar entry points
     * @{ */
    /** Pointer to the module initialization function (optional). */
    DECLCALLBACKMEMBER(int,     pfnModuleInit,(void *hMod));
    /** Pointer to the module termination function (optional). */
    DECLCALLBACKMEMBER(void,    pfnModuleTerm,(void *hMod));
    /** The VMMR0EntryFast entry point for VMMR0. */
    PFNRT                       pfnVMMR0EntryFast;
    /** The VMMR0EntryEx entry point for VMMR0. */
    PFNRT                       pfnVMMR0EntryEx;
    /** The service request handler entry point. */
    PFNSUPR0SERVICEREQHANDLER   pfnServiceReqHandler;
    /** @} */

    /** The symbol table. */
    PCSUPLDRWRAPMODSYMBOL       paSymbols;
    /** Number of symbols. */
    uint32_t                    cSymbols;

    /** The normal VBox module name. */
    char                        szName[32];
    /** Repeating the magic value here (SUPLDRWRAPPEDMODULE_MAGIC). */
    uint32_t                    uEndMagic;
} SUPLDRWRAPPEDMODULE;
/** Pointer to the wrapped module registration structure. */
typedef SUPLDRWRAPPEDMODULE const *PCSUPLDRWRAPPEDMODULE;

/** Magic value for the wrapped module structure (Doris lessing). */
#define SUPLDRWRAPPEDMODULE_MAGIC       UINT32_C(0x19191117)
/** Current SUPLDRWRAPPEDMODULE structure version. */
#define SUPLDRWRAPPEDMODULE_VERSION     UINT16_C(0x0001)

/** Set if this is the VMMR0 module.   */
#define SUPLDRWRAPPEDMODULE_F_VMMR0     UINT16_C(0x0001)


SUPR0DECL(void *) SUPR0ObjRegister(PSUPDRVSESSION pSession, SUPDRVOBJTYPE enmType, PFNSUPDRVDESTRUCTOR pfnDestructor, void *pvUser1, void *pvUser2);
SUPR0DECL(int) SUPR0ObjAddRef(void *pvObj, PSUPDRVSESSION pSession);
SUPR0DECL(int) SUPR0ObjAddRefEx(void *pvObj, PSUPDRVSESSION pSession, bool fNoBlocking);
SUPR0DECL(int) SUPR0ObjRelease(void *pvObj, PSUPDRVSESSION pSession);
SUPR0DECL(int) SUPR0ObjVerifyAccess(void *pvObj, PSUPDRVSESSION pSession, const char *pszObjName);

SUPR0DECL(PVM) SUPR0GetSessionVM(PSUPDRVSESSION pSession);
SUPR0DECL(PGVM) SUPR0GetSessionGVM(PSUPDRVSESSION pSession);
SUPR0DECL(int) SUPR0SetSessionVM(PSUPDRVSESSION pSession, PGVM pGVM, PVM pVM);
SUPR0DECL(RTUID) SUPR0GetSessionUid(PSUPDRVSESSION pSession);

SUPR0DECL(int) SUPR0LockMem(PSUPDRVSESSION pSession, RTR3PTR pvR3, uint32_t cPages, PRTHCPHYS paPages);
SUPR0DECL(int) SUPR0UnlockMem(PSUPDRVSESSION pSession, RTR3PTR pvR3);
SUPR0DECL(int) SUPR0ContAlloc(PSUPDRVSESSION pSession, uint32_t cPages, PRTR0PTR ppvR0, PRTR3PTR ppvR3, PRTHCPHYS pHCPhys);
SUPR0DECL(int) SUPR0ContFree(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr);
SUPR0DECL(int) SUPR0LowAlloc(PSUPDRVSESSION pSession, uint32_t cPages, PRTR0PTR ppvR0, PRTR3PTR ppvR3, PRTHCPHYS paPages);
SUPR0DECL(int) SUPR0LowFree(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr);
SUPR0DECL(int) SUPR0MemAlloc(PSUPDRVSESSION pSession, uint32_t cb, PRTR0PTR ppvR0, PRTR3PTR ppvR3);
SUPR0DECL(int) SUPR0MemGetPhys(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr, PSUPPAGE paPages);
SUPR0DECL(int) SUPR0MemFree(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr);
SUPR0DECL(int) SUPR0PageAllocEx(PSUPDRVSESSION pSession, uint32_t cPages, uint32_t fFlags, PRTR3PTR ppvR3, PRTR0PTR ppvR0, PRTHCPHYS paPages);
SUPR0DECL(int) SUPR0PageMapKernel(PSUPDRVSESSION pSession, RTR3PTR pvR3, uint32_t offSub, uint32_t cbSub, uint32_t fFlags, PRTR0PTR ppvR0);
SUPR0DECL(int) SUPR0PageProtect(PSUPDRVSESSION pSession, RTR3PTR pvR3, RTR0PTR pvR0, uint32_t offSub, uint32_t cbSub, uint32_t fProt);
SUPR0DECL(int) SUPR0PageFree(PSUPDRVSESSION pSession, RTR3PTR pvR3);
SUPR0DECL(int) SUPR0GipMap(PSUPDRVSESSION pSession, PRTR3PTR ppGipR3, PRTHCPHYS pHCPhysGip);
SUPR0DECL(int) SUPR0LdrLock(PSUPDRVSESSION pSession);
SUPR0DECL(int) SUPR0LdrUnlock(PSUPDRVSESSION pSession);
SUPR0DECL(bool) SUPR0LdrIsLockOwnerByMod(void *hMod, bool fWantToHear);
SUPR0DECL(int) SUPR0LdrModByName(PSUPDRVSESSION pSession, const char *pszName, void **phMod);
SUPR0DECL(int) SUPR0LdrModRetain(PSUPDRVSESSION pSession, void *hMod);
SUPR0DECL(int) SUPR0LdrModRelease(PSUPDRVSESSION pSession, void *hMod);
#ifdef RT_OS_LINUX
SUPR0DECL(int) SUPDrvLinuxLdrRegisterWrappedModule(PCSUPLDRWRAPPEDMODULE pWrappedModInfo, const char *pszLnxModName, void **phMod);
SUPR0DECL(int) SUPDrvLinuxLdrDeregisterWrappedModule(PCSUPLDRWRAPPEDMODULE pWrappedModInfo, void **phMod);
#endif
SUPR0DECL(int) SUPR0GetVTSupport(uint32_t *pfCaps);
SUPR0DECL(int) SUPR0GetHwvirtMsrs(PSUPHWVIRTMSRS pMsrs, uint32_t fCaps, bool fForce);
SUPR0DECL(int) SUPR0GetSvmUsability(bool fInitSvm);
SUPR0DECL(int) SUPR0GetVmxUsability(bool *pfIsSmxModeAmbiguous);
SUPR0DECL(int) SUPR0GetCurrentGdtRw(RTHCUINTPTR *pGdtRw);
SUPR0DECL(int) SUPR0QueryVTCaps(PSUPDRVSESSION pSession, uint32_t *pfCaps);
SUPR0DECL(int) SUPR0GipUnmap(PSUPDRVSESSION pSession);
SUPR0DECL(int) SUPR0QueryUcodeRev(PSUPDRVSESSION pSession, uint32_t *puMicrocodeRev);
SUPR0DECL(SUPPAGINGMODE) SUPR0GetPagingMode(void);
SUPR0DECL(RTCCUINTREG) SUPR0ChangeCR4(RTCCUINTREG fOrMask, RTCCUINTREG fAndMask);
SUPR0DECL(int) SUPR0EnableVTx(bool fEnable);
SUPR0DECL(bool) SUPR0SuspendVTxOnCpu(void);
SUPR0DECL(void) SUPR0ResumeVTxOnCpu(bool fSuspended);
#define SUP_TSCDELTA_MEASURE_F_FORCE        RT_BIT_32(0)
#define SUP_TSCDELTA_MEASURE_F_ASYNC        RT_BIT_32(1)
#define SUP_TSCDELTA_MEASURE_F_VALID_MASK   UINT32_C(0x00000003)
SUPR0DECL(int) SUPR0TscDeltaMeasureBySetIndex(PSUPDRVSESSION pSession, uint32_t iCpuSet, uint32_t fFlags,
                                              RTMSINTERVAL cMsWaitRetry, RTMSINTERVAL cMsWaitThread, uint32_t cTries);

SUPR0DECL(void) SUPR0BadContext(PSUPDRVSESSION pSession, const char *pszFile, uint32_t uLine, const char *pszExpr);

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
/**
 * Translates a physical address to a virtual mapping (valid up to end of page).
 * @returns VBox status code.
 * @param   HCPhys      The physical address, must be page aligned.
 * @param   ppv         Where to store the mapping address on success.
 */
SUPR0DECL(int) SUPR0HCPhysToVirt(RTHCPHYS HCPhys, void **ppv);
#endif

/** Context structure returned by SUPR0IoCtlSetup for use with
 * SUPR0IoCtlPerform and cleaned up by SUPR0IoCtlCleanup. */
typedef struct SUPR0IOCTLCTX *PSUPR0IOCTLCTX;

/**
 * Sets up a I/O control context for the given handle.
 *
 * @returns VBox status code.
 * @param   pSession        The support driver session.
 * @param   hHandle         The handle.
 * @param   fFlags          Flag, MBZ.
 * @param   ppCtx           Where the context is returned.
 */
SUPR0DECL(int) SUPR0IoCtlSetupForHandle(PSUPDRVSESSION pSession, intptr_t hHandle, uint32_t fFlags, PSUPR0IOCTLCTX *ppCtx);

/**
 * Cleans up the I/O control context when done.
 *
 * This won't close the handle passed to SUPR0IoCtlSetupForHandle.
 *
 * @returns VBox status code.
 * @param   pCtx            The I/O control context to cleanup.
 */
SUPR0DECL(int) SUPR0IoCtlCleanup(PSUPR0IOCTLCTX pCtx);

/**
 * Performs an I/O control operation.
 *
 * @returns VBox status code.
 * @param   pCtx            The I/O control context returned by
 *                          SUPR0IoCtlSetupForHandle.
 * @param   uFunction       The I/O control function to perform.
 * @param   pvInput         Pointer to input buffer (ring-0).
 * @param   pvInputUser     Ring-3 pointer corresponding to @a pvInput.
 * @param   cbInput         The amount of input.  If zero, both input pointers
 *                          are expected to be NULL.
 * @param   pvOutput        Pointer to output buffer (ring-0).
 * @param   pvOutputUser    Ring-3 pointer corresponding to @a pvInput.
 * @param   cbOutput        The amount of input.  If zero, both input pointers
 *                          are expected to be NULL.
 * @param   piNativeRc      Where to return the native return code.   When
 *                          specified the VBox status code will typically be
 *                          VINF_SUCCESS and the caller have to consult this for
 *                          the actual result of the operation.  (This saves
 *                          pointless status code conversion.)  Optional.
 *
 * @note    On unix systems where there is only one set of buffers possible,
 *          pass the same pointers as input and output.
 */
SUPR0DECL(int)  SUPR0IoCtlPerform(PSUPR0IOCTLCTX pCtx, uintptr_t uFunction,
                                  void *pvInput, RTR3PTR pvInputUser, size_t cbInput,
                                  void *pvOutput, RTR3PTR pvOutputUser, size_t cbOutput,
                                  int32_t *piNativeRc);

/**
 * Writes to the debugger and/or kernel log, va_list version.
 *
 * The length of the formatted message is somewhat limited, so keep things short
 * and to the point.
 *
 * @returns Number of bytes written, mabye.
 * @param   pszFormat       IPRT format string.
 * @param   va              Arguments referenced by the format string.
 */
SUPR0DECL(int)  SUPR0PrintfV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Writes to the debugger and/or kernel log.
 *
 * The length of the formatted message is somewhat limited, so keep things short
 * and to the point.
 *
 * @returns Number of bytes written, mabye.
 * @param   pszFormat       IPRT format string.
 * @param   ...             Arguments referenced by the format string.
 */
#if defined(__GNUC__) && defined(__inline__)
/* Define it as static for GCC as it cannot inline functions using va_start() anyway,
   and linux redefines __inline__ to always inlining forcing gcc to issue an error. */
static int __attribute__((__unused__))
#else
DECLINLINE(int)
#endif
RT_IPRT_FORMAT_ATTR(1, 2) SUPR0Printf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    SUPR0PrintfV(pszFormat, va);
    va_end(va);
    return 0;
}

/* HACK ALERT! See above. */
#ifdef SUPR0PRINTF_UNDO_INLINE_HACK
# define __inline__ inline
#endif

#ifdef IN_RING0
/** Debug printf macro. This also exist in SUPLib, see SUPLibInternal.h. */
# ifdef DEBUG
#  define SUP_DPRINTF(a) SUPR0Printf a
# else
#  define SUP_DPRINTF(a) do { } while (0)
# endif
#endif

/**
 * Returns configuration flags of the host kernel.
 *
 * @returns Combination of SUPKERNELFEATURES_XXX flags.
 */
SUPR0DECL(uint32_t) SUPR0GetKernelFeatures(void);

/**
 * Notification from R0 VMM prior to loading the guest-FPU register state.
 *
 * @returns Whether the host-FPU register state has been saved by the host kernel.
 * @param   fCtxHook    Whether thread-context hooks are enabled.
 *
 * @remarks Called with preemption disabled.
 */
SUPR0DECL(bool) SUPR0FpuBegin(bool fCtxHook);

/**
 * Notification from R0 VMM after saving the guest-FPU register state (and
 * potentially restoring the host-FPU register state) in ring-0.
 *
 * @param   fCtxHook    Whether thread-context hooks are enabled.
 *
 * @remarks Called with preemption disabled.
 */
SUPR0DECL(void) SUPR0FpuEnd(bool fCtxHook);

/** @copydoc RTLogDefaultInstanceEx
 * @remarks To allow overriding RTLogDefaultInstanceEx locally. */
SUPR0DECL(struct RTLOGGER *) SUPR0DefaultLogInstanceEx(uint32_t fFlagsAndGroup);
/** @copydoc RTLogGetDefaultInstanceEx
 * @remarks To allow overriding RTLogGetDefaultInstanceEx locally. */
SUPR0DECL(struct RTLOGGER *) SUPR0GetDefaultLogInstanceEx(uint32_t fFlagsAndGroup);
/** @copydoc RTLogRelGetDefaultInstanceEx
 * @remarks To allow overriding RTLogRelGetDefaultInstanceEx locally. */
SUPR0DECL(struct RTLOGGER *) SUPR0GetDefaultLogRelInstanceEx(uint32_t fFlagsAndGroup);


/** @name Absolute symbols
 * Take the address of these, don't try call them.
 * @{ */
SUPR0DECL(void) SUPR0AbsIs64bit(void);
SUPR0DECL(void) SUPR0Abs64bitKernelCS(void);
SUPR0DECL(void) SUPR0Abs64bitKernelSS(void);
SUPR0DECL(void) SUPR0Abs64bitKernelDS(void);
SUPR0DECL(void) SUPR0AbsKernelCS(void);
SUPR0DECL(void) SUPR0AbsKernelSS(void);
SUPR0DECL(void) SUPR0AbsKernelDS(void);
SUPR0DECL(void) SUPR0AbsKernelES(void);
SUPR0DECL(void) SUPR0AbsKernelFS(void);
SUPR0DECL(void) SUPR0AbsKernelGS(void);
/** @} */

/**
 * Support driver component factory.
 *
 * Component factories are registered by drivers that provides services
 * such as the host network interface filtering and access to the host
 * TCP/IP stack.
 *
 * @remark Module dependencies and making sure that a component doesn't
 *         get unloaded while in use, is the sole responsibility of the
 *         driver/kext/whatever implementing the component.
 */
typedef struct SUPDRVFACTORY
{
    /** The (unique) name of the component factory. */
    char szName[56];
    /**
     * Queries a factory interface.
     *
     * The factory interface is specific to each component and will be be
     * found in the header(s) for the component alongside its UUID.
     *
     * @returns Pointer to the factory interfaces on success, NULL on failure.
     *
     * @param   pSupDrvFactory      Pointer to this structure.
     * @param   pSession            The SUPDRV session making the query.
     * @param   pszInterfaceUuid    The UUID of the factory interface.
     */
    DECLR0CALLBACKMEMBER(void *, pfnQueryFactoryInterface,(struct SUPDRVFACTORY const *pSupDrvFactory, PSUPDRVSESSION pSession, const char *pszInterfaceUuid));
} SUPDRVFACTORY;
/** Pointer to a support driver factory. */
typedef SUPDRVFACTORY *PSUPDRVFACTORY;
/** Pointer to a const support driver factory. */
typedef SUPDRVFACTORY const *PCSUPDRVFACTORY;

SUPR0DECL(int) SUPR0ComponentRegisterFactory(PSUPDRVSESSION pSession, PCSUPDRVFACTORY pFactory);
SUPR0DECL(int) SUPR0ComponentDeregisterFactory(PSUPDRVSESSION pSession, PCSUPDRVFACTORY pFactory);
SUPR0DECL(int) SUPR0ComponentQueryFactory(PSUPDRVSESSION pSession, const char *pszName, const char *pszInterfaceUuid, void **ppvFactoryIf);


/** @name Tracing
 * @{ */

/**
 * Tracer data associated with a provider.
 */
typedef union SUPDRVTRACERDATA
{
    /** Generic */
    uint64_t                    au64[2];

    /** DTrace data. */
    struct
    {
        /** Provider ID. */
        uintptr_t               idProvider;
        /** The number of trace points provided. */
        uint32_t volatile       cProvidedProbes;
        /** Whether we've invalidated this bugger. */
        bool                    fZombie;
    } DTrace;
} SUPDRVTRACERDATA;
/** Pointer to the tracer data associated with a provider. */
typedef SUPDRVTRACERDATA *PSUPDRVTRACERDATA;

/**
 * Probe location info for ring-0.
 *
 * Since we cannot trust user tracepoint modules, we need to duplicate the probe
 * ID and enabled flag in ring-0.
 */
typedef struct SUPDRVPROBELOC
{
    /** The probe ID. */
    uint32_t                idProbe;
    /** Whether it's enabled or not. */
    bool                    fEnabled;
} SUPDRVPROBELOC;
/** Pointer to a ring-0 probe location record. */
typedef SUPDRVPROBELOC *PSUPDRVPROBELOC;

/**
 * Probe info for ring-0.
 *
 * Since we cannot trust user tracepoint modules, we need to duplicate the
 * probe enable count.
 */
typedef struct SUPDRVPROBEINFO
{
    /** The number of times this probe has been enabled. */
    uint32_t volatile           cEnabled;
} SUPDRVPROBEINFO;
/** Pointer to a ring-0 probe info record. */
typedef SUPDRVPROBEINFO *PSUPDRVPROBEINFO;

/**
 * Support driver tracepoint provider core.
 */
typedef struct SUPDRVVDTPROVIDERCORE
{
    /** The tracer data member. */
    SUPDRVTRACERDATA            TracerData;
    /** Pointer to the provider name (a copy that's always available). */
    const char                 *pszName;
    /** Pointer to the module name (a copy that's always available). */
    const char                 *pszModName;

    /** The provider descriptor. */
    struct VTGDESCPROVIDER     *pDesc;
    /** The VTG header. */
    struct VTGOBJHDR           *pHdr;

    /** The size of the entries in the pvProbeLocsEn table. */
    uint8_t                     cbProbeLocsEn;
    /** The actual module bit count (corresponds to cbProbeLocsEn). */
    uint8_t                     cBits;
    /** Set if this is a Umod, otherwise clear. */
    bool                        fUmod;
    /** Explicit alignment padding (paranoia). */
    uint8_t                     abAlignment[ARCH_BITS == 32 ? 1 : 5];

    /** The probe locations used for descriptive purposes. */
    struct VTGPROBELOC const   *paProbeLocsRO;
    /** Pointer to the probe location array where the enable flag needs
     * flipping. For kernel providers, this will always be SUPDRVPROBELOC,
     * while user providers can either be 32-bit or 64-bit.  Use
     * cbProbeLocsEn to calculate the address of an entry. */
    void                       *pvProbeLocsEn;
    /** Pointer to the probe array containing the enabled counts. */
    uint32_t                   *pacProbeEnabled;

    /** The ring-0 probe location info for user tracepoint modules.
     * This is NULL if fUmod is false. */
    PSUPDRVPROBELOC             paR0ProbeLocs;
    /** The ring-0 probe info for user tracepoint modules.
     * This is NULL if fUmod is false. */
    PSUPDRVPROBEINFO            paR0Probes;

} SUPDRVVDTPROVIDERCORE;
/** Pointer to a tracepoint provider core structure. */
typedef SUPDRVVDTPROVIDERCORE *PSUPDRVVDTPROVIDERCORE;

/** Pointer to a tracer registration record. */
typedef struct SUPDRVTRACERREG const *PCSUPDRVTRACERREG;
/**
 * Support driver tracer registration record.
 */
typedef struct SUPDRVTRACERREG
{
    /** Magic value (SUPDRVTRACERREG_MAGIC). */
    uint32_t                    u32Magic;
    /** Version (SUPDRVTRACERREG_VERSION). */
    uint32_t                    u32Version;

    /**
     * Fire off a kernel probe.
     *
     * @param   pVtgProbeLoc    The probe location record.
     * @param   uArg0           The first raw probe argument.
     * @param   uArg1           The second raw probe argument.
     * @param   uArg2           The third raw probe argument.
     * @param   uArg3           The fourth raw probe argument.
     * @param   uArg4           The fifth raw probe argument.
     *
     * @remarks SUPR0TracerFireProbe will do a tail jump thru this member, so
     *          no extra stack frames will be added.
     * @remarks This does not take a 'this' pointer argument because it doesn't map
     *          well onto VTG or DTrace.
     *
     */
    DECLR0CALLBACKMEMBER(void, pfnProbeFireKernel, (struct VTGPROBELOC *pVtgProbeLoc, uintptr_t uArg0, uintptr_t uArg1, uintptr_t uArg2,
                                                    uintptr_t uArg3, uintptr_t uArg4));

    /**
     * Fire off a user-mode probe.
     *
     * @param   pThis           Pointer to the registration record.
     *
     * @param   pVtgProbeLoc    The probe location record.
     * @param   pSession        The user session.
     * @param   pCtx            The usermode context info.
     * @param   pVtgHdr         The VTG header (read-only).
     * @param   pProbeLocRO     The read-only probe location record .
     */
    DECLR0CALLBACKMEMBER(void, pfnProbeFireUser, (PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, PCSUPDRVTRACERUSRCTX pCtx,
                                                  struct VTGOBJHDR const *pVtgHdr, struct VTGPROBELOC const *pProbeLocRO));

    /**
     * Opens up the tracer.
     *
     * @returns VBox status code.
     * @param   pThis           Pointer to the registration record.
     * @param   pSession        The session doing the opening.
     * @param   uCookie         A cookie (magic) unique to the tracer, so it can
     *                          fend off incompatible clients.
     * @param   uArg            Tracer specific argument.
     * @param   puSessionData   Pointer to the session data variable.  This must be
     *                          set to a non-zero value on success.
     */
    DECLR0CALLBACKMEMBER(int,   pfnTracerOpen, (PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uint32_t uCookie, uintptr_t uArg,
                                                uintptr_t *puSessionData));

    /**
     * I/O control style tracer communication method.
     *
     *
     * @returns VBox status code.
     * @param   pThis           Pointer to the registration record.
     * @param   pSession        The session.
     * @param   uSessionData    The session data value.
     * @param   uCmd            The tracer specific command.
     * @param   uArg            The tracer command specific argument.
     * @param   piRetVal        The tracer specific return value.
     */
    DECLR0CALLBACKMEMBER(int,   pfnTracerIoCtl, (PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uintptr_t uSessionData,
                                                 uintptr_t uCmd, uintptr_t uArg, int32_t *piRetVal));

    /**
     * Cleans up data the tracer has associated with a session.
     *
     * @param   pThis           Pointer to the registration record.
     * @param   pSession        The session handle.
     * @param   uSessionData    The data assoicated with the session.
     */
    DECLR0CALLBACKMEMBER(void,  pfnTracerClose, (PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uintptr_t uSessionData));

    /**
     * Registers a provider.
     *
     * @returns VBox status code.
     * @param   pThis           Pointer to the registration record.
     * @param   pCore           The provider core data.
     *
     * @todo Kernel vs. Userland providers.
     */
    DECLR0CALLBACKMEMBER(int,   pfnProviderRegister, (PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore));

    /**
     * Attempts to deregisters a provider.
     *
     * @returns VINF_SUCCESS or VERR_TRY_AGAIN.  If the latter, the provider
     *          should be made as harmless as possible before returning as the
     *          VTG object and associated code will be unloaded upon return.
     *
     * @param   pThis           Pointer to the registration record.
     * @param   pCore           The provider core data.
     */
    DECLR0CALLBACKMEMBER(int,   pfnProviderDeregister, (PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore));

    /**
     * Make another attempt at unregister a busy provider.
     *
     * @returns VINF_SUCCESS or VERR_TRY_AGAIN.
     * @param   pThis           Pointer to the registration record.
     * @param   pCore           The provider core data.
     */
    DECLR0CALLBACKMEMBER(int,   pfnProviderDeregisterZombie, (PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore));

    /** End marker (SUPDRVTRACERREG_MAGIC). */
    uintptr_t                   uEndMagic;
} SUPDRVTRACERREG;

/** Tracer magic (Kenny Garrett). */
#define SUPDRVTRACERREG_MAGIC   UINT32_C(0x19601009)
/** Tracer registration structure version. */
#define SUPDRVTRACERREG_VERSION RT_MAKE_U32(0, 1)

/** Pointer to a trace helper structure. */
typedef struct SUPDRVTRACERHLP const *PCSUPDRVTRACERHLP;
/**
 * Helper structure.
 */
typedef struct SUPDRVTRACERHLP
{
    /** The structure version (SUPDRVTRACERHLP_VERSION). */
    uintptr_t                   uVersion;

    /** @todo ... */

    /** End marker (SUPDRVTRACERHLP_VERSION) */
    uintptr_t                   uEndVersion;
} SUPDRVTRACERHLP;
/** Tracer helper structure version. */
#define SUPDRVTRACERHLP_VERSION RT_MAKE_U32(0, 1)

SUPR0DECL(int)  SUPR0TracerRegisterImpl(void *hMod, PSUPDRVSESSION pSession, PCSUPDRVTRACERREG pReg, PCSUPDRVTRACERHLP *ppHlp);
SUPR0DECL(int)  SUPR0TracerDeregisterImpl(void *hMod, PSUPDRVSESSION pSession);
SUPR0DECL(int)  SUPR0TracerRegisterDrv(PSUPDRVSESSION pSession, struct VTGOBJHDR *pVtgHdr, const char *pszName);
SUPR0DECL(void) SUPR0TracerDeregisterDrv(PSUPDRVSESSION pSession);
SUPR0DECL(int)  SUPR0TracerRegisterModule(void *hMod, struct VTGOBJHDR *pVtgHdr);
SUPR0DECL(void) SUPR0TracerFireProbe(struct VTGPROBELOC *pVtgProbeLoc, uintptr_t uArg0, uintptr_t uArg1, uintptr_t uArg2,
                                     uintptr_t uArg3, uintptr_t uArg4);
SUPR0DECL(void) SUPR0TracerUmodProbeFire(PSUPDRVSESSION pSession, PSUPDRVTRACERUSRCTX pCtx);
/** @}  */


/** @defgroup   grp_sup_r0_idc  The IDC Interface
 * @{
 */

/** The current SUPDRV IDC version.
 * This follows the usual high word / low word rules, i.e. high word is the
 * major number and it signifies incompatible interface changes. */
#define SUPDRV_IDC_VERSION      UINT32_C(0x00010000)

/**
 * Inter-Driver Communication Handle.
 */
typedef union SUPDRVIDCHANDLE
{
    /** Padding for opaque usage.
     * Must be greater or equal in size than the private struct. */
    void *apvPadding[4];
#ifdef SUPDRVIDCHANDLEPRIVATE_DECLARED
    /** The private view. */
    struct SUPDRVIDCHANDLEPRIVATE s;
#endif
} SUPDRVIDCHANDLE;
/** Pointer to a handle. */
typedef SUPDRVIDCHANDLE *PSUPDRVIDCHANDLE;

SUPR0DECL(int) SUPR0IdcOpen(PSUPDRVIDCHANDLE pHandle, uint32_t uReqVersion, uint32_t uMinVersion,
                            uint32_t *puSessionVersion, uint32_t *puDriverVersion, uint32_t *puDriverRevision);
SUPR0DECL(int) SUPR0IdcCall(PSUPDRVIDCHANDLE pHandle, uint32_t iReq, void *pvReq, uint32_t cbReq);
SUPR0DECL(int) SUPR0IdcClose(PSUPDRVIDCHANDLE pHandle);
SUPR0DECL(PSUPDRVSESSION) SUPR0IdcGetSession(PSUPDRVIDCHANDLE pHandle);
SUPR0DECL(int) SUPR0IdcComponentRegisterFactory(PSUPDRVIDCHANDLE pHandle, PCSUPDRVFACTORY pFactory);
SUPR0DECL(int) SUPR0IdcComponentDeregisterFactory(PSUPDRVIDCHANDLE pHandle, PCSUPDRVFACTORY pFactory);

/** @} */

/** @name Ring-0 module entry points.
 *
 * These can be exported by ring-0 modules SUP are told to load.
 *
 * @{ */
DECLEXPORT(int)  ModuleInit(void *hMod);
DECLEXPORT(void) ModuleTerm(void *hMod);
/** @}  */


/** @} */
#endif


/** @name Trust Anchors and Certificates
 * @{ */

/**
 * Trust anchor table entry (in generated Certificates.cpp).
 */
typedef struct SUPTAENTRY
{
    /** Pointer to the raw bytes. */
    const unsigned char    *pch;
    /** Number of bytes. */
    uint32_t                cb;
    /** Value in RTCRCERTCTX_F_ENC_MASK: RTCRCERTCTX_F_ENC_TAF_DER,
     *  RTCRCERTCTX_F_ENC_X509_DER. */
    uint32_t                fEnc;
} SUPTAENTRY;
/** Pointer to a trust anchor table entry. */
typedef SUPTAENTRY const *PCSUPTAENTRY;

/** Macro for a TAF entry. */
#define SUPTAENTRY_TAF(a_abTA)      { &a_abTA[0], sizeof(a_abTA), RTCRCERTCTX_F_ENC_TAF_DER }

/** Macro for a X.509 certificate entry. */
#define SUPTAENTRY_CER(a_abCertTA)  { &a_abCertTA[0], sizeof(a_abCertTA), RTCRCERTCTX_F_ENC_X509_DER }

/** All certificates we know. */
extern SUPTAENTRY const             g_aSUPAllTAs[];
/** Number of entries in g_aSUPAllTAs. */
extern unsigned const               g_cSUPAllTAs;

/** Software publisher certificate roots (Authenticode). */
extern SUPTAENTRY const             g_aSUPSpcRootTAs[];
/** Number of entries in g_aSUPSpcRootTAs. */
extern unsigned const               g_cSUPSpcRootTAs;

/** Kernel root certificates used by Windows. */
extern SUPTAENTRY const             g_aSUPNtKernelRootTAs[];
/** Number of entries in g_aSUPNtKernelRootTAs. */
extern unsigned const               g_cSUPNtKernelRootTAs;

/** Timestamp root certificates trusted by Windows. */
extern SUPTAENTRY const             g_aSUPTimestampTAs[];
/** Number of entries in g_aSUPTimestampTAs. */
extern unsigned const               g_cSUPTimestampTAs;

/** Root certificates trusted by Apple code signing. */
extern SUPTAENTRY const             g_aSUPAppleRootTAs[];
/** Number of entries in g_cSUPAppleRootTAs. */
extern unsigned const               g_cSUPAppleRootTAs;

/** TAs we put special trust in (the build certificate, Oracle VirtualBox). */
extern SUPTAENTRY const             g_aSUPTrustedTAs[];
/** Number of entries in g_aSUPTrustedTAs. */
extern unsigned const               g_cSUPTrustedTAs;

/** Supplemental certificates, like cross signing certificates. */
extern SUPTAENTRY const             g_aSUPSupplementalTAs[];
/** Number of entries in g_aSUPTrustedTAs. */
extern unsigned const               g_cSUPSupplementalTAs;

/** The build certificate. */
extern const unsigned char          g_abSUPBuildCert[];
/** The size of the build certificate. */
extern const unsigned               g_cbSUPBuildCert;

/** @} */


/** @} */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_sup_h */

