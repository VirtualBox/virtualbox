/* $Id: fileaio-freebsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File async I/O, native implementation for the FreeBSD host platform.
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
#include <iprt/thread.h>
#include "internal/fileaio.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <aio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Async I/O completion context state.
 */
typedef struct RTFILEAIOCTXINTERNAL
{
    /** Handle to the kernel queue. */
    int               iKQueue;
    /** Current number of requests active on this context. */
    volatile int32_t  cRequests;
    /** The ID of the thread which is currently waiting for requests. */
    volatile RTTHREAD hThreadWait;
    /** Flag whether the thread was woken up. */
    volatile bool     fWokenUp;
    /** Flag whether the thread is currently waiting in the syscall. */
    volatile bool     fWaiting;
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
    /** Opaque user data. */
    void                  *pvUser;
    /** Completion context we are assigned to. */
    PRTFILEAIOCTXINTERNAL  pCtxInt;
    /** Number of bytes actually transferred. */
    size_t                 cbTransfered;
    /** Status code. */
    int                    Rc;
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

RTR3DECL(int) RTFileAioGetLimits(PRTFILEAIOLIMITS pAioLimits)
{
    int rcBSD = 0;
    AssertPtrReturn(pAioLimits, VERR_INVALID_POINTER);

    /*
     * The AIO API is implemented in a kernel module which is not
     * loaded by default.
     * If it is loaded there are additional sysctl parameters.
     */
    int cReqsOutstandingMax = 0;
    size_t cbParameter = sizeof(int);

    rcBSD = sysctlbyname("vfs.aio.max_aio_per_proc", /* name */
                         &cReqsOutstandingMax,       /* Where to store the old value. */
                         &cbParameter,               /* Size of the memory pointed to. */
                         NULL,                       /* Where the new value is located. */
                         0);                         /* Where the size of the new value is stored. */
    if (rcBSD == -1)
    {
        /* ENOENT means the value is unknown thus the module is not loaded. */
        if (errno == ENOENT)
            return VERR_NOT_SUPPORTED;
        else
            return RTErrConvertFromErrno(errno);
    }

    pAioLimits->cReqsOutstandingMax = cReqsOutstandingMax;
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
    pReqInt->AioCB.aio_sigevent.sigev_notify = SIGEV_KEVENT;
    pReqInt->AioCB.aio_sigevent.sigev_value.sival_ptr = pReqInt;
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

    pReqInt->AioCB.aio_sigevent.sigev_notify = SIGEV_KEVENT;
    pReqInt->AioCB.aio_sigevent.sigev_value.sival_ptr = pReqInt;
    pReqInt->AioCB.aio_lio_opcode = uTransferDirection;
    pReqInt->AioCB.aio_fildes     = RTFileToNative(hFile);
    pReqInt->AioCB.aio_offset     = off;
    pReqInt->AioCB.aio_nbytes     = cbTransfer;
    pReqInt->AioCB.aio_buf        = pvBuf;
    pReqInt->fFlush               = false;
    pReqInt->pvUser               = pvUser;
    pReqInt->pCtxInt              = NULL;
    pReqInt->Rc                   = VERR_FILE_AIO_IN_PROGRESS;
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
    Assert(hFile != NIL_RTFILE);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);

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


    int rcBSD = aio_cancel(pReqInt->AioCB.aio_fildes, &pReqInt->AioCB);

    if (rcBSD == AIO_CANCELED)
    {
        /*
         * Decrement request count because the request will never arrive at the
         * completion port.
         */
        AssertMsg(RT_VALID_PTR(pReqInt->pCtxInt),
                  ("Invalid state. Request was canceled but wasn't submitted\n"));

        ASMAtomicDecS32(&pReqInt->pCtxInt->cRequests);
        pReqInt->Rc = VERR_FILE_AIO_CANCELED;
        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
        return VINF_SUCCESS;
    }
    else if (rcBSD == AIO_ALLDONE)
        return VERR_FILE_AIO_COMPLETED;
    else if (rcBSD == AIO_NOTCANCELED)
        return VERR_FILE_AIO_IN_PROGRESS;
    else
        return RTErrConvertFromErrno(errno);
}

RTDECL(int) RTFileAioReqGetRC(RTFILEAIOREQ hReq, size_t *pcbTransfered)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    AssertPtrNull(pcbTransfered);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, PREPARED, VERR_FILE_AIO_NOT_SUBMITTED);

    if (  (RT_SUCCESS(pReqInt->Rc))
        && (pcbTransfered))
        *pcbTransfered = pReqInt->cbTransfered;

    return pReqInt->Rc;
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
    pCtxInt->iKQueue = kqueue();
    if (RT_LIKELY(pCtxInt->iKQueue > 0))
    {
        pCtxInt->fFlags       = fFlags;
        pCtxInt->u32Magic     = RTFILEAIOCTX_MAGIC;
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

    close(pCtxInt->iKQueue);
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

    do
    {
        int rcBSD = 0;
        size_t cReqsSubmit = 0;
        size_t i = 0;
        PRTFILEAIOREQINTERNAL pReqInt;

        while (   (i < cReqs)
               && (i < AIO_LISTIO_MAX))
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
                    pReqInt->AioCB.aio_sigevent.sigev_notify_kqueue = 0;
                }
                rc = VERR_INVALID_HANDLE;
                break;
            }

            pReqInt->AioCB.aio_sigevent.sigev_notify_kqueue = pCtxInt->iKQueue;
            pReqInt->pCtxInt                                = pCtxInt;
            RTFILEAIOREQ_SET_STATE(pReqInt, SUBMITTED);

            if (pReqInt->fFlush)
                break;

            cReqsSubmit++;
            i++;
        }

        if (cReqsSubmit)
        {
            rcBSD = lio_listio(LIO_NOWAIT, (struct aiocb **)pahReqs, cReqsSubmit, NULL);
            if (RT_UNLIKELY(rcBSD < 0))
            {
                if (errno == EAGAIN)
                    rc = VERR_FILE_AIO_INSUFFICIENT_RESSOURCES;
                else
                    rc = RTErrConvertFromErrno(errno);

                /* Check which requests got actually submitted and which not. */
                for (i = 0; i < cReqs; i++)
                {
                    pReqInt = pahReqs[i];
                    rcBSD = aio_error(&pReqInt->AioCB);
                    if (   rcBSD == -1
                        && errno == EINVAL)
                    {
                        /* Was not submitted. */
                        RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
                        pReqInt->pCtxInt = NULL;
                    }
                    else if (rcBSD != EINPROGRESS)
                    {
                        /* The request encountered an error. */
                        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
                        pReqInt->Rc = RTErrConvertFromErrno(rcBSD);
                        pReqInt->pCtxInt      = NULL;
                        pReqInt->cbTransfered = 0;
                    }
                }
                break;
            }

            ASMAtomicAddS32(&pCtxInt->cRequests, cReqsSubmit);
            cReqs   -= cReqsSubmit;
            pahReqs += cReqsSubmit;
        }

        /* Check if we have a flush request now. */
        if (cReqs && RT_SUCCESS_NP(rc))
        {
            pReqInt = pahReqs[0];
            RTFILEAIOREQ_VALID_RETURN(pReqInt);

            if (pReqInt->fFlush)
            {
                /*
                 * lio_listio does not work with flush requests so
                 * we have to use aio_fsync directly.
                 */
                 rcBSD = aio_fsync(O_SYNC, &pReqInt->AioCB);
                 if (RT_UNLIKELY(rcBSD < 0))
                 {
                    if (rcBSD == EAGAIN)
                    {
                        /* Was not submitted. */
                        RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
                        pReqInt->pCtxInt = NULL;
                        return VERR_FILE_AIO_INSUFFICIENT_RESSOURCES;
                    }
                    else
                    {
                        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
                        pReqInt->Rc = RTErrConvertFromErrno(errno);
                        pReqInt->cbTransfered = 0;
                        return pReqInt->Rc;
                    }
                 }

                ASMAtomicIncS32(&pCtxInt->cRequests);
                cReqs--;
                pahReqs++;
            }
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

    if (   RT_UNLIKELY(ASMAtomicReadS32(&pCtxInt->cRequests) == 0)
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

    /* For the wakeup call. */
    Assert(pCtxInt->hThreadWait == NIL_RTTHREAD);
    ASMAtomicWriteHandle(&pCtxInt->hThreadWait, RTThreadSelf());

    while (   cMinReqs
           && RT_SUCCESS_NP(rc))
    {
        struct kevent aKEvents[AIO_MAXIMUM_REQUESTS_PER_CONTEXT];
        int cRequestsToWait = cMinReqs < AIO_MAXIMUM_REQUESTS_PER_CONTEXT ? cReqs : AIO_MAXIMUM_REQUESTS_PER_CONTEXT;
        int rcBSD;
        uint64_t StartTime;

        ASMAtomicXchgBool(&pCtxInt->fWaiting, true);
        rcBSD = kevent(pCtxInt->iKQueue, NULL, 0, aKEvents, cRequestsToWait, pTimeout);
        ASMAtomicXchgBool(&pCtxInt->fWaiting, false);

        if (RT_UNLIKELY(rcBSD < 0))
        {
            rc = RTErrConvertFromErrno(errno);
            break;
        }

        uint32_t const cDone = rcBSD;

        /* Process received events. */
        for (uint32_t i = 0; i < cDone; i++)
        {
            PRTFILEAIOREQINTERNAL pReqInt = (PRTFILEAIOREQINTERNAL)aKEvents[i].udata;
            AssertPtr(pReqInt);
            Assert(pReqInt->u32Magic == RTFILEAIOREQ_MAGIC);

            /*
             * Retrieve the status code here already because the
             * user may omit the RTFileAioReqGetRC() call and
             * we will leak kernel resources then.
             * This will result in errors during submission
             * of other requests as soon as the max_aio_queue_per_proc
             * limit is reached.
             */
            int cbTransfered = aio_return(&pReqInt->AioCB);

            if (cbTransfered < 0)
            {
                pReqInt->Rc = RTErrConvertFromErrno(cbTransfered);
                pReqInt->cbTransfered = 0;
            }
            else
            {
                pReqInt->Rc = VINF_SUCCESS;
                pReqInt->cbTransfered = cbTransfered;
            }
            RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
            pahReqs[cRequestsCompleted++] = (RTFILEAIOREQ)pReqInt;
        }

        /*
         * Done Yet? If not advance and try again.
         */
        if (cDone >= cMinReqs)
            break;
        cMinReqs -= cDone;
        cReqs    -= cDone;

        if (cMillies != RT_INDEFINITE_WAIT)
        {
            /* The API doesn't return ETIMEDOUT, so we have to fix that ourselves. */
            uint64_t NanoTS = RTTimeNanoTS();
            uint64_t cMilliesElapsed = (NanoTS - StartNanoTS) / 1000000;
            if (cMilliesElapsed >= cMillies)
            {
                rc = VERR_TIMEOUT;
                break;
            }

            /* The syscall supposedly updates it, but we're paranoid. :-) */
            Timeout.tv_sec  = (cMillies - (RTMSINTERVAL)cMilliesElapsed) / 1000;
            Timeout.tv_nsec = (cMillies - (RTMSINTERVAL)cMilliesElapsed) % 1000 * 1000000;
        }
    }

    /*
     * Update the context state and set the return value.
     */
    *pcReqs = cRequestsCompleted;
    ASMAtomicSubS32(&pCtxInt->cRequests, cRequestsCompleted);
    Assert(pCtxInt->hThreadWait == RTThreadSelf());
    ASMAtomicWriteHandle(&pCtxInt->hThreadWait, NIL_RTTHREAD);

    /*
     * Clear the wakeup flag and set rc.
     */
    if (    pCtxInt->fWokenUp
        &&  RT_SUCCESS(rc))
    {
        ASMAtomicXchgBool(&pCtxInt->fWokenUp, false);
        rc = VERR_INTERRUPTED;
    }

    return rc;
}

RTDECL(int) RTFileAioCtxWakeup(RTFILEAIOCTX hAioCtx)
{
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);

    /** @todo r=bird: Define the protocol for how to resume work after calling
     *        this function. */

    bool fWokenUp = ASMAtomicXchgBool(&pCtxInt->fWokenUp, true);

    /*
     * Read the thread handle before the status flag.
     * If we read the handle after the flag we might
     * end up with an invalid handle because the thread
     * waiting in RTFileAioCtxWakeup() might get scheduled
     * before we read the flag and returns.
     * We can ensure that the handle is valid if fWaiting is true
     * when reading the handle before the status flag.
     */
    RTTHREAD hThread;
    ASMAtomicReadHandle(&pCtxInt->hThreadWait, &hThread);
    bool fWaiting    = ASMAtomicReadBool(&pCtxInt->fWaiting);
    if (    !fWokenUp
        &&  fWaiting)
    {
        /*
         * If a thread waits the handle must be valid.
         * It is possible that the thread returns from
         * kevent() before the signal is send.
         * This is no problem because we already set fWokenUp
         * to true which will let the thread return VERR_INTERRUPTED
         * and the next call to RTFileAioCtxWait() will not
         * return VERR_INTERRUPTED because signals are not saved
         * and will simply vanish if the destination thread can't
         * receive it.
         */
        Assert(hThread != NIL_RTTHREAD);
        RTThreadPoke(hThread);
    }

    return VINF_SUCCESS;
}

