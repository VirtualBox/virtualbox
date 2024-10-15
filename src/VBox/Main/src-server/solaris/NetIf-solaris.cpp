/* $Id: NetIf-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - NetIfList, Solaris implementation.
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

#include <iprt/errcore.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <list>

#include "LoggingNew.h"
#include "HostNetworkInterfaceImpl.h"
#include "netif.h"

#ifdef VBOX_WITH_HOSTNETIF_API

#include <map>
#include <iprt/sanitized/string>
#include <fcntl.h>
#include <unistd.h>
#include <stropts.h>
#include <limits.h>
#include <stdio.h>
#include <libdevinfo.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <sys/types.h>
#include <kstat.h>

#include "DynLoadLibSolaris.h"

/** @todo Unify this define with VBoxNetFltBow-solaris.c */
#define VBOXBOW_VNIC_TEMPLATE_NAME      "vboxvnic_template"


static uint32_t getInstance(const char *pszIfaceName, char *pszDevName)
{
    /*
     * Get the instance number from the interface name, then clip it off.
     */
    int cbInstance = 0;
    size_t cbIface = strlen(pszIfaceName);
    const char *pszEnd = pszIfaceName + cbIface - 1;
    for (size_t i = 0; i < cbIface - 1; i++)
    {
        if (!RT_C_IS_DIGIT(*pszEnd))
            break;
        cbInstance++;
        pszEnd--;
    }

    uint32_t uInstance = RTStrToUInt32(pszEnd + 1);
    strncpy(pszDevName, pszIfaceName, cbIface - cbInstance);
    pszDevName[cbIface - cbInstance] = '\0';
    return uInstance;
}

static uint32_t kstatGet(const char *name)
{
    kstat_ctl_t *kc;
    uint32_t uSpeed = 0;

    if ((kc = kstat_open()) == 0)
    {
        LogRel(("kstat_open() -> %d\n", errno));
        return 0;
    }

    kstat_t *ksAdapter = kstat_lookup(kc, (char *)"link", -1, (char *)name);
    if (ksAdapter == 0)
    {
        char szModule[KSTAT_STRLEN];
        uint32_t uInstance = getInstance(name, szModule);
        ksAdapter = kstat_lookup(kc, szModule, uInstance, (char *)"phys");
        if (ksAdapter == 0)
            ksAdapter = kstat_lookup(kc, szModule, uInstance, (char*)name);
    }
    if (ksAdapter == 0)
        LogRel(("Failed to get network statistics for %s\n", name));
    else if (kstat_read(kc, ksAdapter, 0) == -1)
        LogRel(("kstat_read(%s) -> %d\n", name, errno));
    else
    {
        kstat_named_t *kn;
        if ((kn = (kstat_named_t *)kstat_data_lookup(ksAdapter, (char *)"ifspeed")) != NULL)
            uSpeed = (uint32_t)(kn->value.ul / 1000000); /* bits -> Mbits */
        else
            LogRel(("kstat_data_lookup(ifspeed) -> %d, name=%s\n", errno, name));
    }
    kstat_close(kc);
    LogFlow(("kstatGet(%s) -> %u Mbit/s\n", name, uSpeed));
    return uSpeed;
}

static void queryIfaceSpeed(PNETIFINFO pInfo)
{
    /* Don't query interface speed for inactive interfaces (see @bugref{6345}). */
    if (pInfo->enmStatus == NETIF_S_UP)
        pInfo->uSpeedMbits = kstatGet(pInfo->szShortName);
    else
        pInfo->uSpeedMbits = 0;
    LogFlow(("queryIfaceSpeed(%s) -> %u\n", pInfo->szShortName, pInfo->uSpeedMbits));
}

static void vboxSolarisAddHostIface(char *pszIface, int Instance, void *pvHostNetworkInterfaceList)
{
    std::list<ComObjPtr<HostNetworkInterface> > *pList =
        (std::list<ComObjPtr<HostNetworkInterface> > *)pvHostNetworkInterfaceList;
    Assert(pList);

    typedef std::map <std::string, std::string> NICMap;
    typedef std::pair <std::string, std::string> NICPair;
    static NICMap SolarisNICMap;
    if (SolarisNICMap.empty())
    {
        SolarisNICMap.insert(NICPair("afe", "ADMtek Centaur/Comet Fast Ethernet"));
        SolarisNICMap.insert(NICPair("atge", "Atheros/Attansic Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("aggr", "Link Aggregation Interface"));
        SolarisNICMap.insert(NICPair("bfe", "Broadcom BCM4401 Fast Ethernet"));
        SolarisNICMap.insert(NICPair("bge", "Broadcom BCM57xx Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("bnx", "Broadcom NetXtreme Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("bnxe", "Broadcom NetXtreme II 10 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("ce", "Cassini Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("chxge", "Chelsio Ethernet"));
        SolarisNICMap.insert(NICPair("dmfe", "Davicom 9102 Fast Ethernet"));
        SolarisNICMap.insert(NICPair("dnet", "DEC 21040/41 21140 Ethernet"));
        SolarisNICMap.insert(NICPair("e1000", "Intel PRO/1000 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("e1000g", "Intel PRO/1000 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("elx", "3COM Etherlink III Ethernet"));
        SolarisNICMap.insert(NICPair("elxl", "3COM Etherlink XL Ethernet"));
        SolarisNICMap.insert(NICPair("eri", "eri Fast Ethernet"));
        SolarisNICMap.insert(NICPair("ge", "GEM Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("hme", "SUNW,hme Fast-Ethernet"));
        SolarisNICMap.insert(NICPair("hxge", "Sun Blade 10 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("igb", "Intel 82575 PCI-E Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("ipge", "PCI-E Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("iprb", "Intel 82557/58/59 Ethernet"));
        SolarisNICMap.insert(NICPair("ixgb", "Intel 82597ex 10 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("ixgbe", "Intel 10 Gigabit PCI-E Ethernet"));
        SolarisNICMap.insert(NICPair("mcxe", "Mellanox ConnectX-2 10 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("mxfe", "Macronix 98715 Fast Ethernet"));
        SolarisNICMap.insert(NICPair("nfo", "Nvidia Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("nge", "Nvidia Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("ntxn", "NetXen 10/1 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("nxge", "Sun 10/1 Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("pcelx", "3COM EtherLink III PCMCIA Ethernet"));
        SolarisNICMap.insert(NICPair("pcn", "AMD PCnet Ethernet"));
        SolarisNICMap.insert(NICPair("qfe", "SUNW,qfe Quad Fast-Ethernet"));
        SolarisNICMap.insert(NICPair("rge", "Realtek Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("rtls", "Realtek 8139 Fast Ethernet"));
        SolarisNICMap.insert(NICPair("sfe", "SiS900 Fast Ethernet"));
        SolarisNICMap.insert(NICPair("skge", "SksKonnect Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("spwr", "SMC EtherPower II 10/100 (9432) Ethernet"));
        SolarisNICMap.insert(NICPair("vboxnet", "VirtualBox Host Ethernet"));
        SolarisNICMap.insert(NICPair(VBOXBOW_VNIC_TEMPLATE_NAME, "VirtualBox VNIC Template"));
        SolarisNICMap.insert(NICPair("vlan", "Virtual LAN Ethernet"));
        SolarisNICMap.insert(NICPair("vr", "VIA Rhine Fast Ethernet"));
        SolarisNICMap.insert(NICPair("vnic", "Virtual Network Interface Ethernet"));
        SolarisNICMap.insert(NICPair("xge", "Neterior Xframe 10Gigabit Ethernet"));
        SolarisNICMap.insert(NICPair("yge", "Marvell Yukon 2 Fast Ethernet"));
    }

    /*
     * Try picking up description from our NIC map.
     */
    char szNICInstance[128];
    RTStrPrintf(szNICInstance, sizeof(szNICInstance), "%s%d", pszIface, Instance);
    char szNICDesc[256];
    std::string Description = SolarisNICMap[pszIface];
    if (Description != "VirtualBox Host Ethernet")
    {
        if (Description != "")
            RTStrPrintf(szNICDesc, sizeof(szNICDesc), "%s - %s", szNICInstance, Description.c_str());
        else if (!strncmp(szNICInstance, RT_STR_TUPLE(VBOXBOW_VNIC_TEMPLATE_NAME)))
        {
            /*
             * We want prefix matching only for "vboxvnic_template" as it's possible to create "vboxvnic_template_abcd123",
             * which our Solaris Crossbow NetFilter driver will interpret as a VNIC template.
             */
            Description = SolarisNICMap[VBOXBOW_VNIC_TEMPLATE_NAME];
            RTStrPrintf(szNICDesc, sizeof(szNICDesc), "%s - %s", szNICInstance, Description.c_str());
        }
        else
            RTStrPrintf(szNICDesc, sizeof(szNICDesc), "%s - Ethernet", szNICInstance);
    }
    else
        RTStrPrintf(szNICDesc, sizeof(szNICDesc), "%s", szNICInstance);

    /*
     * Try to get IP V4 address and netmask as well as Ethernet address.
     */
    NETIFINFO Info;
    RT_ZERO(Info);
    int Sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (Sock > 0)
    {
        struct lifreq IfReq;
        RTStrCopy(IfReq.lifr_name, sizeof(IfReq.lifr_name), szNICInstance);
        if (ioctl(Sock, SIOCGLIFADDR, &IfReq) >= 0)
        {
            memcpy(Info.IPAddress.au8, &((struct sockaddr_in *)&IfReq.lifr_addr)->sin_addr.s_addr,
                    sizeof(Info.IPAddress.au8));
            struct arpreq ArpReq;
            memcpy(&ArpReq.arp_pa, &IfReq.lifr_addr, sizeof(struct sockaddr_in));

            /*
             * We might fail if the interface has not been assigned an IP address.
             * That doesn't matter; as long as it's plumbed we can pick it up.
             * But, if it has not acquired an IP address we cannot obtain it's MAC
             * address this way, so we just use all zeros there.
             */
            if (ioctl(Sock, SIOCGARP, &ArpReq) >= 0)
            {
                memcpy(&Info.MACAddress, ArpReq.arp_ha.sa_data, sizeof(Info.MACAddress));
            }

        }

        if (ioctl(Sock, SIOCGLIFNETMASK, &IfReq) >= 0)
        {
            memcpy(Info.IPNetMask.au8, &((struct sockaddr_in *)&IfReq.lifr_addr)->sin_addr.s_addr,
                    sizeof(Info.IPNetMask.au8));
        }
        if (ioctl(Sock, SIOCGLIFFLAGS, &IfReq) >= 0)
        {
            Info.enmStatus = IfReq.lifr_flags & IFF_UP ? NETIF_S_UP : NETIF_S_DOWN;
        }
        close(Sock);
    }
    /*
     * Try to get IP V6 address and netmask.
     */
    Sock = socket(PF_INET6, SOCK_DGRAM, IPPROTO_IP);
    if (Sock > 0)
    {
        struct lifreq IfReq;
        RTStrCopy(IfReq.lifr_name, sizeof(IfReq.lifr_name), szNICInstance);
        if (ioctl(Sock, SIOCGLIFADDR, &IfReq) >= 0)
        {
            memcpy(Info.IPv6Address.au8, ((struct sockaddr_in6 *)&IfReq.lifr_addr)->sin6_addr.s6_addr,
                    sizeof(Info.IPv6Address.au8));
        }
        if (ioctl(Sock, SIOCGLIFNETMASK, &IfReq) >= 0)
        {
            memcpy(Info.IPv6NetMask.au8, ((struct sockaddr_in6 *)&IfReq.lifr_addr)->sin6_addr.s6_addr,
                    sizeof(Info.IPv6NetMask.au8));
        }
        close(Sock);
    }

    /*
     * Construct UUID with interface name and the MAC address if available.
     */
    RTUUID Uuid;
    RTUuidClear(&Uuid);
    memcpy(&Uuid, szNICInstance, RT_MIN(strlen(szNICInstance), sizeof(Uuid)));
    Uuid.Gen.u8ClockSeqHiAndReserved = (Uuid.Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
    Uuid.Gen.u16TimeHiAndVersion = (Uuid.Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
    Uuid.Gen.au8Node[0] = Info.MACAddress.au8[0];
    Uuid.Gen.au8Node[1] = Info.MACAddress.au8[1];
    Uuid.Gen.au8Node[2] = Info.MACAddress.au8[2];
    Uuid.Gen.au8Node[3] = Info.MACAddress.au8[3];
    Uuid.Gen.au8Node[4] = Info.MACAddress.au8[4];
    Uuid.Gen.au8Node[5] = Info.MACAddress.au8[5];
    Info.Uuid = Uuid;
    Info.enmMediumType = NETIF_T_ETHERNET;
    strncpy(Info.szShortName, szNICInstance, sizeof(Info.szShortName) - 1);

    HostNetworkInterfaceType_T enmType;
    if (strncmp(szNICInstance, RT_STR_TUPLE("vboxnet")))
        enmType = HostNetworkInterfaceType_Bridged;
    else
        enmType = HostNetworkInterfaceType_HostOnly;
    queryIfaceSpeed(&Info);
    ComObjPtr<HostNetworkInterface> IfObj;
    IfObj.createObject();
    if (SUCCEEDED(IfObj->init(szNICDesc, enmType, &Info)))
        pList->push_back(IfObj);
}

static boolean_t vboxSolarisAddLinkHostIface(const char *pszIface, void *pvHostNetworkInterfaceList)
{
    /*
     * Skip IPSEC interfaces. It's at IP level.
     */
    if (!strncmp(pszIface, RT_STR_TUPLE("ip.tun")))
        return _B_FALSE;

    /*
     * Skip our own dynamic VNICs but don't skip VNIC templates.
     * These names originate from VBoxNetFltBow-solaris.c, hardcoded here for now.
     *                                                                           .
     * ASSUMES template name is longer than 'vboxvnic'.
     */
    if (    strncmp(pszIface, RT_STR_TUPLE(VBOXBOW_VNIC_TEMPLATE_NAME))
        && !strncmp(pszIface, RT_STR_TUPLE("vboxvnic")))
        return _B_FALSE;

    /*
     * Clip off the zone instance number from the interface name (if any).
     */
    char szIfaceName[128];
    strcpy(szIfaceName, pszIface);
    char *pszColon = (char *)memchr(szIfaceName, ':', sizeof(szIfaceName));
    if (pszColon)
        *pszColon = '\0';

    /*
     * Get the instance number from the interface name, then clip it off.
     */
    int cbInstance = 0;
    size_t cbIface = strlen(szIfaceName);
    const char *pszEnd = pszIface + cbIface - 1;
    for (size_t i = 0; i < cbIface - 1; i++)
    {
        if (!RT_C_IS_DIGIT(*pszEnd))
            break;
        cbInstance++;
        pszEnd--;
    }

    int Instance = atoi(pszEnd + 1);
    strncpy(szIfaceName, pszIface, cbIface - cbInstance);
    szIfaceName[cbIface - cbInstance] = '\0';

    /*
     * Add the interface.
     */
    vboxSolarisAddHostIface(szIfaceName, Instance, pvHostNetworkInterfaceList);

    /*
     * Continue walking...
     */
    return _B_FALSE;
}

static bool vboxSolarisSortNICList(const ComObjPtr<HostNetworkInterface> Iface1, const ComObjPtr<HostNetworkInterface> Iface2)
{
    Bstr Iface1Str;
    (*Iface1).COMGETTER(Name) (Iface1Str.asOutParam());

    Bstr Iface2Str;
    (*Iface2).COMGETTER(Name) (Iface2Str.asOutParam());

    return Iface1Str < Iface2Str;
}

static bool vboxSolarisSameNIC(const ComObjPtr<HostNetworkInterface> Iface1, const ComObjPtr<HostNetworkInterface> Iface2)
{
    Bstr Iface1Str;
    (*Iface1).COMGETTER(Name) (Iface1Str.asOutParam());

    Bstr Iface2Str;
    (*Iface2).COMGETTER(Name) (Iface2Str.asOutParam());

    return (Iface1Str == Iface2Str);
}

static int vboxSolarisAddPhysHostIface(di_node_t Node, di_minor_t Minor, void *pvHostNetworkInterfaceList)
{
    NOREF(Minor);

    char *pszDriverName = di_driver_name(Node);
    int   Instance      = di_instance(Node);

    /*
     * Skip aggregations.
     */
    if (!strcmp(pszDriverName, "aggr"))
        return DI_WALK_CONTINUE;

    /*
     * Skip softmacs.
     */
    if (!strcmp(pszDriverName, "softmac"))
        return DI_WALK_CONTINUE;

    /*
     * Driver names doesn't always imply the same link name probably since
     * S11's vanity names by default (e.g. highly descriptive "net0") names
     * was introduced. Try opening the link to find out if it really exists.
     *
     * This weeds out listing of "e1000g0" as a valid interface on my S11.2
     * Dell Optiplex box.
     */
    if (VBoxSolarisLibDlpiFound())
    {
        /** @todo should we try also opening "linkname+instance"? */
        dlpi_handle_t hLink;
        if (g_pfnLibDlpiOpen(pszDriverName, &hLink, 0) != DLPI_SUCCESS)
            return DI_WALK_CONTINUE;
        g_pfnLibDlpiClose(hLink);
    }

    vboxSolarisAddHostIface(pszDriverName, Instance, pvHostNetworkInterfaceList);
    return DI_WALK_CONTINUE;
}

int NetIfList(std::list<ComObjPtr<HostNetworkInterface> > &list)
{
    /*
     * Use libdevinfo for determining all physical interfaces.
     */
    di_node_t Root;
    Root = di_init("/", DINFOCACHE);
    if (Root != DI_NODE_NIL)
    {
        di_walk_minor(Root, DDI_NT_NET, 0 /* flag */, &list, vboxSolarisAddPhysHostIface);
        di_fini(Root);
    }

    /*
     * Use libdlpi for determining all DLPI interfaces.
     */
    if (VBoxSolarisLibDlpiFound())
        g_pfnLibDlpiWalk(vboxSolarisAddLinkHostIface, &list, 0);

    /*
     * This gets only the list of all plumbed logical interfaces.
     * This is needed for zones which cannot access the device tree
     * and in this case we just let them use the list of plumbed interfaces
     * on the zone.
     */
    int Sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (Sock > 0)
    {
        struct lifnum IfNum;
        RT_ZERO(IfNum);
        IfNum.lifn_family = AF_INET;
        int iRc = ioctl(Sock, SIOCGLIFNUM, &IfNum);
        if (!iRc)
        {
            int cIfaces = RT_MIN(1024, IfNum.lifn_count); /* sane limit */
            size_t cbIfaces = (unsigned)RT_MAX(cIfaces, 1) * sizeof(struct lifreq);
            struct lifreq *paIfaces = (struct lifreq *)RTMemTmpAlloc(cbIfaces);
            if (paIfaces)
            {
                struct lifconf IfConfig;
                RT_ZERO(IfConfig);
                IfConfig.lifc_family = AF_INET;
                IfConfig.lifc_len = (int)cbIfaces;
                IfConfig.lifc_buf = (caddr_t)paIfaces;
                iRc = ioctl(Sock, SIOCGLIFCONF, &IfConfig);
                if (!iRc)
                {
                    for (int i = 0; i < cIfaces; i++)
                    {
                        /*
                         * Skip loopback interfaces.
                         */
                        if (!strncmp(paIfaces[i].lifr_name, RT_STR_TUPLE("lo")))
                            continue;

#if 0
                        iRc = ioctl(Sock, SIOCGLIFADDR, &(paIfaces[i]));
                        if (iRc >= 0)
                        {
                            memcpy(Info.IPAddress.au8, ((struct sockaddr *)&paIfaces[i].lifr_addr)->sa_data,
                                   sizeof(Info.IPAddress.au8));
                            // SIOCGLIFNETMASK
                            struct arpreq ArpReq;
                            memcpy(&ArpReq.arp_pa, &paIfaces[i].lifr_addr, sizeof(struct sockaddr_in));

                            /*
                             * We might fail if the interface has not been assigned an IP address.
                             * That doesn't matter; as long as it's plumbed we can pick it up.
                             * But, if it has not acquired an IP address we cannot obtain it's MAC
                             * address this way, so we just use all zeros there.
                             */
                            iRc = ioctl(Sock, SIOCGARP, &ArpReq);
                            if (iRc >= 0)
                                memcpy(&Info.MACAddress, ArpReq.arp_ha.sa_data, sizeof(Info.MACAddress));

                            char szNICDesc[LIFNAMSIZ + 256];
                            char *pszIface = paIfaces[i].lifr_name;
                            strcpy(szNICDesc, pszIface);

                            vboxSolarisAddLinkHostIface(pszIface, &list);
                        }
#endif

                        vboxSolarisAddLinkHostIface(paIfaces[i].lifr_name, &list);
                    }
                }
                RTMemTmpFree(paIfaces);
            }
        }
        close(Sock);
    }

    /*
     * Weed out duplicates caused by dlpi_walk inconsistencies across Nevadas.
     */
    list.sort(vboxSolarisSortNICList);
    list.unique(vboxSolarisSameNIC);

    return VINF_SUCCESS;
}

#else
int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list)
{
    return VERR_NOT_IMPLEMENTED;
}
#endif

int NetIfGetConfigByName(PNETIFINFO pInfo)
{
    NOREF(pInfo);
    return VERR_NOT_IMPLEMENTED;
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
    *puMbits =  kstatGet(pcszIfName);
    return VINF_SUCCESS;
}
