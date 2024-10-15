/* $Id: socket.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal Header for RTSocket.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_INTERNAL_socket_h
#define IPRT_INCLUDED_INTERNAL_socket_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/net.h>
/* Currently requires a bunch of socket headers. */


/** Native socket handle type. */
#ifdef RT_OS_WINDOWS
# define RTSOCKETNATIVE         SOCKET
#else
# define RTSOCKETNATIVE         int
#endif

/** NIL value for native socket handles. */
#ifdef RT_OS_WINDOWS
# define NIL_RTSOCKETNATIVE     INVALID_SOCKET
#else
# define NIL_RTSOCKETNATIVE     (-1)
#endif


RT_C_DECLS_BEGIN

#ifndef IPRT_INTERNAL_SOCKET_POLLING_ONLY
DECLHIDDEN(int) rtSocketResolverError(void);
DECLHIDDEN(int) rtSocketCreateForNative(RTSOCKETINT **ppSocket, RTSOCKETNATIVE hNative, bool fLeaveOpen);
DECLHIDDEN(int) rtSocketCreate(PRTSOCKET phSocket, int iDomain, int iType, int iProtocol, bool fInheritable);
DECLHIDDEN(int) rtSocketCreateTcpPair(RTSOCKET *phServer, RTSOCKET *phClient);
DECLHIDDEN(int) rtSocketBind(RTSOCKET hSocket, PCRTNETADDR pAddr);
DECLHIDDEN(int) rtSocketBindRawAddr(RTSOCKET hSocket, void const *pvAddr, size_t cbAddr);
DECLHIDDEN(int) rtSocketListen(RTSOCKET hSocket, int cMaxPending);
DECLHIDDEN(int) rtSocketAccept(RTSOCKET hSocket, PRTSOCKET phClient, struct sockaddr *pAddr, size_t *pcbAddr);
DECLHIDDEN(int) rtSocketConnect(RTSOCKET hSocket, PCRTNETADDR pAddr, RTMSINTERVAL cMillies);
DECLHIDDEN(int) rtSocketConnectRaw(RTSOCKET hSocket, void const *pvAddr, size_t cbAddr);
DECLHIDDEN(int) rtSocketSetOpt(RTSOCKET hSocket, int iLevel, int iOption, void const *pvValue, int cbValue);
#endif /* IPRT_INTERNAL_SOCKET_POLLING_ONLY */

DECLHIDDEN(int)         rtSocketPollGetHandle(RTSOCKET hSocket, uint32_t fEvents, PRTHCINTPTR phNative);
DECLHIDDEN(int)         rtSocketSetKeepAlive(RTSOCKET hSocket, bool fEnable, uint32_t cSecsIdle, uint32_t cSecsInterval);
DECLHIDDEN(uint32_t)    rtSocketPollStart(RTSOCKET hSocket, RTPOLLSET hPollSet, uint32_t fEvents, bool fFinalEntry, bool fNoWait);
DECLHIDDEN(uint32_t)    rtSocketPollDone(RTSOCKET hSocket, uint32_t fEvents, bool fFinalEntry, bool fHarvestEvents);

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_socket_h */

