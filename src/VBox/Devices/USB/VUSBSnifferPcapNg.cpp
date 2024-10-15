/* $Id: VUSBSnifferPcapNg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB Sniffer facility - PCAP-NG format writer.
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
#define LOG_GROUP LOG_GROUP_DRV_VUSB
#include <VBox/log.h>
#include <iprt/buildconfig.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/time.h>

#include "VUSBSnifferInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** DumpFile Section Header Block type. */
#define DUMPFILE_SHB_BLOCK_TYPE       UINT32_C(0x0a0d0d0a)
/** The byte order magic value. */
#define DUMPFILE_SHB_BYTE_ORDER_MAGIC UINT32_C(0x1a2b3c4d)
/** Current major version. */
#define DUMPFILE_SHB_VERSION_MAJOR    UINT16_C(1)
/** Current minor version. */
#define DUMPFILE_SHB_VERSION_MINOR    UINT16_C(0)

/** Block type for the interface descriptor block. */
#define DUMPFILE_IDB_BLOCK_TYPE       UINT32_C(0x00000001)
/** USB link type. */
#define DUMPFILE_IDB_LINK_TYPE_USB_LINUX        UINT16_C(189)
#define DUMPFILE_IDB_LINK_TYPE_USB_LINUX_MMAPED UINT16_C(220)

/** Block type for an enhanced packet block. */
#define DUMPFILE_EPB_BLOCK_TYPE       UINT32_C(0x00000006)

/** USB packet event types. */
#define DUMPFILE_USB_EVENT_TYPE_SUBMIT   ('S')
#define DUMPFILE_USB_EVENT_TYPE_COMPLETE ('C')
#define DUMPFILE_USB_EVENT_TYPE_ERROR    ('E')

#define DUMPFILE_OPTION_CODE_END      UINT16_C(0)
#define DUMPFILE_OPTION_CODE_COMMENT  UINT16_C(1)

#define DUMPFILE_OPTION_CODE_HARDWARE UINT16_C(2)
#define DUMPFILE_OPTION_CODE_OS       UINT16_C(3)
#define DUMPFILE_OPTION_CODE_USERAPP  UINT16_C(4)

#define DUMPFILE_IDB_OPTION_TS_RESOLUTION UINT16_C(9)


/*********************************************************************************************************************************
*   DumpFile format structures                                                                                                   *
*********************************************************************************************************************************/

/**
 * DumpFile Block header.
 */
typedef struct DumpFileBlockHdr
{
    /** Block type. */
    uint32_t            u32BlockType;
    /** Block total length. */
    uint32_t            u32BlockTotalLength;
} DumpFileBlockHdr;
/** Pointer to a block header. */
typedef DumpFileBlockHdr *PDumpFileBlockHdr;

/**
 * DumpFile Option header.
 */
typedef struct DumpFileOptionHdr
{
    /** Option code. */
    uint16_t            u16OptionCode;
    /** Block total length. */
    uint16_t            u16OptionLength;
} DumpFileOptionHdr;
/** Pointer to a option header. */
typedef DumpFileOptionHdr *PDumpFileOptionHdr;

/**
 * DumpFile Section Header Block.
 */
typedef struct DumpFileShb
{
    /** Block header. */
    DumpFileBlockHdr    Hdr;
    /** Byte order magic. */
    uint32_t            u32ByteOrderMagic;
    /** Major version. */
    uint16_t            u16VersionMajor;
    /** Minor version. */
    uint16_t            u16VersionMinor;
    /** Section length. */
    uint64_t            u64SectionLength;
} DumpFileShb;
/** Pointer to a Section Header Block. */
typedef DumpFileShb *PDumpFileShb;

/**
 * DumpFile Interface description block.
 */
typedef struct DumpFileIdb
{
    /** Block header. */
    DumpFileBlockHdr    Hdr;
    /** Link type. */
    uint16_t            u16LinkType;
    /** Reserved. */
    uint16_t            u16Reserved;
    /** Maximum number of bytes dumped from each packet. */
    uint32_t            u32SnapLen;
} DumpFileIdb;
/** Pointer to an Interface description block. */
typedef DumpFileIdb *PDumpFileIdb;

/**
 * DumpFile Enhanced packet block.
 */
typedef struct DumpFileEpb
{
    /** Block header. */
    DumpFileBlockHdr    Hdr;
    /** Interface ID. */
    uint32_t            u32InterfaceId;
    /** Timestamp (high). */
    uint32_t            u32TimestampHigh;
    /** Timestamp (low). */
    uint32_t            u32TimestampLow;
    /** Captured packet length. */
    uint32_t            u32CapturedLen;
    /** Original packet length. */
    uint32_t            u32PacketLen;
} DumpFileEpb;
/** Pointer to an Enhanced packet block. */
typedef DumpFileEpb *PDumpFileEpb;

/**
 * USB setup URB data.
 */
typedef struct DumpFileUsbSetup
{
    uint8_t    bmRequestType;
    uint8_t    bRequest;
    uint16_t   wValue;
    uint16_t   wIndex;
    uint16_t   wLength;
} DumpFileUsbSetup;
typedef DumpFileUsbSetup *PDumpFileUsbSetup;

/**
 * USB Isochronous data.
 */
typedef struct DumpFileIsoRec
{
    int32_t    i32ErrorCount;
    int32_t    i32NumDesc;
} DumpFileIsoRec;
typedef DumpFileIsoRec *PDumpFileIsoRec;

/**
 * USB packet header (Linux mmapped variant).
 */
typedef struct DumpFileUsbHeaderLnxMmapped
{
    /** Packet Id. */
    uint64_t    u64Id;
    /** Event type. */
    uint8_t     u8EventType;
    /** Transfer type. */
    uint8_t     u8TransferType;
    /** Endpoint number. */
    uint8_t     u8EndpointNumber;
    /** Device address. */
    uint8_t     u8DeviceAddress;
    /** Bus id. */
    uint16_t    u16BusId;
    /** Setup flag != 0 if the URB setup header is not present. */
    uint8_t     u8SetupFlag;
    /** Data present flag != 0 if the URB data is not present. */
    uint8_t     u8DataFlag;
    /** Timestamp (second part). */
    uint64_t    u64TimestampSec;
    /** Timestamp (us part). */
    uint32_t    u32TimestampUSec;
    /** Status. */
    int32_t     i32Status;
    /** URB length. */
    uint32_t    u32UrbLength;
    /** Recorded data length. */
    uint32_t    u32DataLength;
    /** Union of data for different URB types. */
    union
    {
        DumpFileUsbSetup    UsbSetup;
        DumpFileIsoRec      IsoRec;
    } u;
    int32_t     i32Interval;
    int32_t     i32StartFrame;
    /** Copy of transfer flags. */
    uint32_t    u32XferFlags;
    /** Number of isochronous descriptors. */
    uint32_t    u32NumDesc;
} DumpFileUsbHeaderLnxMmapped;
/** Pointer to a USB packet header. */
typedef DumpFileUsbHeaderLnxMmapped *PDumpFileUsbHeaderLnxMmapped;

AssertCompileSize(DumpFileUsbHeaderLnxMmapped, 64);

/**
 * USB packet isochronous descriptor.
 */
typedef struct DumpFileUsbIsoDesc
{
    int32_t     i32Status;
    uint32_t    u32Offset;
    uint32_t    u32Len;
    uint8_t     au8Padding[4];
} DumpFileUsbIsoDesc;
typedef DumpFileUsbIsoDesc *PDumpFileUsbIsoDesc;


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
    /** Current size of the block being written. */
    uint32_t          cbBlockCur;
    /** Maximum size allocated for the block. */
    uint32_t          cbBlockMax;
    /** Current block header. */
    PDumpFileBlockHdr pBlockHdr;
    /** Pointer to the block data which will be written on commit. */
    uint8_t          *pbBlockData;
} VUSBSNIFFERFMTINT;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Supported file extensions.
 */
static const char *s_apszFileExts[] =
{
    "pcap",
    "pcapng",
    NULL
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Allocates additional space for the block.
 *
 * @returns Pointer to the new unused space or NULL if out of memory.
 * @param   pThis           The VUSB sniffer instance.
 * @param   cbAdditional    The additional memory requested.
 */
static void *vusbSnifferBlockAllocSpace(PVUSBSNIFFERFMTINT pThis, uint32_t cbAdditional)
{
    /* Fast path where we have enough memory allocated. */
    if (pThis->cbBlockCur + cbAdditional <= pThis->cbBlockMax)
    {
        void *pv = pThis->pbBlockData + pThis->cbBlockCur;
        pThis->cbBlockCur += cbAdditional;
        return pv;
    }

    /* Allocate additional memory. */
    uint32_t cbNew = pThis->cbBlockCur + cbAdditional;
    uint8_t *pbDataNew = (uint8_t *)RTMemRealloc(pThis->pbBlockData, cbNew);
    if (pbDataNew)
    {
        pThis->pbBlockData = pbDataNew;
        pThis->pBlockHdr   = (PDumpFileBlockHdr)pbDataNew;

        void *pv = pThis->pbBlockData + pThis->cbBlockCur;
        pThis->cbBlockCur = cbNew;
        pThis->cbBlockMax = cbNew;
        return pv;
    }

    return NULL;
}

/**
 * Adds new data to the current block.
 *
 * @returns VBox status code.
 * @param   pThis           The VUSB sniffer instance.
 * @param   pvData          The data to add.
 * @param   cbData          Amount of data to add.
 */
static int vusbSnifferBlockAddData(PVUSBSNIFFERFMTINT pThis, const void *pvData, uint32_t cbData)
{
    int rc = VINF_SUCCESS;

    Assert(pThis->cbBlockCur);
    AssertPtr(pThis->pBlockHdr);

    void *pv = vusbSnifferBlockAllocSpace(pThis, cbData);
    if (pv)
        memcpy(pv, pvData, cbData);
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Aligns the current block data to a 32bit boundary.
 *
 * @returns VBox status code.
 * @param   pThis           The VUSB sniffer instance.
 */
static int vusbSnifferBlockAlign(PVUSBSNIFFERFMTINT pThis)
{
    int rc = VINF_SUCCESS;

    Assert(pThis->cbBlockCur);

    /* Pad to 32bits. */
    uint8_t abPad[3] = { 0 };
    uint32_t cbPad = RT_ALIGN_32(pThis->cbBlockCur, 4) - pThis->cbBlockCur;

    Assert(cbPad <= 3);
    if (cbPad)
        rc = vusbSnifferBlockAddData(pThis, abPad, cbPad);

    return rc;
}

/**
 * Commits the current block to the capture file.
 *
 * @returns VBox status code.
 * @param   pThis           The VUSB sniffer instance.
 */
static int vusbSnifferBlockCommit(PVUSBSNIFFERFMTINT pThis)
{
    int rc = VINF_SUCCESS;

    AssertPtr(pThis->pBlockHdr);

    rc = vusbSnifferBlockAlign(pThis);
    if (RT_SUCCESS(rc))
    {
        /* Update the block total length field. */
        uint32_t *pcbTotalLength = (uint32_t *)vusbSnifferBlockAllocSpace(pThis, 4);
        if (pcbTotalLength)
        {
            *pcbTotalLength = pThis->cbBlockCur;
            pThis->pBlockHdr->u32BlockTotalLength = pThis->cbBlockCur;

            /* Write the data. */
            rc = pThis->pStrm->pfnWrite(pThis->pStrm, pThis->pbBlockData, pThis->cbBlockCur);
            pThis->cbBlockCur = 0;
            pThis->pBlockHdr  = NULL;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * Starts a new block for capturing.
 *
 * @returns VBox status code.
 * @param   pThis           The VUSB sniffer instance.
 * @param   pBlockHdr       Pointer to the block header for the new block.
 * @param   cbData          Amount of data added with this block.
 */
static int vusbSnifferBlockNew(PVUSBSNIFFERFMTINT pThis, PDumpFileBlockHdr pBlockHdr, uint32_t cbData)
{
    int rc = VINF_SUCCESS;

    /* Validate we don't get called while another block is active. */
    Assert(!pThis->cbBlockCur);
    Assert(!pThis->pBlockHdr);
    pThis->pBlockHdr = (PDumpFileBlockHdr)vusbSnifferBlockAllocSpace(pThis, cbData);
    if (pThis->pBlockHdr)
        memcpy(pThis->pBlockHdr, pBlockHdr, cbData);
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Add a new option to the current block.
 *
 * @returns VBox status code.
 * @param   pThis           The VUSB sniffer instance.
 * @param   u16OptionCode   The option code identifying the type of option.
 * @param   pvOption        Raw data for the option.
 * @param   cbOption        Size of the optiob data.
 */
static int vusbSnifferAddOption(PVUSBSNIFFERFMTINT pThis, uint16_t u16OptionCode, const void *pvOption, size_t cbOption)
{
    AssertStmt((uint16_t)cbOption == cbOption, cbOption = UINT16_MAX);
    DumpFileOptionHdr OptHdr;
    OptHdr.u16OptionCode   = u16OptionCode;
    OptHdr.u16OptionLength = (uint16_t)cbOption;
    int rc = vusbSnifferBlockAddData(pThis, &OptHdr, sizeof(OptHdr));
    if (   RT_SUCCESS(rc)
        && u16OptionCode != DUMPFILE_OPTION_CODE_END
        && cbOption != 0)
    {
        rc = vusbSnifferBlockAddData(pThis, pvOption, (uint16_t)cbOption);
        if (RT_SUCCESS(rc))
            rc = vusbSnifferBlockAlign(pThis);
    }

    return rc;
}


/** @interface_method_impl{VUSBSNIFFERFMT,pfnInit} */
static DECLCALLBACK(int) vusbSnifferFmtPcapNgInit(PVUSBSNIFFERFMTINT pThis, PVUSBSNIFFERSTRM pStrm)
{
    pThis->pStrm       = pStrm;
    pThis->cbBlockCur  = 0;
    pThis->cbBlockMax  = 0;
    pThis->pbBlockData = NULL;

    /* Write header and link type blocks. */
    DumpFileShb Shb;

    Shb.Hdr.u32BlockType        = DUMPFILE_SHB_BLOCK_TYPE;
    Shb.Hdr.u32BlockTotalLength = 0; /* Filled out by lower layer. */
    Shb.u32ByteOrderMagic       = DUMPFILE_SHB_BYTE_ORDER_MAGIC;
    Shb.u16VersionMajor         = DUMPFILE_SHB_VERSION_MAJOR;
    Shb.u16VersionMinor         = DUMPFILE_SHB_VERSION_MINOR;
    Shb.u64SectionLength        = UINT64_C(0xffffffffffffffff); /* -1 */

    /* Write the blocks. */
    int rc = vusbSnifferBlockNew(pThis, &Shb.Hdr, sizeof(Shb));
    if (RT_SUCCESS(rc))
    {
        const char *pszOpt = RTBldCfgTargetDotArch();
        rc = vusbSnifferAddOption(pThis, DUMPFILE_OPTION_CODE_HARDWARE, pszOpt, strlen(pszOpt) + 1);
    }

    if (RT_SUCCESS(rc))
    {
        char szTmp[512];
        size_t cbTmp = sizeof(szTmp);

        RT_ZERO(szTmp);

        /* Build the OS code. */
        rc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, cbTmp);
        if (RT_SUCCESS(rc))
        {
            size_t cb = strlen(szTmp);

            szTmp[cb] = ' ';
            rc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, &szTmp[cb + 1], cbTmp - (cb + 1));
            if (RT_SUCCESS(rc))
            {
                cb = strlen(szTmp);
                szTmp[cb] = ' ';
                rc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, &szTmp[cb + 1], cbTmp - (cb + 1));
            }
        }

        if (RT_SUCCESS(rc) || rc == VERR_BUFFER_OVERFLOW)
            rc = vusbSnifferAddOption(pThis, DUMPFILE_OPTION_CODE_OS, szTmp, strlen(szTmp) + 1);
        else
            rc = VINF_SUCCESS; /* Skip OS code if building the string failed. */
    }

    if (RT_SUCCESS(rc))
    {
        /** @todo Add product info. */
    }

    if (RT_SUCCESS(rc))
        rc = vusbSnifferAddOption(pThis, DUMPFILE_OPTION_CODE_END, NULL, 0);
    if (RT_SUCCESS(rc))
        rc = vusbSnifferBlockCommit(pThis);

    /* Write Interface descriptor block. */
    if (RT_SUCCESS(rc))
    {
        DumpFileIdb Idb;

        Idb.Hdr.u32BlockType        = DUMPFILE_IDB_BLOCK_TYPE;
        Idb.Hdr.u32BlockTotalLength = 0; /* Filled out by lower layer. */
        Idb.u16LinkType             = DUMPFILE_IDB_LINK_TYPE_USB_LINUX_MMAPED;
        Idb.u16Reserved             = 0;
        Idb.u32SnapLen              = UINT32_C(0xffffffff);

        rc = vusbSnifferBlockNew(pThis, &Idb.Hdr, sizeof(Idb));
        if (RT_SUCCESS(rc))
        {
            uint8_t u8TsResolution = 9; /* Nano second resolution. */
            /* Add timestamp resolution option. */
            rc = vusbSnifferAddOption(pThis, DUMPFILE_IDB_OPTION_TS_RESOLUTION,
                                      &u8TsResolution, sizeof(u8TsResolution));
        }
        if (RT_SUCCESS(rc))
            rc = vusbSnifferAddOption(pThis, DUMPFILE_OPTION_CODE_END, NULL, 0);
        if (RT_SUCCESS(rc))
            rc = vusbSnifferBlockCommit(pThis);
    }

    if (   RT_FAILURE(rc)
        && pThis->pbBlockData)
        RTMemFree(pThis->pbBlockData);

    return rc;
}


/** @interface_method_impl{VUSBSNIFFERFMT,pfnDestroy} */
static DECLCALLBACK(void) vusbSnifferFmtPcapNgDestroy(PVUSBSNIFFERFMTINT pThis)
{
    if (pThis->pbBlockData)
        RTMemFree(pThis->pbBlockData);
}


/** @interface_method_impl{VUSBSNIFFERFMT,pfnRecordEvent} */
static DECLCALLBACK(int) vusbSnifferFmtPcapNgRecordEvent(PVUSBSNIFFERFMTINT pThis, PVUSBURB pUrb, VUSBSNIFFEREVENT enmEvent)
{
    DumpFileEpb Epb;
    DumpFileUsbHeaderLnxMmapped UsbHdr;
    uint32_t cbCapturedLength = sizeof(UsbHdr);
    uint8_t *pbData = NULL;

    RTTIMESPEC TimeNow;
    RTTimeNow(&TimeNow);
    uint64_t u64TimestampEvent = RTTimeSpecGetNano(&TimeNow);

    /* Start with the enhanced packet block. */
    Epb.Hdr.u32BlockType        = DUMPFILE_EPB_BLOCK_TYPE;
    Epb.Hdr.u32BlockTotalLength = 0;
    Epb.u32InterfaceId          = 0;
    Epb.u32TimestampHigh        = (u64TimestampEvent >> 32) & UINT32_C(0xffffffff);
    Epb.u32TimestampLow         = u64TimestampEvent & UINT32_C(0xffffffff);

    UsbHdr.u64Id = (uintptr_t)pUrb; /** @todo check whether the pointer is a good ID. */
    uint32_t cbUrbLength;
    switch (enmEvent)
    {
        case VUSBSNIFFEREVENT_SUBMIT:
            UsbHdr.u8EventType = DUMPFILE_USB_EVENT_TYPE_SUBMIT;
            cbUrbLength = pUrb->cbData;
            break;
        case VUSBSNIFFEREVENT_COMPLETE:
            UsbHdr.u8EventType = DUMPFILE_USB_EVENT_TYPE_COMPLETE;
            cbUrbLength = pUrb->cbData;
            break;
        case VUSBSNIFFEREVENT_ERROR_SUBMIT:
        case VUSBSNIFFEREVENT_ERROR_COMPLETE:
            UsbHdr.u8EventType = DUMPFILE_USB_EVENT_TYPE_ERROR;
            cbUrbLength = 0;
            break;
        default:
            AssertMsgFailed(("Invalid event type %d\n", enmEvent));
            cbUrbLength = 0;
    }
    uint32_t cbDataLength = cbUrbLength;
    pbData = &pUrb->abData[0];

    uint32_t cIsocPkts = 0;
    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_ISOC:
        {
            int32_t cErrors = 0;

            UsbHdr.u8TransferType = 0;
            cIsocPkts = pUrb->cIsocPkts;
            for (unsigned i = 0; i < cIsocPkts; i++)
                if (   pUrb->aIsocPkts[i].enmStatus != VUSBSTATUS_OK
                    && pUrb->aIsocPkts[i].enmStatus != VUSBSTATUS_NOT_ACCESSED)
                    cErrors++;

            UsbHdr.u.IsoRec.i32ErrorCount = cErrors;
            UsbHdr.u.IsoRec.i32NumDesc    = pUrb->cIsocPkts;
            cbCapturedLength += cIsocPkts * sizeof(DumpFileUsbIsoDesc);
            break;
        }
        case VUSBXFERTYPE_BULK:
            UsbHdr.u8TransferType = 3;
            break;
        case VUSBXFERTYPE_INTR:
            UsbHdr.u8TransferType = 1;
            break;
        case VUSBXFERTYPE_CTRL:
        case VUSBXFERTYPE_MSG:
            UsbHdr.u8TransferType = 2;
            break;
        default:
            AssertMsgFailed(("invalid transfer type %d\n", pUrb->enmType));
    }

    if (pUrb->enmDir == VUSBDIRECTION_IN)
    {
        if (enmEvent == VUSBSNIFFEREVENT_SUBMIT)
            cbDataLength = 0;
    }
    else if (pUrb->enmDir == VUSBDIRECTION_OUT)
    {
        if (   enmEvent == VUSBSNIFFEREVENT_COMPLETE
            || pUrb->enmType == VUSBXFERTYPE_CTRL
            || pUrb->enmType == VUSBXFERTYPE_MSG)
            cbDataLength = 0;
    }
    else if (   pUrb->enmDir == VUSBDIRECTION_SETUP
             && cbDataLength >= sizeof(VUSBSETUP))
        cbDataLength -= sizeof(VUSBSETUP);

    Epb.u32CapturedLen = cbCapturedLength + cbDataLength;
    Epb.u32PacketLen   = cbCapturedLength + cbUrbLength;

    UsbHdr.u8EndpointNumber = pUrb->EndPt | (pUrb->enmDir == VUSBDIRECTION_IN ? 0x80 : 0x00);
    UsbHdr.u8DeviceAddress  = pUrb->DstAddress;
    UsbHdr.u16BusId         = 0;
    UsbHdr.u8DataFlag       = cbDataLength ? 0 : 1;
    UsbHdr.u64TimestampSec  = u64TimestampEvent / RT_NS_1SEC_64;
    UsbHdr.u32TimestampUSec = u64TimestampEvent / RT_NS_1US_64 - UsbHdr.u64TimestampSec * RT_US_1SEC;
    UsbHdr.i32Status        = pUrb->enmStatus;
    UsbHdr.u32UrbLength     = cbUrbLength;
    UsbHdr.u32DataLength    = cbDataLength + cIsocPkts * sizeof(DumpFileUsbIsoDesc);
    UsbHdr.i32Interval      = 0;
    UsbHdr.i32StartFrame    = 0;
    UsbHdr.u32XferFlags     = 0;
    UsbHdr.u32NumDesc       = cIsocPkts;

    if (   (pUrb->enmType == VUSBXFERTYPE_MSG || pUrb->enmType == VUSBXFERTYPE_CTRL)
        && enmEvent == VUSBSNIFFEREVENT_SUBMIT)
    {
        PVUSBSETUP pSetup = (PVUSBSETUP)pUrb->abData;

        UsbHdr.u.UsbSetup.bmRequestType = pSetup->bmRequestType;
        UsbHdr.u.UsbSetup.bRequest      = pSetup->bRequest;
        UsbHdr.u.UsbSetup.wValue        = pSetup->wValue;
        UsbHdr.u.UsbSetup.wIndex        = pSetup->wIndex;
        UsbHdr.u.UsbSetup.wLength       = pSetup->wLength;
        UsbHdr.u8SetupFlag              = 0;
    }
    else
        UsbHdr.u8SetupFlag  = '-'; /* Follow usbmon source here. */

    /* Write the packet to the capture file. */
    int rc = vusbSnifferBlockNew(pThis, &Epb.Hdr, sizeof(Epb));
    if (RT_SUCCESS(rc))
        rc = vusbSnifferBlockAddData(pThis, &UsbHdr, sizeof(UsbHdr));

    /* Add Isochronous descriptors now. */
    for (unsigned i = 0; i < cIsocPkts && RT_SUCCESS(rc); i++)
    {
        DumpFileUsbIsoDesc IsoDesc;
        IsoDesc.i32Status = pUrb->aIsocPkts[i].enmStatus;
        IsoDesc.u32Offset = pUrb->aIsocPkts[i].off;
        IsoDesc.u32Len    = pUrb->aIsocPkts[i].cb;
        rc = vusbSnifferBlockAddData(pThis, &IsoDesc, sizeof(IsoDesc));
    }

    /* Record data. */
    if (   RT_SUCCESS(rc)
        && cbDataLength)
        rc = vusbSnifferBlockAddData(pThis, pbData, cbDataLength);

    if (RT_SUCCESS(rc))
        rc = vusbSnifferAddOption(pThis, DUMPFILE_OPTION_CODE_END, NULL, 0);

    if (RT_SUCCESS(rc))
        rc = vusbSnifferBlockCommit(pThis);

    return rc;
}

/**
 * VUSB sniffer format writer.
 */
const VUSBSNIFFERFMT g_VUsbSnifferFmtPcapNg =
{
    /** szName */
    "PCAPNG",
    /** pszDesc */
    "PCAP-NG format writer compatible with WireShark",
    /** papszFileExts */
    &s_apszFileExts[0],
    /** cbFmt */
    sizeof(VUSBSNIFFERFMTINT),
    /** pfnInit */
    vusbSnifferFmtPcapNgInit,
    /** pfnDestroy */
    vusbSnifferFmtPcapNgDestroy,
    /** pfnRecordEvent */
    vusbSnifferFmtPcapNgRecordEvent
};

