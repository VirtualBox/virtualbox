/* $Id: GuestFsInfoImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest file system information handling.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_GUESTFSINFO
#include "LoggingNew.h"

#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestFsInfoImpl.h"
#include "GuestCtrlImplPrivate.h"

#include "Global.h"
#include "AutoCaller.h"

#include <VBox/com/array.h>



// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestFsInfo)

HRESULT GuestFsInfo::FinalConstruct(void)
{
    LogFlowThisFuncEnter();
    return BaseFinalConstruct();
}

void GuestFsInfo::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

int GuestFsInfo::init(PCGSTCTLFSINFO pFsInfo)
{
    AssertPtrReturn(pFsInfo, VERR_INVALID_POINTER);

    LogFlowThisFuncEnter();

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    mData = *pFsInfo;

    /* Confirm a successful initialization when it's the case. */
    autoInitSpan.setSucceeded();

    return VINF_SUCCESS;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestFsInfo::uninit(void)
{
    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFuncEnter();
}

// implementation of wrapped private getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestFsInfo::getFreeSize(LONG64 *aFreeSize)
{
    *aFreeSize = mData.cbFree;
    return S_OK;
}

HRESULT GuestFsInfo::getTotalSize(LONG64 *aTotalSize)
{
    *aTotalSize = mData.cbTotalSize;
    return S_OK;
}

HRESULT GuestFsInfo::getBlockSize(ULONG *aBlockSize)
{
    *aBlockSize = mData.cbBlockSize;
    return S_OK;
}

HRESULT GuestFsInfo::getSectorSize(ULONG *aSectorSize)
{
    *aSectorSize = mData.cbSectorSize;
    return S_OK;
}

HRESULT GuestFsInfo::getSerialNumber(ULONG *aSerialNumber)
{
    *aSerialNumber = mData.uSerialNumber;
    return S_OK;
}

HRESULT GuestFsInfo::getIsRemote(BOOL *aIsRemote)
{
    *aIsRemote = mData.fFlags & GSTCTLFSINFO_F_IS_REMOTE;
    return S_OK;
}

HRESULT GuestFsInfo::getIsCaseSensitive(BOOL *aIsCaseSensitive)
{
    *aIsCaseSensitive = mData.fFlags & GSTCTLFSINFO_F_IS_CASE_SENSITIVE;
    return S_OK;
}

HRESULT GuestFsInfo::getIsReadOnly(BOOL *aIsReadOnly)
{
    *aIsReadOnly = mData.fFlags & GSTCTLFSINFO_F_IS_READ_ONLY;
    return S_OK;
}

HRESULT GuestFsInfo::getIsCompressed(BOOL *aIsCompressed)
{
    *aIsCompressed = mData.fFlags & GSTCTLFSINFO_F_IS_COMPRESSED;
    return S_OK;
}

HRESULT GuestFsInfo::getSupportsFileCompression(BOOL *aSupportsFileCompression)
{
    *aSupportsFileCompression = mData.fFeatures & GSTCTLFSINFO_FEATURE_F_FILE_COMPRESSION;
    return S_OK;
}

HRESULT GuestFsInfo::getMaxComponent(ULONG *aMaxComponent)
{
    *aMaxComponent = mData.cMaxComponent;
    return S_OK;
}

HRESULT GuestFsInfo::getType(com::Utf8Str &aType)
{
    aType = mData.szName;
    return S_OK;
}

HRESULT GuestFsInfo::getLabel(com::Utf8Str &aLabel)
{
    aLabel = mData.szLabel;
    return S_OK;
}

HRESULT GuestFsInfo::getMountPoint(com::Utf8Str &aMountPoint)
{
    if (mData.cbMountpoint)
        aMountPoint.assignEx(mData.szMountpoint, mData.cbMountpoint);
    return S_OK;
}

