/* $Id: ConsoleSharedFolderImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_ConsoleSharedFolderImpl_h
#define MAIN_INCLUDED_ConsoleSharedFolderImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "SharedFolderWrap.h"
#include <VBox/shflsvc.h>

class Console;

class ATL_NO_VTABLE ConsoleSharedFolder :
    public SharedFolderWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS (ConsoleSharedFolder)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
//    HRESULT init(Machine *aMachine, const com::Utf8Str &aName, const com::Utf8Str &aHostPath,
//                 bool aWritable, bool aAutoMount, const com::Utf8Str &aAutoMountPoint, bool fFailOnError);
//    HRESULT initCopy(Machine *aMachine, SharedFolder *aThat);
    HRESULT init(Console *aConsole, const com::Utf8Str &aName, const com::Utf8Str &aHostPath,
                 bool aWritable, bool aAutoMount, const com::Utf8Str &aAutoMountPoint, bool fFailOnError);
//     HRESULT init(VirtualBox *aVirtualBox, const Utf8Str &aName, const Utf8Str &aHostPath,
//                  bool aWritable, const com::Utf8Str &aAutoMountPoint, bool aAutoMount, bool fFailOnError);
    void uninit();

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

    /**
     * Public internal method. Returns the shared folder's name. Needs caller! Locking not necessary.
     * @return
     */
    const Utf8Str &i_getName() const;

    /**
     * Public internal method. Returns the shared folder's host path. Needs caller! Locking not necessary.
     * @return
     */
    const Utf8Str &i_getHostPath() const;

    /**
     * Public internal method. Returns true if the shared folder is writable. Needs caller and locking!
     * @return
     */
    bool i_isWritable() const;

    /**
     * Public internal method. Returns true if the shared folder is auto-mounted. Needs caller and locking!
     * @return
     */
    bool i_isAutoMounted() const;

    /**
     * Public internal method for getting the auto mount point.
     */
    const Utf8Str &i_getAutoMountPoint() const;

    /**
     * Public internal method for getting the symlink policy.
     */
    SymlinkPolicy_T i_getSymlinkPolicy() const;

protected:

    HRESULT i_protectedInit(VirtualBoxBase *aParent,
                            const Utf8Str &aName,
                            const Utf8Str &aHostPath,
                            bool aWritable,
                            bool aAutoMount,
                            const com::Utf8Str &aAutoMountPoint,
                            bool fFailOnError);
private:

    // wrapped ISharedFolder properties.
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getHostPath(com::Utf8Str &aHostPath);
    HRESULT getAccessible(BOOL *aAccessible);
    HRESULT getWritable(BOOL *aWritable);
    HRESULT setWritable(BOOL aWritable);
    HRESULT getAutoMount(BOOL *aAutoMount);
    HRESULT setAutoMount(BOOL aAutoMount);
    HRESULT getAutoMountPoint(com::Utf8Str &aAutoMountPoint);
    HRESULT setAutoMountPoint(com::Utf8Str const &aAutoMountPoint);
    HRESULT getLastAccessError(com::Utf8Str &aLastAccessError);
    HRESULT getSymlinkPolicy(SymlinkPolicy_T *aSymlinkPolicy);
    HRESULT setSymlinkPolicy(SymlinkPolicy_T aSymlinkPolicy);

    VirtualBoxBase * const mParent;

    /* weak parents (only one of them is not null) */
    Console        * const mConsole;

    struct Data;            // opaque data struct, defined in ConsoleSharedFolderImpl.cpp
    Data *m;
};

#endif /* !MAIN_INCLUDED_ConsoleSharedFolderImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
