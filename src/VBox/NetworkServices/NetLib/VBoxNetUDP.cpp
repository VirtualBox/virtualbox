/* $Id: VBoxNetUDP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetUDP - IntNet UDP Client Routines.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEFAULT
#include "VBoxNetLib.h"
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/rand.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/intnetinline.h>


/**
 * Checks if the head of the receive ring is a UDP packet matching the given
 * criteria.
 *
 * @returns Pointer to the data if it matches.
 * @param   pBuf            The IntNet buffers.
 * @param   uDstPort        The destination port to match.
 * @param   pDstMac         The destination address to match if
 *                          VBOXNETUDP_MATCH_UNICAST is specied.
 * @param   fFlags          Flags indicating what to match and some debug stuff.
 *                          See VBOXNETUDP_MATCH_*.
 * @param   pHdrs           Where to return the pointers to the headers.
 *                          Optional.
 * @param   pcb             Where to return the size of the data on success.
 */
void *VBoxNetUDPMatch(PINTNETBUF pBuf, unsigned uDstPort, PCRTMAC pDstMac, uint32_t fFlags, PVBOXNETUDPHDRS pHdrs, size_t *pcb)
{
    /*
     * Clear return values so we can return easier on mismatch.
     */
    *pcb = 0;
    if (pHdrs)
    {
        pHdrs->pEth  = NULL;
        pHdrs->pIpv4 = NULL;
        pHdrs->pUdp  = NULL;
    }

    /*
     * Valid IntNet Ethernet frame?
     */
    PCINTNETHDR pHdr = IntNetRingGetNextFrameToRead(&pBuf->Recv);
    if (    !pHdr
        ||  (   pHdr->u8Type != INTNETHDR_TYPE_FRAME
             && pHdr->u8Type != INTNETHDR_TYPE_GSO))
        return NULL;

    size_t          cbFrame = pHdr->cbFrame;
    const void     *pvFrame = IntNetHdrGetFramePtr(pHdr, pBuf);
    PCPDMNETWORKGSO pGso    = NULL;
    if (pHdr->u8Type == INTNETHDR_TYPE_GSO)
    {
        pGso = (PCPDMNETWORKGSO)pvFrame;
        if (!PDMNetGsoIsValid(pGso, cbFrame, cbFrame - sizeof(*pGso)))
            return NULL;
        /** @todo IPv6 UDP support, goes for this entire function really.  Not really
         *        important yet since this is currently only used by the DHCP server. */
        if (pGso->u8Type != PDMNETWORKGSOTYPE_IPV4_UDP)
            return NULL;
        pvFrame  = pGso + 1;
        cbFrame -= sizeof(*pGso);
    }

    PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pvFrame;
    if (pHdrs)
        pHdrs->pEth = pEthHdr;

#ifdef IN_RING3
    /* Dump if to stderr/log if that's wanted. */
    if (fFlags & VBOXNETUDP_MATCH_PRINT_STDERR)
    {
        RTStrmPrintf(g_pStdErr, "frame: cb=%04x dst=%.6Rhxs src=%.6Rhxs type=%04x%s\n",
                     cbFrame, &pEthHdr->DstMac, &pEthHdr->SrcMac, RT_BE2H_U16(pEthHdr->EtherType),
                     !memcmp(&pEthHdr->DstMac, pDstMac, sizeof(*pDstMac)) ? " Mine!" : "");
    }
#endif

    /*
     * Ethernet matching.
     */

    /* Ethernet min frame size. */
    if (cbFrame < 64)
        return NULL;

    /* Match Ethertype: IPV4? */
    /** @todo VLAN tagging? */
    if (pEthHdr->EtherType != RT_H2BE_U16_C(RTNET_ETHERTYPE_IPV4))
        return NULL;

    /* Match destination address (ethernet) */
    if (    (   !(fFlags & VBOXNETUDP_MATCH_UNICAST)
             || memcmp(&pEthHdr->DstMac, pDstMac, sizeof(pEthHdr->DstMac)))
        &&  (   !(fFlags & VBOXNETUDP_MATCH_BROADCAST)
             || pEthHdr->DstMac.au16[0] != 0xffff
             || pEthHdr->DstMac.au16[1] != 0xffff
             || pEthHdr->DstMac.au16[2] != 0xffff))
        return NULL;

    /*
     * If we're working on a GSO frame, we need to make sure the length fields
     * are set correctly (they are usually set to 0).
     */
    if (pGso)
        PDMNetGsoPrepForDirectUse(pGso, (void *)pvFrame, cbFrame, PDMNETCSUMTYPE_NONE);

    /*
     * IP validation and matching.
     */
    PCRTNETIPV4 pIpHdr = (PCRTNETIPV4)(pEthHdr + 1);
    if (pHdrs)
        pHdrs->pIpv4 = pIpHdr;

    /* Protocol: UDP */
    if (pIpHdr->ip_p != RTNETIPV4_PROT_UDP)
        return NULL;

    /* Valid IPv4 header? */
    size_t const offIpHdr = (uintptr_t)pIpHdr - (uintptr_t)pEthHdr;
    if (!RTNetIPv4IsHdrValid(pIpHdr, cbFrame - offIpHdr, cbFrame - offIpHdr, !pGso /*fChecksum*/))
        return NULL;

    /*
     * UDP matching and validation.
     */
    PCRTNETUDP  pUdpHdr   = (PCRTNETUDP)((uint32_t *)pIpHdr + pIpHdr->ip_hl);
    if (pHdrs)
        pHdrs->pUdp = pUdpHdr;

    /* Destination port */
    if (RT_BE2H_U16(pUdpHdr->uh_dport) != uDstPort)
        return NULL;

    if (!pGso)
    {
        /* Validate the UDP header according to flags. */
        size_t      offUdpHdr = (uintptr_t)pUdpHdr - (uintptr_t)pEthHdr;
        if (fFlags & (VBOXNETUDP_MATCH_CHECKSUM | VBOXNETUDP_MATCH_REQUIRE_CHECKSUM))
        {
            if (!RTNetIPv4IsUDPValid(pIpHdr, pUdpHdr, pUdpHdr + 1, cbFrame - offUdpHdr, true /*fChecksum*/))
                return NULL;
            if (    (fFlags & VBOXNETUDP_MATCH_REQUIRE_CHECKSUM)
                &&  !pUdpHdr->uh_sum)
                return NULL;
        }
        else
        {
            if (!RTNetIPv4IsUDPSizeValid(pIpHdr, pUdpHdr, cbFrame - offUdpHdr))
                return NULL;
        }
    }

    /*
     * We've got a match!
     */
    *pcb = RT_N2H_U16(pUdpHdr->uh_ulen) - sizeof(*pUdpHdr);
    return (void *)(pUdpHdr + 1);
}


/** Internal worker for VBoxNetUDPUnicast and VBoxNetUDPBroadcast. */
static int vboxnetudpSend(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf,
                          RTNETADDRIPV4 SrcIPv4Addr, PCRTMAC pSrcMacAddr, unsigned uSrcPort,
                          RTNETADDRIPV4 DstIPv4Addr, PCRTMAC pDstMacAddr, unsigned uDstPort,
                          void const *pvData, size_t cbData)
{
    INTNETSEG aSegs[4];

    /* the Ethernet header */
    RTNETETHERHDR EtherHdr;
    EtherHdr.DstMac     = *pDstMacAddr;
    EtherHdr.SrcMac     = *pSrcMacAddr;
    EtherHdr.EtherType  = RT_H2BE_U16_C(RTNET_ETHERTYPE_IPV4);

    aSegs[0].pv   = &EtherHdr;
    aSegs[0].cb   = sizeof(EtherHdr);
    aSegs[0].Phys = NIL_RTHCPHYS;

    /* the IP header */
    RTNETIPV4 IpHdr;
    unsigned  cbIdHdr = RT_UOFFSETOF(RTNETIPV4, ip_options);
    IpHdr.ip_v          = 4;
    IpHdr.ip_hl         = cbIdHdr >> 2;
    IpHdr.ip_tos        = 0;
    IpHdr.ip_len        = RT_H2BE_U16((uint16_t)(cbData + sizeof(RTNETUDP) + cbIdHdr));
    IpHdr.ip_id         = (uint16_t)RTRandU32();
    IpHdr.ip_off        = 0;
    IpHdr.ip_ttl        = 255;
    IpHdr.ip_p          = RTNETIPV4_PROT_UDP;
    IpHdr.ip_sum        = 0;
    IpHdr.ip_src        = SrcIPv4Addr;
    IpHdr.ip_dst        = DstIPv4Addr;
    IpHdr.ip_sum        = RTNetIPv4HdrChecksum(&IpHdr);

    aSegs[1].pv   = &IpHdr;
    aSegs[1].cb   = cbIdHdr;
    aSegs[1].Phys = NIL_RTHCPHYS;


    /* the UDP bit */
    RTNETUDP UdpHdr;
    UdpHdr.uh_sport     = RT_H2BE_U16(uSrcPort);
    UdpHdr.uh_dport     = RT_H2BE_U16(uDstPort);
    UdpHdr.uh_ulen      = RT_H2BE_U16((uint16_t)(cbData + sizeof(RTNETUDP)));
#if 0
    UdpHdr.uh_sum       = 0; /* pretend checksumming is disabled */
#else
    UdpHdr.uh_sum       = RTNetIPv4UDPChecksum(&IpHdr, &UdpHdr, pvData);
#endif

    aSegs[2].pv   = &UdpHdr;
    aSegs[2].cb   = sizeof(UdpHdr);
    aSegs[2].Phys = NIL_RTHCPHYS;

    /* the payload */
    aSegs[3].pv   = (void *)pvData;
    aSegs[3].cb   = (uint32_t)cbData;
    aSegs[3].Phys = NIL_RTHCPHYS;


    /* send it */
    return VBoxNetIntIfSend(pSession, hIf, pBuf, RT_ELEMENTS(aSegs), &aSegs[0], true /* fFlush */);
}


/**
 * Sends an unicast UDP packet.
 *
 * @returns VBox status code.
 * @param   pSession        The support driver session handle.
 * @param   hIf             The interface handle.
 * @param   pBuf            The interface buffer.
 * @param   SrcIPv4Addr     The source IPv4 address.
 * @param   pSrcMacAddr     The source MAC address.
 * @param   uSrcPort        The source port number.
 * @param   DstIPv4Addr     The destination IPv4 address. Can be broadcast.
 * @param   pDstMacAddr     The destination MAC address.
 * @param   uDstPort        The destination port number.
 * @param   pvData          The data payload.
 * @param   cbData          The size of the data payload.
 */
int     VBoxNetUDPUnicast(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf,
                          RTNETADDRIPV4 SrcIPv4Addr, PCRTMAC pSrcMacAddr, unsigned uSrcPort,
                          RTNETADDRIPV4 DstIPv4Addr, PCRTMAC pDstMacAddr, unsigned uDstPort,
                          void const *pvData, size_t cbData)
{
    return vboxnetudpSend(pSession, hIf, pBuf,
                          SrcIPv4Addr, pSrcMacAddr, uSrcPort,
                          DstIPv4Addr, pDstMacAddr, uDstPort,
                          pvData, cbData);
}


/**
 * Sends a broadcast UDP packet.
 *
 * @returns VBox status code.
 * @param   pSession        The support driver session handle.
 * @param   hIf             The interface handle.
 * @param   pBuf            The interface buffer.
 * @param   SrcIPv4Addr     The source IPv4 address.
 * @param   pSrcMacAddr     The source MAC address.
 * @param   uSrcPort        The source port number.
 * @param   uDstPort        The destination port number.
 * @param   pvData          The data payload.
 * @param   cbData          The size of the data payload.
 */
int     VBoxNetUDPBroadcast(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf,
                            RTNETADDRIPV4 SrcIPv4Addr, PCRTMAC pSrcMacAddr, unsigned uSrcPort,
                            unsigned uDstPort,
                            void const *pvData, size_t cbData)
{
    RTNETADDRIPV4   IPv4AddrBrdCast;
    IPv4AddrBrdCast.u = UINT32_C(0xffffffff);
    RTMAC           MacBrdCast;
    MacBrdCast.au16[0] = MacBrdCast.au16[1] = MacBrdCast.au16[2] = UINT16_C(0xffff);

    return vboxnetudpSend(pSession, hIf, pBuf,
                          SrcIPv4Addr, pSrcMacAddr, uSrcPort,
                          IPv4AddrBrdCast, &MacBrdCast, uDstPort,
                          pvData, cbData);
}

