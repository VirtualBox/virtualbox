/* $Id: GuestFsObjInfoImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest file system object information handling.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_GUESTFSOBJINFO
#include "LoggingNew.h"

#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestFsObjInfoImpl.h"
#include "GuestCtrlImplPrivate.h"

#include "Global.h"
#include "AutoCaller.h"

#include <VBox/com/array.h>



// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestFsObjInfo)

HRESULT GuestFsObjInfo::FinalConstruct(void)
{
    LogFlowThisFuncEnter();
    return BaseFinalConstruct();
}

void GuestFsObjInfo::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

int GuestFsObjInfo::init(const GuestFsObjData &objData)
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    mData = objData;

    /* Confirm a successful initialization when it's the case. */
    autoInitSpan.setSucceeded();

    return VINF_SUCCESS;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestFsObjInfo::uninit(void)
{
    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFuncEnter();
}

// implementation of wrapped private getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestFsObjInfo::getAccessTime(LONG64 *aAccessTime)
{
    *aAccessTime = mData.mAccessTime;

    return S_OK;
}

HRESULT GuestFsObjInfo::getAllocatedSize(LONG64 *aAllocatedSize)
{
    *aAllocatedSize = mData.mAllocatedSize;

    return S_OK;
}

HRESULT GuestFsObjInfo::getBirthTime(LONG64 *aBirthTime)
{
    *aBirthTime = mData.mBirthTime;

    return S_OK;
}

HRESULT GuestFsObjInfo::getChangeTime(LONG64 *aChangeTime)
{
    *aChangeTime = mData.mChangeTime;

    return S_OK;
}



HRESULT GuestFsObjInfo::getDeviceNumber(ULONG *aDeviceNumber)
{
    *aDeviceNumber = mData.mDeviceNumber;

    return S_OK;
}

HRESULT GuestFsObjInfo::getFileAttributes(com::Utf8Str &aFileAttributes)
{
    aFileAttributes = mData.mFileAttrs;

    return S_OK;
}

HRESULT GuestFsObjInfo::getGenerationId(ULONG *aGenerationId)
{
    *aGenerationId = mData.mGenerationID;

    return S_OK;
}

HRESULT GuestFsObjInfo::getGID(LONG *aGID)
{
    *aGID = mData.mGID;

    return S_OK;
}

HRESULT GuestFsObjInfo::getGroupName(com::Utf8Str &aGroupName)
{
    aGroupName = mData.mGroupName;

    return S_OK;
}

HRESULT GuestFsObjInfo::getHardLinks(ULONG *aHardLinks)
{
    *aHardLinks = mData.mNumHardLinks;

    return S_OK;
}

HRESULT GuestFsObjInfo::getModificationTime(LONG64 *aModificationTime)
{
    *aModificationTime = mData.mModificationTime;

    return S_OK;
}

HRESULT GuestFsObjInfo::getName(com::Utf8Str &aName)
{
    aName = mData.mName;

    return S_OK;
}

HRESULT GuestFsObjInfo::getNodeId(LONG64 *aNodeId)
{
    *aNodeId = mData.mNodeID;

    return S_OK;
}

HRESULT GuestFsObjInfo::getNodeIdDevice(ULONG *aNodeIdDevice)
{
    *aNodeIdDevice = mData.mNodeIDDevice;

    return S_OK;
}

HRESULT GuestFsObjInfo::getObjectSize(LONG64 *aObjectSize)
{
    *aObjectSize = mData.mObjectSize;

    return S_OK;
}

HRESULT GuestFsObjInfo::getType(FsObjType_T *aType)
{
    *aType = mData.mType;

    return S_OK;
}

HRESULT GuestFsObjInfo::getUID(LONG *aUID)
{
    *aUID = mData.mUID;

    return S_OK;
}

HRESULT GuestFsObjInfo::getUserFlags(ULONG *aUserFlags)
{
    *aUserFlags = mData.mUserFlags;

    return S_OK;
}

HRESULT GuestFsObjInfo::getUserName(com::Utf8Str &aUserName)
{
    aUserName = mData.mUserName;

    return S_OK;
}

