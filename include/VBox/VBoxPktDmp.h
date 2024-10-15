/* $Id: VBoxPktDmp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxPktDmp.h - Dump Ethernet frame into debug log.
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

#ifndef VBOX_INCLUDED_VBoxPktDmp_h
#define VBOX_INCLUDED_VBoxPktDmp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/net.h>
#include <iprt/log.h>
#if defined(LOG_ENABLED) && !defined(VBOX_DEVICE_STRUCT_TESTCASE)
# include <iprt/asm.h>
#endif


DECLINLINE(const char *) vboxEthTypeStr(uint16_t uType)
{
    switch (uType)
    {
        case RTNET_ETHERTYPE_IPV4: return "IP";
        case RTNET_ETHERTYPE_IPV6: return "IPv6";
        case RTNET_ETHERTYPE_ARP:  return "ARP";
    }
    return "unknown";
}


DECLINLINE(void) vboxEthPacketDump(const char *pcszInstance, const char *pcszText, const uint8_t *pcPacket, uint32_t cb)
{
#if defined(LOG_ENABLED) && !defined(VBOX_DEVICE_STRUCT_TESTCASE)
    AssertReturnVoid(cb >= 14);

    const uint8_t *pHdr = pcPacket;
    const uint8_t *pEnd = pcPacket + cb;
    AssertReturnVoid(pEnd - pHdr >= 14);
    uint16_t uEthType = RT_N2H_U16(*(uint16_t*)(pHdr+12));
    Log2(("%s: %s (%d bytes), %RTmac => %RTmac, EthType=%s(0x%x)\n", pcszInstance,
          pcszText, cb, pHdr+6, pHdr, vboxEthTypeStr(uEthType), uEthType));
    pHdr += sizeof(RTNETETHERHDR);
    if (uEthType == RTNET_ETHERTYPE_VLAN)
    {
        AssertReturnVoid(pEnd - pHdr >= 4);
        uEthType = RT_N2H_U16(*(uint16_t*)(pHdr+2));
        Log2((" + VLAN: id=%d EthType=%s(0x%x)\n", RT_N2H_U16(*(uint16_t*)(pHdr)) & 0xFFF,
              vboxEthTypeStr(uEthType), uEthType));
        pHdr += 2 * sizeof(uint16_t);
    }
    uint8_t uProto = 0xFF;
    switch (uEthType)
    {
        case RTNET_ETHERTYPE_IPV6:
            AssertReturnVoid(pEnd - pHdr >= 40);
            uProto = pHdr[6];
            Log2((" + IPv6: %RTnaipv6 => %RTnaipv6\n", pHdr+8, pHdr+24));
            pHdr += 40;
            break;
        case RTNET_ETHERTYPE_IPV4:
            AssertReturnVoid(pEnd - pHdr >= 20);
            uProto = pHdr[9];
            Log2((" + IP: %RTnaipv4 => %RTnaipv4\n", *(uint32_t*)(pHdr+12), *(uint32_t*)(pHdr+16)));
            pHdr += (pHdr[0] & 0xF) * 4;
            break;
        case RTNET_ETHERTYPE_ARP:
            AssertReturnVoid(pEnd - pHdr >= 28);
            AssertReturnVoid(RT_N2H_U16(*(uint16_t*)(pHdr+2)) == RTNET_ETHERTYPE_IPV4);
            switch (RT_N2H_U16(*(uint16_t*)(pHdr+6)))
            {
                case 1: /* ARP request */
                    Log2((" + ARP-REQ: who-has %RTnaipv4 tell %RTnaipv4\n",
                          *(uint32_t*)(pHdr+24), *(uint32_t*)(pHdr+14)));
                    break;
                case 2: /* ARP reply */
                    Log2((" + ARP-RPL: %RTnaipv4 is-at %RTmac\n",
                          *(uint32_t*)(pHdr+14), pHdr+8));
                    break;
                default:
                    Log2((" + ARP: unknown op %d\n", RT_N2H_U16(*(uint16_t*)(pHdr+6))));
                    break;
            }
            break;
        /* There is no default case as uProto is initialized with 0xFF */
    }
    while (uProto != 0xFF)
    {
        switch (uProto)
        {
            case 0:  /* IPv6 Hop-by-Hop option*/
            case 60: /* IPv6 Destination option*/
            case 43: /* IPv6 Routing option */
            case 44: /* IPv6 Fragment option */
                Log2((" + IPv6 option (%d): <not implemented>\n", uProto));
                uProto = pHdr[0];
                pHdr += pHdr[1] * 8 + 8; /* Skip to the next extension/protocol */
                break;
            case 51: /* IPv6 IPsec AH */
                Log2((" + IPv6 IPsec AH: <not implemented>\n"));
                uProto = pHdr[0];
                pHdr += (pHdr[1] + 2) * 4; /* Skip to the next extension/protocol */
                break;
            case 50: /* IPv6 IPsec ESP */
                /* Cannot decode IPsec, fall through */
                Log2((" + IPv6 IPsec ESP: <not implemented>\n"));
                uProto = 0xFF;
                break;
            case 59: /* No Next Header */
                Log2((" + IPv6 No Next Header\n"));
                uProto = 0xFF;
                break;
            case 58: /* IPv6-ICMP */
                switch (pHdr[0])
                {
                    case 1:   Log2((" + IPv6-ICMP: destination unreachable, code %d\n", pHdr[1])); break;
                    case 128: Log2((" + IPv6-ICMP: echo request\n")); break;
                    case 129: Log2((" + IPv6-ICMP: echo reply\n")); break;
                    default:  Log2((" + IPv6-ICMP: unknown type %d, code %d\n", pHdr[0], pHdr[1])); break;
                }
                uProto = 0xFF;
                break;
            case 1: /* ICMP */
                switch (pHdr[0])
                {
                    case 0:  Log2((" + ICMP: echo reply\n")); break;
                    case 8:  Log2((" + ICMP: echo request\n")); break;
                    case 3:  Log2((" + ICMP: destination unreachable, code %d\n", pHdr[1])); break;
                    default: Log2((" + ICMP: unknown type %d, code %d\n", pHdr[0], pHdr[1])); break;
                }
                uProto = 0xFF;
                break;
            case 6: /* TCP */
                Log2((" + TCP: src=%d dst=%d seq=%x ack=%x\n",
                      RT_N2H_U16(*(uint16_t*)(pHdr)), RT_N2H_U16(*(uint16_t*)(pHdr+2)),
                      RT_N2H_U32(*(uint32_t*)(pHdr+4)), RT_N2H_U32(*(uint32_t*)(pHdr+8))));
                uProto = 0xFF;
                break;
            case 17: /* UDP */
                Log2((" + UDP: src=%d dst=%d\n",
                      RT_N2H_U16(*(uint16_t*)(pHdr)), RT_N2H_U16(*(uint16_t*)(pHdr+2))));
                uProto = 0xFF;
                break;
            default:
                Log2((" + Unknown: proto=0x%x\n", uProto));
                uProto = 0xFF;
                break;
        }
    }
    Log3(("%.*Rhxd\n", cb, pcPacket));
#else
    RT_NOREF4(pcszInstance, pcszText, pcPacket, cb);
#endif
}

#endif /* !VBOX_INCLUDED_VBoxPktDmp_h */
