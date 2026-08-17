/* $Id: ClipboardTransferImpl.cpp 115060 2026-08-17 17:28:06Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer object.
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
#include "ClipboardTransferDataImpl.h"
#include "ClipboardTransferDirectoryImpl.h"
#include "ClipboardTransferFileImpl.h"
#include "ClipboardTransferFsObjInfoImpl.h"

#include <VBox/com/ErrorInfo.h>

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include <new>

/** Maximum number of active directory levels, including the initial directory,
 *  while recursively listing a transfer.  This bounds stack consumption for
 *  externally supplied directory trees. */
#define VBOX_SHCL_MAIN_MAX_RECURSION_DEPTH      128
/** Maximum number of result nodes accumulated while recursively listing a
 *  transfer.  This bounds memory consumption and traversal work for externally
 *  supplied directory trees. */
#define VBOX_SHCL_MAIN_MAX_RECURSIVE_NODES      _64K


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(ClipboardTransfer)


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts a Shared Clipboard transfer result code to a Main API HRESULT.
 *
 * @returns COM status code.
 * @param   vrc             VBox status code to convert.
 */
static HRESULT clipboardTransferDataPlaneRcToHrc(int vrc)
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
        || vrc == VERR_WRONG_ORDER
        || vrc == VERR_BUFFER_OVERFLOW
        || vrc == VERR_TOO_MUCH_DATA)
        return E_INVALIDARG;
    if (vrc == VERR_NO_DATA || vrc == VERR_NO_MORE_FILES || vrc == VERR_NOT_FOUND)
        return VBOX_E_SHCL_NO_DATA;
    return VBOX_E_SHCL_ERROR;
}


/**
 * Maps a Shared Clipboard transfer direction to the Main transfer direction.
 *
 * @returns Main transfer direction.
 * @param   enmDir              Shared Clipboard transfer direction.
 */
static ClipboardTransferDirection_T clipboardTransferDirectionFromShCl(SHCLTRANSFERDIR enmDir)
{
    switch (enmDir)
    {
        case SHCLTRANSFERDIR_HOST_TO_GUEST: return ClipboardTransferDirection_ToGuest;
        case SHCLTRANSFERDIR_GUEST_TO_HOST: return ClipboardTransferDirection_ToHost;
        default:                            return ClipboardTransferDirection_Any;
    }
}


/**
 * Maps a Shared Clipboard source to the Main clipboard source.
 *
 * @returns Main clipboard source.
 * @param   enmSource           Shared Clipboard source.
 */
static ClipboardSource_T clipboardTransferSourceFromShCl(SHCLSOURCE enmSource)
{
    switch (enmSource)
    {
        case SHCLSOURCE_LOCAL:  return ClipboardSource_Host;
        case SHCLSOURCE_REMOTE: return ClipboardSource_Guest;
        default:                return ClipboardSource_Custom;
    }
}


/**
 * Builds a transfer-relative child path.
 *
 * @returns Child path.
 * @param   aParent             Parent path.
 * @param   pszName             Entry name.
 */
static com::Utf8Str clipboardTransferMakeChildPath(const com::Utf8Str &aParent, const char *pszName)
{
    if (aParent.isEmpty())
        return com::Utf8Str(pszName ? pszName : "");
    return com::Utf8StrFmt("%s/%s", aParent.c_str(), pszName ? pszName : "");
}


/**
 * Validates a transfer-relative path supplied through the high-level API.
 *
 * @returns COM status code.
 * @param   aPath               Path to validate.
 * @param   fAllowEmpty         Whether the empty path is accepted.
 */
static HRESULT clipboardTransferValidatePath(const com::Utf8Str &aPath, bool fAllowEmpty)
{
    if (aPath.isEmpty())
        return fAllowEmpty ? S_OK : E_INVALIDARG;

    const char *pszPath = aPath.c_str();
    if (   pszPath[0] == '/'
        || pszPath[0] == '\\'
        || pszPath[aPath.length() - 1] == '/'
        || strchr(pszPath, '\\')
        || strchr(pszPath, ':'))
        return E_INVALIDARG;

    const char *psz = pszPath;
    while (*psz)
    {
        const char *pszSlash = strchr(psz, '/');
        size_t const cchComponent = pszSlash ? (size_t)(pszSlash - psz) : strlen(psz);
        if (   cchComponent == 0
            || (cchComponent == 1 && psz[0] == '.')
            || (cchComponent == 2 && psz[0] == '.' && psz[1] == '.'))
            return E_INVALIDARG;
        if (!pszSlash)
            break;
        psz = pszSlash + 1;
    }
    return S_OK;
}


/**
 * Validates a local source path supplied for a Main-created transfer.
 *
 * @returns COM status code.
 * @param   aSourcePath         Source-side local path to validate.
 */
static HRESULT clipboardTransferValidateSourcePath(const com::Utf8Str &aSourcePath)
{
    if (aSourcePath.isEmpty())
        return E_INVALIDARG;

    const char *pszPath = aSourcePath.c_str();
    if (   strchr(pszPath, '\r')
        || strchr(pszPath, '\n')
        || strstr(pszPath, SHCL_TRANSFER_URI_LIST_SEP_STR))
        return E_INVALIDARG;
    return S_OK;
}


/**
 * Creates a local provider-backed Shared Clipboard transfer from source paths.
 *
 * @returns COM status code.
 * @param   aSourcePaths        Source-side local paths to publish as transfer roots.
 * @param   ppTransfer          Where to return the created backend transfer.
 */
static HRESULT clipboardTransferCreateLocalProviderBackend(const std::vector<com::Utf8Str> &aSourcePaths,
                                                           PSHCLTRANSFER *ppTransfer)
{
    AssertPtrReturn(ppTransfer, E_POINTER);
    *ppTransfer = NULL;
    if (aSourcePaths.empty())
        return S_OK;
    com::Utf8Str strRoots;
    try
    {
        for (std::vector<com::Utf8Str>::const_iterator it = aSourcePaths.begin(); it != aSourcePaths.end(); ++it)
        {
            HRESULT hrc = clipboardTransferValidateSourcePath(*it);
            if (FAILED(hrc))
                return hrc;
            strRoots += *it;
            strRoots += SHCL_TRANSFER_URI_LIST_SEP_STR;
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    PSHCLTRANSFER pTransfer = NULL;
    int vrc = ShClTransferCreateEx(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL,
                                   SHCL_TRANSFER_DEFAULT_MAX_CHUNK_SIZE,
                                   SHCL_TRANSFER_DEFAULT_MAX_LIST_HANDLES,
                                   SHCL_TRANSFER_DEFAULT_MAX_OBJ_HANDLES,
                                   &pTransfer);
    if (RT_SUCCESS(vrc))
    {
        SHCLTXPROVIDER Provider;
        RT_ZERO(Provider);
        ShClTransferProviderLocalQueryInterface(&Provider);
        Provider.enmSource = SHCLSOURCE_LOCAL;
        vrc = ShClTransferSetProvider(pTransfer, &Provider);
    }
    if (RT_SUCCESS(vrc))
        vrc = ShClTransferRootsSetFromStringList(pTransfer, strRoots.c_str(), strRoots.length() + 1 /* terminator */);
    if (RT_SUCCESS(vrc))
        vrc = ShClTransferInit(pTransfer);

    if (RT_FAILURE(vrc))
    {
        if (pTransfer)
            ShClTransferDestroy(pTransfer);
        return clipboardTransferDataPlaneRcToHrc(vrc);
    }

    *ppTransfer = pTransfer;
    return S_OK;
}


/**
 * Creates a high-level transfer object-info wrapper from a Shared Clipboard entry.
 *
 * @returns COM status code.
 * @param   aParent             Parent path.
 * @param   pEntry              Shared Clipboard list entry.
 * @param   aNode               Where to return the Main object-info wrapper.
 */
static HRESULT clipboardTransferCreateFsObjInfoFromEntry(const com::Utf8Str &aParent,
                                                         PCSHCLLISTENTRY pEntry,
                                                         ComPtr<IClipboardTransferFsObjInfo> &aNode)
{
    AssertPtrReturn(pEntry, E_POINTER);
    if (   pEntry->fInfo != VBOX_SHCL_INFO_F_FSOBJINFO
        || !ShClTransferListEntryIsValid((PSHCLLISTENTRY)pEntry))
        return E_INVALIDARG;

    ComObjPtr<ClipboardTransferFsObjInfo> ptrInfo;
    HRESULT hrc = ptrInfo.createObject();
    if (FAILED(hrc))
        return hrc;

    try
    {
        com::Utf8Str const strName(pEntry->pszName);
        com::Utf8Str const strPath = clipboardTransferMakeChildPath(aParent, pEntry->pszName);
        hrc = ptrInfo->init(strPath, strName, (PCSHCLFSOBJINFO)pEntry->pvInfo);
        if (FAILED(hrc))
            return hrc;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return ptrInfo.queryInterfaceTo(aNode.asOutParam());
}


/**
 * Opens a transfer list.
 *
 * @returns COM status code.
 * @param   pTransfer           Backing Shared Clipboard transfer.
 * @param   aPath               Transfer-relative directory path.
 * @param   phList              Where to return the list handle.
 */
static HRESULT clipboardTransferOpenList(PSHCLTRANSFER pTransfer, const com::Utf8Str &aPath, PSHCLLISTHANDLE phList)
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
    return clipboardTransferDataPlaneRcToHrc(vrc);
}


/**
 * Recursively lists a transfer directory.
 *
 * @returns COM status code.
 * @param   pTransfer           Backing Shared Clipboard transfer.
 * @param   aPath               Transfer-relative directory path.
 * @param   aFlags              ClipboardTransferListFlag mask.
 * @param   aNodes              Where to append listed nodes.
 * @param   cDepth              Current recursion depth.
 */
static HRESULT clipboardTransferListRecursive(PSHCLTRANSFER pTransfer,
                                              const com::Utf8Str &aPath,
                                              ULONG aFlags,
                                              std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes,
                                              uint32_t cDepth)
{
    if (cDepth >= VBOX_SHCL_MAIN_MAX_RECURSION_DEPTH)
        return E_INVALIDARG;

    SHCLLISTHANDLE hList = NIL_SHCLLISTHANDLE;
    HRESULT hrc = clipboardTransferOpenList(pTransfer, aPath, &hList);
    if (FAILED(hrc))
        return hrc;

    for (;;)
    {
        SHCLLISTENTRY Entry;
        int vrc = ShClTransferListEntryInit(&Entry);
        if (RT_FAILURE(vrc))
        {
            ShClTransferListClose(pTransfer, hList);
            return clipboardTransferDataPlaneRcToHrc(vrc);
        }
        vrc = ShClTransferListRead(pTransfer, hList, &Entry);
        if (RT_FAILURE(vrc))
        {
            ShClTransferListEntryDestroy(&Entry);
            ShClTransferListClose(pTransfer, hList);
            if (vrc == VERR_NO_MORE_FILES || vrc == VERR_NO_DATA || vrc == VERR_NOT_FOUND)
                return S_OK;
            return clipboardTransferDataPlaneRcToHrc(vrc);
        }

        ComPtr<IClipboardTransferFsObjInfo> ptrInfo;
        hrc = clipboardTransferCreateFsObjInfoFromEntry(aPath, &Entry, ptrInfo);
        bool const fIsDirectory =    Entry.pvInfo
                                  && Entry.cbInfo == sizeof(SHCLFSOBJINFO)
                                  && RTFS_IS_DIRECTORY(((PCSHCLFSOBJINFO)Entry.pvInfo)->Attr.fMode);
        com::Utf8Str strChild;
        if (SUCCEEDED(hrc))
        {
            Bstr bstrPath;
            hrc = ptrInfo->COMGETTER(Path)(bstrPath.asOutParam());
            if (SUCCEEDED(hrc))
            {
                try
                {
                    strChild = bstrPath;
                    if (aNodes.size() >= VBOX_SHCL_MAIN_MAX_RECURSIVE_NODES)
                        hrc = E_INVALIDARG;
                    else
                        aNodes.push_back(ptrInfo);
                }
                catch (std::bad_alloc &)
                {
                    hrc = E_OUTOFMEMORY;
                }
            }
        }
        ShClTransferListEntryDestroy(&Entry);
        if (FAILED(hrc))
        {
            ShClTransferListClose(pTransfer, hList);
            return hrc;
        }
        if (   fIsDirectory
            && !(aFlags & ClipboardTransferListFlag_NoRecursion))
        {
            hrc = clipboardTransferListRecursive(pTransfer, strChild, aFlags, aNodes, cDepth + 1);
            if (FAILED(hrc))
            {
                ShClTransferListClose(pTransfer, hList);
                return hrc;
            }
        }
    }
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Completes construction of a clipboard transfer object.
 *
 * @returns COM status code.
 */
HRESULT ClipboardTransfer::FinalConstruct()
{
    Log3Func(("\n"));
    mData.mId        = 0;
    mData.mDirection = ClipboardTransferDirection_Any;
    mData.mSource    = ClipboardSource_Custom;
    mData.mAction    = ClipboardAction_Invalid;
    mData.mState     = ClipboardTransferState_Added;
    mData.mError     = ClipboardError_None;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    mData.mTransfer = NULL;
    mData.mfOwnTransfer = false;
#endif
    return BaseFinalConstruct();
}


/**
 * Releases a clipboard transfer object.
 */
void ClipboardTransfer::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


/**
 * Initializes a clipboard transfer object.
 *
 * @returns COM status code.
 * @param   aId             Unique transfer identifier.
 * @param   aDirection      Clipboard transfer direction.
 * @param   aSource         Clipboard transfer source.
 * @param   aAction         Clipboard transfer action.
 * @param   aItem           Clipboard item being transferred.
 * @param   aProgress       Progress object for the transfer.
 */
HRESULT ClipboardTransfer::init(ULONG aId,
                                ClipboardTransferDirection_T aDirection,
                                ClipboardSource_T aSource,
                                ClipboardAction_T aAction,
                                const ComPtr<IClipboardItem> &aItem,
                                const ComPtr<IProgress> &aProgress
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                                , PSHCLTRANSFER aTransfer /* = NULL */,
                                bool fOwnTransfer /* = false */
#endif
                                )
{
    Log2Func(("aId=%RU32, aDirection=%RU32, aSource=%RU32, aAction=%RU32, aItem=%p, aProgress=%p\n",
              (uint32_t)aId, (uint32_t)aDirection, (uint32_t)aSource, (uint32_t)aAction, (void *)aItem, (void *)aProgress));
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mData.mId       = aId;
    mData.mDirection = aDirection;
    mData.mSource   = aSource;
    mData.mAction   = aAction;
    mData.mItem     = aItem;
    mData.mProgress = aProgress;
    mData.mState    = ClipboardTransferState_Added;
    mData.mMessage.setNull();
    mData.mError    = ClipboardError_None;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    mData.mTransfer = aTransfer;
    mData.mfOwnTransfer = fOwnTransfer;
    if (aTransfer)
    {
        mData.mDirection = clipboardTransferDirectionFromShCl(ShClTransferGetDir(aTransfer));
        mData.mSource = clipboardTransferSourceFromShCl(ShClTransferGetSource(aTransfer));
    }
#endif

    autoInitSpan.setSucceeded();
    return S_OK;
}


/**
 * Uninitializes a clipboard transfer object.
 */
void ClipboardTransfer::uninit()
{
    Log3Func(("id=%RU32, action=%RU32\n", (uint32_t)mData.mId, (uint32_t)mData.mAction));
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    PSHCLTRANSFER pTransfer = NULL;
    bool fOwnTransfer = false;
#endif
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        mData.mId = 0;
        mData.mDirection = ClipboardTransferDirection_Any;
        mData.mSource = ClipboardSource_Custom;
        mData.mAction = ClipboardAction_Invalid;
        mData.mState = ClipboardTransferState_Removed;
        mData.mItem.setNull();
        mData.mProgress.setNull();
        mData.mMessage.setNull();
        mData.mError = ClipboardError_None;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        mData.mSourcePaths.clear();
        pTransfer = mData.mTransfer;
        fOwnTransfer = mData.mfOwnTransfer;
        mData.mTransfer = NULL;
        mData.mfOwnTransfer = false;
#endif
    }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fOwnTransfer && pTransfer)
    {
        int vrc = ShClTransferDestroy(pTransfer);
        AssertRC(vrc);
    }
#endif
}


/**
 * Returns the transfer identifier.
 *
 * @returns COM status code.
 * @param   aId             Where to return the transfer identifier.
 */
HRESULT ClipboardTransfer::getId(ULONG *aId)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aId);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aId = mData.mId;
    Log3Func(("aId=%RU32\n", (uint32_t)*aId));
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Returns the backing Shared Clipboard transfer.
 *
 * @returns Backing Shared Clipboard transfer, or NULL if none is attached.
 */
PSHCLTRANSFER ClipboardTransfer::i_getTransfer() const
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return mData.mTransfer;
}
#endif


/**
 * Updates the public transfer state fields.
 *
 * @param   aState          New public transfer state.
 * @param   aMessage        Optional transfer status message.
 * @param   aError          Transfer error code.
 */
void ClipboardTransfer::i_setState(ClipboardTransferState_T aState,
                                   const com::Utf8Str &aMessage,
                                   ClipboardError_T aError)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData.mState = aState;
    mData.mMessage = aMessage;
    mData.mError = aError;
}


/**
 * Returns the transfer direction.
 *
 * @returns COM status code.
 * @param   aDirection      Where to return the transfer direction.
 */
HRESULT ClipboardTransfer::getDirection(ClipboardTransferDirection_T *aDirection)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aDirection);
    ReturnComNotImplemented();
#else
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aDirection = mData.mDirection;
    return S_OK;
#endif
}


/**
 * Returns the clipboard source that owns or advertised the transfer.
 *
 * @returns COM status code.
 * @param   aSource         Where to return the clipboard source.
 */
HRESULT ClipboardTransfer::getSource(ClipboardSource_T *aSource)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aSource);
    ReturnComNotImplemented();
#else
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aSource = mData.mSource;
    return S_OK;
#endif
}


/**
 * Returns the transfer action.
 *
 * @returns COM status code.
 * @param   aAction         Where to return the transfer action.
 */
HRESULT ClipboardTransfer::getAction(ClipboardAction_T *aAction)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aAction);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aAction = mData.mAction;
    Log3Func(("aAction=%RU32\n", (uint32_t)*aAction));
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Returns the current transfer state.
 *
 * @returns COM status code.
 * @param   aState          Where to return the transfer state.
 */
HRESULT ClipboardTransfer::getState(ClipboardTransferState_T *aState)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aState);
    ReturnComNotImplemented();
#else
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aState = mData.mState;
    return S_OK;
#endif
}


/**
 * Returns the transfer item.
 *
 * @returns COM status code.
 * @param   aItem           Where to return the transfer item.
 */
HRESULT ClipboardTransfer::getItem(ComPtr<IClipboardItem> &aItem)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aItem);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aItem = mData.mItem;
    Log3Func(("aItem=%p\n", (void *)aItem));
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Returns the transfer progress object.
 *
 * @returns COM status code.
 * @param   aProgress       Where to return the progress object.
 */
HRESULT ClipboardTransfer::getProgress(ComPtr<IProgress> &aProgress)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aProgress);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aProgress = mData.mProgress;
    Log3Func(("aProgress=%p\n", (void *)aProgress));
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Returns the transfer message.
 *
 * @returns COM status code.
 * @param   aMessage        Where to return the transfer message.
 */
HRESULT ClipboardTransfer::getMessage(com::Utf8Str &aMessage)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aMessage);
    ReturnComNotImplemented();
#else
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aMessage = mData.mMessage;
    return S_OK;
#endif
}


/**
 * Returns the last transfer error.
 *
 * @returns COM status code.
 * @param   aError          Where to return the transfer error.
 */
HRESULT ClipboardTransfer::getError(ClipboardError_T *aError)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aError);
    ReturnComNotImplemented();
#else
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aError = mData.mError;
    return S_OK;
#endif
}


/**
 * Returns the low-level transfer data-plane interface.
 *
 * @returns COM status code.
 * @param   aData           Where to return the data-plane interface.
 */
HRESULT ClipboardTransfer::getData(ComPtr<IClipboardTransferData> &aData)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aData);
    ReturnComNotImplemented();
#else
    PSHCLTRANSFER const pTransfer = i_getTransfer();
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));

    ComObjPtr<ClipboardTransferData> ptrData;
    HRESULT hrc = ptrData.createObject();
    if (FAILED(hrc))
        return setError(hrc, tr("Creating clipboard transfer data-plane object failed"));

    ComPtr<IClipboardTransfer> ptrSelf(this);
    hrc = ptrData->init(ptrSelf, pTransfer);
    if (FAILED(hrc))
        return setError(hrc, tr("Initializing clipboard transfer data-plane object failed"));
    hrc = ptrData.queryInterfaceTo(aData.asOutParam());
    if (FAILED(hrc))
        return setError(hrc, tr("Querying clipboard transfer data-plane interface failed"));
    return S_OK;
#endif
}


/**
 * Returns the source-side local paths explicitly configured for this transfer.
 *
 * @returns COM status code.
 * @param   aSourcePaths    Where to return the source paths.
 */
HRESULT ClipboardTransfer::getSourcePaths(std::vector<com::Utf8Str> &aSourcePaths)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aSourcePaths);
    ReturnComNotImplemented();
#else
    std::vector<com::Utf8Str> SourcePaths;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        try
        {
            SourcePaths = mData.mSourcePaths;
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer source-path result failed"));
        }
    }
    aSourcePaths.swap(SourcePaths);
    return S_OK;
#endif
}


/**
 * Sets the source-side local paths backing this transfer.
 *
 * @returns COM status code.
 * @param   aSourcePaths    Source-side local paths to publish as transfer roots.
 */
HRESULT ClipboardTransfer::setSourcePaths(const std::vector<com::Utf8Str> &aSourcePaths)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aSourcePaths);
    ReturnComNotImplemented();
#else
    ClipboardTransferDirection_T enmDirection;
    ClipboardSource_T enmSource;
    ClipboardTransferState_T enmState;
    bool fOwnTransfer;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        enmDirection = mData.mDirection;
        enmSource = mData.mSource;
        enmState = mData.mState;
        fOwnTransfer = mData.mfOwnTransfer;
        if (mData.mTransfer && !fOwnTransfer)
            return setError(E_NOTIMPL, tr("Clipboard transfer source paths cannot replace a foreign data-plane backend"));
    }

    if (   enmDirection != ClipboardTransferDirection_ToGuest
        || enmSource != ClipboardSource_Host)
        return setError(E_NOTIMPL, tr("Clipboard transfer source paths currently require a host-to-guest transfer"));
    if (enmState != ClipboardTransferState_Added)
        return setError(E_FAIL, tr("Clipboard transfer source paths can only be changed while the transfer is pending"));

    std::vector<com::Utf8Str> vecSourcePaths;
    try
    {
        vecSourcePaths = aSourcePaths;
    }
    catch (std::bad_alloc &)
    {
        return setError(E_OUTOFMEMORY, tr("Copying clipboard transfer source paths failed"));
    }

    PSHCLTRANSFER pNewTransfer = NULL;
    HRESULT hrc = clipboardTransferCreateLocalProviderBackend(vecSourcePaths, &pNewTransfer);
    if (FAILED(hrc))
        return setError(hrc, tr("Creating clipboard transfer source-path backend failed"));

    PSHCLTRANSFER pOldTransfer = NULL;
    bool fDestroyOldTransfer = false;
    bool fForeignBackend = false;
    bool fWrongDirection = false;
    bool fWrongState = false;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        fForeignBackend = mData.mTransfer && !mData.mfOwnTransfer;
        fWrongDirection =    mData.mDirection != ClipboardTransferDirection_ToGuest
                          || mData.mSource != ClipboardSource_Host;
        fWrongState = mData.mState != ClipboardTransferState_Added;
        if (!fForeignBackend && !fWrongDirection && !fWrongState)
        {
            pOldTransfer = mData.mTransfer;
            fDestroyOldTransfer = mData.mfOwnTransfer && pOldTransfer;
            mData.mTransfer = pNewTransfer;
            mData.mfOwnTransfer = pNewTransfer != NULL;
            mData.mSourcePaths.swap(vecSourcePaths);
        }
    }

    if (fForeignBackend || fWrongDirection || fWrongState)
    {
        if (pNewTransfer)
        {
            int vrc = ShClTransferDestroy(pNewTransfer);
            AssertRC(vrc);
        }
        if (fForeignBackend)
            return setError(E_NOTIMPL, tr("Clipboard transfer source paths cannot replace a foreign data-plane backend"));
        if (fWrongDirection)
            return setError(E_NOTIMPL, tr("Clipboard transfer source paths currently require a host-to-guest transfer"));
        return setError(E_FAIL, tr("Clipboard transfer source paths can only be changed while the transfer is pending"));
    }

    if (fDestroyOldTransfer)
    {
        int vrc = ShClTransferDestroy(pOldTransfer);
        AssertRC(vrc);
    }
    return S_OK;
#endif
}


/**
 * Returns the transfer root nodes.
 *
 * @returns COM status code.
 * @param   aNodes          Where to return the root nodes.
 */
HRESULT ClipboardTransfer::roots(std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aNodes);
    ReturnComNotImplemented();
#else
    PSHCLTRANSFER const pTransfer = i_getTransfer();
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));
    std::vector<ComPtr<IClipboardTransferFsObjInfo> > Nodes;
    HRESULT const hrc = i_roots(pTransfer, Nodes);
    if (SUCCEEDED(hrc))
        aNodes.swap(Nodes);
    return hrc;
#endif
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Returns root nodes from one parent-owned backing transfer.
 *
 * @returns COM status code.
 * @param   pTransfer       Retained backing transfer.
 * @param   aNodes          Where to return the root nodes.
 */
HRESULT ClipboardTransfer::i_roots(PSHCLTRANSFER pTransfer,
                                   std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes)
{
    AssertPtrReturn(pTransfer, E_POINTER);
    aNodes.clear();

    uint64_t const cRoots = ShClTransferRootsCount(pTransfer);
    for (uint64_t i = 0; i < cRoots; ++i)
    {
        PCSHCLLISTENTRY const pEntry = ShClTransferRootsEntryGet(pTransfer, i);
        if (!pEntry)
            return setErrorBoth(clipboardTransferDataPlaneRcToHrc(VERR_NOT_FOUND), VERR_NOT_FOUND,
                                tr("No clipboard transfer root entry exists at index %RU64"), i);

        ComPtr<IClipboardTransferFsObjInfo> ptrInfo;
        HRESULT hrc = clipboardTransferCreateFsObjInfoFromEntry(com::Utf8Str(), pEntry, ptrInfo);
        if (FAILED(hrc))
            return setError(hrc, tr("Creating clipboard transfer root entry information failed"));
        try
        {
            aNodes.push_back(ptrInfo);
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer root result failed"));
        }
    }
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Queries a transfer-relative node.
 *
 * @returns COM status code.
 * @param   aPath           Transfer-relative path.
 * @param   aNode           Where to return the node information.
 */
HRESULT ClipboardTransfer::query(const com::Utf8Str &aPath,
                                 ComPtr<IClipboardTransferFsObjInfo> &aNode)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aPath, aNode);
    ReturnComNotImplemented();
#else
    HRESULT hrc = clipboardTransferValidatePath(aPath, false /* fAllowEmpty */);
    if (FAILED(hrc))
        return setError(hrc, tr("Invalid clipboard transfer query path"));

    PSHCLTRANSFER const pTransfer = i_getTransfer();
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));
    return i_query(pTransfer, aPath, aNode);
#endif
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Queries a node from one parent-owned backing transfer.
 *
 * @returns COM status code.
 * @param   pTransfer       Retained backing transfer.
 * @param   aPath           Transfer-relative path.
 * @param   aNode           Where to return the node information.
 */
HRESULT ClipboardTransfer::i_query(PSHCLTRANSFER pTransfer,
                                   const com::Utf8Str &aPath,
                                   ComPtr<IClipboardTransferFsObjInfo> &aNode)
{
    AssertPtrReturn(pTransfer, E_POINTER);
    std::vector<ComPtr<IClipboardTransferFsObjInfo> > vecNodes;
    HRESULT hrc = i_list(pTransfer, com::Utf8Str(), ClipboardTransferListFlag_None, vecNodes);
    if (FAILED(hrc))
        return setError(hrc, tr("Listing clipboard transfer roots for query failed"));
    for (std::vector<ComPtr<IClipboardTransferFsObjInfo> >::const_iterator it = vecNodes.begin(); it != vecNodes.end(); ++it)
    {
        Bstr bstrPath;
        hrc = (*it)->COMGETTER(Path)(bstrPath.asOutParam());
        if (FAILED(hrc))
            return setError(hrc, tr("Querying clipboard transfer node path failed"));
        try
        {
            if (com::Utf8Str(bstrPath) == aPath)
            {
                aNode = *it;
                return S_OK;
            }
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer query path failed"));
        }
    }
    return setError(VBOX_E_SHCL_NO_DATA, tr("Clipboard transfer path '%s' was not found"), aPath.c_str());
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Lists a transfer directory.
 *
 * @returns COM status code.
 * @param   aPath           Transfer-relative directory path, or empty for roots.
 * @param   aFlags          ClipboardTransferListFlag mask.
 * @param   aNodes          Where to return listed nodes.
 */
HRESULT ClipboardTransfer::list(const com::Utf8Str &aPath,
                                ULONG aFlags,
                                std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aPath, aFlags, aNodes);
    ReturnComNotImplemented();
#else
    if (aFlags & ~(ClipboardTransferListFlag_NoRecursion | ClipboardTransferListFlag_IncludeRoot | ClipboardTransferListFlag_NoFollowSymlinks))
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer list flags %RU32"), aFlags);
    HRESULT hrc = clipboardTransferValidatePath(aPath, true /* fAllowEmpty */);
    if (FAILED(hrc))
        return setError(hrc, tr("Invalid clipboard transfer list path"));

    PSHCLTRANSFER const pTransfer = i_getTransfer();
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));
    std::vector<ComPtr<IClipboardTransferFsObjInfo> > Nodes;
    hrc = i_list(pTransfer, aPath, aFlags, Nodes);
    if (SUCCEEDED(hrc))
        aNodes.swap(Nodes);
    return hrc;
#endif
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Lists nodes from one parent-owned backing transfer.
 *
 * @returns COM status code.
 * @param   pTransfer       Retained backing transfer.
 * @param   aPath           Transfer-relative directory path, or empty for roots.
 * @param   aFlags          ClipboardTransferListFlag mask.
 * @param   aNodes          Where to return listed nodes.
 */
HRESULT ClipboardTransfer::i_list(PSHCLTRANSFER pTransfer,
                                  const com::Utf8Str &aPath,
                                  ULONG aFlags,
                                  std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes)
{
    AssertPtrReturn(pTransfer, E_POINTER);
    aNodes.clear();

    if (aPath.isEmpty())
    {
        HRESULT hrc = i_roots(pTransfer, aNodes);
        if (FAILED(hrc))
            return setError(hrc, tr("Listing clipboard transfer roots failed"));
        if (aFlags & ClipboardTransferListFlag_NoRecursion)
            return S_OK;

        std::vector<ComPtr<IClipboardTransferFsObjInfo> > vecRoots;
        try
        {
            vecRoots = aNodes;
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer root traversal list failed"));
        }
        for (std::vector<ComPtr<IClipboardTransferFsObjInfo> >::const_iterator it = vecRoots.begin(); it != vecRoots.end(); ++it)
        {
            FsObjType_T enmType = FsObjType_Unknown;
            HRESULT hrc2 = (*it)->COMGETTER(Type)(&enmType);
            if (FAILED(hrc2))
                return setError(hrc2, tr("Querying clipboard transfer node type failed"));
            if (enmType == FsObjType_Directory)
            {
                Bstr bstrPath;
                hrc2 = (*it)->COMGETTER(Path)(bstrPath.asOutParam());
                if (FAILED(hrc2))
                    return setError(hrc2, tr("Querying clipboard transfer node path failed"));
                try
                {
                    hrc2 = clipboardTransferListRecursive(pTransfer, com::Utf8Str(bstrPath), aFlags, aNodes, 0);
                }
                catch (std::bad_alloc &)
                {
                    hrc2 = E_OUTOFMEMORY;
                }
                if (FAILED(hrc2))
                    return setError(hrc2, tr("Recursively listing clipboard transfer directory failed"));
            }
        }
        return S_OK;
    }

    if (aFlags & ClipboardTransferListFlag_IncludeRoot)
    {
        ComPtr<IClipboardTransferFsObjInfo> ptrRoot;
        HRESULT hrc = i_query(pTransfer, aPath, ptrRoot);
        if (FAILED(hrc))
            return setError(hrc, tr("Querying clipboard transfer list root failed"));
        try
        {
            aNodes.push_back(ptrRoot);
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer list result failed"));
        }
    }
    HRESULT hrc = clipboardTransferListRecursive(pTransfer, aPath, aFlags, aNodes, 0);
    if (FAILED(hrc))
        return setError(hrc, tr("Listing clipboard transfer directory failed"));
    return S_OK;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Opens a transfer directory handle.
 *
 * @returns COM status code.
 * @param   aPath           Transfer-relative directory path.
 * @param   aFlags          ClipboardTransferListFlag mask.
 * @param   aDirectory      Where to return the opened directory.
 */
HRESULT ClipboardTransfer::openDirectory(const com::Utf8Str &aPath,
                                         ULONG aFlags,
                                         ComPtr<IClipboardTransferDirectory> &aDirectory)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aPath, aFlags, aDirectory);
    ReturnComNotImplemented();
#else
    if (aFlags & ~(ClipboardTransferListFlag_NoRecursion | ClipboardTransferListFlag_IncludeRoot | ClipboardTransferListFlag_NoFollowSymlinks))
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer directory-open flags %RU32"), aFlags);
    HRESULT hrc = clipboardTransferValidatePath(aPath, false /* fAllowEmpty */);
    if (FAILED(hrc))
        return setError(hrc, tr("Invalid clipboard transfer directory path"));

    PSHCLTRANSFER const pTransfer = i_getTransfer();
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));

    SHCLLISTHANDLE hList = NIL_SHCLLISTHANDLE;
    hrc = clipboardTransferOpenList(pTransfer, aPath, &hList);
    if (FAILED(hrc))
        return setError(hrc, tr("Opening clipboard transfer directory failed"));

    ComObjPtr<ClipboardTransfer> ptrSelf(this);

    ComObjPtr<ClipboardTransferDirectory> ptrDirectory;
    hrc = ptrDirectory.createObject();
    if (FAILED(hrc))
    {
        ShClTransferListClose(pTransfer, hList);
        return setError(hrc, tr("Creating clipboard transfer directory object failed"));
    }
    hrc = ptrDirectory->init(ptrSelf, pTransfer, aPath, hList);
    if (FAILED(hrc))
    {
        ShClTransferListClose(pTransfer, hList);
        return setError(hrc, tr("Initializing clipboard transfer directory object failed"));
    }
    hrc = ptrDirectory.queryInterfaceTo(aDirectory.asOutParam());
    if (FAILED(hrc))
        return setError(hrc, tr("Querying clipboard transfer directory interface failed"));
    return S_OK;
#endif
}


/**
 * Opens a transfer file handle.
 *
 * @returns COM status code.
 * @param   aPath           Transfer-relative file path.
 * @param   aAccessMode     File access mode.
 * @param   aOpenAction     File open action.
 * @param   aSharingMode    File sharing mode.
 * @param   aCreationMode   File creation mode.
 * @param   aFile           Where to return the opened file.
 */
HRESULT ClipboardTransfer::openFile(const com::Utf8Str &aPath,
                                    FileAccessMode_T aAccessMode,
                                    FileOpenAction_T aOpenAction,
                                    FileSharingMode_T aSharingMode,
                                    ULONG aCreationMode,
                                    ComPtr<IClipboardTransferFile> &aFile)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aPath, aAccessMode, aOpenAction, aSharingMode, aCreationMode, aFile);
    ReturnComNotImplemented();
#else
    if (aAccessMode != FileAccessMode_ReadOnly)
        return setError(E_NOTIMPL, tr("Only read-only clipboard transfer files are currently supported"));
    if (aOpenAction != FileOpenAction_OpenExisting)
        return setError(E_NOTIMPL, tr("Only opening existing clipboard transfer files is currently supported"));
    if (   aSharingMode != FileSharingMode_Read
        && aSharingMode != FileSharingMode_All)
        return setError(E_NOTIMPL, tr("Only read sharing is currently supported for clipboard transfer files"));
    if (aCreationMode)
        return setError(E_INVALIDARG, tr("Clipboard transfer file creation mode must be zero for read-only opens"));

    HRESULT hrc = clipboardTransferValidatePath(aPath, false /* fAllowEmpty */);
    if (FAILED(hrc))
        return setError(hrc, tr("Invalid clipboard transfer file path"));

    PSHCLTRANSFER const pTransfer = i_getTransfer();
    if (!pTransfer)
        return setError(E_NOTIMPL, tr("Clipboard transfer has no data-plane backend"));

    SHCLOBJOPENCREATEPARMS OpenParms;
    int vrc = ShClTransferObjOpenParmsInit(&OpenParms);
    if (RT_FAILURE(vrc))
        return setErrorBoth(clipboardTransferDataPlaneRcToHrc(vrc), vrc,
                            tr("Initializing clipboard transfer file-open parameters failed with %Rrc"), vrc);

    OpenParms.fCreate = SHCL_OBJ_CF_ACCESS_READ | SHCL_OBJ_CF_ACCESS_DENYNONE | SHCL_OBJ_CF_ACCESS_ATTR_READ;
    vrc = RTStrCopy(OpenParms.pszPath, OpenParms.cbPath, aPath.c_str());
    SHCLOBJHANDLE hObj = NIL_SHCLOBJHANDLE;
    if (RT_SUCCESS(vrc))
        vrc = ShClTransferObjOpen(pTransfer, &OpenParms, &hObj);
    SHCLFSOBJINFO Info = OpenParms.ObjInfo;
    ShClTransferObjOpenParmsDestroy(&OpenParms);
    if (RT_FAILURE(vrc))
        return setErrorBoth(clipboardTransferDataPlaneRcToHrc(vrc), vrc,
                            tr("Opening clipboard transfer file failed with %Rrc"), vrc);

    ComPtr<IClipboardTransfer> ptrSelf(this);

    ComObjPtr<ClipboardTransferFile> ptrFile;
    hrc = ptrFile.createObject();
    if (FAILED(hrc))
    {
        ShClTransferObjClose(pTransfer, hObj);
        return setError(hrc, tr("Creating clipboard transfer file object failed"));
    }
    hrc = ptrFile->init(ptrSelf, pTransfer, hObj, aPath, Info, aAccessMode, aOpenAction, aSharingMode, aCreationMode);
    if (FAILED(hrc))
    {
        ShClTransferObjClose(pTransfer, hObj);
        return setError(hrc, tr("Initializing clipboard transfer file object failed"));
    }
    hrc = ptrFile.queryInterfaceTo(aFile.asOutParam());
    if (FAILED(hrc))
        return setError(hrc, tr("Querying clipboard transfer file interface failed"));
    return S_OK;
#endif
}


/**
 * Creates a directory in a writable or virtual transfer tree.
 *
 * @returns COM status code.
 * @param   aPath           Transfer-relative directory path.
 */
HRESULT ClipboardTransfer::createDirectory(const com::Utf8Str &aPath)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aPath);
    ReturnComNotImplemented();
#else
    HRESULT hrc = clipboardTransferValidatePath(aPath, false /* fAllowEmpty */);
    if (FAILED(hrc))
        return setError(hrc, tr("Invalid clipboard transfer directory path"));
    return setError(E_NOTIMPL, tr("Creating clipboard transfer directories is not implemented yet"));
#endif
}
