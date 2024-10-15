/* $Id: DrvIfsTrace-serial.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
 * ISerialPort Implementation.
 *
 */
static const RTTRACELOGEVTITEMDESC g_ISerialPortDataAvailRdrNotifyEvtItems[] =
{
    {"cbAvail", "Number of bytes available",                       RTTRACELOGTYPE_SIZE,  0},
    {"rc",      "Status code returned by the upper device/driver", RTTRACELOGTYPE_INT32, 0}
};

static const RTTRACELOGEVTDESC g_ISerialPortDataAvailRdrNotifyEvtDesc =
{
    "ISerialPort.DataAvailRdrNotify",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialPortDataAvailRdrNotifyEvtItems),
    g_ISerialPortDataAvailRdrNotifyEvtItems
};

/**
 * @interface_method_impl{PDMISERIALPORT,pfnDataAvailRdrNotify}
 */
static DECLCALLBACK(int) drvIfTraceISerialPort_DataAvailRdrNotify(PPDMISERIALPORT pInterface, size_t cbAvail)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialPort);
    int rc = pThis->pISerialPortAbove->pfnDataAvailRdrNotify(pThis->pISerialPortAbove, cbAvail);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialPortDataAvailRdrNotifyEvtDesc, 0, 0, 0, cbAvail, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialPortDataSentNotifyEvtItems[] =
{
    {"rc",      "Status code returned by the upper device/driver", RTTRACELOGTYPE_INT32, 0}
};

static const RTTRACELOGEVTDESC g_ISerialPortDataSentNotifyEvtDesc =
{
    "ISerialPort.DataSentNotify",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialPortDataSentNotifyEvtItems),
    g_ISerialPortDataSentNotifyEvtItems
};

/**
 * @interface_method_impl{PDMISERIALPORT,pfnDataSentNotify}
 */
static DECLCALLBACK(int) drvIfTraceISerialPort_DataSentNotify(PPDMISERIALPORT pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialPort);
    int rc = pThis->pISerialPortAbove->pfnDataSentNotify(pThis->pISerialPortAbove);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialPortDataSentNotifyEvtDesc, 0, 0, 0, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialPortReadWrEvtItems[] =
{
    {"cbRead",  "Number of bytes to read max",                     RTTRACELOGTYPE_SIZE,  0},
    {"pcbRead", "Number of bytes actually read",                   RTTRACELOGTYPE_SIZE,  0},
    {"rc",      "Status code returned by the upper device/driver", RTTRACELOGTYPE_INT32, 0}
};

static const RTTRACELOGEVTDESC g_ISerialPortReadWrEvtDesc =
{
    "ISerialPort.ReadWr",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialPortReadWrEvtItems),
    g_ISerialPortReadWrEvtItems
};

/**
 * @interface_method_impl{PDMISERIALPORT,pfnReadWr}
 */
static DECLCALLBACK(int) drvIfTraceISerialPort_ReadWr(PPDMISERIALPORT pInterface, void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialPort);
    int rc = pThis->pISerialPortAbove->pfnReadWr(pThis->pISerialPortAbove, pvBuf, cbRead, pcbRead);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialPortReadWrEvtDesc, 0, 0, 0, cbRead, *pcbRead, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialPortNotifyStsLinesChangedEvtItems[] =
{
    {"fNewStsLines",  "Status line mask",                                RTTRACELOGTYPE_UINT32,  0},
    {"rc",            "Status code returned by the upper device/driver", RTTRACELOGTYPE_INT32,   0}
};

static const RTTRACELOGEVTDESC g_ISerialPortNotifyStsLinesChangedEvtDesc =
{
    "ISerialPort.NotifyStsLinesChanged",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialPortNotifyStsLinesChangedEvtItems),
    g_ISerialPortNotifyStsLinesChangedEvtItems
};

/**
 * @interface_method_impl{PDMISERIALPORT,pfnNotifyStsLinesChanged}
 */
static DECLCALLBACK(int) drvIfTraceISerialPort_NotifyStsLinesChanged(PPDMISERIALPORT pInterface, uint32_t fNewStatusLines)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialPort);
    int rc = pThis->pISerialPortAbove->pfnNotifyStsLinesChanged(pThis->pISerialPortAbove, fNewStatusLines);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialPortNotifyStsLinesChangedEvtDesc, 0, 0, 0, fNewStatusLines, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialPortNotifyBrkEvtItems[] =
{
    {"rc",      "Status code returned by the upper device/driver", RTTRACELOGTYPE_INT32, 0}
};

static const RTTRACELOGEVTDESC g_ISerialPortNotifyBrkEvtDesc =
{
    "ISerialPort.NotifyBrk",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialPortNotifyBrkEvtItems),
    g_ISerialPortNotifyBrkEvtItems
};

/**
 * @interface_method_impl{PDMISERIALPORT,pfnNotifyBrk}
 */
static DECLCALLBACK(int) drvIfTraceISerialPort_NotifyBrk(PPDMISERIALPORT pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialPort);
    int rc = pThis->pISerialPortAbove->pfnNotifyBrk(pThis->pISerialPortAbove);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialPortNotifyBrkEvtDesc, 0, 0, 0, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


/*
 *
 * ISerialConnector Implementation.
 *
 */
static const RTTRACELOGEVTITEMDESC g_ISerialConnectorDataAvailWrNotifyEvtItems[] =
{
    {"rc",      "Status code returned by the lower driver", RTTRACELOGTYPE_INT32, 0}
};

static const RTTRACELOGEVTDESC g_ISerialConnectorDataAvailWrNotifyEvtDesc =
{
    "ISerialConnector.DataAvailWrNotify",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialConnectorDataAvailWrNotifyEvtItems),
    g_ISerialConnectorDataAvailWrNotifyEvtItems
};

/** @interface_method_impl{PDMISERIALCONNECTOR,pfnDataAvailWrNotify} */
static DECLCALLBACK(int) drvIfTraceISerialConnector_DataAvailWrNotify(PPDMISERIALCONNECTOR pInterface)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialConnector);
    int rc = pThis->pISerialConBelow->pfnDataAvailWrNotify(pThis->pISerialConBelow);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialConnectorDataAvailWrNotifyEvtDesc, 0, 0, 0, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialConnectorReadRdrEvtItems[] =
{
    {"cbRead",  "Number of bytes to read max",                     RTTRACELOGTYPE_SIZE,  0},
    {"pcbRead", "Number of bytes actually read",                   RTTRACELOGTYPE_SIZE,  0},
    {"rc",      "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32, 0}
};

static const RTTRACELOGEVTDESC g_ISerialConnectorReadRdrEvtDesc =
{
    "ISerialConnector.ReadRdr",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialConnectorReadRdrEvtItems),
    g_ISerialConnectorReadRdrEvtItems
};

/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnReadRdr}
 */
static DECLCALLBACK(int) drvIfTraceISerialConnector_ReadRdr(PPDMISERIALCONNECTOR pInterface, void *pvBuf,
                                                            size_t cbRead, size_t *pcbRead)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialConnector);
    int rc = pThis->pISerialConBelow->pfnReadRdr(pThis->pISerialConBelow, pvBuf, cbRead, pcbRead);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialConnectorReadRdrEvtDesc, 0, 0, 0, cbRead, *pcbRead, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialConnectorChgParamsEvtItems[] =
{
    {"uBps",        "Baudrate",                                        RTTRACELOGTYPE_UINT32, 0},
    {"enmParity",   "The parity to configure",                         RTTRACELOGTYPE_UINT32, 0},
    {"cDataBits",   "Number of data bits for each symbol",             RTTRACELOGTYPE_UINT32, 0},
    {"enmStopBits", "Number of stop bits for each symbol",             RTTRACELOGTYPE_UINT32, 0},
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,  0}
};

static const RTTRACELOGEVTDESC g_ISerialConnectorChgParamsEvtDesc =
{
    "ISerialConnector.ChgParams",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialConnectorChgParamsEvtItems),
    g_ISerialConnectorChgParamsEvtItems
};

/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnChgParams}
 */
static DECLCALLBACK(int) drvIfTraceISerialConnector_ChgParams(PPDMISERIALCONNECTOR pInterface, uint32_t uBps,
                                                              PDMSERIALPARITY enmParity, unsigned cDataBits,
                                                              PDMSERIALSTOPBITS enmStopBits)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialConnector);
    int rc = pThis->pISerialConBelow->pfnChgParams(pThis->pISerialConBelow, uBps, enmParity, cDataBits, enmStopBits);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialConnectorChgParamsEvtDesc, 0, 0, 0,
                                         uBps, enmParity, cDataBits, enmStopBits, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialConnectorChgModemLinesEvtItems[] =
{
    {"fRts",        "State of RTS line",                               RTTRACELOGTYPE_BOOL,   0},
    {"fDtr",        "State of DTR line",                               RTTRACELOGTYPE_BOOL,   0},
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,  0}
};

static const RTTRACELOGEVTDESC g_ISerialConnectorChgModemLinesEvtDesc =
{
    "ISerialConnector.ChgModemLines",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialConnectorChgModemLinesEvtItems),
    g_ISerialConnectorChgModemLinesEvtItems
};

/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnChgModemLines}
 */
static DECLCALLBACK(int) drvIfTraceISerialConnector_ChgModemLines(PPDMISERIALCONNECTOR pInterface, bool fRts, bool fDtr)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialConnector);
    int rc = pThis->pISerialConBelow->pfnChgModemLines(pThis->pISerialConBelow, fRts, fDtr);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialConnectorChgModemLinesEvtDesc, 0, 0, 0, fRts, fDtr, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialConnectorChgBrkEvtItems[] =
{
    {"fBrk",        "Signal break flag",                               RTTRACELOGTYPE_BOOL,   0},
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,  0}
};

static const RTTRACELOGEVTDESC g_ISerialConnectorChgBrkEvtDesc =
{
    "ISerialConnector.ChgBrk",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialConnectorChgBrkEvtItems),
    g_ISerialConnectorChgBrkEvtItems
};

/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnChgBrk}
 */
static DECLCALLBACK(int) drvIfTraceISerialConnector_ChgBrk(PPDMISERIALCONNECTOR pInterface, bool fBrk)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialConnector);
    int rc = pThis->pISerialConBelow->pfnChgBrk(pThis->pISerialConBelow, fBrk);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialConnectorChgBrkEvtDesc, 0, 0, 0, fBrk, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialConnectorQueryStsLinesEvtItems[] =
{
    {"fStsLines",   "Status line flags",                               RTTRACELOGTYPE_UINT32, 0},
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,  0}
};

static const RTTRACELOGEVTDESC g_ISerialConnectorQueryStsLinesEvtDesc =
{
    "ISerialConnector.QueryStsLines",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialConnectorQueryStsLinesEvtItems),
    g_ISerialConnectorQueryStsLinesEvtItems
};

/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnQueryStsLines}
 */
static DECLCALLBACK(int) drvIfTraceISerialConnector_QueryStsLines(PPDMISERIALCONNECTOR pInterface, uint32_t *pfStsLines)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialConnector);
    int rc = pThis->pISerialConBelow->pfnQueryStsLines(pThis->pISerialConBelow, pfStsLines);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialConnectorQueryStsLinesEvtDesc, 0, 0, 0, *pfStsLines, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


static const RTTRACELOGEVTITEMDESC g_ISerialConnectorQueuesFlushEvtItems[] =
{
    {"fQueueRecv",  "Whether to flush the receive queue",              RTTRACELOGTYPE_BOOL, 0},
    {"fQueueXmit",  "Whether to flush the transmit queue",             RTTRACELOGTYPE_BOOL, 0},
    {"rc",          "Status code returned by the lower driver",        RTTRACELOGTYPE_INT32,  0}
};

static const RTTRACELOGEVTDESC g_ISerialConnectorQueuesFlushEvtDesc =
{
    "ISerialConnector.QueuesFlush",
    "",
    RTTRACELOGEVTSEVERITY_DEBUG,
    RT_ELEMENTS(g_ISerialConnectorQueuesFlushEvtItems),
    g_ISerialConnectorQueuesFlushEvtItems
};

/**
 * @callback_method_impl{PDMISERIALCONNECTOR,pfnQueuesFlush}
 */
static DECLCALLBACK(int) drvIfTraceISerialConnector_QueuesFlush(PPDMISERIALCONNECTOR pInterface, bool fQueueRecv, bool fQueueXmit)
{
    PDRVIFTRACE pThis = RT_FROM_MEMBER(pInterface, DRVIFTRACE, ISerialConnector);
    int rc = pThis->pISerialConBelow->pfnQueuesFlush(pThis->pISerialConBelow, fQueueRecv, fQueueXmit);

    int rcTraceLog = RTTraceLogWrEvtAddL(pThis->hTraceLog, &g_ISerialConnectorQueuesFlushEvtDesc, 0, 0, 0, fQueueRecv, fQueueXmit, rc);
    if (RT_FAILURE(rcTraceLog))
        LogRelMax(10, ("DrvIfTrace#%d: Failed to add event to trace log %Rrc\n", pThis->pDrvIns->iInstance, rcTraceLog));

    return rc;
}


/**
 * Initializes serial port relaated interfaces.
 *
 * @param   pThis                   The interface callback trace driver instance.
 */
DECLHIDDEN(void) drvIfsTrace_SerialIfInit(PDRVIFTRACE pThis)
{
    pThis->ISerialPort.pfnDataAvailRdrNotify     = drvIfTraceISerialPort_DataAvailRdrNotify;
    pThis->ISerialPort.pfnDataSentNotify         = drvIfTraceISerialPort_DataSentNotify;
    pThis->ISerialPort.pfnReadWr                 = drvIfTraceISerialPort_ReadWr;
    pThis->ISerialPort.pfnNotifyStsLinesChanged  = drvIfTraceISerialPort_NotifyStsLinesChanged;
    pThis->ISerialPort.pfnNotifyBrk              = drvIfTraceISerialPort_NotifyBrk;

    pThis->ISerialConnector.pfnDataAvailWrNotify = drvIfTraceISerialConnector_DataAvailWrNotify;
    pThis->ISerialConnector.pfnReadRdr           = drvIfTraceISerialConnector_ReadRdr;
    pThis->ISerialConnector.pfnChgParams         = drvIfTraceISerialConnector_ChgParams;
    pThis->ISerialConnector.pfnChgModemLines     = drvIfTraceISerialConnector_ChgModemLines;
    pThis->ISerialConnector.pfnChgBrk            = drvIfTraceISerialConnector_ChgBrk;
    pThis->ISerialConnector.pfnQueryStsLines     = drvIfTraceISerialConnector_QueryStsLines;
    pThis->ISerialConnector.pfnQueuesFlush       = drvIfTraceISerialConnector_QueuesFlush;
}

