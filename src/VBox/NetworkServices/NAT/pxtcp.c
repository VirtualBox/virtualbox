/* $Id: pxtcp.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - TCP proxy.
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

#include "pxtcp.h"

#include "proxy.h"
#include "proxy_pollmgr.h"
#include "pxremap.h"
#include "portfwd.h"            /* fwspec */

#ifndef RT_OS_WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#ifdef RT_OS_SOLARIS
#include <sys/filio.h>          /* FIONREAD is BSD'ism */
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#include <err.h>                /* BSD'ism */
#else
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <iprt/stdint.h>
#include "winpoll.h"
#endif

#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/tcp_impl.h"      /* XXX: to access tcp_abandon() */
#include "lwip/icmp.h"
#include "lwip/icmp6.h"

/*
 * Different OSes have different quirks in reporting POLLHUP for TCP
 * sockets.
 *
 * Using shutdown(2) "how" values here would be more readable, but
 * since SHUT_RD is 0, we can't use 0 for "none", unfortunately.
 */
#if defined(RT_OS_NETBSD) || defined(RT_OS_SOLARIS)
# define HAVE_TCP_POLLHUP 0                     /* not reported */
#elif defined(RT_OS_DARWIN) || defined(RT_OS_WINDOWS)
# define HAVE_TCP_POLLHUP POLLIN                /* reported when remote closes */
#else
# define HAVE_TCP_POLLHUP (POLLIN|POLLOUT)      /* reported when both directions are closed */
#endif


/**
 * Ring buffer for inbound data.  Filled with data from the host
 * socket on poll manager thread.  Data consumed by scheduling
 * tcp_write() to the pcb on the lwip thread.
 *
 * NB: There is actually third party present, the lwip stack itself.
 * Thus the buffer doesn't have dual free vs. data split, but rather
 * three-way free / send and unACKed data / unsent data split.
 */
struct ringbuf {
    char *buf;
    size_t bufsize;

    /*
     * Start of free space, producer writes here (up till "unacked").
     */
    volatile size_t vacant;

    /*
     * Start of sent but unacknowledged data.  The data are "owned" by
     * the stack as it may need to retransmit.  This is the free space
     * limit for producer.
     */
    volatile size_t unacked;

    /*
     * Start of unsent data, consumer reads/sends from here (up till
     * "vacant").  Not declared volatile since it's only accessed from
     * the consumer thread.
     */
    size_t unsent;
};


/**
 */
struct pxtcp {
    /**
     * Our poll manager handler.  Must be first, strong/weak
     * references depend on this "inheritance".
     */
    struct pollmgr_handler pmhdl;

    /**
     * lwIP (internal/guest) side of the proxied connection.
     */
    struct tcp_pcb *pcb;

    /**
     * Host (external) side of the proxied connection.
     */
    SOCKET sock;

    /**
     * Socket events we are currently polling for.
     */
    int events;

    /**
     * Socket error.  Currently used to save connect(2) errors so that
     * we can decide if we need to send ICMP error.
     */
    int sockerr;

    /**
     * Interface that we have got the SYN from.  Needed to send ICMP
     * with correct source address.
     */
    struct netif *netif;

    /**
     * For tentatively accepted connections for which we are in
     * process of connecting to the real destination this is the
     * initial pbuf that we might need to build ICMP error.
     *
     * When connection is established this is used to hold outbound
     * pbuf chain received by pxtcp_pcb_recv() but not yet completely
     * forwarded over the socket.  We cannot "return" it to lwIP since
     * the head of the chain is already sent and freed.
     */
    struct pbuf *unsent;

    /**
     * Guest has closed its side. Reported to pxtcp_pcb_recv() only
     * once and we might not be able to forward it immediately if we
     * have unsent pbuf.
     */
    int outbound_close;

    /**
     * Outbound half-close has been done on the socket.
     */
    int outbound_close_done;

    /**
     * External has closed its side.  We might not be able to forward
     * it immediately if we have unforwarded data.
     */
    int inbound_close;

    /**
     * Inbound half-close has been done on the pcb.
     */
    int inbound_close_done;

    /**
     * On systems that report POLLHUP as soon as the final FIN is
     * received on a socket we cannot continue polling for the rest of
     * input, so we have to read (pull) last data from the socket on
     * the lwIP thread instead of polling/pushing it from the poll
     * manager thread.  See comment in pxtcp_pmgr_pump() POLLHUP case.
     */
    int inbound_pull;


    /**
     * When poll manager schedules delete we may not be able to delete
     * a pxtcp immediately if not all inbound data has been acked by
     * the guest: lwIP may need to resend and the data are in pxtcp's
     * inbuf::buf.  We defer delete until all data are acked to
     * pxtcp_pcb_sent().
     */
    int deferred_delete;

    /**
     * Ring-buffer for inbound data.
     */
    struct ringbuf inbuf;

    /**
     * lwIP thread's strong reference to us.
     */
    struct pollmgr_refptr *rp;


    /*
     * We use static messages to call functions on the lwIP thread to
     * void malloc/free overhead.
     */
    struct tcpip_msg msg_delete;   /* delete pxtcp */
    struct tcpip_msg msg_reset;    /* reset connection and delete pxtcp */
    struct tcpip_msg msg_accept;   /* confirm accept of proxied connection */
    struct tcpip_msg msg_outbound; /* trigger send of outbound data */
    struct tcpip_msg msg_inbound;  /* trigger send of inbound data */
#if HAVE_TCP_POLLHUP
    struct tcpip_msg msg_inpull;   /* trigger pull of last inbound data */
#endif
};



static struct pxtcp *pxtcp_allocate(void);
static void pxtcp_free(struct pxtcp *);

static void pxtcp_pcb_associate(struct pxtcp *, struct tcp_pcb *);
static void pxtcp_pcb_dissociate(struct pxtcp *);

/* poll manager callbacks for pxtcp related channels */
static int pxtcp_pmgr_chan_add(struct pollmgr_handler *, SOCKET, int);
static int pxtcp_pmgr_chan_pollout(struct pollmgr_handler *, SOCKET, int);
static int pxtcp_pmgr_chan_pollin(struct pollmgr_handler *, SOCKET, int);
#if !(HAVE_TCP_POLLHUP & POLLOUT)
static int pxtcp_pmgr_chan_del(struct pollmgr_handler *, SOCKET, int);
#endif
static int pxtcp_pmgr_chan_reset(struct pollmgr_handler *, SOCKET, int);

/* helper functions for sending/receiving pxtcp over poll manager channels */
static ssize_t pxtcp_chan_send(enum pollmgr_slot_t, struct pxtcp *);
static ssize_t pxtcp_chan_send_weak(enum pollmgr_slot_t, struct pxtcp *);
static struct pxtcp *pxtcp_chan_recv(struct pollmgr_handler *, SOCKET, int);
static struct pxtcp *pxtcp_chan_recv_strong(struct pollmgr_handler *, SOCKET, int);

/* poll manager callbacks for individual sockets */
static int pxtcp_pmgr_connect(struct pollmgr_handler *, SOCKET, int);
static int pxtcp_pmgr_pump(struct pollmgr_handler *, SOCKET, int);

/* get incoming traffic into ring buffer */
static ssize_t pxtcp_sock_read(struct pxtcp *, int *);
static ssize_t pxtcp_sock_recv(struct pxtcp *, IOVEC *, size_t); /* default */

/* convenience functions for poll manager callbacks */
static int pxtcp_schedule_delete(struct pxtcp *);
static int pxtcp_schedule_reset(struct pxtcp *);
static int pxtcp_schedule_reject(struct pxtcp *);

/* lwip thread callbacks called via proxy_lwip_post() */
static void pxtcp_pcb_delete_pxtcp(void *);
static void pxtcp_pcb_reset_pxtcp(void *);
static void pxtcp_pcb_accept_refuse(void *);
static void pxtcp_pcb_accept_confirm(void *);
static void pxtcp_pcb_write_outbound(void *);
static void pxtcp_pcb_write_inbound(void *);
#if HAVE_TCP_POLLHUP
static void pxtcp_pcb_pull_inbound(void *);
#endif

/* tcp pcb callbacks */
static err_t pxtcp_pcb_heard(void *, struct tcp_pcb *, struct pbuf *); /* global */
static err_t pxtcp_pcb_accept(void *, struct tcp_pcb *, err_t);
static err_t pxtcp_pcb_connected(void *, struct tcp_pcb *, err_t);
static err_t pxtcp_pcb_recv(void *, struct tcp_pcb *, struct pbuf *, err_t);
static err_t pxtcp_pcb_sent(void *, struct tcp_pcb *, u16_t);
static err_t pxtcp_pcb_poll(void *, struct tcp_pcb *);
static void pxtcp_pcb_err(void *, err_t);

static err_t pxtcp_pcb_forward_outbound(struct pxtcp *, struct pbuf *);
static void pxtcp_pcb_forward_outbound_close(struct pxtcp *);

static ssize_t pxtcp_sock_send(struct pxtcp *, IOVEC *, size_t);

static void pxtcp_pcb_forward_inbound(struct pxtcp *);
static void pxtcp_pcb_forward_inbound_close(struct pxtcp *);
DECLINLINE(int) pxtcp_pcb_forward_inbound_done(const struct pxtcp *);
static void pxtcp_pcb_schedule_poll(struct pxtcp *);
static void pxtcp_pcb_cancel_poll(struct pxtcp *);

static void pxtcp_pcb_reject(struct tcp_pcb *, int, struct netif *, struct pbuf *);
DECLINLINE(void) pxtcp_pcb_maybe_deferred_delete(struct pxtcp *);

/* poll manager handlers for pxtcp channels */
static struct pollmgr_handler pxtcp_pmgr_chan_add_hdl;
static struct pollmgr_handler pxtcp_pmgr_chan_pollout_hdl;
static struct pollmgr_handler pxtcp_pmgr_chan_pollin_hdl;
#if !(HAVE_TCP_POLLHUP & POLLOUT)
static struct pollmgr_handler pxtcp_pmgr_chan_del_hdl;
#endif
static struct pollmgr_handler pxtcp_pmgr_chan_reset_hdl;


/**
 * Init PXTCP - must be run when neither lwIP tcpip thread, nor poll
 * manager threads haven't been created yet.
 */
void
pxtcp_init(void)
{
    /*
     * Create channels.
     */
#define CHANNEL(SLOT, NAME) do {                \
        NAME##_hdl.callback = NAME;             \
        NAME##_hdl.data = NULL;                 \
        NAME##_hdl.slot = -1;                   \
        pollmgr_add_chan(SLOT, &NAME##_hdl);    \
    } while (0)

    CHANNEL(POLLMGR_CHAN_PXTCP_ADD,     pxtcp_pmgr_chan_add);
    CHANNEL(POLLMGR_CHAN_PXTCP_POLLIN,  pxtcp_pmgr_chan_pollin);
    CHANNEL(POLLMGR_CHAN_PXTCP_POLLOUT, pxtcp_pmgr_chan_pollout);
#if !(HAVE_TCP_POLLHUP & POLLOUT)
    CHANNEL(POLLMGR_CHAN_PXTCP_DEL,     pxtcp_pmgr_chan_del);
#endif
    CHANNEL(POLLMGR_CHAN_PXTCP_RESET,   pxtcp_pmgr_chan_reset);

#undef CHANNEL

    /*
     * Listen to outgoing connection from guest(s).
     */
    tcp_proxy_accept(pxtcp_pcb_heard);
}


/**
 * Syntactic sugar for sending pxtcp pointer over poll manager
 * channel.  Used by lwip thread functions.
 */
static ssize_t
pxtcp_chan_send(enum pollmgr_slot_t slot, struct pxtcp *pxtcp)
{
    return pollmgr_chan_send(slot, &pxtcp, sizeof(pxtcp));
}


/**
 * Syntactic sugar for sending weak reference to pxtcp over poll
 * manager channel.  Used by lwip thread functions.
 */
static ssize_t
pxtcp_chan_send_weak(enum pollmgr_slot_t slot, struct pxtcp *pxtcp)
{
    pollmgr_refptr_weak_ref(pxtcp->rp);
    return pollmgr_chan_send(slot, &pxtcp->rp, sizeof(pxtcp->rp));
}


/**
 * Counterpart of pxtcp_chan_send().
 */
static struct pxtcp *
pxtcp_chan_recv(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;

    pxtcp = (struct pxtcp *)pollmgr_chan_recv_ptr(handler, fd, revents);
    return pxtcp;
}


/**
 * Counterpart of pxtcp_chan_send_weak().
 */
static struct pxtcp *
pxtcp_chan_recv_strong(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pollmgr_refptr *rp;
    struct pollmgr_handler *base;
    struct pxtcp *pxtcp;

    rp = (struct pollmgr_refptr *)pollmgr_chan_recv_ptr(handler, fd, revents);
    base = (struct pollmgr_handler *)pollmgr_refptr_get(rp);
    pxtcp = (struct pxtcp *)base;

    return pxtcp;
}


/**
 * Register pxtcp with poll manager.
 *
 * Used for POLLMGR_CHAN_PXTCP_ADD and by port-forwarding.  Since
 * error handling is different in these two cases, we leave it up to
 * the caller.
 */
int
pxtcp_pmgr_add(struct pxtcp *pxtcp)
{
    int status;

    LWIP_ASSERT1(pxtcp != NULL);
#ifdef RT_OS_WINDOWS
    LWIP_ASSERT1(pxtcp->sock != INVALID_SOCKET);
#else
    LWIP_ASSERT1(pxtcp->sock >= 0);
#endif
    LWIP_ASSERT1(pxtcp->pmhdl.callback != NULL);
    LWIP_ASSERT1(pxtcp->pmhdl.data == (void *)pxtcp);
    LWIP_ASSERT1(pxtcp->pmhdl.slot < 0);

    status = pollmgr_add(&pxtcp->pmhdl, pxtcp->sock, pxtcp->events);
    return status;
}


/**
 * Unregister pxtcp with poll manager.
 *
 * Used for POLLMGR_CHAN_PXTCP_RESET and by port-forwarding (on error
 * leg).
 */
void
pxtcp_pmgr_del(struct pxtcp *pxtcp)
{
    LWIP_ASSERT1(pxtcp != NULL);

    pollmgr_del_slot(pxtcp->pmhdl.slot);
}


/**
 * POLLMGR_CHAN_PXTCP_ADD handler.
 *
 * Get new pxtcp from lwip thread and start polling its socket.
 */
static int
pxtcp_pmgr_chan_add(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;
    int status;

    pxtcp = pxtcp_chan_recv(handler, fd, revents);
    DPRINTF0(("pxtcp_add: new pxtcp %p; pcb %p; sock %d\n",
              (void *)pxtcp, (void *)pxtcp->pcb, pxtcp->sock));

    status = pxtcp_pmgr_add(pxtcp);
    if (status < 0) {
        (void) pxtcp_schedule_reset(pxtcp);
    }

    return POLLIN;
}


/**
 * POLLMGR_CHAN_PXTCP_POLLOUT handler.
 *
 * pxtcp_pcb_forward_outbound() on the lwIP thread tried to send data
 * and failed, it now requests us to poll the socket for POLLOUT and
 * schedule pxtcp_pcb_forward_outbound() when sock is writable again.
 */
static int
pxtcp_pmgr_chan_pollout(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;

    pxtcp = pxtcp_chan_recv_strong(handler, fd, revents);
    DPRINTF0(("pxtcp_pollout: pxtcp %p\n", (void *)pxtcp));

    if (pxtcp == NULL) {
        return POLLIN;
    }

    LWIP_ASSERT1(pxtcp->pmhdl.data == (void *)pxtcp);
    LWIP_ASSERT1(pxtcp->pmhdl.slot > 0);

    pxtcp->events |= POLLOUT;
    pollmgr_update_events(pxtcp->pmhdl.slot, pxtcp->events);

    return POLLIN;
}


/**
 * POLLMGR_CHAN_PXTCP_POLLIN handler.
 */
static int
pxtcp_pmgr_chan_pollin(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;

    pxtcp = pxtcp_chan_recv_strong(handler, fd, revents);
    DPRINTF2(("pxtcp_pollin: pxtcp %p\n", (void *)pxtcp));

    if (pxtcp == NULL) {
        return POLLIN;
    }

    LWIP_ASSERT1(pxtcp->pmhdl.data == (void *)pxtcp);
    LWIP_ASSERT1(pxtcp->pmhdl.slot > 0);

    if (pxtcp->inbound_close) {
        return POLLIN;
    }

    pxtcp->events |= POLLIN;
    pollmgr_update_events(pxtcp->pmhdl.slot, pxtcp->events);

    return POLLIN;
}


#if !(HAVE_TCP_POLLHUP & POLLOUT)
/**
 * POLLMGR_CHAN_PXTCP_DEL handler.
 *
 * Schedule pxtcp deletion.  We only need this if host system doesn't
 * report POLLHUP for fully closed tcp sockets.
 */
static int
pxtcp_pmgr_chan_del(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;

    pxtcp = pxtcp_chan_recv_strong(handler, fd, revents);
    if (pxtcp == NULL) {
        return POLLIN;
    }

    DPRINTF(("PXTCP_DEL: pxtcp %p; pcb %p; sock %d\n",
             (void *)pxtcp, (void *)pxtcp->pcb, pxtcp->sock));

    LWIP_ASSERT1(pxtcp->pmhdl.callback != NULL);
    LWIP_ASSERT1(pxtcp->pmhdl.data == (void *)pxtcp);

    LWIP_ASSERT1(pxtcp->inbound_close);       /* EOF read */
    LWIP_ASSERT1(pxtcp->outbound_close_done); /* EOF sent */

    pxtcp_pmgr_del(pxtcp);
    (void) pxtcp_schedule_delete(pxtcp);

    return POLLIN;
}
#endif  /* !(HAVE_TCP_POLLHUP & POLLOUT)  */


/**
 * POLLMGR_CHAN_PXTCP_RESET handler.
 *
 * Close the socket with RST and delete pxtcp.
 */
static int
pxtcp_pmgr_chan_reset(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;

    pxtcp = pxtcp_chan_recv_strong(handler, fd, revents);
    if (pxtcp == NULL) {
        return POLLIN;
    }

    DPRINTF0(("PXTCP_RESET: pxtcp %p; pcb %p; sock %d\n",
              (void *)pxtcp, (void *)pxtcp->pcb, pxtcp->sock));

    LWIP_ASSERT1(pxtcp->pmhdl.callback != NULL);
    LWIP_ASSERT1(pxtcp->pmhdl.data == (void *)pxtcp);

    pxtcp_pmgr_del(pxtcp);

    proxy_reset_socket(pxtcp->sock);
    pxtcp->sock = INVALID_SOCKET;

    (void) pxtcp_schedule_reset(pxtcp);

    return POLLIN;
}


static struct pxtcp *
pxtcp_allocate(void)
{
    struct pxtcp *pxtcp;

    pxtcp = (struct pxtcp *)malloc(sizeof(*pxtcp));
    if (pxtcp == NULL) {
        return NULL;
    }

    pxtcp->pmhdl.callback = NULL;
    pxtcp->pmhdl.data = (void *)pxtcp;
    pxtcp->pmhdl.slot = -1;

    pxtcp->pcb = NULL;
    pxtcp->sock = INVALID_SOCKET;
    pxtcp->events = 0;
    pxtcp->sockerr = 0;
    pxtcp->netif = NULL;
    pxtcp->unsent = NULL;
    pxtcp->outbound_close = 0;
    pxtcp->outbound_close_done = 0;
    pxtcp->inbound_close = 0;
    pxtcp->inbound_close_done = 0;
    pxtcp->inbound_pull = 0;
    pxtcp->deferred_delete = 0;

    pxtcp->inbuf.bufsize = 64 * 1024;
    pxtcp->inbuf.buf = (char *)malloc(pxtcp->inbuf.bufsize);
    if (pxtcp->inbuf.buf == NULL) {
        free(pxtcp);
        return NULL;
    }
    pxtcp->inbuf.vacant = 0;
    pxtcp->inbuf.unacked = 0;
    pxtcp->inbuf.unsent = 0;

    pxtcp->rp = pollmgr_refptr_create(&pxtcp->pmhdl);
    if (pxtcp->rp == NULL) {
        free(pxtcp->inbuf.buf);
        free(pxtcp);
        return NULL;
    }

#define CALLBACK_MSG(MSG, FUNC)                         \
    do {                                                \
        pxtcp->MSG.type = TCPIP_MSG_CALLBACK_STATIC;    \
        pxtcp->MSG.sem = NULL;                          \
        pxtcp->MSG.msg.cb.function = FUNC;              \
        pxtcp->MSG.msg.cb.ctx = (void *)pxtcp;          \
    } while (0)

    CALLBACK_MSG(msg_delete, pxtcp_pcb_delete_pxtcp);
    CALLBACK_MSG(msg_reset, pxtcp_pcb_reset_pxtcp);
    CALLBACK_MSG(msg_accept, pxtcp_pcb_accept_confirm);
    CALLBACK_MSG(msg_outbound, pxtcp_pcb_write_outbound);
    CALLBACK_MSG(msg_inbound, pxtcp_pcb_write_inbound);
#if HAVE_TCP_POLLHUP
    CALLBACK_MSG(msg_inpull, pxtcp_pcb_pull_inbound);
#endif

#undef CALLBACK_MSG

    return pxtcp;
}


/**
 * Exported to fwtcp to create pxtcp for incoming port-forwarded
 * connections.  Completed with pcb in pxtcp_pcb_connect().
 */
struct pxtcp *
pxtcp_create_forwarded(SOCKET sock)
{
    struct pxtcp *pxtcp;

    pxtcp = pxtcp_allocate();
    if (pxtcp == NULL) {
        return NULL;
    }

    pxtcp->sock = sock;
    pxtcp->pmhdl.callback = pxtcp_pmgr_pump;
    pxtcp->events = 0;

    return pxtcp;
}


static void
pxtcp_pcb_associate(struct pxtcp *pxtcp, struct tcp_pcb *pcb)
{
    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pcb != NULL);

    pxtcp->pcb = pcb;

    tcp_arg(pcb, pxtcp);

    tcp_recv(pcb, pxtcp_pcb_recv);
    tcp_sent(pcb, pxtcp_pcb_sent);
    tcp_poll(pcb, NULL, 255);
    tcp_err(pcb, pxtcp_pcb_err);
}


static void
pxtcp_free(struct pxtcp *pxtcp)
{
    if (pxtcp->unsent != NULL) {
        pbuf_free(pxtcp->unsent);
    }
    if (pxtcp->inbuf.buf != NULL) {
        free(pxtcp->inbuf.buf);
    }
    free(pxtcp);
}


/**
 * Counterpart to pxtcp_create_forwarded() to destruct pxtcp that
 * fwtcp failed to register with poll manager to post to lwip thread
 * for doing connect.
 */
void
pxtcp_cancel_forwarded(struct pxtcp *pxtcp)
{
    LWIP_ASSERT1(pxtcp->pcb == NULL);
    pxtcp_pcb_reset_pxtcp(pxtcp);
}


static void
pxtcp_pcb_dissociate(struct pxtcp *pxtcp)
{
    if (pxtcp == NULL || pxtcp->pcb == NULL) {
        return;
    }

    DPRINTF(("%s: pxtcp %p <-> pcb %p\n",
             __func__, (void *)pxtcp, (void *)pxtcp->pcb));

    /*
     * We must have dissociated from a fully closed pcb immediately
     * since lwip recycles them and we don't wan't to mess with what
     * would be someone else's pcb that we happen to have a stale
     * pointer to.
     */
    LWIP_ASSERT1(pxtcp->pcb->callback_arg == pxtcp);

    tcp_recv(pxtcp->pcb, NULL);
    tcp_sent(pxtcp->pcb, NULL);
    tcp_poll(pxtcp->pcb, NULL, 255);
    tcp_err(pxtcp->pcb, NULL);
    tcp_arg(pxtcp->pcb, NULL);
    pxtcp->pcb = NULL;
}


/**
 * Lwip thread callback invoked via pxtcp::msg_delete
 *
 * Since we use static messages to communicate to the lwip thread, we
 * cannot delete pxtcp without making sure there are no unprocessed
 * messages in the lwip thread mailbox.
 *
 * The easiest way to ensure that is to send this "delete" message as
 * the last one and when it's processed we know there are no more and
 * it's safe to delete pxtcp.
 *
 * Poll manager handlers should use pxtcp_schedule_delete()
 * convenience function.
 */
static void
pxtcp_pcb_delete_pxtcp(void *ctx)
{
    struct pxtcp *pxtcp = (struct pxtcp *)ctx;

    DPRINTF(("%s: pxtcp %p, pcb %p, sock %d%s\n",
             __func__, (void *)pxtcp, (void *)pxtcp->pcb, pxtcp->sock,
             (pxtcp->deferred_delete && !pxtcp->inbound_pull
                  ? " (was deferred)" : "")));

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->pmhdl.slot < 0);
    LWIP_ASSERT1(pxtcp->outbound_close_done);
    LWIP_ASSERT1(pxtcp->inbound_close); /* not necessarily done */


    /*
     * pxtcp is no longer registered with poll manager, so it's safe
     * to close the socket.
     */
    if (pxtcp->sock != INVALID_SOCKET) {
        closesocket(pxtcp->sock);
        pxtcp->sock = INVALID_SOCKET;
    }

    /*
     * We might have already dissociated from a fully closed pcb, or
     * guest might have sent us a reset while msg_delete was in
     * transit.  If there's no pcb, we are done.
     */
    if (pxtcp->pcb == NULL) {
        pollmgr_refptr_unref(pxtcp->rp);
        pxtcp_free(pxtcp);
        return;
    }

    /*
     * Have we completely forwarded all inbound traffic to the guest?
     *
     * We may still be waiting for ACKs.  We may have failed to send
     * some of the data (tcp_write() failed with ERR_MEM).  We may
     * have failed to send the FIN (tcp_shutdown() failed with
     * ERR_MEM).
     */
    if (pxtcp_pcb_forward_inbound_done(pxtcp)) {
        pxtcp_pcb_dissociate(pxtcp);
        pollmgr_refptr_unref(pxtcp->rp);
        pxtcp_free(pxtcp);
    }
    else {
        DPRINTF2(("delete: pxtcp %p; pcb %p:"
                  " unacked %d, unsent %d, vacant %d, %s - DEFER!\n",
                  (void *)pxtcp, (void *)pxtcp->pcb,
                  (int)pxtcp->inbuf.unacked,
                  (int)pxtcp->inbuf.unsent,
                  (int)pxtcp->inbuf.vacant,
                  pxtcp->inbound_close_done ? "FIN sent" : "FIN is NOT sent"));

        LWIP_ASSERT1(!pxtcp->deferred_delete);
        pxtcp->deferred_delete = 1;
    }
}


/**
 * If we couldn't delete pxtcp right away in the msg_delete callback
 * from the poll manager thread, we repeat the check at the end of
 * relevant pcb callbacks.
 */
DECLINLINE(void)
pxtcp_pcb_maybe_deferred_delete(struct pxtcp *pxtcp)
{
    if (pxtcp->deferred_delete && pxtcp_pcb_forward_inbound_done(pxtcp)) {
        pxtcp_pcb_delete_pxtcp(pxtcp);
    }
}


/**
 * Poll manager callbacks should use this convenience wrapper to
 * schedule pxtcp deletion on the lwip thread and to deregister from
 * the poll manager.
 */
static int
pxtcp_schedule_delete(struct pxtcp *pxtcp)
{
    /*
     * If pollmgr_refptr_get() is called by any channel before
     * scheduled deletion happens, let them know we are gone.
     */
    pxtcp->pmhdl.slot = -1;

    /*
     * Schedule deletion.  Since poll manager thread may be pre-empted
     * right after we send the message, the deletion may actually
     * happen on the lwip thread before we return from this function,
     * so it's not safe to refer to pxtcp after this call.
     */
    proxy_lwip_post(&pxtcp->msg_delete);

    /* tell poll manager to deregister us */
    return -1;
}


/**
 * Lwip thread callback invoked via pxtcp::msg_reset
 *
 * Like pxtcp_pcb_delete(), but sends RST to the guest before
 * deleting this pxtcp.
 */
static void
pxtcp_pcb_reset_pxtcp(void *ctx)
{
    struct pxtcp *pxtcp = (struct pxtcp *)ctx;
    LWIP_ASSERT1(pxtcp != NULL);

    DPRINTF0(("%s: pxtcp %p, pcb %p, sock %d\n",
              __func__, (void *)pxtcp, (void *)pxtcp->pcb, pxtcp->sock));

    if (pxtcp->sock != INVALID_SOCKET) {
        proxy_reset_socket(pxtcp->sock);
        pxtcp->sock = INVALID_SOCKET;
    }

    if (pxtcp->pcb != NULL) {
        struct tcp_pcb *pcb = pxtcp->pcb;
        pxtcp_pcb_dissociate(pxtcp);
        tcp_abort(pcb);
    }

    pollmgr_refptr_unref(pxtcp->rp);
    pxtcp_free(pxtcp);
}



/**
 * Poll manager callbacks should use this convenience wrapper to
 * schedule pxtcp reset and deletion on the lwip thread and to
 * deregister from the poll manager.
 *
 * See pxtcp_schedule_delete() for additional comments.
 */
static int
pxtcp_schedule_reset(struct pxtcp *pxtcp)
{
    pxtcp->pmhdl.slot = -1;
    proxy_lwip_post(&pxtcp->msg_reset);
    return -1;
}


/**
 * Reject proxy connection attempt.  Depending on the cause (sockerr)
 * we may just drop the pcb silently, generate an ICMP datagram or
 * send TCP reset.
 */
static void
pxtcp_pcb_reject(struct tcp_pcb *pcb, int sockerr,
                 struct netif *netif,  struct pbuf *p)
{
    int reset = 0;

    if (sockerr == ECONNREFUSED) {
        reset = 1;
    }
    else if (p != NULL) {
        struct netif *oif;

        LWIP_ASSERT1(netif != NULL);

        oif = ip_current_netif();
        ip_current_netif() = netif;

        if (PCB_ISIPV6(pcb)) {
            if (sockerr == EHOSTDOWN) {
                icmp6_dest_unreach(p, ICMP6_DUR_ADDRESS); /* XXX: ??? */
            }
            else if (sockerr == EHOSTUNREACH
                     || sockerr == ENETDOWN
                     || sockerr == ENETUNREACH)
            {
                icmp6_dest_unreach(p, ICMP6_DUR_NO_ROUTE);
            }
        }
        else {
            if (sockerr == EHOSTDOWN
                || sockerr == EHOSTUNREACH
                || sockerr == ENETDOWN
                || sockerr == ENETUNREACH)
            {
                icmp_dest_unreach(p, ICMP_DUR_HOST);
            }
        }

        ip_current_netif() = oif;
    }

    tcp_abandon(pcb, reset);
}


/**
 * Called from poll manager thread via pxtcp::msg_accept when proxy
 * failed to connect to the destination.  Also called when we failed
 * to register pxtcp with poll manager.
 *
 * This is like pxtcp_pcb_reset_pxtcp() but is more discriminate in
 * how this unestablished connection is terminated.
 */
static void
pxtcp_pcb_accept_refuse(void *ctx)
{
    struct pxtcp *pxtcp = (struct pxtcp *)ctx;

    DPRINTF0(("%s: pxtcp %p, pcb %p, sock %d: %R[sockerr]\n",
              __func__, (void *)pxtcp, (void *)pxtcp->pcb,
              pxtcp->sock, pxtcp->sockerr));

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->sock == INVALID_SOCKET);

    if (pxtcp->pcb != NULL) {
        struct tcp_pcb *pcb = pxtcp->pcb;
        pxtcp_pcb_dissociate(pxtcp);
        pxtcp_pcb_reject(pcb,  pxtcp->sockerr, pxtcp->netif, pxtcp->unsent);
    }

    pollmgr_refptr_unref(pxtcp->rp);
    pxtcp_free(pxtcp);
}


/**
 * Convenience wrapper for poll manager connect callback to reject
 * connection attempt.
 *
 * Like pxtcp_schedule_reset(), but the callback is more discriminate
 * in how this unestablished connection is terminated.
 */
static int
pxtcp_schedule_reject(struct pxtcp *pxtcp)
{
    pxtcp->msg_accept.msg.cb.function = pxtcp_pcb_accept_refuse;
    pxtcp->pmhdl.slot = -1;
    proxy_lwip_post(&pxtcp->msg_accept);
    return -1;
}


/**
 * Global tcp_proxy_accept() callback for proxied outgoing TCP
 * connections from guest(s).
 */
static err_t
pxtcp_pcb_heard(void *arg, struct tcp_pcb *newpcb, struct pbuf *syn)
{
    LWIP_UNUSED_ARG(arg);

    return pxtcp_pcb_accept_outbound(newpcb, syn,
               PCB_ISIPV6(newpcb), &newpcb->local_ip, newpcb->local_port);
}


err_t
pxtcp_pcb_accept_outbound(struct tcp_pcb *newpcb, struct pbuf *p,
                          int is_ipv6, ipX_addr_t *dst_addr, u16_t dst_port)
{
    struct pxtcp *pxtcp;
    ipX_addr_t mapped_dst_addr;
    int sdom;
    SOCKET sock;
    ssize_t nsent;
    int sockerr = 0;

    /*
     * TCP first calls accept callback when it receives the first SYN
     * and "tentatively accepts" new proxied connection attempt.  When
     * proxy "confirms" the SYN and sends SYN|ACK and the guest
     * replies with ACK the accept callback is called again, this time
     * with the established connection.
     */
    LWIP_ASSERT1(newpcb->state == SYN_RCVD_0);
    tcp_accept(newpcb, pxtcp_pcb_accept);
    tcp_arg(newpcb, NULL);

    tcp_setprio(newpcb, TCP_PRIO_MAX);

    pxremap_outbound_ipX(is_ipv6, &mapped_dst_addr, dst_addr);

    sdom = is_ipv6 ? PF_INET6 : PF_INET;
    sock = proxy_connected_socket(sdom, SOCK_STREAM,
                                  &mapped_dst_addr, dst_port);
    if (sock == INVALID_SOCKET) {
        sockerr = SOCKERRNO();
        goto abort;
    }

    pxtcp = pxtcp_allocate();
    if (pxtcp == NULL) {
        proxy_reset_socket(sock);
        goto abort;
    }

    /* save initial datagram in case we need to reply with ICMP */
    if (p != NULL) {
        pbuf_ref(p);
        pxtcp->unsent = p;
        pxtcp->netif = ip_current_netif();
    }

    pxtcp_pcb_associate(pxtcp, newpcb);
    pxtcp->sock = sock;

    pxtcp->pmhdl.callback = pxtcp_pmgr_connect;
    pxtcp->events = POLLOUT;

    nsent = pxtcp_chan_send(POLLMGR_CHAN_PXTCP_ADD, pxtcp);
    if (nsent < 0) {
        pxtcp->sock = INVALID_SOCKET;
        proxy_reset_socket(sock);
        pxtcp_pcb_accept_refuse(pxtcp);
        return ERR_ABRT;
    }

    return ERR_OK;

  abort:
    DPRINTF0(("%s: pcb %p, sock %d: %R[sockerr]\n",
              __func__, (void *)newpcb, sock, sockerr));
    pxtcp_pcb_reject(newpcb, sockerr, ip_current_netif(), p);
    return ERR_ABRT;
}


/**
 * tcp_proxy_accept() callback for accepted proxied outgoing TCP
 * connections from guest(s).  This is "real" accept with three-way
 * handshake completed.
 */
static err_t
pxtcp_pcb_accept(void *arg, struct tcp_pcb *pcb, err_t error)
{
    struct pxtcp *pxtcp = (struct pxtcp *)arg;

    LWIP_UNUSED_ARG(pcb);       /* used only in asserts */
    LWIP_UNUSED_ARG(error);     /* always ERR_OK */

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->pcb = pcb);
    LWIP_ASSERT1(pcb->callback_arg == pxtcp);

    /* send any inbound data that are already queued */
    pxtcp_pcb_forward_inbound(pxtcp);
    return ERR_OK;
}


/**
 * Initial poll manager callback for proxied outgoing TCP connections.
 * pxtcp_pcb_accept() sets pxtcp::pmhdl::callback to this.
 *
 * Waits for connect(2) to the destination to complete.  On success
 * replaces itself with pxtcp_pmgr_pump() callback common to all
 * established TCP connections.
 */
static int
pxtcp_pmgr_connect(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;
    RT_NOREF(fd);

    pxtcp = (struct pxtcp *)handler->data;
    LWIP_ASSERT1(handler == &pxtcp->pmhdl);
    LWIP_ASSERT1(fd == pxtcp->sock);
    LWIP_ASSERT1(pxtcp->sockerr == 0);

    if (revents & POLLNVAL) {
        pxtcp->sock = INVALID_SOCKET;
        pxtcp->sockerr = ETIMEDOUT;
        return pxtcp_schedule_reject(pxtcp);
    }

    /*
     * Solaris and NetBSD don't report either POLLERR or POLLHUP when
     * connect(2) fails, just POLLOUT.  In that case we always need to
     * check SO_ERROR.
     */
#if defined(RT_OS_SOLARIS) || defined(RT_OS_NETBSD)
# define CONNECT_CHECK_ERROR POLLOUT
#else
# define CONNECT_CHECK_ERROR (POLLERR | POLLHUP)
#endif

    /*
     * Check the cause of the failure so that pxtcp_pcb_reject() may
     * behave accordingly.
     */
    if (revents & CONNECT_CHECK_ERROR) {
        socklen_t optlen = (socklen_t)sizeof(pxtcp->sockerr);
        int status;
        SOCKET s;

        status = getsockopt(pxtcp->sock, SOL_SOCKET, SO_ERROR,
                            (char *)&pxtcp->sockerr, &optlen);
        if (RT_UNLIKELY(status == SOCKET_ERROR)) { /* should not happen */
            DPRINTF(("%s: sock %d: SO_ERROR failed: %R[sockerr]\n",
                     __func__, fd, SOCKERRNO()));
            pxtcp->sockerr = ETIMEDOUT;
        }
        else {
            /* don't spam this log on successful connect(2) */
            if ((revents & (POLLERR | POLLHUP)) /* we were told it's failed */
                || pxtcp->sockerr != 0)         /* we determined it's failed */
            {
                DPRINTF(("%s: sock %d: connect: %R[sockerr]\n",
                         __func__, fd, pxtcp->sockerr));
            }

            if ((revents & (POLLERR | POLLHUP))
                && RT_UNLIKELY(pxtcp->sockerr == 0))
            {
                /* if we're told it's failed, make sure it's marked as such */
                pxtcp->sockerr = ETIMEDOUT;
            }
        }

        if (pxtcp->sockerr != 0) {
            s = pxtcp->sock;
            pxtcp->sock = INVALID_SOCKET;
            closesocket(s);
            return pxtcp_schedule_reject(pxtcp);
        }
    }

    if (revents & POLLOUT) { /* connect is successful */
        /* confirm accept to the guest */
        proxy_lwip_post(&pxtcp->msg_accept);

        /*
         * Switch to common callback used for all established proxied
         * connections.
         */
        pxtcp->pmhdl.callback = pxtcp_pmgr_pump;

        /*
         * Initially we poll for incoming traffic only.  Outgoing
         * traffic is fast-forwarded by pxtcp_pcb_recv(); if it fails
         * it will ask us to poll for POLLOUT too.
         */
        pxtcp->events = POLLIN;
        return pxtcp->events;
    }

    /* should never get here */
    DPRINTF0(("%s: pxtcp %p, sock %d: unexpected revents 0x%x\n",
              __func__, (void *)pxtcp, fd, revents));
    return pxtcp_schedule_reset(pxtcp);
}


/**
 * Called from poll manager thread via pxtcp::msg_accept when proxy
 * connected to the destination.  Finalize accept by sending SYN|ACK
 * to the guest.
 */
static void
pxtcp_pcb_accept_confirm(void *ctx)
{
    struct pxtcp *pxtcp = (struct pxtcp *)ctx;
    err_t error;

    LWIP_ASSERT1(pxtcp != NULL);
    if (pxtcp->pcb == NULL) {
        return;
    }

    /* we are not going to reply with ICMP, so we can drop initial pbuf */
    if (pxtcp->unsent != NULL) {
        pbuf_free(pxtcp->unsent);
        pxtcp->unsent = NULL;
    }

    error = tcp_proxy_accept_confirm(pxtcp->pcb);

    /*
     * If lwIP failed to enqueue SYN|ACK because it's out of pbufs it
     * abandons the pcb.  Retrying that is not very easy, since it
     * would require keeping "fractional state".  From guest's point
     * of view there is no reply to its SYN so it will either resend
     * the SYN (effetively triggering full connection retry for us),
     * or it will eventually time out.
     */
    if (error == ERR_ABRT) {
        pxtcp->pcb = NULL;      /* pcb is gone */
        pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_RESET, pxtcp);
    }

    /*
     * else if (error != ERR_OK): even if tcp_output() failed with
     * ERR_MEM - don't give up, that SYN|ACK is enqueued and will be
     * retransmitted eventually.
     */
}


/**
 * Entry point for port-forwarding.
 *
 * fwtcp accepts new incoming connection, creates pxtcp for the socket
 * (with no pcb yet) and adds it to the poll manager (polling for
 * errors only).  Then it calls this function to construct the pcb and
 * perform connection to the guest.
 */
void
pxtcp_pcb_connect(struct pxtcp *pxtcp, const struct fwspec *fwspec)
{
    struct sockaddr_storage ss;
    socklen_t sslen;
    struct tcp_pcb *pcb;
    ipX_addr_t src_addr, dst_addr;
    u16_t src_port, dst_port;
    int status;
    err_t error;

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->pcb == NULL);
    LWIP_ASSERT1(fwspec->stype == SOCK_STREAM);

    pcb = tcp_new();
    if (pcb == NULL) {
        goto reset;
    }

    tcp_setprio(pcb, TCP_PRIO_MAX);
    pxtcp_pcb_associate(pxtcp, pcb);

    sslen = sizeof(ss);
    status = getpeername(pxtcp->sock, (struct sockaddr *)&ss, &sslen);
    if (status  == SOCKET_ERROR) {
        goto reset;
    }

    /* nit: compares PF and AF, but they are the same everywhere */
    LWIP_ASSERT1(ss.ss_family == fwspec->sdom);

    status = fwany_ipX_addr_set_src(&src_addr, (const struct sockaddr *)&ss);
    if (status == PXREMAP_FAILED) {
        goto reset;
    }

    if (ss.ss_family == PF_INET) {
        const struct sockaddr_in *peer4 = (const struct sockaddr_in *)&ss;

        src_port = peer4->sin_port;

        memcpy(&dst_addr.ip4, &fwspec->dst.sin.sin_addr, sizeof(ip_addr_t));
        dst_port = fwspec->dst.sin.sin_port;
    }
    else { /* PF_INET6 */
        const struct sockaddr_in6 *peer6 = (const struct sockaddr_in6 *)&ss;
        ip_set_v6(pcb, 1);

        src_port = peer6->sin6_port;

        memcpy(&dst_addr.ip6, &fwspec->dst.sin6.sin6_addr, sizeof(ip6_addr_t));
        dst_port = fwspec->dst.sin6.sin6_port;
    }

    /* lwip port arguments are in host order */
    src_port = ntohs(src_port);
    dst_port = ntohs(dst_port);

    error = tcp_proxy_bind(pcb, ipX_2_ip(&src_addr), src_port);
    if (error != ERR_OK) {
        goto reset;
    }

    error = tcp_connect(pcb, ipX_2_ip(&dst_addr), dst_port,
                        /* callback: */ pxtcp_pcb_connected);
    if (error != ERR_OK) {
        goto reset;
    }

    return;

  reset:
    pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_RESET, pxtcp);
}


/**
 * Port-forwarded connection to guest is successful, pump data.
 */
static err_t
pxtcp_pcb_connected(void *arg, struct tcp_pcb *pcb, err_t error)
{
    struct pxtcp *pxtcp = (struct pxtcp *)arg;

    LWIP_ASSERT1(error == ERR_OK); /* always called with ERR_OK */
    LWIP_UNUSED_ARG(error);

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->pcb == pcb);
    LWIP_ASSERT1(pcb->callback_arg == pxtcp);
    LWIP_UNUSED_ARG(pcb);

    DPRINTF0(("%s: new pxtcp %p; pcb %p; sock %d\n",
              __func__, (void *)pxtcp, (void *)pxtcp->pcb, pxtcp->sock));

    /* ACK on connection is like ACK on data in pxtcp_pcb_sent() */
    pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_POLLIN, pxtcp);

    return ERR_OK;
}


/**
 * tcp_recv() callback.
 */
static err_t
pxtcp_pcb_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error)
{
    struct pxtcp *pxtcp = (struct pxtcp *)arg;

    LWIP_ASSERT1(error == ERR_OK); /* always called with ERR_OK */
    LWIP_UNUSED_ARG(error);

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->pcb == pcb);
    LWIP_ASSERT1(pcb->callback_arg == pxtcp);
    LWIP_UNUSED_ARG(pcb);


    /*
     * Have we done sending previous batch?
     */
    if (pxtcp->unsent != NULL) {
        if (p != NULL) {
            /*
             * Return an error to tell TCP to hold onto that pbuf.
             * It will be presented to us later from tcp_fasttmr().
             */
            return ERR_WOULDBLOCK;
        }
        else {
            /*
             * Unlike data, p == NULL indicating orderly shutdown is
             * NOT presented to us again
             */
            pxtcp->outbound_close = 1;
            return ERR_OK;
        }
    }


    /*
     * Guest closed?
     */
    if (p == NULL) {
        pxtcp->outbound_close = 1;
        pxtcp_pcb_forward_outbound_close(pxtcp);
        return ERR_OK;
    }


    /*
     * Got data, send what we can without blocking.
     */
    return pxtcp_pcb_forward_outbound(pxtcp, p);
}


/**
 * Guest half-closed its TX side of the connection.
 *
 * Called either immediately from pxtcp_pcb_recv() when it gets NULL,
 * or from pxtcp_pcb_forward_outbound() when it finishes forwarding
 * previously unsent data and sees pxtcp::outbound_close flag saved by
 * pxtcp_pcb_recv().
 */
static void
pxtcp_pcb_forward_outbound_close(struct pxtcp *pxtcp)
{
    struct tcp_pcb *pcb;

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->outbound_close);
    LWIP_ASSERT1(!pxtcp->outbound_close_done);

    pcb = pxtcp->pcb;
    LWIP_ASSERT1(pcb != NULL);

    DPRINTF(("outbound_close: pxtcp %p; pcb %p %s\n",
             (void *)pxtcp, (void *)pcb, tcp_debug_state_str(pcb->state)));


    /* set the flag first, since shutdown() may trigger POLLHUP */
    pxtcp->outbound_close_done = 1;
    shutdown(pxtcp->sock, SHUT_WR); /* half-close the socket */

#if !(HAVE_TCP_POLLHUP & POLLOUT)
    /*
     * We need to nudge poll manager manually, since OS will not
     * report POLLHUP.
     */
    if (pxtcp->inbound_close) {
        pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_DEL, pxtcp);
    }
#endif


    /* no more outbound data coming to us */
    tcp_recv(pcb, NULL);

    /*
     * If we have already done inbound close previously (active close
     * on the pcb), then we must not hold onto a pcb in TIME_WAIT
     * state since those will be recycled by lwip when it runs out of
     * free pcbs in the pool.
     *
     * The test is true also for a pcb in CLOSING state that waits
     * just for the ACK of its FIN (to transition to TIME_WAIT).
     */
    if (pxtcp_pcb_forward_inbound_done(pxtcp)) {
        pxtcp_pcb_dissociate(pxtcp);
    }
}


/**
 * Forward outbound data from pcb to socket.
 *
 * Called by pxtcp_pcb_recv() to forward new data and by callout
 * triggered by POLLOUT on the socket to send previously unsent data.
 *
 * (Re)scehdules one-time callout if not all data are sent.
 */
static err_t
pxtcp_pcb_forward_outbound(struct pxtcp *pxtcp, struct pbuf *p)
{
    struct pbuf *qs, *q;
    size_t qoff;
    size_t forwarded;
    int sockerr;

    LWIP_ASSERT1(pxtcp->unsent == NULL || pxtcp->unsent == p);

    forwarded = 0;
    sockerr = 0;

    q = NULL;
    qoff = 0;

    qs = p;
    while (qs != NULL) {
        IOVEC iov[8];
        const size_t iovsize = sizeof(iov)/sizeof(iov[0]);
        size_t fwd1;
        ssize_t nsent;
        size_t i;

        fwd1 = 0;
        for (i = 0, q = qs; i < iovsize && q != NULL; ++i, q = q->next) {
            LWIP_ASSERT1(q->len > 0);
            IOVEC_SET_BASE(iov[i], q->payload);
            IOVEC_SET_LEN(iov[i], q->len);
            fwd1 += q->len;
        }

        /*
         * TODO: This is where application-level proxy can hook into
         * to process outbound traffic.
         */
        nsent = pxtcp_sock_send(pxtcp, iov, i);

        if (nsent == (ssize_t)fwd1) {
            /* successfully sent this chain fragment completely */
            forwarded += nsent;
            qs = q;
        }
        else if (nsent >= 0) {
            /* successfully sent only some data */
            forwarded += nsent;

            /* find the first pbuf that was not completely forwarded */
            qoff = nsent;
            for (i = 0, q = qs; i < iovsize && q != NULL; ++i, q = q->next) {
                if (qoff < q->len) {
                    break;
                }
                qoff -= q->len;
            }
            LWIP_ASSERT1(q != NULL);
            LWIP_ASSERT1(qoff < q->len);
            break;
        }
        else {
            sockerr = -nsent;

            /*
             * Some errors are really not errors - if we get them,
             * it's not different from getting nsent == 0, so filter
             * them out here.
             */
            if (proxy_error_is_transient(sockerr)) {
                sockerr = 0;
            }
            q = qs;
            qoff = 0;
            break;
        }
    }

    if (forwarded > 0) {
        DPRINTF2(("forward_outbound: pxtcp %p, pcb %p: sent %d bytes\n",
                  (void *)pxtcp, (void *)pxtcp->pcb, (int)forwarded));
        tcp_recved(pxtcp->pcb, (u16_t)forwarded);
    }

    if (q == NULL) { /* everything is forwarded? */
        LWIP_ASSERT1(sockerr == 0);
        LWIP_ASSERT1(forwarded == p->tot_len);

        pxtcp->unsent = NULL;
        pbuf_free(p);
        if (pxtcp->outbound_close) {
            pxtcp_pcb_forward_outbound_close(pxtcp);
        }
    }
    else {
        if (q != p) {
            /* free forwarded pbufs at the beginning of the chain */
            pbuf_ref(q);
            pbuf_free(p);
        }
        if (qoff > 0) {
            /* advance payload pointer past the forwarded part */
            pbuf_header(q, -(s16_t)qoff);
        }
        pxtcp->unsent = q;
        DPRINTF2(("forward_outbound: pxtcp %p, pcb %p: kept %d bytes\n",
                  (void *)pxtcp, (void *)pxtcp->pcb, (int)q->tot_len));

        /*
         * Have sendmsg() failed?
         *
         * Connection reset will be detected by poll and
         * pxtcp_schedule_reset() will be called.
         *
         * Otherwise something *really* unexpected must have happened,
         * so we'd better abort.
         */
        if (sockerr != 0 && sockerr != ECONNRESET) {
            struct tcp_pcb *pcb = pxtcp->pcb;
            DPRINTF2(("forward_outbound: pxtcp %p, pcb %p: %R[sockerr]\n",
                      (void *)pxtcp, (void *)pcb, sockerr));

            pxtcp_pcb_dissociate(pxtcp);

            tcp_abort(pcb);

            /* call error callback manually since we've already dissociated */
            pxtcp_pcb_err((void *)pxtcp, ERR_ABRT);
            return ERR_ABRT;
        }

        /* schedule one-shot POLLOUT on the socket */
        pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_POLLOUT, pxtcp);
    }
    return ERR_OK;
}


#if !defined(RT_OS_WINDOWS)
static ssize_t
pxtcp_sock_send(struct pxtcp *pxtcp, IOVEC *iov, size_t iovlen)
{
    struct msghdr mh;
    ssize_t nsent;

#ifdef MSG_NOSIGNAL
    const int send_flags = MSG_NOSIGNAL;
#else
    const int send_flags = 0;
#endif

    memset(&mh, 0, sizeof(mh));

    mh.msg_iov = iov;
    mh.msg_iovlen = iovlen;

    nsent = sendmsg(pxtcp->sock, &mh, send_flags);
    if (nsent < 0) {
        nsent = -SOCKERRNO();
    }

    return nsent;
}
#else /* RT_OS_WINDOWS */
static ssize_t
pxtcp_sock_send(struct pxtcp *pxtcp, IOVEC *iov, size_t iovlen)
{
    DWORD nsent;
    int status;

    status = WSASend(pxtcp->sock, iov, (DWORD)iovlen, &nsent,
                     0, NULL, NULL);
    if (status == SOCKET_ERROR) {
        return -SOCKERRNO();
    }

    return nsent;
}
#endif /* RT_OS_WINDOWS */


/**
 * Callback from poll manager (on POLLOUT) to send data from
 * pxtcp::unsent pbuf to socket.
 */
static void
pxtcp_pcb_write_outbound(void *ctx)
{
    struct pxtcp *pxtcp = (struct pxtcp *)ctx;
    LWIP_ASSERT1(pxtcp != NULL);

    if (pxtcp->pcb == NULL) {
        return;
    }

    pxtcp_pcb_forward_outbound(pxtcp, pxtcp->unsent);
}


/**
 * Common poll manager callback used by both outgoing and incoming
 * (port-forwarded) connections that has connected socket.
 */
static int
pxtcp_pmgr_pump(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    struct pxtcp *pxtcp;
    int status;
    int sockerr;
    RT_NOREF(fd);

    pxtcp = (struct pxtcp *)handler->data;
    LWIP_ASSERT1(handler == &pxtcp->pmhdl);
    LWIP_ASSERT1(fd == pxtcp->sock);

    if (revents & POLLNVAL) {
        pxtcp->sock = INVALID_SOCKET;
        return pxtcp_schedule_reset(pxtcp);
    }

    if (revents & POLLERR) {
        socklen_t optlen = (socklen_t)sizeof(sockerr);

        status = getsockopt(pxtcp->sock, SOL_SOCKET, SO_ERROR,
                            (char *)&sockerr, &optlen);
        if (status == SOCKET_ERROR) { /* should not happen */
            DPRINTF(("sock %d: POLLERR: SO_ERROR failed: %R[sockerr]\n",
                     fd, SOCKERRNO()));
        }
        else {
            DPRINTF0(("sock %d: POLLERR: %R[sockerr]\n", fd, sockerr));
        }
        return pxtcp_schedule_reset(pxtcp);
    }

    if (revents & POLLOUT) {
        pxtcp->events &= ~POLLOUT;
        proxy_lwip_post(&pxtcp->msg_outbound);
    }

    if (revents & POLLIN) {
        ssize_t nread;
        int stop_pollin;

        nread = pxtcp_sock_read(pxtcp, &stop_pollin);
        if (nread < 0) {
            DPRINTF0(("sock %d: POLLIN: %R[sockerr]\n", fd, -(int)nread));
            return pxtcp_schedule_reset(pxtcp);
        }

        if (stop_pollin) {
            pxtcp->events &= ~POLLIN;
        }

        if (nread > 0) {
            proxy_lwip_post(&pxtcp->msg_inbound);
#if !HAVE_TCP_POLLHUP
            /*
             * If host does not report POLLHUP for closed sockets
             * (e.g. NetBSD) we should check for full close manually.
             */
            if (pxtcp->inbound_close && pxtcp->outbound_close_done) {
                LWIP_ASSERT1((revents & POLLHUP) == 0);
                return pxtcp_schedule_delete(pxtcp);
            }
#endif
        }
    }

#if !HAVE_TCP_POLLHUP
    LWIP_ASSERT1((revents & POLLHUP) == 0);
#else
    if (revents & POLLHUP) {
        DPRINTF(("sock %d: HUP\n", fd));

#if HAVE_TCP_POLLHUP == POLLIN
        /*
         * XXX: OSX reports POLLHUP once more when inbound is already
         * half-closed (which has already been reported as a "normal"
         * POLLHUP, handled below), the socket is polled for POLLOUT
         * (guest sends a lot of data that we can't push out fast
         * enough), and remote sends a reset - e.g. an http client
         * that half-closes after request and then aborts the transfer.
         *
         * It really should have been reported as POLLERR, but it
         * seems OSX never reports POLLERR for sockets.
         */
#if defined(RT_OS_DARWIN)
        {
            socklen_t optlen = (socklen_t)sizeof(sockerr);

            status = getsockopt(pxtcp->sock, SOL_SOCKET, SO_ERROR,
                                (char *)&sockerr, &optlen);
            if (status == SOCKET_ERROR) { /* should not happen */
                DPRINTF(("sock %d: POLLHUP: SO_ERROR failed: %R[sockerr]\n",
                         fd, SOCKERRNO()));
                sockerr = ECONNRESET;
            }
            else if (sockerr != 0) {
                DPRINTF0(("sock %d: POLLHUP: %R[sockerr]\n", fd, sockerr));
            }

            if (sockerr != 0) { /* XXX: should have been POLLERR */
                return pxtcp_schedule_reset(pxtcp);
            }
        }
#endif  /* RT_OS_DARWIN */

        /*
         * Remote closed inbound.
         */
        if (!pxtcp->outbound_close_done) {
            /*
             * We might still need to poll for POLLOUT, but we can not
             * poll for POLLIN anymore (even if not all data are read)
             * because we will be spammed by POLLHUP.
             */
            pxtcp->events &= ~POLLIN;
            if (!pxtcp->inbound_close) {
                /* the rest of the input has to be pulled */
                proxy_lwip_post(&pxtcp->msg_inpull);
            }
        }
        else
#endif
        /*
         * Both directions are closed.
         */
        {
            LWIP_ASSERT1(pxtcp->outbound_close_done);

            if (pxtcp->inbound_close) {
                /* there's no unread data, we are done */
                return pxtcp_schedule_delete(pxtcp);
            }
            else {
                /* pull the rest of the input first (deferred_delete) */
                pxtcp->pmhdl.slot = -1;
                proxy_lwip_post(&pxtcp->msg_inpull);
                return -1;
            }
            /* NOTREACHED */
        }

    }
#endif  /* HAVE_TCP_POLLHUP */

    return pxtcp->events;
}


/**
 * Read data from socket to ringbuf.  This may be used both on lwip
 * and poll manager threads.
 *
 * Flag pointed to by pstop is set when further reading is impossible,
 * either temporary when buffer is full, or permanently when EOF is
 * received.
 *
 * Returns number of bytes read.  NB: EOF is reported as 1!
 *
 * Returns zero if nothing was read, either because buffer is full, or
 * if no data is available (EWOULDBLOCK, EINTR &c).
 *
 * Returns -errno on real socket errors.
 */
static ssize_t
pxtcp_sock_read(struct pxtcp *pxtcp, int *pstop)
{
    IOVEC iov[2];
    size_t iovlen;
    ssize_t nread;

    const size_t sz = pxtcp->inbuf.bufsize;
    size_t beg, lim, wrnew;

    *pstop = 0;

    beg = pxtcp->inbuf.vacant;
    IOVEC_SET_BASE(iov[0], &pxtcp->inbuf.buf[beg]);

    /* lim is the index we can NOT write to */
    lim = pxtcp->inbuf.unacked;
    if (lim == 0) {
        lim = sz - 1;           /* empty slot at the end */
    }
    else if (lim == 1 && beg != 0) {
        lim = sz;               /* empty slot at the beginning */
    }
    else {
        --lim;
    }

    if (beg == lim) {
        /*
         * Buffer is full, stop polling for POLLIN.
         *
         * pxtcp_pcb_sent() will re-enable POLLIN when guest ACKs
         * data, freeing space in the ring buffer.
         */
        *pstop = 1;
        return 0;
    }

    if (beg < lim) {
        /* free space in one chunk */
        iovlen = 1;
        IOVEC_SET_LEN(iov[0], lim - beg);
    }
    else {
        /* free space in two chunks */
        iovlen = 2;
        IOVEC_SET_LEN(iov[0], sz - beg);
        IOVEC_SET_BASE(iov[1], &pxtcp->inbuf.buf[0]);
        IOVEC_SET_LEN(iov[1], lim);
    }

    /*
     * TODO: This is where application-level proxy can hook into to
     * process inbound traffic.
     */
    nread = pxtcp_sock_recv(pxtcp, iov, iovlen);

    if (nread > 0) {
        wrnew = beg + nread;
        if (wrnew >= sz) {
            wrnew -= sz;
        }
        pxtcp->inbuf.vacant = wrnew;
        DPRINTF2(("pxtcp %p: sock %d read %d bytes\n",
                  (void *)pxtcp, pxtcp->sock, (int)nread));
        return nread;
    }
    else if (nread == 0) {
        *pstop = 1;
        pxtcp->inbound_close = 1;
        DPRINTF2(("pxtcp %p: sock %d read EOF\n",
                 (void *)pxtcp, pxtcp->sock));
        return 1;
    }
    else {
        int sockerr = -nread;

        if (proxy_error_is_transient(sockerr)) {
            /* haven't read anything, just return */
            DPRINTF2(("pxtcp %p: sock %d read cancelled\n",
                      (void *)pxtcp, pxtcp->sock));
            return 0;
        }
        else {
            /* socket error! */
            DPRINTF0(("pxtcp %p: sock %d read: %R[sockerr]\n",
                      (void *)pxtcp, pxtcp->sock, sockerr));
            return -sockerr;
        }
    }
}


#if !defined(RT_OS_WINDOWS)
static ssize_t
pxtcp_sock_recv(struct pxtcp *pxtcp, IOVEC *iov, size_t iovlen)
{
    struct msghdr mh;
    ssize_t nread;

    memset(&mh, 0, sizeof(mh));

    mh.msg_iov = iov;
    mh.msg_iovlen = iovlen;

    nread = recvmsg(pxtcp->sock, &mh, 0);
    if (nread < 0) {
        nread = -SOCKERRNO();
    }

    return nread;
}
#else /* RT_OS_WINDOWS */
static ssize_t
pxtcp_sock_recv(struct pxtcp *pxtcp, IOVEC *iov, size_t iovlen)
{
    DWORD flags;
    DWORD nread;
    int status;

    flags = 0;
    status = WSARecv(pxtcp->sock, iov, (DWORD)iovlen, &nread,
                     &flags, NULL, NULL);
    if (status == SOCKET_ERROR) {
        return -SOCKERRNO();
    }

    return (ssize_t)nread;
}
#endif /* RT_OS_WINDOWS */


/**
 * Callback from poll manager (pxtcp::msg_inbound) to trigger output
 * from ringbuf to guest.
 */
static void
pxtcp_pcb_write_inbound(void *ctx)
{
    struct pxtcp *pxtcp = (struct pxtcp *)ctx;
    LWIP_ASSERT1(pxtcp != NULL);

    if (pxtcp->pcb == NULL) {
        return;
    }

    pxtcp_pcb_forward_inbound(pxtcp);
}


/**
 * tcp_poll() callback
 *
 * We swtich it on when tcp_write() or tcp_shutdown() fail with
 * ERR_MEM to prevent connection from stalling.  If there are ACKs or
 * more inbound data then pxtcp_pcb_forward_inbound() will be
 * triggered again, but if neither happens, tcp_poll() comes to the
 * rescue.
 */
static err_t
pxtcp_pcb_poll(void *arg, struct tcp_pcb *pcb)
{
    struct pxtcp *pxtcp = (struct pxtcp *)arg;
    LWIP_UNUSED_ARG(pcb);

    DPRINTF2(("%s: pxtcp %p; pcb %p\n",
              __func__, (void *)pxtcp, (void *)pxtcp->pcb));

    pxtcp_pcb_forward_inbound(pxtcp);

    /*
     * If the last thing holding up deletion of the pxtcp was failed
     * tcp_shutdown() and it succeeded, we may be the last callback.
     */
    pxtcp_pcb_maybe_deferred_delete(pxtcp);

    return ERR_OK;
}


static void
pxtcp_pcb_schedule_poll(struct pxtcp *pxtcp)
{
    tcp_poll(pxtcp->pcb, pxtcp_pcb_poll, 0);
}


static void
pxtcp_pcb_cancel_poll(struct pxtcp *pxtcp)
{
    tcp_poll(pxtcp->pcb, NULL, 255);
}


/**
 * Forward inbound data from ring buffer to the guest.
 *
 * Scheduled by poll manager thread after it receives more data into
 * the ring buffer (we have more data to send).

 * Also called from tcp_sent() callback when guest ACKs some data,
 * increasing pcb->snd_buf (we are permitted to send more data).
 *
 * Also called from tcp_poll() callback if previous attempt to forward
 * inbound data failed with ERR_MEM (we need to try again).
 */
static void
pxtcp_pcb_forward_inbound(struct pxtcp *pxtcp)
{
    struct tcp_pcb *pcb;
    size_t sndbuf;
    size_t beg, lim, sndlim;
    size_t toeob, tolim;
    size_t nsent;
    err_t error;

    LWIP_ASSERT1(pxtcp != NULL);
    pcb = pxtcp->pcb;
    if (pcb == NULL) {
        return;
    }

    if (/* __predict_false */ pcb->state < ESTABLISHED) {
        /*
         * If we have just confirmed accept of this connection, the
         * pcb is in SYN_RCVD state and we still haven't received the
         * ACK of our SYN.  It's only in SYN_RCVD -> ESTABLISHED
         * transition that lwip decrements pcb->acked so that that ACK
         * is not reported to pxtcp_pcb_sent().  If we send something
         * now and immediately close (think "daytime", e.g.)  while
         * still in SYN_RCVD state, we will move directly to
         * FIN_WAIT_1 and when our confirming SYN is ACK'ed lwip will
         * report it to pxtcp_pcb_sent().
         */
        DPRINTF2(("forward_inbound: pxtcp %p; pcb %p %s - later...\n",
                  (void *)pxtcp, (void *)pcb, tcp_debug_state_str(pcb->state)));
        return;
    }


    beg = pxtcp->inbuf.unsent;  /* private to lwip thread */
    lim = pxtcp->inbuf.vacant;

    if (beg == lim) {
        if (pxtcp->inbound_close && !pxtcp->inbound_close_done) {
            pxtcp_pcb_forward_inbound_close(pxtcp);
            tcp_output(pcb);
            return;
        }

        /*
         * Else, there's no data to send.
         *
         * If there is free space in the buffer, producer will
         * reschedule us as it receives more data and vacant (lim)
         * advances.
         *
         * If buffer is full when all data have been passed to
         * tcp_write() but not yet acknowledged, we will advance
         * unacked on ACK, freeing some space for producer to write to
         * (then see above).
         */
        return;
    }

    sndbuf = tcp_sndbuf(pcb);
    if (sndbuf == 0) {
        /*
         * Can't send anything now.  As guest ACKs some data, TCP will
         * call pxtcp_pcb_sent() callback and we will come here again.
         */
        return;
    }

    nsent = 0;

    /*
     * We have three limits to consider:
     * - how much data we have in the ringbuf
     * - how much data we are allowed to send
     * - ringbuf size
     */
    toeob = pxtcp->inbuf.bufsize - beg;
    if (lim < beg) {            /* lim wrapped */
        if (sndbuf < toeob) {   /* but we are limited by sndbuf */
            /* so beg is not going to wrap, treat sndbuf as lim */
            lim = beg + sndbuf; /* ... and proceed to the simple case */
        }
        else { /* we are limited by the end of the buffer, beg will wrap */
            u8_t maybemore;
            if (toeob == sndbuf || lim == 0) {
                maybemore = 0;
            }
            else {
                maybemore = TCP_WRITE_FLAG_MORE;
            }

            Assert(toeob == (u16_t)toeob);
            error = tcp_write(pcb, &pxtcp->inbuf.buf[beg], (u16_t)toeob, maybemore);
            if (error != ERR_OK) {
                goto writeerr;
            }
            nsent += toeob;
            pxtcp->inbuf.unsent = 0; /* wrap */

            if (maybemore) {
                beg = 0;
                sndbuf -= toeob;
            }
            else {
                /* we are done sending, but ... */
                goto check_inbound_close;
            }
        }
    }

    LWIP_ASSERT1(beg < lim);
    sndlim = beg + sndbuf;
    if (lim > sndlim) {
        lim = sndlim;
    }
    tolim = lim - beg;
    if (tolim > 0) {
        error = tcp_write(pcb, &pxtcp->inbuf.buf[beg], (u16_t)tolim, 0);
        if (error != ERR_OK) {
            goto writeerr;
        }
        nsent += tolim;
        pxtcp->inbuf.unsent = lim;
    }

  check_inbound_close:
    if (pxtcp->inbound_close && pxtcp->inbuf.unsent == pxtcp->inbuf.vacant) {
        pxtcp_pcb_forward_inbound_close(pxtcp);
    }

    DPRINTF2(("forward_inbound: pxtcp %p, pcb %p: sent %d bytes\n",
              (void *)pxtcp, (void *)pcb, (int)nsent));
    tcp_output(pcb);
    pxtcp_pcb_cancel_poll(pxtcp);
    return;

  writeerr:
    if (error == ERR_MEM) {
        if (nsent > 0) {    /* first write succeeded, second failed */
            DPRINTF2(("forward_inbound: pxtcp %p, pcb %p: sent %d bytes only\n",
                      (void *)pxtcp, (void *)pcb, (int)nsent));
            tcp_output(pcb);
        }
        DPRINTF(("forward_inbound: pxtcp %p, pcb %p: ERR_MEM\n",
                 (void *)pxtcp, (void *)pcb));
        pxtcp_pcb_schedule_poll(pxtcp);
    }
    else {
        DPRINTF(("forward_inbound: pxtcp %p, pcb %p: %s\n",
                 (void *)pxtcp, (void *)pcb, proxy_lwip_strerr(error)));

        /* XXX: We shouldn't get ERR_ARG.  Check ERR_CONN conditions early? */
        LWIP_ASSERT1(error == ERR_MEM);
    }
}


static void
pxtcp_pcb_forward_inbound_close(struct pxtcp *pxtcp)
{
    struct tcp_pcb *pcb;
    err_t error;

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->inbound_close);
    LWIP_ASSERT1(!pxtcp->inbound_close_done);
    LWIP_ASSERT1(pxtcp->inbuf.unsent == pxtcp->inbuf.vacant);

    pcb = pxtcp->pcb;
    LWIP_ASSERT1(pcb != NULL);

    DPRINTF(("inbound_close: pxtcp %p; pcb %p: %s\n",
             (void *)pxtcp, (void *)pcb, tcp_debug_state_str(pcb->state)));

    error = tcp_shutdown(pcb, /*RX*/ 0, /*TX*/ 1);
    if (error != ERR_OK) {
        DPRINTF(("inbound_close: pxtcp %p; pcb %p:"
                 " tcp_shutdown: error=%s\n",
                 (void *)pxtcp, (void *)pcb, proxy_lwip_strerr(error)));
        pxtcp_pcb_schedule_poll(pxtcp);
        return;
    }

    pxtcp_pcb_cancel_poll(pxtcp);
    pxtcp->inbound_close_done = 1;


    /*
     * If we have already done outbound close previously (passive
     * close on the pcb), then we must not hold onto a pcb in LAST_ACK
     * state since those will be deleted by lwip when that last ack
     * comes from the guest.
     *
     * NB: We do NOT check for deferred delete here, even though we
     * have just set one of its conditions, inbound_close_done.  We
     * let pcb callbacks that called us do that.  It's simpler and
     * cleaner that way.
     */
    if (pxtcp->outbound_close_done && pxtcp_pcb_forward_inbound_done(pxtcp)) {
        pxtcp_pcb_dissociate(pxtcp);
    }
}


/**
 * Check that all forwarded inbound data is sent and acked, and that
 * inbound close is scheduled (we aren't called back when it's acked).
 */
DECLINLINE(int)
pxtcp_pcb_forward_inbound_done(const struct pxtcp *pxtcp)
{
    return (pxtcp->inbound_close_done /* also implies that all data forwarded */
            && pxtcp->inbuf.unacked == pxtcp->inbuf.unsent);
}


/**
 * tcp_sent() callback - guest acknowledged len bytes.
 *
 * We can advance inbuf::unacked index, making more free space in the
 * ringbuf and wake up producer on poll manager thread.
 *
 * We can also try to send more data if we have any since pcb->snd_buf
 * was increased and we are now permitted to send more.
 */
static err_t
pxtcp_pcb_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    struct pxtcp *pxtcp = (struct pxtcp *)arg;
    size_t unacked;

    LWIP_ASSERT1(pxtcp != NULL);
    LWIP_ASSERT1(pxtcp->pcb == pcb);
    LWIP_ASSERT1(pcb->callback_arg == pxtcp);
    LWIP_UNUSED_ARG(pcb);       /* only in assert */

    DPRINTF2(("%s: pxtcp %p; pcb %p: +%d ACKed:"
              " unacked %d, unsent %d, vacant %d\n",
              __func__, (void *)pxtcp, (void *)pcb, (int)len,
              (int)pxtcp->inbuf.unacked,
              (int)pxtcp->inbuf.unsent,
              (int)pxtcp->inbuf.vacant));

    if (/* __predict_false */ len == 0) {
        /* we are notified to start pulling */
        LWIP_ASSERT1(!pxtcp->inbound_close);
        LWIP_ASSERT1(pxtcp->inbound_pull);

        unacked = pxtcp->inbuf.unacked;
    }
    else {
        /*
         * Advance unacked index.  Guest acknowledged the data, so it
         * won't be needed again for potential retransmits.
         */
        unacked = pxtcp->inbuf.unacked + len;
        if (unacked > pxtcp->inbuf.bufsize) {
            unacked -= pxtcp->inbuf.bufsize;
        }
        pxtcp->inbuf.unacked = unacked;
    }

    /* arrange for more inbound data */
    if (!pxtcp->inbound_close) {
        if (!pxtcp->inbound_pull) {
            /* wake up producer, in case it has stopped polling for POLLIN */
            pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_POLLIN, pxtcp);
#ifdef RT_OS_WINDOWS
            /**
             * We have't got enought room in ring buffer to read atm,
             * but we don't want to lose notification from WSAW4ME when
             * space would be available, so we reset event with empty recv
             */
            recv(pxtcp->sock, NULL, 0, 0);
#endif
        }
        else {
            ssize_t nread;
            int stop_pollin;    /* ignored */

            nread = pxtcp_sock_read(pxtcp, &stop_pollin);

            if (nread < 0) {
                int sockerr = -(int)nread;
                LWIP_UNUSED_ARG(sockerr);
                DPRINTF0(("%s: sock %d: %R[sockerr]\n",
                          __func__, pxtcp->sock, sockerr));

#if HAVE_TCP_POLLHUP == POLLIN /* see counterpart in pxtcp_pmgr_pump() */
                /*
                 * It may still be registered with poll manager for POLLOUT.
                 */
                pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_RESET, pxtcp);
                return ERR_OK;
#else
                /*
                 * It is no longer registered with poll manager so we
                 * can kill it directly.
                 */
                pxtcp_pcb_reset_pxtcp(pxtcp);
                return ERR_ABRT;
#endif
            }
        }
    }

    /* forward more data if we can */
    if (!pxtcp->inbound_close_done) {
        pxtcp_pcb_forward_inbound(pxtcp);

        /*
         * NB: we might have dissociated from a pcb that transitioned
         * to LAST_ACK state, so don't refer to pcb below.
         */
    }


    /* have we got all the acks? */
    if (pxtcp->inbound_close                          /* no more new data */
        && pxtcp->inbuf.unsent == pxtcp->inbuf.vacant /* all data is sent */
        && unacked == pxtcp->inbuf.unsent)            /* ... and is acked */
    {
        char *buf;

        DPRINTF(("%s: pxtcp %p; pcb %p; all data ACKed\n",
                 __func__, (void *)pxtcp, (void *)pxtcp->pcb));

        /* no more retransmits, so buf is not needed */
        buf = pxtcp->inbuf.buf;
        pxtcp->inbuf.buf = NULL;
        free(buf);

        /* no more acks, so no more callbacks */
        if (pxtcp->pcb != NULL) {
            tcp_sent(pxtcp->pcb, NULL);
        }

        /*
         * We may be the last callback for this pcb if we have also
         * successfully forwarded inbound_close.
         */
        pxtcp_pcb_maybe_deferred_delete(pxtcp);
    }

    return ERR_OK;
}


#if HAVE_TCP_POLLHUP
/**
 * Callback from poll manager (pxtcp::msg_inpull) to switch
 * pxtcp_pcb_sent() to actively pull the last bits of input.  See
 * POLLHUP comment in pxtcp_pmgr_pump().
 *
 * pxtcp::sock is deregistered from poll manager after this callback
 * is scheduled.
 */
static void
pxtcp_pcb_pull_inbound(void *ctx)
{
    struct pxtcp *pxtcp = (struct pxtcp *)ctx;
    LWIP_ASSERT1(pxtcp != NULL);

    if (pxtcp->pcb == NULL) {
        DPRINTF(("%s: pxtcp %p: PCB IS GONE\n", __func__, (void *)pxtcp));
        pxtcp_pcb_reset_pxtcp(pxtcp);
        return;
    }

    pxtcp->inbound_pull = 1;
    if (pxtcp->pmhdl.slot < 0) {
        DPRINTF(("%s: pxtcp %p: pcb %p (deferred delete)\n",
                 __func__, (void *)pxtcp, (void *)pxtcp->pcb));
        pxtcp->deferred_delete = 1;
    }
    else {
        DPRINTF(("%s: pxtcp %p: pcb %p\n",
                 __func__, (void *)pxtcp, (void *)pxtcp->pcb));
    }

    pxtcp_pcb_sent(pxtcp, pxtcp->pcb, 0);
}
#endif  /* HAVE_TCP_POLLHUP */


/**
 * tcp_err() callback.
 *
 * pcb is not passed to this callback since it may be already
 * deallocated by the stack, but we can't do anything useful with it
 * anyway since connection is gone.
 */
static void
pxtcp_pcb_err(void *arg, err_t error)
{
    struct pxtcp *pxtcp = (struct pxtcp *)arg;
    LWIP_ASSERT1(pxtcp != NULL);

    /*
     * ERR_CLSD is special - it is reported here when:
     *
     * . guest has already half-closed
     * . we send FIN to guest when external half-closes
     * . guest acks that FIN
     *
     * Since connection is closed but receive has been already closed
     * lwip can only report this via tcp_err.  At this point the pcb
     * is still alive, so we can peek at it if need be.
     *
     * The interesting twist is when the ACK from guest that akcs our
     * FIN also acks some data.  In this scenario lwip will NOT call
     * tcp_sent() callback with the ACK for that last bit of data but
     * instead will call tcp_err with ERR_CLSD right away.  Since that
     * ACK also acknowledges all the data, we should run some of
     * pxtcp_pcb_sent() logic here.
     */
    if (error == ERR_CLSD) {
        struct tcp_pcb *pcb = pxtcp->pcb; /* still alive */

        DPRINTF2(("ERR_CLSD: pxtcp %p; pcb %p:"
                  " pcb->acked %d;"
                  " unacked %d, unsent %d, vacant %d\n",
                  (void *)pxtcp, (void *)pcb,
                  pcb->acked,
                  (int)pxtcp->inbuf.unacked,
                  (int)pxtcp->inbuf.unsent,
                  (int)pxtcp->inbuf.vacant));

        LWIP_ASSERT1(pxtcp->pcb == pcb);
        LWIP_ASSERT1(pcb->callback_arg == pxtcp);

        if (pcb->acked > 0) {
            pxtcp_pcb_sent(pxtcp, pcb, pcb->acked);
        }
        return;
    }

    DPRINTF0(("tcp_err: pxtcp=%p, error=%s\n",
              (void *)pxtcp, proxy_lwip_strerr(error)));

    pxtcp->pcb = NULL;          /* pcb is gone */
    if (pxtcp->deferred_delete) {
        pxtcp_pcb_reset_pxtcp(pxtcp);
    }
    else {
        pxtcp_chan_send_weak(POLLMGR_CHAN_PXTCP_RESET, pxtcp);
    }
}
