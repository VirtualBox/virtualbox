/* $Id: tstRTCritSect.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Critical Sections.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef TRY_WIN32_CRIT
# include <iprt/win/windows.h>
#endif
#define RTCRITSECT_WITHOUT_REMAPPING
#include <iprt/critsect.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/cpp/lock.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/thread.h>


#ifndef TRY_WIN32_CRIT
# define LOCKERS(sect)   ((sect).cLockers)
#else /* TRY_WIN32_CRIT */

/* This is for comparing with the "real thing". */
#define RTCRITSECT      CRITICAL_SECTION
#define PRTCRITSECT     LPCRITICAL_SECTION
#define LOCKERS(sect)   (*(LONG volatile *)&(sect).LockCount)

DECLINLINE(int) RTCritSectInit(PCRITICAL_SECTION pCritSect)
{
    InitializeCriticalSection(pCritSect);
    return VINF_SUCCESS;
}

DECLINLINE(int) RTCritSectEnter(PCRITICAL_SECTION pCritSect)
{
    EnterCriticalSection(pCritSect);
    return VINF_SUCCESS;
}

DECLINLINE(int) RTCritSectLeave(PCRITICAL_SECTION pCritSect)
{
    LeaveCriticalSection(pCritSect);
    return VINF_SUCCESS;
}

DECLINLINE(int) RTCritSectDelete(PCRITICAL_SECTION pCritSect)
{
    DeleteCriticalSection(pCritSect);
    return VINF_SUCCESS;
}

#endif /* TRY_WIN32_CRIT */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Arguments to ThreadTest1().
 */
typedef struct THREADTEST1ARGS
{
    /** The critical section. */
    PRTCRITSECT         pCritSect;
    /** The thread ordinal. */
    uint32_t            iThread;
    /** Pointer to the release counter. */
    uint32_t volatile  *pu32Release;
} THREADTEST1ARGS, *PTHREADTEST1ARGS;


/**
 * Arguments to ThreadTest2().
 */
typedef struct THREADTEST2ARGS
{
    /** The critical section. */
    PRTCRITSECT         pCritSect;
    /** The thread ordinal. */
    uint32_t            iThread;
    /** Pointer to the release counter. */
    uint32_t volatile  *pu32Release;
    /** Pointer to the alone indicator. */
    uint32_t volatile  *pu32Alone;
    /** Pointer to the previous thread variable. */
    uint32_t volatile  *pu32Prev;
    /** Pointer to the sequential enters counter. */
    uint32_t volatile  *pcSeq;
    /** Pointer to the reordered enters counter. */
    uint32_t volatile  *pcReordered;
    /** Pointer to the variable counting running threads. */
    uint32_t volatile  *pcThreadRunning;
    /** Number of times this thread was inside the section. */
    uint32_t volatile   cTimes;
    /** The number of threads. */
    uint32_t            cThreads;
    /** Number of iterations (sum of all threads). */
    uint32_t            cIterations;
    /** Yield while inside the section. */
    unsigned            cCheckLoops;
    /** Signal this when done. */
    RTSEMEVENT          EventDone;
} THREADTEST2ARGS, *PTHREADTEST2ARGS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test handle. */
static RTTEST g_hTest;



/**
 * Thread which goes to sleep on the critsect and checks that it's released in the right order.
 */
static DECLCALLBACK(int) ThreadTest1(RTTHREAD ThreadSelf, void *pvArgs)
{
    RT_NOREF1(ThreadSelf);
    THREADTEST1ARGS Args = *(PTHREADTEST1ARGS)pvArgs;
    Log2(("ThreadTest1: Start - iThread=%d ThreadSelf=%p\n", Args.iThread, ThreadSelf));
    RTMemFree(pvArgs);

    /*
     * Enter it.
     */
    int rc = RTCritSectEnter(Args.pCritSect);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "thread %d: RTCritSectEnter -> %Rrc", Args.iThread, rc);
        return 1;
    }

    /*
     * Check release order.
     */
    if (*Args.pu32Release != Args.iThread)
        RTTestFailed(g_hTest, "thread %d: released as number %d", Args.iThread, *Args.pu32Release);
    ASMAtomicIncU32(Args.pu32Release);

    /*
     * Leave it.
     */
    rc = RTCritSectLeave(Args.pCritSect);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "thread %d: RTCritSectEnter -> %Rrc", Args.iThread, rc);
        return 1;
    }

    Log2(("ThreadTest1: End - iThread=%d ThreadSelf=%p\n", Args.iThread, ThreadSelf));
    return 0;
}


static int Test1(unsigned cThreads)
{
    RTTestSubF(g_hTest, "Test #1 with %u thread", cThreads);

    /*
     * Create a critical section.
     */
    RTCRITSECT CritSect;
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectInit(&CritSect), VINF_SUCCESS, 1);

    /*
     * Enter, leave and enter again.
     */
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectEnter(&CritSect), VINF_SUCCESS, 1);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectLeave(&CritSect), VINF_SUCCESS, 1);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectEnter(&CritSect), VINF_SUCCESS, 1);

    /*
     * Now spawn threads which will go to sleep entering the critsect.
     */
    uint32_t    u32Release = 0;
    for (uint32_t iThread = 0; iThread < cThreads; iThread++)
    {
        PTHREADTEST1ARGS pArgs = (PTHREADTEST1ARGS)RTMemAllocZ(sizeof(*pArgs));
        pArgs->iThread = iThread;
        pArgs->pCritSect = &CritSect;
        pArgs->pu32Release = &u32Release;
        int32_t     iLock = LOCKERS(CritSect);
        RTTHREAD    Thread;
        RTTEST_CHECK_RC_RET(g_hTest, RTThreadCreateF(&Thread, ThreadTest1, pArgs, 0, RTTHREADTYPE_DEFAULT, 0, "T%d", iThread), VINF_SUCCESS, 1);

        /* wait for it to get into waiting. */
        while (LOCKERS(CritSect) == iLock)
            RTThreadSleep(10);
        RTThreadSleep(20);
    }

    /*
     * Now we'll release the threads and wait for all of them to quit.
     */
    u32Release = 0;
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectLeave(&CritSect), VINF_SUCCESS, 1);
    while (u32Release < cThreads)
        RTThreadSleep(10);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectDelete(&CritSect), VINF_SUCCESS, 1);
    return 0;
}



/**
 * Thread which goes to sleep on the critsect and checks
 * that it's released along and in the right order. This is done a number of times.
 *
 */
static DECLCALLBACK(int) ThreadTest2(RTTHREAD ThreadSelf, void *pvArg)
{
    RT_NOREF1(ThreadSelf);
    PTHREADTEST2ARGS pArgs = (PTHREADTEST2ARGS)pvArg;
    Log2(("ThreadTest2: Start - iThread=%d ThreadSelf=%p\n", pArgs->iThread, ThreadSelf));
    uint64_t    u64TSStart = 0;
    ASMAtomicIncU32(pArgs->pcThreadRunning);

    for (unsigned i = 0; *pArgs->pu32Release < pArgs->cIterations; i++)
    {
        /*
         * Enter it.
         */
        int rc = RTCritSectEnter(pArgs->pCritSect);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(g_hTest, "thread %d, iteration %d: RTCritSectEnter -> %d", pArgs->iThread, i, rc);
            return 1;
        }
        if (!u64TSStart)
            u64TSStart = RTTimeNanoTS();

#if 0 /* We just check for sequences. */
        /*
         * Check release order.
         */
        if ((*pArgs->pu32Release % pArgs->cThreads) != pArgs->iThread)
            RTTestFailed(g_hTest, "thread %d, iteration %d: released as number %d (%d)",
                         pArgs->iThread, i, *pArgs->pu32Release % pArgs->cThreads, *pArgs->pu32Release);
        else
            RTTestPrintf(g_hTest, RTTESTLVL_INFO, "iteration %d: released as number %d (%d)\n",
                         pArgs->iThread, i, *pArgs->pu32Release % pArgs->cThreads, *pArgs->pu32Release);
#endif
        pArgs->cTimes++;
        ASMAtomicIncU32(pArgs->pu32Release);

        /*
         * Check distribution every now and again.
         */
#if 0
        if (!(*pArgs->pu32Release % 879))
        {
            uint32_t u32Perfect = *pArgs->pu32Release / pArgs->cThreads;
            for (int iThread = 0 ; iThread < (int)pArgs->cThreads; iThread++)
            {
                int cDiff = pArgs[iThread - pArgs->iThread].cTimes - u32Perfect;
                if ((unsigned)RT_ABS(cDiff) > RT_MAX(u32Perfect / 10000, 2))
                {
                    printf("tstCritSect: FAILURE - bad distribution thread %d u32Perfect=%d cTimes=%d cDiff=%d (runtime)\n",
                           iThread, u32Perfect, pArgs[iThread - pArgs->iThread].cTimes, cDiff);
                    ASMAtomicIncU32(&g_cErrors);
                }
            }
        }
#endif
        /*
         * Check alone and make sure we stay inside here a while
         * so the other guys can get ready.
         */
        uint32_t u32;
        for (u32 = 0; u32 < pArgs->cCheckLoops; u32++)
        {
            if (*pArgs->pu32Alone != ~0U)
            {
                RTTestFailed(g_hTest, "thread %d, iteration %d: not alone!!!", pArgs->iThread, i);
                //AssertReleaseMsgFailed(("Not alone!\n"));
                return 1;
            }
        }
        ASMAtomicCmpXchgU32(pArgs->pu32Alone, pArgs->iThread, UINT32_MAX);
        for (u32 = 0; u32 < pArgs->cCheckLoops; u32++)
        {
            if (*pArgs->pu32Alone != pArgs->iThread)
            {
                RTTestFailed(g_hTest, "thread %d, iteration %d: not alone!!!", pArgs->iThread, i);
                //AssertReleaseMsgFailed(("Not alone!\n"));
                return 1;
            }
        }
        ASMAtomicXchgU32(pArgs->pu32Alone, UINT32_MAX);

        /*
         * Check for sequences.
         */
        if (*pArgs->pu32Prev == pArgs->iThread && pArgs->cThreads > 1)
            ASMAtomicIncU32(pArgs->pcSeq);
        else if ((*pArgs->pu32Prev + 1) % pArgs->cThreads != pArgs->iThread)
            ASMAtomicIncU32(pArgs->pcReordered);
        ASMAtomicXchgU32(pArgs->pu32Prev, pArgs->iThread);

        /*
         * Leave it.
         */
        rc = RTCritSectLeave(pArgs->pCritSect);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(g_hTest, "thread %d, iteration %d: RTCritSectEnter -> %d", pArgs->iThread, i, rc);
            return 1;
        }
    }

    uint64_t u64TSEnd = RTTimeNanoTS(); NOREF(u64TSEnd);
    ASMAtomicDecU32(pArgs->pcThreadRunning);
    RTSemEventSignal(pArgs->EventDone);
    Log2(("ThreadTest2: End - iThread=%d ThreadSelf=%p time=%lld\n", pArgs->iThread, ThreadSelf, u64TSEnd - u64TSStart));
    return 0;
}

static int Test2(unsigned cThreads, unsigned cIterations, unsigned cCheckLoops)
{
    RTTestSubF(g_hTest, "Test #2 - cThreads=%u cIterations=%u cCheckLoops=%u", cThreads, cIterations, cCheckLoops);

    /*
     * Create a critical section.
     */
    RTCRITSECT CritSect;
    int rc;
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectInit(&CritSect), VINF_SUCCESS, 1);

    /*
     * Enter, leave and enter again.
     */
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectEnter(&CritSect), VINF_SUCCESS, 1);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectLeave(&CritSect), VINF_SUCCESS, 1);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectEnter(&CritSect), VINF_SUCCESS, 1);

    /*
     * Now spawn threads which will go to sleep entering the critsect.
     */
    PTHREADTEST2ARGS paArgs = (PTHREADTEST2ARGS)RTMemAllocZ(sizeof(THREADTEST2ARGS) * cThreads);
    RTSEMEVENT       EventDone;
    RTTEST_CHECK_RC_RET(g_hTest, RTSemEventCreate(&EventDone), VINF_SUCCESS, 1);
    uint32_t volatile   u32Release = 0;
    uint32_t volatile   u32Alone = UINT32_MAX;
    uint32_t volatile   u32Prev = UINT32_MAX;
    uint32_t volatile   cSeq = 0;
    uint32_t volatile   cReordered = 0;
    uint32_t volatile   cThreadRunning = 0;
    unsigned iThread;
    for (iThread = 0; iThread < cThreads; iThread++)
    {
        paArgs[iThread].iThread     = iThread;
        paArgs[iThread].pCritSect   = &CritSect;
        paArgs[iThread].pu32Release = &u32Release;
        paArgs[iThread].pu32Alone   = &u32Alone;
        paArgs[iThread].pu32Prev    = &u32Prev;
        paArgs[iThread].pcSeq       = &cSeq;
        paArgs[iThread].pcReordered = &cReordered;
        paArgs[iThread].pcThreadRunning = &cThreadRunning;
        paArgs[iThread].cTimes      = 0;
        paArgs[iThread].cThreads    = cThreads;
        paArgs[iThread].cIterations = cIterations;
        paArgs[iThread].cCheckLoops = cCheckLoops;
        paArgs[iThread].EventDone   = EventDone;
        int32_t     iLock = LOCKERS(CritSect);
        char szThread[17];
        RTStrPrintf(szThread, sizeof(szThread), "T%d", iThread);
        RTTHREAD  Thread;
        rc = RTThreadCreate(&Thread, ThreadTest2, &paArgs[iThread], 0, RTTHREADTYPE_DEFAULT, 0, szThread);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(g_hTest, "RTThreadCreate -> %d", rc);
            return 1;
        }
        /* wait for it to get into waiting. */
        while (LOCKERS(CritSect) == iLock)
            RTThreadSleep(10);
        RTThreadSleep(20);
    }
    RTTestPrintf(g_hTest, RTTESTLVL_INFO, "threads created...\n");

    /*
     * Now we'll release the threads and wait for all of them to quit.
     */
    u32Release = 0;
    uint64_t u64TSStart = RTTimeNanoTS();
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectLeave(&CritSect), VINF_SUCCESS, 1);

    while (cThreadRunning > 0)
        RTSemEventWait(EventDone, RT_INDEFINITE_WAIT);
    uint64_t u64TSEnd = RTTimeNanoTS();

    /*
     * Clean up and report results.
     */
    RTTEST_CHECK_RC(g_hTest, RTCritSectDelete(&CritSect), VINF_SUCCESS);

    /* sequences */
    if (cSeq > RT_MAX(u32Release / 10000, 1))
        RTTestFailed(g_hTest, "too many same thread sequences! cSeq=%d\n", cSeq);

    /* distribution caused by sequences / reordering. */
    unsigned cDiffTotal = 0;
    uint32_t u32Perfect = (u32Release + cThreads / 2) / cThreads;
    for (iThread = 0; iThread < cThreads; iThread++)
    {
        int cDiff = paArgs[iThread].cTimes - u32Perfect;
        if ((unsigned)RT_ABS(cDiff) > RT_MAX(u32Perfect / 10000, 2))
            RTTestFailed(g_hTest, "bad distribution thread %d u32Perfect=%d cTimes=%d cDiff=%d\n",
                         iThread, u32Perfect, paArgs[iThread].cTimes, cDiff);
        cDiffTotal += RT_ABS(cDiff);
    }

    uint32_t cMillies = (uint32_t)((u64TSEnd - u64TSStart) / 1000000);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "%d enter+leave in %dms cSeq=%d cReordered=%d cDiffTotal=%d\n",
                 u32Release, cMillies, cSeq, cReordered, cDiffTotal);
    return 0;
}


int main(int argc, char **argv)
{
    RTTEST hTest;
#ifndef TRY_WIN32_CRT
    int rc = RTTestInitAndCreate("tstRTCritSect", &hTest);
#else
    int rc = RTTestInitAndCreate("tstRTCritSectW32", &hTest);
#endif
    if (rc)
        return rc;
    RTTestBanner(hTest);
    g_hTest = hTest;

    /* parse args. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--distribution", 'd', RTGETOPT_REQ_NOTHING },
        { "--help",         'h', RTGETOPT_REQ_NOTHING }
    };

    bool fTestDistribution = false;

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'd':
                fTestDistribution = true;
                break;

            case 'h':
                RTTestIPrintf(RTTESTLVL_ALWAYS, "%s [--help|-h] [--distribution|-d]\n", argv[0]);
                return 1;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return 0;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }


    /*
     * Perform the testing.
     */
    if (    !Test1(1)
        &&  !Test1(3)
        &&  !Test1(10)
        &&  !Test1(63))
    {

        if (    fTestDistribution
            &&  !Test2(1, 200000, 1000)
            &&  !Test2(2, 200000, 1000)
            &&  !Test2(3, 200000, 1000)
            &&  !Test2(4, 200000, 1000)
            &&  !Test2(5, 200000, 1000)
            &&  !Test2(7, 200000, 1000)
            &&  !Test2(67, 200000, 1000))
        {
            /*nothing*/;
        }
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

