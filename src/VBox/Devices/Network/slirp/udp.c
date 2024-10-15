/* $Id: udp.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - UDP protocol.
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
 *      @(#)udp_usrreq.c        8.4 (Berkeley) 1/21/94
 * udp_usrreq.c,v 1.4 1994/10/02 17:48:45 phk Exp
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


/*
 * UDP protocol implementation.
 * Per RFC 768, August, 1980.
 */
#define udpcksum 1

void
udp_init(PNATState pData)
{
    udp_last_so = &udb;
    udb.so_next = udb.so_prev = &udb;
}

/* m->m_data  points at ip packet header
 * m->m_len   length ip packet
 * ip->ip_len length data (IPDU)
 */
void
udp_input(PNATState pData, register struct mbuf *m, int iphlen)
{
    register struct ip *ip;
    register struct udphdr *uh;
    int len;
    struct ip save_ip;
    struct socket *so;
    int ret;
    int ttl, tos;

    LogFlowFunc(("ENTER: m = %p, iphlen = %d\n", m, iphlen));
    ip = mtod(m, struct ip *);
    Log2(("%RTnaipv4 iphlen = %d\n", ip->ip_dst, iphlen));

    udpstat.udps_ipackets++;

    /*
     * Strip IP options, if any; should skip this,
     * make available to user, and use on returned packets,
     * but we don't yet have a way to check the checksum
     * with options still present.
     */
    if (iphlen > sizeof(struct ip))
    {
        ip_stripoptions(m, (struct mbuf *)0);
        iphlen = sizeof(struct ip);
    }

    /*
     * Get IP and UDP header together in first mbuf.
     */
    ip = mtod(m, struct ip *);
    uh = (struct udphdr *)((caddr_t)ip + iphlen);

    /*
     * Make mbuf data length reflect UDP length.
     * If not enough data to reflect UDP length, drop.
     */
    len = RT_N2H_U16((u_int16_t)uh->uh_ulen);
    Assert(ip->ip_len + iphlen == (ssize_t)m_length(m, NULL));

    if (ip->ip_len != len)
    {
        if (len > ip->ip_len)
        {
            udpstat.udps_badlen++;
            Log3(("NAT: IP(id: %hd) has bad size\n", ip->ip_id));
            goto bad_free_mbuf;
        }
        m_adj(m, len - ip->ip_len);
        ip->ip_len = len;
    }

    /*
     * Save a copy of the IP header in case we want restore it
     * for sending an ICMP error message in response.
     */
    save_ip = *ip;
    save_ip.ip_len+= iphlen;         /* tcp_input subtracts this */

    /*
     * Checksum extended UDP header and data.
     */
    if (udpcksum && uh->uh_sum)
    {
        memset(((struct ipovly *)ip)->ih_x1, 0, 9);
        ((struct ipovly *)ip)->ih_len = uh->uh_ulen;
#if 0
        /* keep uh_sum for ICMP reply */
        uh->uh_sum = cksum(m, len + sizeof (struct ip));
        if (uh->uh_sum)
        {

#endif
            if (cksum(m, len + iphlen))
            {
                udpstat.udps_badsum++;
                Log3(("NAT: IP(id: %hd) has bad (udp) cksum\n", ip->ip_id));
                goto bad_free_mbuf;
            }
        }
#if 0
    }
#endif

    /*
     *  handle DHCP/BOOTP
     */
    if (uh->uh_dport == RT_H2N_U16_C(BOOTP_SERVER))
    {
        bootp_input(pData, m);
        goto done_free_mbuf;
    }

    LogFunc(("uh src: %RTnaipv4:%d, dst: %RTnaipv4:%d\n",
             ip->ip_src.s_addr, RT_N2H_U16(uh->uh_sport),
             ip->ip_dst.s_addr, RT_N2H_U16(uh->uh_dport)));

    /*
     * handle DNS host resolver without creating a socket
     */
    if (   pData->fUseHostResolver
        && uh->uh_dport == RT_H2N_U16_C(53)
        && CTL_CHECK(ip->ip_dst.s_addr, CTL_DNS))
    {
        struct sockaddr_in dst, src;

        src.sin_addr.s_addr = ip->ip_dst.s_addr;
        src.sin_port = uh->uh_dport;
        dst.sin_addr.s_addr = ip->ip_src.s_addr;
        dst.sin_port = uh->uh_sport;

        m_adj(m, sizeof(struct udpiphdr));

        m = hostresolver(pData, m, ip->ip_src.s_addr, uh->uh_sport);
        if (m == NULL)
            goto done_free_mbuf;

        slirpMbufTagService(pData, m, CTL_DNS);

        udp_output2(pData, NULL, m, &src, &dst, IPTOS_LOWDELAY);
        LogFlowFuncLeave();
        return;
    }

    /*
     *  handle TFTP
     */
    if (   uh->uh_dport == RT_H2N_U16_C(TFTP_SERVER)
        && CTL_CHECK(ip->ip_dst.s_addr, CTL_TFTP))
    {
        if (pData->pvTftpSessions)
            slirpTftpInput(pData, m);
        goto done_free_mbuf;
    }

    /*
     * XXX: DNS proxy currently relies on the fact that each socket
     * only serves one request.
     */
    if (   pData->fUseDnsProxy
        && CTL_CHECK(ip->ip_dst.s_addr, CTL_DNS)
        && (uh->uh_dport == RT_H2N_U16_C(53)))
    {
        so = NULL;
        goto new_socket;
    }

    /*
     * Drop UDP packets destind for CTL_ALIAS (i.e. the hosts loopback interface)
     * if it is disabled.
     */
    if (   CTL_CHECK(ip->ip_dst.s_addr, CTL_ALIAS)
        && !pData->fLocalhostReachable)
        goto done_free_mbuf;

    /*
     * Locate pcb for datagram.
     */
    so = udp_last_so;
    if (   so->so_lport != uh->uh_sport
        || so->so_laddr.s_addr != ip->ip_src.s_addr)
    {
        struct socket *tmp;

        for (tmp = udb.so_next; tmp != &udb; tmp = tmp->so_next)
        {
            if (   tmp->so_lport        == uh->uh_sport
                && tmp->so_laddr.s_addr == ip->ip_src.s_addr)
            {
                so = tmp;
                break;
            }
        }
        if (tmp == &udb)
            so = NULL;
        else
        {
            udpstat.udpps_pcbcachemiss++;
            udp_last_so = so;
        }
    }

  new_socket:
    if (so == NULL)
    {
        /*
         * If there's no socket for this packet,
         * create one
         */
        if ((so = socreate()) == NULL)
        {
            Log2(("NAT: IP(id: %hd) failed to create socket\n", ip->ip_id));
            goto bad_free_mbuf;
        }

        /*
         * Setup fields
         */
        so->so_laddr = ip->ip_src;
        so->so_lport = uh->uh_sport;
        so->so_iptos = ip->ip_tos;

        if (udp_attach(pData, so) <= 0)
        {
            Log2(("NAT: IP(id: %hd) udp_attach errno = %d (%s)\n",
                        ip->ip_id, errno, strerror(errno)));
            sofree(pData, so);
            goto bad_free_mbuf;
        }

        /* udp_last_so = so; */
        /*
         * XXXXX Here, check if it's in udpexec_list,
         * and if it is, do the fork_exec() etc.
         */
    }

    so->so_faddr = ip->ip_dst;   /* XXX */
    so->so_fport = uh->uh_dport; /* XXX */
    Assert(so->so_type == IPPROTO_UDP);

    /*
     * DNS proxy
     */
    if (   pData->fUseDnsProxy
        && CTL_CHECK(ip->ip_dst.s_addr, CTL_DNS)
        && (uh->uh_dport == RT_H2N_U16_C(53)))
    {
        dnsproxy_query(pData, so, m, iphlen);
        goto done_free_mbuf;
    }

    iphlen += sizeof(struct udphdr);
    m->m_len -= iphlen;
    m->m_data += iphlen;

    ttl = ip->ip_ttl = save_ip.ip_ttl;
    if (ttl != so->so_sottl) {
        ret = setsockopt(so->s, IPPROTO_IP, IP_TTL,
                         (char *)&ttl, sizeof(ttl));
        if (RT_LIKELY(ret == 0))
            so->so_sottl = ttl;
    }

    tos = save_ip.ip_tos;
    if (tos != so->so_sotos) {
        ret = setsockopt(so->s, IPPROTO_IP, IP_TOS,
                         (char *)&tos, sizeof(tos));
        if (RT_LIKELY(ret == 0))
            so->so_sotos = tos;
    }

    {
        /*
         * Different OSes have different socket options for DF.  We
         * can't use IP_HDRINCL here as it's only valid for SOCK_RAW.
         */
#     define USE_DF_OPTION(_Optname) \
        const int dfopt = _Optname
#if   defined(IP_MTU_DISCOVER)
        USE_DF_OPTION(IP_MTU_DISCOVER);
#elif defined(IP_DONTFRAG)      /* Solaris 11+, FreeBSD */
        USE_DF_OPTION(IP_DONTFRAG);
#elif defined(IP_DONTFRAGMENT)  /* Windows */
        USE_DF_OPTION(IP_DONTFRAGMENT);
#else
        USE_DF_OPTION(0);
#endif
        if (dfopt) {
            int df = (save_ip.ip_off & IP_DF) != 0;
#if defined(IP_MTU_DISCOVER)
            df = df ? IP_PMTUDISC_DO : IP_PMTUDISC_DONT;
#endif
            if (df != so->so_sodf) {
                ret = setsockopt(so->s, IPPROTO_IP, dfopt,
                                 (char *)&df, sizeof(df));
                if (RT_LIKELY(ret == 0))
                    so->so_sodf = df;
            }
        }
    }

    if (   sosendto(pData, so, m) == -1
        && (   !soIgnorableErrorCode(errno)
            && errno != ENOTCONN))
    {
        m->m_len += iphlen;
        m->m_data -= iphlen;
        *ip = save_ip;
        Log2(("NAT: UDP tx errno = %d (%s) on sent to %RTnaipv4\n",
              errno, strerror(errno), ip->ip_dst));
        icmp_error(pData, m, ICMP_UNREACH, ICMP_UNREACH_NET, 0, strerror(errno));
        so->so_m = NULL;
        LogFlowFuncLeave();
        return;
    }

    if (so->so_m)
        m_freem(pData, so->so_m);   /* used for ICMP if error on sorecvfrom */

    /* restore the orig mbuf packet */
    m->m_len += iphlen;
    m->m_data -= iphlen;
    *ip = save_ip;
    so->so_m = m;         /* ICMP backup */
    LogFlowFuncLeave();
    return;

bad_free_mbuf:
    Log2(("NAT: UDP(id: %hd) datagram to %RTnaipv4 with size(%d) claimed as bad\n",
        ip->ip_id, &ip->ip_dst, ip->ip_len));

done_free_mbuf:
    /* some services like bootp(built-in), dns(buildt-in) and dhcp don't need sockets
     * and create new m'buffers to send them to guest, so we'll free their incomming
     * buffers here.
     */
    if (m != NULL)
        m_freem(pData, m);
    LogFlowFuncLeave();
    return;
}

/**
 * Output a UDP packet.
 *
 * @note This function will finally free m!
 */
int udp_output2(PNATState pData, struct socket *so, struct mbuf *m,
                struct sockaddr_in *saddr, struct sockaddr_in *daddr,
                int iptos)
{
    register struct udpiphdr *ui;
    int error;
    int mlen = 0;

    LogFlowFunc(("ENTER: so = %R[natsock], m = %p, saddr = %RTnaipv4, daddr = %RTnaipv4\n",
                 so, m, saddr->sin_addr.s_addr, daddr->sin_addr.s_addr));

    /* in case of built-in service so might be NULL */
    if (so) Assert(so->so_type == IPPROTO_UDP);

    /*
     * Adjust for header
     */
    m->m_data -= sizeof(struct udpiphdr);
    m->m_len += sizeof(struct udpiphdr);
    mlen = m_length(m, NULL);

    /*
     * Fill in mbuf with extended UDP header
     * and addresses and length put into network format.
     */
    ui = mtod(m, struct udpiphdr *);
    memset(ui->ui_x1, 0, 9);
    ui->ui_pr = IPPROTO_UDP;
    ui->ui_len = RT_H2N_U16((uint16_t)(mlen - sizeof(struct ip)));
    /* XXXXX Check for from-one-location sockets, or from-any-location sockets */
    ui->ui_src = saddr->sin_addr;
    ui->ui_dst = daddr->sin_addr;
    ui->ui_sport = saddr->sin_port;
    ui->ui_dport = daddr->sin_port;
    ui->ui_ulen = ui->ui_len;

    /*
     * Stuff checksum and output datagram.
     */
    ui->ui_sum = 0;
    if (udpcksum)
    {
        if ((ui->ui_sum = cksum(m, /* sizeof (struct udpiphdr) + */ mlen)) == 0)
            ui->ui_sum = 0xffff;
    }
    ((struct ip *)ui)->ip_len = mlen;
    ((struct ip *)ui)->ip_ttl = ip_defttl;
    ((struct ip *)ui)->ip_tos = iptos;

    udpstat.udps_opackets++;

    error = ip_output(pData, so, m);

    return error;
}

/**
 * @note This function will free m!
 */
int udp_output(PNATState pData, struct socket *so, struct mbuf *m,
               struct sockaddr_in *addr)
{
    struct sockaddr_in saddr, daddr;

    Assert(so->so_type == IPPROTO_UDP);
    LogFlowFunc(("ENTER: so = %R[natsock], m = %p, saddr = %RTnaipv4\n", so, m, addr->sin_addr.s_addr));

    if (so->so_laddr.s_addr == INADDR_ANY)
    {
        if (pData->guest_addr_guess.s_addr != INADDR_ANY)
        {
            LogRel2(("NAT: port-forward: using %RTnaipv4 for %R[natsock]\n",
                     pData->guest_addr_guess.s_addr, so));
            so->so_laddr = pData->guest_addr_guess;
        }
        else
        {
            LogRel2(("NAT: port-forward: guest address unknown for %R[natsock]\n", so));
            m_freem(pData, m);
            return 0;
        }
    }

    saddr = *addr;
    if ((so->so_faddr.s_addr & RT_H2N_U32(pData->netmask)) == pData->special_addr.s_addr)
    {
        saddr.sin_addr.s_addr = so->so_faddr.s_addr;
        if (slirpIsWideCasting(pData, so->so_faddr.s_addr))
        {
            /**
             * We haven't got real firewall but have got its submodule libalias.
             */
            m->m_flags |= M_SKIP_FIREWALL;
            /**
             * udp/137 port is Name Service in NetBIOS protocol. for some reasons Windows guest rejects
             * accept data from non-aliased server.
             */
            if (   (so->so_fport == so->so_lport)
                && (so->so_fport == RT_H2N_U16(137)))
                saddr.sin_addr.s_addr = alias_addr.s_addr;
            else
                saddr.sin_addr.s_addr = addr->sin_addr.s_addr;
            so->so_faddr.s_addr = addr->sin_addr.s_addr;
        }
    }

    /* Any UDP packet to the loopback address must be translated to be from
     * the forwarding address, i.e. 10.0.2.2. */
    if (   (saddr.sin_addr.s_addr & RT_H2N_U32_C(IN_CLASSA_NET))
        == RT_H2N_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET))
        saddr.sin_addr.s_addr = alias_addr.s_addr;

    daddr.sin_addr = so->so_laddr;
    daddr.sin_port = so->so_lport;

    return udp_output2(pData, so, m, &saddr, &daddr, so->so_iptos);
}

int
udp_attach(PNATState pData, struct socket *so)
{
    struct sockaddr sa_addr;
    socklen_t socklen = sizeof(struct sockaddr);
    int status;
    int opt = 1;

    AssertReturn(so->so_type == 0, -1);
    so->so_type = IPPROTO_UDP;

    so->s = socket(AF_INET, SOCK_DGRAM, 0);
    if (so->s == -1)
        goto error;
    fd_nonblock(so->s);

    so->so_sottl = 0;
    so->so_sotos = 0;
    so->so_sodf = -1;

    status = sobind(pData, so);
    if (status != 0)
        return status;

    /* success, insert in queue */
    so->so_expire = curtime + SO_EXPIRE;

    /* enable broadcast for later use */
    setsockopt(so->s, SOL_SOCKET, SO_BROADCAST, (const char *)&opt, sizeof(opt));

    status = getsockname(so->s, &sa_addr, &socklen);
    if (status == 0)
    {
        Assert(sa_addr.sa_family == AF_INET);
        so->so_hlport = ((struct sockaddr_in *)&sa_addr)->sin_port;
        so->so_hladdr.s_addr = ((struct sockaddr_in *)&sa_addr)->sin_addr.s_addr;
    }

    SOCKET_LOCK_CREATE(so);
    QSOCKET_LOCK(udb);
    insque(pData, so, &udb);
    NSOCK_INC();
    QSOCKET_UNLOCK(udb);
    return so->s;
error:
    Log2(("NAT: can't create datagram socket\n"));
    return -1;
}

void
udp_detach(PNATState pData, struct socket *so)
{
    if (so != &pData->icmp_socket)
    {
        Assert(so->so_type == IPPROTO_UDP);
        QSOCKET_LOCK(udb);
        SOCKET_LOCK(so);
        QSOCKET_UNLOCK(udb);
        closesocket(so->s);
        sofree(pData, so);
        SOCKET_UNLOCK(so);
    }
}

struct socket *
udp_listen(PNATState pData, u_int32_t bind_addr, u_int port, u_int32_t laddr, u_int lport, int flags)
{
    struct sockaddr_in addr;
    struct socket *so;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    int opt = 1;
    LogFlowFunc(("ENTER: bind_addr:%RTnaipv4, port:%d, laddr:%RTnaipv4, lport:%d, flags:%x\n",
                 bind_addr, RT_N2H_U16(port), laddr, RT_N2H_U16(lport), flags));

    if ((so = socreate()) == NULL)
    {
        LogFlowFunc(("LEAVE: NULL\n"));
        return NULL;
    }

    so->s = socket(AF_INET, SOCK_DGRAM, 0);
    if (so->s == -1)
    {
        LogRel(("NAT: can't create datagram socket\n"));
        RTMemFree(so);
        LogFlowFunc(("LEAVE: NULL\n"));
        return NULL;
    }
    so->so_expire = curtime + SO_EXPIRE;
    so->so_type = IPPROTO_UDP;
    fd_nonblock(so->s);
    so->so_sottl = 0;
    so->so_sotos = 0;
    so->so_sodf = -1;
    SOCKET_LOCK_CREATE(so);
    QSOCKET_LOCK(udb);
    insque(pData, so, &udb);
    NSOCK_INC();
    QSOCKET_UNLOCK(udb);

    memset(&addr, 0, sizeof(addr));
#ifdef RT_OS_DARWIN
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = bind_addr;
    addr.sin_port = port;

    if (bind(so->s,(struct sockaddr *)&addr, addrlen) < 0)
    {
        LogRel(("NAT: udp bind to %RTnaipv4:%d failed, error %d\n",
                addr.sin_addr, RT_N2H_U16(port), errno));
        udp_detach(pData, so);
        LogFlowFunc(("LEAVE: NULL\n"));
        return NULL;
    }
    setsockopt(so->s, SOL_SOCKET, SO_REUSEADDR,(char *)&opt, sizeof(int));
/*  setsockopt(so->s, SOL_SOCKET, SO_OOBINLINE,(char *)&opt, sizeof(int)); */

    getsockname(so->s,(struct sockaddr *)&addr,&addrlen);
    so->so_hladdr = addr.sin_addr;
    so->so_hlport = addr.sin_port;

    /* XXX: wtf are we setting so_faddr/so_fport here? */
    so->so_fport = addr.sin_port;
#if 0
    /* The original check was completely broken, as the commented out
     * if statement was always true (INADDR_ANY=0). */
    /** @todo vvl - alias_addr should be set (if required)
     * later by liabalias module.
     */
    if (addr.sin_addr.s_addr == 0 || addr.sin_addr.s_addr == loopback_addr.s_addr)
        so->so_faddr = alias_addr;
    else
#endif
        so->so_faddr = addr.sin_addr;

    so->so_lport = lport;
    so->so_laddr.s_addr = laddr;
    if (flags != SS_FACCEPTONCE)
        so->so_expire = 0;

    so->so_state = SS_ISFCONNECTED;

    LogFlowFunc(("LEAVE: %R[natsock]\n", so));
    return so;
}
