/* $Id: tstRTTime.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Simple RTTime tests (requires GIP).
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
#include <iprt/time.h>

#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/test.h>
#include <iprt/thread.h>

int main()
{
    /*
     * Init.
     */
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitExAndCreate(0, NULL, RTR3INIT_FLAGS_SUPLIB, "tstRTTime", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * RTNanoTimeTS() shall never return something which
     * is less or equal to the return of the previous call.
     */

    /* Take down the start time of both sources, try get them w/o being rescheduled. */
    RTTimeSystemNanoTS(); RTTimeNanoTS(); RTThreadYield();
    uint64_t u64RTStartTS = RTTimeNanoTS();
    uint64_t u64OSStartTS = RTTimeSystemNanoTS();
    uint64_t const cNsMaxTolerance = 256 * RT_NS_1US;
    for (unsigned cTries = 0; cTries < 32 && RTTimeNanoTS() - u64RTStartTS > cNsMaxTolerance; cTries++)
    {
        RTThreadYield();
        u64RTStartTS = RTTimeNanoTS();
        u64OSStartTS = RTTimeSystemNanoTS();
    }

    /* Test loop. */
    uint32_t i;
    uint64_t u64Prev = RTTimeNanoTS();
    for (i = 0; i < 100*_1M; i++)
    {
        uint64_t u64 = RTTimeNanoTS();
        if (u64 <= u64Prev)
        {
            /** @todo wrapping detection. */
            RTTestFailed(hTest, "i=%#010x u64=%#llx u64Prev=%#llx (1)\n", i, u64, u64Prev);
            if (RTTestErrorCount(hTest) >= 256)
                break;
            RTThreadYield();
            u64 = RTTimeNanoTS();
        }
        else if (u64 - u64Prev > 1000000000 /* 1sec */)
        {
            RTTestFailed(hTest, "i=%#010x u64=%#llx u64Prev=%#llx delta=%lld\n", i, u64, u64Prev, u64 - u64Prev);
            if (RTTestErrorCount(hTest) >= 256)
                break;
            RTThreadYield();
            u64 = RTTimeNanoTS();
        }
        if (!(i & (_1M*2 - 1)))
        {
            RTTestPrintf(hTest, RTTESTLVL_INFO, "i=%#010x u64=%#llx u64Prev=%#llx delta=%lld\n", i, u64, u64Prev, u64 - u64Prev);
            RTThreadYield();
            u64 = RTTimeNanoTS();
        }
        u64Prev = u64;
    }

    /* Take down the stop time of both sources, again try get them w/o being rescheduled. */
    RTTimeSystemNanoTS(); RTTimeNanoTS(); RTThreadYield();
    uint64_t u64RTElapsedTS = RTTimeNanoTS();
    uint64_t u64OSElapsedTS = RTTimeSystemNanoTS();
    for (unsigned cTries = 0; cTries < 32 && RTTimeNanoTS() - u64RTElapsedTS > cNsMaxTolerance; cTries++)
    {
        RTThreadYield();
        u64RTElapsedTS = RTTimeNanoTS();
        u64OSElapsedTS = RTTimeSystemNanoTS();
    }
    u64RTElapsedTS -= u64RTStartTS;
    u64OSElapsedTS -= u64OSStartTS;

    /* Check the runtime difference between the two sources. */
    int64_t i64Diff = u64OSElapsedTS >= u64RTElapsedTS ? u64OSElapsedTS - u64RTElapsedTS : u64RTElapsedTS - u64OSElapsedTS;
    if (i64Diff > (int64_t)(RT_MAX(u64OSElapsedTS / 1000, cNsMaxTolerance)))
        RTTestFailed(hTest, "total time differs too much! u64OSElapsedTS=%#llx u64RTElapsedTS=%#llx delta=%lld\n",
                     u64OSElapsedTS, u64RTElapsedTS, u64OSElapsedTS - u64RTElapsedTS);
    else
    {
        if (u64OSElapsedTS >= u64RTElapsedTS)
            RTTestValue(hTest, "Total time delta", u64OSElapsedTS - u64RTElapsedTS, RTTESTUNIT_NS);
        else
            RTTestValue(hTest, "Total time delta", u64RTElapsedTS - u64OSElapsedTS, RTTESTUNIT_NS);
        RTTestPrintf(hTest, RTTESTLVL_INFO, "total time difference: u64OSElapsedTS=%#llx u64RTElapsedTS=%#llx delta=%lld\n",
                     u64OSElapsedTS, u64RTElapsedTS, u64OSElapsedTS - u64RTElapsedTS);
    }

    /* Report debug details: */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) /** @todo This isn't really x86 or AMD64 specific... */
    RTTestValue(hTest, "RTTimeDbgSteps",        RTTimeDbgSteps(),                           RTTESTUNIT_OCCURRENCES);
    RTTestValue(hTest, "RTTimeDbgSteps pp",     ((uint64_t)RTTimeDbgSteps() * 1000) / i,    RTTESTUNIT_PP1K);
    RTTestValue(hTest, "RTTimeDbgExpired",      RTTimeDbgExpired(),                         RTTESTUNIT_OCCURRENCES);
    RTTestValue(hTest, "RTTimeDbgExpired pp",   ((uint64_t)RTTimeDbgExpired() * 1000) / i,  RTTESTUNIT_PP1K);
    RTTestValue(hTest, "RTTimeDbgBad",          RTTimeDbgBad(),                             RTTESTUNIT_OCCURRENCES);
    RTTestValue(hTest, "RTTimeDbgBad pp",       ((uint64_t)RTTimeDbgBad() * 1000) / i,      RTTESTUNIT_PP1K);
    RTTestValue(hTest, "RTTimeDbgRaces",        RTTimeDbgRaces(),                           RTTESTUNIT_OCCURRENCES);
    RTTestValue(hTest, "RTTimeDbgRaces pp",     ((uint64_t)RTTimeDbgRaces() * 1000) / i,    RTTESTUNIT_PP1K);
#endif

    return RTTestSummaryAndDestroy(hTest);
}
