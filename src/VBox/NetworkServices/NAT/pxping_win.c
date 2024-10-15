/* $Id: pxping_win.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - ping proxy, Windows ICMP API version.
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
#include "pxremap.h"

#include "lwip/ip.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"

/* XXX: lwIP names conflict with winsock <iphlpapi.h> */
#undef IP_STATS
#undef ICMP_STATS
#undef TCP_STATS
#undef UDP_STATS
#undef IP6_STATS

#include <winternl.h>           /* for PIO_APC_ROUTINE &c */
#ifndef PIO_APC_ROUTINE_DEFINED
# define PIO_APC_ROUTINE_DEFINED 1
#endif
#include <iprt/win/iphlpapi.h>
#include <icmpapi.h>

#include <stdio.h>


struct pxping {
    /*
     * We use single ICMP handle for all pings.  This means that all
     * proxied pings will have the same id and share single sequence
     * of sequence numbers.
     */
    HANDLE hdl4;
    HANDLE hdl6;

    struct netif *netif;

    /*
     * On Windows XP and Windows Server 2003 IcmpSendEcho2() callback
     * is FARPROC, but starting from Vista it's PIO_APC_ROUTINE with
     * two extra arguments.  Callbacks use WINAPI (stdcall) calling
     * convention with callee responsible for popping the arguments,
     * so to avoid stack corruption we check windows version at run
     * time and provide correct callback.
     */
    PIO_APC_ROUTINE pfnCallback4;
    PIO_APC_ROUTINE pfnCallback6;
};


struct pong4 {
    struct netif *netif;

    struct ip_hdr reqiph;
    struct icmp_echo_hdr reqicmph;

    size_t bufsize;
    u8_t buf[1];
};


struct pong6 {
    struct netif *netif;

    ip6_addr_t reqsrc;
    struct icmp6_echo_hdr reqicmph;
    size_t reqsize;

    size_t bufsize;
    u8_t buf[1];
};


static void pxping_recv4(void *arg, struct pbuf *p);
static void pxping_recv6(void *arg, struct pbuf *p);

static VOID WINAPI pxping_icmp4_callback_old(void *);
static VOID WINAPI pxping_icmp4_callback_apc(void *, PIO_STATUS_BLOCK, ULONG);
static void pxping_icmp4_callback(struct pong4 *pong);

static VOID WINAPI pxping_icmp6_callback_old(void *);
static VOID WINAPI pxping_icmp6_callback_apc(void *, PIO_STATUS_BLOCK, ULONG);
static void pxping_icmp6_callback(struct pong6 *pong);


struct pxping g_pxping;


err_t
pxping_init(struct netif *netif, SOCKET sock4, SOCKET sock6)
{
    OSVERSIONINFO osvi;
    int status;

    LWIP_UNUSED_ARG(sock4);
    LWIP_UNUSED_ARG(sock6);

    ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    status = GetVersionEx(&osvi);
    if (status == 0) {
        return ERR_ARG;
    }

    if (osvi.dwMajorVersion >= 6) {
        g_pxping.pfnCallback4 = pxping_icmp4_callback_apc;
        g_pxping.pfnCallback6 = pxping_icmp6_callback_apc;
    }
    else {
        g_pxping.pfnCallback4 = (PIO_APC_ROUTINE)pxping_icmp4_callback_old;
        g_pxping.pfnCallback6 = (PIO_APC_ROUTINE)pxping_icmp6_callback_old;
    }


    g_pxping.hdl4 = IcmpCreateFile();
    if (g_pxping.hdl4 != INVALID_HANDLE_VALUE) {
        ping_proxy_accept(pxping_recv4, &g_pxping);
    }
    else {
        DPRINTF(("IcmpCreateFile: error %d\n", GetLastError()));
    }

    g_pxping.hdl6 = Icmp6CreateFile();
    if (g_pxping.hdl6 != INVALID_HANDLE_VALUE) {
        ping6_proxy_accept(pxping_recv6, &g_pxping);
    }
    else {
        DPRINTF(("Icmp6CreateFile: error %d\n", GetLastError()));
    }

    if (g_pxping.hdl4 == INVALID_HANDLE_VALUE
        && g_pxping.hdl6 == INVALID_HANDLE_VALUE)
    {
        return ERR_ARG;
    }

    g_pxping.netif = netif;

    return ERR_OK;
}


/**
 * ICMP Echo Request in pbuf "p" is to be proxied.
 */
static void
pxping_recv4(void *arg, struct pbuf *p)
{
    struct pxping *pxping = (struct pxping *)arg;
    const struct ip_hdr *iph;
    const struct icmp_echo_hdr *icmph;
    u16_t iphlen;
    size_t bufsize;
    struct pong4 *pong;
    IPAddr dst;
    int mapped;
    int ttl;
    IP_OPTION_INFORMATION opts;
    void *reqdata;
    size_t reqsize;
    int status;

    pong = NULL;

    iphlen = ip_current_header_tot_len();
    if (RT_UNLIKELY(iphlen != IP_HLEN)) { /* we don't do options */
        goto out;
    }

    iph = (const struct ip_hdr *)ip_current_header();
    icmph = (const struct icmp_echo_hdr *)p->payload;

    mapped = pxremap_outbound_ip4((ip_addr_t *)&dst, (ip_addr_t *)&iph->dest);
    if (RT_UNLIKELY(mapped == PXREMAP_FAILED)) {
        goto out;
    }

    ttl = IPH_TTL(iph);
    if (mapped == PXREMAP_ASIS) {
        if (RT_UNLIKELY(ttl == 1)) {
            status = pbuf_header(p, iphlen); /* back to IP header */
            if (RT_LIKELY(status == 0)) {
                icmp_time_exceeded(p, ICMP_TE_TTL);
            }
            goto out;
        }
        --ttl;
    }

    status = pbuf_header(p, -(u16_t)sizeof(*icmph)); /* to ping payload */
    if (RT_UNLIKELY(status != 0)) {
        goto out;
    }

    bufsize = sizeof(ICMP_ECHO_REPLY);
    if (p->tot_len < sizeof(IO_STATUS_BLOCK) + sizeof(struct icmp_echo_hdr))
        bufsize += sizeof(IO_STATUS_BLOCK) + sizeof(struct icmp_echo_hdr);
    else
        bufsize += p->tot_len;
    bufsize += 16; /* whatever that is; empirically at least XP needs it */

    pong = (struct pong4 *)malloc(RT_UOFFSETOF(struct pong4, buf) + bufsize);
    if (RT_UNLIKELY(pong == NULL)) {
        goto out;
    }
    pong->bufsize = bufsize;
    pong->netif = pxping->netif;

    memcpy(&pong->reqiph, iph, sizeof(*iph));
    memcpy(&pong->reqicmph, icmph, sizeof(*icmph));

    reqsize = p->tot_len;
    if (p->next == NULL) {
        /* single pbuf can be directly used as request data source */
        reqdata = p->payload;
    }
    else {
        /* data from pbuf chain must be concatenated */
        pbuf_copy_partial(p, pong->buf, p->tot_len, 0);
        reqdata = pong->buf;
    }

    opts.Ttl = ttl;
    opts.Tos = IPH_TOS(iph); /* affected by DisableUserTOSSetting key */
    opts.Flags = (IPH_OFFSET(iph) & PP_HTONS(IP_DF)) != 0 ? IP_FLAG_DF : 0;
    opts.OptionsSize = 0;
    opts.OptionsData = 0;

    status = IcmpSendEcho2(pxping->hdl4, NULL,
                           pxping->pfnCallback4, pong,
                           dst, reqdata, (WORD)reqsize, &opts,
                           pong->buf, (DWORD)pong->bufsize,
                           5 * 1000 /* ms */);

    if (RT_UNLIKELY(status != 0)) {
        DPRINTF(("IcmpSendEcho2: unexpected status %d\n", status));
        goto out;
    }
    if ((status = GetLastError()) != ERROR_IO_PENDING) {
        int code;

        DPRINTF(("IcmpSendEcho2: error %d\n", status));
        switch (status) {
        case ERROR_NETWORK_UNREACHABLE:
            code = ICMP_DUR_NET;
            break;
        case ERROR_HOST_UNREACHABLE:
            code = ICMP_DUR_HOST;
            break;
        default:
            code = -1;
            break;
        }

        if (code != -1) {
            /* move payload back to IP header */
            status = pbuf_header(p, (u16_t)(sizeof(*icmph) + iphlen));
            if (RT_LIKELY(status == 0)) {
                icmp_dest_unreach(p, code);
            }
        }
        goto out;
    }

    pong = NULL;                /* callback owns it now */
  out:
    if (pong != NULL) {
        free(pong);
    }
    pbuf_free(p);
}


static VOID WINAPI
pxping_icmp4_callback_apc(void *ctx, PIO_STATUS_BLOCK iob, ULONG reserved)
{
    struct pong4 *pong = (struct pong4 *)ctx;
    LWIP_UNUSED_ARG(iob);
    LWIP_UNUSED_ARG(reserved);

    if (pong != NULL) {
        pxping_icmp4_callback(pong);
        free(pong);
    }
}


static VOID WINAPI
pxping_icmp4_callback_old(void *ctx)
{
    struct pong4 *pong = (struct pong4 *)ctx;

    if (pong != NULL) {
        pxping_icmp4_callback(pong);
        free(pong);
    }
}


static void
pxping_icmp4_callback(struct pong4 *pong)
{
    ICMP_ECHO_REPLY *reply;
    DWORD nreplies;
    size_t icmplen;
    struct pbuf *p;
    struct icmp_echo_hdr *icmph;
    ip_addr_t src;
    int mapped;

    nreplies = IcmpParseReplies(pong->buf, (DWORD)pong->bufsize);
    if (nreplies == 0) {
        DWORD error = GetLastError();
        if (error == IP_REQ_TIMED_OUT) {
            DPRINTF2(("pong4: %p timed out\n", (void *)pong));
        }
        else {
            DPRINTF(("pong4: %p: IcmpParseReplies: error %d\n",
                     (void *)pong, error));
        }
        return;
    }

    reply = (ICMP_ECHO_REPLY *)pong->buf;

    if (reply->Options.OptionsSize != 0) { /* don't do options */
        return;
    }

    mapped = pxremap_inbound_ip4(&src, (ip_addr_t *)&reply->Address);
    if (mapped == PXREMAP_FAILED) {
        return;
    }
    if (mapped == PXREMAP_ASIS) {
        if (reply->Options.Ttl == 1) {
            return;
        }
        --reply->Options.Ttl;
    }

    if (reply->Status == IP_SUCCESS) {
        icmplen = sizeof(struct icmp_echo_hdr) + reply->DataSize;
        if ((reply->Options.Flags & IP_FLAG_DF) != 0
            && IP_HLEN + icmplen > pong->netif->mtu)
        {
            return;
        }

        p = pbuf_alloc(PBUF_IP, (u16_t)icmplen, PBUF_RAM);
        if (RT_UNLIKELY(p == NULL)) {
            return;
        }

        icmph = (struct icmp_echo_hdr *)p->payload;
        icmph->type = ICMP_ER;
        icmph->code = 0;
        icmph->chksum = 0;
        icmph->id = pong->reqicmph.id;
        icmph->seqno = pong->reqicmph.seqno;

        memcpy((u8_t *)p->payload + sizeof(*icmph),
               reply->Data, reply->DataSize);
    }
    else {
        u8_t type, code;

        switch (reply->Status) {
        case IP_DEST_NET_UNREACHABLE:
            type = ICMP_DUR; code = ICMP_DUR_NET;
            break;
        case IP_DEST_HOST_UNREACHABLE:
            type = ICMP_DUR; code = ICMP_DUR_HOST;
            break;
        case IP_DEST_PROT_UNREACHABLE:
            type = ICMP_DUR; code = ICMP_DUR_PROTO;
            break;
        case IP_PACKET_TOO_BIG:
            type = ICMP_DUR; code = ICMP_DUR_FRAG;
            break;
        case IP_SOURCE_QUENCH:
            type = ICMP_SQ; code = 0;
            break;
        case IP_TTL_EXPIRED_TRANSIT:
            type = ICMP_TE; code = ICMP_TE_TTL;
            break;
        case IP_TTL_EXPIRED_REASSEM:
            type = ICMP_TE; code = ICMP_TE_FRAG;
            break;
        default:
            DPRINTF(("pong4: reply status %d, dropped\n", reply->Status));
            return;
        }

        DPRINTF(("pong4: reply status %d -> type %d/code %d\n",
                 reply->Status, type, code));

        icmplen = sizeof(*icmph) + sizeof(pong->reqiph) + sizeof(pong->reqicmph);

        p = pbuf_alloc(PBUF_IP, (u16_t)icmplen, PBUF_RAM);
        if (RT_UNLIKELY(p == NULL)) {
            return;
        }

        icmph = (struct icmp_echo_hdr *)p->payload;
        icmph->type = type;
        icmph->code = code;
        icmph->chksum = 0;
        icmph->id = 0;
        icmph->seqno = 0;

        /*
         * XXX: we don't know the TTL of the request at the time this
         * ICMP error was generated (we can guess it was 1 for ttl
         * exceeded, but don't bother faking it).
         */
        memcpy((u8_t *)p->payload + sizeof(*icmph),
               &pong->reqiph, sizeof(pong->reqiph));

        memcpy((u8_t *)p->payload + sizeof(*icmph) + sizeof(pong->reqiph),
               &pong->reqicmph, sizeof(pong->reqicmph));
    }

    icmph->chksum = inet_chksum(p->payload, (u16_t)icmplen);
    ip_output_if(p, &src,
                 (ip_addr_t *)&pong->reqiph.src, /* dst */
                 reply->Options.Ttl,
                 reply->Options.Tos,
                 IPPROTO_ICMP,
                 pong->netif);
    pbuf_free(p);
}


static void
pxping_recv6(void *arg, struct pbuf *p)
{
    struct pxping *pxping = (struct pxping *)arg;
    struct icmp6_echo_hdr *icmph;
    size_t bufsize;
    struct pong6 *pong;
    int mapped;
    void *reqdata;
    size_t reqsize;
    struct sockaddr_in6 src, dst;
    int hopl;
    IP_OPTION_INFORMATION opts;
    int status;

    pong = NULL;

    icmph = (struct icmp6_echo_hdr *)p->payload;

    memset(&dst, 0, sizeof(dst));
    dst.sin6_family = AF_INET6;
    mapped = pxremap_outbound_ip6((ip6_addr_t *)&dst.sin6_addr,
                                  ip6_current_dest_addr());
    if (RT_UNLIKELY(mapped == PXREMAP_FAILED)) {
        goto out;
    }

    hopl = IP6H_HOPLIM(ip6_current_header());
    if (mapped == PXREMAP_ASIS) {
        if (RT_UNLIKELY(hopl == 1)) {
            status = pbuf_header(p, ip_current_header_tot_len());
            if (RT_LIKELY(status == 0)) {
                icmp6_time_exceeded(p, ICMP6_TE_HL);
            }
            goto out;
        }
        --hopl;
    }

    status = pbuf_header(p, -(u16_t)sizeof(*icmph)); /* to ping payload */
    if (RT_UNLIKELY(status != 0)) {
        goto out;
    }

    /* XXX: parrotted from IPv4 version, not tested all os version/bitness */
    bufsize = sizeof(ICMPV6_ECHO_REPLY);
    if (p->tot_len < sizeof(IO_STATUS_BLOCK) + sizeof(struct icmp6_echo_hdr))
        bufsize += sizeof(IO_STATUS_BLOCK) + sizeof(struct icmp6_echo_hdr);
    else
        bufsize += p->tot_len;
    bufsize += 16;

    pong = (struct pong6 *)malloc(RT_UOFFSETOF(struct pong6, buf) + bufsize);
    if (RT_UNLIKELY(pong == NULL)) {
        goto out;
    }
    pong->bufsize = bufsize;
    pong->netif = pxping->netif;

    ip6_addr_copy(pong->reqsrc, *ip6_current_src_addr());
    memcpy(&pong->reqicmph, icmph, sizeof(*icmph));

    memset(pong->buf, 0xa5, pong->bufsize);

    pong->reqsize = reqsize = p->tot_len;
    if (p->next == NULL) {
        /* single pbuf can be directly used as request data source */
        reqdata = p->payload;
    }
    else {
        /* data from pbuf chain must be concatenated */
        pbuf_copy_partial(p, pong->buf, p->tot_len, 0);
        reqdata = pong->buf;
    }

    memset(&src, 0, sizeof(src));
    src.sin6_family = AF_INET6;
    src.sin6_addr = in6addr_any; /* let the OS select host source address */

    memset(&opts, 0, sizeof(opts));
    opts.Ttl = hopl;

    status = Icmp6SendEcho2(pxping->hdl6, NULL,
                            pxping->pfnCallback6, pong,
                            &src, &dst, reqdata, (WORD)reqsize, &opts,
                            pong->buf, (DWORD)pong->bufsize,
                            5 * 1000 /* ms */);

    if (RT_UNLIKELY(status != 0)) {
        DPRINTF(("Icmp6SendEcho2: unexpected status %d\n", status));
        goto out;
    }
    if ((status = GetLastError()) != ERROR_IO_PENDING) {
        int code;

        DPRINTF(("Icmp6SendEcho2: error %d\n", status));
        switch (status) {
        case ERROR_NETWORK_UNREACHABLE:
        case ERROR_HOST_UNREACHABLE:
            code = ICMP6_DUR_NO_ROUTE;
            break;
        default:
            code = -1;
            break;
        }

        if (code != -1) {
            /* move payload back to IP header */
            status = pbuf_header(p, (u16_t)(sizeof(*icmph)
                                            + ip_current_header_tot_len()));
            if (RT_LIKELY(status == 0)) {
                icmp6_dest_unreach(p, code);
            }
        }
        goto out;
    }

    pong = NULL;                /* callback owns it now */
  out:
    if (pong != NULL) {
        free(pong);
    }
    pbuf_free(p);
}


static VOID WINAPI
pxping_icmp6_callback_apc(void *ctx, PIO_STATUS_BLOCK iob, ULONG reserved)
{
    struct pong6 *pong = (struct pong6 *)ctx;
    LWIP_UNUSED_ARG(iob);
    LWIP_UNUSED_ARG(reserved);

    if (pong != NULL) {
        pxping_icmp6_callback(pong);
        free(pong);
    }
}


static VOID WINAPI
pxping_icmp6_callback_old(void *ctx)
{
    struct pong6 *pong = (struct pong6 *)ctx;

    if (pong != NULL) {
        pxping_icmp6_callback(pong);
        free(pong);
    }
}


static void
pxping_icmp6_callback(struct pong6 *pong)
{
    DWORD nreplies;
    ICMPV6_ECHO_REPLY *reply;
    struct pbuf *p;
    struct icmp6_echo_hdr *icmph;
    size_t icmplen;
    ip6_addr_t src;
    int mapped;

    nreplies = Icmp6ParseReplies(pong->buf, (DWORD)pong->bufsize);
    if (nreplies == 0) {
        DWORD error = GetLastError();
        if (error == IP_REQ_TIMED_OUT) {
            DPRINTF2(("pong6: %p timed out\n", (void *)pong));
        }
        else {
            DPRINTF(("pong6: %p: Icmp6ParseReplies: error %d\n",
                     (void *)pong, error));
        }
        return;
    }

    reply = (ICMPV6_ECHO_REPLY *)pong->buf;

    mapped = pxremap_inbound_ip6(&src, (ip6_addr_t *)reply->Address.sin6_addr);
    if (mapped == PXREMAP_FAILED) {
        return;
    }

    /*
     * Reply data follows ICMPV6_ECHO_REPLY structure in memory, but
     * it doesn't tell us its size.  Assume it's equal the size of the
     * request.
     */
    icmplen = sizeof(*icmph) + pong->reqsize;
    p = pbuf_alloc(PBUF_IP, (u16_t)icmplen, PBUF_RAM);
    if (RT_UNLIKELY(p == NULL)) {
        return;
    }

    icmph = (struct icmp6_echo_hdr *)p->payload;
    icmph->type = ICMP6_TYPE_EREP;
    icmph->code = 0;
    icmph->chksum = 0;
    icmph->id = pong->reqicmph.id;
    icmph->seqno = pong->reqicmph.seqno;

    memcpy((u8_t *)p->payload + sizeof(*icmph),
           pong->buf + sizeof(*reply), pong->reqsize);

    icmph->chksum = ip6_chksum_pseudo(p, IP6_NEXTH_ICMP6, p->tot_len,
                                      &src, &pong->reqsrc);
    ip6_output_if(p, /* :src */ &src, /* :dst */ &pong->reqsrc,
                  LWIP_ICMP6_HL, 0, IP6_NEXTH_ICMP6,
                  pong->netif);
    pbuf_free(p);
}
