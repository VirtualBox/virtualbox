/* $Id: TstHGCMMockUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TstHGCMMockUtils.cpp - Utility functions for the HGCM Mocking framework.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include <VBox/HostServices/TstHGCMMockUtils.h>

#include <iprt/err.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/types.h>


/*********************************************************************************************************************************
*   Context                                                                                                                      *
*********************************************************************************************************************************/
/**
 * Initializes a HGCM Mock utils context.
 *
 * @param   pCtx                Context to intiialize.
 * @param   pSvc                HGCM Mock service instance to use.
 */
void TstHGCMUtilsCtxInit(PTSTHGCMUTILSCTX pCtx, PTSTHGCMMOCKSVC pSvc)
{
    RT_BZERO(pCtx, sizeof(TSTHGCMUTILSCTX));

    pCtx->pSvc = pSvc;
}


/*********************************************************************************************************************************
*   Tasks                                                                                                                        *
*********************************************************************************************************************************/
/**
 * Returns the current task of a HGCM Mock utils context.
 *
 * @returns Current task of a HGCM Mock utils context. NULL if no current task found.
 * @param   pCtx                HGCM Mock utils context.
 */
PTSTHGCMUTILSTASK TstHGCMUtilsTaskGetCurrent(PTSTHGCMUTILSCTX pCtx)
{
    /* Currently we only support one task at a time. */
    return &pCtx->Task;
}

/**
 * Initializes a HGCM Mock utils task.
 *
 * @returns VBox status code.
 * @param   pTask               Task to initialize.
 */
int TstHGCMUtilsTaskInit(PTSTHGCMUTILSTASK pTask)
{
    pTask->pvUser = NULL;
    pTask->rcCompleted = pTask->rcExpected = VERR_IPE_UNINITIALIZED_STATUS;
    return RTSemEventCreate(&pTask->hEvent);
}

/**
 * Destroys a HGCM Mock utils task.
 *
 * @returns VBox status code.
 * @param   pTask               Task to destroy.
 */
void TstHGCMUtilsTaskDestroy(PTSTHGCMUTILSTASK pTask)
{
    RTSemEventDestroy(pTask->hEvent);
}

/**
 * Waits for a HGCM Mock utils task to complete.
 *
 * @returns VBox status code.
 * @param   pTask               Task to wait for.
 * @param   msTimeout           Timeout (in ms) to wait.
 */
int TstHGCMUtilsTaskWait(PTSTHGCMUTILSTASK pTask, RTMSINTERVAL msTimeout)
{
    return RTSemEventWait(pTask->hEvent, msTimeout);
}

/**
 * Returns if the HGCM Mock utils task has been completed successfully.
 *
 * @returns \c true if successful, \c false if not.
 * @param   pTask               Task to check.
 */
bool TstHGCMUtilsTaskOk(PTSTHGCMUTILSTASK pTask)
{
    return pTask->rcCompleted == pTask->rcExpected;
}

/**
 * Returns if the HGCM Mock utils task has been completed (failed or succeeded).
 *
 * @returns \c true if completed, \c false if (still) running.
 * @param   pTask               Task to check.
 */
bool TstHGCMUtilsTaskCompleted(PTSTHGCMUTILSTASK pTask)
{
    return pTask->rcCompleted != VERR_IPE_UNINITIALIZED_STATUS;
}

/**
 * Signals a HGCM Mock utils task to complete its operation.
 *
 * @param   pTask               Task to complete.
 * @param   rc                  Task result to set for completion.
 */
void TstHGCMUtilsTaskSignal(PTSTHGCMUTILSTASK pTask, int rc)
{
    AssertMsg(pTask->rcCompleted == VERR_IPE_UNINITIALIZED_STATUS, ("Task already completed\n"));
    pTask->rcCompleted = rc;
    int rc2 = RTSemEventSignal(pTask->hEvent);
    AssertRC(rc2);
}


/*********************************************************************************************************************************
 * Threading                                                                                                                     *
 ********************************************************************************************************************************/

/**
 * Thread worker for the guest side thread.
 *
 * @returns VBox status code.
 * @param   hThread             Thread handle.
 * @param   pvUser              Pointer of type PTSTHGCMUTILSCTX.
 *
 * @note    Runs in the guest thread.
 */
static DECLCALLBACK(int) tstHGCMUtilsGuestThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    PTSTHGCMUTILSCTX pCtx = (PTSTHGCMUTILSCTX)pvUser;
    AssertPtr(pCtx);

    RTThreadUserSignal(hThread);

    if (pCtx->Guest.pfnThread)
        return pCtx->Guest.pfnThread(pCtx, pCtx->Guest.pvUser);

    return VINF_SUCCESS;
}

/**
 * Starts the guest side thread.
 *
 * @returns VBox status code.
 * @param   pCtx                HGCM Mock utils context to start guest thread for.
 * @param   pFnThread           Pointer to custom thread worker function to call within the guest side thread.
 * @param   pvUser              User-supplied pointer to guest thread context data. Optional and can be NULL.
 */
int TstHGCMUtilsGuestThreadStart(PTSTHGCMUTILSCTX pCtx, PFNTSTHGCMUTILSTHREAD pFnThread, void *pvUser)
{
    pCtx->Guest.pfnThread = pFnThread;
    pCtx->Guest.pvUser    = pvUser;

    int rc = RTThreadCreate(&pCtx->Guest.hThread, tstHGCMUtilsGuestThread, pCtx, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE,
                            "tstShClGst");
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(pCtx->Guest.hThread, RT_MS_30SEC);

    return rc;
}

/**
 * Stops the guest side thread.
 *
 * @returns VBox status code.
 * @param   pCtx                HGCM Mock utils context to stop guest thread for.
 */
int TstHGCMUtilsGuestThreadStop(PTSTHGCMUTILSCTX pCtx)
{
    ASMAtomicWriteBool(&pCtx->Guest.fShutdown, true);

    int rcThread;
    int rc = RTThreadWait(pCtx->Guest.hThread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rc))
        rc = rcThread;
    if (RT_SUCCESS(rc))
        pCtx->Guest.hThread = NIL_RTTHREAD;

    return rc;
}

/**
 * Thread worker function for the host side HGCM service.
 *
 * @returns VBox status code.
 * @param   hThread             Thread handle.
 * @param   pvUser              Pointer of type PTSTHGCMUTILSCTX.
 *
 * @note    Runs in the host service thread.
 */
static DECLCALLBACK(int) tstHGCMUtilsHostThreadWorker(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    PTSTHGCMUTILSCTX pCtx = (PTSTHGCMUTILSCTX)pvUser;
    AssertPtr(pCtx);

    int rc = VINF_SUCCESS;

    RTThreadUserSignal(hThread);

    PTSTHGCMMOCKSVC const pSvc = TstHgcmMockSvcInst();

    for (;;)
    {
        if (ASMAtomicReadBool(&pCtx->Host.fShutdown))
            break;

        /* Wait for a new (mock) HGCM client to connect. */
        PTSTHGCMMOCKCLIENT pMockClient = TstHgcmMockSvcWaitForConnectEx(pSvc, 100 /* ms */);
        if (pMockClient) /* Might be NULL when timed out. */
        {
            if (pCtx->Host.Callbacks.pfnOnClientConnected)
                /* ignore rc */ pCtx->Host.Callbacks.pfnOnClientConnected(pCtx, pMockClient, pCtx->Host.pvUser);
        }
    }

    return rc;
}

/**
 * Starts the host side thread.
 *
 * @returns VBox status code.
 * @param   pCtx                HGCM Mock utils context to start host thread for.
 * @param   pCallbacks          Pointer to host callback table to use.
 * @param   pvUser              User-supplied pointer to reach into the host thread callbacks.
 */
int TstHGCMUtilsHostThreadStart(PTSTHGCMUTILSCTX pCtx, PTSTHGCMUTILSHOSTCALLBACKS pCallbacks, void *pvUser)
{
    memcpy(&pCtx->Host.Callbacks, pCallbacks, sizeof(TSTHGCMUTILSHOSTCALLBACKS));
    pCtx->Host.pvUser = pvUser;

    int rc = RTThreadCreate(&pCtx->Host.hThread, tstHGCMUtilsHostThreadWorker, pCtx, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE,
                            "tstShClHst");
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(pCtx->Host.hThread, RT_MS_30SEC);

    return rc;
}

/**
 * Stops the host side thread.
 *
 * @returns VBox status code.
 * @param   pCtx                HGCM Mock utils context to stop host thread for.
 */
int TstHGCMUtilsHostThreadStop(PTSTHGCMUTILSCTX pCtx)
{
    ASMAtomicWriteBool(&pCtx->Host.fShutdown, true);

    int rcThread;
    int rc = RTThreadWait(pCtx->Host.hThread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rc))
        rc = rcThread;
    if (RT_SUCCESS(rc))
        pCtx->Host.hThread = NIL_RTTHREAD;

    return rc;
}

