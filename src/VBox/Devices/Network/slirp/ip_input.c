/* $Id: ip_input.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - IP input.
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
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *      @(#)ip_input.c  8.2 (Berkeley) 1/4/94
 * ip_input.c,v 1.11 1994/11/16 10:17:08 jkh Exp
 */

/*
 * Changes and additions relating to SLiRP are
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>
#include "ip_icmp.h"
#include "alias.h"


/*
 * IP initialization: fill in IP protocol switch table.
 * All protocols not implemented in kernel go to raw IP protocol handler.
 */
void
ip_init(PNATState pData)
{
    int i = 0;
    for (i = 0; i < IPREASS_NHASH; ++i)
        TAILQ_INIT(&ipq[i]);
    maxnipq = 100; /* ??? */
    maxfragsperpacket = 16;
    nipq = 0;
    ip_currid = tt.tv_sec & 0xffff;
    udp_init(pData);
    tcp_init(pData);
}

/*
 * Ip input routine.  Checksum and byte swap header.  If fragmented
 * try to reassemble.  Process options.  Pass to next level.
 */
void
ip_input(PNATState pData, struct mbuf *m)
{
    register struct ip *ip;
    int hlen = 0;
    int mlen = 0;
    int iplen = 0;

    STAM_PROFILE_START(&pData->StatIP_input, a);

    LogFlowFunc(("ENTER: m = %p\n", m));
    ip = mtod(m, struct ip *);
    Log2(("ip_dst=%RTnaipv4(len:%d) m_len = %d\n", ip->ip_dst, RT_N2H_U16(ip->ip_len), m->m_len));

    ipstat.ips_total++;

    mlen = m->m_len;

    if (mlen < sizeof(struct ip))
    {
        ipstat.ips_toosmall++;
        goto bad_free_m;
    }

    ip = mtod(m, struct ip *);
    if (ip->ip_v != IPVERSION)
    {
        ipstat.ips_badvers++;
        goto bad_free_m;
    }

    hlen = ip->ip_hl << 2;
    if (   hlen < sizeof(struct ip)
        || hlen > mlen)
    {
        /* min header length */
        ipstat.ips_badhlen++;                     /* or packet too short */
        goto bad_free_m;
    }

    /* keep ip header intact for ICMP reply
     * ip->ip_sum = cksum(m, hlen);
     * if (ip->ip_sum) {
     */
    if (cksum(m, hlen))
    {
        ipstat.ips_badsum++;
        goto bad_free_m;
    }

    iplen = RT_N2H_U16(ip->ip_len);
    if (iplen < hlen)
    {
        ipstat.ips_badlen++;
        goto bad_free_m;
    }

    /*
     * Check that the amount of data in the buffers
     * is as at least much as the IP header would have us expect.
     * Trim mbufs if longer than we expect.
     * Drop packet if shorter than we expect.
     */
    if (mlen < iplen)
    {
        ipstat.ips_tooshort++;
        goto bad_free_m;
    }

    /* Should drop packet if mbuf too long? hmmm... */
    if (mlen > iplen)
    {
        m_adj(m, iplen - mlen);
        mlen = m->m_len;
    }

    /* source must be unicast */
    if ((ip->ip_src.s_addr & RT_N2H_U32_C(0xe0000000)) == RT_N2H_U32_C(0xe0000000))
        goto free_m;

    /*
     * Drop multicast (class d) and reserved (class e) here.  The rest
     * of the code is not yet prepared to deal with it.  IGMP is not
     * implemented either.
     */
    if (   (ip->ip_dst.s_addr & RT_N2H_U32_C(0xe0000000)) == RT_N2H_U32_C(0xe0000000)
        && ip->ip_dst.s_addr != 0xffffffff)
    {
        goto free_m;
    }


    /* do we need to "forward" this packet? */
    if (!CTL_CHECK_MINE(ip->ip_dst.s_addr))
    {
        if (ip->ip_ttl <= 1)
        {
            /* icmp_error expects these in host order */
            NTOHS(ip->ip_len);
            NTOHS(ip->ip_id);
            NTOHS(ip->ip_off);

            icmp_error(pData, m, ICMP_TIMXCEED, ICMP_TIMXCEED_INTRANS, 0, "ttl");
            goto no_free_m;
        }

        /* ignore packets to other nodes from our private network */
        if (   CTL_CHECK_NETWORK(ip->ip_dst.s_addr)
            && !CTL_CHECK_BROADCAST(ip->ip_dst.s_addr))
        {
            /* XXX: send ICMP_REDIRECT_HOST to be pedantic? */
            goto free_m;
        }

        ip->ip_ttl--;
        if (ip->ip_sum > RT_H2N_U16_C(0xffffU - (1 << 8)))
            ip->ip_sum += RT_H2N_U16_C(1 << 8) + 1;
        else
            ip->ip_sum += RT_H2N_U16_C(1 << 8);
    }

    /* run it through libalias */
    {
        int rc;
        if (!(m->m_flags & M_SKIP_FIREWALL))
        {
            STAM_PROFILE_START(&pData->StatALIAS_input, b);
            rc = LibAliasIn(pData->proxy_alias, mtod(m, char *), mlen);
            STAM_PROFILE_STOP(&pData->StatALIAS_input, b);
            Log2(("NAT: LibAlias return %d\n", rc));
        }
        else
            m->m_flags &= ~M_SKIP_FIREWALL;

#if 0 /* disabled: no module we use does it in this direction */
        /*
         * XXX: spooky action at a distance - libalias may modify the
         * packet and will update ip_len to reflect the new length.
         */
        if (iplen != RT_N2H_U16(ip->ip_len))
        {
            iplen = RT_N2H_U16(ip->ip_len);
            m->m_len = iplen;
            mlen = m->m_len;
        }
#endif
    }

    /*
     * Convert fields to host representation.
     */
    NTOHS(ip->ip_len);
    NTOHS(ip->ip_id);
    NTOHS(ip->ip_off);

    /*
     * If offset or IP_MF are set, must reassemble.
     * Otherwise, nothing need be done.
     * (We could look in the reassembly queue to see
     * if the packet was previously fragmented,
     * but it's not worth the time; just let them time out.)
     *
     */
    if (ip->ip_off & (IP_MF | IP_OFFMASK))
    {
        m = ip_reass(pData, m);
        if (m == NULL)
            goto no_free_m;
        ip = mtod(m, struct ip *);
        hlen = ip->ip_hl << 2;
    }
    else
        ip->ip_len -= hlen;

    /*
     * Switch out to protocol's input routine.
     */
    ipstat.ips_delivered++;
    switch (ip->ip_p)
    {
        case IPPROTO_TCP:
            tcp_input(pData, m, hlen, (struct socket *)NULL);
            break;
        case IPPROTO_UDP:
            udp_input(pData, m, hlen);
            break;
        case IPPROTO_ICMP:
            icmp_input(pData, m, hlen);
            break;
        default:
            ipstat.ips_noproto++;
            m_freem(pData, m);
    }
    goto no_free_m;

bad_free_m:
    Log2(("NAT: IP datagram to %RTnaipv4 with size(%d) claimed as bad\n",
        ip->ip_dst, ip->ip_len));
free_m:
    m_freem(pData, m);
no_free_m:
    STAM_PROFILE_STOP(&pData->StatIP_input, a);
    LogFlowFuncLeave();
    return;
}

struct mbuf *
ip_reass(PNATState pData, struct mbuf* m)
{
    struct ip *ip;
    struct mbuf *p, *q, *nq;
    struct ipq_t *fp = NULL;
    struct ipqhead *head;
    int i, hlen, next;
    u_short hash;

    /* If maxnipq or maxfragsperpacket are 0, never accept fragments. */
    LogFlowFunc(("ENTER: m:%p\n", m));
    if (   maxnipq == 0
        || maxfragsperpacket == 0)
    {
        ipstat.ips_fragments++;
        ipstat.ips_fragdropped++;
        m_freem(pData, m);
        LogFlowFunc(("LEAVE: NULL\n"));
        return (NULL);
    }

    ip = mtod(m, struct ip *);
    hlen = ip->ip_hl << 2;

    hash = IPREASS_HASH(ip->ip_src.s_addr, ip->ip_id);
    head = &ipq[hash];

    /*
     * Look for queue of fragments
     * of this datagram.
     */
    TAILQ_FOREACH(fp, head, ipq_list)
        if (ip->ip_id == fp->ipq_id &&
            ip->ip_src.s_addr == fp->ipq_src.s_addr &&
            ip->ip_dst.s_addr == fp->ipq_dst.s_addr &&
            ip->ip_p == fp->ipq_p)
            goto found;

    fp = NULL;

    /*
     * Attempt to trim the number of allocated fragment queues if it
     * exceeds the administrative limit.
     */
    if ((nipq > maxnipq) && (maxnipq > 0))
    {
        /*
         * drop something from the tail of the current queue
         * before proceeding further
         */
        struct ipq_t *pHead = TAILQ_LAST(head, ipqhead);
        if (pHead == NULL)
        {
            /* gak */
            for (i = 0; i < IPREASS_NHASH; i++)
            {
                struct ipq_t *pTail = TAILQ_LAST(&ipq[i], ipqhead);
                if (pTail)
                {
                    ipstat.ips_fragtimeout += pTail->ipq_nfrags;
                    ip_freef(pData, &ipq[i], pTail);
                    break;
                }
            }
        }
        else
        {
            ipstat.ips_fragtimeout += pHead->ipq_nfrags;
            ip_freef(pData, head, pHead);
        }
    }

found:
    /*
     * Adjust ip_len to not reflect header,
     * convert offset of this to bytes.
     */
    ip->ip_len -= hlen;
    if (ip->ip_off & IP_MF)
    {
        /*
         * Make sure that fragments have a data length
         * that's a non-zero multiple of 8 bytes.
         */
        if (ip->ip_len == 0 || (ip->ip_len & 0x7) != 0)
        {
            ipstat.ips_toosmall++; /* XXX */
            goto dropfrag;
        }
        m->m_flags |= M_FRAG;
    }
    else
        m->m_flags &= ~M_FRAG;
    ip->ip_off <<= 3;


    /*
     * Attempt reassembly; if it succeeds, proceed.
     * ip_reass() will return a different mbuf.
     */
    ipstat.ips_fragments++;

    /* Previous ip_reass() started here. */
    /*
     * Presence of header sizes in mbufs
     * would confuse code below.
     */
    m->m_data += hlen;
    m->m_len -= hlen;

    /*
     * If first fragment to arrive, create a reassembly queue.
     */
    if (fp == NULL)
    {
        fp = RTMemAlloc(sizeof(struct ipq_t));
        if (fp == NULL)
            goto dropfrag;
        TAILQ_INSERT_HEAD(head, fp, ipq_list);
        nipq++;
        fp->ipq_nfrags = 1;
        fp->ipq_ttl = IPFRAGTTL;
        fp->ipq_p = ip->ip_p;
        fp->ipq_id = ip->ip_id;
        fp->ipq_src = ip->ip_src;
        fp->ipq_dst = ip->ip_dst;
        fp->ipq_frags = m;
        m->m_nextpkt = NULL;
        goto done;
    }
    else
    {
        fp->ipq_nfrags++;
    }

#define GETIP(m)    ((struct ip*)((m)->m_pkthdr.header))

    /*
     * Find a segment which begins after this one does.
     */
    for (p = NULL, q = fp->ipq_frags; q; p = q, q = q->m_nextpkt)
        if (GETIP(q)->ip_off > ip->ip_off)
            break;

    /*
     * If there is a preceding segment, it may provide some of
     * our data already.  If so, drop the data from the incoming
     * segment.  If it provides all of our data, drop us, otherwise
     * stick new segment in the proper place.
     *
     * If some of the data is dropped from the preceding
     * segment, then it's checksum is invalidated.
     */
    if (p)
    {
        i = GETIP(p)->ip_off + GETIP(p)->ip_len - ip->ip_off;
        if (i > 0)
        {
            if (i >= ip->ip_len)
                goto dropfrag;
            m_adj(m, i);
            ip->ip_off += i;
            ip->ip_len -= i;
        }
        m->m_nextpkt = p->m_nextpkt;
        p->m_nextpkt = m;
    }
    else
    {
        m->m_nextpkt = fp->ipq_frags;
        fp->ipq_frags = m;
    }

    /*
     * While we overlap succeeding segments trim them or,
     * if they are completely covered, dequeue them.
     */
    for (; q != NULL && ip->ip_off + ip->ip_len > GETIP(q)->ip_off;
         q = nq)
    {
        i = (ip->ip_off + ip->ip_len) - GETIP(q)->ip_off;
        if (i < GETIP(q)->ip_len)
        {
            GETIP(q)->ip_len -= i;
            GETIP(q)->ip_off += i;
            m_adj(q, i);
            break;
        }
        nq = q->m_nextpkt;
        m->m_nextpkt = nq;
        ipstat.ips_fragdropped++;
        fp->ipq_nfrags--;
        m_freem(pData, q);
    }

    /*
     * Check for complete reassembly and perform frag per packet
     * limiting.
     *
     * Frag limiting is performed here so that the nth frag has
     * a chance to complete the packet before we drop the packet.
     * As a result, n+1 frags are actually allowed per packet, but
     * only n will ever be stored. (n = maxfragsperpacket.)
     *
     */
    next = 0;
    for (p = NULL, q = fp->ipq_frags; q; p = q, q = q->m_nextpkt)
    {
        if (GETIP(q)->ip_off != next)
        {
            if (fp->ipq_nfrags > maxfragsperpacket)
            {
                ipstat.ips_fragdropped += fp->ipq_nfrags;
                ip_freef(pData, head, fp);
            }
            goto done;
        }
        next += GETIP(q)->ip_len;
    }
    /* Make sure the last packet didn't have the IP_MF flag */
    if (p->m_flags & M_FRAG)
    {
        if (fp->ipq_nfrags > maxfragsperpacket)
        {
            ipstat.ips_fragdropped += fp->ipq_nfrags;
            ip_freef(pData, head, fp);
        }
        goto done;
    }

    /*
     * Reassembly is complete.  Make sure the packet is a sane size.
     */
    q = fp->ipq_frags;
    ip = GETIP(q);
    hlen = ip->ip_hl << 2;
    if (next + hlen > IP_MAXPACKET)
    {
        ipstat.ips_fragdropped += fp->ipq_nfrags;
        ip_freef(pData, head, fp);
        goto done;
    }

    /*
     * Concatenate fragments.
     */
    m = q;
    nq = q->m_nextpkt;
    q->m_nextpkt = NULL;
    for (q = nq; q != NULL; q = nq)
    {
        nq = q->m_nextpkt;
        q->m_nextpkt = NULL;
        m_cat(pData, m, q);

        m->m_len += hlen;
        m->m_data -= hlen;
        ip = mtod(m, struct ip *); /*update ip pointer */
        hlen = ip->ip_hl << 2;
        m->m_len -= hlen;
        m->m_data += hlen;
    }
    m->m_len += hlen;
    m->m_data -= hlen;

    /*
     * Create header for new ip packet by modifying header of first
     * packet;  dequeue and discard fragment reassembly header.
     * Make header visible.
     */

    ip->ip_len = next;
    ip->ip_src = fp->ipq_src;
    ip->ip_dst = fp->ipq_dst;
    TAILQ_REMOVE(head, fp, ipq_list);
    nipq--;
    RTMemFree(fp);

    Assert((ip->ip_len == next));
    /* some debugging cruft by sklower, below, will go away soon */
#if 0
    if (m->m_flags & M_PKTHDR)    /* XXX this should be done elsewhere */
        m_fixhdr(m);
#endif
    ipstat.ips_reassembled++;
    LogFlowFunc(("LEAVE: %p\n", m));
    return (m);

dropfrag:
    ipstat.ips_fragdropped++;
    if (fp != NULL)
        fp->ipq_nfrags--;
    m_freem(pData, m);

done:
    LogFlowFunc(("LEAVE: NULL\n"));
    return NULL;

#undef GETIP
}

void
ip_freef(PNATState pData, struct ipqhead *fhp, struct ipq_t *fp)
{
    struct mbuf *q;

    while (fp->ipq_frags)
    {
        q = fp->ipq_frags;
        fp->ipq_frags = q->m_nextpkt;
        m_freem(pData, q);
    }
    TAILQ_REMOVE(fhp, fp, ipq_list);
    RTMemFree(fp);
    nipq--;
}

/*
 * IP timer processing;
 * if a timer expires on a reassembly
 * queue, discard it.
 */
void
ip_slowtimo(PNATState pData)
{
    register struct ipq_t *fp;

    /* XXX: the fragment expiration is the same but requier
     * additional loop see (see ip_input.c in FreeBSD tree)
     */
    int i;
    LogFlow(("ip_slowtimo:\n"));
    for (i = 0; i < IPREASS_NHASH; i++)
    {
        for(fp = TAILQ_FIRST(&ipq[i]); fp;)
        {
            struct ipq_t *fpp;

            fpp = fp;
            fp = TAILQ_NEXT(fp, ipq_list);
            if(--fpp->ipq_ttl == 0)
            {
                ipstat.ips_fragtimeout += fpp->ipq_nfrags;
                ip_freef(pData, &ipq[i], fpp);
            }
        }
    }
    /*
     * If we are over the maximum number of fragments
     * (due to the limit being lowered), drain off
     * enough to get down to the new limit.
     */
    if (maxnipq >= 0 && nipq > maxnipq)
    {
        for (i = 0; i < IPREASS_NHASH; i++)
        {
            while (nipq > maxnipq && !TAILQ_EMPTY(&ipq[i]))
            {
                ipstat.ips_fragdropped += TAILQ_FIRST(&ipq[i])->ipq_nfrags;
                ip_freef(pData, &ipq[i], TAILQ_FIRST(&ipq[i]));
            }
        }
    }
}


/*
 * Strip out IP options, at higher
 * level protocol in the kernel.
 * Second argument is buffer to which options
 * will be moved, and return value is their length.
 * (XXX) should be deleted; last arg currently ignored.
 */
void
ip_stripoptions(struct mbuf *m, struct mbuf *mopt)
{
    register int i;
    struct ip *ip = mtod(m, struct ip *);
    register caddr_t opts;
    int olen;
    NOREF(mopt); /** @todo do we really will need this options buffer? */

    olen = (ip->ip_hl<<2) - sizeof(struct ip);
    opts = (caddr_t)(ip + 1);
    i = m->m_len - (sizeof(struct ip) + olen);
    memcpy(opts, opts  + olen, (unsigned)i);
    m->m_len -= olen;

    ip->ip_hl = sizeof(struct ip) >> 2;
}
