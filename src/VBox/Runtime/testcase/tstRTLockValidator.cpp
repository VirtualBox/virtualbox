/* $Id: tstRTLockValidator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTLockValidator.
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
#include <iprt/lockvalidator.h>

#include <iprt/asm.h>                   /* for return addresses */
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/semaphore.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SECS_SIMPLE_TEST    1
#define SECS_RACE_TEST      3
#define TEST_SMALL_TIMEOUT  (  10*1000)
#define TEST_LARGE_TIMEOUT  (  60*1000)
#define TEST_DEBUG_TIMEOUT  (3600*1000)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The testcase handle. */
static RTTEST               g_hTest;
/** Flip this in the debugger to get some peace to single step wild code. */
bool volatile               g_fDoNotSpin = false;

/** Set when the main thread wishes to terminate the test. */
bool volatile               g_fShutdown = false;
/** The number of threads. */
static uint32_t             g_cThreads;
static uint32_t             g_iDeadlockThread;
static RTTHREAD             g_ahThreads[32];
static RTLOCKVALCLASS       g_ahClasses[32];
static RTCRITSECT           g_aCritSects[32];
static RTSEMRW              g_ahSemRWs[32];
static RTSEMMUTEX           g_ahSemMtxes[32];
static RTSEMEVENT           g_hSemEvt;
static RTSEMEVENTMULTI      g_hSemEvtMulti;

/** Multiple release event semaphore that is signalled by the main thread after
 * it has started all the threads. */
static RTSEMEVENTMULTI      g_hThreadsStartedEvt;

/** The number of threads that have called testThreadBlocking */
static uint32_t volatile    g_cThreadsBlocking;
/** Multiple release event semaphore that is signalled by the last thread to
 * call testThreadBlocking.  testWaitForAllOtherThreadsToSleep waits on this. */
static RTSEMEVENTMULTI      g_hThreadsBlockingEvt;

/** When to stop testing. */
static uint64_t             g_NanoTSStop;
/** The number of deadlocks. */
static uint32_t volatile    g_cDeadlocks;
/** The number of loops. */
static uint32_t volatile    g_cLoops;


/**
 * Spin until the callback stops returning VERR_TRY_AGAIN.
 *
 * @returns Callback result. VERR_TIMEOUT if too much time elapses.
 * @param   pfnCallback     Callback for checking the state.
 * @param   pvWhat          Callback parameter.
 */
static int testWaitForSomethingToBeOwned(int (*pfnCallback)(void *), void *pvWhat)
{
    RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
    RTTEST_CHECK_RC_OK(g_hTest, RTSemEventMultiWait(g_hThreadsStartedEvt, TEST_SMALL_TIMEOUT));

    uint64_t u64StartMS = RTTimeMilliTS();
    for (unsigned iLoop = 0; ; iLoop++)
    {
        RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);

        int rc = pfnCallback(pvWhat);
        if (rc != VERR_TRY_AGAIN/* && !g_fDoNotSpin*/)
        {
            RTTEST_CHECK_RC_OK(g_hTest, rc);
            return rc;
        }

        uint64_t cMsElapsed = RTTimeMilliTS() - u64StartMS;
        if (!g_fDoNotSpin)
            RTTEST_CHECK_RET(g_hTest, cMsElapsed <= TEST_SMALL_TIMEOUT, VERR_TIMEOUT);

        RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);
        RTThreadSleep(/*g_fDoNotSpin ? TEST_DEBUG_TIMEOUT :*/ iLoop > 256 ? 1 : 0);
    }
}


static int testCheckIfCritSectIsOwned(void *pvWhat)
{
    PRTCRITSECT pCritSect = (PRTCRITSECT)pvWhat;
    if (!RTCritSectIsInitialized(pCritSect))
        return VERR_SEM_DESTROYED;
    if (RTCritSectIsOwned(pCritSect))
        return VINF_SUCCESS;
    return VERR_TRY_AGAIN;
}


static int testWaitForCritSectToBeOwned(PRTCRITSECT pCritSect)
{
    return testWaitForSomethingToBeOwned(testCheckIfCritSectIsOwned, pCritSect);
}


static int testCheckIfSemRWIsOwned(void *pvWhat)
{
    RTSEMRW hSemRW = (RTSEMRW)pvWhat;
    if (RTSemRWGetWriteRecursion(hSemRW) > 0)
        return VINF_SUCCESS;
    if (RTSemRWGetReadCount(hSemRW) > 0)
        return VINF_SUCCESS;
    return VERR_TRY_AGAIN;
}

static int testWaitForSemRWToBeOwned(RTSEMRW hSemRW)
{
    return testWaitForSomethingToBeOwned(testCheckIfSemRWIsOwned, hSemRW);
}


static int testCheckIfSemMutexIsOwned(void *pvWhat)
{
    RTSEMMUTEX hSemRW = (RTSEMMUTEX)pvWhat;
    if (RTSemMutexIsOwned(hSemRW))
        return VINF_SUCCESS;
    return VERR_TRY_AGAIN;
}

static int testWaitForSemMutexToBeOwned(RTSEMMUTEX hSemMutex)
{
    return testWaitForSomethingToBeOwned(testCheckIfSemMutexIsOwned, hSemMutex);
}


/**
 * For reducing spin in testWaitForAllOtherThreadsToSleep.
 */
static void testThreadBlocking(void)
{
    if (ASMAtomicIncU32(&g_cThreadsBlocking) == g_cThreads)
        RTTEST_CHECK_RC_OK(g_hTest, RTSemEventMultiSignal(g_hThreadsBlockingEvt));
}


/**
 * Waits for all the other threads to enter sleeping states.
 *
 * @returns VINF_SUCCESS on success, VERR_INTERNAL_ERROR on failure.
 * @param   enmDesiredState     The desired thread sleep state.
 * @param   cWaitOn             The distance to the lock they'll be waiting on,
 *                              the lock type is derived from the desired state.
 *                              UINT32_MAX means no special lock.
 */
static int testWaitForAllOtherThreadsToSleep(RTTHREADSTATE enmDesiredState, uint32_t cWaitOn)
{
    testThreadBlocking();
    RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
    RTTEST_CHECK_RC_OK(g_hTest, RTSemEventMultiWait(g_hThreadsBlockingEvt, TEST_SMALL_TIMEOUT));

    RTTHREAD hThreadSelf = RTThreadSelf();
    for (uint32_t iOuterLoop = 0; ; iOuterLoop++)
    {
        uint32_t cMissing  = 0;
        uint32_t cWaitedOn = 0;
        for (uint32_t i = 0; i < g_cThreads; i++)
        {
            RTTHREAD hThread = g_ahThreads[i];
            if (hThread == NIL_RTTHREAD)
                cMissing++;
            else if (hThread != hThreadSelf)
            {
                /*
                 * Figure out which lock to wait for.
                 */
                void *pvLock = NULL;
                if (cWaitOn != UINT32_MAX)
                {
                    uint32_t j = (i + cWaitOn) % g_cThreads;
                    switch (enmDesiredState)
                    {
                        case RTTHREADSTATE_CRITSECT:    pvLock = &g_aCritSects[j]; break;
                        case RTTHREADSTATE_RW_WRITE:
                        case RTTHREADSTATE_RW_READ:     pvLock = g_ahSemRWs[j]; break;
                        case RTTHREADSTATE_MUTEX:       pvLock = g_ahSemMtxes[j]; break;
                        default: break;
                    }
                }

                /*
                 * Wait for this thread.
                 */
                for (unsigned iLoop = 0; ; iLoop++)
                {
                    RTTHREADSTATE enmState = RTThreadGetReallySleeping(hThread);
                    if (RTTHREAD_IS_SLEEPING(enmState))
                    {
                        if (   enmState == enmDesiredState
                            && (   !pvLock
                                || (   pvLock == RTLockValidatorQueryBlocking(hThread)
                                    && !RTLockValidatorIsBlockedThreadInValidator(hThread) )
                               )
                            && RTThreadGetNativeState(hThread) != RTTHREADNATIVESTATE_RUNNING
                           )
                            break;
                    }
                    else if (   enmState != RTTHREADSTATE_RUNNING
                             && enmState != RTTHREADSTATE_INITIALIZING)
                        return VERR_INTERNAL_ERROR;
                    RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);
                    RTThreadSleep(g_fDoNotSpin ? TEST_DEBUG_TIMEOUT : iOuterLoop + iLoop > 256 ? 1 : 0);
                    RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);
                    cWaitedOn++;
                }
            }
            RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);
        }

        if (!cMissing && !cWaitedOn)
            break;
        RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);
        RTThreadSleep(g_fDoNotSpin ? TEST_DEBUG_TIMEOUT : iOuterLoop > 256 ? 1 : 0);
        RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);
    }

    RTThreadSleep(0);                   /* fudge factor */
    RTTEST_CHECK_RET(g_hTest, !g_fShutdown, VERR_INTERNAL_ERROR);
    return VINF_SUCCESS;
}


/**
 * Worker that starts the threads.
 *
 * @returns Same as RTThreadCreate.
 * @param   cThreads            The number of threads to start.
 * @param   pfnThread           Thread function.
 */
static int testStartThreads(uint32_t cThreads, PFNRTTHREAD pfnThread)
{
    RTSemEventMultiReset(g_hThreadsStartedEvt);

    for (uint32_t i = 0; i < RT_ELEMENTS(g_ahThreads); i++)
        g_ahThreads[i] = NIL_RTTHREAD;

    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < cThreads; i++)
    {
        rc = RTThreadCreateF(&g_ahThreads[i], pfnThread, (void *)(uintptr_t)i, 0,
                             RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "thread-%02u", i);
        RTTEST_CHECK_RC_OK(g_hTest, rc);
        if (RT_FAILURE(rc))
            break;
    }

    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemEventMultiSignal(g_hThreadsStartedEvt), rcCheck);
    return rc;
}


/**
 * Worker that waits for the threads to complete.
 *
 * @param   cMillies            How long to wait for each.
 * @param   fStopOnError        Whether to stop on error and heed the thread
 *                              return status.
 */
static void testWaitForThreads(uint32_t cMillies, bool fStopOnError)
{
    uint32_t i = RT_ELEMENTS(g_ahThreads);
    while (i-- > 0)
        if (g_ahThreads[i] != NIL_RTTHREAD)
        {
            int rcThread;
            int rc2;
            RTTEST_CHECK_RC_OK(g_hTest, rc2 = RTThreadWait(g_ahThreads[i], cMillies, &rcThread));
            if (RT_SUCCESS(rc2))
                g_ahThreads[i] = NIL_RTTHREAD;
            if (fStopOnError && (RT_FAILURE(rc2) || RT_FAILURE(rcThread)))
                return;
        }
}


static void testIt(uint32_t cThreads, uint32_t cSecs, bool fLoops, PFNRTTHREAD pfnThread, const char *pszName)
{
    /*
     * Init test.
     */
    if (cSecs > 0)
        RTTestSubF(g_hTest, "%s, %u threads, %u secs", pszName, cThreads, cSecs);
    else
        RTTestSubF(g_hTest, "%s, %u threads, single pass", pszName, cThreads);

    RTTEST_CHECK_RETV(g_hTest, RT_ELEMENTS(g_ahThreads) >= cThreads);
    RTTEST_CHECK_RETV(g_hTest, RT_ELEMENTS(g_aCritSects) >= cThreads);

    g_cThreads = cThreads;
    g_fShutdown = false;

    for (uint32_t i = 0; i < cThreads; i++)
    {
        RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectInitEx(&g_aCritSects[i], 0 /*fFlags*/, NIL_RTLOCKVALCLASS,
                                                       RTLOCKVAL_SUB_CLASS_ANY, "RTCritSect"), VINF_SUCCESS);
        RTTEST_CHECK_RC_RETV(g_hTest, RTSemRWCreateEx(&g_ahSemRWs[i], 0 /*fFlags*/, NIL_RTLOCKVALCLASS,
                                                       RTLOCKVAL_SUB_CLASS_ANY, "RTSemRW"), VINF_SUCCESS);
        RTTEST_CHECK_RC_RETV(g_hTest, RTSemMutexCreateEx(&g_ahSemMtxes[i], 0 /*fFlags*/, NIL_RTLOCKVALCLASS,
                                                         RTLOCKVAL_SUB_CLASS_ANY, "RTSemMutex"), VINF_SUCCESS);
    }
    RTTEST_CHECK_RC_RETV(g_hTest, RTSemEventCreate(&g_hSemEvt), VINF_SUCCESS);
    RTTEST_CHECK_RC_RETV(g_hTest, RTSemEventMultiCreate(&g_hSemEvtMulti), VINF_SUCCESS);
    RTTEST_CHECK_RC_RETV(g_hTest, RTSemEventMultiCreate(&g_hThreadsStartedEvt), VINF_SUCCESS);
    RTTEST_CHECK_RC_RETV(g_hTest, RTSemEventMultiCreate(&g_hThreadsBlockingEvt), VINF_SUCCESS);

    /*
     * The test loop.
     */
    uint32_t cPasses    = 0;
    uint32_t cLoops     = 0;
    uint32_t cDeadlocks = 0;
    uint32_t cErrors    = RTTestErrorCount(g_hTest);
    uint64_t uStartNS   = RTTimeNanoTS();
    g_NanoTSStop        = uStartNS + cSecs * UINT64_C(1000000000);
    do
    {
        g_iDeadlockThread  = (cThreads - 1 + cPasses) % cThreads;
        g_cLoops           = 0;
        g_cDeadlocks       = 0;
        g_cThreadsBlocking = 0;
        RTTEST_CHECK_RC(g_hTest, RTSemEventMultiReset(g_hThreadsBlockingEvt), VINF_SUCCESS);

        int rc = testStartThreads(cThreads, pfnThread);
        if (RT_SUCCESS(rc))
        {
            testWaitForThreads(TEST_LARGE_TIMEOUT + cSecs*1000, true);
            if (g_fDoNotSpin && RTTestErrorCount(g_hTest) != cErrors)
                testWaitForThreads(TEST_DEBUG_TIMEOUT, true);
        }

        RTTEST_CHECK(g_hTest, !fLoops || g_cLoops > 0);
        cLoops += g_cLoops;
        RTTEST_CHECK(g_hTest, !fLoops || g_cDeadlocks > 0);
        cDeadlocks += g_cDeadlocks;
        cPasses++;
    } while (   RTTestErrorCount(g_hTest) == cErrors
             && !fLoops
             && RTTimeNanoTS() < g_NanoTSStop);

    /*
     * Cleanup.
     */
    ASMAtomicWriteBool(&g_fShutdown, true);
    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiSignal(g_hThreadsBlockingEvt), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiSignal(g_hThreadsStartedEvt), VINF_SUCCESS);
    RTThreadSleep(RTTestErrorCount(g_hTest) == cErrors ? 0 : 50);

    for (uint32_t i = 0; i < cThreads; i++)
    {
        RTTEST_CHECK_RC(g_hTest, RTCritSectDelete(&g_aCritSects[i]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTSemRWDestroy(g_ahSemRWs[i]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTSemMutexDestroy(g_ahSemMtxes[i]), VINF_SUCCESS);
    }
    RTTEST_CHECK_RC(g_hTest, RTSemEventDestroy(g_hSemEvt), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiDestroy(g_hSemEvtMulti), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiDestroy(g_hThreadsStartedEvt), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiDestroy(g_hThreadsBlockingEvt), VINF_SUCCESS);

    testWaitForThreads(TEST_SMALL_TIMEOUT, false);

    /*
     * Print results if applicable.
     */
    if (cSecs)
    {
        if (fLoops)
            RTTestPrintf(g_hTest,  RTTESTLVL_ALWAYS, "cLoops=%u cDeadlocks=%u (%u%%)\n",
                         cLoops, cDeadlocks, cLoops ? cDeadlocks * 100 / cLoops : 0);
        else
            RTTestPrintf(g_hTest,  RTTESTLVL_ALWAYS, "cPasses=%u\n", cPasses);
    }
}


static DECLCALLBACK(int) testDd1Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    uintptr_t       i     = (uintptr_t)pvUser;
    PRTCRITSECT     pMine = &g_aCritSects[i];
    PRTCRITSECT     pNext = &g_aCritSects[(i + 1) % g_cThreads];
    RT_NOREF_PV(ThreadSelf);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectEnter(pMine), VINF_SUCCESS, rcCheck);
    if (!(i & 1))
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(pMine), VINF_SUCCESS);
    if (RT_SUCCESS(testWaitForCritSectToBeOwned(pNext)))
    {
        int rc;
        if (i != g_iDeadlockThread)
        {
            testThreadBlocking();
            RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(pNext), VINF_SUCCESS);
        }
        else
        {
            RTTEST_CHECK_RC_OK(g_hTest, rc = testWaitForAllOtherThreadsToSleep(RTTHREADSTATE_CRITSECT, 1));
            if (RT_SUCCESS(rc))
                RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(pNext), VERR_SEM_LV_DEADLOCK);
        }
        RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
        if (RT_SUCCESS(rc))
            RTTEST_CHECK_RC(g_hTest, rc = RTCritSectLeave(pNext), VINF_SUCCESS);
    }
    if (!(i & 1))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(pMine), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(pMine), VINF_SUCCESS);
    return VINF_SUCCESS;
}


static void testDd1(uint32_t cThreads, uint32_t cSecs)
{
    testIt(cThreads, cSecs, false, testDd1Thread, "deadlock, critsect");
}


static DECLCALLBACK(int) testDd2Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    uintptr_t       i     = (uintptr_t)pvUser;
    RTSEMRW         hMine = g_ahSemRWs[i];
    RTSEMRW         hNext = g_ahSemRWs[(i + 1) % g_cThreads];
    int             rc;
    RT_NOREF_PV(ThreadSelf);

    if (i & 1)
    {
        RTTEST_CHECK_RC_RET(g_hTest, RTSemRWRequestWrite(hMine, RT_INDEFINITE_WAIT), VINF_SUCCESS, rcCheck);
        if ((i & 3) == 3)
            RTTEST_CHECK_RC(g_hTest, RTSemRWRequestWrite(hMine, RT_INDEFINITE_WAIT), VINF_SUCCESS);
    }
    else
        RTTEST_CHECK_RC_RET(g_hTest, RTSemRWRequestRead(hMine, RT_INDEFINITE_WAIT), VINF_SUCCESS, rcCheck);
    if (RT_SUCCESS(testWaitForSemRWToBeOwned(hNext)))
    {
        if (i != g_iDeadlockThread)
        {
            testThreadBlocking();
            RTTEST_CHECK_RC(g_hTest, rc = RTSemRWRequestWrite(hNext, RT_INDEFINITE_WAIT), VINF_SUCCESS);
        }
        else
        {
            RTTEST_CHECK_RC_OK(g_hTest, rc = testWaitForAllOtherThreadsToSleep(RTTHREADSTATE_RW_WRITE, 1));
            if (RT_SUCCESS(rc))
            {
                if (g_cThreads > 1)
                    RTTEST_CHECK_RC(g_hTest, rc = RTSemRWRequestWrite(hNext, RT_INDEFINITE_WAIT), VERR_SEM_LV_DEADLOCK);
                else
                    RTTEST_CHECK_RC(g_hTest, rc = RTSemRWRequestWrite(hNext, RT_INDEFINITE_WAIT), VERR_SEM_LV_ILLEGAL_UPGRADE);
            }
        }
        RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
        if (RT_SUCCESS(rc))
            RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(hNext), VINF_SUCCESS);
    }
    if (i & 1)
    {
        if ((i & 3) == 3)
            RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(hMine), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(hMine), VINF_SUCCESS);
    }
    else
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead(hMine), VINF_SUCCESS);
    RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
    return VINF_SUCCESS;
}


static void testDd2(uint32_t cThreads, uint32_t cSecs)
{
    testIt(cThreads, cSecs, false, testDd2Thread, "deadlock, read-write");
}


static DECLCALLBACK(int) testDd3Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    uintptr_t       i     = (uintptr_t)pvUser;
    RTSEMRW         hMine = g_ahSemRWs[i];
    RTSEMRW         hNext = g_ahSemRWs[(i + 1) % g_cThreads];
    int             rc;
    RT_NOREF_PV(ThreadSelf);

    if (i & 1)
        RTTEST_CHECK_RC_RET(g_hTest, RTSemRWRequestWrite(hMine, RT_INDEFINITE_WAIT), VINF_SUCCESS, rcCheck);
    else
        RTTEST_CHECK_RC_RET(g_hTest, RTSemRWRequestRead(hMine, RT_INDEFINITE_WAIT), VINF_SUCCESS, rcCheck);
    if (RT_SUCCESS(testWaitForSemRWToBeOwned(hNext)))
    {
        do
        {
            rc = RTSemRWRequestWrite(hNext, TEST_SMALL_TIMEOUT);
            if (rc != VINF_SUCCESS && rc != VERR_SEM_LV_DEADLOCK && rc != VERR_SEM_LV_ILLEGAL_UPGRADE)
            {
                RTTestFailed(g_hTest, "#%u: RTSemRWRequestWrite -> %Rrc\n", i, rc);
                break;
            }
            if (RT_SUCCESS(rc))
            {
                RTTEST_CHECK_RC(g_hTest, rc = RTSemRWReleaseWrite(hNext), VINF_SUCCESS);
                if (RT_FAILURE(rc))
                    break;
            }
            else
                ASMAtomicIncU32(&g_cDeadlocks);
            ASMAtomicIncU32(&g_cLoops);
        } while (RTTimeNanoTS() < g_NanoTSStop);
    }
    if (i & 1)
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(hMine), VINF_SUCCESS);
    else
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead(hMine), VINF_SUCCESS);
    RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
    return VINF_SUCCESS;
}


static void testDd3(uint32_t cThreads, uint32_t cSecs)
{
    testIt(cThreads, cSecs, true, testDd3Thread, "deadlock, read-write race");
}


static DECLCALLBACK(int) testDd4Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    uintptr_t       i     = (uintptr_t)pvUser;
    RTSEMRW         hMine = g_ahSemRWs[i];
    RTSEMRW         hNext = g_ahSemRWs[(i + 1) % g_cThreads];
    RT_NOREF_PV(ThreadSelf);

    do
    {
        int rc1 = (i & 1 ? RTSemRWRequestWrite : RTSemRWRequestRead)(hMine, TEST_SMALL_TIMEOUT); /* ugly ;-) */
        RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
        if (rc1 != VINF_SUCCESS && rc1 != VERR_SEM_LV_DEADLOCK && rc1 != VERR_SEM_LV_ILLEGAL_UPGRADE)
        {
            RTTestFailed(g_hTest, "#%u: RTSemRWRequest%s(hMine,) -> %Rrc\n", i, i & 1 ? "Write" : "read", rc1);
            break;
        }
        if (RT_SUCCESS(rc1))
        {
            for (unsigned iInner = 0; iInner < 4; iInner++)
            {
                int rc2 = RTSemRWRequestWrite(hNext, TEST_SMALL_TIMEOUT);
                if (rc2 != VINF_SUCCESS && rc2 != VERR_SEM_LV_DEADLOCK && rc2 != VERR_SEM_LV_ILLEGAL_UPGRADE)
                {
                    RTTestFailed(g_hTest, "#%u: RTSemRWRequestWrite -> %Rrc\n", i, rc2);
                    break;
                }
                if (RT_SUCCESS(rc2))
                {
                    RTTEST_CHECK_RC(g_hTest, rc2 = RTSemRWReleaseWrite(hNext), VINF_SUCCESS);
                    if (RT_FAILURE(rc2))
                        break;
                }
                else
                    ASMAtomicIncU32(&g_cDeadlocks);
                ASMAtomicIncU32(&g_cLoops);
            }

            RTTEST_CHECK_RC(g_hTest, rc1 = (i & 1 ? RTSemRWReleaseWrite : RTSemRWReleaseRead)(hMine), VINF_SUCCESS);
            RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
            if (RT_FAILURE(rc1))
                break;
        }
        else
            ASMAtomicIncU32(&g_cDeadlocks);
        ASMAtomicIncU32(&g_cLoops);
    } while (RTTimeNanoTS() < g_NanoTSStop);

    return VINF_SUCCESS;
}


static void testDd4(uint32_t cThreads, uint32_t cSecs)
{
    testIt(cThreads, cSecs, true, testDd4Thread, "deadlock, read-write race v2");
}


static DECLCALLBACK(int) testDd5Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    uintptr_t       i     = (uintptr_t)pvUser;
    RTSEMMUTEX      hMine = g_ahSemMtxes[i];
    RTSEMMUTEX      hNext = g_ahSemMtxes[(i + 1) % g_cThreads];
    RT_NOREF_PV(ThreadSelf);

    RTTEST_CHECK_RC_RET(g_hTest, RTSemMutexRequest(hMine, RT_INDEFINITE_WAIT), VINF_SUCCESS, rcCheck);
    if (i & 1)
        RTTEST_CHECK_RC(g_hTest, RTSemMutexRequest(hMine, RT_INDEFINITE_WAIT), VINF_SUCCESS);
    if (RT_SUCCESS(testWaitForSemMutexToBeOwned(hNext)))
    {
        int rc;
        if (i != g_iDeadlockThread)
        {
            testThreadBlocking();
            RTTEST_CHECK_RC(g_hTest, rc = RTSemMutexRequest(hNext, RT_INDEFINITE_WAIT), VINF_SUCCESS);
        }
        else
        {
            RTTEST_CHECK_RC_OK(g_hTest, rc = testWaitForAllOtherThreadsToSleep(RTTHREADSTATE_MUTEX, 1));
            if (RT_SUCCESS(rc))
                RTTEST_CHECK_RC(g_hTest, rc = RTSemMutexRequest(hNext, RT_INDEFINITE_WAIT), VERR_SEM_LV_DEADLOCK);
        }
        RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
        if (RT_SUCCESS(rc))
            RTTEST_CHECK_RC(g_hTest, rc = RTSemMutexRelease(hNext), VINF_SUCCESS);
    }
    if (i & 1)
        RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(hMine), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(hMine), VINF_SUCCESS);
    return VINF_SUCCESS;
}


static void testDd5(uint32_t cThreads, uint32_t cSecs)
{
    testIt(cThreads, cSecs, false, testDd5Thread, "deadlock, mutex");
}


static DECLCALLBACK(int) testDd6Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    uintptr_t       i     = (uintptr_t)pvUser;
    PRTCRITSECT     pMine = &g_aCritSects[i];
    PRTCRITSECT     pNext = &g_aCritSects[(i + 1) % g_cThreads];
    RT_NOREF_PV(ThreadSelf);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectEnter(pMine), VINF_SUCCESS, rcCheck);
    if (i & 1)
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(pMine), VINF_SUCCESS);
    if (RT_SUCCESS(testWaitForCritSectToBeOwned(pNext)))
    {
        int rc;
        if (i != g_iDeadlockThread)
        {
            testThreadBlocking();
            RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(pNext), VINF_SUCCESS);
            RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
            if (RT_SUCCESS(rc))
                RTTEST_CHECK_RC(g_hTest, rc = RTCritSectLeave(pNext), VINF_SUCCESS);
        }
        else
        {
            RTTEST_CHECK_RC_OK(g_hTest, rc = testWaitForAllOtherThreadsToSleep(RTTHREADSTATE_CRITSECT, 1));
            if (RT_SUCCESS(rc))
            {
                RTSemEventSetSignaller(g_hSemEvt, g_ahThreads[0]);
                for (uint32_t iThread = 1; iThread < g_cThreads; iThread++)
                    RTSemEventAddSignaller(g_hSemEvt, g_ahThreads[iThread]);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTTEST_CHECK_RC(g_hTest, RTSemEventWait(g_hSemEvt, TEST_SMALL_TIMEOUT), VERR_SEM_LV_DEADLOCK);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTTEST_CHECK_RC(g_hTest, RTSemEventSignal(g_hSemEvt), VINF_SUCCESS);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTTEST_CHECK_RC(g_hTest, RTSemEventWait(g_hSemEvt, TEST_SMALL_TIMEOUT), VINF_SUCCESS);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTSemEventSetSignaller(g_hSemEvt, NIL_RTTHREAD);
            }
        }
        RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
    }
    if (i & 1)
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(pMine), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(pMine), VINF_SUCCESS);
    return VINF_SUCCESS;
}


static void testDd6(uint32_t cThreads, uint32_t cSecs)
{
    testIt(cThreads, cSecs, false, testDd6Thread, "deadlock, event");
}


static DECLCALLBACK(int) testDd7Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    uintptr_t       i     = (uintptr_t)pvUser;
    PRTCRITSECT     pMine = &g_aCritSects[i];
    PRTCRITSECT     pNext = &g_aCritSects[(i + 1) % g_cThreads];
    RT_NOREF_PV(ThreadSelf);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectEnter(pMine), VINF_SUCCESS, rcCheck);
    if (i & 1)
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(pMine), VINF_SUCCESS);
    if (RT_SUCCESS(testWaitForCritSectToBeOwned(pNext)))
    {
        int rc;
        if (i != g_iDeadlockThread)
        {
            testThreadBlocking();
            RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(pNext), VINF_SUCCESS);
            RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
            if (RT_SUCCESS(rc))
                RTTEST_CHECK_RC(g_hTest, rc = RTCritSectLeave(pNext), VINF_SUCCESS);
        }
        else
        {
            RTTEST_CHECK_RC_OK(g_hTest, rc = testWaitForAllOtherThreadsToSleep(RTTHREADSTATE_CRITSECT, 1));
            if (RT_SUCCESS(rc))
            {
                RTSemEventMultiSetSignaller(g_hSemEvtMulti, g_ahThreads[0]);
                for (uint32_t iThread = 1; iThread < g_cThreads; iThread++)
                    RTSemEventMultiAddSignaller(g_hSemEvtMulti, g_ahThreads[iThread]);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTTEST_CHECK_RC(g_hTest, RTSemEventMultiReset(g_hSemEvtMulti), VINF_SUCCESS);
                RTTEST_CHECK_RC(g_hTest, RTSemEventMultiWait(g_hSemEvtMulti, TEST_SMALL_TIMEOUT), VERR_SEM_LV_DEADLOCK);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTTEST_CHECK_RC(g_hTest, RTSemEventMultiSignal(g_hSemEvtMulti), VINF_SUCCESS);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTTEST_CHECK_RC(g_hTest, RTSemEventMultiWait(g_hSemEvtMulti, TEST_SMALL_TIMEOUT), VINF_SUCCESS);
                RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
                RTSemEventMultiSetSignaller(g_hSemEvtMulti, NIL_RTTHREAD);
            }
        }
        RTTEST_CHECK(g_hTest, RTThreadGetState(RTThreadSelf()) == RTTHREADSTATE_RUNNING);
    }
    if (i & 1)
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(pMine), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(pMine), VINF_SUCCESS);
    return VINF_SUCCESS;
}


static void testDd7(uint32_t cThreads, uint32_t cSecs)
{
    testIt(cThreads, cSecs, false, testDd7Thread, "deadlock, event multi");
}


static void testLo1(void)
{
    RTTestSub(g_hTest, "locking order basics");

    /* Initialize the critsections, the first 4 has their own classes, the rest
       use the same class and relies on the sub-class mechanism for ordering.  */
    for (unsigned i = 0; i < RT_ELEMENTS(g_ahClasses); i++)
    {
        if (i <= 3)
        {
            RTTEST_CHECK_RC_RETV(g_hTest, RTLockValidatorClassCreate(&g_ahClasses[i], true /*fAutodidact*/, RT_SRC_POS, "testLo1-%u", i), VINF_SUCCESS);
            RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectInitEx(&g_aCritSects[i], 0, g_ahClasses[i], RTLOCKVAL_SUB_CLASS_NONE, "RTCritSectLO-Auto"), VINF_SUCCESS);
            RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRetain(g_ahClasses[i]) == 3);
            RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRelease(g_ahClasses[i]) == 2);
        }
        else
        {
            g_ahClasses[i] = RTLockValidatorClassForSrcPos(RT_SRC_POS, "testLo1-%u", i);
            RTTEST_CHECK_RETV(g_hTest, g_ahClasses[i] != NIL_RTLOCKVALCLASS);
            RTTEST_CHECK_RETV(g_hTest, i == 4 || g_ahClasses[i] == g_ahClasses[i - 1]);
            if (i == 4)
                RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectInitEx(&g_aCritSects[i], 0, g_ahClasses[i], RTLOCKVAL_SUB_CLASS_NONE,     "RTCritSectLO-None"), VINF_SUCCESS);
            else if (i == 5)
                RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectInitEx(&g_aCritSects[i], 0, g_ahClasses[i], RTLOCKVAL_SUB_CLASS_ANY,      "RTCritSectLO-Any"), VINF_SUCCESS);
            else
                RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectInitEx(&g_aCritSects[i], 0, g_ahClasses[i], RTLOCKVAL_SUB_CLASS_USER + i, "RTCritSectLO-User"), VINF_SUCCESS);

            RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRetain(g_ahClasses[i]) == 1 + (i - 4 + 1) * 2); /* released in cleanup. */
        }
    }

    /* Enter the first 4 critsects in ascending order and thereby defining
       this as a valid lock order.  */
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);

    /* Now, leave and re-enter the critsects in a way that should break the
       order and check that we get the appropriate response. */
    int rc;
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[0]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[1]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc= RTCritSectEnter(&g_aCritSects[2]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);

    /* Check that recursion isn't subject to order checks. */
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[0]), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[0]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[1]), VINF_SUCCESS);

        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);
    }
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);

    /* Enable strict release order for class 2 and check that violations
       are caught. */
    RTTEST_CHECK_RC(g_hTest, RTLockValidatorClassEnforceStrictReleaseOrder(g_ahClasses[2], true), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectLeave(&g_aCritSects[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    if (RT_FAILURE(rc))
    {
        /* applies to recursions as well */
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    }
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);

    /* Test that sub-class order works (4 = NONE, 5 = ANY, 6+ = USER). */
    uint32_t cErrorsBefore = RTTestErrorCount(g_hTest);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[7]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[4]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[4]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[5]), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[5]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[8]), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[8]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[6]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[6]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[7]), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[7]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[7]), VINF_SUCCESS);

    /* Check that NONE trumps both ANY and USER. */
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[4]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[5]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[5]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[6]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[6]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[4]), VINF_SUCCESS);

    /* Take all the locks using sub-classes. */
    if (cErrorsBefore == RTTestErrorCount(g_hTest))
    {
        bool fSavedQuiet = RTLockValidatorSetQuiet(true);
        for (uint32_t i = 6; i < RT_ELEMENTS(g_aCritSects); i++)
        {
            RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[i]), VINF_SUCCESS);
            RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[4]), VERR_SEM_LV_WRONG_ORDER);
            RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[5]), VINF_SUCCESS);
        }
        for (uint32_t i = 6; i < RT_ELEMENTS(g_aCritSects); i++)
        {
            RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[i]), VINF_SUCCESS);
            RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[5]), VINF_SUCCESS);
        }
        RTLockValidatorSetQuiet(fSavedQuiet);
    }

    /* Work up some hash statistics and trigger a violation to show them. */
    for (uint32_t i = 0; i < 10240; i++)
    {
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[0]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[1]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[5]), VINF_SUCCESS);

        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[5]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);
    }
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[5]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VERR_SEM_LV_WRONG_ORDER);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[5]), VINF_SUCCESS);

    /* clean up */
    //for (int i = RT_ELEMENTS(g_ahClasses) - 1; i >= 0; i--)
    for (unsigned i = 0; i < RT_ELEMENTS(g_ahClasses); i++)
    {
        uint32_t c;
        if (i <= 3)
            RTTEST_CHECK_MSG(g_hTest, (c = RTLockValidatorClassRelease(g_ahClasses[i])) == 5 - i,
                             (g_hTest, "c=%u i=%u\n", c, i));
        else
        {
            uint32_t cExpect = 1 + (RT_ELEMENTS(g_ahClasses) - i) * 2 - 1;
            RTTEST_CHECK_MSG(g_hTest, (c = RTLockValidatorClassRelease(g_ahClasses[i])) == cExpect,
                             (g_hTest, "c=%u e=%u i=%u\n", c, cExpect, i));
        }
        g_ahClasses[i] = NIL_RTLOCKVALCLASS;
        RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectDelete(&g_aCritSects[i]), VINF_SUCCESS);
    }
}


static void testLo2(void)
{
    RTTestSub(g_hTest, "locking order, critsect");

    /* Initialize the critsection with all different classes */
    for (unsigned i = 0; i < 4; i++)
    {
        RTTEST_CHECK_RC_RETV(g_hTest, RTLockValidatorClassCreate(&g_ahClasses[i], true /*fAutodidact*/, RT_SRC_POS, "testLo2-%u", i), VINF_SUCCESS);
        RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectInitEx(&g_aCritSects[i], 0, g_ahClasses[i], RTLOCKVAL_SUB_CLASS_NONE, "RTCritSectLO"), VINF_SUCCESS);
        RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRetain(g_ahClasses[i]) == 3);
        RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRelease(g_ahClasses[i]) == 2);
    }

    /* Check the sub-class API.*/
    RTTEST_CHECK(g_hTest, RTCritSectSetSubClass(&g_aCritSects[0], RTLOCKVAL_SUB_CLASS_ANY)  == RTLOCKVAL_SUB_CLASS_NONE);
    RTTEST_CHECK(g_hTest, RTCritSectSetSubClass(&g_aCritSects[0], RTLOCKVAL_SUB_CLASS_NONE) == RTLOCKVAL_SUB_CLASS_ANY);

    /* Enter the first 4 critsects in ascending order and thereby defining
       this as a valid lock order.  */
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);

    /* Now, leave and re-enter the critsects in a way that should break the
       order and check that we get the appropriate response. */
    int rc;
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[0]), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[0]), VINF_SUCCESS);

    /* Check that recursion isn't subject to order checks. */
    RTTEST_CHECK_RC(g_hTest, rc = RTCritSectEnter(&g_aCritSects[1]), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);

    /* Enable strict release order for class 2 and check that violations
       are caught - including recursion. */
    RTTEST_CHECK_RC(g_hTest, RTLockValidatorClassEnforceStrictReleaseOrder(g_ahClasses[2], true), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[2]), VINF_SUCCESS);                      /* start recursion */
    RTTEST_CHECK_RC(g_hTest, RTCritSectEnter(&g_aCritSects[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);                      /* end recursion */
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectLeave(&g_aCritSects[2]), VINF_SUCCESS);

    /* clean up */
    for (int i = 4 - 1; i >= 0; i--)
    {
        RTTEST_CHECK(g_hTest, RTLockValidatorClassRelease(g_ahClasses[i]) == 1);
        g_ahClasses[i] = NIL_RTLOCKVALCLASS;
        RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectDelete(&g_aCritSects[i]), VINF_SUCCESS);
    }
}


static void testLo3(void)
{
    RTTestSub(g_hTest, "locking order, read-write");

    /* Initialize the critsection with all different classes */
    for (unsigned i = 0; i < 6; i++)
    {
        RTTEST_CHECK_RC_RETV(g_hTest, RTLockValidatorClassCreate(&g_ahClasses[i], true /*fAutodidact*/, RT_SRC_POS, "testLo3-%u", i), VINF_SUCCESS);
        RTTEST_CHECK_RC_RETV(g_hTest, RTSemRWCreateEx(&g_ahSemRWs[i], 0, g_ahClasses[i], RTLOCKVAL_SUB_CLASS_NONE, "hSemRW-Lo3-%u", i), VINF_SUCCESS);
        RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRetain(g_ahClasses[i]) == 4);
        RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRelease(g_ahClasses[i]) == 3);
    }

    /* Check the sub-class API.*/
    RTTEST_CHECK(g_hTest, RTSemRWSetSubClass(g_ahSemRWs[0], RTLOCKVAL_SUB_CLASS_ANY)  == RTLOCKVAL_SUB_CLASS_NONE);
    RTTEST_CHECK(g_hTest, RTSemRWSetSubClass(g_ahSemRWs[0], RTLOCKVAL_SUB_CLASS_NONE) == RTLOCKVAL_SUB_CLASS_ANY);

    /* Enter the first 4 critsects in ascending order and thereby defining
       this as a valid lock order.  */
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestWrite(g_ahSemRWs[0], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestRead( g_ahSemRWs[1], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestRead( g_ahSemRWs[2], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestWrite(g_ahSemRWs[3], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestWrite(g_ahSemRWs[4], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestWrite(g_ahSemRWs[5], RT_INDEFINITE_WAIT), VINF_SUCCESS);

    /* Now, leave and re-enter the critsects in a way that should break the
       order and check that we get the appropriate response. */
    int rc;
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc = RTSemRWRequestWrite(g_ahSemRWs[0], RT_INDEFINITE_WAIT), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[0]), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead(g_ahSemRWs[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc = RTSemRWRequestRead(g_ahSemRWs[1], RT_INDEFINITE_WAIT), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead(g_ahSemRWs[1]), VINF_SUCCESS);

    /* Check that recursion isn't subject to order checks. */
    RTTEST_CHECK_RC(g_hTest, rc = RTSemRWRequestRead(g_ahSemRWs[2], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead(g_ahSemRWs[2]), VINF_SUCCESS);
    RTTEST_CHECK(g_hTest, RTSemRWGetReadCount(g_ahSemRWs[2]) == 1);

    RTTEST_CHECK_RC(g_hTest, rc = RTSemRWRequestWrite(g_ahSemRWs[3], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[3]), VINF_SUCCESS);
    RTTEST_CHECK(g_hTest, RTSemRWGetWriteRecursion(g_ahSemRWs[3]) == 1);

    /* Enable strict release order for class 2 and 3, then check that violations
       are caught - including recursion. */
    RTTEST_CHECK_RC(g_hTest, RTLockValidatorClassEnforceStrictReleaseOrder(g_ahClasses[2], true), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTLockValidatorClassEnforceStrictReleaseOrder(g_ahClasses[3], true), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestRead( g_ahSemRWs[2], RT_INDEFINITE_WAIT), VINF_SUCCESS);  /* start recursion */
    RTTEST_CHECK(   g_hTest, RTSemRWGetReadCount(g_ahSemRWs[2]) == 2);
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestWrite(g_ahSemRWs[3], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK(   g_hTest, RTSemRWGetWriteRecursion(g_ahSemRWs[3]) == 2);
    RTTEST_CHECK_RC(g_hTest, RTSemRWRequestRead( g_ahSemRWs[4], RT_INDEFINITE_WAIT), VINF_SUCCESS);  /* (mixed) */

    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead( g_ahSemRWs[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[3]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK(   g_hTest, RTSemRWGetWriteRecursion(g_ahSemRWs[3]) == 2);
    RTTEST_CHECK(   g_hTest, RTSemRWGetReadCount(g_ahSemRWs[2]) == 2);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead( g_ahSemRWs[4]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[3]), VINF_SUCCESS);
    RTTEST_CHECK(   g_hTest, RTSemRWGetWriteRecursion(g_ahSemRWs[3]) == 1);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead( g_ahSemRWs[2]), VINF_SUCCESS);                      /* end recursion */
    RTTEST_CHECK(   g_hTest, RTSemRWGetReadCount(g_ahSemRWs[2]) == 1);

    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead( g_ahSemRWs[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK(g_hTest, RTSemRWGetReadCount(g_ahSemRWs[2]) == 1);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[3]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK(g_hTest, RTSemRWGetWriteRecursion(g_ahSemRWs[3]) == 1);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[5]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[4]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseWrite(g_ahSemRWs[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemRWReleaseRead( g_ahSemRWs[2]), VINF_SUCCESS);

    /* clean up */
    for (int i = 6 - 1; i >= 0; i--)
    {
        uint32_t c;
        RTTEST_CHECK_MSG(g_hTest, (c = RTLockValidatorClassRelease(g_ahClasses[i])) == 2, (g_hTest, "c=%u i=%u\n", c, i));
        g_ahClasses[i] = NIL_RTLOCKVALCLASS;
        RTTEST_CHECK_RC_RETV(g_hTest, RTSemRWDestroy(g_ahSemRWs[i]), VINF_SUCCESS);
        g_ahSemRWs[i] = NIL_RTSEMRW;
    }
}


static void testLo4(void)
{
    RTTestSub(g_hTest, "locking order, mutex");

    /* Initialize the critsection with all different classes */
    for (unsigned i = 0; i < 4; i++)
    {
        RTTEST_CHECK_RC_RETV(g_hTest, RTLockValidatorClassCreate(&g_ahClasses[i], true /*fAutodidact*/, RT_SRC_POS, "testLo4-%u", i), VINF_SUCCESS);
        RTTEST_CHECK_RC_RETV(g_hTest, RTSemMutexCreateEx(&g_ahSemMtxes[i], 0, g_ahClasses[i], RTLOCKVAL_SUB_CLASS_NONE, "RTSemMutexLo4-%u", i), VINF_SUCCESS);
        RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRetain(g_ahClasses[i]) == 3);
        RTTEST_CHECK_RETV(g_hTest, RTLockValidatorClassRelease(g_ahClasses[i]) == 2);
    }

    /* Check the sub-class API.*/
    RTTEST_CHECK(g_hTest, RTSemMutexSetSubClass(g_ahSemMtxes[0], RTLOCKVAL_SUB_CLASS_ANY)  == RTLOCKVAL_SUB_CLASS_NONE);
    RTTEST_CHECK(g_hTest, RTSemMutexSetSubClass(g_ahSemMtxes[0], RTLOCKVAL_SUB_CLASS_NONE) == RTLOCKVAL_SUB_CLASS_ANY);

    /* Enter the first 4 critsects in ascending order and thereby defining
       this as a valid lock order.  */
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRequest(g_ahSemMtxes[0], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRequest(g_ahSemMtxes[1], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRequest(g_ahSemMtxes[2], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRequest(g_ahSemMtxes[3], RT_INDEFINITE_WAIT), VINF_SUCCESS);

    /* Now, leave and re-enter the critsects in a way that should break the
       order and check that we get the appropriate response. */
    int rc;
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[0]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, rc = RTSemMutexRequest(g_ahSemMtxes[0], RT_INDEFINITE_WAIT), VERR_SEM_LV_WRONG_ORDER);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[0]), VINF_SUCCESS);

    /* Check that recursion isn't subject to order checks. */
    RTTEST_CHECK_RC(g_hTest, rc = RTSemMutexRequest(g_ahSemMtxes[1], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[1]), VINF_SUCCESS);

    /* Enable strict release order for class 2 and check that violations
       are caught - including recursion. */
    RTTEST_CHECK_RC(g_hTest, RTLockValidatorClassEnforceStrictReleaseOrder(g_ahClasses[2], true), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTSemMutexRequest(g_ahSemMtxes[2], RT_INDEFINITE_WAIT), VINF_SUCCESS);  /* start recursion */
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRequest(g_ahSemMtxes[3], RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[2]), VINF_SUCCESS);                      /* end recursion */

    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[2]), VERR_SEM_LV_WRONG_RELEASE_ORDER);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[1]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[3]), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTSemMutexRelease(g_ahSemMtxes[2]), VINF_SUCCESS);

    /* clean up */
    for (int i = 4 - 1; i >= 0; i--)
    {
        RTTEST_CHECK(g_hTest, RTLockValidatorClassRelease(g_ahClasses[i]) == 1);
        g_ahClasses[i] = NIL_RTLOCKVALCLASS;
        RTTEST_CHECK_RC_RETV(g_hTest, RTSemMutexDestroy(g_ahSemMtxes[i]), VINF_SUCCESS);
    }
}




static const char *testCheckIfLockValidationIsCompiledIn(void)
{
    RTCRITSECT CritSect;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTCritSectInit(&CritSect), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTCritSectEnter(&CritSect), "");
    bool fRet = CritSect.pValidatorRec
             && CritSect.pValidatorRec->hThread == RTThreadSelf();
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTCritSectLeave(&CritSect), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTCritSectDelete(&CritSect), "");
    if (!fRet)
        return "Lock validation is not enabled for critical sections";

    /* deadlock detection for RTSemRW */
    RTSEMRW hSemRW;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWCreateEx(&hSemRW, 0 /*fFlags*/, NIL_RTLOCKVALCLASS,
                                                    RTLOCKVAL_SUB_CLASS_NONE, "RTSemRW-1"), NULL);
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWRequestRead(hSemRW, 50), "");
    int rc = RTSemRWRequestWrite(hSemRW, 1);
    RTTEST_CHECK_RET(g_hTest, RT_FAILURE_NP(rc), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWReleaseRead(hSemRW), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWDestroy(hSemRW), "");
    if (rc != VERR_SEM_LV_ILLEGAL_UPGRADE)
        return "Deadlock detection is not enabled for the read/write semaphores";

    /* lock order for RTSemRW */
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWCreateEx(&hSemRW, 0 /*fFlags*/,
                                                    RTLockValidatorClassCreateUnique(RT_SRC_POS, NULL),
                                                    RTLOCKVAL_SUB_CLASS_NONE, "RTSemRW-2"), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWRequestRead(hSemRW, 50), "");
    rc = RTSemRWRequestWrite(hSemRW, 1);
    RTTEST_CHECK_RET(g_hTest, RT_FAILURE_NP(rc), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWReleaseRead(hSemRW), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemRWDestroy(hSemRW), "");
    if (rc != VERR_SEM_LV_WRONG_ORDER)
    {
        RTTestPrintf(g_hTest,  RTTESTLVL_ALWAYS,  "%Rrc\n", rc);
        return "Lock order validation is not enabled for the read/write semaphores";
    }

    /* lock order for RTSemMutex */
    RTSEMMUTEX hSemMtx1;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexCreateEx(&hSemMtx1, 0 /*fFlags*/,
                                                       RTLockValidatorClassCreateUnique(RT_SRC_POS, NULL),
                                                       RTLOCKVAL_SUB_CLASS_NONE, "RTSemMtx-1"), "");
    RTSEMMUTEX hSemMtx2;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexCreateEx(&hSemMtx2, 0 /*fFlags*/,
                                                       RTLockValidatorClassCreateUnique(RT_SRC_POS, NULL),
                                                       RTLOCKVAL_SUB_CLASS_NONE, "RTSemMtx-2"), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexRequest(hSemMtx1, 50), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexRequest(hSemMtx2, 50), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexRelease(hSemMtx2), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexRelease(hSemMtx1), "");

    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexRequest(hSemMtx2, 50), "");
    rc = RTSemMutexRequest(hSemMtx1, 50);
    RTTEST_CHECK_RET(g_hTest, RT_FAILURE_NP(rc), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexRelease(hSemMtx2), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexDestroy(hSemMtx2), "");   hSemMtx2 = NIL_RTSEMMUTEX;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemMutexDestroy(hSemMtx1), "");   hSemMtx1 = NIL_RTSEMMUTEX;
    if (rc != VERR_SEM_LV_WRONG_ORDER)
        return "Lock order validation is not enabled for the mutex semaphores";

    /* signaller checks on event sems. */
    RTSEMEVENT hSemEvt;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemEventCreate(&hSemEvt), "");
    RTSemEventSetSignaller(hSemEvt, RTThreadSelf());
    RTSemEventSetSignaller(hSemEvt, NIL_RTTHREAD);
    rc = RTSemEventSignal(hSemEvt);
    RTTEST_CHECK_RET(g_hTest, RT_FAILURE_NP(rc), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemEventDestroy(hSemEvt), "");
    if (rc != VERR_SEM_LV_NOT_SIGNALLER)
        return "Signalling checks are not enabled for the event semaphores";

    /* signaller checks on multiple release event sems. */
    RTSEMEVENTMULTI hSemEvtMulti;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemEventMultiCreate(&hSemEvtMulti), "");
    RTSemEventMultiSetSignaller(hSemEvtMulti, RTThreadSelf());
    RTSemEventMultiSetSignaller(hSemEvtMulti, NIL_RTTHREAD);
    rc = RTSemEventMultiSignal(hSemEvtMulti);
    RTTEST_CHECK_RET(g_hTest, RT_FAILURE_NP(rc), "");
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTSemEventMultiDestroy(hSemEvtMulti), "");
    if (rc != VERR_SEM_LV_NOT_SIGNALLER)
        return "Signalling checks are not enabled for the multiple release event semaphores";

    /* we're good */
    return NULL;
}


int main()
{
    /*
     * Init.
     */
    int rc = RTTestInitAndCreate("tstRTLockValidator", &g_hTest);
    if (rc)
        return rc;
    RTTestBanner(g_hTest);

    RTLockValidatorSetEnabled(true);
    RTLockValidatorSetMayPanic(false);
    RTLockValidatorSetQuiet(true);
    const char *pszWhyDisabled = testCheckIfLockValidationIsCompiledIn();
    if (pszWhyDisabled)
        return RTTestErrorCount(g_hTest) > 0
            ? RTTestSummaryAndDestroy(g_hTest)
            : RTTestSkipAndDestroy(g_hTest, pszWhyDisabled);
    RTLockValidatorSetQuiet(false);

    bool fTestDd = true;
    bool fTestLo = true;

    /*
     * Some initial tests with verbose output (all single pass).
     */
    if (fTestDd)
    {
        testDd1(3, 0);
        testDd2(1, 0);
        testDd2(3, 0);
        testDd5(3, 0);
        testDd6(3, 0);
        testDd7(3, 0);
    }
    if (fTestLo)
    {
        testLo1();
        testLo2();
        testLo3();
        testLo4();
    }


    /*
     * If successful, perform more thorough testing without noisy output.
     */
    if (RTTestErrorCount(g_hTest) == 0)
    {
        RTLockValidatorSetQuiet(true);

        if (fTestDd)
        {
            testDd1( 2, SECS_SIMPLE_TEST);
            testDd1( 3, SECS_SIMPLE_TEST);
            testDd1( 7, SECS_SIMPLE_TEST);
            testDd1(10, SECS_SIMPLE_TEST);
            testDd1(15, SECS_SIMPLE_TEST);
            testDd1(30, SECS_SIMPLE_TEST);

            testDd2( 1, SECS_SIMPLE_TEST);
            testDd2( 2, SECS_SIMPLE_TEST);
            testDd2( 3, SECS_SIMPLE_TEST);
            testDd2( 7, SECS_SIMPLE_TEST);
            testDd2(10, SECS_SIMPLE_TEST);
            testDd2(15, SECS_SIMPLE_TEST);
            testDd2(30, SECS_SIMPLE_TEST);

            testDd3( 2, SECS_SIMPLE_TEST);
            testDd3(10, SECS_SIMPLE_TEST);

            testDd4( 2, SECS_RACE_TEST);
            testDd4( 6, SECS_RACE_TEST);
            testDd4(10, SECS_RACE_TEST);
            testDd4(30, SECS_RACE_TEST);

            testDd5( 2, SECS_RACE_TEST);
            testDd5( 3, SECS_RACE_TEST);
            testDd5( 7, SECS_RACE_TEST);
            testDd5(10, SECS_RACE_TEST);
            testDd5(15, SECS_RACE_TEST);
            testDd5(30, SECS_RACE_TEST);

            testDd6( 2, SECS_SIMPLE_TEST);
            testDd6( 3, SECS_SIMPLE_TEST);
            testDd6( 7, SECS_SIMPLE_TEST);
            testDd6(10, SECS_SIMPLE_TEST);
            testDd6(15, SECS_SIMPLE_TEST);
            testDd6(30, SECS_SIMPLE_TEST);

            testDd7( 2, SECS_SIMPLE_TEST);
            testDd7( 3, SECS_SIMPLE_TEST);
            testDd7( 7, SECS_SIMPLE_TEST);
            testDd7(10, SECS_SIMPLE_TEST);
            testDd7(15, SECS_SIMPLE_TEST);
            testDd7(30, SECS_SIMPLE_TEST);
        }
    }

    return RTTestSummaryAndDestroy(g_hTest);
}

