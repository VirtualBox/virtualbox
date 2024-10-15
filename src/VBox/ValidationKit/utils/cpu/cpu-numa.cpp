/* $Id: cpu-numa.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * numa - NUMA / memory benchmark.
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
#include <iprt/test.h>

#include <iprt/asm.h>
//#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
//# include <iprt/asm-amd64-x86.h>
//#endif
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The number of threads to skip when testing. */
static uint32_t g_cThreadsToSkip = 1;

/**
 * Gets the next online CPU.
 *
 * @returns Next CPU index or RTCPUSET_MAX_CPUS.
 * @param   iCurCpu             The current CPU (index).
 */
static int getNextCpu(unsigned iCurCpu)
{
    /* Skip to the next chip. */
    iCurCpu = (iCurCpu / g_cThreadsToSkip) * g_cThreadsToSkip;
    iCurCpu += g_cThreadsToSkip;

    /* Skip offline cpus. */
    while (   iCurCpu < RTCPUSET_MAX_CPUS
           && !RTMpIsCpuOnline(iCurCpu) )
        iCurCpu++;

    /* Make sure we're within bounds (in case of bad input). */
    if (iCurCpu > RTCPUSET_MAX_CPUS)
        iCurCpu = RTCPUSET_MAX_CPUS;
    return iCurCpu;
}


static void doTest(RTTEST hTest)
{
    NOREF(hTest);
    uint32_t iAllocCpu = 0;
    while (iAllocCpu < RTCPUSET_MAX_CPUS)
    {
        const uint32_t cbTestSet   = _1M * 32;
        const uint32_t cIterations = 384;

        /*
         * Change CPU and allocate a chunk of memory.
         */
        RTTESTI_CHECK_RC_OK_RETV(RTThreadSetAffinityToCpu(RTMpCpuIdFromSetIndex(iAllocCpu)));

        void *pvTest = RTMemPageAlloc(cbTestSet); /* may be leaked, who cares */
        RTTESTI_CHECK_RETV(pvTest != NULL);
        memset(pvTest, 0xef, cbTestSet);

        /*
         * Do the tests.
         */
        uint32_t iAccessCpu = 0;
        while (iAccessCpu < RTCPUSET_MAX_CPUS)
        {
            RTTESTI_CHECK_RC_OK_RETV(RTThreadSetAffinityToCpu(RTMpCpuIdFromSetIndex(iAccessCpu)));

            /*
             * The write test.
             */
            RTTimeNanoTS(); RTThreadYield();
            uint64_t u64StartTS = RTTimeNanoTS();
            for (uint32_t i = 0; i < cIterations; i++)
            {
                ASMCompilerBarrier(); /* paranoia */
                memset(pvTest, i, cbTestSet);
            }
            uint64_t const cNsElapsedWrite = RTTimeNanoTS() - u64StartTS;
            uint64_t cMBPerSec = (uint64_t)(  ((uint64_t)cIterations * cbTestSet) /* bytes */
                                            / ((long double)cNsElapsedWrite / RT_NS_1SEC_64) /* seconds */
                                            / _1M /* MB */ );
            RTTestIValueF(cMBPerSec, RTTESTUNIT_MEGABYTES_PER_SEC, "cpu%02u-mem%02u-write", iAllocCpu, iAccessCpu);

            /*
             * The read test.
             */
            memset(pvTest, 0, cbTestSet);
            RTTimeNanoTS(); RTThreadYield();
            u64StartTS = RTTimeNanoTS();
            for (uint32_t i = 0; i < cIterations; i++)
            {
#if 1
                size_t           u = 0;
                size_t volatile *puCur = (size_t volatile *)pvTest;
                size_t volatile *puEnd = puCur + cbTestSet / sizeof(size_t);
                while (puCur != puEnd)
                    u += *puCur++;
#else
                ASMCompilerBarrier(); /* paranoia */
                void *pvFound = memchr(pvTest, (i & 127) + 1, cbTestSet);
                RTTESTI_CHECK(pvFound == NULL);
#endif
            }
            uint64_t const cNsElapsedRead = RTTimeNanoTS() - u64StartTS;
            cMBPerSec = (uint64_t)(  ((uint64_t)cIterations * cbTestSet) /* bytes */
                                   / ((long double)cNsElapsedRead / RT_NS_1SEC_64) /* seconds */
                                   / _1M /* MB */ );
            RTTestIValueF(cMBPerSec, RTTESTUNIT_MEGABYTES_PER_SEC, "cpu%02u-mem%02u-read", iAllocCpu, iAccessCpu);

            /*
             * The read/write test.
             */
            RTTimeNanoTS(); RTThreadYield();
            u64StartTS = RTTimeNanoTS();
            for (uint32_t i = 0; i < cIterations; i++)
            {
                ASMCompilerBarrier(); /* paranoia */
                memcpy(pvTest, (uint8_t *)pvTest + cbTestSet / 2, cbTestSet / 2);
            }
            uint64_t const cNsElapsedRW = RTTimeNanoTS() - u64StartTS;
            cMBPerSec = (uint64_t)(  ((uint64_t)cIterations * cbTestSet) /* bytes */
                                   / ((long double)cNsElapsedRW / RT_NS_1SEC_64) /* seconds */
                                   / _1M /* MB */ );
            RTTestIValueF(cMBPerSec, RTTESTUNIT_MEGABYTES_PER_SEC, "cpu%02u-mem%02u-read-write", iAllocCpu, iAccessCpu);

            /*
             * Total time.
             */
            RTTestIValueF(cNsElapsedRead + cNsElapsedWrite + cNsElapsedRW, RTTESTUNIT_NS,
                          "cpu%02u-mem%02u-time", iAllocCpu, iAccessCpu);

            /* advance */
            iAccessCpu = getNextCpu(iAccessCpu);
        }

        /*
         * Clean up and advance to the next CPU.
         */
        RTMemPageFree(pvTest, cbTestSet);
        iAllocCpu = getNextCpu(iAllocCpu);
    }
}


int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("numa-1", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    /** @todo figure basic topology. */
#endif
    if (argc == 2)
        g_cThreadsToSkip = RTStrToUInt8(argv[1]);

    doTest(hTest);

    return RTTestSummaryAndDestroy(hTest);
}

