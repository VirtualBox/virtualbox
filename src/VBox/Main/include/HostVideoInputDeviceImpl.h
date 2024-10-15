/* $Id: HostVideoInputDeviceImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * A host video capture device description.
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

#ifndef MAIN_INCLUDED_HostVideoInputDeviceImpl_h
#define MAIN_INCLUDED_HostVideoInputDeviceImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "HostVideoInputDeviceWrap.h"

#include <list>

class HostVideoInputDevice;

typedef std::list<ComObjPtr<HostVideoInputDevice> > HostVideoInputDeviceList;

class ATL_NO_VTABLE HostVideoInputDevice :
    public HostVideoInputDeviceWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(HostVideoInputDevice)

    HRESULT FinalConstruct();
    void FinalRelease();

    /* Public initializer/uninitializer for internal purposes only. */
    HRESULT init(const com::Utf8Str &name, const com::Utf8Str &path, const com::Utf8Str &alias);
    void uninit();

    static HRESULT queryHostDevices(VirtualBox *pVirtualBox, HostVideoInputDeviceList *pList);

private:

    // wrapped IHostVideoInputDevice properties
    virtual HRESULT getName(com::Utf8Str &aName) { aName = m.name; return S_OK; }
    virtual HRESULT getPath(com::Utf8Str &aPath) { aPath = m.path; return S_OK; }
    virtual HRESULT getAlias(com::Utf8Str &aAlias) { aAlias = m.alias; return S_OK; }

    /* Data. */
    struct Data
    {
        Data()
        {
        }

        com::Utf8Str name;
        com::Utf8Str path;
        com::Utf8Str alias;
    };

    Data m;
};

#endif /* !MAIN_INCLUDED_HostVideoInputDeviceImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
