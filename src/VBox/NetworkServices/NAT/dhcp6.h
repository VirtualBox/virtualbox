/* $Id: dhcp6.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - DHCPv6 protocol definitions.
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

#ifndef VBOX_INCLUDED_SRC_NAT_dhcp6_h
#define VBOX_INCLUDED_SRC_NAT_dhcp6_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* UDP ports */
#define DHCP6_CLIENT_PORT       546
#define DHCP6_SERVER_PORT       547

/* Message types */
#define DHCP6_REPLY                     7
#define DHCP6_INFORMATION_REQUEST       11
#define DHCP6_RELAY_FORW                12
#define DHCP6_RELAY_REPLY               13

/* DUID types */
#define DHCP6_DUID_LLT                  1
#define DHCP6_DUID_EN                   2
#define DHCP6_DUID_LL                   3

/* Hardware type for DUID-LLT and DUID-LL */
#define ARES_HRD_ETHERNET               1 /* RFC 826*/

/* Options */
#define DHCP6_OPTION_CLIENTID           1
#define DHCP6_OPTION_SERVERID           2
#define DHCP6_OPTION_ORO                6
#define DHCP6_OPTION_ELAPSED_TIME       8
#define DHCP6_OPTION_STATUS_CODE        13
#define DHCP6_OPTION_DNS_SERVERS        23 /* RFC 3646 */
#define DHCP6_OPTION_DOMAIN_LIST        24 /* RFC 3646 */

#endif /* !VBOX_INCLUDED_SRC_NAT_dhcp6_h */
