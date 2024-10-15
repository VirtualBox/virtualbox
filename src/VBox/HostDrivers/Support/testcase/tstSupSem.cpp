/* $Id: tstSupSem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Support Library Testcase - Ring-3 Semaphore interface.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <VBox/sup.h>

#include <VBox/param.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/process.h>
#include <iprt/env.h>
#include <iprt/string.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
static PSUPDRVSESSION   g_pSession;
static RTTEST           g_hTest;
static uint32_t         g_cMillies; /* Used by the interruptible tests. */



static DECLCALLBACK(int) tstSupSemInterruptibleSRE(RTTHREAD hSelf, void *pvUser)
{
    SUPSEMEVENT hEvent = (SUPSEMEVENT)pvUser;
    RTThreadUserSignal(hSelf);
    return SUPSemEventWaitNoResume(g_pSession, hEvent, g_cMillies);
}


static DECLCALLBACK(int) tstSupSemInterruptibleMRE(RTTHREAD hSelf, void *pvUser)
{
    SUPSEMEVENTMULTI hEventMulti = (SUPSEMEVENTMULTI)pvUser;
    RTThreadUserSignal(hSelf);
    return SUPSemEventMultiWaitNoResume(g_pSession, hEventMulti, g_cMillies);
}


int main(int argc, char **argv)
{
    bool fSys = true;
    bool fGip = false;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fGip = true;
#endif

    /*
     * Init.
     */
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    if (argc == 2 && !strcmp(argv[1], "child"))
    {
        RTThreadSleep(300);
        return 0;
    }

    RTTEST hTest;
    rc = RTTestCreate("tstSupSem", &hTest);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstSupSem: fatal error: RTTestCreate failed with rc=%Rrc\n", rc);
        return 1;
    }
    g_hTest = hTest;

    PSUPDRVSESSION pSession;
    rc = SUPR3Init(&pSession);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(hTest, "SUPR3Init failed with rc=%Rrc\n", rc);
        return RTTestSummaryAndDestroy(hTest);
    }
    g_pSession = pSession;
    RTTestBanner(hTest);

    /*
     * Basic API checks.
     */
    RTTestSub(hTest, "Single Release Event (SRE) API");
    SUPSEMEVENT hEvent = NIL_SUPSEMEVENT;
    RTTESTI_CHECK_RC(SUPSemEventCreate(pSession, &hEvent),          VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 0),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 1),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 2),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 8),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent,20),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 0),  VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 1),  VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 2),  VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 8),  VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 20), VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent,1000),VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent),           VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 0),  VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 0),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 1),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 2),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent, 8),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventWaitNoResume(pSession, hEvent,20),  VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent),            VINF_OBJECT_DESTROYED);
    RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent),            VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(SUPSemEventClose(pSession, NIL_SUPSEMEVENT),   VINF_SUCCESS);

    RTTestSub(hTest, "Multiple Release Event (MRE) API");
    SUPSEMEVENTMULTI hEventMulti = NIL_SUPSEMEVENT;
    RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEventMulti),            VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 0),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 1),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 2),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 8),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti,20),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiSignal(pSession, hEventMulti),             VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 0),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 0),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 0),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 1),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 2),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 8),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti,20),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti,1000),  VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiSignal(pSession, hEventMulti),             VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiSignal(pSession, hEventMulti),             VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 0),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiReset(pSession, hEventMulti),              VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 0),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 1),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 2),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 8),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti,20),    VERR_TIMEOUT);
    RTTESTI_CHECK_RC(SUPSemEventMultiSignal(pSession, hEventMulti),             VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 0),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 1),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 2),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 8),    VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti, 20),   VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiWaitNoResume(pSession, hEventMulti,1000),  VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEventMulti),              VINF_OBJECT_DESTROYED);
    RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEventMulti),              VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, NIL_SUPSEMEVENTMULTI),     VINF_SUCCESS);

#if !defined(RT_OS_OS2) && !defined(RT_OS_WINDOWS)
    RTTestSub(hTest, "SRE Interruptibility");
    RTTESTI_CHECK_RC(SUPSemEventCreate(pSession, &hEvent), VINF_SUCCESS);
    g_cMillies = RT_INDEFINITE_WAIT;
    RTTHREAD hThread = NIL_RTTHREAD;
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemInterruptibleSRE, (void *)hEvent, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "IntSRE"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadUserWait(hThread, 60*1000), VINF_SUCCESS);
    RTThreadSleep(120);
    RTThreadPoke(hThread);
    int rcThread = VINF_SUCCESS;
    RTTESTI_CHECK_RC(RTThreadWait(hThread, 60*1000, &rcThread), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcThread, VERR_INTERRUPTED);
    RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent), VINF_OBJECT_DESTROYED);

    RTTESTI_CHECK_RC(SUPSemEventCreate(pSession, &hEvent), VINF_SUCCESS);
    g_cMillies = 120*1000;
    hThread = NIL_RTTHREAD;
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemInterruptibleSRE, (void *)hEvent, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "IntSRE"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadUserWait(hThread, 60*1000), VINF_SUCCESS);
    RTThreadSleep(120);
    RTThreadPoke(hThread);
    rcThread = VINF_SUCCESS;
    RTTESTI_CHECK_RC(RTThreadWait(hThread, 60*1000, &rcThread), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcThread, VERR_INTERRUPTED);
    RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent), VINF_OBJECT_DESTROYED);


    RTTestSub(hTest, "MRE Interruptibility");
    RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEventMulti), VINF_SUCCESS);
    g_cMillies = RT_INDEFINITE_WAIT;
    hThread = NIL_RTTHREAD;
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemInterruptibleMRE, (void *)hEventMulti, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "IntMRE"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadUserWait(hThread, 60*1000), VINF_SUCCESS);
    RTThreadSleep(120);
    RTThreadPoke(hThread);
    rcThread = VINF_SUCCESS;
    RTTESTI_CHECK_RC(RTThreadWait(hThread, 60*1000, &rcThread), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcThread, VERR_INTERRUPTED);
    RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEventMulti), VINF_OBJECT_DESTROYED);

    RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEventMulti), VINF_SUCCESS);
    g_cMillies = 120*1000;
    hThread = NIL_RTTHREAD;
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemInterruptibleMRE, (void *)hEventMulti, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "IntMRE"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadUserWait(hThread, 60*1000), VINF_SUCCESS);
    RTThreadSleep(120);
    RTThreadPoke(hThread);
    rcThread = VINF_SUCCESS;
    RTTESTI_CHECK_RC(RTThreadWait(hThread, 60*1000, &rcThread), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcThread, VERR_INTERRUPTED);
    RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEventMulti), VINF_OBJECT_DESTROYED);

    /*
     * Fork test.
     * Spawn a thread waiting for an event, then spawn a new child process (of
     * ourselves) and make sure that this does not alter the intended behaviour
     * of our event semaphore implementation (see @bugref{5090}).
     */
    RTTestSub(hTest, "SRE Process Spawn");
    hThread = NIL_RTTHREAD;
    g_cMillies = 120*1000;
    RTTESTI_CHECK_RC(SUPSemEventCreate(pSession, &hEvent), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemInterruptibleSRE, (void *)hEvent, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "IntSRE"), VINF_SUCCESS);

    const char *apszArgs[3] = { argv[0], "child", NULL };
    RTPROCESS Process = NIL_RTPROCESS;
    RTThreadSleep(250);
    RTTESTI_CHECK_RC(RTProcCreate(apszArgs[0], apszArgs, RTENV_DEFAULT, 0, &Process), VINF_SUCCESS);

    RTThreadSleep(250);
    RTTESTI_CHECK_RC(SUPSemEventSignal(pSession, hEvent), VINF_SUCCESS);

    rcThread = VERR_GENERAL_FAILURE;
    RTTESTI_CHECK_RC(RTThreadWait(hThread, 120*1000, &rcThread), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcThread, VINF_SUCCESS);
    RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent), VINF_OBJECT_DESTROYED);


    RTTestSub(hTest, "MRE Process Spawn");
    hThread = NIL_RTTHREAD;
    g_cMillies = 120*1000;
    RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEvent), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemInterruptibleMRE, (void *)hEvent, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "IntSRE"), VINF_SUCCESS);

    RTTHREAD hThread2 = NIL_RTTHREAD;
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread2, tstSupSemInterruptibleMRE, (void *)hEvent, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "IntSRE"), VINF_SUCCESS);

    Process = NIL_RTPROCESS;
    RTThreadSleep(250);
    RTTESTI_CHECK_RC(RTProcCreate(apszArgs[0], apszArgs, RTENV_DEFAULT, 0, &Process), VINF_SUCCESS);

    RTThreadSleep(250);
    RTTESTI_CHECK_RC(SUPSemEventMultiSignal(pSession, hEvent), VINF_SUCCESS);

    rcThread = VERR_GENERAL_FAILURE;
    RTTESTI_CHECK_RC(RTThreadWait(hThread, 120*1000, &rcThread), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcThread, VINF_SUCCESS);

    int rcThread2 = VERR_GENERAL_FAILURE;
    RTTESTI_CHECK_RC(RTThreadWait(hThread2, 120*1000, &rcThread2), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcThread2, VINF_SUCCESS);

    RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEvent), VINF_OBJECT_DESTROYED);

#endif /* !OS2 && !WINDOWS */

    {

#define LOOP_COUNT 20
        static unsigned const s_acMsIntervals[] = { 0, 1, 2, 3, 4, 8, 10, 16, 32 };
        if (RTTestErrorCount(hTest) == 0)
        {
            RTTestSub(hTest, "SRE Timeout Accuracy (ms)");
            RTTESTI_CHECK_RC(SUPSemEventCreate(pSession, &hEvent), VINF_SUCCESS);

            uint32_t cInterrupted = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(s_acMsIntervals); i++)
            {
                uint64_t cMs        = s_acMsIntervals[i];
                uint64_t cNsMinSys  = UINT64_MAX;
                uint64_t cNsMin     = UINT64_MAX;
                uint64_t cNsTotalSys= 0;
                uint64_t cNsTotal   = 0;
                unsigned cLoops     = 0;
                while (cLoops < LOOP_COUNT)
                {
                    uint64_t u64StartSys = RTTimeSystemNanoTS();
                    uint64_t u64Start    = RTTimeNanoTS();
                    int rcX = SUPSemEventWaitNoResume(pSession, hEvent, cMs);
                    uint64_t cNsElapsedSys = RTTimeSystemNanoTS() - u64StartSys;
                    uint64_t cNsElapsed    = RTTimeNanoTS()       - u64Start;

                    if (rcX == VERR_INTERRUPTED)
                    {
                        cInterrupted++;
                        continue; /* retry */
                    }
                    if (rcX != VERR_TIMEOUT)
                        RTTestFailed(hTest, "%Rrc cLoops=%u cMs=%u", rcX, cLoops, cMs);

                    if (cNsElapsedSys < cNsMinSys)
                        cNsMinSys = cNsElapsedSys;
                    if (cNsElapsed < cNsMin)
                        cNsMin = cNsElapsed;
                    cNsTotalSys += cNsElapsedSys;
                    cNsTotal    += cNsElapsed;
                    cLoops++;
                }
                if (fSys)
                {
                    RTTestValueF(hTest, cNsMinSys, RTTESTUNIT_NS,            "%u ms min (clock=sys)", cMs);
                    RTTestValueF(hTest, cNsTotalSys / cLoops, RTTESTUNIT_NS, "%u ms avg (clock=sys)", cMs);
                }
                if (fGip)
                {
                    RTTestValueF(hTest, cNsMin, RTTESTUNIT_NS,               "%u ms min (clock=gip)", cMs);
                    RTTestValueF(hTest, cNsTotal / cLoops, RTTESTUNIT_NS,    "%u ms avg (clock=gip)", cMs);
                }
            }

            RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent), VINF_OBJECT_DESTROYED);
            RTTestValueF(hTest, cInterrupted, RTTESTUNIT_OCCURRENCES, "VERR_INTERRUPTED returned");
        }

        if (RTTestErrorCount(hTest) == 0)
        {
            RTTestSub(hTest, "MRE Timeout Accuracy (ms)");
            RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEvent), VINF_SUCCESS);

            uint32_t cInterrupted = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(s_acMsIntervals); i++)
            {
                uint64_t cMs        = s_acMsIntervals[i];
                uint64_t cNsMinSys  = UINT64_MAX;
                uint64_t cNsMin     = UINT64_MAX;
                uint64_t cNsTotalSys= 0;
                uint64_t cNsTotal   = 0;
                unsigned cLoops     = 0;
                while (cLoops < LOOP_COUNT)
                {
                    uint64_t u64StartSys = RTTimeSystemNanoTS();
                    uint64_t u64Start    = RTTimeNanoTS();
                    int rcX = SUPSemEventMultiWaitNoResume(pSession, hEvent, cMs);
                    uint64_t cNsElapsedSys = RTTimeSystemNanoTS() - u64StartSys;
                    uint64_t cNsElapsed    = RTTimeNanoTS()       - u64Start;

                    if (rcX == VERR_INTERRUPTED)
                    {
                        cInterrupted++;
                        continue; /* retry */
                    }
                    if (rcX != VERR_TIMEOUT)
                        RTTestFailed(hTest, "%Rrc cLoops=%u cMs=%u", rcX, cLoops, cMs);

                    if (cNsElapsedSys < cNsMinSys)
                        cNsMinSys = cNsElapsedSys;
                    if (cNsElapsed < cNsMin)
                        cNsMin = cNsElapsed;
                    cNsTotalSys += cNsElapsedSys;
                    cNsTotal    += cNsElapsed;
                    cLoops++;
                }
                if (fSys)
                {
                    RTTestValueF(hTest, cNsMinSys, RTTESTUNIT_NS,            "%u ms min (clock=sys)", cMs);
                    RTTestValueF(hTest, cNsTotalSys / cLoops, RTTESTUNIT_NS, "%u ms avg (clock=sys)", cMs);
                }
                if (fGip)
                {
                    RTTestValueF(hTest, cNsMin, RTTESTUNIT_NS,               "%u ms min (clock=gip)", cMs);
                    RTTestValueF(hTest, cNsTotal / cLoops, RTTESTUNIT_NS,    "%u ms avg (clock=gip)", cMs);
                }
            }

            RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEvent), VINF_OBJECT_DESTROYED);
            RTTestValueF(hTest, cInterrupted, RTTESTUNIT_OCCURRENCES, "VERR_INTERRUPTED returned");
        }
    }

    {
        static uint32_t const s_acNsIntervals[] =
        {
            0, 1000, 5000, 15000, 30000, 50000, 100000, 250000, 500000, 750000, 900000, 1500000, 2200000
        };

        if (RTTestErrorCount(hTest) == 0)
        {
            RTTestSub(hTest, "SUPSemEventWaitNsRelIntr Accuracy");
            RTTestValueF(hTest, SUPSemEventGetResolution(pSession), RTTESTUNIT_NS, "SRE resolution");
            RTTESTI_CHECK_RC(SUPSemEventCreate(pSession, &hEvent), VINF_SUCCESS);

            uint32_t cInterrupted = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(s_acNsIntervals); i++)
            {
                uint64_t cNs        = s_acNsIntervals[i];
                uint64_t cNsMinSys  = UINT64_MAX;
                uint64_t cNsMin     = UINT64_MAX;
                uint64_t cNsTotalSys= 0;
                uint64_t cNsTotal   = 0;
                unsigned cLoops     = 0;
                while (cLoops < LOOP_COUNT)
                {
                    uint64_t u64StartSys = RTTimeSystemNanoTS();
                    uint64_t u64Start    = RTTimeNanoTS();
                    int rcX = SUPSemEventWaitNsRelIntr(pSession, hEvent, cNs);
                    uint64_t cNsElapsedSys = RTTimeSystemNanoTS() - u64StartSys;
                    uint64_t cNsElapsed    = RTTimeNanoTS()       - u64Start;

                    if (rcX == VERR_INTERRUPTED)
                    {
                        cInterrupted++;
                        continue; /* retry */
                    }
                    if (rcX != VERR_TIMEOUT)
                        RTTestFailed(hTest, "%Rrc cLoops=%u cNs=%u", rcX, cLoops, cNs);

                    if (cNsElapsedSys < cNsMinSys)
                        cNsMinSys = cNsElapsedSys;
                    if (cNsElapsed < cNsMin)
                        cNsMin = cNsElapsed;
                    cNsTotalSys += cNsElapsedSys;
                    cNsTotal    += cNsElapsed;
                    cLoops++;
                }
                if (fSys)
                {
                    RTTestValueF(hTest, cNsMinSys, RTTESTUNIT_NS,            "%'u ns min (clock=sys)", cNs);
                    RTTestValueF(hTest, cNsTotalSys / cLoops, RTTESTUNIT_NS, "%'u ns avg (clock=sys)", cNs);
                }
                if (fGip)
                {
                    RTTestValueF(hTest, cNsMin, RTTESTUNIT_NS,               "%'u ns min (clock=gip)", cNs);
                    RTTestValueF(hTest, cNsTotal / cLoops, RTTESTUNIT_NS,    "%'u ns avg (clock=gip)", cNs);
                }
            }

            RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent), VINF_OBJECT_DESTROYED);
            RTTestValueF(hTest, cInterrupted, RTTESTUNIT_OCCURRENCES, "VERR_INTERRUPTED returned");
        }

        if (RTTestErrorCount(hTest) == 0)
        {
            RTTestSub(hTest, "SUPSemEventMultiWaitNsRelIntr Accuracy");
            RTTestValueF(hTest, SUPSemEventMultiGetResolution(pSession), RTTESTUNIT_NS, "MRE resolution");
            RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEvent), VINF_SUCCESS);

            uint32_t cInterrupted = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(s_acNsIntervals); i++)
            {
                uint64_t cNs        = s_acNsIntervals[i];
                uint64_t cNsMinSys  = UINT64_MAX;
                uint64_t cNsMin     = UINT64_MAX;
                uint64_t cNsTotalSys= 0;
                uint64_t cNsTotal   = 0;
                unsigned cLoops     = 0;
                while (cLoops < LOOP_COUNT)
                {
                    uint64_t u64StartSys = RTTimeSystemNanoTS();
                    uint64_t u64Start    = RTTimeNanoTS();
                    int rcX = SUPSemEventMultiWaitNsRelIntr(pSession, hEvent, cNs);
                    uint64_t cNsElapsedSys = RTTimeSystemNanoTS() - u64StartSys;
                    uint64_t cNsElapsed    = RTTimeNanoTS()       - u64Start;

                    if (rcX == VERR_INTERRUPTED)
                    {
                        cInterrupted++;
                        continue; /* retry */
                    }
                    if (rcX != VERR_TIMEOUT)
                        RTTestFailed(hTest, "%Rrc cLoops=%u cNs=%u", rcX, cLoops, cNs);

                    if (cNsElapsedSys < cNsMinSys)
                        cNsMinSys = cNsElapsedSys;
                    if (cNsElapsed < cNsMin)
                        cNsMin = cNsElapsed;
                    cNsTotalSys += cNsElapsedSys;
                    cNsTotal    += cNsElapsed;
                    cLoops++;
                }
                if (fSys)
                {
                    RTTestValueF(hTest, cNsMinSys, RTTESTUNIT_NS,            "%'u ns min (clock=sys)", cNs);
                    RTTestValueF(hTest, cNsTotalSys / cLoops, RTTESTUNIT_NS, "%'u ns avg (clock=sys)", cNs);
                }
                if (fGip)
                {
                    RTTestValueF(hTest, cNsMin, RTTESTUNIT_NS,               "%'u ns min (clock=gip)", cNs);
                    RTTestValueF(hTest, cNsTotal / cLoops, RTTESTUNIT_NS,    "%'u ns avg (clock=gip)", cNs);
                }
            }

            RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEvent), VINF_OBJECT_DESTROYED);
            RTTestValueF(hTest, cInterrupted, RTTESTUNIT_OCCURRENCES, "VERR_INTERRUPTED returned");
        }

        if (RTTestErrorCount(hTest) == 0)
        {
            RTTestSub(hTest, "SUPSemEventWaitNsAbsIntr Accuracy");
            RTTestValueF(hTest, SUPSemEventGetResolution(pSession), RTTESTUNIT_NS, "MRE resolution");
            RTTESTI_CHECK_RC(SUPSemEventCreate(pSession, &hEvent), VINF_SUCCESS);

            uint32_t cInterrupted = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(s_acNsIntervals); i++)
            {
                uint64_t cNs        = s_acNsIntervals[i];
                uint64_t cNsMinSys  = UINT64_MAX;
                uint64_t cNsMin     = UINT64_MAX;
                uint64_t cNsTotalSys= 0;
                uint64_t cNsTotal   = 0;
                unsigned cLoops     = 0;
                while (cLoops < LOOP_COUNT)
                {
                    uint64_t u64StartSys   = RTTimeSystemNanoTS();
                    uint64_t u64Start      = RTTimeNanoTS();
                    uint64_t uAbsDeadline  = (fGip ? u64Start : u64StartSys) + cNs;
                    int rcX = SUPSemEventWaitNsAbsIntr(pSession, hEvent, uAbsDeadline);
                    uint64_t cNsElapsedSys = RTTimeSystemNanoTS() - u64StartSys;
                    uint64_t cNsElapsed    = RTTimeNanoTS()       - u64Start;

                    if (rcX == VERR_INTERRUPTED)
                    {
                        cInterrupted++;
                        continue; /* retry */
                    }
                    if (rcX != VERR_TIMEOUT)
                        RTTestFailed(hTest, "%Rrc cLoops=%u cNs=%u", rcX, cLoops, cNs);

                    if (cNsElapsedSys < cNsMinSys)
                        cNsMinSys = cNsElapsedSys;
                    if (cNsElapsed < cNsMin)
                        cNsMin = cNsElapsed;
                    cNsTotalSys += cNsElapsedSys;
                    cNsTotal    += cNsElapsed;
                    cLoops++;
                }
                if (fSys)
                {
                    RTTestValueF(hTest, cNsMinSys, RTTESTUNIT_NS,            "%'u ns min (clock=sys)", cNs);
                    RTTestValueF(hTest, cNsTotalSys / cLoops, RTTESTUNIT_NS, "%'u ns avg (clock=sys)", cNs);
                }
                if (fGip)
                {
                    RTTestValueF(hTest, cNsMin, RTTESTUNIT_NS,               "%'u ns min (clock=gip)", cNs);
                    RTTestValueF(hTest, cNsTotal / cLoops, RTTESTUNIT_NS,    "%'u ns avg (clock=gip)", cNs);
                }
            }

            RTTESTI_CHECK_RC(SUPSemEventClose(pSession, hEvent), VINF_OBJECT_DESTROYED);
            RTTestValueF(hTest, cInterrupted, RTTESTUNIT_OCCURRENCES, "VERR_INTERRUPTED returned");
        }


        if (RTTestErrorCount(hTest) == 0)
        {
            RTTestSub(hTest, "SUPSemEventMultiWaitNsAbsIntr Accuracy");
            RTTestValueF(hTest, SUPSemEventMultiGetResolution(pSession), RTTESTUNIT_NS, "MRE resolution");
            RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEvent), VINF_SUCCESS);

            uint32_t cInterrupted = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(s_acNsIntervals); i++)
            {
                uint64_t cNs        = s_acNsIntervals[i];
                uint64_t cNsMinSys  = UINT64_MAX;
                uint64_t cNsMin     = UINT64_MAX;
                uint64_t cNsTotalSys= 0;
                uint64_t cNsTotal   = 0;
                unsigned cLoops     = 0;
                while (cLoops < LOOP_COUNT)
                {
                    uint64_t u64StartSys   = RTTimeSystemNanoTS();
                    uint64_t u64Start      = RTTimeNanoTS();
                    uint64_t uAbsDeadline  = (fGip ? u64Start : u64StartSys) + cNs;
                    int rcX = SUPSemEventMultiWaitNsAbsIntr(pSession, hEvent, uAbsDeadline);
                    uint64_t cNsElapsedSys = RTTimeSystemNanoTS() - u64StartSys;
                    uint64_t cNsElapsed    = RTTimeNanoTS()       - u64Start;

                    if (rcX == VERR_INTERRUPTED)
                    {
                        cInterrupted++;
                        continue; /* retry */
                    }
                    if (rcX != VERR_TIMEOUT)
                        RTTestFailed(hTest, "%Rrc cLoops=%u cNs=%u", rcX, cLoops, cNs);

                    if (cNsElapsedSys < cNsMinSys)
                        cNsMinSys = cNsElapsedSys;
                    if (cNsElapsed < cNsMin)
                        cNsMin = cNsElapsed;
                    cNsTotalSys += cNsElapsedSys;
                    cNsTotal    += cNsElapsed;
                    cLoops++;
                }
                if (fSys)
                {
                    RTTestValueF(hTest, cNsMinSys, RTTESTUNIT_NS,            "%'u ns min (clock=sys)", cNs);
                    RTTestValueF(hTest, cNsTotalSys / cLoops, RTTESTUNIT_NS, "%'u ns avg (clock=sys)", cNs);
                }
                if (fGip)
                {
                    RTTestValueF(hTest, cNsMin, RTTESTUNIT_NS,               "%'u ns min (clock=gip)", cNs);
                    RTTestValueF(hTest, cNsTotal / cLoops, RTTESTUNIT_NS,    "%'u ns avg (clock=gip)", cNs);
                }
            }

            RTTESTI_CHECK_RC(SUPSemEventMultiClose(pSession, hEvent), VINF_OBJECT_DESTROYED);
            RTTestValueF(hTest, cInterrupted, RTTESTUNIT_OCCURRENCES, "VERR_INTERRUPTED returned");
        }

    }


    /*
     * Done.
     */
    return RTTestSummaryAndDestroy(hTest);
}

