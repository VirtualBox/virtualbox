/* $Id: nttimesources.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Check the various time sources on Windows NT.
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
#include <iprt/win/windows.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct _MY_KSYSTEM_TIME
{
    ULONG LowPart;
    LONG High1Time;
    LONG High2Time;
} MY_KSYSTEM_TIME;

typedef struct _MY_KUSER_SHARED_DATA
{
    ULONG TickCountLowDeprecated;
    ULONG TickCountMultiplier;
    volatile MY_KSYSTEM_TIME InterruptTime;
    volatile MY_KSYSTEM_TIME SystemTime;
    volatile MY_KSYSTEM_TIME TimeZoneBias;
    /* The rest is not relevant. */
} MY_KUSER_SHARED_DATA;

/** The fixed pointer to the user shared data. */
#define MY_USER_SHARED_DATA ((MY_KUSER_SHARED_DATA *)0x7ffe0000)

/** Spins until GetTickCount() changes. */
static void SpinUntilTick(void)
{
    /* spin till GetTickCount changes. */
    DWORD dwMsTick = GetTickCount();
    while (GetTickCount() == dwMsTick)
        /* nothing */;
}

/** Delay function that tries to return right after GetTickCount changed. */
static void DelayMillies(DWORD dwMsStart, DWORD cMillies)
{
    /* Delay cMillies - 1. */
    Sleep(cMillies - 1);
    while (GetTickCount() - dwMsStart < cMillies - 1U)
        Sleep(1);

    SpinUntilTick();
}


int main(int argc, char **argv)
{
    RT_NOREF1(argv);

    /*
     * Init, create a test instance and "parse" arguments.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("nttimesources", &hTest);
    if (rc)
        return rc;
    if (argc > 1)
    {
        RTTestFailed(hTest, "Syntax error! no arguments expected");
        return RTTestSummaryAndDestroy(hTest);
    }

    /*
     * Guess MHz using GetTickCount.
     */
    RTTestSub(hTest, "Guess MHz");
    DWORD       dwTickStart, dwTickEnd, cMsTicks;
    uint64_t    u64TscStart, u64TscEnd, cTscTicks;

    /* get a good start time. */
    SpinUntilTick();
    do
    {
        dwTickStart = GetTickCount();
        ASMCompilerBarrier();
        ASMSerializeInstruction();
        u64TscStart = ASMReadTSC();
        ASMCompilerBarrier();
    } while (GetTickCount() != dwTickStart);

    /* delay a good while. */
    DelayMillies(dwTickStart, 256);

    /* get a good end time. */
    do
    {
        dwTickEnd = GetTickCount();
        ASMCompilerBarrier();
        ASMSerializeInstruction();
        u64TscEnd = ASMReadTSC();
        ASMCompilerBarrier();
    } while (GetTickCount() != dwTickEnd);
    cMsTicks  = dwTickEnd - dwTickStart;
    cTscTicks = u64TscEnd - u64TscStart;

    /* Calc an approximate TSC frequency:
            cTscTicks / uTscHz = cMsTicks / 1000
                    1 / uTscHz = (cMsTicks / 1000) / cTscTicks
                        uTscHz = cTscTicks / (cMsTicks / 1000) */
    uint64_t u64TscHz = (long double)cTscTicks / ((long double)cMsTicks / 1000.0);
    if (    u64TscHz > _1M*3
        &&  u64TscHz < _1T)
        RTTestPrintf(hTest,  RTTESTLVL_ALWAYS,  "u64TscHz=%'llu", u64TscHz);
    else
    {
        RTTestFailed(hTest, "u64TscHz=%'llu - out of range", u64TscHz);
        u64TscHz = 0;
    }


    /*
     * Pit GetTickCount, InterruptTime, Performance Counters and TSC against each other.
     */
    LARGE_INTEGER PrfHz;
    LARGE_INTEGER PrfStart, PrfEnd, cPrfTicks;
    LARGE_INTEGER IntStart, IntEnd, cIntTicks;
    for (uint32_t i = 0; i < 7; i++)
    {
        RTTestSubF(hTest, "The whole bunch - pass #%u", i + 1);

        if (!QueryPerformanceFrequency(&PrfHz))
        {
            RTTestFailed(hTest, "QueryPerformanceFrequency failed (%u)", GetLastError());
            return RTTestSummaryAndDestroy(hTest);
        }

        /* get a good start time. */
        SpinUntilTick();
        do
        {
            IntStart.HighPart = MY_USER_SHARED_DATA->InterruptTime.High1Time;
            IntStart.LowPart  = MY_USER_SHARED_DATA->InterruptTime.LowPart;
            dwTickStart = GetTickCount();
            if (!QueryPerformanceCounter(&PrfStart))
            {
                RTTestFailed(hTest, "QueryPerformanceCounter failed (%u)", GetLastError());
                return RTTestSummaryAndDestroy(hTest);
            }
            ASMCompilerBarrier();
            ASMSerializeInstruction();
            u64TscStart = ASMReadTSC();
            ASMCompilerBarrier();
        } while (   MY_USER_SHARED_DATA->InterruptTime.High2Time != IntStart.HighPart
                 || MY_USER_SHARED_DATA->InterruptTime.LowPart   != IntStart.LowPart
                 || GetTickCount() != dwTickStart);

        /* delay a good while. */
        DelayMillies(dwTickStart, 256);

        /* get a good end time. */
        do
        {
            IntEnd.HighPart = MY_USER_SHARED_DATA->InterruptTime.High1Time;
            IntEnd.LowPart  = MY_USER_SHARED_DATA->InterruptTime.LowPart;
            dwTickEnd = GetTickCount();
            if (!QueryPerformanceCounter(&PrfEnd))
            {
                RTTestFailed(hTest, "QueryPerformanceCounter failed (%u)", GetLastError());
                return RTTestSummaryAndDestroy(hTest);
            }
            ASMCompilerBarrier();
            ASMSerializeInstruction();
            u64TscEnd = ASMReadTSC();
            ASMCompilerBarrier();
        } while (   MY_USER_SHARED_DATA->InterruptTime.High2Time != IntEnd.HighPart
                 || MY_USER_SHARED_DATA->InterruptTime.LowPart   != IntEnd.LowPart
                 || GetTickCount() != dwTickEnd);

        cMsTicks           = dwTickEnd - dwTickStart;
        cTscTicks          = u64TscEnd - u64TscStart;
        cIntTicks.QuadPart = IntEnd.QuadPart - IntStart.QuadPart;
        cPrfTicks.QuadPart = PrfEnd.QuadPart - PrfStart.QuadPart;

        /* Recalc to micro seconds. */
        uint64_t u64MicroSecMs  = (uint64_t)cMsTicks * 1000;
        uint64_t u64MicroSecTsc = u64TscHz ? (long double)cTscTicks / (long double)u64TscHz       * 1000000 : u64MicroSecMs;
        uint64_t u64MicroSecPrf =   (long double)cPrfTicks.QuadPart / (long double)PrfHz.QuadPart * 1000000;
        uint64_t u64MicroSecInt = cIntTicks.QuadPart / 10; /* 100ns units*/

        /* check how much they differ using the millisecond tick count as the standard candle. */
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, " %9llu / %7lld us - GetTickCount\n", u64MicroSecMs, 0);

        int64_t off = u64MicroSecTsc - u64MicroSecMs;
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, " %9llu / %7lld us - TSC\n", u64MicroSecTsc, off);
        RTTEST_CHECK(hTest, RT_ABS(off) < 50000 /*us*/); /* some extra uncertainty with TSC.  */

        off = u64MicroSecInt - u64MicroSecMs;
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, " %9llu / %7lld us - InterruptTime\n", u64MicroSecInt, off);
        RTTEST_CHECK(hTest, RT_ABS(off) < 25000 /*us*/);

        off = u64MicroSecPrf - u64MicroSecMs;
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, " %9llu / %7lld us - QueryPerformanceCounter\n", u64MicroSecPrf, off);
        RTTEST_CHECK(hTest, RT_ABS(off) < 25000 /*us*/);
    }

    return RTTestSummaryAndDestroy(hTest);
}

