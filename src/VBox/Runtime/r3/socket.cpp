/* $Id: socket.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Network Sockets.
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
# include <iprt/win/ws2tcpip.h>
# include <mstcpip.h>  /* for struct tcp_keepalive */
#else /* !RT_OS_WINDOWS */
# include <errno.h>
# include <sys/select.h>
# include <sys/stat.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# ifdef IPRT_WITH_TCPIP_V6
#  include <netinet6/in6.h>
# endif
# include <sys/un.h>
# include <netdb.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/uio.h>
# ifndef AF_LOCAL
#   define AF_LOCAL AF_UNIX
# endif
#endif /* !RT_OS_WINDOWS */
#include <limits.h>

#include "internal/iprt.h"
#include <iprt/socket.h>

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mempool.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/mem.h>
#include <iprt/sg.h>
#include <iprt/log.h>

#include "internal/magics.h"
#include "internal/socket.h"
#include "internal/string.h"
#ifdef RT_OS_WINDOWS
# include "win/internal-r3-win.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* non-standard linux stuff (it seems). */
#ifndef MSG_NOSIGNAL
# define MSG_NOSIGNAL           0
#endif

/* Windows has different names for SHUT_XXX. */
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
#ifndef SHUT_RD
# ifdef SD_RECEIVE
#  define SHUT_RD               SD_RECEIVE
# else
#  define SHUT_RD               0
# endif
#endif

/* fixup backlevel OSes. */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define socklen_t              int
#endif

/** How many pending connection. */
#define RTTCP_SERVER_BACKLOG    10

/* Limit read and write sizes on Windows and OS/2. */
#ifdef RT_OS_WINDOWS
# define RTSOCKET_MAX_WRITE     (INT_MAX / 2)
# define RTSOCKET_MAX_READ      (INT_MAX / 2)
#elif defined(RT_OS_OS2)
# define RTSOCKET_MAX_WRITE     0x10000
# define RTSOCKET_MAX_READ      0x10000
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Socket handle data.
 *
 * This is mainly required for implementing RTPollSet on Windows.
 */
typedef struct RTSOCKETINT
{
    /** Magic number (RTSOCKET_MAGIC). */
    uint32_t            u32Magic;
    /** Exclusive user count.
     * This is used to prevent two threads from accessing the handle concurrently.
     * It can be higher than 1 if this handle is reference multiple times in a
     * polling set (Windows). */
    uint32_t volatile   cUsers;
    /** The native socket handle. */
    RTSOCKETNATIVE      hNative;
    /** Indicates whether the handle has been closed or not. */
    bool volatile       fClosed;
    /** Indicates whether the socket is operating in blocking or non-blocking mode
     * currently. */
    bool                fBlocking;
    /** Whether to leave the native socket open rather than closing it (for
     * RTHandleGetStandard). */
    bool                fLeaveOpen;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    /** The pollset currently polling this socket.  This is NIL if no one is
     * polling. */
    RTPOLLSET           hPollSet;
#endif
#ifdef RT_OS_WINDOWS
    /** The event semaphore we've associated with the socket handle.
     * This is WSA_INVALID_EVENT if not done. */
    WSAEVENT            hEvent;
    /** The events we're polling for. */
    uint32_t            fPollEvts;
    /** The events we're currently subscribing to with WSAEventSelect.
     * This is ZERO if we're currently not subscribing to anything. */
    uint32_t            fSubscribedEvts;
    /** Saved events which are only posted once and events harvested for
     * sockets entered multiple times into to a poll set.   Imagine a scenario where
     * you have a RTPOLL_EVT_READ entry and RTPOLL_EVT_ERROR entry.  The READ
     * condition can be triggered between checking the READ entry and the ERROR
     * entry, and we don't want to drop the READ, so we store it here and make sure
     * the event is signalled.
     *
     * The RTPOLL_EVT_ERROR is inconsistenly sticky at the momemnt... */
    uint32_t            fEventsSaved;
    /** Set if fEventsSaved contains harvested events (used to avoid multiple
     *  calls to rtSocketPollCheck on the same socket during rtSocketPollDone). */
    bool                fHarvestedEvents;
    /** Set if we're using the polling fallback. */
    bool                fPollFallback;
    /** Set if the fallback polling is active (event not set). */
    bool volatile       fPollFallbackActive;
    /** Set to shut down the fallback polling thread. */
    bool volatile       fPollFallbackShutdown;
    /** Socket use to wake up the select thread. */
    RTSOCKETNATIVE      hPollFallbackNotifyW;
    /** Socket the select thread always waits on. */
    RTSOCKETNATIVE      hPollFallbackNotifyR;
    /** The fallback polling thread. */
    RTTHREAD            hPollFallbackThread;
#endif /* RT_OS_WINDOWS */
} RTSOCKETINT;


/**
 * Address union used internally for things like getpeername and getsockname.
 */
typedef union RTSOCKADDRUNION
{
    struct sockaddr     Addr;
    struct sockaddr_in  IPv4;
#ifdef IPRT_WITH_TCPIP_V6
    struct sockaddr_in6 IPv6;
#endif
} RTSOCKADDRUNION;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
/** Indicates that we've successfully initialized winsock.  */
static uint32_t volatile g_uWinSockInitedVersion = 0;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
static void rtSocketPokePollFallbackThread(RTSOCKETINT *pThis);
#endif



#ifdef RT_OS_WINDOWS
/**
 * Initializes winsock for the process.
 *
 * @returns IPRT status code.
 */
static int rtSocketInitWinsock(void)
{
    if (g_uWinSockInitedVersion != 0)
        return VINF_SUCCESS;

    if (   !g_pfnWSAGetLastError
        || !g_pfnWSAStartup
        || !g_pfnsocket
        || !g_pfnclosesocket)
        return VERR_NET_INIT_FAILED;

    /*
     * Initialize winsock. Try with 2.2 and back down till we get something that works.
     */
    static const WORD s_awVersions[] =
    {
        MAKEWORD(2, 2),
        MAKEWORD(2, 1),
        MAKEWORD(2, 0),
        MAKEWORD(1, 1),
        MAKEWORD(1, 0),
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_awVersions); i++)
    {
        WSADATA     wsaData;
        RT_ZERO(wsaData);
        int rcWsa = g_pfnWSAStartup(s_awVersions[i], &wsaData);
        if (rcWsa == 0)
        {
            /* AssertMsg(wsaData.wVersion >= s_awVersions[i]); - triggers with winsock 1.1 */
            ASMAtomicWriteU32(&g_uWinSockInitedVersion, wsaData.wVersion);
            return VINF_SUCCESS;
        }
        AssertLogRelMsg(rcWsa == WSAVERNOTSUPPORTED, ("rcWsa=%d (winsock version %#x)\n", rcWsa, s_awVersions[i]));
    }
    LogRel(("Failed to init winsock!\n"));
    return VERR_NET_INIT_FAILED;
}
#endif


/**
 * Get the last error as an iprt status code.
 *
 * @returns IPRT status code.
 */
DECLINLINE(int) rtSocketError(void)
{
#ifdef RT_OS_WINDOWS
    if (g_pfnWSAGetLastError)
        return RTErrConvertFromWin32(g_pfnWSAGetLastError());
    return VERR_NET_IO_ERROR;
#else
    return RTErrConvertFromErrno(errno);
#endif
}


/**
 * Resets the last error.
 */
DECLINLINE(void) rtSocketErrorReset(void)
{
#ifdef RT_OS_WINDOWS
    if (g_pfnWSASetLastError)
        g_pfnWSASetLastError(0);
#else
    errno = 0;
#endif
}


/**
 * Get the last resolver error as an iprt status code.
 *
 * @returns iprt status code.
 */
DECLHIDDEN(int) rtSocketResolverError(void)
{
#ifdef RT_OS_WINDOWS
    if (g_pfnWSAGetLastError)
        return RTErrConvertFromWin32(g_pfnWSAGetLastError());
    return VERR_UNRESOLVED_ERROR;
#else
    switch (h_errno)
    {
        case HOST_NOT_FOUND:
            return VERR_NET_HOST_NOT_FOUND;
        case NO_DATA:
            return VERR_NET_ADDRESS_NOT_AVAILABLE;
        case NO_RECOVERY:
            return VERR_IO_GEN_FAILURE;
        case TRY_AGAIN:
            return VERR_TRY_AGAIN;

        default:
            AssertLogRelMsgFailed(("Unhandled error %u\n", h_errno));
            return VERR_UNRESOLVED_ERROR;
    }
#endif
}


/**
 * Converts from a native socket address to a generic IPRT network address.
 *
 * @returns IPRT status code.
 * @param   pSrc                The source address.
 * @param   cbSrc               The size of the source address.
 * @param   pAddr               Where to return the generic IPRT network
 *                              address.
 */
static int rtSocketNetAddrFromAddr(RTSOCKADDRUNION const *pSrc, size_t cbSrc, PRTNETADDR pAddr)
{
    /*
     * Convert the address.
     */
    if (   cbSrc == sizeof(struct sockaddr_in)
        && pSrc->Addr.sa_family == AF_INET)
    {
        RT_ZERO(*pAddr);
        pAddr->enmType      = RTNETADDRTYPE_IPV4;
        pAddr->uPort        = RT_N2H_U16(pSrc->IPv4.sin_port);
        pAddr->uAddr.IPv4.u = pSrc->IPv4.sin_addr.s_addr;
    }
#ifdef IPRT_WITH_TCPIP_V6
    else if (   cbSrc == sizeof(struct sockaddr_in6)
             && pSrc->Addr.sa_family == AF_INET6)
    {
        RT_ZERO(*pAddr);
        pAddr->enmType            = RTNETADDRTYPE_IPV6;
        pAddr->uPort              = RT_N2H_U16(pSrc->IPv6.sin6_port);
        pAddr->uAddr.IPv6.au32[0] = pSrc->IPv6.sin6_addr.s6_addr32[0];
        pAddr->uAddr.IPv6.au32[1] = pSrc->IPv6.sin6_addr.s6_addr32[1];
        pAddr->uAddr.IPv6.au32[2] = pSrc->IPv6.sin6_addr.s6_addr32[2];
        pAddr->uAddr.IPv6.au32[3] = pSrc->IPv6.sin6_addr.s6_addr32[3];
    }
#endif
    else
        return VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED;
    return VINF_SUCCESS;
}


/**
 * Converts from a generic IPRT network address to a native socket address.
 *
 * @returns IPRT status code.
 * @param   pAddr               Pointer to the generic IPRT network address.
 * @param   pDst                The source address.
 * @param   cbDst               The size of the source address.
 * @param   pcbAddr             Where to store the size of the returned address.
 *                              Optional
 */
static int rtSocketAddrFromNetAddr(PCRTNETADDR pAddr, RTSOCKADDRUNION *pDst, size_t cbDst, int *pcbAddr)
{
    RT_BZERO(pDst, cbDst);
    if (pAddr->enmType == RTNETADDRTYPE_IPV4)
    {
        if (cbDst < sizeof(struct sockaddr_in))
            return VERR_BUFFER_OVERFLOW;

        pDst->Addr.sa_family       = AF_INET;
        pDst->IPv4.sin_port        = RT_H2N_U16(pAddr->uPort);
        pDst->IPv4.sin_addr.s_addr = pAddr->uAddr.IPv4.u;
        if (pcbAddr)
            *pcbAddr = sizeof(pDst->IPv4);
    }
#ifdef IPRT_WITH_TCPIP_V6
    else if (pAddr->enmType == RTNETADDRTYPE_IPV6)
    {
        if (cbDst < sizeof(struct sockaddr_in6))
            return VERR_BUFFER_OVERFLOW;

        pDst->Addr.sa_family              = AF_INET6;
        pDst->IPv6.sin6_port              = RT_H2N_U16(pAddr->uPort);
        pSrc->IPv6.sin6_addr.s6_addr32[0] = pAddr->uAddr.IPv6.au32[0];
        pSrc->IPv6.sin6_addr.s6_addr32[1] = pAddr->uAddr.IPv6.au32[1];
        pSrc->IPv6.sin6_addr.s6_addr32[2] = pAddr->uAddr.IPv6.au32[2];
        pSrc->IPv6.sin6_addr.s6_addr32[3] = pAddr->uAddr.IPv6.au32[3];
        if (pcbAddr)
            *pcbAddr = sizeof(pDst->IPv6);
    }
#endif
    else
        return VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED;
    return VINF_SUCCESS;
}


/**
 * Tries to lock the socket for exclusive usage by the calling thread.
 *
 * Call rtSocketUnlock() to unlock.
 *
 * @returns @c true if locked, @c false if not.
 * @param   pThis               The socket structure.
 */
DECLINLINE(bool) rtSocketTryLock(RTSOCKETINT *pThis)
{
    return ASMAtomicCmpXchgU32(&pThis->cUsers, 1, 0);
}


/**
 * Unlocks the socket.
 *
 * @param   pThis               The socket structure.
 */
DECLINLINE(void) rtSocketUnlock(RTSOCKETINT *pThis)
{
    ASMAtomicCmpXchgU32(&pThis->cUsers, 0, 1);
}


/**
 * The slow path of rtSocketSwitchBlockingMode that does the actual switching.
 *
 * @returns IPRT status code.
 * @param   pThis               The socket structure.
 * @param   fBlocking           The desired mode of operation.
 * @remarks Do not call directly.
 */
static int rtSocketSwitchBlockingModeSlow(RTSOCKETINT *pThis, bool fBlocking)
{
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnioctlsocket, VERR_NET_NOT_UNSUPPORTED);
    u_long uBlocking = fBlocking ? 0 : 1;
    if (g_pfnioctlsocket(pThis->hNative, FIONBIO, &uBlocking))
        return rtSocketError();

#else
    int fFlags = fcntl(pThis->hNative, F_GETFL, 0);
    if (fFlags == -1)
        return rtSocketError();

    if (fBlocking)
        fFlags &= ~O_NONBLOCK;
    else
        fFlags |= O_NONBLOCK;
    if (fcntl(pThis->hNative, F_SETFL, fFlags) == -1)
       return rtSocketError();
#endif

    pThis->fBlocking = fBlocking;
    return VINF_SUCCESS;
}


/**
 * Switches the socket to the desired blocking mode if necessary.
 *
 * The socket must be locked.
 *
 * @returns IPRT status code.
 * @param   pThis               The socket structure.
 * @param   fBlocking           The desired mode of operation.
 */
DECLINLINE(int) rtSocketSwitchBlockingMode(RTSOCKETINT *pThis, bool fBlocking)
{
    if (pThis->fBlocking != fBlocking)
        return rtSocketSwitchBlockingModeSlow(pThis, fBlocking);
    return VINF_SUCCESS;
}


/**
 * Creates an IPRT socket handle for a native one.
 *
 * @returns IPRT status code.
 * @param   ppSocket        Where to return the IPRT socket handle.
 * @param   hNative         The native handle.
 * @param   fLeaveOpen      Whether to leave the native socket handle open when
 *                          closed.
 */
DECLHIDDEN(int) rtSocketCreateForNative(RTSOCKETINT **ppSocket, RTSOCKETNATIVE hNative, bool fLeaveOpen)
{
    RTSOCKETINT *pThis = (RTSOCKETINT *)RTMemPoolAlloc(RTMEMPOOL_DEFAULT, sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    pThis->u32Magic         = RTSOCKET_MAGIC;
    pThis->cUsers           = 0;
    pThis->hNative          = hNative;
    pThis->fClosed          = false;
    pThis->fLeaveOpen       = fLeaveOpen;
    pThis->fBlocking        = true;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    pThis->hPollSet         = NIL_RTPOLLSET;
#endif
#ifdef RT_OS_WINDOWS
    pThis->hEvent                   = WSA_INVALID_EVENT;
    pThis->fPollEvts                = 0;
    pThis->fSubscribedEvts          = 0;
    pThis->fEventsSaved             = 0;
    pThis->fHarvestedEvents         = false;
    pThis->fPollFallback            = g_uWinSockInitedVersion < MAKEWORD(2, 0)
                                   || g_pfnWSACreateEvent == NULL
                                   || g_pfnWSACloseEvent == NULL
                                   || g_pfnWSAEventSelect == NULL
                                   || g_pfnWSAEnumNetworkEvents == NULL;
    pThis->fPollFallbackActive      = false;
    pThis->fPollFallbackShutdown    = false;
    pThis->hPollFallbackNotifyR     = NIL_RTSOCKETNATIVE;
    pThis->hPollFallbackNotifyW     = NIL_RTSOCKETNATIVE;
    pThis->hPollFallbackThread      = NIL_RTTHREAD;
#endif
    *ppSocket = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTSocketFromNative(PRTSOCKET phSocket, RTHCINTPTR uNative)
{
    AssertReturn(uNative != NIL_RTSOCKETNATIVE, VERR_INVALID_PARAMETER);
#ifndef RT_OS_WINDOWS
    AssertReturn(uNative >= 0, VERR_INVALID_PARAMETER);
#endif
    AssertPtrReturn(phSocket, VERR_INVALID_POINTER);
    return rtSocketCreateForNative(phSocket, uNative, false /*fLeaveOpen*/);
}


/**
 * Wrapper around socket().
 *
 * @returns IPRT status code.
 * @param   phSocket            Where to store the handle to the socket on
 *                              success.
 * @param   iDomain             The protocol family (PF_XXX).
 * @param   iType               The socket type (SOCK_XXX).
 * @param   iProtocol           Socket parameter, usually 0.
 * @param   fInheritable        Set to true if the socket should be inherted by
 *                              child processes, false if not inheritable.
 */
DECLHIDDEN(int) rtSocketCreate(PRTSOCKET phSocket, int iDomain, int iType, int iProtocol, bool fInheritable)
{
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnsocket, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnclosesocket, VERR_NET_NOT_UNSUPPORTED);

    /* Initialize WinSock. */
    int rc2 = rtSocketInitWinsock();
    if (RT_FAILURE(rc2))
        return rc2;
#endif

    /*
     * Create the socket.
     *
     * The RTSocketSetInheritance operation isn't necessarily reliable on windows,
     * so try use WSA_FLAG_NO_HANDLE_INHERIT with WSASocketW when possible.
     */
#ifdef RT_OS_WINDOWS
    bool           fCallSetInheritance = true;
    RTSOCKETNATIVE hNative;
    if (g_pfnWSASocketW)
    {
        DWORD fWsaFlags = WSA_FLAG_OVERLAPPED | (!fInheritable ? WSA_FLAG_NO_HANDLE_INHERIT : 0);
        hNative = g_pfnWSASocketW(iDomain, iType, iProtocol, NULL, 0 /*Group*/, fWsaFlags);
        if (hNative != NIL_RTSOCKETNATIVE)
            fCallSetInheritance = false;
        else
        {
            if (!fInheritable)
                hNative = g_pfnsocket(iDomain, iType, iProtocol);
            if (hNative == NIL_RTSOCKETNATIVE)
                return rtSocketError();
        }
    }
    else
    {
        hNative = g_pfnsocket(iDomain, iType, iProtocol);
        if (hNative == NIL_RTSOCKETNATIVE)
            return rtSocketError();
    }
#else
    RTSOCKETNATIVE hNative = socket(iDomain, iType, iProtocol);
    if (hNative == NIL_RTSOCKETNATIVE)
        return rtSocketError();
#endif

    /*
     * Wrap it.
     */
    int rc = rtSocketCreateForNative(phSocket, hNative, false /*fLeaveOpen*/);
    if (RT_SUCCESS(rc))
    {
#ifdef RT_OS_WINDOWS
        if (fCallSetInheritance)
#endif
            RTSocketSetInheritance(*phSocket, fInheritable);
    }
    else
    {
#ifdef RT_OS_WINDOWS
        g_pfnclosesocket(hNative);
#else
        close(hNative);
#endif
    }
    return rc;
}


/**
 * Wrapper around socketpair() for creating a local TCP connection.
 *
 * @returns IPRT status code.
 * @param   phServer            Where to return the first native socket.
 * @param   phClient            Where to return the second native socket.
 */
static int rtSocketCreateNativeTcpPair(RTSOCKETNATIVE *phServer, RTSOCKETNATIVE *phClient)
{
#ifdef RT_OS_WINDOWS
    /*
     * Initialize WinSock and make sure we got the necessary APIs.
     */
    int rc = rtSocketInitWinsock();
    if (RT_FAILURE(rc))
        return rc;
    AssertReturn(g_pfnsocket, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnclosesocket, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnsetsockopt, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnbind, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfngetsockname, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnlisten, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnaccept, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnconnect, VERR_NET_NOT_UNSUPPORTED);

    /*
     * Create the "server" listen socket and the "client" socket.
     */
    RTSOCKETNATIVE hListener = g_pfnsocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (hListener == NIL_RTSOCKETNATIVE)
        return rtSocketError();
    RTSOCKETNATIVE hClient = g_pfnsocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (hClient != NIL_RTSOCKETNATIVE)
    {

        /*
         * We let WinSock choose a port number when we bind.
         */
        union
        {
            struct sockaddr_in  Ip;
            struct sockaddr     Generic;
        } uAddr;
        RT_ZERO(uAddr);
        uAddr.Ip.sin_family      = AF_INET;
        uAddr.Ip.sin_addr.s_addr = RT_H2N_U32_C(INADDR_LOOPBACK);
        //uAddr.Ip.sin_port      = 0;
        int fReuse = 1;
        rc = g_pfnsetsockopt(hListener, SOL_SOCKET, SO_REUSEADDR, (const char *)&fReuse, sizeof(fReuse));
        if (rc == 0)
        {
            rc = g_pfnbind(hListener, &uAddr.Generic, sizeof(uAddr.Ip));
            if (rc == 0)
            {
                /*
                 * Get the address the client should connect to.  According to the docs,
                 * we cannot assume that getsockname sets the IP and family.
                 */
                RT_ZERO(uAddr);
                int cbAddr = sizeof(uAddr.Ip);
                rc = g_pfngetsockname(hListener, &uAddr.Generic, &cbAddr);
                if (rc == 0)
                {
                    uAddr.Ip.sin_family      = AF_INET;
                    uAddr.Ip.sin_addr.s_addr = RT_H2N_U32_C(INADDR_LOOPBACK);

                    /*
                     * Listen, connect and accept.
                     */
                    rc = g_pfnlisten(hListener, 1 /*cBacklog*/);
                    if (rc == 0)
                    {
                        rc = g_pfnconnect(hClient, &uAddr.Generic, sizeof(uAddr.Ip));
                        if (rc == 0)
                        {
                            RTSOCKETNATIVE hServer = g_pfnaccept(hListener, NULL, NULL);
                            if (hServer != NIL_RTSOCKETNATIVE)
                            {
                                g_pfnclosesocket(hListener);

                                /*
                                 * Done!
                                 */
                                *phServer = hServer;
                                *phClient = hClient;
                                return VINF_SUCCESS;
                            }
                        }
                    }
                }
            }
        }
        rc = rtSocketError();
        g_pfnclosesocket(hClient);
    }
    else
        rc = rtSocketError();
    g_pfnclosesocket(hListener);
    return rc;

#else
    /*
     * Got socket pair, so use it.
     * Note! This isn't TCP per se, but it should fool the users.
     */
    int aSockets[2] = { -1, -1 };
    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, aSockets) == 0)
    {
        *phServer = aSockets[0];
        *phClient = aSockets[1];
        return VINF_SUCCESS;
    }
    return rtSocketError();
#endif
}


/**
 * Worker for RTTcpCreatePair.
 *
 * @returns IPRT status code.
 * @param   phServer            Where to return the "server" side of the pair.
 * @param   phClient            Where to return the "client" side of the pair.
 * @note    There is no server or client side, but we gotta call it something.
 */
DECLHIDDEN(int) rtSocketCreateTcpPair(RTSOCKET *phServer, RTSOCKET *phClient)
{
    RTSOCKETNATIVE hServer = NIL_RTSOCKETNATIVE;
    RTSOCKETNATIVE hClient = NIL_RTSOCKETNATIVE;
    int rc = rtSocketCreateNativeTcpPair(&hServer, &hClient);
    if (RT_SUCCESS(rc))
    {
        rc = rtSocketCreateForNative(phServer, hServer, false /*fLeaveOpen*/);
        if (RT_SUCCESS(rc))
        {
            rc = rtSocketCreateForNative(phClient, hClient, false /*fLeaveOpen*/);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
            RTSocketRelease(*phServer);
        }
        else
        {
#ifdef RT_OS_WINDOWS
            g_pfnclosesocket(hServer);
#else
            close(hServer);
#endif
        }
#ifdef RT_OS_WINDOWS
        g_pfnclosesocket(hClient);
#else
        close(hClient);
#endif
    }

    *phServer = NIL_RTSOCKET;
    *phClient = NIL_RTSOCKET;
    return rc;
}


RTDECL(uint32_t) RTSocketRetain(RTSOCKET hSocket)
{
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, UINT32_MAX);
    return RTMemPoolRetain(pThis);
}


/**
 * Worker for RTSocketRelease and RTSocketClose.
 *
 * @returns IPRT status code.
 * @param   pThis               The socket handle instance data.
 * @param   fDestroy            Whether we're reaching ref count zero.
 */
static int rtSocketCloseIt(RTSOCKETINT *pThis, bool fDestroy)
{
    /*
     * Invalidate the handle structure on destroy.
     */
    if (fDestroy)
    {
        Assert(ASMAtomicReadU32(&pThis->u32Magic) == RTSOCKET_MAGIC);
        ASMAtomicWriteU32(&pThis->u32Magic, RTSOCKET_MAGIC_DEAD);
    }

    int rc = VINF_SUCCESS;
    if (ASMAtomicCmpXchgBool(&pThis->fClosed, true, false))
    {
#ifdef RT_OS_WINDOWS
        /*
         * Poke the polling thread if active and give it a small chance to stop.
         */
        if (   pThis->fPollFallback
            && pThis->hPollFallbackThread != NIL_RTTHREAD)
        {
            ASMAtomicWriteBool(&pThis->fPollFallbackShutdown, true);
            rtSocketPokePollFallbackThread(pThis);
            int rc2 = RTThreadWait(pThis->hPollFallbackThread, RT_MS_1SEC, NULL);
            if (RT_SUCCESS(rc2))
                pThis->hPollFallbackThread = NIL_RTTHREAD;
        }
#endif

        /*
         * Close the native handle.
         */
        RTSOCKETNATIVE hNative = pThis->hNative;
        if (hNative != NIL_RTSOCKETNATIVE)
        {
            pThis->hNative = NIL_RTSOCKETNATIVE;

            if (!pThis->fLeaveOpen)
            {
#ifdef RT_OS_WINDOWS
                AssertReturn(g_pfnclosesocket, VERR_NET_NOT_UNSUPPORTED);
                if (g_pfnclosesocket(hNative))
#else
                if (close(hNative))
#endif
                {
                    rc = rtSocketError();
#ifdef RT_OS_WINDOWS
                    AssertMsgFailed(("closesocket(%p) -> %Rrc\n", (uintptr_t)hNative, rc));
#else
                    AssertMsgFailed(("close(%d) -> %Rrc\n", hNative, rc));
#endif
                }
            }
        }

#ifdef RT_OS_WINDOWS
        /*
         * Windows specific polling cleanup.
         */
        WSAEVENT hEvent = pThis->hEvent;
        if (hEvent != WSA_INVALID_EVENT)
        {
            pThis->hEvent = WSA_INVALID_EVENT;
            if (!pThis->fPollFallback)
            {
                Assert(g_pfnWSACloseEvent);
                if (g_pfnWSACloseEvent)
                    g_pfnWSACloseEvent(hEvent);
            }
            else
                CloseHandle(hEvent);
        }

        if (pThis->fPollFallback)
        {
            if (pThis->hPollFallbackNotifyW != NIL_RTSOCKETNATIVE)
            {
                g_pfnclosesocket(pThis->hPollFallbackNotifyW);
                pThis->hPollFallbackNotifyW = NIL_RTSOCKETNATIVE;
            }

            if (pThis->hPollFallbackThread != NIL_RTTHREAD)
            {
                int rc2 = RTThreadWait(pThis->hPollFallbackThread, RT_MS_1MIN / 2, NULL);
                AssertRC(rc2);
                pThis->hPollFallbackThread = NIL_RTTHREAD;
            }

            if (pThis->hPollFallbackNotifyR != NIL_RTSOCKETNATIVE)
            {
                g_pfnclosesocket(pThis->hPollFallbackNotifyR);
                pThis->hPollFallbackNotifyR = NIL_RTSOCKETNATIVE;
            }
        }
#endif
    }

    return rc;
}


RTDECL(uint32_t) RTSocketRelease(RTSOCKET hSocket)
{
    RTSOCKETINT *pThis = hSocket;
    if (pThis == NIL_RTSOCKET)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, UINT32_MAX);

    /* get the refcount without killing it... */
    uint32_t cRefs = RTMemPoolRefCount(pThis);
    AssertReturn(cRefs != UINT32_MAX, UINT32_MAX);
    if (cRefs == 1)
        rtSocketCloseIt(pThis, true);

    return RTMemPoolRelease(RTMEMPOOL_DEFAULT, pThis);
}


RTDECL(int) RTSocketClose(RTSOCKET hSocket)
{
    RTSOCKETINT *pThis = hSocket;
    if (pThis == NIL_RTSOCKET)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);

    uint32_t cRefs = RTMemPoolRefCount(pThis);
    AssertReturn(cRefs != UINT32_MAX, UINT32_MAX);

    int rc = rtSocketCloseIt(pThis, cRefs == 1);

    RTMemPoolRelease(RTMEMPOOL_DEFAULT, pThis);
    return rc;
}


RTDECL(RTHCUINTPTR) RTSocketToNative(RTSOCKET hSocket)
{
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, RTHCUINTPTR_MAX);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, RTHCUINTPTR_MAX);
    return (RTHCUINTPTR)pThis->hNative;
}


RTDECL(int) RTSocketSetInheritance(RTSOCKET hSocket, bool fInheritable)
{
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRefCount(pThis) >= (pThis->cUsers ? 2U : 1U), VERR_CALLER_NO_REFERENCE);

#ifndef RT_OS_WINDOWS
    if (fcntl(pThis->hNative, F_SETFD, fInheritable ? 0 : FD_CLOEXEC) < 0)
        return RTErrConvertFromErrno(errno);
    return VINF_SUCCESS;
#else
    /* Windows is more complicated as sockets are complicated wrt inheritance
       (see stackoverflow for details).  In general, though we cannot hope to
       make a socket really non-inheritable before vista as other layers in
       the winsock maze may have additional handles associated with the socket. */
    if (g_pfnGetHandleInformation)
    {
        /* Check if the handle is already in what seems to be the right state
           before we try doing anything. */
        DWORD fFlags;
        if (g_pfnGetHandleInformation((HANDLE)pThis->hNative, &fFlags))
        {
            if (RT_BOOL(fFlags & HANDLE_FLAG_INHERIT) == fInheritable)
                return VINF_SUCCESS;
        }
    }

    if (!g_pfnSetHandleInformation)
        return VERR_NET_NOT_UNSUPPORTED;

    if (!g_pfnSetHandleInformation((HANDLE)pThis->hNative, HANDLE_FLAG_INHERIT, fInheritable ? HANDLE_FLAG_INHERIT : 0))
        return RTErrConvertFromWin32(GetLastError());
    /** @todo Need we do something related to WS_SIO_ASSOCIATE_HANDLE or
     *        WS_SIO_TRANSLATE_HANDLE? Or what  other handles could be associated
     *        with the socket? that we need to modify? */

    return VINF_SUCCESS;
#endif
}


static bool rtSocketIsIPv4Numerical(const char *pszAddress, PRTNETADDRIPV4 pAddr)
{

    /* Empty address resolves to the INADDR_ANY address (good for bind). */
    if (!pszAddress || !*pszAddress)
    {
        pAddr->u = INADDR_ANY;
        return true;
    }

    /* Four quads? */
    char *psz = (char *)pszAddress;
    for (int i = 0; i < 4; i++)
    {
        uint8_t u8;
        int rc = RTStrToUInt8Ex(psz, &psz, 0, &u8);
        if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
            return false;
        if (*psz != (i < 3 ? '.' : '\0'))
            return false;
        psz++;

        pAddr->au8[i] = u8;             /* big endian */
    }

    return true;
}

RTDECL(int) RTSocketParseInetAddress(const char *pszAddress, unsigned uPort, PRTNETADDR pAddr)
{
    int rc;

    /*
     * Validate input.
     */
    AssertReturn(uPort > 0, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszAddress, VERR_INVALID_POINTER);

    /*
     * Resolve the address. Pretty crude at the moment, but we have to make
     * sure to not ask the NT 4 gethostbyname about an IPv4 address as it may
     * give a wrong answer.
     */
    /** @todo this only supports IPv4, and IPv6 support needs to be added.
     * It probably needs to be converted to getaddrinfo(). */
    RTNETADDRIPV4 IPv4Quad;
    if (rtSocketIsIPv4Numerical(pszAddress, &IPv4Quad))
    {
        Log3(("rtSocketIsIPv4Numerical: %s -> %#x (%RTnaipv4)\n", pszAddress, IPv4Quad.u, IPv4Quad));
        RT_ZERO(*pAddr);
        pAddr->enmType      = RTNETADDRTYPE_IPV4;
        pAddr->uPort        = uPort;
        pAddr->uAddr.IPv4   = IPv4Quad;
        return VINF_SUCCESS;
    }

#ifdef RT_OS_WINDOWS
    /* Initialize WinSock and check version before we call gethostbyname. */
    if (!g_pfngethostbyname)
        return VERR_NET_NOT_UNSUPPORTED;

    int rc2 = rtSocketInitWinsock();
    if (RT_FAILURE(rc2))
        return rc2;

# define gethostbyname g_pfngethostbyname
#endif

    struct hostent *pHostEnt;
    pHostEnt = gethostbyname(pszAddress);
    if (!pHostEnt)
    {
        rc = rtSocketResolverError();
        AssertMsg(rc == VERR_NET_HOST_NOT_FOUND,
                  ("Could not resolve '%s', rc=%Rrc\n", pszAddress, rc));
        return rc;
    }

    if (pHostEnt->h_addrtype == AF_INET)
    {
        RT_ZERO(*pAddr);
        pAddr->enmType      = RTNETADDRTYPE_IPV4;
        pAddr->uPort        = uPort;
        pAddr->uAddr.IPv4.u = ((struct in_addr *)pHostEnt->h_addr)->s_addr;
        Log3(("gethostbyname: %s -> %#x (%RTnaipv4)\n", pszAddress, pAddr->uAddr.IPv4.u, pAddr->uAddr.IPv4));
    }
    else
        return VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED;

#ifdef RT_OS_WINDOWS
# undef gethostbyname
#endif
    return VINF_SUCCESS;
}


/*
 * New function to allow both ipv4 and ipv6 addresses to be resolved.
 * Breaks compatibility with windows before 2000.
 */
RTDECL(int) RTSocketQueryAddressStr(const char *pszHost, char *pszResult, size_t *pcbResult, PRTNETADDRTYPE penmAddrType)
{
    AssertPtrReturn(pszHost, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbResult, VERR_INVALID_POINTER);
    AssertPtrNullReturn(penmAddrType, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pszResult, VERR_INVALID_POINTER);

#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS) /** @todo dynamically resolve the APIs not present in NT4! */
    return VERR_NOT_SUPPORTED;

#else
    int rc;
    if (*pcbResult < 16)
        return VERR_NET_ADDRESS_NOT_AVAILABLE;

    /* Setup the hint. */
    struct addrinfo grHints;
    RT_ZERO(grHints);
    grHints.ai_socktype = 0;
    grHints.ai_flags    = 0;
    grHints.ai_protocol = 0;
    grHints.ai_family   = AF_UNSPEC;
    if (penmAddrType)
    {
        switch (*penmAddrType)
        {
            case RTNETADDRTYPE_INVALID:
                /*grHints.ai_family = AF_UNSPEC;*/
                break;
            case RTNETADDRTYPE_IPV4:
                grHints.ai_family = AF_INET;
                break;
            case RTNETADDRTYPE_IPV6:
                grHints.ai_family = AF_INET6;
                break;
            default:
                AssertFailedReturn(VERR_INVALID_PARAMETER);
        }
    }

# ifdef RT_OS_WINDOWS
    /*
     * Winsock2 init
     */
    if (   !g_pfngetaddrinfo
        || !g_pfnfreeaddrinfo)
        return VERR_NET_NOT_UNSUPPORTED;

    int rc2 = rtSocketInitWinsock();
    if (RT_FAILURE(rc2))
        return rc2;

#  define getaddrinfo  g_pfngetaddrinfo
#  define freeaddrinfo g_pfnfreeaddrinfo
# endif

    /** @todo r=bird: getaddrinfo and freeaddrinfo breaks the additions on NT4. */
    struct addrinfo *pgrResults = NULL;
    rc = getaddrinfo(pszHost, "", &grHints, &pgrResults);
    if (rc != 0)
        return VERR_NET_ADDRESS_NOT_AVAILABLE;

    // return data
    // on multiple matches return only the first one

    if (!pgrResults)
        return VERR_NET_ADDRESS_NOT_AVAILABLE;

    struct addrinfo const *pgrResult = pgrResults->ai_next;
    if (!pgrResult)
    {
        freeaddrinfo(pgrResults);
        return VERR_NET_ADDRESS_NOT_AVAILABLE;
    }

    RTNETADDRTYPE   enmAddrType = RTNETADDRTYPE_INVALID;
    size_t          cchIpAddress;
    char            szIpAddress[48];
    if (pgrResult->ai_family == AF_INET)
    {
        struct sockaddr_in const *pgrSa = (struct sockaddr_in const *)pgrResult->ai_addr;
        cchIpAddress = RTStrPrintf(szIpAddress, sizeof(szIpAddress),
                                   "%RTnaipv4", pgrSa->sin_addr.s_addr);
        Assert(cchIpAddress >= 7 && cchIpAddress < sizeof(szIpAddress) - 1);
        enmAddrType = RTNETADDRTYPE_IPV4;
        rc = VINF_SUCCESS;
    }
    else if (pgrResult->ai_family == AF_INET6)
    {
        struct sockaddr_in6 const *pgrSa6 = (struct sockaddr_in6 const *)pgrResult->ai_addr;
        cchIpAddress = RTStrPrintf(szIpAddress, sizeof(szIpAddress),
                                   "%RTnaipv6", (PRTNETADDRIPV6)&pgrSa6->sin6_addr);
        enmAddrType = RTNETADDRTYPE_IPV6;
        rc = VINF_SUCCESS;
    }
    else
    {
        rc = VERR_NET_ADDRESS_NOT_AVAILABLE;
        szIpAddress[0] = '\0';
        cchIpAddress = 0;
    }
    freeaddrinfo(pgrResults);

    /*
     * Copy out the result.
     */
    size_t const cbResult = *pcbResult;
    *pcbResult = cchIpAddress + 1;
    if (cchIpAddress < cbResult)
        memcpy(pszResult, szIpAddress, cchIpAddress + 1);
    else
    {
        RT_BZERO(pszResult, cbResult);
        if (RT_SUCCESS(rc))
            rc = VERR_BUFFER_OVERFLOW;
    }
    if (penmAddrType && RT_SUCCESS(rc))
        *penmAddrType = enmAddrType;
    return rc;

# ifdef RT_OS_WINDOWS
#  undef getaddrinfo
#  undef freeaddrinfo
# endif
#endif /* !RT_OS_OS2 */
}


RTDECL(int) RTSocketRead(RTSOCKET hSocket, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(cbBuffer > 0, VERR_INVALID_PARAMETER);
    AssertPtr(pvBuffer);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnrecv, VERR_NET_NOT_UNSUPPORTED);
# define recv g_pfnrecv
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = rtSocketSwitchBlockingMode(pThis, true /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read loop.
     * If pcbRead is NULL we have to fill the entire buffer!
     */
    size_t  cbRead   = 0;
    size_t  cbToRead = cbBuffer;
    for (;;)
    {
        rtSocketErrorReset();
#ifdef RTSOCKET_MAX_READ
        int    cbNow = cbToRead >= RTSOCKET_MAX_READ ? RTSOCKET_MAX_READ : (int)cbToRead;
#else
        size_t cbNow = cbToRead;
#endif
        ssize_t cbBytesRead = recv(pThis->hNative, (char *)pvBuffer + cbRead, cbNow, MSG_NOSIGNAL);
        if (cbBytesRead <= 0)
        {
            rc = rtSocketError();
            Assert(RT_FAILURE_NP(rc) || cbBytesRead == 0);
            if (RT_SUCCESS_NP(rc))
            {
                if (!pcbRead)
                    rc = VERR_NET_SHUTDOWN;
                else
                {
                    *pcbRead = 0;
                    rc = VINF_SUCCESS;
                }
            }
            break;
        }
        if (pcbRead)
        {
            /* return partial data */
            *pcbRead = cbBytesRead;
            break;
        }

        /* read more? */
        cbRead += cbBytesRead;
        if (cbRead == cbBuffer)
            break;

        /* next */
        cbToRead = cbBuffer - cbRead;
    }

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef recv
#endif
    return rc;
}


RTDECL(int) RTSocketReadFrom(RTSOCKET hSocket, void *pvBuffer, size_t cbBuffer, size_t *pcbRead, PRTNETADDR pSrcAddr)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(cbBuffer > 0, VERR_INVALID_PARAMETER);
    AssertPtr(pvBuffer);
    AssertPtr(pcbRead);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnrecvfrom, VERR_NET_NOT_UNSUPPORTED);
# define recvfrom g_pfnrecvfrom
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = rtSocketSwitchBlockingMode(pThis, true /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read data.
     */
    size_t  cbRead   = 0;
    size_t  cbToRead = cbBuffer;
    rtSocketErrorReset();
    RTSOCKADDRUNION u;
#ifdef RTSOCKET_MAX_READ
    int       cbNow  = cbToRead >= RTSOCKET_MAX_READ ? RTSOCKET_MAX_READ : (int)cbToRead;
    int       cbAddr = sizeof(u);
#else
    size_t    cbNow  = cbToRead;
    socklen_t cbAddr = sizeof(u);
#endif
    ssize_t cbBytesRead = recvfrom(pThis->hNative, (char *)pvBuffer + cbRead, cbNow, MSG_NOSIGNAL, &u.Addr, &cbAddr);
    if (cbBytesRead <= 0)
    {
        rc = rtSocketError();
        Assert(RT_FAILURE_NP(rc) || cbBytesRead == 0);
        if (RT_SUCCESS_NP(rc))
        {
            *pcbRead = 0;
            rc = VINF_SUCCESS;
        }
    }
    else
    {
        if (pSrcAddr)
            rc = rtSocketNetAddrFromAddr(&u, cbAddr, pSrcAddr);
        *pcbRead = cbBytesRead;
    }

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef recvfrom
#endif
    return rc;
}


RTDECL(int) RTSocketWrite(RTSOCKET hSocket, const void *pvBuffer, size_t cbBuffer)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnsend, VERR_NET_NOT_UNSUPPORTED);
# define send g_pfnsend
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = rtSocketSwitchBlockingMode(pThis, true /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Try write all at once.
     */
#ifdef RTSOCKET_MAX_WRITE
    int     cbNow     = cbBuffer >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbBuffer;
#else
    size_t  cbNow     = cbBuffer >= SSIZE_MAX   ? SSIZE_MAX   :      cbBuffer;
#endif
    ssize_t cbWritten = send(pThis->hNative, (const char *)pvBuffer, cbNow, MSG_NOSIGNAL);
    if (RT_LIKELY((size_t)cbWritten == cbBuffer && cbWritten >= 0))
        rc = VINF_SUCCESS;
    else if (cbWritten < 0)
        rc = rtSocketError();
    else
    {
        /*
         * Unfinished business, write the remainder of the request.  Must ignore
         * VERR_INTERRUPTED here if we've managed to send something.
         */
        size_t cbSentSoFar = 0;
        for (;;)
        {
            /* advance */
            cbBuffer    -= (size_t)cbWritten;
            if (!cbBuffer)
                break;
            cbSentSoFar += (size_t)cbWritten;
            pvBuffer     = (char const *)pvBuffer + cbWritten;

            /* send */
#ifdef RTSOCKET_MAX_WRITE
            cbNow = cbBuffer >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbBuffer;
#else
            cbNow = cbBuffer >= SSIZE_MAX   ? SSIZE_MAX   :      cbBuffer;
#endif
            cbWritten = send(pThis->hNative, (const char *)pvBuffer, cbNow, MSG_NOSIGNAL);
            if (cbWritten >= 0)
                AssertMsg(cbBuffer >= (size_t)cbWritten, ("Wrote more than we requested!!! cbWritten=%zu cbBuffer=%zu rtSocketError()=%d\n",
                                                          cbWritten, cbBuffer, rtSocketError()));
            else
            {
                rc = rtSocketError();
                if (rc != VERR_INTERNAL_ERROR || cbSentSoFar == 0)
                    break;
                cbWritten = 0;
                rc = VINF_SUCCESS;
            }
        }
    }

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef send
#endif
    return rc;
}


RTDECL(int) RTSocketWriteTo(RTSOCKET hSocket, const void *pvBuffer, size_t cbBuffer, PCRTNETADDR pAddr)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnsendto, VERR_NET_NOT_UNSUPPORTED);
# define sendto g_pfnsendto
#endif

    /* no locking since UDP reads may be done concurrently to writes, and
     * this is the normal use case of this code. */

    int rc = rtSocketSwitchBlockingMode(pThis, true /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    /* Figure out destination address. */
    struct sockaddr *pSA = NULL;
#ifdef RT_OS_WINDOWS
    int cbSA = 0;
#else
    socklen_t cbSA = 0;
#endif
    RTSOCKADDRUNION u;
    if (pAddr)
    {
        rc = rtSocketAddrFromNetAddr(pAddr, &u, sizeof(u), NULL);
        if (RT_FAILURE(rc))
            return rc;
        pSA = &u.Addr;
        cbSA = sizeof(u);
    }

    /*
     * Must write all at once, otherwise it is a failure.
     */
#ifdef RT_OS_WINDOWS
    int     cbNow     = cbBuffer >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbBuffer;
#else
    size_t  cbNow     = cbBuffer >= SSIZE_MAX   ? SSIZE_MAX   :      cbBuffer;
#endif
    ssize_t cbWritten = sendto(pThis->hNative, (const char *)pvBuffer, cbNow, MSG_NOSIGNAL, pSA, cbSA);
    if (RT_LIKELY((size_t)cbWritten == cbBuffer && cbWritten >= 0))
        rc = VINF_SUCCESS;
    else if (cbWritten < 0)
        rc = rtSocketError();
    else
        rc = VERR_TOO_MUCH_DATA;

    /// @todo rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef sendto
#endif
    return rc;
}


RTDECL(int) RTSocketWriteToNB(RTSOCKET hSocket, const void *pvBuffer, size_t cbBuffer, PCRTNETADDR pAddr)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnsendto, VERR_NET_NOT_UNSUPPORTED);
# define sendto g_pfnsendto
#endif

    /* no locking since UDP reads may be done concurrently to writes, and
     * this is the normal use case of this code. */

    int rc = rtSocketSwitchBlockingMode(pThis, false /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    /* Figure out destination address. */
    struct sockaddr *pSA = NULL;
#ifdef RT_OS_WINDOWS
    int cbSA = 0;
#else
    socklen_t cbSA = 0;
#endif
    RTSOCKADDRUNION u;
    if (pAddr)
    {
        rc = rtSocketAddrFromNetAddr(pAddr, &u, sizeof(u), NULL);
        if (RT_FAILURE(rc))
            return rc;
        pSA = &u.Addr;
        cbSA = sizeof(u);
    }

    /*
     * Must write all at once, otherwise it is a failure.
     */
#ifdef RT_OS_WINDOWS
    int     cbNow     = cbBuffer >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbBuffer;
#else
    size_t  cbNow     = cbBuffer >= SSIZE_MAX   ? SSIZE_MAX   :      cbBuffer;
#endif
    ssize_t cbWritten = sendto(pThis->hNative, (const char *)pvBuffer, cbNow, MSG_NOSIGNAL, pSA, cbSA);
    if (RT_LIKELY((size_t)cbWritten == cbBuffer && cbWritten >= 0))
        rc = VINF_SUCCESS;
    else if (cbWritten < 0)
        rc = rtSocketError();
    else
        rc = VERR_TOO_MUCH_DATA;

    /// @todo rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef sendto
#endif
    return rc;
}


RTDECL(int) RTSocketSgWrite(RTSOCKET hSocket, PCRTSGBUF pSgBuf)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pSgBuf, VERR_INVALID_PARAMETER);
    AssertReturn(pSgBuf->cSegs > 0, VERR_INVALID_PARAMETER);
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = rtSocketSwitchBlockingMode(pThis, true /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Construct message descriptor (translate pSgBuf) and send it.
     */
    rc = VERR_NO_TMP_MEMORY;
#ifdef RT_OS_WINDOWS
    if (g_pfnWSASend)
    {
        AssertCompileSize(WSABUF, sizeof(RTSGSEG));
        AssertCompileMemberSize(WSABUF, buf, RT_SIZEOFMEMB(RTSGSEG, pvSeg));

        LPWSABUF paMsg = (LPWSABUF)RTMemTmpAllocZ(pSgBuf->cSegs * sizeof(WSABUF));
        if (paMsg)
        {
            for (unsigned i = 0; i < pSgBuf->cSegs; i++)
            {
                paMsg[i].buf = (char *)pSgBuf->paSegs[i].pvSeg;
                paMsg[i].len = (u_long)pSgBuf->paSegs[i].cbSeg;
            }

            DWORD dwSent;
            int hrc = g_pfnWSASend(pThis->hNative, paMsg, pSgBuf->cSegs, &dwSent, MSG_NOSIGNAL, NULL, NULL);
            if (!hrc)
                rc = VINF_SUCCESS;
    /** @todo check for incomplete writes */
            else
                rc = rtSocketError();

            RTMemTmpFree(paMsg);
        }
    }
    else if (g_pfnsend)
    {
        rc = VINF_SUCCESS;
        for (uint32_t iSeg = 0; iSeg < pSgBuf->cSegs; iSeg++)
        {
            uint8_t const *pbSeg = (uint8_t const *)pSgBuf->paSegs[iSeg].pvSeg;
            size_t         cbSeg = pSgBuf->paSegs[iSeg].cbSeg;
            int            cbNow;
            ssize_t        cbWritten;
            for (;;)
            {
                cbNow = cbSeg >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbSeg;
                cbWritten = g_pfnsend(pThis->hNative, (const char *)pbSeg, cbNow, MSG_NOSIGNAL);
                if ((size_t)cbWritten >= cbSeg || cbWritten < 0)
                    break;
                pbSeg += cbWritten;
                cbSeg -= cbWritten;
            }
            if (cbWritten < 0)
            {
                rc = rtSocketError();
                break;
            }
        }
    }
    else
        rc = VERR_NET_NOT_UNSUPPORTED;

#else  /* !RT_OS_WINDOWS */
    AssertCompileSize(struct iovec, sizeof(RTSGSEG));
    AssertCompileMemberSize(struct iovec, iov_base, RT_SIZEOFMEMB(RTSGSEG, pvSeg));
    AssertCompileMemberSize(struct iovec, iov_len,  RT_SIZEOFMEMB(RTSGSEG, cbSeg));

    struct iovec *paMsg = (struct iovec *)RTMemTmpAllocZ(pSgBuf->cSegs * sizeof(struct iovec));
    if (paMsg)
    {
        for (unsigned i = 0; i < pSgBuf->cSegs; i++)
        {
            paMsg[i].iov_base = pSgBuf->paSegs[i].pvSeg;
            paMsg[i].iov_len  = pSgBuf->paSegs[i].cbSeg;
        }

        struct msghdr msgHdr;
        RT_ZERO(msgHdr);
        msgHdr.msg_iov    = paMsg;
        msgHdr.msg_iovlen = pSgBuf->cSegs;
        ssize_t cbWritten = sendmsg(pThis->hNative, &msgHdr, MSG_NOSIGNAL);
        if (RT_LIKELY(cbWritten >= 0))
            rc = VINF_SUCCESS;
/** @todo check for incomplete writes */
        else
            rc = rtSocketError();

        RTMemTmpFree(paMsg);
    }
#endif /* !RT_OS_WINDOWS */

    rtSocketUnlock(pThis);
    return rc;
}


RTDECL(int) RTSocketSgWriteL(RTSOCKET hSocket, size_t cSegs, ...)
{
    va_list va;
    va_start(va, cSegs);
    int rc = RTSocketSgWriteLV(hSocket, cSegs, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTSocketSgWriteLV(RTSOCKET hSocket, size_t cSegs, va_list va)
{
    /*
     * Set up a S/G segment array + buffer on the stack and pass it
     * on to RTSocketSgWrite.
     */
    Assert(cSegs <= 16);
    PRTSGSEG paSegs = (PRTSGSEG)alloca(cSegs * sizeof(RTSGSEG));
    AssertReturn(paSegs, VERR_NO_TMP_MEMORY);
    for (size_t i = 0; i < cSegs; i++)
    {
        paSegs[i].pvSeg = va_arg(va, void *);
        paSegs[i].cbSeg = va_arg(va, size_t);
    }

    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, paSegs, cSegs);
    return RTSocketSgWrite(hSocket, &SgBuf);
}


RTDECL(int) RTSocketReadNB(RTSOCKET hSocket, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(cbBuffer > 0, VERR_INVALID_PARAMETER);
    AssertPtr(pvBuffer);
    AssertPtrReturn(pcbRead, VERR_INVALID_PARAMETER);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnrecv, VERR_NET_NOT_UNSUPPORTED);
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = rtSocketSwitchBlockingMode(pThis, false /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    rtSocketErrorReset();
#ifdef RTSOCKET_MAX_READ
    int    cbNow = cbBuffer >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbBuffer;
#else
    size_t cbNow = cbBuffer;
#endif

#ifdef RT_OS_WINDOWS
    int cbRead = g_pfnrecv(pThis->hNative, (char *)pvBuffer, cbNow, MSG_NOSIGNAL);
    if (cbRead >= 0)
    {
        *pcbRead = cbRead;
        rc = VINF_SUCCESS;
    }
    else
    {
        rc = rtSocketError();
        if (rc == VERR_TRY_AGAIN)
        {
            *pcbRead = 0;
            rc = VINF_TRY_AGAIN;
        }
    }

#else
    ssize_t cbRead = recv(pThis->hNative, pvBuffer, cbNow, MSG_NOSIGNAL);
    if (cbRead >= 0)
        *pcbRead = cbRead;
    else if (   errno == EAGAIN
# ifdef EWOULDBLOCK
#  if EWOULDBLOCK != EAGAIN
             || errno == EWOULDBLOCK
#  endif
# endif
             )
    {
        *pcbRead = 0;
        rc = VINF_TRY_AGAIN;
    }
    else
        rc = rtSocketError();
#endif

    rtSocketUnlock(pThis);
    return rc;
}


RTDECL(int) RTSocketWriteNB(RTSOCKET hSocket, const void *pvBuffer, size_t cbBuffer, size_t *pcbWritten)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pcbWritten, VERR_INVALID_PARAMETER);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnsend, VERR_NET_NOT_UNSUPPORTED);
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = rtSocketSwitchBlockingMode(pThis, false /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    rtSocketErrorReset();
#ifdef RT_OS_WINDOWS
# ifdef RTSOCKET_MAX_WRITE
    int    cbNow = cbBuffer >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbBuffer;
# else
    size_t cbNow = cbBuffer;
# endif
    int cbWritten = g_pfnsend(pThis->hNative, (const char *)pvBuffer, cbNow, MSG_NOSIGNAL);
    if (cbWritten >= 0)
    {
        *pcbWritten = cbWritten;
        rc = VINF_SUCCESS;
    }
    else
    {
        rc = rtSocketError();
        if (rc == VERR_TRY_AGAIN)
        {
            *pcbWritten = 0;
            rc = VINF_TRY_AGAIN;
        }
    }
#else
    ssize_t cbWritten = send(pThis->hNative, pvBuffer, cbBuffer, MSG_NOSIGNAL);
    if (cbWritten >= 0)
        *pcbWritten = cbWritten;
    else if (   errno == EAGAIN
# ifdef EWOULDBLOCK
#  if EWOULDBLOCK != EAGAIN
             || errno == EWOULDBLOCK
#  endif
# endif
            )
    {
        *pcbWritten = 0;
        rc = VINF_TRY_AGAIN;
    }
    else
        rc = rtSocketError();
#endif

    rtSocketUnlock(pThis);
    return rc;
}


RTDECL(int) RTSocketSgWriteNB(RTSOCKET hSocket, PCRTSGBUF pSgBuf, size_t *pcbWritten)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pSgBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_PARAMETER);
    AssertReturn(pSgBuf->cSegs > 0, VERR_INVALID_PARAMETER);
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = rtSocketSwitchBlockingMode(pThis, false /* fBlocking */);
    if (RT_FAILURE(rc))
        return rc;

    unsigned cSegsToSend = 0;
    rc = VERR_NO_TMP_MEMORY;
#ifdef RT_OS_WINDOWS
    if (g_pfnWSASend)
    {
        LPWSABUF paMsg = NULL;
        RTSgBufMapToNative(paMsg, pSgBuf, WSABUF, buf, char *, len, u_long, cSegsToSend);
        if (paMsg)
        {
            DWORD dwSent = 0;
            int hrc = g_pfnWSASend(pThis->hNative, paMsg, cSegsToSend, &dwSent, MSG_NOSIGNAL, NULL, NULL);
            if (!hrc)
                rc = VINF_SUCCESS;
            else
                rc = rtSocketError();

            *pcbWritten = dwSent;

            RTMemTmpFree(paMsg);
        }
    }
    else if (g_pfnsend)
    {
        size_t cbWrittenTotal = 0;
        rc = VINF_SUCCESS;
        for (uint32_t iSeg = 0; iSeg < pSgBuf->cSegs; iSeg++)
        {
            uint8_t const *pbSeg = (uint8_t const *)pSgBuf->paSegs[iSeg].pvSeg;
            size_t         cbSeg = pSgBuf->paSegs[iSeg].cbSeg;
            int            cbNow;
            ssize_t        cbWritten;
            for (;;)
            {
                cbNow = cbSeg >= RTSOCKET_MAX_WRITE ? RTSOCKET_MAX_WRITE : (int)cbSeg;
                cbWritten = g_pfnsend(pThis->hNative, (const char *)pbSeg, cbNow, MSG_NOSIGNAL);
                if ((size_t)cbWritten >= cbSeg || cbWritten < 0)
                    break;
                cbWrittenTotal += cbWrittenTotal;
                pbSeg += cbWritten;
                cbSeg -= cbWritten;
            }
            if (cbWritten < 0)
            {
                rc = rtSocketError();
                break;
            }
            if (cbWritten != cbNow)
                break;
        }
        *pcbWritten = cbWrittenTotal;
    }
    else
        rc = VERR_NET_NOT_UNSUPPORTED;

#else  /* !RT_OS_WINDOWS */
    struct iovec *paMsg = NULL;

    RTSgBufMapToNative(paMsg, pSgBuf, struct iovec, iov_base, void *, iov_len, size_t, cSegsToSend);
    if (paMsg)
    {
        struct msghdr msgHdr;
        RT_ZERO(msgHdr);
        msgHdr.msg_iov    = paMsg;
        msgHdr.msg_iovlen = cSegsToSend;
        ssize_t cbWritten = sendmsg(pThis->hNative, &msgHdr, MSG_NOSIGNAL);
        if (RT_LIKELY(cbWritten >= 0))
        {
            rc = VINF_SUCCESS;
            *pcbWritten = cbWritten;
        }
        else
            rc = rtSocketError();

        RTMemTmpFree(paMsg);
    }
#endif /* !RT_OS_WINDOWS */

    rtSocketUnlock(pThis);
    return rc;
}


RTDECL(int) RTSocketSgWriteLNB(RTSOCKET hSocket, size_t cSegs, size_t *pcbWritten, ...)
{
    va_list va;
    va_start(va, pcbWritten);
    int rc = RTSocketSgWriteLVNB(hSocket, cSegs, pcbWritten, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTSocketSgWriteLVNB(RTSOCKET hSocket, size_t cSegs, size_t *pcbWritten, va_list va)
{
    /*
     * Set up a S/G segment array + buffer on the stack and pass it
     * on to RTSocketSgWrite.
     */
    Assert(cSegs <= 16);
    PRTSGSEG paSegs = (PRTSGSEG)alloca(cSegs * sizeof(RTSGSEG));
    AssertReturn(paSegs, VERR_NO_TMP_MEMORY);
    for (size_t i = 0; i < cSegs; i++)
    {
        paSegs[i].pvSeg = va_arg(va, void *);
        paSegs[i].cbSeg = va_arg(va, size_t);
    }

    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, paSegs, cSegs);
    return RTSocketSgWriteNB(hSocket, &SgBuf, pcbWritten);
}


RTDECL(int) RTSocketSelectOne(RTSOCKET hSocket, RTMSINTERVAL cMillies)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRefCount(pThis) >= (pThis->cUsers ? 2U : 1U), VERR_CALLER_NO_REFERENCE);
    int const fdMax = (int)pThis->hNative + 1;
    AssertReturn((RTSOCKETNATIVE)(fdMax - 1) == pThis->hNative, VERR_INTERNAL_ERROR_5);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnselect, VERR_NET_NOT_UNSUPPORTED);
# define select g_pfnselect
#endif

    /*
     * Set up the file descriptor sets and do the select.
     */
    fd_set fdsetR;
    FD_ZERO(&fdsetR);
    FD_SET(pThis->hNative, &fdsetR);

    fd_set fdsetE = fdsetR;

    int rc;
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = select(fdMax, &fdsetR, NULL, &fdsetE, NULL);
    else
    {
        struct timeval timeout;
        timeout.tv_sec = cMillies / 1000;
        timeout.tv_usec = (cMillies % 1000) * 1000;
        rc = select(fdMax, &fdsetR, NULL, &fdsetE, &timeout);
    }
    if (rc > 0)
        rc = VINF_SUCCESS;
    else if (rc == 0)
        rc = VERR_TIMEOUT;
    else
        rc = rtSocketError();

#ifdef RT_OS_WINDOWS
# undef select
#endif
    return rc;
}


/**
 * Internal worker for RTSocketSelectOneEx and rtSocketPollCheck (fallback)
 *
 * @returns IPRT status code
 * @param   pThis               The socket (valid).
 * @param   fEvents             The events to select for.
 * @param   pfEvents            Where to return the events.
 * @param   cMillies            How long to select for, in milliseconds.
 */
static int rtSocketSelectOneEx(RTSOCKET pThis, uint32_t fEvents, uint32_t *pfEvents, RTMSINTERVAL cMillies)
{
    RTSOCKETNATIVE hNative = pThis->hNative;
    if (hNative == NIL_RTSOCKETNATIVE)
    {
        /* Socket is already closed? Possible we raced someone calling rtSocketCloseIt.
           Should we return a different status code? */
        *pfEvents = RTSOCKET_EVT_ERROR;
        return VINF_SUCCESS;
    }

    int const fdMax = (int)hNative + 1;
    AssertReturn((RTSOCKETNATIVE)(fdMax - 1) == hNative, VERR_INTERNAL_ERROR_5);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnselect, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfn__WSAFDIsSet, VERR_NET_NOT_UNSUPPORTED);
# define select         g_pfnselect
# define __WSAFDIsSet   g_pfn__WSAFDIsSet
#endif

    *pfEvents = 0;

    /*
     * Set up the file descriptor sets and do the select.
     */
    fd_set fdsetR;
    fd_set fdsetW;
    fd_set fdsetE;
    FD_ZERO(&fdsetR);
    FD_ZERO(&fdsetW);
    FD_ZERO(&fdsetE);

    if (fEvents & RTSOCKET_EVT_READ)
        FD_SET(hNative, &fdsetR);
    if (fEvents & RTSOCKET_EVT_WRITE)
        FD_SET(hNative, &fdsetW);
    if (fEvents & RTSOCKET_EVT_ERROR)
        FD_SET(hNative, &fdsetE);

    int rc;
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = select(fdMax, &fdsetR, &fdsetW, &fdsetE, NULL);
    else
    {
        struct timeval timeout;
        timeout.tv_sec = cMillies / 1000;
        timeout.tv_usec = (cMillies % 1000) * 1000;
        rc = select(fdMax, &fdsetR, &fdsetW, &fdsetE, &timeout);
    }
    if (rc > 0)
    {
        if (pThis->hNative == hNative)
        {
            if (FD_ISSET(hNative, &fdsetR))
                *pfEvents |= RTSOCKET_EVT_READ;
            if (FD_ISSET(hNative, &fdsetW))
                *pfEvents |= RTSOCKET_EVT_WRITE;
            if (FD_ISSET(hNative, &fdsetE))
                *pfEvents |= RTSOCKET_EVT_ERROR;
            rc = VINF_SUCCESS;
        }
        else
        {
            /* Socket was closed while we waited (rtSocketCloseIt).  Different status code? */
            *pfEvents = RTSOCKET_EVT_ERROR;
            rc = VINF_SUCCESS;
        }
    }
    else if (rc == 0)
        rc = VERR_TIMEOUT;
    else
        rc = rtSocketError();

#ifdef RT_OS_WINDOWS
# undef select
# undef __WSAFDIsSet
#endif
    return rc;
}


RTDECL(int) RTSocketSelectOneEx(RTSOCKET hSocket, uint32_t fEvents, uint32_t *pfEvents, RTMSINTERVAL cMillies)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfEvents, VERR_INVALID_PARAMETER);
    AssertReturn(!(fEvents & ~RTSOCKET_EVT_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(RTMemPoolRefCount(pThis) >= (pThis->cUsers ? 2U : 1U), VERR_CALLER_NO_REFERENCE);

    return rtSocketSelectOneEx(pThis, fEvents, pfEvents, cMillies);
}


RTDECL(int) RTSocketShutdown(RTSOCKET hSocket, bool fRead, bool fWrite)
{
    /*
     * Validate input, don't lock it because we might want to interrupt a call
     * active on a different thread.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRefCount(pThis) >= (pThis->cUsers ? 2U : 1U), VERR_CALLER_NO_REFERENCE);
    AssertReturn(fRead || fWrite, VERR_INVALID_PARAMETER);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnshutdown, VERR_NET_NOT_UNSUPPORTED);
# define shutdown g_pfnshutdown
#endif

    /*
     * Do the job.
     */
    int rc = VINF_SUCCESS;
    int fHow;
    if (fRead && fWrite)
        fHow = SHUT_RDWR;
    else if (fRead)
        fHow = SHUT_RD;
    else
        fHow = SHUT_WR;
    if (shutdown(pThis->hNative, fHow) == -1)
        rc = rtSocketError();

#ifdef RT_OS_WINDOWS
# undef shutdown
#endif
    return rc;
}


RTDECL(int) RTSocketGetLocalAddress(RTSOCKET hSocket, PRTNETADDR pAddr)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRefCount(pThis) >= (pThis->cUsers ? 2U : 1U), VERR_CALLER_NO_REFERENCE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfngetsockname, VERR_NET_NOT_UNSUPPORTED);
# define getsockname g_pfngetsockname
#endif

    /*
     * Get the address and convert it.
     */
    int             rc;
    RTSOCKADDRUNION u;
#ifdef RT_OS_WINDOWS
    int             cbAddr = sizeof(u);
#else
    socklen_t       cbAddr = sizeof(u);
#endif
    RT_ZERO(u);
    if (getsockname(pThis->hNative, &u.Addr, &cbAddr) == 0)
        rc = rtSocketNetAddrFromAddr(&u, cbAddr, pAddr);
    else
        rc = rtSocketError();

#ifdef RT_OS_WINDOWS
# undef getsockname
#endif
    return rc;
}


RTDECL(int) RTSocketGetPeerAddress(RTSOCKET hSocket, PRTNETADDR pAddr)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTMemPoolRefCount(pThis) >= (pThis->cUsers ? 2U : 1U), VERR_CALLER_NO_REFERENCE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfngetpeername, VERR_NET_NOT_UNSUPPORTED);
# define getpeername g_pfngetpeername
#endif

    /*
     * Get the address and convert it.
     */
    int             rc;
    RTSOCKADDRUNION u;
#ifdef RT_OS_WINDOWS
    int             cbAddr = sizeof(u);
#else
    socklen_t       cbAddr = sizeof(u);
#endif
    RT_ZERO(u);
    if (getpeername(pThis->hNative, &u.Addr, &cbAddr) == 0)
        rc = rtSocketNetAddrFromAddr(&u, cbAddr, pAddr);
    else
        rc = rtSocketError();

#ifdef RT_OS_WINDOWS
# undef getpeername
#endif
    return rc;
}



/**
 * Wrapper around bind.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle.
 * @param   pAddr               The address to bind to.
 */
DECLHIDDEN(int) rtSocketBind(RTSOCKET hSocket, PCRTNETADDR pAddr)
{
    RTSOCKADDRUNION u;
    int             cbAddr;
    int rc = rtSocketAddrFromNetAddr(pAddr, &u, sizeof(u), &cbAddr);
    if (RT_SUCCESS(rc))
        rc = rtSocketBindRawAddr(hSocket, &u.Addr, cbAddr);
    return rc;
}


/**
 * Very thin wrapper around bind.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle.
 * @param   pvAddr              The address to bind to (struct sockaddr and
 *                              friends).
 * @param   cbAddr              The size of the address.
 */
DECLHIDDEN(int) rtSocketBindRawAddr(RTSOCKET hSocket, void const *pvAddr, size_t cbAddr)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvAddr, VERR_INVALID_POINTER);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnbind, VERR_NET_NOT_UNSUPPORTED);
# define bind g_pfnbind
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc;
    if (bind(pThis->hNative, (struct sockaddr const *)pvAddr, (int)cbAddr) == 0)
        rc = VINF_SUCCESS;
    else
        rc = rtSocketError();

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef bind
#endif
    return rc;
}



/**
 * Wrapper around listen.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle.
 * @param   cMaxPending         The max number of pending connections.
 */
DECLHIDDEN(int) rtSocketListen(RTSOCKET hSocket, int cMaxPending)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnlisten, VERR_NET_NOT_UNSUPPORTED);
# define listen g_pfnlisten
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = VINF_SUCCESS;
    if (listen(pThis->hNative, cMaxPending) != 0)
        rc = rtSocketError();

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef listen
#endif
    return rc;
}


/**
 * Wrapper around accept.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle.
 * @param   phClient            Where to return the client socket handle on
 *                              success.
 * @param   pAddr               Where to return the client address.
 * @param   pcbAddr             On input this gives the size buffer size of what
 *                              @a pAddr point to.  On return this contains the
 *                              size of what's stored at @a pAddr.
 */
DECLHIDDEN(int) rtSocketAccept(RTSOCKET hSocket, PRTSOCKET phClient, struct sockaddr *pAddr, size_t *pcbAddr)
{
    /*
     * Validate input.
     * Only lock the socket temporarily while we get the native handle, so that
     * we can safely shutdown and destroy the socket from a different thread.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnaccept, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnclosesocket, VERR_NET_NOT_UNSUPPORTED);
# define accept g_pfnaccept
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    /*
     * Call accept().
     */
    rtSocketErrorReset();
    int         rc      = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    int         cbAddr  = (int)*pcbAddr;
#else
    socklen_t   cbAddr  = *pcbAddr;
#endif
    RTSOCKETNATIVE hNativeClient = accept(pThis->hNative, pAddr, &cbAddr);
    if (hNativeClient != NIL_RTSOCKETNATIVE)
    {
        *pcbAddr = cbAddr;

        /*
         * Wrap the client socket.
         */
        rc = rtSocketCreateForNative(phClient, hNativeClient, false /*fLeaveOpen*/);
        if (RT_FAILURE(rc))
        {
#ifdef RT_OS_WINDOWS
            g_pfnclosesocket(hNativeClient);
#else
            close(hNativeClient);
#endif
        }
    }
    else
        rc = rtSocketError();

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef accept
#endif
    return rc;
}


/**
 * Wrapper around connect.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle.
 * @param   pAddr               The socket address to connect to.
 * @param   cMillies            Number of milliseconds to wait for the connect attempt to complete.
 *                              Use RT_INDEFINITE_WAIT to wait for ever.
 *                              Use RT_TCPCLIENTCONNECT_DEFAULT_WAIT to wait for the default time
 *                              configured on the running system.
 */
DECLHIDDEN(int) rtSocketConnect(RTSOCKET hSocket, PCRTNETADDR pAddr, RTMSINTERVAL cMillies)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnconnect, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfnselect, VERR_NET_NOT_UNSUPPORTED);
    AssertReturn(g_pfngetsockopt, VERR_NET_NOT_UNSUPPORTED);
# define connect        g_pfnconnect
# define select         g_pfnselect
# define getsockopt     g_pfngetsockopt
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    RTSOCKADDRUNION u;
    int             cbAddr;
    int rc = rtSocketAddrFromNetAddr(pAddr, &u, sizeof(u), &cbAddr);
    if (RT_SUCCESS(rc))
    {
        if (cMillies == RT_SOCKETCONNECT_DEFAULT_WAIT)
        {
            if (connect(pThis->hNative, &u.Addr, cbAddr) != 0)
                rc = rtSocketError();
        }
        else
        {
            /*
             * Switch the socket to nonblocking mode, initiate the connect
             * and wait for the socket to become writable or until the timeout
             * expires.
             */
            rc = rtSocketSwitchBlockingMode(pThis, false /* fBlocking */);
            if (RT_SUCCESS(rc))
            {
                if (connect(pThis->hNative, &u.Addr, cbAddr) != 0)
                {
                    rc = rtSocketError();
                    if (rc == VERR_TRY_AGAIN || rc == VERR_NET_IN_PROGRESS)
                    {
                        int rcSock = 0;
                        fd_set FdSetWriteable;
                        struct timeval TvTimeout;

                        TvTimeout.tv_sec = cMillies / RT_MS_1SEC;
                        TvTimeout.tv_usec = (cMillies % RT_MS_1SEC) * RT_US_1MS;

                        FD_ZERO(&FdSetWriteable);
                        FD_SET(pThis->hNative, &FdSetWriteable);
                        do
                        {
                            rcSock = select(pThis->hNative + 1, NULL, &FdSetWriteable, NULL,
                                              cMillies == RT_INDEFINITE_WAIT || cMillies >= INT_MAX
                                            ? NULL
                                            : &TvTimeout);
                            if (rcSock > 0)
                            {
                                int iSockError = 0;
                                socklen_t cbSockOpt = sizeof(iSockError);
                                rcSock = getsockopt(pThis->hNative, SOL_SOCKET, SO_ERROR, (char *)&iSockError, &cbSockOpt);
                                if (rcSock == 0)
                                {
                                    if (iSockError == 0)
                                        rc = VINF_SUCCESS;
                                    else
                                    {
#ifdef RT_OS_WINDOWS
                                        rc = RTErrConvertFromWin32(iSockError);
#else
                                        rc = RTErrConvertFromErrno(iSockError);
#endif
                                    }
                                }
                                else
                                    rc = rtSocketError();
                            }
                            else if (rcSock == 0)
                                rc = VERR_TIMEOUT;
                            else
                                rc = rtSocketError();
                        } while (rc == VERR_INTERRUPTED);
                    }
                }

                rtSocketSwitchBlockingMode(pThis, true /* fBlocking */);
            }
        }
    }

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef connect
# undef select
# undef getsockopt
#endif
    return rc;
}


/**
 * Wrapper around connect, raw address, no timeout.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle.
 * @param   pvAddr              The raw socket address to connect to.
 * @param   cbAddr              The size of the raw address.
 */
DECLHIDDEN(int) rtSocketConnectRaw(RTSOCKET hSocket, void const *pvAddr, size_t cbAddr)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnconnect, VERR_NET_NOT_UNSUPPORTED);
# define connect        g_pfnconnect
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc;
    if (connect(pThis->hNative, (const struct sockaddr *)pvAddr, (int)cbAddr) == 0)
        rc = VINF_SUCCESS;
    else
        rc = rtSocketError();

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef connect
#endif
    return rc;
}


/**
 * Wrapper around setsockopt.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle.
 * @param   iLevel              The protocol level, e.g. IPPORTO_TCP.
 * @param   iOption             The option, e.g. TCP_NODELAY.
 * @param   pvValue             The value buffer.
 * @param   cbValue             The size of the value pointed to by pvValue.
 */
DECLHIDDEN(int) rtSocketSetOpt(RTSOCKET hSocket, int iLevel, int iOption, void const *pvValue, int cbValue)
{
    /*
     * Validate input.
     */
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(g_pfnsetsockopt, VERR_NET_NOT_UNSUPPORTED);
# define setsockopt g_pfnsetsockopt
#endif
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = VINF_SUCCESS;
    if (setsockopt(pThis->hNative, iLevel, iOption, (const char *)pvValue, cbValue) != 0)
        rc = rtSocketError();

    rtSocketUnlock(pThis);
#ifdef RT_OS_WINDOWS
# undef setsockopt
#endif
    return rc;
}


/**
 * Internal RTPollSetAdd helper that returns the handle that should be added to
 * the pollset.
 *
 * @returns Valid handle on success, INVALID_HANDLE_VALUE on failure.
 * @param   hSocket             The socket handle.
 * @param   fEvents             The events we're polling for.
 * @param   phNative            Where to put the primary handle.
 */
DECLHIDDEN(int) rtSocketPollGetHandle(RTSOCKET hSocket, uint32_t fEvents, PRTHCINTPTR phNative)
{
    RTSOCKETINT *pThis = hSocket;
    RT_NOREF_PV(fEvents);
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_OS_WINDOWS
    AssertReturn(rtSocketTryLock(pThis), VERR_CONCURRENT_ACCESS);

    int rc = VINF_SUCCESS;
    if (pThis->hEvent != WSA_INVALID_EVENT)
        *phNative = (RTHCINTPTR)pThis->hEvent;
    else if (g_pfnWSACreateEvent)
    {
        pThis->hEvent = g_pfnWSACreateEvent();
        *phNative = (RTHCINTPTR)pThis->hEvent;
        if (pThis->hEvent == WSA_INVALID_EVENT)
            rc = rtSocketError();
    }
    else
    {
        AssertCompile(WSA_INVALID_EVENT == (WSAEVENT)NULL);
        pThis->hEvent = CreateEventW(NULL, TRUE /*fManualReset*/, FALSE /*fInitialState*/,  NULL /*pwszName*/);
        *phNative = (RTHCINTPTR)pThis->hEvent;
        if (pThis->hEvent == WSA_INVALID_EVENT)
            rc = RTErrConvertFromWin32(GetLastError());
    }

    rtSocketUnlock(pThis);
    return rc;

#else  /* !RT_OS_WINDOWS */
    *phNative = (RTHCUINTPTR)pThis->hNative;
    return VINF_SUCCESS;
#endif /* !RT_OS_WINDOWS */
}

#ifdef RT_OS_WINDOWS

/**
 * Fallback poller thread.
 *
 * @returns VINF_SUCCESS.
 * @param   hSelf               The thread handle.
 * @param   pvUser              Socket instance data.
 */
static DECLCALLBACK(int) rtSocketPollFallbackThreadProc(RTTHREAD hSelf, void *pvUser)
{
    RTSOCKETINT *pThis = (RTSOCKETINT *)pvUser;
    RT_NOREF(hSelf);
# define __WSAFDIsSet g_pfn__WSAFDIsSet

    /*
     * The execution loop.
     */
    while (!ASMAtomicReadBool(&pThis->fPollFallbackShutdown))
    {
        /*
         * Do the selecting (with a 15 second timeout because that seems like a good idea).
         */
        struct fd_set SetRead;
        struct fd_set SetWrite;
        struct fd_set SetXcpt;

        FD_ZERO(&SetRead);
        FD_ZERO(&SetWrite);
        FD_ZERO(&SetXcpt);

        FD_SET(pThis->hPollFallbackNotifyR, &SetRead);
        FD_SET(pThis->hPollFallbackNotifyR, &SetXcpt);

        bool     fActive = ASMAtomicReadBool(&pThis->fPollFallbackActive);
        uint32_t fEvents;
        if (!fActive)
            fEvents = 0;
        else
        {
            fEvents = ASMAtomicReadU32(&pThis->fSubscribedEvts);
            if (fEvents & RTPOLL_EVT_READ)
                FD_SET(pThis->hNative, &SetRead);
            if (fEvents & RTPOLL_EVT_WRITE)
                FD_SET(pThis->hNative, &SetWrite);
            if (fEvents & RTPOLL_EVT_ERROR)
                FD_SET(pThis->hNative, &SetXcpt);
        }

        struct timeval Timeout;
        Timeout.tv_sec  = 15;
        Timeout.tv_usec = 0;
        int rc = g_pfnselect(INT_MAX /*ignored*/, &SetRead, &SetWrite, &SetXcpt, &Timeout);

        /* Stop immediately if told to shut down. */
        if (ASMAtomicReadBool(&pThis->fPollFallbackShutdown))
            break;

        /*
         * Process the result.
         */
        if (rc > 0)
        {
            /* First the socket we're listening on. */
            if (   fEvents
                && (   FD_ISSET(pThis->hNative, &SetRead)
                    || FD_ISSET(pThis->hNative, &SetWrite)
                    || FD_ISSET(pThis->hNative, &SetXcpt)) )
            {
                ASMAtomicWriteBool(&pThis->fPollFallbackActive, false);
                SetEvent(pThis->hEvent);
            }

            /* Then maintain the notification pipe.  (We only read one byte here
               because we're overly paranoid wrt socket switching to blocking mode.) */
            if (FD_ISSET(pThis->hPollFallbackNotifyR, &SetRead))
            {
                char chIgnored;
                g_pfnrecv(pThis->hPollFallbackNotifyR, &chIgnored, sizeof(chIgnored), MSG_NOSIGNAL);
            }
        }
        else
            AssertMsg(rc == 0, ("%Rrc\n", rtSocketError()));
    }

# undef __WSAFDIsSet
    return VINF_SUCCESS;
}


/**
 * Pokes the fallback thread, making sure it gets out of whatever it's stuck in.
 *
 * @param   pThis               The socket handle.
 */
static void rtSocketPokePollFallbackThread(RTSOCKETINT *pThis)
{
    Assert(pThis->fPollFallback);
    if (pThis->hPollFallbackThread != NIL_RTTHREAD)
    {
        int cbWritten = g_pfnsend(pThis->hPollFallbackNotifyW, "!", 1, MSG_NOSIGNAL);
        AssertMsg(cbWritten == 1, ("cbWritten=%d err=%Rrc\n",  rtSocketError()));
        RT_NOREF_PV(cbWritten);
    }
}


/**
 * Called by rtSocketPollStart to make the thread start selecting on the socket.
 *
 * @returns 0 on success, RTPOLL_EVT_ERROR on failure.
 * @param   pThis               The socket handle.
 */
static uint32_t rtSocketPollFallbackStart(RTSOCKETINT *pThis)
{
    /*
     * Reset the event and tell the thread to start selecting on the socket.
     */
    ResetEvent(pThis->hEvent);
    ASMAtomicWriteBool(&pThis->fPollFallbackActive, true);

    /*
     * Wake up the thread the thread.
     */
    if (pThis->hPollFallbackThread != NIL_RTTHREAD)
        rtSocketPokePollFallbackThread(pThis);
    else
    {
        /*
         * Not running, need to set it up and start it.
         */
        AssertLogRelReturn(pThis->hEvent != NULL && pThis->hEvent != INVALID_HANDLE_VALUE, RTPOLL_EVT_ERROR);

        /* Create the notification socket pair. */
        int rc;
        if (pThis->hPollFallbackNotifyR == NIL_RTSOCKETNATIVE)
        {
            rc = rtSocketCreateNativeTcpPair(&pThis->hPollFallbackNotifyW, &pThis->hPollFallbackNotifyR);
            AssertLogRelRCReturn(rc, RTPOLL_EVT_ERROR);

            /* Make the read end non-blocking (not fatal). */
            u_long fNonBlocking = 1;
            rc = g_pfnioctlsocket(pThis->hPollFallbackNotifyR, FIONBIO, &fNonBlocking);
            AssertLogRelMsg(rc == 0,  ("rc=%#x %Rrc\n", rc, rtSocketError()));
        }

        /* Finally, start the thread.  ASSUME we don't need too much stack. */
        rc = RTThreadCreate(&pThis->hPollFallbackThread, rtSocketPollFallbackThreadProc, pThis,
                            _128K, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "sockpoll");
        AssertLogRelRCReturn(rc, RTPOLL_EVT_ERROR);
    }
    return 0;
}


/**
 * Undos the harm done by WSAEventSelect.
 *
 * @returns IPRT status code.
 * @param   pThis               The socket handle.
 */
static int rtSocketPollClearEventAndRestoreBlocking(RTSOCKETINT *pThis)
{
    int rc = VINF_SUCCESS;
    if (pThis->fSubscribedEvts)
    {
        if (!pThis->fPollFallback)
        {
            Assert(g_pfnWSAEventSelect && g_pfnioctlsocket);
            if (g_pfnWSAEventSelect && g_pfnioctlsocket)
            {
                if (g_pfnWSAEventSelect(pThis->hNative, WSA_INVALID_EVENT, 0) == 0)
                {
                    pThis->fSubscribedEvts = 0;

                    /*
                     * Switch back to blocking mode if that was the state before the
                     * operation.
                     */
                    if (pThis->fBlocking)
                    {
                        u_long fNonBlocking = 0;
                        int rc2 = g_pfnioctlsocket(pThis->hNative, FIONBIO, &fNonBlocking);
                        if (rc2 != 0)
                        {
                            rc = rtSocketError();
                            AssertMsgFailed(("%Rrc; rc2=%d\n", rc, rc2));
                        }
                    }
                }
                else
                {
                    rc = rtSocketError();
                    AssertMsgFailed(("%Rrc\n", rc));
                }
            }
            else
            {
                Assert(pThis->fPollFallback);
                rc = VINF_SUCCESS;
            }
        }
        /*
         * Just clear the event mask as we never started waiting if we get here.
         */
        else
            ASMAtomicWriteU32(&pThis->fSubscribedEvts, 0);
    }
    return rc;
}


/**
 * Updates the mask of events we're subscribing to.
 *
 * @returns IPRT status code.
 * @param   pThis               The socket handle.
 * @param   fEvents             The events we want to subscribe to.
 */
static int rtSocketPollUpdateEvents(RTSOCKETINT *pThis, uint32_t fEvents)
{
    if (!pThis->fPollFallback)
    {
        LONG fNetworkEvents = 0;
        if (fEvents & RTPOLL_EVT_READ)
            fNetworkEvents |= FD_READ;
        if (fEvents & RTPOLL_EVT_WRITE)
            fNetworkEvents |= FD_WRITE;
        if (fEvents & RTPOLL_EVT_ERROR)
            fNetworkEvents |= FD_CLOSE;
        LogFlowFunc(("fNetworkEvents=%#x\n", fNetworkEvents));

        if (g_pfnWSAEventSelect(pThis->hNative, pThis->hEvent, fNetworkEvents) == 0)
        {
            pThis->fSubscribedEvts = fEvents;
            return VINF_SUCCESS;
        }

        int rc = rtSocketError();
        AssertMsgFailed(("fNetworkEvents=%#x rc=%Rrc\n", fNetworkEvents, rtSocketError()));
        return rc;
    }

    /*
     * Update the events we're waiting for.  Caller will poke/start the thread. later
     */
    ASMAtomicWriteU32(&pThis->fSubscribedEvts, fEvents);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtSocketSetKeepAlive(RTSOCKET hSocket, bool fEnable, uint32_t cSecsIdle, uint32_t cSecsInterval)
{
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, UINT32_MAX);

    if (!g_pfnWSAIoctl)
        return VERR_NET_NOT_UNSUPPORTED;

    struct tcp_keepalive keepAliveOptions;
    DWORD dwBytesReturned;

    keepAliveOptions.onoff = fEnable ? 1 : 0;
    keepAliveOptions.keepalivetime = cSecsIdle * 1000;
    keepAliveOptions.keepaliveinterval = cSecsInterval * 1000;

    if (g_pfnWSAIoctl(pThis->hNative,
                      SIO_KEEPALIVE_VALS,
                      &keepAliveOptions, sizeof(keepAliveOptions),
                      NULL, 0, &dwBytesReturned, NULL, NULL) == 0)
        return VINF_SUCCESS;

    int rc = rtSocketError();
    AssertMsgFailed(("WSAIoctl(.., SIO_KEEPALIVE_VALS, ...) failed: rc=%Rrc\n", rc));
    return rc;
}

#endif  /* RT_OS_WINDOWS */


#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)

/**
 * Checks for pending events.
 *
 * @returns Event mask or 0.
 * @param   pThis               The socket handle.
 * @param   fEvents             The desired events.
 */
static uint32_t rtSocketPollCheck(RTSOCKETINT *pThis, uint32_t fEvents)
{
    uint32_t fRetEvents = 0;

    LogFlowFunc(("pThis=%#p fEvents=%#x\n", pThis, fEvents));

# ifdef RT_OS_WINDOWS
    /* Make sure WSAEnumNetworkEvents returns what we want. */
    int rc = VINF_SUCCESS;
    if ((pThis->fSubscribedEvts & fEvents) != fEvents)
        rc = rtSocketPollUpdateEvents(pThis, pThis->fSubscribedEvts | fEvents);

    if (!pThis->fPollFallback)
    {
        /* Atomically get pending events and reset the event semaphore. */
        Assert(g_pfnWSAEnumNetworkEvents);
        WSANETWORKEVENTS NetEvts;
        RT_ZERO(NetEvts);
        if (g_pfnWSAEnumNetworkEvents(pThis->hNative, pThis->hEvent, &NetEvts) == 0)
        {
            if (   (NetEvts.lNetworkEvents & FD_READ)
                && NetEvts.iErrorCode[FD_READ_BIT] == 0)
                fRetEvents |= RTPOLL_EVT_READ;

            if (   (NetEvts.lNetworkEvents & FD_WRITE)
                && NetEvts.iErrorCode[FD_WRITE_BIT] == 0)
                fRetEvents |= RTPOLL_EVT_WRITE;

            if (NetEvts.lNetworkEvents & FD_CLOSE)
                fRetEvents |= RTPOLL_EVT_ERROR;
            else
                for (uint32_t i = 0; i < FD_MAX_EVENTS; i++)
                    if (   (NetEvts.lNetworkEvents & (1L << i))
                        && NetEvts.iErrorCode[i] != 0)
                        fRetEvents |= RTPOLL_EVT_ERROR;

            pThis->fEventsSaved = fRetEvents |= pThis->fEventsSaved;
            fRetEvents &= fEvents | RTPOLL_EVT_ERROR;
        }
        else
            rc = rtSocketError();
    }

    /* Fall back on select if we hit an error above or is using fallback polling. */
    if (pThis->fPollFallback || RT_FAILURE(rc))
    {
        rc = rtSocketSelectOneEx(pThis, fEvents & RTPOLL_EVT_ERROR ? fEvents | RTPOLL_EVT_READ : fEvents, &fRetEvents, 0);
        if (RT_SUCCESS(rc))
        {
            /* rtSocketSelectOneEx may return RTPOLL_EVT_READ on disconnect.  Use
               getpeername to fix this. */
            if ((fRetEvents & (RTPOLL_EVT_READ | RTPOLL_EVT_ERROR)) == RTPOLL_EVT_READ)
            {
# if 0 /* doens't work */
                rtSocketErrorReset();
                char chIgn;
                rc = g_pfnrecv(pThis->hNative, &chIgn, 0, MSG_NOSIGNAL);
                rc = rtSocketError();
                if (RT_FAILURE(rc))
                    fRetEvents |= RTPOLL_EVT_ERROR;

                rc = g_pfnsend(pThis->hNative, &chIgn, 0, MSG_NOSIGNAL);
                rc = rtSocketError();
                if (RT_FAILURE(rc))
                    fRetEvents |= RTPOLL_EVT_ERROR;

                RTSOCKADDRUNION u;
                int cbAddr = sizeof(u);
                if (g_pfngetpeername(pThis->hNative, &u.Addr, &cbAddr) == SOCKET_ERROR)
                    fRetEvents |= RTPOLL_EVT_ERROR;
# endif
                /* If no bytes are available, assume error condition. */
                u_long cbAvail = 0;
                rc = g_pfnioctlsocket(pThis->hNative, FIONREAD, &cbAvail);
                if (rc == 0 && cbAvail == 0)
                    fRetEvents |= RTPOLL_EVT_ERROR;
            }
            fRetEvents &= fEvents | RTPOLL_EVT_ERROR;
        }
        else if (rc == VERR_TIMEOUT)
            fRetEvents = 0;
        else
            fRetEvents |= RTPOLL_EVT_ERROR;
    }

# else  /* RT_OS_OS2 */
    int aFds[4] = { pThis->hNative, pThis->hNative, pThis->hNative, -1 };
    int rc = os2_select(aFds, 1, 1, 1, 0);
    if (rc > 0)
    {
        if (aFds[0] == pThis->hNative)
            fRetEvents |= RTPOLL_EVT_READ;
        if (aFds[1] == pThis->hNative)
            fRetEvents |= RTPOLL_EVT_WRITE;
        if (aFds[2] == pThis->hNative)
            fRetEvents |= RTPOLL_EVT_ERROR;
        fRetEvents &= fEvents;
    }
# endif /* RT_OS_OS2 */

    LogFlowFunc(("fRetEvents=%#x\n", fRetEvents));
    return fRetEvents;
}


/**
 * Internal RTPoll helper that polls the socket handle and, if @a fNoWait is
 * clear, starts whatever actions we've got running during the poll call.
 *
 * @returns 0 if no pending events, actions initiated if @a fNoWait is clear.
 *          Event mask (in @a fEvents) and no actions if the handle is ready
 *          already.
 *          UINT32_MAX (asserted) if the socket handle is busy in I/O or a
 *          different poll set.
 *
 * @param   hSocket             The socket handle.
 * @param   hPollSet            The poll set handle (for access checks).
 * @param   fEvents             The events we're polling for.
 * @param   fFinalEntry         Set if this is the final entry for this handle
 *                              in this poll set.  This can be used for dealing
 *                              with duplicate entries.
 * @param   fNoWait             Set if it's a zero-wait poll call.  Clear if
 *                              we'll wait for an event to occur.
 *
 * @remarks There is a potential race wrt duplicate handles when @a fNoWait is
 *          @c true, we don't currently care about that oddity...
 */
DECLHIDDEN(uint32_t) rtSocketPollStart(RTSOCKET hSocket, RTPOLLSET hPollSet, uint32_t fEvents, bool fFinalEntry, bool fNoWait)
{
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, UINT32_MAX);
    /** @todo This isn't quite sane. Replace by critsect and open up concurrent
     *        reads and writes! */
    if (rtSocketTryLock(pThis))
        pThis->hPollSet = hPollSet;
    else
    {
        AssertReturn(pThis->hPollSet == hPollSet, UINT32_MAX);
        ASMAtomicIncU32(&pThis->cUsers);
    }

    /* (rtSocketPollCheck will reset the event object). */
# ifdef RT_OS_WINDOWS
    uint32_t fRetEvents = pThis->fEventsSaved;
    pThis->fEventsSaved = 0; /* Reset */
    fRetEvents |= rtSocketPollCheck(pThis, fEvents);

    if (   !fRetEvents
        && !fNoWait)
    {
        pThis->fPollEvts |= fEvents;
        if (fFinalEntry)
        {
            if (pThis->fSubscribedEvts != pThis->fPollEvts)
            {
                /** @todo seems like there might be a call to many here and that fPollEvts is
                 *        totally unnecessary... (bird) */
                int rc = rtSocketPollUpdateEvents(pThis, pThis->fPollEvts);
                if (RT_FAILURE(rc))
                {
                    pThis->fPollEvts = 0;
                    fRetEvents       = UINT32_MAX;
                }
            }

            /* Make sure we don't block when there are events pending relevant to an earlier poll set entry. */
            if (pThis->fEventsSaved && !pThis->fPollFallback && g_pfnWSASetEvent && fRetEvents == 0)
                g_pfnWSASetEvent(pThis->hEvent);
        }
    }
# else
    uint32_t fRetEvents = rtSocketPollCheck(pThis, fEvents);
# endif

    if (fRetEvents || fNoWait)
    {
        if (pThis->cUsers == 1)
        {
# ifdef RT_OS_WINDOWS
            pThis->fEventsSaved    &= RTPOLL_EVT_ERROR;
            pThis->fHarvestedEvents = false;
            rtSocketPollClearEventAndRestoreBlocking(pThis);
# endif
            pThis->hPollSet = NIL_RTPOLLSET;
        }
# ifdef RT_OS_WINDOWS
        else
            pThis->fHarvestedEvents = true;
# endif
        ASMAtomicDecU32(&pThis->cUsers);
    }
# ifdef RT_OS_WINDOWS
    /*
     * Kick the poller thread on if this is the final entry and we're in
     * winsock 1.x fallback mode.
     */
    else if (pThis->fPollFallback && fFinalEntry)
        fRetEvents = rtSocketPollFallbackStart(pThis);
# endif

    return fRetEvents;
}


/**
 * Called after a WaitForMultipleObjects returned in order to check for pending
 * events and stop whatever actions that rtSocketPollStart() initiated.
 *
 * @returns Event mask or 0.
 *
 * @param   hSocket             The socket handle.
 * @param   fEvents             The events we're polling for.
 * @param   fFinalEntry         Set if this is the final entry for this handle
 *                              in this poll set.  This can be used for dealing
 *                              with duplicate entries.  Only keep in mind that
 *                              this method is called in reverse order, so the
 *                              first call will have this set (when the entire
 *                              set was processed).
 * @param   fHarvestEvents      Set if we should check for pending events.
 */
DECLHIDDEN(uint32_t) rtSocketPollDone(RTSOCKET hSocket, uint32_t fEvents, bool fFinalEntry, bool fHarvestEvents)
{
    RTSOCKETINT *pThis = hSocket;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTSOCKET_MAGIC, 0);
    Assert(pThis->cUsers > 0);
    Assert(pThis->hPollSet != NIL_RTPOLLSET);
    RT_NOREF_PV(fFinalEntry);

# ifdef RT_OS_WINDOWS
    /*
     * Deactivate the poll thread if we're in winsock 1.x fallback poll mode.
     */
    if (   pThis->fPollFallback
        && pThis->hPollFallbackThread != NIL_RTTHREAD)
    {
        ASMAtomicWriteU32(&pThis->fSubscribedEvts, 0);
        if (ASMAtomicXchgBool(&pThis->fPollFallbackActive, false))
            rtSocketPokePollFallbackThread(pThis);
    }
# endif

    /*
     * Harvest events and clear the event mask for the next round of polling.
     */
    uint32_t fRetEvents;
# ifdef RT_OS_WINDOWS
    if (!pThis->fPollFallback)
    {
        if (!pThis->fHarvestedEvents)
        {
            fRetEvents = rtSocketPollCheck(pThis, fEvents);
            pThis->fHarvestedEvents = true;
        }
        else
            fRetEvents = pThis->fEventsSaved;
        if (fHarvestEvents)
            fRetEvents &= fEvents;
        else
            fRetEvents = 0;
        pThis->fPollEvts = 0;
    }
    else
# endif
    {
        if (fHarvestEvents)
            fRetEvents = rtSocketPollCheck(pThis, fEvents);
        else
            fRetEvents = 0;
    }

    /*
     * Make the socket blocking again and unlock the handle.
     */
    if (pThis->cUsers == 1)
    {
# ifdef RT_OS_WINDOWS
        pThis->fEventsSaved    &= RTPOLL_EVT_ERROR;
        pThis->fHarvestedEvents = false;
        rtSocketPollClearEventAndRestoreBlocking(pThis);
# endif
        pThis->hPollSet = NIL_RTPOLLSET;
    }
    ASMAtomicDecU32(&pThis->cUsers);
    return fRetEvents;
}

#endif /* RT_OS_WINDOWS || RT_OS_OS2 */

