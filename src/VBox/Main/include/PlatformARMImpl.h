/* $Id: PlatformARMImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - ARM platform settings.
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

#ifndef MAIN_INCLUDED_PlatformARMImpl_h
#define MAIN_INCLUDED_PlatformARMImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "PlatformARMWrap.h"
#include "PlatformBase.h"

class Machine;
class Platform;

namespace settings
{
    struct PlatformARM;
}

class ATL_NO_VTABLE PlatformARM
    : public PlatformARMWrap, public PlatformBase
{
public:

    DECLARE_COMMON_CLASS_METHODS(PlatformARM)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Platform *aParent, Machine *aMachine);
    HRESULT init(Platform *aParent, Machine *aMachine, PlatformARM *aThat);
    HRESULT initCopy(Platform *aParent, Machine *aMachine, PlatformARM *aThat);
    void uninit();

    // public internal methods
    void i_rollback();
    void i_commit();
    void i_copyFrom(PlatformARM *aThat);

    // public internal methods
    HRESULT i_loadSettings(const settings::PlatformARM &data);
    HRESULT i_saveSettings(settings::PlatformARM &data);
    HRESULT i_applyDefaults(GuestOSType *aOsType);
};
#endif /* !MAIN_INCLUDED_PlatformARMImpl_h */

