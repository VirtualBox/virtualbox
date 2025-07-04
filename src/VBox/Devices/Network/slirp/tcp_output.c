/* $Id: tcp_output.c 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * NAT - TCP output.
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
 *      @(#)tcp_output.c        8.3 (Berkeley) 12/30/93
 * tcp_output.c,v 1.3 1994/09/15 10:36:55 davidg Exp
 */

/*
 * Changes and additions relating to SLiRP
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>

/*
 * Since this is only used in "stats socket", we give meaning
 * names instead of the REAL names
 */
const char * const tcpstates[] =
{
/*  "CLOSED",       "LISTEN",       "SYN_SENT",     "SYN_RCVD", */
    "REDIRECT",     "LISTEN",       "SYN_SENT",     "SYN_RCVD",
    "ESTABLISHED",  "CLOSE_WAIT",   "FIN_WAIT_1",   "CLOSING",
    "LAST_ACK",     "FIN_WAIT_2",   "TIME_WAIT",
};

static const u_char  tcp_outflags[TCP_NSTATES] =
{
    TH_RST|TH_ACK, 0,      TH_SYN,        TH_SYN|TH_ACK,
    TH_ACK,        TH_ACK, TH_FIN|TH_ACK, TH_FIN|TH_ACK,
    TH_FIN|TH_ACK, TH_ACK, TH_ACK,
};


#define MAX_TCPOPTLEN   32      /* max # bytes that go in options */

/*
 * Tcp output routine: figure out what should be sent and send it.
 */
int
tcp_output(PNATState pData, register struct tcpcb *tp)
{
    register struct socket *so = tp->t_socket;
    register long len, win;
    int off, flags, error;
    register struct mbuf *m = NULL;
    register struct tcpiphdr *ti;
    u_char opt[MAX_TCPOPTLEN];
    unsigned optlen, hdrlen;
    int idle, sendalot;
    int size = 0;

    LogFlowFunc(("ENTER: tcp_output: tp = %R[tcpcb793]\n", tp));

    /*
     * Determine length of data that should be transmitted,
     * and flags that will be used.
     * If there is some data or critical controls (SYN, RST)
     * to send, then transmit; otherwise, investigate further.
     */
    idle = (tp->snd_max == tp->snd_una);
    if (idle && tp->t_idle >= tp->t_rxtcur)
        /*
         * We have been idle for "a while" and no acks are
         * expected to clock out any data we send --
         * slow start to get ack "clock" running again.
         */
        tp->snd_cwnd = tp->t_maxseg;

again:
    sendalot = 0;
    off = tp->snd_nxt - tp->snd_una;
    win = min(tp->snd_wnd, tp->snd_cwnd);

    flags = tcp_outflags[tp->t_state];

    Log2((" --- tcp_output flags = 0x%x\n", flags));

    /*
     * If in persist timeout with window of 0, send 1 byte.
     * Otherwise, if window is small but nonzero
     * and timer expired, we will send what we can
     * and go to transmit state.
     */
    if (tp->t_force)
    {
        if (win == 0)
        {
            /*
             * If we still have some data to send, then
             * clear the FIN bit.  Usually this would
             * happen below when it realizes that we
             * aren't sending all the data.  However,
             * if we have exactly 1 byte of unset data,
             * then it won't clear the FIN bit below,
             * and if we are in persist state, we wind
             * up sending the packet without recording
             * that we sent the FIN bit.
             *
             * We can't just blindly clear the FIN bit,
             * because if we don't have any more data
             * to send then the probe will be the FIN
             * itself.
             */
            if (off < SBUF_LEN(&so->so_snd))
                flags &= ~TH_FIN;
            win = 1;
        }
        else
        {
            tp->t_timer[TCPT_PERSIST] = 0;
            tp->t_rxtshift = 0;
        }
    }

    len = min(SBUF_LEN(&so->so_snd), win) - off;
    if (len < 0)
    {
        /*
         * If FIN has been sent but not acked,
         * but we haven't been called to retransmit,
         * len will be -1.  Otherwise, window shrank
         * after we sent into it.  If window shrank to 0,
         * cancel pending retransmit and pull snd_nxt
         * back to (closed) window.  We will enter persist
         * state below.  If the window didn't close completely,
         * just wait for an ACK.
         */
        len = 0;
        if (win == 0)
        {
            tp->t_timer[TCPT_REXMT] = 0;
            tp->snd_nxt = tp->snd_una;
        }
    }
    if (len > tp->t_maxseg)
    {
        len = tp->t_maxseg;
        sendalot = 1;
    }
    if (SEQ_LT(tp->snd_nxt + len, tp->snd_una + SBUF_LEN(&so->so_snd)))
        flags &= ~TH_FIN;

    win = sbspace(&so->so_rcv);

    /*
     * Sender silly window avoidance.  If connection is idle
     * and can send all data, a maximum segment,
     * at least a maximum default-size segment do it,
     * or are forced, do it; otherwise don't bother.
     * If peer's buffer is tiny, then send
     * when window is at least half open.
     * If retransmitting (possibly after persist timer forced us
     * to send into a small window), then must resend.
     */
    if (len)
    {
        if (len == tp->t_maxseg)
            goto send;
        if ((1 || idle || tp->t_flags & TF_NODELAY) &&
                len + off >= SBUF_LEN(&so->so_snd))
            goto send;
        if (tp->t_force)
            goto send;
        if (len >= tp->max_sndwnd / 2 && tp->max_sndwnd > 0)
            goto send;
        if (SEQ_LT(tp->snd_nxt, tp->snd_max))
            goto send;
    }

    /*
     * Compare available window to amount of window
     * known to peer (as advertised window less
     * next expected input).  If the difference is at least two
     * max size segments, or at least 50% of the maximum possible
     * window, then want to send a window update to peer.
     */
    if (win > 0)
    {
        /*
         * "adv" is the amount we can increase the window,
         * taking into account that we are limited by
         * TCP_MAXWIN << tp->rcv_scale.
         */
        long adv = min(win, (long)TCP_MAXWIN << tp->rcv_scale);
        if (SEQ_GT(tp->rcv_adv, tp->rcv_nxt))
            adv -= tp->rcv_adv - tp->rcv_nxt;

        if (adv >= (long) (2 * tp->t_maxseg))
            goto send;
        if (2 * adv >= (long) SBUF_SIZE(&so->so_rcv))
            goto send;
    }

    /*
     * Send if we owe peer an ACK.
     */
    if (tp->t_flags & TF_ACKNOW)
        goto send;
    if (flags & (TH_SYN|TH_RST))
        goto send;
    if (SEQ_GT(tp->snd_up, tp->snd_una))
        goto send;
    /*
     * If our state indicates that FIN should be sent
     * and we have not yet done so, or we're retransmitting the FIN,
     * then we need to send.
     */
    if (   flags & TH_FIN
        && ((tp->t_flags & TF_SENTFIN) == 0 || tp->snd_nxt == tp->snd_una))
        goto send;

    /*
     * TCP window updates are not reliable, rather a polling protocol
     * using ``persist'' packets is used to insure receipt of window
     * updates.  The three ``states'' for the output side are:
     *      idle                    not doing retransmits or persists
     *      persisting              to move a small or zero window
     *      (re)transmitting        and thereby not persisting
     *
     * tp->t_timer[TCPT_PERSIST]
     *      is set when we are in persist state.
     * tp->t_force
     *      is set when we are called to send a persist packet.
     * tp->t_timer[TCPT_REXMT]
     *      is set when we are retransmitting
     * The output side is idle when both timers are zero.
     *
     * If send window is too small, there is data to transmit, and no
     * retransmit or persist is pending, then go to persist state.
     * If nothing happens soon, send when timer expires:
     * if window is nonzero, transmit what we can,
     * otherwise force out a byte.
     */
    if (   SBUF_LEN(&so->so_snd)
        && tp->t_timer[TCPT_REXMT] == 0
        && tp->t_timer[TCPT_PERSIST] == 0)
    {
        tp->t_rxtshift = 0;
        tcp_setpersist(tp);
    }

    /*
     * No reason to send a segment, just return.
     */
    tcpstat.tcps_didnuttin++;

    LogFlowFuncLeave();
    return (0);

send:
    LogFlowFunc(("send\n"));
    /*
     * Before ESTABLISHED, force sending of initial options
     * unless TCP set not to do any options.
     * NOTE: we assume that the IP/TCP header plus TCP options
     * always fit in a single mbuf, leaving room for a maximum
     * link header, i.e.
     *      max_linkhdr + sizeof (struct tcpiphdr) + optlen <= MHLEN
     */
    optlen = 0;
    hdrlen = sizeof (struct tcpiphdr);
    if (flags & TH_SYN)
    {
        tp->snd_nxt = tp->iss;
        if ((tp->t_flags & TF_NOOPT) == 0)
        {
            u_int16_t mss;

            opt[0] = TCPOPT_MAXSEG;
            opt[1] = 4;
            mss = RT_H2N_U16((u_int16_t) tcp_mss(pData, tp, 0));
            memcpy((caddr_t)(opt + 2), (caddr_t)&mss, sizeof(mss));
            optlen = 4;

#if 0
            if (   (tp->t_flags & TF_REQ_SCALE)
                && (   (flags & TH_ACK) == 0
                    || (tp->t_flags & TF_RCVD_SCALE)))
            {
                *((u_int32_t *) (opt + optlen)) = RT_H2N_U32(  TCPOPT_NOP << 24
                                                             | TCPOPT_WINDOW << 16
                                                             | TCPOLEN_WINDOW << 8
                                                             | tp->request_r_scale);
                optlen += 4;
            }
#endif
        }
    }

    /*
     * Send a timestamp and echo-reply if this is a SYN and our side
     * wants to use timestamps (TF_REQ_TSTMP is set) or both our side
     * and our peer have sent timestamps in our SYN's.
     */
#if 0
    if (   (tp->t_flags & (TF_REQ_TSTMP|TF_NOOPT)) == TF_REQ_TSTMP
        && (flags & TH_RST) == 0
        && (   (flags & (TH_SYN|TH_ACK)) == TH_SYN
            || (tp->t_flags & TF_RCVD_TSTMP)))
    {
        u_int32_t *lp = (u_int32_t *)(opt + optlen);

        /* Form timestamp option as shown in appendix A of RFC 1323. */
        *lp++ = RT_H2N_U32_C(TCPOPT_TSTAMP_HDR);
        *lp++ = RT_H2N_U32(tcp_now);
        *lp   = RT_H2N_U32(tp->ts_recent);
        optlen += TCPOLEN_TSTAMP_APPA;
    }
#endif
    hdrlen += optlen;

    /*
     * Adjust data length if insertion of options will
     * bump the packet length beyond the t_maxseg length.
     */
    if (len > tp->t_maxseg - optlen)
    {
        len = tp->t_maxseg - optlen;
        sendalot = 1;
    }

    /*
     * Grab a header mbuf, attaching a copy of data to
     * be transmitted, and initialize the header from
     * the template for sends on this connection.
     */
    if (len)
    {
        if (tp->t_force && len == 1)
            tcpstat.tcps_sndprobe++;
        else if (SEQ_LT(tp->snd_nxt, tp->snd_max))
        {
            tcpstat.tcps_sndrexmitpack++;
            tcpstat.tcps_sndrexmitbyte += len;
        }
        else
        {
            tcpstat.tcps_sndpack++;
            tcpstat.tcps_sndbyte += len;
        }

        size = MCLBYTES;
        if ((len + hdrlen + ETH_HLEN) < MSIZE)
            size = MCLBYTES;
        else if ((len + hdrlen + ETH_HLEN) < MCLBYTES)
            size = MCLBYTES;
        else if((len + hdrlen + ETH_HLEN) < MJUM9BYTES)
            size = MJUM9BYTES;
        else if ((len + hdrlen + ETH_HLEN) < MJUM16BYTES)
            size = MJUM16BYTES;
        else
            AssertMsgFailed(("Unsupported size"));
        m = m_getjcl(pData, M_NOWAIT, MT_HEADER, M_PKTHDR, size);
        if (m == NULL)
        {
/*          error = ENOBUFS; */
            error = 1;
            goto out;
        }
        m->m_data += if_maxlinkhdr;
        m->m_pkthdr.header = mtod(m, void *);
        m->m_len = hdrlen;

        /*
         * This will always succeed, since we make sure our mbufs
         * are big enough to hold one MSS packet + header + ... etc.
         */
#if 0
        if (len <= MHLEN - hdrlen - max_linkhdr)
        {
#endif
            sbcopy(&so->so_snd, off, (int) len, mtod(m, caddr_t) + hdrlen);
            m->m_len += len;
#if 0
        }
        else
        {
            m->m_next = m_copy(so->so_snd.sb_mb, off, (int) len);
            if (m->m_next == 0)
                len = 0;
        }
#endif
        /*
         * If we're sending everything we've got, set PUSH.
         * (This will keep happy those implementations which only
         * give data to the user when a buffer fills or
         * a PUSH comes in.)
         */
        if (off + len == (ssize_t)SBUF_LEN(&so->so_snd))
            flags |= TH_PUSH;
    }
    else
    {
        bool fUninitializedTemplate = false;
        if (tp->t_flags & TF_ACKNOW)
            tcpstat.tcps_sndacks++;
        else if (flags & (TH_SYN|TH_FIN|TH_RST))
            tcpstat.tcps_sndctrl++;
        else if (SEQ_GT(tp->snd_up, tp->snd_una))
            tcpstat.tcps_sndurg++;
        else
            tcpstat.tcps_sndwinup++;

        if ((hdrlen + ETH_HLEN) < MSIZE)
        {
            size = MCLBYTES;
        }
        else if ((hdrlen + ETH_HLEN) < MCLBYTES)
        {
            size = MCLBYTES;
        }
        else if((hdrlen + ETH_HLEN) < MJUM9BYTES)
        {
            size = MJUM9BYTES;
        }
        else if ((hdrlen + ETH_HLEN) < MJUM16BYTES)
        {
            size = MJUM16BYTES;
        }
        else
        {
            AssertMsgFailed(("Unsupported size"));
        }
        m = m_getjcl(pData, M_NOWAIT, MT_HEADER, M_PKTHDR, size);
        if (m == NULL)
        {
/*          error = ENOBUFS; */
            error = 1;
            goto out;
        }
        m->m_data += if_maxlinkhdr;
        m->m_pkthdr.header = mtod(m, void *);
        m->m_len = hdrlen;
        /*
         * Uninitialized TCP template looks very suspicious at this processing state, thus why we have
         * to workaround the problem till right fix. Warning appears once at release log.
         */
        fUninitializedTemplate = RT_BOOL((   tp->t_template.ti_src.s_addr == INADDR_ANY
                                          || tp->t_template.ti_dst.s_addr == INADDR_ANY));
#ifndef DEBUG_vvl
        if (fUninitializedTemplate)
        {
            static bool fWarn;
            tcp_template(tp);
            if(!fWarn)
            {
                LogRel(("NAT: TCP: TCP template was created forcely from socket information\n"));
                fWarn = true;
            }
        }
#else
        Assert((!fUninitializedTemplate));
#endif
    }

    ti = mtod(m, struct tcpiphdr *);

    memcpy((caddr_t)ti, &tp->t_template, sizeof (struct tcpiphdr));

    /*
     * Fill in fields, remembering maximum advertised
     * window for use in delaying messages about window sizes.
     * If resending a FIN, be sure not to use a new sequence number.
     */
    if (   flags & TH_FIN
        && tp->t_flags & TF_SENTFIN
        && tp->snd_nxt == tp->snd_max)
        tp->snd_nxt--;
    /*
     * If we are doing retransmissions, then snd_nxt will
     * not reflect the first unsent octet.  For ACK only
     * packets, we do not want the sequence number of the
     * retransmitted packet, we want the sequence number
     * of the next unsent octet.  So, if there is no data
     * (and no SYN or FIN), use snd_max instead of snd_nxt
     * when filling in ti_seq.  But if we are in persist
     * state, snd_max might reflect one byte beyond the
     * right edge of the window, so use snd_nxt in that
     * case, since we know we aren't doing a retransmission.
     * (retransmit and persist are mutually exclusive...)
     */
    if (len || (flags & (TH_SYN|TH_FIN)) || tp->t_timer[TCPT_PERSIST])
        ti->ti_seq = RT_H2N_U32(tp->snd_nxt);
    else
        ti->ti_seq = RT_H2N_U32(tp->snd_max);
    ti->ti_ack = RT_H2N_U32(tp->rcv_nxt);
    if (optlen)
    {
        memcpy((caddr_t)(ti + 1), (caddr_t)opt, optlen);
        ti->ti_off = (uint8_t)((sizeof (struct tcphdr) + optlen) >> 2);
    }
    ti->ti_flags = flags;
    /*
     * Calculate receive window.  Don't shrink window,
     * but avoid silly window syndrome.
     */
    if (win < (long)(SBUF_SIZE(&so->so_rcv) / 4) && win < (long)tp->t_maxseg)
        win = 0;
    if (win > (long)TCP_MAXWIN << tp->rcv_scale)
        win = (long)TCP_MAXWIN << tp->rcv_scale;
    if (win < (long)(int32_t)(tp->rcv_adv - tp->rcv_nxt))
        win = (long)(int32_t)(tp->rcv_adv - tp->rcv_nxt);
    ti->ti_win = RT_H2N_U16((u_int16_t) (win>>tp->rcv_scale));

#if 0
    if (SEQ_GT(tp->snd_up, tp->snd_nxt))
    {
        ti->ti_urp = RT_H2N_U16((u_int16_t)(tp->snd_up - tp->snd_nxt));
#else
    if (SEQ_GT(tp->snd_up, tp->snd_una))
    {
        ti->ti_urp = RT_H2N_U16((u_int16_t)(tp->snd_up - RT_N2H_U32(ti->ti_seq)));
#endif
        ti->ti_flags |= TH_URG;
    }
    else
        /*
         * If no urgent pointer to send, then we pull
         * the urgent pointer to the left edge of the send window
         * so that it doesn't drift into the send window on sequence
         * number wraparound.
         */
        tp->snd_up = tp->snd_una;               /* drag it along */

    /*
     * Put TCP length in extended header, and then
     * checksum extended header and data.
     */
    if (len + optlen)
        ti->ti_len = RT_H2N_U16((u_int16_t)(sizeof (struct tcphdr)
                                            + optlen + len));
    ti->ti_sum = cksum(m, (int)(hdrlen + len));

    /*
     * In transmit state, time the transmission and arrange for
     * the retransmit.  In persist state, just set snd_max.
     */
    if (tp->t_force == 0 || tp->t_timer[TCPT_PERSIST] == 0)
    {
        tcp_seq startseq = tp->snd_nxt;

        /*
         * Advance snd_nxt over sequence space of this segment.
         */
        if (flags & (TH_SYN|TH_FIN))
        {
            if (flags & TH_SYN)
                tp->snd_nxt++;
            if (flags & TH_FIN)
            {
                tp->snd_nxt++;
                tp->t_flags |= TF_SENTFIN;
            }
        }
        tp->snd_nxt += len;
        if (SEQ_GT(tp->snd_nxt, tp->snd_max))
        {
            tp->snd_max = tp->snd_nxt;
            /*
             * Time this transmission if not a retransmission and
             * not currently timing anything.
             */
            if (tp->t_rtt == 0)
            {
                tp->t_rtt = 1;
                tp->t_rtseq = startseq;
                tcpstat.tcps_segstimed++;
            }
        }

        /*
         * Set retransmit timer if not currently set,
         * and not doing an ack or a keep-alive probe.
         * Initial value for retransmit timer is smoothed
         * round-trip time + 2 * round-trip time variance.
         * Initialize shift counter which is used for backoff
         * of retransmit time.
         */
        if (   tp->t_timer[TCPT_REXMT] == 0
            && tp->snd_nxt != tp->snd_una)
        {
            tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;
            if (tp->t_timer[TCPT_PERSIST])
            {
                tp->t_timer[TCPT_PERSIST] = 0;
                tp->t_rxtshift = 0;
            }
        }
    }
    else
        if (SEQ_GT(tp->snd_nxt + len, tp->snd_max))
            tp->snd_max = tp->snd_nxt + len;

    /*
     * Fill in IP length and desired time to live and
     * send to IP level.  There should be a better way
     * to handle ttl and tos; we could keep them in
     * the template, but need a way to checksum without them.
     */
    M_ASSERTPKTHDR(m);
    m->m_pkthdr.header = mtod(m, void *);
    m->m_len = hdrlen + len; /* XXX Needed? m_len should be correct */

    {
        ((struct ip *)ti)->ip_len = m->m_len;
        ((struct ip *)ti)->ip_ttl = ip_defttl;
       ((struct ip *)ti)->ip_tos = so->so_iptos;

        /* #if BSD >= 43 */
        /* Don't do IP options... */
#if 0
        error = ip_output(m, tp->t_inpcb->inp_options, &tp->t_inpcb->inp_route,
                         so->so_options & SO_DONTROUTE, 0);
#endif
        error = ip_output(pData, so, m);

#if 0
/* #else */
        error = ip_output(m, (struct mbuf *)0, &tp->t_inpcb->inp_route,
                          so->so_options & SO_DONTROUTE);
/* #endif */
#endif
    }
    if (error)
    {
out:
#if 0
        if (error == ENOBUFS)
        {
            tcp_quench(tp->t_inpcb, 0);
            return (0);
        }

        if (  (   error == EHOSTUNREACH
               || error == ENETDOWN)
            && TCPS_HAVERCVDSYN(tp->t_state))
        {
            tp->t_softerror = error;
            return (0);
        }
#endif
        if (m != NULL)
            m_freem(pData, m);
        return (error);
    }
    tcpstat.tcps_sndtotal++;

    /*
     * Data sent (as far as we can tell).
     * If this advertises a larger window than any other segment,
     * then remember the size of the advertised window.
     * Any pending ACK has now been sent.
     */
    if (win > 0 && SEQ_GT(tp->rcv_nxt+win, tp->rcv_adv))
        tp->rcv_adv = tp->rcv_nxt + win;
    tp->last_ack_sent = tp->rcv_nxt;
    tp->t_flags &= ~(TF_ACKNOW|TF_DELACK);
    if (sendalot)
        goto again;

    return (0);
}

void
tcp_setpersist(struct tcpcb *tp)
{
    int t = ((tp->t_srtt >> 2) + tp->t_rttvar) >> 1;

#if 0
    if (tp->t_timer[TCPT_REXMT])
        panic("tcp_output REXMT");
#endif
    /*
     * Start/restart persistence timer.
     */
    TCPT_RANGESET(tp->t_timer[TCPT_PERSIST],
                  t * tcp_backoff[tp->t_rxtshift],
                  TCPTV_PERSMIN, TCPTV_PERSMAX);
    if (tp->t_rxtshift < TCP_MAXRXTSHIFT)
        tp->t_rxtshift++;
}
