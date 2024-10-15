/* $Id: tstRTSemEventMulti.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Multiple Release Event Semaphores.
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
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test handle. */
static RTTEST  g_hTest;


static DECLCALLBACK(int) test1Thread1(RTTHREAD ThreadSelf, void *pvUser)
{
    RTSEMEVENTMULTI hSem = *(PRTSEMEVENTMULTI)pvUser;
    RT_NOREF_PV(ThreadSelf);

    uint64_t u64 = RTTimeSystemMilliTS();
    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiWait(hSem, 1000), VERR_TIMEOUT);
    u64 = RTTimeSystemMilliTS() - u64;
    RTTEST_CHECK_MSG(g_hTest, u64 < 1500 && u64 > 950, (g_hTest, "u64=%llu\n", u64));

    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiWait(hSem, 2000), VINF_SUCCESS);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) test1Thread2(RTTHREAD ThreadSelf, void *pvUser)
{
    RTSEMEVENTMULTI hSem = *(PRTSEMEVENTMULTI)pvUser;
    RT_NOREF_PV(ThreadSelf);

    RTTEST_CHECK_RC(g_hTest, RTSemEventMultiWait(hSem, RT_INDEFINITE_WAIT), VINF_SUCCESS);
    return VINF_SUCCESS;
}


static void test1(void)
{
    RTTestISub("Three threads");

    /*
     * Create the threads and let them block on the event multi semaphore.
     */
    RTSEMEVENTMULTI hSem;
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiCreate(&hSem), VINF_SUCCESS);

    RTTHREAD hThread2;
    RTTESTI_CHECK_RC_RETV(RTThreadCreate(&hThread2, test1Thread2, &hSem, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "test2"), VINF_SUCCESS);
    RTThreadSleep(100);

    RTTHREAD hThread1;
    RTTESTI_CHECK_RC_RETV(RTThreadCreate(&hThread1, test1Thread1, &hSem, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "test1"), VINF_SUCCESS);

    /* Force first thread (which has a timeout of 1 second) to timeout in the
     * first wait, and the second wait will succeed. */
    RTTESTI_CHECK_RC(RTThreadSleep(1500), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadWait(hThread1, 5000, NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTThreadWait(hThread2, 5000, NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTSemEventMultiDestroy(hSem), VINF_SUCCESS);
}


static void testBasicsWaitTimeout(RTSEMEVENTMULTI hSem, unsigned i)
{
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWait(hSem, 0), VERR_TIMEOUT);
#if 0
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitNoResume(hSem, 0), VERR_TIMEOUT);
#else
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_RELATIVE,
                                                0),
                          VERR_TIMEOUT);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                RTTimeSystemNanoTS() + 1000*i),
                          VERR_TIMEOUT);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                RTTimeNanoTS() + 1000*i),
                          VERR_TIMEOUT);

    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_RELATIVE,
                                                0),
                          VERR_TIMEOUT);
#endif
}


static void testBasicsWaitSuccess(RTSEMEVENTMULTI hSem, unsigned i)
{
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWait(hSem, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWait(hSem, RT_INDEFINITE_WAIT), VINF_SUCCESS);
#if 0
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitNoResume(hSem, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitNoResume(hSem, RT_INDEFINITE_WAIT), VINF_SUCCESS);
#else
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_RELATIVE,
                                                0),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem, RTSEMWAIT_FLAGS_RESUME   | RTSEMWAIT_FLAGS_INDEFINITE, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                RTTimeSystemNanoTS() + 1000*i),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                RTTimeNanoTS() + 1000*i),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                0),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                _1G),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                UINT64_MAX),
                          VINF_SUCCESS);


    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                RTTimeSystemMilliTS() + 1000*i),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                RTTimeMilliTS() + 1000*i),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                0),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                _1M),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiWaitEx(hSem,
                                                RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_ABSOLUTE,
                                                UINT64_MAX),
                          VINF_SUCCESS);
#endif
}


static void testBasics(void)
{
    RTTestISub("Basics");

    RTSEMEVENTMULTI hSem;
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiCreate(&hSem), VINF_SUCCESS);

    /* The semaphore is created in a reset state, calling reset explicitly
       shouldn't make any difference. */
    testBasicsWaitTimeout(hSem, 0);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiReset(hSem), VINF_SUCCESS);
    testBasicsWaitTimeout(hSem, 1);
    if (RTTestIErrorCount())
        return;

    /* When signalling the semaphore all successive wait calls shall
       succeed, signalling it again should make no difference. */
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    testBasicsWaitSuccess(hSem, 2);
    if (RTTestIErrorCount())
        return;

    /* After resetting it we should time out again. */
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiReset(hSem), VINF_SUCCESS);
    testBasicsWaitTimeout(hSem, 3);
    if (RTTestIErrorCount())
        return;

    /* The number of resets or signal calls shouldn't matter. */
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiReset(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiReset(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiReset(hSem), VINF_SUCCESS);
    testBasicsWaitTimeout(hSem, 4);

    RTTESTI_CHECK_RC_RETV(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    testBasicsWaitSuccess(hSem, 5);

    RTTESTI_CHECK_RC_RETV(RTSemEventMultiReset(hSem), VINF_SUCCESS);
    testBasicsWaitTimeout(hSem, 6);

    /* Destroy it. */
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiDestroy(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiDestroy(NIL_RTSEMEVENTMULTI), VINF_SUCCESS);

    /* Whether it is reset (above), signalled or not used shouldn't matter.  */
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiCreate(&hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiSignal(hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiDestroy(hSem), VINF_SUCCESS);

    RTTESTI_CHECK_RC_RETV(RTSemEventMultiCreate(&hSem), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTSemEventMultiDestroy(hSem), VINF_SUCCESS);

    RTTestISubDone();
}


int main(int argc, char **argv)
{
    RT_NOREF_PV(argc); RT_NOREF_PV(argv);

    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTSemEventMulti", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    testBasics();
    if (!RTTestErrorCount(g_hTest))
    {
        test1();
    }

    return RTTestSummaryAndDestroy(g_hTest);
}

