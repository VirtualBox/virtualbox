/* $Id: tstRTThreadExecutionTime.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTThreadGetExecution.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/thread.h>

#include <iprt/asm.h>
#include <iprt/test.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;
static volatile uint64_t g_cMsKernel;
static volatile uint64_t g_cMsUser;


static DECLCALLBACK(int) testThread(RTTHREAD hSelf, void *pvUser)
{
    RTMSINTERVAL const msWait = *(RTMSINTERVAL const *)pvUser;
    RT_NOREF_PV(hSelf);

    uint64_t const msNow = RTTimeMilliTS();
    uint64_t cMsKernelStart, cMsUserStart;
    RTTEST_CHECK_RC_RET(g_hTest, RTThreadGetExecutionTimeMilli(&cMsKernelStart, &cMsUserStart), VINF_SUCCESS, rcCheck);

    while (RTTimeMilliTS() < msNow + msWait)
        ASMNopPause();

    uint64_t cMsKernel, cMsUser;
    RTTEST_CHECK_RC_RET(g_hTest, RTThreadGetExecutionTimeMilli(&cMsKernel, &cMsUser), VINF_SUCCESS, rcCheck);

    cMsKernel -= cMsKernelStart;
    cMsUser   -= cMsUserStart;
    RTPrintf("kernel = %4lldms, user = %4lldms\n", cMsKernel, cMsUser);
    ASMAtomicAddU64(&g_cMsKernel, cMsKernel);
    ASMAtomicAddU64(&g_cMsUser,   cMsUser);

    return VINF_SUCCESS;
}


static void test1(RTMSINTERVAL msWait)
{
    RTTHREAD ahThreads[16];
    RTTestSubF(g_hTest, "RTThreadGetExecutionTimeMilli - %zu thread for %u ms", RT_ELEMENTS(ahThreads), (unsigned)msWait);
    for (unsigned i = 0; i < RT_ELEMENTS(ahThreads); i++)
        RTTESTI_CHECK_RC_RETV(RTThreadCreate(&ahThreads[i], testThread, &msWait, 0, RTTHREADTYPE_DEFAULT,
                              RTTHREADFLAGS_WAITABLE, "test"), VINF_SUCCESS);

    RTPrintf("Waiting for the threads to complete...\n");
    for (unsigned i = 0; i < RT_ELEMENTS(ahThreads); i++)
        RTTESTI_CHECK_RC(RTThreadWait(ahThreads[i], msWait * 5, NULL), VINF_SUCCESS);

    RTPrintf("sum kernel = %lldms, sum user = %lldms\n", g_cMsKernel, g_cMsUser);
}


static void test2(void)
{
    RTTestSub(g_hTest, "RTThreadGetExecutionTimeMilli perf");

    /* Run it for ~3 seconds. */
    RTThreadYield();
    uint64_t       cCalls  = 0;
    uint64_t const nsStart = RTTimeNanoTS();
    for (;;)
    {
        uint32_t cLeftBeforeCheck = 16384;
        while (cLeftBeforeCheck-- > 0)
        {
            uint64_t uIgn;
            RTThreadGetExecutionTimeMilli(&uIgn, &uIgn);
            cCalls++;
        }
        uint64_t const cNsElapsed = RTTimeNanoTS() - nsStart;
        if (cNsElapsed >= RT_NS_1SEC_64 * 3)
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "%'RU64 calls in %'RU64 ns\n", cCalls, cNsElapsed);
            RTTestValue(g_hTest, "RTThreadGetExecutionTimeMilli avg.", cNsElapsed * 1000 / cCalls, RTTESTUNIT_PS_PER_CALL);
            return;
        }
    }
}


int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTThreadExecutionTime", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    uint64_t uIgn;
    int rc = RTThreadGetExecutionTimeMilli(&uIgn, &uIgn);
    if (rc == VERR_NOT_IMPLEMENTED)
        return RTTestSkipAndDestroy(g_hTest, "VERR_NOT_IMPLEMENTED");

    test1(RT_MS_1SEC);
    test2();

    return RTTestSummaryAndDestroy(g_hTest);
}
