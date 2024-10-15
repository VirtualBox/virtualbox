/* $Id: USBControllerImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VBox USBController COM Class declaration.
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_USBControllerImpl_h
#define MAIN_INCLUDED_USBControllerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "USBControllerWrap.h"

class HostUSBDevice;
class USBDeviceFilter;

namespace settings
{
    struct USBController;
}

class ATL_NO_VTABLE USBController :
    public USBControllerWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(USBController)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent, const com::Utf8Str &aName, USBControllerType_T enmType);
    HRESULT init(Machine *aParent, USBController *aThat, bool fReshare = false);
    HRESULT initCopy(Machine *aParent, USBController *aThat);
    void uninit();

    // public methods only for internal purposes
    void i_rollback();
    void i_commit();
    void i_copyFrom(USBController *aThat);
    void i_unshare();

    ComObjPtr<USBController> i_getPeer();
    const Utf8Str &i_getName() const;
    const USBControllerType_T &i_getControllerType() const;

private:

    // wrapped IUSBController properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT setName(const com::Utf8Str &aName);
    HRESULT getType(USBControllerType_T *aType);
    HRESULT setType(USBControllerType_T aType);
    HRESULT getUSBStandard(USHORT *aUSBStandard);

    void printList();

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_USBControllerImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
