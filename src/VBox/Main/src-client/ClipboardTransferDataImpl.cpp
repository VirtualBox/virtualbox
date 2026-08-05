/* $Id: ClipboardTransferDataImpl.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer data plane object.
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
#include "ClipboardTransferDataImpl.h"

#include <VBox/com/ErrorInfo.h>

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <new>


DEFINE_EMPTY_CTOR_DTOR(ClipboardTransferData)


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts a Shared Clipboard transfer result code to a Main API HRESULT.
 *
 * @returns COM status code.
 * @param   vrc             VBox status code to convert.
 */
static HRESULT clipboardTransferDataRcToHrc(int vrc)
{
    if (RT_SUCCESS(vrc))
        return S_OK;
    if (vrc == VERR_ACCESS_DENIED)
        return VBOX_E_SHCL_ACCESS_DENIED;
    if (vrc == VERR_NOT_SUPPORTED || vrc == VERR_NOT_IMPLEMENTED)
        return E_NOTIMPL;
    if (vrc == VERR_NO_MEMORY)
        return E_OUTOFMEMORY;
    if (   vrc == VERR_INVALID_PARAMETER
        || vrc == VERR_INVALID_POINTER
        || vrc == VERR_INVALID_HANDLE
        || vrc == VERR_INVALID_NAME
        || vrc == VERR_INVALID_UTF8_ENCODING
        || vrc == VERR_PATH_IS_NOT_RELATIVE
        || vrc == VERR_WRONG_ORDER
        || vrc == VERR_BUFFER_OVERFLOW
        || vrc == VERR_TOO_MUCH_DATA)
        return E_INVALIDARG;
    if (vrc == VERR_NO_DATA || vrc == VERR_NO_MORE_FILES || vrc == VERR_NOT_FOUND)
        return VBOX_E_SHCL_NO_DATA;
    return VBOX_E_SHCL_ERROR;
}


/**
 * Copies a Shared Clipboard list entry to Main API output values.
 *
 * @returns COM status code.
 * @param   pEntry          Shared Clipboard list entry to copy.
 * @param   aName           Where to return the entry name.
 * @param   aInfoFlags      Where to return the entry information flags.
 * @param   aInfo           Where to return the entry information payload.
 */
static HRESULT clipboardTransferDataListEntryToMain(PCSHCLLISTENTRY pEntry,
                                                    com::Utf8Str &aName,
                                                    ULONG *aInfoFlags,
                                                    std::vector<BYTE> &aInfo)
{
    AssertPtrReturn(pEntry, E_POINTER);
    AssertPtrReturn(aInfoFlags, E_POINTER);
    if (!ShClTransferListEntryIsValid((PSHCLLISTENTRY)pEntry))
        return E_INVALIDARG;

    com::Utf8Str Name;
    std::vector<BYTE> Info;
    try
    {
        Name = pEntry->pszName;
        Info.resize(pEntry->cbInfo);
        if (pEntry->cbInfo)
            memcpy(&Info[0], pEntry->pvInfo, pEntry->cbInfo);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    aName.swap(Name);
    aInfo.swap(Info);
    *aInfoFlags = pEntry->fInfo;
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Completes construction of a clipboard transfer data plane object.
 *
 * @returns COM status code.
 */
HRESULT ClipboardTransferData::FinalConstruct()
{
    return BaseFinalConstruct();
}


/**
 * Releases a clipboard transfer data plane object.
 */
void ClipboardTransferData::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Initializes a clipboard transfer data plane object.
 *
 * @returns COM status code.
 * @param   aParent         Parent transfer object.
 * @param   aTransfer       Backing Shared Clipboard transfer.
 */
HRESULT ClipboardTransferData::init(const ComPtr<IClipboardTransfer> &aParent, PSHCLTRANSFER aTransfer)
{
    AssertPtrReturn(aTransfer, E_POINTER);
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mData.mParent = aParent;
    mData.mTransfer = aTransfer;

    autoInitSpan.setSucceeded();
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Uninitializes a clipboard transfer data plane object.
 */
void ClipboardTransferData::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData.mTransfer = NULL;
    mData.mParent.setNull();
#endif
}


/**
 * Opens or prepares a transfer data-plane object.
 *
 * @returns COM status code.
 * @param   aType           Transfer data-plane object type.
 * @param   aPath           Relative transfer path, or empty for the root list.
 * @param   aFilter         Optional list filter.
 * @param   aFlags          List or object-open flags.
 * @param   aHandle         Where to return the opened handle or root count.
 */
HRESULT ClipboardTransferData::open(ClipboardTransferDataType_T aType,
                                    const com::Utf8Str &aPath,
                                    const com::Utf8Str &aFilter,
                                    ULONG aFlags,
                                    LONG64 *aHandle)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aType, aPath, aFilter, aFlags, aHandle);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    if (!aHandle)
        return setError(E_POINTER, tr("The clipboard transfer data handle output argument must not be NULL"));
    *aHandle = 0;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));

    int vrc;
    switch (aType)
    {
        case ClipboardTransferDataType_RootList:
            if (   !aPath.isEmpty()
                || !aFilter.isEmpty()
                || aFlags != 0)
                return setError(E_INVALIDARG, tr("Root-list clipboard transfer open does not accept a path, filter or flags"));
            *aHandle = (LONG64)ShClTransferRootsCount(pTransfer);
            vrc = VINF_SUCCESS;
            break;

        case ClipboardTransferDataType_List:
        {
            vrc = ShClTransferValidatePath(aPath.c_str(), false /* fMustExist */);
            if (RT_FAILURE(vrc))
            {
                HRESULT const hrc = clipboardTransferDataRcToHrc(vrc);
                return setErrorBoth(hrc, vrc, tr("Invalid clipboard transfer list path: %Rrc"), vrc);
            }

            SHCLLISTOPENPARMS OpenParms;
            vrc = ShClTransferListOpenParmsInit(&OpenParms);
            if (RT_SUCCESS(vrc))
            {
                OpenParms.fList = aFlags;
                if (!aPath.isEmpty())
                    vrc = RTStrCopy(OpenParms.pszPath, OpenParms.cbPath, aPath.c_str());
                if (   RT_SUCCESS(vrc)
                    && !aFilter.isEmpty())
                    vrc = RTStrCopy(OpenParms.pszFilter, OpenParms.cbFilter, aFilter.c_str());
                if (RT_SUCCESS(vrc))
                {
                    SHCLLISTHANDLE hList = NIL_SHCLLISTHANDLE;
                    vrc = ShClTransferListOpen(pTransfer, &OpenParms, &hList);
                    if (RT_SUCCESS(vrc))
                        *aHandle = (LONG64)hList;
                }
                ShClTransferListOpenParmsDestroy(&OpenParms);
            }
            break;
        }

        case ClipboardTransferDataType_Object:
        {
            if (aPath.isEmpty())
                return setError(E_INVALIDARG, tr("Clipboard transfer object paths must not be empty"));
            vrc = ShClTransferValidatePath(aPath.c_str(), false /* fMustExist */);
            if (RT_FAILURE(vrc))
            {
                HRESULT const hrc = clipboardTransferDataRcToHrc(vrc);
                return setErrorBoth(hrc, vrc, tr("Invalid clipboard transfer object path: %Rrc"), vrc);
            }

            SHCLOBJOPENCREATEPARMS OpenParms;
            vrc = ShClTransferObjOpenParmsInit(&OpenParms);
            if (RT_SUCCESS(vrc))
            {
                OpenParms.fCreate = aFlags;
                vrc = RTStrCopy(OpenParms.pszPath, OpenParms.cbPath, aPath.c_str());
                if (RT_SUCCESS(vrc))
                {
                    SHCLOBJHANDLE hObj = NIL_SHCLOBJHANDLE;
                    vrc = ShClTransferObjOpen(pTransfer, &OpenParms, &hObj);
                    if (RT_SUCCESS(vrc))
                        *aHandle = (LONG64)hObj;
                }
                ShClTransferObjOpenParmsDestroy(&OpenParms);
            }
            break;
        }

        default:
            return setError(E_INVALIDARG, tr("Invalid clipboard transfer data type %RU32"), (uint32_t)aType);
    }

    HRESULT hrc = clipboardTransferDataRcToHrc(vrc);
    if (FAILED(hrc))
        return setErrorBoth(hrc, vrc, tr("Opening clipboard transfer data failed with %Rrc"), vrc);
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Closes an opened transfer data-plane handle.
 *
 * @returns COM status code.
 * @param   aType           Transfer data-plane object type.
 * @param   aHandle         Handle to close.
 */
HRESULT ClipboardTransferData::close(ClipboardTransferDataType_T aType, LONG64 aHandle)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aType, aHandle);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));
    if (aHandle < 0)
        return setError(E_INVALIDARG, tr("Clipboard transfer data handle must not be negative"));

    int vrc;
    switch (aType)
    {
        case ClipboardTransferDataType_RootList:
            return aHandle == 0 ? S_OK
                                : setError(E_INVALIDARG, tr("Root-list clipboard transfer handles are not persistent and only handle 0 is accepted"));
        case ClipboardTransferDataType_List:
            vrc = ShClTransferListClose(pTransfer, (SHCLLISTHANDLE)aHandle);
            break;
        case ClipboardTransferDataType_Object:
            vrc = ShClTransferObjClose(pTransfer, (SHCLOBJHANDLE)aHandle);
            break;
        default:
            return setError(E_INVALIDARG, tr("Invalid clipboard transfer data type %RU32"), (uint32_t)aType);
    }
    HRESULT hrc = clipboardTransferDataRcToHrc(vrc);
    if (FAILED(hrc))
        return setErrorBoth(hrc, vrc, tr("Closing clipboard transfer data handle failed with %Rrc"), vrc);
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Reads from the transfer data plane.
 *
 * @returns COM status code.
 * @param   aType           Transfer data-plane object type.
 * @param   aHandle         Root index, list handle, or object handle.
 * @param   aSize           Maximum object bytes to read.
 * @param   aFlags          Read flags.
 * @param   aName           Where to return root/list entry name.
 * @param   aInfoFlags      Where to return root/list entry information flags.
 * @param   aInfo           Where to return root/list entry information payload.
 * @param   aData           Where to return object payload bytes.
 */
HRESULT ClipboardTransferData::read(ClipboardTransferDataType_T aType,
                                    LONG64 aHandle,
                                    ULONG aSize,
                                    ULONG aFlags,
                                    com::Utf8Str &aName,
                                    ULONG *aInfoFlags,
                                    std::vector<BYTE> &aInfo,
                                    std::vector<BYTE> &aData)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aType, aHandle, aSize, aFlags, aName, aInfoFlags, aInfo, aData);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    if (!aInfoFlags)
        return setError(E_POINTER, tr("The clipboard transfer information-flags output argument must not be NULL"));
    *aInfoFlags = 0;
    aName.setNull();
    aInfo.clear();
    aData.clear();
    if (aHandle < 0)
        return setError(E_INVALIDARG, tr("Clipboard transfer data handle must not be negative"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));

    int vrc = VINF_SUCCESS;
    switch (aType)
    {
        case ClipboardTransferDataType_RootList:
        {
            if (aSize != 0 || aFlags != 0)
                return setError(E_INVALIDARG, tr("Root-list clipboard transfer reads require size 0 and flags 0"));

            PCSHCLLISTENTRY const pEntry = ShClTransferRootsEntryGet(pTransfer, (uint64_t)aHandle);
            if (!pEntry)
                return setErrorBoth(clipboardTransferDataRcToHrc(VERR_NOT_FOUND), VERR_NOT_FOUND,
                                    tr("No clipboard transfer root entry exists at index %RI64"), aHandle);

            HRESULT hrc = clipboardTransferDataListEntryToMain(pEntry, aName, aInfoFlags, aInfo);
            if (FAILED(hrc))
                return setError(hrc, tr("Copying clipboard transfer root entry metadata failed"));
            return S_OK;
        }

        case ClipboardTransferDataType_List:
        {
            if (aSize != 0 || aFlags != 0)
                return setError(E_INVALIDARG, tr("Clipboard transfer list reads require size 0 and flags 0"));
            SHCLLISTENTRY Entry;
            vrc = ShClTransferListEntryInit(&Entry);
            if (RT_SUCCESS(vrc))
            {
                vrc = ShClTransferListRead(pTransfer, (SHCLLISTHANDLE)aHandle, &Entry);
                if (RT_SUCCESS(vrc))
                {
                    HRESULT const hrc = clipboardTransferDataListEntryToMain(&Entry, aName, aInfoFlags, aInfo);
                    if (FAILED(hrc))
                    {
                        ShClTransferListEntryDestroy(&Entry);
                        return setError(hrc, tr("Copying clipboard transfer list entry metadata failed"));
                    }
                }
                ShClTransferListEntryDestroy(&Entry);
            }
            break;
        }

        case ClipboardTransferDataType_Object:
        {
            if (!aSize || aFlags != 0)
                return setError(E_INVALIDARG, tr("Clipboard transfer object reads require a non-zero size and flags 0"));
            if (aSize > pTransfer->cbMaxChunkSize)
                return setError(E_INVALIDARG, tr("Clipboard transfer object read size exceeds the backend chunk limit"));
            try
            {
                aData.resize(aSize);
            }
            catch (std::bad_alloc &)
            {
                return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer read buffer failed"));
            }
            uint32_t cbRead = 0;
            vrc = ShClTransferObjRead(pTransfer, (SHCLOBJHANDLE)aHandle, aData.empty() ? NULL : &aData[0], aSize,
                                      aFlags, &cbRead);
            if (   RT_SUCCESS(vrc)
                && cbRead <= aSize)
                aData.resize(cbRead);
            else if (RT_SUCCESS(vrc))
            {
                aData.clear();
                return setError(E_INVALIDARG, tr("Clipboard transfer provider returned an invalid read size"));
            }
            if (RT_FAILURE(vrc))
                aData.clear();
            break;
        }

        default:
            return setError(E_INVALIDARG, tr("Invalid clipboard transfer data type %RU32"), (uint32_t)aType);
    }

    HRESULT hrc = clipboardTransferDataRcToHrc(vrc);
    if (FAILED(hrc))
        return setErrorBoth(hrc, vrc, tr("Reading clipboard transfer data failed with %Rrc"), vrc);
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Writes to the transfer data plane.
 *
 * @returns COM status code.
 * @param   aType           Transfer data-plane object type.
 * @param   aHandle         List or object handle.
 * @param   aName           List entry name.
 * @param   aInfoFlags      List entry information flags.
 * @param   aInfo           List entry information payload.
 * @param   aData           Object payload bytes.
 * @param   aFlags          Write flags.
 * @param   aWritten        Where to return bytes written.
 */
HRESULT ClipboardTransferData::write(ClipboardTransferDataType_T aType,
                                     LONG64 aHandle,
                                     const com::Utf8Str &aName,
                                     ULONG aInfoFlags,
                                     const std::vector<BYTE> &aInfo,
                                     const std::vector<BYTE> &aData,
                                     ULONG aFlags,
                                     ULONG *aWritten)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aType, aHandle, aName, aInfoFlags, aInfo, aData, aFlags, aWritten);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    if (!aWritten)
        return setError(E_POINTER, tr("The clipboard transfer written-bytes output argument must not be NULL"));
    *aWritten = 0;
    if (aHandle < 0)
        return setError(E_INVALIDARG, tr("Clipboard transfer data handle must not be negative"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    PSHCLTRANSFER const pTransfer = mData.mTransfer;
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));

    int vrc;
    switch (aType)
    {
        case ClipboardTransferDataType_List:
        {
            if (aFlags != 0 || aName.isEmpty())
                return setError(E_INVALIDARG, tr("Clipboard transfer list writes require flags 0 and a non-empty entry name"));
            if (   aInfo.size() > UINT32_MAX
                || (   aInfoFlags == VBOX_SHCL_INFO_F_NONE
                    && !aInfo.empty())
                || (   aInfoFlags == VBOX_SHCL_INFO_F_FSOBJINFO
                    && aInfo.size() != sizeof(SHCLFSOBJINFO))
                || (   aInfoFlags != VBOX_SHCL_INFO_F_NONE
                    && aInfoFlags != VBOX_SHCL_INFO_F_FSOBJINFO))
                return setError(E_INVALIDARG, tr("Clipboard transfer list information flags and payload do not match"));
            void *pvInfo = NULL;
            if (!aInfo.empty())
            {
                pvInfo = RTMemDup(&aInfo[0], aInfo.size());
                if (!pvInfo)
                {
                    vrc = VERR_NO_MEMORY;
                    break;
                }
            }

            SHCLLISTENTRY Entry;
            vrc = ShClTransferListEntryInitEx(&Entry, aInfoFlags, aName.c_str(), pvInfo, (uint32_t)aInfo.size());
            if (RT_SUCCESS(vrc))
            {
                pvInfo = NULL; /* Ownership transferred to Entry. */
                if (!ShClTransferListEntryIsValid(&Entry))
                    vrc = VERR_INVALID_PARAMETER;
                else
                    vrc = ShClTransferListWrite(pTransfer, (SHCLLISTHANDLE)aHandle, &Entry);
                ShClTransferListEntryDestroy(&Entry);
            }
            if (pvInfo)
                RTMemFree(pvInfo);
            break;
        }

        case ClipboardTransferDataType_Object:
        {
            if (   aFlags != 0
                || aData.empty()
                || aData.size() > UINT32_MAX)
                return setError(E_INVALIDARG, tr("Clipboard transfer object writes require flags 0 and between 1 and UINT32_MAX payload bytes"));
            uint32_t cbWritten = 0;
            vrc = ShClTransferObjWrite(pTransfer, (SHCLOBJHANDLE)aHandle, (void *)&aData[0], (uint32_t)aData.size(),
                                       aFlags, &cbWritten);
            if (   RT_SUCCESS(vrc)
                && cbWritten <= aData.size())
                *aWritten = cbWritten;
            else if (RT_SUCCESS(vrc))
                return setError(E_INVALIDARG, tr("Clipboard transfer provider returned an invalid write size"));
            break;
        }

        case ClipboardTransferDataType_RootList:
            return setError(E_NOTIMPL, tr("Writing clipboard transfer root-list entries is not supported"));
        default:
            return setError(E_INVALIDARG, tr("Invalid clipboard transfer data type %RU32"), (uint32_t)aType);
    }

    HRESULT hrc = clipboardTransferDataRcToHrc(vrc);
    if (FAILED(hrc))
        return setErrorBoth(hrc, vrc, tr("Writing clipboard transfer data failed with %Rrc"), vrc);
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}
