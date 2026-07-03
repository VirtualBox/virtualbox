/* $Id: VRDEServerImpl.h 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_VRDEServerImpl_h
#define MAIN_INCLUDED_VRDEServerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VRDEServerWrap.h"

namespace settings
{
    struct VRDESettings;
}

class ATL_NO_VTABLE VRDEServer :
    public VRDEServerWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(VRDEServer)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent);
    HRESULT init(Machine *aParent, VRDEServer *aThat);
    HRESULT initCopy(Machine *aParent, VRDEServer *aThat);
    void uninit();

    // public methods only for internal purposes
    HRESULT i_loadSettings(const settings::VRDESettings &data);
    HRESULT i_saveSettings(settings::VRDESettings &data);
    int i_generateServerCertificate();
    HRESULT i_certificateRepair(BOOL &certificateGenerated);
    void i_rollback();
    void i_commit();
    void i_copyFrom(VRDEServer *aThat);

private:

     // wrapped IVRDEServer properties
     HRESULT getEnabled(BOOL *aEnabled);
     HRESULT setEnabled(BOOL aEnabled);
     HRESULT getAuthType(AuthType_T *aAuthType);
     HRESULT setAuthType(AuthType_T aAuthType);
     HRESULT getAuthTimeout(ULONG *aAuthTimeout);
     HRESULT setAuthTimeout(ULONG aAuthTimeout);
     HRESULT getAllowMultiConnection(BOOL *aAllowMultiConnection);
     HRESULT setAllowMultiConnection(BOOL aAllowMultiConnection);
     HRESULT getReuseSingleConnection(BOOL *aReuseSingleConnection);
     HRESULT setReuseSingleConnection(BOOL aReuseSingleConnection);
     HRESULT getVRDEExtPack(com::Utf8Str &aVRDEExtPack);
     HRESULT setVRDEExtPack(const com::Utf8Str &aVRDEExtPack);
     HRESULT getAuthLibrary(com::Utf8Str &aAuthLibrary);
     HRESULT setAuthLibrary(const com::Utf8Str &aAuthLibrary);
     HRESULT getVRDEProperties(std::vector<com::Utf8Str> &aVRDEProperties);

    // wrapped IVRDEServer methods
    HRESULT setVRDEProperty(const com::Utf8Str &aKey,
                            const com::Utf8Str &aValue);
    HRESULT getVRDEProperty(const com::Utf8Str &aKey,
                            com::Utf8Str &aValue);

    Machine * const     mParent;
    const ComObjPtr<VRDEServer> mPeer;

    Backupable<settings::VRDESettings> mData;
};

#endif /* !MAIN_INCLUDED_VRDEServerImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
