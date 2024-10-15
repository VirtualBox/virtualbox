/* $Id: UsbTestServiceTcp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestService - Remote USB test configuration and execution server, TCP/IP Transport Layer.
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

#include "UsbTestServiceInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The default server port. */
#define UTS_TCP_DEF_BIND_PORT                   6042
/** The default server bind address. */
#define UTS_TCP_DEF_BIND_ADDRESS                ""


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * TCP specific client data.
 */
typedef struct UTSTRANSPORTCLIENT
{
    /** Socket of the current client. */
    RTSOCKET             hTcpClient;
    /** The size of the stashed data. */
    size_t               cbTcpStashed;
    /** The size of the stashed data allocation. */
    size_t               cbTcpStashedAlloced;
    /** The stashed data. */
    uint8_t             *pbTcpStashed;
} UTSTRANSPORTCLIENT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name TCP Parameters
 * @{ */
/** The addresses to bind to.  Empty string means any.  */
static char                 g_szTcpBindAddr[256]    = UTS_TCP_DEF_BIND_ADDRESS;
/** The TCP port to listen to. */
static uint32_t             g_uTcpBindPort          = UTS_TCP_DEF_BIND_PORT;
/** @} */

/** Pointer to the TCP server instance. */
static PRTTCPSERVER         g_pTcpServer            = NULL;
#if 0 /* unused */
/** Stop connecting attempts when set. */
static bool                 g_fTcpStopConnecting    = false;
#endif



/**
 * Disconnects the current client and frees all stashed data.
 */
static void utsTcpDisconnectClient(PUTSTRANSPORTCLIENT pClient)
{
    if (pClient->hTcpClient != NIL_RTSOCKET)
    {
        int rc = RTTcpServerDisconnectClient2(pClient->hTcpClient);
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
 * @interface_method_impl{UTSTRANSPORT,pfnWaitForConnect}
 */
static DECLCALLBACK(int) utsTcpWaitForConnect(PPUTSTRANSPORTCLIENT ppClientNew)
{
    int rc;
    RTSOCKET hClientNew;

    rc = RTTcpServerListen2(g_pTcpServer, &hClientNew);
    Log(("utsTcpWaitForConnect: RTTcpServerListen2 -> %Rrc\n", rc));

    if (RT_SUCCESS(rc))
    {
        PUTSTRANSPORTCLIENT pClient = (PUTSTRANSPORTCLIENT)RTMemAllocZ(sizeof(UTSTRANSPORTCLIENT));
        if (RT_LIKELY(pClient))
        {
            pClient->hTcpClient          = hClientNew;
            pClient->cbTcpStashed        = 0;
            pClient->cbTcpStashedAlloced = 0;
            pClient->pbTcpStashed        = NULL;
            *ppClientNew = pClient;
        }
        else
        {
            RTTcpServerDisconnectClient2(hClientNew);
            rc = VERR_NO_MEMORY;
        }
    }

    return rc;
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnNotifyReboot}
 */
static DECLCALLBACK(void) utsTcpNotifyReboot(void)
{
    Log(("utsTcpNotifyReboot: RTTcpServerDestroy(%p)\n", g_pTcpServer));
    if (g_pTcpServer)
    {
        int rc = RTTcpServerDestroy(g_pTcpServer);
        if (RT_FAILURE(rc))
            RTMsgInfo("RTTcpServerDestroy failed in utsTcpNotifyReboot: %Rrc", rc);
        g_pTcpServer = NULL;
    }
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnNotifyBye}
 */
static DECLCALLBACK(void) utsTcpNotifyBye(PUTSTRANSPORTCLIENT pClient)
{
    Log(("utsTcpNotifyBye: utsTcpDisconnectClient %RTsock\n", pClient->hTcpClient));
    utsTcpDisconnectClient(pClient);
    RTMemFree(pClient);
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnNotifyHowdy}
 */
static DECLCALLBACK(void) utsTcpNotifyHowdy(PUTSTRANSPORTCLIENT pClient)
{
    /* nothing to do here */
    RT_NOREF1(pClient);
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnBabble}
 */
static DECLCALLBACK(void) utsTcpBabble(PUTSTRANSPORTCLIENT pClient, PCUTSPKTHDR pPktHdr, RTMSINTERVAL cMsSendTimeout)
{
    /*
     * Try send the babble reply.
     */
    NOREF(cMsSendTimeout); /** @todo implement the timeout here; non-blocking write + select-on-write. */
    int     rc;
    size_t  cbToSend = RT_ALIGN_Z(pPktHdr->cb, UTSPKT_ALIGNMENT);
    do  rc = RTTcpWrite(pClient->hTcpClient, pPktHdr, cbToSend);
    while (rc == VERR_INTERRUPTED);

    /*
     * Disconnect the client.
     */
    Log(("utsTcpBabble: utsTcpDisconnectClient(%RTsock) (RTTcpWrite rc=%Rrc)\n", pClient->hTcpClient, rc));
    utsTcpDisconnectClient(pClient);
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnSendPkt}
 */
static DECLCALLBACK(int) utsTcpSendPkt(PUTSTRANSPORTCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    Assert(pPktHdr->cb >= sizeof(UTSPKTHDR));

    /*
     * Write it.
     */
    size_t cbToSend = RT_ALIGN_Z(pPktHdr->cb, UTSPKT_ALIGNMENT);
    int rc = RTTcpWrite(pClient->hTcpClient, pPktHdr, cbToSend);
    if (    RT_FAILURE(rc)
        &&  rc != VERR_INTERRUPTED)
    {
        /* assume fatal connection error. */
        Log(("RTTcpWrite -> %Rrc -> utsTcpDisconnectClient(%RTsock)\n", rc, pClient->hTcpClient));
        utsTcpDisconnectClient(pClient);
    }

    return rc;
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnRecvPkt}
 */
static DECLCALLBACK(int) utsTcpRecvPkt(PUTSTRANSPORTCLIENT pClient, PPUTSPKTHDR ppPktHdr)
{
    int rc = VINF_SUCCESS;
    *ppPktHdr = NULL;

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
        cbDataAlloced = RT_ALIGN_Z(64,  UTSPKT_ALIGNMENT);
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
        rc = RTTcpRead(pClient->hTcpClient, pbData + offData, sizeof(uint32_t) - offData, &cbRead);
        if (RT_FAILURE(rc))
            break;
        if (cbRead == 0)
        {
            Log(("utsTcpRecvPkt: RTTcpRead -> %Rrc / cbRead=0 -> VERR_NET_NOT_CONNECTED (#1)\n", rc));
            rc = VERR_NET_NOT_CONNECTED;
            break;
        }
        offData += cbRead;
    }
    if (RT_SUCCESS(rc))
    {
        ASMCompilerBarrier(); /* paranoia^3 */
        cbData = *(uint32_t volatile *)pbData;
        if (cbData >= sizeof(UTSPKTHDR) && cbData <= UTSPKT_MAX_SIZE)
        {
            /*
             * Align the length and reallocate the return packet it necessary.
             */
            cbData = RT_ALIGN_Z(cbData, UTSPKT_ALIGNMENT);
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
                        Log(("utsTcpRecvPkt: RTTcpRead -> %Rrc / cbRead=0 -> VERR_NET_NOT_CONNECTED (#2)\n", rc));
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
        *ppPktHdr = (PUTSPKTHDR)pbData;
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

            /* assume fatal connection error. */
            Log(("utsTcpRecvPkt: RTTcpRead -> %Rrc -> utsTcpDisconnectClient(%RTsock)\n", rc, pClient->hTcpClient));
            utsTcpDisconnectClient(pClient);
        }
    }

    return rc;
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnPollSetAdd}
 */
static DECLCALLBACK(int) utsTcpPollSetAdd(RTPOLLSET hPollSet, PUTSTRANSPORTCLIENT pClient, uint32_t idStart)
{
    return RTPollSetAddSocket(hPollSet, pClient->hTcpClient, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, idStart);
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnPollSetRemove}
 */
static DECLCALLBACK(int) utsTcpPollSetRemove(RTPOLLSET hPollSet, PUTSTRANSPORTCLIENT pClient, uint32_t idStart)
{
    RT_NOREF1(pClient);
    return RTPollSetRemove(hPollSet, idStart);
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnPollIn}
 */
static DECLCALLBACK(bool) utsTcpPollIn(PUTSTRANSPORTCLIENT pClient)
{
    int rc = RTTcpSelectOne(pClient->hTcpClient, 0/*cMillies*/);
    return RT_SUCCESS(rc);
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnTerm}
 */
static DECLCALLBACK(void) utsTcpTerm(void)
{
    /* Shut down the server (will wake up thread). */
    if (g_pTcpServer)
    {
        Log(("utsTcpTerm: Destroying server...\n"));
        int rc = RTTcpServerDestroy(g_pTcpServer);
        if (RT_FAILURE(rc))
            RTMsgInfo("RTTcpServerDestroy failed in utsTcpTerm: %Rrc", rc);
        g_pTcpServer        = NULL;
    }

    Log(("utsTcpTerm: done\n"));
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnInit}
 */
static DECLCALLBACK(int) utsTcpInit(void)
{
    int rc = RTTcpServerCreateEx(g_szTcpBindAddr[0] ? g_szTcpBindAddr : NULL, g_uTcpBindPort, &g_pTcpServer);
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
            RTMsgError("RTTcpServerCreateEx(%s, %u,) failed: %Rrc\n",
                       g_szTcpBindAddr[0] ? g_szTcpBindAddr : NULL, g_uTcpBindPort, rc);
        }
    }

    return rc;
}

/** Options  */
enum UTSTCPOPT
{
    UTSTCPOPT_BIND_ADDRESS = 1000,
    UTSTCPOPT_BIND_PORT
};

/**
 * @interface_method_impl{UTSTRANSPORT,pfnOption}
 */
static DECLCALLBACK(int) utsTcpOption(int ch, PCRTGETOPTUNION pVal)
{
    int rc;

    switch (ch)
    {
        case UTSTCPOPT_BIND_ADDRESS:
            rc = RTStrCopy(g_szTcpBindAddr, sizeof(g_szTcpBindAddr), pVal->psz);
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "TCP bind address is too long (%Rrc)", rc);
            return VINF_SUCCESS;

        case UTSTCPOPT_BIND_PORT:
            g_uTcpBindPort = pVal->u16 == 0 ? UTS_TCP_DEF_BIND_PORT : pVal->u16;
            return VINF_SUCCESS;
    }
    return VERR_TRY_AGAIN;
}

/**
 * @interface_method_impl{UTSTRANSPORT,pfnUsage}
 */
static DECLCALLBACK(void) utsTcpUsage(PRTSTREAM pStream)
{
    RTStrmPrintf(pStream,
                 "  --tcp-bind-address <address>\n"
                 "       The address(es) to listen to TCP connection on.  Empty string\n"
                 "       means any address, this is the default.\n"
                 "  --tcp-bind-port <port>\n"
                 "       The port to listen to TCP connections on.\n"
                 "       Default: %u\n"
                 , UTS_TCP_DEF_BIND_PORT);
}

/** Command line options for the TCP/IP transport layer. */
static const RTGETOPTDEF  g_TcpOpts[] =
{
    { "--tcp-bind-address",     UTSTCPOPT_BIND_ADDRESS,     RTGETOPT_REQ_STRING },
    { "--tcp-bind-port",        UTSTCPOPT_BIND_PORT,        RTGETOPT_REQ_UINT16 }
};

/** TCP/IP transport layer. */
const UTSTRANSPORT g_TcpTransport =
{
    /* .szName            = */ "tcp",
    /* .pszDesc           = */ "TCP/IP",
    /* .cOpts             = */ &g_TcpOpts[0],
    /* .paOpts            = */ RT_ELEMENTS(g_TcpOpts),
    /* .pfnUsage          = */ utsTcpUsage,
    /* .pfnOption         = */ utsTcpOption,
    /* .pfnInit           = */ utsTcpInit,
    /* .pfnTerm           = */ utsTcpTerm,
    /* .pfnWaitForConnect = */ utsTcpWaitForConnect,
    /* .pfnPollIn         = */ utsTcpPollIn,
    /* .pfnPollSetAdd     = */ utsTcpPollSetAdd,
    /* .pfnPollSetRemove  = */ utsTcpPollSetRemove,
    /* .pfnRecvPkt        = */ utsTcpRecvPkt,
    /* .pfnSendPkt        = */ utsTcpSendPkt,
    /* .pfnBabble         = */ utsTcpBabble,
    /* .pfnNotifyHowdy    = */ utsTcpNotifyHowdy,
    /* .pfnNotifyBye      = */ utsTcpNotifyBye,
    /* .pfnNotifyReboot   = */ utsTcpNotifyReboot,
    /* .u32EndMarker      = */ UINT32_C(0x12345678)
};
