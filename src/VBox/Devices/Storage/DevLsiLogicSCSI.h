/* $Id: DevLsiLogicSCSI.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: LsiLogic LSI53c1030 SCSI controller - Defines and structures.
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

#ifndef VBOX_INCLUDED_SRC_Storage_DevLsiLogicSCSI_h
#define VBOX_INCLUDED_SRC_Storage_DevLsiLogicSCSI_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/stdint.h>

/*
 * Custom fixed I/O ports for BIOS controller access. Note that these should
 * not be in the ISA range (below 400h) to avoid conflicts with ISA device
 * probing. Addresses in the 300h-340h range should be especially avoided.
 */
#define LSILOGIC_BIOS_IO_PORT       0x434
#define LSILOGIC_SAS_BIOS_IO_PORT   0x438

#define LSILOGICSCSI_REQUEST_QUEUE_DEPTH_MIN        8       /**< (bird just picked this out thin air) */
#define LSILOGICSCSI_REQUEST_QUEUE_DEPTH_MAX        1024    /**< (bird just picked this out thin air) */
#define LSILOGICSCSI_REQUEST_QUEUE_DEPTH_DEFAULT    256

#define LSILOGICSCSI_REPLY_QUEUE_DEPTH_MIN          8       /**< (bird just picked this out thin air) */
#define LSILOGICSCSI_REPLY_QUEUE_DEPTH_MAX          1024    /**< (bird just picked this out thin air) */
#define LSILOGICSCSI_REPLY_QUEUE_DEPTH_DEFAULT      256

#define LSILOGICSCSI_MAXIMUM_CHAIN_DEPTH 3

#define LSILOGIC_NR_OF_ALLOWED_BIGGER_LISTS 100

/** Equal for all devices */
#define LSILOGICSCSI_PCI_VENDOR_ID            (0x1000)

/** SPI SCSI controller (LSI53C1030) */
#define LSILOGICSCSI_PCI_SPI_CTRLNAME             "LSI53C1030"
#define LSILOGICSCSI_PCI_SPI_DEVICE_ID            (0x0030)
#define LSILOGICSCSI_PCI_SPI_REVISION_ID          (0x00)
#define LSILOGICSCSI_PCI_SPI_CLASS_CODE           (0x01)
#define LSILOGICSCSI_PCI_SPI_SUBSYSTEM_VENDOR_ID  (0x1000)
#define LSILOGICSCSI_PCI_SPI_SUBSYSTEM_ID         (0x8000)
#define LSILOGICSCSI_PCI_SPI_PORTS_MAX 1
#define LSILOGICSCSI_PCI_SPI_BUSES_MAX 1
#define LSILOGICSCSI_PCI_SPI_DEVICES_PER_BUS_MAX 16
#define LSILOGICSCSI_PCI_SPI_DEVICES_MAX (LSILOGICSCSI_PCI_SPI_BUSES_MAX*LSILOGICSCSI_PCI_SPI_DEVICES_PER_BUS_MAX)

/** SAS SCSI controller (SAS1068 PCI-X Fusion-MPT SAS) */
#define LSILOGICSCSI_PCI_SAS_CTRLNAME             "SAS1068"
#define LSILOGICSCSI_PCI_SAS_DEVICE_ID            (0x0054)
#define LSILOGICSCSI_PCI_SAS_REVISION_ID          (0x00)
#define LSILOGICSCSI_PCI_SAS_CLASS_CODE           (0x00)
#define LSILOGICSCSI_PCI_SAS_SUBSYSTEM_VENDOR_ID  (0x1000)
#define LSILOGICSCSI_PCI_SAS_SUBSYSTEM_ID         (0x8000)
#define LSILOGICSCSI_PCI_SAS_PORTS_MAX             256
#define LSILOGICSCSI_PCI_SAS_PORTS_DEFAULT           8
#define LSILOGICSCSI_PCI_SAS_DEVICES_PER_PORT_MAX    1
#define LSILOGICSCSI_PCI_SAS_DEVICES_MAX          (LSILOGICSCSI_PCI_SAS_PORTS_MAX * LSILOGICSCSI_PCI_SAS_DEVICES_PER_PORT_MAX)

/**
 * A SAS address.
 */
typedef union SASADDRESS
{
    /** 64bit view. */
    uint64_t    u64Address;
    /** 32bit view. */
    uint32_t    u32Address[2];
    /** 16bit view. */
    uint16_t    u16Address[4];
    /** Byte view. */
    uint8_t     u8Address[8];
} SASADDRESS, *PSASADDRESS;
AssertCompileSize(SASADDRESS, 8);

/**
 * Possible device types we support.
 */
typedef enum LSILOGICCTRLTYPE
{
    /** SPI SCSI controller (PCI dev id 0x0030) */
    LSILOGICCTRLTYPE_SCSI_SPI = 0,
    /** SAS SCSI controller (PCI dev id 0x0054) */
    LSILOGICCTRLTYPE_SCSI_SAS = 1,
    /** 32bit hack */
    LSILOGICCTRLTYPE_32BIT_HACK = 0x7fffffff
} LSILOGICCTRLTYPE, *PLSILOGICCTRLTYPE;

/**
 * A simple SG element for a 64bit address.
 */
typedef struct MptSGEntrySimple64
{
    /** Length of the buffer this entry describes. */
    unsigned u24Length:          24;
    /** Flag whether this element is the end of the list. */
    unsigned fEndOfList:          1;
    /** Flag whether the address is 32bit or 64bits wide. */
    unsigned f64BitAddress:       1;
    /** Flag whether this buffer contains data to be transferred or is the destination. */
    unsigned fBufferContainsData: 1;
    /** Flag whether this is a local address or a system address. */
    unsigned fLocalAddress:       1;
    /** Element type. */
    unsigned u2ElementType:       2;
    /** Flag whether this is the last element of the buffer. */
    unsigned fEndOfBuffer:        1;
    /** Flag whether this is the last element of the current segment. */
    unsigned fLastElement:        1;
    /** Lower 32bits of the address of the data buffer. */
    unsigned u32DataBufferAddressLow: 32;
    /** Upper 32bits of the address of the data buffer. */
    unsigned u32DataBufferAddressHigh: 32;
} MptSGEntrySimple64, *PMptSGEntrySimple64;
AssertCompileSize(MptSGEntrySimple64, 12);

/**
 * A simple SG element for a 32bit address.
 */
typedef struct MptSGEntrySimple32
{
    /** Length of the buffer this entry describes. */
    unsigned u24Length:          24;
    /** Flag whether this element is the end of the list. */
    unsigned fEndOfList:          1;
    /** Flag whether the address is 32bit or 64bits wide. */
    unsigned f64BitAddress:       1;
    /** Flag whether this buffer contains data to be transferred or is the destination. */
    unsigned fBufferContainsData: 1;
    /** Flag whether this is a local address or a system address. */
    unsigned fLocalAddress:       1;
    /** Element type. */
    unsigned u2ElementType:       2;
    /** Flag whether this is the last element of the buffer. */
    unsigned fEndOfBuffer:        1;
    /** Flag whether this is the last element of the current segment. */
    unsigned fLastElement:        1;
    /** Lower 32bits of the address of the data buffer. */
    unsigned u32DataBufferAddressLow: 32;
} MptSGEntrySimple32, *PMptSGEntrySimple32;
AssertCompileSize(MptSGEntrySimple32, 8);

/**
 * A chain SG element.
 */
typedef struct MptSGEntryChain
{
    /** Size of the segment. */
    unsigned u16Length: 16;
    /** Offset in 32bit words of the next chain element in the segment
     *  identified by this element. */
    unsigned u8NextChainOffset: 8;
    /** Reserved. */
    unsigned fReserved0:    1;
    /** Flag whether the address is 32bit or 64bits wide. */
    unsigned f64BitAddress: 1;
    /** Reserved. */
    unsigned fReserved1:    1;
    /** Flag whether this is a local address or a system address. */
    unsigned fLocalAddress: 1;
    /** Element type. */
    unsigned u2ElementType: 2;
    /** Flag whether this is the last element of the buffer. */
    unsigned u2Reserved2:   2;
    /** Lower 32bits of the address of the data buffer. */
    unsigned u32SegmentAddressLow: 32;
    /** Upper 32bits of the address of the data buffer. */
    unsigned u32SegmentAddressHigh: 32;
} MptSGEntryChain, *PMptSGEntryChain;
AssertCompileSize(MptSGEntryChain, 12);

typedef union MptSGEntryUnion
{
    MptSGEntrySimple64 Simple64;
    MptSGEntrySimple32 Simple32;
    MptSGEntryChain    Chain;
} MptSGEntryUnion, *PMptSGEntryUnion;

/**
 * MPT Fusion message header - Common for all message frames.
 * This is filled in by the guest.
 */
typedef struct MptMessageHdr
{
    /** Function dependent data. */
    uint16_t    u16FunctionDependent;
    /** Chain offset. */
    uint8_t     u8ChainOffset;
    /** The function code. */
    uint8_t     u8Function;
    /** Function dependent data. */
    uint8_t     au8FunctionDependent[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context - Unique ID from the guest unmodified by the device. */
    uint32_t    u32MessageContext;
} MptMessageHdr, *PMptMessageHdr;
AssertCompileSize(MptMessageHdr, 12);

/** Defined function codes found in the message header. */
#define MPT_MESSAGE_HDR_FUNCTION_SCSI_IO_REQUEST        (0x00)
#define MPT_MESSAGE_HDR_FUNCTION_SCSI_TASK_MGMT         (0x01)
#define MPT_MESSAGE_HDR_FUNCTION_IOC_INIT               (0x02)
#define MPT_MESSAGE_HDR_FUNCTION_IOC_FACTS              (0x03)
#define MPT_MESSAGE_HDR_FUNCTION_CONFIG                 (0x04)
#define MPT_MESSAGE_HDR_FUNCTION_PORT_FACTS             (0x05)
#define MPT_MESSAGE_HDR_FUNCTION_PORT_ENABLE            (0x06)
#define MPT_MESSAGE_HDR_FUNCTION_EVENT_NOTIFICATION     (0x07)
#define MPT_MESSAGE_HDR_FUNCTION_EVENT_ACK              (0x08)
#define MPT_MESSAGE_HDR_FUNCTION_FW_DOWNLOAD            (0x09)
#define MPT_MESSAGE_HDR_FUNCTION_TARGET_CMD_BUFFER_POST (0x0A)
#define MPT_MESSAGE_HDR_FUNCTION_TARGET_ASSIST          (0x0B)
#define MPT_MESSAGE_HDR_FUNCTION_TARGET_STATUS_SEND     (0x0C)
#define MPT_MESSAGE_HDR_FUNCTION_TARGET_MODE_ABORT      (0x0D)
#define MPT_MESSAGE_HDR_FUNCTION_FW_UPLOAD              (0x12)

#ifdef LOG_ENABLED
/**
 * Function names
 */
static const char * const g_apszMPTFunctionNames[] =
{
    "SCSI I/O Request",
    "SCSI Task Management",
    "IOC Init",
    "IOC Facts",
    "Config",
    "Port Facts",
    "Port Enable",
    "Event Notification",
    "Event Ack",
    "Firmware Download"
};
#endif

/**
 * Default reply message.
 * Send from the device to the guest upon completion of a request.
 */
typedef struct MptDefaultReplyMessage
{
    /** Function dependent data. */
    uint16_t    u16FunctionDependent;
    /** Length of the message in 32bit DWords. */
    uint8_t     u8MessageLength;
    /** Function which completed. */
    uint8_t     u8Function;
    /** Function dependent. */
    uint8_t     au8FunctionDependent[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context given in the request. */
    uint32_t    u32MessageContext;
    /** Function dependent status code. */
    uint16_t    u16FunctionDependentStatus;
    /** Status of the IOC. */
    uint16_t    u16IOCStatus;
    /** Additional log info. */
    uint32_t    u32IOCLogInfo;
} MptDefaultReplyMessage, *PMptDefaultReplyMessage;
AssertCompileSize(MptDefaultReplyMessage, 20);

/**
 * IO controller init request.
 */
typedef struct MptIOCInitRequest
{
    /** Which system send this init request. */
    uint8_t     u8WhoInit;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Chain offset in the SG list. */
    uint8_t     u8ChainOffset;
    /** Function to execute. */
    uint8_t     u8Function;
    /** Flags */
    uint8_t     u8Flags;
    /** Maximum number of devices the driver can handle. */
    uint8_t     u8MaxDevices;
    /** Maximum number of buses the driver can handle. */
    uint8_t     u8MaxBuses;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** Reply frame size. */
    uint16_t    u16ReplyFrameSize;
    /** Reserved */
    uint16_t    u16Reserved;
    /** Upper 32bit part of the 64bit address the message frames are in.
     *  That means all frames must be in the same 4GB segment. */
    uint32_t    u32HostMfaHighAddr;
    /** Upper 32bit of the sense buffer. */
    uint32_t    u32SenseBufferHighAddr;
} MptIOCInitRequest, *PMptIOCInitRequest;
AssertCompileSize(MptIOCInitRequest, 24);

/**
 * IO controller init reply.
 */
typedef struct MptIOCInitReply
{
    /** Which subsystem send this init request. */
    uint8_t     u8WhoInit;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Message length */
    uint8_t     u8MessageLength;
    /** Function. */
    uint8_t     u8Function;
    /** Flags */
    uint8_t     u8Flags;
    /** Maximum number of devices the driver can handle. */
    uint8_t     u8MaxDevices;
    /** Maximum number of busses the driver can handle. */
    uint8_t     u8MaxBuses;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID */
    uint32_t    u32MessageContext;
    /** Reserved */
    uint16_t    u16Reserved;
    /** IO controller status. */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
} MptIOCInitReply, *PMptIOCInitReply;
AssertCompileSize(MptIOCInitReply, 20);

/**
 * IO controller facts request.
 */
typedef struct MptIOCFactsRequest
{
    /** Reserved. */
    uint16_t    u16Reserved;
    /** Chain offset in SG list. */
    uint8_t     u8ChainOffset;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved */
    uint8_t     u8Reserved[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
} MptIOCFactsRequest, *PMptIOCFactsRequest;
AssertCompileSize(MptIOCFactsRequest, 12);

/**
 * IO controller facts reply.
 */
typedef struct MptIOCFactsReply
{
    /** Message version. */
    uint16_t    u16MessageVersion;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved */
    uint16_t    u16Reserved1;
    /** IO controller number */
    uint8_t     u8IOCNumber;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** IO controller exceptions */
    uint16_t    u16IOCExceptions;
    /** IO controller status. */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
    /** Maximum chain depth. */
    uint8_t     u8MaxChainDepth;
    /** The current value of the WhoInit field. */
    uint8_t     u8WhoInit;
    /** Block size. */
    uint8_t     u8BlockSize;
    /** Flags. */
    uint8_t     u8Flags;
    /** Depth of the reply queue. */
    uint16_t    u16ReplyQueueDepth;
    /** Size of a request frame. */
    uint16_t    u16RequestFrameSize;
    /** Reserved */
    uint16_t    u16Reserved2;
    /** Product ID. */
    uint16_t    u16ProductID;
    /** Current value of the high 32bit MFA address. */
    uint32_t    u32CurrentHostMFAHighAddr;
    /** Global credits - Number of entries allocated to queues */
    uint16_t    u16GlobalCredits;
    /** Number of ports on the IO controller */
    uint8_t     u8NumberOfPorts;
    /** Event state. */
    uint8_t     u8EventState;
    /** Current value of the high 32bit sense buffer address. */
    uint32_t    u32CurrentSenseBufferHighAddr;
    /** Current reply frame size. */
    uint16_t    u16CurReplyFrameSize;
    /** Maximum number of devices. */
    uint8_t     u8MaxDevices;
    /** Maximum number of buses. */
    uint8_t     u8MaxBuses;
    /** Size of the firmware image. */
    uint32_t    u32FwImageSize;
    /** Reserved. */
    uint32_t    u32Reserved;
    /** Firmware version */
    uint32_t    u32FWVersion;
} MptIOCFactsReply, *PMptIOCFactsReply;
AssertCompileSize(MptIOCFactsReply, 60);

/**
 * Port facts request
 */
typedef struct MptPortFactsRequest
{
    /** Reserved */
    uint16_t    u16Reserved1;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved */
    uint16_t    u16Reserved2;
    /** Port number to get facts for. */
    uint8_t     u8PortNumber;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
} MptPortFactsRequest, *PMptPortFactsRequest;
AssertCompileSize(MptPortFactsRequest, 12);

/**
 * Port facts reply.
 */
typedef struct MptPortFactsReply
{
    /** Reserved. */
    uint16_t    u16Reserved1;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved */
    uint16_t    u16Reserved2;
    /** Port number the facts are for. */
    uint8_t     u8PortNumber;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** Reserved. */
    uint16_t    u16Reserved3;
    /** IO controller status. */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Port type */
    uint8_t     u8PortType;
    /** Maximum number of devices on this port. */
    uint16_t    u16MaxDevices;
    /** SCSI ID of this port on the attached bus. */
    uint16_t    u16PortSCSIID;
    /** Protocol flags. */
    uint16_t    u16ProtocolFlags;
    /** Maximum number of target command buffers which can be posted to this port at a time. */
    uint16_t    u16MaxPostedCmdBuffers;
    /** Maximum number of target IDs that remain persistent between power/reset cycles. */
    uint16_t    u16MaxPersistentIDs;
    /** Maximum number of LAN buckets. */
    uint16_t    u16MaxLANBuckets;
    /** Reserved. */
    uint16_t    u16Reserved4;
    /** Reserved. */
    uint32_t    u32Reserved;
} MptPortFactsReply, *PMptPortFactsReply;
AssertCompileSize(MptPortFactsReply, 40);

/**
 * Port Enable request.
 */
typedef struct MptPortEnableRequest
{
    /** Reserved. */
    uint16_t    u16Reserved1;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved. */
    uint16_t    u16Reserved2;
    /** Port number to enable. */
    uint8_t     u8PortNumber;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
} MptPortEnableRequest, *PMptPortEnableRequest;
AssertCompileSize(MptPortEnableRequest, 12);

/**
 * Port enable reply.
 */
typedef struct MptPortEnableReply
{
    /** Reserved. */
    uint16_t    u16Reserved1;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved */
    uint16_t    u16Reserved2;
    /** Port number which was enabled. */
    uint8_t     u8PortNumber;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** Reserved. */
    uint16_t    u16Reserved3;
    /** IO controller status */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
} MptPortEnableReply, *PMptPortEnableReply;
AssertCompileSize(MptPortEnableReply, 20);

/**
 * Event notification request.
 */
typedef struct MptEventNotificationRequest
{
    /** Switch - Turns event notification on and off. */
    uint8_t     u8Switch;
    /** Reserved. */
    uint8_t     u8Reserved1;
    /** Chain offset. */
    uint8_t     u8ChainOffset;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved. */
    uint8_t     u8reserved2[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
} MptEventNotificationRequest, *PMptEventNotificationRequest;
AssertCompileSize(MptEventNotificationRequest, 12);

/**
 * Event notification reply.
 */
typedef struct MptEventNotificationReply
{
    /** Event data length. */
    uint16_t    u16EventDataLength;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved. */
    uint16_t    u16Reserved1;
    /** Ack required. */
    uint8_t     u8AckRequired;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** Reserved. */
    uint16_t    u16Reserved2;
    /** IO controller status. */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
    /** Notification event. */
    uint32_t    u32Event;
    /** Event context. */
    uint32_t    u32EventContext;
    /** Event data. */
    uint32_t    u32EventData;
} MptEventNotificationReply, *PMptEventNotificationReply;
AssertCompileSize(MptEventNotificationReply, 32);

#define MPT_EVENT_EVENT_CHANGE (0x0000000a)

/**
 * FW download request.
 */
typedef struct MptFWDownloadRequest
{
    /** Switch - Turns event notification on and off. */
    uint8_t     u8ImageType;
    /** Reserved. */
    uint8_t     u8Reserved1;
    /** Chain offset. */
    uint8_t     u8ChainOffset;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved. */
    uint8_t     u8Reserved2[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
} MptFWDownloadRequest, *PMptFWDownloadRequest;
AssertCompileSize(MptFWDownloadRequest, 12);

#define MPT_FW_DOWNLOAD_REQUEST_IMAGE_TYPE_RESERVED 0
#define MPT_FW_DOWNLOAD_REQUEST_IMAGE_TYPE_FIRMWARE 1
#define MPT_FW_DOWNLOAD_REQUEST_IMAGE_TYPE_MPI_BIOS 2
#define MPT_FW_DOWNLOAD_REQUEST_IMAGE_TYPE_NVDATA   3

/**
 * FW download reply.
 */
typedef struct MptFWDownloadReply
{
    /** Reserved. */
    uint16_t    u16Reserved1;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved. */
    uint8_t     u8Reserved2[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** Reserved. */
    uint16_t    u16Reserved2;
    /** IO controller status. */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
} MptFWDownloadReply, *PMptFWDownloadReply;
AssertCompileSize(MptFWDownloadReply, 20);

/**
 * FW upload request.
 */
typedef struct MptFWUploadRequest
{
    /** Requested image type. */
    uint8_t     u8ImageType;
    /** Reserved. */
    uint8_t     u8Reserved1;
    /** Chain offset. */
    uint8_t     u8ChainOffset;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved. */
    uint8_t     u8Reserved2[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
} MptFWUploadRequest, *PMptFWUploadRequest;
AssertCompileSize(MptFWUploadRequest, 12);

/**
 * FW upload reply.
 */
typedef struct MptFWUploadReply
{
    /** Image type. */
    uint8_t     u8ImageType;
    /** Reserved. */
    uint8_t     u8Reserved1;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** Reserved. */
    uint8_t     u8Reserved2[3];
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** Reserved. */
    uint16_t    u16Reserved2;
    /** IO controller status. */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
    /** Uploaded image size. */
    uint32_t    u32ActualImageSize;
} MptFWUploadReply, *PMptFWUploadReply;
AssertCompileSize(MptFWUploadReply, 24);

/**
 * SCSI IO Request
 */
typedef struct MptSCSIIORequest
{
    /** Target ID */
    uint8_t     u8TargetID;
    /** Bus number */
    uint8_t     u8Bus;
    /** Chain offset */
    uint8_t     u8ChainOffset;
    /** Function number. */
    uint8_t     u8Function;
    /** CDB length. */
    uint8_t     u8CDBLength;
    /** Sense buffer length. */
    uint8_t     u8SenseBufferLength;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** LUN */
    uint8_t     au8LUN[8];
    /** Control values. */
    uint32_t    u32Control;
    /** The CDB. */
    uint8_t     au8CDB[16];
    /** Data length. */
    uint32_t    u32DataLength;
    /** Sense buffer low 32bit address. */
    uint32_t    u32SenseBufferLowAddress;
} MptSCSIIORequest, *PMptSCSIIORequest;
AssertCompileSize(MptSCSIIORequest, 48);

#define MPT_SCSIIO_REQUEST_CONTROL_TXDIR_GET(x) (((x) & 0x3000000) >> 24)
#define MPT_SCSIIO_REQUEST_CONTROL_TXDIR_NONE  (0x0)
#define MPT_SCSIIO_REQUEST_CONTROL_TXDIR_WRITE (0x1)
#define MPT_SCSIIO_REQUEST_CONTROL_TXDIR_READ  (0x2)

/**
 * SCSI IO error reply.
 */
typedef struct MptSCSIIOErrorReply
{
    /** Target ID */
    uint8_t     u8TargetID;
    /** Bus number */
    uint8_t     u8Bus;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** CDB length */
    uint8_t     u8CDBLength;
    /** Sense buffer length */
    uint8_t     u8SenseBufferLength;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Message flags */
    uint8_t     u8MessageFlags;
    /** Message context ID */
    uint32_t    u32MessageContext;
    /** SCSI status. */
    uint8_t     u8SCSIStatus;
    /** SCSI state */
    uint8_t     u8SCSIState;
    /** IO controller status */
    uint16_t    u16IOCStatus;
    /** IO controller log information */
    uint32_t    u32IOCLogInfo;
    /** Transfer count */
    uint32_t    u32TransferCount;
    /** Sense count */
    uint32_t    u32SenseCount;
    /** Response information */
    uint32_t    u32ResponseInfo;
} MptSCSIIOErrorReply, *PMptSCSIIOErrorReply;
AssertCompileSize(MptSCSIIOErrorReply, 32);

#define MPT_SCSI_IO_ERROR_SCSI_STATE_AUTOSENSE_VALID (0x01)
#define MPT_SCSI_IO_ERROR_SCSI_STATE_TERMINATED      (0x08)

/**
 * IOC status codes specific to the SCSI I/O error reply.
 */
#define MPT_SCSI_IO_ERROR_IOCSTATUS_INVALID_BUS      (0x0041)
#define MPT_SCSI_IO_ERROR_IOCSTATUS_INVALID_TARGETID (0x0042)
#define MPT_SCSI_IO_ERROR_IOCSTATUS_DEVICE_NOT_THERE (0x0043)

/**
 * SCSI task management request.
 */
typedef struct MptSCSITaskManagementRequest
{
    /** Target ID */
    uint8_t     u8TargetID;
    /** Bus number */
    uint8_t     u8Bus;
    /** Chain offset */
    uint8_t     u8ChainOffset;
    /** Function number */
    uint8_t     u8Function;
    /** Reserved */
    uint8_t     u8Reserved1;
    /** Task type */
    uint8_t     u8TaskType;
    /** Reserved */
    uint8_t     u8Reserved2;
    /** Message flags */
    uint8_t     u8MessageFlags;
    /** Message context ID */
    uint32_t    u32MessageContext;
    /** LUN */
    uint8_t     au8LUN[8];
    /** Reserved */
    uint8_t     auReserved[28];
    /** Task message context ID. */
    uint32_t    u32TaskMessageContext;
} MptSCSITaskManagementRequest, *PMptSCSITaskManagementRequest;
AssertCompileSize(MptSCSITaskManagementRequest, 52);

/**
 * SCSI task management reply.
 */
typedef struct MptSCSITaskManagementReply
{
    /** Target ID */
    uint8_t     u8TargetID;
    /** Bus number */
    uint8_t     u8Bus;
    /** Message length */
    uint8_t     u8MessageLength;
    /** Function number */
    uint8_t     u8Function;
    /** Reserved */
    uint8_t     u8Reserved1;
    /** Task type */
    uint8_t     u8TaskType;
    /** Reserved */
    uint8_t     u8Reserved2;
    /** Message flags */
    uint8_t     u8MessageFlags;
    /** Message context ID */
    uint32_t    u32MessageContext;
    /** Reserved */
    uint16_t    u16Reserved;
    /** IO controller status */
    uint16_t    u16IOCStatus;
    /** IO controller log information */
    uint32_t    u32IOCLogInfo;
    /** Termination count */
    uint32_t    u32TerminationCount;
} MptSCSITaskManagementReply, *PMptSCSITaskManagementReply;
AssertCompileSize(MptSCSITaskManagementReply, 24);

/**
 * Page address for SAS expander page types.
 */
typedef union MptConfigurationPageAddressSASExpander
{
    struct
    {
        uint16_t    u16Handle;
        uint16_t    u16Reserved;
    } Form0And2;
    struct
    {
        uint16_t    u16Handle;
        uint8_t     u8PhyNum;
        uint8_t     u8Reserved;
    } Form1;
} MptConfigurationPageAddressSASExpander, *PMptConfigurationPageAddressSASExpander;
AssertCompileSize(MptConfigurationPageAddressSASExpander, 4);

/**
 * Page address for SAS device page types.
 */
typedef union MptConfigurationPageAddressSASDevice
{
    struct
    {
        uint16_t    u16Handle;
        uint16_t    u16Reserved;
    } Form0And2;
    struct
    {
        uint8_t     u8TargetID;
        uint8_t     u8Bus;
        uint8_t     u8Reserved;
    } Form1; /**< r=bird: only three bytes? */
} MptConfigurationPageAddressSASDevice, *PMptConfigurationPageAddressSASDevice;
AssertCompileSize(MptConfigurationPageAddressSASDevice, 4);

/**
 * Page address for SAS PHY page types.
 */
typedef union MptConfigurationPageAddressSASPHY
{
    struct
    {
        uint8_t     u8PhyNumber;
        uint8_t     u8Reserved[3];
    } Form0;
    struct
    {
        uint16_t    u16Index;
        uint16_t    u16Reserved;
    } Form1;
} MptConfigurationPageAddressSASPHY, *PMptConfigurationPageAddressSASPHY;
AssertCompileSize(MptConfigurationPageAddressSASPHY, 4);

/**
 * Page address for SAS Enclosure page types.
 */
typedef struct MptConfigurationPageAddressSASEnclosure
{
    uint16_t    u16Handle;
    uint16_t    u16Reserved;
} MptConfigurationPageAddressSASEnclosure, *PMptConfigurationPageAddressSASEnclosure;
AssertCompileSize(MptConfigurationPageAddressSASEnclosure, 4);

/**
 * Union of all possible address types.
 */
typedef union MptConfigurationPageAddress
{
    /** 32bit view. */
    uint32_t u32PageAddress;
    struct
    {
        /** Port number to get the configuration page for. */
        uint8_t u8PortNumber;
        /** Reserved. */
        uint8_t u8Reserved[3];
    } MPIPortNumber;
    struct
    {
        /** Target ID to get the configuration page for. */
        uint8_t u8TargetID;
        /** Bus number to get the configuration page for. */
        uint8_t u8Bus;
        /** Reserved. */
        uint8_t u8Reserved[2];
    } BusAndTargetId;
    MptConfigurationPageAddressSASExpander  SASExpander;
    MptConfigurationPageAddressSASDevice    SASDevice;
    MptConfigurationPageAddressSASPHY       SASPHY;
    MptConfigurationPageAddressSASEnclosure SASEnclosure;
} MptConfigurationPageAddress, *PMptConfigurationPageAddress;
AssertCompileSize(MptConfigurationPageAddress, 4);

#define MPT_CONFIGURATION_PAGE_ADDRESS_GET_SAS_FORM(x) (((x).u32PageAddress >> 28) & 0x0f)

/**
 * Configuration request
 */
typedef struct MptConfigurationRequest
{
    /** Action code. */
    uint8_t    u8Action;
    /** Reserved. */
    uint8_t    u8Reserved1;
    /** Chain offset. */
    uint8_t    u8ChainOffset;
    /** Function number. */
    uint8_t    u8Function;
    /** Extended page length. */
    uint16_t   u16ExtPageLength;
    /** Extended page type */
    uint8_t    u8ExtPageType;
    /** Message flags. */
    uint8_t    u8MessageFlags;
    /** Message context ID. */
    uint32_t   u32MessageContext;
    /** Reserved. */
    uint8_t    u8Reserved2[8];
    /** Version number of the page. */
    uint8_t    u8PageVersion;
    /** Length of the page in 32bit Dwords. */
    uint8_t    u8PageLength;
    /** Page number to access. */
    uint8_t    u8PageNumber;
    /** Type of the page being accessed. */
    uint8_t    u8PageType;
    /** Page type dependent address. */
    MptConfigurationPageAddress PageAddress;
    /** Simple SG element describing the buffer. */
    MptSGEntrySimple64          SimpleSGElement;
} MptConfigurationRequest, *PMptConfigurationRequest;
AssertCompileSize(MptConfigurationRequest, 40);

/** Possible action codes. */
#define MPT_CONFIGURATION_REQUEST_ACTION_HEADER        (0x00)
#define MPT_CONFIGURATION_REQUEST_ACTION_READ_CURRENT  (0x01)
#define MPT_CONFIGURATION_REQUEST_ACTION_WRITE_CURRENT (0x02)
#define MPT_CONFIGURATION_REQUEST_ACTION_DEFAULT       (0x03)
#define MPT_CONFIGURATION_REQUEST_ACTION_WRITE_NVRAM   (0x04)
#define MPT_CONFIGURATION_REQUEST_ACTION_READ_DEFAULT  (0x05)
#define MPT_CONFIGURATION_REQUEST_ACTION_READ_NVRAM    (0x06)

/** Page type codes. */
#define MPT_CONFIGURATION_REQUEST_PAGE_TYPE_IO_UNIT    (0x00)
#define MPT_CONFIGURATION_REQUEST_PAGE_TYPE_IOC        (0x01)
#define MPT_CONFIGURATION_REQUEST_PAGE_TYPE_BIOS       (0x02)
#define MPT_CONFIGURATION_REQUEST_PAGE_TYPE_SCSI_PORT  (0x03)
#define MPT_CONFIGURATION_REQUEST_PAGE_TYPE_EXTENDED   (0x0F)

/**
 * Configuration reply.
 */
typedef struct MptConfigurationReply
{
    /** Action code. */
    uint8_t    u8Action;
    /** Reserved. */
    uint8_t    u8Reserved;
    /** Message length. */
    uint8_t    u8MessageLength;
    /** Function number. */
    uint8_t    u8Function;
    /** Extended page length. */
    uint16_t   u16ExtPageLength;
    /** Extended page type */
    uint8_t    u8ExtPageType;
    /** Message flags. */
    uint8_t    u8MessageFlags;
    /** Message context ID. */
    uint32_t   u32MessageContext;
    /** Reserved. */
    uint16_t   u16Reserved;
    /** I/O controller status. */
    uint16_t   u16IOCStatus;
    /** I/O controller log information. */
    uint32_t   u32IOCLogInfo;
    /** Version number of the page. */
    uint8_t    u8PageVersion;
    /** Length of the page in 32bit Dwords. */
    uint8_t    u8PageLength;
    /** Page number to access. */
    uint8_t    u8PageNumber;
    /** Type of the page being accessed. */
    uint8_t    u8PageType;
} MptConfigurationReply, *PMptConfigurationReply;
AssertCompileSize(MptConfigurationReply, 24);

/** Additional I/O controller status codes for the configuration reply. */
#define MPT_IOCSTATUS_CONFIG_INVALID_ACTION (0x0020)
#define MPT_IOCSTATUS_CONFIG_INVALID_TYPE   (0x0021)
#define MPT_IOCSTATUS_CONFIG_INVALID_PAGE   (0x0022)
#define MPT_IOCSTATUS_CONFIG_INVALID_DATA   (0x0023)
#define MPT_IOCSTATUS_CONFIG_NO_DEFAULTS    (0x0024)
#define MPT_IOCSTATUS_CONFIG_CANT_COMMIT    (0x0025)

/**
 * Union of all possible request messages.
 */
typedef union MptRequestUnion
{
    MptMessageHdr                Header;
    MptIOCInitRequest            IOCInit;
    MptIOCFactsRequest           IOCFacts;
    MptPortFactsRequest          PortFacts;
    MptPortEnableRequest         PortEnable;
    MptEventNotificationRequest  EventNotification;
    MptSCSIIORequest             SCSIIO;
    MptSCSITaskManagementRequest SCSITaskManagement;
    MptConfigurationRequest      Configuration;
    MptFWDownloadRequest         FWDownload;
    MptFWUploadRequest           FWUpload;
} MptRequestUnion, *PMptRequestUnion;

/**
 * Union of all possible reply messages.
 */
typedef union MptReplyUnion
{
    /** 16bit view. */
    uint16_t                   au16Reply[30];
    MptDefaultReplyMessage     Header;
    MptIOCInitReply            IOCInit;
    MptIOCFactsReply           IOCFacts;
    MptPortFactsReply          PortFacts;
    MptPortEnableReply         PortEnable;
    MptEventNotificationReply  EventNotification;
    MptSCSIIOErrorReply        SCSIIOError;
    MptSCSITaskManagementReply SCSITaskManagement;
    MptConfigurationReply      Configuration;
    MptFWDownloadReply         FWDownload;
    MptFWUploadReply           FWUpload;
} MptReplyUnion, *PMptReplyUnion;
AssertCompileSize(MptReplyUnion, 60);

/**
 * Firmware image header.
 */
typedef struct FwImageHdr
{
    /** ARM branch instruction. */
    uint32_t    u32ArmBrInsn;
    /** Signature part 1. */
    uint32_t    u32Signature1;
    /** Signature part 2. */
    uint32_t    u32Signature2;
    /** Signature part 3. */
    uint32_t    u32Signature3;
    /** Another ARM branch instruction. */
    uint32_t    u32ArmBrInsn2;
    /** Yet another ARM branch instruction. */
    uint32_t    u32ArmBrInsn3;
    /** Reserved. */
    uint32_t    u32Reserved;
    /** Checksum of the image. */
    uint32_t    u32Checksum;
    /** Vendor ID. */
    uint16_t    u16VendorId;
    /** Product ID. */
    uint16_t    u16ProductId;
    /** Firmware version. */
    uint32_t    u32FwVersion;
    /** Firmware sequencer Code version. */
    uint32_t    u32SeqCodeVersion;
    /** Image size in bytes including the header. */
    uint32_t    u32ImageSize;
    /** Offset of the first extended image header. */
    uint32_t    u32NextImageHeaderOffset;
    /** Start address of the image in IOC memory. */
    uint32_t    u32LoadStartAddress;
    /** Absolute start address of the Iop ARM. */
    uint32_t    u32IopResetVectorValue;
    /** Address of the IopResetVector register. */
    uint32_t    u32IopResetVectorRegAddr;
    /** Marker value for what utility. */
    uint32_t    u32VersionNameWhat;
    /** ASCII string of version. */
    uint8_t     aszVersionName[256];
    /** Marker value for what utility. */
    uint32_t    u32VendorNameWhat;
    /** ASCII string of vendor name. */
    uint8_t     aszVendorName[256];
} FwImageHdr, *PFwImageHdr;
AssertCompileSize(FwImageHdr, 584);

/** First part of the signature. */
#define LSILOGIC_FWIMGHDR_SIGNATURE1 UINT32_C(0x5aeaa55a)
/** Second part of the signature. */
#define LSILOGIC_FWIMGHDR_SIGNATURE2 UINT32_C(0xa55aeaa5)
/** Third part of the signature. */
#define LSILOGIC_FWIMGHDR_SIGNATURE3 UINT32_C(0x5aa55aea)
/** Load address of the firmware image to watch for,
 * seen used by Solaris 9. When this value is written to the
 * diagnostic address register we know a firmware image is downloaded.
 */
#define LSILOGIC_FWIMGHDR_LOAD_ADDRESS UINT32_C(0x21ff5e00)

/**
 * Configuration Page attributes.
 */
#define MPT_CONFIGURATION_PAGE_ATTRIBUTE_READONLY            (0x00)
#define MPT_CONFIGURATION_PAGE_ATTRIBUTE_CHANGEABLE          (0x10)
#define MPT_CONFIGURATION_PAGE_ATTRIBUTE_PERSISTENT          (0x20)
#define MPT_CONFIGURATION_PAGE_ATTRIBUTE_PERSISTENT_READONLY (0x30)

#define MPT_CONFIGURATION_PAGE_ATTRIBUTE_GET(u8PageType) ((u8PageType) & 0xf0)

/**
 * Configuration Page types.
 */
#define MPT_CONFIGURATION_PAGE_TYPE_IO_UNIT                  (0x00)
#define MPT_CONFIGURATION_PAGE_TYPE_IOC                      (0x01)
#define MPT_CONFIGURATION_PAGE_TYPE_BIOS                     (0x02)
#define MPT_CONFIGURATION_PAGE_TYPE_SCSI_SPI_PORT            (0x03)
#define MPT_CONFIGURATION_PAGE_TYPE_SCSI_SPI_DEVICE          (0x04)
#define MPT_CONFIGURATION_PAGE_TYPE_MANUFACTURING            (0x09)
#define MPT_CONFIGURATION_PAGE_TYPE_EXTENDED                 (0x0F)

#define MPT_CONFIGURATION_PAGE_TYPE_GET(u8PageType) ((u8PageType) & 0x0f)

/**
 * Extented page types.
 */
#define MPT_CONFIGURATION_PAGE_TYPE_EXTENDED_SASIOUNIT       (0x10)
#define MPT_CONFIGURATION_PAGE_TYPE_EXTENDED_SASEXPANDER     (0x11)
#define MPT_CONFIGURATION_PAGE_TYPE_EXTENDED_SASDEVICE       (0x12)
#define MPT_CONFIGURATION_PAGE_TYPE_EXTENDED_SASPHYS         (0x13)
#define MPT_CONFIGURATION_PAGE_TYPE_EXTENDED_LOG             (0x14)
#define MPT_CONFIGURATION_PAGE_TYPE_EXTENDED_ENCLOSURE       (0x15)

/**
 * Configuration Page header - Common to all pages.
 */
typedef struct MptConfigurationPageHeader
{
    /** Version of the page. */
    uint8_t     u8PageVersion;
    /** The length of the page in 32bit D-Words. */
    uint8_t     u8PageLength;
    /** Number of the page. */
    uint8_t     u8PageNumber;
    /** Type of the page. */
    uint8_t     u8PageType;
} MptConfigurationPageHeader, *PMptConfigurationPageHeader;
AssertCompileSize(MptConfigurationPageHeader, 4);

/**
 * Extended configuration page header - Common to all extended pages.
 */
typedef struct MptExtendedConfigurationPageHeader
{
    /** Version of the page. */
    uint8_t     u8PageVersion;
    /** Reserved. */
    uint8_t     u8Reserved1;
    /** Number of the page. */
    uint8_t     u8PageNumber;
    /** Type of the page. */
    uint8_t     u8PageType;
    /** Extended page length. */
    uint16_t    u16ExtPageLength;
    /** Extended page type. */
    uint8_t     u8ExtPageType;
    /** Reserved */
    uint8_t     u8Reserved2;
} MptExtendedConfigurationPageHeader, *PMptExtendedConfigurationPageHeader;
AssertCompileSize(MptExtendedConfigurationPageHeader, 8);

/**
 * Manufacturing page 0. - Readonly.
 */
typedef struct MptConfigurationPageManufacturing0 /**< @todo r=bird: This and a series of other structs could save a lot of 'u.' typing by promoting the inner 'u' union... */
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[76];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Name of the chip. */
            uint8_t               abChipName[16];
            /** Chip revision. */
            uint8_t               abChipRevision[8];
            /** Board name. */
            uint8_t               abBoardName[16];
            /** Board assembly. */
            uint8_t               abBoardAssembly[16];
            /** Board tracer number. */
            uint8_t               abBoardTracerNumber[16];
        } fields;
    } u;
} MptConfigurationPageManufacturing0, *PMptConfigurationPageManufacturing0;
AssertCompileSize(MptConfigurationPageManufacturing0, 76);

/**
 * Manufacturing page 1. - Readonly Persistent.
 */
typedef struct MptConfigurationPageManufacturing1
{
    /** Union */
    union
    {
        /** Byte view */
        uint8_t                           abPageData[260];
        /** Field view */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** VPD info - don't know what belongs here so all zero. */
            uint8_t                       abVPDInfo[256];
        } fields;
    } u;
} MptConfigurationPageManufacturing1, *PMptConfigurationPageManufacturing1;
AssertCompileSize(MptConfigurationPageManufacturing1, 260);

/**
 * Manufacturing page 2. - Readonly.
 */
typedef struct MptConfigurationPageManufacturing2
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                        abPageData[8];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader Header;
            /** PCI Device ID. */
            uint16_t                   u16PCIDeviceID;
            /** PCI Revision ID. */
            uint8_t                    u8PCIRevisionID;
            /** Reserved. */
            uint8_t                    u8Reserved;
            /** Hardware specific settings... */
        } fields;
    } u;
} MptConfigurationPageManufacturing2, *PMptConfigurationPageManufacturing2;
AssertCompileSize(MptConfigurationPageManufacturing2, 8);

/**
 * Manufacturing page 3. - Readonly.
 */
typedef struct MptConfigurationPageManufacturing3
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[8];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** PCI Device ID. */
            uint16_t              u16PCIDeviceID;
            /** PCI Revision ID. */
            uint8_t               u8PCIRevisionID;
            /** Reserved. */
            uint8_t               u8Reserved;
            /** Chip specific settings... */
        } fields;
    } u;
} MptConfigurationPageManufacturing3, *PMptConfigurationPageManufacturing3;
AssertCompileSize(MptConfigurationPageManufacturing3, 8);

/**
 * Manufacturing page 4. - Readonly.
 */
typedef struct MptConfigurationPageManufacturing4
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[84];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Reserved. */
            uint32_t              u32Reserved;
            /** InfoOffset0. */
            uint8_t               u8InfoOffset0;
            /** Info size. */
            uint8_t               u8InfoSize0;
            /** InfoOffset1. */
            uint8_t               u8InfoOffset1;
            /** Info size. */
            uint8_t               u8InfoSize1;
            /** Size of the inquiry data. */
            uint8_t               u8InquirySize;
            /** Reserved. */
            uint8_t               abReserved[3];
            /** Inquiry data. */
            uint8_t               abInquiryData[56];
            /** IS volume settings. */
            uint32_t              u32ISVolumeSettings;
            /** IME volume settings. */
            uint32_t              u32IMEVolumeSettings;
            /** IM volume settings. */
            uint32_t              u32IMVolumeSettings;
        } fields;
    } u;
} MptConfigurationPageManufacturing4, *PMptConfigurationPageManufacturing4;
AssertCompileSize(MptConfigurationPageManufacturing4, 84);

/**
 * Manufacturing page 5 - Readonly.
 */
#pragma pack(1) /* u64BaseWWID is at offset 4, which isn't natural for uint64_t. */
typedef struct MptConfigurationPageManufacturing5
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                           abPageData[88];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Base WWID.
             * @note Not aligned on 8-byte boundrary  */
            uint64_t                      u64BaseWWID;
            /** Flags */
            uint8_t                       u8Flags;
            /** Number of ForceWWID fields in this page. */
            uint8_t                       u8NumForceWWID;
            /** Reserved */
            uint16_t                      u16Reserved;
            /** Reserved */
            uint32_t                      au32Reserved[2];
            /** ForceWWID entries  Maximum of 8 because the SAS controller doesn't has more */
            uint64_t                      au64ForceWWID[8];
        } fields;
    } u;
} MptConfigurationPageManufacturing5, *PMptConfigurationPageManufacturing5;
#pragma pack()
AssertCompileSize(MptConfigurationPageManufacturing5, 24+64);

/**
 * Manufacturing page 6 - Readonly.
 */
typedef struct MptConfigurationPageManufacturing6
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                           abPageData[4];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Product specific data - 0 for now */
        } fields;
    } u;
} MptConfigurationPageManufacturing6, *PMptConfigurationPageManufacturing6;
AssertCompileSize(MptConfigurationPageManufacturing6, 4);

/**
 * Manufacutring page 7 - PHY element.
 */
typedef struct MptConfigurationPageManufacturing7PHY
{
    /** Pinout */
    uint32_t                  u32Pinout;
    /** Connector name */
    uint8_t                   szConnector[16];
    /** Location */
    uint8_t                   u8Location;
    /** reserved */
    uint8_t                   u8Reserved;
    /** Slot */
    uint16_t                  u16Slot;
} MptConfigurationPageManufacturing7PHY, *PMptConfigurationPageManufacturing7PHY;
AssertCompileSize(MptConfigurationPageManufacturing7PHY, 24);

/**
 * Manufacturing page 7 - Readonly.
 */
typedef struct MptConfigurationPageManufacturing7
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                           abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Reserved */
            uint32_t                      au32Reserved[2];
            /** Flags */
            uint32_t                      u32Flags;
            /** Enclosure name */
            uint8_t                       szEnclosureName[16];
            /** Number of PHYs */
            uint8_t                       u8NumPhys;
            /** Reserved */
            uint8_t                       au8Reserved[3];
            /** PHY list for the SAS controller - variable depending on the number of ports */
            MptConfigurationPageManufacturing7PHY aPHY[1];
        } fields;
    } u;
} MptConfigurationPageManufacturing7, *PMptConfigurationPageManufacturing7;
AssertCompileSize(MptConfigurationPageManufacturing7, 36+sizeof(MptConfigurationPageManufacturing7PHY));

#define LSILOGICSCSI_MANUFACTURING7_GET_SIZE(ports) (sizeof(MptConfigurationPageManufacturing7) + ((ports) - 1) * sizeof(MptConfigurationPageManufacturing7PHY))

/** Flags for the flags field */
#define LSILOGICSCSI_MANUFACTURING7_FLAGS_USE_PROVIDED_INFORMATION RT_BIT(0)

/** Flags for the pinout field */
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_UNKNOWN                 RT_BIT(0)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8482                 RT_BIT(1)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8470_LANE1           RT_BIT(8)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8470_LANE2           RT_BIT(9)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8470_LANE3           RT_BIT(10)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8470_LANE4           RT_BIT(11)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8484_LANE1           RT_BIT(16)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8484_LANE2           RT_BIT(17)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8484_LANE3           RT_BIT(18)
#define LSILOGICSCSI_MANUFACTURING7_PINOUT_SFF8484_LANE4           RT_BIT(19)

/** Flags for the location field */
#define LSILOGICSCSI_MANUFACTURING7_LOCATION_UNKNOWN               0x01
#define LSILOGICSCSI_MANUFACTURING7_LOCATION_INTERNAL              0x02
#define LSILOGICSCSI_MANUFACTURING7_LOCATION_EXTERNAL              0x04
#define LSILOGICSCSI_MANUFACTURING7_LOCATION_SWITCHABLE            0x08
#define LSILOGICSCSI_MANUFACTURING7_LOCATION_AUTO                  0x10
#define LSILOGICSCSI_MANUFACTURING7_LOCATION_NOT_PRESENT           0x20
#define LSILOGICSCSI_MANUFACTURING7_LOCATION_NOT_CONNECTED         0x80

/**
 * Manufacturing page 8 - Readonly.
 */
typedef struct MptConfigurationPageManufacturing8
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                           abPageData[4];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Product specific information */
        } fields;
    } u;
} MptConfigurationPageManufacturing8, *PMptConfigurationPageManufacturing8;
AssertCompileSize(MptConfigurationPageManufacturing8, 4);

/**
 * Manufacturing page 9 - Readonly.
 */
typedef struct MptConfigurationPageManufacturing9
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                           abPageData[4];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Product specific information */
        } fields;
    } u;
} MptConfigurationPageManufacturing9, *PMptConfigurationPageManufacturing9;
AssertCompileSize(MptConfigurationPageManufacturing9, 4);

/**
 * Manufacturing page 10 - Readonly.
 */
typedef struct MptConfigurationPageManufacturing10
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                           abPageData[4];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Product specific information */
        } fields;
    } u;
} MptConfigurationPageManufacturing10, *PMptConfigurationPageManufacturing10;
AssertCompileSize(MptConfigurationPageManufacturing10, 4);

/**
 * IO Unit page 0. - Readonly.
 */
#pragma pack(1) /* u64UniqueIdentifier is at offset 4, which isn't natural for uint64_t. */
typedef struct MptConfigurationPageIOUnit0
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[12];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** A unique identifier. */
            uint64_t              u64UniqueIdentifier;
        } fields;
    } u;
} MptConfigurationPageIOUnit0, *PMptConfigurationPageIOUnit0;
#pragma pack()
AssertCompileSize(MptConfigurationPageIOUnit0, 12);

/**
 * IO Unit page 1. - Read/Write.
 */
typedef struct MptConfigurationPageIOUnit1
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[8];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Flag whether this is a single function PCI device. */
            unsigned              fSingleFunction:         1;
            /** Flag whether all possible paths to a device are mapped. */
            unsigned              fAllPathsMapped:         1;
            /** Reserved. */
            unsigned              u4Reserved:              4;
            /** Flag whether all RAID functionality is disabled. */
            unsigned              fIntegratedRAIDDisabled: 1;
            /** Flag whether 32bit PCI accesses are forced. */
            unsigned              f32BitAccessForced:      1;
            /** Reserved. */
            unsigned              abReserved:             24;
        } fields;
    } u;
} MptConfigurationPageIOUnit1, *PMptConfigurationPageIOUnit1;
AssertCompileSize(MptConfigurationPageIOUnit1, 8);

/**
 * Adapter Ordering.
 */
typedef struct MptConfigurationPageIOUnit2AdapterOrdering
{
    /** PCI bus number. */
    unsigned    u8PCIBusNumber:   8;
    /** PCI device and function number. */
    unsigned    u8PCIDevFn:       8;
    /** Flag whether the adapter is embedded. */
    unsigned    fAdapterEmbedded: 1;
    /** Flag whether the adapter is enabled. */
    unsigned    fAdapterEnabled:  1;
    /** Reserved. */
    unsigned    u6Reserved:       6;
    /** Reserved. */
    unsigned    u8Reserved:       8;
} MptConfigurationPageIOUnit2AdapterOrdering, *PMptConfigurationPageIOUnit2AdapterOrdering;
AssertCompileSize(MptConfigurationPageIOUnit2AdapterOrdering, 4);

/**
 * IO Unit page 2. - Read/Write.
 */
typedef struct MptConfigurationPageIOUnit2
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[28];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Reserved. */
            unsigned              fReserved:           1;
            /** Flag whether Pause on error is enabled. */
            unsigned              fPauseOnError:       1;
            /** Flag whether verbose mode is enabled. */
            unsigned              fVerboseModeEnabled: 1;
            /** Set to disable color video. */
            unsigned              fDisableColorVideo:  1;
            /** Flag whether int 40h is hooked. */
            unsigned              fNotHookInt40h:      1;
            /** Reserved. */
            unsigned              u3Reserved:          3;
            /** Reserved. */
            unsigned              abReserved:         24;
            /** BIOS version. */
            uint32_t              u32BIOSVersion;
            /** Adapter ordering. */
            MptConfigurationPageIOUnit2AdapterOrdering aAdapterOrder[4];
        } fields;
    } u;
} MptConfigurationPageIOUnit2, *PMptConfigurationPageIOUnit2;
AssertCompileSize(MptConfigurationPageIOUnit2, 28);

/*
 * IO Unit page 3. - Read/Write.
 */
typedef struct MptConfigurationPageIOUnit3
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[8];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Number of GPIO values. */
            uint8_t               u8GPIOCount;
            /** Reserved. */
            uint8_t               abReserved[3];
        } fields;
    } u;
} MptConfigurationPageIOUnit3, *PMptConfigurationPageIOUnit3;
AssertCompileSize(MptConfigurationPageIOUnit3, 8);

/*
 * IO Unit page 4. - Readonly for everyone except the BIOS.
 */
typedef struct MptConfigurationPageIOUnit4
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[20];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Reserved */
            uint32_t                      u32Reserved;
            /** SG entry describing the Firmware location. */
            MptSGEntrySimple64            FWImageSGE;
        } fields;
    } u;
} MptConfigurationPageIOUnit4, *PMptConfigurationPageIOUnit4;
AssertCompileSize(MptConfigurationPageIOUnit4, 20);

/**
 * IOC page 0. - Readonly
 */
typedef struct MptConfigurationPageIOC0
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[28];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Total amount of NV memory in bytes. */
            uint32_t              u32TotalNVStore;
            /** Number of free bytes in the NV store. */
            uint32_t              u32FreeNVStore;
            /** PCI vendor ID. */
            uint16_t              u16VendorId;
            /** PCI device ID. */
            uint16_t              u16DeviceId;
            /** PCI revision ID. */
            uint8_t               u8RevisionId;
            /** Reserved. */
            uint8_t               abReserved[3];
            /** PCI class code. */
            uint32_t              u32ClassCode;
            /** Subsystem vendor Id. */
            uint16_t              u16SubsystemVendorId;
            /** Subsystem Id. */
            uint16_t              u16SubsystemId;
        } fields;
    } u;
} MptConfigurationPageIOC0, *PMptConfigurationPageIOC0;
AssertCompileSize(MptConfigurationPageIOC0, 28);

/**
 * IOC page 1. - Read/Write
 */
typedef struct MptConfigurationPageIOC1
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[16];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Flag whether reply coalescing is enabled. */
            unsigned              fReplyCoalescingEnabled: 1;
            /** Reserved. */
            unsigned              u31Reserved:            31;
            /** Coalescing Timeout in microseconds. */
            unsigned              u32CoalescingTimeout:   32;
            /** Coalescing depth. */
            unsigned              u8CoalescingDepth:       8;
            /** Reserved. */
            unsigned              u8Reserved0:             8;
            unsigned              u8Reserved1:             8;
            unsigned              u8Reserved2:             8;
        } fields;
    } u;
} MptConfigurationPageIOC1, *PMptConfigurationPageIOC1;
AssertCompileSize(MptConfigurationPageIOC1, 16);

/**
 * IOC page 2. - Readonly
 */
typedef struct MptConfigurationPageIOC2
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[12];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Flag whether striping is supported. */
            unsigned              fStripingSupported:            1;
            /** Flag whether enhanced mirroring is supported. */
            unsigned              fEnhancedMirroringSupported:   1;
            /** Flag whether mirroring is supported. */
            unsigned              fMirroringSupported:           1;
            /** Reserved. */
            unsigned              u26Reserved:                  26;
            /** Flag whether SES is supported. */
            unsigned              fSESSupported:                 1;
            /** Flag whether SAF-TE is supported. */
            unsigned              fSAFTESupported:               1;
            /** Flag whether cross channel volumes are supported. */
            unsigned              fCrossChannelVolumesSupported: 1;
            /** Number of active integrated RAID volumes. */
            unsigned              u8NumActiveVolumes:            8;
            /** Maximum number of integrated RAID volumes supported. */
            unsigned              u8MaxVolumes:                  8;
            /** Number of active integrated RAID physical disks. */
            unsigned              u8NumActivePhysDisks:          8;
            /** Maximum number of integrated RAID physical disks supported. */
            unsigned              u8MaxPhysDisks:                8;
            /** RAID volumes... - not supported. */
        } fields;
    } u;
} MptConfigurationPageIOC2, *PMptConfigurationPageIOC2;
AssertCompileSize(MptConfigurationPageIOC2, 12);

/**
 * IOC page 3. - Readonly
 */
typedef struct MptConfigurationPageIOC3
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[8];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Number of active integrated RAID physical disks. */
            uint8_t               u8NumPhysDisks;
            /** Reserved. */
            uint8_t               abReserved[3];
        } fields;
    } u;
} MptConfigurationPageIOC3, *PMptConfigurationPageIOC3;
AssertCompileSize(MptConfigurationPageIOC3, 8);

/**
 * IOC page 4. - Read/Write
 */
typedef struct MptConfigurationPageIOC4
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[8];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Number of SEP entries in this page. */
            uint8_t               u8ActiveSEP;
            /** Maximum number of SEp entries supported. */
            uint8_t               u8MaxSEP;
            /** Reserved. */
            uint16_t              u16Reserved;
            /** SEP entries... - not supported. */
        } fields;
    } u;
} MptConfigurationPageIOC4, *PMptConfigurationPageIOC4;
AssertCompileSize(MptConfigurationPageIOC4, 8);

/**
 * IOC page 6. - Read/Write
 */
typedef struct MptConfigurationPageIOC6
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[60];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            uint32_t                      u32CapabilitiesFlags;
            uint8_t                       u8MaxDrivesIS;
            uint8_t                       u8MaxDrivesIM;
            uint8_t                       u8MaxDrivesIME;
            uint8_t                       u8Reserved1;
            uint8_t                       u8MinDrivesIS;
            uint8_t                       u8MinDrivesIM;
            uint8_t                       u8MinDrivesIME;
            uint8_t                       u8Reserved2;
            uint8_t                       u8MaxGlobalHotSpares;
            uint8_t                       u8Reserved3;
            uint16_t                      u16Reserved4;
            uint32_t                      u32Reserved5;
            uint32_t                      u32SupportedStripeSizeMapIS;
            uint32_t                      u32SupportedStripeSizeMapIME;
            uint32_t                      u32Reserved6;
            uint8_t                       u8MetadataSize;
            uint8_t                       u8Reserved7;
            uint16_t                      u16Reserved8;
            uint16_t                      u16MaxBadBlockTableEntries;
            uint16_t                      u16Reserved9;
            uint16_t                      u16IRNvsramUsage;
            uint16_t                      u16Reserved10;
            uint32_t                      u32IRNvsramVersion;
            uint32_t                      u32Reserved11;
        } fields;
    } u;
} MptConfigurationPageIOC6, *PMptConfigurationPageIOC6;
AssertCompileSize(MptConfigurationPageIOC6, 60);

/**
 * BIOS page 1 - Read/write.
 */
typedef struct MptConfigurationPageBIOS1
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[48];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** BIOS options */
            uint32_t                      u32BiosOptions;
            /** IOC settings */
            uint32_t                      u32IOCSettings;
            /** Reserved */
            uint32_t                      u32Reserved;
            /** Device settings */
            uint32_t                      u32DeviceSettings;
            /** Number of devices */
            uint16_t                      u16NumberOfDevices;
            /** Expander spinup */
            uint8_t                       u8ExpanderSpinup;
            /** Reserved */
            uint8_t                       u8Reserved;
            /** I/O timeout of block devices without removable media */
            uint16_t                      u16IOTimeoutBlockDevicesNonRM;
            /** I/O timeout sequential */
            uint16_t                      u16IOTimeoutSequential;
            /** I/O timeout other */
            uint16_t                      u16IOTimeoutOther;
            /** I/O timeout of block devices with removable media */
            uint16_t                      u16IOTimeoutBlockDevicesRM;
        } fields;
    } u;
} MptConfigurationPageBIOS1, *PMptConfigurationPageBIOS1;
AssertCompileSize(MptConfigurationPageBIOS1, 48);

#define LSILOGICSCSI_BIOS1_BIOSOPTIONS_BIOS_DISABLE              RT_BIT(0)
#define LSILOGICSCSI_BIOS1_BIOSOPTIONS_SCAN_FROM_HIGH_TO_LOW     RT_BIT(1)
#define LSILOGICSCSI_BIOS1_BIOSOPTIONS_BIOS_EXTENDED_SAS_SUPPORT RT_BIT(8)
#define LSILOGICSCSI_BIOS1_BIOSOPTIONS_BIOS_EXTENDED_FC_SUPPORT  RT_BIT(9)
#define LSILOGICSCSI_BIOS1_BIOSOPTIONS_BIOS_EXTENDED_SPI_SUPPORT RT_BIT(10)

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_ALTERNATE_CHS             RT_BIT(3)

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_ADAPTER_SUPPORT_SET(x)    ((x) << 4)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_ADAPTER_SUPPORT_DISABLED  0x00
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_ADAPTER_SUPPORT_BIOS_ONLY 0x01
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_ADAPTER_SUPPORT_OS_ONLY   0x02
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_ADAPTER_SUPPORT_BOT       0x03

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_REMOVABLE_MEDIA_SET(x)    ((x) << 6)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_REMOVABLE_MEDIA_NO_INT13H 0x00
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_REMOVABLE_BOOT_MEDIA_INT13H 0x01
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_REMOVABLE_MEDIA_INT13H      0x02

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_SPINUP_DELAY_SET(x)       ((x & 0xF) << 8)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_SPINUP_DELAY_GET(x)       ((x >> 8) & 0x0F)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_MAX_TARGET_SPINUP_SET(x)  ((x & 0xF) << 12)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_MAX_TARGET_SPINUP_GET(x)  ((x >> 12) & 0x0F)

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_BOOT_PREFERENCE_SET(x)      (((x) & 0x3) << 16)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_BOOT_PREFERENCE_ENCLOSURE   0x0
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_BOOT_PREFERENCE_SAS_ADDRESS 0x1

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_DIRECT_ATTACH_SPINUP_MODE_ALL RT_BIT(18)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_AUTO_PORT_ENABLE              RT_BIT(19)

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_PORT_ENABLE_REPLY_DELAY_SET(x) (((x) & 0xF) << 20)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_PORT_ENABLE_REPLY_DELAY_GET(x) ((x >> 20) & 0x0F)

#define LSILOGICSCSI_BIOS1_IOCSETTINGS_PORT_ENABLE_SPINUP_DELAY_SET(x) (((x) & 0xF) << 24)
#define LSILOGICSCSI_BIOS1_IOCSETTINGS_PORT_ENABLE_SPINUP_DELAY_GET(x) ((x >> 24) & 0x0F)

#define LSILOGICSCSI_BIOS1_DEVICESETTINGS_DISABLE_LUN_SCANS                           RT_BIT(0)
#define LSILOGICSCSI_BIOS1_DEVICESETTINGS_DISABLE_LUN_SCANS_FOR_NON_REMOVABLE_DEVICES RT_BIT(1)
#define LSILOGICSCSI_BIOS1_DEVICESETTINGS_DISABLE_LUN_SCANS_FOR_REMOVABLE_DEVICES     RT_BIT(2)
#define LSILOGICSCSI_BIOS1_DEVICESETTINGS_DISABLE_LUN_SCANS2                          RT_BIT(3)
#define LSILOGICSCSI_BIOS1_DEVICESETTINGS_DISABLE_SMART_POLLING                       RT_BIT(4)

#define LSILOGICSCSI_BIOS1_EXPANDERSPINUP_SPINUP_DELAY_SET(x)          ((x) & 0x0F)
#define LSILOGICSCSI_BIOS1_EXPANDERSPINUP_SPINUP_DELAY_GET(x)          ((x) & 0x0F)
#define LSILOGICSCSI_BIOS1_EXPANDERSPINUP_MAX_SPINUP_DELAY_SET(x)      (((x) & 0x0F) << 4)
#define LSILOGICSCSI_BIOS1_EXPANDERSPINUP_MAX_SPINUP_DELAY_GET(x)      ((x >> 4) & 0x0F)

/**
 * BIOS page 2 - Read/write.
 */
typedef struct MptConfigurationPageBIOS2
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[384];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Reserved */
            uint32_t                      au32Reserved[6];
            /** Format of the boot device field. */
            uint8_t                       u8BootDeviceForm;
            /** Previous format of the boot device field. */
            uint8_t                       u8PrevBootDeviceForm;
            /** Reserved */
            uint16_t                      u16Reserved;
            /** Boot device fields - dependent on the format */
            union
            {
                /** Device for AdapterNumber:Bus:Target:LUN */
                struct
                {
                    /** Target ID */
                    uint8_t               u8TargetID;
                    /** Bus */
                    uint8_t               u8Bus;
                    /** Adapter Number */
                    uint8_t               u8AdapterNumber;
                    /** Reserved */
                    uint8_t               u8Reserved;
                    /** Reserved */
                    uint32_t              au32Reserved[3];
                    /** LUN */
                    uint32_t              aLUN[5];
                    /** Reserved */
                    uint32_t              au32Reserved2[56];
                } AdapterNumberBusTargetLUN;
                /** Device for PCIAddress:Bus:Target:LUN */
                struct
                {
                    /** Target ID */
                    uint8_t               u8TargetID;
                    /** Bus */
                    uint8_t               u8Bus;
                    /** Adapter Number */
                    uint16_t              u16PCIAddress;
                    /** Reserved */
                    uint32_t              au32Reserved[3];
                    /** LUN */
                    uint32_t              aLUN[5];
                    /** Reserved */
                    uint32_t              au32Reserved2[56];
                } PCIAddressBusTargetLUN;
#if 0 /** @todo r=bird: The u16PCISlotNo member looks like it has the wrong type, but I cannot immediately locate specs and check. */
                /** Device for PCISlotNo:Bus:Target:LUN */
                struct
                {
                    /** Target ID */
                    uint8_t               u8TargetID;
                    /** Bus */
                    uint8_t               u8Bus;
                    /** PCI Slot Number */
                    uint8_t              u16PCISlotNo;
                    /** Reserved */
                    uint32_t              au32Reserved[3];
                    /** LUN */
                    uint32_t              aLUN[5];
                    /** Reserved */
                    uint32_t              au32Reserved2[56];
                } PCIAddressBusSlotLUN;
#endif
                /** Device for FC channel world wide name */
                struct
                {
                    /** World wide port name low */
                    uint32_t              u32WorldWidePortNameLow;
                    /** World wide port name high */
                    uint32_t              u32WorldWidePortNameHigh;
                    /** Reserved */
                    uint32_t              au32Reserved[3];
                    /** LUN */
                    uint32_t              aLUN[5];
                    /** Reserved */
                    uint32_t              au32Reserved2[56];
                } FCWorldWideName;
                /** Device for FC channel world wide name */
                struct
                {
                    /** SAS address */
                    SASADDRESS            SASAddress;
                    /** Reserved */
                    uint32_t              au32Reserved[3];
                    /** LUN */
                    uint32_t              aLUN[5];
                    /** Reserved */
                    uint32_t              au32Reserved2[56];
                } SASWorldWideName;
                /** Device for Enclosure/Slot */
                struct
                {
                    /** Enclosure logical ID */
                    uint64_t              u64EnclosureLogicalID;
                    /** Reserved */
                    uint32_t              au32Reserved[3];
                    /** LUN */
                    uint32_t              aLUN[5];
                    /** Reserved */
                    uint32_t              au32Reserved2[56];
                } EnclosureSlot;
            } BootDevice;
        } fields;
    } u;
} MptConfigurationPageBIOS2, *PMptConfigurationPageBIOS2;
AssertCompileMemberAlignment(MptConfigurationPageBIOS2, u.fields, 8);
AssertCompileSize(MptConfigurationPageBIOS2, 384);

#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_SET(x)                 ((x) & 0x0F)
#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_FIRST                  0x0
#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_ADAPTER_BUS_TARGET_LUN 0x1
#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_PCIADDR_BUS_TARGET_LUN 0x2
#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_PCISLOT_BUS_TARGET_LUN 0x3
#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_FC_WWN                 0x4
#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_SAS_WWN                0x5
#define LSILOGICSCSI_BIOS2_BOOT_DEVICE_FORM_ENCLOSURE_SLOT         0x6

/**
 * BIOS page 4 - Read/Write (Where is 3? - not defined in the spec)
 */
#pragma pack(1) /* u64ReassignmentBaseWWID starts at offset 4, which isn't normally natural for uint64_t.  */
typedef struct MptConfigurationPageBIOS4
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[12];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Reassignment Base WWID */
            uint64_t                      u64ReassignmentBaseWWID;
        } fields;
    } u;
} MptConfigurationPageBIOS4, *PMptConfigurationPageBIOS4;
#pragma pack()
AssertCompileSize(MptConfigurationPageBIOS4, 12);

/**
 * SCSI-SPI port page 0. - Readonly
 */
typedef struct MptConfigurationPageSCSISPIPort0
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[12];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Flag whether this port is information unit transfers capable. */
            unsigned              fInformationUnitTransfersCapable: 1;
            /** Flag whether the port is DT (Dual Transfer) capable. */
            unsigned              fDTCapable:                       1;
            /** Flag whether the port is QAS (Quick Arbitrate and Select) capable. */
            unsigned              fQASCapable:                      1;
            /** Reserved. */
            unsigned              u5Reserved1:                      5;
            /** Minimum Synchronous transfer period. */
            unsigned              u8MinimumSynchronousTransferPeriod: 8;
            /** Maximum synchronous offset. */
            unsigned              u8MaximumSynchronousOffset:         8;
            /** Reserved. */
            unsigned              u5Reserved2:                      5;
            /** Flag whether indicating the width of the bus - 0 narrow and 1 for wide. */
            unsigned              fWide:                            1;
            /** Reserved */
            unsigned              fReserved:                        1;
            /** Flag whether the port is AIP (Asynchronous Information Protection) capable. */
            unsigned              fAIPCapable:                      1;
            /** Signaling Type. */
            unsigned              u2SignalingType:                  2;
            /** Reserved. */
            unsigned              u30Reserved:                     30;
        } fields;
    } u;
} MptConfigurationPageSCSISPIPort0, *PMptConfigurationPageSCSISPIPort0;
AssertCompileSize(MptConfigurationPageSCSISPIPort0, 12);

/**
 * SCSI-SPI port page 1. - Read/Write
 */
typedef struct MptConfigurationPageSCSISPIPort1
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[12];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** The SCSI ID of the port. */
            uint8_t               u8SCSIID;
            /** Reserved. */
            uint8_t               u8Reserved;
            /** Port response IDs Bit mask field. */
            uint16_t              u16PortResponseIDsBitmask;
            /** Value for the on BUS timer. */
            uint32_t              u32OnBusTimerValue;
        } fields;
    } u;
} MptConfigurationPageSCSISPIPort1, *PMptConfigurationPageSCSISPIPort1;
AssertCompileSize(MptConfigurationPageSCSISPIPort1, 12);

/**
 * Device settings for one device.
 */
typedef struct MptDeviceSettings
{
    /** Timeout for I/O in seconds. */
    unsigned    u8Timeout:             8;
    /** Minimum synchronous factor. */
    unsigned    u8SyncFactor:          8;
    /** Flag whether disconnect is enabled. */
    unsigned    fDisconnectEnable:     1;
    /** Flag whether Scan ID is enabled. */
    unsigned    fScanIDEnable:         1;
    /** Flag whether Scan LUNs is enabled. */
    unsigned    fScanLUNEnable:        1;
    /** Flag whether tagged queuing is enabled. */
    unsigned    fTaggedQueuingEnabled: 1;
    /** Flag whether wide is enabled. */
    unsigned    fWideDisable:          1;
    /** Flag whether this device is bootable. */
    unsigned    fBootChoice:           1;
    /** Reserved. */
    unsigned    u10Reserved:          10;
} MptDeviceSettings, *PMptDeviceSettings;
AssertCompileSize(MptDeviceSettings, 4);

/**
 * SCSI-SPI port page 2. - Read/Write for the BIOS
 */
typedef struct MptConfigurationPageSCSISPIPort2
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[76];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Flag indicating the bus scan order. */
            unsigned              fBusScanOrderHighToLow:  1;
            /** Reserved. */
            unsigned              fReserved:               1;
            /** Flag whether SCSI Bus resets are avoided. */
            unsigned              fAvoidSCSIBusResets:     1;
            /** Flag whether alternate CHS is used. */
            unsigned              fAlternateCHS:           1;
            /** Flag whether termination is disabled. */
            unsigned              fTerminationDisabled:    1;
            /** Reserved. */
            unsigned              u27Reserved:            27;
            /** Host SCSI ID. */
            unsigned              u4HostSCSIID:            4;
            /** Initialize HBA. */
            unsigned              u2InitializeHBA:         2;
            /** Removeable media setting. */
            unsigned              u2RemovableMediaSetting: 2;
            /** Spinup delay. */
            unsigned              u4SpinupDelay:           4;
            /** Negotiating settings. */
            unsigned              u2NegotitatingSettings:  2;
            /** Reserved. */
            unsigned              u18Reserved:            18;
            /** Device Settings. */
            MptDeviceSettings     aDeviceSettings[16];
        } fields;
    } u;
} MptConfigurationPageSCSISPIPort2, *PMptConfigurationPageSCSISPIPort2;
AssertCompileSize(MptConfigurationPageSCSISPIPort2, 76);

/**
 * SCSI-SPI device page 0. - Readonly
 */
typedef struct MptConfigurationPageSCSISPIDevice0
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[12];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Negotiated Parameters. */
            /** Information Units enabled. */
            unsigned              fInformationUnitsEnabled: 1;
            /** Dual Transfers Enabled. */
            unsigned              fDTEnabled:               1;
            /** QAS enabled. */
            unsigned              fQASEnabled:              1;
            /** Reserved. */
            unsigned              u5Reserved1:              5;
            /** Synchronous Transfer period. */
            unsigned              u8NegotiatedSynchronousTransferPeriod: 8;
            /** Synchronous offset. */
            unsigned              u8NegotiatedSynchronousOffset: 8;
            /** Reserved. */
            unsigned              u5Reserved2:              5;
            /** Width - 0 for narrow and 1 for wide. */
            unsigned              fWide:                    1;
            /** Reserved. */
            unsigned              fReserved:                1;
            /** AIP enabled. */
            unsigned              fAIPEnabled:              1;
            /** Flag whether negotiation occurred. */
            unsigned              fNegotationOccured:       1;
            /** Flag whether a SDTR message was rejected. */
            unsigned              fSDTRRejected:            1;
            /** Flag whether a WDTR message was rejected. */
            unsigned              fWDTRRejected:            1;
            /** Flag whether a PPR message was rejected. */
            unsigned              fPPRRejected:             1;
            /** Reserved. */
            unsigned              u28Reserved:             28;
        } fields;
    } u;
} MptConfigurationPageSCSISPIDevice0, *PMptConfigurationPageSCSISPIDevice0;
AssertCompileSize(MptConfigurationPageSCSISPIDevice0, 12);

/**
 * SCSI-SPI device page 1. - Read/Write
 */
typedef struct MptConfigurationPageSCSISPIDevice1
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[16];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Requested Parameters. */
            /** Information Units enable. */
            unsigned              fInformationUnitsEnable: 1;
            /** Dual Transfers Enable. */
            unsigned              fDTEnable:               1;
            /** QAS enable. */
            unsigned              fQASEnable:              1;
            /** Reserved. */
            unsigned              u5Reserved1:             5;
            /** Synchronous Transfer period. */
            unsigned              u8NegotiatedSynchronousTransferPeriod: 8;
            /** Synchronous offset. */
            unsigned              u8NegotiatedSynchronousOffset: 8;
            /** Reserved. */
            unsigned              u5Reserved2:             5;
            /** Width - 0 for narrow and 1 for wide. */
            unsigned              fWide:                   1;
            /** Reserved. */
            unsigned              fReserved1:              1;
            /** AIP enable. */
            unsigned              fAIPEnable:              1;
            /** Reserved. */
            unsigned              fReserved2:              1;
            /** WDTR disallowed. */
            unsigned              fWDTRDisallowed:         1;
            /** SDTR disallowed. */
            unsigned              fSDTRDisallowed:         1;
            /** Reserved. */
            unsigned              u29Reserved:            29;
        } fields;
    } u;
} MptConfigurationPageSCSISPIDevice1, *PMptConfigurationPageSCSISPIDevice1;
AssertCompileSize(MptConfigurationPageSCSISPIDevice1, 16);

/**
 * SCSI-SPI device page 2. - Read/Write
 */
typedef struct MptConfigurationPageSCSISPIDevice2
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[16];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Reserved. */
            unsigned              u4Reserved: 4;
            /** ISI enable. */
            unsigned              fISIEnable: 1;
            /** Secondary driver enable. */
            unsigned              fSecondaryDriverEnable:          1;
            /** Reserved. */
            unsigned              fReserved:                       1;
            /** Slew create controller. */
            unsigned              u3SlewRateControler:             3;
            /** Primary drive strength controller. */
            unsigned              u3PrimaryDriveStrengthControl:   3;
            /** Secondary drive strength controller. */
            unsigned              u3SecondaryDriveStrengthControl: 3;
            /** Reserved. */
            unsigned              u12Reserved:                    12;
            /** XCLKH_ST. */
            unsigned              fXCLKH_ST:                       1;
            /** XCLKS_ST. */
            unsigned              fXCLKS_ST:                       1;
            /** XCLKH_DT. */
            unsigned              fXCLKH_DT:                       1;
            /** XCLKS_DT. */
            unsigned              fXCLKS_DT:                       1;
            /** Parity pipe select. */
            unsigned              u2ParityPipeSelect:              2;
            /** Reserved. */
            unsigned              u30Reserved:                    30;
            /** Data bit pipeline select. */
            unsigned              u32DataPipelineSelect:          32;
        } fields;
    } u;
} MptConfigurationPageSCSISPIDevice2, *PMptConfigurationPageSCSISPIDevice2;
AssertCompileSize(MptConfigurationPageSCSISPIDevice2, 16);

/**
 * SCSI-SPI device page 3 (Revision G). - Readonly
 */
typedef struct MptConfigurationPageSCSISPIDevice3
{
    /** Union. */
    union
    {
        /** Byte view. */
        uint8_t                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptConfigurationPageHeader    Header;
            /** Number of times the IOC rejected a message because it doesn't support the operation. */
            uint16_t                      u16MsgRejectCount;
            /** Number of times the SCSI bus entered an invalid operation state. */
            uint16_t                      u16PhaseErrorCount;
            /** Number of parity errors. */
            uint16_t                      u16ParityCount;
            /** Reserved. */
            uint16_t                      u16Reserved;
        } fields;
    } u;
} MptConfigurationPageSCSISPIDevice3, *PMptConfigurationPageSCSISPIDevice3;
AssertCompileSize(MptConfigurationPageSCSISPIDevice3, 12);

/**
 * PHY entry for the SAS I/O unit page 0
 */
typedef struct MptConfigurationPageSASIOUnit0PHY
{
    /** Port number */
    uint8_t                           u8Port;
    /** Port flags */
    uint8_t                           u8PortFlags;
    /** Phy flags */
    uint8_t                           u8PhyFlags;
    /** negotiated link rate */
    uint8_t                           u8NegotiatedLinkRate;
    /** Controller phy device info */
    uint32_t                          u32ControllerPhyDeviceInfo;
    /** Attached device handle */
    uint16_t                          u16AttachedDevHandle;
    /** Controller device handle */
    uint16_t                          u16ControllerDevHandle;
    /** Discovery status */
    uint32_t                          u32DiscoveryStatus;
} MptConfigurationPageSASIOUnit0PHY, *PMptConfigurationPageSASIOUnit0PHY;
AssertCompileSize(MptConfigurationPageSASIOUnit0PHY, 16);

/**
 * SAS I/O  Unit page 0 - Readonly
 */
typedef struct MptConfigurationPageSASIOUnit0
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Nvdata version default */
            uint16_t                              u16NvdataVersionDefault;
            /** Nvdata version persistent */
            uint16_t                              u16NvdataVersionPersistent;
            /** Number of physical ports */
            uint8_t                               u8NumPhys;
            /** Reserved */
            uint8_t                               au8Reserved[3];
            /** Content for each physical port - variable depending on the amount of ports. */
            MptConfigurationPageSASIOUnit0PHY     aPHY[1];
        } fields;
    } u;
} MptConfigurationPageSASIOUnit0, *PMptConfigurationPageSASIOUnit0;
AssertCompileSize(MptConfigurationPageSASIOUnit0, 8+2+2+1+3+sizeof(MptConfigurationPageSASIOUnit0PHY));

#define LSILOGICSCSI_SASIOUNIT0_GET_SIZE(ports) (sizeof(MptConfigurationPageSASIOUnit0) + ((ports) - 1) * sizeof(MptConfigurationPageSASIOUnit0PHY))

#define LSILOGICSCSI_SASIOUNIT0_PORT_CONFIGURATION_AUTO  RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT0_PORT_TARGET_IOC          RT_BIT(2)
#define LSILOGICSCSI_SASIOUNIT0_PORT_DISCOVERY_IN_STATUS RT_BIT(3)

#define LSILOGICSCSI_SASIOUNIT0_PHY_RX_INVERTED          RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT0_PHY_TX_INVERTED          RT_BIT(1)
#define LSILOGICSCSI_SASIOUNIT0_PHY_DISABLED             RT_BIT(2)

#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_SET(x)   ((x) & 0x0F)
#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_GET(x)   ((x) & 0x0F)
#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_UNKNOWN  0x00
#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_DISABLED 0x01
#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_FAILED   0x02
#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_SATA_OOB 0x03
#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_15GB     0x08
#define LSILOGICSCSI_SASIOUNIT0_NEGOTIATED_RATE_30GB     0x09

#define LSILOGICSCSI_SASIOUNIT0_DEVICE_TYPE_SET(x)          ((x) & 0x3)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_TYPE_NO              0x0
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_TYPE_END             0x1
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_TYPE_EDGE_EXPANDER   0x2
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_TYPE_FANOUT_EXPANDER 0x3

#define LSILOGICSCSI_SASIOUNIT0_DEVICE_SATA_HOST            RT_BIT(3)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_SMP_INITIATOR        RT_BIT(4)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_STP_INITIATOR        RT_BIT(5)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_SSP_INITIATOR        RT_BIT(6)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_SATA                 RT_BIT(7)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_SMP_TARGET           RT_BIT(8)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_STP_TARGET           RT_BIT(9)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_SSP_TARGET           RT_BIT(10)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_DIRECT_ATTACHED      RT_BIT(11)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_LSI                  RT_BIT(12)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_ATAPI_DEVICE         RT_BIT(13)
#define LSILOGICSCSI_SASIOUNIT0_DEVICE_SEP_DEVICE           RT_BIT(14)

#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_LOOP            RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_UNADDRESSABLE   RT_BIT(1)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_SAME_SAS_ADDR   RT_BIT(2)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_EXPANDER_ERROR  RT_BIT(3)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_SMP_TIMEOUT     RT_BIT(4)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_EXP_ROUTE_OOE   RT_BIT(5)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_EXP_ROUTE_IDX   RT_BIT(6)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_SMP_FUNC_FAILED RT_BIT(7)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_SMP_CRC_ERROR   RT_BIT(8)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_SUBTRSCTIVE_LNK RT_BIT(9)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_TBL_LNK         RT_BIT(10)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_UNSUPPORTED_DEV RT_BIT(11)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_MAX_SATA_TGTS   RT_BIT(12)
#define LSILOGICSCSI_SASIOUNIT0_DISCOVERY_STATUS_MULT_CTRLS      RT_BIT(13)

/**
 * PHY entry for the SAS I/O unit page 1
 */
typedef struct MptConfigurationPageSASIOUnit1PHY
{
    /** Port number */
    uint8_t                           u8Port;
    /** Port flags */
    uint8_t                           u8PortFlags;
    /** Phy flags */
    uint8_t                           u8PhyFlags;
    /** Max link rate */
    uint8_t                           u8MaxMinLinkRate;
    /** Controller phy device info */
    uint32_t                          u32ControllerPhyDeviceInfo;
    /** Maximum target port connect time */
    uint16_t                          u16MaxTargetPortConnectTime;
    /** Reserved */
    uint16_t                          u16Reserved;
} MptConfigurationPageSASIOUnit1PHY, *PMptConfigurationPageSASIOUnit1PHY;
AssertCompileSize(MptConfigurationPageSASIOUnit1PHY, 12);

/**
 * SAS I/O  Unit page 1 - Read/Write
 */
typedef struct MptConfigurationPageSASIOUnit1
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Control flags */
            uint16_t                              u16ControlFlags;
            /** maximum number of SATA targets */
            uint16_t                              u16MaxNumSATATargets;
            /** additional control flags */
            uint16_t                              u16AdditionalControlFlags;
            /** Reserved */
            uint16_t                              u16Reserved;
            /** Number of PHYs */
            uint8_t                               u8NumPhys;
            /** maximum SATA queue depth */
            uint8_t                               u8SATAMaxQDepth;
            /** Delay for reporting missing devices. */
            uint8_t                               u8ReportDeviceMissingDelay;
            /** I/O device missing delay */
            uint8_t                               u8IODeviceMissingDelay;
            /** Content for each physical port - variable depending on the number of ports */
            MptConfigurationPageSASIOUnit1PHY     aPHY[1];
        } fields;
    } u;
} MptConfigurationPageSASIOUnit1, *PMptConfigurationPageSASIOUnit1;
AssertCompileSize(MptConfigurationPageSASIOUnit1, 8+12+sizeof(MptConfigurationPageSASIOUnit1PHY));

#define LSILOGICSCSI_SASIOUNIT1_GET_SIZE(ports) (sizeof(MptConfigurationPageSASIOUnit1) + ((ports) - 1) * sizeof(MptConfigurationPageSASIOUnit1PHY))

#define LSILOGICSCSI_SASIOUNIT1_CONTROL_CLEAR_SATA_AFFILIATION     RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_FIRST_LEVEL_DISCOVERY_ONLY RT_BIT(1)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SUBTRACTIVE_LNK_ILLEGAL    RT_BIT(2)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_IOC_ENABLE_HIGH_PHY        RT_BIT(3)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_FUA_REQUIRED          RT_BIT(4)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_NCQ_REQUIRED          RT_BIT(5)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_SMART_REQUIRED        RT_BIT(6)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_LBA48_REQUIRED        RT_BIT(7)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_INIT_POSTPONED        RT_BIT(8)

#define LSILOGICSCSI_SASIOUNIT1_CONTROL_DEVICE_SUPPORT_SET(x)       (((x) & 0x3) << 9)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_DEVICE_SUPPORT_GET(x)       (((x) >> 9) & 0x3)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_DEVICE_SUPPORT_SAS_AND_SATA 0x00
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_DEVICE_SUPPORT_SAS          0x01
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_DEVICE_SUPPORT_SATA         0x02

#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_EXP_ADDR                  RT_BIT(11)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_SETTINGS_PRESERV_REQUIRED RT_BIT(12)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_LIMIT_RATE_15GB           RT_BIT(13)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SATA_LIMIT_RATE_30GB           RT_BIT(14)
#define LSILOGICSCSI_SASIOUNIT1_CONTROL_SAS_SELF_TEST_ENABLED          RT_BIT(15)

#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_TBL_LNKS_ALLOW             RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_SATA_RST_NO_AFFIL          RT_BIT(1)
#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_SATA_RST_SELF_AFFIL        RT_BIT(2)
#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_SATA_RST_OTHER_AFFIL       RT_BIT(3)
#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_SATA_RST_PORT_EN_ONLY      RT_BIT(4)
#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_HIDE_NON_ZERO_PHYS         RT_BIT(5)
#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_SATA_ASYNC_NOTIF           RT_BIT(6)
#define LSILOGICSCSI_SASIOUNIT1_ADDITIONAL_CONTROL_MULT_PORTS_ILL_SAME_DOMAIN RT_BIT(7)

#define LSILOGICSCSI_SASIOUNIT1_MISSING_DEVICE_DELAY_UNITS_16_SEC             RT_BIT(7)
#define LSILOGICSCSI_SASIOUNIT1_MISSING_DEVICE_DELAY_SET(x)                   ((x) & 0x7F)
#define LSILOGICSCSI_SASIOUNIT1_MISSING_DEVICE_DELAY_GET(x)                   ((x) & 0x7F)

#define LSILOGICSCSI_SASIOUNIT1_PORT_CONFIGURATION_AUTO                       RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT1_PORT_CONFIGURATION_IOC1                       RT_BIT(2)

#define LSILOGICSCSI_SASIOUNIT1_PHY_RX_INVERT                                 RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT1_PHY_TX_INVERT                                 RT_BIT(1)
#define LSILOGICSCSI_SASIOUNIT1_PHY_DISABLE                                   RT_BIT(2)

#define LSILOGICSCSI_SASIOUNIT1_LINK_RATE_MIN_SET(x)                          ((x) & 0x0F)
#define LSILOGICSCSI_SASIOUNIT1_LINK_RATE_MIN_GET(x)                          ((x) & 0x0F)
#define LSILOGICSCSI_SASIOUNIT1_LINK_RATE_MAX_SET(x)                          (((x) & 0x0F) << 4)
#define LSILOGICSCSI_SASIOUNIT1_LINK_RATE_MAX_GET(x)                          ((x >> 4) & 0x0F)
#define LSILOGICSCSI_SASIOUNIT1_LINK_RATE_15GB                                0x8
#define LSILOGICSCSI_SASIOUNIT1_LINK_RATE_30GB                                0x9

#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_TYPE_SET(x)                    ((x) & 0x3)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_TYPE_GET(x)                    ((x) & 0x3)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_TYPE_NO                        0x0
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_TYPE_END                       0x1
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_TYPE_EDGE_EXPANDER             0x2
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_TYPE_FANOUT_EXPANDER           0x3
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_SMP_INITIATOR                  RT_BIT(4)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_STP_INITIATOR                  RT_BIT(5)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_SSP_INITIATOR                  RT_BIT(6)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_SMP_TARGET                     RT_BIT(8)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_STP_TARGET                     RT_BIT(9)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_SSP_TARGET                     RT_BIT(10)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_DIRECT_ATTACHED                RT_BIT(11)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_LSI                            RT_BIT(12)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_ATAPI                          RT_BIT(13)
#define LSILOGICSCSI_SASIOUNIT1_CTL_PHY_DEVICE_SEP                            RT_BIT(14)

/**
 * SAS I/O unit page 2 - Read/Write
 */
typedef struct MptConfigurationPageSASIOUnit2
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Device numbers per enclosure */
            uint8_t                               u8NumDevsPerEnclosure;
            /** Boot device wait time */
            uint8_t                               u8BootDeviceWaitTime;
            /** Reserved */
            uint16_t                              u16Reserved;
            /** Maximum number of persistent Bus and target ID mappings */
            uint16_t                              u16MaxPersistentIDs;
            /** Number of persistent IDs used */
            uint16_t                              u16NumPersistentIDsUsed;
            /** Status */
            uint8_t                               u8Status;
            /** Flags */
            uint8_t                               u8Flags;
            /** Maximum number of physical mapped IDs */
            uint16_t                              u16MaxNumPhysicalMappedIDs;
        } fields;
    } u;
} MptConfigurationPageSASIOUnit2, *PMptConfigurationPageSASIOUnit2;
AssertCompileSize(MptConfigurationPageSASIOUnit2, 20);

#define LSILOGICSCSI_SASIOUNIT2_STATUS_PERSISTENT_MAP_TBL_FULL       RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT2_STATUS_PERSISTENT_MAP_DISABLED       RT_BIT(1)
#define LSILOGICSCSI_SASIOUNIT2_STATUS_PERSISTENT_ENC_DEV_UNMAPPED   RT_BIT(2)
#define LSILOGICSCSI_SASIOUNIT2_STATUS_PERSISTENT_DEV_LIMIT_EXCEEDED RT_BIT(3)

#define LSILOGICSCSI_SASIOUNIT2_FLAGS_PERSISTENT_MAP_DISABLE          RT_BIT(0)
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_PERSISTENT_PHYS_MAP_MODE_SET(x) ((x & 0x7) << 1)
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_PERSISTENT_PHYS_MAP_MODE_GET(x) ((x >> 1) & 0x7)
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_PERSISTENT_PHYS_MAP_MODE_NO              0x0
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_PERSISTENT_PHYS_MAP_MODE_DIRECT_ATTACHED 0x1
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_PERSISTENT_PHYS_MAP_MODE_ENC             0x2
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_PERSISTENT_PHYS_MAP_MODE_HOST            0x7
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_RESERVE_TARGET_ID_ZERO          RT_BIT(4)
#define LSILOGICSCSI_SASIOUNIT2_FLAGS_START_SLOT_NUMBER_ONE           RT_BIT(5)

/**
 * SAS I/O unit page 3 - Read/Write
 */
typedef struct MptConfigurationPageSASIOUnit3
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Reserved */
            uint32_t                              u32Reserved;
            uint32_t                              u32MaxInvalidDwordCount;
            uint32_t                              u32InvalidDwordCountTime;
            uint32_t                              u32MaxRunningDisparityErrorCount;
            uint32_t                              u32RunningDisparityErrorTime;
            uint32_t                              u32MaxLossDwordSynchCount;
            uint32_t                              u32LossDwordSynchCountTime;
            uint32_t                              u32MaxPhysResetProblemCount;
            uint32_t                              u32PhyResetProblemTime;
        } fields;
    } u;
} MptConfigurationPageSASIOUnit3, *PMptConfigurationPageSASIOUnit3;
AssertCompileSize(MptConfigurationPageSASIOUnit3, 44);

/**
 * SAS PHY page 0 - Readonly
 */
#pragma pack(1) /* SASAddress starts at offset 12, which isn't typically natural for uint64_t (inside it). */
typedef struct MptConfigurationPageSASPHY0
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Owner dev handle. */
            uint16_t                              u16OwnerDevHandle;
            /** Reserved */
            uint16_t                              u16Reserved0;
            /** SAS address */
            SASADDRESS                            SASAddress;
            /** Attached device handle */
            uint16_t                              u16AttachedDevHandle;
            /** Attached phy identifier */
            uint8_t                               u8AttachedPhyIdentifier;
            /** Reserved */
            uint8_t                               u8Reserved1;
            /** Attached device information */
            uint32_t                              u32AttachedDeviceInfo;
            /** Programmed link rate */
            uint8_t                               u8ProgrammedLinkRate;
            /** Hardware link rate */
            uint8_t                               u8HwLinkRate;
            /** Change count */
            uint8_t                               u8ChangeCount;
            /** Flags */
            uint8_t                               u8Flags;
            /** Phy information */
            uint32_t                              u32PhyInfo;
        } fields;
    } u;
} MptConfigurationPageSASPHY0, *PMptConfigurationPageSASPHY0;
#pragma pack()
AssertCompileSize(MptConfigurationPageSASPHY0, 36);

#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_TYPE_SET(x)              ((x) & 0x3)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_TYPE_GET(x)              ((x) & 0x3)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_TYPE_NO                  0x0
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_TYPE_END                 0x1
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_TYPE_EDGE_EXPANDER       0x2
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_TYPE_FANOUT_EXPANDER     0x3
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_SMP_INITIATOR            RT_BIT(4)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_STP_INITIATOR            RT_BIT(5)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_SSP_INITIATOR            RT_BIT(6)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_SMP_TARGET               RT_BIT(8)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_STP_TARGET               RT_BIT(9)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_SSP_TARGET               RT_BIT(10)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_DIRECT_ATTACHED          RT_BIT(11)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_LSI                      RT_BIT(12)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_ATAPI                    RT_BIT(13)
#define LSILOGICSCSI_SASPHY0_DEV_INFO_DEVICE_SEP                      RT_BIT(14)

/**
 * SAS PHY page 1 - Readonly
 */
typedef struct MptConfigurationPageSASPHY1
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Reserved */
            uint32_t                              u32Reserved0;
            uint32_t                              u32InvalidDwordCound;
            uint32_t                              u32RunningDisparityErrorCount;
            uint32_t                              u32LossDwordSynchCount;
            uint32_t                              u32PhyResetProblemCount;
        } fields;
    } u;
} MptConfigurationPageSASPHY1, *PMptConfigurationPageSASPHY1;
AssertCompileSize(MptConfigurationPageSASPHY1, 28);

/**
 * SAS Device page 0 - Readonly
 */
#pragma pack(1) /* SASAddress starts at offset 12, which isn't typically natural for uint64_t (inside it). */
typedef struct MptConfigurationPageSASDevice0
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Slot number */
            uint16_t                              u16Slot;
            /** Enclosure handle. */
            uint16_t                              u16EnclosureHandle;
            /** SAS address */
            SASADDRESS                            SASAddress;
            /** Parent device handle */
            uint16_t                              u16ParentDevHandle;
            /** Phy number */
            uint8_t                               u8PhyNum;
            /** Access status */
            uint8_t                               u8AccessStatus;
            /** Device handle */
            uint16_t                              u16DevHandle;
            /** Target ID */
            uint8_t                               u8TargetID;
            /** Bus */
            uint8_t                               u8Bus;
            /** Device info */
            uint32_t                              u32DeviceInfo;
            /** Flags */
            uint16_t                              u16Flags;
            /** Physical port */
            uint8_t                               u8PhysicalPort;
            /** Reserved */
            uint8_t                               u8Reserved0;
        } fields;
    } u;
} MptConfigurationPageSASDevice0, *PMptConfigurationPageSASDevice0;
#pragma pack()
AssertCompileSize(MptConfigurationPageSASDevice0, 36);

#define LSILOGICSCSI_SASDEVICE0_STATUS_NO_ERRORS                         (0x00)

#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_TYPE_SET(x)              ((x) & 0x3)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_TYPE_GET(x)              ((x) & 0x3)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_TYPE_NO                  0x0
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_TYPE_END                 0x1
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_TYPE_EDGE_EXPANDER       0x2
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_TYPE_FANOUT_EXPANDER     0x3
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_SMP_INITIATOR            RT_BIT(4)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_STP_INITIATOR            RT_BIT(5)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_SSP_INITIATOR            RT_BIT(6)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_SMP_TARGET               RT_BIT(8)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_STP_TARGET               RT_BIT(9)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_SSP_TARGET               RT_BIT(10)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_DIRECT_ATTACHED          RT_BIT(11)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_LSI                      RT_BIT(12)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_ATAPI                    RT_BIT(13)
#define LSILOGICSCSI_SASDEVICE0_DEV_INFO_DEVICE_SEP                      RT_BIT(14)

#define LSILOGICSCSI_SASDEVICE0_FLAGS_DEVICE_PRESENT                     (RT_BIT(0))
#define LSILOGICSCSI_SASDEVICE0_FLAGS_DEVICE_MAPPED_TO_BUS_AND_TARGET_ID (RT_BIT(1))
#define LSILOGICSCSI_SASDEVICE0_FLAGS_DEVICE_MAPPING_PERSISTENT          (RT_BIT(2))

/**
 * SAS Device page 1 - Readonly
 */
#pragma pack(1) /* SASAddress starts at offset 12, which isn't typically natural for uint64_t (inside it). */
typedef struct MptConfigurationPageSASDevice1
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Reserved */
            uint32_t                              u32Reserved0;
            /** SAS address */
            SASADDRESS                            SASAddress;
            /** Reserved */
            uint32_t                              u32Reserved;
            /** Device handle */
            uint16_t                              u16DevHandle;
            /** Target ID */
            uint8_t                               u8TargetID;
            /** Bus */
            uint8_t                               u8Bus;
            /** Initial REgister device FIS */
            uint32_t                              au32InitialRegDeviceFIS[5];
        } fields;
    } u;
} MptConfigurationPageSASDevice1, *PMptConfigurationPageSASDevice1;
#pragma pack()
AssertCompileSize(MptConfigurationPageSASDevice1, 48);

/**
 * SAS Device page 2 - Read/Write persistent
 */
#pragma pack(1) /* Because of a uint64_t inside SASAddress, the struct size would be 24 without packing. */
typedef struct MptConfigurationPageSASDevice2
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Physical identifier */
            SASADDRESS                            SASAddress;
            /** Enclosure mapping */
            uint32_t                              u32EnclosureMapping;
        } fields;
    } u;
} MptConfigurationPageSASDevice2, *PMptConfigurationPageSASDevice2;
#pragma pack()
AssertCompileSize(MptConfigurationPageSASDevice2, 20);

/**
 * A device entitiy containing all pages.
 */
typedef struct MptSASDevice
{
    /** Pointer to the next device if any. */
    struct MptSASDevice            *pNext;
    /** Pointer to the previous device if any. */
    struct MptSASDevice            *pPrev;

    MptConfigurationPageSASDevice0  SASDevicePage0;
    MptConfigurationPageSASDevice1  SASDevicePage1;
    MptConfigurationPageSASDevice2  SASDevicePage2;
} MptSASDevice, *PMptSASDevice;

/**
 * SAS Expander page 0 - Readonly
 */
#pragma pack(1) /* SASAddress starts at offset 12, which isn't typically natural for uint64_t (inside it). */
typedef struct MptConfigurationPageSASExpander0
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Physical port */
            uint8_t                               u8PhysicalPort;
            /** Reserved */
            uint8_t                               u8Reserved0;
            /** Enclosure handle */
            uint16_t                              u16EnclosureHandle;
            /** SAS address */
            SASADDRESS                            SASAddress;
            /** Discovery status */
            uint32_t                              u32DiscoveryStatus;
            /** Device handle. */
            uint16_t                              u16DevHandle;
            /** Parent device handle */
            uint16_t                              u16ParentDevHandle;
            /** Expander change count */
            uint16_t                              u16ExpanderChangeCount;
            /** Expander route indexes */
            uint16_t                              u16ExpanderRouteIndexes;
            /** Number of PHys in this expander */
            uint8_t                               u8NumPhys;
            /** SAS level */
            uint8_t                               u8SASLevel;
            /** Flags */
            uint8_t                               u8Flags;
            /** Reserved */
            uint8_t                               u8Reserved1;
        } fields;
    } u;
} MptConfigurationPageSASExpander0, *PMptConfigurationPageSASExpander0;
#pragma pack()
AssertCompileSize(MptConfigurationPageSASExpander0, 36);

/**
 * SAS Expander page 1 - Readonly
 */
typedef struct MptConfigurationPageSASExpander1
{
    /** Union. */
    union
    {
        /** Byte view - variable. */
        uint8_t                                   abPageData[1];
        /** Field view. */
        struct
        {
            /** The omnipresent header. */
            MptExtendedConfigurationPageHeader    ExtHeader;
            /** Physical port */
            uint8_t                               u8PhysicalPort;
            /** Reserved */
            uint8_t                               u8Reserved0[3];
            /** Number of PHYs */
            uint8_t                               u8NumPhys;
            /** Number of the Phy the information in this page is for. */
            uint8_t                               u8Phy;
            /** Number of routing table entries */
            uint16_t                              u16NumTableEntriesProgrammed;
            /** Programmed link rate */
            uint8_t                               u8ProgrammedLinkRate;
            /** Hardware link rate */
            uint8_t                               u8HwLinkRate;
            /** Attached device handle */
            uint16_t                              u16AttachedDevHandle;
            /** Phy information */
            uint32_t                              u32PhyInfo;
            /** Attached device information */
            uint32_t                              u32AttachedDeviceInfo;
            /** Owner device handle. */
            uint16_t                              u16OwnerDevHandle;
            /** Change count */
            uint8_t                               u8ChangeCount;
            /** Negotiated link rate */
            uint8_t                               u8NegotiatedLinkRate;
            /** Phy identifier */
            uint8_t                               u8PhyIdentifier;
            /** Attached phy identifier */
            uint8_t                               u8AttachedPhyIdentifier;
            /** Reserved */
            uint8_t                               u8Reserved1;
            /** Discovery information */
            uint8_t                               u8DiscoveryInfo;
            /** Reserved */
            uint32_t                              u32Reserved;
        } fields;
    } u;
} MptConfigurationPageSASExpander1, *PMptConfigurationPageSASExpander1;
AssertCompileSize(MptConfigurationPageSASExpander1, 40);

/**
 * Structure of all supported pages for the SCSI SPI controller.
 * Used to load the device state from older versions.
 */
typedef struct MptConfigurationPagesSupported_SSM_V2
{
    MptConfigurationPageManufacturing0 ManufacturingPage0;
    MptConfigurationPageManufacturing1 ManufacturingPage1;
    MptConfigurationPageManufacturing2 ManufacturingPage2;
    MptConfigurationPageManufacturing3 ManufacturingPage3;
    MptConfigurationPageManufacturing4 ManufacturingPage4;
    MptConfigurationPageIOUnit0        IOUnitPage0;
    MptConfigurationPageIOUnit1        IOUnitPage1;
    MptConfigurationPageIOUnit2        IOUnitPage2;
    MptConfigurationPageIOUnit3        IOUnitPage3;
    MptConfigurationPageIOC0           IOCPage0;
    MptConfigurationPageIOC1           IOCPage1;
    MptConfigurationPageIOC2           IOCPage2;
    MptConfigurationPageIOC3           IOCPage3;
    MptConfigurationPageIOC4           IOCPage4;
    MptConfigurationPageIOC6           IOCPage6;
    struct
    {
        MptConfigurationPageSCSISPIPort0   SCSISPIPortPage0;
        MptConfigurationPageSCSISPIPort1   SCSISPIPortPage1;
        MptConfigurationPageSCSISPIPort2   SCSISPIPortPage2;
    } aPortPages[1]; /* Currently only one port supported. */
    struct
    {
        struct
        {
            MptConfigurationPageSCSISPIDevice0 SCSISPIDevicePage0;
            MptConfigurationPageSCSISPIDevice1 SCSISPIDevicePage1;
            MptConfigurationPageSCSISPIDevice2 SCSISPIDevicePage2;
            MptConfigurationPageSCSISPIDevice3 SCSISPIDevicePage3;
        } aDevicePages[LSILOGICSCSI_PCI_SPI_DEVICES_MAX];
    } aBuses[1]; /* Only one bus at the moment. */
} MptConfigurationPagesSupported_SSM_V2, *PMptConfigurationPagesSupported_SSM_V2;

typedef struct MptConfigurationPagesSpi
{
    struct
    {
        MptConfigurationPageSCSISPIPort0   SCSISPIPortPage0;
        MptConfigurationPageSCSISPIPort1   SCSISPIPortPage1;
        MptConfigurationPageSCSISPIPort2   SCSISPIPortPage2;
    } aPortPages[1]; /* Currently only one port supported. */
    struct
    {
        struct
        {
            MptConfigurationPageSCSISPIDevice0 SCSISPIDevicePage0;
            MptConfigurationPageSCSISPIDevice1 SCSISPIDevicePage1;
            MptConfigurationPageSCSISPIDevice2 SCSISPIDevicePage2;
            MptConfigurationPageSCSISPIDevice3 SCSISPIDevicePage3;
        } aDevicePages[LSILOGICSCSI_PCI_SPI_DEVICES_MAX];
    } aBuses[1]; /* Only one bus at the moment. */
} MptConfigurationPagesSpi, *PMptConfigurationPagesSpi;

typedef struct MptPHY
{
    MptConfigurationPageSASPHY0     SASPHYPage0;
    MptConfigurationPageSASPHY1     SASPHYPage1;
} MptPHY, *PMptPHY;

typedef struct MptConfigurationPagesSas
{
    /** Pointer to the manufacturing page 7 */
    PMptConfigurationPageManufacturing7 pManufacturingPage7;
    /** Size of the manufacturing page 7 */
    uint32_t                            cbManufacturingPage7;
    /** Size of the I/O unit page 0 */
    uint32_t                            cbSASIOUnitPage0;
    /** Pointer to the I/O unit page 0 */
    PMptConfigurationPageSASIOUnit0     pSASIOUnitPage0;
    /** Pointer to the I/O unit page 1 */
    PMptConfigurationPageSASIOUnit1     pSASIOUnitPage1;
    /** Size of the I/O unit page 1 */
    uint32_t                            cbSASIOUnitPage1;
    /** I/O unit page 2 */
    MptConfigurationPageSASIOUnit2      SASIOUnitPage2;
    /** I/O unit page 3 */
    MptConfigurationPageSASIOUnit3      SASIOUnitPage3;

    /** Number of PHYs in the array. */
    uint32_t                            cPHYs;
    /** Pointer to an array of per PHYS pages. */
    R3PTRTYPE(PMptPHY)                  paPHYs;

    /** Number of devices detected. */
    uint32_t                            cDevices;
    uint32_t                            u32Padding;
    /** Pointer to the first SAS device. */
    R3PTRTYPE(PMptSASDevice)            pSASDeviceHead;
    /** Pointer to the last SAS device. */
    R3PTRTYPE(PMptSASDevice)            pSASDeviceTail;
} MptConfigurationPagesSas, *PMptConfigurationPagesSas;
AssertCompile(RTASSERT_OFFSET_OF(MptConfigurationPagesSas,cbSASIOUnitPage0) + 4 == RTASSERT_OFFSET_OF(MptConfigurationPagesSas, pSASIOUnitPage0));
AssertCompile(RTASSERT_OFFSET_OF(MptConfigurationPagesSas,cPHYs)            + 4 == RTASSERT_OFFSET_OF(MptConfigurationPagesSas, paPHYs));
AssertCompile(RTASSERT_OFFSET_OF(MptConfigurationPagesSas,cDevices)         + 8 == RTASSERT_OFFSET_OF(MptConfigurationPagesSas, pSASDeviceHead));


/**
 * Structure of all supported pages for both controllers.
 */
typedef struct MptConfigurationPagesSupported
{
    MptConfigurationPageManufacturing0  ManufacturingPage0;
    MptConfigurationPageManufacturing1  ManufacturingPage1;
    MptConfigurationPageManufacturing2  ManufacturingPage2;
    MptConfigurationPageManufacturing3  ManufacturingPage3;
    MptConfigurationPageManufacturing4  ManufacturingPage4;
    MptConfigurationPageManufacturing5  ManufacturingPage5;
    MptConfigurationPageManufacturing6  ManufacturingPage6;
    MptConfigurationPageManufacturing8  ManufacturingPage8;
    MptConfigurationPageManufacturing9  ManufacturingPage9;
    MptConfigurationPageManufacturing10 ManufacturingPage10;
    MptConfigurationPageIOUnit0         IOUnitPage0;
    MptConfigurationPageIOUnit1         IOUnitPage1;
    MptConfigurationPageIOUnit2         IOUnitPage2;
    MptConfigurationPageIOUnit3         IOUnitPage3;
    MptConfigurationPageIOUnit4         IOUnitPage4;
    MptConfigurationPageIOC0            IOCPage0;
    MptConfigurationPageIOC1            IOCPage1;
    MptConfigurationPageIOC2            IOCPage2;
    MptConfigurationPageIOC3            IOCPage3;
    MptConfigurationPageIOC4            IOCPage4;
    MptConfigurationPageIOC6            IOCPage6;
    /* BIOS page 0 is not described */
    MptConfigurationPageBIOS1           BIOSPage1;
    MptConfigurationPageBIOS2           BIOSPage2;
    /* BIOS page 3 is not described */
    MptConfigurationPageBIOS4           BIOSPage4;

    /** Controller dependent data. */
    union
    {
        MptConfigurationPagesSpi        SpiPages;
        MptConfigurationPagesSas        SasPages;
    } u;
} MptConfigurationPagesSupported, *PMptConfigurationPagesSupported;

/**
 * Initializes a page header.
 */
#define MPT_CONFIG_PAGE_HEADER_INIT(pg, type, nr, flags) \
    (pg)->u.fields.Header.u8PageType   = (flags); \
    (pg)->u.fields.Header.u8PageNumber = (nr); \
    (pg)->u.fields.Header.u8PageLength = sizeof(type) / 4

#define MPT_CONFIG_PAGE_HEADER_INIT_MANUFACTURING(pg, type, nr, flags) \
    RT_ZERO(*pg); \
    MPT_CONFIG_PAGE_HEADER_INIT(pg, type, nr, flags | MPT_CONFIGURATION_PAGE_TYPE_MANUFACTURING)

#define MPT_CONFIG_PAGE_HEADER_INIT_IO_UNIT(pg, type, nr, flags) \
    RT_ZERO(*pg); \
    MPT_CONFIG_PAGE_HEADER_INIT(pg, type, nr, flags | MPT_CONFIGURATION_PAGE_TYPE_IO_UNIT)

#define MPT_CONFIG_PAGE_HEADER_INIT_IOC(pg, type, nr, flags) \
    RT_ZERO(*pg); \
    MPT_CONFIG_PAGE_HEADER_INIT(pg, type, nr, flags | MPT_CONFIGURATION_PAGE_TYPE_IOC)

#define MPT_CONFIG_PAGE_HEADER_INIT_BIOS(pg, type, nr, flags) \
    RT_ZERO(*pg); \
    MPT_CONFIG_PAGE_HEADER_INIT(pg, type, nr, flags | MPT_CONFIGURATION_PAGE_TYPE_BIOS)

/**
 * Initializes a extended page header.
 */
#define MPT_CONFIG_EXTENDED_PAGE_HEADER_INIT(pg, cb, nr, flags, exttype) \
    RT_BZERO(pg, cb); \
    (pg)->u.fields.ExtHeader.u8PageType   = (flags) | MPT_CONFIGURATION_PAGE_TYPE_EXTENDED; \
    (pg)->u.fields.ExtHeader.u8PageNumber = (nr); \
    (pg)->u.fields.ExtHeader.u8ExtPageType = (exttype); \
    (pg)->u.fields.ExtHeader.u16ExtPageLength = (cb) / 4

/**
 * Possible SG element types.
 */
enum MPTSGENTRYTYPE
{
    MPTSGENTRYTYPE_TRANSACTION_CONTEXT = 0x00,
    MPTSGENTRYTYPE_SIMPLE              = 0x01,
    MPTSGENTRYTYPE_CHAIN               = 0x03
};

/**
 * Register interface.
 */

/**
 * Defined states that the SCSI controller can have.
 */
typedef enum LSILOGICSTATE
{
    /** Reset state. */
    LSILOGICSTATE_RESET       = 0x00,
    /** Ready state. */
    LSILOGICSTATE_READY       = 0x01,
    /** Operational state. */
    LSILOGICSTATE_OPERATIONAL = 0x02,
    /** Fault state. */
    LSILOGICSTATE_FAULT       = 0x04,
    /** 32bit size hack */
    LSILOGICSTATE_32BIT_HACK  = 0x7fffffff
} LSILOGICSTATE;

/**
 * Which entity needs to initialize the controller
 * to get into the operational state.
 */
typedef enum LSILOGICWHOINIT
{
    /** Not initialized. */
    LSILOGICWHOINIT_NOT_INITIALIZED = 0x00,
    /** System BIOS. */
    LSILOGICWHOINIT_SYSTEM_BIOS     = 0x01,
    /** ROM Bios. */
    LSILOGICWHOINIT_ROM_BIOS        = 0x02,
    /** PCI Peer. */
    LSILOGICWHOINIT_PCI_PEER        = 0x03,
    /** Host driver. */
    LSILOGICWHOINIT_HOST_DRIVER     = 0x04,
    /** Manufacturing. */
    LSILOGICWHOINIT_MANUFACTURING   = 0x05,
    /** 32bit size hack. */
    LSILOGICWHOINIT_32BIT_HACK      = 0x7fffffff
} LSILOGICWHOINIT;


/**
 * Doorbell state.
 */
typedef enum LSILOGICDOORBELLSTATE
{
    /** Invalid value. */
    LSILOGICDOORBELLSTATE_INVALID = 0,
    /** Doorbell not in use. */
    LSILOGICDOORBELLSTATE_NOT_IN_USE,
    /** Reply frame removal, transfer number of entries, low 16bits. */
    LSILOGICDOORBELLSTATE_RFR_FRAME_COUNT_LOW,
    /** Reply frame removal, transfer number of entries, high 16bits. */
    LSILOGICDOORBELLSTATE_RFR_FRAME_COUNT_HIGH,
    /** Reply frame removal, remove next free frame, low part. */
    LSILOGICDOORBELLSTATE_RFR_NEXT_FRAME_LOW,
    /** Reply frame removal, remove next free frame, high part. */
    LSILOGICDOORBELLSTATE_RFR_NEXT_FRAME_HIGH,
    /** Function handshake. */
    LSILOGICDOORBELLSTATE_FN_HANDSHAKE,
    /** 32bit hack. */
    LSILOGICDOORBELLSTATE_32BIT_HACK = 0x7fffffff
} LSILOGICDOORBELLSTATE;
/** Pointer to a doorbell state. */
typedef LSILOGICDOORBELLSTATE *PLSILOGICDOORBELLSTATE;


/**
 * IOC status codes.
 */
#define LSILOGIC_IOCSTATUS_SUCCESS                0x0000
#define LSILOGIC_IOCSTATUS_INVALID_FUNCTION       0x0001
#define LSILOGIC_IOCSTATUS_BUSY                   0x0002
#define LSILOGIC_IOCSTATUS_INVALID_SGL            0x0003
#define LSILOGIC_IOCSTATUS_INTERNAL_ERROR         0x0004
#define LSILOGIC_IOCSTATUS_RESERVED               0x0005
#define LSILOGIC_IOCSTATUS_INSUFFICIENT_RESOURCES 0x0006
#define LSILOGIC_IOCSTATUS_INVALID_FIELD          0x0007
#define LSILOGIC_IOCSTATUS_INVALID_STATE          0x0008
#define LSILOGIC_IOCSTATUS_OP_STATE_NOT_SUPPOTED  0x0009

/**
 * Size of the I/O and MMIO space.
 */
#define LSILOGIC_PCI_SPACE_IO_SIZE  256
#define LSILOGIC_PCI_SPACE_MEM_SIZE 128 * _1K

/**
 * Doorbell register - Used to get the status of the controller and
 * initialise it.
 */
#define LSILOGIC_REG_DOORBELL 0x00
# define LSILOGIC_REG_DOORBELL_SET_STATE(enmState)     (((enmState) & 0x0f) << 28)
# define LSILOGIC_REG_DOORBELL_SET_USED(enmDoorbell)   (((enmDoorbell != LSILOGICDOORBELLSTATE_NOT_IN_USE) ? 1 : 0) << 27)
# define LSILOGIC_REG_DOORBELL_SET_WHOINIT(enmWhoInit) (((enmWhoInit) & 0x07) << 24)
# define LSILOGIC_REG_DOORBELL_SET_FAULT_CODE(u16Code) (u16Code)
# define LSILOGIC_REG_DOORBELL_GET_FUNCTION(x)         (((x) & 0xff000000) >> 24)
# define LSILOGIC_REG_DOORBELL_GET_SIZE(x)             (((x) & 0x00ff0000) >> 16)

/**
 * Functions which can be passed through the system doorbell.
 */
#define LSILOGIC_DOORBELL_FUNCTION_IOC_MSG_UNIT_RESET  0x40
#define LSILOGIC_DOORBELL_FUNCTION_IO_UNIT_RESET       0x41
#define LSILOGIC_DOORBELL_FUNCTION_HANDSHAKE           0x42
#define LSILOGIC_DOORBELL_FUNCTION_REPLY_FRAME_REMOVAL 0x43

/**
 * Write sequence register for the diagnostic register.
 */
#define LSILOGIC_REG_WRITE_SEQUENCE    0x04

/**
 * Diagnostic register - used to reset the controller.
 */
#define LSILOGIC_REG_HOST_DIAGNOSTIC   0x08
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DIAG_MEM_ENABLE     (RT_BIT(0))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DISABLE_ARM         (RT_BIT(1))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_RESET_ADAPTER       (RT_BIT(2))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DIAG_RW_ENABLE      (RT_BIT(4))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_RESET_HISTORY       (RT_BIT(5))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_FLASH_BAD_SIG       (RT_BIT(6))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DRWE                (RT_BIT(7))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_PREVENT_IOC_BOOT    (RT_BIT(9))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_CLEAR_FLASH_BAD_SIG (RT_BIT(10))

#define LSILOGIC_REG_TEST_BASE_ADDRESS 0x0c
#define LSILOGIC_REG_DIAG_RW_DATA      0x10
#define LSILOGIC_REG_DIAG_RW_ADDRESS   0x14

/**
 * Interrupt status register.
 */
#define LSILOGIC_REG_HOST_INTR_STATUS  0x30
# define LSILOGIC_REG_HOST_INTR_STATUS_W_MASK (RT_BIT(3))
# define LSILOGIC_REG_HOST_INTR_STATUS_DOORBELL_STS    (RT_BIT(31))
# define LSILOGIC_REG_HOST_INTR_STATUS_REPLY_INTR      (RT_BIT(3))
# define LSILOGIC_REG_HOST_INTR_STATUS_SYSTEM_DOORBELL (RT_BIT(0))

/**
 * Interrupt mask register.
 */
#define LSILOGIC_REG_HOST_INTR_MASK    0x34
# define LSILOGIC_REG_HOST_INTR_MASK_W_MASK (RT_BIT(0) | RT_BIT(3) | RT_BIT(8) | RT_BIT(9))
# define LSILOGIC_REG_HOST_INTR_MASK_IRQ_ROUTING (RT_BIT(8) | RT_BIT(9))
# define LSILOGIC_REG_HOST_INTR_MASK_DOORBELL RT_BIT(0)
# define LSILOGIC_REG_HOST_INTR_MASK_REPLY    RT_BIT(3)

/**
 * Queue registers.
 */
#define LSILOGIC_REG_REQUEST_QUEUE     0x40
#define LSILOGIC_REG_REPLY_QUEUE       0x44

#endif /* !VBOX_INCLUDED_SRC_Storage_DevLsiLogicSCSI_h */
