/* $Id: EMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_EMInternal_h
#define VMM_INCLUDED_SRC_include_EMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/stam.h>
#include <VBox/dis.h>
#include <VBox/vmm/pdmcritsect.h>
#include <iprt/avl.h>
#include <iprt/setjmp-without-sigmask.h>

RT_C_DECLS_BEGIN


/** @defgroup grp_em_int       Internal
 * @ingroup grp_em
 * @internal
 * @{
 */

/** The saved state version. */
#define EM_SAVED_STATE_VERSION                          5
#define EM_SAVED_STATE_VERSION_PRE_IEM                  4
#define EM_SAVED_STATE_VERSION_PRE_MWAIT                3
#define EM_SAVED_STATE_VERSION_PRE_SMP                  2


/** @name MWait state flags.
 * @{
 */
/** MWait activated. */
#define EMMWAIT_FLAG_ACTIVE             RT_BIT(0)
/** MWait will continue when an interrupt is pending even when IF=0. */
#define EMMWAIT_FLAG_BREAKIRQIF0        RT_BIT(1)
/** Monitor instruction was executed previously. */
#define EMMWAIT_FLAG_MONITOR_ACTIVE     RT_BIT(2)
/** @} */

/** EM time slice in ms; used for capping execution time. */
#define EM_TIME_SLICE                   100

/**
 * Cli node structure
 */
typedef struct CLISTAT
{
    /** The key is the cli address. */
    AVLGCPTRNODECORE        Core;
#if HC_ARCH_BITS == 32 && !defined(RT_OS_WINDOWS)
    /** Padding. */
    uint32_t                u32Padding;
#endif
    /** Occurrences. */
    STAMCOUNTER             Counter;
} CLISTAT, *PCLISTAT;
#ifdef IN_RING3
AssertCompileMemberAlignment(CLISTAT, Counter, 8);
#endif


/**
 * Exit history entry.
 *
 * @remarks We could perhaps trim this down a little bit by assuming uFlatPC
 *          only needs 48 bits (currently true but will change) and stuffing
 *          the flags+type in the available 16 bits made available.  The
 *          timestamp could likewise be shortened to accomodate the index, or
 *          we might skip the index entirely.  However, since we will have to
 *          deal with 56-bit wide PC address before long, there's not point.
 *
 *          On the upside, there are unused bits in both uFlagsAndType and the
 *          idxSlot fields if needed for anything.
 */
typedef struct EMEXITENTRY
{
    /** The flat PC (CS:EIP/RIP) address of the exit.
     * UINT64_MAX if not available.  */
    uint64_t        uFlatPC;
    /** The EMEXIT_MAKE_FLAGS_AND_TYPE */
    uint32_t        uFlagsAndType;
    /** The index into the exit slot hash table.
     * UINT32_MAX if too many collisions and not entered into it. */
    uint32_t        idxSlot;
    /** The TSC timestamp of the exit.
     * This is 0 if not timestamped. */
    uint64_t        uTimestamp;
} EMEXITENTRY;
/** Pointer to an exit history entry. */
typedef EMEXITENTRY *PEMEXITENTRY;
/** Pointer to a const exit history entry. */
typedef EMEXITENTRY const *PCEMEXITENTRY;


/**
 * EM VM Instance data.
 */
typedef struct EM
{
    /** Whether IEM executes everything. */
    bool                    fIemExecutesAll;
    /** Whether IEM execution (pure) is recompiled (true) or interpreted (false). */
    bool                    fIemRecompiled;
    /** Whether a triple fault triggers a guru. */
    bool                    fGuruOnTripleFault;
    /** Alignment padding. */
    bool                    afPadding[5];
} EM;
/** Pointer to EM VM instance data. */
typedef EM *PEM;


/**
 * EM VMCPU Instance data.
 */
typedef struct EMCPU
{
    /** Execution Manager State. */
    EMSTATE volatile        enmState;

    /** The state prior to the suspending of the VM. */
    EMSTATE                 enmPrevState;

    /** Set if hypercall instruction VMMCALL (AMD) & VMCALL (Intel) are enabled.
     * GIM sets this and the execution managers queries it.  Not saved, as GIM
     * takes care of that bit too.  */
    bool                    fHypercallEnabled;

    /** Explicit padding. */
    uint8_t                 abPadding0[7];

    /** Start of the current time slice in ms (RTTimeMilliTS). */
    uint64_t                msTimeSliceStart;
    /** The sum of the RTThreadGetExecutionTimeMilli() values at the start of the
     * current slice. */
    uint64_t                cMsTimeSliceStartExec;
    /** Number of milliseconds into the current time slice last we checked.
     * This is in terms of the RTThreadGetExecutionTimeMilli() total, like for
     * cMsTimeSliceStartExec. */
    uint64_t                cMsTimeSliceExec;

    /** Pending ring-3 I/O port access (VINF_EM_PENDING_R3_IOPORT_READ / VINF_EM_PENDING_R3_IOPORT_WRITE). */
    struct
    {
        RTIOPORT            uPort;          /**< The I/O port number.*/
        uint8_t             cbValue;        /**< The value size in bytes.  Zero when not pending. */
        uint8_t             cbInstr;        /**< The instruction length. */
        uint32_t            uValue;         /**< The value to write. */
    } PendingIoPortAccess;

    /** MWait halt state. */
    struct
    {
        uint32_t            fWait;          /**< Type of mwait; see EMMWAIT_FLAG_*. */
        uint32_t            u32Padding;
        RTGCPTR             uMWaitRAX;      /**< MWAIT hints. */
        RTGCPTR             uMWaitRCX;      /**< MWAIT extensions. */
        RTGCPTR             uMonitorRAX;    /**< Monitored address. */
        RTGCPTR             uMonitorRCX;    /**< Monitor extension. */
        RTGCPTR             uMonitorRDX;    /**< Monitor hint. */
    } MWait;

#if 0
    /** Make sure the jmp_buf is at a 32-byte boundrary. */
    uint64_t                au64Padding1[4];
#endif
    union
    {
        /** Padding used in the other rings.
         * This must be larger than jmp_buf on any supported platform. */
#if defined(RT_OS_LINUX) && defined(RT_ARCH_ARM64)
        char                achPaddingFatalLongJump[512];
#else
        char                achPaddingFatalLongJump[256];
#endif
#ifdef IN_RING3
        /** Long buffer jump for fatal VM errors.
         * It will jump to before the outer EM loop is entered. */
        jmp_buf             FatalLongJump;
#endif
    } u;

    /** For saving stack space, the disassembler state is allocated here instead of
     * on the stack. */
    DISSTATE                Dis;

    /** @name Execution profiling.
     * @{ */
    STAMPROFILE             StatForcedActions;
    STAMPROFILE             StatHalted;
    STAMPROFILEADV          StatCapped;
    STAMPROFILEADV          StatHMEntry;
    STAMPROFILE             StatHMExec;
    STAMPROFILE             StatIEMEmu;
    STAMPROFILE             StatIEMThenREM;
    STAMPROFILEADV          StatNEMEntry;
    STAMPROFILE             StatNEMExec;
    STAMPROFILE             StatREMExec;
    STAMPROFILE             StatREMTotal;
    STAMPROFILEADV          StatTotal;
    /** @} */

    /** R3: Profiling of emR3RawExecuteIOInstruction. */
    STAMPROFILE             StatIOEmu;
    STAMCOUNTER             StatIoRestarted;
    STAMCOUNTER             StatIoIem;
    /** R3: Profiling of emR3RawPrivileged. */
    STAMPROFILE             StatPrivEmu;
    /** R3: Number of times emR3HmExecute is called. */
    STAMCOUNTER             StatHMExecuteCalled;
    /** R3: Number of times emR3NEMExecute is called. */
    STAMCOUNTER             StatNEMExecuteCalled;

    /** Align the next member at a 32-byte boundrary. */
    uint64_t                au64Padding2[1+2];

    /** Exit history table (6KB). */
    EMEXITENTRY             aExitHistory[256];
    /** Where to store the next exit history entry.
     * Since aExitHistory is 256 items longs, we'll just increment this and
     * mask it when using it.  That help the readers detect whether we've
     * wrapped around or not.  */
    uint64_t                iNextExit;

    /** Index into aExitRecords set by EMHistoryExec when returning to ring-3.
     * This is UINT16_MAX if not armed.  */
    uint16_t volatile       idxContinueExitRec;
    /** Whether exit optimizations are enabled or not (in general). */
    bool                    fExitOptimizationEnabled : 1;
    /** Whether exit optimizations are enabled for ring-0 (in general). */
    bool                    fExitOptimizationEnabledR0 : 1;
    /** Whether exit optimizations are enabled for ring-0 when preemption is disabled. */
    bool                    fExitOptimizationEnabledR0PreemptDisabled : 1;
    /** Explicit padding. */
    bool                    fPadding2;
    /** Max number of instructions to execute. */
    uint16_t                cHistoryExecMaxInstructions;
    /** Min number of instructions to execute while probing. */
    uint16_t                cHistoryProbeMinInstructions;
    /** Max number of instructions to execute without an exit before giving up probe. */
    uint16_t                cHistoryProbeMaxInstructionsWithoutExit;
    uint16_t                uPadding3;
    /** Number of exit records in use. */
    uint32_t                cExitRecordUsed;
    /** Profiling the EMHistoryExec when executing (not probing). */
    STAMPROFILE             StatHistoryExec;
    /** Number of saved exits. */
    STAMCOUNTER             StatHistoryExecSavedExits;
    /** Number of instructions executed by EMHistoryExec. */
    STAMCOUNTER             StatHistoryExecInstructions;
    uint64_t                uPadding4;
    /** Number of instructions executed by EMHistoryExec when probing. */
    STAMCOUNTER             StatHistoryProbeInstructions;
    /** Number of times probing resulted in EMEXITACTION_NORMAL_PROBED. */
    STAMCOUNTER             StatHistoryProbedNormal;
    /** Number of times probing resulted in EMEXITACTION_EXEC_WITH_MAX. */
    STAMCOUNTER             StatHistoryProbedExecWithMax;
    /** Number of times probing resulted in ring-3 continuation. */
    STAMCOUNTER             StatHistoryProbedToRing3;
    /** Profiling the EMHistoryExec when probing.*/
    STAMPROFILE             StatHistoryProbe;
    /** Hit statistics for each lookup step. */
    STAMCOUNTER             aStatHistoryRecHits[16];
    /** Type change statistics for each lookup step. */
    STAMCOUNTER             aStatHistoryRecTypeChanged[16];
    /** Replacement statistics for each lookup step. */
    STAMCOUNTER             aStatHistoryRecReplaced[16];
    /** New record statistics for each lookup step. */
    STAMCOUNTER             aStatHistoryRecNew[16];

    /** Exit records (32KB). (Aligned on 32 byte boundrary.) */
    EMEXITREC               aExitRecords[1024];
} EMCPU;
/** Pointer to EM VM instance data. */
typedef EMCPU *PEMCPU;

/** @} */

int             emR3InitDbg(PVM pVM);

int             emR3HmExecute(PVM pVM, PVMCPU pVCpu, bool *pfFFDone);
VBOXSTRICTRC    emR3NemExecute(PVM pVM, PVMCPU pVCpu, bool *pfFFDone);
int             emR3RawExecute(PVM pVM, PVMCPU pVCpu, bool *pfFFDone);

EMSTATE         emR3Reschedule(PVM pVM, PVMCPU pVCpu);
int             emR3ForcedActions(PVM pVM, PVMCPU pVCpu, int rc);
VBOXSTRICTRC    emR3HighPriorityPostForcedActions(PVM pVM, PVMCPU pVCpu, VBOXSTRICTRC rc);

int             emR3RawResumeHyper(PVM pVM, PVMCPU pVCpu);
int             emR3RawStep(PVM pVM, PVMCPU pVCpu);

VBOXSTRICTRC    emR3NemSingleInstruction(PVM pVM, PVMCPU pVCpu, uint32_t fFlags);

bool            emR3IsExecutionAllowedSlow(PVM pVM, PVMCPU pVCpu);

VBOXSTRICTRC    emR3ExecutePendingIoPortWrite(PVM pVM, PVMCPU pVCpu);
VBOXSTRICTRC    emR3ExecutePendingIoPortRead(PVM pVM, PVMCPU pVCpu);
VBOXSTRICTRC    emR3ExecuteSplitLockInstruction(PVM pVM, PVMCPU pVCpu);

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_EMInternal_h */

