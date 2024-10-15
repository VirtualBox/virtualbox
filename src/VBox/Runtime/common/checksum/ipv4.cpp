/* $Id: ipv4.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - IPv4 Checksum calculation and validation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/net.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>


/**
 * Calculates the checksum of the IPv4 header.
 *
 * @returns Checksum (network endian).
 * @param   pIpHdr      Pointer to the IPv4 header to checksum, network endian (big).
 *                      Assumes the caller already checked the minimum size requirement.
 */
RTDECL(uint16_t) RTNetIPv4HdrChecksum(PCRTNETIPV4 pIpHdr)
{
    uint16_t const *paw = (uint16_t const *)pIpHdr;
    uint32_t u32Sum = paw[0]            /* ip_hl */
                    + paw[1]            /* ip_len */
                    + paw[2]            /* ip_id */
                    + paw[3]            /* ip_off */
                    + paw[4]            /* ip_ttl */
                    /*+ paw[5] == 0 */  /* ip_sum */
                    + paw[6]            /* ip_src */
                    + paw[7]            /* ip_src:16 */
                    + paw[8]            /* ip_dst */
                    + paw[9];           /* ip_dst:16 */
    /* any options */
    if (pIpHdr->ip_hl > 20 / 4)
    {
        /* this is a bit insane... (identical to the TCP header) */
        switch (pIpHdr->ip_hl)
        {
            case 6:  u32Sum += paw[10] + paw[11]; break;
            case 7:  u32Sum += paw[10] + paw[11] + paw[12] + paw[13]; break;
            case 8:  u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15]; break;
            case 9:  u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17]; break;
            case 10: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19]; break;
            case 11: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21]; break;
            case 12: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23]; break;
            case 13: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23] + paw[24] + paw[25]; break;
            case 14: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23] + paw[24] + paw[25] + paw[26] + paw[27]; break;
            case 15: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23] + paw[24] + paw[25] + paw[26] + paw[27] + paw[28] + paw[29]; break;
            default:
                AssertFailed();
        }
    }

    /* 16-bit one complement fun */
    u32Sum = (u32Sum >> 16) + (u32Sum & 0xffff);  /* hi + low words */
    u32Sum += u32Sum >> 16;                     /* carry */
    return (uint16_t)~u32Sum;
}
RT_EXPORT_SYMBOL(RTNetIPv4HdrChecksum);


/**
 * Verifies the header version, header size, packet size, and header checksum
 * of the specified IPv4 header.
 *
 * @returns true if valid, false if invalid.
 * @param   pIpHdr      Pointer to the IPv4 header to validate. Network endian (big).
 * @param   cbHdrMax    The max header size, or  the max size of what pIpHdr points
 *                      to if you like. Note that an IPv4 header can be up to 60 bytes.
 * @param   cbPktMax    The max IP packet size, IP header and payload. This doesn't have
 *                      to be mapped following pIpHdr.
 * @param   fChecksum   Whether to validate the checksum (GSO).
 */
RTDECL(bool) RTNetIPv4IsHdrValid(PCRTNETIPV4 pIpHdr, size_t cbHdrMax, size_t cbPktMax, bool fChecksum)
{
    /*
     * The header fields.
     */
    Assert(cbPktMax >= cbHdrMax);
    if (RT_UNLIKELY(cbHdrMax < RTNETIPV4_MIN_LEN))
        return false;
    if (RT_UNLIKELY(pIpHdr->ip_hl * 4 < RTNETIPV4_MIN_LEN))
        return false;
    if (RT_UNLIKELY((size_t)pIpHdr->ip_hl * 4 > cbHdrMax))
    {
        Assert((size_t)pIpHdr->ip_hl * 4 > cbPktMax); /* You'll hit this if you mapped/copy too little of the header! */
        return false;
    }
    if (RT_UNLIKELY(pIpHdr->ip_v != 4))
        return false;
    if (RT_UNLIKELY(RT_BE2H_U16(pIpHdr->ip_len) > cbPktMax))
        return false;

    /*
     * The header checksum if requested.
     */
    if (fChecksum)
    {
        uint16_t u16Sum = RTNetIPv4HdrChecksum(pIpHdr);
        if (RT_UNLIKELY(pIpHdr->ip_sum != u16Sum))
            return false;
    }
    return true;
}
RT_EXPORT_SYMBOL(RTNetIPv4IsHdrValid);


/**
 * Calculates the checksum of a pseudo header given an IPv4 header [inlined].
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pIpHdr      The IP header (network endian (big)).
 */
DECLINLINE(uint32_t) rtNetIPv4PseudoChecksum(PCRTNETIPV4 pIpHdr)
{
    uint16_t cbPayload = RT_BE2H_U16(pIpHdr->ip_len) - pIpHdr->ip_hl * 4;
    uint32_t u32Sum = pIpHdr->ip_src.au16[0]
                    + pIpHdr->ip_src.au16[1]
                    + pIpHdr->ip_dst.au16[0]
                    + pIpHdr->ip_dst.au16[1]
#ifdef RT_BIG_ENDIAN
                    + pIpHdr->ip_p
#else
                    + ((uint32_t)pIpHdr->ip_p << 8)
#endif
                    + RT_H2BE_U16(cbPayload);
    return u32Sum;
}


/**
 * Calculates the checksum of a pseudo header given an IPv4 header.
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pIpHdr      The IP header (network endian (big)).
 */
RTDECL(uint32_t) RTNetIPv4PseudoChecksum(PCRTNETIPV4 pIpHdr)
{
    return rtNetIPv4PseudoChecksum(pIpHdr);
}
RT_EXPORT_SYMBOL(RTNetIPv4PseudoChecksum);


/**
 * Calculates the checksum of a pseudo header given the individual components.
 *
 * @returns 32-bit intermediary checksum value.
 * @param   SrcAddr         The source address in host endian.
 * @param   DstAddr         The destination address in host endian.
 * @param   bProtocol       The protocol number.
 * @param   cbPkt           The packet size (host endian of course) (no IPv4 header).
 */
RTDECL(uint32_t) RTNetIPv4PseudoChecksumBits(RTNETADDRIPV4 SrcAddr, RTNETADDRIPV4 DstAddr, uint8_t bProtocol, uint16_t cbPkt)
{
    uint32_t u32Sum = RT_H2BE_U16(SrcAddr.au16[0])
                    + RT_H2BE_U16(SrcAddr.au16[1])
                    + RT_H2BE_U16(DstAddr.au16[0])
                    + RT_H2BE_U16(DstAddr.au16[1])
#ifdef RT_BIG_ENDIAN
                    + bProtocol
#else
                    + ((uint32_t)bProtocol << 8)
#endif
                    + RT_H2BE_U16(cbPkt);
    return u32Sum;
}
RT_EXPORT_SYMBOL(RTNetIPv4PseudoChecksumBits);


/**
 * Adds the checksum of the UDP header to the intermediate checksum value [inlined].
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pUdpHdr         Pointer to the UDP header to checksum, network endian (big).
 * @param   u32Sum          The 32-bit intermediate checksum value.
 */
DECLINLINE(uint32_t) rtNetIPv4AddUDPChecksum(PCRTNETUDP pUdpHdr, uint32_t u32Sum)
{
    u32Sum += pUdpHdr->uh_sport
            + pUdpHdr->uh_dport
            /*+ pUdpHdr->uh_sum = 0 */
            + pUdpHdr->uh_ulen;
    return u32Sum;
}


/**
 * Adds the checksum of the UDP header to the intermediate checksum value.
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pUdpHdr         Pointer to the UDP header to checksum, network endian (big).
 * @param   u32Sum          The 32-bit intermediate checksum value.
 */
RTDECL(uint32_t) RTNetIPv4AddUDPChecksum(PCRTNETUDP pUdpHdr, uint32_t u32Sum)
{
    return rtNetIPv4AddUDPChecksum(pUdpHdr, u32Sum);
}
RT_EXPORT_SYMBOL(RTNetIPv4AddUDPChecksum);


/**
 * Adds the checksum of the TCP header to the intermediate checksum value [inlined].
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pTcpHdr         Pointer to the TCP header to checksum, network
 *                          endian (big). Assumes the caller has already validate
 *                          it and made sure the entire header is present.
 * @param   u32Sum          The 32-bit intermediate checksum value.
 */
DECLINLINE(uint32_t) rtNetIPv4AddTCPChecksum(PCRTNETTCP pTcpHdr, uint32_t u32Sum)
{
    uint16_t const *paw = (uint16_t const *)pTcpHdr;
    u32Sum += paw[0]                    /* th_sport */
            + paw[1]                    /* th_dport */
            + paw[2]                    /* th_seq */
            + paw[3]                    /* th_seq:16 */
            + paw[4]                    /* th_ack */
            + paw[5]                    /* th_ack:16 */
            + paw[6]                    /* th_off, th_x2, th_flags */
            + paw[7]                    /* th_win */
            /*+ paw[8] == 0 */          /* th_sum */
            + paw[9];                   /* th_urp */
    if (pTcpHdr->th_off > RTNETTCP_MIN_LEN / 4)
    {
        /* this is a bit insane... (identical to the IPv4 header) */
        switch (pTcpHdr->th_off)
        {
            case 6:  u32Sum += paw[10] + paw[11]; break;
            case 7:  u32Sum += paw[10] + paw[11] + paw[12] + paw[13]; break;
            case 8:  u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15]; break;
            case 9:  u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17]; break;
            case 10: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19]; break;
            case 11: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21]; break;
            case 12: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23]; break;
            case 13: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23] + paw[24] + paw[25]; break;
            case 14: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23] + paw[24] + paw[25] + paw[26] + paw[27]; break;
            case 15: u32Sum += paw[10] + paw[11] + paw[12] + paw[13] + paw[14] + paw[15] + paw[16] + paw[17] + paw[18] + paw[19] + paw[20] + paw[21] + paw[22] + paw[23] + paw[24] + paw[25] + paw[26] + paw[27] + paw[28] + paw[29]; break;
            default:
                AssertFailed();
        }
    }

    return u32Sum;
}


/**
 * Adds the checksum of the TCP header to the intermediate checksum value.
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pTcpHdr         Pointer to the TCP header to checksum, network
 *                          endian (big). Assumes the caller has already validate
 *                          it and made sure the entire header is present.
 * @param   u32Sum          The 32-bit intermediate checksum value.
 */
RTDECL(uint32_t) RTNetIPv4AddTCPChecksum(PCRTNETTCP pTcpHdr, uint32_t u32Sum)
{
    return rtNetIPv4AddTCPChecksum(pTcpHdr, u32Sum);
}
RT_EXPORT_SYMBOL(RTNetIPv4AddTCPChecksum);


/**
 * Adds the checksum of the specified data segment to the intermediate checksum value [inlined].
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pvData          Pointer to the data that should be checksummed.
 * @param   cbData          The number of bytes to checksum.
 * @param   u32Sum          The 32-bit intermediate checksum value.
 * @param   pfOdd           This is used to keep track of odd bits, initialize to false
 *                          when starting to checksum the data (aka text) after a TCP
 *                          or UDP header (data never start at an odd offset).
 */
DECLINLINE(uint32_t) rtNetIPv4AddDataChecksum(void const *pvData, size_t cbData, uint32_t u32Sum, bool *pfOdd)
{
    if (*pfOdd)
    {
#ifdef RT_BIG_ENDIAN
        /* there was an odd byte in the previous chunk, add the lower byte. */
        u32Sum += *(uint8_t *)pvData;
#else
        /* there was an odd byte in the previous chunk, add the upper byte. */
        u32Sum += (uint32_t)*(uint8_t *)pvData << 8;
#endif
        /* skip the byte. */
        cbData--;
        if (!cbData)
            return u32Sum;
        pvData = (uint8_t const *)pvData + 1;
    }

    /* iterate the data. */
    uint16_t const *pw = (uint16_t const *)pvData;
    while (cbData > 1)
    {
        u32Sum += *pw;
        pw++;
        cbData -= 2;
    }

    /* handle odd byte. */
    if (cbData)
    {
#ifdef RT_BIG_ENDIAN
        u32Sum += (uint32_t)*(uint8_t *)pw << 8;
#else
        u32Sum += *(uint8_t *)pw;
#endif
        *pfOdd = true;
    }
    else
        *pfOdd = false;
    return u32Sum;
}

/**
 * Adds the checksum of the specified data segment to the intermediate checksum value.
 *
 * @returns 32-bit intermediary checksum value.
 * @param   pvData          The data bits to checksum.
 * @param   cbData          The number of bytes to checksum.
 * @param   u32Sum          The 32-bit intermediate checksum value.
 * @param   pfOdd           This is used to keep track of odd bits, initialize to false
 *                          when starting to checksum the data (aka text) after a TCP
 *                          or UDP header (data never start at an odd offset).
 */
RTDECL(uint32_t) RTNetIPv4AddDataChecksum(void const *pvData, size_t cbData, uint32_t u32Sum, bool *pfOdd)
{
    return rtNetIPv4AddDataChecksum(pvData, cbData, u32Sum, pfOdd);
}
RT_EXPORT_SYMBOL(RTNetIPv4AddDataChecksum);


/**
 * Finalizes a IPv4 checksum [inlined].
 *
 * @returns The checksum (network endian).
 * @param   u32Sum          The 32-bit intermediate checksum value.
 */
DECLINLINE(uint16_t) rtNetIPv4FinalizeChecksum(uint32_t u32Sum)
{
    /* 16-bit one complement fun */
    u32Sum = (u32Sum >> 16) + (u32Sum & 0xffff);  /* hi + low words */
    u32Sum += u32Sum >> 16;                       /* carry */
    return (uint16_t)~u32Sum;
}


/**
 * Finalizes a IPv4 checksum.
 *
 * @returns The checksum (network endian).
 * @param   u32Sum          The 32-bit intermediate checksum value.
 */
RTDECL(uint16_t) RTNetIPv4FinalizeChecksum(uint32_t u32Sum)
{
    return rtNetIPv4FinalizeChecksum(u32Sum);
}
RT_EXPORT_SYMBOL(RTNetIPv4FinalizeChecksum);


/**
 * Calculates the checksum for the UDP header given the UDP header w/ payload
 * and the checksum of the pseudo header.
 *
 * @returns The checksum (network endian).
 * @param   u32Sum          The checksum of the pseudo header. See
 *                          RTNetIPv4PseudoChecksum and RTNetIPv6PseudoChecksum.
 * @param   pUdpHdr         Pointer to the UDP header and the payload, in
 *                          network endian (big).  We use the uh_ulen field to
 *                          figure out how much to checksum.
 */
RTDECL(uint16_t) RTNetUDPChecksum(uint32_t u32Sum, PCRTNETUDP pUdpHdr)
{
    bool fOdd;
    u32Sum = rtNetIPv4AddUDPChecksum(pUdpHdr, u32Sum);
    fOdd = false;
    u32Sum = rtNetIPv4AddDataChecksum(pUdpHdr + 1, RT_BE2H_U16(pUdpHdr->uh_ulen) - sizeof(*pUdpHdr), u32Sum, &fOdd);
    return rtNetIPv4FinalizeChecksum(u32Sum);
}
RT_EXPORT_SYMBOL(RTNetUDPChecksum);


/**
 * Calculates the checksum for the UDP header given the IP header,
 * UDP header and payload.
 *
 * @returns The checksum (network endian).
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 * @param   pUdpHdr         Pointer to the UDP header, in network endian (big).
 * @param   pvData          Pointer to the UDP payload. The size is taken from the
 *                          UDP header and the caller is supposed to have validated
 *                          this before calling.
 */
RTDECL(uint16_t) RTNetIPv4UDPChecksum(PCRTNETIPV4 pIpHdr, PCRTNETUDP pUdpHdr, void const *pvData)
{
    bool fOdd;
    uint32_t u32Sum = rtNetIPv4PseudoChecksum(pIpHdr);
    u32Sum = rtNetIPv4AddUDPChecksum(pUdpHdr, u32Sum);
    fOdd = false;
    u32Sum = rtNetIPv4AddDataChecksum(pvData, RT_BE2H_U16(pUdpHdr->uh_ulen) - sizeof(*pUdpHdr), u32Sum, &fOdd);
    return rtNetIPv4FinalizeChecksum(u32Sum);
}
RT_EXPORT_SYMBOL(RTNetIPv4UDPChecksum);


/**
 * Simple verification of an UDP packet size.
 *
 * @returns true if valid, false if invalid.
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 *                          This is assumed to be valid and the minimum size being mapped.
 * @param   pUdpHdr         Pointer to the UDP header, in network endian (big).
 * @param   cbPktMax        The max UDP packet size, UDP header and payload (data).
 */
DECLINLINE(bool) rtNetIPv4IsUDPSizeValid(PCRTNETIPV4 pIpHdr, PCRTNETUDP pUdpHdr, size_t cbPktMax)
{
    /*
     * Size validation.
     */
    size_t cb;
    if (RT_UNLIKELY(cbPktMax < RTNETUDP_MIN_LEN))
        return false;
    cb = RT_BE2H_U16(pUdpHdr->uh_ulen);
    if (RT_UNLIKELY(cb > cbPktMax))
        return false;
    if (RT_UNLIKELY(cb > (size_t)(RT_BE2H_U16(pIpHdr->ip_len) - pIpHdr->ip_hl * 4)))
        return false;
    return true;
}


/**
 * Simple verification of an UDP packet size.
 *
 * @returns true if valid, false if invalid.
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 *                          This is assumed to be valid and the minimum size being mapped.
 * @param   pUdpHdr         Pointer to the UDP header, in network endian (big).
 * @param   cbPktMax        The max UDP packet size, UDP header and payload (data).
 */
RTDECL(bool) RTNetIPv4IsUDPSizeValid(PCRTNETIPV4 pIpHdr, PCRTNETUDP pUdpHdr, size_t cbPktMax)
{
    return rtNetIPv4IsUDPSizeValid(pIpHdr, pUdpHdr, cbPktMax);
}
RT_EXPORT_SYMBOL(RTNetIPv4IsUDPSizeValid);


/**
 * Simple verification of an UDP packet (size + checksum).
 *
 * @returns true if valid, false if invalid.
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 *                          This is assumed to be valid and the minimum size being mapped.
 * @param   pUdpHdr         Pointer to the UDP header, in network endian (big).
 * @param   pvData          Pointer to the data, assuming it's one single segment
 *                          and that cbPktMax - sizeof(RTNETUDP) is mapped here.
 * @param   cbPktMax        The max UDP packet size, UDP header and payload (data).
 * @param   fChecksum       Whether to validate the checksum (GSO).
 */
RTDECL(bool) RTNetIPv4IsUDPValid(PCRTNETIPV4 pIpHdr, PCRTNETUDP pUdpHdr, void const *pvData, size_t cbPktMax, bool fChecksum)
{
    if (RT_UNLIKELY(!rtNetIPv4IsUDPSizeValid(pIpHdr, pUdpHdr, cbPktMax)))
        return false;
    if (fChecksum && pUdpHdr->uh_sum)
    {
        uint16_t u16Sum = RTNetIPv4UDPChecksum(pIpHdr, pUdpHdr, pvData);
        if (RT_UNLIKELY(pUdpHdr->uh_sum != u16Sum))
            return false;
    }
    return true;
}
RT_EXPORT_SYMBOL(RTNetIPv4IsUDPValid);


/**
 * Calculates the checksum for the TCP header given the IP header,
 * TCP header and payload.
 *
 * @returns The checksum (network endian).
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 * @param   pTcpHdr         Pointer to the TCP header, in network endian (big).
 * @param   pvData          Pointer to the TCP payload. The size is derived from
 *                          the two headers and the caller is supposed to have
 *                          validated this before calling.  If NULL, we assume
 *                          the data follows immediately after the TCP header.
 */
RTDECL(uint16_t) RTNetIPv4TCPChecksum(PCRTNETIPV4 pIpHdr, PCRTNETTCP pTcpHdr, void const *pvData)
{
    bool fOdd;
    size_t cbData;
    uint32_t u32Sum = rtNetIPv4PseudoChecksum(pIpHdr);
    u32Sum = rtNetIPv4AddTCPChecksum(pTcpHdr, u32Sum);
    fOdd = false;
    cbData = RT_BE2H_U16(pIpHdr->ip_len) - pIpHdr->ip_hl * 4 - pTcpHdr->th_off * 4;
    u32Sum = rtNetIPv4AddDataChecksum(pvData ? pvData : (uint8_t const *)pTcpHdr + pTcpHdr->th_off * 4,
                                      cbData, u32Sum, &fOdd);
    return rtNetIPv4FinalizeChecksum(u32Sum);
}
RT_EXPORT_SYMBOL(RTNetIPv4TCPChecksum);


/**
 * Calculates the checksum for the TCP header given the TCP header, payload and
 * the checksum of the pseudo header.
 *
 * This is not specific to IPv4.
 *
 * @returns The checksum (network endian).
 * @param   u32Sum          The checksum of the pseudo header. See
 *                          RTNetIPv4PseudoChecksum and RTNetIPv6PseudoChecksum.
 * @param   pTcpHdr         Pointer to the TCP header, in network endian (big).
 * @param   pvData          Pointer to the TCP payload.
 * @param   cbData          The size of the TCP payload.
 */
RTDECL(uint16_t) RTNetTCPChecksum(uint32_t u32Sum, PCRTNETTCP pTcpHdr, void const *pvData, size_t cbData)
{
    bool fOdd;
    u32Sum = rtNetIPv4AddTCPChecksum(pTcpHdr, u32Sum);
    fOdd = false;
    u32Sum = rtNetIPv4AddDataChecksum(pvData, cbData, u32Sum, &fOdd);
    return rtNetIPv4FinalizeChecksum(u32Sum);
}
RT_EXPORT_SYMBOL(RTNetTCPChecksum);


/**
 * Verification of a TCP header.
 *
 * @returns true if valid, false if invalid.
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 *                          This is assumed to be valid and the minimum size being mapped.
 * @param   pTcpHdr         Pointer to the TCP header, in network endian (big).
 * @param   cbHdrMax        The max TCP header size (what pTcpHdr points to).
 * @param   cbPktMax        The max TCP packet size, TCP header and payload (data).
 */
DECLINLINE(bool) rtNetIPv4IsTCPSizeValid(PCRTNETIPV4 pIpHdr, PCRTNETTCP pTcpHdr, size_t cbHdrMax, size_t cbPktMax)
{
    size_t cbTcpHdr;
    size_t cbTcp;

    Assert(cbPktMax >= cbHdrMax);

    /*
     * Size validations.
     */
    if (RT_UNLIKELY(cbPktMax < RTNETTCP_MIN_LEN))
        return false;
    cbTcpHdr = pTcpHdr->th_off * 4;
    if (RT_UNLIKELY(cbTcpHdr > cbHdrMax))
        return false;
    cbTcp = RT_BE2H_U16(pIpHdr->ip_len) - pIpHdr->ip_hl * 4;
    if (RT_UNLIKELY(cbTcp > cbPktMax))
        return false;
    return true;
}


/**
 * Simple verification of an TCP packet size.
 *
 * @returns true if valid, false if invalid.
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 *                          This is assumed to be valid and the minimum size being mapped.
 * @param   pTcpHdr         Pointer to the TCP header, in network endian (big).
 * @param   cbHdrMax        The max TCP header size (what pTcpHdr points to).
 * @param   cbPktMax        The max TCP packet size, TCP header and payload (data).
 */
RTDECL(bool) RTNetIPv4IsTCPSizeValid(PCRTNETIPV4 pIpHdr, PCRTNETTCP pTcpHdr, size_t cbHdrMax, size_t cbPktMax)
{
    return rtNetIPv4IsTCPSizeValid(pIpHdr, pTcpHdr, cbHdrMax, cbPktMax);
}
RT_EXPORT_SYMBOL(RTNetIPv4IsTCPSizeValid);


/**
 * Simple verification of an TCP packet (size + checksum).
 *
 * @returns true if valid, false if invalid.
 * @param   pIpHdr          Pointer to the IPv4 header, in network endian (big).
 *                          This is assumed to be valid and the minimum size being mapped.
 * @param   pTcpHdr         Pointer to the TCP header, in network endian (big).
 * @param   cbHdrMax        The max TCP header size (what pTcpHdr points to).
 * @param   pvData          Pointer to the data, assuming it's one single segment
 *                          and that cbPktMax - sizeof(RTNETTCP) is mapped here.
 *                          If NULL then we assume the data follows immediately after
 *                          the TCP header.
 * @param   cbPktMax        The max TCP packet size, TCP header and payload (data).
 * @param   fChecksum       Whether to validate the checksum (GSO).
 */
RTDECL(bool) RTNetIPv4IsTCPValid(PCRTNETIPV4 pIpHdr, PCRTNETTCP pTcpHdr, size_t cbHdrMax, void const *pvData, size_t cbPktMax,
                                 bool fChecksum)
{
    if (RT_UNLIKELY(!rtNetIPv4IsTCPSizeValid(pIpHdr, pTcpHdr, cbHdrMax, cbPktMax)))
        return false;
    if (fChecksum)
    {
        uint16_t u16Sum = RTNetIPv4TCPChecksum(pIpHdr, pTcpHdr, pvData);
        if (RT_UNLIKELY(pTcpHdr->th_sum != u16Sum))
            return false;
    }
    return true;
}
RT_EXPORT_SYMBOL(RTNetIPv4IsTCPValid);


/**
 * Minimal validation of a DHCP packet.
 *
 * This will fail on BOOTP packets (if sufficient data is supplied).
 * It will not verify the source and destination ports, that's the
 * caller's responsibility.
 *
 * This function will ASSUME that the hardware type is ethernet
 * and use that for htype/hlen validation.
 *
 * @returns true if valid, false if invalid.
 * @param   pUdpHdr         Pointer to the UDP header, in network endian (big).
 *                          This is assumed to be valid and fully mapped.
 * @param   pDhcp           Pointer to the DHCP packet.
 *                          This might not be the entire thing, see cbDhcp.
 * @param   cbDhcp          The number of valid bytes that pDhcp points to.
 * @param   pMsgType        Where to store the message type (if found).
 *                          This will be set to 0 if not found and on failure.
 */
RTDECL(bool) RTNetIPv4IsDHCPValid(PCRTNETUDP pUdpHdr, PCRTNETBOOTP pDhcp, size_t cbDhcp, uint8_t *pMsgType)
{
    ssize_t         cbLeft;
    uint8_t         MsgType;
    PCRTNETDHCPOPT  pOpt;
    NOREF(pUdpHdr); /** @todo rainy-day: Why isn't the UDP header used? */

    AssertPtrNull(pMsgType);
    if (pMsgType)
        *pMsgType = 0;

    /*
     * Validate all the header fields we're able to...
     */
    if (cbDhcp < RT_UOFFSETOF(RTNETBOOTP, bp_op) + sizeof(pDhcp->bp_op))
        return true;
    if (RT_UNLIKELY(    pDhcp->bp_op != RTNETBOOTP_OP_REQUEST
                    &&  pDhcp->bp_op != RTNETBOOTP_OP_REPLY))
        return false;

    if (cbDhcp < RT_UOFFSETOF(RTNETBOOTP, bp_htype) + sizeof(pDhcp->bp_htype))
        return true;
    if (RT_UNLIKELY(pDhcp->bp_htype != RTNET_ARP_ETHER))
        return false;

    if (cbDhcp < RT_UOFFSETOF(RTNETBOOTP, bp_hlen) + sizeof(pDhcp->bp_hlen))
        return true;
    if (RT_UNLIKELY(pDhcp->bp_hlen != sizeof(RTMAC)))
        return false;

    if (cbDhcp < RT_UOFFSETOF(RTNETBOOTP, bp_flags) + sizeof(pDhcp->bp_flags))
        return true;
    if (RT_UNLIKELY(RT_BE2H_U16(pDhcp->bp_flags) & ~(RTNET_DHCP_FLAGS_NO_BROADCAST)))
        return false;

    /*
     * Check the DHCP cookie and make sure it isn't followed by an END option
     * (because that seems to be indicating that it's BOOTP and not DHCP).
     */
    cbLeft = (ssize_t)cbDhcp - RT_UOFFSETOF(RTNETBOOTP, bp_vend.Dhcp.dhcp_cookie) + sizeof(pDhcp->bp_vend.Dhcp.dhcp_cookie);
    if (cbLeft < 0)
        return true;
    if (RT_UNLIKELY(RT_BE2H_U32(pDhcp->bp_vend.Dhcp.dhcp_cookie) != RTNET_DHCP_COOKIE))
        return false;
    if (cbLeft < 1)
        return true;
    pOpt = (PCRTNETDHCPOPT)&pDhcp->bp_vend.Dhcp.dhcp_opts[0];
    if (pOpt->dhcp_opt == RTNET_DHCP_OPT_END)
        return false;

    /*
     * Scan the options until we find the message type or run out of message.
     *
     * We're not strict about termination (END) for many reasons, however,
     * we don't accept END without MSG_TYPE.
     */
    MsgType = 0;
    while (cbLeft > 0)
    {
        if (pOpt->dhcp_opt == RTNET_DHCP_OPT_END)
        {
            /* Fail if no MSG_TYPE. */
            if (!MsgType)
                return false;
            break;
        }
        if (pOpt->dhcp_opt == RTNET_DHCP_OPT_PAD)
        {
            pOpt = (PCRTNETDHCPOPT)((uint8_t const *)pOpt + 1);
            cbLeft--;
        }
        else
        {
            switch (pOpt->dhcp_opt)
            {
                case RTNET_DHCP_OPT_MSG_TYPE:
                {
                    if (cbLeft < 3)
                        return true;
                    MsgType = *(const uint8_t *)(pOpt + 1);
                    switch (MsgType)
                    {
                        case RTNET_DHCP_MT_DISCOVER:
                        case RTNET_DHCP_MT_OFFER:
                        case RTNET_DHCP_MT_REQUEST:
                        case RTNET_DHCP_MT_DECLINE:
                        case RTNET_DHCP_MT_ACK:
                        case RTNET_DHCP_MT_NAC:
                        case RTNET_DHCP_MT_RELEASE:
                        case RTNET_DHCP_MT_INFORM:
                            break;

                        default:
                            /* we don't know this message type, fail. */
                            return false;
                    }

                    /* Found a known message type, consider the job done. */
                    if (pMsgType)
                        *pMsgType = MsgType;
                    return true;
                }
            }

            /* Skip the option. */
            cbLeft -= pOpt->dhcp_len + sizeof(*pOpt);
            pOpt = (PCRTNETDHCPOPT)((uint8_t const *)pOpt + pOpt->dhcp_len + sizeof(*pOpt));
        }
    }

    return true;
}
RT_EXPORT_SYMBOL(RTNetIPv4IsDHCPValid);

