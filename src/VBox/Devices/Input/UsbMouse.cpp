/* $Id: UsbMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbMouse - USB Human Interface Device Emulation (Mouse).
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
#define LOG_GROUP   LOG_GROUP_USB_MOUSE
#include <VBox/vmm/pdmusb.h>
#include <VBox/log.h>
#include <VBox/err.h>
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
/** @name USB HID string IDs
 * @{ */
#define USBHID_STR_ID_MANUFACTURER  1
#define USBHID_STR_ID_PRODUCT_M     2
#define USBHID_STR_ID_PRODUCT_T     3
#define USBHID_STR_ID_PRODUCT_MT    4
#define USBHID_STR_ID_PRODUCT_TP    5
/** @} */

/** @name USB HID specific descriptor types
 * @{ */
#define DT_IF_HID_DESCRIPTOR        0x21
#define DT_IF_HID_REPORT            0x22
/** @} */

/** @name USB HID vendor and product IDs
 * @{ */
#define VBOX_USB_VENDOR             0x80EE
#define USBHID_PID_MOUSE            0x0020
#define USBHID_PID_TABLET           0x0021
#define USBHID_PID_MT_TOUCHSCREEN   0x0022
#define USBHID_PID_MT_TOUCHPAD      0x0023
/** @} */

#define TOUCH_TIMER_MSEC            20      /* 50 Hz touch contact repeat timer. */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * The USB HID request state.
 */
typedef enum USBHIDREQSTATE
{
    /** Invalid status. */
    USBHIDREQSTATE_INVALID = 0,
    /** Ready to receive a new read request. */
    USBHIDREQSTATE_READY,
    /** Have (more) data for the host. */
    USBHIDREQSTATE_DATA_TO_HOST,
    /** Waiting to supply status information to the host. */
    USBHIDREQSTATE_STATUS,
    /** The end of the valid states. */
    USBHIDREQSTATE_END
} USBHIDREQSTATE;

/**
 * The device reporting mode.
 * @todo Use an interface instead of an enum and switches.
 */
typedef enum USBHIDMODE
{
    /** Relative. */
    USBHIDMODE_RELATIVE = 0,
    /** Absolute. */
    USBHIDMODE_ABSOLUTE,
    /** Multi-touch Touchscreen. */
    USBHIDMODE_MT_ABSOLUTE,
    /** Multi-touch Touchpad. */
    USBHIDMODE_MT_RELATIVE,
} USBHIDMODE;


/**
 * Endpoint status data.
 */
typedef struct USBHIDEP
{
    bool                fHalted;
} USBHIDEP;
/** Pointer to the endpoint status. */
typedef USBHIDEP *PUSBHIDEP;


/**
 * A URB queue.
 */
typedef struct USBHIDURBQUEUE
{
    /** The head pointer. */
    PVUSBURB            pHead;
    /** Where to insert the next entry. */
    PVUSBURB           *ppTail;
} USBHIDURBQUEUE;
/** Pointer to a URB queue. */
typedef USBHIDURBQUEUE *PUSBHIDURBQUEUE;
/** Pointer to a const URB queue. */
typedef USBHIDURBQUEUE const *PCUSBHIDURBQUEUE;


/**
 * Mouse movement accumulator.
 */
typedef struct USBHIDM_ACCUM
{
    union
    {
        struct
        {
            uint32_t    fButtons;
            int32_t     dx;
            int32_t     dy;
            int32_t     dz;
        } Relative;
        struct
        {
            uint32_t    fButtons;
            int32_t     dz;
            int32_t     dw;
            uint32_t    x;
            uint32_t    y;
        } Absolute;
    } u;
} USBHIDM_ACCUM, *PUSBHIDM_ACCUM;

#define MT_CONTACTS_PER_REPORT 5

#define MT_CONTACT_MAX_COUNT 10
#define TPAD_CONTACT_MAX_COUNT 5

#define MT_CONTACT_F_IN_CONTACT 0x01
#define MT_CONTACT_F_IN_RANGE   0x02
#define MT_CONTACT_F_CONFIDENCE 0x04

#define MT_CONTACT_S_ACTIVE    0x01 /* Contact must be reported to the guest. */
#define MT_CONTACT_S_CANCELLED 0x02 /* Contact loss must be reported to the guest. */
#define MT_CONTACT_S_REUSED    0x04 /* Report contact loss for the oldId and then new contact for the id. */
#define MT_CONTACT_S_DIRTY     0x08 /* Temporary flag used to track already processed elements. */

typedef struct MTCONTACT
{
    uint16_t x;
    uint16_t y;
    uint8_t  id;
    uint8_t  flags;
    uint8_t  status;
    uint8_t  oldId; /* Valid only if MT_CONTACT_S_REUSED is set. */
} MTCONTACT;


/**
 * The USB HID instance data.
 */
typedef struct USBHID
{
    /** Pointer back to the PDM USB Device instance structure. */
    PPDMUSBINS          pUsbIns;
    /** Critical section protecting the device state. */
    RTCRITSECT          CritSect;

    /** The current configuration.
     * (0 - default, 1 - the one supported configuration, i.e configured.) */
    uint8_t             bConfigurationValue;
    /** Endpoint 0 is the default control pipe, 1 is the dev->host interrupt one. */
    USBHIDEP            aEps[2];
    /** The state of the HID (state machine).*/
    USBHIDREQSTATE      enmState;

    /** Pointer movement accumulator. */
    USBHIDM_ACCUM       PtrDelta;

    /** Pending to-host queue.
     * The URBs waiting here are waiting for data to become available.
     */
    USBHIDURBQUEUE      ToHostQueue;

    /** Done queue
     * The URBs stashed here are waiting to be reaped. */
    USBHIDURBQUEUE      DoneQueue;
    /** Signalled when adding an URB to the done queue and fHaveDoneQueueWaiter
     *  is set. */
    RTSEMEVENT          hEvtDoneQueue;

    /** Someone is waiting on the done queue. */
    bool                fHaveDoneQueueWaiter;
    /** If device has pending changes. */
    bool                fHasPendingChanges;
    /** Is this a relative, absolute or multi-touch pointing device? */
    USBHIDMODE          enmMode;
    /** Tablet coordinate shift factor for old and broken operating systems. */
    uint8_t             u8CoordShift;

    /** Contact repeat timer. */
    TMTIMERHANDLE       hContactTimer;

    /**
     * Mouse port - LUN#0.
     *
     * @implements  PDMIBASE
     * @implements  PDMIMOUSEPORT
     */
    struct
    {
        /** The base interface for the mouse port. */
        PDMIBASE                            IBase;
        /** The mouse port base interface. */
        PDMIMOUSEPORT                       IPort;

        /** The base interface of the attached mouse driver. */
        R3PTRTYPE(PPDMIBASE)                pDrvBase;
        /** The mouse interface of the attached mouse driver. */
        R3PTRTYPE(PPDMIMOUSECONNECTOR)      pDrv;
    } Lun0;

    MTCONTACT aCurrentContactState[MT_CONTACT_MAX_COUNT];
    MTCONTACT aReportingContactState[MT_CONTACT_MAX_COUNT];
    uint32_t u32LastTouchScanTime;
    bool fTouchReporting;
    bool fTouchStateUpdated;
} USBHID;
/** Pointer to the USB HID instance data. */
typedef USBHID *PUSBHID;

#pragma pack(1)
/**
 * The USB HID report structure for relative device.
 */
typedef struct USBHIDM_REPORT
{
    uint8_t     fButtons;
    int8_t      dx;
    int8_t      dy;
    int8_t      dz;
} USBHIDM_REPORT, *PUSBHIDM_REPORT;

/**
 * The USB HID report structure for absolute device.
 */

typedef struct USBHIDT_REPORT
{
    uint8_t     fButtons;
    int8_t      dz;
    int8_t      dw;
    uint8_t     padding;
    uint16_t    x;
    uint16_t    y;
} USBHIDT_REPORT, *PUSBHIDT_REPORT;

/**
 * The combined USB HID report union for relative and absolute
 * devices.
 */
typedef union USBHIDTM_REPORT
{
    USBHIDM_REPORT      m;
    USBHIDT_REPORT      t;
} USBHIDTM_REPORT, *PUSBHIDTM_REPORT;

/**
 * The USB HID report structure for the multi-touch device.
 */
typedef struct USBHIDMT_REPORT
{
    uint8_t     idReport;
    uint8_t     cContacts;
    struct
    {
        uint8_t     fContact;
        uint8_t     cContact;
        uint16_t    x;
        uint16_t    y;
    } aContacts[MT_CONTACTS_PER_REPORT];
    uint32_t    u32ScanTime;
} USBHIDMT_REPORT, *PUSBHIDMT_REPORT;

typedef struct USBHIDMT_REPORT_POINTER
{
    uint8_t     idReport;
    uint8_t     fButtons;
    uint16_t    x;
    uint16_t    y;
} USBHIDMT_REPORT_POINTER;

/**
 * The USB HID report structure for the touchpad device.
 * It is a superset of the multi-touch report.
 */
typedef struct USBHIDTP_REPORT
{
    USBHIDMT_REPORT mt;
    uint8_t         buttons;    /* Required by Win10, not used. */
} USBHIDTP_REPORT, *PUSBHIDTP_REPORT;

typedef union USBHIDALL_REPORT
{
    USBHIDM_REPORT          m;
    USBHIDT_REPORT          t;
    USBHIDMT_REPORT         mt;
    USBHIDMT_REPORT_POINTER mp;
    USBHIDTP_REPORT         tp;
} USBHIDALL_REPORT, *PUSBHIDALL_REPORT;

#pragma pack()


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const PDMUSBDESCCACHESTRING g_aUsbHidStrings_en_US[] =
{
    { USBHID_STR_ID_MANUFACTURER,   "VirtualBox"      },
    { USBHID_STR_ID_PRODUCT_M,      "USB Mouse"       },
    { USBHID_STR_ID_PRODUCT_T,      "USB Tablet"      },
    { USBHID_STR_ID_PRODUCT_MT,     "USB Multi-Touch" },
    { USBHID_STR_ID_PRODUCT_TP,     "USB Touchpad"    },
};

static const PDMUSBDESCCACHELANG g_aUsbHidLanguages[] =
{
    { 0x0409, RT_ELEMENTS(g_aUsbHidStrings_en_US), g_aUsbHidStrings_en_US }
};

static const VUSBDESCENDPOINTEX g_aUsbHidMEndpointDescs[] =
{
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x81 /* ep=1, in */,
            /* .bmAttributes = */       3 /* interrupt */,
            /* .wMaxPacketSize = */     4,
            /* .bInterval = */          10,
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0
    },
};

static const VUSBDESCENDPOINTEX g_aUsbHidTEndpointDescs[] =
{
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x81 /* ep=1, in */,
            /* .bmAttributes = */       3 /* interrupt */,
            /* .wMaxPacketSize = */     8,
            /* .bInterval = */          10,
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0
    },
};

static const VUSBDESCENDPOINTEX g_aUsbHidMTEndpointDescs[] =
{
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x81 /* ep=1, in */,
            /* .bmAttributes = */       3 /* interrupt */,
            /* .wMaxPacketSize = */     64,
            /* .bInterval = */          10,
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0
    },
};

static const VUSBDESCENDPOINTEX g_aUsbHidTPEndpointDescs[] =
{
    {
        {
            /* .bLength = */            sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */   0x81 /* ep=1, in */,
            /* .bmAttributes = */       3 /* interrupt */,
            /* .wMaxPacketSize = */     64,
            /* .bInterval = */          10,
        },
        /* .pvMore = */     NULL,
        /* .pvClass = */    NULL,
        /* .cbClass = */    0
    },
};

/* HID report descriptor (mouse). */
static const uint8_t g_UsbHidMReportDesc[] =
{
    /* Usage Page */                0x05, 0x01,     /* Generic Desktop */
    /* Usage */                     0x09, 0x02,     /* Mouse */
    /* Collection */                0xA1, 0x01,     /* Application */
    /* Usage */                     0x09, 0x01,     /* Pointer */
    /* Collection */                0xA1, 0x00,     /* Physical */
    /* Usage Page */                0x05, 0x09,     /* Button */
    /* Usage Minimum */             0x19, 0x01,     /* Button 1 */
    /* Usage Maximum */             0x29, 0x05,     /* Button 5 */
    /* Logical Minimum */           0x15, 0x00,     /* 0 */
    /* Logical Maximum */           0x25, 0x01,     /* 1 */
    /* Report Count */              0x95, 0x05,     /* 5 */
    /* Report Size */               0x75, 0x01,     /* 1 */
    /* Input */                     0x81, 0x02,     /* Data, Value, Absolute, Bit field */
    /* Report Count */              0x95, 0x01,     /* 1 */
    /* Report Size */               0x75, 0x03,     /* 3 (padding bits) */
    /* Input */                     0x81, 0x03,     /* Constant, Value, Absolute, Bit field */
    /* Usage Page */                0x05, 0x01,     /* Generic Desktop */
    /* Usage */                     0x09, 0x30,     /* X */
    /* Usage */                     0x09, 0x31,     /* Y */
    /* Usage */                     0x09, 0x38,     /* Z (wheel) */
    /* Logical Minimum */           0x15, 0x81,     /* -127 */
    /* Logical Maximum */           0x25, 0x7F,     /* +127 */
    /* Report Size */               0x75, 0x08,     /* 8 */
    /* Report Count */              0x95, 0x03,     /* 3 */
    /* Input */                     0x81, 0x06,     /* Data, Value, Relative, Bit field */
    /* End Collection */            0xC0,
    /* End Collection */            0xC0,
};

/* HID report descriptor (tablet). */
/* NB: The layout is far from random. Having the buttons and Z axis grouped
 * together avoids alignment issues. Also, if X/Y is reported first, followed
 * by buttons/Z, Windows gets phantom Z movement. That is likely a bug in Windows
 * as OS X shows no such problem. When X/Y is reported last, Windows behaves
 * properly.
 */

static const uint8_t g_UsbHidTReportDesc[] =
{
    /* Usage Page */                0x05, 0x01,     /* Generic Desktop */
    /* Usage */                     0x09, 0x02,     /* Mouse */
    /* Collection */                0xA1, 0x01,     /* Application */
    /* Usage */                     0x09, 0x01,     /* Pointer */
    /* Collection */                0xA1, 0x00,     /* Physical */
    /* Usage Page */                0x05, 0x09,     /* Button */
    /* Usage Minimum */             0x19, 0x01,     /* Button 1 */
    /* Usage Maximum */             0x29, 0x05,     /* Button 5 */
    /* Logical Minimum */           0x15, 0x00,     /* 0 */
    /* Logical Maximum */           0x25, 0x01,     /* 1 */
    /* Report Count */              0x95, 0x05,     /* 5 */
    /* Report Size */               0x75, 0x01,     /* 1 */
    /* Input */                     0x81, 0x02,     /* Data, Value, Absolute, Bit field */
    /* Report Count */              0x95, 0x01,     /* 1 */
    /* Report Size */               0x75, 0x03,     /* 3 (padding bits) */
    /* Input */                     0x81, 0x03,     /* Constant, Value, Absolute, Bit field */
    /* Usage Page */                0x05, 0x01,     /* Generic Desktop */
    /* Usage */                     0x09, 0x38,     /* Z (wheel) */
    /* Logical Minimum */           0x15, 0x81,     /* -127 */
    /* Logical Maximum */           0x25, 0x7F,     /* +127 */
    /* Report Size */               0x75, 0x08,     /* 8 */
    /* Report Count */              0x95, 0x01,     /* 1 */
    /* Input */                     0x81, 0x06,     /* Data, Value, Relative, Bit field */
    /* Usage Page */                0x05, 0x0C,     /* Consumer Devices */
    /* Usage */                     0x0A, 0x38, 0x02,/* AC Pan (horizontal wheel) */
    /* Report Count */              0x95, 0x01,     /* 1 */
    /* Input */                     0x81, 0x06,     /* Data, Value, Relative, Bit field */
    /* Report Size */               0x75, 0x08,     /* 8 (padding byte) */
    /* Report Count */              0x95, 0x01,     /* 1 */
    /* Input */                     0x81, 0x03,     /* Constant, Value, Absolute, Bit field */
    /* Usage Page */                0x05, 0x01,     /* Generic Desktop */
    /* Usage */                     0x09, 0x30,     /* X */
    /* Usage */                     0x09, 0x31,     /* Y */
    /* Logical Minimum */           0x15, 0x00,     /* 0 */
    /* Logical Maximum */           0x26, 0xFF,0x7F,/* 0x7fff */
    /* Physical Minimum */          0x35, 0x00,     /* 0 */
    /* Physical Maximum */          0x46, 0xFF,0x7F,/* 0x7fff */
    /* Report Size */               0x75, 0x10,     /* 16 */
    /* Report Count */              0x95, 0x02,     /* 2 */
    /* Input */                     0x81, 0x02,     /* Data, Value, Absolute, Bit field */
    /* End Collection */            0xC0,
    /* End Collection */            0xC0,
};

/*
 * Multi-touch device implementation based on "Windows Pointer Device Data Delivery Protocol"
 * specification.
 */

#define REPORTID_TOUCH_POINTER   1
#define REPORTID_TOUCH_EVENT     2
#define REPORTID_TOUCH_MAX_COUNT 3
#define REPORTID_TOUCH_QABLOB    4
#define REPORTID_TOUCH_DEVCONFIG 5

static const uint8_t g_UsbHidMTReportDesc[] =
{
/* Usage Page (Digitizer)                */ 0x05, 0x0D,
/* Usage (Touch Screen)                  */ 0x09, 0x04,
/* Collection (Application)              */ 0xA1, 0x01,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_EVENT,
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Usage (Contact count)             */ 0x09, 0x54,
/*     Report Size (8)                   */ 0x75, 0x08,
/*     Logical Minimum (0)               */ 0x15, 0x00,
/*     Logical Maximum (12)              */ 0x25, 0x0C,
/*     Report Count (1)                  */ 0x95, 0x01,
/*     Input (Var)                       */ 0x81, 0x02,

/* MT_CONTACTS_PER_REPORT structs u8TipSwitch, u8ContactIdentifier, u16X, u16Y */
/* 1 of 5 */
/*     Usage (Finger)                    */ 0x09, 0x22,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage (Tip Switch)            */ 0x09, 0x42,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,

/*         Usage (In Range)              */ 0x09, 0x32,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,

/*         Report Count (6)              */ 0x95, 0x06,
/*         Input (Cnst,Var)              */ 0x81, 0x03,

/*         Report Size (8)               */ 0x75, 0x08,
/*         Usage (Contact identifier)    */ 0x09, 0x51,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (32)          */ 0x25, 0x20,
/*         Input (Var)                   */ 0x81, 0x02,

/*         Usage Page (Generic Desktop)  */ 0x05, 0x01,
/*         Logical Maximum (32K)         */ 0x26, 0xFF, 0x7F,
/*         Report Size (16)              */ 0x75, 0x10,
/*         Usage (X)                     */ 0x09, 0x30,
/*         Input (Var)                   */ 0x81, 0x02,

/*         Usage (Y)                     */ 0x09, 0x31,
/*         Input (Var)                   */ 0x81, 0x02,
/*     End Collection                    */ 0xC0,
/* 2 of 5 */
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Usage (Finger)                    */ 0x09, 0x22,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage (Tip Switch)            */ 0x09, 0x42,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (In Range)              */ 0x09, 0x32,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Report Count (6)              */ 0x95, 0x06,
/*         Input (Cnst,Var)              */ 0x81, 0x03,
/*         Report Size (8)               */ 0x75, 0x08,
/*         Usage (Contact identifier)    */ 0x09, 0x51,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (32)          */ 0x25, 0x20,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage Page (Generic Desktop)  */ 0x05, 0x01,
/*         Logical Maximum (32K)         */ 0x26, 0xFF, 0x7F,
/*         Report Size (16)              */ 0x75, 0x10,
/*         Usage (X)                     */ 0x09, 0x30,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (Y)                     */ 0x09, 0x31,
/*         Input (Var)                   */ 0x81, 0x02,
/*     End Collection                    */ 0xC0,
/* 3 of 5 */
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Usage (Finger)                    */ 0x09, 0x22,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage (Tip Switch)            */ 0x09, 0x42,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (In Range)              */ 0x09, 0x32,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Report Count (6)              */ 0x95, 0x06,
/*         Input (Cnst,Var)              */ 0x81, 0x03,
/*         Report Size (8)               */ 0x75, 0x08,
/*         Usage (Contact identifier)    */ 0x09, 0x51,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (32)          */ 0x25, 0x20,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage Page (Generic Desktop)  */ 0x05, 0x01,
/*         Logical Maximum (32K)         */ 0x26, 0xFF, 0x7F,
/*         Report Size (16)              */ 0x75, 0x10,
/*         Usage (X)                     */ 0x09, 0x30,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (Y)                     */ 0x09, 0x31,
/*         Input (Var)                   */ 0x81, 0x02,
/*     End Collection                    */ 0xC0,
/* 4 of 5 */
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Usage (Finger)                    */ 0x09, 0x22,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage (Tip Switch)            */ 0x09, 0x42,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (In Range)              */ 0x09, 0x32,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Report Count (6)              */ 0x95, 0x06,
/*         Input (Cnst,Var)              */ 0x81, 0x03,
/*         Report Size (8)               */ 0x75, 0x08,
/*         Usage (Contact identifier)    */ 0x09, 0x51,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (32)          */ 0x25, 0x20,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage Page (Generic Desktop)  */ 0x05, 0x01,
/*         Logical Maximum (32K)         */ 0x26, 0xFF, 0x7F,
/*         Report Size (16)              */ 0x75, 0x10,
/*         Usage (X)                     */ 0x09, 0x30,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (Y)                     */ 0x09, 0x31,
/*         Input (Var)                   */ 0x81, 0x02,
/*     End Collection                    */ 0xC0,
/* 5 of 5 */
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Usage (Finger)                    */ 0x09, 0x22,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage (Tip Switch)            */ 0x09, 0x42,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (In Range)              */ 0x09, 0x32,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Report Count (6)              */ 0x95, 0x06,
/*         Input (Cnst,Var)              */ 0x81, 0x03,
/*         Report Size (8)               */ 0x75, 0x08,
/*         Usage (Contact identifier)    */ 0x09, 0x51,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (32)          */ 0x25, 0x20,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage Page (Generic Desktop)  */ 0x05, 0x01,
/*         Logical Maximum (32K)         */ 0x26, 0xFF, 0x7F,
/*         Report Size (16)              */ 0x75, 0x10,
/*         Usage (X)                     */ 0x09, 0x30,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Usage (Y)                     */ 0x09, 0x31,
/*         Input (Var)                   */ 0x81, 0x02,
/*     End Collection                    */ 0xC0,

/* Note: "Scan time" usage is required for all touch devices (in 100microseconds units). */
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Logical Minimum (0)               */ 0x17, 0x00, 0x00, 0x00, 0x00,
/*     Logical Maximum (2147483647)      */ 0x27, 0xFF, 0xFF, 0xFF, 0x7F,
/*     Report Size (32)                  */ 0x75, 0x20,
/*     Report Count (1)                  */ 0x95, 0x01,
/*     Unit Exponent (0)                 */ 0x55, 0x00,
/*     Unit (None)                       */ 0x65, 0x00,
/*     Usage (Scan time)                 */ 0x09, 0x56,
/*     Input (Var)                       */ 0x81, 0x02,

/*     Report ID                         */ 0x85, REPORTID_TOUCH_MAX_COUNT,
/*     Usage (Contact count maximum)     */ 0x09, 0x55,
/*     Usage (Device identifier)         */ 0x09, 0x53,
/*     Report Size (8)                   */ 0x75, 0x08,
/*     Report Count (2)                  */ 0x95, 0x02,
/*     Logical Maximum (255)             */ 0x26, 0xFF, 0x00,
/*     Feature (Var)                     */ 0xB1, 0x02,

/*     Usage Page (Vendor-Defined 1)     */ 0x06, 0x00, 0xFF,
/*     Usage (QA blob)                   */ 0x09, 0xC5,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_QABLOB,
/*     Logical Minimum (0)               */ 0x15, 0x00,
/*     Logical Maximum (255)             */ 0x26, 0xFF, 0x00,
/*     Report Size (8)                   */ 0x75, 0x08,
/*     Report Count (256)                */ 0x96, 0x00, 0x01,
/*     Feature (Var)                     */ 0xB1, 0x02,
/* End Collection                        */ 0xC0,

/* Note: the pointer report is required by specification:
 * "The report descriptor for a multiple input device must include at least
 * one top-level collection for the primary device and a separate top-level
 * collection for the mouse."
 */
/* Usage Page (Generic Desktop)          */ 0x05, 0x01,
/* Usage (Pointer)                       */ 0x09, 0x01,
/* Collection (Application)              */ 0xA1, 0x01,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_POINTER,
/*     Usage (Pointer)                   */ 0x09, 0x01,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage Page (Button)           */ 0x05, 0x09,
/*         Usage Minimum (Button 1)      */ 0x19, 0x01,
/*         Usage Maximum (Button 2)      */ 0x29, 0x02,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Count (2)              */ 0x95, 0x02,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Report Size (6)               */ 0x75, 0x06,
/*         Input (Cnst,Ary,Abs)          */ 0x81, 0x01,
/*         Usage Page (Generic Desktop)  */ 0x05, 0x01,
/*         Usage (X)                     */ 0x09, 0x30,
/*         Usage (Y)                     */ 0x09, 0x31,
/*         Logical Minimum (0)           */ 0x16, 0x00, 0x00,
/*         Logical Maximum (32K)         */ 0x26, 0xFF, 0x7F,
/*         Physical Minimum (0)          */ 0x36, 0x00, 0x00,
/*         Physical Maximum (32K)        */ 0x46, 0xFF, 0x7F,
/*         Unit (None)                   */ 0x66, 0x00, 0x00,
/*         Report Size (16)              */ 0x75, 0x10,
/*         Report Count (2)              */ 0x95, 0x02,
/*         Input (Var)                   */ 0x81, 0x02,
/*     End Collection                    */ 0xC0,
/* End Collection                        */ 0xC0,

/* Usage Page (Digitizer)                */ 0x05, 0x0D,
/* Usage (Device configuration)          */ 0x09, 0x0E,
/* Collection (Application)              */ 0xA1, 0x01,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_DEVCONFIG,
/*     Usage (Device settings)           */ 0x09, 0x23,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage (Device mode)           */ 0x09, 0x52,
/*         Usage (Device identifier)     */ 0x09, 0x53,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (10)          */ 0x25, 0x0A,
/*         Report Size (8)               */ 0x75, 0x08,
/*         Report Count (2)              */ 0x95, 0x02,
/*         Feature (Var)                 */ 0xB1, 0x02,
/*     End Collection                    */ 0xC0,
/* End Collection                        */ 0xC0
};


#define TOUCHPAD_REPORT_FINGER_USAGE \
/*     Usage Page (Digitizer)            */ 0x05, 0x0D, \
/*     Usage (Finger)                    */ 0x09, 0x22, \
/*     Collection (Logical)              */ 0xA1, 0x02, \
/*         Usage (Tip Switch)            */ 0x09, 0x42, \
/*         Logical Minimum (0)           */ 0x15, 0x00, \
/*         Logical Maximum (1)           */ 0x25, 0x01, \
/*         Report Size (1)               */ 0x75, 0x01, \
/*         Report Count (1)              */ 0x95, 0x01, \
/*         Input (Var)                   */ 0x81, 0x02, \
                                                        \
/*         Note: In Range not required   */             \
/*         Report Count (1)              */ 0x95, 0x01, \
/*         Input (Cnst,Var)              */ 0x81, 0x03, \
                                                        \
/*         Usage (Confidence)            */ 0x09, 0x47, \
/*         Logical Minimum (0)           */ 0x15, 0x00, \
/*         Logical Maximum (1)           */ 0x25, 0x01, \
/*         Report Size (1)               */ 0x75, 0x01, \
/*         Report Count (1)              */ 0x95, 0x01, \
/*         Input (Var)                   */ 0x81, 0x02, \
                                                        \
/*         Report Count (5)              */ 0x95, 0x05, \
/*         Input (Cnst,Var)              */ 0x81, 0x03, \
                                                        \
/*         Report Size (8)               */ 0x75, 0x08, \
/*         Usage (Contact identifier)    */ 0x09, 0x51, \
/*         Report Count (1)              */ 0x95, 0x01, \
/*         Logical Minimum (0)           */ 0x15, 0x00, \
/*         Logical Maximum (32)          */ 0x25, 0x20, \
/*         Input (Var)                   */ 0x81, 0x02, \
                                                        \
/*         Usage Page (Generic Desktop)  */ 0x05, 0x01, \
/*         Logical Minimum (0)           */ 0x15, 0x00, \
/*         Logical Maximum (65535)       */ 0x27, 0xFF, 0xFF, 0x00, 0x00, \
/*         Report Size (16)              */ 0x75, 0x10, \
/*         Unit Exponent (-2)            */ 0x55, 0x0e, \
/*         Unit (Eng Lin: Length (in))   */ 0x65, 0x13, \
/*         Usage (X)                     */ 0x09, 0x30, \
/*         Physical Minimum (0)          */ 0x35, 0x00, \
/*         Physical Maximum (461)        */ 0x46, 0xcd, 0x01, \
/*         Report Count (1)              */ 0x95, 0x01, \
/*         Input (Var)                   */ 0x81, 0x02, \
/*         Usage (Y)                     */ 0x09, 0x31, \
/*         Physical Maximum (346)        */ 0x46, 0x5a, 0x01, \
/*         Input (Var)                   */ 0x81, 0x02, \
/*     End Collection                    */ 0xC0,

static const uint8_t g_UsbHidTPReportDesc[] =
    {
/* Usage Page (Digitizer)                */ 0x05, 0x0D,
/* Usage (Touch Pad)                     */ 0x09, 0x05,
/* Collection (Application)              */ 0xA1, 0x01,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_EVENT,
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Usage (Contact count)             */ 0x09, 0x54,
/*     Report Size (8)                   */ 0x75, 0x08,
/*     Logical Minimum (0)               */ 0x15, 0x00,
/*     Logical Maximum (12)              */ 0x25, 0x0C,
/*     Report Count (1)                  */ 0x95, 0x01,
/*     Input (Var)                       */ 0x81, 0x02,

/* MT_CONTACTS_PER_REPORT structs u8TipSwitch, u8ContactIdentifier, u16X, u16Y */
TOUCHPAD_REPORT_FINGER_USAGE
TOUCHPAD_REPORT_FINGER_USAGE
TOUCHPAD_REPORT_FINGER_USAGE
TOUCHPAD_REPORT_FINGER_USAGE
TOUCHPAD_REPORT_FINGER_USAGE

/* Note: "Scan time" usage is required for all touch devices (in 100microseconds units). */
/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Logical Minimum (0)               */ 0x17, 0x00, 0x00, 0x00, 0x00,
/*     Logical Maximum (2147483647)      */ 0x27, 0xFF, 0xFF, 0xFF, 0x7F,
/*     Report Size (32)                  */ 0x75, 0x20,
/*     Report Count (1)                  */ 0x95, 0x01,
/*     Unit Exponent (0)                 */ 0x55, 0x00,
/*     Unit (None)                       */ 0x65, 0x00,
/*     Usage (Scan time)                 */ 0x09, 0x56,
/*     Input (Var)                       */ 0x81, 0x02,

/* Note: Button required by Windows 10 Precision Touchpad */
/*     Usage Page (Button)               */ 0x05, 0x09,
/*     Usage (Button 1)                  */ 0x09, 0x01,
/*     Logical Maximum (1)               */ 0x25, 0x01,
/*     Report Size (1)                   */ 0x75, 0x01,
/*     Report Count (1)                  */ 0x95, 0x01,
/*     Input (Var)                       */ 0x81, 0x02,
/*     Report Count (7)                  */ 0x95, 0x07,
/*     Input (Cnst,Var)                  */ 0x81, 0x03,

/*     Usage Page (Digitizer)            */ 0x05, 0x0D,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_MAX_COUNT,
/*     Usage (Contact count maximum)     */ 0x09, 0x55,
/*     Usage (Device identifier)         */ 0x09, 0x53,
/*     Report Size (8)                   */ 0x75, 0x08,
/*     Report Count (2)                  */ 0x95, 0x02,
/*     Logical Maximum (255)             */ 0x26, 0xFF, 0x00,
/*     Feature (Var)                     */ 0xB1, 0x02,

/*     Usage Page (Vendor-Defined 1)     */ 0x06, 0x00, 0xFF,
/*     Usage (QA blob)                   */ 0x09, 0xC5,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_QABLOB,
/*     Logical Minimum (0)               */ 0x15, 0x00,
/*     Logical Maximum (255)             */ 0x26, 0xFF, 0x00,
/*     Report Size (8)                   */ 0x75, 0x08,
/*     Report Count (256)                */ 0x96, 0x00, 0x01,
/*     Feature (Var)                     */ 0xB1, 0x02,
/* End Collection                        */ 0xC0,

/* Note: the pointer report is required by specification:
 * "The report descriptor for a multiple input device must include at least
 * one top-level collection for the primary device and a separate top-level
 * collection for the mouse."
 */
/* Usage Page (Generic Desktop)          */ 0x05, 0x01,
/* Usage (Pointer)                       */ 0x09, 0x01,
/* Collection (Application)              */ 0xA1, 0x01,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_POINTER,
/*     Usage (Pointer)                   */ 0x09, 0x01,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage Page (Button)           */ 0x05, 0x09,
/*         Usage Minimum (Button 1)      */ 0x19, 0x01,
/*         Usage Maximum (Button 2)      */ 0x29, 0x02,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (1)           */ 0x25, 0x01,
/*         Report Count (2)              */ 0x95, 0x02,
/*         Report Size (1)               */ 0x75, 0x01,
/*         Input (Var)                   */ 0x81, 0x02,
/*         Report Count (1)              */ 0x95, 0x01,
/*         Report Size (6)               */ 0x75, 0x06,
/*         Input (Cnst,Ary,Abs)          */ 0x81, 0x01,
/*         Usage Page (Generic Desktop)  */ 0x05, 0x01,
/*         Usage (X)                     */ 0x09, 0x30,
/*         Usage (Y)                     */ 0x09, 0x31,
/*         Logical Minimum (0)           */ 0x16, 0x00, 0x00,
/*         Logical Maximum (32K)         */ 0x26, 0xFF, 0x7F,
/*         Physical Minimum (0)          */ 0x36, 0x00, 0x00,
/*         Physical Maximum (32K)        */ 0x46, 0xFF, 0x7F,
/*         Unit (None)                   */ 0x66, 0x00, 0x00,
/*         Report Size (16)              */ 0x75, 0x10,
/*         Report Count (2)              */ 0x95, 0x02,
/*         Input (Var)                   */ 0x81, 0x02,
/*     End Collection                    */ 0xC0,
/* End Collection                        */ 0xC0,

/* Usage Page (Digitizer)                */ 0x05, 0x0D,
/* Usage (Device configuration)          */ 0x09, 0x0E,
/* Collection (Application)              */ 0xA1, 0x01,
/*     Report ID                         */ 0x85, REPORTID_TOUCH_DEVCONFIG,
/*     Usage (Device settings)           */ 0x09, 0x23,
/*     Collection (Logical)              */ 0xA1, 0x02,
/*         Usage (Device mode)           */ 0x09, 0x52,
/*         Usage (Device identifier)     */ 0x09, 0x53,
/*         Logical Minimum (0)           */ 0x15, 0x00,
/*         Logical Maximum (10)          */ 0x25, 0x0A,
/*         Report Size (8)               */ 0x75, 0x08,
/*         Report Count (2)              */ 0x95, 0x02,
/*         Feature (Var)                 */ 0xB1, 0x02,
/*     End Collection                    */ 0xC0,
/* End Collection                        */ 0xC0
    };


/** @todo Do these really have to all be duplicated three times? */
/* Additional HID class interface descriptor. */
static const uint8_t g_UsbHidMIfHidDesc[] =
{
    /* .bLength = */                0x09,
    /* .bDescriptorType = */        0x21,       /* HID */
    /* .bcdHID = */                 0x10, 0x01, /* 1.1 */
    /* .bCountryCode = */           0,
    /* .bNumDescriptors = */        1,
    /* .bDescriptorType = */        0x22,       /* Report */
    /* .wDescriptorLength = */      sizeof(g_UsbHidMReportDesc), 0x00
};

/* Additional HID class interface descriptor. */
static const uint8_t g_UsbHidTIfHidDesc[] =
{
    /* .bLength = */                0x09,
    /* .bDescriptorType = */        0x21,       /* HID */
    /* .bcdHID = */                 0x10, 0x01, /* 1.1 */
    /* .bCountryCode = */           0,
    /* .bNumDescriptors = */        1,
    /* .bDescriptorType = */        0x22,       /* Report */
    /* .wDescriptorLength = */      sizeof(g_UsbHidTReportDesc), 0x00
};

/* Additional HID class interface descriptor. */
static const uint8_t g_UsbHidMTIfHidDesc[] =
{
    /* .bLength = */                0x09,
    /* .bDescriptorType = */        0x21,       /* HID */
    /* .bcdHID = */                 0x10, 0x02, /* 2.1 */
    /* .bCountryCode = */           0,
    /* .bNumDescriptors = */        1,
    /* .bDescriptorType = */        0x22,       /* Report */
    /* .wDescriptorLength = */      (uint8_t)(sizeof(g_UsbHidMTReportDesc) & 0xFF),
                                    (uint8_t)((sizeof(g_UsbHidMTReportDesc) >> 8) & 0xFF)
};

/* Additional HID class interface descriptor. */
static const uint8_t g_UsbHidTPIfHidDesc[] =
{
    /* .bLength = */                0x09,
    /* .bDescriptorType = */        0x21,       /* HID */
    /* .bcdHID = */                 0x10, 0x02, /* 2.1 */
    /* .bCountryCode = */           0,
    /* .bNumDescriptors = */        1,
    /* .bDescriptorType = */        0x22,       /* Report */
    /* .wDescriptorLength = */      (uint8_t)(sizeof(g_UsbHidTPReportDesc) & 0xFF),
                                    (uint8_t)((sizeof(g_UsbHidTPReportDesc) >> 8) & 0xFF)
};

static const VUSBDESCINTERFACEEX g_UsbHidMInterfaceDesc =
{
    {
        /* .bLength = */                sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */        VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */       0,
        /* .bAlternateSetting = */      0,
        /* .bNumEndpoints = */          1,
        /* .bInterfaceClass = */        3 /* HID */,
        /* .bInterfaceSubClass = */     1 /* Boot Interface */,
        /* .bInterfaceProtocol = */     2 /* Mouse */,
        /* .iInterface = */             0
    },
    /* .pvMore = */     NULL,
    /* .pvClass = */    &g_UsbHidMIfHidDesc,
    /* .cbClass = */    sizeof(g_UsbHidMIfHidDesc),
    &g_aUsbHidMEndpointDescs[0],
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBDESCINTERFACEEX g_UsbHidTInterfaceDesc =
{
    {
        /* .bLength = */                sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */        VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */       0,
        /* .bAlternateSetting = */      0,
        /* .bNumEndpoints = */          1,
        /* .bInterfaceClass = */        3 /* HID */,
        /* .bInterfaceSubClass = */     0 /* No subclass - no boot interface. */,
        /* .bInterfaceProtocol = */     0 /* No protocol - no boot interface. */,
        /* .iInterface = */             0
    },
    /* .pvMore = */     NULL,
    /* .pvClass = */    &g_UsbHidTIfHidDesc,
    /* .cbClass = */    sizeof(g_UsbHidTIfHidDesc),
    &g_aUsbHidTEndpointDescs[0],
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBDESCINTERFACEEX g_UsbHidMTInterfaceDesc =
{
    {
        /* .bLength = */                sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */        VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */       0,
        /* .bAlternateSetting = */      0,
        /* .bNumEndpoints = */          1,
        /* .bInterfaceClass = */        3 /* HID */,
        /* .bInterfaceSubClass = */     0 /* No subclass - no boot interface. */,
        /* .bInterfaceProtocol = */     0 /* No protocol - no boot interface. */,
        /* .iInterface = */             0
    },
    /* .pvMore = */     NULL,
    /* .pvClass = */    &g_UsbHidMTIfHidDesc,
    /* .cbClass = */    sizeof(g_UsbHidMTIfHidDesc),
    &g_aUsbHidMTEndpointDescs[0],
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBDESCINTERFACEEX g_UsbHidTPInterfaceDesc =
{
    {
        /* .bLength = */                sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */        VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */       0,
        /* .bAlternateSetting = */      0,
        /* .bNumEndpoints = */          1,
        /* .bInterfaceClass = */        3 /* HID */,
        /* .bInterfaceSubClass = */     0 /* No subclass - no boot interface. */,
        /* .bInterfaceProtocol = */     0 /* No protocol - no boot interface. */,
        /* .iInterface = */             0
    },
    /* .pvMore = */     NULL,
    /* .pvClass = */    &g_UsbHidTPIfHidDesc,
    /* .cbClass = */    sizeof(g_UsbHidTPIfHidDesc),
    &g_aUsbHidTPEndpointDescs[0],
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBINTERFACE g_aUsbHidMInterfaces[] =
{
    { &g_UsbHidMInterfaceDesc, /* .cSettings = */ 1 },
};

static const VUSBINTERFACE g_aUsbHidTInterfaces[] =
{
    { &g_UsbHidTInterfaceDesc, /* .cSettings = */ 1 },
};

static const VUSBINTERFACE g_aUsbHidMTInterfaces[] =
{
    { &g_UsbHidMTInterfaceDesc, /* .cSettings = */ 1 },
};

static const VUSBINTERFACE g_aUsbHidTPInterfaces[] =
{
    { &g_UsbHidTPInterfaceDesc, /* .cSettings = */ 1 },
};

static const VUSBDESCCONFIGEX g_UsbHidMConfigDesc =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbHidMInterfaces),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbHidMInterfaces[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCCONFIGEX g_UsbHidTConfigDesc =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbHidTInterfaces),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbHidTInterfaces[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCCONFIGEX g_UsbHidMTConfigDesc =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbHidMTInterfaces),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbHidMTInterfaces[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCCONFIGEX g_UsbHidTPConfigDesc =
{
    {
        /* .bLength = */            sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .wTotalLength = */       0 /* recalculated on read */,
        /* .bNumInterfaces = */     RT_ELEMENTS(g_aUsbHidTPInterfaces),
        /* .bConfigurationValue =*/ 1,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       RT_BIT(7),
        /* .MaxPower = */           50 /* 100mA */
    },
    NULL,                           /* pvMore */
    NULL,                           /* pvClass */
    0,                              /* cbClass */
    &g_aUsbHidTPInterfaces[0],
    NULL                            /* pvOriginal */
};

static const VUSBDESCDEVICE g_UsbHidMDeviceDesc =
{
    /* .bLength = */                sizeof(g_UsbHidMDeviceDesc),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x110,  /* 1.1 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        8,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBHID_PID_MOUSE,
    /* .bcdDevice = */              0x0100, /* 1.0 */
    /* .iManufacturer = */          USBHID_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBHID_STR_ID_PRODUCT_M,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDESCDEVICE g_UsbHidTDeviceDesc =
{
    /* .bLength = */                sizeof(g_UsbHidTDeviceDesc),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x110,  /* 1.1 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        8,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBHID_PID_TABLET,
    /* .bcdDevice = */              0x0100, /* 1.0 */
    /* .iManufacturer = */          USBHID_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBHID_STR_ID_PRODUCT_T,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDESCDEVICE g_UsbHidMTDeviceDesc =
{
    /* .bLength = */                sizeof(g_UsbHidMTDeviceDesc),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x110,  /* 1.1 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        8,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBHID_PID_MT_TOUCHSCREEN,
    /* .bcdDevice = */              0x0100, /* 1.0 */
    /* .iManufacturer = */          USBHID_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBHID_STR_ID_PRODUCT_MT,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};

static const VUSBDESCDEVICE g_UsbHidTPDeviceDesc =
{
    /* .bLength = */                sizeof(g_UsbHidTPDeviceDesc),
    /* .bDescriptorType = */        VUSB_DT_DEVICE,
    /* .bcdUsb = */                 0x110,  /* 1.1 */
    /* .bDeviceClass = */           0 /* Class specified in the interface desc. */,
    /* .bDeviceSubClass = */        0 /* Subclass specified in the interface desc. */,
    /* .bDeviceProtocol = */        0 /* Protocol specified in the interface desc. */,
    /* .bMaxPacketSize0 = */        8,
    /* .idVendor = */               VBOX_USB_VENDOR,
    /* .idProduct = */              USBHID_PID_MT_TOUCHPAD,
    /* .bcdDevice = */              0x0100, /* 1.0 */
    /* .iManufacturer = */          USBHID_STR_ID_MANUFACTURER,
    /* .iProduct = */               USBHID_STR_ID_PRODUCT_TP,
    /* .iSerialNumber = */          0,
    /* .bNumConfigurations = */     1
};


static const PDMUSBDESCCACHE g_UsbHidMDescCache =
{
    /* .pDevice = */                &g_UsbHidMDeviceDesc,
    /* .paConfigs = */              &g_UsbHidMConfigDesc,
    /* .paLanguages = */            g_aUsbHidLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbHidLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbHidTDescCache =
{
    /* .pDevice = */                &g_UsbHidTDeviceDesc,
    /* .paConfigs = */              &g_UsbHidTConfigDesc,
    /* .paLanguages = */            g_aUsbHidLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbHidLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbHidMTDescCache =
{
    /* .pDevice = */                &g_UsbHidMTDeviceDesc,
    /* .paConfigs = */              &g_UsbHidMTConfigDesc,
    /* .paLanguages = */            g_aUsbHidLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbHidLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_UsbHidTPDescCache =
{
    /* .pDevice = */                &g_UsbHidTPDeviceDesc,
    /* .paConfigs = */              &g_UsbHidTPConfigDesc,
    /* .paLanguages = */            g_aUsbHidLanguages,
    /* .cLanguages = */             RT_ELEMENTS(g_aUsbHidLanguages),
    /* .fUseCachedDescriptors = */  true,
    /* .fUseCachedStringsDescriptors = */ true
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Initializes an URB queue.
 *
 * @param   pQueue              The URB queue.
 */
static void usbHidQueueInit(PUSBHIDURBQUEUE pQueue)
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
DECLINLINE(void) usbHidQueueAddTail(PUSBHIDURBQUEUE pQueue, PVUSBURB pUrb)
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
DECLINLINE(PVUSBURB) usbHidQueueRemoveHead(PUSBHIDURBQUEUE pQueue)
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
DECLINLINE(bool) usbHidQueueRemove(PUSBHIDURBQUEUE pQueue, PVUSBURB pUrb)
{
    PVUSBURB pCur = pQueue->pHead;
    if (pCur == pUrb)
    {
        pQueue->pHead = pUrb->Dev.pNext;
        if (!pUrb->Dev.pNext)
            pQueue->ppTail = &pQueue->pHead;
    }
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
        if (!pUrb->Dev.pNext)
            pQueue->ppTail = &pCur->Dev.pNext;
    }
    pUrb->Dev.pNext = NULL;
    return true;
}


#if 0 /* unused */
/**
 * Checks if the queue is empty or not.
 *
 * @returns true if it is, false if it isn't.
 * @param   pQueue              The URB queue.
 */
DECLINLINE(bool) usbHidQueueIsEmpty(PCUSBHIDURBQUEUE pQueue)
{
    return pQueue->pHead == NULL;
}
#endif /* unused */


/**
 * Links an URB into the done queue.
 *
 * @param   pThis               The HID instance.
 * @param   pUrb                The URB.
 */
static void usbHidLinkDone(PUSBHID pThis, PVUSBURB pUrb)
{
    usbHidQueueAddTail(&pThis->DoneQueue, pUrb);

    if (pThis->fHaveDoneQueueWaiter)
    {
        int rc = RTSemEventSignal(pThis->hEvtDoneQueue);
        AssertRC(rc);
    }
}



/**
 * Completes the URB with a stalled state, halting the pipe.
 */
static int usbHidCompleteStall(PUSBHID pThis, PUSBHIDEP pEp, PVUSBURB pUrb, const char *pszWhy)
{
    LogRelFlow(("usbHidCompleteStall/#%u: pUrb=%p:%s: %s\n",
                pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc, pszWhy));

    pUrb->enmStatus = VUSBSTATUS_STALL;

    /** @todo figure out if the stall is global or pipe-specific or both. */
    if (pEp)
        pEp->fHalted = true;
    else
    {
        pThis->aEps[0].fHalted = true;
        pThis->aEps[1].fHalted = true;
    }

    usbHidLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}


/**
 * Completes the URB after device successfully processed it. Optionally copies data
 * into the URB. May still generate an error if the URB is not big enough.
 */
static int usbHidCompleteOk(PUSBHID pThis, PVUSBURB pUrb, const void *pSrc, size_t cbSrc)
{
    Log(("usbHidCompleteOk/#%u: pUrb=%p:%s (cbData=%#x) cbSrc=%#zx\n", pThis->pUsbIns->iInstance, pUrb, pUrb->pszDesc, pUrb->cbData, cbSrc));

    pUrb->enmStatus = VUSBSTATUS_OK;
    size_t  cbCopy  = 0;
    size_t  cbSetup = 0;

    if (pSrc)   /* Can be NULL if not copying anything. */
    {
        Assert(cbSrc);
        uint8_t *pDst = pUrb->abData;

        /* Returned data is written after the setup message in control URBs. */
        if (pUrb->enmType == VUSBXFERTYPE_MSG)
            cbSetup = sizeof(VUSBSETUP);

        Assert(pUrb->cbData >= cbSetup);    /* Only triggers if URB is corrupted. */

        if (pUrb->cbData > cbSetup)
        {
            /* There is at least one byte of room in the URB. */
            cbCopy = RT_MIN(pUrb->cbData - cbSetup, cbSrc);
            memcpy(pDst + cbSetup, pSrc, cbCopy);
            pUrb->cbData = (uint32_t)(cbCopy + cbSetup);
            Log(("Copied %zu bytes to pUrb->abData[%zu], source had %zu bytes\n", cbCopy, cbSetup, cbSrc));
        }

        /* Need to check length differences. If cbSrc is less than what
         * the URB has space for, it'll be resolved as a short packet. But
         * if cbSrc is bigger, there is a real problem and the host needs
         * to see an overrun/babble error.
         */
        if (RT_UNLIKELY(cbSrc > cbCopy))
            pUrb->enmStatus = VUSBSTATUS_DATA_OVERRUN;
    }
    else
        Assert(cbSrc == 0); /* Make up your mind, caller! */

    usbHidLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}


/**
 * Reset worker for usbHidUsbReset, usbHidUsbSetConfiguration and
 * usbHidHandleDefaultPipe.
 *
 * @returns VBox status code.
 * @param   pThis               The HID instance.
 * @param   pUrb                Set when usbHidHandleDefaultPipe is the
 *                              caller.
 * @param   fSetConfig          Set when usbHidUsbSetConfiguration is the
 *                              caller.
 */
static int usbHidResetWorker(PUSBHID pThis, PVUSBURB pUrb, bool fSetConfig)
{
    /*
     * Wait for the any command currently executing to complete before
     * resetting.  (We cannot cancel its execution.)  How we do this depends
     * on the reset method.
     */

    /*
     * Reset the device state.
     */
    pThis->enmState = USBHIDREQSTATE_READY;
    pThis->fHasPendingChanges = false;
    pThis->fTouchStateUpdated = false;

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aEps); i++)
        pThis->aEps[i].fHalted = false;

    if (!pUrb && !fSetConfig) /* (only device reset) */
        pThis->bConfigurationValue = 0; /* default */

    /*
     * Ditch all pending URBs.
     */
    PVUSBURB pCurUrb;
    while ((pCurUrb = usbHidQueueRemoveHead(&pThis->ToHostQueue)) != NULL)
    {
        pCurUrb->enmStatus = VUSBSTATUS_CRC;
        usbHidLinkDone(pThis, pCurUrb);
    }

    if (pUrb)
        return usbHidCompleteOk(pThis, pUrb, NULL, 0);
    return VINF_SUCCESS;
}

static int8_t clamp_i8(int32_t val)
{
    if (val > 127) {
        val = 127;
    } else if (val < -127) {
        val = -127;
    }
    return val;
}

/**
 * Create a USB HID report report based on the currently accumulated data.
 */
static size_t usbHidFillReport(PUSBHIDTM_REPORT pReport,
                               PUSBHIDM_ACCUM pAccumulated, USBHIDMODE enmMode)
{
    size_t          cbCopy;

    switch (enmMode)
    {
    case USBHIDMODE_ABSOLUTE:
        pReport->t.fButtons = pAccumulated->u.Absolute.fButtons;
        pReport->t.dz       = clamp_i8(pAccumulated->u.Absolute.dz);
        pReport->t.dw       = clamp_i8(pAccumulated->u.Absolute.dw);
        pReport->t.padding  = 0;
        pReport->t.x        = pAccumulated->u.Absolute.x;
        pReport->t.y        = pAccumulated->u.Absolute.y;

        cbCopy = sizeof(pReport->t);
        LogRel3(("Abs event, x=%d, y=%d, fButtons=%02x, report size %d\n",
                 pReport->t.x, pReport->t.y, pReport->t.fButtons,
                 cbCopy));
        break;
    case USBHIDMODE_RELATIVE:
        pReport->m.fButtons = pAccumulated->u.Relative.fButtons;
        pReport->m.dx       = clamp_i8(pAccumulated->u.Relative.dx);
        pReport->m.dy       = clamp_i8(pAccumulated->u.Relative.dy);
        pReport->m.dz       = clamp_i8(pAccumulated->u.Relative.dz);

        cbCopy = sizeof(pReport->m);
        LogRel3(("Rel event, dx=%d, dy=%d, dz=%d, fButtons=%02x, report size %d\n",
                 pReport->m.dx, pReport->m.dy, pReport->m.dz,
                 pReport->m.fButtons, cbCopy));
        break;
    default:
        AssertFailed(); /* Unexpected here. */
        cbCopy = 0;
        break;
    }

    /* Clear the accumulated movement. */
    RT_ZERO(*pAccumulated);

    return cbCopy;
}

DECLINLINE(MTCONTACT *) usbHidFindMTContact(MTCONTACT *paContacts, size_t cContacts,
                                            uint8_t u8Mask, uint8_t u8Value)
{
    size_t i;
    for (i = 0; i < cContacts; i++)
    {
        if ((paContacts[i].status & u8Mask) == u8Value)
        {
            return &paContacts[i];
        }
    }

    return NULL;
}

static int usbHidSendMultiTouchReport(PUSBHID pThis, PVUSBURB pUrb)
{
    uint8_t i;
    MTCONTACT *pRepContact;
    MTCONTACT *pCurContact;

    /* Number of contacts to be reported. In hybrid mode the first report contains
     * total number of contacts and subsequent reports contain 0.
     */
    uint8_t cContacts = 0;

    uint8_t cMaxContacts = pThis->enmMode == USBHIDMODE_MT_RELATIVE ? TPAD_CONTACT_MAX_COUNT  : MT_CONTACT_MAX_COUNT;
    size_t  cbReport     = pThis->enmMode == USBHIDMODE_MT_RELATIVE ? sizeof(USBHIDTP_REPORT) : sizeof(USBHIDMT_REPORT);

    Assert(pThis->fHasPendingChanges);

    if (!pThis->fTouchReporting)
    {
        pThis->fTouchReporting = true;
        pThis->fTouchStateUpdated = false;

        /* Update the reporting state with the new current state.
         * Also mark all active contacts in reporting state as dirty,
         * that is they must be reported to the guest.
         */
        for (i = 0; i < cMaxContacts; i++)
        {
            pRepContact = &pThis->aReportingContactState[i];
            pCurContact = &pThis->aCurrentContactState[i];

            if (pCurContact->status & MT_CONTACT_S_ACTIVE)
            {
                if (pCurContact->status & MT_CONTACT_S_REUSED)
                {
                    pCurContact->status &= ~MT_CONTACT_S_REUSED;

                    /* Keep x,y. Will report lost contact at this point. */
                    pRepContact->id     = pCurContact->oldId;
                    pRepContact->flags  = 0;
                    pRepContact->status = MT_CONTACT_S_REUSED;
                }
                else if (pThis->aCurrentContactState[i].status & MT_CONTACT_S_CANCELLED)
                {
                    pCurContact->status &= ~(MT_CONTACT_S_CANCELLED | MT_CONTACT_S_ACTIVE);

                    /* Keep x,y. Will report lost contact at this point. */
                    pRepContact->id     = pCurContact->id;
                    pRepContact->flags  = 0;
                    pRepContact->status = 0;
                }
                else
                {
                    if (pCurContact->flags == 0)
                    {
                        pCurContact->status &= ~MT_CONTACT_S_ACTIVE; /* Contact disapeared. */
                    }

                    pRepContact->x      = pCurContact->x;
                    pRepContact->y      = pCurContact->y;
                    pRepContact->id     = pCurContact->id;
                    pRepContact->flags  = pCurContact->flags;
                    pRepContact->status = 0;
                }

                cContacts++;

                pRepContact->status |= MT_CONTACT_S_DIRTY;
            }
            else
            {
                pRepContact->status = 0;
            }
        }
    }

    /* Report current state. */
    USBHIDTP_REPORT r;
    USBHIDTP_REPORT *p = &r;
    RT_ZERO(*p);

    p->mt.idReport = REPORTID_TOUCH_EVENT;
    p->mt.cContacts = cContacts;
    p->buttons = 0; /* Not currently used. */

    uint8_t iReportedContact;
    for (iReportedContact = 0; iReportedContact < MT_CONTACTS_PER_REPORT; iReportedContact++)
    {
        /* Find the next not reported contact. */
        pRepContact = usbHidFindMTContact(pThis->aReportingContactState, RT_ELEMENTS(pThis->aReportingContactState),
                                          MT_CONTACT_S_DIRTY, MT_CONTACT_S_DIRTY);

        if (!pRepContact)
        {
            LogRel3(("usbHid: no more touch contacts to report\n"));
            break;
        }

        if (pRepContact->status & MT_CONTACT_S_REUSED)
        {
            /* Do not clear DIRTY flag for contacts which were reused.
             * Because two reports must be generated:
             * one for old contact off, and the second for new contact on.
             */
            pRepContact->status &= ~MT_CONTACT_S_REUSED;
        }
        else
        {
            pRepContact->status &= ~MT_CONTACT_S_DIRTY;
        }

        p->mt.aContacts[iReportedContact].fContact = pRepContact->flags;
        p->mt.aContacts[iReportedContact].cContact = pRepContact->id;
        p->mt.aContacts[iReportedContact].x = pRepContact->x >> pThis->u8CoordShift;
        p->mt.aContacts[iReportedContact].y = pRepContact->y >> pThis->u8CoordShift;

        if (pThis->enmMode == USBHIDMODE_MT_RELATIVE) {
            /** @todo Parse touch confidence in Qt frontend */
            p->mt.aContacts[iReportedContact].fContact |= MT_CONTACT_F_CONFIDENCE;
        }
    }

    p->mt.u32ScanTime = pThis->u32LastTouchScanTime * 10;

    Assert(iReportedContact > 0);

    /* Reset TouchReporting if all contacts reported. */
    pRepContact = usbHidFindMTContact(pThis->aReportingContactState, RT_ELEMENTS(pThis->aReportingContactState),
                                      MT_CONTACT_S_DIRTY, MT_CONTACT_S_DIRTY);

    if (!pRepContact)
    {
        LogRel3(("usbHid: all touch contacts reported\n"));
        pThis->fTouchReporting = false;
        pThis->fHasPendingChanges = pThis->fTouchStateUpdated;
    }
    else
    {
        pThis->fHasPendingChanges = true;
    }

    LogRel3(("usbHid: reporting touch contact:\n%.*Rhxd\n", cbReport, p));
    return usbHidCompleteOk(pThis, pUrb, p, cbReport);
}


/**
 * Sends a state report to the host if there is a pending URB.
 */
static int usbHidSendReport(PUSBHID pThis)
{
    PVUSBURB    pUrb = usbHidQueueRemoveHead(&pThis->ToHostQueue);

    if (pThis->enmMode == USBHIDMODE_MT_ABSOLUTE || pThis->enmMode == USBHIDMODE_MT_RELATIVE)
    {
        /* These modes use a different reporting method and maintain fHasPendingChanges. */
        if (pUrb)
            return usbHidSendMultiTouchReport(pThis, pUrb);
        return VINF_SUCCESS;
    }

    if (pUrb)
    {
        USBHIDTM_REPORT     report;
        PUSBHIDTM_REPORT    pReport = &report;
        size_t              cbCopy;

        cbCopy = usbHidFillReport(pReport, &pThis->PtrDelta, pThis->enmMode);
        pThis->fHasPendingChanges = false;
        return usbHidCompleteOk(pThis, pUrb, pReport, cbCopy);
    }
    else
    {
        LogRelFlow(("No available URB for USB mouse\n"));
        pThis->fHasPendingChanges = true;
    }
    return VINF_EOF;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) usbHidMouseQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PUSBHID pThis = RT_FROM_MEMBER(pInterface, USBHID, Lun0.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->Lun0.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUSEPORT, &pThis->Lun0.IPort);
    return NULL;
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEvent}
 */
static DECLCALLBACK(int) usbHidMousePutEvent(PPDMIMOUSEPORT pInterface, int32_t dx, int32_t dy,
                                             int32_t dz, int32_t dw, uint32_t fButtons)
{
    RT_NOREF1(dw);
    PUSBHID pThis = RT_FROM_MEMBER(pInterface, USBHID, Lun0.IPort);
    RTCritSectEnter(&pThis->CritSect);

    /* Accumulate movement - the events from the front end may arrive
     * at a much higher rate than USB can handle.
     */
    pThis->PtrDelta.u.Relative.fButtons = fButtons;
    pThis->PtrDelta.u.Relative.dx      += dx;
    pThis->PtrDelta.u.Relative.dy      += dy;
    pThis->PtrDelta.u.Relative.dz      -= dz;    /* Inverted! */

    /* Send a report if possible. */
    usbHidSendReport(pThis);

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventAbs}
 */
static DECLCALLBACK(int) usbHidMousePutEventAbs(PPDMIMOUSEPORT pInterface,
                                                uint32_t x, uint32_t y,
                                                int32_t dz, int32_t dw,
                                                uint32_t fButtons)
{
    PUSBHID pThis = RT_FROM_MEMBER(pInterface, USBHID, Lun0.IPort);
    RTCritSectEnter(&pThis->CritSect);

    Assert(pThis->enmMode == USBHIDMODE_ABSOLUTE);

    /* Accumulate movement - the events from the front end may arrive
     * at a much higher rate than USB can handle. Probably not a real issue
     * when only the Z axis is relative (X/Y movement isn't technically
     * accumulated and only the last value is used).
     */
    pThis->PtrDelta.u.Absolute.fButtons = fButtons;
    pThis->PtrDelta.u.Absolute.x        = x >> pThis->u8CoordShift;
    pThis->PtrDelta.u.Absolute.y        = y >> pThis->u8CoordShift;
    pThis->PtrDelta.u.Absolute.dz      -= dz;    /* Inverted! */
    pThis->PtrDelta.u.Absolute.dw      -= dw;    /* Inverted! */

    /* Send a report if possible. */
    usbHidSendReport(pThis);

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * Worker for usbHidMousePutEventTouchScreen and
 * usbHidMousePutEventTouchPad.
 */
static DECLCALLBACK(int) usbHidMousePutEventMultiTouch(PUSBHID pThis,
                                                       uint8_t cContacts,
                                                       const uint64_t *pau64Contacts,
                                                       uint32_t u32ScanTime)
{
    uint8_t i;
    uint8_t j;

    /* Make a copy of new contacts */
    MTCONTACT *paNewContacts = (MTCONTACT *)RTMemTmpAlloc(sizeof(MTCONTACT) * cContacts);
    if (!paNewContacts)
        return VERR_NO_MEMORY;

    for (i = 0; i < cContacts; i++)
    {
        uint32_t u32Lo = RT_LO_U32(pau64Contacts[i]);
        uint32_t u32Hi = RT_HI_U32(pau64Contacts[i]);
        paNewContacts[i].x      = (uint16_t)u32Lo;
        paNewContacts[i].y      = (uint16_t)(u32Lo >> 16);
        paNewContacts[i].id     = RT_BYTE1(u32Hi);
        paNewContacts[i].flags  = RT_BYTE2(u32Hi);
        paNewContacts[i].status = MT_CONTACT_S_DIRTY;
        paNewContacts[i].oldId  = 0; /* Not used. */

        if (pThis->enmMode == USBHIDMODE_MT_ABSOLUTE)
        {
            paNewContacts[i].flags &= MT_CONTACT_F_IN_CONTACT | MT_CONTACT_F_IN_RANGE;
            if (paNewContacts[i].flags & MT_CONTACT_F_IN_CONTACT)
            {
                paNewContacts[i].flags |= MT_CONTACT_F_IN_RANGE;
            }
        }
        else
        {
            Assert(pThis->enmMode == USBHIDMODE_MT_RELATIVE);
            paNewContacts[i].flags &= MT_CONTACT_F_IN_CONTACT;
        }
    }

    MTCONTACT *pCurContact = NULL;
    MTCONTACT *pNewContact = NULL;

    RTCritSectEnter(&pThis->CritSect);

    /* Maintain a state of all current contacts.
     * Intr URBs will be completed according to the state.
     */

    /* Mark all existing contacts as dirty. */
    for (i = 0; i < RT_ELEMENTS(pThis->aCurrentContactState); i++)
        pThis->aCurrentContactState[i].status |= MT_CONTACT_S_DIRTY;

    /* Update existing contacts and mark new contacts. */
    for (i = 0; i < cContacts; i++)
    {
        pNewContact = &paNewContacts[i];

        /* Find existing contact with the same id. */
        pCurContact = NULL;
        for (j = 0; j < RT_ELEMENTS(pThis->aCurrentContactState); j++)
        {
            if (   (pThis->aCurrentContactState[j].status & MT_CONTACT_S_ACTIVE) != 0
                && pThis->aCurrentContactState[j].id == pNewContact->id)
            {
                pCurContact = &pThis->aCurrentContactState[j];
                break;
            }
        }

        if (pCurContact)
        {
            pNewContact->status &= ~MT_CONTACT_S_DIRTY;

            pCurContact->x = pNewContact->x;
            pCurContact->y = pNewContact->y;
            if (pCurContact->flags == 0) /* Contact disappeared already. */
            {
                if ((pCurContact->status & MT_CONTACT_S_REUSED) == 0)
                {
                    pCurContact->status |= MT_CONTACT_S_REUSED; /* Report to the guest that the contact not in touch. */
                    pCurContact->oldId = pCurContact->id;
                }
            }
            pCurContact->flags = pNewContact->flags;
            pCurContact->status &= ~MT_CONTACT_S_DIRTY;
        }
    }

    /* Append new contacts (the dirty one in the paNewContacts). */
    for (i = 0; i < cContacts; i++)
    {
        pNewContact = &paNewContacts[i];

        if (pNewContact->status & MT_CONTACT_S_DIRTY)
        {
            /* It is a new contact, copy is to one of not ACTIVE or not updated existing contacts. */
            pCurContact = usbHidFindMTContact(pThis->aCurrentContactState, RT_ELEMENTS(pThis->aCurrentContactState),
                                              MT_CONTACT_S_ACTIVE, 0);

            if (pCurContact)
            {
                *pCurContact = *pNewContact;
                pCurContact->status = MT_CONTACT_S_ACTIVE; /* Reset status. */
            }
            else
            {
                /* Dirty existing contacts can be reused. */
                pCurContact = usbHidFindMTContact(pThis->aCurrentContactState, RT_ELEMENTS(pThis->aCurrentContactState),
                                                  MT_CONTACT_S_ACTIVE | MT_CONTACT_S_DIRTY,
                                                  MT_CONTACT_S_ACTIVE | MT_CONTACT_S_DIRTY);

                if (pCurContact)
                {
                    pCurContact->x = pNewContact->x;
                    pCurContact->y = pNewContact->y;
                    if ((pCurContact->status & MT_CONTACT_S_REUSED) == 0)
                    {
                        pCurContact->status |= MT_CONTACT_S_REUSED; /* Report to the guest that the contact not in touch. */
                        pCurContact->oldId = pCurContact->id;
                    }
                    pCurContact->flags = pNewContact->flags;
                    pCurContact->status &= ~MT_CONTACT_S_DIRTY;
                }
                else
                {
                    LogRel3(("usbHid: dropped new contact: %d,%d id %d flags %RX8 status %RX8 oldId %d\n",
                             pNewContact->x,
                             pNewContact->y,
                             pNewContact->id,
                             pNewContact->flags,
                             pNewContact->status,
                             pNewContact->oldId
                           ));
                }
            }
        }
    }

    bool fTouchActive = false;

    /* Mark still dirty existing contacts as cancelled, because a new set of contacts does not include them. */
    for (i = 0; i < RT_ELEMENTS(pThis->aCurrentContactState); i++)
    {
        pCurContact = &pThis->aCurrentContactState[i];
        if (pCurContact->status & MT_CONTACT_S_DIRTY)
        {
            pCurContact->status |= MT_CONTACT_S_CANCELLED;
            pCurContact->status &= ~MT_CONTACT_S_DIRTY;
        }
        if (pCurContact->flags & MT_CONTACT_F_IN_CONTACT)
            fTouchActive = true;
    }

    pThis->u32LastTouchScanTime = u32ScanTime;

    LogRel3(("usbHid: scanTime (ms): %d\n", pThis->u32LastTouchScanTime));
    for (i = 0; i < RT_ELEMENTS(pThis->aCurrentContactState); i++)
    {
        LogRel3(("usbHid: contact state[%d]: %d,%d id %d flags %RX8 status %RX8 oldId %d\n",
                  i,
                  pThis->aCurrentContactState[i].x,
                  pThis->aCurrentContactState[i].y,
                  pThis->aCurrentContactState[i].id,
                  pThis->aCurrentContactState[i].flags,
                  pThis->aCurrentContactState[i].status,
                  pThis->aCurrentContactState[i].oldId
                ));
    }

    pThis->fTouchStateUpdated = true;
    pThis->fHasPendingChanges = true;

    /* Send a report if possible. */
    usbHidSendReport(pThis);

    /* If there is an active contact, set up a timer. Windows requires that touch input
     * gets repeated as long as there's contact, otherwise the guest decides that there
     * is no contact anymore, even though it was never told that.
     */
    if (fTouchActive)
        PDMUsbHlpTimerSetMillies(pThis->pUsbIns, pThis->hContactTimer, TOUCH_TIMER_MSEC);
    else
        PDMUsbHlpTimerStop(pThis->pUsbIns, pThis->hContactTimer);

    RTCritSectLeave(&pThis->CritSect);

    RTMemTmpFree(paNewContacts);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventTouchScreen}
 */
static DECLCALLBACK(int) usbHidMousePutEventTouchScreen(PPDMIMOUSEPORT pInterface,
                                                       uint8_t cContacts,
                                                       const uint64_t *pau64Contacts,
                                                       uint32_t u32ScanTime)
{
    PUSBHID pThis = RT_FROM_MEMBER(pInterface, USBHID, Lun0.IPort);

    Assert(pThis->enmMode == USBHIDMODE_MT_ABSOLUTE);

    return usbHidMousePutEventMultiTouch(pThis, cContacts, pau64Contacts, u32ScanTime);
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventTouchPad}
 */
static DECLCALLBACK(int) usbHidMousePutEventTouchPad(PPDMIMOUSEPORT pInterface,
                                                       uint8_t cContacts,
                                                       const uint64_t *pau64Contacts,
                                                       uint32_t u32ScanTime)
{
    PUSBHID pThis = RT_FROM_MEMBER(pInterface, USBHID, Lun0.IPort);

    Assert(pThis->enmMode == USBHIDMODE_MT_RELATIVE);

    return usbHidMousePutEventMultiTouch(pThis, cContacts, pau64Contacts, u32ScanTime);
}

/**
 * @interface_method_impl{PDMUSBREG,pfnUrbReap}
 */
static DECLCALLBACK(PVUSBURB) usbHidUrbReap(PPDMUSBINS pUsbIns, RTMSINTERVAL cMillies)
{
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);

    LogFlowFunc(("pUsbIns=%p cMillies=%u\n", pUsbIns, cMillies));

    RTCritSectEnter(&pThis->CritSect);

    PVUSBURB pUrb = usbHidQueueRemoveHead(&pThis->DoneQueue);
    if (!pUrb && cMillies)
    {
        /* Wait */
        pThis->fHaveDoneQueueWaiter = true;
        RTCritSectLeave(&pThis->CritSect);

        RTSemEventWait(pThis->hEvtDoneQueue, cMillies);

        RTCritSectEnter(&pThis->CritSect);
        pThis->fHaveDoneQueueWaiter = false;

        pUrb = usbHidQueueRemoveHead(&pThis->DoneQueue);
    }

    RTCritSectLeave(&pThis->CritSect);

    if (pUrb)
        LogRelFlow(("usbHidUrbReap/#%u: pUrb=%p:%s\n", pUsbIns->iInstance, pUrb,
                    pUrb->pszDesc));
    return pUrb;
}

/**
 * @interface_method_impl{PDMUSBREG,pfnWakeup}
 */
static DECLCALLBACK(int) usbHidWakeup(PPDMUSBINS pUsbIns)
{
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);

    return RTSemEventSignal(pThis->hEvtDoneQueue);
}

/**
 * @interface_method_impl{PDMUSBREG,pfnUrbCancel}
 */
static DECLCALLBACK(int) usbHidUrbCancel(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    LogRelFlow(("usbHidUrbCancel/#%u: pUrb=%p:%s\n", pUsbIns->iInstance, pUrb,
                pUrb->pszDesc));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Remove the URB from the to-host queue and move it onto the done queue.
     */
    if (usbHidQueueRemove(&pThis->ToHostQueue, pUrb))
        usbHidLinkDone(pThis, pUrb);

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * Handles request sent to the inbound (device to host) interrupt pipe. This is
 * rather different from bulk requests because an interrupt read URB may complete
 * after arbitrarily long time.
 */
static int usbHidHandleIntrDevToHost(PUSBHID pThis, PUSBHIDEP pEp, PVUSBURB pUrb)
{
    /*
     * Stall the request if the pipe is halted.
     */
    if (RT_UNLIKELY(pEp->fHalted))
        return usbHidCompleteStall(pThis, NULL, pUrb, "Halted pipe");

    /*
     * Deal with the URB according to the state.
     */
    switch (pThis->enmState)
    {
        /*
         * We've data left to transfer to the host.
         */
        case USBHIDREQSTATE_DATA_TO_HOST:
        {
            AssertFailed();
            LogRelFlow(("usbHidHandleIntrDevToHost: Entering STATUS\n"));
            return usbHidCompleteOk(pThis, pUrb, NULL, 0);
        }

        /*
         * Status transfer.
         */
        case USBHIDREQSTATE_STATUS:
        {
            AssertFailed();
            LogRelFlow(("usbHidHandleIntrDevToHost: Entering READY\n"));
            pThis->enmState = USBHIDREQSTATE_READY;
            return usbHidCompleteOk(pThis, pUrb, NULL, 0);
        }

        case USBHIDREQSTATE_READY:
            usbHidQueueAddTail(&pThis->ToHostQueue, pUrb);
            LogRelFlow(("usbHidHandleIntrDevToHost: Added %p:%s to the queue\n",
                        pUrb, pUrb->pszDesc));
            /* If a report is pending, send it right away. */
            if (pThis->fHasPendingChanges)
                usbHidSendReport(pThis);
            return VINF_SUCCESS;

        /*
         * Bad states, stall.
         */
        default:
            LogRelFlow(("usbHidHandleIntrDevToHost: enmState=%d cbData=%#x\n",
                        pThis->enmState, pUrb->cbData));
            return usbHidCompleteStall(pThis, NULL, pUrb, "Really bad state (D2H)!");
    }
}

#define GET_REPORT   0x01
#define GET_IDLE     0x02
#define GET_PROTOCOL 0x03
#define SET_REPORT   0x09
#define SET_IDLE     0x0A
#define SET_PROTOCOL 0x0B

static uint8_t const g_abQASampleBlob[256 + 1] =
{
    REPORTID_TOUCH_QABLOB,  /* Report Id. */
    0xfc, 0x28, 0xfe, 0x84, 0x40, 0xcb, 0x9a, 0x87,
    0x0d, 0xbe, 0x57, 0x3c, 0xb6, 0x70, 0x09, 0x88,
    0x07, 0x97, 0x2d, 0x2b, 0xe3, 0x38, 0x34, 0xb6,
    0x6c, 0xed, 0xb0, 0xf7, 0xe5, 0x9c, 0xf6, 0xc2,
    0x2e, 0x84, 0x1b, 0xe8, 0xb4, 0x51, 0x78, 0x43,
    0x1f, 0x28, 0x4b, 0x7c, 0x2d, 0x53, 0xaf, 0xfc,
    0x47, 0x70, 0x1b, 0x59, 0x6f, 0x74, 0x43, 0xc4,
    0xf3, 0x47, 0x18, 0x53, 0x1a, 0xa2, 0xa1, 0x71,
    0xc7, 0x95, 0x0e, 0x31, 0x55, 0x21, 0xd3, 0xb5,
    0x1e, 0xe9, 0x0c, 0xba, 0xec, 0xb8, 0x89, 0x19,
    0x3e, 0xb3, 0xaf, 0x75, 0x81, 0x9d, 0x53, 0xb9,
    0x41, 0x57, 0xf4, 0x6d, 0x39, 0x25, 0x29, 0x7c,
    0x87, 0xd9, 0xb4, 0x98, 0x45, 0x7d, 0xa7, 0x26,
    0x9c, 0x65, 0x3b, 0x85, 0x68, 0x89, 0xd7, 0x3b,
    0xbd, 0xff, 0x14, 0x67, 0xf2, 0x2b, 0xf0, 0x2a,
    0x41, 0x54, 0xf0, 0xfd, 0x2c, 0x66, 0x7c, 0xf8,
    0xc0, 0x8f, 0x33, 0x13, 0x03, 0xf1, 0xd3, 0xc1,
    0x0b, 0x89, 0xd9, 0x1b, 0x62, 0xcd, 0x51, 0xb7,
    0x80, 0xb8, 0xaf, 0x3a, 0x10, 0xc1, 0x8a, 0x5b,
    0xe8, 0x8a, 0x56, 0xf0, 0x8c, 0xaa, 0xfa, 0x35,
    0xe9, 0x42, 0xc4, 0xd8, 0x55, 0xc3, 0x38, 0xcc,
    0x2b, 0x53, 0x5c, 0x69, 0x52, 0xd5, 0xc8, 0x73,
    0x02, 0x38, 0x7c, 0x73, 0xb6, 0x41, 0xe7, 0xff,
    0x05, 0xd8, 0x2b, 0x79, 0x9a, 0xe2, 0x34, 0x60,
    0x8f, 0xa3, 0x32, 0x1f, 0x09, 0x78, 0x62, 0xbc,
    0x80, 0xe3, 0x0f, 0xbd, 0x65, 0x20, 0x08, 0x13,
    0xc1, 0xe2, 0xee, 0x53, 0x2d, 0x86, 0x7e, 0xa7,
    0x5a, 0xc5, 0xd3, 0x7d, 0x98, 0xbe, 0x31, 0x48,
    0x1f, 0xfb, 0xda, 0xaf, 0xa2, 0xa8, 0x6a, 0x89,
    0xd6, 0xbf, 0xf2, 0xd3, 0x32, 0x2a, 0x9a, 0xe4,
    0xcf, 0x17, 0xb7, 0xb8, 0xf4, 0xe1, 0x33, 0x08,
    0x24, 0x8b, 0xc4, 0x43, 0xa5, 0xe5, 0x24, 0xc2
};

static int usbHidRequestClass(PUSBHID pThis, PUSBHIDEP pEp, PVUSBURB pUrb)
{
    PVUSBSETUP pSetup = (PVUSBSETUP)&pUrb->abData[0];

    if ((pThis->enmMode != USBHIDMODE_MT_ABSOLUTE) && (pThis->enmMode != USBHIDMODE_MT_RELATIVE))
    {
        LogRelFlow(("usbHid: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n",
                    pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue,
                    pSetup->wIndex, pSetup->wLength));
        return usbHidCompleteStall(pThis, pEp, pUrb, "Unsupported class req");
    }

    int rc = VINF_SUCCESS;

    switch (pSetup->bRequest)
    {
        case SET_REPORT:
        case GET_REPORT:
        {
            uint8_t u8ReportType = RT_HI_U8(pSetup->wValue);
            uint8_t u8ReportID = RT_LO_U8(pSetup->wValue);
            LogRelFlow(("usbHid: %s: type %d, ID %d, data\n%.*Rhxd\n",
                        pSetup->bRequest == GET_REPORT? "GET_REPORT": "SET_REPORT",
                        u8ReportType, u8ReportID,
                        pUrb->cbData - sizeof(VUSBSETUP), &pUrb->abData[sizeof(VUSBSETUP)]));
            if (pSetup->bRequest == GET_REPORT)
            {
                uint8_t     abData[sizeof(USBHIDALL_REPORT)];
                uint8_t     *pData = (uint8_t *)&abData;
                uint32_t    cbData = 0; /* 0 means that the report is unsupported. */

                if (u8ReportType == 1 && u8ReportID == REPORTID_TOUCH_POINTER)
                {
                    USBHIDMT_REPORT_POINTER *p = (USBHIDMT_REPORT_POINTER *)&abData;
                    /* The actual state should be reported here. */
                    p->idReport = REPORTID_TOUCH_POINTER;
                    p->fButtons = 0;
                    p->x = 0;
                    p->y = 0;
                    cbData = sizeof(USBHIDMT_REPORT_POINTER);
                }
                else if (u8ReportType == 1 && u8ReportID == REPORTID_TOUCH_EVENT)
                {
                    switch (pThis->enmMode)
                    {
                        case USBHIDMODE_MT_ABSOLUTE:
                        {
                            USBHIDMT_REPORT *p = (USBHIDMT_REPORT *)&abData;
                            /* The actual state should be reported here. */
                            RT_ZERO(*p);
                            p->idReport = REPORTID_TOUCH_EVENT;
                            cbData = sizeof(USBHIDMT_REPORT);
                            break;
                        }
                        case USBHIDMODE_MT_RELATIVE:
                        {
                            USBHIDTP_REPORT *p = (USBHIDTP_REPORT *)&abData;
                            /* The actual state should be reported here. */
                            RT_ZERO(*p);
                            p->mt.idReport = REPORTID_TOUCH_EVENT;
                            cbData = sizeof(USBHIDTP_REPORT);
                            break;
                        }
                        default:
                            AssertMsgFailed(("Invalid HID mode %d\n", pThis->enmMode));
                            break;
                    }
                }
                else if (u8ReportType == 3 && u8ReportID == REPORTID_TOUCH_MAX_COUNT)
                {
                    uint8_t cMaxContacts = 0;
                    switch (pThis->enmMode)
                    {
                        case USBHIDMODE_MT_ABSOLUTE:
                            cMaxContacts = MT_CONTACT_MAX_COUNT;
                            break;
                        case USBHIDMODE_MT_RELATIVE:
                            cMaxContacts = TPAD_CONTACT_MAX_COUNT;
                            break;
                        default:
                            AssertMsgFailed(("Invalid HID mode %d\n", pThis->enmMode));
                            break;
                    }
                    abData[0] = REPORTID_TOUCH_MAX_COUNT;
                    abData[1] = cMaxContacts;           /* Contact count maximum. */
                    abData[2] = 0;                      /* Device identifier */
                    cbData = 3;
                }
                else if (u8ReportType == 3 && u8ReportID == REPORTID_TOUCH_QABLOB)
                {
                    pData  = (uint8_t *)&g_abQASampleBlob;
                    cbData = sizeof(g_abQASampleBlob);
                }
                else if (u8ReportType == 3 && u8ReportID == REPORTID_TOUCH_DEVCONFIG)
                {
                    abData[0] = REPORTID_TOUCH_DEVCONFIG;
                    abData[1] = 2;  /* Device mode:
                                     * "HID touch device supporting contact
                                     * identifier and contact count maximum."
                                     */
                    abData[2] = 0;  /* Device identifier */
                    cbData = 3;
                }

                if (cbData > 0)
                {
                    rc = usbHidCompleteOk(pThis, pUrb, pData, cbData);
                }
                else
                {
                    rc = usbHidCompleteStall(pThis, pEp, pUrb, "Unsupported GET_REPORT MT");
                }
            }
            else
            {
                /* SET_REPORT */
                rc = usbHidCompleteOk(pThis, pUrb, NULL, 0);
            }
        } break;
        default:
        {
            LogRelFlow(("usbHid: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n",
                        pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue,
                        pSetup->wIndex, pSetup->wLength));
            rc = usbHidCompleteStall(pThis, pEp, pUrb, "Unsupported class req MT");
        }
    }

    return rc;
}

/**
 * Handles request sent to the default control pipe.
 */
static int usbHidHandleDefaultPipe(PUSBHID pThis, PUSBHIDEP pEp, PVUSBURB pUrb)
{
    PVUSBSETUP pSetup = (PVUSBSETUP)&pUrb->abData[0];
    AssertReturn(pUrb->cbData >= sizeof(*pSetup), VERR_VUSB_FAILED_TO_QUEUE_URB);

    if ((pSetup->bmRequestType & VUSB_REQ_MASK) == VUSB_REQ_STANDARD)
    {
        switch (pSetup->bRequest)
        {
            case VUSB_REQ_GET_DESCRIPTOR:
            {
                switch (pSetup->bmRequestType)
                {
                    case VUSB_TO_DEVICE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        switch (pSetup->wValue >> 8)
                        {
                            case VUSB_DT_STRING:
                                LogRelFlow(("usbHid: GET_DESCRIPTOR DT_STRING wValue=%#x wIndex=%#x\n",
                                            pSetup->wValue, pSetup->wIndex));
                                break;
                            default:
                                LogRelFlow(("usbHid: GET_DESCRIPTOR, huh? wValue=%#x wIndex=%#x\n",
                                            pSetup->wValue, pSetup->wIndex));
                                break;
                        }
                        break;
                    }

                    case VUSB_TO_INTERFACE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        switch (pSetup->wValue >> 8)
                        {
                            uint32_t        cbCopy;
                            uint32_t        cbDesc;
                            const uint8_t   *pDesc;

                            case DT_IF_HID_DESCRIPTOR:
                            {
                                switch (pThis->enmMode)
                                {
                                    case USBHIDMODE_ABSOLUTE:
                                        cbDesc = sizeof(g_UsbHidTIfHidDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidTIfHidDesc;
                                        break;
                                    case USBHIDMODE_RELATIVE:
                                        cbDesc = sizeof(g_UsbHidMIfHidDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidMIfHidDesc;
                                        break;
                                    case USBHIDMODE_MT_ABSOLUTE:
                                        cbDesc = sizeof(g_UsbHidMTIfHidDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidMTIfHidDesc;
                                        break;
                                    case USBHIDMODE_MT_RELATIVE:
                                        cbDesc = sizeof(g_UsbHidTPIfHidDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidTPIfHidDesc;
                                        break;
                                    default:
                                        cbDesc = 0;
                                        pDesc = 0;
                                        break;
                                }
                                /* Returned data is written after the setup message. */
                                cbCopy = RT_MIN(pSetup->wValue, cbDesc);
                                LogRelFlow(("usbHidMouse: GET_DESCRIPTOR DT_IF_HID_DESCRIPTOR wValue=%#x wIndex=%#x cbCopy=%#x\n",
                                            pSetup->wValue, pSetup->wIndex,
                                            cbCopy));
                                return usbHidCompleteOk(pThis, pUrb, pDesc, cbCopy);
                            }

                            case DT_IF_HID_REPORT:
                            {
                                switch (pThis->enmMode)
                                {
                                    case USBHIDMODE_ABSOLUTE:
                                        cbDesc = sizeof(g_UsbHidTReportDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidTReportDesc;
                                        break;
                                    case USBHIDMODE_RELATIVE:
                                        cbDesc = sizeof(g_UsbHidMReportDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidMReportDesc;
                                        break;
                                    case USBHIDMODE_MT_ABSOLUTE:
                                        cbDesc = sizeof(g_UsbHidMTReportDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidMTReportDesc;
                                        break;
                                    case USBHIDMODE_MT_RELATIVE:
                                        cbDesc = sizeof(g_UsbHidTPReportDesc);
                                        pDesc = (const uint8_t *)&g_UsbHidTPReportDesc;
                                        break;
                                    default:
                                        cbDesc = 0;
                                        pDesc = 0;
                                        break;
                                }
                                /* Returned data is written after the setup message. */
                                cbCopy = RT_MIN(pSetup->wLength, cbDesc);
                                LogRelFlow(("usbHid: GET_DESCRIPTOR DT_IF_HID_REPORT wValue=%#x wIndex=%#x cbCopy=%#x\n",
                                            pSetup->wValue, pSetup->wIndex,
                                            cbCopy));
                                return usbHidCompleteOk(pThis, pUrb, pDesc, cbCopy);
                            }

                            default:
                                LogRelFlow(("usbHid: GET_DESCRIPTOR, huh? wValue=%#x wIndex=%#x\n",
                                            pSetup->wValue, pSetup->wIndex));
                                break;
                        }
                        break;
                    }

                    default:
                        LogRelFlow(("usbHid: Bad GET_DESCRIPTOR req: bmRequestType=%#x\n",
                                    pSetup->bmRequestType));
                        return usbHidCompleteStall(pThis, pEp, pUrb, "Bad GET_DESCRIPTOR");
                }
                break;
            }

            case VUSB_REQ_GET_STATUS:
            {
                uint16_t    wRet = 0;

                if (pSetup->wLength != 2)
                {
                    LogRelFlow(("usbHid: Bad GET_STATUS req: wLength=%#x\n",
                                pSetup->wLength));
                    break;
                }
                Assert(pSetup->wValue == 0);
                switch (pSetup->bmRequestType)
                {
                    case VUSB_TO_DEVICE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        Assert(pSetup->wIndex == 0);
                        LogRelFlow(("usbHid: GET_STATUS (device)\n"));
                        wRet = 0;   /* Not self-powered, no remote wakeup. */
                        return usbHidCompleteOk(pThis, pUrb, &wRet, sizeof(wRet));
                    }

                    case VUSB_TO_INTERFACE | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        if (pSetup->wIndex == 0)
                        {
                            return usbHidCompleteOk(pThis, pUrb, &wRet, sizeof(wRet));
                        }
                        LogRelFlow(("usbHid: GET_STATUS (interface) invalid, wIndex=%#x\n", pSetup->wIndex));
                        break;
                    }

                    case VUSB_TO_ENDPOINT | VUSB_REQ_STANDARD | VUSB_DIR_TO_HOST:
                    {
                        if (pSetup->wIndex < RT_ELEMENTS(pThis->aEps))
                        {
                            wRet = pThis->aEps[pSetup->wIndex].fHalted ? 1 : 0;
                            return usbHidCompleteOk(pThis, pUrb, &wRet, sizeof(wRet));
                        }
                        LogRelFlow(("usbHid: GET_STATUS (endpoint) invalid, wIndex=%#x\n", pSetup->wIndex));
                        break;
                    }

                    default:
                        LogRelFlow(("usbHid: Bad GET_STATUS req: bmRequestType=%#x\n",
                                    pSetup->bmRequestType));
                        return usbHidCompleteStall(pThis, pEp, pUrb, "Bad GET_STATUS");
                }
                break;
            }

            case VUSB_REQ_CLEAR_FEATURE:
                break;
        }

        /** @todo implement this. */
        LogRelFlow(("usbHid: Implement standard request: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n",
                    pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue,
                    pSetup->wIndex, pSetup->wLength));

        usbHidCompleteStall(pThis, pEp, pUrb, "TODO: standard request stuff");
    }
    else if ((pSetup->bmRequestType & VUSB_REQ_MASK) == VUSB_REQ_CLASS)
    {
        /* Only VUSB_TO_INTERFACE is allowed. */
        if ((pSetup->bmRequestType & VUSB_RECIP_MASK) == VUSB_TO_INTERFACE)
        {
            return usbHidRequestClass(pThis, pEp, pUrb);
        }

        LogRelFlow(("usbHid: invalid recipient of class req: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n",
                    pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue,
                    pSetup->wIndex, pSetup->wLength));
        return usbHidCompleteStall(pThis, pEp, pUrb, "Invalid recip");
    }
    else
    {
        LogRelFlow(("usbHid: Unknown control msg: bmRequestType=%#x bRequest=%#x wValue=%#x wIndex=%#x wLength=%#x\n",
                    pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue,
                    pSetup->wIndex, pSetup->wLength));
        return usbHidCompleteStall(pThis, pEp, pUrb, "Unknown control msg");
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbQueue}
 */
static DECLCALLBACK(int) usbHidQueue(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    LogRelFlow(("usbHidQueue/#%u: pUrb=%p:%s EndPt=%#x\n", pUsbIns->iInstance,
                pUrb, pUrb->pszDesc, pUrb->EndPt));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Parse on a per end-point basis.
     */
    int rc;
    switch (pUrb->EndPt)
    {
        case 0:
            rc = usbHidHandleDefaultPipe(pThis, &pThis->aEps[0], pUrb);
            break;

        case 0x81:
            AssertFailed();
            RT_FALL_THRU();
        case 0x01:
            rc = usbHidHandleIntrDevToHost(pThis, &pThis->aEps[1], pUrb);
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
static DECLCALLBACK(int) usbHidUsbClearHaltedEndpoint(PPDMUSBINS pUsbIns, unsigned uEndpoint)
{
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    LogRelFlow(("usbHidUsbClearHaltedEndpoint/#%u: uEndpoint=%#x\n",
                pUsbIns->iInstance, uEndpoint));

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
static DECLCALLBACK(int) usbHidUsbSetInterface(PPDMUSBINS pUsbIns, uint8_t bInterfaceNumber, uint8_t bAlternateSetting)
{
    LogRelFlow(("usbHidUsbSetInterface/#%u: bInterfaceNumber=%u bAlternateSetting=%u\n",
                pUsbIns->iInstance, bInterfaceNumber, bAlternateSetting));
    Assert(bAlternateSetting == 0);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbSetConfiguration}
 */
static DECLCALLBACK(int) usbHidUsbSetConfiguration(PPDMUSBINS pUsbIns, uint8_t bConfigurationValue,
                                                   const void *pvOldCfgDesc, const void *pvOldIfState, const void *pvNewCfgDesc)
{
    RT_NOREF3(pvOldCfgDesc, pvOldIfState, pvNewCfgDesc);
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    LogRelFlow(("usbHidUsbSetConfiguration/#%u: bConfigurationValue=%u\n",
                pUsbIns->iInstance, bConfigurationValue));
    Assert(bConfigurationValue == 1);
    RTCritSectEnter(&pThis->CritSect);

    /*
     * If the same config is applied more than once, it's a kind of reset.
     */
    if (pThis->bConfigurationValue == bConfigurationValue)
        usbHidResetWorker(pThis, NULL, true /*fSetConfig*/); /** @todo figure out the exact difference */
    pThis->bConfigurationValue = bConfigurationValue;

    /*
     * Set received event type to absolute or relative.
     */
    pThis->Lun0.pDrv->pfnReportModes(pThis->Lun0.pDrv,
                                     pThis->enmMode == USBHIDMODE_RELATIVE,
                                     pThis->enmMode == USBHIDMODE_ABSOLUTE,
                                     pThis->enmMode == USBHIDMODE_MT_ABSOLUTE,
                                     pThis->enmMode == USBHIDMODE_MT_RELATIVE);

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbGetDescriptorCache}
 */
static DECLCALLBACK(PCPDMUSBDESCCACHE) usbHidUsbGetDescriptorCache(PPDMUSBINS pUsbIns)
{
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    LogRelFlow(("usbHidUsbGetDescriptorCache/#%u:\n", pUsbIns->iInstance));
    switch (pThis->enmMode)
    {
        case USBHIDMODE_ABSOLUTE:
            return &g_UsbHidTDescCache;
        case USBHIDMODE_RELATIVE:
            return &g_UsbHidMDescCache;
        case USBHIDMODE_MT_ABSOLUTE:
            return &g_UsbHidMTDescCache;
        case USBHIDMODE_MT_RELATIVE:
            return &g_UsbHidTPDescCache;
        default:
            return NULL;
    }
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbReset}
 */
static DECLCALLBACK(int) usbHidUsbReset(PPDMUSBINS pUsbIns, bool fResetOnLinux)
{
    RT_NOREF1(fResetOnLinux);
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    LogRelFlow(("usbHidUsbReset/#%u:\n", pUsbIns->iInstance));
    RTCritSectEnter(&pThis->CritSect);

    /* We can not handle any input until device is configured again. */
    pThis->Lun0.pDrv->pfnReportModes(pThis->Lun0.pDrv, false, false, false, false);

    int rc = usbHidResetWorker(pThis, NULL, false /*fSetConfig*/);

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/**
 * @callback_method_impl{FNTMTIMERUSB}
 *
 * A touchscreen needs to repeatedly sent contact information as long
 * as the contact is maintained.
 */
static DECLCALLBACK(void) usbHidContactTimer(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PUSBHID pThis = (PUSBHID)pvUser;

    LogRel3(("usbHid: contact repeat timer\n"));
    usbHidSendReport(pThis);

    PDMUsbHlpTimerSetMillies(pUsbIns, hTimer, TOUCH_TIMER_MSEC);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnDestruct}
 */
static DECLCALLBACK(void) usbHidDestruct(PPDMUSBINS pUsbIns)
{
    PDMUSB_CHECK_VERSIONS_RETURN_VOID(pUsbIns);
    PUSBHID pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    LogRelFlow(("usbHidDestruct/#%u:\n", pUsbIns->iInstance));

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

    PDMUsbHlpTimerDestroy(pUsbIns, pThis->hContactTimer);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnConstruct}
 */
static DECLCALLBACK(int) usbHidConstruct(PPDMUSBINS pUsbIns, int iInstance, PCFGMNODE pCfg, PCFGMNODE pCfgGlobal)
{
    RT_NOREF1(pCfgGlobal);
    PDMUSB_CHECK_VERSIONS_RETURN(pUsbIns);
    PUSBHID     pThis = PDMINS_2_DATA(pUsbIns, PUSBHID);
    PCPDMUSBHLP pHlp  = pUsbIns->pHlpR3;

    LogRelFlow(("usbHidConstruct/#%u:\n", iInstance));

    /*
     * Perform the basic structure initialization first so the destructor
     * will not misbehave.
     */
    pThis->pUsbIns                                  = pUsbIns;
    pThis->hEvtDoneQueue                            = NIL_RTSEMEVENT;
    usbHidQueueInit(&pThis->ToHostQueue);
    usbHidQueueInit(&pThis->DoneQueue);

    int rc = RTCritSectInit(&pThis->CritSect);
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pThis->hEvtDoneQueue);
    AssertRCReturn(rc, rc);

    /*
     * Validate and read the configuration.
     */
    rc = pHlp->pfnCFGMValidateConfig(pCfg, "/", "Mode|CoordShift", "Config", "UsbHid", iInstance);
    if (RT_FAILURE(rc))
        return rc;
    char szMode[64];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "Mode", szMode, sizeof(szMode), "relative");
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("HID failed to query settings"));
    if (!RTStrCmp(szMode, "relative"))
        pThis->enmMode = USBHIDMODE_RELATIVE;
    else if (!RTStrCmp(szMode, "absolute"))
        pThis->enmMode = USBHIDMODE_ABSOLUTE;
    else if (!RTStrCmp(szMode, "multitouch"))
        pThis->enmMode = USBHIDMODE_MT_ABSOLUTE;
    else if (!RTStrCmp(szMode, "touchpad"))
        pThis->enmMode = USBHIDMODE_MT_RELATIVE;
    else
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS,
                                   N_("Invalid HID device mode"));

    LogRelFlow(("usbHidConstruct/#%u: mode '%s'\n", iInstance, szMode));

    pThis->Lun0.IBase.pfnQueryInterface      = usbHidMouseQueryInterface;
    pThis->Lun0.IPort.pfnPutEvent            = usbHidMousePutEvent;
    pThis->Lun0.IPort.pfnPutEventAbs         = usbHidMousePutEventAbs;
    pThis->Lun0.IPort.pfnPutEventTouchScreen = usbHidMousePutEventTouchScreen;
    pThis->Lun0.IPort.pfnPutEventTouchPad    = usbHidMousePutEventTouchPad;

    /*
     * Attach the mouse driver.
     */
    rc = PDMUsbHlpDriverAttach(pUsbIns, 0 /*iLun*/, &pThis->Lun0.IBase, &pThis->Lun0.pDrvBase, "Mouse Port");
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("HID failed to attach mouse driver"));

    pThis->Lun0.pDrv = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pDrvBase, PDMIMOUSECONNECTOR);
    if (!pThis->Lun0.pDrv)
        return PDMUsbHlpVMSetError(pUsbIns, VERR_PDM_MISSING_INTERFACE, RT_SRC_POS, N_("HID failed to query mouse interface"));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "CoordShift", &pThis->u8CoordShift, 1);
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("HID failed to query shift factor"));

    /*
     * Create the touchscreen contact repeat timer.
     */
    rc = PDMUsbHlpTimerCreate(pUsbIns, TMCLOCK_VIRTUAL, usbHidContactTimer, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT,
                              "Touchscreen Contact", &pThis->hContactTimer);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * The USB Human Interface Device (HID) Mouse registration record.
 */
const PDMUSBREG g_UsbHidMou =
{
    /* u32Version */
    PDM_USBREG_VERSION,
    /* szName */
    "HidMouse",
    /* pszDescription */
    "USB HID Mouse.",
    /* fFlags */
    0,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(USBHID),
    /* pfnConstruct */
    usbHidConstruct,
    /* pfnDestruct */
    usbHidDestruct,
    /* pfnVMInitComplete */
    NULL,
    /* pfnVMPowerOn */
    NULL,
    /* pfnVMReset */
    NULL,
    /* pfnVMSuspend */
    NULL,
    /* pfnVMResume */
    NULL,
    /* pfnVMPowerOff */
    NULL,
    /* pfnHotPlugged */
    NULL,
    /* pfnHotUnplugged */
    NULL,
    /* pfnDriverAttach */
    NULL,
    /* pfnDriverDetach */
    NULL,
    /* pfnQueryInterface */
    NULL,
    /* pfnUsbReset */
    usbHidUsbReset,
    /* pfnUsbGetDescriptorCache */
    usbHidUsbGetDescriptorCache,
    /* pfnUsbSetConfiguration */
    usbHidUsbSetConfiguration,
    /* pfnUsbSetInterface */
    usbHidUsbSetInterface,
    /* pfnUsbClearHaltedEndpoint */
    usbHidUsbClearHaltedEndpoint,
    /* pfnUrbNew */
    NULL/*usbHidUrbNew*/,
    /* pfnUrbQueue */
    usbHidQueue,
    /* pfnUrbCancel */
    usbHidUrbCancel,
    /* pfnUrbReap */
    usbHidUrbReap,
    /* pfnWakeup */
    usbHidWakeup,
    /* u32TheEnd */
    PDM_USBREG_VERSION
};
