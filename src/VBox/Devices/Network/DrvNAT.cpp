/* $Id: DrvNAT.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvNAT - NAT network transport driver.
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
#define LOG_GROUP LOG_GROUP_DRV_NAT
#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS
#include "slirp/libslirp.h"
extern "C" {
#include "slirp/slirp_dns.h"
}
#include "slirp/ctl.h"

#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>

#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/cidr.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/pipe.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"

#ifndef RT_OS_WINDOWS
# include <unistd.h>
# include <fcntl.h>
# include <poll.h>
# include <errno.h>
#endif
#ifdef RT_OS_FREEBSD
# include <netinet/in.h>
#endif
#include <iprt/semaphore.h>
#include <iprt/req.h>
#ifdef RT_OS_DARWIN
# include <SystemConfiguration/SystemConfiguration.h>
# include <CoreFoundation/CoreFoundation.h>
#endif

#define COUNTERS_INIT
#include "counters.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

#define DRVNAT_MAXFRAMESIZE (16 * 1024)

/**
 * @todo: This is a bad hack to prevent freezing the guest during high network
 *        activity. Windows host only. This needs to be fixed properly.
 */
#define VBOX_NAT_DELAY_HACK

#define GET_EXTRADATA(pdrvins, node, name, rc, type, type_name, var)                                  \
do {                                                                                                \
    (rc) = (pdrvins)->pHlpR3->pfnCFGMQuery ## type((node), name, &(var));                                               \
    if (RT_FAILURE((rc)) && (rc) != VERR_CFGM_VALUE_NOT_FOUND)                                      \
        return PDMDrvHlpVMSetError((pdrvins), (rc), RT_SRC_POS, N_("NAT#%d: configuration query for \"" name "\" " #type_name " failed"), \
                                   (pdrvins)->iInstance);                                    \
} while (0)

#define GET_ED_STRICT(pdrvins, node, name, rc, type, type_name, var)                                  \
do {                                                                                                \
    (rc) = (pdrvins)->pHlpR3->pfnCFGMQuery ## type((node), name, &(var));                                               \
    if (RT_FAILURE((rc)))                                                                           \
        return PDMDrvHlpVMSetError((pdrvins), (rc), RT_SRC_POS, N_("NAT#%d: configuration query for \"" name "\" " #type_name " failed"), \
                                  (pdrvins)->iInstance);                                     \
} while (0)

#define GET_EXTRADATA_N(pdrvins, node, name, rc, type, type_name, var, var_size)                      \
do {                                                                                                \
    (rc) = (pdrvins)->pHlpR3->pfnCFGMQuery ## type((node), name, &(var), var_size);                                     \
    if (RT_FAILURE((rc)) && (rc) != VERR_CFGM_VALUE_NOT_FOUND)                                      \
        return PDMDrvHlpVMSetError((pdrvins), (rc), RT_SRC_POS, N_("NAT#%d: configuration query for \"" name "\" " #type_name " failed"), \
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
/**
 * NAT network transport driver instance data.
 *
 * @implements  PDMINETWORKUP
 */
typedef struct DRVNAT
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The network NAT Engine configureation. */
    PDMINETWORKNATCONFIG    INetworkNATCfg;
    /** The port we're attached to. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** The network config of the port we're attached to. */
    PPDMINETWORKCONFIG      pIAboveConfig;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** Link state */
    PDMNETWORKLINKSTATE     enmLinkState;
    /** NAT state for this instance. */
    PNATState               pNATState;
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
# if HC_ARCH_BITS == 32
    uint32_t                u32Padding;
# endif
#else
    /** for external notification */
    HANDLE                  hWakeupEvent;
#endif

#define DRV_PROFILE_COUNTER(name, dsc)     STAMPROFILE Stat ## name
#define DRV_COUNTING_COUNTER(name, dsc)    STAMCOUNTER Stat ## name
#include "counters.h"
    /** thread delivering packets for receiving by the guest */
    PPDMTHREAD              pRecvThread;
    /** thread delivering urg packets for receiving by the guest */
    PPDMTHREAD              pUrgRecvThread;
    /** event to wakeup the guest receive thread */
    RTSEMEVENT              EventRecv;
    /** event to wakeup the guest urgent receive thread */
    RTSEMEVENT              EventUrgRecv;
    /** Receive Req queue (deliver packets to the guest) */
    RTREQQUEUE              hRecvReqQueue;
    /** Receive Urgent Req queue (deliver packets to the guest). */
    RTREQQUEUE              hUrgRecvReqQueue;

    /** makes access to device func RecvAvail and Recv atomical. */
    RTCRITSECT              DevAccessLock;
    /** Number of in-flight urgent packets. */
    volatile uint32_t       cUrgPkts;
    /** Number of in-flight regular packets. */
    volatile uint32_t       cPkts;

    /** Transmit lock taken by BeginXmit and released by EndXmit. */
    RTCRITSECT              XmitLock;

    /** Request queue for the async host resolver. */
    RTREQQUEUE               hHostResQueue;
    /** Async host resolver thread. */
    PPDMTHREAD               pHostResThread;

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
DECLINLINE(void) drvNATUpdateDNS(PDRVNAT pThis, bool fFlapLink);
static DECLCALLBACK(int) drvNATReinitializeHostNameResolving(PDRVNAT pThis);


/**
 * @callback_method_impl{FNPDMTHREADDRV}
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

    STAM_COUNTER_INC(&pThis->StatNATRecvWakeups);
    return RTSemEventSignal(pThis->EventRecv);
}


/**
 * @callback_method_impl{FNPDMTHREADDRV}
 */
static DECLCALLBACK(int) drvNATUrgRecv(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        RTReqQueueProcess(pThis->hUrgRecvReqQueue, 0);
        if (ASMAtomicReadU32(&pThis->cUrgPkts) == 0)
        {
            int rc = RTSemEventWait(pThis->EventUrgRecv, RT_INDEFINITE_WAIT);
            AssertRC(rc);
        }
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPDMTHREADWAKEUPDRV}
 */
static DECLCALLBACK(int) drvNATUrgRecvWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    int rc = RTSemEventSignal(pThis->EventUrgRecv);
    AssertRC(rc);

    return VINF_SUCCESS;
}


static DECLCALLBACK(void) drvNATUrgRecvWorker(PDRVNAT pThis, uint8_t *pu8Buf, int cb, struct mbuf *m)
{
    int rc = RTCritSectEnter(&pThis->DevAccessLock);
    AssertRC(rc);
    rc = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, pu8Buf, cb);
        AssertRC(rc);
    }
    else if (   rc != VERR_TIMEOUT
             && rc != VERR_INTERRUPTED)
    {
        AssertRC(rc);
    }

    rc = RTCritSectLeave(&pThis->DevAccessLock);
    AssertRC(rc);

    slirp_ext_m_free(pThis->pNATState, m, pu8Buf);
    if (ASMAtomicDecU32(&pThis->cUrgPkts) == 0)
    {
        drvNATRecvWakeup(pThis->pDrvIns, pThis->pRecvThread);
        drvNATNotifyNATThread(pThis, "drvNATUrgRecvWorker");
    }
}


static DECLCALLBACK(void) drvNATRecvWorker(PDRVNAT pThis, uint8_t *pu8Buf, int cb, struct mbuf *m)
{
    int rc;
    STAM_PROFILE_START(&pThis->StatNATRecv, a);


    while (ASMAtomicReadU32(&pThis->cUrgPkts) != 0)
    {
        rc = RTSemEventWait(pThis->EventRecv, RT_INDEFINITE_WAIT);
        if (   RT_FAILURE(rc)
            && (   rc == VERR_TIMEOUT
                || rc == VERR_INTERRUPTED))
            goto done_unlocked;
    }

    rc = RTCritSectEnter(&pThis->DevAccessLock);
    AssertRC(rc);

    STAM_PROFILE_START(&pThis->StatNATRecvWait, b);
    rc = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
    STAM_PROFILE_STOP(&pThis->StatNATRecvWait, b);

    if (RT_SUCCESS(rc))
    {
        rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, pu8Buf, cb);
        AssertRC(rc);
    }
    else if (   rc != VERR_TIMEOUT
             && rc != VERR_INTERRUPTED)
    {
        AssertRC(rc);
    }

    rc = RTCritSectLeave(&pThis->DevAccessLock);
    AssertRC(rc);

done_unlocked:
    slirp_ext_m_free(pThis->pNATState, m, pu8Buf);
    ASMAtomicDecU32(&pThis->cPkts);

    drvNATNotifyNATThread(pThis, "drvNATRecvWorker");

    STAM_PROFILE_STOP(&pThis->StatNATRecv, a);
}

/**
 * Frees a S/G buffer allocated by drvNATNetworkUp_AllocBuf.
 *
 * @param   pThis               Pointer to the NAT instance.
 * @param   pSgBuf              The S/G buffer to free.
 */
static void drvNATFreeSgBuf(PDRVNAT pThis, PPDMSCATTERGATHER pSgBuf)
{
    Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
    pSgBuf->fFlags = 0;
    if (pSgBuf->pvAllocator)
    {
        Assert(!pSgBuf->pvUser);
        slirp_ext_m_free(pThis->pNATState, (struct mbuf *)pSgBuf->pvAllocator, NULL);
        pSgBuf->pvAllocator = NULL;
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
#if 0 /* Assertion happens often to me after resuming a VM -- no time to investigate this now. */
    Assert(pThis->enmLinkState == PDMNETWORKLINKSTATE_UP);
#endif
    if (pThis->enmLinkState == PDMNETWORKLINKSTATE_UP)
    {
        struct mbuf *m = (struct mbuf *)pSgBuf->pvAllocator;
        if (m)
        {
            /*
             * A normal frame.
             */
            pSgBuf->pvAllocator = NULL;
            slirp_input(pThis->pNATState, m, pSgBuf->cbUsed);
        }
        else
        {
            /*
             * GSO frame, need to segment it.
             */
            /** @todo Make the NAT engine grok large frames?  Could be more efficient... */
#if 0 /* this is for testing PDMNetGsoCarveSegmentQD. */
            uint8_t         abHdrScratch[256];
#endif
            uint8_t const  *pbFrame = (uint8_t const *)pSgBuf->aSegs[0].pvSeg;
            PCPDMNETWORKGSO pGso    = (PCPDMNETWORKGSO)pSgBuf->pvUser;
            /* Do not attempt to segment frames with invalid GSO parameters. */
            if (PDMNetGsoIsValid(pGso, sizeof(*pGso), pSgBuf->cbUsed))
            {
                uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, pSgBuf->cbUsed);  Assert(cSegs > 1);
                for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
                {
                    size_t cbSeg;
                    void  *pvSeg;
                    m = slirp_ext_m_get(pThis->pNATState, pGso->cbHdrsTotal + pGso->cbMaxSeg, &pvSeg, &cbSeg);
                    if (!m)
                        break;

#if 1
                    uint32_t cbPayload, cbHdrs;
                    uint32_t offPayload = PDMNetGsoCarveSegment(pGso, pbFrame, pSgBuf->cbUsed,
                                                                iSeg, cSegs, (uint8_t *)pvSeg, &cbHdrs, &cbPayload);
                    memcpy((uint8_t *)pvSeg + cbHdrs, pbFrame + offPayload, cbPayload);

                    slirp_input(pThis->pNATState, m, cbPayload + cbHdrs);
#else
                    uint32_t cbSegFrame;
                    void *pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)pbFrame, pSgBuf->cbUsed, abHdrScratch,
                                                            iSeg, cSegs, &cbSegFrame);
                    memcpy((uint8_t *)pvSeg, pvSegFrame, cbSegFrame);

                    slirp_input(pThis->pNATState, m, cbSegFrame);
#endif
                }
            }
        }
    }
    drvNATFreeSgBuf(pThis, pSgBuf);

    /** @todo Implement the VERR_TRY_AGAIN drvNATNetworkUp_AllocBuf semantics. */
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
    PPDMSCATTERGATHER pSgBuf = (PPDMSCATTERGATHER)RTMemAlloc(sizeof(*pSgBuf));
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
        pSgBuf->pvAllocator = slirp_ext_m_get(pThis->pNATState, cbMin,
                                              &pSgBuf->aSegs[0].pvSeg, &pSgBuf->aSegs[0].cbSeg);
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
        pSgBuf->aSegs[0].cbSeg = RT_ALIGN_Z(cbMin, 16);
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

#if 0 /* poison */
    memset(pSgBuf->aSegs[0].pvSeg, 'F', pSgBuf->aSegs[0].cbSeg);
#endif
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

    int rc;
    if (pThis->pSlirpThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        rc = RTReqQueueCallEx(pThis->hSlirpReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvNATSendWorker, 2, pThis, pSgBuf);
        if (RT_SUCCESS(rc))
        {
            drvNATNotifyNATThread(pThis, "drvNATNetworkUp_SendBuf");
            return VINF_SUCCESS;
        }

        rc = VERR_NET_NO_BUFFER_SPACE;
    }
    else
        rc = VERR_NET_DOWN;
    drvNATFreeSgBuf(pThis, pSgBuf);
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
    int rc;
#ifndef RT_OS_WINDOWS
    /* kick poll() */
    size_t cbIgnored;
    rc = RTPipeWrite(pThis->hPipeWrite, "", 1, &cbIgnored);
#else
    /* kick WSAWaitForMultipleEvents */
    rc = WSASetEvent(pThis->hWakeupEvent);
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
 */
static DECLCALLBACK(void) drvNATNotifyLinkChangedWorker(PDRVNAT pThis, PDMNETWORKLINKSTATE enmLinkState)
{
    pThis->enmLinkState = pThis->enmLinkStateWant = enmLinkState;
    switch (enmLinkState)
    {
        case PDMNETWORKLINKSTATE_UP:
            LogRel(("NAT: Link up\n"));
            slirp_link_up(pThis->pNATState);
            break;

        case PDMNETWORKLINKSTATE_DOWN:
        case PDMNETWORKLINKSTATE_DOWN_RESUME:
            LogRel(("NAT: Link down\n"));
            slirp_link_down(pThis->pNATState);
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

static DECLCALLBACK(void) drvNATNotifyApplyPortForwardCommand(PDRVNAT pThis, bool fRemove,
                                                              bool fUdp, const char *pHostIp,
                                                              uint16_t u16HostPort, const char *pGuestIp, uint16_t u16GuestPort)
{
    struct in_addr guestIp, hostIp;

    if (   pHostIp == NULL
        || inet_aton(pHostIp, &hostIp) == 0)
        hostIp.s_addr = INADDR_ANY;

    if (   pGuestIp == NULL
        || inet_aton(pGuestIp, &guestIp) == 0)
        guestIp.s_addr = pThis->GuestIP;

    if (fRemove)
        slirp_remove_redirect(pThis->pNATState, fUdp, hostIp, u16HostPort, guestIp, u16GuestPort);
    else
        slirp_add_redirect(pThis->pNATState, fUdp, hostIp, u16HostPort, guestIp, u16GuestPort);
}

static DECLCALLBACK(int) drvNATNetworkNatConfigRedirect(PPDMINETWORKNATCONFIG pInterface, bool fRemove,
                                                        bool fUdp, const char *pHostIp, uint16_t u16HostPort,
                                                        const char *pGuestIp, uint16_t u16GuestPort)
{
    LogFlowFunc(("fRemove=%d, fUdp=%d, pHostIp=%s, u16HostPort=%u, pGuestIp=%s, u16GuestPort=%u\n",
                 RT_BOOL(fRemove), RT_BOOL(fUdp), pHostIp, u16HostPort, pGuestIp, u16GuestPort));
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkNATCfg);
    /* Execute the command directly if the VM is not running. */
    int rc;
    if (pThis->pSlirpThread->enmState != PDMTHREADSTATE_RUNNING)
    {
        drvNATNotifyApplyPortForwardCommand(pThis, fRemove, fUdp, pHostIp,
                                           u16HostPort, pGuestIp,u16GuestPort);
        rc = VINF_SUCCESS;
    }
    else
    {
        PRTREQ pReq;
        rc = RTReqQueueCallEx(pThis->hSlirpReqQueue, &pReq, 0 /*cMillies*/, RTREQFLAGS_VOID,
                              (PFNRT)drvNATNotifyApplyPortForwardCommand, 7, pThis, fRemove,
                              fUdp, pHostIp, u16HostPort, pGuestIp, u16GuestPort);
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
 * NAT thread handling the slirp stuff.
 *
 * The slirp implementation is single-threaded so we execute this enginre in a
 * dedicated thread. We take care that this thread does not become the
 * bottleneck: If the guest wants to send, a request is enqueued into the
 * hSlirpReqQueue and handled asynchronously by this thread.  If this thread
 * wants to deliver packets to the guest, it enqueues a request into
 * hRecvReqQueue which is later handled by the Recv thread.
 */
static DECLCALLBACK(int) drvNATAsyncIoThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    int     nFDs = -1;
#ifdef RT_OS_WINDOWS
    HANDLE  *phEvents = slirp_get_events(pThis->pNATState);
    unsigned int cBreak = 0;
#else /* RT_OS_WINDOWS */
    unsigned int cPollNegRet = 0;
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
        nFDs = slirp_get_nsock(pThis->pNATState);
        /* allocation for all sockets + Management pipe */
        struct pollfd *polls = (struct pollfd *)RTMemAlloc((1 + nFDs) * sizeof(struct pollfd) + sizeof(uint32_t));
        if (polls == NULL)
            return VERR_NO_MEMORY;

        /* don't pass the management pipe */
        slirp_select_fill(pThis->pNATState, &nFDs, &polls[1]);

        polls[0].fd = RTPipeToNative(pThis->hPipeRead);
        /* POLLRDBAND usually doesn't used on Linux but seems used on Solaris */
        polls[0].events = POLLRDNORM | POLLPRI | POLLRDBAND;
        polls[0].revents = 0;

        int cChangedFDs = poll(polls, nFDs + 1, slirp_get_timeout_ms(pThis->pNATState));
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

        if (cChangedFDs >= 0)
        {
            slirp_select_poll(pThis->pNATState, &polls[1], nFDs);
            if (polls[0].revents & (POLLRDNORM|POLLPRI|POLLRDBAND))
            {
                /* drain the pipe
                 *
                 * Note! drvNATSend decoupled so we don't know how many times
                 * device's thread sends before we've entered multiplex,
                 * so to avoid false alarm drain pipe here to the very end
                 *
                 * @todo: Probably we should counter drvNATSend to count how
                 * deep pipe has been filed before drain.
                 *
                 */
                /** @todo XXX: Make it reading exactly we need to drain the
                 * pipe.*/
                char ch;
                size_t cbRead;
                RTPipeRead(pThis->hPipeRead, &ch, 1, &cbRead);
            }
        }
        /* process _all_ outstanding requests but don't wait */
        RTReqQueueProcess(pThis->hSlirpReqQueue, 0);
        RTMemFree(polls);

#else /* RT_OS_WINDOWS */
        nFDs = -1;
        slirp_select_fill(pThis->pNATState, &nFDs);
        DWORD dwEvent = WSAWaitForMultipleEvents(nFDs, phEvents, FALSE,
                                                 slirp_get_timeout_ms(pThis->pNATState),
                                                 /* :fAlertable */ TRUE);
        AssertCompile(WSA_WAIT_EVENT_0 == 0);
        if (   (/*dwEvent < WSA_WAIT_EVENT_0 ||*/ dwEvent > WSA_WAIT_EVENT_0 + nFDs - 1)
            && dwEvent != WSA_WAIT_TIMEOUT && dwEvent != WSA_WAIT_IO_COMPLETION)
        {
            int error = WSAGetLastError();
            LogRel(("NAT: WSAWaitForMultipleEvents returned %d (error %d)\n", dwEvent, error));
            RTAssertPanic();
        }

        if (dwEvent == WSA_WAIT_TIMEOUT)
        {
            /* only check for slow/fast timers */
            slirp_select_poll(pThis->pNATState, /* fTimeout=*/true);
            continue;
        }
        /* poll the sockets in any case */
        Log2(("%s: poll\n", __FUNCTION__));
        slirp_select_poll(pThis->pNATState, /* fTimeout=*/false);
        /* process _all_ outstanding requests but don't wait */
        RTReqQueueProcess(pThis->hSlirpReqQueue, 0);
# ifdef VBOX_NAT_DELAY_HACK
        if (cBreak++ > 128)
        {
            cBreak = 0;
            RTThreadSleep(2);
        }
# endif
#endif /* RT_OS_WINDOWS */
    }

    return VINF_SUCCESS;
}


/**
 * Unblock the send thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The pcnet device instance.
 * @param   pThread     The send thread.
 */
static DECLCALLBACK(int) drvNATAsyncIoWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);

    drvNATNotifyNATThread(pThis, "drvNATAsyncIoWakeup");
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) drvNATHostResThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        RTReqQueueProcess(pThis->hHostResQueue, RT_INDEFINITE_WAIT);
    }

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) drvNATReqQueueInterrupt()
{
    /*
     * RTReqQueueProcess loops until request returns a warning or info
     * status code (other than VINF_SUCCESS).
     */
    return VINF_INTERRUPTED;
}


static DECLCALLBACK(int) drvNATHostResWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    Assert(pThis != NULL);

    int rc;
    rc = RTReqQueueCallEx(pThis->hHostResQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_IPRT_STATUS | RTREQFLAGS_NO_WAIT,
                          (PFNRT)drvNATReqQueueInterrupt, 0);
    return rc;
}


#if 0 /* unused */
/**
 * Function called by slirp to check if it's possible to feed incoming data to the network port.
 * @returns 1 if possible.
 * @returns 0 if not possible.
 */
int slirp_can_output(void *pvUser)
{
    RT_NOREF(pvUser);
    return 1;
}

static void slirp_push_recv_thread(void *pvUser)
{
    PDRVNAT pThis = (PDRVNAT)pvUser;
    Assert(pThis);
    drvNATUrgRecvWakeup(pThis->pDrvIns, pThis->pUrgRecvThread);
}
#endif

void slirp_urg_output(void *pvUser, struct mbuf *m, const uint8_t *pu8Buf, int cb)
{
    PDRVNAT pThis = (PDRVNAT)pvUser;
    Assert(pThis);

    /* don't queue new requests when the NAT thread is about to stop */
    if (pThis->pSlirpThread->enmState != PDMTHREADSTATE_RUNNING)
        return;

    ASMAtomicIncU32(&pThis->cUrgPkts);
    int rc = RTReqQueueCallEx(pThis->hUrgRecvReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvNATUrgRecvWorker, 4, pThis, pu8Buf, cb, m);
    AssertRC(rc);
    drvNATUrgRecvWakeup(pThis->pDrvIns, pThis->pUrgRecvThread);
}

/**
 * Function called by slirp to wake up device after VERR_TRY_AGAIN
 */
void slirp_output_pending(void *pvUser)
{
    PDRVNAT pThis = (PDRVNAT)pvUser;
    Assert(pThis);
    LogFlowFuncEnter();
    pThis->pIAboveNet->pfnXmitPending(pThis->pIAboveNet);
    LogFlowFuncLeave();
}

/**
 * Function called by slirp to feed incoming data to the NIC.
 */
void slirp_output(void *pvUser, struct mbuf *m, const uint8_t *pu8Buf, int cb)
{
    PDRVNAT pThis = (PDRVNAT)pvUser;
    Assert(pThis);

    LogFlow(("slirp_output BEGIN %p %d\n", pu8Buf, cb));
    Log6(("slirp_output: pu8Buf=%p cb=%#x (pThis=%p)\n%.*Rhxd\n", pu8Buf, cb, pThis, cb, pu8Buf));

    /* don't queue new requests when the NAT thread is about to stop */
    if (pThis->pSlirpThread->enmState != PDMTHREADSTATE_RUNNING)
        return;

    ASMAtomicIncU32(&pThis->cPkts);
    int rc = RTReqQueueCallEx(pThis->hRecvReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvNATRecvWorker, 4, pThis, pu8Buf, cb, m);
    AssertRC(rc);
    drvNATRecvWakeup(pThis->pDrvIns, pThis->pRecvThread);
    STAM_COUNTER_INC(&pThis->StatQueuePktSent);
    LogFlowFuncLeave();
}


/*
 * Call a function on the slirp thread.
 */
int slirp_call(void *pvUser, PRTREQ *ppReq, RTMSINTERVAL cMillies, unsigned fFlags, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PDRVNAT pThis = (PDRVNAT)pvUser;
    Assert(pThis);

    int rc;

    va_list va;
    va_start(va, cArgs);

    rc = RTReqQueueCallV(pThis->hSlirpReqQueue, ppReq, cMillies, fFlags, pfnFunction, cArgs, va);

    va_end(va);

    if (RT_SUCCESS(rc))
        drvNATNotifyNATThread(pThis, "slirp_vcall");

    return rc;
}


/*
 * Call a function on the host resolver thread.
 */
int slirp_call_hostres(void *pvUser, PRTREQ *ppReq, RTMSINTERVAL cMillies,
                       unsigned fFlags, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PDRVNAT pThis = (PDRVNAT)pvUser;
    Assert(pThis);

    int rc;

    AssertReturn((pThis->hHostResQueue != NIL_RTREQQUEUE), VERR_INVALID_STATE);
    AssertReturn((pThis->pHostResThread != NULL), VERR_INVALID_STATE);

    va_list va;
    va_start(va, cArgs);

    rc = RTReqQueueCallV(pThis->hHostResQueue, ppReq, cMillies, fFlags, pfnFunction, cArgs, va);

    va_end(va);
    return rc;
}


#if HAVE_NOTIFICATION_FOR_DNS_UPDATE && !defined(RT_OS_DARWIN)
/**
 * @interface_method_impl{PDMINETWORKNATCONFIG,pfnNotifyDnsChanged}
 *
 * We are notified that host's resolver configuration has changed.  In
 * the current setup we don't get any details and just reread that
 * information ourselves.
 */
static DECLCALLBACK(void) drvNATNotifyDnsChanged(PPDMINETWORKNATCONFIG pInterface, PCPDMINETWORKNATDNSCONFIG pDnsConf)
{
    RT_NOREF(pDnsConf);
    PDRVNAT pThis = RT_FROM_MEMBER(pInterface, DRVNAT, INetworkNATCfg);
    drvNATUpdateDNS(pThis, /* fFlapLink */ true);
}
#endif

#ifdef RT_OS_DARWIN
/**
 * Callback for the SystemConfiguration framework to notify us whenever the DNS
 * server changes.
 *
 * @param   hDynStor    The DynamicStore handle.
 * @param   hChangedKey Array of changed keys we watch for.
 * @param   pvUser      Opaque user data (NAT driver instance).
 */
static DECLCALLBACK(void) drvNatDnsChanged(SCDynamicStoreRef hDynStor, CFArrayRef hChangedKeys, void *pvUser)
{
    PDRVNAT pThis = (PDRVNAT)pvUser;

    Log2(("NAT: System configuration has changed\n"));

    /* Check if any of parameters we are interested in were actually changed. If the size
     * of hChangedKeys is 0, it means that SCDynamicStore has been restarted. */
    if (hChangedKeys && CFArrayGetCount(hChangedKeys) > 0)
    {
        /* Look to the updated parameters in particular. */
        CFStringRef pDNSKey = CFSTR("State:/Network/Global/DNS");

        if (CFArrayContainsValue(hChangedKeys, CFRangeMake(0, CFArrayGetCount(hChangedKeys)), pDNSKey))
        {
            LogRel(("NAT: DNS servers changed, triggering reconnect\n"));
#if 0
            CFDictionaryRef hDnsDict = (CFDictionaryRef)SCDynamicStoreCopyValue(hDynStor, pDNSKey);
            if (hDnsDict)
            {
                CFArrayRef hArrAddresses = (CFArrayRef)CFDictionaryGetValue(hDnsDict, kSCPropNetDNSServerAddresses);
                if (hArrAddresses && CFArrayGetCount(hArrAddresses) > 0)
                {
                    /* Dump DNS servers list. */
                    for (int i = 0; i < CFArrayGetCount(hArrAddresses); i++)
                    {
                        CFStringRef pDNSAddrStr = (CFStringRef)CFArrayGetValueAtIndex(hArrAddresses, i);
                        const char *pszDNSAddr  =  pDNSAddrStr ? CFStringGetCStringPtr(pDNSAddrStr, CFStringGetSystemEncoding()) : NULL;
                        LogRel(("NAT: New DNS server#%d: %s\n", i, pszDNSAddr ? pszDNSAddr : "None"));
                    }
                }
                else
                    LogRel(("NAT: DNS server list is empty (1)\n"));

                CFRelease(hDnsDict);
            }
            else
                LogRel(("NAT: DNS server list is empty (2)\n"));
#else
            RT_NOREF(hDynStor);
#endif
            drvNATUpdateDNS(pThis, /* fFlapLink */ true);
        }
        else
            Log2(("NAT: No DNS changes detected\n"));
    }
    else
        Log2(("NAT: SCDynamicStore has been restarted\n"));
}
#endif

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
 * Get the MAC address into the slirp stack.
 *
 * Called by drvNATLoadDone and drvNATPowerOn.
 */
static void drvNATSetMac(PDRVNAT pThis)
{
#if 0 /* XXX: do we still need this for anything? */
    if (pThis->pIAboveConfig)
    {
        RTMAC Mac;
        pThis->pIAboveConfig->pfnGetMac(pThis->pIAboveConfig, &Mac);
    }
#else
    RT_NOREF(pThis);
#endif
}


/**
 * After loading we have to pass the MAC address of the ethernet device to the slirp stack.
 * Otherwise the guest is not reachable until it performs a DHCP request or an ARP request
 * (usually done during guest boot).
 */
static DECLCALLBACK(int) drvNATLoadDone(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    drvNATSetMac(pThis);
    return VINF_SUCCESS;
}


/**
 * Some guests might not use DHCP to retrieve an IP but use a static IP.
 */
static DECLCALLBACK(void) drvNATPowerOn(PPDMDRVINS pDrvIns)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    drvNATSetMac(pThis);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnResume}
 */
static DECLCALLBACK(void) drvNATResume(PPDMDRVINS pDrvIns)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    VMRESUMEREASON enmReason = PDMDrvHlpVMGetResumeReason(pDrvIns);

    switch (enmReason)
    {
        case VMRESUMEREASON_HOST_RESUME:
            bool fFlapLink;
#if HAVE_NOTIFICATION_FOR_DNS_UPDATE
            /* let event handler do it if necessary */
            fFlapLink = false;
#else
            /* XXX: when in doubt, use brute force */
            fFlapLink = true;
#endif
            drvNATUpdateDNS(pThis, fFlapLink);
            return;
        default: /* Ignore every other resume reason. */
            /* do nothing */
            return;
    }
}


static DECLCALLBACK(int) drvNATReinitializeHostNameResolving(PDRVNAT pThis)
{
    slirpReleaseDnsSettings(pThis->pNATState);
    slirpInitializeDnsSettings(pThis->pNATState);
    return VINF_SUCCESS;
}

/**
 * This function at this stage could be called from two places, but both from non-NAT thread,
 * - drvNATResume (EMT?)
 * - drvNatDnsChanged (darwin, GUI or main) "listener"
 * When Main's interface IHost will support host network configuration change event on every host,
 * we won't call it from drvNATResume, but from listener of Main event in the similar way it done
 * for port-forwarding, and it wan't be on GUI/main thread, but on EMT thread only.
 *
 * Thread here is important, because we need to change DNS server list and domain name (+ perhaps,
 * search string) at runtime (VBOX_NAT_ENFORCE_INTERNAL_DNS_UPDATE), we can do it safely on NAT thread,
 * so with changing other variables (place where we handle update) the main mechanism of update
 * _won't_ be changed, the only thing will change is drop of fFlapLink parameter.
 */
DECLINLINE(void) drvNATUpdateDNS(PDRVNAT pThis, bool fFlapLink)
{
    int strategy = slirp_host_network_configuration_change_strategy_selector(pThis->pNATState);
    switch (strategy)
    {
        case VBOX_NAT_DNS_DNSPROXY:
        {
            /**
             * XXX: Here or in _strategy_selector we should deal with network change
             * in "network change" scenario domain name change we have to update guest lease
             * forcibly.
             * Note at that built-in dhcp also updates DNS information on NAT thread.
             */
            /**
             * It's unsafe to to do it directly on non-NAT thread
             * so we schedule the worker and kick the NAT thread.
             */
            int rc = RTReqQueueCallEx(pThis->hSlirpReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                      (PFNRT)drvNATReinitializeHostNameResolving, 1, pThis);
            if (RT_SUCCESS(rc))
                drvNATNotifyNATThread(pThis, "drvNATUpdateDNS");

            return;
        }

        case VBOX_NAT_DNS_EXTERNAL:
            /*
             * Host resumed from a suspend and the network might have changed.
             * Disconnect the guest from the network temporarily to let it pick up the changes.
             */
            if (fFlapLink)
                pThis->pIAboveConfig->pfnSetLinkState(pThis->pIAboveConfig,
                                                      PDMNETWORKLINKSTATE_DOWN_RESUME);
            return;

        case VBOX_NAT_DNS_HOSTRESOLVER:
        default:
            return;
    }
}


/**
 * Info handler.
 */
static DECLCALLBACK(void) drvNATInfo(PPDMDRVINS pDrvIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PDRVNAT pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    slirp_info(pThis->pNATState, pHlp, pszArgs);
}

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
static int drvNATConstructDNSMappings(unsigned iInstance, PDRVNAT pThis, PCFGMNODE pMappingsCfg)
{
    PPDMDRVINS pDrvIns = pThis->pDrvIns;
    PCPDMDRVHLPR3 pHlp = pDrvIns->pHlpR3;

    RT_NOREF(iInstance);
    int rc = VINF_SUCCESS;
    LogFlowFunc(("ENTER: iInstance:%d\n", iInstance));
    for (PCFGMNODE pNode = pHlp->pfnCFGMGetFirstChild(pMappingsCfg); pNode; pNode = pHlp->pfnCFGMGetNextChild(pNode))
    {
        if (!pHlp->pfnCFGMAreValuesValid(pNode, "HostName\0HostNamePattern\0HostIP\0"))
            return PDMDRV_SET_ERROR(pThis->pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                    N_("Unknown configuration in dns mapping"));
        char szHostNameOrPattern[255];
        bool fPattern = false;
        RT_ZERO(szHostNameOrPattern);
        GET_STRING(rc, pDrvIns, pNode, "HostName", szHostNameOrPattern[0], sizeof(szHostNameOrPattern));
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            GET_STRING(rc, pDrvIns, pNode, "HostNamePattern", szHostNameOrPattern[0], sizeof(szHostNameOrPattern));
            if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            {
                char szNodeName[225];
                RT_ZERO(szNodeName);
                pHlp->pfnCFGMGetName(pNode, szNodeName, sizeof(szNodeName));
                LogRel(("NAT: Neither 'HostName' nor 'HostNamePattern' is specified for mapping %s\n", szNodeName));
                continue;
            }
            fPattern = true;
        }
        struct in_addr HostIP;
        RT_ZERO(HostIP);
        GETIP_DEF(rc, pDrvIns, pNode, HostIP, INADDR_ANY);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            LogRel(("NAT: DNS mapping %s is ignored (address not pointed)\n", szHostNameOrPattern));
            continue;
        }
        slirp_add_host_resolver_mapping(pThis->pNATState, szHostNameOrPattern, fPattern, HostIP.s_addr);
    }
    LogFlowFunc(("LEAVE: %Rrc\n", rc));
    return rc;
}
#endif /* !VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER */


/**
 * Sets up the redirectors.
 *
 * @returns VBox status code.
 * @param   pCfg            The configuration handle.
 */
static int drvNATConstructRedir(unsigned iInstance, PDRVNAT pThis, PCFGMNODE pCfg, PRTNETADDRIPV4 pNetwork)
{
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

        /* host address ("BindIP" name is rather unfortunate given "HostPort" to go with it) */
        struct in_addr BindIP;
        RT_ZERO(BindIP);
        GETIP_DEF(rc, pDrvIns, pNode, BindIP, INADDR_ANY);

        /* guest address */
        struct in_addr GuestIP;
        RT_ZERO(GuestIP);
        GETIP_DEF(rc, pDrvIns, pNode, GuestIP, INADDR_ANY);

        /*
         * Call slirp about it.
         */
        if (slirp_add_redirect(pThis->pNATState, fUDP, BindIP, iHostPort, GuestIP, iGuestPort) < 0)
            return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_NAT_REDIR_SETUP, RT_SRC_POS,
                                       N_("NAT#%d: configuration error: failed to set up "
                                       "redirection of %d to %d. Probably a conflict with "
                                       "existing services or other rules"), iInstance, iHostPort,
                                       iGuestPort);
    } /* for each redir rule */

    return VINF_SUCCESS;
}


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

    if (pThis->pNATState)
    {
        slirp_term(pThis->pNATState);
        slirp_deregister_statistics(pThis->pNATState, pDrvIns);
#ifdef VBOX_WITH_STATISTICS
# define DRV_PROFILE_COUNTER(name, dsc)     DEREGISTER_COUNTER(name, pThis)
# define DRV_COUNTING_COUNTER(name, dsc)    DEREGISTER_COUNTER(name, pThis)
# include "counters.h"
#endif
        pThis->pNATState = NULL;
    }

    RTReqQueueDestroy(pThis->hHostResQueue);
    pThis->hHostResQueue = NIL_RTREQQUEUE;

    RTReqQueueDestroy(pThis->hSlirpReqQueue);
    pThis->hSlirpReqQueue = NIL_RTREQQUEUE;

    RTReqQueueDestroy(pThis->hUrgRecvReqQueue);
    pThis->hUrgRecvReqQueue = NIL_RTREQQUEUE;

    RTReqQueueDestroy(pThis->hRecvReqQueue);
    pThis->hRecvReqQueue = NIL_RTREQQUEUE;

    RTSemEventDestroy(pThis->EventRecv);
    pThis->EventRecv = NIL_RTSEMEVENT;

    RTSemEventDestroy(pThis->EventUrgRecv);
    pThis->EventUrgRecv = NIL_RTSEMEVENT;

    if (RTCritSectIsInitialized(&pThis->DevAccessLock))
        RTCritSectDelete(&pThis->DevAccessLock);

    if (RTCritSectIsInitialized(&pThis->XmitLock))
        RTCritSectDelete(&pThis->XmitLock);

#ifndef RT_OS_WINDOWS
    RTPipeClose(pThis->hPipeRead);
    RTPipeClose(pThis->hPipeWrite);
#endif

#ifdef RT_OS_DARWIN
    /* Cleanup the DNS watcher. */
    if (pThis->hRunLoopSrcDnsWatcher != NULL)
    {
        CFRunLoopRef hRunLoopMain = CFRunLoopGetMain();
        CFRetain(hRunLoopMain);
        CFRunLoopRemoveSource(hRunLoopMain, pThis->hRunLoopSrcDnsWatcher, kCFRunLoopCommonModes);
        CFRelease(hRunLoopMain);
        CFRelease(pThis->hRunLoopSrcDnsWatcher);
        pThis->hRunLoopSrcDnsWatcher = NULL;
    }
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
    PDRVNAT         pThis = PDMINS_2_DATA(pDrvIns, PDRVNAT);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    LogFlow(("drvNATConstruct:\n"));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    pThis->pNATState                    = NULL;
    pThis->pszTFTPPrefix                = NULL;
    pThis->pszBootFile                  = NULL;
    pThis->pszNextServer                = NULL;
    pThis->hSlirpReqQueue               = NIL_RTREQQUEUE;
    pThis->hUrgRecvReqQueue             = NIL_RTREQQUEUE;
    pThis->hHostResQueue                = NIL_RTREQQUEUE;
    pThis->EventRecv                    = NIL_RTSEMEVENT;
    pThis->EventUrgRecv                 = NIL_RTSEMEVENT;
#ifdef RT_OS_DARWIN
    pThis->hRunLoopSrcDnsWatcher        = NULL;
#endif

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
#if HAVE_NOTIFICATION_FOR_DNS_UPDATE && !defined(RT_OS_DARWIN)
    /*
     * On OS X we stick to the old OS X specific notifications for
     * now.  Elsewhere use IHostNameResolutionConfigurationChangeEvent
     * by enbaling HAVE_NOTIFICATION_FOR_DNS_UPDATE in libslirp.h.
     * This code is still in a bit of flux and is implemented and
     * enabled in steps to simplify more conservative backporting.
     */
    pThis->INetworkNATCfg.pfnNotifyDnsChanged = drvNATNotifyDnsChanged;
#else
    pThis->INetworkNATCfg.pfnNotifyDnsChanged = NULL;
#endif

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
//#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
                                  "|HostResolverMappings"
//#endif
                                  , "PortForwarding");

    /*
     * Get the configuration settings.
     */
    int rc;
    bool fPassDomain = true;
    GET_BOOL(rc, pDrvIns, pCfg, "PassDomain", fPassDomain);

    GET_STRING_ALLOC(rc, pDrvIns, pCfg, "TFTPPrefix", pThis->pszTFTPPrefix);
    GET_STRING_ALLOC(rc, pDrvIns, pCfg, "BootFile", pThis->pszBootFile);
    GET_STRING_ALLOC(rc, pDrvIns, pCfg, "NextServer", pThis->pszNextServer);

    int fDNSProxy = 0;
    GET_S32(rc, pDrvIns, pCfg, "DNSProxy", fDNSProxy);
    int fUseHostResolver = 0;
    GET_S32(rc, pDrvIns, pCfg, "UseHostResolver", fUseHostResolver);
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

    /*
     * Initialize slirp.
     */
    rc = slirp_init(&pThis->pNATState, RT_H2N_U32(Network.u), Netmask.u,
                    fPassDomain, !!fUseHostResolver, i32AliasMode,
                    iIcmpCacheLimit, fLocalhostReachable, pThis);
    if (RT_SUCCESS(rc))
    {
        slirp_set_dhcp_TFTP_prefix(pThis->pNATState, pThis->pszTFTPPrefix);
        slirp_set_dhcp_TFTP_bootfile(pThis->pNATState, pThis->pszBootFile);
        slirp_set_dhcp_next_server(pThis->pNATState, pThis->pszNextServer);
        slirp_set_dhcp_dns_proxy(pThis->pNATState, !!fDNSProxy);
        slirp_set_mtu(pThis->pNATState, MTU);
        slirp_set_somaxconn(pThis->pNATState, i32SoMaxConn);

        char *pszBindIP = NULL;
        GET_STRING_ALLOC(rc, pDrvIns, pCfg, "BindIP", pszBindIP);
        slirp_set_binding_address(pThis->pNATState, pszBindIP);
        if (pszBindIP != NULL)
            PDMDrvHlpMMHeapFree(pDrvIns, pszBindIP);

#define SLIRP_SET_TUNING_VALUE(name, setter)                    \
            do                                                  \
            {                                                   \
                int len = 0;                                    \
                rc = pHlp->pfnCFGMQueryS32(pCfg, name, &len);    \
                if (RT_SUCCESS(rc))                             \
                    setter(pThis->pNATState, len);              \
            } while(0)

        SLIRP_SET_TUNING_VALUE("SockRcv", slirp_set_rcvbuf);
        SLIRP_SET_TUNING_VALUE("SockSnd", slirp_set_sndbuf);
        SLIRP_SET_TUNING_VALUE("TcpRcv", slirp_set_tcp_rcvspace);
        SLIRP_SET_TUNING_VALUE("TcpSnd", slirp_set_tcp_sndspace);

        slirp_register_statistics(pThis->pNATState, pDrvIns);
#ifdef VBOX_WITH_STATISTICS
# define DRV_PROFILE_COUNTER(name, dsc)     REGISTER_COUNTER(name, pThis, STAMTYPE_PROFILE, STAMUNIT_TICKS_PER_CALL, dsc)
# define DRV_COUNTING_COUNTER(name, dsc)    REGISTER_COUNTER(name, pThis, STAMTYPE_COUNTER, STAMUNIT_COUNT,          dsc)
# include "counters.h"
#endif

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
        PCFGMNODE pMappingsCfg = pHlp->pfnCFGMGetChild(pCfg, "HostResolverMappings");

        if (pMappingsCfg)
        {
            rc = drvNATConstructDNSMappings(pDrvIns->iInstance, pThis, pMappingsCfg);
            AssertRC(rc);
        }
#endif
        rc = drvNATConstructRedir(pDrvIns->iInstance, pThis, pCfg, &Network);
        if (RT_SUCCESS(rc))
        {
            /*
             * Register a load done notification to get the MAC address into the slirp
             * engine after we loaded a guest state.
             */
            rc = PDMDrvHlpSSMRegisterLoadDone(pDrvIns, drvNATLoadDone);
            AssertLogRelRCReturn(rc, rc);

            rc = RTReqQueueCreate(&pThis->hSlirpReqQueue);
            AssertLogRelRCReturn(rc, rc);

            rc = RTReqQueueCreate(&pThis->hRecvReqQueue);
            AssertLogRelRCReturn(rc, rc);

            rc = RTReqQueueCreate(&pThis->hUrgRecvReqQueue);
            AssertLogRelRCReturn(rc, rc);

            rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pRecvThread, pThis, drvNATRecv,
                                       drvNATRecvWakeup, 128 * _1K, RTTHREADTYPE_IO, "NATRX");
            AssertRCReturn(rc, rc);

            rc = RTSemEventCreate(&pThis->EventRecv);
            AssertRCReturn(rc, rc);

            rc = RTSemEventCreate(&pThis->EventUrgRecv);
            AssertRCReturn(rc, rc);

            rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pUrgRecvThread, pThis, drvNATUrgRecv,
                                       drvNATUrgRecvWakeup, 128 * _1K, RTTHREADTYPE_IO, "NATURGRX");
            AssertRCReturn(rc, rc);

            rc = RTReqQueueCreate(&pThis->hHostResQueue);
            AssertRCReturn(rc, rc);

#if defined(RT_OS_LINUX) && defined(RT_ARCH_ARM64)
            /* 64KiB stacks are not supported at least linux.arm64 (thread creation fails). */
            size_t const cbStack = _128K;
#else
            size_t const cbStack = _64K;
#endif
            rc = PDMDrvHlpThreadCreate(pThis->pDrvIns, &pThis->pHostResThread,
                                       pThis, drvNATHostResThread, drvNATHostResWakeup,
                                       cbStack, RTTHREADTYPE_IO, "HOSTRES");
            AssertRCReturn(rc, rc);

            rc = RTCritSectInit(&pThis->DevAccessLock);
            AssertRCReturn(rc, rc);

            rc = RTCritSectInit(&pThis->XmitLock);
            AssertRCReturn(rc, rc);

            char szTmp[128];
            RTStrPrintf(szTmp, sizeof(szTmp), "nat%d", pDrvIns->iInstance);
            PDMDrvHlpDBGFInfoRegister(pDrvIns, szTmp, "NAT info.", drvNATInfo);

#ifndef RT_OS_WINDOWS
            /*
             * Create the control pipe.
             */
            rc = RTPipeCreate(&pThis->hPipeRead, &pThis->hPipeWrite, 0 /*fFlags*/);
            AssertRCReturn(rc, rc);
#else
            pThis->hWakeupEvent = CreateEvent(NULL, FALSE, FALSE, NULL); /* auto-reset event */
            slirp_register_external_event(pThis->pNATState, pThis->hWakeupEvent,
                                          VBOX_WAKEUP_EVENT_INDEX);
#endif

            rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pSlirpThread, pThis, drvNATAsyncIoThread,
                                       drvNATAsyncIoWakeup, 128 * _1K, RTTHREADTYPE_IO, "NAT");
            AssertRCReturn(rc, rc);

            pThis->enmLinkState = pThis->enmLinkStateWant = PDMNETWORKLINKSTATE_UP;

#ifdef RT_OS_DARWIN
            /* Set up a watcher which notifies us everytime the DNS server changes. */
            int rc2 = VINF_SUCCESS;
            SCDynamicStoreContext SCDynStorCtx;

            SCDynStorCtx.version = 0;
            SCDynStorCtx.info = pThis;
            SCDynStorCtx.retain = NULL;
            SCDynStorCtx.release = NULL;
            SCDynStorCtx.copyDescription = NULL;

            SCDynamicStoreRef hDynStor = SCDynamicStoreCreate(NULL, CFSTR("org.virtualbox.drvnat"), drvNatDnsChanged, &SCDynStorCtx);
            if (hDynStor)
            {
                CFRunLoopSourceRef hRunLoopSrc = SCDynamicStoreCreateRunLoopSource(NULL, hDynStor, 0);
                if (hRunLoopSrc)
                {
                    CFStringRef aWatchKeys[] =
                    {
                        CFSTR("State:/Network/Global/DNS")
                    };
                    CFArrayRef hArray = CFArrayCreate(NULL, (const void **)aWatchKeys, 1, &kCFTypeArrayCallBacks);

                    if (hArray)
                    {
                        if (SCDynamicStoreSetNotificationKeys(hDynStor, hArray, NULL))
                        {
                            CFRunLoopRef hRunLoopMain = CFRunLoopGetMain();
                            CFRetain(hRunLoopMain);
                            CFRunLoopAddSource(hRunLoopMain, hRunLoopSrc, kCFRunLoopCommonModes);
                            CFRelease(hRunLoopMain);
                            pThis->hRunLoopSrcDnsWatcher = hRunLoopSrc;
                        }
                        else
                            rc2 = VERR_NO_MEMORY;

                        CFRelease(hArray);
                    }
                    else
                        rc2 = VERR_NO_MEMORY;

                    if (RT_FAILURE(rc2)) /* Keep the runloop source referenced for destruction. */
                        CFRelease(hRunLoopSrc);
                }
                CFRelease(hDynStor);
            }
            else
                rc2 = VERR_NO_MEMORY;

            if (RT_FAILURE(rc2))
                LogRel(("NAT#%d: Failed to install DNS change notifier. The guest might loose DNS access when switching networks on the host\n",
                         pDrvIns->iInstance));
#endif
            return rc;
        }

        /* failure path */
        slirp_term(pThis->pNATState);
        pThis->pNATState = NULL;
    }
    else
    {
        PDMDRV_SET_ERROR(pDrvIns, rc, N_("Unknown error during NAT networking setup: "));
        AssertMsgFailed(("Add error message for rc=%d (%Rrc)\n", rc, rc));
    }

    return rc;
}


/**
 * NAT network transport driver registration record.
 */
const PDMDRVREG g_DrvNAT =
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
    "NAT Network Transport Driver",
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
    drvNATPowerOn,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    drvNATResume,
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
