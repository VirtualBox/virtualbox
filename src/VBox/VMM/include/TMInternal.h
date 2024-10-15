/* $Id: TMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_TMInternal_h
#define VMM_INCLUDED_SRC_include_TMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <iprt/time.h>
#include <iprt/timer.h>
#include <iprt/assert.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmcritsectrw.h>

RT_C_DECLS_BEGIN


/** @defgroup grp_tm_int       Internal
 * @ingroup grp_tm
 * @internal
 * @{
 */

/** Frequency of the real clock. */
#define TMCLOCK_FREQ_REAL       UINT32_C(1000)
/** Frequency of the virtual clock. */
#define TMCLOCK_FREQ_VIRTUAL    UINT32_C(1000000000)

/** @def TM_SECONDS_TO_AUTOMATIC_POWER_OFF
 * Powers off the VM automatically after the defined number of seconds.
 * This is for debugging only. */
#if defined(DOXYGEN_RUNNING) || 0
# define TM_SECONDS_TO_AUTOMATIC_POWER_OFF 45
#endif

/**
 * Timer type.
 */
typedef enum TMTIMERTYPE
{
    /** Invalid zero value. */
    TMTIMERTYPE_INVALID = 0,
    /** Device timer. */
    TMTIMERTYPE_DEV,
    /** USB device timer. */
    TMTIMERTYPE_USB,
    /** Driver timer. */
    TMTIMERTYPE_DRV,
    /** Internal timer . */
    TMTIMERTYPE_INTERNAL
} TMTIMERTYPE;

/**
 * Timer state
 */
typedef enum TMTIMERSTATE
{
    /** Invalid zero entry (used for table entry zero). */
    TMTIMERSTATE_INVALID = 0,
    /** Timer is stopped. */
    TMTIMERSTATE_STOPPED,
    /** Timer is active. */
    TMTIMERSTATE_ACTIVE,
    /** Timer is expired, getting expire and unlinking. */
    TMTIMERSTATE_EXPIRED_GET_UNLINK,
    /** Timer is expired and is being delivered. */
    TMTIMERSTATE_EXPIRED_DELIVER,

    /** Timer is stopped but still in the active list.
     * Currently in the ScheduleTimers list. */
    TMTIMERSTATE_PENDING_STOP,
    /** Timer is stopped but needs unlinking from the ScheduleTimers list.
     * Currently in the ScheduleTimers list. */
    TMTIMERSTATE_PENDING_STOP_SCHEDULE,
    /** Timer is being modified and will soon be pending scheduling.
     * Currently in the ScheduleTimers list. */
    TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE,
    /** Timer is pending scheduling.
     * Currently in the ScheduleTimers list. */
    TMTIMERSTATE_PENDING_SCHEDULE,
    /** Timer is being modified and will soon be pending rescheduling.
     * Currently in the ScheduleTimers list and the active list. */
    TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE,
    /** Timer is modified and is now pending rescheduling.
     * Currently in the ScheduleTimers list and the active list. */
    TMTIMERSTATE_PENDING_RESCHEDULE,
    /** Timer is being destroyed. */
    TMTIMERSTATE_DESTROY,
    /** Timer is free. */
    TMTIMERSTATE_FREE
} TMTIMERSTATE;

/** Predicate that returns true if the give state is pending scheduling or
 *  rescheduling of any kind. Will reference the argument more than once! */
#define TMTIMERSTATE_IS_PENDING_SCHEDULING(enmState) \
    (   (enmState) <= TMTIMERSTATE_PENDING_RESCHEDULE \
     && (enmState) >= TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE)

/** @name Timer handle value elements
 * @{ */
#define TMTIMERHANDLE_RANDOM_MASK       UINT64_C(0xffffffffff000000)
#define TMTIMERHANDLE_QUEUE_IDX_SHIFT   16
#define TMTIMERHANDLE_QUEUE_IDX_MASK    UINT64_C(0x0000000000ff0000)
#define TMTIMERHANDLE_QUEUE_IDX_SMASK   UINT64_C(0x00000000000000ff)
#define TMTIMERHANDLE_TIMER_IDX_MASK    UINT64_C(0x000000000000ffff)
/** @} */


/**
 * Internal representation of a timer.
 *
 * For correct serialization (without the use of semaphores and
 * other blocking/slow constructs) certain rules applies to updating
 * this structure:
 *      - For thread other than EMT only u64Expire, enmState and pScheduleNext*
 *        are changeable. Everything else is out of bounds.
 *      - Updating of u64Expire timer can only happen in the TMTIMERSTATE_STOPPED
 *        and TMTIMERSTATE_PENDING_RESCHEDULING_SET_EXPIRE states.
 *      - Timers in the TMTIMERSTATE_EXPIRED state are only accessible from EMT.
 *      - Actual destruction of a timer can only be done at scheduling time.
 */
typedef struct TMTIMER
{
    /** Expire time. */
    volatile uint64_t       u64Expire;

    /** Timer state. */
    volatile TMTIMERSTATE   enmState;
    /** The index of the next next timer in the schedule list. */
    uint32_t volatile       idxScheduleNext;

    /** The index of the next timer in the chain. */
    uint32_t                idxNext;
    /** The index of the previous timer in the chain. */
    uint32_t                idxPrev;

    /** The timer frequency hint.  This is 0 if not hint was given. */
    uint32_t volatile       uHzHint;
    /** Timer callback type. */
    TMTIMERTYPE             enmType;

    /** It's own handle value. */
    TMTIMERHANDLE           hSelf;
    /** TMTIMER_FLAGS_XXX.   */
    uint32_t                fFlags;
    /** Explicit alignment padding. */
    uint32_t                u32Alignment;

    /** User argument. */
    RTR3PTR                 pvUser;
    /** The critical section associated with the lock. */
    R3PTRTYPE(PPDMCRITSECT) pCritSect;

    /* --- new cache line (64-bit / 64 bytes) --- */

    /** Type specific data. */
    union
    {
        /** TMTIMERTYPE_DEV. */
        struct
        {
            /** Callback. */
            R3PTRTYPE(PFNTMTIMERDEV)    pfnTimer;
            /** Device instance. */
            PPDMDEVINSR3                pDevIns;
        } Dev;

        /** TMTIMERTYPE_DEV. */
        struct
        {
            /** Callback. */
            R3PTRTYPE(PFNTMTIMERUSB)    pfnTimer;
            /** USB device instance. */
            PPDMUSBINS                  pUsbIns;
        } Usb;

        /** TMTIMERTYPE_DRV. */
        struct
        {
            /** Callback. */
            R3PTRTYPE(PFNTMTIMERDRV)    pfnTimer;
            /** Device instance. */
            R3PTRTYPE(PPDMDRVINS)       pDrvIns;
        } Drv;

        /** TMTIMERTYPE_INTERNAL. */
        struct
        {
            /** Callback. */
            R3PTRTYPE(PFNTMTIMERINT)    pfnTimer;
        } Internal;
    } u;

    /** The timer name. */
    char                    szName[32];

    /** @todo think of two useful release statistics counters here to fill up the
     *        cache line. */
#ifndef VBOX_WITH_STATISTICS
    uint64_t                auAlignment2[2];
#else
    STAMPROFILE             StatTimer;
    STAMPROFILE             StatCritSectEnter;
    STAMCOUNTER             StatGet;
    STAMCOUNTER             StatSetAbsolute;
    STAMCOUNTER             StatSetRelative;
    STAMCOUNTER             StatStop;
    uint64_t                auAlignment2[6];
#endif
} TMTIMER;
AssertCompileMemberSize(TMTIMER, u64Expire, sizeof(uint64_t));
AssertCompileMemberSize(TMTIMER, enmState, sizeof(uint32_t));
AssertCompileSizeAlignment(TMTIMER, 64);


/**
 * Updates a timer state in the correct atomic manner.
 */
#if 1
# define TM_SET_STATE(pTimer, state) \
    ASMAtomicWriteU32((uint32_t volatile *)&(pTimer)->enmState, state)
#else
# define TM_SET_STATE(pTimer, state) \
    do { \
        uint32_t uOld1 = (pTimer)->enmState; \
        Log(("%s: %p: %d -> %d\n", __FUNCTION__, (pTimer), (pTimer)->enmState, state)); \
        uint32_t uOld2 = ASMAtomicXchgU32((uint32_t volatile *)&(pTimer)->enmState, state); \
        Assert(uOld1 == uOld2); \
    } while (0)
#endif

/**
 * Tries to updates a timer state in the correct atomic manner.
 */
#if 1
# define TM_TRY_SET_STATE(pTimer, StateNew, StateOld, fRc) \
    (fRc) = ASMAtomicCmpXchgU32((uint32_t volatile *)&(pTimer)->enmState, StateNew, StateOld)
#else
# define TM_TRY_SET_STATE(pTimer, StateNew, StateOld, fRc) \
    do { (fRc) = ASMAtomicCmpXchgU32((uint32_t volatile *)&(pTimer)->enmState, StateNew, StateOld); \
         Log(("%s: %p: %d -> %d %RTbool\n", __FUNCTION__, (pTimer), StateOld, StateNew, fRc)); \
    } while (0)
#endif


/**
 * A timer queue, shared.
 */
typedef struct TMTIMERQUEUE
{
    /** The ring-0 mapping of the timer table. */
    R3PTRTYPE(PTMTIMER)     paTimers;

    /** The cached expire time for this queue.
     * Updated by EMT when scheduling the queue or modifying the head timer.
     * Assigned UINT64_MAX when there is no head timer. */
    uint64_t                u64Expire;
    /** Doubly linked list of active timers.
     *
     * When no scheduling is pending, this list is will be ordered by expire time (ascending).
     * Access is serialized by only letting the emulation thread (EMT) do changes.
     */
    uint32_t                idxActive;
    /** List of timers pending scheduling of some kind.
     *
     * Timer stats allowed in the list are TMTIMERSTATE_PENDING_STOPPING,
     * TMTIMERSTATE_PENDING_DESTRUCTION, TMTIMERSTATE_PENDING_STOPPING_DESTRUCTION,
     * TMTIMERSTATE_PENDING_RESCHEDULING and TMTIMERSTATE_PENDING_SCHEDULE.
     */
    uint32_t volatile       idxSchedule;
    /** The clock for this queue. */
    TMCLOCK                 enmClock;   /**< @todo consider duplicating this in TMTIMERQUEUER0 for better cache locality (paTimers). */

    /** The size of the paTimers allocation (in entries). */
    uint32_t                cTimersAlloc;
    /** Number of free timer entries. */
    uint32_t                cTimersFree;
    /** Where to start looking for free timers. */
    uint32_t                idxFreeHint;
    /** The queue name. */
    char                    szName[16];
    /** Set when a thread is doing scheduling and callback. */
    bool volatile           fBeingProcessed;
    /** Set if we've disabled growing. */
    bool                    fCannotGrow;
    /** Align on 64-byte boundrary. */
    bool                    afAlignment1[2];
    /** The current max timer Hz hint. */
    uint32_t volatile       uMaxHzHint;

    /* --- new cache line (64-bit / 64 bytes) --- */

    /** Time spent doing scheduling and timer callbacks. */
    STAMPROFILE             StatDo;
    /** The thread servicing this queue, NIL if none. */
    R3PTRTYPE(RTTHREAD)     hThread;
    /** The handle to the event semaphore the worker thread sleeps on. */
    SUPSEMEVENT             hWorkerEvt;
    /** Absolute sleep deadline for the worker (enmClock time). */
    uint64_t volatile       tsWorkerWakeup;
    uint64_t                u64Alignment2;

    /** Lock serializing the active timer list and associated work. */
    PDMCRITSECT             TimerLock;
    /** Lock serializing timer allocation and deallocation.
     * @note This may be used in read-mode all over the place if we later
     *       implement runtime array growing. */
    PDMCRITSECTRW           AllocLock;
} TMTIMERQUEUE;
AssertCompileMemberAlignment(TMTIMERQUEUE, AllocLock, 64);
AssertCompileSizeAlignment(TMTIMERQUEUE, 64);
/** Pointer to a timer queue. */
typedef TMTIMERQUEUE *PTMTIMERQUEUE;

/**
 * A timer queue, ring-0 only bits.
 */
typedef struct TMTIMERQUEUER0
{
    /** The size of the paTimers allocation (in entries). */
    uint32_t                cTimersAlloc;
    uint32_t                uAlignment;
    /** The ring-0 mapping of the timer table. */
    R0PTRTYPE(PTMTIMER)     paTimers;
    /** Handle to the timer table allocation. */
    RTR0MEMOBJ              hMemObj;
    /** Handle to the ring-3 mapping of the timer table. */
    RTR0MEMOBJ              hMapObj;
} TMTIMERQUEUER0;
/** Pointer to the ring-0 timer queue data. */
typedef TMTIMERQUEUER0 *PTMTIMERQUEUER0;

/** Pointer to the current context data for a timer queue.
 * @note In ring-3 this is the same as the shared data. */
#ifdef IN_RING3
typedef TMTIMERQUEUE   *PTMTIMERQUEUECC;
#else
typedef TMTIMERQUEUER0 *PTMTIMERQUEUECC;
#endif
/** Helper macro for getting the current context queue point. */
#ifdef IN_RING3
# define TM_GET_TIMER_QUEUE_CC(a_pVM, a_idxQueue, a_pQueueShared)  (a_pQueueShared)
#else
# define TM_GET_TIMER_QUEUE_CC(a_pVM, a_idxQueue, a_pQueueShared)  (&(a_pVM)->tmr0.s.aTimerQueues[a_idxQueue])
#endif


/**
 * CPU load data set.
 * Mainly used by tmR3CpuLoadTimer.
 */
typedef struct TMCPULOADSTATE
{
    /** The percent of the period spent executing guest code. */
    uint8_t                 cPctExecuting;
    /** The percent of the period spent halted. */
    uint8_t                 cPctHalted;
    /** The percent of the period spent on other things. */
    uint8_t                 cPctOther;
    /** Explicit alignment padding */
    uint8_t                 au8Alignment[1];
    /** Index into aHistory of the current entry. */
    uint16_t volatile       idxHistory;
    /** Number of valid history entries before idxHistory. */
    uint16_t volatile       cHistoryEntries;

    /** Previous cNsTotal value. */
    uint64_t                cNsPrevTotal;
    /** Previous cNsExecuting value. */
    uint64_t                cNsPrevExecuting;
    /** Previous cNsHalted value. */
    uint64_t                cNsPrevHalted;
    /** Data for the last 30 min (given an interval of 1 second). */
    struct
    {
        uint8_t             cPctExecuting;
        /** The percent of the period spent halted. */
        uint8_t             cPctHalted;
        /** The percent of the period spent on other things. */
        uint8_t             cPctOther;
    }                       aHistory[30*60];
} TMCPULOADSTATE;
AssertCompileSizeAlignment(TMCPULOADSTATE, 8);
AssertCompileMemberAlignment(TMCPULOADSTATE, cNsPrevTotal, 8);
/** Pointer to a CPU load data set. */
typedef TMCPULOADSTATE *PTMCPULOADSTATE;


/**
 * TSC mode.
 *
 * The main modes of how TM implements the TSC clock (TMCLOCK_TSC).
 */
typedef enum TMTSCMODE
{
    /** The guest TSC is an emulated, virtual TSC. */
    TMTSCMODE_VIRT_TSC_EMULATED = 1,
    /** The guest TSC is an offset of the real TSC. */
    TMTSCMODE_REAL_TSC_OFFSET,
    /** The guest TSC is dynamically derived through emulating or offsetting. */
    TMTSCMODE_DYNAMIC,
    /** The native API provides it. */
    TMTSCMODE_NATIVE_API
} TMTSCMODE;
AssertCompileSize(TMTSCMODE, sizeof(uint32_t));


/**
 * TM VM Instance data.
 * Changes to this must checked against the padding of the cfgm union in VM!
 */
typedef struct TM
{
    /** Timer queues for the different clock types.
     * @note is first in the structure to ensure cache-line alignment.  */
    TMTIMERQUEUE                aTimerQueues[TMCLOCK_MAX];

    /** The current TSC mode of the VM.
     *  Config variable: Mode (string). */
    TMTSCMODE                   enmTSCMode;
    /** The original TSC mode of the VM. */
    TMTSCMODE                   enmOriginalTSCMode;
    /** Whether the TSC is tied to the execution of code.
     * Config variable: TSCTiedToExecution (bool) */
    bool                        fTSCTiedToExecution;
    /** Modifier for fTSCTiedToExecution which pauses the TSC while halting if true.
     * Config variable: TSCNotTiedToHalt (bool) */
    bool                        fTSCNotTiedToHalt;
    /** Whether TM TSC mode switching is allowed at runtime. */
    bool                        fTSCModeSwitchAllowed;
    /** Whether the guest has enabled use of paravirtualized TSC. */
    bool                        fParavirtTscEnabled;
    /** The ID of the virtual CPU that normally runs the timers. */
    VMCPUID                     idTimerCpu;

    /** The number of CPU clock ticks per seconds of the host CPU.   */
    uint64_t                    cTSCTicksPerSecondHost;
    /** The number of CPU clock ticks per second (TMCLOCK_TSC).
     * Config variable: TSCTicksPerSecond (64-bit unsigned int)
     * The config variable implies @c enmTSCMode would be
     * TMTSCMODE_VIRT_TSC_EMULATED. */
    uint64_t                    cTSCTicksPerSecond;
    /** The TSC difference introduced by pausing the VM. */
    uint64_t                    offTSCPause;
    /** The TSC value when the last TSC was paused. */
    uint64_t                    u64LastPausedTSC;
    /** CPU TSCs ticking indicator (one for each VCPU). */
    uint32_t volatile           cTSCsTicking;

    /** Virtual time ticking enabled indicator (counter for each VCPU). (TMCLOCK_VIRTUAL) */
    uint32_t volatile           cVirtualTicking;
    /** Virtual time is not running at 100%. */
    bool                        fVirtualWarpDrive;
    /** Virtual timer synchronous time ticking enabled indicator (bool). (TMCLOCK_VIRTUAL_SYNC) */
    bool volatile               fVirtualSyncTicking;
    /** Virtual timer synchronous time catch-up active. */
    bool volatile               fVirtualSyncCatchUp;
    /** The multiplier for TSC. */
    uint8_t                     u8TSCMultiplier;
    /** WarpDrive percentage.
     * 100% is normal (fVirtualSyncNormal == true). When other than 100% we apply
     * this percentage to the raw time source for the period it's been valid in,
     * i.e. since u64VirtualWarpDriveStart. */
    uint32_t                    u32VirtualWarpDrivePercentage;

    /** The offset of the virtual clock relative to it's timesource.
     * Only valid if fVirtualTicking is set. */
    uint64_t                    u64VirtualOffset;
    /** The guest virtual time when fVirtualTicking is cleared. */
    uint64_t                    u64Virtual;
    /** When the Warp drive was started or last adjusted.
     * Only valid when fVirtualWarpDrive is set. */
    uint64_t                    u64VirtualWarpDriveStart;
    /** The previously returned nano TS.
     * This handles TSC drift on SMP systems and expired interval.
     * This is a valid range u64NanoTS to u64NanoTS + 1000000000 (ie. 1sec). */
    uint64_t volatile           u64VirtualRawPrev;
    /** The ring-3 data structure for the RTTimeNanoTS workers used by tmVirtualGetRawNanoTS. */
    RTTIMENANOTSDATAR3          VirtualGetRawData;
    /** Pointer to the ring-3 tmVirtualGetRawNanoTS worker function. */
    R3PTRTYPE(PFNTIMENANOTSINTERNAL) pfnVirtualGetRaw;
    /** The guest virtual timer synchronous time when fVirtualSyncTicking is cleared.
     * When fVirtualSyncTicking is set it holds the last time returned to
     * the guest (while the lock was held). */
    uint64_t volatile           u64VirtualSync;
    /** The offset of the timer synchronous virtual clock (TMCLOCK_VIRTUAL_SYNC) relative
     * to the virtual clock (TMCLOCK_VIRTUAL).
     * (This is accessed by the timer thread and must be updated atomically.) */
    uint64_t volatile           offVirtualSync;
    /** The offset into offVirtualSync that's been irrevocably given up by failed catch-up attempts.
     * Thus the current lag is offVirtualSync - offVirtualSyncGivenUp. */
    uint64_t                    offVirtualSyncGivenUp;
    /** The TMCLOCK_VIRTUAL at the previous TMVirtualGetSync call when catch-up is active. */
    uint64_t volatile           u64VirtualSyncCatchUpPrev;
    /** The current catch-up percentage. */
    uint32_t volatile           u32VirtualSyncCatchUpPercentage;
    /** How much slack when processing timers. */
    uint32_t                    u32VirtualSyncScheduleSlack;
    /** When to stop catch-up. */
    uint64_t                    u64VirtualSyncCatchUpStopThreshold;
    /** When to give up catch-up. */
    uint64_t                    u64VirtualSyncCatchUpGiveUpThreshold;
/** @def TM_MAX_CATCHUP_PERIODS
 * The number of catchup rates. */
#define TM_MAX_CATCHUP_PERIODS  10
    /** The aggressiveness of the catch-up relative to how far we've lagged behind.
     * The idea is to have increasing catch-up percentage as the lag increases. */
    struct TMCATCHUPPERIOD
    {
        uint64_t                u64Start;       /**< When this period starts. (u64VirtualSyncOffset). */
        uint32_t                u32Percentage;  /**< The catch-up percent to apply. */
        uint32_t                u32Alignment;   /**< Structure alignment */
    }                           aVirtualSyncCatchUpPeriods[TM_MAX_CATCHUP_PERIODS];

    union
    {
        /** Combined value for updating. */
        uint64_t volatile       u64Combined;
        struct
        {
            /** Bitmap indicating which timer queues needs their uMaxHzHint updated. */
            uint32_t volatile   bmNeedsUpdating;
            /** The current max timer Hz hint. */
            uint32_t volatile   uMax;
        } s;
    } HzHint;
    /** @cfgm{/TM/HostHzMax, uint32_t, Hz, 0, UINT32_MAX, 20000}
     * The max host Hz frequency hint returned by TMCalcHostTimerFrequency.  */
    uint32_t                    cHostHzMax;
    /** @cfgm{/TM/HostHzFudgeFactorTimerCpu, uint32_t, Hz, 0, UINT32_MAX, 111}
     * The number of Hz TMCalcHostTimerFrequency adds for the timer CPU.  */
    uint32_t                    cPctHostHzFudgeFactorTimerCpu;
    /** @cfgm{/TM/HostHzFudgeFactorOtherCpu, uint32_t, Hz, 0, UINT32_MAX, 110}
     * The number of Hz TMCalcHostTimerFrequency adds for the other CPUs. */
    uint32_t                    cPctHostHzFudgeFactorOtherCpu;
    /** @cfgm{/TM/HostHzFudgeFactorCatchUp100, uint32_t, Hz, 0, UINT32_MAX, 300}
     *  The fudge factor (expressed in percent) that catch-up percentages below
     * 100% is multiplied by. */
    uint32_t                    cPctHostHzFudgeFactorCatchUp100;
    /** @cfgm{/TM/HostHzFudgeFactorCatchUp200, uint32_t, Hz, 0, UINT32_MAX, 250}
     * The fudge factor (expressed in percent) that catch-up percentages
     * 100%-199% is multiplied by. */
    uint32_t                    cPctHostHzFudgeFactorCatchUp200;
    /** @cfgm{/TM/HostHzFudgeFactorCatchUp400, uint32_t, Hz, 0, UINT32_MAX, 200}
     * The fudge factor (expressed in percent) that catch-up percentages
     * 200%-399% is multiplied by. */
    uint32_t                    cPctHostHzFudgeFactorCatchUp400;

    /** The UTC offset in ns.
     * This is *NOT* for converting UTC to local time. It is for converting real
     * world UTC time to VM UTC time. This feature is indented for doing date
     * testing of software and similar.
     * @todo Implement warpdrive on UTC. */
    int64_t                     offUTC;
    /** The last value TMR3UtcNow returned. */
    int64_t volatile            nsLastUtcNow;
    /** File to touch on UTC jump. */
    R3PTRTYPE(char *)           pszUtcTouchFileOnJump;

    /** Pointer to our R3 mapping of the GIP. */
    R3PTRTYPE(void *)           pvGIPR3;

    /** The schedule timer timer handle (runtime timer).
     * This timer will do frequent check on pending queue schedules and
     * raise VM_FF_TIMER to pull EMTs attention to them.
     */
    R3PTRTYPE(PRTTIMER)         pTimer;
    /** Interval in milliseconds of the pTimer timer. */
    uint32_t                    u32TimerMillies;

    /** Indicates that queues are being run. */
    bool volatile               fRunningQueues;
    /** Indicates that the virtual sync queue is being run. */
    bool volatile               fRunningVirtualSyncQueue;
    /** Alignment */
    bool                        afAlignment3[2];

    /** Lock serializing access to the VirtualSync clock and the associated
     * timer queue.
     * @todo Consider merging this with the TMTIMERQUEUE::TimerLock for the
     *       virtual sync queue. */
    PDMCRITSECT                 VirtualSyncLock;

    /** CPU load state for all the virtual CPUs (tmR3CpuLoadTimer). */
    TMCPULOADSTATE              CpuLoad;

    /** TMR3TimerQueuesDo
     * @{ */
    STAMPROFILE                 StatDoQueues;
    /** @} */
    /** tmSchedule
     * @{ */
    STAMPROFILE                 StatScheduleOneRZ;
    STAMPROFILE                 StatScheduleOneR3;
    STAMCOUNTER                 StatScheduleSetFF;
    STAMCOUNTER                 StatPostponedR3;
    STAMCOUNTER                 StatPostponedRZ;
    /** @} */
    /** Read the time
     * @{ */
    STAMCOUNTER                 StatVirtualGet;
    STAMCOUNTER                 StatVirtualGetSetFF;
    STAMCOUNTER                 StatVirtualSyncGet;
    STAMCOUNTER                 StatVirtualSyncGetAdjLast;
    STAMCOUNTER                 StatVirtualSyncGetELoop;
    STAMCOUNTER                 StatVirtualSyncGetExpired;
    STAMCOUNTER                 StatVirtualSyncGetLockless;
    STAMCOUNTER                 StatVirtualSyncGetLocked;
    STAMCOUNTER                 StatVirtualSyncGetSetFF;
    STAMCOUNTER                 StatVirtualPause;
    STAMCOUNTER                 StatVirtualResume;
    /** @} */
    /** TMTimerPoll
     * @{ */
    STAMCOUNTER                 StatPoll;
    STAMCOUNTER                 StatPollAlreadySet;
    STAMCOUNTER                 StatPollELoop;
    STAMCOUNTER                 StatPollMiss;
    STAMCOUNTER                 StatPollRunning;
    STAMCOUNTER                 StatPollSimple;
    STAMCOUNTER                 StatPollVirtual;
    STAMCOUNTER                 StatPollVirtualSync;
    /** @} */
    /** TMTimerSet sans virtual sync timers.
     * @{ */
    STAMCOUNTER                 StatTimerSet;
    STAMCOUNTER                 StatTimerSetOpt;
    STAMPROFILE                 StatTimerSetRZ;
    STAMPROFILE                 StatTimerSetR3;
    STAMCOUNTER                 StatTimerSetStStopped;
    STAMCOUNTER                 StatTimerSetStExpDeliver;
    STAMCOUNTER                 StatTimerSetStActive;
    STAMCOUNTER                 StatTimerSetStPendStop;
    STAMCOUNTER                 StatTimerSetStPendStopSched;
    STAMCOUNTER                 StatTimerSetStPendSched;
    STAMCOUNTER                 StatTimerSetStPendResched;
    STAMCOUNTER                 StatTimerSetStOther;
    /** @}  */
    /** TMTimerSet on virtual sync timers.
     * @{ */
    STAMCOUNTER                 StatTimerSetVs;
    STAMPROFILE                 StatTimerSetVsRZ;
    STAMPROFILE                 StatTimerSetVsR3;
    STAMCOUNTER                 StatTimerSetVsStStopped;
    STAMCOUNTER                 StatTimerSetVsStExpDeliver;
    STAMCOUNTER                 StatTimerSetVsStActive;
    /** @} */
    /** TMTimerSetRelative sans virtual sync timers
     * @{ */
    STAMCOUNTER                 StatTimerSetRelative;
    STAMPROFILE                 StatTimerSetRelativeRZ;
    STAMPROFILE                 StatTimerSetRelativeR3;
    STAMCOUNTER                 StatTimerSetRelativeOpt;
    STAMCOUNTER                 StatTimerSetRelativeStStopped;
    STAMCOUNTER                 StatTimerSetRelativeStExpDeliver;
    STAMCOUNTER                 StatTimerSetRelativeStActive;
    STAMCOUNTER                 StatTimerSetRelativeStPendStop;
    STAMCOUNTER                 StatTimerSetRelativeStPendStopSched;
    STAMCOUNTER                 StatTimerSetRelativeStPendSched;
    STAMCOUNTER                 StatTimerSetRelativeStPendResched;
    STAMCOUNTER                 StatTimerSetRelativeStOther;
    /** @} */
    /** TMTimerSetRelative on virtual sync timers.
     * @{ */
    STAMCOUNTER                 StatTimerSetRelativeVs;
    STAMPROFILE                 StatTimerSetRelativeVsRZ;
    STAMPROFILE                 StatTimerSetRelativeVsR3;
    STAMCOUNTER                 StatTimerSetRelativeVsStStopped;
    STAMCOUNTER                 StatTimerSetRelativeVsStExpDeliver;
    STAMCOUNTER                 StatTimerSetRelativeVsStActive;
    /** @} */
    /** TMTimerStop sans virtual sync.
     * @{ */
    STAMPROFILE                 StatTimerStopRZ;
    STAMPROFILE                 StatTimerStopR3;
    /** @} */
    /** TMTimerStop on virtual sync timers.
     * @{ */
    STAMPROFILE                 StatTimerStopVsRZ;
    STAMPROFILE                 StatTimerStopVsR3;
    /** @} */
    /** VirtualSync - Running and Catching Up
     * @{ */
    STAMCOUNTER                 StatVirtualSyncRun;
    STAMCOUNTER                 StatVirtualSyncRunRestart;
    STAMPROFILE                 StatVirtualSyncRunSlack;
    STAMCOUNTER                 StatVirtualSyncRunStop;
    STAMCOUNTER                 StatVirtualSyncRunStoppedAlready;
    STAMCOUNTER                 StatVirtualSyncGiveUp;
    STAMCOUNTER                 StatVirtualSyncGiveUpBeforeStarting;
    STAMPROFILEADV              StatVirtualSyncCatchup;
    STAMCOUNTER                 aStatVirtualSyncCatchupInitial[TM_MAX_CATCHUP_PERIODS];
    STAMCOUNTER                 aStatVirtualSyncCatchupAdjust[TM_MAX_CATCHUP_PERIODS];
    /** @} */
    /** TMR3VirtualSyncFF (non dedicated EMT). */
    STAMPROFILE                 StatVirtualSyncFF;
    /** The timer callback. */
    STAMCOUNTER                 StatTimerCallbackSetFF;
    STAMCOUNTER                 StatTimerCallback;

    /** Calls to TMCpuTickSet. */
    STAMCOUNTER                 StatTSCSet;

    /** TSC starts and stops. */
    STAMCOUNTER                 StatTSCPause;
    STAMCOUNTER                 StatTSCResume;

    /** @name Reasons for refusing TSC offsetting in TMCpuTickCanUseRealTSC.
     * @{ */
    STAMCOUNTER                 StatTSCNotFixed;
    STAMCOUNTER                 StatTSCNotTicking;
    STAMCOUNTER                 StatTSCCatchupLE010;
    STAMCOUNTER                 StatTSCCatchupLE025;
    STAMCOUNTER                 StatTSCCatchupLE100;
    STAMCOUNTER                 StatTSCCatchupOther;
    STAMCOUNTER                 StatTSCWarp;
    STAMCOUNTER                 StatTSCUnderflow;
    STAMCOUNTER                 StatTSCSyncNotTicking;
    /** @} */

#ifdef TM_SECONDS_TO_AUTOMATIC_POWER_OFF
    /** The automatic power off timer. */
    TMTIMERHANDLE               hAutoPowerOff;
#endif
} TM;
/** Pointer to TM VM instance data. */
typedef TM *PTM;


/**
 * TM VMCPU Instance data.
 * Changes to this must checked against the padding of the tm union in VM!
 */
typedef struct TMCPU
{
    /** The offset between the host tick (TSC/virtual depending on the TSC mode) and
     *  the guest tick. */
    uint64_t                    offTSCRawSrc;
    /** The guest TSC when fTicking is cleared. */
    uint64_t                    u64TSC;
    /** The last seen TSC by the guest. */
    uint64_t                    u64TSCLastSeen;
    /** CPU timestamp ticking enabled indicator (bool). (RDTSC) */
    bool                        fTSCTicking;
#ifdef VBOX_WITHOUT_NS_ACCOUNTING
    bool                        afAlignment1[7]; /**< alignment padding */
#else /* !VBOX_WITHOUT_NS_ACCOUNTING */

    /** Set by the timer callback to trigger updating of statistics in
     *  TMNotifyEndOfExecution. */
    bool volatile               fUpdateStats;
    bool                        afAlignment1[6];
    /** The time not spent executing or halted.
     * @note Only updated after halting and after the timer runs. */
    uint64_t                    cNsOtherStat;
    /** Reasonably up to date total run time value.
     * @note Only updated after halting and after the timer runs. */
    uint64_t                    cNsTotalStat;
# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
    /** Resettable copy of version of cNsOtherStat.
    * @note Only updated after halting. */
    STAMCOUNTER                 StatNsOther;
    /** Resettable copy of cNsTotalStat.
     * @note Only updated after halting. */
    STAMCOUNTER                 StatNsTotal;
# else
    uint64_t                    auAlignment2[2];
# endif

    /** @name Core accounting data.
     * @note Must be cache-line aligned and only written to by the EMT owning it.
     * @{ */
    /** The cNsXXX generation. */
    uint32_t volatile           uTimesGen;
    /** Set if executing (between TMNotifyStartOfExecution and
     *  TMNotifyEndOfExecution). */
    bool volatile               fExecuting;
    /** Set if halting (between TMNotifyStartOfHalt and TMNotifyEndOfHalt). */
    bool volatile               fHalting;
    /** Set if we're suspended and u64NsTsStartTotal is to be cNsTotal. */
    bool volatile               fSuspended;
    bool                        afAlignment;
    /** The nanosecond timestamp of the CPU start or resume.
     * This is recalculated when the VM is started so that
     * cNsTotal = RTTimeNanoTS() - u64NsTsStartCpu. */
    uint64_t                    nsStartTotal;
    /** The TSC of the last start-execute notification. */
    uint64_t                    uTscStartExecuting;
    /** The number of nanoseconds spent executing. */
    uint64_t                    cNsExecuting;
    /** The number of guest execution runs. */
    uint64_t                    cPeriodsExecuting;
    /** The nanosecond timestamp of the last start-halt notification. */
    uint64_t                    nsStartHalting;
    /** The number of nanoseconds being halted. */
    uint64_t                    cNsHalted;
    /** The number of halts. */
    uint64_t                    cPeriodsHalted;
    /** @} */

# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
    /** Resettable version of cNsExecuting. */
    STAMPROFILE                 StatNsExecuting;
    /** Long execution intervals. */
    STAMPROFILE                 StatNsExecLong;
    /** Short execution intervals. */
    STAMPROFILE                 StatNsExecShort;
    /** Tiny execution intervals. */
    STAMPROFILE                 StatNsExecTiny;
    /** Resettable version of cNsHalted. */
    STAMPROFILE                 StatNsHalted;
# endif

    /** CPU load state for this virtual CPU (tmR3CpuLoadTimer). */
    TMCPULOADSTATE              CpuLoad;
#endif
} TMCPU;
#ifndef VBOX_WITHOUT_NS_ACCOUNTING
AssertCompileMemberAlignment(TMCPU, uTimesGen, 64);
# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
AssertCompileMemberAlignment(TMCPU, StatNsExecuting, 64);
# else
AssertCompileMemberAlignment(TMCPU, CpuLoad, 64);
# endif
#endif
/** Pointer to TM VMCPU instance data. */
typedef TMCPU *PTMCPU;


/**
 * TM data kept in the ring-0 GVM.
 */
typedef struct TMR0PERVM
{
    /** Timer queues for the different clock types. */
    TMTIMERQUEUER0              aTimerQueues[TMCLOCK_MAX];

    /** The ring-0 data structure for the RTTimeNanoTS workers used by tmVirtualGetRawNanoTS. */
    RTTIMENANOTSDATAR0          VirtualGetRawData;
    /** Pointer to the ring-0 tmVirtualGetRawNanoTS worker function. */
    R0PTRTYPE(PFNTIMENANOTSINTERNAL) pfnVirtualGetRaw;
} TMR0PERVM;


const char             *tmTimerState(TMTIMERSTATE enmState);
void                    tmTimerQueueSchedule(PVMCC pVM, PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue);
#ifdef VBOX_STRICT
void                    tmTimerQueuesSanityChecks(PVMCC pVM, const char *pszWhere);
#endif
void                    tmHCTimerQueueGrowInit(PTMTIMER paTimers, TMTIMER const *paOldTimers, uint32_t cNewTimers, uint32_t cOldTimers);

uint64_t                tmR3CpuTickGetRawVirtualNoCheck(PVM pVM);
int                     tmCpuTickPause(PVMCPUCC pVCpu);
int                     tmCpuTickPauseLocked(PVMCC pVM, PVMCPUCC pVCpu);
int                     tmCpuTickResume(PVMCC pVM, PVMCPUCC pVCpu);
int                     tmCpuTickResumeLocked(PVMCC pVM, PVMCPUCC pVCpu);

int                     tmVirtualPauseLocked(PVMCC pVM);
int                     tmVirtualResumeLocked(PVMCC pVM);
DECLCALLBACK(DECLEXPORT(void))      tmVirtualNanoTSBad(PRTTIMENANOTSDATA pData, uint64_t u64NanoTS,
                                                       uint64_t u64DeltaPrev, uint64_t u64PrevNanoTS);
DECLCALLBACK(DECLEXPORT(uint64_t))  tmVirtualNanoTSRediscover(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra);
DECLCALLBACK(DECLEXPORT(uint64_t))  tmVirtualNanoTSBadCpuIndex(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra,
                                                              uint16_t idApic, uint16_t iCpuSet, uint16_t iGipCpu);
/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_TMInternal_h */
