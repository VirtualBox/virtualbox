/* $Id: proxy.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - common definitions and declarations.
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

#ifndef VBOX_INCLUDED_SRC_NAT_proxy_h
#define VBOX_INCLUDED_SRC_NAT_proxy_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if !defined(VBOX)
#include "vbox-compat.h"
#endif

#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "winutils.h"

/* forward */
struct netif;
struct tcpip_msg;
struct pbuf;
struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;

struct ip4_lomap
{
    ip_addr_t loaddr;
    uint32_t off;
};

struct ip4_lomap_desc
{
    const struct ip4_lomap *lomap;
    unsigned int num_lomap;
};

struct proxy_options {
    ip_addr_t ipv4_addr;
    ip_addr_t ipv4_mask;
    ip6_addr_t ipv6_addr;
    int ipv6_enabled;
    int ipv6_defroute;
    SOCKET icmpsock4;
    SOCKET icmpsock6;
    const char *tftp_root;
    const struct sockaddr_in *src4;
    const struct sockaddr_in6 *src6;
    const struct ip4_lomap_desc *lomap_desc;
    const char **nameservers;
};

extern volatile struct proxy_options *g_proxy_options;
extern struct netif *g_proxy_netif;

void proxy_init(struct netif *, struct proxy_options *);
SOCKET proxy_connected_socket(int, int, ipX_addr_t *, u16_t);
SOCKET proxy_bound_socket(int, int, struct sockaddr *);
#ifdef RT_OS_LINUX
int proxy_fixup_accepted_socket(SOCKET);
#endif
void proxy_reset_socket(SOCKET);
int proxy_sendto(SOCKET, struct pbuf *, void *, size_t);
void proxy_lwip_post(struct tcpip_msg *);
const char *proxy_lwip_strerr(err_t);

/* proxy_rtadvd.c */
void proxy_rtadvd_start(struct netif *);
void proxy_rtadvd_do_quick(void *);

/* rtmon_*.c */
int rtmon_get_defaults(void);

/* proxy_dhcp6ds.c */
err_t dhcp6ds_init(struct netif *);

/* proxy_tftpd.c */
err_t tftpd_init(struct netif *, const char *);

/* pxtcp.c */
void pxtcp_init(void);

/* pxudp.c */
void pxudp_init(void);

/* pxdns.c */
err_t pxdns_init(struct netif *);
void pxdns_set_nameservers(void *);

/* pxping.c */
err_t pxping_init(struct netif *, SOCKET, SOCKET);


#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS)
# define HAVE_SA_LEN 0
#else
# define HAVE_SA_LEN 1
#endif

#define LWIP_ASSERT1(condition) LWIP_ASSERT(#condition, condition)

/*
 * TODO: DPRINTF0 should probably become LogRel but its usage needs to
 * be cleaned up a bit before.
 */
#define DPRINTF0(a) Log(a)

#define DPRINTF(a)  DPRINTF1(a)
#define DPRINTF1(a) Log2(a)
#define DPRINTF2(a) Log3(a)

#endif /* !VBOX_INCLUDED_SRC_NAT_proxy_h */

