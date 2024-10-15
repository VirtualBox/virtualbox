/* $Id: rtmon_linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - IPv6 default route monitor for Linux netlink.
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

#include <sys/types.h>          /* must come before linux/netlink */
#include <sys/socket.h>

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>


static int rtmon_check_defaults(const void *buf, size_t len);


/**
 * Read IPv6 routing table - Linux rtnetlink version.
 *
 * XXX: TODO: To avoid re-reading the table we should subscribe to
 * updates by binding a monitoring NETLINK_ROUTE socket to
 * sockaddr_nl::nl_groups = RTMGRP_IPV6_ROUTE.
 *
 * But that will provide updates only.  Documentation is scarce, but
 * from what I've seen it seems that to get accurate routing info the
 * monitoring socket needs to be created first, then full routing
 * table requested (easier to do via spearate socket), then monitoring
 * socket polled for input.  The first update(s) of the monitoring
 * socket may happen before full table is returned, so we can't just
 * count the defaults, we need to keep track of their { oif, gw } to
 * correctly ignore updates that are reported via monitoring socket,
 * but that are already reflected in the full routing table returned
 * in response to our request.
 */
int
rtmon_get_defaults(void)
{
    int rtsock;
    ssize_t nsent, ssize;
    int ndefrts;

    char *buf = NULL;
    size_t bufsize;

    struct {
        struct nlmsghdr nh;
        struct rtmsg rtm;
        char attrbuf[512];
    } rtreq;

    memset(&rtreq, 0, sizeof(rtreq));
    rtreq.nh.nlmsg_type = RTM_GETROUTE;
    rtreq.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    rtreq.rtm.rtm_family = AF_INET6;
    rtreq.rtm.rtm_table = RT_TABLE_MAIN;
    rtreq.rtm.rtm_protocol = RTPROT_UNSPEC;

    rtreq.nh.nlmsg_len = NLMSG_SPACE(sizeof(rtreq.rtm));

    bufsize = 1024;
    ssize = bufsize;
    for (;;) {
        char *newbuf;
        int recverr;

        newbuf = (char *)realloc(buf, ssize);
        if (newbuf == NULL) {
            DPRINTF0(("rtmon: failed to %sallocate buffer\n",
                      buf == NULL ? "" : "re"));
            free(buf);
            return -1;
        }

        buf = newbuf;
        bufsize = ssize;

        /* it's easier to reopen than to flush */
        rtsock = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (rtsock < 0) {
            DPRINTF0(("rtmon: failed to create netlink socket: %s", strerror(errno)));
            free(buf);
            return -1;
        }

        nsent = send(rtsock, &rtreq, rtreq.nh.nlmsg_len, 0);
        if (nsent < 0) {
            DPRINTF0(("rtmon: RTM_GETROUTE failed: %s", strerror(errno)));
            close (rtsock);
            free(buf);
            return -1;
        }

        ssize = recv(rtsock, buf, bufsize, MSG_TRUNC);
        recverr = errno;
        close (rtsock);

        if (ssize < 0) {
            DPRINTF(("rtmon: failed to read RTM_GETROUTE response: %s",
                     strerror(recverr)));
            free(buf);
            return -1;
        }

        if ((size_t)ssize <= bufsize) {
            DPRINTF2(("rtmon: RTM_GETROUTE: %lu bytes\n",
                      (unsigned long)ssize));
            break;
        }

        DPRINTF2(("rtmon: RTM_GETROUTE: truncated %lu to %lu bytes, retrying\n",
                  (unsigned long)ssize, (unsigned long)bufsize));
        /* try again with larger buffer */
    }

    ndefrts = rtmon_check_defaults(buf, (size_t)ssize);
    free(buf);

    if (ndefrts == 0) {
        DPRINTF(("rtmon: no IPv6 default routes found\n"));
    }
    else {
        DPRINTF(("rtmon: %d IPv6 default route%s found\n",
                 ndefrts,
                 ndefrts == 1 || ndefrts == -1 ? "" : "s"));
    }

    return ndefrts;
}


/**
 * Scan netlink message in the buffer for IPv6 default route changes.
 */
static int
rtmon_check_defaults(const void *buf, size_t len)
{
    struct nlmsghdr *nh;
    int dfltdiff = 0;

    for (nh = (struct nlmsghdr *)buf;
         NLMSG_OK(nh, len);
         nh = NLMSG_NEXT(nh, len))
    {
        struct rtmsg *rtm;
        struct rtattr *rta;
        int attrlen;
        int delta = 0;
        const void *gwbuf;
        size_t gwlen;
        int oif;

        DPRINTF2(("nlmsg seq %d type %d flags 0x%x\n",
                  nh->nlmsg_seq, nh->nlmsg_type, nh->nlmsg_flags));

        if (nh->nlmsg_type == NLMSG_DONE) {
            break;
        }

        if (nh->nlmsg_type == NLMSG_ERROR) {
            struct nlmsgerr *ne = (struct nlmsgerr *)NLMSG_DATA(nh);
            DPRINTF2(("> error %d\n", ne->error));
            LWIP_UNUSED_ARG(ne);
            break;
        }

        if (nh->nlmsg_type < RTM_BASE || RTM_MAX <= nh->nlmsg_type) {
            /* shouldn't happen */
            DPRINTF2(("> not an RTM message!\n"));
            continue;
        }


        rtm = (struct rtmsg *)NLMSG_DATA(nh);
        attrlen = RTM_PAYLOAD(nh);

        if (nh->nlmsg_type == RTM_NEWROUTE) {
            delta = +1;
        }
        else if (nh->nlmsg_type == RTM_DELROUTE) {
            delta = -1;
        }
        else {
            /* shouldn't happen */
            continue;
        }

        /*
         * Is this an IPv6 default route in the main table?  (Local
         * table always has ::/0 reject route, hence the last check).
         */
        if (rtm->rtm_family == AF_INET6 /* should always be true */
            && rtm->rtm_dst_len == 0
            && rtm->rtm_table == RT_TABLE_MAIN)
        {
            dfltdiff += delta;
        }
        else {
            /* some other route change */
            continue;
        }


        gwbuf = NULL;
        gwlen = 0;
        oif = -1;

        for (rta = RTM_RTA(rtm);
             RTA_OK(rta, attrlen);
             rta = RTA_NEXT(rta, attrlen))
        {
            if (rta->rta_type == RTA_GATEWAY) {
                gwbuf = RTA_DATA(rta);
                gwlen = RTA_PAYLOAD(rta);
            }
            else if (rta->rta_type == RTA_OIF) {
                /* assert RTA_PAYLOAD(rta) == 4 */
                memcpy(&oif, RTA_DATA(rta), sizeof(oif));
            }
        }

        /* XXX: TODO: note that { oif, gw } was added/removed */
        LWIP_UNUSED_ARG(gwbuf);
        LWIP_UNUSED_ARG(gwlen);
        LWIP_UNUSED_ARG(oif);
    }

    return dfltdiff;
}
