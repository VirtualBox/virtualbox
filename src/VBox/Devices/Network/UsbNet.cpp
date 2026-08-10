/* $Id: UsbNet.cpp 114919 2026-08-10 12:09:50Z alexander.eichner@oracle.com $ */
/** @file
 * UsbNet - USB NCM Ethernet Device Emulation.
 */

/*
 * Copyright (C) 2007-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP   LOG_GROUP_USB_NET
#include <VBox/vmm/pdmusb.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/scsi.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name USB Ethernet string IDs
 * @{ */
#define USBNET_STR_ID_MANUFACTURER  1
#define USBNET_STR_ID_PRODUCT       2
#define USBNET_STR_ID_MAC_ADDRESS   3
/** @} */

/** @name USB Ethernet vendor and product IDs
 * @{ */
#define VBOX_USB_VENDOR             0x80EE
#define USBNET_PID                  0x0040
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#pragma pack(1)
typedef struct USBCDCNOTIFICICATION
{
    uint8_t                         bmRequestType;
    uint8_t                         bNotificationCode;
    uint16_t                        wValue;
    uint16_t                        wIndex;
    uint16_t                        wLength;
} USBCDCNOTIFICICATION;
#pragma pack()
AssertCompileSize(USBCDCNOTIFICICATION, 8);

#define USB_CDC_NOTIFICATION_CODE_NETWORK_CONNECTION            0x00
#define USB_CDC_NOTIFICATION_CODE_CONNECTION_SPEED_CHANGE       0x2a


#pragma pack(1)
typedef struct USBCDCNOTIFICICATIONSPEEDCHG
{
    USBCDCNOTIFICICATION            Hdr;
    uint32_t                        DLBitRate;
    uint32_t                        ULBitRate;
} USBCDCNOTIFICICATIONSPEEDCHG;
#pragma pack()
AssertCompileSize(USBCDCNOTIFICICATIONSPEEDCHG, 8 + 2 * 4);


#pragma pack(1)
typedef struct USBCDCFUNCDESCHDR
{
    uint8_t                         bFunctionLength;
    uint8_t                         bDescriptorType;
    uint8_t                         bDescriptorSubtype;
} USBCDCFUNCDESCHDR;
#pragma pack()
AssertCompileSize(USBCDCFUNCDESCHDR, 3);

#define USB_CDC_DESCRIPTOR_TYPE_INTERFACE                       0x24
#define USB_CDC_DESCRIPTOR_TYPE_ENDPOINT                        0x25


#define USB_CDC_DESCRIPTOR_SUB_TYPE_HEADER                      0x00
#define USB_CDC_DESCRIPTOR_SUB_TYPE_CALL_MGMT                   0x01
#define USB_CDC_DESCRIPTOR_SUB_TYPE_ACM                         0x02
#define USB_CDC_DESCRIPTOR_SUB_TYPE_DLM                         0x03
#define USB_CDC_DESCRIPTOR_SUB_TYPE_PHONE_RINGER                0x04
#define USB_CDC_DESCRIPTOR_SUB_TYPE_PHONE_STATE_REPORTING       0x05
#define USB_CDC_DESCRIPTOR_SUB_TYPE_UNION                       0x06
#define USB_CDC_DESCRIPTOR_SUB_TYPE_COUNTRY_SELECTION           0x07
#define USB_CDC_DESCRIPTOR_SUB_TYPE_PHONE_OPERATING_MODES       0x08
#define USB_CDC_DESCRIPTOR_SUB_TYPE_USB_TERMINAL                0x09
#define USB_CDC_DESCRIPTOR_SUB_TYPE_NETWORK_CHANNEL_TERMINAL    0x0a
#define USB_CDC_DESCRIPTOR_SUB_TYPE_PROTOCOL_UNIT               0x0b
#define USB_CDC_DESCRIPTOR_SUB_TYPE_EXTENSION_UNIT              0x0c
#define USB_CDC_DESCRIPTOR_SUB_TYPE_MULTI_CHANNEL_MGMT          0x0d
#define USB_CDC_DESCRIPTOR_SUB_TYPE_CAPI_CONTROL_MGMT           0x0e
#define USB_CDC_DESCRIPTOR_SUB_TYPE_ETHERNET_NETWORKING         0x0f
#define USB_CDC_DESCRIPTOR_SUB_TYPE_ATM_NETEORKING              0x10
#define USB_CDC_DESCRIPTOR_SUB_TYPE_WIRELESS_HANDSET_CONTROL    0x11
#define USB_CDC_DESCRIPTOR_SUB_TYPE_MOBILE_DIRECT_LINE_MODEL    0x12
#define USB_CDC_DESCRIPTOR_SUB_TYPE_MDLM_DETAIL                 0x13
#define USB_CDC_DESCRIPTOR_SUB_TYPE_DEVICE_MGMT_MODEL           0x14
#define USB_CDC_DESCRIPTOR_SUB_TYPE_OBEX                        0x15
#define USB_CDC_DESCRIPTOR_SUB_TYPE_COMMAND_SET                 0x16
#define USB_CDC_DESCRIPTOR_SUB_TYPE_COMMAND_SET_DETAIL          0x17
#define USB_CDC_DESCRIPTOR_SUB_TYPE_PHONE_CONTROL_MODEL         0x18
#define USB_CDC_DESCRIPTOR_SUB_TYPE_OBEX_SERVICE_IDENTIFIER     0x19
#define USB_CDC_DESCRIPTOR_SUB_TYPE_NCM                         0x1a


#pragma pack(1)
typedef struct USBCDCHDRFUNCDESC
{
    USBCDCFUNCDESCHDR               Hdr;
    uint16_t                        bcdCDC;
} USBCDCHDRFUNCDESC;
#pragma pack()
AssertCompileSize(USBCDCHDRFUNCDESC, 5);


#pragma pack(1)
typedef struct USBCDCUNIONFUNCDESC
{
    USBCDCFUNCDESCHDR               Hdr;
    uint8_t                         bControlInterface;
    uint8_t                         bSubordinateInterface0;
} USBCDCUNIONFUNCDESC;
#pragma pack()
AssertCompileSize(USBCDCUNIONFUNCDESC, 5);


#pragma pack(1)
typedef struct USBCDCECMFUNCDESC
{
    USBCDCFUNCDESCHDR               Hdr;
    uint8_t                         iMACAddress;
    uint32_t                        bmEthernetStatistics;
    uint16_t                        wMaxSegmentSize;
    uint16_t                        wMaxNumberMCFilters;
    uint8_t                         bNumberPowerFilters;
} USBCDCECMFUNCDESC;
#pragma pack()
AssertCompileSize(USBCDCECMFUNCDESC, 13);


#pragma pack(1)
typedef struct USBCDCNCMFUNCDESC
{
    USBCDCFUNCDESCHDR               Hdr;
    uint16_t                        bcdNcmVersion;
    uint8_t                         bmNetworkCapabilities;
} USBCDCNCMFUNCDESC;
#pragma pack()
AssertCompileSize(USBCDCNCMFUNCDESC, 6);


#define USB_CDC_NCM_FUNC_DESC_CAP_F_PACKET_FILTER           RT_BIT(0)
#define USB_CDC_NCM_FUNC_DESC_CAP_F_NET_ADDR                RT_BIT(1)
#define USB_CDC_NCM_FUNC_DESC_CAP_F_ENCAPSULATED_CMD        RT_BIT(2)
#define USB_CDC_NCM_FUNC_DESC_CAP_F_MAX_DATAGRAM            RT_BIT(3)
#define USB_CDC_NCM_FUNC_DESC_CAP_F_CRC_MODE                RT_BIT(4)
#define USB_CDC_NCM_FUNC_DESC_CAP_F_NTB_INPUT_SIZE_8BYTE    RT_BIT(5)


/**
 * Our NCM interface class descriptor.
 */
#pragma pack(1)
typedef struct USBNCMFUNCDESC
{
    USBCDCHDRFUNCDESC   FuncHdr;
    USBCDCUNIONFUNCDESC Union;
    USBCDCECMFUNCDESC   Ecm;
    USBCDCNCMFUNCDESC   Ncm;
} USBNCMFUNCDESC;
#pragma pack()
AssertCompileSize(USBNCMFUNCDESC, sizeof(USBCDCHDRFUNCDESC) + sizeof(USBCDCUNIONFUNCDESC) + sizeof(USBCDCNCMFUNCDESC) + sizeof(USBCDCECMFUNCDESC));

#pragma pack(1)
typedef struct USBNCMNTBPARAMS
{
    uint16_t                        wLength;
    uint16_t                        bmNtbFormatsSupported;
    uint32_t                        dwNtbInMaxSize;
    uint16_t                        wNdpInDivisor;
    uint16_t                        wNdpInPayloadRemainder;
    uint16_t                        wNdpInAlignment;
    uint16_t                        u16Rsvd0;
    uint32_t                        dwNtbOutMaxSize;
    uint16_t                        wNdpOutDivisor;
    uint16_t                        wNdpOutPayloadRemainder;
    uint16_t                        wNdpOutAlignment;
    uint16_t                        wNtpOutMaxDatagrams;
} USBNCMNTBPARAMS;
#pragma pack()
AssertCompileSize(USBNCMNTBPARAMS, 28);

#define VUSB_REQ_GET_NTB_PARAMETERS 0x80


#pragma pack(1)
typedef struct USBNCMNTH16
{
    uint32_t                        dwSignature;
    uint16_t                        wHeaderLength;
    uint16_t                        wSequence;
    uint16_t                        wBlockLength;
    uint16_t                        wNdpIndex;
} USBNCMNTH16;
#pragma pack()
AssertCompileSize(USBNCMNTH16, 12);
typedef USBNCMNTH16 *PUSBNCMNTH16;
typedef const USBNCMNTH16 *PCUSBNCMNTH16;

#define USBNCMNTH16_SIGNATURE UINT32_C(0x484d434e)


typedef struct USBNCMNDP16DGRAM
{
    uint16_t                        wDatagramIndex;
    uint16_t                        wDatagramLength;
} USBNCMNDP16DGRAM;
AssertCompileSize(USBNCMNDP16DGRAM, 4);
typedef USBNCMNDP16DGRAM *PUSBNCMNDP16DGRAM;
typedef const USBNCMNDP16DGRAM *PCUSBNCMNDP16DGRAM;

#pragma pack(1)
typedef struct USBNCMNDP16
{
    uint32_t                        dwSignature;
    uint16_t                        wLength;
    uint16_t                        wNextNdpIndex;
    USBNCMNDP16DGRAM                DataGram0;
    USBNCMNDP16DGRAM                DataGram1;
    /* More pairs of wDatagramIndex/wDatagramLength can follow. */
} USBNCMNDP16;
#pragma pack()
AssertCompileSize(USBNCMNDP16, 16);
typedef USBNCMNDP16 *PUSBNCMNDP16;
typedef const USBNCMNDP16 *PCUSBNCMNDP16;

#define USBNCMNDP16_SIGNATURE_NCM0 UINT32_C(0x304d434e)
#define USBNCMNDP16_SIGNATURE_NCM1 UINT32_C(0x314d434e)


#pragma pack(1)
typedef struct USBNCMNTH32
{
    uint32_t                        dwSignature;
    uint16_t                        wHeaderLength;
    uint16_t                        wSequence;
    uint32_t                        dwBlockLength;
    uint32_t                        dwNdpIndex;
} USBNCMNTH32;
#pragma pack()
AssertCompileSize(USBNCMNTH32, 16);
typedef USBNCMNTH32 *PUSBNCMNTH32;
typedef const USBNCMNTH32 *PCUSBNCMNTH32;

#define USBNCMNTH32_SIGNATURE UINT32_C(0x686d636e)


#pragma pack(1)
typedef struct USBNCMNDP32
{
    uint32_t                        dwSignature;
    uint16_t                        wLength;
    uint16_t                        wReserved6;
    uint32_t                        dwNextNdpIndex;
    uint32_t                        dwReserved12;
    uint32_t                        dwDatagramIndex0;
    uint32_t                        dwDatagramLength0;
    uint32_t                        dwDatagramIndex1;
    uint32_t                        dwDatagramLength1;
    /* More pairs of dwDatagramIndex/dwDatagramLength can follow. */
} USBNCMNDP32;
#pragma pack()
AssertCompileSize(USBNCMNDP32, 32);
typedef USBNCMNDP32 *PUSBNCMNDP32;
typedef const USBNCMNDP32 *PCUSBNCMNDP32;

#define USBNCMNDP32_SIGNATURE_NCM0 UINT32_C(0x304d434e)
#define USBNCMNDP32_SIGNATURE_NCM1 UINT32_C(0x314d434e)


/**
 * Endpoint status data.
 */
typedef struct USBNETEP
{
    bool                fHalted;
} USBNETEP;
/** Pointer to the endpoint status. */
typedef USBNETEP *PUSBNETEP;


/**
 * A URB queue.
 */
typedef struct USBNETURBQUEUE
{
    /** The head pointer. */
    PVUSBURB            pHead;
    /** Where to insert the next entry. */
    PVUSBURB           *ppTail;
} USBNETURBQUEUE;
/** Pointer to a URB queue. */
typedef USBNETURBQUEUE *PUSBNETURBQUEUE;
/** Pointer to a const URB queue. */
typedef USBNETURBQUEUE const *PCUSBNETURBQUEUE;


/**
 * The USB Ethernet instance data.
 */
typedef struct USBNET
{
    /** Pointer back to the PDM USB Device instance structure. */
    PPDMUSBINS                          pUsbIns;
    /** Critical section protecting the device state. */
    RTCRITSECT                          CritSect;

    /** The current configuration.
     * (0 - default, 1 - the only, i.e configured.) */
    uint8_t                             bConfigurationValue;
    /** Current alternate setting. */
    uint8_t                             bAlternateSetting;
    /** NTH sequence number. */
    uint16_t                            idSequence;

    /** Endpoint 0 is the default control pipe, 1 is the host->dev bulk pipe and 2
     * is the dev->host one, and 3 is the interrupt dev -> host one. */
    USBNETEP                            aEps[4];

    /** The "hardware" MAC address. */
    RTMAC                               MacConfigured;
    /** The stringified MAC address. */
    char                                aszMac[13]; /* Includes zero temrinator. */

    /** USB descriptor strings. */
    PDMUSBDESCCACHESTRING               aUsbStringsEnUs[3];
    /** USB languages. */
    PDMUSBDESCCACHELANG                 UsbLang;
    /** The dynamically generated USB descriptor cache. */
    PDMUSBDESCCACHE                     UsbDescCache;

    /** If set the link is currently up. */
    bool                                fLinkUp;
    /** If set the link is temporarily down because of a saved state load. */
    bool                                fLinkTempDown;
    /** Link Up(/Restore) Timer. */
    TMTIMERHANDLE                       hTimerLinkUp;

    bool                                fInitialLinkStatusSent;
    bool                                fInitialSpeedChangeSent;

    /** Pending to-host queue.
     * The URBs waiting here are pending the completion of the current request and
     * data or status to become available.
     */
    USBNETURBQUEUE                      ToHostQueue;
    /** Pending to-host interrupt queue.
     * The URBs waiting here are pending the completion of the current request and
     * data or status to become available.
     */
    USBNETURBQUEUE                      ToHostIntrQueue;
    /** Done queue
     * The URBs stashed here are waiting to be reaped. */
    USBNETURBQUEUE                      DoneQueue;
    /** Signalled when adding an URB to the done queue and fHaveDoneQueueWaiter
     *  is set. */
    RTSEMEVENT                          hEvtDoneQueue;
    /** Signalled when adding an URB to the to host queue and fHaveToHostQueueWaiter
     *  is set. */
    RTSEMEVENT                          hEvtToHostQueue;
    /** Someone is waiting on the done queue. */
    bool                                fHaveDoneQueueWaiter;
    /** Someone is waiting on the to host queue. */
    volatile bool                       fHaveToHostQueueWaiter;

    /** VM is currently suspended and we should ignore all external requests. */
    volatile bool                       fSuspended;

    /** Whether to signal the reset semaphore when the current request completes. */
    bool                                fSignalResetSem;
    /** Semaphore usbNetUsbReset waits on when a request is executing at reset
     *  time.  Only signalled when fSignalResetSem is set. */
    RTSEMEVENTMULTI                     hEvtReset;
    /** The reset URB.
     * This is waiting for SCSI request completion before finishing the reset. */
    PVUSBURB                            pResetUrb;

    STAMCOUNTER                         StatReceiveBytes;
    STAMCOUNTER                         StatTransmitBytes;

    uint32_t                            u32PktNo;
    /**
     * LUN\#0 data.
     */
    struct
    {
        /** LUN\#0 + status LUN: The base interface. */
        PDMIBASE                            IBase;
        /** LUN\#0: The network port interface. */
        PDMINETWORKDOWN                     INetworkDown;
        /** LUN\#0: The network config port interface. */
        PDMINETWORKCONFIG                   INetworkConfig;

        /** Pointer to the connector of the attached network driver. */
        PPDMINETWORKUPR3                    pINetwork;
        /** Pointer to the attached network driver. */
        R3PTRTYPE(PPDMIBASE)                pIBase;
    } Lun0;

} USBNET;
/** Pointer to the USB Ethernet instance data. */
typedef USBNET *PUSBNET;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const USBNCMFUNCDESC g_UsbNetFuncDesc =
{
    {
        { 5, USB_CDC_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_DESCRIPTOR_SUB_TYPE_HEADER },
        0x0110
    },
    {
        { 5, USB_CDC_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_DESCRIPTOR_SUB_TYPE_UNION },
        0,
        1
    },
    {
        { 13, USB_CDC_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_DESCRIPTOR_SUB_TYPE_ETHERNET_NETWORKING },
        USBNET_STR_ID_MAC_ADDRESS,
        0,
        1514,
        0,
        0
    },
    {
        { 6, USB_CDC_DESCRIPTOR_TYPE_INTERFACE, USB_CDC_DESCRIPTOR_SUB_TYPE_NCM },
        0x0100,
        0
    }
};


static const VUSBDESCIAD g_UsbNetInterfaceIad =
{
    sizeof(VUSBDESCIAD), // bLength;
    VUSB_DT_INTERFACE_ASSOCIATION, // bDescriptorType;
    0, // bFirstInterface;
    2, // bInterfaceCount;
    2, // bFunctionClass;
    0x0d, // bFunctionSubClass;
    0, // bFunctionProtocol;
    0 // iFunction;
};


static const VUSBDESCENDPOINTEX g_aUsbNetEndpointDescsAlt1FS[3] =
{
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x81 /* ep=1, in */,
            /* .bmAttributes = */       2 /* bulk */,
            /* .wMaxPacketSize = */     64 /* maximum possible */,
            /* .bInterval = */          0 /* not applicable for bulk EP */
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        /* .pvSsepc = */    NULL,
        /* .cbSsepc = */    0
    },
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x02 /* ep=2, out */,
            /* .bmAttributes = */       2 /* bulk */,
            /* .wMaxPacketSize = */     64 /* maximum possible */,
            /* .bInterval = */          0 /* not applicable for bulk EP */
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        /* .pvSsepc = */    NULL,
        /* .cbSsepc = */    0
    },
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x83 /* ep=3, in */,
            /* .bmAttributes = */       3 /* interrupt */,
            /* .wMaxPacketSize = */     64,
            /* .bInterval = */          10,
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0
    },
};

static const VUSBDESCENDPOINTEX g_aUsbNetEndpointDescsAlt1HS[3] =
{
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x81 /* ep=1, in */,
            /* .bmAttributes = */       2 /* bulk */,
            /* .wMaxPacketSize = */     512 /* HS bulk packet size */,
            /* .bInterval = */          0 /* no NAKs */
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        /* .pvSsepc = */    NULL,
        /* .cbSsepc = */    0
    },
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x02 /* ep=2, out */,
            /* .bmAttributes = */       2 /* bulk */,
            /* .wMaxPacketSize = */     512 /* HS bulk packet size */,
            /* .bInterval = */          0 /* no NAKs */
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        /* .pvSsepc = */    NULL,
        /* .cbSsepc = */    0
    },
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x83 /* ep=3, in */,
            /* .bmAttributes = */       3 /* interrupt */,
            /* .wMaxPacketSize = */     64,
            /* .bInterval = */          10,
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0
    },
};

static const VUSBDESCSSEPCOMPANION g_aUsbNetEpCompanionSS =
{
    /* .bLength = */            sizeof(VUSBDESCSSEPCOMPANION),
    /* .bDescriptorType = */    VUSB_DT_SS_ENDPOINT_COMPANION,
    /* .bMaxBurst = */          15  /* we can burst all the way */,
    /* .bmAttributes = */       0   /* no streams */,
    /* .wBytesPerInterval = */  0   /* not a periodic endpoint */
};

static const VUSBDESCENDPOINTEX g_aUsbNetEndpointDescsAlt1SS[3] =
{
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x81 /* ep=1, in */,
            /* .bmAttributes = */       2 /* bulk */,
            /* .wMaxPacketSize = */     1024 /* SS bulk packet size */,
            /* .bInterval = */          0 /* no NAKs */
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        /* .pvSsepc = */    &g_aUsbNetEpCompanionSS,
        /* .cbSsepc = */    sizeof(g_aUsbNetEpCompanionSS)
    },
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x02 /* ep=2, out */,
            /* .bmAttributes = */       2 /* bulk */,
            /* .wMaxPacketSize = */     1024 /* SS bulk packet size */,
            /* .bInterval = */          0 /* no NAKs */
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        /* .pvSsepc = */    &g_aUsbNetEpCompanionSS,
        /* .cbSsepc = */    sizeof(g_aUsbNetEpCompanionSS)
    },
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x83 /* ep=3, in */,
            /* .bmAttributes = */       3 /* interrupt */,
            /* .wMaxPacketSize = */     64,
            /* .bInterval = */          10,
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        /* .pvSsepc = */    &g_aUsbNetEpCompanionSS,
        /* .cbSsepc = */    sizeof(g_aUsbNetEpCompanionSS)
    },
};


static const VUSBDESCINTERFACEEX g_aUsbNetInterfaceDescFS_0[] =
{
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       0,
            /* .bAlternateSetting = */      0,
            /* .bNumEndpoints = */          1,
            /* .bInterfaceClass = */        2    /* Communications Device */,
            /* .bInterfaceSubClass = */     0x0d /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     0    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */      NULL,
        /* .pvClass = */     &g_UsbNetFuncDesc,
        /* .cbClass = */     sizeof(g_UsbNetFuncDesc),
        /* .paEndpoints = */  &g_aUsbNetEndpointDescsAlt1FS[2],
        /* .pIAD = */        &g_UsbNetInterfaceIad,
        /* .cbIAD = */       sizeof(g_UsbNetInterfaceIad)
    }
};

static const VUSBDESCINTERFACEEX g_aUsbNetInterfaceDescFS_1[] =
{
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       1,
            /* .bAlternateSetting = */      0,
            /* .bNumEndpoints = */          0,
            /* .bInterfaceClass = */        0x0a /* Communications Device */,
            /* .bInterfaceSubClass = */     0    /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     0    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        NULL,
        /* .pIAD = */ NULL,
        /* .cbIAD = */ 0
    },
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       1,
            /* .bAlternateSetting = */      1,
            /* .bNumEndpoints = */          2,
            /* .bInterfaceClass = */        0x0a /* Communications Device */,
            /* .bInterfaceSubClass = */     0    /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     1    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        &g_aUsbNetEndpointDescsAlt1FS[0],
        /* .pIAD = */        NULL, //&g_UsbNetInterfaceIad,
        /* .cbIAD = */       0, //sizeof(g_UsbNetInterfaceIad)
    }
};


static const VUSBDESCINTERFACEEX g_aUsbNetInterfaceDescHS_0[] =
{
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       0,
            /* .bAlternateSetting = */      0,
            /* .bNumEndpoints = */          1,
            /* .bInterfaceClass = */        2    /* Communications Device */,
            /* .bInterfaceSubClass = */     0x0d /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     0    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */      NULL,
        /* .pvClass = */     &g_UsbNetFuncDesc,
        /* .cbClass = */     sizeof(g_UsbNetFuncDesc),
        /* .paEndpoints = */  &g_aUsbNetEndpointDescsAlt1HS[2],
        /* .pIAD = */        &g_UsbNetInterfaceIad,
        /* .cbIAD = */       sizeof(g_UsbNetInterfaceIad)
    }
};

static const VUSBDESCINTERFACEEX g_aUsbNetInterfaceDescHS_1[] =
{
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       1,
            /* .bAlternateSetting = */      0,
            /* .bNumEndpoints = */          0,
            /* .bInterfaceClass = */        0x0a /* Communications Device */,
            /* .bInterfaceSubClass = */     0    /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     0    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        NULL,
        /* .pIAD = */ NULL,
        /* .cbIAD = */ 0
    },
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       1,
            /* .bAlternateSetting = */      1,
            /* .bNumEndpoints = */          2,
            /* .bInterfaceClass = */        0x0a /* Communications Device */,
            /* .bInterfaceSubClass = */     0    /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     1    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        &g_aUsbNetEndpointDescsAlt1HS[0],
        /* .pIAD = */        NULL, //&g_UsbNetInterfaceIad,
        /* .cbIAD = */       0, //sizeof(g_UsbNetInterfaceIad)
    }
};


static const VUSBDESCINTERFACEEX g_aUsbNetInterfaceDescSS_0[] =
{
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       0,
            /* .bAlternateSetting = */      0,
            /* .bNumEndpoints = */          1,
            /* .bInterfaceClass = */        2    /* Communications Device */,
            /* .bInterfaceSubClass = */     0x0d /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     0    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */      NULL,
        /* .pvClass = */     &g_UsbNetFuncDesc,
        /* .cbClass = */     sizeof(g_UsbNetFuncDesc),
        /* .paEndpoints = */  &g_aUsbNetEndpointDescsAlt1SS[2],
        /* .pIAD = */        &g_UsbNetInterfaceIad,
        /* .cbIAD = */       sizeof(g_UsbNetInterfaceIad)
    }
};

static const VUSBDESCINTERFACEEX g_aUsbNetInterfaceDescSS_1[] =
{
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       1,
            /* .bAlternateSetting = */      0,
            /* .bNumEndpoints = */          0,
            /* .bInterfaceClass = */        0x0a /* Communications Device */,
            /* .bInterfaceSubClass = */     0    /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     0    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        NULL,
        /* .pIAD = */ NULL,
        /* .cbIAD = */ 0
    },
    {
        {
            /* .bLength = */                sizeof(VUSBDESCINTERFACE),
            /* .bDescriptorType = */        VUSB_DT_INTERFACE,
            /* .bInterfaceNumber = */       1,
            /* .bAlternateSetting = */      1,
            /* .bNumEndpoints = */          2,
            /* .bInterfaceClass = */        0x0a /* Communications Device */,
            /* .bInterfaceSubClass = */     0    /* Communications Device Subclass / NCM */,
            /* .bInterfaceProtocol = */     1    /* No encapsulated commands / responses */,
            /* .iInterface = */             0
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0,
        &g_aUsbNetEndpointDescsAlt1SS[0],
        /* .pIAD = */        NULL, //&g_UsbNetInterfaceIad,
        /* .cbIAD = */       0, //sizeof(g_UsbNetInterfaceIad)
    }
};

static const VUSBINTERFACE g_aUsbNetInterfacesFS[] =
{
    { g_aUsbNetInterfaceDescFS_0, /* .cSettings = */ RT_ELEMENTS(g_aUsbNetInterfaceDescFS_0) },
    { g_aUsbNetInterfaceDescFS_1, /* .cSettings = */ RT_ELEMENTS(g_aUsbNetInterfaceDescFS_1) },
};

static const VUSBINTERFACE g_aUsbNetInterfacesHS[] =
{
    { g_aUsbNetInterfaceDescHS_0, /* .cSettings = */ RT_ELEMENTS(g_aUsbNetInterfaceDescHS_0) },
    { g_aUsbNetInterfaceDescHS_1, /* .cSettings = */ RT_ELEMENTS(g_aUsbNetInterfaceDescHS_1) },
};

static const VUSBINTERFACE g_aUsbNetInterfacesSS[] =
{
    { g_aUsbNetInterfaceDescSS_0, /* .cSettings = */ RT_ELEMENTS(g_aUsbNetInterfaceDescSS_0) },
    { g_aUsbNetInterfaceDescSS_1, /* .cSettings = */ RT_ELEMENTS(g_aUsbNetInterfaceDescSS_1) },
};

static const VUSBDESCCONFIGEX g_UsbNetConfigDescFS =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbNetInterfacesFS),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbNetInterfacesFS[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCCONFIGEX g_UsbNetConfigDescHS =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbNetInterfacesHS),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbNetInterfacesHS[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCCONFIGEX g_UsbNetConfigDescSS =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     2,
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbNetInterfacesSS[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCDEVICE g_UsbNetDeviceDesc20 =
{
    /* .bLength = */                sizeof(g_UsbNetDeviceDesc20),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x200, /* USB 2.0 */
    /* .bDeviceClass = */           2 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        64,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBNET_PID,
    /* .bcdDevice = */              0x0100, /* 1.0 */
    /* .iManufacturer = */          USBNET_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBNET_STR_ID_PRODUCT,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDESCDEVICE g_UsbNetDeviceDesc30 =
{
    /* .bLength = */                sizeof(g_UsbNetDeviceDesc30),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x300, /* USB 2.0 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        9 /* 512, the only option for USB3. */,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBNET_PID,
    /* .bcdDevice = */              0x0110, /* 1.10 */
    /* .iManufacturer = */          USBNET_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBNET_STR_ID_PRODUCT,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDEVICEQUALIFIER g_UsbNetDeviceQualifier =
{
    /* .bLength = */                sizeof(g_UsbNetDeviceQualifier),
    /* .bDescriptorType = */        VUSB_DT_DEVICE_QUALIFIER,
    /* .bcdUsb = */                 0x200, /* USB 2.0 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        64,
    /* .bNumConfigurations = */     1,
    /* .bReserved = */              0
};

static const struct {
    VUSBDESCBOS         bos;
    VUSBDESCSSDEVCAP    sscap;
} g_UsbNetBOS =
{
    {
        /* .bLength = */                sizeof(g_UsbNetBOS.bos),
        /* .bDescriptorType = */        VUSB_DT_BOS,
        /* .wTotalLength = */           sizeof(g_UsbNetBOS),
        /* .bNumDeviceCaps = */         1
    },
    {
        /* .bLength = */                sizeof(VUSBDESCSSDEVCAP),
        /* .bDescriptorType = */        VUSB_DT_DEVICE_CAPABILITY,
        /* .bDevCapabilityType = */     VUSB_DCT_SUPERSPEED_USB,
        /* .bmAttributes = */           0   /* No LTM. */,
        /* .wSpeedsSupported = */       0xe /* Any speed is good. */,
        /* .bFunctionalitySupport = */  2   /* Want HS at least. */,
        /* .bU1DevExitLat = */          0,  /* We are blazingly fast. */
        /* .wU2DevExitLat = */          0
    }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  usbNetHandleBulkDevToHost(PUSBNET pThis, PUSBNETEP pEp, PVUSBURB pUrb);


/**
 * Initializes an URB queue.
 *
 * @param   pQueue              The URB queue.
 */
static void usbNetQueueInit(PUSBNETURBQUEUE pQueue)
{
    pQueue->pHead = NULL;
    pQueue->ppTail = &pQueue->pHead;
}



/**
 * Inserts an URB at the end of the queue.
 *
 * @param   pQueue              The URB queue.
 * @param   pUrb                The URB to insert.
 */
DECLINLINE(void) usbNetQueueAddTail(PUSBNETURBQUEUE pQueue, PVUSBURB pUrb)
{
    pUrb->Dev.pNext = NULL;
    *pQueue->ppTail = pUrb;
    pQueue->ppTail  = &pUrb->Dev.pNext;
}


/**
 * Unlinks the head of the queue and returns it.
 *
 * @returns The head entry.
 * @param   pQueue              The URB queue.
 */
DECLINLINE(PVUSBURB) usbNetQueueRemoveHead(PUSBNETURBQUEUE pQueue)
{
    PVUSBURB pUrb = pQueue->pHead;
    if (pUrb)
    {
        PVUSBURB pNext = pUrb->Dev.pNext;
        pQueue->pHead = pNext;
        if (!pNext)
            pQueue->ppTail = &pQueue->pHead;
        else
            pUrb->Dev.pNext = NULL;
    }
    return pUrb;
}


/**
 * Removes an URB from anywhere in the queue.
 *
 * @returns true if found, false if not.
 * @param   pQueue              The URB queue.
 * @param   pUrb                The URB to remove.
 */
DECLINLINE(bool) usbNetQueueRemove(PUSBNETURBQUEUE pQueue, PVUSBURB pUrb)
{
    PVUSBURB pCur = pQueue->pHead;
    if (pCur == pUrb)
        pQueue->pHead = pUrb->Dev.pNext;
    else
    {
        while (pCur)
        {
            if (pCur->Dev.pNext == pUrb)
            {
                pCur->Dev.pNext = pUrb->Dev.pNext;
                break;
            }
            pCur = pCur->Dev.pNext;
        }
        if (!pCur)
            return false;
    }
    if (!pUrb->Dev.pNext)
        pQueue->ppTail = &pQueue->pHead;
    return true;
}


/**
 * Checks if the queue is empty or not.
 *
 * @returns true if it is, false if it isn't.
 * @param   pQueue              The URB queue.
 */
DECLINLINE(bool) usbNetQueueIsEmpty(PCUSBNETURBQUEUE pQueue)
{
    return pQueue->pHead == NULL;
}


/**
 * Links an URB into the done queue.
 *
 * @param   pThis               The USBNET instance.
 * @param   pUrb                The URB.
 */
static void usbNetLinkDone(PUSBNET pThis, PVUSBURB pUrb)
{
    usbNetQueueAddTail(&pThis->DoneQueue, pUrb);

    if (pThis->fHaveDoneQueueWaiter)
    {
        int rc = RTSemEventSignal(pThis->hEvtDoneQueue);
        AssertRC(rc);
    }
}


/**
 * Completes the URB with a stalled state, halting the pipe.
 */
static int usbNetCompleteStall(PUSBNET pThis, PUSBNETEP pEp, PVUSBURB pUrb, const char *pszWhy)
{
    RT_NOREF(pszWhy);
    LogFunc(("/#%u/ pUrb=%p:%s: %s\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc, pszWhy));

    pUrb->enmStatus = VUSBSTATUS_STALL;

    /** @todo figure out if the stall is global or pipe-specific or both. */
    if (pEp)
        pEp->fHalted = true;
    else
    {
        pThis->aEps[1].fHalted = true;
        pThis->aEps[2].fHalted = true;
        pThis->aEps[3].fHalted = true;
    }

    usbNetLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}


/**
 * Completes the URB with a OK state.
 */
static int usbNetCompleteOk(PUSBNET pThis, PVUSBURB pUrb, size_t cbData)
{
    LogFlowFunc(("/#%u/ pUrb=%p:%s cbData=%#zx\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc, cbData));

    pUrb->enmStatus = VUSBSTATUS_OK;
    pUrb->cbData    = (uint32_t)cbData;

    usbNetLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}


/**
 * Completes the URB after device successfully processed it. Optionally copies data
 * into the URB. May still generate an error if the URB is not big enough.
 */
static void usbNetCompleteNotificationOk(PUSBNET pThis, PVUSBURB pUrb, const void *pSrc, size_t cbSrc)
{
    LogFlowFunc(("/#%u/ pUrb=%p:%s (cbData=%#x) cbSrc=%#zx\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc, pUrb->cbData, cbSrc));

    pUrb->enmStatus = VUSBSTATUS_OK;
    if (pSrc)   /* Can be NULL if not copying anything. */
    {
        Assert(cbSrc);
        uint8_t *pDst = pUrb->pbData;

        /* Returned data is written after the setup message in control URBs. */
        Assert (pUrb->enmType == VUSBXFERTYPE_INTR);

        /* There is at least one byte of room in the URB. */
        size_t cbCopy = RT_MIN(pUrb->cbData, cbSrc);
        memcpy(pDst, pSrc, cbCopy);
        pUrb->cbData = (uint32_t)cbCopy;
        Log2Func(("/#%u/ Copied %zu bytes to pUrb->pbData, source had %zu bytes\n", pThis->pUsbIns->iInstance, cbCopy, cbSrc));

        /*
         * Need to check length differences. If cbSrc is less than what
         * the URB has space for, it'll be resolved as a short packet. But
         * if cbSrc is bigger, there is a real problem and the host needs
         * to see an overrun/babble error.
         */
        if (RT_UNLIKELY(cbSrc > cbCopy))
            pUrb->enmStatus = VUSBSTATUS_DATA_OVERRUN;
    }
    else
        Assert(cbSrc == 0); /* Make up your mind, caller! */

    usbNetLinkDone(pThis, pUrb);
}


/**
 * Reset worker for usbNetUsbReset, usbNetUsbSetConfiguration and
 * usbNetUrbHandleDefaultPipe.
 *
 * @returns VBox status code.
 * @param   pThis               The MSD instance.
 * @param   pUrb                Set when usbNetUrbHandleDefaultPipe is the
 *                              caller.
 * @param   fSetConfig          Set when usbNetUsbSetConfiguration is the
 *                              caller.
 */
static int usbNetResetWorker(PUSBNET pThis, PVUSBURB pUrb, bool fSetConfig)
{
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aEps); i++)
        pThis->aEps[i].fHalted = false;

    if (!pUrb && !fSetConfig) /* (only device reset) */
        pThis->bConfigurationValue = 0; /* default */

    pThis->idSequence = 0;

    /*
     * Ditch all pending URBs.
     */
    PVUSBURB pCurUrb;
    while ((pCurUrb = usbNetQueueRemoveHead(&pThis->ToHostQueue)) != NULL)
    {
        pCurUrb->enmStatus = VUSBSTATUS_CRC;
        usbNetLinkDone(pThis, pCurUrb);
    }

    while ((pCurUrb = usbNetQueueRemoveHead(&pThis->ToHostIntrQueue)) != NULL)
    {
        pCurUrb->enmStatus = VUSBSTATUS_CRC;
        usbNetLinkDone(pThis, pCurUrb);
    }

    pCurUrb = pThis->pResetUrb;
    if (pCurUrb)
    {
        pThis->pResetUrb = NULL;
        pCurUrb->enmStatus  = VUSBSTATUS_CRC;
        usbNetLinkDone(pThis, pCurUrb);
    }

    if (pUrb)
        return usbNetCompleteOk(pThis, pUrb, 0);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) usbNetLun0QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PUSBNET pThis = RT_FROM_MEMBER(pInterface, USBNET, Lun0.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->Lun0.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKCONFIG, &pThis->Lun0.INetworkConfig);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKDOWN, &pThis->Lun0.INetworkDown);
    return NULL;
}


static DECLCALLBACK(int) usbNetNetworkDown_WaitReceiveAvail(PPDMINETWORKDOWN pInterface, RTMSINTERVAL cMillies)
{
    PUSBNET pThis = RT_FROM_MEMBER(pInterface, USBNET, Lun0.INetworkDown);
    int rc = VINF_SUCCESS;
    LogFlowFunc(("/#%u/ pInterface=%p cMillies=%u\n", pThis->pUsbIns->iInstance, pInterface, cMillies));

    RTCritSectEnter(&pThis->CritSect);
    pThis->fHaveToHostQueueWaiter = usbNetQueueIsEmpty(&pThis->ToHostQueue) && !pThis->fSuspended;
    RTCritSectLeave(&pThis->CritSect);
    /*
    * It is ok to read fHaveToHostQueueWaiter outside of the critical section because:
    * 1) There is only one receive thread that calls this function;
    * 2) This is the only function that modifies fHaveToHostQueueWaiter.
    */
    while (ASMAtomicReadBool(&pThis->fHaveToHostQueueWaiter))
    {
        rc = RTSemEventWait(pThis->hEvtToHostQueue, cMillies);

        RTCritSectEnter(&pThis->CritSect);
        pThis->fHaveToHostQueueWaiter = usbNetQueueIsEmpty(&pThis->ToHostQueue) && !pThis->fSuspended;
        RTCritSectLeave(&pThis->CritSect);
        Log6Func(("/#%u/ pInterface=%p received hEvtToHostQueue, %s need to wait\n",
                  pThis->pUsbIns->iInstance, pInterface, pThis->fHaveToHostQueueWaiter ? "still" : "no"));
    }
    LogFlowFunc(("/#%u/ pInterface=%p returned %u\n", pThis->pUsbIns->iInstance, pInterface, rc));
    return rc;
}

#ifdef LOG_ENABLED

DECLINLINE(void) usbNetProtocolDump(const uint8_t *cpPacket, uint32_t cb)
{
    RT_NOREF(cb);
    uint8_t  protocol;
    uint16_t offset = sizeof(RTNETETHERHDR);
    uint16_t uEthType = RT_N2H_U16(*(uint16_t*)(cpPacket+12));
    if (uEthType == RTNET_ETHERTYPE_VLAN)
    {
        uEthType = RT_N2H_U16(*(uint16_t*)(cpPacket+16));
        offset += 4;
    }
    if (uEthType == RTNET_ETHERTYPE_ARP)
    {
        Log4(("\tARP\n"));
        return;
    }
    else if (uEthType == RTNET_ETHERTYPE_IPV4)
    {
        PRTNETIPV4 pHdr = (PRTNETIPV4)(cpPacket + offset);
        offset += pHdr->ip_hl * 4;
        protocol = pHdr->ip_p;
        Log4(("\tIPv4: %RTnaipv4 => %RTnaipv4\n", pHdr->ip_src, pHdr->ip_dst));
    }
    else if (uEthType == RTNET_ETHERTYPE_IPV6)
    {
        PRTNETIPV6 pHdr = (PRTNETIPV6)(cpPacket + offset);
        protocol = pHdr->ip6_nxt;
        offset += sizeof(RTNETIPV6);
        if (protocol == 0 || protocol == 60)
        {
            /* Skip hop-by-hop and destination options */
            while (cpPacket[offset] == 0 || cpPacket[offset] == 60)
                offset += 8 * (1 + cpPacket[offset+1]);
            protocol = cpPacket[offset];
            offset += 8 * (1 + cpPacket[offset+1]);
        }
        Log4(("\tIPv6: %RTnaipv6 => %RTnaipv6\n", pHdr->ip6_src, pHdr->ip6_dst));
    }
    else
    {
        Log4(("\tEthernet: 0x%04x\n", uEthType));
        return;
    }
    switch (protocol)
    {
        case 1: Log4(("\tICMP\n")); break;
        case 2: Log4(("\tIGMP\n")); break;
        case 58: Log4(("\tICMPv6\n")); break;

        case 6:
            {
                PRTNETTCP pTcp = (PRTNETTCP)(cpPacket + offset);
                Log4(("\tTCP: %u => %u, seq=%u ack=%u\n", RT_N2H_U16(pTcp->th_sport), RT_N2H_U16(pTcp->th_dport),
                      RT_N2H_U32(pTcp->th_seq), RT_N2H_U32(pTcp->th_ack)));
            }
            break;
        case 17:
            {
                PRTNETUDP pUdp = (PRTNETUDP)(cpPacket + offset);
                Log4(("\tUDP: %u => %u\n", RT_N2H_U16(pUdp->uh_sport), RT_N2H_U16(pUdp->uh_dport)));
            }
            break;
        default:
            Log4(("\tUnknown (%u)", protocol));
            break;
    }
}

DECLINLINE(void) usbNetPacketDump(PUSBNET pThis, const void *pvBuf, size_t cb, const char *pcszText)
{
    const uint8_t *cpPacket = (const uint8_t *)pvBuf;
    if (cb < 14)
        Log3(("/#%u/ %s packet #%d is too small (%u bytes):\n%.*Rhxd\n",
              pThis->pUsbIns->iInstance, pcszText, ++pThis->u32PktNo, (uint32_t)cb, (uint32_t)cb, cpPacket));
    else
    {
        Log3(("/#%u/ %s packet #%d, %u bytes, %RTmac => %RTmac\n",
              pThis->pUsbIns->iInstance, pcszText, ++pThis->u32PktNo, (uint32_t)cb, cpPacket+6, cpPacket));
        if (LogIs4Enabled())
            usbNetProtocolDump(cpPacket, (uint32_t)cb);
        Log5(("%.*Rhxd\n", (uint32_t)cb, cpPacket));
    }
}

#else
#define usbNetPacketDump(pUsbIns, pvBuf, cb, pcszText)
#endif

/**
 * Receive data and pass it to lwIP for processing.
 *
 * @returns VBox status code
 * @param   pInterface  PDM network port interface pointer.
 * @param   pvBuf       Pointer to frame data.
 * @param   cb          Frame size.
 */
static DECLCALLBACK(int) usbNetNetworkDown_Receive(PPDMINETWORKDOWN pInterface, const void *pvBuf, size_t cb)
{
    PUSBNET pThis = RT_FROM_MEMBER(pInterface, USBNET, Lun0.INetworkDown);

    LogFlowFunc(("/#%u/ pInterface=%p cb=%u\n", pThis->pUsbIns->iInstance, pInterface, cb));
    RTCritSectEnter(&pThis->CritSect);

    if (usbNetQueueIsEmpty(&pThis->ToHostQueue) || pThis->fSuspended)
    {
        RTCritSectLeave(&pThis->CritSect);
        LogFlowFunc(("/#%u/ return %Rrc due to %s\n", pThis->pUsbIns->iInstance, VINF_SUCCESS, pThis->fSuspended ? "being suspended" : "empty host queue"));
        return VINF_SUCCESS;
    }

    PVUSBURB pUrb = usbNetQueueRemoveHead(&pThis->ToHostQueue);
    PUSBNETEP pEp = &pThis->aEps[2];

    if (RT_UNLIKELY(pEp->fHalted))
    {
        usbNetCompleteStall(pThis, NULL, pUrb, "Halted pipe");
        RTCritSectLeave(&pThis->CritSect);
        return VINF_SUCCESS;
    }

    if (pUrb->cbData < sizeof(USBNCMNTH16) + sizeof(USBNCMNDP16) + cb)
    {
        LogFunc(("/#%u/ Receive URB too small (%#x vs %#x)\n", pThis->pUsbIns->iInstance, pUrb->cbData,
             sizeof(USBNCMNTH16) + sizeof(USBNCMNDP16) + cb));
        pUrb->enmStatus = VUSBSTATUS_DATA_OVERRUN;
        usbNetLinkDone(pThis, pUrb);
        RTCritSectLeave(&pThis->CritSect);
        return VINF_SUCCESS;
    }

    usbNetPacketDump(pThis, pvBuf, cb, "<-- Rx");

    PUSBNCMNTH16 pNth16 = (PUSBNCMNTH16)&pUrb->pbData[0];
    PUSBNCMNDP16 pNdp16 = (PUSBNCMNDP16)(pNth16 + 1);

    /* Build NTH16. */
    pNth16->dwSignature   = USBNCMNTH16_SIGNATURE;
    pNth16->wHeaderLength = sizeof(*pNth16);
    pNth16->wSequence     = pThis->idSequence++;
    pNth16->wBlockLength  = (uint16_t)(sizeof(*pNth16) + sizeof(*pNdp16) + cb);
    pNth16->wNdpIndex     = sizeof(*pNth16);

    /* Build NDP16. */
    pNdp16->dwSignature   = USBNCMNDP16_SIGNATURE_NCM0;
    pNdp16->wLength       = sizeof(*pNdp16);
    pNdp16->wNextNdpIndex = 0;
    pNdp16->DataGram0.wDatagramIndex  = sizeof(*pNth16) + sizeof(*pNdp16);
    pNdp16->DataGram0.wDatagramLength = (uint16_t)cb;
    pNdp16->DataGram1.wDatagramIndex  = 0;
    pNdp16->DataGram1.wDatagramLength = 0;

    /* Copy frame over. */
    memcpy(pNdp16 + 1, pvBuf, cb);

    pUrb->cbData = (uint32_t)(sizeof(*pNth16) + sizeof(*pNdp16) + cb);
    usbNetLinkDone(pThis, pUrb);
    STAM_REL_COUNTER_ADD(&pThis->StatReceiveBytes, cb);
    RTCritSectLeave(&pThis->CritSect);

    LogFlowFunc(("/#%u/ return %Rrc\n", pThis->pUsbIns->iInstance, VINF_SUCCESS));
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnXmitPending}
 */
static DECLCALLBACK(void) usbNetNetworkDown_XmitPending(PPDMINETWORKDOWN pInterface)
{
    RT_NOREF(pInterface);
#ifdef LOG_ENABLED
    PUSBNET pThis = RT_FROM_MEMBER(pInterface, USBNET, Lun0.INetworkDown);
    LogFlowFunc(("/#%u/ pInterface=%p\n", pThis->pUsbIns->iInstance, pInterface));
#endif
}


/* -=-=-=-=-=- USBNET::INetworkConfig -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetMac}
 */
static DECLCALLBACK(int) usbNetGetMac(PPDMINETWORKCONFIG pInterface, PRTMAC pMac)
{
    PUSBNET pThis = RT_FROM_MEMBER(pInterface, USBNET, Lun0.INetworkConfig);

    LogFlowFunc(("/#%u/\n", pThis->pUsbIns->iInstance));
    memcpy(pMac, &pThis->MacConfigured, sizeof(*pMac));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetLinkState}
 */
static DECLCALLBACK(PDMNETWORKLINKSTATE) usbNetGetLinkState(PPDMINETWORKCONFIG pInterface)
{
    PUSBNET pThis = RT_FROM_MEMBER(pInterface, USBNET, Lun0.INetworkConfig);

    LogFlowFunc(("/#%u/\n", pThis->pUsbIns->iInstance));
    if (pThis->fLinkUp && !pThis->fLinkTempDown)
        return PDMNETWORKLINKSTATE_UP;
    if (!pThis->fLinkUp)
        return PDMNETWORKLINKSTATE_DOWN;
    if (pThis->fLinkTempDown)
        return PDMNETWORKLINKSTATE_DOWN_RESUME;
    AssertMsgFailed(("Invalid link state!\n"));
    return PDMNETWORKLINKSTATE_INVALID;
}


DECLHIDDEN(void) usbNetLinkStateNotify(PUSBNET pThis, PDMNETWORKLINKSTATE enmLinkState);

/**
 * @callback_method_impl{FNTMTIMERUSB}
 *
 * By the time the host resumes after sleep the network environment may have changed considerably.
 * We handle this by temporarily lowering the link state. This callback brings the link back up.
 */
static DECLCALLBACK(void) usbNetTimerLinkUp(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PUSBNET pThis = (PUSBNET)pvUser;
    RT_NOREF(pUsbIns, hTimer);

    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));

    /** @todo Do we really care for potential races with link state? */
    pThis->fLinkTempDown = false;
    if (!pThis->fLinkUp)
        usbNetLinkStateNotify(pThis, PDMNETWORKLINKSTATE_UP);
}


/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnSetLinkState}
 */
static DECLCALLBACK(int) usbNetSetLinkState(PPDMINETWORKCONFIG pInterface, PDMNETWORKLINKSTATE enmState)
{
    PUSBNET pThis = RT_FROM_MEMBER(pInterface, USBNET, Lun0.INetworkConfig);

    bool fLinkUp = enmState == PDMNETWORKLINKSTATE_UP;

    LogFlowFunc(("/#%u/ enmState=%d\n", pThis->pUsbIns->iInstance, enmState));
    AssertMsgReturn(enmState > PDMNETWORKLINKSTATE_INVALID && enmState <= PDMNETWORKLINKSTATE_DOWN_RESUME,
                    ("Invalid link state: enmState=%d\n", enmState), VERR_INVALID_PARAMETER);

    if (enmState == PDMNETWORKLINKSTATE_DOWN_RESUME)
    {
        pThis->fLinkTempDown = true;
        /* Do not bother to notify anyone if the link has been down */
        PDMUsbHlpTimerSetMillies(pThis->pUsbIns, pThis->hTimerLinkUp, 500);    /* 0.5 sec */
    }
    /* has the state changed? */
    if (pThis->fLinkUp != fLinkUp)
    {
        pThis->fLinkUp = fLinkUp;
        usbNetLinkStateNotify(pThis, enmState);
    }
    return VINF_SUCCESS;
}



/**
 * @interface_method_impl{PDMUSBREG,pfnUrbReap}
 */
static DECLCALLBACK(PVUSBURB) usbNetUrbReap(PPDMUSBINS pUsbIns, RTMSINTERVAL cMillies)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/ cMillies=%u\n", pUsbIns->iInstance, cMillies));

    RTCritSectEnter(&pThis->CritSect);

    PVUSBURB pUrb = usbNetQueueRemoveHead(&pThis->DoneQueue);
    if (!pUrb && cMillies)
    {
        /* Wait */
        pThis->fHaveDoneQueueWaiter = true;
        RTCritSectLeave(&pThis->CritSect);

        RTSemEventWait(pThis->hEvtDoneQueue, cMillies);

        RTCritSectEnter(&pThis->CritSect);
        pThis->fHaveDoneQueueWaiter = false;

        pUrb = usbNetQueueRemoveHead(&pThis->DoneQueue);
    }

    RTCritSectLeave(&pThis->CritSect);

    if (pUrb)
        Log2Func(("/#%u/ pUrb=%p:%s\n", pUsbIns->iInstance, pUrb, pUrb->pszDesc));
    return pUrb;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnWakeup}
 */
static DECLCALLBACK(int) usbNetWakeup(PPDMUSBINS pUsbIns)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));

    return RTSemEventSignal(pThis->hEvtDoneQueue);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbCancel}
 */
static DECLCALLBACK(int) usbNetUrbCancel(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/ pUrb=%p:%s\n", pUsbIns->iInstance, pUrb, pUrb->pszDesc));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Remove the URB from the to-host queue and move it onto the done queue.
     */
    if (usbNetQueueRemove(&pThis->ToHostQueue, pUrb))
        usbNetLinkDone(pThis, pUrb);

    if (usbNetQueueRemove(&pThis->ToHostIntrQueue, pUrb))
        usbNetLinkDone(pThis, pUrb);

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * Handle requests sent to the outbound (to device) bulk pipe.
 */
static int usbNetHandleBulkHostToDev(PUSBNET pThis, PUSBNETEP pEp, PVUSBURB pUrb)
{
    /*
     * Stall the request if the pipe is halted.
     */
    if (RT_UNLIKELY(pEp->fHalted))
        return usbNetCompleteStall(pThis, NULL, pUrb, "Halted pipe");

    /*
     * Process the transfer.
     */
    PCUSBNCMNTH16 pNth16 = (PCUSBNCMNTH16)&pUrb->pbData[0];
    if (pUrb->cbData < sizeof(*pNth16))
    {
        LogFunc(("/#%u/ Bad NTH16: cbData=%#x < min=%#x\n", pThis->pUsbIns->iInstance, pUrb->cbData, sizeof(*pNth16) ));
        return usbNetCompleteStall(pThis, NULL, pUrb, "BAD NTH16");
    }
    if (pNth16->dwSignature != USBNCMNTH16_SIGNATURE)
    {
        LogFunc(("/#%u/ NTH16: Invalid dwSignature value: %#x\n", pThis->pUsbIns->iInstance, pNth16->dwSignature));
        return usbNetCompleteStall(pThis, NULL, pUrb, "Bad NTH16");
    }
    Log2Func(("/#%u/ NTH16: wHeaderLength=%#x wSequence=%#x wBlockLength=%#x wNdpIndex=%#x cbData=%#x fShortNotOk=%RTbool\n", pThis->pUsbIns->iInstance,
              pNth16->wHeaderLength, pNth16->wSequence, pNth16->wBlockLength, pNth16->wNdpIndex, pUrb->cbData, pUrb->fShortNotOk));
    if (pNth16->wHeaderLength != sizeof(*pNth16))
    {
        LogFunc(("/#%u/ NTH16: Bad wHeaderLength value: %#x\n", pThis->pUsbIns->iInstance, pNth16->wHeaderLength));
        return usbNetCompleteStall(pThis, NULL, pUrb, "Bad NTH16");

    }
    if (pNth16->wBlockLength > pUrb->cbData)
    {
        LogFunc(("/#%u/ NTH16: Bad wBlockLength value: %#x\n", pThis->pUsbIns->iInstance, pNth16->wBlockLength));
        return usbNetCompleteStall(pThis, NULL, pUrb, "Bad NTH16");
    }

    if (pNth16->wNdpIndex < sizeof(*pNth16))
    {
        LogFunc(("/#%u/ NTH16: wNdpIndex is too small: %#x (%u), at least required %#x\n", pThis->pUsbIns->iInstance,
             pNth16->wNdpIndex, pNth16->wNdpIndex, sizeof(*pNth16) ));
        return usbNetCompleteStall(pThis, NULL, pUrb, "Bad NTH16");
    }

    /* Walk the NDPs and process the datagrams. */
    uint16_t offNdp16Next = pNth16->wNdpIndex;
    while (offNdp16Next)
    {
        if (offNdp16Next >= pUrb->cbData)
        {
            LogFunc(("/#%u/ Bad NDP16: offNdp16Next=%#x >= cbData=%#x\n", pThis->pUsbIns->iInstance, offNdp16Next, pUrb->cbData));
            return usbNetCompleteStall(pThis, NULL, pUrb, "BAD NDP16");
        }

        size_t cbNdpMax = pUrb->cbData - offNdp16Next;
        PCUSBNCMNDP16 pNdp16 = (PCUSBNCMNDP16)&pUrb->pbData[pNth16->wNdpIndex];
        if (cbNdpMax < sizeof(*pNdp16))
        {
            LogFunc(("/#%u/ Bad NDP16: cbNdpMax=%#x < min=%#x\n", pThis->pUsbIns->iInstance, cbNdpMax, sizeof(*pNdp16) ));
            return usbNetCompleteStall(pThis, NULL, pUrb, "BAD NDP16");
        }

        if (   pNdp16->dwSignature != USBNCMNDP16_SIGNATURE_NCM0
            && pNdp16->dwSignature != USBNCMNDP16_SIGNATURE_NCM1)
        {
            LogFunc(("/#%u/ NDP16: Invalid dwSignature value: %#x\n", pThis->pUsbIns->iInstance, pNdp16->dwSignature));
            return usbNetCompleteStall(pThis, NULL, pUrb, "Bad NDP16");
        }

        if (   pNdp16->wLength < sizeof(*pNdp16)
            || (pNdp16->wLength & 0x3)
            || pNdp16->wLength > cbNdpMax)
        {
            LogFunc(("/#%u/ NDP16: Invalid size value: %#x, req. (min %#x max %#x)\n", pThis->pUsbIns->iInstance,
                 pNdp16->wLength, sizeof(*pNdp16), cbNdpMax));
            return usbNetCompleteStall(pThis, NULL, pUrb, "Bad NDP16");
        }

        if (pNdp16->dwSignature == USBNCMNDP16_SIGNATURE_NCM0)
        {
            PCUSBNCMNDP16DGRAM pDGram = &pNdp16->DataGram0;
            size_t cEntries = 2 + (pNdp16->wLength - (sizeof(*pNdp16))) / sizeof(*pDGram);

            int rc = pThis->Lun0.pINetwork->pfnBeginXmit(pThis->Lun0.pINetwork, true /* fOnWorkerThread */);
            if (RT_FAILURE(rc))
                return usbNetCompleteStall(pThis, NULL, pUrb, "BeginXmit failed");

            for (uint32_t i = 0; i < cEntries; i++)
            {
                /* Either 0 element marks end of list. */
                if (   pDGram->wDatagramIndex == 0
                    || pDGram->wDatagramLength == 0)
                    break;

                if (   pDGram->wDatagramIndex < sizeof(*pNth16)
                    || pDGram->wDatagramIndex >= pUrb->cbData)
                {
                    LogFunc(("/#%u/ DGRAM16: Invalid wDatagramIndex value: %#x\n", pThis->pUsbIns->iInstance, pDGram->wDatagramIndex));
                    return usbNetCompleteStall(pThis, NULL, pUrb, "Bad DGRAM16");
                }

                if (pUrb->cbData - pDGram->wDatagramIndex < pDGram->wDatagramLength)
                {
                    LogFunc(("/#%u/ DGRAM16: Invalid wDatagramLength value: %#x (max %#x)\n", pThis->pUsbIns->iInstance,
                         pDGram->wDatagramLength, pUrb->cbData - pDGram->wDatagramIndex));
                    return usbNetCompleteStall(pThis, NULL, pUrb, "Bad DGRAM16");
                }

                PPDMSCATTERGATHER pSgBuf;
                rc = pThis->Lun0.pINetwork->pfnAllocBuf(pThis->Lun0.pINetwork, pDGram->wDatagramLength, NULL /*pGso*/, &pSgBuf);
                if (RT_SUCCESS(rc) && pSgBuf)
                {
                    uint8_t *pbBuf = (uint8_t *)pSgBuf->aSegs[0].pvSeg;
                    memcpy(pbBuf, &pUrb->pbData[pDGram->wDatagramIndex], pDGram->wDatagramLength);
                    usbNetPacketDump(pThis, pbBuf, pDGram->wDatagramLength, "--> Tx");
                    pSgBuf->cbUsed = pDGram->wDatagramLength;
                    rc = pThis->Lun0.pINetwork->pfnSendBuf(pThis->Lun0.pINetwork, pSgBuf, true /* fOnWorkerThread */);
                    if (RT_FAILURE(rc))
                        return usbNetCompleteStall(pThis, NULL, pUrb, "SendBuf failed");
                    STAM_REL_COUNTER_ADD(&pThis->StatTransmitBytes, pDGram->wDatagramLength);
                }
                else
                    return usbNetCompleteStall(pThis, NULL, pUrb, "AllocBuf failed");

                pDGram++;
            }

            pThis->Lun0.pINetwork->pfnEndXmit(pThis->Lun0.pINetwork);
        }
        else
        {
            LogFunc(("/#%u/ NDP16: Not implemented\n", pThis->pUsbIns->iInstance));
            return usbNetCompleteStall(pThis, NULL, pUrb, "Bad NDP16");
        }

        offNdp16Next = pNdp16->wNextNdpIndex;
    }

    return usbNetCompleteOk(pThis, pUrb, pUrb->cbData);
}


/**
 * Handle requests sent to the inbound (to host) bulk pipe.
 */
static int usbNetHandleBulkDevToHost(PUSBNET pThis, PUSBNETEP pEp, PVUSBURB pUrb)
{
    /*
     * Stall the request if the pipe is halted OR if there is no
     * pending request yet.
     */
    if (RT_UNLIKELY(pEp->fHalted))
        return usbNetCompleteStall(pThis, NULL, pUrb, pEp->fHalted ? "Halted pipe" : "No request");

    usbNetQueueAddTail(&pThis->ToHostQueue, pUrb);
    if (pThis->fHaveToHostQueueWaiter)
        RTSemEventSignal(pThis->hEvtToHostQueue);

    LogFlowFunc(("/#%u/ Added %p:%s to the to-host queue\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc));
    return VINF_SUCCESS;
}


/**
 * Handle requests sent to the inbound (to host) interrupt pipe.
 */
static int usbNetHandleIntrDevToHost(PUSBNET pThis, PUSBNETEP pEp, PVUSBURB pUrb)
{
    /* Stall the request if the pipe is halted. */
    if (RT_UNLIKELY(pEp->fHalted))
        return usbNetCompleteStall(pThis, NULL, pUrb, pEp->fHalted ? "Halted pipe" : "No request");

    /* CONNECTION_SPEED_CHANGE is sent first, followed by NETWORK_CONNECTION. See [NCM10] (section 7.1) */
    if (!pThis->fInitialSpeedChangeSent)
    {
        USBCDCNOTIFICICATIONSPEEDCHG SpeedChange;
        SpeedChange.Hdr.bmRequestType     = 0xa1;
        SpeedChange.Hdr.bNotificationCode = USB_CDC_NOTIFICATION_CODE_CONNECTION_SPEED_CHANGE;
        SpeedChange.Hdr.wValue            = 0;
        SpeedChange.Hdr.wIndex            = 0;
        SpeedChange.Hdr.wLength           = 8;
        SpeedChange.DLBitRate             = UINT32_MAX;
        SpeedChange.ULBitRate             = UINT32_MAX;
        usbNetCompleteNotificationOk(pThis, pUrb, &SpeedChange, sizeof(SpeedChange));
        pThis->fInitialSpeedChangeSent = true;
    }
    else if (!pThis->fInitialLinkStatusSent)
    {
        USBCDCNOTIFICICATION LinkNotification;
        LinkNotification.bmRequestType     = 0xa1;
        LinkNotification.bNotificationCode = USB_CDC_NOTIFICATION_CODE_NETWORK_CONNECTION;
        LinkNotification.wValue            = pThis->fLinkUp ? 1 : 0;
        LinkNotification.wIndex            = 0;
        LinkNotification.wLength           = 0;
        usbNetCompleteNotificationOk(pThis, pUrb, &LinkNotification, sizeof(LinkNotification));
        pThis->fInitialLinkStatusSent = true;
    }
    else
        usbNetQueueAddTail(&pThis->ToHostIntrQueue, pUrb);

    LogFlowFunc(("/#%u/ Added %p:%s to the to-host interrupt queue\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc));
    return VINF_SUCCESS;
}


/**
 * Notifies both the guest and the network/driver of link state changes.
 *
 * @param   pThis               The UsbNet instance.
 * @param   enmLinkState        The new link state.
 */
DECLHIDDEN(void) usbNetLinkStateNotify(PUSBNET pThis, PDMNETWORKLINKSTATE enmLinkState)
{
    LogFlowFunc(("/#%u/ enmLinkState=%d\n", pThis->pUsbIns->iInstance, enmLinkState));
    RTCritSectEnter(&pThis->CritSect);
    /* Trigger notifications */
    pThis->fInitialLinkStatusSent  = false;
    pThis->fInitialSpeedChangeSent = false;
    PVUSBURB pUrb = usbNetQueueRemoveHead(&pThis->ToHostIntrQueue);
    /* If there is a request pending, use it. Otherwise the host should poll us soon. */
    if (pUrb)
        usbNetHandleIntrDevToHost(pThis, &pThis->aEps[3], pUrb);
    RTCritSectLeave(&pThis->CritSect);
    /* Always notify our network when notifying the guest. */
    if (pThis->Lun0.pINetwork)
        pThis->Lun0.pINetwork->pfnNotifyLinkChanged(pThis->Lun0.pINetwork, enmLinkState);
}


/**
 * Handles request send to the default control pipe.
 */
static int usbNetHandleDefaultPipe(PUSBNET pThis, PUSBNETEP pEp, PVUSBURB pUrb)
{
    PVUSBSETUP pSetup = (PVUSBSETUP)&pUrb->pbData[0];
    AssertReturn(pUrb->cbData >= sizeof(*pSetup), VERR_VUSB_FAILED_TO_QUEUE_URB);

    if ((pSetup->bmRequestType & VUSB_REQ_MASK) == VUSB_REQ_STANDARD)
    {
        switch (pSetup->bRequest)
        {
            case VUSB_REQ_GET_DESCRIPTOR:
            {
                if (pSetup->bmRequestType != (VUSB_TO_DEVICE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST))
                {
                    LogFunc(("/#%u/ Bad GET_DESCRIPTOR req: bmRequestType=%#x\n", pThis->pUsbIns->iInstance, pSetup->bmRequestType));
                    return usbNetCompleteStall(pThis, pEp, pUrb, "Bad GET_DESCRIPTOR");
                }

                switch (pSetup->wValue >> 8)
                {
                    uint32_t    cbCopy;

                    case VUSB_DT_STRING:
                        LogFunc(("/#%u/ GET_DESCRIPTOR DT_STRING wValue=%#x wIndex=%#x\n", pThis->pUsbIns->iInstance, pSetup->wValue, pSetup->wIndex));
                        break;
                    case VUSB_DT_DEVICE_QUALIFIER:
                        LogFunc(("/#%u/ GET_DESCRIPTOR DT_DEVICE_QUALIFIER wValue=%#x wIndex=%#x\n", pThis->pUsbIns->iInstance, pSetup->wValue, pSetup->wIndex));
                        /* Returned data is written after the setup message. */
                        cbCopy = pUrb->cbData - sizeof(*pSetup);
                        cbCopy = RT_MIN(cbCopy, sizeof(g_UsbNetDeviceQualifier));
                        memcpy(&pUrb->pbData[sizeof(*pSetup)], &g_UsbNetDeviceQualifier, cbCopy);
                        return usbNetCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
                    case VUSB_DT_BOS:
                        LogFunc(("/#%u/ GET_DESCRIPTOR DT_BOS wValue=%#x wIndex=%#x\n", pThis->pUsbIns->iInstance, pSetup->wValue, pSetup->wIndex));
                        /* Returned data is written after the setup message. */
                        cbCopy = pUrb->cbData - sizeof(*pSetup);
                        cbCopy = RT_MIN(cbCopy, sizeof(g_UsbNetBOS));
                        memcpy(&pUrb->pbData[sizeof(*pSetup)], &g_UsbNetBOS, cbCopy);
                        return usbNetCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
                    default:
                        LogFunc(("/#%u/ GET_DESCRIPTOR, huh? wValue=%#x wIndex=%#x\n", pThis->pUsbIns->iInstance, pSetup->wValue, pSetup->wIndex));
                        break;
                }
                break;
            }

            case VUSB_REQ_GET_STATUS:
            {
                uint16_t    wRet = 0;
                size_t     cbCopy = 0;

                if (pSetup->wLength != 2)
                {
                    LogRelFlow(("UsbNet: Bad GET_STATUS req: wLength=%#x\n",
                                pSetup->wLength));
                    break;
                }
                Assert(pSetup->wValue == 0);
                switch (pSetup->bmRequestType)
                {
                    case VUSB_TO_DEVICE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        Assert(pSetup->wIndex == 0);
                        LogRelFlow(("UsbNet: GET_STATUS (device)\n"));
                        wRet = 0;   /* Not self-powered, no remote wakeup. */
                        cbCopy = pUrb->cbData - sizeof(*pSetup);
                        cbCopy = RT_MIN(cbCopy, sizeof(wRet));
                        memcpy(&pUrb->pbData[sizeof(*pSetup)], &wRet, cbCopy);
                        return usbNetCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
                    }

                    case VUSB_TO_INTERFACE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        if (pSetup->wIndex == 0)
                        {
                            cbCopy = pUrb->cbData - sizeof(*pSetup);
                            cbCopy = RT_MIN(cbCopy, sizeof(wRet));
                            memcpy(&pUrb->pbData[sizeof(*pSetup)], &wRet, cbCopy);
                            return usbNetCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
                        }
                        LogRelFlow(("UsbNet: GET_STATUS (interface) invalid, wIndex=%#x\n", pSetup->wIndex));
                        break;
                    }

                    case VUSB_TO_ENDPOINT | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        if (pSetup->wIndex < RT_ELEMENTS(pThis->aEps))
                        {
                            wRet = pThis->aEps[pSetup->wIndex].fHalted ? 1 : 0;
                            cbCopy = pUrb->cbData - sizeof(*pSetup);
                            cbCopy = RT_MIN(cbCopy, sizeof(wRet));
                            memcpy(&pUrb->pbData[sizeof(*pSetup)], &wRet, cbCopy);
                            return usbNetCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
                        }
                        LogRelFlow(("UsbNet: GET_STATUS (endpoint) invalid, wIndex=%#x\n", pSetup->wIndex));
                        break;
                    }

                    default:
                        LogRelFlow(("UsbNet: Bad GET_STATUS req: bmRequestType=%#x\n",
                                    pSetup->bmRequestType));
                        return usbNetCompleteStall(pThis, pEp, pUrb, "Bad GET_STATUS");
                }
                break;
            }

            case 0x31:
                break;

            case VUSB_REQ_CLEAR_FEATURE:
                break;
        }

        /** @todo implement this. */
        LogFunc(("/#%u/ Implement standard request: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n", pThis->pUsbIns->iInstance,
             pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue, pSetup->wIndex, pSetup->wLength));

        usbNetCompleteStall(pThis, pEp, pUrb, "TODO: standard request stuff");
    }
    else if ((pSetup->bmRequestType & VUSB_REQ_MASK) == VUSB_REQ_CLASS)
    {
        switch (pSetup->bRequest)
        {
            case VUSB_REQ_GET_NTB_PARAMETERS:
            {
                if (pSetup->bmRequestType != (VUSB_TO_INTERFACE | VUSB_REQ_CLASS | VUSB_DIR_TO_HOST))
                {
                    LogFunc(("/#%u/ Bad GET_NTB_PARAMETERS req: bmRequestType=%#x\n", pThis->pUsbIns->iInstance, pSetup->bmRequestType));
                    return usbNetCompleteStall(pThis, pEp, pUrb, "Bad GET_NTB_PARAMETERS");
                }

                USBNCMNTBPARAMS NtbParams;

                NtbParams.wLength                 = sizeof(NtbParams);
                NtbParams.bmNtbFormatsSupported   = RT_BIT(0);
                NtbParams.dwNtbInMaxSize          = _4K;
                NtbParams.wNdpInDivisor           = 4;
                NtbParams.wNdpInPayloadRemainder  = 4;
                NtbParams.wNdpInAlignment         = 4;
                NtbParams.u16Rsvd0                = 0;
                NtbParams.dwNtbOutMaxSize         = _4K;
                NtbParams.wNdpOutDivisor          = 4;
                NtbParams.wNdpOutPayloadRemainder = 4;
                NtbParams.wNdpOutAlignment        = 4;
                NtbParams.wNtpOutMaxDatagrams     = 1;

                uint32_t cbCopy = pUrb->cbData - sizeof(*pSetup);
                cbCopy = RT_MIN(cbCopy, sizeof(NtbParams));
                memcpy(&pUrb->pbData[sizeof(*pSetup)], &NtbParams, cbCopy);
                return usbNetCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
            }

            case VUSB_REQ_CLEAR_FEATURE:
                break;
        }

        usbNetCompleteStall(pThis, pEp, pUrb, "CLASS_REQ");
    }
    else
    {
        LogFunc(("/#%u/ Unknown control msg: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n", pThis->pUsbIns->iInstance,
             pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue, pSetup->wIndex, pSetup->wLength));
        return usbNetCompleteStall(pThis, pEp, pUrb, "Unknown control msg");
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbQueue}
 */
static DECLCALLBACK(int) usbNetQueue(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/ pUrb=%p:%s EndPt=%#x\n", pUsbIns->iInstance, pUrb, pUrb->pszDesc, pUrb->EndPt));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Parse on a per end-point basis.
     */
    int rc;
    switch (pUrb->EndPt)
    {
        case 0:
            rc = usbNetHandleDefaultPipe(pThis, &pThis->aEps[0], pUrb);
            break;

        case 0x81:
            AssertFailed();
            RT_FALL_THRU();
        case 0x01:
            rc = usbNetHandleBulkDevToHost(pThis, &pThis->aEps[1], pUrb);
            break;

        case 0x02:
            rc = usbNetHandleBulkHostToDev(pThis, &pThis->aEps[2], pUrb);
            break;

        case 0x03:
            rc = usbNetHandleIntrDevToHost(pThis, &pThis->aEps[3], pUrb);
            break;

        default:
            AssertMsgFailed(("EndPt=%d\n", pUrb->EndPt));
            rc = VERR_VUSB_FAILED_TO_QUEUE_URB;
            break;
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbClearHaltedEndpoint}
 */
static DECLCALLBACK(int) usbNetUsbClearHaltedEndpoint(PPDMUSBINS pUsbIns, unsigned uEndpoint)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/ uEndpoint=%#x\n", pUsbIns->iInstance, uEndpoint));

    if ((uEndpoint & ~0x80) < RT_ELEMENTS(pThis->aEps))
    {
        RTCritSectEnter(&pThis->CritSect);
        pThis->aEps[(uEndpoint & ~0x80)].fHalted = false;
        RTCritSectLeave(&pThis->CritSect);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbSetInterface}
 */
static DECLCALLBACK(int) usbNetUsbSetInterface(PPDMUSBINS pUsbIns, uint8_t bInterfaceNumber, uint8_t bAlternateSetting)
{
    RT_NOREF(bInterfaceNumber);
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/ bInterfaceNumber=%u bAlternateSetting=%u\n", pUsbIns->iInstance, bInterfaceNumber, bAlternateSetting));
    Assert(bAlternateSetting == 0 || bAlternateSetting == 1);
    RTCritSectEnter(&pThis->CritSect);
    if (pThis->bAlternateSetting != bAlternateSetting)
    {
        if (bAlternateSetting == 0)
        {
            /* This is some kind of reset. */
            usbNetResetWorker(pThis, NULL, true /*fSetConfig*/);
        }
        else
        {
            Assert(bAlternateSetting == 1);
            /* Switching from configuration to data mode, need to send notifications. */
            pThis->fInitialLinkStatusSent  = false;
            pThis->fInitialSpeedChangeSent = false;
        }
        pThis->bAlternateSetting = bAlternateSetting;
    }
    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbSetConfiguration}
 */
static DECLCALLBACK(int) usbNetUsbSetConfiguration(PPDMUSBINS pUsbIns, uint8_t bConfigurationValue,
                                                   const void *pvOldCfgDesc, const void *pvOldIfState, const void *pvNewCfgDesc)
{
    RT_NOREF(pvOldCfgDesc, pvOldIfState,  pvNewCfgDesc);
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/ bConfigurationValue=%u\n", pUsbIns->iInstance, bConfigurationValue));
    Assert(bConfigurationValue == 1);
    RTCritSectEnter(&pThis->CritSect);

    /*
     * If the same config is applied more than once, it's a kind of reset.
     */
    if (pThis->bConfigurationValue == bConfigurationValue)
        usbNetResetWorker(pThis, NULL, true /*fSetConfig*/); /** @todo figure out the exact difference */
    pThis->bConfigurationValue = bConfigurationValue;

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbGetDescriptorCache}
 */
static DECLCALLBACK(PCPDMUSBDESCCACHE) usbNetUsbGetDescriptorCache(PPDMUSBINS pUsbIns)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));
    return &pThis->UsbDescCache;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbReset}
 */
static DECLCALLBACK(int) usbNetUsbReset(PPDMUSBINS pUsbIns, bool fResetOnLinux)
{
    RT_NOREF(fResetOnLinux);
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));
    RTCritSectEnter(&pThis->CritSect);

    int rc = usbNetResetWorker(pThis, NULL, false /*fSetConfig*/);

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnDriverAttach}
 */
static DECLCALLBACK(int) usbNetDriverAttach(PPDMUSBINS pUsbIns, unsigned iLUN, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);

    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));

    AssertMsg(iLUN == 0, ("UsbNet: No other LUN than 0 is supported\n"));
    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("UsbNet: Device does not support hotplugging\n"));

    /* the usual paranoia */
    AssertRelease(!pThis->Lun0.pIBase);
    AssertRelease(!pThis->Lun0.pINetwork);

    /*
     * Try attach the network driver.
     */
    int rc = PDMUsbHlpDriverAttach(pUsbIns, iLUN, &pThis->Lun0.IBase, &pThis->Lun0.pIBase, NULL);
    if (RT_SUCCESS(rc))
    {
        /* Get network interface. */
        pThis->Lun0.pINetwork = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pIBase, PDMINETWORKUP);
        AssertMsgReturn(pThis->Lun0.pINetwork, ("Missing network interface below\n"), VERR_PDM_MISSING_INTERFACE);
    }
    else
        AssertMsgFailed(("Failed to attach LUN#%d. rc=%Rrc\n", iLUN, rc));

    if (RT_FAILURE(rc))
    {
        pThis->Lun0.pIBase    = NULL;
        pThis->Lun0.pINetwork = NULL;
    }

    return rc;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnDriverDetach}
 */
static DECLCALLBACK(void) usbNetDriverDetach(PPDMUSBINS pUsbIns, unsigned iLUN, uint32_t fFlags)
{
    RT_NOREF(iLUN, fFlags);
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);

    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));

    AssertMsg(iLUN == 0, ("UsbNet: No other LUN than 0 is supported\n"));
    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("UsbNet: Device does not support hotplugging\n"));

    /*
     * Zero some important members.
     */
    pThis->Lun0.pIBase    = NULL;
    pThis->Lun0.pINetwork = NULL;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnVMReset}
 */
static DECLCALLBACK(void) usbNetVMReset(PPDMUSBINS pUsbIns)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);

    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));
    RTCritSectEnter(&pThis->CritSect);
    int rc = usbNetResetWorker(pThis, NULL, false /*fSetConfig*/);
    AssertRC(rc);
    RTCritSectLeave(&pThis->CritSect);
}


 /**
 * @interface_method_impl{PDMUSBREG,pfnVMSuspend}
 */
static DECLCALLBACK(void) usbNetVMSuspend(PPDMUSBINS pUsbIns)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
#ifdef LOG_ENABLED
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;
    VMSUSPENDREASON enmReason = pHlp->pfnVMGetSuspendReason(pUsbIns);
    LogFlowFunc(("/#%u/ enmReason=%u\n", pUsbIns->iInstance, enmReason));
#endif
    RTCritSectEnter(&pThis->CritSect);
    pThis->fSuspended = true;
    /* Unblock receive thread */
    RTSemEventSignal(pThis->hEvtToHostQueue);
    RTCritSectLeave(&pThis->CritSect);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnVMResume}
 */
static DECLCALLBACK(void) usbNetVMResume(PPDMUSBINS pUsbIns)
{
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
#ifdef LOG_ENABLED
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;
    VMRESUMEREASON enmReason = pHlp->pfnVMGetResumeReason(pUsbIns);
    LogFlowFunc(("/#%u/ enmReason=%u\n", pUsbIns->iInstance, enmReason));
#endif
    RTCritSectEnter(&pThis->CritSect);
    pThis->fSuspended = false;
    RTCritSectLeave(&pThis->CritSect);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnDestruct}
 */
static DECLCALLBACK(void) usbNetDestruct(PPDMUSBINS pUsbIns)
{
    PDMUSB_CHECK_VERSIONS_RETURN_VOID(pUsbIns);
    PUSBNET pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    LogFlowFunc(("/#%u/\n", pUsbIns->iInstance));

    PDMUsbHlpTimerDestroy(pUsbIns, pThis->hTimerLinkUp);

    if (RTCritSectIsInitialized(&pThis->CritSect))
    {
        RTCritSectEnter(&pThis->CritSect);
        RTCritSectLeave(&pThis->CritSect);
        RTCritSectDelete(&pThis->CritSect);
    }

    if (pThis->hEvtDoneQueue != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hEvtDoneQueue);
        pThis->hEvtDoneQueue = NIL_RTSEMEVENT;
    }

    if (pThis->hEvtToHostQueue != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hEvtToHostQueue);
        pThis->hEvtToHostQueue = NIL_RTSEMEVENT;
    }

    if (pThis->hEvtReset != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(pThis->hEvtReset);
        pThis->hEvtReset = NIL_RTSEMEVENTMULTI;
    }
}


/**
 * @interface_method_impl{PDMUSBREG,pfnConstruct}
 */
static DECLCALLBACK(int) usbNetConstruct(PPDMUSBINS pUsbIns, int iInstance, PCFGMNODE pCfg, PCFGMNODE pCfgGlobal)
{
    RT_NOREF(pCfgGlobal);
    PDMUSB_CHECK_VERSIONS_RETURN(pUsbIns);
    PUSBNET     pThis = PDMINS_2_DATA(pUsbIns, PUSBNET);
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;

    LogFlowFunc(("/#%u/\n", iInstance));

    pThis->u32PktNo = 1;
    /*
     * Perform the basic structure initialization first so the destructor
     * will not misbehave.
     */
    pThis->pUsbIns                                      = pUsbIns;
    pThis->hEvtDoneQueue                                = NIL_RTSEMEVENT;
    pThis->hEvtToHostQueue                              = NIL_RTSEMEVENT;
    pThis->hEvtReset                                    = NIL_RTSEMEVENTMULTI;
    /* IBase */
    pThis->Lun0.IBase.pfnQueryInterface                 = usbNetLun0QueryInterface;
    /* INetworkPort */
    pThis->Lun0.INetworkDown.pfnWaitReceiveAvail        = usbNetNetworkDown_WaitReceiveAvail;
    pThis->Lun0.INetworkDown.pfnReceive                 = usbNetNetworkDown_Receive;
    pThis->Lun0.INetworkDown.pfnXmitPending             = usbNetNetworkDown_XmitPending;
    /* INetworkConfig */
    pThis->Lun0.INetworkConfig.pfnGetMac                = usbNetGetMac;
    pThis->Lun0.INetworkConfig.pfnGetLinkState          = usbNetGetLinkState;
    pThis->Lun0.INetworkConfig.pfnSetLinkState          = usbNetSetLinkState;

    usbNetQueueInit(&pThis->ToHostQueue);
    usbNetQueueInit(&pThis->ToHostIntrQueue);
    usbNetQueueInit(&pThis->DoneQueue);

    int rc = RTCritSectInit(&pThis->CritSect);
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pThis->hEvtDoneQueue);
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pThis->hEvtToHostQueue);
    AssertRCReturn(rc, rc);

    rc = RTSemEventMultiCreate(&pThis->hEvtReset);
    AssertRCReturn(rc, rc);

    /*
     * Validate and read the configuration.
     */
    rc = pHlp->pfnCFGMValidateConfig(pCfg, "/",
                                     "MAC|"
                                     "CableConnected|"
                                     "LinkUpDelay"
                                     , "Config", "UsbNet", iInstance);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read the configuration.
     */
    rc = pHlp->pfnCFGMQueryBytes(pCfg, "MAC", &pThis->MacConfigured, sizeof(pThis->MacConfigured));
    if (RT_FAILURE(rc))
        return PDMUSB_SET_ERROR(pUsbIns, rc,
                                N_("Configuration error: Failed to get the \"MAC\" value"));
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CableConnected", &pThis->fLinkUp, true);
    if (RT_FAILURE(rc))
        return PDMUSB_SET_ERROR(pUsbIns, rc,
                                N_("Configuration error: Failed to get the \"CableConnected\" value"));

    /*
     * Attach the network driver.
     */
    rc = PDMUsbHlpDriverAttach(pUsbIns, 0 /*iLun*/, &pThis->Lun0.IBase, &pThis->Lun0.pIBase, "Network Port");
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBNET failed to attach network driver"));
    pThis->Lun0.pINetwork = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pIBase, PDMINETWORKUP);
    if (!pThis->Lun0.pINetwork)
        return PDMUsbHlpVMSetError(pUsbIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS,
                                   N_("USBNET failed to query the PDMINETWORKUP from the driver below it"));

    /*
     * Create the timer for delaying of bringing the link up.
     */
    rc = PDMUsbHlpTimerCreate(pUsbIns, TMCLOCK_VIRTUAL, usbNetTimerLinkUp, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT,
                              "Link Up", &pThis->hTimerLinkUp);
    AssertRCReturn(rc, rc);

    /*
     * Build the USB descriptors.
     */
    pThis->aUsbStringsEnUs[0].idx = USBNET_STR_ID_MANUFACTURER;
    pThis->aUsbStringsEnUs[0].psz = "VirtualBox";

    pThis->aUsbStringsEnUs[1].idx = USBNET_STR_ID_PRODUCT;
    pThis->aUsbStringsEnUs[1].psz = "USB Ethernet";

    /* Build the MAC address. */
    ssize_t cch = RTStrPrintf2(&pThis->aszMac[0], sizeof(pThis->aszMac), "%#.6Rhxs", &pThis->MacConfigured);
    AssertReturn(cch + 1 == sizeof(pThis->aszMac), VERR_INTERNAL_ERROR_4);

    pThis->aUsbStringsEnUs[2].idx = USBNET_STR_ID_MAC_ADDRESS;
    pThis->aUsbStringsEnUs[2].psz = &pThis->aszMac[0];

    pThis->UsbLang.idLang    = 0x0409; /* en_US. */
    pThis->UsbLang.cStrings  = RT_ELEMENTS(pThis->aUsbStringsEnUs);
    pThis->UsbLang.paStrings = &pThis->aUsbStringsEnUs[0];

    pThis->UsbDescCache.paLanguages                  = &pThis->UsbLang;
    pThis->UsbDescCache.cLanguages                   = 1;
    pThis->UsbDescCache.fUseCachedDescriptors        = true;
    pThis->UsbDescCache.fUseCachedStringsDescriptors = true;

    switch (pUsbIns->enmSpeed)
    {
        case VUSB_SPEED_SUPER:
        case VUSB_SPEED_SUPERPLUS:
        {
            pThis->UsbDescCache.pDevice   = &g_UsbNetDeviceDesc30;
            pThis->UsbDescCache.paConfigs = &g_UsbNetConfigDescSS;
            break;
        }
        case VUSB_SPEED_HIGH:
        {
            pThis->UsbDescCache.pDevice   = &g_UsbNetDeviceDesc20;
            pThis->UsbDescCache.paConfigs = &g_UsbNetConfigDescHS;
            break;
        }
        case VUSB_SPEED_FULL:
        case VUSB_SPEED_LOW:
        {
            pThis->UsbDescCache.pDevice   = &g_UsbNetDeviceDesc20;
            pThis->UsbDescCache.paConfigs = &g_UsbNetConfigDescFS;
            break;
        }
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    /*
     * Register statistics.
     * The /Public/ bits are official and used by session info in the GUI.
     */
    PDMUsbHlpSTAMRegisterF(pUsbIns, &pThis->StatReceiveBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data received",    "/Public/NetAdapter/%u/BytesReceived", iInstance);
    PDMUsbHlpSTAMRegisterF(pUsbIns, &pThis->StatTransmitBytes, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data transmitted", "/Public/NetAdapter/%u/BytesTransmitted", iInstance);
    PDMUsbHlpSTAMRegisterF(pUsbIns, &pUsbIns->iInstance,       STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                           "Device instance number",     "/Public/NetAdapter/%u/%s", iInstance, pUsbIns->pReg->szName);

    return VINF_SUCCESS;
}


/**
 * The USB Communications Device Class, Network Control Model (CDC NCM) registration record.
 */
const PDMUSBREG g_UsbNet =
{
    /* u32Version */
    PDM_USBREG_VERSION,
    /* szName */
    "UsbNet",
    /* pszDescription */
    "USB Communications Device Class, one LUN.",
    /* fFlags */
      PDM_USBREG_HIGHSPEED_CAPABLE /*| PDM_USBREG_SUPERSPEED_CAPABLE */,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(USBNET),
    /* pfnConstruct */
    usbNetConstruct,
    /* pfnDestruct */
    usbNetDestruct,
    /* pfnVMInitComplete */
    NULL,
    /* pfnVMPowerOn */
    NULL,
    /* pfnVMReset */
    usbNetVMReset,
    /* pfnVMSuspend */
    usbNetVMSuspend,
    /* pfnVMResume */
    usbNetVMResume,
    /* pfnVMPowerOff */
    NULL,
    /* pfnHotPlugged */
    NULL,
    /* pfnHotUnplugged */
    NULL,
    /* pfnDriverAttach */
    usbNetDriverAttach,
    /* pfnDriverDetach */
    usbNetDriverDetach,
    /* pfnQueryInterface */
    NULL,
    /* pfnUsbReset */
    usbNetUsbReset,
    /* pfnUsbGetDescriptorCache */
    usbNetUsbGetDescriptorCache,
    /* pfnUsbSetConfiguration */
    usbNetUsbSetConfiguration,
    /* pfnUsbSetInterface */
    usbNetUsbSetInterface,
    /* pfnUsbClearHaltedEndpoint */
    usbNetUsbClearHaltedEndpoint,
    /* pfnUrbNew */
    NULL/*usbNetUrbNew*/,
    /* pfnQueue */
    usbNetQueue,
    /* pfnUrbCancel */
    usbNetUrbCancel,
    /* pfnUrbReap */
    usbNetUrbReap,
    /* pfnWakeup */
    usbNetWakeup,
    /* u32TheEnd */
    PDM_USBREG_VERSION
};

