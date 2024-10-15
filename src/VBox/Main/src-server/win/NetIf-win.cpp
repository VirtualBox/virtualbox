/* $Id: NetIf-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - NetIfList, Windows implementation.
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

#define NETIF_WITHOUT_NETCFG

#include <iprt/errcore.h>
#include <list>

#define _WIN32_DCOM
#include <iprt/win/winsock2.h>
#include <iprt/win/ws2tcpip.h>
#include <iprt/win/windows.h>
#include <tchar.h>

#ifdef VBOX_WITH_NETFLT
# include "VBox/VBoxNetCfg-win.h"
# include "devguid.h"
#endif

#include <iprt/win/iphlpapi.h>
#include <iprt/win/ntddndis.h>

#include "LoggingNew.h"
#include "HostNetworkInterfaceImpl.h"
#include "ProgressImpl.h"
#include "VirtualBoxImpl.h"
#include "VBoxNls.h"
#include "Global.h"
#include "netif.h"
#include "ThreadTask.h"

DECLARE_TRANSLATION_CONTEXT(NetIfWin);

#ifdef VBOX_WITH_NETFLT
# include <Wbemidl.h>

# include "svchlp.h"

# include <shellapi.h>
# define INITGUID
# include <guiddef.h>
# include <devguid.h>
# include <iprt/win/objbase.h>
# include <iprt/win/setupapi.h>
# include <iprt/win/shlobj.h>
# include <cfgmgr32.h>

# define VBOX_APP_NAME L"VirtualBox"

static int getDefaultInterfaceIndex()
{
    PMIB_IPFORWARDTABLE pIpTable;
    DWORD dwSize = sizeof(MIB_IPFORWARDTABLE) * 20;
    DWORD dwRC = NO_ERROR;
    int iIndex = -1;

    pIpTable = (MIB_IPFORWARDTABLE *)RTMemAlloc(dwSize);
    if (GetIpForwardTable(pIpTable, &dwSize, 0) == ERROR_INSUFFICIENT_BUFFER)
    {
        RTMemFree(pIpTable);
        pIpTable = (MIB_IPFORWARDTABLE *)RTMemAlloc(dwSize);
        if (!pIpTable)
            return -1;
    }
    dwRC = GetIpForwardTable(pIpTable, &dwSize, 0);
    if (dwRC == NO_ERROR)
    {
        for (unsigned int i = 0; i < pIpTable->dwNumEntries; i++)
            if (pIpTable->table[i].dwForwardDest == 0)
            {
                iIndex = pIpTable->table[i].dwForwardIfIndex;
                break;
            }
    }
    RTMemFree(pIpTable);
    return iIndex;
}

static int collectNetIfInfo(Bstr &strName, Guid &guid, PNETIFINFO pInfo, int iDefault)
{
    RT_NOREF(strName);

    /*
     * Most of the hosts probably have less than 10 adapters,
     * so we'll mostly succeed from the first attempt.
     */
    ULONG uBufLen = sizeof(IP_ADAPTER_ADDRESSES) * 10;
    PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)RTMemAlloc(uBufLen);
    if (!pAddresses)
        return VERR_NO_MEMORY;
    DWORD dwRc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &uBufLen);
    if (dwRc == ERROR_BUFFER_OVERFLOW)
    {
        /* Impressive! More than 10 adapters! Get more memory and try again. */
        RTMemFree(pAddresses);
        pAddresses = (PIP_ADAPTER_ADDRESSES)RTMemAlloc(uBufLen);
        if (!pAddresses)
            return VERR_NO_MEMORY;
        dwRc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &uBufLen);
    }
    if (dwRc == NO_ERROR)
    {
        PIP_ADAPTER_ADDRESSES pAdapter;
        for (pAdapter = pAddresses; pAdapter; pAdapter = pAdapter->Next)
        {
            char *pszUuid = RTStrDup(pAdapter->AdapterName);
            size_t len = strlen(pszUuid) - 1;
            if (pszUuid[0] == '{' && pszUuid[len] == '}')
            {
                pszUuid[len] = 0;
                if (!RTUuidCompareStr(&pInfo->Uuid, pszUuid + 1))
                {
                    bool fIPFound, fIPv6Found;
                    PIP_ADAPTER_UNICAST_ADDRESS pAddr;
                    fIPFound = fIPv6Found = false;
                    for (pAddr = pAdapter->FirstUnicastAddress; pAddr; pAddr = pAddr->Next)
                    {
                        switch (pAddr->Address.lpSockaddr->sa_family)
                        {
                            case AF_INET:
                                if (!fIPFound)
                                {
                                    fIPFound = true;
                                    memcpy(&pInfo->IPAddress,
                                        &((struct sockaddr_in *)pAddr->Address.lpSockaddr)->sin_addr.s_addr,
                                        sizeof(pInfo->IPAddress));
                                }
                                break;
                            case AF_INET6:
                                if (!fIPv6Found)
                                {
                                    fIPv6Found = true;
                                    memcpy(&pInfo->IPv6Address,
                                        ((struct sockaddr_in6 *)pAddr->Address.lpSockaddr)->sin6_addr.s6_addr,
                                        sizeof(pInfo->IPv6Address));
                                }
                                break;
                        }
                    }
                    PIP_ADAPTER_PREFIX pPrefix;
                    fIPFound = fIPv6Found = false;
                    for (pPrefix = pAdapter->FirstPrefix; pPrefix; pPrefix = pPrefix->Next)
                    {
                        switch (pPrefix->Address.lpSockaddr->sa_family)
                        {
                            case AF_INET:
                                if (!fIPFound)
                                {
                                    if (pPrefix->PrefixLength <= sizeof(pInfo->IPNetMask) * 8)
                                    {
                                        fIPFound = true;
                                        RTNetPrefixToMaskIPv4(pPrefix->PrefixLength, &pInfo->IPNetMask);
                                    }
                                    else
                                        LogFunc(("Unexpected IPv4 prefix length of %d\n",
                                                 pPrefix->PrefixLength));
                                }
                                break;
                            case AF_INET6:
                                if (!fIPv6Found)
                                {
                                    if (pPrefix->PrefixLength <= sizeof(pInfo->IPv6NetMask) * 8)
                                    {
                                        fIPv6Found = true;
                                        RTNetPrefixToMaskIPv6(pPrefix->PrefixLength, &pInfo->IPv6NetMask);
                                    }
                                    else
                                        LogFunc(("Unexpected IPv6 prefix length of %d\n",
                                                pPrefix->PrefixLength));
                                }
                                break;
                        }
                    }
                    if (sizeof(pInfo->MACAddress) != pAdapter->PhysicalAddressLength)
                        LogFunc(("Unexpected physical address length: %u\n", pAdapter->PhysicalAddressLength));
                    else
                        memcpy(pInfo->MACAddress.au8, pAdapter->PhysicalAddress, sizeof(pInfo->MACAddress));
                    pInfo->enmMediumType = NETIF_T_ETHERNET;
                    pInfo->enmStatus = pAdapter->OperStatus == IfOperStatusUp ? NETIF_S_UP : NETIF_S_DOWN;
                    pInfo->fIsDefault = (pAdapter->IfIndex == (DWORD)iDefault);
                    RTStrFree(pszUuid);
                    break;
                }
            }
            RTStrFree(pszUuid);
        }

        ADAPTER_SETTINGS Settings;
        HRESULT hrc = VBoxNetCfgWinGetAdapterSettings((const GUID *)guid.raw(), &Settings);
        if (hrc == S_OK)
        {
            if (Settings.ip)
            {
                pInfo->IPAddress.u = Settings.ip;
                pInfo->IPNetMask.u = Settings.mask;
            }
            pInfo->fDhcpEnabled = Settings.bDhcp;
        }
        else
        {
            pInfo->fDhcpEnabled = false;
        }
    }
    RTMemFree(pAddresses);

    return VINF_SUCCESS;
}

/* svc helper func */

struct StaticIpConfig
{
    ULONG  IPAddress;
    ULONG  IPNetMask;
};

struct StaticIpV6Config
{
    char *         IPV6Address;
    ULONG          IPV6NetMaskLength;
};

class NetworkInterfaceHelperClientData : public ThreadVoidData
{
public:
    NetworkInterfaceHelperClientData(){};
    ~NetworkInterfaceHelperClientData()
    {
        if (msgCode == SVCHlpMsg::EnableStaticIpConfigV6 && u.StaticIPV6.IPV6Address)
        {
            RTStrFree(u.StaticIPV6.IPV6Address);
            u.StaticIPV6.IPV6Address = NULL;
        }
    };

    SVCHlpMsg::Code msgCode;
    /* for SVCHlpMsg::CreateHostOnlyNetworkInterface */
    Bstr name;
    ComObjPtr<HostNetworkInterface> iface;
    ComObjPtr<VirtualBox> ptrVBox;
    /* for SVCHlpMsg::RemoveHostOnlyNetworkInterface */
    Guid guid;

    union
    {
        StaticIpConfig StaticIP;
        StaticIpV6Config StaticIPV6;
    } u;

};

static HRESULT netIfNetworkInterfaceHelperClient(SVCHlpClient *aClient,
                                                 Progress *aProgress,
                                                 void *aUser, int *aVrc)
{
    LogFlowFuncEnter();
    LogFlowFunc(("aClient={%p}, aProgress={%p}, aUser={%p}\n",
                 aClient, aProgress, aUser));

    AssertReturn(   (aClient == NULL && aProgress == NULL && aVrc == NULL)
                 || (aClient != NULL && aProgress != NULL && aVrc != NULL),
                 E_POINTER);
    AssertReturn(aUser, E_POINTER);

    NetworkInterfaceHelperClientData* d = static_cast<NetworkInterfaceHelperClientData *>(aUser);

    if (aClient == NULL)
    {
        /* "cleanup only" mode, just return (it will free aUser) */
        return S_OK;
    }

    HRESULT hrc = S_OK;
    int vrc = VINF_SUCCESS;

    switch (d->msgCode)
    {
        case SVCHlpMsg::CreateHostOnlyNetworkInterface:
        {
            LogFlowFunc(("CreateHostOnlyNetworkInterface:\n"));
            LogFlowFunc(("Network connection name = '%ls'\n", d->name.raw()));

            /* write message and parameters */
            vrc = aClient->write(d->msgCode);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(Utf8Str(d->name));
            if (RT_FAILURE(vrc)) break;

            /* wait for a reply */
            bool endLoop = false;
            while (!endLoop)
            {
                SVCHlpMsg::Code reply = SVCHlpMsg::Null;

                vrc = aClient->read(reply);
                if (RT_FAILURE(vrc)) break;

                switch (reply)
                {
                    case SVCHlpMsg::CreateHostOnlyNetworkInterface_OK:
                    {
                        /* read the GUID */
                        Guid guid;
                        Utf8Str name;
                        vrc = aClient->read(name);
                        if (RT_FAILURE(vrc)) break;
                        vrc = aClient->read(guid);
                        if (RT_FAILURE(vrc)) break;

                        LogFlowFunc(("Network connection GUID = {%RTuuid}\n", guid.raw()));

                        /* initialize the object returned to the caller by
                         * CreateHostOnlyNetworkInterface() */
                        hrc = d->iface->init(Bstr(name), Bstr(name), guid, HostNetworkInterfaceType_HostOnly);
                        if (SUCCEEDED(hrc))
                        {
                            hrc = d->iface->i_setVirtualBox(d->ptrVBox);
                            if (SUCCEEDED(hrc))
                            {
                                hrc = d->iface->updateConfig();
                                if (SUCCEEDED(hrc))
                                    hrc = d->iface->i_updatePersistentConfig();
                            }
                        }
                        endLoop = true;
                        break;
                    }
                    case SVCHlpMsg::Error:
                    {
                        /* read the error message */
                        Utf8Str errMsg;
                        vrc = aClient->read(errMsg);
                        if (RT_FAILURE(vrc)) break;

                        hrc = E_FAIL;
                        d->iface->setError(E_FAIL, errMsg.c_str());
                        endLoop = true;
                        break;
                    }
                    default:
                    {
                        endLoop = true;
                        hrc = E_FAIL;/// @todo ComAssertMsgFailedBreak((
                            //"Invalid message code %d (%08lX)\n",
                            //reply, reply),
                            //hrc = E_FAIL);
                    }
                }
            }

            break;
        }
        case SVCHlpMsg::RemoveHostOnlyNetworkInterface:
        {
            LogFlowFunc(("RemoveHostOnlyNetworkInterface:\n"));
            LogFlowFunc(("Network connection GUID = {%RTuuid}\n", d->guid.raw()));

            /* write message and parameters */
            vrc = aClient->write(d->msgCode);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->guid);
            if (RT_FAILURE(vrc)) break;

            /* wait for a reply */
            bool endLoop = false;
            while (!endLoop)
            {
                SVCHlpMsg::Code reply = SVCHlpMsg::Null;

                vrc = aClient->read(reply);
                if (RT_FAILURE(vrc)) break;

                switch (reply)
                {
                    case SVCHlpMsg::OK:
                    {
                        /* no parameters */
                        hrc = S_OK;
                        endLoop = true;
                        break;
                    }
                    case SVCHlpMsg::Error:
                    {
                        /* read the error message */
                        Utf8Str errMsg;
                        vrc = aClient->read(errMsg);
                        if (RT_FAILURE(vrc)) break;

                        hrc = E_FAIL;
                        d->iface->setError(E_FAIL, errMsg.c_str());
                        endLoop = true;
                        break;
                    }
                    default:
                    {
                        endLoop = true;
                        hrc = E_FAIL; /// @todo ComAssertMsgFailedBreak((
                            //"Invalid message code %d (%08lX)\n",
                            //reply, reply),
                            //hrc = E_FAIL);
                    }
                }
            }

            break;
        }
        case SVCHlpMsg::EnableDynamicIpConfig: /* see usage in code */
        {
            LogFlowFunc(("EnableDynamicIpConfig:\n"));
            LogFlowFunc(("Network connection name = '%ls'\n", d->name.raw()));

            /* write message and parameters */
            vrc = aClient->write(d->msgCode);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->guid);
            if (RT_FAILURE(vrc)) break;

            /* wait for a reply */
            bool endLoop = false;
            while (!endLoop)
            {
                SVCHlpMsg::Code reply = SVCHlpMsg::Null;

                vrc = aClient->read(reply);
                if (RT_FAILURE(vrc)) break;

                switch (reply)
                {
                    case SVCHlpMsg::OK:
                    {
                        /* no parameters */
                        hrc = d->iface->updateConfig();
                        endLoop = true;
                        break;
                    }
                    case SVCHlpMsg::Error:
                    {
                        /* read the error message */
                        Utf8Str errMsg;
                        vrc = aClient->read(errMsg);
                        if (RT_FAILURE(vrc)) break;

                        hrc = E_FAIL;
                        d->iface->setError(E_FAIL, errMsg.c_str());
                        endLoop = true;
                        break;
                    }
                    default:
                    {
                        endLoop = true;
                        hrc = E_FAIL; /// @todo ComAssertMsgFailedBreak((
                            //"Invalid message code %d (%08lX)\n",
                            //reply, reply),
                            //hrc = E_FAIL);
                    }
                }
            }

            break;
        }
        case SVCHlpMsg::EnableStaticIpConfig: /* see usage in code */
        {
            LogFlowFunc(("EnableStaticIpConfig:\n"));
            LogFlowFunc(("Network connection name = '%ls'\n", d->name.raw()));

            /* write message and parameters */
            vrc = aClient->write(d->msgCode);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->guid);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->u.StaticIP.IPAddress);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->u.StaticIP.IPNetMask);
            if (RT_FAILURE(vrc)) break;

            /* wait for a reply */
            bool endLoop = false;
            while (!endLoop)
            {
                SVCHlpMsg::Code reply = SVCHlpMsg::Null;

                vrc = aClient->read(reply);
                if (RT_FAILURE(vrc)) break;

                switch (reply)
                {
                    case SVCHlpMsg::OK:
                    {
                        /* no parameters */
                        hrc = d->iface->updateConfig();
                        endLoop = true;
                        break;
                    }
                    case SVCHlpMsg::Error:
                    {
                        /* read the error message */
                        Utf8Str errMsg;
                        vrc = aClient->read(errMsg);
                        if (RT_FAILURE(vrc)) break;

                        hrc = E_FAIL;
                        d->iface->setError(E_FAIL, errMsg.c_str());
                        endLoop = true;
                        break;
                    }
                    default:
                    {
                        endLoop = true;
                        hrc = E_FAIL; /// @todo ComAssertMsgFailedBreak((
                            //"Invalid message code %d (%08lX)\n",
                            //reply, reply),
                            //hrc = E_FAIL);
                    }
                }
            }

            break;
        }
        case SVCHlpMsg::EnableStaticIpConfigV6: /* see usage in code */
        {
            LogFlowFunc(("EnableStaticIpConfigV6:\n"));
            LogFlowFunc(("Network connection name = '%ls'\n", d->name.raw()));

            /* write message and parameters */
            vrc = aClient->write(d->msgCode);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->guid);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->u.StaticIPV6.IPV6Address);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->u.StaticIPV6.IPV6NetMaskLength);
            if (RT_FAILURE(vrc)) break;

            /* wait for a reply */
            bool endLoop = false;
            while (!endLoop)
            {
                SVCHlpMsg::Code reply = SVCHlpMsg::Null;

                vrc = aClient->read(reply);
                if (RT_FAILURE(vrc)) break;

                switch (reply)
                {
                    case SVCHlpMsg::OK:
                    {
                        /* no parameters */
                        hrc = d->iface->updateConfig();
                        endLoop = true;
                        break;
                    }
                    case SVCHlpMsg::Error:
                    {
                        /* read the error message */
                        Utf8Str errMsg;
                        vrc = aClient->read(errMsg);
                        if (RT_FAILURE(vrc)) break;

                        hrc = E_FAIL;
                        d->iface->setError(E_FAIL, errMsg.c_str());
                        endLoop = true;
                        break;
                    }
                    default:
                    {
                        endLoop = true;
                        hrc = E_FAIL; /// @todo ComAssertMsgFailedBreak((
                            //"Invalid message code %d (%08lX)\n",
                            //reply, reply),
                            //hrc = E_FAIL);
                    }
                }
            }

            break;
        }
        case SVCHlpMsg::DhcpRediscover: /* see usage in code */
        {
            LogFlowFunc(("DhcpRediscover:\n"));
            LogFlowFunc(("Network connection name = '%ls'\n", d->name.raw()));

            /* write message and parameters */
            vrc = aClient->write(d->msgCode);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->write(d->guid);
            if (RT_FAILURE(vrc)) break;

            /* wait for a reply */
            bool endLoop = false;
            while (!endLoop)
            {
                SVCHlpMsg::Code reply = SVCHlpMsg::Null;

                vrc = aClient->read(reply);
                if (RT_FAILURE(vrc)) break;

                switch (reply)
                {
                    case SVCHlpMsg::OK:
                    {
                        /* no parameters */
                        hrc = d->iface->updateConfig();
                        endLoop = true;
                        break;
                    }
                    case SVCHlpMsg::Error:
                    {
                        /* read the error message */
                        Utf8Str errMsg;
                        vrc = aClient->read(errMsg);
                        if (RT_FAILURE(vrc)) break;

                        hrc = E_FAIL;
                        d->iface->setError(E_FAIL, errMsg.c_str());
                        endLoop = true;
                        break;
                    }
                    default:
                    {
                        endLoop = true;
                        hrc = E_FAIL; /// @todo ComAssertMsgFailedBreak((
                            //"Invalid message code %d (%08lX)\n",
                            //reply, reply),
                            //hrc = E_FAIL);
                    }
                }
            }

            break;
        }
        default:
            hrc = E_FAIL; /// @todo ComAssertMsgFailedBreak((
//                "Invalid message code %d (%08lX)\n",
//                d->msgCode, d->msgCode),
//                hrc = E_FAIL);
    }

    if (aVrc)
        *aVrc = vrc;

    LogFlowFunc(("hrc=0x%08X, vrc=%Rrc\n", hrc, vrc));
    LogFlowFuncLeave();
    return hrc;
}


int netIfNetworkInterfaceHelperServer(SVCHlpClient *aClient,
                                      SVCHlpMsg::Code aMsgCode)
{
    LogFlowFuncEnter();
    LogFlowFunc(("aClient={%p}, aMsgCode=%d\n", aClient, aMsgCode));

    AssertReturn(aClient, VERR_INVALID_POINTER);

    int vrc = VINF_SUCCESS;
    HRESULT hrc;

    switch (aMsgCode)
    {
        case SVCHlpMsg::CreateHostOnlyNetworkInterface:
        {
            LogFlowFunc(("CreateHostOnlyNetworkInterface:\n"));

            Utf8Str desiredName;
            vrc = aClient->read(desiredName);
            if (RT_FAILURE(vrc)) break;

            Guid guid;
            Utf8Str errMsg;
            Bstr name;
            Bstr bstrErr;

#ifdef VBOXNETCFG_DELAYEDRENAME
            Bstr devId;
            hrc = VBoxNetCfgWinCreateHostOnlyNetworkInterface(NULL, false, Bstr(desiredName).raw(), guid.asOutParam(), devId.asOutParam(),
                                                              bstrErr.asOutParam());
#else /* !VBOXNETCFG_DELAYEDRENAME */
            hrc = VBoxNetCfgWinCreateHostOnlyNetworkInterface(NULL, false, Bstr(desiredName).raw(), guid.asOutParam(), name.asOutParam(),
                                                              bstrErr.asOutParam());
#endif /* !VBOXNETCFG_DELAYEDRENAME */

            if (hrc == S_OK)
            {
                ULONG ip, mask;
                hrc = VBoxNetCfgWinGenHostOnlyNetworkNetworkIp(&ip, &mask);
                if (hrc == S_OK)
                {
                    /* ip returned by VBoxNetCfgWinGenHostOnlyNetworkNetworkIp is a network ip,
                     * i.e. 192.168.xxx.0, assign  192.168.xxx.1 for the hostonly adapter */
                    ip = ip | (1 << 24);
                    hrc = VBoxNetCfgWinEnableStaticIpConfig((const GUID*)guid.raw(), ip, mask);
                    if (hrc != S_OK)
                        LogRel(("VBoxNetCfgWinEnableStaticIpConfig failed (0x%x)\n", hrc));
                }
                else
                    LogRel(("VBoxNetCfgWinGenHostOnlyNetworkNetworkIp failed (0x%x)\n", hrc));
#ifdef VBOXNETCFG_DELAYEDRENAME
                hrc = VBoxNetCfgWinRenameHostOnlyConnection((const GUID*)guid.raw(), devId.raw(), name.asOutParam());
                if (hrc != S_OK)
                    LogRel(("VBoxNetCfgWinRenameHostOnlyConnection failed, error = 0x%x", hrc));
#endif /* VBOXNETCFG_DELAYEDRENAME */
                /* write success followed by GUID */
                vrc = aClient->write(SVCHlpMsg::CreateHostOnlyNetworkInterface_OK);
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(Utf8Str(name));
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(guid);
                if (RT_FAILURE(vrc)) break;
            }
            else
            {
                vrc = VERR_GENERAL_FAILURE;
                errMsg = Utf8Str(bstrErr);
                /* write failure followed by error message */
                if (errMsg.isEmpty())
                    errMsg = Utf8StrFmt("Unspecified error (%Rrc)", vrc);
                vrc = aClient->write(SVCHlpMsg::Error);
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(errMsg);
                if (RT_FAILURE(vrc)) break;
            }

            break;
        }
        case SVCHlpMsg::RemoveHostOnlyNetworkInterface:
        {
            LogFlowFunc(("RemoveHostOnlyNetworkInterface:\n"));

            Guid guid;
            Bstr bstrErr;

            vrc = aClient->read(guid);
            if (RT_FAILURE(vrc)) break;

            Utf8Str errMsg;
            hrc = VBoxNetCfgWinRemoveHostOnlyNetworkInterface((const GUID*)guid.raw(), bstrErr.asOutParam());

            if (hrc == S_OK)
            {
                /* write parameter-less success */
                vrc = aClient->write(SVCHlpMsg::OK);
                if (RT_FAILURE(vrc)) break;
            }
            else
            {
                vrc = VERR_GENERAL_FAILURE;
                errMsg = Utf8Str(bstrErr);
                /* write failure followed by error message */
                if (errMsg.isEmpty())
                    errMsg = Utf8StrFmt("Unspecified error (%Rrc)", vrc);
                vrc = aClient->write(SVCHlpMsg::Error);
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(errMsg);
                if (RT_FAILURE(vrc)) break;
            }

            break;
        }
        case SVCHlpMsg::EnableStaticIpConfigV6:
        {
            LogFlowFunc(("EnableStaticIpConfigV6:\n"));

            Guid guid;
            Utf8Str ipV6;
            ULONG maskLengthV6;
            vrc = aClient->read(guid);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->read(ipV6);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->read(maskLengthV6);
            if (RT_FAILURE(vrc)) break;

            Utf8Str errMsg;
            vrc = VERR_NOT_IMPLEMENTED;

            if (RT_SUCCESS(vrc))
            {
                /* write success followed by GUID */
                vrc = aClient->write(SVCHlpMsg::OK);
                if (RT_FAILURE(vrc)) break;
            }
            else
            {
                /* write failure followed by error message */
                if (errMsg.isEmpty())
                    errMsg = Utf8StrFmt("Unspecified error (%Rrc)", vrc);
                vrc = aClient->write(SVCHlpMsg::Error);
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(errMsg);
                if (RT_FAILURE(vrc)) break;
            }

            break;
        }
        case SVCHlpMsg::EnableStaticIpConfig:
        {
            LogFlowFunc(("EnableStaticIpConfig:\n"));

            Guid guid;
            ULONG ip, mask;
            vrc = aClient->read(guid);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->read(ip);
            if (RT_FAILURE(vrc)) break;
            vrc = aClient->read(mask);
            if (RT_FAILURE(vrc)) break;

            Utf8Str errMsg;
            hrc = VBoxNetCfgWinEnableStaticIpConfig((const GUID *)guid.raw(), ip, mask);

            if (hrc == S_OK)
            {
                /* write success followed by GUID */
                vrc = aClient->write(SVCHlpMsg::OK);
                if (RT_FAILURE(vrc)) break;
            }
            else
            {
                vrc = VERR_GENERAL_FAILURE;
                /* write failure followed by error message */
                if (errMsg.isEmpty())
                    errMsg = Utf8StrFmt("Unspecified error (%Rrc)", vrc);
                vrc = aClient->write(SVCHlpMsg::Error);
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(errMsg);
                if (RT_FAILURE(vrc)) break;
            }

            break;
        }
        case SVCHlpMsg::EnableDynamicIpConfig:
        {
            LogFlowFunc(("EnableDynamicIpConfig:\n"));

            Guid guid;
            vrc = aClient->read(guid);
            if (RT_FAILURE(vrc)) break;

            Utf8Str errMsg;
            hrc = VBoxNetCfgWinEnableDynamicIpConfig((const GUID *)guid.raw());

            if (hrc == S_OK)
            {
                /* write success followed by GUID */
                vrc = aClient->write(SVCHlpMsg::OK);
                if (RT_FAILURE(vrc)) break;
            }
            else
            {
                vrc = VERR_GENERAL_FAILURE;
                /* write failure followed by error message */
                if (errMsg.isEmpty())
                    errMsg = Utf8StrFmt("Unspecified error (%Rrc)", vrc);
                vrc = aClient->write(SVCHlpMsg::Error);
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(errMsg);
                if (RT_FAILURE(vrc)) break;
            }

            break;
        }
        case SVCHlpMsg::DhcpRediscover:
        {
            LogFlowFunc(("DhcpRediscover:\n"));

            Guid guid;
            vrc = aClient->read(guid);
            if (RT_FAILURE(vrc)) break;

            Utf8Str errMsg;
            hrc = VBoxNetCfgWinDhcpRediscover((const GUID *)guid.raw());

            if (hrc == S_OK)
            {
                /* write success followed by GUID */
                vrc = aClient->write(SVCHlpMsg::OK);
                if (RT_FAILURE(vrc)) break;
            }
            else
            {
                vrc = VERR_GENERAL_FAILURE;
                /* write failure followed by error message */
                if (errMsg.isEmpty())
                    errMsg = Utf8StrFmt("Unspecified error (%Rrc)", vrc);
                vrc = aClient->write(SVCHlpMsg::Error);
                if (RT_FAILURE(vrc)) break;
                vrc = aClient->write(errMsg);
                if (RT_FAILURE(vrc)) break;
            }

            break;
        }
        default:
            AssertMsgFailedBreakStmt(
                ("Invalid message code %d (%08lX)\n", aMsgCode, aMsgCode),
                vrc = VERR_GENERAL_FAILURE);
    }

    LogFlowFunc(("vrc=%Rrc\n", vrc));
    LogFlowFuncLeave();
    return vrc;
}

/** @todo REMOVE. OBSOLETE NOW. */
/**
 * Returns TRUE if the Windows version is 6.0 or greater (i.e. it's Vista and
 * later OSes) and it has the UAC (User Account Control) feature enabled.
 */
static BOOL IsUACEnabled()
{
    OSVERSIONINFOEX info;
    ZeroMemory(&info, sizeof(OSVERSIONINFOEX));
    info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    BOOL frc = GetVersionEx((OSVERSIONINFO *) &info);
    AssertReturn(frc != FALSE, FALSE);

    LogFlowFunc(("dwMajorVersion=%d, dwMinorVersion=%d\n", info.dwMajorVersion, info.dwMinorVersion));

    /* we are interested only in Vista (and newer versions...). In all
     * earlier versions UAC is not present. */
    if (info.dwMajorVersion < 6)
        return FALSE;

    /* the default EnableLUA value is 1 (Enabled) */
    DWORD dwEnableLUA = 1;

    HKEY hKey;
    LSTATUS lrc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                                0, KEY_QUERY_VALUE, &hKey);

    Assert(lrc == ERROR_SUCCESS || lrc == ERROR_PATH_NOT_FOUND);
    if (lrc == ERROR_SUCCESS)
    {

        DWORD cbEnableLUA = sizeof(dwEnableLUA);
        lrc = RegQueryValueExA(hKey, "EnableLUA", NULL, NULL, (LPBYTE) &dwEnableLUA, &cbEnableLUA);

        RegCloseKey(hKey);

        Assert(lrc == ERROR_SUCCESS || lrc == ERROR_FILE_NOT_FOUND);
    }

    LogFlowFunc(("lrc=%d, dwEnableLUA=%d\n", lrc, dwEnableLUA));

    return dwEnableLUA == 1;
}

/* end */

static int vboxNetWinAddComponent(std::list<ComObjPtr<HostNetworkInterface> > * pPist,
                                  INetCfgComponent * pncc, HostNetworkInterfaceType enmType,
                                  int iDefaultInterface)
{
    int vrc = VERR_GENERAL_FAILURE;

    LPWSTR lpszName;
    HRESULT hrc = pncc->GetDisplayName(&lpszName);
    Assert(hrc == S_OK);
    if (hrc == S_OK)
    {
        Bstr name(lpszName);

        GUID IfGuid;
        hrc = pncc->GetInstanceGuid(&IfGuid);
        Assert(hrc == S_OK);
        if (hrc == S_OK)
        {
            Guid guidIfCopy(IfGuid);
            NETIFINFO Info;
            RT_ZERO(Info);
            Info.Uuid = *guidIfCopy.raw();
            vrc = collectNetIfInfo(name, guidIfCopy, &Info, iDefaultInterface);
            if (RT_FAILURE(vrc))
                LogRelFunc(("collectNetIfInfo() -> %Rrc\n", vrc));
            LogFunc(("adding %ls\n", lpszName));
            /* create a new object and add it to the list */
            ComObjPtr<HostNetworkInterface> iface;
            iface.createObject();
            /* remove the curly bracket at the end */
            vrc = iface->init(name, enmType, &Info);
            if (SUCCEEDED(vrc))
            {
                if (Info.fIsDefault)
                    pPist->push_front(iface);
                else
                    pPist->push_back(iface);
            }
            else
            {
                LogRelFunc(("HostNetworkInterface::init() -> %Rrc\n", vrc));
                AssertComRC(vrc);
            }
        }
        else
            LogRelFunc(("failed to get device instance GUID (0x%x)\n", hrc));
        CoTaskMemFree(lpszName);
    }
    else
        LogRelFunc(("failed to get device display name (0x%x)\n", hrc));

    return vrc;
}

#endif /* VBOX_WITH_NETFLT */


static int netIfListHostAdapters(INetCfg *pNc, std::list<ComObjPtr<HostNetworkInterface> > &list)
{
#ifndef VBOX_WITH_NETFLT
    /* VBoxNetAdp is available only when VBOX_WITH_NETFLT is enabled */
    return VERR_NOT_IMPLEMENTED;
#else /* #  if defined VBOX_WITH_NETFLT */
    IEnumNetCfgComponent *pEnumComponent;
    HRESULT hrc = pNc->EnumComponents(&GUID_DEVCLASS_NET, &pEnumComponent);
    if (hrc == S_OK)
    {
        INetCfgComponent *pMpNcc;
        while ((hrc = pEnumComponent->Next(1, &pMpNcc, NULL)) == S_OK)
        {
            LPWSTR pwszName;
            ULONG uComponentStatus;
            hrc = pMpNcc->GetDisplayName(&pwszName);
            if (hrc == S_OK)
                LogFunc(("%ls\n", pwszName));
            else
                LogRelFunc(("failed to get device display name (0x%x)\n", hrc));
            hrc = pMpNcc->GetDeviceStatus(&uComponentStatus);
            if (hrc == S_OK)
            {
                if (uComponentStatus == 0)
                {
                    LPWSTR pId;
                    hrc = pMpNcc->GetId(&pId);
                    Assert(hrc == S_OK);
                    if (hrc == S_OK)
                    {
                        LogFunc(("id = %ls\n", pId));
                        if (!_wcsnicmp(pId, L"sun_VBoxNetAdp", sizeof(L"sun_VBoxNetAdp")/2))
                            vboxNetWinAddComponent(&list, pMpNcc, HostNetworkInterfaceType_HostOnly, -1);
                        CoTaskMemFree(pId);
                    }
                    else
                        LogRelFunc(("failed to get device id (0x%x)\n", hrc));
                }
            }
            else
                LogRelFunc(("failed to get device status (0x%x)\n", hrc));
            pMpNcc->Release();
        }
        Assert(hrc == S_OK || hrc == S_FALSE);

        pEnumComponent->Release();
    }
    else
        LogRelFunc(("EnumComponents error (0x%x)\n", hrc));
#endif /* #  if defined VBOX_WITH_NETFLT */
    return VINF_SUCCESS;
}

int NetIfGetConfig(HostNetworkInterface * pIf, NETIFINFO *pInfo)
{
#ifndef VBOX_WITH_NETFLT
    return VERR_NOT_IMPLEMENTED;
#else
    Bstr name;
    HRESULT hrc = pIf->COMGETTER(Name)(name.asOutParam());
    if (hrc == S_OK)
    {
        Bstr                IfGuid;
        hrc = pIf->COMGETTER(Id)(IfGuid.asOutParam());
        Assert(hrc == S_OK);
        if (hrc == S_OK)
        {
            memset(pInfo, 0, sizeof(NETIFINFO));
            Guid guid(IfGuid);
            pInfo->Uuid = *(guid.raw());

            return collectNetIfInfo(name, guid, pInfo, getDefaultInterfaceIndex());
        }
    }
    return VERR_GENERAL_FAILURE;
#endif
}

int NetIfGetConfigByName(PNETIFINFO)
{
    return VERR_NOT_IMPLEMENTED;
}

/**
 * Obtain the current state of the interface.
 *
 * @returns VBox status code.
 *
 * @param   pcszIfName  Interface name.
 * @param   penmState   Where to store the retrieved state.
 */
int NetIfGetState(const char *pcszIfName, NETIFSTATUS *penmState)
{
    RT_NOREF(pcszIfName, penmState);
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
    RT_NOREF(pcszIfName, puMbits);
    return VERR_NOT_IMPLEMENTED;
}

int NetIfCreateHostOnlyNetworkInterface(VirtualBox *pVirtualBox,
                                        IHostNetworkInterface **aHostNetworkInterface,
                                        IProgress **aProgress,
                                        IN_BSTR aName)
{
#ifndef VBOX_WITH_NETFLT
    return VERR_NOT_IMPLEMENTED;
#else
    /* create a progress object */
    ComObjPtr<Progress> progress;
    HRESULT hrc = progress.createObject();
    AssertComRCReturn(hrc, Global::vboxStatusCodeFromCOM(hrc));

    ComPtr<IHost> host;
    hrc = pVirtualBox->COMGETTER(Host)(host.asOutParam());
    if (SUCCEEDED(hrc))
    {
        hrc = progress->init(pVirtualBox, host,
                             Bstr(NetIfWin::tr("Creating host only network interface")).raw(),
                             FALSE /* aCancelable */);
        if (SUCCEEDED(hrc))
        {
            progress.queryInterfaceTo(aProgress);

            /* create a new uninitialized host interface object */
            ComObjPtr<HostNetworkInterface> iface;
            iface.createObject();
            iface.queryInterfaceTo(aHostNetworkInterface);

            /* create the networkInterfaceHelperClient() argument */
            NetworkInterfaceHelperClientData* d = new NetworkInterfaceHelperClientData();

            d->msgCode = SVCHlpMsg::CreateHostOnlyNetworkInterface;
            d->name = aName;
            d->iface = iface;
            d->ptrVBox = pVirtualBox;

            hrc = pVirtualBox->i_startSVCHelperClient(IsUACEnabled() == TRUE /* aPrivileged */,
                                                      netIfNetworkInterfaceHelperClient,
                                                      static_cast<void *>(d),
                                                      progress);
            /* d is now owned by netIfNetworkInterfaceHelperClient(), no need to delete one here */

        }
    }

    return Global::vboxStatusCodeFromCOM(hrc);
#endif
}

int NetIfRemoveHostOnlyNetworkInterface(VirtualBox *pVirtualBox, const Guid &aId,
                                        IProgress **aProgress)
{
#ifndef VBOX_WITH_NETFLT
    return VERR_NOT_IMPLEMENTED;
#else
    /* create a progress object */
    ComObjPtr<Progress> progress;
    HRESULT hrc = progress.createObject();
    AssertComRCReturn(hrc, Global::vboxStatusCodeFromCOM(hrc));

    ComPtr<IHost> host;
    hrc = pVirtualBox->COMGETTER(Host)(host.asOutParam());
    if (SUCCEEDED(hrc))
    {
        hrc = progress->init(pVirtualBox, host,
                             Bstr(NetIfWin::tr("Removing host network interface")).raw(),
                             FALSE /* aCancelable */);
        if (SUCCEEDED(hrc))
        {
            progress.queryInterfaceTo(aProgress);

            /* create the networkInterfaceHelperClient() argument */
            NetworkInterfaceHelperClientData* d = new NetworkInterfaceHelperClientData();

            d->msgCode = SVCHlpMsg::RemoveHostOnlyNetworkInterface;
            d->guid = aId;

            hrc = pVirtualBox->i_startSVCHelperClient(IsUACEnabled() == TRUE /* aPrivileged */,
                                                      netIfNetworkInterfaceHelperClient,
                                                      static_cast<void *>(d),
                                                      progress);
            /* d is now owned by netIfNetworkInterfaceHelperClient(), no need to delete one here */

        }
    }

    return Global::vboxStatusCodeFromCOM(hrc);
#endif
}

int NetIfEnableStaticIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf, ULONG aOldIp, ULONG ip, ULONG mask)
{
    RT_NOREF(aOldIp);
#ifndef VBOX_WITH_NETFLT
    return VERR_NOT_IMPLEMENTED;
#else
    Bstr guid;
    HRESULT hrc = pIf->COMGETTER(Id)(guid.asOutParam());
    if (SUCCEEDED(hrc))
    {
//        ComPtr<VirtualBox> pVBox;
//        hrc = pIf->getVirtualBox(pVBox.asOutParam());
//        if (SUCCEEDED(hrc))
        {
            /* create a progress object */
            ComObjPtr<Progress> progress;
            progress.createObject();
//            ComPtr<IHost> host;
//            HRESULT hrc = pVBox->COMGETTER(Host)(host.asOutParam());
//            if (SUCCEEDED(hrc))
            {
                hrc = progress->init(pVBox, (IHostNetworkInterface*)pIf,
                                     Bstr(NetIfWin::tr("Enabling Dynamic Ip Configuration")).raw(),
                                     FALSE /* aCancelable */);
                if (SUCCEEDED(hrc))
                {
                    if (FAILED(hrc)) return hrc;
//                    progress.queryInterfaceTo(aProgress);

                    /* create the networkInterfaceHelperClient() argument */
                    NetworkInterfaceHelperClientData* d = new NetworkInterfaceHelperClientData();

                    d->msgCode = SVCHlpMsg::EnableStaticIpConfig;
                    d->guid = Guid(guid);
                    d->iface = pIf;
                    d->u.StaticIP.IPAddress = ip;
                    d->u.StaticIP.IPNetMask = mask;

                    hrc = pVBox->i_startSVCHelperClient(IsUACEnabled() == TRUE /* aPrivileged */,
                                                        netIfNetworkInterfaceHelperClient,
                                                        static_cast<void *>(d),
                                                        progress);
                    /* d is now owned by netIfNetworkInterfaceHelperClient(), no need to delete one here */

                    if (SUCCEEDED(hrc))
                    {
                        progress->WaitForCompletion(-1);
                    }
                }
            }
        }
    }

    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
#endif
}

int NetIfEnableStaticIpConfigV6(VirtualBox *pVBox, HostNetworkInterface * pIf, const Utf8Str &aOldIPV6Address,
                                const Utf8Str &aIPV6Address, ULONG aIPV6MaskPrefixLength)
{
    RT_NOREF(aOldIPV6Address);
#ifndef VBOX_WITH_NETFLT
    return VERR_NOT_IMPLEMENTED;
#else
    Bstr guid;
    HRESULT hrc = pIf->COMGETTER(Id)(guid.asOutParam());
    if (SUCCEEDED(hrc))
    {
//        ComPtr<VirtualBox> pVBox;
//        hrc = pIf->getVirtualBox(pVBox.asOutParam());
//        if (SUCCEEDED(hrc))
        {
            /* create a progress object */
            ComObjPtr<Progress> progress;
            progress.createObject();
//            ComPtr<IHost> host;
//            HRESULT hrc = pVBox->COMGETTER(Host)(host.asOutParam());
//            if (SUCCEEDED(hrc))
            {
                hrc = progress->init(pVBox, (IHostNetworkInterface*)pIf,
                                     Bstr(NetIfWin::tr("Enabling Dynamic Ip Configuration")).raw(),
                                     FALSE /* aCancelable */);
                if (SUCCEEDED(hrc))
                {
                    if (FAILED(hrc)) return hrc;
//                    progress.queryInterfaceTo(aProgress);

                    /* create the networkInterfaceHelperClient() argument */
                    NetworkInterfaceHelperClientData* d = new NetworkInterfaceHelperClientData();

                    d->msgCode = SVCHlpMsg::EnableStaticIpConfigV6;
                    d->guid = guid;
                    d->iface = pIf;
                    d->u.StaticIPV6.IPV6Address = RTStrDup(aIPV6Address.c_str());
                    d->u.StaticIPV6.IPV6NetMaskLength = aIPV6MaskPrefixLength;

                    hrc = pVBox->i_startSVCHelperClient(IsUACEnabled() == TRUE /* aPrivileged */,
                                                        netIfNetworkInterfaceHelperClient,
                                                        static_cast<void *>(d),
                                                        progress);
                    /* d is now owned by netIfNetworkInterfaceHelperClient(), no need to delete one here */

                    if (SUCCEEDED(hrc))
                    {
                        progress->WaitForCompletion(-1);
                    }
                }
            }
        }
    }

    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
#endif
}

int NetIfEnableDynamicIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf)
{
#ifndef VBOX_WITH_NETFLT
    return VERR_NOT_IMPLEMENTED;
#else
    HRESULT hrc;
    Bstr guid;
    hrc = pIf->COMGETTER(Id)(guid.asOutParam());
    if (SUCCEEDED(hrc))
    {
//        ComPtr<VirtualBox> pVBox;
//        hrc = pIf->getVirtualBox(pVBox.asOutParam());
//        if (SUCCEEDED(hrc))
        {
            /* create a progress object */
            ComObjPtr<Progress> progress;
            progress.createObject();
//            ComPtr<IHost> host;
//            HRESULT hrc = pVBox->COMGETTER(Host)(host.asOutParam());
//            if (SUCCEEDED(hrc))
            {
                hrc = progress->init(pVBox, (IHostNetworkInterface*)pIf,
                                     Bstr(NetIfWin::tr("Enabling Dynamic Ip Configuration")).raw(),
                                     FALSE /* aCancelable */);
                if (SUCCEEDED(hrc))
                {
                    if (FAILED(hrc)) return hrc;
//                    progress.queryInterfaceTo(aProgress);

                    /* create the networkInterfaceHelperClient() argument */
                    NetworkInterfaceHelperClientData* d = new NetworkInterfaceHelperClientData();

                    d->msgCode = SVCHlpMsg::EnableDynamicIpConfig;
                    d->guid = guid;
                    d->iface = pIf;

                    hrc = pVBox->i_startSVCHelperClient(IsUACEnabled() == TRUE /* aPrivileged */,
                                                        netIfNetworkInterfaceHelperClient,
                                                        static_cast<void *>(d),
                                                        progress);
                    /* d is now owned by netIfNetworkInterfaceHelperClient(), no need to delete one here */

                    if (SUCCEEDED(hrc))
                    {
                        progress->WaitForCompletion(-1);
                    }
                }
            }
        }
    }

    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
#endif
}

int NetIfDhcpRediscover(VirtualBox *pVBox, HostNetworkInterface * pIf)
{
#ifndef VBOX_WITH_NETFLT
    return VERR_NOT_IMPLEMENTED;
#else
    HRESULT hrc;
    Bstr guid;
    hrc = pIf->COMGETTER(Id)(guid.asOutParam());
    if (SUCCEEDED(hrc))
    {
//        ComPtr<VirtualBox> pVBox;
//        hrc = pIf->getVirtualBox(pVBox.asOutParam());
//        if (SUCCEEDED(hrc))
        {
            /* create a progress object */
            ComObjPtr<Progress> progress;
            progress.createObject();
//            ComPtr<IHost> host;
//            HRESULT hrc = pVBox->COMGETTER(Host)(host.asOutParam());
//            if (SUCCEEDED(hrc))
            {
                hrc = progress->init(pVBox, (IHostNetworkInterface*)pIf,
                                     Bstr(NetIfWin::tr("Enabling Dynamic Ip Configuration")).raw(),
                                     FALSE /* aCancelable */);
                if (SUCCEEDED(hrc))
                {
                    if (FAILED(hrc)) return hrc;
//                    progress.queryInterfaceTo(aProgress);

                    /* create the networkInterfaceHelperClient() argument */
                    NetworkInterfaceHelperClientData* d = new NetworkInterfaceHelperClientData();

                    d->msgCode = SVCHlpMsg::DhcpRediscover;
                    d->guid = guid;
                    d->iface = pIf;

                    hrc = pVBox->i_startSVCHelperClient(IsUACEnabled() == TRUE /* aPrivileged */,
                                                        netIfNetworkInterfaceHelperClient,
                                                        static_cast<void *>(d),
                                                        progress);
                    /* d is now owned by netIfNetworkInterfaceHelperClient(), no need to delete one here */

                    if (SUCCEEDED(hrc))
                    {
                        progress->WaitForCompletion(-1);
                    }
                }
            }
        }
    }

    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
#endif
}


#define netIfLog LogFunc

struct BoundAdapter
{
    LPWSTR                pName;
    LPWSTR                pHwId;
    RTUUID                guid;
    PIP_ADAPTER_ADDRESSES pAdapter;
    BOOL                  fWireless;
};

static int netIfGetUnboundHostOnlyAdapters(INetCfg *pNetCfg, std::list<BoundAdapter> &adapters)
{
    IEnumNetCfgComponent *pEnumComponent;
    HRESULT hrc = pNetCfg->EnumComponents(&GUID_DEVCLASS_NET, &pEnumComponent);
    if (hrc != S_OK)
        LogRelFunc(("failed to enumerate network adapter components (0x%x)\n", hrc));
    else
    {
        INetCfgComponent *pMiniport;
        while ((hrc = pEnumComponent->Next(1, &pMiniport, NULL)) == S_OK)
        {
            GUID guid;
            ULONG uComponentStatus;
            struct BoundAdapter adapter;
            memset(&adapter, 0, sizeof(adapter));
            if ((hrc = pMiniport->GetDisplayName(&adapter.pName)) != S_OK)
                LogRelFunc(("failed to get device display name (0x%x)\n", hrc));
            else if ((hrc = pMiniport->GetDeviceStatus(&uComponentStatus)) != S_OK)
                netIfLog(("failed to get device status (0x%x)\n", hrc));
            else if (uComponentStatus != 0)
                netIfLog(("wrong device status (0x%x)\n", uComponentStatus));
            else if ((hrc = pMiniport->GetId(&adapter.pHwId)) != S_OK)
                LogRelFunc(("failed to get device id (0x%x)\n", hrc));
            else if (_wcsnicmp(adapter.pHwId, L"sun_VBoxNetAdp", sizeof(L"sun_VBoxNetAdp")/2))
                netIfLog(("not host-only id = %ls, ignored\n", adapter.pHwId));
            else if ((hrc = pMiniport->GetInstanceGuid(&guid)) != S_OK)
                LogRelFunc(("failed to get instance id (0x%x)\n", hrc));
            else
            {
                adapter.guid = *(Guid(guid).raw());
                netIfLog(("guid=%RTuuid, name=%ls id = %ls\n", &adapter.guid, adapter.pName, adapter.pHwId));
                adapters.push_back(adapter);
                adapter.pName = adapter.pHwId = NULL; /* do not free, will be done later */
            }
            if (adapter.pHwId)
                CoTaskMemFree(adapter.pHwId);
            if (adapter.pName)
                CoTaskMemFree(adapter.pName);
            pMiniport->Release();
        }
        Assert(hrc == S_OK || hrc == S_FALSE);

        pEnumComponent->Release();
    }
    netIfLog(("return\n"));
    return VINF_SUCCESS;
}

#define DEVNAME_PREFIX L"\\\\.\\"

static BOOL netIfIsWireless(INetCfgComponent *pAdapter)
{
    bool fWireless = false;

    /* Construct a device name. */
    LPWSTR  pwszBindName = NULL;
    HRESULT hrc = pAdapter->GetBindName(&pwszBindName);
    if (SUCCEEDED(hrc) && pwszBindName)
    {
        WCHAR wszFileName[MAX_PATH];
        int vrc = RTUtf16Copy(wszFileName, MAX_PATH, DEVNAME_PREFIX);
        if (RT_SUCCESS(vrc))
            vrc = RTUtf16Cat(wszFileName, MAX_PATH, pwszBindName);
        if (RT_SUCCESS(vrc))
        {
            /* open the device */
            HANDLE hDevice = CreateFileW(wszFileName,
                                         GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL,
                                         OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL,
                                         NULL);
            if (hDevice != INVALID_HANDLE_VALUE)
            {
                /* now issue the OID_GEN_PHYSICAL_MEDIUM query */
                DWORD Oid = OID_GEN_PHYSICAL_MEDIUM;
                NDIS_PHYSICAL_MEDIUM PhMedium = NdisPhysicalMediumUnspecified;
                DWORD cbResultIgn = 0;
                if (DeviceIoControl(hDevice,
                                    IOCTL_NDIS_QUERY_GLOBAL_STATS,
                                    &Oid,
                                    sizeof(Oid),
                                    &PhMedium,
                                    sizeof(PhMedium),
                                    &cbResultIgn,
                                    NULL))
                {
                    /* that was simple, now examine PhMedium */
                    fWireless = PhMedium == NdisPhysicalMediumWirelessWan
                             || PhMedium == NdisPhysicalMediumWirelessLan
                             || PhMedium == NdisPhysicalMediumNative802_11
                             || PhMedium == NdisPhysicalMediumBluetooth;
                }
                else
                {
                    DWORD rcWin = GetLastError();
                    LogRel(("netIfIsWireless: DeviceIoControl to '%ls' failed with rcWin=%u (%#x) - ignoring\n",
                            wszFileName, rcWin, rcWin));
                    Assert(rcWin == ERROR_INVALID_PARAMETER || rcWin == ERROR_NOT_SUPPORTED || rcWin == ERROR_BAD_COMMAND);
                }
                CloseHandle(hDevice);
            }
            else
            {
                DWORD rcWin = GetLastError();
#if 0 /* bird: Triggers on each VBoxSVC startup so, disabled.  Whoever want it, can enable using DEBUG_xxxx. */
                AssertLogRelMsgFailed(("netIfIsWireless: CreateFile on '%ls' failed with rcWin=%u (%#x) - ignoring\n",
                                       wszFileName, rcWin, rcWin));
#else
                LogRel(("netIfIsWireless: CreateFile on '%ls' failed with rcWin=%u (%#x) - ignoring\n",
                        wszFileName, rcWin, rcWin));
#endif
            }
        }
        CoTaskMemFree(pwszBindName);
    }
    else
        LogRel(("netIfIsWireless: GetBindName failed hrc=%Rhrc\n", hrc));

    return fWireless;
}

static HRESULT netIfGetBoundAdapters(std::list<BoundAdapter> &boundAdapters)
{

    netIfLog(("building the list of interfaces\n"));
    /* we are using the INetCfg API for getting the list of miniports */
    INetCfg  *pNetCfg = NULL;
    LPWSTR    lpszApp = NULL;
    HRESULT hrc = VBoxNetCfgWinQueryINetCfg(&pNetCfg, FALSE, VBOX_APP_NAME, 10000, &lpszApp);
    Assert(hrc == S_OK);
    if (hrc != S_OK)
    {
        LogRelFunc(("failed to query INetCfg (0x%x)\n", hrc));
        return hrc;
    }

    INetCfgComponent *pFilter;
    if ((hrc = pNetCfg->FindComponent(L"oracle_VBoxNetLwf", &pFilter)) != S_OK
        /* fall back to NDIS5 miniport lookup */
        && (hrc = pNetCfg->FindComponent(L"sun_VBoxNetFlt", &pFilter)))
        LogRelFunc(("could not find either 'oracle_VBoxNetLwf' or 'sun_VBoxNetFlt' components (0x%x)\n", hrc));
    else
    {
        INetCfgComponentBindings *pFilterBindings;
        if ((pFilter->QueryInterface(IID_INetCfgComponentBindings, (PVOID*)&pFilterBindings)) != S_OK)
            LogRelFunc(("failed to query INetCfgComponentBindings (0x%x)\n", hrc));
        else
        {
            IEnumNetCfgBindingPath *pEnumBp;
            INetCfgBindingPath     *pBp;
            if ((pFilterBindings->EnumBindingPaths(EBP_BELOW, &pEnumBp)) != S_OK)
                LogRelFunc(("failed to enumerate binding paths (0x%x)\n", hrc));
            else
            {
                pEnumBp->Reset();
                while ((hrc = pEnumBp->Next(1, &pBp, NULL)) == S_OK)
                {
                    IEnumNetCfgBindingInterface *pEnumBi;
                    INetCfgBindingInterface     *pBi;
                    if (pBp->IsEnabled() != S_OK)
                    {
                        /** @todo some id of disabled path could be useful. */
                        netIfLog(("INetCfgBindingPath is disabled (0x%x)\n", hrc));
                        pBp->Release();
                        continue;
                    }
                    if ((pBp->EnumBindingInterfaces(&pEnumBi)) != S_OK)
                        LogRelFunc(("failed to enumerate binding interfaces (0x%x)\n", hrc));
                    else
                    {
                        hrc = pEnumBi->Reset();
                        while ((hrc = pEnumBi->Next(1, &pBi, NULL)) == S_OK)
                        {
                            INetCfgComponent *pAdapter;
                            if ((hrc = pBi->GetLowerComponent(&pAdapter)) != S_OK)
                                LogRelFunc(("failed to get lower component (0x%x)\n", hrc));
                            else
                            {
                                LPWSTR pwszName = NULL;
                                if ((hrc = pAdapter->GetDisplayName(&pwszName)) != S_OK)
                                    LogRelFunc(("failed to get display name (0x%x)\n", hrc));
                                else
                                {
                                    ULONG uStatus;
                                    DWORD dwChars;
                                    if ((hrc = pAdapter->GetDeviceStatus(&uStatus)) != S_OK)
                                        netIfLog(("%ls: failed to get device status (0x%x)\n",
                                                  pwszName, hrc));
                                    else if ((hrc = pAdapter->GetCharacteristics(&dwChars)) != S_OK)
                                        netIfLog(("%ls: failed to get device characteristics (0x%x)\n",
                                                  pwszName, hrc));
                                    else if (uStatus != 0)
                                        netIfLog(("%ls: wrong status 0x%x\n",
                                                  pwszName, uStatus));
                                    else if (dwChars & NCF_HIDDEN)
                                        netIfLog(("%ls: wrong characteristics 0x%x\n",
                                                  pwszName, dwChars));
                                    else
                                    {
                                        GUID guid;
                                        LPWSTR pwszHwId = NULL;
                                        if ((hrc = pAdapter->GetId(&pwszHwId)) != S_OK)
                                            LogRelFunc(("%ls: failed to get hardware id (0x%x)\n",
                                                      pwszName, hrc));
                                        else if (!_wcsnicmp(pwszHwId, L"sun_VBoxNetAdp", sizeof(L"sun_VBoxNetAdp")/2))
                                            netIfLog(("host-only adapter %ls, ignored\n", pwszName));
                                        else if ((hrc = pAdapter->GetInstanceGuid(&guid)) != S_OK)
                                            LogRelFunc(("%ls: failed to get instance GUID (0x%x)\n",
                                                      pwszName, hrc));
                                        else
                                        {
                                            struct BoundAdapter adapter;
                                            adapter.pName    = pwszName;
                                            adapter.pHwId    = pwszHwId;
                                            adapter.guid     = *(Guid(guid).raw());
                                            adapter.pAdapter = NULL;
                                            adapter.fWireless = netIfIsWireless(pAdapter);
                                            netIfLog(("guid=%RTuuid, name=%ls, hwid=%ls, status=%x, chars=%x\n",
                                                      &adapter.guid, pwszName, pwszHwId, uStatus, dwChars));
                                            boundAdapters.push_back(adapter);
                                            pwszName = pwszHwId = NULL; /* do not free, will be done later */
                                        }
                                        if (pwszHwId)
                                            CoTaskMemFree(pwszHwId);
                                    }
                                    if (pwszName)
                                        CoTaskMemFree(pwszName);
                                }

                                pAdapter->Release();
                            }
                            pBi->Release();
                        }
                        pEnumBi->Release();
                    }
                    pBp->Release();
                }
                pEnumBp->Release();
            }
            pFilterBindings->Release();
        }
        pFilter->Release();
    }
    /* Host-only adapters are not necessarily bound, add them separately. */
    netIfGetUnboundHostOnlyAdapters(pNetCfg, boundAdapters);
    VBoxNetCfgWinReleaseINetCfg(pNetCfg, FALSE);

    return S_OK;
}

#if 0
static HRESULT netIfGetBoundAdaptersFallback(std::list<BoundAdapter> &boundAdapters)
{
    return CO_E_NOT_SUPPORTED;
}
#endif

/**
 * Walk through the list of adpater addresses and extract the required
 * information. XP and older don't not have the OnLinkPrefixLength field.
 */
static void netIfFillInfoWithAddressesXp(PNETIFINFO pInfo, PIP_ADAPTER_ADDRESSES pAdapter)
{
    PIP_ADAPTER_UNICAST_ADDRESS pAddr;
    bool fIPFound = false;
    bool fIPv6Found = false;
    for (pAddr = pAdapter->FirstUnicastAddress; pAddr; pAddr = pAddr->Next)
    {
        switch (pAddr->Address.lpSockaddr->sa_family)
        {
            case AF_INET:
                if (!fIPFound)
                {
                    fIPFound = true;
                    memcpy(&pInfo->IPAddress,
                           &((struct sockaddr_in *)pAddr->Address.lpSockaddr)->sin_addr.s_addr,
                           sizeof(pInfo->IPAddress));
                }
                break;
            case AF_INET6:
                if (!fIPv6Found)
                {
                    fIPv6Found = true;
                    memcpy(&pInfo->IPv6Address,
                           ((struct sockaddr_in6 *)pAddr->Address.lpSockaddr)->sin6_addr.s6_addr,
                           sizeof(pInfo->IPv6Address));
                }
                break;
        }
    }
    PIP_ADAPTER_PREFIX pPrefix;
    ULONG uPrefixLenV4 = 0;
    ULONG uPrefixLenV6 = 0;
    for (pPrefix = pAdapter->FirstPrefix; pPrefix && !(uPrefixLenV4 && uPrefixLenV6); pPrefix = pPrefix->Next)
    {
        switch (pPrefix->Address.lpSockaddr->sa_family)
        {
            case AF_INET:
                if (!uPrefixLenV4)
                {
                    ULONG ip = ((PSOCKADDR_IN)(pPrefix->Address.lpSockaddr))->sin_addr.s_addr;
                    netIfLog(("prefix=%RTnaipv4 len=%u\n", ip, pPrefix->PrefixLength));
                    if (   pPrefix->PrefixLength < sizeof(pInfo->IPNetMask) * 8
                        && pPrefix->PrefixLength > 0
                        && (ip & 0xF0) < 224)
                    {
                        uPrefixLenV4 = pPrefix->PrefixLength;
                        RTNetPrefixToMaskIPv4(pPrefix->PrefixLength, &pInfo->IPNetMask);
                    }
                    else
                        netIfLog(("Unexpected IPv4 prefix length of %d\n",
                             pPrefix->PrefixLength));
                }
                break;
            case AF_INET6:
                if (!uPrefixLenV6)
                {
                    PBYTE ipv6 = ((PSOCKADDR_IN6)(pPrefix->Address.lpSockaddr))->sin6_addr.s6_addr;
                    netIfLog(("prefix=%RTnaipv6 len=%u\n", ipv6, pPrefix->PrefixLength));
                    if (   pPrefix->PrefixLength < sizeof(pInfo->IPv6NetMask) * 8
                        && pPrefix->PrefixLength > 0
                        && ipv6[0] != 0xFF)
                    {
                        uPrefixLenV6 = pPrefix->PrefixLength;
                        RTNetPrefixToMaskIPv6(pPrefix->PrefixLength, &pInfo->IPv6NetMask);
                    }
                    else
                        netIfLog(("Unexpected IPv6 prefix length of %d\n", pPrefix->PrefixLength));
                }
                break;
        }
    }
    netIfLog(("%RTnaipv4/%u\n", pInfo->IPAddress, uPrefixLenV4));
    netIfLog(("%RTnaipv6/%u\n", &pInfo->IPv6Address, uPrefixLenV6));
}

/**
 * Walk through the list of adpater addresses and extract the required
 * information. XP and older don't not have the OnLinkPrefixLength field.
 */
static void netIfFillInfoWithAddressesVista(PNETIFINFO pInfo, PIP_ADAPTER_ADDRESSES pAdapter)
{
    PIP_ADAPTER_UNICAST_ADDRESS pAddr;

    if (sizeof(pInfo->MACAddress) != pAdapter->PhysicalAddressLength)
        netIfLog(("Unexpected physical address length: %u\n", pAdapter->PhysicalAddressLength));
    else
        memcpy(pInfo->MACAddress.au8, pAdapter->PhysicalAddress, sizeof(pInfo->MACAddress));

    bool fIPFound = false;
    bool fIPv6Found = false;
    for (pAddr = pAdapter->FirstUnicastAddress; pAddr; pAddr = pAddr->Next)
    {
        PIP_ADAPTER_UNICAST_ADDRESS_LH pAddrLh = (PIP_ADAPTER_UNICAST_ADDRESS_LH)pAddr;
        switch (pAddrLh->Address.lpSockaddr->sa_family)
        {
            case AF_INET:
                if (!fIPFound)
                {
                    fIPFound = true;
                    memcpy(&pInfo->IPAddress,
                           &((struct sockaddr_in *)pAddrLh->Address.lpSockaddr)->sin_addr.s_addr,
                           sizeof(pInfo->IPAddress));
                    if (pAddrLh->OnLinkPrefixLength > 32)
                        netIfLog(("Invalid IPv4 prefix length of %d\n", pAddrLh->OnLinkPrefixLength));
                    else
                        RTNetPrefixToMaskIPv4(pAddrLh->OnLinkPrefixLength, &pInfo->IPNetMask);
                }
                break;
            case AF_INET6:
                if (!fIPv6Found)
                {
                    fIPv6Found = true;
                    memcpy(&pInfo->IPv6Address,
                           ((struct sockaddr_in6 *)pAddrLh->Address.lpSockaddr)->sin6_addr.s6_addr,
                           sizeof(pInfo->IPv6Address));
                    if (pAddrLh->OnLinkPrefixLength > 128)
                        netIfLog(("Invalid IPv6 prefix length of %d\n", pAddrLh->OnLinkPrefixLength));
                    else
                        RTNetPrefixToMaskIPv6(pAddrLh->OnLinkPrefixLength, &pInfo->IPv6NetMask);
                }
                break;
        }
    }

    if (fIPFound)
    {
        int iPrefixIPv4 = -1;
        RTNetMaskToPrefixIPv4(&pInfo->IPNetMask, &iPrefixIPv4);
        netIfLog(("%RTnaipv4/%u\n", pInfo->IPAddress, iPrefixIPv4));
    }
    if (fIPv6Found)
    {
        int iPrefixIPv6 = -1;
        RTNetMaskToPrefixIPv6(&pInfo->IPv6NetMask, &iPrefixIPv6);
        netIfLog(("%RTnaipv6/%u\n", &pInfo->IPv6Address, iPrefixIPv6));
    }
}

#if (NTDDI_VERSION >= NTDDI_VISTA)
#define NETIF_GAA_FLAGS GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
#else /* (NTDDI_VERSION < NTDDI_VISTA) */
#define NETIF_GAA_FLAGS GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
#endif /* (NTDDI_VERSION < NTDDI_VISTA) */

int NetIfList(std::list<ComObjPtr<HostNetworkInterface> > &list)
{
    int iDefault = getDefaultInterfaceIndex();
    /* MSDN recommends to pre-allocate a 15KB buffer. */
    ULONG uBufLen = 15 * 1024;
    PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)RTMemAlloc(uBufLen);
    if (!pAddresses)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
    DWORD dwRc = GetAdaptersAddresses(AF_UNSPEC, NETIF_GAA_FLAGS, NULL, pAddresses, &uBufLen);
    for (int tries = 0; tries < 3 && dwRc == ERROR_BUFFER_OVERFLOW; ++tries)
    {
        /* Get more memory and try again. */
        RTMemFree(pAddresses);
        pAddresses = (PIP_ADAPTER_ADDRESSES)RTMemAlloc(uBufLen);
        if (!pAddresses)
            return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
        dwRc = GetAdaptersAddresses(AF_UNSPEC, NETIF_GAA_FLAGS, NULL, pAddresses, &uBufLen);
    }
    HRESULT hrc;
    if (dwRc != NO_ERROR)
    {
        LogRelFunc(("GetAdaptersAddresses failed (0x%x)\n", dwRc));
        hrc = HRESULT_FROM_WIN32(dwRc);
    }
    else
    {
        std::list<BoundAdapter> boundAdapters;
        hrc = netIfGetBoundAdapters(boundAdapters);
#if 0
        if (hrc != S_OK)
            hrc = netIfGetBoundAdaptersFallback(boundAdapters);
#endif
        if (hrc != S_OK)
            LogRelFunc(("netIfGetBoundAdapters failed (0x%x)\n", hrc));
        else
        {
            PIP_ADAPTER_ADDRESSES pAdapter;

            for (pAdapter = pAddresses; pAdapter; pAdapter = pAdapter->Next)
            {
                char *pszUuid = RTStrDup(pAdapter->AdapterName);
                if (!pszUuid)
                {
                    LogRelFunc(("out of memory\n"));
                    break;
                }
                size_t len = strlen(pszUuid) - 1;
                if (pszUuid[0] != '{' || pszUuid[len] != '}')
                    LogRelFunc(("ignoring invalid GUID %s\n", pAdapter->AdapterName));
                else
                {
                    std::list<BoundAdapter>::iterator it;
                    pszUuid[len] = 0;
                    for (it = boundAdapters.begin(); it != boundAdapters.end(); ++it)
                    {
                        if (!RTUuidCompareStr(&(*it).guid, pszUuid + 1))
                        {
                            (*it).pAdapter = pAdapter;
                            break;
                        }
                    }
                }
                RTStrFree(pszUuid);
            }
            std::list<BoundAdapter>::iterator it;
            for (it = boundAdapters.begin(); it != boundAdapters.end(); ++it)
            {
                NETIFINFO info;
                memset(&info, 0, sizeof(info));
                info.Uuid = (*it).guid;
                info.enmMediumType = NETIF_T_ETHERNET;
                info.fWireless = (*it).fWireless;
                pAdapter = (*it).pAdapter;
                if (pAdapter)
                {
                    info.enmStatus = pAdapter->OperStatus == IfOperStatusUp ? NETIF_S_UP : NETIF_S_DOWN;
                    info.fIsDefault = (pAdapter->IfIndex == (DWORD)iDefault);
                    info.fDhcpEnabled = pAdapter->Flags & IP_ADAPTER_DHCP_ENABLED;
                    OSVERSIONINFOEX OSInfoEx;
                    RT_ZERO(OSInfoEx);
                    OSInfoEx.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
                    if (   GetVersionEx((LPOSVERSIONINFO)&OSInfoEx)
                        && OSInfoEx.dwMajorVersion < 6)
                        netIfFillInfoWithAddressesXp(&info, pAdapter);
                    else
                        netIfFillInfoWithAddressesVista(&info, pAdapter);
                }
                else
                    info.enmStatus = NETIF_S_DOWN;
                /* create a new object and add it to the list */
                ComObjPtr<HostNetworkInterface> iface;
                iface.createObject();
                HostNetworkInterfaceType enmType = _wcsnicmp((*it).pHwId, L"sun_VBoxNetAdp", sizeof(L"sun_VBoxNetAdp") / 2)
                                                 ? HostNetworkInterfaceType_Bridged : HostNetworkInterfaceType_HostOnly;
                netIfLog(("Adding %ls as %s\n", (*it).pName,
                        enmType == HostNetworkInterfaceType_Bridged ? "bridged" :
                        enmType == HostNetworkInterfaceType_HostOnly ? "host-only" : "unknown"));
                hrc = iface->init((*it).pName, enmType, &info);
                if (FAILED(hrc))
                    LogRelFunc(("HostNetworkInterface::init() -> %Rrc\n", hrc));
                else
                {
                    if (info.fIsDefault)
                        list.push_front(iface);
                    else
                        list.push_back(iface);
                }
                if ((*it).pHwId)
                    CoTaskMemFree((*it).pHwId);
                if ((*it).pName)
                    CoTaskMemFree((*it).pName);
            }
        }
    }
    RTMemFree(pAddresses);

    return hrc;
}
