/* $Id: DrvIfsTrace.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox interface callback tracing driver.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MISC
#include <VBox/log.h>
#include <VBox/version.h>

#include <iprt/errcore.h>
#include <iprt/buildconfig.h>
#include <iprt/tracelog.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"
#include "DrvIfsTraceInternal.h"


/*
 *
 * IBase Implementation.
 *
 */


static DECLCALLBACK(void *) drvIfTraceIBase_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVIFTRACE pThis   = PDMINS_2_DATA(pDrvIns, PDRVIFTRACE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    if (pThis->pISerialConBelow)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMISERIALCONNECTOR, &pThis->ISerialConnector);
    if (pThis->pISerialPortAbove)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMISERIALPORT, &pThis->ISerialPort);

    if (pThis->pITpmConBelow)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMITPMCONNECTOR, &pThis->ITpmConnector);
    if (pThis->pITpmPortAbove)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMITPMPORT, &pThis->ITpmPort);

    return NULL;
}


/*
 *
 * PDMDRVREG Methods
 *
 */
static const RTTRACELOGEVTDESC g_IfTraceVmResumeEvtDesc =
{
    "IfTrace.VmResume",
    "VM was resumed",
    RTTRACELOGEVTSEVERITY_DEBUG,
    0,
    NULL
};


/**
 * @callback_method_impl{FNPDMDRVRESUME}
 */
static DECLCALLBACK(void) drvIfTrace_Resume(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVIFTRACE pThis = PDMINS_2_DATA(pDrvIns, PDRVIFTRACE);
    LogFlow(("%s: iInstance=%d\n", __FUNCTION__, pDrvIns->iInstance));

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_IfTraceVmResumeEvtDesc, 0, 0, 0);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));
}



static const RTTRACELOGEVTDESC g_IfTraceVmSuspendEvtDesc =
{
    "IfTrace.VmSuspend",
    "VM was suspended",
    RTTRACELOGEVTSEVERITY_DEBUG,
    0,
    NULL
};


/**
 * @callback_method_impl{FNPDMDRVSUSPEND}
 */
static DECLCALLBACK(void) drvIfTrace_Suspend(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVIFTRACE pThis = PDMINS_2_DATA(pDrvIns, PDRVIFTRACE);
    LogFlow(("%s: iInstance=%d\n", __FUNCTION__, pDrvIns->iInstance));

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_IfTraceVmSuspendEvtDesc, 0, 0, 0);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));
}


static const RTTRACELOGEVTDESC g_IfTraceVmResetEvtDesc =
{
    "IfTrace.VmReset",
    "VM was reset",
    RTTRACELOGEVTSEVERITY_DEBUG,
    0,
    NULL
};

/**
 * @callback_method_impl{FNPDMDRVRESET}
 */
static DECLCALLBACK(void) drvIfTrace_Reset(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVIFTRACE pThis = PDMINS_2_DATA(pDrvIns, PDRVIFTRACE);
    LogFlow(("%s: iInstance=%d\n", __FUNCTION__, pDrvIns->iInstance));

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_IfTraceVmResetEvtDesc, 0, 0, 0);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));
}



/**
 * Destroys a interface filter driver instance.
 *
 * @copydoc FNPDMDRVDESTRUCT
 */
static DECLCALLBACK(void) drvIfTrace_Destruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVIFTRACE pThis = PDMINS_2_DATA(pDrvIns, PDRVIFTRACE);
    LogFlow(("%s: iInstance=%d\n", __FUNCTION__, pDrvIns->iInstance));

    if (pThis->hTraceLog != NIL_RTTRACELOGWR)
    {
        RTTraceLogWrDestroy(pThis->hTraceLog);
        pThis->hTraceLog = NIL_RTTRACELOGWR;
    }
}


/**
 * Construct a interface filter driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvIfTrace_Construct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVIFTRACE     pThis = PDMINS_2_DATA(pDrvIns, PDRVIFTRACE);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;


    /*
     * Initialize the instance data.
     */
    pThis->pDrvIns   = pDrvIns;
    pThis->hTraceLog = NIL_RTTRACELOGWR;
    pDrvIns->IBase.pfnQueryInterface             = drvIfTraceIBase_QueryInterface;

    drvIfsTrace_SerialIfInit(pThis);
    drvIfsTrace_TpmIfInit(pThis);

    /*
     * Validate and read config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "TraceFilePath|TraceLocation", "");

    char *pszLocation = NULL;
    int rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "TraceFilePath", &pszLocation);
    if (RT_SUCCESS(rc))
    {
        /* Try to create a file based trace log. */
        rc = RTTraceLogWrCreateFile(&pThis->hTraceLog, RTBldCfgVersion(), pszLocation);
        PDMDrvHlpMMHeapFree(pDrvIns, pszLocation);

        AssertLogRelRCReturn(rc, rc);
    }
    else if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        /* Try to connect to an external server. */
        rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "TraceLocation", &pszLocation);
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                       N_("Configuration error: querying \"TraceLocation\" resulted in %Rrc"), rc);

        char *pszPort = strchr(pszLocation, ':');
        if (!pszPort)
            return PDMDrvHlpVMSetError(pDrvIns, VERR_NOT_FOUND, RT_SRC_POS,
                                       N_("IfTrace#%d: The location misses the port to connect to"),
                                       pDrvIns->iInstance);

        *pszPort = '\0'; /* Overwrite temporarily to avoid copying the hostname into a temporary buffer. */
        uint32_t uPort = 0;
        rc = RTStrToUInt32Ex(pszPort + 1, NULL, 10, &uPort);
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                       N_("IfTrace#%d: The port part of the location is not a numerical value"),
                                       pDrvIns->iInstance);

        rc = RTTraceLogWrCreateTcpClient(&pThis->hTraceLog, RTBldCfgVersion(), pszLocation, uPort);
        *pszPort = ':'; /* Restore delimiter before checking the status. */
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                       N_("IfTrace#%d: Failed to connect to socket %s"),
                                       pDrvIns->iInstance, pszLocation);

        PDMDrvHlpMMHeapFree(pDrvIns, pszLocation);
    }


    /*
     * Query interfaces from the driver/device above us.
     */
    pThis->pISerialPortAbove = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMISERIALPORT);
    pThis->pITpmPortAbove    = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMITPMPORT);

    /*
     * Attach driver below us.
     */
    PPDMIBASE pIBaseBelow;
    rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pIBaseBelow);
    AssertLogRelRCReturn(rc, rc);

    pThis->pISerialConBelow = PDMIBASE_QUERY_INTERFACE(pIBaseBelow, PDMISERIALCONNECTOR);
    pThis->pITpmConBelow    = PDMIBASE_QUERY_INTERFACE(pIBaseBelow, PDMITPMCONNECTOR);

    return VINF_SUCCESS;
}


/**
 * Storage filter driver registration record.
 */
const PDMDRVREG g_DrvIfTrace =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "IfTrace",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Interface callback tracing driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STATUS,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVIFTRACE),
    /* pfnConstruct */
    drvIfTrace_Construct,
    /* pfnDestruct */
    drvIfTrace_Destruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    drvIfTrace_Reset,
    /* pfnSuspend */
    drvIfTrace_Suspend,
    /* pfnResume */
    drvIfTrace_Resume,
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

