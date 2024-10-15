/* $Id: VDIfTcpNet.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VD - Virtual disk container implementation, Default TCP/IP interface implementation.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/vd.h>
#include <VBox/err.h>
#include <iprt/asm.h>
#include <iprt/log.h>
#include <iprt/tcp.h>
#include <iprt/sg.h>
#include <iprt/poll.h>
#include <iprt/pipe.h>
#include <iprt/system.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Pollset id of the socket. */
#define VDSOCKET_POLL_ID_SOCKET 0
/** Pollset id of the pipe. */
#define VDSOCKET_POLL_ID_PIPE   1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Socket data.
 */
typedef struct VDSOCKETINT
{
    /** IPRT socket handle. */
    RTSOCKET      hSocket;
    /** Pollset with the wakeup pipe and socket. */
    RTPOLLSET     hPollSet;
    /** Pipe endpoint - read (in the pollset). */
    RTPIPE        hPipeR;
    /** Pipe endpoint - write. */
    RTPIPE        hPipeW;
    /** Flag whether the thread was woken up. */
    volatile bool fWokenUp;
    /** Flag whether the thread is waiting in the select call. */
    volatile bool fWaiting;
    /** Old event mask. */
    uint32_t      fEventsOld;
} VDSOCKETINT, *PVDSOCKETINT;


/**
 * VD TCP/NET interface instance data.
 */
typedef struct VDIFINSTINT
{
    /** The TCP/NET interface descriptor. */
    VDINTERFACETCPNET    VdIfTcpNet;
} VDIFINSTINT;
/** Pointer to the VD TCP/NET interface instance data. */
typedef VDIFINSTINT *PVDIFINSTINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/** @interface_method_impl{VDINTERFACETCPNET,pfnSocketCreate} */
static DECLCALLBACK(int) vdIfTcpNetSocketCreate(uint32_t fFlags, PVDSOCKET phVdSock)
{
    int rc = VINF_SUCCESS;
    int rc2 = VINF_SUCCESS;
    PVDSOCKETINT pSockInt = NULL;

    pSockInt = (PVDSOCKETINT)RTMemAllocZ(sizeof(VDSOCKETINT));
    if (!pSockInt)
        return VERR_NO_MEMORY;

    pSockInt->hSocket  = NIL_RTSOCKET;
    pSockInt->hPollSet = NIL_RTPOLLSET;
    pSockInt->hPipeR   = NIL_RTPIPE;
    pSockInt->hPipeW   = NIL_RTPIPE;
    pSockInt->fWokenUp = false;
    pSockInt->fWaiting = false;

    if (fFlags & VD_INTERFACETCPNET_CONNECT_EXTENDED_SELECT)
    {
        /* Init pipe and pollset. */
        rc = RTPipeCreate(&pSockInt->hPipeR, &pSockInt->hPipeW, 0);
        if (RT_SUCCESS(rc))
        {
            rc = RTPollSetCreate(&pSockInt->hPollSet);
            if (RT_SUCCESS(rc))
            {
                rc = RTPollSetAddPipe(pSockInt->hPollSet, pSockInt->hPipeR,
                                      RTPOLL_EVT_READ, VDSOCKET_POLL_ID_PIPE);
                if (RT_SUCCESS(rc))
                {
                    *phVdSock = pSockInt;
                    return VINF_SUCCESS;
                }

                RTPollSetRemove(pSockInt->hPollSet, VDSOCKET_POLL_ID_PIPE);
                rc2 = RTPollSetDestroy(pSockInt->hPollSet);
                AssertRC(rc2);
            }

            rc2 = RTPipeClose(pSockInt->hPipeR);
            AssertRC(rc2);
            rc2 = RTPipeClose(pSockInt->hPipeW);
            AssertRC(rc2);
        }
    }
    else
    {
        *phVdSock = pSockInt;
        return VINF_SUCCESS;
    }

    RTMemFree(pSockInt);

    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSocketDestroy} */
static DECLCALLBACK(int) vdIfTcpNetSocketDestroy(VDSOCKET hVdSock)
{
    int rc = VINF_SUCCESS;
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    /* Destroy the pipe and pollset if necessary. */
    if (pSockInt->hPollSet != NIL_RTPOLLSET)
    {
        if (pSockInt->hSocket != NIL_RTSOCKET)
        {
            rc = RTPollSetRemove(pSockInt->hPollSet, VDSOCKET_POLL_ID_SOCKET);
            Assert(RT_SUCCESS(rc) || rc == VERR_POLL_HANDLE_ID_NOT_FOUND);
        }
        rc = RTPollSetRemove(pSockInt->hPollSet, VDSOCKET_POLL_ID_PIPE);
        AssertRC(rc);
        rc = RTPollSetDestroy(pSockInt->hPollSet);
        AssertRC(rc);
        rc = RTPipeClose(pSockInt->hPipeR);
        AssertRC(rc);
        rc = RTPipeClose(pSockInt->hPipeW);
        AssertRC(rc);
    }

    if (pSockInt->hSocket != NIL_RTSOCKET)
        rc = RTTcpClientCloseEx(pSockInt->hSocket, false /*fGracefulShutdown*/);

    RTMemFree(pSockInt);

    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnClientConnect} */
static DECLCALLBACK(int) vdIfTcpNetClientConnect(VDSOCKET hVdSock, const char *pszAddress, uint32_t uPort,
                                                  RTMSINTERVAL cMillies)
{
    int rc = VINF_SUCCESS;
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    rc = RTTcpClientConnectEx(pszAddress, uPort, &pSockInt->hSocket, cMillies, NULL);
    if (RT_SUCCESS(rc))
    {
        /* Add to the pollset if required. */
        if (pSockInt->hPollSet != NIL_RTPOLLSET)
        {
            pSockInt->fEventsOld = RTPOLL_EVT_READ | RTPOLL_EVT_WRITE | RTPOLL_EVT_ERROR;

            rc = RTPollSetAddSocket(pSockInt->hPollSet, pSockInt->hSocket,
                                    pSockInt->fEventsOld, VDSOCKET_POLL_ID_SOCKET);
        }

        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        rc = RTTcpClientCloseEx(pSockInt->hSocket, false /*fGracefulShutdown*/);
    }

    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnClientClose} */
static DECLCALLBACK(int) vdIfTcpNetClientClose(VDSOCKET hVdSock)
{
    int rc = VINF_SUCCESS;
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    if (pSockInt->hPollSet != NIL_RTPOLLSET)
    {
        rc = RTPollSetRemove(pSockInt->hPollSet, VDSOCKET_POLL_ID_SOCKET);
        AssertRC(rc);
    }

    rc = RTTcpClientCloseEx(pSockInt->hSocket, false /*fGracefulShutdown*/);
    pSockInt->hSocket = NIL_RTSOCKET;

    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnIsClientConnected} */
static DECLCALLBACK(bool) vdIfTcpNetIsClientConnected(VDSOCKET hVdSock)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return pSockInt->hSocket != NIL_RTSOCKET;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSelectOne} */
static DECLCALLBACK(int) vdIfTcpNetSelectOne(VDSOCKET hVdSock, RTMSINTERVAL cMillies)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpSelectOne(pSockInt->hSocket, cMillies);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnRead} */
static DECLCALLBACK(int) vdIfTcpNetRead(VDSOCKET hVdSock, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpRead(pSockInt->hSocket, pvBuffer, cbBuffer, pcbRead);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnWrite} */
static DECLCALLBACK(int) vdIfTcpNetWrite(VDSOCKET hVdSock, const void *pvBuffer, size_t cbBuffer)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpWrite(pSockInt->hSocket, pvBuffer, cbBuffer);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSgWrite} */
static DECLCALLBACK(int) vdIfTcpNetSgWrite(VDSOCKET hVdSock, PCRTSGBUF pSgBuf)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpSgWrite(pSockInt->hSocket, pSgBuf);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnReadNB} */
static DECLCALLBACK(int) vdIfTcpNetReadNB(VDSOCKET hVdSock, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpReadNB(pSockInt->hSocket, pvBuffer, cbBuffer, pcbRead);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnWriteNB} */
static DECLCALLBACK(int) vdIfTcpNetWriteNB(VDSOCKET hVdSock, const void *pvBuffer, size_t cbBuffer, size_t *pcbWritten)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpWriteNB(pSockInt->hSocket, pvBuffer, cbBuffer, pcbWritten);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSgWriteNB} */
static DECLCALLBACK(int) vdIfTcpNetSgWriteNB(VDSOCKET hVdSock, PRTSGBUF pSgBuf, size_t *pcbWritten)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpSgWriteNB(pSockInt->hSocket, pSgBuf, pcbWritten);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnFlush} */
static DECLCALLBACK(int) vdIfTcpNetFlush(VDSOCKET hVdSock)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpFlush(pSockInt->hSocket);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSetSendCoalescing} */
static DECLCALLBACK(int) vdIfTcpNetSetSendCoalescing(VDSOCKET hVdSock, bool fEnable)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpSetSendCoalescing(pSockInt->hSocket, fEnable);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnGetLocalAddress} */
static DECLCALLBACK(int) vdIfTcpNetGetLocalAddress(VDSOCKET hVdSock, PRTNETADDR pAddr)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpGetLocalAddress(pSockInt->hSocket, pAddr);
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnGetPeerAddress} */
static DECLCALLBACK(int) vdIfTcpNetGetPeerAddress(VDSOCKET hVdSock, PRTNETADDR pAddr)
{
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    return RTTcpGetPeerAddress(pSockInt->hSocket, pAddr);
}

static DECLCALLBACK(int) vdIfTcpNetSelectOneExPoll(VDSOCKET hVdSock, uint32_t fEvents,
                                                    uint32_t *pfEvents, RTMSINTERVAL cMillies)
{
    int rc = VINF_SUCCESS;
    uint32_t id = 0;
    uint32_t fEventsRecv = 0;
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    *pfEvents = 0;

    if (   pSockInt->fEventsOld != fEvents
        && pSockInt->hSocket != NIL_RTSOCKET)
    {
        uint32_t fPollEvents = 0;

        if (fEvents & VD_INTERFACETCPNET_EVT_READ)
            fPollEvents |= RTPOLL_EVT_READ;
        if (fEvents & VD_INTERFACETCPNET_EVT_WRITE)
            fPollEvents |= RTPOLL_EVT_WRITE;
        if (fEvents & VD_INTERFACETCPNET_EVT_ERROR)
            fPollEvents |= RTPOLL_EVT_ERROR;

        rc = RTPollSetEventsChange(pSockInt->hPollSet, VDSOCKET_POLL_ID_SOCKET, fPollEvents);
        if (RT_FAILURE(rc))
            return rc;

        pSockInt->fEventsOld = fEvents;
    }

    ASMAtomicXchgBool(&pSockInt->fWaiting, true);
    if (ASMAtomicXchgBool(&pSockInt->fWokenUp, false))
    {
        ASMAtomicXchgBool(&pSockInt->fWaiting, false);
        return VERR_INTERRUPTED;
    }

    rc = RTPoll(pSockInt->hPollSet, cMillies, &fEventsRecv, &id);
    Assert(RT_SUCCESS(rc) || rc == VERR_TIMEOUT);

    ASMAtomicXchgBool(&pSockInt->fWaiting, false);

    if (RT_SUCCESS(rc))
    {
        if (id == VDSOCKET_POLL_ID_SOCKET)
        {
            fEventsRecv &= RTPOLL_EVT_VALID_MASK;

            if (fEventsRecv & RTPOLL_EVT_READ)
                *pfEvents |= VD_INTERFACETCPNET_EVT_READ;
            if (fEventsRecv & RTPOLL_EVT_WRITE)
                *pfEvents |= VD_INTERFACETCPNET_EVT_WRITE;
            if (fEventsRecv & RTPOLL_EVT_ERROR)
                *pfEvents |= VD_INTERFACETCPNET_EVT_ERROR;
        }
        else
        {
            size_t cbRead = 0;
            uint8_t abBuf[10];
            Assert(id == VDSOCKET_POLL_ID_PIPE);
            Assert((fEventsRecv & RTPOLL_EVT_VALID_MASK) == RTPOLL_EVT_READ);

            /* We got interrupted, drain the pipe. */
            rc = RTPipeRead(pSockInt->hPipeR, abBuf, sizeof(abBuf), &cbRead);
            AssertRC(rc);

            ASMAtomicXchgBool(&pSockInt->fWokenUp, false);

            rc = VERR_INTERRUPTED;
        }
    }

    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSelectOneEx} */
static DECLCALLBACK(int) vdIfTcpNetSelectOneExNoPoll(VDSOCKET hVdSock, uint32_t fEvents, uint32_t *pfEvents, RTMSINTERVAL cMillies)
{
    RT_NOREF(cMillies); /** @todo timeouts */
    int rc = VINF_SUCCESS;
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    *pfEvents = 0;

    ASMAtomicXchgBool(&pSockInt->fWaiting, true);
    if (ASMAtomicXchgBool(&pSockInt->fWokenUp, false))
    {
        ASMAtomicXchgBool(&pSockInt->fWaiting, false);
        return VERR_INTERRUPTED;
    }

    if (   pSockInt->hSocket == NIL_RTSOCKET
        || !fEvents)
    {
        /*
         * Only the pipe is configured or the caller doesn't wait for a socket event,
         * wait until there is something to read from the pipe.
         */
        size_t cbRead = 0;
        char ch = 0;
        rc = RTPipeReadBlocking(pSockInt->hPipeR, &ch, 1, &cbRead);
        if (RT_SUCCESS(rc))
        {
            Assert(cbRead == 1);
            rc = VERR_INTERRUPTED;
            ASMAtomicXchgBool(&pSockInt->fWokenUp, false);
        }
    }
    else
    {
        uint32_t fSelectEvents = 0;

        if (fEvents & VD_INTERFACETCPNET_EVT_READ)
            fSelectEvents |= RTSOCKET_EVT_READ;
        if (fEvents & VD_INTERFACETCPNET_EVT_WRITE)
            fSelectEvents |= RTSOCKET_EVT_WRITE;
        if (fEvents & VD_INTERFACETCPNET_EVT_ERROR)
            fSelectEvents |= RTSOCKET_EVT_ERROR;

        if (fEvents & VD_INTERFACETCPNET_HINT_INTERRUPT)
        {
            uint32_t fEventsRecv = 0;

            /* Make sure the socket is not in the pollset. */
            rc = RTPollSetRemove(pSockInt->hPollSet, VDSOCKET_POLL_ID_SOCKET);
            Assert(RT_SUCCESS(rc) || rc == VERR_POLL_HANDLE_ID_NOT_FOUND);

            for (;;)
            {
                uint32_t id = 0;
                rc = RTPoll(pSockInt->hPollSet, 5, &fEvents, &id);
                if (rc == VERR_TIMEOUT)
                {
                    /* Check the socket. */
                    rc = RTTcpSelectOneEx(pSockInt->hSocket, fSelectEvents, &fEventsRecv, 0);
                    if (RT_SUCCESS(rc))
                    {
                        if (fEventsRecv & RTSOCKET_EVT_READ)
                            *pfEvents |= VD_INTERFACETCPNET_EVT_READ;
                        if (fEventsRecv & RTSOCKET_EVT_WRITE)
                            *pfEvents |= VD_INTERFACETCPNET_EVT_WRITE;
                        if (fEventsRecv & RTSOCKET_EVT_ERROR)
                            *pfEvents |= VD_INTERFACETCPNET_EVT_ERROR;
                        break; /* Quit */
                    }
                    else if (rc != VERR_TIMEOUT)
                        break;
                }
                else if (RT_SUCCESS(rc))
                {
                    size_t cbRead = 0;
                    uint8_t abBuf[10];
                    Assert(id == VDSOCKET_POLL_ID_PIPE);
                    Assert((fEventsRecv & RTPOLL_EVT_VALID_MASK) == RTPOLL_EVT_READ);

                    /* We got interrupted, drain the pipe. */
                    rc = RTPipeRead(pSockInt->hPipeR, abBuf, sizeof(abBuf), &cbRead);
                    AssertRC(rc);

                    ASMAtomicXchgBool(&pSockInt->fWokenUp, false);

                    rc = VERR_INTERRUPTED;
                    break;
                }
                else
                    break;
            }
        }
        else /* The caller waits for a socket event. */
        {
            uint32_t fEventsRecv = 0;

            /* Loop until we got woken up or a socket event occurred. */
            for (;;)
            {
                /** @todo find an adaptive wait algorithm based on the
                 * number of wakeups in the past. */
                rc = RTTcpSelectOneEx(pSockInt->hSocket, fSelectEvents, &fEventsRecv, 5);
                if (rc == VERR_TIMEOUT)
                {
                    /* Check if there is an event pending. */
                    size_t cbRead = 0;
                    char ch = 0;
                    rc = RTPipeRead(pSockInt->hPipeR, &ch, 1, &cbRead);
                    if (RT_SUCCESS(rc) && rc != VINF_TRY_AGAIN)
                    {
                        Assert(cbRead == 1);
                        rc = VERR_INTERRUPTED;
                        ASMAtomicXchgBool(&pSockInt->fWokenUp, false);
                        break; /* Quit */
                    }
                    else
                        Assert(rc == VINF_TRY_AGAIN);
                }
                else if (RT_SUCCESS(rc))
                {
                    if (fEventsRecv & RTSOCKET_EVT_READ)
                        *pfEvents |= VD_INTERFACETCPNET_EVT_READ;
                    if (fEventsRecv & RTSOCKET_EVT_WRITE)
                        *pfEvents |= VD_INTERFACETCPNET_EVT_WRITE;
                    if (fEventsRecv & RTSOCKET_EVT_ERROR)
                        *pfEvents |= VD_INTERFACETCPNET_EVT_ERROR;
                    break; /* Quit */
                }
                else
                    break;
            }
        }
    }

    ASMAtomicXchgBool(&pSockInt->fWaiting, false);

    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnPoke} */
static DECLCALLBACK(int) vdIfTcpNetPoke(VDSOCKET hVdSock)
{
    int rc = VINF_SUCCESS;
    size_t cbWritten = 0;
    PVDSOCKETINT pSockInt = (PVDSOCKETINT)hVdSock;

    ASMAtomicXchgBool(&pSockInt->fWokenUp, true);

    if (ASMAtomicReadBool(&pSockInt->fWaiting))
    {
        rc = RTPipeWrite(pSockInt->hPipeW, "", 1, &cbWritten);
        Assert(RT_SUCCESS(rc) || cbWritten == 0);
    }

    return VINF_SUCCESS;
}


VBOXDDU_DECL(int) VDIfTcpNetInstDefaultCreate(PVDIFINST phTcpNetInst, PVDINTERFACE *ppVdIfs)
{
    AssertPtrReturn(phTcpNetInst, VERR_INVALID_POINTER);
    AssertPtrReturn(ppVdIfs, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PVDIFINSTINT pThis = (PVDIFINSTINT)RTMemAllocZ(sizeof(*pThis));
    if (RT_LIKELY(pThis))
    {
        pThis->VdIfTcpNet.pfnSocketCreate      = vdIfTcpNetSocketCreate;
        pThis->VdIfTcpNet.pfnSocketDestroy     = vdIfTcpNetSocketDestroy;
        pThis->VdIfTcpNet.pfnClientConnect     = vdIfTcpNetClientConnect;
        pThis->VdIfTcpNet.pfnIsClientConnected = vdIfTcpNetIsClientConnected;
        pThis->VdIfTcpNet.pfnClientClose       = vdIfTcpNetClientClose;
        pThis->VdIfTcpNet.pfnSelectOne         = vdIfTcpNetSelectOne;
        pThis->VdIfTcpNet.pfnRead              = vdIfTcpNetRead;
        pThis->VdIfTcpNet.pfnWrite             = vdIfTcpNetWrite;
        pThis->VdIfTcpNet.pfnSgWrite           = vdIfTcpNetSgWrite;
        pThis->VdIfTcpNet.pfnReadNB            = vdIfTcpNetReadNB;
        pThis->VdIfTcpNet.pfnWriteNB           = vdIfTcpNetWriteNB;
        pThis->VdIfTcpNet.pfnSgWriteNB         = vdIfTcpNetSgWriteNB;
        pThis->VdIfTcpNet.pfnFlush             = vdIfTcpNetFlush;
        pThis->VdIfTcpNet.pfnSetSendCoalescing = vdIfTcpNetSetSendCoalescing;
        pThis->VdIfTcpNet.pfnGetLocalAddress   = vdIfTcpNetGetLocalAddress;
        pThis->VdIfTcpNet.pfnGetPeerAddress    = vdIfTcpNetGetPeerAddress;
        pThis->VdIfTcpNet.pfnPoke              = vdIfTcpNetPoke;

        /*
         * There is a 15ms delay between receiving the data and marking the socket
         * as readable on Windows XP which hurts async I/O performance of
         * TCP backends badly. Provide a different select method without
         * using poll on XP.
         * This is only used on XP because it is not as efficient as the one using poll
         * and all other Windows versions are working fine.
         */
        char szOS[64];
        memset(szOS, 0, sizeof(szOS));
        rc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, &szOS[0], sizeof(szOS));

        if (RT_SUCCESS(rc) && !strncmp(szOS, "Windows XP", 10))
        {
            LogRel(("VD: Detected Windows XP, disabled poll based waiting for TCP\n"));
            pThis->VdIfTcpNet.pfnSelectOneEx = vdIfTcpNetSelectOneExNoPoll;
        }
        else
            pThis->VdIfTcpNet.pfnSelectOneEx = vdIfTcpNetSelectOneExPoll;

        rc = VDInterfaceAdd(&pThis->VdIfTcpNet.Core, "VD_IfTcpNet",
                            VDINTERFACETYPE_TCPNET, NULL,
                            sizeof(VDINTERFACETCPNET), ppVdIfs);
        AssertRC(rc);

        if (RT_SUCCESS(rc))
            *phTcpNetInst = pThis;
        else
            RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


VBOXDDU_DECL(void) VDIfTcpNetInstDefaultDestroy(VDIFINST hTcpNetInst)
{
    PVDIFINSTINT pThis = hTcpNetInst;
    AssertPtrReturnVoid(pThis);

    RTMemFree(pThis);
}

