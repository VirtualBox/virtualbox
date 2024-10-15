/* $Id: proxy_pollmgr.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - poll manager, definitions and declarations.
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

#ifndef VBOX_INCLUDED_SRC_NAT_proxy_pollmgr_h
#define VBOX_INCLUDED_SRC_NAT_proxy_pollmgr_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef RT_OS_WINDOWS
# include <unistd.h>             /* for ssize_t */
#endif
#include "lwip/sys.h"

enum pollmgr_slot_t {
    POLLMGR_CHAN_PXTCP_ADD,     /* new proxy tcp connection from guest */
    POLLMGR_CHAN_PXTCP_POLLIN,  /* free space in ringbuf, may POLLIN */
    POLLMGR_CHAN_PXTCP_POLLOUT, /* schedule one-shot POLLOUT callback */
    POLLMGR_CHAN_PXTCP_DEL,     /* delete pxtcp */
    POLLMGR_CHAN_PXTCP_RESET,   /* send RST and delete pxtcp */

    POLLMGR_CHAN_PXUDP_ADD,     /* new proxy udp conversation from guest */
    POLLMGR_CHAN_PXUDP_DEL,     /* delete pxudp from pollmgr */

    POLLMGR_CHAN_PORTFWD,       /* add/remove port forwarding rules */

    POLLMGR_CHAN_COUNT
};


struct pollmgr_handler;         /* forward */
typedef int (*pollmgr_callback)(struct pollmgr_handler *, SOCKET, int);

struct pollmgr_handler {
    pollmgr_callback callback;
    void *data;
    int slot;
};

struct pollmgr_refptr {
    struct pollmgr_handler *ptr;
    sys_mutex_t lock;
    size_t strong;
    size_t weak;
};

int pollmgr_init(void);

/* static named slots (aka "channels") */
SOCKET pollmgr_add_chan(int, struct pollmgr_handler *);
ssize_t pollmgr_chan_send(int, void *buf, size_t nbytes);
void *pollmgr_chan_recv_ptr(struct pollmgr_handler *, SOCKET, int);

/* dynamic slots */
int pollmgr_add(struct pollmgr_handler *, SOCKET, int);

/* special-purpose strong/weak references */
struct pollmgr_refptr *pollmgr_refptr_create(struct pollmgr_handler *);
void pollmgr_refptr_weak_ref(struct pollmgr_refptr *);
struct pollmgr_handler *pollmgr_refptr_get(struct pollmgr_refptr *);
void pollmgr_refptr_unref(struct pollmgr_refptr *);

void pollmgr_update_events(int, int);
void pollmgr_del_slot(int);

void pollmgr_thread(void *);

/* buffer for callbacks to receive udp without worrying about truncation */
extern u8_t pollmgr_udpbuf[64 * 1024];

#endif /* !VBOX_INCLUDED_SRC_NAT_proxy_pollmgr_h */
