/* $Id: NATEngineImpl.h 107610 2025-01-08 21:14:58Z jack.doherty@oracle.com $ */

/** @file
 *
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

#ifndef MAIN_INCLUDED_NATEngineImpl_h
#define MAIN_INCLUDED_NATEngineImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "NATEngineWrap.h"

namespace settings
{
    struct NAT;
}


class ATL_NO_VTABLE NATEngine :
    public NATEngineWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(NATEngine)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(Machine *aParent, INetworkAdapter *aAdapter);
    HRESULT init(Machine *aParent, INetworkAdapter *aAdapter, NATEngine *aThat);
    HRESULT initCopy(Machine *aParent, INetworkAdapter *aAdapter, NATEngine *aThat);
    void uninit();

    bool i_isModified();
    void i_rollback();
    void i_commit();
    void i_copyFrom(NATEngine *aThat);
    void i_applyDefaults();
    bool i_hasDefaults();
    HRESULT i_loadSettings(const settings::NAT &data);
    HRESULT i_saveSettings(settings::NAT &data);

private:

    // wrapped INATEngine properties
    HRESULT setNetwork(const com::Utf8Str &aNetwork);
    HRESULT getNetwork(com::Utf8Str &aNetwork);
    HRESULT setHostIP(const com::Utf8Str &aHostIP);
    HRESULT getHostIP(com::Utf8Str &aBindIP);
    HRESULT setLocalhostReachable(BOOL fLocalhostReachable);
    HRESULT getLocalhostReachable(BOOL *pfLocalhostReachable);
    HRESULT setForwardBroadcast(BOOL fForwardBroadcast);
    HRESULT getForwardBroadcast(BOOL *pfForwardBroadcast);
    /* TFTP properties */
    HRESULT setTFTPPrefix(const com::Utf8Str &aTFTPPrefix);
    HRESULT getTFTPPrefix(com::Utf8Str &aTFTPPrefix);
    HRESULT setTFTPBootFile(const com::Utf8Str &aTFTPBootFile);
    HRESULT getTFTPBootFile(com::Utf8Str &aTFTPBootFile);
    HRESULT setTFTPNextServer(const com::Utf8Str &aTFTPNextServer);
    HRESULT getTFTPNextServer(com::Utf8Str &aTFTPNextServer);
    /* DNS properties */
    HRESULT setDNSPassDomain(BOOL aDNSPassDomain);
    HRESULT getDNSPassDomain(BOOL *aDNSPassDomain);
    HRESULT setDNSProxy(BOOL aDNSProxy);
    HRESULT getDNSProxy(BOOL *aDNSProxy);
    HRESULT getDNSUseHostResolver(BOOL *aDNSUseHostResolver);
    HRESULT setDNSUseHostResolver(BOOL aDNSUseHostResolver);
    /* Alias properties */
    HRESULT setAliasMode(ULONG aAliasMode);
    HRESULT getAliasMode(ULONG *aAliasMode);

    HRESULT getRedirects(std::vector<com::Utf8Str> &aRedirects);

    HRESULT setNetworkSettings(ULONG aMtu,
                               ULONG aSockSnd,
                               ULONG aSockRcv,
                               ULONG aTcpWndSnd,
                               ULONG aTcpWndRcv);

    HRESULT getNetworkSettings(ULONG *aMtu,
                               ULONG *aSockSnd,
                               ULONG *aSockRcv,
                               ULONG *aTcpWndSnd,
                               ULONG *aTcpWndRcv);

    HRESULT addRedirect(const com::Utf8Str  &aName,
                              NATProtocol_T aProto,
                        const com::Utf8Str  &aHostIP,
                              USHORT        aHostPort,
                        const com::Utf8Str  &aGuestIP,
                              USHORT        aGuestPort);

    HRESULT removeRedirect(const com::Utf8Str &aName);

    struct Data;
    Data *mData;
    const ComObjPtr<NATEngine> mPeer;
    Machine * const mParent;
    INetworkAdapter * const mAdapter;
};
#endif /* !MAIN_INCLUDED_NATEngineImpl_h */
