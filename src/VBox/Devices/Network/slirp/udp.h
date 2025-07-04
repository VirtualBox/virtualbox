/* $Id: udp.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * NAT - UDP protocol (declarations/defines).
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
 *      @(#)udp.h       8.1 (Berkeley) 6/10/93
 * udp.h,v 1.3 1994/08/21 05:27:41 paul Exp
 */

#ifndef _UDP_H_
#define _UDP_H_

#define UDP_TTL 0x60
#define UDP_UDPDATALEN 16192

extern struct socket *udp_last_so;

/*
 * Udp protocol header.
 * Per RFC 768, September, 1981.
 */
struct udphdr
{
    uint16_t   uh_sport;               /* source port */
    uint16_t   uh_dport;               /* destination port */
    int16_t    uh_ulen;                /* udp length */
    uint16_t   uh_sum;                 /* udp checksum */
};
AssertCompileSize(struct udphdr, 8);

/*
 * UDP kernel structures and variables.
 */
struct udpiphdr
{
    struct ipovly ui_i;            /* overlaid ip structure */
    struct udphdr ui_u;            /* udp header */
};
AssertCompileSize(struct udpiphdr, 28);
#define ui_next         ui_i.ih_next
#define ui_prev         ui_i.ih_prev
#define ui_x1           ui_i.ih_x1
#define ui_pr           ui_i.ih_pr
#define ui_len          ui_i.ih_len
#define ui_src          ui_i.ih_src
#define ui_dst          ui_i.ih_dst
#define ui_sport        ui_u.uh_sport
#define ui_dport        ui_u.uh_dport
#define ui_ulen         ui_u.uh_ulen
#define ui_sum          ui_u.uh_sum

struct udpstat_t
{
                                    /* input statistics: */
    u_long  udps_ipackets;          /* total input packets */
    u_long  udps_hdrops;            /* packet shorter than header */
    u_long  udps_badsum;            /* checksum error */
    u_long  udps_badlen;            /* data length larger than packet */
    u_long  udps_noport;            /* no socket on port */
    u_long  udps_noportbcast;       /* of above, arrived as broadcast */
    u_long  udps_fullsock;          /* not delivered, input socket full */
    u_long  udpps_pcbcachemiss;     /* input packets missing pcb cache */
                                    /* output statistics: */
    u_long  udps_opackets;          /* total output packets */
};

/*
 * Names for UDP sysctl objects
 */
#define UDPCTL_CHECKSUM         1       /* checksum UDP packets */
#define UDPCTL_MAXID            2

extern struct udpstat udpstat;
extern struct socket udb;
struct mbuf;

void udp_init (PNATState);
void udp_input (PNATState, register struct mbuf *, int);
int udp_output (PNATState, struct socket *, struct mbuf *, struct sockaddr_in *);
int udp_attach (PNATState, struct socket *);
void udp_detach (PNATState, struct socket *);
u_int8_t udp_tos (struct socket *);
void udp_emu (PNATState, struct socket *, struct mbuf *);
struct socket * udp_listen (PNATState, u_int32_t, u_int, u_int32_t, u_int, int);
int udp_output2(PNATState pData, struct socket *so, struct mbuf *m,
                struct sockaddr_in *saddr, struct sockaddr_in *daddr,
                int iptos);

#endif
