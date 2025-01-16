/* $Id: DrvNATlibslirp.cpp 107914 2025-01-16 14:50:32Z knut.osmundsen@oracle.com $ */
/** @file
 * DrvNATlibslirp - NATlibslirp network transport driver.
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
#define LOG_GROUP LOG_GROUP_DRV_NAT
#define RTNET_INCL_IN_ADDR
#include "VBoxDD.h"

#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
# include <iprt/win/ws2tcpip.h>
# include "winutils.h"
# define inet_aton(x, y) inet_pton(2, x, y)
# define AF_INET6 23
#endif

#include <libslirp.h>

#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>

#ifndef RT_OS_WINDOWS
# include <unistd.h>
# include <fcntl.h>
# include <poll.h>
# include <errno.h>
#endif

#ifdef RT_OS_FREEBSD
# include <netinet/in.h>
#endif

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/cidr.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/net.h>
#include <iprt/pipe.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/time.h>
#include <iprt/uuid.h>

#include <iprt/asm.h>

#include <iprt/semaphore.h>
#include <iprt/req.h>
#ifdef RT_OS_DARWIN
# include <SystemConfiguration/SystemConfiguration.h>
# include <CoreFoundation/CoreFoundation.h>
#endif

#define COUNTERS_INIT
#include "slirp/counters.h"
#include "slirp/resolv_conf_parser.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define DRVNAT_MAXFRAMESIZE (16 * 1024)
#define DRVNAT_DEFAULT_TIMEOUT (3600*1000)
#define MAX_IP_ADDRESS_STR_LEN_W_NULL 16

#define GET_EXTRADATA(pdrvins, node, name, rc, type, type_name, var)                                  \
    do {                                                                                                \
        (rc) = (pdrvins)->pHlpR3->pfnCFGMQuery ## type((node), name, &(var));                                               \
        if (RT_FAILURE((rc)) && (rc) != VERR_CFGM_VALUE_NOT_FOUND)                                      \
            return PDMDrvHlpVMSetError((pdrvins), (rc), RT_SRC_POS, \
                                       N_("NAT#%d: configuration query for \"" name "\" " #type_name " failed"), \
                                       (pdrvins)->iInstance);                                    \
    } while (0)

#define GET_ED_STRICT(pdrvins, node, name, rc, type, type_name, var)                                  \
    do {                                                                                                \
        (rc) = (pdrvins)->pHlpR3->pfnCFGMQuery ## type((node), name, &(var));                                               \
        if (RT_FAILURE((rc)))                                                                           \
            return PDMDrvHlpVMSetError((pdrvins), (rc), RT_SRC_POS, \
                                       N_("NAT#%d: configuration query for \"" name "\" " #type_name " failed"), \
                                       (pdrvins)->iInstance);                                     \
    } while (0)

#define GET_EXTRADATA_N(pdrvins, node, name, rc, type, type_name, var, var_size)                      \
    do {                                                                                                \
        (rc) = (pdrvins)->pHlpR3->pfnCFGMQuery ## type((node), name, &(var), var_size);                                     \
        if (RT_FAILURE((rc)) && (rc) != VERR_CFGM_VALUE_NOT_FOUND)                                      \
            return PDMDrvHlpVMSetError((pdrvins), (rc), RT_SRC_POS, \
                                       N_("NAT#%d: configuration query for \"" name "\" " #type_name " failed"), \
                                       (pdrvins)->iInstance);                                     \
    } while (0)

#define GET_BOOL(rc, pdrvins, node, name, var) \
    GET_EXTRADATA(pdrvins, node, name, (rc), Bool, bolean, (var))
#define GET_STRING(rc, pdrvins, node, name, var, var_size) \
    GET_EXTRADATA_N(pdrvins, node, name, (rc), String, string, (var), (var_size))
#define GET_STRING_ALLOC(rc, pdrvins, node, name, var) \
    GET_EXTRADATA(pdrvins, node, name, (rc), StringAlloc, string, (var))
#define GET_S32(rc, pdrvins, node, name, var) \
    GET_EXTRADATA(pdrvins, node, name, (rc), S32, int, (var))
#define GET_S32_STRICT(rc, pdrvins, node, name, var) \
    GET_ED_STRICT(pdrvins, node, name, (rc), S32, int, (var))

#define DO_GET_IP(rc, node, instance, status, x)                                \
    do {                                                                            \
        char    sz##x[32];                                                          \
        GET_STRING((rc), (node), (instance), #x, sz ## x[0],  sizeof(sz ## x));     \
        if (rc != VERR_CFGM_VALUE_NOT_FOUND)                                        \
            (status) = inet_aton(sz ## x, &x);                                      \
    } while (0)

#define GETIP_DEF(rc, node, instance, x, def)           \
    do                                                      \
    {                                                       \
        int status = 0;                                     \
        DO_GET_IP((rc), (node), (instance),  status, x);    \
        if (status == 0 || rc == VERR_CFGM_VALUE_NOT_FOUND) \
            x.s_addr = def;                                 \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Slirp Timer */
typedef struct slirpTimer
{
    struct slirpTimer *next;
    uint32_t uTimeExpire;
    SlirpTimerCb pHandler;
    void *opaque;
} SlirpTimer;

/**
 * Main state of Libslirp NAT
 */
typedef struct SlirpState
{
    unsigned int nsock;

    Slirp *pSlirp;
    struct pollfd *polls;

    /** Num Polls (not bytes) */
    unsigned int uPollCap = 0;

    SlirpTimer *pTimerHead;
    bool fPassDomain;
} SlirpState;
typedef SlirpState *pSlirpState;

/**
 * NAT network transport driver instance data.
 *
 * @implements  PDMINETWORKUP
 */
typedef struct DRVNAT
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The network NAT Engine configuration. */
    PDMINETWORKNATCONFIG    INetworkNATCfg;
    /** The port we're attached to. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** The network config of the port we're attached to. */
    PPDMINETWORKCONFIG      pIAboveConfig;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** Link state */
    PDMNETWORKLINKSTATE     enmLinkState;
    /** NAT state */
    pSlirpState             pNATState;
    /** TFTP directory prefix. */
    char                   *pszTFTPPrefix;
    /** Boot file name to provide in the DHCP server response. */
    char                   *pszBootFile;
    /** tftp server name to provide in the DHCP server response. */
    char                   *pszNextServer;
    /** Polling thread. */
    PPDMTHREAD              pSlirpThread;
    /** Queue for NAT-thread-external events. */
    RTREQQUEUE              hSlirpReqQueue;
    /** The guest IP for port-forwarding. */
    uint32_t                GuestIP;
    /** Link state set when the VM is suspended. */
    PDMNETWORKLINKSTATE     enmLinkStateWant;

#ifndef RT_OS_WINDOWS
    /** The write end of the control pipe. */
    RTPIPE                  hPipeWrite;
    /** The read end of the control pipe. */
    RTPIPE                  hPipeRead;
#else
    /* wakeup socket pair for NAT thread */
    SOCKET                  pWakeupSockPair[2];
#endif
    /* count of bytes sent to notify NAT thread */
    volatile uint64_t       cbWakeupNotifs;

#define DRV_PROFILE_COUNTER(name, dsc)     STAMPROFILE Stat ## name
#define DRV_COUNTING_COUNTER(name, dsc)    STAMCOUNTER Stat ## name
#include "slirp/counters.h"
    /** thread delivering packets for receiving by the guest */
    PPDMTHREAD              pRecvThread;
    /** event to wakeup the guest receive thread */
    RTSEMEVENT              EventRecv;
    /** Receive Req queue (deliver packets to the guest) */
    RTREQQUEUE              hRecvReqQueue;

    /** makes access to device func RecvAvail and Recv atomical. */
    RTCRITSECT              DevAccessLock;
    /** Number of in-flight packets. */
    volatile uint32_t       cPkts;

    /** Transmit lock taken by BeginXmit and released by EndXmit. */
    RTCRITSECT              XmitLock;

#ifdef RT_OS_DARWIN
    /* Handle of the DNS watcher runloop source. */
    CFRunLoopSourceRef      hRunLoopSrcDnsWatcher;
#endif
} DRVNAT;
AssertCompileMemberAlignment(DRVNAT, StatNATRecvWakeups, 8);
/** Pointer to the NAT driver instance data. */
typedef DRVNAT *PDRVNAT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void drvNATNotifyNATThread(PDRVNAT pThis, const char *pszWho);
static void drvNAT_UpdateTimeout(uint32_t *uTimeout, void *opaque);
static void drvNAT_CheckTimeout(void *opaque);
static DECLCALLBACK(int) drvNAT_AddPollCb(int iFd, int iEvents, void *opaque);
static DECLCALLBACK(int64_t) drvNAT_ClockGetNsCb(void *opaque);
static DECLCALLBACK(int) drvNAT_GetREventsCb(int idx, void *opaque);
static DECLCALLBACK(int) drvNATNotifyApplyPortForwardCommand(PDRVNAT pThis, bool fRemove, bool fUdp, const char *pszHostIp,
                                                             uint16_t u16HostPort, const char *pszGuestIp, uint16_t u16GuestPort);



/*
 * PDM Function Implementations
 */

/**
 * @callback_method_impl{FNPDMTHREADDRV}
 *
 * Queues guest process received packet. Triggered by drvNATRecvWakeup.
 */
static DECLCALLBACK(int) drvNATRecv(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        RTReqQueueProcess(pThis->hRecvReqQueue, 0);
        if (ASMAtomicReadU32(&pThis->cPkts) == 0)
            RTSemEventWait(pThis->EventRecv, RT_INDEFINITE_WAIT);
    }
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNPDMTHREADWAKEUPDRV}
 */
static DECLCALLBACK(int) drvNATRecvWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    int rc;
    rc = RTSemEventSignal(pThis->EventRecv);

    STAM_COUNTER_INC(&pThis->StatNATRecvWakeups);
    return VINF_SUCCESS;
}

/**
 * @brief Processes incoming packet (to guest).
 *
 * @param   pThis   Pointer to DRVNAT state for current context.
 * @param   pBuf    Pointer to packet buffer.
 * @param   cb      Size of packet in buffer.
 *
 * @thread  NAT
 */
static DECLCALLBACK(void) drvNATRecvWorker(PDRVNAT pThis, void *pBuf, size_t cb)
{
    int rc;
    STAM_PROFILE_START(&pThis->StatNATRecv, a);

    rc = RTCritSectEnter(&pThis->DevAccessLock);
    AssertRC(rc);

    STAM_PROFILE_START(&pThis->StatNATRecvWait, b);
    rc = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
    STAM_PROFILE_STOP(&pThis->StatNATRecvWait, b);

    if (RT_SUCCESS(rc))
    {
        rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, pBuf, cb);
        AssertRC(rc);
        RTMemFree(pBuf);
        pBuf = NULL;
    }
    else if (   rc != VERR_TIMEOUT
             && rc != VERR_INTERRUPTED)
    {
        AssertRC(rc);
    }

    rc = RTCritSectLeave(&pThis->DevAccessLock);
    AssertRC(rc);
    ASMAtomicDecU32(&pThis->cPkts);
    drvNATNotifyNATThread(pThis, "drvNATRecvWorker");
    STAM_PROFILE_STOP(&pThis->StatNATRecv, a);
}

/**
 * Frees a S/G buffer allocated by drvNATNetworkUp_AllocBuf.
 *
 * @param   pThis               Pointer to the NAT instance.
 * @param   pSgBuf              The S/G buffer to free.
 *
 * @thread  NAT
 */
static void drvNATFreeSgBuf(PDRVNAT pThis, PPDMSCATTERGATHER pSgBuf)
{
    RT_NOREF(pThis);
    Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
    pSgBuf->fFlags = 0;
    if (pSgBuf->pvAllocator)
    {
        Assert(!pSgBuf->pvUser);
        RTMemFree(pSgBuf->aSegs[0].pvSeg);
    }
    else if (pSgBuf->pvUser)
    {
        RTMemFree(pSgBuf->aSegs[0].pvSeg);
        pSgBuf->aSegs[0].pvSeg = NULL;
        RTMemFree(pSgBuf->pvUser);
        pSgBuf->pvUser = NULL;
    }
    RTMemFree(pSgBuf);
}

/**
 * Worker function for drvNATSend().
 *
 * @param   pThis               Pointer to the NAT instance.
 * @param   pSgBuf              The scatter/gather buffer.
 * @thread  NAT
 */
static DECLCALLBACK(void) drvNATSendWorker(PDRVNAT pThis, PPDMSCATTERGATHER pSgBuf)
{
    LogFlowFunc(("pThis=%p pSgBuf=%p\n", pThis, pSgBuf));

    if (pThis->enmLinkState == PDMNETWORKLINKSTATE_UP)
    {
        const uint8_t *m = static_cast<const uint8_t*>(pSgBuf->pvAllocator);
        if (m)
        {
            /*
             * A normal frame.
             */
            LogFlowFunc(("m=%p\n", m));
            slirp_input(pThis->pNATState->pSlirp, (uint8_t const *)pSgBuf->pvAllocator, (int)pSgBuf->cbUsed);
        }
        else
        {
            /*
             * M_EXT buf, need to segment it.
             */

            uint8_t const  *pbFrame = (uint8_t const *)pSgBuf->aSegs[0].pvSeg;
            PCPDMNETWORKGSO pGso    = (PCPDMNETWORKGSO)pSgBuf->pvUser;
            /* Do not attempt to segment frames with invalid GSO parameters. */
            if (PDMNetGsoIsValid((const PDMNETWORKGSO *)pGso, sizeof(*pGso), pSgBuf->cbUsed))
            {
                uint32_t const cSegs = PDMNetGsoCalcSegmentCount(pGso, pSgBuf->cbUsed);
                Assert(cSegs > 1);
                for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
                {
                    void  *pvSeg;
                    pvSeg = RTMemAlloc(DRVNAT_MAXFRAMESIZE);

                    uint32_t cbPayload, cbHdrs;
                    uint32_t offPayload = PDMNetGsoCarveSegment(pGso, pbFrame, pSgBuf->cbUsed,
                                                                iSeg, cSegs, (uint8_t *)pvSeg, &cbHdrs, &cbPayload);
                    memcpy((uint8_t *)pvSeg + cbHdrs, pbFrame + offPayload, cbPayload);

                    slirp_input(pThis->pNATState->pSlirp, (uint8_t const *)pvSeg, cbPayload + cbHdrs);
                    RTMemFree(pvSeg);
                }
            }
        }
    }

    LogFlowFunc(("leave\n"));
    drvNATFreeSgBuf(pThis, pSgBuf);
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
static DECLCALLBACK(int) drvNATNetworkUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkUp);
    int rc = RTCritSectTryEnter(&pThis->XmitLock);
    if (RT_FAILURE(rc))
    {
        /** @todo Kick the worker thread when we have one... */
        rc = VERR_TRY_AGAIN;
    }
    LogFlowFunc(("Beginning xmit...\n"));
    return rc;
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnAllocBuf}
 */
static DECLCALLBACK(int) drvNATNetworkUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                  PCPDMNETWORKGSO pGso, PPPDMSCATTERGATHER ppSgBuf)
{
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkUp);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));

    LogFlowFuncEnter();

    /*
     * Drop the incoming frame if the NAT thread isn't running.
     */
    if (pThis->pSlirpThread->enmState != PDMTHREADSTATE_RUNNING)
    {
        Log(("drvNATNetowrkUp_AllocBuf: returns VERR_NET_DOWN\n"));
        return VERR_NET_DOWN;
    }

    /*
     * Allocate a scatter/gather buffer and an mbuf.
     */
    PPDMSCATTERGATHER pSgBuf = (PPDMSCATTERGATHER)RTMemAllocZ(sizeof(PDMSCATTERGATHER));
    if (!pSgBuf)
        return VERR_NO_MEMORY;
    if (!pGso)
    {
        /*
         * Drop the frame if it is too big.
         */
        if (cbMin >= DRVNAT_MAXFRAMESIZE)
        {
            Log(("drvNATNetowrkUp_AllocBuf: drops over-sized frame (%u bytes), returns VERR_INVALID_PARAMETER\n",
                 cbMin));
            RTMemFree(pSgBuf);
            return VERR_INVALID_PARAMETER;
        }

        pSgBuf->pvUser      = NULL;
        pSgBuf->aSegs[0].cbSeg = RT_ALIGN_Z(cbMin, 128);
        pSgBuf->aSegs[0].pvSeg = RTMemAlloc(pSgBuf->aSegs[0].cbSeg);
        pSgBuf->pvAllocator = pSgBuf->aSegs[0].pvSeg;

        if (!pSgBuf->pvAllocator)
        {
            RTMemFree(pSgBuf);
            return VERR_TRY_AGAIN;
        }
    }
    else
    {
        /*
         * Drop the frame if its segment is too big.
         */
        if (pGso->cbHdrsTotal + pGso->cbMaxSeg >= DRVNAT_MAXFRAMESIZE)
        {
            Log(("drvNATNetowrkUp_AllocBuf: drops over-sized frame (%u bytes), returns VERR_INVALID_PARAMETER\n",
                 pGso->cbHdrsTotal + pGso->cbMaxSeg));
            RTMemFree(pSgBuf);
            return VERR_INVALID_PARAMETER;
        }

        pSgBuf->pvUser      = RTMemDup(pGso, sizeof(*pGso));
        pSgBuf->pvAllocator = NULL;

        pSgBuf->aSegs[0].cbSeg = RT_ALIGN_Z(cbMin, 128);
        pSgBuf->aSegs[0].pvSeg = RTMemAlloc(pSgBuf->aSegs[0].cbSeg);
        if (!pSgBuf->pvUser || !pSgBuf->aSegs[0].pvSeg)
        {
            RTMemFree(pSgBuf->aSegs[0].pvSeg);
            RTMemFree(pSgBuf->pvUser);
            RTMemFree(pSgBuf);
            return VERR_TRY_AGAIN;
        }
    }

    /*
     * Initialize the S/G buffer and return.
     */
    pSgBuf->fFlags      = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1;
    pSgBuf->cbUsed      = 0;
    pSgBuf->cbAvailable = pSgBuf->aSegs[0].cbSeg;
    pSgBuf->cSegs       = 1;

    *ppSgBuf = pSgBuf;
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnFreeBuf}
 */
static DECLCALLBACK(int) drvNATNetworkUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkUp);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));
    drvNATFreeSgBuf(pThis, pSgBuf);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
static DECLCALLBACK(int) drvNATNetworkUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkUp);
    Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_OWNER_MASK) == PDMSCATTERGATHER_FLAGS_OWNER_1);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));

    LogFlowFunc(("enter\n"));

    int rc;
    if (pThis->pSlirpThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        rc = RTReqQueueCallEx(pThis->hSlirpReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvNATSendWorker, 2, pThis, pSgBuf);
        if (RT_SUCCESS(rc))
        {
            drvNATNotifyNATThread(pThis, "drvNATNetworkUp_SendBuf");
            LogFlowFunc(("leave success\n"));
            return VINF_SUCCESS;
        }

        rc = VERR_NET_NO_BUFFER_SPACE;
    }
    else
        rc = VERR_NET_DOWN;
    drvNATFreeSgBuf(pThis, pSgBuf);
    LogFlowFunc(("leave rc=%Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
static DECLCALLBACK(void) drvNATNetworkUp_EndXmit(PPDMINETWORKUP pInterface)
{
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkUp);
    RTCritSectLeave(&pThis->XmitLock);
}

/**
 * Get the NAT thread out of poll/WSAWaitForMultipleEvents
 */
static void drvNATNotifyNATThread(PDRVNAT pThis, const char *pszWho)
{
    RT_NOREF(pszWho);
    int rc = 0;
#ifndef RT_OS_WINDOWS
    /* kick poll() */
    size_t cbIgnored;
    rc = RTPipeWrite(pThis->hPipeWrite, "", 1, &cbIgnored);
    if (RT_SUCCESS(rc))
    {
        /* Count how many bites we send down the socket */
        ASMAtomicIncU64(&pThis->cbWakeupNotifs);
    }
#else
    int cbWritten = send(pThis->pWakeupSockPair[0], "", 1, NULL);
    if (cbWritten == SOCKET_ERROR)
    {
        Log4(("Notify NAT Thread Error %d\n", WSAGetLastError()));
    }
    else
    {
        /* Count how many bites we send down the socket */
        ASMAtomicIncU64(&pThis->cbWakeupNotifs);
    }
#endif
    AssertRC(rc);
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
static DECLCALLBACK(void) drvNATNetworkUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    RT_NOREF(pInterface, fPromiscuous);
    LogFlow(("drvNATNetworkUp_SetPromiscuousMode: fPromiscuous=%d\n", fPromiscuous));
    /* nothing to do */
}

/**
 * Worker function for drvNATNetworkUp_NotifyLinkChanged().
 * @thread "NAT" thread.
 *
 * @param   pThis           Pointer to DRVNAT state for current context.
 * @param   enmLinkState    Enum value of link state.
 *
 * @thread  NAT
 */
static DECLCALLBACK(void) drvNATNotifyLinkChangedWorker(PDRVNAT pThis, PDMNETWORKLINKSTATE enmLinkState)
{
    pThis->enmLinkState = pThis->enmLinkStateWant = enmLinkState;
    switch (enmLinkState)
    {
        case PDMNETWORKLINKSTATE_UP:
            LogRel(("NAT: Link up\n"));
            break;

        case PDMNETWORKLINKSTATE_DOWN:
        case PDMNETWORKLINKSTATE_DOWN_RESUME:
            LogRel(("NAT: Link down\n"));
            break;

        default:
            AssertMsgFailed(("drvNATNetworkUp_NotifyLinkChanged: unexpected link state %d\n", enmLinkState));
    }
}

/**
 * Notification on link status changes.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   enmLinkState    The new link state.
 *
 * @thread  EMT
 */
static DECLCALLBACK(void) drvNATNetworkUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkUp);

    LogFlow(("drvNATNetworkUp_NotifyLinkChanged: enmLinkState=%d\n", enmLinkState));

    /* Don't queue new requests if the NAT thread is not running (e.g. paused,
     * stopping), otherwise we would deadlock. Memorize the change. */
    if (pThis->pSlirpThread->enmState != PDMTHREADSTATE_RUNNING)
    {
        pThis->enmLinkStateWant = enmLinkState;
        return;
    }

    PRTREQ pReq;
    int rc = RTReqQueueCallEx(pThis->hSlirpReqQueue, &pReq, 0 /*cMillies*/, RTREQFLAGS_VOID,
                              (PFNRT)drvNATNotifyLinkChangedWorker, 2, pThis, enmLinkState);
    if (rc == VERR_TIMEOUT)
    {
        drvNATNotifyNATThread(pThis, "drvNATNetworkUp_NotifyLinkChanged");
        rc = RTReqWait(pReq, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }
    else
        AssertRC(rc);
    RTReqRelease(pReq);
}

/**
 * NAT thread handling the slirp stuff.
 *
 * The slirp implementation is single-threaded so we execute this enginre in a
 * dedicated thread. We take care that this thread does not become the
 * bottleneck: If the guest wants to send, a request is enqueued into the
 * hSlirpReqQueue and handled asynchronously by this thread.  If this thread
 * wants to deliver packets to the guest, it enqueues a request into
 * hRecvReqQueue which is later handled by the Recv thread.
 *
 * @param   pDrvIns     Pointer to PDM driver context.
 * @param   pThread     Pointer to calling thread context.
 *
 * @returns VBox status code
 *
 * @thread  NAT
 */
static DECLCALLBACK(int) drvNATAsyncIoThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
#ifdef RT_OS_WINDOWS
    drvNAT_AddPollCb(pThis->pWakeupSockPair[1], SLIRP_POLL_IN | SLIRP_POLL_HUP, pThis);
    pThis->pNATState->polls[0].fd = pThis->pWakeupSockPair[1];
#else
    unsigned int cPollNegRet = 0;
    drvNAT_AddPollCb(RTPipeToNative(pThis->hPipeRead), SLIRP_POLL_IN | SLIRP_POLL_HUP, pThis);
    pThis->pNATState->polls[0].fd = RTPipeToNative(pThis->hPipeRead);
    pThis->pNATState->polls[0].events = POLLRDNORM | POLLPRI | POLLRDBAND;
    pThis->pNATState->polls[0].revents = 0;
#endif /* !RT_OS_WINDOWS */

    LogFlow(("drvNATAsyncIoThread: pThis=%p\n", pThis));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    if (pThis->enmLinkStateWant != pThis->enmLinkState)
        drvNATNotifyLinkChangedWorker(pThis, pThis->enmLinkStateWant);

    /*
     * Polling loop.
     */
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * To prevent concurrent execution of sending/receiving threads
         */
#ifndef RT_OS_WINDOWS
        uint32_t uTimeout = DRVNAT_DEFAULT_TIMEOUT;
        pThis->pNATState->nsock = 1;

        slirp_pollfds_fill(pThis->pNATState->pSlirp, &uTimeout, drvNAT_AddPollCb /* SlirpAddPollCb */, pThis /* opaque */);
        drvNAT_UpdateTimeout(&uTimeout, pThis);

        int cChangedFDs = poll(pThis->pNATState->polls, pThis->pNATState->nsock, uTimeout /* timeout */);

        if (cChangedFDs < 0)
        {
            if (errno == EINTR)
            {
                Log2(("NAT: signal was caught while sleep on poll\n"));
                /* No error, just process all outstanding requests but don't wait */
                cChangedFDs = 0;
            }
            else if (cPollNegRet++ > 128)
            {
                LogRel(("NAT: Poll returns (%s) suppressed %d\n", strerror(errno), cPollNegRet));
                cPollNegRet = 0;
            }
        }


        slirp_pollfds_poll(pThis->pNATState->pSlirp, cChangedFDs < 0, drvNAT_GetREventsCb /* SlirpGetREventsCb */, pThis /* opaque */);
        if (pThis->pNATState->polls[0].revents & (POLLRDNORM|POLLPRI|POLLRDBAND))
        {
            /* drain the pipe
             *
             * Note! drvNATSend decoupled so we don't know how many times
             * device's thread sends before we've entered multiplex,
             * so to avoid false alarm drain pipe here to the very end
             */
            char ch[1024];
            size_t cbRead;
            uint64_t cbWakeupNotifs = ASMAtomicReadU64(&pThis->cbWakeupNotifs);
            RTPipeRead(pThis->hPipeRead, &ch[0], RT_MIN(cbWakeupNotifs, 1024), &cbRead);
            ASMAtomicSubU64(&pThis->cbWakeupNotifs, cbRead);
        }

        /* process _all_ outstanding requests but don't wait */
        RTReqQueueProcess(pThis->hSlirpReqQueue, 0);
        drvNAT_CheckTimeout(pThis);

#else /* RT_OS_WINDOWS */
        uint32_t msTimeout = DRVNAT_DEFAULT_TIMEOUT;
        pThis->pNATState->nsock = 1;
        slirp_pollfds_fill(pThis->pNATState->pSlirp, &msTimeout, drvNAT_AddPollCb /* SlirpAddPollCb */, pThis /* opaque */);
        drvNAT_UpdateTimeout(&msTimeout, pThis);

        int cChangedFDs = WSAPoll(pThis->pNATState->polls, pThis->pNATState->nsock, msTimeout /* timeout */);
        int error = WSAGetLastError();
        if (cChangedFDs == SOCKET_ERROR)
        {
            LogRel(("NAT: RTWinPoll returned error=%Rrc (cChangedFDs=%d)\n", error, cChangedFDs));
            Log4(("NAT: NSOCK = %d\n", pThis->pNATState->nsock));
        }

        if (pThis->pNATState->polls[0].revents & (POLLIN))
        {
            /* drain the pipe
             *
             * Note! drvNATSend decoupled so we don't know how many times
             * device's thread sends before we've entered multiplex,
             * so to avoid false alarm drain pipe here to the very end
             */
            char ch[1024];
            size_t cbRead;
            uint64_t cbWakeupNotifs = ASMAtomicReadU64(&pThis->cbWakeupNotifs);
            cbRead = recv(pThis->pWakeupSockPair[1], &ch[0], RT_MIN(cbWakeupNotifs, 1024), NULL);
            ASMAtomicSubU64(&pThis->cbWakeupNotifs, cbRead);
        }

        if (cChangedFDs == 0)
        {
            /* only check for slow/fast timers */
            slirp_pollfds_poll(pThis->pNATState->pSlirp, false /*select error*/, drvNAT_GetREventsCb /* SlirpGetREventsCb */, pThis /* opaque */);
            RTReqQueueProcess(pThis->hSlirpReqQueue, 0);
            continue;
        }
        /* poll the sockets in any case */
        Log2(("%s: poll\n", __FUNCTION__));
        slirp_pollfds_poll(pThis->pNATState->pSlirp, cChangedFDs < 0 /*select error*/, drvNAT_GetREventsCb /* SlirpGetREventsCb */, pThis /* opaque */);

        /* process _all_ outstanding requests but don't wait */
        RTReqQueueProcess(pThis->hSlirpReqQueue, 0);
        drvNAT_CheckTimeout(pThis);
#endif /* RT_OS_WINDOWS */
    }

    return VINF_SUCCESS;
}

/**
 * Unblock the send thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The pcnet device instance.
 * @param   pThread     The send thread.
 *
 * @thread  ?
 */
static DECLCALLBACK(int) drvNATAsyncIoWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);

    drvNATNotifyNATThread(pThis, "drvNATAsyncIoWakeup");
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvNATQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVNAT     pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUp);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKNATCONFIG, &pThis->INetworkNATCfg);
    return NULL;
}

/**
 * Info handler.
 *
 * @param   pDrvIns     The PDM driver context.
 * @param   pHlp        ....
 * @param   pszArgs     Unused.
 *
 * @thread  any
 */
static DECLCALLBACK(void) drvNATInfo(PPDMDRVINS pDrvIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    pHlp->pfnPrintf(pHlp, "libslirp Connection Info:\n");
    pHlp->pfnPrintf(pHlp, slirp_connection_info(pThis->pNATState->pSlirp));
    pHlp->pfnPrintf(pHlp, "libslirp Neighbor Info:\n");
    pHlp->pfnPrintf(pHlp, slirp_neighbor_info(pThis->pNATState->pSlirp));
    pHlp->pfnPrintf(pHlp, "libslirp Version String: %s \n", slirp_version_string());
}

/**
 * Sets up the redirectors.
 *
 * @returns VBox status code.
 * @param   uInstance       ?
 * @param   pThis           ?
 * @param   pCfg            The configuration handle.
 * @param   pNetwork        Unused.
 *
 * @thread  ?
 */
static int drvNATConstructRedir(unsigned iInstance, PDRVNAT pThis, PCFGMNODE pCfg, PRTNETADDRIPV4 pNetwork)
{
    /** @todo r=jack: rewrite to support IPv6? */
    PPDMDRVINS pDrvIns = pThis->pDrvIns;
    PCPDMDRVHLPR3 pHlp = pDrvIns->pHlpR3;

    RT_NOREF(pNetwork); /** @todo figure why pNetwork isn't used */

    PCFGMNODE pPFTree = pHlp->pfnCFGMGetChild(pCfg, "PortForwarding");
    if (pPFTree == NULL)
        return VINF_SUCCESS;

    /*
     * Enumerate redirections.
     */
    for (PCFGMNODE pNode = pHlp->pfnCFGMGetFirstChild(pPFTree); pNode; pNode = pHlp->pfnCFGMGetNextChild(pNode))
    {
        /*
         * Validate the port forwarding config.
         */
        if (!pHlp->pfnCFGMAreValuesValid(pNode, "Name\0Protocol\0UDP\0HostPort\0GuestPort\0GuestIP\0BindIP\0"))
            return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                    N_("Unknown configuration in port forwarding"));

        /* protocol type */
        bool fUDP;
        char szProtocol[32];
        int rc;
        GET_STRING(rc, pDrvIns, pNode, "Protocol", szProtocol[0], sizeof(szProtocol));
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            fUDP = false;
            GET_BOOL(rc, pDrvIns, pNode, "UDP", fUDP);
        }
        else if (RT_SUCCESS(rc))
        {
            if (!RTStrICmp(szProtocol, "TCP"))
                fUDP = false;
            else if (!RTStrICmp(szProtocol, "UDP"))
                fUDP = true;
            else
                return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                    N_("NAT#%d: Invalid configuration value for \"Protocol\": \"%s\""),
                    iInstance, szProtocol);
        }
        else
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                       N_("NAT#%d: configuration query for \"Protocol\" failed"),
                                       iInstance);
        /* host port */
        int32_t iHostPort;
        GET_S32_STRICT(rc, pDrvIns, pNode, "HostPort", iHostPort);

        /* guest port */
        int32_t iGuestPort;
        GET_S32_STRICT(rc, pDrvIns, pNode, "GuestPort", iGuestPort);

        /** @todo r=jack: why are we using IP INADD_ANY for port forward when FE does not do so. */
        /* host address ("BindIP" name is rather unfortunate given "HostPort" to go with it) */
        char szHostIp[MAX_IP_ADDRESS_STR_LEN_W_NULL] = {0};
        // GETIP_DEF(rc, pDrvIns, pNode, szHostIp, INADDR_ANY);
        GET_STRING(rc, pDrvIns, pNode, "BindIP", szHostIp[0], sizeof(szHostIp));

        /* guest address */
        char szGuestIp[MAX_IP_ADDRESS_STR_LEN_W_NULL] = {0};
        // GETIP_DEF(rc, pDrvIns, pNode, szGuestIp, INADDR_ANY);
        GET_STRING(rc, pDrvIns, pNode, "GuestIP", szGuestIp[0], sizeof(szGuestIp));

        LogRelMax(256, ("Preconfigured port forward rule discovered on startup: fUdp=%d, HostIp=%s, u16HostPort=%u, GuestIp=%s, u16GuestPort=%u\n",
                        RT_BOOL(fUDP), szHostIp, iHostPort, szGuestIp, iGuestPort));

        /*
         * Apply port forward.
         */
        if (drvNATNotifyApplyPortForwardCommand(pThis, false /* fRemove */, fUDP, szHostIp, iHostPort, szGuestIp, iGuestPort) < 0)
            return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_NAT_REDIR_SETUP, RT_SRC_POS,
                                       N_("NAT#%d: configuration error: failed to set up redirection of %d to %d. "
                                          "Probably a conflict with existing services or other rules"), 
                                       iInstance, iHostPort, iGuestPort);
    } /* for each redir rule */

    return VINF_SUCCESS;
}

/**
 * Applies port forwarding between guest and host.
 *
 * @param   pThis           Pointer to DRVNAT state for current context.
 * @param   fRemove         Flag to remove port forward instead of create.
 * @param   fUdp            Flag specifying if UDP. If false, TCP.
 * @param   pszHostIp       String of host IP address.
 * @param   u16HostPort     Host port to forward to.
 * @param   pszGuestIp      String of guest IP address.
 * @param   u16GuestPort    Guest port to forward.
 *
 * @thread  ?
 */
static DECLCALLBACK(int) drvNATNotifyApplyPortForwardCommand(PDRVNAT pThis, bool fRemove, bool fUdp, const char *pszHostIp,
                                                             uint16_t u16HostPort, const char *pszGuestIp, uint16_t u16GuestPort)
{
    /** @todo r=jack:
     * - rewrite for IPv6
     * - do we want to lock the guestIp to the VMs IP?
     */
    struct in_addr guestIp, hostIp;

    if (   pszHostIp == NULL
        || inet_aton(pszHostIp, &hostIp) == 0)
        hostIp.s_addr = INADDR_ANY;

    if (   pszGuestIp == NULL
        || inet_aton(pszGuestIp, &guestIp) == 0)
        guestIp.s_addr = pThis->GuestIP;

    int rc;
    if (fRemove)
        rc = slirp_remove_hostfwd(pThis->pNATState->pSlirp, fUdp, hostIp, u16HostPort);
    else
        rc = slirp_add_hostfwd(pThis->pNATState->pSlirp, fUdp, hostIp,
                               u16HostPort, guestIp, u16GuestPort);
    if (rc < 0)
    {
        LogRelFunc(("Port forward modify FAIL! Details: fRemove=%d, fUdp=%d, pszHostIp=%s, u16HostPort=%u, pszGuestIp=%s, u16GuestPort=%u\n",
                    RT_BOOL(fRemove), RT_BOOL(fUdp), pszHostIp, u16HostPort, pszGuestIp, u16GuestPort));
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_NAT_REDIR_SETUP, RT_SRC_POS,
                                   N_("NAT#%d: configuration error: failed to set up redirection of %d to %d. "
                                      "Probably a conflict with existing services or other rules"), 
                                   pThis->pDrvIns->iInstance, u16HostPort, u16GuestPort);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMINETWORKNATCONFIG,pfnRedirectRuleCommand}
 */
static DECLCALLBACK(int) drvNATNetworkNatConfigRedirect(PPDMINETWORKNATCONFIG pInterface, bool fRemove,
                                                        bool fUdp, const char *pHostIp, uint16_t u16HostPort,
                                                        const char *pGuestIp, uint16_t u16GuestPort)
{
    LogRelMax(256, ("New port forwarded added: "
                    "fRemove=%d, fUdp=%d, pHostIp=%s, u16HostPort=%u, pGuestIp=%s, u16GuestPort=%u\n",
                        RT_BOOL(fRemove), RT_BOOL(fUdp), pHostIp, u16HostPort, pGuestIp, u16GuestPort));
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkNATCfg);
    /* Execute the command directly if the VM is not running. */
    int rc;
    if (pThis->pSlirpThread->enmState != PDMTHREADSTATE_RUNNING)
        rc = drvNATNotifyApplyPortForwardCommand(pThis, fRemove, fUdp, pHostIp, u16HostPort, pGuestIp,u16GuestPort);
    else
    {
        PRTREQ pReq;
        rc = RTReqQueueCallEx(pThis->hSlirpReqQueue, &pReq, 0 /*cMillies*/, RTREQFLAGS_VOID,
                              (PFNRT)drvNATNotifyApplyPortForwardCommand, 7, 
                              pThis, fRemove, fUdp, pHostIp, u16HostPort, pGuestIp, u16GuestPort);
        if (rc == VERR_TIMEOUT)
        {
            drvNATNotifyNATThread(pThis, "drvNATNetworkNatConfigRedirect");
            rc = RTReqWait(pReq, RT_INDEFINITE_WAIT);
            AssertRC(rc);
        }
        else
            AssertRC(rc);

        RTReqRelease(pReq);
    }
    return rc;
}

/**
 * @interface_method_impl{PDMINETWORKNATCONFIG,pfnNotifyDnsChanged}
 */
static DECLCALLBACK(void) drvNATNotifyDnsChanged(PPDMINETWORKNATCONFIG pInterface, PCPDMINETWORKNATDNSCONFIG pDnsConf)
{
    PDRVNAT const      pThis     = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkNATCfg);
    SlirpState * const pNATState = pThis->pNATState;
    AssertReturnVoid(pNATState);
    AssertReturnVoid(pNATState->pSlirp);

    if (!pNATState->fPassDomain)
        return;

    LogRel(("NAT: DNS settings changed, triggering update\n"));

    if (pDnsConf->szDomainName[0] == '\0')
        slirp_set_vdomainname(pNATState->pSlirp, NULL);
    else
        slirp_set_vdomainname(pNATState->pSlirp, pDnsConf->szDomainName);

    slirp_set_vdnssearch(pNATState->pSlirp, pDnsConf->papszSearchDomains);
    /** @todo Convert the papszNameServers entries to IP address and tell about
     *        the first IPv4 and IPv6 ones. */
}


/*
 * Libslirp Utility Functions
 */
/**
 * Update the timeout field in given list of Slirp timers.
 *
 * @param uTimeout  Pointer to timeout value.
 * @param opaque    Pointer to NAT State context.
 *
 * @thread  ?
 */
static void drvNAT_UpdateTimeout(uint32_t *uTimeout, void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);

    uint32_t currTime = drvNAT_ClockGetNsCb(pThis) / (1000 * 1000);
    SlirpTimer *pCurrent = pThis->pNATState->pTimerHead;
    while (pCurrent != NULL)
    {
        if (pCurrent->uTimeExpire != 0)
        {
            int64_t diff = pCurrent->uTimeExpire - currTime;

            if (diff < 0)
                diff = 0;

            if (diff < *uTimeout)
                *uTimeout = diff;
        }

        pCurrent = pCurrent->next;
    }
}

/**
 * Check if timeout has passed in given list of Slirp timers.
 *
 * @param   opaque  Pointer to NAT State context.
 *
 * @thread  ?
 */
static void drvNAT_CheckTimeout(void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);

    int64_t currTime = drvNAT_ClockGetNsCb(pThis) / (1000 * 1000);
    SlirpTimer *pCurrent = pThis->pNATState->pTimerHead;
    while (pCurrent != NULL)
    {
        if (pCurrent->uTimeExpire != 0)
        {
            int64_t diff = pCurrent->uTimeExpire - currTime;
            if (diff <= 0)
            {
                pCurrent->uTimeExpire = 0;
                pCurrent->pHandler(pCurrent->opaque);
            }
        }

        pCurrent = pCurrent->next;
    }
}

/**
 * Converts slirp representation of poll events to host representation.
 *
 * @param   iEvents     Integer representing slirp type poll events.
 *
 * @returns Integer representing host type poll events.
 *
 * @thread  ?
 */
static int drvNAT_PollEventSlirpToHost(int iEvents) {
    int iRet = 0;
#ifndef RT_OS_WINDOWS
    if (iEvents & SLIRP_POLL_IN)  iRet |= POLLIN;
    if (iEvents & SLIRP_POLL_OUT) iRet |= POLLOUT;
    if (iEvents & SLIRP_POLL_PRI) iRet |= POLLPRI;
    if (iEvents & SLIRP_POLL_ERR) iRet |= POLLERR;
    if (iEvents & SLIRP_POLL_HUP) iRet |= POLLHUP;
#else
    if (iEvents & SLIRP_POLL_IN)  iRet |= (POLLRDNORM | POLLRDBAND);
    if (iEvents & SLIRP_POLL_OUT) iRet |= POLLWRNORM;
    if (iEvents & SLIRP_POLL_PRI) iRet |= (POLLIN);
    if (iEvents & SLIRP_POLL_ERR) iRet |= 0;
    if (iEvents & SLIRP_POLL_HUP) iRet |= 0;
#endif
    return iRet;
}

/**
 * Converts host representation of poll events to slirp representation.
 *
 * @param   iEvents     Integer representing host type poll events.
 *
 * @returns Integer representing slirp type poll events.
 *
 * @thread  ?
 */
static int drvNAT_PollEventHostToSlirp(int iEvents) {
    int iRet = 0;
#ifndef RT_OS_WINDOWS
    if (iEvents & POLLIN)  iRet |= SLIRP_POLL_IN;
    if (iEvents & POLLOUT) iRet |= SLIRP_POLL_OUT;
    if (iEvents & POLLPRI) iRet |= SLIRP_POLL_PRI;
    if (iEvents & POLLERR) iRet |= SLIRP_POLL_ERR;
    if (iEvents & POLLHUP) iRet |= SLIRP_POLL_HUP;
#else
    if (iEvents & (POLLRDNORM | POLLRDBAND))  iRet |= SLIRP_POLL_IN;
    if (iEvents & POLLWRNORM) iRet |= SLIRP_POLL_OUT;
    if (iEvents & (POLLPRI)) iRet |= SLIRP_POLL_PRI;
    if (iEvents & POLLERR) iRet |= SLIRP_POLL_ERR;
    if (iEvents & POLLHUP) iRet |= SLIRP_POLL_HUP;
#endif
    return iRet;
}


/*
 * Libslirp Callbacks
 */
/**
 * Callback called by libslirp to send packet into guest.
 *
 * @param   pBuf    Pointer to packet buffer.
 * @param   cb      Size of packet.
 * @param   opaque  Pointer to NAT State context.
 *
 * @returns Size of packet received or -1 on error.
 *
 * @thread  ?
 */
static DECLCALLBACK(ssize_t) drvNAT_SendPacketCb(const void *pBuf, size_t cb, void *opaque /* PDRVNAT */)
{
    char *pNewBuf = (char *)RTMemAlloc(cb);
    if (pNewBuf == NULL)
        return -1;

    memcpy(pNewBuf, pBuf, cb);

    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);

    LogFlow(("slirp_output BEGIN %p %d\n", pNewBuf, cb));
    Log6(("slirp_output: pNewBuf=%p cb=%#x (pThis=%p)\n"
          "%.*Rhxd\n", pNewBuf, cb, pThis, cb, pNewBuf));

    /* don't queue new requests when the NAT thread is about to stop */
    if (pThis->pSlirpThread->enmState != PDMTHREADSTATE_RUNNING)
        return -1;

    ASMAtomicIncU32(&pThis->cPkts);
    int rc = RTReqQueueCallEx(pThis->hRecvReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvNATRecvWorker, 3, pThis, pNewBuf, cb);
    AssertRC(rc);
    drvNATRecvWakeup(pThis->pDrvIns, pThis->pRecvThread);
    drvNATNotifyNATThread(pThis, "drvNAT_SendPacketCb");
    STAM_COUNTER_INC(&pThis->StatQueuePktSent);
    LogFlowFuncLeave();
    return cb;
}

/**
 * Callback called by libslirp on an error from a guest.
 *
 * @param   pMsg    Error message string.
 * @param   opaque  Pointer to NAT State context.
 *
 * @thread  ?
 */
static DECLCALLBACK(void) drvNAT_GuestErrorCb(const char *pMsg, void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);

    PDMDRV_SET_ERROR(pThis->pDrvIns, VERR_PDM_UNKNOWN_DRVREG_VERSION,
                            N_("Unknown error: "));
    LogRel((pMsg));
}

/**
 * Callback called by libslirp to get the current timestamp in nanoseconds.
 *
 * @param   opaque  Pointer to NAT State context.
 *
 * @returns 64-bit signed integer representing time in nanoseconds.
 */
static DECLCALLBACK(int64_t) drvNAT_ClockGetNsCb(void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);

    RT_NOREF(pThis);

    return (int64_t)RTTimeNanoTS();
}

/**
 * Callback called by slirp to create a new timer and insert it into the given list.
 *
 * @param   slirpTimeCb     Callback function supplied to the new timer upon timer expiry.
 *                          Called later by the timeout handler.
 * @param   cb_opaque       Opaque object supplied to slirpTimeCb when called. Should be
 *                          Identical to the opaque parameter.
 * @param   opaque          Pointer to NAT State context.
 *
 * @returns Pointer to new timer.
 */
static DECLCALLBACK(void *) drvNAT_TimerNewCb(SlirpTimerCb slirpTimeCb, void *cb_opaque, void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);

    SlirpTimer *pNewTimer = (SlirpTimer *)RTMemAlloc(sizeof(SlirpTimer));
    if (!pNewTimer)
        return NULL;

    pNewTimer->next = pThis->pNATState->pTimerHead;
    pNewTimer->uTimeExpire = 0;
    pNewTimer->pHandler = slirpTimeCb;
    pNewTimer->opaque = cb_opaque;
    pThis->pNATState->pTimerHead = pNewTimer;

    return pNewTimer;
}

/**
 * Callback called by slirp to free a timer.
 *
 * @param   pTimer  Pointer to slirpTimer object to be freed.
 * @param   opaque  Pointer to NAT State context.
 */
static DECLCALLBACK(void) drvNAT_TimerFreeCb(void *pTimer, void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);
    SlirpTimer *pCurrent = pThis->pNATState->pTimerHead;

    while (pCurrent != NULL)
    {
        if (pCurrent == (SlirpTimer *)pTimer)
        {
            SlirpTimer *pTmp = pCurrent->next;
            RTMemFree(pCurrent);
            pCurrent = pTmp;
        }
        else
            pCurrent = pCurrent->next;
    }
}

/**
 * Callback called by slirp to modify a timer.
 *
 * @param   pTimer      Pointer to slirpTimer object to be modified.
 * @param   expireTime  Signed 64-bit integer representing the new expiry time.
 * @param   opaque      Pointer to NAT State context.
 */
static DECLCALLBACK(void) drvNAT_TimerModCb(void *pTimer, int64_t expireTime, void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    Assert(pThis);

    RT_NOREF(pThis);

    ((SlirpTimer *)pTimer)->uTimeExpire = expireTime;
}

/**
 * Callback called by slirp when there is I/O that needs to happen.
 *
 * @param   opaque  Pointer to NAT State context.
 */
static DECLCALLBACK(void) drvNAT_NotifyCb(void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;

    drvNATAsyncIoWakeup(pThis->pDrvIns, NULL);
}

/**
 * Registers poll. Unused function (other than logging).
 */
static DECLCALLBACK(void) drvNAT_RegisterPoll(int fd, void *opaque)
{
    RT_NOREF(fd, opaque);
    Log4(("Poll registered\n"));
}

/**
 * Unregisters poll. Unused function (other than logging).
 */
static DECLCALLBACK(void) drvNAT_UnregisterPoll(int fd, void *opaque)
{
    RT_NOREF(fd, opaque);
    Log4(("Poll unregistered\n"));
}

/**
 * Callback function to add entry to pollfd array.
 *
 * @param   iFd     Integer of system file descriptor of socket.
 *                  (on windows, this is a VBox internal, not system, value).
 * @param   iEvents Integer of slirp type poll events.
 * @param   opaque  Pointer to NAT State context.
 *
 * @returns Index of latest pollfd entry.
 *
 * @thread  ?
 */
static DECLCALLBACK(int) drvNAT_AddPollCb(int iFd, int iEvents, void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;

    if (pThis->pNATState->nsock + 1 >= pThis->pNATState->uPollCap)
    {
        int cbNew = pThis->pNATState->uPollCap * 2 * sizeof(struct pollfd);
        struct pollfd *pvNew = (struct pollfd *)RTMemRealloc(pThis->pNATState->polls, cbNew);
        if (pvNew)
        {
            pThis->pNATState->polls = pvNew;
            pThis->pNATState->uPollCap *= 2;
        }
        else
            return -1;
    }

    int idx = pThis->pNATState->nsock;
#ifdef RT_OS_WINDOWS
    pThis->pNATState->polls[idx].fd = libslirp_wrap_RTHandleTableLookup(iFd);
#else
    pThis->pNATState->polls[idx].fd = iFd;
#endif
    pThis->pNATState->polls[idx].events = drvNAT_PollEventSlirpToHost(iEvents);
    pThis->pNATState->polls[idx].revents = 0;
    pThis->pNATState->nsock += 1;
    return idx;
}

/**
 * Get translated revents from a poll at a given index.
 *
 * @param   idx     Integer index of poll.
 * @param   opaque  Pointer to NAT State context.
 *
 * @returns Integer representing transalted revents.
 *
 * @thread  ?
 */
static DECLCALLBACK(int) drvNAT_GetREventsCb(int idx, void *opaque)
{
    PDRVNAT pThis = (PDRVNAT)opaque;
    struct pollfd* polls = pThis->pNATState->polls;
    return drvNAT_PollEventHostToSlirp(polls[idx].revents);
}

/**
 * Contructor/Destructor
 */
/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvNATDestruct(PPDMDRVINS pDrvIns)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    LogFlow(("drvNATDestruct:\n"));
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    SlirpState * const pNATState = pThis->pNATState;
    if (pNATState)
    {
        slirp_cleanup(pNATState->pSlirp);

#ifdef VBOX_WITH_STATISTICS
# define DRV_PROFILE_COUNTER(name, dsc)     DEREGISTER_COUNTER(name, pThis)
# define DRV_COUNTING_COUNTER(name, dsc)    DEREGISTER_COUNTER(name, pThis)
# include "slirp/counters.h"
#endif
        RTMemFree(pNATState->polls);
        pNATState->polls = NULL;

        RTMemFree(pNATState);
        pThis->pNATState = NULL;
    }

    RTReqQueueDestroy(pThis->hSlirpReqQueue);
    pThis->hSlirpReqQueue = NIL_RTREQQUEUE;

    RTReqQueueDestroy(pThis->hRecvReqQueue);
    pThis->hRecvReqQueue = NIL_RTREQQUEUE;

    RTSemEventDestroy(pThis->EventRecv);
    pThis->EventRecv = NIL_RTSEMEVENT;

    if (RTCritSectIsInitialized(&pThis->DevAccessLock))
        RTCritSectDelete(&pThis->DevAccessLock);

    if (RTCritSectIsInitialized(&pThis->XmitLock))
        RTCritSectDelete(&pThis->XmitLock);

#ifndef RT_OS_WINDOWS
    RTPipeClose(pThis->hPipeRead);
    RTPipeClose(pThis->hPipeWrite);
#endif
}

/**
 * Construct a NAT network transport driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvNATConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;

    SlirpState * const pNATState = (SlirpState *)RTMemAllocZ(sizeof(*pNATState));
    if (pNATState == NULL)
        return VERR_NO_MEMORY;
    pThis->pNATState                    = pNATState;
    pNATState->nsock                    = 0;
    pNATState->pTimerHead               = NULL;
    pNATState->polls                    = (struct pollfd *)RTMemAllocZ(64 * sizeof(struct pollfd));
    AssertReturn(pNATState->polls, VERR_NO_MEMORY);
    pNATState->uPollCap                 = 64;

    pThis->hSlirpReqQueue               = NIL_RTREQQUEUE;
    pThis->EventRecv                    = NIL_RTSEMEVENT;

    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvNATQueryInterface;

    /* INetwork */
    pThis->INetworkUp.pfnBeginXmit          = drvNATNetworkUp_BeginXmit;
    pThis->INetworkUp.pfnAllocBuf           = drvNATNetworkUp_AllocBuf;
    pThis->INetworkUp.pfnFreeBuf            = drvNATNetworkUp_FreeBuf;
    pThis->INetworkUp.pfnSendBuf            = drvNATNetworkUp_SendBuf;
    pThis->INetworkUp.pfnEndXmit            = drvNATNetworkUp_EndXmit;
    pThis->INetworkUp.pfnSetPromiscuousMode = drvNATNetworkUp_SetPromiscuousMode;
    pThis->INetworkUp.pfnNotifyLinkChanged  = drvNATNetworkUp_NotifyLinkChanged;

    /* NAT engine configuration */
    pThis->INetworkNATCfg.pfnRedirectRuleCommand = drvNATNetworkNatConfigRedirect;
    pThis->INetworkNATCfg.pfnNotifyDnsChanged    = drvNATNotifyDnsChanged;

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,
                                  "PassDomain"
                                  "|TFTPPrefix"
                                  "|BootFile"
                                  "|Network"
                                  "|NextServer"
                                  "|DNSProxy"
                                  "|BindIP"
                                  "|UseHostResolver"
                                  "|SlirpMTU"
                                  "|AliasMode"
                                  "|SockRcv"
                                  "|SockSnd"
                                  "|TcpRcv"
                                  "|TcpSnd"
                                  "|ICMPCacheLimit"
                                  "|SoMaxConnection"
                                  "|LocalhostReachable"
                                  "|HostResolverMappings"
                                  "|ForwardBroadcast"
                                  , "PortForwarding");

    LogRel(("These CFGM parameters are currently not supported when using NAT:\n"
        "DNSProxy\n"
        "UseHostResolver\n"
        "AliasMode\n"
        "SockRcv\n"
        "SockSnd\n"
        "TcpRcv\n"
        "TcpSnd\n"
        "ICMPCacheLimit\n"
        "HostResolverMappings\n"
    ));

    /*
     * Get the configuration settings.
     */
    int  rc;

    bool fPassDomain = true;
    GET_BOOL(rc, pDrvIns, pCfg, "PassDomain", fPassDomain);
    pNATState->fPassDomain = fPassDomain;

    bool fForwardBroadcast = false;
    GET_BOOL(rc, pDrvIns, pCfg, "ForwardBroadcast", fForwardBroadcast);

    GET_STRING_ALLOC(rc, pDrvIns, pCfg, "TFTPPrefix", pThis->pszTFTPPrefix);
    GET_STRING_ALLOC(rc, pDrvIns, pCfg, "BootFile", pThis->pszBootFile);
    GET_STRING_ALLOC(rc, pDrvIns, pCfg, "NextServer", pThis->pszNextServer);

    int fDNSProxy = 0;
    GET_S32(rc, pDrvIns, pCfg, "DNSProxy", fDNSProxy);
    int MTU = 1500;
    GET_S32(rc, pDrvIns, pCfg, "SlirpMTU", MTU);
    int i32AliasMode = 0;
    int i32MainAliasMode = 0;
    GET_S32(rc, pDrvIns, pCfg, "AliasMode", i32MainAliasMode);
    int iIcmpCacheLimit = 100;
    GET_S32(rc, pDrvIns, pCfg, "ICMPCacheLimit", iIcmpCacheLimit);
    bool fLocalhostReachable = false;
    GET_BOOL(rc, pDrvIns, pCfg, "LocalhostReachable", fLocalhostReachable);

    i32AliasMode |= (i32MainAliasMode & 0x1 ? 0x1 : 0);
    i32AliasMode |= (i32MainAliasMode & 0x2 ? 0x40 : 0);
    i32AliasMode |= (i32MainAliasMode & 0x4 ? 0x4 : 0);
    int i32SoMaxConn = 10;
    GET_S32(rc, pDrvIns, pCfg, "SoMaxConnection", i32SoMaxConn);
    /*
     * Query the network port interface.
     */
    pThis->pIAboveNet = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKDOWN);
    if (!pThis->pIAboveNet)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE,
                                N_("Configuration error: the above device/driver didn't "
                                "export the network port interface"));
    pThis->pIAboveConfig = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKCONFIG);
    if (!pThis->pIAboveConfig)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE,
                                N_("Configuration error: the above device/driver didn't "
                                "export the network config interface"));

    /* Generate a network address for this network card. */
    char szNetwork[32]; /* xxx.xxx.xxx.xxx/yy */
    GET_STRING(rc, pDrvIns, pCfg, "Network", szNetwork[0], sizeof(szNetwork));
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("NAT%d: Configuration error: missing network"),
                                   pDrvIns->iInstance);

    RTNETADDRIPV4 Network, Netmask;
    rc = RTCidrStrToIPv4(szNetwork, &Network, &Netmask);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("NAT#%d: Configuration error: network '%s' describes not a valid IPv4 network"),
                                   pDrvIns->iInstance, szNetwork);

    /* Construct Libslirp Config and Initialzie Slirp */

    LogFlow(("Here is what is coming out of the vbox config:\n"
             "  Network: %lu\n"
             "  Netmask: %lu\n", Network, Netmask));

    struct in_addr vnetwork = RTNetIPv4AddrHEToInAddr(&Network);
    struct in_addr vnetmask = RTNetIPv4AddrHEToInAddr(&Netmask);
    struct in_addr vhost = RTNetInAddrFromU8(10, 0, 2, 2);
    struct in_addr vdhcp_start = RTNetInAddrFromU8(10, 0, 2, 15);
    struct in_addr vnameserver = RTNetInAddrFromU8(10, 0, 2, 3);

    SlirpConfig slirpCfg = { 0 };
    static SlirpCb slirpCallbacks = { 0 };

    slirpCfg.version = 4;
    slirpCfg.restricted = false;
    slirpCfg.in_enabled = true;
    slirpCfg.vnetwork = vnetwork;
    slirpCfg.vnetmask = vnetmask;
    slirpCfg.vhost = vhost;
    slirpCfg.in6_enabled = true;

    inet_pton(AF_INET6, "fd00::", &slirpCfg.vprefix_addr6);
    slirpCfg.vprefix_len = 64;
    inet_pton(AF_INET6, "fd00::2", &slirpCfg.vhost6);

    slirpCfg.vhostname = "vbox";
    slirpCfg.tftp_server_name = pThis->pszNextServer;
    slirpCfg.tftp_path = pThis->pszTFTPPrefix;
    slirpCfg.bootfile = pThis->pszBootFile;
    slirpCfg.vdhcp_start = vdhcp_start;
    slirpCfg.vnameserver = vnameserver;
    slirpCfg.if_mtu = MTU;

    inet_pton(AF_INET6, "fd00::3", &slirpCfg.vnameserver6);

    slirpCfg.vdnssearch = NULL;
    slirpCfg.vdomainname = NULL;
    slirpCfg.disable_host_loopback = !fLocalhostReachable;
    slirpCfg.fForwardBroadcast = fForwardBroadcast;
    slirpCfg.iSoMaxConn = i32SoMaxConn;

    slirpCallbacks.send_packet = &drvNAT_SendPacketCb;
    slirpCallbacks.guest_error = &drvNAT_GuestErrorCb;
    slirpCallbacks.clock_get_ns = &drvNAT_ClockGetNsCb;
    slirpCallbacks.timer_new = &drvNAT_TimerNewCb;
    slirpCallbacks.timer_free = &drvNAT_TimerFreeCb;
    slirpCallbacks.timer_mod = &drvNAT_TimerModCb;
    slirpCallbacks.register_poll_fd = &drvNAT_RegisterPoll;
    slirpCallbacks.unregister_poll_fd = &drvNAT_UnregisterPoll;
    slirpCallbacks.notify = &drvNAT_NotifyCb;
    slirpCallbacks.init_completed = NULL;
    slirpCallbacks.timer_new_opaque = NULL;

    Slirp *pSlirp = slirp_new(/* cfg */ &slirpCfg, /* callbacks */ &slirpCallbacks, /* opaque */ pThis);

    if (pSlirp == NULL)
        return VERR_INVALID_POINTER;

    pThis->pNATState->pSlirp = pSlirp;

    rc = drvNATConstructRedir(pDrvIns->iInstance, pThis, pCfg, &Network);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMDrvHlpSSMRegisterLoadDone(pDrvIns, NULL);
    AssertLogRelRCReturn(rc, rc);

    rc = RTReqQueueCreate(&pThis->hSlirpReqQueue);
    AssertLogRelRCReturn(rc, rc);

    rc = RTReqQueueCreate(&pThis->hRecvReqQueue);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pRecvThread, pThis, drvNATRecv,
                               drvNATRecvWakeup, 256 * _1K, RTTHREADTYPE_IO, "NATRX");
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pThis->EventRecv);
    AssertRCReturn(rc, rc);

    rc = RTCritSectInit(&pThis->DevAccessLock);
    AssertRCReturn(rc, rc);

    rc = RTCritSectInit(&pThis->XmitLock);
    AssertRCReturn(rc, rc);

    char szTmp[128];
    RTStrPrintf(szTmp, sizeof(szTmp), "nat%d", pDrvIns->iInstance);
    PDMDrvHlpDBGFInfoRegister(pDrvIns, szTmp, "NAT info.", drvNATInfo);

#ifdef VBOX_WITH_STATISTICS
# define DRV_PROFILE_COUNTER(name, dsc)     REGISTER_COUNTER(name, pThis, STAMTYPE_PROFILE, STAMUNIT_TICKS_PER_CALL, dsc)
# define DRV_COUNTING_COUNTER(name, dsc)    REGISTER_COUNTER(name, pThis, STAMTYPE_COUNTER, STAMUNIT_COUNT,          dsc)
# include "slirp/counters.h"
#endif

#ifndef RT_OS_WINDOWS
    // Create the control pipe.
    rc = RTPipeCreate(&pThis->hPipeRead, &pThis->hPipeWrite, 0 /*fFlags*/);
    AssertRCReturn(rc, rc);
#else
    // Create the wakeup socket pair.
    pThis->pWakeupSockPair[0] = NULL;
    pThis->pWakeupSockPair[1] = NULL;

    /* idx=0 is write, idx=1 is read */
    rc = RTWinSocketPair(AF_INET, SOCK_DGRAM, 0, pThis->pWakeupSockPair);
    AssertRCReturn(rc, rc);
#endif
    /* initalize the notifier counter */
    pThis->cbWakeupNotifs = 0;

    rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pSlirpThread, pThis, drvNATAsyncIoThread,
                               drvNATAsyncIoWakeup, 256 * _1K, RTTHREADTYPE_IO, "NAT");
    AssertRCReturn(rc, rc);

    pThis->enmLinkState = pThis->enmLinkStateWant = PDMNETWORKLINKSTATE_UP;

    return rc;
}

/**
 * NAT network transport driver registration record.
 */
const PDMDRVREG g_DrvNATlibslirp =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "NAT",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "NATlibslrip Network Transport Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVNAT),
    /* pfnConstruct */
    drvNATConstruct,
    /* pfnDestruct */
    drvNATDestruct,
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
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};
