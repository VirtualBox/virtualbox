/* $Id: PDMAsyncCompletionFileFailsafe.cpp 108651 2025-03-06 11:15:45Z alexander.eichner@oracle.com $ */
/** @file
 * PDM Async I/O - Transport data asynchronous in R3 using EMT.
 * Simple File I/O manager.
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
#define LOG_GROUP LOG_GROUP_PDM_ASYNC_COMPLETION
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <VBox/log.h>

#include "PDMAsyncCompletionFileInternal.h"



/**
 * Put a list of tasks in the pending request list of an endpoint.
 */
DECLINLINE(void) pdmacFileAioMgrEpAddTaskList(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint, PPDMACTASKFILE pTaskHead)
{
    /* Add the rest of the tasks to the pending list */
    if (!pEndpoint->AioMgr.pReqsPendingHead)
    {
        Assert(!pEndpoint->AioMgr.pReqsPendingTail);
        pEndpoint->AioMgr.pReqsPendingHead = pTaskHead;
    }
    else
    {
        Assert(pEndpoint->AioMgr.pReqsPendingTail);
        pEndpoint->AioMgr.pReqsPendingTail->pNext = pTaskHead;
    }

    /* Update the tail. */
    while (pTaskHead->pNext)
        pTaskHead = pTaskHead->pNext;

    pEndpoint->AioMgr.pReqsPendingTail = pTaskHead;
    pTaskHead->pNext = NULL;
}

/**
 * Processes a given task list for assigned to the given endpoint.
 */
static int pdmacFileAioMgrFailsafeProcessEndpointTaskList(PPDMACEPFILEMGR pAioMgr,
                                                          PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                                          PPDMACTASKFILE pTasks)
{
    int rc = VINF_SUCCESS;

    while (pTasks)
    {
        RTMSINTERVAL msWhenNext;
        PPDMACTASKFILE pCurr = pTasks;

        if (!pdmacEpIsTransferAllowed(&pEndpoint->Core, pCurr->DataSeg.cbSeg, &msWhenNext))
        {
            pAioMgr->msBwLimitExpired = RT_MIN(pAioMgr->msBwLimitExpired, msWhenNext);
            break;
        }

        pTasks = pTasks->pNext;

        switch (pCurr->enmTransferType)
        {
            case PDMACTASKFILETRANSFER_FLUSH:
            {
                rc = RTFileFlush(pEndpoint->hFile);
                break;
            }
            case PDMACTASKFILETRANSFER_READ:
            case PDMACTASKFILETRANSFER_WRITE:
            {
                if (pCurr->enmTransferType == PDMACTASKFILETRANSFER_READ)
                {
                    rc = RTFileReadAt(pEndpoint->hFile, pCurr->Off,
                                      pCurr->DataSeg.pvSeg,
                                      pCurr->DataSeg.cbSeg,
                                      NULL);
                }
                else
                {
                    if (RT_UNLIKELY((uint64_t)pCurr->Off + pCurr->DataSeg.cbSeg > pEndpoint->cbFile))
                    {
                        ASMAtomicWriteU64(&pEndpoint->cbFile, pCurr->Off + pCurr->DataSeg.cbSeg);
                        RTFileSetSize(pEndpoint->hFile, pCurr->Off + pCurr->DataSeg.cbSeg);
                    }

                    rc = RTFileWriteAt(pEndpoint->hFile, pCurr->Off,
                                       pCurr->DataSeg.pvSeg,
                                       pCurr->DataSeg.cbSeg,
                                       NULL);
                }

                break;
            }
            default:
                AssertMsgFailed(("Invalid transfer type %d\n", pTasks->enmTransferType));
        }

        pCurr->pfnCompleted(pCurr, pCurr->pvUser, rc);
        pdmacFileTaskFree(pEndpoint, pCurr);
    }

    if (pTasks)
    {
        /* Add the rest of the tasks to the pending list */
        pdmacFileAioMgrEpAddTaskList(pEndpoint, pTasks);
    }

    return VINF_SUCCESS;
}

static int pdmacFileAioMgrFailsafeProcessEndpoint(PPDMACEPFILEMGR pAioMgr,
                                                  PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    int rc = VINF_SUCCESS;
    PPDMACTASKFILE pTasks = pEndpoint->AioMgr.pReqsPendingHead;

    pEndpoint->AioMgr.pReqsPendingHead = NULL;
    pEndpoint->AioMgr.pReqsPendingTail = NULL;

    /* Process the request pending list first in case the endpoint was migrated due to an error. */
    if (pTasks)
        rc = pdmacFileAioMgrFailsafeProcessEndpointTaskList(pAioMgr, pEndpoint, pTasks);

    if (RT_SUCCESS(rc))
    {
        pTasks = pdmacFileEpGetNewTasks(pEndpoint);

        if (pTasks)
            rc = pdmacFileAioMgrFailsafeProcessEndpointTaskList(pAioMgr, pEndpoint, pTasks);
    }

    return rc;
}

/**
 * A fallback method in case something goes wrong with the normal
 * I/O manager.
 */
DECLCALLBACK(int) pdmacFileAioMgrFailsafe(RTTHREAD hThreadSelf, void *pvUser)
{
    int             rc      = VINF_SUCCESS;
    PPDMACEPFILEMGR pAioMgr = (PPDMACEPFILEMGR)pvUser;
    NOREF(hThreadSelf);

    while (   (pAioMgr->enmState == PDMACEPFILEMGRSTATE_RUNNING)
           || (pAioMgr->enmState == PDMACEPFILEMGRSTATE_SUSPENDING))
    {
        ASMAtomicWriteBool(&pAioMgr->fWaitingEventSem, true);
        if (!ASMAtomicReadBool(&pAioMgr->fWokenUp))
            rc = RTSemEventWait(pAioMgr->EventSem, pAioMgr->msBwLimitExpired);
        ASMAtomicWriteBool(&pAioMgr->fWaitingEventSem, false);
        Assert(RT_SUCCESS(rc) || rc == VERR_TIMEOUT);

        LogFlow(("Got woken up\n"));
        ASMAtomicWriteBool(&pAioMgr->fWokenUp, false);

        /* Process endpoint events first. */
        PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint = pAioMgr->pEndpointsHead;
        while (pEndpoint)
        {
            pAioMgr->msBwLimitExpired = RT_INDEFINITE_WAIT;
            rc = pdmacFileAioMgrFailsafeProcessEndpoint(pAioMgr, pEndpoint);
            AssertRC(rc);
            pEndpoint = pEndpoint->AioMgr.pEndpointNext;
        }

        /* Now check for an external blocking event. */
        if (pAioMgr->fBlockingEventPending)
        {
            switch (pAioMgr->enmBlockingEvent)
            {
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_ADD_ENDPOINT:
                {
                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointNew = pAioMgr->BlockingEventData.AddEndpoint.pEndpoint;
                    AssertMsg(RT_VALID_PTR(pEndpointNew), ("Adding endpoint event without a endpoint to add\n"));

                    pEndpointNew->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE;

                    pEndpointNew->AioMgr.pEndpointNext = pAioMgr->pEndpointsHead;
                    pEndpointNew->AioMgr.pEndpointPrev = NULL;
                    if (pAioMgr->pEndpointsHead)
                        pAioMgr->pEndpointsHead->AioMgr.pEndpointPrev = pEndpointNew;
                    pAioMgr->pEndpointsHead = pEndpointNew;

                    pAioMgr->cEndpoints++;

                    /*
                     * Process the task list the first time. There might be pending requests
                     * if the endpoint was migrated from another endpoint.
                     */
                    rc = pdmacFileAioMgrFailsafeProcessEndpoint(pAioMgr, pEndpointNew);
                    AssertRC(rc);
                    break;
                }
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_REMOVE_ENDPOINT:
                {
                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointRemove = pAioMgr->BlockingEventData.RemoveEndpoint.pEndpoint;
                    AssertMsg(RT_VALID_PTR(pEndpointRemove), ("Removing endpoint event without a endpoint to remove\n"));

                    pEndpointRemove->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_REMOVING;

                    PPDMASYNCCOMPLETIONENDPOINTFILE pPrev = pEndpointRemove->AioMgr.pEndpointPrev;
                    PPDMASYNCCOMPLETIONENDPOINTFILE pNext = pEndpointRemove->AioMgr.pEndpointNext;

                    if (pPrev)
                        pPrev->AioMgr.pEndpointNext = pNext;
                    else
                        pAioMgr->pEndpointsHead = pNext;

                    if (pNext)
                        pNext->AioMgr.pEndpointPrev = pPrev;

                    pAioMgr->cEndpoints--;
                    break;
                }
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_CLOSE_ENDPOINT:
                {
                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointClose = pAioMgr->BlockingEventData.CloseEndpoint.pEndpoint;
                    AssertMsg(RT_VALID_PTR(pEndpointClose), ("Close endpoint event without a endpoint to Close\n"));

                    pEndpointClose->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_CLOSING;

                    /* Make sure all tasks finished. */
                    rc = pdmacFileAioMgrFailsafeProcessEndpoint(pAioMgr, pEndpointClose);
                    AssertRC(rc);
                    break;
                }
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_SHUTDOWN:
                    pAioMgr->enmState = PDMACEPFILEMGRSTATE_SHUTDOWN;
                    break;
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_SUSPEND:
                    pAioMgr->enmState = PDMACEPFILEMGRSTATE_SUSPENDING;
                    break;
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_RESUME:
                    pAioMgr->enmState = PDMACEPFILEMGRSTATE_RUNNING;
                    break;
                default:
                    AssertMsgFailed(("Invalid event type %d\n", pAioMgr->enmBlockingEvent));
            }

            ASMAtomicWriteBool(&pAioMgr->fBlockingEventPending, false);
            pAioMgr->enmBlockingEvent = PDMACEPFILEAIOMGRBLOCKINGEVENT_INVALID;

            /* Release the waiting thread. */
            rc = RTSemEventSignal(pAioMgr->EventSemBlock);
            AssertRC(rc);
        }
    }

    return rc;
}

