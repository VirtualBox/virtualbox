  /* $Id: DrvNamedPipe.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Named pipe / local socket stream driver.
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
#define LOG_GROUP LOG_GROUP_DRV_NAMEDPIPE
#include <VBox/vmm/pdmdrv.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/stream.h>
#include <iprt/alloc.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/socket.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#else /* !RT_OS_WINDOWS */
# include <errno.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/un.h>
# ifndef SHUT_RDWR /* OS/2 */
#  define SHUT_RDWR 3
# endif
#endif /* !RT_OS_WINDOWS */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

#ifndef RT_OS_WINDOWS
# define DRVNAMEDPIPE_POLLSET_ID_SOCKET 0
# define DRVNAMEDPIPE_POLLSET_ID_WAKEUP 1
#endif

# define DRVNAMEDPIPE_WAKEUP_REASON_EXTERNAL       0
# define DRVNAMEDPIPE_WAKEUP_REASON_NEW_CONNECTION 1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Named pipe driver instance data.
 *
 * @implements PDMISTREAM
 */
typedef struct DRVNAMEDPIPE
{
    /** The stream interface. */
    PDMISTREAM          IStream;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;
    /** Pointer to the named pipe file name. (Freed by MM) */
    char                *pszLocation;
    /** Flag whether VirtualBox represents the server or client side. */
    bool                fIsServer;
#ifdef RT_OS_WINDOWS
    /** File handle of the named pipe. */
    HANDLE              NamedPipe;
    /** The wake event handle. */
    HANDLE              hEvtWake;
    /** Overlapped structure for writes. */
    OVERLAPPED          OverlappedWrite;
    /** Overlapped structure for reads. */
    OVERLAPPED          OverlappedRead;
    /** Listen thread wakeup semaphore */
    RTSEMEVENTMULTI     ListenSem;
    /** Read buffer. */
    uint8_t             abBufRead[32];
    /** Write buffer. */
    uint8_t             abBufWrite[32];
    /** Read buffer currently used. */
    size_t              cbReadBufUsed;
    /** Size of the write buffer used. */
    size_t              cbWriteBufUsed;
    /** Flag whether a wake operation was caused by an external trigger. */
    volatile bool       fWakeExternal;
    /** Flag whether a read was started. */
    bool                fReadPending;
#else /* !RT_OS_WINDOWS */
    /** Poll set used to wait for I/O events. */
    RTPOLLSET           hPollSet;
    /** Reading end of the wakeup pipe. */
    RTPIPE              hPipeWakeR;
    /** Writing end of the wakeup pipe. */
    RTPIPE              hPipeWakeW;
    /** Socket handle. */
    RTSOCKET            hSock;
    /** Flag whether the socket is in the pollset. */
    bool                fSockInPollSet;
    /** Socket handle of the local socket for server. */
    int                 LocalSocketServer;
#endif /* !RT_OS_WINDOWS */
    /** Thread for listening for new connections. */
    RTTHREAD            ListenThread;
    /** Flag to signal listening thread to shut down. */
    bool volatile       fShutdown;
} DRVNAMEDPIPE, *PDRVNAMEDPIPE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Kicks any possibly polling thread to get informed about changes.
 *
 * @returns VBOx status code.
 * @param   pThis                  The named pipe driver instance.
 * @param   bReason                The reason code to handle.
 */
static int drvNamedPipePollerKick(PDRVNAMEDPIPE pThis, uint8_t bReason)
{
#ifdef RT_OS_WINDOWS
    if (bReason == DRVNAMEDPIPE_WAKEUP_REASON_EXTERNAL)
        ASMAtomicXchgBool(&pThis->fWakeExternal, true);
    if (!SetEvent(pThis->hEvtWake))
        return RTErrConvertFromWin32(GetLastError());

    return VINF_SUCCESS;
#else
    size_t cbWritten = 0;
    return RTPipeWrite(pThis->hPipeWakeW, &bReason, 1, &cbWritten);
#endif
}


/** @interface_method_impl{PDMISTREAM,pfnPoll} */
static DECLCALLBACK(int) drvNamedPipePoll(PPDMISTREAM pInterface, uint32_t fEvts, uint32_t *pfEvts, RTMSINTERVAL cMillies)
{
    int rc = VINF_SUCCESS;
    PDRVNAMEDPIPE pThis = RT_FROM_MEMBER(pInterface, DRVNAMEDPIPE, IStream);

    LogFlowFunc(("pInterface=%#p fEvts=%#x pfEvts=%#p cMillies=%u\n", pInterface, fEvts, pfEvts, cMillies));

#ifdef RT_OS_WINDOWS
    /* Immediately return if there is something to read or no write pending and the respective events are set. */
    *pfEvts = 0;
    if (   (fEvts & RTPOLL_EVT_READ)
        && pThis->cbReadBufUsed > 0)
        *pfEvts |= RTPOLL_EVT_READ;
    if (   (fEvts & RTPOLL_EVT_WRITE)
        && !pThis->cbWriteBufUsed)
        *pfEvts |= RTPOLL_EVT_WRITE;

    if (*pfEvts)
        return VINF_SUCCESS;

    while (RT_SUCCESS(rc))
    {
        /* Set up the waiting handles. */
        HANDLE ahEvts[3];
        unsigned cEvts = 0;

        ahEvts[cEvts++] = pThis->hEvtWake;
        if (fEvts & RTPOLL_EVT_WRITE)
        {
            Assert(pThis->cbWriteBufUsed);
            ahEvts[cEvts++] = pThis->OverlappedWrite.hEvent;
        }
        if (   (fEvts & RTPOLL_EVT_READ)
            && pThis->NamedPipe != INVALID_HANDLE_VALUE
            && !pThis->fReadPending)
        {
            Assert(!pThis->cbReadBufUsed);

            DWORD cbReallyRead;
            pThis->OverlappedRead.Offset     = 0;
            pThis->OverlappedRead.OffsetHigh = 0;
            if (!ReadFile(pThis->NamedPipe, &pThis->abBufRead[0], sizeof(pThis->abBufRead), &cbReallyRead, &pThis->OverlappedRead))
            {
                DWORD uError = GetLastError();

                if (uError == ERROR_IO_PENDING)
                {
                    uError = 0;
                    pThis->fReadPending = true;
                }

                if (   uError == ERROR_PIPE_LISTENING
                    || uError == ERROR_PIPE_NOT_CONNECTED)
                {
                    /* No connection yet/anymore */
                    cbReallyRead = 0;
                }
                else
                {
                    rc = RTErrConvertFromWin32(uError);
                    Log(("drvNamedPipePoll: ReadFile returned %d (%Rrc)\n", uError, rc));
                }
            }
            else
            {
                LogFlowFunc(("Read completed: cbReallyRead=%u\n", cbReallyRead));
                pThis->fReadPending = false;
                pThis->cbReadBufUsed = cbReallyRead;
                *pfEvts |= RTPOLL_EVT_READ;
                return VINF_SUCCESS;
            }

            if (RT_FAILURE(rc))
            {
                Log(("drvNamedPipePoll: FileRead returned %Rrc fShutdown=%d\n", rc, pThis->fShutdown));
                if (    !pThis->fShutdown
                    &&  (   rc == VERR_EOF
                         || rc == VERR_BROKEN_PIPE
                        )
                   )
                {
                    FlushFileBuffers(pThis->NamedPipe);
                    DisconnectNamedPipe(pThis->NamedPipe);
                    if (!pThis->fIsServer)
                    {
                        CloseHandle(pThis->NamedPipe);
                        pThis->NamedPipe = INVALID_HANDLE_VALUE;
                    }
                    /* pretend success */
                    rc = VINF_SUCCESS;
                }
                cbReallyRead = 0;
            }
        }

        if (pThis->fReadPending)
            ahEvts[cEvts++] = pThis->OverlappedRead.hEvent;

        DWORD dwMillies = cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies;
        DWORD uErr = WaitForMultipleObjects(cEvts, &ahEvts[0], FALSE /* bWaitAll */, dwMillies);
        if (uErr == WAIT_TIMEOUT)
            rc = VERR_TIMEOUT;
        else if (uErr == WAIT_FAILED)
            rc = RTErrConvertFromWin32(GetLastError());
        else
        {
            /* Something triggered. */
            unsigned idxEvt = uErr - WAIT_OBJECT_0;
            Assert(idxEvt < cEvts);

            LogFlowFunc(("Interrupted by pipe activity: idxEvt=%u\n", idxEvt));

            if (idxEvt == 0)
            {
                /* The wakeup triggered. */
                if (ASMAtomicXchgBool(&pThis->fWakeExternal, false))
                    rc = VERR_INTERRUPTED;
                else
                {
                    /*
                     * Internal event because there was a new connection from the listener thread,
                     * restart everything.
                     */
                    rc = VINF_SUCCESS;
                }
            }
            else if (ahEvts[idxEvt] == pThis->OverlappedWrite.hEvent)
            {
                LogFlowFunc(("Write completed\n"));
                /* Fetch the result of the write. */
                DWORD cbWritten = 0;
                if (GetOverlappedResult(pThis->NamedPipe, &pThis->OverlappedWrite, &cbWritten, TRUE) == FALSE)
                {
                    uErr = GetLastError();
                    rc = RTErrConvertFromWin32(uErr);
                    Log(("drvNamedPipePoll: Write completed with %d (%Rrc)\n", uErr, rc));

                    if (RT_FAILURE(rc))
                    {
                        /** @todo WriteFile(pipe) has been observed to return  ERROR_NO_DATA
                         *        (VERR_NO_DATA) instead of ERROR_BROKEN_PIPE, when the pipe is
                         *        disconnected. */
                        if (    rc == VERR_EOF
                            ||  rc == VERR_BROKEN_PIPE)
                        {
                            FlushFileBuffers(pThis->NamedPipe);
                            DisconnectNamedPipe(pThis->NamedPipe);
                            if (!pThis->fIsServer)
                            {
                                CloseHandle(pThis->NamedPipe);
                                pThis->NamedPipe = INVALID_HANDLE_VALUE;
                            }
                            /* pretend success */
                            rc = VINF_SUCCESS;
                        }
                        cbWritten = (DWORD)pThis->cbWriteBufUsed;
                    }
                }

                pThis->cbWriteBufUsed -= cbWritten;
                if (!pThis->cbWriteBufUsed && (fEvts & RTPOLL_EVT_WRITE))
                {
                    *pfEvts |= RTPOLL_EVT_WRITE;
                    break;
                }
            }
            else
            {
                Assert(ahEvts[idxEvt] == pThis->OverlappedRead.hEvent);

                DWORD cbRead = 0;
                if (GetOverlappedResult(pThis->NamedPipe, &pThis->OverlappedRead, &cbRead, TRUE) == FALSE)
                {
                    uErr = GetLastError();
                    rc = RTErrConvertFromWin32(uErr);
                    Log(("drvNamedPipePoll: Read completed with %d (%Rrc)\n", uErr, rc));

                    if (RT_FAILURE(rc))
                    {
                        /** @todo WriteFile(pipe) has been observed to return  ERROR_NO_DATA
                         *        (VERR_NO_DATA) instead of ERROR_BROKEN_PIPE, when the pipe is
                         *        disconnected. */
                        if (    rc == VERR_EOF
                            ||  rc == VERR_BROKEN_PIPE)
                        {
                            FlushFileBuffers(pThis->NamedPipe);
                            DisconnectNamedPipe(pThis->NamedPipe);
                            if (!pThis->fIsServer)
                            {
                                CloseHandle(pThis->NamedPipe);
                                pThis->NamedPipe = INVALID_HANDLE_VALUE;
                            }
                            /* pretend success */
                            rc = VINF_SUCCESS;
                        }
                        cbRead = 0;
                    }
                }

                LogFlowFunc(("Read completed with cbRead=%u\n", cbRead));
                pThis->fReadPending = false;
                pThis->cbReadBufUsed = cbRead;
                if (pThis->cbReadBufUsed && (fEvts & RTPOLL_EVT_READ))
                {
                    *pfEvts |= RTPOLL_EVT_READ;
                    break;
                }
            }
        }
    }
#else
    if (pThis->hSock != NIL_RTSOCKET)
    {
        if (!pThis->fSockInPollSet)
        {
            rc = RTPollSetAddSocket(pThis->hPollSet, pThis->hSock,
                                    fEvts, DRVNAMEDPIPE_POLLSET_ID_SOCKET);
            if (RT_SUCCESS(rc))
                pThis->fSockInPollSet = true;
        }
        else
        {
            /* Always include error event. */
            fEvts |= RTPOLL_EVT_ERROR;
            rc = RTPollSetEventsChange(pThis->hPollSet, DRVNAMEDPIPE_POLLSET_ID_SOCKET, fEvts);
            AssertRC(rc);
        }
    }

    while (RT_SUCCESS(rc))
    {
        uint32_t fEvtsRecv = 0;
        uint32_t idHnd = 0;

        rc = RTPoll(pThis->hPollSet, cMillies, &fEvtsRecv, &idHnd);
        if (RT_SUCCESS(rc))
        {
            if (idHnd == DRVNAMEDPIPE_POLLSET_ID_WAKEUP)
            {
                /* We got woken up, drain the pipe and return. */
                uint8_t bReason;
                size_t cbRead = 0;
                rc = RTPipeRead(pThis->hPipeWakeR, &bReason, 1, &cbRead);
                AssertRC(rc);

                if (bReason == DRVNAMEDPIPE_WAKEUP_REASON_EXTERNAL)
                    rc = VERR_INTERRUPTED;
                else if (bReason == DRVNAMEDPIPE_WAKEUP_REASON_NEW_CONNECTION)
                {
                    Assert(!pThis->fSockInPollSet);
                    rc = RTPollSetAddSocket(pThis->hPollSet, pThis->hSock,
                                            fEvts, DRVNAMEDPIPE_POLLSET_ID_SOCKET);
                    if (RT_SUCCESS(rc))
                        pThis->fSockInPollSet = true;
                }
                else
                    AssertMsgFailed(("Unknown wakeup reason in pipe %u\n", bReason));
            }
            else
            {
                Assert(idHnd == DRVNAMEDPIPE_POLLSET_ID_SOCKET);

                /* On error we close the socket here. */
                if (fEvtsRecv & RTPOLL_EVT_ERROR)
                {
                    rc = RTPollSetRemove(pThis->hPollSet, DRVNAMEDPIPE_POLLSET_ID_SOCKET);
                    AssertRC(rc);

                    RTSocketClose(pThis->hSock);
                    pThis->hSock = NIL_RTSOCKET;
                    pThis->fSockInPollSet = false;
                    /* Continue with polling. */
                }
                else
                {
                    *pfEvts = fEvtsRecv;
                    break;
                }
            }
        }
    }
#endif

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/** @interface_method_impl{PDMISTREAM,pfnPollInterrupt} */
static DECLCALLBACK(int) drvNamedPipePollInterrupt(PPDMISTREAM pInterface)
{
    PDRVNAMEDPIPE pThis = RT_FROM_MEMBER(pInterface, DRVNAMEDPIPE, IStream);
    return drvNamedPipePollerKick(pThis, DRVNAMEDPIPE_WAKEUP_REASON_EXTERNAL);
}


/** @interface_method_impl{PDMISTREAM,pfnRead} */
static DECLCALLBACK(int) drvNamedPipeRead(PPDMISTREAM pInterface, void *pvBuf, size_t *pcbRead)
{
    int rc = VINF_SUCCESS;
    PDRVNAMEDPIPE pThis = RT_FROM_MEMBER(pInterface, DRVNAMEDPIPE, IStream);
    LogFlow(("%s: pvBuf=%p *pcbRead=%#x (%s)\n", __FUNCTION__, pvBuf, *pcbRead, pThis->pszLocation));

    Assert(pvBuf);
#ifdef RT_OS_WINDOWS
    if (pThis->NamedPipe != INVALID_HANDLE_VALUE)
    {
        /* Check if there is something in the read buffer and return as much as we can. */
        if (pThis->cbReadBufUsed)
        {
            size_t cbRead = RT_MIN(*pcbRead, pThis->cbReadBufUsed);

            memcpy(pvBuf, &pThis->abBufRead[0], cbRead);
            if (cbRead < pThis->cbReadBufUsed)
                memmove(&pThis->abBufRead[0], &pThis->abBufRead[cbRead], pThis->cbReadBufUsed - cbRead);
            pThis->cbReadBufUsed -= cbRead;
            *pcbRead = cbRead;
        }
        else
            *pcbRead = 0;
    }
#else /* !RT_OS_WINDOWS */
    if (pThis->hSock != NIL_RTSOCKET)
    {
        size_t cbRead;
        size_t cbBuf = *pcbRead;
        rc = RTSocketReadNB(pThis->hSock, pvBuf, cbBuf, &cbRead);
        if (RT_SUCCESS(rc))
        {
            if (!cbRead && rc != VINF_TRY_AGAIN)
            {
                rc = RTPollSetRemove(pThis->hPollSet, DRVNAMEDPIPE_POLLSET_ID_SOCKET);
                AssertRC(rc);

                RTSocketClose(pThis->hSock);
                pThis->hSock = NIL_RTSOCKET;
                pThis->fSockInPollSet = false;
                rc = VINF_SUCCESS;
            }
            *pcbRead = cbRead;
        }
    }
#endif /* !RT_OS_WINDOWS */
    else
    {
        RTThreadSleep(100);
        *pcbRead = 0;
    }

    LogFlow(("%s: *pcbRead=%zu returns %Rrc\n", __FUNCTION__, *pcbRead, rc));
    return rc;
}


/** @interface_method_impl{PDMISTREAM,pfnWrite} */
static DECLCALLBACK(int) drvNamedPipeWrite(PPDMISTREAM pInterface, const void *pvBuf, size_t *pcbWrite)
{
    int rc = VINF_SUCCESS;
    PDRVNAMEDPIPE pThis = RT_FROM_MEMBER(pInterface, DRVNAMEDPIPE, IStream);
    LogFlow(("%s: pvBuf=%p *pcbWrite=%#x (%s)\n", __FUNCTION__, pvBuf, *pcbWrite, pThis->pszLocation));

    Assert(pvBuf);
#ifdef RT_OS_WINDOWS
    if (pThis->NamedPipe != INVALID_HANDLE_VALUE)
    {
        /* Accept the data in case the write buffer is empty. */
        if (!pThis->cbWriteBufUsed)
        {
            size_t cbWrite = RT_MIN(*pcbWrite, sizeof(pThis->cbWriteBufUsed));

            memcpy(&pThis->abBufWrite[0], pvBuf, cbWrite);
            pThis->cbWriteBufUsed += cbWrite;

            /* Initiate the write. */
            pThis->OverlappedWrite.Offset     = 0;
            pThis->OverlappedWrite.OffsetHigh = 0;
            if (!WriteFile(pThis->NamedPipe, pvBuf, (DWORD)cbWrite, NULL, &pThis->OverlappedWrite))
            {
                DWORD uError = GetLastError();

                if (   uError == ERROR_PIPE_LISTENING
                    || uError == ERROR_PIPE_NOT_CONNECTED)
                {
                    /* No connection yet/anymore; just discard the write (pretending everything was written). */
                     pThis->cbWriteBufUsed = 0;
                    cbWrite = *pcbWrite;
                }
                else if (uError != ERROR_IO_PENDING) /* We wait for the write to complete in the poll callback. */
                {
                    rc = RTErrConvertFromWin32(uError);
                    Log(("drvNamedPipeWrite: WriteFile returned %d (%Rrc)\n", uError, rc));
                    cbWrite = 0;
                }
            }

            if (RT_FAILURE(rc))
            {
                /** @todo WriteFile(pipe) has been observed to return  ERROR_NO_DATA
                 *        (VERR_NO_DATA) instead of ERROR_BROKEN_PIPE, when the pipe is
                 *        disconnected. */
                if (    rc == VERR_EOF
                    ||  rc == VERR_BROKEN_PIPE)
                {
                    FlushFileBuffers(pThis->NamedPipe);
                    DisconnectNamedPipe(pThis->NamedPipe);
                    if (!pThis->fIsServer)
                    {
                        CloseHandle(pThis->NamedPipe);
                        pThis->NamedPipe = INVALID_HANDLE_VALUE;
                    }
                    /* pretend success */
                    rc = VINF_SUCCESS;
                }
                cbWrite = 0;
            }

            *pcbWrite = cbWrite;
        }
        else
            *pcbWrite = 0;
    }
#else /* !RT_OS_WINDOWS */
    if (pThis->hSock != NIL_RTSOCKET)
    {
        size_t cbBuf = *pcbWrite;
        rc = RTSocketWriteNB(pThis->hSock, pvBuf, cbBuf, pcbWrite);
    }
    else
        *pcbWrite = 0;
#endif /* !RT_OS_WINDOWS */

    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvNamedPipeQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVNAMEDPIPE   pThis   = PDMINS_2_DATA(pDrvIns, PDRVNAMEDPIPE);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMISTREAM, &pThis->IStream);
    return NULL;
}


/* -=-=-=-=- listen thread -=-=-=-=- */

/**
 * Receive thread loop.
 *
 * @returns 0 on success.
 * @param   hThreadSelf Thread handle to this thread.
 * @param   pvUser      User argument.
 */
static DECLCALLBACK(int) drvNamedPipeListenLoop(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    PDRVNAMEDPIPE   pThis = (PDRVNAMEDPIPE)pvUser;
    int             rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    HANDLE          NamedPipe = pThis->NamedPipe;
    HANDLE          hEvent = CreateEvent(NULL, TRUE, FALSE, 0);
#endif

    while (RT_LIKELY(!pThis->fShutdown))
    {
#ifdef RT_OS_WINDOWS
        OVERLAPPED overlapped;

        memset(&overlapped, 0, sizeof(overlapped));
        overlapped.hEvent = hEvent;

        BOOL fConnected = ConnectNamedPipe(NamedPipe, &overlapped);
        if (    !fConnected
            &&  !pThis->fShutdown)
        {
            DWORD hrc = GetLastError();

            if (hrc == ERROR_IO_PENDING)
            {
                DWORD dummy;

                hrc = 0;
                if (GetOverlappedResult(pThis->NamedPipe, &overlapped, &dummy, TRUE) == FALSE)
                    hrc = GetLastError();
                else
                    drvNamedPipePollerKick(pThis, DRVNAMEDPIPE_WAKEUP_REASON_NEW_CONNECTION);
            }

            if (pThis->fShutdown)
                break;

            if (hrc == ERROR_PIPE_CONNECTED)
            {
                RTSemEventMultiWait(pThis->ListenSem, 250);
            }
            else if (hrc != ERROR_SUCCESS)
            {
                rc = RTErrConvertFromWin32(hrc);
                LogRel(("NamedPipe%d: ConnectNamedPipe failed, rc=%Rrc\n", pThis->pDrvIns->iInstance, rc));
                break;
            }
        }
#else /* !RT_OS_WINDOWS */
        if (listen(pThis->LocalSocketServer, 0) == -1)
        {
            rc = RTErrConvertFromErrno(errno);
            LogRel(("NamedPipe%d: listen failed, rc=%Rrc\n", pThis->pDrvIns->iInstance, rc));
            break;
        }
        int s = accept(pThis->LocalSocketServer, NULL, NULL);
        if (s == -1)
        {
            rc = RTErrConvertFromErrno(errno);
            LogRel(("NamedPipe%d: accept failed, rc=%Rrc\n", pThis->pDrvIns->iInstance, rc));
            break;
        }
        if (pThis->hSock != NIL_RTSOCKET)
        {
            LogRel(("NamedPipe%d: only single connection supported\n", pThis->pDrvIns->iInstance));
            close(s);
        }
        else
        {
            RTSOCKET hSockNew = NIL_RTSOCKET;
            rc = RTSocketFromNative(&hSockNew, s);
            if (RT_SUCCESS(rc))
            {
                pThis->hSock = hSockNew;
                /* Inform the poller about the new socket. */
                drvNamedPipePollerKick(pThis, DRVNAMEDPIPE_WAKEUP_REASON_NEW_CONNECTION);
            }
            else
            {
                LogRel(("NamedPipe%d: Failed to wrap socket with %Rrc\n", pThis->pDrvIns->iInstance, rc));
                close(s);
            }
        }
#endif /* !RT_OS_WINDOWS */
    }

#ifdef RT_OS_WINDOWS
    CloseHandle(hEvent);
#endif
    return VINF_SUCCESS;
}

/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

/**
 * Common worker for drvNamedPipePowerOff and drvNamedPipeDestructor.
 *
 * @param   pThis               The instance data.
 */
static void drvNamedPipeShutdownListener(PDRVNAMEDPIPE pThis)
{
    /*
     * Signal shutdown of the listener thread.
     */
    pThis->fShutdown = true;
#ifdef RT_OS_WINDOWS
    if (    pThis->fIsServer
        &&  pThis->NamedPipe != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(pThis->NamedPipe);
        DisconnectNamedPipe(pThis->NamedPipe);

        BOOL fRc = CloseHandle(pThis->NamedPipe);
        Assert(fRc); NOREF(fRc);
        pThis->NamedPipe = INVALID_HANDLE_VALUE;

        /* Wake up listen thread */
        if (pThis->ListenSem != NIL_RTSEMEVENT)
            RTSemEventMultiSignal(pThis->ListenSem);
    }
#else
    if (    pThis->fIsServer
        &&  pThis->LocalSocketServer != -1)
    {
        int rc = shutdown(pThis->LocalSocketServer, SHUT_RDWR);
        AssertRC(rc == 0); NOREF(rc);

        rc = close(pThis->LocalSocketServer);
        AssertRC(rc == 0);
        pThis->LocalSocketServer = -1;
    }
#endif
}


/**
 * Power off a named pipe stream driver instance.
 *
 * This does most of the destruction work, to avoid ordering dependencies.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvNamedPipePowerOff(PPDMDRVINS pDrvIns)
{
    PDRVNAMEDPIPE pThis = PDMINS_2_DATA(pDrvIns, PDRVNAMEDPIPE);
    LogFlow(("%s: %s\n", __FUNCTION__, pThis->pszLocation));

    drvNamedPipeShutdownListener(pThis);
}


/**
 * Destruct a named pipe stream driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that
 * any non-VM resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvNamedPipeDestruct(PPDMDRVINS pDrvIns)
{
    PDRVNAMEDPIPE pThis = PDMINS_2_DATA(pDrvIns, PDRVNAMEDPIPE);
    LogFlow(("%s: %s\n", __FUNCTION__, pThis->pszLocation));
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    drvNamedPipeShutdownListener(pThis);

    /*
     * While the thread exits, clean up as much as we can.
     */
#ifdef RT_OS_WINDOWS
    if (pThis->NamedPipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pThis->NamedPipe);
        pThis->NamedPipe = INVALID_HANDLE_VALUE;
    }
    if (pThis->OverlappedRead.hEvent != NULL)
    {
        CloseHandle(pThis->OverlappedRead.hEvent);
        pThis->OverlappedRead.hEvent = NULL;
    }
    if (pThis->OverlappedWrite.hEvent != NULL)
    {
        CloseHandle(pThis->OverlappedWrite.hEvent);
        pThis->OverlappedWrite.hEvent = NULL;
    }
    if (pThis->hEvtWake != NULL)
    {
        CloseHandle(pThis->hEvtWake);
        pThis->hEvtWake = NULL;
    }
#else /* !RT_OS_WINDOWS */
    Assert(pThis->LocalSocketServer == -1);

    if (pThis->hSock != NIL_RTSOCKET)
    {
        int rc = RTPollSetRemove(pThis->hPollSet, DRVNAMEDPIPE_POLLSET_ID_SOCKET);
        AssertRC(rc);

        rc = RTSocketShutdown(pThis->hSock, true /* fRead */, true /* fWrite */);
        AssertRC(rc);

        rc = RTSocketClose(pThis->hSock);
        AssertRC(rc); RT_NOREF(rc);

        pThis->hSock = NIL_RTSOCKET;
    }

    if (pThis->hPipeWakeR != NIL_RTPIPE)
    {
        int rc = RTPipeClose(pThis->hPipeWakeR);
        AssertRC(rc);

        pThis->hPipeWakeR = NIL_RTPIPE;
    }

    if (pThis->hPipeWakeW != NIL_RTPIPE)
    {
        int rc = RTPipeClose(pThis->hPipeWakeW);
        AssertRC(rc);

        pThis->hPipeWakeW = NIL_RTPIPE;
    }

    if (pThis->hPollSet != NIL_RTPOLLSET)
    {
        int rc = RTPollSetDestroy(pThis->hPollSet);
        AssertRC(rc);

        pThis->hPollSet = NIL_RTPOLLSET;
    }

    if (   pThis->fIsServer
        && pThis->pszLocation)
        RTFileDelete(pThis->pszLocation);
#endif /* !RT_OS_WINDOWS */

    PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszLocation);
    pThis->pszLocation = NULL;

    /*
     * Wait for the thread.
     */
    if (pThis->ListenThread != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(pThis->ListenThread, 30000, NULL);
        if (RT_SUCCESS(rc))
            pThis->ListenThread = NIL_RTTHREAD;
        else
            LogRel(("NamedPipe%d: listen thread did not terminate (%Rrc)\n", pDrvIns->iInstance, rc));
    }

    /*
     * The last bits of cleanup.
     */
#ifdef RT_OS_WINDOWS
    if (pThis->ListenSem != NIL_RTSEMEVENT)
    {
        RTSemEventMultiDestroy(pThis->ListenSem);
        pThis->ListenSem = NIL_RTSEMEVENT;
    }
#endif
}


/**
 * Construct a named pipe stream driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvNamedPipeConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVNAMEDPIPE pThis = PDMINS_2_DATA(pDrvIns, PDRVNAMEDPIPE);
    PCPDMDRVHLPR3 pHlp  = pDrvIns->pHlpR3;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    pThis->pszLocation                  = NULL;
    pThis->fIsServer                    = false;
#ifdef RT_OS_WINDOWS
    pThis->NamedPipe                    = INVALID_HANDLE_VALUE;
    pThis->ListenSem                    = NIL_RTSEMEVENTMULTI;
    pThis->OverlappedWrite.hEvent       = NULL;
    pThis->OverlappedRead.hEvent        = NULL;
    pThis->hEvtWake                     = NULL;
#else /* !RT_OS_WINDOWS */
    pThis->LocalSocketServer            = -1;
    pThis->hSock                        = NIL_RTSOCKET;

    pThis->hPollSet                     = NIL_RTPOLLSET;
    pThis->hPipeWakeR                   = NIL_RTPIPE;
    pThis->hPipeWakeW                   = NIL_RTPIPE;
    pThis->fSockInPollSet               = false;
#endif /* !RT_OS_WINDOWS */
    pThis->ListenThread                 = NIL_RTTHREAD;
    pThis->fShutdown                    = false;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvNamedPipeQueryInterface;
    /* IStream */
    pThis->IStream.pfnPoll              = drvNamedPipePoll;
    pThis->IStream.pfnPollInterrupt     = drvNamedPipePollInterrupt;
    pThis->IStream.pfnRead              = drvNamedPipeRead;
    pThis->IStream.pfnWrite             = drvNamedPipeWrite;

    /*
     * Validate and read the configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "Location|IsServer", "");

    int rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "Location", &pThis->pszLocation);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Configuration error: querying \"Location\" resulted in %Rrc"), rc);
    rc = pHlp->pfnCFGMQueryBool(pCfg, "IsServer", &pThis->fIsServer);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Configuration error: querying \"IsServer\" resulted in %Rrc"), rc);

    /*
     * Create/Open the pipe.
     */
#ifdef RT_OS_WINDOWS
    if (pThis->fIsServer)
    {
        pThis->NamedPipe = CreateNamedPipe(pThis->pszLocation,
                                           PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                           1,        /*nMaxInstances*/
                                           32,       /*nOutBufferSize*/
                                           32,       /*nOutBufferSize*/
                                           10000,    /*nDefaultTimeOut*/
                                           NULL);    /* lpSecurityAttributes*/
        if (pThis->NamedPipe == INVALID_HANDLE_VALUE)
        {
            rc = RTErrConvertFromWin32(GetLastError());
            LogRel(("NamedPipe%d: CreateNamedPipe failed rc=%Rrc\n", pThis->pDrvIns->iInstance));
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("NamedPipe#%d failed to create named pipe %s"),
                                       pDrvIns->iInstance, pThis->pszLocation);
        }

        rc = RTSemEventMultiCreate(&pThis->ListenSem);
        AssertRCReturn(rc, rc);

        rc = RTThreadCreate(&pThis->ListenThread, drvNamedPipeListenLoop, (void *)pThis, 0,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "SerPipe");
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, rc,  RT_SRC_POS, N_("NamedPipe#%d failed to create listening thread"),
                                       pDrvIns->iInstance);

    }
    else
    {
        /* Connect to the named pipe. */
        pThis->NamedPipe = CreateFile(pThis->pszLocation, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (pThis->NamedPipe == INVALID_HANDLE_VALUE)
        {
            rc = RTErrConvertFromWin32(GetLastError());
            LogRel(("NamedPipe%d: CreateFile failed rc=%Rrc\n", pThis->pDrvIns->iInstance));
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("NamedPipe#%d failed to connect to named pipe %s"),
                                       pDrvIns->iInstance, pThis->pszLocation);
        }
    }

    memset(&pThis->OverlappedWrite, 0, sizeof(pThis->OverlappedWrite));
    pThis->OverlappedWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    AssertReturn(pThis->OverlappedWrite.hEvent != NULL, VERR_OUT_OF_RESOURCES);

    memset(&pThis->OverlappedRead, 0, sizeof(pThis->OverlappedRead));
    pThis->OverlappedRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    AssertReturn(pThis->OverlappedRead.hEvent != NULL, VERR_OUT_OF_RESOURCES);

    pThis->hEvtWake = CreateEvent(NULL, FALSE, FALSE, NULL);
    AssertReturn(pThis->hEvtWake != NULL, VERR_OUT_OF_RESOURCES);

#else /* !RT_OS_WINDOWS */
    rc = RTPipeCreate(&pThis->hPipeWakeR, &pThis->hPipeWakeW, 0 /* fFlags */);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("DrvTCP#%d: Failed to create wake pipe"), pDrvIns->iInstance);

    rc = RTPollSetCreate(&pThis->hPollSet);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("DrvTCP#%d: Failed to create poll set"), pDrvIns->iInstance);

    rc = RTPollSetAddPipe(pThis->hPollSet, pThis->hPipeWakeR,
                            RTPOLL_EVT_READ | RTPOLL_EVT_ERROR,
                            DRVNAMEDPIPE_POLLSET_ID_WAKEUP);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("DrvTCP#%d failed to add wakeup pipe for %s to poll set"),
                                   pDrvIns->iInstance, pThis->pszLocation);

    int s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s == -1)
        return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("NamedPipe#%d failed to create local socket"), pDrvIns->iInstance);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, pThis->pszLocation, sizeof(addr.sun_path) - 1);

    if (pThis->fIsServer)
    {
        /* Bind address to the local socket. */
        pThis->LocalSocketServer = s;
        RTFileDelete(pThis->pszLocation);
        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
            return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("NamedPipe#%d failed to bind to local socket %s"),
                                       pDrvIns->iInstance, pThis->pszLocation);
        rc = RTThreadCreate(&pThis->ListenThread, drvNamedPipeListenLoop, (void *)pThis, 0,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "SerPipe");
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, rc,  RT_SRC_POS,
                                       N_("NamedPipe#%d failed to create listening thread"), pDrvIns->iInstance);
    }
    else
    {
        /* Connect to the local socket. */
        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
            close(s);
            return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                       N_("NamedPipe#%d failed to connect to local socket %s"),
                                       pDrvIns->iInstance, pThis->pszLocation);
        }

        rc = RTSocketFromNative(&pThis->hSock, s);
        if (RT_FAILURE(rc))
        {
            close(s);
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                       N_("NamedPipe#%d failed to wrap socket %Rrc"),
                                       pDrvIns->iInstance, pThis->pszLocation);
        }
    }
#endif /* !RT_OS_WINDOWS */

    LogRel(("NamedPipe: location %s, %s\n", pThis->pszLocation, pThis->fIsServer ? "server" : "client"));
    return VINF_SUCCESS;
}


/**
 * Named pipe driver registration record.
 */
const PDMDRVREG g_DrvNamedPipe =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "NamedPipe",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Named Pipe stream driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STREAM,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVNAMEDPIPE),
    /* pfnConstruct */
    drvNamedPipeConstruct,
    /* pfnDestruct */
    drvNamedPipeDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvNamedPipePowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

