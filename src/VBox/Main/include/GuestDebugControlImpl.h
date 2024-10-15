/* $Id: GuestDebugControlImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox/GuestDebugControl COM class implementation
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_GuestDebugControlImpl_h
#define MAIN_INCLUDED_GuestDebugControlImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestDebugControlWrap.h"

namespace settings
{
    struct Debugging;
}

class ATL_NO_VTABLE GuestDebugControl :
    public GuestDebugControlWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(GuestDebugControl)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent);
    HRESULT init(Machine *aParent, GuestDebugControl *aThat);
    HRESULT initCopy(Machine *aParent, GuestDebugControl *aThat);
    void uninit();

    // public internal methods
    HRESULT i_loadSettings(const settings::Debugging &data);
    HRESULT i_saveSettings(settings::Debugging &data);
    void i_rollback();
    void i_commit();
    void i_copyFrom(GuestDebugControl *aThat);
    Machine *i_getMachine() const;

private:

    // wrapped IGuestDebugControl properties
    HRESULT getDebugProvider(GuestDebugProvider_T *aDebugProvider);
    HRESULT setDebugProvider(GuestDebugProvider_T aDebugProvider);
    HRESULT getDebugIoProvider(GuestDebugIoProvider_T *aDebugIoProvider);
    HRESULT setDebugIoProvider(GuestDebugIoProvider_T aDebugIoProvider);
    HRESULT getDebugAddress(com::Utf8Str &aAddress);
    HRESULT setDebugAddress(const com::Utf8Str &aAddress);
    HRESULT getDebugPort(ULONG *aPort);
    HRESULT setDebugPort(ULONG aPort);

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_GuestDebugControlImpl_h */
