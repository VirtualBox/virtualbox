/* $Id: pxremap.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - Loopback remapping.
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

/*
 * This file contains functions pertinent to magic address remapping.
 *
 * We want to expose host's loopback interfaces to the guest by
 * mapping them to the addresses from the same prefix/subnet, so if,
 * for example proxy interface is 10.0.2.1, we redirect traffic to
 * 10.0.2.2 to host's 127.0.0.1 loopback.  If need be, we may extend
 * this to provide additional mappings, e.g. 127.0.1.1 loopback
 * address is used on Ubuntu 12.10+ for NetworkManager's dnsmasq.
 *
 * Ditto for IPv6, except that IPv6 only has one loopback address.
 */
#define LOG_GROUP LOG_GROUP_NAT_SERVICE

#include "winutils.h"
#include "pxremap.h"
#include "proxy.h"

#include "lwip/netif.h"
#include "netif/etharp.h"       /* proxy arp hook */

#include "lwip/ip4.h"           /* IPv4 divert hook */
#include "lwip/ip6.h"           /* IPv6 divert hook */

#include <string.h>


/**
 * Check if "dst" is an IPv4 address that proxy remaps to host's
 * loopback.
 */
static int
proxy_ip4_is_mapped_loopback(struct netif *netif, const ip_addr_t *dst, ip_addr_t *lo)
{
    u32_t off;
    const struct ip4_lomap *lomap;
    size_t i;

    LWIP_ASSERT1(dst != NULL);

    if (g_proxy_options->lomap_desc == NULL) {
        return 0;
    }

    if (!ip_addr_netcmp(dst, &netif->ip_addr, &netif->netmask)) {
        return 0;
    }

    /* XXX: TODO: check netif is a proxying netif! */

    off = ntohl(ip4_addr_get_u32(dst) & ~ip4_addr_get_u32(&netif->netmask));
    lomap = g_proxy_options->lomap_desc->lomap;
    for (i = 0; i < g_proxy_options->lomap_desc->num_lomap; ++i) {
        if (off == lomap[i].off) {
            if (lo != NULL) {
                ip_addr_copy(*lo, lomap[i].loaddr);
            }
            return 1;
        }
    }
    return 0;
}


#if ARP_PROXY
/**
 * Hook function for etharp_arp_input() - returns true to cause proxy
 * ARP reply to be generated for "dst".
 */
int
pxremap_proxy_arp(struct netif *netif, ip_addr_t *dst)
{
    return proxy_ip4_is_mapped_loopback(netif, dst, NULL);
}
#endif /* ARP_PROXY */


/**
 * Hook function for ip_forward() - returns true to divert packets to
 * "dst" to proxy (instead of forwarding them via "netif" or dropping).
 */
int
pxremap_ip4_divert(struct netif *netif, ip_addr_t *dst)
{
    return proxy_ip4_is_mapped_loopback(netif, dst, NULL);
}


/**
 * Mapping from local network to loopback for outbound connections.
 *
 * Copy "src" to "dst" with ip_addr_set(dst, src), but if "src" is a
 * local network address that maps host's loopback address, copy
 * loopback address to "dst".
 */
int
pxremap_outbound_ip4(ip_addr_t *dst, ip_addr_t *src)
{
    struct netif *netif;

    LWIP_ASSERT1(dst != NULL);
    LWIP_ASSERT1(src != NULL);

    for (netif = netif_list; netif != NULL; netif = netif->next) {
        if (netif_is_up(netif) /* && this is a proxy netif */) {
            if (proxy_ip4_is_mapped_loopback(netif, src, dst)) {
                return PXREMAP_MAPPED;
            }
        }
    }

    /* not remapped, just copy src */
    ip_addr_set(dst, src);
    return PXREMAP_ASIS;
}


/**
 * Mapping from loopback to local network for inbound (port-forwarded)
 * connections.
 *
 * Copy "src" to "dst" with ip_addr_set(dst, src), but if "src" is a
 * host's loopback address, copy local network address that maps it to
 * "dst".
 */
int
pxremap_inbound_ip4(ip_addr_t *dst, ip_addr_t *src)
{
    struct netif *netif;
    const struct ip4_lomap *lomap;
    unsigned int i;

    if (ip4_addr1(src) != IP_LOOPBACKNET) {
        ip_addr_set(dst, src);
        return PXREMAP_ASIS;
    }

    if (g_proxy_options->lomap_desc == NULL) {
        return PXREMAP_FAILED;
    }

#if 0 /* ?TODO: with multiple interfaces we need to consider fwspec::dst */
    netif = ip_route(target);
    if (netif == NULL) {
        return PXREMAP_FAILED;
    }
#else
    netif = netif_list;
    LWIP_ASSERT1(netif != NULL);
    LWIP_ASSERT1(netif->next == NULL);
#endif

    lomap = g_proxy_options->lomap_desc->lomap;
    for (i = 0; i < g_proxy_options->lomap_desc->num_lomap; ++i) {
        if (ip_addr_cmp(src, &lomap[i].loaddr)) {
            ip_addr_t net;

            ip_addr_get_network(&net, &netif->ip_addr, &netif->netmask);
            ip4_addr_set_u32(dst,
                             htonl(ntohl(ip4_addr_get_u32(&net))
                                   + lomap[i].off));
            return PXREMAP_MAPPED;
        }
    }

    return PXREMAP_FAILED;
}


static int
proxy_ip6_is_mapped_loopback(struct netif *netif, ip6_addr_t *dst)
{
    int i;

    /* XXX: TODO: check netif is a proxying netif! */

    LWIP_ASSERT1(dst != NULL);

    for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i) {
        if (ip6_addr_ispreferred(netif_ip6_addr_state(netif, i))
            && ip6_addr_isuniquelocal(netif_ip6_addr(netif, i)))
        {
            ip6_addr_t *ifaddr = netif_ip6_addr(netif, i);
            if (memcmp(dst, ifaddr, sizeof(ip6_addr_t) - 1) == 0
                && ((IP6_ADDR_BLOCK8(dst) & 0xff)
                    == (IP6_ADDR_BLOCK8(ifaddr) & 0xff) + 1))
            {
                return 1;
            }
        }
    }

    return 0;
}


/**
 * Hook function for nd6_input() - returns true to cause proxy NA
 * reply to be generated for "dst".
 */
int
pxremap_proxy_na(struct netif *netif, ip6_addr_t *dst)
{
    return proxy_ip6_is_mapped_loopback(netif, dst);
}


/**
 * Hook function for ip6_forward() - returns true to divert packets to
 * "dst" to proxy (instead of forwarding them via "netif" or dropping).
 */
int
pxremap_ip6_divert(struct netif *netif, ip6_addr_t *dst)
{
    return proxy_ip6_is_mapped_loopback(netif, dst);
}


/**
 * Mapping from local network to loopback for outbound connections.
 *
 * Copy "src" to "dst" with ip6_addr_set(dst, src), but if "src" is a
 * local network address that maps host's loopback address, copy IPv6
 * loopback address to "dst".
 */
int
pxremap_outbound_ip6(ip6_addr_t *dst, ip6_addr_t *src)
{
    struct netif *netif;
    int i;

    LWIP_ASSERT1(dst != NULL);
    LWIP_ASSERT1(src != NULL);

    for (netif = netif_list; netif != NULL; netif = netif->next) {
        if (!netif_is_up(netif) /* || this is not a proxy netif */) {
            continue;
        }

        for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i) {
            if (ip6_addr_ispreferred(netif_ip6_addr_state(netif, i))
                && ip6_addr_isuniquelocal(netif_ip6_addr(netif, i)))
            {
                ip6_addr_t *ifaddr = netif_ip6_addr(netif, i);
                if (memcmp(src, ifaddr, sizeof(ip6_addr_t) - 1) == 0
                    && ((IP6_ADDR_BLOCK8(src) & 0xff)
                        == (IP6_ADDR_BLOCK8(ifaddr) & 0xff) + 1))
                {
                    ip6_addr_set_loopback(dst);
                    return PXREMAP_MAPPED;
                }
            }
        }
    }

    /* not remapped, just copy src */
    ip6_addr_set(dst, src);
    return PXREMAP_ASIS;
}


/**
 * Mapping from loopback to local network for inbound (port-forwarded)
 * connections.
 *
 * Copy "src" to "dst" with ip6_addr_set(dst, src), but if "src" is a
 * host's loopback address, copy local network address that maps it to
 * "dst".
 */
int
pxremap_inbound_ip6(ip6_addr_t *dst, ip6_addr_t *src)
{
    ip6_addr_t loopback;
    struct netif *netif;
    int i;

    ip6_addr_set_loopback(&loopback);
    if (!ip6_addr_cmp(src, &loopback)) {
        ip6_addr_set(dst, src);
        return PXREMAP_ASIS;
    }

#if 0  /* ?TODO: with multiple interfaces we need to consider fwspec::dst */
    netif = ip6_route_fwd(target);
    if (netif == NULL) {
        return PXREMAP_FAILED;
    }
#else
    netif = netif_list;
    LWIP_ASSERT1(netif != NULL);
    LWIP_ASSERT1(netif->next == NULL);
#endif

    for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i) {
        ip6_addr_t *ifaddr = netif_ip6_addr(netif, i);
        if (ip6_addr_ispreferred(netif_ip6_addr_state(netif, i))
            && ip6_addr_isuniquelocal(ifaddr))
        {
            ip6_addr_set(dst, ifaddr);
            ++((u8_t *)&dst->addr[3])[3];
            return PXREMAP_MAPPED;
        }
    }

    return PXREMAP_FAILED;
}
