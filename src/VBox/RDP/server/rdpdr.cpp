/* $Id: rdpdr.cpp 114935 2026-08-10 12:46:46Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBox Remote Desktop Protocol - "RDPDR" static virtual channel: File System Virtual Channel Extension.
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

#include "vrdpserv.h"
#include "utils.h"

#include <limits.h>

#include "rdpdr.h"

#ifdef VRDP_DEBUG_RDPDR
#  define RDPDRLOG(a) do { VRDPLOG(("RDPDR::%s: ", __FUNCTION__)); VRDPLOG(a); } while (0)
#else
#  define RDPDRLOG(a)
#endif /* VRDP_DEBUG_RDPDR */

#ifdef DEBUG_sunlover
#undef RDPDRLOG
#define RDPDRLOG LogRel
#endif

/* RDPDR structures. */
#pragma pack(1)

#define RDPDR_CTYP_CORE 0x4472
#define RDPDR_CTYP_PRN 0x5052

#define PAKID_CORE_SERVER_ANNOUNCE 0x496E
#define PAKID_CORE_CLIENTID_CONFIRM 0x4343
#define PAKID_CORE_CLIENT_NAME 0x434E
#define PAKID_CORE_DEVICELIST_ANNOUNCE 0x4441
#define PAKID_CORE_DEVICE_REPLY 0x6472
#define PAKID_CORE_DEVICE_IOREQUEST 0x4952
#define PAKID_CORE_DEVICE_IOCOMPLETION 0x4943
#define PAKID_CORE_SERVER_CAPABILITY 0x5350
#define PAKID_CORE_CLIENT_CAPABILITY 0x4350
#define PAKID_CORE_DEVICELIST_REMOVE 0x444D
#define PAKID_PRN_CACHE_DATA 0x5043
#define PAKID_CORE_USER_LOGGEDON 0x554C
#define PAKID_PRN_USING_XPS 0x5543

typedef struct RDPDR_HEADER
{
    uint16_t u16Component;
    uint16_t u16PacketId;
} RDPDR_HEADER;

#define CAP_GENERAL_TYPE 0x0001
#define CAP_PRINTER_TYPE 0x0002
#define CAP_PORT_TYPE 0x0003
#define CAP_DRIVE_TYPE 0x0004
#define CAP_SMARTCARD_TYPE 0x0005

#define GENERAL_CAPABILITY_VERSION_01 0x00000001
#define GENERAL_CAPABILITY_VERSION_02 0x00000002
#define PRINT_CAPABILITY_VERSION_01 0x00000001
#define PORT_CAPABILITY_VERSION_01 0x00000001
#define DRIVE_CAPABILITY_VERSION_01 0x00000001
#define DRIVE_CAPABILITY_VERSION_02 0x00000002
#define SMARTCARD_CAPABILITY_VERSION_01 0x00000001

typedef struct CAPABILITY_HEADER
{
    uint16_t u16CapabilityType;
    uint16_t u16CapabilityLength;
    uint32_t u32Version;
} CAPABILITY_HEADER;

/* CAPABILITY_SET consists of CAPABILITY_HEADER followed by capability data, which
 * depends on CAPABILITY_HEADER::CapabilityType.
 */

#define RDPDR_DTYP_SERIAL 0x00000001
#define RDPDR_DTYP_PARALLEL 0x00000002
#define RDPDR_DTYP_PRINT 0x00000004
#define RDPDR_DTYP_FILESYSTEM 0x00000008
#define RDPDR_DTYP_SMARTCARD 0x00000020

typedef struct DEVICE_ANNOUNCE
{
    uint32_t u32DeviceType;
    uint32_t u32DeviceId;
    uint8_t au8PreferredDosName[8];
    uint32_t u32DeviceDataLength;
    /* Device Data follows. */
} DEVICE_ANNOUNCE;

#define DR_IRP_MJ_CREATE 0x00000000
#define DR_IRP_MJ_CLOSE 0x00000002
#define DR_IRP_MJ_READ 0x00000003
#define DR_IRP_MJ_WRITE 0x00000004
#define DR_IRP_MJ_DEVICE_CONTROL 0x0000000E
#define DR_IRP_MJ_QUERY_VOLUME_INFORMATION 0x0000000A
#define DR_IRP_MJ_SET_VOLUME_INFORMATION 0x0000000B
#define DR_IRP_MJ_QUERY_INFORMATION 0x00000005
#define DR_IRP_MJ_SET_INFORMATION 0x00000006
#define DR_IRP_MJ_DIRECTORY_CONTROL 0x0000000C
#define DR_IRP_MJ_LOCK_CONTROL 0x00000011

#define DR_IRP_MN_QUERY_DIRECTORY 0x00000001
#define DR_IRP_MN_NOTIFY_CHANGE_DIRECTORY 0x00000002

typedef struct DR_DEVICE_IOREQUEST
{
    RDPDR_HEADER Header;
    uint32_t u32DeviceId;
    uint32_t u32FileId;
    uint32_t u32CompletionId;
    uint32_t u32MajorFunction;
    uint32_t u32MinorFunction;
} DR_DEVICE_IOREQUEST;

typedef struct DR_CREATE_REQ
{
    DR_DEVICE_IOREQUEST DeviceIoRequest;
    uint32_t u32DesiredAccess;
    uint64_t u64AllocationSize;
    uint32_t u32FileAttributes;
    uint32_t u32SharedAccess;
    uint32_t u32CreateDisposition;
    uint32_t u32CreateOptions;
    uint32_t u32PathLength;
    uint16_t au16Path[1]; /* Includes the null-terminator. */
} DR_CREATE_REQ;

typedef struct DR_CLOSE_REQ
{
    DR_DEVICE_IOREQUEST DeviceIoRequest;
    uint8_t au8Padding[32];
} DR_CLOSE_REQ;

typedef struct DR_READ_REQ
{
    DR_DEVICE_IOREQUEST DeviceIoRequest;
    uint32_t u32Length;
    uint64_t u64Offset;
    uint8_t au8Padding[20];
} DR_READ_REQ;

typedef struct DR_WRITE_REQ
{
    DR_DEVICE_IOREQUEST DeviceIoRequest;
    uint32_t u32Length;
    uint64_t u64Offset;
    uint8_t au8Padding[20];
    /* Write data follows. */
} DR_WRITE_REQ;

typedef struct DR_CONTROL_REQ
{
    DR_DEVICE_IOREQUEST DeviceIoRequest;
    uint32_t u32OutputBufferLength;
    uint32_t u32InputBufferLength;
    uint32_t u32IoControlCode;
    uint8_t au8Padding[20];
    /* InputBuffer follows. */
} DR_CONTROL_REQ;

typedef struct DR_DEVICE_IOCOMPLETION
{
    RDPDR_HEADER Header;
    uint32_t u32DeviceId;
    uint32_t u32CompletionId;
    uint32_t u32IoStatus;
} DR_DEVICE_IOCOMPLETION;

#define DR_FILE_SUPERSEDED 0x00000000
#define DR_FILE_OPENED 0x00000001
#define DR_FILE_OVERWRITTEN 0x00000003

typedef struct DR_CREATE_RSP
{
    DR_DEVICE_IOCOMPLETION DeviceIoReply;
    uint32_t u32FileId;
    uint8_t u8Information;
} DR_CREATE_RSP;

typedef struct DR_CLOSE_RSP
{
    DR_DEVICE_IOCOMPLETION DeviceIoReply;
    uint8_t au8Padding[5];
} DR_CLOSE_RSP;

typedef struct DR_READ_RSP
{
    DR_DEVICE_IOCOMPLETION DeviceIoReply;
    uint32_t u32Length;
    /* Read data follows. */
} DR_READ_RSP;

typedef struct DR_WRITE_RSP
{
    DR_DEVICE_IOCOMPLETION DeviceIoReply;
    uint32_t u32Length;
    /* Optional uint8_t au8Padding[1]; */
} DR_WRITE_RSP;

typedef struct DR_CONTROL_RSP
{
    DR_DEVICE_IOCOMPLETION DeviceIoReply;
    uint32_t u32OutputBufferLength;
    /* OutputBuffer data follows. */
} DR_CONTROL_RSP;

typedef struct RDP_LOCK_INFO
{
    uint64_t u64Length;
    uint64_t u64Offset;
} RDP_LOCK_INFO;

typedef struct DR_CORE_DEVICE_ANNOUNCE_RSP
{
    RDPDR_HEADER Header;
    uint32_t u32DeviceId;
    uint32_t u32ResultCode;
} DR_CORE_DEVICE_ANNOUNCE_RSP;

#define DR_VERSION_RDP6 0x000C
#define DR_VERSION_RDP52 0x000A
#define DR_VERSION_RDP51 0x0005
#define DR_VERSION_RDP50 0x0002

typedef struct DR_CORE_SERVER_ANNOUNCE_REQ
{
    RDPDR_HEADER Header;
    uint16_t u16VersionMajor;
    uint16_t u16VersionMinor;
    uint32_t u32ClientId;
} DR_CORE_SERVER_ANNOUNCE_REQ;

typedef struct DR_CORE_CLIENT_ANNOUNCE_RSP
{
    RDPDR_HEADER Header;
    uint16_t u16VersionMajor;
    uint16_t u16VersionMinor;
    uint32_t u32ClientId;
} DR_CORE_CLIENT_ANNOUNCE_RSP;

typedef struct DR_CORE_CLIENT_NAME_REQ
{
    RDPDR_HEADER Header;
    uint32_t u32UnicodeFlag;
    uint32_t u32CodePage;
    uint32_t u32ComputerNameLen;
    uint8_t au8ComputerName[1]; /* Null-terminated. */
} DR_CORE_CLIENT_NAME_REQ;

typedef struct DR_CORE_USER_LOGGEDON
{
    RDPDR_HEADER Header;
} DR_CORE_USER_LOGGEDON;

typedef struct DR_CORE_SERVER_CLIENTID_CONFIRM
{
    RDPDR_HEADER Header;
    uint16_t u16VersionMajor;
    uint16_t u16VersionMinor;
    uint32_t u32ClientId;
} DR_CORE_SERVER_CLIENTID_CONFIRM;

typedef struct DR_CORE_CAPABILITY_REQ
{
    RDPDR_HEADER Header;
    uint16_t u16NumCapabilities;
    uint16_t u16Padding;
    /* Array of CAPABILITY_SET structures follows. */
} DR_CORE_CAPABILITY_REQ;

#define IOCODE_RDPDR_IRP_MJ_CREATE 0x00000001
#define IOCODE_RDPDR_IRP_MJ_CLEANUP 0x00000002
#define IOCODE_RDPDR_IRP_MJ_CLOSE 0x00000004
#define IOCODE_RDPDR_IRP_MJ_READ 0x00000008
#define IOCODE_RDPDR_IRP_MJ_WRITE 0x00000010
#define IOCODE_RDPDR_IRP_MJ_FLUSH_BUFFERS 0x00000020
#define IOCODE_RDPDR_IRP_MJ_SHUTDOWN 0x00000040
#define IOCODE_RDPDR_IRP_MJ_DEVICE_CONTROL 0x00000080
#define IOCODE_RDPDR_IRP_MJ_QUERY_VOLUME_INFORMATION 0x00000100
#define IOCODE_RDPDR_IRP_MJ_SET_VOLUME_INFORMATION 0x00000200
#define IOCODE_RDPDR_IRP_MJ_QUERY_INFORMATION 0x00000400
#define IOCODE_RDPDR_IRP_MJ_SET_INFORMATION 0x00000800
#define IOCODE_RDPDR_IRP_MJ_DIRECTORY_CONTROL 0x00001000
#define IOCODE_RDPDR_IRP_MJ_LOCK_CONTROL 0x00002000
#define IOCODE_RDPDR_IRP_MJ_QUERY_SECURITY 0x00004000
#define IOCODE_RDPDR_IRP_MJ_SET_SECURITY 0x00008000

#define RDPDR_DEVICE_REMOVE_PDUS 0x00000001
#define RDPDR_CLIENT_DISPLAY_NAME_PDU 0x00000002
#define RDPDR_USER_LOGGEDON_PDU 0x00000004

#define ENABLE_ASYNCIO 0x00000001

typedef struct GENERAL_CAPS_SET
{
    CAPABILITY_HEADER Header;
    uint32_t u32OsType;
    uint32_t u32OsVersion;
    uint16_t u16ProtocolMajorVersion;
    uint16_t u16ProtocolMinorVersion;
    uint32_t u32IoCode1;
    uint32_t u32IoCode2;
    uint32_t u32ExtendedPDU;
    uint32_t u32ExtraFlags1;
    uint32_t u32ExtraFlags2;
} GENERAL_CAPS_SET;

typedef struct GENERAL_CAPS_SET_2
{
    GENERAL_CAPS_SET CapSet;
    uint32_t u32SpecialTypeDeviceCap;
} GENERAL_CAPS_SET_2;

typedef struct PRINTER_CAPS_SET
{
    CAPABILITY_HEADER Header;
} PRINTER_CAPS_SET;

typedef struct PORT_CAPS_SET
{
    CAPABILITY_HEADER Header;
} PORT_CAPS_SET;

typedef struct DRIVE_CAPS_SET
{
    CAPABILITY_HEADER Header;
} DRIVE_CAPS_SET;

typedef struct SMARTCARD_CAPS_SET
{
    CAPABILITY_HEADER Header;
} SMARTCARD_CAPS_SET;

typedef DR_CORE_CAPABILITY_REQ DR_CORE_CAPABILITY_RSP;

typedef struct DR_CORE_DEVICELIST_ANNOUNCE_REQ
{
    RDPDR_HEADER Header;
    uint32_t u32DeviceCount;
    /* Array of DEVICE_ANNOUNCE headers follows. */
} DR_CORE_DEVICELIST_ANNOUNCE_REQ;

#pragma pack()


typedef struct RDPDRIOCompletion
{
    RTLISTNODE NodeIOCompletion;

    uint32_t u32CompletionId;

    /* Saved IO information. */
    RDPDRPKTHDR *pPktHdr;
    uint32_t u32MajorFunction;
    uint32_t u32DeviceId;
} RDPDRIOCompletion;


VRDPChannelRDPDR::VRDPChannelRDPDR (VRDPTP *pvrdptp)
    :
    VRDPChannel (),
    m_pvrdptp (pvrdptp)
{
    VRDPLock::Create("RDPDR", &m_pLock);
    initMembers ();
}

void VRDPChannelRDPDR::initMembers (void)
{
    m_channelOptions = 0;
    m_channelId = 0;

    m_fOperational = false;
    m_fSetup = false;

    m_u32ClientId = 0;
    m_fSmartCard = false;

    m_u32IoCode1 = 0;
    m_u32ExtendedPDU = 0;
    m_u32ExtraFlags1 = 0;

    RT_ZERO(m_IOCompletion);
    RTListInit(&m_IOCompletion.ListFree);
    RTListInit(&m_IOCompletion.ListUsed);

    RT_ZERO(m_smartcard);
}

VRDPChannelRDPDR::~VRDPChannelRDPDR ()
{
    Close ();
    VRDPLock::Delete(&m_pLock);
}

int VRDPChannelRDPDR::Open (uint32_t options, uint16_t id)
{
    RDPDRLOG(("RDPDR: Open id = %d\n", id));

    VRDPServer *pServer = m_pvrdptp->Client()->Server();
    NOREF(pServer);

    m_channelOptions = options;
    m_channelId = id;

    InitOutgoingChannelId (m_channelId);

    return VINF_SUCCESS;
}

void VRDPChannelRDPDR::Setup (void)
{
    RDPDRLOG(("RDPDR: Setup\n"));

    if (m_channelId != 0)
    {
        m_fSetup = true;
    }

    return;
}

void VRDPChannelRDPDR::Close (void)
{
    RTLISTANCHOR ListFree;
    RTLISTANCHOR ListUsed;

    bool fLocked = RT_SUCCESS(VRDPLock::Lock(m_pLock));

    RTListMove(&ListFree, &m_IOCompletion.ListFree);
    RTListMove(&ListUsed, &m_IOCompletion.ListUsed);

    if (fLocked)
    {
        VRDPLock::Unlock(m_pLock);
    }

    /* Free all allocated completionIds. */
    RDPDRIOCompletion *pIter;
    RDPDRIOCompletion *pNext;

    RTListForEachSafe(&ListFree, pIter, pNext, RDPDRIOCompletion, NodeIOCompletion)
    {
        VRDPMemFree(pIter);
    }

    RTListForEachSafe(&ListUsed, pIter, pNext, RDPDRIOCompletion, NodeIOCompletion)
    {
        RDPDRIOCTL *pIO = (RDPDRIOCTL *)pIter->pPktHdr;
        uint32_t u32DeviceId = pIter->u32DeviceId;

        RDPDRLOG(("Close: completing pending IO %p [%d,%d]\n",
                  pIO, m_pvrdptp->Client()->Id(), u32DeviceId));

        rdpdrDispatchIOCompletion(pIO, u32DeviceId, RDPDR_STATUS_UNSUCCESSFUL);
        RDPDRPktRelease(&pIO->hdr);

        VRDPMemFree(pIter);
    }

    if (m_smartcard.fEnabled)
    {
        m_smartcard.fEnabled = false;
        m_pvrdptp->Client()->Server()->SCard()->SCardDetach(m_pvrdptp->Client()->Id(), m_smartcard.u32DeviceId);
    }

    initMembers ();
}

void VRDPChannelRDPDR::Start (void)
{
    RDPDRLOG(("RDPDR: Start\n"));

    if (!m_fSetup)
    {
        /* If the channel is not initialized. */
        RDPDRLOG(("channel not supported, negotiation cancelled!!!\n"));

        VRDPLOGRELLIMIT(16, ("RDPDR disabled.\n"));

        return;
    }

    if (m_fOperational)
    {
        RDPDRLOG(("channel already intialized, negotiation cancelled!!!\n"));
        return;
    }

    m_fOperational = true;

    /* Send Server Announce Request. */
    DR_CORE_SERVER_ANNOUNCE_REQ pdu;

    pdu.Header.u16Component = RDPDR_CTYP_CORE;
    pdu.Header.u16PacketId = PAKID_CORE_SERVER_ANNOUNCE;
    pdu.u16VersionMajor = 0x0001;
    pdu.u16VersionMinor = DR_VERSION_RDP6;
    pdu.u32ClientId = m_pvrdptp->Client()->Id();

    VRDPBUFFER aBuffers[1];

    aBuffers[0].pv = &pdu;
    aBuffers[0].cb = sizeof (pdu);

    m_pvrdptp->SendToChannel (this /* as OutputCtx */, m_channelId, m_channelOptions,
                              RT_ELEMENTS(aBuffers), aBuffers, false);

    return;
}

/** @thread OUTPUT */
int VRDPChannelRDPDR::SendData(VRDPBUFFER *paBuffers, int cBuffers)
{
    int rc = VINF_SUCCESS;

    RDPDRLOG(("RDPDR: SendData %d, paBuffers %p, cBuffers = %d\n", m_fOperational, paBuffers, cBuffers));

#ifdef DEBUG_sunlover
    int iBuffer;
    for (iBuffer = 0; iBuffer < cBuffers; iBuffer++)
    {
        RDPDRLOG(("RDPDR:     [%d] pv %p, cb %d\n", iBuffer, paBuffers[iBuffer].pv, paBuffers[iBuffer].cb));
        // hexdump((uint8_t *)paBuffers[iBuffer].pv, paBuffers[iBuffer].cb);
    }
#endif

    if (!m_fOperational)
    {
        return rc;
    }

    rc = sendBuffers(cBuffers, paBuffers);

    return rc;
}

/** @thread OUTPUT */
int VRDPChannelRDPDR::sendBuffers (int cBuffers, const VRDPBUFFER *paBuffers)
{
    /* A Reset is not necessary because the contexts automatically resets after sending a packet. */
    Assert (IsReset ());
    Assert (m_fOperational);
    Assert (m_pvrdptp->Client ()->Server ()->IsOutputThread ());

    return m_pvrdptp->SendToChannel (this /* as OutputCtx */, m_channelId, m_channelOptions,
                                     cBuffers, paBuffers, false);
}

/** @thread any */
int VRDPChannelRDPDR::createIO(uint32_t *pu32CompletionId,
                               RDPDRPKTHDR *pHdr,
                               uint32_t u32MajorFunction,
                               uint32_t u32DeviceId)
{
    int rc = VINF_SUCCESS;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        /* Allocate or reuse a completionId for IO request and associate pHdr with it. */
        RDPDRIOCompletion *pIOC = RTListGetFirst(&m_IOCompletion.ListFree, RDPDRIOCompletion, NodeIOCompletion);

        if (pIOC)
        {
            /* There is a free iocompletion id. */
            RTListNodeRemove(&pIOC->NodeIOCompletion);
        }
        else
        {
            /* Allocate a new IOC. */
            pIOC = (RDPDRIOCompletion *)VRDPMemAllocZ(sizeof(RDPDRIOCompletion));
            if (pIOC)
            {
                pIOC->u32CompletionId = ASMAtomicIncU32(&m_IOCompletion.u32IdSrc);

                /* Since ids must be reused, verify that there are not too many ids. */
                m_IOCompletion.cIds++;
                AssertLogRelMsg(m_IOCompletion.cIds < _64K,
                                ("%d\n", m_IOCompletion.cIds));
            }
            else
            {
                rc = VERR_NO_MEMORY;
            }
        }

        if (RT_SUCCESS(rc))
        {
            pIOC->pPktHdr = pHdr;
            pIOC->u32MajorFunction = u32MajorFunction;
            pIOC->u32DeviceId = u32DeviceId;

            *pu32CompletionId = pIOC->u32CompletionId;

            RTListAppend(&m_IOCompletion.ListUsed, &pIOC->NodeIOCompletion);
        }

        VRDPLock::Unlock(m_pLock);
    }
    else
    {
        rc = VERR_NOT_SUPPORTED;
    }

    if (RT_SUCCESS(rc))
    {
        RDPDRLOG(("RDPDR: created completionId %d\n",
                  *pu32CompletionId));
    }
    else
    {
        /* The pHdr was not added to the list. */
        RDPDRPktRelease(pHdr);
    }

    return rc;
}

/** @thread any */
int VRDPChannelRDPDR::fetchIO(uint32_t u32CompletionId,
                              RDPDRPKTHDR **ppHdr,
                              uint32_t *pu32MajorFunction,
                              uint32_t *pu32DeviceId)
{
    /* Find the completionId context. */

    int rc = VINF_SUCCESS;

    if (RT_SUCCESS(VRDPLock::Lock(m_pLock)))
    {
        RDPDRIOCompletion *pIOC = NULL;

        RDPDRIOCompletion *pIter;
        RTListForEach(&m_IOCompletion.ListUsed, pIter, RDPDRIOCompletion, NodeIOCompletion)
        {
            if (pIter->u32CompletionId == u32CompletionId)
            {
                pIOC = pIter;
                break;
            }
        }

        if (pIOC)
        {
            /* Found the IO operation. Fetch the info and move the completionId to the Free list. */
            RTListNodeRemove(&pIOC->NodeIOCompletion);

            /* Pass the referenced pHdr to the caller. */
            *ppHdr = pIOC->pPktHdr;
            *pu32MajorFunction = pIOC->u32MajorFunction;
            *pu32DeviceId = pIOC->u32DeviceId;

            pIOC->pPktHdr= NULL;
            pIOC->u32MajorFunction = 0;
            pIOC->u32DeviceId = 0;
            RTListAppend(&m_IOCompletion.ListFree, &pIOC->NodeIOCompletion);
        }
        else
        {
            rc = VERR_NOT_FOUND;
        }

        VRDPLock::Unlock(m_pLock);
    }
    else
    {
        rc = VERR_NOT_SUPPORTED;
    }

    return rc;
}

/** @thread INPUT */
void VRDPChannelRDPDR::cancelIOForDevice(uint32_t u32DeviceId)
{
    RTLISTANCHOR ListPendingIO;
    RTListInit(&ListPendingIO);

    RDPDRIOCompletion *pIter;
    RDPDRIOCompletion *pNext;

    bool fLocked = RT_SUCCESS(VRDPLock::Lock(m_pLock));

    RTListForEachSafe(&m_IOCompletion.ListUsed, pIter, pNext, RDPDRIOCompletion, NodeIOCompletion)
    {
        if (pIter->u32DeviceId == u32DeviceId)
        {
            RTListNodeRemove(&pIter->NodeIOCompletion);
            RTListAppend(&ListPendingIO, &pIter->NodeIOCompletion);
        }
    }

    if (fLocked)
    {
        VRDPLock::Unlock(m_pLock);
    }

    RTListForEachSafe(&ListPendingIO, pIter, pNext, RDPDRIOCompletion, NodeIOCompletion)
    {
        RDPDRIOCTL *pIO = (RDPDRIOCTL *)pIter->pPktHdr;

        RDPDRLOG(("cancelIOForDevice: pending IO %p [%d,%d]\n",
                  pIO, m_pvrdptp->Client()->Id(), u32DeviceId));

        rdpdrDispatchIOCompletion(pIO, u32DeviceId, RDPDR_STATUS_UNSUCCESSFUL);
        RDPDRPktRelease(&pIO->hdr);

        VRDPMemFree(pIter);
    }
}

/** @thread OUTPUT */
int VRDPChannelRDPDR::ProcessOutput (const void *pvData, uint32_t cbData)
{
    /* Parse the data header to know what kind of output it is. */
    if (cbData < sizeof(RDPDROUT))
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    RDPDROUT *pOut = (RDPDROUT *)pvData;

    RDPDRPKTHDR *pHdr = pOut->pPktHdr;

    int rc = VINF_SUCCESS;

    switch(pHdr->pktType)
    {
        case RDPDR_OUT_RAW:
        {
            RDPDRLOG(("RDPDR: RDPDR_OUT_RAW\n"));

            VRDPBUFFER aBuffers[1];

            aBuffers[0].pv = RDPDRPktDataPtr(pHdr);
            aBuffers[0].cb = RDPDRPktDataSize(pHdr);

            rc = m_pvrdptp->SendToChannel(this /* as OutputCtx */, m_channelId, m_channelOptions,
                                          RT_ELEMENTS(aBuffers), aBuffers, false);
        } break;

        case RDPDR_OUT_IOCTL:
        {
            RDPDRLOG(("RDPDR: RDPDR_OUT_IOCTL\n"));

            /* Reference for being in the IOCompletion list. */
            RDPDRPktAddRef(pHdr);

            uint32_t u32CompletionId = 0;
            rc = createIO(&u32CompletionId, pHdr, DR_IRP_MJ_DEVICE_CONTROL, pHdr->u32DeviceId);

            if (RT_FAILURE(rc))
            {
                break;
            }

            RDPDRIOCTL *pIO = (RDPDRIOCTL *)pHdr;

            RDPDRLOG(("RDPDR: u32InputBufferLength %d, completionId %d\n",
                      pIO->u32InputBufferLength, u32CompletionId));

            DR_CONTROL_REQ req;

            req.DeviceIoRequest.Header.u16Component = RDPDR_CTYP_CORE;
            req.DeviceIoRequest.Header.u16PacketId = PAKID_CORE_DEVICE_IOREQUEST;

            req.DeviceIoRequest.u32DeviceId      = pHdr->u32DeviceId;
            req.DeviceIoRequest.u32FileId        = 0; /* ok? */
            req.DeviceIoRequest.u32CompletionId  = u32CompletionId;
            req.DeviceIoRequest.u32MajorFunction = DR_IRP_MJ_DEVICE_CONTROL;
            req.DeviceIoRequest.u32MinorFunction = 0;

            req.u32OutputBufferLength = pIO->u32RequestedOutputLength;
            req.u32InputBufferLength  = pIO->u32InputBufferLength;
            req.u32IoControlCode      = pIO->u32Function;
            memset(&req.au8Padding[0], 0, sizeof(req.au8Padding));
            /* InputBuffer follows. */
#ifdef DEBUG_sunlover
            hexdumprel(pIO->pu8InputBuffer, pIO->u32InputBufferLength);
#endif /* DEBUG_sunlover */

            VRDPBUFFER aBuffers[2];

            aBuffers[0].pv = &req;
            aBuffers[0].cb = sizeof(req);

            aBuffers[1].pv = pIO->pu8InputBuffer;
            aBuffers[1].cb = pIO->u32InputBufferLength;

            rc = m_pvrdptp->SendToChannel(this /* as OutputCtx */, m_channelId, m_channelOptions,
                                          RT_ELEMENTS(aBuffers), aBuffers, false);
        } break;
        default:
        {
            AssertFailed();
            rc = VERR_NOT_SUPPORTED;
        }
    }

    RDPDRPktRelease(pHdr);

    return rc;
}

int VRDPChannelRDPDR::postOutput (RDPDRPKTHDR *pHdr, uint32_t cbData)
{
    int rc = VINF_SUCCESS;

    RDPDRPKTHDR *pCopy = (RDPDRPKTHDR *)VRDPMemAlloc(cbData);
    if (!pCopy)
    {
        return VERR_NO_MEMORY;
    }

    memcpy(pCopy, pHdr, cbData);

    RDPDRPktAddRef(pCopy);

    RDPDROUT outdata;
    outdata.pPktHdr = pCopy;

    m_pvrdptp->Client()->Server ()->PostOutput (VRDP_OUTPUT_RDPDR, m_pvrdptp->Client ()->Id (), &outdata, sizeof(outdata));

    return rc;
}

/** @thread INPUT */
int VRDPChannelRDPDR::ProcessChannelInput (const uint8_t *pu8Input, uint32_t u32Length)
{
    int rc = VINF_SUCCESS;

    if (u32Length < sizeof (RDPDR_HEADER))
    {
        return VERR_VRDP_PROTOCOL_ERROR;
    }

    const RDPDR_HEADER *pHdr = (const RDPDR_HEADER *)pu8Input;

    RDPDRLOG(("Input: Component 0x%04X, PacketId = 0x%04X\n",
               pHdr->u16Component, pHdr->u16PacketId));

    if (pHdr->u16Component == RDPDR_CTYP_CORE)
    {
        switch (pHdr->u16PacketId)
        {
            case PAKID_CORE_CLIENTID_CONFIRM:
            {
                DR_CORE_CLIENT_ANNOUNCE_RSP *pPDU = (DR_CORE_CLIENT_ANNOUNCE_RSP *)pHdr;

                RDPDRLOG(("Input: DR_CORE_CLIENT_ANNOUNCE_RSP: version 0x%04X.0x%04X id 0x%08X\n",
                           pPDU->u16VersionMajor, pPDU->u16VersionMinor, pPDU->u32ClientId));

                m_u32ClientId = pPDU->u32ClientId;
            } break;

            case PAKID_CORE_CLIENT_NAME:
            {
                DR_CORE_CLIENT_NAME_REQ *pPDU = (DR_CORE_CLIENT_NAME_REQ *)pHdr;
                RT_NOREF_PV(pPDU);

                RDPDRLOG(("Input: DR_CORE_CLIENT_NAME_REQ: unicode 0x%08X, CodePage %d, len %d\n",
                           pPDU->u32UnicodeFlag, pPDU->u32CodePage, pPDU->u32ComputerNameLen));

                /* Send Server Core Capability */
                rc = rdpdrSendServerCoreCapability();

                if (RT_SUCCESS(rc))
                {
                    rc = rdpdrSendServerClientIdConfirm();
                }
            } break;

            case PAKID_CORE_CLIENT_CAPABILITY:
            {
                rc = rdpdrParseCoreClientCapability(pu8Input, u32Length);
            } break;

            case PAKID_CORE_DEVICELIST_ANNOUNCE:
            {
                rc = rdpdrParseClientDeviceListAnnounce(pu8Input, u32Length);
            } break;

            case PAKID_CORE_DEVICE_IOCOMPLETION:
            {
                rc = rdpdrParseDeviceIOCompletion(pu8Input, u32Length);
            } break;

            default:
            {
                /* Do nothing, skip unsupported. */
                RDPDRLOG(("Input: unsupported PacketId 0x%04X\n",
                           pHdr->u16PacketId));
            } break;
        }
    }
    else if (pHdr->u16Component == RDPDR_CTYP_PRN)
    {
        rc = VERR_NOT_SUPPORTED;
    }
    else
    {
        rc = VERR_VRDP_PROTOCOL_ERROR;
    }

    return rc;
}

int VRDPChannelRDPDR::rdpdrSendServerCoreCapability(void)
{
    int rc= VINF_SUCCESS;

#pragma pack(1)
    struct
    {
        RDPDRPKTHDR hdr;
        DR_CORE_CAPABILITY_REQ req;
        GENERAL_CAPS_SET_2 capGeneral;
        SMARTCARD_CAPS_SET capSmartCard;
    } pkt;
#pragma pack()

    RDPDRPktInit(&pkt.hdr, RDPDR_OUT_RAW, sizeof(pkt), NULL, 0);

    pkt.req.Header.u16Component = RDPDR_CTYP_CORE;
    pkt.req.Header.u16PacketId = PAKID_CORE_SERVER_CAPABILITY;
    pkt.req.u16NumCapabilities = 2; /* See pdu structure. */
    pkt.req.u16Padding = 0;

    pkt.capGeneral.CapSet.Header.u16CapabilityType = CAP_GENERAL_TYPE;
    pkt.capGeneral.CapSet.Header.u16CapabilityLength = sizeof (GENERAL_CAPS_SET_2);
    pkt.capGeneral.CapSet.Header.u32Version = GENERAL_CAPABILITY_VERSION_02;
    pkt.capGeneral.CapSet.u32OsType = 0;
    pkt.capGeneral.CapSet.u32OsVersion = 0;
    pkt.capGeneral.CapSet.u16ProtocolMajorVersion = 1;
    pkt.capGeneral.CapSet.u16ProtocolMinorVersion = DR_VERSION_RDP6;
    pkt.capGeneral.CapSet.u32IoCode1 =   IOCODE_RDPDR_IRP_MJ_CREATE
                                       | IOCODE_RDPDR_IRP_MJ_CLEANUP
                                       | IOCODE_RDPDR_IRP_MJ_CLOSE
                                       | IOCODE_RDPDR_IRP_MJ_READ
                                       | IOCODE_RDPDR_IRP_MJ_WRITE
                                       | IOCODE_RDPDR_IRP_MJ_FLUSH_BUFFERS
                                       | IOCODE_RDPDR_IRP_MJ_SHUTDOWN
                                       | IOCODE_RDPDR_IRP_MJ_DEVICE_CONTROL
                                       | IOCODE_RDPDR_IRP_MJ_QUERY_VOLUME_INFORMATION
                                       | IOCODE_RDPDR_IRP_MJ_SET_VOLUME_INFORMATION
                                       | IOCODE_RDPDR_IRP_MJ_QUERY_INFORMATION
                                       | IOCODE_RDPDR_IRP_MJ_SET_INFORMATION
                                       | IOCODE_RDPDR_IRP_MJ_DIRECTORY_CONTROL
                                       | IOCODE_RDPDR_IRP_MJ_LOCK_CONTROL
                                       /* | IOCODE_RDPDR_IRP_MJ_QUERY_SECURITY
                                       | IOCODE_RDPDR_IRP_MJ_SET_SECURITY */;
    pkt.capGeneral.CapSet.u32IoCode2 = 0;
    pkt.capGeneral.CapSet.u32ExtendedPDU =   RDPDR_DEVICE_REMOVE_PDUS
                                           | RDPDR_CLIENT_DISPLAY_NAME_PDU
                                           | RDPDR_USER_LOGGEDON_PDU;
    pkt.capGeneral.CapSet.u32ExtraFlags1 = 0;
    pkt.capGeneral.CapSet.u32ExtraFlags2 = 0;
    pkt.capGeneral.u32SpecialTypeDeviceCap = 1;

    pkt.capSmartCard.Header.u16CapabilityType = CAP_SMARTCARD_TYPE;
    pkt.capSmartCard.Header.u16CapabilityLength = sizeof(SMARTCARD_CAPS_SET);
    pkt.capSmartCard.Header.u32Version = SMARTCARD_CAPABILITY_VERSION_01;

    postOutput (&pkt.hdr, sizeof(pkt));

    return rc;
}

int VRDPChannelRDPDR::rdpdrSendServerClientIdConfirm(void)
{
    int rc = VINF_SUCCESS;

#pragma pack(1)
    struct
    {
        RDPDRPKTHDR hdr;
        DR_CORE_SERVER_CLIENTID_CONFIRM pdu;
    } pkt;
#pragma pack()

    RDPDRPktInit(&pkt.hdr, RDPDR_OUT_RAW, sizeof(pkt), NULL, 0);

    pkt.pdu.Header.u16Component = RDPDR_CTYP_CORE;
    pkt.pdu.Header.u16PacketId = PAKID_CORE_CLIENTID_CONFIRM;
    pkt.pdu.u16VersionMajor = 0x0001;
    pkt.pdu.u16VersionMinor = DR_VERSION_RDP6;
    pkt.pdu.u32ClientId = m_u32ClientId;

    postOutput (&pkt.hdr, sizeof(pkt));

    return rc;
}

int VRDPChannelRDPDR::rdpdrParseCoreClientCapability(const uint8_t *pu8Input, uint32_t u32Length)
{
    if (u32Length < sizeof(DR_CORE_CAPABILITY_RSP))
    {
        return VERR_VRDP_PROTOCOL_ERROR;
    }

    int rc = VINF_SUCCESS;

    DR_CORE_CAPABILITY_RSP *pPDU = (DR_CORE_CAPABILITY_RSP *)pu8Input;

    RDPDRLOG(("Input: DR_CORE_CAPABILITY_RSP: num %d\n",
               pPDU->u16NumCapabilities));

    uint8_t *pu8 = (uint8_t *)pPDU + sizeof(DR_CORE_CAPABILITY_RSP);
    uint32_t u32Left = u32Length - sizeof(DR_CORE_CAPABILITY_RSP);

    uint16_t u16Index;
    for (u16Index = 0; u16Index < pPDU->u16NumCapabilities; u16Index++)
    {
        if (u32Left < sizeof(CAPABILITY_HEADER))
        {
            rc = VERR_VRDP_PROTOCOL_ERROR;
            break;
        }

        CAPABILITY_HEADER *pCapHdr = (CAPABILITY_HEADER *)pu8;

        RDPDRLOG(("Input: client cap: type 0x%04X, len 0x%04X, ver 0x%08X\n",
                   pCapHdr->u16CapabilityType, pCapHdr->u16CapabilityLength, pCapHdr->u32Version));

        if (u32Left < pCapHdr->u16CapabilityLength)
        {
            rc = VERR_VRDP_PROTOCOL_ERROR;
            break;
        }

        switch(pCapHdr->u16CapabilityType)
        {
            case CAP_GENERAL_TYPE:
            {
                if (pCapHdr->u16CapabilityLength < sizeof (GENERAL_CAPS_SET))
                {
                   rc = VERR_VRDP_PROTOCOL_ERROR;
                   break;
                }

                GENERAL_CAPS_SET *pCapSet = (GENERAL_CAPS_SET *)pCapHdr;

                RDPDRLOG(("Input: GENERAL_CAPS_SET:\n"
                          "    osType 0x%08X\n"
                          "    osVersion 0x%08X\n"
                          "    ver 0x%04X.0x%04X\n"
                          "    ioCode1 0x%08X\n"
                          "    ioCode2 0x%08X\n"
                          "    extendedPDU 0x%08X\n"
                          "    extraFlags1 0x%08X\n"
                          "    extraFlags2 0x%08X\n",
                          pCapSet->u32OsType,
                          pCapSet->u32OsVersion,
                          pCapSet->u16ProtocolMajorVersion,
                          pCapSet->u16ProtocolMinorVersion,
                          pCapSet->u32IoCode1,
                          pCapSet->u32IoCode2,
                          pCapSet->u32ExtendedPDU,
                          pCapSet->u32ExtraFlags1,
                          pCapSet->u32ExtraFlags2));

                m_u32IoCode1 = pCapSet->u32IoCode1;
                m_u32ExtendedPDU = pCapSet->u32ExtendedPDU;
                m_u32ExtraFlags1 = pCapSet->u32ExtraFlags1;

                if (pCapHdr->u32Version == GENERAL_CAPABILITY_VERSION_02)
                {
                    GENERAL_CAPS_SET_2 *pCapSet2 = (GENERAL_CAPS_SET_2 *)pCapHdr;
                    RT_NOREF_PV(pCapSet2);

                    RDPDRLOG(("Input: GENERAL_CAPS_SET_2:\n"
                              "    specialTypeDeviceCap 0x%08X\n",
                              pCapSet2->u32SpecialTypeDeviceCap));
                }

            } break;

            case CAP_SMARTCARD_TYPE:
            {
                if (pCapHdr->u16CapabilityLength < sizeof (SMARTCARD_CAPS_SET))
                {
                   rc = VERR_VRDP_PROTOCOL_ERROR;
                   break;
                }

                RDPDRLOG(("Input: CAP_SMARTCARD_TYPE\n"));
                m_fSmartCard = true;
            } break;

            default:
            {
                /* Ignore. */
            }
        }

        if (RT_FAILURE(rc))
        {
            break;
        }

        pu8 += pCapHdr->u16CapabilityLength;
        u32Left -= pCapHdr->u16CapabilityLength;
    }

    RDPDRLOG(("Clients caps rc %Rrc\n", rc));
    return rc;
}

int VRDPChannelRDPDR::rdpdrParseClientDeviceListAnnounce(const uint8_t *pu8Input, uint32_t u32Length)
{
    if (u32Length < sizeof(DR_CORE_DEVICELIST_ANNOUNCE_REQ))
    {
        return VERR_VRDP_PROTOCOL_ERROR;
    }

    int rc = VINF_SUCCESS;

    DR_CORE_DEVICELIST_ANNOUNCE_REQ *pPDU = (DR_CORE_DEVICELIST_ANNOUNCE_REQ *)pu8Input;

    RDPDRLOG(("Input: DR_CORE_DEVICELIST_ANNOUNCE_REQ: count %d\n",
               pPDU->u32DeviceCount));

    uint8_t *pu8 = (uint8_t *)pPDU + sizeof(DR_CORE_DEVICELIST_ANNOUNCE_REQ);
    uint32_t u32Left = u32Length - sizeof(DR_CORE_DEVICELIST_ANNOUNCE_REQ);

    uint32_t u32Index;
    for (u32Index = 0; u32Index < pPDU->u32DeviceCount; u32Index++)
    {
        if (u32Left < sizeof(DEVICE_ANNOUNCE))
        {
            rc = VERR_VRDP_PROTOCOL_ERROR;
            break;
        }

        DEVICE_ANNOUNCE *pDevHdr = (DEVICE_ANNOUNCE *)pu8;

        RDPDRLOG(("Input: device hdr: type 0x%08X, id 0x%08X, length %d\n",
                   pDevHdr->u32DeviceType, pDevHdr->u32DeviceId, pDevHdr->u32DeviceDataLength));
#ifdef LOG_ENABLED
        char szName[9];
        memcpy(szName, pDevHdr->au8PreferredDosName, sizeof(pDevHdr->au8PreferredDosName));
        szName[8] = 0;
        RDPDRLOG(("Input: device hdr: dos name [%s]\n",
                   szName));
#endif

        if (u32Left < pDevHdr->u32DeviceDataLength + sizeof (DEVICE_ANNOUNCE))
        {
            rc = VERR_VRDP_PROTOCOL_ERROR;
            break;
        }

        uint32_t u32Status = RDPDR_STATUS_SUCCESS;

        rc = rdpdrOnDeviceAdd(pDevHdr, pu8 + sizeof (DEVICE_ANNOUNCE), &u32Status);

        if (RT_FAILURE(rc))
        {
            break;
        }

        /* Send Device announce response. */
#pragma pack(1)
        struct
        {
            RDPDRPKTHDR hdr;
            DR_CORE_DEVICE_ANNOUNCE_RSP rsp;
        } pkt;
#pragma pack()

        RDPDRPktInit(&pkt.hdr, RDPDR_OUT_RAW, sizeof(pkt), NULL, 0);

        pkt.rsp.Header.u16Component = RDPDR_CTYP_CORE;
        pkt.rsp.Header.u16PacketId = PAKID_CORE_DEVICE_REPLY;
        pkt.rsp.u32DeviceId = pDevHdr->u32DeviceId;
        pkt.rsp.u32ResultCode = u32Status;

        postOutput(&pkt.hdr, sizeof(pkt));

        pu8 += pDevHdr->u32DeviceDataLength + sizeof (DEVICE_ANNOUNCE);
        u32Left -= pDevHdr->u32DeviceDataLength + sizeof (DEVICE_ANNOUNCE);
    }

    RDPDRLOG(("Device announce rc %Rrc\n", rc));
    return rc;
}

int VRDPChannelRDPDR::rdpdrOnDeviceAdd(const DEVICE_ANNOUNCE *pDevHdr,
                                       const uint8_t *pu8DeviceData,
                                       uint32_t *pu32Status)
{
    RT_NOREF1(pu8DeviceData);
    int rc = VINF_SUCCESS;

    uint32_t u32Status = RDPDR_STATUS_SUCCESS;

    switch(pDevHdr->u32DeviceType)
    {
        case RDPDR_DTYP_SMARTCARD:
        {
            RDPDRLOG(("Input: RDPDR_DTYP_SMARTCARD:\n%.*Rhxd\n", pDevHdr->u32DeviceDataLength, pu8DeviceData));

            /** @todo Add the device instance to the list of devices for RDPDR management. */

            /* There can be only one SCARD device.
             * Just save the device id in a member and do hardcoded calls to VRDPSCard.
             */
#ifdef DEBUG_sunlover
            Assert(!m_smartcard.fEnabled);
#endif
            if (m_smartcard.fEnabled)
            {
                /* Cancel pending IOCTLs for the existing device */
                cancelIOForDevice(m_smartcard.u32DeviceId);
            }

            m_smartcard.fEnabled = true;
            m_smartcard.u32DeviceId = pDevHdr->u32DeviceId;
            m_pvrdptp->Client()->Server()->SCard()->SCardAttach(m_pvrdptp->Client()->Id(), m_smartcard.u32DeviceId);
        } break;

        default:
        {
            /* Ignore. */
            u32Status = RDPDR_STATUS_NOT_SUPPORTED;
        }
    }

    *pu32Status = u32Status;

    RDPDRLOG(("Device add rc %Rrc\n", rc));
    return rc;
}

int VRDPChannelRDPDR::rdpdrParseDeviceIOCompletion(const uint8_t *pu8Input, uint32_t u32Length)
{
    if (u32Length < sizeof(DR_DEVICE_IOCOMPLETION))
    {
        return VERR_VRDP_PROTOCOL_ERROR;
    }

    int rc = VINF_SUCCESS;

    DR_DEVICE_IOCOMPLETION *pHdr = (DR_DEVICE_IOCOMPLETION *)pu8Input;

    RDPDRLOG(("Input: DR_DEVICE_IOCOMPLETION: u32Length %d, dev 0x%08X, completionId 0x%08X, IoStatus 0x%08X\n",
               u32Length, pHdr->u32DeviceId, pHdr->u32CompletionId, pHdr->u32IoStatus));

    uint32_t u32MajorFunction = 0;
    uint32_t u32DeviceId = 0;
    RDPDRPKTHDR *pPktHdr = NULL;
    rc = fetchIO(pHdr->u32CompletionId, &pPktHdr, &u32MajorFunction, &u32DeviceId);

    if (RT_SUCCESS(rc))
    {
        switch(u32MajorFunction)
        {
            case DR_IRP_MJ_DEVICE_CONTROL:
            {
                RDPDRLOG(("IOCompletion DR_IRP_MJ_DEVICE_CONTROL\n"));

                if (u32Length < sizeof(DR_CONTROL_RSP))
                {
                    RDPDRLOG(("IOCompletion DR_IRP_MJ_DEVICE_CONTROL u32Length %d < sizeof(DR_CONTROL_RSP) %d\n",
                              u32Length, sizeof(DR_CONTROL_RSP)));
                    rc = VERR_VRDP_PROTOCOL_ERROR;
                    break;
                }

                DR_CONTROL_RSP *pRsp = (DR_CONTROL_RSP *)pHdr;

                RDPDRLOG(("IOCompletion DR_IRP_MJ_DEVICE_CONTROL u32OutputBufferLength %d, total length %d\n",
                          pRsp->u32OutputBufferLength, sizeof(DR_CONTROL_RSP) + pRsp->u32OutputBufferLength));

                const uint8_t *pu8OutputBuffer = pu8Input + sizeof(DR_CONTROL_RSP);
                uint32_t cbOutputBuffer = u32Length - sizeof(DR_CONTROL_RSP);

#ifdef DEBUG_sunlover
                hexdumprel((uint8_t *)pu8OutputBuffer, u32Length - sizeof(DR_CONTROL_RSP));
#endif

                if (cbOutputBuffer < pRsp->u32OutputBufferLength)
                {
                    RDPDRLOG(("IOCompletion DR_IRP_MJ_DEVICE_CONTROL cbOutputBuffer %d < OutputBufferLength %d\n",
                              cbOutputBuffer, pRsp->u32OutputBufferLength));
                    rc = VERR_VRDP_PROTOCOL_ERROR;
                    break;
                }

                /* Update the IO structure. */
                RDPDRIOCTL *pIO = (RDPDRIOCTL *)pPktHdr;

                pIO->u32ReturnedOutputLength = pRsp->u32OutputBufferLength;
                pIO->pu8OutputBuffer = pu8OutputBuffer;
                pIO->u32OutputBufferSize = cbOutputBuffer;

                rdpdrDispatchIOCompletion(pIO, pHdr->u32DeviceId, pHdr->u32IoStatus);
            }
        }

        RDPDRPktRelease(pPktHdr);
    }
    else
    {
        /* Ignore the packet. */
        rc = VERR_INVALID_CONTEXT;
    }

    RDPDRLOG(("IOCompletion %Rrc\n", rc));
    return rc;
}

/** @todo this must be a generic method with u32MajorFunction parameter and RDPDRPKTHDR instead of RDPDRIOCTL. */
int VRDPChannelRDPDR::rdpdrDispatchIOCompletion(RDPDRIOCTL *pIO, uint32_t u32DeviceId, uint32_t u32IoStatus)
{
    /** @todo a dynamic mapping of u32DeviceId to the caller. */
    if (u32DeviceId == m_smartcard.u32DeviceId)
    {
        RDPDRPktAddRef(&pIO->hdr);
        m_pvrdptp->Client()->Server()->SCard()->SCardIOCompletion(m_pvrdptp->Client()->Id(),
                                                                  m_smartcard.u32DeviceId,
                                                                  u32IoStatus,
                                                                  pIO);
    }

    return VINF_SUCCESS;
}
