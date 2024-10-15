/* $Id: PDMAllNetShaper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Network Shaper - Limit network traffic according to bandwidth group settings.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_NET_SHAPER
#include <VBox/vmm/pdmnetshaper.h>
#include "PDMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/log.h>
#include <iprt/time.h>
#include <iprt/asm-math.h>


/**
 * Obtain bandwidth in a bandwidth group.
 *
 * @returns True if bandwidth was allocated, false if not.
 * @param   pVM             The cross context VM structure.
 * @param   pFilter         Pointer to the filter that allocates bandwidth.
 * @param   cbTransfer      Number of bytes to allocate.
 */
VMM_INT_DECL(bool) PDMNetShaperAllocateBandwidth(PVMCC pVM, PPDMNSFILTER pFilter, size_t cbTransfer)
{
    AssertPtrReturn(pFilter, true);

    /*
     * If we haven't got a valid bandwidth group, we always allow the traffic.
     */
    bool fAllowed = true;
    uint32_t iGroup = ASMAtomicUoReadU32(&pFilter->iGroup);
    if (iGroup != 0)
    {
        if (iGroup <= RT_MIN(pVM->pdm.s.cNsGroups, RT_ELEMENTS(pVM->pdm.s.aNsGroups)))
        {
            PPDMNSBWGROUP pGroup = &pVM->pdm.s.aNsGroups[iGroup - 1];
            int rc = PDMCritSectEnter(pVM, &pGroup->Lock, VINF_TRY_AGAIN);
            if (rc == VINF_SUCCESS)
            {
                uint64_t const cbPerSecMax = pGroup->cbPerSecMax;
                if (cbPerSecMax > 0)
                {
                    /*
                     * Re-fill the bucket first
                     *
                     * Note! We limit the cTokensAdded calculation to 1 second, since it's really
                     *       pointless to calculate much beyond PDM_NETSHAPER_MAX_LATENCY (100ms)
                     *       let alone 1 sec.  This makes it possible to use ASMMultU64ByU32DivByU32
                     *       as the cNsDelta is less than 30 bits wide now, which means we don't get
                     *       into overflow issues when multiplying two 64-bit values.
                     */
                    uint64_t const nsNow        = RTTimeSystemNanoTS();
                    uint64_t const cNsDelta     = nsNow - pGroup->tsUpdatedLast;
                    uint64_t const cTokensAdded = cNsDelta < RT_NS_1SEC
                                                ? ASMMultU64ByU32DivByU32(cbPerSecMax, (uint32_t)cNsDelta, RT_NS_1SEC)
                                                : cbPerSecMax;
                    uint32_t const cbBucket     = pGroup->cbBucket;
                    uint32_t const cbTokensLast = pGroup->cbTokensLast;
                    uint32_t const cTokens      = (uint32_t)RT_MIN(cbBucket, cTokensAdded + cbTokensLast);

                    /*
                     * Allowed?
                     */
                    if (cbTransfer <= cTokens)
                    {
                        pGroup->cbTokensLast  = cTokens - (uint32_t)cbTransfer;
                        pGroup->tsUpdatedLast = nsNow;
                        Log2(("pdmNsAllocateBandwidth/%s: allowed - cbTransfer=%#zx cTokens=%#x cTokensAdded=%#x\n",
                              pGroup->szName, cbTransfer, cTokens, cTokensAdded));
                    }
                    else
                    {
                        /*
                         * No, we're choked.  Arm the unchoke timer for the next period.
                         * Just do this on a simple PDM_NETSHAPER_MAX_LATENCY clock granularity.
                         * ASSUMES the timer uses millisecond resolution clock.
                         */
                        ASMAtomicWriteBool(&pFilter->fChoked, true);
                        if (ASMAtomicCmpXchgBool(&pVM->pdm.s.fNsUnchokeTimerArmed, true, false))
                        {
                            Assert(TMTimerGetFreq(pVM, pVM->pdm.s.hNsUnchokeTimer) == RT_MS_1SEC);
                            uint64_t const msNow    = TMTimerGet(pVM, pVM->pdm.s.hNsUnchokeTimer);
                            uint64_t const msExpire = (msNow / PDM_NETSHAPER_MAX_LATENCY + 1) * PDM_NETSHAPER_MAX_LATENCY;
                            rc = TMTimerSet(pVM, pVM->pdm.s.hNsUnchokeTimer, msExpire);
                            AssertRC(rc);

                            Log2(("pdmNsAllocateBandwidth/%s: refused - cbTransfer=%#zx cTokens=%#x cTokensAdded=%#x cMsExpire=%u\n",
                                  pGroup->szName, cbTransfer, cTokens, cTokensAdded, msExpire - msNow));
                        }
                        else
                            Log2(("pdmNsAllocateBandwidth/%s: refused - cbTransfer=%#zx cTokens=%#x cTokensAdded=%#x\n",
                                  pGroup->szName, cbTransfer, cTokens, cTokensAdded));
                        ASMAtomicIncU64(&pGroup->cTotalChokings);
                        fAllowed = false;
                    }
                }
                else
                    Log2(("pdmNsAllocateBandwidth/%s: disabled\n", pGroup->szName));

                rc = PDMCritSectLeave(pVM, &pGroup->Lock);
                AssertRCSuccess(rc);
            }
            else if (rc == VINF_TRY_AGAIN) /* (accounted for by the critsect stats) */
                Log2(("pdmNsAllocateBandwidth/%s: allowed - lock contention\n", pGroup->szName));
            else
                PDM_CRITSECT_RELEASE_ASSERT_RC(pVM, &pGroup->Lock, rc);
        }
        else
            AssertMsgFailed(("Invalid iGroup=%d\n", iGroup));
    }
    return fAllowed;
}
