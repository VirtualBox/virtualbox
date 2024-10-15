/* $Id: fileaio-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File async I/O, native implementation for the Windows host platform.
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
#define LOG_GROUP RTLOGGROUP_DIR

#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/fileaio.h"

#include <iprt/win/windows.h>

#include "internal-r3-win.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Transfer direction.
 */
typedef enum TRANSFERDIRECTION
{
    TRANSFERDIRECTION_INVALID = 0,
    /** Read. */
    TRANSFERDIRECTION_READ,
    /** Write. */
    TRANSFERDIRECTION_WRITE,
    /** The usual 32-bit hack. */
    TRANSFERDIRECTION_32BIT_HACK = 0x7fffffff
} TRANSFERDIRECTION;

/**
 * Async I/O completion context state.
 */
typedef struct RTFILEAIOCTXINTERNAL
{
    /** handle to I/O completion port. */
    HANDLE            hIoCompletionPort;
    /** Current number of requests pending. */
    volatile int32_t  cRequests;
    /** Flag whether the thread was woken up. */
    volatile bool     fWokenUp;
    /** Flag whether the thread is currently waiting. */
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
    /** Overlapped structure. */
    OVERLAPPED            Overlapped;
    /** Current state the request is in. */
    RTFILEAIOREQSTATE     enmState;
    /** The file handle. */
    HANDLE                hFile;
    /** Kind of transfer Read/Write. */
    TRANSFERDIRECTION     enmTransferDirection;
    /** Number of bytes to transfer. */
    size_t                cbTransfer;
    /** Pointer to the buffer. */
    void                 *pvBuf;
    /** Opaque user data. */
    void                 *pvUser;
    /** Flag whether the request completed. */
    bool                  fCompleted;
    /** Number of bytes transferred successfully. */
    size_t                cbTransfered;
    /** Error code of the completed request. */
    int                   Rc;
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
/** Id for the wakeup event. */
#define AIO_CONTEXT_WAKEUP_EVENT 1
/** Converts a pointer to an OVERLAPPED structure to a internal request. */
#define OVERLAPPED_2_RTFILEAIOREQINTERNAL(pOverlapped) ( (PRTFILEAIOREQINTERNAL)((uintptr_t)(pOverlapped) - RT_UOFFSETOF(RTFILEAIOREQINTERNAL, Overlapped)) )

RTR3DECL(int) RTFileAioGetLimits(PRTFILEAIOLIMITS pAioLimits)
{
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

    pReqInt->pCtxInt    = NULL;
    pReqInt->fCompleted = false;
    pReqInt->u32Magic   = RTFILEAIOREQ_MAGIC;
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
                                            TRANSFERDIRECTION enmTransferDirection,
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

    pReqInt->enmTransferDirection  = enmTransferDirection;
    pReqInt->hFile                 = (HANDLE)RTFileToNative(hFile);
    pReqInt->Overlapped.Offset     = (DWORD)(off & 0xffffffff);
    pReqInt->Overlapped.OffsetHigh = (DWORD)(off >> 32);
    pReqInt->cbTransfer            = cbTransfer;
    pReqInt->pvBuf                 = pvBuf;
    pReqInt->pvUser                = pvUser;
    pReqInt->fCompleted            = false;

    return VINF_SUCCESS;
}

RTDECL(int) RTFileAioReqPrepareRead(RTFILEAIOREQ hReq, RTFILE hFile, RTFOFF off,
                                    void *pvBuf, size_t cbRead, void *pvUser)
{
    return rtFileAioReqPrepareTransfer(hReq, hFile, TRANSFERDIRECTION_READ,
                                       off, pvBuf, cbRead, pvUser);
}

RTDECL(int) RTFileAioReqPrepareWrite(RTFILEAIOREQ hReq, RTFILE hFile, RTFOFF off,
                                     void const *pvBuf, size_t cbWrite, void *pvUser)
{
    return rtFileAioReqPrepareTransfer(hReq, hFile, TRANSFERDIRECTION_WRITE,
                                       off, (void *)pvBuf, cbWrite, pvUser);
}

RTDECL(int) RTFileAioReqPrepareFlush(RTFILEAIOREQ hReq, RTFILE hFile, void *pvUser)
{
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    AssertReturn(hFile != NIL_RTFILE, VERR_INVALID_HANDLE);
    RT_NOREF_PV(pvUser);

    return VERR_NOT_SUPPORTED;
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

    /**
     * @todo r=aeichner It is not possible to cancel specific
     * requests on Windows before Vista.
     * CancelIo cancels all requests for a file issued by the
     * calling thread and CancelIoEx which does what we need
     * is only available from Vista and up.
     * The solution is to return VERR_FILE_AIO_IN_PROGRESS
     * if the request didn't completed yet (checked above).
     * Shouldn't be a big issue because a request is normally
     * only canceled if it exceeds a timeout which is quite huge.
     */
    return VERR_FILE_AIO_COMPLETED;
}

RTDECL(int) RTFileAioReqGetRC(RTFILEAIOREQ hReq, size_t *pcbTransfered)
{
    int rc = VINF_SUCCESS;
    PRTFILEAIOREQINTERNAL pReqInt = hReq;
    RTFILEAIOREQ_VALID_RETURN(pReqInt);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, PREPARED, VERR_FILE_AIO_NOT_SUBMITTED);

    rc = pReqInt->Rc;
    if (pcbTransfered && RT_SUCCESS(rc))
        *pcbTransfered = pReqInt->cbTransfered;

    return rc;
}

RTDECL(int) RTFileAioCtxCreate(PRTFILEAIOCTX phAioCtx, uint32_t cAioReqsMax, uint32_t fFlags)
{
    AssertPtrReturn(phAioCtx, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTFILEAIOCTX_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    RT_NOREF_PV(cAioReqsMax);

    if (   g_pfnCreateIoCompletionPort
        && g_pfnGetQueuedCompletionStatus
        && g_pfnPostQueuedCompletionStatus)
    {
        PRTFILEAIOCTXINTERNAL pCtxInt = (PRTFILEAIOCTXINTERNAL)RTMemAllocZ(sizeof(RTFILEAIOCTXINTERNAL));
        if (RT_UNLIKELY(!pCtxInt))
            return VERR_NO_MEMORY;

        pCtxInt->hIoCompletionPort = g_pfnCreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (RT_UNLIKELY(!pCtxInt->hIoCompletionPort))
        {
            RTMemFree(pCtxInt);
            return VERR_NO_MEMORY;
        }

        pCtxInt->fFlags   = fFlags;
        pCtxInt->u32Magic = RTFILEAIOCTX_MAGIC;

        *phAioCtx = (RTFILEAIOCTX)pCtxInt;

        return VINF_SUCCESS;
    }
    return VERR_NOT_SUPPORTED;
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

    CloseHandle(pCtxInt->hIoCompletionPort);
    ASMAtomicUoWriteU32(&pCtxInt->u32Magic, RTFILEAIOCTX_MAGIC_DEAD);
    RTMemFree(pCtxInt);

    return VINF_SUCCESS;
}

RTDECL(int) RTFileAioCtxAssociateWithFile(RTFILEAIOCTX hAioCtx, RTFILE hFile)
{
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);

    if (   g_pfnCreateIoCompletionPort
        && g_pfnGetQueuedCompletionStatus
        && g_pfnPostQueuedCompletionStatus)
    {
        int rc = VINF_SUCCESS;
        HANDLE hTemp = g_pfnCreateIoCompletionPort((HANDLE)RTFileToNative(hFile), pCtxInt->hIoCompletionPort, 0, 1);
        if (hTemp != pCtxInt->hIoCompletionPort)
            rc = RTErrConvertFromWin32(GetLastError());

        return rc;
    }
    return VERR_NOT_SUPPORTED;
}

RTDECL(uint32_t) RTFileAioCtxGetMaxReqCount(RTFILEAIOCTX hAioCtx)
{
    RT_NOREF_PV(hAioCtx);
    return RTFILEAIO_UNLIMITED_REQS;
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
    Assert(cReqs <= INT32_MAX);
    AssertPtrReturn(pahReqs, VERR_INVALID_POINTER);
    size_t i;

    for (i = 0; i < cReqs; i++)
    {
        PRTFILEAIOREQINTERNAL pReqInt = pahReqs[i];
        BOOL fSucceeded;

        Assert(pReqInt->cbTransfer == (DWORD)pReqInt->cbTransfer);
        if (pReqInt->enmTransferDirection == TRANSFERDIRECTION_READ)
        {
            fSucceeded = ReadFile(pReqInt->hFile, pReqInt->pvBuf,
                                  (DWORD)pReqInt->cbTransfer, NULL,
                                  &pReqInt->Overlapped);
        }
        else if (pReqInt->enmTransferDirection == TRANSFERDIRECTION_WRITE)
        {
            fSucceeded = WriteFile(pReqInt->hFile, pReqInt->pvBuf,
                                   (DWORD)pReqInt->cbTransfer, NULL,
                                   &pReqInt->Overlapped);
        }
        else
        {
            fSucceeded = false;
            AssertMsgFailed(("Invalid transfer direction\n"));
        }

        if (RT_UNLIKELY(!fSucceeded && GetLastError() != ERROR_IO_PENDING))
        {
            RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
            rc = RTErrConvertFromWin32(GetLastError());
            pReqInt->Rc = rc;
            break;
        }
        RTFILEAIOREQ_SET_STATE(pReqInt, SUBMITTED);
    }

    ASMAtomicAddS32(&pCtxInt->cRequests, (int32_t)i);

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
     * Can't wait if there are no requests around.
     */
    if (   RT_UNLIKELY(ASMAtomicUoReadS32(&pCtxInt->cRequests) == 0)
        && !(pCtxInt->fFlags & RTFILEAIOCTX_FLAGS_WAIT_WITHOUT_PENDING_REQUESTS))
        return VERR_FILE_AIO_NO_REQUEST;

    /* Wait for at least one. */
    if (!cMinReqs)
        cMinReqs = 1;

    /*
     * Loop until we're woken up, hit an error (incl timeout), or
     * have collected the desired number of requests.
     */
    int rc = VINF_SUCCESS;
    int cRequestsCompleted = 0;
    while (   !pCtxInt->fWokenUp
           && cMinReqs > 0)
    {
        uint64_t     StartNanoTS = 0;
        DWORD        dwTimeout = cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies;
        DWORD        cbTransfered;
        LPOVERLAPPED pOverlapped;
        ULONG_PTR    lCompletionKey;
        BOOL         fSucceeded;

        if (cMillies != RT_INDEFINITE_WAIT)
            StartNanoTS = RTTimeNanoTS();

        ASMAtomicXchgBool(&pCtxInt->fWaiting, true);
        fSucceeded = g_pfnGetQueuedCompletionStatus(pCtxInt->hIoCompletionPort,
                                                    &cbTransfered,
                                                    &lCompletionKey,
                                                    &pOverlapped,
                                                    dwTimeout);
        ASMAtomicXchgBool(&pCtxInt->fWaiting, false);
        if (   !fSucceeded
            && !pOverlapped)
        {
            /* The call failed to dequeue a completion packet, includes VERR_TIMEOUT */
            rc = RTErrConvertFromWin32(GetLastError());
            break;
        }

        /* Check if we got woken up. */
        if (lCompletionKey == AIO_CONTEXT_WAKEUP_EVENT)
        {
            Assert(fSucceeded && !pOverlapped);
            break;
        }

        /* A request completed. */
        PRTFILEAIOREQINTERNAL pReqInt = OVERLAPPED_2_RTFILEAIOREQINTERNAL(pOverlapped);
        AssertPtr(pReqInt);
        Assert(pReqInt->u32Magic == RTFILEAIOREQ_MAGIC);

        /* Mark the request as finished. */
        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);

        pReqInt->cbTransfered = cbTransfered;
        if (fSucceeded)
            pReqInt->Rc = VINF_SUCCESS;
        else
        {
            DWORD errCode = GetLastError();
            pReqInt->Rc = RTErrConvertFromWin32(errCode);
            if (pReqInt->Rc == VERR_UNRESOLVED_ERROR)
                LogRel(("AIO/win: Request %#p returned rc=%Rrc (native %u\n)", pReqInt, pReqInt->Rc, errCode));
        }

        pahReqs[cRequestsCompleted++] = (RTFILEAIOREQ)pReqInt;

        /* Update counter. */
        cMinReqs--;

        if (cMillies != RT_INDEFINITE_WAIT)
        {
            /* Recalculate timeout. */
            uint64_t NanoTS = RTTimeNanoTS();
            uint64_t cMilliesElapsed = (NanoTS - StartNanoTS) / 1000000;
            if (cMilliesElapsed < cMillies)
                cMillies -= cMilliesElapsed;
            else
                cMillies = 0;
        }
    }

    /*
     * Update the context state and set the return value.
     */
    *pcReqs = cRequestsCompleted;
    ASMAtomicSubS32(&pCtxInt->cRequests, cRequestsCompleted);

    /*
     * Clear the wakeup flag and set rc.
     */
    bool fWokenUp = ASMAtomicXchgBool(&pCtxInt->fWokenUp, false);

    if (    fWokenUp
        &&  RT_SUCCESS(rc))
        rc = VERR_INTERRUPTED;

    return rc;
}

RTDECL(int) RTFileAioCtxWakeup(RTFILEAIOCTX hAioCtx)
{
    int rc = VINF_SUCCESS;
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);

    bool fWokenUp = ASMAtomicXchgBool(&pCtxInt->fWokenUp, true);
    bool fWaiting = ASMAtomicReadBool(&pCtxInt->fWaiting);

    if (   !fWokenUp
        && fWaiting)
    {
        BOOL fSucceeded = g_pfnPostQueuedCompletionStatus(pCtxInt->hIoCompletionPort,
                                                          0, AIO_CONTEXT_WAKEUP_EVENT,
                                                          NULL);

        if (!fSucceeded)
            rc = RTErrConvertFromWin32(GetLastError());
    }

    return rc;
}

