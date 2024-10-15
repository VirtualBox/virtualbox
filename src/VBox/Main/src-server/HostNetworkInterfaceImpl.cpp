/* $Id: HostNetworkInterfaceImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOSTNETWORKINTERFACE
#include "HostNetworkInterfaceImpl.h"
#include "AutoCaller.h"
#include "netif.h"
#ifdef VBOX_WITH_RESOURCE_USAGE_API
# include "Performance.h"
# include "PerformanceImpl.h"
#endif
#include "LoggingNew.h"

#include <iprt/cpp/utils.h>

#ifdef RT_OS_FREEBSD
# include <netinet/in.h> /* INADDR_NONE */
#endif /* RT_OS_FREEBSD */

#include "VirtualBoxImpl.h"

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

HostNetworkInterface::HostNetworkInterface()
    : mVirtualBox(NULL)
{
}

HostNetworkInterface::~HostNetworkInterface()
{
}

HRESULT HostNetworkInterface::FinalConstruct()
{
    return BaseFinalConstruct();
}

void HostNetworkInterface::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the host object.
 *
 * @returns COM result indicator
 * @param   aInterfaceName name of the network interface
 * @param   aShortName  short name of the network interface
 * @param   aGuid       GUID of the host network interface
 * @param   ifType      interface type
 */
HRESULT HostNetworkInterface::init(Utf8Str aInterfaceName, Utf8Str aShortName, Guid aGuid, HostNetworkInterfaceType_T ifType)
{
    LogFlowThisFunc(("aInterfaceName={%s}, aGuid={%s}\n",
                      aInterfaceName.c_str(), aGuid.toString().c_str()));

    ComAssertRet(!aInterfaceName.isEmpty(), E_INVALIDARG);
    ComAssertRet(aGuid.isValid(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mInterfaceName) = aInterfaceName;
#ifdef VBOX_WITH_HOSTNETIF_API
    unconst(mNetworkName) = i_composeNetworkName(aShortName);
#endif
    unconst(mShortName) = aShortName;
    unconst(mGuid) = aGuid;
    mIfType = ifType;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

#ifdef VBOX_WITH_RESOURCE_USAGE_API

void HostNetworkInterface::i_registerMetrics(PerformanceCollector *aCollector, ComPtr<IUnknown> objptr)
{
    LogFlowThisFunc(("mShortName={%s}, mInterfaceName={%s}, mGuid={%s}, mSpeedMbits=%u\n",
                     mShortName.c_str(), mInterfaceName.c_str(), mGuid.toString().c_str(), m.speedMbits));
    pm::CollectorHAL *hal = aCollector->getHAL();
    /* Create sub metrics */
    Utf8StrFmt strName("Net/%s", mShortName.c_str());
    pm::SubMetric *networkLoadRx   = new pm::SubMetric(strName + "/Load/Rx",
        "Percentage of network interface receive bandwidth used.");
    pm::SubMetric *networkLoadTx   = new pm::SubMetric(strName + "/Load/Tx",
        "Percentage of network interface transmit bandwidth used.");
    pm::SubMetric *networkLinkSpeed = new pm::SubMetric(strName + "/LinkSpeed",
        "Physical link speed.");

    /* Create and register base metrics */
    pm::BaseMetric *networkSpeed = new pm::HostNetworkSpeed(hal, objptr, strName + "/LinkSpeed",
                                                            Utf8Str(mShortName), Utf8Str(mInterfaceName),
                                                            m.speedMbits, networkLinkSpeed);
    aCollector->registerBaseMetric(networkSpeed);
    pm::BaseMetric *networkLoad = new pm::HostNetworkLoadRaw(hal, objptr, strName + "/Load",
                                                             Utf8Str(mShortName), Utf8Str(mInterfaceName),
                                                             m.speedMbits, networkLoadRx, networkLoadTx);
    aCollector->registerBaseMetric(networkLoad);

    aCollector->registerMetric(new pm::Metric(networkSpeed, networkLinkSpeed, 0));
    aCollector->registerMetric(new pm::Metric(networkSpeed, networkLinkSpeed,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(networkSpeed, networkLinkSpeed,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(networkSpeed, networkLinkSpeed,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadRx, 0));
    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadRx,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadRx,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadRx,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadTx, 0));
    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadTx,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadTx,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(networkLoad, networkLoadTx,
                                              new pm::AggregateMax()));
}

void HostNetworkInterface::i_unregisterMetrics(PerformanceCollector *aCollector, ComPtr<IUnknown> objptr)
{
    LogFlowThisFunc(("mShortName={%s}, mInterfaceName={%s}, mGuid={%s}\n",
                     mShortName.c_str(), mInterfaceName.c_str(), mGuid.toString().c_str()));
    Utf8StrFmt name("Net/%s", mShortName.c_str());
    aCollector->unregisterMetricsFor(objptr, name + "/*");
    aCollector->unregisterBaseMetricsFor(objptr, name);
}

#endif /* VBOX_WITH_RESOURCE_USAGE_API */

#ifdef VBOX_WITH_HOSTNETIF_API
#if defined(RT_OS_WINDOWS)

HRESULT HostNetworkInterface::saveAdapterConfigParameter(const char *szParamName, const Utf8Str &strValue)
{
    AssertReturn(mVirtualBox != NULL, E_POINTER);
    return mVirtualBox->SetExtraData(BstrFmt("HostOnly/{%RTuuid}/%s", mGuid.raw(), szParamName).raw(), Bstr(strValue).raw());
}

HRESULT HostNetworkInterface::eraseAdapterConfigParameter(const char *szParamName)
{
    AssertReturn(mVirtualBox != NULL, E_POINTER);
    return mVirtualBox->SetExtraData(BstrFmt("HostOnly/{%RTuuid}/%s", mGuid.raw(), szParamName).raw(), NULL);
}

HRESULT HostNetworkInterface::saveAdapterConfigIPv4Dhcp()
{
    HRESULT hrc = saveAdapterConfigParameter("IPAddress", "DHCP");
    if (hrc == S_OK)
        hrc = eraseAdapterConfigParameter("IPNetMask");
    return hrc;
}

HRESULT HostNetworkInterface::saveAdapterConfigIPv4(ULONG addr, ULONG mask)
{
    HRESULT hrc = saveAdapterConfigParameter("IPAddress", Utf8StrFmt("%RTnaipv4", addr));
    if (hrc == S_OK)
        hrc = saveAdapterConfigParameter("IPNetMask", Utf8StrFmt("%RTnaipv4", mask));
    return hrc;
}

HRESULT HostNetworkInterface::saveAdapterConfigIPv6(const Utf8Str& addr, ULONG prefix)
{
    HRESULT hrc = saveAdapterConfigParameter("IPV6Address", addr);
    if (hrc == S_OK)
        hrc = saveAdapterConfigParameter("IPV6PrefixLen", Utf8StrFmt("%u", prefix));
    return hrc;
}

bool HostNetworkInterface::isInConfigFile(void)
{
    /* We care about host-only adapters only */
    if (mIfType != HostNetworkInterfaceType_HostOnly)
        return true;

    Assert(mVirtualBox != NULL);
    if (mVirtualBox == NULL)
        return false; /* Trigger config update, which will fail with proper return code */
    Bstr tmpName;
    mVirtualBox->GetExtraData(BstrFmt("HostOnly/{%RTuuid}/Name", mGuid.raw()).raw(), tmpName.asOutParam());
    return (tmpName.isNotEmpty() && tmpName == mInterfaceName);

}

HRESULT HostNetworkInterface::saveAdapterConfig(void)
{
    /* We care about host-only adapters only */
    if (mIfType != HostNetworkInterfaceType_HostOnly)
        return true;

    HRESULT hrc = saveAdapterConfigParameter("Name", mInterfaceName.c_str());
    if (FAILED(hrc))
        return hrc;
    if (m.dhcpEnabled)
        hrc = saveAdapterConfigIPv4Dhcp();
    else
        hrc = saveAdapterConfigIPv4(m.IPAddress, m.networkMask);
    if (SUCCEEDED(hrc))
        hrc = saveAdapterConfigIPv6(m.IPV6Address.c_str(), m.IPV6NetworkMaskPrefixLength);
    return hrc;
}

HRESULT HostNetworkInterface::i_updatePersistentConfig(void)
{
    if (mVirtualBox == NULL)
        return E_POINTER;

    HRESULT hrc = S_OK;
    if (!isInConfigFile())
    {
        hrc = saveAdapterConfig();
    }
    return hrc;
}

#endif /* defined(RT_OS_WINDOWS) */

HRESULT HostNetworkInterface::updateConfig()
{
    NETIFINFO info;
    int vrc = NetIfGetConfig(this, &info);
    if (RT_SUCCESS(vrc))
    {
        int iPrefixIPv6;

        m.realIPAddress = m.IPAddress = info.IPAddress.u;
        m.realNetworkMask = m.networkMask = info.IPNetMask.u;
        m.dhcpEnabled = info.fDhcpEnabled;
        if (info.IPv6Address.s.Lo || info.IPv6Address.s.Hi)
            m.realIPV6Address = m.IPV6Address = Utf8StrFmt("%RTnaipv6", &info.IPv6Address);
        else
            m.realIPV6Address = m.IPV6Address = Utf8Str::Empty;
        RTNetMaskToPrefixIPv6(&info.IPv6NetMask, &iPrefixIPv6);
        m.realIPV6PrefixLength = m.IPV6NetworkMaskPrefixLength = (ULONG)iPrefixIPv6;
        m.hardwareAddress = Utf8StrFmt("%RTmac", &info.MACAddress);
        AssertCompile((unsigned)NETIF_T_UNKNOWN == (unsigned)HostNetworkInterfaceMediumType_Unknown);
        m.mediumType = (HostNetworkInterfaceMediumType_T)info.enmMediumType;
        AssertCompile((unsigned)NETIF_S_UNKNOWN == (unsigned)HostNetworkInterfaceStatus_Unknown);
        m.status = (HostNetworkInterfaceStatus_T)info.enmStatus;
        m.speedMbits = info.uSpeedMbits;
        m.wireless = info.fWireless;
        return S_OK;
    }
    return vrc == VERR_NOT_IMPLEMENTED ? E_NOTIMPL : E_FAIL;
}

Utf8Str HostNetworkInterface::i_composeNetworkName(const Utf8Str aShortName)
{
    return Utf8Str("HostInterfaceNetworking-").append(aShortName);
}
/**
 * Initializes the host object.
 *
 * @returns COM result indicator
 * @param   aInterfaceName name of the network interface
 * @param   aGuid GUID of the host network interface
 */
HRESULT HostNetworkInterface::init(Utf8Str aInterfaceName, HostNetworkInterfaceType_T ifType, PNETIFINFO pIf)
{
//    LogFlowThisFunc(("aInterfaceName={%s}, aGuid={%s}\n",
//                      aInterfaceName.c_str(), aGuid.toString().c_str()));

//    ComAssertRet(aInterfaceName, E_INVALIDARG);
//    ComAssertRet(aGuid.isValid(), E_INVALIDARG);
    ComAssertRet(pIf, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mInterfaceName) = aInterfaceName;
    unconst(mGuid) = pIf->Uuid;
    if (pIf->szShortName[0])
    {
        unconst(mNetworkName) = i_composeNetworkName(pIf->szShortName);
        unconst(mShortName)   = pIf->szShortName;
    }
    else
    {
        unconst(mNetworkName) = i_composeNetworkName(aInterfaceName);
        unconst(mShortName)   = aInterfaceName;
    }
    mIfType = ifType;

    int iPrefixIPv6;

    m.realIPAddress = m.IPAddress = pIf->IPAddress.u;
    m.realNetworkMask = m.networkMask = pIf->IPNetMask.u;
    if (pIf->IPv6Address.s.Lo || pIf->IPv6Address.s.Hi)
        m.realIPV6Address = m.IPV6Address = Utf8StrFmt("%RTnaipv6", &pIf->IPv6Address);
    else
        m.realIPV6Address = m.IPV6Address = Utf8Str::Empty;
    RTNetMaskToPrefixIPv6(&pIf->IPv6NetMask, &iPrefixIPv6);
    m.realIPV6PrefixLength = m.IPV6NetworkMaskPrefixLength = (ULONG)iPrefixIPv6;
    m.dhcpEnabled = pIf->fDhcpEnabled;
    m.hardwareAddress = Utf8StrFmt("%RTmac", &pIf->MACAddress);
    AssertCompile((unsigned)NETIF_T_UNKNOWN == (unsigned)HostNetworkInterfaceMediumType_Unknown);
    m.mediumType = (HostNetworkInterfaceMediumType_T)pIf->enmMediumType;
    AssertCompile((unsigned)NETIF_S_UNKNOWN == (unsigned)HostNetworkInterfaceStatus_Unknown);
    m.status = (HostNetworkInterfaceStatus_T)pIf->enmStatus;
    m.speedMbits = pIf->uSpeedMbits;
    m.wireless = pIf->fWireless;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

#endif /* VBOX_WITH_HOSTNETIF_API */

// wrapped IHostNetworkInterface properties
/////////////////////////////////////////////////////////////////////////////
/**
 * Returns the name of the host network interface.
 *
 * @returns COM status code
 * @param   aInterfaceName - Interface Name
 */

HRESULT HostNetworkInterface::getName(com::Utf8Str &aInterfaceName)
{
    aInterfaceName = mInterfaceName;
    return S_OK;
}

/**
 * Returns the short name of the host network interface.
 *
 * @returns  COM status code
 * @param   aShortName Short Name
 */

HRESULT HostNetworkInterface::getShortName(com::Utf8Str &aShortName)
{
    aShortName = mShortName;

    return S_OK;
}

/**
 * Returns the GUID of the host network interface.
 *
 * @returns COM status code
 * @param   aGuid GUID
 */
HRESULT HostNetworkInterface::getId(com::Guid &aGuid)
{
    aGuid = mGuid;

    return S_OK;
}

HRESULT HostNetworkInterface::getDHCPEnabled(BOOL *aDHCPEnabled)
{
    *aDHCPEnabled = m.dhcpEnabled;

    return S_OK;
}


/**
 * Returns the IP address of the host network interface.
 *
 * @returns COM status code
 * @param   aIPAddress  Address name
 */
HRESULT HostNetworkInterface::getIPAddress(com::Utf8Str &aIPAddress)
{
    in_addr tmp;
#if defined(RT_OS_WINDOWS)
    tmp.S_un.S_addr = m.IPAddress;
#else
    tmp.s_addr = m.IPAddress;
#endif
    char *addr = inet_ntoa(tmp);
    if (addr)
    {
        aIPAddress = addr;
        return S_OK;
    }

    return E_FAIL;
}

/**
 * Returns the netwok mask of the host network interface.
 *
 * @returns COM status code
 * @param   aNetworkMask name.
 */
HRESULT HostNetworkInterface::getNetworkMask(com::Utf8Str &aNetworkMask)
{

    in_addr tmp;
#if defined(RT_OS_WINDOWS)
    tmp.S_un.S_addr = m.networkMask;
#else
    tmp.s_addr = m.networkMask;
#endif
    char *addr = inet_ntoa(tmp);
    if (addr)
    {
        aNetworkMask = Utf8Str(addr);
        return S_OK;
    }

    return E_FAIL;
}

HRESULT HostNetworkInterface::getIPV6Supported(BOOL *aIPV6Supported)
{
#if defined(RT_OS_WINDOWS)
    *aIPV6Supported = FALSE;
#else
    *aIPV6Supported = TRUE;
#endif

    return S_OK;
}

/**
 * Returns the IP V6 address of the host network interface.
 *
 * @returns COM status code
 * @param   aIPV6Address
 */
HRESULT HostNetworkInterface::getIPV6Address(com::Utf8Str &aIPV6Address)
{
    aIPV6Address = m.IPV6Address;
    return S_OK;
}

/**
 * Returns the IP V6 network mask prefix length of the host network interface.
 *
 * @returns COM status code
 * @param   aIPV6NetworkMaskPrefixLength address of result pointer
 */
HRESULT HostNetworkInterface::getIPV6NetworkMaskPrefixLength(ULONG *aIPV6NetworkMaskPrefixLength)
{
    *aIPV6NetworkMaskPrefixLength = m.IPV6NetworkMaskPrefixLength;

    return S_OK;
}

/**
 * Returns the hardware address of the host network interface.
 *
 * @returns COM status code
 * @param   aHardwareAddress hardware address
 */
HRESULT HostNetworkInterface::getHardwareAddress(com::Utf8Str &aHardwareAddress)
{
    aHardwareAddress = m.hardwareAddress;
    return S_OK;
}

/**
 * Returns the encapsulation protocol type of the host network interface.
 *
 * @returns COM status code
 * @param   aType address of result pointer
 */
HRESULT HostNetworkInterface::getMediumType(HostNetworkInterfaceMediumType_T *aType)
{
    *aType = m.mediumType;

    return S_OK;
}

/**
 * Returns the current state of the host network interface.
 *
 * @returns COM status code
 * @param   aStatus address of result pointer
 */
HRESULT HostNetworkInterface::getStatus(HostNetworkInterfaceStatus_T *aStatus)
{
    *aStatus = m.status;

    return S_OK;
}

/**
 * Returns network interface type
 *
 * @returns COM status code
 * @param   aType address of result pointer
 */
HRESULT HostNetworkInterface::getInterfaceType(HostNetworkInterfaceType_T *aType)
{
    *aType = mIfType;

    return S_OK;

}

HRESULT HostNetworkInterface::getNetworkName(com::Utf8Str &aNetworkName)
{
    aNetworkName = mNetworkName;

    return S_OK;
}

HRESULT HostNetworkInterface::getWireless(BOOL *aWireless)
{
    *aWireless = m.wireless;

    return S_OK;
}

HRESULT HostNetworkInterface::enableStaticIPConfig(const com::Utf8Str &aIPAddress,
                                                   const com::Utf8Str &aNetworkMask)
{
#ifndef VBOX_WITH_HOSTNETIF_API
    RT_NOREF(aIPAddress, aNetworkMask);
    return E_NOTIMPL;
#else
    HRESULT hrc;

    if (aIPAddress.isEmpty())
    {
        if (m.IPAddress)
        {
            int vrc = NetIfEnableStaticIpConfig(mVirtualBox, this, m.IPAddress, 0, 0);
            if (RT_SUCCESS(vrc))
            {
                m.realIPAddress = 0;
#if defined(RT_OS_WINDOWS)
                eraseAdapterConfigParameter("IPAddress");
                eraseAdapterConfigParameter("IPNetMask");
#else /* !defined(RT_OS_WINDOWS) */
                if (FAILED(mVirtualBox->SetExtraData(BstrFmt("HostOnly/%s/IPAddress",
                                                             mInterfaceName.c_str()).raw(), NULL)))
                    return E_FAIL;
                if (FAILED(mVirtualBox->SetExtraData(BstrFmt("HostOnly/%s/IPNetMask",
                                                             mInterfaceName.c_str()).raw(), NULL)))
                    return E_FAIL;
#endif /* !defined(RT_OS_WINDOWS) */
                return S_OK;
            }
        }
        else
            return S_OK;
    }

    ULONG ip, mask;
    ip = inet_addr(aIPAddress.c_str());
    if (ip != INADDR_NONE)
    {
        if (aNetworkMask.isEmpty())
            mask = 0xFFFFFF;
        else
            mask = inet_addr(aNetworkMask.c_str());
        if (mask != INADDR_NONE)
        {
            if (m.realIPAddress == ip && m.realNetworkMask == mask)
                return S_OK;
            int vrc = NetIfEnableStaticIpConfig(mVirtualBox, this, m.IPAddress, ip, mask);
            if (RT_SUCCESS(vrc))
            {
                m.realIPAddress   = ip;
                m.realNetworkMask = mask;
#if defined(RT_OS_WINDOWS)
                saveAdapterConfigIPv4(ip, mask);
#else /* !defined(RT_OS_WINDOWS) */
                if (FAILED(mVirtualBox->SetExtraData(BstrFmt("HostOnly/%s/IPAddress",
                                                             mInterfaceName.c_str()).raw(),
                                                     Bstr(aIPAddress).raw())))
                    return E_FAIL;
                if (FAILED(mVirtualBox->SetExtraData(BstrFmt("HostOnly/%s/IPNetMask",
                                                             mInterfaceName.c_str()).raw(),
                                                     Bstr(aNetworkMask).raw())))
                    return E_FAIL;
#endif /* !defined(RT_OS_WINDOWS) */
                return S_OK;
            }
            else
            {
                LogRel(("Failed to EnableStaticIpConfig with vrc=%Rrc\n", vrc));
                /* Global::vboxStatusCodeToCOM assert things we can guarantee */
                switch (vrc)
                {
                    case VERR_NOT_IMPLEMENTED:
                        hrc = E_NOTIMPL;
                        break;
                    case VERR_ACCESS_DENIED:
                        hrc = E_ACCESSDENIED;
                        break;
                    default:
                        hrc = E_FAIL;
                        break;
                }
                return hrc;
            }

        }
    }
    return E_FAIL;
#endif
}

HRESULT HostNetworkInterface::enableStaticIPConfigV6(const com::Utf8Str &aIPV6Address,
                                                     ULONG aIPV6NetworkMaskPrefixLength)
{
#ifndef VBOX_WITH_HOSTNETIF_API
    RT_NOREF(aIPV6Address, aIPV6NetworkMaskPrefixLength);
    return E_NOTIMPL;
#else
    if (aIPV6NetworkMaskPrefixLength > 128)
        return mVirtualBox->setErrorBoth(E_INVALIDARG, VERR_INVALID_PARAMETER,
                   tr("Invalid IPv6 prefix length"));

    HRESULT hrc;

    RTNETADDRIPV6 AddrNew;
    char *pszZoneIgnored;
    int vrc = RTNetStrToIPv6Addr(aIPV6Address.c_str(), &AddrNew, &pszZoneIgnored);
    if (RT_FAILURE(vrc))
        return mVirtualBox->setErrorBoth(E_INVALIDARG, vrc, tr("Invalid IPv6 address"));

    RTNETADDRIPV6 AddrOld;
    vrc = RTNetStrToIPv6Addr(com::Utf8Str(m.realIPV6Address).c_str(), &AddrOld, &pszZoneIgnored);
    bool fAddrChanged = RT_SUCCESS(vrc) ? AddrNew.s.Lo != AddrOld.s.Lo || AddrNew.s.Hi != AddrOld.s.Hi : true;

    if (   fAddrChanged
        || m.realIPV6PrefixLength != aIPV6NetworkMaskPrefixLength)
    {
        if (aIPV6NetworkMaskPrefixLength == 0)
            aIPV6NetworkMaskPrefixLength = 64;
        vrc = NetIfEnableStaticIpConfigV6(mVirtualBox, this, m.IPV6Address.c_str(),
                                          aIPV6Address.c_str(),
                                          aIPV6NetworkMaskPrefixLength);
        if (RT_FAILURE(vrc))
        {
            LogRel(("Failed to EnableStaticIpConfigV6 with vrc=%Rrc\n", vrc));
            /* Global::vboxStatusCodeToCOM assert things we can guarantee */
            switch (vrc)
            {
                case VERR_NOT_IMPLEMENTED:
                    hrc = E_NOTIMPL;
                    break;
                case VERR_ACCESS_DENIED:
                    hrc = E_ACCESSDENIED;
                    break;
                default:
                    hrc = E_FAIL;
                    break;
            }
            return hrc;
        }
        else
        {
            m.realIPV6Address = aIPV6Address;
            m.realIPV6PrefixLength = aIPV6NetworkMaskPrefixLength;
#if defined(RT_OS_WINDOWS)
            saveAdapterConfigIPv6(Bstr(aIPV6Address).raw(), aIPV6NetworkMaskPrefixLength);
#else /* !defined(RT_OS_WINDOWS) */
            if (FAILED(mVirtualBox->SetExtraData(BstrFmt("HostOnly/%s/IPV6Address",
                                                         mInterfaceName.c_str()).raw(),
                                                 Bstr(aIPV6Address).raw())))
                return E_FAIL;
            if (FAILED(mVirtualBox->SetExtraData(BstrFmt("HostOnly/%s/IPV6NetMask",
                                                         mInterfaceName.c_str()).raw(),
                                                 BstrFmt("%u", aIPV6NetworkMaskPrefixLength).raw())))
#endif /* !defined(RT_OS_WINDOWS) */
                return E_FAIL;
        }

    }
    return S_OK;
#endif
}

HRESULT HostNetworkInterface::enableDynamicIPConfig()
{
#ifndef VBOX_WITH_HOSTNETIF_API
    return E_NOTIMPL;
#else
    int vrc = NetIfEnableDynamicIpConfig(mVirtualBox, this);
    if (RT_FAILURE(vrc))
    {
        LogRel(("Failed to EnableDynamicIpConfig with vrc=%Rrc\n", vrc));
        return vrc == VERR_NOT_IMPLEMENTED ? E_NOTIMPL : E_FAIL;
    }
    return S_OK;
#endif
}

HRESULT HostNetworkInterface::dHCPRediscover()
{
#ifndef VBOX_WITH_HOSTNETIF_API
    return E_NOTIMPL;
#else
    int vrc = NetIfDhcpRediscover(mVirtualBox, this);
    if (RT_FAILURE(vrc))
    {
        LogRel(("Failed to DhcpRediscover with vrc=%Rrc\n", vrc));
        return vrc == VERR_NOT_IMPLEMENTED ? E_NOTIMPL : E_FAIL;
    }
    return S_OK;
#endif
}

HRESULT HostNetworkInterface::i_setVirtualBox(VirtualBox *pVirtualBox)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AssertReturn(mVirtualBox != pVirtualBox, S_OK);

    unconst(mVirtualBox) = pVirtualBox;

#if !defined(RT_OS_WINDOWS)
    /* If IPv4 address hasn't been initialized */
    if (m.IPAddress == 0 && mIfType == HostNetworkInterfaceType_HostOnly)
    {
        Bstr tmpAddr, tmpMask;
        HRESULT hrc = mVirtualBox->GetExtraData(BstrFmt("HostOnly/%s/IPAddress",
                                                        mInterfaceName.c_str()).raw(),
                                                tmpAddr.asOutParam());
        if (FAILED(hrc) || tmpAddr.isEmpty())
            tmpAddr = getDefaultIPv4Address(mInterfaceName);

        hrc = mVirtualBox->GetExtraData(BstrFmt("HostOnly/%s/IPNetMask",
                                                mInterfaceName.c_str()).raw(),
                                        tmpMask.asOutParam());
        if (FAILED(hrc) || tmpMask.isEmpty())
            tmpMask = Bstr(VBOXNET_IPV4MASK_DEFAULT);

        m.IPAddress = inet_addr(Utf8Str(tmpAddr).c_str());
        m.networkMask = inet_addr(Utf8Str(tmpMask).c_str());
    }

    if (m.IPV6Address.isEmpty())
    {
        Bstr bstrIPV4Addr;
        Bstr tmpPrefixLen;
        HRESULT hrc = mVirtualBox->GetExtraData(BstrFmt("HostOnly/%s/IPV6Address",
                                                        mInterfaceName.c_str()).raw(),
                                                bstrIPV4Addr.asOutParam());
        if (SUCCEEDED(hrc))
        {
            m.IPV6Address = bstrIPV4Addr;
            if (!m.IPV6Address.isEmpty())
            {
                hrc = mVirtualBox->GetExtraData(BstrFmt("HostOnly/%s/IPV6PrefixLen",
                                                        mInterfaceName.c_str()).raw(),
                                                tmpPrefixLen.asOutParam());
                if (SUCCEEDED(hrc) && !tmpPrefixLen.isEmpty())
                    m.IPV6NetworkMaskPrefixLength = Utf8Str(tmpPrefixLen).toUInt32();
                else
                    m.IPV6NetworkMaskPrefixLength = 64;
            }
        }
    }
#endif

    return S_OK;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
