/* $Id: StorageControllerImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VBox StorageController COM Class declaration.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_StorageControllerImpl_h
#define MAIN_INCLUDED_StorageControllerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif
#include "StorageControllerWrap.h"

class ATL_NO_VTABLE StorageController :
    public StorageControllerWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(StorageController)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent,
                 const com::Utf8Str &aName,
                 StorageBus_T aBus,
                 ULONG aInstance,
                 bool fBootable);
    HRESULT init(Machine *aParent,
                 StorageController *aThat,
                 bool aReshare = false);
    HRESULT initCopy(Machine *aParent,
                     StorageController *aThat);
    void uninit();

    // public methods only for internal purposes
    const Utf8Str &i_getName() const;
    StorageControllerType_T i_getControllerType() const;
    StorageBus_T i_getStorageBus() const;
    ULONG i_getInstance() const;
    bool i_getBootable() const;
    HRESULT i_checkPortAndDeviceValid(LONG aControllerPort,
                                      LONG aDevice);
    void i_setBootable(BOOL fBootable);
    void i_rollback();
    void i_commit();

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

    void i_unshare();

    // public static methods
    static com::Utf8Str i_controllerNameFromBusType(StorageBus_T aBusType);

    /** @note this doesn't require a read lock since mParent is constant. */
    Machine* i_getMachine();
    ComObjPtr<StorageController> i_getPeer();

private:

    // Wrapped IStorageController properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT setName(const com::Utf8Str &aName);
    HRESULT getMaxDevicesPerPortCount(ULONG *aMaxDevicesPerPortCount);
    HRESULT getMinPortCount(ULONG *aMinPortCount);
    HRESULT getMaxPortCount(ULONG *aMaxPortCount);
    HRESULT getInstance(ULONG *aInstance);
    HRESULT setInstance(ULONG aInstance);
    HRESULT getPortCount(ULONG *aPortCount);
    HRESULT setPortCount(ULONG aPortCount);
    HRESULT getBus(StorageBus_T *aBus);
    HRESULT getControllerType(StorageControllerType_T *aControllerType);
    HRESULT setControllerType(StorageControllerType_T aControllerType);
    HRESULT getUseHostIOCache(BOOL *aUseHostIOCache);
    HRESULT setUseHostIOCache(BOOL aUseHostIOCache);
    HRESULT getBootable(BOOL *aBootable);

    void i_printList();

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_StorageControllerImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
