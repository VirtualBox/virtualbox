/* $Id: VUSBSnifferVmx.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB Sniffer facility - VMX USBIO format.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_VUSB
#include <VBox/log.h>
#include <iprt/mem.h>
#include <iprt/buildconfig.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/time.h>

#include "VUSBSnifferInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * The internal VUSB sniffer state.
 */
typedef struct VUSBSNIFFERFMTINT
{
    /** Stream handle. */
    PVUSBSNIFFERSTRM  pStrm;
} VUSBSNIFFERFMTINT;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Supported file extensions.
 */
static const char *s_apszFileExts[] =
{
    "vmx",
    "vmware",
    "usbio",
    NULL
};


/**
 * Month strings.
 */
static const char *s_apszMonths[] =
{
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


static int vusbSnifferFmtVmxLogData(PVUSBSNIFFERFMTINT pThis, PRTTIME pTime, uint8_t *pbBuf, size_t cbBuf)
{
    int rc;
    char szLineBuf[256];
    size_t off = 0;

    do
    {
        size_t cch = RTStrPrintf(&szLineBuf[0], sizeof(szLineBuf),
                                 "%s %02u %02u:%02u:%02u.%3.*u: vmx| USBIO:  %03zx: %16.*Rhxs\n",
                                 s_apszMonths[pTime->u8Month - 1], pTime->u8MonthDay,
                                 pTime->u8Hour, pTime->u8Minute, pTime->u8Second, 3, pTime->u32Nanosecond,
                                 off, RT_MIN(cbBuf - off, 16), pbBuf);
        rc = pThis->pStrm->pfnWrite(pThis->pStrm, &szLineBuf[0], cch);
        off   += RT_MIN(cbBuf, 16);
        pbBuf += RT_MIN(cbBuf, 16);
    } while (RT_SUCCESS(rc) && off < cbBuf);

    return rc;
}

/** @interface_method_impl{VUSBSNIFFERFMT,pfnInit} */
static DECLCALLBACK(int) vusbSnifferFmtVmxInit(PVUSBSNIFFERFMTINT pThis, PVUSBSNIFFERSTRM pStrm)
{
    pThis->pStrm = pStrm;
    return VINF_SUCCESS;
}


/** @interface_method_impl{VUSBSNIFFERFMT,pfnDestroy} */
static DECLCALLBACK(void) vusbSnifferFmtVmxDestroy(PVUSBSNIFFERFMTINT pThis)
{
    NOREF(pThis);
}


/** @interface_method_impl{VUSBSNIFFERFMT,pfnRecordEvent} */
static DECLCALLBACK(int) vusbSnifferFmtVmxRecordEvent(PVUSBSNIFFERFMTINT pThis, PVUSBURB pUrb, VUSBSNIFFEREVENT enmEvent)
{
    RTTIMESPEC TimeNow;
    RTTIME Time;
    char szLineBuf[256];
    const char *pszEvt = enmEvent == VUSBSNIFFEREVENT_SUBMIT ? "Down" : "Up";
    uint8_t cIsocPkts = pUrb->enmType == VUSBXFERTYPE_ISOC ? pUrb->cIsocPkts : 0;

    if (pUrb->enmType == VUSBXFERTYPE_MSG)
        return VINF_SUCCESS;

    RT_ZERO(szLineBuf);

    RTTimeNow(&TimeNow);
    RTTimeExplode(&Time, &TimeNow);

    size_t cch = RTStrPrintf(&szLineBuf[0], sizeof(szLineBuf),
                             "%s %02u %02u:%02u:%02u.%3.*u: vmx| USBIO: %s dev=%u endpt=%x datalen=%u numPackets=%u status=%u 0\n",
                             s_apszMonths[Time.u8Month - 1], Time.u8MonthDay, Time.u8Hour, Time.u8Minute, Time.u8Second, 3, Time.u32Nanosecond,
                             pszEvt, pUrb->DstAddress, pUrb->EndPt | (pUrb->enmDir == VUSBDIRECTION_IN ? 0x80 : 0x00),
                             pUrb->cbData, cIsocPkts, pUrb->enmStatus);
    int rc = pThis->pStrm->pfnWrite(pThis->pStrm, &szLineBuf[0], cch);
    if (RT_SUCCESS(rc))
    {
        /* Log the data in the appropriate stage. */
        if (   pUrb->enmType == VUSBXFERTYPE_CTRL
            || pUrb->enmType == VUSBXFERTYPE_MSG)
        {
            if (enmEvent == VUSBSNIFFEREVENT_SUBMIT)
                rc = vusbSnifferFmtVmxLogData(pThis, &Time, &pUrb->abData[0], sizeof(VUSBSETUP));
            else if (enmEvent == VUSBSNIFFEREVENT_COMPLETE)
            {
                rc = vusbSnifferFmtVmxLogData(pThis, &Time, &pUrb->abData[0], sizeof(VUSBSETUP));
                if (   RT_SUCCESS(rc)
                    && pUrb->cbData > sizeof(VUSBSETUP))
                    rc = vusbSnifferFmtVmxLogData(pThis, &Time, &pUrb->abData[sizeof(VUSBSETUP)], pUrb->cbData - sizeof(VUSBSETUP));
            }
        }
        else
        {
            if (   enmEvent == VUSBSNIFFEREVENT_SUBMIT
                && pUrb->enmDir == VUSBDIRECTION_OUT)
                rc = vusbSnifferFmtVmxLogData(pThis, &Time, &pUrb->abData[0], pUrb->cbData);
            else if (   enmEvent == VUSBSNIFFEREVENT_COMPLETE
                     && pUrb->enmDir == VUSBDIRECTION_IN)
                rc = vusbSnifferFmtVmxLogData(pThis, &Time, &pUrb->abData[0], pUrb->cbData);
        }
    }

    return rc;
}

/**
 * VUSB sniffer format writer.
 */
const VUSBSNIFFERFMT g_VUsbSnifferFmtVmx =
{
    /** szName */
    "VMX",
    /** pszDesc */
    "VMX log format writer supported by vusb-analyzer: http://vusb-analyzer.sourceforge.net",
    /** papszFileExts */
    &s_apszFileExts[0],
    /** cbFmt */
    sizeof(VUSBSNIFFERFMTINT),
    /** pfnInit */
    vusbSnifferFmtVmxInit,
    /** pfnDestroy */
    vusbSnifferFmtVmxDestroy,
    /** pfnRecordEvent */
    vusbSnifferFmtVmxRecordEvent
};

