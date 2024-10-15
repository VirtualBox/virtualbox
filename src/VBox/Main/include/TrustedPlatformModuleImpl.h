/* $Id: TrustedPlatformModuleImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox COM class implementation - Machine Trusted Platform Module settings.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_TrustedPlatformModuleImpl_h
#define MAIN_INCLUDED_TrustedPlatformModuleImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "TrustedPlatformModuleWrap.h"

class GuestOSType;

namespace settings
{
    struct TpmSettings;
}

class ATL_NO_VTABLE TrustedPlatformModule :
    public TrustedPlatformModuleWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(TrustedPlatformModule)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *parent);
    HRESULT init(Machine *parent, TrustedPlatformModule *that);
    HRESULT initCopy(Machine *parent, TrustedPlatformModule *that);
    void uninit();

    // public methods for internal purposes only
    HRESULT i_loadSettings(const settings::TpmSettings &data);
    HRESULT i_saveSettings(settings::TpmSettings &data);

    void i_rollback();
    void i_commit();
    void i_copyFrom(TrustedPlatformModule *aThat);
    void i_applyDefaults(GuestOSType *aOsType);

private:

    // wrapped ITrustedPlatformModule properties
    HRESULT getType(TpmType_T *aType);
    HRESULT setType(TpmType_T aType);
    HRESULT getLocation(com::Utf8Str &location);
    HRESULT setLocation(const com::Utf8Str &location);

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_TrustedPlatformModuleImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
