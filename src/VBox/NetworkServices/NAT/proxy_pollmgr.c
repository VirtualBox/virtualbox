/* $Id: proxy_pollmgr.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - poll manager.
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

#include "proxy_pollmgr.h"
#include "proxy.h"

#ifndef RT_OS_WINDOWS
#include <sys/socket.h>
#include <netinet/in.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#else
#include <iprt/errcore.h>
#include <stdlib.h>
#include <string.h>
#include "winpoll.h"
#endif

#include <iprt/req.h>
#include <iprt/errcore.h>


#define POLLMGR_GARBAGE (-1)


enum {
    POLLMGR_QUEUE = 0,

    POLLMGR_SLOT_STATIC_COUNT,
    POLLMGR_SLOT_FIRST_DYNAMIC = POLLMGR_SLOT_STATIC_COUNT
};


struct pollmgr_chan {
    struct pollmgr_handler *handler;
    void *arg;
    bool arg_valid;
};

struct pollmgr {
    struct pollfd *fds;
    struct pollmgr_handler **handlers;
    nfds_t capacity;            /* allocated size of the arrays */
    nfds_t nfds;                /* part of the arrays in use */

    /* channels (socketpair) for static slots */
    SOCKET chan[POLLMGR_SLOT_STATIC_COUNT][2];
#define POLLMGR_CHFD_RD 0       /* - pollmgr side */
#define POLLMGR_CHFD_WR 1       /* - client side */


    /* emulate channels with request queue */
    RTREQQUEUE queue;
    struct pollmgr_handler queue_handler;
    struct pollmgr_chan chan_handlers[POLLMGR_CHAN_COUNT];
} pollmgr;


static int pollmgr_queue_callback(struct pollmgr_handler *, SOCKET, int);
static DECLCALLBACK(void) pollmgr_chan_call_handler(int, void *);

static void pollmgr_loop(void);

static void pollmgr_add_at(int, struct pollmgr_handler *, SOCKET, int);
static void pollmgr_refptr_delete(struct pollmgr_refptr *);


/*
 * We cannot portably peek at the length of the incoming datagram and
 * pre-allocate pbuf chain to recvmsg() directly to it.  On Linux it's
 * possible to recv with MSG_PEEK|MSG_TRUC, but extra syscall is
 * probably more expensive (haven't measured) than doing an extra copy
 * of data, since typical UDP datagrams are small enough to avoid
 * fragmentation.
 *
 * We can use shared buffer here since we read from sockets
 * sequentially in a loop over pollfd.
 */
u8_t pollmgr_udpbuf[64 * 1024];


int
pollmgr_init(void)
{
    struct pollfd *newfds;
    struct pollmgr_handler **newhdls;
    nfds_t newcap;
    int rc, status;
    nfds_t i;

    rc = RTReqQueueCreate(&pollmgr.queue);
    if (RT_FAILURE(rc))
        return -1;

    pollmgr.fds = NULL;
    pollmgr.handlers = NULL;
    pollmgr.capacity = 0;
    pollmgr.nfds = 0;

    for (i = 0; i < POLLMGR_SLOT_STATIC_COUNT; ++i) {
        pollmgr.chan[i][POLLMGR_CHFD_RD] = INVALID_SOCKET;
        pollmgr.chan[i][POLLMGR_CHFD_WR] = INVALID_SOCKET;
    }

    for (i = 0; i < POLLMGR_SLOT_STATIC_COUNT; ++i) {
#ifndef RT_OS_WINDOWS
        int j;

        status = socketpair(PF_LOCAL, SOCK_DGRAM, 0, pollmgr.chan[i]);
        if (status < 0) {
            DPRINTF(("socketpair: %R[sockerr]\n", SOCKERRNO()));
            goto cleanup_close;
        }

        /* now manually make them O_NONBLOCK */
        for (j = 0; j < 2; ++j) {
            int s = pollmgr.chan[i][j];
            int sflags;

            sflags = fcntl(s, F_GETFL, 0);
            if (sflags < 0) {
                DPRINTF0(("F_GETFL: %R[sockerr]\n", errno));
                goto cleanup_close;
            }

            status = fcntl(s, F_SETFL, sflags | O_NONBLOCK);
            if (status < 0) {
                DPRINTF0(("O_NONBLOCK: %R[sockerr]\n", errno));
                goto cleanup_close;
            }
        }
#else
        status = RTWinSocketPair(PF_INET, SOCK_DGRAM, 0, pollmgr.chan[i]);
        if (RT_FAILURE(status)) {
            goto cleanup_close;
        }
#endif
    }


    newcap = 16;                /* XXX: magic */
    LWIP_ASSERT1(newcap >= POLLMGR_SLOT_STATIC_COUNT);

    newfds = (struct pollfd *)
        malloc(newcap * sizeof(*pollmgr.fds));
    if (newfds == NULL) {
        DPRINTF(("%s: Failed to allocate fds array\n", __func__));
        goto cleanup_close;
    }

    newhdls = (struct pollmgr_handler **)
        malloc(newcap * sizeof(*pollmgr.handlers));
    if (newhdls == NULL) {
        DPRINTF(("%s: Failed to allocate handlers array\n", __func__));
        free(newfds);
        goto cleanup_close;
    }

    pollmgr.capacity = newcap;
    pollmgr.fds = newfds;
    pollmgr.handlers = newhdls;

    pollmgr.nfds = POLLMGR_SLOT_STATIC_COUNT;

    for (i = 0; i < pollmgr.capacity; ++i) {
        pollmgr.fds[i].fd = INVALID_SOCKET;
        pollmgr.fds[i].events = 0;
        pollmgr.fds[i].revents = 0;
    }

    /* add request queue notification */
    pollmgr.queue_handler.callback = pollmgr_queue_callback;
    pollmgr.queue_handler.data = NULL;
    pollmgr.queue_handler.slot = -1;

    pollmgr_add_at(POLLMGR_QUEUE, &pollmgr.queue_handler,
                   pollmgr.chan[POLLMGR_QUEUE][POLLMGR_CHFD_RD],
                   POLLIN);

    return 0;

  cleanup_close:
    for (i = 0; i < POLLMGR_SLOT_STATIC_COUNT; ++i) {
        SOCKET *chan = pollmgr.chan[i];
        if (chan[POLLMGR_CHFD_RD] != INVALID_SOCKET) {
            closesocket(chan[POLLMGR_CHFD_RD]);
            closesocket(chan[POLLMGR_CHFD_WR]);
        }
    }

    return -1;
}


/*
 * Add new channel.  We now implement channels with request queue, so
 * all channels get the same socket that triggers queue processing.
 *
 * Must be called before pollmgr loop is started, so no locking.
 */
SOCKET
pollmgr_add_chan(int slot, struct pollmgr_handler *handler)
{
    AssertReturn(0 <= slot && slot < POLLMGR_CHAN_COUNT, INVALID_SOCKET);
    AssertReturn(handler != NULL && handler->callback != NULL, INVALID_SOCKET);

    handler->slot = slot;
    pollmgr.chan_handlers[slot].handler = handler;
    return pollmgr.chan[POLLMGR_QUEUE][POLLMGR_CHFD_WR];
}


/*
 * This used to actually send data over the channel's socket.  Now we
 * queue a request and send single byte notification over shared
 * POLLMGR_QUEUE socket.
 */
ssize_t
pollmgr_chan_send(int slot, void *buf, size_t nbytes)
{
    static const char notification = 0x5a;

    void *ptr;
    SOCKET fd;
    ssize_t nsent;

    AssertReturn(0 <= slot && slot < POLLMGR_CHAN_COUNT, -1);

    /*
     * XXX: Hack alert.  We only ever "sent" single pointer which was
     * simultaneously both the wakeup event for the poll and the
     * argument for the channel handler that it read from the channel.
     * So now we pass this pointer to the request and arrange for the
     * handler to "read" it when it asks for it.
     */
    if (nbytes != sizeof(void *)) {
        return -1;
    }

    ptr = *(void **)buf;

    int rc = RTReqQueueCallEx(pollmgr.queue, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)pollmgr_chan_call_handler, 2, slot, ptr);
    if (RT_FAILURE(rc))
    {
        DPRINTF(("Queuing pollmgr_chan_call_handler() on poll manager queue failed with %Rrc\n", rc));
        return -1;
    }

    fd = pollmgr.chan[POLLMGR_QUEUE][POLLMGR_CHFD_WR];
    nsent = send(fd, &notification, 1, 0);
    if (nsent == SOCKET_ERROR) {
        DPRINTF(("send on chan %d: %R[sockerr]\n", slot, SOCKERRNO()));
        return -1;
    }
    else if ((size_t)nsent != 1) {
        DPRINTF(("send on chan %d: datagram truncated to %u bytes",
                 slot, (unsigned int)nsent));
        return -1;
    }

    /* caller thinks it's sending the pointer */
    return sizeof(void *);
}


/*
 * pollmgr_chan_send() sent us a notification, process the queue.
 */
static int
pollmgr_queue_callback(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    RT_NOREF(handler, revents);
    Assert(pollmgr.queue != NIL_RTREQQUEUE);

    ssize_t nread = recv(fd, (char *)pollmgr_udpbuf, sizeof(pollmgr_udpbuf), 0);
    if (nread == SOCKET_ERROR) {
        DPRINTF0(("%s: recv: %R[sockerr]\n", __func__, SOCKERRNO()));
        return POLLIN;
    }

    DPRINTF2(("%s: read %zd\n", __func__, nread));
    if (nread == 0) {
        return POLLIN;
    }

    int rc = RTReqQueueProcess(pollmgr.queue, 0);
    if (RT_UNLIKELY(rc != VERR_TIMEOUT && RT_FAILURE_NP(rc))) {
        DPRINTF0(("%s: RTReqQueueProcess: %Rrc\n", __func__, rc));
    }

    return POLLIN;
}


/*
 * Queued requests use this function to emulate the call to the
 * handler's callback.
 */
static void
pollmgr_chan_call_handler(int slot, void *arg)
{
    struct pollmgr_handler *handler;
    int nevents;

    AssertReturnVoid(0 <= slot && slot < POLLMGR_CHAN_COUNT);

    handler = pollmgr.chan_handlers[slot].handler;
    AssertReturnVoid(handler != NULL && handler->callback != NULL);

    /* arrange for pollmgr_chan_recv_ptr() to "receive" the arg */
    pollmgr.chan_handlers[slot].arg = arg;
    pollmgr.chan_handlers[slot].arg_valid = true;

    nevents = handler->callback(handler, INVALID_SOCKET, POLLIN);
    if (nevents != POLLIN) {
        DPRINTF2(("%s: nevents=0x%x!\n", __func__, nevents));
    }
}


/*
 * "Receive" a pointer "sent" over poll manager channel.
 */
void *
pollmgr_chan_recv_ptr(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    int slot;
    void *ptr;

    RT_NOREF(fd);

    slot = handler->slot;
    Assert(0 <= slot && slot < POLLMGR_CHAN_COUNT);

    if (revents & POLLNVAL) {
        errx(EXIT_FAILURE, "chan %d: fd invalid", (int)handler->slot);
        /* NOTREACHED */
    }

    if (revents & (POLLERR | POLLHUP)) {
        errx(EXIT_FAILURE, "chan %d: fd error", (int)handler->slot);
        /* NOTREACHED */
    }

    LWIP_ASSERT1(revents & POLLIN);

    if (!pollmgr.chan_handlers[slot].arg_valid) {
        err(EXIT_FAILURE, "chan %d: recv", (int)handler->slot);
        /* NOTREACHED */
    }

    ptr = pollmgr.chan_handlers[slot].arg;
    pollmgr.chan_handlers[slot].arg_valid = false;

    return ptr;
}


/*
 * Must be called from pollmgr loop (via callbacks), so no locking.
 */
int
pollmgr_add(struct pollmgr_handler *handler, SOCKET fd, int events)
{
    int slot;

    DPRINTF2(("%s: new fd %d\n", __func__, fd));

    if (pollmgr.nfds == pollmgr.capacity) {
        struct pollfd *newfds;
        struct pollmgr_handler **newhdls;
        nfds_t newcap;
        nfds_t i;

        newcap = pollmgr.capacity * 2;

        newfds = (struct pollfd *)
            realloc(pollmgr.fds, newcap * sizeof(*pollmgr.fds));
        if (newfds == NULL) {
            DPRINTF(("%s: Failed to reallocate fds array\n", __func__));
            handler->slot = -1;
            return -1;
        }

        pollmgr.fds = newfds; /* don't crash/leak if realloc(handlers) fails */
        /* but don't update capacity yet! */

        newhdls = (struct pollmgr_handler **)
            realloc(pollmgr.handlers, newcap * sizeof(*pollmgr.handlers));
        if (newhdls == NULL) {
            DPRINTF(("%s: Failed to reallocate handlers array\n", __func__));
            /* if we failed to realloc here, then fds points to the
             * new array, but we pretend we still has old capacity */
            handler->slot = -1;
            return -1;
        }

        pollmgr.handlers = newhdls;
        pollmgr.capacity = newcap;

        for (i = pollmgr.nfds; i < newcap; ++i) {
            newfds[i].fd = INVALID_SOCKET;
            newfds[i].events = 0;
            newfds[i].revents = 0;
            newhdls[i] = NULL;
        }
    }

    slot = pollmgr.nfds;
    ++pollmgr.nfds;

    pollmgr_add_at(slot, handler, fd, events);
    return slot;
}


static void
pollmgr_add_at(int slot, struct pollmgr_handler *handler, SOCKET fd, int events)
{
    pollmgr.fds[slot].fd = fd;
    pollmgr.fds[slot].events = events;
    pollmgr.fds[slot].revents = 0;
    pollmgr.handlers[slot] = handler;

    handler->slot = slot;
}


void
pollmgr_update_events(int slot, int events)
{
    LWIP_ASSERT1(slot >= POLLMGR_SLOT_FIRST_DYNAMIC);
    LWIP_ASSERT1((nfds_t)slot < pollmgr.nfds);

    pollmgr.fds[slot].events = events;
}


void
pollmgr_del_slot(int slot)
{
    LWIP_ASSERT1(slot >= POLLMGR_SLOT_FIRST_DYNAMIC);

    DPRINTF2(("%s(%d): fd %d ! DELETED\n",
              __func__, slot, pollmgr.fds[slot].fd));

    pollmgr.fds[slot].fd = INVALID_SOCKET; /* see poll loop */
}


void
pollmgr_thread(void *ignored)
{
    LWIP_UNUSED_ARG(ignored);
    pollmgr_loop();
}


static void
pollmgr_loop(void)
{
    int nready;
    SOCKET delfirst;
    SOCKET *pdelprev;
    int i;

    for (;;) {
#ifndef RT_OS_WINDOWS
        nready = poll(pollmgr.fds, pollmgr.nfds, -1);
#else
        int rc = RTWinPoll(pollmgr.fds, pollmgr.nfds,RT_INDEFINITE_WAIT, &nready);
        if (RT_FAILURE(rc)) {
            err(EXIT_FAILURE, "poll"); /* XXX: what to do on error? */
            /* NOTREACHED*/
        }
#endif

        DPRINTF2(("%s: ready %d fd%s\n",
                  __func__, nready, (nready == 1 ? "" : "s")));

        if (nready < 0) {
            if (errno == EINTR) {
                continue;
            }

            err(EXIT_FAILURE, "poll"); /* XXX: what to do on error? */
            /* NOTREACHED*/
        }
        else if (nready == 0) { /* cannot happen, we wait forever (-1) */
            continue;           /* - but be defensive */
        }


        delfirst = INVALID_SOCKET;
        pdelprev = &delfirst;

        for (i = 0; (nfds_t)i < pollmgr.nfds && nready > 0; ++i) {
            struct pollmgr_handler *handler;
            SOCKET fd;
            int revents, nevents;

            fd = pollmgr.fds[i].fd;
            revents = pollmgr.fds[i].revents;

            /*
             * Channel handlers can request deletion of dynamic slots
             * by calling pollmgr_del_slot() that clobbers slot's fd.
             */
            if (fd == INVALID_SOCKET && i >= POLLMGR_SLOT_FIRST_DYNAMIC) {
                /* adjust count if events were pending for that slot */
                if (revents != 0) {
                    --nready;
                }

                /* pretend that slot handler requested deletion */
                nevents = -1;
                goto update_events;
            }

            if (revents == 0) {
                continue; /* next fd */
            }
            --nready;

            handler = pollmgr.handlers[i];

            if (handler != NULL && handler->callback != NULL) {
#ifdef LWIP_PROXY_DEBUG
# if LWIP_PROXY_DEBUG /* DEBUG */
                if (i < POLLMGR_SLOT_FIRST_DYNAMIC) {
                    if (revents == POLLIN) {
                        DPRINTF2(("%s: ch %d\n", __func__, i));
                    }
                    else {
                        DPRINTF2(("%s: ch %d @ revents 0x%x!\n",
                                  __func__, i, revents));
                    }
                }
                else {
                    DPRINTF2(("%s: fd %d @ revents 0x%x\n",
                              __func__, fd, revents));
                }
# endif /* LWIP_PROXY_DEBUG / DEBUG */
#endif
                nevents = (*handler->callback)(handler, fd, revents);
            }
            else {
                DPRINTF0(("%s: invalid handler for fd %d: ", __func__, fd));
                if (handler == NULL) {
                    DPRINTF0(("NULL\n"));
                }
                else {
                    DPRINTF0(("%p (callback = NULL)\n", (void *)handler));
                }
                nevents = -1;   /* delete it */
            }

          update_events:
            if (nevents >= 0) {
                if (nevents != pollmgr.fds[i].events) {
                    DPRINTF2(("%s: fd %d ! nevents 0x%x\n",
                              __func__, fd, nevents));
                }
                pollmgr.fds[i].events = nevents;
            }
            else if (i < POLLMGR_SLOT_FIRST_DYNAMIC) {
                /* Don't garbage-collect channels. */
                DPRINTF2(("%s: fd %d ! DELETED (channel %d)\n",
                          __func__, fd, i));
                pollmgr.fds[i].fd = INVALID_SOCKET;
                pollmgr.fds[i].events = 0;
                pollmgr.fds[i].revents = 0;
                pollmgr.handlers[i] = NULL;
            }
            else {
                DPRINTF2(("%s: fd %d ! DELETED\n", __func__, fd));

                /* schedule for deletion (see g/c loop for details) */
                *pdelprev = i;  /* make previous entry point to us */
                pdelprev = &pollmgr.fds[i].fd;

                pollmgr.fds[i].fd = INVALID_SOCKET; /* end of list (for now) */
                pollmgr.fds[i].events = POLLMGR_GARBAGE;
                pollmgr.fds[i].revents = 0;
                pollmgr.handlers[i] = NULL;
            }
        } /* processing loop */


        /*
         * Garbage collect and compact the array.
         *
         * We overload pollfd::fd of garbage entries to store the
         * index of the next garbage entry.  The garbage list is
         * co-directional with the fds array.  The index of the first
         * entry is in "delfirst", the last entry "points to"
         * INVALID_SOCKET.
         *
         * See update_events code for nevents < 0 at the end of the
         * processing loop above.
         */
        while (delfirst != INVALID_SOCKET) {
            const int last = pollmgr.nfds - 1;

            /*
             * We want a live entry in the last slot to swap into the
             * freed slot, so make sure we have one.
             */
            if (pollmgr.fds[last].events == POLLMGR_GARBAGE /* garbage */
                || pollmgr.fds[last].fd == INVALID_SOCKET)  /* or killed */
            {
                /* drop garbage entry at the end of the array */
                --pollmgr.nfds;

                if (delfirst == (SOCKET)last) {
                    /* congruent to delnext >= pollmgr.nfds test below */
                    delfirst = INVALID_SOCKET; /* done */
                }
            }
            else {
                const SOCKET delnext = pollmgr.fds[delfirst].fd;

                /* copy live entry at the end to the first slot being freed */
                pollmgr.fds[delfirst] = pollmgr.fds[last]; /* struct copy */
                pollmgr.handlers[delfirst] = pollmgr.handlers[last];
                pollmgr.handlers[delfirst]->slot = (int)delfirst;
                --pollmgr.nfds;

                if ((nfds_t)delnext >= pollmgr.nfds) {
                    delfirst = INVALID_SOCKET; /* done */
                }
                else {
                    delfirst = delnext;
                }
            }

            pollmgr.fds[last].fd = INVALID_SOCKET;
            pollmgr.fds[last].events = 0;
            pollmgr.fds[last].revents = 0;
            pollmgr.handlers[last] = NULL;
        }
    } /* poll loop */
}


/**
 * Create strongly held refptr.
 */
struct pollmgr_refptr *
pollmgr_refptr_create(struct pollmgr_handler *ptr)
{
    struct pollmgr_refptr *rp;

    LWIP_ASSERT1(ptr != NULL);

    rp = (struct pollmgr_refptr *)malloc(sizeof (*rp));
    if (rp == NULL) {
        return NULL;
    }

    sys_mutex_new(&rp->lock);
    rp->ptr = ptr;
    rp->strong = 1;
    rp->weak = 0;

    return rp;
}


static void
pollmgr_refptr_delete(struct pollmgr_refptr *rp)
{
    if (rp == NULL) {
        return;
    }

    LWIP_ASSERT1(rp->strong == 0);
    LWIP_ASSERT1(rp->weak == 0);

    sys_mutex_free(&rp->lock);
    free(rp);
}


/**
 * Add weak reference before "rp" is sent over a poll manager channel.
 */
void
pollmgr_refptr_weak_ref(struct pollmgr_refptr *rp)
{
    sys_mutex_lock(&rp->lock);

    LWIP_ASSERT1(rp->ptr != NULL);
    LWIP_ASSERT1(rp->strong > 0);

    ++rp->weak;

    sys_mutex_unlock(&rp->lock);
}


/**
 * Try to get the pointer from implicitely weak reference we've got
 * from a channel.
 *
 * If we detect that the object is still strongly referenced, but no
 * longer registered with the poll manager we abort strengthening
 * conversion here b/c lwip thread callback is already scheduled to
 * destruct the object.
 */
struct pollmgr_handler *
pollmgr_refptr_get(struct pollmgr_refptr *rp)
{
    struct pollmgr_handler *handler;
    size_t weak;

    sys_mutex_lock(&rp->lock);

    LWIP_ASSERT1(rp->weak > 0);
    weak = --rp->weak;

    handler = rp->ptr;
    if (handler == NULL) {
        LWIP_ASSERT1(rp->strong == 0);
        sys_mutex_unlock(&rp->lock);
        if (weak == 0) {
            pollmgr_refptr_delete(rp);
        }
        return NULL;
    }

    LWIP_ASSERT1(rp->strong == 1);

    /*
     * Here we woild do:
     *
     *   ++rp->strong;
     *
     * and then, after channel handler is done, we would decrement it
     * back.
     *
     * Instead we check that the object is still registered with poll
     * manager. If it is, there's no race with lwip thread trying to
     * drop its strong reference, as lwip thread callback to destruct
     * the object is always scheduled by its poll manager callback.
     *
     * Conversly, if we detect that the object is no longer registered
     * with poll manager, we immediately abort. Since channel handler
     * can't do anything useful anyway and would have to return
     * immediately.
     *
     * Since channel handler would always find rp->strong as it had
     * left it, just elide extra strong reference creation to avoid
     * the whole back-and-forth.
     */

    if (handler->slot < 0) { /* no longer polling */
        sys_mutex_unlock(&rp->lock);
        return NULL;
    }

    sys_mutex_unlock(&rp->lock);
    return handler;
}


/**
 * Remove (the only) strong reference.
 *
 * If it were real strong/weak pointers, we should also call
 * destructor for the referenced object, but
 */
void
pollmgr_refptr_unref(struct pollmgr_refptr *rp)
{
    sys_mutex_lock(&rp->lock);

    LWIP_ASSERT1(rp->strong == 1);
    --rp->strong;

    if (rp->strong > 0) {
        sys_mutex_unlock(&rp->lock);
    }
    else {
        size_t weak;

        /* void *ptr = rp->ptr; */
        rp->ptr = NULL;

        /* delete ptr; // see doc comment */

        weak = rp->weak;
        sys_mutex_unlock(&rp->lock);
        if (weak == 0) {
            pollmgr_refptr_delete(rp);
        }
    }
}
