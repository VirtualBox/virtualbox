/* $Id: rdtsc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * rdtsc - Test if three consecutive rdtsc instructions return different values.
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
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RDTSCRESULT
{
    RTCCUINTREG uLow, uHigh;
} RDTSCRESULT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern "C" RDTSCRESULT g_aRdTscResults[]; /* rdtsc-asm.asm */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/**
 * Does 3 (32-bit) or 6 (64-bit) fast TSC reads and stores the result
 * in g_aRdTscResults, starting with the 2nd entry.
 *
 * Starting the result storing at g_aRdTscResults[1] make it easy to do the
 * comparisons in a loop.
 *
 * @returns Number of results read into g_aRdTscResults[1] and onwards.
 */
DECLASM(uint32_t) DoTscReads(void);




int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Tunables.
     */
    uint64_t        offJumpThreshold  = _4G * 2;
    unsigned        cMaxLoops         = 10000000;
    unsigned        cStatusEvery      = 2000000;
    unsigned        cMinSeconds       = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *psz = argv[i];
        if (*psz == '-')
        {
            psz++;
            char chOpt;
            while ((chOpt = *psz++) != '\0')
            {
                /* Option value. */
                const char *pszValue = NULL;
                uint64_t    uValue = 0;
                switch (chOpt)
                {
                    case 'l':
                    case 's':
                    case 'm':
                        if (*psz == '\0')
                        {
                            if (i + 1 >= argc)
                                return RTMsgSyntax("The %c option requires a value", chOpt);
                            pszValue = argv[++i];
                        }
                        else
                            pszValue = psz + (*psz == ':' || *psz == '=');
                        switch (chOpt)
                        {
                            case 'l':
                            case 's':
                            case 'm':
                            {
                                char *pszNext = NULL;
                                rc = RTStrToUInt64Ex(pszValue, &pszNext, 0, &uValue);
                                if (RT_FAILURE(rc))
                                    return RTMsgSyntax("Bad number: %s (%Rrc)", pszValue, rc);
                                if (pszNext && *pszNext != '\0')
                                {
                                    if (*pszNext == 'M'&& pszNext[1] == '\0')
                                        uValue *= _1M;
                                    else if (*pszNext == 'K' && pszNext[1] == '\0')
                                        uValue *= _1K;
                                    else if (*pszNext == 'G' && pszNext[1] == '\0')
                                        uValue *= _1G;
                                    else
                                        return RTMsgSyntax("Bad value format for option %c: %s", chOpt, pszValue);
                                }
                                break;
                            }
                        }
                        break;
                }

                /* handle the option. */
                switch (chOpt)
                {
                    case 'l':
                        cMaxLoops = uValue;
                        break;

                    case 'm':
                        cMinSeconds = uValue;
                        break;

                    case 's':
                        cStatusEvery = uValue;
                        break;

                    case 'h':
                    case '?':
                        RTPrintf("usage: rdtsc [-l <loops>] [-s <loops-between-status>]\n"
                                 "             [-m <minimum-seconds-to-run>]\n");
                        return RTEXITCODE_SUCCESS;

                    default:
                        return RTMsgSyntax("Unknown option %c (argument %d)\n", chOpt, i);
                }
            }
        }
        else
            return RTMsgSyntax("argument %d (%s): not an option\n", i, psz);
    }

    /*
     * Do the job.
     */
    uint64_t const  nsTsStart          = RTTimeNanoTS();
    unsigned        cOuterLoops        = 0;
    unsigned        cLoopsToNextStatus = cStatusEvery;
    unsigned        cRdTscInstructions = 0;
    unsigned        cBackwards         = 0;
    unsigned        cSame              = 0;
    unsigned        cBadValues         = 0;
    unsigned        cJumps             = 0;
    uint64_t        offMaxJump         = 0;
    uint64_t        offMinIncr         = UINT64_MAX;
    uint64_t        offMaxIncr         = 0;

    g_aRdTscResults[0] = g_aRdTscResults[DoTscReads() - 1];

    for (;;)
    {
        for (unsigned iLoop = 0; iLoop < cMaxLoops; iLoop++)
        {
            uint32_t const cResults = DoTscReads();
            cRdTscInstructions += cResults;

            for (uint32_t i = 0; i < cResults; i++)
            {
                uint64_t uPrev = RT_MAKE_U64((uint32_t)g_aRdTscResults[i    ].uLow, (uint32_t)g_aRdTscResults[i    ].uHigh);
                uint64_t uCur  = RT_MAKE_U64((uint32_t)g_aRdTscResults[i + 1].uLow, (uint32_t)g_aRdTscResults[i + 1].uHigh);
                if (RT_LIKELY(uCur != uPrev))
                {
                    int64_t offDelta = uCur - uPrev;
                    if (RT_LIKELY(offDelta >= 0))
                    {
                        if (RT_LIKELY((uint64_t)offDelta < offJumpThreshold))
                        {
                            if ((uint64_t)offDelta < offMinIncr)
                                offMinIncr = offDelta;
                            if ((uint64_t)offDelta > offMaxIncr && i != 0)
                                offMaxIncr = offDelta;
                        }
                        else
                        {
                            cJumps++;
                            if ((uint64_t)offDelta > offMaxJump)
                                offMaxJump = offDelta;
                            RTPrintf("%u/%u: Jump: %#010x`%08x -> %#010x`%08x\n", cOuterLoops, iLoop,
                                     (unsigned)g_aRdTscResults[i].uHigh, (unsigned)g_aRdTscResults[i].uLow,
                                     (unsigned)g_aRdTscResults[i + 1].uHigh, (unsigned)g_aRdTscResults[i + 1].uLow);
                        }
                    }
                    else
                    {
                        cBackwards++;
                        RTPrintf("%u/%u: Back: %#010x`%08x -> %#010x`%08x\n", cOuterLoops, iLoop,
                                 (unsigned)g_aRdTscResults[i].uHigh, (unsigned)g_aRdTscResults[i].uLow,
                                 (unsigned)g_aRdTscResults[i + 1].uHigh, (unsigned)g_aRdTscResults[i + 1].uLow);
                    }
                }
                else
                {
                    cSame++;
                    RTPrintf("%u/%u: Same: %#010x`%08x -> %#010x`%08x\n", cOuterLoops, iLoop,
                             (unsigned)g_aRdTscResults[i].uHigh, (unsigned)g_aRdTscResults[i].uLow,
                             (unsigned)g_aRdTscResults[i + 1].uHigh, (unsigned)g_aRdTscResults[i + 1].uLow);
                }
#if ARCH_BITS == 64
                if ((g_aRdTscResults[i + 1].uLow >> 32) || (g_aRdTscResults[i + 1].uHigh >> 32))
                    cBadValues++;
#endif
            }

            /* Copy the last value for the next iteration. */
            g_aRdTscResults[0] = g_aRdTscResults[cResults];

            /* Display status. */
            if (RT_LIKELY(--cLoopsToNextStatus > 0))
            { /* likely */ }
            else
            {
                cLoopsToNextStatus = cStatusEvery;
                RTPrintf("%u/%u: %#010x`%08x\n", cOuterLoops, iLoop,
                         (unsigned)g_aRdTscResults[cResults].uHigh, (unsigned)g_aRdTscResults[cResults].uLow);
            }
        }

        /*
         * Check minimum number of seconds.
         */
        cOuterLoops++;
        if (!cMinSeconds)
            break;
        uint64_t nsElapsed = RTTimeNanoTS() - nsTsStart;
        if (nsElapsed >= cMinSeconds * RT_NS_1SEC_64)
            break;
    }

    /*
     * Summary.
     */
    if (cBackwards == 0 && cSame == 0 && cJumps == 0 && cBadValues == 0)
    {
        RTPrintf("rdtsc: Success (%u RDTSC over %u*%u loops, deltas: %#x`%08x..%#x`%08x)\n",
                 cRdTscInstructions, cOuterLoops, cMaxLoops,
                 (unsigned)(offMinIncr >> 32), (unsigned)offMinIncr, (unsigned)(offMaxIncr >> 32), (unsigned)offMaxIncr);
        return RTEXITCODE_SUCCESS;
    }
    RTPrintf("RDTSC instructions: %u\n", cRdTscInstructions);
    RTPrintf("Loops:              %u * %u => %u\n", cMaxLoops, cOuterLoops, cOuterLoops * cMaxLoops);
    RTPrintf("Backwards:          %u\n", cBackwards);
    RTPrintf("Jumps:              %u\n", cJumps);
    RTPrintf("Max jumps:          %#010x`%08x\n", (unsigned)(offMaxJump >> 32), (unsigned)offMaxJump);
    RTPrintf("Same value:         %u\n", cSame);
    RTPrintf("Bad values:         %u\n", cBadValues);
    RTPrintf("Min increment:      %#010x`%08x\n", (unsigned)(offMinIncr >> 32), (unsigned)offMinIncr);
    RTPrintf("Max increment:      %#010x`%08x\n", (unsigned)(offMaxIncr >> 32), (unsigned)offMaxIncr);
    return RTEXITCODE_FAILURE;
}

