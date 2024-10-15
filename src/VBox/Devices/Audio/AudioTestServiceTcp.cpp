/* $Id: AudioTestServiceTcp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * AudioTestServiceTcp - Audio test execution server, TCP/IP Transport Layer.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_AUDIO_TEST
#include <iprt/log.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/tcp.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <VBox/log.h>

#include "AudioTestService.h"
#include "AudioTestServiceInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * TCP specific client data.
 */
typedef struct ATSTRANSPORTCLIENT
{
    /** Socket of the current client. */
    RTSOCKET                           hTcpClient;
    /** Indicates whether \a hTcpClient comes from the server or from a client
     * connect (relevant when closing it). */
    bool                               fFromServer;
    /** The size of the stashed data. */
    size_t                             cbTcpStashed;
    /** The size of the stashed data allocation. */
    size_t                             cbTcpStashedAlloced;
    /** The stashed data. */
    uint8_t                           *pbTcpStashed;
} ATSTRANSPORTCLIENT;

/**
 * Structure for keeping Audio Test Service (ATS) transport instance-specific data.
 */
typedef struct ATSTRANSPORTINST
{
    /** Critical section for serializing access. */
    RTCRITSECT                         CritSect;
    /** Connection mode to use. */
    ATSCONNMODE                        enmConnMode;
    /** The addresses to bind to.  Empty string means any. */
    char                               szBindAddr[256];
    /** The TCP port to listen to. */
    uint32_t                           uBindPort;
    /** The addresses to connect to if running in reversed (VM NATed) mode. */
    char                               szConnectAddr[256];
    /** The TCP port to connect to if running in reversed (VM NATed) mode. */
    uint32_t                           uConnectPort;
    /** Pointer to the TCP server instance. */
    PRTTCPSERVER                       pTcpServer;
    /** Thread calling RTTcpServerListen2. */
    RTTHREAD                           hThreadServer;
    /** Thread calling RTTcpClientConnect. */
    RTTHREAD                           hThreadConnect;
    /** The main thread handle (for signalling). */
    RTTHREAD                           hThreadMain;
    /** Stop connecting attempts when set. */
    bool                               fStopConnecting;
    /** Connect cancel cookie. */
    PRTTCPCLIENTCONNECTCANCEL volatile pConnectCancelCookie;
} ATSTRANSPORTINST;
/** Pointer to an Audio Test Service (ATS) TCP/IP transport instance. */
typedef ATSTRANSPORTINST *PATSTRANSPORTINST;

/**
 * Structure holding an ATS connection context, which is
 * required when connecting a client via server (listening) or client (connecting).
 */
typedef struct ATSCONNCTX
{
    /** Pointer to transport instance to use. */
    PATSTRANSPORTINST                  pInst;
    /** Pointer to transport client to connect. */
    PATSTRANSPORTCLIENT                pClient;
    /** Connection timeout (in ms).
     *  Use RT_INDEFINITE_WAIT to wait indefinitely. */
    uint32_t                           msTimeout;
} ATSCONNCTX;
/** Pointer to an Audio Test Service (ATS) TCP/IP connection context. */
typedef ATSCONNCTX *PATSCONNCTX;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Disconnects the current client and frees all stashed data.
 *
 * @param   pThis           Transport instance.
 * @param   pClient         Client to disconnect.
 */
static void atsTcpDisconnectClient(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient)
{
    RT_NOREF(pThis);

    LogRelFlowFunc(("pClient=%RTsock\n", pClient->hTcpClient));

    if (pClient->hTcpClient != NIL_RTSOCKET)
    {
        LogRelFlowFunc(("%RTsock\n", pClient->hTcpClient));

        int rc;
        if (pClient->fFromServer)
            rc = RTTcpServerDisconnectClient2(pClient->hTcpClient);
        else
            rc = RTTcpClientClose(pClient->hTcpClient);
        pClient->hTcpClient = NIL_RTSOCKET;
        AssertRCSuccess(rc);
    }

    if (pClient->pbTcpStashed)
    {
        RTMemFree(pClient->pbTcpStashed);
        pClient->pbTcpStashed = NULL;
    }
}

/**
 * Free's a client.
 *
 * @param   pThis           Transport instance.
 * @param   pClient         Client to free.
 *                          The pointer will be invalid after calling.
 */
static void atsTcpFreeClient(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient)
{
    if (!pClient)
        return;

    /* Make sure to disconnect first. */
    atsTcpDisconnectClient(pThis, pClient);

    RTMemFree(pClient);
    pClient = NULL;
}

/**
 * Sets the current client socket in a safe manner.
 *
 * @returns NIL_RTSOCKET if consumed, otherwise hTcpClient.
 * @param   pThis           Transport instance.
 * @param   pClient         Client to set the socket for.
 * @param   fFromServer     Whether the socket is from a server (listening) or client (connecting) call.
 *                          Important when closing / disconnecting.
 * @param   hTcpClient      The client socket.
 */
static RTSOCKET atsTcpSetClient(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient, bool fFromServer, RTSOCKET hTcpClient)
{
    RTCritSectEnter(&pThis->CritSect);
    if (   pClient->hTcpClient  == NIL_RTSOCKET
        && !pThis->fStopConnecting)
    {
        LogRelFlowFunc(("New client %RTsock connected (fFromServer=%RTbool)\n", hTcpClient, fFromServer));

        pClient->fFromServer = fFromServer;
        pClient->hTcpClient = hTcpClient;
        hTcpClient = NIL_RTSOCKET; /* Invalidate, as pClient has now ownership. */
    }
    RTCritSectLeave(&pThis->CritSect);
    return hTcpClient;
}

/**
 * Checks if it's a fatal RTTcpClientConnect return code.
 *
 * @returns true / false.
 * @param   rc              The IPRT status code.
 */
static bool atsTcpIsFatalClientConnectStatus(int rc)
{
    return rc != VERR_NET_UNREACHABLE
        && rc != VERR_NET_HOST_DOWN
        && rc != VERR_NET_HOST_UNREACHABLE
        && rc != VERR_NET_CONNECTION_REFUSED
        && rc != VERR_TIMEOUT
        && rc != VERR_NET_CONNECTION_TIMED_OUT;
}

/**
 * Server mode connection thread.
 *
 * @returns iprt status code.
 * @param   hSelf           Thread handle. Ignored.
 * @param   pvUser          Pointer to ATSTRANSPORTINST the thread is bound to.
 */
static DECLCALLBACK(int) atsTcpServerConnectThread(RTTHREAD hSelf, void *pvUser)
{
    RT_NOREF(hSelf);

    PATSCONNCTX         pConnCtx = (PATSCONNCTX)pvUser;
    PATSTRANSPORTINST   pThis    = pConnCtx->pInst;
    PATSTRANSPORTCLIENT pClient  = pConnCtx->pClient;

    /** @todo Implement cancellation support for using pConnCtx->msTimeout. */

    LogRelFlowFuncEnter();

    RTSOCKET hTcpClient;
    int rc = RTTcpServerListen2(pThis->pTcpServer, &hTcpClient);
    if (RT_SUCCESS(rc))
    {
        hTcpClient = atsTcpSetClient(pThis, pClient, true /* fFromServer */, hTcpClient);
        RTTcpServerDisconnectClient2(hTcpClient);
    }

    LogRelFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Client mode connection thread.
 *
 * @returns iprt status code.
 * @param   hSelf           Thread handle. Use to sleep on. The main thread will
 *                          signal it to speed up thread shutdown.
 * @param   pvUser          Pointer to a connection context (PATSCONNCTX) the thread is bound to.
 */
static DECLCALLBACK(int) atsTcpClientConnectThread(RTTHREAD hSelf, void *pvUser)
{
    PATSCONNCTX         pConnCtx = (PATSCONNCTX)pvUser;
    PATSTRANSPORTINST   pThis    = pConnCtx->pInst;
    PATSTRANSPORTCLIENT pClient  = pConnCtx->pClient;

    uint64_t msStartTs = RTTimeMilliTS();

    LogRelFlowFuncEnter();

    for (;;)
    {
        /* Stop? */
        RTCritSectEnter(&pThis->CritSect);
        bool fStop = pThis->fStopConnecting;
        RTCritSectLeave(&pThis->CritSect);
        if (fStop)
            return VINF_SUCCESS;

        /* Try connect. */ /** @todo make cancelable! */
        RTSOCKET hTcpClient;
        int rc = RTTcpClientConnectEx(pThis->szConnectAddr, pThis->uConnectPort, &hTcpClient,
                                      RT_SOCKETCONNECT_DEFAULT_WAIT, &pThis->pConnectCancelCookie);
        if (RT_SUCCESS(rc))
        {
            hTcpClient = atsTcpSetClient(pThis, pClient, false /* fFromServer */, hTcpClient);
            RTTcpClientCloseEx(hTcpClient, true /* fGracefulShutdown*/);
            break;
        }

        if (atsTcpIsFatalClientConnectStatus(rc))
            return rc;

        if (   pConnCtx->msTimeout         != RT_INDEFINITE_WAIT
            && RTTimeMilliTS() - msStartTs >= pConnCtx->msTimeout)
        {
            LogRelFlowFunc(("Timed out (%RU32ms)\n", pConnCtx->msTimeout));
            return VERR_TIMEOUT;
        }

        /* Delay a wee bit before retrying. */
        RTThreadUserWait(hSelf, 1536);
    }

    LogRelFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Wait on the threads to complete.
 *
 * @returns Thread status (if collected), otherwise VINF_SUCCESS.
 * @param   pThis           Transport instance.
 * @param   cMillies        The period to wait on each thread.
 */
static int atsTcpConnectWaitOnThreads(PATSTRANSPORTINST pThis, RTMSINTERVAL cMillies)
{
    int rcRet = VINF_SUCCESS;

    LogRelFlowFuncEnter();

    if (pThis->hThreadConnect != NIL_RTTHREAD)
    {
        int rcThread;
        int rc2 = RTThreadWait(pThis->hThreadConnect, cMillies, &rcThread);
        if (RT_SUCCESS(rc2))
        {
            pThis->hThreadConnect = NIL_RTTHREAD;
            rcRet = rcThread;
        }
    }

    if (pThis->hThreadServer != NIL_RTTHREAD)
    {
        int rcThread;
        int rc2 = RTThreadWait(pThis->hThreadServer, cMillies, &rcThread);
        if (RT_SUCCESS(rc2))
        {
            pThis->hThreadServer = NIL_RTTHREAD;
            if (RT_SUCCESS(rc2))
                rcRet = rcThread;
        }
    }

    LogRelFlowFuncLeaveRC(rcRet);
    return rcRet;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnWaitForConnect}
 */
static DECLCALLBACK(int) atsTcpWaitForConnect(PATSTRANSPORTINST pThis,  RTMSINTERVAL msTimeout,
                                              bool *pfFromServer, PPATSTRANSPORTCLIENT ppClientNew)
{
    PATSTRANSPORTCLIENT pClient = (PATSTRANSPORTCLIENT)RTMemAllocZ(sizeof(ATSTRANSPORTCLIENT));
    AssertPtrReturn(pClient, VERR_NO_MEMORY);

    int rc;

    LogRelFlowFunc(("msTimeout=%RU32, enmConnMode=%#x\n", msTimeout, pThis->enmConnMode));

    uint64_t msStartTs = RTTimeMilliTS();

    if (pThis->enmConnMode == ATSCONNMODE_SERVER)
    {
        /** @todo Implement cancellation support for using \a msTimeout. */

        pClient->fFromServer = true;
        rc = RTTcpServerListen2(pThis->pTcpServer, &pClient->hTcpClient);
        LogRelFlowFunc(("RTTcpServerListen2(%RTsock) -> %Rrc\n", pClient->hTcpClient, rc));
    }
    else if (pThis->enmConnMode == ATSCONNMODE_CLIENT)
    {
        pClient->fFromServer = false;
        for (;;)
        {
            LogRelFlowFunc(("Calling RTTcpClientConnect(%s, %u,)...\n", pThis->szConnectAddr, pThis->uConnectPort));
            rc = RTTcpClientConnect(pThis->szConnectAddr, pThis->uConnectPort, &pClient->hTcpClient);
            LogRelFlowFunc(("RTTcpClientConnect(%RTsock) -> %Rrc\n", pClient->hTcpClient, rc));
            if (RT_SUCCESS(rc) || atsTcpIsFatalClientConnectStatus(rc))
                break;

            if (   msTimeout                   != RT_INDEFINITE_WAIT
                && RTTimeMilliTS() - msStartTs >= msTimeout)
            {
                rc = VERR_TIMEOUT;
                break;
            }

            if (pThis->fStopConnecting)
            {
                rc = VINF_SUCCESS;
                break;
            }

            /* Delay a wee bit before retrying. */
            RTThreadSleep(1536);
        }
    }
    else
    {
        Assert(pThis->enmConnMode == ATSCONNMODE_BOTH);

        /*
         * Create client threads.
         */
        RTCritSectEnter(&pThis->CritSect);

        pThis->fStopConnecting = false;
        RTCritSectLeave(&pThis->CritSect);

        atsTcpConnectWaitOnThreads(pThis, 32 /* cMillies */);

        ATSCONNCTX ConnCtx;
        RT_ZERO(ConnCtx);
        ConnCtx.pInst     = pThis;
        ConnCtx.pClient   = pClient;
        ConnCtx.msTimeout = msTimeout;

        rc = VINF_SUCCESS;
        if (pThis->hThreadConnect == NIL_RTTHREAD)
        {
            pThis->pConnectCancelCookie = NULL;
            rc = RTThreadCreate(&pThis->hThreadConnect, atsTcpClientConnectThread, &ConnCtx, 0, RTTHREADTYPE_DEFAULT,
                                RTTHREADFLAGS_WAITABLE, "tcpconn");
        }
        if (pThis->hThreadServer == NIL_RTTHREAD && RT_SUCCESS(rc))
            rc = RTThreadCreate(&pThis->hThreadServer, atsTcpServerConnectThread, &ConnCtx, 0, RTTHREADTYPE_DEFAULT,
                                RTTHREADFLAGS_WAITABLE, "tcpserv");

        RTCritSectEnter(&pThis->CritSect);

        /*
         * Wait for connection to be established.
         */
        while (   RT_SUCCESS(rc)
               && pClient->hTcpClient == NIL_RTSOCKET)
        {
            RTCritSectLeave(&pThis->CritSect);
            rc = atsTcpConnectWaitOnThreads(pThis, 10 /* cMillies */);
            RTCritSectEnter(&pThis->CritSect);
        }

        /*
         * Cancel the threads.
         */
        pThis->fStopConnecting = true;

        RTCritSectLeave(&pThis->CritSect);
        RTTcpClientCancelConnect(&pThis->pConnectCancelCookie);
    }

    if (RT_SUCCESS(rc))
    {
        if (pfFromServer)
            *pfFromServer = pClient->fFromServer;
        *ppClientNew = pClient;
    }
    else
    {
        atsTcpFreeClient(pThis, pClient);
        pClient = NULL;
    }

    if (RT_FAILURE(rc))
        LogRelFunc(("Failed with %Rrc\n", rc));

    return rc;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnNotifyReboot}
 */
static DECLCALLBACK(void) atsTcpNotifyReboot(PATSTRANSPORTINST pThis)
{
    LogRelFlowFuncEnter();
    if (pThis->pTcpServer)
    {
        int rc = RTTcpServerDestroy(pThis->pTcpServer);
        if (RT_FAILURE(rc))
            LogRelFunc(("RTTcpServerDestroy failed, rc=%Rrc", rc));
        pThis->pTcpServer = NULL;
    }
    LogRelFlowFuncLeave();
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnNotifyBye}
 */
static DECLCALLBACK(void) atsTcpNotifyBye(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient)
{
    LogRelFlowFunc(("pClient=%RTsock\n", pClient->hTcpClient));
    atsTcpDisconnectClient(pThis, pClient);
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnNotifyHowdy}
 */
static DECLCALLBACK(void) atsTcpNotifyHowdy(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient)
{
    LogRelFlowFunc(("pClient=%RTsock\n", pClient->hTcpClient));

    /* nothing to do here */
    RT_NOREF(pThis);
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnBabble}
 */
static DECLCALLBACK(void) atsTcpBabble(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient, PCATSPKTHDR pPktHdr, RTMSINTERVAL cMsSendTimeout)
{
    /*
     * Try send the babble reply.
     */
    RT_NOREF(cMsSendTimeout); /** @todo implement the timeout here; non-blocking write + select-on-write. */
    int     rc;
    size_t  cbToSend = RT_ALIGN_Z(pPktHdr->cb, ATSPKT_ALIGNMENT);
    do  rc = RTTcpWrite(pClient->hTcpClient, pPktHdr, cbToSend);
    while (rc == VERR_INTERRUPTED);

    LogRelFlowFunc(("pClient=%RTsock, rc=%Rrc\n", pClient->hTcpClient, rc));

    /*
     * Disconnect the client.
     */
    atsTcpDisconnectClient(pThis, pClient);
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnSendPkt}
 */
static DECLCALLBACK(int) atsTcpSendPkt(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient, PCATSPKTHDR pPktHdr)
{
    AssertReturn(pPktHdr->cb >= sizeof(ATSPKTHDR), VERR_INVALID_PARAMETER);

    /*
     * Write it.
     */
    size_t cbToSend = RT_ALIGN_Z(pPktHdr->cb, ATSPKT_ALIGNMENT);

    Log3Func(("%RU32 -> %zu\n", pPktHdr->cb, cbToSend));

    LogRel4(("pClient=%RTsock\n", pClient->hTcpClient));
    LogRel4(("Header:\n"
             "%.*Rhxd\n", RT_MIN(sizeof(ATSPKTHDR), cbToSend), pPktHdr));

    if (cbToSend > sizeof(ATSPKTHDR))
        LogRel4(("Payload:\n"
                 "%.*Rhxd\n",
                 RT_MIN(64, cbToSend - sizeof(ATSPKTHDR)), (uint8_t *)pPktHdr + sizeof(ATSPKTHDR)));

    int rc = RTTcpWrite(pClient->hTcpClient, pPktHdr, cbToSend);
    if (    RT_FAILURE(rc)
        &&  rc != VERR_INTERRUPTED)
    {
        /* assume fatal connection error. */
        LogRelFunc(("RTTcpWrite -> %Rrc -> atsTcpDisconnectClient(%RTsock)\n", rc, pClient->hTcpClient));
        atsTcpDisconnectClient(pThis, pClient);
    }

    LogRel3(("atsTcpSendPkt: pClient=%RTsock, achOpcode=%.8s, cbSent=%zu -> %Rrc\n", pClient->hTcpClient, (const char *)pPktHdr->achOpcode, cbToSend, rc));
    return rc;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnRecvPkt}
 */
static DECLCALLBACK(int) atsTcpRecvPkt(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient, PPATSPKTHDR ppPktHdr)
{
    int rc = VINF_SUCCESS;
    *ppPktHdr = NULL;

    LogRel4(("pClient=%RTsock (cbTcpStashed=%zu, cbTcpStashedAlloced=%zu)\n",
             pClient->hTcpClient, pClient->cbTcpStashed, pClient->cbTcpStashedAlloced));

    /*
     * Read state.
     */
    size_t      offData       = 0;
    size_t      cbData        = 0;
    size_t      cbDataAlloced;
    uint8_t    *pbData        = NULL;

    /*
     * Any stashed data?
     */
    if (pClient->cbTcpStashedAlloced)
    {
        offData               = pClient->cbTcpStashed;
        cbDataAlloced         = pClient->cbTcpStashedAlloced;
        pbData                = pClient->pbTcpStashed;

        pClient->cbTcpStashed        = 0;
        pClient->cbTcpStashedAlloced = 0;
        pClient->pbTcpStashed        = NULL;
    }
    else
    {
        cbDataAlloced = RT_ALIGN_Z(64,  ATSPKT_ALIGNMENT);
        pbData = (uint8_t *)RTMemAlloc(cbDataAlloced);
        AssertPtrReturn(pbData, VERR_NO_MEMORY);
    }

    /*
     * Read and validate the length.
     */
    while (offData < sizeof(uint32_t))
    {
        size_t cbRead;
        rc = RTTcpRead(pClient->hTcpClient, pbData + offData, sizeof(uint32_t) - offData, &cbRead);
        if (RT_FAILURE(rc))
            break;
        if (cbRead == 0)
        {
            LogRelFunc(("RTTcpRead -> %Rrc / cbRead=0 -> VERR_NET_NOT_CONNECTED (#1)\n", rc));
            rc = VERR_NET_NOT_CONNECTED;
            break;
        }
        offData += cbRead;
    }
    if (RT_SUCCESS(rc))
    {
        ASMCompilerBarrier(); /* paranoia^3 */
        cbData = *(uint32_t volatile *)pbData;
        if (cbData >= sizeof(ATSPKTHDR) && cbData <= ATSPKT_MAX_SIZE)
        {
            /*
             * Align the length and reallocate the return packet it necessary.
             */
            cbData = RT_ALIGN_Z(cbData, ATSPKT_ALIGNMENT);
            if (cbData > cbDataAlloced)
            {
                void *pvNew = RTMemRealloc(pbData, cbData);
                if (pvNew)
                {
                    pbData = (uint8_t *)pvNew;
                    cbDataAlloced = cbData;
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Read the remainder of the data.
                 */
                while (offData < cbData)
                {
                    size_t cbRead;
                    rc = RTTcpRead(pClient->hTcpClient, pbData + offData, cbData - offData, &cbRead);
                    if (RT_FAILURE(rc))
                        break;
                    if (cbRead == 0)
                    {
                        LogRelFunc(("RTTcpRead -> %Rrc / cbRead=0 -> VERR_NET_NOT_CONNECTED (#2)\n", rc));
                        rc = VERR_NET_NOT_CONNECTED;
                        break;
                    }

                    offData += cbRead;
                }

                LogRel4(("Header:\n"
                         "%.*Rhxd\n", sizeof(ATSPKTHDR), pbData));

                if (   RT_SUCCESS(rc)
                    && cbData > sizeof(ATSPKTHDR))
                    LogRel4(("Payload:\n"
                             "%.*Rhxd\n", RT_MIN(64, cbData - sizeof(ATSPKTHDR)), (uint8_t *)pbData + sizeof(ATSPKTHDR)));
            }
        }
        else
        {
            LogRelFunc(("Received invalid packet size (%zu)\n", cbData));
            rc = VERR_NET_PROTOCOL_ERROR;
        }
    }
    if (RT_SUCCESS(rc))
        *ppPktHdr = (PATSPKTHDR)pbData;
    else
    {
        /*
         * Deal with errors.
         */
        if (rc == VERR_INTERRUPTED)
        {
            /* stash it away for the next call. */
            pClient->cbTcpStashed        = cbData;
            pClient->cbTcpStashedAlloced = cbDataAlloced;
            pClient->pbTcpStashed        = pbData;
        }
        else
        {
            RTMemFree(pbData);
            pbData = NULL;

            /* assume fatal connection error. */
            LogRelFunc(("RTTcpRead -> %Rrc -> atsTcpDisconnectClient(%RTsock)\n", rc, pClient->hTcpClient));
            atsTcpDisconnectClient(pThis, pClient);
        }
    }

    PATSPKTHDR pPktHdr = (PATSPKTHDR)pbData;
    LogRel3(("atsTcpRecvPkt: pClient=%RTsock, achOpcode=%.8s, cbRead=%zu -> %Rrc\n",
             pClient->hTcpClient, pPktHdr ? (const char *)pPktHdr->achOpcode : "NONE    ", cbData, rc));
    return rc;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnPollSetAdd}
 */
static DECLCALLBACK(int) atsTcpPollSetAdd(PATSTRANSPORTINST pThis, RTPOLLSET hPollSet, PATSTRANSPORTCLIENT pClient, uint32_t idStart)
{
    RT_NOREF(pThis);
    return RTPollSetAddSocket(hPollSet, pClient->hTcpClient, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, idStart);
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnPollSetRemove}
 */
static DECLCALLBACK(int) atsTcpPollSetRemove(PATSTRANSPORTINST pThis, RTPOLLSET hPollSet, PATSTRANSPORTCLIENT pClient, uint32_t idStart)
{
    RT_NOREF(pThis, pClient);
    return RTPollSetRemove(hPollSet, idStart);
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnDisconnect}
 */
static DECLCALLBACK(void) atsTcpDisconnect(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient)
{
    atsTcpFreeClient(pThis, pClient);
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnPollIn}
 */
static DECLCALLBACK(bool) atsTcpPollIn(PATSTRANSPORTINST pThis, PATSTRANSPORTCLIENT pClient)
{
    RT_NOREF(pThis);
    int rc = RTTcpSelectOne(pClient->hTcpClient, 0/*cMillies*/);
    return RT_SUCCESS(rc);
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnStop}
 */
static DECLCALLBACK(void) atsTcpStop(PATSTRANSPORTINST pThis)
{
    LogRelFlowFuncEnter();

    /* Signal thread */
    if (RTCritSectIsInitialized(&pThis->CritSect))
    {
        RTCritSectEnter(&pThis->CritSect);
        pThis->fStopConnecting = true;
        RTCritSectLeave(&pThis->CritSect);
    }

    if (pThis->hThreadConnect != NIL_RTTHREAD)
    {
        RTThreadUserSignal(pThis->hThreadConnect);
        RTTcpClientCancelConnect(&pThis->pConnectCancelCookie);
    }

    /* Shut down the server (will wake up thread). */
    if (pThis->pTcpServer)
    {
        LogRelFlowFunc(("Destroying server...\n"));
        int rc = RTTcpServerDestroy(pThis->pTcpServer);
        if (RT_FAILURE(rc))
            LogRelFunc(("RTTcpServerDestroy failed with %Rrc", rc));
        pThis->pTcpServer        = NULL;
    }

    /* Wait for the thread (they should've had some time to quit by now). */
    atsTcpConnectWaitOnThreads(pThis, 15000);

    LogRelFlowFuncLeave();
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnCreate}
 */
static DECLCALLBACK(int) atsTcpCreate(PATSTRANSPORTINST *ppThis)
{
    PATSTRANSPORTINST pThis = (PATSTRANSPORTINST)RTMemAllocZ(sizeof(ATSTRANSPORTINST));
    AssertPtrReturn(pThis, VERR_NO_MEMORY);

    int rc = RTCritSectInit(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        *ppThis = pThis;
    }

    return rc;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnDestroy}
 */
static DECLCALLBACK(int) atsTcpDestroy(PATSTRANSPORTINST pThis)
{
    /* Stop things first. */
    atsTcpStop(pThis);

    /* Finally, clean up the critical section. */
    if (RTCritSectIsInitialized(&pThis->CritSect))
        RTCritSectDelete(&pThis->CritSect);

    RTMemFree(pThis);
    pThis = NULL;

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnStart}
 */
static DECLCALLBACK(int) atsTcpStart(PATSTRANSPORTINST pThis)
{
    int rc = VINF_SUCCESS;

    if (pThis->enmConnMode != ATSCONNMODE_CLIENT)
    {
        rc = RTTcpServerCreateEx(pThis->szBindAddr[0] ? pThis->szBindAddr : NULL, pThis->uBindPort, &pThis->pTcpServer);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_NET_DOWN)
            {
                LogRelFunc(("RTTcpServerCreateEx(%s, %u,) failed: %Rrc, retrying for 20 seconds...\n",
                            pThis->szBindAddr[0] ? pThis->szBindAddr : NULL, pThis->uBindPort, rc));
                uint64_t StartMs = RTTimeMilliTS();
                do
                {
                    RTThreadSleep(1000);
                    rc = RTTcpServerCreateEx(pThis->szBindAddr[0] ? pThis->szBindAddr : NULL, pThis->uBindPort, &pThis->pTcpServer);
                } while (   rc == VERR_NET_DOWN
                         && RTTimeMilliTS() - StartMs < 20000);
                if (RT_SUCCESS(rc))
                    LogRelFunc(("RTTcpServerCreateEx succceeded\n"));
            }

            if (RT_FAILURE(rc))
            {
                LogRelFunc(("RTTcpServerCreateEx(%s, %u,) failed: %Rrc\n",
                            pThis->szBindAddr[0] ? pThis->szBindAddr : NULL, pThis->uBindPort, rc));
            }
        }
    }

    return rc;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnOption}
 */
static DECLCALLBACK(int) atsTcpOption(PATSTRANSPORTINST pThis, int ch, PCRTGETOPTUNION pVal)
{
    int rc;

    switch (ch)
    {
        case ATSTCPOPT_CONN_MODE:
            pThis->enmConnMode = (ATSCONNMODE)pVal->u32;
            return VINF_SUCCESS;

        case ATSTCPOPT_BIND_ADDRESS:
            rc = RTStrCopy(pThis->szBindAddr, sizeof(pThis->szBindAddr), pVal->psz);
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "TCP bind address is too long (%Rrc)", rc);
            if (!pThis->szBindAddr[0])
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "No TCP bind address specified: %s", pThis->szBindAddr);
            return VINF_SUCCESS;

        case ATSTCPOPT_BIND_PORT:
            pThis->uBindPort = pVal->u16;
            return VINF_SUCCESS;

        case ATSTCPOPT_CONNECT_ADDRESS:
            rc = RTStrCopy(pThis->szConnectAddr, sizeof(pThis->szConnectAddr), pVal->psz);
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "TCP connect address is too long (%Rrc)", rc);
            if (!pThis->szConnectAddr[0])
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "No TCP connect address specified");
            return VINF_SUCCESS;

        case ATSTCPOPT_CONNECT_PORT:
            pThis->uConnectPort = pVal->u16;
            return VINF_SUCCESS;

        default:
            break;
    }
    return VERR_TRY_AGAIN;
}

/**
 * @interface_method_impl{ATSTRANSPORT,pfnUsage}
 */
static DECLCALLBACK(void) atsTcpUsage(PRTSTREAM pStream)
{
    RTStrmPrintf(pStream,
                 "  --tcp-conn-mode <0=both|1=client|2=server>\n"
                 "    Selects the connection mode\n"
                 "    Default: 0 (both)\n"
                 "  --tcp-bind-addr[ess] <address>\n"
                 "    The address(es) to listen to TCP connection on. Empty string\n"
                 "    means any address, this is the default\n"
                 "  --tcp-bind-port <port>\n"
                 "    The port to listen to TCP connections on\n"
                 "    Default: %u\n"
                 "  --tcp-connect-addr[ess] <address>\n"
                 "    The address of the server to try connect to in client mode\n"
                 "    Default: " ATS_TCP_DEF_CONNECT_GUEST_STR "\n"
                 "  --tcp-connect-port <port>\n"
                 "    The port on the server to connect to in client mode\n"
                 "    Default: %u\n"
                 , ATS_TCP_DEF_BIND_PORT_GUEST, ATS_TCP_DEF_CONNECT_PORT_GUEST);
}

/** Command line options for the TCP/IP transport layer. */
static const RTGETOPTDEF  g_TcpOpts[] =
{
    { "--tcp-conn-mode",        ATSTCPOPT_CONN_MODE,        RTGETOPT_REQ_STRING },
    { "--tcp-bind-addr",        ATSTCPOPT_BIND_ADDRESS,     RTGETOPT_REQ_STRING },
    { "--tcp-bind-address",     ATSTCPOPT_BIND_ADDRESS,     RTGETOPT_REQ_STRING },
    { "--tcp-bind-port",        ATSTCPOPT_BIND_PORT,        RTGETOPT_REQ_UINT16 },
    { "--tcp-connect-addr",     ATSTCPOPT_CONNECT_ADDRESS,  RTGETOPT_REQ_STRING },
    { "--tcp-connect-address",  ATSTCPOPT_CONNECT_ADDRESS,  RTGETOPT_REQ_STRING },
    { "--tcp-connect-port",     ATSTCPOPT_CONNECT_PORT,     RTGETOPT_REQ_UINT16 }
};

/** TCP/IP transport layer. */
const ATSTRANSPORT g_TcpTransport =
{
    /* .szName            = */ "tcp",
    /* .pszDesc           = */ "TCP/IP",
    /* .cOpts             = */ &g_TcpOpts[0],
    /* .paOpts            = */ RT_ELEMENTS(g_TcpOpts),
    /* .pfnUsage          = */ atsTcpUsage,
    /* .pfnCreate         = */ atsTcpCreate,
    /* .pfnDestroy        = */ atsTcpDestroy,
    /* .pfnOption         = */ atsTcpOption,
    /* .pfnStart          = */ atsTcpStart,
    /* .pfnStop           = */ atsTcpStop,
    /* .pfnWaitForConnect = */ atsTcpWaitForConnect,
    /* .pfnDisconnect     = */ atsTcpDisconnect,
    /* .pfnPollIn         = */ atsTcpPollIn,
    /* .pfnPollSetAdd     = */ atsTcpPollSetAdd,
    /* .pfnPollSetRemove  = */ atsTcpPollSetRemove,
    /* .pfnRecvPkt        = */ atsTcpRecvPkt,
    /* .pfnSendPkt        = */ atsTcpSendPkt,
    /* .pfnBabble         = */ atsTcpBabble,
    /* .pfnNotifyHowdy    = */ atsTcpNotifyHowdy,
    /* .pfnNotifyBye      = */ atsTcpNotifyBye,
    /* .pfnNotifyReboot   = */ atsTcpNotifyReboot,
    /* .u32EndMarker      = */ UINT32_C(0x12345678)
};

