/* $Id: tstGIP-2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SUP Testcase - Global Info Page interface (ring 3).
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
#include <VBox/sup.h>
#include <iprt/errcore.h>
#include <VBox/param.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/thread.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/x86.h>


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    /*
     * Parse args
     */
    static const RTGETOPTDEF g_aOptions[] =
    {
        { "--iterations",       'i', RTGETOPT_REQ_INT32 },
        { "--hex",              'h', RTGETOPT_REQ_NOTHING },
        { "--decimal",          'd', RTGETOPT_REQ_NOTHING },
        { "--spin",             's', RTGETOPT_REQ_NOTHING },
        { "--reference",        'r', RTGETOPT_REQ_UINT64 },  /* reference value of CpuHz, display the
                                                              * CpuHz deviation in a separate column. */
        { "--notestmode",       't', RTGETOPT_REQ_NOTHING }  /* don't run GIP in test-mode (atm, test-mode
                                                              * implies updating GIP CpuHz even when invariant) */
    };

    bool          fHex = true;
    bool          fSpin = false;
    bool          fCompat = true;
    bool          fTestMode = true;
    int           ch;
    uint32_t      cIterations = 40;
    uint64_t      uCpuHzRef = UINT64_MAX;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aOptions, RT_ELEMENTS(g_aOptions), 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'i':
                cIterations = ValueUnion.u32;
                break;

            case 'd':
                fHex = false;
                break;

            case 'h':
                fHex = true;
                break;

            case 's':
                fSpin = true;
                break;

            case 'r':
                uCpuHzRef = ValueUnion.u64;
                break;

            case 't':
                fTestMode = false;
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    /*
     * Init
     */
    PSUPDRVSESSION pSession = NIL_RTR0PTR;
    int rc = SUPR3Init(&pSession);
    if (RT_SUCCESS(rc))
    {
        if (g_pSUPGlobalInfoPage)
        {
            uint64_t uCpuHzOverallDeviation = 0;
            uint32_t cCpuHzNotCompat = 0;
            int64_t  iCpuHzMaxDeviation = 0;
            int32_t  cCpuHzOverallDevCnt = 0;
            uint32_t cCpuHzChecked = 0;

            /* Pick current CpuHz as the reference if none was specified. */
            if (uCpuHzRef == UINT64_MAX)
                uCpuHzRef = SUPGetCpuHzFromGip(g_pSUPGlobalInfoPage);

            if (   fTestMode
                && g_pSUPGlobalInfoPage->u32Mode == SUPGIPMODE_INVARIANT_TSC)
                SUPR3GipSetFlags(SUPGIP_FLAGS_TESTING_ENABLE, UINT32_MAX);

            RTPrintf("tstGIP-2: u32Mode=%d (%s)  fTestMode=%RTbool  u32Version=%#x  fGetGipCpu=%#RX32  cPages=%#RX32\n",
                     g_pSUPGlobalInfoPage->u32Mode,
                     SUPGetGIPModeName(g_pSUPGlobalInfoPage),
                     fTestMode,
                     g_pSUPGlobalInfoPage->u32Version,
                     g_pSUPGlobalInfoPage->fGetGipCpu,
                     g_pSUPGlobalInfoPage->cPages);
            RTPrintf("tstGIP-2: cCpus=%d  cPossibleCpus=%d cPossibleCpuGroups=%d cPresentCpus=%d cOnlineCpus=%d idCpuMax=%#x\n",
                     g_pSUPGlobalInfoPage->cCpus,
                     g_pSUPGlobalInfoPage->cPossibleCpus,
                     g_pSUPGlobalInfoPage->cPossibleCpuGroups,
                     g_pSUPGlobalInfoPage->cPresentCpus,
                     g_pSUPGlobalInfoPage->cOnlineCpus,
                     g_pSUPGlobalInfoPage->idCpuMax);
            RTPrintf("tstGIP-2: u32UpdateHz=%RU32  u32UpdateIntervalNS=%RU32  u64NanoTSLastUpdateHz=%RX64  u64CpuHz=%RU64  uCpuHzRef=%RU64\n",
                     g_pSUPGlobalInfoPage->u32UpdateHz,
                     g_pSUPGlobalInfoPage->u32UpdateIntervalNS,
                     g_pSUPGlobalInfoPage->u64NanoTSLastUpdateHz,
                     g_pSUPGlobalInfoPage->u64CpuHz,
                     uCpuHzRef);
            for (uint32_t iCpu = 0; iCpu < g_pSUPGlobalInfoPage->cCpus; iCpu++)
                if (g_pSUPGlobalInfoPage->aCPUs[iCpu].enmState != SUPGIPCPUSTATE_INVALID)
                {
                    SUPGIPCPU const *pGipCpu = &g_pSUPGlobalInfoPage->aCPUs[iCpu];
                    RTPrintf("tstGIP-2: aCPU[%3u]: enmState=%d iCpuSet=%-3u idCpu=%#010x iCpuGroup=%-2u iCpuGroupMember=%-3u idApic=%#06x\n",
                             iCpu, pGipCpu->enmState, pGipCpu->iCpuSet, pGipCpu->idCpu, pGipCpu->iCpuGroup,
                             pGipCpu->iCpuGroupMember, pGipCpu->idApic);
                }

            RTPrintf(fHex
                     ? "tstGIP-2:     it: u64NanoTS        delta     u64TSC           UpIntTSC H  TransId      CpuHz      %sTSC Interval History...\n"
                     : "tstGIP-2:     it: u64NanoTS        delta     u64TSC             UpIntTSC H    TransId      CpuHz      %sTSC Interval History...\n",
                     uCpuHzRef ? "  CpuHz deviation  Compat  " : "");
            static SUPGIPCPU s_aaCPUs[2][RTCPUSET_MAX_CPUS];
            for (uint32_t i = 0; i < cIterations; i++)
            {
                /* Copy the data. */
                memcpy(&s_aaCPUs[i & 1][0], &g_pSUPGlobalInfoPage->aCPUs[0], g_pSUPGlobalInfoPage->cCpus * sizeof(g_pSUPGlobalInfoPage->aCPUs[0]));

                /* Display it & find something to spin on. */
                uint32_t u32TransactionId = 0;
                uint32_t volatile *pu32TransactionId = NULL;
                for (unsigned iCpu = 0; iCpu < g_pSUPGlobalInfoPage->cCpus; iCpu++)
                    if (g_pSUPGlobalInfoPage->aCPUs[iCpu].enmState == SUPGIPCPUSTATE_ONLINE)
                    {
                        char szCpuHzDeviation[32];
                        PSUPGIPCPU pPrevCpu = &s_aaCPUs[!(i & 1)][iCpu];
                        PSUPGIPCPU pCpu = &s_aaCPUs[i & 1][iCpu];
                        if (uCpuHzRef)
                        {
                            /* Only CPU 0 is updated for invariant & sync modes, see supdrvGipUpdate(). */
                            if (   iCpu == 0
                                || g_pSUPGlobalInfoPage->u32Mode == SUPGIPMODE_ASYNC_TSC)
                            {
                                /* Wait until the history validation code takes effect. */
                                if (pCpu->u32TransactionId > 23 + (8 * 2) + 1)
                                {
                                    int64_t  iCpuHzDeviation = pCpu->u64CpuHz - uCpuHzRef;
                                    uint64_t uCpuHzDeviation = RT_ABS(iCpuHzDeviation);
                                    bool     fCurHzCompat = SUPIsTscFreqCompatibleEx(uCpuHzRef, pCpu->u64CpuHz, false /*fRelax*/);
                                    if (uCpuHzDeviation <= 999999999)
                                    {
                                        if (RT_ABS(iCpuHzDeviation) > RT_ABS(iCpuHzMaxDeviation))
                                            iCpuHzMaxDeviation = iCpuHzDeviation;
                                        uCpuHzOverallDeviation += uCpuHzDeviation;
                                        cCpuHzOverallDevCnt++;
                                        uint32_t uPct = (uint32_t)(uCpuHzDeviation * 100000 / uCpuHzRef + 5);
                                        RTStrPrintf(szCpuHzDeviation, sizeof(szCpuHzDeviation), "%10RI64%3d.%02d%%  %RTbool   ",
                                                    iCpuHzDeviation, uPct / 1000, (uPct % 1000) / 10, fCurHzCompat);
                                    }
                                    else
                                    {
                                        RTStrPrintf(szCpuHzDeviation, sizeof(szCpuHzDeviation), "%17s  %RTbool   ", "?",
                                                    fCurHzCompat);
                                    }

                                    if (!fCurHzCompat)
                                        ++cCpuHzNotCompat;
                                    fCompat &= fCurHzCompat;
                                    ++cCpuHzChecked;
                                }
                                else
                                    RTStrPrintf(szCpuHzDeviation, sizeof(szCpuHzDeviation), "%25s  ", "priming");
                            }
                            else
                                RTStrPrintf(szCpuHzDeviation, sizeof(szCpuHzDeviation), "%25s  ", "");
                        }
                        else
                            szCpuHzDeviation[0] = '\0';
                        RTPrintf(fHex
                                 ? "tstGIP-2: %4d/%d: %016llx %09llx %016llx %08x %d %08x %15llu %s%08x %08x %08x %08x %08x %08x %08x %08x (%d)\n"
                                 : "tstGIP-2: %4d/%d: %016llu %09llu %016llu %010u %d %010u %15llu %s%08x %08x %08x %08x %08x %08x %08x %08x (%d)\n",
                                 i, iCpu,
                                 pCpu->u64NanoTS,
                                 i ? pCpu->u64NanoTS - pPrevCpu->u64NanoTS : 0,
                                 pCpu->u64TSC,
                                 pCpu->u32UpdateIntervalTSC,
                                 pCpu->iTSCHistoryHead,
                                 pCpu->u32TransactionId,
                                 pCpu->u64CpuHz,
                                 szCpuHzDeviation,
                                 pCpu->au32TSCHistory[0],
                                 pCpu->au32TSCHistory[1],
                                 pCpu->au32TSCHistory[2],
                                 pCpu->au32TSCHistory[3],
                                 pCpu->au32TSCHistory[4],
                                 pCpu->au32TSCHistory[5],
                                 pCpu->au32TSCHistory[6],
                                 pCpu->au32TSCHistory[7],
                                 pCpu->cErrors);
                        if (!pu32TransactionId)
                        {
                            pu32TransactionId = &g_pSUPGlobalInfoPage->aCPUs[iCpu].u32TransactionId;
                            u32TransactionId = pCpu->u32TransactionId;
                        }
                    }

                /* Wait a bit / spin. */
                if (!fSpin)
                    RTThreadSleep(9);
                else
                {
                    if (pu32TransactionId)
                    {
                        uint32_t uTmp;
                        while (   u32TransactionId == (uTmp = *pu32TransactionId)
                               || (uTmp & 1))
                            ASMNopPause();
                    }
                    else
                        RTThreadSleep(1);
                }
            }

            /*
             * Display TSC deltas.
             *
             * First iterative over the APIC ID array to get mostly consistent CPUID to APIC ID mapping.
             * Then iterate over the offline CPUs. It is possible that there's a race between the online/offline
             * states between the two iterations, but that cannot be helped from ring-3 anyway and not a biggie.
             */
            RTPrintf("tstGIP-2: TSC deltas:\n");
            RTPrintf("tstGIP-2:  idApic: i64TSCDelta\n");
            for (uint32_t i = 0; i < RT_ELEMENTS(g_pSUPGlobalInfoPage->aiCpuFromApicId); i++)
            {
                uint16_t iCpu = g_pSUPGlobalInfoPage->aiCpuFromApicId[i];
                if (iCpu != UINT16_MAX)
                    RTPrintf("tstGIP-2: %#7x: %6lld (grp=%#04x mbr=%#05x set=%d cpu=%#05x)\n",
                             g_pSUPGlobalInfoPage->aCPUs[iCpu].idApic, g_pSUPGlobalInfoPage->aCPUs[iCpu].i64TSCDelta,
                             g_pSUPGlobalInfoPage->aCPUs[iCpu].iCpuGroup, g_pSUPGlobalInfoPage->aCPUs[iCpu].iCpuGroupMember,
                             g_pSUPGlobalInfoPage->aCPUs[iCpu].iCpuSet, iCpu);
            }

            for (uint32_t iCpu = 0; iCpu < g_pSUPGlobalInfoPage->cCpus; iCpu++)
                if (g_pSUPGlobalInfoPage->aCPUs[iCpu].idApic == UINT16_MAX)
                    RTPrintf("tstGIP-2: offline: %6lld (grp=%#04x mbr=%#05x set=%d cpu=%#05x)\n",
                             g_pSUPGlobalInfoPage->aCPUs[iCpu].i64TSCDelta, g_pSUPGlobalInfoPage->aCPUs[iCpu].iCpuGroup,
                             g_pSUPGlobalInfoPage->aCPUs[iCpu].iCpuGroupMember, g_pSUPGlobalInfoPage->aCPUs[iCpu].iCpuSet, iCpu);

            RTPrintf("tstGIP-2: enmUseTscDelta=%d  fGetGipCpu=%#x\n",
                     g_pSUPGlobalInfoPage->enmUseTscDelta, g_pSUPGlobalInfoPage->fGetGipCpu);
            if (uCpuHzRef)
            {
                if (cCpuHzOverallDevCnt)
                {
                    uint32_t uPct    = (uint32_t)(uCpuHzOverallDeviation * 100000 / cCpuHzOverallDevCnt / uCpuHzRef + 5);
                    RTPrintf("tstGIP-2: Average CpuHz deviation: %d.%02d%%\n",
                             uPct / 1000, (uPct % 1000) / 10);

                    uint32_t uMaxPct = (uint32_t)(RT_ABS(iCpuHzMaxDeviation) * 100000 / uCpuHzRef + 5);
                    RTPrintf("tstGIP-2: Maximum CpuHz deviation: %d.%02d%% (%RI64 ticks)\n",
                             uMaxPct / 1000, (uMaxPct % 1000) / 10, iCpuHzMaxDeviation);
                }
                else
                {
                    RTPrintf("tstGIP-2: Average CpuHz deviation: ??.??\n");
                    RTPrintf("tstGIP-2: Average CpuHz deviation: ??.??\n");
                }

                RTPrintf("tstGIP-2: CpuHz compatibility: %RTbool (incompatible %u of %u times w/ %RU64 Hz - %s GIP)\n", fCompat,
                         cCpuHzNotCompat, cCpuHzChecked, uCpuHzRef, SUPGetGIPModeName(g_pSUPGlobalInfoPage));

                if (   !fCompat
                    && g_pSUPGlobalInfoPage->u32Mode == SUPGIPMODE_INVARIANT_TSC)
                    rc = -1;
            }

            /* Disable GIP test mode. */
            if (fTestMode)
                SUPR3GipSetFlags(0, ~SUPGIP_FLAGS_TESTING_ENABLE);
        }
        else
        {
            RTPrintf("tstGIP-2: g_pSUPGlobalInfoPage is NULL\n");
            rc = -1;
        }

        SUPR3Term(false /*fForced*/);
    }
    else
        RTPrintf("tstGIP-2: SUPR3Init failed: %Rrc\n", rc);
    return !!rc;
}

#if !defined(VBOX_WITH_HARDENING) || !defined(RT_OS_WINDOWS)
/**
 * Main entry point.
 */
int main(int argc, char **argv)
{
    return TrustedMain(argc, argv);
}
#endif

