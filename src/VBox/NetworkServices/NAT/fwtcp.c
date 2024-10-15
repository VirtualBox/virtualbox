/* $Id: fwtcp.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - TCP port-forwarding.
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
#include "pxtcp.h"

#ifndef RT_OS_WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <poll.h>

#include <err.h>                /* BSD'ism */
#else
#include <stdio.h>
#include "winpoll.h"
#endif

#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/tcpip.h"


/**
 */
struct fwtcp {
    /**
     * Our poll manager handler.
     */
    struct pollmgr_handler pmhdl;

    /**
     * Forwarding specification.
     */
    struct fwspec fwspec;

    /**
     * Listening socket.
     */
    SOCKET sock;

    /**
     * Mailbox for new inbound connections.
     *
     * XXX: since we have single producer and single consumer we can
     * use lockless ringbuf like for pxtcp.
     */
    sys_mbox_t connmbox;

    struct tcpip_msg msg_connect;
    struct tcpip_msg msg_delete;

    /**
     * Linked list entry.
     */
    struct fwtcp *next;
};


static struct fwtcp *fwtcp_create(struct fwspec *);

/* poll manager callback for fwtcp listening socket */
static int fwtcp_pmgr_listen(struct pollmgr_handler *, SOCKET, int);

/* lwip thread callbacks called via proxy_lwip_post() */
static void fwtcp_pcb_connect(void *);
static void fwtcp_pcb_delete(void *);


/**
 * Linked list of active fwtcp forwarders.
 */
struct fwtcp *fwtcp_list = NULL;


void
fwtcp_init(void)
{
    return;
}


void
fwtcp_add(struct fwspec *fwspec)
{
    struct fwtcp *fwtcp;

    fwtcp = fwtcp_create(fwspec);
    if (fwtcp == NULL) {
        DPRINTF0(("%s: failed to add rule for TCP ...\n", __func__));
        return;
    }

    DPRINTF0(("%s\n", __func__));
    /* fwtcp_create has put fwtcp on the linked list */
}


void
fwtcp_del(struct fwspec *fwspec)
{
    struct fwtcp *fwtcp;
    struct fwtcp **pprev;

    for (pprev = &fwtcp_list; (fwtcp = *pprev) != NULL; pprev = &fwtcp->next) {
        if (fwspec_equal(&fwtcp->fwspec, fwspec)) {
            *pprev = fwtcp->next;
            fwtcp->next = NULL;
            break;
        }
    }

    if (fwtcp == NULL) {
        DPRINTF0(("%s: not found\n", __func__));
        return;
    }

    DPRINTF0(("%s\n", __func__));

    pollmgr_del_slot(fwtcp->pmhdl.slot);
    fwtcp->pmhdl.slot = -1;

    closesocket(fwtcp->sock);
    fwtcp->sock = INVALID_SOCKET;

    /* let pending msg_connect be processed before we delete fwtcp */
    proxy_lwip_post(&fwtcp->msg_delete);
}


struct fwtcp *
fwtcp_create(struct fwspec *fwspec)
{
    struct fwtcp *fwtcp;
    SOCKET lsock;
    int status;
    err_t error;

    lsock = proxy_bound_socket(fwspec->sdom, fwspec->stype, &fwspec->src.sa);
    if (lsock == INVALID_SOCKET) {
        return NULL;
    }

    fwtcp = (struct fwtcp *)malloc(sizeof(*fwtcp));
    if (fwtcp == NULL) {
        closesocket(lsock);
        return NULL;
    }

    fwtcp->pmhdl.callback = fwtcp_pmgr_listen;
    fwtcp->pmhdl.data = (void *)fwtcp;
    fwtcp->pmhdl.slot = -1;

    fwtcp->sock = lsock;
    fwtcp->fwspec = *fwspec;    /* struct copy */

    error = sys_mbox_new(&fwtcp->connmbox, 16);
    if (error != ERR_OK) {
        closesocket(lsock);
        free(fwtcp);
        return (NULL);
    }

#define CALLBACK_MSG(MSG, FUNC)                         \
    do {                                                \
        fwtcp->MSG.type = TCPIP_MSG_CALLBACK_STATIC;    \
        fwtcp->MSG.sem = NULL;                          \
        fwtcp->MSG.msg.cb.function = FUNC;              \
        fwtcp->MSG.msg.cb.ctx = (void *)fwtcp;          \
    } while (0)

    CALLBACK_MSG(msg_connect, fwtcp_pcb_connect);
    CALLBACK_MSG(msg_delete,  fwtcp_pcb_delete);

#undef CALLBACK_MSG

    status = pollmgr_add(&fwtcp->pmhdl, fwtcp->sock, POLLIN);
    if (status < 0) {
        sys_mbox_free(&fwtcp->connmbox);
        closesocket(lsock);
        free(fwtcp);
        return NULL;
    }

    fwtcp->next = fwtcp_list;
    fwtcp_list = fwtcp;

    return fwtcp;
}


int
fwtcp_pmgr_listen(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct fwtcp *fwtcp;
    struct sockaddr_storage ss;
    socklen_t sslen;
    struct pxtcp *pxtcp;
    SOCKET newsock;
    int status;
    err_t error;

    fwtcp = (struct fwtcp *)handler->data;
    pxtcp = NULL;

    LWIP_ASSERT1(fwtcp != NULL);
    LWIP_ASSERT1(fd == fwtcp->sock);
    LWIP_ASSERT1(revents == POLLIN);
    LWIP_UNUSED_ARG(fd);
    LWIP_UNUSED_ARG(revents);

    LWIP_ASSERT1(sys_mbox_valid(&fwtcp->connmbox));

    sslen = sizeof(ss);
    newsock = accept(fwtcp->sock, (struct sockaddr *)&ss, &sslen);
    if (newsock == INVALID_SOCKET) {
        return POLLIN;
    }

#ifdef RT_OS_LINUX
    status = proxy_fixup_accepted_socket(newsock);
    if (status < 0) {
        proxy_reset_socket(newsock);
        return POLLIN;
    }
#endif

    if (ss.ss_family == PF_INET) {
        struct sockaddr_in *peer4 = (struct sockaddr_in *)&ss;
        RT_NOREF(peer4);
        DPRINTF(("<--- TCP %RTnaipv4:%d\n",
                 peer4->sin_addr.s_addr, ntohs(peer4->sin_port)));
    }
    else { /* PF_INET6 */
        struct sockaddr_in6 *peer6 = (struct sockaddr_in6 *)&ss;
        RT_NOREF(peer6);
        DPRINTF(("<--- TCP %RTnaipv6:%d\n",
                 &peer6->sin6_addr, ntohs(peer6->sin6_port)));
    }

    pxtcp = pxtcp_create_forwarded(newsock);
    if (pxtcp == NULL) {
        proxy_reset_socket(newsock);
        return POLLIN;
    }

    status = pxtcp_pmgr_add(pxtcp);
    if (status < 0) {
        pxtcp_cancel_forwarded(pxtcp);
        return POLLIN;
    }

    error = sys_mbox_trypost(&fwtcp->connmbox, (void *)pxtcp);
    if (error != ERR_OK) {
        pxtcp_pmgr_del(pxtcp);
        pxtcp_cancel_forwarded(pxtcp);
        return POLLIN;
    }

    proxy_lwip_post(&fwtcp->msg_connect);
    return POLLIN;
}


void
fwtcp_pcb_connect(void *arg)
{
    struct fwtcp *fwtcp = (struct fwtcp *)arg;
    struct pxtcp *pxtcp;
    u32_t timo;

    if (!sys_mbox_valid(&fwtcp->connmbox)) {
        return;
    }

    pxtcp = NULL;
    timo = sys_mbox_tryfetch(&fwtcp->connmbox, (void **)&pxtcp);
    if (timo == SYS_MBOX_EMPTY) {
        return;
    }

    LWIP_ASSERT1(pxtcp != NULL);

    /* hand off to pxtcp */
    pxtcp_pcb_connect(pxtcp, &fwtcp->fwspec);
}


static void
fwtcp_pcb_delete(void *arg)
{
    struct fwtcp *fwtcp = (struct fwtcp *)arg;
    void *data;
    u32_t timo;

    timo = sys_mbox_tryfetch(&fwtcp->connmbox, &data);
    LWIP_ASSERT1(timo == SYS_MBOX_EMPTY);
    LWIP_UNUSED_ARG(timo);      /* only in assert */

    sys_mbox_free(&fwtcp->connmbox);
    free(fwtcp);
}
