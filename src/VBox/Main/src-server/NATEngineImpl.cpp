/* $Id: NATEngineImpl.cpp 107914 2025-01-16 14:50:32Z knut.osmundsen@oracle.com $ */
/** @file
 * Implementation of INATEngine in VBoxSVC.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_NATENGINE
#include "NATEngineImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include "MachineImpl.h"

#include <iprt/string.h>
#include <iprt/cpp/utils.h>

#include <iprt/errcore.h>
#include <VBox/settings.h>
#include <VBox/com/array.h>

struct NATEngine::Data
{
    Backupable<settings::NAT> m;
};


// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

NATEngine::NATEngine():mData(NULL), mParent(NULL), mAdapter(NULL) {}
NATEngine::~NATEngine(){}

HRESULT NATEngine::FinalConstruct()
{
    return BaseFinalConstruct();
}

void NATEngine::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


HRESULT NATEngine::init(Machine *aParent, INetworkAdapter *aAdapter)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);
    autoInitSpan.setSucceeded();
    mData = new Data();
    mData->m.allocate();
    mData->m->strNetwork.setNull();
    mData->m->strBindIP.setNull();
    unconst(mParent) = aParent;
    unconst(mAdapter) = aAdapter;
    return S_OK;
}

HRESULT NATEngine::init(Machine *aParent, INetworkAdapter *aAdapter, NATEngine *aThat)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);
    Log(("init that:%p this:%p\n", aThat, this));

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);

    mData = new Data();
    mData->m.share(aThat->mData->m);
    unconst(mParent) = aParent;
    unconst(mAdapter) = aAdapter;
    unconst(mPeer) = aThat;
    autoInitSpan.setSucceeded();
    return S_OK;
}

HRESULT NATEngine::initCopy(Machine *aParent, INetworkAdapter *aAdapter, NATEngine *aThat)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    Log(("initCopy that:%p this:%p\n", aThat, this));

    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);

    mData = new Data();
    mData->m.attachCopy(aThat->mData->m);
    unconst(mAdapter) = aAdapter;
    unconst(mParent) = aParent;
    autoInitSpan.setSucceeded();

    return S_OK;
}


void NATEngine::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    mData->m.free();
    delete mData;
    mData = NULL;
    unconst(mPeer) = NULL;
    unconst(mParent) = NULL;
}

bool NATEngine::i_isModified()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    bool fModified = mData->m.isBackedUp();
    return fModified;
}

void NATEngine::i_rollback()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData->m.rollback();
}

void NATEngine::i_commit()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(mPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(mPeer, this COMMA_LOCKVAL_SRC_POS);
    if (mData->m.isBackedUp())
    {
        mData->m.commit();
        if (mPeer)
            mPeer->mData->m.attach(mData->m);
    }
}

void NATEngine::i_copyFrom(NATEngine *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    mData->m.assignCopy(aThat->mData->m);
}

void NATEngine::i_applyDefaults()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData->m->fLocalhostReachable = false; /* Applies to new VMs only, see @bugref{9896} */
    mData->m->fForwardBroadcast = false;   /* Applies to new VMs only. see @bugref{10268} */
}

bool NATEngine::i_hasDefaults()
{
   /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), true);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    return mData->m->areDefaultSettings(mParent->i_getSettingsVersion());
}

HRESULT NATEngine::getNetworkSettings(ULONG *aMtu, ULONG *aSockSnd, ULONG *aSockRcv, ULONG *aTcpWndSnd, ULONG *aTcpWndRcv)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (aMtu)
        *aMtu = mData->m->u32Mtu;
    if (aSockSnd)
        *aSockSnd = mData->m->u32SockSnd;
    if (aSockRcv)
        *aSockRcv = mData->m->u32SockRcv;
    if (aTcpWndSnd)
        *aTcpWndSnd = mData->m->u32TcpSnd;
    if (aTcpWndRcv)
        *aTcpWndRcv = mData->m->u32TcpRcv;

    return S_OK;
}

HRESULT NATEngine::setNetworkSettings(ULONG aMtu, ULONG aSockSnd, ULONG aSockRcv, ULONG aTcpWndSnd, ULONG aTcpWndRcv)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (   aMtu || aSockSnd || aSockRcv
        || aTcpWndSnd || aTcpWndRcv)
    {
        mData->m.backup();
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    if (aMtu)
        mData->m->u32Mtu = aMtu;
    if (aSockSnd)
        mData->m->u32SockSnd = aSockSnd;
    if (aSockRcv)
        mData->m->u32SockRcv = aSockSnd;
    if (aTcpWndSnd)
        mData->m->u32TcpSnd = aTcpWndSnd;
    if (aTcpWndRcv)
        mData->m->u32TcpRcv = aTcpWndRcv;

    return S_OK;
}


HRESULT NATEngine::getRedirects(std::vector<com::Utf8Str> &aRedirects)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aRedirects.resize(mData->m->mapRules.size());
    size_t i = 0;
    settings::NATRulesMap::const_iterator it;
    for (it = mData->m->mapRules.begin(); it != mData->m->mapRules.end(); ++it, ++i)
    {
        settings::NATRule r = it->second;
        aRedirects[i] = Utf8StrFmt("%s,%d,%s,%d,%s,%d",
                                   r.strName.c_str(),
                                   r.proto,
                                   r.strHostIP.c_str(),
                                   r.u16HostPort,
                                   r.strGuestIP.c_str(),
                                   r.u16GuestPort);
    }
    return S_OK;
}

HRESULT NATEngine::addRedirect(const com::Utf8Str &aName, NATProtocol_T aProto, const com::Utf8Str &aHostIP,
                               USHORT aHostPort, const com::Utf8Str &aGuestIP, USHORT aGuestPort)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Utf8Str name = aName;
    settings::NATRule r;
    const char *proto;
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
        name = Utf8StrFmt("%s_%d_%d", proto, aHostPort, aGuestPort);
    else
    {
        const char *s;
        char c;

        for (s = name.c_str(); (c = *s) != '\0'; ++s)
        {
            if (c == ',') /* we use csv in several places e.g. GetRedirects or natpf<N> argument */
                return setError(E_INVALIDARG,
                                tr("'%c' - invalid character in NAT rule name"), c);
        }
    }

    settings::NATRulesMap::iterator it;
    for (it = mData->m->mapRules.begin(); it != mData->m->mapRules.end(); ++it)
    {
        r = it->second;
        if (it->first == name)
            return setError(E_INVALIDARG,
                            tr("A NAT rule of this name already exists"));
        if (   r.strHostIP == aHostIP
            && r.u16HostPort == aHostPort
            && r.proto == aProto)
            return setError(E_INVALIDARG,
                            tr("A NAT rule for this host port and this host IP already exists"));
    }

    mData->m.backup();
    r.strName = name.c_str();
    r.proto = aProto;
    r.strHostIP = aHostIP;
    r.u16HostPort = aHostPort;
    r.strGuestIP = aGuestIP;
    r.u16GuestPort = aGuestPort;
    mData->m->mapRules.insert(std::make_pair(name, r));
    mParent->i_setModified(Machine::IsModified_NetworkAdapters);

    ULONG ulSlot;
    mAdapter->COMGETTER(Slot)(&ulSlot);

    alock.release();
    mParent->i_onNATRedirectRuleChanged(ulSlot, FALSE, name, aProto, r.strHostIP, r.u16HostPort, r.strGuestIP, r.u16GuestPort);
    return S_OK;
}

HRESULT NATEngine::removeRedirect(const com::Utf8Str &aName)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    settings::NATRulesMap::iterator it = mData->m->mapRules.find(aName);
    if (it == mData->m->mapRules.end())
        return E_INVALIDARG;
    mData->m.backup();
    /*
     * NB: "it" may now point to the backup!  In that case it's ok to
     * get data from the backup copy of s.mapRules via it, but we can't
     * erase(it) from potentially new s.mapRules.
     */
    settings::NATRule r = it->second;
    ULONG ulSlot;
    mAdapter->COMGETTER(Slot)(&ulSlot);

    mData->m->mapRules.erase(aName); /* NB: erase by key, "it" may not be valid */
    mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    alock.release();
    mParent->i_onNATRedirectRuleChanged(ulSlot, TRUE, aName, r.proto, r.strHostIP, r.u16HostPort, r.strGuestIP, r.u16GuestPort);
    return S_OK;
}

HRESULT NATEngine::i_loadSettings(const settings::NAT &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData->m.assignCopy(&data);
    return S_OK;
}


HRESULT NATEngine::i_saveSettings(settings::NAT &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = S_OK;
    data = *mData->m.data();
    return hrc;
}

HRESULT NATEngine::setNetwork(const com::Utf8Str &aNetwork)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData->m->strNetwork != aNetwork)
    {
        mData->m.backup();
        mData->m->strNetwork = aNetwork;
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}


HRESULT NATEngine::getNetwork(com::Utf8Str &aNetwork)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData->m->strNetwork.isEmpty())
    {
        aNetwork = mData->m->strNetwork;
        Log(("Getter (this:%p) Network: %s\n", this, mData->m->strNetwork.c_str()));
    }
    return S_OK;
}

HRESULT NATEngine::setHostIP(const com::Utf8Str &aHostIP)
{
    if (aHostIP.isNotEmpty())
    {
        RTNETADDRIPV4 addr;

        /* parses as an IPv4 address */
        int vrc = RTNetStrToIPv4Addr(aHostIP.c_str(), &addr);
        if (RT_FAILURE(vrc))
            return setError(E_INVALIDARG, "Invalid IPv4 address \"%s\"", aHostIP.c_str());

        /* is a unicast address */
        if ((addr.u & RT_N2H_U32_C(0xe0000000)) == RT_N2H_U32_C(0xe0000000))
            return setError(E_INVALIDARG, "Cannot bind to a multicast address %s", aHostIP.c_str());
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData->m->strBindIP != aHostIP)
    {
        mData->m.backup();
        mData->m->strBindIP = aHostIP;
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::getHostIP(com::Utf8Str &aBindIP)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!mData->m->strBindIP.isEmpty())
        aBindIP = mData->m->strBindIP;
    return S_OK;
}

HRESULT NATEngine::setLocalhostReachable(BOOL fLocalhostReachable)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->m->fLocalhostReachable != RT_BOOL(fLocalhostReachable))
    {
        mData->m.backup();
        mData->m->fLocalhostReachable = RT_BOOL(fLocalhostReachable);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::getLocalhostReachable(BOOL *pfLocalhostReachable)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *pfLocalhostReachable = mData->m->fLocalhostReachable;
    return S_OK;
}

HRESULT NATEngine::setForwardBroadcast(BOOL fForwardBroadcast)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->m->fForwardBroadcast != RT_BOOL(fForwardBroadcast))
    {
        mData->m.backup();
        mData->m->fForwardBroadcast = RT_BOOL(fForwardBroadcast);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::getForwardBroadcast(BOOL *pfForwardBroadcast)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *pfForwardBroadcast = mData->m->fForwardBroadcast;
    return S_OK;
}

HRESULT NATEngine::setTFTPPrefix(const com::Utf8Str &aTFTPPrefix)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData->m->strTFTPPrefix != aTFTPPrefix)
    {
        mData->m.backup();
        mData->m->strTFTPPrefix = aTFTPPrefix;
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}


HRESULT NATEngine::getTFTPPrefix(com::Utf8Str &aTFTPPrefix)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!mData->m->strTFTPPrefix.isEmpty())
    {
        aTFTPPrefix = mData->m->strTFTPPrefix;
        Log(("Getter (this:%p) TFTPPrefix: %s\n", this, mData->m->strTFTPPrefix.c_str()));
    }
    return S_OK;
}

HRESULT NATEngine::setTFTPBootFile(const com::Utf8Str &aTFTPBootFile)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData->m->strTFTPBootFile != aTFTPBootFile)
    {
        mData->m.backup();
        mData->m->strTFTPBootFile = aTFTPBootFile;
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}


HRESULT NATEngine::getTFTPBootFile(com::Utf8Str &aTFTPBootFile)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData->m->strTFTPBootFile.isEmpty())
    {
        aTFTPBootFile = mData->m->strTFTPBootFile;
        Log(("Getter (this:%p) BootFile: %s\n", this, mData->m->strTFTPBootFile.c_str()));
    }
    return S_OK;
}


HRESULT NATEngine::setTFTPNextServer(const com::Utf8Str &aTFTPNextServer)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData->m->strTFTPNextServer != aTFTPNextServer)
    {
        mData->m.backup();
        mData->m->strTFTPNextServer = aTFTPNextServer;
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::getTFTPNextServer(com::Utf8Str &aTFTPNextServer)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData->m->strTFTPNextServer.isEmpty())
    {
        aTFTPNextServer =  mData->m->strTFTPNextServer;
        Log(("Getter (this:%p) NextServer: %s\n", this, mData->m->strTFTPNextServer.c_str()));
    }
    return S_OK;
}

/* DNS */
HRESULT NATEngine::setDNSPassDomain(BOOL aDNSPassDomain)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->m->fDNSPassDomain != RT_BOOL(aDNSPassDomain))
    {
        mData->m.backup();
        mData->m->fDNSPassDomain = RT_BOOL(aDNSPassDomain);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::getDNSPassDomain(BOOL *aDNSPassDomain)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aDNSPassDomain = mData->m->fDNSPassDomain;
    return S_OK;
}


HRESULT NATEngine::setDNSProxy(BOOL aDNSProxy)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData->m->fDNSProxy != RT_BOOL(aDNSProxy))
    {
        mData->m.backup();
        mData->m->fDNSProxy = RT_BOOL(aDNSProxy);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::getDNSProxy(BOOL *aDNSProxy)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aDNSProxy = mData->m->fDNSProxy;
    return S_OK;
}


HRESULT NATEngine::getDNSUseHostResolver(BOOL *aDNSUseHostResolver)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aDNSUseHostResolver = mData->m->fDNSUseHostResolver;
    return S_OK;
}


HRESULT NATEngine::setDNSUseHostResolver(BOOL aDNSUseHostResolver)
{
    if (mData->m->fDNSUseHostResolver != RT_BOOL(aDNSUseHostResolver))
    {
        mData->m.backup();
        mData->m->fDNSUseHostResolver = RT_BOOL(aDNSUseHostResolver);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::setAliasMode(ULONG aAliasMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    ULONG uAliasMode = (mData->m->fAliasUseSamePorts ? NATAliasMode_AliasUseSamePorts : 0);
    uAliasMode |= (mData->m->fAliasLog ? NATAliasMode_AliasLog : 0);
    uAliasMode |= (mData->m->fAliasProxyOnly ? NATAliasMode_AliasProxyOnly : 0);
    if (uAliasMode != aAliasMode)
    {
        mData->m.backup();
        mData->m->fAliasUseSamePorts = RT_BOOL(aAliasMode & NATAliasMode_AliasUseSamePorts);
        mData->m->fAliasLog = RT_BOOL(aAliasMode & NATAliasMode_AliasLog);
        mData->m->fAliasProxyOnly = RT_BOOL(aAliasMode & NATAliasMode_AliasProxyOnly);
        mParent->i_setModified(Machine::IsModified_NetworkAdapters);
    }
    return S_OK;
}

HRESULT NATEngine::getAliasMode(ULONG *aAliasMode)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    ULONG uAliasMode = (mData->m->fAliasUseSamePorts ? NATAliasMode_AliasUseSamePorts : 0);
    uAliasMode |= (mData->m->fAliasLog ? NATAliasMode_AliasLog : 0);
    uAliasMode |= (mData->m->fAliasProxyOnly ? NATAliasMode_AliasProxyOnly : 0);
    *aAliasMode = uAliasMode;
    return S_OK;
}

