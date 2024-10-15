/* $Id: PDMNetShaper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <VBox/vmm/pdm.h>
#include "PDMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include <VBox/vmm/pdmnetshaper.h>




/**
 * Looks up a network bandwidth group by it's name.
 *
 * @returns Pointer to the group if found, NULL if not.
 * @param   pVM         The cross context VM structure.
 * @param   pszName     The name of the group to find.
 */
static PPDMNSBWGROUP pdmNsBwGroupFindByName(PVM pVM, const char *pszName)
{
    AssertPtrReturn(pszName, NULL);
    AssertReturn(*pszName != '\0', NULL);

    size_t const cGroups = RT_MIN(pVM->pdm.s.cNsGroups, RT_ELEMENTS(pVM->pdm.s.aNsGroups));
    for (size_t i = 0; i < cGroups; i++)
        if (RTStrCmp(pVM->pdm.s.aNsGroups[i].szName, pszName) == 0)
            return &pVM->pdm.s.aNsGroups[i];
    return NULL;
}


#ifdef VBOX_STRICT
/**
 * Checks if pFilter is attached to the given group by walking the list.
 */
DECLINLINE(bool) pdmR3NsIsFilterAttached(PPDMNSBWGROUP pGroup, PPDMNSFILTER pFilter)
{
    PPDMNSFILTER pCur;
    RTListForEach(&pGroup->FilterList, pCur, PDMNSFILTER, ListEntry)
    {
        if (pCur == pFilter)
            return true;
    }
    return false;
}
#endif

/**
 * Attaches a network filter driver to the named bandwidth group.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_INITIALIZED if already attached.
 * @retval  VERR_NOT_FOUND if the bandwidth wasn't found.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pDrvIns     The driver instance.
 * @param   pszName     Name of the bandwidth group to attach to.
 * @param   pFilter     Pointer to the filter to attach.
 */
VMMR3_INT_DECL(int) PDMR3NsAttach(PVM pVM, PPDMDRVINS pDrvIns, const char *pszName, PPDMNSFILTER pFilter)
{
    /*
     * Validate input.
     */
    RT_NOREF(pDrvIns);
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pFilter, VERR_INVALID_POINTER);

    uint32_t iGroup = pFilter->iGroup;
    AssertMsgReturn(iGroup == 0, ("iGroup=%d\n", iGroup), VERR_ALREADY_INITIALIZED);
    Assert(pFilter->ListEntry.pNext == NULL);
    Assert(pFilter->ListEntry.pPrev == NULL);

    /* Resolve the group. */
    PPDMNSBWGROUP pGroup = pdmNsBwGroupFindByName(pVM, pszName);
    AssertMsgReturn(pGroup, ("'%s'\n", pszName), VERR_NOT_FOUND);

    /*
     * The attach is protected by PDM::NsLock and by updating iGroup atomatically.
     */
    int rc = RTCritSectEnter(&pVM->pdm.s.NsLock);
    if (RT_SUCCESS(rc))
    {
        if (ASMAtomicCmpXchgU32(&pFilter->iGroup, (uint32_t)(pGroup - &pVM->pdm.s.aNsGroups[0]) + 1, 0))
        {
            Assert(pFilter->ListEntry.pNext == NULL);
            Assert(pFilter->ListEntry.pPrev == NULL);
            RTListAppend(&pGroup->FilterList, &pFilter->ListEntry);

            uint32_t cRefs = ASMAtomicIncU32(&pGroup->cRefs);
            AssertMsg(cRefs > 0 && cRefs < _16K, ("%u\n", cRefs));
            RT_NOREF_PV(cRefs);

            LogFlow(("PDMR3NsAttach: Attached '%s'/%u to %s (cRefs=%u)\n",
                     pDrvIns->pReg->szName, pDrvIns->iInstance, pGroup->szName, cRefs));
            rc = VINF_SUCCESS;
        }
        else
        {
            AssertMsgFailed(("iGroup=%d (attach race)\n", pFilter->iGroup));
            rc = VERR_ALREADY_INITIALIZED;
        }

        int rc2 = RTCritSectLeave(&pVM->pdm.s.NsLock);
        AssertRC(rc2);
    }

    return rc;
}


/**
 * Detaches a network filter driver from its current bandwidth group (if any).
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pDrvIns     The driver instance.
 * @param   pFilter     Pointer to the filter to detach.
 */
VMMR3_INT_DECL(int) PDMR3NsDetach(PVM pVM, PPDMDRVINS pDrvIns, PPDMNSFILTER pFilter)
{
    /*
     * Validate input.
     */
    RT_NOREF(pDrvIns);
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pFilter, VERR_INVALID_POINTER);

    /* Now, return quietly if the filter isn't attached since driver/device
       destructors are called on constructor failure. */
    uint32_t const iGroup = ASMAtomicUoReadU32(&pFilter->iGroup);
    if (!iGroup)
        return VINF_SUCCESS;
    AssertMsgReturn(iGroup - 1 < RT_MIN(pVM->pdm.s.cNsGroups, RT_ELEMENTS(pVM->pdm.s.aNsGroups)), ("iGroup=%#x\n", iGroup),
                    VERR_INVALID_HANDLE);
    PPDMNSBWGROUP const pGroup = &pVM->pdm.s.aNsGroups[iGroup - 1];

    /*
     * The detaching is protected by PDM::NsLock and by atomically updating iGroup.
     */
    int rc = RTCritSectEnter(&pVM->pdm.s.NsLock);
    if (RT_SUCCESS(rc))
    {
        if (ASMAtomicCmpXchgU32(&pFilter->iGroup, 0, iGroup))
        {
            Assert(pdmR3NsIsFilterAttached(pGroup, pFilter));
            RTListNodeRemove(&pFilter->ListEntry);
            Assert(pFilter->ListEntry.pNext == NULL);
            Assert(pFilter->ListEntry.pPrev == NULL);
            ASMAtomicWriteU32(&pFilter->iGroup, 0);

            uint32_t cRefs = ASMAtomicDecU32(&pGroup->cRefs);
            Assert(cRefs < _16K);
            RT_NOREF_PV(cRefs);

            LogFlow(("PDMR3NsDetach: Detached '%s'/%u from %s (cRefs=%u)\n",
                     pDrvIns->pReg->szName, pDrvIns->iInstance, pGroup->szName, cRefs));
            rc = VINF_SUCCESS;
        }
        else
            AssertFailedStmt(rc = VERR_WRONG_ORDER);

        int rc2 = RTCritSectLeave(&pVM->pdm.s.NsLock);
        AssertRC(rc2);
    }
    else
        AssertRC(rc);
    return rc;
}


/**
 * This is used both by pdmR3NsUnchokeThread and PDMR3NsBwGroupSetLimit,
 * the latter only when setting cbPerSecMax to zero.
 *
 * @param   pGroup      The group which filters should be unchoked.
 * @note    Caller owns the PDM::NsLock critsect.
 */
static void pdmR3NsUnchokeGroupFilters(PPDMNSBWGROUP pGroup)
{
    PPDMNSFILTER pFilter;
    RTListForEach(&pGroup->FilterList, pFilter, PDMNSFILTER, ListEntry)
    {
        bool fChoked = ASMAtomicXchgBool(&pFilter->fChoked, false);
        if (fChoked)
        {
            PPDMINETWORKDOWN pIDrvNet = pFilter->pIDrvNetR3;
            if (pIDrvNet && pIDrvNet->pfnXmitPending != NULL)
            {
                Log3(("pdmR3NsUnchokeGroupFilters: Unchoked %p in %s, calling %p\n",
                      pFilter, pGroup->szName, pIDrvNet->pfnXmitPending));
                pIDrvNet->pfnXmitPending(pIDrvNet);
            }
            else
                Log3(("pdmR3NsUnchokeGroupFilters: Unchoked %p in %s (no callback)\n", pFilter, pGroup->szName));
        }
    }
}


/**
 * Worker for PDMR3NsBwGroupSetLimit and pdmR3NetShaperInit.
 *
 * @returns New bucket size.
 * @param   pGroup      The group to update.
 * @param   cbPerSecMax The new max bytes per second.
 */
static uint32_t pdmNsBwGroupSetLimit(PPDMNSBWGROUP pGroup, uint64_t cbPerSecMax)
{
    uint32_t const cbRet = RT_MAX(PDM_NETSHAPER_MIN_BUCKET_SIZE, cbPerSecMax * PDM_NETSHAPER_MAX_LATENCY / RT_MS_1SEC);
    pGroup->cbBucket     = cbRet;
    pGroup->cbPerSecMax  = cbPerSecMax;
    LogFlow(("pdmNsBwGroupSetLimit: New rate limit is %#RX64 bytes per second, adjusted bucket size to %#x bytes\n",
             cbPerSecMax, cbRet));
    return cbRet;
}


/**
 * Adjusts the maximum rate for the bandwidth group.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   pszName     Name of the bandwidth group to attach to.
 * @param   cbPerSecMax Maximum number of bytes per second to be transmitted.
 */
VMMR3DECL(int) PDMR3NsBwGroupSetLimit(PUVM pUVM, const char *pszName, uint64_t cbPerSecMax)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM const     pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    int           rc;
    PPDMNSBWGROUP pGroup = pdmNsBwGroupFindByName(pVM, pszName);
    if (pGroup)
    {
        /*
         * Lock the group while we effect the changes.
         */
        rc = PDMCritSectEnter(pVM, &pGroup->Lock, VERR_IGNORED);
        if (RT_SUCCESS(rc))
        {
            uint32_t const cbBucket = pdmNsBwGroupSetLimit(pGroup, cbPerSecMax);

            /* Drop extra tokens */
            if (pGroup->cbTokensLast > cbBucket)
                pGroup->cbTokensLast = cbBucket;
            Log(("PDMR3NsBwGroupSetLimit/%s: cbBucket=%#x cbPerSecMax=%#RX64\n", pGroup->szName, cbBucket, cbPerSecMax));

            int rc2 = PDMCritSectLeave(pVM, &pGroup->Lock);
            AssertRC(rc2);

            /*
             * If we disabled the group, we must make sure to unchoke all filter
             * as the thread will ignore the group from now on.
             *
             * We do this after leaving the group lock to keep the locking simple.
             * Extra pfnXmitPending calls should be harmless, of course ASSUMING
             * nobody take offence to being called on this thread.
             */
            if (cbPerSecMax == 0)
            {
                Log(("PDMR3NsBwGroupSetLimit: cbPerSecMax was set to zero, so unchoking filters...\n"));
                rc = RTCritSectEnter(&pVM->pdm.s.NsLock);
                AssertRC(rc);

                pdmR3NsUnchokeGroupFilters(pGroup);

                rc2 = RTCritSectLeave(&pVM->pdm.s.NsLock);
                AssertRC(rc2);
            }
        }
        else
            AssertRC(rc);
    }
    else
        rc = VERR_NOT_FOUND;
    return rc;
}


/**
 * I/O thread for pending unchoking and associating transmitting.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   pVM         The cross context VM structure.
 * @param   pThread     The PDM thread data.
 */
static DECLCALLBACK(int) pdmR3NsUnchokeThread(PVM pVM, PPDMTHREAD pThread)
{
    LogFlow(("pdmR3NsUnchokeThread: pVM=%p\n", pVM));
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        int rc = RTSemEventWait(pVM->pdm.s.hNsUnchokeEvt, RT_INDEFINITE_WAIT);
        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            break;
        AssertMsgStmt(RT_SUCCESS(rc) || rc == VERR_TIMEOUT /* paranioa*/, ("%Rrc\n", rc),
                      RTThreadSleep(PDM_NETSHAPER_MAX_LATENCY));

        /*
         * Go over all bandwidth groups/filters and unchoke their filters.
         *
         * We take the main lock here to prevent any detaching or attaching
         * from taking place while we're traversing the filter lists.
         */
        rc = RTCritSectEnter(&pVM->pdm.s.NsLock);
        AssertRC(rc);

        size_t const cGroups = RT_MIN(pVM->pdm.s.cNsGroups, RT_ELEMENTS(pVM->pdm.s.aNsGroups));
        for (size_t i = 0; i < cGroups; i++)
        {
            PPDMNSBWGROUP const pGroup = &pVM->pdm.s.aNsGroups[i];
            if (   pGroup->cRefs > 0
                && pGroup->cbPerSecMax > 0)
                pdmR3NsUnchokeGroupFilters(pGroup);
        }

        rc = RTCritSectLeave(&pVM->pdm.s.NsLock);
        AssertRC(rc);
    }
    return VINF_SUCCESS;
}


/**
 * @copydoc FNPDMTHREADWAKEUPINT
 */
static DECLCALLBACK(int) pdmR3NsUnchokeWakeUp(PVM pVM, PPDMTHREAD pThread)
{
    LogFlow(("pdmR3NsUnchokeWakeUp:\n"));

    /* Wake up the thread. */
    int rc = RTSemEventSignal(pVM->pdm.s.hNsUnchokeEvt);
    AssertRC(rc);

    RT_NOREF(pThread);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNTMTIMERINT, Wakes up pdmR3NsUnchokeThread.}
 */
static DECLCALLBACK(void) pdmR3NsUnchokeTimer(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    ASMAtomicWriteBool(&pVM->pdm.s.fNsUnchokeTimerArmed, false);

    /* Wake up the thread. */
    int rc = RTSemEventSignal(pVM->pdm.s.hNsUnchokeEvt);
    AssertRC(rc);

    RT_NOREF(hTimer, pvUser);
}


/**
 * Terminate the network shaper, groups, lock and everything.
 *
 * @param   pVM  The cross context VM structure.
 */
void pdmR3NetShaperTerm(PVM pVM)
{
    size_t const cGroups = RT_MIN(pVM->pdm.s.cNsGroups, RT_ELEMENTS(pVM->pdm.s.aNsGroups));
    for (size_t i = 0; i < cGroups; i++)
    {
        PPDMNSBWGROUP const pGroup = &pVM->pdm.s.aNsGroups[i];
        AssertMsg(pGroup->cRefs == 0, ("cRefs=%s '%s'\n", pGroup->cRefs, pGroup->szName));
        AssertContinue(PDMCritSectIsInitialized(&pGroup->Lock));
        PDMR3CritSectDelete(pVM, &pGroup->Lock);
    }

    RTCritSectDelete(&pVM->pdm.s.NsLock);
}


/**
 * Initialize the network shaper.
 *
 * @returns VBox status code
 * @param   pVM The cross context VM structure.
 */
int pdmR3NetShaperInit(PVM pVM)
{
    LogFlow(("pdmR3NetShaperInit: pVM=%p\n", pVM));
    VM_ASSERT_EMT(pVM);

    Assert(pVM->pdm.s.cNsGroups == 0);
    pVM->pdm.s.hNsUnchokeEvt   = NIL_RTSEMEVENT;
    pVM->pdm.s.hNsUnchokeTimer = NIL_TMTIMERHANDLE;

    /*
     * Initialize the critical section protecting attaching, detaching and unchoking.
     *
     * This is a non-recursive lock to make sure nobody tries to mess with the groups
     * from the pfnXmitPending callback.
     */
    int rc = RTCritSectInitEx(&pVM->pdm.s.NsLock, RTCRITSECT_FLAGS_NO_NESTING,
                              NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "PDMNetShaper");
    AssertRCReturn(rc, rc);

    /*
     * Initialize all bandwidth groups.
     */
    PCFGMNODE pCfgNetShaper = CFGMR3GetChild(CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM"), "NetworkShaper");
    PCFGMNODE pCfgBwGrp     = CFGMR3GetChild(pCfgNetShaper, "BwGroups");
    if (pCfgBwGrp)
    {
        uint32_t iGroup = 0;
        for (PCFGMNODE pCur = CFGMR3GetFirstChild(pCfgBwGrp); pCur; pCur = CFGMR3GetNextChild(pCur))
        {
            /*
             * Get the config data.
             */
            size_t cchName = CFGMR3GetNameLen(pCur);
            AssertBreakStmt(cchName <= PDM_NET_SHAPER_MAX_NAME_LEN,
                            rc = VMR3SetError(pVM->pUVM, VERR_INVALID_NAME, RT_SRC_POS,
                                              N_("Network shaper group name #%u is too long: %zu, max %u"),
                                              iGroup, cchName, PDM_NET_SHAPER_MAX_NAME_LEN));
            char   szName[PDM_NET_SHAPER_MAX_NAME_LEN + 1];
            rc = CFGMR3GetName(pCur, szName, sizeof(szName));
            AssertRCBreak(rc);
            AssertBreakStmt(szName[0] != '\0',
                            rc = VMR3SetError(pVM->pUVM, VERR_INVALID_NAME, RT_SRC_POS,
                                              N_("Empty network shaper group name #%u"), iGroup));

            uint64_t cbMax;
            rc = CFGMR3QueryU64(pCur, "Max", &cbMax);
            AssertRCBreakStmt(rc, rc = VMR3SetError(pVM->pUVM, rc, RT_SRC_POS,
                                                    N_("Failed to read 'Max' value for network shaper group '%s': %Rrc"),
                                                    szName, rc));

            /*
             * Initialize the group table entry.
             */
            AssertBreakStmt(iGroup < RT_ELEMENTS(pVM->pdm.s.aNsGroups),
                            rc = VMR3SetError(pVM->pUVM, VERR_TOO_MUCH_DATA, RT_SRC_POS, N_("Too many bandwidth groups (max %zu)"),
                                              RT_ELEMENTS(pVM->pdm.s.aNsGroups)));

            rc = PDMR3CritSectInit(pVM, &pVM->pdm.s.aNsGroups[iGroup].Lock, RT_SRC_POS, "BWGRP%02u-%s", iGroup, szName);
            AssertRCBreak(rc);

            RTListInit(&pVM->pdm.s.aNsGroups[iGroup].FilterList);
            pVM->pdm.s.aNsGroups[iGroup].cRefs          = 0;
            RTStrCopy(pVM->pdm.s.aNsGroups[iGroup].szName, sizeof(pVM->pdm.s.aNsGroups[iGroup].szName), szName);
            pVM->pdm.s.aNsGroups[iGroup].cbTokensLast   = pdmNsBwGroupSetLimit(&pVM->pdm.s.aNsGroups[iGroup], cbMax);
            pVM->pdm.s.aNsGroups[iGroup].tsUpdatedLast  = RTTimeSystemNanoTS();
            LogFlowFunc(("PDM NetShaper Group #%u: %s - cbPerSecMax=%#RU64 cbBucket=%#x\n",
                         iGroup, pVM->pdm.s.aNsGroups[iGroup].szName, pVM->pdm.s.aNsGroups[iGroup].cbPerSecMax,
                         pVM->pdm.s.aNsGroups[iGroup].cbBucket));

            /*
             * Register statistics.
             */
            STAMR3RegisterF(pVM, (void *)&pVM->pdm.s.aNsGroups[iGroup].cbPerSecMax,     STAMTYPE_U64, STAMVISIBILITY_ALWAYS,
                            STAMUNIT_BYTES, "", "/PDM/NetShaper/%u-%s/cbPerSecMax", iGroup, szName);
            STAMR3RegisterF(pVM, (void *)&pVM->pdm.s.aNsGroups[iGroup].cRefs,           STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                            STAMUNIT_BYTES, "", "/PDM/NetShaper/%u-%s/cRefs", iGroup, szName);
            STAMR3RegisterF(pVM, (void *)&pVM->pdm.s.aNsGroups[iGroup].cbBucket,        STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                            STAMUNIT_BYTES, "", "/PDM/NetShaper/%u-%s/cbBucket", iGroup, szName);
            STAMR3RegisterF(pVM, (void *)&pVM->pdm.s.aNsGroups[iGroup].cbTokensLast,    STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                            STAMUNIT_BYTES, "", "/PDM/NetShaper/%u-%s/cbTokensLast", iGroup, szName);
            STAMR3RegisterF(pVM, (void *)&pVM->pdm.s.aNsGroups[iGroup].tsUpdatedLast,   STAMTYPE_U64, STAMVISIBILITY_ALWAYS,
                            STAMUNIT_NS, "", "/PDM/NetShaper/%u-%s/tsUpdatedLast", iGroup, szName);
            STAMR3RegisterF(pVM, (void *)&pVM->pdm.s.aNsGroups[iGroup].cTotalChokings,  STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS,
                            STAMUNIT_OCCURENCES, "", "/PDM/NetShaper/%u-%s/TotalChokings", iGroup, szName);

            pVM->pdm.s.cNsGroups = ++iGroup;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * If there are any groups configured, create a unchoke thread and an
         * associated timer for waking it up when needed.   The timer runs on
         * the real time clock.
         */
        if (pVM->pdm.s.cNsGroups == 0)
        {
            LogFlowFunc(("returns VINF_SUCCESS - no groups\n"));
            return VINF_SUCCESS;
        }

        rc = RTSemEventCreate(&pVM->pdm.s.hNsUnchokeEvt);
        if (RT_SUCCESS(rc))
        {
            rc = TMR3TimerCreate(pVM, TMCLOCK_REAL, pdmR3NsUnchokeTimer, NULL, TMTIMER_FLAGS_NO_RING0,
                                 "PDMNetShaperUnchoke", &pVM->pdm.s.hNsUnchokeTimer);
            if (RT_SUCCESS(rc))
            {
                rc = PDMR3ThreadCreate(pVM, &pVM->pdm.s.pNsUnchokeThread, NULL, pdmR3NsUnchokeThread, pdmR3NsUnchokeWakeUp,
                                       0 /*cbStack*/, RTTHREADTYPE_IO, "PDMNsUnchoke");
                if (RT_SUCCESS(rc))
                {

                    LogFlowFunc(("returns VINF_SUCCESS (%u groups)\n", pVM->pdm.s.cNsGroups));
                    return VINF_SUCCESS;
                }
            }
        }
    }

    RTCritSectDelete(&pVM->pdm.s.NsLock);
    LogRel(("pdmR3NetShaperInit: failed rc=%Rrc\n", rc));
    return rc;
}

