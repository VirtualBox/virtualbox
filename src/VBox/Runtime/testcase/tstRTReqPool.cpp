/* $Id: tstRTReqPool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Request Thread Pool.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/req.h>

#include <iprt/err.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest = NIL_RTTEST;


static DECLCALLBACK(int) NopCallback(void)
{
    return VINF_SUCCESS;
}

static void test1(void)
{
    RTTestISub("Basics");
    RTREQPOOL hPool;
    uint32_t cMaxThreads = 10;
    RTTESTI_CHECK_RC_RETV(RTReqPoolCreate(cMaxThreads, RT_MS_1SEC, 6, 500, "test1", &hPool), VINF_SUCCESS);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_THREAD_TYPE) == (uint64_t)RTTHREADTYPE_DEFAULT);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MAX_THREADS) == 10);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MIN_THREADS) > 1);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MS_MIN_IDLE) == RT_MS_1SEC);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MS_IDLE_SLEEP) == RT_MS_1SEC);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_PUSH_BACK_THRESHOLD) == 6);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_PUSH_BACK_MAX_MS) == 500);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_PUSH_BACK_MIN_MS) < 500);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MAX_FREE_REQUESTS) >= 10);
    RTTESTI_CHECK(RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MAX_FREE_REQUESTS) < 1024);

    RTTESTI_CHECK(RTReqPoolGetStat(hPool, RTREQPOOLSTAT_REQUESTS_FREE) == 0);
    RTTESTI_CHECK(RTReqPoolGetStat(hPool, RTREQPOOLSTAT_THREADS) == 0);
    uint32_t const cMinThreads  = RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MIN_THREADS);
    uint32_t const cMaxFreeReqs = RTReqPoolGetCfgVar(hPool, RTREQPOOLCFGVAR_MAX_FREE_REQUESTS);

    PRTREQ hReq;
    RTTESTI_CHECK_RC_RETV(RTReqPoolAlloc(hPool, RTREQTYPE_INTERNAL, &hReq), VINF_SUCCESS);
    RTTESTI_CHECK(RTReqRetain(hReq) == 2);
    RTTESTI_CHECK(RTReqRelease(hReq) == 1);
    RTTESTI_CHECK_RC(RTReqGetStatus(hReq), VERR_RT_REQUEST_STATUS_STILL_PENDING);
    RTTESTI_CHECK(RTReqRelease(hReq) == 0);
    RTTESTI_CHECK(RTReqPoolGetStat(hPool, RTREQPOOLSTAT_REQUESTS_FREE) == 1);

    RTTESTI_CHECK(RTReqPoolGetStat(hPool, RTREQPOOLSTAT_REQUESTS_PROCESSED) == 0);
    RTTESTI_CHECK_RC(RTReqPoolCallWait(hPool, (PFNRT)RTThreadSleep, 1, (RTMSINTERVAL)0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTReqPoolCallWait(hPool, (PFNRT)RTThreadSleep, 1, (RTMSINTERVAL)2), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTReqPoolCallWait(hPool, (PFNRT)RTThreadSleep, 1, (RTMSINTERVAL)3), VINF_SUCCESS);
    RTTESTI_CHECK(RTReqPoolGetStat(hPool, RTREQPOOLSTAT_REQUESTS_PROCESSED) > 1);
    RTTESTI_CHECK(RTReqPoolGetStat(hPool, RTREQPOOLSTAT_THREADS) == 1);

    /* Use no wait requests to maximize the number of worker threads. */
    RTTestISub("No wait requests");
    for (unsigned i = 0; i < 32; i++)
        RTTESTI_CHECK_RC(RTReqPoolCallNoWait(hPool, (PFNRT)RTThreadSleep, 1, (RTMSINTERVAL)100), VINF_SUCCESS);
    uint32_t cThreads = RTReqPoolGetStat(hPool, RTREQPOOLSTAT_THREADS);
    RTTestIValue("thread-count-1", cThreads, RTTESTUNIT_OCCURRENCES);
    RTTESTI_CHECK(cThreads >= cMinThreads);
    RTTESTI_CHECK(cThreads <= cMaxThreads);

    /* Check that idle thread shutdown kicks in.  This means delaying a bit first. */
    RTTestISub("Idle thread shutdown");
    for (unsigned i = 0; i < 20; i++)
    {
        RTTESTI_CHECK_RC(RTReqPoolCallNoWait(hPool, (PFNRT)RTThreadSleep, 1, (RTMSINTERVAL)10), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTReqPoolCallNoWait(hPool, (PFNRT)RTThreadSleep, 1, (RTMSINTERVAL)10), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTReqPoolCallWait(hPool, (PFNRT)RTThreadSleep, 1, (RTMSINTERVAL)100), VINF_SUCCESS);
    }
    RTTESTI_CHECK(RTReqPoolGetStat(hPool, RTREQPOOLSTAT_REQUESTS_FREE) == cMaxFreeReqs || cMaxFreeReqs > 32);

    /* Idle shutdown of worker threads should kick in now. */
    uint32_t cThreads2 = RTReqPoolGetStat(hPool, RTREQPOOLSTAT_THREADS);
    RTTestIValue("thread-count-2", cThreads2, RTTESTUNIT_OCCURRENCES);
    RTTESTI_CHECK(cThreads2 >= cMinThreads);
    RTTESTI_CHECK(cThreads2 < cThreads || cThreads2 <= cMinThreads);

    RTTESTI_CHECK(RTReqPoolRelease(hPool) == 0);
}


static void test2(void)
{
    RTTestISub("Simple Benchmark");

    RTREQPOOL hPool;
    RTTESTI_CHECK_RC_RETV(RTReqPoolCreate(10, RT_MS_1SEC, 6, 500, "test1", &hPool), VINF_SUCCESS);
    uint64_t NsTsStart = RTTimeNanoTS();
    for (unsigned i = 0; i < 10000; i++)
        RTTESTI_CHECK_RC_BREAK(RTReqPoolCallWait(hPool, (PFNRT)NopCallback, 0), VINF_SUCCESS);
    uint64_t cNsElapsed = RTTimeNanoTS() - NsTsStart;
    RTTestIValue("total time",              cNsElapsed, RTTESTUNIT_NS);
    RTTestIValue("per call",                cNsElapsed / 10000, RTTESTUNIT_NS_PER_CALL);
    RTTestIValue("total processing time",   RTReqPoolGetStat(hPool, RTREQPOOLSTAT_NS_AVERAGE_REQ_PROCESSING), RTTESTUNIT_NS_PER_CALL);

    RTTESTI_CHECK(RTReqPoolRelease(hPool) == 0);
}


int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTReqPool", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    test1();
    if (RTTestIErrorCount() == 0)
    {
        test2();
    }
    return RTTestSummaryAndDestroy(g_hTest);
}
