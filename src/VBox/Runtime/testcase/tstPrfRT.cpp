/* $Id: tstPrfRT.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT testcase - profile some of the important functions.
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
#include <iprt/initterm.h>
#include <iprt/time.h>
#include <iprt/log.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
DECLASM(void) tstRTPRfAMemoryAccess(void);
DECLASM(void) tstRTPRfARegisterAccess(void);
DECLASM(void) tstRTPRfAMemoryUnalignedAccess(void);
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)

static void PrintResult(uint64_t u64Ticks, uint64_t u64MaxTicks, uint64_t u64MinTicks, unsigned cTimes, const char *pszOperation)
{
    //RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
    //             "%-32s %5lld / %5lld / %5lld ticks per call (%u calls %lld ticks)\n",
    //             pszOperation, u64MinTicks, u64Ticks / (uint64_t)cTimes, u64MaxTicks, cTimes, u64Ticks);
    //RTTestValueF(g_hTest, u64MinTicks,                  RTTESTUNIT_NONE, "%s min ticks", pszOperation);
    RTTestValueF(g_hTest, u64Ticks / (uint64_t)cTimes,  RTTESTUNIT_NONE, "%s avg ticks", pszOperation);
    //RTTestValueF(g_hTest, u64MaxTicks,                  RTTESTUNIT_NONE, "%s max ticks", pszOperation);
    RT_NOREF_PV(u64MaxTicks); RT_NOREF_PV(u64MinTicks);
}

# define ITERATE(preexpr, expr, postexpr, cIterations) \
    AssertCompile(((cIterations) % 8) == 0); \
    /* Min and max value. */ \
    for (i = 0, u64MinTS = UINT64_MAX, u64MaxTS = 0; i < (cIterations); i++) \
    { \
        { preexpr } \
        uint64_t u64StartTS = ASMReadTSC(); \
        { expr } \
        uint64_t u64ElapsedTS = ASMReadTSC() - u64StartTS; \
        { postexpr } \
        if (u64ElapsedTS > u64MinTS * 32) \
        { \
            i--; \
            continue; \
        } \
        if (u64ElapsedTS < u64MinTS) \
            u64MinTS = u64ElapsedTS; \
        if (u64ElapsedTS > u64MaxTS) \
            u64MaxTS = u64ElapsedTS; \
    } \
    { \
        /* Calculate a good average value (may be smaller than min). */ \
        i = (cIterations); \
        AssertRelease((i % 8) == 0); \
        { preexpr } \
        uint64_t u64StartTS = ASMReadTSC(); \
        while (i != 0) \
        { \
            { expr } \
            { expr } \
            { expr } \
            { expr } \
            { expr } \
            { expr } \
            { expr } \
            { expr } \
            i -= 8; \
        } \
        u64TotalTS = ASMReadTSC() - u64StartTS; \
        { postexpr } \
        i = (cIterations); \
    }

#else  /* !AMD64 && !X86 */

void PrintResult(uint64_t cNs, uint64_t cNsMax, uint64_t cNsMin, unsigned cTimes, const char *pszOperation)
{
    RT_NOREF(cNsMax, cNsMin);
    //RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
    //             "%-32s %5lld / %5lld / %5lld ns per call (%u calls %lld ns)\n",
    //             pszOperation, cNsMin, cNs / (uint64_t)cTimes, cNsMax, cTimes, cNs);
    //RTTestValueF(g_hTest, cNsMin,                  RTTESTUNIT_NS_PER_CALL, "%s min", pszOperation);
    RTTestValueF(g_hTest, cNs / (uint64_t)cTimes,  RTTESTUNIT_NS_PER_CALL, "%s avg", pszOperation);
    //RTTestValueF(g_hTest, cNsMax,                  RTTESTUNIT_NS_PER_CALL, "%s max", pszOperation);
}

# define ITERATE(preexpr, expr, postexpr, cIterations) \
    for (i = 0, u64TotalTS = 0, u64MinTS = UINT64_MAX, u64MaxTS = 0; i < (cIterations); i++) \
    { \
        { preexpr } \
        uint64_t u64StartTS = RTTimeNanoTS(); \
        { expr } \
        uint64_t u64ElapsedTS = RTTimeNanoTS() - u64StartTS; \
        { postexpr } \
        if (u64ElapsedTS > u64MinTS * 32) \
        { \
            i--; \
            continue; \
        } \
        if (u64ElapsedTS < u64MinTS) \
            u64MinTS = u64ElapsedTS; \
        if (u64ElapsedTS > u64MaxTS) \
            u64MaxTS = u64ElapsedTS; \
        u64TotalTS += u64ElapsedTS; \
    }

#endif /* !AMD64 && !X86 */


int main(int argc, char **argv)
{
    uint64_t    u64TotalTS;
    uint64_t    u64MinTS;
    uint64_t    u64MaxTS;
    uint32_t    i;

    RTEXITCODE rcExit = RTTestInitExAndCreate(argc, &argv, argc == 2 ? RTR3INIT_FLAGS_SUPLIB : 0, "tstRTPrf", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    /*
     * RTTimeNanoTS, RTTimeProgramNanoTS, RTTimeMilliTS, and RTTimeProgramMilliTS.
     */
    ITERATE(RT_NOTHING, RTTimeNanoTS();, RT_NOTHING, _32M);
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTTimeNanoTS");

    ITERATE(RT_NOTHING, RTTimeProgramNanoTS();, RT_NOTHING, UINT32_C(1000000));
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTTimeProgramNanoTS");

    ITERATE(RT_NOTHING, RTTimeMilliTS();, RT_NOTHING, UINT32_C(1000000));
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTTimeMilliTS");

    ITERATE(RT_NOTHING, RTTimeProgramMilliTS();, RT_NOTHING, UINT32_C(1000000));
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTTimeProgramMilliTS");

    /*
     * RTTimeNow
     */
    RTTIMESPEC Time;
    ITERATE(RT_NOTHING, RTTimeNow(&Time);, RT_NOTHING, UINT32_C(1000000));
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTTimeNow");

    /*
     * RTLogDefaultInstance()
     */
    ITERATE(RT_NOTHING, RTLogDefaultInstance();, RT_NOTHING, UINT32_C(1000000));
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTLogDefaultInstance");

    /*
     * RTThreadSelf and RTThreadNativeSelf
     */
    ITERATE(RT_NOTHING, RTThreadSelf();, RT_NOTHING, UINT32_C(1000000));
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTThreadSelf");

    ITERATE(RT_NOTHING, RTThreadNativeSelf();, RT_NOTHING, UINT32_C(1000000));
    PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "RTThreadNativeSelf");

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    /*
     * Registers vs stack.
     */
    ITERATE(RT_NOTHING, tstRTPRfARegisterAccess();, RT_NOTHING, UINT32_C(1000));
    uint64_t const cRegTotal = u64TotalTS;
    //PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "Register only algorithm");

    ITERATE(RT_NOTHING, tstRTPRfAMemoryAccess();, RT_NOTHING, UINT32_C(1000));
    uint64_t const cMemTotal = u64TotalTS;
    //PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "Memory only algorithm");

    ITERATE(RT_NOTHING, tstRTPRfAMemoryUnalignedAccess();, RT_NOTHING, UINT32_C(1000));
    uint64_t const cMemUnalignedTotal = u64TotalTS;
    //PrintResult(u64TotalTS, u64MaxTS, u64MinTS, i, "Memory only algorithm");

    uint64_t const cSlower100 = cMemTotal * 100 / cRegTotal;
    RTTestValue(g_hTest, "Memory instead of registers slowdown", cSlower100, RTTESTUNIT_PCT);
    uint64_t const cUnalignedSlower100 = cMemUnalignedTotal * 100 / cRegTotal;
    RTTestValue(g_hTest, "Unaligned memory instead of registers slowdown", cUnalignedSlower100, RTTESTUNIT_PCT);
#endif

    return RTTestSummaryAndDestroy(g_hTest);
}
