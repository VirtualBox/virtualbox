/* $Id: DrvUDPTunnel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvUDPTunnel - UDP tunnel network transport driver
 *
 * Based on code contributed by Christophe Devriese
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_UDPTUNNEL
#include <VBox/log.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/udp.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/critsect.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * UDP tunnel driver instance data.
 *
 * @implements PDMINETWORKUP
 */
typedef struct DRVUDPTUNNEL
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The network interface. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** UDP tunnel source port. */
    uint16_t                uSrcPort;
    /** UDP tunnel destination port. */
    uint16_t                uDestPort;
    /** UDP tunnel destination IP address. */
    char                    *pszDestIP;
    /** UDP tunnel instance string. */
    char                    *pszInstance;

    /** UDP destination address. */
    RTNETADDR               DestAddress;
    /** Transmit lock used by drvUDPTunnelUp_BeginXmit. */
    RTCRITSECT              XmitLock;
    /** Server data structure for UDP communication. */
    PRTUDPSERVER            pServer;

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
} DRVUDPTUNNEL, *PDRVUDPTUNNEL;


/** Converts a pointer to UDPTUNNEL::INetworkUp to a PRDVUDPTUNNEL. */
#define PDMINETWORKUP_2_DRVUDPTUNNEL(pInterface) ( (PDRVUDPTUNNEL)((uintptr_t)pInterface - RT_UOFFSETOF(DRVUDPTUNNEL, INetworkUp)) )


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
static DECLCALLBACK(int) drvUDPTunnelUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVUDPTUNNEL pThis = PDMINETWORKUP_2_DRVUDPTUNNEL(pInterface);
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
static DECLCALLBACK(int) drvUDPTunnelUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                 PCPDMNETWORKGSO pGso, PPPDMSCATTERGATHER ppSgBuf)
{
    PDRVUDPTUNNEL pThis = PDMINETWORKUP_2_DRVUDPTUNNEL(pInterface);
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
static DECLCALLBACK(int) drvUDPTunnelUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    PDRVUDPTUNNEL pThis = PDMINETWORKUP_2_DRVUDPTUNNEL(pInterface);
    Assert(RTCritSectIsOwner(&pThis->XmitLock)); NOREF(pThis);
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
static DECLCALLBACK(int) drvUDPTunnelUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVUDPTUNNEL pThis = PDMINETWORKUP_2_DRVUDPTUNNEL(pInterface);
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
        LogFunc(("%-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                 pSgBuf->cbUsed, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
        pThis->u64LastTransferTS = u64Now;
#endif
        Log2(("pSgBuf->aSegs[0].pvSeg=%p pSgBuf->cbUsed=%#x\n%.*Rhxd\n",
              pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, pSgBuf->cbUsed, pSgBuf->aSegs[0].pvSeg));

        rc = RTUdpWrite(pThis->pServer, pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, &pThis->DestAddress);
    }
    else
    {
        uint8_t         abHdrScratch[256];
        uint8_t const  *pbFrame = (uint8_t const *)pSgBuf->aSegs[0].pvSeg;
        PCPDMNETWORKGSO pGso    = (PCPDMNETWORKGSO)pSgBuf->pvUser;
        uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, pSgBuf->cbUsed);  Assert(cSegs > 1);
        rc = VINF_SUCCESS;
        for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
        {
            uint32_t cbSegFrame;
            void *pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)pbFrame, pSgBuf->cbUsed, abHdrScratch,
                                                       iSeg, cSegs, &cbSegFrame);
            rc = RTUdpWrite(pThis->pServer, pvSegFrame, cbSegFrame, &pThis->DestAddress);
        }
    }

    pSgBuf->fFlags = 0;
    RTMemFree(pSgBuf);

    STAM_PROFILE_STOP(&pThis->StatTransmit, a);
    AssertRC(rc);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_NO_MEMORY)
            rc = VERR_NET_NO_BUFFER_SPACE;
        else
            rc = VERR_NET_DOWN;
    }
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
static DECLCALLBACK(void) drvUDPTunnelUp_EndXmit(PPDMINETWORKUP pInterface)
{
    PDRVUDPTUNNEL pThis = PDMINETWORKUP_2_DRVUDPTUNNEL(pInterface);
    RTCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
static DECLCALLBACK(void) drvUDPTunnelUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
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
static DECLCALLBACK(void) drvUDPTunnelUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    LogFlowFunc(("enmLinkState=%d\n", enmLinkState));
    PDRVUDPTUNNEL pThis = PDMINETWORKUP_2_DRVUDPTUNNEL(pInterface);

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


static DECLCALLBACK(int) drvUDPTunnelReceive(RTSOCKET Sock, void *pvUser)
{
    PDRVUDPTUNNEL pThis = PDMINS_2_DATA((PPDMDRVINS)pvUser, PDRVUDPTUNNEL);
    LogFlowFunc(("pThis=%p\n", pThis));

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

    /*
     * Read the frame.
     */
    char achBuf[16384];
    size_t cbRead = 0;
    int rc = RTUdpRead(Sock, achBuf, sizeof(achBuf), &cbRead, NULL);
    if (RT_SUCCESS(rc))
    {
        if (!pThis->fLinkDown)
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
            rc = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
            STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

            /*
             * A return code != VINF_SUCCESS means that we were woken up during a VM
             * state transition. Drop the packet and wait for the next one.
             */
            if (RT_FAILURE(rc))
            {
                STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
                return VINF_SUCCESS;
            }

            /*
             * Pass the data up.
             */
#ifdef LOG_ENABLED
            uint64_t u64Now = RTTimeProgramNanoTS();
            LogFunc(("%-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                     cbRead, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
            pThis->u64LastReceiveTS = u64Now;
#endif
            Log2(("cbRead=%#x\n" "%.*Rhxd\n", cbRead, cbRead, achBuf));
            STAM_COUNTER_INC(&pThis->StatPktRecv);
            STAM_COUNTER_ADD(&pThis->StatPktRecvBytes, cbRead);
            rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, achBuf, cbRead);
            AssertRC(rc);
        }
    }
    else
    {
        STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
        LogFunc(("RTUdpRead -> %Rrc\n", rc));
        if (rc == VERR_INVALID_HANDLE)
            return VERR_UDP_SERVER_STOP;
    }

    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
    return VINF_SUCCESS;
}


/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvUDPTunnelQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVUDPTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVUDPTUNNEL);

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
static DECLCALLBACK(void) drvUDPTunnelDestruct(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PDRVUDPTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVUDPTUNNEL);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    ASMAtomicXchgSize(&pThis->fLinkDown, true);

    if (pThis->pszInstance)
    {
        RTStrFree(pThis->pszInstance);
        pThis->pszInstance = NULL;
    }

    if (pThis->pszDestIP)
    {
        PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszDestIP);
        pThis->pszDestIP = NULL;
    }

    if (pThis->pServer)
    {
        RTUdpServerDestroy(pThis->pServer);
        pThis->pServer = NULL;
    }

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
#endif /* VBOX_WITH_STATISTICS */
}


/**
 * Construct a UDP tunnel network transport driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvUDPTunnelConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVUDPTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVUDPTUNNEL);
    PCPDMDRVHLPR3 pHlp  = pDrvIns->pHlpR3;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    pThis->pszDestIP                    = NULL;
    pThis->pszInstance                  = NULL;

    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvUDPTunnelQueryInterface;
    /* INetwork */
    pThis->INetworkUp.pfnBeginXmit              = drvUDPTunnelUp_BeginXmit;
    pThis->INetworkUp.pfnAllocBuf               = drvUDPTunnelUp_AllocBuf;
    pThis->INetworkUp.pfnFreeBuf                = drvUDPTunnelUp_FreeBuf;
    pThis->INetworkUp.pfnSendBuf                = drvUDPTunnelUp_SendBuf;
    pThis->INetworkUp.pfnEndXmit                = drvUDPTunnelUp_EndXmit;
    pThis->INetworkUp.pfnSetPromiscuousMode     = drvUDPTunnelUp_SetPromiscuousMode;
    pThis->INetworkUp.pfnNotifyLinkChanged      = drvUDPTunnelUp_NotifyLinkChanged;

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSent,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of sent packets.",          "/Drivers/UDPTunnel%d/Packets/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSentBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of sent bytes.",            "/Drivers/UDPTunnel%d/Bytes/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecv,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of received packets.",      "/Drivers/UDPTunnel%d/Packets/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecvBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of received bytes.",        "/Drivers/UDPTunnel%d/Bytes/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatTransmit,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet transmit runs.",  "/Drivers/UDPTunnel%d/Transmit", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReceive,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet receive runs.",   "/Drivers/UDPTunnel%d/Receive", pDrvIns->iInstance);
#endif /* VBOX_WITH_STATISTICS */

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,  "sport"
                                            "|dest"
                                            "|dport",
                                            "");

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
    char szVal[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "sport", szVal, sizeof(szVal), "4444");
    if (RT_FAILURE(rc))
        rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                              N_("DrvUDPTunnel: Configuration error: Querying \"sport\" as string failed"));
    rc = RTStrToUInt16Full(szVal, 0, &pThis->uSrcPort);
    if (RT_FAILURE(rc))
        rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                              N_("DrvUDPTunnel: Configuration error: Converting \"sport\" to integer failed"));
    if (!pThis->uSrcPort)
        pThis->uSrcPort = 4444;

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "dport", szVal, sizeof(szVal), "4445");
    if (RT_FAILURE(rc))
        rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                              N_("DrvUDPTunnel: Configuration error: Querying \"dport\" as string failed"));
    rc = RTStrToUInt16Full(szVal, 0, &pThis->uDestPort);
    if (RT_FAILURE(rc))
        rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                              N_("DrvUDPTunnel: Configuration error: Converting \"dport\" to integer failed"));
    if (!pThis->uDestPort)
        pThis->uDestPort = 4445;

    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "dest", &pThis->pszDestIP, "127.0.0.1");
    if (RT_FAILURE(rc))
        rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                              N_("DrvUDPTunnel: Configuration error: Querying \"dest\" as string failed"));

    LogRel(("UDPTunnel#%d: sport=%d;dest=%s;dport=%d\n", pDrvIns->iInstance, pThis->uSrcPort, pThis->pszDestIP, pThis->uDestPort));

    /*
     * Set up destination address for UDP.
     */
    rc = RTSocketParseInetAddress(pThis->pszDestIP, pThis->uDestPort, &pThis->DestAddress);
    AssertRCReturn(rc, rc);

    /*
     * Create unique thread name for the UDP receiver.
     */
    rc = RTStrAPrintf(&pThis->pszInstance, "UDPTunnel%d", pDrvIns->iInstance);
    AssertRC(rc);

    /*
     * Start the UDP receiving thread.
     */
    rc = RTUdpServerCreate("", pThis->uSrcPort, RTTHREADTYPE_IO, pThis->pszInstance,
                           drvUDPTunnelReceive, pDrvIns, &pThis->pServer);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("UDPTunnel: Failed to start the UDP tunnel server"));

    /*
     * Create the transmit lock.
     */
    rc = RTCritSectInit(&pThis->XmitLock);
    AssertRCReturn(rc, rc);

    return rc;
}


/**
 * Suspend notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvUDPTunnelSuspend(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PDRVUDPTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVUDPTUNNEL);

    if (pThis->pServer)
    {
        RTUdpServerDestroy(pThis->pServer);
        pThis->pServer = NULL;
    }
}


/**
 * Resume notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvUDPTunnelResume(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PDRVUDPTUNNEL pThis = PDMINS_2_DATA(pDrvIns, PDRVUDPTUNNEL);

    int rc = RTUdpServerCreate("", pThis->uSrcPort, RTTHREADTYPE_IO, pThis->pszInstance,
                               drvUDPTunnelReceive, pDrvIns, &pThis->pServer);
    if (RT_FAILURE(rc))
        PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                            N_("UDPTunnel: Failed to start the UDP tunnel server"));

}


/**
 * UDP tunnel network transport driver registration record.
 */
const PDMDRVREG g_DrvUDPTunnel =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "UDPTunnel",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "UDP Tunnel Network Transport Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVUDPTUNNEL),
    /* pfnConstruct */
    drvUDPTunnelConstruct,
    /* pfnDestruct */
    drvUDPTunnelDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    drvUDPTunnelSuspend,
    /* pfnResume */
    drvUDPTunnelResume,
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

