/* $Id: USBProxyDevice-usbip.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device proxy - USB/IP backend.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_USBPROXY

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/vmm/pdm.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/socket.h>
#include <iprt/poll.h>
#include <iprt/tcp.h>
#include <iprt/pipe.h>
#include <iprt/list.h>
#include <iprt/semaphore.h>

#include "../USBProxyDevice.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/** The USB version number used for the protocol. */
#define USBIP_VERSION         UINT16_C(0x0111)
/** Request indicator in the command code. */
#define USBIP_INDICATOR_REQ   RT_BIT(15)

/** Command/Reply code for OP_REQ/RET_DEVLIST. */
#define USBIP_REQ_RET_DEVLIST UINT16_C(5)
/** Command/Reply code for OP_REQ/REP_IMPORT. */
#define USBIP_REQ_RET_IMPORT  UINT16_C(3)
/** USB submit command identifier. */
#define USBIP_CMD_SUBMIT      UINT32_C(1)
/** USB submit status identifier. */
#define USBIP_RET_SUBMIT      UINT32_C(3)
/** URB unlink (cancel) command identifier. */
#define USBIP_CMD_UNLINK      UINT32_C(2)
/** URB unlink (cancel) reply identifier. */
#define USBIP_RET_UNLINK      UINT32_C(4)

/** Short read is not okay for the specified URB. */
#define USBIP_XFER_FLAGS_SHORT_NOT_OK        RT_BIT_32(0)
/** Queue the isochronous URB as soon as possible. */
#define USBIP_XFER_FLAGS_ISO_ASAP            RT_BIT_32(1)
/** Don't use DMA mappings for this URB. */
#define USBIP_XFER_FLAGS_NO_TRANSFER_DMA_MAP RT_BIT_32(2)
/** Explain - only applies to UHCI. */
#define USBIP_XFER_FLAGS_FSBR                RT_BIT_32(4)

/** URB direction - input. */
#define USBIP_DIR_IN                         UINT32_C(1)
/** URB direction - output. */
#define USBIP_DIR_OUT                        UINT32_C(0)

/** @name USB/IP error codes.
 * @{ */
/** Success indicator. */
#define USBIP_STATUS_SUCCESS                 INT32_C(0)
/** Pipe stalled. */
#define USBIP_STATUS_PIPE_STALLED            INT32_C(-32)
/** URB was unlinked by a call to usb_unlink_urb(). */
#define USBIP_STATUS_URB_UNLINKED            INT32_C(-104)
/** Short read. */
#define USBIP_STATUS_SHORT_READ              INT32_C(-121)
/** @} */

/**
 * Exported device entry in the OP_RET_DEVLIST reply.
 */
#pragma pack(1)
typedef struct UsbIpExportedDevice
{
    /** Path of the device, zero terminated string. */
    char     szPath[256];
    /** Bus ID of the exported device, zero terminated string. */
    char     szBusId[32];
    /** Bus number. */
    uint32_t u32BusNum;
    /** Device number. */
    uint32_t u32DevNum;
    /** Speed indicator of the device. */
    uint32_t u32Speed;
    /** Vendor ID of the device. */
    uint16_t u16VendorId;
    /** Product ID of the device. */
    uint16_t u16ProductId;
    /** Device release number. */
    uint16_t u16BcdDevice;
    /** Device class. */
    uint8_t  bDeviceClass;
    /** Device Subclass. */
    uint8_t  bDeviceSubClass;
    /** Device protocol. */
    uint8_t  bDeviceProtocol;
    /** Configuration value. */
    uint8_t  bConfigurationValue;
    /** Current configuration value of the device. */
    uint8_t  bNumConfigurations;
    /** Number of interfaces for the device. */
    uint8_t  bNumInterfaces;
} UsbIpExportedDevice;
/** Pointer to a exported device entry. */
typedef UsbIpExportedDevice *PUsbIpExportedDevice;
#pragma pack()
AssertCompileSize(UsbIpExportedDevice, 312);

/**
 * Interface descriptor entry for an exported device.
 */
#pragma pack(1)
typedef struct UsbIpDeviceInterface
{
    /** Intefrace class. */
    uint8_t  bInterfaceClass;
    /** Interface sub class. */
    uint8_t  bInterfaceSubClass;
    /** Interface protocol identifier. */
    uint8_t  bInterfaceProtocol;
    /** Padding byte for alignment. */
    uint8_t  bPadding;
} UsbIpDeviceInterface;
/** Pointer to an interface descriptor entry. */
typedef UsbIpDeviceInterface *PUsbIpDeviceInterface;
#pragma pack()

/**
 * USB/IP Import request.
 */
#pragma pack(1)
typedef struct UsbIpReqImport
{
    /** Protocol version number. */
    uint16_t     u16Version;
    /** Command code. */
    uint16_t     u16Cmd;
    /** Status field, unused. */
    int32_t      i32Status;
    /** Bus Id of the device as zero terminated string. */
    char         aszBusId[32];
} UsbIpReqImport;
/** Pointer to a import request. */
typedef UsbIpReqImport *PUsbIpReqImport;
#pragma pack()

/**
 * USB/IP Import reply.
 *
 * This is only the header, for successful
 * imports the device details are sent to as
 * defined in UsbIpExportedDevice.
 */
#pragma pack(1)
typedef struct UsbIpRetImport
{
    /** Protocol version number. */
    uint16_t     u16Version;
    /** Command code. */
    uint16_t     u16Cmd;
    /** Status field, unused. */
    int32_t      i32Status;
} UsbIpRetImport;
/** Pointer to a import reply. */
typedef UsbIpRetImport *PUsbIpRetImport;
#pragma pack()

/**
 * Command/Reply header common to the submit and unlink commands
 * replies.
 */
#pragma pack(1)
typedef struct UsbIpReqRetHdr
{
    /** Request/Return code. */
    uint32_t     u32ReqRet;
    /** Sequence number to identify the URB. */
    uint32_t     u32SeqNum;
    /** Device id. */
    uint32_t     u32DevId;
    /** Direction of the endpoint (host->device, device->host). */
    uint32_t     u32Direction;
    /** Endpoint number. */
    uint32_t     u32Endpoint;
} UsbIpReqRetHdr;
/** Pointer to a request/reply header. */
typedef UsbIpReqRetHdr *PUsbIpReqRetHdr;
#pragma pack()

/**
 * USB/IP Submit request.
 */
#pragma pack(1)
typedef struct UsbIpReqSubmit
{
    /** The request header. */
    UsbIpReqRetHdr Hdr;
    /** Transfer flags for the URB. */
    uint32_t       u32XferFlags;
    /** Transfer buffer length. */
    uint32_t       u32TransferBufferLength;
    /** Frame to transmit an ISO frame. */
    uint32_t       u32StartFrame;
    /** Number of isochronous packets. */
    uint32_t       u32NumIsocPkts;
    /** Maximum time for the request on the server side host controller. */
    uint32_t       u32Interval;
    /** Setup data for a control URB. */
    VUSBSETUP      Setup;
} UsbIpReqSubmit;
/** Pointer to a submit request. */
typedef UsbIpReqSubmit *PUsbIpReqSubmit;
#pragma pack()
AssertCompileSize(UsbIpReqSubmit, 48);

/**
 * USB/IP Submit reply.
 */
#pragma pack(1)
typedef struct UsbIpRetSubmit
{
    /** The reply header. */
    UsbIpReqRetHdr Hdr;
    /** Status code. */
    int32_t        i32Status;
    /** Actual length of the reply buffer. */
    uint32_t       u32ActualLength;
    /** The actual selected frame for a isochronous transmit. */
    uint32_t       u32StartFrame;
    /** Number of isochronous packets. */
    uint32_t       u32NumIsocPkts;
    /** Number of failed isochronous packets. */
    uint32_t       u32ErrorCount;
    /** Setup data for a control URB. */
    VUSBSETUP      Setup;
} UsbIpRetSubmit;
/** Pointer to a submit reply. */
typedef UsbIpRetSubmit *PUsbIpRetSubmit;
#pragma pack()
AssertCompileSize(UsbIpRetSubmit, 48);

/**
 * Unlink URB request.
 */
#pragma pack(1)
typedef struct UsbIpReqUnlink
{
    /** The request header. */
    UsbIpReqRetHdr Hdr;
    /** The sequence number to unlink. */
    uint32_t       u32SeqNum;
    /** Padding - unused. */
    uint8_t        abPadding[24];
} UsbIpReqUnlink;
/** Pointer to a URB unlink request. */
typedef UsbIpReqUnlink *PUsbIpReqUnlink;
#pragma pack()
AssertCompileSize(UsbIpReqUnlink, 48);

/**
 * Unlink URB reply.
 */
#pragma pack(1)
typedef struct UsbIpRetUnlink
{
    /** The reply header. */
    UsbIpReqRetHdr Hdr;
    /** Status of the request. */
    int32_t        i32Status;
    /** Padding - unused. */
    uint8_t        abPadding[24];
} UsbIpRetUnlink;
/** Pointer to a URB unlink request. */
typedef UsbIpRetUnlink *PUsbIpRetUnlink;
#pragma pack()
AssertCompileSize(UsbIpRetUnlink, 48);

/**
 * Union of possible replies from the server during normal operation.
 */
#pragma pack(1)
typedef union UsbIpRet
{
    /** The header. */
    UsbIpReqRetHdr Hdr;
    /** Submit reply. */
    UsbIpRetSubmit RetSubmit;
    /** Unlink reply. */
    UsbIpRetUnlink RetUnlink;
    /** Byte view. */
    uint8_t        abReply[1];
} UsbIpRet;
/** Pointer to a reply union. */
typedef UsbIpRet *PUsbIpRet;
#pragma pack()

/**
 * Isochronous packet descriptor.
*/
#pragma pack(1)
typedef struct UsbIpIsocPktDesc
{
    /** Offset */
    uint32_t       u32Offset;
    /** Length of the packet including padding. */
    uint32_t       u32Length;
    /** Size of the transmitted data. */
    uint32_t       u32ActualLength;
    /** Completion status for this packet. */
    int32_t        i32Status;
} UsbIpIsocPktDesc;
/** Pointer to a isochronous packet descriptor. */
typedef UsbIpIsocPktDesc *PUsbIpIsocPktDesc;
#pragma pack()

/**
 * USB/IP backend specific data for one URB.
 * Required for tracking in flight and landed URBs.
 */
typedef struct USBPROXYURBUSBIP
{
    /** List node for the in flight or landed URB list. */
    RTLISTNODE         NodeList;
    /** Sequence number the assigned URB is identified by. */
    uint32_t           u32SeqNumUrb;
    /** Sequence number of the unlink command if the URB was cancelled. */
    uint32_t           u32SeqNumUrbUnlink;
    /** Flag whether the URB was cancelled. */
    bool               fCancelled;
    /** USB xfer type. */
    VUSBXFERTYPE       enmType;
    /** USB xfer direction. */
    VUSBDIRECTION      enmDir;
    /** Completion status. */
    VUSBSTATUS         enmStatus;
    /** Pointer to the VUSB URB. */
    PVUSBURB           pVUsbUrb;
} USBPROXYURBUSBIP;
/** Pointer to a USB/IP URB. */
typedef USBPROXYURBUSBIP *PUSBPROXYURBUSBIP;

/**
 * USB/IP data receive states.
 */
typedef enum USBPROXYUSBIPRECVSTATE
{
    /** Invalid receive state. */
    USBPROXYUSBIPRECVSTATE_INVALID = 0,
    /** Currently receiving the common header structure. */
    USBPROXYUSBIPRECVSTATE_HDR_COMMON,
    /** Currently receieving the rest of the header structure. */
    USBPROXYUSBIPRECVSTATE_HDR_RESIDUAL,
    /** Currently receiving data into the URB buffer. */
    USBPROXYUSBIPRECVSTATE_URB_BUFFER,
    /** Currently receiving the isochronous packet descriptors. */
    USBPROXYUSBIPRECVSTATE_ISOC_PKT_DESCS,
    /** Usual 32bit hack. */
    USBPROXYUSBIPRECVSTATE_32BIT_HACK = 0x7fffffff
} USBPROXYUSBIPRECVSTATE;
/** Pointer to an receive state. */
typedef USBPROXYUSBIPRECVSTATE *PUSBPROXYUSBIPRECVSTATE;

/**
 * Backend data for the USB/IP USB Proxy device backend.
 */
typedef struct USBPROXYDEVUSBIP
{
    /** IPRT socket handle. */
    RTSOCKET                  hSocket;
    /** Pollset with the wakeup pipe and socket. */
    RTPOLLSET                 hPollSet;
    /** Pipe endpoint - read (in the pollset). */
    RTPIPE                    hPipeR;
    /** Pipe endpoint - write. */
    RTPIPE                    hPipeW;
    /** Next sequence number to use for identifying submitted URBs. */
    volatile uint32_t         u32SeqNumNext;
    /** Fast mutex protecting the lists below against concurrent access. */
    RTSEMFASTMUTEX            hMtxLists;
    /** List of in flight URBs. */
    RTLISTANCHOR              ListUrbsInFlight;
    /** List of landed URBs. */
    RTLISTANCHOR              ListUrbsLanded;
    /** List of URBs to submit. */
    RTLISTANCHOR              ListUrbsToQueue;
    /** Port of the USB/IP host to connect to. */
    uint32_t                  uPort;
    /** USB/IP host address. */
    char                     *pszHost;
    /** USB Bus ID of the device to capture. */
    char                     *pszBusId;
    /** The device ID to use to identify the device. */
    uint32_t                  u32DevId;
    /** Temporary buffer for the next reply header */
    UsbIpRet                  BufRet;
    /** Temporary buffer to hold all isochronous packet descriptors. */
    UsbIpIsocPktDesc          aIsocPktDesc[8];
    /** Pointer to the current buffer to write received data to. */
    uint8_t                  *pbRecv;
    /** Number of bytes received so far. */
    size_t                    cbRecv;
    /** Number of bytes left to receive. until we advance the state machine and process the data */
    size_t                    cbLeft;
    /** The current receiving state. */
    USBPROXYUSBIPRECVSTATE    enmRecvState;
    /** The URB we currently receive a response for. */
    PUSBPROXYURBUSBIP         pUrbUsbIp;
} USBPROXYDEVUSBIP, *PUSBPROXYDEVUSBIP;

/** Pollset id of the socket. */
#define USBIP_POLL_ID_SOCKET 0
/** Pollset id of the pipe. */
#define USBIP_POLL_ID_PIPE   1

/** USB/IP address prefix for identifcation. */
#define USBIP_URI_PREFIX     "usbip://"
/** USB/IP address prefix length. */
#define USBIP_URI_PREFIX_LEN (sizeof(USBIP_URI_PREFIX) - 1)

/** Waking reason for the USB I/P reaper: New URBs to queue. */
#define USBIP_REAPER_WAKEUP_REASON_QUEUE 'Q'
/** Waking reason for the USB I/P reaper: External wakeup. */
#define USBIP_REAPER_WAKEUP_REASON_EXTERNAL 'E'

/**
 * Converts a request/reply header from network to host endianness.
 *
 * @param   pHdr    The header to convert.
 */
DECLINLINE(void) usbProxyUsbIpReqRetHdrN2H(PUsbIpReqRetHdr pHdr)
{
    pHdr->u32ReqRet    = RT_H2N_U32(pHdr->u32ReqRet);
    pHdr->u32SeqNum    = RT_H2N_U32(pHdr->u32SeqNum);
    pHdr->u32DevId     = RT_H2N_U32(pHdr->u32DevId);
    pHdr->u32Direction = RT_H2N_U32(pHdr->u32Direction);
    pHdr->u32Endpoint  = RT_H2N_U32(pHdr->u32Endpoint);
}

/**
 * Converts a request/reply header from host to network endianness.
 *
 * @param   pHdr              The header to convert.
 */
DECLINLINE(void) usbProxyUsbIpReqRetHdrH2N(PUsbIpReqRetHdr pHdr)
{
    pHdr->u32ReqRet    = RT_N2H_U32(pHdr->u32ReqRet);
    pHdr->u32SeqNum    = RT_N2H_U32(pHdr->u32SeqNum);
    pHdr->u32DevId     = RT_N2H_U32(pHdr->u32DevId);
    pHdr->u32Direction = RT_N2H_U32(pHdr->u32Direction);
    pHdr->u32Endpoint  = RT_N2H_U32(pHdr->u32Endpoint);
}

/**
 * Converts a submit request from host to network endianness.
 *
 * @param   pReqSubmit        The submit request to convert.
 */
DECLINLINE(void) usbProxyUsbIpReqSubmitH2N(PUsbIpReqSubmit pReqSubmit)
{
    usbProxyUsbIpReqRetHdrH2N(&pReqSubmit->Hdr);
    pReqSubmit->u32XferFlags            = RT_H2N_U32(pReqSubmit->u32XferFlags);
    pReqSubmit->u32TransferBufferLength = RT_H2N_U32(pReqSubmit->u32TransferBufferLength);
    pReqSubmit->u32StartFrame           = RT_H2N_U32(pReqSubmit->u32StartFrame);
    pReqSubmit->u32NumIsocPkts          = RT_H2N_U32(pReqSubmit->u32NumIsocPkts);
    pReqSubmit->u32Interval             = RT_H2N_U32(pReqSubmit->u32Interval);
}

/**
 * Converts a submit reply from network to host endianness.
 *
 * @param   pReqSubmit        The submit reply to convert.
 */
DECLINLINE(void) usbProxyUsbIpRetSubmitN2H(PUsbIpRetSubmit pRetSubmit)
{
    usbProxyUsbIpReqRetHdrN2H(&pRetSubmit->Hdr);
    pRetSubmit->i32Status       = RT_N2H_S32(pRetSubmit->i32Status);
    pRetSubmit->u32ActualLength = RT_N2H_U32(pRetSubmit->u32ActualLength);
    pRetSubmit->u32StartFrame   = RT_N2H_U32(pRetSubmit->u32StartFrame);
    pRetSubmit->u32NumIsocPkts  = RT_N2H_U32(pRetSubmit->u32NumIsocPkts);
    pRetSubmit->u32ErrorCount   = RT_N2H_U32(pRetSubmit->u32ErrorCount);
}

/**
 * Converts a isochronous packet descriptor from host to network endianness.
 *
 * @param   pIsocPktDesc      The packet descriptor to convert.
 */
DECLINLINE(void) usbProxyUsbIpIsocPktDescH2N(PUsbIpIsocPktDesc pIsocPktDesc)
{
    pIsocPktDesc->u32Offset       = RT_H2N_U32(pIsocPktDesc->u32Offset);
    pIsocPktDesc->u32Length       = RT_H2N_U32(pIsocPktDesc->u32Length);
    pIsocPktDesc->u32ActualLength = RT_H2N_U32(pIsocPktDesc->u32ActualLength);
    pIsocPktDesc->i32Status       = RT_H2N_U32(pIsocPktDesc->i32Status);
}

/**
 * Converts a isochronous packet descriptor from network to host endianness.
 *
 * @param   pIsocPktDesc      The packet descriptor to convert.
 */
DECLINLINE(void) usbProxyUsbIpIsocPktDescN2H(PUsbIpIsocPktDesc pIsocPktDesc)
{
    pIsocPktDesc->u32Offset       = RT_N2H_U32(pIsocPktDesc->u32Offset);
    pIsocPktDesc->u32Length       = RT_N2H_U32(pIsocPktDesc->u32Length);
    pIsocPktDesc->u32ActualLength = RT_N2H_U32(pIsocPktDesc->u32ActualLength);
    pIsocPktDesc->i32Status       = RT_N2H_U32(pIsocPktDesc->i32Status);
}

/**
 * Converts a unlink request from host to network endianness.
 *
 * @param   pReqUnlink        The unlink request to convert.
 */
DECLINLINE(void) usbProxyUsbIpReqUnlinkH2N(PUsbIpReqUnlink pReqUnlink)
{
    usbProxyUsbIpReqRetHdrH2N(&pReqUnlink->Hdr);
    pReqUnlink->u32SeqNum = RT_H2N_U32(pReqUnlink->u32SeqNum);
}

/**
 * Converts a unlink reply from network to host endianness.
 *
 * @param   pRetUnlink        The unlink reply to convert.
 */
DECLINLINE(void) usbProxyUsbIpRetUnlinkN2H(PUsbIpRetUnlink pRetUnlink)
{
    usbProxyUsbIpReqRetHdrN2H(&pRetUnlink->Hdr);
    pRetUnlink->i32Status = RT_N2H_S32(pRetUnlink->i32Status);
}

/**
 * Convert the given exported device structure from host to network byte order.
 *
 * @param   pDevice           The device structure to convert.
 */
DECLINLINE(void) usbProxyUsbIpExportedDeviceN2H(PUsbIpExportedDevice pDevice)
{
    pDevice->u32BusNum    = RT_N2H_U32(pDevice->u32BusNum);
    pDevice->u32DevNum    = RT_N2H_U32(pDevice->u32DevNum);
    pDevice->u32Speed     = RT_N2H_U16(pDevice->u32Speed);
    pDevice->u16VendorId  = RT_N2H_U16(pDevice->u16VendorId);
    pDevice->u16ProductId = RT_N2H_U16(pDevice->u16ProductId);
    pDevice->u16BcdDevice = RT_N2H_U16(pDevice->u16BcdDevice);
}

/**
 * Converts a USB/IP status code to a VUSB status code.
 *
 * @returns VUSB status code.
 * @param   i32Status    The USB/IP status code from the reply.
 */
DECLINLINE(VUSBSTATUS) usbProxyUsbIpVUsbStatusConvertFromStatus(int32_t i32Status)
{
    if (RT_LIKELY(   i32Status == USBIP_STATUS_SUCCESS
                  || i32Status == USBIP_STATUS_SHORT_READ))
        return VUSBSTATUS_OK;

    switch (i32Status)
    {
        case USBIP_STATUS_PIPE_STALLED:
            return VUSBSTATUS_STALL;
        default:
            return VUSBSTATUS_DNR;
    }
    /* not reached */
}

/**
 * Gets the next free sequence number.
 *
 * @returns Next free sequence number.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 */
DECLINLINE(uint32_t) usbProxyUsbIpSeqNumGet(PUSBPROXYDEVUSBIP pProxyDevUsbIp)
{
    uint32_t u32SeqNum = ASMAtomicIncU32(&pProxyDevUsbIp->u32SeqNumNext);
    if (RT_UNLIKELY(!u32SeqNum))
        u32SeqNum = ASMAtomicIncU32(&pProxyDevUsbIp->u32SeqNumNext);

    return u32SeqNum;
}

/**
 * Links a given URB into the given list.
 *
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 * @param   pList             The list to link the URB into.
 * @param   pUrbUsbIp         The URB to link.
 */
DECLINLINE(void) usbProxyUsbIpLinkUrb(PUSBPROXYDEVUSBIP pProxyDevUsbIp, PRTLISTANCHOR pList, PUSBPROXYURBUSBIP pUrbUsbIp)
{
    int rc = RTSemFastMutexRequest(pProxyDevUsbIp->hMtxLists);
    AssertRC(rc);
    RTListAppend(pList, &pUrbUsbIp->NodeList);
    RTSemFastMutexRelease(pProxyDevUsbIp->hMtxLists);
}

/**
 * Unlinks a given URB from the current assigned list.
 *
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 * @param   pUrbUsbIp         The URB to unlink.
 */
DECLINLINE(void) usbProxyUsbIpUnlinkUrb(PUSBPROXYDEVUSBIP pProxyDevUsbIp, PUSBPROXYURBUSBIP pUrbUsbIp)
{
    int rc = RTSemFastMutexRequest(pProxyDevUsbIp->hMtxLists);
    AssertRC(rc);
    RTListNodeRemove(&pUrbUsbIp->NodeList);
    RTSemFastMutexRelease(pProxyDevUsbIp->hMtxLists);
}

/**
 * Allocates a USB/IP proxy specific URB state.
 *
 * @returns Pointer to the USB/IP specific URB data or NULL on failure.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 */
static PUSBPROXYURBUSBIP usbProxyUsbIpUrbAlloc(PUSBPROXYDEVUSBIP pProxyDevUsbIp)
{
    NOREF(pProxyDevUsbIp);
    return (PUSBPROXYURBUSBIP)RTMemAllocZ(sizeof(USBPROXYURBUSBIP));
}

/**
 * Frees the given USB/IP URB state.
 *
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 * @param   pUrbUsbIp         The USB/IP speciic URB data.
 */
static void usbProxyUsbIpUrbFree(PUSBPROXYDEVUSBIP pProxyDevUsbIp, PUSBPROXYURBUSBIP pUrbUsbIp)
{
    NOREF(pProxyDevUsbIp);
    RTMemFree(pUrbUsbIp);
}

/**
 * Parse the string representation of the host address.
 *
 * @returns VBox status code.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data to parse the address for.
 * @param   pszAddress        The address string to parse.
 */
static int usbProxyUsbIpParseAddress(PUSBPROXYDEVUSBIP pProxyDevUsbIp, const char *pszAddress)
{
    int rc = VINF_SUCCESS;

    if (!RTStrNCmp(pszAddress, USBIP_URI_PREFIX, USBIP_URI_PREFIX_LEN))
    {
        pszAddress += USBIP_URI_PREFIX_LEN;

        const char *pszPortStart = RTStrStr(pszAddress, ":");
        if (pszPortStart)
        {
            pszPortStart++;

            const char *pszBusIdStart = RTStrStr(pszPortStart, ":");
            if (pszBusIdStart)
            {
                size_t cbHost = pszPortStart - pszAddress - 1;
                size_t cbBusId = strlen(pszBusIdStart);

                pszBusIdStart++;

                rc = RTStrToUInt32Ex(pszPortStart, NULL, 10 /* uBase */, &pProxyDevUsbIp->uPort);
                if (   rc == VINF_SUCCESS
                    || rc == VWRN_TRAILING_CHARS)
                {
                    rc = RTStrAllocEx(&pProxyDevUsbIp->pszHost, cbHost + 1);
                    if (RT_SUCCESS(rc))
                        rc = RTStrAllocEx(&pProxyDevUsbIp->pszBusId, cbBusId + 1);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTStrCopyEx(pProxyDevUsbIp->pszHost, cbHost + 1, pszAddress, cbHost);
                        AssertRC(rc);

                        rc = RTStrCopyEx(pProxyDevUsbIp->pszBusId, cbBusId + 1, pszBusIdStart, cbBusId);
                        AssertRC(rc);

                        return VINF_SUCCESS;
                    }
                }
                else
                    rc = VERR_INVALID_PARAMETER;
            }
            else
                rc = VERR_INVALID_PARAMETER;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

/**
 * Connects to the USB/IP host and claims the device given in the proxy device data.
 *
 * @returns VBox status code.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 */
static int usbProxyUsbIpConnect(PUSBPROXYDEVUSBIP pProxyDevUsbIp)
{
    int rc = VINF_SUCCESS;
    rc = RTTcpClientConnect(pProxyDevUsbIp->pszHost, pProxyDevUsbIp->uPort, &pProxyDevUsbIp->hSocket);
    if (RT_SUCCESS(rc))
    {
        /* Disable send coalescing. */
        rc = RTTcpSetSendCoalescing(pProxyDevUsbIp->hSocket, false);
        if (RT_FAILURE(rc))
            LogRel(("UsbIp: Disabling send coalescing failed (rc=%Rrc), continuing nevertheless but expect reduced performance\n", rc));

        /* Import the device, i.e. claim it for our use. */
        UsbIpReqImport ReqImport;
        ReqImport.u16Version = RT_H2N_U16(USBIP_VERSION);
        ReqImport.u16Cmd     = RT_H2N_U16(USBIP_INDICATOR_REQ | USBIP_REQ_RET_IMPORT);
        ReqImport.i32Status  = RT_H2N_S32(USBIP_STATUS_SUCCESS);
        rc = RTStrCopy(&ReqImport.aszBusId[0], sizeof(ReqImport.aszBusId), pProxyDevUsbIp->pszBusId);
        if (rc == VINF_SUCCESS)
        {
            rc = RTTcpWrite(pProxyDevUsbIp->hSocket, &ReqImport, sizeof(ReqImport));
            if (RT_SUCCESS(rc))
            {
                /* Read the reply. */
                UsbIpRetImport RetImport;
                rc = RTTcpRead(pProxyDevUsbIp->hSocket, &RetImport, sizeof(RetImport), NULL);
                if (RT_SUCCESS(rc))
                {
                    RetImport.u16Version = RT_N2H_U16(RetImport.u16Version);
                    RetImport.u16Cmd     = RT_N2H_U16(RetImport.u16Cmd);
                    RetImport.i32Status  = RT_N2H_S32(RetImport.i32Status);
                    if (   RetImport.u16Version == USBIP_VERSION
                        && RetImport.u16Cmd == USBIP_REQ_RET_IMPORT
                        && RetImport.i32Status == USBIP_STATUS_SUCCESS)
                    {
                        /* Read the device data. */
                        UsbIpExportedDevice Device;
                        rc = RTTcpRead(pProxyDevUsbIp->hSocket, &Device, sizeof(Device), NULL);
                        if (RT_SUCCESS(rc))
                        {
                            usbProxyUsbIpExportedDeviceN2H(&Device);
                            pProxyDevUsbIp->u32DevId = (Device.u32BusNum << 16) | Device.u32DevNum;

                            rc = RTPollSetAddSocket(pProxyDevUsbIp->hPollSet, pProxyDevUsbIp->hSocket,
                                                    RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, USBIP_POLL_ID_SOCKET);
                        }
                    }
                    else
                    {
                        /* Check what went wrong and leave a meaningful error message in the log. */
                        if (RetImport.u16Version != USBIP_VERSION)
                            LogRel(("UsbIp: Unexpected protocol version received from host (%#x vs. %#x)\n",
                                    RetImport.u16Version, USBIP_VERSION));
                        else if (RetImport.u16Cmd != USBIP_REQ_RET_IMPORT)
                            LogRel(("UsbIp: Unexpected reply code received from host (%#x vs. %#x)\n",
                                    RetImport.u16Cmd, USBIP_REQ_RET_IMPORT));
                        else if (RetImport.i32Status != 0)
                            LogRel(("UsbIp: Claiming the device has failed on the host with an unspecified error\n"));
                        else
                            AssertMsgFailed(("Something went wrong with if condition\n"));
                    }
                }
            }
        }
        else
        {
            LogRel(("UsbIp: Given bus ID is exceeds permitted protocol length: %u vs %u\n",
                    strlen(pProxyDevUsbIp->pszBusId) + 1, sizeof(ReqImport.aszBusId)));
            rc = VERR_INVALID_PARAMETER;
        }

        if (RT_FAILURE(rc))
            RTTcpClientCloseEx(pProxyDevUsbIp->hSocket, false /*fGracefulShutdown*/);
    }
    if (RT_FAILURE(rc))
        LogRel(("UsbIp: Connecting to the host %s failed with %Rrc\n", pProxyDevUsbIp->pszHost, rc));
    return rc;
}

/**
 * Disconnects from the USB/IP host releasing the device given in the proxy device data.
 *
 * @returns VBox status code.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 */
static int usbProxyUsbIpDisconnect(PUSBPROXYDEVUSBIP pProxyDevUsbIp)
{
    int rc = RTPollSetRemove(pProxyDevUsbIp->hPollSet, USBIP_POLL_ID_SOCKET);
    Assert(RT_SUCCESS(rc) || rc == VERR_POLL_HANDLE_ID_NOT_FOUND);

    rc = RTTcpClientCloseEx(pProxyDevUsbIp->hSocket, false /*fGracefulShutdown*/);
    if (RT_SUCCESS(rc))
        pProxyDevUsbIp->hSocket = NIL_RTSOCKET;
    return rc;
}

/**
 * Returns the URB matching the given sequence number from the in flight list.
 *
 * @returns pointer to the URB matching the given sequence number or NULL
 * @param  pProxyDevUsbIp    The USB/IP proxy device data.
 * @param  u32SeqNum         The sequence number to search for.
 */
static PUSBPROXYURBUSBIP usbProxyUsbIpGetInFlightUrbFromSeqNum(PUSBPROXYDEVUSBIP pProxyDevUsbIp, uint32_t u32SeqNum)
{
    bool fFound = false;

    int rc = RTSemFastMutexRequest(pProxyDevUsbIp->hMtxLists);
    AssertRC(rc);
    PUSBPROXYURBUSBIP pIt;
    RTListForEach(&pProxyDevUsbIp->ListUrbsInFlight, pIt, USBPROXYURBUSBIP, NodeList)
    {
        if (pIt->u32SeqNumUrb == u32SeqNum)
        {
            fFound = true;
            break;
        }
    }
    RTSemFastMutexRelease(pProxyDevUsbIp->hMtxLists);

    return fFound ? pIt : NULL;
}

/**
 * Returns the URB matching the given sequence number from the cancel list.
 *
 * @returns pointer to the URB matching the given sequence number or NULL
 * @param  pProxyDevUsbIp    The USB/IP proxy device data.
 * @param  u32SeqNum         The sequence number to search for.
 */
static PUSBPROXYURBUSBIP usbProxyUsbIpGetCancelledUrbFromSeqNum(PUSBPROXYDEVUSBIP pProxyDevUsbIp, uint32_t u32SeqNum)
{
    bool fFound = false;

    int rc = RTSemFastMutexRequest(pProxyDevUsbIp->hMtxLists);
    AssertRC(rc);
    PUSBPROXYURBUSBIP pIt;
    RTListForEach(&pProxyDevUsbIp->ListUrbsInFlight, pIt, USBPROXYURBUSBIP, NodeList)
    {
        if (   pIt->u32SeqNumUrbUnlink == u32SeqNum
            && pIt->fCancelled == true)
        {
            fFound = true;
            break;
        }
    }
    RTSemFastMutexRelease(pProxyDevUsbIp->hMtxLists);

    return fFound ? pIt : NULL;
}

/**
 * Resets the receive state for a new reply.
 *
 * @param  pProxyDevUsbIp    The USB/IP proxy device data.
 */
static void usbProxyUsbIpResetRecvState(PUSBPROXYDEVUSBIP pProxyDevUsbIp)
{
    pProxyDevUsbIp->enmRecvState = USBPROXYUSBIPRECVSTATE_HDR_COMMON;
    pProxyDevUsbIp->pbRecv       = (uint8_t *)&pProxyDevUsbIp->BufRet;
    pProxyDevUsbIp->cbRecv       = 0;
    pProxyDevUsbIp->cbLeft       = sizeof(UsbIpReqRetHdr);
}

static void usbProxyUsbIpRecvStateAdvance(PUSBPROXYDEVUSBIP pProxyDevUsbIp, USBPROXYUSBIPRECVSTATE enmState,
                                          uint8_t *pbData, size_t cbData)
{
    pProxyDevUsbIp->enmRecvState = enmState;
    pProxyDevUsbIp->cbRecv = 0;
    pProxyDevUsbIp->cbLeft = cbData;
    pProxyDevUsbIp->pbRecv = pbData;
}

/**
 * Handles reception of a USB/IP PDU.
 *
 * @returns VBox status code.
 * @param  pProxyDevUsbIp    The USB/IP proxy device data.
 * @param  ppUrbUsbIp        Where to store the pointer to the USB/IP URB which completed.
 *                           Will be NULL if the received PDU is not complete and we have
 *                           have to wait for more data or on failure.
 */
static int usbProxyUsbIpRecvPdu(PUSBPROXYDEVUSBIP pProxyDevUsbIp, PUSBPROXYURBUSBIP *ppUrbUsbIp)
{
    int rc = VINF_SUCCESS;
    size_t cbRead = 0;
    PUSBPROXYURBUSBIP pUrbUsbIp = NULL;

    Assert(pProxyDevUsbIp->cbLeft);

    /* Read any available data first. */
    rc = RTTcpReadNB(pProxyDevUsbIp->hSocket, pProxyDevUsbIp->pbRecv, pProxyDevUsbIp->cbLeft, &cbRead);
    if (RT_SUCCESS(rc))
    {
        pProxyDevUsbIp->cbRecv += cbRead;
        pProxyDevUsbIp->cbLeft -= cbRead;
        pProxyDevUsbIp->pbRecv += cbRead;

        /* Process the received data if there is nothing to receive left for the current state. */
        if (!pProxyDevUsbIp->cbLeft)
        {
            switch (pProxyDevUsbIp->enmRecvState)
            {
                case USBPROXYUSBIPRECVSTATE_HDR_COMMON:
                {
                    Assert(pProxyDevUsbIp->cbRecv == sizeof(UsbIpReqRetHdr));

                    /*
                     * Determine the residual amount of data to receive until
                     * the complete reply header was received.
                     */
                    switch (RT_N2H_U32(pProxyDevUsbIp->BufRet.Hdr.u32ReqRet))
                    {
                        case USBIP_RET_SUBMIT:
                            pProxyDevUsbIp->cbLeft = sizeof(UsbIpRetSubmit) - sizeof(UsbIpReqRetHdr);
                            pProxyDevUsbIp->enmRecvState = USBPROXYUSBIPRECVSTATE_HDR_RESIDUAL;
                            break;
                        case USBIP_RET_UNLINK:
                            pProxyDevUsbIp->cbLeft = sizeof(UsbIpRetUnlink) - sizeof(UsbIpReqRetHdr);
                            pProxyDevUsbIp->enmRecvState = USBPROXYUSBIPRECVSTATE_HDR_RESIDUAL;
                            break;
                        default:
                            AssertLogRelMsgFailed(("Invalid reply header received: %d\n",
                                                   pProxyDevUsbIp->BufRet.Hdr.u32ReqRet));
                            usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
                    }

                    break;
                }
                case USBPROXYUSBIPRECVSTATE_HDR_RESIDUAL:
                {
                    switch (RT_N2H_U32(pProxyDevUsbIp->BufRet.Hdr.u32ReqRet))
                    {
                        case USBIP_RET_SUBMIT:
                            /* Get the URB from the in flight list. */
                            pProxyDevUsbIp->pUrbUsbIp = usbProxyUsbIpGetInFlightUrbFromSeqNum(pProxyDevUsbIp, RT_N2H_U32(pProxyDevUsbIp->BufRet.Hdr.u32SeqNum));
                            if (pProxyDevUsbIp->pUrbUsbIp)
                            {
                                usbProxyUsbIpRetSubmitN2H(&pProxyDevUsbIp->BufRet.RetSubmit);

                                /* We still have to receive the transfer buffer, even in case of an error. */
                                pProxyDevUsbIp->pUrbUsbIp->enmStatus = usbProxyUsbIpVUsbStatusConvertFromStatus(pProxyDevUsbIp->BufRet.RetSubmit.i32Status);
                                if (pProxyDevUsbIp->pUrbUsbIp->enmDir == VUSBDIRECTION_IN)
                                {
                                    uint8_t *pbData = NULL;
                                    size_t cbRet = 0;

                                    AssertPtr(pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb);
                                    if (pProxyDevUsbIp->pUrbUsbIp->enmType == VUSBXFERTYPE_MSG)
                                    {
                                        /* Preserve the setup request. */
                                        pbData = &pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->abData[sizeof(VUSBSETUP)];
                                        cbRet = pProxyDevUsbIp->BufRet.RetSubmit.u32ActualLength + sizeof(VUSBSETUP);
                                    }
                                    else
                                    {
                                        pbData = &pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->abData[0];
                                        cbRet = pProxyDevUsbIp->BufRet.RetSubmit.u32ActualLength;
                                    }

                                    if (pProxyDevUsbIp->BufRet.RetSubmit.u32ActualLength)
                                    {
                                        if (RT_LIKELY(pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->cbData >= cbRet))
                                        {
                                            pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->cbData = (uint32_t)cbRet;
                                            usbProxyUsbIpRecvStateAdvance(pProxyDevUsbIp, USBPROXYUSBIPRECVSTATE_URB_BUFFER,
                                                                          pbData, pProxyDevUsbIp->BufRet.RetSubmit.u32ActualLength);
                                        }
                                        else
                                        {
                                            /*
                                             * Bogus length returned from the USB/IP remote server.
                                             * Error out because there is no way to find the end of the current
                                             * URB and the beginning of the next one. The error will cause closing the
                                             * connection to the rogue remote and all URBs get completed with an error.
                                             */
                                            LogRelMax(10, ("USB/IP: Received reply with sequence number %u contains invalid length %zu (max %zu)\n",
                                                           pProxyDevUsbIp->BufRet.Hdr.u32SeqNum, cbRet,
                                                           pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->cbData));
                                            rc = VERR_NET_PROTOCOL_ERROR;
                                        }
                                    }
                                    else
                                    {
                                        pUrbUsbIp = pProxyDevUsbIp->pUrbUsbIp;
                                        usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
                                    }
                                }
                                else
                                {
                                    Assert(pProxyDevUsbIp->pUrbUsbIp->enmDir == VUSBDIRECTION_OUT);
                                    pUrbUsbIp = pProxyDevUsbIp->pUrbUsbIp;
                                    usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
                                }
                            }
                            else
                            {
                                LogRel(("USB/IP: Received reply with sequence number %u doesn't match any local URB\n",
                                        RT_N2H_U32(pProxyDevUsbIp->BufRet.Hdr.u32SeqNum)));
                                usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
                                rc = VERR_NET_PROTOCOL_ERROR;
                            }
                            break;
                        case USBIP_RET_UNLINK:
                            pProxyDevUsbIp->pUrbUsbIp = usbProxyUsbIpGetCancelledUrbFromSeqNum(pProxyDevUsbIp, RT_N2H_U32(pProxyDevUsbIp->BufRet.Hdr.u32SeqNum));
                            if (pProxyDevUsbIp->pUrbUsbIp)
                            {
                                usbProxyUsbIpRetUnlinkN2H(&pProxyDevUsbIp->BufRet.RetUnlink);
                                pUrbUsbIp = pProxyDevUsbIp->pUrbUsbIp;
                                pUrbUsbIp->pVUsbUrb->enmStatus = usbProxyUsbIpVUsbStatusConvertFromStatus(pProxyDevUsbIp->BufRet.RetUnlink.i32Status);
                            }
                            /* else: Probably received the data for the URB and is complete already. */

                            usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
                            break;
                    }

                    break;
                }
                case USBPROXYUSBIPRECVSTATE_URB_BUFFER:
                    if (pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->enmType == VUSBXFERTYPE_ISOC)
                        usbProxyUsbIpRecvStateAdvance(pProxyDevUsbIp, USBPROXYUSBIPRECVSTATE_ISOC_PKT_DESCS,
                                                      (uint8_t *)&pProxyDevUsbIp->aIsocPktDesc[0],
                                                      pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->cIsocPkts * sizeof(UsbIpIsocPktDesc));
                    else
                    {
                        pUrbUsbIp = pProxyDevUsbIp->pUrbUsbIp;
                        usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
                    }
                    break;
                case USBPROXYUSBIPRECVSTATE_ISOC_PKT_DESCS:
                    /* Process all received isochronous packet descriptors. */
                    for (unsigned i = 0; i < pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->cIsocPkts; i++)
                    {
                        PVUSBURBISOCPTK pIsocPkt = &pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->aIsocPkts[i];
                        PUsbIpIsocPktDesc pIsocPktUsbIp = &pProxyDevUsbIp->aIsocPktDesc[i];

                        usbProxyUsbIpIsocPktDescN2H(pIsocPktUsbIp);
                        pIsocPkt->enmStatus = usbProxyUsbIpVUsbStatusConvertFromStatus(pIsocPktUsbIp->i32Status);

                        if (RT_LIKELY(   pIsocPktUsbIp->u32Offset < pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->cbData
                                      && pProxyDevUsbIp->pUrbUsbIp->pVUsbUrb->cbData - pIsocPktUsbIp->u32Offset >= pIsocPktUsbIp->u32ActualLength))
                        {
                            pIsocPkt->off = pIsocPktUsbIp->u32Offset;
                            pIsocPkt->cb  = pIsocPktUsbIp->u32ActualLength;
                        }
                        else
                        {
                            /*
                             * The offset and length value in the isoc packet descriptor are bogus and would cause a buffer overflow later on, leave an
                             * error message and disconnect from the rogue remote end.
                             */
                            LogRelMax(10, ("USB/IP: Received reply with sequence number %u contains invalid isoc packet descriptor %u (offset=%u length=%u)\n",
                                           pProxyDevUsbIp->BufRet.Hdr.u32SeqNum, i,
                                           pIsocPktUsbIp->u32Offset, pIsocPktUsbIp->u32ActualLength));
                            rc = VERR_NET_PROTOCOL_ERROR;
                            break;
                        }
                    }

                    pUrbUsbIp = pProxyDevUsbIp->pUrbUsbIp;
                    usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
                    break;
                default:
                    AssertLogRelMsgFailed(("USB/IP: Invalid receive state %d\n", pProxyDevUsbIp->enmRecvState));
            }
        }
    }

    if (RT_SUCCESS(rc))
        *ppUrbUsbIp = pUrbUsbIp;
    else
    {
        /* Complete all URBs with DNR error and mark device as unplugged, the current one is still in the in flight list. */
        pProxyDevUsbIp->pUrbUsbIp = NULL;
        usbProxyUsbIpResetRecvState(pProxyDevUsbIp);
        usbProxyUsbIpDisconnect(pProxyDevUsbIp);

        rc = RTSemFastMutexRequest(pProxyDevUsbIp->hMtxLists);
        AssertRC(rc);
        PUSBPROXYURBUSBIP pIt;
        PUSBPROXYURBUSBIP pItNext;
        RTListForEachSafe(&pProxyDevUsbIp->ListUrbsInFlight, pIt, pItNext, USBPROXYURBUSBIP, NodeList)
        {
            if (pIt->pVUsbUrb) /* can be NULL for requests created by usbProxyUsbIpCtrlUrbExchangeSync(). */
                pIt->pVUsbUrb->enmStatus = VUSBSTATUS_CRC;
            RTListNodeRemove(&pIt->NodeList);
            RTListAppend(&pProxyDevUsbIp->ListUrbsLanded, &pIt->NodeList);
        }
        RTSemFastMutexRelease(pProxyDevUsbIp->hMtxLists);
    }

    return rc;
}

/**
 * Worker for queueing an URB on the main I/O thread.
 *
 * @returns VBox status code.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 * @param   pUrbUsbIp         The USB/IP URB to queue.
 */
static int usbProxyUsbIpUrbQueueWorker(PUSBPROXYDEVUSBIP pProxyDevUsbIp, PUSBPROXYURBUSBIP pUrbUsbIp)
{
    PVUSBURB pUrb = pUrbUsbIp->pVUsbUrb;

    pUrbUsbIp->u32SeqNumUrb = usbProxyUsbIpSeqNumGet(pProxyDevUsbIp);
    pUrbUsbIp->enmType      = pUrb->enmType;
    pUrbUsbIp->enmStatus    = pUrb->enmStatus;
    pUrbUsbIp->enmDir       = pUrb->enmDir;

    UsbIpReqSubmit ReqSubmit;

    RT_ZERO(ReqSubmit);
    ReqSubmit.Hdr.u32ReqRet           = USBIP_CMD_SUBMIT;
    ReqSubmit.Hdr.u32SeqNum           = pUrbUsbIp->u32SeqNumUrb;
    ReqSubmit.Hdr.u32DevId            = pProxyDevUsbIp->u32DevId;
    ReqSubmit.Hdr.u32Endpoint         = pUrb->EndPt;
    ReqSubmit.Hdr.u32Direction        = pUrb->enmDir == VUSBDIRECTION_IN ? USBIP_DIR_IN : USBIP_DIR_OUT;
    ReqSubmit.u32XferFlags            = 0;
    if (pUrb->enmDir == VUSBDIRECTION_IN && pUrb->fShortNotOk)
        ReqSubmit.u32XferFlags |= USBIP_XFER_FLAGS_SHORT_NOT_OK;

    ReqSubmit.u32TransferBufferLength = pUrb->cbData;
    ReqSubmit.u32StartFrame           = 0;
    ReqSubmit.u32NumIsocPkts          = 0;
    ReqSubmit.u32Interval             = 0;

    RTSGSEG          aSegReq[3]; /* Maximum number of segments used for a Isochronous transfer. */
    UsbIpIsocPktDesc aIsocPktsDesc[8];
    unsigned cSegsUsed = 1;
    aSegReq[0].pvSeg = &ReqSubmit;
    aSegReq[0].cbSeg = sizeof(ReqSubmit);

    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_MSG:
            memcpy(&ReqSubmit.Setup, &pUrb->abData, sizeof(ReqSubmit.Setup));
            ReqSubmit.u32TransferBufferLength -= sizeof(VUSBSETUP);
            if (pUrb->enmDir == VUSBDIRECTION_OUT)
            {
                aSegReq[cSegsUsed].cbSeg = pUrb->cbData - sizeof(VUSBSETUP);
                aSegReq[cSegsUsed].pvSeg = pUrb->abData + sizeof(VUSBSETUP);
                if (aSegReq[cSegsUsed].cbSeg)
                    cSegsUsed++;
            }
            LogFlowFunc(("Message (Control) URB\n"));
            break;
        case VUSBXFERTYPE_ISOC:
            LogFlowFunc(("Isochronous URB\n"));
            ReqSubmit.u32XferFlags |= USBIP_XFER_FLAGS_ISO_ASAP;
            ReqSubmit.u32NumIsocPkts = pUrb->cIsocPkts;
            if (pUrb->enmDir == VUSBDIRECTION_OUT)
            {
                aSegReq[cSegsUsed].cbSeg = pUrb->cbData;
                aSegReq[cSegsUsed].pvSeg = pUrb->abData;
                cSegsUsed++;
            }

            for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
            {
                aIsocPktsDesc[i].u32Offset       = pUrb->aIsocPkts[i].off;
                aIsocPktsDesc[i].u32Length       = pUrb->aIsocPkts[i].cb;
                aIsocPktsDesc[i].u32ActualLength = 0; /** @todo */
                aIsocPktsDesc[i].i32Status       = pUrb->aIsocPkts[i].enmStatus;
                usbProxyUsbIpIsocPktDescH2N(&aIsocPktsDesc[i]);
            }

            if (pUrb->cIsocPkts)
            {
                aSegReq[cSegsUsed].cbSeg = pUrb->cIsocPkts * sizeof(UsbIpIsocPktDesc);
                aSegReq[cSegsUsed].pvSeg = &aIsocPktsDesc[0];
                cSegsUsed++;
            }

            break;
        case VUSBXFERTYPE_BULK:
        case VUSBXFERTYPE_INTR:
            LogFlowFunc(("Bulk URB\n"));
            if (pUrb->enmDir == VUSBDIRECTION_OUT)
            {
                aSegReq[cSegsUsed].cbSeg = pUrb->cbData;
                aSegReq[cSegsUsed].pvSeg = pUrb->abData;
                cSegsUsed++;
            }
            break;
        default:
            return VERR_INVALID_PARAMETER; /** @todo better status code. */
    }

    usbProxyUsbIpReqSubmitH2N(&ReqSubmit);

    Assert(cSegsUsed <= RT_ELEMENTS(aSegReq));

    /* Send the command. */
    RTSGBUF SgBufReq;
    RTSgBufInit(&SgBufReq, &aSegReq[0], cSegsUsed);

    int rc = RTTcpSgWrite(pProxyDevUsbIp->hSocket, &SgBufReq);
    if (RT_SUCCESS(rc))
    {
        /* Link the URB into the list of in flight URBs. */
        usbProxyUsbIpLinkUrb(pProxyDevUsbIp, &pProxyDevUsbIp->ListUrbsInFlight, pUrbUsbIp);
    }

    return rc;
}

/**
 * Queues all pending URBs from the list.
 *
 * @returns VBox status code.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 */
static int usbProxyUsbIpUrbsQueuePending(PUSBPROXYDEVUSBIP pProxyDevUsbIp)
{
    RTLISTANCHOR ListUrbsPending;

    int rc = RTSemFastMutexRequest(pProxyDevUsbIp->hMtxLists);
    AssertRC(rc);
    RTListMove(&ListUrbsPending, &pProxyDevUsbIp->ListUrbsToQueue);
    RTSemFastMutexRelease(pProxyDevUsbIp->hMtxLists);

    PUSBPROXYURBUSBIP pIter;
    PUSBPROXYURBUSBIP pIterNext;
    RTListForEachSafe(&ListUrbsPending, pIter, pIterNext, USBPROXYURBUSBIP, NodeList)
    {
        RTListNodeRemove(&pIter->NodeList);
        rc = usbProxyUsbIpUrbQueueWorker(pProxyDevUsbIp, pIter);
        if (RT_FAILURE(rc))
        {
            /* Complete URB with an error and place into landed list. */
            pIter->pVUsbUrb->enmStatus = VUSBSTATUS_DNR;
            usbProxyUsbIpLinkUrb(pProxyDevUsbIp, &pProxyDevUsbIp->ListUrbsLanded, pIter);
        }
    }

    return VINF_SUCCESS;
}

/**
 * Kick the reaper thread.
 *
 * @returns VBox status code.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 * @param   bReason           The wakeup reason.
 */
static int usbProxyReaperKick(PUSBPROXYDEVUSBIP pProxyDevUsbIp, char bReason)
{
    int rc = VINF_SUCCESS;
    size_t cbWritten = 0;

    rc = RTPipeWrite(pProxyDevUsbIp->hPipeW, &bReason, 1, &cbWritten);
    Assert(RT_SUCCESS(rc) || cbWritten == 0);

    return rc;
}

/**
 * Drain the wakeup pipe.
 *
 * @returns Wakeup reason.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 */
static char usbProxyUsbIpWakeupPipeDrain(PUSBPROXYDEVUSBIP pProxyDevUsbIp)
{
    char bRead = 0;
    size_t cbRead = 0;
    int rc = RTPipeRead(pProxyDevUsbIp->hPipeR, &bRead, 1, &cbRead);
    Assert(RT_SUCCESS(rc) && cbRead == 1); NOREF(rc);

    return bRead;
}

/**
 * Executes the poll/receive loop either until a URB is received (with an optional matching sequence number) or
 * the given timeout has elapsed.
 *
 * @returns Pointer to the received USB/IP URB or NULL on timeout or error.
 * @param   pProxyDevUsbIp    The USB/IP proxy device data.
 * @param   u32SeqNumRet      The sequence number of a specific reply to return the URB for, 0 if
 *                            any received URB is accepted.
 * @param   fPollWakePipe     Flag whether to poll the wakeup pipe.
 * @param   cMillies          Maximum number of milliseconds to wait for an URB to arrive.
 */
static PUSBPROXYURBUSBIP usbProxyUsbIpPollWorker(PUSBPROXYDEVUSBIP pProxyDevUsbIp, uint32_t u32SeqNumRet,
                                                 bool fPollWakePipe, RTMSINTERVAL cMillies)
{
    int rc = VINF_SUCCESS;
    PUSBPROXYURBUSBIP pUrbUsbIp = NULL;

    if (!fPollWakePipe)
    {
        rc = RTPollSetEventsChange(pProxyDevUsbIp->hPollSet, USBIP_POLL_ID_PIPE, RTPOLL_EVT_ERROR);
        AssertRC(rc);
    }

    while (!pUrbUsbIp && RT_SUCCESS(rc) && cMillies)
    {
        uint32_t uIdReady = 0;
        uint32_t fEventsRecv = 0;
        RTMSINTERVAL msStart = RTTimeMilliTS();
        RTMSINTERVAL msNow;

        rc = RTPoll(pProxyDevUsbIp->hPollSet, cMillies, &fEventsRecv, &uIdReady);
        Assert(RT_SUCCESS(rc) || rc == VERR_TIMEOUT);
        if (RT_SUCCESS(rc))
        {
            msNow = RTTimeMilliTS();
            cMillies = msNow - msStart >= cMillies ? 0 : cMillies - (msNow - msStart);

            if (uIdReady == USBIP_POLL_ID_SOCKET)
            {
                rc = usbProxyUsbIpRecvPdu(pProxyDevUsbIp, &pUrbUsbIp);
                if (   RT_SUCCESS(rc)
                    && pUrbUsbIp)
                {
                    /* Link the URB into the landed list if a specifc reply is requested and the URB doesn't match. */
                    if (   u32SeqNumRet != 0
                        && pUrbUsbIp->u32SeqNumUrb != u32SeqNumRet)
                    {
                        usbProxyUsbIpUnlinkUrb(pProxyDevUsbIp, pUrbUsbIp);
                        usbProxyUsbIpLinkUrb(pProxyDevUsbIp, &pProxyDevUsbIp->ListUrbsLanded, pUrbUsbIp);
                        pUrbUsbIp = NULL;
                    }
                }
            }
            else
            {
                AssertLogRelMsg(uIdReady == USBIP_POLL_ID_PIPE, ("Invalid pollset ID given\n"));

                char bReason = usbProxyUsbIpWakeupPipeDrain(pProxyDevUsbIp);
                if (bReason == USBIP_REAPER_WAKEUP_REASON_QUEUE)
                    usbProxyUsbIpUrbsQueuePending(pProxyDevUsbIp);
                else
                {
                    Assert(bReason == USBIP_REAPER_WAKEUP_REASON_EXTERNAL);
                    break;
                }
            }
        }
    }

    if (!fPollWakePipe)
    {
        rc = RTPollSetEventsChange(pProxyDevUsbIp->hPollSet, USBIP_POLL_ID_PIPE, RTPOLL_EVT_READ);
        AssertRC(rc);
    }

    return pUrbUsbIp;
}

/**
 * Synchronously exchange a given control message with the remote device.
 *
 * @eturns VBox status code.
 * @param  pProxyDevUsbIp    The USB/IP proxy device data.
 * @param  pSetup            The setup message.
 *
 * @note This method is only used to implement the *SetConfig, *SetInterface and *ClearHaltedEp
 *       callbacks because the USB/IP protocol lacks dedicated requests for these.
 * @remark It is assumed that this method is never called while usbProxyUsbIpUrbReap is called
 *         on another thread.
 */
static int usbProxyUsbIpCtrlUrbExchangeSync(PUSBPROXYDEVUSBIP pProxyDevUsbIp, PVUSBSETUP pSetup)
{
    int rc = VINF_SUCCESS;

    UsbIpReqSubmit ReqSubmit;
    USBPROXYURBUSBIP UsbIpUrb;

    RT_ZERO(ReqSubmit);

    uint32_t u32SeqNum = usbProxyUsbIpSeqNumGet(pProxyDevUsbIp);
    ReqSubmit.Hdr.u32ReqRet           = USBIP_CMD_SUBMIT;
    ReqSubmit.Hdr.u32SeqNum           = u32SeqNum;
    ReqSubmit.Hdr.u32DevId            = pProxyDevUsbIp->u32DevId;
    ReqSubmit.Hdr.u32Direction        = USBIP_DIR_OUT;
    ReqSubmit.Hdr.u32Endpoint         = 0; /* Only default control endpoint is allowed for these kind of messages. */
    ReqSubmit.u32XferFlags            = 0;
    ReqSubmit.u32TransferBufferLength = 0;
    ReqSubmit.u32StartFrame           = 0;
    ReqSubmit.u32NumIsocPkts          = 0;
    ReqSubmit.u32Interval             = 0;
    memcpy(&ReqSubmit.Setup, pSetup, sizeof(ReqSubmit.Setup));
    usbProxyUsbIpReqSubmitH2N(&ReqSubmit);

    UsbIpUrb.u32SeqNumUrb       = u32SeqNum;
    UsbIpUrb.u32SeqNumUrbUnlink = 0;
    UsbIpUrb.fCancelled         = false;
    UsbIpUrb.enmType            = VUSBXFERTYPE_MSG;
    UsbIpUrb.enmDir             = VUSBDIRECTION_OUT;
    UsbIpUrb.pVUsbUrb           = NULL;

    /* Send the command. */
    rc = RTTcpWrite(pProxyDevUsbIp->hSocket, &ReqSubmit, sizeof(ReqSubmit));
    if (RT_SUCCESS(rc))
    {
        usbProxyUsbIpLinkUrb(pProxyDevUsbIp, &pProxyDevUsbIp->ListUrbsInFlight, &UsbIpUrb);
        PUSBPROXYURBUSBIP pUrbUsbIp = usbProxyUsbIpPollWorker(pProxyDevUsbIp, u32SeqNum, false /*fPollWakePipe*/,
                                                              30 * RT_MS_1SEC);
        Assert(   !pUrbUsbIp
               || pUrbUsbIp == &UsbIpUrb); /* The returned URB should point to the URB we submitted. */
        usbProxyUsbIpUnlinkUrb(pProxyDevUsbIp, &UsbIpUrb);

        if (!pUrbUsbIp)
            rc = VERR_TIMEOUT;
    }

    return rc;
}


/*
 * The USB proxy device functions.
 */

/**
 * @interface_method_impl{USBPROXYBACK,pfnOpen}
 */
static DECLCALLBACK(int) usbProxyUsbIpOpen(PUSBPROXYDEV pProxyDev, const char *pszAddress)
{
    LogFlowFunc(("pProxyDev=%p pszAddress=%s\n", pProxyDev, pszAddress));

    PUSBPROXYDEVUSBIP pDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    int rc = VINF_SUCCESS;

    RTListInit(&pDevUsbIp->ListUrbsInFlight);
    RTListInit(&pDevUsbIp->ListUrbsLanded);
    RTListInit(&pDevUsbIp->ListUrbsToQueue);
    pDevUsbIp->hSocket       = NIL_RTSOCKET;
    pDevUsbIp->hPollSet      = NIL_RTPOLLSET;
    pDevUsbIp->hPipeW        = NIL_RTPIPE;
    pDevUsbIp->hPipeR        = NIL_RTPIPE;
    pDevUsbIp->u32SeqNumNext = 0;
    pDevUsbIp->pszHost       = NULL;
    pDevUsbIp->pszBusId      = NULL;
    usbProxyUsbIpResetRecvState(pDevUsbIp);

    rc = RTSemFastMutexCreate(&pDevUsbIp->hMtxLists);
    if (RT_SUCCESS(rc))
    {
        /* Setup wakeup pipe and poll set first. */
        rc = RTPipeCreate(&pDevUsbIp->hPipeR, &pDevUsbIp->hPipeW, 0);
        if (RT_SUCCESS(rc))
        {
            rc = RTPollSetCreate(&pDevUsbIp->hPollSet);
            if (RT_SUCCESS(rc))
            {
                rc = RTPollSetAddPipe(pDevUsbIp->hPollSet, pDevUsbIp->hPipeR,
                                      RTPOLL_EVT_READ, USBIP_POLL_ID_PIPE);
                if (RT_SUCCESS(rc))
                {
                    /* Connect to the USB/IP host. */
                    rc = usbProxyUsbIpParseAddress(pDevUsbIp, pszAddress);
                    if (RT_SUCCESS(rc))
                        rc = usbProxyUsbIpConnect(pDevUsbIp);
                }

                if (RT_FAILURE(rc))
                {
                    RTPollSetRemove(pDevUsbIp->hPollSet, USBIP_POLL_ID_PIPE);
                    int rc2 = RTPollSetDestroy(pDevUsbIp->hPollSet);
                    AssertRC(rc2);
                }
            }

            if (RT_FAILURE(rc))
            {
                int rc2 = RTPipeClose(pDevUsbIp->hPipeR);
                AssertRC(rc2);
                rc2 = RTPipeClose(pDevUsbIp->hPipeW);
                AssertRC(rc2);
            }
        }
    }

    return rc;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnClose}
 */
static DECLCALLBACK(void) usbProxyUsbIpClose(PUSBPROXYDEV pProxyDev)
{
    int rc = VINF_SUCCESS;
    LogFlowFunc(("pProxyDev = %p\n", pProxyDev));

    PUSBPROXYDEVUSBIP pDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    if (pDevUsbIp->hSocket != NIL_RTSOCKET)
        usbProxyUsbIpDisconnect(pDevUsbIp);

    /* Destroy the pipe and pollset if necessary. */
    if (pDevUsbIp->hPollSet != NIL_RTPOLLSET)
    {
        rc = RTPollSetRemove(pDevUsbIp->hPollSet, USBIP_POLL_ID_PIPE);
        AssertRC(rc);
        rc = RTPollSetDestroy(pDevUsbIp->hPollSet);
        AssertRC(rc);
        rc = RTPipeClose(pDevUsbIp->hPipeR);
        AssertRC(rc);
        rc = RTPipeClose(pDevUsbIp->hPipeW);
        AssertRC(rc);
    }

    if (pDevUsbIp->pszHost)
        RTStrFree(pDevUsbIp->pszHost);
    if (pDevUsbIp->pszBusId)
        RTStrFree(pDevUsbIp->pszBusId);

    /* Clear the URB lists. */
    rc = RTSemFastMutexRequest(pDevUsbIp->hMtxLists);
    AssertRC(rc);
    PUSBPROXYURBUSBIP pIter;
    PUSBPROXYURBUSBIP pIterNext;
    RTListForEachSafe(&pDevUsbIp->ListUrbsInFlight, pIter, pIterNext, USBPROXYURBUSBIP, NodeList)
    {
        RTListNodeRemove(&pIter->NodeList);
        RTMemFree(pIter);
    }

    RTListForEachSafe(&pDevUsbIp->ListUrbsLanded, pIter, pIterNext, USBPROXYURBUSBIP, NodeList)
    {
        RTListNodeRemove(&pIter->NodeList);
        RTMemFree(pIter);
    }
    RTSemFastMutexRelease(pDevUsbIp->hMtxLists);
    RTSemFastMutexDestroy(pDevUsbIp->hMtxLists);
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnReset}
 */
static DECLCALLBACK(int) usbProxyUsbIpReset(PUSBPROXYDEV pProxyDev, bool fResetOnLinux)
{
    LogFlowFunc(("pProxyDev = %p\n", pProxyDev));

    int rc = VINF_SUCCESS;
    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    VUSBSETUP Setup;

    if (fResetOnLinux)
    {
        Setup.bmRequestType = RT_BIT(5) | 0x03; /* Port request. */
        Setup.bRequest      = 0x03; /* SET_FEATURE */
        Setup.wValue        = 4; /* Port feature: Reset */
        Setup.wIndex        = 0; /* Port number, irrelevant */
        Setup.wLength       = 0;
        rc =  usbProxyUsbIpCtrlUrbExchangeSync(pProxyDevUsbIp, &Setup);
        if (RT_SUCCESS(rc))
        {
            pProxyDev->iActiveCfg = -1;
            pProxyDev->cIgnoreSetConfigs = 2;
        }
    }

    return rc;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnSetConfig}
 */
static DECLCALLBACK(int) usbProxyUsbIpSetConfig(PUSBPROXYDEV pProxyDev, int iCfg)
{
    LogFlowFunc(("pProxyDev=%s cfg=%#x\n", pProxyDev->pUsbIns->pszName, iCfg));

    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    VUSBSETUP Setup;

    Setup.bmRequestType = 0;
    Setup.bRequest      = 0x09;
    Setup.wValue        = iCfg;
    Setup.wIndex        = 0;
    Setup.wLength       = 0;
    return usbProxyUsbIpCtrlUrbExchangeSync(pProxyDevUsbIp, &Setup);
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnClaimInterface}
 */
static DECLCALLBACK(int) usbProxyUsbIpClaimInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    RT_NOREF(pProxyDev, iIf);
    LogFlowFunc(("pProxyDev=%s iIf=%#x\n", pProxyDev->pUsbIns->pszName, iIf));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnReleaseInterface}
 */
static DECLCALLBACK(int) usbProxyUsbIpReleaseInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    RT_NOREF(pProxyDev, iIf);
    LogFlowFunc(("pProxyDev=%s iIf=%#x\n", pProxyDev->pUsbIns->pszName, iIf));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnSetInterface}
 */
static DECLCALLBACK(int) usbProxyUsbIpSetInterface(PUSBPROXYDEV pProxyDev, int iIf, int setting)
{
    LogFlowFunc(("pProxyDev=%p iIf=%#x setting=%#x\n", pProxyDev, iIf, setting));

    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    VUSBSETUP Setup;

    Setup.bmRequestType = 0x1;
    Setup.bRequest      = 0x0b; /* SET_INTERFACE */
    Setup.wValue        = setting;
    Setup.wIndex        = iIf;
    Setup.wLength       = 0;
    return usbProxyUsbIpCtrlUrbExchangeSync(pProxyDevUsbIp, &Setup);
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnClearHaltedEndpoint}
 */
static DECLCALLBACK(int) usbProxyUsbIpClearHaltedEp(PUSBPROXYDEV pProxyDev, unsigned int iEp)
{
    LogFlowFunc(("pProxyDev=%s ep=%u\n", pProxyDev->pUsbIns->pszName, iEp));

    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    VUSBSETUP Setup;

    Setup.bmRequestType = 0x2;
    Setup.bRequest      = 0x01; /* CLEAR_FEATURE */
    Setup.wValue        = 0x00; /* ENDPOINT_HALT */
    Setup.wIndex        = iEp;
    Setup.wLength       = 0;
    return usbProxyUsbIpCtrlUrbExchangeSync(pProxyDevUsbIp, &Setup);
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnUrbQueue}
 */
static DECLCALLBACK(int) usbProxyUsbIpUrbQueue(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    LogFlowFunc(("pUrb=%p\n", pUrb));

    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);

    /* Allocate a USB/IP Urb. */
    PUSBPROXYURBUSBIP pUrbUsbIp = usbProxyUsbIpUrbAlloc(pProxyDevUsbIp);
    if (!pUrbUsbIp)
        return VERR_NO_MEMORY;

    pUrbUsbIp->fCancelled = false;
    pUrbUsbIp->pVUsbUrb = pUrb;
    pUrb->Dev.pvPrivate = pUrbUsbIp;

    int rc = RTSemFastMutexRequest(pProxyDevUsbIp->hMtxLists);
    AssertRC(rc);
    RTListAppend(&pProxyDevUsbIp->ListUrbsToQueue, &pUrbUsbIp->NodeList);
    RTSemFastMutexRelease(pProxyDevUsbIp->hMtxLists);

    return usbProxyReaperKick(pProxyDevUsbIp, USBIP_REAPER_WAKEUP_REASON_QUEUE);
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnUrbReap}
 */
static DECLCALLBACK(PVUSBURB) usbProxyUsbIpUrbReap(PUSBPROXYDEV pProxyDev, RTMSINTERVAL cMillies)
{
    LogFlowFunc(("pProxyDev=%s\n", pProxyDev->pUsbIns->pszName));

    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    PUSBPROXYURBUSBIP pUrbUsbIp = NULL;
    PVUSBURB pUrb = NULL;
    int rc = VINF_SUCCESS;

    /* Queue new URBs first. */
    rc = usbProxyUsbIpUrbsQueuePending(pProxyDevUsbIp);
    AssertRC(rc);

    /* Any URBs pending delivery? */
    if (!RTListIsEmpty(&pProxyDevUsbIp->ListUrbsLanded))
        pUrbUsbIp = RTListGetFirst(&pProxyDevUsbIp->ListUrbsLanded, USBPROXYURBUSBIP, NodeList);
    else
        pUrbUsbIp = usbProxyUsbIpPollWorker(pProxyDevUsbIp, 0, true /*fPollWakePipe*/, cMillies);

    if (pUrbUsbIp)
    {
        pUrb = pUrbUsbIp->pVUsbUrb;
        pUrb->enmStatus = pUrbUsbIp->enmStatus;

        /* unlink from the pending delivery list */
        usbProxyUsbIpUnlinkUrb(pProxyDevUsbIp, pUrbUsbIp);
        usbProxyUsbIpUrbFree(pProxyDevUsbIp, pUrbUsbIp);
    }

    return pUrb;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnUrbCancel}
 */
static DECLCALLBACK(int) usbProxyUsbIpUrbCancel(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    LogFlowFunc(("pUrb=%p\n", pUrb));

    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    PUSBPROXYURBUSBIP pUrbUsbIp = (PUSBPROXYURBUSBIP)pUrb->Dev.pvPrivate;
    UsbIpReqUnlink ReqUnlink;

    RT_ZERO(ReqUnlink);

    uint32_t u32SeqNum = usbProxyUsbIpSeqNumGet(pProxyDevUsbIp);
    ReqUnlink.Hdr.u32ReqRet           = USBIP_CMD_UNLINK;
    ReqUnlink.Hdr.u32SeqNum           = u32SeqNum;
    ReqUnlink.Hdr.u32DevId            = pProxyDevUsbIp->u32DevId;
    ReqUnlink.Hdr.u32Direction        = USBIP_DIR_OUT;
    ReqUnlink.Hdr.u32Endpoint         = pUrb->EndPt;
    ReqUnlink.u32SeqNum               = pUrbUsbIp->u32SeqNumUrb;

    usbProxyUsbIpReqUnlinkH2N(&ReqUnlink);
    int rc = RTTcpWrite(pProxyDevUsbIp->hSocket, &ReqUnlink, sizeof(ReqUnlink));
    if (RT_SUCCESS(rc))
    {
        pUrbUsbIp->u32SeqNumUrbUnlink = u32SeqNum;
        pUrbUsbIp->fCancelled         = true;
    }

    return rc;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnWakeup}
 */
static DECLCALLBACK(int) usbProxyUsbIpWakeup(PUSBPROXYDEV pProxyDev)
{
    LogFlowFunc(("pProxyDev=%s\n", pProxyDev->pUsbIns->pszName));

    PUSBPROXYDEVUSBIP pProxyDevUsbIp = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVUSBIP);
    return usbProxyReaperKick(pProxyDevUsbIp, USBIP_REAPER_WAKEUP_REASON_EXTERNAL);
}


/**
 * The USB/IP USB Proxy Backend operations.
 */
extern const USBPROXYBACK g_USBProxyDeviceUsbIp =
{
    /* pszName */
    "usbip",
    /* cbBackend */
    sizeof(USBPROXYDEVUSBIP),
    usbProxyUsbIpOpen,
    NULL,
    usbProxyUsbIpClose,
    usbProxyUsbIpReset,
    usbProxyUsbIpSetConfig,
    usbProxyUsbIpClaimInterface,
    usbProxyUsbIpReleaseInterface,
    usbProxyUsbIpSetInterface,
    usbProxyUsbIpClearHaltedEp,
    usbProxyUsbIpUrbQueue,
    usbProxyUsbIpUrbCancel,
    usbProxyUsbIpUrbReap,
    usbProxyUsbIpWakeup,
    0
};
