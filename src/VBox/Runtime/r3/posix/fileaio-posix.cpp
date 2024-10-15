/* $Id: fileaio-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File async I/O, native implementation for POSIX compliant host platforms.
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
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include "internal/fileaio.h"

#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
# include <sys/types.h>
# include <sys/sysctl.h> /* for sysctlbyname */
#endif
#if defined(RT_OS_FREEBSD)
# include <fcntl.h> /* O_SYNC */
#endif
#include <aio.h>
#include <errno.h>
#include <time.h>

/*
 * Linux does not define this value.
 * Just define it with really big
 * value.
 */
#ifndef AIO_LISTIO_MAX
# define AIO_LISTIO_MAX UINT32_MAX
#endif

#if 0 /* Only used for debugging */
# undef AIO_LISTIO_MAX
# define AIO_LISTIO_MAX 16
#endif

/** Invalid entry in the waiting array. */
#define RTFILEAIOCTX_WAIT_ENTRY_INVALID (~0U)

/** No-op replacement for rtFileAioCtxDump for non debug builds */
#ifndef LOG_ENABLED
# define rtFileAioCtxDump(pCtxInt) do {} while (0)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Async I/O request state.
 */
typedef struct RTFILEAIOREQINTERNAL
{
    /** The aio control block. FIRST ELEMENT! */
    struct aiocb                 AioCB;
    /** Next element in the chain. */
    struct RTFILEAIOREQINTERNAL *pNext;
    /** Previous element in the chain. */
    struct RTFILEAIOREQINTERNAL *pPrev;
    /** Current state the request is in. */
    RTFILEAIOREQSTATE            enmState;
    /** Flag whether this is a flush request. */
    bool                         fFlush;
    /** Flag indicating if the request was canceled. */
    volatile bool                fCanceled;
    /** Opaque user data. */
    void                        *pvUser;
    /** Number of bytes actually transferred. */
    size_t                       cbTransfered;
    /** Status code. */
    int                          Rc;
    /** Completion context we are assigned to. */
    struct RTFILEAIOCTXINTERNAL *pCtxInt;
    /** Entry in the waiting list the request is in. */
    unsigned                     iWaitingList;
    /** Magic value  (RTFILEAIOREQ_MAGIC). */
    uint32_t                     u32Magic;
} RTFILEAIOREQINTERNAL, *PRTFILEAIOREQINTERNAL;

/**
 * Async I/O completion context state.
 */
typedef struct RTFILEAIOCTXINTERNAL
{
    /** Current number of requests active on this context. */
    volatile int32_t      cRequests;
    /** Maximum number of requests this context can handle. */
    uint32_t              cMaxRequests;
    /** The ID of the thread which is currently waiting for requests. */
    volatile RTTHREAD     hThreadWait;
    /** Flag whether the thread was woken up. */
    volatile bool         fWokenUp;
    /** Flag whether the thread is currently waiting in the syscall. */
    volatile bool         fWaiting;
    /** Flags given during creation. */
    uint32_t              fFlags;
    /** Magic value (RTFILEAIOCTX_MAGIC). */
    uint32_t              u32Magic;
    /** Flag whether the thread was woken up due to a internal event. */
    volatile bool         fWokenUpInternal;
    /** List of new requests which needs to be inserted into apReqs by the
     *  waiting thread. */
    volatile PRTFILEAIOREQINTERNAL apReqsNewHead[5];
    /** Special entry for requests which are canceled. Because only one
     * request can be canceled at a time and the thread canceling the request
     * has to wait we need only one entry. */
    volatile PRTFILEAIOREQINTERNAL pReqToCancel;
    /** Event semaphore the canceling thread is waiting for completion of
     * the operation. */
    RTSEMEVENT            SemEventCancel;
    /** Head of submitted elements waiting to get into the array. */
    PRTFILEAIOREQINTERNAL pReqsWaitHead;
    /** Tail of submitted elements waiting to get into the array. */
    PRTFILEAIOREQINTERNAL pReqsWaitTail;
    /** Maximum number of elements in the waiting array. */
    unsigned              cReqsWaitMax;
    /** First free slot in the waiting list. */
    unsigned              iFirstFree;
    /** List of requests we are currently waiting on.
     * Size depends on cMaxRequests and AIO_LISTIO_MAX. */
    volatile PRTFILEAIOREQINTERNAL apReqs[1];
} RTFILEAIOCTXINTERNAL, *PRTFILEAIOCTXINTERNAL;

/**
 * Internal worker for waking up the waiting thread.
 */
static void rtFileAioCtxWakeup(PRTFILEAIOCTXINTERNAL pCtxInt)
{
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
    bool fWaiting = ASMAtomicReadBool(&pCtxInt->fWaiting);
    if (fWaiting)
    {
        /*
         * If a thread waits the handle must be valid.
         * It is possible that the thread returns from
         * aio_suspend() before the signal is send.
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
}

/**
 * Internal worker processing events and inserting new requests into the waiting list.
 */
static int rtFileAioCtxProcessEvents(PRTFILEAIOCTXINTERNAL pCtxInt)
{
    int rc = VINF_SUCCESS;

    /* Process new requests first. */
    bool fWokenUp = ASMAtomicXchgBool(&pCtxInt->fWokenUpInternal, false);
    if (fWokenUp)
    {
        for (unsigned iSlot = 0; iSlot < RT_ELEMENTS(pCtxInt->apReqsNewHead); iSlot++)
        {
            PRTFILEAIOREQINTERNAL pReqHead = ASMAtomicXchgPtrT(&pCtxInt->apReqsNewHead[iSlot], NULL, PRTFILEAIOREQINTERNAL);

            while (  (pCtxInt->iFirstFree < pCtxInt->cReqsWaitMax)
                   && pReqHead)
            {
                RTFIELAIOREQ_ASSERT_STATE(pReqHead, SUBMITTED);
                pCtxInt->apReqs[pCtxInt->iFirstFree] = pReqHead;
                pReqHead->iWaitingList = pCtxInt->iFirstFree;
                pReqHead = pReqHead->pNext;

                /* Clear pointer to next and previous element just for safety. */
                pCtxInt->apReqs[pCtxInt->iFirstFree]->pNext = NULL;
                pCtxInt->apReqs[pCtxInt->iFirstFree]->pPrev = NULL;
                pCtxInt->iFirstFree++;

                Assert(   (pCtxInt->iFirstFree <= pCtxInt->cMaxRequests)
                       && (pCtxInt->iFirstFree <= pCtxInt->cReqsWaitMax));
            }

            /* Append the rest to the wait list. */
            if (pReqHead)
            {
                RTFIELAIOREQ_ASSERT_STATE(pReqHead, SUBMITTED);
                if (!pCtxInt->pReqsWaitHead)
                {
                    Assert(!pCtxInt->pReqsWaitTail);
                    pCtxInt->pReqsWaitHead = pReqHead;
                    pReqHead->pPrev = NULL;
                }
                else
                {
                    AssertPtr(pCtxInt->pReqsWaitTail);

                    pCtxInt->pReqsWaitTail->pNext = pReqHead;
                    pReqHead->pPrev = pCtxInt->pReqsWaitTail;
                }

                /* Update tail. */
                while (pReqHead->pNext)
                {
                    RTFIELAIOREQ_ASSERT_STATE(pReqHead->pNext, SUBMITTED);
                    pReqHead = pReqHead->pNext;
                }

                pCtxInt->pReqsWaitTail = pReqHead;
                pCtxInt->pReqsWaitTail->pNext = NULL;
            }
        }

        /* Check if a request needs to be canceled. */
        PRTFILEAIOREQINTERNAL pReqToCancel = ASMAtomicReadPtrT(&pCtxInt->pReqToCancel, PRTFILEAIOREQINTERNAL);
        if (pReqToCancel)
        {
            /* The request can be in the array waiting for completion or still in the list because it is full. */
            if (pReqToCancel->iWaitingList != RTFILEAIOCTX_WAIT_ENTRY_INVALID)
            {
                /* Put it out of the waiting list. */
                pCtxInt->apReqs[pReqToCancel->iWaitingList] = pCtxInt->apReqs[--pCtxInt->iFirstFree];
                pCtxInt->apReqs[pReqToCancel->iWaitingList]->iWaitingList = pReqToCancel->iWaitingList;
            }
            else
            {
                /* Unlink from the waiting list. */
                PRTFILEAIOREQINTERNAL pPrev = pReqToCancel->pPrev;
                PRTFILEAIOREQINTERNAL pNext = pReqToCancel->pNext;

                if (pNext)
                    pNext->pPrev = pPrev;
                else
                {
                    /* We canceled the tail. */
                    pCtxInt->pReqsWaitTail = pPrev;
                }

                if (pPrev)
                    pPrev->pNext = pNext;
                else
                {
                    /* We canceled the head. */
                    pCtxInt->pReqsWaitHead = pNext;
                }
            }

            ASMAtomicDecS32(&pCtxInt->cRequests);
            AssertMsg(pCtxInt->cRequests >= 0, ("Canceled request not which is not in this context\n"));
            RTSemEventSignal(pCtxInt->SemEventCancel);
        }
    }
    else
    {
        if (ASMAtomicXchgBool(&pCtxInt->fWokenUp, false))
            rc = VERR_INTERRUPTED;
    }

    return rc;
}

RTR3DECL(int) RTFileAioGetLimits(PRTFILEAIOLIMITS pAioLimits)
{
    int rcBSD = 0;
    AssertPtrReturn(pAioLimits, VERR_INVALID_POINTER);

#if defined(RT_OS_DARWIN)
    int cReqsOutstandingMax = 0;
    size_t cbParameter = sizeof(int);

    rcBSD = sysctlbyname("kern.aioprocmax",     /* name */
                         &cReqsOutstandingMax,  /* Where to store the old value. */
                         &cbParameter,          /* Size of the memory pointed to. */
                         NULL,                  /* Where the new value is located. */
                         0);                    /* Where the size of the new value is stored. */
    if (rcBSD == -1)
        return RTErrConvertFromErrno(errno);

    pAioLimits->cReqsOutstandingMax = cReqsOutstandingMax;
    pAioLimits->cbBufferAlignment   = 0;
#elif defined(RT_OS_FREEBSD)
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
#else
    pAioLimits->cReqsOutstandingMax = RTFILEAIO_UNLIMITED_REQS;
    pAioLimits->cbBufferAlignment   = 0;
#endif

    return VINF_SUCCESS;
}

RTR3DECL(int) RTFileAioReqCreate(PRTFILEAIOREQ phReq)
{
    AssertPtrReturn(phReq, VERR_INVALID_POINTER);

    PRTFILEAIOREQINTERNAL pReqInt = (PRTFILEAIOREQINTERNAL)RTMemAllocZ(sizeof(RTFILEAIOREQINTERNAL));
    if (RT_UNLIKELY(!pReqInt))
        return VERR_NO_MEMORY;

    pReqInt->pCtxInt      = NULL;
    pReqInt->u32Magic     = RTFILEAIOREQ_MAGIC;
    pReqInt->iWaitingList = RTFILEAIOCTX_WAIT_ENTRY_INVALID;
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

    memset(&pReqInt->AioCB, 0, sizeof(struct aiocb));
    pReqInt->fFlush               = false;
    pReqInt->AioCB.aio_lio_opcode = uTransferDirection;
    pReqInt->AioCB.aio_fildes     = RTFileToNative(hFile);
    pReqInt->AioCB.aio_offset     = off;
    pReqInt->AioCB.aio_nbytes     = cbTransfer;
    pReqInt->AioCB.aio_buf        = pvBuf;
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
    RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReqInt, SUBMITTED, VERR_FILE_AIO_IN_PROGRESS);
    Assert(hFile != NIL_RTFILE);

    pReqInt->fFlush           = true;
    pReqInt->AioCB.aio_fildes = RTFileToNative(hFile);
    pReqInt->AioCB.aio_offset = 0;
    pReqInt->AioCB.aio_nbytes = 0;
    pReqInt->AioCB.aio_buf    = NULL;
    pReqInt->pvUser           = pvUser;
    pReqInt->Rc               = VERR_FILE_AIO_IN_PROGRESS;
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

    ASMAtomicXchgBool(&pReqInt->fCanceled, true);

    int rcPosix = aio_cancel(pReqInt->AioCB.aio_fildes, &pReqInt->AioCB);

    if (rcPosix == AIO_CANCELED)
    {
        PRTFILEAIOCTXINTERNAL pCtxInt = pReqInt->pCtxInt;
        /*
         * Notify the waiting thread that the request was canceled.
         */
        AssertMsg(RT_VALID_PTR(pCtxInt), ("Invalid state. Request was canceled but wasn't submitted\n"));

        Assert(!pCtxInt->pReqToCancel);
        ASMAtomicWritePtr(&pCtxInt->pReqToCancel, pReqInt);
        rtFileAioCtxWakeup(pCtxInt);

        /* Wait for acknowledge. */
        int rc = RTSemEventWait(pCtxInt->SemEventCancel, RT_INDEFINITE_WAIT);
        AssertRC(rc);

        ASMAtomicWriteNullPtr(&pCtxInt->pReqToCancel);
        pReqInt->Rc = VERR_FILE_AIO_CANCELED;
        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
        return VINF_SUCCESS;
    }
    else if (rcPosix == AIO_ALLDONE)
        return VERR_FILE_AIO_COMPLETED;
    else if (rcPosix == AIO_NOTCANCELED)
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

    if (  (RT_SUCCESS(pReqInt->Rc))
        && (pcbTransfered))
        *pcbTransfered = pReqInt->cbTransfered;

    return pReqInt->Rc;
}


RTDECL(int) RTFileAioCtxCreate(PRTFILEAIOCTX phAioCtx, uint32_t cAioReqsMax,
                               uint32_t fFlags)
{
    PRTFILEAIOCTXINTERNAL pCtxInt;
    unsigned cReqsWaitMax;

    AssertPtrReturn(phAioCtx, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTFILEAIOCTX_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    if (cAioReqsMax == RTFILEAIO_UNLIMITED_REQS)
        return VERR_OUT_OF_RANGE;

    cReqsWaitMax = RT_MIN(cAioReqsMax, AIO_LISTIO_MAX);

    pCtxInt = (PRTFILEAIOCTXINTERNAL)RTMemAllocZ(  sizeof(RTFILEAIOCTXINTERNAL)
                                                 + cReqsWaitMax * sizeof(PRTFILEAIOREQINTERNAL));
    if (RT_UNLIKELY(!pCtxInt))
        return VERR_NO_MEMORY;

    /* Create event semaphore. */
    int rc = RTSemEventCreate(&pCtxInt->SemEventCancel);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pCtxInt);
        return rc;
    }

    pCtxInt->u32Magic     = RTFILEAIOCTX_MAGIC;
    pCtxInt->cMaxRequests = cAioReqsMax;
    pCtxInt->cReqsWaitMax = cReqsWaitMax;
    pCtxInt->fFlags       = fFlags;
    *phAioCtx = (RTFILEAIOCTX)pCtxInt;

    return VINF_SUCCESS;
}


RTDECL(int) RTFileAioCtxDestroy(RTFILEAIOCTX hAioCtx)
{
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;

    AssertPtrReturn(pCtxInt, VERR_INVALID_HANDLE);

    if (RT_UNLIKELY(pCtxInt->cRequests))
        return VERR_FILE_AIO_BUSY;

    RTSemEventDestroy(pCtxInt->SemEventCancel);
    RTMemFree(pCtxInt);

    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTFileAioCtxGetMaxReqCount(RTFILEAIOCTX hAioCtx)
{
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;

    if (hAioCtx == NIL_RTFILEAIOCTX)
        return RTFILEAIO_UNLIMITED_REQS;
    return pCtxInt->cMaxRequests;
}

RTDECL(int) RTFileAioCtxAssociateWithFile(RTFILEAIOCTX hAioCtx, RTFILE hFile)
{
    NOREF(hAioCtx); NOREF(hFile);
    return VINF_SUCCESS;
}

#ifdef LOG_ENABLED
/**
 * Dumps the state of a async I/O context.
 */
static void rtFileAioCtxDump(PRTFILEAIOCTXINTERNAL pCtxInt)
{
    LogFlow(("cRequests=%d\n", pCtxInt->cRequests));
    LogFlow(("cMaxRequests=%u\n", pCtxInt->cMaxRequests));
    LogFlow(("hThreadWait=%#p\n", pCtxInt->hThreadWait));
    LogFlow(("fWokenUp=%RTbool\n", pCtxInt->fWokenUp));
    LogFlow(("fWaiting=%RTbool\n", pCtxInt->fWaiting));
    LogFlow(("fWokenUpInternal=%RTbool\n", pCtxInt->fWokenUpInternal));
    for (unsigned i = 0; i < RT_ELEMENTS(pCtxInt->apReqsNewHead); i++)
        LogFlow(("apReqsNewHead[%u]=%#p\n", i, pCtxInt->apReqsNewHead[i]));
    LogFlow(("pReqToCancel=%#p\n", pCtxInt->pReqToCancel));
    LogFlow(("pReqsWaitHead=%#p\n", pCtxInt->pReqsWaitHead));
    LogFlow(("pReqsWaitTail=%#p\n", pCtxInt->pReqsWaitTail));
    LogFlow(("cReqsWaitMax=%u\n", pCtxInt->cReqsWaitMax));
    LogFlow(("iFirstFree=%u\n", pCtxInt->iFirstFree));
    for (unsigned i = 0; i < pCtxInt->cReqsWaitMax; i++)
        LogFlow(("apReqs[%u]=%#p\n", i, pCtxInt->apReqs[i]));
}
#endif

RTDECL(int) RTFileAioCtxSubmit(RTFILEAIOCTX hAioCtx, PRTFILEAIOREQ pahReqs, size_t cReqs)
{
    int rc = VINF_SUCCESS;
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;

    /* Parameter checks */
    AssertPtrReturn(pCtxInt, VERR_INVALID_HANDLE);
    AssertReturn(cReqs != 0, VERR_INVALID_POINTER);
    AssertPtrReturn(pahReqs,  VERR_INVALID_PARAMETER);

    rtFileAioCtxDump(pCtxInt);

    /* Check that we don't exceed the limit */
    if (ASMAtomicUoReadS32(&pCtxInt->cRequests) + cReqs > pCtxInt->cMaxRequests)
        return VERR_FILE_AIO_LIMIT_EXCEEDED;

    PRTFILEAIOREQINTERNAL pHead = NULL;

    do
    {
        int rcPosix = 0;
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

                    /* Unlink from the list again. */
                    PRTFILEAIOREQINTERNAL pNext, pPrev;
                    pNext = pReqInt->pNext;
                    pPrev = pReqInt->pPrev;
                    if (pNext)
                        pNext->pPrev = pPrev;
                    if (pPrev)
                        pPrev->pNext = pNext;
                    else
                        pHead = pNext;
                }
                rc = VERR_INVALID_HANDLE;
                break;
            }

            pReqInt->pCtxInt = pCtxInt;

            if (pReqInt->fFlush)
                break;

            /* Link them together. */
            pReqInt->pNext = pHead;
            if (pHead)
                pHead->pPrev = pReqInt;
            pReqInt->pPrev = NULL;
            pHead = pReqInt;
            RTFILEAIOREQ_SET_STATE(pReqInt, SUBMITTED);

            cReqsSubmit++;
            i++;
        }

        if (cReqsSubmit)
        {
            rcPosix = lio_listio(LIO_NOWAIT, (struct aiocb **)pahReqs, cReqsSubmit, NULL);
            if (RT_UNLIKELY(rcPosix < 0))
            {
                size_t cReqsSubmitted = cReqsSubmit;

                if (errno == EAGAIN)
                    rc = VERR_FILE_AIO_INSUFFICIENT_RESSOURCES;
                else
                    rc = RTErrConvertFromErrno(errno);

                /* Check which ones were not submitted. */
                for (i = 0; i < cReqsSubmit; i++)
                {
                    pReqInt = pahReqs[i];

                    rcPosix = aio_error(&pReqInt->AioCB);

                    if ((rcPosix != EINPROGRESS) && (rcPosix != 0))
                    {
                        cReqsSubmitted--;

#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
                        if (errno == EINVAL)
#else
                        if (rcPosix == EINVAL)
#endif
                        {
                            /* Was not submitted. */
                            RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
                        }
                        else
                        {
                            /* An error occurred. */
                            RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);

                            /*
                             * Looks like Apple and glibc interpret the standard in different ways.
                             * glibc returns the error code which would be in errno but Apple returns
                             * -1 and sets errno to the appropriate value
                             */
#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
                            Assert(rcPosix == -1);
                            pReqInt->Rc = RTErrConvertFromErrno(errno);
#elif defined(RT_OS_LINUX)
                            pReqInt->Rc = RTErrConvertFromErrno(rcPosix);
#endif
                            pReqInt->cbTransfered = 0;
                        }
                        /* Unlink from the list. */
                        PRTFILEAIOREQINTERNAL pNext, pPrev;
                        pNext = pReqInt->pNext;
                        pPrev = pReqInt->pPrev;
                        if (pNext)
                            pNext->pPrev = pPrev;
                        if (pPrev)
                            pPrev->pNext = pNext;
                        else
                            pHead = pNext;

                        pReqInt->pNext = NULL;
                        pReqInt->pPrev = NULL;
                    }
                }
                ASMAtomicAddS32(&pCtxInt->cRequests, cReqsSubmitted);
                AssertMsg(pCtxInt->cRequests >= 0, ("Adding requests resulted in overflow\n"));
                break;
            }

            ASMAtomicAddS32(&pCtxInt->cRequests, cReqsSubmit);
            AssertMsg(pCtxInt->cRequests >= 0, ("Adding requests resulted in overflow\n"));
            cReqs   -= cReqsSubmit;
            pahReqs += cReqsSubmit;
        }

        /*
         * Check if we have a flush request now.
         * If not we hit the AIO_LISTIO_MAX limit
         * and will continue submitting requests
         * above.
         */
        if (cReqs && RT_SUCCESS_NP(rc))
        {
            pReqInt = pahReqs[0];

            if (pReqInt->fFlush)
            {
                /*
                 * lio_listio does not work with flush requests so
                 * we have to use aio_fsync directly.
                 */
                rcPosix = aio_fsync(O_SYNC, &pReqInt->AioCB);
                if (RT_UNLIKELY(rcPosix < 0))
                {
                    if (errno == EAGAIN)
                    {
                        rc = VERR_FILE_AIO_INSUFFICIENT_RESSOURCES;
                        RTFILEAIOREQ_SET_STATE(pReqInt, PREPARED);
                    }
                    else
                    {
                        rc = RTErrConvertFromErrno(errno);
                        RTFILEAIOREQ_SET_STATE(pReqInt, COMPLETED);
                        pReqInt->Rc = rc;
                    }
                    pReqInt->cbTransfered = 0;
                    break;
                }

                /* Link them together. */
                pReqInt->pNext = pHead;
                if (pHead)
                    pHead->pPrev = pReqInt;
                pReqInt->pPrev = NULL;
                pHead = pReqInt;
                RTFILEAIOREQ_SET_STATE(pReqInt, SUBMITTED);

                ASMAtomicIncS32(&pCtxInt->cRequests);
                AssertMsg(pCtxInt->cRequests >= 0, ("Adding requests resulted in overflow\n"));
                cReqs--;
                pahReqs++;
            }
        }
    } while (   cReqs
             && RT_SUCCESS_NP(rc));

    if (pHead)
    {
        /*
         * Forward successfully submitted requests to the thread waiting for requests.
         * We search for a free slot first and if we don't find one
         * we will grab the first one and append our list to the existing entries.
         */
        unsigned iSlot = 0;
        while (  (iSlot < RT_ELEMENTS(pCtxInt->apReqsNewHead))
               && !ASMAtomicCmpXchgPtr(&pCtxInt->apReqsNewHead[iSlot], pHead, NULL))
            iSlot++;

        if (iSlot == RT_ELEMENTS(pCtxInt->apReqsNewHead))
        {
            /* Nothing found. */
            PRTFILEAIOREQINTERNAL pOldHead = ASMAtomicXchgPtrT(&pCtxInt->apReqsNewHead[0], NULL, PRTFILEAIOREQINTERNAL);

            /* Find the end of the current head and link the old list to the current. */
            PRTFILEAIOREQINTERNAL pTail = pHead;
            while (pTail->pNext)
                pTail = pTail->pNext;

            pTail->pNext = pOldHead;

            ASMAtomicWritePtr(&pCtxInt->apReqsNewHead[0], pHead);
        }

        /* Set the internal wakeup flag and wakeup the thread if possible. */
        bool fWokenUp = ASMAtomicXchgBool(&pCtxInt->fWokenUpInternal, true);
        if (!fWokenUp)
            rtFileAioCtxWakeup(pCtxInt);
    }

    rtFileAioCtxDump(pCtxInt);

    return rc;
}


RTDECL(int) RTFileAioCtxWait(RTFILEAIOCTX hAioCtx, size_t cMinReqs, RTMSINTERVAL cMillies,
                             PRTFILEAIOREQ pahReqs, size_t cReqs, uint32_t *pcReqs)
{
    int rc = VINF_SUCCESS;
    int cRequestsCompleted = 0;
    PRTFILEAIOCTXINTERNAL pCtxInt = (PRTFILEAIOCTXINTERNAL)hAioCtx;
    struct timespec Timeout;
    struct timespec *pTimeout = NULL;
    uint64_t         StartNanoTS = 0;

    LogFlowFunc(("hAioCtx=%#p cMinReqs=%zu cMillies=%u pahReqs=%#p cReqs=%zu pcbReqs=%#p\n",
                 hAioCtx, cMinReqs, cMillies, pahReqs, cReqs, pcReqs));

    /* Check parameters. */
    AssertPtrReturn(pCtxInt, VERR_INVALID_HANDLE);
    AssertPtrReturn(pcReqs, VERR_INVALID_POINTER);
    AssertPtrReturn(pahReqs, VERR_INVALID_POINTER);
    AssertReturn(cReqs != 0, VERR_INVALID_PARAMETER);
    AssertReturn(cReqs >= cMinReqs, VERR_OUT_OF_RANGE);

    rtFileAioCtxDump(pCtxInt);

    int32_t cRequestsWaiting = ASMAtomicReadS32(&pCtxInt->cRequests);

    if (   RT_UNLIKELY(cRequestsWaiting <= 0)
        && !(pCtxInt->fFlags & RTFILEAIOCTX_FLAGS_WAIT_WITHOUT_PENDING_REQUESTS))
        return VERR_FILE_AIO_NO_REQUEST;

    if (RT_UNLIKELY(cMinReqs > (uint32_t)cRequestsWaiting))
        return VERR_INVALID_PARAMETER;

    if (cMillies != RT_INDEFINITE_WAIT)
    {
        Timeout.tv_sec  = cMillies / 1000;
        Timeout.tv_nsec = (cMillies % 1000) * 1000000;
        pTimeout = &Timeout;
        StartNanoTS = RTTimeNanoTS();
    }

    /* Wait for at least one. */
    if (!cMinReqs)
        cMinReqs = 1;

    /* For the wakeup call. */
    Assert(pCtxInt->hThreadWait == NIL_RTTHREAD);
    ASMAtomicWriteHandle(&pCtxInt->hThreadWait, RTThreadSelf());

    /* Update the waiting list once before we enter the loop. */
    rc = rtFileAioCtxProcessEvents(pCtxInt);

    while (   cMinReqs
           && RT_SUCCESS_NP(rc))
    {
#ifdef RT_STRICT
        if (RT_UNLIKELY(!pCtxInt->iFirstFree))
        {
            for (unsigned i = 0; i < pCtxInt->cReqsWaitMax; i++)
                RTAssertMsg2Weak("wait[%d] = %#p\n", i, pCtxInt->apReqs[i]);

            AssertMsgFailed(("No request to wait for. pReqsWaitHead=%#p pReqsWaitTail=%#p\n",
                            pCtxInt->pReqsWaitHead, pCtxInt->pReqsWaitTail));
        }
#endif

        LogFlow(("Waiting for %d requests to complete\n", pCtxInt->iFirstFree));
        rtFileAioCtxDump(pCtxInt);

        ASMAtomicXchgBool(&pCtxInt->fWaiting, true);
        int rcPosix = aio_suspend((const struct aiocb * const *)pCtxInt->apReqs,
                                  pCtxInt->iFirstFree, pTimeout);
        ASMAtomicXchgBool(&pCtxInt->fWaiting, false);
        if (rcPosix < 0)
        {
            LogFlow(("aio_suspend failed %d nent=%u\n", errno, pCtxInt->iFirstFree));
            /* Check that this is an external wakeup event. */
            if (errno == EINTR)
                rc = rtFileAioCtxProcessEvents(pCtxInt);
            else
                rc = RTErrConvertFromErrno(errno);
        }
        else
        {
            /* Requests finished. */
            unsigned iReqCurr = 0;
            unsigned cDone = 0;

            /* Remove completed requests from the waiting list. */
            while (   (iReqCurr < pCtxInt->iFirstFree)
                   && (cDone < cReqs))
            {
                PRTFILEAIOREQINTERNAL pReq = pCtxInt->apReqs[iReqCurr];
                int rcReq = aio_error(&pReq->AioCB);

                if (rcReq != EINPROGRESS)
                {
                    /* Completed store the return code. */
                    if (rcReq == 0)
                    {
                        pReq->Rc = VINF_SUCCESS;
                        /* Call aio_return() to free resources. */
                        pReq->cbTransfered = aio_return(&pReq->AioCB);
                    }
                    else
                    {
#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
                        pReq->Rc = RTErrConvertFromErrno(errno);
#else
                        pReq->Rc = RTErrConvertFromErrno(rcReq);
#endif
                    }

                    /* Mark the request as finished. */
                    RTFILEAIOREQ_SET_STATE(pReq, COMPLETED);
                    cDone++;

                    /* If there are other entries waiting put the head into the now free entry. */
                    if (pCtxInt->pReqsWaitHead)
                    {
                        PRTFILEAIOREQINTERNAL pReqInsert = pCtxInt->pReqsWaitHead;

                        pCtxInt->pReqsWaitHead = pReqInsert->pNext;
                        if (!pCtxInt->pReqsWaitHead)
                        {
                            /* List is empty now. Clear tail too. */
                            pCtxInt->pReqsWaitTail = NULL;
                        }

                        pReqInsert->iWaitingList = pReq->iWaitingList;
                        pCtxInt->apReqs[pReqInsert->iWaitingList] = pReqInsert;
                        iReqCurr++;
                    }
                    else
                    {
                        /*
                         * Move the last entry into the current position to avoid holes
                         * but only if it is not the last element already.
                         */
                        if (pReq->iWaitingList < pCtxInt->iFirstFree - 1)
                        {
                            pCtxInt->apReqs[pReq->iWaitingList] = pCtxInt->apReqs[--pCtxInt->iFirstFree];
                            pCtxInt->apReqs[pReq->iWaitingList]->iWaitingList = pReq->iWaitingList;
                        }
                        else
                            pCtxInt->iFirstFree--;

                        pCtxInt->apReqs[pCtxInt->iFirstFree] = NULL;
                    }

                    /* Put the request into the completed list. */
                    pahReqs[cRequestsCompleted++] = pReq;
                    pReq->iWaitingList = RTFILEAIOCTX_WAIT_ENTRY_INVALID;
                }
                else
                    iReqCurr++;
            }

            AssertMsg((cDone <= cReqs), ("Overflow cReqs=%u cMinReqs=%u cDone=%u\n",
                                         cReqs, cDone));
            cReqs    -= cDone;
            cMinReqs  = RT_MAX(cMinReqs, cDone) - cDone;
            ASMAtomicSubS32(&pCtxInt->cRequests, cDone);

            AssertMsg(pCtxInt->cRequests >= 0, ("Finished more requests than currently active\n"));

            if (!cMinReqs)
                break;

            if (cMillies != RT_INDEFINITE_WAIT)
            {
                uint64_t TimeDiff;

                /* Recalculate the timeout. */
                TimeDiff = RTTimeSystemNanoTS() - StartNanoTS;
                Timeout.tv_sec  = Timeout.tv_sec  - (TimeDiff / 1000000);
                Timeout.tv_nsec = Timeout.tv_nsec - (TimeDiff % 1000000);
            }

            /* Check for new elements. */
            rc = rtFileAioCtxProcessEvents(pCtxInt);
        }
    }

    *pcReqs = cRequestsCompleted;
    Assert(pCtxInt->hThreadWait == RTThreadSelf());
    ASMAtomicWriteHandle(&pCtxInt->hThreadWait, NIL_RTTHREAD);

    rtFileAioCtxDump(pCtxInt);

    return rc;
}


RTDECL(int) RTFileAioCtxWakeup(RTFILEAIOCTX hAioCtx)
{
    PRTFILEAIOCTXINTERNAL pCtxInt = hAioCtx;
    RTFILEAIOCTX_VALID_RETURN(pCtxInt);

    /** @todo r=bird: Define the protocol for how to resume work after calling
     *        this function. */

    bool fWokenUp = ASMAtomicXchgBool(&pCtxInt->fWokenUp, true);
    if (!fWokenUp)
        rtFileAioCtxWakeup(pCtxInt);

    return VINF_SUCCESS;
}

