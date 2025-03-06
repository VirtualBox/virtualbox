/* $Id: PDMAsyncCompletionFileNormal.cpp 108651 2025-03-06 11:15:45Z alexander.eichner@oracle.com $ */
/** @file
 * PDM Async I/O - Async File I/O manager.
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
#include <iprt/types.h>
#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <VBox/log.h>

#include "PDMAsyncCompletionFileInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The update period for the I/O load statistics in ms. */
#define PDMACEPFILEMGR_LOAD_UPDATE_PERIOD   1000
/** Maximum number of requests a manager will handle. */
#define PDMACEPFILEMGR_REQS_STEP              64


/*********************************************************************************************************************************
*   Internal functions                                                                                                           *
*********************************************************************************************************************************/
static int pdmacFileAioMgrNormalProcessTaskList(PPDMACTASKFILE pTaskHead,
                                                PPDMACEPFILEMGR pAioMgr,
                                                PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint);

static PPDMACTASKFILE pdmacFileAioMgrNormalRangeLockFree(PPDMACEPFILEMGR pAioMgr,
                                                         PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                                         PPDMACFILERANGELOCK pRangeLock);

static void pdmacFileAioMgrNormalReqCompleteRc(PPDMACEPFILEMGR pAioMgr, RTFILEAIOREQ hReq,
                                               int rc, size_t cbTransfered);


int pdmacFileAioMgrNormalInit(PPDMACEPFILEMGR pAioMgr)
{
    pAioMgr->cRequestsActiveMax = PDMACEPFILEMGR_REQS_STEP;

    int rc = RTFileAioCtxCreate(&pAioMgr->hAioCtx, RTFILEAIO_UNLIMITED_REQS, 0 /* fFlags */);
    if (rc == VERR_OUT_OF_RANGE)
        rc = RTFileAioCtxCreate(&pAioMgr->hAioCtx, pAioMgr->cRequestsActiveMax, 0 /* fFlags */);

    if (RT_SUCCESS(rc))
    {
        /* Initialize request handle array. */
        pAioMgr->iFreeEntry       = 0;
        pAioMgr->cReqEntries      = pAioMgr->cRequestsActiveMax;
        pAioMgr->pahReqsFree      = (RTFILEAIOREQ *)RTMemAllocZ(pAioMgr->cReqEntries * sizeof(RTFILEAIOREQ));

        if (pAioMgr->pahReqsFree)
        {
            /* Create the range lock memcache. */
            rc = RTMemCacheCreate(&pAioMgr->hMemCacheRangeLocks, sizeof(PDMACFILERANGELOCK),
                                  0, UINT32_MAX, NULL, NULL, NULL, 0);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            RTMemFree(pAioMgr->pahReqsFree);
        }
        else
        {
            RTFileAioCtxDestroy(pAioMgr->hAioCtx);
            rc = VERR_NO_MEMORY;
        }
    }

    return rc;
}

void pdmacFileAioMgrNormalDestroy(PPDMACEPFILEMGR pAioMgr)
{
    RTFileAioCtxDestroy(pAioMgr->hAioCtx);

    while (pAioMgr->iFreeEntry > 0)
    {
        pAioMgr->iFreeEntry--;
        Assert(pAioMgr->pahReqsFree[pAioMgr->iFreeEntry] != NIL_RTFILEAIOREQ);
        RTFileAioReqDestroy(pAioMgr->pahReqsFree[pAioMgr->iFreeEntry]);
    }

    RTMemFree(pAioMgr->pahReqsFree);
    RTMemCacheDestroy(pAioMgr->hMemCacheRangeLocks);
}

#if 0 /* currently unused */
/**
 * Sorts the endpoint list with insertion sort.
 */
static void pdmacFileAioMgrNormalEndpointsSortByLoad(PPDMACEPFILEMGR pAioMgr)
{
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpPrev, pEpCurr, pEpNextToSort;

    pEpPrev = pAioMgr->pEndpointsHead;
    pEpCurr = pEpPrev->AioMgr.pEndpointNext;

    while (pEpCurr)
    {
        /* Remember the next element to sort because the list might change. */
        pEpNextToSort = pEpCurr->AioMgr.pEndpointNext;

        /* Unlink the current element from the list. */
        PPDMASYNCCOMPLETIONENDPOINTFILE pPrev = pEpCurr->AioMgr.pEndpointPrev;
        PPDMASYNCCOMPLETIONENDPOINTFILE pNext = pEpCurr->AioMgr.pEndpointNext;

        if (pPrev)
            pPrev->AioMgr.pEndpointNext = pNext;
        else
            pAioMgr->pEndpointsHead = pNext;

        if (pNext)
            pNext->AioMgr.pEndpointPrev = pPrev;

        /* Go back until we reached the place to insert the current endpoint into. */
        while (pEpPrev && (pEpPrev->AioMgr.cReqsPerSec < pEpCurr->AioMgr.cReqsPerSec))
            pEpPrev = pEpPrev->AioMgr.pEndpointPrev;

        /* Link the endpoint into the list. */
        if (pEpPrev)
            pNext = pEpPrev->AioMgr.pEndpointNext;
        else
            pNext = pAioMgr->pEndpointsHead;

        pEpCurr->AioMgr.pEndpointNext = pNext;
        pEpCurr->AioMgr.pEndpointPrev = pEpPrev;

        if (pNext)
            pNext->AioMgr.pEndpointPrev = pEpCurr;

        if (pEpPrev)
            pEpPrev->AioMgr.pEndpointNext = pEpCurr;
        else
            pAioMgr->pEndpointsHead = pEpCurr;

        pEpCurr = pEpNextToSort;
    }

#ifdef DEBUG
    /* Validate sorting algorithm */
    unsigned cEndpoints = 0;
    pEpCurr = pAioMgr->pEndpointsHead;

    AssertMsg(pEpCurr, ("No endpoint in the list?\n"));
    AssertMsg(!pEpCurr->AioMgr.pEndpointPrev, ("First element in the list points to previous element\n"));

    while (pEpCurr)
    {
        cEndpoints++;

        PPDMASYNCCOMPLETIONENDPOINTFILE pNext = pEpCurr->AioMgr.pEndpointNext;
        PPDMASYNCCOMPLETIONENDPOINTFILE pPrev = pEpCurr->AioMgr.pEndpointPrev;

        Assert(!pNext || pNext->AioMgr.cReqsPerSec <= pEpCurr->AioMgr.cReqsPerSec);
        Assert(!pPrev || pPrev->AioMgr.cReqsPerSec >= pEpCurr->AioMgr.cReqsPerSec);

        pEpCurr = pNext;
    }

    AssertMsg(cEndpoints == pAioMgr->cEndpoints, ("Endpoints lost during sort!\n"));

#endif
}
#endif /* currently unused */

/**
 * Removes an endpoint from the currently assigned manager.
 *
 * @returns TRUE if there are still requests pending on the current manager for this endpoint.
 *          FALSE otherwise.
 * @param   pEndpointRemove    The endpoint to remove.
 */
static bool pdmacFileAioMgrNormalRemoveEndpoint(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointRemove)
{
    PPDMASYNCCOMPLETIONENDPOINTFILE pPrev   = pEndpointRemove->AioMgr.pEndpointPrev;
    PPDMASYNCCOMPLETIONENDPOINTFILE pNext   = pEndpointRemove->AioMgr.pEndpointNext;
    PPDMACEPFILEMGR                 pAioMgr = pEndpointRemove->pAioMgr;

    pAioMgr->cEndpoints--;

    if (pPrev)
        pPrev->AioMgr.pEndpointNext = pNext;
    else
        pAioMgr->pEndpointsHead = pNext;

    if (pNext)
        pNext->AioMgr.pEndpointPrev = pPrev;

    /* Make sure that there is no request pending on this manager for the endpoint. */
    if (!pEndpointRemove->AioMgr.cRequestsActive)
    {
        Assert(!pEndpointRemove->pFlushReq);

        /* Reopen the file so that the new endpoint can re-associate with the file */
        RTFileClose(pEndpointRemove->hFile);
        int rc = RTFileOpen(&pEndpointRemove->hFile, pEndpointRemove->Core.pszUri, pEndpointRemove->fFlags);
        AssertRC(rc);
        return false;
    }

    return true;
}

#if 0 /* currently unused */

static bool pdmacFileAioMgrNormalIsBalancePossible(PPDMACEPFILEMGR pAioMgr)
{
    /* Balancing doesn't make sense with only one endpoint. */
    if (pAioMgr->cEndpoints == 1)
        return false;

    /* Doesn't make sens to move endpoints if only one produces the whole load */
    unsigned cEndpointsWithLoad = 0;

    PPDMASYNCCOMPLETIONENDPOINTFILE pCurr = pAioMgr->pEndpointsHead;

    while (pCurr)
    {
        if (pCurr->AioMgr.cReqsPerSec)
            cEndpointsWithLoad++;

        pCurr = pCurr->AioMgr.pEndpointNext;
    }

    return (cEndpointsWithLoad > 1);
}

/**
 * Creates a new I/O manager and spreads the I/O load of the endpoints
 * between the given I/O manager and the new one.
 *
 * @param   pAioMgr    The I/O manager with high I/O load.
 */
static void pdmacFileAioMgrNormalBalanceLoad(PPDMACEPFILEMGR pAioMgr)
{
    /*
     * Check if balancing would improve the situation.
     */
    if (pdmacFileAioMgrNormalIsBalancePossible(pAioMgr))
    {
        PPDMASYNCCOMPLETIONEPCLASSFILE  pEpClassFile = (PPDMASYNCCOMPLETIONEPCLASSFILE)pAioMgr->pEndpointsHead->Core.pEpClass;
        PPDMACEPFILEMGR                 pAioMgrNew = NULL;

        int rc = pdmacFileAioMgrCreate(pEpClassFile, &pAioMgrNew, PDMACEPFILEMGRTYPE_ASYNC);
        if (RT_SUCCESS(rc))
        {
            /* We will sort the list by request count per second. */
            pdmacFileAioMgrNormalEndpointsSortByLoad(pAioMgr);

            /* Now move some endpoints to the new manager. */
            unsigned cReqsHere  = pAioMgr->pEndpointsHead->AioMgr.cReqsPerSec;
            unsigned cReqsOther = 0;
            PPDMASYNCCOMPLETIONENDPOINTFILE pCurr = pAioMgr->pEndpointsHead->AioMgr.pEndpointNext;

            while (pCurr)
            {
                if (cReqsHere <= cReqsOther)
                {
                    /*
                     * The other manager has more requests to handle now.
                     * We will keep the current endpoint.
                     */
                    Log(("Keeping endpoint %#p{%s} with %u reqs/s\n", pCurr->Core.pszUri, pCurr->AioMgr.cReqsPerSec));
                    cReqsHere += pCurr->AioMgr.cReqsPerSec;
                    pCurr = pCurr->AioMgr.pEndpointNext;
                }
                else
                {
                    /* Move to other endpoint. */
                    Log(("Moving endpoint %#p{%s} with %u reqs/s to other manager\n", pCurr, pCurr->Core.pszUri, pCurr->AioMgr.cReqsPerSec));
                    cReqsOther += pCurr->AioMgr.cReqsPerSec;

                    PPDMASYNCCOMPLETIONENDPOINTFILE pMove = pCurr;

                    pCurr = pCurr->AioMgr.pEndpointNext;

                    bool fReqsPending = pdmacFileAioMgrNormalRemoveEndpoint(pMove);

                    if (fReqsPending)
                    {
                        pMove->enmState          = PDMASYNCCOMPLETIONENDPOINTFILESTATE_REMOVING;
                        pMove->AioMgr.fMoving    = true;
                        pMove->AioMgr.pAioMgrDst = pAioMgrNew;
                    }
                    else
                    {
                        pMove->AioMgr.fMoving    = false;
                        pMove->AioMgr.pAioMgrDst = NULL;
                        pdmacFileAioMgrAddEndpoint(pAioMgrNew, pMove);
                    }
                }
            }
        }
        else
        {
            /* Don't process further but leave a log entry about reduced performance. */
            LogRel(("AIOMgr: Could not create new I/O manager (rc=%Rrc). Expect reduced performance\n", rc));
        }
    }
    else
        Log(("AIOMgr: Load balancing would not improve anything\n"));
}

#endif /* unused */

/**
 * Increase the maximum number of active requests for the given I/O manager.
 *
 * @returns VBox status code.
 * @param   pAioMgr    The I/O manager to grow.
 */
static int pdmacFileAioMgrNormalGrow(PPDMACEPFILEMGR pAioMgr)
{
    LogFlowFunc(("pAioMgr=%#p\n", pAioMgr));

    AssertMsg(    pAioMgr->enmState == PDMACEPFILEMGRSTATE_GROWING
              && !pAioMgr->cRequestsActive,
              ("Invalid state of the I/O manager\n"));

#ifdef RT_OS_WINDOWS
    /*
     * Reopen the files of all assigned endpoints first so we can assign them to the new
     * I/O context.
     */
    PPDMASYNCCOMPLETIONENDPOINTFILE pCurr = pAioMgr->pEndpointsHead;

    while (pCurr)
    {
        RTFileClose(pCurr->hFile);
        int rc2 = RTFileOpen(&pCurr->hFile, pCurr->Core.pszUri, pCurr->fFlags); AssertRC(rc2);

        pCurr = pCurr->AioMgr.pEndpointNext;
    }
#endif

    /* Create the new bigger context. */
    pAioMgr->cRequestsActiveMax += PDMACEPFILEMGR_REQS_STEP;

    RTFILEAIOCTX hAioCtxNew = NIL_RTFILEAIOCTX;
    int rc = RTFileAioCtxCreate(&hAioCtxNew, RTFILEAIO_UNLIMITED_REQS, 0 /* fFlags */);
    if (rc == VERR_OUT_OF_RANGE)
        rc = RTFileAioCtxCreate(&hAioCtxNew, pAioMgr->cRequestsActiveMax, 0 /* fFlags */);

    if (RT_SUCCESS(rc))
    {
        /* Close the old context. */
        rc = RTFileAioCtxDestroy(pAioMgr->hAioCtx);
        AssertRC(rc); /** @todo r=bird: Ignoring error code, will propagate. */

        pAioMgr->hAioCtx = hAioCtxNew;

        /* Create a new I/O task handle array */
        uint32_t cReqEntriesNew = pAioMgr->cRequestsActiveMax + 1;
        RTFILEAIOREQ *pahReqNew = (RTFILEAIOREQ *)RTMemAllocZ(cReqEntriesNew * sizeof(RTFILEAIOREQ));

        if (pahReqNew)
        {
            /* Copy the cached request handles. */
            for (uint32_t iReq = 0; iReq < pAioMgr->cReqEntries; iReq++)
                pahReqNew[iReq] = pAioMgr->pahReqsFree[iReq];

            RTMemFree(pAioMgr->pahReqsFree);
            pAioMgr->pahReqsFree = pahReqNew;
            pAioMgr->cReqEntries = cReqEntriesNew;
            LogFlowFunc(("I/O manager increased to handle a maximum of %u requests\n",
                         pAioMgr->cRequestsActiveMax));
        }
        else
            rc = VERR_NO_MEMORY;
    }

#ifdef RT_OS_WINDOWS
    /* Assign the file to the new context. */
    pCurr = pAioMgr->pEndpointsHead;
    while (pCurr)
    {
        rc = RTFileAioCtxAssociateWithFile(pAioMgr->hAioCtx, pCurr->hFile);
        AssertRC(rc); /** @todo r=bird: Ignoring error code, will propagate. */

        pCurr = pCurr->AioMgr.pEndpointNext;
    }
#endif

    if (RT_FAILURE(rc))
    {
        LogFlow(("Increasing size of the I/O manager failed with rc=%Rrc\n", rc));
        pAioMgr->cRequestsActiveMax -= PDMACEPFILEMGR_REQS_STEP;
    }

    pAioMgr->enmState = PDMACEPFILEMGRSTATE_RUNNING;
    LogFlowFunc(("returns rc=%Rrc\n", rc));

    return rc;
}

/**
 * Checks if a given status code is fatal.
 * Non fatal errors can be fixed by migrating the endpoint to a
 * failsafe manager.
 *
 * @returns true If the error is fatal and migrating to a failsafe manager doesn't help
 *          false If the error can be fixed by a migration. (image on NFS disk for example)
 * @param   rcReq    The status code to check.
 */
DECLINLINE(bool) pdmacFileAioMgrNormalRcIsFatal(int rcReq)
{
    return rcReq == VERR_DEV_IO_ERROR
        || rcReq == VERR_FILE_IO_ERROR
        || rcReq == VERR_DISK_IO_ERROR
        || rcReq == VERR_DISK_FULL
        || rcReq == VERR_FILE_TOO_BIG;
}

/**
 * Error handler which will create the failsafe managers and destroy the failed I/O manager.
 *
 * @returns VBox status code
 * @param   pAioMgr     The I/O manager the error occurred on.
 * @param   rc          The error code.
 * @param   SRC_POS     The source location of the error (use RT_SRC_POS).
 */
static int pdmacFileAioMgrNormalErrorHandler(PPDMACEPFILEMGR pAioMgr, int rc, RT_SRC_POS_DECL)
{
    LogRel(("AIOMgr: I/O manager %#p encountered a critical error (rc=%Rrc) during operation. Falling back to failsafe mode. Expect reduced performance\n",
            pAioMgr, rc));
    LogRel(("AIOMgr: Error happened in %s:(%u){%s}\n", RT_SRC_POS_ARGS));
    LogRel(("AIOMgr: Please contact the product vendor\n"));

    PPDMASYNCCOMPLETIONEPCLASSFILE pEpClassFile = (PPDMASYNCCOMPLETIONEPCLASSFILE)pAioMgr->pEndpointsHead->Core.pEpClass;

    pAioMgr->enmState = PDMACEPFILEMGRSTATE_FAULT;
    ASMAtomicWriteU32((volatile uint32_t *)&pEpClassFile->enmMgrTypeOverride, PDMACEPFILEMGRTYPE_SIMPLE);

    AssertMsgFailed(("Implement\n"));
    return VINF_SUCCESS;
}

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
 * Put one task in the pending request list of an endpoint.
 */
DECLINLINE(void) pdmacFileAioMgrEpAddTask(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint, PPDMACTASKFILE pTask)
{
    /* Add the rest of the tasks to the pending list */
    if (!pEndpoint->AioMgr.pReqsPendingHead)
    {
        Assert(!pEndpoint->AioMgr.pReqsPendingTail);
        pEndpoint->AioMgr.pReqsPendingHead = pTask;
    }
    else
    {
        Assert(pEndpoint->AioMgr.pReqsPendingTail);
        pEndpoint->AioMgr.pReqsPendingTail->pNext = pTask;
    }

    pEndpoint->AioMgr.pReqsPendingTail = pTask;
    pTask->pNext = NULL;
}

/**
 * Allocates a async I/O request.
 *
 * @returns Handle to the request.
 * @param   pAioMgr    The I/O manager.
 */
static RTFILEAIOREQ pdmacFileAioMgrNormalRequestAlloc(PPDMACEPFILEMGR pAioMgr)
{
    /* Get a request handle. */
    RTFILEAIOREQ hReq;
    if (pAioMgr->iFreeEntry > 0)
    {
        pAioMgr->iFreeEntry--;
        hReq = pAioMgr->pahReqsFree[pAioMgr->iFreeEntry];
        pAioMgr->pahReqsFree[pAioMgr->iFreeEntry] = NIL_RTFILEAIOREQ;
        Assert(hReq != NIL_RTFILEAIOREQ);
    }
    else
    {
        int rc = RTFileAioReqCreate(&hReq);
        AssertRCReturn(rc, NIL_RTFILEAIOREQ);
    }

    return hReq;
}

/**
 * Frees a async I/O request handle.
 *
 * @param   pAioMgr    The I/O manager.
 * @param   hReq       The I/O request handle to free.
 */
static void pdmacFileAioMgrNormalRequestFree(PPDMACEPFILEMGR pAioMgr, RTFILEAIOREQ hReq)
{
    Assert(pAioMgr->iFreeEntry < pAioMgr->cReqEntries);
    Assert(pAioMgr->pahReqsFree[pAioMgr->iFreeEntry] == NIL_RTFILEAIOREQ);

    pAioMgr->pahReqsFree[pAioMgr->iFreeEntry] = hReq;
    pAioMgr->iFreeEntry++;
}

/**
 * Wrapper around RTFIleAioCtxSubmit() which is also doing error handling.
 */
static void pdmacFileAioMgrNormalReqsEnqueue(PPDMACEPFILEMGR pAioMgr,
                                             PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                             PRTFILEAIOREQ pahReqs, unsigned cReqs)
{
    pAioMgr->cRequestsActive += cReqs;
    pEndpoint->AioMgr.cRequestsActive += cReqs;

    LogFlow(("Enqueuing %d requests. I/O manager has a total of %d active requests now\n", cReqs, pAioMgr->cRequestsActive));
    LogFlow(("Endpoint has a total of %d active requests now\n", pEndpoint->AioMgr.cRequestsActive));

    int rc = RTFileAioCtxSubmit(pAioMgr->hAioCtx, pahReqs, cReqs);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_FILE_AIO_INSUFFICIENT_RESSOURCES)
        {
            PPDMASYNCCOMPLETIONEPCLASSFILE pEpClass = (PPDMASYNCCOMPLETIONEPCLASSFILE)pEndpoint->Core.pEpClass;

            /* Append any not submitted task to the waiting list. */
            for (size_t i = 0; i < cReqs; i++)
            {
                int rcReq = RTFileAioReqGetRC(pahReqs[i], NULL);

                if (rcReq != VERR_FILE_AIO_IN_PROGRESS)
                {
                    PPDMACTASKFILE pTask = (PPDMACTASKFILE)RTFileAioReqGetUser(pahReqs[i]);

                    Assert(pTask->hReq == pahReqs[i]);
                    pdmacFileAioMgrEpAddTask(pEndpoint, pTask);
                    pAioMgr->cRequestsActive--;
                    pEndpoint->AioMgr.cRequestsActive--;

                    if (pTask->enmTransferType == PDMACTASKFILETRANSFER_FLUSH)
                    {
                        /* Clear the pending flush */
                        Assert(pEndpoint->pFlushReq == pTask);
                        pEndpoint->pFlushReq = NULL;
                    }
                }
            }

            pAioMgr->cRequestsActiveMax = pAioMgr->cRequestsActive;

            /* Print an entry in the release log */
            if (RT_UNLIKELY(!pEpClass->fOutOfResourcesWarningPrinted))
            {
                pEpClass->fOutOfResourcesWarningPrinted = true;
                LogRel(("AIOMgr: Host limits number of active IO requests to %u. Expect a performance impact.\n",
                        pAioMgr->cRequestsActive));
            }

            LogFlow(("Removed requests. I/O manager has a total of %u active requests now\n", pAioMgr->cRequestsActive));
            LogFlow(("Endpoint has a total of %u active requests now\n", pEndpoint->AioMgr.cRequestsActive));
            rc = VINF_SUCCESS;
        }
        else /* Another kind of error happened (full disk, ...) */
        {
            /* An error happened. Find out which one caused the error and resubmit all other tasks. */
            for (size_t i = 0; i < cReqs; i++)
            {
                int rcReq = RTFileAioReqGetRC(pahReqs[i], NULL);

                if (rcReq == VERR_FILE_AIO_NOT_SUBMITTED)
                {
                    /* We call ourself again to do any error handling which might come up now. */
                    pdmacFileAioMgrNormalReqsEnqueue(pAioMgr, pEndpoint, &pahReqs[i], 1);
                }
                else if (rcReq != VERR_FILE_AIO_IN_PROGRESS)
                    pdmacFileAioMgrNormalReqCompleteRc(pAioMgr, pahReqs[i], rcReq, 0);
            }


            if (    pEndpoint->pFlushReq
                && !pAioMgr->cRequestsActive
                && !pEndpoint->fAsyncFlushSupported)
            {
                /*
                 * Complete a pending flush if we don't have requests enqueued and the host doesn't support
                 * the async flush API.
                 * Happens only if this we just noticed that this is not supported
                 * and the only active request was a flush.
                 */
                PPDMACTASKFILE pFlush = pEndpoint->pFlushReq;
                pEndpoint->pFlushReq = NULL;
                pFlush->pfnCompleted(pFlush, pFlush->pvUser, VINF_SUCCESS);
                pdmacFileTaskFree(pEndpoint, pFlush);
            }
        }
    }
}

static bool pdmacFileAioMgrNormalIsRangeLocked(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                               RTFOFF offStart, size_t cbRange,
                                               PPDMACTASKFILE pTask, bool fAlignedReq)
{
    AssertMsg(   pTask->enmTransferType == PDMACTASKFILETRANSFER_WRITE
              || pTask->enmTransferType == PDMACTASKFILETRANSFER_READ,
                 ("Invalid task type %d\n", pTask->enmTransferType));

    /*
     * If there is no unaligned request active and the current one is aligned
     * just pass it through.
     */
    if (!pEndpoint->AioMgr.cLockedReqsActive && fAlignedReq)
        return false;

    PPDMACFILERANGELOCK pRangeLock;
    pRangeLock = (PPDMACFILERANGELOCK)RTAvlrFileOffsetRangeGet(pEndpoint->AioMgr.pTreeRangesLocked, offStart);
    if (!pRangeLock)
    {
        pRangeLock = (PPDMACFILERANGELOCK)RTAvlrFileOffsetGetBestFit(pEndpoint->AioMgr.pTreeRangesLocked, offStart, true);
        /* Check if we intersect with the range. */
        if (   !pRangeLock
            || !(   (pRangeLock->Core.Key) <= (offStart + (RTFOFF)cbRange - 1)
                && (pRangeLock->Core.KeyLast) >= offStart))
        {
            pRangeLock = NULL; /* False alarm */
        }
    }

    /* Check whether we have one of the situations explained below */
    if (pRangeLock)
    {
        /* Add to the list. */
        pTask->pNext = NULL;

        if (!pRangeLock->pWaitingTasksHead)
        {
            Assert(!pRangeLock->pWaitingTasksTail);
            pRangeLock->pWaitingTasksHead = pTask;
            pRangeLock->pWaitingTasksTail = pTask;
        }
        else
        {
            AssertPtr(pRangeLock->pWaitingTasksTail);
            pRangeLock->pWaitingTasksTail->pNext = pTask;
            pRangeLock->pWaitingTasksTail = pTask;
        }
        return true;
    }

    return false;
}

static int pdmacFileAioMgrNormalRangeLock(PPDMACEPFILEMGR pAioMgr,
                                          PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                          RTFOFF offStart, size_t cbRange,
                                          PPDMACTASKFILE pTask, bool fAlignedReq)
{
    LogFlowFunc(("pAioMgr=%#p pEndpoint=%#p offStart=%RTfoff cbRange=%zu pTask=%#p\n",
                 pAioMgr, pEndpoint, offStart, cbRange, pTask));

    AssertMsg(!pdmacFileAioMgrNormalIsRangeLocked(pEndpoint, offStart, cbRange, pTask, fAlignedReq),
              ("Range is already locked offStart=%RTfoff cbRange=%u\n",
               offStart, cbRange));

    /*
     * If there is no unaligned request active and the current one is aligned
     * just don't use the lock.
     */
    if (!pEndpoint->AioMgr.cLockedReqsActive && fAlignedReq)
    {
        pTask->pRangeLock = NULL;
        return VINF_SUCCESS;
    }

    PPDMACFILERANGELOCK pRangeLock = (PPDMACFILERANGELOCK)RTMemCacheAlloc(pAioMgr->hMemCacheRangeLocks);
    if (!pRangeLock)
        return VERR_NO_MEMORY;

    /* Init the lock. */
    pRangeLock->Core.Key          = offStart;
    pRangeLock->Core.KeyLast      = offStart + cbRange - 1;
    pRangeLock->cRefs             = 1;
    pRangeLock->fReadLock         = pTask->enmTransferType == PDMACTASKFILETRANSFER_READ;
    pRangeLock->pWaitingTasksHead = NULL;
    pRangeLock->pWaitingTasksTail = NULL;

    bool fInserted = RTAvlrFileOffsetInsert(pEndpoint->AioMgr.pTreeRangesLocked, &pRangeLock->Core);
    AssertMsg(fInserted, ("Range lock was not inserted!\n")); NOREF(fInserted);

    /* Let the task point to its lock. */
    pTask->pRangeLock = pRangeLock;
    pEndpoint->AioMgr.cLockedReqsActive++;

    return VINF_SUCCESS;
}

static PPDMACTASKFILE pdmacFileAioMgrNormalRangeLockFree(PPDMACEPFILEMGR pAioMgr,
                                                         PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                                         PPDMACFILERANGELOCK pRangeLock)
{
    PPDMACTASKFILE pTasksWaitingHead;

    LogFlowFunc(("pAioMgr=%#p pEndpoint=%#p pRangeLock=%#p\n",
                 pAioMgr, pEndpoint, pRangeLock));

    /* pRangeLock can be NULL if there was no lock assigned with the task. */
    if (!pRangeLock)
        return NULL;

    Assert(pRangeLock->cRefs == 1);

    RTAvlrFileOffsetRemove(pEndpoint->AioMgr.pTreeRangesLocked, pRangeLock->Core.Key);
    pTasksWaitingHead = pRangeLock->pWaitingTasksHead;
    pRangeLock->pWaitingTasksHead = NULL;
    pRangeLock->pWaitingTasksTail = NULL;
    RTMemCacheFree(pAioMgr->hMemCacheRangeLocks, pRangeLock);
    pEndpoint->AioMgr.cLockedReqsActive--;

    return pTasksWaitingHead;
}

static int pdmacFileAioMgrNormalTaskPrepareBuffered(PPDMACEPFILEMGR pAioMgr,
                                                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                                    PPDMACTASKFILE pTask, PRTFILEAIOREQ phReq)
{
    AssertMsg(   pTask->enmTransferType == PDMACTASKFILETRANSFER_WRITE
              || (uint64_t)(pTask->Off + pTask->DataSeg.cbSeg) <= pEndpoint->cbFile,
              ("Read exceeds file size offStart=%RTfoff cbToTransfer=%d cbFile=%llu\n",
               pTask->Off, pTask->DataSeg.cbSeg, pEndpoint->cbFile));

    pTask->fPrefetch = false;
    pTask->cbBounceBuffer = 0;

    /*
     * Before we start to setup the request we have to check whether there is a task
     * already active which range intersects with ours. We have to defer execution
     * of this task in two cases:
     *     - The pending task is a write and the current is either read or write
     *     - The pending task is a read and the current task is a write task.
     *
     * To check whether a range is currently "locked" we use the AVL tree where every pending task
     * is stored by its file offset range. The current task will be added to the active task
     * and will be executed when the active one completes. (The method below
     * which checks whether a range is already used will add the task)
     *
     * This is necessary because of the requirement to align all requests to a 512 boundary
     * which is enforced by the host OS (Linux and Windows atm). It is possible that
     * we have to process unaligned tasks and need to align them using bounce buffers.
     * While the data is fetched from the file another request might arrive writing to
     * the same range. This will result in data corruption if both are executed concurrently.
     */
    int  rc = VINF_SUCCESS;
    bool fLocked = pdmacFileAioMgrNormalIsRangeLocked(pEndpoint, pTask->Off, pTask->DataSeg.cbSeg, pTask,
                                                      true /* fAlignedReq */);
    if (!fLocked)
    {
        /* Get a request handle. */
        RTFILEAIOREQ hReq = pdmacFileAioMgrNormalRequestAlloc(pAioMgr);
        AssertMsg(hReq != NIL_RTFILEAIOREQ, ("Out of request handles\n"));

        if (pTask->enmTransferType == PDMACTASKFILETRANSFER_WRITE)
        {
            /* Grow the file if needed. */
            if (RT_UNLIKELY((uint64_t)(pTask->Off + pTask->DataSeg.cbSeg) > pEndpoint->cbFile))
            {
                ASMAtomicWriteU64(&pEndpoint->cbFile, pTask->Off + pTask->DataSeg.cbSeg);
                RTFileSetSize(pEndpoint->hFile, pTask->Off + pTask->DataSeg.cbSeg);
            }

            rc = RTFileAioReqPrepareWrite(hReq, pEndpoint->hFile,
                                          pTask->Off, pTask->DataSeg.pvSeg,
                                          pTask->DataSeg.cbSeg, pTask);
        }
        else
            rc = RTFileAioReqPrepareRead(hReq, pEndpoint->hFile,
                                         pTask->Off, pTask->DataSeg.pvSeg,
                                         pTask->DataSeg.cbSeg, pTask);
        AssertRC(rc);

        rc = pdmacFileAioMgrNormalRangeLock(pAioMgr, pEndpoint, pTask->Off,
                                            pTask->DataSeg.cbSeg,
                                            pTask, true /* fAlignedReq */);

        if (RT_SUCCESS(rc))
        {
            pTask->hReq = hReq;
            *phReq = hReq;
        }
    }
    else
        LogFlow(("Task %#p was deferred because the access range is locked\n", pTask));

    return rc;
}

static int pdmacFileAioMgrNormalTaskPrepareNonBuffered(PPDMACEPFILEMGR pAioMgr,
                                                       PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                                       PPDMACTASKFILE pTask, PRTFILEAIOREQ phReq)
{
    /*
     * Check if the alignment requirements are met.
     * Offset, transfer size and buffer address
     * need to be on a 512 boundary.
     */
    RTFOFF offStart = pTask->Off & ~(RTFOFF)(512-1);
    size_t cbToTransfer = RT_ALIGN_Z(pTask->DataSeg.cbSeg + (pTask->Off - offStart), 512);
    PDMACTASKFILETRANSFER enmTransferType = pTask->enmTransferType;
    bool fAlignedReq =     cbToTransfer == pTask->DataSeg.cbSeg
                        && offStart == pTask->Off;

    AssertMsg(   pTask->enmTransferType == PDMACTASKFILETRANSFER_WRITE
                || (uint64_t)(offStart + cbToTransfer) <= pEndpoint->cbFile,
                ("Read exceeds file size offStart=%RTfoff cbToTransfer=%d cbFile=%llu\n",
                offStart, cbToTransfer, pEndpoint->cbFile));

    pTask->fPrefetch = false;

    /*
     * Before we start to setup the request we have to check whether there is a task
     * already active which range intersects with ours. We have to defer execution
     * of this task in two cases:
     *     - The pending task is a write and the current is either read or write
     *     - The pending task is a read and the current task is a write task.
     *
     * To check whether a range is currently "locked" we use the AVL tree where every pending task
     * is stored by its file offset range. The current task will be added to the active task
     * and will be executed when the active one completes. (The method below
     * which checks whether a range is already used will add the task)
     *
     * This is necessary because of the requirement to align all requests to a 512 boundary
     * which is enforced by the host OS (Linux and Windows atm). It is possible that
     * we have to process unaligned tasks and need to align them using bounce buffers.
     * While the data is fetched from the file another request might arrive writing to
     * the same range. This will result in data corruption if both are executed concurrently.
     */
    int  rc = VINF_SUCCESS;
    bool fLocked = pdmacFileAioMgrNormalIsRangeLocked(pEndpoint, offStart, cbToTransfer, pTask, fAlignedReq);
    if (!fLocked)
    {
        PPDMASYNCCOMPLETIONEPCLASSFILE  pEpClassFile = (PPDMASYNCCOMPLETIONEPCLASSFILE)pEndpoint->Core.pEpClass;
        void                           *pvBuf        = pTask->DataSeg.pvSeg;

        /* Get a request handle. */
        RTFILEAIOREQ hReq = pdmacFileAioMgrNormalRequestAlloc(pAioMgr);
        AssertMsg(hReq != NIL_RTFILEAIOREQ, ("Out of request handles\n"));

        if (   !fAlignedReq
            || ((pEpClassFile->uBitmaskAlignment & (RTR3UINTPTR)pvBuf) != (RTR3UINTPTR)pvBuf))
        {
            LogFlow(("Using bounce buffer for task %#p cbToTransfer=%zd cbSeg=%zd offStart=%RTfoff off=%RTfoff\n",
                     pTask, cbToTransfer, pTask->DataSeg.cbSeg, offStart, pTask->Off));

            /* Create bounce buffer. */
            pTask->cbBounceBuffer = cbToTransfer;

            AssertMsg(pTask->Off >= offStart, ("Overflow in calculation Off=%llu offStart=%llu\n",
                      pTask->Off, offStart));
            pTask->offBounceBuffer = pTask->Off - offStart;

            /** @todo I think we need something like a RTMemAllocAligned method here.
             * Current assumption is that the maximum alignment is 4096byte
             * (GPT disk on Windows)
             * so we can use RTMemPageAlloc here.
             */
            pTask->pvBounceBuffer = RTMemPageAlloc(cbToTransfer);
            if (RT_LIKELY(pTask->pvBounceBuffer))
            {
                pvBuf = pTask->pvBounceBuffer;

                if (pTask->enmTransferType == PDMACTASKFILETRANSFER_WRITE)
                {
                    if (   RT_UNLIKELY(cbToTransfer != pTask->DataSeg.cbSeg)
                        || RT_UNLIKELY(offStart != pTask->Off))
                    {
                        /* We have to fill the buffer first before we can update the data. */
                        LogFlow(("Prefetching data for task %#p\n", pTask));
                        pTask->fPrefetch = true;
                        enmTransferType = PDMACTASKFILETRANSFER_READ;
                    }
                    else
                        memcpy(pvBuf, pTask->DataSeg.pvSeg, pTask->DataSeg.cbSeg);
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            pTask->cbBounceBuffer = 0;

        if (RT_SUCCESS(rc))
        {
            AssertMsg((pEpClassFile->uBitmaskAlignment & (RTR3UINTPTR)pvBuf) == (RTR3UINTPTR)pvBuf,
                      ("AIO: Alignment restrictions not met! pvBuf=%p uBitmaskAlignment=%p\n", pvBuf, pEpClassFile->uBitmaskAlignment));

            if (enmTransferType == PDMACTASKFILETRANSFER_WRITE)
            {
                /* Grow the file if needed. */
                if (RT_UNLIKELY((uint64_t)(pTask->Off + pTask->DataSeg.cbSeg) > pEndpoint->cbFile))
                {
                    ASMAtomicWriteU64(&pEndpoint->cbFile, pTask->Off + pTask->DataSeg.cbSeg);
                    RTFileSetSize(pEndpoint->hFile, pTask->Off + pTask->DataSeg.cbSeg);
                }

                rc = RTFileAioReqPrepareWrite(hReq, pEndpoint->hFile,
                                              offStart, pvBuf, cbToTransfer, pTask);
            }
            else
                rc = RTFileAioReqPrepareRead(hReq, pEndpoint->hFile,
                                             offStart, pvBuf, cbToTransfer, pTask);
            AssertRC(rc);

            rc = pdmacFileAioMgrNormalRangeLock(pAioMgr, pEndpoint, offStart, cbToTransfer, pTask, fAlignedReq);
            if (RT_SUCCESS(rc))
            {
                pTask->hReq = hReq;
                *phReq = hReq;
            }
            else
            {
                /* Cleanup */
                if (pTask->cbBounceBuffer)
                    RTMemPageFree(pTask->pvBounceBuffer, pTask->cbBounceBuffer);
            }
        }
    }
    else
        LogFlow(("Task %#p was deferred because the access range is locked\n", pTask));

    return rc;
}

static int pdmacFileAioMgrNormalProcessTaskList(PPDMACTASKFILE pTaskHead,
                                                PPDMACEPFILEMGR pAioMgr,
                                                PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    RTFILEAIOREQ  apReqs[20];
    unsigned      cRequests = 0;
    int           rc        = VINF_SUCCESS;

    AssertMsg(pEndpoint->enmState == PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE,
              ("Trying to process request lists of a non active endpoint!\n"));

    /* Go through the list and queue the requests until we get a flush request */
    while (   pTaskHead
           && !pEndpoint->pFlushReq
           && (pAioMgr->cRequestsActive + cRequests < pAioMgr->cRequestsActiveMax)
           && RT_SUCCESS(rc))
    {
        RTMSINTERVAL msWhenNext;
        PPDMACTASKFILE pCurr = pTaskHead;

        if (!pdmacEpIsTransferAllowed(&pEndpoint->Core, pCurr->DataSeg.cbSeg, &msWhenNext))
        {
            pAioMgr->msBwLimitExpired = RT_MIN(pAioMgr->msBwLimitExpired, msWhenNext);
            break;
        }

        pTaskHead = pTaskHead->pNext;

        pCurr->pNext = NULL;

        AssertMsg(RT_VALID_PTR(pCurr->pEndpoint) && pCurr->pEndpoint == pEndpoint,
                  ("Endpoints do not match\n"));

        switch (pCurr->enmTransferType)
        {
            case PDMACTASKFILETRANSFER_FLUSH:
            {
                /* If there is no data transfer request this flush request finished immediately. */
                if (pEndpoint->fAsyncFlushSupported)
                {
                    /* Issue a flush to the host. */
                    RTFILEAIOREQ hReq = pdmacFileAioMgrNormalRequestAlloc(pAioMgr);
                    AssertMsg(hReq != NIL_RTFILEAIOREQ, ("Out of request handles\n"));

                    LogFlow(("Flush request %#p\n", hReq));

                    rc = RTFileAioReqPrepareFlush(hReq, pEndpoint->hFile, pCurr);
                    if (RT_FAILURE(rc))
                    {
                        if (rc == VERR_NOT_SUPPORTED)
                            LogRel(("AIOMgr: Async flushes not supported\n"));
                        else
                            LogRel(("AIOMgr: Preparing flush failed with %Rrc, disabling async flushes\n", rc));
                        pEndpoint->fAsyncFlushSupported = false;
                        pdmacFileAioMgrNormalRequestFree(pAioMgr, hReq);
                        rc = VINF_SUCCESS; /* Fake success */
                    }
                    else
                    {
                        pCurr->hReq = hReq;
                        apReqs[cRequests] = hReq;
                        pEndpoint->AioMgr.cReqsProcessed++;
                        cRequests++;
                    }
                }

                if (   !pEndpoint->AioMgr.cRequestsActive
                    && !pEndpoint->fAsyncFlushSupported)
                {
                    pCurr->pfnCompleted(pCurr, pCurr->pvUser, VINF_SUCCESS);
                    pdmacFileTaskFree(pEndpoint, pCurr);
                }
                else
                {
                    Assert(!pEndpoint->pFlushReq);
                    pEndpoint->pFlushReq = pCurr;
                }
                break;
            }
            case PDMACTASKFILETRANSFER_READ:
            case PDMACTASKFILETRANSFER_WRITE:
            {
                RTFILEAIOREQ hReq = NIL_RTFILEAIOREQ;

                if (pCurr->hReq == NIL_RTFILEAIOREQ)
                {
                    if (pEndpoint->enmBackendType == PDMACFILEEPBACKEND_BUFFERED)
                        rc = pdmacFileAioMgrNormalTaskPrepareBuffered(pAioMgr, pEndpoint, pCurr, &hReq);
                    else if (pEndpoint->enmBackendType == PDMACFILEEPBACKEND_NON_BUFFERED)
                        rc = pdmacFileAioMgrNormalTaskPrepareNonBuffered(pAioMgr, pEndpoint, pCurr, &hReq);
                    else
                        AssertMsgFailed(("Invalid backend type %d\n", pEndpoint->enmBackendType));

                    AssertRC(rc);
                }
                else
                {
                    LogFlow(("Task %#p has I/O request %#p already\n", pCurr, pCurr->hReq));
                    hReq = pCurr->hReq;
                }

                LogFlow(("Read/Write request %#p\n", hReq));

                if (hReq != NIL_RTFILEAIOREQ)
                {
                    apReqs[cRequests] = hReq;
                    cRequests++;
                }
                break;
            }
            default:
                AssertMsgFailed(("Invalid transfer type %d\n", pCurr->enmTransferType));
        } /* switch transfer type */

        /* Queue the requests if the array is full. */
        if (cRequests == RT_ELEMENTS(apReqs))
        {
            pdmacFileAioMgrNormalReqsEnqueue(pAioMgr, pEndpoint, apReqs, cRequests);
            cRequests = 0;
        }
    }

    if (cRequests)
        pdmacFileAioMgrNormalReqsEnqueue(pAioMgr, pEndpoint, apReqs, cRequests);

    if (pTaskHead)
    {
        /* Add the rest of the tasks to the pending list */
        pdmacFileAioMgrEpAddTaskList(pEndpoint, pTaskHead);

        if (RT_UNLIKELY(   pAioMgr->cRequestsActiveMax == pAioMgr->cRequestsActive
                        && !pEndpoint->pFlushReq))
        {
#if 0
            /*
             * The I/O manager has no room left for more requests
             * but there are still requests to process.
             * Create a new I/O manager and let it handle some endpoints.
             */
            pdmacFileAioMgrNormalBalanceLoad(pAioMgr);
#else
            /* Grow the I/O manager */
            pAioMgr->enmState = PDMACEPFILEMGRSTATE_GROWING;
#endif
        }
    }

    /* Insufficient resources are not fatal. */
    if (rc == VERR_FILE_AIO_INSUFFICIENT_RESSOURCES)
        rc = VINF_SUCCESS;

    return rc;
}

/**
 * Adds all pending requests for the given endpoint
 * until a flush request is encountered or there is no
 * request anymore.
 *
 * @returns VBox status code.
 * @param   pAioMgr    The async I/O manager for the endpoint
 * @param   pEndpoint  The endpoint to get the requests from.
 */
static int pdmacFileAioMgrNormalQueueReqs(PPDMACEPFILEMGR pAioMgr,
                                          PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    int rc = VINF_SUCCESS;
    PPDMACTASKFILE pTasksHead = NULL;

    AssertMsg(pEndpoint->enmState == PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE,
              ("Trying to process request lists of a non active endpoint!\n"));

    Assert(!pEndpoint->pFlushReq);

    /* Check the pending list first */
    if (pEndpoint->AioMgr.pReqsPendingHead)
    {
        LogFlow(("Queuing pending requests first\n"));

        pTasksHead = pEndpoint->AioMgr.pReqsPendingHead;
        /*
         * Clear the list as the processing routine will insert them into the list
         * again if it gets a flush request.
         */
        pEndpoint->AioMgr.pReqsPendingHead = NULL;
        pEndpoint->AioMgr.pReqsPendingTail = NULL;
        rc = pdmacFileAioMgrNormalProcessTaskList(pTasksHead, pAioMgr, pEndpoint);
        AssertRC(rc);                   /** @todo r=bird: status code potentially overwritten.  */
    }

    if (!pEndpoint->pFlushReq && !pEndpoint->AioMgr.pReqsPendingHead)
    {
        /* Now the request queue. */
        pTasksHead = pdmacFileEpGetNewTasks(pEndpoint);
        if (pTasksHead)
        {
            rc = pdmacFileAioMgrNormalProcessTaskList(pTasksHead, pAioMgr, pEndpoint);
            AssertRC(rc);
        }
    }

    return rc;
}

static int pdmacFileAioMgrNormalProcessBlockingEvent(PPDMACEPFILEMGR pAioMgr)
{
    int rc = VINF_SUCCESS;
    bool fNotifyWaiter = false;

    LogFlowFunc((": Enter\n"));

    Assert(pAioMgr->fBlockingEventPending);

    switch (pAioMgr->enmBlockingEvent)
    {
        case PDMACEPFILEAIOMGRBLOCKINGEVENT_ADD_ENDPOINT:
        {
            PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointNew = ASMAtomicReadPtrT(&pAioMgr->BlockingEventData.AddEndpoint.pEndpoint, PPDMASYNCCOMPLETIONENDPOINTFILE);
            AssertMsg(RT_VALID_PTR(pEndpointNew), ("Adding endpoint event without a endpoint to add\n"));

            pEndpointNew->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE;

            pEndpointNew->AioMgr.pEndpointNext = pAioMgr->pEndpointsHead;
            pEndpointNew->AioMgr.pEndpointPrev = NULL;
            if (pAioMgr->pEndpointsHead)
                pAioMgr->pEndpointsHead->AioMgr.pEndpointPrev = pEndpointNew;
            pAioMgr->pEndpointsHead = pEndpointNew;

            /* Assign the completion point to this file. */
            rc = RTFileAioCtxAssociateWithFile(pAioMgr->hAioCtx, pEndpointNew->hFile);
            fNotifyWaiter = true;
            pAioMgr->cEndpoints++;
            break;
        }
        case PDMACEPFILEAIOMGRBLOCKINGEVENT_REMOVE_ENDPOINT:
        {
            PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointRemove = ASMAtomicReadPtrT(&pAioMgr->BlockingEventData.RemoveEndpoint.pEndpoint, PPDMASYNCCOMPLETIONENDPOINTFILE);
            AssertMsg(RT_VALID_PTR(pEndpointRemove), ("Removing endpoint event without a endpoint to remove\n"));

            pEndpointRemove->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_REMOVING;
            fNotifyWaiter = !pdmacFileAioMgrNormalRemoveEndpoint(pEndpointRemove);
            break;
        }
        case PDMACEPFILEAIOMGRBLOCKINGEVENT_CLOSE_ENDPOINT:
        {
            PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointClose = ASMAtomicReadPtrT(&pAioMgr->BlockingEventData.CloseEndpoint.pEndpoint, PPDMASYNCCOMPLETIONENDPOINTFILE);
            AssertMsg(RT_VALID_PTR(pEndpointClose), ("Close endpoint event without a endpoint to close\n"));

            if (pEndpointClose->enmState == PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE)
            {
                LogFlowFunc((": Closing endpoint %#p{%s}\n", pEndpointClose, pEndpointClose->Core.pszUri));

                /* Make sure all tasks finished. Process the queues a last time first. */
                rc = pdmacFileAioMgrNormalQueueReqs(pAioMgr, pEndpointClose);
                AssertRC(rc);

                pEndpointClose->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_CLOSING;
                fNotifyWaiter = !pdmacFileAioMgrNormalRemoveEndpoint(pEndpointClose);
            }
            else if (   (pEndpointClose->enmState == PDMASYNCCOMPLETIONENDPOINTFILESTATE_CLOSING)
                     && (!pEndpointClose->AioMgr.cRequestsActive))
                fNotifyWaiter = true;
            break;
        }
        case PDMACEPFILEAIOMGRBLOCKINGEVENT_SHUTDOWN:
        {
            pAioMgr->enmState = PDMACEPFILEMGRSTATE_SHUTDOWN;
            if (!pAioMgr->cRequestsActive)
                fNotifyWaiter = true;
            break;
        }
        case PDMACEPFILEAIOMGRBLOCKINGEVENT_SUSPEND:
        {
            pAioMgr->enmState = PDMACEPFILEMGRSTATE_SUSPENDING;
            break;
        }
        case PDMACEPFILEAIOMGRBLOCKINGEVENT_RESUME:
        {
            pAioMgr->enmState = PDMACEPFILEMGRSTATE_RUNNING;
            fNotifyWaiter = true;
            break;
        }
        default:
            AssertReleaseMsgFailed(("Invalid event type %d\n", pAioMgr->enmBlockingEvent));
    }

    if (fNotifyWaiter)
    {
        ASMAtomicWriteBool(&pAioMgr->fBlockingEventPending, false);
        pAioMgr->enmBlockingEvent = PDMACEPFILEAIOMGRBLOCKINGEVENT_INVALID;

        /* Release the waiting thread. */
        LogFlow(("Signalling waiter\n"));
        rc = RTSemEventSignal(pAioMgr->EventSemBlock);
        AssertRC(rc);
    }

    LogFlowFunc((": Leave\n"));
    return rc;
}

/**
 * Checks all endpoints for pending events or new requests.
 *
 * @returns VBox status code.
 * @param   pAioMgr    The I/O manager handle.
 */
static int pdmacFileAioMgrNormalCheckEndpoints(PPDMACEPFILEMGR pAioMgr)
{
    /* Check the assigned endpoints for new tasks if there isn't a flush request active at the moment. */
    int rc = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint = pAioMgr->pEndpointsHead;

    pAioMgr->msBwLimitExpired = RT_INDEFINITE_WAIT;

    while (pEndpoint)
    {
        if (!pEndpoint->pFlushReq
            && (pEndpoint->enmState == PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE)
            && !pEndpoint->AioMgr.fMoving)
        {
            rc = pdmacFileAioMgrNormalQueueReqs(pAioMgr, pEndpoint);
            if (RT_FAILURE(rc))
                return rc;
        }
        else if (   !pEndpoint->AioMgr.cRequestsActive
                 && pEndpoint->enmState != PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE)
        {
            /* Reopen the file so that the new endpoint can re-associate with the file */
            RTFileClose(pEndpoint->hFile);
            rc = RTFileOpen(&pEndpoint->hFile, pEndpoint->Core.pszUri, pEndpoint->fFlags);
            AssertRC(rc);

            if (pEndpoint->AioMgr.fMoving)
            {
                pEndpoint->AioMgr.fMoving = false;
                pdmacFileAioMgrAddEndpoint(pEndpoint->AioMgr.pAioMgrDst, pEndpoint);
            }
            else
            {
                Assert(pAioMgr->fBlockingEventPending);
                ASMAtomicWriteBool(&pAioMgr->fBlockingEventPending, false);

                /* Release the waiting thread. */
                LogFlow(("Signalling waiter\n"));
                rc = RTSemEventSignal(pAioMgr->EventSemBlock);
                AssertRC(rc);
            }
        }

        pEndpoint = pEndpoint->AioMgr.pEndpointNext;
    }

    return rc;
}

/**
 * Wrapper around pdmacFileAioMgrNormalReqCompleteRc().
 */
static void pdmacFileAioMgrNormalReqComplete(PPDMACEPFILEMGR pAioMgr, RTFILEAIOREQ hReq)
{
    size_t cbTransfered = 0;
    int rcReq = RTFileAioReqGetRC(hReq, &cbTransfered);

    pdmacFileAioMgrNormalReqCompleteRc(pAioMgr, hReq, rcReq, cbTransfered);
}

static void pdmacFileAioMgrNormalReqCompleteRc(PPDMACEPFILEMGR pAioMgr, RTFILEAIOREQ hReq,
                                               int rcReq, size_t cbTransfered)
{
    int rc = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint;
    PPDMACTASKFILE pTask = (PPDMACTASKFILE)RTFileAioReqGetUser(hReq);
    PPDMACTASKFILE pTasksWaiting;

    LogFlowFunc(("pAioMgr=%#p hReq=%#p\n", pAioMgr, hReq));

    pEndpoint = pTask->pEndpoint;

    pTask->hReq = NIL_RTFILEAIOREQ;

    pAioMgr->cRequestsActive--;
    pEndpoint->AioMgr.cRequestsActive--;
    pEndpoint->AioMgr.cReqsProcessed++;

    /*
     * It is possible that the request failed on Linux with kernels < 2.6.23
     * if the passed buffer was allocated with remap_pfn_range or if the file
     * is on an NFS endpoint which does not support async and direct I/O at the same time.
     * The endpoint will be migrated to a failsafe manager in case a request fails.
     */
    if (RT_FAILURE(rcReq))
    {
        /* Free bounce buffers and the IPRT request. */
        pdmacFileAioMgrNormalRequestFree(pAioMgr, hReq);

        if (pTask->enmTransferType == PDMACTASKFILETRANSFER_FLUSH)
        {
            LogRel(("AIOMgr: Flush failed with %Rrc, disabling async flushes\n", rcReq));
            pEndpoint->fAsyncFlushSupported = false;
            AssertMsg(pEndpoint->pFlushReq == pTask, ("Failed flush request doesn't match active one\n"));
            /* The other method will take over now. */

            pEndpoint->pFlushReq = NULL;
            /* Call completion callback */
            LogFlow(("Flush task=%#p completed with %Rrc\n", pTask, VINF_SUCCESS));
            pTask->pfnCompleted(pTask, pTask->pvUser, VINF_SUCCESS);
            pdmacFileTaskFree(pEndpoint, pTask);
        }
        else
        {
            /* Free the lock and process pending tasks if necessary */
            pTasksWaiting = pdmacFileAioMgrNormalRangeLockFree(pAioMgr, pEndpoint, pTask->pRangeLock);
            rc = pdmacFileAioMgrNormalProcessTaskList(pTasksWaiting, pAioMgr, pEndpoint);
            AssertRC(rc);

            if (pTask->cbBounceBuffer)
                RTMemPageFree(pTask->pvBounceBuffer, pTask->cbBounceBuffer);

            /*
             * Fatal errors are reported to the guest and non-fatal errors
             * will cause a migration to the failsafe manager in the hope
             * that the error disappears.
             */
            if (!pdmacFileAioMgrNormalRcIsFatal(rcReq))
            {
                /* Queue the request on the pending list. */
                pTask->pNext = pEndpoint->AioMgr.pReqsPendingHead;
                pEndpoint->AioMgr.pReqsPendingHead = pTask;

                /* Create a new failsafe manager if necessary. */
                if (!pEndpoint->AioMgr.fMoving)
                {
                    PPDMACEPFILEMGR pAioMgrFailsafe;

                    LogRel(("%s: Request %#p failed with rc=%Rrc, migrating endpoint %s to failsafe manager.\n",
                            RTThreadGetName(pAioMgr->Thread), pTask, rcReq, pEndpoint->Core.pszUri));

                    pEndpoint->AioMgr.fMoving = true;

                    rc = pdmacFileAioMgrCreate((PPDMASYNCCOMPLETIONEPCLASSFILE)pEndpoint->Core.pEpClass,
                                                &pAioMgrFailsafe, PDMACEPFILEMGRTYPE_SIMPLE);
                    AssertRC(rc);

                    pEndpoint->AioMgr.pAioMgrDst = pAioMgrFailsafe;

                    /* Update the flags to open the file with. Disable async I/O and enable the host cache. */
                    pEndpoint->fFlags &= ~(RTFILE_O_ASYNC_IO | RTFILE_O_NO_CACHE);
                }

                /* If this was the last request for the endpoint migrate it to the new manager. */
                if (!pEndpoint->AioMgr.cRequestsActive)
                {
                    bool fReqsPending = pdmacFileAioMgrNormalRemoveEndpoint(pEndpoint);
                    Assert(!fReqsPending); NOREF(fReqsPending);

                    rc = pdmacFileAioMgrAddEndpoint(pEndpoint->AioMgr.pAioMgrDst, pEndpoint);
                    AssertRC(rc);
                }
            }
            else
            {
                pTask->pfnCompleted(pTask, pTask->pvUser, rcReq);
                pdmacFileTaskFree(pEndpoint, pTask);
            }
        }
    }
    else
    {
        if (pTask->enmTransferType == PDMACTASKFILETRANSFER_FLUSH)
        {
            /* Clear pending flush */
            AssertMsg(pEndpoint->pFlushReq == pTask, ("Completed flush request doesn't match active one\n"));
            pEndpoint->pFlushReq = NULL;
            pdmacFileAioMgrNormalRequestFree(pAioMgr, hReq);

            /* Call completion callback */
            LogFlow(("Flush task=%#p completed with %Rrc\n", pTask, rcReq));
            pTask->pfnCompleted(pTask, pTask->pvUser, rcReq);
            pdmacFileTaskFree(pEndpoint, pTask);
        }
        else
        {
            /*
             * Restart an incomplete transfer.
             * This usually means that the request will return an error now
             * but to get the cause of the error (disk full, file too big, I/O error, ...)
             * the transfer needs to be continued.
             */
            pTask->cbTransfered += cbTransfered;

            if (RT_UNLIKELY(   pTask->cbTransfered < pTask->DataSeg.cbSeg
                            || (   pTask->cbBounceBuffer
                                && pTask->cbTransfered < pTask->cbBounceBuffer)))
            {
                RTFOFF offStart;
                size_t cbToTransfer;
                uint8_t *pbBuf = NULL;

                LogFlow(("Restarting incomplete transfer %#p (%zu bytes transferred)\n",
                         pTask, cbTransfered));
                Assert(cbTransfered % 512 == 0);

                if (pTask->cbBounceBuffer)
                {
                    AssertPtr(pTask->pvBounceBuffer);
                    offStart     = (pTask->Off & ~((RTFOFF)512-1)) + pTask->cbTransfered;
                    cbToTransfer = pTask->cbBounceBuffer - pTask->cbTransfered;
                    pbBuf        = (uint8_t *)pTask->pvBounceBuffer + pTask->cbTransfered;
                }
                else
                {
                    Assert(!pTask->pvBounceBuffer);
                    offStart     = pTask->Off + pTask->cbTransfered;
                    cbToTransfer = pTask->DataSeg.cbSeg - pTask->cbTransfered;
                    pbBuf        = (uint8_t *)pTask->DataSeg.pvSeg + pTask->cbTransfered;
                }

                if (pTask->fPrefetch || pTask->enmTransferType == PDMACTASKFILETRANSFER_READ)
                {
                    rc = RTFileAioReqPrepareRead(hReq, pEndpoint->hFile, offStart,
                                                 pbBuf, cbToTransfer, pTask);
                }
                else
                {
                    AssertMsg(pTask->enmTransferType == PDMACTASKFILETRANSFER_WRITE,
                                  ("Invalid transfer type\n"));
                    rc = RTFileAioReqPrepareWrite(hReq, pEndpoint->hFile, offStart,
                                                  pbBuf, cbToTransfer, pTask);
                }
                AssertRC(rc);

                pTask->hReq = hReq;
                pdmacFileAioMgrNormalReqsEnqueue(pAioMgr, pEndpoint, &hReq, 1);
            }
            else if (pTask->fPrefetch)
            {
                Assert(pTask->enmTransferType == PDMACTASKFILETRANSFER_WRITE);
                Assert(pTask->cbBounceBuffer);

                memcpy(((uint8_t *)pTask->pvBounceBuffer) + pTask->offBounceBuffer,
                        pTask->DataSeg.pvSeg,
                        pTask->DataSeg.cbSeg);

                /* Write it now. */
                pTask->fPrefetch = false;
                RTFOFF offStart = pTask->Off & ~(RTFOFF)(512-1);
                size_t cbToTransfer = RT_ALIGN_Z(pTask->DataSeg.cbSeg + (pTask->Off - offStart), 512);

                pTask->cbTransfered = 0;

                /* Grow the file if needed. */
                if (RT_UNLIKELY((uint64_t)(pTask->Off + pTask->DataSeg.cbSeg) > pEndpoint->cbFile))
                {
                    ASMAtomicWriteU64(&pEndpoint->cbFile, pTask->Off + pTask->DataSeg.cbSeg);
                    RTFileSetSize(pEndpoint->hFile, pTask->Off + pTask->DataSeg.cbSeg);
                }

                rc = RTFileAioReqPrepareWrite(hReq, pEndpoint->hFile,
                                              offStart, pTask->pvBounceBuffer, cbToTransfer, pTask);
                AssertRC(rc);
                pTask->hReq = hReq;
                pdmacFileAioMgrNormalReqsEnqueue(pAioMgr, pEndpoint, &hReq, 1);
            }
            else
            {
                if (RT_SUCCESS(rc) && pTask->cbBounceBuffer)
                {
                    if (pTask->enmTransferType == PDMACTASKFILETRANSFER_READ)
                        memcpy(pTask->DataSeg.pvSeg,
                               ((uint8_t *)pTask->pvBounceBuffer) + pTask->offBounceBuffer,
                               pTask->DataSeg.cbSeg);

                    RTMemPageFree(pTask->pvBounceBuffer, pTask->cbBounceBuffer);
                }

                pdmacFileAioMgrNormalRequestFree(pAioMgr, hReq);

                /* Free the lock and process pending tasks if necessary */
                pTasksWaiting = pdmacFileAioMgrNormalRangeLockFree(pAioMgr, pEndpoint, pTask->pRangeLock);
                if (pTasksWaiting)
                {
                    rc = pdmacFileAioMgrNormalProcessTaskList(pTasksWaiting, pAioMgr, pEndpoint);
                    AssertRC(rc);
                }

                /* Call completion callback */
                LogFlow(("Task=%#p completed with %Rrc\n", pTask, rcReq));
                pTask->pfnCompleted(pTask, pTask->pvUser, rcReq);
                pdmacFileTaskFree(pEndpoint, pTask);

                /*
                 * If there is no request left on the endpoint but a flush request is set
                 * it completed now and we notify the owner.
                 * Furthermore we look for new requests and continue.
                 */
                if (!pEndpoint->AioMgr.cRequestsActive && pEndpoint->pFlushReq)
                {
                    /* Call completion callback */
                    pTask = pEndpoint->pFlushReq;
                    pEndpoint->pFlushReq = NULL;

                    AssertMsg(pTask->pEndpoint == pEndpoint, ("Endpoint of the flush request does not match assigned one\n"));

                    pTask->pfnCompleted(pTask, pTask->pvUser, VINF_SUCCESS);
                    pdmacFileTaskFree(pEndpoint, pTask);
                }
                else if (RT_UNLIKELY(!pEndpoint->AioMgr.cRequestsActive && pEndpoint->AioMgr.fMoving))
                {
                    /* If the endpoint is about to be migrated do it now. */
                    bool fReqsPending = pdmacFileAioMgrNormalRemoveEndpoint(pEndpoint);
                    Assert(!fReqsPending); NOREF(fReqsPending);

                    rc = pdmacFileAioMgrAddEndpoint(pEndpoint->AioMgr.pAioMgrDst, pEndpoint);
                    AssertRC(rc);
                }
            }
        } /* Not a flush request */
    } /* request completed successfully */
}

/** Helper macro for checking for error codes. */
#define CHECK_RC(pAioMgr, rc) \
    if (RT_FAILURE(rc)) \
    {\
        int rc2 = pdmacFileAioMgrNormalErrorHandler(pAioMgr, rc, RT_SRC_POS);\
        return rc2;\
    }

/**
 * The normal I/O manager using the RTFileAio* API
 *
 * @returns VBox status code.
 * @param   hThreadSelf Handle of the thread.
 * @param   pvUser      Opaque user data.
 */
DECLCALLBACK(int) pdmacFileAioMgrNormal(RTTHREAD hThreadSelf, void *pvUser)
{
    int             rc          = VINF_SUCCESS;
    PPDMACEPFILEMGR pAioMgr     = (PPDMACEPFILEMGR)pvUser;
    uint64_t        uMillisEnd  = RTTimeMilliTS() + PDMACEPFILEMGR_LOAD_UPDATE_PERIOD;
    NOREF(hThreadSelf);

    while (   pAioMgr->enmState == PDMACEPFILEMGRSTATE_RUNNING
           || pAioMgr->enmState == PDMACEPFILEMGRSTATE_SUSPENDING
           || pAioMgr->enmState == PDMACEPFILEMGRSTATE_GROWING)
    {
        if (!pAioMgr->cRequestsActive)
        {
            ASMAtomicWriteBool(&pAioMgr->fWaitingEventSem, true);
            if (!ASMAtomicReadBool(&pAioMgr->fWokenUp))
                rc = RTSemEventWait(pAioMgr->EventSem, pAioMgr->msBwLimitExpired);
            ASMAtomicWriteBool(&pAioMgr->fWaitingEventSem, false);
            Assert(RT_SUCCESS(rc) || rc == VERR_TIMEOUT);

            LogFlow(("Got woken up\n"));
            ASMAtomicWriteBool(&pAioMgr->fWokenUp, false);
        }

        /* Check for an external blocking event first. */
        if (pAioMgr->fBlockingEventPending)
        {
            rc = pdmacFileAioMgrNormalProcessBlockingEvent(pAioMgr);
            CHECK_RC(pAioMgr, rc);
        }

        if (RT_LIKELY(    pAioMgr->enmState == PDMACEPFILEMGRSTATE_RUNNING
                      ||  pAioMgr->enmState == PDMACEPFILEMGRSTATE_GROWING))
        {
            /* We got woken up because an endpoint issued new requests. Queue them. */
            rc = pdmacFileAioMgrNormalCheckEndpoints(pAioMgr);
            CHECK_RC(pAioMgr, rc);

            while (pAioMgr->cRequestsActive)
            {
                RTFILEAIOREQ apReqs[20];
                uint32_t     cReqsCompleted = 0;
                size_t       cReqsWait;

                if (pAioMgr->cRequestsActive > RT_ELEMENTS(apReqs))
                    cReqsWait = RT_ELEMENTS(apReqs);
                else
                    cReqsWait = pAioMgr->cRequestsActive;

                LogFlow(("Waiting for %d of %d tasks to complete\n", 1, cReqsWait));

                rc = RTFileAioCtxWait(pAioMgr->hAioCtx,
                                      1,
                                      RT_INDEFINITE_WAIT, apReqs,
                                      cReqsWait, &cReqsCompleted);
                if (RT_FAILURE(rc) && (rc != VERR_INTERRUPTED))
                    CHECK_RC(pAioMgr, rc);

                LogFlow(("%d tasks completed\n", cReqsCompleted));

                for (uint32_t i = 0; i < cReqsCompleted; i++)
                    pdmacFileAioMgrNormalReqComplete(pAioMgr, apReqs[i]);

                /* Check for an external blocking event before we go to sleep again. */
                if (pAioMgr->fBlockingEventPending)
                {
                    rc = pdmacFileAioMgrNormalProcessBlockingEvent(pAioMgr);
                    CHECK_RC(pAioMgr, rc);
                }

                /* Update load statistics. */
                uint64_t uMillisCurr = RTTimeMilliTS();
                if (uMillisCurr > uMillisEnd)
                {
                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointCurr = pAioMgr->pEndpointsHead;

                    /* Calculate timespan. */
                    uMillisCurr -= uMillisEnd;

                    while (pEndpointCurr)
                    {
                        pEndpointCurr->AioMgr.cReqsPerSec    = pEndpointCurr->AioMgr.cReqsProcessed / (uMillisCurr + PDMACEPFILEMGR_LOAD_UPDATE_PERIOD);
                        pEndpointCurr->AioMgr.cReqsProcessed = 0;
                        pEndpointCurr = pEndpointCurr->AioMgr.pEndpointNext;
                    }

                    /* Set new update interval */
                    uMillisEnd = RTTimeMilliTS() + PDMACEPFILEMGR_LOAD_UPDATE_PERIOD;
                }

                /* Check endpoints for new requests. */
                if (pAioMgr->enmState != PDMACEPFILEMGRSTATE_GROWING)
                {
                    rc = pdmacFileAioMgrNormalCheckEndpoints(pAioMgr);
                    CHECK_RC(pAioMgr, rc);
                }
            } /* while requests are active. */

            if (pAioMgr->enmState == PDMACEPFILEMGRSTATE_GROWING)
            {
                rc = pdmacFileAioMgrNormalGrow(pAioMgr);
                AssertRC(rc);
                Assert(pAioMgr->enmState == PDMACEPFILEMGRSTATE_RUNNING);

                rc = pdmacFileAioMgrNormalCheckEndpoints(pAioMgr);
                CHECK_RC(pAioMgr, rc);
            }
        } /* if still running */
    } /* while running */

    LogFlowFunc(("rc=%Rrc\n", rc));
    return rc;
}

#undef CHECK_RC

