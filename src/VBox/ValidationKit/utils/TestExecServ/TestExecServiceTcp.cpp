/* $Id: TestExecServiceTcp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TestExecServ - Basic Remote Execution Service, TCP/IP Transport Layer.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/tcp.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "TestExecServiceInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The default server port. */
#define TXS_TCP_DEF_BIND_PORT                   5042
/** The default client port. */
#define TXS_TCP_DEF_CONNECT_PORT                5048

/** The default server bind address. */
#define TXS_TCP_DEF_BIND_ADDRESS                ""
/** The default client connect address (i.e. of the host server). */
#define TXS_TCP_DEF_CONNECT_ADDRESS             "10.0.2.2"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name TCP Parameters
 * @{ */
static enum  { TXSTCPMODE_BOTH, TXSTCPMODE_CLIENT, TXSTCPMODE_SERVER }
                            g_enmTcpMode            = TXSTCPMODE_BOTH;

/** The addresses to bind to.  Empty string means any.  */
static char                 g_szTcpBindAddr[256]    = TXS_TCP_DEF_BIND_ADDRESS;
/** The TCP port to listen to. */
static uint32_t             g_uTcpBindPort          = TXS_TCP_DEF_BIND_PORT;
/** The addresses to connect to if fRevesedSetupMode is @c true. */
static char                 g_szTcpConnectAddr[256] = TXS_TCP_DEF_CONNECT_ADDRESS;
/** The TCP port to listen to. */
static uint32_t             g_uTcpConnectPort       = TXS_TCP_DEF_CONNECT_PORT;
/** @} */

/** Critical section for serializing access to the next few variables. */
static RTCRITSECT           g_TcpCritSect;
/** Pointer to the TCP server instance. */
static PRTTCPSERVER         g_pTcpServer            = NULL;
/** Thread calling RTTcpServerListen2. */
static RTTHREAD             g_hThreadTcpServer      = NIL_RTTHREAD;
/** Thread calling RTTcpClientConnect. */
static RTTHREAD             g_hThreadTcpConnect     = NIL_RTTHREAD;
/** The main thread handle (for signalling). */
static RTTHREAD             g_hThreadMain           = NIL_RTTHREAD;
/** Stop connecting attempts when set. */
static bool                 g_fTcpStopConnecting    = false;
/** Connect cancel cookie. */
static PRTTCPCLIENTCONNECTCANCEL volatile g_pTcpConnectCancelCookie = NULL;

/** Socket of the current client. */
static RTSOCKET             g_hTcpClient            = NIL_RTSOCKET;
/** Indicates whether g_hTcpClient comes from the server or from a client
 * connect (relevant when closing it). */
static bool                 g_fTcpClientFromServer  = false;
/** The size of the stashed data. */
static size_t               g_cbTcpStashed          = 0;
/** The size of the stashed data allocation. */
static size_t               g_cbTcpStashedAlloced   = 0;
/** The stashed data. */
static uint8_t             *g_pbTcpStashed          = NULL;



/**
 * Disconnects the current client.
 */
static void txsTcpDisconnectClient(void)
{
    int rc;
    if (g_fTcpClientFromServer)
        rc = RTTcpServerDisconnectClient2(g_hTcpClient);
    else
        rc = RTTcpClientClose(g_hTcpClient);
    AssertRCSuccess(rc);
    g_hTcpClient = NIL_RTSOCKET;
}

/**
 * Sets the current client socket in a safe manner.
 *
 * @returns NIL_RTSOCKET if consumed, other wise hTcpClient.
 * @param   hTcpClient      The client socket.
 */
static RTSOCKET txsTcpSetClient(RTSOCKET hTcpClient)
{
    RTCritSectEnter(&g_TcpCritSect);
    if (   g_hTcpClient  == NIL_RTSOCKET
        && !g_fTcpStopConnecting
        && g_hThreadMain != NIL_RTTHREAD
       )
    {
        g_fTcpClientFromServer = true;
        g_hTcpClient = hTcpClient;
        int rc = RTThreadUserSignal(g_hThreadMain); AssertRC(rc);
        hTcpClient = NIL_RTSOCKET;
    }
    RTCritSectLeave(&g_TcpCritSect);
    return hTcpClient;
}

/**
 * Server mode connection thread.
 *
 * @returns iprt status code.
 * @param   hSelf           Thread handle. Ignored.
 * @param   pvUser          Ignored.
 */
static DECLCALLBACK(int) txsTcpServerConnectThread(RTTHREAD hSelf, void *pvUser)
{
    RTSOCKET hTcpClient;
    int rc = RTTcpServerListen2(g_pTcpServer, &hTcpClient);
    Log(("txsTcpConnectServerThread: RTTcpServerListen2 -> %Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        hTcpClient = txsTcpSetClient(hTcpClient);
        RTTcpServerDisconnectClient2(hTcpClient);
    }

    RT_NOREF2(hSelf, pvUser);
    return rc;
}

/**
 * Checks if it's a fatal RTTcpClientConnect return code.
 *
 * @returns true / false.
 * @param   rc              The IPRT status code.
 */
static bool txsTcpIsFatalClientConnectStatus(int rc)
{
    return rc != VERR_NET_UNREACHABLE
        && rc != VERR_NET_HOST_DOWN
        && rc != VERR_NET_HOST_UNREACHABLE
        && rc != VERR_NET_CONNECTION_REFUSED
        && rc != VERR_TIMEOUT
        && rc != VERR_NET_CONNECTION_TIMED_OUT;
}

/**
 * Client mode connection thread.
 *
 * @returns iprt status code.
 * @param   hSelf           Thread handle. Use to sleep on. The main thread will
 *                          signal it to speed up thread shutdown.
 * @param   pvUser          Ignored.
 */
static DECLCALLBACK(int) txsTcpClientConnectThread(RTTHREAD hSelf, void *pvUser)
{
    RT_NOREF1(pvUser);

    for (;;)
    {
        /* Stop? */
        RTCritSectEnter(&g_TcpCritSect);
        bool fStop = g_fTcpStopConnecting;
        RTCritSectLeave(&g_TcpCritSect);
        if (fStop)
            return VINF_SUCCESS;

        /* Try connect. */ /** @todo make cancelable! */
        RTSOCKET hTcpClient;
        Log2(("Calling RTTcpClientConnect(%s, %u,)...\n", g_szTcpConnectAddr, g_uTcpConnectPort));
        int rc = RTTcpClientConnectEx(g_szTcpConnectAddr, g_uTcpConnectPort, &hTcpClient,
                                      RT_SOCKETCONNECT_DEFAULT_WAIT, &g_pTcpConnectCancelCookie);
        Log(("txsTcpRecvPkt: RTTcpClientConnect -> %Rrc\n", rc));
        if (RT_SUCCESS(rc))
        {
            uint32_t cSecsIdle              = 75; /* idle time in seconds before first keep-alive probe */
            uint32_t cSecsInterval          = 30; /* interval in seconds between keep-alive probes */
            uint32_t cFailedPktsBeforeClose =  4; /* number of unacknowledged keep-alive probes before closing connection */
            rc = RTTcpSetKeepAlive(hTcpClient, true /* fEnable */, cSecsIdle, cSecsInterval, cFailedPktsBeforeClose);
            if (RT_FAILURE(rc))
                RTMsgInfo("Failed to set SO_KEEPALIVE on client socket hTcpClient: rc=%Rrc\n", rc);
            hTcpClient = txsTcpSetClient(hTcpClient);
            RTTcpClientCloseEx(hTcpClient, true /* fGracefulShutdown*/);
            break;
        }

        if (txsTcpIsFatalClientConnectStatus(rc))
            return rc;

        /* Delay a wee bit before retrying. */
        RTThreadUserWait(hSelf, 1536);
    }
    return VINF_SUCCESS;
}

/**
 * Wait on the threads to complete.
 *
 * @returns Thread status (if collected), otherwise VINF_SUCCESS.
 * @param   cMillies        The period to wait on each thread.
 */
static int txsTcpConnectWaitOnThreads(RTMSINTERVAL cMillies)
{
    int rcRet = VINF_SUCCESS;

    if (g_hThreadTcpConnect != NIL_RTTHREAD)
    {
        int rcThread;
        int rc2 = RTThreadWait(g_hThreadTcpConnect, cMillies, &rcThread);
        if (RT_SUCCESS(rc2))
        {
            g_hThreadTcpConnect = NIL_RTTHREAD;
            rcRet = rcThread;
        }
    }

    if (g_hThreadTcpServer != NIL_RTTHREAD)
    {
        int rcThread;
        int rc2 = RTThreadWait(g_hThreadTcpServer, cMillies, &rcThread);
        if (RT_SUCCESS(rc2))
        {
            g_hThreadTcpServer = NIL_RTTHREAD;
            if (RT_SUCCESS(rc2))
                rcRet = rcThread;
        }
    }
    return rcRet;
}

/**
 * Connects to the peer.
 *
 * @returns VBox status code. Updates g_hTcpClient and g_fTcpClientFromServer on
 *          success
 */
static int txsTcpConnect(void)
{
    int rc;
    if (g_enmTcpMode == TXSTCPMODE_SERVER)
    {
        g_fTcpClientFromServer = true;
        rc = RTTcpServerListen2(g_pTcpServer, &g_hTcpClient);
        Log(("txsTcpRecvPkt: RTTcpServerListen2 -> %Rrc\n", rc));
    }
    else if (g_enmTcpMode == TXSTCPMODE_CLIENT)
    {
        g_fTcpClientFromServer = false;
        for (;;)
        {
            Log2(("Calling RTTcpClientConnect(%s, %u,)...\n", g_szTcpConnectAddr, g_uTcpConnectPort));
            rc = RTTcpClientConnect(g_szTcpConnectAddr, g_uTcpConnectPort, &g_hTcpClient);
            Log(("txsTcpRecvPkt: RTTcpClientConnect -> %Rrc\n", rc));
            if (RT_SUCCESS(rc) || txsTcpIsFatalClientConnectStatus(rc))
                break;

            /* Delay a wee bit before retrying. */
            RTThreadSleep(1536);
        }
    }
    else
    {
        Assert(g_enmTcpMode == TXSTCPMODE_BOTH);
        RTTHREAD hSelf = RTThreadSelf();

        /*
         * Create client threads.
         */
        RTCritSectEnter(&g_TcpCritSect);
        RTThreadUserReset(hSelf);
        g_hThreadMain        = hSelf;
        g_fTcpStopConnecting = false;
        RTCritSectLeave(&g_TcpCritSect);

        txsTcpConnectWaitOnThreads(32);

        rc = VINF_SUCCESS;
        if (g_hThreadTcpConnect == NIL_RTTHREAD)
        {
            g_pTcpConnectCancelCookie = NULL;
            rc = RTThreadCreate(&g_hThreadTcpConnect, txsTcpClientConnectThread, NULL, 0, RTTHREADTYPE_DEFAULT,
                                RTTHREADFLAGS_WAITABLE, "tcpconn");
        }
        if (g_hThreadTcpServer == NIL_RTTHREAD && RT_SUCCESS(rc))
            rc = RTThreadCreate(&g_hThreadTcpServer, txsTcpServerConnectThread, NULL, 0, RTTHREADTYPE_DEFAULT,
                                RTTHREADFLAGS_WAITABLE, "tcpserv");

        RTCritSectEnter(&g_TcpCritSect);

        /*
         * Wait for connection to be established.
         */
        while (   RT_SUCCESS(rc)
               && g_hTcpClient == NIL_RTSOCKET)
        {
            RTCritSectLeave(&g_TcpCritSect);
            RTThreadUserWait(hSelf, 1536);
            rc = txsTcpConnectWaitOnThreads(0);
            RTCritSectEnter(&g_TcpCritSect);
        }

        /*
         * Cancel the threads.
         */
        g_hThreadMain        = NIL_RTTHREAD;
        g_fTcpStopConnecting = true;

        RTCritSectLeave(&g_TcpCritSect);
        RTTcpClientCancelConnect(&g_pTcpConnectCancelCookie);
    }

    AssertMsg(RT_SUCCESS(rc) ? g_hTcpClient != NIL_RTSOCKET : g_hTcpClient == NIL_RTSOCKET, ("%Rrc %p\n", rc, g_hTcpClient));
    g_cbTcpStashed = 0;
    return rc;
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnNotifyReboot}
 */
static DECLCALLBACK(void) txsTcpNotifyReboot(void)
{
    Log(("txsTcpNotifyReboot: RTTcpServerDestroy(%p)\n", g_pTcpServer));
    if (g_pTcpServer)
    {
        int rc = RTTcpServerDestroy(g_pTcpServer);
        if (RT_FAILURE(rc))
            RTMsgInfo("RTTcpServerDestroy failed in txsTcpNotifyReboot: %Rrc", rc);
        g_pTcpServer = NULL;
    }
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnNotifyBye}
 */
static DECLCALLBACK(void) txsTcpNotifyBye(void)
{
    Log(("txsTcpNotifyBye: txsTcpDisconnectClient %RTsock\n", g_hTcpClient));
    txsTcpDisconnectClient();
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnNotifyHowdy}
 */
static DECLCALLBACK(void) txsTcpNotifyHowdy(void)
{
    /* nothing to do here */
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnBabble}
 */
static DECLCALLBACK(void) txsTcpBabble(PCTXSPKTHDR pPktHdr, RTMSINTERVAL cMsSendTimeout)
{
    /*
     * Quietly ignore already disconnected client.
     */
    RTSOCKET hTcpClient = g_hTcpClient;
    if (hTcpClient == NIL_RTSOCKET)
        return;

    /*
     * Try send the babble reply.
     */
    NOREF(cMsSendTimeout); /** @todo implement the timeout here; non-blocking write + select-on-write. */
    int     rc;
    size_t  cbToSend = RT_ALIGN_Z(pPktHdr->cb, TXSPKT_ALIGNMENT);
    do  rc = RTTcpWrite(hTcpClient, pPktHdr, cbToSend);
    while (rc == VERR_INTERRUPTED);

    /*
     * Disconnect the client.
     */
    Log(("txsTcpBabble: txsTcpDisconnectClient(%RTsock) (RTTcpWrite rc=%Rrc)\n", g_hTcpClient, rc));
    txsTcpDisconnectClient();
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnSendPkt}
 */
static DECLCALLBACK(int) txsTcpSendPkt(PCTXSPKTHDR pPktHdr)
{
    Assert(pPktHdr->cb >= sizeof(TXSPKTHDR));

    /*
     * Fail if no client connection.
     */
    RTSOCKET hTcpClient = g_hTcpClient;
    if (hTcpClient == NIL_RTSOCKET)
        return VERR_NET_NOT_CONNECTED;

    /*
     * Write it.
     */
    size_t cbToSend = RT_ALIGN_Z(pPktHdr->cb, TXSPKT_ALIGNMENT);
    int rc = RTTcpWrite(hTcpClient, pPktHdr, cbToSend);
    if (    RT_FAILURE(rc)
        &&  rc != VERR_INTERRUPTED)
    {
        /* assume fatal connection error. */
        Log(("RTTcpWrite -> %Rrc -> txsTcpDisconnectClient(%RTsock)\n", rc, g_hTcpClient));
        txsTcpDisconnectClient();
    }

    return rc;
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnRecvPkt}
 */
static DECLCALLBACK(int) txsTcpRecvPkt(PPTXSPKTHDR ppPktHdr)
{
    int rc = VINF_SUCCESS;
    *ppPktHdr = NULL;

    /*
     * Do we have to wait for a client to connect?
     */
    RTSOCKET hTcpClient = g_hTcpClient;
    if (hTcpClient == NIL_RTSOCKET)
    {
        rc = txsTcpConnect();
        if (RT_FAILURE(rc))
            return rc;
        hTcpClient = g_hTcpClient; Assert(hTcpClient != NIL_RTSOCKET);
    }

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
    if (g_cbTcpStashedAlloced)
    {
        offData               = g_cbTcpStashed;
        cbDataAlloced         = g_cbTcpStashedAlloced;
        pbData                = g_pbTcpStashed;

        g_cbTcpStashed        = 0;
        g_cbTcpStashedAlloced = 0;
        g_pbTcpStashed        = NULL;
    }
    else
    {
        cbDataAlloced = RT_ALIGN_Z(64,  TXSPKT_ALIGNMENT);
        pbData = (uint8_t *)RTMemAlloc(cbDataAlloced);
        if (!pbData)
            return VERR_NO_MEMORY;
    }

    /*
     * Read and valid the length.
     */
    while (offData < sizeof(uint32_t))
    {
        size_t cbRead;
        rc = RTTcpRead(hTcpClient, pbData + offData, sizeof(uint32_t) - offData, &cbRead);
        if (RT_FAILURE(rc))
            break;
        if (cbRead == 0)
        {
            Log(("txsTcpRecvPkt: RTTcpRead -> %Rrc / cbRead=0 -> VERR_NET_NOT_CONNECTED (#1)\n", rc));
            rc = VERR_NET_NOT_CONNECTED;
            break;
        }
        offData += cbRead;
    }
    if (RT_SUCCESS(rc))
    {
        ASMCompilerBarrier(); /* paranoia^3 */
        cbData = *(uint32_t volatile *)pbData;
        if (cbData >= sizeof(TXSPKTHDR) && cbData <= TXSPKT_MAX_SIZE)
        {
            /*
             * Align the length and reallocate the return packet it necessary.
             */
            cbData = RT_ALIGN_Z(cbData, TXSPKT_ALIGNMENT);
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
                    rc = RTTcpRead(hTcpClient, pbData + offData, cbData - offData, &cbRead);
                    if (RT_FAILURE(rc))
                        break;
                    if (cbRead == 0)
                    {
                        Log(("txsTcpRecvPkt: RTTcpRead -> %Rrc / cbRead=0 -> VERR_NET_NOT_CONNECTED (#2)\n", rc));
                        rc = VERR_NET_NOT_CONNECTED;
                        break;
                    }
                    offData += cbRead;
                }
            }
        }
        else
            rc = VERR_NET_PROTOCOL_ERROR;
    }
    if (RT_SUCCESS(rc))
        *ppPktHdr = (PTXSPKTHDR)pbData;
    else
    {
        /*
         * Deal with errors.
         */
        if (rc == VERR_INTERRUPTED)
        {
            /* stash it away for the next call. */
            g_cbTcpStashed        = cbData;
            g_cbTcpStashedAlloced = cbDataAlloced;
            g_pbTcpStashed        = pbData;
        }
        else
        {
            RTMemFree(pbData);

            /* assume fatal connection error. */
            Log(("txsTcpRecvPkt: RTTcpRead -> %Rrc -> txsTcpDisconnectClient(%RTsock)\n", rc, g_hTcpClient));
            txsTcpDisconnectClient();
        }
    }

    return rc;
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnPollSetAdd}
 */
static DECLCALLBACK(int) txsTcpPollSetAdd(RTPOLLSET hPollSet, uint32_t idStart)
{
    return RTPollSetAddSocket(hPollSet, g_hTcpClient, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, idStart);
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnPollIn}
 */
static DECLCALLBACK(bool) txsTcpPollIn(void)
{
    RTSOCKET hTcpClient = g_hTcpClient;
    if (hTcpClient == NIL_RTSOCKET)
        return false;
    int rc = RTTcpSelectOne(hTcpClient, 0/*cMillies*/);
    return RT_SUCCESS(rc);
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnTerm}
 */
static DECLCALLBACK(void) txsTcpTerm(void)
{
    /* Signal thread */
    if (RTCritSectIsInitialized(&g_TcpCritSect))
    {
        RTCritSectEnter(&g_TcpCritSect);
        g_fTcpStopConnecting = true;
        RTCritSectLeave(&g_TcpCritSect);
    }

    if (g_hThreadTcpConnect != NIL_RTTHREAD)
    {
        RTThreadUserSignal(g_hThreadTcpConnect);
        RTTcpClientCancelConnect(&g_pTcpConnectCancelCookie);
    }

    /* Shut down the server (will wake up thread). */
    if (g_pTcpServer)
    {
        Log(("txsTcpTerm: Destroying server...\n"));
        int rc = RTTcpServerDestroy(g_pTcpServer);
        if (RT_FAILURE(rc))
            RTMsgInfo("RTTcpServerDestroy failed in txsTcpTerm: %Rrc", rc);
        g_pTcpServer        = NULL;
    }

    /* Shut down client */
    if (g_hTcpClient != NIL_RTSOCKET)
    {
        if (g_fTcpClientFromServer)
        {
            Log(("txsTcpTerm: Disconnecting client...\n"));
            int rc = RTTcpServerDisconnectClient2(g_hTcpClient);
            if (RT_FAILURE(rc))
                RTMsgInfo("RTTcpServerDisconnectClient2(%RTsock) failed in txsTcpTerm: %Rrc", g_hTcpClient, rc);
        }
        else
        {
            int rc = RTTcpClientClose(g_hTcpClient);
            if (RT_FAILURE(rc))
                RTMsgInfo("RTTcpClientClose(%RTsock) failed in txsTcpTerm: %Rrc", g_hTcpClient, rc);
        }
        g_hTcpClient        = NIL_RTSOCKET;
    }

    /* Clean up stashing. */
    RTMemFree(g_pbTcpStashed);
    g_pbTcpStashed          = NULL;
    g_cbTcpStashed          = 0;
    g_cbTcpStashedAlloced   = 0;

    /* Wait for the thread (they should've had some time to quit by now). */
    txsTcpConnectWaitOnThreads(15000);

    /* Finally, clean up the critical section. */
    if (RTCritSectIsInitialized(&g_TcpCritSect))
        RTCritSectDelete(&g_TcpCritSect);

    Log(("txsTcpTerm: done\n"));
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnInit}
 */
static DECLCALLBACK(int) txsTcpInit(void)
{
    int rc = RTCritSectInit(&g_TcpCritSect);
    if (RT_SUCCESS(rc) && g_enmTcpMode != TXSTCPMODE_CLIENT)
    {
        rc = RTTcpServerCreateEx(g_szTcpBindAddr[0] ? g_szTcpBindAddr : NULL, g_uTcpBindPort, &g_pTcpServer);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_NET_DOWN)
            {
                RTMsgInfo("RTTcpServerCreateEx(%s, %u,) failed: %Rrc, retrying for 20 seconds...\n",
                          g_szTcpBindAddr[0] ? g_szTcpBindAddr : NULL, g_uTcpBindPort, rc);
                uint64_t StartMs = RTTimeMilliTS();
                do
                {
                    RTThreadSleep(1000);
                    rc = RTTcpServerCreateEx(g_szTcpBindAddr[0] ? g_szTcpBindAddr : NULL, g_uTcpBindPort, &g_pTcpServer);
                } while (   rc == VERR_NET_DOWN
                         && RTTimeMilliTS() - StartMs < 20000);
                if (RT_SUCCESS(rc))
                    RTMsgInfo("RTTcpServerCreateEx succceeded.\n");
            }
            if (RT_FAILURE(rc))
            {
                g_pTcpServer = NULL;
                RTCritSectDelete(&g_TcpCritSect);
                RTMsgError("RTTcpServerCreateEx(%s, %u,) failed: %Rrc\n",
                           g_szTcpBindAddr[0] ? g_szTcpBindAddr : NULL, g_uTcpBindPort, rc);
            }
        }
    }

    return rc;
}

/** Options  */
enum TXSTCPOPT
{
    TXSTCPOPT_MODE = 1000,
    TXSTCPOPT_BIND_ADDRESS,
    TXSTCPOPT_BIND_PORT,
    TXSTCPOPT_CONNECT_ADDRESS,
    TXSTCPOPT_CONNECT_PORT,

    /* legacy: */
    TXSTCPOPT_LEGACY_PORT,
    TXSTCPOPT_LEGACY_CONNECT
};

/**
 * @interface_method_impl{TXSTRANSPORT,pfnOption}
 */
static DECLCALLBACK(int) txsTcpOption(int ch, PCRTGETOPTUNION pVal)
{
    int rc;

    switch (ch)
    {
        case TXSTCPOPT_MODE:
            if (!strcmp(pVal->psz, "both"))
                g_enmTcpMode = TXSTCPMODE_BOTH;
            else if (!strcmp(pVal->psz, "client"))
                 g_enmTcpMode = TXSTCPMODE_CLIENT;
            else if (!strcmp(pVal->psz, "server"))
                 g_enmTcpMode = TXSTCPMODE_SERVER;
            else
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "Invalid TCP mode: '%s'\n", pVal->psz);
            return VINF_SUCCESS;

        case TXSTCPOPT_BIND_ADDRESS:
            rc = RTStrCopy(g_szTcpBindAddr, sizeof(g_szTcpBindAddr), pVal->psz);
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "TCP bind address is too long (%Rrc)", rc);
            return VINF_SUCCESS;

        case TXSTCPOPT_BIND_PORT:
            g_uTcpBindPort = pVal->u16 == 0 ? TXS_TCP_DEF_BIND_PORT : pVal->u16;
            return VINF_SUCCESS;

        case TXSTCPOPT_LEGACY_CONNECT:
            g_enmTcpMode = TXSTCPMODE_CLIENT;
            RT_FALL_THRU();
        case TXSTCPOPT_CONNECT_ADDRESS:
            rc = RTStrCopy(g_szTcpConnectAddr, sizeof(g_szTcpConnectAddr), pVal->psz);
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "TCP connect address is too long (%Rrc)", rc);
            if (!g_szTcpConnectAddr[0])
                strcpy(g_szTcpConnectAddr, TXS_TCP_DEF_CONNECT_ADDRESS);
            return VINF_SUCCESS;

        case TXSTCPOPT_CONNECT_PORT:
            g_uTcpConnectPort = pVal->u16 == 0 ? TXS_TCP_DEF_CONNECT_PORT : pVal->u16;
            return VINF_SUCCESS;

        case TXSTCPOPT_LEGACY_PORT:
            if (pVal->u16 == 0)
            {
                g_uTcpBindPort      = TXS_TCP_DEF_BIND_PORT;
                g_uTcpConnectPort   = TXS_TCP_DEF_CONNECT_PORT;
            }
            else
            {
                g_uTcpBindPort      = pVal->u16;
                g_uTcpConnectPort   = pVal->u16;
            }
            return VINF_SUCCESS;
    }
    return VERR_TRY_AGAIN;
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnUsage}
 */
static DECLCALLBACK(void) txsTcpUsage(PRTSTREAM pStream)
{
    RTStrmPrintf(pStream,
                 "  --tcp-mode <both|client|server>\n"
                 "       Selects the mode of operation.\n"
                 "       Default: both\n"
                 "  --tcp-bind-address <address>\n"
                 "       The address(es) to listen to TCP connection on.  Empty string\n"
                 "       means any address, this is the default.\n"
                 "  --tcp-bind-port <port>\n"
                 "       The port to listen to TCP connections on.\n"
                 "       Default: %u\n"
                 "  --tcp-connect-address <address>\n"
                 "       The address of the server to try connect to in client mode.\n"
                 "       Default: " TXS_TCP_DEF_CONNECT_ADDRESS "\n"
                 "  --tcp-connect-port <port>\n"
                 "       The port on the server to connect to in client mode.\n"
                 "       Default: %u\n"
                 , TXS_TCP_DEF_BIND_PORT, TXS_TCP_DEF_CONNECT_PORT);
}

/** Command line options for the TCP/IP transport layer. */
static const RTGETOPTDEF  g_TcpOpts[] =
{
    { "--tcp-mode",             TXSTCPOPT_MODE,             RTGETOPT_REQ_STRING },
    { "--tcp-bind-address",     TXSTCPOPT_BIND_ADDRESS,     RTGETOPT_REQ_STRING },
    { "--tcp-bind-port",        TXSTCPOPT_BIND_PORT,        RTGETOPT_REQ_UINT16 },
    { "--tcp-connect-address",  TXSTCPOPT_CONNECT_ADDRESS,  RTGETOPT_REQ_STRING },
    { "--tcp-connect-port",     TXSTCPOPT_CONNECT_PORT,     RTGETOPT_REQ_UINT16 },

    /* legacy */
    { "--tcp-port",             TXSTCPOPT_LEGACY_PORT,      RTGETOPT_REQ_UINT16 },
    { "--tcp-connect",          TXSTCPOPT_LEGACY_CONNECT,   RTGETOPT_REQ_STRING },
};

/** TCP/IP transport layer. */
const TXSTRANSPORT g_TcpTransport =
{
    /* .szName          = */ "tcp",
    /* .pszDesc         = */ "TCP/IP",
    /* .cOpts           = */ &g_TcpOpts[0],
    /* .paOpts          = */ RT_ELEMENTS(g_TcpOpts),
    /* .pfnUsage        = */ txsTcpUsage,
    /* .pfnOption       = */ txsTcpOption,
    /* .pfnInit         = */ txsTcpInit,
    /* .pfnTerm         = */ txsTcpTerm,
    /* .pfnPollIn       = */ txsTcpPollIn,
    /* .pfnPollSetAdd   = */ txsTcpPollSetAdd,
    /* .pfnRecvPkt      = */ txsTcpRecvPkt,
    /* .pfnSendPkt      = */ txsTcpSendPkt,
    /* .pfnBabble       = */ txsTcpBabble,
    /* .pfnNotifyHowdy  = */ txsTcpNotifyHowdy,
    /* .pfnNotifyBye    = */ txsTcpNotifyBye,
    /* .pfnNotifyReboot = */ txsTcpNotifyReboot,
    /* .u32EndMarker    = */ UINT32_C(0x12345678)
};

