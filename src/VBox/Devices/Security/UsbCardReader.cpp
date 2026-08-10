/* $Id: UsbCardReader.cpp 114703 2026-07-14 13:23:44Z vitali.pelenjow@oracle.com $ */
/** @file
 * UsbCardReader - Usb Smart Card Reader implementation.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_USB_CARDREADER
#include <VBox/AssertGuest.h>
#include <VBox/param.h>
#include <VBox/vmm/pdmusb.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/cpp/utils.h>
#include <VBox/version.h>
#include "vusbcardreader.h"

#include "UsbCardReaderDefs.h"

#include <VBox/vmm/pdmcardreaderinfs.h>

#include "VBoxDD.h"

/*
 * USB smart card reader (USCR).
 *
 * The slot status is monitored using GetSlotStatusChange, this will provide
 * reliable information about the backend slot status. Most likely converting
 * scard error codes to a slot status is not reliable.
 *
 * Also this will work the same way as the old code in regard to slot status changes.
 * The monitor thread will complete pending intr-in urbs.
 */

/* For important logging. When UsbSCardReader is idle nothing should go to the log file. */
#define UCRLOG(a) do { LogRel2((LOG_FN_FMT ": ", __FUNCTION__)); LogRel2(a); } while(0)
/* For repeated logging, like URB submit. Interrupt endpoint, etc. */
#define UCRLOGF(a) do { LogRel3((LOG_FN_FMT ": ", __FUNCTION__)); LogRel3(a); } while(0)

#define UCRLOGFLEAVERC(rc) UCRLOGF(("LEAVE: %Rrc\n", (rc)))


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

extern void usbCardReaderInitLogging(void);

/* Emulated device type. */
#define USCR_DEVICE_TYPE_VBOX   0 /* VirtualBox emulated device. */
#define USCR_DEVICE_TYPE_SCR335 1 /* SCM SCR335 smart card reader. */

#define USCR_STR_ID_MANUFACTURER   1
#define USCR_STR_ID_PRODUCT        2
#define USCR_STR_ID_SERIAL         3

#define USCR_VENDOR_ID_VBOX          VBOX_PCI_VENDORID
#define USCR_PRODUCT_ID_VBOX         0x0040
#define USCR_FW_VER_VBOX             0x0100 /* 1.00 */
#define USCR_VENDOR_STR_VBOX         "VirtualBox"
#define USCR_PRODUCT_STR_VBOX        "VirtualBox USB Smart Card Reader"
#define USCR_SERIAL_STR_VBOX         "0002"

#define USCR_VENDOR_ID_SCR335        0x04e6  /* SCM */
#define USCR_PRODUCT_ID_SCR335       0x5115  /* SCR335 */
#define USCR_FW_VER_SCR335           0x0523  /* 5.23 */
#define USCR_VENDOR_STR_SCR335       "SCM Microsystems Inc."
#define USCR_PRODUCT_STR_SCR335      "SCR33x USB Smart Card Reader"
#define USCR_SERIAL_STR_SCR335       "0002"

#define USBCARDREADER_EP_COUNT 4

#define SCARDCLASSDEVICE_FEATURES_NO_SPECIAL                            (0)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ATR                         (0x2)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_ACTIVATION_ON_INSERT    (0x4)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_VOLTAGE_SELECTION       (0x8)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_CLK_FREQ_CHANGE         (0x10)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_BAUD_RATE_CHANGE        (0x20)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_ON_NEGOTIATION          (0x40)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_AUTO_PPS                (0x80)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_CLOCK_STOP              (0x100)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_ICC_NAD                     (0x200)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_IFSD                        (0x400)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_TPDU                        (0x10000)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_APDU                        (0x20000)
#define SCARDCLASSDEVICE_FEATURES_AUTOMATIC_EXT_APDU                    (0x40000)

#define ISO7816_MAX_PROTOCOLS                 7
#define ISO7816_MAX_BYTES_PER_INTERFACE       4


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

typedef struct USBCARDREADEREP
{
    bool fHalted;
} USBCARDREADEREP, *PUSBCARDREADEREP;

#define ISO7816_ATR_TX0 RT_BIT(0)
#define ISO7816_ATR_TX1 RT_BIT(1)
#define ISO7816_ATR_TX2 RT_BIT(2)
#define ISO7816_ATR_TX3 RT_BIT(3)


#define ISO7816_ATR_TAi RT_BIT(4)
#define ISO7816_ATR_TBi RT_BIT(5)
#define ISO7816_ATR_TCi RT_BIT(6)
#define ISO7816_ATR_TDi RT_BIT(7)

#define ISO7816_T1_BLK_TYPE_MASK   0xC0

#define ISO7816_T1_BLK_S           0xC0
#define ISO7816_T1_BLK_S_RESPONSE  0x20
#define ISO7816_T1_BLK_S_RESYNCH   0x00
#define ISO7816_T1_BLK_S_IFS       0x01
#define ISO7816_T1_BLK_S_ABORT     0x02
#define ISO7816_T1_BLK_S_WTX       0x03
#define ISO7816_T1_BLK_S_ERR      (0x04 | ISO7816_T1_BLK_S_RESPONSE)

#define ISO7816_T1_BLK_R           0x80
#define ISO7816_T1_BLK_R_NR        0x10 /* N(R) the number of the next expected I-block N(S). */

#define ISO7816_T1_BLK_I           0x00
#define ISO7816_T1_BLK_I1          0x40 /* I-block with the N(S) bit. */
#define ISO7816_T1_BLK_I_NS        0x40 /* I-block number mod 2 at this bit position. */
#define ISO7816_T1_BLK_I_MORE      0x20

/* How many bytes to send to the host in one I-block. 10 + 4 + USED_IFSC <= max input urb data == 64 */
/** @todo SLOT::ParmsT1.bIFSC */
#define USED_IFSC 32

typedef struct
{
    /* */
    uint8_t auProtoBytes[ISO7816_MAX_BYTES_PER_INTERFACE];
    /* Presents of the bytes cold checked with ISO7816_ATR_TX macroses */
    uint8_t u8PresentBitMask;
} CARDATRPROTODESC;

typedef struct CARDATR
{
    uint8_t *pu8RawATR;
    uint8_t cbRawATR;
    uint8_t u8TS;
    uint8_t u8T0;
    /* TA1- ???,
     * TA2 - program the protocol: if b8 ifset can be changed T(b1-b4) defines the parameter
     */
    CARDATRPROTODESC TA;
    CARDATRPROTODESC TB;
    CARDATRPROTODESC TC;
    CARDATRPROTODESC TD;
} CARDATR, *PCARDATR;

/**
 * Should be described in ISO7816 §7
 */

typedef struct T1BLKHEADER
{
    /* node address descriptor */
    uint8_t u8Nad;
    /* it looks like
     * I-block: (application)
     *   b8 - 0,
     *   b7 - sequence number mod 2
     *   b6 - more
     * R-block: (readiness)
     *   b8 - 1
     *   b7 - 0
     *   b2,b1 - for error codes
     * S-block: superviser
     */
    uint8_t u8Pcb;
    /* Length of data, header and chksum is not included. */
    uint8_t u8Len;
} T1BLKHEADER, *PT1BLKHEADER;

typedef PT1BLKHEADER *PPT1BLKHEADER;

typedef enum IBLOCKCHAINMODE
{
    IBLOCKCHAIN_NOT_USED,    /* No I-block chain in progress. */
    IBLOCKCHAIN_FROM_HOST,   /* I-blocks are transferred from the host to the device. */
    IBLOCKCHAIN_FROM_DEVICE  /* From device to the host. */
} IBLOCKCHAINMODE;

typedef struct CARDREADERSLOT
{
    uint8_t u8IBlockNS; /* The block number mod 2 in bit 0x40 of PCB (Protocol Control Byte)
                         * of I-blocks, which are sent back to the driver. The current value
                         * will be used in the PCB and will be XORed with ISO7816_T1_BLK_I_NS (0x40)
                         * after each sent I-block.
                         *
                         * This is reset on resynch or power on.
                         */

    /* Accumulation buffer for the chain of I-blocks. Reset on resynch or power on.
     * A chain can be transferred in one of directions. So one buffer is enough.
     */
    IBLOCKCHAINMODE enmChainMode; /* Direction of the chain. */
    uint8_t *pu8IBlockChain;      /* Memory buffer where the chained data is stored. */
    uint32_t cbIBlockChain;       /* The size of data in the buffer. */
    uint32_t offIBlockChain;      /* The read offset of data in the buffer. */

    uint8_t     u8Id;

    char        *pszCardReaderName;
    /**
     * 0 - T0
     * 1 - T1
     * 0x80,0x81,0x82 - usupported
     */
    uint8_t     u8ProtocolSelector;

    /* Currently negotiated parameters. */
    VUSBCARDREADERPARMST0 ParmsT0;
    VUSBCARDREADERPARMST1 ParmsT1;

    /** @todo how to deal with abort and busy response */
    /* SCard API connection */
    const char *pszSCardReaderSlotName;

    PDMICARDREADER_READERSTATE ReaderStats;

    /* internal representation of ATR */
    CARDATR Atr;
}CARDREADERSLOT, *PCARDREADERSLOT;
typedef const PCARDREADERSLOT PCCARDREADERSLOT;

/* States of the connection to the ICC in the backend. */
typedef enum ICCCONNSTATE
{
    ICCNOCONNECTION, /* No connection was established. */
    ICCDISCONNECTED, /* Connection is not established. */
    ICCCONNECTING,   /* A connect request is being processed. */
    ICCCONNECTED,    /* Connection established. */
    ICCDISCONNECTING /* A disconnect request has been sent. */
} ICCCONNSTATE;

/* A disconnect request is being sent in many cases. */
typedef enum DISCONNECTREASON
{
    DISCONNECT_VOID,          /* No disconnect was sent. */
    DISCONNECT_POWEROFF,      /* ICC power off request from the guest. */
    DISCONNECT_ONERROR,       /* A request has failed. */
    DISCONNECT_ONSTATUSCHANGE /* GetStatusChange notification that the slot is empty. */
} DISCONNECTREASON;

/* Command flags. */
#define VUSBCARDREADER_F_CMD_BUSY       0x01 /* Reader processes a command. */

typedef enum BACKENDSTATUS
{
    BACKEND_STATUS_NO_CONTEXT,
    BACKEND_STATUS_REQUESTING,
    BACKEND_STATUS_ESTABLISHED
} BACKENDSTATUS;

typedef enum STATUSCHANGE
{
    SLOT_STATUS_CHANGE_NOT_USED,
    SLOT_STATUS_CHANGE_REQUESTED,
    SLOT_STATUS_CHANGE_RECEIVED,
    SLOT_STATUS_CHANGE_SENT
} STATUSCHANGE;

/* A response ready to be returned via bulk-in. */
typedef struct USCRRSP
{
    RTLISTNODE nodeRsp;

    uint8_t *pu8Rsp;
    uint32_t cbRsp;

    uint32_t cbLeft; /* How many unreturned bytes in this response. Initially = cbRsp. */
} USCRRSP;

typedef struct URBQUEUE
{
    PVUSBURB pUrbHead;
    PVUSBURB *ppUrbTail;
} URBQUEUE, *PURBQUEUE;

typedef struct USBCARDREADER
{
    uint32_t iDeviceType; /* USCR_DEVICE_TYPE_* */

    BACKENDSTATUS enmBackendStatus;
    STATUSCHANGE enmStatusChange;

    DISCONNECTREASON enmDisconnectReason;

    uint32_t u32EventStateBackend; /**< @todo slot VBOX_SCARD_STATE_* returned by GetStatusChange */

    ICCCONNSTATE enmICCConnState; /**< @todo slot What is the state of the connection to the ICC in the backend. */

    uint8_t bICCStatus;        /**< @todo slot The current ICC status as should be seen by the guest. */
    bool fICCStateChanged;     /**< @todo slot Whether bICCState has changed. Used in the INTR endpoint. */

    uint8_t fu8Cmd;             /* VUSBCARDREADER_F_CMD_* */


    PPDMUSBINS  pUsbIns;
    USBCARDREADEREP                                 aEps[USBCARDREADER_EP_COUNT];
    RTCRITSECT                                      csThis;
    RTSEMEVENT                                      hEvtDoneQueue;
    /** Someone is waiting on the done queue. */
    bool                                            fHaveDoneQueueWaiter;
    uint32_t                                        u32CardReaderStatus;
    /** array of card reader's slots */
    PCARDREADERSLOT                                 paSlots;
    /** number elements in array paSlots */
    uint32_t                                        cSlots;

    /* URB queues */
    struct
    {
        URBQUEUE BulkIn; /* Pending bulk-in URBs. */
        URBQUEUE IntrIn; /* Pending intr-in URBs. */
        URBQUEUE Done;   /* Completed URBs ready for pfnUrbReap */
    } urbQueues;

    /* Responses */
    USCRRSP *pRspCurrent;
    RTLISTANCHOR listRspReady;
    RTLISTANCHOR listRspFree;
    USCRRSP aRspAlloc[8];

    /* Currently processed command. */
    struct
    {
        VUSBCARDREADERBULKHDR hdr; /* The header of the request. */
        uint8_t bMessageTypeRsp;   /* The corresponding response type. */
    } command;

    PPDMTHREAD pStatusMonitorThread;
    RTSEMEVENT semStatusMonitor;

    struct
    {
        PDMIBASE            IBase;
        PDMICARDREADERUP    ICardReaderUp;
        PPDMIBASE           pDrvBase;
        PPDMICARDREADERDOWN pCardReaderDown;
    } Lun0;
} USBCARDREADER, *PUSBCARDREADER;

typedef struct USBCARDREADERICCREQPROC USBCARDREADERICCREQPROC;
typedef USBCARDREADERICCREQPROC* PUSBCARDREADERICCREQPROC;
typedef const PUSBCARDREADERICCREQPROC PCUSBCARDREADERICCREQPROC;
/** type for handlers of ICC commands */
typedef int (*PFNCARDREADERICCPROC)(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);

typedef struct USBCARDREADERICCREQPROC
{
    /* Request type */
    uint8_t     u8ReqId;
    /* Response type */
    uint8_t     u8RespId;
    /* Function processing the request */
    PFNCARDREADERICCPROC pfnCardReaderIccProc;
} USBCARDREADERICCREQPROC, *PUSBCARDREADERICCREQPROC;

typedef const PUSBCARDREADERICCREQPROC PCUSBCARDREADERICCREQPROC;

static int usbCardReaderICCPowerOn(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);
static int usbCardReaderICCPowerOff(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);
static int usbCardReaderICCGetSlotStatus(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);
static int usbCardReaderICCXfrBlock(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);
static int usbCardReaderICCGetParameters(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);
static int usbCardReaderICCResetParameters(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);
static int usbCardReaderICCSetParameters(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);

static int usbCardReaderXfrBlock(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, const VUSBCARDREADERBULKHDR *pCmd);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

static const VUSBCARDREADERDESCDEVICECLASS g_USCRClass =
{
    /* bLength = */ sizeof(VUSBCARDREADERDESCDEVICECLASS),
    /* bDescriptorType = */ 0x21,
    /* bcdCCID = */ 0x110, /* 1.10 */
    /* bMaxSlotIndex = */ 0, /* emulate a one slot device */
    /* bVoltageSupport = */ 0x07, /* 1.8V, 3V, 5V */
    /* dwProtocols = */ 0x03, /* support both T0 and T1 */
    /* dwDefaultClock = */ 4000, /* 4 MHz */
    /* dwMaximumClock = */ 12000, /* 12 MHz */
    /* bNumClockSupported = */ 0, /* default and maximum clock is supported */
    /* dwDataRate = */ 12903, /* bps */
    /* dwMaximumDataRate = */ 412903, /* bps */
    /* bNumDataRateSupported = */ 0, /* all rates */
    /* dwMaxIFSD = */ 252, /* Information Field Size Device, bytes, for T1 */
    /* dwSyncProtocols = */ 0, /* according to the spec */
    /* dwMechanical = */ 0, /* no special features */
    /* dwFeatures = */ 0x100BA, /* TPDU + AutoPPS + AutoBaud + AutoClock + AutoVolt + AutoParamATR */
    /* dwMaxCCIDMessageLength = */ 271,
    /* bClassGetResponse = */ 0xff,
    /* bClassGetEnvilope = */ 0xff,
    /* wLcdLayout = */ 0, /* no LCD */
    /* bPINSupport = */ 0,
    /* bMaxCCIDBusySlots = */ 1
};

static const VUSBDESCENDPOINTEX g_aUSCREndPointDescs[] =
{
    /* 0 = */
    {
        /* .Core = */
        {
            /* .bLength = */ sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */ VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */ 0x1, /* Out */
            /* .bmAttributes = */ 0x2, /* Bulk endpoint*/
            /* .wMaxPacketSize = */ 64,
            /* .bInterval */ 0,
        },
        /* .pvMore = */ NULL,
        /* .pvClass = */ NULL,
        /* .cbClass = */ 0,
        /* .pvSsepc = */ NULL,
        /* .cbSsepc = */ 0,
    },
    /* 1 = */
    {
        /* .Core = */
        {
            /* .bLength = */ sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */ VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */ 0x82, /* In */
            /* .bmAttributes = */ 0x2, /* Bulk endpoint*/
            /* .wMaxPacketSize = */ 64,
            /* .bInterval = */ 0,
        },
        /* .pvMore = */ NULL,
        /* .pvClass = */ NULL,
        /* .cbClass = */ 0,
        /* .pvSsepc = */ NULL,
        /* .cbSsepc = */ 0,
    },
    /* 2 = */
    {
        /* .Core = */
        {
            /* .bLength = */ sizeof(VUSBDESCENDPOINT),
            /* .bDescriptorType = */ VUSB_DT_ENDPOINT,
            /* .bEndpointAddress = */ 0x83,
            /* .bmAttributes = */ 0x3, /* interrupt endpoint*/
            /* .wMaxPacketSize = */ 0x10, /* for Full Speed device it is maximum size of packet */
            /* .bInterval = */ 0xff, /* for Full speed device it is a interval time in millis */
        },
        /* .pvMore = */ NULL,
        /* .pvClass = */ NULL,
        /* .cbClass = */ 0,
        /* .pvSsepc = */ NULL,
        /* .cbSsepc = */ 0,
    }
};

static const VUSBDESCINTERFACEEX g_USCRInterfaceDescriptor =
{
    /* .Core = */
    {
        /* .bLength = */ sizeof(VUSBDESCINTERFACE),
        /* .bDescriptorType = */ VUSB_DT_INTERFACE,
        /* .bInterfaceNumber = */ 0, /* Zero based index in the array of interfaces supported by this configuration. */
        /* .bAlternateSetting = */ 0,
        /* .bNumEndpoints = */ RT_ELEMENTS(g_aUSCREndPointDescs),
        /* .bInterfaceClass = */ 0x0B, /* Smart Card Device Class */
        /* .bInterfaceSubClass = */ 0x0, /* Sub Class */
        /* .bInterfaceProtocol = */ 0,
        /* .iInterface = */ USCR_STR_ID_PRODUCT,
    },
    /* .pvMore = */ NULL,
    /* .pvClass = */ &g_USCRClass,
    /* .cbClass = */ sizeof(g_USCRClass),
    /* .paEndpoints = */ g_aUSCREndPointDescs,
    /* .pIAD = */ NULL,
    /* .cbIAD = */ 0
};

static const VUSBINTERFACE g_aUSCRInterfaces[] =
{
    /* 0 = */
    {
        /* .paSettings = */ &g_USCRInterfaceDescriptor,
        /* .cSettings = */ 1
    }
};

static const struct VUSBDESCCONFIGEX g_aUSCRConfigs =
{
    /* .Core */
    {
        /* .bLength = */ sizeof(VUSBDESCCONFIG),
        /* .bDescriptorType = */ VUSB_DT_CONFIG,
        /* .wTotalLength = */ 0,
        /* .bNumInterfaces = */ RT_ELEMENTS(g_aUSCRInterfaces),
        /* .bConfigurationValue = */ 0x1 ,
        /* .iConfiguration = */ 0,
        /* .bmAttributes = */ 0x80,
        /* .MaxPower = */ 0xfa,
    },
    /* .pvMore */ NULL,
    /* .pvClass */ NULL,
    /* .cbClass */ 0,
    /* .paIfs */ &g_aUSCRInterfaces[0],
    /* .pvOriginal = */ NULL,
};

static const VUSBDESCDEVICE g_USCRDeviceDescriptorVBOX =
{
    /* .bLength = */ sizeof(VUSBDESCDEVICE),
    /* .bDescriptorType = */ VUSB_DT_DEVICE,
    /* .bcdUSB = */ 0x0200,
    /* .bDeviceClass = */ 0, /* defined on interface level */
    /* .bDeviceSubClass = */ 0, /* defined on interface level */
    /* .bDeviceProtocol = */ 0x0,
    /* .bMaxPacketSize0 = */ 0x10 , /* Control endpoint packet-size */
    /* .idVendor = */ USCR_VENDOR_ID_VBOX,
    /* .idProduct = */ USCR_PRODUCT_ID_VBOX,
    /* .bcdDevice = */ USCR_FW_VER_VBOX,
    /* .iManufacturer = */ USCR_STR_ID_MANUFACTURER,
    /* .iProduct = */ USCR_STR_ID_PRODUCT,
    /* .iSerialNumber = */ USCR_STR_ID_SERIAL, /* If 0, then device does not work with generic Windows driver. */
    /* .bNumConfigurations = */ 0x1,
};

static const VUSBDESCDEVICE g_USCRDeviceDescriptorSCR335 =
{
    /* .bLength = */ sizeof(VUSBDESCDEVICE),
    /* .bDescriptorType = */ VUSB_DT_DEVICE,
    /* .bcdUSB = */ 0x0200,
    /* .bDeviceClass = */ 0, /* defined on interface level */
    /* .bDeviceSubClass = */ 0, /* defined on interface level */
    /* .bDeviceProtocol = */ 0x0,
    /* .bMaxPacketSize0 = */ 0x10 , /* Control endpoint packet-size */
    /* .idVendor = */ USCR_VENDOR_ID_SCR335,
    /* .idProduct = */ USCR_PRODUCT_ID_SCR335,
    /* .bcdDevice = */ USCR_FW_VER_SCR335,
    /* .iManufacturer = */ USCR_STR_ID_MANUFACTURER,
    /* .iProduct = */ USCR_STR_ID_PRODUCT,
    /* .iSerialNumber = */ USCR_STR_ID_SERIAL, /* If 0, then device does not work with generic Windows driver. */
    /* .bNumConfigurations = */ 0x1,
};

static const PDMUSBDESCCACHESTRING g_aUSCRStringsVBOX_en_US[] =
{
    /* 0 = */
    {USCR_STR_ID_MANUFACTURER, USCR_VENDOR_STR_VBOX},
    /* 1 = */
    {USCR_STR_ID_PRODUCT, USCR_PRODUCT_STR_VBOX},
    /* 2 = */
    {USCR_STR_ID_SERIAL, USCR_SERIAL_STR_VBOX}
};

static const PDMUSBDESCCACHESTRING g_aUSCRStringsSCR335_en_US[] =
{
    /* 0 = */
    {USCR_STR_ID_MANUFACTURER, USCR_VENDOR_STR_SCR335},
    /* 1 = */
    {USCR_STR_ID_PRODUCT, USCR_PRODUCT_STR_SCR335},
    /* 2 = */
    {USCR_STR_ID_SERIAL, USCR_SERIAL_STR_SCR335}
};

static const PDMUSBDESCCACHELANG g_aUSCRLanguagesVBOX[] =
{
    { 0x0409, RT_ELEMENTS(g_aUSCRStringsVBOX_en_US), g_aUSCRStringsVBOX_en_US}
};

static const PDMUSBDESCCACHELANG g_aUSCRLanguagesSCR335[] =
{
    { 0x0409, RT_ELEMENTS(g_aUSCRStringsSCR335_en_US), g_aUSCRStringsSCR335_en_US}
};

static const PDMUSBDESCCACHE g_USCRDescriptorCacheVBOX =
{
    /* pDevice = */ &g_USCRDeviceDescriptorVBOX,
    /* paConfigs = */ &g_aUSCRConfigs,
    /* paLanguages = */ g_aUSCRLanguagesVBOX,
    /* cLanguages = */ RT_ELEMENTS(g_aUSCRLanguagesVBOX),
    /* fUseCachedDescriptors = */ true,
    /* fUseCachedStringsDescriptors = */ true
};

static const PDMUSBDESCCACHE g_USCRDescriptorCacheSCR335 =
{
    /* pDevice = */ &g_USCRDeviceDescriptorSCR335,
    /* paConfigs = */ &g_aUSCRConfigs,
    /* paLanguages = */ g_aUSCRLanguagesSCR335,
    /* cLanguages = */ RT_ELEMENTS(g_aUSCRLanguagesSCR335),
    /* fUseCachedDescriptors = */ true,
    /* fUseCachedStringsDescriptors = */ true
};

static const PDMICARDREADER_IO_REQUEST g_PdmIoRecvT0 =  { VBOX_SCARD_PROTOCOL_T0, sizeof(PDMICARDREADER_IO_REQUEST)};
static const PDMICARDREADER_IO_REQUEST g_PdmIoRecvT1 =  { VBOX_SCARD_PROTOCOL_T1, sizeof(PDMICARDREADER_IO_REQUEST)};

/* Initial and default parameters for T0 and T1 protocols. */
static const VUSBCARDREADERPARMST0 g_ParmsT0 =
{
    /* bmFindexDindex    = */ 0x13, /* From initial SetParameters */
    /* bmTCCKST0         = */ 0x00,
    /* bGuardTimeT0      = */ 12,
    /* bWaitingIntegerT0 = */ 10,
    /* bClockStop        = */ 0x03 /* Stop with Clock either High or Low */
};

static const VUSBCARDREADERPARMST1 g_ParmsT1 =
{
    /* bmFindexDindex      = */ 0x13, /* From initial SetParameters */
    /* bmTCCKST1           = */ 0x10, /* Checksum type LRC, Convention direct */
    /* bGuardTimeT1        = */ 12,
    /* bmWaitingIntegersT1 = */ 0x9F, /* u4BWI | u4CWI */
    /* bClockStop          = */ 0x03, /* Stop with Clock either High or Low */
    /* bIFSC               = */ 254,
    /* bNadValue           = */ 0x00 /* CCID doesn't support a value other then the default value */
};


static USBCARDREADERICCREQPROC g_UsbCardReaderICCProc[] =
{
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_ICCPOWERON,                      VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_DATABLOCK,                    usbCardReaderICCPowerOn},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_ICCPOWEROFF,                     VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_SLOTSTATUS,                   usbCardReaderICCPowerOff},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_GETSLOTSTATUS,                   VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_SLOTSTATUS,                   usbCardReaderICCGetSlotStatus},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_XFRBLOCK,                        VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_DATABLOCK,                    usbCardReaderICCXfrBlock},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_GETPARAMETERS,                   VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_PARAMETERS,                   usbCardReaderICCGetParameters},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_RESETPARAMETERS,                 VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_PARAMETERS,                   usbCardReaderICCResetParameters},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_SETPARAMETERS,                   VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_PARAMETERS,                   usbCardReaderICCSetParameters},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_ESCAPE,                          VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_ESCAPE,                       NULL},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_ICCLOCK,                         VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_SLOTSTATUS,                   NULL},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_T0APDU,                          VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_SLOTSTATUS,                   NULL},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_SECURE,                          VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_DATABLOCK,                    NULL},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_MECHANICAL,                      VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_SLOTSTATUS,                   NULL},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_ABORT,                           VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_SLOTSTATUS,                   NULL},
    {VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_SETDATARATEANDCLOCKFREQUENCY,    VUSBCARDREADER_MSG_TYPE_PC_TO_RDR_SETDATARATEANDCLOCKFREQUENCY, NULL}
};


/*
 * URB queue maintenance.
 */
static void urbQueueInit(PURBQUEUE pQueue)
{
    pQueue->pUrbHead = NULL;
    pQueue->ppUrbTail = &pQueue->pUrbHead;
}

static void urbQueueAddTail(PURBQUEUE pQueue, PVUSBURB pUrb)
{
    pUrb->Dev.pNext = NULL;
    *pQueue->ppUrbTail = pUrb;
    pQueue->ppUrbTail  = &pUrb->Dev.pNext;
}

static PVUSBURB urbQueueRemoveHead(PURBQUEUE pQueue)
{
    PVUSBURB pUrb = pQueue->pUrbHead;

    if (pUrb)
    {
        pQueue->pUrbHead = pUrb->Dev.pNext;
        if (pUrb->Dev.pNext == NULL)
        {
            pQueue->ppUrbTail = &pQueue->pUrbHead;
        }

        pUrb->Dev.pNext = NULL;
    }

    return pUrb;
}

static bool urbQueueRemove(PURBQUEUE pQueue, PVUSBURB pUrb)
{
    PVUSBURB pIter = pQueue->pUrbHead;

    if (pIter == pUrb)
    {
        pQueue->pUrbHead = pIter->Dev.pNext;
        if (pIter->Dev.pNext == NULL)
        {
            pQueue->ppUrbTail = &pQueue->pUrbHead;
        }
    }
    else
    {
        while (pIter)
        {
            if (pIter->Dev.pNext == pUrb)
            {
                pIter->Dev.pNext = pUrb->Dev.pNext;
                break;
            }
            pIter = pIter->Dev.pNext;
        }

        if (!pIter)
        {
            return false;
        }

        if (pIter->Dev.pNext == NULL)
        {
            pQueue->ppUrbTail = &pIter->Dev.pNext;
        }
    }

    pUrb->Dev.pNext = NULL;
    return true;
}

/*
 * Internal Helper Functions
 */
/*
 * URB completion helpers.
 */

static void usbCardReaderLinkDone(PUSBCARDREADER pThis, PVUSBURB pUrb)
{
    urbQueueAddTail(&pThis->urbQueues.Done, pUrb);

    if (pThis->fHaveDoneQueueWaiter)
    {
        int rc = RTSemEventSignal(pThis->hEvtDoneQueue);
        AssertRC(rc);
    }
}

static int usbCardReaderCompleteOk(PUSBCARDREADER pThis, PVUSBURB pUrb, uint32_t cbData)
{
    UCRLOGF(("pUrb:%p cbData:%d\n", pUrb, cbData));
    pUrb->enmStatus = VUSBSTATUS_OK;
    pUrb->cbData    = cbData;

    if (cbData)
    {
        UCRLOGF(("URB data\n%.*Rhxd\n", cbData > 32? 32: cbData, pUrb->pbData));
    }

    usbCardReaderLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}

static int usbCardReaderCompleteStall(PUSBCARDREADER pThis, PVUSBURB pUrb)
{
    UCRLOG(("pUrb:%p\n", pUrb));

    pUrb->enmStatus = VUSBSTATUS_STALL;

    usbCardReaderLinkDone(pThis, pUrb);
    return VINF_SUCCESS;
}

static int usbCardReaderCompleteEpStall(PUSBCARDREADER pThis, PUSBCARDREADEREP pEp, PVUSBURB pUrb)
{
    UCRLOG(("pUrb:%p\n", pUrb));

    /* Halt the endpoint. */
    pEp->fHalted = true;

    return usbCardReaderCompleteStall(pThis, pUrb);
}

static int usbCardReaderCompleteSetupUnsupported(PUSBCARDREADER pThis, PVUSBURB pUrb)
{
    static int cReported = 0;
    if (cReported < 8)
    {
        cReported++;

        PVUSBSETUP pSetup = (PVUSBSETUP)pUrb->pbData;
        LogRel(("USBSMARTCARDREADER: ignored request 0x%02X 0x%02X 0x%04X 0x%04X %d, cb %d\n",
                pSetup->bmRequestType,
                pSetup->bRequest,
                pSetup->wValue,
                pSetup->wIndex,
                pSetup->wLength,
                pUrb->cbData
              ));
#ifdef DEBUG_sunlover
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

        LogRel(("CTRL: bmRequestType=0x%.2x (%s %s %s) bRequest=0x%.2x (%s) wValue=0x%.4x wIndex=0x%.4x wLength=0x%.4x\n",
                pSetup->bmRequestType,
                s_apszReqDirs[pSetup->bmRequestType >> 7],
                s_apszReqTypes[(pSetup->bmRequestType >> 5) & 0x3],
                (unsigned)(pSetup->bmRequestType & 0xf) < RT_ELEMENTS(s_apszReqRecipients) ? s_apszReqRecipients[pSetup->bmRequestType & 0xf] : "??",
                pSetup->bRequest, pSetup->bRequest < RT_ELEMENTS(s_apszRequests) ? s_apszRequests[pSetup->bRequest] : "??",
                pSetup->wValue, pSetup->wIndex, pSetup->wLength));
#endif
    }

    return usbCardReaderCompleteStall(pThis, pUrb);
}

static int usbCardReaderCompleteSetupOk(PUSBCARDREADER pThis, PVUSBURB pUrb,
                                        uint32_t cbData, const void *pvDataOptional)
{
    if (RT_LIKELY(pUrb->cbData >= sizeof(VUSBSETUP)))
    {
        cbData = RT_MIN(cbData, pUrb->cbData - sizeof(VUSBSETUP));

        /* pvDataOptional is NULL if the data is already copied. */
        if (pvDataOptional)
        {
            memcpy(&pUrb->pbData[sizeof(VUSBSETUP)], pvDataOptional, cbData);
        }

        return usbCardReaderCompleteOk(pThis, pUrb, sizeof(VUSBSETUP) + cbData);
    }

    AssertFailed();
    return usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
}

static int uscrResponseCreate(PUSBCARDREADER pThis,
                              const VUSBCARDREADERBULKHDR *pHdr,
                              const void *pvData,
                              uint32_t cbData)
{
    USCRRSP *pRsp = RTListGetFirst(&pThis->listRspFree, USCRRSP, nodeRsp);

    if (!pRsp)
    {
        /* No free space in the queue, have to drop this response. */
        static int cLogged = 0;
        if (cLogged < 8)
        {
            cLogged++;
            LogRel(("USB: emulated smart card reader: dropped response: cb %d\n",
                    cbData));
        }
        return VERR_NOT_SUPPORTED;
    }

    RTListNodeRemove(&pRsp->nodeRsp);

    uint32_t cbRsp = sizeof(VUSBCARDREADERBULKHDR) + cbData;
    uint8_t *pu8Rsp = (uint8_t *)RTMemAlloc(cbRsp);

    if (pu8Rsp == NULL)
    {
        RTListPrepend(&pThis->listRspFree, &pRsp->nodeRsp);
        return VERR_NO_MEMORY;
    }

    memcpy(&pu8Rsp[0], pHdr, sizeof(VUSBCARDREADERBULKHDR));
    memcpy(&pu8Rsp[sizeof(VUSBCARDREADERBULKHDR)], pvData, cbData);

    pRsp->pu8Rsp = pu8Rsp;
    pRsp->cbRsp = cbRsp;
    pRsp->cbLeft = cbRsp;

    RTListAppend(&pThis->listRspReady, &pRsp->nodeRsp);

    return VINF_SUCCESS;
}


static int uscrResponseSlotError(PUSBCARDREADER pThis,
                                 PCARDREADERSLOT pSlot,
                                 uint8_t u8SlotError)
{
    VUSBCARDREADERBULKHDR hdrRsp = pThis->command.hdr;
    hdrRsp.bMessageType = pThis->command.bMessageTypeRsp;

    hdrRsp.dwLength                 = 0;
    hdrRsp.u.RDR_to_PC.bSlotStatus  = 0x40;
    hdrRsp.u.RDR_to_PC.bSlotStatus |= pSlot? (pThis->bICCStatus & 0x3): VUSBCARDREADER_ICC_STATUS_NO_CARD;
    hdrRsp.u.RDR_to_PC.bSlotError   = u8SlotError;
    hdrRsp.u.RDR_to_PC.u.u8Raw      = 0;
    return uscrResponseCreate(pThis, &hdrRsp, NULL, 0);
}

static int uscrResponseOK(PUSBCARDREADER pThis,
                          PCARDREADERSLOT pSlot,
                          const void *pvData,
                          uint32_t cbData,
                          uint8_t u8RDRtoPCRaw)
{
    VUSBCARDREADERBULKHDR hdrRsp = pThis->command.hdr;
    hdrRsp.bMessageType = pThis->command.bMessageTypeRsp;

    hdrRsp.dwLength                 = cbData;
    hdrRsp.u.RDR_to_PC.bSlotStatus  = 0x00;
    hdrRsp.u.RDR_to_PC.bSlotStatus |= pSlot? (pThis->bICCStatus & 0x3): VUSBCARDREADER_ICC_STATUS_NO_CARD;
    hdrRsp.u.RDR_to_PC.bSlotError   = 0;
    hdrRsp.u.RDR_to_PC.u.u8Raw      = u8RDRtoPCRaw;
    return uscrResponseCreate(pThis, &hdrRsp, pvData, cbData);
}


static int uscrResponseRead(PUSBCARDREADER pThis,
                            uint8_t *pu8Data,
                            uint32_t cbData,
                            uint32_t *pcbOut)
{
    USCRRSP *pRsp = pThis->pRspCurrent;

    if (!pRsp)
    {
        pRsp = RTListGetFirst(&pThis->listRspReady, USCRRSP, nodeRsp);

        if (!pRsp)
        {
            return VERR_NO_DATA;
        }

        RTListNodeRemove(&pRsp->nodeRsp);

        UCRLOG(("Response %p started %d bytes\n",
                pRsp, pRsp->cbRsp));
    }

    uint32_t cbToCopy = RT_MIN(pRsp->cbLeft, cbData);

    memcpy(pu8Data,
           &pRsp->pu8Rsp[pRsp->cbRsp - pRsp->cbLeft],
           cbToCopy);

    pRsp->cbLeft -= cbToCopy;

    UCRLOG(("cbToCopy %d cbLeft %d\n",
            cbToCopy, pRsp->cbLeft));

    if (pRsp->cbLeft == 0)
    {
        /* Last chunk of data for the response.
         * Move the structure to the free list.
         */
        RTMemFree(pRsp->pu8Rsp);
        pRsp->pu8Rsp = NULL;

        RTListPrepend(&pThis->listRspFree, &pRsp->nodeRsp);

        UCRLOG(("Response %p ended\n",
                pRsp));
        pRsp = NULL;
    }

    /* Save the current response pointer for next invocation. */
    pThis->pRspCurrent = pRsp;

    *pcbOut = cbToCopy;
    return VINF_SUCCESS;
}

static void uscrResponseCleanup(PUSBCARDREADER pThis)
{
    if (pThis->pRspCurrent)
    {
        /* Move the structure to the free list. */
        RTMemFree(pThis->pRspCurrent->pu8Rsp);
        pThis->pRspCurrent->pu8Rsp = NULL;

        RTListPrepend(&pThis->listRspFree, &pThis->pRspCurrent->nodeRsp);
        pThis->pRspCurrent = NULL;
    }

    USCRRSP *pIter;
    USCRRSP *pNext;
    RTListForEachSafe(&pThis->listRspReady, pIter, pNext, USCRRSP, nodeRsp)
    {
        /* Move the structure to the free list. */
        UCRLOG(("Freeing ready response %p\n", pIter));
        RTMemFree(pIter->pu8Rsp);
        pIter->pu8Rsp = NULL;

        RTListNodeRemove(&pIter->nodeRsp);
        RTListPrepend(&pThis->listRspFree, &pIter->nodeRsp);
    }
}


/* Callback to read data into a bulk-in or interrupt URB. Returns VERR_NO_DATA if data is currently not available. */
typedef int FNREADDATA(PUSBCARDREADER pThis, uint8_t *pu8Data, uint32_t cbData, uint32_t *pcbOut);
typedef FNREADDATA *PFNREADDATA;

static void urbQueueComplete(PUSBCARDREADER pThis, URBQUEUE *pQueue, PFNREADDATA pfnReadData)
{
    PVUSBURB pUrb = pQueue->pUrbHead;
    while (pUrb)
    {
        uint32_t cbDataReturned = 0;

        /* 'pfnReadData == NULL' means that all queued URBs must be completed with no data. */
        if (pfnReadData)
        {
            int rc = pfnReadData(pThis, &pUrb->pbData[0], pUrb->cbData, &cbDataReturned);
            if (rc == VERR_NO_DATA)
            {
                /* If the caller wants data and data is not available, then keep the URB in the queue for future data. */
                break;
            }

            if (RT_FAILURE(rc))
                cbDataReturned = 0; /* Complete URB with zero data size. */
        }

        bool fRemoved = urbQueueRemove(pQueue, pUrb);
        Assert(fRemoved); NOREF(fRemoved);

        UCRLOGF(("iInstance:%d completing %s pUrb %p cbData %d, cbDataReturned %d\n",
                 pThis->pUsbIns->iInstance,
                 pQueue == &pThis->urbQueues.IntrIn? "intr-in":
                 pQueue == &pThis->urbQueues.BulkIn? "bulk-in":
                 "unknown",
                 pUrb, pUrb->cbData, cbDataReturned));

        usbCardReaderCompleteOk(pThis, pUrb, cbDataReturned);

        pUrb = pQueue->pUrbHead;
    }
}


static void usbCardReaderChainReset(CARDREADERSLOT *pSlot)
{
    UCRLOG(("enmChainMode %d, %d bytes\n",
            pSlot->enmChainMode, pSlot->cbIBlockChain));

    RTMemFree(pSlot->pu8IBlockChain);
    pSlot->pu8IBlockChain = NULL;
    pSlot->cbIBlockChain  = 0;
    pSlot->offIBlockChain = 0;
    pSlot->enmChainMode   = IBLOCKCHAIN_NOT_USED;
}

static int usbCardReaderChainAdd(CARDREADERSLOT *pSlot, const uint8_t *pu8Data, uint8_t u8DataLen)
{
    /* Add a chunk of data to the buffer when transferring from the host to the device. */
    if (pSlot->enmChainMode == IBLOCKCHAIN_FROM_DEVICE)
    {
        /* Wrong mode. */
        AssertFailed();
        return VERR_NOT_SUPPORTED;
    }

    if (pSlot->enmChainMode == IBLOCKCHAIN_NOT_USED)
    {
        UCRLOG(("New chain started.\n"));

        Assert(pSlot->pu8IBlockChain == NULL && pSlot->cbIBlockChain == 0);

        pSlot->pu8IBlockChain = NULL;
        pSlot->cbIBlockChain = 0;
        pSlot->offIBlockChain = 0;
        pSlot->enmChainMode = IBLOCKCHAIN_FROM_HOST;
    }

    if (u8DataLen > 0)
    {
        void *pv = RTMemRealloc(pSlot->pu8IBlockChain, pSlot->cbIBlockChain + u8DataLen);
        if (!pv)
        {
            usbCardReaderChainReset(pSlot);
            return VERR_NO_MEMORY;
        }

        pSlot->pu8IBlockChain = (uint8_t *)pv;

        memcpy(&pSlot->pu8IBlockChain[pSlot->cbIBlockChain], pu8Data, u8DataLen);
        pSlot->cbIBlockChain += u8DataLen;
    }

    return VINF_SUCCESS;
}

static int usbCardReaderChainSet(CARDREADERSLOT *pSlot, const uint8_t *pu8Data, uint32_t cbData)
{
    /* Set the data, which must be transferred from the device to the host. */
    if (   pSlot->enmChainMode == IBLOCKCHAIN_FROM_HOST
        || pSlot->enmChainMode == IBLOCKCHAIN_FROM_DEVICE)
    {
        /* Wrong mode because this is called once before the transfer. */
        AssertFailed();
        return VERR_NOT_SUPPORTED;
    }

    Assert(pSlot->enmChainMode == IBLOCKCHAIN_NOT_USED);

    UCRLOG(("New chain started.\n"));

    Assert(pSlot->pu8IBlockChain == NULL && pSlot->cbIBlockChain == 0);

    void *pv = RTMemAlloc(cbData);
    if (!pv)
    {
        usbCardReaderChainReset(pSlot);
        return VERR_NO_MEMORY;
    }

    memcpy(pv, pu8Data, cbData);

    pSlot->pu8IBlockChain = (uint8_t *)pv;
    pSlot->cbIBlockChain = cbData;
    pSlot->offIBlockChain = 0;
    pSlot->enmChainMode = IBLOCKCHAIN_FROM_DEVICE;

    return VINF_SUCCESS;
}


/*
 * ICC processing functions
 */

/* PC_TO_RDR_IccPowerOn */
static int usbCardReaderICCPowerOn(PUSBCARDREADER pThis,
                                   PCARDREADERSLOT pSlot,
                                   const VUSBCARDREADERBULKHDR *pCmd)
{
    RT_NOREF1(pCmd);
    UCRLOG(("\n"));

    int rc = VINF_SUCCESS;

    /* Emulate IccPowerOn/IccPowerOff requests by connecting and disconnecting to the card. */
    switch (pThis->enmICCConnState)
    {
       case ICCNOCONNECTION:
       case ICCDISCONNECTED:
       {
           UCRLOG(("%s\n", pThis->enmICCConnState == ICCNOCONNECTION? "ICCNOCONNECTION": "ICCDISCONNECTED"));

           int rcBackend = pThis->Lun0.pCardReaderDown->pfnConnect(pThis->Lun0.pCardReaderDown,
                                                                   pSlot, /** @todo replace with NULL or Command Context */
                                                                   pSlot->pszCardReaderName,
                                                                   VBOX_SCARD_SHARE_EXCLUSIVE,
                                                                   VBOX_SCARD_PROTOCOL_T1 | VBOX_SCARD_PROTOCOL_T0);

           if (RT_SUCCESS(rcBackend))
           {
               /* Mark the command as being processed. */
               pThis->fu8Cmd |= VUSBCARDREADER_F_CMD_BUSY;

               pThis->enmICCConnState = ICCCONNECTING;
           }
           else
           {
               rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON); /* Not supported. */

               pThis->enmICCConnState = ICCNOCONNECTION;
           }
       } break;

       case ICCCONNECTING:
       {
           UCRLOG(("ICCCONNECTING\n"));
           /* Connect request was sent already. This can happen if the original request did not return yet
            * and the guest may be cancelled the previous request and trying again.
            * Treat this as an error condition.
            */
           rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_BUSY_WITH_AUTO_SEQUENCE);
       } break;

       case ICCCONNECTED:
       {
           UCRLOG(("ICCCONNECTED\n"));
           /* Already connected that is do nothing and return the already received ATR. */
           rc = uscrResponseOK(pThis, pSlot, pSlot->Atr.pu8RawATR, pSlot->Atr.cbRawATR, 0);
       } break;

       case ICCDISCONNECTING:
       {
           UCRLOG(("ICCDISCONNECTING\n"));
           /* Treat this as an error condition. Like ICCCONNECTING. */
           rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_BUSY_WITH_AUTO_SEQUENCE);
       } break;

       default:
           AssertFailed();
           rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON); /* Not supported. */
           break;
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

/* PC_TO_RDR_IccPowerOff */
static int usbCardReaderICCPowerOff(PUSBCARDREADER pThis,
                                    PCARDREADERSLOT pSlot,
                                    const VUSBCARDREADERBULKHDR *pCmd)
{
    RT_NOREF1(pCmd);
    UCRLOG(("\n"));

    int rc = VINF_SUCCESS;

    /* Emulate IccPowerOn/IccPowerOff requests by connecting and disconnecting to the card. */
    switch (pThis->enmICCConnState)
    {
       case ICCNOCONNECTION:
       case ICCDISCONNECTED:
       {
           UCRLOG(("%s\n", pThis->enmICCConnState == ICCNOCONNECTION? "ICCNOCONNECTION": "ICCDISCONNECTED"));

           /* Already disconnected that is do nothing and return the slot status response. */
           rc = uscrResponseOK(pThis, pSlot, NULL, 0, 0x3); /* Clock stopped in an unknown state. */
           break;
       }

       case ICCCONNECTING:
           AssertFailed();
           RT_FALL_THRU();
       case ICCCONNECTED:
       {
           UCRLOG(("%s\n", pThis->enmICCConnState == ICCCONNECTING? "ICCCONNECTING": "ICCCONNECTED"));

           /* Connected or a connect request was sent and is still being processed.
            * The PowerOff request overrides the latter.
            */

           int rcBackend = pThis->Lun0.pCardReaderDown->pfnDisconnect(pThis->Lun0.pCardReaderDown,
                                                                      pSlot,
                                                                      VBOX_SCARD_UNPOWER_CARD);

           if (RT_SUCCESS(rcBackend))
           {
               /* Mark the command as being processed. */
               pThis->fu8Cmd |= VUSBCARDREADER_F_CMD_BUSY;

               pThis->enmICCConnState = ICCDISCONNECTING;
               pThis->enmDisconnectReason = DISCONNECT_POWEROFF;
           }
           else
           {
               rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON); /* Not supported. */

               /* Think that there is no connection anymore then. */
               pThis->enmICCConnState = ICCNOCONNECTION;
           }
           break;
       }

       case ICCDISCONNECTING:
       {
           UCRLOG(("ICCDISCONNECTING\n"));
           /* Treat this as an error condition. */
           rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_BUSY_WITH_AUTO_SEQUENCE);
           break;
       }

       default:
           AssertFailed();
           rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON); /* Not supported. */
           break;
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

/* PC_TO_RDR_GetSlotStatus */
static int usbCardReaderICCGetSlotStatus(PUSBCARDREADER pThis,
                                         PCARDREADERSLOT pSlot,
                                         const VUSBCARDREADERBULKHDR *pCmd)
{
    RT_NOREF1(pCmd);
    UCRLOG(("ICCStatus %d\n", pThis->bICCStatus));

    int rc = VINF_SUCCESS;

    if (pThis->bICCStatus == 2)
    {
        /* This seems to be what a real device returns. */
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_ICC_MUTE); /* No ICC present. */
    }
    else
    {
        uint8_t bClockStatus = pThis->bICCStatus == 0?
                                   0x00: /* Clock running */
                                   0x03; /* Clock stopped in an unknown state. */

        rc = uscrResponseOK(pThis, pSlot, NULL, 0, bClockStatus);
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

/* PC_TO_RDR_XfrBlock */
static int usbCardReaderICCXfrBlock(PUSBCARDREADER pThis,
                                    PCARDREADERSLOT pSlot,
                                    const VUSBCARDREADERBULKHDR *pCmd)
{
    UCRLOG(("\n"));

    int rc = VINF_SUCCESS;

    if (pThis->bICCStatus == 0)
    {
        rc = usbCardReaderXfrBlock(pThis, pSlot, pCmd);
    }
    else if (pThis->bICCStatus == 1)
    {
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
    }
    else
    {
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_ICC_MUTE); /* No ICC present. */
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

static int usbCardReaderMake_RDR_to_PC_Parameters(PUSBCARDREADER pThis,
                                                   PCARDREADERSLOT pSlot,
                                                   uint8_t bProtocolNum)
{
    int rc = VINF_SUCCESS;

    if (pThis->bICCStatus == 0)
    {
        /* Just return the stored parameters. */
        void *pvParms = NULL;
        uint32_t cbParms = 0;

        if (bProtocolNum == 1)
        {
            cbParms = sizeof(pSlot->ParmsT1);
            pvParms = &pSlot->ParmsT1;
        }
        else if (bProtocolNum == 0)
        {
            cbParms = sizeof(pSlot->ParmsT0);
            pvParms = &pSlot->ParmsT0;
        }
        else
        {
            AssertFailed();
        }

        if (pvParms)
        {
            rc = uscrResponseOK(pThis, pSlot, pvParms, cbParms, bProtocolNum);
        }
        else
        {
            rc = uscrResponseSlotError(pThis, pSlot, 7); /* Protocol invalid or not supported. */
        }
    }
    else if (pThis->bICCStatus == 1)
    {
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
    }
    else
    {
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_ICC_MUTE); /* No ICC present. */
    }

    return rc;
}

/* PC_to_RDR_GetParameters */
static int usbCardReaderICCGetParameters(PUSBCARDREADER pThis,
                                         PCARDREADERSLOT pSlot,
                                         const VUSBCARDREADERBULKHDR *pCmd)
{
    RT_NOREF1(pCmd);
    UCRLOG(("\n"));

    int rc = VINF_SUCCESS;

    rc = usbCardReaderMake_RDR_to_PC_Parameters(pThis, pSlot, pSlot->u8ProtocolSelector);

    UCRLOGFLEAVERC(rc);
    return rc;
}

/* PC_to_RDR_ResetParameters. */
static int usbCardReaderICCResetParameters(PUSBCARDREADER pThis,
                                           PCARDREADERSLOT pSlot,
                                           const VUSBCARDREADERBULKHDR *pCmd)
{
    RT_NOREF1(pCmd);
    UCRLOG(("\n"));

    int rc = VINF_SUCCESS;

    pSlot->ParmsT0 = g_ParmsT0;
    pSlot->ParmsT1 = g_ParmsT1;

    rc = usbCardReaderMake_RDR_to_PC_Parameters(pThis, pSlot, pSlot->u8ProtocolSelector);

    UCRLOGFLEAVERC(rc);
    return rc;
}

/* PC_to_RDR_SetParameters */
static int usbCardReaderICCSetParameters(PUSBCARDREADER pThis,
                                         PCARDREADERSLOT pSlot,
                                         const VUSBCARDREADERBULKHDR *pCmd)
{
    UCRLOG(("\n"));

    int rc = VINF_SUCCESS;

    bool fErrorReported = false;

    if (pThis->bICCStatus == 0)
    {
        const VUSBCARDREADERBULKHDR *pHdr = pCmd;

        if (pHdr->u.PC_to_RDR.u.SetParameters.bProtocolNum != pSlot->u8ProtocolSelector)
        {
            rc = uscrResponseSlotError(pThis, pSlot, 7); /* Protocol invalid or not supported. */
            fErrorReported = true;
        }
        else
        {
            /* Offset of an unsupported or incorrect parameter. */
            uint8_t u8UnsupportedOffset = 0;

            /* The CCID claims automatic parameters negotiation and allows to change only bmFindexDindex. */
            if (pHdr->u.PC_to_RDR.u.SetParameters.bProtocolNum == 0)
            {
                if (pCmd->dwLength >= sizeof(VUSBCARDREADERPARMST0))
                {
                    VUSBCARDREADERPARMST0 *pT0 = (VUSBCARDREADERPARMST0 *)&pHdr[1];

                    UCRLOG(("T0: bmFindexDindex 0x%02X, bmTCCKST0 0x%02X, bGuardTimeT0 0x%02X,"
                            " bWaitingIntegerT0 0x%02X, bClockStop 0x%02X\n",
                            pT0->bmFindexDindex, pT0->bmTCCKST0, pT0->bGuardTimeT0,
                            pT0->bWaitingIntegerT0, pT0->bClockStop));

                    pSlot->ParmsT0.bmFindexDindex = pT0->bmFindexDindex;
                }
                else
                {
                    u8UnsupportedOffset = (uint8_t)RT_UOFFSETOF(VUSBCARDREADERBULKHDR, u.PC_to_RDR.u.SetParameters.bProtocolNum);
                }
            }
            else if (pHdr->u.PC_to_RDR.u.SetParameters.bProtocolNum == 1)
            {
                if (pCmd->dwLength >= sizeof(VUSBCARDREADERPARMST1))
                {
                    VUSBCARDREADERPARMST1 *pT1 = (VUSBCARDREADERPARMST1 *)&pHdr[1];

                    UCRLOG(("T1: bmFindexDindex 0x%02X, bmTCCKST1 0x%02X, bGuardTimeT1 0x%02X,"
                            " bmWaitingIntegersT1 0x%02X, bClockStop 0x%02X, bIFSC 0x%02X, bNadValue 0x%02X\n",
                            pT1->bmFindexDindex, pT1->bmTCCKST1, pT1->bGuardTimeT1,
                            pT1->bmWaitingIntegersT1, pT1->bClockStop, pT1->bIFSC, pT1->bNadValue));

                    /* Check paramaters. */
                    if (pT1->bIFSC > 254)
                    {
                        u8UnsupportedOffset = (uint8_t)(RT_UOFFSETOF(VUSBCARDREADERPARMST1, bmTCCKST1) + sizeof(VUSBCARDREADERBULKHDR));
                    }

                    if (u8UnsupportedOffset == 0)
                    {
                        /* Change parameters only if there is no error. */
                        pSlot->ParmsT1.bmFindexDindex = pT1->bmFindexDindex;

                        #define UPDATEPARM(parm) if (pT1->parm != 0) pSlot->ParmsT1.parm = pT1->parm
                        UPDATEPARM(bmFindexDindex);
                        UPDATEPARM(bmTCCKST1);
                        UPDATEPARM(bGuardTimeT1);
                        UPDATEPARM(bmWaitingIntegersT1);
                        UPDATEPARM(bClockStop);
                        UPDATEPARM(bIFSC);
                        UPDATEPARM(bNadValue);
                        #undef UPDATEPARM
                    }
                }
                else
                {
                    u8UnsupportedOffset = (uint8_t)RT_UOFFSETOF(VUSBCARDREADERBULKHDR, u.PC_to_RDR.u.SetParameters.bProtocolNum);
                }
            }
            else
            {
                u8UnsupportedOffset = (uint8_t)RT_UOFFSETOF(VUSBCARDREADERBULKHDR, u.PC_to_RDR.u.SetParameters.bProtocolNum);
            }

            if (u8UnsupportedOffset != 0)
            {
                rc = uscrResponseSlotError(pThis, pSlot, u8UnsupportedOffset);
                fErrorReported = true;
            }
        }
    }

    if (!fErrorReported)
    {
        rc = usbCardReaderMake_RDR_to_PC_Parameters(pThis, pSlot, pSlot->u8ProtocolSelector);
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

/*
 * T0 protocol specific functions
 */

/*
 * This function process PC_to_RDR_XfrBlock (6.1.4) in T0 protocol specific way
 */
static int usbCardReaderXfrBlockT0(PUSBCARDREADER pThis,
                                   PCARDREADERSLOT pSlot,
                                   const VUSBCARDREADERBULKHDR *pCmd)
{
    int rc = VINF_SUCCESS;

    UCRLOG(("\n%.*Rhxd\n", pCmd->dwLength, &pCmd[1]));

    Assert(pSlot->u8ProtocolSelector == 0);

    /* T0 can receive up to 256 bytes of data + 2 status bytes. */
    int rcBackend = pThis->Lun0.pCardReaderDown->pfnTransmit(pThis->Lun0.pCardReaderDown,
                                                             pSlot,
                                                             &g_PdmIoRecvT0,
                                                             (uint8_t *)&pCmd[1],
                                                             pCmd->dwLength,
                                                             256 + 2);

    if (RT_SUCCESS(rcBackend))
    {
        /* Mark the command as being processed. */
        pThis->fu8Cmd |= VUSBCARDREADER_F_CMD_BUSY;
    }
    else
    {
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}


/*
 * T1 protocol specific functions
 */

/* Checksum notice:
 * opened part of 7816-3 doesn't mentioned which polynome or method should be used.
 * PCSC reference on LRC and CRC16-CCITT
 */
static bool usbCardReaderIsCrc16ChkSum(PCARDREADERSLOT pSlot)
{
    return (   (   pSlot->Atr.TC.u8PresentBitMask & ISO7816_ATR_TX2
                && pSlot->Atr.TC.auProtoBytes[2] == 1)
            || (   pSlot->Atr.TC.u8PresentBitMask & ISO7816_ATR_TX3
                && pSlot->Atr.TC.auProtoBytes[3] == 1));
}

/* CRC functions 16-bit */
static int usbCardReaderT1ChkSumCrc16(uint16_t *pu16ChkSum, const uint8_t *pbBlock, size_t cbBlock)
{
    /** @todo implement */
    RT_NOREF3(pu16ChkSum, pbBlock, cbBlock);
    int rc = VERR_NOT_IMPLEMENTED;
    UCRLOG(("\n"));
    AssertFailed();
    UCRLOGFLEAVERC(rc);
    return rc;
}

static int usbCardReaderT1ChkSumLrc(uint8_t *pu8ChkSum, const uint8_t *pbBlock, size_t cbBlock)
{
    int rc = VINF_SUCCESS;

    UCRLOGF(("\n"));

    uint8_t u8ChkSum = 0;
    while (cbBlock--)
        u8ChkSum ^= *pbBlock++;

    *pu8ChkSum = u8ChkSum;

    UCRLOGFLEAVERC(rc);
    return rc;
}

static int usbCardReaderT1ChkSum(PCARDREADERSLOT pSlot, uint8_t *pu8Sum, const uint8_t *pbBlock, size_t cbBlock)
{
    int rc = VINF_SUCCESS;

    UCRLOGF(("pu8Sum:%p, pbBlock:%.*Rhxs, cbBlock:%d\n", pu8Sum, cbBlock, pbBlock, cbBlock));

    if (RT_UNLIKELY(usbCardReaderIsCrc16ChkSum(pSlot)))
    {
        rc = usbCardReaderT1ChkSumCrc16((uint16_t *)pu8Sum, pbBlock, cbBlock);
        UCRLOGF(("pu8Sum16:%RX16\n", *(uint16_t *)pu8Sum));
    }
    else
    {
        rc = usbCardReaderT1ChkSumLrc(pu8Sum, pbBlock, cbBlock);
        UCRLOGF(("pu8Sum8:%RX8\n", *pu8Sum));
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

static bool usbCardReaderT1ValidateChkSum(PCARDREADERSLOT pSlot, const uint8_t *pbBlock, size_t cbBlock)
{
    UCRLOGF(("pbBlock:%.*Rhxs, cbBlock:%d\n", cbBlock, pbBlock, cbBlock));

    uint8_t au8Sum[2];
    uint8_t cbSum = usbCardReaderIsCrc16ChkSum(pSlot) ? 2 : 1;
    if (cbBlock < cbSum)
        return false;

    int rc = usbCardReaderT1ChkSum(pSlot, au8Sum, pbBlock, cbBlock - cbSum);

    if (RT_SUCCESS(rc))
    {
        return (!memcmp(au8Sum, &pbBlock[cbBlock - cbSum], cbSum));
    }

    return false;
}

/*
 * This function calculates size of T1 block for header and body and allocates required amount of memory
 */
static int usbCardReaderT1CreateBlock(PCARDREADERSLOT pSlot,
                                      PPT1BLKHEADER ppT1Block,
                                      uint32_t *pcbT1Block,
                                      uint8_t u8Nad,
                                      uint8_t u8PcbFlags,
                                      uint8_t *pu8T1BodyBlock,
                                      uint8_t cbT1BodyBlock)
{
    UCRLOG(("ENTER: pSlot:%p, ppT1Block:%p, u8Nad:%RX8, u8PcbFlags:%RX8, cbT1BodyBlock:%d\n",
            pSlot, ppT1Block, u8Nad, u8PcbFlags, cbT1BodyBlock));

    int rc = VINF_SUCCESS;

    uint32_t const cbChkSum = usbCardReaderIsCrc16ChkSum(pSlot) ? 2 : 1;

    uint32_t const cbT1Blk =   sizeof(T1BLKHEADER)
                             + cbT1BodyBlock
                             + cbChkSum;

    PT1BLKHEADER pT1Blk = (PT1BLKHEADER)RTMemAllocZ(cbT1Blk);
    AssertReturn(pT1Blk, VERR_NO_MEMORY);

    pT1Blk->u8Nad = u8Nad;
    pT1Blk->u8Pcb = u8PcbFlags;
    pT1Blk->u8Len = cbT1BodyBlock;

    if (   pu8T1BodyBlock
        && cbT1BodyBlock)
    {
        memcpy(&pT1Blk[1], pu8T1BodyBlock, cbT1BodyBlock);
    }

    rc = usbCardReaderT1ChkSum(pSlot,
                               /* last byte(s) reserved chksum */
                               ((uint8_t *)pT1Blk) + cbT1Blk - cbChkSum,
                               (uint8_t *)pT1Blk,
                               cbT1Blk - cbChkSum);
    if (RT_SUCCESS(rc))
    {
        *ppT1Block = pT1Blk;
        *pcbT1Block = cbT1Blk;
    }
    else
    {
        RTMemFree(pT1Blk);
    }

    UCRLOGF(("LEAVE: %Rrc\n", rc));
    return rc;
}

static int usbCardReaderT1BlkSProcess(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, PT1BLKHEADER pT1BlkHeader)
{
    UCRLOG(("ENTER: pThis:%p, pSlot:%p, pT1BlkHeader:%.*Rhxs\n",
            pThis,
            pSlot,
            sizeof(T1BLKHEADER) + pT1BlkHeader->u8Len + (usbCardReaderIsCrc16ChkSum(pSlot) ? 2 : 1),
            pT1BlkHeader));

    int rc = VINF_SUCCESS;

    if ((pT1BlkHeader->u8Pcb & ISO7816_T1_BLK_S_RESPONSE) != 0)
    {
        /* It is a response block. */
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON);
    }
    else
    {
        PT1BLKHEADER pT1BlkHeaderResponse = NULL;
        uint32_t cbT1BlkHeaderResponse = 0;

        switch (pT1BlkHeader->u8Pcb & ~ISO7816_T1_BLK_TYPE_MASK)
        {
            case 0x01: /* IFS request */
            {
                UCRLOG(("ISO7816_T1_BLK_S_IFS\n"));
                rc = usbCardReaderT1CreateBlock(pSlot,
                                                &pT1BlkHeaderResponse,
                                                &cbT1BlkHeaderResponse,
                                                pT1BlkHeader->u8Nad,
                                                pT1BlkHeader->u8Pcb | ISO7816_T1_BLK_S_RESPONSE,
                                                (uint8_t *)&pT1BlkHeader[1],
                                                pT1BlkHeader->u8Len);
            } break;

            case 0x00: /* RESYNCH request */
            {
                UCRLOG(("ISO7816_T1_BLK_S_RESYNCH\n"));

                /* After resynch this must be set to the initial value. */
                pSlot->u8IBlockNS = 0;
                usbCardReaderChainReset(pSlot);

                rc = usbCardReaderT1CreateBlock(pSlot,
                                                &pT1BlkHeaderResponse,
                                                &cbT1BlkHeaderResponse,
                                                pT1BlkHeader->u8Nad,
                                                pT1BlkHeader->u8Pcb | ISO7816_T1_BLK_S_RESPONSE,
                                                (uint8_t *)&pT1BlkHeader[1],
                                                pT1BlkHeader->u8Len);
            } break;

            case 0x02:  /* ABORT request */
            {
                UCRLOG(("ISO7816_T1_BLK_S_ABORT\n"));
                rc = VERR_NOT_SUPPORTED;
            } break;

            case 0x03: /* WTX request */
            {
                UCRLOG(("ISO7816_T1_BLK_S_WTX\n"));
                rc = VERR_NOT_SUPPORTED;
            } break;

            default:
            {
                UCRLOG(("ISO7816_T1_BLK_S_? 0x%02X\n", pT1BlkHeader->u8Pcb & ~ISO7816_T1_BLK_TYPE_MASK));
                rc = VERR_NOT_SUPPORTED;
            } break;
        }

        if (RT_SUCCESS(rc))
        {
            rc = uscrResponseOK(pThis, pSlot, pT1BlkHeaderResponse, cbT1BlkHeaderResponse, 0);
            RTMemFree(pT1BlkHeaderResponse);
        }
        else
        {
            rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON);
        }
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

static int usbCardReaderT1BlkRProcess(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, PT1BLKHEADER pT1BlkHeader)
{
    UCRLOG(("ENTER: pThis:%p, pSlot:%p, pT1BlkHeader:%.*Rhxs\n",
            pThis, pSlot,
            sizeof(T1BLKHEADER) + pT1BlkHeader->u8Len + (usbCardReaderIsCrc16ChkSum(pSlot) ? 2 : 1),
            pT1BlkHeader));

    int rc = VINF_SUCCESS;

    PT1BLKHEADER pT1Response = NULL;
    uint32_t cbT1Response = 0;

    /* The host wants a new I-block in the chain. */
    if (pSlot->enmChainMode == IBLOCKCHAIN_FROM_DEVICE)
    {
        /* Determine which part of the data can be sent. */
        Assert(pSlot->cbIBlockChain > pSlot->offIBlockChain);

        uint8_t *pu8BlockData = &pSlot->pu8IBlockChain[pSlot->offIBlockChain];
        uint32_t cbBlockData = pSlot->cbIBlockChain - pSlot->offIBlockChain;

        if (cbBlockData > USED_IFSC)
        {
            cbBlockData = USED_IFSC;
        }

        /* Update the read offset. */
        pSlot->offIBlockChain += cbBlockData;

        uint8_t u8More = pSlot->offIBlockChain < pSlot->cbIBlockChain? ISO7816_T1_BLK_I_MORE: 0;

        UCRLOG(("chain I-block %d bytes, more 0x%02X\n",
                cbBlockData, u8More));

        /* Check that current I-block number is the same as the requested block number. */
        Assert((pSlot->u8IBlockNS == 0) == ((pT1BlkHeader->u8Pcb & ISO7816_T1_BLK_R_NR) == 0));

        rc = usbCardReaderT1CreateBlock(pSlot,
                                        &pT1Response,
                                        &cbT1Response,
                                        0,
                                        ISO7816_T1_BLK_I | pSlot->u8IBlockNS | u8More,
                                        pu8BlockData,
                                        cbBlockData);
        pSlot->u8IBlockNS ^= ISO7816_T1_BLK_I_NS;

        if (!u8More)
        {
            usbCardReaderChainReset(pSlot);
        }
    }
    else
    {
        /* Unexpected. */
        UCRLOG(("Unexpected R-block!!!\n"));

        /* Just return a no error response. */
        rc = usbCardReaderT1CreateBlock(pSlot,
                                        &pT1Response,
                                        &cbT1Response,
                                        pT1BlkHeader->u8Nad,
                                        ISO7816_T1_BLK_R, /* u8Pcb */
                                        NULL,
                                        0 /* u8Len */);

    }

    if (RT_SUCCESS(rc))
    {
        rc = uscrResponseOK(pThis, pSlot, pT1Response, cbT1Response, 0);
    }
    else
    {
        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON);
    }

    RTMemFree(pT1Response);

    UCRLOGFLEAVERC(rc);
    return rc;
}

static uint8_t usbCardReaderT1ValidateBlock(PCARDREADERSLOT pSlot,
                                            const VUSBCARDREADERBULKHDR *pCmd)
{
    uint8_t enmMsgStatus = VUSBCARDREADER_MSG_STATUS_ERR_NON;

    /* T1 block is T1BLKHEADER + data[T1BLKHEADER::u8Len] + checksum. */
    uint32_t cbT1Block = sizeof(T1BLKHEADER) + (usbCardReaderIsCrc16ChkSum(pSlot) ? 2 : 1);
    if (pCmd->dwLength < cbT1Block)
    {
        enmMsgStatus = VUSBCARDREADER_MSG_STATUS_ERR_XFR_OVERRUN;
    }
    else
    {
        PT1BLKHEADER pT1Hdr = (PT1BLKHEADER)&pCmd[1];
        cbT1Block += pT1Hdr->u8Len;
        if (pCmd->dwLength < cbT1Block)
        {
            enmMsgStatus = VUSBCARDREADER_MSG_STATUS_ERR_XFR_OVERRUN;
        }
        else
        {
            bool const fT1ChkSumValid = usbCardReaderT1ValidateChkSum(pSlot, (uint8_t *)&pCmd[1], pCmd->dwLength);
            if (RT_UNLIKELY(!fT1ChkSumValid))
            {
                enmMsgStatus = VUSBCARDREADER_MSG_STATUS_ERR_XFR_PARITY_ERROR;
            }
        }
    }

    return enmMsgStatus;
}

/*
 * This function process PC_to_RDR_XfrBlock (6.1.4) in T1 protocol specific way.
 */
static int usbCardReaderXfrBlockT1(PUSBCARDREADER pThis,
                                   PCARDREADERSLOT pSlot,
                                   const VUSBCARDREADERBULKHDR *pCmd)
{
    int rc = VINF_SUCCESS;

    UCRLOG(("bBWI %d, wLevelParameter 0x%x\n",
            pCmd->u.PC_to_RDR.u.XfrBlock.bBWI, pCmd->u.PC_to_RDR.u.XfrBlock.wLevelParameter));

    Assert(pSlot->u8ProtocolSelector == 1);

    uint8_t const enmMsgStatus = usbCardReaderT1ValidateBlock(pSlot, pCmd);

    if (RT_UNLIKELY(enmMsgStatus != VUSBCARDREADER_MSG_STATUS_ERR_NON))
    {
        rc = uscrResponseSlotError(pThis, pSlot, enmMsgStatus);
    }
    else
    {
        PT1BLKHEADER pT1Hdr = (PT1BLKHEADER)&pCmd[1];

        UCRLOG(("pT1Hdr->u8Len %d, pCmd->dwLength %d, pT1Hdr->u8Pcb 0x%02X\n",
                pT1Hdr->u8Len, pCmd->dwLength, pT1Hdr->u8Pcb));

        switch (pT1Hdr->u8Pcb & ISO7816_T1_BLK_TYPE_MASK)
        {
            case ISO7816_T1_BLK_S:
            {
                UCRLOG(("ISO7816_T1_BLK_S\n"));
                rc = usbCardReaderT1BlkSProcess(pThis, pSlot, pT1Hdr);
            } break;

            case ISO7816_T1_BLK_R:
            {
                UCRLOG(("ISO7816_T1_BLK_R\n"));
                /* setParameters bIFSC */
                rc = usbCardReaderT1BlkRProcess(pThis, pSlot, pT1Hdr);
            } break;

            default: /* ISO7816_T1_BLK_I and ISO7816_T1_BLK_I1 */
            {
                UCRLOG(("ISO7816_T1_BLK_I\n"));

                if ((pT1Hdr->u8Pcb & ISO7816_T1_BLK_I_MORE) != 0)
                {
                    /* The host sends a chain of I-blocks.
                     * Save the current portion and reply with a R-block.
                     */
                    rc = usbCardReaderChainAdd(pSlot, (uint8_t *)&pT1Hdr[1], pT1Hdr->u8Len);

                    if (RT_FAILURE(rc))
                    {
                        /** @todo test if this should stall the endpoint */
                        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
                        break;
                    }

                    /* Reply with a R-block. */
                    PT1BLKHEADER pT1Response = NULL;
                    uint32_t cbT1Response = 0;

                    /* Tell the host that a next block is expected. */
                    uint8_t u8NR = ((pT1Hdr->u8Pcb & ISO7816_T1_BLK_I_NS) != 0)? 0: ISO7816_T1_BLK_R_NR;

                    rc = usbCardReaderT1CreateBlock(pSlot,
                                                    &pT1Response,
                                                    &cbT1Response,
                                                    pT1Hdr->u8Nad,
                                                    ISO7816_T1_BLK_R | u8NR, /* u8Pcb */
                                                    NULL,
                                                    0 /* u8Len */);

                    if (RT_FAILURE(rc))
                    {
                        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
                        break;
                    }

                    rc = uscrResponseOK(pThis, pSlot, pT1Response, cbT1Response, 0);

                    RTMemFree(pT1Response);
                }
                else
                {
                    /* A single block or a last block in a chain. */
                    uint8_t *pu8Data = NULL;
                    uint32_t cbData = 0;

                    if (pSlot->enmChainMode == IBLOCKCHAIN_FROM_HOST)
                    {
                        rc = usbCardReaderChainAdd(pSlot, (uint8_t *)&pT1Hdr[1], pT1Hdr->u8Len);

                        if (RT_FAILURE(rc))
                        {
                            rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
                            break;
                        }

                        pu8Data = pSlot->pu8IBlockChain;
                        cbData = pSlot->cbIBlockChain;
                    }
                    else
                    {
                        pu8Data = (uint8_t *)&pT1Hdr[1];
                        cbData = pT1Hdr->u8Len;
                    }

                    if (pu8Data)
                    {
                        UCRLOG(("\n%.*Rhxd\n", cbData, pu8Data));

                        int rcBackend = pThis->Lun0.pCardReaderDown->pfnTransmit(pThis->Lun0.pCardReaderDown,
                                                                                 pSlot,
                                                                                 &g_PdmIoRecvT1,
                                                                                 pu8Data,
                                                                                 cbData,
                                                                                 64 * _1K);

                        if (RT_SUCCESS(rcBackend))
                        {
                            /* Mark the command as being processed. */
                            pThis->fu8Cmd |= VUSBCARDREADER_F_CMD_BUSY;
                        }
                        else
                        {
                            rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
                        }
                    }

                    usbCardReaderChainReset(pSlot);
                }
            } break;
        }
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

/*
 * This function do basic validation of incoming PC_to_RDR_XfrBlock (6.1.4)
 * request and pass for processing with protocol specific routines.
 */
static int usbCardReaderXfrBlock(PUSBCARDREADER pThis,
                                 PCARDREADERSLOT pSlot,
                                 const VUSBCARDREADERBULKHDR *pCmd)
{
    int rc = VINF_SUCCESS;

    UCRLOGF(("pThis:%p, pSlot:%p\n", pThis, pSlot));

    /* The USB CCID spec says that the block should never exceed the dwMaxCCIDMessageLength-10
     * in the Class Descriptor. But the host sends larger blocks. 258 bytes, which was IFSD + 3 + 1.
     */
    /** @todo So send blocks, which do not exceed the IFSD? */
    switch (pSlot->u8ProtocolSelector)
    {
        case 0:
        {
            rc = usbCardReaderXfrBlockT0(pThis, pSlot, pCmd);
        } break;
        case 1:
        {
            rc = usbCardReaderXfrBlockT1(pThis, pSlot, pCmd);
        } break;
        default:
        {
            rc = uscrResponseSlotError(pThis, pSlot, 7); /* Protocol invalid or not supported. */
        } break;
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}


/*
 * This function should be called when ATR received from ICC
 * ISO-7618-3 (partly available here http://www.cardwerk.com/smartcards/smartcard_standard_ISO7816-3.aspx)
 */
static int usbCardReaderParseATR(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot)
{
    int rc = VINF_SUCCESS;
    UCRLOG(("ENTER: pThis:%p, pSlot:%p\n", pThis, pSlot));

    /* no received ATR */
    AssertReturn(   pSlot->Atr.pu8RawATR
                 && pSlot->Atr.cbRawATR, VERR_INVALID_PARAMETER);

    /** @todo have we already initialized ATR structure and if it's match the ICC inserted in reader? */
    PCARDATR pAtr = &pSlot->Atr;
    pAtr->u8TS = pAtr->pu8RawATR[0];
    pAtr->u8T0 = pAtr->pu8RawATR[1];

    RT_ZERO(pAtr->TA);
    RT_ZERO(pAtr->TB);
    RT_ZERO(pAtr->TC);
    RT_ZERO(pAtr->TD);

    uint8_t u8TDi = pAtr->u8T0;
    int idxProtocol = 0;
    uint8_t u8RawAtrPointer = 2;
    while (   idxProtocol < ISO7816_MAX_BYTES_PER_INTERFACE
           && u8RawAtrPointer < pSlot->Atr.cbRawATR)
    {
        /* TAi */
        if (u8TDi & ISO7816_ATR_TAi)
        {
            pAtr->TA.auProtoBytes[idxProtocol] = pAtr->pu8RawATR[u8RawAtrPointer];
            pAtr->TA.u8PresentBitMask |= RT_BIT(idxProtocol);
            u8RawAtrPointer++;
        }
        /* TBi */
        if (u8TDi & ISO7816_ATR_TBi)
        {
            pAtr->TB.auProtoBytes[idxProtocol] = pAtr->pu8RawATR[u8RawAtrPointer];
            pAtr->TB.u8PresentBitMask |= RT_BIT(idxProtocol);
            u8RawAtrPointer++;
        }

        /* TCi */
        if (u8TDi & ISO7816_ATR_TCi)
        {
            pAtr->TC.auProtoBytes[idxProtocol] = pAtr->pu8RawATR[u8RawAtrPointer];
            pAtr->TC.u8PresentBitMask |= RT_BIT(idxProtocol);
            u8RawAtrPointer++;
        }

        /* TDi */
        if (u8TDi & ISO7816_ATR_TDi)
        {
            /* u8TDi changes */
            u8TDi = pAtr->TD.auProtoBytes[idxProtocol] = pAtr->pu8RawATR[u8RawAtrPointer];
            pAtr->TD.u8PresentBitMask |= RT_BIT(idxProtocol);
            u8RawAtrPointer++;
            idxProtocol++;
        }
        else
            break;
    }

    /** @todo What else we need from ATR? */

    UCRLOGFLEAVERC(rc);
    return rc;
}


static int usbCardReaderSRToHostTodevice(PUSBCARDREADER pThis,PUSBCARDREADEREP pEp, PVUSBURB pUrb, PCVUSBSETUP pSetup)
{
    UCRLOG(("ENTER: iInstance:%d pEp:%p pUrb:%p pSetup:%p\n",
            pThis->pUsbIns->iInstance, pEp, pUrb, pSetup));

    int rc = VINF_SUCCESS;

    switch (pSetup->bRequest)
    {
        case VUSB_REQ_GET_STATUS:
        {
            UCRLOG(("GET_STATUS\n"));
            uint32_t u32Status = 0; /* no remote wakeup, bus-powered. */
            rc = usbCardReaderCompleteSetupOk(pThis, pUrb, sizeof(uint32_t), &u32Status);
        } break;

        case VUSB_REQ_GET_DESCRIPTOR:
        case VUSB_REQ_GET_CONFIGURATION:
        case VUSB_REQ_GET_INTERFACE:
        default:
            rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
    }

    UCRLOGF(("LEAVE: rc:%Rrc\n", rc));
    return rc;
}

static int usbCardReaderCSToHost(PUSBCARDREADER pThis,PUSBCARDREADEREP pEp, PVUSBURB pUrb, PVUSBSETUP pSetup)
{
    UCRLOG(("ENTER: pThis:%p, pEp:%RTbool, pUrb:%p pSetup:%p\n", pThis, pEp->fHalted, pUrb, pSetup));

    int rc = VINF_SUCCESS;

    switch (pSetup->bRequest)
    {
        case VUSB_CARDREADER_REQ_ABORT:
            UCRLOG(("ABORT\n"));
            /** @todo inform thread talking to backend inform to abort current operation */
            /** @todo prepare corresponding RDR_to_PC_SlotStatus msg and check PC_to_RDR_Abort req
             * please read 5.3.1 for more details.
             * As I understand this control message should validate PC_to_RDR_Abort
             *
             * Same bSeq as in the PC_to_RDR_Abort command
             * Call backend Cancel.
             */
            rc = usbCardReaderCompleteOk(pThis, pUrb, sizeof(pSetup));
            break;
        case VUSB_CARDREADER_REQ_GET_DATA_RATES:
            /* A CCID with bNumDataRatesSupported equal to 00h does not have to support this request. */
        case VUSB_CARDREADER_REQ_GET_CLOCK_FREQUENCIES:
            /* A CCID with bNumClockSupported equal to 00h does not have to support this request. */
        default:
            rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
    }

    UCRLOGF(("LEAVE: rc:%Rrc\n", rc));
    return rc;
}

/*
 * Pipe handlers.
 */

/*
 * Control pipe
 */
static int usbCardReaderDefaultPipe(PUSBCARDREADER pThis, PUSBCARDREADEREP pEp, PVUSBURB pUrb)
{
    UCRLOG(("ENTER: pThis: %p, pUrb:%p\n", pThis, pUrb));

    int rc = VINF_SUCCESS;

    if (RT_UNLIKELY(pUrb->cbData < sizeof(VUSBSETUP)))
    {
        UCRLOG(("pUrb->cbData %d\n", pUrb->cbData));
        AssertFailedStmt(rc = VERR_NOT_SUPPORTED);
    }
    else
    {
        PVUSBSETUP pSetup = (PVUSBSETUP)pUrb->pbData;

        switch (pSetup->bmRequestType & VUSB_REQ_MASK)
        {
            case VUSB_REQ_STANDARD:
                if ((pSetup->bmRequestType & VUSB_DIR_MASK) == VUSB_DIR_TO_HOST)
                {
                    switch (pSetup->bmRequestType & VUSB_RECIP_MASK)
                    {
                        case VUSB_TO_DEVICE:
                            rc = usbCardReaderSRToHostTodevice(pThis, pEp, pUrb, pSetup);
                            break;
                        case VUSB_TO_ENDPOINT:
                        case VUSB_TO_INTERFACE:
                        case VUSB_TO_OTHER:
                        default:
                            rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
                    }
                }
                else
                {
                    switch (pSetup->bmRequestType & VUSB_RECIP_MASK)
                    {
                        case VUSB_TO_ENDPOINT:
                        {
                            if (pSetup->bRequest == VUSB_REQ_CLEAR_FEATURE)
                            {
                                UCRLOG(("endpoint:CLEAR_FEATURE: wValue %d, wIndex 0x%02X\n",
                                         pSetup->wValue, pSetup->wIndex));
                                /** @todo */
                                unsigned i;
                                for (i = 0; i < RT_ELEMENTS(pThis->aEps); i++)
                                {
                                    pThis->aEps[i].fHalted = false;
                                }

                                uscrResponseCleanup(pThis);

                                rc = usbCardReaderCompleteOk(pThis, pUrb, pUrb->cbData);
                            }
                            else
                            {
                                rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
                            }
                        } break;

                        case VUSB_TO_DEVICE:
                        case VUSB_TO_INTERFACE:
                        case VUSB_TO_OTHER:
                        default:
                            rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
                    }
                }
                break;
            case VUSB_REQ_CLASS:
                if ((pSetup->bmRequestType & VUSB_DIR_MASK) == VUSB_DIR_TO_HOST)
                {
                    switch (pSetup->bmRequestType & VUSB_RECIP_MASK)
                    {
                        /* Linux and Windows guest make different requests? */
                        case VUSB_TO_DEVICE:
                        case VUSB_TO_INTERFACE:
                            rc = usbCardReaderCSToHost(pThis, pEp, pUrb, pSetup);
                            break;
                        case VUSB_TO_ENDPOINT:
                        case VUSB_TO_OTHER:
                        default:
                            rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
                    }
                }
                else
                {
                   rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
                }
                break;
            case VUSB_REQ_VENDOR:
            default:
                rc = usbCardReaderCompleteSetupUnsupported(pThis, pUrb);
        }
    }

    UCRLOGF(("LEAVE: rc:%Rrc\n", rc));
    return rc;
}

/*
 * Bulk Out pipe
 */
static int usbCardReaderBulkOutPipe(PUSBCARDREADER pThis, PUSBCARDREADEREP pEp, PVUSBURB pUrb)
{
    UCRLOG(("pThis %p, pEp %RTbool, pUrb %p, cbData %d\n%.*Rhxd\n",
            pThis, pEp->fHalted, pUrb, pUrb->cbData, pUrb->cbData, pUrb->pbData));

    int rc = VINF_SUCCESS; /* If not success then stall. */

    if (RT_UNLIKELY(pEp->fHalted))
    {
        UCRLOG(("halted\n"));
        rc = VERR_NOT_SUPPORTED;
    }
    else if (RT_UNLIKELY(pUrb->cbData < sizeof(VUSBCARDREADERBULKHDR)))
    {
        UCRLOG(("pUrb->cbData %d\n", pUrb->cbData));
        AssertFailed();
        rc = VERR_NOT_SUPPORTED;
    }
    else
    {
        const VUSBCARDREADERBULKHDR *pCmd = (VUSBCARDREADERBULKHDR *)&pUrb->pbData[0];

        /* Save the command header. Other methods will take necessary info from there. */
        pThis->command.hdr = *pCmd;
        pThis->command.bMessageTypeRsp = pCmd->bMessageType;

        /* Find the command. */
        PUSBCARDREADERICCREQPROC pProc = NULL;
        unsigned idxProc;
        for (idxProc = 0; idxProc < RT_ELEMENTS(g_UsbCardReaderICCProc); ++idxProc)
        {
            if (pCmd->bMessageType == g_UsbCardReaderICCProc[idxProc].u8ReqId)
            {
                pProc = &g_UsbCardReaderICCProc[idxProc];
                break;
            }
        }

        if (pProc)
        {
            /* Set the correct response message type. */
            pThis->command.bMessageTypeRsp = pProc->u8RespId;

            if (RT_UNLIKELY(pCmd->bSlot >= pThis->cSlots))
            {
                UCRLOG(("Invalid slot %d\n", pCmd->bSlot));
                rc = uscrResponseSlotError(pThis, NULL, VUSBCARDREADER_MSG_STATUS_ERR_SLOT_DOESNT_EXIST);
            }
            else
            {
                CARDREADERSLOT *pSlot = &pThis->paSlots[pCmd->bSlot];

                if (RT_UNLIKELY((pThis->fu8Cmd & VUSBCARDREADER_F_CMD_BUSY) != 0))
                {
                    UCRLOG(("Slot busy\n"));
                    rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_CMD_SLOT_BUSY);
                }
                else if (RT_UNLIKELY(pProc->pfnCardReaderIccProc == NULL))
                {
                    UCRLOG(("Unsupported bMessageType 0x%02X: %R[cardreadermsgreq]\n", pCmd->bMessageType, pCmd));
                    rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON);
                }
                else if (RT_UNLIKELY(pCmd->dwLength > pUrb->cbData - sizeof(VUSBCARDREADERBULKHDR)))
                {
                    UCRLOG(("Bad pCmd->dwLength %d\n", pCmd->dwLength));
                    /* RDR_to_PC_DataBlock returns offset of dwLength (1). Otherwise just return a generic error. */
                    if (pProc->u8RespId == VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_DATABLOCK)
                        rc = uscrResponseSlotError(pThis, pSlot, 1); /* 6.1.4 PC_to_RDR_XfrBlock: "Bad dwLength" */
                    else
                        rc = uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON);
                }
                else
                {
                    /* The command is accepted in principle. The BUSY flag will be set by
                     * the corresponding handler if the command needs more processing.
                     */

                    UCRLOG(("Processing %R[cardreadermsgreq]\n", pCmd));

                    /* Process the command. */
                    rc = pProc->pfnCardReaderIccProc(pThis, pSlot, pCmd);

                    UCRLOG(("ICCProc 0x%02X fu8Cmd 0x%02X rc %Rrc\n", pCmd->bMessageType, pThis->fu8Cmd, rc));
                }
            }
        }
        else
        {
            UCRLOG(("Invalid bMessageType 0x%02X\n", pCmd->bMessageType));
            rc = uscrResponseSlotError(pThis, NULL, VUSBCARDREADER_MSG_STATUS_ERR_NON);
        }
    }

    if (RT_SUCCESS(rc))
    {
        rc = usbCardReaderCompleteOk(pThis, pUrb, pUrb->cbData);
    }
    else
    {
        rc = usbCardReaderCompleteEpStall(pThis, pEp, pUrb);
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}


/*
 * Bulk In pipe
 */
static int usbCardReaderBulkInPipe(PUSBCARDREADER pThis, PUSBCARDREADEREP pEp, PVUSBURB pUrb)
{
    RT_NOREF1(pEp);
    UCRLOGF(("iInstance:%d pUrb %p cbData %d, fu8Cmd %RX8\n",
             pThis->pUsbIns->iInstance, pUrb, pUrb->cbData, pThis->fu8Cmd));

    /* It could it be that there are already URBs to be completed before the submitted one.
     * Add the URB to the BulkIn queue and complete URBs from the queue.
     */
    urbQueueAddTail(&pThis->urbQueues.BulkIn, pUrb);
    urbQueueComplete(pThis, &pThis->urbQueues.BulkIn, uscrResponseRead);

    return VINF_SUCCESS;
}


static int uscrEventRead(PUSBCARDREADER pThis, uint8_t *pu8Data, uint32_t cbData, uint32_t *pcbOut)
{
    ASSERT_GUEST_RETURN(cbData >= sizeof(VUSBCARDREADERNOTIFYSLOTCHANGE), VERR_BUFFER_OVERFLOW);

    int rc = VINF_SUCCESS;
    if (pThis->fICCStateChanged)
    {
        pThis->fICCStateChanged = false;

        PVUSBCARDREADERNOTIFYSLOTCHANGE pNotify = (PVUSBCARDREADERNOTIFYSLOTCHANGE)pu8Data;
        pNotify->Core.bMessageType = VUSBCARDREADER_MSG_TYPE_RDR_TO_PC_NOTIFY_SLOT_CHANGES;
        pNotify->bmSlotICCState = 0x02;  /* Slot status was changed. */

        if (   pThis->bICCStatus == VUSBCARDREADER_ICC_STATUS_PRESENT
            || pThis->bICCStatus == VUSBCARDREADER_ICC_STATUS_INACTIVE)
        {
            pNotify->bmSlotICCState |= 0x01; /* ICC present */
        }

        *pcbOut = sizeof(VUSBCARDREADERNOTIFYSLOTCHANGE);

        UCRLOG(("Reporting a slot change\n%.*Rhxs\n", *pcbOut, pu8Data));
    }
    else
        rc = VERR_NO_DATA;

    return rc;
}


/*
 * Int pipe
 */
static int usbCardReaderIntPipe(PUSBCARDREADER pThis, PUSBCARDREADEREP pEp, PVUSBURB pUrb)
{
    RT_NOREF1(pEp);
    UCRLOG(("intr pUrb:%p, cbData:%d\n",
            pUrb, pUrb->cbData));

    /* If there is a pending URB, complete it without data. It is most likely cancelled.
     *
     * The card reader does not complete URB if there is no data. Therefore if a new intr-in URB is submitted,
     * it means that the old URB is not ok anymore and returned data in it may be lost.
     */
    urbQueueComplete(pThis, &pThis->urbQueues.IntrIn, NULL /* no data */);

    urbQueueAddTail(&pThis->urbQueues.IntrIn, pUrb);
    urbQueueComplete(pThis, &pThis->urbQueues.IntrIn, uscrEventRead);

    return VINF_SUCCESS;
}

/*
 * Card reader helper functions
 */
DECLINLINE(int) uscrLock(PUSBCARDREADER pThis)
{
    return RTCritSectEnter(&pThis->csThis);
}

DECLINLINE(void) uscrUnlock(PUSBCARDREADER pThis)
{
    RTCritSectLeave(&pThis->csThis);
}

static void usbCardReaderUpdateICCStatus(PUSBCARDREADER pThis, uint8_t bICCStatus, bool fForceChanged)
{
    bool fCardPresentNew = (bICCStatus != VUSBCARDREADER_ICC_STATUS_NO_CARD);
    bool fCardPresentOld = (pThis->bICCStatus != VUSBCARDREADER_ICC_STATUS_NO_CARD);
    if (   bICCStatus != pThis->bICCStatus
        || fForceChanged)
    {
        UCRLOG(("bICCStatus %d (current %d), fForceChanged %d, fICCStateChanged %d\n",
                bICCStatus, pThis->bICCStatus, fForceChanged, pThis->fICCStateChanged));

        pThis->bICCStatus = bICCStatus;

        /*
         * If there was a status change then do not forget it.
         * For example when bICCStatus is updated not from StatusChnage, then the change may be lost
         * if later the StatusChange reports the same status.
         */
        pThis->fICCStateChanged = pThis->fICCStateChanged || (fCardPresentNew != fCardPresentOld);
    }
}

static void usbCardReaderSendGetStatusChange(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot)
{
    AssertReturnVoid(pThis->enmStatusChange == SLOT_STATUS_CHANGE_REQUESTED);

    pThis->enmStatusChange = SLOT_STATUS_CHANGE_SENT;

    pSlot->ReaderStats.pszReaderName = pSlot->pszCardReaderName;
    /* Only leave the EMPTY/PRESENT flags in the current state. If other flags are set, some clients
     * immediately return even if the current state has not changed.
     */
    pSlot->ReaderStats.u32CurrentState =   pThis->u32EventStateBackend
                                         & (VBOX_SCARD_STATE_EMPTY | VBOX_SCARD_STATE_PRESENT);

    int rcBackend = pThis->Lun0.pCardReaderDown->pfnGetStatusChange(pThis->Lun0.pCardReaderDown,
                                                                    pSlot,
                                                                    RT_INDEFINITE_WAIT,
                                                                    &pSlot->ReaderStats,
                                                                    1);

    if (RT_SUCCESS(rcBackend))
    {
        /* Do nothing. */
    }
    else
    {
        UCRLOGF(("rcBackend %Rrc\n", rcBackend));
        pThis->enmStatusChange = SLOT_STATUS_CHANGE_RECEIVED; /* No change. */
    }
}

static bool usbCardReaderSendDisconnect(PUSBCARDREADER pThis, PCARDREADERSLOT pSlot, DISCONNECTREASON disconnectReason)
{
    bool fSent = false;

    /*
     * If the card is connected, do a disconnect to release the resource on the client.
     * Do not complete a pending command while disconnecting. This is still a BUSY state of the reader.
     */
    if (   pThis->enmICCConnState != ICCNOCONNECTION
        && pThis->enmICCConnState != ICCDISCONNECTED)
    {
        int rcBackend = pThis->Lun0.pCardReaderDown->pfnDisconnect(pThis->Lun0.pCardReaderDown,
                                                                                 pSlot,
                                                                                 VBOX_SCARD_UNPOWER_CARD);

        UCRLOG(("disconnect the card: %Rrc\n", rcBackend));

        if (RT_SUCCESS(rcBackend))
        {
            pThis->enmICCConnState = ICCDISCONNECTING;
            pThis->enmDisconnectReason = disconnectReason;
            fSent = true;
        }
    }

    return fSent;
}

/*
 * Slot status monitor.
 */
static void uscrStatusMonitorProcess(PUSBCARDREADER pThis)
{
    UCRLOGF(("pThis: %p\n", pThis));

    PCARDREADERSLOT pSlot = &pThis->paSlots[0];

    /* This is used to monitor the backend availability and card insertion/removal.
     * No backend means no card.
     * GetStatusChange is used for obtaining the card status initially
     * and when there is no card.
     *
     * Initially send the GetStatusChange.
     * On response
     *    If card is there, report and stop sending.
     *    If card is absent, resend GetStatusChange.
     * If any of backend functions report an error, send the GetStatusChange.
     *
     * When GetStatusChange is used, it means that there is no card.
     *
     * SLOT_STATUS_CHANGE_NOT_USED - must not use it, card is there and other functions can be used.
     * SLOT_STATUS_CHANGE_REQUESTED - have to monitor the card status with the GetStatus change.
     * SLOT_STATUS_CHANGE_RECEIVED - response received.
     * SLOT_STATUS_CHANGE_SENT - request sent.
     */

    if (pThis->enmBackendStatus == BACKEND_STATUS_ESTABLISHED)
    {
        if (pThis->enmStatusChange == SLOT_STATUS_CHANGE_RECEIVED)
        {
            UCRLOGF(("SLOT_STATUS_CHANGE_RECEIVED\n"));

            uint8_t bICCStatus;

            if ((pThis->u32EventStateBackend & VBOX_SCARD_STATE_PRESENT) != 0)
            {
                /* _MUTE, _UNPOWERED does not always work. Can't rely on this.
                 * So do not change the card status if it is not a NO_CARD.
                 */
                bICCStatus = (pThis->bICCStatus == VUSBCARDREADER_ICC_STATUS_NO_CARD)?
                                VUSBCARDREADER_ICC_STATUS_INACTIVE:
                                pThis->bICCStatus;
            }
            else
            {
                bICCStatus = VUSBCARDREADER_ICC_STATUS_NO_CARD;
            }

            /* Received status change (PRESENT/EMPTY) notification, which means that something was
             * changed on the client, which means that the connection must be reestablished.
             *
             * Have to release the card handle. Otherwise a possible second connect will fail,
             * because exclusive access is used. The disconnect request is sent if
             * the card is connected: usbCardReaderSendDisconnect checks this condition.
             */
            if ((pThis->u32EventStateBackend & VBOX_SCARD_STATE_EMPTY) != 0)
            {
                /* Some clients send status change notifications even if PRESENT/EMPTY has not chnaged.
                 * Try to reconnect only if there is no card.
                 */
                usbCardReaderSendDisconnect(pThis, pSlot, DISCONNECT_ONSTATUSCHANGE);
                pThis->enmICCConnState = ICCNOCONNECTION;
            }

            bool fForceChanged = (pThis->u32EventStateBackend & VBOX_SCARD_STATE_CHANGED) != 0;

            UCRLOGF(("SLOT_STATUS_CHANGE_RECEIVED: bICCStatus %d (current %d), fForceChanged %d, EventState 0x%08X\n",
                     bICCStatus, pThis->bICCStatus, fForceChanged, pThis->u32EventStateBackend));

            pThis->u32EventStateBackend &= ~VBOX_SCARD_STATE_CHANGED; /* report this once. */

            usbCardReaderUpdateICCStatus(pThis, bICCStatus, fForceChanged);
        }
        else if (pThis->enmStatusChange == SLOT_STATUS_CHANGE_SENT)
        {
            UCRLOGF(("SLOT_STATUS_CHANGE_SENT\n"));
            /* Do nothing. Continue waiting for the response. */
        }
        else if (pThis->enmStatusChange == SLOT_STATUS_CHANGE_NOT_USED)
        {
            UCRLOGF(("SLOT_STATUS_CHANGE_NOT_USED\n"));
            /* Do nothing, because nothing is to be done. */
        }
        else
        {
            Assert(pThis->enmStatusChange == SLOT_STATUS_CHANGE_REQUESTED);
        }

        /* Decide if there is a need to continue monitoring. */
        if (   pThis->enmStatusChange == SLOT_STATUS_CHANGE_RECEIVED
            || pThis->enmStatusChange == SLOT_STATUS_CHANGE_NOT_USED)
        {
            pThis->enmStatusChange = SLOT_STATUS_CHANGE_REQUESTED;
        }

        /* The enmStatusChange can be updated by the above code. So recheck the condition. */
        if (pThis->enmStatusChange == SLOT_STATUS_CHANGE_REQUESTED)
        {
            UCRLOGF(("SLOT_STATUS_CHANGE_REQUESTED\n"));
            usbCardReaderSendGetStatusChange(pThis, pSlot);
        }
    }
    else
    {
        UCRLOGF(("no backend context\n"));

        usbCardReaderUpdateICCStatus(pThis, VUSBCARDREADER_ICC_STATUS_NO_CARD, false);
    }

    return;
}

static DECLCALLBACK(int) uscrStatusMonitor(PPDMUSBINS pUsbIns, PPDMTHREAD pThread)
{
    RT_NOREF1(pUsbIns);
    PUSBCARDREADER pThis = (PUSBCARDREADER)pThread->pvUser;

    UCRLOG(("pThis:%p, enmState %d\n", pThis, pThread->enmState));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        int rc = uscrLock(pThis);
        if (RT_SUCCESS(rc))
        {
            uscrStatusMonitorProcess(pThis);

            urbQueueComplete(pThis, &pThis->urbQueues.IntrIn, uscrEventRead);
            uscrUnlock(pThis);
        }

        /* Protection from backend returning status immediately. Try to wait at least a second. */
        uint64_t u64Start = RTTimeMilliTS();

        RTSemEventWait(pThis->semStatusMonitor, RT_INDEFINITE_WAIT);

        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
        {
            break;
        }

        uint64_t u64Waited = RTTimeMilliTS() - u64Start;

        if (u64Waited < 1000)
        {
            UCRLOGF(("u64Waited %llu\n", u64Waited));

            rc = RTSemEventWait(pThis->semStatusMonitor, 1000);

            if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            {
                break;
            }

            if (RT_FAILURE(rc) && rc != VERR_TIMEOUT)
            {
                UCRLOG(("wait %Rrc\n", rc));
                RTThreadSleep(1000);
            }
        }
    }

    UCRLOG(("pThis:%p, enmState %d return\n", pThis, pThread->enmState));
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) uscrStatusMonitorWakeUp(PPDMUSBINS pDevIns, PPDMTHREAD pThread)
{
    RT_NOREF1(pDevIns);
    PUSBCARDREADER pThis = (PUSBCARDREADER)pThread->pvUser;
    UCRLOG(("pThis:%p\n", pThis));
    return RTSemEventSignal(pThis->semStatusMonitor);
}


/*
 * The smart card reader PDM interface.
 */

static DECLCALLBACK(int) usbSCardReaderEstablishContext(PPDMICARDREADERUP pInterface,
                                                        int32_t lSCardRc)
{
    int rc = VINF_SUCCESS;

    UCRLOG(("rc = %RX32\n", lSCardRc));

    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, Lun0.ICardReaderUp);

    rc = uscrLock(pThis);
    if (RT_FAILURE(rc))
    {
        return rc;
    }

    Assert(pThis->enmBackendStatus == BACKEND_STATUS_REQUESTING);

    if (lSCardRc == VBOX_SCARD_S_SUCCESS)
    {
        pThis->enmBackendStatus = BACKEND_STATUS_ESTABLISHED;

        RTSemEventSignal(pThis->semStatusMonitor);
    }
    else
    {
        pThis->enmBackendStatus = BACKEND_STATUS_NO_CONTEXT;
    }

    uscrUnlock(pThis);

    return rc;
}

static DECLCALLBACK(int) usbSCardReaderStatus(PPDMICARDREADERUP pInterface,
                                              void *pvUser,
                                              int32_t lSCardRc,
                                              char *pszReaderName,
                                              uint32_t cchReaderName,
                                              uint32_t u32CardState,
                                              uint32_t u32Protocol,
                                              uint8_t *pu8Atr,
                                              uint32_t cbAtr)
{
    UCRLOGF(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc], pszCardReaderName:%p, cchReaderName:%d, u32CardState:%R[scardSts], u32Protocol:%RX32, pu8Atr:%p, cbAtr:%d\n",
                 pInterface, pvUser, lSCardRc, pszReaderName, cchReaderName, u32CardState, u32Protocol, pu8Atr, cbAtr));
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(int) usbSCardReaderConnect(PPDMICARDREADERUP pInterface,
                                               void *pvUser,
                                               int32_t lSCardRc,
                                               uint32_t u32ActiveProtocol)
{
    int rc = VINF_SUCCESS;

    UCRLOG(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc], u32ActiveProtocol:%d\n",
            pInterface, pvUser, lSCardRc, u32ActiveProtocol));

    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, Lun0.ICardReaderUp);
    PCARDREADERSLOT pSlot = (PCARDREADERSLOT)pvUser;

    rc = uscrLock(pThis);
    if (RT_FAILURE(rc))
    {
        return rc;
    }

    Assert(pThis->enmICCConnState == ICCCONNECTING);

    if (   lSCardRc == VBOX_SCARD_S_SUCCESS
        && (   pThis->enmICCConnState == ICCCONNECTING
            || pThis->enmICCConnState == ICCCONNECTED)
       )
    {
        /** @todo issue a Status request to fetch ATR if no ATR yet received. */
        UCRLOG(("bICCStatus %d, pSlot->Atr.cbRawATR %d\n", pThis->bICCStatus, pSlot->Atr.cbRawATR));

        /* Make sure that only the client can set the protocol selector.
         * Because the device must work with the same protocol as reported by the client.
         */
        pSlot->u8ProtocolSelector = u32ActiveProtocol == VBOX_SCARD_PROTOCOL_T1? 1: 0;

        /* At the beginning this must be set to the initial value. */
        pSlot->u8IBlockNS = 0;
        usbCardReaderChainReset(pSlot);
        pSlot->ParmsT0 = g_ParmsT0;
        pSlot->ParmsT1 = g_ParmsT1;

        usbCardReaderUpdateICCStatus(pThis, VUSBCARDREADER_ICC_STATUS_PRESENT, false);
        pThis->enmICCConnState = ICCCONNECTED;

        uscrResponseOK(pThis, pSlot, pSlot->Atr.pu8RawATR, pSlot->Atr.cbRawATR, 0);
    }
    else
    {
        usbCardReaderUpdateICCStatus(pThis, VUSBCARDREADER_ICC_STATUS_NO_CARD, false);
        pThis->enmICCConnState = ICCNOCONNECTION;

        uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON); /* Not supported. */
    }

    /* Command completed. */
    pThis->fu8Cmd &= ~VUSBCARDREADER_F_CMD_BUSY;

    /* Process possibly pending bulk-in URBs. */
    urbQueueComplete(pThis, &pThis->urbQueues.BulkIn, uscrResponseRead);

    uscrUnlock(pThis);

    UCRLOGFLEAVERC(rc);
    return rc;
}

static DECLCALLBACK(int) usbSCardReaderDisconnect(PPDMICARDREADERUP pInterface,
                                                  void *pvUser,
                                                  int32_t lSCardRc)
{
    int rc = VINF_SUCCESS;

    UCRLOG(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc]\n",
            pInterface, pvUser,lSCardRc));

    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, Lun0.ICardReaderUp);
    PCARDREADERSLOT pSlot = (PCARDREADERSLOT)pvUser;
    /** @todo enmDisconnectReason must be taken from pvUser */

    rc = uscrLock(pThis);
    if (RT_FAILURE(rc))
    {
        return rc;
    }

    if (pThis->enmDisconnectReason == DISCONNECT_ONERROR)
    {
        /* This is a result of a backend error. */
        UCRLOG(("DISCONNECT_ONERROR\n"));

        /*
         * Go to the initial state: no connection at all. This will restart
         * the INTR endpoint polling for the card status changes.
         */
        usbCardReaderUpdateICCStatus(pThis, VUSBCARDREADER_ICC_STATUS_NO_CARD, false);
        pThis->enmICCConnState = ICCNOCONNECTION;

        /* Set a proper error. */
        uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_NON); /* Not supported. */

        /* Command completed. */
        pThis->fu8Cmd &= ~VUSBCARDREADER_F_CMD_BUSY;
    }
    else if (pThis->enmDisconnectReason == DISCONNECT_ONSTATUSCHANGE)
    {
        /* This is a result of a removed card. */
        UCRLOG(("DISCONNECT_ONSTATUSCHANGE\n"));

        /* Do nothing. BUSY is also unaffected. */
    }
    else
    {
        Assert(pThis->enmDisconnectReason == DISCONNECT_POWEROFF);
        UCRLOG(("DISCONNECT_POWEROFF\n"));

        /* Disconnect is a result of ICCPowerOff request. */
        if (lSCardRc == VBOX_SCARD_S_SUCCESS)
        {
            pThis->enmICCConnState = ICCDISCONNECTED;

            /* If the status is already NO_CARD then there is not need to change it. */
            if (pThis->bICCStatus != VUSBCARDREADER_ICC_STATUS_NO_CARD)
            {
                usbCardReaderUpdateICCStatus(pThis, VUSBCARDREADER_ICC_STATUS_INACTIVE, false);
            }
        }
        else
        {
            /* Go to the initial state: no connection at all. This will restart
             * the INTR endpoint polling for the card status changes.
             */
            usbCardReaderUpdateICCStatus(pThis, VUSBCARDREADER_ICC_STATUS_NO_CARD, false);
            pThis->enmICCConnState = ICCNOCONNECTION;
        }

        uscrResponseOK(pThis, pSlot, NULL, 0, 0x03); /* Clock stopped in an unknown state */

        /* Command completed. */
        pThis->fu8Cmd &= ~VUSBCARDREADER_F_CMD_BUSY;
    }

    pThis->enmDisconnectReason = DISCONNECT_VOID;

    /* Process possibly pending bulk-in URBs. */
    urbQueueComplete(pThis, &pThis->urbQueues.BulkIn, uscrResponseRead);

    uscrUnlock(pThis);

    UCRLOGFLEAVERC(rc);
    return rc;
}

static DECLCALLBACK(int) usbSCardReaderSetStatusChange(PPDMICARDREADERUP pInterface,
                                                       void *pvUser,
                                                       int32_t lSCardRc,
                                                       PDMICARDREADER_READERSTATE *paReaderStats,
                                                       uint32_t cReaderStats)
{
    int rc = VINF_SUCCESS;

    UCRLOGF(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc], rgReaderStats:%p, cReaderStats:%d\n",
            pInterface, pvUser, lSCardRc, paReaderStats, cReaderStats));

    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, Lun0.ICardReaderUp);
    PCARDREADERSLOT pSlot = (PCARDREADERSLOT)pvUser;

    rc = uscrLock(pThis);
    if (RT_FAILURE(rc))
    {
        return rc;
    }

    Assert(pThis->enmStatusChange == SLOT_STATUS_CHANGE_SENT);

    if (lSCardRc == VBOX_SCARD_S_SUCCESS)
    {
        Assert(pSlot->u8Id == 0);

        UCRLOG(("u32EventState 0x%RX32, cbATR %d\n",
                paReaderStats[pSlot->u8Id].u32EventState,
                paReaderStats[pSlot->u8Id].cbAtr));

        pThis->u32EventStateBackend = paReaderStats[pSlot->u8Id].u32EventState;

        RTMemFree(pSlot->Atr.pu8RawATR);
        pSlot->Atr.pu8RawATR = NULL;
        pSlot->Atr.cbRawATR = 0;

        if (paReaderStats[pSlot->u8Id].cbAtr)
        {
            /** @todo au8RawATR */
            pSlot->Atr.cbRawATR = paReaderStats[pSlot->u8Id].cbAtr;
            pSlot->Atr.pu8RawATR = (uint8_t *)RTMemAlloc(pSlot->Atr.cbRawATR);
            memcpy(pSlot->Atr.pu8RawATR, paReaderStats[pSlot->u8Id].au8Atr, pSlot->Atr.cbRawATR);
            usbCardReaderParseATR(pThis, pSlot);
        }
    }
    else if (lSCardRc != VBOX_SCARD_E_TIMEOUT) /* No change for a timeout. */
    {
        /* State and ATR are unknown. */
        pThis->u32EventStateBackend = 0;

        RTMemFree(pSlot->Atr.pu8RawATR);
        pSlot->Atr.pu8RawATR = NULL;
        pSlot->Atr.cbRawATR = 0;
    }

    pThis->enmStatusChange = SLOT_STATUS_CHANGE_RECEIVED;

    RTSemEventSignal(pThis->semStatusMonitor);

    uscrUnlock(pThis);

    UCRLOGFLEAVERC(rc);
    return rc;
}

static DECLCALLBACK(int) usbSCardReaderBeginTransaction(PPDMICARDREADERUP pInterface,
                                                        void *pvUser,
                                                        int32_t lSCardRc)
{
    UCRLOGF(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc]\n",
             pInterface, pvUser, lSCardRc));
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(int) usbSCardReaderEndTransaction(PPDMICARDREADERUP pInterface,
                                                      void *pvUser,
                                                      int32_t lSCardRc)
{
    UCRLOGF(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc]\n",
                 pInterface, pvUser, lSCardRc));
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(int) usbSCardReaderTransmit(PPDMICARDREADERUP pInterface,
                                                void *pvUser,
                                                int32_t lSCardRc,
                                                const PDMICARDREADER_IO_REQUEST *pioRecvPci,
                                                uint8_t *pu8RecvBuffer,
                                                uint32_t cbRecvBuffer)
{
    int rc = VINF_SUCCESS;

    UCRLOG(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc], pioRecvPci:%R[scardioreq], pu8RecvBuffer:%p, cbRecvBuffer:%d\n",
            pInterface, pvUser, lSCardRc, pioRecvPci, pu8RecvBuffer, cbRecvBuffer));

    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, Lun0.ICardReaderUp);
    PCARDREADERSLOT pSlot = (PCARDREADERSLOT)pvUser;

    rc = uscrLock(pThis);
    if (RT_FAILURE(rc))
    {
        return rc;
    }

    Assert((pThis->fu8Cmd & VUSBCARDREADER_F_CMD_BUSY) != 0);

    if (lSCardRc == VBOX_SCARD_S_SUCCESS)
    {
        void *pvData = NULL;
        uint32_t cbData = 0;

        UCRLOG(("\n%.*Rhxd\n", cbRecvBuffer, pu8RecvBuffer));

        if (pSlot->u8ProtocolSelector == 1)
        {
            uint8_t *pu8BlockData = NULL;
            uint32_t cbBlockData = 0;
            uint8_t u8More = 0;

            if (cbRecvBuffer > USED_IFSC)
            {
                /* Block is large and must be splitted. */
                rc = usbCardReaderChainSet(pSlot, pu8RecvBuffer, cbRecvBuffer);

                if (RT_SUCCESS(rc))
                {
                    /* Determine which part of the data can be sent. */
                    pu8BlockData = pSlot->pu8IBlockChain;
                    cbBlockData = USED_IFSC;

                    /* Update the read offset. */
                    pSlot->offIBlockChain = cbBlockData;

                    u8More = ISO7816_T1_BLK_I_MORE;
                }
            }
            else
            {
                /* Complete block can be transferred. */
                pu8BlockData = pu8RecvBuffer;
                cbBlockData = cbRecvBuffer;
            }

            if (RT_SUCCESS(rc))
            {
                rc = usbCardReaderT1CreateBlock(pSlot,
                                                (PPT1BLKHEADER)&pvData,
                                                &cbData,
                                                0,
                                                ISO7816_T1_BLK_I | pSlot->u8IBlockNS | u8More,
                                                pu8BlockData,
                                                cbBlockData);
                pSlot->u8IBlockNS ^= ISO7816_T1_BLK_I_NS;
            }
        }
        else
        {
            pvData = pu8RecvBuffer;
            cbData = cbRecvBuffer;
            rc = VINF_SUCCESS;
        }

        if (RT_SUCCESS(rc))
        {
            uscrResponseOK(pThis, pSlot, pvData, cbData, 0);
        }
        else
        {
            uscrResponseSlotError(pThis, pSlot, VUSBCARDREADER_MSG_STATUS_ERR_HW_ERROR);
            rc = VINF_SUCCESS;
        }

        if (pvData != pu8RecvBuffer)
        {
            RTMemFree(pvData);
        }

        /* Command completed. */
        pThis->fu8Cmd &= ~VUSBCARDREADER_F_CMD_BUSY;
    }
    else
    {
        /* If there is an error, then disconnect the card and do not create a response yet. */
        usbCardReaderSendDisconnect(pThis, pSlot, DISCONNECT_ONERROR);
    }

    /* Process possibly pending bulk-in URBs. */
    urbQueueComplete(pThis, &pThis->urbQueues.BulkIn, uscrResponseRead);

    uscrUnlock(pThis);

    UCRLOGF(("LEAVE: %Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) usbSCardReaderGetAttrib(PPDMICARDREADERUP pInterface,
                                                 void *pvUser,
                                                 int32_t lSCardRc,
                                                 uint32_t u32AttribId,
                                                 void *pvAttrib,
                                                 uint32_t cbAttrib)
{
    UCRLOGF(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc], u32AttribId:%R[scardAttr], pvAttrib:%p, cbAttrib:%d\n",
                 pInterface, pvUser, lSCardRc, u32AttribId, pvAttrib, cbAttrib));
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(int) usbSCardReaderSetAttrib(PPDMICARDREADERUP pInterface,
                                                 void *pvUser,
                                                 int32_t lSCardRc,
                                                 uint32_t u32AttribId)
{
    UCRLOGF(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc] u32AttribId:%R[scardAttr]\n",
                 pInterface, pvUser, lSCardRc, u32AttribId));
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(int) usbSCardReaderControl(PPDMICARDREADERUP pInterface,
                                               void *pvUser,
                                               int32_t lSCardRc,
                                               uint32_t u32ControlCode,
                                               void *pvOutBuffer,
                                               uint32_t cbOutBuffer)
{
    UCRLOGF(("ENTER: pInterface:%p, pvUser:%p, lSCardRc:%R[scardRc] u32ControlCode:%RX32, pvOutBuffer:%p, cbOutBuffer:%d\n",
                 pInterface, pvUser, lSCardRc, u32ControlCode, pvOutBuffer, cbOutBuffer));
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}


static DECLCALLBACK(void *) usbSCardReaderQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    UCRLOGF(("ENTER: pInterface:%p, pszIID:%s\n", pInterface, pszIID));
    PUSBCARDREADER  pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, Lun0.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMICARDREADERUP, &pThis->Lun0.ICardReaderUp);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->Lun0.IBase);
    return NULL;
}

static int uscrInitializeSlots(PUSBCARDREADER pThis, PCFGMNODE pCfg)
{
    RT_NOREF1(pCfg);
    int rc = VINF_SUCCESS;

    pThis->cSlots = 1; /** @todo value from CFGM */
    pThis->paSlots = (PCARDREADERSLOT)RTMemAllocZ(sizeof(CARDREADERSLOT) * pThis->cSlots);
    AssertPtrReturn(pThis->paSlots, VERR_NO_MEMORY);

    uint8_t idxSlot;
    for(idxSlot = 0; idxSlot < pThis->cSlots; ++idxSlot)
    {
        PCARDREADERSLOT pSlot = &pThis->paSlots[idxSlot];

        pSlot->u8Id = idxSlot;

        /** @todo remove or use the actual reader name from the driver. */
        pSlot->pszCardReaderName = RTStrDup("SCM SCR 335 [CCID Interface] (21120844306107) 00 00");
        AssertPtrReturn(pThis->paSlots, VERR_NO_MEMORY);

        pSlot->enmChainMode = IBLOCKCHAIN_NOT_USED;
    }

    return rc;
}


/*
 * Emulated USB device callbacks.
 */

/** @todo wrong order, missing doxygen bits.   */

static DECLCALLBACK(int) usbSCardReaderConstruct(PPDMUSBINS pUsbIns, int iInstance, PCFGMNODE pCfg,
                                                 PCFGMNODE pCfgGlobal)
{
    PDMUSB_CHECK_VERSIONS_RETURN(pUsbIns);
    UCRLOGF(("ENTER: (pUsbIns: %p, iInstance: %d, pCfg: %p, pCfgGlobal: %p)\n",
             pUsbIns, iInstance, pCfg, pCfgGlobal));

    int rc = VINF_SUCCESS;
    unsigned i;

    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);

    pThis->pUsbIns = pUsbIns;

    pThis->Lun0.IBase.pfnQueryInterface = usbSCardReaderQueryInterface;

    pThis->Lun0.ICardReaderUp.pfnEstablishContext = usbSCardReaderEstablishContext;
    pThis->Lun0.ICardReaderUp.pfnStatus           = usbSCardReaderStatus;
    pThis->Lun0.ICardReaderUp.pfnConnect          = usbSCardReaderConnect;
    pThis->Lun0.ICardReaderUp.pfnDisconnect       = usbSCardReaderDisconnect;
    pThis->Lun0.ICardReaderUp.pfnSetStatusChange  = usbSCardReaderSetStatusChange;
    pThis->Lun0.ICardReaderUp.pfnBeginTransaction = usbSCardReaderBeginTransaction;
    pThis->Lun0.ICardReaderUp.pfnEndTransaction   = usbSCardReaderEndTransaction;
    pThis->Lun0.ICardReaderUp.pfnTransmit         = usbSCardReaderTransmit;
    pThis->Lun0.ICardReaderUp.pfnGetAttrib        = usbSCardReaderGetAttrib;
    pThis->Lun0.ICardReaderUp.pfnSetAttrib        = usbSCardReaderSetAttrib;
    pThis->Lun0.ICardReaderUp.pfnControl          = usbSCardReaderControl;

    usbCardReaderInitLogging();

    /* Emulate SCR335 device, because it is well supported by guests. */
    pThis->iDeviceType = USCR_DEVICE_TYPE_SCR335;
    pThis->enmBackendStatus = BACKEND_STATUS_NO_CONTEXT;
    pThis->enmStatusChange = SLOT_STATUS_CHANGE_RECEIVED; /* Start to monitor the slot status. */
    pThis->enmDisconnectReason = DISCONNECT_VOID;
    pThis->u32EventStateBackend = 0;
    pThis->enmICCConnState = ICCNOCONNECTION;
    pThis->fu8Cmd = 0;
    pThis->bICCStatus = 2; /* No card */
    pThis->fICCStateChanged = true; /* State changed, because it must be reported initially. */

    urbQueueInit(&pThis->urbQueues.BulkIn);
    urbQueueInit(&pThis->urbQueues.IntrIn);
    urbQueueInit(&pThis->urbQueues.Done);

    pThis->pRspCurrent = NULL;
    RTListInit(&pThis->listRspReady);
    RTListInit(&pThis->listRspFree);
    for (i = 0; i < RT_ELEMENTS(pThis->aRspAlloc); i++)
        RTListAppend(&pThis->listRspFree, &pThis->aRspAlloc[i].nodeRsp);

    RT_ZERO(pThis->command);

    rc = RTCritSectInit(&pThis->csThis);
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Failed to create %Rrc"), rc);

    rc = uscrInitializeSlots(pThis, pCfg);
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Slot init %Rrc"), rc);

    rc = RTSemEventCreate(&pThis->semStatusMonitor);
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Event sem %Rrc"), rc);

    pThis->hEvtDoneQueue = NIL_RTSEMEVENT;
    rc = RTSemEventCreate(&pThis->hEvtDoneQueue);
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Failed to create event semaphore"));

    rc = PDMUsbHlpThreadCreate(pUsbIns, &pThis->pStatusMonitorThread, pThis,
                               uscrStatusMonitor, uscrStatusMonitorWakeUp,
                               0, RTTHREADTYPE_IO, "USCR-MON");
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Monitor thread %Rrc"), rc);

    /* Get the driver interface. */
#ifdef VBOX_WITH_USB_CARDREADER_TEST
    rc = PDMUsbHlpDriverAttach(pUsbIns, 0 /* iLun */, &pThis->Lun0.IBase, &pThis->Lun0.pDrvBase, "DrvDirectCardReader");
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Can't attach usb card reader driver"));
#else
    rc = PDMUsbHlpDriverAttach(pUsbIns, 0 /* iLun */, &pThis->Lun0.IBase, &pThis->Lun0.pDrvBase, "UsbCardReaderInterface");
    if (RT_FAILURE(rc))
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Can't attach usb card reader driver"));
#endif
    pThis->Lun0.pCardReaderDown = (PPDMICARDREADERDOWN)pThis->Lun0.pDrvBase->pfnQueryInterface(pThis->Lun0.pDrvBase, PDMICARDREADERDOWN_IID);
    if (!pThis->Lun0.pCardReaderDown)
        return PDMUsbHlpVMSetError(pUsbIns, rc, RT_SRC_POS, N_("USBSCARDREADER: Can't attach usb card reader driver"));

    UCRLOGFLEAVERC(rc);
    return rc;
}

static DECLCALLBACK(void) usbSCardReaderDestruct(PPDMUSBINS pUsbIns)
{
    PDMUSB_CHECK_VERSIONS_RETURN_VOID(pUsbIns);
    UCRLOGF(("ENTER: (pUsbIns:%p)\n", pUsbIns));

    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);
    PCPDMUSBHLP pHlp = pUsbIns->pHlpR3;

    RTMemFree(pThis->paSlots);
    pThis->paSlots = NULL;

    if (pThis->pStatusMonitorThread)
    {
        int rcThread;
        pHlp->pfnThreadDestroy(pThis->pStatusMonitorThread, &rcThread);
        pThis->pStatusMonitorThread = NULL;
    }

    RTSemEventDestroy(pThis->semStatusMonitor);

    if (pThis->hEvtDoneQueue != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hEvtDoneQueue);
        pThis->hEvtDoneQueue = NIL_RTSEMEVENT;
    }

    if (RTCritSectIsInitialized(&pThis->csThis))
    {
        RTCritSectEnter(&pThis->csThis);
        RTCritSectLeave(&pThis->csThis);
        RTCritSectDelete(&pThis->csThis);
    }

    uscrResponseCleanup(pThis);

    UCRLOGF(("LEAVE\n"));
}

static DECLCALLBACK(int) usbSCardReaderUsbReset(PPDMUSBINS pUsbIns, bool fResetOnLinux)
{
    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);
    NOREF(fResetOnLinux);

    UCRLOG(("iInstance:%d\n",
            pUsbIns->iInstance));

    int rc = uscrLock(pThis);

    if (RT_SUCCESS(rc))
    {
        /* Report the slot status in the next intr-in URB. */
        pThis->fICCStateChanged = true;

        uscrUnlock(pThis);
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

static DECLCALLBACK(PCPDMUSBDESCCACHE) usbSCardReaderGetDescriptor(PPDMUSBINS pUsbIns)
{
    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);

    UCRLOGF(("pThis->iDeviceType %d\n", pThis->iDeviceType));

    if (pThis->iDeviceType == USCR_DEVICE_TYPE_SCR335)
    {
        return &g_USCRDescriptorCacheSCR335;
    }

    /* Default is a VirtualBox device. */
    return &g_USCRDescriptorCacheVBOX;
}

static DECLCALLBACK(int) usbSCardReaderUrbQueue(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    UCRLOGF(("ENTER: pUsbIns:%p, pUrb:%p, EP:%#x\n",
                 pUsbIns, pUrb, pUrb? pUrb->EndPt: 0));

    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);

    int rc = uscrLock(pThis);

    if (RT_SUCCESS(rc))
    {
        /* Try to establish a backend context if it does not exist yet. */
        if (RT_UNLIKELY(pThis->enmBackendStatus == BACKEND_STATUS_NO_CONTEXT))
        {
            int rcBackend = pThis->Lun0.pCardReaderDown->pfnEstablishContext(pThis->Lun0.pCardReaderDown);

            if (RT_SUCCESS(rcBackend))
            {
                pThis->enmBackendStatus = BACKEND_STATUS_REQUESTING;
            }
        }

        switch (pUrb->EndPt)
        {
            case 0:
                rc = usbCardReaderDefaultPipe(pThis, &pThis->aEps[0], pUrb);
                break;
            case 1:
                rc = usbCardReaderBulkOutPipe(pThis, &pThis->aEps[1], pUrb);
                break;
            case 2:
                rc = usbCardReaderBulkInPipe(pThis, &pThis->aEps[2], pUrb);
                break;
            case 3:
                rc = usbCardReaderIntPipe(pThis, &pThis->aEps[3], pUrb);
                break;
            default:
                AssertMsgFailed(("EndPt:%#x\n", pUrb->EndPt));
                rc = VERR_VUSB_FAILED_TO_QUEUE_URB;
        }

        uscrUnlock(pThis);
    }
    else
    {
        rc = VERR_VUSB_FAILED_TO_QUEUE_URB;
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

static DECLCALLBACK(int) usbSCardReaderUrbCancel(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);

    UCRLOG(("iInstance:%d pUrb:%p:%s EndPt:%x\n",
            pUsbIns->iInstance, pUrb, pUrb->pszDesc, pUrb->EndPt));

    int rc = uscrLock(pThis);

    if (RT_SUCCESS(rc))
    {
        /* Look for the URB in all queues. */
        bool fRemoved = urbQueueRemove(&pThis->urbQueues.BulkIn, pUrb);
        if (!fRemoved)
        {
            fRemoved = urbQueueRemove(&pThis->urbQueues.IntrIn, pUrb);
        }

        if (fRemoved)
        {
            UCRLOG(("Cancelled URB pUrb:%p:%s EndPt:%x\n",
                     pUrb, pUrb->pszDesc, pUrb->EndPt));
            usbCardReaderLinkDone(pThis, pUrb);
        }

        uscrUnlock(pThis);
    }

    UCRLOGFLEAVERC(rc);
    return rc;
}

static DECLCALLBACK(PVUSBURB) usbSCardReaderUrbReap(PPDMUSBINS pUsbIns, RTMSINTERVAL cMillies)
{
    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);

    PVUSBURB pUrb = NULL;

    int rc = uscrLock(pThis);

    if (RT_SUCCESS(rc))
    {
        bool fLock = true;

        pUrb = urbQueueRemoveHead(&pThis->urbQueues.Done);

        if (!pUrb && cMillies)
        {
            pThis->fHaveDoneQueueWaiter = true;
            uscrUnlock(pThis);

            RTSemEventWait(pThis->hEvtDoneQueue, cMillies);

            rc = uscrLock(pThis);

            if (RT_SUCCESS(rc))
            {
                pThis->fHaveDoneQueueWaiter = false;
                fLock = true;
                pUrb = urbQueueRemoveHead(&pThis->urbQueues.Done);
            }
        }

        if (fLock)
        {
            uscrUnlock(pThis);
        }
    }

    if (pUrb)
    {
        UCRLOGF(("iInstance=%d cMillies=%d pUrb=%p\n",
                 pUsbIns->iInstance, cMillies, pUrb));
    }

    return pUrb;
}

static DECLCALLBACK(int) usbSCardReaderWakeup(PPDMUSBINS pUsbIns)
{
    PUSBCARDREADER pThis = PDMINS_2_DATA(pUsbIns, PUSBCARDREADER);

    return RTSemEventSignal(pThis->hEvtDoneQueue);
}

const PDMUSBREG g_UsbSCardReader =
{
    /* u32Version */
    PDM_USBREG_VERSION,
    /* szName[32] */
    "CardReader",
    /* pszDescription */
    "USB Smart Card Reader",
    /* fFlags */
    0,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(USBCARDREADER),
    /* pfnConstruct */
    usbSCardReaderConstruct,
    /* pfnDestruct */
    usbSCardReaderDestruct,
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
    usbSCardReaderUsbReset,
    /* pfnUsbGetDescriptorCache */
    usbSCardReaderGetDescriptor,
    /* pfnUsbSetConfiguration */
    NULL,
    /* pfnUsbSetInterface */
    NULL,
    /* pfnUsbClearHaltedEndpoint */
    NULL,
    /* pfnUrbNew */
    NULL,
    /* pfnUrbQueue */
    usbSCardReaderUrbQueue,
    /* pfnUrbCancel */
    usbSCardReaderUrbCancel,
    /* pfnUrbReap */
    usbSCardReaderUrbReap,
    /* pfnWakeup */
    usbSCardReaderWakeup,
    /* u32TheEnd */
    PDM_USBREG_VERSION
};
