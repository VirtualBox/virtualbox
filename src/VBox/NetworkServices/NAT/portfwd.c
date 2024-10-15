/* $Id: portfwd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - port-forwarding rules.
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
#include "portfwd.h"

#ifndef RT_OS_WINDOWS
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#else
# include "winpoll.h"
#endif
#include <stdio.h>
#include <string.h>

#include "proxy.h"
#include "proxy_pollmgr.h"
#include "pxremap.h"

#include "lwip/netif.h"


struct portfwd_msg {
    struct fwspec *fwspec;
    int add;
};


static int portfwd_chan_send(struct portfwd_msg *);
static int portfwd_rule_add_del(struct fwspec *, int);
static int portfwd_pmgr_chan(struct pollmgr_handler *, SOCKET, int);


static struct pollmgr_handler portfwd_pmgr_chan_hdl;


void
portfwd_init(void)
{
    portfwd_pmgr_chan_hdl.callback = portfwd_pmgr_chan;
    portfwd_pmgr_chan_hdl.data = NULL;
    portfwd_pmgr_chan_hdl.slot = -1;
    pollmgr_add_chan(POLLMGR_CHAN_PORTFWD, &portfwd_pmgr_chan_hdl);

    /* add preconfigured forwarders */
    fwtcp_init();
    fwudp_init();
}


static int
portfwd_chan_send(struct portfwd_msg *msg)
{
    ssize_t nsent;

    nsent = pollmgr_chan_send(POLLMGR_CHAN_PORTFWD, &msg, sizeof(msg));
    if (nsent < 0) {
        free(msg);
        return -1;
    }

    return 0;
}


static int
portfwd_rule_add_del(struct fwspec *fwspec, int add)
{
    struct portfwd_msg *msg;

    msg = (struct portfwd_msg *)malloc(sizeof(*msg));
    if (msg == NULL) {
        DPRINTF0(("%s: failed to allocate message\n", __func__));
        return -1;
    }

    msg->fwspec = fwspec;
    msg->add = add;

    return portfwd_chan_send(msg);
}


int
portfwd_rule_add(struct fwspec *fwspec)
{
    return portfwd_rule_add_del(fwspec, 1);
}


int
portfwd_rule_del(struct fwspec *fwspec)
{
    return portfwd_rule_add_del(fwspec, 0);
}


/**
 * POLLMGR_CHAN_PORTFWD handler.
 */
static int
portfwd_pmgr_chan(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    void *ptr = pollmgr_chan_recv_ptr(handler, fd, revents);
    struct portfwd_msg *msg = (struct portfwd_msg *)ptr;

    if (msg->fwspec->stype == SOCK_STREAM) {
        if (msg->add) {
            fwtcp_add(msg->fwspec);
        }
        else {
            fwtcp_del(msg->fwspec);
        }
    }
    else { /* SOCK_DGRAM */
        if (msg->add) {
            fwudp_add(msg->fwspec);
        }
        else {
            fwudp_del(msg->fwspec);
        }
    }

    free(msg->fwspec);
    free(msg);

    return POLLIN;
}


int
fwspec_set(struct fwspec *fwspec, int sdom, int stype,
           const char *src_addr_str, uint16_t src_port,
           const char *dst_addr_str, uint16_t dst_port)
{
    struct addrinfo hints;
    struct addrinfo *ai;
    int status;

    LWIP_ASSERT1(sdom == PF_INET || sdom == PF_INET6);
    LWIP_ASSERT1(stype == SOCK_STREAM || stype == SOCK_DGRAM);

    fwspec->sdom = sdom;
    fwspec->stype = stype;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = (sdom == PF_INET) ? AF_INET : AF_INET6;
    hints.ai_socktype = stype;
    hints.ai_flags = AI_NUMERICHOST;

    status = getaddrinfo(src_addr_str, NULL, &hints, &ai);
    if (status != 0) {
        LogRel(("\"%s\": %s\n", src_addr_str, gai_strerror(status)));
        return -1;
    }
    LWIP_ASSERT1(ai != NULL);
    LWIP_ASSERT1(ai->ai_addrlen <= sizeof(fwspec->src));
    memcpy(&fwspec->src, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(ai);
    ai = NULL;

    status = getaddrinfo(dst_addr_str, NULL, &hints, &ai);
    if (status != 0) {
        LogRel(("\"%s\": %s\n", dst_addr_str, gai_strerror(status)));
        return -1;
    }
    LWIP_ASSERT1(ai != NULL);
    LWIP_ASSERT1(ai->ai_addrlen <= sizeof(fwspec->dst));
    memcpy(&fwspec->dst, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(ai);
    ai = NULL;

    if (sdom == PF_INET) {
        fwspec->src.sin.sin_port = htons(src_port);
        fwspec->dst.sin.sin_port = htons(dst_port);
    }
    else { /* PF_INET6 */
        fwspec->src.sin6.sin6_port = htons(src_port);
        fwspec->dst.sin6.sin6_port = htons(dst_port);
    }

    return 0;
}


int
fwspec_equal(struct fwspec *a, struct fwspec *b)
{
    LWIP_ASSERT1(a != NULL);
    LWIP_ASSERT1(b != NULL);

    if (a->sdom != b->sdom || a->stype != b->stype) {
        return 0;
    }

    if (a->sdom == PF_INET) {
        return a->src.sin.sin_port == b->src.sin.sin_port
            && a->dst.sin.sin_port == b->dst.sin.sin_port
            && a->src.sin.sin_addr.s_addr == b->src.sin.sin_addr.s_addr
            && a->dst.sin.sin_addr.s_addr == b->dst.sin.sin_addr.s_addr;
    }
    else { /* PF_INET6 */
        return a->src.sin6.sin6_port == b->src.sin6.sin6_port
            && a->dst.sin6.sin6_port == b->dst.sin6.sin6_port
            && IN6_ARE_ADDR_EQUAL(&a->src.sin6.sin6_addr, &b->src.sin6.sin6_addr)
            && IN6_ARE_ADDR_EQUAL(&a->dst.sin6.sin6_addr, &b->dst.sin6.sin6_addr);
    }
}


/**
 * Set fwdsrc to the IP address of the peer.
 *
 * For port-forwarded connections originating from hosts loopback the
 * source address is set to the address of one of lwIP interfaces.
 *
 * Currently we only have one interface so there's not much logic
 * here.  In the future we might need to additionally consult fwspec
 * and routing table to determine which netif is used for connections
 * to the specified guest.
 */
int
fwany_ipX_addr_set_src(ipX_addr_t *fwdsrc, const struct sockaddr *peer)
{
    int mapping;

    if (peer->sa_family == AF_INET) {
        const struct sockaddr_in *peer4 = (const struct sockaddr_in *)peer;
        ip_addr_t peerip4;

        peerip4.addr = peer4->sin_addr.s_addr;
        mapping = pxremap_inbound_ip4(&fwdsrc->ip4, &peerip4);
    }
    else if (peer->sa_family == AF_INET6) {
        const struct sockaddr_in6 *peer6 = (const struct sockaddr_in6 *)peer;
        ip6_addr_t peerip6;

        memcpy(&peerip6, &peer6->sin6_addr, sizeof(ip6_addr_t));
        mapping = pxremap_inbound_ip6(&fwdsrc->ip6, &peerip6);
    }
    else {
        mapping = PXREMAP_FAILED;
    }

    return mapping;
}
