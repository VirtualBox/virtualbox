/* $Id: ClipboardTransferDirectoryImpl.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer directory handle.
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
#include "ClipboardTransferImpl.h"
#include "ClipboardTransferDirectoryImpl.h"
#include "ClipboardTransferFsObjInfoImpl.h"

#include <VBox/com/ErrorInfo.h>
#include <VBox/com/array.h>

#include <iprt/string.h>

#include <new>


DEFINE_EMPTY_CTOR_DTOR(ClipboardTransferDirectory)


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts a Shared Clipboard result code to a Main API HRESULT.
 *
 * @returns COM status code.
 * @param   vrc                 VBox status code to convert.
 */
static HRESULT clipboardTransferDirectoryRcToHrc(int vrc)
{
    if (RT_SUCCESS(vrc))
        return S_OK;
    if (vrc == VERR_NO_DATA || vrc == VERR_NO_MORE_FILES || vrc == VERR_NOT_FOUND)
        return VBOX_E_OBJECT_NOT_FOUND;
    if (vrc == VERR_NO_MEMORY)
        return E_OUTOFMEMORY;
    if (vrc == VERR_NOT_SUPPORTED || vrc == VERR_NOT_IMPLEMENTED)
        return E_NOTIMPL;
    if (vrc == VERR_INVALID_PARAMETER || vrc == VERR_INVALID_POINTER || vrc == VERR_INVALID_HANDLE)
        return E_INVALIDARG;
    return VBOX_E_SHCL_ERROR;
}


/**
 * Builds a child transfer path from a directory path and entry name.
 *
 * @returns Child transfer path.
 * @param   aDirectoryPath      Parent directory path.
 * @param   pszName             Child entry name.
 */
static com::Utf8Str clipboardTransferDirectoryMakeChildPath(const com::Utf8Str &aDirectoryPath, const char *pszName)
{
    if (aDirectoryPath.isEmpty())
        return com::Utf8Str(pszName ? pszName : "");
    return com::Utf8StrFmt("%s/%s", aDirectoryPath.c_str(), pszName ? pszName : "");
}


/**
 * Opens a Shared Clipboard transfer directory list.
 *
 * @returns VBox status code.
 * @param   pTransfer           Backing transfer.
 * @param   aPath               Transfer-relative directory path.
 * @param   phList              Where to return the list handle.
 */
static int clipboardTransferDirectoryOpenList(PSHCLTRANSFER pTransfer, const com::Utf8Str &aPath, PSHCLLISTHANDLE phList)
{
    SHCLLISTOPENPARMS OpenParms;
    int vrc = ShClTransferListOpenParmsInit(&OpenParms);
    if (RT_SUCCESS(vrc))
    {
        OpenParms.fList = VBOX_SHCL_LIST_F_NONE;
        if (!aPath.isEmpty())
            vrc = RTStrCopy(OpenParms.pszPath, OpenParms.cbPath, aPath.c_str());
        if (RT_SUCCESS(vrc))
            vrc = ShClTransferListOpen(pTransfer, &OpenParms, phList);
        ShClTransferListOpenParmsDestroy(&OpenParms);
    }
    return vrc;
}


/**
 * Creates a Main transfer object-info wrapper from a Shared Clipboard list entry.
 *
 * @returns COM status code.
 * @param   aDirectoryPath      Parent directory path.
 * @param   Entry               Shared Clipboard list entry.
 * @param   aInfo               Where to return the object-info wrapper.
 */
static HRESULT clipboardTransferDirectoryEntryToInfo(const com::Utf8Str &aDirectoryPath,
                                                     const SHCLLISTENTRY &Entry,
                                                     ComPtr<IClipboardTransferFsObjInfo> &aInfo)
{
    if (   Entry.fInfo != VBOX_SHCL_INFO_F_FSOBJINFO
        || !ShClTransferListEntryIsValid((PSHCLLISTENTRY)&Entry))
        return E_INVALIDARG;

    ComObjPtr<ClipboardTransferFsObjInfo> ptrInfo;
    HRESULT hrc = ptrInfo.createObject();
    if (FAILED(hrc))
        return hrc;

    try
    {
        com::Utf8Str const strName(Entry.pszName);
        com::Utf8Str const strPath = clipboardTransferDirectoryMakeChildPath(aDirectoryPath, Entry.pszName);
        hrc = ptrInfo->init(strPath, strName, (PCSHCLFSOBJINFO)Entry.pvInfo);
        if (FAILED(hrc))
            return hrc;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    return ptrInfo.queryInterfaceTo(aInfo.asOutParam());
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Completes construction of a clipboard transfer directory handle.
 *
 * @returns COM status code.
 */
HRESULT ClipboardTransferDirectory::FinalConstruct()
{
    return BaseFinalConstruct();
}


/**
 * Releases a clipboard transfer directory handle.
 */
void ClipboardTransferDirectory::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Initializes a clipboard transfer directory handle.
 *
 * @returns COM status code.
 * @param   aParent             Concrete parent transfer object.
 * @param   aTransfer           Backing Shared Clipboard transfer.
 * @param   aPath               Transfer-relative directory path.
 * @param   aHandle             Open Shared Clipboard list handle.
 */
HRESULT ClipboardTransferDirectory::init(const ComObjPtr<ClipboardTransfer> &aParent,
                                         PSHCLTRANSFER aTransfer,
                                         const com::Utf8Str &aPath,
                                         SHCLLISTHANDLE aHandle)
{
    AssertReturn(aParent.isNotNull(), E_POINTER);
    AssertPtrReturn(aTransfer, E_POINTER);
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    try
    {
        mData.mPath = aPath;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    mData.mParent = aParent;
    mData.mHandle = aHandle;
    mData.mStatus = DirectoryStatus_Open;
    mData.mTransfer = aTransfer;

    autoInitSpan.setSucceeded();
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Uninitializes a clipboard transfer directory handle.
 */
void ClipboardTransferDirectory::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ComObjPtr<ClipboardTransfer> ptrParent;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLLISTHANDLE hList = NIL_SHCLLISTHANDLE;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        ptrParent = mData.mParent;
        pTransfer = mData.mTransfer;
        hList = mData.mHandle;
        mData.mTransfer = NULL;
        mData.mHandle = NIL_SHCLLISTHANDLE;
        mData.mStatus = DirectoryStatus_Close;
        mData.mParent.setNull();
    }
    if (pTransfer && hList != NIL_SHCLLISTHANDLE)
    {
        int vrc = ShClTransferListClose(pTransfer, hList);
        AssertRC(vrc);
    }
#endif
}


HRESULT ClipboardTransferDirectory::getDirectoryName(com::Utf8Str &aDirectoryName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aDirectoryName = mData.mPath;
    return S_OK;
}


HRESULT ClipboardTransferDirectory::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    aEventSource.setNull();
    return S_OK;
}


HRESULT ClipboardTransferDirectory::getFilter(com::Utf8Str &aFilter)
{
    aFilter.setNull();
    return S_OK;
}


HRESULT ClipboardTransferDirectory::getId(ULONG *aId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aId = (ULONG)mData.mHandle;
    return S_OK;
}


HRESULT ClipboardTransferDirectory::getStatus(DirectoryStatus_T *aStatus)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aStatus = mData.mStatus;
    return S_OK;
}


HRESULT ClipboardTransferDirectory::close()
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ReturnComNotImplemented();
#else
    ComObjPtr<ClipboardTransfer> ptrParent;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLLISTHANDLE hList = NIL_SHCLLISTHANDLE;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        ptrParent = mData.mParent;
        pTransfer = mData.mTransfer;
        hList = mData.mHandle;
        mData.mTransfer = NULL;
        mData.mHandle = NIL_SHCLLISTHANDLE;
        mData.mStatus = DirectoryStatus_Close;
        mData.mParent.setNull();
    }
    if (!pTransfer)
        return S_OK;
    int const vrc = hList == NIL_SHCLLISTHANDLE ? VINF_SUCCESS : ShClTransferListClose(pTransfer, hList);
    HRESULT hrc = clipboardTransferDirectoryRcToHrc(vrc);
    if (FAILED(hrc))
        return setErrorBoth(hrc, vrc, tr("Closing clipboard transfer directory failed with %Rrc"), vrc);
    return S_OK;
#endif
}


HRESULT ClipboardTransferDirectory::list(ULONG aMaxEntries, std::vector<ComPtr<IFsObjInfo> > &aObjInfo)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aMaxEntries, aObjInfo);
    ReturnComNotImplemented();
#else
    std::vector<ComPtr<IClipboardTransferFsObjInfo> > vecEntries;
    HRESULT hrc = listEx(aMaxEntries, ClipboardTransferListFlag_NoRecursion, vecEntries);
    if (FAILED(hrc))
        return setError(hrc, tr("Listing clipboard transfer directory failed"));
    std::vector<ComPtr<IFsObjInfo> > ObjInfo;
    try
    {
        ObjInfo.reserve(vecEntries.size());
        for (std::vector<ComPtr<IClipboardTransferFsObjInfo> >::const_iterator it = vecEntries.begin();
             it != vecEntries.end(); ++it)
        {
            ComPtr<IFsObjInfo> ptrInfo(*it);
            ObjInfo.push_back(ptrInfo);
        }
    }
    catch (std::bad_alloc &)
    {
        return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer directory result failed"));
    }
    aObjInfo.swap(ObjInfo);
    return S_OK;
#endif
}


HRESULT ClipboardTransferDirectory::read(ComPtr<IFsObjInfo> &aObjInfo)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aObjInfo);
    ReturnComNotImplemented();
#else
    std::vector<ComPtr<IFsObjInfo> > vecInfo;
    HRESULT hrc = list(1, vecInfo);
    if (FAILED(hrc))
        return setError(hrc, tr("Reading clipboard transfer directory failed"));
    if (vecInfo.empty())
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("No clipboard transfer directory entry is available"));
    aObjInfo = vecInfo[0];
    return S_OK;
#endif
}


HRESULT ClipboardTransferDirectory::rewind()
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ReturnComNotImplemented();
#else
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    SHCLLISTHANDLE const hOld = mData.mHandle;
    com::Utf8Str const strPath = mData.mPath;
    if (!pTransfer || hOld == NIL_SHCLLISTHANDLE)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer directory is closed"));

    SHCLLISTHANDLE hNew = NIL_SHCLLISTHANDLE;
    int vrc = clipboardTransferDirectoryOpenList(pTransfer, strPath, &hNew);
    if (RT_FAILURE(vrc))
    {
        HRESULT hrc = clipboardTransferDirectoryRcToHrc(vrc);
        return setErrorBoth(hrc, vrc, tr("Rewinding clipboard transfer directory failed with %Rrc"), vrc);
    }

    mData.mHandle = hNew;
    mData.mStatus = DirectoryStatus_Open;
    if (hOld != NIL_SHCLLISTHANDLE)
        ShClTransferListClose(pTransfer, hOld);
    return S_OK;
#endif
}


HRESULT ClipboardTransferDirectory::getPath(com::Utf8Str &aPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aPath = mData.mPath;
    return S_OK;
}




HRESULT ClipboardTransferDirectory::listEx(ULONG aMaxEntries,
                                           ULONG aFlags,
                                           std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aEntries)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aMaxEntries, aFlags, aEntries);
    ReturnComNotImplemented();
#else
    if (aFlags & ~(ClipboardTransferListFlag_NoRecursion | ClipboardTransferListFlag_IncludeRoot | ClipboardTransferListFlag_NoFollowSymlinks))
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer directory-list flags %RU32"), aFlags);

    std::vector<ComPtr<IClipboardTransferFsObjInfo> > Entries;
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    SHCLLISTHANDLE const hList = mData.mHandle;
    com::Utf8Str const strPath = mData.mPath;
    ComObjPtr<ClipboardTransfer> const ptrParent = mData.mParent;
    if (!pTransfer || hList == NIL_SHCLLISTHANDLE)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer directory is closed"));

    if (!(aFlags & ClipboardTransferListFlag_NoRecursion))
    {
        if (ptrParent.isNull())
            return setError(E_FAIL, tr("Clipboard transfer directory has no parent transfer for recursive listing"));

        std::vector<ComPtr<IClipboardTransferFsObjInfo> > vecEntries;
        HRESULT hrc = ptrParent->i_list(pTransfer, strPath, aFlags, vecEntries);
        if (FAILED(hrc))
            return setError(hrc, tr("Recursively listing clipboard transfer directory failed"));
        for (std::vector<ComPtr<IClipboardTransferFsObjInfo> >::const_iterator it = vecEntries.begin();
             it != vecEntries.end(); ++it)
        {
            if (aMaxEntries && Entries.size() >= aMaxEntries)
                break;
            try
            {
                Entries.push_back(*it);
            }
            catch (std::bad_alloc &)
            {
                return setError(E_OUTOFMEMORY, tr("Allocating the recursive clipboard transfer directory result failed"));
            }
        }
        aEntries.swap(Entries);
        return S_OK;
    }

    if (   (aFlags & ClipboardTransferListFlag_IncludeRoot)
        && !strPath.isEmpty())
    {
        if (ptrParent.isNull())
            return setError(E_FAIL, tr("Clipboard transfer directory has no parent transfer for querying its root"));

        ComPtr<IClipboardTransferFsObjInfo> ptrRoot;
        HRESULT hrc = ptrParent->i_query(pTransfer, strPath, ptrRoot);
        if (FAILED(hrc))
            return setError(hrc, tr("Querying the clipboard transfer directory root failed"));
        try
        {
            Entries.push_back(ptrRoot);
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer directory root result failed"));
        }
    }

    for (;;)
    {
        if (aMaxEntries && Entries.size() >= aMaxEntries)
            break;

        SHCLLISTENTRY Entry;
        int vrc = ShClTransferListEntryInit(&Entry);
        if (RT_FAILURE(vrc))
        {
            HRESULT hrc = clipboardTransferDirectoryRcToHrc(vrc);
            return setErrorBoth(hrc, vrc, tr("Initializing clipboard transfer directory entry failed with %Rrc"), vrc);
        }
        vrc = ShClTransferListRead(pTransfer, hList, &Entry);
        if (RT_FAILURE(vrc))
        {
            ShClTransferListEntryDestroy(&Entry);
            if (vrc == VERR_NO_MORE_FILES || vrc == VERR_NO_DATA || vrc == VERR_NOT_FOUND)
                break;
            HRESULT hrc = clipboardTransferDirectoryRcToHrc(vrc);
            return setErrorBoth(hrc, vrc, tr("Reading clipboard transfer directory entry failed with %Rrc"), vrc);
        }

        ComPtr<IClipboardTransferFsObjInfo> ptrInfo;
        HRESULT hrc = clipboardTransferDirectoryEntryToInfo(strPath, Entry, ptrInfo);
        ShClTransferListEntryDestroy(&Entry);
        if (FAILED(hrc))
            return setError(hrc, tr("Creating clipboard transfer directory entry information failed"));
        try
        {
            Entries.push_back(ptrInfo);
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer directory result failed"));
        }
    }
    aEntries.swap(Entries);
    return S_OK;
#endif
}
