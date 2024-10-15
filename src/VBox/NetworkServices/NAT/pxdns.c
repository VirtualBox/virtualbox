/* $Id: pxdns.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - DNS proxy.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
 * Copyright (c) 2003,2004,2005 Armin Wolfermann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define LOG_GROUP LOG_GROUP_NAT_SERVICE

#include "winutils.h"

#include "proxy.h"
#include "proxy_pollmgr.h"
#include "pxtcp.h"

#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"

#ifndef RT_OS_WINDOWS
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#else
#include "winpoll.h"
#endif

#include <stdio.h>
#include <string.h>


union sockaddr_inet {
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};


struct request;


/**
 * DNS Proxy
 */
struct pxdns {
    SOCKET sock4;
    SOCKET sock6;

    struct pollmgr_handler pmhdl4;
    struct pollmgr_handler pmhdl6;

    struct udp_pcb *pcb4;
    struct udp_pcb *pcb6;

    struct tcp_pcb *ltcp;

    size_t generation;
    size_t nresolvers;
    union sockaddr_inet *resolvers;

    u16_t id;

    sys_mutex_t lock;

    size_t active_queries;
    size_t expired_queries;
    size_t late_answers;
    size_t hash_collisions;

#define TIMEOUT 5
    size_t timeout_slot;
    u32_t timeout_mask;
    struct request *timeout_list[TIMEOUT];

#define HASHSIZE 10
#define HASH(id) ((id) & ((1 << HASHSIZE) - 1))
    struct request *request_hash[1 << HASHSIZE];
} g_pxdns;


struct request {
    /**
     * Request ID that we use in relayed request.
     */
    u16_t id;

    /**
     * pxdns::generation used for this request
     */
    size_t generation;

    /**
     * Current index into pxdns::resolvers
     */
    size_t residx;

    /**
     * PCB from which we have received this request.  lwIP doesn't
     * support listening for both IPv4 and IPv6 on the same pcb, so we
     * use two and need to keep track.
     */
    struct udp_pcb *pcb;

    /**
     * Client this request is from and its original request ID.
     */
    ipX_addr_t client_addr;
    u16_t client_port;
    u16_t client_id;

    /**
     * Chaining for pxdns::request_hash
     */
    struct request **pprev_hash;
    struct request *next_hash;

    /**
     * Chaining for pxdns::timeout_list
     */
    struct request **pprev_timeout;
    struct request *next_timeout;

    /**
     * Slot in pxdns::timeout_list
     */
    size_t timeout_slot;

    /**
     * Pbuf with reply received on pollmgr thread.
     */
    struct pbuf *reply;

    /**
     * Preallocated lwIP message to send reply from the lwIP thread.
     */
    struct tcpip_msg msg_reply;

    /**
     * Client request.  ID is replaced with ours, original saved in
     * client_id.  Use a copy since we might need to resend and we
     * don't want to hold onto pbuf of the request.
     */
    size_t size;
    u8_t data[1];
};


static void pxdns_create_resolver_sockaddrs(struct pxdns *pxdns,
                                            const char **nameservers);

static err_t pxdns_accept_syn(void *arg, struct tcp_pcb *newpcb, struct pbuf *syn);

static void pxdns_recv4(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        ip_addr_t *addr, u16_t port);
static void pxdns_recv6(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        ip6_addr_t *addr, u16_t port);
static void pxdns_query(struct pxdns *pxdns, struct udp_pcb *pcb, struct pbuf *p,
                        ipX_addr_t *addr, u16_t port);
static void pxdns_timer(void *arg);
static int pxdns_rexmit(struct pxdns *pxdns, struct request *req);
static int pxdns_forward_outbound(struct pxdns *pxdns, struct request *req);

static int pxdns_pmgr_pump(struct pollmgr_handler *handler, SOCKET fd, int revents);
static void pxdns_pcb_reply(void *ctx);

static void pxdns_request_register(struct pxdns *pxdns, struct request *req);
static void pxdns_request_deregister(struct pxdns *pxdns, struct request *req);
static struct request *pxdns_request_find(struct pxdns *pxdns, u16_t id);

static void pxdns_hash_add(struct pxdns *pxdns, struct request *req);
static void pxdns_hash_del(struct pxdns *pxdns, struct request *req);
static void pxdns_timeout_add(struct pxdns *pxdns, struct request *req);
static void pxdns_timeout_del(struct pxdns *pxdns, struct request *req);

static void pxdns_request_free(struct request *req);


err_t
pxdns_init(struct netif *proxy_netif)
{
    struct pxdns *pxdns = &g_pxdns;
    err_t error;

    LWIP_UNUSED_ARG(proxy_netif);

    pxdns->ltcp = tcp_new();
    if (pxdns->ltcp != NULL) {
        tcp_bind_ip6(pxdns->ltcp, IP6_ADDR_ANY, 53);
        pxdns->ltcp = tcp_listen_dual(pxdns->ltcp);
        if (pxdns->ltcp != NULL) {
            tcp_arg(pxdns->ltcp, pxdns);
            tcp_accept_syn(pxdns->ltcp, pxdns_accept_syn);
        }
    }

    pxdns->pmhdl4.callback = pxdns_pmgr_pump;
    pxdns->pmhdl4.data = (void *)pxdns;
    pxdns->pmhdl4.slot = -1;

    pxdns->pmhdl6.callback = pxdns_pmgr_pump;
    pxdns->pmhdl6.data = (void *)pxdns;
    pxdns->pmhdl6.slot = -1;

    pxdns->pcb4 = udp_new();
    if (pxdns->pcb4 == NULL) {
        error = ERR_MEM;
        goto err_cleanup_pcb;
    }

    pxdns->pcb6 = udp_new_ip6();
    if (pxdns->pcb6 == NULL) {
        error = ERR_MEM;
        goto err_cleanup_pcb;
    }

    error = udp_bind(pxdns->pcb4, IP_ADDR_ANY, 53);
    if (error != ERR_OK) {
        goto err_cleanup_pcb;
    }

    error = udp_bind_ip6(pxdns->pcb6, IP6_ADDR_ANY, 53);
    if (error != ERR_OK) {
        goto err_cleanup_pcb;
    }

    udp_recv(pxdns->pcb4, pxdns_recv4, pxdns);
    udp_recv_ip6(pxdns->pcb6, pxdns_recv6, pxdns);

    pxdns->sock4 = socket(AF_INET, SOCK_DGRAM, 0);
    if (pxdns->sock4 == INVALID_SOCKET) {
        goto err_cleanup_pcb;
    }

    pxdns->sock6 = socket(AF_INET6, SOCK_DGRAM, 0);
    if (pxdns->sock6 == INVALID_SOCKET) {
        /* it's ok if the host doesn't support IPv6 */
        /* XXX: TODO: log */
    }

    pxdns->generation = 0;
    pxdns->nresolvers = 0;
    pxdns->resolvers = NULL;
    pxdns_create_resolver_sockaddrs(pxdns, g_proxy_options->nameservers);

    sys_mutex_new(&pxdns->lock);

    pxdns->timeout_slot = 0;
    pxdns->timeout_mask = 0;

    /* NB: assumes pollmgr thread is not running yet */
    pollmgr_add(&pxdns->pmhdl4, pxdns->sock4, POLLIN);
    if (pxdns->sock6 != INVALID_SOCKET) {
        pollmgr_add(&pxdns->pmhdl6, pxdns->sock6, POLLIN);
    }

    return ERR_OK;

  err_cleanup_pcb:
    if (pxdns->pcb4 != NULL) {
        udp_remove(pxdns->pcb4);
        pxdns->pcb4 = NULL;
    }
    if (pxdns->pcb6 != NULL) {
        udp_remove(pxdns->pcb6);
        pxdns->pcb4 = NULL;
    }

    return error;
}


/**
 * lwIP thread callback to set the new list of nameservers.
 */
void
pxdns_set_nameservers(void *arg)
{
    const char **nameservers = (const char **)arg;

    if (g_proxy_options->nameservers != NULL) {
        RTMemFree((void *)g_proxy_options->nameservers);
    }
    g_proxy_options->nameservers = nameservers;

    pxdns_create_resolver_sockaddrs(&g_pxdns, nameservers);
}


/**
 * Use this list of nameservers to resolve guest requests.
 *
 * Runs on lwIP thread, so no new queries or retramsmits compete with
 * it for the use of the existing list of resolvers (to be replaced).
 */
static void
pxdns_create_resolver_sockaddrs(struct pxdns *pxdns, const char **nameservers)
{
    struct addrinfo hints;
    union sockaddr_inet *resolvers;
    size_t nnames, nresolvers;
    const char **p;
    int status;

    resolvers = NULL;
    nresolvers = 0;

    if (nameservers == NULL) {
        goto update_resolvers;
    }

    nnames = 0;
    for (p = nameservers; *p != NULL; ++p) {
        ++nnames;
    }

    if (nnames == 0) {
        goto update_resolvers;
    }

    resolvers = (union sockaddr_inet *)calloc(nnames, sizeof(resolvers[0]));
    if (resolvers == NULL) {
        nresolvers = 0;
        goto update_resolvers;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    for (p = nameservers; *p != NULL; ++p) {
        const char *name = *p;
        struct addrinfo *ai;
        status = getaddrinfo(name, /* "domain" */ "53", &hints, &ai);
        if (status != 0) {
            /* XXX: log failed resolution */
            continue;
        }

        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
            /* XXX: log unsupported address family */
            freeaddrinfo(ai);
            continue;
        }

        if (ai->ai_addrlen > sizeof(resolvers[nresolvers])) {
            /* XXX: log */
            freeaddrinfo(ai);
            continue;
        }

        if (ai->ai_family == AF_INET6 && pxdns->sock6 == INVALID_SOCKET) {
            /* no IPv6 support on the host, can't use this resolver */
            freeaddrinfo(ai);
            continue;
        }

        memcpy(&resolvers[nresolvers], ai->ai_addr, ai->ai_addrlen);
        freeaddrinfo(ai);
        ++nresolvers;
    }

    if (nresolvers == 0) {
        if (resolvers != NULL) {
            free(resolvers);
        }
        resolvers = NULL;
    }

  update_resolvers:
    ++pxdns->generation;
    if (pxdns->resolvers != NULL) {
        free(pxdns->resolvers);
    }
    pxdns->resolvers = resolvers;
    pxdns->nresolvers = nresolvers;
}


static void
pxdns_request_free(struct request *req)
{
    LWIP_ASSERT1(req->pprev_hash == NULL);
    LWIP_ASSERT1(req->pprev_timeout == NULL);

    if (req->reply != NULL) {
        pbuf_free(req->reply);
    }
    free(req);
}


static void
pxdns_hash_add(struct pxdns *pxdns, struct request *req)
{
    struct request **chain;

    LWIP_ASSERT1(req->pprev_hash == NULL);
    ++pxdns->active_queries;

    chain = &pxdns->request_hash[HASH(req->id)];
    if ((req->next_hash = *chain) != NULL) {
        (*chain)->pprev_hash = &req->next_hash;
        ++pxdns->hash_collisions;
    }
    *chain = req;
    req->pprev_hash = chain;
}


static void
pxdns_timeout_add(struct pxdns *pxdns, struct request *req)
{
    struct request **chain;
    u32_t omask;

    LWIP_ASSERT1(req->pprev_timeout == NULL);

    req->timeout_slot = pxdns->timeout_slot;
    chain = &pxdns->timeout_list[req->timeout_slot];
    if ((req->next_timeout = *chain) != NULL) {
        (*chain)->pprev_timeout = &req->next_timeout;
    }
    *chain = req;
    req->pprev_timeout = chain;

    omask = pxdns->timeout_mask;
    pxdns->timeout_mask |= 1U << req->timeout_slot;
    if (omask == 0) {
        sys_untimeout(pxdns_timer, pxdns);
        sys_timeout(1 * 1000, pxdns_timer, pxdns);
    }
}


static void
pxdns_hash_del(struct pxdns *pxdns, struct request *req)
{
    LWIP_ASSERT1(req->pprev_hash != NULL);
    --pxdns->active_queries;

    if (req->next_hash != NULL) {
        req->next_hash->pprev_hash = req->pprev_hash;
    }
    *req->pprev_hash = req->next_hash;
    req->pprev_hash = NULL;
    req->next_hash = NULL;
}


static void
pxdns_timeout_del(struct pxdns *pxdns, struct request *req)
{
    LWIP_ASSERT1(req->pprev_timeout != NULL);
    LWIP_ASSERT1(req->timeout_slot < TIMEOUT);

    if (req->next_timeout != NULL) {
        req->next_timeout->pprev_timeout = req->pprev_timeout;
    }
    *req->pprev_timeout = req->next_timeout;
    req->pprev_timeout = NULL;
    req->next_timeout = NULL;

    if (pxdns->timeout_list[req->timeout_slot] == NULL) {
        pxdns->timeout_mask &= ~(1U << req->timeout_slot);
        /* may be on pollmgr thread so no sys_untimeout */
    }
}



/**
 * Do bookkeeping on new request.  Called from pxdns_query().
 */
static void
pxdns_request_register(struct pxdns *pxdns, struct request *req)
{
    sys_mutex_lock(&pxdns->lock);

    pxdns_hash_add(pxdns, req);
    pxdns_timeout_add(pxdns, req);

    sys_mutex_unlock(&pxdns->lock);
}


static void
pxdns_request_deregister(struct pxdns *pxdns, struct request *req)
{
    sys_mutex_lock(&pxdns->lock);

    pxdns_hash_del(pxdns, req);
    pxdns_timeout_del(pxdns, req);

    sys_mutex_unlock(&pxdns->lock);
}


/**
 * Find request by the id we used when relaying it and remove it from
 * id hash and timeout list.  Called from pxdns_pmgr_pump() when reply
 * comes.
 */
static struct request *
pxdns_request_find(struct pxdns *pxdns, u16_t id)
{
    struct request *req = NULL;

    sys_mutex_lock(&pxdns->lock);

    /* find request in the id->req hash */
    for (req = pxdns->request_hash[HASH(id)]; req != NULL; req = req->next_hash) {
        if (req->id == id) {
            break;
        }
    }

    if (req != NULL) {
        pxdns_hash_del(pxdns, req);
        pxdns_timeout_del(pxdns, req);
    }

    sys_mutex_unlock(&pxdns->lock);
    return req;
}


/**
 * Retransmit of g/c expired requests and move timeout slot forward.
 */
static void
pxdns_timer(void *arg)
{
    struct pxdns *pxdns = (struct pxdns *)arg;
    struct request **chain, *req;
    u32_t mask;

    sys_mutex_lock(&pxdns->lock);

    /*
     * Move timeout slot first.  New slot points to the list of
     * expired requests.  If any expired request is retransmitted, we
     * keep it on the list (that is now current), effectively
     * resetting the timeout.
     */
    LWIP_ASSERT1(pxdns->timeout_slot < TIMEOUT);
    if (++pxdns->timeout_slot == TIMEOUT) {
        pxdns->timeout_slot = 0;
    }

    chain = &pxdns->timeout_list[pxdns->timeout_slot];
    req = *chain;
    while (req != NULL) {
        struct request *expired = req;
        req = req->next_timeout;

        if (pxdns_rexmit(pxdns, expired)) {
            continue;
        }

        pxdns_hash_del(pxdns, expired);
        pxdns_timeout_del(pxdns, expired);
        ++pxdns->expired_queries;

        pxdns_request_free(expired);
    }

    if (pxdns->timeout_list[pxdns->timeout_slot] == NULL) {
        pxdns->timeout_mask &= ~(1U << pxdns->timeout_slot);
    }
    else {
        pxdns->timeout_mask |= 1U << pxdns->timeout_slot;
    }
    mask = pxdns->timeout_mask;

    sys_mutex_unlock(&pxdns->lock);

    if (mask != 0) {
        sys_timeout(1 * 1000, pxdns_timer, pxdns);
    }
}


static void
pxdns_recv4(void *arg, struct udp_pcb *pcb, struct pbuf *p,
            ip_addr_t *addr, u16_t port)
{
    struct pxdns *pxdns = (struct pxdns *)arg;
    pxdns_query(pxdns, pcb, p, ip_2_ipX(addr), port);
}

static void
pxdns_recv6(void *arg, struct udp_pcb *pcb, struct pbuf *p,
            ip6_addr_t *addr, u16_t port)
{
    struct pxdns *pxdns = (struct pxdns *)arg;
    pxdns_query(pxdns, pcb, p, ip6_2_ipX(addr), port);
}


static void
pxdns_query(struct pxdns *pxdns, struct udp_pcb *pcb, struct pbuf *p,
            ipX_addr_t *addr, u16_t port)
{
    struct request *req;
    int sent;

    if (pxdns->nresolvers == 0) {
        /* nothing we can do */
        pbuf_free(p);
        return;
    }

    req = calloc(1, sizeof(struct request) - 1 + p->tot_len);
    if (req == NULL) {
        pbuf_free(p);
        return;
    }

    /* copy request data */
    req->size = p->tot_len;
    pbuf_copy_partial(p, req->data, p->tot_len, 0);

    /* save client identity and client's request id */
    req->pcb = pcb;
    ipX_addr_copy(PCB_ISIPV6(pcb), req->client_addr, *addr);
    req->client_port = port;
    memcpy(&req->client_id, req->data, sizeof(req->client_id));

    /* slap our request id onto it */
    req->id = pxdns->id++;
    memcpy(req->data, &req->id, sizeof(u16_t));

    /* resolver to forward to */
    req->generation = pxdns->generation;
    req->residx = 0;

    /* prepare for relaying the reply back to guest */
    req->msg_reply.type = TCPIP_MSG_CALLBACK_STATIC;
    req->msg_reply.sem = NULL;
    req->msg_reply.msg.cb.function = pxdns_pcb_reply;
    req->msg_reply.msg.cb.ctx = (void *)req;

    DPRINTF2(("%s: req=%p: client id %d -> id %d\n",
              __func__, (void *)req, req->client_id, req->id));

    pxdns_request_register(pxdns, req);

    sent = pxdns_forward_outbound(pxdns, req);
    if (!sent) {
        sent = pxdns_rexmit(pxdns, req);
    }
    if (!sent) {
        pxdns_request_deregister(pxdns, req);
        pxdns_request_free(req);
    }
}


/**
 * Forward request to the req::residx resolver in the pxdns::resolvers
 * array of upstream resolvers.
 *
 * Returns 1 on success, 0 on failure.
 */
static int
pxdns_forward_outbound(struct pxdns *pxdns, struct request *req)
{
    union sockaddr_inet *resolver;
    ssize_t nsent;
#ifdef RT_OS_WINDOWS
    const char *pSendData = (const char *)&req->data[0];
    int         cbSendData = (int)req->size;
    Assert((size_t)cbSendData == req->size);
#else
    const void *pSendData = &req->data[0];
    size_t      cbSendData = req->size;
#endif

    DPRINTF2(("%s: req %p: sending to resolver #%lu\n",
              __func__, (void *)req, (unsigned long)req->residx));

    LWIP_ASSERT1(req->generation == pxdns->generation);
    LWIP_ASSERT1(req->residx < pxdns->nresolvers);
    resolver = &pxdns->resolvers[req->residx];

    if (resolver->sa.sa_family == AF_INET) {
        nsent = sendto(pxdns->sock4, pSendData, cbSendData, 0,
                       &resolver->sa, sizeof(resolver->sin));

    }
    else if (resolver->sa.sa_family == AF_INET6) {
        if (pxdns->sock6 != INVALID_SOCKET) {
            nsent = sendto(pxdns->sock6, pSendData, cbSendData, 0,
                           &resolver->sa, sizeof(resolver->sin6));
        }
        else {
            /* shouldn't happen, we should have weeded out IPv6 resolvers */
            return 0;
        }
    }
    else {
        /* shouldn't happen, we should have weeded out unsupported families */
        return 0;
    }

    if ((size_t)nsent == req->size) {
        return 1; /* sent */
    }

    if (nsent < 0) {
        DPRINTF2(("%s: send: %R[sockerr]\n", __func__, SOCKERRNO()));
    }
    else {
        DPRINTF2(("%s: sent only %lu of %lu\n",
                  __func__, (unsigned long)nsent, (unsigned long)req->size));
    }
    return 0; /* not sent, caller will retry as necessary */
}


/**
 * Forward request to the next resolver in the pxdns::resolvers array
 * of upstream resolvers if there are any left.
 */
static int
pxdns_rexmit(struct pxdns *pxdns, struct request *req)
{
    int sent;

    if (/* __predict_false */ req->generation != pxdns->generation) {
        DPRINTF2(("%s: req %p: generation %lu != pxdns generation %lu\n",
                  __func__, (void *)req,
                  (unsigned long)req->generation,
                  (unsigned long)pxdns->generation));
        return 0;
    }

    LWIP_ASSERT1(req->residx < pxdns->nresolvers);
    do {
        if (++req->residx == pxdns->nresolvers) {
            return 0;
        }

        sent = pxdns_forward_outbound(pxdns, req);
    } while (!sent);

    return 1;
}


static int
pxdns_pmgr_pump(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxdns *pxdns;
    struct request *req;
    ssize_t nread;
    err_t error;
    u16_t id;

    pxdns = (struct pxdns *)handler->data;
    LWIP_ASSERT1(handler == &pxdns->pmhdl4 || handler == &pxdns->pmhdl6);
    LWIP_ASSERT1(fd == (handler == &pxdns->pmhdl4 ? pxdns->sock4 : pxdns->sock6));

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


#ifdef RT_OS_WINDOWS
    nread = recv(fd, (char *)pollmgr_udpbuf, sizeof(pollmgr_udpbuf), 0);
#else
    nread = recv(fd, pollmgr_udpbuf, sizeof(pollmgr_udpbuf), 0);
#endif
    if (nread < 0) {
        DPRINTF(("%s: %R[sockerr]\n", __func__, SOCKERRNO()));
        return POLLIN;
    }

    /* check for minimum dns packet length */
    if (nread < 12) {
        DPRINTF2(("%s: short reply %lu bytes\n",
                  __func__, (unsigned long)nread));
        return POLLIN;
    }

    /* XXX: shall we proxy back RCODE=Refused responses? */

    memcpy(&id, pollmgr_udpbuf, sizeof(id));
    req = pxdns_request_find(pxdns, id);
    if (req == NULL) {
        DPRINTF2(("%s: orphaned reply for %d\n", __func__, id));
        ++pxdns->late_answers;
        return POLLIN;
    }

    DPRINTF2(("%s: reply for req=%p: id %d -> client id %d\n",
              __func__, (void *)req, req->id, req->client_id));

    req->reply = pbuf_alloc(PBUF_RAW, nread, PBUF_RAM);
    if (req->reply == NULL) {
        DPRINTF(("%s: pbuf_alloc(%d) failed\n", __func__, (int)nread));
        pxdns_request_free(req);
        return POLLIN;
    }

    memcpy(pollmgr_udpbuf, &req->client_id, sizeof(req->client_id));
    error = pbuf_take(req->reply, pollmgr_udpbuf, nread);
    if (error != ERR_OK) {
        DPRINTF(("%s: pbuf_take(%d) failed\n", __func__, (int)nread));
        pxdns_request_free(req);
        return POLLIN;
    }

    proxy_lwip_post(&req->msg_reply);
    return POLLIN;
}


/**
 * Called on lwIP thread via request::msg_reply callback.
 */
static void
pxdns_pcb_reply(void *ctx)
{
    struct request *req = (struct request *)ctx;
    err_t error;

    error = udp_sendto(req->pcb, req->reply,
                       ipX_2_ip(&req->client_addr), req->client_port);
    if (error != ERR_OK) {
        DPRINTF(("%s: udp_sendto err %s\n",
                 __func__, proxy_lwip_strerr(error)));
    }

    pxdns_request_free(req);
}


/**
 * TCP DNS proxy.  This kicks in for large replies that don't fit into
 * 512 bytes of UDP payload.  Client will retry with TCP to get
 * complete reply.
 */
static err_t
pxdns_accept_syn(void *arg, struct tcp_pcb *newpcb, struct pbuf *syn)
{
    struct pxdns *pxdns = (struct pxdns *)arg;
    union sockaddr_inet *si;
    ipX_addr_t *dst;
    u16_t dst_port;

    tcp_accepted(pxdns->ltcp);

    if (pxdns->nresolvers == 0) {
        return ERR_CONN;
    }

    si = &pxdns->resolvers[0];

    if (si->sa.sa_family == AF_INET6) {
        dst = (ipX_addr_t *)&si->sin6.sin6_addr;
        dst_port = ntohs(si->sin6.sin6_port);
    }
    else {
        dst = (ipX_addr_t *)&si->sin.sin_addr;
        dst_port = ntohs(si->sin.sin_port);
    }

    /*
     * XXX: TODO: need to implement protocol hooks.  E.g. here if
     * connect fails, we should try connecting to a different server.
     */
    return pxtcp_pcb_accept_outbound(newpcb, syn,
               si->sa.sa_family == AF_INET6, dst, dst_port);
}
