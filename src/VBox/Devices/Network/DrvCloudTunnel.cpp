/* $Id: DrvCloudTunnel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvCloudTunnel - Cloud tunnel network transport driver
 *
 * Based on code contributed by Christophe Devriese
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_CTUN
#include <VBox/log.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/uuid.h>
#include <iprt/req.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/critsect.h>

#include "VBoxDD.h"

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
typedef int socklen_t;
#else
# include <errno.h>
 typedef int SOCKET;
# define closesocket close
# define INVALID_SOCKET -1
# define SOCKET_ERROR   -1
DECLINLINE(int) WSAGetLastError() { return errno; }
#endif

/* Prevent inclusion of Winsock2.h */
#define _WINSOCK2API_
#include <libssh/libssh.h>
#include <libssh/callbacks.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Cloud tunnel driver instance data.
 *
 * @implements PDMINETWORKUP
 */
typedef struct DRVCLOUDTUNNEL
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The network interface. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** Cloud instance private key. */
    ssh_key                 SshKey;
    /** Cloud instance user. */
    char                    *pszUser;
    /** Cloud instance primary IP address. */
    char                    *pszPrimaryIP;
    /** Cloud instance primary IP address. */
    char                    *pszSecondaryIP;
    /** MAC address to set on cloud primary interface. */
    RTMAC                   targetMac;
    /** SSH connection timeout in seconds. */
    long                    ulTimeoutInSecounds;

    /** Primary proxy type. */
    char                    *pszPrimaryProxyType;
    /** Primary proxy server IP address. */
    char                    *pszPrimaryProxyHost;
    /** Primary proxy server port. */
    uint16_t                u16PrimaryProxyPort;
    /** Primary proxy user. */
    char                    *pszPrimaryProxyUser;
    /** Primary proxy password. */
    char                    *pszPrimaryProxyPassword;

    /** Secondary proxy type. */
    char                    *pszSecondaryProxyType;
    /** Secondary proxy server IP address. */
    char                    *pszSecondaryProxyHost;
    /** Secondary proxy server port. */
    uint16_t                u16SecondaryProxyPort;
    /** Secondary proxy user. */
    char                    *pszSecondaryProxyUser;
    /** Secondary proxy password. */
    char                    *pszSecondaryProxyPassword;

    /** Cloud tunnel instance string. */
    char                    *pszInstance;
    /** Cloud tunnel I/O thread unique name. */
    char                    *pszInstanceIo;
    /** Cloud tunnel device thread unique name. */
    char                    *pszInstanceDev;

    /** Command assembly buffer. */
    char                    *pszCommandBuffer;
    /** Command output buffer. */
    char                    *pszOutputBuffer;
    /** Name of primary interface of cloud instance. */
    char                    *pszCloudPrimaryInterface;

    /** Cloud destination address. */
    RTNETADDR               DestAddress;
    /** Transmit lock used by drvCloudTunnelUp_BeginXmit. */
    RTCRITSECT              XmitLock;
    /** Server data structure for Cloud communication. */
//    PRTCLOUDSERVER            pServer;

    /** RX thread for delivering packets to attached device. */
    PPDMTHREAD              pDevThread;
    /** Queue for device-thread requests. */
    RTREQQUEUE              hDevReqQueue;
    /** I/O thread for tunnel channel. */
    PPDMTHREAD              pIoThread;
    /** Queue for I/O-thread requests. */
    RTREQQUEUE              hIoReqQueue;
    /** I/O thread notification socket pair (in). */
    SOCKET                  iSocketIn;
    /** I/O thread notification socket pair (out). */
    SOCKET                  iSocketOut;

    /** SSH private key. */

    /** SSH Log Verbosity: 0 - No log, 1 - warnings, 2 - protocol, 3 - packet, 4 - functions */
    int                     iSshVerbosity;
    /** SSH Session. */
    ssh_session             pSshSession;
    /** SSH Tunnel Channel. */
    ssh_channel             pSshChannel;
    /** SSH Packet Receive Callback Structure. */
    struct ssh_channel_callbacks_struct Callbacks;

    /** Flag whether the link is down. */
    bool volatile           fLinkDown;

#ifdef VBOX_WITH_STATISTICS
    /** Number of sent packets. */
    STAMCOUNTER             StatPktSent;
    /** Number of sent bytes. */
    STAMCOUNTER             StatPktSentBytes;
    /** Number of received packets. */
    STAMCOUNTER             StatPktRecv;
    /** Number of received bytes. */
    STAMCOUNTER             StatPktRecvBytes;
    /** Profiling packet transmit runs. */
    STAMPROFILEADV          StatTransmit;
    /** Profiling packet receive runs. */
    STAMPROFILEADV          StatReceive;
    /** Profiling packet receive device (both actual receive and waiting). */
    STAMPROFILE             StatDevRecv;
    /** Profiling packet receive device waiting. */
    STAMPROFILE             StatDevRecvWait;
#endif /* VBOX_WITH_STATISTICS */

#ifdef LOG_ENABLED
    /** The nano ts of the last transfer. */
    uint64_t                u64LastTransferTS;
    /** The nano ts of the last receive. */
    uint64_t                u64LastReceiveTS;
#endif
} DRVCLOUDTUNNEL, *PDRVCLOUDTUNNEL;


/** Converts a pointer to CLOUDTUNNEL::INetworkUp to a PRDVCLOUDTUNNEL. */
#define PDMINETWORKUP_2_DRVCLOUDTUNNEL(pInterface) ( (PDRVCLOUDTUNNEL)((uintptr_t)pInterface - RT_UOFFSETOF(DRVCLOUDTUNNEL, INetworkUp)) )


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
static DECLCALLBACK(int) drvCloudTunnelUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVCLOUDTUNNEL pThis = PDMINETWORKUP_2_DRVCLOUDTUNNEL(pInterface);
    int rc = RTCritSectTryEnter(&pThis->XmitLock);
    if (RT_FAILURE(rc))
    {
        /** @todo XMIT thread */
        rc = VERR_TRY_AGAIN;
    }
    return rc;
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnAllocBuf}
 */
static DECLCALLBACK(int) drvCloudTunnelUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                 PCPDMNETWORKGSO pGso, PPPDMSCATTERGATHER ppSgBuf)
{
    PDRVCLOUDTUNNEL pThis = PDMINETWORKUP_2_DRVCLOUDTUNNEL(pInterface);
    Assert(RTCritSectIsOwner(&pThis->XmitLock)); NOREF(pThis);

    /*
     * Allocate a scatter / gather buffer descriptor that is immediately
     * followed by the buffer space of its single segment.  The GSO context
     * comes after that again.
     */
    PPDMSCATTERGATHER pSgBuf = (PPDMSCATTERGATHER)RTMemAlloc(  RT_ALIGN_Z(sizeof(*pSgBuf), 16)
                                                             + RT_ALIGN_Z(cbMin, 16)
                                                             + (pGso ? RT_ALIGN_Z(sizeof(*pGso), 16) : 0));
    if (!pSgBuf)
        return VERR_NO_MEMORY;

    /*
     * Initialize the S/G buffer and return.
     */
    pSgBuf->fFlags         = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1;
    pSgBuf->cbUsed         = 0;
    pSgBuf->cbAvailable    = RT_ALIGN_Z(cbMin, 16);
    pSgBuf->pvAllocator    = NULL;
    if (!pGso)
        pSgBuf->pvUser     = NULL;
    else
    {
        pSgBuf->pvUser     = (uint8_t *)(pSgBuf + 1) + pSgBuf->cbAvailable;
        *(PPDMNETWORKGSO)pSgBuf->pvUser = *pGso;
    }
    pSgBuf->cSegs          = 1;
    pSgBuf->aSegs[0].cbSeg = pSgBuf->cbAvailable;
    pSgBuf->aSegs[0].pvSeg = pSgBuf + 1;

#if 0 /* poison */
    memset(pSgBuf->aSegs[0].pvSeg, 'F', pSgBuf->aSegs[0].cbSeg);
#endif
    *ppSgBuf = pSgBuf;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnFreeBuf}
 */
static DECLCALLBACK(int) drvCloudTunnelUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    PDRVCLOUDTUNNEL pThis = PDMINETWORKUP_2_DRVCLOUDTUNNEL(pInterface);
    Assert(RTCritSectIsOwner(&pThis->XmitLock)); NOREF(pThis);
    if (pSgBuf)
    {
        Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
        pSgBuf->fFlags = 0;
        RTMemFree(pSgBuf);
    }
    return VINF_SUCCESS;
}

static int createConnectedSockets(PDRVCLOUDTUNNEL pThis)
{
    LogFlow(("%s: creating a pair of connected sockets...\n", pThis->pszInstance));
    struct sockaddr_in inaddr;
    struct sockaddr addr;
    SOCKET lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&inaddr, 0, sizeof(inaddr));
    memset(&addr, 0, sizeof(addr));
    inaddr.sin_family = AF_INET;
    inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    inaddr.sin_port = 0;
    int yes = 1;
    setsockopt(lst, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    bind(lst, (struct sockaddr *)&inaddr, sizeof(inaddr));
    listen(lst, 1);
    socklen_t len=sizeof(inaddr);
    getsockname(lst, &addr, &len);
    pThis->iSocketOut = socket(AF_INET, SOCK_STREAM, 0);
    connect(pThis->iSocketOut, &addr, len);
    pThis->iSocketIn = accept(lst, 0, 0);
    closesocket(lst);
    Log2(("%s: socket(%d) <= socket(%d) created successfully.\n", pThis->pszInstance, pThis->iSocketIn, pThis->iSocketOut));
    return VINF_SUCCESS;
}


static void destroyConnectedSockets(PDRVCLOUDTUNNEL pThis)
{
    if (pThis->iSocketOut != INVALID_SOCKET)
    {
        LogFlow(("%s: destroying output socket (%d)...\n", pThis->pszInstance, pThis->iSocketOut));
        closesocket(pThis->iSocketOut);
    }
    if (pThis->iSocketIn != INVALID_SOCKET)
    {
        LogFlow(("%s: destroying input socket (%d)...\n", pThis->pszInstance, pThis->iSocketIn));
        closesocket(pThis->iSocketIn);
    }
}


DECLINLINE(void) drvCloudTunnelFreeSgBuf(PDRVCLOUDTUNNEL pThis, PPDMSCATTERGATHER pSgBuf)
{
    RT_NOREF(pThis);
    RTMemFree(pSgBuf);
}

DECLINLINE(void) drvCloudTunnelNotifyIoThread(PDRVCLOUDTUNNEL pThis, const char *pszWho)
{
    RT_NOREF(pszWho);
    int cBytes = send(pThis->iSocketOut, " ", 1, 0);
    if (cBytes == SOCKET_ERROR)
        LogRel(("Failed to send a signalling packet, error code %d", WSAGetLastError())); // @todo!

}


/**
 * Worker function for sending packets on I/O thread.
 *
 * @param   pThis               Pointer to the cloud tunnel instance.
 * @param   pSgBuf              The scatter/gather buffer.
 * @thread  I/O
 */
static DECLCALLBACK(void) drvCloudTunnelSendWorker(PDRVCLOUDTUNNEL pThis, PPDMSCATTERGATHER pSgBuf)
{
    // int rc = VINF_SUCCESS;
    if (!pSgBuf->pvUser)
    {
#ifdef LOG_ENABLED
        uint64_t u64Now = RTTimeProgramNanoTS();
        LogFunc(("%-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                 pSgBuf->cbUsed, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
        pThis->u64LastTransferTS = u64Now;
#endif
        Log2(("writing to tunnel channel: pSgBuf->aSegs[0].pvSeg=%p pSgBuf->cbUsed=%#x\n%.*Rhxd\n",
              pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, pSgBuf->cbUsed, pSgBuf->aSegs[0].pvSeg));

        int cBytes = ssh_channel_write(pThis->pSshChannel, pSgBuf->aSegs[0].pvSeg, (uint32_t)pSgBuf->cbUsed);
        if (cBytes == SSH_ERROR)
            LogRel(("%s: ssh_channel_write failed\n", pThis->pszInstance));
    }
    else
    {
        uint8_t         abHdrScratch[256];
        uint8_t const  *pbFrame = (uint8_t const *)pSgBuf->aSegs[0].pvSeg;
        PCPDMNETWORKGSO pGso    = (PCPDMNETWORKGSO)pSgBuf->pvUser;
        uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, pSgBuf->cbUsed);  Assert(cSegs > 1);
        for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
        {
            uint32_t cbSegFrame;
            void *pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)pbFrame, pSgBuf->cbUsed, abHdrScratch,
                                                       iSeg, cSegs, &cbSegFrame);
            Log2(("writing to tunnel channel: pvSegFrame=%p cbSegFrame=%#x\n%.*Rhxd\n",
                pvSegFrame, cbSegFrame, cbSegFrame, pvSegFrame));
            int cBytes = ssh_channel_write(pThis->pSshChannel, pvSegFrame, cbSegFrame);
            if (cBytes == SSH_ERROR)
                LogRel(("%s: ssh_channel_write failed\n", pThis->pszInstance));
        }
    }

    pSgBuf->fFlags = 0;
    RTMemFree(pSgBuf);

    STAM_PROFILE_ADV_STOP(&pThis->StatTransmit, a);
    // AssertRC(rc);
    // if (RT_FAILURE(rc))
    // {
    //     if (rc == VERR_NO_MEMORY)
    //         rc = VERR_NET_NO_BUFFER_SPACE;
    //     else
    //         rc = VERR_NET_DOWN;
    // }
    // return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
static DECLCALLBACK(int) drvCloudTunnelUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVCLOUDTUNNEL pThis = PDMINETWORKUP_2_DRVCLOUDTUNNEL(pInterface);
    STAM_COUNTER_INC(&pThis->StatPktSent);
    STAM_COUNTER_ADD(&pThis->StatPktSentBytes, pSgBuf->cbUsed);
    STAM_PROFILE_ADV_START(&pThis->StatTransmit, a);

    AssertPtr(pSgBuf);
    Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));

    int rc = VINF_SUCCESS;
    if (pThis->pIoThread && pThis->pIoThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        Log2(("%s: submitting TX request (pvSeg=%p, %u bytes) to I/O queue...\n",
              pThis->pszInstance, pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed));
        rc = RTReqQueueCallEx(pThis->hIoReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvCloudTunnelSendWorker, 2, pThis, pSgBuf);

        if (RT_SUCCESS(rc))
        {
            drvCloudTunnelNotifyIoThread(pThis, "drvCloudTunnelUp_SendBuf");
            return VINF_SUCCESS;
        }

        rc = VERR_NET_NO_BUFFER_SPACE;
    }
    else
        rc = VERR_NET_DOWN;
    drvCloudTunnelFreeSgBuf(pThis, pSgBuf);
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
static DECLCALLBACK(void) drvCloudTunnelUp_EndXmit(PPDMINETWORKUP pInterface)
{
    PDRVCLOUDTUNNEL pThis = PDMINETWORKUP_2_DRVCLOUDTUNNEL(pInterface);
    RTCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
static DECLCALLBACK(void) drvCloudTunnelUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    RT_NOREF(pInterface, fPromiscuous);
    LogFlowFunc(("fPromiscuous=%d\n", fPromiscuous));
    /* nothing to do */
}


/**
 * Notification on link status changes.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   enmLinkState    The new link state.
 * @thread  EMT
 */
static DECLCALLBACK(void) drvCloudTunnelUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    LogFlowFunc(("enmLinkState=%d\n", enmLinkState));
    PDRVCLOUDTUNNEL pThis = PDMINETWORKUP_2_DRVCLOUDTUNNEL(pInterface);

    bool fLinkDown;
    switch (enmLinkState)
    {
        case PDMNETWORKLINKSTATE_DOWN:
        case PDMNETWORKLINKSTATE_DOWN_RESUME:
            fLinkDown = true;
            break;
        default:
            AssertMsgFailed(("enmLinkState=%d\n", enmLinkState));
            RT_FALL_THRU();
        case PDMNETWORKLINKSTATE_UP:
            fLinkDown = false;
            break;
    }
    ASMAtomicXchgSize(&pThis->fLinkDown, fLinkDown);
}



/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvCloudTunnelQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUp);
    return NULL;
}


/**
 * I/O thread handling the libssh I/O.
 *
 * The libssh implementation is single-threaded so we perform I/O in a
 * dedicated thread. We take care that this thread does not become the
 * bottleneck: If the guest wants to send, a request is enqueued into the
 * hIoReqQueue and is handled asynchronously by this thread.  TODO:If this thread
 * wants to deliver packets to the guest, it enqueues a request into
 * hRecvReqQueue which is later handled by the Recv thread.
 */
static DECLCALLBACK(int) drvCloudTunnelIoThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);
    // int     nFDs = -1;

    LogFlow(("%s: started I/O thread %p\n", pThis->pszInstance, pThread));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    // if (pThis->enmLinkStateWant != pThis->enmLinkState)
    //     drvNATNotifyLinkChangedWorker(pThis, pThis->enmLinkStateWant);

    /*
     * Polling loop.
     */
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * To prevent concurrent execution of sending/receiving threads
         */
//#ifndef RT_OS_WINDOWS
        // /* process _all_ outstanding requests but don't wait */
        // RTReqQueueProcess(pThis->hIoReqQueue, 0);
        // RTMemFree(polls);
//#else /* RT_OS_WINDOWS */

        struct timeval timeout;
        ssh_channel in_channels[2], out_channels[2];
        fd_set fds;
        int maxfd;

        timeout.tv_sec = 30;
        timeout.tv_usec = 0;
        in_channels[0] = pThis->pSshChannel;
        in_channels[1] = NULL;
        FD_ZERO(&fds);
        FD_SET(pThis->iSocketIn, &fds);
        maxfd = pThis->iSocketIn + 1;

        ssh_select(in_channels, out_channels, maxfd, &fds, &timeout);

        /* Poll will call the receive callback on each packet coming from the tunnel. */
        if (out_channels[0] != NULL)
            ssh_channel_poll(pThis->pSshChannel, false);

        /* Did we get notified by drvCloudTunnelNotifyIoThread() via connected sockets? */
        if (FD_ISSET(pThis->iSocketIn, &fds))
        {
            char buf[2];
            recv(pThis->iSocketIn, buf, 1, 0);
            /* process all outstanding requests but don't wait */
            RTReqQueueProcess(pThis->hIoReqQueue, 0);
        }
//#endif /* RT_OS_WINDOWS */
    }

    LogFlow(("%s: I/O thread %p terminated\n", pThis->pszInstance, pThread));

    return VINF_SUCCESS;
}


/**
 * Unblock the I/O thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The pcnet device instance.
 * @param   pThread     The send thread.
 */
static DECLCALLBACK(int) drvCloudTunnelIoWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);

    LogFlow(("%s: waking up I/O thread %p...\n", pThis->pszInstance, pThread));

    drvCloudTunnelNotifyIoThread(pThis, "drvCloudTunnelIoWakeup");
    return VINF_SUCCESS;
}


/*
 * Remove the following cut&paste code after a while, when
 * we are positive that no frames get coalesced!
 */
#define VBOX_CTUN_COALESCED_FRAME_DETECTION
#ifdef VBOX_CTUN_COALESCED_FRAME_DETECTION
struct ssh_buffer_struct {
    bool secure;
    size_t used;
    size_t allocated;
    size_t pos;
    uint8_t *data;
};

/**  @internal
 * Describes the different possible states in a
 * outgoing (client) channel request
 */
enum ssh_channel_request_state_e {
    /** No request has been made */
    SSH_CHANNEL_REQ_STATE_NONE = 0,
    /** A request has been made and answer is pending */
    SSH_CHANNEL_REQ_STATE_PENDING,
    /** A request has been replied and accepted */
    SSH_CHANNEL_REQ_STATE_ACCEPTED,
    /** A request has been replied and refused */
    SSH_CHANNEL_REQ_STATE_DENIED,
    /** A request has been replied and an error happend */
    SSH_CHANNEL_REQ_STATE_ERROR
};

enum ssh_channel_state_e {
  SSH_CHANNEL_STATE_NOT_OPEN = 0,
  SSH_CHANNEL_STATE_OPENING,
  SSH_CHANNEL_STATE_OPEN_DENIED,
  SSH_CHANNEL_STATE_OPEN,
  SSH_CHANNEL_STATE_CLOSED
};

/* The channel has been closed by the remote side */
#define SSH_CHANNEL_FLAG_CLOSED_REMOTE 0x0001

/* The channel has been closed locally */
#define SSH_CHANNEL_FLAG_CLOSED_LOCAL 0x0002

/* The channel has been freed by the calling program */
#define SSH_CHANNEL_FLAG_FREED_LOCAL 0x0004

/* the channel has not yet been bound to a remote one */
#define SSH_CHANNEL_FLAG_NOT_BOUND 0x0008

struct ssh_channel_struct {
    ssh_session session; /* SSH_SESSION pointer */
    uint32_t local_channel;
    uint32_t local_window;
    int local_eof;
    uint32_t local_maxpacket;

    uint32_t remote_channel;
    uint32_t remote_window;
    int remote_eof; /* end of file received */
    uint32_t remote_maxpacket;
    enum ssh_channel_state_e state;
    int delayed_close;
    int flags;
    ssh_buffer stdout_buffer;
    ssh_buffer stderr_buffer;
    void *userarg;
    int exit_status;
    enum ssh_channel_request_state_e request_state;
    struct ssh_list *callbacks; /* list of ssh_channel_callbacks */

    /* counters */
    ssh_counter counter;
};
#endif /* VBOX_CTUN_COALESCED_FRAME_DETECTION */

/**
 * Worker function for delivering receive packets to the attached device.
 *
 * @param   pThis               Pointer to the cloud tunnel instance.
 * @param   pbData              Packet data.
 * @param   u32Len              Packet length.
 * @thread  Dev
 */
static DECLCALLBACK(void) drvCloudTunnelReceiveWorker(PDRVCLOUDTUNNEL pThis, uint8_t *pbData, uint32_t u32Len)
{
    AssertPtrReturnVoid(pbData);
    AssertReturnVoid(u32Len!=0);

    STAM_PROFILE_START(&pThis->StatDevRecv, a);

    Log2(("%s: waiting until device is ready to receive...\n", pThis->pszInstance));
    STAM_PROFILE_START(&pThis->StatDevRecvWait, b);
    int rc = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
    STAM_PROFILE_STOP(&pThis->StatDevRecvWait, b);

    if (RT_SUCCESS(rc))
    {
        Log2(("%s: delivering %u-byte packet to attached device...\n", pThis->pszInstance, u32Len));
        rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, pbData, u32Len);
        AssertRC(rc);
    }

    RTMemFree(pbData);
    STAM_PROFILE_STOP(&pThis->StatDevRecv, a);
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
}

static int drvCloudTunnelReceiveCallback(ssh_session session, ssh_channel channel, void* data, uint32_t len, int is_stderr, void* userdata)
{
    RT_NOREF(session);
    PDRVCLOUDTUNNEL pThis = (PDRVCLOUDTUNNEL)userdata;

    Log2(("drvCloudTunnelReceiveCallback: len=%d is_stderr=%s\n", len, is_stderr ? "true" : "false"));
    if (ASMAtomicReadBool(&pThis->fLinkDown))
    {
        Log2(("drvCloudTunnelReceiveCallback: ignoring packet as the link is down\n"));
        return len;
    }

#ifdef VBOX_CTUN_COALESCED_FRAME_DETECTION
    if (channel->stdout_buffer->data != data)
        LogRel(("drvCloudTunnelReceiveCallback: coalesced frames!\n"));
#endif /* VBOX_CTUN_COALESCED_FRAME_DETECTION */

    if (is_stderr)
    {
        LogRel(("%s: [REMOTE] %.*s", pThis->pszInstance, len, data));
        return 0;
    }

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

    if (pThis->iSshVerbosity >= SSH_LOG_PACKET)
        Log2(("%.*Rhxd\n", len, data));

    /** @todo Validate len! */
    void *pvPacket = RTMemDup(data, len);
    if (!pvPacket)
    {
        LogRel(("%s: failed to allocate %d bytes\n", pThis->pszInstance, len));
        STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
        return len;
    }
    int rc = RTReqQueueCallEx(pThis->hDevReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvCloudTunnelReceiveWorker, 3, pThis, pvPacket, len);
    if (RT_FAILURE(rc))
    {
        LogRel(("%s: failed to enqueue device request - %Rrc\n", pThis->pszInstance, rc));
        STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
    }

    return len;
}


/* See ssh_channel_write_wontblock_callback in libssh/callbacks.h. */
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0,10,0)
static int channelWriteWontblockCallback(ssh_session, ssh_channel, uint32_t, void *)
#else
static int channelWriteWontblockCallback(ssh_session, ssh_channel, size_t, void *)
#endif
{
    return 0;
}



/**
 * This thread feeds the attached device with the packets received from the tunnel.
 *
 * This thread is needed because we cannot block I/O thread waiting for the attached
 * device to become ready to receive packets coming from the tunnel.
 */
static DECLCALLBACK(int) drvCloudTunnelDevThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);

    LogFlow(("%s: device thread %p started\n", pThis->pszInstance, pThread));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    /*
     * Request processing loop.
     */
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        int rc = RTReqQueueProcess(pThis->hDevReqQueue, RT_INDEFINITE_WAIT);
        Log2(("drvCloudTunnelDevThread: RTReqQueueProcess returned '%Rrc'\n", rc));
        if (RT_FAILURE(rc))
            LogRel(("%s: failed to process device request with '%Rrc'\n", pThis->pszInstance, rc));
    }

    LogFlow(("%s: device thread %p terminated\n", pThis->pszInstance, pThread));
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) drvCloudTunnelReceiveWakeup(PDRVCLOUDTUNNEL pThis)
{
    NOREF(pThis);
    /* Returning a VINF_* will cause RTReqQueueProcess return. */
    return VWRN_STATE_CHANGED;
}

/**
 * Unblock the I/O thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The pcnet device instance.
 * @param   pThread     The send thread.
 */
static DECLCALLBACK(int) drvCloudTunnelDevWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);
    LogFlow(("%s: waking up device thread %p...\n", pThis->pszInstance, pThread));

    /* Wake up device thread. */
    PRTREQ pReq;
    int rc = RTReqQueueCall(pThis->hDevReqQueue, &pReq, 10000 /*cMillies*/, (PFNRT)drvCloudTunnelReceiveWakeup, 1, pThis);
    if (RT_FAILURE(rc))
        LogRel(("%s: failed to wake up device thread - %Rrc\n", pThis->pszInstance, rc));
    if (RT_SUCCESS(rc))
        RTReqRelease(pReq);

    return rc;
}

#define DRVCLOUDTUNNEL_COMMAND_BUFFER_SIZE 1024
#define DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE 65536

static int drvCloudTunnelExecuteRemoteCommandNoOutput(PDRVCLOUDTUNNEL pThis, const char *pcszCommand, ...)
{
    va_list va;
    va_start(va, pcszCommand);
    ssize_t const cch = RTStrPrintf2V(pThis->pszCommandBuffer, DRVCLOUDTUNNEL_COMMAND_BUFFER_SIZE, pcszCommand, va);
    va_end(va);
    if (cch <= 0)
    {
        Log(("%s: Failed to process '%s'\n", pThis->pszInstance, pcszCommand));
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to compose command line"));
    }

    LogFlow(("%s: [REMOTE] executing '%s'...\n", pThis->pszInstance, pThis->pszCommandBuffer));

    ssh_channel channel = ssh_channel_new(pThis->pSshSession);
    if (channel == NULL)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to allocate new channel"));

    int rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK)
        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                 N_("Failed to open session channel"));
    else
    {
        rc = ssh_channel_request_exec(channel, pThis->pszCommandBuffer);
        if (rc != SSH_OK)
        {
            LogRel(("%s: Failed to execute '%s'\n", pThis->pszInstance, pThis->pszCommandBuffer));
            Log(("%s: Failed to execute '%s'\n", pThis->pszInstance, pThis->pszCommandBuffer));
            rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                     N_("Execute request failed with %d"), rc);
        }
        ssh_channel_close(channel);
    }
    ssh_channel_free(channel);

    return rc;
}


static int drvCloudTunnelExecuteRemoteCommand(PDRVCLOUDTUNNEL pThis, const char *pcszCommand, ...)
{
    va_list va;
    va_start(va, pcszCommand);
    ssize_t const cch = RTStrPrintf2V(pThis->pszCommandBuffer, DRVCLOUDTUNNEL_COMMAND_BUFFER_SIZE, pcszCommand, va);
    va_end(va);
    if (cch <= 0)
    {
        Log(("%s: Failed to process '%s'\n", pThis->pszInstance, pcszCommand));
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to compose command line"));
    }

    LogFlow(("%s: [REMOTE] executing '%s'...\n", pThis->pszInstance, pThis->pszCommandBuffer));

    ssh_channel channel = ssh_channel_new(pThis->pSshSession);
    if (channel == NULL)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to allocate new channel"));

    int rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK)
        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                 N_("Failed to open session channel"));
    else
    {
        rc = ssh_channel_request_exec(channel, pThis->pszCommandBuffer);
        if (rc != SSH_OK)
        {
            LogRel(("%s: Failed to execute '%s'\n", pThis->pszInstance, pThis->pszCommandBuffer));
            Log(("%s: Failed to execute '%s'\n", pThis->pszInstance, pThis->pszCommandBuffer));
            rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                     N_("Execute request failed with %d"), rc);
        }
        else
        {
            int cbSpaceLeft = DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE;
            int cbStdOut = 0;
            char *pszBuffer = pThis->pszOutputBuffer;
            int cBytes = ssh_channel_read_timeout(channel, pszBuffer, cbSpaceLeft, 0, 60000 /* ms */); /* Is 60 seconds really enough? */
            while (cBytes > 0)
            {
                cbStdOut += cBytes;
                pszBuffer += cBytes;
                cbSpaceLeft -= cBytes;
                if (cbSpaceLeft <= 0)
                    break;
                cBytes = ssh_channel_read_timeout(channel, pszBuffer, cbSpaceLeft, 0, 60000 /* ms */); /* Is 60 seconds really enough? */
            }
            if (cBytes < 0)
            {
                LogRel(("%s: while executing '%s' ssh_channel_read_timeout returned error\n", pThis->pszInstance, pThis->pszCommandBuffer));
                Log(("%s: while executing '%s' ssh_channel_read_timeout returned error\n", pThis->pszInstance, pThis->pszCommandBuffer));
                rc = VERR_INTERNAL_ERROR;
            }
            else
            {
                /* Make sure the buffer is terminated. */
                if (cbStdOut < DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE)
                    if (cbStdOut > 1 && pThis->pszOutputBuffer[cbStdOut - 1] == '\n')
                        pThis->pszOutputBuffer[cbStdOut - 1] = 0; /* Trim newline */
                    else
                        pThis->pszOutputBuffer[cbStdOut] = 0;
                else
                    pThis->pszOutputBuffer[DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE - 1] = 0; /* No choice but to eat up last character. Could have returned warning though. */
                if (cbStdOut == 0)
                    Log(("%s: received no output from remote console\n", pThis->pszInstance));
                else
                    Log(("%s: received output from remote console:\n%s\n", pThis->pszInstance, pThis->pszOutputBuffer));
                rc = VINF_SUCCESS;

                char *pszErrorBuffer = (char *)RTMemAlloc(DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE);
                if (pszErrorBuffer == NULL)
                {
                    LogRel(("%s: Failed to allocate error buffer\n", pThis->pszInstance));
                    rc = VERR_INTERNAL_ERROR;
                }
                else
                {
                    /* Report errors if there were any */
                    cBytes = ssh_channel_read_timeout(channel, pszErrorBuffer, DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE, 1, 0); /* Peek at stderr */
                    if (cBytes > 0)
                    {
                        LogRel(("%s: WARNING! While executing '%s' remote console reported errors:\n", pThis->pszInstance, pThis->pszCommandBuffer));
                        Log(("%s: WARNING! While executing '%s' remote console reported errors:\n", pThis->pszInstance, pThis->pszCommandBuffer));
                    }
                    while (cBytes > 0)
                    {
                        LogRel(("%.*s", cBytes, pszErrorBuffer));
                        Log(("%.*s", cBytes, pszErrorBuffer));
                        cBytes = ssh_channel_read_timeout(channel, pszErrorBuffer, DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE, 1, 1000); /* Wait for a second for more error output */
                    }
                    RTMemFree(pszErrorBuffer);
                }
            }
            ssh_channel_send_eof(channel);
        }
        ssh_channel_close(channel);
    }
    ssh_channel_free(channel);

    return rc;
}


static int drvCloudTunnelCloudInstanceInitialConfig(PDRVCLOUDTUNNEL pThis)
{
    LogFlow(("%s: configuring cloud instance...\n", pThis->pszInstance));

    int rc = drvCloudTunnelExecuteRemoteCommand(pThis, "python3 -c \"from oci_utils.vnicutils import VNICUtils; cfg = VNICUtils().get_network_config(); print('CONFIG:', [i['IFACE'] for i in cfg if 'IS_PRIMARY' in i][0], [i['IFACE']+' '+i['VIRTRT'] for i in cfg if not 'IS_PRIMARY' in i][0])\"");
    if (RT_FAILURE(rc))
        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                    N_("Failed to get network config via console channel"));
    else
    {
        char *pszConfig = RTStrStr(pThis->pszOutputBuffer, "CONFIG: ");
        if (!pszConfig)
            rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                     N_("Failed to parse network config"));
        else
        {
            char **ppapszTokens;
            size_t cTokens;
            rc = RTStrSplit(pszConfig + 8, DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE - (pszConfig - pThis->pszOutputBuffer) - 8,
                            " ", &ppapszTokens, &cTokens);
            if (RT_SUCCESS(rc))
            {
                /*
                * There should be exactly three tokens:
                * 1) Primary network interface name;
                * 2) Secondary network interface name;
                * 3) Secondary network gateway address.
                */
                if (cTokens != 3)
                    Log(("%s: Got %u tokes instead of three while parsing '%s'\n", pThis->pszInstance, cTokens, pThis->pszOutputBuffer));
                else
                {
                    char *pszSecondaryInterface = NULL;
                    char *pszSecondaryGateway   = NULL;

                    if (pThis->pszCloudPrimaryInterface)
                        RTStrFree(pThis->pszCloudPrimaryInterface);
                    pThis->pszCloudPrimaryInterface = RTStrDup(ppapszTokens[0]);
                    pszSecondaryInterface           = ppapszTokens[1];
                    pszSecondaryGateway             = ppapszTokens[2];
                    Log(("%s: primary=%s secondary=%s gateway=%s\n", pThis->pszInstance, pThis->pszCloudPrimaryInterface, pszSecondaryInterface, pszSecondaryGateway));

                    rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo oci-network-config -c");
                    if (RT_SUCCESS(rc))
                        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip tuntap add dev tap0 mod tap user opc");
                    if (RT_SUCCESS(rc))
                        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo sh -c 'echo \"PermitTunnel yes\" >> /etc/ssh/sshd_config'");
                    if (RT_SUCCESS(rc))
                        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo kill -SIGHUP $(pgrep -f \"sshd -D\")");
                    if (RT_SUCCESS(rc))
                        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link add name br0 type bridge");
                    if (RT_SUCCESS(rc))
                        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link set dev tap0 master br0");
                    if (RT_SUCCESS(rc))
                        rc = drvCloudTunnelExecuteRemoteCommandNoOutput(pThis, "sudo ip route change default via %s dev %s", pszSecondaryGateway, pszSecondaryInterface);
                    if (RT_FAILURE(rc))
                        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                                N_("Failed to execute network config command via console channel"));
                }

                for (size_t i = 0; i < cTokens; i++)
                    RTStrFree(ppapszTokens[i]);
                RTMemFree(ppapszTokens);
            }
        }
    }

    return rc;
}


static int drvCloudTunnelCloudInstanceFinalConfig(PDRVCLOUDTUNNEL pThis)
{
    if (pThis->pszCloudPrimaryInterface == NULL)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to finalize cloud instance config because of unknown primary interface name!"));

    LogFlow(("%s: finalizing cloud instance configuration...\n", pThis->pszInstance));

    int rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link set dev %s down", pThis->pszCloudPrimaryInterface);
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link set dev %s address %RTmac", pThis->pszCloudPrimaryInterface, pThis->targetMac.au8);
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ifconfig %s 0.0.0.0", pThis->pszCloudPrimaryInterface); /* Make sure no IP is configured on primary */
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link set dev %s master br0", pThis->pszCloudPrimaryInterface);
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link set dev %s up", pThis->pszCloudPrimaryInterface);
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link set dev tap0 up");
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelExecuteRemoteCommand(pThis, "sudo ip link set dev br0 up");
    if (RT_FAILURE(rc))
        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                N_("Failed to execute network config command via console channel"));

    return rc;
}


static int drvCloudTunnelOpenTunnelChannel(PDRVCLOUDTUNNEL pThis)
{
    LogFlow(("%s: opening tunnel channel...\n", pThis->pszInstance));
    pThis->pSshChannel = ssh_channel_new(pThis->pSshSession);
    if (pThis->pSshChannel == NULL)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to allocate new channel"));
    int rc = ssh_channel_open_tunnel(pThis->pSshChannel, 0);
    if (rc < 0)
        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to open tunnel channel"));
    else
    {
        /* Set packet receive callback. */
        rc = ssh_set_channel_callbacks(pThis->pSshChannel, &pThis->Callbacks);
        if (rc != SSH_OK)
            rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                     N_("Failed to set packet receive callback"));
    }

    return rc;
}


static void closeTunnelChannel(PDRVCLOUDTUNNEL pThis)
{
    if (pThis->pSshChannel)
    {
        LogFlow(("%s: closing tunnel channel %p\n", pThis->pszInstance, pThis->pSshChannel));
        ssh_channel_close(pThis->pSshChannel);
        ssh_channel_free(pThis->pSshChannel);
        pThis->pSshChannel = NULL;
    }
}


static int drvCloudTunnelStartIoThread(PDRVCLOUDTUNNEL pThis)
{
    LogFlow(("%s: starting I/O thread...\n", pThis->pszInstance));
    int rc = createConnectedSockets(pThis);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to create a pair of connected sockets"));

    /*
     * Start the cloud I/O thread.
     */
    rc = PDMDrvHlpThreadCreate(pThis->pDrvIns, &pThis->pIoThread,
                               pThis, drvCloudTunnelIoThread, drvCloudTunnelIoWakeup,
                               64 * _1K, RTTHREADTYPE_IO, pThis->pszInstanceIo);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to start I/O thread"));

    return rc;
}

static void drvCloudTunnelStopIoThread(PDRVCLOUDTUNNEL pThis)
{
    if (pThis->pIoThread)
    {
        LogFlow(("%s: stopping I/O thread...\n", pThis->pszInstance));
        int rc = PDMDrvHlpThreadDestroy(pThis->pDrvIns, pThis->pIoThread, NULL);
        AssertRC(rc);
        pThis->pIoThread = NULL;
    }
    destroyConnectedSockets(pThis);

}

static int destroyTunnel(PDRVCLOUDTUNNEL pThis)
{
    if (pThis->pSshChannel)
    {
        int rc = ssh_remove_channel_callbacks(pThis->pSshChannel, &pThis->Callbacks);
        if (rc != SSH_OK)
            LogRel(("%s: WARNING! Failed to remove tunnel channel callbacks.\n", pThis->pszInstance));
    }
    drvCloudTunnelStopIoThread(pThis);
    closeTunnelChannel(pThis);
    ssh_disconnect(pThis->pSshSession);
    ssh_free(pThis->pSshSession);
    pThis->pSshSession = NULL;
    return VINF_SUCCESS;
}


static int drvCloudTunnelNewSession(PDRVCLOUDTUNNEL pThis, bool fPrimary)
{
    pThis->pSshSession = ssh_new();
    if (pThis->pSshSession == NULL)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to allocate new SSH session"));
    if (ssh_options_set(pThis->pSshSession, SSH_OPTIONS_LOG_VERBOSITY, &pThis->iSshVerbosity) < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to set SSH_OPTIONS_LOG_VERBOSITY"));
    if (ssh_options_set(pThis->pSshSession, SSH_OPTIONS_USER, pThis->pszUser) < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to set SSH_OPTIONS_USER"));
    if (ssh_options_set(pThis->pSshSession, SSH_OPTIONS_HOST, fPrimary ? pThis->pszPrimaryIP : pThis->pszSecondaryIP) < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to set SSH_OPTIONS_HOST"));

    if (ssh_options_set(pThis->pSshSession, SSH_OPTIONS_TIMEOUT, &pThis->ulTimeoutInSecounds) < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to set SSH_OPTIONS_TIMEOUT"));

    const char *pcszProxyType = fPrimary ? pThis->pszPrimaryProxyType : pThis->pszSecondaryProxyType;
    if (pcszProxyType)
    {
        char szProxyCmd[1024];

        const char *pcszProxyUser = fPrimary ? pThis->pszPrimaryProxyUser : pThis->pszSecondaryProxyUser;
        if (pcszProxyUser)
            RTStrPrintf(szProxyCmd, sizeof(szProxyCmd), "#VBoxProxy%s %s %u %s %s",
                        fPrimary ? pThis->pszPrimaryProxyType : pThis->pszSecondaryProxyType,
                        fPrimary ? pThis->pszPrimaryProxyHost : pThis->pszSecondaryProxyHost,
                        fPrimary ? pThis->u16PrimaryProxyPort : pThis->u16SecondaryProxyPort,
                        fPrimary ? pThis->pszPrimaryProxyUser : pThis->pszSecondaryProxyUser,
                        fPrimary ? pThis->pszPrimaryProxyPassword : pThis->pszSecondaryProxyPassword);
        else
            RTStrPrintf(szProxyCmd, sizeof(szProxyCmd), "#VBoxProxy%s %s %u",
                        fPrimary ? pThis->pszPrimaryProxyType : pThis->pszSecondaryProxyType,
                        fPrimary ? pThis->pszPrimaryProxyHost : pThis->pszSecondaryProxyHost,
                        fPrimary ? pThis->u16PrimaryProxyPort : pThis->u16SecondaryProxyPort);
        LogRel(("%s: using proxy command '%s'\n", pThis->pszInstance, szProxyCmd));
        if (ssh_options_set(pThis->pSshSession, SSH_OPTIONS_PROXYCOMMAND, szProxyCmd) < 0)
            return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                    N_("Failed to set SSH_OPTIONS_PROXYCOMMAND"));
    }

    int rc = ssh_connect(pThis->pSshSession);
    for (int cAttempt = 1; rc != SSH_OK && cAttempt <= 5; cAttempt++)
    {
        ssh_disconnect(pThis->pSshSession);
        /* One more time, just to be sure. */
        LogRel(("%s: failed to connect to %s, retrying(#%d)...\n", pThis->pszInstance,
                fPrimary ? pThis->pszPrimaryIP : pThis->pszSecondaryIP, cAttempt));
        RTThreadSleep(10000); /* Sleep 10 seconds, then retry */
        rc = ssh_connect(pThis->pSshSession);
    }
    if (rc != SSH_OK)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to connect to %s interface"), fPrimary ? "primary" : "secondary");

    rc = ssh_userauth_publickey(pThis->pSshSession, NULL, pThis->SshKey);
    if (rc != SSH_AUTH_SUCCESS)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to authenticate with public key"));

    return VINF_SUCCESS;
}

static int drvCloudTunnelSwitchToSecondary(PDRVCLOUDTUNNEL pThis)
{
    int rc = drvCloudTunnelNewSession(pThis, true /* fPrimary */);
    /*
     * Establish temporary console channel and configure the cloud instance
     * to bridge the tunnel channel to instance's primary interface.
     */
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelCloudInstanceInitialConfig(pThis);

    ssh_disconnect(pThis->pSshSession);
    ssh_free(pThis->pSshSession);
    pThis->pSshSession = NULL;

    return rc;
}


static int establishTunnel(PDRVCLOUDTUNNEL pThis)
{
    int rc = drvCloudTunnelNewSession(pThis, false /* fPrimary */);
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelCloudInstanceFinalConfig(pThis);
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelOpenTunnelChannel(pThis);
    if (RT_SUCCESS(rc))
        rc = drvCloudTunnelStartIoThread(pThis);
    if (RT_FAILURE(rc))
    {
        destroyTunnel(pThis);
        return rc;
    }

    return rc;
}


static DECL_NOTHROW(void) drvCloudTunnelSshLogCallback(int priority, const char *function, const char *buffer, void *userdata)
{
    PDRVCLOUDTUNNEL pThis = (PDRVCLOUDTUNNEL)userdata;
#ifdef LOG_ENABLED
    const char *pcszVerbosity;
    switch (priority)
    {
        case SSH_LOG_WARNING:
            pcszVerbosity = "WARNING";
            break;
        case SSH_LOG_PROTOCOL:
            pcszVerbosity = "PROTOCOL";
            break;
        case SSH_LOG_PACKET:
            pcszVerbosity = "PACKET";
            break;
        case SSH_LOG_FUNCTIONS:
            pcszVerbosity = "FUNCTIONS";
            break;
        default:
            pcszVerbosity = "UNKNOWN";
            break;
    }
    Log3(("%s: SSH-%s: %s: %s\n", pThis->pszInstance, pcszVerbosity, function, buffer));
#else
    RT_NOREF(priority);
    LogRel(("%s: SSH %s: %s\n", pThis->pszInstance, function, buffer));
#endif
}

/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

DECLINLINE(void) drvCloudTunnelStrFree(char **ppszString)
{
    if (*ppszString)
    {
        RTStrFree(*ppszString);
        *ppszString = NULL;
    }
}

DECLINLINE(void) drvCloudTunnelHeapFree(PPDMDRVINS pDrvIns, char **ppszString)
{
    if (*ppszString)
    {
        PDMDrvHlpMMHeapFree(pDrvIns, *ppszString);
        *ppszString = NULL;
    }
}

/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvCloudTunnelDestruct(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    ASMAtomicXchgSize(&pThis->fLinkDown, true);

    destroyTunnel(pThis);

    if (pThis->hIoReqQueue != NIL_RTREQQUEUE)
    {
        RTReqQueueDestroy(pThis->hIoReqQueue);
        pThis->hIoReqQueue = NIL_RTREQQUEUE;
    }

    drvCloudTunnelStrFree(&pThis->pszCloudPrimaryInterface);

    drvCloudTunnelHeapFree(pDrvIns, &pThis->pszPrimaryProxyType);
    drvCloudTunnelStrFree(&pThis->pszPrimaryProxyHost);
    drvCloudTunnelHeapFree(pDrvIns, &pThis->pszPrimaryProxyUser);
    drvCloudTunnelStrFree(&pThis->pszPrimaryProxyPassword);

    drvCloudTunnelHeapFree(pDrvIns, &pThis->pszSecondaryProxyType);
    drvCloudTunnelStrFree(&pThis->pszSecondaryProxyHost);
    drvCloudTunnelHeapFree(pDrvIns, &pThis->pszSecondaryProxyUser);
    drvCloudTunnelStrFree(&pThis->pszSecondaryProxyPassword);

    drvCloudTunnelStrFree(&pThis->pszSecondaryIP);
    drvCloudTunnelStrFree(&pThis->pszPrimaryIP);
    drvCloudTunnelStrFree(&pThis->pszUser);

    drvCloudTunnelStrFree(&pThis->pszInstanceDev);
    drvCloudTunnelStrFree(&pThis->pszInstanceIo);
    drvCloudTunnelStrFree(&pThis->pszInstance);

    drvCloudTunnelStrFree(&pThis->pszOutputBuffer);
    drvCloudTunnelStrFree(&pThis->pszCommandBuffer);

    ssh_key_free(pThis->SshKey);

    ssh_finalize();
    //OPENSSL_cleanup();

    // if (pThis->pServer)
    // {
    //     RTUdpServerDestroy(pThis->pServer);
    //     pThis->pServer = NULL;
    // }

    /*
     * Kill the xmit lock.
     */
    if (RTCritSectIsInitialized(&pThis->XmitLock))
        RTCritSectDelete(&pThis->XmitLock);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Deregister statistics.
     */
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktSent);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktSentBytes);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktRecv);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktRecvBytes);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatTransmit);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReceive);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatDevRecv);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatDevRecvWait);
#endif /* VBOX_WITH_STATISTICS */
}


/**
 * Construct a Cloud tunnel network transport driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvCloudTunnelConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    pThis->pszCommandBuffer             = NULL;
    pThis->pszOutputBuffer              = NULL;
    pThis->pszInstance                  = NULL;
    pThis->pszPrimaryIP                 = NULL;
    pThis->pszSecondaryIP               = NULL;
    pThis->pszUser                      = NULL;
    pThis->SshKey                       = 0;

    /* IBase */
    pDrvIns->IBase.pfnQueryInterface            = drvCloudTunnelQueryInterface;
    /* INetwork */
    pThis->INetworkUp.pfnBeginXmit              = drvCloudTunnelUp_BeginXmit;
    pThis->INetworkUp.pfnAllocBuf               = drvCloudTunnelUp_AllocBuf;
    pThis->INetworkUp.pfnFreeBuf                = drvCloudTunnelUp_FreeBuf;
    pThis->INetworkUp.pfnSendBuf                = drvCloudTunnelUp_SendBuf;
    pThis->INetworkUp.pfnEndXmit                = drvCloudTunnelUp_EndXmit;
    pThis->INetworkUp.pfnSetPromiscuousMode     = drvCloudTunnelUp_SetPromiscuousMode;
    pThis->INetworkUp.pfnNotifyLinkChanged      = drvCloudTunnelUp_NotifyLinkChanged;

    /* ??? */
    pThis->iSocketIn   = INVALID_SOCKET;
    pThis->iSocketOut  = INVALID_SOCKET;
    pThis->pSshSession = 0;
    pThis->pSshChannel = 0;

    pThis->pDevThread  = 0;
    pThis->pIoThread   = 0;
    pThis->hIoReqQueue = NIL_RTREQQUEUE;

    pThis->fLinkDown   = false;

    pThis->pszCloudPrimaryInterface = NULL;

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSent,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of sent packets.",          "/Drivers/CloudTunnel%d/Packets/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSentBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of sent bytes.",            "/Drivers/CloudTunnel%d/Bytes/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecv,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of received packets.",      "/Drivers/CloudTunnel%d/Packets/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecvBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of received bytes.",        "/Drivers/CloudTunnel%d/Bytes/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatTransmit,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet transmit runs.",  "/Drivers/CloudTunnel%d/Transmit", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReceive,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet receive runs.",   "/Drivers/CloudTunnel%d/Receive", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatDevRecv,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling device receive runs.",   "/Drivers/CloudTunnel%d/DeviceReceive", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatDevRecvWait,   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling device receive waits.",  "/Drivers/CloudTunnel%d/DeviceReceiveWait", pDrvIns->iInstance);
#endif /* VBOX_WITH_STATISTICS */

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,  "SshKey"
                                            "|PrimaryIP"
                                            "|SecondaryIP"
                                            "|TargetMAC"

                                            "|PrimaryProxyType"
                                            "|PrimaryProxyHost"
                                            "|PrimaryProxyPort"
                                            "|PrimaryProxyUser"
                                            "|PrimaryProxyPassword"
                                            "|SecondaryProxyType"
                                            "|SecondaryProxyHost"
                                            "|SecondaryProxyPort"
                                            "|SecondaryProxyUser"
                                            "|SecondaryProxyPassword"

                                            ,"");

    /*
     * Check that no-one is attached to us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Query the network port interface.
     */
    pThis->pIAboveNet = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKDOWN);
    if (!pThis->pIAboveNet)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE,
                                N_("Configuration error: The above device/driver didn't export the network port interface"));

    /*
     * Read the configuration.
     */
    int rc;

    char szVal[2048];
    RTNETADDRIPV4 tmpAddr;
    rc = pHlp->pfnCFGMQueryString(pCfg, "PrimaryIP", szVal, sizeof(szVal));
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: Querying \"PrimaryIP\" as string failed"));
    rc = RTNetStrToIPv4Addr(szVal, &tmpAddr);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: \"PrimaryIP\" is not valid"));
    else
        pThis->pszPrimaryIP = RTStrDup(szVal);

    rc = pHlp->pfnCFGMQueryString(pCfg, "SecondaryIP", szVal, sizeof(szVal));
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: Querying \"SecondaryIP\" as string failed"));
    rc = RTNetStrToIPv4Addr(szVal, &tmpAddr);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: \"SecondaryIP\" is not valid"));
    else
        pThis->pszSecondaryIP = RTStrDup(szVal);
    rc = pHlp->pfnCFGMQueryBytes(pCfg, "TargetMAC", pThis->targetMac.au8, sizeof(pThis->targetMac.au8));
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: Failed to get target MAC address"));
    /** @todo In the near future we will want to include proxy settings here! */
    // Do we want to pass the user name via CFGM?
    pThis->pszUser = RTStrDup("opc");
    // Is it safe to expose verbosity via CFGM?
#ifdef LOG_ENABLED
    pThis->iSshVerbosity = SSH_LOG_PACKET; //SSH_LOG_FUNCTIONS;
#else
    pThis->iSshVerbosity = SSH_LOG_WARNING;
#endif

    pThis->ulTimeoutInSecounds = 30;    /* The default 10-second timeout is too short? */

    rc = pHlp->pfnCFGMQueryPassword(pCfg, "SshKey", szVal, sizeof(szVal));
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: Querying \"SshKey\" as password failed"));
    rc = ssh_pki_import_privkey_base64(szVal, NULL, NULL, NULL, &pThis->SshKey);
    RTMemWipeThoroughly(szVal, sizeof(szVal), 10);
    if (rc != SSH_OK)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_INVALID_BASE64_ENCODING,
                                N_("DrvCloudTunnel: Configuration error: Converting \"SshKey\" from base64 failed"));

    /* PrimaryProxyType is optional */
    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "PrimaryProxyType", &pThis->pszPrimaryProxyType, NULL);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: Querying \"PrimaryProxyType\" as string failed"));
    if (pThis->pszPrimaryProxyType)
    {
        rc = pHlp->pfnCFGMQueryString(pCfg, "PrimaryProxyHost", szVal, sizeof(szVal));
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: Querying \"PrimaryProxyHost\" as string failed"));
        rc = RTNetStrToIPv4Addr(szVal, &tmpAddr);
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: \"PrimaryProxyHost\" is not valid"));
        else
            pThis->pszPrimaryProxyHost = RTStrDup(szVal);

        uint64_t u64Val;
        rc = pHlp->pfnCFGMQueryInteger(pCfg, "PrimaryProxyPort", &u64Val);
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: Querying \"PrimaryProxyPort\" as integer failed"));
        if (u64Val > 0xFFFF)
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: \"PrimaryProxyPort\" is not valid"));
        pThis->u16PrimaryProxyPort = (uint16_t)u64Val;

        /* PrimaryProxyUser is optional */
        rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "PrimaryProxyUser", &pThis->pszPrimaryProxyUser, NULL);
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: Querying \"PrimaryProxyUser\" as string failed"));
        /* PrimaryProxyPassword must be present if PrimaryProxyUser is present */
        if (pThis->pszPrimaryProxyUser)
        {
            rc = pHlp->pfnCFGMQueryPassword(pCfg, "PrimaryProxyPassword", szVal, sizeof(szVal));
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc,
                                        N_("DrvCloudTunnel: Configuration error: Querying \"PrimaryProxyPassword\" as string failed"));
            pThis->pszPrimaryProxyPassword = RTStrDup(szVal);
        }
    }

    /* SecondaryProxyType is optional */
    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "SecondaryProxyType", &pThis->pszSecondaryProxyType, NULL);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("DrvCloudTunnel: Configuration error: Querying \"SecondaryProxyType\" as string failed"));
    if (pThis->pszSecondaryProxyType)
    {
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: Querying \"SecondaryProxyType\" as string failed"));

        rc = pHlp->pfnCFGMQueryString(pCfg, "SecondaryProxyHost", szVal, sizeof(szVal));
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: Querying \"SecondaryProxyHost\" as string failed"));
        rc = RTNetStrToIPv4Addr(szVal, &tmpAddr);
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: \"SecondaryProxyHost\" is not valid"));
        else
            pThis->pszSecondaryProxyHost = RTStrDup(szVal);

        uint64_t u64Val;
        rc = pHlp->pfnCFGMQueryInteger(pCfg, "SecondaryProxyPort", &u64Val);
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: Querying \"SecondaryProxyPort\" as integer failed"));
        if (u64Val > 0xFFFF)
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: \"SecondaryProxyPort\" is not valid"));
        pThis->u16SecondaryProxyPort = (uint16_t)u64Val;

        /* SecondaryProxyUser is optional */
        rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "SecondaryProxyUser", &pThis->pszSecondaryProxyUser, NULL);
        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("DrvCloudTunnel: Configuration error: Querying \"SecondaryProxyUser\" as string failed"));
        /* SecondaryProxyPassword must be present if SecondaryProxyUser is present */
        if (pThis->pszSecondaryProxyUser)
        {
            rc = pHlp->pfnCFGMQueryPassword(pCfg, "SecondaryProxyPassword", szVal, sizeof(szVal));
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc,
                                        N_("DrvCloudTunnel: Configuration error: Querying \"SecondaryProxyPassword\" as string failed"));
            pThis->pszSecondaryProxyPassword = RTStrDup(szVal);
        }
    }

    pThis->pszCommandBuffer = (char *)RTMemAlloc(DRVCLOUDTUNNEL_COMMAND_BUFFER_SIZE);
    if (pThis->pszCommandBuffer == NULL)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_HIF_OPEN_FAILED,
                                N_("DrvCloudTunnel: Failed to allocate command buffer"));
    pThis->pszOutputBuffer = (char *)RTMemAlloc(DRVCLOUDTUNNEL_OUTPUT_BUFFER_SIZE);
    if (pThis->pszOutputBuffer == NULL)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_HIF_OPEN_FAILED,
                                N_("DrvCloudTunnel: Failed to allocate output buffer"));
    /*
     * Create unique instance name for logging.
     */
    rc = RTStrAPrintf(&pThis->pszInstance, "CT#%d", pDrvIns->iInstance);
    AssertRC(rc);

    LogRel(("%s: primary=%s secondary=%s target-mac=%RTmac\n", pThis->pszInstance, pThis->pszPrimaryIP, pThis->pszSecondaryIP, pThis->targetMac.au8));

    /*
     * Create unique thread name for cloud I/O.
     */
    rc = RTStrAPrintf(&pThis->pszInstanceIo, "CTunIO%d", pDrvIns->iInstance);
    AssertRC(rc);

    /*
     * Create unique thread name for device receive function.
     */
    rc = RTStrAPrintf(&pThis->pszInstanceDev, "CTunDev%d", pDrvIns->iInstance);
    AssertRC(rc);

    /*
     * Create the transmit lock.
     */
    rc = RTCritSectInit(&pThis->XmitLock);
    AssertRCReturn(rc, rc);

    /*
     * Create the request queue for I/O requests.
     */
    rc = RTReqQueueCreate(&pThis->hIoReqQueue);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Create the request queue for attached device requests.
     */
    rc = RTReqQueueCreate(&pThis->hDevReqQueue);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Start the device output thread.
     */
    rc = PDMDrvHlpThreadCreate(pThis->pDrvIns, &pThis->pDevThread,
                               pThis, drvCloudTunnelDevThread, drvCloudTunnelDevWakeup,
                               64 * _1K, RTTHREADTYPE_IO, pThis->pszInstanceDev);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to start device thread"));

    rc = ssh_init();
    if (rc != SSH_OK)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to initialize libssh"));

    memset(&pThis->Callbacks, 0, sizeof(pThis->Callbacks));
#ifdef PACKET_CAPTURE_ENABLED
    pThis->Callbacks.channel_data_function = drvCloudTunnelReceiveCallbackWithPacketCapture;
#else
    pThis->Callbacks.channel_data_function = drvCloudTunnelReceiveCallback;
#endif
    pThis->Callbacks.userdata = pThis;
    pThis->Callbacks.channel_write_wontblock_function = channelWriteWontblockCallback;
    ssh_callbacks_init(&pThis->Callbacks);

    rc = ssh_set_log_callback(drvCloudTunnelSshLogCallback);
    if (rc != SSH_OK)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to set libssh log callback"));
    rc = ssh_set_log_userdata(pThis);
    if (rc != SSH_OK)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("CloudTunnel: Failed to set libssh log userdata"));

    rc = drvCloudTunnelSwitchToSecondary(pThis);
    if (RT_SUCCESS(rc))
        rc = establishTunnel(pThis);

    return rc;
}


#if 0
/**
 * Suspend notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvCloudTunnelSuspend(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);

    RT_NOREF(pThis);
    // if (pThis->pServer)
    // {
    //     RTUdpServerDestroy(pThis->pServer);
    //     pThis->pServer = NULL;
    // }
}


/**
 * Resume notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvCloudTunnelResume(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PDRVCLOUDTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVCLOUDTUNNEL);

    int rc = RTUdpServerCreate("", pThis->uSrcPort, RTTHREADTYPE_IO, pThis->pszInstance,
                               drvCloudTunnelReceive, pDrvIns, &pThis->pServer);
    if (RT_FAILURE(rc))
        PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                            N_("CloudTunnel: Failed to start the Cloud tunnel server"));

}
#endif

/**
 * Cloud tunnel network transport driver registration record.
 */
const PDMDRVREG g_DrvCloudTunnel =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "CloudTunnel",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Cloud Tunnel Network Transport Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVCLOUDTUNNEL),
    /* pfnConstruct */
    drvCloudTunnelConstruct,
    /* pfnDestruct */
    drvCloudTunnelDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL, // drvCloudTunnelSuspend,
    /* pfnResume */
    NULL, // drvCloudTunnelResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

