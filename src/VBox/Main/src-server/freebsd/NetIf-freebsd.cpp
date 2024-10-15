/* $Id: NetIf-freebsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - NetIfList, FreeBSD implementation.
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

/*
 * Original (C) 2009 Fredrik Lindberg <fli@shapeshifter.se>. Contributed
 * to VirtualBox under the MIT license by the author.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_HOST
#include <sys/types.h>

#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net80211/ieee80211_ioctl.h>

#include <net/route.h>
/*
 * route.h includes net/radix.h which for some reason defines Free as a wrapper
 * around free. This collides with Free defined in xpcom/include/nsIMemory.h
 * Undefine it and hope for the best
 */
#undef Free

#include <net/if_dl.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <list>

#include "HostNetworkInterfaceImpl.h"
#include "netif.h"
#include "LoggingNew.h"

#define ROUNDUP(a) \
    ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

void extractAddresses(int iAddrMask, caddr_t cp, caddr_t cplim, struct sockaddr **pAddresses)
{
    struct sockaddr *sa;

    for (int i = 0; i < RTAX_MAX && cp < cplim; i++) {
        if (!(iAddrMask & (1 << i)))
            continue;

        sa = (struct sockaddr *)cp;

        pAddresses[i] = sa;

        ADVANCE(cp, sa);
    }
}

static int getDefaultIfaceIndex(unsigned short *pu16Index, int family)
{
    size_t cbNeeded;
    char *pBuf, *pNext;
    int aiMib[6];
    struct sockaddr *addresses[RTAX_MAX];
    aiMib[0] = CTL_NET;
    aiMib[1] = PF_ROUTE;
    aiMib[2] = 0;
    aiMib[3] = family;    /* address family */
    aiMib[4] = NET_RT_DUMP;
    aiMib[5] = 0;

    if (sysctl(aiMib, 6, NULL, &cbNeeded, NULL, 0) < 0)
    {
        Log(("getDefaultIfaceIndex: Failed to get estimate for list size (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }
    if ((pBuf = (char*)malloc(cbNeeded)) == NULL)
        return VERR_NO_MEMORY;
    if (sysctl(aiMib, 6, pBuf, &cbNeeded, NULL, 0) < 0)
    {
        free(pBuf);
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
                             pRtMsg->rtm_msglen + (char *)pRtMsg, addresses);
            if ((pRtMsg->rtm_addrs & RTA_DST))
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
                    free(pBuf);
                    return VINF_SUCCESS;
                }
            }
        }
    }
    free(pBuf);
    return VERR_INTERNAL_ERROR;

}

void extractAddressesToNetInfo(int iAddrMask, caddr_t cp, caddr_t cplim, PNETIFINFO pInfo)
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


static bool isWireless(const char *pszName)
{
    bool fWireless = false;
    int iSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (iSock >= 0)
    {
        struct ieee80211req WReq;
        uint8_t abData[32];

        RT_ZERO(WReq);
        strncpy(WReq.i_name, pszName, sizeof(WReq.i_name));
        WReq.i_type = IEEE80211_IOC_SSID;
        WReq.i_val = -1;
        WReq.i_data = abData;
        WReq.i_len = sizeof(abData);

        fWireless = ioctl(iSock, SIOCG80211, &WReq) >= 0;
        close(iSock);
    }

    return fWireless;
}

int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list)
{
    int vrc = VINF_SUCCESS;
    size_t cbNeeded;
    char *pBuf, *pNext;
    int aiMib[6];
    unsigned short u16DefaultIface = 0; /* shut up gcc. */
    bool fDefaultIfaceExistent = true;

    /* Get the index of the interface associated with default route. */
    vrc = getDefaultIfaceIndex(&u16DefaultIface, PF_INET);
    if (RT_FAILURE(vrc))
    {
        fDefaultIfaceExistent = false;
        vrc = VINF_SUCCESS;
    }

    aiMib[0] = CTL_NET;
    aiMib[1] = PF_ROUTE;
    aiMib[2] = 0;
    aiMib[3] = 0;    /* address family */
    aiMib[4] = NET_RT_IFLIST;
    aiMib[5] = 0;

    if (sysctl(aiMib, 6, NULL, &cbNeeded, NULL, 0) < 0)
    {
        Log(("NetIfList: Failed to get estimate for list size (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }
    if ((pBuf = (char*)malloc(cbNeeded)) == NULL)
        return VERR_NO_MEMORY;
    if (sysctl(aiMib, 6, pBuf, &cbNeeded, NULL, 0) < 0)
    {
        free(pBuf);
        Log(("NetIfList: Failed to retrieve interface table (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }

    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        free(pBuf);
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

        size_t cbNameLen = pSdl->sdl_nlen + 1;
        PNETIFINFO pNew = (PNETIFINFO)RTMemAllocZ(RT_UOFFSETOF_DYN(NETIFINFO, szName[cbNameLen]));
        if (!pNew)
        {
            vrc = VERR_NO_MEMORY;
            break;
        }
        memcpy(pNew->MACAddress.au8, LLADDR(pSdl), sizeof(pNew->MACAddress.au8));
        pNew->enmMediumType = NETIF_T_ETHERNET;
        Assert(sizeof(pNew->szShortName) >= cbNameLen);
        strlcpy(pNew->szShortName, pSdl->sdl_data, cbNameLen);
        strlcpy(pNew->szName, pSdl->sdl_data, cbNameLen);
        /* Generate UUID from name and MAC address. */
        RTUUID uuid;
        RTUuidClear(&uuid);
        memcpy(&uuid, pNew->szShortName, RT_MIN(cbNameLen, sizeof(uuid)));
        uuid.Gen.u8ClockSeqHiAndReserved = (uuid.Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
        uuid.Gen.u16TimeHiAndVersion = (uuid.Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
        memcpy(uuid.Gen.au8Node, pNew->MACAddress.au8, sizeof(uuid.Gen.au8Node));
        pNew->Uuid = uuid;

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

        if (pSdl->sdl_type == IFT_ETHER || pSdl->sdl_type == IFT_L2VLAN)
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

            pNew->fWireless = isWireless(pNew->szName);

            ComObjPtr<HostNetworkInterface> IfObj;
            IfObj.createObject();
            if (SUCCEEDED(IfObj->init(Bstr(pNew->szName), enmType, pNew)))
            {
                /* Make sure the default interface gets to the beginning. */
                if (   fDefaultIfaceExistent
                    && pIfMsg->ifm_index == u16DefaultIface)
                    list.push_front(IfObj);
                else
                    list.push_back(IfObj);
            }
        }
        RTMemFree(pNew);
    }

    close(sock);
    free(pBuf);
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
    aiMib[3] = 0;    /* address family */
    aiMib[4] = NET_RT_IFLIST;
    aiMib[5] = 0;

    if (sysctl(aiMib, 6, NULL, &cbNeeded, NULL, 0) < 0)
    {
        Log(("NetIfList: Failed to get estimate for list size (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }
    if ((pBuf = (char*)malloc(cbNeeded)) == NULL)
        return VERR_NO_MEMORY;
    if (sysctl(aiMib, 6, pBuf, &cbNeeded, NULL, 0) < 0)
    {
        free(pBuf);
        Log(("NetIfList: Failed to retrieve interface table (errno=%d).\n", errno));
        return RTErrConvertFromErrno(errno);
    }

    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        free(pBuf);
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

        bool fSkip = !!strcmp(pInfo->szShortName, pSdl->sdl_data);

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

        if (!fSkip && (pSdl->sdl_type == IFT_ETHER || pSdl->sdl_type == IFT_L2VLAN))
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
    free(pBuf);
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
int NetIfGetLinkSpeed(const char * /*pcszIfName*/, uint32_t * /*puMbits*/)
{
    return VERR_NOT_IMPLEMENTED;
}
