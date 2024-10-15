/* $Id: RTWinPollLibslirp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - poll(2) implementation for winsock.
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
#define LOG_GROUP LOG_GROUP_DRV_NAT

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/errcore.h>
#include <iprt/string.h>

#include <iprt/errcore.h>
#include <VBox/log.h>

#include <iprt/win/winsock2.h>
#include <iprt/win/windows.h>
#include "winpoll.h"

static HANDLE g_hNetworkEvent;

int RTWinPoll(struct pollfd *pFds, unsigned int nfds, int timeout, int *pNready, HANDLE phControl)
{
    AssertPtrReturn(pFds, VERR_INVALID_PARAMETER);
    HANDLE pHandleArr[] = {phControl, g_hNetworkEvent};

    if (g_hNetworkEvent == WSA_INVALID_EVENT)
    {
        g_hNetworkEvent = WSACreateEvent();
        AssertReturn(g_hNetworkEvent != WSA_INVALID_EVENT, VERR_INTERNAL_ERROR);
    }

    for (unsigned int i = 0; i < nfds; ++i)
    {
        long eventMask = 0;
        short pollEvents = pFds[i].events;

        /* clean revents */
        pFds[i].revents = 0;

        /* ignore invalid sockets */
        if (pFds[i].fd == INVALID_SOCKET)
          continue;

        /*
         * POLLIN         Data other than high priority data may be read without blocking.
         * This is equivalent to ( POLLRDNORM | POLLRDBAND ).
         * POLLRDBAND     Priority data may be read without blocking.
         * POLLRDNORM     Normal data may be read without blocking.
         */
        if (pollEvents & POLLIN)
            eventMask |= FD_READ | FD_ACCEPT;

        /*
         * POLLOUT        Normal data may be written without blocking.  This is equivalent
         *  to POLLWRNORM.
         * POLLWRNORM     Normal data may be written without blocking.
         */
        if (pollEvents & POLLOUT)
            eventMask |= FD_WRITE | FD_CONNECT;

        /*
         * This is "moral" equivalent to POLLHUP.
         */
        eventMask |= FD_CLOSE;
        WSAEventSelect(pFds[i].fd, g_hNetworkEvent, eventMask);
    }

    DWORD index = WSAWaitForMultipleEvents(2,
                                           pHandleArr,
                                           FALSE,
                                           timeout == RT_INDEFINITE_WAIT ? WSA_INFINITE : timeout,
                                           FALSE);
    if (index != WSA_WAIT_EVENT_0)
    {
        if (index == WSA_WAIT_TIMEOUT)
            return VERR_TIMEOUT;
    }

    int nready = 0;
    for (unsigned int i = 0; i < nfds; ++i)
    {
        short revents = 0;
        WSANETWORKEVENTS NetworkEvents;
        int err;

        if (pFds[i].fd == INVALID_SOCKET)
            continue;

        RT_ZERO(NetworkEvents);

        err = WSAEnumNetworkEvents(pFds[i].fd,
                                   g_hNetworkEvent,
                                   &NetworkEvents);

        if (err == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAENOTSOCK)
            {
                pFds[i].revents = POLLNVAL;
                ++nready;
            }
            continue;
        }

        /* deassociate socket with event */
        WSAEventSelect(pFds[i].fd, g_hNetworkEvent, 0);

#define WSA_TO_POLL(_wsaev, _pollev)                                    \
        do {                                                            \
            if (NetworkEvents.lNetworkEvents & (_wsaev)) {              \
                revents |= (_pollev);                                   \
                if (NetworkEvents.iErrorCode[_wsaev##_BIT] != 0) {      \
                    Log2(("sock %d: %s: %R[sockerr]\n",                 \
                          pFds[i].fd, #_wsaev,                          \
                          NetworkEvents.iErrorCode[_wsaev##_BIT]));     \
                    revents |= POLLERR;                                 \
                }                                                       \
            }                                                           \
        } while (0)

        WSA_TO_POLL(FD_READ,    POLLIN);
        WSA_TO_POLL(FD_ACCEPT,  POLLIN);
        WSA_TO_POLL(FD_WRITE,   POLLOUT);
        WSA_TO_POLL(FD_CONNECT, POLLOUT);
        WSA_TO_POLL(FD_CLOSE,   POLLHUP | (pFds[i].events & POLLIN));

        Assert((revents & ~(pFds[i].events | POLLHUP | POLLERR)) == 0);

        if (revents != 0)
        {
            pFds[i].revents = revents;
            ++nready;
        }
    }
    WSAResetEvent(g_hNetworkEvent);

    if (pNready)
      *pNready = nready;

    return VINF_SUCCESS;
}
