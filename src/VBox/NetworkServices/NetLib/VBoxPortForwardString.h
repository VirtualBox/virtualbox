/* $Id: VBoxPortForwardString.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxPortForwardString
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_NetLib_VBoxPortForwardString_h
#define VBOX_INCLUDED_SRC_NetLib_VBoxPortForwardString_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/net.h>
#include <VBox/intnet.h>

RT_C_DECLS_BEGIN

#define PF_NAMELEN 64
/*
 * TBD: Here is shared implementation of parsing port-forward string
 * of format:
 *      name:[ipv4 or ipv6 address]:host-port:[ipv4 or ipv6 guest addr]:guest port
 *
 * This code supposed to be used in NetService and Frontend and perhaps in corresponding
 * services.
 *
 * Note: ports are in host format.
 */

typedef struct PORTFORWARDRULE
{
    char       szPfrName[PF_NAMELEN];
    /* true if ipv6 and false otherwise */
    int        fPfrIPv6;
    /* IPPROTO_{UDP,TCP} */
    int        iPfrProto;
    char       szPfrHostAddr[INET6_ADDRSTRLEN];
    uint16_t   u16PfrHostPort;
    char       szPfrGuestAddr[INET6_ADDRSTRLEN];
    uint16_t   u16PfrGuestPort;
} PORTFORWARDRULE, *PPORTFORWARDRULE;

int netPfStrToPf(const char *pszStrPortForward, bool fIPv6, PPORTFORWARDRULE pPfr);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_NetLib_VBoxPortForwardString_h */

