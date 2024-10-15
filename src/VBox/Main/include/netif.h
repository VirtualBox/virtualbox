/* $Id: netif.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - Network Interfaces.
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

#ifndef MAIN_INCLUDED_netif_h
#define MAIN_INCLUDED_netif_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/net.h>
/** @todo r=bird: The inlined code below that drags in asm.h here. I doubt
 *        speed is very important here, so move it into a .cpp file, please. */
#include <iprt/asm.h>

#ifndef RT_OS_WINDOWS
# include <arpa/inet.h>
# include <stdio.h>
#endif /* !RT_OS_WINDOWS */

#ifdef RT_OS_LINUX
# include <linux/version.h>
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0) /* proc_ns introduced */
#  define VBOXNETFLT_LINUX_NAMESPACE_SUPPORT
# endif
#endif

#define VBOXNET_IPV4ADDR_DEFAULT      0x0138A8C0  /* 192.168.56.1 */
#define VBOXNET_IPV4MASK_DEFAULT      "255.255.255.0"

#define VBOXNET_MAX_SHORT_NAME 50

#if 1
/**
 * Encapsulation type.
 * @note Must match HostNetworkInterfaceMediumType_T exactly.
 * @todo r=bird: Why are we duplicating HostNetworkInterfaceMediumType_T here?!?
 */
typedef enum NETIFTYPE
{
    NETIF_T_UNKNOWN,
    NETIF_T_ETHERNET,
    NETIF_T_PPP,
    NETIF_T_SLIP
} NETIFTYPE;

/**
 * Current state of the interface.
 * @note Must match HostNetworkInterfaceStatus_T exactly.
 * @todo r=bird: Why are we duplicating HostNetworkInterfaceStatus_T here?!?
 */
typedef enum NETIFSTATUS
{
    NETIF_S_UNKNOWN,
    NETIF_S_UP,
    NETIF_S_DOWN
} NETIFSTATUS;

/**
 * Host Network Interface Information.
 */
typedef struct NETIFINFO
{
    NETIFINFO     *pNext;
    RTNETADDRIPV4  IPAddress;
    RTNETADDRIPV4  IPNetMask;
    RTNETADDRIPV6  IPv6Address;
    RTNETADDRIPV6  IPv6NetMask;
    BOOL           fDhcpEnabled;
    BOOL           fIsDefault;
    BOOL           fWireless;
    RTMAC          MACAddress;
    NETIFTYPE      enmMediumType;
    NETIFSTATUS    enmStatus;
    uint32_t       uSpeedMbits;
    RTUUID         Uuid;
    char           szShortName[VBOXNET_MAX_SHORT_NAME];
    char           szName[1];
} NETIFINFO;

/** Pointer to a network interface info. */
typedef NETIFINFO *PNETIFINFO;
/** Pointer to a const network interface info. */
typedef NETIFINFO const *PCNETIFINFO;
#endif

int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list);
int NetIfEnableStaticIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf, ULONG aOldIp, ULONG aNewIp, ULONG aMask);
int NetIfEnableStaticIpConfigV6(VirtualBox *pVBox, HostNetworkInterface *pIf, const Utf8Str &aOldIPV6Address, const Utf8Str &aIPV6Address, ULONG aIPV6MaskPrefixLength);
int NetIfEnableDynamicIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf);
#ifdef RT_OS_WINDOWS
int NetIfCreateHostOnlyNetworkInterface(VirtualBox *pVBox, IHostNetworkInterface **aHostNetworkInterface, IProgress **aProgress,
                                        IN_BSTR bstrName = NULL);
#else
int NetIfCreateHostOnlyNetworkInterface(VirtualBox *pVBox, IHostNetworkInterface **aHostNetworkInterface, IProgress **aProgress,
                                        const char *pszName = NULL);
#endif
int NetIfRemoveHostOnlyNetworkInterface(VirtualBox *pVBox, const Guid &aId, IProgress **aProgress);
int NetIfGetConfig(HostNetworkInterface * pIf, NETIFINFO *);
int NetIfGetConfigByName(PNETIFINFO pInfo);
int NetIfGetState(const char *pcszIfName, NETIFSTATUS *penmState);
int NetIfGetLinkSpeed(const char *pcszIfName, uint32_t *puMbits);
int NetIfDhcpRediscover(VirtualBox *pVBox, HostNetworkInterface * pIf);
int NetIfAdpCtlOut(const char *pszName, const char *pszCmd, char *pszBuffer, size_t cBufSize);

DECLINLINE(Bstr) getDefaultIPv4Address(Bstr bstrIfName)
{
    /* Get the index from the name */
    Utf8Str strTmp = bstrIfName;
    const char *pcszIfName = strTmp.c_str();
    size_t iPos = strcspn(pcszIfName, "0123456789");
    uint32_t uInstance = 0;
    if (pcszIfName[iPos])
        uInstance = RTStrToUInt32(pcszIfName + iPos);

    in_addr tmp;
#if defined(RT_OS_WINDOWS)
    tmp.S_un.S_addr = VBOXNET_IPV4ADDR_DEFAULT + (uInstance << 16);
#else
    tmp.s_addr = VBOXNET_IPV4ADDR_DEFAULT + (uInstance << 16);
#endif
    char *addr = inet_ntoa(tmp);
    return Bstr(addr);
}

#endif /* !MAIN_INCLUDED_netif_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
