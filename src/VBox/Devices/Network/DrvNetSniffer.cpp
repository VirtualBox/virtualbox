/* $Id: DrvNetSniffer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvNetSniffer - Network sniffer filter driver.
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
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/file.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <VBox/param.h>

#include "Pcap.h"
#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Block driver instance data.
 *
 * @implements  PDMINETWORKUP
 * @implements  PDMINETWORKDOWN
 * @implements  PDMINETWORKCONFIG
 */
typedef struct DRVNETSNIFFER
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The network interface. */
    PDMINETWORKDOWN         INetworkDown;
    /** The network config interface.
     * @todo this is a main interface and shouldn't be here...  */
    PDMINETWORKCONFIG       INetworkConfig;
    /** The port we're attached to. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** The config port interface we're attached to. */
    PPDMINETWORKCONFIG      pIAboveConfig;
    /** The connector that's attached to us. */
    PPDMINETWORKUP          pIBelowNet;
    /** The filename. */
    char                    szFilename[RTPATH_MAX];
    /** The filehandle. */
    RTFILE                  hFile;
    /** The lock serializing the file access. */
    RTCRITSECT              Lock;
    /** The NanoTS delta we pass to the pcap writers. */
    uint64_t                StartNanoTS;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** For when we're the leaf driver. */
    RTCRITSECT              XmitLock;

} DRVNETSNIFFER, *PDRVNETSNIFFER;



/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
static DECLCALLBACK(int) drvNetSnifferUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkUp);
    if (RT_UNLIKELY(!pThis->pIBelowNet))
    {
        int rc = RTCritSectTryEnter(&pThis->XmitLock);
        if (RT_UNLIKELY(rc == VERR_SEM_BUSY))
            rc = VERR_TRY_AGAIN;
        return rc;
    }
    return pThis->pIBelowNet->pfnBeginXmit(pThis->pIBelowNet, fOnWorkerThread);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnAllocBuf}
 */
static DECLCALLBACK(int) drvNetSnifferUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                  PCPDMNETWORKGSO pGso, PPPDMSCATTERGATHER ppSgBuf)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkUp);
    if (RT_UNLIKELY(!pThis->pIBelowNet))
        return VERR_NET_DOWN;
    return pThis->pIBelowNet->pfnAllocBuf(pThis->pIBelowNet, cbMin, pGso, ppSgBuf);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnFreeBuf}
 */
static DECLCALLBACK(int) drvNetSnifferUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkUp);
    if (RT_UNLIKELY(!pThis->pIBelowNet))
        return VERR_NET_DOWN;
    return pThis->pIBelowNet->pfnFreeBuf(pThis->pIBelowNet, pSgBuf);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
static DECLCALLBACK(int) drvNetSnifferUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkUp);
    if (RT_UNLIKELY(!pThis->pIBelowNet))
        return VERR_NET_DOWN;

    /* output to sniffer */
    RTCritSectEnter(&pThis->Lock);
    if (!pSgBuf->pvUser)
        PcapFileFrame(pThis->hFile, pThis->StartNanoTS,
                      pSgBuf->aSegs[0].pvSeg,
                      pSgBuf->cbUsed,
                      RT_MIN(pSgBuf->cbUsed, pSgBuf->aSegs[0].cbSeg));
    else
        PcapFileGsoFrame(pThis->hFile, pThis->StartNanoTS, (PCPDMNETWORKGSO)pSgBuf->pvUser,
                         pSgBuf->aSegs[0].pvSeg,
                         pSgBuf->cbUsed,
                         RT_MIN(pSgBuf->cbUsed, pSgBuf->aSegs[0].cbSeg));
    RTCritSectLeave(&pThis->Lock);

    return pThis->pIBelowNet->pfnSendBuf(pThis->pIBelowNet, pSgBuf, fOnWorkerThread);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
static DECLCALLBACK(void) drvNetSnifferUp_EndXmit(PPDMINETWORKUP pInterface)
{
    LogFlow(("drvNetSnifferUp_EndXmit:\n"));
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkUp);
    if (RT_LIKELY(pThis->pIBelowNet))
        pThis->pIBelowNet->pfnEndXmit(pThis->pIBelowNet);
    else
        RTCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
static DECLCALLBACK(void) drvNetSnifferUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    LogFlow(("drvNetSnifferUp_SetPromiscuousMode: fPromiscuous=%d\n", fPromiscuous));
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkUp);
    if (pThis->pIBelowNet)
        pThis->pIBelowNet->pfnSetPromiscuousMode(pThis->pIBelowNet, fPromiscuous);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnNotifyLinkChanged}
 */
static DECLCALLBACK(void) drvNetSnifferUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    LogFlow(("drvNetSnifferUp_NotifyLinkChanged: enmLinkState=%d\n", enmLinkState));
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkUp);
    if (pThis->pIBelowNet)
        pThis->pIBelowNet->pfnNotifyLinkChanged(pThis->pIBelowNet, enmLinkState);
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnWaitReceiveAvail}
 */
static DECLCALLBACK(int) drvNetSnifferDown_WaitReceiveAvail(PPDMINETWORKDOWN pInterface, RTMSINTERVAL cMillies)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkDown);
    return pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, cMillies);
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnReceive}
 */
static DECLCALLBACK(int) drvNetSnifferDown_Receive(PPDMINETWORKDOWN pInterface, const void *pvBuf, size_t cb)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkDown);

    /* output to sniffer */
    RTCritSectEnter(&pThis->Lock);
    PcapFileFrame(pThis->hFile, pThis->StartNanoTS, pvBuf, cb, cb);
    RTCritSectLeave(&pThis->Lock);

    /* pass up */
    int rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, pvBuf, cb);
#if 0
    RTCritSectEnter(&pThis->Lock);
    u64TS = RTTimeProgramNanoTS();
    Hdr.ts_sec = (uint32_t)(u64TS / 1000000000);
    Hdr.ts_usec = (uint32_t)((u64TS / 1000) % 1000000);
    Hdr.incl_len = 0;
    RTFileWrite(pThis->hFile, &Hdr, sizeof(Hdr), NULL);
    RTCritSectLeave(&pThis->Lock);
#endif
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnXmitPending}
 */
static DECLCALLBACK(void) drvNetSnifferDown_XmitPending(PPDMINETWORKDOWN pInterface)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkDown);
    pThis->pIAboveNet->pfnXmitPending(pThis->pIAboveNet);
}


/**
 * Gets the current Media Access Control (MAC) address.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   pMac            Where to store the MAC address.
 * @thread  EMT
 */
static DECLCALLBACK(int) drvNetSnifferDownCfg_GetMac(PPDMINETWORKCONFIG pInterface, PRTMAC pMac)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkConfig);
    return pThis->pIAboveConfig->pfnGetMac(pThis->pIAboveConfig, pMac);
}

/**
 * Gets the new link state.
 *
 * @returns The current link state.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @thread  EMT
 */
static DECLCALLBACK(PDMNETWORKLINKSTATE) drvNetSnifferDownCfg_GetLinkState(PPDMINETWORKCONFIG pInterface)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkConfig);
    return pThis->pIAboveConfig->pfnGetLinkState(pThis->pIAboveConfig);
}

/**
 * Sets the new link state.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   enmState        The new link state
 * @thread  EMT
 */
static DECLCALLBACK(int) drvNetSnifferDownCfg_SetLinkState(PPDMINETWORKCONFIG pInterface, PDMNETWORKLINKSTATE enmState)
{
    PDRVNETSNIFFER pThis = RT_FROM_MEMBER(pInterface, DRVNETSNIFFER, INetworkConfig);
    return pThis->pIAboveConfig->pfnSetLinkState(pThis->pIAboveConfig, enmState);
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvNetSnifferQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVNETSNIFFER  pThis   = PDMINS_2_DATA(pDrvIns, PDRVNETSNIFFER);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUp);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKDOWN, &pThis->INetworkDown);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKCONFIG, &pThis->INetworkConfig);
    return NULL;
}


/**
 * @interface_method_impl{PDMDRVREG,pfnDetach}
 */
static DECLCALLBACK(void) drvNetSnifferDetach(PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDRVNETSNIFFER pThis = PDMINS_2_DATA(pDrvIns, PDRVNETSNIFFER);

    LogFlow(("drvNetSnifferDetach: pDrvIns: %p, fFlags: %u\n", pDrvIns, fFlags));
    RTCritSectEnter(&pThis->XmitLock);
    pThis->pIBelowNet = NULL;
    RTCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnAttach}
 */
static DECLCALLBACK(int) drvNetSnifferAttach(PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDRVNETSNIFFER pThis = PDMINS_2_DATA(pDrvIns, PDRVNETSNIFFER);
    LogFlow(("drvNetSnifferAttach/#%#x: fFlags=%#x\n", pDrvIns->iInstance, fFlags));
    RTCritSectEnter(&pThis->XmitLock);

    /*
     * Query the network connector interface.
     */
    PPDMIBASE   pBaseDown;
    int rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pBaseDown);
    if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
        || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        pThis->pIBelowNet = NULL;
        rc = VINF_SUCCESS;
    }
    else if (RT_SUCCESS(rc))
    {
        pThis->pIBelowNet = PDMIBASE_QUERY_INTERFACE(pBaseDown, PDMINETWORKUP);
        if (pThis->pIBelowNet)
            rc = VINF_SUCCESS;
        else
        {
            AssertMsgFailed(("Configuration error: the driver below didn't export the network connector interface!\n"));
            rc = VERR_PDM_MISSING_INTERFACE_BELOW;
        }
    }
    else
        AssertMsgFailed(("Failed to attach to driver below! rc=%Rrc\n", rc));

    RTCritSectLeave(&pThis->XmitLock);
    return rc;
}


/**
 * @interface_method_impl{PDMDRVREG,pfnDestruct}
 */
static DECLCALLBACK(void) drvNetSnifferDestruct(PPDMDRVINS pDrvIns)
{
    PDRVNETSNIFFER pThis = PDMINS_2_DATA(pDrvIns, PDRVNETSNIFFER);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    if (RTCritSectIsInitialized(&pThis->Lock))
        RTCritSectDelete(&pThis->Lock);

    if (RTCritSectIsInitialized(&pThis->XmitLock))
        RTCritSectDelete(&pThis->XmitLock);

    if (pThis->hFile != NIL_RTFILE)
    {
        RTFileClose(pThis->hFile);
        pThis->hFile = NIL_RTFILE;
    }
}


/**
 * @interface_method_impl{Construct a NAT network transport driver instance,
 *                       PDMDRVREG,pfnDestruct}
 */
static DECLCALLBACK(int) drvNetSnifferConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVNETSNIFFER  pThis = PDMINS_2_DATA(pDrvIns, PDRVNETSNIFFER);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    LogFlow(("drvNetSnifferConstruct:\n"));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                                  = pDrvIns;
    pThis->hFile                                    = NIL_RTFILE;
    /* The pcap file *must* start at time offset 0,0. */
    pThis->StartNanoTS                              = RTTimeNanoTS() - RTTimeProgramNanoTS();
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface                = drvNetSnifferQueryInterface;
    /* INetworkUp */
    pThis->INetworkUp.pfnBeginXmit                  = drvNetSnifferUp_BeginXmit;
    pThis->INetworkUp.pfnAllocBuf                   = drvNetSnifferUp_AllocBuf;
    pThis->INetworkUp.pfnFreeBuf                    = drvNetSnifferUp_FreeBuf;
    pThis->INetworkUp.pfnSendBuf                    = drvNetSnifferUp_SendBuf;
    pThis->INetworkUp.pfnEndXmit                    = drvNetSnifferUp_EndXmit;
    pThis->INetworkUp.pfnSetPromiscuousMode         = drvNetSnifferUp_SetPromiscuousMode;
    pThis->INetworkUp.pfnNotifyLinkChanged          = drvNetSnifferUp_NotifyLinkChanged;
    /* INetworkDown */
    pThis->INetworkDown.pfnWaitReceiveAvail         = drvNetSnifferDown_WaitReceiveAvail;
    pThis->INetworkDown.pfnReceive                  = drvNetSnifferDown_Receive;
    pThis->INetworkDown.pfnXmitPending              = drvNetSnifferDown_XmitPending;
    /* INetworkConfig */
    pThis->INetworkConfig.pfnGetMac                 = drvNetSnifferDownCfg_GetMac;
    pThis->INetworkConfig.pfnGetLinkState           = drvNetSnifferDownCfg_GetLinkState;
    pThis->INetworkConfig.pfnSetLinkState           = drvNetSnifferDownCfg_SetLinkState;

    /*
     * Create the locks.
     */
    int rc = RTCritSectInit(&pThis->Lock);
    AssertRCReturn(rc, rc);
    rc = RTCritSectInit(&pThis->XmitLock);
    AssertRCReturn(rc, rc);

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "File", "");

    if (pHlp->pfnCFGMGetFirstChild(pCfg))
        LogRel(("NetSniffer: Found child config entries -- are you trying to redirect ports?\n"));

    /*
     * Get the filename.
     */
    rc = pHlp->pfnCFGMQueryString(pCfg, "File", pThis->szFilename, sizeof(pThis->szFilename));
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        if (pDrvIns->iInstance > 0)
            RTStrPrintf(pThis->szFilename, sizeof(pThis->szFilename), "./VBox-%x-%u.pcap", RTProcSelf(), pDrvIns->iInstance);
        else
            RTStrPrintf(pThis->szFilename, sizeof(pThis->szFilename), "./VBox-%x.pcap", RTProcSelf());
    }

    else if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Failed to query \"File\", rc=%Rrc.\n", rc));
        return rc;
    }

    /*
     * Query the network port interface.
     */
    pThis->pIAboveNet = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKDOWN);
    if (!pThis->pIAboveNet)
    {
        AssertMsgFailed(("Configuration error: the above device/driver didn't export the network port interface!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    /*
     * Query the network config interface.
     */
    pThis->pIAboveConfig = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKCONFIG);
    if (!pThis->pIAboveConfig)
    {
        AssertMsgFailed(("Configuration error: the above device/driver didn't export the network config interface!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    /*
     * Query the network connector interface.
     */
    PPDMIBASE   pBaseDown;
    rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pBaseDown);
    if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
        || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
        pThis->pIBelowNet = NULL;
    else if (RT_SUCCESS(rc))
    {
        pThis->pIBelowNet = PDMIBASE_QUERY_INTERFACE(pBaseDown, PDMINETWORKUP);
        if (!pThis->pIBelowNet)
        {
            AssertMsgFailed(("Configuration error: the driver below didn't export the network connector interface!\n"));
            return VERR_PDM_MISSING_INTERFACE_BELOW;
        }
    }
    else
    {
        AssertMsgFailed(("Failed to attach to driver below! rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * Open output file / pipe.
     */
    rc = RTFileOpen(&pThis->hFile, pThis->szFilename,
                    RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Netsniffer cannot open '%s' for writing. The directory must exist and it must be writable for the current user"), pThis->szFilename);

    char *pszPathReal = RTPathRealDup(pThis->szFilename);
    if (pszPathReal)
    {
        LogRel(("NetSniffer: Sniffing to '%s'\n", pszPathReal));
        RTStrFree(pszPathReal);
    }
    else
        LogRel(("NetSniffer: Sniffing to '%s'\n", pThis->szFilename));

    /*
     * Write pcap header.
     * Some time has gone by since capturing pThis->StartNanoTS so get the
     * current time again.
     */
    PcapFileHdr(pThis->hFile, RTTimeNanoTS());

    return VINF_SUCCESS;
}



/**
 * Network sniffer filter driver registration record.
 */
const PDMDRVREG g_DrvNetSniffer =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "NetSniffer",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Network Sniffer Filter Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    UINT32_MAX,
    /* cbInstance */
    sizeof(DRVNETSNIFFER),
    /* pfnConstruct */
    drvNetSnifferConstruct,
    /* pfnDestruct */
    drvNetSnifferDestruct,
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
    drvNetSnifferAttach,
    /* pfnDetach */
    drvNetSnifferDetach,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

