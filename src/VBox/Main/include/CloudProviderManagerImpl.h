/* $Id: CloudProviderManagerImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_CloudProviderManagerImpl_h
#define MAIN_INCLUDED_CloudProviderManagerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "CloudProviderManagerWrap.h"

class ATL_NO_VTABLE CloudProviderManager
    : public CloudProviderManagerWrap
{
public:
    CloudProviderManager();
    virtual ~CloudProviderManager();

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(VirtualBox *aVirtualBox);
    void uninit();

#ifdef VBOX_WITH_EXTPACK
    // Safe helpers, take care of caller and lock themselves.
    bool i_canRemoveExtPack(IExtPack *aExtPack);
    void i_addExtPack(IExtPack *aExtPack);
#endif

private:
    // wrapped ICloudProviderManager attributes and methods
    HRESULT getProviders(std::vector<ComPtr<ICloudProvider> > &aProviders);
    HRESULT getProviderById(const com::Guid &aProviderId,
                            ComPtr<ICloudProvider> &aProvider);
    HRESULT getProviderByShortName(const com::Utf8Str &aProviderName,
                                   ComPtr<ICloudProvider> &aProvider);
    HRESULT getProviderByName(const com::Utf8Str &aProviderName,
                              ComPtr<ICloudProvider> &aProvider);

private:
#ifdef VBOX_WITH_EXTPACK
    typedef std::map<com::Utf8Str, ComPtr<ICloudProviderManager> > ExtPackNameCloudProviderManagerMap;
    ExtPackNameCloudProviderManagerMap m_mapCloudProviderManagers;

    typedef std::vector<com::Utf8Str> ExtPackNameVec;
    ExtPackNameVec m_astrExtPackNames;
#endif

    typedef std::vector<ComPtr<ICloudProvider> > CloudProviderVec;
    CloudProviderVec m_apCloudProviders;

    VirtualBox * const m_pVirtualBox;
};

#endif /* !MAIN_INCLUDED_CloudProviderManagerImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
