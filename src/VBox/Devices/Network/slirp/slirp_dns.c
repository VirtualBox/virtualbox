/* $Id: slirp_dns.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - dns initialization.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#include "slirp.h"
#ifdef RT_OS_OS2
# include <paths.h>
#endif

#include <iprt/errcore.h>
#include <VBox/vmm/pdmdrv.h>
#include <iprt/assert.h>
#include <iprt/file.h>

#ifdef RT_OS_WINDOWS
# include <iprt/utf16.h>
# include <Winnls.h>
# define _WINSOCK2API_
# include <iprt/win/iphlpapi.h>

static int get_dns_addr_domain(PNATState pData)
{
    /*ULONG flags = GAA_FLAG_INCLUDE_PREFIX;*/ /*GAA_FLAG_INCLUDE_ALL_INTERFACES;*/ /* all interfaces registered in NDIS */
    PIP_ADAPTER_ADDRESSES pAdapterAddr = NULL;
    PIP_ADAPTER_ADDRESSES pAddr = NULL;
    PIP_ADAPTER_DNS_SERVER_ADDRESS pDnsAddr = NULL;
    ULONG size;
    char *pszSuffix;
    struct dns_domain_entry *pDomain = NULL;
    ULONG ret = ERROR_SUCCESS;

    /** @todo add SKIPing flags to get only required information */

    /* determine size of buffer */
    size = 0;
    ret = pData->pfnGetAdaptersAddresses(AF_INET, 0, NULL /* reserved */, pAdapterAddr, &size);
    if (ret != ERROR_BUFFER_OVERFLOW)
    {
        Log(("NAT: error %lu occurred on capacity detection operation\n", ret));
        return -1;
    }
    if (size == 0)
    {
        Log(("NAT: Win socket API returns non capacity\n"));
        return -1;
    }

    pAdapterAddr = RTMemAllocZ(size);
    if (!pAdapterAddr)
    {
        Log(("NAT: No memory available\n"));
        return -1;
    }
    ret = pData->pfnGetAdaptersAddresses(AF_INET, 0, NULL /* reserved */, pAdapterAddr, &size);
    if (ret != ERROR_SUCCESS)
    {
        Log(("NAT: error %lu occurred on fetching adapters info\n", ret));
        RTMemFree(pAdapterAddr);
        return -1;
    }

    for (pAddr = pAdapterAddr; pAddr != NULL; pAddr = pAddr->Next)
    {
        int found;
        if (pAddr->OperStatus != IfOperStatusUp)
            continue;

        for (pDnsAddr = pAddr->FirstDnsServerAddress; pDnsAddr != NULL; pDnsAddr = pDnsAddr->Next)
        {
            struct sockaddr *SockAddr = pDnsAddr->Address.lpSockaddr;
            struct in_addr  InAddr;
            struct dns_entry *pDns;

            if (SockAddr->sa_family != AF_INET)
                continue;

            InAddr = ((struct sockaddr_in *)SockAddr)->sin_addr;

            /* add dns server to list */
            pDns = RTMemAllocZ(sizeof(struct dns_entry));
            if (!pDns)
            {
                Log(("NAT: Can't allocate buffer for DNS entry\n"));
                RTMemFree(pAdapterAddr);
                return VERR_NO_MEMORY;
            }

            Log(("NAT: adding %RTnaipv4 to DNS server list\n", InAddr));
            if ((InAddr.s_addr & RT_H2N_U32_C(IN_CLASSA_NET)) == RT_N2H_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET))
                pDns->de_addr.s_addr = RT_H2N_U32(RT_N2H_U32(pData->special_addr.s_addr) | CTL_ALIAS);
            else
                pDns->de_addr.s_addr = InAddr.s_addr;

            TAILQ_INSERT_HEAD(&pData->pDnsList, pDns, de_list);

            if (pAddr->DnsSuffix == NULL)
                continue;

            /* uniq */
            RTUtf16ToUtf8(pAddr->DnsSuffix, &pszSuffix);
            if (!pszSuffix || strlen(pszSuffix) == 0)
            {
                RTStrFree(pszSuffix);
                continue;
            }

            found = 0;
            LIST_FOREACH(pDomain, &pData->pDomainList, dd_list)
            {
                if (   pDomain->dd_pszDomain != NULL
                    && strcmp(pDomain->dd_pszDomain, pszSuffix) == 0)
                {
                    found = 1;
                    RTStrFree(pszSuffix);
                    break;
                }
            }
            if (!found)
            {
                pDomain = RTMemAllocZ(sizeof(struct dns_domain_entry));
                if (!pDomain)
                {
                    Log(("NAT: not enough memory\n"));
                    RTStrFree(pszSuffix);
                    RTMemFree(pAdapterAddr);
                    return VERR_NO_MEMORY;
                }
                pDomain->dd_pszDomain = pszSuffix;
                Log(("NAT: adding domain name %s to search list\n", pDomain->dd_pszDomain));
                LIST_INSERT_HEAD(&pData->pDomainList, pDomain, dd_list);
            }
        }
    }
    RTMemFree(pAdapterAddr);
    return 0;
}

#else /* !RT_OS_WINDOWS */

#include "resolv_conf_parser.h"

static int get_dns_addr_domain(PNATState pData)
{
    struct rcp_state st;
    int rc;
    unsigned i;

    /* XXX: perhaps IPv6 shouldn't be ignored if we're using DNS proxy */
    st.rcps_flags = RCPSF_IGNORE_IPV6;
    rc = rcp_parse(&st, RESOLV_CONF_FILE);

    if (rc < 0)
        return -1;

    /* for historical reasons: Slirp returns -1 if no nameservers were found */
    if (st.rcps_num_nameserver == 0)
        return -1;


    /* XXX: We're composing the list, but we already knows
     * its size so we can allocate array instead (Linux guests
     * dont like >3 servers in the list anyway)
     * or use pre-allocated array in NATState.
     */
    for (i = 0; i != st.rcps_num_nameserver; ++i)
    {
        struct dns_entry *pDns;
        RTNETADDRU *address = &st.rcps_nameserver[i].uAddr;

        if (address->IPv4.u == INADDR_ANY)
        {
            /*
             * This doesn't seem to be very well documented except for
             * RTFS of res_init.c, but INADDR_ANY is a valid value for
             * for "nameserver".
             */
            address->IPv4.u = RT_H2N_U32_C(INADDR_LOOPBACK);
        }

        if (  (address->IPv4.u & RT_H2N_U32_C(IN_CLASSA_NET))
           == RT_N2H_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET))
        {
            /**
             * XXX: Note shouldn't patch the address in case of using DNS proxy,
             * because DNS proxy we do revert it back actually.
             */
            if (   address->IPv4.u == RT_N2H_U32_C(INADDR_LOOPBACK)
                && pData->fLocalhostReachable)
                address->IPv4.u = RT_H2N_U32(RT_N2H_U32(pData->special_addr.s_addr) | CTL_ALIAS);
            else if (pData->fUseDnsProxy == 0) {
                /*
                 * Either the resolver lives somewhere else on the 127/8 network or the loopback interface
                 * is blocked for access from the guest, either way switch to the DNS proxy.
                 */
                if (pData->fLocalhostReachable)
                    LogRel(("NAT: DNS server %RTnaipv4 registration detected, switching to the DNS proxy\n", address->IPv4));
                else
                    LogRel(("NAT: Switching to DNS proxying due to access to the loopback interface being blocked\n"));
                pData->fUseDnsProxy = 1;
            }
        }

        pDns = RTMemAllocZ(sizeof(struct dns_entry));
        if (pDns == NULL)
        {
            slirpReleaseDnsSettings(pData);
            return VERR_NO_MEMORY;
        }

        pDns->de_addr.s_addr = address->IPv4.u;
        TAILQ_INSERT_HEAD(&pData->pDnsList, pDns, de_list);
    }

    if (st.rcps_domain != 0)
    {
        struct dns_domain_entry *pDomain = RTMemAllocZ(sizeof(struct dns_domain_entry));
        if (pDomain == NULL)
        {
            slirpReleaseDnsSettings(pData);
            return -1;
        }

        pDomain->dd_pszDomain = RTStrDup(st.rcps_domain);
        LogRel(("NAT: Adding domain name %s\n", pDomain->dd_pszDomain));
        LIST_INSERT_HEAD(&pData->pDomainList, pDomain, dd_list);
    }

    return 0;
}

#endif /* !RT_OS_WINDOWS */

int slirpInitializeDnsSettings(PNATState pData)
{
    int rc = VINF_SUCCESS;
    AssertPtrReturn(pData, VERR_INVALID_PARAMETER);
    LogFlowFuncEnter();
    if (!pData->fUseHostResolverPermanent)
    {
        TAILQ_INIT(&pData->pDnsList);
        LIST_INIT(&pData->pDomainList);

        /*
         * Some distributions haven't got /etc/resolv.conf
         * so we should other way to configure DNS settings.
         */
        if (get_dns_addr_domain(pData) < 0)
            pData->fUseHostResolver = true;
        else
        {
            pData->fUseHostResolver = false;
            dnsproxy_init(pData);
        }

        if (!pData->fUseHostResolver)
        {
            struct dns_entry *pDNSEntry = NULL;
            int cDNSListEntry = 0;
            TAILQ_FOREACH_REVERSE(pDNSEntry, &pData->pDnsList, dns_list_head, de_list)
            {
                LogRel(("NAT: DNS#%i: %RTnaipv4\n", cDNSListEntry, pDNSEntry->de_addr.s_addr));
                cDNSListEntry++;
            }
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int slirpReleaseDnsSettings(PNATState pData)
{
    struct dns_entry *pDns = NULL;
    struct dns_domain_entry *pDomain = NULL;
    int rc = VINF_SUCCESS;
    AssertPtrReturn(pData, VERR_INVALID_PARAMETER);
    LogFlowFuncEnter();

    while (!TAILQ_EMPTY(&pData->pDnsList))
    {
        pDns = TAILQ_FIRST(&pData->pDnsList);
        TAILQ_REMOVE(&pData->pDnsList, pDns, de_list);
        RTMemFree(pDns);
    }

    while (!LIST_EMPTY(&pData->pDomainList))
    {
        pDomain = LIST_FIRST(&pData->pDomainList);
        LIST_REMOVE(pDomain, dd_list);
        if (pDomain->dd_pszDomain != NULL)
            RTStrFree(pDomain->dd_pszDomain);
        RTMemFree(pDomain);
    }

    /* tell any pending dnsproxy requests their copy is expired */
    ++pData->dnsgen;

    LogFlowFuncLeaveRC(rc);
    return rc;
}
