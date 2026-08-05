/* $Id: ClipboardTransferFileImpl.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer file handle.
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
#include "ClipboardTransferFileImpl.h"
#include "ClipboardTransferFsObjInfoImpl.h"

#include <VBox/com/ErrorInfo.h>

#include <iprt/mem.h>
#include <iprt/path.h>

#include <new>


DEFINE_EMPTY_CTOR_DTOR(ClipboardTransferFile)


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts a Shared Clipboard result code to a Main API HRESULT.
 *
 * @returns COM status code.
 * @param   vrc                 VBox status code to convert.
 */
static HRESULT clipboardTransferFileRcToHrc(int vrc)
{
    if (RT_SUCCESS(vrc))
        return S_OK;
    if (vrc == VERR_NO_DATA || vrc == VERR_EOF || vrc == VERR_NOT_FOUND)
        return VBOX_E_OBJECT_NOT_FOUND;
    if (vrc == VERR_NO_MEMORY)
        return E_OUTOFMEMORY;
    if (vrc == VERR_NOT_SUPPORTED || vrc == VERR_NOT_IMPLEMENTED)
        return E_NOTIMPL;
    if (vrc == VERR_ACCESS_DENIED)
        return VBOX_E_SHCL_ACCESS_DENIED;
    if (vrc == VERR_INVALID_PARAMETER || vrc == VERR_INVALID_POINTER || vrc == VERR_INVALID_HANDLE || vrc == VERR_BUFFER_OVERFLOW)
        return E_INVALIDARG;
    return VBOX_E_SHCL_ERROR;
}


/**
 * Opens a Shared Clipboard object for a transfer path.
 *
 * @returns VBox status code.
 * @param   pTransfer           Backing transfer.
 * @param   strPath             Transfer-relative path.
 * @param   phObj               Where to return the opened object handle.
 * @param   pInfo               Where to return object information. Optional.
 */
static int clipboardTransferFileOpenObject(PSHCLTRANSFER pTransfer,
                                           const com::Utf8Str &strPath,
                                           PSHCLOBJHANDLE phObj,
                                           PSHCLFSOBJINFO pInfo)
{
    SHCLOBJOPENCREATEPARMS OpenParms;
    int vrc = ShClTransferObjOpenParmsInit(&OpenParms);
    if (RT_SUCCESS(vrc))
    {
        OpenParms.fCreate = SHCL_OBJ_CF_ACCESS_READ | SHCL_OBJ_CF_ACCESS_DENYNONE | SHCL_OBJ_CF_ACCESS_ATTR_READ;
        vrc = RTStrCopy(OpenParms.pszPath, OpenParms.cbPath, strPath.c_str());
        if (RT_SUCCESS(vrc))
        {
            vrc = ShClTransferObjOpen(pTransfer, &OpenParms, phObj);
            if (   RT_SUCCESS(vrc)
                && pInfo)
                *pInfo = OpenParms.ObjInfo;
        }
        ShClTransferObjOpenParmsDestroy(&OpenParms);
    }
    return vrc;
}


/**
 * Reads and discards bytes from an opened Shared Clipboard object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Backing transfer.
 * @param   hObj                Object handle.
 * @param   cbSkip              Number of bytes to skip.
 */
static int clipboardTransferFileSkip(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj, uint64_t cbSkip)
{
    uint32_t const cbMaxChunk = pTransfer->cbMaxChunkSize;
    if (!cbMaxChunk)
        return VERR_INVALID_STATE;

    uint8_t abBuf[_64K];
    while (cbSkip > 0)
    {
        uint32_t const cbToRead = (uint32_t)RT_MIN(cbSkip, (uint64_t)RT_MIN(sizeof(abBuf), (size_t)cbMaxChunk));
        uint32_t cbRead = 0;
        int vrc = ShClTransferObjRead(pTransfer, hObj, abBuf, cbToRead, 0, &cbRead);
        if (RT_FAILURE(vrc))
            return vrc;
        if (   !cbRead
            || cbRead > cbToRead
            || cbRead > cbSkip)
            return VERR_EOF;
        cbSkip -= cbRead;
    }
    return VINF_SUCCESS;
}


#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Completes construction of a clipboard transfer file handle.
 *
 * @returns COM status code.
 */
HRESULT ClipboardTransferFile::FinalConstruct()
{
    return BaseFinalConstruct();
}


/**
 * Releases a clipboard transfer file handle.
 */
void ClipboardTransferFile::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Initializes a clipboard transfer file handle.
 *
 * @returns COM status code.
 * @param   aParent             Parent transfer object.
 * @param   aTransfer           Backing Shared Clipboard transfer.
 * @param   aHandle             Open Shared Clipboard object handle.
 * @param   aPath               Transfer-relative file path.
 * @param   aInfo               File object information returned by the open operation.
 * @param   aAccessMode         File access mode.
 * @param   aOpenAction         File open action.
 * @param   aSharingMode        File sharing mode.
 * @param   aCreationMode       Creation mode.
 */
HRESULT ClipboardTransferFile::init(const ComPtr<IClipboardTransfer> &aParent,
                                    PSHCLTRANSFER aTransfer,
                                    SHCLOBJHANDLE aHandle,
                                    const com::Utf8Str &aPath,
                                    const SHCLFSOBJINFO &aInfo,
                                    FileAccessMode_T aAccessMode,
                                    FileOpenAction_T aOpenAction,
                                    FileSharingMode_T aSharingMode,
                                    ULONG aCreationMode)
{
    AssertPtrReturn(aTransfer, E_POINTER);
    if (aInfo.cbObject < 0)
        return E_INVALIDARG;
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
    mData.mInfo = aInfo;
    mData.mOffset = 0;
    mData.mStatus = FileStatus_Open;
    mData.mAccessMode = aAccessMode;
    mData.mOpenAction = aOpenAction;
    mData.mSharingMode = aSharingMode;
    mData.mCreationMode = aCreationMode;
    mData.mTransfer = aTransfer;

    autoInitSpan.setSucceeded();
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Uninitializes a clipboard transfer file handle.
 */
void ClipboardTransferFile::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ComPtr<IClipboardTransfer> ptrParent;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLOBJHANDLE hObj = NIL_SHCLOBJHANDLE;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        ptrParent = mData.mParent;
        pTransfer = mData.mTransfer;
        hObj = mData.mHandle;
        mData.mTransfer = NULL;
        mData.mHandle = NIL_SHCLOBJHANDLE;
        mData.mStatus = FileStatus_Closed;
        mData.mParent.setNull();
    }
    if (pTransfer && hObj != NIL_SHCLOBJHANDLE)
    {
        int vrc = ShClTransferObjClose(pTransfer, hObj);
        AssertRC(vrc);
    }
#endif
}


HRESULT ClipboardTransferFile::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    aEventSource.setNull();
    return S_OK;
}


HRESULT ClipboardTransferFile::getId(ULONG *aId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aId = (ULONG)mData.mHandle;
    return S_OK;
}


HRESULT ClipboardTransferFile::getInitialSize(LONG64 *aInitialSize)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aInitialSize = mData.mInfo.cbObject;
    return S_OK;
}


HRESULT ClipboardTransferFile::getOffset(LONG64 *aOffset)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aOffset = mData.mOffset;
    return S_OK;
}


HRESULT ClipboardTransferFile::getStatus(FileStatus_T *aStatus)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aStatus = mData.mStatus;
    return S_OK;
}


HRESULT ClipboardTransferFile::getFilename(com::Utf8Str &aFilename)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aFilename = mData.mPath;
    return S_OK;
}


HRESULT ClipboardTransferFile::getCreationMode(ULONG *aCreationMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aCreationMode = mData.mCreationMode;
    return S_OK;
}


HRESULT ClipboardTransferFile::getOpenAction(FileOpenAction_T *aOpenAction)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aOpenAction = mData.mOpenAction;
    return S_OK;
}


HRESULT ClipboardTransferFile::getAccessMode(FileAccessMode_T *aAccessMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aAccessMode = mData.mAccessMode;
    return S_OK;
}


HRESULT ClipboardTransferFile::close()
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ReturnComNotImplemented();
#else
    ComPtr<IClipboardTransfer> ptrParent;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLOBJHANDLE hObj = NIL_SHCLOBJHANDLE;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        ptrParent = mData.mParent;
        pTransfer = mData.mTransfer;
        hObj = mData.mHandle;
        mData.mTransfer = NULL;
        mData.mHandle = NIL_SHCLOBJHANDLE;
        mData.mStatus = FileStatus_Closed;
        mData.mParent.setNull();
    }
    if (!pTransfer)
        return S_OK;
    int const vrc = hObj == NIL_SHCLOBJHANDLE ? VINF_SUCCESS : ShClTransferObjClose(pTransfer, hObj);
    HRESULT hrc = clipboardTransferFileRcToHrc(vrc);
    if (FAILED(hrc))
        return setErrorBoth(hrc, vrc, tr("Closing clipboard transfer file failed with %Rrc"), vrc);
    return S_OK;
#endif
}


HRESULT ClipboardTransferFile::queryInfo(ComPtr<IFsObjInfo> &aObjInfo)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aObjInfo);
    ReturnComNotImplemented();
#else
    SHCLFSOBJINFO Info;
    com::Utf8Str strPath;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        Info = mData.mInfo;
        strPath = mData.mPath;
    }

    ComObjPtr<ClipboardTransferFsObjInfo> ptrInfo;
    HRESULT hrc = ptrInfo.createObject();
    if (FAILED(hrc))
        return setError(hrc, tr("Creating clipboard transfer file information object failed"));
    hrc = ptrInfo->init(strPath, RTPathFilename(strPath.c_str()), &Info);
    if (FAILED(hrc))
        return setError(hrc, tr("Initializing clipboard transfer file information object failed"));
    hrc = ptrInfo.queryInterfaceTo(aObjInfo.asOutParam());
    if (FAILED(hrc))
        return setError(hrc, tr("Querying clipboard transfer file information interface failed"));
    return S_OK;
#endif
}


HRESULT ClipboardTransferFile::querySize(LONG64 *aSize)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aSize = mData.mInfo.cbObject;
    return S_OK;
}


HRESULT ClipboardTransferFile::read(ULONG aToRead, ULONG aTimeoutMS, std::vector<BYTE> &aData)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aToRead, aTimeoutMS, aData);
    ReturnComNotImplemented();
#else
    RT_NOREF(aTimeoutMS);
    if (!aToRead)
        return setError(E_INVALIDARG, tr("Clipboard transfer file read size must be non-zero"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    SHCLOBJHANDLE const hObj = mData.mHandle;
    if (!pTransfer || hObj == NIL_SHCLOBJHANDLE)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer file is closed"));
    if (aToRead > pTransfer->cbMaxChunkSize)
        return setError(E_INVALIDARG, tr("Clipboard transfer file read size exceeds the backend chunk limit"));

    try
    {
        aData.resize(aToRead);
    }
    catch (std::bad_alloc &)
    {
        return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer file read buffer failed"));
    }
    uint32_t cbRead = 0;
    int vrc = ShClTransferObjRead(pTransfer, hObj, aData.empty() ? NULL : &aData[0], aToRead, 0, &cbRead);
    if (RT_FAILURE(vrc))
    {
        aData.clear();
        HRESULT hrc = clipboardTransferFileRcToHrc(vrc);
        return setErrorBoth(hrc, vrc, tr("Reading clipboard transfer file failed with %Rrc"), vrc);
    }
    if (   cbRead > aToRead
        || mData.mOffset > INT64_MAX - cbRead)
    {
        aData.clear();
        return setError(E_INVALIDARG, tr("Clipboard transfer provider returned an invalid read size"));
    }
    aData.resize(cbRead);
    mData.mOffset += cbRead;
    return S_OK;
#endif
}


HRESULT ClipboardTransferFile::readAt(LONG64 aOffset, ULONG aToRead, ULONG aTimeoutMS, std::vector<BYTE> &aData)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aOffset, aToRead, aTimeoutMS, aData);
    ReturnComNotImplemented();
#else
    if (aOffset < 0)
        return setError(E_INVALIDARG, tr("Clipboard transfer file read offset must not be negative"));
    HRESULT hrc = i_reopenAt((uint64_t)aOffset);
    if (FAILED(hrc))
        return setError(hrc, tr("Seeking clipboard transfer file for readAt failed"));
    return read(aToRead, aTimeoutMS, aData);
#endif
}


HRESULT ClipboardTransferFile::seek(LONG64 aOffset, FileSeekOrigin_T aWhence, LONG64 *aNewOffset)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aOffset, aWhence, aNewOffset);
    ReturnComNotImplemented();
#else
    if (!aNewOffset)
        return setError(E_POINTER, tr("The clipboard transfer file seek output argument must not be NULL"));

    LONG64 offBase;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (aWhence == FileSeekOrigin_Begin)
            offBase = 0;
        else if (aWhence == FileSeekOrigin_Current)
            offBase = mData.mOffset;
        else if (aWhence == FileSeekOrigin_End)
            offBase = mData.mInfo.cbObject;
        else
            return setError(E_INVALIDARG, tr("Invalid clipboard transfer file seek origin %RU32"), (uint32_t)aWhence);
    }

    if (   (aOffset > 0 && offBase > INT64_MAX - aOffset)
        || (aOffset < 0 && offBase < INT64_MIN - aOffset))
        return setError(E_INVALIDARG, tr("Clipboard transfer file seek offset is out of range"));

    LONG64 const offNew = offBase + aOffset;
    if (offNew < 0)
        return setError(E_INVALIDARG, tr("Clipboard transfer file seek would move before the start of the file"));
    HRESULT hrc = i_reopenAt((uint64_t)offNew);
    if (FAILED(hrc))
        return setError(hrc, tr("Seeking clipboard transfer file failed"));
    *aNewOffset = offNew;
    return S_OK;
#endif
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Reopens the backing transfer object and positions it at a byte offset.
 *
 * @returns COM status code.
 * @param   offNew              New byte offset.
 */
HRESULT ClipboardTransferFile::i_reopenAt(uint64_t offNew)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    SHCLOBJHANDLE const hOld = mData.mHandle;
    com::Utf8Str const strPath = mData.mPath;
    if (!pTransfer)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer file is closed"));

    SHCLOBJHANDLE hNew = NIL_SHCLOBJHANDLE;
    SHCLFSOBJINFO Info;
    RT_ZERO(Info);
    int vrc = clipboardTransferFileOpenObject(pTransfer, strPath, &hNew, &Info);
    if (   RT_SUCCESS(vrc)
        && Info.cbObject < 0)
        vrc = VERR_OUT_OF_RANGE;
    if (RT_SUCCESS(vrc))
        vrc = clipboardTransferFileSkip(pTransfer, hNew, offNew);
    if (RT_FAILURE(vrc))
    {
        if (hNew != NIL_SHCLOBJHANDLE)
            ShClTransferObjClose(pTransfer, hNew);
        HRESULT hrc = clipboardTransferFileRcToHrc(vrc);
        return setErrorBoth(hrc, vrc, tr("Seeking clipboard transfer file failed with %Rrc"), vrc);
    }

    mData.mHandle = hNew;
    mData.mInfo = Info;
    mData.mOffset = (LONG64)offNew;
    if (hOld != NIL_SHCLOBJHANDLE)
        ShClTransferObjClose(pTransfer, hOld);
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


HRESULT ClipboardTransferFile::setACL(const com::Utf8Str &aAcl, ULONG aMode)
{
    RT_NOREF(aAcl, aMode);
    return setError(E_NOTIMPL, tr("Changing clipboard transfer file ACLs is not implemented"));
}


HRESULT ClipboardTransferFile::setSize(LONG64 aSize)
{
    RT_NOREF(aSize);
    return setError(E_NOTIMPL, tr("Changing clipboard transfer file size is not implemented"));
}


HRESULT ClipboardTransferFile::write(const std::vector<BYTE> &aData, ULONG aTimeoutMS, ULONG *aWritten)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aData, aTimeoutMS, aWritten);
    ReturnComNotImplemented();
#else
    RT_NOREF(aTimeoutMS);
    if (!aWritten)
        return setError(E_POINTER, tr("The clipboard transfer file written-bytes output argument must not be NULL"));
    *aWritten = 0;
    if (aData.empty())
        return setError(E_INVALIDARG, tr("Clipboard transfer file write data must not be empty"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    SHCLOBJHANDLE const hObj = mData.mHandle;
    FileAccessMode_T const enmAccessMode = mData.mAccessMode;
    if (enmAccessMode == FileAccessMode_ReadOnly)
        return setError(E_NOTIMPL, tr("Writing to read-only clipboard transfer files is not implemented"));
    if (!pTransfer || hObj == NIL_SHCLOBJHANDLE)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer file is closed"));
    if (   aData.size() > UINT32_MAX
        || aData.size() > pTransfer->cbMaxChunkSize)
        return setError(E_INVALIDARG, tr("Clipboard transfer file write size exceeds the backend chunk limit"));

    uint32_t cbWritten = 0;
    int vrc = ShClTransferObjWrite(pTransfer, hObj, (void *)&aData[0], (uint32_t)aData.size(), 0, &cbWritten);
    if (RT_FAILURE(vrc))
    {
        HRESULT hrc = clipboardTransferFileRcToHrc(vrc);
        return setErrorBoth(hrc, vrc, tr("Writing clipboard transfer file failed with %Rrc"), vrc);
    }
    if (   cbWritten > aData.size()
        || mData.mOffset > INT64_MAX - cbWritten)
        return setError(E_INVALIDARG, tr("Clipboard transfer provider returned an invalid write size"));
    mData.mOffset += cbWritten;
    *aWritten = cbWritten;
    return S_OK;
#endif
}


HRESULT ClipboardTransferFile::writeAt(LONG64 aOffset, const std::vector<BYTE> &aData, ULONG aTimeoutMS, ULONG *aWritten)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aOffset, aData, aTimeoutMS, aWritten);
    ReturnComNotImplemented();
#else
    LONG64 offCurrent;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        offCurrent = mData.mOffset;
    }
    if (aOffset != offCurrent)
        return setError(E_NOTIMPL, tr("Writing clipboard transfer files at non-current offsets is not implemented"));
    return write(aData, aTimeoutMS, aWritten);
#endif
}


HRESULT ClipboardTransferFile::getPath(com::Utf8Str &aPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aPath = mData.mPath;
    return S_OK;
}
