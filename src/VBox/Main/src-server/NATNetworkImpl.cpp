/* $Id: NATNetworkImpl.cpp 110269 2025-07-16 17:58:19Z jack.doherty@oracle.com $ */
/** @file
 * INATNetwork implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_NATNETWORK
#include "NetworkServiceRunner.h"
#include "DHCPServerImpl.h"
#include "NATNetworkImpl.h"
#include "AutoCaller.h"

#include <iprt/asm.h>
#include <iprt/cpp/utils.h>
#include <iprt/net.h>
#include <iprt/cidr.h>
#include <iprt/net.h>
#include <VBox/com/array.h>
#include <VBox/com/ptr.h>
#include <VBox/settings.h>

#include "EventImpl.h"
#include "LoggingNew.h"

#include "VirtualBoxImpl.h"
#include <algorithm>
#include <list>

#ifndef RT_OS_WINDOWS
# include <netinet/in.h>
#else
# define IN_LOOPBACKNET 127
#endif


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////
struct NATNetwork::Data
{
    Data()
      : pVirtualBox(NULL)
      , offGateway(0)
      , offDhcp(0)
    {
    }
    virtual ~Data(){}
    const ComObjPtr<EventSource> pEventSource;
#ifdef VBOX_WITH_NAT_SERVICE
    NATNetworkServiceRunner NATRunner;
    ComObjPtr<IDHCPServer> dhcpServer;
#endif
    /** weak VirtualBox parent */
    VirtualBox * const pVirtualBox;

    /** NATNetwork settings */
    settings::NATNetwork s;

    com::Utf8Str IPv4Gateway;
    com::Utf8Str IPv4NetworkMask;
    com::Utf8Str IPv4DhcpServer;
    com::Utf8Str IPv4DhcpServerLowerIp;
    com::Utf8Str IPv4DhcpServerUpperIp;

    uint32_t offGateway;
    uint32_t offDhcp;

    void recalculatePortForwarding(const RTNETADDRIPV4 &AddrNew, const RTNETADDRIPV4 &MaskNew);
};


NATNetwork::NATNetwork()
    : m(NULL)
{
}


NATNetwork::~NATNetwork()
{
}


HRESULT NATNetwork::FinalConstruct()
{
    return BaseFinalConstruct();
}


void NATNetwork::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}


void NATNetwork::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
    unconst(m->pVirtualBox) = NULL;
    delete m;
    m = NULL;
}

HRESULT NATNetwork::init(VirtualBox *aVirtualBox, com::Utf8Str aName)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();
    /* share VirtualBox weakly */
    unconst(m->pVirtualBox) = aVirtualBox;
    m->s.strNetworkName = aName;
    m->s.strIPv4NetworkCidr = "10.0.2.0/24";
    m->offGateway = 1;
    i_recalculateIPv6Prefix();  /* set m->strIPv6Prefix based on IPv4 */

    settings::NATHostLoopbackOffset off;
    off.strLoopbackHostAddress = "127.0.0.1";
    off.u32Offset = (uint32_t)2;
    m->s.llHostLoopbackOffsetList.push_back(off);

    i_recalculateIpv4AddressAssignments();

    HRESULT hrc = unconst(m->pEventSource).createObject();
    if (FAILED(hrc)) throw hrc;

    hrc = m->pEventSource->init();
    if (FAILED(hrc)) throw hrc;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}


HRESULT NATNetwork::setErrorBusy()
{
    return setError(E_FAIL,
                    tr("Unable to change settings"
                       " while NATNetwork instance is running"));
}


HRESULT NATNetwork::i_loadSettings(const settings::NATNetwork &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->s = data;
    if (   m->s.strIPv6Prefix.isEmpty()
           /* also clean up bogus old default */
        || m->s.strIPv6Prefix == "fe80::/64")
        i_recalculateIPv6Prefix(); /* set m->strIPv6Prefix based on IPv4 */
    i_recalculateIpv4AddressAssignments();

    return S_OK;
}

HRESULT NATNetwork::i_saveSettings(settings::NATNetwork &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(!m->s.strNetworkName.isEmpty(), E_FAIL);
    data = m->s;

    m->pVirtualBox->i_onNATNetworkSetting(m->s.strNetworkName,
                                          m->s.fEnabled,
                                          m->s.strIPv4NetworkCidr,
                                          m->IPv4Gateway,
                                          m->s.fAdvertiseDefaultIPv6Route,
                                          m->s.fNeedDhcpServer);

    /* Notify listeners listening on this network only */
    ::FireNATNetworkSettingEvent(m->pEventSource,
                                 m->s.strNetworkName,
                                 m->s.fEnabled,
                                 m->s.strIPv4NetworkCidr,
                                 m->IPv4Gateway,
                                 m->s.fAdvertiseDefaultIPv6Route,
                                 m->s.fNeedDhcpServer);

    return S_OK;
}

HRESULT NATNetwork::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    /* event source is const, no need to lock */
    m->pEventSource.queryInterfaceTo(aEventSource.asOutParam());
    return S_OK;
}

HRESULT NATNetwork::getNetworkName(com::Utf8Str &aNetworkName)
{
    AssertReturn(!m->s.strNetworkName.isEmpty(), E_FAIL);
    aNetworkName = m->s.strNetworkName;
    return S_OK;
}

HRESULT NATNetwork::setNetworkName(const com::Utf8Str &aNetworkName)
{
    if (aNetworkName.isEmpty())
        return setError(E_INVALIDARG,
                        tr("Network name cannot be empty"));

    {
        AutoReadLock alockNatNetList(m->pVirtualBox->i_getNatNetLock() COMMA_LOCKVAL_SRC_POS);
        if (m->pVirtualBox->i_isNatNetStarted(m->s.strNetworkName))
            return setErrorBusy();

        /** @todo r=uwe who ensures there's no other network with that name? */

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (aNetworkName == m->s.strNetworkName)
            return S_OK;

        m->s.strNetworkName = aNetworkName;
    }


    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = m->pVirtualBox->i_saveSettings();
    ComAssertComRCRetRC(hrc);

    return S_OK;
}

HRESULT NATNetwork::getEnabled(BOOL *aEnabled)
{
    *aEnabled = m->s.fEnabled;

    i_recalculateIpv4AddressAssignments();
    return S_OK;
}

HRESULT NATNetwork::setEnabled(const BOOL aEnabled)
{
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (RT_BOOL(aEnabled) == m->s.fEnabled)
            return S_OK;
        m->s.fEnabled = RT_BOOL(aEnabled);
    }

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = m->pVirtualBox->i_saveSettings();
    ComAssertComRCRetRC(hrc);
    return S_OK;
}

HRESULT NATNetwork::getGateway(com::Utf8Str &aIPv4Gateway)
{
    aIPv4Gateway = m->IPv4Gateway;
    return S_OK;
}

HRESULT NATNetwork::getNetwork(com::Utf8Str &aNetwork)
{
    aNetwork = m->s.strIPv4NetworkCidr;
    return S_OK;
}


HRESULT NATNetwork::setNetwork(const com::Utf8Str &aIPv4NetworkCidr)
{
    RTNETADDRIPV4 Net;
    int iPrefix;
    int vrc = RTNetStrToIPv4Cidr(aIPv4NetworkCidr.c_str(), &Net, &iPrefix);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("%s is not a valid IPv4 CIDR notation"), aIPv4NetworkCidr.c_str());

    /*
     * /32 is a single address, not a network, /31 is the degenerate
     * point-to-point case, so reject these.  Larger values and
     * negative values are already treated as errors by the
     * conversion.
     */
    if (iPrefix > 30)
        return setError(E_FAIL, tr("%s network is too small"), aIPv4NetworkCidr.c_str());

    if (iPrefix == 0)
        return setError(E_FAIL, tr("%s specifies zero prefix"), aIPv4NetworkCidr.c_str());

    RTNETADDRIPV4 Mask;
    vrc = RTNetPrefixToMaskIPv4(iPrefix, &Mask);
    AssertRCReturn(vrc, setErrorBoth(E_FAIL, vrc, tr("%s: internal error: failed to convert prefix %d to netmask: %Rrc"),
                                     aIPv4NetworkCidr.c_str(), iPrefix, vrc));

    if ((Net.u & ~Mask.u) != 0)
        return setError(E_FAIL, tr("%s: the specified address is longer than the specified prefix"),
                        aIPv4NetworkCidr.c_str());

    /** @todo r=uwe Check the address is unicast, not a loopback, etc. */

    /* normalized CIDR notation */
    com::Utf8StrFmt strCidr("%RTnaipv4/%d", Net.u, iPrefix);

    {
        AutoReadLock alockNatNetList(m->pVirtualBox->i_getNatNetLock() COMMA_LOCKVAL_SRC_POS);
        if (m->pVirtualBox->i_isNatNetStarted(m->s.strNetworkName))
            return setErrorBusy();

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (m->s.strIPv4NetworkCidr == strCidr)
            return S_OK;

        m->recalculatePortForwarding(Net, Mask);

        m->s.strIPv4NetworkCidr = strCidr;
        i_recalculateIpv4AddressAssignments();
    }

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = m->pVirtualBox->i_saveSettings();
    ComAssertComRCRetRC(hrc);
    return S_OK;
}


/**
 * Do best effort attempt at converting existing port forwarding rules
 * from the old prefix to the new one.  This might not be possible if
 * the new prefix is longer (i.e. the network is smaller) or if a rule
 * lists destination not from the network (though that rule wouldn't
 * be terribly useful, at least currently).
 */
void NATNetwork::Data::recalculatePortForwarding(const RTNETADDRIPV4 &NetNew,
                                                 const RTNETADDRIPV4 &MaskNew)
{
    if (s.mapPortForwardRules4.empty())
        return;                 /* nothing to do */

    RTNETADDRIPV4 NetOld;
    int iPrefixOld;
    int vrc = RTNetStrToIPv4Cidr(s.strIPv4NetworkCidr.c_str(), &NetOld, &iPrefixOld);
    if (RT_FAILURE(vrc))
        return;

    RTNETADDRIPV4 MaskOld;
    vrc = RTNetPrefixToMaskIPv4(iPrefixOld, &MaskOld);
    if (RT_FAILURE(vrc))
        return;

    for (settings::NATRulesMap::iterator it = s.mapPortForwardRules4.begin();
         it != s.mapPortForwardRules4.end();
         ++it)
    {
        settings::NATRule &rule = it->second;

        /* parse the old destination address */
        RTNETADDRIPV4 AddrOld;
        vrc = RTNetStrToIPv4Addr(rule.strGuestIP.c_str(), &AddrOld);
        if (RT_FAILURE(vrc))
            continue;

        /* is it in the old network? (likely) */
        if ((AddrOld.u & MaskOld.u) != NetOld.u)
            continue;

        uint32_t u32Host = (AddrOld.u & ~MaskOld.u);

        /* does it fit into the new network? */
        if ((u32Host & MaskNew.u) != 0)
            continue;

        rule.strGuestIP.printf("%RTnaipv4", NetNew.u | u32Host);
    }
}


HRESULT NATNetwork::getIPv6Enabled(BOOL *aIPv6Enabled)
{
    *aIPv6Enabled = m->s.fIPv6Enabled;

    return S_OK;
}


HRESULT NATNetwork::setIPv6Enabled(const BOOL aIPv6Enabled)
{
    {
        AutoReadLock alockNatNetList(m->pVirtualBox->i_getNatNetLock() COMMA_LOCKVAL_SRC_POS);
        if (m->pVirtualBox->i_isNatNetStarted(m->s.strNetworkName))
            return setErrorBusy();

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (RT_BOOL(aIPv6Enabled) == m->s.fIPv6Enabled)
            return S_OK;

        /*
         * If we are enabling ipv6 and the prefix is not set, provide
         * the default based on ipv4.
         */
        if (aIPv6Enabled && m->s.strIPv6Prefix.isEmpty())
            i_recalculateIPv6Prefix();

        m->s.fIPv6Enabled = RT_BOOL(aIPv6Enabled);
    }

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = m->pVirtualBox->i_saveSettings();
    ComAssertComRCRetRC(hrc);

    return S_OK;
}


HRESULT NATNetwork::getIPv6Prefix(com::Utf8Str &aIPv6Prefix)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aIPv6Prefix = m->s.strIPv6Prefix;
    return S_OK;
}

HRESULT NATNetwork::setIPv6Prefix(const com::Utf8Str &aIPv6Prefix)
{
    HRESULT hrc;
    int vrc;

    /* Since we store it in text form, use canonical representation */
    com::Utf8Str strNormalizedIPv6Prefix;

    const char *pcsz = RTStrStripL(aIPv6Prefix.c_str());
    if (*pcsz != '\0')          /* verify it first if not empty/blank */
    {
        RTNETADDRIPV6 Net6;
        int iPrefixLength;
        vrc = RTNetStrToIPv6Cidr(aIPv6Prefix.c_str(), &Net6, &iPrefixLength);
        if (RT_FAILURE(vrc))
            return setError(E_INVALIDARG, tr("%s is not a valid IPv6 prefix"), aIPv6Prefix.c_str());

        /* Accept both addr:: and addr::/64 */
        if (iPrefixLength == 128)   /* no length was specified after the address? */
            iPrefixLength = 64;     /*   take it to mean /64 which we require anyway */
        else if (iPrefixLength != 64)
            return setError(E_INVALIDARG, tr("Invalid IPv6 prefix length %d, must be 64"), iPrefixLength);

        /* Verify the address is unicast. */
        if (   (Net6.au8[0] & 0xe0) != 0x20  /* global 2000::/3 */
            && (Net6.au8[0] & 0xfe) != 0xfc) /* local  fc00::/7 */
            return setError(E_INVALIDARG, tr("IPv6 prefix %RTnaipv6 is not unicast"), &Net6);

        /* Verify the interfaces ID part is zero */
        if (Net6.au64[1] != 0)
            return setError(E_INVALIDARG, tr("Non-zero bits in the interface ID part of the IPv6 prefix %RTnaipv6/64"), &Net6);

        vrc = strNormalizedIPv6Prefix.printfNoThrow("%RTnaipv6/64", &Net6);
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_NO_MEMORY)
                return setError(E_OUTOFMEMORY);
            return setError(E_FAIL, tr("Internal error"));
        }
    }

    {
        AutoReadLock alockNatNetList(m->pVirtualBox->i_getNatNetLock() COMMA_LOCKVAL_SRC_POS);
        if (m->pVirtualBox->i_isNatNetStarted(m->s.strNetworkName))
            return setErrorBusy();

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (strNormalizedIPv6Prefix == m->s.strIPv6Prefix)
            return S_OK;

        /* only allow prefix to be empty if IPv6 is disabled */
        if (strNormalizedIPv6Prefix.isEmpty() && m->s.fIPv6Enabled)
            return setError(E_FAIL, tr("Setting an empty IPv6 prefix when IPv6 is enabled"));

        /**
         * @todo
         * silently ignore network IPv6 prefix update.
         * todo: see similar todo in NATNetwork::COMSETTER(Network)(IN_BSTR)
         */
        if (!m->s.mapPortForwardRules6.empty())
            return S_OK;

        m->s.strIPv6Prefix = strNormalizedIPv6Prefix;
    }

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    hrc = m->pVirtualBox->i_saveSettings();
    ComAssertComRCRetRC(hrc);

    return S_OK;
}


HRESULT NATNetwork::getAdvertiseDefaultIPv6RouteEnabled(BOOL *aAdvertiseDefaultIPv6Route)
{
    *aAdvertiseDefaultIPv6Route = m->s.fAdvertiseDefaultIPv6Route;

    return S_OK;
}


HRESULT NATNetwork::setAdvertiseDefaultIPv6RouteEnabled(const BOOL aAdvertiseDefaultIPv6Route)
{
    {
        AutoReadLock alockNatNetList(m->pVirtualBox->i_getNatNetLock() COMMA_LOCKVAL_SRC_POS);
        if (m->pVirtualBox->i_isNatNetStarted(m->s.strNetworkName))
            return setErrorBusy();

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (RT_BOOL(aAdvertiseDefaultIPv6Route) == m->s.fAdvertiseDefaultIPv6Route)
            return S_OK;

        m->s.fAdvertiseDefaultIPv6Route = RT_BOOL(aAdvertiseDefaultIPv6Route);

    }

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = m->pVirtualBox->i_saveSettings();
    ComAssertComRCRetRC(hrc);

    return S_OK;
}


HRESULT NATNetwork::getNeedDhcpServer(BOOL *aNeedDhcpServer)
{
    *aNeedDhcpServer = m->s.fNeedDhcpServer;

    return S_OK;
}

HRESULT NATNetwork::setNeedDhcpServer(const BOOL aNeedDhcpServer)
{
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (RT_BOOL(aNeedDhcpServer) == m->s.fNeedDhcpServer)
            return S_OK;

        m->s.fNeedDhcpServer = RT_BOOL(aNeedDhcpServer);

        i_recalculateIpv4AddressAssignments();

    }

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = m->pVirtualBox->i_saveSettings();
    ComAssertComRCRetRC(hrc);

    return S_OK;
}

HRESULT NATNetwork::getLocalMappings(std::vector<com::Utf8Str> &aLocalMappings)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aLocalMappings.resize(m->s.llHostLoopbackOffsetList.size());
    size_t i = 0;
    for (settings::NATLoopbackOffsetList::const_iterator it = m->s.llHostLoopbackOffsetList.begin();
         it != m->s.llHostLoopbackOffsetList.end(); ++it, ++i)
    {
        aLocalMappings[i] = Utf8StrFmt("%s=%d",
                            (*it).strLoopbackHostAddress.c_str(),
                            (*it).u32Offset);
    }

    return S_OK;
}

HRESULT NATNetwork::addLocalMapping(const com::Utf8Str &aHostId, LONG aOffset)
{
    RTNETADDRIPV4 addr;
    int vrc = RTNetStrToIPv4Addr(Utf8Str(aHostId).c_str(), &addr);
    if (RT_FAILURE(vrc))
        return E_INVALIDARG;

    /* check against 127/8 */
    if ((RT_N2H_U32(addr.u) >> IN_CLASSA_NSHIFT) != IN_LOOPBACKNET)
        return E_INVALIDARG;

    /* check against networkid vs network mask */
    RTNETADDRIPV4 net, mask;
    vrc = RTCidrStrToIPv4(Utf8Str(m->s.strIPv4NetworkCidr).c_str(), &net, &mask);
    if (RT_FAILURE(vrc))
        return E_INVALIDARG;

    if (((net.u + (uint32_t)aOffset) & mask.u) != net.u)
        return E_INVALIDARG;

    settings::NATLoopbackOffsetList::iterator it;

    it = std::find(m->s.llHostLoopbackOffsetList.begin(),
                   m->s.llHostLoopbackOffsetList.end(),
                   aHostId);
    if (it != m->s.llHostLoopbackOffsetList.end())
    {
        if (aOffset == 0) /* erase */
            m->s.llHostLoopbackOffsetList.erase(it, it);
        else /* modify */
        {
            settings::NATLoopbackOffsetList::iterator it1;
            it1 = std::find(m->s.llHostLoopbackOffsetList.begin(),
                            m->s.llHostLoopbackOffsetList.end(),
                            (uint32_t)aOffset);
            if (it1 != m->s.llHostLoopbackOffsetList.end())
                return E_INVALIDARG; /* this offset is already registered. */

            (*it).u32Offset = (uint32_t)aOffset;
        }

        AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
        return m->pVirtualBox->i_saveSettings();
    }

    /* injection */
    it = std::find(m->s.llHostLoopbackOffsetList.begin(),
                   m->s.llHostLoopbackOffsetList.end(),
                   (uint32_t)aOffset);

    if (it != m->s.llHostLoopbackOffsetList.end())
        return E_INVALIDARG; /* offset is already registered. */

    settings::NATHostLoopbackOffset off;
    off.strLoopbackHostAddress = aHostId;
    off.u32Offset = (uint32_t)aOffset;
    m->s.llHostLoopbackOffsetList.push_back(off);

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    return m->pVirtualBox->i_saveSettings();
}


HRESULT NATNetwork::getLoopbackIp6(LONG *aLoopbackIp6)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aLoopbackIp6 = (LONG)m->s.u32HostLoopback6Offset;
    return S_OK;
}


HRESULT NATNetwork::setLoopbackIp6(LONG aLoopbackIp6)
{
    {
        AutoReadLock alockNatNetList(m->pVirtualBox->i_getNatNetLock() COMMA_LOCKVAL_SRC_POS);
        if (m->pVirtualBox->i_isNatNetStarted(m->s.strNetworkName))
            return setErrorBusy();

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (aLoopbackIp6 < 0)
            return E_INVALIDARG;

        if (static_cast<uint32_t>(aLoopbackIp6) == m->s.u32HostLoopback6Offset)
            return S_OK;

        m->s.u32HostLoopback6Offset = (uint32_t)aLoopbackIp6;
    }

    AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
    return m->pVirtualBox->i_saveSettings();
}


HRESULT NATNetwork::getPortForwardRules4(std::vector<com::Utf8Str> &aPortForwardRules4)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    i_getPortForwardRulesFromMap(aPortForwardRules4,
                                 m->s.mapPortForwardRules4);
    return S_OK;
}

HRESULT NATNetwork::getPortForwardRules6(std::vector<com::Utf8Str> &aPortForwardRules6)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    i_getPortForwardRulesFromMap(aPortForwardRules6,
                                 m->s.mapPortForwardRules6);
    return S_OK;
}

HRESULT NATNetwork::setLocalhostReachable(BOOL fLocalhostReachable)
{
    RT_NOREF(fLocalhostReachable);
    return E_NOTIMPL;
}

HRESULT NATNetwork::getLocalhostReachable(BOOL *pfLocalhostReachable)
{
    RT_NOREF(pfLocalhostReachable);
    return E_NOTIMPL;
}

HRESULT NATNetwork::setForwardBroadcast(BOOL fForwardBroadcast)
{
    RT_NOREF(fForwardBroadcast);
    return E_NOTIMPL;
}

HRESULT NATNetwork::getForwardBroadcast(BOOL *pfForwardBroadcast)
{
    RT_NOREF(pfForwardBroadcast);
    return E_NOTIMPL;
}

HRESULT NATNetwork::setNatMTU(ULONG uMTU)
{
    RT_NOREF(uMTU);
    return E_NOTIMPL;
}

HRESULT NATNetwork::getNatMTU(ULONG *puMTU)
{
    RT_NOREF(puMTU);
    return E_NOTIMPL;
}

HRESULT NATNetwork::setNatMRU(ULONG uMRU)
{
    RT_NOREF(uMRU);
    return E_NOTIMPL;
}

HRESULT NATNetwork::getNatMRU(ULONG *puMRU)
{
    RT_NOREF(puMRU);
    return E_NOTIMPL;
}

HRESULT NATNetwork::addPortForwardRule(BOOL aIsIpv6,
                                       const com::Utf8Str &aPortForwardRuleName,
                                       NATProtocol_T aProto,
                                       const com::Utf8Str &aHostIp,
                                       USHORT aHostPort,
                                       const com::Utf8Str &aGuestIp,
                                       USHORT aGuestPort)
{
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Utf8Str name = aPortForwardRuleName;
        Utf8Str proto;
        settings::NATRule r;
        settings::NATRulesMap &mapRules = aIsIpv6 ? m->s.mapPortForwardRules6 : m->s.mapPortForwardRules4;
        switch (aProto)
        {
            case NATProtocol_TCP:
                proto = "tcp";
                break;
            case NATProtocol_UDP:
                proto = "udp";
                break;
            default:
                return E_INVALIDARG;
        }
        if (name.isEmpty())
            name = Utf8StrFmt("%s_[%s]%%%d_[%s]%%%d", proto.c_str(),
                              aHostIp.c_str(), aHostPort,
                              aGuestIp.c_str(), aGuestPort);

        for (settings::NATRulesMap::iterator it = mapRules.begin(); it != mapRules.end(); ++it)
        {
            r = it->second;
            if (it->first == name)
                return setError(E_INVALIDARG,
                                tr("A NAT rule of this name already exists"));
            if (   r.strHostIP == aHostIp
                   && r.u16HostPort == aHostPort
                   && r.proto == aProto)
                return setError(E_INVALIDARG,
                                tr("A NAT rule for this host port and this host IP already exists"));
        }

        r.strName = name.c_str();
        r.proto = aProto;
        r.strHostIP = aHostIp;
        r.u16HostPort = aHostPort;
        r.strGuestIP = aGuestIp;
        r.u16GuestPort = aGuestPort;
        mapRules.insert(std::make_pair(name, r));
    }
    {
        AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
        HRESULT hrc = m->pVirtualBox->i_saveSettings();
        ComAssertComRCRetRC(hrc);
    }

    m->pVirtualBox->i_onNATNetworkPortForward(m->s.strNetworkName, TRUE, aIsIpv6,
                                              aPortForwardRuleName, aProto,
                                              aHostIp, aHostPort,
                                              aGuestIp, aGuestPort);

    /* Notify listeners listening on this network only */
    ::FireNATNetworkPortForwardEvent(m->pEventSource, m->s.strNetworkName, TRUE,
                                     aIsIpv6, aPortForwardRuleName, aProto,
                                     aHostIp, aHostPort,
                                     aGuestIp, aGuestPort);

    return S_OK;
}

HRESULT NATNetwork::removePortForwardRule(BOOL aIsIpv6, const com::Utf8Str &aPortForwardRuleName)
{
    Utf8Str strHostIP;
    Utf8Str strGuestIP;
    uint16_t u16HostPort;
    uint16_t u16GuestPort;
    NATProtocol_T proto;

    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        settings::NATRulesMap &mapRules = aIsIpv6 ? m->s.mapPortForwardRules6 : m->s.mapPortForwardRules4;
        settings::NATRulesMap::iterator it = mapRules.find(aPortForwardRuleName);

        if (it == mapRules.end())
            return E_INVALIDARG;

        strHostIP = it->second.strHostIP;
        strGuestIP = it->second.strGuestIP;
        u16HostPort = it->second.u16HostPort;
        u16GuestPort = it->second.u16GuestPort;
        proto = it->second.proto;

        mapRules.erase(it);
    }

    {
        AutoWriteLock vboxLock(m->pVirtualBox COMMA_LOCKVAL_SRC_POS);
        HRESULT hrc = m->pVirtualBox->i_saveSettings();
        ComAssertComRCRetRC(hrc);
    }

    m->pVirtualBox->i_onNATNetworkPortForward(m->s.strNetworkName, FALSE, aIsIpv6, aPortForwardRuleName, proto,
                                              strHostIP, u16HostPort, strGuestIP, u16GuestPort);

    /* Notify listeners listening on this network only */
    ::FireNATNetworkPortForwardEvent(m->pEventSource, m->s.strNetworkName, FALSE, aIsIpv6, aPortForwardRuleName, proto,
                                     strHostIP, u16HostPort, strGuestIP, u16GuestPort);
    return S_OK;
}


void NATNetwork::i_updateDomainNameOption(ComPtr<IHost> &host)
{
    com::Bstr domain;
    if (FAILED(host->COMGETTER(DomainName)(domain.asOutParam())))
        LogRel(("NATNetwork: Failed to get host's domain name\n"));
    ComPtr<IDHCPGlobalConfig> pDHCPConfig;
    HRESULT hrc = m->dhcpServer->COMGETTER(GlobalConfig)(pDHCPConfig.asOutParam());
    if (FAILED(hrc))
    {
        LogRel(("NATNetwork: Failed to get global DHCP config when updating domain name option with %Rhrc\n", hrc));
        return;
    }
    if (domain.isNotEmpty())
    {
        hrc = pDHCPConfig->SetOption(DHCPOption_DomainName, DHCPOptionEncoding_Normal, domain.raw());
        if (FAILED(hrc))
            LogRel(("NATNetwork: Failed to add domain name option with %Rhrc\n", hrc));
    }
    else
        pDHCPConfig->RemoveOption(DHCPOption_DomainName);
}

void NATNetwork::i_updateDomainNameServerOption(ComPtr<IHost> &host)
{
    RTNETADDRIPV4 networkid, netmask;
    int vrc = RTCidrStrToIPv4(m->s.strIPv4NetworkCidr.c_str(), &networkid, &netmask);
    if (RT_FAILURE(vrc))
    {
        LogRel(("NATNetwork: Failed to parse cidr %s with %Rrc\n", m->s.strIPv4NetworkCidr.c_str(), vrc));
        return;
    }

    /* XXX: these are returned, surprisingly, in host order */
    networkid.u = RT_H2N_U32(networkid.u);
    netmask.u = RT_H2N_U32(netmask.u);

    com::SafeArray<BSTR> nameServers;
    HRESULT hrc = host->COMGETTER(NameServers)(ComSafeArrayAsOutParam(nameServers));
    if (FAILED(hrc))
    {
        LogRel(("NATNetwork: Failed to get name servers from host with %Rhrc\n", hrc));
        return;
    }
    ComPtr<IDHCPGlobalConfig> pDHCPConfig;
    hrc = m->dhcpServer->COMGETTER(GlobalConfig)(pDHCPConfig.asOutParam());
    if (FAILED(hrc))
    {
        LogRel(("NATNetwork: Failed to get global DHCP config when updating domain name server option with %Rhrc\n", hrc));
        return;
    }

    size_t cAddresses = nameServers.size();
    if (cAddresses)
    {
        RTCList<RTCString> lstServers;
        /* The following code was copied (and adapted a bit) from VBoxNetDhcp::hostDnsServers */
        /*
        * Recent fashion is to run dnsmasq on 127.0.1.1 which we
        * currently can't map.  If that's the only nameserver we've got,
        * we need to use DNS proxy for VMs to reach it.
        */
        bool fUnmappedLoopback = false;

        for (size_t i = 0; i < cAddresses; ++i)
        {
            com::Utf8Str strNameServerAddress(nameServers[i]);
            RTNETADDRIPV4 addr;
            vrc = RTNetStrToIPv4Addr(strNameServerAddress.c_str(), &addr);
            if (RT_FAILURE(vrc))
            {
                LogRel(("NATNetwork: Failed to parse IP address %s with %Rrc\n", strNameServerAddress.c_str(), vrc));
                continue;
            }

            if (addr.u == INADDR_ANY)
            {
                /*
                * This doesn't seem to be very well documented except for
                * RTFS of res_init.c, but INADDR_ANY is a valid value for
                * for "nameserver".
                */
                addr.u = RT_H2N_U32_C(INADDR_LOOPBACK);
            }

            if (addr.au8[0] == 127)
            {
                settings::NATLoopbackOffsetList::const_iterator it;

                it = std::find(m->s.llHostLoopbackOffsetList.begin(),
                               m->s.llHostLoopbackOffsetList.end(),
                               strNameServerAddress);
                if (it == m->s.llHostLoopbackOffsetList.end())
                {
                    fUnmappedLoopback = true;
                    continue;
                }
                addr.u = RT_H2N_U32(RT_N2H_U32(networkid.u) + it->u32Offset);
            }
            lstServers.append(RTCStringFmt("%RTnaipv4", addr));
        }

        if (lstServers.isEmpty() && fUnmappedLoopback)
            lstServers.append(RTCStringFmt("%RTnaipv4", networkid.u | RT_H2N_U32_C(1U))); /* proxy */

        hrc = pDHCPConfig->SetOption(DHCPOption_DomainNameServers, DHCPOptionEncoding_Normal, Bstr(RTCString::join(lstServers, " ")).raw());
        if (FAILED(hrc))
            LogRel(("NATNetwork: Failed to add domain name server option '%s' with %Rhrc\n", RTCString::join(lstServers, " ").c_str(), hrc));
    }
    else
        pDHCPConfig->RemoveOption(DHCPOption_DomainNameServers);
}

void NATNetwork::i_updateDnsOptions()
{
    ComPtr<IHost> host;
    if (SUCCEEDED(m->pVirtualBox->COMGETTER(Host)(host.asOutParam())))
    {
        i_updateDomainNameOption(host);
        i_updateDomainNameServerOption(host);
    }
}


HRESULT  NATNetwork::start()
{
#ifdef VBOX_WITH_NAT_SERVICE
    if (!m->s.fEnabled) return S_OK;
    AssertReturn(!m->s.strNetworkName.isEmpty(), E_FAIL);

    m->NATRunner.resetArguments();
    m->NATRunner.addArgPair(NetworkServiceRunner::kpszKeyNetwork, Utf8Str(m->s.strNetworkName).c_str());

    /* No portforwarding rules from command-line, all will be fetched via API */

    if (m->s.fNeedDhcpServer)
    {
        /*
         * Just to as idea... via API (on creation user pass the cidr of network and)
         * and we calculate it's addreses (mutable?).
         */

        /** @todo r=aeichner This comment doen't seem to reflect reality
         *                   When FindDHCPServerByNetworkName() returns E_INVALIDARG
         *                   a new DHCP server is created while the comment states that
         *                   the server should already exist... */
        /** @todo r=aeichner Returning an error should set an error message! */
        /*
         * Configuration and running DHCP server:
         * 1. find server first createDHCPServer
         * 2. if return status is E_INVALIDARG => server already exists just find and start.
         * 3. if return status neither E_INVALIDARG nor S_OK => return E_FAIL
         * 4. if return status S_OK proceed to DHCP server configuration
         * 5. call setConfiguration() and pass all required parameters
         * 6. start dhcp server.
         */
        HRESULT hrc = m->pVirtualBox->FindDHCPServerByNetworkName(Bstr(m->s.strNetworkName).raw(),
                                                                  m->dhcpServer.asOutParam());
        switch (hrc)
        {
            case E_INVALIDARG:
            {
                LogFunc(("gateway: %s, dhcpserver:%s, dhcplowerip:%s, dhcpupperip:%s\n",
                         m->IPv4Gateway.c_str(),
                         m->IPv4DhcpServer.c_str(),
                         m->IPv4DhcpServerLowerIp.c_str(),
                         m->IPv4DhcpServerUpperIp.c_str()));

                /* server haven't beeen found let create it then */
                hrc = m->pVirtualBox->CreateDHCPServer(Bstr(m->s.strNetworkName).raw(),
                                                       m->dhcpServer.asOutParam());
                if (FAILED(hrc))
                  return E_FAIL;

                hrc = m->dhcpServer->COMSETTER(Enabled)(true);
                if (FAILED(hrc))
                  return setError(hrc, tr("Failed to enable DHCP server for network '%s'"),
                                          m->s.strNetworkName.c_str());

                hrc = m->dhcpServer->SetConfiguration(Bstr(m->IPv4DhcpServer).raw(),
                                                      Bstr(m->IPv4NetworkMask).raw(),
                                                      Bstr(m->IPv4DhcpServerLowerIp).raw(),
                                                      Bstr(m->IPv4DhcpServerUpperIp).raw());
                if (FAILED(hrc))
                  return setError(hrc, tr("Failed to set DHCP server configuration for network '%s'"),
                                          m->s.strNetworkName.c_str());

                break;
            }
            case S_OK:
                break;

            default:
                return E_FAIL;
        }

#ifdef VBOX_WITH_DHCPD
        i_updateDnsOptions();
#endif /* VBOX_WITH_DHCPD */
        /* XXX: AddGlobalOption(DhcpOpt_Router,) - enables attachement of DhcpServer to Main (no longer true with VBoxNetDhcpd). */
        ComPtr<IDHCPGlobalConfig> pDHCPConfig;
        hrc = m->dhcpServer->COMGETTER(GlobalConfig)(pDHCPConfig.asOutParam());
        if (FAILED(hrc))
        {
            LogRel(("NATNetwork: Failed to get global DHCP config when updating IPv4 gateway option with %Rhrc\n", hrc));
            m->dhcpServer.setNull();
            return E_FAIL;
        }
        pDHCPConfig->SetOption(DHCPOption_Routers, DHCPOptionEncoding_Normal, Bstr(m->IPv4Gateway).raw());

        hrc = m->dhcpServer->Start(Bstr::Empty.raw(), Bstr(TRUNKTYPE_WHATEVER).raw());
        if (FAILED(hrc))
        {
            m->dhcpServer.setNull();
            return E_FAIL;
        }
    }

    if (RT_SUCCESS(m->NATRunner.start(false /* KillProcOnStop */)))
    {
        m->pVirtualBox->i_onNATNetworkStartStop(m->s.strNetworkName, TRUE);
        return S_OK;
    }
    /** @todo missing setError()! */
    return E_FAIL;
#else
    ReturnComNotImplemented();
#endif
}

HRESULT NATNetwork::stop()
{
#ifdef VBOX_WITH_NAT_SERVICE
    m->pVirtualBox->i_onNATNetworkStartStop(m->s.strNetworkName, FALSE);

    if (!m->dhcpServer.isNull())
        m->dhcpServer->Stop();

    if (RT_SUCCESS(m->NATRunner.stop()))
        return S_OK;

    /** @todo missing setError()! */
    return E_FAIL;
#else
    ReturnComNotImplemented();
#endif
}


void NATNetwork::i_getPortForwardRulesFromMap(std::vector<com::Utf8Str> &aPortForwardRules, settings::NATRulesMap &aRules)
{
    aPortForwardRules.resize(aRules.size());
    size_t i = 0;
    for (settings::NATRulesMap::const_iterator it = aRules.begin();
         it != aRules.end(); ++it, ++i)
    {
        settings::NATRule r = it->second;
        aPortForwardRules[i] =  Utf8StrFmt("%s:%s:[%s]:%d:[%s]:%d",
                                           r.strName.c_str(),
                                           (r.proto == NATProtocol_TCP ? "tcp" : "udp"),
                                           r.strHostIP.c_str(),
                                           r.u16HostPort,
                                           r.strGuestIP.c_str(),
                                           r.u16GuestPort);
    }
}


int NATNetwork::i_findFirstAvailableOffset(ADDRESSLOOKUPTYPE addrType, uint32_t *poff)
{
    RTNETADDRIPV4 network, netmask;
    int vrc = RTCidrStrToIPv4(m->s.strIPv4NetworkCidr.c_str(), &network, &netmask);
    AssertRCReturn(vrc, vrc);

    uint32_t off;
    for (off = 1; off < ~netmask.u; ++off)
    {
        for (settings::NATLoopbackOffsetList::iterator it = m->s.llHostLoopbackOffsetList.begin();
             it != m->s.llHostLoopbackOffsetList.end();
             ++it)
        {
            if ((*it).u32Offset == off)
                continue;
        }

        if (off == m->offGateway)
        {
            if (addrType == ADDR_GATEWAY)
                break;
            else
                continue;
        }
        else if (off == m->offDhcp)
        {
            if (addrType == ADDR_DHCP)
                break;
            else
                continue;
        }
        else
            break;
    }

    if (poff)
        *poff = off;

    return VINF_SUCCESS;
}

int NATNetwork::i_recalculateIpv4AddressAssignments()
{
    RTNETADDRIPV4 network, netmask;
    int vrc = RTCidrStrToIPv4(m->s.strIPv4NetworkCidr.c_str(), &network, &netmask);
    AssertRCReturn(vrc, vrc);

    i_findFirstAvailableOffset(ADDR_GATEWAY, &m->offGateway);
    if (m->s.fNeedDhcpServer)
        i_findFirstAvailableOffset(ADDR_DHCP, &m->offDhcp);

    /* I don't remember the reason CIDR calculated on the host. */
    RTNETADDRIPV4 gateway = network;
    gateway.u += m->offGateway;
    gateway.u = RT_H2N_U32(gateway.u);
    char szTmpIp[16];
    RTStrPrintf(szTmpIp, sizeof(szTmpIp), "%RTnaipv4", gateway);
    m->IPv4Gateway = szTmpIp;

    if (m->s.fNeedDhcpServer)
    {
        RTNETADDRIPV4 dhcpserver = network;
        dhcpserver.u += m->offDhcp;

        /* XXX: adding more services should change the math here */
        RTNETADDRIPV4 dhcplowerip = network;
        uint32_t offDhcpLowerIp;
        i_findFirstAvailableOffset(ADDR_DHCPLOWERIP, &offDhcpLowerIp);
        dhcplowerip.u = RT_H2N_U32(dhcplowerip.u + offDhcpLowerIp);

        RTNETADDRIPV4 dhcpupperip;
        dhcpupperip.u = RT_H2N_U32((network.u | ~netmask.u) - 1);

        dhcpserver.u = RT_H2N_U32(dhcpserver.u);
        network.u = RT_H2N_U32(network.u);

        RTStrPrintf(szTmpIp, sizeof(szTmpIp), "%RTnaipv4", dhcpserver);
        m->IPv4DhcpServer = szTmpIp;
        RTStrPrintf(szTmpIp, sizeof(szTmpIp), "%RTnaipv4", dhcplowerip);
        m->IPv4DhcpServerLowerIp = szTmpIp;
        RTStrPrintf(szTmpIp, sizeof(szTmpIp), "%RTnaipv4", dhcpupperip);
        m->IPv4DhcpServerUpperIp = szTmpIp;

        LogFunc(("network:%RTnaipv4, dhcpserver:%RTnaipv4, dhcplowerip:%RTnaipv4, dhcpupperip:%RTnaipv4\n",
                 network, dhcpserver, dhcplowerip, dhcpupperip));
    }

    /* we need IPv4NetworkMask for NAT's gw service start */
    netmask.u = RT_H2N_U32(netmask.u);
    RTStrPrintf(szTmpIp, 16, "%RTnaipv4", netmask);
    m->IPv4NetworkMask = szTmpIp;

    LogFlowFunc(("gateway:%RTnaipv4, netmask:%RTnaipv4\n", gateway, netmask));
    return VINF_SUCCESS;
}


int NATNetwork::i_recalculateIPv6Prefix()
{
    RTNETADDRIPV4 net, mask;
    int vrc = RTCidrStrToIPv4(Utf8Str(m->s.strIPv4NetworkCidr).c_str(), &net, &mask);
    if (RT_FAILURE(vrc))
        return vrc;

    net.u = RT_H2N_U32(net.u);  /* XXX: fix RTCidrStrToIPv4! */

    /*
     * [fd17:625c:f037:XXXX::/64] - RFC 4193 (ULA) Locally Assigned
     * Global ID where XXXX, 16 bit Subnet ID, are two bytes from the
     * middle of the IPv4 address, e.g. :dead: for 10.222.173.1
     */
    RTNETADDRIPV6 prefix;
    RT_ZERO(prefix);

    prefix.au8[0] = 0xFD;
    prefix.au8[1] = 0x17;

    prefix.au8[2] = 0x62;
    prefix.au8[3] = 0x5C;

    prefix.au8[4] = 0xF0;
    prefix.au8[5] = 0x37;

    prefix.au8[6] = net.au8[1];
    prefix.au8[7] = net.au8[2];

    char szBuf[32];
    RTStrPrintf(szBuf, sizeof(szBuf), "%RTnaipv6/64", &prefix);

    m->s.strIPv6Prefix = szBuf;
    return VINF_SUCCESS;
}
