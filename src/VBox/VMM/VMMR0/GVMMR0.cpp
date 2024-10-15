/* $Id: GVMMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GVMM - Global VM Manager.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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


/** @page pg_gvmm   GVMM - The Global VM Manager
 *
 * The Global VM Manager lives in ring-0.  Its main function at the moment is
 * to manage a list of all running VMs, keep a ring-0 only structure (GVM) for
 * each of them, and assign them unique identifiers (so GMM can track page
 * owners).  The GVMM also manage some of the host CPU resources, like the
 * periodic preemption timer.
 *
 * The GVMM will create a ring-0 object for each VM when it is registered, this
 * is both for session cleanup purposes and for having a point where it is
 * possible to implement usage polices later (in SUPR0ObjRegister).
 *
 *
 * @section  sec_gvmm_ppt       Periodic Preemption Timer (PPT)
 *
 * On system that sports a high resolution kernel timer API, we use per-cpu
 * timers to generate interrupts that preempts VT-x, AMD-V and raw-mode guest
 * execution.  The timer frequency is calculating by taking the max
 * TMCalcHostTimerFrequency for all VMs running on a CPU for the last ~160 ms
 * (RT_ELEMENTS((PGVMMHOSTCPU)0, Ppt.aHzHistory) *
 * GVMMHOSTCPU_PPT_HIST_INTERVAL_NS).
 *
 * The TMCalcHostTimerFrequency() part of the things gets its takes the max
 * TMTimerSetFrequencyHint() value and adjusts by the current catch-up percent,
 * warp drive percent and some fudge factors.  VMMR0.cpp reports the result via
 * GVMMR0SchedUpdatePeriodicPreemptionTimer() before switching to the VT-x,
 * AMD-V and raw-mode execution environments.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GVMM
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/gmm.h>
#include "GVMMR0Internal.h"
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/vmm.h>
#ifdef VBOX_WITH_NEM_R0
# include <VBox/vmm/nem.h>
#endif
#include <VBox/vmm/vmcpuset.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/param.h>
#include <VBox/err.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/critsect.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>
#include <VBox/log.h>
#include <iprt/thread.h>
#include <iprt/process.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/spinlock.h>
#include <iprt/timer.h>

#include "dtrace/VBoxVMM.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS) || defined(DOXYGEN_RUNNING)
/** Define this to enable the periodic preemption timer. */
# define GVMM_SCHED_WITH_PPT
#endif

#if /*defined(RT_OS_WINDOWS) ||*/ defined(DOXYGEN_RUNNING)
/** Define this to enable the per-EMT high resolution wakeup timers. */
# define GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
#endif


/** Special value that GVMMR0DeregisterVCpu sets. */
#define GVMM_RTNATIVETHREAD_DESTROYED       (~(RTNATIVETHREAD)1)
AssertCompile(GVMM_RTNATIVETHREAD_DESTROYED != NIL_RTNATIVETHREAD);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Global VM handle.
 */
typedef struct GVMHANDLE
{
    /** The index of the next handle in the list (free or used). (0 is nil.) */
    uint16_t volatile   iNext;
    /** Our own index / handle value. */
    uint16_t            iSelf;
    /** The process ID of the handle owner.
     * This is used for access checks. */
    RTPROCESS           ProcId;
    /** The pointer to the ring-0 only (aka global) VM structure. */
    PGVM                pGVM;
    /** The virtual machine object. */
    void               *pvObj;
    /** The session this VM is associated with. */
    PSUPDRVSESSION      pSession;
    /** The ring-0 handle of the EMT0 thread.
     * This is used for ownership checks as well as looking up a VM handle by thread
     * at times like assertions. */
    RTNATIVETHREAD      hEMT0;
} GVMHANDLE;
/** Pointer to a global VM handle. */
typedef GVMHANDLE *PGVMHANDLE;

/** Number of GVM handles (including the NIL handle). */
#if HC_ARCH_BITS == 64
# define GVMM_MAX_HANDLES   8192
#else
# define GVMM_MAX_HANDLES   128
#endif

/**
 * Per host CPU GVMM data.
 */
typedef struct GVMMHOSTCPU
{
    /** Magic number (GVMMHOSTCPU_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The CPU ID. */
    RTCPUID             idCpu;
    /** The CPU set index. */
    uint32_t            idxCpuSet;

#ifdef GVMM_SCHED_WITH_PPT
    /** Periodic preemption timer data. */
    struct
    {
        /** The handle to the periodic preemption timer. */
        PRTTIMER            pTimer;
        /** Spinlock protecting the data below. */
        RTSPINLOCK          hSpinlock;
        /** The smalles Hz that we need to care about. (static) */
        uint32_t            uMinHz;
        /** The number of ticks between each historization. */
        uint32_t            cTicksHistoriziationInterval;
        /** The current historization tick (counting up to
         * cTicksHistoriziationInterval and then resetting). */
        uint32_t            iTickHistorization;
        /** The current timer interval.  This is set to 0 when inactive. */
        uint32_t            cNsInterval;
        /** The current timer frequency.  This is set to 0 when inactive. */
        uint32_t            uTimerHz;
        /** The current max frequency reported by the EMTs.
         * This gets historicize and reset by the timer callback.  This is
         * read without holding the spinlock, so needs atomic updating. */
        uint32_t volatile   uDesiredHz;
        /** Whether the timer was started or not. */
        bool volatile       fStarted;
        /** Set if we're starting timer. */
        bool volatile       fStarting;
        /** The index of the next history entry (mod it). */
        uint32_t            iHzHistory;
        /** Historicized uDesiredHz values.  The array wraps around, new entries
         * are added at iHzHistory. This is updated approximately every
         * GVMMHOSTCPU_PPT_HIST_INTERVAL_NS by the timer callback. */
        uint32_t            aHzHistory[8];
        /** Statistics counter for recording the number of interval changes. */
        uint32_t            cChanges;
        /** Statistics counter for recording the number of timer starts. */
        uint32_t            cStarts;
    } Ppt;
#endif /* GVMM_SCHED_WITH_PPT */

} GVMMHOSTCPU;
/** Pointer to the per host CPU GVMM data. */
typedef GVMMHOSTCPU *PGVMMHOSTCPU;
/** The GVMMHOSTCPU::u32Magic value (Petra, Tanya & Rachel Haden). */
#define GVMMHOSTCPU_MAGIC   UINT32_C(0x19711011)
/** The interval on history entry should cover (approximately) give in
 *  nanoseconds. */
#define GVMMHOSTCPU_PPT_HIST_INTERVAL_NS    UINT32_C(20000000)


/**
 * The GVMM instance data.
 */
typedef struct GVMM
{
    /** Eyecatcher / magic. */
    uint32_t            u32Magic;
    /** The index of the head of the free handle chain. (0 is nil.) */
    uint16_t volatile   iFreeHead;
    /** The index of the head of the active handle chain. (0 is nil.) */
    uint16_t volatile   iUsedHead;
    /** The number of VMs. */
    uint16_t volatile   cVMs;
    /** Alignment padding. */
    uint16_t            u16Reserved;
    /** The number of EMTs. */
    uint32_t volatile   cEMTs;
    /** The number of EMTs that have halted in GVMMR0SchedHalt. */
    uint32_t volatile   cHaltedEMTs;
    /** Mini lock for restricting early wake-ups to one thread. */
    bool volatile       fDoingEarlyWakeUps;
    bool                afPadding[3]; /**< explicit alignment padding. */
    /** When the next halted or sleeping EMT will wake up.
     * This is set to 0 when it needs recalculating and to UINT64_MAX when
     * there are no halted or sleeping EMTs in the GVMM. */
    uint64_t            uNsNextEmtWakeup;
    /** The lock used to serialize VM creation, destruction and associated events that
     * isn't performance critical. Owners may acquire the list lock. */
    RTCRITSECT          CreateDestroyLock;
    /** The lock used to serialize used list updates and accesses.
     * This indirectly includes scheduling since the scheduler will have to walk the
     * used list to examin running VMs. Owners may not acquire any other locks. */
    RTCRITSECTRW        UsedLock;
    /** The handle array.
     * The size of this array defines the maximum number of currently running VMs.
     * The first entry is unused as it represents the NIL handle. */
    GVMHANDLE           aHandles[GVMM_MAX_HANDLES];

    /** @gcfgm{/GVMM/cEMTsMeansCompany, 32-bit, 0, UINT32_MAX, 1}
     * The number of EMTs that means we no longer consider ourselves alone on a
     * CPU/Core.
     */
    uint32_t            cEMTsMeansCompany;
    /** @gcfgm{/GVMM/MinSleepAlone,32-bit, 0, 100000000, 750000, ns}
     * The minimum sleep time for when we're alone, in nano seconds.
     */
    uint32_t            nsMinSleepAlone;
    /** @gcfgm{/GVMM/MinSleepCompany,32-bit,0, 100000000, 15000, ns}
     * The minimum sleep time for when we've got company, in nano seconds.
     */
    uint32_t            nsMinSleepCompany;
#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
    /** @gcfgm{/GVMM/MinSleepWithHrWakeUp,32-bit,0, 100000000, 5000, ns}
     * The minimum sleep time for when we've got a high-resolution wake-up timer, in
     * nano seconds.
     */
    uint32_t            nsMinSleepWithHrTimer;
#endif
    /** @gcfgm{/GVMM/EarlyWakeUp1, 32-bit, 0, 100000000, 25000, ns}
     * The limit for the first round of early wake-ups, given in nano seconds.
     */
    uint32_t            nsEarlyWakeUp1;
    /** @gcfgm{/GVMM/EarlyWakeUp2, 32-bit, 0, 100000000, 50000, ns}
     * The limit for the second round of early wake-ups, given in nano seconds.
     */
    uint32_t            nsEarlyWakeUp2;

    /** Set if we're doing early wake-ups.
     * This reflects  nsEarlyWakeUp1 and nsEarlyWakeUp2.  */
    bool volatile       fDoEarlyWakeUps;

    /** The number of entries in the host CPU array (aHostCpus). */
    uint32_t            cHostCpus;
    /** Per host CPU data (variable length). */
    GVMMHOSTCPU         aHostCpus[1];
} GVMM;
AssertCompileMemberAlignment(GVMM, CreateDestroyLock, 8);
AssertCompileMemberAlignment(GVMM, UsedLock, 8);
AssertCompileMemberAlignment(GVMM, uNsNextEmtWakeup, 8);
/** Pointer to the GVMM instance data. */
typedef GVMM *PGVMM;

/** The GVMM::u32Magic value (Charlie Haden). */
#define GVMM_MAGIC      UINT32_C(0x19370806)



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the GVMM instance data.
 * (Just my general dislike for global variables.) */
static PGVMM g_pGVMM = NULL;

/** Macro for obtaining and validating the g_pGVMM pointer.
 * On failure it will return from the invoking function with the specified return value.
 *
 * @param   pGVMM   The name of the pGVMM variable.
 * @param   rc      The return value on failure. Use VERR_GVMM_INSTANCE for VBox
 *                  status codes.
 */
#define GVMM_GET_VALID_INSTANCE(pGVMM, rc) \
    do { \
        (pGVMM) = g_pGVMM;\
        AssertPtrReturn((pGVMM), (rc)); \
        AssertMsgReturn((pGVMM)->u32Magic == GVMM_MAGIC, ("%p - %#x\n", (pGVMM), (pGVMM)->u32Magic), (rc)); \
    } while (0)

/** Macro for obtaining and validating the g_pGVMM pointer, void function variant.
 * On failure it will return from the invoking function.
 *
 * @param   pGVMM   The name of the pGVMM variable.
 */
#define GVMM_GET_VALID_INSTANCE_VOID(pGVMM) \
    do { \
        (pGVMM) = g_pGVMM;\
        AssertPtrReturnVoid((pGVMM)); \
        AssertMsgReturnVoid((pGVMM)->u32Magic == GVMM_MAGIC, ("%p - %#x\n", (pGVMM), (pGVMM)->u32Magic)); \
    } while (0)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void gvmmR0InitPerVMData(PGVM pGVM, int16_t hSelf, VMCPUID cCpus, PSUPDRVSESSION pSession);
static DECLCALLBACK(void) gvmmR0HandleObjDestructor(void *pvObj, void *pvGVMM, void *pvHandle);
static int gvmmR0ByGVM(PGVM pGVM, PGVMM *ppGVMM, bool fTakeUsedLock);
static int gvmmR0ByGVMandEMT(PGVM pGVM, VMCPUID idCpu, PGVMM *ppGVMM);

#ifdef GVMM_SCHED_WITH_PPT
static DECLCALLBACK(void) gvmmR0SchedPeriodicPreemptionTimerCallback(PRTTIMER pTimer, void *pvUser, uint64_t iTick);
#endif
#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
static DECLCALLBACK(void) gvmmR0EmtWakeUpTimerCallback(PRTTIMER pTimer, void *pvUser, uint64_t iTick);
#endif


/**
 * Initializes the GVMM.
 *
 * This is called while owning the loader semaphore (see supdrvIOCtl_LdrLoad()).
 *
 * @returns VBox status code.
 */
GVMMR0DECL(int) GVMMR0Init(void)
{
    LogFlow(("GVMMR0Init:\n"));

    /*
     * Allocate and initialize the instance data.
     */
    uint32_t cHostCpus = RTMpGetArraySize();
    AssertMsgReturn(cHostCpus > 0 && cHostCpus < _64K, ("%d", (int)cHostCpus), VERR_GVMM_HOST_CPU_RANGE);

    PGVMM pGVMM = (PGVMM)RTMemAllocZ(RT_UOFFSETOF_DYN(GVMM, aHostCpus[cHostCpus]));
    if (!pGVMM)
        return VERR_NO_MEMORY;
    int rc = RTCritSectInitEx(&pGVMM->CreateDestroyLock, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE,
                              "GVMM-CreateDestroyLock");
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectRwInitEx(&pGVMM->UsedLock, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "GVMM-UsedLock");
        if (RT_SUCCESS(rc))
        {
            pGVMM->u32Magic = GVMM_MAGIC;
            pGVMM->iUsedHead = 0;
            pGVMM->iFreeHead = 1;

            /* the nil handle */
            pGVMM->aHandles[0].iSelf = 0;
            pGVMM->aHandles[0].iNext = 0;

            /* the tail */
            unsigned i = RT_ELEMENTS(pGVMM->aHandles) - 1;
            pGVMM->aHandles[i].iSelf = i;
            pGVMM->aHandles[i].iNext = 0; /* nil */

            /* the rest */
            while (i-- > 1)
            {
                pGVMM->aHandles[i].iSelf = i;
                pGVMM->aHandles[i].iNext = i + 1;
            }

            /* The default configuration values. */
            uint32_t cNsResolution = RTSemEventMultiGetResolution();
            pGVMM->cEMTsMeansCompany     = 1;                           /** @todo should be adjusted to relative to the cpu count or something... */
            if (cNsResolution >= 5*RT_NS_100US)
            {
                pGVMM->nsMinSleepAlone   = 750000 /* ns (0.750 ms) */;  /** @todo this should be adjusted to be 75% (or something) of the scheduler granularity... */
                pGVMM->nsMinSleepCompany =  15000 /* ns (0.015 ms) */;
                pGVMM->nsEarlyWakeUp1    =  25000 /* ns (0.025 ms) */;
                pGVMM->nsEarlyWakeUp2    =  50000 /* ns (0.050 ms) */;
            }
            else if (cNsResolution > RT_NS_100US)
            {
                pGVMM->nsMinSleepAlone   = cNsResolution / 2;
                pGVMM->nsMinSleepCompany = cNsResolution / 4;
                pGVMM->nsEarlyWakeUp1    = 0;
                pGVMM->nsEarlyWakeUp2    = 0;
            }
            else
            {
                pGVMM->nsMinSleepAlone   = 2000;
                pGVMM->nsMinSleepCompany = 2000;
                pGVMM->nsEarlyWakeUp1    = 0;
                pGVMM->nsEarlyWakeUp2    = 0;
            }
#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
            pGVMM->nsMinSleepWithHrTimer = 5000 /* ns (0.005 ms) */;
#endif
            pGVMM->fDoEarlyWakeUps = pGVMM->nsEarlyWakeUp1 > 0 && pGVMM->nsEarlyWakeUp2 > 0;

            /* The host CPU data. */
            pGVMM->cHostCpus = cHostCpus;
            uint32_t    iCpu = cHostCpus;
            RTCPUSET    PossibleSet;
            RTMpGetSet(&PossibleSet);
            while (iCpu-- > 0)
            {
                pGVMM->aHostCpus[iCpu].idxCpuSet        = iCpu;
#ifdef GVMM_SCHED_WITH_PPT
                pGVMM->aHostCpus[iCpu].Ppt.pTimer       = NULL;
                pGVMM->aHostCpus[iCpu].Ppt.hSpinlock    = NIL_RTSPINLOCK;
                pGVMM->aHostCpus[iCpu].Ppt.uMinHz       = 5; /** @todo Add some API which figures this one out. (not *that* important) */
                pGVMM->aHostCpus[iCpu].Ppt.cTicksHistoriziationInterval = 1;
                //pGVMM->aHostCpus[iCpu].Ppt.iTickHistorization           = 0;
                //pGVMM->aHostCpus[iCpu].Ppt.cNsInterval  = 0;
                //pGVMM->aHostCpus[iCpu].Ppt.uTimerHz     = 0;
                //pGVMM->aHostCpus[iCpu].Ppt.uDesiredHz   = 0;
                //pGVMM->aHostCpus[iCpu].Ppt.fStarted     = false;
                //pGVMM->aHostCpus[iCpu].Ppt.fStarting    = false;
                //pGVMM->aHostCpus[iCpu].Ppt.iHzHistory   = 0;
                //pGVMM->aHostCpus[iCpu].Ppt.aHzHistory   = {0};
#endif

                if (RTCpuSetIsMember(&PossibleSet, iCpu))
                {
                    pGVMM->aHostCpus[iCpu].idCpu        = RTMpCpuIdFromSetIndex(iCpu);
                    pGVMM->aHostCpus[iCpu].u32Magic     = GVMMHOSTCPU_MAGIC;

#ifdef GVMM_SCHED_WITH_PPT
                    rc = RTTimerCreateEx(&pGVMM->aHostCpus[iCpu].Ppt.pTimer,
                                         50*1000*1000 /* whatever */,
                                         RTTIMER_FLAGS_CPU(iCpu) | RTTIMER_FLAGS_HIGH_RES,
                                         gvmmR0SchedPeriodicPreemptionTimerCallback,
                                         &pGVMM->aHostCpus[iCpu]);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTSpinlockCreate(&pGVMM->aHostCpus[iCpu].Ppt.hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "GVMM/CPU");
                        if (RT_FAILURE(rc))
                            LogRel(("GVMMR0Init: RTSpinlockCreate failed for #%u (%d)\n", iCpu, rc));
                    }
                    else
                        LogRel(("GVMMR0Init: RTTimerCreateEx failed for #%u (%d)\n", iCpu, rc));
                    if (RT_FAILURE(rc))
                    {
                        while (iCpu < cHostCpus)
                        {
                            RTTimerDestroy(pGVMM->aHostCpus[iCpu].Ppt.pTimer);
                            RTSpinlockDestroy(pGVMM->aHostCpus[iCpu].Ppt.hSpinlock);
                            pGVMM->aHostCpus[iCpu].Ppt.hSpinlock = NIL_RTSPINLOCK;
                            iCpu++;
                        }
                        break;
                    }
#endif
                }
                else
                {
                    pGVMM->aHostCpus[iCpu].idCpu        = NIL_RTCPUID;
                    pGVMM->aHostCpus[iCpu].u32Magic     = 0;
                }
            }
            if (RT_SUCCESS(rc))
            {
                g_pGVMM = pGVMM;
                LogFlow(("GVMMR0Init: pGVMM=%p cHostCpus=%u\n", pGVMM, cHostCpus));
                return VINF_SUCCESS;
            }

            /* bail out. */
            RTCritSectRwDelete(&pGVMM->UsedLock);
        }
        else
            LogRel(("GVMMR0Init: RTCritSectRwInitEx failed (%d)\n", rc));
        RTCritSectDelete(&pGVMM->CreateDestroyLock);
    }
    else
        LogRel(("GVMMR0Init: RTCritSectInitEx failed (%d)\n", rc));

    RTMemFree(pGVMM);
    return rc;
}


/**
 * Terminates the GVM.
 *
 * This is called while owning the loader semaphore (see supdrvLdrFree()).
 * And unless something is wrong, there should be absolutely no VMs
 * registered at this point.
 */
GVMMR0DECL(void) GVMMR0Term(void)
{
    LogFlow(("GVMMR0Term:\n"));

    PGVMM pGVMM = g_pGVMM;
    g_pGVMM = NULL;
    if (RT_UNLIKELY(!RT_VALID_PTR(pGVMM)))
    {
        SUPR0Printf("GVMMR0Term: pGVMM=%RKv\n", pGVMM);
        return;
    }

    /*
     * First of all, stop all active timers.
     */
    uint32_t cActiveTimers = 0;
    uint32_t iCpu = pGVMM->cHostCpus;
    while (iCpu-- > 0)
    {
        ASMAtomicWriteU32(&pGVMM->aHostCpus[iCpu].u32Magic, ~GVMMHOSTCPU_MAGIC);
#ifdef GVMM_SCHED_WITH_PPT
        if (    pGVMM->aHostCpus[iCpu].Ppt.pTimer != NULL
            &&  RT_SUCCESS(RTTimerStop(pGVMM->aHostCpus[iCpu].Ppt.pTimer)))
            cActiveTimers++;
#endif
    }
    if (cActiveTimers)
        RTThreadSleep(1); /* fudge */

    /*
     * Invalidate the and free resources.
     */
    pGVMM->u32Magic = ~GVMM_MAGIC;
    RTCritSectRwDelete(&pGVMM->UsedLock);
    RTCritSectDelete(&pGVMM->CreateDestroyLock);

    pGVMM->iFreeHead = 0;
    if (pGVMM->iUsedHead)
    {
        SUPR0Printf("GVMMR0Term: iUsedHead=%#x! (cVMs=%#x cEMTs=%#x)\n", pGVMM->iUsedHead, pGVMM->cVMs, pGVMM->cEMTs);
        pGVMM->iUsedHead = 0;
    }

#ifdef GVMM_SCHED_WITH_PPT
    iCpu = pGVMM->cHostCpus;
    while (iCpu-- > 0)
    {
        RTTimerDestroy(pGVMM->aHostCpus[iCpu].Ppt.pTimer);
        pGVMM->aHostCpus[iCpu].Ppt.pTimer = NULL;
        RTSpinlockDestroy(pGVMM->aHostCpus[iCpu].Ppt.hSpinlock);
        pGVMM->aHostCpus[iCpu].Ppt.hSpinlock = NIL_RTSPINLOCK;
    }
#endif

    RTMemFree(pGVMM);
}


/**
 * A quick hack for setting global config values.
 *
 * @returns VBox status code.
 *
 * @param   pSession    The session handle. Used for authentication.
 * @param   pszName     The variable name.
 * @param   u64Value    The new value.
 */
GVMMR0DECL(int) GVMMR0SetConfig(PSUPDRVSESSION pSession, const char *pszName, uint64_t u64Value)
{
    /*
     * Validate input.
     */
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);
    AssertPtrReturn(pSession, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);

    /*
     * String switch time!
     */
    if (strncmp(pszName, RT_STR_TUPLE("/GVMM/")))
        return VERR_CFGM_VALUE_NOT_FOUND; /* borrow status codes from CFGM... */
    int rc = VINF_SUCCESS;
    pszName += sizeof("/GVMM/") - 1;
    if (!strcmp(pszName, "cEMTsMeansCompany"))
    {
        if (u64Value <= UINT32_MAX)
            pGVMM->cEMTsMeansCompany = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else if (!strcmp(pszName, "MinSleepAlone"))
    {
        if (u64Value <= RT_NS_100MS)
            pGVMM->nsMinSleepAlone = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else if (!strcmp(pszName, "MinSleepCompany"))
    {
        if (u64Value <= RT_NS_100MS)
            pGVMM->nsMinSleepCompany = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
    else if (!strcmp(pszName, "MinSleepWithHrWakeUp"))
    {
        if (u64Value <= RT_NS_100MS)
            pGVMM->nsMinSleepWithHrTimer = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
#endif
    else if (!strcmp(pszName, "EarlyWakeUp1"))
    {
        if (u64Value <= RT_NS_100MS)
        {
            pGVMM->nsEarlyWakeUp1 = u64Value;
            pGVMM->fDoEarlyWakeUps = pGVMM->nsEarlyWakeUp1 > 0 && pGVMM->nsEarlyWakeUp2 > 0;
        }
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else if (!strcmp(pszName, "EarlyWakeUp2"))
    {
        if (u64Value <= RT_NS_100MS)
        {
            pGVMM->nsEarlyWakeUp2 = u64Value;
            pGVMM->fDoEarlyWakeUps = pGVMM->nsEarlyWakeUp1 > 0 && pGVMM->nsEarlyWakeUp2 > 0;
        }
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else
        rc = VERR_CFGM_VALUE_NOT_FOUND;
    return rc;
}


/**
 * A quick hack for getting global config values.
 *
 * @returns VBox status code.
 *
 * @param   pSession    The session handle. Used for authentication.
 * @param   pszName     The variable name.
 * @param   pu64Value   Where to return the value.
 */
GVMMR0DECL(int) GVMMR0QueryConfig(PSUPDRVSESSION pSession, const char *pszName, uint64_t *pu64Value)
{
    /*
     * Validate input.
     */
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);
    AssertPtrReturn(pSession, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pu64Value, VERR_INVALID_POINTER);

    /*
     * String switch time!
     */
    if (strncmp(pszName, RT_STR_TUPLE("/GVMM/")))
        return VERR_CFGM_VALUE_NOT_FOUND; /* borrow status codes from CFGM... */
    int rc = VINF_SUCCESS;
    pszName += sizeof("/GVMM/") - 1;
    if (!strcmp(pszName, "cEMTsMeansCompany"))
        *pu64Value = pGVMM->cEMTsMeansCompany;
    else if (!strcmp(pszName, "MinSleepAlone"))
        *pu64Value = pGVMM->nsMinSleepAlone;
    else if (!strcmp(pszName, "MinSleepCompany"))
        *pu64Value = pGVMM->nsMinSleepCompany;
#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
    else if (!strcmp(pszName, "MinSleepWithHrWakeUp"))
        *pu64Value = pGVMM->nsMinSleepWithHrTimer;
#endif
    else if (!strcmp(pszName, "EarlyWakeUp1"))
        *pu64Value = pGVMM->nsEarlyWakeUp1;
    else if (!strcmp(pszName, "EarlyWakeUp2"))
        *pu64Value = pGVMM->nsEarlyWakeUp2;
    else
        rc = VERR_CFGM_VALUE_NOT_FOUND;
    return rc;
}


/**
 * Acquire the 'used' lock in shared mode.
 *
 * This prevents destruction of the VM while we're in ring-0.
 *
 * @returns IPRT status code, see RTSemFastMutexRequest.
 * @param   a_pGVMM     The GVMM instance data.
 * @sa      GVMMR0_USED_SHARED_UNLOCK, GVMMR0_USED_EXCLUSIVE_LOCK
 */
#define GVMMR0_USED_SHARED_LOCK(a_pGVMM)        RTCritSectRwEnterShared(&(a_pGVMM)->UsedLock)

/**
 * Release the 'used' lock in when owning it in shared mode.
 *
 * @returns IPRT status code, see RTSemFastMutexRequest.
 * @param   a_pGVMM     The GVMM instance data.
 * @sa      GVMMR0_USED_SHARED_LOCK
 */
#define GVMMR0_USED_SHARED_UNLOCK(a_pGVMM)      RTCritSectRwLeaveShared(&(a_pGVMM)->UsedLock)

/**
 * Acquire the 'used' lock in exclusive mode.
 *
 * Only use this function when making changes to the used list.
 *
 * @returns IPRT status code, see RTSemFastMutexRequest.
 * @param   a_pGVMM     The GVMM instance data.
 * @sa      GVMMR0_USED_EXCLUSIVE_UNLOCK
 */
#define GVMMR0_USED_EXCLUSIVE_LOCK(a_pGVMM)     RTCritSectRwEnterExcl(&(a_pGVMM)->UsedLock)

/**
 * Release the 'used' lock when owning it in exclusive mode.
 *
 * @returns IPRT status code, see RTSemFastMutexRelease.
 * @param   a_pGVMM     The GVMM instance data.
 * @sa      GVMMR0_USED_EXCLUSIVE_LOCK, GVMMR0_USED_SHARED_UNLOCK
 */
#define GVMMR0_USED_EXCLUSIVE_UNLOCK(a_pGVMM)   RTCritSectRwLeaveExcl(&(a_pGVMM)->UsedLock)


/**
 * Try acquire the 'create & destroy' lock.
 *
 * @returns IPRT status code, see RTSemFastMutexRequest.
 * @param   pGVMM   The GVMM instance data.
 */
DECLINLINE(int) gvmmR0CreateDestroyLock(PGVMM pGVMM)
{
    LogFlow(("++gvmmR0CreateDestroyLock(%p)\n", pGVMM));
    int rc = RTCritSectEnter(&pGVMM->CreateDestroyLock);
    LogFlow(("gvmmR0CreateDestroyLock(%p)->%Rrc\n", pGVMM, rc));
    return rc;
}


/**
 * Release the 'create & destroy' lock.
 *
 * @returns IPRT status code, see RTSemFastMutexRequest.
 * @param   pGVMM   The GVMM instance data.
 */
DECLINLINE(int) gvmmR0CreateDestroyUnlock(PGVMM pGVMM)
{
    LogFlow(("--gvmmR0CreateDestroyUnlock(%p)\n", pGVMM));
    int rc = RTCritSectLeave(&pGVMM->CreateDestroyLock);
    AssertRC(rc);
    return rc;
}


/**
 * Request wrapper for the GVMMR0CreateVM API.
 *
 * @returns VBox status code.
 * @param   pReq        The request buffer.
 * @param   pSession    The session handle. The VM will be associated with this.
 */
GVMMR0DECL(int) GVMMR0CreateVMReq(PGVMMCREATEVMREQ pReq, PSUPDRVSESSION pSession)
{
    /*
     * Validate the request.
     */
    if (!RT_VALID_PTR(pReq))
        return VERR_INVALID_POINTER;
    if (pReq->Hdr.cbReq != sizeof(*pReq))
        return VERR_INVALID_PARAMETER;
    if (pReq->pSession != pSession)
        return VERR_INVALID_POINTER;

    /*
     * Execute it.
     */
    PGVM pGVM;
    pReq->pVMR0 = NULL;
    pReq->pVMR3 = NIL_RTR3PTR;
    int rc = GVMMR0CreateVM(pSession, pReq->cCpus, &pGVM);
    if (RT_SUCCESS(rc))
    {
        pReq->pVMR0 = pGVM; /** @todo don't expose this to ring-3, use a unique random number instead. */
        pReq->pVMR3 = pGVM->pVMR3;
    }
    return rc;
}


/**
 * Allocates the VM structure and registers it with GVM.
 *
 * The caller will become the VM owner and there by the EMT.
 *
 * @returns VBox status code.
 * @param   pSession    The support driver session.
 * @param   cCpus       Number of virtual CPUs for the new VM.
 * @param   ppGVM       Where to store the pointer to the VM structure.
 *
 * @thread  EMT.
 */
GVMMR0DECL(int) GVMMR0CreateVM(PSUPDRVSESSION pSession, uint32_t cCpus, PGVM *ppGVM)
{
    LogFlow(("GVMMR0CreateVM: pSession=%p\n", pSession));
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);

    AssertPtrReturn(ppGVM, VERR_INVALID_POINTER);
    *ppGVM = NULL;

    if (    cCpus == 0
        ||  cCpus > VMM_MAX_CPU_COUNT)
        return VERR_INVALID_PARAMETER;

    RTNATIVETHREAD hEMT0 = RTThreadNativeSelf();
    AssertReturn(hEMT0 != NIL_RTNATIVETHREAD, VERR_GVMM_BROKEN_IPRT);
    RTPROCESS      ProcId = RTProcSelf();
    AssertReturn(ProcId != NIL_RTPROCESS, VERR_GVMM_BROKEN_IPRT);

    /*
     * The whole allocation process is protected by the lock.
     */
    int rc = gvmmR0CreateDestroyLock(pGVMM);
    AssertRCReturn(rc, rc);

    /*
     * Only one VM per session.
     */
    if (SUPR0GetSessionVM(pSession) != NULL)
    {
        gvmmR0CreateDestroyUnlock(pGVMM);
        SUPR0Printf("GVMMR0CreateVM: The session %p already got a VM: %p\n", pSession, SUPR0GetSessionVM(pSession));
        return VERR_ALREADY_EXISTS;
    }

    /*
     * Allocate a handle first so we don't waste resources unnecessarily.
     */
    uint16_t iHandle = pGVMM->iFreeHead;
    if (iHandle)
    {
        PGVMHANDLE pHandle = &pGVMM->aHandles[iHandle];

        /* consistency checks, a bit paranoid as always. */
        if (    !pHandle->pGVM
            &&  !pHandle->pvObj
            &&  pHandle->iSelf == iHandle)
        {
            pHandle->pvObj = SUPR0ObjRegister(pSession, SUPDRVOBJTYPE_VM, gvmmR0HandleObjDestructor, pGVMM, pHandle);
            if (pHandle->pvObj)
            {
                /*
                 * Move the handle from the free to used list and perform permission checks.
                 */
                rc = GVMMR0_USED_EXCLUSIVE_LOCK(pGVMM);
                AssertRC(rc);

                pGVMM->iFreeHead = pHandle->iNext;
                pHandle->iNext = pGVMM->iUsedHead;
                pGVMM->iUsedHead = iHandle;
                pGVMM->cVMs++;

                pHandle->pGVM     = NULL;
                pHandle->pSession = pSession;
                pHandle->hEMT0    = NIL_RTNATIVETHREAD;
                pHandle->ProcId   = NIL_RTPROCESS;

                GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);

                rc = SUPR0ObjVerifyAccess(pHandle->pvObj, pSession, NULL);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Allocate memory for the VM structure (combined VM + GVM).
                     */
                    const uint32_t  cbVM      = RT_UOFFSETOF_DYN(GVM, aCpus[cCpus]);
                    const uint32_t  cPages    = RT_ALIGN_32(cbVM, HOST_PAGE_SIZE) >> HOST_PAGE_SHIFT;
                    RTR0MEMOBJ      hVMMemObj = NIL_RTR0MEMOBJ;
                    rc = RTR0MemObjAllocPage(&hVMMemObj, cPages << HOST_PAGE_SHIFT, false /* fExecutable */);
                    if (RT_SUCCESS(rc))
                    {
                        PGVM pGVM = (PGVM)RTR0MemObjAddress(hVMMemObj);
                        AssertPtr(pGVM);

                        /*
                         * Initialise the structure.
                         */
                        RT_BZERO(pGVM, cPages << HOST_PAGE_SHIFT);
                        gvmmR0InitPerVMData(pGVM, iHandle, cCpus, pSession);
                        pGVM->gvmm.s.VMMemObj  = hVMMemObj;
                        rc = GMMR0InitPerVMData(pGVM);
                        int rc2 = PGMR0InitPerVMData(pGVM, hVMMemObj);
                        int rc3 = VMMR0InitPerVMData(pGVM);
                        CPUMR0InitPerVMData(pGVM);
                        DBGFR0InitPerVMData(pGVM);
                        PDMR0InitPerVMData(pGVM);
                        IOMR0InitPerVMData(pGVM);
                        TMR0InitPerVMData(pGVM);
                        if (RT_SUCCESS(rc) && RT_SUCCESS(rc2) && RT_SUCCESS(rc3))
                        {
                            /*
                             * Allocate page array.
                             * This currently have to be made available to ring-3, but this is should change eventually.
                             */
                            rc = RTR0MemObjAllocPage(&pGVM->gvmm.s.VMPagesMemObj, cPages * sizeof(SUPPAGE), false /* fExecutable */);
                            if (RT_SUCCESS(rc))
                            {
                                PSUPPAGE paPages = (PSUPPAGE)RTR0MemObjAddress(pGVM->gvmm.s.VMPagesMemObj); AssertPtr(paPages);
                                for (uint32_t iPage = 0; iPage < cPages; iPage++)
                                {
                                    paPages[iPage].uReserved = 0;
                                    paPages[iPage].Phys = RTR0MemObjGetPagePhysAddr(pGVM->gvmm.s.VMMemObj, iPage);
                                    Assert(paPages[iPage].Phys != NIL_RTHCPHYS);
                                }

                                /*
                                 * Map the page array, VM and VMCPU structures into ring-3.
                                 */
                                AssertCompileSizeAlignment(VM, HOST_PAGE_SIZE);
                                rc = RTR0MemObjMapUserEx(&pGVM->gvmm.s.VMMapObj, pGVM->gvmm.s.VMMemObj, (RTR3PTR)-1, 0,
                                                         RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS,
                                                         0 /*offSub*/, sizeof(VM));
                                for (VMCPUID i = 0; i < cCpus && RT_SUCCESS(rc); i++)
                                {
                                    AssertCompileSizeAlignment(VMCPU, HOST_PAGE_SIZE);
                                    rc = RTR0MemObjMapUserEx(&pGVM->aCpus[i].gvmm.s.VMCpuMapObj, pGVM->gvmm.s.VMMemObj,
                                                             (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS,
                                                             RT_UOFFSETOF_DYN(GVM, aCpus[i]), sizeof(VMCPU));
                                }
                                if (RT_SUCCESS(rc))
                                    rc = RTR0MemObjMapUser(&pGVM->gvmm.s.VMPagesMapObj, pGVM->gvmm.s.VMPagesMemObj, (RTR3PTR)-1,
                                                           0 /* uAlignment */, RTMEM_PROT_READ | RTMEM_PROT_WRITE,
                                                           NIL_RTR0PROCESS);
                                if (RT_SUCCESS(rc))
                                {
                                    /*
                                     * Initialize all the VM pointers.
                                     */
                                    PVMR3 pVMR3 = RTR0MemObjAddressR3(pGVM->gvmm.s.VMMapObj);
                                    AssertMsg(RTR0MemUserIsValidAddr(pVMR3) && pVMR3 != NIL_RTR3PTR, ("%p\n", pVMR3));

                                    for (VMCPUID i = 0; i < cCpus; i++)
                                    {
                                        pGVM->aCpus[i].pVMR0 = pGVM;
                                        pGVM->aCpus[i].pVMR3 = pVMR3;
                                        pGVM->apCpusR3[i] = RTR0MemObjAddressR3(pGVM->aCpus[i].gvmm.s.VMCpuMapObj);
                                        pGVM->aCpus[i].pVCpuR3 = pGVM->apCpusR3[i];
                                        pGVM->apCpusR0[i] = &pGVM->aCpus[i];
                                        AssertMsg(RTR0MemUserIsValidAddr(pGVM->apCpusR3[i]) && pGVM->apCpusR3[i] != NIL_RTR3PTR,
                                                  ("apCpusR3[%u]=%p\n", i, pGVM->apCpusR3[i]));
                                    }

                                    pGVM->paVMPagesR3 = RTR0MemObjAddressR3(pGVM->gvmm.s.VMPagesMapObj);
                                    AssertMsg(RTR0MemUserIsValidAddr(pGVM->paVMPagesR3) && pGVM->paVMPagesR3 != NIL_RTR3PTR,
                                              ("%p\n", pGVM->paVMPagesR3));

#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
                                    /*
                                     * Create the high resolution wake-up timer for EMT 0, ignore failures.
                                     */
                                    if (RTTimerCanDoHighResolution())
                                    {
                                        int rc4 = RTTimerCreateEx(&pGVM->aCpus[0].gvmm.s.hHrWakeUpTimer,
                                                                  0 /*one-shot, no interval*/,
                                                                  RTTIMER_FLAGS_HIGH_RES, gvmmR0EmtWakeUpTimerCallback,
                                                                  &pGVM->aCpus[0]);
                                        if (RT_FAILURE(rc4))
                                            pGVM->aCpus[0].gvmm.s.hHrWakeUpTimer = NULL;
                                    }
#endif

                                    /*
                                     * Complete the handle - take the UsedLock sem just to be careful.
                                     */
                                    rc = GVMMR0_USED_EXCLUSIVE_LOCK(pGVMM);
                                    AssertRC(rc);

                                    pHandle->pGVM                       = pGVM;
                                    pHandle->hEMT0                      = hEMT0;
                                    pHandle->ProcId                     = ProcId;
                                    pGVM->pVMR3                         = pVMR3;
                                    pGVM->pVMR3Unsafe                   = pVMR3;
                                    pGVM->aCpus[0].hEMT                 = hEMT0;
                                    pGVM->aCpus[0].hNativeThreadR0      = hEMT0;
                                    pGVM->aCpus[0].cEmtHashCollisions   = 0;
                                    uint32_t const idxHash = GVMM_EMT_HASH_1(hEMT0);
                                    pGVM->aCpus[0].gvmm.s.idxEmtHash    = (uint16_t)idxHash;
                                    pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt = hEMT0;
                                    pGVM->gvmm.s.aEmtHash[idxHash].idVCpu     = 0;
                                    pGVMM->cEMTs += cCpus;

                                    /* Associate it with the session and create the context hook for EMT0. */
                                    rc = SUPR0SetSessionVM(pSession, pGVM, pGVM);
                                    if (RT_SUCCESS(rc))
                                    {
                                        rc = VMMR0ThreadCtxHookCreateForEmt(&pGVM->aCpus[0]);
                                        if (RT_SUCCESS(rc))
                                        {
                                            /*
                                             * Done!
                                             */
                                            VBOXVMM_R0_GVMM_VM_CREATED(pGVM, pGVM, ProcId, (void *)hEMT0, cCpus);

                                            GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);
                                            gvmmR0CreateDestroyUnlock(pGVMM);

                                            CPUMR0RegisterVCpuThread(&pGVM->aCpus[0]);

                                            *ppGVM = pGVM;
                                            Log(("GVMMR0CreateVM: pVMR3=%p pGVM=%p hGVM=%d\n", pVMR3, pGVM, iHandle));
                                            return VINF_SUCCESS;
                                        }

                                        SUPR0SetSessionVM(pSession, NULL, NULL);
                                    }
                                    GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);
                                }

                                /* Cleanup mappings. */
                                if (pGVM->gvmm.s.VMMapObj != NIL_RTR0MEMOBJ)
                                {
                                    RTR0MemObjFree(pGVM->gvmm.s.VMMapObj, false /* fFreeMappings */);
                                    pGVM->gvmm.s.VMMapObj = NIL_RTR0MEMOBJ;
                                }
                                for (VMCPUID i = 0; i < cCpus; i++)
                                    if (pGVM->aCpus[i].gvmm.s.VMCpuMapObj != NIL_RTR0MEMOBJ)
                                    {
                                        RTR0MemObjFree(pGVM->aCpus[i].gvmm.s.VMCpuMapObj, false /* fFreeMappings */);
                                        pGVM->aCpus[i].gvmm.s.VMCpuMapObj = NIL_RTR0MEMOBJ;
                                    }
                                if (pGVM->gvmm.s.VMPagesMapObj != NIL_RTR0MEMOBJ)
                                {
                                    RTR0MemObjFree(pGVM->gvmm.s.VMPagesMapObj, false /* fFreeMappings */);
                                    pGVM->gvmm.s.VMPagesMapObj = NIL_RTR0MEMOBJ;
                                }
                            }
                        }
                        else
                        {
                            if (RT_SUCCESS_NP(rc))
                                rc = rc2;
                            if (RT_SUCCESS_NP(rc))
                                rc = rc3;
                            AssertStmt(RT_FAILURE_NP(rc), rc = VERR_IPE_UNEXPECTED_STATUS);
                        }
                    }
                }
                /* else: The user wasn't permitted to create this VM. */

                /*
                 * The handle will be freed by gvmmR0HandleObjDestructor as we release the
                 * object reference here. A little extra mess because of non-recursive lock.
                 */
                void *pvObj = pHandle->pvObj;
                pHandle->pvObj = NULL;
                gvmmR0CreateDestroyUnlock(pGVMM);

                SUPR0ObjRelease(pvObj, pSession);

                SUPR0Printf("GVMMR0CreateVM: failed, rc=%Rrc\n", rc);
                return rc;
            }

            rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_GVMM_IPE_1;
    }
    else
        rc = VERR_GVM_TOO_MANY_VMS;

    gvmmR0CreateDestroyUnlock(pGVMM);
    return rc;
}


/**
 * Initializes the per VM data belonging to GVMM.
 *
 * @param   pGVM        Pointer to the global VM structure.
 * @param   hSelf       The handle.
 * @param   cCpus       The CPU count.
 * @param   pSession    The session this VM is associated with.
 */
static void gvmmR0InitPerVMData(PGVM pGVM, int16_t hSelf, VMCPUID cCpus, PSUPDRVSESSION pSession)
{
    AssertCompile(RT_SIZEOFMEMB(GVM,gvmm.s) <= RT_SIZEOFMEMB(GVM,gvmm.padding));
    AssertCompile(RT_SIZEOFMEMB(GVMCPU,gvmm.s) <= RT_SIZEOFMEMB(GVMCPU,gvmm.padding));
    AssertCompileMemberAlignment(VM, cpum, 64);
    AssertCompileMemberAlignment(VM, tm, 64);

    /* GVM: */
    pGVM->u32Magic         = GVM_MAGIC;
    pGVM->hSelf            = hSelf;
    pGVM->cCpus            = cCpus;
    pGVM->pSession         = pSession;
    pGVM->pSelf            = pGVM;

    /* VM: */
    pGVM->enmVMState       = VMSTATE_CREATING;
    pGVM->hSelfUnsafe      = hSelf;
    pGVM->pSessionUnsafe   = pSession;
    pGVM->pVMR0ForCall     = pGVM;
    pGVM->cCpusUnsafe      = cCpus;
    pGVM->uCpuExecutionCap = 100; /* default is no cap. */
    pGVM->uStructVersion   = 1;
    pGVM->cbSelf           = sizeof(VM);
    pGVM->cbVCpu           = sizeof(VMCPU);

    /* GVMM: */
    pGVM->gvmm.s.VMMemObj       = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.VMMapObj       = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.VMPagesMemObj  = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.VMPagesMapObj  = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.fDoneVMMR0Init = false;
    pGVM->gvmm.s.fDoneVMMR0Term = false;

    for (size_t i = 0; i < RT_ELEMENTS(pGVM->gvmm.s.aWorkerThreads); i++)
    {
        pGVM->gvmm.s.aWorkerThreads[i].hNativeThread   = NIL_RTNATIVETHREAD;
        pGVM->gvmm.s.aWorkerThreads[i].hNativeThreadR3 = NIL_RTNATIVETHREAD;
    }
    pGVM->gvmm.s.aWorkerThreads[0].hNativeThread = GVMM_RTNATIVETHREAD_DESTROYED; /* invalid entry */

    for (size_t i = 0; i < RT_ELEMENTS(pGVM->gvmm.s.aEmtHash); i++)
    {
        pGVM->gvmm.s.aEmtHash[i].hNativeEmt = NIL_RTNATIVETHREAD;
        pGVM->gvmm.s.aEmtHash[i].idVCpu     = NIL_VMCPUID;
    }

    /*
     * Per virtual CPU.
     */
    for (VMCPUID i = 0; i < pGVM->cCpus; i++)
    {
        pGVM->aCpus[i].idCpu                 = i;
        pGVM->aCpus[i].idCpuUnsafe           = i;
        pGVM->aCpus[i].gvmm.s.HaltEventMulti = NIL_RTSEMEVENTMULTI;
        pGVM->aCpus[i].gvmm.s.VMCpuMapObj    = NIL_RTR0MEMOBJ;
        pGVM->aCpus[i].gvmm.s.idxEmtHash     = UINT16_MAX;
        pGVM->aCpus[i].gvmm.s.hHrWakeUpTimer = NULL;
        pGVM->aCpus[i].hEMT                  = NIL_RTNATIVETHREAD;
        pGVM->aCpus[i].pGVM                  = pGVM;
        pGVM->aCpus[i].idHostCpu             = NIL_RTCPUID;
        pGVM->aCpus[i].iHostCpuSet           = UINT32_MAX;
        pGVM->aCpus[i].hNativeThread         = NIL_RTNATIVETHREAD;
        pGVM->aCpus[i].hNativeThreadR0       = NIL_RTNATIVETHREAD;
        pGVM->aCpus[i].enmState              = VMCPUSTATE_STOPPED;
        pGVM->aCpus[i].pVCpuR0ForVtg         = &pGVM->aCpus[i];
    }
}


/**
 * Does the VM initialization.
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 */
GVMMR0DECL(int) GVMMR0InitVM(PGVM pGVM)
{
    LogFlow(("GVMMR0InitVM: pGVM=%p\n", pGVM));

    int rc = VERR_INTERNAL_ERROR_3;
    if (   !pGVM->gvmm.s.fDoneVMMR0Init
        && pGVM->aCpus[0].gvmm.s.HaltEventMulti == NIL_RTSEMEVENTMULTI)
    {
        for (VMCPUID i = 0; i < pGVM->cCpus; i++)
        {
            rc = RTSemEventMultiCreate(&pGVM->aCpus[i].gvmm.s.HaltEventMulti);
            if (RT_FAILURE(rc))
            {
                pGVM->aCpus[i].gvmm.s.HaltEventMulti = NIL_RTSEMEVENTMULTI;
                break;
            }
        }
    }
    else
        rc = VERR_WRONG_ORDER;

    LogFlow(("GVMMR0InitVM: returns %Rrc\n", rc));
    return rc;
}


/**
 * Indicates that we're done with the ring-0 initialization
 * of the VM.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @thread  EMT(0)
 */
GVMMR0DECL(void) GVMMR0DoneInitVM(PGVM pGVM)
{
    /* Set the indicator. */
    pGVM->gvmm.s.fDoneVMMR0Init = true;
}


/**
 * Indicates that we're doing the ring-0 termination of the VM.
 *
 * @returns true if termination hasn't been done already, false if it has.
 * @param   pGVM        Pointer to the global VM structure. Optional.
 * @thread  EMT(0) or session cleanup thread.
 */
GVMMR0DECL(bool) GVMMR0DoingTermVM(PGVM pGVM)
{
    /* Validate the VM structure, state and handle. */
    AssertPtrReturn(pGVM, false);

    /* Set the indicator. */
    if (pGVM->gvmm.s.fDoneVMMR0Term)
        return false;
    pGVM->gvmm.s.fDoneVMMR0Term = true;
    return true;
}


/**
 * Destroys the VM, freeing all associated resources (the ring-0 ones anyway).
 *
 * This is call from the vmR3DestroyFinalBit and from a error path in VMR3Create,
 * and the caller is not the EMT thread, unfortunately. For security reasons, it
 * would've been nice if the caller was actually the EMT thread or that we somehow
 * could've associated the calling thread with the VM up front.
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 *
 * @thread  EMT(0) if it's associated with the VM, otherwise any thread.
 */
GVMMR0DECL(int) GVMMR0DestroyVM(PGVM pGVM)
{
    LogFlow(("GVMMR0DestroyVM: pGVM=%p\n", pGVM));
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);

    /*
     * Validate the VM structure, state and caller.
     */
    AssertPtrReturn(pGVM, VERR_INVALID_POINTER);
    AssertReturn(!((uintptr_t)pGVM & HOST_PAGE_OFFSET_MASK), VERR_INVALID_POINTER);
    AssertMsgReturn(pGVM->enmVMState >= VMSTATE_CREATING && pGVM->enmVMState <= VMSTATE_TERMINATED, ("%d\n", pGVM->enmVMState),
                    VERR_WRONG_ORDER);

    uint32_t        hGVM = pGVM->hSelf;
    ASMCompilerBarrier();
    AssertReturn(hGVM != NIL_GVM_HANDLE, VERR_INVALID_VM_HANDLE);
    AssertReturn(hGVM < RT_ELEMENTS(pGVMM->aHandles), VERR_INVALID_VM_HANDLE);

    PGVMHANDLE      pHandle = &pGVMM->aHandles[hGVM];
    AssertReturn(pHandle->pGVM == pGVM, VERR_NOT_OWNER);

    RTPROCESS       ProcId = RTProcSelf();
    RTNATIVETHREAD  hSelf  = RTThreadNativeSelf();
    AssertReturn(   (   pHandle->hEMT0  == hSelf
                     && pHandle->ProcId == ProcId)
                 || pHandle->hEMT0 == NIL_RTNATIVETHREAD, VERR_NOT_OWNER);

    /*
     * Lookup the handle and destroy the object.
     * Since the lock isn't recursive and we'll have to leave it before dereferencing the
     * object, we take some precautions against racing callers just in case...
     */
    int rc = gvmmR0CreateDestroyLock(pGVMM);
    AssertRC(rc);

    /* Be careful here because we might theoretically be racing someone else cleaning up. */
    if (   pHandle->pGVM == pGVM
        && (   (   pHandle->hEMT0  == hSelf
                && pHandle->ProcId == ProcId)
            || pHandle->hEMT0 == NIL_RTNATIVETHREAD)
        && RT_VALID_PTR(pHandle->pvObj)
        && RT_VALID_PTR(pHandle->pSession)
        && RT_VALID_PTR(pHandle->pGVM)
        && pHandle->pGVM->u32Magic == GVM_MAGIC)
    {
        /* Check that other EMTs have deregistered. */
        uint32_t cNotDeregistered = 0;
        for (VMCPUID idCpu = 1; idCpu < pGVM->cCpus; idCpu++)
            cNotDeregistered += pGVM->aCpus[idCpu].hEMT != GVMM_RTNATIVETHREAD_DESTROYED;
        if (cNotDeregistered == 0)
        {
            /* Grab the object pointer. */
            void *pvObj = pHandle->pvObj;
            pHandle->pvObj = NULL;
            gvmmR0CreateDestroyUnlock(pGVMM);

            SUPR0ObjRelease(pvObj, pHandle->pSession);
        }
        else
        {
            gvmmR0CreateDestroyUnlock(pGVMM);
            rc = VERR_GVMM_NOT_ALL_EMTS_DEREGISTERED;
        }
    }
    else
    {
        SUPR0Printf("GVMMR0DestroyVM: pHandle=%RKv:{.pGVM=%p, .hEMT0=%p, .ProcId=%u, .pvObj=%p} pGVM=%p hSelf=%p\n",
                    pHandle, pHandle->pGVM, pHandle->hEMT0, pHandle->ProcId, pHandle->pvObj, pGVM, hSelf);
        gvmmR0CreateDestroyUnlock(pGVMM);
        rc = VERR_GVMM_IPE_2;
    }

    return rc;
}


/**
 * Performs VM cleanup task as part of object destruction.
 *
 * @param   pGVM        The GVM pointer.
 */
static void gvmmR0CleanupVM(PGVM pGVM)
{
    if (    pGVM->gvmm.s.fDoneVMMR0Init
        &&  !pGVM->gvmm.s.fDoneVMMR0Term)
    {
        if (    pGVM->gvmm.s.VMMemObj != NIL_RTR0MEMOBJ
            &&  RTR0MemObjAddress(pGVM->gvmm.s.VMMemObj) == pGVM)
        {
            LogFlow(("gvmmR0CleanupVM: Calling VMMR0TermVM\n"));
            VMMR0TermVM(pGVM, NIL_VMCPUID);
        }
        else
            AssertMsgFailed(("gvmmR0CleanupVM: VMMemObj=%p pGVM=%p\n", pGVM->gvmm.s.VMMemObj, pGVM));
    }

    GMMR0CleanupVM(pGVM);
#ifdef VBOX_WITH_NEM_R0
    NEMR0CleanupVM(pGVM);
#endif
    PDMR0CleanupVM(pGVM);
    IOMR0CleanupVM(pGVM);
    DBGFR0CleanupVM(pGVM);
    PGMR0CleanupVM(pGVM);
    TMR0CleanupVM(pGVM);
    VMMR0CleanupVM(pGVM);
}


/**
 * @callback_method_impl{FNSUPDRVDESTRUCTOR,VM handle destructor}
 *
 * pvUser1 is the GVM instance pointer.
 * pvUser2 is the handle pointer.
 */
static DECLCALLBACK(void) gvmmR0HandleObjDestructor(void *pvObj, void *pvUser1, void *pvUser2)
{
    LogFlow(("gvmmR0HandleObjDestructor: %p %p %p\n", pvObj, pvUser1, pvUser2));

    NOREF(pvObj);

    /*
     * Some quick, paranoid, input validation.
     */
    PGVMHANDLE pHandle = (PGVMHANDLE)pvUser2;
    AssertPtr(pHandle);
    PGVMM pGVMM = (PGVMM)pvUser1;
    Assert(pGVMM == g_pGVMM);
    const uint16_t iHandle = pHandle - &pGVMM->aHandles[0];
    if (    !iHandle
        ||  iHandle >= RT_ELEMENTS(pGVMM->aHandles)
        ||  iHandle != pHandle->iSelf)
    {
        SUPR0Printf("GVM: handle %d is out of range or corrupt (iSelf=%d)!\n", iHandle, pHandle->iSelf);
        return;
    }

    int rc = gvmmR0CreateDestroyLock(pGVMM);
    AssertRC(rc);
    rc = GVMMR0_USED_EXCLUSIVE_LOCK(pGVMM);
    AssertRC(rc);

    /*
     * This is a tad slow but a doubly linked list is too much hassle.
     */
    if (RT_UNLIKELY(pHandle->iNext >= RT_ELEMENTS(pGVMM->aHandles)))
    {
        SUPR0Printf("GVM: used list index %d is out of range!\n", pHandle->iNext);
        GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);
        gvmmR0CreateDestroyUnlock(pGVMM);
        return;
    }

    if (pGVMM->iUsedHead == iHandle)
        pGVMM->iUsedHead = pHandle->iNext;
    else
    {
        uint16_t iPrev = pGVMM->iUsedHead;
        int c = RT_ELEMENTS(pGVMM->aHandles) + 2;
        while (iPrev)
        {
            if (RT_UNLIKELY(iPrev >= RT_ELEMENTS(pGVMM->aHandles)))
            {
                SUPR0Printf("GVM: used list index %d is out of range!\n", iPrev);
                GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);
                gvmmR0CreateDestroyUnlock(pGVMM);
                return;
            }
            if (RT_UNLIKELY(c-- <= 0))
            {
                iPrev = 0;
                break;
            }

            if (pGVMM->aHandles[iPrev].iNext == iHandle)
                break;
            iPrev = pGVMM->aHandles[iPrev].iNext;
        }
        if (!iPrev)
        {
            SUPR0Printf("GVM: can't find the handle previous previous of %d!\n", pHandle->iSelf);
            GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);
            gvmmR0CreateDestroyUnlock(pGVMM);
            return;
        }

        Assert(pGVMM->aHandles[iPrev].iNext == iHandle);
        pGVMM->aHandles[iPrev].iNext = pHandle->iNext;
    }
    pHandle->iNext = 0;
    pGVMM->cVMs--;

    /*
     * Do the global cleanup round.
     */
    PGVM pGVM = pHandle->pGVM;
    if (   RT_VALID_PTR(pGVM)
        && pGVM->u32Magic == GVM_MAGIC)
    {
        pGVMM->cEMTs -= pGVM->cCpus;

        if (pGVM->pSession)
            SUPR0SetSessionVM(pGVM->pSession, NULL, NULL);

        GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);

        gvmmR0CleanupVM(pGVM);

        /*
         * Do the GVMM cleanup - must be done last.
         */
        /* The VM and VM pages mappings/allocations. */
        if (pGVM->gvmm.s.VMPagesMapObj != NIL_RTR0MEMOBJ)
        {
            rc = RTR0MemObjFree(pGVM->gvmm.s.VMPagesMapObj, false /* fFreeMappings */); AssertRC(rc);
            pGVM->gvmm.s.VMPagesMapObj = NIL_RTR0MEMOBJ;
        }

        if (pGVM->gvmm.s.VMMapObj != NIL_RTR0MEMOBJ)
        {
            rc = RTR0MemObjFree(pGVM->gvmm.s.VMMapObj, false /* fFreeMappings */); AssertRC(rc);
            pGVM->gvmm.s.VMMapObj = NIL_RTR0MEMOBJ;
        }

        if (pGVM->gvmm.s.VMPagesMemObj != NIL_RTR0MEMOBJ)
        {
            rc = RTR0MemObjFree(pGVM->gvmm.s.VMPagesMemObj, false /* fFreeMappings */); AssertRC(rc);
            pGVM->gvmm.s.VMPagesMemObj = NIL_RTR0MEMOBJ;
        }

        for (VMCPUID i = 0; i < pGVM->cCpus; i++)
        {
            if (pGVM->aCpus[i].gvmm.s.HaltEventMulti != NIL_RTSEMEVENTMULTI)
            {
                rc = RTSemEventMultiDestroy(pGVM->aCpus[i].gvmm.s.HaltEventMulti); AssertRC(rc);
                pGVM->aCpus[i].gvmm.s.HaltEventMulti = NIL_RTSEMEVENTMULTI;
            }
            if (pGVM->aCpus[i].gvmm.s.VMCpuMapObj != NIL_RTR0MEMOBJ)
            {
                rc = RTR0MemObjFree(pGVM->aCpus[i].gvmm.s.VMCpuMapObj, false /* fFreeMappings */); AssertRC(rc);
                pGVM->aCpus[i].gvmm.s.VMCpuMapObj = NIL_RTR0MEMOBJ;
            }
#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
            if (pGVM->aCpus[i].gvmm.s.hHrWakeUpTimer != NULL)
            {
                RTTimerDestroy(pGVM->aCpus[i].gvmm.s.hHrWakeUpTimer);
                pGVM->aCpus[i].gvmm.s.hHrWakeUpTimer = NULL;
            }
#endif
        }

        /* the GVM structure itself. */
        pGVM->u32Magic |= UINT32_C(0x80000000);
        Assert(pGVM->gvmm.s.VMMemObj != NIL_RTR0MEMOBJ);
        rc = RTR0MemObjFree(pGVM->gvmm.s.VMMemObj, true /*fFreeMappings*/); AssertRC(rc);
        pGVM = NULL;

        /* Re-acquire the UsedLock before freeing the handle since we're updating handle fields. */
        rc = GVMMR0_USED_EXCLUSIVE_LOCK(pGVMM);
        AssertRC(rc);
    }
    /* else: GVMMR0CreateVM cleanup. */

    /*
     * Free the handle.
     */
    pHandle->iNext = pGVMM->iFreeHead;
    pGVMM->iFreeHead = iHandle;
    ASMAtomicWriteNullPtr(&pHandle->pGVM);
    ASMAtomicWriteNullPtr(&pHandle->pvObj);
    ASMAtomicWriteNullPtr(&pHandle->pSession);
    ASMAtomicWriteHandle(&pHandle->hEMT0,        NIL_RTNATIVETHREAD);
    ASMAtomicWriteU32(&pHandle->ProcId,          NIL_RTPROCESS);

    GVMMR0_USED_EXCLUSIVE_UNLOCK(pGVMM);
    gvmmR0CreateDestroyUnlock(pGVMM);
    LogFlow(("gvmmR0HandleObjDestructor: returns\n"));
}


/**
 * Registers the calling thread as the EMT of a Virtual CPU.
 *
 * Note that VCPU 0 is automatically registered during VM creation.
 *
 * @returns VBox status code
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       VCPU id to register the current thread as.
 */
GVMMR0DECL(int) GVMMR0RegisterVCpu(PGVM pGVM, VMCPUID idCpu)
{
    AssertReturn(idCpu != 0, VERR_INVALID_FUNCTION);

    /*
     * Validate the VM structure, state and handle.
     */
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, false /* fTakeUsedLock */);
    if (RT_SUCCESS(rc))
    {
        if (idCpu < pGVM->cCpus)
        {
            PGVMCPU const        pGVCpu      = &pGVM->aCpus[idCpu];
            RTNATIVETHREAD const hNativeSelf = RTThreadNativeSelf();

            gvmmR0CreateDestroyLock(pGVMM); /** @todo per-VM lock? */

            /* Check that the EMT isn't already assigned to a thread. */
            if (pGVCpu->hEMT == NIL_RTNATIVETHREAD)
            {
                Assert(pGVCpu->hNativeThreadR0 == NIL_RTNATIVETHREAD);

                /* A thread may only be one EMT (this makes sure hNativeSelf isn't NIL). */
                for (VMCPUID iCpu = 0; iCpu < pGVM->cCpus; iCpu++)
                    AssertBreakStmt(pGVM->aCpus[iCpu].hEMT != hNativeSelf, rc = VERR_INVALID_PARAMETER);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Do the assignment, then try setup the hook. Undo if that fails.
                     */
                    unsigned cCollisions = 0;
                    uint32_t idxHash     = GVMM_EMT_HASH_1(hNativeSelf);
                    if (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt != NIL_RTNATIVETHREAD)
                    {
                        uint32_t const idxHash2 = GVMM_EMT_HASH_2(hNativeSelf);
                        do
                        {
                            cCollisions++;
                            Assert(cCollisions < GVMM_EMT_HASH_SIZE);
                            idxHash = (idxHash + idxHash2) % GVMM_EMT_HASH_SIZE;
                        } while (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt != NIL_RTNATIVETHREAD);
                    }
                    pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt = hNativeSelf;
                    pGVM->gvmm.s.aEmtHash[idxHash].idVCpu     = idCpu;

                    pGVCpu->hNativeThreadR0        = hNativeSelf;
                    pGVCpu->hEMT                   = hNativeSelf;
                    pGVCpu->cEmtHashCollisions     = (uint8_t)cCollisions;
                    pGVCpu->gvmm.s.idxEmtHash      = (uint16_t)idxHash;

                    rc = VMMR0ThreadCtxHookCreateForEmt(pGVCpu);
                    if (RT_SUCCESS(rc))
                    {
                        CPUMR0RegisterVCpuThread(pGVCpu);

#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
                        /*
                         * Create the high resolution wake-up timer, ignore failures.
                         */
                        if (RTTimerCanDoHighResolution())
                        {
                            int rc2 = RTTimerCreateEx(&pGVCpu->gvmm.s.hHrWakeUpTimer, 0 /*one-shot, no interval*/,
                                                      RTTIMER_FLAGS_HIGH_RES, gvmmR0EmtWakeUpTimerCallback, pGVCpu);
                            if (RT_FAILURE(rc2))
                                pGVCpu->gvmm.s.hHrWakeUpTimer = NULL;
                        }
#endif
                    }
                    else
                    {
                        pGVCpu->hNativeThreadR0                   = NIL_RTNATIVETHREAD;
                        pGVCpu->hEMT                              = NIL_RTNATIVETHREAD;
                        pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt = NIL_RTNATIVETHREAD;
                        pGVM->gvmm.s.aEmtHash[idxHash].idVCpu     = NIL_VMCPUID;
                        pGVCpu->gvmm.s.idxEmtHash                 = UINT16_MAX;
                    }
                }
            }
            else
                rc = VERR_ACCESS_DENIED;

            gvmmR0CreateDestroyUnlock(pGVMM);
        }
        else
            rc = VERR_INVALID_CPU_ID;
    }
    return rc;
}


/**
 * Deregisters the calling thread as the EMT of a Virtual CPU.
 *
 * Note that VCPU 0 shall call GVMMR0DestroyVM intead of this API.
 *
 * @returns VBox status code
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       VCPU id to register the current thread as.
 */
GVMMR0DECL(int) GVMMR0DeregisterVCpu(PGVM pGVM, VMCPUID idCpu)
{
    AssertReturn(idCpu != 0, VERR_INVALID_FUNCTION);

    /*
     * Validate the VM structure, state and handle.
     */
    PGVMM pGVMM;
    int rc = gvmmR0ByGVMandEMT(pGVM, idCpu, &pGVMM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Take the destruction lock and recheck the handle state to
         * prevent racing GVMMR0DestroyVM.
         */
        gvmmR0CreateDestroyLock(pGVMM);

        uint32_t hSelf = pGVM->hSelf;
        ASMCompilerBarrier();
        if (   hSelf < RT_ELEMENTS(pGVMM->aHandles)
            && pGVMM->aHandles[hSelf].pvObj != NULL
            && pGVMM->aHandles[hSelf].pGVM  == pGVM)
        {
            /*
             * Do per-EMT cleanups.
             */
            VMMR0ThreadCtxHookDestroyForEmt(&pGVM->aCpus[idCpu]);

            /*
             * Invalidate hEMT.  We don't use NIL here as that would allow
             * GVMMR0RegisterVCpu to be called again, and we don't want that.
             */
            pGVM->aCpus[idCpu].hEMT            = GVMM_RTNATIVETHREAD_DESTROYED;
            pGVM->aCpus[idCpu].hNativeThreadR0 = NIL_RTNATIVETHREAD;

            uint32_t const idxHash = pGVM->aCpus[idCpu].gvmm.s.idxEmtHash;
            if (idxHash < RT_ELEMENTS(pGVM->gvmm.s.aEmtHash))
                pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt = GVMM_RTNATIVETHREAD_DESTROYED;
        }

        gvmmR0CreateDestroyUnlock(pGVMM);
    }
    return rc;
}


/**
 * Registers the caller as a given worker thread.
 *
 * This enables the thread to operate critical sections in ring-0.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   enmWorker       The worker thread this is supposed to be.
 * @param   hNativeSelfR3   The ring-3 native self of the caller.
 */
GVMMR0DECL(int) GVMMR0RegisterWorkerThread(PGVM pGVM, GVMMWORKERTHREAD enmWorker, RTNATIVETHREAD hNativeSelfR3)
{
    /*
     * Validate input.
     */
    AssertReturn(enmWorker > GVMMWORKERTHREAD_INVALID && enmWorker < GVMMWORKERTHREAD_END, VERR_INVALID_PARAMETER);
    AssertReturn(hNativeSelfR3 != NIL_RTNATIVETHREAD, VERR_INVALID_HANDLE);
    RTNATIVETHREAD const hNativeSelf = RTThreadNativeSelf();
    AssertReturn(hNativeSelf != NIL_RTNATIVETHREAD, VERR_INTERNAL_ERROR_3);
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, false /*fTakeUsedLock*/);
    AssertRCReturn(rc, rc);
    AssertReturn(pGVM->enmVMState < VMSTATE_DESTROYING, VERR_VM_INVALID_VM_STATE);

    /*
     * Grab the big lock and check the VM state again.
     */
    uint32_t const hSelf = pGVM->hSelf;
    gvmmR0CreateDestroyLock(pGVMM); /** @todo per-VM lock? */
    if (   hSelf < RT_ELEMENTS(pGVMM->aHandles)
        && pGVMM->aHandles[hSelf].pvObj != NULL
        && pGVMM->aHandles[hSelf].pGVM  == pGVM
        && pGVMM->aHandles[hSelf].ProcId == RTProcSelf())
    {
        if (pGVM->enmVMState < VMSTATE_DESTROYING)
        {
            /*
             * Check that the thread isn't an EMT or serving in some other worker capacity.
             */
            for (VMCPUID iCpu = 0; iCpu < pGVM->cCpus; iCpu++)
                AssertBreakStmt(pGVM->aCpus[iCpu].hEMT != hNativeSelf, rc = VERR_INVALID_PARAMETER);
            for (size_t idx = 0; idx < RT_ELEMENTS(pGVM->gvmm.s.aWorkerThreads); idx++)
                AssertBreakStmt(idx == (size_t)enmWorker || pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThread != hNativeSelf,
                                rc = VERR_INVALID_PARAMETER);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Do the registration.
                 */
                if (   pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThread   == NIL_RTNATIVETHREAD
                    && pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThreadR3 == NIL_RTNATIVETHREAD)
                {
                    pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThread   = hNativeSelf;
                    pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThreadR3 = hNativeSelfR3;
                    rc = VINF_SUCCESS;
                }
                else if (   pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThread   == hNativeSelf
                         && pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThreadR3 == hNativeSelfR3)
                    rc = VERR_ALREADY_EXISTS;
                else
                    rc = VERR_RESOURCE_BUSY;
            }
        }
        else
            rc = VERR_VM_INVALID_VM_STATE;
    }
    else
        rc = VERR_INVALID_VM_HANDLE;
    gvmmR0CreateDestroyUnlock(pGVMM);
    return rc;
}


/**
 * Deregisters a workinger thread (caller).
 *
 * The worker thread cannot be re-created and re-registered, instead the given
 * @a enmWorker slot becomes invalid.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   enmWorker       The worker thread this is supposed to be.
 */
GVMMR0DECL(int)  GVMMR0DeregisterWorkerThread(PGVM pGVM, GVMMWORKERTHREAD enmWorker)
{
    /*
     * Validate input.
     */
    AssertReturn(enmWorker > GVMMWORKERTHREAD_INVALID && enmWorker < GVMMWORKERTHREAD_END, VERR_INVALID_PARAMETER);
    RTNATIVETHREAD const hNativeThread = RTThreadNativeSelf();
    AssertReturn(hNativeThread != NIL_RTNATIVETHREAD, VERR_INTERNAL_ERROR_3);
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, false /*fTakeUsedLock*/);
    AssertRCReturn(rc, rc);

    /*
     * Grab the big lock and check the VM state again.
     */
    uint32_t const hSelf = pGVM->hSelf;
    gvmmR0CreateDestroyLock(pGVMM); /** @todo per-VM lock? */
    if (   hSelf < RT_ELEMENTS(pGVMM->aHandles)
        && pGVMM->aHandles[hSelf].pvObj != NULL
        && pGVMM->aHandles[hSelf].pGVM  == pGVM
        && pGVMM->aHandles[hSelf].ProcId == RTProcSelf())
    {
        /*
         * Do the deregistration.
         * This will prevent any other threads register as the worker later.
         */
        if (pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThread == hNativeThread)
        {
            pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThread   = GVMM_RTNATIVETHREAD_DESTROYED;
            pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThreadR3 = GVMM_RTNATIVETHREAD_DESTROYED;
            rc = VINF_SUCCESS;
        }
        else if (   pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThread   == GVMM_RTNATIVETHREAD_DESTROYED
                 && pGVM->gvmm.s.aWorkerThreads[enmWorker].hNativeThreadR3 == GVMM_RTNATIVETHREAD_DESTROYED)
            rc = VINF_SUCCESS;
        else
            rc = VERR_NOT_OWNER;
    }
    else
        rc = VERR_INVALID_VM_HANDLE;
    gvmmR0CreateDestroyUnlock(pGVMM);
    return rc;
}


/**
 * Lookup a GVM structure by its handle.
 *
 * @returns The GVM pointer on success, NULL on failure.
 * @param   hGVM    The global VM handle. Asserts on bad handle.
 */
GVMMR0DECL(PGVM) GVMMR0ByHandle(uint32_t hGVM)
{
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, NULL);

    /*
     * Validate.
     */
    AssertReturn(hGVM != NIL_GVM_HANDLE, NULL);
    AssertReturn(hGVM < RT_ELEMENTS(pGVMM->aHandles), NULL);

    /*
     * Look it up.
     */
    PGVMHANDLE pHandle = &pGVMM->aHandles[hGVM];
    AssertPtrReturn(pHandle->pvObj, NULL);
    PGVM pGVM = pHandle->pGVM;
    AssertPtrReturn(pGVM, NULL);

    return pGVM;
}


/**
 * Check that the given GVM and VM structures match up.
 *
 * The calling thread must be in the same process as the VM. All current lookups
 * are by threads inside the same process, so this will not be an issue.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   ppGVMM          Where to store the pointer to the GVMM instance data.
 * @param   fTakeUsedLock   Whether to take the used lock or not.  We take it in
 *                          shared mode when requested.
 *
 *                          Be very careful if not taking the lock as it's
 *                          possible that the VM will disappear then!
 *
 * @remark  This will not assert on an invalid pGVM but try return silently.
 */
static int gvmmR0ByGVM(PGVM pGVM, PGVMM *ppGVMM, bool fTakeUsedLock)
{
    /*
     * Check the pointers.
     */
    int rc;
    if (RT_LIKELY(   RT_VALID_PTR(pGVM)
                  && ((uintptr_t)pGVM & HOST_PAGE_OFFSET_MASK) == 0 ))
    {
        /*
         * Get the pGVMM instance and check the VM handle.
         */
        PGVMM pGVMM;
        GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);

        uint16_t hGVM = pGVM->hSelf;
        if (RT_LIKELY(   hGVM != NIL_GVM_HANDLE
                      && hGVM < RT_ELEMENTS(pGVMM->aHandles)))
        {
            RTPROCESS const pidSelf = RTProcSelf();
            PGVMHANDLE      pHandle = &pGVMM->aHandles[hGVM];
            if (fTakeUsedLock)
            {
                rc = GVMMR0_USED_SHARED_LOCK(pGVMM);
                AssertRCReturn(rc, rc);
            }

            if (RT_LIKELY(   pHandle->pGVM   == pGVM
                          && pHandle->ProcId == pidSelf
                          && RT_VALID_PTR(pHandle->pvObj)))
            {
                /*
                 * Some more VM data consistency checks.
                 */
                if (RT_LIKELY(   pGVM->cCpusUnsafe == pGVM->cCpus
                              && pGVM->hSelfUnsafe == hGVM
                              && pGVM->pSelf       == pGVM))
                {
                    if (RT_LIKELY(   pGVM->enmVMState >= VMSTATE_CREATING
                                  && pGVM->enmVMState <= VMSTATE_TERMINATED))
                    {
                        *ppGVMM = pGVMM;
                        return VINF_SUCCESS;
                    }
                    rc = VERR_INCONSISTENT_VM_HANDLE;
                }
                else
                    rc = VERR_INCONSISTENT_VM_HANDLE;
            }
            else
                rc = VERR_INVALID_VM_HANDLE;

            if (fTakeUsedLock)
                GVMMR0_USED_SHARED_UNLOCK(pGVMM);
        }
        else
            rc = VERR_INVALID_VM_HANDLE;
    }
    else
        rc = VERR_INVALID_POINTER;
    return rc;
}


/**
 * Validates a GVM/VM pair.
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 */
GVMMR0DECL(int) GVMMR0ValidateGVM(PGVM pGVM)
{
    PGVMM pGVMM;
    return gvmmR0ByGVM(pGVM, &pGVMM, false /*fTakeUsedLock*/);
}


/**
 * Check that the given GVM and VM structures match up.
 *
 * The calling thread must be in the same process as the VM. All current lookups
 * are by threads inside the same process, so this will not be an issue.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The (alleged) Virtual CPU ID of the calling EMT.
 * @param   ppGVMM          Where to store the pointer to the GVMM instance data.
 * @thread  EMT
 *
 * @remarks This will assert in all failure paths.
 */
static int gvmmR0ByGVMandEMT(PGVM pGVM, VMCPUID idCpu, PGVMM *ppGVMM)
{
    /*
     * Check the pointers.
     */
    AssertPtrReturn(pGVM, VERR_INVALID_POINTER);
    AssertReturn(((uintptr_t)pGVM & HOST_PAGE_OFFSET_MASK) == 0, VERR_INVALID_POINTER);

    /*
     * Get the pGVMM instance and check the VM handle.
     */
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);

    uint16_t hGVM = pGVM->hSelf;
    ASMCompilerBarrier();
    AssertReturn(   hGVM != NIL_GVM_HANDLE
                 && hGVM < RT_ELEMENTS(pGVMM->aHandles), VERR_INVALID_VM_HANDLE);

    RTPROCESS const pidSelf = RTProcSelf();
    PGVMHANDLE      pHandle = &pGVMM->aHandles[hGVM];
    AssertReturn(   pHandle->pGVM   == pGVM
                 && pHandle->ProcId == pidSelf
                 && RT_VALID_PTR(pHandle->pvObj),
                 VERR_INVALID_HANDLE);

    /*
     * Check the EMT claim.
     */
    RTNATIVETHREAD const hAllegedEMT = RTThreadNativeSelf();
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(pGVM->aCpus[idCpu].hEMT == hAllegedEMT, VERR_NOT_OWNER);

    /*
     * Some more VM data consistency checks.
     */
    AssertReturn(pGVM->cCpusUnsafe == pGVM->cCpus, VERR_INCONSISTENT_VM_HANDLE);
    AssertReturn(pGVM->hSelfUnsafe == hGVM, VERR_INCONSISTENT_VM_HANDLE);
    AssertReturn(   pGVM->enmVMState >= VMSTATE_CREATING
                 && pGVM->enmVMState <= VMSTATE_TERMINATED, VERR_INCONSISTENT_VM_HANDLE);

    *ppGVMM = pGVMM;
    return VINF_SUCCESS;
}


/**
 * Validates a GVM/EMT pair.
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The Virtual CPU ID of the calling EMT.
 * @thread  EMT(idCpu)
 */
GVMMR0DECL(int) GVMMR0ValidateGVMandEMT(PGVM pGVM, VMCPUID idCpu)
{
    PGVMM pGVMM;
    return gvmmR0ByGVMandEMT(pGVM, idCpu, &pGVMM);
}


/**
 * Looks up the VM belonging to the specified EMT thread.
 *
 * This is used by the assertion machinery in VMMR0.cpp to avoid causing
 * unnecessary kernel panics when the EMT thread hits an assertion. The
 * call may or not be an EMT thread.
 *
 * @returns Pointer to the VM on success, NULL on failure.
 * @param   hEMT    The native thread handle of the EMT.
 *                  NIL_RTNATIVETHREAD means the current thread
 */
GVMMR0DECL(PVMCC) GVMMR0GetVMByEMT(RTNATIVETHREAD hEMT)
{
    /*
     * No Assertions here as we're usually called in a AssertMsgN or
     * RTAssert* context.
     */
    PGVMM pGVMM = g_pGVMM;
    if (    !RT_VALID_PTR(pGVMM)
        ||  pGVMM->u32Magic != GVMM_MAGIC)
        return NULL;

    if (hEMT == NIL_RTNATIVETHREAD)
        hEMT = RTThreadNativeSelf();
    RTPROCESS ProcId = RTProcSelf();

    /*
     * Search the handles in a linear fashion as we don't dare to take the lock (assert).
     */
/** @todo introduce some pid hash table here, please. */
    for (unsigned i = 1; i < RT_ELEMENTS(pGVMM->aHandles); i++)
    {
        if (    pGVMM->aHandles[i].iSelf == i
            &&  pGVMM->aHandles[i].ProcId == ProcId
            &&  RT_VALID_PTR(pGVMM->aHandles[i].pvObj)
            &&  RT_VALID_PTR(pGVMM->aHandles[i].pGVM))
        {
            if (pGVMM->aHandles[i].hEMT0 == hEMT)
                return pGVMM->aHandles[i].pGVM;

            /* This is fearly safe with the current process per VM approach. */
            PGVM pGVM = pGVMM->aHandles[i].pGVM;
            VMCPUID const cCpus = pGVM->cCpus;
            ASMCompilerBarrier();
            if (    cCpus < 1
                ||  cCpus > VMM_MAX_CPU_COUNT)
                continue;
            for (VMCPUID idCpu = 1; idCpu < cCpus; idCpu++)
                if (pGVM->aCpus[idCpu].hEMT == hEMT)
                    return pGVMM->aHandles[i].pGVM;
        }
    }
    return NULL;
}


/**
 * Looks up the GVMCPU belonging to the specified EMT thread.
 *
 * This is used by the assertion machinery in VMMR0.cpp to avoid causing
 * unnecessary kernel panics when the EMT thread hits an assertion. The
 * call may or not be an EMT thread.
 *
 * @returns Pointer to the VM on success, NULL on failure.
 * @param   hEMT    The native thread handle of the EMT.
 *                  NIL_RTNATIVETHREAD means the current thread
 */
GVMMR0DECL(PGVMCPU) GVMMR0GetGVCpuByEMT(RTNATIVETHREAD hEMT)
{
    /*
     * No Assertions here as we're usually called in a AssertMsgN,
     * RTAssert*, Log and LogRel contexts.
     */
    PGVMM pGVMM = g_pGVMM;
    if (   !RT_VALID_PTR(pGVMM)
        || pGVMM->u32Magic != GVMM_MAGIC)
        return NULL;

    if (hEMT == NIL_RTNATIVETHREAD)
        hEMT = RTThreadNativeSelf();
    RTPROCESS ProcId = RTProcSelf();

    /*
     * Search the handles in a linear fashion as we don't dare to take the lock (assert).
     */
/** @todo introduce some pid hash table here, please. */
    for (unsigned i = 1; i < RT_ELEMENTS(pGVMM->aHandles); i++)
    {
        if (   pGVMM->aHandles[i].iSelf == i
            && pGVMM->aHandles[i].ProcId == ProcId
            && RT_VALID_PTR(pGVMM->aHandles[i].pvObj)
            && RT_VALID_PTR(pGVMM->aHandles[i].pGVM))
        {
            PGVM pGVM = pGVMM->aHandles[i].pGVM;
            if (pGVMM->aHandles[i].hEMT0 == hEMT)
                return &pGVM->aCpus[0];

            /* This is fearly safe with the current process per VM approach. */
            VMCPUID const cCpus = pGVM->cCpus;
            ASMCompilerBarrier();
            ASMCompilerBarrier();
            if (   cCpus < 1
                || cCpus > VMM_MAX_CPU_COUNT)
                continue;
            for (VMCPUID idCpu = 1; idCpu < cCpus; idCpu++)
                if (pGVM->aCpus[idCpu].hEMT == hEMT)
                    return &pGVM->aCpus[idCpu];
        }
    }
    return NULL;
}


/**
 * Get the GVMCPU structure for the given EMT.
 *
 * @returns The VCpu structure for @a hEMT, NULL if not an EMT.
 * @param   pGVM    The global (ring-0) VM structure.
 * @param   hEMT    The native thread handle of the EMT.
 *                  NIL_RTNATIVETHREAD means the current thread
 */
GVMMR0DECL(PGVMCPU) GVMMR0GetGVCpuByGVMandEMT(PGVM pGVM, RTNATIVETHREAD hEMT)
{
    /*
     * Validate & adjust input.
     */
    AssertPtr(pGVM);
    Assert(pGVM->u32Magic == GVM_MAGIC);
    if (hEMT == NIL_RTNATIVETHREAD /* likely */)
    {
        hEMT = RTThreadNativeSelf();
        AssertReturn(hEMT != NIL_RTNATIVETHREAD, NULL);
    }

    /*
     * Find the matching hash table entry.
     * See similar code in GVMMR0GetRing3ThreadForSelf.
     */
    uint32_t idxHash = GVMM_EMT_HASH_1(hEMT);
    if (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt == hEMT)
    { /* likely */ }
    else
    {
#ifdef VBOX_STRICT
        unsigned       cCollisions = 0;
#endif
        uint32_t const idxHash2    = GVMM_EMT_HASH_2(hEMT);
        for (;;)
        {
            Assert(cCollisions++ < GVMM_EMT_HASH_SIZE);
            idxHash = (idxHash + idxHash2) % GVMM_EMT_HASH_SIZE;
            if (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt == hEMT)
                break;
            if (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt == NIL_RTNATIVETHREAD)
            {
#ifdef VBOX_STRICT
                uint32_t idxCpu = pGVM->cCpus;
                AssertStmt(idxCpu < VMM_MAX_CPU_COUNT, idxCpu = VMM_MAX_CPU_COUNT);
                while (idxCpu-- > 0)
                    Assert(pGVM->aCpus[idxCpu].hNativeThreadR0 != hEMT);
#endif
                return NULL;
            }
        }
    }

    /*
     * Validate the VCpu number and translate it into a pointer.
     */
    VMCPUID const idCpu = pGVM->gvmm.s.aEmtHash[idxHash].idVCpu;
    AssertReturn(idCpu < pGVM->cCpus, NULL);
    PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
    Assert(pGVCpu->hNativeThreadR0   == hEMT);
    Assert(pGVCpu->gvmm.s.idxEmtHash == idxHash);
    return pGVCpu;
}


/**
 * Get the native ring-3 thread handle for the caller.
 *
 * This works for EMTs and registered workers.
 *
 * @returns ring-3 native thread handle or NIL_RTNATIVETHREAD.
 * @param   pGVM    The global (ring-0) VM structure.
 */
GVMMR0DECL(RTNATIVETHREAD) GVMMR0GetRing3ThreadForSelf(PGVM pGVM)
{
    /*
     * Validate input.
     */
    AssertPtr(pGVM);
    AssertReturn(pGVM->u32Magic == GVM_MAGIC, NIL_RTNATIVETHREAD);
    RTNATIVETHREAD const hNativeSelf = RTThreadNativeSelf();
    AssertReturn(hNativeSelf != NIL_RTNATIVETHREAD, NIL_RTNATIVETHREAD);

    /*
     * Find the matching hash table entry.
     * See similar code in GVMMR0GetGVCpuByGVMandEMT.
     */
    uint32_t idxHash = GVMM_EMT_HASH_1(hNativeSelf);
    if (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt == hNativeSelf)
    { /* likely */ }
    else
    {
#ifdef VBOX_STRICT
        unsigned       cCollisions = 0;
#endif
        uint32_t const idxHash2    = GVMM_EMT_HASH_2(hNativeSelf);
        for (;;)
        {
            Assert(cCollisions++ < GVMM_EMT_HASH_SIZE);
            idxHash = (idxHash + idxHash2) % GVMM_EMT_HASH_SIZE;
            if (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt == hNativeSelf)
                break;
            if (pGVM->gvmm.s.aEmtHash[idxHash].hNativeEmt == NIL_RTNATIVETHREAD)
            {
#ifdef VBOX_STRICT
                uint32_t idxCpu = pGVM->cCpus;
                AssertStmt(idxCpu < VMM_MAX_CPU_COUNT, idxCpu = VMM_MAX_CPU_COUNT);
                while (idxCpu-- > 0)
                    Assert(pGVM->aCpus[idxCpu].hNativeThreadR0 != hNativeSelf);
#endif

                /*
                 * Not an EMT, so see if it's a worker thread.
                 */
                size_t idx = RT_ELEMENTS(pGVM->gvmm.s.aWorkerThreads);
                while (--idx > GVMMWORKERTHREAD_INVALID)
                    if (pGVM->gvmm.s.aWorkerThreads[idx].hNativeThread == hNativeSelf)
                        return pGVM->gvmm.s.aWorkerThreads[idx].hNativeThreadR3;

                return NIL_RTNATIVETHREAD;
            }
        }
    }

    /*
     * Validate the VCpu number and translate it into a pointer.
     */
    VMCPUID const idCpu = pGVM->gvmm.s.aEmtHash[idxHash].idVCpu;
    AssertReturn(idCpu < pGVM->cCpus, NIL_RTNATIVETHREAD);
    PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
    Assert(pGVCpu->hNativeThreadR0   == hNativeSelf);
    Assert(pGVCpu->gvmm.s.idxEmtHash == idxHash);
    return pGVCpu->hNativeThread;
}


/**
 * Converts a pointer with the GVM structure to a host physical address.
 *
 * @returns Host physical address.
 * @param   pGVM    The global (ring-0) VM structure.
 * @param   pv      The address to convert.
 * @thread  EMT
 */
GVMMR0DECL(RTHCPHYS) GVMMR0ConvertGVMPtr2HCPhys(PGVM pGVM, void *pv)
{
    AssertPtr(pGVM);
    Assert(pGVM->u32Magic == GVM_MAGIC);
    uintptr_t const off = (uintptr_t)pv - (uintptr_t)pGVM;
    Assert(off < RT_UOFFSETOF_DYN(GVM, aCpus[pGVM->cCpus]));
    return RTR0MemObjGetPagePhysAddr(pGVM->gvmm.s.VMMemObj, off >> HOST_PAGE_SHIFT) | ((uintptr_t)pv & HOST_PAGE_OFFSET_MASK);
}


/**
 * This is will wake up expired and soon-to-be expired VMs.
 *
 * @returns Number of VMs that has been woken up.
 * @param   pGVMM       Pointer to the GVMM instance data.
 * @param   u64Now      The current time.
 */
static unsigned gvmmR0SchedDoWakeUps(PGVMM pGVMM, uint64_t u64Now)
{
    /*
     * Skip this if we've got disabled because of high resolution wakeups or by
     * the user.
     */
    if (!pGVMM->fDoEarlyWakeUps)
        return 0;

/** @todo Rewrite this algorithm. See performance defect XYZ. */

    /*
     * A cheap optimization to stop wasting so much time here on big setups.
     */
    const uint64_t  uNsEarlyWakeUp2 = u64Now + pGVMM->nsEarlyWakeUp2;
    if (   pGVMM->cHaltedEMTs == 0
        || uNsEarlyWakeUp2 > pGVMM->uNsNextEmtWakeup)
        return 0;

    /*
     * Only one thread doing this at a time.
     */
    if (!ASMAtomicCmpXchgBool(&pGVMM->fDoingEarlyWakeUps, true, false))
        return 0;

    /*
     * The first pass will wake up VMs which have actually expired
     * and look for VMs that should be woken up in the 2nd and 3rd passes.
     */
    const uint64_t  uNsEarlyWakeUp1 = u64Now + pGVMM->nsEarlyWakeUp1;
    uint64_t        u64Min          = UINT64_MAX;
    unsigned        cWoken          = 0;
    unsigned        cHalted         = 0;
    unsigned        cTodo2nd        = 0;
    unsigned        cTodo3rd        = 0;
    for (unsigned i = pGVMM->iUsedHead, cGuard = 0;
         i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
         i = pGVMM->aHandles[i].iNext)
    {
        PGVM pCurGVM = pGVMM->aHandles[i].pGVM;
        if (    RT_VALID_PTR(pCurGVM)
            &&  pCurGVM->u32Magic == GVM_MAGIC)
        {
            for (VMCPUID idCpu = 0; idCpu < pCurGVM->cCpus; idCpu++)
            {
                PGVMCPU     pCurGVCpu = &pCurGVM->aCpus[idCpu];
                uint64_t    u64       = ASMAtomicUoReadU64(&pCurGVCpu->gvmm.s.u64HaltExpire);
                if (u64)
                {
                    if (u64 <= u64Now)
                    {
                        if (ASMAtomicXchgU64(&pCurGVCpu->gvmm.s.u64HaltExpire, 0))
                        {
                            int rc = RTSemEventMultiSignal(pCurGVCpu->gvmm.s.HaltEventMulti);
                            AssertRC(rc);
                            cWoken++;
                        }
                    }
                    else
                    {
                        cHalted++;
                        if (u64 <= uNsEarlyWakeUp1)
                            cTodo2nd++;
                        else if (u64 <= uNsEarlyWakeUp2)
                            cTodo3rd++;
                        else if (u64 < u64Min)
                            u64 = u64Min;
                    }
                }
            }
        }
        AssertLogRelBreak(cGuard++ < RT_ELEMENTS(pGVMM->aHandles));
    }

    if (cTodo2nd)
    {
        for (unsigned i = pGVMM->iUsedHead, cGuard = 0;
             i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
             i = pGVMM->aHandles[i].iNext)
        {
            PGVM pCurGVM = pGVMM->aHandles[i].pGVM;
            if (    RT_VALID_PTR(pCurGVM)
                &&  pCurGVM->u32Magic == GVM_MAGIC)
            {
                for (VMCPUID idCpu = 0; idCpu < pCurGVM->cCpus; idCpu++)
                {
                    PGVMCPU     pCurGVCpu = &pCurGVM->aCpus[idCpu];
                    uint64_t    u64       = ASMAtomicUoReadU64(&pCurGVCpu->gvmm.s.u64HaltExpire);
                    if (   u64
                        && u64 <= uNsEarlyWakeUp1)
                    {
                        if (ASMAtomicXchgU64(&pCurGVCpu->gvmm.s.u64HaltExpire, 0))
                        {
                            int rc = RTSemEventMultiSignal(pCurGVCpu->gvmm.s.HaltEventMulti);
                            AssertRC(rc);
                            cWoken++;
                        }
                    }
                }
            }
            AssertLogRelBreak(cGuard++ < RT_ELEMENTS(pGVMM->aHandles));
        }
    }

    if (cTodo3rd)
    {
        for (unsigned i = pGVMM->iUsedHead, cGuard = 0;
             i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
             i = pGVMM->aHandles[i].iNext)
        {
            PGVM pCurGVM = pGVMM->aHandles[i].pGVM;
            if (    RT_VALID_PTR(pCurGVM)
                &&  pCurGVM->u32Magic == GVM_MAGIC)
            {
                for (VMCPUID idCpu = 0; idCpu < pCurGVM->cCpus; idCpu++)
                {
                    PGVMCPU     pCurGVCpu = &pCurGVM->aCpus[idCpu];
                    uint64_t    u64       = ASMAtomicUoReadU64(&pCurGVCpu->gvmm.s.u64HaltExpire);
                    if (   u64
                        && u64 <= uNsEarlyWakeUp2)
                    {
                        if (ASMAtomicXchgU64(&pCurGVCpu->gvmm.s.u64HaltExpire, 0))
                        {
                            int rc = RTSemEventMultiSignal(pCurGVCpu->gvmm.s.HaltEventMulti);
                            AssertRC(rc);
                            cWoken++;
                        }
                    }
                }
            }
            AssertLogRelBreak(cGuard++ < RT_ELEMENTS(pGVMM->aHandles));
        }
    }

    /*
     * Set the minimum value.
     */
    pGVMM->uNsNextEmtWakeup = u64Min;

    ASMAtomicWriteBool(&pGVMM->fDoingEarlyWakeUps, false);
    return cWoken;
}


#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
/**
 * Timer callback for the EMT high-resolution wake-up timer.
 *
 * @param   pTimer  The timer handle.
 * @param   pvUser  The global (ring-0) CPU structure for the EMT to wake up.
 * @param   iTick   The current tick.
 */
static DECLCALLBACK(void) gvmmR0EmtWakeUpTimerCallback(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PGVMCPU pGVCpu = (PGVMCPU)pvUser;
    NOREF(pTimer); NOREF(iTick);

    pGVCpu->gvmm.s.fHrWakeUptimerArmed = false;
    if (pGVCpu->gvmm.s.u64HaltExpire != 0)
    {
        RTSemEventMultiSignal(pGVCpu->gvmm.s.HaltEventMulti);
        pGVCpu->gvmm.s.Stats.cWakeUpTimerHits += 1;
    }
    else
        pGVCpu->gvmm.s.Stats.cWakeUpTimerMisses += 1;

    if (RTMpCpuId() == pGVCpu->gvmm.s.idHaltedOnCpu)
        pGVCpu->gvmm.s.Stats.cWakeUpTimerSameCpu += 1;
}
#endif /* GVMM_SCHED_WITH_HR_WAKE_UP_TIMER */


/**
 * Halt the EMT thread.
 *
 * @returns VINF_SUCCESS normal wakeup (timeout or kicked by other thread).
 *          VERR_INTERRUPTED if a signal was scheduled for the thread.
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   pGVCpu              The global (ring-0) CPU structure of the calling
 *                              EMT.
 * @param   u64ExpireGipTime    The time for the sleep to expire expressed as GIP time.
 * @thread  EMT(pGVCpu).
 */
GVMMR0DECL(int) GVMMR0SchedHalt(PGVM pGVM, PGVMCPU pGVCpu, uint64_t u64ExpireGipTime)
{
    LogFlow(("GVMMR0SchedHalt: pGVM=%p pGVCpu=%p(%d) u64ExpireGipTime=%#RX64\n",
             pGVM, pGVCpu, pGVCpu->idCpu, u64ExpireGipTime));
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);

    pGVM->gvmm.s.StatsSched.cHaltCalls++;
    Assert(!pGVCpu->gvmm.s.u64HaltExpire);

    /*
     * If we're doing early wake-ups, we must take the UsedList lock before we
     * start querying the current time.
     * Note! Interrupts must NOT be disabled at this point because we ask for GIP time!
     */
    bool const fDoEarlyWakeUps = pGVMM->fDoEarlyWakeUps;
    if (fDoEarlyWakeUps)
    {
        int rc2 = GVMMR0_USED_SHARED_LOCK(pGVMM); AssertRC(rc2);
    }

    /* GIP hack: We might are frequently sleeping for short intervals where the
       difference between GIP and system time matters on systems with high resolution
       system time. So, convert the input from GIP to System time in that case. */
    Assert(ASMGetFlags() & X86_EFL_IF);
    const uint64_t u64NowSys = RTTimeSystemNanoTS();
    const uint64_t u64NowGip = RTTimeNanoTS();

    if (fDoEarlyWakeUps)
        pGVM->gvmm.s.StatsSched.cHaltWakeUps += gvmmR0SchedDoWakeUps(pGVMM, u64NowGip);

    /*
     * Go to sleep if we must...
     * Cap the sleep time to 1 second to be on the safe side.
     */
    int rc;
    uint64_t cNsInterval = u64ExpireGipTime - u64NowGip;
    if (    u64NowGip < u64ExpireGipTime
        &&  (    cNsInterval >= (pGVMM->cEMTs > pGVMM->cEMTsMeansCompany
                                 ? pGVMM->nsMinSleepCompany
                                 : pGVMM->nsMinSleepAlone)
#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
             || (pGVCpu->gvmm.s.hHrWakeUpTimer != NULL && cNsInterval >= pGVMM->nsMinSleepWithHrTimer)
#endif
             )
       )
    {
        pGVM->gvmm.s.StatsSched.cHaltBlocking++;
        if (cNsInterval > RT_NS_1SEC)
            u64ExpireGipTime = u64NowGip + RT_NS_1SEC;
        ASMAtomicWriteU64(&pGVCpu->gvmm.s.u64HaltExpire, u64ExpireGipTime);
        ASMAtomicIncU32(&pGVMM->cHaltedEMTs);
        if (fDoEarlyWakeUps)
        {
            if (u64ExpireGipTime < pGVMM->uNsNextEmtWakeup)
                pGVMM->uNsNextEmtWakeup = u64ExpireGipTime;
            GVMMR0_USED_SHARED_UNLOCK(pGVMM);
        }

#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
        if (   pGVCpu->gvmm.s.hHrWakeUpTimer != NULL
            && cNsInterval >= RT_MIN(RT_NS_1US, pGVMM->nsMinSleepWithHrTimer))
        {
            STAM_REL_PROFILE_START(&pGVCpu->gvmm.s.Stats.Start, a);
            RTTimerStart(pGVCpu->gvmm.s.hHrWakeUpTimer, cNsInterval);
            pGVCpu->gvmm.s.fHrWakeUptimerArmed = true;
            pGVCpu->gvmm.s.idHaltedOnCpu       = RTMpCpuId();
            STAM_REL_PROFILE_STOP(&pGVCpu->gvmm.s.Stats.Start, a);
        }
#endif

        rc = RTSemEventMultiWaitEx(pGVCpu->gvmm.s.HaltEventMulti,
                                   RTSEMWAIT_FLAGS_ABSOLUTE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_INTERRUPTIBLE,
                                   u64NowGip > u64NowSys ? u64ExpireGipTime : u64NowSys + cNsInterval);

        ASMAtomicWriteU64(&pGVCpu->gvmm.s.u64HaltExpire, 0);
        ASMAtomicDecU32(&pGVMM->cHaltedEMTs);

#ifdef GVMM_SCHED_WITH_HR_WAKE_UP_TIMER
        if (!pGVCpu->gvmm.s.fHrWakeUptimerArmed)
        { /* likely */ }
        else
        {
            STAM_REL_PROFILE_START(&pGVCpu->gvmm.s.Stats.Stop, a);
            RTTimerStop(pGVCpu->gvmm.s.hHrWakeUpTimer);
            pGVCpu->gvmm.s.fHrWakeUptimerArmed         = false;
            pGVCpu->gvmm.s.Stats.cWakeUpTimerCanceled += 1;
            STAM_REL_PROFILE_STOP(&pGVCpu->gvmm.s.Stats.Stop, a);
        }
#endif

        /* Reset the semaphore to try prevent a few false wake-ups. */
        if (rc == VINF_SUCCESS)
            RTSemEventMultiReset(pGVCpu->gvmm.s.HaltEventMulti);
        else if (rc == VERR_TIMEOUT)
        {
            pGVM->gvmm.s.StatsSched.cHaltTimeouts++;
            rc = VINF_SUCCESS;
        }
    }
    else
    {
        pGVM->gvmm.s.StatsSched.cHaltNotBlocking++;
        if (fDoEarlyWakeUps)
            GVMMR0_USED_SHARED_UNLOCK(pGVMM);
        RTSemEventMultiReset(pGVCpu->gvmm.s.HaltEventMulti);
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Halt the EMT thread.
 *
 * @returns VINF_SUCCESS normal wakeup (timeout or kicked by other thread).
 *          VERR_INTERRUPTED if a signal was scheduled for the thread.
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The Virtual CPU ID of the calling EMT.
 * @param   u64ExpireGipTime    The time for the sleep to expire expressed as GIP time.
 * @thread  EMT(idCpu).
 */
GVMMR0DECL(int) GVMMR0SchedHaltReq(PGVM pGVM, VMCPUID idCpu, uint64_t u64ExpireGipTime)
{
    PGVMM pGVMM;
    int rc = gvmmR0ByGVMandEMT(pGVM, idCpu, &pGVMM);
    if (RT_SUCCESS(rc))
        rc = GVMMR0SchedHalt(pGVM, &pGVM->aCpus[idCpu], u64ExpireGipTime);
    return rc;
}



/**
 * Worker for GVMMR0SchedWakeUp and GVMMR0SchedWakeUpAndPokeCpus that wakes up
 * the a sleeping EMT.
 *
 * @retval  VINF_SUCCESS if successfully woken up.
 * @retval  VINF_GVM_NOT_BLOCKED if the EMT wasn't blocked.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   pGVCpu              The global (ring-0) VCPU structure.
 */
DECLINLINE(int) gvmmR0SchedWakeUpOne(PGVM pGVM, PGVMCPU pGVCpu)
{
    pGVM->gvmm.s.StatsSched.cWakeUpCalls++;

    /*
     * Signal the semaphore regardless of whether it's current blocked on it.
     *
     * The reason for this is that there is absolutely no way we can be 100%
     * certain that it isn't *about* go to go to sleep on it and just got
     * delayed a bit en route. So, we will always signal the semaphore when
     * the it is flagged as halted in the VMM.
     */
/** @todo we can optimize some of that by means of the pVCpu->enmState now. */
    int rc;
    if (pGVCpu->gvmm.s.u64HaltExpire)
    {
        rc = VINF_SUCCESS;
        ASMAtomicWriteU64(&pGVCpu->gvmm.s.u64HaltExpire, 0);
    }
    else
    {
        rc = VINF_GVM_NOT_BLOCKED;
        pGVM->gvmm.s.StatsSched.cWakeUpNotHalted++;
    }

    int rc2 = RTSemEventMultiSignal(pGVCpu->gvmm.s.HaltEventMulti);
    AssertRC(rc2);

    return rc;
}


/**
 * Wakes up the halted EMT thread so it can service a pending request.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if successfully woken up.
 * @retval  VINF_GVM_NOT_BLOCKED if the EMT wasn't blocked.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The Virtual CPU ID of the EMT to wake up.
 * @param   fTakeUsedLock       Take the used lock or not
 * @thread  Any but EMT(idCpu).
 */
GVMMR0DECL(int) GVMMR0SchedWakeUpEx(PGVM pGVM, VMCPUID idCpu, bool fTakeUsedLock)
{
    /*
     * Validate input and take the UsedLock.
     */
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, fTakeUsedLock);
    if (RT_SUCCESS(rc))
    {
        if (idCpu < pGVM->cCpus)
        {
            /*
             * Do the actual job.
             */
            rc = gvmmR0SchedWakeUpOne(pGVM, &pGVM->aCpus[idCpu]);

            if (fTakeUsedLock && pGVMM->fDoEarlyWakeUps)
            {
                /*
                 * While we're here, do a round of scheduling.
                 */
                Assert(ASMGetFlags() & X86_EFL_IF);
                const uint64_t u64Now = RTTimeNanoTS(); /* (GIP time) */
                pGVM->gvmm.s.StatsSched.cWakeUpWakeUps += gvmmR0SchedDoWakeUps(pGVMM, u64Now);
            }
        }
        else
            rc = VERR_INVALID_CPU_ID;

        if (fTakeUsedLock)
        {
            int rc2 = GVMMR0_USED_SHARED_UNLOCK(pGVMM);
            AssertRC(rc2);
        }
    }

    LogFlow(("GVMMR0SchedWakeUpEx: returns %Rrc\n", rc));
    return rc;
}


/**
 * Wakes up the halted EMT thread so it can service a pending request.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if successfully woken up.
 * @retval  VINF_GVM_NOT_BLOCKED if the EMT wasn't blocked.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The Virtual CPU ID of the EMT to wake up.
 * @thread  Any but EMT(idCpu).
 */
GVMMR0DECL(int) GVMMR0SchedWakeUp(PGVM pGVM, VMCPUID idCpu)
{
    return GVMMR0SchedWakeUpEx(pGVM, idCpu, true /* fTakeUsedLock */);
}


/**
 * Wakes up the halted EMT thread so it can service a pending request, no GVM
 * parameter and no used locking.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if successfully woken up.
 * @retval  VINF_GVM_NOT_BLOCKED if the EMT wasn't blocked.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The Virtual CPU ID of the EMT to wake up.
 * @thread  Any but EMT(idCpu).
 * @deprecated  Don't use in new code if possible!  Use the GVM variant.
 */
GVMMR0DECL(int) GVMMR0SchedWakeUpNoGVMNoLock(PGVM pGVM, VMCPUID idCpu)
{
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, false /*fTakeUsedLock*/);
    if (RT_SUCCESS(rc))
        rc = GVMMR0SchedWakeUpEx(pGVM, idCpu, false /*fTakeUsedLock*/);
    return rc;
}


/**
 * Worker common to GVMMR0SchedPoke and GVMMR0SchedWakeUpAndPokeCpus that pokes
 * the Virtual CPU if it's still busy executing guest code.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if poked successfully.
 * @retval  VINF_GVM_NOT_BUSY_IN_GC if the EMT wasn't busy in GC.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   pVCpu               The cross context virtual CPU structure.
 */
DECLINLINE(int) gvmmR0SchedPokeOne(PGVM pGVM, PVMCPUCC pVCpu)
{
    pGVM->gvmm.s.StatsSched.cPokeCalls++;

    RTCPUID idHostCpu = pVCpu->idHostCpu;
    if (    idHostCpu == NIL_RTCPUID
        ||  VMCPU_GET_STATE(pVCpu) != VMCPUSTATE_STARTED_EXEC)
    {
        pGVM->gvmm.s.StatsSched.cPokeNotBusy++;
        return VINF_GVM_NOT_BUSY_IN_GC;
    }

    /* Note: this function is not implemented on Darwin and Linux (kernel < 2.6.19) */
    RTMpPokeCpu(idHostCpu);
    return VINF_SUCCESS;
}


/**
 * Pokes an EMT if it's still busy running guest code.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if poked successfully.
 * @retval  VINF_GVM_NOT_BUSY_IN_GC if the EMT wasn't busy in GC.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The ID of the virtual CPU to poke.
 * @param   fTakeUsedLock       Take the used lock or not
 */
GVMMR0DECL(int) GVMMR0SchedPokeEx(PGVM pGVM, VMCPUID idCpu, bool fTakeUsedLock)
{
    /*
     * Validate input and take the UsedLock.
     */
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, fTakeUsedLock);
    if (RT_SUCCESS(rc))
    {
        if (idCpu < pGVM->cCpus)
            rc = gvmmR0SchedPokeOne(pGVM, &pGVM->aCpus[idCpu]);
        else
            rc = VERR_INVALID_CPU_ID;

        if (fTakeUsedLock)
        {
            int rc2 = GVMMR0_USED_SHARED_UNLOCK(pGVMM);
            AssertRC(rc2);
        }
    }

    LogFlow(("GVMMR0SchedWakeUpAndPokeCpus: returns %Rrc\n", rc));
    return rc;
}


/**
 * Pokes an EMT if it's still busy running guest code.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if poked successfully.
 * @retval  VINF_GVM_NOT_BUSY_IN_GC if the EMT wasn't busy in GC.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The ID of the virtual CPU to poke.
 */
GVMMR0DECL(int) GVMMR0SchedPoke(PGVM pGVM, VMCPUID idCpu)
{
    return GVMMR0SchedPokeEx(pGVM, idCpu, true /* fTakeUsedLock */);
}


/**
 * Pokes an EMT if it's still busy running guest code, no GVM parameter and no
 * used locking.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if poked successfully.
 * @retval  VINF_GVM_NOT_BUSY_IN_GC if the EMT wasn't busy in GC.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The ID of the virtual CPU to poke.
 *
 * @deprecated  Don't use in new code if possible!  Use the GVM variant.
 */
GVMMR0DECL(int) GVMMR0SchedPokeNoGVMNoLock(PGVM pGVM, VMCPUID idCpu)
{
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, false /*fTakeUsedLock*/);
    if (RT_SUCCESS(rc))
    {
        if (idCpu < pGVM->cCpus)
            rc = gvmmR0SchedPokeOne(pGVM, &pGVM->aCpus[idCpu]);
        else
            rc = VERR_INVALID_CPU_ID;
    }
    return rc;
}


/**
 * Wakes up a set of halted EMT threads so they can service pending request.
 *
 * @returns VBox status code, no informational stuff.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   pSleepSet           The set of sleepers to wake up.
 * @param   pPokeSet            The set of CPUs to poke.
 */
GVMMR0DECL(int) GVMMR0SchedWakeUpAndPokeCpus(PGVM pGVM, PCVMCPUSET pSleepSet, PCVMCPUSET pPokeSet)
{
    AssertPtrReturn(pSleepSet, VERR_INVALID_POINTER);
    AssertPtrReturn(pPokeSet, VERR_INVALID_POINTER);
    RTNATIVETHREAD hSelf = RTThreadNativeSelf();

    /*
     * Validate input and take the UsedLock.
     */
    PGVMM pGVMM;
    int rc = gvmmR0ByGVM(pGVM, &pGVMM, true /* fTakeUsedLock */);
    if (RT_SUCCESS(rc))
    {
        rc = VINF_SUCCESS;
        VMCPUID idCpu = pGVM->cCpus;
        while (idCpu-- > 0)
        {
            /* Don't try poke or wake up ourselves. */
            if (pGVM->aCpus[idCpu].hEMT == hSelf)
                continue;

            /* just ignore errors for now. */
            if (VMCPUSET_IS_PRESENT(pSleepSet, idCpu))
                gvmmR0SchedWakeUpOne(pGVM, &pGVM->aCpus[idCpu]);
            else if (VMCPUSET_IS_PRESENT(pPokeSet, idCpu))
                gvmmR0SchedPokeOne(pGVM, &pGVM->aCpus[idCpu]);
        }

        int rc2 = GVMMR0_USED_SHARED_UNLOCK(pGVMM);
        AssertRC(rc2);
    }

    LogFlow(("GVMMR0SchedWakeUpAndPokeCpus: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GVMMR0SchedWakeUpAndPokeCpus.
 *
 * @returns see GVMMR0SchedWakeUpAndPokeCpus.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pReq            Pointer to the request packet.
 */
GVMMR0DECL(int) GVMMR0SchedWakeUpAndPokeCpusReq(PGVM pGVM, PGVMMSCHEDWAKEUPANDPOKECPUSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GVMMR0SchedWakeUpAndPokeCpus(pGVM, &pReq->SleepSet, &pReq->PokeSet);
}



/**
 * Poll the schedule to see if someone else should get a chance to run.
 *
 * This is a bit hackish and will not work too well if the machine is
 * under heavy load from non-VM processes.
 *
 * @returns VINF_SUCCESS if not yielded.
 *          VINF_GVM_YIELDED if an attempt to switch to a different VM task was made.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The Virtual CPU ID of the calling EMT.
 * @param   fYield          Whether to yield or not.
 *                          This is for when we're spinning in the halt loop.
 * @thread  EMT(idCpu).
 */
GVMMR0DECL(int) GVMMR0SchedPoll(PGVM pGVM, VMCPUID idCpu, bool fYield)
{
    /*
     * Validate input.
     */
    PGVMM pGVMM;
    int rc = gvmmR0ByGVMandEMT(pGVM, idCpu, &pGVMM);
    if (RT_SUCCESS(rc))
    {
        /*
         * We currently only implement helping doing wakeups (fYield = false), so don't
         * bother taking the lock if gvmmR0SchedDoWakeUps is not going to do anything.
         */
        if (!fYield && pGVMM->fDoEarlyWakeUps)
        {
            rc = GVMMR0_USED_SHARED_LOCK(pGVMM); AssertRC(rc);
            pGVM->gvmm.s.StatsSched.cPollCalls++;

            Assert(ASMGetFlags() & X86_EFL_IF);
            const uint64_t u64Now = RTTimeNanoTS(); /* (GIP time) */

            pGVM->gvmm.s.StatsSched.cPollWakeUps += gvmmR0SchedDoWakeUps(pGVMM, u64Now);

            GVMMR0_USED_SHARED_UNLOCK(pGVMM);
        }
        /*
         * Not quite sure what we could do here...
         */
        else if (fYield)
            rc = VERR_NOT_IMPLEMENTED; /** @todo implement this... */
        else
            rc = VINF_SUCCESS;
    }

    LogFlow(("GVMMR0SchedWakeUp: returns %Rrc\n", rc));
    return rc;
}


#ifdef GVMM_SCHED_WITH_PPT
/**
 * Timer callback for the periodic preemption timer.
 *
 * @param   pTimer      The timer handle.
 * @param   pvUser      Pointer to the per cpu structure.
 * @param   iTick       The current tick.
 */
static DECLCALLBACK(void) gvmmR0SchedPeriodicPreemptionTimerCallback(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PGVMMHOSTCPU pCpu = (PGVMMHOSTCPU)pvUser;
    NOREF(pTimer); NOREF(iTick);

    /*
     * Termination check
     */
    if (pCpu->u32Magic != GVMMHOSTCPU_MAGIC)
        return;

    /*
     * Do the house keeping.
     */
    RTSpinlockAcquire(pCpu->Ppt.hSpinlock);

    if (++pCpu->Ppt.iTickHistorization >= pCpu->Ppt.cTicksHistoriziationInterval)
    {
        /*
         * Historicize the max frequency.
         */
        uint32_t iHzHistory = ++pCpu->Ppt.iHzHistory % RT_ELEMENTS(pCpu->Ppt.aHzHistory);
        pCpu->Ppt.aHzHistory[iHzHistory] = pCpu->Ppt.uDesiredHz;
        pCpu->Ppt.iTickHistorization = 0;
        pCpu->Ppt.uDesiredHz         = 0;

        /*
         * Check if the current timer frequency.
         */
        uint32_t uHistMaxHz = 0;
        for (uint32_t i = 0; i < RT_ELEMENTS(pCpu->Ppt.aHzHistory); i++)
            if (pCpu->Ppt.aHzHistory[i] > uHistMaxHz)
                uHistMaxHz = pCpu->Ppt.aHzHistory[i];
        if (uHistMaxHz == pCpu->Ppt.uTimerHz)
            RTSpinlockRelease(pCpu->Ppt.hSpinlock);
        else if (uHistMaxHz)
        {
            /*
             * Reprogram it.
             */
            pCpu->Ppt.cChanges++;
            pCpu->Ppt.iTickHistorization    = 0;
            pCpu->Ppt.uTimerHz              = uHistMaxHz;
            uint32_t const cNsInterval      = RT_NS_1SEC / uHistMaxHz;
            pCpu->Ppt.cNsInterval           = cNsInterval;
            if (cNsInterval < GVMMHOSTCPU_PPT_HIST_INTERVAL_NS)
                pCpu->Ppt.cTicksHistoriziationInterval = (  GVMMHOSTCPU_PPT_HIST_INTERVAL_NS
                                                          + GVMMHOSTCPU_PPT_HIST_INTERVAL_NS / 2 - 1)
                                                       / cNsInterval;
            else
                pCpu->Ppt.cTicksHistoriziationInterval = 1;
            RTSpinlockRelease(pCpu->Ppt.hSpinlock);

            /*SUPR0Printf("Cpu%u: change to %u Hz / %u ns\n", pCpu->idxCpuSet, uHistMaxHz, cNsInterval);*/
            RTTimerChangeInterval(pTimer, cNsInterval);
        }
        else
        {
            /*
             * Stop it.
             */
            pCpu->Ppt.fStarted    = false;
            pCpu->Ppt.uTimerHz    = 0;
            pCpu->Ppt.cNsInterval = 0;
            RTSpinlockRelease(pCpu->Ppt.hSpinlock);

            /*SUPR0Printf("Cpu%u: stopping (%u Hz)\n", pCpu->idxCpuSet, uHistMaxHz);*/
            RTTimerStop(pTimer);
        }
    }
    else
        RTSpinlockRelease(pCpu->Ppt.hSpinlock);
}
#endif /* GVMM_SCHED_WITH_PPT */


/**
 * Updates the periodic preemption timer for the calling CPU.
 *
 * The caller must have disabled preemption!
 * The caller must check that the host can do high resolution timers.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idHostCpu   The current host CPU id.
 * @param   uHz         The desired frequency.
 */
GVMMR0DECL(void) GVMMR0SchedUpdatePeriodicPreemptionTimer(PGVM pGVM, RTCPUID idHostCpu, uint32_t uHz)
{
    NOREF(pGVM);
#ifdef GVMM_SCHED_WITH_PPT
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(RTTimerCanDoHighResolution());

    /*
     * Resolve the per CPU data.
     */
    uint32_t    iCpu  = RTMpCpuIdToSetIndex(idHostCpu);
    PGVMM       pGVMM = g_pGVMM;
    if (   !RT_VALID_PTR(pGVMM)
        || pGVMM->u32Magic != GVMM_MAGIC)
        return;
    AssertMsgReturnVoid(iCpu < pGVMM->cHostCpus, ("iCpu=%d cHostCpus=%d\n", iCpu, pGVMM->cHostCpus));
    PGVMMHOSTCPU pCpu = &pGVMM->aHostCpus[iCpu];
    AssertMsgReturnVoid(   pCpu->u32Magic == GVMMHOSTCPU_MAGIC
                        && pCpu->idCpu    == idHostCpu,
                        ("u32Magic=%#x idCpu=% idHostCpu=%d\n", pCpu->u32Magic, pCpu->idCpu, idHostCpu));

    /*
     * Check whether we need to do anything about the timer.
     * We have to be a little bit careful since we might be race the timer
     * callback here.
     */
    if (uHz > 16384)
        uHz = 16384;  /** @todo add a query method for this! */
    if (RT_UNLIKELY(   uHz > ASMAtomicReadU32(&pCpu->Ppt.uDesiredHz)
                    && uHz >= pCpu->Ppt.uMinHz
                    && !pCpu->Ppt.fStarting /* solaris paranoia */))
    {
        RTSpinlockAcquire(pCpu->Ppt.hSpinlock);

        pCpu->Ppt.uDesiredHz = uHz;
        uint32_t cNsInterval = 0;
        if (!pCpu->Ppt.fStarted)
        {
            pCpu->Ppt.cStarts++;
            pCpu->Ppt.fStarted              = true;
            pCpu->Ppt.fStarting             = true;
            pCpu->Ppt.iTickHistorization    = 0;
            pCpu->Ppt.uTimerHz              = uHz;
            pCpu->Ppt.cNsInterval           = cNsInterval = RT_NS_1SEC / uHz;
            if (cNsInterval < GVMMHOSTCPU_PPT_HIST_INTERVAL_NS)
                pCpu->Ppt.cTicksHistoriziationInterval = (  GVMMHOSTCPU_PPT_HIST_INTERVAL_NS
                                                          + GVMMHOSTCPU_PPT_HIST_INTERVAL_NS / 2 - 1)
                                                       / cNsInterval;
            else
                pCpu->Ppt.cTicksHistoriziationInterval = 1;
        }

        RTSpinlockRelease(pCpu->Ppt.hSpinlock);

        if (cNsInterval)
        {
            RTTimerChangeInterval(pCpu->Ppt.pTimer, cNsInterval);
            int rc = RTTimerStart(pCpu->Ppt.pTimer, cNsInterval);
            AssertRC(rc);

            RTSpinlockAcquire(pCpu->Ppt.hSpinlock);
            if (RT_FAILURE(rc))
                pCpu->Ppt.fStarted = false;
            pCpu->Ppt.fStarting = false;
            RTSpinlockRelease(pCpu->Ppt.hSpinlock);
        }
    }
#else  /* !GVMM_SCHED_WITH_PPT */
    NOREF(idHostCpu); NOREF(uHz);
#endif /* !GVMM_SCHED_WITH_PPT */
}


/**
 * Calls @a pfnCallback for each VM in the system.
 *
 * This will enumerate the VMs while holding the global VM used list lock in
 * shared mode.  So, only suitable for simple work.  If more expensive work
 * needs doing, a different approach must be taken as using this API would
 * otherwise block VM creation and destruction.
 *
 * @returns VBox status code.
 * @param   pfnCallback     The callback function.
 * @param   pvUser          User argument to the callback.
 */
GVMMR0DECL(int) GVMMR0EnumVMs(PFNGVMMR0ENUMCALLBACK pfnCallback, void *pvUser)
{
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);

    int rc = VINF_SUCCESS;
    GVMMR0_USED_SHARED_LOCK(pGVMM);
    for (unsigned i = pGVMM->iUsedHead, cLoops = 0;
         i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
         i = pGVMM->aHandles[i].iNext, cLoops++)
    {
        PGVM pGVM = pGVMM->aHandles[i].pGVM;
        if (   RT_VALID_PTR(pGVM)
            && RT_VALID_PTR(pGVMM->aHandles[i].pvObj)
            && pGVM->u32Magic == GVM_MAGIC)
        {
            rc = pfnCallback(pGVM, pvUser);
            if (rc != VINF_SUCCESS)
                break;
        }

        AssertBreak(cLoops < RT_ELEMENTS(pGVMM->aHandles) * 4); /* paranoia */
    }
    GVMMR0_USED_SHARED_UNLOCK(pGVMM);
    return rc;
}


/**
 * Retrieves the GVMM statistics visible to the caller.
 *
 * @returns VBox status code.
 *
 * @param   pStats      Where to put the statistics.
 * @param   pSession    The current session.
 * @param   pGVM        The GVM to obtain statistics for. Optional.
 */
GVMMR0DECL(int) GVMMR0QueryStatistics(PGVMMSTATS pStats, PSUPDRVSESSION pSession, PGVM pGVM)
{
    LogFlow(("GVMMR0QueryStatistics: pStats=%p pSession=%p pGVM=%p\n", pStats, pSession, pGVM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStats, VERR_INVALID_POINTER);
    pStats->cVMs = 0; /* (crash before taking the sem...) */

    /*
     * Take the lock and get the VM statistics.
     */
    PGVMM pGVMM;
    if (pGVM)
    {
        int rc = gvmmR0ByGVM(pGVM, &pGVMM, true /*fTakeUsedLock*/);
        if (RT_FAILURE(rc))
            return rc;
        pStats->SchedVM = pGVM->gvmm.s.StatsSched;

        uint32_t iCpu = RT_MIN(pGVM->cCpus, RT_ELEMENTS(pStats->aVCpus));
        if (iCpu < RT_ELEMENTS(pStats->aVCpus))
            RT_BZERO(&pStats->aVCpus[iCpu], (RT_ELEMENTS(pStats->aVCpus) - iCpu) * sizeof(pStats->aVCpus[0]));
        while (iCpu-- > 0)
            pStats->aVCpus[iCpu] = pGVM->aCpus[iCpu].gvmm.s.Stats;
    }
    else
    {
        GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);
        RT_ZERO(pStats->SchedVM);
        RT_ZERO(pStats->aVCpus);

        int rc = GVMMR0_USED_SHARED_LOCK(pGVMM);
        AssertRCReturn(rc, rc);
    }

    /*
     * Enumerate the VMs and add the ones visible to the statistics.
     */
    pStats->cVMs = 0;
    pStats->cEMTs = 0;
    memset(&pStats->SchedSum, 0, sizeof(pStats->SchedSum));

    for (unsigned i = pGVMM->iUsedHead;
         i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
         i = pGVMM->aHandles[i].iNext)
    {
        PGVM pOtherGVM = pGVMM->aHandles[i].pGVM;
        void *pvObj = pGVMM->aHandles[i].pvObj;
        if (    RT_VALID_PTR(pvObj)
            &&  RT_VALID_PTR(pOtherGVM)
            &&  pOtherGVM->u32Magic == GVM_MAGIC
            &&  RT_SUCCESS(SUPR0ObjVerifyAccess(pvObj, pSession, NULL)))
        {
            pStats->cVMs++;
            pStats->cEMTs += pOtherGVM->cCpus;

            pStats->SchedSum.cHaltCalls        += pOtherGVM->gvmm.s.StatsSched.cHaltCalls;
            pStats->SchedSum.cHaltBlocking     += pOtherGVM->gvmm.s.StatsSched.cHaltBlocking;
            pStats->SchedSum.cHaltTimeouts     += pOtherGVM->gvmm.s.StatsSched.cHaltTimeouts;
            pStats->SchedSum.cHaltNotBlocking  += pOtherGVM->gvmm.s.StatsSched.cHaltNotBlocking;
            pStats->SchedSum.cHaltWakeUps      += pOtherGVM->gvmm.s.StatsSched.cHaltWakeUps;

            pStats->SchedSum.cWakeUpCalls      += pOtherGVM->gvmm.s.StatsSched.cWakeUpCalls;
            pStats->SchedSum.cWakeUpNotHalted  += pOtherGVM->gvmm.s.StatsSched.cWakeUpNotHalted;
            pStats->SchedSum.cWakeUpWakeUps    += pOtherGVM->gvmm.s.StatsSched.cWakeUpWakeUps;

            pStats->SchedSum.cPokeCalls        += pOtherGVM->gvmm.s.StatsSched.cPokeCalls;
            pStats->SchedSum.cPokeNotBusy      += pOtherGVM->gvmm.s.StatsSched.cPokeNotBusy;

            pStats->SchedSum.cPollCalls        += pOtherGVM->gvmm.s.StatsSched.cPollCalls;
            pStats->SchedSum.cPollHalts        += pOtherGVM->gvmm.s.StatsSched.cPollHalts;
            pStats->SchedSum.cPollWakeUps      += pOtherGVM->gvmm.s.StatsSched.cPollWakeUps;
        }
    }

    /*
     * Copy out the per host CPU statistics.
     */
    uint32_t iDstCpu = 0;
    uint32_t cSrcCpus = pGVMM->cHostCpus;
    for (uint32_t iSrcCpu = 0; iSrcCpu < cSrcCpus; iSrcCpu++)
    {
        if (pGVMM->aHostCpus[iSrcCpu].idCpu != NIL_RTCPUID)
        {
            pStats->aHostCpus[iDstCpu].idCpu      = pGVMM->aHostCpus[iSrcCpu].idCpu;
            pStats->aHostCpus[iDstCpu].idxCpuSet  = pGVMM->aHostCpus[iSrcCpu].idxCpuSet;
#ifdef GVMM_SCHED_WITH_PPT
            pStats->aHostCpus[iDstCpu].uDesiredHz = pGVMM->aHostCpus[iSrcCpu].Ppt.uDesiredHz;
            pStats->aHostCpus[iDstCpu].uTimerHz   = pGVMM->aHostCpus[iSrcCpu].Ppt.uTimerHz;
            pStats->aHostCpus[iDstCpu].cChanges   = pGVMM->aHostCpus[iSrcCpu].Ppt.cChanges;
            pStats->aHostCpus[iDstCpu].cStarts    = pGVMM->aHostCpus[iSrcCpu].Ppt.cStarts;
#else
            pStats->aHostCpus[iDstCpu].uDesiredHz = 0;
            pStats->aHostCpus[iDstCpu].uTimerHz   = 0;
            pStats->aHostCpus[iDstCpu].cChanges   = 0;
            pStats->aHostCpus[iDstCpu].cStarts    = 0;
#endif
            iDstCpu++;
            if (iDstCpu >= RT_ELEMENTS(pStats->aHostCpus))
                break;
        }
    }
    pStats->cHostCpus = iDstCpu;

    GVMMR0_USED_SHARED_UNLOCK(pGVMM);

    return VINF_SUCCESS;
}


/**
 * VMMR0 request wrapper for GVMMR0QueryStatistics.
 *
 * @returns see GVMMR0QueryStatistics.
 * @param   pGVM            The global (ring-0) VM structure. Optional.
 * @param   pReq            Pointer to the request packet.
 * @param   pSession        The current session.
 */
GVMMR0DECL(int) GVMMR0QueryStatisticsReq(PGVM pGVM, PGVMMQUERYSTATISTICSSREQ pReq, PSUPDRVSESSION pSession)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);
    AssertReturn(pReq->pSession == pSession, VERR_INVALID_PARAMETER);

    return GVMMR0QueryStatistics(&pReq->Stats, pSession, pGVM);
}


/**
 * Resets the specified GVMM statistics.
 *
 * @returns VBox status code.
 *
 * @param   pStats      Which statistics to reset, that is, non-zero fields indicates which to reset.
 * @param   pSession    The current session.
 * @param   pGVM        The GVM to reset statistics for. Optional.
 */
GVMMR0DECL(int) GVMMR0ResetStatistics(PCGVMMSTATS pStats, PSUPDRVSESSION pSession, PGVM pGVM)
{
    LogFlow(("GVMMR0ResetStatistics: pStats=%p pSession=%p pGVM=%p\n", pStats, pSession, pGVM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStats, VERR_INVALID_POINTER);

    /*
     * Take the lock and get the VM statistics.
     */
    PGVMM pGVMM;
    if (pGVM)
    {
        int rc = gvmmR0ByGVM(pGVM, &pGVMM, true /*fTakeUsedLock*/);
        if (RT_FAILURE(rc))
            return rc;
#       define MAYBE_RESET_FIELD(field) \
            do { if (pStats->SchedVM. field ) { pGVM->gvmm.s.StatsSched. field = 0; } } while (0)
        MAYBE_RESET_FIELD(cHaltCalls);
        MAYBE_RESET_FIELD(cHaltBlocking);
        MAYBE_RESET_FIELD(cHaltTimeouts);
        MAYBE_RESET_FIELD(cHaltNotBlocking);
        MAYBE_RESET_FIELD(cHaltWakeUps);
        MAYBE_RESET_FIELD(cWakeUpCalls);
        MAYBE_RESET_FIELD(cWakeUpNotHalted);
        MAYBE_RESET_FIELD(cWakeUpWakeUps);
        MAYBE_RESET_FIELD(cPokeCalls);
        MAYBE_RESET_FIELD(cPokeNotBusy);
        MAYBE_RESET_FIELD(cPollCalls);
        MAYBE_RESET_FIELD(cPollHalts);
        MAYBE_RESET_FIELD(cPollWakeUps);
#       undef MAYBE_RESET_FIELD
    }
    else
    {
        GVMM_GET_VALID_INSTANCE(pGVMM, VERR_GVMM_INSTANCE);

        int rc = GVMMR0_USED_SHARED_LOCK(pGVMM);
        AssertRCReturn(rc, rc);
    }

    /*
     * Enumerate the VMs and add the ones visible to the statistics.
     */
    if (!ASMMemIsZero(&pStats->SchedSum, sizeof(pStats->SchedSum)))
    {
        for (unsigned i = pGVMM->iUsedHead;
             i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
             i = pGVMM->aHandles[i].iNext)
        {
            PGVM pOtherGVM = pGVMM->aHandles[i].pGVM;
            void *pvObj = pGVMM->aHandles[i].pvObj;
            if (    RT_VALID_PTR(pvObj)
                &&  RT_VALID_PTR(pOtherGVM)
                &&  pOtherGVM->u32Magic == GVM_MAGIC
                &&  RT_SUCCESS(SUPR0ObjVerifyAccess(pvObj, pSession, NULL)))
            {
#               define MAYBE_RESET_FIELD(field) \
                    do { if (pStats->SchedSum. field ) { pOtherGVM->gvmm.s.StatsSched. field = 0; } } while (0)
                MAYBE_RESET_FIELD(cHaltCalls);
                MAYBE_RESET_FIELD(cHaltBlocking);
                MAYBE_RESET_FIELD(cHaltTimeouts);
                MAYBE_RESET_FIELD(cHaltNotBlocking);
                MAYBE_RESET_FIELD(cHaltWakeUps);
                MAYBE_RESET_FIELD(cWakeUpCalls);
                MAYBE_RESET_FIELD(cWakeUpNotHalted);
                MAYBE_RESET_FIELD(cWakeUpWakeUps);
                MAYBE_RESET_FIELD(cPokeCalls);
                MAYBE_RESET_FIELD(cPokeNotBusy);
                MAYBE_RESET_FIELD(cPollCalls);
                MAYBE_RESET_FIELD(cPollHalts);
                MAYBE_RESET_FIELD(cPollWakeUps);
#               undef MAYBE_RESET_FIELD
            }
        }
    }

    GVMMR0_USED_SHARED_UNLOCK(pGVMM);

    return VINF_SUCCESS;
}


/**
 * VMMR0 request wrapper for GVMMR0ResetStatistics.
 *
 * @returns see GVMMR0ResetStatistics.
 * @param   pGVM            The global (ring-0) VM structure. Optional.
 * @param   pReq            Pointer to the request packet.
 * @param   pSession        The current session.
 */
GVMMR0DECL(int) GVMMR0ResetStatisticsReq(PGVM pGVM, PGVMMRESETSTATISTICSSREQ pReq, PSUPDRVSESSION pSession)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);
    AssertReturn(pReq->pSession == pSession, VERR_INVALID_PARAMETER);

    return GVMMR0ResetStatistics(&pReq->Stats, pSession, pGVM);
}

