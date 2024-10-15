/* $Id: tstTimer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Timers.
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
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/errcore.h>
#include <iprt/string.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static volatile unsigned gcTicks;
static volatile uint64_t gu64Min;
static volatile uint64_t gu64Max;
static volatile uint64_t gu64Prev;
static volatile uint64_t gu64Norm;

static uint32_t cFrequency[200];

static DECLCALLBACK(void) TimerCallback(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    RT_NOREF_PV(pTimer); RT_NOREF_PV(pvUser); RT_NOREF_PV(iTick);

    gcTicks++;

    if (iTick != gcTicks)
        RTPrintf("tstTimer: FAILURE - iTick=%llu expected %u\n", iTick, gcTicks);

    const uint64_t u64Now = RTTimeNanoTS();
    if (gu64Prev)
    {
        const uint64_t u64Delta = u64Now - gu64Prev;
        if (u64Delta < gu64Min)
            gu64Min = u64Delta;
        if (u64Delta > gu64Max)
            gu64Max = u64Delta;
        int i = (int)(  RT_ELEMENTS(cFrequency)
                      - (u64Delta * (RT_ELEMENTS(cFrequency) / 2) / gu64Norm));
        if (i >= 0 && i < (int)RT_ELEMENTS(cFrequency))
            cFrequency[i]++;
    }
    gu64Prev = u64Now;
}


int main()
{
    /*
     * Init runtime
     */
    unsigned cErrors = 0;
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Check that the clock is reliable.
     */
    RTPrintf("tstTimer: TESTING - RTTimeNanoTS() for 2sec\n");
    uint64_t uTSMillies = RTTimeMilliTS();
    uint64_t uTSBegin = RTTimeNanoTS();
    uint64_t uTSLast = uTSBegin;
    uint64_t uTSDiff;
    uint64_t cIterations = 0;

    do
    {
        uint64_t uTS = RTTimeNanoTS();
        if (uTS < uTSLast)
        {
            RTPrintf("tstTimer: FAILURE - RTTimeNanoTS() is unreliable. uTS=%RU64 uTSLast=%RU64\n", uTS, uTSLast);
            cErrors++;
        }
        if (++cIterations > (2*1000*1000*1000))
        {
            RTPrintf("tstTimer: FAILURE - RTTimeNanoTS() is unreliable. cIterations=%RU64 uTS=%RU64 uTSBegin=%RU64\n", cIterations, uTS, uTSBegin);
            return 1;
        }
        uTSLast = uTS;
        uTSDiff = uTSLast - uTSBegin;
    } while (uTSDiff < (2*1000*1000*1000));
    uTSMillies = RTTimeMilliTS() - uTSMillies;
    if (uTSMillies >= 2500 || uTSMillies <= 1500)
    {
        RTPrintf("tstTimer: FAILURE - uTSMillies=%RI64 uTSBegin=%RU64 uTSLast=%RU64 uTSDiff=%RU64\n",
                 uTSMillies, uTSBegin, uTSLast, uTSDiff);
        cErrors++;
    }
    if (!cErrors)
        RTPrintf("tstTimer: OK      - RTTimeNanoTS()\n");

    /*
     * Tests.
     */
    static struct
    {
        unsigned uMicroInterval;
        unsigned uMilliesWait;
        unsigned cLower;
        unsigned cUpper;
    } aTests[] =
    {
        { 32000, 2000, 0, 0 },
        { 20000, 2000, 0, 0 },
        { 10000, 2000, 0, 0 },
        {  8000, 2000, 0, 0 },
        {  2000, 2000, 0, 0 },
        {  1000, 2000, 0, 0 },
        {   500, 5000, 0, 0 },
        {   200, 5000, 0, 0 },
        {   100, 5000, 0, 0 }
    };

    unsigned i = 0;
    for (i = 0; i < RT_ELEMENTS(aTests); i++)
    {
        aTests[i].cLower = (aTests[i].uMilliesWait*1000 - aTests[i].uMilliesWait*100) / aTests[i].uMicroInterval;
        aTests[i].cUpper = (aTests[i].uMilliesWait*1000 + aTests[i].uMilliesWait*100) / aTests[i].uMicroInterval;
        gu64Norm = aTests[i].uMicroInterval*1000;

        RTPrintf("\n"
                 "tstTimer: TESTING - %d us interval, %d ms wait, expects %d-%d ticks.\n",
                 aTests[i].uMicroInterval, aTests[i].uMilliesWait, aTests[i].cLower, aTests[i].cUpper);

        /*
         * Start timer which ticks every 10ms.
         */
        gcTicks = 0;
        PRTTIMER pTimer;
        gu64Max = 0;
        gu64Min = UINT64_MAX;
        gu64Prev = 0;
        RT_ZERO(cFrequency);
        rc = RTTimerCreateEx(&pTimer, aTests[i].uMicroInterval * (uint64_t)1000, 0, TimerCallback, NULL);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstTimer: FAILURE - RTTimerCreateEx(,%u*1M,,,) -> %Rrc\n", aTests[i].uMicroInterval, rc);
            cErrors++;
            continue;
        }

        /*
         * Start the timer and active waiting for the requested test period.
         */
        uTSBegin = RTTimeNanoTS();
        rc = RTTimerStart(pTimer, 0);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstTimer: FAILURE - RTTimerStart(,0) -> %Rrc\n", rc);
            cErrors++;
        }

        while (RTTimeNanoTS() - uTSBegin < (uint64_t)aTests[i].uMilliesWait * 1000000)
            /* nothing */;

        /* destroy the timer */
        uint64_t uTSEnd = RTTimeNanoTS();
        uTSDiff = uTSEnd - uTSBegin;
        rc = RTTimerDestroy(pTimer);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstTimer: FAILURE - RTTimerDestroy() -> %d gcTicks=%d\n", rc, gcTicks);
            cErrors++;
        }

        RTPrintf("tstTimer: uTS=%RI64 (%RU64 - %RU64)\n", uTSDiff, uTSBegin, uTSEnd);
        unsigned cTicks = gcTicks;
        RTThreadSleep(aTests[i].uMicroInterval/1000 * 3);
        if (gcTicks != cTicks)
        {
            RTPrintf("tstTimer: FAILURE - RTTimerDestroy() didn't really stop the timer! gcTicks=%d cTicks=%d\n", gcTicks, cTicks);
            cErrors++;
            continue;
        }

        /*
         * Check the number of ticks.
         */
        if (gcTicks < aTests[i].cLower)
        {
            RTPrintf("tstTimer: FAILURE - Too few ticks gcTicks=%d (expected %d-%d)", gcTicks, aTests[i].cUpper, aTests[i].cLower);
            cErrors++;
        }
        else if (gcTicks > aTests[i].cUpper)
        {
            RTPrintf("tstTimer: FAILURE - Too many ticks gcTicks=%d (expected %d-%d)", gcTicks, aTests[i].cUpper, aTests[i].cLower);
            cErrors++;
        }
        else
            RTPrintf("tstTimer: OK      - gcTicks=%d",  gcTicks);
        RTPrintf(" min=%RU64 max=%RU64\n", gu64Min, gu64Max);

        for (int j = 0; j < (int)RT_ELEMENTS(cFrequency); j++)
        {
            uint32_t len = cFrequency[j] * 70 / gcTicks;
            uint32_t deviation = j - RT_ELEMENTS(cFrequency) / 2;
            uint64_t u64FreqPercent = (uint64_t)cFrequency[j] * 10000 / gcTicks;
            uint64_t u64FreqPercentFrac = u64FreqPercent % 100;
            u64FreqPercent = u64FreqPercent / 100;
            RTPrintf("%+4d%c %6u %3llu.%02llu%% ",
                    deviation, deviation == 0 ? ' ' : '%', cFrequency[j],
                    u64FreqPercent, u64FreqPercentFrac);
            for (unsigned k = 0; k < len; k++)
                RTPrintf("*");
            RTPrintf("\n");
        }
    }

    /*
     * Summary.
     */
    if (!cErrors)
        RTPrintf("tstTimer: SUCCESS\n");
    else
        RTPrintf("tstTimer: FAILURE %d errors\n", cErrors);
    return !!cErrors;
}

