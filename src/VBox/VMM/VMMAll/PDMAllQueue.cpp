/* $Id: PDMAllQueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Queue - Transport data and tasks to EMT and R3.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_QUEUE
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#ifndef IN_RC
# include <VBox/vmm/mm.h>
#endif
#include <VBox/vmm/vmcc.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * Macros for thoroughly validating a queue handle and ownership.
 */
#define PDMQUEUE_HANDLE_TO_VARS_RETURN_COMMON(a_cbMax, a_cbTotalMax) \
    AssertReturn(cbItem >= sizeof(PDMQUEUEITEMCORE), pQueue->rcOkay = VERR_INTERNAL_ERROR_4); \
    AssertReturn(cbItem <= (a_cbMax), pQueue->rcOkay = VERR_INTERNAL_ERROR_4); \
    \
    /* paranoia^3: */ \
    AssertReturn(cItems > 0, pQueue->rcOkay = VERR_INTERNAL_ERROR_4); \
    AssertReturn(cItems <= PDMQUEUE_MAX_ITEMS, pQueue->rcOkay = VERR_INTERNAL_ERROR_4); \
    AssertReturn(cbItem * cItems <= (a_cbTotalMax), pQueue->rcOkay = VERR_INTERNAL_ERROR_4)

#ifdef IN_RING0
# define PDMQUEUE_HANDLE_TO_VARS_RETURN(a_pVM, a_hQueue, a_pvOwner) \
    AssertPtrReturn((a_pvOwner), VERR_INVALID_PARAMETER); \
    \
    AssertCompile(RT_ELEMENTS((a_pVM)->pdm.s.apRing0Queues) == RT_ELEMENTS((a_pVM)->pdmr0.s.aQueues)); \
    AssertReturn((a_hQueue) < RT_ELEMENTS((a_pVM)->pdmr0.s.aQueues), VERR_INVALID_HANDLE); \
    AssertReturn((a_hQueue) < (a_pVM)->pdmr0.s.cQueues, VERR_INVALID_HANDLE); \
    AssertReturn((a_pVM)->pdmr0.s.aQueues[(a_hQueue)].pvOwner == (a_pvOwner), VERR_INVALID_HANDLE); \
    PPDMQUEUE pQueue = (a_pVM)->pdmr0.s.aQueues[(a_hQueue)].pQueue; \
    AssertPtrReturn(pQueue, VERR_INVALID_HANDLE); \
    AssertReturn(pQueue->u32Magic == PDMQUEUE_MAGIC, VERR_INVALID_HANDLE); \
    AssertReturn(pQueue->rcOkay == VINF_SUCCESS, pQueue->rcOkay); \
    \
    uint32_t const cbItem   = (a_pVM)->pdmr0.s.aQueues[(a_hQueue)].cbItem; \
    uint32_t const cItems   = (a_pVM)->pdmr0.s.aQueues[(a_hQueue)].cItems; \
    uint32_t const offItems = (a_pVM)->pdmr0.s.aQueues[(a_hQueue)].offItems; \
    \
    /* paranoia^2: */ \
    AssertReturn(pQueue->cbItem == cbItem, pQueue->rcOkay = VERR_INTERNAL_ERROR_3); \
    AssertReturn(pQueue->cItems == cItems, pQueue->rcOkay = VERR_INTERNAL_ERROR_3); \
    AssertReturn(pQueue->offItems == offItems, pQueue->rcOkay = VERR_INTERNAL_ERROR_3); \
    \
    PDMQUEUE_HANDLE_TO_VARS_RETURN_COMMON(PDMQUEUE_MAX_ITEM_SIZE, PDMQUEUE_MAX_TOTAL_SIZE_R0)

#else
# define PDMQUEUE_HANDLE_TO_VARS_RETURN(a_pVM, a_hQueue, a_pvOwner) \
    AssertPtrReturn((a_pvOwner), VERR_INVALID_PARAMETER); \
    \
    PPDMQUEUE pQueue; \
    if ((a_hQueue) < RT_ELEMENTS((a_pVM)->pdm.s.apRing0Queues)) \
        pQueue = (a_pVM)->pdm.s.apRing0Queues[(a_hQueue)]; \
    else \
    { \
        (a_hQueue) -= RT_ELEMENTS((a_pVM)->pdm.s.apRing0Queues); \
        AssertReturn((a_pVM)->pdm.s.cRing3Queues, VERR_INVALID_HANDLE); \
        pQueue = (a_pVM)->pdm.s.papRing3Queues[(a_hQueue)]; \
    } \
    AssertPtrReturn(pQueue, VERR_INVALID_HANDLE); \
    AssertReturn(pQueue->u32Magic == PDMQUEUE_MAGIC, VERR_INVALID_HANDLE); \
    AssertReturn(pQueue->u.Gen.pvOwner == (a_pvOwner), VERR_INVALID_HANDLE); \
    AssertReturn(pQueue->rcOkay == VINF_SUCCESS, pQueue->rcOkay); \
    \
    uint32_t const cbItem   = pQueue->cbItem; \
    uint32_t const cItems   = pQueue->cItems; \
    uint32_t const offItems = pQueue->offItems; \
    \
    PDMQUEUE_HANDLE_TO_VARS_RETURN_COMMON(PDMQUEUE_MAX_ITEM_SIZE, PDMQUEUE_MAX_TOTAL_SIZE_R3)

#endif


/**
 * Commmon function for initializing the shared queue structure.
 */
void pdmQueueInit(PPDMQUEUE pQueue, uint32_t cbBitmap, uint32_t cbItem, uint32_t cItems,
                  const char *pszName, PDMQUEUETYPE enmType, RTR3PTR pfnCallback, RTR3PTR pvOwner)
{
    Assert(cbBitmap * 8 >= cItems);

    pQueue->u32Magic            = PDMQUEUE_MAGIC;
    pQueue->cbItem              = cbItem;
    pQueue->cItems              = cItems;
    pQueue->offItems            = RT_UOFFSETOF(PDMQUEUE, bmAlloc) + cbBitmap;
    pQueue->rcOkay              = VINF_SUCCESS;
    pQueue->u32Padding          = 0;
    pQueue->hTimer              = NIL_TMTIMERHANDLE;
    pQueue->cMilliesInterval    = 0;
    pQueue->enmType             = enmType;
    pQueue->u.Gen.pfnCallback   = pfnCallback;
    pQueue->u.Gen.pvOwner       = pvOwner;
    RTStrCopy(pQueue->szName, sizeof(pQueue->szName), pszName);
    pQueue->iPending            = UINT32_MAX;
    RT_BZERO(pQueue->bmAlloc, cbBitmap);
    ASMBitSetRange(pQueue->bmAlloc, 0, cItems);

    uint8_t *pbItem = (uint8_t *)&pQueue->bmAlloc[0] + cbBitmap;
    while (cItems-- > 0)
    {
        ((PPDMQUEUEITEMCORE)pbItem)->u64View = UINT64_C(0xfeedfeedfeedfeed);

        /* next */
        pbItem += cbItem;
    }
}


/**
 * Allocate an item from a queue, extended version.
 *
 * The allocated item must be handed on to PDMR3QueueInsert() after the
 * data have been filled in.
 *
 * @returns VBox status code.
 * @param   pVM         Pointer to the cross context VM structure w/ ring-0.
 * @param   hQueue      The queue handle.
 * @param   pvOwner     The queue owner.
 * @param   ppNew       Where to return the item pointer on success.
 * @thread  Any thread.
 */
VMMDECL(int) PDMQueueAllocEx(PVMCC pVM, PDMQUEUEHANDLE hQueue, void *pvOwner, PPDMQUEUEITEMCORE *ppNew)
{
    /*
     * Validate and translate input.
     */
    *ppNew = NULL;
    PDMQUEUE_HANDLE_TO_VARS_RETURN(pVM, hQueue, pvOwner);

    /*
     * Do the allocation.
     */
    uint32_t cEmptyScans = 0;
    for (;;)
    {
        /*
         * ASMBitFirstSet() mandates a cBits to be a multiple of 32 while cItems can be less (but the bitmap)
         * is aligned to 64 bytes and correctly initialized so only the number of items allocated are valid
         * and can be marked as free.
         */
        int32_t iBit = ASMBitFirstSet(pQueue->bmAlloc, RT_ALIGN(cItems, 32));
        if (iBit >= 0)
        {
            if (ASMAtomicBitTestAndClear(pQueue->bmAlloc, iBit))
            {
                PPDMQUEUEITEMCORE pNew = (PPDMQUEUEITEMCORE)&((uint8_t *)pQueue)[offItems + iBit * cbItem];
                pNew->u64View = UINT64_C(0xbeefbeefbeefbeef);
                *ppNew = pNew;
                return VINF_SUCCESS;
            }
            cEmptyScans = 0;
        }
        else if (++cEmptyScans < 16)
            ASMNopPause();
        else
        {
            STAM_REL_COUNTER_INC(&pQueue->StatAllocFailures);
            return VERR_OUT_OF_RESOURCES;
        }
    }
}


/**
 * Allocate an item from a queue.
 *
 * The allocated item must be handed on to PDMR3QueueInsert() after the
 * data have been filled in.
 *
 * @returns Pointer to the new item on success, NULL on failure.
 * @param   pVM         Pointer to the cross context VM structure w/ ring-0.
 * @param   hQueue      The queue handle.
 * @param   pvOwner     The queue owner.
 * @thread  Any thread.
 */
VMMDECL(PPDMQUEUEITEMCORE) PDMQueueAlloc(PVMCC pVM, PDMQUEUEHANDLE hQueue, void *pvOwner)
{
    PPDMQUEUEITEMCORE pNew = NULL;
    int rc = PDMQueueAllocEx(pVM, hQueue, pvOwner, &pNew);
    if (RT_SUCCESS(rc))
        return pNew;
    return NULL;
}


/**
 * Sets the FFs and fQueueFlushed.
 *
 * @param   pVM         Pointer to the cross context VM structure w/ ring-0.
 */
static void pdmQueueSetFF(PVMCC pVM)
{
    Log2(("PDMQueueInsert: VM_FF_PDM_QUEUES %d -> 1\n", VM_FF_IS_SET(pVM, VM_FF_PDM_QUEUES)));
    VM_FF_SET(pVM, VM_FF_PDM_QUEUES);
    ASMAtomicBitSet(&pVM->pdm.s.fQueueFlushing, PDM_QUEUE_FLUSH_FLAG_PENDING_BIT);
#ifdef IN_RING3
    VMR3NotifyGlobalFFU(pVM->pUVM, VMNOTIFYFF_FLAGS_DONE_REM);
#endif
}


/**
 * Queue an item.
 *
 * The item must have been obtained using PDMQueueAlloc(). Once the item
 * have been passed to this function it must not be touched!
 *
 * @returns VBox status code.
 * @param   pVM         Pointer to the cross context VM structure w/ ring-0.
 * @param   hQueue      The queue handle.
 * @param   pvOwner     The queue owner.
 * @param   pInsert     The item to insert.
 * @thread  Any thread.
 */
VMMDECL(int) PDMQueueInsert(PVMCC pVM, PDMQUEUEHANDLE hQueue, void *pvOwner, PPDMQUEUEITEMCORE pInsert)
{
    /*
     * Validate and translate input.
     */
    PDMQUEUE_HANDLE_TO_VARS_RETURN(pVM, hQueue, pvOwner);

    uint8_t * const pbItems   = (uint8_t *)pQueue + offItems;
    uintptr_t const offInsert = (uintptr_t)pInsert - (uintptr_t)pbItems;
    uintptr_t const iInsert   = offInsert / cbItem;
    AssertReturn(iInsert < cItems, VERR_INVALID_PARAMETER);
    AssertReturn(iInsert * cbItem == offInsert, VERR_INVALID_PARAMETER);

    AssertReturn(ASMBitTest(pQueue->bmAlloc, iInsert) == false, VERR_INVALID_PARAMETER);

    /*
     * Append the item to the pending list.
     */
    for (;;)
    {
        uint32_t const iOldPending = ASMAtomicUoReadU32(&pQueue->iPending);
        pInsert->iNext = iOldPending;
        if (ASMAtomicCmpXchgU32(&pQueue->iPending, iInsert, iOldPending))
            break;
        ASMNopPause();
    }

    if (pQueue->hTimer == NIL_TMTIMERHANDLE)
        pdmQueueSetFF(pVM);
    STAM_REL_COUNTER_INC(&pQueue->StatInsert);
    STAM_STATS({ ASMAtomicIncU32(&pQueue->cStatPending); });

    return VINF_SUCCESS;
}


/**
 * Schedule the queue for flushing (processing) if necessary.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if a flush was necessary.
 * @retval  VINF_NO_CHANGE if no flushing needed.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pvOwner     The alleged queue owner.
 * @param   hQueue      The queueu to maybe flush.
 */
VMMDECL(int) PDMQueueFlushIfNecessary(PVMCC pVM, PDMQUEUEHANDLE hQueue, void *pvOwner)
{
    /*
     * Validate input.
     */
    PDMQUEUE_HANDLE_TO_VARS_RETURN(pVM, hQueue, pvOwner);
    RT_NOREF(offItems);

    /*
     * Check and maybe flush.
     */
    if (ASMAtomicUoReadU32(&pQueue->iPending) != UINT32_MAX)
    {
        pdmQueueSetFF(pVM);
        return VINF_SUCCESS;
    }
    return VINF_NO_CHANGE;
}

