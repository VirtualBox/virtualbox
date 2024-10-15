/* $Id: tstRTTimerLR.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Low Resolution Timers.
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
#include <iprt/timer.h>

#include <iprt/errcore.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static volatile unsigned gcTicks;
static volatile uint64_t gu64Min;
static volatile uint64_t gu64Max;
static volatile uint64_t gu64Prev;


static DECLCALLBACK(void) TimerLRCallback(RTTIMERLR hTimerLR, void *pvUser, uint64_t iTick)
{
    RT_NOREF_PV(hTimerLR); RT_NOREF_PV(pvUser); RT_NOREF_PV(iTick);

    gcTicks++;

    const uint64_t u64Now = RTTimeNanoTS();
    if (gu64Prev)
    {
        const uint64_t u64Delta = u64Now - gu64Prev;
        if (u64Delta < gu64Min)
            gu64Min = u64Delta;
        if (u64Delta > gu64Max)
            gu64Max = u64Delta;
    }
    gu64Prev = u64Now;
}


int main()
{
    /*
     * Init runtime
     */
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTTimerLR", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Check that the clock is reliable.
     */
    RTTestSub(hTest, "RTTimeNanoTS() for 2sec");
    uint64_t uTSMillies = RTTimeMilliTS();
    uint64_t uTSBegin = RTTimeNanoTS();
    uint64_t uTSLast = uTSBegin;
    uint64_t uTSDiff;
    uint64_t cIterations = 0;

    do
    {
        uint64_t uTS = RTTimeNanoTS();
        if (uTS < uTSLast)
            RTTestFailed(hTest, "RTTimeNanoTS() is unreliable. uTS=%RU64 uTSLast=%RU64", uTS, uTSLast);
        if (++cIterations > 2*1000*1000*1000)
        {
            RTTestFailed(hTest, "RTTimeNanoTS() is unreliable. cIterations=%RU64 uTS=%RU64 uTSBegin=%RU64",
                         cIterations, uTS, uTSBegin);
            return RTTestSummaryAndDestroy(hTest);
        }
        uTSLast = uTS;
        uTSDiff = uTSLast - uTSBegin;
    } while (uTSDiff < (2*1000*1000*1000));
    uTSMillies = RTTimeMilliTS() - uTSMillies;
    if (uTSMillies >= 2500 || uTSMillies <= 1500)
        RTTestFailed(hTest, "uTSMillies=%RI64 uTSBegin=%RU64 uTSLast=%RU64 uTSDiff=%RU64",
                     uTSMillies, uTSBegin, uTSLast, uTSDiff);

    /*
     * Tests.
     */
    static struct
    {
        unsigned uMilliesInterval;
        unsigned uMilliesWait;
        unsigned cLower;
        unsigned cUpper;
    } aTests[] =
    {
        { 1000, 2500, 3,   3 }, /* (keep in mind the immediate first tick) */
        {  250, 2000, 6,  10 },
        {  100, 2000, 17, 23 },
    };

    int rc;
    unsigned i = 0;
    for (i = 0; i < RT_ELEMENTS(aTests); i++)
    {
        //aTests[i].cLower = (aTests[i].uMilliesWait - aTests[i].uMilliesWait / 10) / aTests[i].uMilliesInterval;
        //aTests[i].cUpper = (aTests[i].uMilliesWait + aTests[i].uMilliesWait / 10) / aTests[i].uMilliesInterval;

        RTTestSubF(hTest, "%d ms interval, %d ms wait, expects %d-%d ticks",
                   aTests[i].uMilliesInterval, aTests[i].uMilliesWait, aTests[i].cLower, aTests[i].cUpper);

        /*
         * Start timer which ticks every 10ms.
         */
        gcTicks = 0;
        RTTIMERLR hTimerLR;
        gu64Max = 0;
        gu64Min = UINT64_MAX;
        gu64Prev = 0;
        rc = RTTimerLRCreateEx(&hTimerLR, aTests[i].uMilliesInterval * (uint64_t)1000000, 0, TimerLRCallback, NULL);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(hTest, "RTTimerLRCreateEX(,%u*1M,,,) -> %Rrc", aTests[i].uMilliesInterval, rc);
            continue;
        }

        /*
         * Start the timer an actively wait for it for the period requested.
         */
        uTSBegin = RTTimeNanoTS();
        rc = RTTimerLRStart(hTimerLR, 0);
        if (RT_FAILURE(rc))
            RTTestFailed(hTest, "RTTimerLRStart() -> %Rrc", rc);

        while (RTTimeNanoTS() - uTSBegin < (uint64_t)aTests[i].uMilliesWait * 1000000)
            RTThreadSleep(1);

        /* don't stop it, destroy it because there are potential races in destroying an active timer. */
        rc = RTTimerLRDestroy(hTimerLR);
        if (RT_FAILURE(rc))
            RTTestFailed(hTest, "RTTimerLRDestroy() -> %Rrc gcTicks=%d", rc, gcTicks);

        uint64_t uTSEnd = RTTimeNanoTS();
        uTSDiff = uTSEnd - uTSBegin;
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "uTS=%'RI64 (%'RU64 - %'RU64) gcTicks=%u min=%'RU64 max=%'RU64\n",
                     uTSDiff, uTSBegin, uTSEnd, gcTicks, gu64Min, gu64Max);

        /* Check that it really stopped. */
        unsigned cTicks = gcTicks;
        RTThreadSleep(aTests[i].uMilliesInterval * 2);
        if (gcTicks != cTicks)
        {
            RTTestFailed(hTest, "RTTimerLRDestroy() didn't really stop the timer! gcTicks=%d cTicks=%d", gcTicks, cTicks);
            continue;
        }

        /*
         * Check the number of ticks.
         */
        if (gcTicks < aTests[i].cLower)
            RTTestFailed(hTest, "Too few ticks gcTicks=%d (expected %d-%d)", gcTicks, aTests[i].cUpper, aTests[i].cLower);
        else if (gcTicks > aTests[i].cUpper)
            RTTestFailed(hTest, "Too many ticks gcTicks=%d (expected %d-%d)", gcTicks, aTests[i].cUpper, aTests[i].cLower);
    }

    /*
     * Test changing the interval dynamically
     */
    RTTestSub(hTest, "RTTimerLRChangeInterval");
    do
    {
        RTTIMERLR hTimerLR;
        rc = RTTimerLRCreateEx(&hTimerLR, aTests[0].uMilliesInterval * (uint64_t)1000000, 0, TimerLRCallback, NULL);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(hTest, "RTTimerLRCreateEX(,%u*1M,,,) -> %Rrc", aTests[0].uMilliesInterval, rc);
            break;
        }

        for (i = 0; i < RT_ELEMENTS(aTests); i++)
        {
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%d ms interval, %d ms wait, expects %d-%d ticks.\n",
                         aTests[i].uMilliesInterval, aTests[i].uMilliesWait, aTests[i].cLower, aTests[i].cUpper);

            gcTicks = 0;
            gu64Max = 0;
            gu64Min = UINT64_MAX;
            gu64Prev = 0;

            /*
             * Start the timer an actively wait for it for the period requested.
             */
            uTSBegin = RTTimeNanoTS();
            if (i == 0)
            {
                rc = RTTimerLRStart(hTimerLR, 0);
                if (RT_FAILURE(rc))
                    RTTestFailed(hTest, "RTTimerLRStart() -> %Rrc", rc);
            }
            else
            {
                rc = RTTimerLRChangeInterval(hTimerLR, aTests[i].uMilliesInterval * RT_NS_1MS_64);
                if (RT_FAILURE(rc))
                    RTTestFailed(hTest, "RTTimerLRChangeInterval() -> %d gcTicks=%d", rc, gcTicks);
            }

            while (RTTimeNanoTS() - uTSBegin < (uint64_t)aTests[i].uMilliesWait * RT_NS_1MS_64)
                RTThreadSleep(1);

            uint64_t uTSEnd = RTTimeNanoTS();
            uTSDiff = uTSEnd - uTSBegin;
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "uTS=%'RI64 (%'RU64 - %'RU64) gcTicks=%u min=%'RU64 max=%'RU64\n",
                         uTSDiff, uTSBegin, uTSEnd, gcTicks, gu64Min, gu64Max);

            /*
             * Check the number of ticks.
             */
            if (gcTicks < aTests[i].cLower)
                RTTestFailed(hTest, "Too few ticks gcTicks=%d (expected %d-%d)", gcTicks, aTests[i].cUpper, aTests[i].cLower);
            else if (gcTicks > aTests[i].cUpper)
                RTTestFailed(hTest, "Too many ticks gcTicks=%d (expected %d-%d)", gcTicks, aTests[i].cUpper, aTests[i].cLower);
        }

        /* don't stop it, destroy it because there are potential races in destroying an active timer. */
        rc = RTTimerLRDestroy(hTimerLR);
        if (RT_FAILURE(rc))
            RTTestFailed(hTest, "RTTimerLRDestroy() -> %d gcTicks=%d", rc, gcTicks);
    } while (0);

    /*
     * Test multiple timers running at once.
     */
    /** @todo multiple LR timer testcase. */

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

