/* $Id: DhcpdInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - Internal header.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_DhcpdInternal_h
#define VBOX_INCLUDED_SRC_Dhcpd_DhcpdInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef IN_VBOXSVC
# define LOG_GROUP LOG_GROUP_NET_DHCPD
#elif !defined(LOG_GROUP)
# define LOG_GROUP LOG_GROUP_MAIN_DHCPCONFIG
#endif
#include <iprt/stdint.h>
#include <iprt/string.h>
#include <VBox/log.h>

#include <map>
#include <vector>

#ifndef IN_VBOXSVC

# if __cplusplus >= 199711
#include <memory>
using std::shared_ptr;
# else
#  include <tr1/memory>
using std::tr1::shared_ptr;
# endif

class DhcpOption;
/** DHCP option map (keyed by option number, DhcpOption value). */
typedef std::map<uint8_t, std::shared_ptr<DhcpOption> > optmap_t;

#endif /* !IN_VBOXSVC */

/** Byte vector. */
typedef std::vector<uint8_t> octets_t;

/** Raw DHCP option map (keyed by option number, byte vector value). */
typedef std::map<uint8_t, octets_t> rawopts_t;


/** Equal compare operator for mac address. */
DECLINLINE(bool) operator==(const RTMAC &l, const RTMAC &r)
{
    return memcmp(&l, &r, sizeof(RTMAC)) == 0;
}

/** Less-than compare operator for mac address. */
DECLINLINE(bool) operator<(const RTMAC &l, const RTMAC &r)
{
    return memcmp(&l, &r, sizeof(RTMAC)) < 0;
}


/** @name LogXRel + return NULL helpers
 * @{ */
#define DHCP_LOG_RET_NULL(a_MsgArgs)        do { LogRel(a_MsgArgs);     return NULL; } while (0)
#define DHCP_LOG2_RET_NULL(a_MsgArgs)       do { LogRel2(a_MsgArgs);    return NULL; } while (0)
#define DHCP_LOG3_RET_NULL(a_MsgArgs)       do { LogRel3(a_MsgArgs);    return NULL; } while (0)
/** @} */


/** @name LogXRel + return a_rcRet helpers
 * @{ */
#define DHCP_LOG_RET(a_rcRet, a_MsgArgs)    do { LogRel(a_MsgArgs);     return (a_rcRet); } while (0)
#define DHCP_LOG2_RET(a_rcRet, a_MsgArgs)   do { LogRel2(a_MsgArgs);    return (a_rcRet); } while (0)
#define DHCP_LOG3_RET(a_rcRet, a_MsgArgs)   do { LogRel3(a_MsgArgs);    return (a_rcRet); } while (0)
/** @} */

/** LogRel + RTMsgError helper. */
#define DHCP_LOG_MSG_ERROR(a_MsgArgs)       do { LogRel(a_MsgArgs);     RTMsgError a_MsgArgs; } while (0)

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_DhcpdInternal_h */
