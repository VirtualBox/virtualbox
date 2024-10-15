/* $Id: VMMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_VMMInternal_h
#define VMM_INCLUDED_SRC_include_VMMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/sup.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/vmm.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/critsect.h>

#if !defined(IN_VMM_R3) && !defined(IN_VMM_R0) && !defined(IN_VMM_RC)
# error "Not in VMM! This is an internal header!"
#endif
#if HC_ARCH_BITS == 32
# error "32-bit hosts are no longer supported. Go back to 6.0 or earlier!"
#endif



/** @defgroup grp_vmm_int   Internals
 * @ingroup grp_vmm
 * @internal
 * @{
 */

/** @def VBOX_WITH_RC_RELEASE_LOGGING
 * Enables RC release logging. */
#define VBOX_WITH_RC_RELEASE_LOGGING

/** @def VBOX_WITH_R0_LOGGING
 * Enables Ring-0 logging (non-release).
 *
 * Ring-0 logging isn't 100% safe yet (thread id reuse / process exit cleanup),
 * so you have to sign up here by adding your defined(DEBUG_<userid>) to the
 * \#if, or by adding VBOX_WITH_R0_LOGGING to your LocalConfig.kmk.
 */
#if defined(DEBUG_sandervl) || defined(DEBUG_frank) || defined(DEBUG_ramshankar) || defined(DOXYGEN_RUNNING)
# define VBOX_WITH_R0_LOGGING
#endif

/** @def VBOX_STRICT_VMM_STACK
 * Enables VMM stack guard pages to catch stack over- and underruns. */
#if defined(VBOX_STRICT) || defined(DOXYGEN_RUNNING)
# define VBOX_STRICT_VMM_STACK
#endif


/** Number of buffers per logger. */
#define VMMLOGGER_BUFFER_COUNT  4

/**
 * R0 logger data (ring-0 only data).
 */
typedef struct VMMR0PERVCPULOGGER
{
    /** Pointer to the logger instance.
     * The RTLOGGER::u32UserValue1 member is used for flags and magic, while the
     * RTLOGGER::u64UserValue2 member is the corresponding PGVMCPU value.
     * RTLOGGER::u64UserValue3 is currently and set to the PGVMCPU value too. */
    R0PTRTYPE(PRTLOGGER)    pLogger;
    /** Log buffer descriptor.
     * The buffer is allocated in a common block for all VCpus, see VMMR0PERVM.  */
    RTLOGBUFFERDESC         aBufDescs[VMMLOGGER_BUFFER_COUNT];
    /** Flag indicating whether we've registered the instance already. */
    bool                    fRegistered;
    /** Set if the EMT is waiting on hEventFlushWait. */
    bool                    fEmtWaiting;
    /** Set while we're inside vmmR0LoggerFlushCommon to prevent recursion. */
    bool                    fFlushing;
    /** Flush to parent VMM's debug log instead of ring-3. */
    bool                    fFlushToParentVmmDbg : 1;
    /** Flush to parent VMM's debug log instead of ring-3. */
    bool                    fFlushToParentVmmRel : 1;
    /** Number of buffers currently queued for flushing. */
    uint32_t volatile       cFlushing;
    /** The event semaphore the EMT waits on while the buffer is being flushed. */
    RTSEMEVENT              hEventFlushWait;
} VMMR0PERVCPULOGGER;
/** Pointer to the R0 logger data (ring-0 only). */
typedef VMMR0PERVCPULOGGER *PVMMR0PERVCPULOGGER;


/**
 * R0 logger data shared with ring-3 (per CPU).
 */
typedef struct VMMR3CPULOGGER
{
    /** Buffer info. */
    struct
    {
        /** Auxiliary buffer descriptor. */
        RTLOGBUFFERAUXDESC      AuxDesc;
        /** Ring-3 mapping of the logging buffer. */
        R3PTRTYPE(char *)       pchBufR3;
    } aBufs[VMMLOGGER_BUFFER_COUNT];
    /** The current buffer. */
    uint32_t                idxBuf;
    /** Number of buffers currently queued for flushing (copy of
     *  VMMR0PERVCPULOGGER::cFlushing). */
    uint32_t volatile       cFlushing;
    /** The buffer size. */
    uint32_t                cbBuf;
    /** Number of bytes dropped because the flush context didn't allow waiting.  */
    uint32_t                cbDropped;
    STAMCOUNTER             StatFlushes;
    STAMCOUNTER             StatCannotBlock;
    STAMPROFILE             StatWait;
    STAMPROFILE             StatRaces;
    STAMCOUNTER             StatRacesToR0;
} VMMR3CPULOGGER;
/** Pointer to r0 logger data shared with ring-3. */
typedef VMMR3CPULOGGER *PVMMR3CPULOGGER;

/** @name Logger indexes for VMMR0PERVCPU::u.aLoggers and VMMCPU::u.aLoggers.
 * @{ */
#define VMMLOGGER_IDX_REGULAR   0
#define VMMLOGGER_IDX_RELEASE   1
#define VMMLOGGER_IDX_MAX       2
/** @} */


/** Pointer to a ring-0 jump buffer. */
typedef struct VMMR0JMPBUF *PVMMR0JMPBUF;
/**
 * Jump buffer for the setjmp/longjmp like constructs used to
 * quickly 'call' back into Ring-3.
 */
typedef struct VMMR0JMPBUF
{
    /** Traditional jmp_buf stuff
     * @{ */
#if HC_ARCH_BITS == 32
    uint32_t                    ebx;
    uint32_t                    esi;
    uint32_t                    edi;
    uint32_t                    ebp;
    uint32_t                    esp;
    uint32_t                    eip;
    uint32_t                    eflags;
#endif
#if HC_ARCH_BITS == 64
    uint64_t                    rbx;
# ifdef RT_OS_WINDOWS
    uint64_t                    rsi;
    uint64_t                    rdi;
# endif
    uint64_t                    rbp;
    uint64_t                    r12;
    uint64_t                    r13;
    uint64_t                    r14;
    uint64_t                    r15;
    uint64_t                    rsp;
    uint64_t                    rip;
# ifdef RT_OS_WINDOWS
    uint128_t                   xmm6;
    uint128_t                   xmm7;
    uint128_t                   xmm8;
    uint128_t                   xmm9;
    uint128_t                   xmm10;
    uint128_t                   xmm11;
    uint128_t                   xmm12;
    uint128_t                   xmm13;
    uint128_t                   xmm14;
    uint128_t                   xmm15;
# endif
    uint64_t                    rflags;
#endif
    /** @} */

    /** RSP/ESP at the time of the stack mirroring (what pvStackBuf starts with). */
    RTHCUINTREG                 UnwindSp;
    /** RSP/ESP at the time of the long jump call. */
    RTHCUINTREG                 UnwindRetSp;
    /** RBP/EBP inside the vmmR0CallRing3LongJmp frame. */
    RTHCUINTREG                 UnwindBp;
    /** RIP/EIP within vmmR0CallRing3LongJmp for assisting unwinding. */
    RTHCUINTREG                 UnwindPc;
    /** Unwind: The vmmR0CallRing3SetJmp return address value. */
    RTHCUINTREG                 UnwindRetPcValue;
    /** Unwind: The vmmR0CallRing3SetJmp return address stack location. */
    RTHCUINTREG                 UnwindRetPcLocation;

    /** The function last being executed here. */
    RTHCUINTREG                 pfn;
    /** The first argument to the function. */
    RTHCUINTREG                 pvUser1;
    /** The second argument to the function. */
    RTHCUINTREG                 pvUser2;

    /** Number of valid bytes in pvStackBuf.  */
    uint32_t                    cbStackValid;
    /** Size of buffer pvStackBuf points to. */
    uint32_t                    cbStackBuf;
    /** Pointer to buffer for mirroring the stack. Optional. */
    RTR0PTR                     pvStackBuf;
    /** Pointer to a ring-3 accessible jump buffer structure for automatic
     *  mirroring on longjmp. Optional. */
    R0PTRTYPE(PVMMR0JMPBUF)     pMirrorBuf;
} VMMR0JMPBUF;


/**
 * Log flusher job.
 *
 * There is a ring buffer of these in ring-0 (VMMR0PERVM::aLogFlushRing) and a
 * copy of the current one in the shared VM structure (VMM::LogFlusherItem).
 */
typedef union VMMLOGFLUSHERENTRY
{
    struct
    {
        /** The virtual CPU ID. */
        uint32_t            idCpu : 16;
        /** The logger: 0 for release, 1 for debug. */
        uint32_t            idxLogger : 8;
        /** The buffer to be flushed. */
        uint32_t            idxBuffer : 7;
        /** Set by the flusher thread once it fetched the entry and started
         *  processing it. */
        uint32_t            fProcessing : 1;
    } s;
    uint32_t                u32;
} VMMLOGFLUSHERENTRY;


/**
 * VMM Data (part of VM)
 */
typedef struct VMM
{
    /** Whether we should use the periodic preemption timers. */
    bool                        fUsePeriodicPreemptionTimers;
    /** Alignment padding. */
    bool                        afPadding0[7];

#if 0 /* pointless when timers doesn't run on EMT */
    /** The EMT yield timer. */
    TMTIMERHANDLE               hYieldTimer;
    /** The period to the next timeout when suspended or stopped.
     * This is 0 when running. */
    uint32_t                    cYieldResumeMillies;
    /** The EMT yield timer interval (milliseconds). */
    uint32_t                    cYieldEveryMillies;
    /** The timestamp of the previous yield. (nano) */
    uint64_t                    u64LastYield;
#endif

    /** @name EMT Rendezvous
     * @{ */
    /** Semaphore to wait on upon entering ordered execution. */
    R3PTRTYPE(PRTSEMEVENT)      pahEvtRendezvousEnterOrdered;
    /** Semaphore to wait on upon entering for one-by-one execution. */
    RTSEMEVENT                  hEvtRendezvousEnterOneByOne;
    /** Semaphore to wait on upon entering for all-at-once execution. */
    RTSEMEVENTMULTI             hEvtMulRendezvousEnterAllAtOnce;
    /** Semaphore to wait on when done. */
    RTSEMEVENTMULTI             hEvtMulRendezvousDone;
    /** Semaphore the VMMR3EmtRendezvous caller waits on at the end. */
    RTSEMEVENT                  hEvtRendezvousDoneCaller;
    /** Semaphore to wait on upon recursing. */
    RTSEMEVENTMULTI             hEvtMulRendezvousRecursionPush;
    /** Semaphore to wait on after done with recursion (caller restoring state). */
    RTSEMEVENTMULTI             hEvtMulRendezvousRecursionPop;
    /** Semaphore the initiator waits on while the EMTs are getting into position
     *  on hEvtMulRendezvousRecursionPush. */
    RTSEMEVENT                  hEvtRendezvousRecursionPushCaller;
    /** Semaphore the initiator waits on while the EMTs sitting on
     *  hEvtMulRendezvousRecursionPop wakes up and leave. */
    RTSEMEVENT                  hEvtRendezvousRecursionPopCaller;
    /** Callback. */
    R3PTRTYPE(PFNVMMEMTRENDEZVOUS) volatile pfnRendezvous;
    /** The user argument for the callback. */
    RTR3PTR volatile            pvRendezvousUser;
    /** Flags. */
    volatile uint32_t           fRendezvousFlags;
    /** The number of EMTs that has entered. */
    volatile uint32_t           cRendezvousEmtsEntered;
    /** The number of EMTs that has done their job. */
    volatile uint32_t           cRendezvousEmtsDone;
    /** The number of EMTs that has returned. */
    volatile uint32_t           cRendezvousEmtsReturned;
    /** The status code. */
    volatile int32_t            i32RendezvousStatus;
    /** Spin lock. */
    volatile uint32_t           u32RendezvousLock;
    /** The recursion depth. */
    volatile uint32_t           cRendezvousRecursions;
    /** The number of EMTs that have entered the recursion routine. */
    volatile uint32_t           cRendezvousEmtsRecursingPush;
    /** The number of EMTs that have leaft the recursion routine. */
    volatile uint32_t           cRendezvousEmtsRecursingPop;
    /** Triggers rendezvous recursion in the other threads. */
    volatile bool               fRendezvousRecursion;

    /** @} */

    /** RTThreadPreemptIsPendingTrusty() result, set by vmmR0InitVM() for
     * release logging purposes. */
    bool                        fIsPreemptPendingApiTrusty : 1;
    /** The RTThreadPreemptIsPossible() result,  set by vmmR0InitVM() for
     * release logging purposes.  */
    bool                        fIsPreemptPossible : 1;
    /** Set if ring-0 uses context hooks.  */
    bool                        fIsUsingContextHooks : 1;

    bool                        afAlignment2[2]; /**< Alignment padding. */

    /** Buffer for storing the standard assertion message for a ring-0 assertion.
     * Used for saving the assertion message text for the release log and guru
     * meditation dump. */
    char                        szRing0AssertMsg1[512];
    /** Buffer for storing the custom message for a ring-0 assertion. */
    char                        szRing0AssertMsg2[256];

    /** @name Logging
     * @{ */
    /** Used when setting up ring-0 logger. */
    uint64_t                    nsProgramStart;
    /** Log flusher thread. */
    RTTHREAD                    hLogFlusherThread;
    /** Copy of the current work log flusher work item. */
    VMMLOGFLUSHERENTRY volatile LogFlusherItem;
    STAMCOUNTER                 StatLogFlusherFlushes;
    STAMCOUNTER                 StatLogFlusherNoWakeUp;
    /** @} */

    /** Number of VMMR0_DO_HM_RUN or VMMR0_DO_NEM_RUN calls. */
    STAMCOUNTER                 StatRunGC;

    /** Statistics for each of the RC/R0 return codes.
     * @{ */
    STAMCOUNTER                 StatRZRetNormal;
    STAMCOUNTER                 StatRZRetInterrupt;
    STAMCOUNTER                 StatRZRetInterruptHyper;
    STAMCOUNTER                 StatRZRetGuestTrap;
    STAMCOUNTER                 StatRZRetRingSwitch;
    STAMCOUNTER                 StatRZRetRingSwitchInt;
    STAMCOUNTER                 StatRZRetStaleSelector;
    STAMCOUNTER                 StatRZRetIRETTrap;
    STAMCOUNTER                 StatRZRetEmulate;
    STAMCOUNTER                 StatRZRetPatchEmulate;
    STAMCOUNTER                 StatRZRetIORead;
    STAMCOUNTER                 StatRZRetIOWrite;
    STAMCOUNTER                 StatRZRetIOCommitWrite;
    STAMCOUNTER                 StatRZRetMMIORead;
    STAMCOUNTER                 StatRZRetMMIOWrite;
    STAMCOUNTER                 StatRZRetMMIOCommitWrite;
    STAMCOUNTER                 StatRZRetMMIOPatchRead;
    STAMCOUNTER                 StatRZRetMMIOPatchWrite;
    STAMCOUNTER                 StatRZRetMMIOReadWrite;
    STAMCOUNTER                 StatRZRetMSRRead;
    STAMCOUNTER                 StatRZRetMSRWrite;
    STAMCOUNTER                 StatRZRetLDTFault;
    STAMCOUNTER                 StatRZRetGDTFault;
    STAMCOUNTER                 StatRZRetIDTFault;
    STAMCOUNTER                 StatRZRetTSSFault;
    STAMCOUNTER                 StatRZRetCSAMTask;
    STAMCOUNTER                 StatRZRetSyncCR3;
    STAMCOUNTER                 StatRZRetMisc;
    STAMCOUNTER                 StatRZRetPatchInt3;
    STAMCOUNTER                 StatRZRetPatchPF;
    STAMCOUNTER                 StatRZRetPatchGP;
    STAMCOUNTER                 StatRZRetPatchIretIRQ;
    STAMCOUNTER                 StatRZRetRescheduleREM;
    STAMCOUNTER                 StatRZRetToR3Total;
    STAMCOUNTER                 StatRZRetToR3FF;
    STAMCOUNTER                 StatRZRetToR3Unknown;
    STAMCOUNTER                 StatRZRetToR3TMVirt;
    STAMCOUNTER                 StatRZRetToR3HandyPages;
    STAMCOUNTER                 StatRZRetToR3PDMQueues;
    STAMCOUNTER                 StatRZRetToR3Rendezvous;
    STAMCOUNTER                 StatRZRetToR3Timer;
    STAMCOUNTER                 StatRZRetToR3DMA;
    STAMCOUNTER                 StatRZRetToR3CritSect;
    STAMCOUNTER                 StatRZRetToR3Iem;
    STAMCOUNTER                 StatRZRetToR3Iom;
    STAMCOUNTER                 StatRZRetTimerPending;
    STAMCOUNTER                 StatRZRetInterruptPending;
    STAMCOUNTER                 StatRZRetPATMDuplicateFn;
    STAMCOUNTER                 StatRZRetPendingRequest;
    STAMCOUNTER                 StatRZRetPGMFlushPending;
    STAMCOUNTER                 StatRZRetPatchTPR;
    /** @} */
} VMM;
/** Pointer to VMM. */
typedef VMM *PVMM;


/**
 * VMMCPU Data (part of VMCPU)
 */
typedef struct VMMCPU
{
    /** The last RC/R0 return code. */
    int32_t                     iLastGZRc;
    /** Alignment padding. */
    uint32_t                    u32Padding0;

    /** @name Rendezvous
     * @{ */
    /** Whether the EMT is executing a rendezvous right now. For detecting
     *  attempts at recursive rendezvous. */
    bool volatile               fInRendezvous;
    bool                        afPadding1[2];
    /** @} */

    /** Whether we can HLT in VMMR0 rather than having to return to EM.
     * Updated by vmR3SetHaltMethodU(). */
    bool                        fMayHaltInRing0;
    /** The minimum delta for which we can HLT in ring-0 for.
     * The deadlines we can calculate are  from TM, so, if it's too close
     * we should just return to ring-3 and run the timer wheel, no point
     * in spinning in ring-0.
     * Updated by vmR3SetHaltMethodU(). */
    uint32_t                    cNsSpinBlockThreshold;
    /** Number of ring-0 halts (used for depreciating following values). */
    uint32_t                    cR0Halts;
    /** Number of ring-0 halts succeeding (VINF_SUCCESS) recently. */
    uint32_t                    cR0HaltsSucceeded;
    /** Number of ring-0 halts failing (VINF_EM_HALT) recently. */
    uint32_t                    cR0HaltsToRing3;
    /** Padding   */
    uint32_t                    u32Padding2;

    /** @name Raw-mode context tracing data.
     * @{ */
    SUPDRVTRACERUSRCTX          TracerCtx;
    /** @} */

    /** @name Ring-0 assertion info for this EMT.
     * @{ */
    /** Copy of the ring-0 jmp buffer after an assertion. */
    VMMR0JMPBUF                 AssertJmpBuf;
    /** Copy of the assertion stack. */
    uint8_t                     abAssertStack[8192];
    /** @} */

    /**
     * Loggers.
     */
    union
    {
        struct
        {
            /** The R0 logger data shared with ring-3. */
            VMMR3CPULOGGER      Logger;
            /** The R0 release logger data shared with ring-3. */
            VMMR3CPULOGGER      RelLogger;
        } s;
        /** Array view. */
        VMMR3CPULOGGER          aLoggers[VMMLOGGER_IDX_MAX];
    } u;

    STAMPROFILE                 StatR0HaltBlock;
    STAMPROFILE                 StatR0HaltBlockOnTime;
    STAMPROFILE                 StatR0HaltBlockOverslept;
    STAMPROFILE                 StatR0HaltBlockInsomnia;
    STAMCOUNTER                 StatR0HaltExec;
    STAMCOUNTER                 StatR0HaltExecFromBlock;
    STAMCOUNTER                 StatR0HaltExecFromSpin;
    STAMCOUNTER                 StatR0HaltToR3;
    STAMCOUNTER                 StatR0HaltToR3FromSpin;
    STAMCOUNTER                 StatR0HaltToR3Other;
    STAMCOUNTER                 StatR0HaltToR3PendingFF;
    STAMCOUNTER                 StatR0HaltToR3SmallDelta;
    STAMCOUNTER                 StatR0HaltToR3PostNoInt;
    STAMCOUNTER                 StatR0HaltToR3PostPendingFF;
} VMMCPU;
AssertCompileMemberAlignment(VMMCPU, TracerCtx, 8);
AssertCompile(   RTASSERT_OFFSET_OF(VMMCPU, u.s.Logger)
              == RTASSERT_OFFSET_OF(VMMCPU, u.aLoggers) + sizeof(VMMR3CPULOGGER) * VMMLOGGER_IDX_REGULAR);
AssertCompile(RTASSERT_OFFSET_OF(VMMCPU, u.s.RelLogger)
              == RTASSERT_OFFSET_OF(VMMCPU, u.aLoggers) + sizeof(VMMR3CPULOGGER) * VMMLOGGER_IDX_RELEASE);

/** Pointer to VMMCPU. */
typedef VMMCPU *PVMMCPU;

/**
 * VMM per-VCpu ring-0 only instance data.
 */
typedef struct VMMR0PERVCPU
{
    /** The EMT hash table index. */
    uint16_t                            idxEmtHash;
    /** Flag indicating whether we've disabled flushing (world switch) or not. */
    bool                                fLogFlushingDisabled;
    bool                                afPadding1[5];
    /** Pointer to the VMMR0EntryFast preemption state structure.
     * This is used to temporarily restore preemption before blocking.  */
    R0PTRTYPE(PRTTHREADPREEMPTSTATE)    pPreemptState;
    /** Thread context switching hook (ring-0). */
    RTTHREADCTXHOOK                     hCtxHook;

    /** @name Arguments passed by VMMR0EntryEx via vmmR0CallRing3SetJmpEx.
     * @note Cannot be put on the stack as the location may change and upset the
     *       validation of resume-after-ring-3-call logic.
     * @todo This no longer needs to be here now that we don't call ring-3 and mess
     *       around with stack restoring/switching.
     * @{ */
    PGVM                                pGVM;
    VMCPUID                             idCpu;
    VMMR0OPERATION                      enmOperation;
    PSUPVMMR0REQHDR                     pReq;
    uint64_t                            u64Arg;
    PSUPDRVSESSION                      pSession;
    /** @} */

    /** @name Ring-0 setjmp / assertion handling.
     * @{ */
    /** The ring-0 setjmp buffer. */
    VMMR0JMPBUF                         AssertJmpBuf;
    /** The disable counter. */
    uint32_t                            cCallRing3Disabled;
    uint32_t                            u32Padding3;
    /** Ring-0 assertion notification callback. */
    R0PTRTYPE(PFNVMMR0ASSERTIONNOTIFICATION) pfnAssertCallback;
    /** Argument for pfnRing0AssertionNotificationCallback. */
    R0PTRTYPE(void *)                   pvAssertCallbackUser;
    /** @} */

    /**
     * Loggers
     */
    union
    {
        struct
        {
            /** The R0 logger data. */
            VMMR0PERVCPULOGGER          Logger;
            /** The R0 release logger data. */
            VMMR0PERVCPULOGGER          RelLogger;
        } s;
        /** Array view. */
        VMMR0PERVCPULOGGER              aLoggers[VMMLOGGER_IDX_MAX];
    } u;
} VMMR0PERVCPU;
AssertCompile(   RTASSERT_OFFSET_OF(VMMR0PERVCPU, u.s.Logger)
              == RTASSERT_OFFSET_OF(VMMR0PERVCPU, u.aLoggers) + sizeof(VMMR0PERVCPULOGGER) * VMMLOGGER_IDX_REGULAR);
AssertCompile(RTASSERT_OFFSET_OF(VMMR0PERVCPU, u.s.RelLogger)
              == RTASSERT_OFFSET_OF(VMMR0PERVCPU, u.aLoggers) + sizeof(VMMR0PERVCPULOGGER) * VMMLOGGER_IDX_RELEASE);
AssertCompileMemberAlignment(VMMR0PERVCPU, AssertJmpBuf, 64);
/** Pointer to VMM ring-0 VMCPU instance data. */
typedef VMMR0PERVCPU *PVMMR0PERVCPU;

/** @name RTLOGGER::u32UserValue1 Flags
 * @{ */
/** The magic value. */
#define VMMR0_LOGGER_FLAGS_MAGIC_VALUE          UINT32_C(0x7d297f05)
/** Part of the flags value used for the magic. */
#define VMMR0_LOGGER_FLAGS_MAGIC_MASK           UINT32_C(0xffffff0f)
/** @} */


/**
 * VMM data kept in the ring-0 GVM.
 */
typedef struct VMMR0PERVM
{
    /** Set if vmmR0InitVM has been called. */
    bool                    fCalledInitVm;
    bool                    afPadding1[7];

    /** @name Logging
     *  @{ */
    /** Logger (debug) buffer allocation.
     * This covers all CPUs.  */
    RTR0MEMOBJ              hMemObjLogger;
    /** The ring-3 mapping object for hMemObjLogger. */
    RTR0MEMOBJ              hMapObjLogger;

    /** Release logger buffer allocation.
     * This covers all CPUs.  */
    RTR0MEMOBJ              hMemObjReleaseLogger;
    /** The ring-3 mapping object for hMemObjReleaseLogger. */
    RTR0MEMOBJ              hMapObjReleaseLogger;

    struct
    {
        /** Spinlock protecting the logger ring buffer and associated variables. */
        R0PTRTYPE(RTSPINLOCK)   hSpinlock;
        /** The log flusher thread handle to make sure there is only one. */
        RTNATIVETHREAD          hThread;
        /** The handle to the event semaphore the log flusher waits on. */
        RTSEMEVENT              hEvent;
        /** The index of the log flusher queue head (flusher thread side). */
        uint32_t volatile       idxRingHead;
        /** The index of the log flusher queue tail (EMT side). */
        uint32_t volatile       idxRingTail;
        /** Set if the log flusher thread is waiting for work and needs poking. */
        bool volatile           fThreadWaiting;
        /** Set when the log flusher thread should shut down. */
        bool volatile           fThreadShutdown;
        /** Indicates that the log flusher thread is running. */
        bool volatile           fThreadRunning;
        bool                    afPadding2[5];
        STAMCOUNTER             StatFlushes;
        STAMCOUNTER             StatNoWakeUp;
        /** Logger ring buffer.
         * This is for communicating with the log flusher thread.  */
        VMMLOGFLUSHERENTRY      aRing[VMM_MAX_CPU_COUNT * 2 /*loggers*/ * 1 /*buffer*/ + 16 /*fudge*/];
    } LogFlusher;
    /** @} */
} VMMR0PERVM;

RT_C_DECLS_BEGIN

int  vmmInitFormatTypes(void);
void vmmTermFormatTypes(void);
uint32_t vmmGetBuildType(void);

#ifdef IN_RING3
int  vmmR3SwitcherInit(PVM pVM);
void vmmR3SwitcherRelocate(PVM pVM, RTGCINTPTR offDelta);
#endif /* IN_RING3 */

#ifdef IN_RING0

/**
 * World switcher assembly routine.
 * It will call VMMRCEntry().
 *
 * @returns return code from VMMRCEntry().
 * @param   pVM         The cross context VM structure.
 * @param   uArg        See VMMRCEntry().
 * @internal
 */
DECLASM(int)    vmmR0WorldSwitch(PVM pVM, unsigned uArg);

/**
 * Callback function for vmmR0CallRing3SetJmp.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
typedef DECLCALLBACKTYPE(int, FNVMMR0SETJMP,(PVMCC pVM, PVMCPUCC pVCpu));
/** Pointer to FNVMMR0SETJMP(). */
typedef FNVMMR0SETJMP *PFNVMMR0SETJMP;

/**
 * The setjmp variant used for calling Ring-3.
 *
 * This differs from the normal setjmp in that it will resume VMMRZCallRing3 if we're
 * in the middle of a ring-3 call. Another differences is the function pointer and
 * argument. This has to do with resuming code and the stack frame of the caller.
 *
 * @returns VINF_SUCCESS on success or whatever is passed to vmmR0CallRing3LongJmp.
 * @param   pJmpBuf     The jmp_buf to set.
 * @param   pfn         The function to be called when not resuming.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
DECLASM(int)    vmmR0CallRing3SetJmp(PVMMR0JMPBUF pJmpBuf, PFNVMMR0SETJMP pfn, PVM pVM, PVMCPU pVCpu);


/**
 * Callback function for vmmR0CallRing3SetJmp2.
 *
 * @returns VBox status code.
 * @param   pGVM        The ring-0 VM structure.
 * @param   idCpu       The ID of the calling EMT.
 */
typedef DECLCALLBACKTYPE(int, FNVMMR0SETJMP2,(PGVM pGVM, VMCPUID idCpu));
/** Pointer to FNVMMR0SETJMP2(). */
typedef FNVMMR0SETJMP2 *PFNVMMR0SETJMP2;

/**
 * Same as vmmR0CallRing3SetJmp except for the function signature.
 *
 * @returns VINF_SUCCESS on success or whatever is passed to vmmR0CallRing3LongJmp.
 * @param   pJmpBuf     The jmp_buf to set.
 * @param   pfn         The function to be called when not resuming.
 * @param   pGVM        The ring-0 VM structure.
 * @param   idCpu       The ID of the calling EMT.
 */
DECLASM(int)    vmmR0CallRing3SetJmp2(PVMMR0JMPBUF pJmpBuf, PFNVMMR0SETJMP2 pfn, PGVM pGVM, VMCPUID idCpu);


/**
 * Callback function for vmmR0CallRing3SetJmpEx.
 *
 * @returns VBox status code.
 * @param   pvUser      The user argument.
 */
typedef DECLCALLBACKTYPE(int, FNVMMR0SETJMPEX,(void *pvUser));
/** Pointer to FNVMMR0SETJMPEX(). */
typedef FNVMMR0SETJMPEX *PFNVMMR0SETJMPEX;

/**
 * Same as vmmR0CallRing3SetJmp except for the function signature.
 *
 * @returns VINF_SUCCESS on success or whatever is passed to vmmR0CallRing3LongJmp.
 * @param   pJmpBuf     The jmp_buf to set.
 * @param   pfn         The function to be called when not resuming.
 * @param   pvUser      The argument of that function.
 * @param   uCallKey    Unused call parameter that should be used to help
 *                      uniquely identify the call.
 */
DECLASM(int)    vmmR0CallRing3SetJmpEx(PVMMR0JMPBUF pJmpBuf, PFNVMMR0SETJMPEX pfn, void *pvUser, uintptr_t uCallKey);


/**
 * Worker for VMMRZCallRing3.
 * This will save the stack and registers.
 *
 * @returns rc.
 * @param   pJmpBuf     Pointer to the jump buffer.
 * @param   rc          The return code.
 */
DECLASM(int)    vmmR0CallRing3LongJmp(PVMMR0JMPBUF pJmpBuf, int rc);

# ifdef VBOX_WITH_TRIPLE_FAULT_HACK
int  vmmR0TripleFaultHackInit(void);
void vmmR0TripleFaultHackTerm(void);
# endif

#endif /* IN_RING0 */

RT_C_DECLS_END

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_VMMInternal_h */
