/* $Id: fileaio-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File async I/O, native implementation for the Solaris host platform.
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
#define LOG_GROUP RTLOGGROUP_FILE
#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/fileaio.h"

#include <port.h>
#include <aio.h>
#include <errno.h>
#include <unistd.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Async I/O completion context state.
 */
typedef struct RTFILEAIOCTXINTERNAL
{
    /** Handle to the port. */
    int               iPort;
    /** Current number of requests active on this context. */
    volatile int32_t  cRequests;
    /** Flags given during creation. */
    uint32_t          fFlags;
    /** Magic value (RTFILEAIOCTX_MAGIC). */
    uint32_t          u32Magic;
} RTFILEAIOCTXINTERNAL;
/** Pointer to an internal context structure. */
typedef RTFILEAIOCTXINTERNAL *PRTFILEAIOCTXINTERNAL;

/**
 * Async I/O request state.
 */
typedef struct RTFILEAIOREQINTERNAL
{
    /** The aio control block. Must be the FIRST
     *  element. */
    struct aiocb           AioCB;
    /** Current state the request is in. */
    RTFILEAIOREQSTATE      enmState;
    /** Flag whether this is a flush request. */
    bool                   fFlush;
    /** Port notifier object to associate a request to a port. */
    port_notify_t          PortNotifier;
    /** Opaque user data. */
    void                  *pvUser;
    /** Completion context we are assigned to. */
    PRTFILEAIOCTXINTERNAL  pCtxInt;
    /** Magic value  (RTFILEAIOREQ_MAGIC). */
    uint32_t               u32Magic;
} RTFILEAIOREQINTERNAL;
/** Pointer to an internal request structure. */
typedef RTFILEAIOREQINTERNAL *PRTFILEAIOREQINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max number of events to get in one call. */
#define AIO_MAXIMUM_REQUESTS_PER_CONTEXT 64
/** Id for the wakeup event. */
#define AIO_CONTEXT_WAKEUP_EVENT 1

RTR3DECL(int) RTFileAioGetLimits(PRTFILEAIOLIMITS pAioLimits)
{
    int rcBSD = 0;
    AssertPtrReturn(pAioLimits, VERR_INVALID_POINTER);

    /* No limits known. */
    pAioLimits->cReqsOutstandingMax = RTFILEAIO_UNLIMITED_REQS;
    pAioLimits->cbBufferAlignment   = 0;

    return VINF_SUCCESS;
}

RTR3DECL(int) RTFileAioReqCreate(PRTFILEAIOREQ phReq)
{
    AssertPtrReturn(phReq, VERR_INVALID_POINTER);

    PRTFILEAIOREQINTERNAL pReqInt = (PRTFILEAIOREQINTERNAL)RTMemAllocZ(sizeof(RTFILEAIOREQINTERNAL));
    if (RT_UNLIKELY(!pReqInt))
        return VERR_NO_MEMORY;

    /* Ininitialize static parts. */
    pReqInt->AioCB.aio_sigevent.sigev_notify = SIGEV_PORT;
    pReqInt->AioCB.aio_sigevent.sigev_value.sival_ptr = &pReqInt->PortNotifier;
    pReqInt->PortNotifier.portnfy_user = pReqInt;
    pReqInt->pCtxInt                   = NULL;
    pReqInt->u32Magic                  = RTFILEAIOREQ_MAGIC;
    RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);

    *phReq = (RTFILEAIOREQ)pReqInt;

    return VINF_SUCCESS;
}

RTDECL(int) RTFileAioReqDestroy(RTFILEAIOREQ hReq)
{
    /*
     * Validate the handle and ignore nil.
     */
    if (hReq == NIL_RTFILEAIOREQ)
        return VINF_SUCCESS;
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);

    /*
     * Trash the magic and free it.
     */
    ASMAtomicUoWriteU32(&pReqInt->u32Magic, ~RTFILEAIOREQ_MAGIC);
    RTMemFree(pReqInt);
    return VINF_SUCCESS;
}

/**
 * Worker setting up the request.
 */
DECLINLINE(int) rtFileAioReqPrepareTransfer(RTFILEAIOREQ hReq, RTFILE hFile,
                                            unsigned uTransferDirection,
                                            RTFOFF off, void *pvBuf, size_t cbTransfer,
                                            void *pvUser)
{
    /*
     * Validate the input.
     */
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    Assert(hFile != NIL_RTFILE);
    AssertPtr(pvBuf);
    Assert(off >= 0);
    Assert(cbTransfer > 0);

    pReqInt->AioCB.aio_lio_opcode = uTransferDirection;
    pReqInt->AioCB.aio_fildes     = RTFileToNative(hFile);
    pReqInt->AioCB.aio_offset     = off;
    pReqInt->AioCB.aio_nbytes     = cbTransfer;
    pReqInt->AioCB.aio_buf        = pvBuf;
    pReqInt->fFlush               = false;
    pReqInt->pvUser               = pvUser;
    pReqInt->pCtxInt              = NULL;
    RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);

    return VINF_SUCCESS;
}

RTDECL(int) RTFileAioReqPrepareRead(RTFILEAIOREQ hReq, RTFILE hFile, RTFOFF off,
                                    void *pvBuf, size_t cbRead, void *pvUser)
{
    return rtFileAioReqPrepareTransfer(hReq, hFile, LIO_READ,
                                       off, pvBuf, cbRead, pvUser);
}

RTDECL(int) RTFileAioReqPrepareWrite(RTFILEAIOREQ hReq, RTFILE hFile, RTFOFF off,
                                     void const *pvBuf, size_t cbWrite, void *pvUser)
{
    return rtFileAioReqPrepareTransfer(hReq, hFile, LIO_WRITE,
                                       off, (void *)pvBuf, cbWrite, pvUser);
}

RTDECL(int) RTFileAioReqPrepareFlush(RTFILEAIOREQ hReq, RTFILE hFile, void *pvUser)
{
    PRTFILEAIOREQINTERNAL pReqInt = (PRTFILEAIOREQINTERNAL)hReq;

    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    Assert(hFile != NIL_RTFILE);

    pReqInt->fFlush           = true;
    pReqInt->AioCB.aio_fildes = RTFileToNative(hFile);
    pReqInt->AioCB.aio_offset = 0;
    pReqInt->AioCB.aio_nbytes = 0;
    pReqInt->AioCB.aio_buf    = NULL;
    pReqInt->pvUser           = pvUser;
    RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);

    return VINF_SUCCESS;
}

RTDECL(void *) RTFileAioReqGetUser(RTFILEAIOREQ hReq)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN_RC(pReqInt, NULL);

    return pReqInt->pvUser;
}

RTDECL(int) RTFileAioReqCancel(RTFILEAIOREQ hReq)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_NOT_SUBMITTED);

    int rcSolaris = aio_cancel(pReqInt->AioCB.aio_fildes, &pReqInt->AioCB);

    if (rcSolaris == AIO_CANCELED)
    {
        /*
         * Decrement request count because the request will never arrive at the
         * completion port.
         */
        AssertMsg(RT_VALID_PTR(pReqInt->pCtxInt), ("Invalid state. Request was canceled but wasn't submitted\n"));

        ASMAtomicDecS32(&pReqInt->pCtxInt->cRequests);
        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
        return VINF_SUCCESS;
    }
    else if (rcSolaris == AIO_ALLDONE)
        return VERR_FILE_AIO_COMPLETED;
    else if (rcSolaris == AIO_NOTCANCELED)
        return VERR_FILE_AIO_IN_PROGRESS;
    else
        return RTErrConvertFromErrno(errno);
}

RTDECL(int) RTFileAioReqGetRC(RTFILEAIOREQ hReq, size_t *pcbTransfered)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, PREPARED, VERR_FILE_AIO_NOT_SUBMITTED);
    AssertPtrNull(pcbTransfered);

    int rcSol = aio_error(&pReqInt->AioCB);
    Assert(rcSol != EINPROGRESS); /* Handled by our own state handling. */

    if (rcSol == 0)
    {
        if (pcbTransfered)
            *pcbTransfered = aio_return(&pReqInt->AioCB);
        return VINF_SUCCESS;
    }

    /* An error occurred. */
    return RTErrConvertFromErrno(rcSol);
}

RTDECL(int) RTFileAioCtxCreate(PRTFILEAIOCTX phAioCtx, uint32_t cAioReqsMax,
                               uint32_t fFlags)
{
    int rc = VINF_SUCCESS;
    PRTFILEAIOCTXINTERNAL pCtxInt;
    AssertPtrReturn(phAioCtx, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTFILEAIOCTX_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    pCtxInt = (PRTFILEAIOCTXINTERNAL)RTMemAllocZ(sizeof(RTFILEAIOCTXINTERNAL));
    if (RT_UNLIKELY(!pCtxInt))
        return VERR_NO_MEMORY;

    /* Init the event handle. */
    pCtxInt->iPort = port_create();
    if (RT_LIKELY(pCtxInt->iPort > 0))
    {
        pCtxInt->fFlags   = fFlags;
        pCtxInt->u32Magic = RTFILEAIOCTX_MAGIC;
        *phAioCtx = (RTFILEAIOCTX)pCtxInt;
    }
    else
    {
        RTMemFree(pCtxInt);
        rc = RTErrConvertFromErrno(errno);
    }

    return rc;
}

RTDECL(int) RTFileAioCtxDestroy(RTFILEAIOCTX hAioCtx)
{
    /* Validate the handle and ignore nil. */
    if (hAioCtx == NIL_RTFILEAIOCTX)
        return VINF_SUCCESS;
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);

    /* Cannot destroy a busy context. */
    if (RT_UNLIKELY(pCtxInt->cRequests))
        return VERR_FILE_AIO_BUSY;

    close(pCtxInt->iPort);
    ASMAtomicUoWriteU32(&pCtxInt->u32Magic, RTFILEAIOCTX_MAGIC_DEAD);
    RTMemFree(pCtxInt);

    return VINF_SUCCESS;
}

RTDECL(uint32_t) RTFileAioCtxGetMaxReqCount(RTFILEAIOCTX hAioCtx)
{
    return RTFILEAIO_UNLIMITED_REQS;
}

RTDECL(int) RTFileAioCtxAssociateWithFile(RTFILEAIOCTX hAioCtx, RTFILE hFile)
{
    return VINF_SUCCESS;
}

RTDECL(int) RTFileAioCtxSubmit(RTFILEAIOCTX hAioCtx, PRTFILEAIOREQ pahReqs, size_t cReqs)
{
    /*
     * Parameter validation.
     */
    int rc = VINF_SUCCESS;
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);
    AssertReturn(cReqs > 0,  VERR_INVALID_PARAMETER);
    AssertPtrReturn(pahReqs, VERR_INVALID_POINTER);
    size_t i = cReqs;

    do
    {
        int rcSol = 0;
        size_t cReqsSubmit = 0;
        PRTFILEAIOREQINTERNAL pReqInt;

        while(i-- > 0)
        {
            pReqInt = pahReqs[i];
            if (RTFILEAIOREQ_IS_NOT_VALID(pReqInt))
            {
                /* Undo everything and stop submitting. */
                for (size_t iUndo = 0; iUndo < i; iUndo++)
                {
                    pReqInt = pahReqs[iUndo];
                    RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
                    pReqInt->pCtxInt = NULL;
                }
                rc = VERR_INVALID_HANDLE;
                break;
            }

            pReqInt->PortNotifier.portnfy_port = pCtxInt->iPort;
            pReqInt->pCtxInt                   = pCtxInt;
            RTFILEAIOREQ_SET_STATE(pReqInt, SUBMITTED);

            if (pReqInt->fFlush)
                break;

            cReqsSubmit++;
        }

        if (cReqsSubmit)
        {
            rcSol = lio_listio(LIO_NOWAIT, (struct aiocb **)pahReqs, cReqsSubmit, NULL);
            if (RT_UNLIKELY(rcSol < 0))
            {
                if (rcSol == EAGAIN)
                    rc = VERR_FILE_AIO_INSUFFICIENT_RESSOURCES;
                else
                    rc = RTErrConvertFromErrno(errno);

                /* Check which requests got actually submitted and which not. */
                for (i = 0; i < cReqs; i++)
                {
                    pReqInt = pahReqs[i];
                    rcSol = aio_error(&pReqInt->AioCB);
                    if (rcSol == EINVAL)
                    {
                        /* Was not submitted. */
                        RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
                        pReqInt->pCtxInt = NULL;
                    }
                    else if (rcSol != EINPROGRESS)
                    {
                        /* The request encountered an error. */
                        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
                    }
                }
                break;
            }

            ASMAtomicAddS32(&pCtxInt->cRequests, cReqsSubmit);
            cReqs   -= cReqsSubmit;
            pahReqs += cReqsSubmit;
        }

        if (cReqs)
        {
            pReqInt = pahReqs[0];
            RTFILEAIOREQ_VALID_RETURN(pReqInt);

            /*
             * If there are still requests left we have a flush request.
             * lio_listio does not work with this requests so
             * we have to use aio_fsync directly.
             */
            rcSol = aio_fsync(O_SYNC, &pReqInt->AioCB);
            if (RT_UNLIKELY(rcSol < 0))
            {
                RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
                rc = RTErrConvertFromErrno(errno);
                break;
            }

            ASMAtomicIncS32(&pCtxInt->cRequests);
            cReqs--;
            pahReqs++;
        }
    } while (cReqs);

    return rc;
}

RTDECL(int) RTFileAioCtxWait(RTFILEAIOCTX hAioCtx, size_t cMinReqs, RTMSINTERVAL cMillies,
                             PRTFILEAIOREQ pahReqs, size_t cReqs, uint32_t *pcReqs)
{
    int rc = VINF_SUCCESS;
    int cRequestsCompleted = 0;

    /*
     * Validate the parameters, making sure to always set pcReqs.
     */
    AssertPtrReturn(pcReqs, VERR_INVALID_POINTER);
    *pcReqs = 0; /* always set */
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);
    AssertPtrReturn(pahReqs, VERR_INVALID_POINTER);
    AssertReturn(cReqs != 0, VERR_INVALID_PARAMETER);
    AssertReturn(cReqs >= cMinReqs, VERR_OUT_OF_RANGE);

    if (    RT_UNLIKELY(ASMAtomicReadS32(&pCtxInt->cRequests) == 0)
        && !(pCtxInt->fFlags & RTFILEAIOCTX_FLAGS_WAIT_WITHOUT_PENDING_REQUESTS))
        return VERR_FILE_AIO_NO_REQUEST;

    /*
     * Convert the timeout if specified.
     */
    struct timespec    *pTimeout = NULL;
    struct timespec     Timeout = {0,0};
    uint64_t            StartNanoTS = 0;
    if (cMillies != RT_INDEFINITE_WAIT)
    {
        Timeout.tv_sec  = cMillies / 1000;
        Timeout.tv_nsec = cMillies % 1000 * 1000000;
        pTimeout = &Timeout;
        StartNanoTS = RTTimeNanoTS();
    }

    /* Wait for at least one. */
    if (!cMinReqs)
        cMinReqs = 1;

    while (   cMinReqs
           && RT_SUCCESS_NP(rc))
    {
        port_event_t aPortEvents[AIO_MAXIMUM_REQUESTS_PER_CONTEXT];
        uint_t cRequests    = cMinReqs;
        int cRequestsToWait = RT_MIN(cReqs, AIO_MAXIMUM_REQUESTS_PER_CONTEXT);
        int rcSol;
        uint64_t StartTime;

        rcSol = port_getn(pCtxInt->iPort, &aPortEvents[0], cRequestsToWait, &cRequests, pTimeout);

        if (RT_UNLIKELY(rcSol < 0))
            rc = RTErrConvertFromErrno(errno);

        /* Process received events. */
        for (uint_t i = 0; i < cRequests; i++)
        {
            if (aPortEvents[i].portev_source == PORT_SOURCE_ALERT)
            {
                Assert(aPortEvents[i].portev_events == AIO_CONTEXT_WAKEUP_EVENT);
                rc = VERR_INTERRUPTED; /* We've got interrupted. */
                /* Reset the port. */
                port_alert(pCtxInt->iPort, PORT_ALERT_SET, 0, NULL);
            }
            else
            {
                PRTFILEAIOREQINTERNAL pReqInt = (PRTFILEAIOREQINTERNAL)aPortEvents[i].portev_user;
                AssertPtr(pReqInt);
                Assert(pReqInt->u32Magic == RTFILEAIOREQ_MAGIC);

                /* A request has finished. */
                pahReqs[cRequestsCompleted++] = pReqInt;

                /* Mark the request as finished. */
                RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
            }
        }

        /*
         * Done Yet? If not advance and try again.
         */
        if (cRequests >= cMinReqs)
            break;
        cMinReqs -= cRequests;
        cReqs    -= cRequests;

        if (cMillies != RT_INDEFINITE_WAIT)
        {
            uint64_t NanoTS = RTTimeNanoTS();
            uint64_t cMilliesElapsed = (NanoTS - StartNanoTS) / 1000000;

            /* The syscall supposedly updates it, but we're paranoid. :-) */
            if (cMilliesElapsed < cMillies)
            {
                Timeout.tv_sec  = (cMillies - (RTMSINTERVAL)cMilliesElapsed) / 1000;
                Timeout.tv_nsec = (cMillies - (RTMSINTERVAL)cMilliesElapsed) % 1000 * 1000000;
            }
            else
            {
                Timeout.tv_sec  = 0;
                Timeout.tv_nsec = 0;
            }
        }
    }

    /*
     * Update the context state and set the return value.
     */
    *pcReqs = cRequestsCompleted;
    ASMAtomicSubS32(&pCtxInt->cRequests, cRequestsCompleted);

    return rc;
}

RTDECL(int) RTFileAioCtxWakeup(RTFILEAIOCTX hAioCtx)
{
    int rc = VINF_SUCCESS;
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);

    rc = port_alert(pCtxInt->iPort, PORT_ALERT_UPDATE, AIO_CONTEXT_WAKEUP_EVENT, NULL);
    if (RT_UNLIKELY((rc < 0) && (errno != EBUSY)))
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}

