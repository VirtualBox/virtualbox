/* $Id: GuestFsObjInfoImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest file system object information implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_GuestFsObjInfoImpl_h
#define MAIN_INCLUDED_GuestFsObjInfoImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestFsObjInfoWrap.h"
#include "GuestCtrlImplPrivate.h"

class ATL_NO_VTABLE GuestFsObjInfo
    : public GuestFsObjInfoWrap
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(GuestFsObjInfo)

    int     init(const GuestFsObjData &objData);
    void    uninit(void);

    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

    /** @name Internal access helpers.
     * @{ */
    const GuestFsObjData &i_getData() const { return mData; }
    /** @}  */

private:

    /** Wrapped @name IGuestFsObjInfo properties.
     * @{ */
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getType(FsObjType_T *aType);
    HRESULT getFileAttributes(com::Utf8Str &aFileAttributes);
    HRESULT getObjectSize(LONG64 *aObjectSize);
    HRESULT getAllocatedSize(LONG64 *aAllocatedSize);
    HRESULT getAccessTime(LONG64 *aAccessTime);
    HRESULT getBirthTime(LONG64 *aBirthTime);
    HRESULT getChangeTime(LONG64 *aChangeTime);
    HRESULT getModificationTime(LONG64 *aModificationTime);
    HRESULT getUID(LONG *aUID);
    HRESULT getUserName(com::Utf8Str &aUserName);
    HRESULT getGID(LONG *aGID);
    HRESULT getGroupName(com::Utf8Str &aGroupName);
    HRESULT getNodeId(LONG64 *aNodeId);
    HRESULT getNodeIdDevice(ULONG *aNodeIdDevice);
    HRESULT getHardLinks(ULONG *aHardLinks);
    HRESULT getDeviceNumber(ULONG *aDeviceNumber);
    HRESULT getGenerationId(ULONG *aGenerationId);
    HRESULT getUserFlags(ULONG *aUserFlags);
    /** @}  */

    GuestFsObjData mData;
};

#endif /* !MAIN_INCLUDED_GuestFsObjInfoImpl_h */

