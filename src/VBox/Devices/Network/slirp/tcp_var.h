/* $Id: tcp_var.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - TCP (declarations).
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
 * Copyright (c) 1982, 1986, 1993, 1994
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
 *      @(#)tcp_var.h   8.3 (Berkeley) 4/10/94
 * tcp_var.h,v 1.3 1994/08/21 05:27:39 paul Exp
 */

#ifndef _TCP_VAR_H_
#define _TCP_VAR_H_

#include "queue.h"
#include "tcpip.h"
#include "tcp_timer.h"

/* TCP segment queue entry */
struct tseg_qent
{
    LIST_ENTRY(tseg_qent) tqe_q;
    int             tqe_len;        /* TCP segment data length */
    struct tcphdr   *tqe_th;        /* a pointer to tcp header */
    struct  mbuf    *tqe_m;         /* mbuf contains packet */
};
LIST_HEAD(tsegqe_head, tseg_qent);

/*
 * Tcp control block, one per tcp; fields:
 */
struct tcpcb
{
    LIST_ENTRY(tcpcb) t_list;
    struct tsegqe_head t_segq;       /* segment reassembly queue */
    int       t_segqlen;             /* segment reassembly queue length */
    int16_t   t_state;               /* state of this connection */
    int16_t   t_timer[TCPT_NTIMERS]; /* tcp timers */
    int16_t   t_rxtshift;            /* log(2) of rexmt exp. backoff */
    int16_t   t_rxtcur;              /* current retransmit value */
    int16_t   t_dupacks;             /* consecutive dup acks recd */
    uint16_t  t_maxseg;              /* maximum segment size */
    char      t_force;               /* 1 if forcing out a byte */
    uint16_t  t_flags;
#define TF_ACKNOW       0x0001       /* ack peer immediately */
#define TF_DELACK       0x0002       /* ack, but try to delay it */
#define TF_NODELAY      0x0004       /* don't delay packets to coalesce */
#define TF_NOOPT        0x0008       /* don't use tcp options */
#define TF_SENTFIN      0x0010       /* have sent FIN */
#define TF_REQ_SCALE    0x0020       /* have/will request window scaling */
#define TF_RCVD_SCALE   0x0040       /* other side has requested scaling */
#define TF_REQ_TSTMP    0x0080       /* have/will request timestamps */
#define TF_RCVD_TSTMP   0x0100       /* a timestamp was received in SYN */
#define TF_SACK_PERMIT  0x0200       /* other side said I could SACK */

    /* Make it static  for now */
/*  struct  tcpiphdr *t_template;   / * skeletal packet for transmit */
    struct tcpiphdr t_template;

    struct socket *t_socket;               /* back pointer to socket */
/*
 * The following fields are used as in the protocol specification.
 * See RFC783, Dec. 1981, page 21.
 */
    /* send sequence variables */
    tcp_seq   snd_una;               /* send unacknowledged */
    tcp_seq   snd_nxt;               /* send next */
    tcp_seq   snd_up;                /* send urgent pointer */
    tcp_seq   snd_wl1;               /* window update seg seq number */
    tcp_seq   snd_wl2;               /* window update seg ack number */
    tcp_seq   iss;                   /* initial send sequence number */
    uint32_t  snd_wnd;               /* send window */
    /* receive sequence variables */
    uint32_t  rcv_wnd;               /* receive window */
    tcp_seq   rcv_nxt;               /* receive next */
    tcp_seq   rcv_up;                /* receive urgent pointer */
    tcp_seq   irs;                   /* initial receive sequence number */
/*
 * Additional variables for this implementation.
 */
    /* receive variables */
    tcp_seq   rcv_adv;               /* advertised window */
    /* retransmit variables */
    tcp_seq   snd_max;               /* highest sequence number sent;
                                      * used to recognize retransmits
                                      */
    /* congestion control (for slow start, source quench, retransmit after loss) */
    uint32_t  snd_cwnd;              /* congestion-controlled window */
    uint32_t  snd_ssthresh;          /* snd_cwnd size threshold for
                                      * for slow start exponential to
                                      * linear switch
                                      */
/*
 * transmit timing stuff.  See below for scale of srtt and rttvar.
 * "Variance" is actually smoothed difference.
 */
    int16_t   t_idle;                /* inactivity time */
    int16_t   t_rtt;                 /* round trip time */
    tcp_seq   t_rtseq;               /* sequence number being timed */
    int16_t   t_srtt;                /* smoothed round-trip time */
    int16_t   t_rttvar;              /* variance in round-trip time */
    uint16_t  t_rttmin;              /* minimum rtt allowed */
    uint32_t  max_sndwnd;            /* largest window peer has offered */

/* out-of-band data */
    char      t_oobflags;            /* have some */
    char      t_iobc;                /* input character */
#define TCPOOB_HAVEDATA 0x01
#define TCPOOB_HADDATA  0x02
    short     t_softerror;           /* possible error not yet reported */

/* RFC 1323 variables */
    uint8_t   snd_scale;             /* window scaling for send window */
    uint8_t   rcv_scale;             /* window scaling for recv window */
    uint8_t   request_r_scale;       /* pending window scaling */
    uint8_t   requested_s_scale;
    uint32_t  ts_recent;             /* timestamp echo data */
    uint32_t  ts_recent_age;         /* when last updated */
    tcp_seq   last_ack_sent;
};

LIST_HEAD(tcpcbhead, tcpcb);

#define sototcpcb(so)   ((so)->so_tcpcb)

/*
 * The smoothed round-trip time and estimated variance
 * are stored as fixed point numbers scaled by the values below.
 * For convenience, these scales are also used in smoothing the average
 * (smoothed = (1/scale)sample + ((scale-1)/scale)smoothed).
 * With these scales, srtt has 3 bits to the right of the binary point,
 * and thus an "ALPHA" of 0.875.  rttvar has 2 bits to the right of the
 * binary point, and is smoothed with an ALPHA of 0.75.
 */
#define TCP_RTT_SCALE           8       /* multiplier for srtt; 3 bits frac. */
#define TCP_RTT_SHIFT           3       /* shift for srtt; 3 bits frac. */
#define TCP_RTTVAR_SCALE        4       /* multiplier for rttvar; 2 bits */
#define TCP_RTTVAR_SHIFT        2       /* multiplier for rttvar; 2 bits */

/*
 * The initial retransmission should happen at rtt + 4 * rttvar.
 * Because of the way we do the smoothing, srtt and rttvar
 * will each average +1/2 tick of bias.  When we compute
 * the retransmit timer, we want 1/2 tick of rounding and
 * 1 extra tick because of +-1/2 tick uncertainty in the
 * firing of the timer.  The bias will give us exactly the
 * 1.5 tick we need.  But, because the bias is
 * statistical, we have to test that we don't drop below
 * the minimum feasible timer (which is 2 ticks).
 * This macro assumes that the value of TCP_RTTVAR_SCALE
 * is the same as the multiplier for rttvar.
 */
#define TCP_REXMTVAL(tp) \
        (((tp)->t_srtt >> TCP_RTT_SHIFT) + (tp)->t_rttvar)

/*
 * TCP statistics.
 * Many of these should be kept per connection,
 * but that's inconvenient at the moment.
 */
struct tcpstat_t
{
    u_long  tcps_connattempt;       /* connections initiated */
    u_long  tcps_accepts;           /* connections accepted */
    u_long  tcps_connects;          /* connections established */
    u_long  tcps_drops;             /* connections dropped */
    u_long  tcps_conndrops;         /* embryonic connections dropped */
    u_long  tcps_closed;            /* conn. closed (includes drops) */
    u_long  tcps_segstimed;         /* segs where we tried to get rtt */
    u_long  tcps_rttupdated;        /* times we succeeded */
    u_long  tcps_delack;            /* delayed acks sent */
    u_long  tcps_timeoutdrop;       /* conn. dropped in rxmt timeout */
    u_long  tcps_rexmttimeo;        /* retransmit timeouts */
    u_long  tcps_persisttimeo;      /* persist timeouts */
    u_long  tcps_keeptimeo;         /* keepalive timeouts */
    u_long  tcps_keepprobe;         /* keepalive probes sent */
    u_long  tcps_keepdrops;         /* connections dropped in keepalive */

    u_long  tcps_sndtotal;          /* total packets sent */
    u_long  tcps_sndpack;           /* data packets sent */
    u_long  tcps_sndbyte;           /* data bytes sent */
    u_long  tcps_sndrexmitpack;     /* data packets retransmitted */
    u_long  tcps_sndrexmitbyte;     /* data bytes retransmitted */
    u_long  tcps_sndacks;           /* ack-only packets sent */
    u_long  tcps_sndprobe;          /* window probes sent */
    u_long  tcps_sndurg;            /* packets sent with URG only */
    u_long  tcps_sndwinup;          /* window update-only packets sent */
    u_long  tcps_sndctrl;           /* control (SYN|FIN|RST) packets sent */

    u_long  tcps_rcvtotal;          /* total packets received */
    u_long  tcps_rcvpack;           /* packets received in sequence */
    u_long  tcps_rcvbyte;           /* bytes received in sequence */
    u_long  tcps_rcvbadsum;         /* packets received with ccksum errs */
    u_long  tcps_rcvbadoff;         /* packets received with bad offset */
/*  u_long  tcps_rcvshort;  */      /* packets received too short */
    u_long  tcps_rcvduppack;        /* duplicate-only packets received */
    u_long  tcps_rcvdupbyte;        /* duplicate-only bytes received */
    u_long  tcps_rcvpartduppack;    /* packets with some duplicate data */
    u_long  tcps_rcvpartdupbyte;    /* dup. bytes in part-dup. packets */
    u_long  tcps_rcvoopack;         /* out-of-order packets received */
    u_long  tcps_rcvoobyte;         /* out-of-order bytes received */
    u_long  tcps_rcvpackafterwin;   /* packets with data after window */
    u_long  tcps_rcvbyteafterwin;   /* bytes rcvd after window */
    u_long  tcps_rcvafterclose;     /* packets rcvd after "close" */
    u_long  tcps_rcvwinprobe;       /* rcvd window probe packets */
    u_long  tcps_rcvdupack;         /* rcvd duplicate acks */
    u_long  tcps_rcvacktoomuch;     /* rcvd acks for unsent data */
    u_long  tcps_rcvackpack;        /* rcvd ack packets */
    u_long  tcps_rcvackbyte;        /* bytes acked by rcvd acks */
    u_long  tcps_rcvwinupd;         /* rcvd window update packets */
/*  u_long  tcps_pawsdrop;  */      /* segments dropped due to PAWS */
    u_long  tcps_predack;           /* times hdr predict ok for acks */
    u_long  tcps_preddat;           /* times hdr predict ok for data pkts */
    u_long  tcps_socachemiss;       /* tcp_last_so misses */
    u_long  tcps_didnuttin;         /* Times tcp_output didn't do anything XXX */
    u_long  tcps_rcvmemdrop;
};

#endif
