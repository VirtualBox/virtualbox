/* $Id: NetIf-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - NetIfList, Darwin implementation.
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
#define LOG_GROUP LOG_GROUP_MAIN_HOST

/*
 * Deal with conflicts first.
 * PVM - BSD mess, that FreeBSD has correct a long time ago.
 * iprt/types.h before sys/param.h - prevents UINT32_C and friends.
 */
#include <iprt/types.h>
#include <sys/param.h>
#undef PVM

#include <iprt/errcore.h>
#include <iprt/alloc.h>

#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <ifaddrs.h>
#include <errno.h>
#include <unistd.h>
#include <list>

#include "HostNetworkInterfaceImpl.h"
#include "netif.h"
#include "iokit.h"
#include "LoggingNew.h"

#if 0
int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list)
{
    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        Log(("NetIfList: socket() -> %d\n", errno));
        return NULL;
    }
    struct ifaddrs *IfAddrs, *pAddr;
    int iRc = getifaddrs(&IfAddrs);
    if (iRc)
    {
        close(sock);
        Log(("NetIfList: getifaddrs() -> %d\n", iRc));
        return VERR_INTERNAL_ERROR;
    }

    PDARWINETHERNIC pEtherNICs = DarwinGetEthernetControllers();
    while (pEtherNICs)
    {
        size_t cbNameLen = strlen(pEtherNICs->szName) + 1;
        PNETIFINFO pNew = (PNETIFINFO)RTMemAllocZ(RT_OFFSETOF(NETIFINFO, szName[cbNameLen]));
        pNew->MACAddress = pEtherNICs->Mac;
        pNew->enmMediumType = NETIF_T_ETHERNET;
        pNew->Uuid = pEtherNICs->Uuid;
        Assert(sizeof(pNew->szShortName) > sizeof(pEtherNICs->szBSDName));
        memcpy(pNew->szShortName, pEtherNICs->szBSDName, sizeof(pEtherNICs->szBSDName));
        pNew->szShortName[sizeof(pEtherNICs->szBSDName)] = '\0';
        memcpy(pNew->szName, pEtherNICs->szName, cbNameLen);

        struct ifreq IfReq;
        RTStrCopy(IfReq.ifr_name, sizeof(IfReq.ifr_name), pNew->szShortName);
        if (ioctl(sock, SIOCGIFFLAGS, &IfReq) < 0)
        {
            Log(("NetIfList: ioctl(SIOCGIFFLAGS) -> %d\n", errno));
            pNew->enmStatus = NETIF_S_UNKNOWN;
        }
        else
            pNew->enmStatus = (IfReq.ifr_flags & IFF_UP) ? NETIF_S_UP : NETIF_S_DOWN;

        for (pAddr = IfAddrs; pAddr != NULL; pAddr = pAddr->ifa_next)
        {
            if (strcmp(pNew->szShortName, pAddr->ifa_name))
                continue;

            struct sockaddr_in *pIPAddr, *pIPNetMask;
            struct sockaddr_in6 *pIPv6Addr, *pIPv6NetMask;

            switch (pAddr->ifa_addr->sa_family)
            {
                case AF_INET:
                    if (pNew->IPAddress.u)
                        break;
                    pIPAddr = (struct sockaddr_in *)pAddr->ifa_addr;
                    Assert(sizeof(pNew->IPAddress) == sizeof(pIPAddr->sin_addr));
                    pNew->IPAddress.u = pIPAddr->sin_addr.s_addr;
                    pIPNetMask = (struct sockaddr_in *)pAddr->ifa_netmask;
                    Assert(pIPNetMask->sin_family == AF_INET);
                    Assert(sizeof(pNew->IPNetMask) == sizeof(pIPNetMask->sin_addr));
                    pNew->IPNetMask.u = pIPNetMask->sin_addr.s_addr;
                    break;
                case AF_INET6:
                    if (pNew->IPv6Address.s.Lo || pNew->IPv6Address.s.Hi)
                        break;
                    pIPv6Addr = (struct sockaddr_in6 *)pAddr->ifa_addr;
                    Assert(sizeof(pNew->IPv6Address) == sizeof(pIPv6Addr->sin6_addr));
                    memcpy(pNew->IPv6Address.au8,
                           pIPv6Addr->sin6_addr.__u6_addr.__u6_addr8,
                           sizeof(pNew->IPv6Address));
                    pIPv6NetMask = (struct sockaddr_in6 *)pAddr->ifa_netmask;
                    Assert(pIPv6NetMask->sin6_family == AF_INET6);
                    Assert(sizeof(pNew->IPv6NetMask) == sizeof(pIPv6NetMask->sin6_addr));
                    memcpy(pNew->IPv6NetMask.au8,
                           pIPv6NetMask->sin6_addr.__u6_addr.__u6_addr8,
                           sizeof(pNew->IPv6NetMask));
                    break;
            }
        }

        ComObjPtr<HostNetworkInterface> IfObj;
        IfObj.createObject();
        if (SUCCEEDED(IfObj->init(Bstr(pEtherNICs->szName), HostNetworkInterfaceType_Bridged, pNew)))
            list.push_back(IfObj);
        RTMemFree(pNew);

        /* next, free current */
        void *pvFree = pEtherNICs;
        pEtherNICs = pEtherNICs->pNext;
        RTMemFree(pvFree);
    }

    freeifaddrs(IfAddrs);
    close(sock);
    return VINF_SUCCESS;
}
#else

#define ROUNDUP(a) \
    (((a) & (sizeof(u_long) - 1)) ? (1 + ((a) | (sizeof(u_long) - 1))) : (a))
#define ADVANCE(x, n) (x += (n)->sa_len ? ROUNDUP((n)->sa_len) : sizeof(u_long))

static void extractAddresses(int iAddrMask, caddr_t cp, caddr_t cplim, struct sockaddr **pAddresses)
{
    struct sockaddr *sa;

    for (int i = 0; i < RTAX_MAX && cp < cplim; i++) {
        if (iAddrMask & (1 << i))
        {
            sa = (struct sockaddr *)cp;

            pAddresses[i] = sa;

            ADVANCE(cp, sa);
        }
        else
            pAddresses[i] = NULL;
    }
}

static void extractAddressesToNetInfo(int iAddrMask, caddr_t cp, caddr_t cplim, PNETIFINFO pInfo)
{
    struct sockaddr *addresses[RTAX_MAX];

    extractAddresses(iAddrMask, cp, cplim, addresses);
    switch (addresses[RTAX_IFA]->sa_family)
    {
        case AF_INET:
            if (!pInfo->IPAddress.u)
            {
                pInfo->IPAddress.u = ((struct sockaddr_in *)addresses[RTAX_IFA])->sin_addr.s_addr;
                pInfo->IPNetMask.u = ((struct sockaddr_in *)addresses[RTAX_NETMASK])->sin_addr.s_addr;
            }
            break;
        case AF_INET6:
            if (!pInfo->IPv6Address.s.Lo && !pInfo->IPv6Address.s.Hi)
            {
                memcpy(pInfo->IPv6Address.au8,
                       ((struct sockaddr_in6 *)addresses[RTAX_IFA])->sin6_addr.__u6_addr.__u6_addr8,
                       sizeof(pInfo->IPv6Address));
                memcpy(pInfo->IPv6NetMask.au8,
                       ((struct sockaddr_in6 *)addresses[RTAX_NETMASK])->sin6_addr.__u6_addr.__u6_addr8,
                       sizeof(pInfo->IPv6NetMask));
            }
            break;
        default:
            Log(("NetIfList: Unsupported address family: %u\n", addresses[RTAX_IFA]->sa_family));
            break;
    }
}

static int getDefaultIfaceIndex(unsigned short *pu16Index)
{
    size_t cbNeeded;
    char *pBuf, *pNext;
    int aiMib[6];
    struct sockaddr *addresses[RTAX_MAX];

    aiMib[0] = CTL_NET;
    aiMib[1] = PF_ROUTE;
    aiMib[2] = 0;
    aiMib[3] = PF_INET; /* address family */
    aiMib[4] = NET_RT_DUMP;
    aiMib[5] = 0;

    if (sysctl(aiMib, 6, NULL, &cbNeeded, NULL, 0) < 0)
    {
        Log(("getDefaultIfaceIndex: Failed to get estimate for list size (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }
    if ((pBuf = (char *)RTMemAlloc(cbNeeded)) == NULL)
        return VERR_NO_MEMORY;
    if (sysctl(aiMib, 6, pBuf, &cbNeeded, NULL, 0) < 0)
    {
        RTMemFree(pBuf);
        Log(("getDefaultIfaceIndex: Failed to retrieve interface table (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }

    char *pEnd = pBuf + cbNeeded;
    struct rt_msghdr *pRtMsg;
    for (pNext = pBuf; pNext < pEnd; pNext += pRtMsg->rtm_msglen)
    {
        pRtMsg = (struct rt_msghdr *)pNext;

        if (pRtMsg->rtm_type != RTM_GET)
        {
            Log(("getDefaultIfaceIndex: Got message %u while expecting %u.\n",
                 pRtMsg->rtm_type, RTM_GET));
            //vrc = VERR_INTERNAL_ERROR;
            continue;
        }
        if ((char*)(pRtMsg + 1) < pEnd)
        {
            /* Extract addresses from the message. */
            extractAddresses(pRtMsg->rtm_addrs, (char *)(pRtMsg + 1),
                             pRtMsg->rtm_msglen + 1 + (char *)pRtMsg, addresses);
            if ((pRtMsg->rtm_addrs & RTA_DST)
                && (pRtMsg->rtm_addrs & RTA_NETMASK))
            {
                if (addresses[RTAX_DST]->sa_family != AF_INET)
                    continue;
                struct sockaddr_in *addr = (struct sockaddr_in *)addresses[RTAX_DST];
                struct sockaddr_in *mask = (struct sockaddr_in *)addresses[RTAX_NETMASK];
                if ((addr->sin_addr.s_addr == INADDR_ANY) &&
                    mask &&
                    (ntohl(mask->sin_addr.s_addr) == 0L ||
                     mask->sin_len == 0))
                {
                    *pu16Index = pRtMsg->rtm_index;
                    RTMemFree(pBuf);
                    return VINF_SUCCESS;
                }
            }
        }
    }
    RTMemFree(pBuf);
    return 0; /* Failed to find default interface, take the first one in the list. */
}

int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list)
{
    int vrc = VINF_SUCCESS;
    size_t cbNeeded;
    char *pBuf, *pNext;
    int aiMib[6];
    unsigned short u16DefaultIface = 0; /* initialized to shut up gcc */

    /* Get the index of the interface associated with default route. */
    vrc = getDefaultIfaceIndex(&u16DefaultIface);
    if (RT_FAILURE(vrc))
        return vrc;

    aiMib[0] = CTL_NET;
    aiMib[1] = PF_ROUTE;
    aiMib[2] = 0;
    aiMib[3] = 0;       /* address family */
    aiMib[4] = NET_RT_IFLIST;
    aiMib[5] = 0;

    if (sysctl(aiMib, 6, NULL, &cbNeeded, NULL, 0) < 0)
    {
        Log(("NetIfList: Failed to get estimate for list size (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }
    if ((pBuf = (char*)RTMemAlloc(cbNeeded)) == NULL)
        return VERR_NO_MEMORY;
    if (sysctl(aiMib, 6, pBuf, &cbNeeded, NULL, 0) < 0)
    {
        RTMemFree(pBuf);
        Log(("NetIfList: Failed to retrieve interface table (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }

    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        RTMemFree(pBuf);
        Log(("NetIfList: socket() -> %d\n", errno));
        return RTErrConvertFromErrno(errno);
    }

    PDARWINETHERNIC pNIC;
    PDARWINETHERNIC pEtherNICs = DarwinGetEthernetControllers();

    char *pEnd = pBuf + cbNeeded;
    for (pNext = pBuf; pNext < pEnd;)
    {
        struct if_msghdr *pIfMsg = (struct if_msghdr *)pNext;

        if (pIfMsg->ifm_type != RTM_IFINFO)
        {
            Log(("NetIfList: Got message %u while expecting %u.\n",
                 pIfMsg->ifm_type, RTM_IFINFO));
            vrc = VERR_INTERNAL_ERROR;
            break;
        }
        struct sockaddr_dl *pSdl = (struct sockaddr_dl *)(pIfMsg + 1);

        size_t cbNameLen = pSdl->sdl_nlen + 1;
        Assert(pSdl->sdl_nlen < sizeof(pNIC->szBSDName));
        for (pNIC = pEtherNICs; pNIC; pNIC = pNIC->pNext)
            if (   !strncmp(pSdl->sdl_data, pNIC->szBSDName, pSdl->sdl_nlen)
                && pNIC->szBSDName[pSdl->sdl_nlen] == '\0')
            {
                cbNameLen = strlen(pNIC->szName) + 1;
                break;
            }
        PNETIFINFO pNew = (PNETIFINFO)RTMemAllocZ(RT_UOFFSETOF_DYN(NETIFINFO, szName[cbNameLen]));
        if (!pNew)
        {
            vrc = VERR_NO_MEMORY;
            break;
        }
        memcpy(pNew->MACAddress.au8, LLADDR(pSdl), sizeof(pNew->MACAddress.au8));
        pNew->enmMediumType = NETIF_T_ETHERNET;
        Assert(sizeof(pNew->szShortName) > pSdl->sdl_nlen);
        memcpy(pNew->szShortName, pSdl->sdl_data, RT_MIN(pSdl->sdl_nlen, sizeof(pNew->szShortName) - 1));

        /*
         * If we found the adapter in the list returned by
         * DarwinGetEthernetControllers() copy the name and UUID from there.
         */
        if (pNIC)
        {
            memcpy(pNew->szName, pNIC->szName, cbNameLen);
            pNew->Uuid = pNIC->Uuid;
            pNew->fWireless = pNIC->fWireless;
        }
        else
        {
            memcpy(pNew->szName, pSdl->sdl_data, pSdl->sdl_nlen);
            /* Generate UUID from name and MAC address. */
            RTUUID uuid;
            RTUuidClear(&uuid);
            memcpy(&uuid, pNew->szShortName, RT_MIN(cbNameLen, sizeof(uuid)));
            uuid.Gen.u8ClockSeqHiAndReserved = (uuid.Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
            uuid.Gen.u16TimeHiAndVersion = (uuid.Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
            memcpy(uuid.Gen.au8Node, pNew->MACAddress.au8, sizeof(uuid.Gen.au8Node));
            pNew->Uuid = uuid;
        }

        pNext += pIfMsg->ifm_msglen;
        while (pNext < pEnd)
        {
            struct ifa_msghdr *pIfAddrMsg = (struct ifa_msghdr *)pNext;

            if (pIfAddrMsg->ifam_type != RTM_NEWADDR)
                break;
            extractAddressesToNetInfo(pIfAddrMsg->ifam_addrs,
                                      (char *)(pIfAddrMsg + 1),
                                      pIfAddrMsg->ifam_msglen + (char *)pIfAddrMsg,
                                      pNew);
            pNext += pIfAddrMsg->ifam_msglen;
        }

        if (pSdl->sdl_type == IFT_ETHER)
        {
            struct ifreq IfReq;
            RTStrCopy(IfReq.ifr_name, sizeof(IfReq.ifr_name), pNew->szShortName);
            if (ioctl(sock, SIOCGIFFLAGS, &IfReq) < 0)
            {
                Log(("NetIfList: ioctl(SIOCGIFFLAGS) -> %d\n", errno));
                pNew->enmStatus = NETIF_S_UNKNOWN;
            }
            else
                pNew->enmStatus = (IfReq.ifr_flags & IFF_UP) ? NETIF_S_UP : NETIF_S_DOWN;

            HostNetworkInterfaceType_T enmType;
            if (strncmp(pNew->szName, RT_STR_TUPLE("vboxnet")))
                enmType = HostNetworkInterfaceType_Bridged;
            else
                enmType = HostNetworkInterfaceType_HostOnly;

            ComObjPtr<HostNetworkInterface> IfObj;
            IfObj.createObject();
            if (SUCCEEDED(IfObj->init(Bstr(pNew->szName), enmType, pNew)))
            {
                /* Make sure the default interface gets to the beginning. */
                if (pIfMsg->ifm_index == u16DefaultIface)
                    list.push_front(IfObj);
                else
                    list.push_back(IfObj);
            }
        }
        RTMemFree(pNew);
    }
    for (pNIC = pEtherNICs; pNIC;)
    {
        void *pvFree = pNIC;
        pNIC = pNIC->pNext;
        RTMemFree(pvFree);
    }
    close(sock);
    RTMemFree(pBuf);
    return vrc;
}

int NetIfGetConfigByName(PNETIFINFO pInfo)
{
    int vrc = VINF_SUCCESS;
    size_t cbNeeded;
    char *pBuf, *pNext;
    int aiMib[6];

    aiMib[0] = CTL_NET;
    aiMib[1] = PF_ROUTE;
    aiMib[2] = 0;
    aiMib[3] = 0;       /* address family */
    aiMib[4] = NET_RT_IFLIST;
    aiMib[5] = 0;

    if (sysctl(aiMib, 6, NULL, &cbNeeded, NULL, 0) < 0)
    {
        Log(("NetIfList: Failed to get estimate for list size (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }
    if ((pBuf = (char*)RTMemAlloc(cbNeeded)) == NULL)
        return VERR_NO_MEMORY;
    if (sysctl(aiMib, 6, pBuf, &cbNeeded, NULL, 0) < 0)
    {
        RTMemFree(pBuf);
        Log(("NetIfList: Failed to retrieve interface table (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }

    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        RTMemFree(pBuf);
        Log(("NetIfList: socket() -> %d\n", errno));
        return RTErrConvertFromErrno(errno);
    }

    char *pEnd = pBuf + cbNeeded;
    for (pNext = pBuf; pNext < pEnd;)
    {
        struct if_msghdr *pIfMsg = (struct if_msghdr *)pNext;

        if (pIfMsg->ifm_type != RTM_IFINFO)
        {
            Log(("NetIfList: Got message %u while expecting %u.\n",
                 pIfMsg->ifm_type, RTM_IFINFO));
            vrc = VERR_INTERNAL_ERROR;
            break;
        }
        struct sockaddr_dl *pSdl = (struct sockaddr_dl *)(pIfMsg + 1);

        bool fSkip = !!strncmp(pInfo->szShortName, pSdl->sdl_data, pSdl->sdl_nlen)
            || pInfo->szShortName[pSdl->sdl_nlen] != '\0';

        pNext += pIfMsg->ifm_msglen;
        while (pNext < pEnd)
        {
            struct ifa_msghdr *pIfAddrMsg = (struct ifa_msghdr *)pNext;

            if (pIfAddrMsg->ifam_type != RTM_NEWADDR)
                break;
            if (!fSkip)
                extractAddressesToNetInfo(pIfAddrMsg->ifam_addrs,
                                          (char *)(pIfAddrMsg + 1),
                                          pIfAddrMsg->ifam_msglen + (char *)pIfAddrMsg,
                                          pInfo);
            pNext += pIfAddrMsg->ifam_msglen;
        }

        if (!fSkip && pSdl->sdl_type == IFT_ETHER)
        {
            size_t cbNameLen = pSdl->sdl_nlen + 1;
            memcpy(pInfo->MACAddress.au8, LLADDR(pSdl), sizeof(pInfo->MACAddress.au8));
            pInfo->enmMediumType = NETIF_T_ETHERNET;
            /* Generate UUID from name and MAC address. */
            RTUUID uuid;
            RTUuidClear(&uuid);
            memcpy(&uuid, pInfo->szShortName, RT_MIN(cbNameLen, sizeof(uuid)));
            uuid.Gen.u8ClockSeqHiAndReserved = (uuid.Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
            uuid.Gen.u16TimeHiAndVersion = (uuid.Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
            memcpy(uuid.Gen.au8Node, pInfo->MACAddress.au8, sizeof(uuid.Gen.au8Node));
            pInfo->Uuid = uuid;

            struct ifreq IfReq;
            RTStrCopy(IfReq.ifr_name, sizeof(IfReq.ifr_name), pInfo->szShortName);
            if (ioctl(sock, SIOCGIFFLAGS, &IfReq) < 0)
            {
                Log(("NetIfList: ioctl(SIOCGIFFLAGS) -> %d\n", errno));
                pInfo->enmStatus = NETIF_S_UNKNOWN;
            }
            else
                pInfo->enmStatus = (IfReq.ifr_flags & IFF_UP) ? NETIF_S_UP : NETIF_S_DOWN;

            return VINF_SUCCESS;
        }
    }
    close(sock);
    RTMemFree(pBuf);
    return vrc;
}

/**
 * Retrieve the physical link speed in megabits per second. If the interface is
 * not up or otherwise unavailable the zero speed is returned.
 *
 * @returns VBox status code.
 *
 * @param   pcszIfName  Interface name.
 * @param   puMbits     Where to store the link speed.
 */
int NetIfGetLinkSpeed(const char *pcszIfName, uint32_t *puMbits)
{
    RT_NOREF(pcszIfName, puMbits);
    return VERR_NOT_IMPLEMENTED;
}
#endif
