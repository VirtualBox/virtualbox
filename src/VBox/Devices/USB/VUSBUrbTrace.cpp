/* $Id: VUSBUrbTrace.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB - URBs.
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
#define LOG_GROUP LOG_GROUP_DRV_VUSB
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/vmapi.h>
#include <iprt/errcore.h>
#include <iprt/alloc.h>
#include <VBox/log.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/env.h>
#include "VUSBInternal.h"



#ifdef LOG_ENABLED
DECLINLINE(const char *) GetScsiErrCd(uint8_t ScsiErr)
{
    switch (ScsiErr)
    {
        case 0:     return "?";
    }
    return "?";
}

DECLINLINE(const char *) GetScsiKCQ(uint8_t Key, uint8_t ASC, uint8_t ASCQ)
{
    switch (Key)
    {
        case 0:
            switch (RT_MAKE_U16(ASC, ASCQ))
            {
                case RT_MAKE_U16(0x00, 0x00):  return "No error";
            }
            break;

        case 1:
            return "Soft Error";

        case 2:
            return "Not Ready";

        case 3:
            return "Medium Error";

        case 4:
            return "Hard Error";

        case 5:
            return "Illegal Request";

        case 6:
            return "Unit Attention";

        case 7:
            return "Write Protected";

        case 0xb:
            return "Aborted Command";
    }
    return "?";
}

DECLHIDDEN(const char *) vusbUrbStatusName(VUSBSTATUS enmStatus)
{
    /** Strings for the URB statuses. */
    static const char * const s_apszNames[] =
    {
        "OK",
        "STALL",
        "ERR_DNR",
        "ERR_CRC",
        "DATA_UNDERRUN",
        "DATA_OVERRUN",
        "NOT_ACCESSED",
        "7", "8", "9", "10", "11", "12", "13", "14", "15"
    };

    return enmStatus < (int)RT_ELEMENTS(s_apszNames)
        ? s_apszNames[enmStatus]
        : enmStatus == VUSBSTATUS_INVALID
            ? "INVALID"
            : "??";
}

DECLHIDDEN(const char *) vusbUrbDirName(VUSBDIRECTION enmDir)
{
    /** Strings for the URB directions. */
    static const char * const s_apszNames[] =
    {
        "setup",
        "in",
        "out"
    };

    return enmDir < (int)RT_ELEMENTS(s_apszNames)
        ? s_apszNames[enmDir]
        : "??";
}

DECLHIDDEN(const char *) vusbUrbTypeName(VUSBXFERTYPE enmType)
{
    /** Strings for the URB types. */
    static const char * const s_apszName[] =
    {
        "control-part",
        "isochronous",
        "bulk",
        "interrupt",
        "control"
    };

    return enmType < (int)RT_ELEMENTS(s_apszName)
        ? s_apszName[enmType]
        : "??";
}

/**
 * Logs an URB.
 *
 * Note that pUrb->pVUsb->pDev and pUrb->pVUsb->pDev->pUsbIns can all be NULL.
 */
DECLHIDDEN(void) vusbUrbTrace(PVUSBURB pUrb, const char *pszMsg, bool fComplete)
{
    PVUSBDEV        pDev   = pUrb->pVUsb ? pUrb->pVUsb->pDev : NULL; /* Can be NULL when called from usbProxyConstruct and friends. */
    PVUSBPIPE       pPipe  = pDev ? &pDev->aPipes[pUrb->EndPt] : NULL;
    const uint8_t  *pbData = pUrb->abData;
    uint32_t        cbData = pUrb->cbData;
    PCVUSBSETUP     pSetup = NULL;
    bool            fDescriptors = false;
    static size_t   s_cchMaxMsg = 10;
    size_t          cchMsg = strlen(pszMsg);
    if (cchMsg > s_cchMaxMsg)
        s_cchMaxMsg = cchMsg;

    Log(("%s: %*s: pDev=%p[%s] rc=%s a=%i e=%u d=%s t=%s cb=%#x(%d) ts=%RU64 (%RU64 ns ago) %s\n",
         pUrb->pszDesc, s_cchMaxMsg, pszMsg,
         pDev,
         pUrb->pVUsb && pUrb->pVUsb->pDev && pUrb->pVUsb->pDev->pUsbIns ? pUrb->pVUsb->pDev->pUsbIns->pszName : "",
         vusbUrbStatusName(pUrb->enmStatus),
         pDev ? pDev->u8Address : -1,
         pUrb->EndPt,
         vusbUrbDirName(pUrb->enmDir),
         vusbUrbTypeName(pUrb->enmType),
         pUrb->cbData,
         pUrb->cbData,
         pUrb->pVUsb ? pUrb->pVUsb->u64SubmitTS : 0,
         pUrb->pVUsb ? RTTimeNanoTS() - pUrb->pVUsb->u64SubmitTS : 0,
         pUrb->fShortNotOk ? "ShortNotOk" : "ShortOk"));

#ifndef DEBUG_bird
    if (    pUrb->enmType   == VUSBXFERTYPE_CTRL
        &&  pUrb->enmStatus == VUSBSTATUS_OK)
        return;
#endif

    if (    pUrb->enmType == VUSBXFERTYPE_MSG
        ||  (   pUrb->enmDir  == VUSBDIRECTION_SETUP
             && pUrb->enmType == VUSBXFERTYPE_CTRL
             && cbData))
    {
        static const char * const s_apszReqDirs[]       = {"host2dev", "dev2host"};
        static const char * const s_apszReqTypes[]      = {"std", "class", "vendor", "reserved"};
        static const char * const s_apszReqRecipients[] = {"dev", "if", "endpoint", "other"};
        static const char * const s_apszRequests[] =
        {
            "GET_STATUS",        "CLEAR_FEATURE",     "2?",             "SET_FEATURE",
            "4?",                "SET_ADDRESS",       "GET_DESCRIPTOR", "SET_DESCRIPTOR",
            "GET_CONFIGURATION", "SET_CONFIGURATION", "GET_INTERFACE",  "SET_INTERFACE",
            "SYNCH_FRAME"
        };
        pSetup = (PVUSBSETUP)pUrb->abData;
        pbData += sizeof(*pSetup);
        cbData -= sizeof(*pSetup);

        Log(("%s: %*s: CTRL: bmRequestType=0x%.2x (%s %s %s) bRequest=0x%.2x (%s) wValue=0x%.4x wIndex=0x%.4x wLength=0x%.4x\n",
             pUrb->pszDesc, s_cchMaxMsg, pszMsg,
             pSetup->bmRequestType, s_apszReqDirs[pSetup->bmRequestType >> 7], s_apszReqTypes[(pSetup->bmRequestType >> 5) & 0x3],
             (unsigned)(pSetup->bmRequestType & 0xf) < RT_ELEMENTS(s_apszReqRecipients) ? s_apszReqRecipients[pSetup->bmRequestType & 0xf] : "??",
             pSetup->bRequest, pSetup->bRequest < RT_ELEMENTS(s_apszRequests) ? s_apszRequests[pSetup->bRequest] : "??",
             pSetup->wValue, pSetup->wIndex, pSetup->wLength));

        if (    pSetup->bRequest == VUSB_REQ_GET_DESCRIPTOR
            &&  fComplete
            &&  pUrb->enmStatus == VUSBSTATUS_OK
            &&  ((pSetup->bmRequestType >> 5) & 0x3) < 2 /* vendor */)
            fDescriptors = true;
    }
    else if (   fComplete
             && pUrb->enmDir == VUSBDIRECTION_IN
             && pUrb->enmType == VUSBXFERTYPE_CTRL
             && pUrb->enmStatus == VUSBSTATUS_OK
             && pPipe->pCtrl
             && pPipe->pCtrl->enmStage == CTLSTAGE_DATA
             && cbData > 0)
    {
        pSetup = pPipe->pCtrl->pMsg;
        if (pSetup->bRequest == VUSB_REQ_GET_DESCRIPTOR)
        {
            /* HID report (0x22) and physical (0x23) descriptors do not use standard format
             * with descriptor length/type at the front. Don't try to dump them, we'll only
             * misinterpret them.
             */
            if (    ((pSetup->bmRequestType >> 5) & 0x3) == 1   /* class */
                && ((RT_HIBYTE(pSetup->wValue) == 0x22) || (RT_HIBYTE(pSetup->wValue) == 0x23)))
            {
                fDescriptors = false;
            }
        }
        else
            fDescriptors = true;
    }

    /*
     * Dump descriptors.
     */
    if (fDescriptors)
    {
        const uint8_t *pb = pbData;
        const uint8_t *pbEnd = pbData + cbData;
        while (pb + 1 < pbEnd)
        {
            const unsigned  cbLeft = pbEnd - pb;
            const unsigned  cbLength = *pb;
            unsigned        cb = cbLength;
            uint8_t         bDescriptorType = pb[1];

            /* length out of bounds? */
            if (cbLength > cbLeft)
            {
                cb = cbLeft;
                if (cbLength != 0xff) /* ignore this */
                    Log(("URB: %*s: DESC: warning descriptor length goes beyond the end of the URB! cbLength=%d cbLeft=%d\n",
                         s_cchMaxMsg, pszMsg, cbLength, cbLeft));
            }

            if (cb >= 2)
            {
                Log(("URB: %*s: DESC: %04x: %25s = %#04x (%d)\n"
                     "URB: %*s:       %04x: %25s = %#04x (",
                     s_cchMaxMsg, pszMsg, pb - pbData, "bLength", cbLength, cbLength,
                     s_cchMaxMsg, pszMsg, pb - pbData + 1, "bDescriptorType", bDescriptorType));

                #pragma pack(1)
                #define BYTE_FIELD(strct, memb) \
                    if ((unsigned)RT_OFFSETOF(strct, memb) < cb) \
                        Log(("URB: %*s:       %04x: %25s = %#04x\n", s_cchMaxMsg, pszMsg, \
                             pb + RT_OFFSETOF(strct, memb) - pbData, #memb, pb[RT_OFFSETOF(strct, memb)]))
                #define BYTE_FIELD_START(strct, memb) do { \
                    if ((unsigned)RT_OFFSETOF(strct, memb) < cb) \
                    { \
                        Log(("URB: %*s:       %04x: %25s = %#04x", s_cchMaxMsg, pszMsg, \
                             pb + RT_OFFSETOF(strct, memb) - pbData, #memb, pb[RT_OFFSETOF(strct, memb)]))
                #define BYTE_FIELD_END(strct, memb) \
                        Log(("\n")); \
                    } } while (0)
                #define WORD_FIELD(strct, memb) \
                    if ((unsigned)RT_OFFSETOF(strct, memb) + 1 < cb) \
                        Log(("URB: %*s:       %04x: %25s = %#06x\n", s_cchMaxMsg, pszMsg, \
                             pb + RT_OFFSETOF(strct, memb) - pbData, #memb, *(uint16_t *)&pb[RT_OFFSETOF(strct, memb)]))
                #define BCD_FIELD(strct, memb) \
                    if ((unsigned)RT_OFFSETOF(strct, memb) + 1 < cb) \
                        Log(("URB: %*s:       %04x: %25s = %#06x (%02x.%02x)\n", s_cchMaxMsg, pszMsg, \
                             pb + RT_OFFSETOF(strct, memb) - pbData, #memb, *(uint16_t *)&pb[RT_OFFSETOF(strct, memb)], \
                             pb[RT_OFFSETOF(strct, memb) + 1], pb[RT_OFFSETOF(strct, memb)]))
                #define SIZE_CHECK(strct) \
                    if (cb > sizeof(strct)) \
                        Log(("URB: %*s:       %04x: WARNING %d extra byte(s) %.*Rhxs\n", s_cchMaxMsg, pszMsg, \
                             pb + sizeof(strct) - pbData, cb - sizeof(strct), cb - sizeof(strct), pb + sizeof(strct))); \
                    else if (cb < sizeof(strct)) \
                        Log(("URB: %*s:       %04x: WARNING %d missing byte(s)! Expected size %d.\n", s_cchMaxMsg, pszMsg, \
                             pb + cb - pbData, sizeof(strct) - cb, sizeof(strct)))

                /* on type */
                switch (bDescriptorType)
                {
                    case VUSB_DT_DEVICE:
                    {
                        struct dev_desc
                        {
                            uint8_t  bLength;
                            uint8_t  bDescriptorType;
                            uint16_t bcdUSB;
                            uint8_t  bDeviceClass;
                            uint8_t  bDeviceSubClass;
                            uint8_t  bDeviceProtocol;
                            uint8_t  bMaxPacketSize0;
                            uint16_t idVendor;
                            uint16_t idProduct;
                            uint16_t bcdDevice;
                            uint8_t  iManufacturer;
                            uint8_t  iProduct;
                            uint8_t  iSerialNumber;
                            uint8_t  bNumConfigurations;
                        } *pDesc = (struct dev_desc *)pb; NOREF(pDesc);
                        Log(("DEV)\n"));
                        BCD_FIELD( struct dev_desc, bcdUSB);
                        BYTE_FIELD(struct dev_desc, bDeviceClass);
                        BYTE_FIELD(struct dev_desc, bDeviceSubClass);
                        BYTE_FIELD(struct dev_desc, bDeviceProtocol);
                        BYTE_FIELD(struct dev_desc, bMaxPacketSize0);
                        WORD_FIELD(struct dev_desc, idVendor);
                        WORD_FIELD(struct dev_desc, idProduct);
                        BCD_FIELD( struct dev_desc, bcdDevice);
                        BYTE_FIELD(struct dev_desc, iManufacturer);
                        BYTE_FIELD(struct dev_desc, iProduct);
                        BYTE_FIELD(struct dev_desc, iSerialNumber);
                        BYTE_FIELD(struct dev_desc, bNumConfigurations);
                        SIZE_CHECK(struct dev_desc);
                        break;
                    }

                    case VUSB_DT_CONFIG:
                    {
                        struct cfg_desc
                        {
                            uint8_t  bLength;
                            uint8_t  bDescriptorType;
                            uint16_t wTotalLength;
                            uint8_t  bNumInterfaces;
                            uint8_t  bConfigurationValue;
                            uint8_t  iConfiguration;
                            uint8_t  bmAttributes;
                            uint8_t  MaxPower;
                        } *pDesc = (struct cfg_desc *)pb; NOREF(pDesc);
                        Log(("CFG)\n"));
                        WORD_FIELD(struct cfg_desc, wTotalLength);
                        BYTE_FIELD(struct cfg_desc, bNumInterfaces);
                        BYTE_FIELD(struct cfg_desc, bConfigurationValue);
                        BYTE_FIELD(struct cfg_desc, iConfiguration);
                        BYTE_FIELD_START(struct cfg_desc, bmAttributes);
                            static const char * const s_apszTransType[4] = { "Control", "Isochronous", "Bulk", "Interrupt" };
                            static const char * const s_apszSyncType[4]  = { "NoSync", "Asynchronous", "Adaptive", "Synchronous" };
                            static const char * const s_apszUsageType[4] = { "Data ep", "Feedback ep.", "Implicit feedback Data ep.", "Reserved" };
                            Log((" %s - %s - %s", s_apszTransType[(pDesc->bmAttributes & 0x3)],
                                 s_apszSyncType[((pDesc->bmAttributes >> 2) & 0x3)], s_apszUsageType[((pDesc->bmAttributes >> 4) & 0x3)]));
                        BYTE_FIELD_END(struct cfg_desc, bmAttributes);
                        BYTE_FIELD(struct cfg_desc, MaxPower);
                        SIZE_CHECK(struct cfg_desc);
                        break;
                    }

                    case VUSB_DT_STRING:
                        if (!pSetup->wIndex)
                        {
                            /* langid array */
                            uint16_t *pu16 = (uint16_t *)pb + 1;
                            Log(("LANGIDs)\n"));
                            while ((uintptr_t)pu16 + 2 - (uintptr_t)pb <= cb)
                            {
                                Log(("URB: %*s:       %04x: wLANGID[%#x] = %#06x\n",
                                     s_cchMaxMsg, pszMsg, (uint8_t *)pu16 - pbData, pu16 - (uint16_t *)pb, *pu16));
                                pu16++;
                            }
                            if (cb & 1)
                                Log(("URB: %*s:       %04x: WARNING descriptor size is odd! extra byte: %02\n",
                                     s_cchMaxMsg, pszMsg, (uint8_t *)pu16 - pbData, *(uint8_t *)pu16));
                        }
                        else
                        {
                            /** a string. */
                            Log(("STRING)\n"));
                            if (cb > 2)
                                Log(("URB: %*s:       %04x: Length=%d String=%.*ls\n",
                                     s_cchMaxMsg, pszMsg, pb - pbData, cb - 2, cb / 2 - 1, pb + 2));
                            else
                                Log(("URB: %*s:       %04x: Length=0\n", s_cchMaxMsg, pszMsg, pb - pbData));
                        }
                        break;

                    case VUSB_DT_INTERFACE:
                    {
                        struct if_desc
                        {
                            uint8_t  bLength;
                            uint8_t  bDescriptorType;
                            uint8_t  bInterfaceNumber;
                            uint8_t  bAlternateSetting;
                            uint8_t  bNumEndpoints;
                            uint8_t  bInterfaceClass;
                            uint8_t  bInterfaceSubClass;
                            uint8_t  bInterfaceProtocol;
                            uint8_t  iInterface;
                        } *pDesc = (struct if_desc *)pb; NOREF(pDesc);
                        Log(("IF)\n"));
                        BYTE_FIELD(struct if_desc, bInterfaceNumber);
                        BYTE_FIELD(struct if_desc, bAlternateSetting);
                        BYTE_FIELD(struct if_desc, bNumEndpoints);
                        BYTE_FIELD(struct if_desc, bInterfaceClass);
                        BYTE_FIELD(struct if_desc, bInterfaceSubClass);
                        BYTE_FIELD(struct if_desc, bInterfaceProtocol);
                        BYTE_FIELD(struct if_desc, iInterface);
                        SIZE_CHECK(struct if_desc);
                        break;
                    }

                    case VUSB_DT_ENDPOINT:
                    {
                        struct ep_desc
                        {
                            uint8_t  bLength;
                            uint8_t  bDescriptorType;
                            uint8_t  bEndpointAddress;
                            uint8_t  bmAttributes;
                            uint16_t wMaxPacketSize;
                            uint8_t  bInterval;
                        } *pDesc = (struct ep_desc *)pb; NOREF(pDesc);
                        Log(("EP)\n"));
                        BYTE_FIELD(struct ep_desc, bEndpointAddress);
                        BYTE_FIELD(struct ep_desc, bmAttributes);
                        WORD_FIELD(struct ep_desc, wMaxPacketSize);
                        BYTE_FIELD(struct ep_desc, bInterval);
                        SIZE_CHECK(struct ep_desc);
                        break;
                    }

                    case VUSB_DT_DEVICE_QUALIFIER:
                    {
                        struct dq_desc
                        {
                            uint8_t  bLength;
                            uint8_t  bDescriptorType;
                            uint16_t bcdUSB;
                            uint8_t  bDeviceClass;
                            uint8_t  bDeviceSubClass;
                            uint8_t  bDeviceProtocol;
                            uint8_t  bMaxPacketSize0;
                            uint8_t  bNumConfigurations;
                            uint8_t  bReserved;
                        } *pDQDesc = (struct dq_desc *)pb; NOREF(pDQDesc);
                        Log(("DEVQ)\n"));
                        BCD_FIELD( struct dq_desc, bcdUSB);
                        BYTE_FIELD(struct dq_desc, bDeviceClass);
                        BYTE_FIELD(struct dq_desc, bDeviceSubClass);
                        BYTE_FIELD(struct dq_desc, bDeviceProtocol);
                        BYTE_FIELD(struct dq_desc, bMaxPacketSize0);
                        BYTE_FIELD(struct dq_desc, bNumConfigurations);
                        BYTE_FIELD(struct dq_desc, bReserved);
                        SIZE_CHECK(struct dq_desc);
                        break;
                    }

                    case VUSB_DT_OTHER_SPEED_CFG:
                    {
                        struct oth_cfg_desc
                        {
                            uint8_t  bLength;
                            uint8_t  bDescriptorType;
                            uint16_t wTotalLength;
                            uint8_t  bNumInterfaces;
                            uint8_t  bConfigurationValue;
                            uint8_t  iConfiguration;
                            uint8_t  bmAttributes;
                            uint8_t  MaxPower;
                        } *pDesc = (struct oth_cfg_desc *)pb; NOREF(pDesc);
                        Log(("OCFG)\n"));
                        WORD_FIELD(struct oth_cfg_desc, wTotalLength);
                        BYTE_FIELD(struct oth_cfg_desc, bNumInterfaces);
                        BYTE_FIELD(struct oth_cfg_desc, bConfigurationValue);
                        BYTE_FIELD(struct oth_cfg_desc, iConfiguration);
                        BYTE_FIELD_START(struct oth_cfg_desc, bmAttributes);
                            static const char * const s_apszTransType[4] = { "Control", "Isochronous", "Bulk", "Interrupt" };
                            static const char * const s_apszSyncType[4]  = { "NoSync", "Asynchronous", "Adaptive", "Synchronous" };
                            static const char * const s_apszUsageType[4] = { "Data ep", "Feedback ep.", "Implicit feedback Data ep.", "Reserved" };
                            Log((" %s - %s - %s", s_apszTransType[(pDesc->bmAttributes & 0x3)],
                                 s_apszSyncType[((pDesc->bmAttributes >> 2) & 0x3)], s_apszUsageType[((pDesc->bmAttributes >> 4) & 0x3)]));
                        BYTE_FIELD_END(struct oth_cfg_desc, bmAttributes);
                        BYTE_FIELD(struct oth_cfg_desc, MaxPower);
                        SIZE_CHECK(struct oth_cfg_desc);
                        break;
                    }

                    case 0x21:
                    {
                        struct hid_desc
                        {
                            uint8_t  bLength;
                            uint8_t  bDescriptorType;
                            uint16_t bcdHid;
                            uint8_t  bCountry;
                            uint8_t  bNumDescriptors;
                            uint8_t  bReportType;
                            uint16_t wReportLength;
                        } *pDesc = (struct hid_desc *)pb; NOREF(pDesc);
                        Log(("EP)\n"));
                        BCD_FIELD( struct hid_desc, bcdHid);
                        BYTE_FIELD(struct hid_desc, bCountry);
                        BYTE_FIELD(struct hid_desc, bNumDescriptors);
                        BYTE_FIELD(struct hid_desc, bReportType);
                        WORD_FIELD(struct hid_desc, wReportLength);
                        SIZE_CHECK(struct hid_desc);
                        break;
                    }

                    case 0xff:
                        Log(("UNKNOWN-ignore)\n"));
                        break;

                    default:
                        Log(("UNKNOWN)!!!\n"));
                        break;
                }

                #undef BYTE_FIELD
                #undef WORD_FIELD
                #undef BCD_FIELD
                #undef SIZE_CHECK
                #pragma pack()
            }
            else
            {
                Log(("URB: %*s: DESC: %04x: bLength=%d bDescriptorType=%d - invalid length\n",
                     s_cchMaxMsg, pszMsg, pb - pbData, cb, bDescriptorType));
                break;
            }

            /* next */
            pb += cb;
        }
    }

    /*
     * SCSI
     */
    if (    pUrb->enmType == VUSBXFERTYPE_BULK
        &&  pUrb->enmDir  == VUSBDIRECTION_OUT
        &&  pUrb->cbData >= 12
        &&  !memcmp(pUrb->abData, "USBC", 4))
    {
        const struct usbc
        {
            uint32_t    Signature;
            uint32_t    Tag;
            uint32_t    DataTransferLength;
            uint8_t     Flags;
            uint8_t     Lun;
            uint8_t     Length;
            uint8_t     CDB[13];
        } *pUsbC = (struct usbc *)pUrb->abData;
        Log(("URB: %*s: SCSI: Tag=%#x DataTransferLength=%#x Flags=%#x Lun=%#x Length=%#x CDB=%.*Rhxs\n",
             s_cchMaxMsg, pszMsg, pUsbC->Tag, pUsbC->DataTransferLength, pUsbC->Flags, pUsbC->Lun,
             pUsbC->Length, pUsbC->Length, pUsbC->CDB));
        const uint8_t *pb = &pUsbC->CDB[0];
        switch (pb[0])
        {
            case 0x00: /* test unit read */
                Log(("URB: %*s: SCSI: TEST_UNIT_READY LUN=%d Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg, pb[1] >> 5, pb[5]));
                break;
            case 0x03: /* Request Sense command */
                Log(("URB: %*s: SCSI: REQUEST_SENSE LUN=%d AlcLen=%#RX16 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg, pb[1] >> 5, pb[4], pb[5]));
                break;
            case 0x12: /* Inquiry command. */
                Log(("URB: %*s: SCSI: INQUIRY EVPD=%d LUN=%d PgCd=%#RX8 AlcLen=%#RX8 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg, pb[1] & 1, pb[1] >> 5, pb[2], pb[4], pb[5]));
                break;
            case 0x1a: /* Mode Sense(6) command */
                Log(("URB: %*s: SCSI: MODE_SENSE6 LUN=%d DBD=%d PC=%d PgCd=%#RX8 AlcLen=%#RX8 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg, pb[1] >> 5, !!(pb[1] & RT_BIT(3)), pb[2] >> 6, pb[2] & 0x3f, pb[4], pb[5]));
                break;
            case 0x5a:
                Log(("URB: %*s: SCSI: MODE_SENSE10 LUN=%d DBD=%d PC=%d PgCd=%#RX8 AlcLen=%#RX16 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg, pb[1] >> 5, !!(pb[1] & RT_BIT(3)), pb[2] >> 6, pb[2] & 0x3f,
                     RT_MAKE_U16(pb[8], pb[7]), pb[9]));
                break;
            case 0x25: /* Read Capacity(6) command. */
                Log(("URB: %*s: SCSI: READ_CAPACITY\n",
                     s_cchMaxMsg, pszMsg));
                break;
            case 0x28: /* Read(10) command. */
                Log(("URB: %*s: SCSI: READ10 RelAdr=%d FUA=%d DPO=%d LUN=%d LBA=%#RX32 Len=%#RX16 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg,
                     pb[1] & 1, !!(pb[1] & RT_BIT(3)), !!(pb[1] & RT_BIT(4)), pb[1] >> 5,
                     RT_MAKE_U32_FROM_U8(pb[5], pb[4], pb[3], pb[2]),
                     RT_MAKE_U16(pb[8], pb[7]), pb[9]));
                break;
            case 0xa8: /* Read(12) command. */
                Log(("URB: %*s: SCSI: READ12 RelAdr=%d FUA=%d DPO=%d LUN=%d LBA=%#RX32 Len=%#RX32 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg,
                     pb[1] & 1, !!(pb[1] & RT_BIT(3)), !!(pb[1] & RT_BIT(4)), pb[1] >> 5,
                     RT_MAKE_U32_FROM_U8(pb[5], pb[4], pb[3], pb[2]),
                     RT_MAKE_U32_FROM_U8(pb[9], pb[8], pb[7], pb[6]),
                     pb[11]));
                break;
            case 0x3e: /* Read Long command. */
                Log(("URB: %*s: SCSI: READ LONG RelAdr=%d Correct=%d LUN=%d LBA=%#RX16 ByteLen=%#RX16 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg,
                     pb[1] & 1, !!(pb[1] & RT_BIT(1)),  pb[1] >> 5,
                     RT_MAKE_U16(pb[3], pb[2]), RT_MAKE_U16(pb[6], pb[5]),
                     pb[11]));
                break;
            case 0x2a: /* Write(10) command. */
                Log(("URB: %*s: SCSI: WRITE10 RelAdr=%d EBP=%d FUA=%d DPO=%d LUN=%d LBA=%#RX32 Len=%#RX16 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg,
                     pb[1] & 1, !!(pb[1] & RT_BIT(2)), !!(pb[1] & RT_BIT(3)),
                     !!(pb[1] & RT_BIT(4)), pb[1] >> 5,
                     RT_MAKE_U32_FROM_U8(pb[5], pb[4], pb[3], pb[2]),
                     RT_MAKE_U16(pb[8], pb[7]), pb[9]));
                break;
            case 0xaa: /* Write(12) command. */
                Log(("URB: %*s: SCSI: WRITE12 RelAdr=%d EBP=%d FUA=%d DPO=%d LUN=%d LBA=%#RX32 Len=%#RX32 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg,
                     pb[1] & 1, !!(pb[1] & RT_BIT(3)), !!(pb[1] & RT_BIT(4)),
                     !!(pb[1] & RT_BIT(4)), pb[1] >> 5,
                     RT_MAKE_U32_FROM_U8(pb[5], pb[4], pb[3], pb[2]),
                     RT_MAKE_U32_FROM_U8(pb[9], pb[8], pb[7], pb[6]),
                     pb[11]));
                break;
            case 0x3f: /* Write Long command. */
                Log(("URB: %*s: SCSI: WRITE LONG RelAdr=%d LUN=%d LBA=%#RX16 ByteLen=%#RX16 Ctrl=%#RX8\n",
                     s_cchMaxMsg, pszMsg,
                     pb[1] & 1,  pb[1] >> 5,
                     RT_MAKE_U16(pb[3], pb[2]), RT_MAKE_U16(pb[6], pb[5]),
                     pb[11]));
                break;
            case 0x35: /* Synchronize Cache(10) command. */
                Log(("URB: %*s: SCSI: SYNCHRONIZE_CACHE10\n",
                     s_cchMaxMsg, pszMsg));
                break;
            case 0xa0: /* Report LUNs command. */
                Log(("URB: %*s: SCSI: REPORT_LUNS\n",
                     s_cchMaxMsg, pszMsg));
                break;
            default:
                Log(("URB: %*s: SCSI: cmd=%#x\n",
                     s_cchMaxMsg, pszMsg, pb[0]));
                break;
        }
        if (pDev)
            pDev->Urb.u8ScsiCmd = pb[0];
    }
    else if (   fComplete
             && pUrb->enmType == VUSBXFERTYPE_BULK
             && pUrb->enmDir  == VUSBDIRECTION_IN
             && pUrb->cbData >= 12
             && !memcmp(pUrb->abData, "USBS", 4))
    {
        const struct usbs
        {
            uint32_t    Signature;
            uint32_t    Tag;
            uint32_t    DataResidue;
            uint8_t     Status;
            uint8_t     CDB[3];
        } *pUsbS = (struct usbs *)pUrb->abData;
        static const char * const s_apszStatuses[] = { "PASSED", "FAILED", "PHASE ERROR", "RESERVED" };
        Log(("URB: %*s: SCSI: Tag=%#x DataResidue=%#RX32 Status=%#RX8 %s\n",
             s_cchMaxMsg, pszMsg, pUsbS->Tag, pUsbS->DataResidue, pUsbS->Status,
             s_apszStatuses[pUsbS->Status < RT_ELEMENTS(s_apszStatuses) ? pUsbS->Status : RT_ELEMENTS(s_apszStatuses) - 1]));
        if (pDev)
            pDev->Urb.u8ScsiCmd = 0xff;
    }
    else if (   fComplete
             && pUrb->enmType == VUSBXFERTYPE_BULK
             && pUrb->enmDir  == VUSBDIRECTION_IN
             && pDev
             && pDev->Urb.u8ScsiCmd != 0xff)
    {
        const uint8_t *pb = pUrb->abData;
        switch (pDev->Urb.u8ScsiCmd)
        {
            case 0x03: /* REQUEST_SENSE */
                Log(("URB: %*s: SCSI: RESPONSE: REQUEST_SENSE (%s)\n",
                     s_cchMaxMsg, pszMsg, pb[0] & 7 ? "scsi compliant" : "not scsi compliant"));
                Log(("URB: %*s: SCSI: ErrCd=%#RX8 (%s) Seg=%#RX8 Filemark=%d EOM=%d ILI=%d\n",
                     s_cchMaxMsg, pszMsg, pb[0] & 0x7f, GetScsiErrCd(pb[0] & 0x7f), pb[1],
                     pb[2] >> 7, !!(pb[2] & RT_BIT(6)), !!(pb[2] & RT_BIT(5))));
                Log(("URB: %*s: SCSI: SenseKey=%#x ASC=%#RX8 ASCQ=%#RX8 : %s\n",
                     s_cchMaxMsg, pszMsg, pb[2] & 0xf, pb[12], pb[13],
                     GetScsiKCQ(pb[2] & 0xf, pb[12], pb[13])));
                /** @todo more later */
                break;

            case 0x12: /* INQUIRY. */
            {
                unsigned cb = pb[4] + 5;
                Log(("URB: %*s: SCSI: RESPONSE: INQUIRY\n"
                     "URB: %*s: SCSI: PeripheralQualifier=%d PeripheralType=%#RX8 RMB=%d DevTypeMod=%#RX8\n",
                     s_cchMaxMsg, pszMsg, s_cchMaxMsg, pszMsg,
                     pb[0] >> 5, pb[0] & 0x1f, pb[1] >> 7, pb[1] & 0x7f));
                Log(("URB: %*s: SCSI: ISOVer=%d ECMAVer=%d ANSIVer=%d\n",
                     s_cchMaxMsg, pszMsg, pb[2] >> 6, (pb[2] >> 3) & 7, pb[2] & 7));
                Log(("URB: %*s: SCSI: AENC=%d TrmlOP=%d RespDataFmt=%d (%s) AddLen=%d\n",
                     s_cchMaxMsg, pszMsg, pb[3] >> 7, (pb[3] >> 6) & 1,
                     pb[3] & 0xf, pb[3] & 0xf ? "legacy" : "scsi", pb[4]));
                if (cb < 8)
                    break;
                Log(("URB: %*s: SCSI: RelAdr=%d WBus32=%d WBus16=%d Sync=%d Linked=%d CmdQue=%d SftRe=%d\n",
                     s_cchMaxMsg, pszMsg, pb[7] >> 7, !!(pb[7] >> 6), !!(pb[7] >> 5), !!(pb[7] >> 4),
                     !!(pb[7] >> 3), !!(pb[7] >> 1), pb[7] & 1));
                if (cb < 16)
                    break;
                Log(("URB: %*s: SCSI: VendorId=%.8s\n", s_cchMaxMsg, pszMsg, &pb[8]));
                if (cb < 32)
                    break;
                Log(("URB: %*s: SCSI: ProductId=%.16s\n", s_cchMaxMsg, pszMsg, &pb[16]));
                if (cb < 36)
                    break;
                Log(("URB: %*s: SCSI: ProdRevLvl=%.4s\n", s_cchMaxMsg, pszMsg, &pb[32]));
                if (cb > 36)
                    Log(("URB: %*s: SCSI: VendorSpecific=%.*s\n",
                         s_cchMaxMsg, pszMsg, RT_MIN(cb - 36, 20), &pb[36]));
                if (cb > 96)
                    Log(("URB: %*s: SCSI: VendorParam=%.*Rhxs\n",
                         s_cchMaxMsg, pszMsg, cb - 96, &pb[96]));
                break;
            }

            case 0x25: /* Read Capacity(6) command. */
                Log(("URB: %*s: SCSI: RESPONSE: READ_CAPACITY\n"
                     "URB: %*s: SCSI: LBA=%#RX32 BlockLen=%#RX32\n",
                     s_cchMaxMsg, pszMsg, s_cchMaxMsg, pszMsg,
                     RT_MAKE_U32_FROM_U8(pb[3], pb[2], pb[1], pb[0]),
                     RT_MAKE_U32_FROM_U8(pb[7], pb[6], pb[5], pb[4])));
                break;
        }

        pDev->Urb.u8ScsiCmd = 0xff;
    }

    /*
     * The Quickcam control pipe.
     */
    if (    pSetup
        &&  ((pSetup->bmRequestType >> 5) & 0x3) >= 2 /* vendor */
        &&  (fComplete || !(pSetup->bmRequestType >> 7))
        &&  pDev
        &&  pDev->pDescCache
        &&  pDev->pDescCache->pDevice
        &&  pDev->pDescCache->pDevice->idVendor == 0x046d
        &&  (   pDev->pDescCache->pDevice->idProduct == 0x8f6
             || pDev->pDescCache->pDevice->idProduct == 0x8f5
             || pDev->pDescCache->pDevice->idProduct == 0x8f0)
       )
    {
        pbData = (const uint8_t *)(pSetup + 1);
        cbData = pUrb->cbData - sizeof(*pSetup);

        if (    pSetup->bRequest == 0x04
            &&  pSetup->wIndex == 0
            &&  (cbData == 1 || cbData == 2))
        {
            /* the value */
            unsigned uVal = pbData[0];
            if (cbData > 1)
                uVal |= (unsigned)pbData[1] << 8;

            const char *pszReg = NULL;
            switch (pSetup->wValue)
            {
                case 0:         pszReg = "i2c init"; break;
                case 0x0423:    pszReg = "STV_REG23"; break;
                case 0x0509:    pszReg = "RED something"; break;
                case 0x050a:    pszReg = "GREEN something"; break;
                case 0x050b:    pszReg = "BLUE something"; break;
                case 0x143f:    pszReg = "COMMIT? INIT DONE?"; break;
                case 0x1440:    pszReg = "STV_ISO_ENABLE"; break;
                case 0x1442:    pszReg = uVal & (RT_BIT(7)|RT_BIT(5)) ? "BUTTON PRESSED" : "BUTTON" ; break;
                case 0x1443:    pszReg = "STV_SCAN_RATE"; break;
                case 0x1445:    pszReg = "LED?"; break;
                case 0x1500:    pszReg = "STV_REG00"; break;
                case 0x1501:    pszReg = "STV_REG01"; break;
                case 0x1502:    pszReg = "STV_REG02"; break;
                case 0x1503:    pszReg = "STV_REG03"; break;
                case 0x1504:    pszReg = "STV_REG04"; break;
                case 0x15c1:    pszReg = "STV_ISO_SIZE"; break;
                case 0x15c3:    pszReg = "STV_Y_CTRL"; break;
                case 0x1680:    pszReg = "STV_X_CTRL"; break;
                case 0xe00a:    pszReg = "ProductId"; break;
                default:        pszReg = "[no clue]";   break;
            }
            if (pszReg)
                Log(("URB: %*s: QUICKCAM: %s %#x (%d) %s '%s' (%#x)\n",
                     s_cchMaxMsg, pszMsg,
                     (pSetup->bmRequestType >> 7) ? "read" : "write", uVal, uVal, (pSetup->bmRequestType >> 7) ? "from" : "to",
                     pszReg, pSetup->wValue));
        }
        else if (cbData)
            Log(("URB: %*s: QUICKCAM: Unknown request: bRequest=%#x bmRequestType=%#x wValue=%#x wIndex=%#x: %.*Rhxs\n", s_cchMaxMsg, pszMsg,
                 pSetup->bRequest, pSetup->bmRequestType, pSetup->wValue, pSetup->wIndex, cbData, pbData));
        else
            Log(("URB: %*s: QUICKCAM: Unknown request: bRequest=%#x bmRequestType=%#x wValue=%#x wIndex=%#x: (no data)\n", s_cchMaxMsg, pszMsg,
                 pSetup->bRequest, pSetup->bmRequestType, pSetup->wValue, pSetup->wIndex));
    }

#if 1
    if (    cbData /** @todo Fix RTStrFormatV to communicate .* so formatter doesn't apply defaults when cbData=0. */
        && (fComplete
            ? pUrb->enmDir != VUSBDIRECTION_OUT
            : pUrb->enmDir == VUSBDIRECTION_OUT))
        Log3(("%16.*Rhxd\n", cbData, pbData));
#endif
    if (pUrb->enmType == VUSBXFERTYPE_MSG && pUrb->pVUsb && pUrb->pVUsb->pCtrlUrb)
        vusbUrbTrace(pUrb->pVUsb->pCtrlUrb, "NESTED MSG", fComplete);
}
#endif /* LOG_ENABLED */

