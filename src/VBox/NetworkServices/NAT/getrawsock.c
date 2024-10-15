/* $Id: getrawsock.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Obtain raw-sockets from a server when debugging unprivileged.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* XXX: this should be in a header, but isn't.  naughty me. :( */
int getrawsock(int type);


int
getrawsock(int type)
{
    struct sockaddr_un sux;     /* because solaris */
    struct passwd *pw;
    size_t pathlen;
    int rawsock, server;
    struct msghdr mh;
    struct iovec iov[1];
    char buf[1];
    struct cmsghdr *cmh;
    char cmsg[CMSG_SPACE(sizeof(int))];
    ssize_t nread, nsent;
    int status;

    server = -1;
    rawsock = -1;

    memset(&sux, 0, sizeof(sux));
    sux.sun_family = AF_UNIX;

    if (geteuid() == 0) {
        return -1;
    }

    if (type == AF_INET) {
        buf[0] = '4';
    }
    else if (type == AF_INET6) {
        buf[0] = '6';
    }
    else {
        return -1;
    }

    errno = 0;
    pw = getpwuid(getuid());
    if (pw == NULL) {
        perror("getpwuid");
        return -1;
    }

    pathlen = snprintf(sux.sun_path, sizeof(sux.sun_path),
                       "/tmp/.vbox-%s-aux/mkrawsock", pw->pw_name);
    if (pathlen > sizeof(sux.sun_path)) {
        fprintf(stderr, "socket pathname truncated\n");
        return -1;
    }

    server = socket(PF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return -1;
    }

    status = connect(server, (struct sockaddr *)&sux,
                     (sizeof(sux) - sizeof(sux.sun_path)
                      + strlen(sux.sun_path) + 1));
    if (status < 0) {
        perror(sux.sun_path);
        goto out;
    }

    nsent = send(server, buf, 1, 0);
    if (nsent != 1) {
        if (nsent < 0) {
            perror("send");
        }
        else {
            fprintf(stderr, "failed to contact mkrawsock\n");
        }
        goto out;
    }

    buf[0] = '\0';

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;

    memset(&mh, 0, sizeof(mh));
    mh.msg_iov = iov;
    mh.msg_iovlen = 1;
    mh.msg_control = cmsg;
    mh.msg_controllen = sizeof(cmsg);

    nread = recvmsg(server, &mh, 0);
    if (nread != 1) {
        if (nread < 0) {
            perror("recvmsg");
        }
        else {
            fprintf(stderr, "EOF from mkrawsock\n");
        }
        goto out;
    }

    if ((type == AF_INET && buf[0] != '4')
        || (type == AF_INET6 && buf[0] != '6')
        || mh.msg_controllen == 0)
    {
        goto out;
    }

    for (cmh = CMSG_FIRSTHDR(&mh); cmh != NULL; cmh = CMSG_NXTHDR(&mh, cmh)) {
        if ((cmh->cmsg_level == SOL_SOCKET)
            && (cmh->cmsg_type == SCM_RIGHTS)
            && (cmh->cmsg_len == CMSG_LEN(sizeof(rawsock))))
        {
            rawsock = *((int *)CMSG_DATA(cmh));
            break;
        }
    }

  out:
    if (server != -1) {
        close(server);
    }
    if (rawsock != -1) {
        printf("%s: got ICMPv%c socket %d\n",
               __func__, type == AF_INET ? '4' : '6', rawsock);
    }
    return rawsock;
}
