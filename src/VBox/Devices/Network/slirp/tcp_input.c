/* $Id: tcp_input.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - TCP input.
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
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1994
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
 *      @(#)tcp_input.c 8.5 (Berkeley) 4/10/94
 * tcp_input.c,v 1.10 1994/10/13 18:36:32 wollman Exp
 */

/*
 * Changes and additions relating to SLiRP
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>
#include "ip_icmp.h"


#if 0 /* code using this macroses is commented out */
# define TCP_PAWS_IDLE   (24 * 24 * 60 * 60 * PR_SLOWHZ)

/* for modulo comparisons of timestamps */
# define TSTMP_LT(a, b)   ((int)((a)-(b)) < 0)
# define TSTMP_GEQ(a, b)  ((int)((a)-(b)) >= 0)
#endif

#ifndef TCP_ACK_HACK
#define DELAY_ACK(tp, ti)                           \
               if (ti->ti_flags & TH_PUSH)          \
                       tp->t_flags |= TF_ACKNOW;    \
               else                                 \
                       tp->t_flags |= TF_DELACK;
#else /* !TCP_ACK_HACK */
#define DELAY_ACK(tp, ign)                          \
                tp->t_flags |= TF_DELACK;
#endif /* TCP_ACK_HACK */


/*
 * deps: netinet/tcp_reass.c
 *          tcp_reass_maxqlen = 48 (deafault)
 *          tcp_reass_maxseg  = nmbclusters/16 (nmbclusters = 1024 + maxusers * 64 from kern/kern_mbuf.c let's say 256)
 */
int
tcp_reass(PNATState pData, struct tcpcb *tp, struct tcphdr *th, int *tlenp, struct mbuf *m)
{
    struct tseg_qent *q;
    struct tseg_qent *p = NULL;
    struct tseg_qent *nq;
    struct tseg_qent *te = NULL;
    struct socket *so = tp->t_socket;
    int flags;
    STAM_PROFILE_START(&pData->StatTCP_reassamble, tcp_reassamble);
    LogFlowFunc(("ENTER: pData:%p, tp:%R[tcpcb793], th:%p, tlenp:%p, m:%p\n", pData, tp, th, tlenp, m));

    /*
     * XXX: tcp_reass() is rather inefficient with its data structures
     * and should be rewritten (see NetBSD for optimizations).  While
     * doing that it should move to its own file tcp_reass.c.
     */

    /*
     * Call with th==NULL after become established to
     * force pre-ESTABLISHED data up to user socket.
     */
    if (th == NULL)
    {
        LogFlowFunc(("%d -> present\n", __LINE__));
        goto present;
    }

    /*
     * Limit the number of segments in the reassembly queue to prevent
     * holding on to too many segments (and thus running out of mbufs).
     * Make sure to let the missing segment through which caused this
     * queue.  Always keep one global queue entry spare to be able to
     * process the missing segment.
     */
    if (   th->th_seq != tp->rcv_nxt
        && (   tcp_reass_qsize + 1 >= tcp_reass_maxseg
            || tp->t_segqlen >= tcp_reass_maxqlen))
    {
        tcp_reass_overflows++;
        tcpstat.tcps_rcvmemdrop++;
        m_freem(pData, m);
        *tlenp = 0;
        STAM_PROFILE_STOP(&pData->StatTCP_reassamble, tcp_reassamble);
        LogFlowFuncLeave();
        return (0);
    }

    /*
     * Allocate a new queue entry. If we can't, or hit the zone limit
     * just drop the pkt.
     */
    te = RTMemAlloc(sizeof(struct tseg_qent));
    if (te == NULL)
    {
        tcpstat.tcps_rcvmemdrop++;
        m_freem(pData, m);
        *tlenp = 0;
        STAM_PROFILE_STOP(&pData->StatTCP_reassamble, tcp_reassamble);
        LogFlowFuncLeave();
        return (0);
    }
    tp->t_segqlen++;
    tcp_reass_qsize++;

    /*
     * Find a segment which begins after this one does.
     */
    LIST_FOREACH(q, &tp->t_segq, tqe_q)
    {
        if (SEQ_GT(q->tqe_th->th_seq, th->th_seq))
            break;
        p = q;
    }

    /*
     * If there is a preceding segment, it may provide some of
     * our data already.  If so, drop the data from the incoming
     * segment.  If it provides all of our data, drop us.
     */
    if (p != NULL)
    {
        int i;
        /* conversion to int (in i) handles seq wraparound */
        i = p->tqe_th->th_seq + p->tqe_len - th->th_seq;
        if (i > 0)
        {
            if (i >= *tlenp)
            {
                tcpstat.tcps_rcvduppack++;
                tcpstat.tcps_rcvdupbyte += *tlenp;
                m_freem(pData, m);
                RTMemFree(te);
                tp->t_segqlen--;
                tcp_reass_qsize--;
                /*
                 * Try to present any queued data
                 * at the left window edge to the user.
                 * This is needed after the 3-WHS
                 * completes.
                 */
                LogFlowFunc(("%d -> present\n", __LINE__));
                goto present;   /* ??? */
            }
            m_adj(m, i);
            *tlenp -= i;
            th->th_seq += i;
        }
    }
    tcpstat.tcps_rcvoopack++;
    tcpstat.tcps_rcvoobyte += *tlenp;

    /*
     * While we overlap succeeding segments trim them or,
     * if they are completely covered, dequeue them.
     */
    while (q)
    {
        int i = (th->th_seq + *tlenp) - q->tqe_th->th_seq;
        if (i <= 0)
            break;
        if (i < q->tqe_len)
        {
            q->tqe_th->th_seq += i;
            q->tqe_len -= i;
            m_adj(q->tqe_m, i);
            break;
        }

        nq = LIST_NEXT(q, tqe_q);
        LIST_REMOVE(q, tqe_q);
        m_freem(pData, q->tqe_m);
        RTMemFree(q);
        tp->t_segqlen--;
        tcp_reass_qsize--;
        q = nq;
    }

    /* Insert the new segment queue entry into place. */
    te->tqe_m = m;
    te->tqe_th = th;
    te->tqe_len = *tlenp;

    if (p == NULL)
    {
        LIST_INSERT_HEAD(&tp->t_segq, te, tqe_q);
    }
    else
    {
        LIST_INSERT_AFTER(p, te, tqe_q);
    }

present:
    /*
     * Present data to user, advancing rcv_nxt through
     * completed sequence space.
     */
    if (!TCPS_HAVEESTABLISHED(tp->t_state))
    {
        STAM_PROFILE_STOP(&pData->StatTCP_reassamble, tcp_reassamble);
        return (0);
    }
    q = LIST_FIRST(&tp->t_segq);
    if (!q || q->tqe_th->th_seq != tp->rcv_nxt)
    {
        STAM_PROFILE_STOP(&pData->StatTCP_reassamble, tcp_reassamble);
        return (0);
    }
    do
    {
        tp->rcv_nxt += q->tqe_len;
        flags = q->tqe_th->th_flags & TH_FIN;
        nq = LIST_NEXT(q, tqe_q);
        LIST_REMOVE(q, tqe_q);
        /* XXX: This place should be checked for the same code in
         * original BSD code for Slirp and current BSD used SS_FCANTRCVMORE
         */
        if (so->so_state & SS_FCANTSENDMORE)
            m_freem(pData, q->tqe_m);
        else
            sbappend(pData, so, q->tqe_m);
        RTMemFree(q);
        tp->t_segqlen--;
        tcp_reass_qsize--;
        q = nq;
    }
    while (q && q->tqe_th->th_seq == tp->rcv_nxt);

    STAM_PROFILE_STOP(&pData->StatTCP_reassamble, tcp_reassamble);
    return flags;
}

/*
 * TCP input routine, follows pages 65-76 of the
 * protocol specification dated September, 1981 very closely.
 */
void
tcp_input(PNATState pData, register struct mbuf *m, int iphlen, struct socket *inso)
{
    struct ip *ip, *save_ip;
    register struct tcpiphdr *ti;
    caddr_t optp = NULL;
    int optlen = 0;
    int len, off;
    int tlen = 0; /* Shut up MSC (didn't check whether MSC was right). */
    register struct tcpcb *tp = 0;
    register int tiflags;
    struct socket *so = 0;
    int todrop, acked, ourfinisacked, needoutput = 0;
/*  int dropsocket = 0; */
    int iss = 0;
    u_long tiwin;
/*  int ts_present = 0; */
    unsigned ohdrlen;
    uint8_t ohdr[60 + 8]; /* max IP header plus 8 bytes of payload for icmp */

    STAM_PROFILE_START(&pData->StatTCP_input, counter_input);

    LogFlow(("tcp_input: m = %p, iphlen = %2d, inso = %R[natsock]\n", m, iphlen, inso));

    if (inso != NULL)
    {
        QSOCKET_LOCK(tcb);
        SOCKET_LOCK(inso);
        QSOCKET_UNLOCK(tcb);
    }
    /*
     * If called with m == 0, then we're continuing the connect
     */
    if (m == NULL)
    {
        so = inso;
        Log4(("NAT: tcp_input: %R[natsock]\n", so));

        /* Re-set a few variables */
        tp = sototcpcb(so);

        m = so->so_m;
        optp = so->so_optp;     /* points into m if set */
        optlen = so->so_optlen;
        so->so_m = NULL;
        so->so_optp = 0;
        so->so_optlen = 0;

        if (RT_LIKELY(so->so_ohdr != NULL))
        {
            RTMemFree(so->so_ohdr);
            so->so_ohdr = NULL;
        }

        ti = so->so_ti;

        /** @todo (vvl) clarify why it might happens */
        if (ti == NULL)
        {
            LogRel(("NAT: ti is null. can't do any reseting connection actions\n"));
            /* mbuf should be cleared in sofree called from tcp_close */
            tcp_close(pData, tp);
            STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
            LogFlowFuncLeave();
            return;
        }

        tiwin = ti->ti_win;
        tiflags = ti->ti_flags;

        LogFlowFunc(("%d -> cont_conn\n", __LINE__));
        goto cont_conn;
    }

    tcpstat.tcps_rcvtotal++;

    ip = mtod(m, struct ip *);

    /* ip_input() subtracts iphlen from ip::ip_len */
    AssertStmt(ip->ip_len + iphlen == (ssize_t)m_length(m, NULL), goto drop);
    if (RT_UNLIKELY(ip->ip_len < sizeof(struct tcphdr)))
    {
        /* tcps_rcvshort++; */
        goto drop;
    }

    /*
     * Save a copy of the IP header in case we want to restore it for
     * sending an ICMP error message in response.
     *
     * XXX: This function should really be fixed to not strip IP
     * options, to not overwrite IP header and to use "tlen" local
     * variable (instead of ti->ti_len), then "m" could be passed to
     * icmp_error() directly.
     */
    ohdrlen = iphlen + 8;
    m_copydata(m, 0, ohdrlen, (caddr_t)ohdr);
    save_ip = (struct ip *)ohdr;
    save_ip->ip_len += iphlen;  /* undo change by ip_input() */


    /*
     * Get IP and TCP header together in first mbuf.
     * Note: IP leaves IP header in first mbuf.
     */
    ti = mtod(m, struct tcpiphdr *);
    if (iphlen > sizeof(struct ip))
    {
        ip_stripoptions(m, (struct mbuf *)0);
        iphlen = sizeof(struct ip);
    }

    /*
     * Checksum extended TCP header and data.
     */
    tlen = ((struct ip *)ti)->ip_len;
    memset(ti->ti_x1, 0, 9);
    ti->ti_len = RT_H2N_U16((u_int16_t)tlen);
    len = sizeof(struct ip) + tlen;
    /* keep checksum for ICMP reply
     * ti->ti_sum = cksum(m, len);
     * if (ti->ti_sum) { */
    if (cksum(m, len))
    {
        tcpstat.tcps_rcvbadsum++;
        LogFlowFunc(("%d -> drop\n", __LINE__));
        goto drop;
    }

    /*
     * Check that TCP offset makes sense,
     * pull out TCP options and adjust length.              XXX
     */
    off = ti->ti_off << 2;
    if (   off < sizeof (struct tcphdr)
        || off > tlen)
    {
        tcpstat.tcps_rcvbadoff++;
        LogFlowFunc(("%d -> drop\n", __LINE__));
        goto drop;
    }
    tlen -= off;
    ti->ti_len = tlen;
    if (off > sizeof (struct tcphdr))
    {
        optlen = off - sizeof (struct tcphdr);
        optp = mtod(m, caddr_t) + sizeof (struct tcpiphdr);

        /*
         * Do quick retrieval of timestamp options ("options
         * prediction?").  If timestamp is the only option and it's
         * formatted as recommended in RFC 1323 appendix A, we
         * quickly get the values now and not bother calling
         * tcp_dooptions(), etc.
         */
#if 0
        if ((   optlen == TCPOLEN_TSTAMP_APPA
             || (   optlen  > TCPOLEN_TSTAMP_APPA
                 && optp[TCPOLEN_TSTAMP_APPA] == TCPOPT_EOL)) &&
                *(u_int32_t *)optp == RT_H2N_U32_C(TCPOPT_TSTAMP_HDR) &&
                (ti->ti_flags & TH_SYN) == 0)
        {
            ts_present = 1;
            ts_val = RT_N2H_U32(*(u_int32_t *)(optp + 4));
            ts_ecr = RT_N2H_U32(*(u_int32_t *)(optp + 8));
            optp = NULL;   / * we have parsed the options * /
        }
#endif
    }
    tiflags = ti->ti_flags;

    /*
     * Convert TCP protocol specific fields to host format.
     */
    NTOHL(ti->ti_seq);
    NTOHL(ti->ti_ack);
    NTOHS(ti->ti_win);
    NTOHS(ti->ti_urp);

    /*
     * Drop TCP, IP headers and TCP options.
     */
    m->m_data += sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);
    m->m_len  -= sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);

    /*
     * Locate pcb for segment.
     */
findso:
    LogFlowFunc(("(enter) findso: %R[natsock]\n", so));
    if (so != NULL && so != &tcb)
        SOCKET_UNLOCK(so);
    QSOCKET_LOCK(tcb);
    so = tcp_last_so;
    if (   so->so_fport        != ti->ti_dport
        || so->so_lport        != ti->ti_sport
        || so->so_laddr.s_addr != ti->ti_src.s_addr
        || so->so_faddr.s_addr != ti->ti_dst.s_addr)
    {
        QSOCKET_UNLOCK(tcb);
        /** @todo fix SOLOOKUP macrodefinition to be usable here */
        so = solookup(&tcb, ti->ti_src, ti->ti_sport,
                      ti->ti_dst, ti->ti_dport);
        if (so)
        {
            tcp_last_so = so;
        }
        ++tcpstat.tcps_socachemiss;
    }
    else
    {
        SOCKET_LOCK(so);
        QSOCKET_UNLOCK(tcb);
    }
    LogFlowFunc(("(leave) findso: %R[natsock]\n", so));

    /*
     * Check whether the packet is targeting CTL_ALIAS and drop it if the connection wasn't
     * initiated by localhost (so == NULL), see @bugref{9896}.
     */
    if (   (CTL_CHECK(ti->ti_dst.s_addr, CTL_ALIAS))
        && !pData->fLocalhostReachable
        && !so)
    {
        LogFlowFunc(("Packet for CTL_ALIAS and fLocalhostReachable=false so=NULL -> drop\n"));
        goto drop;
    }

    /*
     * If the state is CLOSED (i.e., TCB does not exist) then
     * all data in the incoming segment is discarded.
     * If the TCB exists but is in CLOSED state, it is embryonic,
     * but should either do a listen or a connect soon.
     *
     * state == CLOSED means we've done socreate() but haven't
     * attached it to a protocol yet...
     *
     * XXX If a TCB does not exist, and the TH_SYN flag is
     * the only flag set, then create a session, mark it
     * as if it was LISTENING, and continue...
     */
    if (so == 0)
    {
        if ((tiflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) != TH_SYN)
        {
            LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
            goto dropwithreset;
        }

        if ((so = socreate()) == NULL)
        {
            LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
            goto dropwithreset;
        }
        if (tcp_attach(pData, so) < 0)
        {
            RTMemFree(so); /* Not sofree (if it failed, it's not insqued) */
            LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
            goto dropwithreset;
        }
        SOCKET_LOCK(so);
        sbreserve(pData, &so->so_snd, tcp_sndspace);
        sbreserve(pData, &so->so_rcv, tcp_rcvspace);

/*      tcp_last_so = so; */  /* XXX ? */
/*      tp = sototcpcb(so);    */

        so->so_laddr = ti->ti_src;
        so->so_lport = ti->ti_sport;
        so->so_faddr = ti->ti_dst;
        so->so_fport = ti->ti_dport;

        so->so_iptos = ((struct ip *)ti)->ip_tos;

        tp = sototcpcb(so);
        TCP_STATE_SWITCH_TO(tp, TCPS_LISTEN);
    }

    /*
     * If this is a still-connecting socket, this probably
     * a retransmit of the SYN.  Whether it's a retransmit SYN
     * or something else, we nuke it.
     */
    if (so->so_state & SS_ISFCONNECTING)
    {
        LogFlowFunc(("%d -> drop\n", __LINE__));
        goto drop;
    }

    tp = sototcpcb(so);

    /* XXX Should never fail */
    if (tp == 0)
    {
        LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
        goto dropwithreset;
    }
    if (tp->t_state == TCPS_CLOSED)
    {
        LogFlowFunc(("%d -> drop\n", __LINE__));
        goto drop;
    }

    /* Unscale the window into a 32-bit value. */
/*  if ((tiflags & TH_SYN) == 0)
 *      tiwin = ti->ti_win << tp->snd_scale;
 *  else
 */
    tiwin = ti->ti_win;

    /*
     * Segment received on connection.
     * Reset idle time and keep-alive timer.
     */
    tp->t_idle = 0;
    if (so_options)
        tp->t_timer[TCPT_KEEP] = tcp_keepintvl;
    else
        tp->t_timer[TCPT_KEEP] = tcp_keepidle;

    /*
     * Process options if not in LISTEN state,
     * else do it below (after getting remote address).
     */
    if (optp && tp->t_state != TCPS_LISTEN)
        tcp_dooptions(pData, tp, (u_char *)optp, optlen, ti);
/* , */
/*                      &ts_present, &ts_val, &ts_ecr); */

    /*
     * Header prediction: check for the two common cases
     * of a uni-directional data xfer.  If the packet has
     * no control flags, is in-sequence, the window didn't
     * change and we're not retransmitting, it's a
     * candidate.  If the length is zero and the ack moved
     * forward, we're the sender side of the xfer.  Just
     * free the data acked & wake any higher level process
     * that was blocked waiting for space.  If the length
     * is non-zero and the ack didn't move, we're the
     * receiver side.  If we're getting packets in-order
     * (the reassembly queue is empty), add the data to
     * the socket buffer and note that we need a delayed ack.
     *
     * XXX Some of these tests are not needed
     * eg: the tiwin == tp->snd_wnd prevents many more
     * predictions.. with no *real* advantage..
     */
    if (    tp->t_state == TCPS_ESTABLISHED
         && (tiflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) == TH_ACK
/*       && (!ts_present || TSTMP_GEQ(ts_val, tp->ts_recent)) */
         &&  ti->ti_seq == tp->rcv_nxt
         &&  tiwin && tiwin == tp->snd_wnd
         &&  tp->snd_nxt == tp->snd_max)
    {
        /*
         * If last ACK falls within this segment's sequence numbers,
         *  record the timestamp.
         */
#if 0
        if (ts_present && SEQ_LEQ(ti->ti_seq, tp->last_ack_sent) &&
                SEQ_LT(tp->last_ack_sent, ti->ti_seq + ti->ti_len))
        {
            tp->ts_recent_age = tcp_now;
            tp->ts_recent = ts_val;
        }
#endif

        if (ti->ti_len == 0)
        {
            if (   SEQ_GT(ti->ti_ack, tp->snd_una)
                && SEQ_LEQ(ti->ti_ack, tp->snd_max)
                && tp->snd_cwnd >= tp->snd_wnd)
            {
                /*
                 * this is a pure ack for outstanding data.
                 */
                ++tcpstat.tcps_predack;
#if 0
              if (ts_present)
                  tcp_xmit_timer(tp, tcp_now-ts_ecr+1);
              else
#endif
                  if (   tp->t_rtt
                      && SEQ_GT(ti->ti_ack, tp->t_rtseq))
                      tcp_xmit_timer(pData, tp, tp->t_rtt);
              acked = ti->ti_ack - tp->snd_una;
              tcpstat.tcps_rcvackpack++;
              tcpstat.tcps_rcvackbyte += acked;
              sbdrop(&so->so_snd, acked);
              tp->snd_una = ti->ti_ack;
              m_freem(pData, m);

              /*
               * If all outstanding data are acked, stop
               * retransmit timer, otherwise restart timer
               * using current (possibly backed-off) value.
               * If process is waiting for space,
               * wakeup/selwakeup/signal.  If data
               * are ready to send, let tcp_output
               * decide between more output or persist.
               */
              if (tp->snd_una == tp->snd_max)
                  tp->t_timer[TCPT_REXMT] = 0;
              else if (tp->t_timer[TCPT_PERSIST] == 0)
                  tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;

              /*
               * There's room in so_snd, sowwakup will read()
               * from the socket if we can
               */
#if 0
              if (so->so_snd.sb_flags & SB_NOTIFY)
                  sowwakeup(so);
#endif
              /*
               * This is called because sowwakeup might have
               * put data into so_snd.  Since we don't so sowwakeup,
               * we don't need this.. XXX???
               */
              if (SBUF_LEN(&so->so_snd))
                  (void) tcp_output(pData, tp);

              SOCKET_UNLOCK(so);
              STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
              return;
            }
        }
        else if (   ti->ti_ack == tp->snd_una
                 && LIST_EMPTY(&tp->t_segq)
                 && ti->ti_len <= sbspace(&so->so_rcv))
        {
            /*
             * this is a pure, in-sequence data packet
             * with nothing on the reassembly queue and
             * we have enough buffer space to take it.
             */
            ++tcpstat.tcps_preddat;
            tp->rcv_nxt += ti->ti_len;
            tcpstat.tcps_rcvpack++;
            tcpstat.tcps_rcvbyte += ti->ti_len;
            /*
             * Add data to socket buffer.
             */
            sbappend(pData, so, m);

            /*
             * XXX This is called when data arrives.  Later, check
             * if we can actually write() to the socket
             * XXX Need to check? It's be NON_BLOCKING
             */
/*          sorwakeup(so); */

            /*
             * If this is a short packet, then ACK now - with Nagle
             *      congestion avoidance sender won't send more until
             *      he gets an ACK.
             *
             * It is better to not delay acks at all to maximize
             * TCP throughput.  See RFC 2581.
             */
            tp->t_flags |= TF_ACKNOW;
            tcp_output(pData, tp);
            SOCKET_UNLOCK(so);
            STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
            return;
        }
    } /* header prediction */
    /*
     * Calculate amount of space in receive window,
     * and then do TCP input processing.
     * Receive window is amount of space in rcv queue,
     * but not less than advertised window.
     */
    {
        int win;
        win = sbspace(&so->so_rcv);
        if (win < 0)
            win = 0;
        tp->rcv_wnd = max(win, (int)(tp->rcv_adv - tp->rcv_nxt));
    }

    switch (tp->t_state)
    {
        /*
         * If the state is LISTEN then ignore segment if it contains an RST.
         * If the segment contains an ACK then it is bad and send a RST.
         * If it does not contain a SYN then it is not interesting; drop it.
         * Don't bother responding if the destination was a broadcast.
         * Otherwise initialize tp->rcv_nxt, and tp->irs, select an initial
         * tp->iss, and send a segment:
         *     <SEQ=ISS><ACK=RCV_NXT><CTL=SYN,ACK>
         * Also initialize tp->snd_nxt to tp->iss+1 and tp->snd_una to tp->iss.
         * Fill in remote peer address fields if not previously specified.
         * Enter SYN_RECEIVED state, and process any other fields of this
         * segment in this state.
         */
        case TCPS_LISTEN:
        {
            if (tiflags & TH_RST)
            {
                LogFlowFunc(("%d -> drop\n", __LINE__));
                goto drop;
            }
            if (tiflags & TH_ACK)
            {
                LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
                goto dropwithreset;
            }
            if ((tiflags & TH_SYN) == 0)
            {
                LogFlowFunc(("%d -> drop\n", __LINE__));
                goto drop;
            }

            /*
             * This has way too many gotos...
             * But a bit of spaghetti code never hurt anybody :)
             */
            if (   (tcp_fconnect(pData, so) == -1)
                && errno != EINPROGRESS
                && errno != EWOULDBLOCK)
            {
                u_char code = ICMP_UNREACH_NET;
                Log2((" tcp fconnect errno = %d (%s)\n", errno, strerror(errno)));
                if (errno == ECONNREFUSED)
                {
                    /* ACK the SYN, send RST to refuse the connection */
                    tcp_respond(pData, tp, ti, m, ti->ti_seq+1, (tcp_seq)0,
                               TH_RST|TH_ACK);
                }
                else
                {
                    if (errno == EHOSTUNREACH)
                        code = ICMP_UNREACH_HOST;
                    HTONL(ti->ti_seq);             /* restore tcp header */
                    HTONL(ti->ti_ack);
                    HTONS(ti->ti_win);
                    HTONS(ti->ti_urp);
                    m->m_data -= sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);
                    m->m_len  += sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);
                    *ip = *save_ip;
                    icmp_error(pData, m, ICMP_UNREACH, code, 0, strerror(errno));
                    tp->t_socket->so_m = NULL;
                }
                tp = tcp_close(pData, tp);
            }
            else
            {
                /*
                 * Haven't connected yet, save the current mbuf
                 * and ti, and return
                 * XXX Some OS's don't tell us whether the connect()
                 * succeeded or not.  So we must time it out.
                 */
                so->so_m = m;
                so->so_ti = ti;
                so->so_ohdr = RTMemDup(ohdr, ohdrlen);
                so->so_optp = optp;
                so->so_optlen = optlen;
                tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
                TCP_STATE_SWITCH_TO(tp, TCPS_SYN_RECEIVED);
            }
            SOCKET_UNLOCK(so);
            STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
            LogFlowFuncLeave();
            return;

cont_conn:
            /* m==NULL
             * Check if the connect succeeded
             */
            LogFlowFunc(("cont_conn:\n"));
            if (so->so_state & SS_NOFDREF)
            {
                tp = tcp_close(pData, tp);
                LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
                goto dropwithreset;
            }

            tcp_template(tp);

            if (optp)
                tcp_dooptions(pData, tp, (u_char *)optp, optlen, ti);

            if (iss)
                tp->iss = iss;
            else
                tp->iss = tcp_iss;
            tcp_iss += TCP_ISSINCR/2;
            tp->irs = ti->ti_seq;
            tcp_sendseqinit(tp);
            tcp_rcvseqinit(tp);
            tp->t_flags |= TF_ACKNOW;
            TCP_STATE_SWITCH_TO(tp, TCPS_SYN_RECEIVED);
            tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
            tcpstat.tcps_accepts++;
            LogFlowFunc(("%d -> trimthenstep6\n", __LINE__));
            goto trimthenstep6;
        } /* case TCPS_LISTEN */

        /*
         * If the state is SYN_SENT:
         *      if seg contains an ACK, but not for our SYN, drop the input.
         *      if seg contains a RST, then drop the connection.
         *      if seg does not contain SYN, then drop it.
         * Otherwise this is an acceptable SYN segment
         *      initialize tp->rcv_nxt and tp->irs
         *      if seg contains ack then advance tp->snd_una
         *      if SYN has been acked change to ESTABLISHED else SYN_RCVD state
         *      arrange for segment to be acked (eventually)
         *      continue processing rest of data/controls, beginning with URG
         */
        case TCPS_SYN_SENT:
            if (   (tiflags & TH_ACK)
                && (   SEQ_LEQ(ti->ti_ack, tp->iss)
                    || SEQ_GT(ti->ti_ack, tp->snd_max)))
            {
                LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
                goto dropwithreset;
            }

            if (tiflags & TH_RST)
            {
                if (tiflags & TH_ACK)
                    tp = tcp_drop(pData, tp, 0); /* XXX Check t_softerror! */
                LogFlowFunc(("%d -> drop\n", __LINE__));
                goto drop;
            }

            if ((tiflags & TH_SYN) == 0)
            {
                LogFlowFunc(("%d -> drop\n", __LINE__));
                goto drop;
            }
            if (tiflags & TH_ACK)
            {
                tp->snd_una = ti->ti_ack;
                if (SEQ_LT(tp->snd_nxt, tp->snd_una))
                    tp->snd_nxt = tp->snd_una;
            }

            tp->t_timer[TCPT_REXMT] = 0;
            tp->irs = ti->ti_seq;
            tcp_rcvseqinit(tp);
            tp->t_flags |= TF_ACKNOW;
            if (tiflags & TH_ACK && SEQ_GT(tp->snd_una, tp->iss))
            {
                tcpstat.tcps_connects++;
                soisfconnected(so);
                TCP_STATE_SWITCH_TO(tp, TCPS_ESTABLISHED);

                /* Do window scaling on this connection? */
#if 0
                if ((  tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE))
                     == (TF_RCVD_SCALE|TF_REQ_SCALE))
                {
                    tp->snd_scale = tp->requested_s_scale;
                    tp->rcv_scale = tp->request_r_scale;
                }
#endif
                (void) tcp_reass(pData, tp, (struct tcphdr *)0, NULL, (struct mbuf *)0);
                /*
                 * if we didn't have to retransmit the SYN,
                 * use its rtt as our initial srtt & rtt var.
                 */
                if (tp->t_rtt)
                    tcp_xmit_timer(pData, tp, tp->t_rtt);
            }
            else
                TCP_STATE_SWITCH_TO(tp, TCPS_SYN_RECEIVED);

trimthenstep6:
            LogFlowFunc(("trimthenstep6:\n"));
            /*
             * Advance ti->ti_seq to correspond to first data byte.
             * If data, trim to stay within window,
             * dropping FIN if necessary.
             */
            ti->ti_seq++;
            if (ti->ti_len > tp->rcv_wnd)
            {
                todrop = ti->ti_len - tp->rcv_wnd;
                m_adj(m, -todrop);
                ti->ti_len = tp->rcv_wnd;
                tiflags &= ~TH_FIN;
                tcpstat.tcps_rcvpackafterwin++;
                tcpstat.tcps_rcvbyteafterwin += todrop;
            }
            tp->snd_wl1 = ti->ti_seq - 1;
            tp->rcv_up = ti->ti_seq;
            LogFlowFunc(("%d -> step6\n", __LINE__));
            goto step6;
    } /* switch tp->t_state */
    /*
     * States other than LISTEN or SYN_SENT.
     * First check timestamp, if present.
     * Then check that at least some bytes of segment are within
     * receive window.  If segment begins before rcv_nxt,
     * drop leading data (and SYN); if nothing left, just ack.
     *
     * RFC 1323 PAWS: If we have a timestamp reply on this segment
     * and it's less than ts_recent, drop it.
     */
#if 0
    if (   ts_present
        && (tiflags & TH_RST) == 0
        && tp->ts_recent
        && TSTMP_LT(ts_val, tp->ts_recent))
    {
        /* Check to see if ts_recent is over 24 days old.  */
        if ((int)(tcp_now - tp->ts_recent_age) > TCP_PAWS_IDLE)
        {
            /*
             * Invalidate ts_recent.  If this segment updates
             * ts_recent, the age will be reset later and ts_recent
             * will get a valid value.  If it does not, setting
             * ts_recent to zero will at least satisfy the
             * requirement that zero be placed in the timestamp
             * echo reply when ts_recent isn't valid.  The
             * age isn't reset until we get a valid ts_recent
             * because we don't want out-of-order segments to be
             * dropped when ts_recent is old.
             */
            tp->ts_recent = 0;
        }
        else
        {
            tcpstat.tcps_rcvduppack++;
            tcpstat.tcps_rcvdupbyte += ti->ti_len;
            tcpstat.tcps_pawsdrop++;
            goto dropafterack;
        }
     }
#endif

    todrop = tp->rcv_nxt - ti->ti_seq;
    if (todrop > 0)
    {
        if (tiflags & TH_SYN)
        {
            tiflags &= ~TH_SYN;
            ti->ti_seq++;
            if (ti->ti_urp > 1)
                ti->ti_urp--;
            else
                tiflags &= ~TH_URG;
            todrop--;
        }
        /*
         * Following if statement from Stevens, vol. 2, p. 960.
         */
        if (   todrop > ti->ti_len
            || (   todrop == ti->ti_len
                && (tiflags & TH_FIN) == 0))
        {
            /*
             * Any valid FIN must be to the left of the window.
             * At this point the FIN must be a duplicate or out
             * of sequence; drop it.
             */
            tiflags &= ~TH_FIN;

            /*
             * Send an ACK to resynchronize and drop any data.
             * But keep on processing for RST or ACK.
             */
            tp->t_flags |= TF_ACKNOW;
            todrop = ti->ti_len;
            tcpstat.tcps_rcvduppack++;
            tcpstat.tcps_rcvdupbyte += todrop;
        }
        else
        {
            tcpstat.tcps_rcvpartduppack++;
            tcpstat.tcps_rcvpartdupbyte += todrop;
        }
        m_adj(m, todrop);
        ti->ti_seq += todrop;
        ti->ti_len -= todrop;
        if (ti->ti_urp > todrop)
            ti->ti_urp -= todrop;
        else
        {
            tiflags &= ~TH_URG;
            ti->ti_urp = 0;
        }
    }
    /*
     * If new data are received on a connection after the
     * user processes are gone, then RST the other end.
     */
    if (   (so->so_state & SS_NOFDREF)
        && tp->t_state > TCPS_CLOSE_WAIT && ti->ti_len)
    {
        tp = tcp_close(pData, tp);
        tcpstat.tcps_rcvafterclose++;
        LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
        goto dropwithreset;
    }

    /*
     * If segment ends after window, drop trailing data
     * (and PUSH and FIN); if nothing left, just ACK.
     */
    todrop = (ti->ti_seq+ti->ti_len) - (tp->rcv_nxt+tp->rcv_wnd);
    if (todrop > 0)
    {
        tcpstat.tcps_rcvpackafterwin++;
        if (todrop >= ti->ti_len)
        {
            tcpstat.tcps_rcvbyteafterwin += ti->ti_len;
            /*
             * If a new connection request is received
             * while in TIME_WAIT, drop the old connection
             * and start over if the sequence numbers
             * are above the previous ones.
             */
            if (   tiflags & TH_SYN
                && tp->t_state == TCPS_TIME_WAIT
                && SEQ_GT(ti->ti_seq, tp->rcv_nxt))
            {
                iss = tp->rcv_nxt + TCP_ISSINCR;
                tp = tcp_close(pData, tp);
                SOCKET_UNLOCK(tp->t_socket);
                LogFlowFunc(("%d -> findso\n", __LINE__));
                goto findso;
            }
            /*
             * If window is closed can only take segments at
             * window edge, and have to drop data and PUSH from
             * incoming segments.  Continue processing, but
             * remember to ack.  Otherwise, drop segment
             * and ack.
             */
            if (tp->rcv_wnd == 0 && ti->ti_seq == tp->rcv_nxt)
            {
                tp->t_flags |= TF_ACKNOW;
                tcpstat.tcps_rcvwinprobe++;
            }
            else
            {
                LogFlowFunc(("%d -> dropafterack\n", __LINE__));
                goto dropafterack;
            }
        }
        else
            tcpstat.tcps_rcvbyteafterwin += todrop;
        m_adj(m, -todrop);
        ti->ti_len -= todrop;
        tiflags &= ~(TH_PUSH|TH_FIN);
    }

    /*
     * If last ACK falls within this segment's sequence numbers,
     * record its timestamp.
     */
#if 0
    if (   ts_present
        && SEQ_LEQ(ti->ti_seq, tp->last_ack_sent)
        && SEQ_LT(tp->last_ack_sent, ti->ti_seq + ti->ti_len + ((tiflags & (TH_SYN|TH_FIN)) != 0)))
    {
        tp->ts_recent_age = tcp_now;
        tp->ts_recent = ts_val;
    }
#endif

    /*
     * If the RST bit is set examine the state:
     *    SYN_RECEIVED STATE:
     *      If passive open, return to LISTEN state.
     *      If active open, inform user that connection was refused.
     *    ESTABLISHED, FIN_WAIT_1, FIN_WAIT2, CLOSE_WAIT STATES:
     *      Inform user that connection was reset, and close tcb.
     *    CLOSING, LAST_ACK, TIME_WAIT STATES
     *      Close the tcb.
     */
    if (tiflags&TH_RST)
        switch (tp->t_state)
        {
            case TCPS_SYN_RECEIVED:
/*              so->so_error = ECONNREFUSED; */
                LogFlowFunc(("%d -> close\n", __LINE__));
                goto close;

            case TCPS_ESTABLISHED:
            case TCPS_FIN_WAIT_1:
            case TCPS_FIN_WAIT_2:
            case TCPS_CLOSE_WAIT:
/*              so->so_error = ECONNRESET; */
close:
            LogFlowFunc(("close:\n"));
            TCP_STATE_SWITCH_TO(tp, TCPS_CLOSED);
            tcpstat.tcps_drops++;
            tp = tcp_close(pData, tp);
            LogFlowFunc(("%d -> drop\n", __LINE__));
            goto drop;

            case TCPS_CLOSING:
            case TCPS_LAST_ACK:
            case TCPS_TIME_WAIT:
            tp = tcp_close(pData, tp);
            LogFlowFunc(("%d -> drop\n", __LINE__));
            goto drop;
        }

    /*
     * If a SYN is in the window, then this is an
     * error and we send an RST and drop the connection.
     */
    if (tiflags & TH_SYN)
    {
        tp = tcp_drop(pData, tp, 0);
        LogFlowFunc(("%d -> dropwithreset\n", __LINE__));
        goto dropwithreset;
    }

    /*
     * If the ACK bit is off we drop the segment and return.
     */
    if ((tiflags & TH_ACK) == 0)
    {
        LogFlowFunc(("%d -> drop\n", __LINE__));
        goto drop;
    }

    /*
     * Ack processing.
     */
    switch (tp->t_state)
    {
        /*
         * In SYN_RECEIVED state if the ack ACKs our SYN then enter
         * ESTABLISHED state and continue processing, otherwise
         * send an RST.  una<=ack<=max
         */
        case TCPS_SYN_RECEIVED:
            LogFlowFunc(("%d -> TCPS_SYN_RECEIVED\n", __LINE__));
            if (   SEQ_GT(tp->snd_una, ti->ti_ack)
                || SEQ_GT(ti->ti_ack, tp->snd_max))
                goto dropwithreset;
            tcpstat.tcps_connects++;
            TCP_STATE_SWITCH_TO(tp, TCPS_ESTABLISHED);
            /*
             * The sent SYN is ack'ed with our sequence number +1
             * The first data byte already in the buffer will get
             * lost if no correction is made.  This is only needed for
             * SS_CTL since the buffer is empty otherwise.
             * tp->snd_una++; or:
             */
            tp->snd_una = ti->ti_ack;
            soisfconnected(so);

            /* Do window scaling? */
#if 0
            if (   (tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE))
                == (TF_RCVD_SCALE|TF_REQ_SCALE))
            {
                tp->snd_scale = tp->requested_s_scale;
                tp->rcv_scale = tp->request_r_scale;
            }
#endif
            (void) tcp_reass(pData, tp, (struct tcphdr *)0, (int *)0, (struct mbuf *)0);
            tp->snd_wl1 = ti->ti_seq - 1;
            /* Avoid ack processing; snd_una==ti_ack  =>  dup ack */
            LogFlowFunc(("%d -> synrx_to_est\n", __LINE__));
            goto synrx_to_est;
            /* fall into ... */

            /*
             * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
             * ACKs.  If the ack is in the range
             *      tp->snd_una < ti->ti_ack <= tp->snd_max
             * then advance tp->snd_una to ti->ti_ack and drop
             * data from the retransmission queue.  If this ACK reflects
             * more up to date window information we update our window information.
             */
        case TCPS_ESTABLISHED:
        case TCPS_FIN_WAIT_1:
        case TCPS_FIN_WAIT_2:
        case TCPS_CLOSE_WAIT:
        case TCPS_CLOSING:
        case TCPS_LAST_ACK:
        case TCPS_TIME_WAIT:
            LogFlowFunc(("%d -> TCPS_ESTABLISHED|TCPS_FIN_WAIT_1|TCPS_FIN_WAIT_2|TCPS_CLOSE_WAIT|"
                         "TCPS_CLOSING|TCPS_LAST_ACK|TCPS_TIME_WAIT\n", __LINE__));
            if (SEQ_LEQ(ti->ti_ack, tp->snd_una))
            {
                if (ti->ti_len == 0 && tiwin == tp->snd_wnd)
                {
                    tcpstat.tcps_rcvdupack++;
                    Log2((" dup ack  m = %p, so = %p\n", m, so));
                    /*
                     * If we have outstanding data (other than
                     * a window probe), this is a completely
                     * duplicate ack (ie, window info didn't
                     * change), the ack is the biggest we've
                     * seen and we've seen exactly our rexmt
                     * threshold of them, assume a packet
                     * has been dropped and retransmit it.
                     * Kludge snd_nxt & the congestion
                     * window so we send only this one
                     * packet.
                     *
                     * We know we're losing at the current
                     * window size so do congestion avoidance
                     * (set ssthresh to half the current window
                     * and pull our congestion window back to
                     * the new ssthresh).
                     *
                     * Dup acks mean that packets have left the
                     * network (they're now cached at the receiver)
                     * so bump cwnd by the amount in the receiver
                     * to keep a constant cwnd packets in the
                     * network.
                     */
                    if (   tp->t_timer[TCPT_REXMT] == 0
                        || ti->ti_ack != tp->snd_una)
                        tp->t_dupacks = 0;
                    else if (++tp->t_dupacks == tcprexmtthresh)
                    {
                        tcp_seq onxt = tp->snd_nxt;
                        u_int win = min(tp->snd_wnd, tp->snd_cwnd) / 2 / tp->t_maxseg;
                        if (win < 2)
                            win = 2;
                        tp->snd_ssthresh = win * tp->t_maxseg;
                        tp->t_timer[TCPT_REXMT] = 0;
                        tp->t_rtt = 0;
                        tp->snd_nxt = ti->ti_ack;
                        tp->snd_cwnd = tp->t_maxseg;
                        (void) tcp_output(pData, tp);
                        tp->snd_cwnd = tp->snd_ssthresh +
                            tp->t_maxseg * tp->t_dupacks;
                        if (SEQ_GT(onxt, tp->snd_nxt))
                            tp->snd_nxt = onxt;
                        LogFlowFunc(("%d -> drop\n", __LINE__));
                        goto drop;
                    }
                    else if (tp->t_dupacks > tcprexmtthresh)
                    {
                        tp->snd_cwnd += tp->t_maxseg;
                        (void) tcp_output(pData, tp);
                        LogFlowFunc(("%d -> drop\n", __LINE__));
                        goto drop;
                    }
                }
                else
                    tp->t_dupacks = 0;
                break;
            }
synrx_to_est:
            LogFlowFunc(("synrx_to_est:\n"));
            /*
             * If the congestion window was inflated to account
             * for the other side's cached packets, retract it.
             */
            if (   tp->t_dupacks > tcprexmtthresh
                && tp->snd_cwnd > tp->snd_ssthresh)
                tp->snd_cwnd = tp->snd_ssthresh;
            tp->t_dupacks = 0;
            if (SEQ_GT(ti->ti_ack, tp->snd_max))
            {
                tcpstat.tcps_rcvacktoomuch++;
                LogFlowFunc(("%d -> dropafterack\n", __LINE__));
                goto dropafterack;
            }
            acked = ti->ti_ack - tp->snd_una;
            tcpstat.tcps_rcvackpack++;
            tcpstat.tcps_rcvackbyte += acked;

            /*
             * If we have a timestamp reply, update smoothed
             * round trip time.  If no timestamp is present but
             * transmit timer is running and timed sequence
             * number was acked, update smoothed round trip time.
             * Since we now have an rtt measurement, cancel the
             * timer backoff (cf., Phil Karn's retransmit alg.).
             * Recompute the initial retransmit timer.
             */
#if 0
            if (ts_present)
                tcp_xmit_timer(tp, tcp_now-ts_ecr+1);
            else
#endif
                if (tp->t_rtt && SEQ_GT(ti->ti_ack, tp->t_rtseq))
                    tcp_xmit_timer(pData, tp, tp->t_rtt);

            /*
             * If all outstanding data is acked, stop retransmit
             * timer and remember to restart (more output or persist).
             * If there is more data to be acked, restart retransmit
             * timer, using current (possibly backed-off) value.
             */
            if (ti->ti_ack == tp->snd_max)
            {
                tp->t_timer[TCPT_REXMT] = 0;
                needoutput = 1;
            }
            else if (tp->t_timer[TCPT_PERSIST] == 0)
                tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;
            /*
             * When new data is acked, open the congestion window.
             * If the window gives us less than ssthresh packets
             * in flight, open exponentially (maxseg per packet).
             * Otherwise open linearly: maxseg per window
             * (maxseg^2 / cwnd per packet).
             */
            {
                register u_int cw = tp->snd_cwnd;
                register u_int incr = tp->t_maxseg;

                if (cw > tp->snd_ssthresh)
                    incr = incr * incr / cw;
                tp->snd_cwnd = min(cw + incr, TCP_MAXWIN<<tp->snd_scale);
            }
            if (acked > SBUF_LEN(&so->so_snd))
            {
                tp->snd_wnd -= SBUF_LEN(&so->so_snd);
                sbdrop(&so->so_snd, (int)so->so_snd.sb_cc);
                ourfinisacked = 1;
            }
            else
            {
                sbdrop(&so->so_snd, acked);
                tp->snd_wnd -= acked;
                ourfinisacked = 0;
            }
            /*
             * XXX sowwakup is called when data is acked and there's room for
             * for more data... it should read() the socket
             */
#if 0
            if (so->so_snd.sb_flags & SB_NOTIFY)
                sowwakeup(so);
#endif
            tp->snd_una = ti->ti_ack;
            if (SEQ_LT(tp->snd_nxt, tp->snd_una))
                tp->snd_nxt = tp->snd_una;

            switch (tp->t_state)
            {
                /*
                 * In FIN_WAIT_1 STATE in addition to the processing
                 * for the ESTABLISHED state if our FIN is now acknowledged
                 * then enter FIN_WAIT_2.
                 */
                case TCPS_FIN_WAIT_1:
                    if (ourfinisacked)
                    {
                        /*
                         * If we can't receive any more
                         * data, then closing user can proceed.
                         * Starting the timer is contrary to the
                         * specification, but if we don't get a FIN
                         * we'll hang forever.
                         */
                        if (so->so_state & SS_FCANTRCVMORE)
                        {
                            soisfdisconnected(so);
                            tp->t_timer[TCPT_2MSL] = tcp_maxidle;
                        }
                        TCP_STATE_SWITCH_TO(tp, TCPS_FIN_WAIT_2);
                    }
                    break;

                /*
                 * In CLOSING STATE in addition to the processing for
                 * the ESTABLISHED state if the ACK acknowledges our FIN
                 * then enter the TIME-WAIT state, otherwise ignore
                 * the segment.
                 */
                case TCPS_CLOSING:
                    if (ourfinisacked)
                    {
                        TCP_STATE_SWITCH_TO(tp, TCPS_TIME_WAIT);
                        tcp_canceltimers(tp);
                        tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
                        soisfdisconnected(so);
                    }
                    break;

                /*
                 * In LAST_ACK, we may still be waiting for data to drain
                 * and/or to be acked, as well as for the ack of our FIN.
                 * If our FIN is now acknowledged, delete the TCB,
                 * enter the closed state and return.
                 */
                case TCPS_LAST_ACK:
                    if (ourfinisacked)
                    {
                        tp = tcp_close(pData, tp);
                        LogFlowFunc(("%d -> drop\n", __LINE__));
                        goto drop;
                    }
                    break;

                /*
                 * In TIME_WAIT state the only thing that should arrive
                 * is a retransmission of the remote FIN.  Acknowledge
                 * it and restart the finack timer.
                 */
                case TCPS_TIME_WAIT:
                    tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
                    LogFlowFunc(("%d -> dropafterack\n", __LINE__));
                    goto dropafterack;
            }
    } /* switch(tp->t_state) */

step6:
    LogFlowFunc(("step6:\n"));
    /*
     * Update window information.
     * Don't look at window if no ACK: TAC's send garbage on first SYN.
     */
    if (   (tiflags & TH_ACK)
        && (   SEQ_LT(tp->snd_wl1, ti->ti_seq)
            || (   tp->snd_wl1 == ti->ti_seq
                && (   SEQ_LT(tp->snd_wl2, ti->ti_ack)
                    || (   tp->snd_wl2 == ti->ti_ack
                        && tiwin > tp->snd_wnd)))))
    {
        /* keep track of pure window updates */
        if (   ti->ti_len == 0
            && tp->snd_wl2 == ti->ti_ack
            && tiwin > tp->snd_wnd)
            tcpstat.tcps_rcvwinupd++;
        tp->snd_wnd = tiwin;
        tp->snd_wl1 = ti->ti_seq;
        tp->snd_wl2 = ti->ti_ack;
        if (tp->snd_wnd > tp->max_sndwnd)
            tp->max_sndwnd = tp->snd_wnd;
        needoutput = 1;
    }

    /*
     * Process segments with URG.
     */
    if ((tiflags & TH_URG) && ti->ti_urp &&
            TCPS_HAVERCVDFIN(tp->t_state) == 0)
    {
        /*
         * This is a kludge, but if we receive and accept
         * random urgent pointers, we'll crash in
         * soreceive.  It's hard to imagine someone
         * actually wanting to send this much urgent data.
         */
        if (ti->ti_urp + so->so_rcv.sb_cc > so->so_rcv.sb_datalen)
        {
            ti->ti_urp = 0;
            tiflags &= ~TH_URG;
            LogFlowFunc(("%d -> dodata\n", __LINE__));
            goto dodata;
        }

        /*
         * If this segment advances the known urgent pointer,
         * then mark the data stream.  This should not happen
         * in CLOSE_WAIT, CLOSING, LAST_ACK or TIME_WAIT STATES since
         * a FIN has been received from the remote side.
         * In these states we ignore the URG.
         *
         * According to RFC961 (Assigned Protocols),
         * the urgent pointer points to the last octet
         * of urgent data.  We continue, however,
         * to consider it to indicate the first octet
         * of data past the urgent section as the original
         * spec states (in one of two places).
         */
        if (SEQ_GT(ti->ti_seq+ti->ti_urp, tp->rcv_up))
        {
            tp->rcv_up = ti->ti_seq + ti->ti_urp;
            so->so_urgc =  SBUF_LEN(&so->so_rcv) +
                (tp->rcv_up - tp->rcv_nxt); /* -1; */
            tp->rcv_up = ti->ti_seq + ti->ti_urp;
        }
    }
    else
        /*
         * If no out of band data is expected,
         * pull receive urgent pointer along
         * with the receive window.
         */
        if (SEQ_GT(tp->rcv_nxt, tp->rcv_up))
            tp->rcv_up = tp->rcv_nxt;
dodata:
    LogFlowFunc(("dodata:\n"));

    /*
     * If this is a small packet, then ACK now - with Nagel
     *      congestion avoidance sender won't send more until
     *      he gets an ACK.
     *
     * XXX: In case you wonder...  The magic "27" below is ESC that
     * presumably starts a terminal escape-sequence and that we want
     * to ACK ASAP.  [Original slirp code had three different
     * heuristics to chose from here and in the header prediction case
     * above, but the commented out alternatives were lost and the
     * header prediction case that had an expanded comment about this
     * has been modified to always send an ACK].
     */
    if (   ti->ti_len
        && (unsigned)ti->ti_len <= 5
        && ((struct tcpiphdr_2 *)ti)->first_char == (char)27)
    {
        tp->t_flags |= TF_ACKNOW;
    }

    /*
     * Process the segment text, merging it into the TCP sequencing queue,
     * and arranging for acknowledgment of receipt if necessary.
     * This process logically involves adjusting tp->rcv_wnd as data
     * is presented to the user (this happens in tcp_usrreq.c,
     * case PRU_RCVD).  If a FIN has already been received on this
     * connection then we just ignore the text.
     */
    if (   (ti->ti_len || (tiflags&TH_FIN))
        && TCPS_HAVERCVDFIN(tp->t_state) == 0)
    {
        if (   ti->ti_seq == tp->rcv_nxt
            && LIST_EMPTY(&tp->t_segq)
            && tp->t_state == TCPS_ESTABLISHED)
        {
            DELAY_ACK(tp, ti); /* little bit different from BSD declaration see netinet/tcp_input.c */
            tp->rcv_nxt += tlen;
            tiflags = ti->ti_t.th_flags & TH_FIN;
            tcpstat.tcps_rcvpack++;
            tcpstat.tcps_rcvbyte += tlen;
            if (so->so_state & SS_FCANTRCVMORE)
                m_freem(pData, m);
            else
                sbappend(pData, so, m);
        }
        else
        {
            tiflags = tcp_reass(pData, tp, &ti->ti_t, &tlen, m);
            tp->t_flags |= TF_ACKNOW;
        }
        /*
         * Note the amount of data that peer has sent into
         * our window, in order to estimate the sender's
         * buffer size.
         */
        len = SBUF_SIZE(&so->so_rcv) - (tp->rcv_adv - tp->rcv_nxt);
    }
    else
    {
        m_freem(pData, m);
        tiflags &= ~TH_FIN;
    }

    /*
     * If FIN is received ACK the FIN and let the user know
     * that the connection is closing.
     */
    if (tiflags & TH_FIN)
    {
        if (TCPS_HAVERCVDFIN(tp->t_state) == 0)
        {
            /*
             * If we receive a FIN we can't send more data,
             * set it SS_FDRAIN
             * Shutdown the socket if there is no rx data in the
             * buffer.
             * soread() is called on completion of shutdown() and
             * will got to TCPS_LAST_ACK, and use tcp_output()
             * to send the FIN.
             */
/*          sofcantrcvmore(so); */
            sofwdrain(so);

            tp->t_flags |= TF_ACKNOW;
            tp->rcv_nxt++;
        }
        switch (tp->t_state)
        {
            /*
             * In SYN_RECEIVED and ESTABLISHED STATES
             * enter the CLOSE_WAIT state.
             */
            case TCPS_SYN_RECEIVED:
            case TCPS_ESTABLISHED:
                TCP_STATE_SWITCH_TO(tp, TCPS_CLOSE_WAIT);
                break;

            /*
             * If still in FIN_WAIT_1 STATE FIN has not been acked so
             * enter the CLOSING state.
             */
            case TCPS_FIN_WAIT_1:
                TCP_STATE_SWITCH_TO(tp, TCPS_CLOSING);
                break;

            /*
             * In FIN_WAIT_2 state enter the TIME_WAIT state,
             * starting the time-wait timer, turning off the other
             * standard timers.
             */
            case TCPS_FIN_WAIT_2:
                TCP_STATE_SWITCH_TO(tp, TCPS_TIME_WAIT);
                tcp_canceltimers(tp);
                tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
                soisfdisconnected(so);
                break;

            /*
             * In TIME_WAIT state restart the 2 MSL time_wait timer.
             */
            case TCPS_TIME_WAIT:
                tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
                break;
        }
    }

    /*
     * Return any desired output.
     */
    if (needoutput || (tp->t_flags & TF_ACKNOW))
        tcp_output(pData, tp);

    SOCKET_UNLOCK(so);
    STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
    LogFlowFuncLeave();
    return;

dropafterack:
    LogFlowFunc(("dropafterack:\n"));
    /*
     * Generate an ACK dropping incoming segment if it occupies
     * sequence space, where the ACK reflects our state.
     */
    if (tiflags & TH_RST)
    {
        LogFlowFunc(("%d -> drop\n", __LINE__));
        goto drop;
    }
    m_freem(pData, m);
    tp->t_flags |= TF_ACKNOW;
    (void) tcp_output(pData, tp);
    SOCKET_UNLOCK(so);
    STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
    LogFlowFuncLeave();
    return;

dropwithreset:
    LogFlowFunc(("dropwithreset:\n"));
    /* reuses m if m!=NULL, m_free() unnecessary */
    if (tiflags & TH_ACK)
        tcp_respond(pData, tp, ti, m, (tcp_seq)0, ti->ti_ack, TH_RST);
    else
    {
        if (tiflags & TH_SYN)
            ti->ti_len++;
        tcp_respond(pData, tp, ti, m, ti->ti_seq+ti->ti_len, (tcp_seq)0,
                    TH_RST|TH_ACK);
    }

    if (so != &tcb)
        SOCKET_UNLOCK(so);
    STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
    LogFlowFuncLeave();
    return;

drop:
    LogFlowFunc(("drop:\n"));
    /*
     * Drop space held by incoming segment and return.
     */
    m_freem(pData, m);

#ifdef VBOX_WITH_SLIRP_MT
    if (RTCritSectIsOwned(&so->so_mutex))
    {
        SOCKET_UNLOCK(so);
    }
#endif

    STAM_PROFILE_STOP(&pData->StatTCP_input, counter_input);
    LogFlowFuncLeave();
    return;
}


void
tcp_fconnect_failed(PNATState pData, struct socket *so, int sockerr)
{
    struct tcpcb *tp;
    int code;

    Log2(("NAT: connect error %d %R[natsock]\n", sockerr, so));

    Assert(so->so_state & SS_ISFCONNECTING);
    so->so_state = SS_NOFDREF;

    if (sockerr == ECONNREFUSED || sockerr == ECONNRESET)
    {
        /* hand off to tcp_input():cont_conn to send RST */
        TCP_INPUT(pData, NULL, 0, so);
        return;
    }

    tp = sototcpcb(so);
    if (RT_UNLIKELY(tp == NULL)) /* should never happen */
    {
        LogRel(("NAT: tp == NULL %R[natsock]\n", so));
        sofree(pData, so);
        return;
    }

    if (sockerr == ENETUNREACH || sockerr == ENETDOWN)
        code = ICMP_UNREACH_NET;
    else if (sockerr == EHOSTUNREACH || sockerr == EHOSTDOWN)
        code = ICMP_UNREACH_HOST;
    else
        code = -1;

    if (code >= 0)
    {
        struct ip *oip;
        unsigned ohdrlen;
        struct mbuf *m;

        if (RT_UNLIKELY(so->so_ohdr == NULL))
            goto out;

        oip = (struct ip *)so->so_ohdr;
        ohdrlen = oip->ip_hl * 4 + 8;

        m = m_gethdr(pData, M_NOWAIT, MT_HEADER);
        if (RT_UNLIKELY(m == NULL))
            goto out;

        m_copyback(pData, m, 0, ohdrlen, (caddr_t)so->so_ohdr);
        m->m_pkthdr.header = mtod(m, void *);

        icmp_error(pData, m, ICMP_UNREACH, code, 0, NULL);
    }

  out:
    tcp_close(pData, tp);
}


void
tcp_dooptions(PNATState pData, struct tcpcb *tp, u_char *cp, int cnt, struct tcpiphdr *ti)
{
    u_int16_t mss;
    int opt, optlen;

    LogFlowFunc(("tcp_dooptions: tp = %R[tcpcb793], cnt=%i\n", tp, cnt));

    for (; cnt > 0; cnt -= optlen, cp += optlen)
    {
        opt = cp[0];
        if (opt == TCPOPT_EOL)
            break;
        if (opt == TCPOPT_NOP)
            optlen = 1;
        else
        {
            optlen = cp[1];
            if (optlen <= 0)
                break;
        }
        switch (opt)
        {
            default:
                continue;

            case TCPOPT_MAXSEG:
                if (optlen != TCPOLEN_MAXSEG)
                    continue;
                if (!(ti->ti_flags & TH_SYN))
                    continue;
                memcpy((char *) &mss, (char *) cp + 2, sizeof(mss));
                NTOHS(mss);
                (void) tcp_mss(pData, tp, mss); /* sets t_maxseg */
                break;

#if 0
            case TCPOPT_WINDOW:
                if (optlen != TCPOLEN_WINDOW)
                    continue;
                if (!(ti->ti_flags & TH_SYN))
                    continue;
                tp->t_flags |= TF_RCVD_SCALE;
                tp->requested_s_scale = min(cp[2], TCP_MAX_WINSHIFT);
                break;

            case TCPOPT_TIMESTAMP:
                if (optlen != TCPOLEN_TIMESTAMP)
                    continue;
                *ts_present = 1;
                memcpy((char *) ts_val, (char *)cp + 2, sizeof(*ts_val));
                NTOHL(*ts_val);
                memcpy((char *) ts_ecr, (char *)cp + 6, sizeof(*ts_ecr));
                NTOHL(*ts_ecr);

                /*
                 * A timestamp received in a SYN makes
                 * it ok to send timestamp requests and replies.
                 */
                if (ti->ti_flags & TH_SYN)
                {
                    tp->t_flags |= TF_RCVD_TSTMP;
                    tp->ts_recent = *ts_val;
                    tp->ts_recent_age = tcp_now;
                }
                break;
#endif
                }
        }
}


/*
 * Pull out of band byte out of a segment so
 * it doesn't appear in the user's data queue.
 * It is still reflected in the segment length for
 * sequencing purposes.
 */

#if 0
void
tcp_pulloutofband(struct socket *so, struct tcpiphdr *ti, struct mbuf *m)
{
    int cnt = ti->ti_urp - 1;

    while (cnt >= 0)
    {
        if (m->m_len > cnt)
        {
            char *cp = mtod(m, caddr_t) + cnt;
            struct tcpcb *tp = sototcpcb(so);

            tp->t_iobc = *cp;
            tp->t_oobflags |= TCPOOB_HAVEDATA;
            memcpy(sp, cp+1, (unsigned)(m->m_len - cnt - 1));
            m->m_len--;
            return;
        }
        cnt -= m->m_len;
        m = m->m_next; /* XXX WRONG! Fix it! */
        if (m == 0)
            break;
    }
    panic("tcp_pulloutofband");
}
#endif

/*
 * Collect new round-trip time estimate
 * and update averages and current timeout.
 */

void
tcp_xmit_timer(PNATState pData, register struct tcpcb *tp, int rtt)
{
    register short delta;

    LogFlowFunc(("ENTER: tcp_xmit_timer: tp = %R[tcpcb793] rtt = %d\n", tp, rtt));

    tcpstat.tcps_rttupdated++;
    if (tp->t_srtt != 0)
    {
        /*
         * srtt is stored as fixed point with 3 bits after the
         * binary point (i.e., scaled by 8).  The following magic
         * is equivalent to the smoothing algorithm in rfc793 with
         * an alpha of .875 (srtt = rtt/8 + srtt*7/8 in fixed
         * point).  Adjust rtt to origin 0.
         */
        delta = rtt - 1 - (tp->t_srtt >> TCP_RTT_SHIFT);
        if ((tp->t_srtt += delta) <= 0)
            tp->t_srtt = 1;
        /*
         * We accumulate a smoothed rtt variance (actually, a
         * smoothed mean difference), then set the retransmit
         * timer to smoothed rtt + 4 times the smoothed variance.
         * rttvar is stored as fixed point with 2 bits after the
         * binary point (scaled by 4).  The following is
         * equivalent to rfc793 smoothing with an alpha of .75
         * (rttvar = rttvar*3/4 + |delta| / 4).  This replaces
         * rfc793's wired-in beta.
         */
        if (delta < 0)
            delta = -delta;
        delta -= (tp->t_rttvar >> TCP_RTTVAR_SHIFT);
        if ((tp->t_rttvar += delta) <= 0)
            tp->t_rttvar = 1;
    }
    else
    {
        /*
         * No rtt measurement yet - use the unsmoothed rtt.
         * Set the variance to half the rtt (so our first
         * retransmit happens at 3*rtt).
         */
        tp->t_srtt = rtt << TCP_RTT_SHIFT;
        tp->t_rttvar = rtt << (TCP_RTTVAR_SHIFT - 1);
    }
    tp->t_rtt = 0;
    tp->t_rxtshift = 0;

    /*
     * the retransmit should happen at rtt + 4 * rttvar.
     * Because of the way we do the smoothing, srtt and rttvar
     * will each average +1/2 tick of bias.  When we compute
     * the retransmit timer, we want 1/2 tick of rounding and
     * 1 extra tick because of +-1/2 tick uncertainty in the
     * firing of the timer.  The bias will give us exactly the
     * 1.5 tick we need.  But, because the bias is
     * statistical, we have to test that we don't drop below
     * the minimum feasible timer (which is 2 ticks).
     */
    TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp),
                  (short)tp->t_rttmin, TCPTV_REXMTMAX); /* XXX */

    /*
     * We received an ack for a packet that wasn't retransmitted;
     * it is probably safe to discard any error indications we've
     * received recently.  This isn't quite right, but close enough
     * for now (a route might have failed after we sent a segment,
     * and the return path might not be symmetrical).
     */
    tp->t_softerror = 0;
}

/*
 * Determine a reasonable value for maxseg size.
 * If the route is known, check route for mtu.
 * If none, use an mss that can be handled on the outgoing
 * interface without forcing IP to fragment; if bigger than
 * an mbuf cluster (MCLBYTES), round down to nearest multiple of MCLBYTES
 * to utilize large mbufs.  If no route is found, route has no mtu,
 * or the destination isn't local, use a default, hopefully conservative
 * size (usually 512 or the default IP max size, but no more than the mtu
 * of the interface), as we can't discover anything about intervening
 * gateways or networks.  We also initialize the congestion/slow start
 * window to be a single segment if the destination isn't local.
 * While looking at the routing entry, we also initialize other path-dependent
 * parameters from pre-set or cached values in the routing entry.
 */

int
tcp_mss(PNATState pData, register struct tcpcb *tp, u_int offer)
{
    struct socket *so = tp->t_socket;
    int mss;

    LogFlowFunc(("ENTER: tcp_mss: offer=%u, t_maxseg=%u; tp=%R[natsock]\n",
                 offer, (unsigned int)tp->t_maxseg, so));

    mss = min(if_mtu, if_mru) - sizeof(struct tcpiphdr);
    if (offer)
        mss = min(mss, offer);
    mss = max(mss, 32);
    if (mss < tp->t_maxseg || offer != 0)
        tp->t_maxseg = mss;

    tp->snd_cwnd = mss;

    sbreserve(pData, &so->so_snd, tcp_sndspace+((tcp_sndspace%mss)?(mss-(tcp_sndspace%mss)):0));
    sbreserve(pData, &so->so_rcv, tcp_rcvspace+((tcp_rcvspace%mss)?(mss-(tcp_rcvspace%mss)):0));

    LogFlowFunc(("LEAVE: mss=%d\n", mss));
    return mss;
}
