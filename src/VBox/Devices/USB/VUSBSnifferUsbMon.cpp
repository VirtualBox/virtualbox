/* $Id: VUSBSnifferUsbMon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB Sniffer facility - Linux usbmon ASCII format.
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
    "mon",
    "usbmon",
    NULL
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/** @interface_method_impl{VUSBSNIFFERFMT,pfnInit} */
static DECLCALLBACK(int) vusbSnifferFmtUsbMonInit(PVUSBSNIFFERFMTINT pThis, PVUSBSNIFFERSTRM pStrm)
{
    pThis->pStrm = pStrm;
    return VINF_SUCCESS;
}


/** @interface_method_impl{VUSBSNIFFERFMT,pfnDestroy} */
static DECLCALLBACK(void) vusbSnifferFmtUsbMonDestroy(PVUSBSNIFFERFMTINT pThis)
{
    RT_NOREF(pThis);
}


/** @interface_method_impl{VUSBSNIFFERFMT,pfnRecordEvent} */
static DECLCALLBACK(int) vusbSnifferFmtUsbMonRecordEvent(PVUSBSNIFFERFMTINT pThis, PVUSBURB pUrb, VUSBSNIFFEREVENT enmEvent)
{
    char aszLineBuf[512];
    char chEvtType = 'X';
    char chDir = 'X';
    char chEptType = 'X';

    switch (enmEvent)
    {
        case VUSBSNIFFEREVENT_SUBMIT:
            chEvtType = 'S';
            break;
        case VUSBSNIFFEREVENT_COMPLETE:
            chEvtType = 'C';
            break;
        case VUSBSNIFFEREVENT_ERROR_SUBMIT:
        case VUSBSNIFFEREVENT_ERROR_COMPLETE:
            chEvtType = 'E';
            break;
        default:
            AssertMsgFailed(("Invalid event type %d\n", enmEvent));
    }

    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_ISOC:
            chEptType = 'Z';
            break;
        case VUSBXFERTYPE_BULK:
            chEptType = 'B';
            break;
        case VUSBXFERTYPE_INTR:
            chEptType = 'I';
            break;
        case VUSBXFERTYPE_CTRL:
        case VUSBXFERTYPE_MSG:
            chEptType = 'C';
            break;
        default:
            AssertMsgFailed(("invalid transfer type %d\n", pUrb->enmType));
    }

    if (pUrb->enmDir == VUSBDIRECTION_IN)
        chDir = 'i';
    else if (pUrb->enmDir == VUSBDIRECTION_OUT)
        chDir = 'o';
    else if (pUrb->enmDir == VUSBDIRECTION_SETUP)
        chDir = 'o';

    RT_ZERO(aszLineBuf);

    /* Assemble the static part. */
    size_t cch = RTStrPrintf(&aszLineBuf[0], sizeof(aszLineBuf), "%p %llu %c %c%c:%u:%u:%u ",
                             pUrb, RTTimeNanoTS() / RT_NS_1US, chEvtType, chEptType, chDir,
                             0, pUrb->DstAddress, pUrb->EndPt | (pUrb->enmDir == VUSBDIRECTION_IN ? 0x80 : 0x00));
    int rc = pThis->pStrm->pfnWrite(pThis->pStrm, &aszLineBuf[0], cch);
    if (RT_SUCCESS(rc))
    {
        /* Log the setup packet for control requests, the status otherwise. */
        if (   (pUrb->enmType == VUSBXFERTYPE_CTRL || pUrb->enmType == VUSBXFERTYPE_MSG)
            && enmEvent == VUSBSNIFFEREVENT_SUBMIT)
        {
            PVUSBSETUP pSetup = (PVUSBSETUP)pUrb->abData;

            cch = RTStrPrintf(&aszLineBuf[0], sizeof(aszLineBuf), "s %02x %02x %04x %04x %04x ",
                              pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue,
                              pSetup->wIndex, pSetup->wLength);
            rc = pThis->pStrm->pfnWrite(pThis->pStrm, &aszLineBuf[0], cch);
        }
        else
        {
            bool fLogAdditionalStatus =    pUrb->enmType == VUSBXFERTYPE_ISOC
                                        || pUrb->enmType == VUSBXFERTYPE_INTR;

            cch = RTStrPrintf(&aszLineBuf[0], sizeof(aszLineBuf), "%d%s", pUrb->enmStatus,
                              fLogAdditionalStatus ? "" : " ");

            /* There are additional fields to log for isochronous and interrupt URBs. */
            if (pUrb->enmType == VUSBXFERTYPE_ISOC)
            {
                if (enmEvent == VUSBSNIFFEREVENT_COMPLETE)
                {
                    uint32_t u32ErrorCount = 0;

                    for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
                        if (   pUrb->aIsocPkts[i].enmStatus != VUSBSTATUS_OK
                            && pUrb->aIsocPkts[i].enmStatus != VUSBSTATUS_NOT_ACCESSED)
                            u32ErrorCount++;

                    cch += RTStrPrintf(&aszLineBuf[cch], sizeof(aszLineBuf) - cch, ":%u:%u:%u ",
                                       1 /* Interval */, 0 /* Frame number */, u32ErrorCount);
                }
                else
                    cch += RTStrPrintf(&aszLineBuf[cch], sizeof(aszLineBuf) - cch, ":%u:%u ",
                                       1 /* Interval */, 0 /* Frame number */);
            }
            else if (pUrb->enmType == VUSBXFERTYPE_INTR)
                cch += RTStrPrintf(&aszLineBuf[cch], sizeof(aszLineBuf) - cch, ":%u ",
                                   1 /* Interval */);

            rc = pThis->pStrm->pfnWrite(pThis->pStrm, &aszLineBuf[0], cch);
        }

        /* Log the packet descriptors for isochronous URBs. */
        if (   RT_SUCCESS(rc)
            && pUrb->enmType == VUSBXFERTYPE_ISOC)
        {
            cch = RTStrPrintf(&aszLineBuf[0], sizeof(aszLineBuf), "%u ", pUrb->cIsocPkts);
            rc = pThis->pStrm->pfnWrite(pThis->pStrm, &aszLineBuf[0], cch);
            for (unsigned i = 0; i < pUrb->cIsocPkts && RT_SUCCESS(rc); i++)
            {
                cch = RTStrPrintf(&aszLineBuf[0], sizeof(aszLineBuf), "%d:%u:%u ",
                                  pUrb->aIsocPkts[i].enmStatus, pUrb->aIsocPkts[i].off,
                                  pUrb->aIsocPkts[i].cb);
                rc = pThis->pStrm->pfnWrite(pThis->pStrm, &aszLineBuf[0], cch);
            }
        }

        if (RT_SUCCESS(rc))
        {
            /* Print data length */
            cch = RTStrPrintf(&aszLineBuf[0], sizeof(aszLineBuf), "%d n\n", pUrb->cbData);
            rc = pThis->pStrm->pfnWrite(pThis->pStrm, &aszLineBuf[0], cch);
        }

        /** @todo Dump the data */
    }

    return rc;
}

/**
 * VUSB sniffer format writer.
 */
const VUSBSNIFFERFMT g_VUsbSnifferFmtUsbMon =
{
    /** szName */
    "USBMON",
    /** pszDesc */
    "UsbMon format writer compatible with vusb-analyzer: http://vusb-analyzer.sourceforge.net",
    /** papszFileExts */
    &s_apszFileExts[0],
    /** cbFmt */
    sizeof(VUSBSNIFFERFMTINT),
    /** pfnInit */
    vusbSnifferFmtUsbMonInit,
    /** pfnDestroy */
    vusbSnifferFmtUsbMonDestroy,
    /** pfnRecordEvent */
    vusbSnifferFmtUsbMonRecordEvent
};

