/* $Id: tcp_subr.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - TCP support.
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
 *      @(#)tcp_subr.c  8.1 (Berkeley) 6/10/93
 * tcp_subr.c,v 1.5 1994/10/08 22:39:58 phk Exp
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
 * Tcp initialization
 */
void
tcp_init(PNATState pData)
{
    tcp_iss = 1;            /* wrong */
    tcb.so_next = tcb.so_prev = &tcb;
    tcp_last_so = &tcb;
    tcp_reass_maxqlen = 48;
    tcp_reass_maxseg  = 256;
}

/*
 * Create template to be used to send tcp packets on a connection.
 * Call after host entry created, fills
 * in a skeletal tcp/ip header, minimizing the amount of work
 * necessary when the connection is used.
 */
/* struct tcpiphdr * */
void
tcp_template(struct tcpcb *tp)
{
    struct socket *so = tp->t_socket;
    register struct tcpiphdr *n = &tp->t_template;

    memset(n->ti_x1, 0, 9);
    n->ti_pr = IPPROTO_TCP;
    n->ti_len = RT_H2N_U16(sizeof (struct tcpiphdr) - sizeof (struct ip));
    n->ti_src = so->so_faddr;
    n->ti_dst = so->so_laddr;
    n->ti_sport = so->so_fport;
    n->ti_dport = so->so_lport;

    n->ti_seq = 0;
    n->ti_ack = 0;
    n->ti_x2 = 0;
    n->ti_off = 5;
    n->ti_flags = 0;
    n->ti_win = 0;
    n->ti_sum = 0;
    n->ti_urp = 0;
}

/*
 * Send a single message to the TCP at address specified by
 * the given TCP/IP header.  If m == 0, then we make a copy
 * of the tcpiphdr at ti and send directly to the addressed host.
 * This is used to force keep alive messages out using the TCP
 * template for a connection tp->t_template.  If flags are given
 * then we send a message back to the TCP which originated the
 * segment ti, and discard the mbuf containing it and any other
 * attached mbufs.
 *
 * In any case the ack and sequence number of the transmitted
 * segment are as specified by the parameters.
 */
void
tcp_respond(PNATState pData, struct tcpcb *tp, struct tcpiphdr *ti, struct mbuf *m, tcp_seq ack, tcp_seq seq, int flags)
{
    register int tlen;

    LogFlowFunc(("ENTER: tp = %R[tcpcb793], ti = %p, m = %p, ack = %u, seq = %u, flags = %x\n", tp, ti, m, ack, seq, flags));

    if (m == 0)
    {
        if ((m = m_gethdr(pData, M_DONTWAIT, MT_HEADER)) == NULL)
            return;
#ifdef TCP_COMPAT_42
        tlen = 1;
#else
        tlen = 0;
#endif
        m->m_data += if_maxlinkhdr;
        m->m_pkthdr.header = mtod(m, void *);
        *mtod(m, struct tcpiphdr *) = *ti;
        ti = mtod(m, struct tcpiphdr *);
        flags = TH_ACK;
    }
    else
    {
        /*
         * ti points into m so the next line is just making
         * the mbuf point to ti
         */
        m->m_data = (caddr_t)ti;

        m->m_len = sizeof (struct tcpiphdr);
        tlen = 0;
#define xchg(a,b,type) { type t; t = a; a = b; b = t; }
        xchg(ti->ti_dst.s_addr, ti->ti_src.s_addr, u_int32_t);
        xchg(ti->ti_dport, ti->ti_sport, u_int16_t);
#undef xchg
    }
    ti->ti_len = RT_H2N_U16((u_short)(sizeof (struct tcphdr) + tlen));
    tlen += sizeof (struct tcpiphdr);
    m->m_len = tlen;

    memset(ti->ti_x1, 0, 9);
    ti->ti_seq = RT_H2N_U32(seq);
    ti->ti_ack = RT_H2N_U32(ack);
    ti->ti_x2 = 0;
    ti->ti_off = sizeof (struct tcphdr) >> 2;
    ti->ti_flags = flags;
    if (tp)
    {
        int win = sbspace(&tp->t_socket->so_rcv);
        ti->ti_win = RT_H2N_U16((u_int16_t) (win >> tp->rcv_scale));
    }
    else
        ti->ti_win = 0;
    ti->ti_urp = 0;
    ti->ti_sum = 0;
    ti->ti_sum = cksum(m, tlen);
    ((struct ip *)ti)->ip_len = tlen;

    if(flags & TH_RST)
        ((struct ip *)ti)->ip_ttl = MAXTTL;
    else
        ((struct ip *)ti)->ip_ttl = ip_defttl;

    (void) ip_output(pData, (struct socket *)0, m);
}

/*
 * Create a new TCP control block, making an
 * empty reassembly queue and hooking it to the argument
 * protocol control block.
 */
struct tcpcb *
tcp_newtcpcb(PNATState pData, struct socket *so)
{
    register struct tcpcb *tp;

    tp = (struct tcpcb *)RTMemAllocZ(sizeof(*tp));
    if (tp == NULL)
        return ((struct tcpcb *)0);

    tp->t_maxseg = tcp_mssdflt;

    tp->t_flags = tcp_do_rfc1323 ? (TF_REQ_SCALE|TF_REQ_TSTMP) : 0;
    tp->t_socket = so;

    /*
     * Init srtt to TCPTV_SRTTBASE (0), so we can tell that we have no
     * rtt estimate.  Set rttvar so that srtt + 2 * rttvar gives
     * reasonable initial retransmit time.
     */
    tp->t_srtt = TCPTV_SRTTBASE;
    tp->t_rttvar = tcp_rttdflt * PR_SLOWHZ << 2;
    tp->t_rttmin = TCPTV_MIN;

    TCPT_RANGESET(tp->t_rxtcur,
                  ((TCPTV_SRTTBASE >> 2) + (TCPTV_SRTTDFLT << 2)) >> 1,
                  TCPTV_MIN, TCPTV_REXMTMAX);

    tp->snd_cwnd = TCP_MAXWIN << TCP_MAX_WINSHIFT;
    tp->snd_ssthresh = TCP_MAXWIN << TCP_MAX_WINSHIFT;
    TCP_STATE_SWITCH_TO(tp, TCPS_CLOSED);

    so->so_tcpcb = tp;
    so->so_type = IPPROTO_TCP;

    return (tp);
}

/*
 * Drop a TCP connection, reporting
 * the specified error.  If connection is synchronized,
 * then send a RST to peer.
 */
struct tcpcb *tcp_drop(PNATState pData, struct tcpcb *tp, int err)
{
/* tcp_drop(tp, errno)
        register struct tcpcb *tp;
        int errno;
{
*/
    int fUninitializedTemplate = 0;
#ifndef LOG_ENABLED
    NOREF(err);
#endif
    LogFlowFunc(("ENTER: tp = %R[tcpcb793], errno = %d\n", tp, err));
    fUninitializedTemplate = RT_BOOL((   tp
                                      && (  tp->t_template.ti_src.s_addr == INADDR_ANY
                                         || tp->t_template.ti_dst.s_addr == INADDR_ANY)));

    if (   TCPS_HAVERCVDSYN(tp->t_state)
        && !fUninitializedTemplate)
    {
        TCP_STATE_SWITCH_TO(tp, TCPS_CLOSED);
        (void) tcp_output(pData, tp);
        tcpstat.tcps_drops++;
    }
    else
        tcpstat.tcps_conndrops++;
#if 0
      if (errno == ETIMEDOUT && tp->t_softerror)
              errno = tp->t_softerror;

      so->so_error = errno;
#endif
      return (tcp_close(pData, tp));
}

/*
 * Close a TCP control block:
 *      discard all space held by the tcp
 *      discard internet protocol block
 *      wake up any sleepers
 */
struct tcpcb *
tcp_close(PNATState pData, register struct tcpcb *tp)
{
    struct socket *so = tp->t_socket;

    struct tseg_qent *te = NULL;
    LogFlowFunc(("ENTER: tp = %R[tcpcb793]\n", tp));
    /*XXX: freeing the reassembly queue */
    while (!LIST_EMPTY(&tp->t_segq))
    {
        te = LIST_FIRST(&tp->t_segq);
        LIST_REMOVE(te, tqe_q);
        m_freem(pData, te->tqe_m);
        RTMemFree(te);
        tcp_reass_qsize--;
    }
    RTMemFree(tp);
    so->so_tcpcb = 0;
    soisfdisconnected(so);
    /* clobber input socket cache if we're closing the cached connection */
    if (so == tcp_last_so)
        tcp_last_so = &tcb;
    if (so->s != -1)
        closesocket(so->s);
    /* Avoid double free if the socket is listening and therefore doesn't have
     * any sbufs reserved. */
    if (!(so->so_state & SS_FACCEPTCONN))
    {
        sbfree(&so->so_rcv);
        sbfree(&so->so_snd);
    }
    sofree(pData, so);
    SOCKET_UNLOCK(so);
    tcpstat.tcps_closed++;
    return ((struct tcpcb *)0);
}

void
tcp_drain(void)
{
    /* XXX */
}

/*
 * When a source quench is received, close congestion window
 * to one segment.  We will gradually open it again as we proceed.
 */

#if 0

void
tcp_quench(i, int errno)
{
    struct tcpcb *tp = intotcpcb(inp);

    if (tp)
        tp->snd_cwnd = tp->t_maxseg;
}

#endif

/*
 * TCP protocol interface to socket abstraction.
 */

/*
 * User issued close, and wish to trail through shutdown states:
 * if never received SYN, just forget it.  If got a SYN from peer,
 * but haven't sent FIN, then go to FIN_WAIT_1 state to send peer a FIN.
 * If already got a FIN from peer, then almost done; go to LAST_ACK
 * state.  In all other cases, have already sent FIN to peer (e.g.
 * after PRU_SHUTDOWN), and just have to play tedious game waiting
 * for peer to send FIN or not respond to keep-alives, etc.
 * We can let the user exit from the close as soon as the FIN is acked.
 */
void
tcp_sockclosed(PNATState pData, struct tcpcb *tp)
{
    LogFlowFunc(("ENTER: tp = %R[tcpcb793]\n", tp));
    LogFunc(("tp->t_socket:%R[natsock]\n",tp->t_socket));

    switch (tp->t_state)
    {
        case TCPS_CLOSED:
        case TCPS_LISTEN:
        case TCPS_SYN_SENT:
            TCP_STATE_SWITCH_TO(tp, TCPS_CLOSED);
            tp = tcp_close(pData, tp);
            break;

        case TCPS_SYN_RECEIVED:
        case TCPS_ESTABLISHED:
            TCP_STATE_SWITCH_TO(tp, TCPS_FIN_WAIT_1);
            break;

        case TCPS_CLOSE_WAIT:
            TCP_STATE_SWITCH_TO(tp, TCPS_LAST_ACK);
            break;
    }
/*  soisfdisconnecting(tp->t_socket); */
    if (   tp
        && tp->t_state >= TCPS_FIN_WAIT_2)
        soisfdisconnected(tp->t_socket);
    /*
     * (vasily) there're situations when the FIN or FIN,ACK are lost (Windows host)
     * and retransmitting keeps VBox busy on sending closing sequences *very* frequent,
     * easting a lot of CPU. To avoid this we don't sent on sockets marked as closed
     * (see slirp.c for details about setting so_close member).
     */
    if (   tp
        && tp->t_socket
        && !tp->t_socket->so_close)
        tcp_output(pData, tp);
}

/*
 * Connect to a host on the Internet
 * Called by tcp_input
 * Only do a connect, the tcp fields will be set in tcp_input
 * return 0 if there's a result of the connect,
 * else return -1 means we're still connecting
 * The return value is almost always -1 since the socket is
 * nonblocking.  Connect returns after the SYN is sent, and does
 * not wait for ACK+SYN.
 */
int tcp_fconnect(PNATState pData, struct socket *so)
{
    int ret = 0;

    LogFlowFunc(("ENTER: so = %R[natsock]\n", so));

    if ((ret = so->s = socket(AF_INET, SOCK_STREAM, 0)) >= 0)
    {
        int opt, s = so->s;
        struct sockaddr_in addr;

        fd_nonblock(s);

        opt = 1;
        setsockopt(s, SOL_SOCKET, SO_OOBINLINE, (char *)&opt, sizeof(opt));
        opt = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt));

        ret = sobind(pData, so);
        if (ret != 0)
            return ret;

        addr.sin_family = AF_INET;
        if ((so->so_faddr.s_addr & RT_H2N_U32(pData->netmask)) == pData->special_addr.s_addr)
        {
            /* It's an alias */
            switch(RT_N2H_U32(so->so_faddr.s_addr) & ~pData->netmask)
            {
                case CTL_DNS:
                    /*
                     * TCP DNS proxy.  We only support "forwarding" to
                     * single server.  We don't have infrastructure in
                     * place to re-try connections to other servers.
                     */
                    if (   pData->fUseDnsProxy
                        && so->so_fport == RT_H2N_U16_C(53))
                    {
                        struct dns_entry *ns = TAILQ_LAST(&pData->pDnsList, dns_list_head);
                        if (ns != NULL)
                        {
                            addr.sin_addr = ns->de_addr;
                            break;
                        }
                    }
                    RT_FALL_THRU();
                case CTL_ALIAS:
                default:
                    addr.sin_addr = loopback_addr;
                    break;
            }
        }
        else
            addr.sin_addr = so->so_faddr;
        addr.sin_port = so->so_fport;

        Log2(("NAT: tcp connect to %RTnaipv4:%d\n",
              addr.sin_addr.s_addr, RT_N2H_U16(addr.sin_port)));

        ret = connect(s,(struct sockaddr *)&addr,sizeof (addr));

        /*
         * If it's not in progress, it failed, so we just return 0,
         * without clearing SS_NOFDREF
         */
        soisfconnecting(so);
    }

    return(ret);
}

/*
 * Accept the socket and connect to the local-host
 *
 * We have a problem. The correct thing to do would be
 * to first connect to the local-host, and only if the
 * connection is accepted, then do an accept() here.
 * But, a) we need to know who's trying to connect
 * to the socket to be able to SYN the local-host, and
 * b) we are already connected to the foreign host by
 * the time it gets to accept(), so... We simply accept
 * here and SYN the local-host.
 */
void
tcp_connect(PNATState pData, struct socket *inso)
{
    struct socket *so;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    struct tcpcb *tp;
    int s, opt;
    int status;
    socklen_t optlen;
    static int cVerbose = 1;

    LogFlowFunc(("ENTER: inso = %R[natsock]\n", inso));

    if (   inso->so_laddr.s_addr == INADDR_ANY /* delayed port-forwarding? */
        && pData->guest_addr_guess.s_addr == INADDR_ANY)
    {
        LogRel2(("NAT: Port-forward: guest address unknown for %R[natsock]\n", inso));
        closesocket(accept(inso->s, NULL, NULL));
        if (inso->so_state & SS_FACCEPTONCE)
            tcp_close(pData, sototcpcb(inso));
        return;
    }

    /*
     * If it's an SS_ACCEPTONCE socket, no need to socreate()
     * another socket, just use the accept() socket.
     */
    if (inso->so_state & SS_FACCEPTONCE)
    {
        /* FACCEPTONCE already have a tcpcb */
        so = inso;
    }
    else
    {
        if ((so = socreate()) == NULL)
        {
            /* If it failed, get rid of the pending connection */
            closesocket(accept(inso->s,(struct sockaddr *)&addr,&addrlen));
            return;
        }
        if (tcp_attach(pData, so) < 0)
        {
            RTMemFree(so); /* NOT sofree */
            return;
        }
        so->so_laddr = inso->so_laddr;
        so->so_lport = inso->so_lport;
    }

    if (so->so_laddr.s_addr == INADDR_ANY)
    {
        LogRel2(("NAT: Port-forward: using %RTnaipv4 for %R[natsock]\n",
                 pData->guest_addr_guess.s_addr, inso));
        so->so_laddr = pData->guest_addr_guess;
    }

    (void) tcp_mss(pData, sototcpcb(so), 0);

    fd_nonblock(inso->s);
    if ((s = accept(inso->s,(struct sockaddr *)&addr,&addrlen)) < 0)
    {
        tcp_close(pData, sototcpcb(so)); /* This will sofree() as well */
        return;
    }
    fd_nonblock(s);
    opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,(char *)&opt, sizeof(int));
    opt = 1;
    setsockopt(s, SOL_SOCKET, SO_OOBINLINE,(char *)&opt, sizeof(int));
    opt = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,(char *)&opt, sizeof(int));

    optlen = sizeof(int);
    status = getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&opt, &optlen);
    if (status < 0)
    {
        LogRel(("NAT: Error(%d) while getting RCV capacity\n", errno));
        goto no_sockopt;
    }
    if (cVerbose > 0)
        LogRel(("NAT: Old socket recv size: %dKB\n", opt / 1024));
    /** @todo (r-vvl) make it configurable (via extra data) */
    opt = pData->socket_rcv;
    status = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&opt, sizeof(int));
    if (status < 0)
    {
        LogRel(("NAT: Error(%d) while setting RCV capacity to (%d)\n", errno, opt));
        goto no_sockopt;
    }
    optlen = sizeof(int);
    status = getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&opt, &optlen);
    if (status < 0)
    {
        LogRel(("NAT: Error(%d) while getting SND capacity\n", errno));
        goto no_sockopt;
    }
    if (cVerbose > 0)
        LogRel(("NAT: Old socket send size: %dKB\n", opt / 1024));
    opt = pData->socket_rcv;
    status = setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&opt, sizeof(int));
    if (status < 0)
    {
        LogRel(("NAT: Error(%d) while setting SND capacity to (%d)\n", errno, opt));
        goto no_sockopt;
    }
    if (cVerbose > 0)
        cVerbose--;

 no_sockopt:
    so->so_fport = addr.sin_port;
    so->so_faddr = addr.sin_addr;
    /* Translate connections from localhost to the real hostname */
    if (so->so_faddr.s_addr == 0 || so->so_faddr.s_addr == loopback_addr.s_addr)
        so->so_faddr = alias_addr;

    /* Close the accept() socket, set right state */
    if (inso->so_state & SS_FACCEPTONCE)
    {
        closesocket(so->s);        /* If we only accept once, close the accept() socket */
        so->so_state = SS_NOFDREF; /* Don't select it yet, even though we have an FD */
                                   /* if it's not FACCEPTONCE, it's already NOFDREF */
    }
    so->s = s;

    tp = sototcpcb(so);

    tcp_template(tp);

    /* Compute window scaling to request.  */
/*  while (tp->request_r_scale < TCP_MAX_WINSHIFT
 *         && (TCP_MAXWIN << tp->request_r_scale) < so->so_rcv.sb_hiwat)
 *      tp->request_r_scale++;
 */

/*  soisconnecting(so); */ /* NOFDREF used instead */
    tcpstat.tcps_connattempt++;

    TCP_STATE_SWITCH_TO(tp, TCPS_SYN_SENT);
    tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
    tp->iss = tcp_iss;
    tcp_iss += TCP_ISSINCR/2;
    tcp_sendseqinit(tp);
    tcp_output(pData, tp);
}

/*
 * Attach a TCPCB to a socket.
 */
int
tcp_attach(PNATState pData, struct socket *so)
{
    /* We're attaching already attached socket??? */
    Assert(so->so_type == 0);
    if ((so->so_tcpcb = tcp_newtcpcb(pData, so)) == NULL)
        return -1;

    SOCKET_LOCK_CREATE(so);
    QSOCKET_LOCK(tcb);
    insque(pData, so, &tcb);
    NSOCK_INC();
    QSOCKET_UNLOCK(tcb);
    return 0;
}
