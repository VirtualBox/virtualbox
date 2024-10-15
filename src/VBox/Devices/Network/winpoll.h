/* $Id: winpoll.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - poll(2) for winsock, definitions and declarations.
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

#ifndef VBOX_INCLUDED_SRC_Network_winpoll_h
#define VBOX_INCLUDED_SRC_Network_winpoll_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif
# include <iprt/cdefs.h>
/**
 * WinSock2 has definition for POLL* and pollfd, but it defined for _WIN32_WINNT > 0x0600
 * and used in WSAPoll, which has very unclear history.
 */
# if(_WIN32_WINNT < 0x0600)
#  define POLLRDNORM  0x0100
#  define POLLRDBAND  0x0200
#  define POLLIN      (POLLRDNORM | POLLRDBAND)
#  define POLLPRI     0x0400

#  define POLLWRNORM  0x0010
#  define POLLOUT     (POLLWRNORM)
#  define POLLWRBAND  0x0020

#  define POLLERR     0x0001
#  define POLLHUP     0x0002
#  define POLLNVAL    0x0004

struct pollfd {

    SOCKET  fd;
    SHORT   events;
    SHORT   revents;

};
#endif
RT_C_DECLS_BEGIN
int RTWinPoll(struct pollfd *pFds, unsigned int nfds, int timeout, int *pNready, HANDLE phControl);
RT_C_DECLS_END
#endif /* !VBOX_INCLUDED_SRC_Network_winpoll_h */
