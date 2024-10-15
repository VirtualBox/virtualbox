/* $Id: PlatformImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Platform settings.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_PlatformImpl_h
#define MAIN_INCLUDED_PlatformImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "PlatformWrap.h"
#include "PlatformBase.h"

class GuestOSType;
class PlatformARM;
class PlatformX86;

namespace settings
{
    struct Platform;
}

class ATL_NO_VTABLE Platform :
    public PlatformWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(Platform)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent);
    HRESULT init(Machine *parent, Platform *that);
    HRESULT initCopy(Machine *parent, Platform *that);
    void uninit();

    // public internal methods
    HRESULT i_loadSettings(const settings::Platform &data);
    HRESULT i_saveSettings(settings::Platform &data);

    void i_rollback();
    void i_commit();
    void i_copyFrom(Platform *aThat);

    HRESULT i_initArchitecture(PlatformArchitecture_T aArchitecture, Platform *that = NULL, bool fCopy = false);
    HRESULT i_applyDefaults(GuestOSType *aOsType);

    PlatformArchitecture_T i_getArchitecture();

public:

    // wrapped IPlatform properties
    HRESULT getArchitecture(PlatformArchitecture_T *aArchitecture);
    HRESULT setArchitecture(PlatformArchitecture_T aArchitecture);
    HRESULT getProperties(ComPtr<IPlatformProperties> &aProperties);
    HRESULT getX86(ComPtr<IPlatformX86> &aX86);
    HRESULT getARM(ComPtr<IPlatformARM> &aARM);
    HRESULT getChipsetType(ChipsetType_T *aChipsetType);
    HRESULT setChipsetType(ChipsetType_T aChipsetType);
    HRESULT getIommuType(IommuType_T *aIommuType);
    HRESULT setIommuType(IommuType_T aIommuType);
    HRESULT getRTCUseUTC(BOOL *aRTCUseUTC);
    HRESULT setRTCUseUTC(BOOL aRTCUseUTC);

private:

    // private functions only used internally
    void uninitArchitecture();

private:

    // wrapped IPlatform methods

    Machine * const mParent;

    struct Data;
    Data *m;

    // The following fields need special backup/rollback/commit handling,
    // so they cannot be a part of Data above.

    /** Contains x86-specific platform data.
     *  Only created if platform architecture is x86. */
    const ComObjPtr<PlatformX86> mX86;
#ifdef VBOX_WITH_VIRT_ARMV8
    /** Contains ARM-specific platform data.
     *  Only created if platform architecture is ARM. */
    const ComObjPtr<PlatformARM> mARM;
#endif
};

#endif /* !MAIN_INCLUDED_PlatformImpl_h */

