/* $Id: fileaio-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File async I/O, native implementation for the Linux host platform.
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

/** @page pg_rtfileaio_linux     RTFile Async I/O - Linux Implementation Notes
 * @internal
 *
 * Linux implements the kernel async I/O API through the io_* syscalls. They are
 * not exposed in the glibc (the aio_* API uses userspace threads and blocking
 * I/O operations to simulate async behavior). There is an external library
 * called libaio which implements these syscalls but because we don't want to
 * have another dependency and this library is not installed by default and the
 * interface is really simple we use the kernel interface directly using wrapper
 * functions.
 *
 * The interface has some limitations. The first one is that the file must be
 * opened with O_DIRECT. This disables caching done by the kernel which can be
 * compensated if the user of this API implements caching itself. The next
 * limitation is that data buffers must be aligned at a 512 byte boundary or the
 * request will fail.
 */
/** @todo r=bird: What's this about "must be opened with O_DIRECT"? An
 *        explanation would be nice, esp. seeing what Linus is quoted saying
 *        about it in the open man page... */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FILE
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/thread.h>
#include "internal/fileaio.h"

#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

#include <iprt/file.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** The async I/O context handle */
typedef unsigned long LNXKAIOCONTEXT;

/**
 * Supported commands for the iocbs
 */
enum
{
    LNXKAIO_IOCB_CMD_READ   = 0,
    LNXKAIO_IOCB_CMD_WRITE  = 1,
    LNXKAIO_IOCB_CMD_FSYNC  = 2,
    LNXKAIO_IOCB_CMD_FDSYNC = 3
};

/**
 * The iocb structure of a request which is passed to the kernel.
 *
 * We redefined this here because the version in the header lacks padding
 * for 32bit.
 */
typedef struct LNXKAIOIOCB
{
    /** Opaque pointer to data which is returned on an I/O event. */
    void     *pvUser;
#ifdef RT_ARCH_X86
    uint32_t  u32Padding0;
#endif
    /** Contains the request number and is set by the kernel. */
    uint32_t  u32Key;
    /** Reserved. */
    uint32_t  u32Reserved0;
    /** The I/O opcode. */
    uint16_t  u16IoOpCode;
    /** Request priority. */
    int16_t   i16Priority;
    /** The file descriptor. */
    uint32_t  uFileDesc;
    /** The userspace pointer to the buffer containing/receiving the data. */
    void     *pvBuf;
#ifdef RT_ARCH_X86
    uint32_t  u32Padding1;
#endif
    /** How many bytes to transfer. */
#if ARCH_BITS == 32
    uint32_t  cbTransfer;
    uint32_t  u32Padding2;
#elif ARCH_BITS == 64
    uint64_t  cbTransfer;
#else
# error "Unknown architecture"
#endif
    /** At which offset to start the transfer. */
    int64_t   off;
    /** Reserved. */
    uint64_t  u64Reserved1;
    /** Flags */
    uint32_t  fFlags;
    /** Readyness signal file descriptor. */
    uint32_t  u32ResFd;
} LNXKAIOIOCB, *PLNXKAIOIOCB;

/**
 * I/O event structure to notify about completed requests.
 * Redefined here too because of the padding.
 */
typedef struct LNXKAIOIOEVENT
{
    /** The pvUser field from the iocb. */
    void         *pvUser;
#if ARCH_BITS == 32
    uint32_t      u32Padding0;
#endif
    /** The LNXKAIOIOCB object this event is for. */
    PLNXKAIOIOCB *pIoCB;
#if ARCH_BITS == 32
    uint32_t      u32Padding1;
#endif
    /** The result code of the operation .*/
#if ARCH_BITS == 32
    int32_t       rc;
    uint32_t      u32Padding2;
#elif ARCH_BITS == 64
    int64_t       rc;
#else
# error "Unknown architecture"
#endif
    /** Secondary result code. */
#if ARCH_BITS == 32
    int32_t       rc2;
    uint32_t      u32Padding3;
#elif ARCH_BITS == 64
    int64_t       rc2;
#else
# error "Unknown architecture"
#endif
} LNXKAIOIOEVENT, *PLNXKAIOIOEVENT;


/**
 * Async I/O completion context state.
 */
typedef struct RTFILEAIOCTXINTERNAL
{
    /** Handle to the async I/O context. */
    LNXKAIOCONTEXT      AioContext;
    /** Maximum number of requests this context can handle. */
    int                 cRequestsMax;
    /** Current number of requests active on this context. */
    volatile int32_t    cRequests;
    /** The ID of the thread which is currently waiting for requests. */
    volatile RTTHREAD   hThreadWait;
    /** Flag whether the thread was woken up. */
    volatile bool       fWokenUp;
    /** Flag whether the thread is currently waiting in the syscall. */
    volatile bool       fWaiting;
    /** Flags given during creation. */
    uint32_t            fFlags;
    /** Magic value (RTFILEAIOCTX_MAGIC). */
    uint32_t            u32Magic;
} RTFILEAIOCTXINTERNAL;
/** Pointer to an internal context structure. */
typedef RTFILEAIOCTXINTERNAL *PRTFILEAIOCTXINTERNAL;

/**
 * Async I/O request state.
 */
typedef struct RTFILEAIOREQINTERNAL
{
    /** The aio control block. This must be the FIRST elment in
     *  the structure! (see notes below) */
    LNXKAIOIOCB           AioCB;
    /** Current state the request is in. */
    RTFILEAIOREQSTATE     enmState;
    /** The I/O context this request is associated with. */
    LNXKAIOCONTEXT        AioContext;
    /** Return code the request completed with. */
    int                   Rc;
    /** Number of bytes actually transferred. */
    size_t                cbTransfered;
    /** Completion context we are assigned to. */
    PRTFILEAIOCTXINTERNAL pCtxInt;
    /** Magic value  (RTFILEAIOREQ_MAGIC). */
    uint32_t              u32Magic;
} RTFILEAIOREQINTERNAL;
/** Pointer to an internal request structure. */
typedef RTFILEAIOREQINTERNAL *PRTFILEAIOREQINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max number of events to get in one call. */
#define AIO_MAXIMUM_REQUESTS_PER_CONTEXT 64


/**
 * Creates a new async I/O context.
 */
DECLINLINE(int) rtFileAsyncIoLinuxCreate(unsigned cEvents, LNXKAIOCONTEXT *pAioContext)
{
    int rc = syscall(__NR_io_setup, cEvents, pAioContext);
    if (RT_UNLIKELY(rc == -1))
    {
        if (errno == EAGAIN)
            return VERR_FILE_AIO_INSUFFICIENT_EVENTS;
        else
            return RTErrConvertFromErrno(errno);
    }

    return VINF_SUCCESS;
}

/**
 * Destroys a async I/O context.
 */
DECLINLINE(int) rtFileAsyncIoLinuxDestroy(LNXKAIOCONTEXT AioContext)
{
    int rc = syscall(__NR_io_destroy, AioContext);
    if (RT_UNLIKELY(rc == -1))
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}

/**
 * Submits an array of I/O requests to the kernel.
 */
DECLINLINE(int) rtFileAsyncIoLinuxSubmit(LNXKAIOCONTEXT AioContext, long cReqs, LNXKAIOIOCB **ppIoCB, int *pcSubmitted)
{
    int rc = syscall(__NR_io_submit, AioContext, cReqs, ppIoCB);
    if (RT_UNLIKELY(rc == -1))
        return RTErrConvertFromErrno(errno);

    *pcSubmitted = rc;

    return VINF_SUCCESS;
}

/**
 * Cancels a I/O request.
 */
DECLINLINE(int) rtFileAsyncIoLinuxCancel(LNXKAIOCONTEXT AioContext, PLNXKAIOIOCB pIoCB, PLNXKAIOIOEVENT pIoResult)
{
    int rc = syscall(__NR_io_cancel, AioContext, pIoCB, pIoResult);
    if (RT_UNLIKELY(rc == -1))
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}

/**
 * Waits for I/O events.
 * @returns Number of events (natural number w/ 0), IPRT error code (negative).
 */
DECLINLINE(int) rtFileAsyncIoLinuxGetEvents(LNXKAIOCONTEXT AioContext, long cReqsMin, long cReqs,
                                            PLNXKAIOIOEVENT paIoResults, struct timespec *pTimeout)
{
    int rc = syscall(__NR_io_getevents, AioContext, cReqsMin, cReqs, paIoResults, pTimeout);
    if (RT_UNLIKELY(rc == -1))
        return RTErrConvertFromErrno(errno);

    return rc;
}

RTR3DECL(int) RTFileAioGetLimits(PRTFILEAIOLIMITS pAioLimits)
{
    int rc = VINF_SUCCESS;
    AssertPtrReturn(pAioLimits, VERR_INVALID_POINTER);

    /*
     * Check if the API is implemented by creating a
     * completion port.
     */
    LNXKAIOCONTEXT AioContext = 0;
    rc = rtFileAsyncIoLinuxCreate(1, &AioContext);
    if (RT_FAILURE(rc))
        return rc;

    rc = rtFileAsyncIoLinuxDestroy(AioContext);
    if (RT_FAILURE(rc))
        return rc;

    /* Supported - fill in the limits. The alignment is the only restriction. */
    pAioLimits->cReqsOutstandingMax = RTFILEAIO_UNLIMITED_REQS;
    pAioLimits->cbBufferAlignment   = 512;

    return VINF_SUCCESS;
}


RTR3DECL(int) RTFileAioReqCreate(PRTFILEAIOREQ phReq)
{
    AssertPtrReturn(phReq, VERR_INVALID_POINTER);

    /*
     * Allocate a new request and initialize it.
     */
    PRTFILEAIOREQINTERNAL pReqInt = (PRTFILEAIOREQINTERNAL)RTMemAllocZ(sizeof(*pReqInt));
    if (RT_UNLIKELY(!pReqInt))
        return VERR_NO_MEMORY;

    pReqInt->pCtxInt   = NULL;
    pReqInt->u32Magic  = RTFILEAIOREQ_MAGIC;
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
                                            uint16_t uTransferDirection,
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

    if (uTransferDirection != LNXKAIO_IOCB_CMD_FSYNC)
    {
        AssertPtr(pvBuf);
        Assert(off >= 0);
        Assert(cbTransfer > 0);
    }

    /*
     * Setup the control block and clear the finished flag.
     */
    pReqInt->AioCB.u16IoOpCode = uTransferDirection;
    pReqInt->AioCB.uFileDesc   = RTFileToNative(hFile);
    pReqInt->AioCB.off         = off;
    pReqInt->AioCB.cbTransfer  = cbTransfer;
    pReqInt->AioCB.pvBuf       = pvBuf;
    pReqInt->AioCB.pvUser      = pvUser;

    pReqInt->pCtxInt           = NULL;
    RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);

    return VINF_SUCCESS;
}


RTDECL(int) RTFileAioReqPrepareRead(RTFILEAIOREQ hReq, RTFILE hFile, RTFOFF off,
                                    void *pvBuf, size_t cbRead, void *pvUser)
{
    return rtFileAioReqPrepareTransfer(hReq, hFile, LNXKAIO_IOCB_CMD_READ,
                                       off, pvBuf, cbRead, pvUser);
}


RTDECL(int) RTFileAioReqPrepareWrite(RTFILEAIOREQ hReq, RTFILE hFile, RTFOFF off,
                                     void const *pvBuf, size_t cbWrite, void *pvUser)
{
    return rtFileAioReqPrepareTransfer(hReq, hFile, LNXKAIO_IOCB_CMD_WRITE,
                                       off, (void *)pvBuf, cbWrite, pvUser);
}


RTDECL(int) RTFileAioReqPrepareFlush(RTFILEAIOREQ hReq, RTFILE hFile, void *pvUser)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    AssertReturn(hFile != NIL_RTFILE, VERR_INVALID_HANDLE);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);

    return rtFileAioReqPrepareTransfer(pReqInt, hFile, LNXKAIO_IOCB_CMD_FSYNC,
                                       0, NULL, 0, pvUser);
}


RTDECL(void *) RTFileAioReqGetUser(RTFILEAIOREQ hReq)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN_RC(pReqInt, NULL);

    return pReqInt->AioCB.pvUser;
}


RTDECL(int) RTFileAioReqCancel(RTFILEAIOREQ hReq)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_NOT_SUBMITTED);

    LNXKAIOIOEVENT AioEvent;
    int rc = rtFileAsyncIoLinuxCancel(pReqInt->AioContext, &pReqInt->AioCB, &AioEvent);
    if (RT_SUCCESS(rc))
    {
        /*
         * Decrement request count because the request will never arrive at the
         * completion port.
         */
        AssertMsg(RT_VALID_PTR(pReqInt->pCtxInt), ("Invalid state. Request was canceled but wasn't submitted\n"));

        ASMAtomicDecS32(&pReqInt->pCtxInt->cRequests);
        pReqInt->Rc = VERR_FILE_AIO_CANCELED;
        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
        return VINF_SUCCESS;
    }
    if (rc == VERR_TRY_AGAIN)
        return VERR_FILE_AIO_IN_PROGRESS;
    return rc;
}


RTDECL(int) RTFileAioReqGetRC(RTFILEAIOREQ hReq, size_t *pcbTransfered)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    AssertPtrNull(pcbTransfered);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, PREPARED, VERR_FILE_AIO_NOT_SUBMITTED);

    if (    pcbTransfered
        &&  RT_SUCCESS(pReqInt->Rc))
        *pcbTransfered = pReqInt->cbTransfered;

    return pReqInt->Rc;
}


RTDECL(int) RTFileAioCtxCreate(PRTFILEAIOCTX phAioCtx, uint32_t cAioReqsMax,
                               uint32_t fFlags)
{
    PRTFILEAIOCTXINTERNAL pCtxInt;
    AssertPtrReturn(phAioCtx, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTFILEAIOCTX_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    /* The kernel interface needs a maximum. */
    if (cAioReqsMax == RTFILEAIO_UNLIMITED_REQS)
        return VERR_OUT_OF_RANGE;

    pCtxInt = (PRTFILEAIOCTXINTERNAL)RTMemAllocZ(sizeof(RTFILEAIOCTXINTERNAL));
    if (RT_UNLIKELY(!pCtxInt))
        return VERR_NO_MEMORY;

    /* Init the event handle. */
    int rc = rtFileAsyncIoLinuxCreate(cAioReqsMax, &pCtxInt->AioContext);
    if (RT_SUCCESS(rc))
    {
        pCtxInt->fWokenUp     = false;
        pCtxInt->fWaiting     = false;
        pCtxInt->hThreadWait  = NIL_RTTHREAD;
        pCtxInt->cRequestsMax = cAioReqsMax;
        pCtxInt->fFlags       = fFlags;
        pCtxInt->u32Magic     = RTFILEAIOCTX_MAGIC;
        *phAioCtx = (RTFILEAIOCTX)pCtxInt;
    }
    else
        RTMemFree(pCtxInt);

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

    /* The native bit first, then mark it as dead and free it. */
    int rc = rtFileAsyncIoLinuxDestroy(pCtxInt->AioContext);
    if (RT_FAILURE(rc))
        return rc;
    ASMAtomicUoWriteU32(&pCtxInt->u32Magic, RTFILEAIOCTX_MAGIC_DEAD);
    RTMemFree(pCtxInt);

    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTFileAioCtxGetMaxReqCount(RTFILEAIOCTX hAioCtx)
{
    /* Nil means global here. */
    if (hAioCtx == NIL_RTFILEAIOCTX)
        return RTFILEAIO_UNLIMITED_REQS; /** @todo r=bird: I'm a bit puzzled by this return value since it
                                          *                is completely useless in RTFileAioCtxCreate. */

    /* Return 0 if the handle is invalid, it's better than garbage I think... */
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN_RC(pCtxInt, 0);

    return pCtxInt->cRequestsMax;
}

RTDECL(int) RTFileAioCtxAssociateWithFile(RTFILEAIOCTX hAioCtx, RTFILE hFile)
{
    /* Nothing to do. */
    NOREF(hAioCtx); NOREF(hFile);
    return VINF_SUCCESS;
}

RTDECL(int) RTFileAioCtxSubmit(RTFILEAIOCTX hAioCtx, PRTFILEAIOREQ pahReqs, size_t cReqs)
{
    int rc = VINF_SUCCESS;

    /*
     * Parameter validation.
     */
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);
    AssertReturn(cReqs > 0,  VERR_INVALID_PARAMETER);
    AssertPtrReturn(pahReqs, VERR_INVALID_POINTER);
    uint32_t i = cReqs;
    PRTFILEAIOREQINTERNAL pReqInt = NULL;

    /*
     * Validate requests and associate with the context.
     */
    while (i-- > 0)
    {
        pReqInt = pahReqs[i];
        if (RTFILEAIOREQ_IS_NOT_VALID(pReqInt))
        {
            /* Undo everything and stop submitting. */
            size_t iUndo = cReqs;
            while (iUndo-- > i)
            {
                pReqInt = pahReqs[iUndo];
                RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
                pReqInt->pCtxInt = NULL;
            }
            return VERR_INVALID_HANDLE;
        }

        pReqInt->AioContext = pCtxInt->AioContext;
        pReqInt->pCtxInt    = pCtxInt;
        RTFILEAIOREQ_SET_STATE(pReqInt, SUBMITTED);
    }

    do
    {
        /*
         * We cast pahReqs to the Linux iocb structure to avoid copying the requests
         * into a temporary array. This is possible because the iocb structure is
         * the first element in the request structure (see PRTFILEAIOCTXINTERNAL).
         */
        int cReqsSubmitted = 0;
        rc = rtFileAsyncIoLinuxSubmit(pCtxInt->AioContext, cReqs,
                                      (PLNXKAIOIOCB *)pahReqs,
                                      &cReqsSubmitted);
        if (RT_FAILURE(rc))
        {
            /*
             * We encountered an error.
             * This means that the first IoCB
             * is not correctly initialized
             * (invalid buffer alignment or bad file descriptor).
             * Revert every request into the prepared state except
             * the first one which will switch to completed.
             * Another reason could be insufficient resources.
             */
            i = cReqs;
            while (i-- > 0)
            {
                /* Already validated. */
                pReqInt = pahReqs[i];
                pReqInt->pCtxInt    = NULL;
                pReqInt->AioContext = 0;
                RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
            }

            if (rc == VERR_TRY_AGAIN)
                return VERR_FILE_AIO_INSUFFICIENT_RESSOURCES;
            else
            {
                /* The first request failed. */
                pReqInt = pahReqs[0];
                RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
                pReqInt->Rc = rc;
                pReqInt->cbTransfered = 0;
                return rc;
            }
        }

        /* Advance. */
        cReqs   -= cReqsSubmitted;
        pahReqs += cReqsSubmitted;
        ASMAtomicAddS32(&pCtxInt->cRequests, cReqsSubmitted);

    } while (cReqs);

    return rc;
}


RTDECL(int) RTFileAioCtxWait(RTFILEAIOCTX hAioCtx, size_t cMinReqs, RTMSINTERVAL cMillies,
                             PRTFILEAIOREQ pahReqs, size_t cReqs, uint32_t *pcReqs)
{
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

    /*
     * Can't wait if there are not requests around.
     */
    if (   RT_UNLIKELY(ASMAtomicUoReadS32(&pCtxInt->cRequests) == 0)
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

    /*
     * Loop until we're woken up, hit an error (incl timeout), or
     * have collected the desired number of requests.
     */
    int rc = VINF_SUCCESS;
    int cRequestsCompleted = 0;
    while (!pCtxInt->fWokenUp)
    {
        LNXKAIOIOEVENT  aPortEvents[AIO_MAXIMUM_REQUESTS_PER_CONTEXT];
        int             cRequestsToWait = RT_MIN(cReqs, AIO_MAXIMUM_REQUESTS_PER_CONTEXT);
        ASMAtomicXchgBool(&pCtxInt->fWaiting, true);
        rc = rtFileAsyncIoLinuxGetEvents(pCtxInt->AioContext, cMinReqs, cRequestsToWait, &aPortEvents[0], pTimeout);
        ASMAtomicXchgBool(&pCtxInt->fWaiting, false);
        if (RT_FAILURE(rc))
            break;
        uint32_t const cDone = rc;
        rc = VINF_SUCCESS;

        /*
         * Process received events / requests.
         */
        for (uint32_t i = 0; i < cDone; i++)
        {
            /*
             * The iocb is the first element in our request structure.
             * So we can safely cast it directly to the handle (see above)
             */
            PRTFILEAIOREQINTERNAL pReqInt = (PRTFILEAIOREQINTERNAL)aPortEvents[i].pIoCB;
            AssertPtr(pReqInt);
            Assert(pReqInt->u32Magic == RTFILEAIOREQ_MAGIC);

            /** @todo aeichner: The rc field contains the result code
             *  like you can find in errno for the normal read/write ops.
             *  But there is a second field called rc2. I don't know the
             *  purpose for it yet.
             */
            if (RT_UNLIKELY(aPortEvents[i].rc < 0))
                pReqInt->Rc = RTErrConvertFromErrno(-aPortEvents[i].rc); /* Convert to positive value. */
            else
            {
                pReqInt->Rc = VINF_SUCCESS;
                pReqInt->cbTransfered = aPortEvents[i].rc;
            }

            /* Mark the request as finished. */
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

    bool fWokenUp    = ASMAtomicXchgBool(&pCtxInt->fWokenUp, true);

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
         * rtFileAsyncIoLinuxGetEvents() before the signal
         * is send.
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

