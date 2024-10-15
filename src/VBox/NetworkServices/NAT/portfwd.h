/* $Id: portfwd.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - port-forwarding rules, definitions and declarations.
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

#ifndef VBOX_INCLUDED_SRC_NAT_portfwd_h
#define VBOX_INCLUDED_SRC_NAT_portfwd_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef RT_OS_WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include "lwip/ip_addr.h"


struct fwspec {
    int sdom;                   /* PF_INET, PF_INET6 */
    int stype;                  /* SOCK_STREAM, SOCK_DGRAM */

    /* listen on */
    union {
        struct sockaddr sa;
        struct sockaddr_in sin;   /* sdom == PF_INET  */
        struct sockaddr_in6 sin6; /* sdom == PF_INET6 */
    } src;

    /* forward to */
    union {
        struct sockaddr sa;
        struct sockaddr_in sin;   /* sdom == PF_INET  */
        struct sockaddr_in6 sin6; /* sdom == PF_INET6 */
    } dst;
};


void portfwd_init(void);
int portfwd_rule_add(struct fwspec *);
int portfwd_rule_del(struct fwspec *);


int fwspec_set(struct fwspec *, int, int,
               const char *, uint16_t,
               const char *, uint16_t);

int fwspec_equal(struct fwspec *, struct fwspec *);

void fwtcp_init(void);
void fwudp_init(void);

void fwtcp_add(struct fwspec *);
void fwtcp_del(struct fwspec *);
void fwudp_add(struct fwspec *);
void fwudp_del(struct fwspec *);

int fwany_ipX_addr_set_src(ipX_addr_t *, const struct sockaddr *);

#endif /* !VBOX_INCLUDED_SRC_NAT_portfwd_h */
