/* $Id: tstTSC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - SMP TSC testcase.
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
#include <iprt/asm-amd64-x86.h>
#include <iprt/asm.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mp.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TSCDATA
{
    /** The TSC.  */
    uint64_t volatile   TSC;
    /** The APIC ID. */
    uint8_t volatile    u8ApicId;
    /** Did it succeed? */
    bool volatile       fRead;
    /** Did it fail? */
    bool volatile       fFailed;
    /** The thread handle. */
    RTTHREAD            Thread;
} TSCDATA, *PTSCDATA;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The number of CPUs waiting on their user event semaphore. */
static volatile uint32_t g_cWaiting;
/** The number of CPUs ready (in spin) to do the TSC read. */
static volatile uint32_t g_cReady;
/** The variable the CPUs are spinning on.
 * 0: Spin.
 * 1: Go ahead.
 * 2: You're too late, back to square one. */
static volatile uint32_t g_u32Go;
/** The number of CPUs that managed to read the TSC. */
static volatile uint32_t g_cRead;
/** The number of CPUs that failed to read the TSC. */
static volatile uint32_t g_cFailed;

/** Indicator forcing the threads to quit. */
static volatile bool g_fDone;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) ThreadFunction(RTTHREAD Thread, void *pvUser);


/**
 * Thread function for catching the other cpus.
 *
 * @returns VINF_SUCCESS (we don't care).
 * @param   Thread  The thread handle.
 * @param   pvUser  PTSCDATA.
 */
static DECLCALLBACK(int) ThreadFunction(RTTHREAD Thread, void *pvUser)
{
    PTSCDATA pTscData = (PTSCDATA)pvUser;

    while (!g_fDone)
    {
        /*
         * Wait.
         */
        ASMAtomicIncU32(&g_cWaiting);
        RTThreadUserWait(Thread, RT_INDEFINITE_WAIT);
        RTThreadUserReset(Thread);
        ASMAtomicDecU32(&g_cWaiting);
        if (g_fDone)
            break;

        /*
         * Spin.
         */
        ASMAtomicIncU32(&g_cReady);
        while (!g_fDone)
        {
            const uint8_t   ApicId1 = ASMGetApicId();
            const uint64_t  TSC1    = ASMReadTSC();
            const uint32_t  u32Go   = g_u32Go;
            if (u32Go == 0)
                continue;

            if (u32Go == 1)
            {
                /* do the reading. */
                const uint8_t   ApicId2 = ASMGetApicId();
                const uint64_t  TSC2    = ASMReadTSC();
                const uint8_t   ApicId3 = ASMGetApicId();
                const uint64_t  TSC3    = ASMReadTSC();
                const uint8_t   ApicId4 = ASMGetApicId();

                if (    ApicId1 == ApicId2
                    &&  ApicId1 == ApicId3
                    &&  ApicId1 == ApicId4
                    &&  TSC3 - TSC1 < 2250 /* WARNING: This is just a guess, increase if it doesn't work for you. */
                    &&  TSC2 - TSC1 < TSC3 - TSC1
                    )
                {
                    /* succeeded. */
                    pTscData->TSC = TSC2;
                    pTscData->u8ApicId = ApicId1;
                    pTscData->fFailed = false;
                    pTscData->fRead = true;
                    ASMAtomicIncU32(&g_cRead);
                    break;
                }
            }

            /* failed */
            pTscData->fFailed = true;
            pTscData->fRead = false;
            ASMAtomicIncU32(&g_cFailed);
            break;
        }
    }

    return VINF_SUCCESS;
}

static int tstTSCCalcDrift(void)
{
    /*
     * This is only relevant to on SMP systems.
     */
    const unsigned cCpus = RTMpGetOnlineCount();
    if (cCpus <= 1)
    {
        RTPrintf("tstTSC: SKIPPED - Only relevant on SMP systems\n");
        return 0;
    }

    /*
     * Create the threads.
     */
    static TSCDATA s_aData[254];
    uint32_t i;
    if (cCpus > RT_ELEMENTS(s_aData))
    {
        RTPrintf("tstTSC: FAILED - too many CPUs (%u)\n", cCpus);
        return 1;
    }

    /* ourselves. */
    s_aData[0].Thread = RTThreadSelf();

    /* the others */
    for (i = 1; i < cCpus; i++)
    {
        int rc = RTThreadCreate(&s_aData[i].Thread, ThreadFunction, &s_aData[i], 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "OTHERCPU");
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstTSC: FAILURE - RTThreatCreate failed when creating thread #%u, rc=%Rrc!\n", i, rc);
            ASMAtomicXchgSize(&g_fDone, true);
            while (i-- > 1)
            {
                RTThreadUserSignal(s_aData[i].Thread);
                RTThreadWait(s_aData[i].Thread, 5000, NULL);
            }
            return 1;
        }
    }

    /*
     * Retry until we get lucky (or give up).
     */
    for (unsigned cTries = 0; ; cTries++)
    {
        if (cTries > 10240)
        {
            RTPrintf("tstTSC: FAILURE - %d attempts, giving.\n", cTries);
            break;
        }

        /*
         * Wait for the other threads to get ready (brute force active wait, I'm lazy).
         */
        i = 0;
        while (g_cWaiting < cCpus - 1)
        {
            if (i++ > _2G32)
                break;
            RTThreadSleep(i & 0xf);
        }
        if (g_cWaiting != cCpus - 1)
        {
            RTPrintf("tstTSC: FAILURE - threads failed to get waiting (%d != %d (i=%d))\n", g_cWaiting + 1, cCpus, i);
            break;
        }

        /*
         * Send them spinning.
         */
        ASMAtomicXchgU32(&g_cReady, 0);
        ASMAtomicXchgU32(&g_u32Go, 0);
        ASMAtomicXchgU32(&g_cRead, 0);
        ASMAtomicXchgU32(&g_cFailed, 0);
        for (i = 1; i < cCpus; i++)
        {
            ASMAtomicXchgSize(&s_aData[i].fFailed, false);
            ASMAtomicXchgSize(&s_aData[i].fRead, false);
            ASMAtomicXchgU8(&s_aData[i].u8ApicId, 0xff);

            int rc = RTThreadUserSignal(s_aData[i].Thread);
            if (RT_FAILURE(rc))
                RTPrintf("tstTSC: WARNING - RTThreadUserSignal(%#u) -> rc=%Rrc!\n", i, rc);
        }

        /* wait for them to get ready. */
        i = 0;
        while (g_cReady < cCpus - 1)
        {
            if (i++ > _2G32)
                break;
        }
        if (g_cReady != cCpus - 1)
        {
            RTPrintf("tstTSC: FAILURE - threads failed to get ready (%d != %d, i=%d)\n", g_cWaiting + 1, cCpus, i);
            break;
        }

        /*
         * Flip the "go" switch and do our readings.
         * We give the other threads the slack it takes to two extra TSC and APIC ID reads.
         */
        const uint8_t   ApicId1 = ASMGetApicId();
        const uint64_t  TSC1    = ASMReadTSC();
        ASMAtomicXchgU32(&g_u32Go, 1);
        const uint8_t   ApicId2 = ASMGetApicId();
        const uint64_t  TSC2    = ASMReadTSC();
        const uint8_t   ApicId3 = ASMGetApicId();
        const uint64_t  TSC3    = ASMReadTSC();
        const uint8_t   ApicId4 = ASMGetApicId();
        const uint64_t  TSC4    = ASMReadTSC();
        ASMAtomicXchgU32(&g_u32Go, 2);
        const uint8_t   ApicId5 = ASMGetApicId();
        const uint64_t  TSC5    = ASMReadTSC();
        const uint8_t   ApicId6 = ASMGetApicId();

        /* Compose our own result. */
        if (    ApicId1 == ApicId2
            &&  ApicId1 == ApicId3
            &&  ApicId1 == ApicId4
            &&  ApicId1 == ApicId5
            &&  ApicId1 == ApicId6
            &&  TSC5 - TSC1 < 2750  /* WARNING: This is just a guess, increase if it doesn't work for you. */
            &&  TSC4 - TSC1 < TSC5 - TSC1
            &&  TSC3 - TSC1 < TSC4 - TSC1
            &&  TSC2 - TSC1 < TSC3 - TSC1
            )
        {
            /* succeeded. */
            s_aData[0].TSC = TSC2;
            s_aData[0].u8ApicId = ApicId1;
            s_aData[0].fFailed = false;
            s_aData[0].fRead = true;
            ASMAtomicIncU32(&g_cRead);
        }
        else
        {
            /* failed */
            s_aData[0].fFailed = true;
            s_aData[0].fRead = false;
            ASMAtomicIncU32(&g_cFailed);
        }

        /*
         * Wait a little while to let the other ones to finish.
         */
        i = 0;
        while (g_cRead + g_cFailed < cCpus)
        {
            if (i++ > _2G32)
                break;
            if (i > _1M)
                RTThreadSleep(i & 0xf);
        }
        if (g_cRead + g_cFailed != cCpus)
        {
            RTPrintf("tstTSC: FAILURE - threads failed to complete reading (%d + %d != %d)\n", g_cRead, g_cFailed, cCpus);
            break;
        }

        /*
         * If everone succeeded, print the results.
         */
        if (!g_cFailed)
        {
            /* sort it by apic id first. */
            bool fDone;
            do
            {
                for (i = 1, fDone = true; i < cCpus; i++)
                    if (s_aData[i - 1].u8ApicId > s_aData[i].u8ApicId)
                    {
                        TSCDATA Tmp = s_aData[i - 1];
                        s_aData[i - 1] = s_aData[i];
                        s_aData[i] = Tmp;
                        fDone = false;
                    }
            } while (!fDone);

            RTPrintf(" #  ID  TSC            delta0 (decimal)\n"
                     "-----------------------------------------\n");
            RTPrintf("%2d  %02x  %RX64\n", 0, s_aData[0].u8ApicId, s_aData[0].TSC);
            for (i = 1; i < cCpus; i++)
                RTPrintf("%2d  %02x  %RX64  %s%lld\n", i, s_aData[i].u8ApicId, s_aData[i].TSC,
                         s_aData[i].TSC > s_aData[0].TSC ? "+" : "", s_aData[i].TSC - s_aData[0].TSC);
            RTPrintf("(Needed %u attempt%s.)\n", cTries + 1, cTries ? "s" : "");
            break;
        }
    }

    /*
     * Destroy the threads.
     */
    ASMAtomicXchgSize(&g_fDone, true);
    for (i = 0; i < cCpus; i++)
        if (s_aData[i].Thread != RTThreadSelf())
        {
            int rc = RTThreadUserSignal(s_aData[i].Thread);
            if (RT_FAILURE(rc))
                RTPrintf("tstTSC: WARNING - RTThreadUserSignal(%#u) -> rc=%Rrc! (2)\n", i, rc);
        }
    for (i = 0; i < cCpus; i++)
        if (s_aData[i].Thread != RTThreadSelf())
        {
            int rc = RTThreadWait(s_aData[i].Thread, 5000, NULL);
            if (RT_FAILURE(rc))
                RTPrintf("tstTSC: WARNING - RTThreadWait(%#u) -> rc=%Rrc!\n", i, rc);
        }

    return g_cFailed != 0 || g_cRead != cCpus;
}


static int tstTSCCalcFrequency(uint32_t cMsDuration)
{
    /*
     * Sample the TSC and time, sleep the requested time and calc the deltas.
     */
    uint64_t uNanoTS = RTTimeSystemNanoTS();
    uint64_t uTSC    = ASMReadTSC();
    RTThreadSleep(cMsDuration);
    uNanoTS = RTTimeSystemNanoTS() - uNanoTS;
    uTSC    = ASMReadTSC() - uTSC;

    /*
     * Calc the frequency.
     */
    RTPrintf("tstTSC: %RU64 ticks in %RU64 ns\n", uTSC, uNanoTS);
    uint64_t cHz = (uint64_t)((long double)uTSC / ((long double)uNanoTS / (long double)1000000000));
    RTPrintf("tstTSC: Frequency %RU64 Hz", cHz);
    if (cHz > _1G)
    {
        cHz += _1G / 20;
        RTPrintf("  %RU64.%RU64 GHz", cHz / _1G, (cHz % _1G) / (_1G / 10));
    }
    else if (cHz > _1M)
    {
        cHz += _1M / 20;
        RTPrintf("  %RU64.%RU64 MHz", cHz / _1M, (cHz % _1M) / (_1M / 10));
    }
    RTPrintf("\n");
    return 0;
}


int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    /*
     * Parse arguments.
     */
    bool fCalcFrequency = false;
    uint32_t cMsDuration = 1000; /* 1 sec */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--duration",         'd', RTGETOPT_REQ_UINT32 },
        { "--calc-frequency",   'f', RTGETOPT_REQ_NOTHING },
    };
    int ch;
    RTGETOPTUNION Value;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &Value)))
        switch (ch)
        {
            case 'd':   cMsDuration = Value.u32;
                break;

            case 'f':   fCalcFrequency = true;
                break;

            case 'h':
                RTPrintf("usage: tstTSC\n"
                         "   or: tstTSC <-f|--calc-frequency> [--duration|-d ms]\n");
                return 1;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return 0;

            default:
                return RTGetOptPrintError(ch, &Value);
        }

    if (fCalcFrequency)
        return tstTSCCalcFrequency(cMsDuration);
    return tstTSCCalcDrift();
}

