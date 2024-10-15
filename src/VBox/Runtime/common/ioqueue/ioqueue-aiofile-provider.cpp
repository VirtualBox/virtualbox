/* $Id: ioqueue-aiofile-provider.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - I/O queue, Async I/O file provider.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_IOQUEUE
#include <iprt/ioqueue.h>

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#include "internal/ioqueue.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Internal I/O queue provider instance data.
 */
typedef struct RTIOQUEUEPROVINT
{
    /** The async I/O context handle. */
    RTFILEAIOCTX                hAioCtx;
    /** Pointer to the array of requests waiting for commit. */
    PRTFILEAIOREQ               pahReqsToCommit;
    /** Maximum number of requests to wait for commit.. */
    size_t                      cReqsToCommitMax;
    /** Number of requests waiting for commit. */
    uint32_t                    cReqsToCommit;
    /** Array of free cached request handles. */
    PRTFILEAIOREQ               pahReqsFree;
    /** Maximum number of cached requests. */
    uint32_t                    cReqsFreeMax;
    /** Number of free cached requests. */
    volatile uint32_t           cReqsFree;
} RTIOQUEUEPROVINT;
/** Pointer to the internal I/O queue provider instance data. */
typedef RTIOQUEUEPROVINT *PRTIOQUEUEPROVINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnIsSupported} */
static DECLCALLBACK(bool) rtIoQueueAioFileProv_IsSupported(void)
{
    /* The common code/public API already checked for the proper handle type. */
    /** @todo Check that the file was opened with async I/O enabled on some platforms? */
    return true;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnQueueInit} */
static DECLCALLBACK(int) rtIoQueueAioFileProv_QueueInit(RTIOQUEUEPROV hIoQueueProv, uint32_t fFlags,
                                                        uint32_t cSqEntries, uint32_t cCqEntries)
{
    RT_NOREF(fFlags, cCqEntries);

    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    int rc = VINF_SUCCESS;

    pThis->cReqsToCommitMax = cSqEntries;
    pThis->cReqsFreeMax     = cSqEntries;
    pThis->cReqsFree        = 0;

    pThis->pahReqsToCommit = (PRTFILEAIOREQ)RTMemAllocZ(cSqEntries * sizeof(PRTFILEAIOREQ));
    if (RT_LIKELY(pThis->pahReqsToCommit))
    {
        pThis->pahReqsFree = (PRTFILEAIOREQ)RTMemAllocZ(cSqEntries * sizeof(PRTFILEAIOREQ));
        if (RT_LIKELY(pThis->pahReqsFree))
        {
            rc = RTFileAioCtxCreate(&pThis->hAioCtx, cSqEntries, RTFILEAIOCTX_FLAGS_WAIT_WITHOUT_PENDING_REQUESTS);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            RTMemFree(pThis->pahReqsFree);
        }
        else
            rc = VERR_NO_MEMORY;

        RTMemFree(pThis->pahReqsToCommit);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnQueueDestroy} */
static DECLCALLBACK(void) rtIoQueueAioFileProv_QueueDestroy(RTIOQUEUEPROV hIoQueueProv)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    RTFileAioCtxDestroy(pThis->hAioCtx);

    while (pThis->cReqsFree--)
    {
        RTFILEAIOREQ hReq = pThis->pahReqsFree[pThis->cReqsFree];
        RTFileAioReqDestroy(hReq);
        pThis->pahReqsFree[pThis->cReqsFree] = NULL;
    }

    RTMemFree(pThis->pahReqsFree);
    RTMemFree(pThis->pahReqsToCommit);
    RT_BZERO(pThis, sizeof(*pThis));
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnHandleRegister} */
static DECLCALLBACK(int) rtIoQueueAioFileProv_HandleRegister(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    return RTFileAioCtxAssociateWithFile(pThis->hAioCtx, pHandle->u.hFile);
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnHandleDeregister} */
static DECLCALLBACK(int) rtIoQueueAioFileProv_HandleDeregister(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle)
{
    RT_NOREF(hIoQueueProv, pHandle);

    /** @todo For Windows there doesn't seem to be a way to deregister the file handle without reopening the file,
     *.for all other hosts this is a nop, just like the register method.
     */
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnReqPrepare} */
static DECLCALLBACK(int) rtIoQueueAioFileProv_ReqPrepare(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle, RTIOQUEUEOP enmOp,
                                                         uint64_t off, void *pvBuf, size_t cbBuf, uint32_t fReqFlags,
                                                         void *pvUser)
{
    RT_NOREF(fReqFlags);

    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    /* Try to grab a free request structure from the cache. */
    RTFILEAIOREQ hReq = NIL_RTFILEAIOREQ;
    int rc = VINF_SUCCESS;
    uint32_t cReqsFree = ASMAtomicReadU32(&pThis->cReqsFree);
    if (cReqsFree)
    {
        do
        {
            cReqsFree = ASMAtomicReadU32(&pThis->cReqsFree);
            hReq = pThis->pahReqsFree[pThis->cReqsFree - 1];
        } while (!ASMAtomicCmpXchgU32(&pThis->cReqsFree, cReqsFree - 1, cReqsFree));
    }
    else
        rc = RTFileAioReqCreate(&hReq);

    if (RT_SUCCESS(rc))
    {
        switch (enmOp)
        {
            case RTIOQUEUEOP_READ:
                rc = RTFileAioReqPrepareRead(hReq, pHandle->u.hFile, (RTFOFF)off, pvBuf, cbBuf, pvUser);
                break;
            case RTIOQUEUEOP_WRITE:
                rc = RTFileAioReqPrepareWrite(hReq, pHandle->u.hFile, (RTFOFF)off, pvBuf, cbBuf, pvUser);
                break;
            case RTIOQUEUEOP_SYNC:
                rc = RTFileAioReqPrepareFlush(hReq, pHandle->u.hFile, pvUser);
                break;
            default:
                AssertMsgFailedReturn(("Invalid I/O queue operation: %d\n", enmOp), VERR_INTERNAL_ERROR);
        }

        if (RT_SUCCESS(rc))
            pThis->pahReqsToCommit[pThis->cReqsToCommit++] = hReq;
        else
        {
            int rc2 = RTFileAioReqDestroy(hReq);
            Assert(rc2); RT_NOREF(rc2);
        }
    }

    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnCommit} */
static DECLCALLBACK(int) rtIoQueueAioFileProv_Commit(RTIOQUEUEPROV hIoQueueProv, uint32_t *pcReqsCommitted)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    int rc = RTFileAioCtxSubmit(pThis->hAioCtx, pThis->pahReqsToCommit, pThis->cReqsToCommit);
    if (RT_SUCCESS(rc))
    {
        *pcReqsCommitted = pThis->cReqsToCommit;
        pThis->cReqsToCommit = 0;
    }

    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnEvtWait} */
static DECLCALLBACK(int) rtIoQueueAioFileProv_EvtWait(RTIOQUEUEPROV hIoQueueProv, PRTIOQUEUECEVT paCEvt, uint32_t cCEvt,
                                                      uint32_t cMinWait, uint32_t *pcCEvt, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    int rc = VINF_SUCCESS;
    uint32_t idxCEvt = 0;

    while (   RT_SUCCESS(rc)
           && cMinWait
           && cCEvt)
    {
        RTFILEAIOREQ ahReqs[64];
        uint32_t cReqsCompleted = 0;

        rc = RTFileAioCtxWait(pThis->hAioCtx, cMinWait, RT_INDEFINITE_WAIT,
                              &ahReqs[0], RT_MIN(RT_ELEMENTS(ahReqs), cCEvt), &cReqsCompleted);
        if (RT_SUCCESS(rc))
        {
            for (unsigned i = 0; i < cReqsCompleted; i++)
            {
                RTFILEAIOREQ hReq = ahReqs[i];

                paCEvt[idxCEvt].rcReq    = RTFileAioReqGetRC(hReq, &paCEvt[idxCEvt].cbXfered);
                paCEvt[idxCEvt].pvUser   = RTFileAioReqGetUser(hReq);
                idxCEvt++;

                /* Try to insert the free request into the cache. */
                uint32_t cReqsFree = ASMAtomicReadU32(&pThis->cReqsFree);
                if (cReqsFree < pThis->cReqsFreeMax)
                {
                    do
                    {
                        cReqsFree = ASMAtomicReadU32(&pThis->cReqsFree);
                        pThis->pahReqsFree[pThis->cReqsFree] = hReq;
                    } while (!ASMAtomicCmpXchgU32(&pThis->cReqsFree, cReqsFree + 1, cReqsFree));
                }
                else
                    rc = RTFileAioReqDestroy(hReq);
            }

            cCEvt -= cReqsCompleted;
            cMinWait -= RT_MIN(cMinWait, cReqsCompleted);
        }
    }

    *pcCEvt = idxCEvt;
    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnEvtWaitWakeup} */
static DECLCALLBACK(int) rtIoQueueAioFileProv_EvtWaitWakeup(RTIOQUEUEPROV hIoQueueProv)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    return RTFileAioCtxWakeup(pThis->hAioCtx);
}


/**
 * Async file I/O queue provider virtual method table.
 */
RT_DECL_DATA_CONST(RTIOQUEUEPROVVTABLE const) g_RTIoQueueAioFileProv =
{
    /** uVersion */
    RTIOQUEUEPROVVTABLE_VERSION,
    /** pszId */
    "AioFile",
    /** cbIoQueueProv */
    sizeof(RTIOQUEUEPROVINT),
    /** enmHnd */
    RTHANDLETYPE_FILE,
    /** fFlags */
    0,
    /** pfnIsSupported */
    rtIoQueueAioFileProv_IsSupported,
    /** pfnQueueInit  */
    rtIoQueueAioFileProv_QueueInit,
    /** pfnQueueDestroy */
    rtIoQueueAioFileProv_QueueDestroy,
    /** pfnHandleRegister */
    rtIoQueueAioFileProv_HandleRegister,
    /** pfnHandleDeregister */
    rtIoQueueAioFileProv_HandleDeregister,
    /** pfnReqPrepare */
    rtIoQueueAioFileProv_ReqPrepare,
    /** pfnReqPrepareSg */
    NULL,
    /** pfnCommit */
    rtIoQueueAioFileProv_Commit,
    /** pfnEvtWait */
    rtIoQueueAioFileProv_EvtWait,
    /** pfnEvtWaitWakeup */
    rtIoQueueAioFileProv_EvtWaitWakeup,
    /** uEndMarker */
    RTIOQUEUEPROVVTABLE_VERSION
};

