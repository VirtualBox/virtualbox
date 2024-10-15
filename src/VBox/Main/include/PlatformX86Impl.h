/* $Id: PlatformX86Impl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - x86 platform settings.
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

#ifndef MAIN_INCLUDED_PlatformX86Impl_h
#define MAIN_INCLUDED_PlatformX86Impl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

class Machine;
class Platform;

namespace settings
{
    struct PlatformX86;
}

#include "PlatformX86Wrap.h"
#include "PlatformBase.h"

class ATL_NO_VTABLE PlatformX86
    : public PlatformX86Wrap, public PlatformBase
{
public:

    DECLARE_COMMON_CLASS_METHODS(PlatformX86)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Platform *aParent, Machine *aMachine);
    HRESULT init(Platform *aParent, Machine *aMachine, PlatformX86 *aThat);
    HRESULT initCopy(Platform *aParent, Machine *aMachine, PlatformX86 *aThat);
    void uninit();

    // public internal methods
    void i_rollback();
    void i_commit();
    void i_copyFrom(PlatformX86 *aThat);

    // public internal methods
    HRESULT i_loadSettings(const settings::PlatformX86 &data);
    HRESULT i_saveSettings(settings::PlatformX86 &data);
    HRESULT i_applyDefaults(GuestOSType *aOsType);

private:

    // wrapped IPlatformX86 properties
    HRESULT getHPETEnabled(BOOL *aHPETEnabled);
    HRESULT setHPETEnabled(BOOL aHPETEnabled);

    // wrapped IPlatformX86 methods
    HRESULT getCPUProperty(CPUPropertyTypeX86_T aProperty, BOOL *aValue);
    HRESULT setCPUProperty(CPUPropertyTypeX86_T aProperty, BOOL aValue);
    HRESULT getCPUIDLeafByOrdinal(ULONG aOrdinal, ULONG *aIdx, ULONG *aSubIdx, ULONG *aValEax, ULONG *aValEbx, ULONG *aValEcx, ULONG *aValEdx);
    HRESULT getCPUIDLeaf(ULONG aIdx, ULONG aSubIdx, ULONG *aValEax, ULONG *aValEbx, ULONG *aValEcx, ULONG *aValEdx);
    HRESULT setCPUIDLeaf(ULONG aIdx, ULONG aSubIdx, ULONG aValEax, ULONG aValEbx, ULONG aValEcx, ULONG aValEdx);
    HRESULT removeCPUIDLeaf(ULONG aIdx, ULONG aSubIdx);
    HRESULT removeAllCPUIDLeaves();
    HRESULT getHWVirtExProperty(HWVirtExPropertyType_T aProperty, BOOL *aValue);
    HRESULT setHWVirtExProperty(HWVirtExPropertyType_T aProperty, BOOL aValue);
};

#endif /* !MAIN_INCLUDED_PlatformX86Impl_h */

