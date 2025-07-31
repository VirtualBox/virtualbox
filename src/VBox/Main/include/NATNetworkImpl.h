/* $Id: NATNetworkImpl.h 110268 2025-07-16 17:45:40Z jack.doherty@oracle.com $ */
/** @file
 * INATNetwork implementation header, lives in VBoxSVC.
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

#ifndef MAIN_INCLUDED_NATNetworkImpl_h
#define MAIN_INCLUDED_NATNetworkImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxEvents.h"
#include "NATNetworkWrap.h"

#ifdef VBOX_WITH_HOSTNETIF_API
struct NETIFINFO;
#endif

namespace settings
{
    struct NATNetwork;
    struct NATRule;
    typedef std::map<com::Utf8Str, NATRule> NATRulesMap;
}

#ifdef RT_OS_WINDOWS
# define NATSR_EXECUTABLE_NAME "VBoxNetNAT.exe"
#else
# define NATSR_EXECUTABLE_NAME "VBoxNetNAT"
#endif

#undef ADDR_ANY ///@todo ADDR_ANY collides with some windows header!

enum ADDRESSLOOKUPTYPE
{
    ADDR_GATEWAY,
    ADDR_DHCP,
    ADDR_DHCPLOWERIP,
    ADDR_ANY
};

class NATNetworkServiceRunner: public NetworkServiceRunner
{
public:
    NATNetworkServiceRunner(): NetworkServiceRunner(NATSR_EXECUTABLE_NAME){}
    ~NATNetworkServiceRunner(){}
};

class ATL_NO_VTABLE NATNetwork :
    public NATNetworkWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(NATNetwork)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(VirtualBox *aVirtualBox, com::Utf8Str aName);
    HRESULT i_loadSettings(const settings::NATNetwork &data);
    void uninit();
    HRESULT i_saveSettings(settings::NATNetwork &data);

private:

    // Wrapped INATNetwork properties
    HRESULT getNetworkName(com::Utf8Str &aNetworkName);
    HRESULT setNetworkName(const com::Utf8Str &aNetworkName);
    HRESULT getEnabled(BOOL *aEnabled);
    HRESULT setEnabled(BOOL aEnabled);
    HRESULT getNetwork(com::Utf8Str &aNetwork);
    HRESULT setNetwork(const com::Utf8Str &aNetwork);
    HRESULT getGateway(com::Utf8Str &aGateway);
    HRESULT getIPv6Enabled(BOOL *aIPv6Enabled);
    HRESULT setIPv6Enabled(BOOL aIPv6Enabled);
    HRESULT getIPv6Prefix(com::Utf8Str &aIPv6Prefix);
    HRESULT setIPv6Prefix(const com::Utf8Str &aIPv6Prefix);
    HRESULT getAdvertiseDefaultIPv6RouteEnabled(BOOL *aAdvertiseDefaultIPv6RouteEnabled);
    HRESULT setAdvertiseDefaultIPv6RouteEnabled(BOOL aAdvertiseDefaultIPv6RouteEnabled);
    HRESULT getNeedDhcpServer(BOOL *aNeedDhcpServer);
    HRESULT setNeedDhcpServer(BOOL aNeedDhcpServer);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getPortForwardRules4(std::vector<com::Utf8Str> &aPortForwardRules4);
    HRESULT getLocalMappings(std::vector<com::Utf8Str> &aLocalMappings);
    HRESULT getLoopbackIp6(LONG *aLoopbackIp6);
    HRESULT setLoopbackIp6(LONG aLoopbackIp6);
    HRESULT getPortForwardRules6(std::vector<com::Utf8Str> &aPortForwardRules6);
    HRESULT setLocalhostReachable(BOOL fLocalhostReachable);
    HRESULT getLocalhostReachable(BOOL *pfLocalhostReachable);
    HRESULT setForwardBroadcast(BOOL fForwardBroadcast);
    HRESULT getForwardBroadcast(BOOL *pfForwardBroadcast);
    HRESULT setNatMTU(ULONG uMTU);
    HRESULT getNatMTU(ULONG *puMTU);
    HRESULT setNatMRU(ULONG uMRU);
    HRESULT getNatMRU(ULONG *puMRU);

    // wrapped INATNetwork methods
    HRESULT addLocalMapping(const com::Utf8Str &aHostid,
                            LONG aOffset);
    HRESULT addPortForwardRule(BOOL aIsIpv6,
                               const com::Utf8Str &aRuleName,
                               NATProtocol_T aProto,
                               const com::Utf8Str &aHostIP,
                               USHORT aHostPort,
                               const com::Utf8Str &aGuestIP,
                               USHORT aGuestPort);
    HRESULT removePortForwardRule(BOOL aISipv6,
                                  const com::Utf8Str &aRuleName);
    HRESULT start();
    HRESULT stop();

    // Internal methods
    HRESULT setErrorBusy();

    int i_recalculateIpv4AddressAssignments();
    int i_findFirstAvailableOffset(ADDRESSLOOKUPTYPE, uint32_t *);
    int i_recalculateIPv6Prefix();

    void i_getPortForwardRulesFromMap(std::vector<Utf8Str> &aPortForwardRules, settings::NATRulesMap &aRules);

    void i_updateDnsOptions();
    void i_updateDomainNameOption(ComPtr<IHost> &host);
    void i_updateDomainNameServerOption(ComPtr<IHost> &host);

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_NATNetworkImpl_h */
