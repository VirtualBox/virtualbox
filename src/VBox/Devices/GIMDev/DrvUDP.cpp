/* $Id: DrvUDP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UDP socket stream driver.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_UDP
#include <VBox/log.h>
#include <VBox/vmm/pdmdrv.h>

#include "VBoxDD.h"

#include <iprt/socket.h>
#include <iprt/udp.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Converts a pointer to DRVUDP::IStream to a PDRVUDP. */
#define PDMISTREAM_2_DRVUDP(pInterface) ( (PDRVUDP)((uintptr_t)pInterface - RT_UOFFSETOF(DRVUDP, IStream)) )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * UDP driver instance data.
 *
 * @implements PDMISTREAM
 */
typedef struct DRVUDP
{
    /** The stream interface. */
    PDMISTREAM          IStream;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;
    /** The server port. */
    uint16_t            uServerPort;
    /** The server address. */
    char               *pszServerAddress;
    /** The resolved server address struct. */
    RTNETADDR           ServerAddr;
    /** The UDP socket. */
    RTSOCKET            hSocket;
} DRVUDP, *PDRVUDP;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/** @interface_method_impl{PDMISTREAM,pfnRead} */
static DECLCALLBACK(int) drvUDPRead(PPDMISTREAM pInterface, void *pvBuf, size_t *pcbRead)
{
    int rc = VINF_SUCCESS;
    PDRVUDP pThis = PDMISTREAM_2_DRVUDP(pInterface);
    LogFlowFunc(("pvBuf=%p *pcbRead=%#x (%s:%u)\n", pvBuf, *pcbRead, pThis->pszServerAddress, pThis->uServerPort));

    Assert(pvBuf);
    Assert(pcbRead);
    if (pThis->hSocket != NIL_RTSOCKET)
    {
        size_t cbReallyRead = 0;
        rc = RTSocketRead(pThis->hSocket, pvBuf, *pcbRead, &cbReallyRead);
        if (RT_SUCCESS(rc))
            *pcbRead = cbReallyRead;
    }
    else
        rc = VERR_NET_NOT_SOCKET;

    LogFlowFunc(("*pcbRead=%zu returns %Rrc\n", *pcbRead, rc));
    return rc;
}


/** @interface_method_impl{PDMISTREAM,pfnWrite} */
static DECLCALLBACK(int) drvUDPWrite(PPDMISTREAM pInterface, const void *pvBuf, size_t *pcbWrite)
{
    int rc = VINF_SUCCESS;
    PDRVUDP pThis = PDMISTREAM_2_DRVUDP(pInterface);
    LogFlowFunc(("pvBuf=%p *pcbWrite=%#x (%s:%u)\n", pvBuf, *pcbWrite, pThis->pszServerAddress, pThis->uServerPort));

    Assert(pvBuf);
    Assert(pcbWrite);
    if (pThis->hSocket != NIL_RTSOCKET)
    {
        size_t cbBuf = *pcbWrite;
        rc = RTSocketWriteTo(pThis->hSocket, pvBuf, cbBuf, NULL /*pDstAddr*/);
        if (RT_SUCCESS(rc))
            *pcbWrite = cbBuf;
    }
    else
        rc = VERR_NET_NOT_SOCKET;

    LogFlowFunc(("*pcbWrite=%zu returns %Rrc\n", *pcbWrite, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvUDPQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVUDP    pThis   = PDMINS_2_DATA(pDrvIns, PDRVUDP);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,   &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMISTREAM, &pThis->IStream);
    return NULL;
}


/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

/**
 * Destruct a UDP socket stream driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that
 * any non-VM resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvUDPDestruct(PPDMDRVINS pDrvIns)
{
    PDRVUDP pThis = PDMINS_2_DATA(pDrvIns, PDRVUDP);
    LogFlowFunc(("\n"));
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    if (pThis->hSocket != NIL_RTSOCKET)
    {
        /*
         * We shutdown the socket here to poke out any blocking socket reads. The caller
         * on the other thread/s need to ensure that they do -not- invoke drvUDPRead()
         * or drvUDPWrite() after this.
         */
        RTSocketRetain(pThis->hSocket);
        RTSocketShutdown(pThis->hSocket, true, true);
        RTSocketClose(pThis->hSocket);
        pThis->hSocket = NIL_RTSOCKET;
        LogRel(("DrvUDP#%u: Closed socket to %s:%u\n", pThis->pDrvIns->iInstance, pThis->pszServerAddress, pThis->uServerPort));
    }

    PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszServerAddress);
    pThis->pszServerAddress = NULL;
}


/**
 * Construct a UDP socket stream driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvUDPConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF1(fFlags);
    PDRVUDP         pThis = PDMINS_2_DATA(pDrvIns, PDRVUDP);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvUDPQueryInterface;
    /* IStream */
    pThis->IStream.pfnRead              = drvUDPRead;
    pThis->IStream.pfnWrite             = drvUDPWrite;

    /*
     * Validate and read the configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "ServerAddress|ServerPort", "");

    int rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "ServerAddress", &pThis->pszServerAddress);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Configuration error: querying \"ServerAddress\" resulted in %Rrc"), rc);
    rc = pHlp->pfnCFGMQueryU16(pCfg, "ServerPort", &pThis->uServerPort);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Configuration error: querying \"ServerPort\" resulted in %Rrc"), rc);

    /*
     * Create the socket and connect.
     */
    rc = RTUdpCreateClientSocket(pThis->pszServerAddress, pThis->uServerPort, NULL, &pThis->hSocket);
    if (RT_SUCCESS(rc))
        LogRel(("DrvUDP#%u: Connected socket to %s:%u\n",
                pThis->pDrvIns->iInstance, pThis->pszServerAddress, pThis->uServerPort));
    else
        LogRel(("DrvUDP#%u: Failed to create/connect socket to %s:%u rc=%Rrc\n",
                pThis->pDrvIns->iInstance, pThis->pszServerAddress, pThis->uServerPort, rc));
    return VINF_SUCCESS;
}


/**
 * UDP socket driver registration record.
 */
const PDMDRVREG g_DrvUDP =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "UDP",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "UDP socket stream driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STREAM,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVUDP),
    /* pfnConstruct */
    drvUDPConstruct,
    /* pfnDestruct */
    drvUDPDestruct,
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

