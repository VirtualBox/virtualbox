/* $Id: ClipboardTransferFsObjInfoImpl.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer file system object information.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include "LoggingNew.h"

#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include "ClipboardTransferFsObjInfoImpl.h"

#include <iprt/fs.h>

#include <new>


DEFINE_EMPTY_CTOR_DTOR(ClipboardTransferFsObjInfo)


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts an IPRT file mode to a Main file-system object type.
 *
 * @returns Main file-system object type.
 * @param   fMode               IPRT file mode.
 */
static FsObjType_T clipboardTransferFsObjInfoModeToType(RTFMODE fMode)
{
    if (RTFS_IS_DIRECTORY(fMode)) return FsObjType_Directory;
    if (RTFS_IS_FILE(fMode))      return FsObjType_File;
    if (RTFS_IS_SYMLINK(fMode))   return FsObjType_Symlink;
    if (RTFS_IS_DEV_CHAR(fMode))  return FsObjType_DevChar;
    if (RTFS_IS_DEV_BLOCK(fMode)) return FsObjType_DevBlock;
    if (RTFS_IS_FIFO(fMode))      return FsObjType_Fifo;
    if (RTFS_IS_SOCKET(fMode))    return FsObjType_Socket;
    if (RTFS_IS_WHITEOUT(fMode))  return FsObjType_WhiteOut;
    return FsObjType_Unknown;
}


/**
 * Formats the portable mode bits as a compact attribute string.
 *
 * @returns Attribute string.
 * @param   fMode               IPRT file mode.
 */
static com::Utf8Str clipboardTransferFsObjInfoModeToAttrs(RTFMODE fMode)
{
    char szAttrs[11];
    szAttrs[0] = RTFS_IS_DIRECTORY(fMode) ? 'd'
               : RTFS_IS_SYMLINK(fMode)  ? 'l'
               : RTFS_IS_DEV_CHAR(fMode) ? 'c'
               : RTFS_IS_DEV_BLOCK(fMode)? 'b'
               : RTFS_IS_FIFO(fMode)     ? 'f'
               : RTFS_IS_SOCKET(fMode)   ? 's'
               : RTFS_IS_WHITEOUT(fMode) ? 'w' : '-';
    szAttrs[1] = fMode & RTFS_UNIX_IRUSR ? 'r' : '-';
    szAttrs[2] = fMode & RTFS_UNIX_IWUSR ? 'w' : '-';
    szAttrs[3] = fMode & RTFS_UNIX_IXUSR ? 'x' : '-';
    szAttrs[4] = fMode & RTFS_UNIX_IRGRP ? 'r' : '-';
    szAttrs[5] = fMode & RTFS_UNIX_IWGRP ? 'w' : '-';
    szAttrs[6] = fMode & RTFS_UNIX_IXGRP ? 'x' : '-';
    szAttrs[7] = fMode & RTFS_UNIX_IROTH ? 'r' : '-';
    szAttrs[8] = fMode & RTFS_UNIX_IWOTH ? 'w' : '-';
    szAttrs[9] = fMode & RTFS_UNIX_IXOTH ? 'x' : '-';
    szAttrs[10] = '\0';
    return com::Utf8Str(szAttrs);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Completes construction of a clipboard transfer object-info object.
 *
 * @returns COM status code.
 */
HRESULT ClipboardTransferFsObjInfo::FinalConstruct()
{
    return BaseFinalConstruct();
}


/**
 * Releases a clipboard transfer object-info object.
 */
void ClipboardTransferFsObjInfo::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Initializes a clipboard transfer object-info object.
 *
 * @returns COM status code.
 * @param   aPath               Transfer-relative node path.
 * @param   aName               Node base name.
 * @param   aInfo               Shared Clipboard object information.
 */
HRESULT ClipboardTransferFsObjInfo::init(const com::Utf8Str &aPath,
                                         const com::Utf8Str &aName,
                                         PCSHCLFSOBJINFO aInfo)
{
    AssertPtrReturn(aInfo, E_POINTER);
    if (   aInfo->cbObject < 0
        || aInfo->cbAllocated < 0
        || aInfo->Attr.enmAdditional < SHCLFSOBJATTRADD_NOTHING
        || aInfo->Attr.enmAdditional > SHCLFSOBJATTRADD_LAST)
        return E_INVALIDARG;

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    try
    {
        mData.mPath           = aPath;
        mData.mName           = aName;
        mData.mFileAttributes = clipboardTransferFsObjInfoModeToAttrs(aInfo->Attr.fMode);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    mData.mType             = clipboardTransferFsObjInfoModeToType(aInfo->Attr.fMode);
    mData.mObjectSize       = aInfo->cbObject;
    mData.mAllocatedSize    = aInfo->cbAllocated;
    mData.mAccessTime       = aInfo->AccessTime.i64NanosecondsRelativeToUnixEpoch;
    mData.mBirthTime        = aInfo->BirthTime.i64NanosecondsRelativeToUnixEpoch;
    mData.mChangeTime       = aInfo->ChangeTime.i64NanosecondsRelativeToUnixEpoch;
    mData.mModificationTime = aInfo->ModificationTime.i64NanosecondsRelativeToUnixEpoch;

    if (aInfo->Attr.enmAdditional == SHCLFSOBJATTRADD_UNIX)
    {
        mData.mUID          = (LONG)aInfo->Attr.u.Unix.uid;
        mData.mGID          = (LONG)aInfo->Attr.u.Unix.gid;
        mData.mHardLinks    = aInfo->Attr.u.Unix.cHardlinks;
        mData.mNodeIdDevice = (ULONG)aInfo->Attr.u.Unix.INodeIdDevice;
        mData.mNodeId       = (LONG64)aInfo->Attr.u.Unix.INodeId;
        mData.mUserFlags    = aInfo->Attr.u.Unix.fFlags;
        mData.mGenerationId = aInfo->Attr.u.Unix.GenerationId;
        mData.mDeviceNumber = (ULONG)aInfo->Attr.u.Unix.Device;
    }

    autoInitSpan.setSucceeded();
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Uninitializes a clipboard transfer object-info object.
 */
void ClipboardTransferFsObjInfo::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}


HRESULT ClipboardTransferFsObjInfo::getName(com::Utf8Str &aName) { aName = mData.mName; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getType(FsObjType_T *aType) { *aType = mData.mType; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getFileAttributes(com::Utf8Str &aFileAttributes) { aFileAttributes = mData.mFileAttributes; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getObjectSize(LONG64 *aObjectSize) { *aObjectSize = mData.mObjectSize; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getAllocatedSize(LONG64 *aAllocatedSize) { *aAllocatedSize = mData.mAllocatedSize; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getAccessTime(LONG64 *aAccessTime) { *aAccessTime = mData.mAccessTime; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getBirthTime(LONG64 *aBirthTime) { *aBirthTime = mData.mBirthTime; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getChangeTime(LONG64 *aChangeTime) { *aChangeTime = mData.mChangeTime; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getModificationTime(LONG64 *aModificationTime) { *aModificationTime = mData.mModificationTime; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getUID(LONG *aUID) { *aUID = mData.mUID; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getUserName(com::Utf8Str &aUserName) { aUserName = mData.mUserName; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getGID(LONG *aGID) { *aGID = mData.mGID; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getGroupName(com::Utf8Str &aGroupName) { aGroupName = mData.mGroupName; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getNodeId(LONG64 *aNodeId) { *aNodeId = mData.mNodeId; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getNodeIdDevice(ULONG *aNodeIdDevice) { *aNodeIdDevice = mData.mNodeIdDevice; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getHardLinks(ULONG *aHardLinks) { *aHardLinks = mData.mHardLinks; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getDeviceNumber(ULONG *aDeviceNumber) { *aDeviceNumber = mData.mDeviceNumber; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getGenerationId(ULONG *aGenerationId) { *aGenerationId = mData.mGenerationId; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getUserFlags(ULONG *aUserFlags) { *aUserFlags = mData.mUserFlags; return S_OK; }
HRESULT ClipboardTransferFsObjInfo::getPath(com::Utf8Str &aPath) { aPath = mData.mPath; return S_OK; }
