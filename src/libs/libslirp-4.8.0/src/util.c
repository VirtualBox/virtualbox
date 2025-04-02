/* SPDX-License-Identifier: MIT */
/*
 * util.c (mostly based on QEMU os-win32.c)
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010-2016 Red Hat, Inc.
 *
 * QEMU library functions for win32 which are shared between QEMU and
 * the QEMU tools.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "util.h"

#include <glib.h>
#include <fcntl.h>
#include <stdint.h>

#if defined(_WIN32)
int slirp_inet_aton(const char *cp, struct in_addr *ia)
{
    uint32_t addr = inet_addr(cp);
    if (addr == 0xffffffff) {
        return 0;
    }
    ia->s_addr = addr;
    return 1;
}
#endif

void slirp_set_nonblock(int fd)
{
#ifndef _WIN32
    int f;
    f = fcntl(fd, F_GETFL);
    assert(f != -1);
    f = fcntl(fd, F_SETFL, f | O_NONBLOCK);
    assert(f != -1);
#else
    unsigned long opt = 1;
    ioctlsocket(fd, FIONBIO, &opt);
#endif
}

static void slirp_set_cloexec(int fd)
{
#ifndef _WIN32
    int f;
    f = fcntl(fd, F_GETFD);
    assert(f != -1);
    f = fcntl(fd, F_SETFD, f | FD_CLOEXEC);
    assert(f != -1);
#endif
}

/*
 * Opens a socket with FD_CLOEXEC set
 * On failure errno contains the reason.
 */
int slirp_socket(int domain, int type, int protocol)
{
    int ret;

#ifdef SOCK_CLOEXEC
    ret = socket(domain, type | SOCK_CLOEXEC, protocol);
    if (ret != -1 || errno != EINVAL) {
        return ret;
    }
#endif
    ret = socket(domain, type, protocol);
    if (ret >= 0) {
        slirp_set_cloexec(ret);
    }

    return ret;
}

#ifdef _WIN32
static int socket_error(void)
{
    switch (WSAGetLastError()) {
    case 0:
        return 0;
    case WSAEINTR:
        return EINTR;
    case WSAEINVAL:
        return EINVAL;
    case WSA_INVALID_HANDLE:
        return EBADF;
    case WSA_NOT_ENOUGH_MEMORY:
        return ENOMEM;
    case WSA_INVALID_PARAMETER:
        return EINVAL;
    case WSAENAMETOOLONG:
        return ENAMETOOLONG;
    case WSAENOTEMPTY:
        return ENOTEMPTY;
    case WSAEWOULDBLOCK:
        /* not using EWOULDBLOCK as we don't want code to have
         * to check both EWOULDBLOCK and EAGAIN */
        return EAGAIN;
    case WSAEINPROGRESS:
        return EINPROGRESS;
    case WSAEALREADY:
        return EALREADY;
    case WSAENOTSOCK:
        return ENOTSOCK;
    case WSAEDESTADDRREQ:
        return EDESTADDRREQ;
    case WSAEMSGSIZE:
        return EMSGSIZE;
    case WSAEPROTOTYPE:
        return EPROTOTYPE;
    case WSAENOPROTOOPT:
        return ENOPROTOOPT;
    case WSAEPROTONOSUPPORT:
        return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:
        return EOPNOTSUPP;
    case WSAEAFNOSUPPORT:
        return EAFNOSUPPORT;
    case WSAEADDRINUSE:
        return EADDRINUSE;
    case WSAEADDRNOTAVAIL:
        return EADDRNOTAVAIL;
    case WSAENETDOWN:
        return ENETDOWN;
    case WSAENETUNREACH:
        return ENETUNREACH;
    case WSAENETRESET:
        return ENETRESET;
    case WSAECONNABORTED:
        return ECONNABORTED;
    case WSAECONNRESET:
        return ECONNRESET;
    case WSAENOBUFS:
        return ENOBUFS;
    case WSAEISCONN:
        return EISCONN;
    case WSAENOTCONN:
        return ENOTCONN;
    case WSAETIMEDOUT:
        return ETIMEDOUT;
    case WSAECONNREFUSED:
        return ECONNREFUSED;
    case WSAELOOP:
        return ELOOP;
    case WSAEHOSTUNREACH:
        return EHOSTUNREACH;
    default:
        return EIO;
    }
}

#undef ioctlsocket
int slirp_ioctlsocket_wrap(int fd, int req, void *val)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(fd);

    if (!s)
        return -1;

    ret = ioctlsocket(s, req, val);
#else
    ret = ioctlsocket(fd, req, val);
#endif
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef closesocket
int slirp_closesocket_wrap(int fd)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(fd);
    if (!s)
        return -1;

    ret = closesocket(s);

    if (ret == SOCKET_ERROR)
    {
        errno = socket_error();
        LogFunc(("SOCKET CLOSE ERROR: Handle %d, discovered to be SOCKET %d, \
                failed to close with error: %d\n", fd, s, errno));
        return ret;
    }

    int iFreeRc = libslirp_wrap_RTHandleTableFree(fd);
    if (!RT_SUCCESS(iFreeRc))
    {
        Log3Func(("Handle free error: Handle %d which was really %d. Rc=%d\n", \
                 fd, s, iFreeRc));
        return VERR_INVALID_PARAMETER;
    }
#else
    ret = closesocket(fd);
    if (ret < 0) {
        errno = socket_error();
    }
#endif
    return ret;
}

#undef connect
int slirp_connect_wrap(int sockfd, const struct sockaddr *addr, int addrlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);
    LogFlowFunc(("s == %d\n", s));

    if (!s)
        return -1;

    ret = connect(s, addr, addrlen);
#else
    ret = connect(sockfd, addr, addrlen);
#endif
    if (ret < 0) {
#ifdef VBOX
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            errno = EINPROGRESS;
        else
#endif
            errno = socket_error();
    }

    return ret;
}

#undef listen
int slirp_listen_wrap(int sockfd, int backlog)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = listen(s, backlog);
#else
    ret = listen(sockfd, backlog);
#endif
    if (ret < 0) {
        errno = socket_error();
    }

    return ret;
}

#undef bind
int slirp_bind_wrap(int sockfd, const struct sockaddr *addr, int addrlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = bind(s, addr, addrlen);
#else
    ret = bind(sockfd, addr, addrlen);
#endif
    if (ret < 0) {
        errno = socket_error();
    }

    return ret;
}

#undef socket
#ifdef VBOX
uint32_t slirp_socket_wrap(int domain, int type, int protocol)
#else
int slirp_socket_wrap(int domain, int type, int protocol)
#endif
{
#ifdef VBOX
    SOCKET s;
    uint32_t fd;

    s = socket(domain, type, protocol);
    if (s == -1) {
        errno = socket_error();
        return s;
    }

    int rc = libslirp_wrap_RTHandleTableAlloc(s, &fd);
    if (rc != VINF_SUCCESS) {
        closesocket(s);
        errno = ENOMEM;
    }

    return fd;
#else
    int ret;
    ret = socket(domain, type, protocol);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
#endif
}

#undef accept
int slirp_accept_wrap(int sockfd, struct sockaddr *addr, int *addrlen)
{
#ifdef VBOX
    int fd;
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    s = accept(s, addr, addrlen);
    if (s == -1) {
        errno = socket_error();
        return s;
    }

    int rc = libslirp_wrap_RTHandleTableAlloc(s, &fd);
    if (rc != VINF_SUCCESS) {
        closesocket(s);
        errno = ENOMEM;
    }

    return fd;
#else
    int ret;
    ret = accept(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
#endif
}

#undef shutdown
int slirp_shutdown_wrap(int sockfd, int how)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = shutdown(s, how);
#else
    ret = shutdown(sockfd, how);
#endif
    if (ret < 0) {
        errno = socket_error();
    }

    return ret;
}

#undef getsockopt
int slirp_getsockopt_wrap(int sockfd, int level, int optname, void *optval,
                          int *optlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = getsockopt(s, level, optname, optval, optlen);
#else
    ret = getsockopt(sockfd, level, optname, optval, optlen);
#endif
    if (ret < 0) {
        errno = socket_error();
    }

    return ret;
}

#undef setsockopt
int slirp_setsockopt_wrap(int sockfd, int level, int optname,
                          const void *optval, int optlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = setsockopt(s, level, optname, optval, optlen);
#else
    ret = setsockopt(sockfd, level, optname, optval, optlen);
#endif
    if (ret < 0) {
        errno = socket_error();
    }

    return ret;
}

#undef getpeername
int slirp_getpeername_wrap(int sockfd, struct sockaddr *addr, int *addrlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = getpeername(s, addr, addrlen);
#else
    ret = getpeername(sockfd, addr, addrlen);
#endif
    if (ret < 0) {
        errno = socket_error();
    }

    return ret;
}

#undef getsockname
int slirp_getsockname_wrap(int sockfd, struct sockaddr *addr, int *addrlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = getsockname(s, addr, addrlen);
#else
    ret = getsockname(sockfd, addr, addrlen);
#endif
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef send
slirp_ssize_t slirp_send_wrap(int sockfd, const void *buf, size_t len, int flags)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = send(s, buf, len, flags);
#else
    ret = send(sockfd, buf, len, flags);
#endif
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef sendto
slirp_ssize_t slirp_sendto_wrap(int sockfd, const void *buf, size_t len, int flags,
                          const struct sockaddr *addr, int addrlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = sendto(s, buf, len, flags, addr, addrlen);
#else
    ret = sendto(sockfd, buf, len, flags, addr, addrlen);
#endif
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef recv
slirp_ssize_t slirp_recv_wrap(int sockfd, void *buf, size_t len, int flags)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = recv(s, buf, len, flags);
#else
    ret = recv(sockfd, buf, len, flags);
#endif
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef recvfrom
slirp_ssize_t slirp_recvfrom_wrap(int sockfd, void *buf, size_t len, int flags,
                            struct sockaddr *addr, int *addrlen)
{
    int ret;
#ifdef VBOX
    SOCKET s = libslirp_wrap_RTHandleTableLookup(sockfd);

    if (!s)
        return -1;

    ret = recvfrom(s, buf, len, flags, addr, addrlen);
#else
    ret = recvfrom(sockfd, buf, len, flags, addr, addrlen);
#endif
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}
#endif /* WIN32 */

void slirp_pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for (;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

G_GNUC_PRINTF(3, 0)
static int slirp_vsnprintf(char *str, size_t size,
                           const char *format, va_list args)
{
    int rv = g_vsnprintf(str, size, format, args);

    if (rv < 0) {
        g_error("g_vsnprintf() failed: %s", g_strerror(errno));
    }

    return rv;
}

/*
 * A snprintf()-like function that:
 * - returns the number of bytes written (excluding optional \0-ending)
 * - dies on error
 * - warn on truncation
 */
int slirp_fmt(char *str, size_t size, const char *format, ...)
{
    va_list args;
    int rv;

    va_start(args, format);
    rv = slirp_vsnprintf(str, size, format, args);
    va_end(args);

    if (rv >= size) {
        g_critical("slirp_fmt() truncation");
    }

    return MIN(rv, size);
}

/*
 * A snprintf()-like function that:
 * - always \0-end (unless size == 0)
 * - returns the number of bytes actually written, including \0 ending
 * - dies on error
 * - warn on truncation
 */
int slirp_fmt0(char *str, size_t size, const char *format, ...)
{
    va_list args;
    int rv;

    va_start(args, format);
    rv = slirp_vsnprintf(str, size, format, args);
    va_end(args);

    if (rv >= size) {
        g_critical("slirp_fmt0() truncation");
        if (size > 0)
            str[size - 1] = '\0';
        rv = size;
    } else {
        rv += 1; /* include \0 */
    }

    return rv;
}

const char *slirp_ether_ntoa(const uint8_t *addr, char *out_str,
                             size_t out_str_size)
{
    assert(out_str_size >= ETH_ADDRSTRLEN);

    slirp_fmt0(out_str, out_str_size, "%02x:%02x:%02x:%02x:%02x:%02x",
               addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    return out_str;
}
