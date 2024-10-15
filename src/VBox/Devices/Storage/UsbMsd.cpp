/* $Id: UsbMsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbMSD - USB Mass Storage Device Emulation.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP   LOG_GROUP_USB_MSD
#include <VBox/vmm/pdmusb.h>
#include <VBox/vmm/pdmstorageifs.h>
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
/** @name USB MSD string IDs
 * @{ */
#define USBMSD_STR_ID_MANUFACTURER  1
#define USBMSD_STR_ID_PRODUCT_HD    2
#define USBMSD_STR_ID_PRODUCT_CDROM 3
/** @} */

/** @name USB MSD vendor and product IDs
 * @{ */
#define VBOX_USB_VENDOR             0x80EE
#define USBMSD_PID_HD               0x0030
#define USBMSD_PID_CD               0x0031
/** @} */

/** Saved state version. */
#define USB_MSD_SAVED_STATE_VERSION             2
/** Saved state vesion before the cleanup. */
#define USB_MSD_SAVED_STATE_VERSION_PRE_CLEANUP 1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * USB MSD Command Block Wrapper or CBW. The command block
 * itself (CBWCB) contains protocol-specific data (here SCSI).
 */
#pragma pack(1)
typedef struct USBCBW
{
    uint32_t    dCBWSignature;
#define USBCBW_SIGNATURE        UINT32_C(0x43425355)
    uint32_t    dCBWTag;
    uint32_t    dCBWDataTransferLength;
    uint8_t     bmCBWFlags;
#define USBCBW_DIR_MASK         RT_BIT(7)
#define USBCBW_DIR_OUT          0
#define USBCBW_DIR_IN           RT_BIT(7)
    uint8_t     bCBWLun;
    uint8_t     bCBWCBLength;
    uint8_t     CBWCB[16];
} USBCBW;
#pragma pack()
AssertCompileSize(USBCBW, 31);
/** Pointer to a Command Block Wrapper. */
typedef USBCBW *PUSBCBW;
/** Pointer to a const Command Block Wrapper. */
typedef const USBCBW *PCUSBCBW;

/**
 * USB MSD Command Status Wrapper or CSW.
 */
#pragma pack(1)
typedef struct USBCSW
{
    uint32_t    dCSWSignature;
#define USBCSW_SIGNATURE            UINT32_C(0x53425355)
    uint32_t    dCSWTag;
    uint32_t    dCSWDataResidue;
#define USBCSW_STATUS_OK            UINT8_C(0)
#define USBCSW_STATUS_FAILED        UINT8_C(1)
#define USBCSW_STATUS_PHASE_ERROR   UINT8_C(2)
    uint8_t     bCSWStatus;
} USBCSW;
#pragma pack()
AssertCompileSize(USBCSW, 13);
/** Pointer to a Command Status Wrapper. */
typedef USBCSW *PUSBCSW;
/** Pointer to a const Command Status Wrapper. */
typedef const USBCSW *PCUSBCSW;


/**
 * The USB MSD request state.
 */
typedef enum USBMSDREQSTATE
{
    /** Invalid status. */
    USBMSDREQSTATE_INVALID = 0,
    /** Ready to receive a new SCSI command. */
    USBMSDREQSTATE_READY,
    /** Waiting for the host to supply data. */
    USBMSDREQSTATE_DATA_FROM_HOST,
    /** The SCSI request is being executed by the driver. */
    USBMSDREQSTATE_EXECUTING,
    /** Have (more) data for the host. */
    USBMSDREQSTATE_DATA_TO_HOST,
    /** Waiting to supply status information to the host. */
    USBMSDREQSTATE_STATUS,
    /** Destroy the request upon completion.
     * This is set when the SCSI request doesn't complete before for the device or
     * mass storage reset operation times out.  USBMSD::pReq will be set to NULL
     * and the only reference to this request will be with DrvSCSI. */
    USBMSDREQSTATE_DESTROY_ON_COMPLETION,
    /** The end of the valid states. */
    USBMSDREQSTATE_END,
    /** 32bit blow up hack. */
    USBMSDREQSTATE_32BIT_HACK = 0x7fffffff
} USBMSDREQSTATE;


/**
 * A pending USB MSD request.
 */
typedef struct USBMSDREQ
{
    /** The state of the request. */
    USBMSDREQSTATE      enmState;
    /** The I/O requesthandle .*/
    PDMMEDIAEXIOREQ     hIoReq;
    /** The size of the data buffer. */
    uint32_t            cbBuf;
    /** Pointer to the data buffer. */
    uint8_t            *pbBuf;
    /** Current buffer offset. */
    uint32_t            offBuf;
    /** The current Cbw when we're in the pending state. */
    USBCBW              Cbw;
    /** The status of a completed SCSI request. */
    uint8_t             iScsiReqStatus;
} USBMSDREQ;
/** Pointer to a USB MSD request. */
typedef USBMSDREQ *PUSBMSDREQ;


/**
 * Endpoint status data.
 */
typedef struct USBMSDEP
{
    bool                fHalted;
} USBMSDEP;
/** Pointer to the endpoint status. */
typedef USBMSDEP *PUSBMSDEP;


/**
 * A URB queue.
 */
typedef struct USBMSDURBQUEUE
{
    /** The head pointer. */
    PVUSBURB            pHead;
    /** Where to insert the next entry. */
    PVUSBURB           *ppTail;
} USBMSDURBQUEUE;
/** Pointer to a URB queue. */
typedef USBMSDURBQUEUE *PUSBMSDURBQUEUE;
/** Pointer to a const URB queue. */
typedef USBMSDURBQUEUE const *PCUSBMSDURBQUEUE;


/**
 * The USB MSD instance data.
 */
typedef struct USBMSD
{
    /** Pointer back to the PDM USB Device instance structure. */
    PPDMUSBINS          pUsbIns;
    /** Critical section protecting the device state. */
    RTCRITSECT          CritSect;

    /** The current configuration.
     * (0 - default, 1 - the only, i.e configured.) */
    uint8_t             bConfigurationValue;
    /** Endpoint 0 is the default control pipe, 1 is the host->dev bulk pipe and 2
     * is the dev->host one. */
    USBMSDEP            aEps[3];
    /** The current request. */
    PUSBMSDREQ          pReq;

    /** Pending to-host queue.
     * The URBs waiting here are pending the completion of the current request and
     * data or status to become available.
     */
    USBMSDURBQUEUE      ToHostQueue;

    /** Done queue
     * The URBs stashed here are waiting to be reaped. */
    USBMSDURBQUEUE      DoneQueue;
    /** Signalled when adding an URB to the done queue and fHaveDoneQueueWaiter
     *  is set. */
    RTSEMEVENT          hEvtDoneQueue;
    /** Someone is waiting on the done queue. */
    bool                fHaveDoneQueueWaiter;

    /** Whether to signal the reset semaphore when the current request completes. */
    bool                fSignalResetSem;
    /** Semaphore usbMsdUsbReset waits on when a request is executing at reset
     *  time.  Only signalled when fSignalResetSem is set. */
    RTSEMEVENTMULTI     hEvtReset;
    /** The reset URB.
     * This is waiting for SCSI request completion before finishing the reset. */
    PVUSBURB            pResetUrb;
    /** Indicates that PDMUsbHlpAsyncNotificationCompleted should be called when
     * the MSD is entering the idle state. */
    volatile bool       fSignalIdle;

    /** Indicates that this device is a CD-ROM. */
    bool                fIsCdrom;

    /**
     * LUN\#0 data.
     */
    struct
    {
        /** The base interface for LUN\#0. */
        PDMIBASE            IBase;
        /** The media port interface fo LUN\#0. */
        PDMIMEDIAPORT       IMediaPort;
        /** The extended media port interface for LUN\#0  */
        PDMIMEDIAEXPORT     IMediaExPort;

        /** The base interface for the SCSI driver connected to LUN\#0. */
        PPDMIBASE           pIBase;
        /** The media interface for th SCSI drver conected to LUN\#0. */
        PPDMIMEDIA          pIMedia;
        /** The extended media inerface for the SCSI driver connected to LUN\#0. */
        PPDMIMEDIAEX        pIMediaEx;
    } Lun0;

} USBMSD;
/** Pointer to the USB MSD instance data. */
typedef USBMSD *PUSBMSD;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const PDMUSBDESCCACHESTRING g_aUsbMsdStrings_en_US[] =
{
    { USBMSD_STR_ID_MANUFACTURER,   "VirtualBox"   },
    { USBMSD_STR_ID_PRODUCT_HD,     "USB Harddisk" },
    { USBMSD_STR_ID_PRODUCT_CDROM,  "USB CD-ROM"   }
};

static const PDMUSBDESCCACHELANG g_aUsbMsdLanguages[] =
{
    { 0x0409, RT_ELEMENTS(g_aUsbMsdStrings_en_US), g_aUsbMsdStrings_en_US }
};

static const VUSBDESCENDPOINTEX g_aUsbMsdEndpointDescsFS[2] =
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
    }
};

static const VUSBDESCENDPOINTEX g_aUsbMsdEndpointDescsHS[2] =
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
    }
};

static const VUSBDESCSSEPCOMPANION g_aUsbMsdEpCompanionSS =
{
    /* .bLength = */            sizeof(VUSBDESCSSEPCOMPANION),
    /* .bDescriptorType = */    VUSB_DT_SS_ENDPOINT_COMPANION,
    /* .bMaxBurst = */          15  /* we can burst all the way */,
    /* .bmAttributes = */       0   /* no streams */,
    /* .wBytesPerInterval = */  0   /* not a periodic endpoint */
};

static const VUSBDESCENDPOINTEX g_aUsbMsdEndpointDescsSS[2] =
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
        /* .pvSsepc = */    &g_aUsbMsdEpCompanionSS,
        /* .cbSsepc = */    sizeof(g_aUsbMsdEpCompanionSS)
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
        /* .pvSsepc = */    &g_aUsbMsdEpCompanionSS,
        /* .cbSsepc = */    sizeof(g_aUsbMsdEpCompanionSS)
    }
};

static const VUSBDESCINTERFACEEX g_UsbMsdInterfaceDescFS =
{
    {
        /* .bLength = */                sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */        VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */       0,
        /* .bAlternateSetting = */      0,
        /* .bNumEndpoints = */          2,
        /* .bInterfaceClass = */        8 /* Mass Storage */,
        /* .bInterfaceSubClass = */     6 /* SCSI transparent command set */,
        /* .bInterfaceProtocol = */     0x50 /* Bulk-Only Transport */,
        /* .iInterface = */             0
    },
    /* .pvMore = */     NULL,
    /* .pvClass = */    NULL,
    /* .cbClass = */    0,
    &g_aUsbMsdEndpointDescsFS[0],
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBDESCINTERFACEEX g_UsbMsdInterfaceDescHS =
{
    {
        /* .bLength = */                sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */        VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */       0,
        /* .bAlternateSetting = */      0,
        /* .bNumEndpoints = */          2,
        /* .bInterfaceClass = */        8 /* Mass Storage */,
        /* .bInterfaceSubClass = */     6 /* SCSI transparent command set */,
        /* .bInterfaceProtocol = */     0x50 /* Bulk-Only Transport */,
        /* .iInterface = */             0
    },
    /* .pvMore = */     NULL,
    /* .pvClass = */    NULL,
    /* .cbClass = */    0,
    &g_aUsbMsdEndpointDescsHS[0],
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBDESCINTERFACEEX g_UsbMsdInterfaceDescSS =
{
    {
        /* .bLength = */                sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */        VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */       0,
        /* .bAlternateSetting = */      0,
        /* .bNumEndpoints = */          2,
        /* .bInterfaceClass = */        8 /* Mass Storage */,
        /* .bInterfaceSubClass = */     6 /* SCSI transparent command set */,
        /* .bInterfaceProtocol = */     0x50 /* Bulk-Only Transport */,
        /* .iInterface = */             0
    },
    /* .pvMore = */     NULL,
    /* .pvClass = */    NULL,
    /* .cbClass = */    0,
    &g_aUsbMsdEndpointDescsSS[0],
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBINTERFACE g_aUsbMsdInterfacesFS[] =
{
    { &g_UsbMsdInterfaceDescFS, /* .cSettings = */ 1 },
};

static const VUSBINTERFACE g_aUsbMsdInterfacesHS[] =
{
    { &g_UsbMsdInterfaceDescHS, /* .cSettings = */ 1 },
};

static const VUSBINTERFACE g_aUsbMsdInterfacesSS[] =
{
    { &g_UsbMsdInterfaceDescSS, /* .cSettings = */ 1 },
};

static const VUSBDESCCONFIGEX g_UsbMsdConfigDescFS =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbMsdInterfacesFS),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbMsdInterfacesFS[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCCONFIGEX g_UsbMsdConfigDescHS =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbMsdInterfacesHS),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbMsdInterfacesHS[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCCONFIGEX g_UsbMsdConfigDescSS =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbMsdInterfacesSS),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbMsdInterfacesSS[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCDEVICE g_UsbMsdDeviceDesc20 =
{
    /* .bLength = */                sizeof(g_UsbMsdDeviceDesc20),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x200, /* USB 2.0 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        64,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBMSD_PID_HD,
    /* .bcdDevice = */              0x0100, /* 1.0 */
    /* .iManufacturer = */          USBMSD_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBMSD_STR_ID_PRODUCT_HD,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDESCDEVICE g_UsbCdDeviceDesc20 =
{
    /* .bLength = */                sizeof(g_UsbCdDeviceDesc20),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x200, /* USB 2.0 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        64,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBMSD_PID_CD,
    /* .bcdDevice = */              0x0100, /* 1.0 */
    /* .iManufacturer = */          USBMSD_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBMSD_STR_ID_PRODUCT_CDROM,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDESCDEVICE g_UsbMsdDeviceDesc30 =
{
    /* .bLength = */                sizeof(g_UsbMsdDeviceDesc30),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x300, /* USB 2.0 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        9 /* 512, the only option for USB3. */,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBMSD_PID_HD,
    /* .bcdDevice = */              0x0110, /* 1.10 */
    /* .iManufacturer = */          USBMSD_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBMSD_STR_ID_PRODUCT_HD,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDESCDEVICE g_UsbCdDeviceDesc30 =
{
    /* .bLength = */                sizeof(g_UsbCdDeviceDesc30),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x300, /* USB 2.0 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        9 /* 512, the only option for USB3. */,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBMSD_PID_CD,
    /* .bcdDevice = */              0x0110, /* 1.10 */
    /* .iManufacturer = */          USBMSD_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBMSD_STR_ID_PRODUCT_CDROM,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDEVICEQUALIFIER g_UsbMsdDeviceQualifier =
{
    /* .bLength = */                sizeof(g_UsbMsdDeviceQualifier),
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
} g_UsbMsdBOS =
{
    {
        /* .bLength = */                sizeof(g_UsbMsdBOS.bos),
        /* .bDescriptorType = */        VUSB_DT_BOS,
        /* .wTotalLength = */           sizeof(g_UsbMsdBOS),
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

static const PDMUSBDESCCACHE g_UsbMsdDescCacheFS =
{
    /* .pDevice = */                &g_UsbMsdDeviceDesc20,
    /* .paConfigs = */              &g_UsbMsdConfigDescFS,
    /* .paLanguages = */            g_aUsbMsdLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbMsdLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbCdDescCacheFS =
{
    /* .pDevice = */                &g_UsbCdDeviceDesc20,
    /* .paConfigs = */              &g_UsbMsdConfigDescFS,
    /* .paLanguages = */            g_aUsbMsdLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbMsdLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbMsdDescCacheHS =
{
    /* .pDevice = */                &g_UsbMsdDeviceDesc20,
    /* .paConfigs = */              &g_UsbMsdConfigDescHS,
    /* .paLanguages = */            g_aUsbMsdLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbMsdLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbCdDescCacheHS =
{
    /* .pDevice = */                &g_UsbCdDeviceDesc20,
    /* .paConfigs = */              &g_UsbMsdConfigDescHS,
    /* .paLanguages = */            g_aUsbMsdLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbMsdLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbMsdDescCacheSS =
{
    /* .pDevice = */                &g_UsbMsdDeviceDesc30,
    /* .paConfigs = */              &g_UsbMsdConfigDescSS,
    /* .paLanguages = */            g_aUsbMsdLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbMsdLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbCdDescCacheSS =
{
    /* .pDevice = */                &g_UsbCdDeviceDesc30,
    /* .paConfigs = */              &g_UsbMsdConfigDescSS,
    /* .paLanguages = */            g_aUsbMsdLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbMsdLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  usbMsdHandleBulkDevToHost(PUSBMSD pThis, PUSBMSDEP pEp, PVUSBURB pUrb);


/**
 * Initializes an URB queue.
 *
 * @param   pQueue              The URB queue.
 */
static void usbMsdQueueInit(PUSBMSDURBQUEUE pQueue)
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
DECLINLINE(void) usbMsdQueueAddTail(PUSBMSDURBQUEUE pQueue, PVUSBURB pUrb)
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
DECLINLINE(PVUSBURB) usbMsdQueueRemoveHead(PUSBMSDURBQUEUE pQueue)
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
DECLINLINE(bool) usbMsdQueueRemove(PUSBMSDURBQUEUE pQueue, PVUSBURB pUrb)
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


#ifdef VBOX_STRICT
/**
 * Checks if the queue is empty or not.
 *
 * @returns true if it is, false if it isn't.
 * @param   pQueue              The URB queue.
 */
DECLINLINE(bool) usbMsdQueueIsEmpty(PCUSBMSDURBQUEUE pQueue)
{
    return pQueue->pHead == NULL;
}
#endif /* VBOX_STRICT */


/**
 * Links an URB into the done queue.
 *
 * @param   pThis               The MSD instance.
 * @param   pUrb                The URB.
 */
static void usbMsdLinkDone(PUSBMSD pThis, PVUSBURB pUrb)
{
    usbMsdQueueAddTail(&pThis->DoneQueue, pUrb);

    if (pThis->fHaveDoneQueueWaiter)
    {
        int rc = RTSemEventSignal(pThis->hEvtDoneQueue);
        AssertRC(rc);
    }
}




/**
 * Allocates a new request and does basic init.
 *
 * @returns Pointer to the new request.  NULL if we're out of memory.
 * @param   pThis               The MSD instance.
 */
static PUSBMSDREQ usbMsdReqAlloc(PUSBMSD pThis)
{
    PUSBMSDREQ pReq = NULL;
    PDMMEDIAEXIOREQ hIoReq = NULL;

    int rc = pThis->Lun0.pIMediaEx->pfnIoReqAlloc(pThis->Lun0.pIMediaEx, &hIoReq, (void **)&pReq,
                                                  0 /* uTag */, PDMIMEDIAEX_F_DEFAULT);
    if (RT_SUCCESS(rc))
    {
        pReq->hIoReq         = hIoReq;
        pReq->enmState       = USBMSDREQSTATE_READY;
        pReq->iScsiReqStatus = 0xff;
    }
    else
        LogRel(("usbMsdReqAlloc: Out of memory (%Rrc)\n", rc));

    return pReq;
}


/**
 * Frees a request.
 *
 * @param   pThis               The MSD instance.
 * @param   pReq                The request.
 */
static void usbMsdReqFree(PUSBMSD pThis, PUSBMSDREQ pReq)
{
    /*
     * Check the input.
     */
    AssertReturnVoid(    pReq->enmState > USBMSDREQSTATE_INVALID
                     &&  pReq->enmState != USBMSDREQSTATE_EXECUTING
                     &&  pReq->enmState < USBMSDREQSTATE_END);
    PPDMUSBINS pUsbIns = pThis->pUsbIns;
    AssertPtrReturnVoid(pUsbIns);
    AssertReturnVoid(PDM_VERSION_ARE_COMPATIBLE(pUsbIns->u32Version, PDM_USBINS_VERSION));

    /*
     * Invalidate it and free the associated resources.
     */
    pReq->enmState = USBMSDREQSTATE_INVALID;
    pReq->cbBuf    = 0;
    pReq->offBuf   = 0;

    if (pReq->pbBuf)
    {
        PDMUsbHlpMMHeapFree(pUsbIns, pReq->pbBuf);
        pReq->pbBuf = NULL;
    }

    int rc = pThis->Lun0.pIMediaEx->pfnIoReqFree(pThis->Lun0.pIMediaEx, pReq->hIoReq);
    AssertRC(rc);
}


/**
 * Prepares a request for execution or data buffering.
 *
 * @param   pReq                The request.
 * @param   pCbw                The SCSI command block wrapper.
 */
static void usbMsdReqPrepare(PUSBMSDREQ pReq, PCUSBCBW pCbw)
{
    /* Copy the CBW */
    uint8_t bCBWLen = RT_MIN(pCbw->bCBWCBLength, sizeof(pCbw->CBWCB));
    size_t cbCopy = RT_UOFFSETOF_DYN(USBCBW, CBWCB[bCBWLen]);
    memcpy(&pReq->Cbw, pCbw, cbCopy);
    memset((uint8_t *)&pReq->Cbw + cbCopy, 0, sizeof(pReq->Cbw) - cbCopy);

    /* Setup the SCSI request. */
    pReq->offBuf         = 0;
    pReq->iScsiReqStatus = 0xff;
}


/**
 * Makes sure that there is sufficient buffer space available.
 *
 * @returns Success indicator (true/false)
 * @param   pThis               The MSD instance.
 * @param   pReq                The request.
 * @param   cbBuf               The required buffer space.
 */
static int usbMsdReqEnsureBuffer(PUSBMSD pThis, PUSBMSDREQ pReq, uint32_t cbBuf)
{
    if (RT_LIKELY(pReq->cbBuf >= cbBuf))
        RT_BZERO(pReq->pbBuf, cbBuf);
    else
    {
        PDMUsbHlpMMHeapFree(pThis->pUsbIns, pReq->pbBuf);
        pReq->cbBuf = 0;

        cbBuf = RT_ALIGN_Z(cbBuf, 0x1000);
        pReq->pbBuf = (uint8_t *)PDMUsbHlpMMHeapAllocZ(pThis->pUsbIns, cbBuf);
        if (!pReq->pbBuf)
            return false;

        pReq->cbBuf = cbBuf;
    }
    return true;
}


/**
 * Completes the URB with a stalled state, halting the pipe.
 */
static int usbMsdCompleteStall(PUSBMSD pThis, PUSBMSDEP pEp, PVUSBURB pUrb, const char *pszWhy)
{
    RT_NOREF(pszWhy);
    Log(("usbMsdCompleteStall/#%u: pUrb=%p:%s: %s\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc, pszWhy));

    pUrb->enmStatus = VUSBSTATUS_STALL;

    /** @todo figure out if the stall is global or pipe-specific or both. */
    if (pEp)
        pEp->fHalted = true;
    else
    {
        pThis->aEps[1].fHalted = true;
        pThis->aEps[2].fHalted = true;
    }

    usbMsdLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}


/**
 * Completes the URB with a OK state.
 */
static int usbMsdCompleteOk(PUSBMSD pThis, PVUSBURB pUrb, size_t cbData)
{
    Log(("usbMsdCompleteOk/#%u: pUrb=%p:%s cbData=%#zx\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc, cbData));

    pUrb->enmStatus = VUSBSTATUS_OK;
    pUrb->cbData    = (uint32_t)cbData;

    usbMsdLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}


/**
 * Reset worker for usbMsdUsbReset, usbMsdUsbSetConfiguration and
 * usbMsdUrbHandleDefaultPipe.
 *
 * @returns VBox status code.
 * @param   pThis               The MSD instance.
 * @param   pUrb                Set when usbMsdUrbHandleDefaultPipe is the
 *                              caller.
 * @param   fSetConfig          Set when usbMsdUsbSetConfiguration is the
 *                              caller.
 */
static int usbMsdResetWorker(PUSBMSD pThis, PVUSBURB pUrb, bool fSetConfig)
{
    /*
     * Wait for the any command currently executing to complete before
     * resetting.  (We cannot cancel its execution.)  How we do this depends
     * on the reset method.
     */
    PUSBMSDREQ pReq = pThis->pReq;
    if (   pReq
        && pReq->enmState == USBMSDREQSTATE_EXECUTING)
    {
        /* Don't try to deal with the set config variant nor multiple build-only
           mass storage resets. */
        if (pThis->pResetUrb && (pUrb || fSetConfig))
        {
            Log(("usbMsdResetWorker: pResetUrb is already %p:%s - stalling\n", pThis->pResetUrb, pThis->pResetUrb->pszDesc));
            return usbMsdCompleteStall(pThis, NULL, pUrb, "pResetUrb");
        }

        /* Bulk-Only Mass Storage Reset: Complete the reset on request completion. */
        if (pUrb)
        {
            pThis->pResetUrb = pUrb;
            Log(("usbMsdResetWorker: Setting pResetUrb to %p:%s\n", pThis->pResetUrb, pThis->pResetUrb->pszDesc));
            return VINF_SUCCESS;
        }

        /* Device reset: Wait for up to 10 ms.  If it doesn't work, ditch
           whole the request structure.  We'll allocate a new one when needed. */
        Log(("usbMsdResetWorker: Waiting for completion...\n"));
        Assert(!pThis->fSignalResetSem);
        pThis->fSignalResetSem = true;
        RTSemEventMultiReset(pThis->hEvtReset);
        RTCritSectLeave(&pThis->CritSect);

        int rc = RTSemEventMultiWait(pThis->hEvtReset, 10 /*ms*/);

        RTCritSectEnter(&pThis->CritSect);
        pThis->fSignalResetSem = false;
        if (    RT_FAILURE(rc)
            ||  pReq->enmState == USBMSDREQSTATE_EXECUTING)
        {
            Log(("usbMsdResetWorker: Didn't complete, ditching the current request (%p)!\n", pReq));
            Assert(pReq == pThis->pReq);
            pReq->enmState = USBMSDREQSTATE_DESTROY_ON_COMPLETION;
            pThis->pReq = NULL;
            pReq = NULL;
        }
    }

    /*
     * Reset the request and device state.
     */
    if (pReq)
    {
        pReq->enmState       = USBMSDREQSTATE_READY;
        pReq->iScsiReqStatus = 0xff;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aEps); i++)
        pThis->aEps[i].fHalted = false;

    if (!pUrb && !fSetConfig) /* (only device reset) */
        pThis->bConfigurationValue = 0; /* default */

    /*
     * Ditch all pending URBs.
     */
    PVUSBURB pCurUrb;
    while ((pCurUrb = usbMsdQueueRemoveHead(&pThis->ToHostQueue)) != NULL)
    {
        pCurUrb->enmStatus = VUSBSTATUS_CRC;
        usbMsdLinkDone(pThis, pCurUrb);
    }

    pCurUrb = pThis->pResetUrb;
    if (pCurUrb)
    {
        pThis->pResetUrb = NULL;
        pCurUrb->enmStatus  = VUSBSTATUS_CRC;
        usbMsdLinkDone(pThis, pCurUrb);
    }

    if (pUrb)
        return usbMsdCompleteOk(pThis, pUrb, 0);
    return VINF_SUCCESS;
}


/**
 * Process a completed request.
 *
 * @param   pThis               The MSD instance.
 * @param   pReq                The request.
 * @param   rcReq               The completion status.
 */
static void usbMsdReqComplete(PUSBMSD pThis, PUSBMSDREQ pReq, int rcReq)
{
    RT_NOREF1(rcReq);

    Log(("usbMsdLun0IoReqCompleteNotify: pReq=%p dCBWTag=%#x iScsiReqStatus=%u \n", pReq, pReq->Cbw.dCBWTag, pReq->iScsiReqStatus));
    RTCritSectEnter(&pThis->CritSect);

    if (pReq->enmState != USBMSDREQSTATE_DESTROY_ON_COMPLETION)
    {
        Assert(pReq->enmState == USBMSDREQSTATE_EXECUTING);
        Assert(pThis->pReq == pReq);

        /*
         * Advance the state machine.  The state machine is not affected by
         * SCSI errors.
         */
        if ((pReq->Cbw.bmCBWFlags & USBCBW_DIR_MASK) == USBCBW_DIR_OUT)
        {
            pReq->enmState = USBMSDREQSTATE_STATUS;
            Log(("usbMsdLun0IoReqCompleteNotify: Entering STATUS\n"));
        }
        else
        {
            pReq->enmState = USBMSDREQSTATE_DATA_TO_HOST;
            Log(("usbMsdLun0IoReqCompleteNotify: Entering DATA_TO_HOST\n"));
        }

        /*
         * Deal with pending to-host URBs.
         */
        for (;;)
        {
            PVUSBURB pUrb = usbMsdQueueRemoveHead(&pThis->ToHostQueue);
            if (!pUrb)
                break;

            /* Process it the normal way. */
            usbMsdHandleBulkDevToHost(pThis, &pThis->aEps[1], pUrb);
        }
    }
    else
    {
        Log(("usbMsdLun0IoReqCompleteNotify: freeing %p\n", pReq));
        usbMsdReqFree(pThis, pReq);
    }

    if (pThis->fSignalResetSem)
        RTSemEventMultiSignal(pThis->hEvtReset);

    if (pThis->pResetUrb)
    {
        pThis->pResetUrb = NULL;
        usbMsdResetWorker(pThis, pThis->pResetUrb, false /*fSetConfig*/);
    }

    RTCritSectLeave(&pThis->CritSect);
}


/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyFromBuf}
 */
static DECLCALLBACK(int) usbMsdLun0IoReqCopyFromBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                    void *pvIoReqAlloc, uint32_t offDst, PRTSGBUF pSgBuf,
                                                    size_t cbCopy)
{
    RT_NOREF2(pInterface, hIoReq);
    int rc = VINF_SUCCESS;
    PUSBMSDREQ pReq = (PUSBMSDREQ)pvIoReqAlloc;

    if (RT_UNLIKELY(offDst + cbCopy > pReq->cbBuf))
        rc = VERR_PDM_MEDIAEX_IOBUF_OVERFLOW;
    else
    {
        size_t cbCopied = RTSgBufCopyToBuf(pSgBuf, pReq->pbBuf + offDst, cbCopy);
        Assert(cbCopied == cbCopy); RT_NOREF(cbCopied);
    }

    return rc;
}


/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyToBuf}
 */
static DECLCALLBACK(int) usbMsdLun0IoReqCopyToBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                  void *pvIoReqAlloc, uint32_t offSrc, PRTSGBUF pSgBuf,
                                                  size_t cbCopy)
{
    RT_NOREF2(pInterface, hIoReq);
    int rc = VINF_SUCCESS;
    PUSBMSDREQ pReq = (PUSBMSDREQ)pvIoReqAlloc;

    if (RT_UNLIKELY(offSrc + cbCopy > pReq->cbBuf))
        rc = VERR_PDM_MEDIAEX_IOBUF_UNDERRUN;
    else
    {
        size_t cbCopied = RTSgBufCopyFromBuf(pSgBuf, pReq->pbBuf + offSrc, cbCopy);
        Assert(cbCopied == cbCopy); RT_NOREF(cbCopied);
    }

    return rc;
}


/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCompleteNotify}
 */
static DECLCALLBACK(int) usbMsdLun0IoReqCompleteNotify(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                       void *pvIoReqAlloc, int rcReq)
{
    RT_NOREF1(hIoReq);
    PUSBMSD pThis = RT_FROM_MEMBER(pInterface, USBMSD, Lun0.IMediaExPort);
    PUSBMSDREQ pReq = (PUSBMSDREQ)pvIoReqAlloc;

    usbMsdReqComplete(pThis, pReq, rcReq);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqStateChanged}
 */
static DECLCALLBACK(void) usbMsdLun0IoReqStateChanged(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                      void *pvIoReqAlloc, PDMMEDIAEXIOREQSTATE enmState)
{
    RT_NOREF4(pInterface, hIoReq, pvIoReqAlloc, enmState);
    AssertLogRelMsgFailed(("This should not be hit because I/O requests should not be suspended\n"));
}


/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnMediumEjected}
 */
static DECLCALLBACK(void) usbMsdLun0MediumEjected(PPDMIMEDIAEXPORT pInterface)
{
    RT_NOREF1(pInterface); /** @todo */
}


/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation}
 */
static DECLCALLBACK(int) usbMsdLun0QueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                       uint32_t *piInstance, uint32_t *piLUN)
{
    PUSBMSD    pThis = RT_FROM_MEMBER(pInterface, USBMSD, Lun0.IMediaPort);
    PPDMUSBINS pUsbIns = pThis->pUsbIns;

    AssertPtrReturn(ppcszController, VERR_INVALID_POINTER);
    AssertPtrReturn(piInstance, VERR_INVALID_POINTER);
    AssertPtrReturn(piLUN, VERR_INVALID_POINTER);

    *ppcszController = pUsbIns->pReg->szName;
    *piInstance = pUsbIns->iInstance;
    *piLUN = 0;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) usbMsdLun0QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PUSBMSD pThis = RT_FROM_MEMBER(pInterface, USBMSD, Lun0.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->Lun0.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT, &pThis->Lun0.IMediaPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEXPORT, &pThis->Lun0.IMediaExPort);
    return NULL;
}


/**
 * Checks if all asynchronous I/O is finished.
 *
 * Used by usbMsdVMReset, usbMsdVMSuspend and usbMsdVMPowerOff.
 *
 * @returns true if quiesced, false if busy.
 * @param   pUsbIns         The USB device instance.
 */
static bool usbMsdAllAsyncIOIsFinished(PPDMUSBINS pUsbIns)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);

    if (   RT_VALID_PTR(pThis->pReq)
        && pThis->pReq->enmState == USBMSDREQSTATE_EXECUTING)
        return false;

    return true;
}

/**
 * @callback_method_impl{FNPDMDEVASYNCNOTIFY,
 * Callback employed by usbMsdVMSuspend and usbMsdVMPowerOff.}
 */
static DECLCALLBACK(bool) usbMsdIsAsyncSuspendOrPowerOffDone(PPDMUSBINS pUsbIns)
{
    if (!usbMsdAllAsyncIOIsFinished(pUsbIns))
        return false;

    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    ASMAtomicWriteBool(&pThis->fSignalIdle, false);
    return true;
}

/**
 * Common worker for usbMsdVMSuspend and usbMsdVMPowerOff.
 */
static void usbMsdSuspendOrPowerOff(PPDMUSBINS pUsbIns)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);

    ASMAtomicWriteBool(&pThis->fSignalIdle, true);
    if (!usbMsdAllAsyncIOIsFinished(pUsbIns))
        PDMUsbHlpSetAsyncNotification(pUsbIns, usbMsdIsAsyncSuspendOrPowerOffDone);
    else
    {
        ASMAtomicWriteBool(&pThis->fSignalIdle, false);

        if (pThis->pReq)
        {
            usbMsdReqFree(pThis, pThis->pReq);
            pThis->pReq = NULL;
        }
    }

    if (pThis->Lun0.pIMediaEx)
        pThis->Lun0.pIMediaEx->pfnNotifySuspend(pThis->Lun0.pIMediaEx);
}


/* -=-=-=-=- Saved State -=-=-=-=- */

/**
 * @callback_method_impl{FNSSMUSBSAVEPREP}
 */
static DECLCALLBACK(int) usbMsdSavePrep(PPDMUSBINS pUsbIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
#ifdef VBOX_STRICT
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    Assert(usbMsdAllAsyncIOIsFinished(pUsbIns));
    Assert(usbMsdQueueIsEmpty(&pThis->ToHostQueue));
    Assert(usbMsdQueueIsEmpty(&pThis->DoneQueue));
#else
    RT_NOREF(pUsbIns);
#endif
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMUSBLOADPREP}
 */
static DECLCALLBACK(int) usbMsdLoadPrep(PPDMUSBINS pUsbIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
#ifdef VBOX_STRICT
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    Assert(usbMsdAllAsyncIOIsFinished(pUsbIns));
    Assert(usbMsdQueueIsEmpty(&pThis->ToHostQueue));
    Assert(usbMsdQueueIsEmpty(&pThis->DoneQueue));
#else
    RT_NOREF(pUsbIns);
#endif
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMUSBLIVEEXEC}
 */
static DECLCALLBACK(int) usbMsdLiveExec(PPDMUSBINS pUsbIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RT_NOREF(uPass);
    PUSBMSD     pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;

    /* config. */
    pHlp->pfnSSMPutBool(pSSM, pThis->Lun0.pIBase != NULL);
    return VINF_SSM_DONT_CALL_AGAIN;
}

/**
 * @callback_method_impl{FNSSMUSBSAVEEXEC}
 */
static DECLCALLBACK(int) usbMsdSaveExec(PPDMUSBINS pUsbIns, PSSMHANDLE pSSM)
{
    PUSBMSD     pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;

    /* The config */
    int rc = usbMsdLiveExec(pUsbIns, pSSM, SSM_PASS_FINAL);
    AssertRCReturn(rc, rc);

    pHlp->pfnSSMPutU8(pSSM, pThis->bConfigurationValue);
    pHlp->pfnSSMPutBool(pSSM, pThis->aEps[0].fHalted);
    pHlp->pfnSSMPutBool(pSSM, pThis->aEps[1].fHalted);
    pHlp->pfnSSMPutBool(pSSM, pThis->aEps[2].fHalted);
    pHlp->pfnSSMPutBool(pSSM, pThis->pReq != NULL);

    if (pThis->pReq)
    {
        PUSBMSDREQ pReq = pThis->pReq;

        pHlp->pfnSSMPutU32(pSSM, pReq->enmState);
        pHlp->pfnSSMPutU32(pSSM, pReq->cbBuf);
        if (pReq->cbBuf)
        {
            AssertPtr(pReq->pbBuf);
            pHlp->pfnSSMPutMem(pSSM, pReq->pbBuf, pReq->cbBuf);
        }

        pHlp->pfnSSMPutU32(pSSM, pReq->offBuf);
        pHlp->pfnSSMPutMem(pSSM, &pReq->Cbw, sizeof(pReq->Cbw));
        pHlp->pfnSSMPutU8(pSSM, pReq->iScsiReqStatus);
    }

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}

/**
 * @callback_method_impl{FNSSMUSBLOADEXEC}
 */
static DECLCALLBACK(int) usbMsdLoadExec(PPDMUSBINS pUsbIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PUSBMSD     pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;

    if (uVersion > USB_MSD_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* Verify config. */
    bool fInUse;
    int rc = pHlp->pfnSSMGetBool(pSSM, &fInUse);
    AssertRCReturn(rc, rc);
    if (fInUse != (pThis->Lun0.pIBase != NULL))
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("The %s VM is missing a USB mass storage device. Please make sure the source and target VMs have compatible storage configurations"),
                                       fInUse ? "target" : "source");

    if (uPass == SSM_PASS_FINAL)
    {
        /* Restore data. */
        Assert(!pThis->pReq);

        pHlp->pfnSSMGetU8(pSSM, &pThis->bConfigurationValue);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aEps[0].fHalted);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aEps[1].fHalted);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aEps[2].fHalted);
        bool fReqAlloc = false;
        rc = pHlp->pfnSSMGetBool(pSSM, &fReqAlloc);
        AssertRCReturn(rc, rc);
        if (fReqAlloc)
        {
            PUSBMSDREQ pReq = usbMsdReqAlloc(pThis);
            AssertReturn(pReq, VERR_NO_MEMORY);
            pThis->pReq = pReq;

            AssertCompile(sizeof(pReq->enmState) == sizeof(uint32_t));
            pHlp->pfnSSMGetU32(pSSM, (uint32_t *)&pReq->enmState);

            uint32_t cbBuf = 0;
            rc = pHlp->pfnSSMGetU32(pSSM, &cbBuf);
            AssertRCReturn(rc, rc);
            if (cbBuf)
            {
                if (usbMsdReqEnsureBuffer(pThis, pReq, cbBuf))
                {
                    AssertPtr(pReq->pbBuf);
                    Assert(cbBuf == pReq->cbBuf);
                    pHlp->pfnSSMGetMem(pSSM, pReq->pbBuf, pReq->cbBuf);
                }
                else
                    return VERR_NO_MEMORY;
            }

            pHlp->pfnSSMGetU32(pSSM, &pReq->offBuf);
            pHlp->pfnSSMGetMem(pSSM, &pReq->Cbw, sizeof(pReq->Cbw));

            if (uVersion > USB_MSD_SAVED_STATE_VERSION_PRE_CLEANUP)
                rc = pHlp->pfnSSMGetU8(pSSM, &pReq->iScsiReqStatus);
            else
            {
                int32_t iScsiReqStatus;

                /* Skip old fields which are unused now or can be determined from the CBW. */
                pHlp->pfnSSMSkip(pSSM, 4 * 4 + 64);
                rc = pHlp->pfnSSMGetS32(pSSM, &iScsiReqStatus);
                pReq->iScsiReqStatus = (uint8_t)iScsiReqStatus;
            }
            AssertRCReturn(rc, rc);
        }

        uint32_t u32;
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbReap}
 */
static DECLCALLBACK(PVUSBURB) usbMsdUrbReap(PPDMUSBINS pUsbIns, RTMSINTERVAL cMillies)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdUrbReap/#%u: cMillies=%u\n", pUsbIns->iInstance, cMillies));

    RTCritSectEnter(&pThis->CritSect);

    PVUSBURB pUrb = usbMsdQueueRemoveHead(&pThis->DoneQueue);
    if (!pUrb && cMillies)
    {
        /* Wait */
        pThis->fHaveDoneQueueWaiter = true;
        RTCritSectLeave(&pThis->CritSect);

        RTSemEventWait(pThis->hEvtDoneQueue, cMillies);

        RTCritSectEnter(&pThis->CritSect);
        pThis->fHaveDoneQueueWaiter = false;

        pUrb = usbMsdQueueRemoveHead(&pThis->DoneQueue);
    }

    RTCritSectLeave(&pThis->CritSect);

    if (pUrb)
        Log(("usbMsdUrbReap/#%u: pUrb=%p:%s\n", pUsbIns->iInstance, pUrb, pUrb->pszDesc));
    return pUrb;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnWakeup}
 */
static DECLCALLBACK(int) usbMsdWakeup(PPDMUSBINS pUsbIns)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdUrbReap/#%u:\n", pUsbIns->iInstance));

    return RTSemEventSignal(pThis->hEvtDoneQueue);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbCancel}
 */
static DECLCALLBACK(int) usbMsdUrbCancel(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdUrbCancel/#%u: pUrb=%p:%s\n", pUsbIns->iInstance, pUrb, pUrb->pszDesc));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Remove the URB from the to-host queue and move it onto the done queue.
     */
    if (usbMsdQueueRemove(&pThis->ToHostQueue, pUrb))
        usbMsdLinkDone(pThis, pUrb);

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * Wrapper around  PDMISCSICONNECTOR::pfnSCSIRequestSend that deals with
 * SCSI_REQUEST_SENSE.
 *
 * @returns VBox status code.
 * @param   pThis               The MSD instance data.
 * @param   pReq                The MSD request.
 * @param   pszCaller           Where we're called from.
 */
static int usbMsdSubmitScsiCommand(PUSBMSD pThis, PUSBMSDREQ pReq, const char *pszCaller)
{
    RT_NOREF(pszCaller);
    Log(("%s: Entering EXECUTING (dCBWTag=%#x).\n", pszCaller, pReq->Cbw.dCBWTag));
    Assert(pReq == pThis->pReq);
    pReq->enmState = USBMSDREQSTATE_EXECUTING;

    PDMMEDIAEXIOREQSCSITXDIR enmTxDir = pReq->Cbw.dCBWDataTransferLength == 0
                                      ? PDMMEDIAEXIOREQSCSITXDIR_NONE
                                      :   (pReq->Cbw.bmCBWFlags & USBCBW_DIR_MASK) == USBCBW_DIR_OUT
                                        ? PDMMEDIAEXIOREQSCSITXDIR_TO_DEVICE
                                        : PDMMEDIAEXIOREQSCSITXDIR_FROM_DEVICE;

    return pThis->Lun0.pIMediaEx->pfnIoReqSendScsiCmd(pThis->Lun0.pIMediaEx, pReq->hIoReq, pReq->Cbw.bCBWLun,
                                                      &pReq->Cbw.CBWCB[0], pReq->Cbw.bCBWCBLength, enmTxDir, NULL,
                                                      pReq->Cbw.dCBWDataTransferLength, NULL, 0, NULL,
                                                      &pReq->iScsiReqStatus, 20 * RT_MS_1SEC);
}


/**
 * Handle requests sent to the outbound (to device) bulk pipe.
 */
static int usbMsdHandleBulkHostToDev(PUSBMSD pThis, PUSBMSDEP pEp, PVUSBURB pUrb)
{
    /*
     * Stall the request if the pipe is halted.
     */
    if (RT_UNLIKELY(pEp->fHalted))
        return usbMsdCompleteStall(pThis, NULL, pUrb, "Halted pipe");

    /*
     * Deal with the URB according to the current state.
     */
    PUSBMSDREQ      pReq     = pThis->pReq;
    USBMSDREQSTATE  enmState = pReq ? pReq->enmState : USBMSDREQSTATE_READY;
    switch (enmState)
    {
        case USBMSDREQSTATE_STATUS:
            LogFlow(("usbMsdHandleBulkHostToDev: Skipping pending status.\n"));
            pReq->enmState = USBMSDREQSTATE_READY;
            RT_FALL_THRU();

        /*
         * We're ready to receive a command.  Start off by validating the
         * incoming request.
         */
        case USBMSDREQSTATE_READY:
        {
            PCUSBCBW pCbw = (PUSBCBW)&pUrb->abData[0];
            if (pUrb->cbData < RT_UOFFSETOF(USBCBW, CBWCB[1]))
            {
                Log(("usbMsd: Bad CBW: cbData=%#x < min=%#x\n", pUrb->cbData, RT_UOFFSETOF(USBCBW, CBWCB[1]) ));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "BAD CBW");
            }
            if (pCbw->dCBWSignature != USBCBW_SIGNATURE)
            {
                Log(("usbMsd: CBW: Invalid dCBWSignature value: %#x\n", pCbw->dCBWSignature));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad CBW");
            }
            Log(("usbMsd: CBW: dCBWTag=%#x dCBWDataTransferLength=%#x bmCBWFlags=%#x bCBWLun=%#x bCBWCBLength=%#x  cbData=%#x fShortNotOk=%RTbool\n",
                 pCbw->dCBWTag, pCbw->dCBWDataTransferLength, pCbw->bmCBWFlags, pCbw->bCBWLun, pCbw->bCBWCBLength, pUrb->cbData, pUrb->fShortNotOk));
            if (pCbw->bmCBWFlags & ~USBCBW_DIR_MASK)
            {
                Log(("usbMsd: CBW: Bad bmCBWFlags value: %#x\n", pCbw->bmCBWFlags));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad CBW");

            }
            if (pCbw->bCBWLun != 0)
            {
                Log(("usbMsd: CBW: Bad bCBWLun value: %#x\n", pCbw->bCBWLun));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad CBW");
            }
            if ((pCbw->bCBWCBLength == 0) || (pCbw->bCBWCBLength > sizeof(pCbw->CBWCB)))
            {
                Log(("usbMsd: CBW: Bad bCBWCBLength value: %#x\n", pCbw->bCBWCBLength));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad CBW");
            }
            if (pUrb->cbData < RT_UOFFSETOF_DYN(USBCBW, CBWCB[pCbw->bCBWCBLength]))
            {
                Log(("usbMsd: CBW: Mismatching cbData and bCBWCBLength values: %#x vs. %#x (%#x)\n",
                     pUrb->cbData, RT_UOFFSETOF_DYN(USBCBW, CBWCB[pCbw->bCBWCBLength]), pCbw->bCBWCBLength));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad CBW");
            }
            if (pCbw->dCBWDataTransferLength > _1M)
            {
                Log(("usbMsd: CBW: dCBWDataTransferLength is too large: %#x (%u)\n",
                     pCbw->dCBWDataTransferLength, pCbw->dCBWDataTransferLength));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Too big transfer");
            }

            /*
             * Make sure we've got a request and a sufficient buffer space.
             *
             * Note! This will make sure the buffer is ZERO as well, thus
             *       saving us the trouble of clearing the output buffer on
             *       failure later.
             */
            if (!pReq)
            {
                pReq = usbMsdReqAlloc(pThis);
                if (!pReq)
                    return usbMsdCompleteStall(pThis, NULL, pUrb, "Request allocation failure");
                pThis->pReq = pReq;
            }
            if (!usbMsdReqEnsureBuffer(pThis, pReq, pCbw->dCBWDataTransferLength))
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Buffer allocation failure");

            /*
             * Prepare the request.  Kick it off right away if possible.
             */
            usbMsdReqPrepare(pReq, pCbw);

            if (   pReq->Cbw.dCBWDataTransferLength == 0
                || (pReq->Cbw.bmCBWFlags & USBCBW_DIR_MASK) == USBCBW_DIR_IN)
            {
                int rc = usbMsdSubmitScsiCommand(pThis, pReq, "usbMsdHandleBulkHostToDev");
                if (RT_SUCCESS(rc) && rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
                    usbMsdReqComplete(pThis, pReq, rc);
                else if (RT_FAILURE(rc))
                {
                    Log(("usbMsd: Failed sending SCSI request to driver: %Rrc\n", rc));
                    return usbMsdCompleteStall(pThis, NULL, pUrb, "SCSI Submit #1");
                }
            }
            else
            {
                Log(("usbMsdHandleBulkHostToDev: Entering DATA_FROM_HOST.\n"));
                pReq->enmState = USBMSDREQSTATE_DATA_FROM_HOST;
            }

            return usbMsdCompleteOk(pThis, pUrb, pUrb->cbData);
        }

        /*
         * Stuff the data into the buffer.
         */
        case USBMSDREQSTATE_DATA_FROM_HOST:
        {
            uint32_t    cbData = pUrb->cbData;
            uint32_t    cbLeft = pReq->Cbw.dCBWDataTransferLength - pReq->offBuf;
            if (cbData > cbLeft)
            {
                Log(("usbMsd: Too much data: cbData=%#x offBuf=%#x dCBWDataTransferLength=%#x cbLeft=%#x\n",
                     cbData, pReq->offBuf, pReq->Cbw.dCBWDataTransferLength, cbLeft));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Too much data");
            }
            memcpy(&pReq->pbBuf[pReq->offBuf], &pUrb->abData[0], cbData);
            pReq->offBuf += cbData;

            if (pReq->offBuf == pReq->Cbw.dCBWDataTransferLength)
            {
                int rc = usbMsdSubmitScsiCommand(pThis, pReq, "usbMsdHandleBulkHostToDev");
                if (RT_SUCCESS(rc) && rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
                    usbMsdReqComplete(pThis, pReq, rc);
                else if (RT_FAILURE(rc))
                {
                    Log(("usbMsd: Failed sending SCSI request to driver: %Rrc\n", rc));
                    return usbMsdCompleteStall(pThis, NULL, pUrb, "SCSI Submit #2");
                }
            }
            return usbMsdCompleteOk(pThis, pUrb, cbData);
        }

        /*
         * Bad state, stall.
         */
        case USBMSDREQSTATE_DATA_TO_HOST:
            return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad state H2D: DATA_TO_HOST");

        case USBMSDREQSTATE_EXECUTING:
            return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad state H2D: EXECUTING");

        default:
            AssertMsgFailed(("enmState=%d\n", enmState));
            return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad state (H2D)");
    }
}


/**
 * Handle requests sent to the inbound (to host) bulk pipe.
 */
static int usbMsdHandleBulkDevToHost(PUSBMSD pThis, PUSBMSDEP pEp, PVUSBURB pUrb)
{
    /*
     * Stall the request if the pipe is halted OR if there is no
     * pending request yet.
     */
    PUSBMSDREQ pReq = pThis->pReq;
    if (RT_UNLIKELY(pEp->fHalted || !pReq))
        return usbMsdCompleteStall(pThis, NULL, pUrb, pEp->fHalted ? "Halted pipe" : "No request");

    /*
     * Deal with the URB according to the state.
     */
    switch (pReq->enmState)
    {
        /*
         * We've data left to transfer to the host.
         */
        case USBMSDREQSTATE_DATA_TO_HOST:
        {
            uint32_t cbData = pUrb->cbData;
            uint32_t cbCopy = pReq->Cbw.dCBWDataTransferLength - pReq->offBuf;
            if (cbData <= cbCopy)
                cbCopy = cbData;
            else if (pUrb->fShortNotOk)
            {
                Log(("usbMsd: Requested more data that we've got; cbData=%#x offBuf=%#x dCBWDataTransferLength=%#x cbLeft=%#x\n",
                     cbData, pReq->offBuf, pReq->Cbw.dCBWDataTransferLength, cbCopy));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Data underrun");
            }
            memcpy(&pUrb->abData[0], &pReq->pbBuf[pReq->offBuf], cbCopy);
            pReq->offBuf += cbCopy;

            if (pReq->offBuf == pReq->Cbw.dCBWDataTransferLength)
            {
                Log(("usbMsdHandleBulkDevToHost: Entering STATUS\n"));
                pReq->enmState = USBMSDREQSTATE_STATUS;
            }
            return usbMsdCompleteOk(pThis, pUrb, cbCopy);
        }

        /*
         * Status transfer.
         */
        case USBMSDREQSTATE_STATUS:
        {
            if ((pUrb->cbData < sizeof(USBCSW)) || (pUrb->cbData > sizeof(USBCSW) && pUrb->fShortNotOk))
            {
                Log(("usbMsd: Unexpected status request size: %#x (expected %#x), fShortNotOK=%RTbool\n", pUrb->cbData, sizeof(USBCSW), pUrb->fShortNotOk));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Invalid CSW size");
            }

            /* Enter a CSW into the URB data buffer. */
            PUSBCSW pCsw = (PUSBCSW)&pUrb->abData[0];
            pCsw->dCSWSignature = USBCSW_SIGNATURE;
            pCsw->dCSWTag       = pReq->Cbw.dCBWTag;
            pCsw->bCSWStatus    = pReq->iScsiReqStatus == SCSI_STATUS_OK
                                ? USBCSW_STATUS_OK
                                : pReq->iScsiReqStatus < 0xff
                                ? USBCSW_STATUS_FAILED
                                : USBCSW_STATUS_PHASE_ERROR;
            /** @todo the following is not always accurate; VSCSI needs
             *        to implement residual counts properly! */
            if ((pReq->Cbw.bmCBWFlags & USBCBW_DIR_MASK) == USBCBW_DIR_OUT)
                pCsw->dCSWDataResidue = pCsw->bCSWStatus == USBCSW_STATUS_OK
                                      ? 0
                                      : pReq->Cbw.dCBWDataTransferLength;
            else
                pCsw->dCSWDataResidue = pCsw->bCSWStatus == USBCSW_STATUS_OK
                                      ? 0
                                      : pReq->Cbw.dCBWDataTransferLength;
            Log(("usbMsd: CSW: dCSWTag=%#x bCSWStatus=%d dCSWDataResidue=%#x\n",
                 pCsw->dCSWTag, pCsw->bCSWStatus, pCsw->dCSWDataResidue));

            Log(("usbMsdHandleBulkDevToHost: Entering READY\n"));
            pReq->enmState = USBMSDREQSTATE_READY;
            return usbMsdCompleteOk(pThis, pUrb, sizeof(*pCsw));
        }

        /*
         * Status request before we've received all (or even any) data.
         * Linux 2.4.31 does this sometimes.  The recommended behavior is to
         * to accept the current data amount and execute the request.  (The
         * alternative behavior is to stall.)
         */
        case USBMSDREQSTATE_DATA_FROM_HOST:
        {
            if (pUrb->cbData != sizeof(USBCSW))
            {
                Log(("usbMsdHandleBulkDevToHost: DATA_FROM_HOST; cbData=%#x -> stall\n", pUrb->cbData));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "Invalid CSW size");
            }

            int rc = usbMsdSubmitScsiCommand(pThis, pReq, "usbMsdHandleBulkDevToHost");
            if (RT_SUCCESS(rc) && rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
                usbMsdReqComplete(pThis, pReq, rc);
            else if (RT_FAILURE(rc))
            {
                Log(("usbMsd: Failed sending SCSI request to driver: %Rrc\n", rc));
                return usbMsdCompleteStall(pThis, NULL, pUrb, "SCSI Submit #3");
            }
        }
        RT_FALL_THRU();

        /*
         * The SCSI command is still pending, queue the URB awaiting its
         * completion.
         */
        case USBMSDREQSTATE_EXECUTING:
            usbMsdQueueAddTail(&pThis->ToHostQueue, pUrb);
            LogFlow(("usbMsdHandleBulkDevToHost: Added %p:%s to the to-host queue\n", pUrb, pUrb->pszDesc));
            return VINF_SUCCESS;

        /*
         * Bad states, stall.
         */
        case USBMSDREQSTATE_READY:
            Log(("usbMsdHandleBulkDevToHost: enmState=READ(%d) (cbData=%#x)\n", pReq->enmState, pUrb->cbData));
            return usbMsdCompleteStall(pThis, NULL, pUrb, "Bad state D2H: READY");

        default:
            Log(("usbMsdHandleBulkDevToHost: enmState=%d cbData=%#x\n", pReq->enmState, pUrb->cbData));
            return usbMsdCompleteStall(pThis, NULL, pUrb, "Really bad state (D2H)!");
    }
}


/**
 * Handles request send to the default control pipe.
 */
static int usbMsdHandleDefaultPipe(PUSBMSD pThis, PUSBMSDEP pEp, PVUSBURB pUrb)
{
    PVUSBSETUP pSetup = (PVUSBSETUP)&pUrb->abData[0];
    AssertReturn(pUrb->cbData >= sizeof(*pSetup), VERR_VUSB_FAILED_TO_QUEUE_URB);

    if ((pSetup->bmRequestType & VUSB_REQ_MASK) == VUSB_REQ_STANDARD)
    {
        switch (pSetup->bRequest)
        {
            case VUSB_REQ_GET_DESCRIPTOR:
            {
                if (pSetup->bmRequestType != (VUSB_TO_DEVICE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST))
                {
                    Log(("usbMsd: Bad GET_DESCRIPTOR req: bmRequestType=%#x\n", pSetup->bmRequestType));
                    return usbMsdCompleteStall(pThis, pEp, pUrb, "Bad GET_DESCRIPTOR");
                }

                switch (pSetup->wValue >> 8)
                {
                    uint32_t    cbCopy;

                    case VUSB_DT_STRING:
                        Log(("usbMsd: GET_DESCRIPTOR DT_STRING wValue=%#x wIndex=%#x\n", pSetup->wValue, pSetup->wIndex));
                        break;
                    case VUSB_DT_DEVICE_QUALIFIER:
                        Log(("usbMsd: GET_DESCRIPTOR DT_DEVICE_QUALIFIER wValue=%#x wIndex=%#x\n", pSetup->wValue, pSetup->wIndex));
                        /* Returned data is written after the setup message. */
                        cbCopy = pUrb->cbData - sizeof(*pSetup);
                        cbCopy = RT_MIN(cbCopy, sizeof(g_UsbMsdDeviceQualifier));
                        memcpy(&pUrb->abData[sizeof(*pSetup)], &g_UsbMsdDeviceQualifier, cbCopy);
                        return usbMsdCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
                    case VUSB_DT_BOS:
                        Log(("usbMsd: GET_DESCRIPTOR DT_BOS wValue=%#x wIndex=%#x\n", pSetup->wValue, pSetup->wIndex));
                        /* Returned data is written after the setup message. */
                        cbCopy = pUrb->cbData - sizeof(*pSetup);
                        cbCopy = RT_MIN(cbCopy, sizeof(g_UsbMsdBOS));
                        memcpy(&pUrb->abData[sizeof(*pSetup)], &g_UsbMsdBOS, cbCopy);
                        return usbMsdCompleteOk(pThis, pUrb, cbCopy + sizeof(*pSetup));
                    default:
                        Log(("usbMsd: GET_DESCRIPTOR, huh? wValue=%#x wIndex=%#x\n", pSetup->wValue, pSetup->wIndex));
                        break;
                }
                break;
            }

            case VUSB_REQ_CLEAR_FEATURE:
                break;
        }

        /** @todo implement this. */
        Log(("usbMsd: Implement standard request: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n",
             pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue, pSetup->wIndex, pSetup->wLength));

        usbMsdCompleteStall(pThis, pEp, pUrb, "TODO: standard request stuff");
    }
    /* 3.1 Bulk-Only Mass Storage Reset */
    else if (    pSetup->bmRequestType == (VUSB_REQ_CLASS | VUSB_TO_INTERFACE)
             &&  pSetup->bRequest == 0xff
             &&  !pSetup->wValue
             &&  !pSetup->wLength
             &&  pSetup->wIndex == 0)
    {
        Log(("usbMsdHandleDefaultPipe: Bulk-Only Mass Storage Reset\n"));
        return usbMsdResetWorker(pThis, pUrb, false /*fSetConfig*/);
    }
    /* 3.2 Get Max LUN, may stall if we like (but we don't). */
    else if (   pSetup->bmRequestType == (VUSB_REQ_CLASS | VUSB_TO_INTERFACE | VUSB_DIR_TO_HOST)
             &&  pSetup->bRequest == 0xfe
             &&  !pSetup->wValue
             &&  pSetup->wLength == 1
             &&  pSetup->wIndex == 0)
    {
        *(uint8_t *)(pSetup + 1) = 0; /* max lun is 0 */
        usbMsdCompleteOk(pThis, pUrb, 1);
    }
    else
    {
        Log(("usbMsd: Unknown control msg: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n",
             pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue, pSetup->wIndex, pSetup->wLength));
        return usbMsdCompleteStall(pThis, pEp, pUrb, "Unknown control msg");
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbQueue}
 */
static DECLCALLBACK(int) usbMsdQueue(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdQueue/#%u: pUrb=%p:%s EndPt=%#x\n", pUsbIns->iInstance, pUrb, pUrb->pszDesc, pUrb->EndPt));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Parse on a per end-point basis.
     */
    int rc;
    switch (pUrb->EndPt)
    {
        case 0:
            rc = usbMsdHandleDefaultPipe(pThis, &pThis->aEps[0], pUrb);
            break;

        case 0x81:
            AssertFailed();
            RT_FALL_THRU();
        case 0x01:
            rc = usbMsdHandleBulkDevToHost(pThis, &pThis->aEps[1], pUrb);
            break;

        case 0x02:
            rc = usbMsdHandleBulkHostToDev(pThis, &pThis->aEps[2], pUrb);
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
static DECLCALLBACK(int) usbMsdUsbClearHaltedEndpoint(PPDMUSBINS pUsbIns, unsigned uEndpoint)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdUsbClearHaltedEndpoint/#%u: uEndpoint=%#x\n", pUsbIns->iInstance, uEndpoint));

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
static DECLCALLBACK(int) usbMsdUsbSetInterface(PPDMUSBINS pUsbIns, uint8_t bInterfaceNumber, uint8_t bAlternateSetting)
{
    RT_NOREF(pUsbIns, bInterfaceNumber, bAlternateSetting);
    LogFlow(("usbMsdUsbSetInterface/#%u: bInterfaceNumber=%u bAlternateSetting=%u\n", pUsbIns->iInstance, bInterfaceNumber, bAlternateSetting));
    Assert(bAlternateSetting == 0);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbSetConfiguration}
 */
static DECLCALLBACK(int) usbMsdUsbSetConfiguration(PPDMUSBINS pUsbIns, uint8_t bConfigurationValue,
                                                   const void *pvOldCfgDesc, const void *pvOldIfState, const void *pvNewCfgDesc)
{
    RT_NOREF(pvOldCfgDesc, pvOldIfState,  pvNewCfgDesc);
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdUsbSetConfiguration/#%u: bConfigurationValue=%u\n", pUsbIns->iInstance, bConfigurationValue));
    Assert(bConfigurationValue == 1);
    RTCritSectEnter(&pThis->CritSect);

    /*
     * If the same config is applied more than once, it's a kind of reset.
     */
    if (pThis->bConfigurationValue == bConfigurationValue)
        usbMsdResetWorker(pThis, NULL, true /*fSetConfig*/); /** @todo figure out the exact difference */
    pThis->bConfigurationValue = bConfigurationValue;

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbGetDescriptorCache}
 */
static DECLCALLBACK(PCPDMUSBDESCCACHE) usbMsdUsbGetDescriptorCache(PPDMUSBINS pUsbIns)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdUsbGetDescriptorCache/#%u:\n", pUsbIns->iInstance));
    if (pThis->pUsbIns->enmSpeed == VUSB_SPEED_SUPER)
        return pThis->fIsCdrom ? &g_UsbCdDescCacheSS : &g_UsbMsdDescCacheSS;
    else if (pThis->pUsbIns->enmSpeed == VUSB_SPEED_HIGH)
        return pThis->fIsCdrom ? &g_UsbCdDescCacheHS : &g_UsbMsdDescCacheHS;
    else
        return pThis->fIsCdrom ? &g_UsbCdDescCacheFS : &g_UsbMsdDescCacheFS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbReset}
 */
static DECLCALLBACK(int) usbMsdUsbReset(PPDMUSBINS pUsbIns, bool fResetOnLinux)
{
    RT_NOREF(fResetOnLinux);
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdUsbReset/#%u:\n", pUsbIns->iInstance));
    RTCritSectEnter(&pThis->CritSect);

    int rc = usbMsdResetWorker(pThis, NULL, false /*fSetConfig*/);

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnVMSuspend}
 */
static DECLCALLBACK(void) usbMsdVMSuspend(PPDMUSBINS pUsbIns)
{
    LogFlow(("usbMsdVMSuspend/#%u:\n", pUsbIns->iInstance));
    usbMsdSuspendOrPowerOff(pUsbIns);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnVMSuspend}
 */
static DECLCALLBACK(void) usbMsdVMPowerOff(PPDMUSBINS pUsbIns)
{
    LogFlow(("usbMsdVMPowerOff/#%u:\n", pUsbIns->iInstance));
    usbMsdSuspendOrPowerOff(pUsbIns);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnDriverAttach}
 */
static DECLCALLBACK(int) usbMsdDriverAttach(PPDMUSBINS pUsbIns, unsigned iLUN, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);

    LogFlow(("usbMsdDriverAttach/#%u:\n", pUsbIns->iInstance));

    AssertMsg(iLUN == 0, ("UsbMsd: No other LUN than 0 is supported\n"));
    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("UsbMsd: Device does not support hotplugging\n"));

    /* the usual paranoia */
    AssertRelease(!pThis->Lun0.pIBase);
    AssertRelease(!pThis->Lun0.pIMedia);
    AssertRelease(!pThis->Lun0.pIMediaEx);

    /*
     * Try attach the block device and get the interfaces,
     * required as well as optional.
     */
    int rc = PDMUsbHlpDriverAttach(pUsbIns, iLUN, &pThis->Lun0.IBase, &pThis->Lun0.pIBase, NULL);
    if (RT_SUCCESS(rc))
    {
        /* Get media and extended media interface. */
        pThis->Lun0.pIMedia = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pIBase, PDMIMEDIA);
        AssertMsgReturn(pThis->Lun0.pIMedia, ("Missing media interface below\n"), VERR_PDM_MISSING_INTERFACE);
        pThis->Lun0.pIMediaEx = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pIBase, PDMIMEDIAEX);
        AssertMsgReturn(pThis->Lun0.pIMediaEx, ("Missing extended media interface below\n"), VERR_PDM_MISSING_INTERFACE);

        rc = pThis->Lun0.pIMediaEx->pfnIoReqAllocSizeSet(pThis->Lun0.pIMediaEx, sizeof(USBMSDREQ));
        AssertMsgRCReturn(rc, ("MSD failed to set I/O request size!\n"), VERR_PDM_MISSING_INTERFACE);
    }
    else
        AssertMsgFailed(("Failed to attach LUN#%d. rc=%Rrc\n", iLUN, rc));

    if (RT_FAILURE(rc))
    {
        pThis->Lun0.pIBase = NULL;
        pThis->Lun0.pIMedia = NULL;
        pThis->Lun0.pIMediaEx = NULL;
    }

    pThis->fIsCdrom = false;
    PDMMEDIATYPE enmType = pThis->Lun0.pIMedia->pfnGetType(pThis->Lun0.pIMedia);
    /* Anything else will be reported as a hard disk. */
    if (enmType == PDMMEDIATYPE_CDROM || enmType == PDMMEDIATYPE_DVD)
        pThis->fIsCdrom = true;

    return rc;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnDriverDetach}
 */
static DECLCALLBACK(void) usbMsdDriverDetach(PPDMUSBINS pUsbIns, unsigned iLUN, uint32_t fFlags)
{
    RT_NOREF(iLUN, fFlags);
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);

    LogFlow(("usbMsdDriverDetach/#%u:\n", pUsbIns->iInstance));

    AssertMsg(iLUN == 0, ("UsbMsd: No other LUN than 0 is supported\n"));
    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("UsbMsd: Device does not support hotplugging\n"));

    if (pThis->pReq)
    {
        usbMsdReqFree(pThis, pThis->pReq);
        pThis->pReq = NULL;
    }

    /*
     * Zero some important members.
     */
    pThis->Lun0.pIBase = NULL;
    pThis->Lun0.pIMedia = NULL;
    pThis->Lun0.pIMediaEx = NULL;
}


/**
 * @callback_method_impl{FNPDMDEVASYNCNOTIFY,
 * Callback employed by usbMsdVMReset.}
 */
static DECLCALLBACK(bool) usbMsdIsAsyncResetDone(PPDMUSBINS pUsbIns)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);

    if (!usbMsdAllAsyncIOIsFinished(pUsbIns))
        return false;
    ASMAtomicWriteBool(&pThis->fSignalIdle, false);

    int rc = usbMsdResetWorker(pThis, NULL, false /*fSetConfig*/);
    AssertRC(rc);
    return true;
}

/**
 * @interface_method_impl{PDMUSBREG,pfnVMReset}
 */
static DECLCALLBACK(void) usbMsdVMReset(PPDMUSBINS pUsbIns)
{
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);

    ASMAtomicWriteBool(&pThis->fSignalIdle, true);
    if (!usbMsdAllAsyncIOIsFinished(pUsbIns))
        PDMUsbHlpSetAsyncNotification(pUsbIns, usbMsdIsAsyncResetDone);
    else
    {
        ASMAtomicWriteBool(&pThis->fSignalIdle, false);
        int rc = usbMsdResetWorker(pThis, NULL, false /*fSetConfig*/);
        AssertRC(rc);
    }
}


/**
 * @interface_method_impl{PDMUSBREG,pfnDestruct}
 */
static DECLCALLBACK(void) usbMsdDestruct(PPDMUSBINS pUsbIns)
{
    PDMUSB_CHECK_VERSIONS_RETURN_VOID(pUsbIns);
    PUSBMSD pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    LogFlow(("usbMsdDestruct/#%u:\n", pUsbIns->iInstance));

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

    if (pThis->hEvtReset != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(pThis->hEvtReset);
        pThis->hEvtReset = NIL_RTSEMEVENTMULTI;
    }
}


/**
 * @interface_method_impl{PDMUSBREG,pfnConstruct}
 */
static DECLCALLBACK(int) usbMsdConstruct(PPDMUSBINS pUsbIns, int iInstance, PCFGMNODE pCfg, PCFGMNODE pCfgGlobal)
{
    RT_NOREF(pCfgGlobal);
    PDMUSB_CHECK_VERSIONS_RETURN(pUsbIns);
    PUSBMSD     pThis = PDMINS_2_DATA(pUsbIns, PUSBMSD);
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;

    Log(("usbMsdConstruct/#%u:\n", iInstance));

    /*
     * Perform the basic structure initialization first so the destructor
     * will not misbehave.
     */
    pThis->pUsbIns                                      = pUsbIns;
    pThis->hEvtDoneQueue                                = NIL_RTSEMEVENT;
    pThis->hEvtReset                                    = NIL_RTSEMEVENTMULTI;
    pThis->Lun0.IBase.pfnQueryInterface                 = usbMsdLun0QueryInterface;
    pThis->Lun0.IMediaPort.pfnQueryDeviceLocation       = usbMsdLun0QueryDeviceLocation;
    pThis->Lun0.IMediaExPort.pfnIoReqCompleteNotify     = usbMsdLun0IoReqCompleteNotify;
    pThis->Lun0.IMediaExPort.pfnIoReqCopyFromBuf        = usbMsdLun0IoReqCopyFromBuf;
    pThis->Lun0.IMediaExPort.pfnIoReqCopyToBuf          = usbMsdLun0IoReqCopyToBuf;
    pThis->Lun0.IMediaExPort.pfnIoReqQueryDiscardRanges = NULL;
    pThis->Lun0.IMediaExPort.pfnIoReqStateChanged       = usbMsdLun0IoReqStateChanged;
    pThis->Lun0.IMediaExPort.pfnMediumEjected           = usbMsdLun0MediumEjected;
    usbMsdQueueInit(&pThis->ToHostQueue);
    usbMsdQueueInit(&pThis->DoneQueue);

    int rc = RTCritSectInit(&pThis->CritSect);
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pThis->hEvtDoneQueue);
    AssertRCReturn(rc, rc);

    rc = RTSemEventMultiCreate(&pThis->hEvtReset);
    AssertRCReturn(rc, rc);

    /*
     * Validate and read the configuration.
     */
    rc = pHlp->pfnCFGMValidateConfig(pCfg, "/", "", "", "UsbMsd", iInstance);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Attach the SCSI driver.
     */
    rc = PDMUsbHlpDriverAttach(pUsbIns, 0 /*iLun*/, &pThis->Lun0.IBase, &pThis->Lun0.pIBase, "SCSI Port");
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("MSD failed to attach SCSI driver"));
    pThis->Lun0.pIMedia = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pIBase, PDMIMEDIA);
    if (!pThis->Lun0.pIMedia)
        return PDMUsbHlpVMSetError(pUsbIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS,
                                   N_("MSD failed to query the PDMIMEDIA from the driver below it"));
    pThis->Lun0.pIMediaEx = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pIBase, PDMIMEDIAEX);
    if (!pThis->Lun0.pIMediaEx)
        return PDMUsbHlpVMSetError(pUsbIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS,
                                   N_("MSD failed to query the PDMIMEDIAEX from the driver below it"));

    /*
     * Find out what kind of device we are.
     */
    pThis->fIsCdrom = false;
    PDMMEDIATYPE enmType = pThis->Lun0.pIMedia->pfnGetType(pThis->Lun0.pIMedia);
    /* Anything else will be reported as a hard disk. */
    if (enmType == PDMMEDIATYPE_CDROM || enmType == PDMMEDIATYPE_DVD)
        pThis->fIsCdrom = true;

    rc = pThis->Lun0.pIMediaEx->pfnIoReqAllocSizeSet(pThis->Lun0.pIMediaEx, sizeof(USBMSDREQ));
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("MSD failed to set I/O request size!"));

    /*
     * Register the saved state data unit.
     */
    rc = PDMUsbHlpSSMRegister(pUsbIns, USB_MSD_SAVED_STATE_VERSION, sizeof(*pThis),
                              NULL,           usbMsdLiveExec, NULL,
                              usbMsdSavePrep, usbMsdSaveExec, NULL,
                              usbMsdLoadPrep, usbMsdLoadExec, NULL);
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS,
                                   N_("MSD failed to register SSM save state handlers"));

    return VINF_SUCCESS;
}


/**
 * The USB Mass Storage Device (MSD) registration record.
 */
const PDMUSBREG g_UsbMsd =
{
    /* u32Version */
    PDM_USBREG_VERSION,
    /* szName */
    "Msd",
    /* pszDescription */
    "USB Mass Storage Device, one LUN.",
    /* fFlags */
      PDM_USBREG_HIGHSPEED_CAPABLE | PDM_USBREG_SUPERSPEED_CAPABLE
    | PDM_USBREG_SAVED_STATE_SUPPORTED,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(USBMSD),
    /* pfnConstruct */
    usbMsdConstruct,
    /* pfnDestruct */
    usbMsdDestruct,
    /* pfnVMInitComplete */
    NULL,
    /* pfnVMPowerOn */
    NULL,
    /* pfnVMReset */
    usbMsdVMReset,
    /* pfnVMSuspend */
    usbMsdVMSuspend,
    /* pfnVMResume */
    NULL,
    /* pfnVMPowerOff */
    usbMsdVMPowerOff,
    /* pfnHotPlugged */
    NULL,
    /* pfnHotUnplugged */
    NULL,
    /* pfnDriverAttach */
    usbMsdDriverAttach,
    /* pfnDriverDetach */
    usbMsdDriverDetach,
    /* pfnQueryInterface */
    NULL,
    /* pfnUsbReset */
    usbMsdUsbReset,
    /* pfnUsbGetCachedDescriptors */
    usbMsdUsbGetDescriptorCache,
    /* pfnUsbSetConfiguration */
    usbMsdUsbSetConfiguration,
    /* pfnUsbSetInterface */
    usbMsdUsbSetInterface,
    /* pfnUsbClearHaltedEndpoint */
    usbMsdUsbClearHaltedEndpoint,
    /* pfnUrbNew */
    NULL/*usbMsdUrbNew*/,
    /* pfnQueue */
    usbMsdQueue,
    /* pfnUrbCancel */
    usbMsdUrbCancel,
    /* pfnUrbReap */
    usbMsdUrbReap,
    /* pfnWakeup */
    usbMsdWakeup,
    /* u32TheEnd */
    PDM_USBREG_VERSION
};

