/* $Id: pxremap.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - Loopback remapping, declarations and definitions.
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

#ifndef VBOX_INCLUDED_SRC_NAT_pxremap_h
#define VBOX_INCLUDED_SRC_NAT_pxremap_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "lwip/err.h"
#include "lwip/ip_addr.h"

struct netif;


#define PXREMAP_FAILED (-1)
#define PXREMAP_ASIS   0
#define PXREMAP_MAPPED 1

/* IPv4 */
#if ARP_PROXY
int pxremap_proxy_arp(struct netif *netif, ip_addr_t *dst);
#endif
int pxremap_ip4_divert(struct netif *netif, ip_addr_t *dst);
int pxremap_outbound_ip4(ip_addr_t *dst, ip_addr_t *src);
int pxremap_inbound_ip4(ip_addr_t *dst, ip_addr_t *src);

/* IPv6 */
int pxremap_proxy_na(struct netif *netif, ip6_addr_t *dst);
int pxremap_ip6_divert(struct netif *netif, ip6_addr_t *dst);
int pxremap_outbound_ip6(ip6_addr_t *dst, ip6_addr_t *src);
int pxremap_inbound_ip6(ip6_addr_t *dst, ip6_addr_t *src);

#define pxremap_outbound_ipX(is_ipv6, dst, src)                         \
    ((is_ipv6) ? pxremap_outbound_ip6(&(dst)->ip6, &(src)->ip6)         \
               : pxremap_outbound_ip4(&(dst)->ip4, &(src)->ip4))

#endif /* !VBOX_INCLUDED_SRC_NAT_pxremap_h */
