/* $Id: fwudp.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - UDP port-forwarding.
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
#include "portfwd.h"
#include "pxremap.h"

#ifndef RT_OS_WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#include <err.h>                /* BSD'ism */
#else
#include <stdio.h>
#include <string.h>
#include "winpoll.h"
#endif

#include "lwip/opt.h"
#include "lwip/memp.h"          /* XXX: for bulk delete of pcbs */

#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"

struct fwudp_dgram {
    struct pbuf *p;
    ipX_addr_t src_addr;
    u16_t src_port;
};

/**
 * UDP port-forwarding.
 *
 * Unlike pxudp that uses 1:1 mapping between pcb and socket, for
 * port-forwarded UDP the setup is bit more elaborated.
 *
 * For fwtcp things are simple since incoming TCP connection get a new
 * socket that we just hand off to pxtcp. Thus fwtcp only handles
 * connection initiation.
 *
 * For fwudp all proxied UDP conversations share the same socket, so
 * single fwudp multiplexes to several UDP pcbs.
 *
 * XXX: TODO: Currently pcbs point back directly to fwudp.  It might
 * make sense to introduce a per-pcb structure that points to fwudp
 * and carries additional information, like pre-mapped peer address.
 */
struct fwudp {
    /**
     * Our poll manager handler.
     */
    struct pollmgr_handler pmhdl;

    /**
     * Forwarding specification.
     */
    struct fwspec fwspec;

    /**
     * XXX: lwip-format copy of destination
     */
    ipX_addr_t dst_addr;
    u16_t dst_port;

    /**
     * Listening socket.
     */
    SOCKET sock;

    /**
     * Ring-buffer for inbound datagrams.
     */
    struct {
        struct fwudp_dgram *buf;
        size_t bufsize;
        volatile size_t vacant;
        volatile size_t unsent;
    } inbuf;

    struct tcpip_msg msg_send;
    struct tcpip_msg msg_delete;

    struct fwudp *next;
};


struct fwudp *fwudp_create(struct fwspec *);

/* poll manager callback for fwudp socket */
static int fwudp_pmgr_pump(struct pollmgr_handler *, SOCKET, int);

/* lwip thread callbacks called via proxy_lwip_post() */
static void fwudp_pcb_send(void *);
static void fwudp_pcb_delete(void *);

static void fwudp_pcb_recv(void *, struct udp_pcb *, struct pbuf *, ip_addr_t *, u16_t);
static void fwudp_pcb_forward_outbound(struct fwudp *, struct udp_pcb *, struct pbuf *);


/**
 * Linked list of active fwtcp forwarders.
 */
struct fwudp *fwudp_list = NULL;


void
fwudp_init(void)
{
    return;
}


void
fwudp_add(struct fwspec *fwspec)
{
    struct fwudp *fwudp;

    fwudp = fwudp_create(fwspec);
    if (fwudp == NULL) {
        DPRINTF0(("%s: failed to add rule for UDP ...\n", __func__));
        return;
    }

    DPRINTF0(("%s\n", __func__));
    /* fwudp_create has put fwudp on the linked list */
}


void
fwudp_del(struct fwspec *fwspec)
{
    struct fwudp *fwudp;
    struct fwudp **pprev;

    for (pprev = &fwudp_list; (fwudp = *pprev) != NULL; pprev = &fwudp->next) {
        if (fwspec_equal(&fwudp->fwspec, fwspec)) {
            *pprev = fwudp->next;
            fwudp->next = NULL;
            break;
        }
    }

    if (fwudp == NULL) {
        DPRINTF0(("%s: not found\n", __func__));
        return;
    }

    DPRINTF0(("%s\n", __func__));

    pollmgr_del_slot(fwudp->pmhdl.slot);
    fwudp->pmhdl.slot = -1;

    /* let pending msg_send be processed before we delete fwudp */
    proxy_lwip_post(&fwudp->msg_delete);
}


struct fwudp *
fwudp_create(struct fwspec *fwspec)
{
    struct fwudp *fwudp;
    SOCKET sock;
    int status;

    sock = proxy_bound_socket(fwspec->sdom, fwspec->stype, &fwspec->src.sa);
    if (sock == INVALID_SOCKET) {
        return NULL;
    }

    fwudp = (struct fwudp *)malloc(sizeof(*fwudp));
    if (fwudp == NULL) {
        closesocket(sock);
        return NULL;
    }

    fwudp->pmhdl.callback = fwudp_pmgr_pump;
    fwudp->pmhdl.data = (void *)fwudp;
    fwudp->pmhdl.slot = -1;

    fwudp->sock = sock;
    fwudp->fwspec = *fwspec;    /* struct copy */

    /* XXX */
    if (fwspec->sdom == PF_INET) {
        struct sockaddr_in *dst4 = &fwspec->dst.sin;
        memcpy(&fwudp->dst_addr.ip4, &dst4->sin_addr, sizeof(ip_addr_t));
        fwudp->dst_port = htons(dst4->sin_port);
    }
    else { /* PF_INET6 */
        struct sockaddr_in6 *dst6 = &fwspec->dst.sin6;
        memcpy(&fwudp->dst_addr.ip6, &dst6->sin6_addr, sizeof(ip6_addr_t));
        fwudp->dst_port = htons(dst6->sin6_port);
    }

    fwudp->inbuf.bufsize = 256; /* elements  */
    fwudp->inbuf.buf
        = (struct fwudp_dgram *)calloc(fwudp->inbuf.bufsize,
                                       sizeof(struct fwudp_dgram));
    if (fwudp->inbuf.buf == NULL) {
        closesocket(sock);
        free(fwudp);
        return (NULL);
    }
    fwudp->inbuf.vacant = 0;
    fwudp->inbuf.unsent = 0;

#define CALLBACK_MSG(MSG, FUNC)                         \
    do {                                                \
        fwudp->MSG.type = TCPIP_MSG_CALLBACK_STATIC;    \
        fwudp->MSG.sem = NULL;                          \
        fwudp->MSG.msg.cb.function = FUNC;              \
        fwudp->MSG.msg.cb.ctx = (void *)fwudp;          \
    } while (0)

    CALLBACK_MSG(msg_send, fwudp_pcb_send);
    CALLBACK_MSG(msg_delete, fwudp_pcb_delete);

#undef CALLBACK_MSG

    status = pollmgr_add(&fwudp->pmhdl, fwudp->sock, POLLIN);
    if (status < 0) {
        closesocket(sock);
        free(fwudp->inbuf.buf);
        free(fwudp);
        return NULL;
    }

    fwudp->next = fwudp_list;
    fwudp_list = fwudp;

    return fwudp;
}


/**
 * Poll manager callaback for fwudp::sock
 */
int
fwudp_pmgr_pump(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct fwudp *fwudp;
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    size_t beg, lim;
    struct fwudp_dgram *dgram;
    struct pbuf *p;
    ssize_t nread;
    int status;
    err_t error;

    fwudp = (struct fwudp *)handler->data;

    LWIP_ASSERT1(fwudp != NULL);
    LWIP_ASSERT1(fd == fwudp->sock);
    LWIP_ASSERT1(revents == POLLIN);
    LWIP_UNUSED_ARG(fd);
    LWIP_UNUSED_ARG(revents);

#ifdef RT_OS_WINDOWS
    nread = recvfrom(fwudp->sock, (char *)pollmgr_udpbuf, sizeof(pollmgr_udpbuf), 0,
                     (struct sockaddr *)&ss, &sslen);
#else
    nread = recvfrom(fwudp->sock, pollmgr_udpbuf, sizeof(pollmgr_udpbuf), 0,
                     (struct sockaddr *)&ss, &sslen);
#endif
    if (nread < 0) {
        DPRINTF(("%s: %R[sockerr]\n", __func__, SOCKERRNO()));
        return POLLIN;
    }

    /* Check that ring buffer is not full */
    lim = fwudp->inbuf.unsent;
    if (lim == 0) {
        lim = fwudp->inbuf.bufsize - 1; /* guard slot at the end */
    }
    else {
        --lim;
    }

    beg = fwudp->inbuf.vacant;
    if (beg == lim) { /* no vacant slot */
        return POLLIN;
    }


    dgram = &fwudp->inbuf.buf[beg];


    status = fwany_ipX_addr_set_src(&dgram->src_addr, (struct sockaddr *)&ss);
    if (status == PXREMAP_FAILED) {
        return POLLIN;
    }

    if (ss.ss_family == AF_INET) {
        const struct sockaddr_in *peer4 = (const struct sockaddr_in *)&ss;
        dgram->src_port = htons(peer4->sin_port);
    }
    else { /* PF_INET6 */
        const struct sockaddr_in6 *peer6 = (const struct sockaddr_in6 *)&ss;
        dgram->src_port = htons(peer6->sin6_port);
    }

    p = pbuf_alloc(PBUF_RAW, nread, PBUF_RAM);
    if (p == NULL) {
        DPRINTF(("%s: pbuf_alloc(%d) failed\n", __func__, (int)nread));
        return POLLIN;
    }

    error = pbuf_take(p, pollmgr_udpbuf, nread);
    if (error != ERR_OK) {
        DPRINTF(("%s: pbuf_take(%d) failed\n", __func__, (int)nread));
        pbuf_free(p);
        return POLLIN;
    }

    dgram->p = p;

    ++beg;
    if (beg == fwudp->inbuf.bufsize) {
        beg = 0;
    }
    fwudp->inbuf.vacant = beg;

    proxy_lwip_post(&fwudp->msg_send);

    return POLLIN;
}


/**
 * Lwip thread callback invoked via fwudp::msg_send
 */
void
fwudp_pcb_send(void *arg)
{
    struct fwudp *fwudp = (struct fwudp *)arg;
    struct fwudp_dgram dgram;
    struct udp_pcb *pcb;
    struct udp_pcb **pprev;
    int isv6;
    size_t idx;

    idx = fwudp->inbuf.unsent;

    if (idx == fwudp->inbuf.vacant) {
        /* empty buffer - shouldn't happen! */
        DPRINTF(("%s: ring buffer empty!\n", __func__));
        return;
    }

    dgram = fwudp->inbuf.buf[idx]; /* struct copy */
#if 1 /* valgrind hint */
    fwudp->inbuf.buf[idx].p = NULL;
#endif
    if (++idx == fwudp->inbuf.bufsize) {
        idx = 0;
    }
    fwudp->inbuf.unsent = idx;

    /* XXX: this is *STUPID* */
    isv6 = (fwudp->fwspec.sdom == PF_INET6);
    pprev = &udp_proxy_pcbs;
    for (pcb = udp_proxy_pcbs; pcb != NULL; pcb = pcb->next) {
        if (PCB_ISIPV6(pcb) == isv6
            && pcb->remote_port == fwudp->dst_port
            && ipX_addr_cmp(isv6, &fwudp->dst_addr, &pcb->remote_ip)
            && pcb->local_port == dgram.src_port
            && ipX_addr_cmp(isv6, &dgram.src_addr, &pcb->local_ip))
        {
            break;
        }
        else {
            pprev = &pcb->next;
        }
    }

    if (pcb != NULL) {
        *pprev = pcb->next;
        pcb->next = udp_proxy_pcbs;
        udp_proxy_pcbs = pcb;

        /*
         * XXX: check that its ours and not accidentally created by
         * outbound traffic.
         *
         * ???: Otherwise?  Expire it and set pcb = NULL; to create a
         * new one below?
         */
    }

    if (pcb == NULL) {
        pcb = udp_new();
        if (pcb == NULL) {
            goto out;
        }

        ip_set_v6(pcb, isv6);

        /* equivalent of udp_bind */
        ipX_addr_set(isv6, &pcb->local_ip, &dgram.src_addr);
        pcb->local_port = dgram.src_port;

        /* equivalent to udp_connect */
        ipX_addr_set(isv6, &pcb->remote_ip, &fwudp->dst_addr);
        pcb->remote_port = fwudp->dst_port;
        pcb->flags |= UDP_FLAGS_CONNECTED;

        udp_recv(pcb, fwudp_pcb_recv, fwudp);

        pcb->next = udp_proxy_pcbs;
        udp_proxy_pcbs = pcb;
        udp_proxy_timer_needed();
    }

    udp_send(pcb, dgram.p);

  out:
    pbuf_free(dgram.p);
}


/**
 * udp_recv() callback.
 */
void
fwudp_pcb_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
               ip_addr_t *addr, u16_t port)
{
    struct fwudp *fwudp = (struct fwudp *)arg;

    LWIP_UNUSED_ARG(addr);
    LWIP_UNUSED_ARG(port);

    LWIP_ASSERT1(fwudp != NULL);

    if (p == NULL) {
        DPRINTF(("%s: pcb %p (fwudp %p); sock %d: expired\n",
                 __func__, (void *)pcb, (void *)fwudp, fwudp->sock));
        /* NB: fwudp is "global" and not deleted */
        /* XXX: TODO: delete local reference when we will keep one */
        udp_remove(pcb);
        return;
    }
    else {
        fwudp_pcb_forward_outbound(fwudp, pcb, p);
    }
}


/*
 * XXX: This is pxudp_pcb_forward_outbound modulo:
 * - s/pxudp/fwudp/g
 * - addr/port (unused in either) dropped
 * - destination is specified since host socket is not connected
 */
static void
fwudp_pcb_forward_outbound(struct fwudp *fwudp, struct udp_pcb *pcb,
                           struct pbuf *p)
{
    union {
        struct sockaddr_in sin;
        struct sockaddr_in6 sin6;
    } peer;
    socklen_t namelen;

    memset(&peer, 0, sizeof(peer)); /* XXX: shut up valgrind */

    if (fwudp->fwspec.sdom == PF_INET) {
        peer.sin.sin_family = AF_INET;
#if HAVE_SA_LEN
        peer.sin.sin_len =
#endif
        namelen = sizeof(peer.sin);
        pxremap_outbound_ip4((ip_addr_t *)&peer.sin.sin_addr, &pcb->local_ip.ip4);
        peer.sin.sin_port = htons(pcb->local_port);
    }
    else {
        peer.sin6.sin6_family = AF_INET6;
#if HAVE_SA_LEN
        peer.sin6.sin6_len =
#endif
        namelen = sizeof(peer.sin6);

        pxremap_outbound_ip6((ip6_addr_t *)&peer.sin6.sin6_addr, &pcb->local_ip.ip6);
        peer.sin6.sin6_port = htons(pcb->local_port);
    }

    proxy_sendto(fwudp->sock, p, &peer, namelen);
    pbuf_free(p);
}


/**
 * Lwip thread callback invoked via fwudp::msg_delete
 */
static void
fwudp_pcb_delete(void *arg)
{
    struct fwudp *fwudp = (struct fwudp *)arg;
    struct udp_pcb *pcb;
    struct udp_pcb **pprev;

    LWIP_ASSERT1(fwudp->inbuf.unsent == fwudp->inbuf.vacant);

    pprev = &udp_proxy_pcbs;
    pcb = udp_proxy_pcbs;
    while (pcb != NULL) {
        if (pcb->recv_arg != fwudp) {
            pprev = &pcb->next;
            pcb = pcb->next;
        }
        else {
            struct udp_pcb *dead = pcb;
            pcb = pcb->next;
            *pprev = pcb;
            memp_free(MEMP_UDP_PCB, dead);
        }
    }

    closesocket(fwudp->sock);
    free(fwudp->inbuf.buf);
    free(fwudp);
}
