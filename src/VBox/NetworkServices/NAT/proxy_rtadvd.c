/* $Id: proxy_rtadvd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - IPv6 router advertisement daemon.
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

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/timers.h"

#include "lwip/inet_chksum.h"
#include "lwip/icmp6.h"
#include "lwip/nd6.h"

#include "lwip/raw.h"

#include <string.h>


static void proxy_rtadvd_timer(void *);
static void proxy_rtadvd_send_multicast(struct netif *);
static void proxy_rtadvd_fill_payload(struct netif *, int);

static u8_t rtadvd_recv(void *, struct raw_pcb *, struct pbuf *, ip6_addr_t *);


/* ff02::1 - link-local all nodes multicast address */
static ip6_addr_t allnodes_linklocal = {
    { PP_HTONL(0xff020000UL), 0, 0, PP_HTONL(0x00000001UL) }
};


/*
 * Unsolicited Router Advertisement payload.
 *
 * NB: Since ICMP checksum covers pseudo-header with destination
 * address (link-local allnodes multicast in this case) this payload
 * cannot be used for solicited replies to unicast addresses.
 */
static unsigned int unsolicited_ra_payload_length;
static u8_t unsolicited_ra_payload[
      sizeof(struct ra_header)
      /* reserves enough space for NETIF_MAX_HWADDR_LEN */
    + sizeof(struct lladdr_option)
      /* we only announce one prefix */
    + sizeof(struct prefix_option) * 1
];


static int ndefaults = 0;

static struct raw_pcb *rtadvd_pcb;


void
proxy_rtadvd_start(struct netif *proxy_netif)
{
#if 0 /* XXX */
    ndefaults = rtmon_get_defaults();
#else
    ndefaults = g_proxy_options->ipv6_defroute;
#endif
    if (ndefaults < 0) {
        DPRINTF0(("rtadvd: failed to read IPv6 routing table, aborting\n"));
        return;
    }

    proxy_rtadvd_fill_payload(proxy_netif, ndefaults > 0);

    rtadvd_pcb = raw_new_ip6(IP6_NEXTH_ICMP6);
    if (rtadvd_pcb == NULL) {
        DPRINTF0(("rtadvd: failed to allocate pcb, aborting\n"));
        return;
    }

    /*
     * We cannot use raw_bind_ip6() since raw_input() doesn't grok
     * multicasts.  We are going to use ip6_output_if() directly.
     */
    raw_recv_ip6(rtadvd_pcb, rtadvd_recv, proxy_netif);

    sys_timeout(3 * 1000, proxy_rtadvd_timer, proxy_netif);
}


static int quick_ras = 2;


/**
 * lwIP thread callback invoked when we start/stop advertising default
 * route.
 */
void
proxy_rtadvd_do_quick(void *arg)
{
    struct netif *proxy_netif = (struct netif *)arg;

    quick_ras = 2;
    sys_untimeout(proxy_rtadvd_timer, proxy_netif);
    proxy_rtadvd_timer(proxy_netif); /* sends and re-arms */
}


static void
proxy_rtadvd_timer(void *arg)
{
    struct netif *proxy_netif = (struct netif *)arg;
    int newdefs;
    u32_t delay;

#if 0 /* XXX */
    newdefs = rtmon_get_defaults();
#else
    newdefs = g_proxy_options->ipv6_defroute;
#endif
    if (newdefs != ndefaults && newdefs != -1) {
        ndefaults = newdefs;
        proxy_rtadvd_fill_payload(proxy_netif, ndefaults > 0);
    }

    proxy_rtadvd_send_multicast(proxy_netif);

    if (quick_ras > 0) {
        --quick_ras;
        delay = 16 * 1000;
    }
    else {
        delay = 600 * 1000;
    }

    sys_timeout(delay, proxy_rtadvd_timer, proxy_netif);
}


/*
 * This should be folded into icmp6/nd6 input, but I don't want to
 * solve this in general, making it configurable, etc.
 *
 * Cf. RFC 4861:
 *   6.1.1. Validation of Router Solicitation Messages
 */
static u8_t
rtadvd_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, ip6_addr_t *addr)
{
    enum raw_recv_status { RAW_RECV_CONTINUE = 0, RAW_RECV_CONSUMED = 1 };

    struct netif *proxy_netif = (struct netif *)arg;
    struct ip6_hdr *ip6_hdr;
    struct icmp6_hdr *icmp6_hdr;
    struct lladdr_option *lladdr_opt;
    void *option;
    u8_t opttype, optlen8;

    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(addr);

    /* save a pointer to IP6 header and skip to ICMP6 payload */
    ip6_hdr = (struct ip6_hdr *)p->payload;
    pbuf_header(p, -ip_current_header_tot_len());

    if (p->len < sizeof(struct icmp6_hdr)) {
        ICMP6_STATS_INC(icmp6.lenerr);
        goto drop;
    }

    if (ip6_chksum_pseudo(p, IP6_NEXTH_ICMP6, p->tot_len,
                          ip6_current_src_addr(),
                          ip6_current_dest_addr()) != 0)
    {
        ICMP6_STATS_INC(icmp6.chkerr);
        goto drop;
    }

    icmp6_hdr = (struct icmp6_hdr *)p->payload;
    if (icmp6_hdr->type != ICMP6_TYPE_RS) {
        pbuf_header(p, ip_current_header_tot_len()); /* restore payload ptr */
        return RAW_RECV_CONTINUE; /* not interested */
    }

    /* only now that we know it's ICMP6_TYPE_RS we can check IP6 hop limit */
    if (IP6H_HOPLIM(ip6_hdr) != 255) {
        ICMP6_STATS_INC(icmp6.proterr);
        goto drop;
    }

    /* future, backward-incompatible changes may use different Code values. */
    if (icmp6_hdr->code != 0) {
        ICMP6_STATS_INC(icmp6.proterr);
        goto drop;
    }

    /* skip past rs_header, nothing interesting in it */
    if (p->len < sizeof(struct rs_header)) {
        ICMP6_STATS_INC(icmp6.lenerr);
        goto drop;
    }
    pbuf_header(p, -(s16_t)sizeof(struct rs_header));

    lladdr_opt = NULL;
    while (p->len > 0) {
        int optlen;

        if (p->len < 8) {
            ICMP6_STATS_INC(icmp6.lenerr);
            goto drop;
        }

        option = p->payload;
        opttype = ((u8_t *)option)[0];
        optlen8 = ((u8_t *)option)[1]; /* in units of 8 octets */

        if (optlen8 == 0) {
            ICMP6_STATS_INC(icmp6.proterr);
            goto drop;
        }

        optlen = (unsigned int)optlen8 << 3;
        if (p->len < optlen) {
            ICMP6_STATS_INC(icmp6.lenerr);
            goto drop;
        }

        if (opttype == ND6_OPTION_TYPE_SOURCE_LLADDR) {
            if (lladdr_opt != NULL) { /* duplicate */
                ICMP6_STATS_INC(icmp6.proterr);
                goto drop;
            }
            lladdr_opt = (struct lladdr_option *)option;
        }

        pbuf_header(p, -optlen);
    }

    if (ip6_addr_isany(ip6_current_src_addr())) {
        if (lladdr_opt != NULL) {
            ICMP6_STATS_INC(icmp6.proterr);
            goto drop;
        }

        /* reply with multicast RA */
    }
    else {
        /*
         * XXX: Router is supposed to update its Neighbor Cache (6.2.6),
         * but it's hidden inside nd6.c.
         */

        /* may reply with either unicast or multicast RA */
    }
    /* we just always reply with multicast RA */

    pbuf_free(p);               /* NB: this invalidates lladdr_option */

    sys_untimeout(proxy_rtadvd_timer, proxy_netif);
    proxy_rtadvd_timer(proxy_netif); /* sends and re-arms */

    return RAW_RECV_CONSUMED;

  drop:
    pbuf_free(p);
    ICMP6_STATS_INC(icmp6.drop);
    return RAW_RECV_CONSUMED;
}


static void
proxy_rtadvd_send_multicast(struct netif *proxy_netif)
{
    struct pbuf *ph, *pp;
    err_t error;

    ph = pbuf_alloc(PBUF_IP, 0, PBUF_RAM);
    if (ph == NULL) {
        DPRINTF0(("%s: failed to allocate RA header pbuf\n", __func__));
        return;
    }

    pp = pbuf_alloc(PBUF_RAW, unsolicited_ra_payload_length, PBUF_ROM);
    if (pp == NULL) {
        DPRINTF0(("%s: failed to allocate RA payload pbuf\n", __func__));
        pbuf_free(ph);
        return;
    }
    pp->payload = unsolicited_ra_payload;
    pbuf_chain(ph, pp);

    error = ip6_output_if(ph,
                          netif_ip6_addr(proxy_netif, 0), /* src: link-local */
                          &allnodes_linklocal,            /* dst */
                          255,                            /* hop limit */
                          0,                              /* traffic class */
                          IP6_NEXTH_ICMP6,
                          proxy_netif);
    if (error != ERR_OK) {
        DPRINTF0(("%s: failed to send RA (err=%d)\n", __func__, error));
    }

    pbuf_free(pp);
    pbuf_free(ph);
}


/*
 * XXX: TODO: Only ra_header::router_lifetime (and hence
 * ra_header::chksum) need to be changed, so we can precompute it once
 * and then only update these two fields.
 */
static void
proxy_rtadvd_fill_payload(struct netif *proxy_netif, int is_default)
{
    struct pbuf *p;
    struct ra_header *ra_hdr;
    struct lladdr_option *lladdr_opt;
    struct prefix_option *pfx_opt;
    unsigned int lladdr_optlen;

    LWIP_ASSERT("netif hwaddr too long",
                proxy_netif->hwaddr_len <= NETIF_MAX_HWADDR_LEN);

    /* type + length + ll addr + round up to 8 octets */
    lladdr_optlen = (2 + proxy_netif->hwaddr_len + 7) & ~0x7;

    /* actual payload length */
    unsolicited_ra_payload_length =
          sizeof(struct ra_header)
        + lladdr_optlen
        + sizeof(struct prefix_option) * 1;

    /* Set fields. */
    ra_hdr = (struct ra_header *)unsolicited_ra_payload;
    lladdr_opt = (struct lladdr_option *)((u8_t *)ra_hdr + sizeof(struct ra_header));
    pfx_opt = (struct prefix_option *)((u8_t *)lladdr_opt + lladdr_optlen);

    memset(unsolicited_ra_payload, 0, sizeof(unsolicited_ra_payload));

    ra_hdr->type = ICMP6_TYPE_RA;

#if 0
    /*
     * "M" flag.  Tell guests to use stateful DHCP6.  Disabled here
     * since we don't provide stateful server.
     */
    ra_hdr->flags |= ND6_RA_FLAG_MANAGED_ADDR_CONFIG;
#endif
    /*
     * XXX: TODO: Disable "O" flag for now to match disabled stateless
     * server.  We don't yet get IPv6 nameserver addresses from
     * HostDnsService, so we have nothing to say, don't tell guests to
     * come asking.
     */
#if 0
    /*
     * "O" flag.  Tell guests to use DHCP6 for DNS and the like.  This
     * is served by simple stateless server (RFC 3736).
     *
     * XXX: "STATEFUL" in the flag name was probably a bug in RFC2461.
     * It's present in the text, but not in the router configuration
     * variable name.  It's dropped in the text in RFC4861.
     */
    ra_hdr->flags |= ND6_RA_FLAG_OTHER_STATEFUL_CONFIG;
#endif

    if (is_default) {
        ra_hdr->router_lifetime = PP_HTONS(1200); /* seconds */
    }
    else {
        ra_hdr->router_lifetime = 0;
    }

    lladdr_opt->type = ND6_OPTION_TYPE_SOURCE_LLADDR;
    lladdr_opt->length = lladdr_optlen >> 3; /* in units of 8 octets */
    memcpy(lladdr_opt->addr, proxy_netif->hwaddr, proxy_netif->hwaddr_len);

    pfx_opt->type = ND6_OPTION_TYPE_PREFIX_INFO;
    pfx_opt->length = 4;
    pfx_opt->prefix_length = 64;
    pfx_opt->flags = ND6_PREFIX_FLAG_ON_LINK
        | ND6_PREFIX_FLAG_AUTONOMOUS;
    pfx_opt->valid_lifetime = ~0U; /* infinite */
    pfx_opt->preferred_lifetime = ~0U; /* infinite */
    pfx_opt->prefix.addr[0] = netif_ip6_addr(proxy_netif, 1)->addr[0];
    pfx_opt->prefix.addr[1] = netif_ip6_addr(proxy_netif, 1)->addr[1];


    /* we need a temp pbuf to calculate the checksum */
    p = pbuf_alloc(PBUF_IP, unsolicited_ra_payload_length, PBUF_ROM);
    if (p == NULL) {
        DPRINTF0(("rtadvd: failed to allocate RA pbuf\n"));
        return;
    }
    p->payload = unsolicited_ra_payload;

    ra_hdr->chksum = ip6_chksum_pseudo(p, IP6_NEXTH_ICMP6, p->len,
                                       /* src addr: netif's link-local */
                                       netif_ip6_addr(proxy_netif, 0),
                                       /* dst addr */
                                       &allnodes_linklocal);
    pbuf_free(p);
}
