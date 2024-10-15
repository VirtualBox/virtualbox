/* $Id: pxping.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - ping proxy, raw sockets version.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_NAT_SERVICE

#include "winutils.h"
#include "proxy.h"
#include "proxy_pollmgr.h"
#include "pxremap.h"

#include <iprt/string.h>

#ifndef RT_OS_WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#ifdef RT_OS_DARWIN
# define __APPLE_USE_RFC_3542
#endif
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <iprt/stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winpoll.h"
#endif

#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/icmp.h"

#if defined(RT_OS_LINUX) && !defined(__USE_GNU)
#if __GLIBC_PREREQ(2, 8)
/*
 * XXX: This is gross.  in6_pktinfo is now hidden behind _GNU_SOURCE
 * https://sourceware.org/bugzilla/show_bug.cgi?id=6775
 *
 * But in older glibc versions, e.g. RHEL5, it is not!  I don't want
 * to deal with _GNU_SOURCE now, so as a kludge check for glibc
 * version.  It seems the __USE_GNU guard was introduced in 2.8.
 */
struct in6_pktinfo {
    struct in6_addr ipi6_addr;
    unsigned int ipi6_ifindex;
};
#endif  /* __GLIBC_PREREQ */
#endif  /* RT_OS_LINUX && !__USE_GNU */


/* forward */
struct ping_pcb;


/**
 * Global state for ping proxy collected in one entity to minimize
 * globals.  There's only one instance of this structure.
 *
 * Raw ICMP sockets are promiscuous, so it doesn't make sense to have
 * multiple.  If this code ever needs to support multiple netifs, the
 * netif member should be exiled into "pcb".
 */
struct pxping {
    SOCKET sock4;

#if defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS)
#   define DF_WITH_IP_HDRINCL
    int hdrincl;
#else
    int df;
#endif
    int ttl;
    int tos;

    SOCKET sock6;
#ifdef RT_OS_WINDOWS
    LPFN_WSARECVMSG pfWSARecvMsg6;
#endif
    int hopl;

    struct pollmgr_handler pmhdl4;
    struct pollmgr_handler pmhdl6;

    struct netif *netif;

    /**
     * Protect lwIP and pmgr accesses to the list of pcbs.
     */
    sys_mutex_t lock;

    /*
     * We need to find pcbs both from the guest side and from the host
     * side.  If we need to support industrial grade ping throughput,
     * we will need two pcb hashes.  For now, a short linked list
     * should be enough.  Cf. pxping_pcb_for_request() and
     * pxping_pcb_for_reply().
     */
#define PXPING_MAX_PCBS 8
    size_t npcbs;
    struct ping_pcb *pcbs;

#define TIMEOUT 5
    int timer_active;
    size_t timeout_slot;
    struct ping_pcb *timeout_list[TIMEOUT];
};


/**
 * Quasi PCB for ping.
 */
struct ping_pcb {
    ipX_addr_t src;
    ipX_addr_t dst;

    u8_t is_ipv6;
    u8_t is_mapped;

    u16_t guest_id;
    u16_t host_id;

    /**
     * Desired slot in pxping::timeout_list.  See pxping_timer().
     */
    size_t timeout_slot;

    /**
     * Chaining for pxping::timeout_list
     */
    struct ping_pcb **pprev_timeout;
    struct ping_pcb *next_timeout;

    /**
     * Chaining for pxping::pcbs
     */
    struct ping_pcb *next;

    union {
        struct sockaddr_in sin;
        struct sockaddr_in6 sin6;
    } peer;
};


/**
 * lwIP thread callback message for IPv4 ping.
 *
 * We pass raw IP datagram for ip_output_if() so we only need pbuf and
 * netif (from pxping).
 */
struct ping_msg {
    struct tcpip_msg msg;
    struct pxping *pxping;
    struct pbuf *p;
};


/**
 * lwIP thread callback message for IPv6 ping.
 *
 * We cannot obtain raw IPv6 datagram from host without extra trouble,
 * so we pass ICMPv6 payload in pbuf and also other parameters to
 * ip6_output_if().
 */
struct ping6_msg {
    struct tcpip_msg msg;
    struct pxping *pxping;
    struct pbuf *p;
    ip6_addr_t src, dst;
    int hopl, tclass;
};


#ifdef RT_OS_WINDOWS
static int pxping_init_windows(struct pxping *pxping);
#endif
static void pxping_recv4(void *arg, struct pbuf *p);
static void pxping_recv6(void *arg, struct pbuf *p);

static void pxping_timer(void *arg);
static void pxping_timer_needed(struct pxping *pxping);

static struct ping_pcb *pxping_pcb_for_request(struct pxping *pxping,
                                               int is_ipv6,
                                               ipX_addr_t *src, ipX_addr_t *dst,
                                               u16_t guest_id);
static struct ping_pcb *pxping_pcb_for_reply(struct pxping *pxping, int is_ipv6,
                                             ipX_addr_t *dst, u16_t host_id);

static FNRTSTRFORMATTYPE pxping_pcb_rtstrfmt;
static struct ping_pcb *pxping_pcb_allocate(struct pxping *pxping);
static void pxping_pcb_register(struct pxping *pxping, struct ping_pcb *pcb);
static void pxping_pcb_deregister(struct pxping *pxping, struct ping_pcb *pcb);
static void pxping_pcb_delete(struct pxping *pxping, struct ping_pcb *pcb);
static void pxping_timeout_add(struct pxping *pxping, struct ping_pcb *pcb);
static void pxping_timeout_del(struct pxping *pxping, struct ping_pcb *pcb);

static int pxping_pmgr_pump(struct pollmgr_handler *handler, SOCKET fd, int revents);

static void pxping_pmgr_icmp4(struct pxping *pxping);
static void pxping_pmgr_icmp4_echo(struct pxping *pxping,
                                   u16_t iplen, struct sockaddr_in *peer);
static void pxping_pmgr_icmp4_error(struct pxping *pxping,
                                    u16_t iplen, struct sockaddr_in *peer);
static void pxping_pmgr_icmp6(struct pxping *pxping);
static void pxping_pmgr_icmp6_echo(struct pxping *pxping,
                                   ip6_addr_t *src, ip6_addr_t *dst,
                                   int hopl, int tclass, u16_t icmplen);
static void pxping_pmgr_icmp6_error(struct pxping *pxping,
                                    ip6_addr_t *src, ip6_addr_t *dst,
                                    int hopl, int tclass, u16_t icmplen);

static void pxping_pmgr_forward_inbound(struct pxping *pxping, u16_t iplen);
static void pxping_pcb_forward_inbound(void *arg);

static void pxping_pmgr_forward_inbound6(struct pxping *pxping,
                                         ip6_addr_t *src, ip6_addr_t *dst,
                                         u8_t hopl, u8_t tclass,
                                         u16_t icmplen);
static void pxping_pcb_forward_inbound6(void *arg);

/*
 * NB: This is not documented except in RTFS.
 *
 * If ip_output_if() is passed dest == NULL then it treats p as
 * complete IP packet with payload pointing to the IP header.  It does
 * not build IP header, ignores all header-related arguments, fetches
 * real destination from the header in the pbuf and outputs pbuf to
 * the specified netif.
 */
#define ip_raw_output_if(p, netif)                      \
    (ip_output_if((p), NULL, NULL, 0, 0, 0, (netif)))



static struct pxping g_pxping;


err_t
pxping_init(struct netif *netif, SOCKET sock4, SOCKET sock6)
{
    const int on = 1;
    int status;

    if (sock4 == INVALID_SOCKET && sock6 == INVALID_SOCKET) {
        return ERR_VAL;
    }

    g_pxping.netif = netif;
    sys_mutex_new(&g_pxping.lock);

    g_pxping.sock4 = sock4;
    if (g_pxping.sock4 != INVALID_SOCKET) {
#ifdef DF_WITH_IP_HDRINCL
        g_pxping.hdrincl = 0;
#else
        g_pxping.df = -1;
#endif
        g_pxping.ttl = -1;
        g_pxping.tos = 0;

#ifdef RT_OS_LINUX
        {
            const int dont = IP_PMTUDISC_DONT;
            status = setsockopt(sock4, IPPROTO_IP, IP_MTU_DISCOVER,
                                &dont, sizeof(dont));
            if (status != 0) {
                DPRINTF(("IP_MTU_DISCOVER: %R[sockerr]\n", SOCKERRNO()));
            }
        }
#endif /* RT_OS_LINUX */

        g_pxping.pmhdl4.callback = pxping_pmgr_pump;
        g_pxping.pmhdl4.data = (void *)&g_pxping;
        g_pxping.pmhdl4.slot = -1;
        pollmgr_add(&g_pxping.pmhdl4, g_pxping.sock4, POLLIN);

        ping_proxy_accept(pxping_recv4, &g_pxping);
    }

    g_pxping.sock6 = sock6;
#ifdef RT_OS_WINDOWS
    /* we need recvmsg */
    if (g_pxping.sock6 != INVALID_SOCKET) {
        status = pxping_init_windows(&g_pxping);
        if (status == SOCKET_ERROR) {
            g_pxping.sock6 = INVALID_SOCKET;
            /* close(sock6); */
        }
    }
#endif
    if (g_pxping.sock6 != INVALID_SOCKET) {
        g_pxping.hopl = -1;

#if !defined(IPV6_RECVPKTINFO)
#define IPV6_RECVPKTINFO (IPV6_PKTINFO)
#endif
        status = setsockopt(sock6, IPPROTO_IPV6, IPV6_RECVPKTINFO,
                            (const char *)&on, sizeof(on));
        if (status < 0) {
            DPRINTF(("IPV6_RECVPKTINFO: %R[sockerr]\n", SOCKERRNO()));
            /* XXX: for now this is fatal */
        }

#if !defined(IPV6_RECVHOPLIMIT)
#define IPV6_RECVHOPLIMIT (IPV6_HOPLIMIT)
#endif
        status = setsockopt(sock6, IPPROTO_IPV6, IPV6_RECVHOPLIMIT,
                            (const char *)&on, sizeof(on));
        if (status < 0) {
            DPRINTF(("IPV6_RECVHOPLIMIT: %R[sockerr]\n", SOCKERRNO()));
        }

#ifdef IPV6_RECVTCLASS  /* new in RFC 3542, there's no RFC 2292 counterpart */
        /** @todo IPV6_RECVTCLASS */
#endif

        g_pxping.pmhdl6.callback = pxping_pmgr_pump;
        g_pxping.pmhdl6.data = (void *)&g_pxping;
        g_pxping.pmhdl6.slot = -1;
        pollmgr_add(&g_pxping.pmhdl6, g_pxping.sock6, POLLIN);

        ping6_proxy_accept(pxping_recv6, &g_pxping);
    }

    status = RTStrFormatTypeRegister("ping_pcb", pxping_pcb_rtstrfmt, NULL);
    AssertRC(status);

    return ERR_OK;
}


#ifdef RT_OS_WINDOWS
static int
pxping_init_windows(struct pxping *pxping)
{
    GUID WSARecvMsgGUID = WSAID_WSARECVMSG;
    DWORD nread;
    int status;

    pxping->pfWSARecvMsg6 = NULL;
    status = WSAIoctl(pxping->sock6,
                      SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &WSARecvMsgGUID, sizeof(WSARecvMsgGUID),
                      &pxping->pfWSARecvMsg6, sizeof(pxping->pfWSARecvMsg6),
                      &nread,
                      NULL, NULL);
    return status;
}
#endif  /* RT_OS_WINDOWS */


static u32_t
chksum_delta_16(u16_t oval, u16_t nval)
{
    u32_t sum = (u16_t)~oval;
    sum += nval;
    return sum;
}


static u32_t
chksum_update_16(u16_t *oldp, u16_t nval)
{
    u32_t sum = chksum_delta_16(*oldp, nval);
    *oldp = nval;
    return sum;
}


static u32_t
chksum_delta_32(u32_t oval, u32_t nval)
{
    u32_t sum = ~oval;
    sum = FOLD_U32T(sum);
    sum += FOLD_U32T(nval);
    return sum;
}


static u32_t
chksum_update_32(u32_t *oldp, u32_t nval)
{
    u32_t sum = chksum_delta_32(*oldp, nval);
    *oldp = nval;
    return sum;
}


static u32_t
chksum_delta_ipv6(const ip6_addr_t *oldp, const ip6_addr_t *newp)
{
    u32_t sum;

    sum  = chksum_delta_32(oldp->addr[0], newp->addr[0]);
    sum += chksum_delta_32(oldp->addr[1], newp->addr[1]);
    sum += chksum_delta_32(oldp->addr[2], newp->addr[2]);
    sum += chksum_delta_32(oldp->addr[3], newp->addr[3]);

    return sum;
}


static u32_t
chksum_update_ipv6(ip6_addr_t *oldp, const ip6_addr_t *newp)
{
    u32_t sum;

    sum  = chksum_update_32(&oldp->addr[0], newp->addr[0]);
    sum += chksum_update_32(&oldp->addr[1], newp->addr[1]);
    sum += chksum_update_32(&oldp->addr[2], newp->addr[2]);
    sum += chksum_update_32(&oldp->addr[3], newp->addr[3]);

    return sum;
}


/**
 * ICMP Echo Request in pbuf "p" is to be proxied.
 */
static void
pxping_recv4(void *arg, struct pbuf *p)
{
    struct pxping *pxping = (struct pxping *)arg;
    struct ping_pcb *pcb;
#ifdef DF_WITH_IP_HDRINCL
    struct ip_hdr iph_orig;
#endif
    struct icmp_echo_hdr icmph_orig;
    struct ip_hdr *iph;
    struct icmp_echo_hdr *icmph;
    int df, ttl, tos;
    u32_t sum;
    u16_t iphlen;
    int status;

    iphlen = ip_current_header_tot_len();
    if (iphlen != IP_HLEN) {    /* we don't do options */
        pbuf_free(p);
        return;
    }

    iph = (/* UNCONST */ struct ip_hdr *)ip_current_header();
    icmph = (struct icmp_echo_hdr *)p->payload;

    pcb = pxping_pcb_for_request(pxping, 0,
                                 ipX_current_src_addr(),
                                 ipX_current_dest_addr(),
                                 icmph->id);
    if (pcb == NULL) {
        pbuf_free(p);
        return;
    }

    DPRINTF(("ping %p: %R[ping_pcb] seq %d len %u ttl %d\n",
             pcb, pcb,
             ntohs(icmph->seqno), (unsigned int)p->tot_len,
             IPH_TTL(iph)));

    ttl = IPH_TTL(iph);
    if (!pcb->is_mapped) {
        if (RT_UNLIKELY(ttl == 1)) {
            status = pbuf_header(p, iphlen); /* back to IP header */
            if (RT_LIKELY(status == 0)) {
                icmp_time_exceeded(p, ICMP_TE_TTL);
            }
            pbuf_free(p);
            return;
        }
        --ttl;
    }

    /*
     * OS X doesn't provide a socket option to control fragmentation.
     * Solaris doesn't provide IP_DONTFRAG on all releases we support.
     * In this case we have to use IP_HDRINCL.  We don't want to use
     * it always since it doesn't handle fragmentation (but that's ok
     * for DF) and Windows doesn't do automatic source address
     * selection with IP_HDRINCL.
     */
    df = (IPH_OFFSET(iph) & PP_HTONS(IP_DF)) != 0;

#ifdef DF_WITH_IP_HDRINCL
    if (df != pxping->hdrincl) {
        status = setsockopt(pxping->sock4, IPPROTO_IP, IP_HDRINCL,
                            &df, sizeof(df));
        if (RT_LIKELY(status == 0)) {
            pxping->hdrincl = df;
        }
        else {
            DPRINTF(("IP_HDRINCL: %R[sockerr]\n", SOCKERRNO()));
        }
    }

    if (pxping->hdrincl) {
        status = pbuf_header(p, iphlen); /* back to IP header */
        if (RT_UNLIKELY(status != 0)) {
            pbuf_free(p);
            return;
        }

        /* we will overwrite IP header, save original for ICMP errors */
        memcpy(&iph_orig, iph, iphlen);

        if (pcb->is_mapped) {
            ip4_addr_set_u32(&iph->dest, pcb->peer.sin.sin_addr.s_addr);
        }

        if (g_proxy_options->src4 != NULL) {
            ip4_addr_set_u32(&iph->src, g_proxy_options->src4->sin_addr.s_addr);
        }
        else {
            /* let the kernel select suitable source address */
            ip_addr_set_any(&iph->src);
        }

        IPH_TTL_SET(iph, ttl);  /* already decremented */
        IPH_ID_SET(iph, 0);     /* kernel will set one */
#ifdef RT_OS_DARWIN
        /* wants ip_offset and ip_len fields in host order */
        IPH_OFFSET_SET(iph, ntohs(IPH_OFFSET(iph)));
        IPH_LEN_SET(iph, ntohs(IPH_LEN(iph)));
        /* wants checksum of everything (sic!), in host order */
        sum = inet_chksum_pbuf(p);
        IPH_CHKSUM_SET(iph, sum);
#else /* !RT_OS_DARWIN  */
        IPH_CHKSUM_SET(iph, 0); /* kernel will recalculate */
#endif
    }
    else /* !pxping->hdrincl */
#endif   /* DF_WITH_IP_HDRINCL */
    {
#if !defined(DF_WITH_IP_HDRINCL)
        /* control DF flag via setsockopt(2) */
#define USE_DF_OPTION(_Optname)                         \
        const int dfopt = _Optname;                     \
        const char * const dfoptname = #_Optname; NOREF(dfoptname)
#if   defined(RT_OS_LINUX)
        USE_DF_OPTION(IP_MTU_DISCOVER);
        df = df ? IP_PMTUDISC_DO : IP_PMTUDISC_DONT;
#elif defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
        USE_DF_OPTION(IP_DONTFRAG);
#elif defined(RT_OS_WINDOWS)
        USE_DF_OPTION(IP_DONTFRAGMENT);
#endif
        if (df != pxping->df) {
            status = setsockopt(pxping->sock4, IPPROTO_IP, dfopt,
                                (char *)&df, sizeof(df));
            if (RT_LIKELY(status == 0)) {
                pxping->df = df;
            }
            else {
                DPRINTF(("%s: %R[sockerr]\n", dfoptname, SOCKERRNO()));
            }
        }
#endif /* !DF_WITH_IP_HDRINCL */

        if (ttl != pxping->ttl) {
            status = setsockopt(pxping->sock4, IPPROTO_IP, IP_TTL,
                                (char *)&ttl, sizeof(ttl));
            if (RT_LIKELY(status == 0)) {
                pxping->ttl = ttl;
            }
            else {
                DPRINTF(("IP_TTL: %R[sockerr]\n", SOCKERRNO()));
            }
        }

        tos = IPH_TOS(iph);
        if (tos != pxping->tos) {
            status = setsockopt(pxping->sock4, IPPROTO_IP, IP_TOS,
                                (char *)&tos, sizeof(tos));
            if (RT_LIKELY(status == 0)) {
                pxping->tos = tos;
            }
            else {
                DPRINTF(("IP_TOS: %R[sockerr]\n", SOCKERRNO()));
            }
        }
    }

    /* rewrite ICMP echo header */
    memcpy(&icmph_orig, icmph, sizeof(*icmph));
    sum = (u16_t)~icmph->chksum;
    sum += chksum_update_16(&icmph->id, pcb->host_id);
    sum = FOLD_U32T(sum);
    icmph->chksum = ~sum;

    status = proxy_sendto(pxping->sock4, p,
                          &pcb->peer.sin, sizeof(pcb->peer.sin));
    if (status != 0) {
        int error = -status;
        DPRINTF(("%s: sendto: %R[sockerr]\n", __func__, error));

#ifdef DF_WITH_IP_HDRINCL
        if (pxping->hdrincl) {
            /* restore original IP header */
            memcpy(iph, &iph_orig, iphlen);
        }
        else
#endif
        {
            status = pbuf_header(p, iphlen); /* back to IP header */
            if (RT_UNLIKELY(status != 0)) {
                pbuf_free(p);
                return;
            }
        }

        /* restore original ICMP header */
        memcpy(icmph, &icmph_orig, sizeof(*icmph));

        /*
         * Some ICMP errors may be generated by the kernel and we read
         * them from the socket and forward them normally, hence the
         * ifdefs below.
         */
        switch (error) {

#if !( defined(RT_OS_SOLARIS)                                   \
    || (defined(RT_OS_LINUX) && !defined(DF_WITH_IP_HDRINCL))   \
    )
        case EMSGSIZE:
            icmp_dest_unreach(p, ICMP_DUR_FRAG);
            break;
#endif

        case ENETDOWN:
        case ENETUNREACH:
            icmp_dest_unreach(p, ICMP_DUR_NET);
            break;

        case EHOSTDOWN:
        case EHOSTUNREACH:
            icmp_dest_unreach(p, ICMP_DUR_HOST);
            break;
        }
    }

    pbuf_free(p);
}


/**
 * ICMPv6 Echo Request in pbuf "p" is to be proxied.
 */
static void
pxping_recv6(void *arg, struct pbuf *p)
{
    struct pxping *pxping = (struct pxping *)arg;
    struct ping_pcb *pcb;
    struct ip6_hdr *iph;
    struct icmp6_echo_hdr *icmph;
    int hopl;
    u16_t iphlen;
    u16_t id;
    int status;

    iph = (/* UNCONST */ struct ip6_hdr *)ip6_current_header();
    iphlen = ip_current_header_tot_len();

    icmph = (struct icmp6_echo_hdr *)p->payload;

    id  = icmph->id;
    pcb = pxping_pcb_for_request(pxping, 1,
                                 ipX_current_src_addr(),
                                 ipX_current_dest_addr(),
                                 id);
    if (pcb == NULL) {
        pbuf_free(p);
        return;
    }

    DPRINTF(("ping %p: %R[ping_pcb] seq %d len %u hopl %d\n",
             pcb, pcb,
             ntohs(icmph->seqno), (unsigned int)p->tot_len,
             IP6H_HOPLIM(iph)));

    hopl = IP6H_HOPLIM(iph);
    if (!pcb->is_mapped) {
        if (hopl == 1) {
            status = pbuf_header(p, iphlen); /* back to IP header */
            if (RT_LIKELY(status == 0)) {
                icmp6_time_exceeded(p, ICMP6_TE_HL);
            }
            pbuf_free(p);
            return;
        }
        --hopl;
    }

    /*
     * Rewrite ICMPv6 echo header.  We don't need to recompute the
     * checksum since, unlike IPv4, checksum includes pseudo-header.
     * OS computes checksum for us on send() since it needs to select
     * source address.
     */
    icmph->id = pcb->host_id;

    /** @todo use control messages to save a syscall? */
    if (hopl != pxping->hopl) {
        status = setsockopt(pxping->sock6, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                            (char *)&hopl, sizeof(hopl));
        if (status == 0) {
            pxping->hopl = hopl;
        }
        else {
            DPRINTF(("IPV6_HOPLIMIT: %R[sockerr]\n", SOCKERRNO()));
        }
    }

    status = proxy_sendto(pxping->sock6, p,
                          &pcb->peer.sin6, sizeof(pcb->peer.sin6));
    if (status != 0) {
        int error = -status;
        DPRINTF(("%s: sendto: %R[sockerr]\n", __func__, error));

        status = pbuf_header(p, iphlen); /* back to IP header */
        if (RT_UNLIKELY(status != 0)) {
            pbuf_free(p);
            return;
        }

        /* restore original ICMP header */
        icmph->id = pcb->guest_id;

        switch (error) {
        case EACCES:
            icmp6_dest_unreach(p, ICMP6_DUR_PROHIBITED);
            break;

#ifdef ENONET
        case ENONET:
#endif
        case ENETDOWN:
        case ENETUNREACH:
        case EHOSTDOWN:
        case EHOSTUNREACH:
            icmp6_dest_unreach(p, ICMP6_DUR_NO_ROUTE);
            break;
        }
    }

    pbuf_free(p);
}


/**
 * Formatter for %R[ping_pcb].
 */
static DECLCALLBACK(size_t)
pxping_pcb_rtstrfmt(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                    const char *pszType, const void *pvValue,
                    int cchWidth, int cchPrecision, unsigned int fFlags,
                    void *pvUser)
{
    const struct ping_pcb *pcb = (const struct ping_pcb *)pvValue;
    size_t cb = 0;

    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);

    AssertReturn(strcmp(pszType, "ping_pcb") == 0, 0);

    if (pcb == NULL) {
        return RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL, "(null)");
    }

    /* XXX: %RTnaipv4 takes the value, but %RTnaipv6 takes the pointer */
    if (pcb->is_ipv6) {
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                          "%RTnaipv6 -> %RTnaipv6", &pcb->src, &pcb->dst);
        if (pcb->is_mapped) {
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                              " (%RTnaipv6)", &pcb->peer.sin6.sin6_addr);
        }
    }
    else {
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                          "%RTnaipv4 -> %RTnaipv4",
                          ip4_addr_get_u32(ipX_2_ip(&pcb->src)),
                          ip4_addr_get_u32(ipX_2_ip(&pcb->dst)));
        if (pcb->is_mapped) {
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                              " (%RTnaipv4)", pcb->peer.sin.sin_addr.s_addr);
        }
    }

    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                      " id %04x->%04x", ntohs(pcb->guest_id), ntohs(pcb->host_id));

    return cb;
}


static struct ping_pcb *
pxping_pcb_allocate(struct pxping *pxping)
{
    struct ping_pcb *pcb;

    if (pxping->npcbs >= PXPING_MAX_PCBS) {
        return NULL;
    }

    pcb = (struct ping_pcb *)malloc(sizeof(*pcb));
    if (pcb == NULL) {
        return NULL;
    }

    ++pxping->npcbs;
    return pcb;
}


static void
pxping_pcb_delete(struct pxping *pxping, struct ping_pcb *pcb)
{
    LWIP_ASSERT1(pxping->npcbs > 0);
    LWIP_ASSERT1(pcb->next == NULL);
    LWIP_ASSERT1(pcb->pprev_timeout == NULL);

    DPRINTF(("%s: ping %p\n", __func__, (void *)pcb));

    --pxping->npcbs;
    free(pcb);
}


static void
pxping_timeout_add(struct pxping *pxping, struct ping_pcb *pcb)
{
    struct ping_pcb **chain;

    LWIP_ASSERT1(pcb->pprev_timeout == NULL);

    chain = &pxping->timeout_list[pcb->timeout_slot];
    if ((pcb->next_timeout = *chain) != NULL) {
        (*chain)->pprev_timeout = &pcb->next_timeout;
    }
    *chain = pcb;
    pcb->pprev_timeout = chain;
}


static void
pxping_timeout_del(struct pxping *pxping, struct ping_pcb *pcb)
{
    LWIP_UNUSED_ARG(pxping);

    LWIP_ASSERT1(pcb->pprev_timeout != NULL);
    if (pcb->next_timeout != NULL) {
        pcb->next_timeout->pprev_timeout = pcb->pprev_timeout;
    }
    *pcb->pprev_timeout = pcb->next_timeout;
    pcb->pprev_timeout = NULL;
    pcb->next_timeout = NULL;
}


static void
pxping_pcb_register(struct pxping *pxping, struct ping_pcb *pcb)
{
    pcb->next = pxping->pcbs;
    pxping->pcbs = pcb;

    pxping_timeout_add(pxping, pcb);
}


static void
pxping_pcb_deregister(struct pxping *pxping, struct ping_pcb *pcb)
{
    struct ping_pcb **p;

    for (p = &pxping->pcbs; *p != NULL; p = &(*p)->next) {
        if (*p == pcb) {
            *p = pcb->next;
            pcb->next = NULL;
            break;
        }
    }

    pxping_timeout_del(pxping, pcb);
}


static struct ping_pcb *
pxping_pcb_for_request(struct pxping *pxping,
                       int is_ipv6, ipX_addr_t *src, ipX_addr_t *dst,
                       u16_t guest_id)
{
    struct ping_pcb *pcb;

    /* on lwip thread, so no concurrent updates */
    for (pcb = pxping->pcbs; pcb != NULL; pcb = pcb->next) {
        if (pcb->guest_id == guest_id
            && pcb->is_ipv6 == is_ipv6
            && ipX_addr_cmp(is_ipv6, &pcb->dst, dst)
            && ipX_addr_cmp(is_ipv6, &pcb->src, src))
        {
            break;
        }
    }

    if (pcb == NULL) {
        int mapped;

        pcb = pxping_pcb_allocate(pxping);
        if (pcb == NULL) {
            return NULL;
        }

        pcb->is_ipv6 = is_ipv6;
        ipX_addr_copy(is_ipv6, pcb->src, *src);
        ipX_addr_copy(is_ipv6, pcb->dst, *dst);

        pcb->guest_id = guest_id;
#ifdef RT_OS_WINDOWS
# define random() (rand())
#endif
        pcb->host_id = random() & 0xffffUL;

        pcb->pprev_timeout = NULL;
        pcb->next_timeout = NULL;

        if (is_ipv6) {
            pcb->peer.sin6.sin6_family = AF_INET6;
#if HAVE_SA_LEN
            pcb->peer.sin6.sin6_len = sizeof(pcb->peer.sin6);
#endif
            pcb->peer.sin6.sin6_port = htons(IPPROTO_ICMPV6);
            pcb->peer.sin6.sin6_flowinfo = 0;
            mapped = pxremap_outbound_ip6((ip6_addr_t *)&pcb->peer.sin6.sin6_addr,
                                          ipX_2_ip6(&pcb->dst));
        }
        else {
            pcb->peer.sin.sin_family = AF_INET;
#if HAVE_SA_LEN
            pcb->peer.sin.sin_len = sizeof(pcb->peer.sin);
#endif
            pcb->peer.sin.sin_port = htons(IPPROTO_ICMP);
            mapped = pxremap_outbound_ip4((ip_addr_t *)&pcb->peer.sin.sin_addr,
                                          ipX_2_ip(&pcb->dst));
        }

        if (mapped == PXREMAP_FAILED) {
            free(pcb);
            return NULL;
        }
        else {
            pcb->is_mapped = (mapped == PXREMAP_MAPPED);
        }

        pcb->timeout_slot = pxping->timeout_slot;

        sys_mutex_lock(&pxping->lock);
        pxping_pcb_register(pxping, pcb);
        sys_mutex_unlock(&pxping->lock);

        DPRINTF(("ping %p: %R[ping_pcb] - created\n", pcb, pcb));

        pxping_timer_needed(pxping);
    }
    else {
        /* just bump up expiration timeout lazily */
        DPRINTF(("ping %p: %R[ping_pcb] - slot %d -> %d\n",
                 pcb, pcb,
                 (unsigned int)pcb->timeout_slot,
                 (unsigned int)pxping->timeout_slot));
        pcb->timeout_slot = pxping->timeout_slot;
    }

    return pcb;
}


/* GCC 12.2.1 complains that array subscript is partly outside
 * of array bounds in expansion of ipX_addr_cmp. */
#if RT_GNUC_PREREQ(12, 0)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Warray-bounds"
#endif
/**
 * Called on pollmgr thread.  Caller must do the locking since caller
 * is going to use the returned pcb, which needs to be protected from
 * being expired by pxping_timer() on lwip thread.
 */
static struct ping_pcb *
pxping_pcb_for_reply(struct pxping *pxping,
                     int is_ipv6, ipX_addr_t *dst, u16_t host_id)
{
    struct ping_pcb *pcb;

    for (pcb = pxping->pcbs; pcb != NULL; pcb = pcb->next) {
        if (pcb->host_id == host_id
            && pcb->is_ipv6 == is_ipv6
            /* XXX: allow broadcast pings? */
            && ipX_addr_cmp(is_ipv6, &pcb->dst, dst))
        {
            return pcb;
        }
    }

    return NULL;
}
#if RT_GNUC_PREREQ(12, 0)
# pragma GCC diagnostic pop
#endif


static void
pxping_timer(void *arg)
{
    struct pxping *pxping = (struct pxping *)arg;
    struct ping_pcb **chain, *pcb;

    pxping->timer_active = 0;

    /*
     * New slot points to the list of pcbs to check for expiration.
     */
    LWIP_ASSERT1(pxping->timeout_slot < TIMEOUT);
    if (++pxping->timeout_slot == TIMEOUT) {
        pxping->timeout_slot = 0;
    }

    chain = &pxping->timeout_list[pxping->timeout_slot];
    pcb = *chain;

    /* protect from pollmgr concurrent reads */
    sys_mutex_lock(&pxping->lock);

    while (pcb != NULL) {
        struct ping_pcb *xpcb = pcb;
        pcb = pcb->next_timeout;

        if (xpcb->timeout_slot == pxping->timeout_slot) {
            /* expired */
            pxping_pcb_deregister(pxping, xpcb);
            pxping_pcb_delete(pxping, xpcb);
        }
        else {
            /*
             * If there was another request, we updated timeout_slot
             * but delayed actually moving the pcb until now.
             */
            pxping_timeout_del(pxping, xpcb); /* from current slot */
            pxping_timeout_add(pxping, xpcb); /* to new slot */
        }
    }

    sys_mutex_unlock(&pxping->lock);
    pxping_timer_needed(pxping);
}


static void
pxping_timer_needed(struct pxping *pxping)
{
    if (!pxping->timer_active && pxping->pcbs != NULL) {
        pxping->timer_active = 1;
        sys_timeout(1 * 1000, pxping_timer, pxping);
    }
}


static int
pxping_pmgr_pump(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxping *pxping;

    pxping = (struct pxping *)handler->data;
    LWIP_ASSERT1(fd == pxping->sock4 || fd == pxping->sock6);

    if (revents & ~(POLLIN|POLLERR)) {
        DPRINTF0(("%s: unexpected revents 0x%x\n", __func__, revents));
        return POLLIN;
    }

    if (revents & POLLERR) {
        int sockerr = -1;
        socklen_t optlen = (socklen_t)sizeof(sockerr);
        int status;

        status = getsockopt(fd, SOL_SOCKET,
                            SO_ERROR, (char *)&sockerr, &optlen);
        if (status < 0) {
            DPRINTF(("%s: sock %d: SO_ERROR failed: %R[sockerr]\n",
                     __func__, fd, SOCKERRNO()));
        }
        else {
            DPRINTF(("%s: sock %d: %R[sockerr]\n",
                     __func__, fd, sockerr));
        }
    }

    if ((revents & POLLIN) == 0) {
        return POLLIN;
    }

    if (fd == pxping->sock4) {
        pxping_pmgr_icmp4(pxping);
    }
    else /* fd == pxping->sock6 */ {
        pxping_pmgr_icmp6(pxping);
    }

    return POLLIN;
}


/**
 * Process incoming ICMP message for the host.
 * NB: we will get a lot of spam here and have to sift through it.
 */
static void
pxping_pmgr_icmp4(struct pxping *pxping)
{
    struct sockaddr_in sin;
    socklen_t salen = sizeof(sin);
    ssize_t nread;
    struct ip_hdr *iph;
    struct icmp_echo_hdr *icmph;
    u16_t iplen, ipoff;

    memset(&sin, 0, sizeof(sin));

    /*
     * Reads from raw IPv4 sockets deliver complete IP datagrams with
     * IP header included.
     */
    nread = recvfrom(pxping->sock4, pollmgr_udpbuf, sizeof(pollmgr_udpbuf), 0,
                     (struct sockaddr *)&sin, &salen);
    if (nread < 0) {
        DPRINTF(("%s: %R[sockerr]\n", __func__, SOCKERRNO()));
        return;
    }

    if (nread < IP_HLEN) {
        DPRINTF2(("%s: read %d bytes, IP header truncated\n",
                  __func__, (unsigned int)nread));
        return;
    }

    iph = (struct ip_hdr *)pollmgr_udpbuf;

    /* match version */
    if (IPH_V(iph) != 4) {
        DPRINTF2(("%s: unexpected IP version %d\n", __func__, IPH_V(iph)));
        return;
    }

    /* no fragmentation */
    ipoff = IPH_OFFSET(iph);
#if defined(RT_OS_DARWIN)
    /* darwin reports IPH_OFFSET in host byte order */
    ipoff = htons(ipoff);
    IPH_OFFSET_SET(iph, ipoff);
#endif
    if ((ipoff & PP_HTONS(IP_OFFMASK | IP_MF)) != 0) {
        DPRINTF2(("%s: dropping fragmented datagram (0x%04x)\n",
                  __func__, ntohs(ipoff)));
        return;
    }

    /* no options */
    if (IPH_HL(iph) * 4 != IP_HLEN) {
        DPRINTF2(("%s: dropping datagram with options (IP header length %d)\n",
                  __func__, IPH_HL(iph) * 4));
        return;
    }

    if (IPH_PROTO(iph) != IP_PROTO_ICMP) {
        DPRINTF2(("%s: unexpected protocol %d\n", __func__, IPH_PROTO(iph)));
        return;
    }

    iplen = IPH_LEN(iph);
#if !defined(RT_OS_DARWIN)
    /* darwin reports IPH_LEN in host byte order */
    iplen = ntohs(iplen);
#endif
#if defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS)
    /* darwin and solaris change IPH_LEN to payload length only */
    iplen += IP_HLEN;           /* we verified there are no options */
    IPH_LEN_SET(iph, htons(iplen));
#endif
    if (nread < iplen) {
        DPRINTF2(("%s: read %d bytes but total length is %d bytes\n",
                  __func__, (unsigned int)nread, (unsigned int)iplen));
        return;
    }

    if (iplen < IP_HLEN + ICMP_HLEN) {
        DPRINTF2(("%s: IP length %d bytes, ICMP header truncated\n",
                  __func__, iplen));
        return;
    }

    icmph = (struct icmp_echo_hdr *)(pollmgr_udpbuf + IP_HLEN);
    if (ICMPH_TYPE(icmph) == ICMP_ER) {
        pxping_pmgr_icmp4_echo(pxping, iplen, &sin);
    }
    else if (ICMPH_TYPE(icmph) == ICMP_DUR || ICMPH_TYPE(icmph) == ICMP_TE) {
        pxping_pmgr_icmp4_error(pxping, iplen, &sin);
    }
#if 1
    else {
        DPRINTF2(("%s: ignoring ICMP type %d\n", __func__, ICMPH_TYPE(icmph)));
    }
#endif
}


/**
 * Check if this incoming ICMP echo reply is for one of our pings and
 * forward it to the guest.
 */
static void
pxping_pmgr_icmp4_echo(struct pxping *pxping,
                       u16_t iplen, struct sockaddr_in *peer)
{
    struct ip_hdr *iph;
    struct icmp_echo_hdr *icmph;
    u16_t id;
    ip_addr_t guest_ip, target_ip;
    int mapped;
    struct ping_pcb *pcb;
    u16_t guest_id;
    u16_t oipsum;
    u32_t sum;
    RT_NOREF(peer);

    iph = (struct ip_hdr *)pollmgr_udpbuf;
    icmph = (struct icmp_echo_hdr *)(pollmgr_udpbuf + IP_HLEN);

    id  = icmph->id;
    DPRINTF(("<--- PING %RTnaipv4 id 0x%x seq %d\n",
             peer->sin_addr.s_addr, ntohs(id), ntohs(icmph->seqno)));

    /*
     * Is this a reply to one of our pings?
     */

    ip_addr_copy(target_ip, iph->src);
    mapped = pxremap_inbound_ip4(&target_ip, &target_ip);
    if (mapped == PXREMAP_FAILED) {
        return;
    }
    if (mapped == PXREMAP_ASIS && IPH_TTL(iph) == 1) {
        DPRINTF2(("%s: dropping packet with ttl 1\n", __func__));
        return;
    }

    sys_mutex_lock(&pxping->lock);
    pcb = pxping_pcb_for_reply(pxping, 0, ip_2_ipX(&target_ip), id);
    if (pcb == NULL) {
        sys_mutex_unlock(&pxping->lock);
        DPRINTF2(("%s: no match\n", __func__));
        return;
    }

    DPRINTF2(("%s: pcb %p\n", __func__, (void *)pcb));

    /* save info before unlocking since pcb may expire */
    ip_addr_copy(guest_ip, *ipX_2_ip(&pcb->src));
    guest_id = pcb->guest_id;

    sys_mutex_unlock(&pxping->lock);


    /*
     * Rewrite headers and forward to guest.
     */

    /* rewrite ICMP echo header */
    sum = (u16_t)~icmph->chksum;
    sum += chksum_update_16(&icmph->id, guest_id);
    sum = FOLD_U32T(sum);
    icmph->chksum = ~sum;

    /* rewrite IP header */
    oipsum = IPH_CHKSUM(iph);
    if (oipsum == 0) {
        /* Solaris doesn't compute checksum for local replies */
        ip_addr_copy(iph->dest, guest_ip);
        if (mapped == PXREMAP_MAPPED) {
            ip_addr_copy(iph->src, target_ip);
        }
        else {
            IPH_TTL_SET(iph, IPH_TTL(iph) - 1);
        }
        IPH_CHKSUM_SET(iph, inet_chksum(iph, ntohs(IPH_LEN(iph))));
    }
    else {
        sum = (u16_t)~oipsum;
        sum += chksum_update_32((u32_t *)&iph->dest,
                                ip4_addr_get_u32(&guest_ip));
        if (mapped == PXREMAP_MAPPED) {
            sum += chksum_update_32((u32_t *)&iph->src,
                                    ip4_addr_get_u32(&target_ip));
        }
        else {
            IPH_TTL_SET(iph, IPH_TTL(iph) - 1);
            sum += PP_NTOHS(~0x0100);
        }
        sum = FOLD_U32T(sum);
        IPH_CHKSUM_SET(iph, ~sum);
    }

    pxping_pmgr_forward_inbound(pxping, iplen);
}


/**
 * Check if this incoming ICMP error (destination unreachable or time
 * exceeded) is about one of our pings and forward it to the guest.
 */
static void
pxping_pmgr_icmp4_error(struct pxping *pxping,
                        u16_t iplen, struct sockaddr_in *peer)
{
    struct ip_hdr *iph, *oiph;
    struct icmp_echo_hdr *icmph, *oicmph;
    u16_t oipoff, oiphlen, oiplen;
    u16_t id;
    ip_addr_t guest_ip, target_ip, error_ip;
    int target_mapped, error_mapped;
    struct ping_pcb *pcb;
    u16_t guest_id;
    u32_t sum;
    RT_NOREF(peer);

    iph = (struct ip_hdr *)pollmgr_udpbuf;
    icmph = (struct icmp_echo_hdr *)(pollmgr_udpbuf + IP_HLEN);

    /*
     * Inner IP datagram is not checked by the kernel and may be
     * anything, possibly malicious.
     */

    oipoff = IP_HLEN + ICMP_HLEN;
    oiplen = iplen - oipoff; /* NB: truncated length, not IPH_LEN(oiph) */
    if (oiplen < IP_HLEN) {
        DPRINTF2(("%s: original datagram truncated to %d bytes\n",
                  __func__, oiplen));
    }

    /* IP header of the original message */
    oiph = (struct ip_hdr *)(pollmgr_udpbuf + oipoff);

    /* match version */
    if (IPH_V(oiph) != 4) {
        DPRINTF2(("%s: unexpected IP version %d\n", __func__, IPH_V(oiph)));
        return;
    }

    /* can't match fragments except the first one */
    if ((IPH_OFFSET(oiph) & PP_HTONS(IP_OFFMASK)) != 0) {
        DPRINTF2(("%s: ignoring fragment with offset %d\n",
                  __func__, ntohs(IPH_OFFSET(oiph) & PP_HTONS(IP_OFFMASK))));
        return;
    }

    if (IPH_PROTO(oiph) != IP_PROTO_ICMP) {
#if 0
        /* don't spam with every "destination unreachable" in the system */
        DPRINTF2(("%s: ignoring protocol %d\n", __func__, IPH_PROTO(oiph)));
#endif
        return;
    }

    oiphlen = IPH_HL(oiph) * 4;
    if (oiplen < oiphlen + ICMP_HLEN) {
        DPRINTF2(("%s: original datagram truncated to %d bytes\n",
                  __func__, oiplen));
        return;
    }

    oicmph = (struct icmp_echo_hdr *)(pollmgr_udpbuf + oipoff + oiphlen);
    if (ICMPH_TYPE(oicmph) != ICMP_ECHO) {
        DPRINTF2(("%s: ignoring ICMP error for original ICMP type %d\n",
                  __func__, ICMPH_TYPE(oicmph)));
        return;
    }

    id  = oicmph->id;

    DPRINTF2(("%s: ping %RTnaipv4 id 0x%x seq %d",
              __func__, ip4_addr_get_u32(&oiph->dest), ntohs(id), ntohs(oicmph->seqno)));
    if (ICMPH_TYPE(icmph) == ICMP_DUR) {
        DPRINTF2((" unreachable (code %d)\n", ICMPH_CODE(icmph)));
    }
    else {
        DPRINTF2((" time exceeded\n"));
    }


    /*
     * Is the inner (failed) datagram one of our pings?
     */

    ip_addr_copy(target_ip, oiph->dest); /* inner (failed) */
    target_mapped = pxremap_inbound_ip4(&target_ip, &target_ip);
    if (target_mapped == PXREMAP_FAILED) {
        return;
    }

    sys_mutex_lock(&pxping->lock);
    pcb = pxping_pcb_for_reply(pxping, 0, ip_2_ipX(&target_ip), id);
    if (pcb == NULL) {
        sys_mutex_unlock(&pxping->lock);
        DPRINTF2(("%s: no match\n", __func__));
        return;
    }

    DPRINTF2(("%s: pcb %p\n", __func__, (void *)pcb));

    /* save info before unlocking since pcb may expire */
    ip_addr_copy(guest_ip, *ipX_2_ip(&pcb->src));
    guest_id = pcb->guest_id;

    sys_mutex_unlock(&pxping->lock);


    /*
     * Rewrite both inner and outer headers and forward to guest.
     * Note that the checksum of the outer ICMP error message is
     * preserved by the changes we do to inner headers.
     */

    ip_addr_copy(error_ip, iph->src); /* node that reports the error */
    error_mapped = pxremap_inbound_ip4(&error_ip, &error_ip);
    if (error_mapped == PXREMAP_FAILED) {
        return;
    }
    if (error_mapped == PXREMAP_ASIS && IPH_TTL(iph) == 1) {
        DPRINTF2(("%s: dropping packet with ttl 1\n", __func__));
        return;
    }

    /* rewrite inner ICMP echo header */
    sum = (u16_t)~oicmph->chksum;
    sum += chksum_update_16(&oicmph->id, guest_id);
    sum = FOLD_U32T(sum);
    oicmph->chksum = ~sum;

    /* rewrite inner IP header */
#if defined(RT_OS_DARWIN)
    /* darwin converts inner length to host byte order too */
    IPH_LEN_SET(oiph, htons(IPH_LEN(oiph)));
#endif
    sum = (u16_t)~IPH_CHKSUM(oiph);
    sum += chksum_update_32((u32_t *)&oiph->src, ip4_addr_get_u32(&guest_ip));
    if (target_mapped == PXREMAP_MAPPED) {
        sum += chksum_update_32((u32_t *)&oiph->dest, ip4_addr_get_u32(&target_ip));
    }
    sum = FOLD_U32T(sum);
    IPH_CHKSUM_SET(oiph, ~sum);

    /* rewrite outer IP header */
    sum = (u16_t)~IPH_CHKSUM(iph);
    sum += chksum_update_32((u32_t *)&iph->dest, ip4_addr_get_u32(&guest_ip));
    if (error_mapped == PXREMAP_MAPPED) {
        sum += chksum_update_32((u32_t *)&iph->src, ip4_addr_get_u32(&error_ip));
    }
    else {
        IPH_TTL_SET(iph, IPH_TTL(iph) - 1);
        sum += PP_NTOHS(~0x0100);
    }
    sum = FOLD_U32T(sum);
    IPH_CHKSUM_SET(iph, ~sum);

    pxping_pmgr_forward_inbound(pxping, iplen);
}


/**
 * Process incoming ICMPv6 message for the host.
 * NB: we will get a lot of spam here and have to sift through it.
 */
static void
pxping_pmgr_icmp6(struct pxping *pxping)
{
#ifndef RT_OS_WINDOWS
    struct msghdr mh;
    ssize_t nread;
#else
    WSAMSG mh;
    DWORD nread;
#endif
    IOVEC iov[1];
    static u8_t cmsgbuf[128];
    struct cmsghdr *cmh;
    struct sockaddr_in6 sin6;
    /* socklen_t salen = sizeof(sin6); - unused */
    struct icmp6_echo_hdr *icmph;
    struct in6_pktinfo *pktinfo;
    int hopl, tclass;
#ifdef RT_OS_WINDOWS
    int status;
#endif

    /*
     * Reads from raw IPv6 sockets deliver only the payload.  Full
     * headers are available via recvmsg(2)/cmsg(3).
     */
    IOVEC_SET_BASE(iov[0], pollmgr_udpbuf);
    IOVEC_SET_LEN(iov[0], sizeof(pollmgr_udpbuf));

    memset(&mh, 0, sizeof(mh));
#ifndef RT_OS_WINDOWS
    mh.msg_name = &sin6;
    mh.msg_namelen = sizeof(sin6);
    mh.msg_iov = iov;
    mh.msg_iovlen = 1;
    mh.msg_control = cmsgbuf;
    mh.msg_controllen = sizeof(cmsgbuf);
    mh.msg_flags = 0;

    nread = recvmsg(pxping->sock6, &mh, 0);
    if (nread < 0) {
        DPRINTF(("%s: %R[sockerr]\n", __func__, SOCKERRNO()));
        return;
    }
#else  /* RT_OS_WINDOWS */
    mh.name = (LPSOCKADDR)&sin6;
    mh.namelen = sizeof(sin6);
    mh.lpBuffers = iov;
    mh.dwBufferCount = 1;
    mh.Control.buf = cmsgbuf;
    mh.Control.len = sizeof(cmsgbuf);
    mh.dwFlags = 0;

    status = (*pxping->pfWSARecvMsg6)(pxping->sock6, &mh, &nread, NULL, NULL);
    if (status == SOCKET_ERROR) {
        DPRINTF2(("%s: error %d\n", __func__, WSAGetLastError()));
        return;
    }
#endif

    icmph = (struct icmp6_echo_hdr *)pollmgr_udpbuf;

    DPRINTF2(("%s: %RTnaipv6 ICMPv6: ", __func__, &sin6.sin6_addr));

    if (icmph->type == ICMP6_TYPE_EREP) {
        DPRINTF2(("echo reply %04x %u\n",
                  (unsigned int)icmph->id, (unsigned int)icmph->seqno));
    }
    else { /* XXX */
        if (icmph->type == ICMP6_TYPE_EREQ) {
            DPRINTF2(("echo request %04x %u\n",
                      (unsigned int)icmph->id, (unsigned int)icmph->seqno));
        }
        else if (icmph->type == ICMP6_TYPE_DUR) {
            DPRINTF2(("destination unreachable\n"));
        }
        else if (icmph->type == ICMP6_TYPE_PTB) {
            DPRINTF2(("packet too big\n"));
        }
        else if (icmph->type == ICMP6_TYPE_TE) {
            DPRINTF2(("time exceeded\n"));
        }
        else if (icmph->type == ICMP6_TYPE_PP) {
            DPRINTF2(("parameter problem\n"));
        }
        else {
            DPRINTF2(("type %d len %u\n", icmph->type, (unsigned int)nread));
        }

        if (icmph->type >= ICMP6_TYPE_EREQ) {
            return;             /* informational message */
        }
    }

    pktinfo = NULL;
    hopl = -1;
    tclass = -1;
    for (cmh = CMSG_FIRSTHDR(&mh); cmh != NULL; cmh = CMSG_NXTHDR(&mh, cmh)) {
        if (cmh->cmsg_len == 0)
            break;

        if (cmh->cmsg_level == IPPROTO_IPV6
            && cmh->cmsg_type == IPV6_HOPLIMIT
            && cmh->cmsg_len == CMSG_LEN(sizeof(int)))
        {
            hopl = *(int *)CMSG_DATA(cmh);
            DPRINTF2(("hoplimit = %d\n", hopl));
        }

        if (cmh->cmsg_level == IPPROTO_IPV6
            && cmh->cmsg_type == IPV6_PKTINFO
            && cmh->cmsg_len == CMSG_LEN(sizeof(struct in6_pktinfo)))
        {
            pktinfo = (struct in6_pktinfo *)CMSG_DATA(cmh);
            DPRINTF2(("pktinfo found\n"));
        }
    }

    if (pktinfo == NULL) {
        /*
         * ip6_output_if() doesn't do checksum for us so we need to
         * manually recompute it - for this we must know the
         * destination address of the pseudo-header that we will
         * rewrite with guest's address.  (TODO: yeah, yeah, we can
         * compute it from scratch...)
         */
        DPRINTF2(("%s: unable to get pktinfo\n", __func__));
        return;
    }

    if (hopl < 0) {
        hopl = LWIP_ICMP6_HL;
    }

    if (icmph->type == ICMP6_TYPE_EREP) {
        pxping_pmgr_icmp6_echo(pxping,
                               (ip6_addr_t *)&sin6.sin6_addr,
                               (ip6_addr_t *)&pktinfo->ipi6_addr,
                               hopl, tclass, (u16_t)nread);
    }
    else if (icmph->type < ICMP6_TYPE_EREQ) {
        pxping_pmgr_icmp6_error(pxping,
                                (ip6_addr_t *)&sin6.sin6_addr,
                                (ip6_addr_t *)&pktinfo->ipi6_addr,
                                hopl, tclass, (u16_t)nread);
    }
}


/**
 * Check if this incoming ICMPv6 echo reply is for one of our pings
 * and forward it to the guest.
 */
static void
pxping_pmgr_icmp6_echo(struct pxping *pxping,
                       ip6_addr_t *src, ip6_addr_t *dst,
                       int hopl, int tclass, u16_t icmplen)
{
    struct icmp6_echo_hdr *icmph;
    ip6_addr_t guest_ip, target_ip;
    int mapped;
    struct ping_pcb *pcb;
    u16_t id, guest_id;
    u32_t sum;

    ip6_addr_copy(target_ip, *src);
    mapped = pxremap_inbound_ip6(&target_ip, &target_ip);
    if (mapped == PXREMAP_FAILED) {
        return;
    }
    else if (mapped == PXREMAP_ASIS) {
        if (hopl == 1) {
            DPRINTF2(("%s: dropping packet with ttl 1\n", __func__));
            return;
        }
        --hopl;
    }

    icmph = (struct icmp6_echo_hdr *)pollmgr_udpbuf;
    id = icmph->id;

    sys_mutex_lock(&pxping->lock);
    pcb = pxping_pcb_for_reply(pxping, 1, ip6_2_ipX(&target_ip), id);
    if (pcb == NULL) {
        sys_mutex_unlock(&pxping->lock);
        DPRINTF2(("%s: no match\n", __func__));
        return;
    }

    DPRINTF2(("%s: pcb %p\n", __func__, (void *)pcb));

    /* save info before unlocking since pcb may expire */
    ip6_addr_copy(guest_ip, *ipX_2_ip6(&pcb->src));
    guest_id = pcb->guest_id;

    sys_mutex_unlock(&pxping->lock);

    /* rewrite ICMPv6 echo header */
    sum = (u16_t)~icmph->chksum;
    sum += chksum_update_16(&icmph->id, guest_id);
    sum += chksum_delta_ipv6(dst, &guest_ip); /* pseudo */
    if (mapped) {
        sum += chksum_delta_ipv6(src, &target_ip); /* pseudo */
    }
    sum = FOLD_U32T(sum);
    icmph->chksum = ~sum;

    pxping_pmgr_forward_inbound6(pxping,
                                 &target_ip, /* echo reply src */
                                 &guest_ip, /* echo reply dst */
                                 hopl, tclass, icmplen);
}


/**
 * Check if this incoming ICMPv6 error is about one of our pings and
 * forward it to the guest.
 */
static void
pxping_pmgr_icmp6_error(struct pxping *pxping,
                        ip6_addr_t *src, ip6_addr_t *dst,
                        int hopl, int tclass, u16_t icmplen)
{
    struct icmp6_hdr *icmph;
    u8_t *bufptr;
    size_t buflen, hlen;
    int proto;
    struct ip6_hdr *oiph;
    struct icmp6_echo_hdr *oicmph;
    struct ping_pcb *pcb;
    ip6_addr_t guest_ip, target_ip, error_ip;
    int target_mapped, error_mapped;
    u16_t guest_id;
    u32_t sum;

    icmph = (struct icmp6_hdr *)pollmgr_udpbuf;

    /*
     * Inner IP datagram is not checked by the kernel and may be
     * anything, possibly malicious.
     */
    oiph = NULL;
    oicmph = NULL;

    bufptr = pollmgr_udpbuf;
    buflen = icmplen;

    hlen = sizeof(*icmph);
    proto = IP6_NEXTH_ENCAPS; /* i.e. IPv6, lwIP's name is unfortuate */
    for (;;) {
        if (hlen > buflen) {
            DPRINTF2(("truncated datagram inside ICMPv6 error message is too short\n"));
            return;
        }
        buflen -= hlen;
        bufptr += hlen;

        if (proto == IP6_NEXTH_ENCAPS && oiph == NULL) { /* outermost IPv6 */
            oiph = (struct ip6_hdr *)bufptr;
            if (IP6H_V(oiph) != 6) {
                DPRINTF2(("%s: unexpected IP version %d\n", __func__, IP6H_V(oiph)));
                return;
            }

            proto = IP6H_NEXTH(oiph);
            hlen = IP6_HLEN;
        }
        else if (proto == IP6_NEXTH_ICMP6) {
            oicmph = (struct icmp6_echo_hdr *)bufptr;
            break;
        }
        else if (proto == IP6_NEXTH_ROUTING
                 || proto == IP6_NEXTH_HOPBYHOP
                 || proto == IP6_NEXTH_DESTOPTS)
        {
            proto = bufptr[0];
            hlen = (bufptr[1] + 1) * 8;
        }
        else {
            DPRINTF2(("%s: stopping at protocol %d\n", __func__, proto));
            break;
        }
    }

    if (oiph == NULL || oicmph == NULL) {
        return;
    }

    if (buflen < sizeof(*oicmph)) {
        DPRINTF2(("%s: original ICMPv6 is truncated too short\n", __func__));
        return;
    }

    if (oicmph->type != ICMP6_TYPE_EREQ) {
        DPRINTF2(("%s: ignoring original ICMPv6 type %d\n", __func__, oicmph->type));
        return;
    }

    ip6_addr_copy(target_ip, oiph->dest); /* inner (failed) */
    target_mapped = pxremap_inbound_ip6(&target_ip, &target_ip);
    if (target_mapped == PXREMAP_FAILED) {
        return;
    }

    sys_mutex_lock(&pxping->lock);
    pcb = pxping_pcb_for_reply(pxping, 1, ip6_2_ipX(&target_ip), oicmph->id);
    if (pcb == NULL) {
        sys_mutex_unlock(&pxping->lock);
        DPRINTF2(("%s: no match\n", __func__));
        return;
    }

    DPRINTF2(("%s: pcb %p\n", __func__, (void *)pcb));

    /* save info before unlocking since pcb may expire */
    ip6_addr_copy(guest_ip, *ipX_2_ip6(&pcb->src));
    guest_id = pcb->guest_id;

    sys_mutex_unlock(&pxping->lock);


    /*
     * Rewrite inner and outer headers and forward to guest.  Note
     * that IPv6 has no IP header checksum, but uses pseudo-header for
     * ICMPv6, so we update both in one go, adjusting ICMPv6 checksum
     * as we rewrite IP header.
     */

    ip6_addr_copy(error_ip, *src); /* node that reports the error */
    error_mapped = pxremap_inbound_ip6(&error_ip, &error_ip);
    if (error_mapped == PXREMAP_FAILED) {
        return;
    }
    if (error_mapped == PXREMAP_ASIS && hopl == 1) {
        DPRINTF2(("%s: dropping packet with ttl 1\n", __func__));
        return;
    }

    /* rewrite inner ICMPv6 echo header and inner IPv6 header */
    sum = (u16_t)~oicmph->chksum;
    sum += chksum_update_16(&oicmph->id, guest_id);
    sum += chksum_update_ipv6((ip6_addr_t *)&oiph->src, &guest_ip);
    if (target_mapped) {
        sum += chksum_delta_ipv6((ip6_addr_t *)&oiph->dest, &target_ip);
    }
    sum = FOLD_U32T(sum);
    oicmph->chksum = ~sum;

    /* rewrite outer ICMPv6 error header */
    sum = (u16_t)~icmph->chksum;
    sum += chksum_delta_ipv6(dst, &guest_ip); /* pseudo */
    if (error_mapped) {
        sum += chksum_delta_ipv6(src, &error_ip); /* pseudo */
    }
    sum = FOLD_U32T(sum);
    icmph->chksum = ~sum;

    pxping_pmgr_forward_inbound6(pxping,
                                 &error_ip, /* error src */
                                 &guest_ip, /* error dst */
                                 hopl, tclass, icmplen);
}


/**
 * Hand off ICMP datagram to the lwip thread where it will be
 * forwarded to the guest.
 *
 * We no longer need ping_pcb.  The pcb may get expired on the lwip
 * thread, but we have already patched necessary information into the
 * datagram.
 */
static void
pxping_pmgr_forward_inbound(struct pxping *pxping, u16_t iplen)
{
    struct pbuf *p;
    struct ping_msg *msg;
    err_t error;

    p = pbuf_alloc(PBUF_LINK, iplen, PBUF_RAM);
    if (p == NULL) {
        DPRINTF(("%s: pbuf_alloc(%d) failed\n",
                 __func__, (unsigned int)iplen));
        return;
    }

    error = pbuf_take(p, pollmgr_udpbuf, iplen);
    if (error != ERR_OK) {
        DPRINTF(("%s: pbuf_take(%d) failed\n",
                 __func__, (unsigned int)iplen));
        pbuf_free(p);
        return;
    }

    msg = (struct ping_msg *)malloc(sizeof(*msg));
    if (msg == NULL) {
        pbuf_free(p);
        return;
    }

    msg->msg.type = TCPIP_MSG_CALLBACK_STATIC;
    msg->msg.sem = NULL;
    msg->msg.msg.cb.function = pxping_pcb_forward_inbound;
    msg->msg.msg.cb.ctx = (void *)msg;

    msg->pxping = pxping;
    msg->p = p;

    proxy_lwip_post(&msg->msg);
}


static void
pxping_pcb_forward_inbound(void *arg)
{
    struct ping_msg *msg = (struct ping_msg *)arg;
    err_t error;

    LWIP_ASSERT1(msg != NULL);
    LWIP_ASSERT1(msg->pxping != NULL);
    LWIP_ASSERT1(msg->p != NULL);

    error = ip_raw_output_if(msg->p, msg->pxping->netif);
    if (error != ERR_OK) {
        DPRINTF(("%s: ip_output_if: %s\n",
                 __func__, proxy_lwip_strerr(error)));
    }
    pbuf_free(msg->p);
    free(msg);
}


static void
pxping_pmgr_forward_inbound6(struct pxping *pxping,
                             ip6_addr_t *src, ip6_addr_t *dst,
                             u8_t hopl, u8_t tclass,
                             u16_t icmplen)
{
    struct pbuf *p;
    struct ping6_msg *msg;

    err_t error;

    p = pbuf_alloc(PBUF_IP, icmplen, PBUF_RAM);
    if (p == NULL) {
        DPRINTF(("%s: pbuf_alloc(%d) failed\n",
                 __func__, (unsigned int)icmplen));
        return;
    }

    error = pbuf_take(p, pollmgr_udpbuf, icmplen);
    if (error != ERR_OK) {
        DPRINTF(("%s: pbuf_take(%d) failed\n",
                 __func__, (unsigned int)icmplen));
        pbuf_free(p);
        return;
    }

    msg = (struct ping6_msg *)malloc(sizeof(*msg));
    if (msg == NULL) {
        pbuf_free(p);
        return;
    }

    msg->msg.type = TCPIP_MSG_CALLBACK_STATIC;
    msg->msg.sem = NULL;
    msg->msg.msg.cb.function = pxping_pcb_forward_inbound6;
    msg->msg.msg.cb.ctx = (void *)msg;

    msg->pxping = pxping;
    msg->p = p;
    ip6_addr_copy(msg->src, *src);
    ip6_addr_copy(msg->dst, *dst);
    msg->hopl = hopl;
    msg->tclass = tclass;

    proxy_lwip_post(&msg->msg);
}


static void
pxping_pcb_forward_inbound6(void *arg)
{
    struct ping6_msg *msg = (struct ping6_msg *)arg;
    err_t error;

    LWIP_ASSERT1(msg != NULL);
    LWIP_ASSERT1(msg->pxping != NULL);
    LWIP_ASSERT1(msg->p != NULL);

    error = ip6_output_if(msg->p,
                          &msg->src, &msg->dst, msg->hopl, msg->tclass,
                          IP6_NEXTH_ICMP6, msg->pxping->netif);
    if (error != ERR_OK) {
        DPRINTF(("%s: ip6_output_if: %s\n",
                 __func__, proxy_lwip_strerr(error)));
    }
    pbuf_free(msg->p);
    free(msg);
}
