/* $Id: ip_icmp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - IP/ICMP handling (declarations/defines).
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
 * Copyright (c) 1982, 1986, 1993
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
 *      @(#)ip_icmp.h   8.1 (Berkeley) 6/10/93
 * ip_icmp.h,v 1.4 1995/05/30 08:09:43 rgrimes Exp
 */

#ifndef _NETINET_IP_ICMP_H_
#define _NETINET_IP_ICMP_H_
#include <queue.h>

/*
 * Interface Control Message Protocol Definitions.
 * Per RFC 792, September 1981.
 */

typedef u_int32_t n_time;

/*
 * Structure of an icmp header.
 */
struct icmp
{
    uint8_t     icmp_type;              /* type of message, see below */
    uint8_t     icmp_code;              /* type sub code */
    uint16_t    icmp_cksum;             /* ones complement cksum of struct */
    union
    {
        uint8_t ih_pptr;                 /* ICMP_PARAMPROB */
        struct in_addr ih_gwaddr;       /* ICMP_REDIRECT */
        struct ih_idseq
        {
            uint16_t  icd_id;
            uint16_t  icd_seq;
        } ih_idseq;
        int ih_void;

        /* ICMP_UNREACH_NEEDFRAG -- Path MTU Discovery (RFC1191) */
        struct ih_pmtu
        {
            uint16_t ipm_void;
            uint16_t ipm_nextmtu;
        } ih_pmtu;
    } icmp_hun;
#define icmp_pptr       icmp_hun.ih_pptr
#define icmp_gwaddr     icmp_hun.ih_gwaddr
#define icmp_id         icmp_hun.ih_idseq.icd_id
#define icmp_seq        icmp_hun.ih_idseq.icd_seq
#define icmp_void       icmp_hun.ih_void
#define icmp_pmvoid     icmp_hun.ih_pmtu.ipm_void
#define icmp_nextmtu    icmp_hun.ih_pmtu.ipm_nextmtu
    union
    {
        struct id_ts
        {
            n_time its_otime;
            n_time its_rtime;
            n_time its_ttime;
        } id_ts;
        struct id_ip
        {
            struct ip idi_ip;
            /* options and then 64 bits of data */
        } id_ip;
        uint32_t        id_mask;
        char            id_data[1];
    } icmp_dun;
#define icmp_otime      icmp_dun.id_ts.its_otime
#define icmp_rtime      icmp_dun.id_ts.its_rtime
#define icmp_ttime      icmp_dun.id_ts.its_ttime
#define icmp_ip         icmp_dun.id_ip.idi_ip
#define icmp_mask       icmp_dun.id_mask
#define icmp_data       icmp_dun.id_data
};
AssertCompileSize(struct icmp, 28);

/*
 * Lower bounds on packet lengths for various types.
 * For the error advice packets must first insure that the
 * packet is large enought to contain the returned ip header.
 * Only then can we do the check to see if 64 bits of packet
 * data have been returned, since we need to check the returned
 * ip header length.
 */
#define ICMP_MINLEN     8                               /* abs minimum */
#define ICMP_TSLEN      (8 + 3 * sizeof (n_time))       /* timestamp */
#define ICMP_MASKLEN    12                              /* address mask */
#define ICMP_ADVLENMIN  (8 + sizeof (struct ip) + 8)    /* min */
#define ICMP_ADVLEN(p)  (8 + ((p)->icmp_ip.ip_hl << 2) + 8)
        /* N.B.: must separately check that ip_hl >= 5 */

/*
 * Definition of type and code field values.
 */
#define ICMP_ECHOREPLY          0               /* echo reply */
#define ICMP_UNREACH            3               /* dest unreachable, codes: */
#define         ICMP_UNREACH_NET        0               /* bad net */
#define         ICMP_UNREACH_HOST       1               /* bad host */
#define         ICMP_UNREACH_PROTOCOL   2               /* bad protocol */
#define         ICMP_UNREACH_PORT       3               /* bad port */
#define         ICMP_UNREACH_NEEDFRAG   4               /* IP_DF caused drop */
#define         ICMP_UNREACH_SRCFAIL    5               /* src route failed */
#define         ICMP_UNREACH_NET_UNKNOWN 6              /* unknown net */
#define         ICMP_UNREACH_HOST_UNKNOWN 7             /* unknown host */
#define         ICMP_UNREACH_ISOLATED   8               /* src host isolated */
#define         ICMP_UNREACH_NET_PROHIB 9               /* prohibited access */
#define         ICMP_UNREACH_HOST_PROHIB 10             /* ditto */
#define         ICMP_UNREACH_TOSNET     11              /* bad tos for net */
#define         ICMP_UNREACH_TOSHOST    12              /* bad tos for host */
#define ICMP_SOURCEQUENCH       4               /* packet lost, slow down */
#define ICMP_REDIRECT           5               /* shorter route, codes: */
#define         ICMP_REDIRECT_NET       0               /* for network */
#define         ICMP_REDIRECT_HOST      1               /* for host */
#define         ICMP_REDIRECT_TOSNET    2               /* for tos and net */
#define         ICMP_REDIRECT_TOSHOST   3               /* for tos and host */
#define ICMP_ECHO               8               /* echo service */
#define ICMP_ROUTERADVERT       9               /* router advertisement */
#define ICMP_ROUTERSOLICIT      10              /* router solicitation */
#define ICMP_TIMXCEED           11              /* time exceeded, code: */
#define         ICMP_TIMXCEED_INTRANS   0               /* ttl==0 in transit */
#define         ICMP_TIMXCEED_REASS     1               /* ttl==0 in reass */
#define ICMP_PARAMPROB          12              /* ip header bad */
#define         ICMP_PARAMPROB_OPTABSENT 1              /* req. opt. absent */
#define ICMP_TSTAMP             13              /* timestamp request */
#define ICMP_TSTAMPREPLY        14              /* timestamp reply */
#define ICMP_IREQ               15              /* information request */
#define ICMP_IREQREPLY          16              /* information reply */
#define ICMP_MASKREQ            17              /* address mask request */
#define ICMP_MASKREPLY          18              /* address mask reply */

#define ICMP_MAXTYPE            18

#define ICMP_INFOTYPE(type) \
        ((type) == ICMP_ECHOREPLY || (type) == ICMP_ECHO || \
        (type) == ICMP_ROUTERADVERT || (type) == ICMP_ROUTERSOLICIT || \
        (type) == ICMP_TSTAMP || (type) == ICMP_TSTAMPREPLY || \
        (type) == ICMP_IREQ || (type) == ICMP_IREQREPLY || \
        (type) == ICMP_MASKREQ || (type) == ICMP_MASKREPLY)

void icmp_input (PNATState, struct mbuf *, int);
void icmp_error (PNATState, struct mbuf *, u_char, u_char, int, const char *);
void icmp_reflect (PNATState, struct mbuf *);

struct icmp_msg
{
    TAILQ_ENTRY(icmp_msg) im_queue;
    struct mbuf *im_m;
    struct socket *im_so;
};

TAILQ_HEAD(icmp_storage, icmp_msg);

int icmp_init (PNATState , int);
void icmp_finit (PNATState );
struct icmp_msg * icmp_find_original_mbuf (PNATState , struct ip *);
void icmp_msg_delete(PNATState, struct icmp_msg *);

#ifdef RT_OS_WINDOWS
/* Windows ICMP API code in ip_icmpwin.c */
int icmpwin_init (PNATState);
void icmpwin_finit (PNATState);
void icmpwin_ping(PNATState, struct mbuf *, int);
void icmpwin_process(PNATState);
#endif

#endif
