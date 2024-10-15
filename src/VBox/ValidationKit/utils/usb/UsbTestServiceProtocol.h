/* $Id: UsbTestServiceProtocol.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, Protocol Header.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_SRC_usb_UsbTestServiceProtocol_h
#define VBOX_INCLUDED_SRC_usb_UsbTestServiceProtocol_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

/**
 * Common Packet header (for requests and replies).
 */
typedef struct UTSPKTHDR
{
    /** The unpadded packet length. This include this header. */
    uint32_t        cb;
    /** The CRC-32 for the packet starting from the opcode field.  0 if the packet
     *  hasn't been CRCed. */
    uint32_t        uCrc32;
    /** Packet opcode, an unterminated ASCII string.  */
    uint8_t         achOpcode[8];
} UTSPKTHDR;
AssertCompileSize(UTSPKTHDR, 16);
/** Pointer to a packet header. */
typedef UTSPKTHDR *PUTSPKTHDR;
/** Pointer to a packet header. */
typedef UTSPKTHDR const *PCUTSPKTHDR;
/** Pointer to a packet header pointer. */
typedef PUTSPKTHDR *PPUTSPKTHDR;

/** Packet alignment. */
#define UTSPKT_ALIGNMENT                16
/** Max packet size. */
#define UTSPKT_MAX_SIZE                 _256K

/**
 * Status packet.
 */
typedef struct UTSPKTSTS
{
    /** Embedded common packet header. */
    UTSPKTHDR       Hdr;
    /** The IPRT status code of the request. */
    int32_t         rcReq;
    /** Size of the optional status message following this structure -
     * only for errors. */
    uint32_t        cchStsMsg;
    /** Padding - reserved. */
    uint8_t         au8Padding[8];
} UTSPKTSTS;
AssertCompileSizeAlignment(UTSPKTSTS, UTSPKT_ALIGNMENT);
/** Pointer to a status packet header. */
typedef UTSPKTSTS *PUTSPKTSTS;

#define UTSPKT_OPCODE_HOWDY             "HOWDY   "

/** 32bit protocol version consisting of a 16bit major and 16bit minor part. */
#define UTS_PROTOCOL_VS (UTS_PROTOCOL_VS_MAJOR | UTS_PROTOCOL_VS_MINOR)
/** The major version part of the protocol version. */
#define UTS_PROTOCOL_VS_MAJOR (1 << 16)
/** The minor version part of the protocol version. */
#define UTS_PROTOCOL_VS_MINOR (0)

/**
 * The HOWDY request structure.
 */
typedef struct UTSPKTREQHOWDY
{
    /** Embedded packet header. */
    UTSPKTHDR       Hdr;
    /** Version of the protocol the client wants to use. */
    uint32_t        uVersion;
    /** Mask of USB device connections the client wants to use. */
    uint32_t        fUsbConn;
    /** The number of characters for the hostname. */
    uint32_t        cchHostname;
    /** The client host name as terminated ASCII string. */
    char            achHostname[68];
} UTSPKTREQHOWDY;
AssertCompileSizeAlignment(UTSPKTREQHOWDY, UTSPKT_ALIGNMENT);
/** Pointer to a HOWDY request structure. */
typedef UTSPKTREQHOWDY *PUTSPKTREQHOWDY;

/**
 * The HOWDY reply structure.
 */
typedef struct UTSPKTREPHOWDY
{
    /** Status packet. */
    UTSPKTSTS       Sts;
    /** Version to use for the established connection. */
    uint32_t        uVersion;
    /** Mask of supported USB device connections for this connection. */
    uint32_t        fUsbConn;
    /** Port number the USB/IP server is listening on if
     * the client requested USB/IP support and the server can
     * deliver it. */
    uint32_t        uUsbIpPort;
    /** Maximum number of devices supported over USB/IP
     * at the same time. */
    uint32_t        cUsbIpDevices;
    /** Maximum number of physical devices supported for this client
     * if a physical connection is present. */
    uint32_t        cPhysicalDevices;
    /** Padding - reserved. */
    uint8_t         au8Padding[12];
} UTSPKTREPHOWDY;
AssertCompileSizeAlignment(UTSPKTREPHOWDY, UTSPKT_ALIGNMENT);
/** Pointer to a HOWDY reply structure. */
typedef UTSPKTREPHOWDY *PUTSPKTREPHOWDY;

/** Connections over USB/IP are supported. */
#define UTSPKT_HOWDY_CONN_F_USBIP    RT_BIT_32(0)
/** The server has a physical connection available to the client
 * which can be used for testing. */
#define UTSPKT_HOWDY_CONN_F_PHYSICAL RT_BIT_32(1)


#define UTSPKT_OPCODE_BYE               "BYE     "

/* No additional structures for BYE. */

#define UTSPKT_OPCODE_GADGET_CREATE     "GDGTCRT "

/**
 * The GADGET CREATE request structure.
 */
typedef struct UTSPKTREQGDGTCTOR
{
    /** Embedded packet header. */
    UTSPKTHDR       Hdr;
    /** Gadget type. */
    uint32_t        u32GdgtType;
    /** Access methods. */
    uint32_t        u32GdgtAccess;
    /** Number of config items - following this structure. */
    uint32_t        u32CfgItems;
    /** Reserved. */
    uint32_t        u32Rsvd0;
} UTSPKTREQGDGTCTOR;
AssertCompileSizeAlignment(UTSPKTREQGDGTCTOR, UTSPKT_ALIGNMENT);
/** Pointer to a GADGET CREATE structure. */
typedef UTSPKTREQGDGTCTOR *PUTSPKTREQGDGTCTOR;

/** Gadget type - Test device. */
#define UTSPKT_GDGT_CREATE_TYPE_TEST UINT32_C(0x1)

/** Gadget acess method - USB/IP. */
#define UTSPKT_GDGT_CREATE_ACCESS_USBIP UINT32_C(0x1)

/**
 * Configuration item.
 */
typedef struct UTSPKTREQGDGTCTORCFGITEM
{
    /** Size of the key incuding termination in bytes. */
    uint32_t        u32KeySize;
    /** Item type. */
    uint32_t        u32Type;
    /** Size of the value string including termination in bytes. */
    uint32_t        u32ValSize;
    /** Reserved. */
    uint32_t        u32Rsvd0;
} UTSPKTREQGDGTCTORCFGITEM;
AssertCompileSizeAlignment(UTSPKTREQGDGTCTORCFGITEM, UTSPKT_ALIGNMENT);
/** Pointer to a configuration item. */
typedef UTSPKTREQGDGTCTORCFGITEM *PUTSPKTREQGDGTCTORCFGITEM;

/** Boolean configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_BOOLEAN UINT32_C(1)
/** String configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_STRING  UINT32_C(2)
/** Unsigned 8-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_UINT8   UINT32_C(3)
/** Unsigned 16-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_UINT16  UINT32_C(4)
/** Unsigned 32-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_UINT32  UINT32_C(5)
/** Unsigned 64-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_UINT64  UINT32_C(6)
/** Signed 8-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_INT8    UINT32_C(7)
/** Signed 16-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_INT16   UINT32_C(8)
/** Signed 32-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_INT32   UINT32_C(9)
/** Signed 64-bit integer configuration item type. */
#define UTSPKT_GDGT_CFG_ITEM_TYPE_INT64   UINT32_C(10)

/**
 * The GADGET CREATE reply structure.
 */
typedef struct UTSPKTREPGDGTCTOR
{
    /** Status packet. */
    UTSPKTSTS       Sts;
    /** The gadget ID on success. */
    uint32_t        idGadget;
    /** Bus ID the gadget is attached to */
    uint32_t        u32BusId;
    /** Device ID of the gadget on the bus. */
    uint32_t        u32DevId;
    /** Padding - reserved. */
    uint8_t         au8Padding[4];
} UTSPKTREPGDGTCTOR;
AssertCompileSizeAlignment(UTSPKTREPGDGTCTOR, UTSPKT_ALIGNMENT);
/** Pointer to a GADGET CREATE structure. */
typedef UTSPKTREPGDGTCTOR *PUTSPKTREPGDGTCTOR;


#define UTSPKT_OPCODE_GADGET_DESTROY    "GDGTDTOR"

/**
 * The GADGET DESTROY request structure.
 */
typedef struct UTSPKTREQGDGTDTOR
{
    /** Embedded packet header. */
    UTSPKTHDR       Hdr;
    /** Gadget ID as returned from the GADGET CREATE request on success. */
    uint32_t        idGadget;
    /** Padding - reserved. */
    uint8_t         au8Padding[12];
} UTSPKTREQGDGTDTOR;
AssertCompileSizeAlignment(UTSPKTREQGDGTDTOR, UTSPKT_ALIGNMENT);
/** Pointer to a GADGET DESTROY structure. */
typedef UTSPKTREQGDGTDTOR *PUTSPKTREQGDGTDTOR;

/* No additional structure for the reply (just standard STATUS packet). */

#define UTSPKT_OPCODE_GADGET_CONNECT    "GDGTCNCT"

/**
 * The GADGET CONNECT request structure.
 */
typedef struct UTSPKTREQGDGTCNCT
{
    /** Embedded packet header. */
    UTSPKTHDR       Hdr;
    /** Gadget ID as returned from the GADGET CREATE request on success. */
    uint32_t        idGadget;
    /** Padding - reserved. */
    uint8_t         au8Padding[12];
} UTSPKTREQGDGTCNCT;
AssertCompileSizeAlignment(UTSPKTREQGDGTCNCT, UTSPKT_ALIGNMENT);
/** Pointer to a GADGET CONNECT request structure. */
typedef UTSPKTREQGDGTCNCT *PUTSPKTREQGDGTCNCT;

/* No additional structure for the reply (just standard STATUS packet). */

#define UTSPKT_OPCODE_GADGET_DISCONNECT "GDGTDCNT"

/**
 * The GADGET DISCONNECT request structure.
 */
typedef struct UTSPKTREQGDGTDCNT
{
    /** Embedded packet header. */
    UTSPKTHDR       Hdr;
    /** Gadget ID as returned from the GADGET CREATE request on success. */
    uint32_t        idGadget;
    /** Padding - reserved. */
    uint8_t         au8Padding[12];
} UTSPKTREQGDGTDCNT;
AssertCompileSizeAlignment(UTSPKTREQGDGTDCNT, UTSPKT_ALIGNMENT);
/** Pointer to a GADGET CONNECT request structure. */
typedef UTSPKTREQGDGTDCNT *PUTSPKTREQGDGTDCNT;

/* No additional structure for the reply (just standard STATUS packet). */

/**
 * Checks if the two opcodes match.
 *
 * @returns true on match, false on mismatch.
 * @param   pPktHdr             The packet header.
 * @param   pszOpcode2          The opcode we're comparing with.  Does not have
 *                              to be the whole 8 chars long.
 */
DECLINLINE(bool) utsIsSameOpcode(PCUTSPKTHDR pPktHdr, const char *pszOpcode2)
{
    if (pPktHdr->achOpcode[0] != pszOpcode2[0])
        return false;
    if (pPktHdr->achOpcode[1] != pszOpcode2[1])
        return false;

    unsigned i = 2;
    while (   i < RT_SIZEOFMEMB(UTSPKTHDR, achOpcode)
           && pszOpcode2[i] != '\0')
    {
        if (pPktHdr->achOpcode[i] != pszOpcode2[i])
            break;
        i++;
    }

    if (   i < RT_SIZEOFMEMB(UTSPKTHDR, achOpcode)
        && pszOpcode2[i] == '\0')
    {
        while (   i < RT_SIZEOFMEMB(UTSPKTHDR, achOpcode)
               && pPktHdr->achOpcode[i] == ' ')
            i++;
    }

    return i == RT_SIZEOFMEMB(UTSPKTHDR, achOpcode);
}

/**
 * Converts a UTS request packet from host to network byte ordering.
 *
 * @param   pPktHdr           The packet to convert.
 */
DECLHIDDEN(void) utsProtocolReqH2N(PUTSPKTHDR pPktHdr);

/**
 * Converts a UTS request packet from network to host byte ordering.
 *
 * @param   pPktHdr           The packet to convert.
 */
DECLHIDDEN(void) utsProtocolReqN2H(PUTSPKTHDR pPktHdr);

/**
 * Converts a UTS reply packet from host to network byte ordering.
 *
 * @param   pPktHdr           The packet to convert.
 */
DECLHIDDEN(void) utsProtocolRepH2N(PUTSPKTSTS pPktHdr);

/**
 * Converts a UTS reply packet from network to host byte ordering.
 *
 * @param   pPktHdr           The packet to convert.
 */
DECLHIDDEN(void) utsProtocolRepN2H(PUTSPKTSTS pPktHdr);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_usb_UsbTestServiceProtocol_h */
