/* $Id: tstSupTscDelta.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SUP Testcase - Global Info Page TSC Delta Measurement Utility.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/getopt.h>
#include <iprt/test.h>
#include <iprt/thread.h>



int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitExAndCreate(argc, &argv, 0 /*fRtInit*/, "tstSupTscDelta", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Parse args
     */
    static const RTGETOPTDEF g_aOptions[] =
    {
        { "--iterations",       'i', RTGETOPT_REQ_INT32 },
        { "--delay",            'd', RTGETOPT_REQ_INT32 },
    };

    uint32_t cIterations = 0; /* Currently 0 so that it doesn't upset testing. */
    uint32_t cMsSleepBetweenIterations = 10;

    int           ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aOptions, RT_ELEMENTS(g_aOptions), 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'd':
                cMsSleepBetweenIterations = ValueUnion.u32;
                break;
            case 'i':
                cIterations = ValueUnion.u32;
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!cIterations)
        return RTTestSkipAndDestroy(hTest, "Nothing to do. The --iterations argument is 0 or not given.");

    /*
     * Init
     */
    PSUPDRVSESSION pSession = NIL_RTR0PTR;
    int rc = SUPR3Init(&pSession);
    if (RT_SUCCESS(rc))
    {
        PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
        if (pGip)
        {
            if (pGip->enmUseTscDelta < SUPGIPUSETSCDELTA_PRACTICALLY_ZERO)
                return RTTestSkipAndDestroy(hTest, "No deltas to play with: enmUseTscDelta=%d\n", pGip->enmUseTscDelta);

            /*
             * Init stats.
             */
            struct
            {
                int64_t iLowest;
                int64_t iHighest;
                int64_t iTotal;
                uint64_t uAbsMin;
                uint64_t uAbsMax;
                uint64_t uAbsTotal;
            } aCpuStats[RTCPUSET_MAX_CPUS];
            RT_ZERO(aCpuStats);
            for (uint32_t i = 0; i < pGip->cCpus; i++)
            {
                aCpuStats[i].iLowest  = INT64_MAX;
                aCpuStats[i].iHighest = INT64_MIN;
                aCpuStats[i].uAbsMin  = UINT64_MAX;
            }

            /*
             * Do the work.
             */
            for (uint32_t iIteration = 0; ; iIteration++)
            {
                /*
                 * Display the current deltas and gather statistics.
                 */
                RTPrintf("tstSupTscDelta: Iteration #%u results:", iIteration);
                for (uint32_t iCpu = 0; iCpu < pGip->cCpus; iCpu++)
                {
                    int64_t iTscDelta = pGip->aCPUs[iCpu].i64TSCDelta;

                    /* print */
                    if ((iCpu % 4) == 0)
                        RTPrintf("\ntstSupTscDelta:");
                    if (pGip->aCPUs[iCpu].enmState != SUPGIPCPUSTATE_ONLINE)
                        RTPrintf("  %02x: offline     ", iCpu);
                    else if (iTscDelta != INT64_MAX)
                        RTPrintf("  %02x: %-12lld", iCpu, iTscDelta);
                    else
                        RTPrintf("  %02x: INT64_MAX   ", iCpu);

                    /* stats */
                    if (   iTscDelta != INT64_MAX
                        && pGip->aCPUs[iCpu].enmState == SUPGIPCPUSTATE_ONLINE)
                    {
                        if (aCpuStats[iCpu].iLowest > iTscDelta)
                            aCpuStats[iCpu].iLowest = iTscDelta;
                        if (aCpuStats[iCpu].iHighest < iTscDelta)
                            aCpuStats[iCpu].iHighest = iTscDelta;
                        aCpuStats[iCpu].iTotal += iTscDelta;

                        uint64_t uAbsTscDelta = iTscDelta >= 0 ? (uint64_t)iTscDelta : (uint64_t)-iTscDelta;
                        if (aCpuStats[iCpu].uAbsMin > uAbsTscDelta)
                            aCpuStats[iCpu].uAbsMin = uAbsTscDelta;
                        if (aCpuStats[iCpu].uAbsMax < uAbsTscDelta)
                            aCpuStats[iCpu].uAbsMax = uAbsTscDelta;
                        aCpuStats[iCpu].uAbsTotal += uAbsTscDelta;
                    }
                }
                if (((pGip->cCpus - 1) % 4) != 0)
                    RTPrintf("\n");

                /*
                 * Done?
                 */
                if (iIteration + 1 >= cIterations)
                    break;

                /*
                 * Force a new measurement.
                 */
                RTThreadSleep(cMsSleepBetweenIterations);
                for (uint32_t iCpu = 0; iCpu < pGip->cCpus; iCpu++)
                    if (pGip->aCPUs[iCpu].enmState == SUPGIPCPUSTATE_ONLINE)
                    {
                        rc = SUPR3TscDeltaMeasure(pGip->aCPUs[iCpu].idCpu, false /*fAsync*/, true /*fForce*/, 64, 16 /*ms*/);
                        if (RT_FAILURE(rc))
                            RTTestFailed(hTest, "SUPR3TscDeltaMeasure failed on %#x: %Rrc", pGip->aCPUs[iCpu].idCpu, rc);
                    }
            }

            /*
             * Display statistics that we've gathered.
             */
            RTPrintf("tstSupTscDelta: Results:\n");
            int64_t  iLowest  = INT64_MAX;
            int64_t  iHighest = INT64_MIN;
            int64_t  iTotal   = 0;
            uint32_t cTotal   = 0;
            for (uint32_t iCpu = 0; iCpu < pGip->cCpus; iCpu++)
            {
                if (pGip->aCPUs[iCpu].enmState != SUPGIPCPUSTATE_ONLINE)
                    RTPrintf("tstSupTscDelta:  %02x: offline\n", iCpu);
                else
                {
                    RTPrintf("tstSupTscDelta:  %02x: lowest=%-12lld  highest=%-12lld  average=%-12lld  spread=%-12lld\n",
                             iCpu,
                             aCpuStats[iCpu].iLowest,
                             aCpuStats[iCpu].iHighest,
                             aCpuStats[iCpu].iTotal / cIterations,
                             aCpuStats[iCpu].iHighest - aCpuStats[iCpu].iLowest);
                    RTPrintf(  "tstSupTscDelta:      absmin=%-12llu   absmax=%-12llu   absavg=%-12llu  idCpu=%#4x  idApic=%#4x\n",
                             aCpuStats[iCpu].uAbsMin,
                             aCpuStats[iCpu].uAbsMax,
                             aCpuStats[iCpu].uAbsTotal / cIterations,
                             pGip->aCPUs[iCpu].idCpu,
                             pGip->aCPUs[iCpu].idApic);
                    if (iLowest > aCpuStats[iCpu].iLowest)
                        iLowest = aCpuStats[iCpu].iLowest;
                    if (iHighest < aCpuStats[iCpu].iHighest)
                        iHighest = aCpuStats[iCpu].iHighest;
                    iTotal += aCpuStats[iCpu].iHighest;
                    cTotal += cIterations;
                }
            }
            RTPrintf("tstSupTscDelta: all: lowest=%-12lld  highest=%-12lld  average=%-12lld  spread=%-12lld\n",
                     iLowest, iHighest, iTotal / cTotal, iHighest - iLowest);
        }
        else
            RTTestFailed(hTest, "g_pSUPGlobalInfoPage is NULL");

        SUPR3Term(false /*fForced*/);
    }
    else
        RTTestFailed(hTest, "SUPR3Init failed: %Rrc", rc);
    return RTTestSummaryAndDestroy(hTest);
}

