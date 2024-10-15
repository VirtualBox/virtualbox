/* $Id: proxy_dhcp6ds.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - Simple stateless DHCPv6 (RFC 3736) server.
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
#include "dhcp6.h"
#include "proxy.h"

#include <string.h>

#include "lwip/opt.h"
#include "lwip/mld6.h"
#include "lwip/udp.h"


static void dhcp6ds_recv(void *, struct udp_pcb *, struct pbuf *, ip6_addr_t *, u16_t);


/* ff02::1:2 - "All_DHCP_Relay_Agents_and_Servers" link-scoped multicast */
static /* const */ ip6_addr_t all_dhcp_relays_and_servers = {
    { PP_HTONL(0xff020000UL), 0, 0, PP_HTONL(0x00010002UL) }
};

/* ff05::1:3 - "All_DHCP_Servers" site-scoped multicast */
static /* const */ ip6_addr_t all_dhcp_servers = {
    { PP_HTONL(0xff050000UL), 0, 0, PP_HTONL(0x00010003UL) }
};


static struct udp_pcb *dhcp6ds_pcb;

/* prebuilt Server ID option */
#define DUID_LL_LEN (/* duid type */ 2 + /* hw type */ 2 + /* ether addr */ 6)
static u8_t dhcp6ds_serverid[/* opt */ 2 + /* optlen */ 2 + DUID_LL_LEN];

/* prebuilt DNS Servers option */
static u8_t dhcp6ds_dns[/* opt */ 2 + /* optlen */ 2 + /* IPv6 addr */ 16];


/**
 * Initialize DHCP6 server.
 *
 * Join DHCP6 multicast groups.
 * Create and bind server pcb.
 * Prebuild fixed parts of reply.
 */
err_t
dhcp6ds_init(struct netif *proxy_netif)
{
    ip6_addr_t *pxaddr, *pxaddr_nonlocal;
    int i;
    err_t error;

    LWIP_ASSERT1(proxy_netif != NULL);
    LWIP_ASSERT1(proxy_netif->hwaddr_len == 6); /* ethernet */

    pxaddr = netif_ip6_addr(proxy_netif, 0); /* link local */

    /*
     * XXX: TODO: This is a leftover from testing with IPv6 mapped
     * loopback with a special IPv6->IPv4 mapping hack in pxudp.c
     */
    /* advertise ourself as DNS resolver - will be proxied to host */
    pxaddr_nonlocal = NULL;
    for (i = 1; i < LWIP_IPV6_NUM_ADDRESSES; ++i) {
        if (ip6_addr_ispreferred(netif_ip6_addr_state(proxy_netif, i))
            && !ip6_addr_islinklocal(netif_ip6_addr(proxy_netif, i)))
        {
            pxaddr_nonlocal = netif_ip6_addr(proxy_netif, i);
            break;
        }
    }
    LWIP_ASSERT1(pxaddr_nonlocal != NULL); /* must be configured on the netif */


    error = mld6_joingroup(pxaddr, &all_dhcp_relays_and_servers);
    if (error != ERR_OK) {
        DPRINTF0(("%s: failed to join All_DHCP_Relay_Agents_and_Servers: %s\n",
                  __func__, proxy_lwip_strerr(error)));
        goto err;
    }

    error = mld6_joingroup(pxaddr, &all_dhcp_servers);
    if (error != ERR_OK) {
        DPRINTF0(("%s: failed to join All_DHCP_Servers: %s\n",
                  __func__, proxy_lwip_strerr(error)));
        goto err1;
    }


    dhcp6ds_pcb = udp_new_ip6();
    if (dhcp6ds_pcb == NULL) {
        DPRINTF0(("%s: failed to allocate PCB\n", __func__));
        error = ERR_MEM;
        goto err2;
    }

    udp_recv_ip6(dhcp6ds_pcb, dhcp6ds_recv, NULL);

    error = udp_bind_ip6(dhcp6ds_pcb, pxaddr, DHCP6_SERVER_PORT);
    if (error != ERR_OK) {
        DPRINTF0(("%s: failed to bind PCB\n", __func__));
        goto err3;
    }


#define OPT_SET(buf, off, c) do {                       \
        u16_t _s = PP_HTONS(c);                         \
        memcpy(&(buf)[off], &_s, sizeof(u16_t));        \
    } while (0)

#define SERVERID_SET(off, c)    OPT_SET(dhcp6ds_serverid, (off), (c))
#define DNSSRV_SET(off, c)      OPT_SET(dhcp6ds_dns, (off), (c))

    SERVERID_SET(0, DHCP6_OPTION_SERVERID);
    SERVERID_SET(2, DUID_LL_LEN);
    SERVERID_SET(4, DHCP6_DUID_LL);
    SERVERID_SET(6, ARES_HRD_ETHERNET);
    memcpy(&dhcp6ds_serverid[8], proxy_netif->hwaddr, 6);

    DNSSRV_SET(0, DHCP6_OPTION_DNS_SERVERS);
    DNSSRV_SET(2, 16);          /* one IPv6 address */
    /*
     * XXX: TODO: This is a leftover from testing with IPv6 mapped
     * loopback with a special IPv6->IPv4 mapping hack in pxudp.c
     */
    memcpy(&dhcp6ds_dns[4], pxaddr_nonlocal, sizeof(ip6_addr_t));

#undef SERVERID_SET
#undef DNSSRV_SET

    return ERR_OK;


  err3:
    udp_remove(dhcp6ds_pcb);
    dhcp6ds_pcb = NULL;
  err2:
    mld6_leavegroup(pxaddr, &all_dhcp_servers);
  err1:
    mld6_leavegroup(pxaddr, &all_dhcp_relays_and_servers);
  err:
    return error;
}


static u8_t dhcp6ds_reply_buf[1024];

static void
dhcp6ds_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
             ip6_addr_t *addr, u16_t port)
{
    u8_t msg_header[4];
    unsigned int msg_type;
    int copied;
    size_t roff;
    struct pbuf *q;
    err_t error;

    LWIP_UNUSED_ARG(arg);
    LWIP_ASSERT1(p != NULL);

    copied = pbuf_copy_partial(p, msg_header, sizeof(msg_header), 0);
    if (copied != sizeof(msg_header)) {
        DPRINTF(("%s: message header truncated\n", __func__));
        pbuf_free(p);
        return;
    }
    pbuf_header(p, -(s16_t)sizeof(msg_header));

    msg_type = msg_header[0];
    DPRINTF(("%s: type %u, tid 0x%6x\n", __func__, msg_type,
             (uint32_t)((msg_header[1] << 16) | (msg_header[2] << 8) | msg_header[3])));
    if (msg_type != DHCP6_INFORMATION_REQUEST) { /** @todo ? RELAY_FORW */
        pbuf_free(p);
        return;
    }

    roff = 0;

    msg_header[0] = DHCP6_REPLY;
    memcpy(dhcp6ds_reply_buf + roff, msg_header, sizeof(msg_header));
    roff += sizeof(msg_header);


    /* loop over options */
    while (p->tot_len > 0) {
        u16_t opt, optlen;

        /* fetch option code */
        copied = pbuf_copy_partial(p, &opt, sizeof(opt), 0);
        if (copied != sizeof(opt)) {
            DPRINTF(("%s: option header truncated\n", __func__));
            pbuf_free(p);
            return;
        }
        pbuf_header(p, -(s16_t)sizeof(opt));
        opt = ntohs(opt);

        /* fetch option length */
        copied = pbuf_copy_partial(p, &optlen, sizeof(optlen), 0);
        if (copied != sizeof(optlen)) {
            DPRINTF(("%s: option %u length truncated\n", __func__, opt));
            pbuf_free(p);
            return;
        }
        pbuf_header(p, -(s16_t)sizeof(optlen));
        optlen = ntohs(optlen);

        /* enough data? */
        if (optlen > p->tot_len) {
            DPRINTF(("%s: option %u truncated: expect %u, got %u\n",
                     __func__, opt, optlen, p->tot_len));
            pbuf_free(p);
            return;
        }

        DPRINTF2(("%s: option %u length %u\n", __func__, opt, optlen));

        if (opt == DHCP6_OPTION_CLIENTID) {
            u16_t s;

            /* "A DUID can be no more than 128 octets long (not
               including the type code)." */
            if (optlen > 130) {
                DPRINTF(("%s: client DUID too long: %u\n", __func__, optlen));
                pbuf_free(p);
                return;
            }

            s = PP_HTONS(DHCP6_OPTION_CLIENTID);
            memcpy(dhcp6ds_reply_buf + roff, &s, sizeof(s));
            roff += sizeof(s);

            s = ntohs(optlen);
            memcpy(dhcp6ds_reply_buf + roff, &s, sizeof(s));
            roff += sizeof(s);

            pbuf_copy_partial(p, dhcp6ds_reply_buf + roff, optlen, 0);
            roff += optlen;
        }
        else if (opt == DHCP6_OPTION_ORO) {
            u16_t *opts;
            int i, nopts;

            if (optlen % 2 != 0) {
                DPRINTF2(("%s: Option Request of odd length\n", __func__));
                goto bad_oro;
            }
            nopts = optlen / 2;

            opts = (u16_t *)malloc(optlen);
            if (opts == NULL) {
                DPRINTF2(("%s: failed to allocate space for Option Request\n",
                          __func__));
                goto bad_oro;
            }

            pbuf_copy_partial(p, opts, optlen, 0);
            for (i = 0; i < nopts; ++i) {
                opt = ntohs(opts[i]);
                DPRINTF2(("> request option %u\n", opt));
            };
            free(opts);

          bad_oro: /* empty */;
        }

        pbuf_header(p, -optlen); /* go to next option */
    }
    pbuf_free(p);               /* done */


    memcpy(dhcp6ds_reply_buf + roff, dhcp6ds_serverid, sizeof(dhcp6ds_serverid));
    roff += sizeof(dhcp6ds_serverid);

    memcpy(dhcp6ds_reply_buf + roff, dhcp6ds_dns, sizeof(dhcp6ds_dns));
    roff += sizeof(dhcp6ds_dns);

    Assert(roff == (u16_t)roff);
    q = pbuf_alloc(PBUF_RAW, (u16_t)roff, PBUF_RAM);
    if (q == NULL) {
        DPRINTF(("%s: pbuf_alloc(%d) failed\n", __func__, (int)roff));
        return;
    }

    error = pbuf_take(q, dhcp6ds_reply_buf, (u16_t)roff);
    if (error != ERR_OK) {
        DPRINTF(("%s: pbuf_take(%d) failed: %s\n",
                 __func__, (int)roff, proxy_lwip_strerr(error)));
        pbuf_free(q);
        return;
    }

    error = udp_sendto_ip6(pcb, q, addr, port);
    if (error != ERR_OK) {
        DPRINTF(("%s: udp_sendto failed: %s\n",
                 __func__, proxy_lwip_strerr(error)));
    }

    pbuf_free(q);
}
