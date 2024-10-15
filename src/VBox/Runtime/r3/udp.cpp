/* $Id: udp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UDP/IP.
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
#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <errno.h>
# include <netinet/in.h>
# include <netinet/udp.h>
# include <arpa/inet.h>
# include <netdb.h>
#endif
#include <limits.h>

#include "internal/iprt.h"
#include <iprt/udp.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mempool.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/socket.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "internal/magics.h"
#include "internal/socket.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* fixup backlevel OSes. */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define socklen_t              int
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * UDP Server state.
 */
typedef enum RTUDPSERVERSTATE
{
    /** Invalid. */
    RTUDPSERVERSTATE_INVALID = 0,
    /** Created. */
    RTUDPSERVERSTATE_CREATED,
    /** Thread for incoming datagrams is starting up. */
    RTUDPSERVERSTATE_STARTING,
    /** Waiting for incoming datagrams. */
    RTUDPSERVERSTATE_WAITING,
    /** Handling an incoming datagram. */
    RTUDPSERVERSTATE_RECEIVING,
    /** Thread terminating. */
    RTUDPSERVERSTATE_STOPPING,
    /** Thread terminated. */
    RTUDPSERVERSTATE_STOPPED,
    /** Final cleanup before being unusable. */
    RTUDPSERVERSTATE_DESTROYING
} RTUDPSERVERSTATE;

/*
 * Internal representation of the UDP Server handle.
 */
typedef struct RTUDPSERVER
{
    /** The magic value (RTUDPSERVER_MAGIC). */
    uint32_t volatile           u32Magic;
    /** The server state. */
    RTUDPSERVERSTATE volatile   enmState;
    /** The server thread. */
    RTTHREAD                    Thread;
    /** The server socket. */
    RTSOCKET volatile           hSocket;
    /** The datagram receiver function. */
    PFNRTUDPSERVE               pfnServe;
    /** Argument to pfnServer. */
    void                       *pvUser;
} RTUDPSERVER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  rtUdpServerThread(RTTHREAD ThreadSelf, void *pvServer);
static int  rtUdpServerListen(PRTUDPSERVER pServer);
static int  rtUdpServerListenCleanup(PRTUDPSERVER pServer);
static int  rtUdpServerDestroySocket(RTSOCKET volatile *pSock, const char *pszMsg);
static int  rtUdpClose(RTSOCKET Sock, const char *pszMsg);


/**
 * Atomicly updates a socket variable.
 * @returns The old handle value.
 * @param   phSock          The socket handle variable to update.
 * @param   hNew            The new socket handle value.
 */
DECLINLINE(RTSOCKET) rtUdpAtomicXchgSock(RTSOCKET volatile *phSock, const RTSOCKET hNew)
{
    RTSOCKET hRet;
    ASMAtomicXchgHandle(phSock, hNew, &hRet);
    return hRet;
}


/**
 * Tries to change the UDP server state.
 */
DECLINLINE(bool) rtUdpServerTrySetState(PRTUDPSERVER pServer, RTUDPSERVERSTATE enmStateNew, RTUDPSERVERSTATE enmStateOld)
{
    bool fRc;
    ASMAtomicCmpXchgSize(&pServer->enmState, enmStateNew, enmStateOld, fRc);
    return fRc;
}

/**
 * Changes the UDP server state.
 */
DECLINLINE(void) rtUdpServerSetState(PRTUDPSERVER pServer, RTUDPSERVERSTATE enmStateNew, RTUDPSERVERSTATE enmStateOld)
{
    bool fRc;
    ASMAtomicCmpXchgSize(&pServer->enmState, enmStateNew, enmStateOld, fRc);
    Assert(fRc); NOREF(fRc);
}


/**
 * Closes a socket.
 *
 * @returns IPRT status code.
 */
static int rtUdpServerDestroySocket(RTSOCKET volatile *pSock, const char *pszMsg)
{
    RTSOCKET hSocket = rtUdpAtomicXchgSock(pSock, NIL_RTSOCKET);
    if (hSocket != NIL_RTSOCKET)
    {
        return rtUdpClose(hSocket, pszMsg);
    }
    return VINF_UDP_SERVER_NO_CLIENT;
}


RTR3DECL(int)  RTUdpServerCreate(const char *pszAddress, unsigned uPort, RTTHREADTYPE enmType, const char *pszThrdName,
                                 PFNRTUDPSERVE pfnServe, void *pvUser, PPRTUDPSERVER ppServer)
{
    /*
     * Validate input.
     */
    AssertReturn(uPort > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfnServe, VERR_INVALID_POINTER);
    AssertPtrReturn(pszThrdName, VERR_INVALID_POINTER);
    AssertPtrReturn(ppServer, VERR_INVALID_POINTER);

    /*
     * Create the server.
     */
    PRTUDPSERVER pServer;
    int rc = RTUdpServerCreateEx(pszAddress, uPort, &pServer);
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the listener thread.
         */
        RTMemPoolRetain(pServer);
        pServer->enmState   = RTUDPSERVERSTATE_STARTING;
        pServer->pvUser     = pvUser;
        pServer->pfnServe   = pfnServe;
        rc = RTThreadCreate(&pServer->Thread, rtUdpServerThread, pServer, 0, enmType, /*RTTHREADFLAGS_WAITABLE*/0, pszThrdName);
        if (RT_SUCCESS(rc))
        {
            /* done */
            if (ppServer)
                *ppServer = pServer;
            else
                RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
            return rc;
        }
        RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);

        /*
         * Destroy the server.
         */
        rtUdpServerSetState(pServer, RTUDPSERVERSTATE_CREATED, RTUDPSERVERSTATE_STARTING);
        RTUdpServerDestroy(pServer);
    }

    return rc;
}


/**
 * Server thread, loops waiting for datagrams until it's terminated.
 *
 * @returns iprt status code. (ignored).
 * @param   ThreadSelf      Thread handle.
 * @param   pvServer        Server handle.
 */
static DECLCALLBACK(int)  rtUdpServerThread(RTTHREAD ThreadSelf, void *pvServer)
{
    PRTUDPSERVER    pServer = (PRTUDPSERVER)pvServer;
    int             rc;
    if (rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_WAITING, RTUDPSERVERSTATE_STARTING))
        rc = rtUdpServerListen(pServer);
    else
        rc = rtUdpServerListenCleanup(pServer);
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    NOREF(ThreadSelf);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTUdpServerCreateEx(const char *pszAddress, uint32_t uPort, PPRTUDPSERVER ppServer)
{

    /*
     * Validate input.
     */
    AssertReturn(uPort > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppServer, VERR_INVALID_PARAMETER);

    /*
     * Resolve the address.
     */
    RTNETADDR LocalAddr;
    int rc = RTSocketParseInetAddress(pszAddress, uPort, &LocalAddr);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Setting up socket.
     */
    RTSOCKET Sock;
    rc = rtSocketCreate(&Sock, AF_INET, SOCK_DGRAM, IPPROTO_UDP, false /*fInheritable*/);
    if (RT_SUCCESS(rc))
    {
        /*
         * Set socket options.
         */
        int fFlag = 1;
        if (!rtSocketSetOpt(Sock, SOL_SOCKET, SO_REUSEADDR, &fFlag, sizeof(fFlag)))
        {
            /*
             * Bind a name to the socket.
             */
            rc = rtSocketBind(Sock, &LocalAddr);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create the server handle.
                 */
                PRTUDPSERVER pServer = (PRTUDPSERVER)RTMemPoolAlloc(RTMEMPOOL_DEFAULT, sizeof(*pServer));
                if (pServer)
                {
                    pServer->u32Magic   = RTUDPSERVER_MAGIC;
                    pServer->enmState   = RTUDPSERVERSTATE_CREATED;
                    pServer->Thread     = NIL_RTTHREAD;
                    pServer->hSocket    = Sock;
                    pServer->pfnServe   = NULL;
                    pServer->pvUser     = NULL;
                    *ppServer = pServer;
                    return VINF_SUCCESS;
                }

                /* bail out */
                rc = VERR_NO_MEMORY;
            }
        }
        else
            AssertMsgFailed(("rtSocketSetOpt: %Rrc\n", rc));
        rtUdpClose(Sock, "RTServerCreateEx");
    }

    return rc;
}


RTR3DECL(int) RTUdpServerListen(PRTUDPSERVER pServer, PFNRTUDPSERVE pfnServe, void *pvUser)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pfnServe, VERR_INVALID_POINTER);
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTUDPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE);

    int rc = VERR_INVALID_STATE;
    if (rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_WAITING, RTUDPSERVERSTATE_CREATED))
    {
        Assert(!pServer->pfnServe);
        Assert(!pServer->pvUser);
        Assert(pServer->Thread == NIL_RTTHREAD);

        pServer->pfnServe = pfnServe;
        pServer->pvUser   = pvUser;
        pServer->Thread   = RTThreadSelf();
        Assert(pServer->Thread != NIL_RTTHREAD);
        rc = rtUdpServerListen(pServer);
    }
    else
    {
        AssertMsgFailed(("enmState=%d\n", pServer->enmState));
        rc = VERR_INVALID_STATE;
    }
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    return rc;
}


/**
 * Internal worker common for RTUdpServerListen and the thread created by
 * RTUdpServerCreate().
 *
 * The caller makes sure it has its own memory reference and releases it upon
 * return.
 */
static int rtUdpServerListen(PRTUDPSERVER pServer)
{
    /*
     * Wait for incoming datagrams loop.
     */
    for (;;)
    {
        /*
         * Change state, getting an extra reference to the socket so we can
         * allow others to close it while we're stuck in rtSocketAccept.
         */
        RTUDPSERVERSTATE    enmState      = pServer->enmState;
        RTSOCKET            hSocket;
        ASMAtomicReadHandle(&pServer->hSocket, &hSocket);
        if (hSocket != NIL_RTSOCKET)
            RTSocketRetain(hSocket);
        if (    enmState != RTUDPSERVERSTATE_WAITING
            &&  enmState != RTUDPSERVERSTATE_RECEIVING)
        {
            RTSocketRelease(hSocket);
            return rtUdpServerListenCleanup(pServer);
        }
        if (!rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_WAITING, enmState))
        {
            RTSocketRelease(hSocket);
            continue;
        }

        /*
         * Wait for incoming datagrams or errors.
         */
        uint32_t fEvents;
        int rc = RTSocketSelectOneEx(hSocket, RTSOCKET_EVT_READ | RTSOCKET_EVT_ERROR, &fEvents, 1000);
        RTSocketRelease(hSocket);
        if (rc == VERR_TIMEOUT)
            continue;
        if (RT_FAILURE(rc))
        {
            /* These are typical for what can happen during destruction. */
            if (   rc == VERR_INVALID_HANDLE
                || rc == VERR_INVALID_PARAMETER
                || rc == VERR_NET_NOT_SOCKET)
                return rtUdpServerListenCleanup(pServer);
            continue;
        }
        if (fEvents & RTSOCKET_EVT_ERROR)
            return rtUdpServerListenCleanup(pServer);

        /*
         * Run a pfnServe callback.
         */
        if (!rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_RECEIVING, RTUDPSERVERSTATE_WAITING))
            return rtUdpServerListenCleanup(pServer);
        rc = pServer->pfnServe(hSocket, pServer->pvUser);

        /*
         * Stop the server?
         */
        if (rc == VERR_UDP_SERVER_STOP)
        {
            if (rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_STOPPING, RTUDPSERVERSTATE_RECEIVING))
            {
                /*
                 * Reset the server socket and change the state to stopped. After that state change
                 * we cannot safely access the handle so we'll have to return here.
                 */
                hSocket = rtUdpAtomicXchgSock(&pServer->hSocket, NIL_RTSOCKET);
                rtUdpServerSetState(pServer, RTUDPSERVERSTATE_STOPPED, RTUDPSERVERSTATE_STOPPING);
                rtUdpClose(hSocket, "Listener: server stopped");
            }
            else
                rtUdpServerListenCleanup(pServer); /* ignore rc */
            return rc;
        }
    }
}


/**
 * Clean up after listener.
 */
static int rtUdpServerListenCleanup(PRTUDPSERVER pServer)
{
    /*
     * Close the server socket.
     */
    rtUdpServerDestroySocket(&pServer->hSocket, "ListenCleanup");

    /*
     * Figure the return code and make sure the state is OK.
     */
    RTUDPSERVERSTATE enmState = pServer->enmState;
    switch (enmState)
    {
        case RTUDPSERVERSTATE_STOPPING:
        case RTUDPSERVERSTATE_STOPPED:
            return VERR_UDP_SERVER_SHUTDOWN;

        case RTUDPSERVERSTATE_WAITING:
            rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_STOPPED, enmState);
            return VERR_UDP_SERVER_DESTROYED;

        case RTUDPSERVERSTATE_DESTROYING:
            return VERR_UDP_SERVER_DESTROYED;

        case RTUDPSERVERSTATE_STARTING:
        case RTUDPSERVERSTATE_RECEIVING:
        default:
            AssertMsgFailedReturn(("pServer=%p enmState=%d\n", pServer, enmState), VERR_INTERNAL_ERROR_4);
    }
}


RTR3DECL(int) RTUdpServerShutdown(PRTUDPSERVER pServer)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTUDPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Try change the state to stopping, then replace and destroy the server socket.
     */
    for (;;)
    {
        RTUDPSERVERSTATE enmState = pServer->enmState;
        if (    enmState != RTUDPSERVERSTATE_WAITING
            &&  enmState != RTUDPSERVERSTATE_RECEIVING)
        {
            RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
            switch (enmState)
            {
                case RTUDPSERVERSTATE_CREATED:
                case RTUDPSERVERSTATE_STARTING:
                default:
                    AssertMsgFailed(("%d\n", enmState));
                    return VERR_INVALID_STATE;

                case RTUDPSERVERSTATE_STOPPING:
                case RTUDPSERVERSTATE_STOPPED:
                    return VINF_SUCCESS;

                case RTUDPSERVERSTATE_DESTROYING:
                    return VERR_UDP_SERVER_DESTROYED;
            }
        }
        if (rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_STOPPING, enmState))
        {
            rtUdpServerDestroySocket(&pServer->hSocket, "RTUdpServerShutdown");
            rtUdpServerSetState(pServer, RTUDPSERVERSTATE_STOPPED, RTUDPSERVERSTATE_STOPPING);

            RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
            return VINF_SUCCESS;
        }
    }
}


RTR3DECL(int) RTUdpServerDestroy(PRTUDPSERVER pServer)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTUDPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE); /* paranoia */

    /*
     * Move the state along so the listener can figure out what's going on.
     */
    for (;;)
    {
        bool             fDestroyable;
        RTUDPSERVERSTATE enmState = pServer->enmState;
        switch (enmState)
        {
            case RTUDPSERVERSTATE_STARTING:
            case RTUDPSERVERSTATE_WAITING:
            case RTUDPSERVERSTATE_RECEIVING:
            case RTUDPSERVERSTATE_CREATED:
            case RTUDPSERVERSTATE_STOPPED:
                fDestroyable = rtUdpServerTrySetState(pServer, RTUDPSERVERSTATE_DESTROYING, enmState);
                break;

            /* destroyable states */
            case RTUDPSERVERSTATE_STOPPING:
                fDestroyable = true;
                break;

            /*
             * Everything else means user or internal misbehavior.
             */
            default:
                AssertMsgFailed(("pServer=%p enmState=%d\n", pServer, enmState));
                RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
                return VERR_INTERNAL_ERROR;
        }
        if (fDestroyable)
            break;
    }

    /*
     * Destroy it.
     */
    ASMAtomicWriteU32(&pServer->u32Magic, ~RTUDPSERVER_MAGIC);
    rtUdpServerDestroySocket(&pServer->hSocket, "Destroyer: server");

    /*
     * Release it.
     */
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    return VINF_SUCCESS;
}


/**
 * Internal close function which does all the proper bitching.
 */
static int rtUdpClose(RTSOCKET Sock, const char *pszMsg)
{
    NOREF(pszMsg); /** @todo drop this parameter? */

    /* ignore nil handles. */
    if (Sock == NIL_RTSOCKET)
        return VINF_SUCCESS;

    /*
     * Close the socket handle (drops our reference to it).
     */
    return RTSocketClose(Sock);
}


RTR3DECL(int) RTUdpRead(RTSOCKET Sock, void *pvBuffer, size_t cbBuffer, size_t *pcbRead, PRTNETADDR pSrcAddr)
{
    if (!RT_VALID_PTR(pcbRead))
        return VERR_INVALID_POINTER;
    return RTSocketReadFrom(Sock, pvBuffer, cbBuffer, pcbRead, pSrcAddr);
}


RTR3DECL(int)  RTUdpWrite(PRTUDPSERVER pServer, const void *pvBuffer, size_t cbBuffer, PCRTNETADDR pDstAddr)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTUDPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE);

    RTSOCKET hSocket;
    ASMAtomicReadHandle(&pServer->hSocket, &hSocket);
    if (hSocket == NIL_RTSOCKET)
    {
        RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
        return VERR_INVALID_HANDLE;
    }
    RTSocketRetain(hSocket);

    int rc = VINF_SUCCESS;
    RTUDPSERVERSTATE enmState = pServer->enmState;
    if (    enmState != RTUDPSERVERSTATE_CREATED
        &&  enmState != RTUDPSERVERSTATE_STARTING
        &&  enmState != RTUDPSERVERSTATE_WAITING
        &&  enmState != RTUDPSERVERSTATE_RECEIVING
        &&  enmState != RTUDPSERVERSTATE_STOPPING)
        rc = VERR_INVALID_STATE;

    if (RT_SUCCESS(rc))
        rc = RTSocketWriteTo(hSocket, pvBuffer, cbBuffer, pDstAddr);

    RTSocketRelease(hSocket);
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);

    return rc;
}


RTR3DECL(int) RTUdpCreateClientSocket(const char *pszAddress, uint32_t uPort, PRTNETADDR pLocalAddr, PRTSOCKET pSock)
{
    /*
     * Validate input.
     */
    AssertReturn(uPort > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszAddress, VERR_INVALID_POINTER);
    AssertPtrReturn(pSock, VERR_INVALID_POINTER);

    /*
     * Resolve the address.
     */
    RTNETADDR Addr;
    int rc = RTSocketParseInetAddress(pszAddress, uPort, &Addr);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Create the socket and connect.
     */
    RTSOCKET Sock;
    rc = rtSocketCreate(&Sock, AF_INET, SOCK_DGRAM, 0, false /*fInheritable*/);
    if (RT_SUCCESS(rc))
    {
        if (pLocalAddr)
            rc = rtSocketBind(Sock, pLocalAddr);
        if (RT_SUCCESS(rc))
        {
            rc = rtSocketConnect(Sock, &Addr, RT_SOCKETCONNECT_DEFAULT_WAIT);
            if (RT_SUCCESS(rc))
            {
                *pSock = Sock;
                return VINF_SUCCESS;
            }
        }
        RTSocketClose(Sock);
    }
    return rc;
}


RTR3DECL(int) RTUdpCreateServerSocket(const char *pszAddress, uint32_t uPort, PRTSOCKET pSock)
{
    /*
     * Validate input.
     */
    AssertReturn(uPort > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszAddress, VERR_INVALID_POINTER);
    AssertPtrReturn(pSock, VERR_INVALID_POINTER);

    /*
     * Resolve the address.
     */
    RTNETADDR LocalAddr;
    int rc = RTSocketParseInetAddress(pszAddress, uPort, &LocalAddr);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Setting up socket.
     */
    RTSOCKET Sock;
    rc = rtSocketCreate(&Sock, AF_INET, SOCK_DGRAM, IPPROTO_UDP, false /*fInheritable*/);
    if (RT_SUCCESS(rc))
    {
        /*
         * Set socket options.
         */
        int fFlag = 1;
        if (!rtSocketSetOpt(Sock, SOL_SOCKET, SO_REUSEADDR, &fFlag, sizeof(fFlag)))
        {
            /*
             * Bind a name to the socket.
             */
            rc = rtSocketBind(Sock, &LocalAddr);
            if (RT_SUCCESS(rc))
            {
                *pSock = Sock;
                return VINF_SUCCESS;
            }
        }
        RTSocketClose(Sock);
    }
    return rc;
}
