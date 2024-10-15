/* $Id: DrvVDE.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VDE network transport driver.
 */

/*
 * Contributed by Renzo Davoli. VirtualSquare. University of Bologna, 2010
 *
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
#define LOG_GROUP LOG_GROUP_DRV_TUN
#include <VBox/log.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/VDEPlug.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * VDE driver instance data.
 *
 * @implements PDMINETWORKUP
 */
typedef struct DRVVDE
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The network interface. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** The configured VDE device name. */
    char                   *pszDeviceName;
    /** The write end of the control pipe. */
    RTPIPE                  hPipeWrite;
    /** The read end of the control pipe. */
    RTPIPE                  hPipeRead;
    /** Reader thread. */
    PPDMTHREAD              pThread;
    /** The connection to the VDE switch */
    VDECONN                *pVdeConn;

    /** @todo The transmit thread. */
    /** Transmit lock used by drvTAPNetworkUp_BeginXmit. */
    RTCRITSECT              XmitLock;

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
    STAMPROFILE             StatTransmit;
    /** Profiling packet receive runs. */
    STAMPROFILEADV          StatReceive;
#endif /* VBOX_WITH_STATISTICS */

#ifdef LOG_ENABLED
    /** The nano ts of the last transfer. */
    uint64_t                u64LastTransferTS;
    /** The nano ts of the last receive. */
    uint64_t                u64LastReceiveTS;
#endif
} DRVVDE, *PDRVVDE;


/** Converts a pointer to VDE::INetworkUp to a PRDVVDE. */
#define PDMINETWORKUP_2_DRVVDE(pInterface) ( (PDRVVDE)((uintptr_t)pInterface - RT_UOFFSETOF(DRVVDE, INetworkUp)) )


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/



/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
static DECLCALLBACK(int) drvVDENetworkUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVVDE pThis = PDMINETWORKUP_2_DRVVDE(pInterface);
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
static DECLCALLBACK(int) drvVDENetworkUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                  PCPDMNETWORKGSO pGso, PPPDMSCATTERGATHER ppSgBuf)
{
    RT_NOREF(pInterface);
#ifdef VBOX_STRICT
    PDRVVDE pThis = PDMINETWORKUP_2_DRVVDE(pInterface);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));
#endif

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
static DECLCALLBACK(int) drvVDENetworkUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    RT_NOREF(pInterface);
#ifdef VBOX_STRICT
    PDRVVDE pThis = PDMINETWORKUP_2_DRVVDE(pInterface);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));
#endif
    if (pSgBuf)
    {
        Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
        pSgBuf->fFlags = 0;
        RTMemFree(pSgBuf);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
static DECLCALLBACK(int) drvVDENetworkUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVVDE pThis = PDMINETWORKUP_2_DRVVDE(pInterface);
    STAM_COUNTER_INC(&pThis->StatPktSent);
    STAM_COUNTER_ADD(&pThis->StatPktSentBytes, pSgBuf->cbUsed);
    STAM_PROFILE_START(&pThis->StatTransmit, a);

    AssertPtr(pSgBuf);
    Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));

    int rc;
    if (!pSgBuf->pvUser)
    {
#ifdef LOG_ENABLED
        uint64_t u64Now = RTTimeProgramNanoTS();
        LogFlow(("drvVDESend: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                 pSgBuf->cbUsed, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
        pThis->u64LastTransferTS = u64Now;
#endif
        Log2(("drvVDESend: pSgBuf->aSegs[0].pvSeg=%p pSgBuf->cbUsed=%#x\n"
              "%.*Rhxd\n",
              pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, pSgBuf->cbUsed, pSgBuf->aSegs[0].pvSeg));

        ssize_t cbSent;
        cbSent = vde_send(pThis->pVdeConn, pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, 0);
        rc = cbSent < 0 ? RTErrConvertFromErrno(-cbSent) : VINF_SUCCESS;
    }
    else
    {
        uint8_t         abHdrScratch[256];
        uint8_t const  *pbFrame = (uint8_t const *)pSgBuf->aSegs[0].pvSeg;
        PCPDMNETWORKGSO pGso    = (PCPDMNETWORKGSO)pSgBuf->pvUser;
        uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, pSgBuf->cbUsed);  Assert(cSegs > 1);
        rc = 0;
        for (size_t iSeg = 0; iSeg < cSegs; iSeg++)
        {
            uint32_t cbSegFrame;
            void *pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)pbFrame, pSgBuf->cbUsed, abHdrScratch,
                                                       iSeg, cSegs, &cbSegFrame);
            ssize_t cbSent;
            cbSent = vde_send(pThis->pVdeConn, pvSegFrame, cbSegFrame, 0);
            rc = cbSent < 0 ? RTErrConvertFromErrno(-cbSent) : VINF_SUCCESS;
            if (RT_FAILURE(rc))
                break;
        }
    }

    pSgBuf->fFlags = 0;
    RTMemFree(pSgBuf);

    STAM_PROFILE_STOP(&pThis->StatTransmit, a);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        rc = rc == VERR_NO_MEMORY ? VERR_NET_NO_BUFFER_SPACE : VERR_NET_DOWN;
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
static DECLCALLBACK(void) drvVDENetworkUp_EndXmit(PPDMINETWORKUP pInterface)
{
    PDRVVDE pThis = PDMINETWORKUP_2_DRVVDE(pInterface);
    RTCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
static DECLCALLBACK(void) drvVDENetworkUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    RT_NOREF(pInterface, fPromiscuous);
    LogFlow(("drvVDESetPromiscuousMode: fPromiscuous=%d\n", fPromiscuous));
    /* nothing to do */
}


/**
 * Notification on link status changes.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   enmLinkState    The new link state.
 * @thread  EMT
 */
static DECLCALLBACK(void) drvVDENetworkUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    RT_NOREF(pInterface, enmLinkState);
    LogFlow(("drvNATNetworkUp_NotifyLinkChanged: enmLinkState=%d\n", enmLinkState));
    /** @todo take action on link down and up. Stop the polling and such like. */
}


/**
 * Asynchronous I/O thread for handling receive.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   Thread          Thread handle.
 * @param   pvUser          Pointer to a DRVVDE structure.
 */
static DECLCALLBACK(int) drvVDEAsyncIoThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVVDE pThis = PDMINS_2_DATA(pDrvIns, PDRVVDE);
    LogFlow(("drvVDEAsyncIoThread: pThis=%p\n", pThis));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

    /*
     * Polling loop.
     */
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * Wait for something to become available.
         */
        struct pollfd aFDs[2];
        aFDs[0].fd      = vde_datafd(pThis->pVdeConn);
        aFDs[0].events  = POLLIN | POLLPRI;
        aFDs[0].revents = 0;
        aFDs[1].fd      = RTPipeToNative(pThis->hPipeRead);
        aFDs[1].events  = POLLIN | POLLPRI | POLLERR | POLLHUP;
        aFDs[1].revents = 0;
        STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
        errno=0;
        int rc = poll(&aFDs[0], RT_ELEMENTS(aFDs), -1 /* infinite */);

        /* this might have changed in the meantime */
        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            break;

        STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
        if (    rc > 0
            &&  (aFDs[0].revents & (POLLIN | POLLPRI))
            &&  !aFDs[1].revents)
        {
            /*
             * Read the frame.
             */
            char achBuf[16384];
            ssize_t cbRead = 0;
            cbRead = vde_recv(pThis->pVdeConn, achBuf, sizeof(achBuf), 0);
            rc = cbRead < 0 ? RTErrConvertFromErrno(-cbRead) : VINF_SUCCESS;
            if (RT_SUCCESS(rc))
            {
                /*
                 * Wait for the device to have space for this frame.
                 * Most guests use frame-sized receive buffers, hence non-zero cbMax
                 * automatically means there is enough room for entire frame. Some
                 * guests (eg. Solaris) use large chains of small receive buffers
                 * (each 128 or so bytes large). We will still start receiving as soon
                 * as cbMax is non-zero because:
                 *  - it would be quite expensive for pfnCanReceive to accurately
                 *    determine free receive buffer space
                 *  - if we were waiting for enough free buffers, there is a risk
                 *    of deadlocking because the guest could be waiting for a receive
                 *    overflow error to allocate more receive buffers
                 */
                STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
                int rc1 = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
                STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

                /*
                 * A return code != VINF_SUCCESS means that we were woken up during a VM
                 * state transition. Drop the packet and wait for the next one.
                 */
                if (RT_FAILURE(rc1))
                    continue;

                /*
                 * Pass the data up.
                 */
#ifdef LOG_ENABLED
                uint64_t u64Now = RTTimeProgramNanoTS();
                LogFlow(("drvVDEAsyncIoThread: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                         cbRead, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
                pThis->u64LastReceiveTS = u64Now;
#endif
                Log2(("drvVDEAsyncIoThread: cbRead=%#x\n" "%.*Rhxd\n", cbRead, cbRead, achBuf));
                STAM_COUNTER_INC(&pThis->StatPktRecv);
                STAM_COUNTER_ADD(&pThis->StatPktRecvBytes, cbRead);
                rc1 = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, achBuf, cbRead);
                AssertRC(rc1);
            }
            else
            {
                LogFlow(("drvVDEAsyncIoThread: RTFileRead -> %Rrc\n", rc));
                if (rc == VERR_INVALID_HANDLE)
                    break;
                RTThreadYield();
            }
        }
        else if (   rc > 0
                 && aFDs[1].revents)
        {
            LogFlow(("drvVDEAsyncIoThread: Control message: enmState=%d revents=%#x\n", pThread->enmState, aFDs[1].revents));
            if (aFDs[1].revents & (POLLHUP | POLLERR | POLLNVAL))
                break;

            /* drain the pipe */
            char ch;
            size_t cbRead;
            RTPipeRead(pThis->hPipeRead, &ch, 1, &cbRead);
        }
        else
        {
            /*
             * poll() failed for some reason. Yield to avoid eating too much CPU.
             *
             * EINTR errors have been seen frequently. They should be harmless, even
             * if they are not supposed to occur in our setup.
             */
            if (errno == EINTR)
                Log(("rc=%d revents=%#x,%#x errno=%p %s\n", rc, aFDs[0].revents, aFDs[1].revents, errno, strerror(errno)));
            else
                AssertMsgFailed(("rc=%d revents=%#x,%#x errno=%p %s\n", rc, aFDs[0].revents, aFDs[1].revents, errno, strerror(errno)));
            RTThreadYield();
        }
    }


    LogFlow(("drvVDEAsyncIoThread: returns %Rrc\n", VINF_SUCCESS));
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
    return VINF_SUCCESS;
}


/**
 * Unblock the send thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The pcnet device instance.
 * @param   pThread     The send thread.
 */
static DECLCALLBACK(int) drvVDEAsyncIoWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVVDE pThis = PDMINS_2_DATA(pDrvIns, PDRVVDE);

    size_t cbIgnored;
    int rc = RTPipeWrite(pThis->hPipeWrite, "", 1, &cbIgnored);
    AssertRC(rc);

    return VINF_SUCCESS;
}


/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvVDEQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVVDE     pThis   = PDMINS_2_DATA(pDrvIns, PDRVVDE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUp);
    return NULL;
}

/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvVDEDestruct(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvVDEDestruct\n"));
    PDRVVDE pThis = PDMINS_2_DATA(pDrvIns, PDRVVDE);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    /*
     * Terminate the control pipe.
     */
    if (pThis->hPipeWrite != NIL_RTPIPE)
    {
        RTPipeClose(pThis->hPipeWrite);
        pThis->hPipeWrite = NIL_RTPIPE;
    }
    if (pThis->hPipeRead != NIL_RTPIPE)
    {
        RTPipeClose(pThis->hPipeRead);
        pThis->hPipeRead = NIL_RTPIPE;
    }

    PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszDeviceName);
    pThis->pszDeviceName = NULL;

    /*
     * Kill the xmit lock.
     */
    if (RTCritSectIsInitialized(&pThis->XmitLock))
        RTCritSectDelete(&pThis->XmitLock);

    if (pThis->pVdeConn)
    {
        vde_close(pThis->pVdeConn);
        pThis->pVdeConn = NULL;
    }

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
#endif /* VBOX_WITH_STATISTICS */
}


/**
 * Construct a VDE network transport driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvVDEConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVVDE         pThis = PDMINS_2_DATA(pDrvIns, PDRVVDE);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                              = pDrvIns;
    pThis->pszDeviceName                        = NULL;
    pThis->hPipeRead                            = NIL_RTPIPE;
    pThis->hPipeWrite                           = NIL_RTPIPE;

    /* IBase */
    pDrvIns->IBase.pfnQueryInterface            = drvVDEQueryInterface;
    /* INetwork */
    pThis->INetworkUp.pfnBeginXmit              = drvVDENetworkUp_BeginXmit;
    pThis->INetworkUp.pfnAllocBuf               = drvVDENetworkUp_AllocBuf;
    pThis->INetworkUp.pfnFreeBuf                = drvVDENetworkUp_FreeBuf;
    pThis->INetworkUp.pfnSendBuf                = drvVDENetworkUp_SendBuf;
    pThis->INetworkUp.pfnEndXmit                = drvVDENetworkUp_EndXmit;
    pThis->INetworkUp.pfnSetPromiscuousMode     = drvVDENetworkUp_SetPromiscuousMode;
    pThis->INetworkUp.pfnNotifyLinkChanged      = drvVDENetworkUp_NotifyLinkChanged;

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSent,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of sent packets.",          "/Drivers/VDE%d/Packets/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSentBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of sent bytes.",            "/Drivers/VDE%d/Bytes/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecv,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of received packets.",      "/Drivers/VDE%d/Packets/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecvBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of received bytes.",        "/Drivers/VDE%d/Bytes/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatTransmit,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet transmit runs.",  "/Drivers/VDE%d/Transmit", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReceive,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet receive runs.",   "/Drivers/VDE%d/Receive", pDrvIns->iInstance);
#endif /* VBOX_WITH_STATISTICS */

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "network", "");

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
    char szNetwork[RTPATH_MAX];
    rc = pHlp->pfnCFGMQueryString(pCfg, "network", szNetwork, sizeof(szNetwork));
    if (RT_FAILURE(rc))
        *szNetwork=0;

    if (RT_FAILURE(DrvVDELoadVDEPlug()))
        return PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("VDEplug library: not found"));
    pThis->pVdeConn = vde_open(szNetwork, "VirtualBOX", NULL);
    if (pThis->pVdeConn == NULL)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to connect to the VDE SWITCH"));

    /*
     * Create the transmit lock.
     */
    rc = RTCritSectInit(&pThis->XmitLock);
    AssertRCReturn(rc, rc);

    /*
     * Create the control pipe.
     */
    rc = RTPipeCreate(&pThis->hPipeRead, &pThis->hPipeWrite, 0 /*fFlags*/);
    AssertRCReturn(rc, rc);

    /*
     * Create the async I/O thread.
     */
    rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pThread, pThis, drvVDEAsyncIoThread, drvVDEAsyncIoWakeup, 128 * _1K, RTTHREADTYPE_IO, "VDE");
    AssertRCReturn(rc, rc);

    return rc;
}


/**
 * VDE network transport driver registration record.
 */
const PDMDRVREG g_DrvVDE =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "VDE",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "VDE Network Transport Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVVDE),
    /* pfnConstruct */
    drvVDEConstruct,
    /* pfnDestruct */
    drvVDEDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL, /** @todo Do power on, suspend and resume handlers! */
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

