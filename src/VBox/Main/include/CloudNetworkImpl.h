/* $Id: CloudNetworkImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ICloudNetwork implementation header, lives in VBoxSVC.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_CloudNetworkImpl_h
#define MAIN_INCLUDED_CloudNetworkImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "CloudNetworkWrap.h"

namespace settings
{
    struct CloudNetwork;
}

class ATL_NO_VTABLE CloudNetwork :
    public CloudNetworkWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(CloudNetwork)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(VirtualBox *aVirtualBox, com::Utf8Str aName);
    HRESULT i_loadSettings(const settings::CloudNetwork &data);
    void uninit();
    HRESULT i_saveSettings(settings::CloudNetwork &data);

    // Internal methods
    Utf8Str i_getNetworkName();
    Utf8Str i_getProvider();
    Utf8Str i_getProfile();
    Utf8Str i_getNetworkId();
private:

    // Wrapped ICloudNetwork properties
    HRESULT getNetworkName(com::Utf8Str &aNetworkName);
    HRESULT setNetworkName(const com::Utf8Str &aNetworkName);
    HRESULT getEnabled(BOOL *aEnabled);
    HRESULT setEnabled(BOOL aEnabled);
    HRESULT getProvider(com::Utf8Str &aProvider);
    HRESULT setProvider(const com::Utf8Str &aProvider);
    HRESULT getProfile(com::Utf8Str &aProfile);
    HRESULT setProfile(const com::Utf8Str &aProfile);
    HRESULT getNetworkId(com::Utf8Str &aNetworkId);
    HRESULT setNetworkId(const com::Utf8Str &aNetworkId);

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_CloudNetworkImpl_h */
