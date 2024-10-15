/* $Id: NetIf-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - NetIfList, OS/2 implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN

#include <iprt/errcore.h>
#include <list>

#include "HostNetworkInterfaceImpl.h"
#include "netif.h"

int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfEnableStaticIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf, ULONG ip, ULONG mask)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfEnableStaticIpConfigV6(VirtualBox *pVBox, HostNetworkInterface * pIf, const Utf8Str &aIPV6Address, ULONG aIPV6MaskPrefixLength)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfEnableDynamicIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf)
{
    return VERR_NOT_IMPLEMENTED;
}


int NetIfDhcpRediscover(VirtualBox *pVBox, HostNetworkInterface * pIf)
{
    return VERR_NOT_IMPLEMENTED;
}

