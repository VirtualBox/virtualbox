/* $Id: mkrawsock.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Auxiliary server to create raw-sockets when debugging unprivileged.
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

#ifdef __linux__
#define _GNU_SOURCE
#endif

#ifdef __sun__
#if __STDC_VERSION__ - 0 >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif
#define __EXTENSIONS__ 1
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#ifdef __linux__
#include <linux/icmp.h>         /* for ICMP_FILTER */
#endif
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static void handler(int sig);
static void serve(int s);
static int mkrawsock(int family);

volatile sig_atomic_t signaled = 0;

int
main(int argc, char **argv)
{
    struct sigaction sa;
    struct sockaddr_un sux; /* because solaris */
    struct passwd *pw;
    size_t pathlen;
    char *slash;
    int s, client;
    int status;

    memset(&sux, 0, sizeof(sux));
    sux.sun_family = AF_UNIX;

    if (getuid() == 0) {
        if (argc != 2) {
            fprintf(stderr, "username required when run as root\n");
            return EXIT_FAILURE;
        }

        errno = 0;
        pw = getpwnam(argv[1]);
        if (pw == NULL) {
            perror("getpwnam");
            return EXIT_FAILURE;
        }
        if (pw->pw_uid == 0) {
            fprintf(stderr, "%s is superuser\n", pw->pw_name);
            return EXIT_FAILURE;
        }
    }
    else {
        errno = 0;
        pw = getpwuid(getuid());
        if (pw == NULL) {
            perror("getpwuid");
            return EXIT_FAILURE;
        }
    }

    pathlen = snprintf(sux.sun_path, sizeof(sux.sun_path),
                       "/tmp/.vbox-%s-aux/mkrawsock", pw->pw_name);
    if (pathlen > sizeof(sux.sun_path)) {
        fprintf(stderr, "socket pathname truncated\n");
        return EXIT_FAILURE;
    }

    slash = strrchr(sux.sun_path, '/');
    if (slash == NULL) {
        fprintf(stderr, "%s: no directory separator\n", sux.sun_path);
        return EXIT_FAILURE;
    }

    *slash = '\0';

    status = mkdir(sux.sun_path, 0700);
    if (status == 0) {
        status = chown(sux.sun_path, pw->pw_uid, pw->pw_gid);
        if (status < 0) {
            perror("chown");
            return EXIT_FAILURE;
        }
    }
    else if (errno != EEXIST) {
        perror("mkdir");
        return EXIT_FAILURE;
    }
    else {
        int dirfd;
        struct stat st;

        dirfd = open(sux.sun_path, O_RDONLY, O_DIRECTORY);
        if (dirfd < 0) {
            perror(sux.sun_path);
            return EXIT_FAILURE;
        }

        status = fstat(dirfd, &st);
        close(dirfd);

        if (status < 0) {
            perror(sux.sun_path);
            return EXIT_FAILURE;
        }

        if (st.st_uid != pw->pw_uid) {
            fprintf(stderr, "%s: exists but not owned by %s\n",
                    sux.sun_path, pw->pw_name);
            return EXIT_FAILURE;
        }

        if ((st.st_mode & 0777) != 0700) {
            fprintf(stderr, "%s: bad mode %04o\n",
                    sux.sun_path, (unsigned int)(st.st_mode & 0777));
            return EXIT_FAILURE;
        }
    }

    *slash = '/';

#if 0
    status = unlink(sux.sun_path);
    if (status < 0 && errno != ENOENT) {
        perror("unlink");
    }
#endif

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    status = bind(s, (struct sockaddr *)&sux,
                  (sizeof(sux) - sizeof(sux.sun_path)
                   + strlen(sux.sun_path) + 1));
    if (status < 0) {
        perror(sux.sun_path);
        close(s);
        return EXIT_FAILURE;
    }

    status = chown(sux.sun_path, pw->pw_uid, pw->pw_gid);
    if (status < 0) {
        perror("chown");
        close(s);
        return EXIT_FAILURE;
    }

    status = chmod(sux.sun_path, 0600);
    if (status < 0) {
        perror("chmod");
        close(s);
        return EXIT_FAILURE;
    }

    status = listen(s, 1);
    if (status < 0) {
        perror("listen");
        close(s);
        return EXIT_FAILURE;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    while (!signaled) {
        client = accept(s, NULL, 0);
        if (client < 0) {
            perror("accept");
            continue;
        }

        serve(client);
        close(client);
    }

    close(s);
    status = unlink(sux.sun_path);
    if (status < 0) {
        perror("unlink");
    }

    return EXIT_SUCCESS;
}


static void
handler(int sig)
{
    signaled = 1;
}


static void
serve(int client)
{
#ifdef SO_PEERCRED
    struct ucred cr;
    socklen_t crlen;
#endif
    ssize_t nread, nsent;
    struct msghdr mh;
    struct iovec iov[1];
    char buf[1];
    struct cmsghdr *cmh;
    char cmsg[CMSG_SPACE(sizeof(int))];
    int fd;
    int status;

#ifdef SO_PEERCRED
    crlen = sizeof(cr);
    status = getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cr, &crlen);
    if (status < 0) {
        perror("SO_PEERCRED");
        return;
    }

    fprintf(stderr, "request from pid %lu uid %lu ",
            (unsigned long)cr.pid, (unsigned long)cr.uid);
#endif

    nread = read(client, buf, 1);
    if (nread < 0) {
        perror("recv");
        return;
    }

    fd = -1;
    switch (buf[0]) {

    case '4':
        fprintf(stderr, "for ICMPv4 socket\n");
        fd = mkrawsock(PF_INET);
        break;

    case '6':
        fprintf(stderr, "for ICMPv6 socket\n");
        fd = mkrawsock(PF_INET6);
        break;

    default:
        fprintf(stderr, "bad request 0x%02x\n", (unsigned int)buf[0]);
        return;
    }

    if (fd < 0) {
        buf[0] = '\0';  /* NAK */
        nsent = write(client, buf, 1);
        (void)nsent;
        return;
    }

    memset(&mh, 0, sizeof(mh));
    memset(cmsg, 0, sizeof(cmsg));

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;

    mh.msg_iov = iov;
    mh.msg_iovlen = 1;
    mh.msg_control = cmsg;
    mh.msg_controllen = sizeof(cmsg);

    cmh = CMSG_FIRSTHDR(&mh);
    cmh->cmsg_level = SOL_SOCKET;
    cmh->cmsg_type = SCM_RIGHTS;
    cmh->cmsg_len = CMSG_LEN(sizeof(fd));
    *((int *) CMSG_DATA(cmh)) = fd;

    nsent = sendmsg(client, &mh, 0);
    if (nsent < 0) {
        perror("sendmsg");
    }

    close(fd);
}


static int
mkrawsock(int family)
{
    int fd;

    if (family == PF_INET) {
        fd = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (fd < 0) {
            perror("IPPROTO_ICMP");
            return -1;
        }
    }
    else {
        fd = socket(PF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
        if (fd < 0) {
            perror("IPPROTO_ICMPV6");
            return -1;
        }
    }

    return fd;
}
