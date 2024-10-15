/* $Id: RTWinSocketPair.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - socketpair(2) emulation for winsock.
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

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/errcore.h>

#include <iprt/errcore.h>

#include <iprt/win/winsock2.h>
#include <iprt/win/windows.h>

#include <stdio.h>
#include <iprt/log.h>

extern "C" int RTWinSocketPair(int domain, int type, int protocol, SOCKET socket_vector[2])
{
    LogFlowFunc(("ENTER: domain:%d, type:%d, protocol:%d, socket_vector:%p\n",
                 domain, type, protocol, socket_vector));
    switch (domain)
    {
    case AF_INET:
        break;
    case AF_INET6: /* I dobt we really need it. */
    default:
        AssertMsgFailedReturn(("Unsuported domain:%d\n", domain),
                              VERR_INVALID_PARAMETER);
    }

    switch(type)
    {
    case SOCK_STREAM:
    case SOCK_DGRAM:
        break;
    default:
        AssertMsgFailedReturn(("Unsuported type:%d\n", type),
                              VERR_INVALID_PARAMETER);
    }

    AssertPtrReturn(socket_vector, VERR_INVALID_PARAMETER);
    if (!socket_vector)
      return VERR_INVALID_PARAMETER;

    socket_vector[0] = socket_vector[1] = INVALID_SOCKET;

    SOCKET listener = INVALID_SOCKET;

    union {
        struct sockaddr_in in_addr;
        struct sockaddr addr;
    } sa[2];

    int cb = sizeof(sa);
    memset(&sa, 0, cb);

    sa[0].in_addr.sin_family = domain;
    sa[0].in_addr.sin_addr.s_addr = RT_H2N_U32(INADDR_LOOPBACK);
    sa[0].in_addr.sin_port = 0;
    cb = sizeof(sa[0]);

    if (type == SOCK_STREAM)
    {
        listener = WSASocket(domain, type, protocol, 0, NULL, 0);

        if (listener == INVALID_SOCKET)
        {
            return VERR_INTERNAL_ERROR;
        }

        int reuse = 1;
        cb = sizeof(int);
        int rc = setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, cb);

        if (rc)
        {
            goto close_socket;
        }

        cb = sizeof(sa[0]);
        rc = bind(listener, &sa[0].addr, cb);
        if(rc)
        {
            goto close_socket;
        }

        memset(&sa[0], 0, cb);
        rc = getsockname(listener, &sa[0].addr, &cb);
        if (rc)
        {
            goto close_socket;
        }

        rc = listen(listener, 1);
        if (rc)
        {
            goto close_socket;
        }

        socket_vector[0] = WSASocket(domain, type, protocol, 0, NULL, 0);
        if (socket_vector[0] == INVALID_SOCKET)
        {
            goto close_socket;
        }

        rc = connect(socket_vector[0], &sa[0].addr, cb);
        if (rc)
          goto close_socket;


        socket_vector[1] = accept(listener, NULL, NULL);
        if (socket_vector[1] == INVALID_SOCKET)
        {
            goto close_socket;
        }

        closesocket(listener);
    }
    else
    {
        socket_vector[0] = WSASocket(domain, type, protocol, 0, NULL, 0);

        cb = sizeof(sa[0]);
        int rc = bind(socket_vector[0], &sa[0].addr, cb);
        Assert(rc != SOCKET_ERROR);
        if (rc == SOCKET_ERROR)
        {
            goto close_socket;
        }

        sa[1].in_addr.sin_family = domain;
        sa[1].in_addr.sin_addr.s_addr = RT_H2N_U32(INADDR_LOOPBACK);
        sa[1].in_addr.sin_port = 0;

        socket_vector[1] = WSASocket(domain, type, protocol, 0, NULL, 0);
        rc = bind(socket_vector[1], &sa[1].addr, cb);
        Assert(rc != SOCKET_ERROR);
        if (rc == SOCKET_ERROR)
        {
            goto close_socket;
        }

        {
            u_long mode = 0;
            rc = ioctlsocket(socket_vector[0], FIONBIO, &mode);
            AssertMsgReturn(rc != SOCKET_ERROR,
                            ("ioctl error: %d\n", WSAGetLastError()),
                            VERR_INTERNAL_ERROR);

            rc = ioctlsocket(socket_vector[1], FIONBIO, &mode);
            AssertMsgReturn(rc != SOCKET_ERROR,
                            ("ioctl error: %d\n", WSAGetLastError()),
                            VERR_INTERNAL_ERROR);
        }

        memset(&sa, 0, 2 * cb);
        rc = getsockname(socket_vector[0], &sa[0].addr, &cb);
        Assert(rc != SOCKET_ERROR);
        if (rc == SOCKET_ERROR)
        {
            goto close_socket;
        }

        rc = getsockname(socket_vector[1], &sa[1].addr, &cb);
        Assert(rc != SOCKET_ERROR);
        if (rc == SOCKET_ERROR)
        {
            goto close_socket;
        }

        rc = connect(socket_vector[0], &sa[1].addr, cb);
        Assert(rc != SOCKET_ERROR);
        if (rc == SOCKET_ERROR)
        {
            goto close_socket;
        }

        rc = connect(socket_vector[1], &sa[0].addr, cb);
        Assert(rc != SOCKET_ERROR);
        if (rc == SOCKET_ERROR)
        {
            goto close_socket;
        }
    }

    for (int i = 0; i < 2; ++i) {
        SOCKET s = socket_vector[i];
        u_long mode = 1;

        int status = ioctlsocket(s, FIONBIO, &mode);
        if (status == SOCKET_ERROR) {
            LogRel(("FIONBIO: %R[sockerr]\n", WSAGetLastError()));
        }
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;

close_socket:
    if (listener != INVALID_SOCKET)
      closesocket(listener);

    if (socket_vector[0] != INVALID_SOCKET)
      closesocket(socket_vector[0]);

    if (socket_vector[1] != INVALID_SOCKET)
      closesocket(socket_vector[1]);

    LogFlowFuncLeaveRC(VERR_INTERNAL_ERROR);
    return VERR_INTERNAL_ERROR;
}
