/* $Id: tcp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - TCP/IP.
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
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <netdb.h>
# ifdef FIX_FOR_3_2
#  include <fcntl.h>
# endif
#endif
#include <limits.h>

#include "internal/iprt.h"
#include <iprt/tcp.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
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
/* non-standard linux stuff (it seems). */
#ifndef MSG_NOSIGNAL
# define MSG_NOSIGNAL           0
#endif
#ifndef SHUT_RDWR
# ifdef SD_BOTH
#  define SHUT_RDWR             SD_BOTH
# else
#  define SHUT_RDWR             2
# endif
#endif
#ifndef SHUT_WR
# ifdef SD_SEND
#  define SHUT_WR               SD_SEND
# else
#  define SHUT_WR               1
# endif
#endif

/* fixup backlevel OSes. */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define socklen_t              int
#endif

/** How many pending connection. */
#define RTTCP_SERVER_BACKLOG    10


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * TCP Server state.
 */
typedef enum RTTCPSERVERSTATE
{
    /** Invalid. */
    RTTCPSERVERSTATE_INVALID = 0,
    /** Created. */
    RTTCPSERVERSTATE_CREATED,
    /** Listener thread is starting up. */
    RTTCPSERVERSTATE_STARTING,
    /** Accepting client connections. */
    RTTCPSERVERSTATE_ACCEPTING,
    /** Serving a client. */
    RTTCPSERVERSTATE_SERVING,
    /** Listener terminating. */
    RTTCPSERVERSTATE_STOPPING,
    /** Listener terminated. */
    RTTCPSERVERSTATE_STOPPED,
    /** Listener cleans up. */
    RTTCPSERVERSTATE_DESTROYING
} RTTCPSERVERSTATE;

/*
 * Internal representation of the TCP Server handle.
 */
typedef struct RTTCPSERVER
{
    /** The magic value (RTTCPSERVER_MAGIC). */
    uint32_t volatile           u32Magic;
    /** The server state. */
    RTTCPSERVERSTATE volatile   enmState;
    /** The server thread. */
    RTTHREAD                    Thread;
    /** The server socket. */
    RTSOCKET volatile           hServerSocket;
    /** The socket to the client currently being serviced.
     * This is NIL_RTSOCKET when no client is serviced. */
    RTSOCKET volatile           hClientSocket;
    /** The connection function. */
    PFNRTTCPSERVE               pfnServe;
    /** Argument to pfnServer. */
    void                       *pvUser;
} RTTCPSERVER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  rtTcpServerThread(RTTHREAD ThreadSelf, void *pvServer);
static int  rtTcpServerListen(PRTTCPSERVER pServer);
static int  rtTcpServerListenCleanup(PRTTCPSERVER pServer);
static int  rtTcpClose(RTSOCKET Sock, const char *pszMsg, bool fTryGracefulShutdown);


/**
 * Atomicly updates a socket variable.
 * @returns The old handle value.
 * @param   phSock          The socket handle variable to update.
 * @param   hNew            The new socket handle value.
 */
DECLINLINE(RTSOCKET) rtTcpAtomicXchgSock(RTSOCKET volatile *phSock, const RTSOCKET hNew)
{
    RTSOCKET hRet;
    ASMAtomicXchgHandle(phSock, hNew, &hRet);
    return hRet;
}


/**
 * Tries to change the TCP server state.
 */
DECLINLINE(bool) rtTcpServerTrySetState(PRTTCPSERVER pServer, RTTCPSERVERSTATE enmStateNew, RTTCPSERVERSTATE enmStateOld)
{
    bool fRc;
    ASMAtomicCmpXchgSize(&pServer->enmState, enmStateNew, enmStateOld, fRc);
    return fRc;
}

/**
 * Changes the TCP server state.
 */
DECLINLINE(void) rtTcpServerSetState(PRTTCPSERVER pServer, RTTCPSERVERSTATE enmStateNew, RTTCPSERVERSTATE enmStateOld)
{
    bool fRc;
    ASMAtomicCmpXchgSize(&pServer->enmState, enmStateNew, enmStateOld, fRc);
    Assert(fRc); NOREF(fRc);
}


/**
 * Closes the a socket (client or server).
 *
 * @returns IPRT status code.
 */
static int rtTcpServerDestroySocket(RTSOCKET volatile *pSock, const char *pszMsg, bool fTryGracefulShutdown)
{
    RTSOCKET hSocket = rtTcpAtomicXchgSock(pSock, NIL_RTSOCKET);
    if (hSocket != NIL_RTSOCKET)
    {
        if (!fTryGracefulShutdown)
            RTSocketShutdown(hSocket, true /*fRead*/, true /*fWrite*/);
        return rtTcpClose(hSocket, pszMsg, fTryGracefulShutdown);
    }
    return VINF_TCP_SERVER_NO_CLIENT;
}


RTR3DECL(int)  RTTcpServerCreate(const char *pszAddress, unsigned uPort, RTTHREADTYPE enmType, const char *pszThrdName,
                                 PFNRTTCPSERVE pfnServe, void *pvUser, PPRTTCPSERVER ppServer)
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
    PRTTCPSERVER pServer;
    int rc = RTTcpServerCreateEx(pszAddress, uPort, &pServer);
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the listener thread.
         */
        RTMemPoolRetain(pServer);
        pServer->enmState   = RTTCPSERVERSTATE_STARTING;
        pServer->pvUser     = pvUser;
        pServer->pfnServe   = pfnServe;
        rc = RTThreadCreate(&pServer->Thread, rtTcpServerThread, pServer, 0, enmType, /*RTTHREADFLAGS_WAITABLE*/0, pszThrdName);
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
        rtTcpServerSetState(pServer, RTTCPSERVERSTATE_CREATED, RTTCPSERVERSTATE_STARTING);
        RTTcpServerDestroy(pServer);
    }

    return rc;
}


/**
 * Server thread, loops accepting connections until it's terminated.
 *
 * @returns iprt status code. (ignored).
 * @param   ThreadSelf      Thread handle.
 * @param   pvServer        Server handle.
 */
static DECLCALLBACK(int)  rtTcpServerThread(RTTHREAD ThreadSelf, void *pvServer)
{
    PRTTCPSERVER    pServer = (PRTTCPSERVER)pvServer;
    int             rc;
    if (rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_ACCEPTING, RTTCPSERVERSTATE_STARTING))
        rc = rtTcpServerListen(pServer);
    else
        rc = rtTcpServerListenCleanup(pServer);
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    NOREF(ThreadSelf);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTTcpServerCreateEx(const char *pszAddress, uint32_t uPort, PPRTTCPSERVER ppServer)
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
    RTSOCKET WaitSock;
    rc = rtSocketCreate(&WaitSock, AF_INET, SOCK_STREAM, IPPROTO_TCP, false /*fInheritable*/);
    if (RT_SUCCESS(rc))
    {
        /*
         * Set socket options.
         */
        int fFlag = 1;
        if (!rtSocketSetOpt(WaitSock, SOL_SOCKET, SO_REUSEADDR, &fFlag, sizeof(fFlag)))
        {
            /*
             * Bind a name to a socket and set it listening for connections.
             */
            rc = rtSocketBind(WaitSock, &LocalAddr);
            if (RT_SUCCESS(rc))
                rc = rtSocketListen(WaitSock, RTTCP_SERVER_BACKLOG);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create the server handle.
                 */
                PRTTCPSERVER pServer = (PRTTCPSERVER)RTMemPoolAlloc(RTMEMPOOL_DEFAULT, sizeof(*pServer));
                if (pServer)
                {
                    pServer->u32Magic       = RTTCPSERVER_MAGIC;
                    pServer->enmState       = RTTCPSERVERSTATE_CREATED;
                    pServer->Thread         = NIL_RTTHREAD;
                    pServer->hServerSocket  = WaitSock;
                    pServer->hClientSocket  = NIL_RTSOCKET;
                    pServer->pfnServe       = NULL;
                    pServer->pvUser         = NULL;
                    *ppServer = pServer;
                    return VINF_SUCCESS;
                }

                /* bail out */
                rc = VERR_NO_MEMORY;
            }
        }
        else
            AssertMsgFailed(("rtSocketSetOpt: %Rrc\n", rc));
        rtTcpClose(WaitSock, "RTServerCreateEx", false /*fTryGracefulShutdown*/);
    }

    return rc;
}


RTR3DECL(int) RTTcpServerListen(PRTTCPSERVER pServer, PFNRTTCPSERVE pfnServe, void *pvUser)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pfnServe, VERR_INVALID_POINTER);
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTTCPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE);

    int rc = VERR_INVALID_STATE;
    if (rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_ACCEPTING, RTTCPSERVERSTATE_CREATED))
    {
        Assert(!pServer->pfnServe);
        Assert(!pServer->pvUser);
        Assert(pServer->Thread == NIL_RTTHREAD);
        Assert(pServer->hClientSocket == NIL_RTSOCKET);

        pServer->pfnServe = pfnServe;
        pServer->pvUser   = pvUser;
        pServer->Thread   = RTThreadSelf();
        Assert(pServer->Thread != NIL_RTTHREAD);
        rc = rtTcpServerListen(pServer);
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
 * Internal worker common for RTTcpServerListen and the thread created by
 * RTTcpServerCreate().
 *
 * The caller makes sure it has its own memory reference and releases it upon
 * return.
 */
static int rtTcpServerListen(PRTTCPSERVER pServer)
{
    /*
     * Accept connection loop.
     */
    for (;;)
    {
        /*
         * Change state, getting an extra reference to the socket so we can
         * allow others to close it while we're stuck in rtSocketAccept.
         */
        RTTCPSERVERSTATE    enmState      = pServer->enmState;
        RTSOCKET            hServerSocket;
        ASMAtomicXchgHandle(&pServer->hServerSocket, NIL_RTSOCKET, &hServerSocket);
        if (hServerSocket != NIL_RTSOCKET)
        {
            RTSocketRetain(hServerSocket);
            ASMAtomicWriteHandle(&pServer->hServerSocket, hServerSocket);
        }
        if (    enmState != RTTCPSERVERSTATE_ACCEPTING
            &&  enmState != RTTCPSERVERSTATE_SERVING)
        {
            RTSocketRelease(hServerSocket);
            return rtTcpServerListenCleanup(pServer);
        }
        if (!rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_ACCEPTING, enmState))
        {
            RTSocketRelease(hServerSocket);
            continue;
        }

        /*
         * Accept connection.
         */
        struct sockaddr_in  RemoteAddr;
        size_t              cbRemoteAddr = sizeof(RemoteAddr);
        RTSOCKET            hClientSocket;
        RT_ZERO(RemoteAddr);
        int rc = rtSocketAccept(hServerSocket, &hClientSocket, (struct sockaddr *)&RemoteAddr, &cbRemoteAddr);
        RTSocketRelease(hServerSocket);
        if (RT_FAILURE(rc))
        {
            /* These are typical for what can happen during destruction. */
            if (   rc == VERR_INVALID_HANDLE
                || rc == VERR_INVALID_PARAMETER
                || rc == VERR_NET_NOT_SOCKET)
                return rtTcpServerListenCleanup(pServer);
            continue;
        }
        RTSocketSetInheritance(hClientSocket, false /*fInheritable*/);

        /*
         * Run a pfnServe callback.
         */
        if (!rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_SERVING, RTTCPSERVERSTATE_ACCEPTING))
        {
            rtTcpClose(hClientSocket, "rtTcpServerListen", true /*fTryGracefulShutdown*/);
            return rtTcpServerListenCleanup(pServer);
        }
        RTSocketRetain(hClientSocket);
        rtTcpAtomicXchgSock(&pServer->hClientSocket, hClientSocket);
        rc = pServer->pfnServe(hClientSocket, pServer->pvUser);
        rtTcpServerDestroySocket(&pServer->hClientSocket, "Listener: client (secondary)", true /*fTryGracefulShutdown*/);
        RTSocketRelease(hClientSocket);

        /*
         * Stop the server?
         */
        if (rc == VERR_TCP_SERVER_STOP)
        {
            if (rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_STOPPING, RTTCPSERVERSTATE_SERVING))
            {
                /*
                 * Reset the server socket and change the state to stopped. After that state change
                 * we cannot safely access the handle so we'll have to return here.
                 */
                hServerSocket = rtTcpAtomicXchgSock(&pServer->hServerSocket, NIL_RTSOCKET);
                rtTcpServerSetState(pServer, RTTCPSERVERSTATE_STOPPED, RTTCPSERVERSTATE_STOPPING);
                rtTcpClose(hServerSocket, "Listener: server stopped", false /*fTryGracefulShutdown*/);
            }
            else
                rtTcpServerListenCleanup(pServer); /* ignore rc */
            return rc;
        }
    }
}


/**
 * Clean up after listener.
 */
static int rtTcpServerListenCleanup(PRTTCPSERVER pServer)
{
    /*
     * Close the server socket, the client one shouldn't be set.
     */
    rtTcpServerDestroySocket(&pServer->hServerSocket, "ListenCleanup", false /*fTryGracefulShutdown*/);
    Assert(pServer->hClientSocket == NIL_RTSOCKET);

    /*
     * Figure the return code and make sure the state is OK.
     */
    RTTCPSERVERSTATE enmState = pServer->enmState;
    switch (enmState)
    {
        case RTTCPSERVERSTATE_STOPPING:
        case RTTCPSERVERSTATE_STOPPED:
            return VERR_TCP_SERVER_SHUTDOWN;

        case RTTCPSERVERSTATE_ACCEPTING:
            rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_STOPPED, enmState);
            return VERR_TCP_SERVER_DESTROYED;

        case RTTCPSERVERSTATE_DESTROYING:
            return VERR_TCP_SERVER_DESTROYED;

        case RTTCPSERVERSTATE_STARTING:
        case RTTCPSERVERSTATE_SERVING:
        default:
            AssertMsgFailedReturn(("pServer=%p enmState=%d\n", pServer, enmState), VERR_INTERNAL_ERROR_4);
    }
}


RTR3DECL(int) RTTcpServerListen2(PRTTCPSERVER pServer, PRTSOCKET phClientSocket)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(phClientSocket, VERR_INVALID_HANDLE);
    *phClientSocket = NIL_RTSOCKET;
    AssertReturn(pServer->u32Magic == RTTCPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE);

    int rc = VERR_INVALID_STATE;
    for (;;)
    {
        /*
         * Change state, getting an extra reference to the socket so we can
         * allow others to close it while we're stuck in rtSocketAccept.
         */
        RTTCPSERVERSTATE    enmState      = pServer->enmState;
        RTSOCKET            hServerSocket;
        ASMAtomicXchgHandle(&pServer->hServerSocket, NIL_RTSOCKET, &hServerSocket);
        if (hServerSocket != NIL_RTSOCKET)
        {
            RTSocketRetain(hServerSocket);
            ASMAtomicWriteHandle(&pServer->hServerSocket, hServerSocket);
        }
        if (    enmState != RTTCPSERVERSTATE_SERVING
            &&  enmState != RTTCPSERVERSTATE_CREATED)
        {
            RTSocketRelease(hServerSocket);
            return rtTcpServerListenCleanup(pServer);
        }
        if (!rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_ACCEPTING, enmState))
        {
            RTSocketRelease(hServerSocket);
            continue;
        }
        Assert(!pServer->pfnServe);
        Assert(!pServer->pvUser);
        Assert(pServer->Thread == NIL_RTTHREAD);
        Assert(pServer->hClientSocket == NIL_RTSOCKET);

        /*
         * Accept connection.
         */
        struct sockaddr_in  RemoteAddr;
        size_t              cbRemoteAddr = sizeof(RemoteAddr);
        RTSOCKET            hClientSocket;
        RT_ZERO(RemoteAddr);
        rc = rtSocketAccept(hServerSocket, &hClientSocket, (struct sockaddr *)&RemoteAddr, &cbRemoteAddr);
        RTSocketRelease(hServerSocket);
        if (RT_FAILURE(rc))
        {
            if (!rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_CREATED, RTTCPSERVERSTATE_ACCEPTING))
                rc = rtTcpServerListenCleanup(pServer);
            if (RT_FAILURE(rc))
                break;
            continue;
        }
        RTSocketSetInheritance(hClientSocket, false /*fInheritable*/);

        /*
         * Chance to the 'serving' state and return the socket.
         */
        if (rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_SERVING, RTTCPSERVERSTATE_ACCEPTING))
        {
            *phClientSocket = hClientSocket;
            rc = VINF_SUCCESS;
        }
        else
        {
            rtTcpClose(hClientSocket, "RTTcpServerListen2", true /*fTryGracefulShutdown*/);
            rc = rtTcpServerListenCleanup(pServer);
        }
        break;
    }

    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    return rc;
}


RTR3DECL(int) RTTcpServerDisconnectClient(PRTTCPSERVER pServer)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTTCPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE);

    int rc = rtTcpServerDestroySocket(&pServer->hClientSocket, "DisconnectClient: client", true /*fTryGracefulShutdown*/);

    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    return rc;
}


RTR3DECL(int) RTTcpServerDisconnectClient2(RTSOCKET hClientSocket)
{
    return rtTcpClose(hClientSocket, "RTTcpServerDisconnectClient2", true /*fTryGracefulShutdown*/);
}


RTR3DECL(int) RTTcpServerShutdown(PRTTCPSERVER pServer)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTTCPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Try change the state to stopping, then replace and destroy the server socket.
     */
    for (;;)
    {
        RTTCPSERVERSTATE enmState = pServer->enmState;
        if (    enmState != RTTCPSERVERSTATE_ACCEPTING
            &&  enmState != RTTCPSERVERSTATE_SERVING)
        {
            RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
            switch (enmState)
            {
                case RTTCPSERVERSTATE_CREATED:
                case RTTCPSERVERSTATE_STARTING:
                default:
                    AssertMsgFailed(("%d\n", enmState));
                    return VERR_INVALID_STATE;

                case RTTCPSERVERSTATE_STOPPING:
                case RTTCPSERVERSTATE_STOPPED:
                    return VINF_SUCCESS;

                case RTTCPSERVERSTATE_DESTROYING:
                    return VERR_TCP_SERVER_DESTROYED;
            }
        }
        if (rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_STOPPING, enmState))
        {
            rtTcpServerDestroySocket(&pServer->hServerSocket, "RTTcpServerShutdown", false /*fTryGracefulShutdown*/);
            rtTcpServerSetState(pServer, RTTCPSERVERSTATE_STOPPED, RTTCPSERVERSTATE_STOPPING);

            RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
            return VINF_SUCCESS;
        }
    }
}


RTR3DECL(int) RTTcpServerDestroy(PRTTCPSERVER pServer)
{
    /*
     * Validate input and retain the instance.
     */
    AssertPtrReturn(pServer, VERR_INVALID_HANDLE);
    AssertReturn(pServer->u32Magic == RTTCPSERVER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRetain(pServer) != UINT32_MAX, VERR_INVALID_HANDLE); /* paranoia */

    /*
     * Move the state along so the listener can figure out what's going on.
     */
    for (;;)
    {
        bool             fDestroyable;
        RTTCPSERVERSTATE enmState = pServer->enmState;
        switch (enmState)
        {
            case RTTCPSERVERSTATE_STARTING:
            case RTTCPSERVERSTATE_ACCEPTING:
            case RTTCPSERVERSTATE_SERVING:
            case RTTCPSERVERSTATE_CREATED:
            case RTTCPSERVERSTATE_STOPPED:
                fDestroyable = rtTcpServerTrySetState(pServer, RTTCPSERVERSTATE_DESTROYING, enmState);
                break;

            /* destroyable states */
            case RTTCPSERVERSTATE_STOPPING:
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
    ASMAtomicWriteU32(&pServer->u32Magic, ~RTTCPSERVER_MAGIC);
    rtTcpServerDestroySocket(&pServer->hServerSocket, "Destroyer: server", false /*fTryGracefulShutdown*/);
    rtTcpServerDestroySocket(&pServer->hClientSocket, "Destroyer: client", true  /*fTryGracefulShutdown*/);

    /*
     * Release it.
     */
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pServer);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTTcpClientConnect(const char *pszAddress, uint32_t uPort, PRTSOCKET pSock)
{
    return RTTcpClientConnectEx(pszAddress, uPort, pSock, RT_SOCKETCONNECT_DEFAULT_WAIT, NULL);
}


RTR3DECL(int) RTTcpClientConnectEx(const char *pszAddress, uint32_t uPort, PRTSOCKET pSock,
                                   RTMSINTERVAL cMillies, PRTTCPCLIENTCONNECTCANCEL volatile *ppCancelCookie)
{
    /*
     * Validate input.
     */
    AssertReturn(uPort > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszAddress, VERR_INVALID_POINTER);
    AssertPtrNullReturn(ppCancelCookie, VERR_INVALID_POINTER);

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
    rc = rtSocketCreate(&Sock, PF_INET, SOCK_STREAM, 0, false /*fInheritable*/);
    if (RT_SUCCESS(rc))
    {
        if (!ppCancelCookie)
            rc = rtSocketConnect(Sock, &Addr, cMillies);
        else
        {
            RTSocketRetain(Sock);
            if (ASMAtomicCmpXchgPtr(ppCancelCookie, (PRTTCPCLIENTCONNECTCANCEL)Sock, NULL))
            {
                rc = rtSocketConnect(Sock, &Addr, cMillies);
                if (ASMAtomicCmpXchgPtr(ppCancelCookie, NULL, (PRTTCPCLIENTCONNECTCANCEL)Sock))
                    RTSocketRelease(Sock);
                else
                    rc = VERR_CANCELLED;
            }
            else
            {
                RTSocketRelease(Sock);
                rc = VERR_CANCELLED;
            }
        }
        if (RT_SUCCESS(rc))
        {
            *pSock = Sock;
            return VINF_SUCCESS;
        }

        rtTcpClose(Sock, "RTTcpClientConnect", false /*fTryGracefulShutdown*/);
    }
    if (ppCancelCookie)
        *ppCancelCookie = NULL;
    return rc;
}


RTR3DECL(int) RTTcpClientCancelConnect(PRTTCPCLIENTCONNECTCANCEL volatile *ppCancelCookie)
{
    AssertPtrReturn(ppCancelCookie, VERR_INVALID_POINTER);

    RTSOCKET const hSockCancelled = (RTSOCKET)(uintptr_t)0xdead9999;

    AssertCompile(NIL_RTSOCKET == NULL);
    RTSOCKET hSock = (RTSOCKET)ASMAtomicXchgPtr((void * volatile *)ppCancelCookie, hSockCancelled);
    if (hSock != NIL_RTSOCKET && hSock != hSockCancelled)
    {
        int rc = rtTcpClose(hSock, "RTTcpClientCancelConnect", false /*fTryGracefulShutdown*/);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


RTR3DECL(int) RTTcpClientClose(RTSOCKET Sock)
{
    return rtTcpClose(Sock, "RTTcpClientClose", true /*fTryGracefulShutdown*/);
}


RTR3DECL(int) RTTcpClientCloseEx(RTSOCKET Sock, bool fGracefulShutdown)
{
    return rtTcpClose(Sock, "RTTcpClientCloseEx", fGracefulShutdown);
}


#ifdef FIX_FOR_3_2
/**
 * Changes the blocking mode of the socket.
 *
 * @returns 0 on success, -1 on failure.
 * @param   hSocket             The socket to work on.
 * @param   fBlocking           The desired mode of operation.
 */
static int rtTcpSetBlockingMode(RTHCUINTPTR hSocket, bool fBlocking)
{
    int     rc        = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    u_long  uBlocking = fBlocking ? 0 : 1;
    if (ioctlsocket(hSocket, FIONBIO, &uBlocking))
        return -1;

#else
    int     fFlags    = fcntl(hSocket, F_GETFL, 0);
    if (fFlags == -1)
        return -1;

    if (fBlocking)
        fFlags &= ~O_NONBLOCK;
    else
        fFlags |= O_NONBLOCK;
    if (fcntl(hSocket, F_SETFL, fFlags) == -1)
       return -1;
#endif

    return 0;
}
#endif


/**
 * Internal close function which does all the proper bitching.
 */
static int rtTcpClose(RTSOCKET Sock, const char *pszMsg, bool fTryGracefulShutdown)
{
    NOREF(pszMsg); /** @todo drop this parameter? */

    /* ignore nil handles. */
    if (Sock == NIL_RTSOCKET)
        return VINF_SUCCESS;

    /*
     * Try to gracefully shut it down.
     */
    int rc;
    if (fTryGracefulShutdown)
    {
        rc = RTSocketShutdown(Sock, false /*fRead*/, true /*fWrite*/);
#ifdef FIX_FOR_3_2
        RTHCUINTPTR hNative = RTSocketToNative(Sock);
        if (RT_SUCCESS(rc) && rtTcpSetBlockingMode(hNative, false /*fBlocking*/) == 0)
#else
        if (RT_SUCCESS(rc))
#endif
        {

            size_t      cbReceived = 0;
            uint64_t    u64Start   = RTTimeMilliTS();
            while (   cbReceived < _1G
                   && RTTimeMilliTS() - u64Start < 30000)
            {
#ifdef FIX_FOR_3_2
                fd_set FdSetR;
                FD_ZERO(&FdSetR);
                FD_SET(hNative, &FdSetR);

                fd_set FdSetE;
                FD_ZERO(&FdSetE);
                FD_SET(hNative, &FdSetE);

                struct timeval TvTimeout;
                TvTimeout.tv_sec  = 1;
                TvTimeout.tv_usec = 0;
                rc = select(hNative + 1, &FdSetR, NULL, &FdSetE, &TvTimeout);
                if (rc == 0)
                    continue;
                if (rc < 0)
                    break;
                if (FD_ISSET(hNative, &FdSetE))
                    break;
#else
                uint32_t fEvents;
                rc = RTSocketSelectOneEx(Sock, RTSOCKET_EVT_READ | RTSOCKET_EVT_ERROR, &fEvents, 1000);
                if (rc == VERR_TIMEOUT)
                    continue;
                if (RT_FAILURE(rc))
                    break;
                if (fEvents & RTSOCKET_EVT_ERROR)
                    break;
#endif

                char abBitBucket[16*_1K];
#ifdef FIX_FOR_3_2
                ssize_t cbRead = recv(hNative, &abBitBucket[0], sizeof(abBitBucket), MSG_NOSIGNAL);
                if (cbRead == 0)
                    break; /* orderly shutdown in progress */
                if (cbRead < 0 && errno != EAGAIN)
                    break; /* some kind of error, never mind which... */
#else
                size_t cbRead;
                rc = RTSocketReadNB(Sock, &abBitBucket[0], sizeof(abBitBucket), &cbRead);
                if (RT_FAILURE(rc))
                    break; /* some kind of error, never mind which... */
                if (rc != VINF_TRY_AGAIN && !cbRead)
                    break; /* orderly shutdown in progress */
#endif

                cbReceived += cbRead;
            }
        }
    }

    /*
     * Close the socket handle (drops our reference to it).
     */
    return RTSocketClose(Sock);
}


RTR3DECL(int) RTTcpCreatePair(PRTSOCKET phServer, PRTSOCKET phClient, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phServer, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phClient, VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    /*
     * Do the job.
     */
    return rtSocketCreateTcpPair(phServer, phClient);
}


RTR3DECL(int) RTTcpRead(RTSOCKET Sock, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    return RTSocketRead(Sock, pvBuffer, cbBuffer, pcbRead);
}


RTR3DECL(int)  RTTcpWrite(RTSOCKET Sock, const void *pvBuffer, size_t cbBuffer)
{
    return RTSocketWrite(Sock, pvBuffer, cbBuffer);
}


RTR3DECL(int)  RTTcpFlush(RTSOCKET Sock)
{
    int fFlag = 1;
    int rc = rtSocketSetOpt(Sock, IPPROTO_TCP, TCP_NODELAY, &fFlag, sizeof(fFlag));
    if (RT_SUCCESS(rc))
    {
        fFlag = 0;
        rc = rtSocketSetOpt(Sock, IPPROTO_TCP, TCP_NODELAY, &fFlag, sizeof(fFlag));
    }
    return rc;
}


RTR3DECL(int)  RTTcpSetSendCoalescing(RTSOCKET Sock, bool fEnable)
{
    int fFlag = fEnable ? 0 : 1;
    return rtSocketSetOpt(Sock, IPPROTO_TCP, TCP_NODELAY, &fFlag, sizeof(fFlag));
}


RTR3DECL(int)  RTTcpSetBufferSize(RTSOCKET hSocket, uint32_t cbSize)
{
    int cbIntSize = (int)cbSize;
    AssertReturn(cbIntSize >= 0, VERR_OUT_OF_RANGE);
    int rc = rtSocketSetOpt(hSocket, SOL_SOCKET, SO_SNDBUF, &cbIntSize, sizeof(cbIntSize));
    if (RT_SUCCESS(rc))
        rc = rtSocketSetOpt(hSocket, SOL_SOCKET, SO_RCVBUF, &cbIntSize, sizeof(cbIntSize));
    return rc;
}


RTR3DECL(int)  RTTcpSetKeepAlive(RTSOCKET hSocket, bool fEnable, uint32_t cSecsIdle,
                                 uint32_t cSecsInterval, uint32_t cFailedPktsBeforeClose)
{
#if !defined(RT_OS_WINDOWS)
    int fFlag = fEnable ? 1 : 0;
    int rc = rtSocketSetOpt(hSocket, SOL_SOCKET, SO_KEEPALIVE, &fFlag, sizeof(fFlag));
    if (RT_FAILURE(rc))
        return rc;

# if (defined(TCP_KEEPIDLE) || defined(TCP_KEEPALIVE)) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
    rc = VINF_SUCCESS;

    /* time in seconds that the connection must be idle before sending keep-alive probes */
    if (cSecsIdle)
    {
#  if defined(TCP_KEEPALIVE) && !defined(TCP_KEEPIDLE)  /* macOS */
#   define TCP_KEEPIDLE    TCP_KEEPALIVE
#  endif
        rc = rtSocketSetOpt(hSocket, IPPROTO_TCP, TCP_KEEPIDLE, &cSecsIdle, sizeof(cSecsIdle));
        if (RT_FAILURE(rc))
            return rc;
    }

    /* time in seconds between each keep-alive probe */
    if (cSecsInterval)
    {
        rc = rtSocketSetOpt(hSocket, IPPROTO_TCP, TCP_KEEPINTVL, &cSecsInterval, sizeof(cSecsInterval));
        if (RT_FAILURE(rc))
            return rc;
    }

    /* count of keep-alive probes to send which don't receive a response before closing connection */
    if (cFailedPktsBeforeClose)
    {
        rc = rtSocketSetOpt(hSocket, IPPROTO_TCP, TCP_KEEPCNT, &cFailedPktsBeforeClose, sizeof(cFailedPktsBeforeClose));
        if (RT_FAILURE(rc))
            return rc;
    }

    return rc;
# else
    return VERR_NOT_SUPPORTED;
# endif
#else
    NOREF(cFailedPktsBeforeClose);
    return rtSocketSetKeepAlive(hSocket, fEnable, cSecsIdle, cSecsInterval);
#endif
}


RTR3DECL(int)  RTTcpSelectOne(RTSOCKET Sock, RTMSINTERVAL cMillies)
{
    return RTSocketSelectOne(Sock, cMillies);
}


RTR3DECL(int)  RTTcpSelectOneEx(RTSOCKET Sock, uint32_t fEvents, uint32_t *pfEvents,
                                RTMSINTERVAL cMillies)
{
    return RTSocketSelectOneEx(Sock, fEvents, pfEvents, cMillies);
}


RTR3DECL(int) RTTcpGetLocalAddress(RTSOCKET Sock, PRTNETADDR pAddr)
{
    return RTSocketGetLocalAddress(Sock, pAddr);
}


RTR3DECL(int) RTTcpGetPeerAddress(RTSOCKET Sock, PRTNETADDR pAddr)
{
    return RTSocketGetPeerAddress(Sock, pAddr);
}


RTR3DECL(int)  RTTcpSgWrite(RTSOCKET Sock, PCRTSGBUF pSgBuf)
{
    return RTSocketSgWrite(Sock, pSgBuf);
}


RTR3DECL(int) RTTcpSgWriteL(RTSOCKET hSocket, size_t cSegs, ...)
{
    va_list va;
    va_start(va, cSegs);
    int rc = RTSocketSgWriteLV(hSocket, cSegs, va);
    va_end(va);
    return rc;
}


RTR3DECL(int) RTTcpSgWriteLV(RTSOCKET hSocket, size_t cSegs, va_list va)
{
    return RTSocketSgWriteLV(hSocket, cSegs, va);
}


RTR3DECL(int) RTTcpReadNB(RTSOCKET Sock, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    return RTSocketReadNB(Sock, pvBuffer, cbBuffer, pcbRead);
}


RTR3DECL(int) RTTcpWriteNB(RTSOCKET Sock, const void *pvBuffer, size_t cbBuffer, size_t *pcbWritten)
{
    return RTSocketWriteNB(Sock, pvBuffer, cbBuffer, pcbWritten);
}


RTR3DECL(int)  RTTcpSgWriteNB(RTSOCKET Sock, PCRTSGBUF pSgBuf, size_t *pcbWritten)
{
    return RTSocketSgWriteNB(Sock, pSgBuf, pcbWritten);
}


RTR3DECL(int) RTTcpSgWriteLNB(RTSOCKET hSocket, size_t cSegs, size_t *pcbWritten, ...)
{
    va_list va;
    va_start(va, pcbWritten);
    int rc = RTSocketSgWriteLVNB(hSocket, cSegs, pcbWritten, va);
    va_end(va);
    return rc;
}


RTR3DECL(int) RTTcpSgWriteLVNB(RTSOCKET hSocket, size_t cSegs, size_t *pcbWritten, va_list va)
{
    return RTSocketSgWriteLVNB(hSocket, cSegs, pcbWritten, va);
}

