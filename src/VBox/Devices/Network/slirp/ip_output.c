/* $Id: ip_output.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - IP output.
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

/*
 * This code is based on:
 *
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)ip_output.c 8.3 (Berkeley) 1/21/94
 * ip_output.c,v 1.9 1994/11/16 10:17:10 jkh Exp
 */

/*
 * Changes and additions relating to SLiRP are
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>
#include <iprt/errcore.h>
#include "alias.h"

static const uint8_t broadcast_ethaddr[6] =
{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static int rt_lookup_in_cache(PNATState pData, uint32_t dst, uint8_t *ether)
{
    int rc;
    LogFlowFunc(("ENTER: dst:%RTnaipv4, ether:%RTmac\n", dst, ether));
    if (dst == INADDR_BROADCAST)
    {
        memcpy(ether, broadcast_ethaddr, ETH_ALEN);
        LogFlowFunc(("LEAVE: VINF_SUCCESS\n"));
        return VINF_SUCCESS;
    }

    rc = slirp_arp_lookup_ether_by_ip(pData, dst, ether);
    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("LEAVE: %Rrc\n", rc));
        return rc;
    }

    rc = bootp_cache_lookup_ether_by_ip(pData, dst, ether);
    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("LEAVE: %Rrc\n", rc));
        return rc;
    }
    /*
     * no chance to send this packet, sorry, we will request ether address via ARP
     */
    slirp_arp_who_has(pData, dst);
    LogFlowFunc(("LEAVE: VERR_NOT_FOUND\n"));
    return VERR_NOT_FOUND;
}

/*
 * IP output.  The packet in mbuf chain m contains a skeletal IP
 * header (with len, off, ttl, proto, tos, src, dst).
 * The mbuf chain containing the packet will be freed.
 * The mbuf opt, if present, will not be freed.
 */
int
ip_output(PNATState pData, struct socket *so, struct mbuf *m0)
{
    return ip_output0(pData, so, m0, 0);
}

/* This function will free m0! */
int
ip_output0(PNATState pData, struct socket *so, struct mbuf *m0, int urg)
{
    register struct ip *ip;
    register struct mbuf *m = m0;
    register int hlen = sizeof(struct ip);
    int len, off, error = 0;
    struct ethhdr *eh = NULL;
    uint8_t eth_dst[ETH_ALEN];
    int rc = 1;

    STAM_PROFILE_START(&pData->StatIP_output, a);

#ifdef LOG_ENABLED
    LogFlowFunc(("ip_output: so = %R[natsock], m0 = %p\n", so, m0));
#else
    NOREF(so);
#endif

    M_ASSERTPKTHDR(m);
    Assert(m->m_pkthdr.header);

#if 0 /* We do no options */
    if (opt)
    {
        m = ip_insertoptions(m, opt, &len);
        hlen = len;
    }
#endif
    ip = mtod(m, struct ip *);
    LogFunc(("ip(src:%RTnaipv4, dst:%RTnaipv4)\n", ip->ip_src, ip->ip_dst));
    /*
     * Fill in IP header.
     */
    ip->ip_v = IPVERSION;
    ip->ip_off &= IP_DF;
    ip->ip_id = RT_H2N_U16(ip_currid);
    ip->ip_hl = hlen >> 2;
    ip_currid++;
    ipstat.ips_localout++;

    /* Current TCP/IP stack hasn't routing information at
     * all so we need to calculate destination ethernet address
     */
    rc = rt_lookup_in_cache(pData, ip->ip_dst.s_addr, eth_dst);
    if (RT_FAILURE(rc))
        goto exit_drop_package;

    eh = (struct ethhdr *)(m->m_data - ETH_HLEN);
    /*
     * If small enough for interface, can just send directly.
     */
    if ((u_int16_t)ip->ip_len <= if_mtu)
    {
        ip->ip_len = RT_H2N_U16((u_int16_t)ip->ip_len);
        ip->ip_off = RT_H2N_U16((u_int16_t)ip->ip_off);
        ip->ip_sum = 0;
        ip->ip_sum = cksum(m, hlen);

        if (!(m->m_flags & M_SKIP_FIREWALL)){
            STAM_PROFILE_START(&pData->StatALIAS_output, b);
            rc = LibAliasOut(pData->proxy_alias, mtod(m, char *), m_length(m, NULL));
            if (rc == PKT_ALIAS_IGNORED)
            {
                Log(("NAT: packet was droppped\n"));
                goto exit_drop_package;
            }
            STAM_PROFILE_STOP(&pData->StatALIAS_output, b);
        }
        else
            m->m_flags &= ~M_SKIP_FIREWALL;

        memcpy(eh->h_source, eth_dst, ETH_ALEN);

        LogFlowFunc(("ip(ip_src:%RTnaipv4, ip_dst:%RTnaipv4)\n",
                     ip->ip_src, ip->ip_dst));
        if_encap(pData, ETH_P_IP, m, urg? ETH_ENCAP_URG : 0);
        goto done;
     }

    /*
     * Too large for interface; fragment if possible.
     * Must be able to put at least 8 bytes per fragment.
     */
    if (ip->ip_off & IP_DF)
    {
        error = -1;
        ipstat.ips_cantfrag++;
        goto exit_drop_package;
    }

    len = (if_mtu - hlen) &~ 7;       /* ip databytes per packet */
    if (len < 8)
    {
        error = -1;
        goto exit_drop_package;
    }

    {
        int mhlen, firstlen = len;
        struct mbuf **mnext = &m->m_nextpkt;
        char *buf; /* intermediate buffer we'll use for a copy of the original packet */
        /*
         * Loop through length of segment after first fragment,
         * make new header and copy data of each part and link onto chain.
         */
        m0 = m;
        mhlen = ip->ip_hl << 2;
        Log(("NAT:ip:frag: mhlen = %d\n", mhlen));
        for (off = hlen + len; off < (u_int16_t)ip->ip_len; off += len)
        {
            register struct ip *mhip;
            m = m_getjcl(pData, M_NOWAIT, MT_HEADER , M_PKTHDR, slirp_size(pData));
            if (m == 0)
            {
                error = -1;
                ipstat.ips_odropped++;
                goto exit_drop_package;
            }
            m->m_data += if_maxlinkhdr;
            mhip = mtod(m, struct ip *);
            *mhip = *ip;
            m->m_pkthdr.header = mtod(m, void *);
            /* we've calculated eth_dst for first packet */
#if 0 /* No options */
            if (hlen > sizeof (struct ip))
            {
                mhlen = ip_optcopy(ip, mhip) + sizeof (struct ip);
                mhip->ip_hl = mhlen >> 2;
            }
#endif
            m->m_len = mhlen;
            mhip->ip_off = ((off - mhlen) >> 3) + (ip->ip_off & ~IP_MF);
            if (ip->ip_off & IP_MF)
                mhip->ip_off |= IP_MF;
            if (off + len >= (u_int16_t)ip->ip_len)
                len = (u_int16_t)ip->ip_len - off;
            else
                mhip->ip_off |= IP_MF;
            mhip->ip_len = RT_H2N_U16((u_int16_t)(len + mhlen));

            buf = RTMemAlloc(len);
            Log(("NAT:ip:frag: alloc = %d\n", len));
            m_copydata(m0, off, len, buf); /* copy to buffer */
            Log(("NAT:ip:frag: m_copydata(m0 = %p,off = %d, len = %d,)\n", m0, off, len));

            m->m_data += mhlen;
            m->m_len -= mhlen;
            m_copyback(pData, m, 0, len, buf); /* copy from buffer */
            Log(("NAT:ip:frag: m_copyback(m = %p,, len = %d,)\n", m, len));
            m->m_data -= mhlen;
            m->m_len += mhlen;
            RTMemFree(buf);
            Assert((m->m_len == (mhlen + len)));

            mhip->ip_off = RT_H2N_U16((u_int16_t)(mhip->ip_off));
            mhip->ip_sum = 0;
            mhip->ip_sum = cksum(m, mhlen);
            *mnext = m;
            mnext = &m->m_nextpkt;
            ipstat.ips_ofragments++;
        }
        /*
         * Update first fragment by trimming what's been copied out
         * and updating header, then send each fragment (in order).
         *
         * note: m_adj do all required releases for chained mbufs.
         */
        m = m0;
        m_adj(m, mhlen + firstlen - (u_int16_t)ip->ip_len);
        Log(("NAT:ip:frag: m_adj(m(m_len:%d) = %p, len = %d)\n", m->m_len, m, mhlen + firstlen - (u_int16_t)ip->ip_len));
        ip->ip_len = RT_H2N_U16((u_int16_t)mhlen + firstlen);
        ip->ip_off = RT_H2N_U16((u_int16_t)(ip->ip_off | IP_MF));
        ip->ip_sum = 0;
        ip->ip_sum = cksum(m, mhlen);

        if (!(m->m_flags & M_SKIP_FIREWALL)){
            /** @todo We can't alias all fragments because the way libalias processing
             * the fragments brake the sequence. libalias put alias_address to the source
             * address of IP header of fragment, while IP header of the first packet is
             * is unmodified. That confuses guest's TCP/IP stack and guest drop the sequence.
             * Here we're letting libalias to process the first packet and send the rest as is,
             * it's exactly the way in of packet are processing in proxyonly way.
             * Here we need investigate what should be done to avoid such behavior and find right
             * solution.
             */
            int rcLa;

            rcLa = LibAliasOut(pData->proxy_alias, mtod(m, char *), m->m_len);
            if (rcLa == PKT_ALIAS_IGNORED)
            {
                Log(("NAT: packet was droppped\n"));
                goto exit_drop_package;
            }
            Log2(("NAT: LibAlias return %d\n", rcLa));
        }
        else
            m->m_flags &= ~M_SKIP_FIREWALL;
        for (m = m0; m; m = m0)
        {
            m0 = m->m_nextpkt;
            m->m_nextpkt = 0;
            if (error == 0)
            {
                m->m_data -= ETH_HLEN;
                eh = mtod(m, struct ethhdr *);
                m->m_data += ETH_HLEN;
                memcpy(eh->h_source, eth_dst, ETH_ALEN);

                Log(("NAT:ip:frag: if_encap(,,m(m_len = %d) = %p,0)\n", m->m_len, m));
                if_encap(pData, ETH_P_IP, m, 0);
            }
            else
                m_freem(pData, m);
        }

        if (error == 0)
            ipstat.ips_fragmented++;
    }

done:
    STAM_PROFILE_STOP(&pData->StatIP_output, a);
    LogFlowFunc(("LEAVE: %d\n", error));
    return error;

exit_drop_package:
    m_freem(pData, m0);
    STAM_PROFILE_STOP(&pData->StatIP_output, a);
    LogFlowFunc(("LEAVE: %d\n", error));
    return error;
}
