/* $Id: DrvIfsTrace-tpm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <iprt/tracelog.h>

#include "DrvIfsTraceInternal.h"


/*
 *
 * ITpmConnector Implementation.
 *
 */
static const RTTRACELOGEVTITEMDESC g_ITpmConnectorGetVersionEvtItems[] =
{
    {"enmTpmVersion",    "TPM version (TPMVERSION enum) reported by the lower driver", RTTRACELOGTYPE_UINT32,  0}
};

static const RTTRACELOGEVTDESC g_ITpmConnectorGetVersionEvtDesc =
{
    "ITpmConnector.GetVersion",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorGetVersionEvtItems),
    g_ITpmConnectorGetVersionEvtItems
};

/**
 * @interface_method_impl{PDMITPMCONNECTOR,pfnGetVersion}
 */
static DECLCALLBACK(TPMVERSION) drvIfTraceITpmConnector_GetVersion(PPDMITPMCONNECTOR pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ITpmConnector);
    TPMVERSION enmTpmVersion = pThis->pITpmConBelow->pfnGetVersion(pThis->pITpmConBelow);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorGetVersionEvtDesc, 0, 0, 0, (uint32_t)enmTpmVersion);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return enmTpmVersion;
}


static const RTTRACELOGEVTITEMDESC g_ITpmConnectorGetLocalityMaxEvtItems[] =
{
    {"u32LocMax",    "Maximum locality supported returned by the lower driver", RTTRACELOGTYPE_UINT32,  0}
};

static const RTTRACELOGEVTDESC g_ITpmConnectorGetLocalityMaxEvtDesc =
{
    "ITpmConnector.GetLocalityMax",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorGetLocalityMaxEvtItems),
    g_ITpmConnectorGetLocalityMaxEvtItems
};

/**
 * @interface_method_impl{PDMITPMCONNECTOR,pfnGetLocalityMax}
 */
static DECLCALLBACK(uint32_t) drvIfTraceITpmConnector_GetLocalityMax(PPDMITPMCONNECTOR pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ITpmConnector);
    uint32_t u32LocMax = pThis->pITpmConBelow->pfnGetLocalityMax(pThis->pITpmConBelow);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorGetLocalityMaxEvtDesc, 0, 0, 0, u32LocMax);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return u32LocMax;
}


static const RTTRACELOGEVTITEMDESC g_ITpmConnectorGetBufferSizeEvtItems[] =
{
    {"cbBuffer",    "Buffer size in bytes returned by the lower driver", RTTRACELOGTYPE_UINT32,  0}
};

static const RTTRACELOGEVTDESC g_ITpmConnectorGetBufferSizeEvtDesc =
{
    "ITpmConnector.GetBufferSize",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorGetBufferSizeEvtItems),
    g_ITpmConnectorGetBufferSizeEvtItems
};

/**
 * @interface_method_impl{PDMITPMCONNECTOR,pfnGetBufferSize}
 */
static DECLCALLBACK(uint32_t) drvIfTraceITpmConnector_GetBufferSize(PPDMITPMCONNECTOR pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ITpmConnector);
    uint32_t cbBuffer = pThis->pITpmConBelow->pfnGetBufferSize(pThis->pITpmConBelow);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorGetBufferSizeEvtDesc, 0, 0, 0, cbBuffer);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return cbBuffer;
}


static const RTTRACELOGEVTITEMDESC g_ITpmConnectorGetEstablishedFlagEvtItems[] =
{
    {"fEstablished",    "Established flag status returned by the lower driver", RTTRACELOGTYPE_BOOL,  0}
};

static const RTTRACELOGEVTDESC g_ITpmConnectorGetEstablishedFlagEvtDesc =
{
    "ITpmConnector.GetEstablishedFlag",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorGetEstablishedFlagEvtItems),
    g_ITpmConnectorGetEstablishedFlagEvtItems
};

/**
 * @interface_method_impl{PDMITPMCONNECTOR,pfnGetEstablishedFlag}
 */
static DECLCALLBACK(bool) drvIfTraceITpmConnector_GetEstablishedFlag(PPDMITPMCONNECTOR pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ITpmConnector);
    bool fEstablished = pThis->pITpmConBelow->pfnGetEstablishedFlag(pThis->pITpmConBelow);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorGetEstablishedFlagEvtDesc, 0, 0, 0, fEstablished);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return fEstablished;
}


static const RTTRACELOGEVTITEMDESC g_ITpmConnectorResetEstablishedFlagEvtItems[] =
{
    {"bLoc",        "Locality to reset the flag for",                  RTTRACELOGTYPE_UINT8,  0},
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,  0}
};

static const RTTRACELOGEVTDESC g_ITpmConnectorResetEstablishedFlagEvtDesc =
{
    "ITpmConnector.ResetEstablishedFlag",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorResetEstablishedFlagEvtItems),
    g_ITpmConnectorResetEstablishedFlagEvtItems
};

/**
 * @interface_method_impl{PDMITPMCONNECTOR,pfnResetEstablishedFlag}
 */
static DECLCALLBACK(int) drvIfTraceITpmConnector_ResetEstablishedFlag(PPDMITPMCONNECTOR pInterface, uint8_t bLoc)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ITpmConnector);
    int rc = pThis->pITpmConBelow->pfnResetEstablishedFlag(pThis->pITpmConBelow, bLoc);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorResetEstablishedFlagEvtDesc, 0, 0, 0, bLoc, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ITpmConnectorCmdExecReqEvtItems[] =
{
    {"bLoc",        "Locality in which the request is executed",       RTTRACELOGTYPE_UINT8,    0},
    {"pvCmd",       "The raw command data",                            RTTRACELOGTYPE_RAWDATA,  4096},
    {"cbCmd",       "Size of the command in bytes",                    RTTRACELOGTYPE_SIZE,     0}
};

static const RTTRACELOGEVTDESC g_ITpmConnectorCmdExecReqEvtDesc =
{
    "ITpmConnector.CmdExecReq",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorCmdExecReqEvtItems),
    g_ITpmConnectorCmdExecReqEvtItems
};


static const RTTRACELOGEVTITEMDESC g_ITpmConnectorCmdExecRespEvtItems[] =
{
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,    0},
    {"pvResp",      "TPM response data",                               RTTRACELOGTYPE_RAWDATA,  4096},
    {"cbResp",      "Size of the response in bytes",                   RTTRACELOGTYPE_SIZE,     0},
};

static const RTTRACELOGEVTDESC g_ITpmConnectorCmdExecRespEvtDesc =
{
    "ITpmConnector.CmdExecResp",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorCmdExecRespEvtItems),
    g_ITpmConnectorCmdExecRespEvtItems
};


/**
 * @interface_method_impl{PDMITPMCONNECTOR,pfnCmdExec}
 */
static DECLCALLBACK(int) drvIfTraceITpmConnector_CmdExec(PPDMITPMCONNECTOR pInterface, uint8_t bLoc, const void *pvCmd, size_t cbCmd, void *pvResp, size_t cbResp)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ITpmConnector);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorCmdExecReqEvtDesc, 0, 0, 0, bLoc, pvCmd, cbCmd);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    int rc = pThis->pITpmConBelow->pfnCmdExec(pThis->pITpmConBelow, bLoc, pvCmd, cbCmd, pvResp, cbResp);


    rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorCmdExecRespEvtDesc, 0, 0, 0, rc, pvResp, cbResp);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ITpmConnectorCmdCancelEvtItems[] =
{
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,  0}
};

static const RTTRACELOGEVTDESC g_ITpmConnectorCmdCancelEvtDesc =
{
    "ITpmConnector.CmdCancel",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ITpmConnectorCmdCancelEvtItems),
    g_ITpmConnectorCmdCancelEvtItems
};

/**
 * @callback_method_impl{PDMITPMCONNECTOR,pfnCmdCancel}
 */
static DECLCALLBACK(int) drvIfTraceITpmConnector_CmdCancel(PPDMITPMCONNECTOR pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ITpmConnector);
    int rc = pThis->pITpmConBelow->pfnCmdCancel(pThis->pITpmConBelow);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ITpmConnectorCmdCancelEvtDesc, 0, 0, 0, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


/**
 * Initializes TPM related interfaces.
 *
 * @param   pThis                   The interface callback trace driver instance.
 */
DECLHIDDEN(void) drvIfsTrace_TpmIfInit(PDRVIFTRACE pThis)
{
    pThis->ITpmConnector.pfnGetVersion           = drvIfTraceITpmConnector_GetVersion;
    pThis->ITpmConnector.pfnGetLocalityMax       = drvIfTraceITpmConnector_GetLocalityMax;
    pThis->ITpmConnector.pfnGetBufferSize        = drvIfTraceITpmConnector_GetBufferSize;
    pThis->ITpmConnector.pfnGetEstablishedFlag   = drvIfTraceITpmConnector_GetEstablishedFlag;
    pThis->ITpmConnector.pfnResetEstablishedFlag = drvIfTraceITpmConnector_ResetEstablishedFlag;
    pThis->ITpmConnector.pfnCmdExec              = drvIfTraceITpmConnector_CmdExec;
    pThis->ITpmConnector.pfnCmdCancel            = drvIfTraceITpmConnector_CmdCancel;
}

