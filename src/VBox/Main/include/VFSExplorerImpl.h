/* $Id: VFSExplorerImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_VFSExplorerImpl_h
#define MAIN_INCLUDED_VFSExplorerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VFSExplorerWrap.h"

class ATL_NO_VTABLE VFSExplorer :
    public VFSExplorerWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(VFSExplorer)

    // public initializer/uninitializer for internal purposes only
    HRESULT FinalConstruct() { return BaseFinalConstruct(); }
    void FinalRelease() { uninit(); BaseFinalRelease(); }

    HRESULT init(VFSType_T aType, Utf8Str aFilePath, Utf8Str aHostname, Utf8Str aUsername, Utf8Str aPassword, VirtualBox *aVirtualBox);
    void uninit();

    /* public methods only for internal purposes */
    static HRESULT setErrorStatic(HRESULT aResultCode, const char *aText, ...)
    {
        va_list va;
        va_start(va, aText);
        HRESULT hrc = setErrorInternalV(aResultCode, getStaticClassIID(), getStaticComponentName(), aText, va, false, true);
        va_end(va);
        return hrc;
    }

private:

    // wrapped IVFSExplorer properties
    HRESULT getPath(com::Utf8Str &aPath);
    HRESULT getType(VFSType_T *aType);

   // wrapped IVFSExplorer methods
    HRESULT update(ComPtr<IProgress> &aProgress);
    HRESULT cd(const com::Utf8Str &aDir, ComPtr<IProgress> &aProgress);
    HRESULT cdUp(ComPtr<IProgress> &aProgress);
    HRESULT entryList(std::vector<com::Utf8Str> &aNames,
                      std::vector<ULONG> &aTypes,
                      std::vector<LONG64> &aSizes,
                      std::vector<ULONG> &aModes);
    HRESULT exists(const std::vector<com::Utf8Str> &aNames,
                   std::vector<com::Utf8Str> &aExists);
    HRESULT remove(const std::vector<com::Utf8Str> &aNames,
                   ComPtr<IProgress> &aProgress);

    /* Private member vars */
    VirtualBox * const mVirtualBox;

    ////////////////////////////////////////////////////////////////////////////////
    ////
    //// VFSExplorer definitions
    ////
    //////////////////////////////////////////////////////////////////////////////////
    //
    class TaskVFSExplorer;  /* Worker thread helper */
    struct Data;
    Data *m;

    /* Private member methods */
    FsObjType_T i_iprtToVfsObjType(RTFMODE aType) const;

    HRESULT i_updateFS(TaskVFSExplorer *aTask);
    HRESULT i_deleteFS(TaskVFSExplorer *aTask);

};

#endif /* !MAIN_INCLUDED_VFSExplorerImpl_h */

