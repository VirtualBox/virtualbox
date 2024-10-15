/* $Id: proxy.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - proxy setup and utilities.
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

#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/tcpip.h"

#ifndef RT_OS_WINDOWS
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <iprt/string.h>
#include <unistd.h>
#include <err.h>
#else
# include <iprt/string.h>
#endif

#if defined(SOCK_NONBLOCK) && defined(RT_OS_NETBSD) /* XXX: PR kern/47569 */
# undef SOCK_NONBLOCK
#endif

#ifndef __arraycount
# define __arraycount(a) (sizeof(a)/sizeof(a[0]))
#endif

static FNRTSTRFORMATTYPE proxy_sockerr_rtstrfmt;

static SOCKET proxy_create_socket(int, int);

volatile struct proxy_options *g_proxy_options;
static sys_thread_t pollmgr_tid;

/* XXX: for mapping loopbacks to addresses in our network (ip4) */
struct netif *g_proxy_netif;


/*
 * Called on the lwip thread (aka tcpip thread) from tcpip_init() via
 * its "tcpip_init_done" callback.  Raw API is ok to use here
 * (e.g. rtadvd), but netconn API is not.
 */
void
proxy_init(struct netif *proxy_netif, struct proxy_options *opts)
{
    int status;

    LWIP_ASSERT1(opts != NULL);
    LWIP_UNUSED_ARG(proxy_netif);

    status = RTStrFormatTypeRegister("sockerr", proxy_sockerr_rtstrfmt, NULL);
    AssertRC(status);

    g_proxy_options = opts;
    g_proxy_netif = proxy_netif;

#if 1
    proxy_rtadvd_start(proxy_netif);
#endif

    /*
     * XXX: We use stateless DHCPv6 only to report IPv6 address(es) of
     * nameserver(s).  Since we don't yet support IPv6 addresses in
     * HostDnsService, there's no point in running DHCPv6.
     */
#if 0
    dhcp6ds_init(proxy_netif);
#endif

    if (opts->tftp_root != NULL) {
        tftpd_init(proxy_netif, opts->tftp_root);
    }

    status = pollmgr_init();
    if (status < 0) {
        errx(EXIT_FAILURE, "failed to initialize poll manager");
        /* NOTREACHED */
    }

    pxtcp_init();
    pxudp_init();

    portfwd_init();

    pxdns_init(proxy_netif);

    pxping_init(proxy_netif, opts->icmpsock4, opts->icmpsock6);

    pollmgr_tid = sys_thread_new("pollmgr_thread",
                                 pollmgr_thread, NULL,
                                 DEFAULT_THREAD_STACKSIZE,
                                 DEFAULT_THREAD_PRIO);
    if (!pollmgr_tid) {
        errx(EXIT_FAILURE, "failed to create poll manager thread");
        /* NOTREACHED */
    }
}


#if !defined(RT_OS_WINDOWS)
/**
 * Formatter for %R[sockerr] - unix strerror_r() version.
 */
static DECLCALLBACK(size_t)
proxy_sockerr_rtstrfmt(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                       const char *pszType, const void *pvValue,
                       int cchWidth, int cchPrecision, unsigned int fFlags,
                       void *pvUser)
{
    const int error = (int)(intptr_t)pvValue;

    const char *msg;
    char buf[128];

    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);

    AssertReturn(strcmp(pszType, "sockerr") == 0, 0);

    /* make sure return type mismatch is caught */
    buf[0] = '\0';
#if defined(RT_OS_LINUX) && defined(_GNU_SOURCE)
    msg = strerror_r(error, buf, sizeof(buf));
#else
    strerror_r(error, buf, sizeof(buf));
    msg = buf;
#endif
    return RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL, "%s", msg);
}

#else /* RT_OS_WINDOWS */

/**
 * Formatter for %R[sockerr] - windows FormatMessage() version.
 */
static DECLCALLBACK(size_t)
proxy_sockerr_rtstrfmt(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                       const char *pszType, const void *pvValue,
                       int cchWidth, int cchPrecision, unsigned int fFlags,
                       void *pvUser)
{
    const int error = (int)(intptr_t)pvValue;
    size_t cb = 0;

    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);

    AssertReturn(strcmp(pszType, "sockerr") == 0, 0);

    /*
     * XXX: Windows strerror() doesn't handle posix error codes, but
     * since winsock uses its own, it shouldn't be much of a problem.
     * If you see a strange error message, it's probably from
     * FormatMessage() for an error from <WinError.h> that has the
     * same numeric value.
     */
    if (error < _sys_nerr) {
        char buf[128] = "";
        int status;

        status = strerror_s(buf, sizeof(buf), error);
        if (status == 0) {
            if (strcmp(buf, "Unknown error") == 0) {
                /* windows strerror() doesn't add the numeric value */
                cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                                  "Unknown error: %d", error);
            }
            else {
                cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                                  "%s", buf);
            }
        }
        else {
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                              "Unknown error: %d", error);
        }
    }
    else {
        DWORD nchars;
        char *msg = NULL;

        nchars = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
                                | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                NULL, error, LANG_NEUTRAL,
                                (LPSTR)&msg, 0,
                                NULL);
        if (nchars == 0 || msg == NULL) {
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                              "Unknown error: %d", error);
        }
        else {
            /* FormatMessage() "helpfully" adds newline; get rid of it */
            char *crpos = strchr(msg, '\r');
            if (crpos != NULL) {
                *crpos = '\0';
            }

            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, NULL,
                              "%s", msg);
        }

        if (msg != NULL) {
            LocalFree(msg);
        }
    }

    return cb;
}
#endif /* RT_OS_WINDOWS */


/**
 * Send static callback message from poll manager thread to lwip
 * thread, scheduling a function call in lwip thread context.
 *
 * XXX: Existing lwip api only provides non-blocking version for this.
 * It may fail when lwip thread is not running (mbox invalid) or if
 * post failed (mbox full).  How to handle these?
 */
void
proxy_lwip_post(struct tcpip_msg *msg)
{
    struct tcpip_callback_msg *m;
    err_t error;

    LWIP_ASSERT1(msg != NULL);

    /*
     * lwip plays games with fake incomplete struct tag to enforce API
     */
    m = (struct tcpip_callback_msg *)msg;
    error = tcpip_callbackmsg(m);

    if (error == ERR_VAL) {
        /* XXX: lwip thread is not running (mbox invalid) */
        LWIP_ASSERT1(error != ERR_VAL);
    }

    LWIP_ASSERT1(error == ERR_OK);
}


/**
 * Create a non-blocking socket.  Disable SIGPIPE for TCP sockets if
 * possible.  On Linux it's not possible and should be disabled for
 * each send(2) individually.
 */
static SOCKET
proxy_create_socket(int sdom, int stype)
{
    SOCKET s;
    int stype_and_flags;
    int status;

    LWIP_UNUSED_ARG(status);    /* depends on ifdefs */


    stype_and_flags = stype;

#if defined(SOCK_NONBLOCK)
    stype_and_flags |= SOCK_NONBLOCK;
#endif

    /*
     * Disable SIGPIPE on disconnected socket.  It might be easier to
     * forgo it and just use MSG_NOSIGNAL on each send*(2), since we
     * have to do it for Linux anyway, but Darwin does NOT have that
     * flag (but has SO_NOSIGPIPE socket option).
     */
#if !defined(SOCK_NOSIGPIPE) && !defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
#if 0 /* XXX: Solaris has neither, the program should ignore SIGPIPE globally */
#error Need a way to disable SIGPIPE on connection oriented sockets!
#endif
#endif

#if defined(SOCK_NOSIGPIPE)
    if (stype == SOCK_STREAM) {
        stype_and_flags |= SOCK_NOSIGPIPE;
    }
#endif

    s = socket(sdom, stype_and_flags, 0);
    if (s == INVALID_SOCKET) {
        DPRINTF(("socket: %R[sockerr]\n", SOCKERRNO()));
        return INVALID_SOCKET;
    }

#if defined(RT_OS_WINDOWS)
    {
        u_long mode = 1;
        status = ioctlsocket(s, FIONBIO, &mode);
        if (status == SOCKET_ERROR) {
            DPRINTF(("FIONBIO: %R[sockerr]\n", SOCKERRNO()));
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
#elif !defined(SOCK_NONBLOCK)
    {
        int sflags;

        sflags = fcntl(s, F_GETFL, 0);
        if (sflags < 0) {
            DPRINTF(("F_GETFL: %R[sockerr]\n", SOCKERRNO()));
            closesocket(s);
            return INVALID_SOCKET;
        }

        status = fcntl(s, F_SETFL, sflags | O_NONBLOCK);
        if (status < 0) {
            DPRINTF(("O_NONBLOCK: %R[sockerr]\n", SOCKERRNO()));
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
#endif

#if !defined(SOCK_NOSIGPIPE) && defined(SO_NOSIGPIPE)
    if (stype == SOCK_STREAM) {
        int on = 1;
        const socklen_t onlen = sizeof(on);

        status = setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, onlen);
        if (status < 0) {
            DPRINTF(("SO_NOSIGPIPE: %R[sockerr]\n", SOCKERRNO()));
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
#endif

    /*
     * Disable the Nagle algorithm. Otherwise the host may hold back
     * packets that the guest wants to go out, causing potentially
     * horrible performance. The guest is already applying the Nagle
     * algorithm (or not) the way it wants.
     */
    if (stype == SOCK_STREAM) {
        int on = 1;
        const socklen_t onlen = sizeof(on);

        status = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&on, onlen);
        if (status < 0) {
            DPRINTF(("TCP_NODELAY: %R[sockerr]\n", SOCKERRNO()));
        }
    }

#if defined(RT_OS_WINDOWS)
    /*
     * lwIP only holds one packet of "refused data" for us.  Proxy
     * relies on OS socket send buffer and doesn't do its own
     * buffering.  Unfortunately on Windows send buffer is very small
     * (8K by default) and is not dynamically adpated by the OS it
     * seems.  So a single large write will fill it up and that will
     * make lwIP drop segments, causing guest TCP into pathologic
     * resend patterns.  As a quick and dirty fix just bump it up.
     */
    if (stype == SOCK_STREAM) {
        int sndbuf;
        socklen_t optlen = sizeof(sndbuf);

        status = getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, &optlen);
        if (status == 0) {
            if (sndbuf < 64 * 1024) {
                sndbuf = 64 * 1024;
                status = setsockopt(s, SOL_SOCKET, SO_SNDBUF,
                                    (char *)&sndbuf, optlen);
                if (status != 0) {
                    DPRINTF(("SO_SNDBUF: setsockopt: %R[sockerr]\n", SOCKERRNO()));
                }
            }
        }
        else {
            DPRINTF(("SO_SNDBUF: getsockopt: %R[sockerr]\n", SOCKERRNO()));
        }
    }
#endif

    return s;
}


#ifdef RT_OS_LINUX
/**
 * Fixup a socket returned by accept(2).
 *
 * On Linux a socket returned by accept(2) does NOT inherit the socket
 * options from the listening socket!  We need to repeat parts of the
 * song and dance we did above to make it non-blocking.
 */
int
proxy_fixup_accepted_socket(SOCKET s)
{
    int sflags;
    int status;

    sflags = fcntl(s, F_GETFL, 0);
    if (sflags < 0) {
        DPRINTF(("F_GETFL: %R[sockerr]\n", SOCKERRNO()));
        return -1;
    }

    status = fcntl(s, F_SETFL, sflags | O_NONBLOCK);
    if (status < 0) {
        DPRINTF(("O_NONBLOCK: %R[sockerr]\n", SOCKERRNO()));
        return -1;
    }

    return 0;
}
#endif  /* RT_OS_LINUX */


/**
 * Create a socket for outbound connection to dst_addr:dst_port.
 *
 * The socket is non-blocking and TCP sockets has SIGPIPE disabled if
 * possible.  On Linux it's not possible and should be disabled for
 * each send(2) individually.
 */
SOCKET
proxy_connected_socket(int sdom, int stype,
                       ipX_addr_t *dst_addr, u16_t dst_port)
{
    struct sockaddr_in6 dst_sin6;
    struct sockaddr_in dst_sin;
    struct sockaddr *pdst_sa;
    socklen_t dst_sa_len;
    const struct sockaddr *psrc_sa;
    socklen_t src_sa_len;
    int status;
    int sockerr;
    SOCKET s;

    LWIP_ASSERT1(sdom == PF_INET || sdom == PF_INET6);
    LWIP_ASSERT1(stype == SOCK_STREAM || stype == SOCK_DGRAM);

    DPRINTF(("---> %s ", stype == SOCK_STREAM ? "TCP" : "UDP"));
    if (sdom == PF_INET6) {
        pdst_sa = (struct sockaddr *)&dst_sin6;

        memset(&dst_sin6, 0, sizeof(dst_sin6));
#if HAVE_SA_LEN
        dst_sin6.sin6_len =
#endif
            dst_sa_len = sizeof(dst_sin6);
        dst_sin6.sin6_family = AF_INET6;
        memcpy(&dst_sin6.sin6_addr, &dst_addr->ip6, sizeof(ip6_addr_t));
        dst_sin6.sin6_port = htons(dst_port);

        DPRINTF(("[%RTnaipv6]:%d ", &dst_sin6.sin6_addr, dst_port));
    }
    else { /* sdom = PF_INET */
        pdst_sa = (struct sockaddr *)&dst_sin;

        memset(&dst_sin, 0, sizeof(dst_sin));
#if HAVE_SA_LEN
        dst_sin.sin_len =
#endif
            dst_sa_len = sizeof(dst_sin);
        dst_sin.sin_family = AF_INET;
        dst_sin.sin_addr.s_addr = dst_addr->ip4.addr; /* byte-order? */
        dst_sin.sin_port = htons(dst_port);

        DPRINTF(("%RTnaipv4:%d ", dst_sin.sin_addr.s_addr, dst_port));
    }

    s = proxy_create_socket(sdom, stype);
    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    DPRINTF(("socket %d\n", s));

    /** @todo needs locking if dynamic modifyvm is allowed */
    if (sdom == PF_INET6) {
        psrc_sa = (const struct sockaddr *)g_proxy_options->src6;
        src_sa_len = sizeof(struct sockaddr_in6);
    }
    else {
        psrc_sa = (const struct sockaddr *)g_proxy_options->src4;
        src_sa_len = sizeof(struct sockaddr_in);
    }
    if (psrc_sa != NULL) {
        status = bind(s, psrc_sa, src_sa_len);
        if (status == SOCKET_ERROR) {
            sockerr = SOCKERRNO();
            DPRINTF(("socket %d: bind: %R[sockerr]\n", s, sockerr));
            closesocket(s);
            SET_SOCKERRNO(sockerr);
            return INVALID_SOCKET;
        }
    }

    status = connect(s, pdst_sa, dst_sa_len);
    if (status == SOCKET_ERROR
#if !defined(RT_OS_WINDOWS)
        && SOCKERRNO() != EINPROGRESS
#else
        && SOCKERRNO() != EWOULDBLOCK
#endif
        )
    {
        sockerr = SOCKERRNO();
        DPRINTF(("socket %d: connect: %R[sockerr]\n", s, sockerr));
        closesocket(s);
        SET_SOCKERRNO(sockerr);
        return INVALID_SOCKET;
    }

    return s;
}


/**
 * Create a socket for inbound (port-forwarded) connections to
 * src_addr (port is part of sockaddr, so not a separate argument).
 *
 * The socket is non-blocking and TCP sockets has SIGPIPE disabled if
 * possible.  On Linux it's not possible and should be disabled for
 * each send(2) individually.
 *
 * TODO?: Support v6-mapped v4 so that user can specify she wants
 * "udp" and get both versions?
 */
SOCKET
proxy_bound_socket(int sdom, int stype, struct sockaddr *src_addr)
{
    SOCKET s;
    int on;
    const socklen_t onlen = sizeof(on);
    int status;
    int sockerr;

    s = proxy_create_socket(sdom, stype);
    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    DPRINTF(("socket %d\n", s));

    on = 1;
    status = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, onlen);
    if (status < 0) {           /* not good, but not fatal */
        DPRINTF(("SO_REUSEADDR: %R[sockerr]\n", SOCKERRNO()));
    }

    status = bind(s, src_addr,
                  sdom == PF_INET ?
                    sizeof(struct sockaddr_in)
                  : sizeof(struct sockaddr_in6));
    if (status == SOCKET_ERROR) {
        sockerr = SOCKERRNO();
        DPRINTF(("bind: %R[sockerr]\n", sockerr));
        closesocket(s);
        SET_SOCKERRNO(sockerr);
        return INVALID_SOCKET;
    }

    if (stype == SOCK_STREAM) {
        status = listen(s, 5);
        if (status == SOCKET_ERROR) {
            sockerr = SOCKERRNO();
            DPRINTF(("listen: %R[sockerr]\n", sockerr));
            closesocket(s);
            SET_SOCKERRNO(sockerr);
            return INVALID_SOCKET;
        }
    }

    return s;
}


void
proxy_reset_socket(SOCKET s)
{
    struct linger linger;

    linger.l_onoff = 1;
    linger.l_linger = 0;

    /* On Windows we can run into issue here, perhaps SO_LINGER isn't enough, and
     * we should use WSA{Send,Recv}Disconnect instead.
     *
     * Links for the reference:
     * http://msdn.microsoft.com/en-us/library/windows/desktop/ms738547%28v=vs.85%29.aspx
     * http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=4468997
     */
    setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&linger, sizeof(linger));

    closesocket(s);
}


int
proxy_sendto(SOCKET sock, struct pbuf *p, void *name, size_t namelen)
{
    struct pbuf *q;
    size_t i, clen;
#ifndef RT_OS_WINDOWS
    struct msghdr mh;
    ssize_t nsent;
#else
    DWORD nsent;
#endif
    int rc;
    IOVEC fixiov[8];     /* fixed size (typical case) */
    const size_t fixiovsize = sizeof(fixiov)/sizeof(fixiov[0]);
    IOVEC *dyniov;       /* dynamically sized */
    IOVEC *iov;
    int error = 0;

    /*
     * Static iov[] is usually enough since UDP protocols use small
     * datagrams to avoid fragmentation, but be prepared.
     */
    clen = pbuf_clen(p);
    if (clen > fixiovsize) {
        /*
         * XXX: TODO: check that clen is shorter than IOV_MAX
         */
        dyniov = (IOVEC *)malloc(clen * sizeof(*dyniov));
        if (dyniov == NULL) {
            error = -errno;     /* sic: not a socket error */
            goto out;
        }
        iov = dyniov;
    }
    else {
        dyniov = NULL;
        iov = fixiov;
    }


    for (q = p, i = 0; i < clen; q = q->next, ++i) {
        LWIP_ASSERT1(q != NULL);

        IOVEC_SET_BASE(iov[i], q->payload);
        IOVEC_SET_LEN(iov[i], q->len);
    }

#ifndef RT_OS_WINDOWS
    memset(&mh, 0, sizeof(mh));
    mh.msg_name = name;
    mh.msg_namelen = namelen;
    mh.msg_iov = iov;
    mh.msg_iovlen = clen;

    nsent = sendmsg(sock, &mh, 0);
    rc = (nsent >= 0) ? 0 : SOCKET_ERROR;
#else
    rc = WSASendTo(sock, iov, (DWORD)clen, &nsent, 0,
                   name, (int)namelen, NULL, NULL);
#endif
    if (rc == SOCKET_ERROR) {
        error = SOCKERRNO();
        DPRINTF(("%s: socket %d: sendmsg: %R[sockerr]\n",
                 __func__, sock, error));
        error = -error;
    }

  out:
    if (dyniov != NULL) {
        free(dyniov);
    }
    return error;
}


static const char *lwiperr[] = {
    "ERR_OK",
    "ERR_MEM",
    "ERR_BUF",
    "ERR_TIMEOUT",
    "ERR_RTE",
    "ERR_INPROGRESS",
    "ERR_VAL",
    "ERR_WOULDBLOCK",
    "ERR_USE",
    "ERR_ISCONN",
    "ERR_ABRT",
    "ERR_RST",
    "ERR_CLSD",
    "ERR_CONN",
    "ERR_ARG",
    "ERR_IF"
};


const char *
proxy_lwip_strerr(err_t error)
{
    static char buf[32];
    int e = -error;

    if (0 <= e && e < (int)__arraycount(lwiperr)) {
        return lwiperr[e];
    }
    else {
        RTStrPrintf(buf, sizeof(buf), "unknown error %d", error);
        return buf;
    }
}
