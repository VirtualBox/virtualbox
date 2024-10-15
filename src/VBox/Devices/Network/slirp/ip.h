/* $Id: ip.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - IP handling (declarations/defines).
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
 *      @(#)ip.h        8.1 (Berkeley) 6/10/93
 * ip.h,v 1.3 1994/08/21 05:27:30 paul Exp
 */

#ifndef _IP_H_
#define _IP_H_

#include "queue.h"

#ifdef WORDS_BIGENDIAN
# ifndef NTOHL
#  define NTOHL(d)
# endif
# ifndef NTOHS
#  define NTOHS(d)
# endif
# ifndef HTONL
#  define HTONL(d)
# endif
# ifndef HTONS
#  define HTONS(d)
# endif
#else
# ifndef NTOHL
#  define NTOHL(d) ((d) = RT_N2H_U32((d)))
# endif
# ifndef NTOHS
#  define NTOHS(d) ((d) = RT_N2H_U16((u_int16_t)(d)))
# endif
# ifndef HTONL
#  define HTONL(d) ((d) = RT_H2N_U32((d)))
# endif
# ifndef HTONS
#  define HTONS(d) ((d) = RT_H2N_U16((u_int16_t)(d)))
# endif
#endif

/*
 * Definitions for internet protocol version 4.
 * Per RFC 791, September 1981.
 */
#define IPVERSION       4

/*
 * Structure of an internet header, naked of options.
 */
struct ip
{
#ifdef WORDS_BIGENDIAN
# ifdef _MSC_VER
    uint8_t        ip_v:4;     /* version */
    uint8_t        ip_hl:4;    /* header length */
# else
    unsigned       ip_v:4;     /* version */
    unsigned       ip_hl:4;    /* header length */
# endif
#else
# ifdef _MSC_VER
    uint8_t        ip_hl:4;    /* header length */
    uint8_t        ip_v:4;     /* version */
# else
    unsigned       ip_hl:4;    /* header length */
    unsigned       ip_v:4;     /* version */
# endif
#endif
    uint8_t        ip_tos;     /* type of service */
    uint16_t       ip_len;     /* total length */
    uint16_t       ip_id;      /* identification */
    uint16_t       ip_off;     /* fragment offset field */
#define IP_DF       0x4000     /* don't fragment flag */
#define IP_MF       0x2000     /* more fragments flag */
#define IP_OFFMASK  0x1fff     /* mask for fragmenting bits */
    uint8_t        ip_ttl;     /* time to live */
    uint8_t        ip_p;       /* protocol */
    uint16_t       ip_sum;     /* checksum */
    struct in_addr ip_src;     /* source address */
    struct in_addr ip_dst;     /* destination address */
};
AssertCompileSize(struct ip, 20);

#define IP_MAXPACKET    65535  /* maximum packet size */

/*
 * Definitions for IP type of service (ip_tos)
 */
#define IPTOS_LOWDELAY          0x10
#define IPTOS_THROUGHPUT        0x08
#define IPTOS_RELIABILITY       0x04


/*
 * Time stamp option structure.
 */
struct  ip_timestamp
{
    uint8_t        ipt_code;          /* IPOPT_TS */
    uint8_t        ipt_len;           /* size of structure (variable) */
    uint8_t        ipt_ptr;           /* index of current entry */
#ifdef WORDS_BIGENDIAN
# ifdef _MSC_VER
    uint8_t        ipt_oflw:4;        /* overflow counter */
    uint8_t        ipt_flg:4;         /* flags, see below */
# else
    unsigned       ipt_oflw:4;        /* overflow counter */
    unsigned       ipt_flg:4;         /* flags, see below */
# endif
#else
# ifdef _MSC_VER
    uint8_t        ipt_flg:4;         /* flags, see below */
    uint8_t        ipt_oflw:4;        /* overflow counter */
# else
    unsigned       ipt_flg:4;         /* flags, see below */
    unsigned       ipt_oflw:4;        /* overflow counter */
# endif
#endif
    union ipt_timestamp
    {
        uint32_t           ipt_time[1];
        struct ipt_ta
        {
            struct in_addr ipt_addr;
            uint32_t       ipt_time;
        } ipt_ta[1];
    } ipt_timestamp;
};
AssertCompileSize(struct ip_timestamp, 12);

/*
 * Internet implementation parameters.
 */
#define MAXTTL          255           /* maximum time to live (seconds) */
#define IPDEFTTL        64            /* default ttl, from RFC 1340 */
#define IPFRAGTTL       60            /* time to live for frags, slowhz */
#define IPTTLDEC        1             /* subtracted when forwarding */

#define IP_MSS          576           /* default maximum segment size */

#ifdef HAVE_SYS_TYPES32_H  /* Overcome some Solaris 2.x junk */
# include <sys/types32.h>
#else
typedef caddr_t caddr32_t;
#endif

#if SIZEOF_CHAR_P == 4
typedef struct ipq_t *ipqp_32;
typedef struct ipasfrag *ipasfragp_32;
#else
typedef caddr32_t ipqp_32;
typedef caddr32_t ipasfragp_32;
#endif

/*
 * Overlay for ip header used by other protocols (tcp, udp).
 */
struct ipovly
{
    u_int8_t        ih_x1[9];         /* (unused) */
    u_int8_t        ih_pr;            /* protocol */
    u_int16_t       ih_len;           /* protocol length */
    struct  in_addr ih_src;           /* source internet address */
    struct  in_addr ih_dst;           /* destination internet address */
};
AssertCompileSize(struct ipovly, 20);

/*
 * Ip reassembly queue structure.  Each fragment being reassembled is
 * attached to one of these structures. They are timed out after ipq_ttl
 * drops to 0, and may also be reclaimed if memory becomes tight.
 * size 28 bytes
 */
struct ipq_t
{
    TAILQ_ENTRY(ipq_t) ipq_list;
    u_int8_t        ipq_ttl;         /* time for reass q to live */
    u_int8_t        ipq_p;           /* protocol of this fragment */
    u_int16_t       ipq_id;          /* sequence id for reassembly */
    struct mbuf     *ipq_frags;      /* to ip headers of fragments */
    uint8_t         ipq_nfrags;      /* # of fragments in this packet */
    struct in_addr  ipq_src;
    struct in_addr  ipq_dst;
};


/*
* IP datagram reassembly.
*/
#define IPREASS_NHASH_LOG2      6
#define IPREASS_NHASH           (1 << IPREASS_NHASH_LOG2)
#define IPREASS_HMASK           (IPREASS_NHASH - 1)
#define IPREASS_HASH(x,y) \
(((((x) & 0xF) | ((((x) >> 8) & 0xF) << 4)) ^ (y)) & IPREASS_HMASK)
TAILQ_HEAD(ipqhead, ipq_t);

/*
 * Structure attached to inpcb.ip_moptions and
 * passed to ip_output when IP multicast options are in use.
 */

struct  ipstat_t
{
    u_long  ips_total;              /* total packets received */
    u_long  ips_badsum;             /* checksum bad */
    u_long  ips_tooshort;           /* packet too short */
    u_long  ips_toosmall;           /* not enough data */
    u_long  ips_badhlen;            /* ip header length < data size */
    u_long  ips_badlen;             /* ip length < ip header length */
    u_long  ips_fragments;          /* fragments received */
    u_long  ips_fragdropped;        /* frags dropped (dups, out of space) */
    u_long  ips_fragtimeout;        /* fragments timed out */
    u_long  ips_forward;            /* packets forwarded */
    u_long  ips_cantforward;        /* packets rcvd for unreachable dest */
    u_long  ips_redirectsent;       /* packets forwarded on same net */
    u_long  ips_noproto;            /* unknown or unsupported protocol */
    u_long  ips_delivered;          /* datagrams delivered to upper level*/
    u_long  ips_localout;           /* total ip packets generated here */
    u_long  ips_odropped;           /* lost packets due to nobufs, etc. */
    u_long  ips_reassembled;        /* total packets reassembled ok */
    u_long  ips_fragmented;         /* datagrams successfully fragmented */
    u_long  ips_ofragments;         /* output fragments created */
    u_long  ips_cantfrag;           /* don't fragment flag was set, etc. */
    u_long  ips_badoptions;         /* error in option processing */
    u_long  ips_noroute;            /* packets discarded due to no route */
    u_long  ips_badvers;            /* ip version != 4 */
    u_long  ips_rawout;             /* total raw ip packets generated */
    u_long  ips_unaligned;          /* times the ip packet was not aligned */
};

#endif
