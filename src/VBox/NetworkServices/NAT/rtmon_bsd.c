/* $Id: rtmon_bsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - IPv6 default route monitor for BSD routing sockets.
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

#include "proxy.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if_dl.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip6.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>


/**
 * Query IPv6 routing table - BSD routing sockets version.
 *
 * We don't actually monitor the routing socket for updates, and
 * instead query the kernel each time.
 *
 * We take a shortcut and don't read the reply to our RTM_GET - if
 * there's no default IPv6 route, write(2) will fail with ESRCH
 * synchronously.  In theory it may fail asynchronously and we should
 * wait for the RTM_GET reply and check rt_msghdr::rtm_errno.
 *
 * KAME code in *BSD maintains internally a list of default routers
 * that it learned from RAs, and installs only one of them into the
 * routing table (actually, I'm not sure if BSD routing table can
 * handle multiple routes to the same destination).  One side-effect
 * of this is that when manually configured route (e.g. teredo) is
 * deleted, the system will lose its default route even when KAME IPv6
 * has default router(s) in its internal list.  Next RA will force the
 * update, though.
 *
 * Solaris does expose multiple routes in the routing table and
 * replies to RTM_GET with "default default".
 */
int
rtmon_get_defaults(void)
{
    int rtsock;
    struct req {
        struct rt_msghdr rtm;
        struct sockaddr_in6 dst;
        struct sockaddr_in6 mask;
        struct sockaddr_dl ifp;
    } req;
    ssize_t nsent;

    rtsock = socket(PF_ROUTE, SOCK_RAW, AF_INET6);
    if (rtsock < 0) {
        DPRINTF0(("rtmon: failed to create routing socket\n"));
        return -1;
    }

    memset(&req, 0, sizeof(req));

    req.rtm.rtm_type = RTM_GET;
    req.rtm.rtm_version = RTM_VERSION;
    req.rtm.rtm_msglen = sizeof(req);
    req.rtm.rtm_seq = 0x12345;

    req.rtm.rtm_flags = RTF_UP;
    req.rtm.rtm_addrs = RTA_DST | RTA_NETMASK | RTA_IFP;

    req.dst.sin6_family = AF_INET6;
#if HAVE_SA_LEN
    req.dst.sin6_len = sizeof(req.dst);
#endif

    req.mask.sin6_family = AF_INET6;
#if HAVE_SA_LEN
    req.mask.sin6_len = sizeof(req.mask);
#endif

    req.ifp.sdl_family = AF_LINK;
#if HAVE_SA_LEN
    req.ifp.sdl_len = sizeof(req.ifp);
#endif

    int rc = 1;
    nsent = write(rtsock, &req, req.rtm.rtm_msglen);
    if (nsent < 0) {
        if (errno == ESRCH) {
            /* there's no default route */
            rc = 0;
        }
        else {
            DPRINTF0(("rtmon: failed to send RTM_GET\n"));
            rc = -1;
        }
    }

    close(rtsock);
    return rc;
}
