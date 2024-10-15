/* $Id: PDMR0Queue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Queue - Transport data and tasks to EMT and R3, ring-0 code.
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
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/process.h>
#include <iprt/string.h>



/**
 * Creates a ring-0 capable queue.
 *
 * This is only callable from EMT(0) when the VM is the VMSTATE_CREATING state.
 *
 * @returns VBox status code.
 * @param   pGVM                The ring-0 VM structure.
 * @param   pReq                The queue create request.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PDMR0QueueCreateReqHandler(PGVM pGVM, PPDMQUEUECREATEREQ pReq)
{
    /*
     * Validate input.
     * Note! Restricting to EMT(0) to avoid locking requirements.
     */
    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0 /*idCpu*/);
    AssertRCReturn(rc, rc);

    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertReturn(pReq->cItems <= PDMQUEUE_MAX_ITEMS, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cItems > 0, VERR_INVALID_PARAMETER);
    AssertReturn(pReq->cbItem <= PDMQUEUE_MAX_ITEM_SIZE, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cbItem >= sizeof(PDMQUEUEITEMCORE), VERR_INVALID_PARAMETER);
    pReq->cbItem = RT_ALIGN_32(pReq->cbItem, sizeof(uint64_t));
    AssertReturn((uint64_t)pReq->cbItem * pReq->cItems <= PDMQUEUE_MAX_TOTAL_SIZE_R0, VERR_OUT_OF_RANGE);

    void *pvOwnerR0;
    switch ((PDMQUEUETYPE)pReq->enmType)
    {
        case PDMQUEUETYPE_DEV:
        {
            AssertReturn(pReq->pvOwner != NIL_RTR3PTR, VERR_INVALID_POINTER);
            AssertReturn(!(pReq->pvOwner & HOST_PAGE_OFFSET_MASK), VERR_INVALID_POINTER);

            pvOwnerR0  = NULL;
            uint32_t i = pGVM->pdmr0.s.cDevInstances;
            while (i-- > 0)
            {
                PPDMDEVINSR0 pDevIns = pGVM->pdmr0.s.apDevInstances[i];
                if (   pDevIns
                    && RTR0MemObjAddressR3(pDevIns->Internal.s.hMapObj) == pReq->pvOwner)
                {
                    pvOwnerR0 = pDevIns;
                    break;
                }
            }
            AssertReturn(pvOwnerR0, VERR_NOT_OWNER);
            break;
        }

        case PDMQUEUETYPE_INTERNAL:
            AssertReturn(pReq->pvOwner == pGVM->pVMR3, VERR_NOT_OWNER);
            pvOwnerR0 = pGVM;
            break;

        default:
            AssertFailedReturn(VERR_INVALID_FUNCTION);
    }

    AssertReturn(pGVM->pdmr0.s.cQueues < RT_ELEMENTS(pGVM->pdmr0.s.aQueues), VERR_OUT_OF_RESOURCES);

    /*
     * Calculate the memory needed and allocate it.
     */
    uint32_t const cbBitmap = RT_ALIGN_32(RT_ALIGN_32(pReq->cItems, 64 /*uint64_t*/) / 8, 64 /*cache line */);
    uint32_t const cbQueue  = RT_UOFFSETOF(PDMQUEUE, bmAlloc)
                            + cbBitmap
                            + pReq->cbItem * pReq->cItems;

    RTR0MEMOBJ hMemObj = NIL_RTR0MEMOBJ;
    rc = RTR0MemObjAllocPage(&hMemObj, cbQueue, false /*fExecutable*/);
    if (RT_SUCCESS(rc))
    {
        PPDMQUEUE pQueue = (PPDMQUEUE)RTR0MemObjAddress(hMemObj);

        /*
         * Initialize the queue.
         */
        pdmQueueInit(pQueue, cbBitmap, pReq->cbItem, pReq->cItems, pReq->szName,
                     (PDMQUEUETYPE)pReq->enmType, pReq->pfnCallback, pReq->pvOwner);

        /*
         * Map it into ring-3.
         */
        RTR0MEMOBJ hMapObj = NIL_RTR0MEMOBJ;
        rc = RTR0MemObjMapUser(&hMapObj, hMemObj, (RTR3PTR)-1, HOST_PAGE_SIZE,
                               RTMEM_PROT_READ | RTMEM_PROT_WRITE, RTR0ProcHandleSelf());
        if (RT_SUCCESS(rc))
        {
            /*
             * Enter it into the handle tables.
             */
            uint32_t iQueue = pGVM->pdmr0.s.cQueues;
            if (iQueue < RT_ELEMENTS(pGVM->pdmr0.s.aQueues))
            {
                pGVM->pdmr0.s.aQueues[iQueue].pQueue      = pQueue;
                pGVM->pdmr0.s.aQueues[iQueue].hMemObj     = hMemObj;
                pGVM->pdmr0.s.aQueues[iQueue].hMapObj     = hMapObj;
                pGVM->pdmr0.s.aQueues[iQueue].pvOwner     = pvOwnerR0;
                pGVM->pdmr0.s.aQueues[iQueue].cbItem      = pReq->cbItem;
                pGVM->pdmr0.s.aQueues[iQueue].cItems      = pReq->cItems;
                pGVM->pdmr0.s.aQueues[iQueue].u32Reserved = UINT32_C(0xf00dface);

                pGVM->pdm.s.apRing0Queues[iQueue] = RTR0MemObjAddressR3(hMapObj);

                ASMCompilerBarrier(); /* paranoia */
                pGVM->pdm.s.cRing0Queues = iQueue + 1;
                pGVM->pdmr0.s.cQueues    = iQueue + 1;

                pReq->hQueue = iQueue;
                return VINF_SUCCESS;

            }
            rc = VERR_OUT_OF_RESOURCES;

            RTR0MemObjFree(hMapObj, true /*fFreeMappings*/);
        }
        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
    }
    return rc;
}


/**
 * Called by PDMR0CleanupVM to clean up a queue.
 *
 * @param   pGVM        The ring-0 VM structure.
 * @param   iQueue      Index into the ring-0 queue table.
 */
DECLHIDDEN(void) pdmR0QueueDestroy(PGVM pGVM, uint32_t iQueue)
{
    AssertReturnVoid(iQueue < RT_ELEMENTS(pGVM->pdmr0.s.aQueues));

    PPDMQUEUE pQueue = pGVM->pdmr0.s.aQueues[iQueue].pQueue;
    pGVM->pdmr0.s.aQueues[iQueue].pQueue  = NULL;
    if (RT_VALID_PTR(pQueue))
        pQueue->u32Magic = PDMQUEUE_MAGIC_DEAD;

    pGVM->pdmr0.s.aQueues[iQueue].pvOwner = NULL;

    RTR0MemObjFree(pGVM->pdmr0.s.aQueues[iQueue].hMapObj, true /*fFreeMappings*/);
    pGVM->pdmr0.s.aQueues[iQueue].hMapObj = NIL_RTR0MEMOBJ;

    RTR0MemObjFree(pGVM->pdmr0.s.aQueues[iQueue].hMemObj, true /*fFreeMappings*/);
    pGVM->pdmr0.s.aQueues[iQueue].hMemObj = NIL_RTR0MEMOBJ;
}

