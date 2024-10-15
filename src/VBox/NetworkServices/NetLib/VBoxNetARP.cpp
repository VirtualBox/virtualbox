/* $Id: VBoxNetARP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetARP - IntNet ARP Client Routines.
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
#include <iprt/string.h>
#include <VBox/intnetinline.h>
#include <VBox/log.h>


/**
 * Deal with ARP queries.
 *
 * @returns true if ARP.
 *
 * @param   pSession        The support driver session.
 * @param   hIf             The internal network interface handle.
 * @param   pBuf            The internal network interface buffer.
 * @param   pMacAddr        Our MAC address.
 * @param   IPv4Addr        Our IPv4 address.
 */
bool VBoxNetArpHandleIt(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf, PCRTMAC pMacAddr, RTNETADDRIPV4 IPv4Addr)
{
    /*
     * Valid IntNet Ethernet frame? Skip GSO, no ARP in there.
     */
    PCINTNETHDR pHdr = IntNetRingGetNextFrameToRead(&pBuf->Recv);
    if (   !pHdr
        || pHdr->u8Type != INTNETHDR_TYPE_FRAME)
        return false;

    size_t          cbFrame = pHdr->cbFrame;
    const void     *pvFrame = IntNetHdrGetFramePtr(pHdr, pBuf);
    PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pvFrame;

    /*
     * Arp frame?
     */
    if (pEthHdr->EtherType != RT_H2N_U16_C(RTNET_ETHERTYPE_ARP))
        return false;
    if (   (   pEthHdr->DstMac.au16[0] != 0xffff
            || pEthHdr->DstMac.au16[1] != 0xffff
            || pEthHdr->DstMac.au16[2] != 0xffff)
        && (   pEthHdr->DstMac.au16[0] != pMacAddr->au16[0]
            || pEthHdr->DstMac.au16[1] != pMacAddr->au16[1]
            || pEthHdr->DstMac.au16[2] != pMacAddr->au16[2])
       )
        return false;
    if (cbFrame < sizeof(RTNETARPIPV4) + sizeof(RTNETETHERHDR))
        return false;

    PCRTNETARPHDR pArpHdr = (PCRTNETARPHDR)(pEthHdr + 1);
    if (pArpHdr->ar_htype != RT_H2N_U16_C(RTNET_ARP_ETHER))
        return false;
    if (pArpHdr->ar_hlen != sizeof(RTMAC))
        return false;
    if (pArpHdr->ar_ptype != RT_H2N_U16_C(RTNET_ETHERTYPE_IPV4))
        return false;
    if (pArpHdr->ar_plen != sizeof(RTNETADDRIPV4))
        return false;

    /* It's ARP, alright. Anything we need to do something about. */
    PCRTNETARPIPV4 pArp = (PCRTNETARPIPV4)pArpHdr;
    switch (pArp->Hdr.ar_oper)
    {
        case RT_H2N_U16_C(RTNET_ARPOP_REQUEST):
        case RT_H2N_U16_C(RTNET_ARPOP_REVREQUEST):
        case RT_H2N_U16_C(RTNET_ARPOP_INVREQUEST):
            break;
        default:
            return true;
    }

    /*
     * Deal with the queries.
     */
    RTNETARPIPV4 Reply;
    switch (pArp->Hdr.ar_oper)
    {
        /* 'Who has ar_tpa? Tell ar_spa.'  */
        case RT_H2N_U16_C(RTNET_ARPOP_REQUEST):
            if (pArp->ar_tpa.u != IPv4Addr.u)
                return true;
            Reply.Hdr.ar_oper = RT_H2N_U16_C(RTNET_ARPOP_REPLY);
            break;

        case RT_H2N_U16_C(RTNET_ARPOP_REVREQUEST):
            if (    pArp->ar_tha.au16[0] != pMacAddr->au16[0]
                ||  pArp->ar_tha.au16[1] != pMacAddr->au16[1]
                ||  pArp->ar_tha.au16[2] != pMacAddr->au16[2])
                return true;
            Reply.Hdr.ar_oper = RT_H2N_U16_C(RTNET_ARPOP_REVREPLY);
            break;

        case RT_H2N_U16_C(RTNET_ARPOP_INVREQUEST):
            /** @todo RTNET_ARPOP_INVREQUEST */
            return true;
            //Reply.Hdr.ar_oper = RT_H2N_U16_C(RTNET_ARPOP_INVREPLY);
            //break;
    }

    /*
     * Complete the reply and send it.
     */
    Reply.Hdr.ar_htype = RT_H2N_U16_C(RTNET_ARP_ETHER);
    Reply.Hdr.ar_ptype = RT_H2N_U16_C(RTNET_ETHERTYPE_IPV4);
    Reply.Hdr.ar_hlen  = sizeof(RTMAC);
    Reply.Hdr.ar_plen  = sizeof(RTNETADDRIPV4);
    Reply.ar_sha = *pMacAddr;
    Reply.ar_spa = IPv4Addr;
    Reply.ar_tha = pArp->ar_sha;
    Reply.ar_tpa = pArp->ar_spa;


    RTNETETHERHDR EthHdr;
    EthHdr.DstMac    = pArp->ar_sha;
    EthHdr.SrcMac    = *pMacAddr;
    EthHdr.EtherType = RT_H2N_U16_C(RTNET_ETHERTYPE_ARP);

    uint8_t abTrailer[60 - sizeof(Reply) - sizeof(EthHdr)];
    RT_ZERO(abTrailer);

    INTNETSEG aSegs[3];
    aSegs[0].cb = sizeof(EthHdr);
    aSegs[0].pv = &EthHdr;

    aSegs[1].pv = &Reply;
    aSegs[1].cb = sizeof(Reply);

    aSegs[2].pv = &abTrailer[0];
    aSegs[2].cb = sizeof(abTrailer);

    VBoxNetIntIfSend(pSession, hIf, pBuf, RT_ELEMENTS(aSegs), &aSegs[0], true /* fFlush */);

    return true;
}

