/* $Id: PDMQueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <iprt/errcore.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int                  pdmR3QueueDestroyLocked(PVM pVM, PDMQUEUEHANDLE hQueue, void *pvOwner);
static DECLCALLBACK(void)   pdmR3QueueTimer(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser);



/**
 * Internal worker for the queue creation apis.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   cbItem              Item size.
 * @param   cItems              Number of items.
 * @param   cMilliesInterval    Number of milliseconds between polling the queue.
 *                              If 0 then the emulation thread will be notified whenever an item arrives.
 * @param   fRZEnabled          Set if the queue will be used from RC/R0,
 *                              these can only be created from EMT0.
 * @param   pszName             The queue name. Unique. Not copied.
 * @param   enmType             Owner type.
 * @param   pvOwner             The queue owner pointer.
 * @param   uCallback           Callback function.
 * @param   phQueue             Where to store the queue handle.
 *
 * @thread  Emulation thread only. When @a fRZEnables is true only EMT0.
 * @note    Caller owns ListCritSect.
 */
static int pdmR3QueueCreateLocked(PVM pVM, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval, bool fRZEnabled,
                                  const char *pszName, PDMQUEUETYPE enmType, void *pvOwner, uintptr_t uCallback,
                                  PDMQUEUEHANDLE *phQueue)
{
    /*
     * Validate and adjust the input.
     */
    if (fRZEnabled)
        VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    else
        VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    cbItem = RT_ALIGN(cbItem, sizeof(uint64_t));
    AssertMsgReturn(cbItem >= sizeof(PDMQUEUEITEMCORE) && cbItem < PDMQUEUE_MAX_ITEM_SIZE, ("cbItem=%zu\n", cbItem),
                    VERR_OUT_OF_RANGE);
    AssertMsgReturn(cItems >= 1 && cItems <= PDMQUEUE_MAX_ITEMS, ("cItems=%u\n", cItems), VERR_OUT_OF_RANGE);
    AssertMsgReturn((uint64_t)cbItem * cItems <= (fRZEnabled ? PDMQUEUE_MAX_TOTAL_SIZE_R0 : PDMQUEUE_MAX_TOTAL_SIZE_R3),
                    ("cItems=%u cbItem=%#x -> %#RX64, max %'u\n", cItems, cbItem, (uint64_t)cbItem * cItems,
                     fRZEnabled ? PDMQUEUE_MAX_TOTAL_SIZE_R0 : PDMQUEUE_MAX_TOTAL_SIZE_R3),
                    VERR_OUT_OF_RANGE);
    AssertReturn(!fRZEnabled || enmType == PDMQUEUETYPE_INTERNAL || enmType == PDMQUEUETYPE_DEV, VERR_INVALID_PARAMETER);
    if (SUPR3IsDriverless())
        fRZEnabled = false;

    /* Unqiue name that fits within the szName field: */
    size_t cchName = strlen(pszName);
    AssertReturn(cchName > 0, VERR_INVALID_NAME);
    AssertMsgReturn(cchName < RT_SIZEOFMEMB(PDMQUEUE, szName), ("'%s' is too long\n", pszName), VERR_INVALID_NAME);
    size_t i = pVM->pdm.s.cRing3Queues;
    while (i-- > 0 )
        AssertMsgReturn(strcmp(pVM->pdm.s.papRing3Queues[i]->szName, pszName) != 0, ("%s\n", pszName), VERR_DUPLICATE);
    i = pVM->pdm.s.cRing0Queues;
    while (i-- > 0 )
        AssertMsgReturn(strcmp(pVM->pdm.s.apRing0Queues[i]->szName, pszName) != 0, ("%s\n", pszName), VERR_DUPLICATE);

    /*
     * Align the item size and calculate the structure size.
     */
    PPDMQUEUE      pQueue;
    PDMQUEUEHANDLE hQueue;
    if (fRZEnabled)
    {
        /* Call ring-0 to allocate and create the queue: */
        PDMQUEUECREATEREQ Req;
        Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq    = sizeof(Req);
        Req.cItems       = cItems;
        Req.cbItem       = (uint32_t)cbItem;
        Req.enmType      = enmType;
        Req.pvOwner      = pvOwner;
        Req.pfnCallback  = (RTR3PTR)uCallback;
        RTStrCopy(Req.szName, sizeof(Req.szName), pszName);
        AssertCompileMembersSameSize(PDMQUEUECREATEREQ, szName, PDMQUEUE, szName);
        Req.hQueue       = NIL_PDMQUEUEHANDLE;

        int rc = VMMR3CallR0(pVM, VMMR0_DO_PDM_QUEUE_CREATE, 0, &Req.Hdr);
        if (RT_FAILURE(rc))
            return rc;
        hQueue = Req.hQueue;
        AssertReturn(hQueue < RT_ELEMENTS(pVM->pdm.s.apRing0Queues), VERR_INTERNAL_ERROR_2);
        pQueue = pVM->pdm.s.apRing0Queues[hQueue];
        AssertPtrReturn(pQueue, VERR_INTERNAL_ERROR_3);
        AssertReturn(pQueue->u32Magic == PDMQUEUE_MAGIC, VERR_INTERNAL_ERROR_4);
        AssertReturn(pQueue->cbItem == cbItem, VERR_INTERNAL_ERROR_4);
        AssertReturn(pQueue->cItems == cItems, VERR_INTERNAL_ERROR_4);
        AssertReturn(pQueue->enmType == enmType, VERR_INTERNAL_ERROR_4);
        AssertReturn(pQueue->u.Gen.pvOwner == pvOwner, VERR_INTERNAL_ERROR_4);
        AssertReturn(pQueue->u.Gen.pfnCallback == (RTR3PTR)uCallback, VERR_INTERNAL_ERROR_4);
    }
    else
    {
        /* Do it here using the paged heap: */
        uint32_t const cbBitmap = RT_ALIGN_32(RT_ALIGN_32(cItems, 64) / 8, 64); /* keep bitmap in it's own cacheline  */
        uint32_t const cbQueue  = RT_OFFSETOF(PDMQUEUE, bmAlloc)
                                + cbBitmap
                                + (uint32_t)cbItem * cItems;
        pQueue = (PPDMQUEUE)RTMemPageAllocZ(cbQueue);
        if (!pQueue)
            return VERR_NO_PAGE_MEMORY;
        pdmQueueInit(pQueue, cbBitmap, (uint32_t)cbItem, cItems, pszName, enmType, (RTR3PTR)uCallback, pvOwner);

        uint32_t iQueue = pVM->pdm.s.cRing3Queues;
        if (iQueue >= pVM->pdm.s.cRing3QueuesAlloc)
        {
            AssertLogRelMsgReturnStmt(iQueue < _16K, ("%#x\n", iQueue), RTMemPageFree(pQueue, cbQueue), VERR_TOO_MANY_OPENS);

            uint32_t const cNewAlloc = RT_ALIGN_32(iQueue, 64) + 64;
            PPDMQUEUE *papQueuesNew = (PPDMQUEUE *)RTMemAllocZ(cNewAlloc * sizeof(papQueuesNew[0]));
            AssertLogRelMsgReturnStmt(papQueuesNew, ("cNewAlloc=%u\n", cNewAlloc), RTMemPageFree(pQueue, cbQueue), VERR_NO_MEMORY);

            if (iQueue)
                memcpy(papQueuesNew, pVM->pdm.s.papRing3Queues, iQueue * sizeof(papQueuesNew[0]));
            PPDMQUEUE *papQueuesOld = ASMAtomicXchgPtrT(&pVM->pdm.s.papRing3Queues, papQueuesNew, PPDMQUEUE *);
            pVM->pdm.s.cRing3QueuesAlloc = cNewAlloc;
            RTMemFree(papQueuesOld);
        }

        pVM->pdm.s.papRing3Queues[iQueue] = pQueue;
        pVM->pdm.s.cRing3Queues           = iQueue + 1;
        hQueue = iQueue + RT_ELEMENTS(pVM->pdm.s.apRing0Queues);
    }

    /*
     * Create timer?
     */
    if (cMilliesInterval)
    {
        char szName[48+6];
        RTStrPrintf(szName, sizeof(szName), "Que/%s", pQueue->szName);
        int rc = TMR3TimerCreate(pVM, TMCLOCK_REAL, pdmR3QueueTimer, pQueue, TMTIMER_FLAGS_NO_RING0, szName, &pQueue->hTimer);
        if (RT_SUCCESS(rc))
        {
            rc = TMTimerSetMillies(pVM, pQueue->hTimer, cMilliesInterval);
            if (RT_SUCCESS(rc))
                pQueue->cMilliesInterval = cMilliesInterval;
            else
            {
                AssertMsgFailed(("TMTimerSetMillies failed rc=%Rrc\n", rc));
                int rc2 = TMR3TimerDestroy(pVM, pQueue->hTimer); AssertRC(rc2);
                pQueue->hTimer = NIL_TMTIMERHANDLE;
            }
        }
        else
            AssertMsgFailed(("TMR3TimerCreateInternal failed rc=%Rrc\n", rc));
        if (RT_FAILURE(rc))
        {
            if (!fRZEnabled)
                pdmR3QueueDestroyLocked(pVM, hQueue, pvOwner);
            /* else: will clean up queue when VM is destroyed */
            return rc;
        }
    }

    /*
     * Register the statistics.
     */
    STAMR3RegisterF(pVM, &pQueue->cbItem,               STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                    "Item size.",                       "/PDM/Queue/%s/cbItem",         pQueue->szName);
    STAMR3RegisterF(pVM, &pQueue->cItems,               STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                    "Queue size.",                      "/PDM/Queue/%s/cItems",         pQueue->szName);
    STAMR3RegisterF(pVM, &pQueue->rcOkay,               STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                    "Non-zero means queue is busted.",  "/PDM/Queue/%s/rcOkay",         pQueue->szName);
    STAMR3RegisterF(pVM, &pQueue->StatAllocFailures,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                    "PDMQueueAlloc failures.",          "/PDM/Queue/%s/AllocFailures",  pQueue->szName);
    STAMR3RegisterF(pVM, &pQueue->StatInsert,           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                    "Calls to PDMQueueInsert.",         "/PDM/Queue/%s/Insert",         pQueue->szName);
    STAMR3RegisterF(pVM, &pQueue->StatFlush,            STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                    "Calls to pdmR3QueueFlush.",        "/PDM/Queue/%s/Flush",          pQueue->szName);
    STAMR3RegisterF(pVM, &pQueue->StatFlushLeftovers,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                    "Left over items after flush.",     "/PDM/Queue/%s/FlushLeftovers", pQueue->szName);
#ifdef VBOX_WITH_STATISTICS
    STAMR3RegisterF(pVM, &pQueue->StatFlushPrf,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                    "Profiling pdmR3QueueFlush.",       "/PDM/Queue/%s/FlushPrf",       pQueue->szName);
    STAMR3RegisterF(pVM, (void *)&pQueue->cStatPending, STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                    "Pending items.",                   "/PDM/Queue/%s/Pending",        pQueue->szName);
#endif

    *phQueue = hQueue;
    return VINF_SUCCESS;
}


/**
 * Create a queue with a device owner.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pDevIns             Device instance.
 * @param   cbItem              Size a queue item.
 * @param   cItems              Number of items in the queue.
 * @param   cMilliesInterval    Number of milliseconds between polling the queue.
 *                              If 0 then the emulation thread will be notified whenever an item arrives.
 * @param   pfnCallback         The consumer function.
 * @param   fRZEnabled          Set if the queue must be usable from RC/R0.
 * @param   pszName             The queue name. Unique. Copied.
 * @param   phQueue             Where to store the queue handle on success.
 * @thread  Emulation thread only. Only EMT0 when @a fRZEnables is true.
 */
VMMR3_INT_DECL(int) PDMR3QueueCreateDevice(PVM pVM, PPDMDEVINS pDevIns, size_t cbItem, uint32_t cItems,
                                           uint32_t cMilliesInterval, PFNPDMQUEUEDEV pfnCallback,
                                           bool fRZEnabled, const char *pszName, PDMQUEUEHANDLE *phQueue)
{
    LogFlow(("PDMR3QueueCreateDevice: pDevIns=%p cbItem=%d cItems=%d cMilliesInterval=%d pfnCallback=%p fRZEnabled=%RTbool pszName=%s\n",
             pDevIns, cbItem, cItems, cMilliesInterval, pfnCallback, fRZEnabled, pszName));

    /*
     * Validate input.
     */
    VM_ASSERT_EMT0(pVM);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertPtrReturn(pDevIns, VERR_INVALID_POINTER);

    if (!(pDevIns->Internal.s.fIntFlags & PDMDEVINSINT_FLAGS_R0_ENABLED))
        fRZEnabled = false;

    /*
     * Create the queue.
     */
    int rc = RTCritSectEnter(&pVM->pUVM->pdm.s.ListCritSect);
    AssertRCReturn(rc, rc);

    rc = pdmR3QueueCreateLocked(pVM, cbItem, cItems, cMilliesInterval, fRZEnabled, pszName,
                                PDMQUEUETYPE_DEV, pDevIns, (uintptr_t)pfnCallback, phQueue);

    RTCritSectLeave(&pVM->pUVM->pdm.s.ListCritSect);
    if (RT_SUCCESS(rc))
        Log(("PDM: Created device queue %#RX64; cbItem=%d cItems=%d cMillies=%d pfnCallback=%p pDevIns=%p\n",
             *phQueue, cbItem, cItems, cMilliesInterval, pfnCallback, pDevIns));
    return rc;
}


/**
 * Create a queue with a driver owner.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pDrvIns             Driver instance.
 * @param   cbItem              Size a queue item.
 * @param   cItems              Number of items in the queue.
 * @param   cMilliesInterval    Number of milliseconds between polling the queue.
 *                              If 0 then the emulation thread will be notified whenever an item arrives.
 * @param   pfnCallback         The consumer function.
 * @param   pszName             The queue name. Unique. Copied.
 * @param   phQueue             Where to store the queue handle on success.
 * @thread  Emulation thread only.
 */
VMMR3_INT_DECL(int) PDMR3QueueCreateDriver(PVM pVM, PPDMDRVINS pDrvIns, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                           PFNPDMQUEUEDRV pfnCallback, const char *pszName, PDMQUEUEHANDLE *phQueue)
{
    LogFlow(("PDMR3QueueCreateDriver: pDrvIns=%p cbItem=%d cItems=%d cMilliesInterval=%d pfnCallback=%p pszName=%s\n",
             pDrvIns, cbItem, cItems, cMilliesInterval, pfnCallback, pszName));

    /*
     * Validate input.
     */
    VM_ASSERT_EMT0(pVM);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertPtrReturn(pDrvIns, VERR_INVALID_POINTER);

    /*
     * Create the queue.
     */
    int rc = RTCritSectEnter(&pVM->pUVM->pdm.s.ListCritSect);
    AssertRCReturn(rc, rc);

    rc = pdmR3QueueCreateLocked(pVM, cbItem, cItems, cMilliesInterval, false /*fRZEnabled*/, pszName,
                                PDMQUEUETYPE_DRV, pDrvIns, (uintptr_t)pfnCallback, phQueue);

    RTCritSectLeave(&pVM->pUVM->pdm.s.ListCritSect);
    if (RT_SUCCESS(rc))
        Log(("PDM: Created driver queue %#RX64; cbItem=%d cItems=%d cMillies=%d pfnCallback=%p pDrvIns=%p\n",
             *phQueue, cbItem, cItems, cMilliesInterval, pfnCallback, pDrvIns));
    return rc;
}


/**
 * Create a queue with an internal owner.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   cbItem              Size a queue item.
 * @param   cItems              Number of items in the queue.
 * @param   cMilliesInterval    Number of milliseconds between polling the queue.
 *                              If 0 then the emulation thread will be notified whenever an item arrives.
 * @param   pfnCallback         The consumer function.
 * @param   fRZEnabled          Set if the queue must be usable from RC/R0.
 * @param   pszName             The queue name. Unique. Copied.
 * @param   phQueue             Where to store the queue handle on success.
 * @thread  Emulation thread only. When @a fRZEnables is true only EMT0.
 */
VMMR3_INT_DECL(int) PDMR3QueueCreateInternal(PVM pVM, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                             PFNPDMQUEUEINT pfnCallback, bool fRZEnabled,
                                             const char *pszName, PDMQUEUEHANDLE *phQueue)
{
    LogFlow(("PDMR3QueueCreateInternal: cbItem=%d cItems=%d cMilliesInterval=%d pfnCallback=%p fRZEnabled=%RTbool pszName=%s\n",
             cbItem, cItems, cMilliesInterval, pfnCallback, fRZEnabled, pszName));

    /*
     * Validate input.
     */
    VM_ASSERT_EMT0(pVM);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);

    /*
     * Create the queue.
     */
    int rc = RTCritSectEnter(&pVM->pUVM->pdm.s.ListCritSect);
    AssertRCReturn(rc, rc);

    rc = pdmR3QueueCreateLocked(pVM, cbItem, cItems, cMilliesInterval, fRZEnabled, pszName,
                                PDMQUEUETYPE_INTERNAL, pVM, (uintptr_t)pfnCallback, phQueue);

    RTCritSectLeave(&pVM->pUVM->pdm.s.ListCritSect);
    if (RT_SUCCESS(rc))
        Log(("PDM: Created internal queue %p; cbItem=%d cItems=%d cMillies=%d pfnCallback=%p\n",
             *phQueue, cbItem, cItems, cMilliesInterval, pfnCallback));
    return rc;
}


/**
 * Create a queue with an external owner.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   cbItem              Size a queue item.
 * @param   cItems              Number of items in the queue.
 * @param   cMilliesInterval    Number of milliseconds between polling the queue.
 *                              If 0 then the emulation thread will be notified whenever an item arrives.
 * @param   pfnCallback         The consumer function.
 * @param   pvUser              The user argument to the consumer function.
 * @param   pszName             The queue name. Unique. Not copied.
 * @param   phQueue             Where to store the queue handle on success.
 * @thread  Emulation thread only.
 */
VMMR3DECL(int) PDMR3QueueCreateExternal(PVM pVM, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                        PFNPDMQUEUEEXT pfnCallback, void *pvUser,
                                        const char *pszName, PDMQUEUEHANDLE *phQueue)
{
    LogFlow(("PDMR3QueueCreateExternal: cbItem=%d cItems=%d cMilliesInterval=%d pfnCallback=%p pszName=%s\n",
             cbItem, cItems, cMilliesInterval, pfnCallback, pszName));

    /*
     * Validate input.
     */
    VM_ASSERT_EMT0(pVM);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);

    /*
     * Create the queue.
     */
    int rc = RTCritSectEnter(&pVM->pUVM->pdm.s.ListCritSect);
    AssertRCReturn(rc, rc);

    rc = pdmR3QueueCreateLocked(pVM, cbItem, cItems, cMilliesInterval, false /*fRZEnabled*/, pszName,
                                PDMQUEUETYPE_EXTERNAL, pvUser, (uintptr_t)pfnCallback, phQueue);

    RTCritSectLeave(&pVM->pUVM->pdm.s.ListCritSect);
    if (RT_SUCCESS(rc))
        Log(("PDM: Created external queue %p; cbItem=%d cItems=%d cMillies=%d pfnCallback=%p pvUser=%p\n",
             *phQueue, cbItem, cItems, cMilliesInterval, pfnCallback, pvUser));
    return rc;
}


/**
 * Destroy a queue.
 *
 * @returns VBox status code.
 * @param   pVM         Pointer to the cross context VM structure.
 * @param   hQueue      Handle to the queue that should be destroyed.
 * @param   pvOwner     The owner address.
 * @thread  EMT
 */
static int pdmR3QueueDestroyLocked(PVM pVM, PDMQUEUEHANDLE hQueue, void *pvOwner)
{
    LogFlow(("pdmR3QueueDestroyLocked: hQueue=%p pvOwner=%p\n", hQueue, pvOwner));
    Assert(RTCritSectIsOwner(&pVM->pUVM->pdm.s.ListCritSect));

    /*
     * Validate input.
     */
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    if (hQueue == NIL_PDMQUEUEHANDLE)
        return VINF_SUCCESS;

    PPDMQUEUE pQueue;
    bool      fRZEnabled = false;
    if (hQueue < RT_ELEMENTS(pVM->pdm.s.apRing0Queues))
    {
        AssertReturn(hQueue < pVM->pdm.s.cRing0Queues, VERR_INVALID_HANDLE);
        pQueue = pVM->pdm.s.apRing0Queues[hQueue];
        AssertPtrReturn(pQueue, VERR_INVALID_HANDLE);
        AssertReturn(pQueue->u32Magic == PDMQUEUE_MAGIC, VERR_INVALID_HANDLE);
        AssertReturn(pQueue->u.Gen.pvOwner == pvOwner, VERR_INVALID_HANDLE);

        /* Lazy bird: Cannot dynamically delete ring-0 capable queues. */
        AssertFailedReturn(VERR_NOT_SUPPORTED);
    }
    else
    {
        hQueue -= RT_ELEMENTS(pVM->pdm.s.apRing0Queues);
        AssertReturn(hQueue < pVM->pdm.s.cRing3Queues, VERR_INVALID_HANDLE);
        pQueue = pVM->pdm.s.papRing3Queues[hQueue];
        AssertPtrReturn(pQueue, VERR_INVALID_HANDLE);
        AssertReturn(pQueue->u32Magic == PDMQUEUE_MAGIC, VERR_INVALID_HANDLE);
        AssertReturn(pQueue->u.Gen.pvOwner == pvOwner, VERR_INVALID_HANDLE);

        /* Enter the lock here to serialize with other EMTs traversing the handles. */
        pdmLock(pVM);
        pVM->pdm.s.papRing3Queues[hQueue] = NULL;
        if (hQueue + 1 == pVM->pdm.s.cRing3Queues)
        {
            while (hQueue > 0 && pVM->pdm.s.papRing3Queues[hQueue - 1] == NULL)
                hQueue--;
            pVM->pdm.s.cRing3Queues = hQueue;
        }
        pQueue->u32Magic = PDMQUEUE_MAGIC_DEAD;
        pdmUnlock(pVM);
    }

    /*
     * Deregister statistics.
     */
    STAMR3DeregisterF(pVM->pUVM, "/PDM/Queue/%s/*", pQueue->szName);

    /*
     * Destroy the timer and free it.
     */
    if (pQueue->hTimer != NIL_TMTIMERHANDLE)
    {
        TMR3TimerDestroy(pVM, pQueue->hTimer);
        pQueue->hTimer = NIL_TMTIMERHANDLE;
    }
    if (!fRZEnabled)
        RTMemPageFree(pQueue, pQueue->offItems + pQueue->cbItem * pQueue->cItems);

    return VINF_SUCCESS;
}


/**
 * Destroy a queue.
 *
 * @returns VBox status code.
 * @param   pVM         Pointer to the cross context VM structure.
 * @param   hQueue      Handle to the queue that should be destroyed.
 * @param   pvOwner     The owner address.
 * @thread  EMT
 * @note    Externally visible mainly for testing purposes.
 */
VMMR3DECL(int) PDMR3QueueDestroy(PVM pVM, PDMQUEUEHANDLE hQueue, void *pvOwner)
{
    PUVM const pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);

    int rc = pdmR3QueueDestroyLocked(pVM, hQueue, pvOwner);

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return rc;
}


/**
 * Destroy a all queues with a given owner.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvOwner     The owner pointer.
 * @param   enmType     Owner type.
 * @thread  EMT
 */
static int pdmR3QueueDestroyByOwner(PVM pVM, void *pvOwner, PDMQUEUETYPE enmType)
{
    LogFlow(("pdmR3QueueDestroyByOwner: pvOwner=%p enmType=%d\n", pvOwner, enmType));

    /*
     * Validate input.
     */
    AssertPtrReturn(pvOwner, VERR_INVALID_PARAMETER);
    AssertReturn(pvOwner != pVM, VERR_INVALID_PARAMETER);
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT); /* Not requiring EMT0 here as we cannot destroy RZ capable ones here. */

    /*
     * Scan and destroy.
     */
    PUVM const pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);

    uint32_t i = pVM->pdm.s.cRing0Queues;
    while (i-- > 0)
    {
        PPDMQUEUE pQueue = pVM->pdm.s.apRing0Queues[i];
        if (   pQueue
            && pQueue->u.Gen.pvOwner == pvOwner
            && pQueue->enmType == enmType)
        {
            /* Not supported at runtime. */
            VM_ASSERT_STATE_RETURN(pVM, VMSTATE_DESTROYING, VERR_WRONG_ORDER);
        }
    }

    i = pVM->pdm.s.cRing3Queues;
    while (i-- > 0)
    {
        PPDMQUEUE pQueue = pVM->pdm.s.papRing3Queues[i];
        if (   pQueue
            && pQueue->u.Gen.pvOwner == pvOwner
            && pQueue->enmType       == enmType)
            pdmR3QueueDestroyLocked(pVM, i + RT_ELEMENTS(pVM->pdm.s.apRing0Queues), pvOwner);
    }

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return VINF_SUCCESS;
}


/**
 * Destroy a all queues owned by the specified device.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pDevIns     Device instance.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(int) PDMR3QueueDestroyDevice(PVM pVM, PPDMDEVINS pDevIns)
{
    LogFlow(("PDMR3QueueDestroyDevice: pDevIns=%p\n", pDevIns));
    return pdmR3QueueDestroyByOwner(pVM, pDevIns, PDMQUEUETYPE_DEV);
}


/**
 * Destroy a all queues owned by the specified driver.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pDrvIns     Driver instance.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(int) PDMR3QueueDestroyDriver(PVM pVM, PPDMDRVINS pDrvIns)
{
    LogFlow(("PDMR3QueueDestroyDriver: pDrvIns=%p\n", pDrvIns));
    return pdmR3QueueDestroyByOwner(pVM, pDrvIns, PDMQUEUETYPE_DRV);
}


/**
 * Free an item.
 *
 * @param   pQueue  The queue.
 * @param   pbItems Where the items area starts.
 * @param   cbItem  Item size.
 * @param   pItem   The item to free.
 */
DECLINLINE(void) pdmR3QueueFreeItem(PPDMQUEUE pQueue, uint8_t *pbItems, uint32_t cbItem, PPDMQUEUEITEMCORE pItem)
{
    pItem->u64View = UINT64_C(0xfeedfeedfeedfeed);

    uintptr_t const offItem = (uintptr_t)pItem - (uintptr_t)pbItems;
    uintptr_t const iItem   = offItem / cbItem;
    Assert(!(offItem % cbItem));
    Assert(iItem < pQueue->cItems);
    AssertReturnVoidStmt(ASMAtomicBitTestAndSet(pQueue->bmAlloc, iItem) == false, pQueue->rcOkay = VERR_INTERNAL_ERROR_4);
    STAM_STATS({ ASMAtomicDecU32(&pQueue->cStatPending); });
}



/**
 * Process pending items in one queue.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pQueue  The queue needing flushing.
 */
static int pdmR3QueueFlush(PVM pVM, PPDMQUEUE pQueue)
{
    STAM_PROFILE_START(&pQueue->StatFlushPrf,p);

    uint32_t const  cbItem  = pQueue->cbItem;
    uint32_t const  cItems  = pQueue->cItems;
    uint8_t * const pbItems = (uint8_t *)pQueue + pQueue->offItems;

    /*
     * Get the list and reverse it into a pointer list (inserted in LIFO order to avoid locking).
     */
    uint32_t          cPending = 0;
    PPDMQUEUEITEMCORE pHead    = NULL;
    {
        uint32_t iCur = ASMAtomicXchgU32(&pQueue->iPending, UINT32_MAX);
        do
        {
            AssertMsgReturn(iCur < cItems, ("%#x vs %#x\n", iCur, cItems), pQueue->rcOkay = VERR_INTERNAL_ERROR_5);
            AssertReturn(ASMBitTest(pQueue->bmAlloc, iCur) == false, pQueue->rcOkay = VERR_INTERNAL_ERROR_3);
            PPDMQUEUEITEMCORE pCur = (PPDMQUEUEITEMCORE)&pbItems[iCur * cbItem];

            iCur = pCur->iNext;
            ASMCompilerBarrier(); /* paranoia */
            pCur->pNext = pHead;
            pHead = pCur;
            cPending++;
        } while (iCur != UINT32_MAX);
    }
    RT_NOREF(cPending);

    /*
     * Feed the items to the consumer function.
     */
    Log2(("pdmR3QueueFlush: pQueue=%p enmType=%d pHead=%p cItems=%u\n", pQueue, pQueue->enmType, pHead, cPending));
    switch (pQueue->enmType)
    {
        case PDMQUEUETYPE_DEV:
            while (pHead)
            {
                if (!pQueue->u.Dev.pfnCallback(pQueue->u.Dev.pDevIns, pHead))
                    break;
                PPDMQUEUEITEMCORE pFree = pHead;
                pHead = pHead->pNext;
                ASMCompilerBarrier(); /* paranoia */
                pdmR3QueueFreeItem(pQueue, pbItems, cbItem, pFree);
            }
            break;

        case PDMQUEUETYPE_DRV:
            while (pHead)
            {
                if (!pQueue->u.Drv.pfnCallback(pQueue->u.Drv.pDrvIns, pHead))
                    break;
                PPDMQUEUEITEMCORE pFree = pHead;
                pHead = pHead->pNext;
                ASMCompilerBarrier(); /* paranoia */
                pdmR3QueueFreeItem(pQueue, pbItems, cbItem, pFree);
            }
            break;

        case PDMQUEUETYPE_INTERNAL:
            while (pHead)
            {
                if (!pQueue->u.Int.pfnCallback(pVM, pHead))
                    break;
                PPDMQUEUEITEMCORE pFree = pHead;
                pHead = pHead->pNext;
                ASMCompilerBarrier(); /* paranoia */
                pdmR3QueueFreeItem(pQueue, pbItems, cbItem, pFree);
            }
            break;

        case PDMQUEUETYPE_EXTERNAL:
            while (pHead)
            {
                if (!pQueue->u.Ext.pfnCallback(pQueue->u.Ext.pvUser, pHead))
                    break;
                PPDMQUEUEITEMCORE pFree = pHead;
                pHead = pHead->pNext;
                ASMCompilerBarrier(); /* paranoia */
                pdmR3QueueFreeItem(pQueue, pbItems, cbItem, pFree);
            }
            break;

        default:
            AssertMsgFailed(("Invalid queue type %d\n", pQueue->enmType));
            break;
    }

    /*
     * Success?
     */
    if (!pHead)
    { /* likely */ }
    else
    {
        /*
         * Reverse the list and turn it back into index chain.
         */
        uint32_t iPendingHead = UINT32_MAX;
        do
        {
            PPDMQUEUEITEMCORE pInsert = pHead;
            pHead = pHead->pNext;
            ASMCompilerBarrier(); /* paranoia */
            pInsert->iNext = iPendingHead;
            iPendingHead = ((uintptr_t)pInsert - (uintptr_t)pbItems) / cbItem;
        } while (pHead);

        /*
         * Insert the list at the tail of the pending list.  If someone races
         * us there, we have to join the new LIFO with the old.
         */
        for (;;)
        {
            if (ASMAtomicCmpXchgU32(&pQueue->iPending, iPendingHead, UINT32_MAX))
                break;

            uint32_t const iNewPending = ASMAtomicXchgU32(&pQueue->iPending, UINT32_MAX);
            if (iNewPending != UINT32_MAX)
            {
                /* Find the last entry and chain iPendingHead onto it. */
                uint32_t iCur = iNewPending;
                for (;;)
                {
                    AssertReturn(iCur < cItems, pQueue->rcOkay = VERR_INTERNAL_ERROR_2);
                    AssertReturn(ASMBitTest(pQueue->bmAlloc, iCur) == false, pQueue->rcOkay = VERR_INTERNAL_ERROR_3);
                    PPDMQUEUEITEMCORE pCur = (PPDMQUEUEITEMCORE)&pbItems[iCur * cbItem];
                    iCur = pCur->iNext;
                    if (iCur == UINT32_MAX)
                    {
                        pCur->iNext = iPendingHead;
                        break;
                    }
                }

                iPendingHead = iNewPending;
            }
        }

        STAM_REL_COUNTER_INC(&pQueue->StatFlushLeftovers);
    }

    STAM_PROFILE_STOP(&pQueue->StatFlushPrf,p);
    return VINF_SUCCESS;
}


/**
 * Flush pending queues.
 * This is a forced action callback.
 *
 * @param   pVM     The cross context VM structure.
 * @thread  Emulation thread only.
 * @note    Internal, but exported for use in the testcase.
 */
VMMR3DECL(void) PDMR3QueueFlushAll(PVM pVM)
{
    VM_ASSERT_EMT(pVM);
    LogFlow(("PDMR3QueuesFlush:\n"));

    /*
     * Only let one EMT flushing queues at any one time to preserve the order
     * and to avoid wasting time. The FF is always cleared here, because it's
     * only used to get someones attention. Queue inserts occurring during the
     * flush are caught using the pending bit.
     *
     * Note! We must check the force action and pending flags after clearing
     *       the active bit!
     */
    VM_FF_CLEAR(pVM, VM_FF_PDM_QUEUES);
    while (!ASMAtomicBitTestAndSet(&pVM->pdm.s.fQueueFlushing, PDM_QUEUE_FLUSH_FLAG_ACTIVE_BIT))
    {
        ASMAtomicBitClear(&pVM->pdm.s.fQueueFlushing, PDM_QUEUE_FLUSH_FLAG_PENDING_BIT);

        /* Scan the ring-0 queues: */
        size_t i = pVM->pdm.s.cRing0Queues;
        while (i-- > 0)
        {
            PPDMQUEUE pQueue = pVM->pdm.s.apRing0Queues[i];
            if (   pQueue
                && pQueue->iPending != UINT32_MAX
                && pQueue->hTimer == NIL_TMTIMERHANDLE
                && pQueue->rcOkay == VINF_SUCCESS)
                pdmR3QueueFlush(pVM, pQueue);
        }

        /* Scan the ring-3 queues: */
/** @todo Deal with destroy concurrency issues. */
        i = pVM->pdm.s.cRing3Queues;
        while (i-- > 0)
        {
            PPDMQUEUE pQueue = pVM->pdm.s.papRing3Queues[i];
            if (   pQueue
                && pQueue->iPending != UINT32_MAX
                && pQueue->hTimer == NIL_TMTIMERHANDLE
                && pQueue->rcOkay == VINF_SUCCESS)
                pdmR3QueueFlush(pVM, pQueue);
        }

        ASMAtomicBitClear(&pVM->pdm.s.fQueueFlushing, PDM_QUEUE_FLUSH_FLAG_ACTIVE_BIT);

        /* We're done if there were no inserts while we were busy. */
        if (   !ASMBitTest(&pVM->pdm.s.fQueueFlushing, PDM_QUEUE_FLUSH_FLAG_PENDING_BIT)
            && !VM_FF_IS_SET(pVM, VM_FF_PDM_QUEUES))
            break;
        VM_FF_CLEAR(pVM, VM_FF_PDM_QUEUES);
    }
}



/**
 * @callback_method_impl{FNTMTIMERINT, Timer handler for one PDM queue.}
 */
static DECLCALLBACK(void) pdmR3QueueTimer(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPDMQUEUE pQueue = (PPDMQUEUE)pvUser;
    Assert(hTimer == pQueue->hTimer);

    if (pQueue->iPending != UINT32_MAX)
        pdmR3QueueFlush(pVM, pQueue);

    int rc = TMTimerSetMillies(pVM, hTimer, pQueue->cMilliesInterval);
    AssertRC(rc);
}


/**
 * Terminate the queues, freeing any resources still allocated.
 *
 * @param   pVM                 The cross-context VM structure.
 */
DECLHIDDEN(void) pdmR3QueueTerm(PVM pVM)
{
    PUVM const pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);

    if (pVM->pdm.s.papRing3Queues)
    {
        /*
         * Free the R3 queue handle array.
         */
        PDMQUEUEHANDLE cQueues = pVM->pdm.s.cRing3Queues;
        for (PDMQUEUEHANDLE i = 0; i < cQueues; i++)
            if (pVM->pdm.s.papRing3Queues[i])
            {
                PPDMQUEUE pQueue = pVM->pdm.s.papRing3Queues[i];

                pdmR3QueueDestroyLocked(pVM, RT_ELEMENTS(pVM->pdm.s.apRing0Queues) + i, pQueue->u.Gen.pvOwner);
                Assert(!pVM->pdm.s.papRing3Queues[i]);
            }

        RTMemFree(pVM->pdm.s.papRing3Queues);
        pVM->pdm.s.cRing3QueuesAlloc = 0;
        pVM->pdm.s.papRing3Queues    = NULL;
    }

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
}
