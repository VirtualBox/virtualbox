/* $Id: winutils.h 106686 2024-10-24 23:01:45Z jack.doherty@oracle.com $ */
/** @file
 * NAT Network - winsock compatibility shim.
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

#ifndef VBOX_INCLUDED_SRC_Network_winutils_h
#define VBOX_INCLUDED_SRC_Network_winutils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

# include <iprt/cdefs.h>

# ifdef RT_OS_WINDOWS
#  include <iprt/win/winsock2.h>
#  include <iprt/win/ws2tcpip.h>
#  include <mswsock.h>
#  include <iprt/win/windows.h>
#  include <iprt/err.h>
#  include <iprt/net.h>
#  include <iprt/log.h>
/**
 * Inclusion of lwip/def.h was added here to avoid conflict of definitions
 * of hton-family functions in LWIP and windock's headers.
 */
#  include <lwip/def.h>

#  ifndef PF_LOCAL
#   define PF_LOCAL AF_INET
#  endif

#  ifdef DEBUG
#   define err(code,...) do { \
      AssertMsgFailed((__VA_ARGS__));           \
    }while(0)
#else
#   define err(code,...) do { \
      DPRINTF0((__VA_ARGS__));    \
      ExitProcess(code); \
    }while(0)
#endif
#  define errx err
#  define __func__ __FUNCTION__
#  define __attribute__(x) /* IGNORE */

#  define SOCKERRNO()          (WSAGetLastError())
#  define SET_SOCKERRNO(error) do { WSASetLastError(error); } while (0)

/**
 * "Windows Sockets Error Codes" obtained with WSAGetLastError().
 * http://msdn.microsoft.com/en-us/library/windows/desktop/ms740668(v=vs.85).aspx
 *
 * This block of error codes from <winsock2.h> conflicts with "POSIX
 * supplement" error codes from <errno.h>, but we don't expect to ever
 * encounter the latter in the proxy code, so redefine them to their
 * unixy names.
 */
#  undef  EWOULDBLOCK
#  define EWOULDBLOCK           WSAEWOULDBLOCK
#  undef  EINPROGRESS
#  define EINPROGRESS           WSAEINPROGRESS
#  undef  EALREADY
#  define EALREADY              WSAEALREADY
#  undef  ENOTSOCK
#  define ENOTSOCK              WSAENOTSOCK
#  undef  EDESTADDRREQ
#  define EDESTADDRREQ          WSAEDESTADDRREQ
#  undef  EMSGSIZE
#  define EMSGSIZE              WSAEMSGSIZE
#  undef  EPROTOTYPE
#  define EPROTOTYPE            WSAEPROTOTYPE
#  undef  ENOPROTOOPT
#  define ENOPROTOOPT           WSAENOPROTOOPT
#  undef  EPROTONOSUPPORT
#  define EPROTONOSUPPORT       WSAEPROTONOSUPPORT
#  undef  ESOCKTNOSUPPORT
#  define ESOCKTNOSUPPORT       WSAESOCKTNOSUPPORT
#  undef  EOPNOTSUPP
#  define EOPNOTSUPP            WSAEOPNOTSUPP
#  undef  EPFNOSUPPORT
#  define EPFNOSUPPORT          WSAEPFNOSUPPORT
#  undef  EAFNOSUPPORT
#  define EAFNOSUPPORT          WSAEAFNOSUPPORT
#  undef  EADDRINUSE
#  define EADDRINUSE            WSAEADDRINUSE
#  undef  EADDRNOTAVAIL
#  define EADDRNOTAVAIL         WSAEADDRNOTAVAIL
#  undef  ENETDOWN
#  define ENETDOWN              WSAENETDOWN
#  undef  ENETUNREACH
#  define ENETUNREACH           WSAENETUNREACH
#  undef  ENETRESET
#  define ENETRESET             WSAENETRESET
#  undef  ECONNABORTED
#  define ECONNABORTED          WSAECONNABORTED
#  undef  ECONNRESET
#  define ECONNRESET            WSAECONNRESET
#  undef  ENOBUFS
#  define ENOBUFS               WSAENOBUFS
#  undef  EISCONN
#  define EISCONN               WSAEISCONN
#  undef  ENOTCONN
#  define ENOTCONN              WSAENOTCONN
#  undef  ESHUTDOWN
#  define ESHUTDOWN             WSAESHUTDOWN
#  undef  ETOOMANYREFS
#  define ETOOMANYREFS          WSAETOOMANYREFS
#  undef  ETIMEDOUT
#  define ETIMEDOUT             WSAETIMEDOUT
#  undef  ECONNREFUSED
#  define ECONNREFUSED          WSAECONNREFUSED
#  undef  ELOOP
#  define ELOOP                 WSAELOOP
#  undef  ENAMETOOLONG
#  define ENAMETOOLONG          WSAENAMETOOLONG
#  undef  EHOSTDOWN
#  define EHOSTDOWN             WSAEHOSTDOWN
#  undef  EHOSTUNREACH
#  define EHOSTUNREACH          WSAEHOSTUNREACH

/**
 * parameters to shutdown (2) with Winsock2
 * http://msdn.microsoft.com/en-us/library/windows/desktop/ms740481(v=vs.85).aspx
 */
#  define SHUT_RD SD_RECEIVE
#  define SHUT_WR SD_SEND
#  define SHUT_RDWR SD_BOTH

typedef ULONG nfds_t;

typedef WSABUF IOVEC;

#  define IOVEC_GET_BASE(iov) ((iov).buf)
#  define IOVEC_SET_BASE(iov, b) ((iov).buf = (b))

#  define IOVEC_GET_LEN(iov) ((iov).len)
#  define IOVEC_SET_LEN(iov, l) ((iov).len = (ULONG)(l))

#if _WIN32_WINNT < 0x0600
/* otherwise defined the other way around in ws2def.h */
#define cmsghdr _WSACMSGHDR

#undef CMSG_DATA       /* wincrypt.h can byte my shiny metal #undef */
#define CMSG_DATA WSA_CMSG_DATA
#define CMSG_LEN WSA_CMSG_LEN
#define CMSG_SPACE WSA_CMSG_SPACE

#define CMSG_FIRSTHDR WSA_CMSG_FIRSTHDR
#define CMSG_NXTHDR WSA_CMSG_NXTHDR
#endif  /* _WIN32_WINNT < 0x0600 - provide unglified CMSG names */

RT_C_DECLS_BEGIN
int RTWinSocketPair(int domain, int type, int protocol, SOCKET socket_vector[2]);
RT_C_DECLS_END

# else /* !RT_OS_WINDOWS */

#  include <errno.h>
#  include <unistd.h>

#  define SOCKET int
#  define INVALID_SOCKET (-1)
#  define SOCKET_ERROR (-1)

#  define SOCKERRNO()          (errno)
#  define SET_SOCKERRNO(error) do { errno = (error); } while (0)

#  define closesocket(s) close(s)
#  define ioctlsocket(s, req, arg) ioctl((s), (req), (arg))

typedef struct iovec IOVEC;

#  define IOVEC_GET_BASE(iov) ((iov).iov_base)
#  define IOVEC_SET_BASE(iov, b) ((iov).iov_base = (b))

#  define IOVEC_GET_LEN(iov) ((iov).iov_len)
#  define IOVEC_SET_LEN(iov, l) ((iov).iov_len = (l))
# endif

DECLINLINE(int)
proxy_error_is_transient(int error)
{
# if !defined(RT_OS_WINDOWS)
    return error == EWOULDBLOCK
#  if EAGAIN != EWOULDBLOCK
        || error == EAGAIN
#  endif
        || error == EINTR
        || error == ENOBUFS
        || error == ENOMEM;
# else
    return error == WSAEWOULDBLOCK
        || error == WSAEINTR    /* NB: we don't redefine EINTR above */
        || error == WSAENOBUFS;
# endif
}

#endif /* !VBOX_INCLUDED_SRC_Network_winutils_h */
