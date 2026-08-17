/* $Id: ClipboardDataObjectImpl-win.cpp 115055 2026-08-17 16:40:05Z andreas.loeffler@oracle.com $ */
/** @file
 * ClipboardDataObjectImpl-win.cpp - Shared Clipboard IDataObject implementation.
 */

/*
 * Copyright (C) 2019-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/GuestHost/SharedClipboard-win.h>
#include <VBox/GuestHost/SharedClipboard-transfers.h>

#include <iprt/win/windows.h>
#include <iprt/win/shlobj.h>
#include <iprt/win/shlwapi.h>

#include <iprt/thread.h> // REMOVE

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/uri.h>
#include <iprt/utf16.h>

#include <iprt/errcore.h>
#include <VBox/log.h>

/** Enable this to track the current counts of the data / stream / enum object we create + supply to the Windows clipboard.
 *  Helps finding refcount issues or tracking down memory leaks. */
#ifdef VBOX_SHARED_CLIPBOARD_DEBUG_OBJECT_COUNTS
 int g_cDbgDataObj;
 int g_cDbgStreamObj;
 int g_cDbgEnumFmtObj;
#endif

ShClWinDataObject::ShClWinDataObject(void)
    : m_enmStatus(Uninitialized)
    , m_rcStatus(VERR_IPE_UNINITIALIZED_STATUS)
    , m_lRefCount(0)
    , m_cFormats(0)
    , m_pFormatEtc(NULL)
    , m_pStgMedium(NULL)
    , m_pTransfer(NULL)
    , m_uObjIdx(0)
    , m_fCritSectInitialized(false)
    , m_fCallbacksEnabled(false)
    , m_cCallbacks(0)
    , m_hCallbackThread(NIL_RTNATIVETHREAD)
    , m_EventCallbacksDrained(NIL_RTSEMEVENTMULTI)
    , m_EventListComplete(NIL_RTSEMEVENT)
    , m_EventStatusChanged(NIL_RTSEMEVENT)
    , m_cfFileDescriptorA(0)
    , m_cfFileDescriptorW(0)
    , m_cfFileContents(0)
    , m_cfPerformedDropEffect(0)
{
#ifdef VBOX_SHARED_CLIPBOARD_DEBUG_OBJECT_COUNTS
    g_cDbgDataObj++;
    LogFlowFunc(("g_cDataObj=%d, g_cStreamObj=%d, g_cEnumFmtObj=%d\n", g_cDbgDataObj, g_cDbgStreamObj, g_cDbgEnumFmtObj));
#endif
}

ShClWinDataObject::~ShClWinDataObject(void)
{
    Destroy();

#ifdef VBOX_SHARED_CLIPBOARD_DEBUG_OBJECT_COUNTS
    g_cDbgDataObj--;
    LogFlowFunc(("g_cDataObj=%d, g_cStreamObj=%d, g_cEnumFmtObj=%d\n", g_cDbgDataObj, g_cDbgStreamObj, g_cDbgEnumFmtObj));
#endif

    LogFlowFunc(("mRefCount=%RI32\n", m_lRefCount));
}

/**
 * Initializes a data object instance.
 *
 * @returns VBox status code.
 * @param   pCtx                Opaque Shared Clipboard context to use.
 * @param   pCallbacks          Callbacks table to use.
 * @param   pFormatEtc          FormatETC to use. Optional.
 * @param   pStgMed             Storage medium to use. Optional.
 * @param   cFormats            Number of formats in \a pFormatEtc and \a pStgMed. Optional.
 */
int ShClWinDataObject::Init(PSHCLCONTEXT pCtx, ShClWinDataObject::PCALLBACKS pCallbacks,
                            LPFORMATETC pFormatEtc /* = NULL */, LPSTGMEDIUM pStgMed /* = NULL */,
                            ULONG cFormats /* = 0 */)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCallbacks, VERR_INVALID_POINTER);
    AssertReturn(cFormats == 0 || (RT_VALID_PTR(pFormatEtc) && RT_VALID_PTR(pStgMed)), VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    /*
     * Set up callback context + table.
     */
    memcpy(&m_Callbacks, pCallbacks, sizeof(ShClWinDataObject::CALLBACKS));
    m_CallbackCtx.pvUser = pCtx;
    m_CallbackCtx.pThis  = this;

    /*
     * Set up / register handled formats.
     */
    const ULONG cFixedFormats = 5; /* CF_UNICODETEXT + CFSTR_FILEDESCRIPTORA + CFSTR_FILEDESCRIPTORW + CFSTR_FILECONTENTS + CFSTR_PERFORMEDDROPEFFECT */
    const ULONG cAllFormats   = cFormats + cFixedFormats;

    m_pFormatEtc = new FORMATETC[cAllFormats];
    AssertPtrReturn(m_pFormatEtc, VERR_NO_MEMORY);
    RT_BZERO(m_pFormatEtc, sizeof(FORMATETC) * cAllFormats);
    m_pStgMedium = new STGMEDIUM[cAllFormats];
    AssertPtrReturn(m_pStgMedium, VERR_NO_MEMORY);
    RT_BZERO(m_pStgMedium, sizeof(STGMEDIUM) * cAllFormats);

    /** @todo Do we need CFSTR_FILENAME / CFSTR_SHELLIDLIST here? */

    /*
     * Register fixed formats.
     */
    unsigned uIdx = 0;

    LogFlowFunc(("Registering CF_UNICODETEXT ...\n"));
    registerFormat(&m_pFormatEtc[uIdx++], CF_UNICODETEXT);

    LogFlowFunc(("Registering CFSTR_FILEDESCRIPTORA ...\n"));
    m_cfFileDescriptorA = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA);
    registerFormat(&m_pFormatEtc[uIdx++], m_cfFileDescriptorA);
    LogFlowFunc(("Registering CFSTR_FILEDESCRIPTORW ...\n"));
    m_cfFileDescriptorW = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
    registerFormat(&m_pFormatEtc[uIdx++], m_cfFileDescriptorW);

    /* IStream interface, implemented in ClipboardStreamImpl-win.cpp. */
    LogFlowFunc(("Registering CFSTR_FILECONTENTS ...\n"));
    m_cfFileContents = RegisterClipboardFormat(CFSTR_FILECONTENTS);
    registerFormat(&m_pFormatEtc[uIdx++], m_cfFileContents, TYMED_ISTREAM, 0 /* lIndex */);

    /* We want to know from the target what the outcome of the operation was to react accordingly (e.g. abort a transfer). */
    LogFlowFunc(("Registering CFSTR_PERFORMEDDROPEFFECT ...\n"));
    m_cfPerformedDropEffect = RegisterClipboardFormat(CFSTR_PERFORMEDDROPEFFECT);
    registerFormat(&m_pFormatEtc[uIdx++], m_cfPerformedDropEffect, TYMED_HGLOBAL, -1 /* lIndex */, DVASPECT_CONTENT);

    /*
     * Registration of dynamic formats needed?
     */
    LogFlowFunc(("%RU32 dynamic formats\n", cFormats));
    if (cFormats)
    {
        for (ULONG i = 0; i < cFormats; i++)
        {
            LogFlowFunc(("Format %RU32: cfFormat=%RI16, tyMed=%RU32, dwAspect=%RU32\n",
                         i, pFormatEtc[i].cfFormat, pFormatEtc[i].tymed, pFormatEtc[i].dwAspect));
            m_pFormatEtc[cFixedFormats + i] = pFormatEtc[i];
            m_pStgMedium[cFixedFormats + i] = pStgMed[i];
        }
    }

    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectInit(&m_CritSect);
        if (RT_SUCCESS(rc))
        {
            m_fCritSectInitialized = true;
            rc = RTSemEventMultiCreate(&m_EventCallbacksDrained);
            if (RT_SUCCESS(rc))
                rc = RTSemEventCreate(&m_EventListComplete);
            if (RT_SUCCESS(rc))
                rc = RTSemEventCreate(&m_EventStatusChanged);
        }

        if (RT_SUCCESS(rc))
        {
            m_fCallbacksEnabled = true;
            m_cFormats  = cAllFormats;
            m_enmStatus = Initialized;
        }
    }

    LogFlowFunc(("cAllFormats=%RU32, rc=%Rrc\n", cAllFormats, rc));
    return rc;
}

/**
 * Uninitialized a data object instance, internal version.
 */
void ShClWinDataObject::uninitInternal(void)
{
    LogFlowFuncEnter();

    Assert(m_fCritSectInitialized);
    lock();

    if (m_enmStatus != Uninitialized)
    {

        /* Let the read thread know. */
        setStatusLocked(Uninitialized, VINF_SUCCESS);

        /* Make sure to unlock before stopping the read thread. */
        unlock();

        /* Stop the read thread. */
        if (m_pTransfer)
            ShClTransferStop(m_pTransfer);

        lock();
    }

    /* Make sure to release the transfer in any state. */
    ShClWinDataObject *pObjToRelease = NULL;
    setTransferLocked(NULL, &pObjToRelease);

    unlock();

    if (pObjToRelease)
        pObjToRelease->Release();
}

/**
 * Uninitialized a data object instance.
 */
void ShClWinDataObject::Uninit(void)
{
    LogFlowFuncEnter();

    if (m_fCritSectInitialized)
    {
        DisableCallbacks();
        uninitInternal();
        invalidateStreams();
    }

    /* No callback may retain or use its owner after the object is invalidated. */
    m_CallbackCtx.pvUser = NULL;
    m_CallbackCtx.pThis  = this;
}

/**
 * Permanently disables new backend callbacks and waits for an active callback
 * running on another thread to return.
 */
void ShClWinDataObject::DisableCallbacks(void)
{
    if (!m_fCritSectInitialized)
        return;

    for (;;)
    {
        lock();
        m_fCallbacksEnabled = false;
        m_Callbacks.pfnTransferBegin = NULL;
        m_Callbacks.pfnTransferEnd   = NULL;
        bool const fWaitCallbacks = m_cCallbacks != 0;
        bool const fCallbackThread = fWaitCallbacks
                                  && m_hCallbackThread == RTThreadNativeSelf();
        unlock();

        if (   !fWaitCallbacks
            || fCallbackThread)
            break;

        int rc = RTSemEventMultiWait(m_EventCallbacksDrained, RT_INDEFINITE_WAIT);
        AssertFatalMsgRC(rc, ("Draining Windows clipboard data-object callbacks failed with %Rrc\n", rc));
    }
}

/**
 * Destroys a data object instance.
 */
void ShClWinDataObject::Destroy(void)
{
    LogFlowFuncEnter();

    Uninit();

    if (m_fCritSectInitialized)
    {
        int rc = RTCritSectDelete(&m_CritSect);
        AssertRC(rc);
        m_fCritSectInitialized = false;
    }

    m_CallbackCtx.pvUser = NULL;
    m_CallbackCtx.pThis  = this;
    m_cFormats  = 0;
    m_enmStatus = Uninitialized;

    if (m_EventCallbacksDrained != NIL_RTSEMEVENTMULTI)
    {
        int rc = RTSemEventMultiDestroy(m_EventCallbacksDrained);
        AssertRC(rc);
        m_EventCallbacksDrained = NIL_RTSEMEVENTMULTI;
    }

    if (m_EventListComplete != NIL_RTSEMEVENT)
    {
        int rc = RTSemEventDestroy(m_EventListComplete);
        AssertRC(rc);
        m_EventListComplete = NIL_RTSEMEVENT;
    }

    if (m_EventStatusChanged != NIL_RTSEMEVENT)
    {
        int rc = RTSemEventDestroy(m_EventStatusChanged);
        AssertRC(rc);
        m_EventStatusChanged = NIL_RTSEMEVENT;
    }

    if (m_pFormatEtc)
    {
        delete[] m_pFormatEtc;
        m_pFormatEtc = NULL;
    }

    if (m_pStgMedium)
    {
        delete[] m_pStgMedium;
        m_pStgMedium = NULL;
    }

    FsObjEntryList::const_iterator itRoot = m_lstEntries.cbegin();
    while (itRoot != m_lstEntries.end())
    {
        RTStrFree(itRoot->pszPath);
        ++itRoot;
    }
    m_lstEntries.clear();
}


/*********************************************************************************************************************************
 * IUnknown methods.
 ********************************************************************************************************************************/

STDMETHODIMP_(ULONG) ShClWinDataObject::AddRef(void)
{
    ULONG ulCount = InterlockedIncrement(&m_lRefCount);
    LogFlowFunc(("lCount=%RU32\n", ulCount));
    return ulCount;
}

STDMETHODIMP_(ULONG) ShClWinDataObject::Release(void)
{
    ULONG ulCount = InterlockedDecrement(&m_lRefCount);
    LogFlowFunc(("lCount=%RU32\n", ulCount));
    if (ulCount == 0)
    {
        delete this;
        return 0;
    }

    return ulCount;
}

STDMETHODIMP ShClWinDataObject::QueryInterface(REFIID iid, void **ppvObject)
{
    AssertPtrReturn(ppvObject, E_INVALIDARG);

    if (   iid == IID_IDataObject
        || iid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = 0;
    return E_NOINTERFACE;
}

/**
 * Copies a chunk of data into a HGLOBAL object.
 *
 * @returns VBox status code.
 * @param   pvData              Data to copy.
 * @param   cbData              Size (in bytes) to copy.
 * @param   fFlags              GlobalAlloc flags, used for allocating the HGLOBAL block.
 * @param   phGlobal            Where to store the allocated HGLOBAL object.
 */
int ShClWinDataObject::copyToHGlobal(const void *pvData, size_t cbData, UINT fFlags, HGLOBAL *phGlobal)
{
    AssertPtrReturn(phGlobal, VERR_INVALID_POINTER);

    HGLOBAL hGlobal = GlobalAlloc(fFlags, cbData);
    if (!hGlobal)
        return VERR_NO_MEMORY;

    void *pvAlloc = GlobalLock(hGlobal);
    if (pvAlloc)
    {
        CopyMemory(pvAlloc, pvData, cbData);
        GlobalUnlock(hGlobal);

        *phGlobal = hGlobal;

        return VINF_SUCCESS;
    }

    GlobalFree(hGlobal);
    return VERR_ACCESS_DENIED;
}

inline int ShClWinDataObject::lock(void)
{
    int rc = RTCritSectEnter(&m_CritSect);
    AssertRCReturn(rc, rc);

    return rc;
}

inline int ShClWinDataObject::unlock(void)
{
    int rc = RTCritSectLeave(&m_CritSect);
    AssertRCReturn(rc, rc);

    return rc;
}

/**
 * Reads (handles) a specific directory reursively and inserts its entry into the
 * objects's entry list.
 *
 * @returns VBox status code.
 * @param   pTransfer           Shared Clipboard transfer object to handle.
 * @param   strDir              Directory path to handle.
 */
int ShClWinDataObject::readDir(PSHCLTRANSFER pTransfer, const Utf8Str &strDir)
{
    LogFlowFunc(("strDir=%s\n", strDir.c_str()));

    SHCLLISTOPENPARMS openParmsList;
    int rc = ShClTransferListOpenParmsInit(&openParmsList);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrCopy(openParmsList.pszPath, openParmsList.cbPath, strDir.c_str());
        if (RT_SUCCESS(rc))
        {
            SHCLLISTHANDLE hList;
            rc = ShClTransferListOpen(pTransfer, &openParmsList, &hList);
            if (RT_SUCCESS(rc))
            {
                LogFlowFunc(("strDir=%s -> hList=%RU64\n", strDir.c_str(), hList));

                SHCLLISTHDR hdrList;
                rc = ShClTransferListGetHeader(pTransfer, hList, &hdrList);
                if (RT_SUCCESS(rc))
                {
                    LogFlowFunc(("cTotalObjects=%RU64, cbTotalSize=%RU64\n\n",
                                 hdrList.cEntries, hdrList.cbTotalSize));

                    for (uint64_t o = 0; o < hdrList.cEntries; o++)
                    {
                        SHCLLISTENTRY entryList;
                        rc = ShClTransferListEntryInit(&entryList);
                        if (RT_SUCCESS(rc))
                        {
                            rc = ShClTransferListRead(pTransfer, hList, &entryList);
                            if (RT_SUCCESS(rc))
                            {
                                if (ShClTransferListEntryIsValid(&entryList))
                                {
                                    PSHCLFSOBJINFO pFsObjInfo = (PSHCLFSOBJINFO)entryList.pvInfo;
                                    Assert(entryList.cbInfo == sizeof(SHCLFSOBJINFO));

                                    Utf8Str strPath = strDir + Utf8Str("\\") + Utf8Str(entryList.pszName);

                                    LogFlowFunc(("\t%s (%RU64 bytes) -> %s\n",
                                                 entryList.pszName, pFsObjInfo->cbObject, strPath.c_str()));

                                    if (   RTFS_IS_DIRECTORY(pFsObjInfo->Attr.fMode)
                                        || RTFS_IS_FILE     (pFsObjInfo->Attr.fMode))
                                    {
                                        FSOBJENTRY objEntry;
                                        objEntry.pszPath = RTStrDup(strPath.c_str());
                                        AssertPtrBreakStmt(objEntry.pszPath, rc = VERR_NO_MEMORY);
                                        objEntry.objInfo = *pFsObjInfo;

                                        lock();
                                        m_lstEntries.push_back(objEntry); /** @todo Can this throw? */
                                        unlock();
                                    }
                                    else /* Not fatal, just skip. */
                                        LogRel(("Shared Clipboard: Warning: File system object '%s' of type %#x not supported, skipping\n",
                                                strPath.c_str(), pFsObjInfo->Attr.fMode & RTFS_TYPE_MASK));

                                    /** @todo Handle symlinks. */
                                }
                                else
                                    rc = VERR_INVALID_PARAMETER;
                            }

                            ShClTransferListEntryDestroy(&entryList);
                        }

                        if (   RT_FAILURE(rc)
                            && pTransfer->Thread.fStop)
                            break;
                    }
                }

                ShClTransferListClose(pTransfer, hList);
            }
        }

        ShClTransferListOpenParmsDestroy(&openParmsList);
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reading directory '%s' failed with %Rrc\n", strDir.c_str(), rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Thread for reading transfer data.
 * The data object needs the (high level, root) transfer listing at the time of ::GetData(), so we need
 * to block and wait until we have this data (via this thread) and continue.
 *
 * @returns VBox status code.
 * @param   pTransfer           Pointer to transfer.
 * @param   pvUser              Pointer to user-provided data. Of type ShClWinDataObject.
 */
/* static */
DECLCALLBACK(int) ShClWinDataObject::readThread(PSHCLTRANSFER pTransfer, void *pvUser)
{
    LogFlowFuncEnter();

    ShClWinDataObject *pThis = (ShClWinDataObject *)pvUser;

    LogRel2(("Shared Clipboard: Calculating transfer ...\n"));

    int rc = ShClTransferRootListRead(pTransfer);
    if (RT_SUCCESS(rc))
    {
        uint64_t const cRoots = ShClTransferRootsCount(pTransfer);

        LogFlowFunc(("cRoots=%RU64\n\n", cRoots));

        for (uint32_t i = 0; i < cRoots; i++)
        {
            PCSHCLLISTENTRY pRootEntry = ShClTransferRootsEntryGet(pTransfer, i);
            AssertPtrBreakStmt(pRootEntry, rc = VERR_INVALID_POINTER);

            AssertBreakStmt(pRootEntry->cbInfo == sizeof(SHCLFSOBJINFO), rc = VERR_INVALID_PARAMETER);
            PSHCLFSOBJINFO const pFsObjInfo = (PSHCLFSOBJINFO)pRootEntry->pvInfo;

            LogFlowFunc(("pszRoot=%s, fMode=0x%x (type %#x)\n",
                         pRootEntry->pszName, pFsObjInfo->Attr.fMode, (pFsObjInfo->Attr.fMode & RTFS_TYPE_MASK)));

            if (RTFS_IS_DIRECTORY(pFsObjInfo->Attr.fMode))
            {
                FSOBJENTRY objEntry;
                objEntry.pszPath = RTStrDup(pRootEntry->pszName);
                AssertPtrBreakStmt(objEntry.pszPath, rc = VERR_NO_MEMORY);
                objEntry.objInfo = *pFsObjInfo;

                pThis->lock();
                pThis->m_lstEntries.push_back(objEntry); /** @todo Can this throw? */
                pThis->unlock();

                rc = pThis->readDir(pTransfer, pRootEntry->pszName);
            }
            else if (RTFS_IS_FILE(pFsObjInfo->Attr.fMode))
            {
                FSOBJENTRY objEntry;
                objEntry.pszPath = RTStrDup(pRootEntry->pszName);
                AssertPtrBreakStmt(objEntry.pszPath, rc = VERR_NO_MEMORY);
                objEntry.objInfo = *pFsObjInfo;

                pThis->lock();
                pThis->m_lstEntries.push_back(objEntry); /** @todo Can this throw? */
                pThis->unlock();
            }
            else
            {
                LogRel(("Shared Clipboard: Root entry '%s': File type %#x not supported\n",
                        pRootEntry->pszName, (pFsObjInfo->Attr.fMode & RTFS_TYPE_MASK)));
                rc = VERR_NOT_SUPPORTED;
            }

            if (ASMAtomicReadBool(&pTransfer->Thread.fStop))
            {
                LogRel2(("Shared Clipboard: Stopping transfer calculation ...\n"));
                break;
            }

            if (RT_FAILURE(rc))
                break;
        }

        if (   RT_SUCCESS(rc)
            && !ASMAtomicReadBool(&pTransfer->Thread.fStop))
        {
            LogRel2(("Shared Clipboard: Transfer calculation complete (%zu root entries)\n", pThis->m_lstEntries.size()));

            /*
             * Signal the "list complete" event so that this data object can return (valid) data via ::GetData().
             * This in turn then will create IStream instances (by the OS) for each file system object to handle.
             */
            rc = RTSemEventSignal(pThis->m_EventListComplete);
            if (RT_SUCCESS(rc))
            {
                AssertReleaseMsg(pThis->m_lstEntries.size(),
                                 ("Shared Clipboard: No transfer root entries found -- should not happen, please file a bug report\n"));

                LogRel2(("Shared Clipboard: Waiting for transfer to complete ...\n"));

                for (;;)
                {
                    if (ASMAtomicReadBool(&pTransfer->Thread.fStop))
                        break;

                    /* Transferring stuff can take a while, so don't use any timeout here. */
                    rc = RTSemEventWait(pThis->m_EventStatusChanged, RT_INDEFINITE_WAIT);
                    if (RT_FAILURE(rc))
                        break;

                    pThis->lock();
                    Status const enmStatus = pThis->m_enmStatus;
                    int const rcStatus = pThis->m_rcStatus;
                    pThis->unlock();

                    switch (enmStatus)
                    {
                        case Uninitialized: /* Can happen due to transfer erros. */
                            LogRel2(("Shared Clipboard: Data object was uninitialized\n"));
                            break;

                        case Initialized:
                            AssertFailed(); /* State machine error -- debug this! */
                            break;

                        case Running:
                            continue;

                        case Completed:
                            LogRel2(("Shared Clipboard: Data object: Transfer complete\n"));
                            rc = ShClTransferComplete(pTransfer);
                            break;

                        case Canceled:
                            LogRel2(("Shared Clipboard: Data object: Transfer canceled\n"));
                            rc = ShClTransferCancel(pTransfer);
                            break;

                        case Error:
                            LogRel(("Shared Clipboard: Data object: Transfer error %Rrc occurred\n", rcStatus));
                            rc = ShClTransferError(pTransfer, rcStatus);
                            break;

                        default:
                            AssertFailed();
                            break;
                    }

                    pThis->lock();
                    PFNTRANSFEREND const pfnTransferEnd = pThis->m_fCallbacksEnabled
                                                       ? pThis->m_Callbacks.pfnTransferEnd : NULL;
                    CALLBACKCTX CallbackCtx = pThis->m_CallbackCtx;
                    if (pfnTransferEnd)
                    {
                        Assert(pThis->m_cCallbacks == 0);
                        pThis->m_cCallbacks = 1;
                        pThis->m_hCallbackThread = RTThreadNativeSelf();
                        int rc2 = RTSemEventMultiReset(pThis->m_EventCallbacksDrained);
                        AssertFatalMsgRC(rc2, ("Resetting the Windows clipboard callback-drain event failed with %Rrc\n", rc2));
                    }
                    pThis->unlock();

                    if (pfnTransferEnd)
                    {
                        int rc2 = pfnTransferEnd(&CallbackCtx, pTransfer, rcStatus);
                        if (RT_SUCCESS(rc))
                            rc = rc2;

                        pThis->lock();
                        Assert(pThis->m_cCallbacks == 1);
                        pThis->m_cCallbacks = 0;
                        pThis->m_hCallbackThread = NIL_RTNATIVETHREAD;
                        rc2 = RTSemEventMultiSignal(pThis->m_EventCallbacksDrained);
                        AssertFatalMsgRC(rc2, ("Signalling the Windows clipboard callback-drain event failed with %Rrc\n", rc2));
                        pThis->unlock();
                    }

                    break;
                } /* for */
            }
        }
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Transfer read thread failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    pThis->Release();
    return rc;
}

/**
 * Creates a FILEGROUPDESCRIPTOR[A|W] object from a given Shared Clipboard transfer and stores the result into an HGLOBAL object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Shared Clipboard transfer to create file grou desciprtor for.
 * @param   fUnicode            Whether the FILEGROUPDESCRIPTOR object shall contain Unicode data or not.
 * @param   phGlobal            Where to store the allocated HGLOBAL object on success.
 */
int ShClWinDataObject::createFileGroupDescriptorFromTransfer(PSHCLTRANSFER pTransfer, bool fUnicode, HGLOBAL *phGlobal)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(phGlobal,  VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    const UINT cItems = (UINT)m_lstEntries.size();
    if (!cItems)
        return VERR_NOT_FOUND;

    int rc = VINF_SUCCESS;

    if (fUnicode)
    {
        const size_t cbFGD = sizeof(FILEGROUPDESCRIPTORW)
                           + sizeof(FILEDESCRIPTORW) * (cItems - 1);

        FILEGROUPDESCRIPTORW *pFGD = (FILEGROUPDESCRIPTORW *)RTMemAllocZ(cbFGD);
        if (!pFGD)
            return VERR_NO_MEMORY;

        pFGD->cItems = cItems;

        UINT curIdx = 0;
        FsObjEntryList::const_iterator itRoot = m_lstEntries.cbegin();
        while (itRoot != m_lstEntries.end())
        {
            FILEDESCRIPTORW *pFD = &pFGD->fgd[curIdx];
            RT_BZERO(pFD, sizeof(*pFD));

            const char *pszFile = itRoot->pszPath;
            AssertPtrBreakStmt(pszFile, rc = VERR_INVALID_POINTER);

            PRTUTF16 pwszFileSpec = NULL;
            rc = RTStrToUtf16(pszFile, &pwszFileSpec);
            if (RT_FAILURE(rc))
                break;

            rc = RTUtf16CopyEx((PRTUTF16)pFD->cFileName,
                                RT_ELEMENTS(pFD->cFileName),
                                pwszFileSpec,
                                RTUtf16Len(pwszFileSpec));

            RTUtf16Free(pwszFileSpec);

            if (RT_FAILURE(rc))
                break;

            LogFlowFunc(("pFD->cFileNameW=%ls\n", pFD->cFileName));

            pFD->dwFlags          = FD_PROGRESSUI | FD_ATTRIBUTES;
            pFD->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

#ifdef FD_UNICODE
            pFD->dwFlags |= FD_UNICODE;
#endif
            const SHCLFSOBJINFO *pObjInfo = &itRoot->objInfo;

            if (RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
                pFD->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            else if (RTFS_IS_FILE(pObjInfo->Attr.fMode))
            {
                pFD->dwFlags |= FD_FILESIZE;

                const uint64_t cbObjSize = pObjInfo->cbObject;
                pFD->nFileSizeHigh = RT_HI_U32(cbObjSize);
                pFD->nFileSizeLow  = RT_LO_U32(cbObjSize);
            }
            else if (RTFS_IS_SYMLINK(pObjInfo->Attr.fMode))
            {
                /** @todo Implement symlink support. */
            }

            ++curIdx;
            ++itRoot;
        }

        if (RT_SUCCESS(rc))
            rc = copyToHGlobal(pFGD, cbFGD, GMEM_MOVEABLE, phGlobal);

        RTMemFree(pFGD);
    }
    else
    {
        const size_t cbFGD = sizeof(FILEGROUPDESCRIPTORA)
                           + sizeof(FILEDESCRIPTORA) * (cItems - 1);

        FILEGROUPDESCRIPTORA *pFGD = (FILEGROUPDESCRIPTORA *)RTMemAllocZ(cbFGD);
        if (!pFGD)
            return VERR_NO_MEMORY;

        pFGD->cItems = cItems;

        UINT curIdx = 0;
        FsObjEntryList::const_iterator itRoot = m_lstEntries.cbegin();
        while (itRoot != m_lstEntries.end())
        {
            FILEDESCRIPTORA *pFD = &pFGD->fgd[curIdx];
            RT_BZERO(pFD, sizeof(*pFD));

            const char *pszFile = itRoot->pszPath;
            AssertPtrBreakStmt(pszFile, rc = VERR_INVALID_POINTER);

            /*
             * A fallback path only.  This preserves current behavior by copying
             * the internal UTF-8 bytes into the A descriptor.  Unicode filename
             * correctness belongs to FILEDESCRIPTORW.
             */
            rc = RTStrCopy(pFD->cFileName, sizeof(pFD->cFileName), pszFile);
            if (RT_FAILURE(rc))
                break;

            LogFlowFunc(("pFD->cFileNameA=%s\n", pFD->cFileName));

            pFD->dwFlags          = FD_PROGRESSUI | FD_ATTRIBUTES;
            pFD->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

            const SHCLFSOBJINFO *pObjInfo = &itRoot->objInfo;

            if (RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
                pFD->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            else if (RTFS_IS_FILE(pObjInfo->Attr.fMode))
            {
                pFD->dwFlags |= FD_FILESIZE;

                const uint64_t cbObjSize = pObjInfo->cbObject;
                pFD->nFileSizeHigh = RT_HI_U32(cbObjSize);
                pFD->nFileSizeLow  = RT_LO_U32(cbObjSize);
            }
            else if (RTFS_IS_SYMLINK(pObjInfo->Attr.fMode))
            {
                /** @todo Implement symlink support. */
            }

            ++curIdx;
            ++itRoot;
        }

        if (RT_SUCCESS(rc))
            rc = copyToHGlobal(pFGD, cbFGD, GMEM_MOVEABLE, phGlobal);

        RTMemFree(pFGD);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a CF_UNICODETEXT object from the transfer root entries.
 *
 * @returns VBox status code.
 * @param   pTransfer           Shared Clipboard transfer to create text for.
 * @param   phGlobal            Where to store the allocated HGLOBAL object on success.
 */
int ShClWinDataObject::createUnicodeTextFromTransferRoots(PSHCLTRANSFER pTransfer, HGLOBAL *phGlobal)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(phGlobal,  VERR_INVALID_POINTER);

    *phGlobal = NULL;

    uint64_t const cRoots = ShClTransferRootsCount(pTransfer);
    if (!cRoots)
    {
        LogRelMax(16, ("Shared Clipboard: Cannot provide CF_UNICODETEXT for file transfer because the transfer has no root entries\n"));
        return VERR_NOT_FOUND;
    }

    size_t cwcText = 1; /* Terminator. */
    int rc = VINF_SUCCESS;
    for (uint64_t i = 0; i < cRoots; i++)
    {
        PCSHCLLISTENTRY pRootEntry = ShClTransferRootsEntryGet(pTransfer, i);
        AssertPtrBreakStmt(pRootEntry, rc = VERR_INVALID_POINTER);
        AssertPtrBreakStmt(pRootEntry->pszName, rc = VERR_INVALID_POINTER);

        size_t cwcRoot = 0;
        rc = RTStrCalcUtf16LenEx(pRootEntry->pszName, RTSTR_MAX, &cwcRoot);
        if (RT_FAILURE(rc))
        {
            LogRelMax(16, ("Shared Clipboard: Cannot convert transfer root '%s' to UTF-16 length for CF_UNICODETEXT, rc=%Rrc\n",
                            pRootEntry->pszName, rc));
            break;
        }

        size_t const cwcAdd = cwcRoot + (i + 1 < cRoots ? 2 : 0);
        if (   cwcAdd < cwcRoot
            || cwcText > SIZE_MAX - cwcAdd)
        {
            LogRelMax(16, ("Shared Clipboard: Transfer root text is too large to expose as CF_UNICODETEXT\n"));
            rc = VERR_TOO_MUCH_DATA;
            break;
        }

        cwcText += cwcAdd;
    }

    if (RT_FAILURE(rc))
        return rc;

    if (cwcText > SIZE_MAX / sizeof(RTUTF16))
    {
        LogRelMax(16, ("Shared Clipboard: Transfer root text is too large to allocate as CF_UNICODETEXT (%zu UTF-16 code units)\n",
                       cwcText));
        return VERR_TOO_MUCH_DATA;
    }

    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, cwcText * sizeof(RTUTF16));
    if (!hGlobal)
    {
        LogRelMax(16, ("Shared Clipboard: Allocating %zu bytes for CF_UNICODETEXT failed\n", cwcText * sizeof(RTUTF16)));
        return VERR_NO_MEMORY;
    }

    PRTUTF16 pwszText = (PRTUTF16)GlobalLock(hGlobal);
    if (!pwszText)
    {
        GlobalFree(hGlobal);
        return VERR_ACCESS_DENIED;
    }

    PRTUTF16 pwszDst = pwszText;
    size_t cwcLeft = cwcText;
    for (uint64_t i = 0; i < cRoots; i++)
    {
        PCSHCLLISTENTRY pRootEntry = ShClTransferRootsEntryGet(pTransfer, i);
        AssertPtrBreakStmt(pRootEntry, rc = VERR_INVALID_POINTER);
        AssertPtrBreakStmt(pRootEntry->pszName, rc = VERR_INVALID_POINTER);

        size_t cwcWritten = 0;
        rc = RTStrToUtf16Ex(pRootEntry->pszName, RTSTR_MAX, &pwszDst, cwcLeft, &cwcWritten);
        if (RT_FAILURE(rc))
        {
            LogRelMax(16, ("Shared Clipboard: Converting transfer root '%s' to CF_UNICODETEXT failed with %Rrc\n",
                           pRootEntry->pszName, rc));
            break;
        }

        pwszDst += cwcWritten;
        cwcLeft -= cwcWritten;

        if (i + 1 < cRoots)
        {
            AssertBreakStmt(cwcLeft >= 3, rc = VERR_BUFFER_OVERFLOW);
            *pwszDst++ = '\r';
            *pwszDst++ = '\n';
            cwcLeft -= 2;
        }
    }

    if (RT_SUCCESS(rc))
    {
        Assert(cwcLeft >= 1);
        *pwszDst = '\0';
    }

    GlobalUnlock(hGlobal);

    if (RT_SUCCESS(rc))
    {
        LogRelMax2(16, ("Shared Clipboard: Providing CF_UNICODETEXT with %RU64 transfer root entries (%zu bytes)\n",
                        cRoots, cwcText * sizeof(RTUTF16)));
        *phGlobal = hGlobal;
    }
    else
        GlobalFree(hGlobal);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Ensures that the transfer object has been started and its root listing has been read.
 *
 * @returns VBox status code.
 * @note    Caller must have taken the critical section; returns with it held.
 */
int ShClWinDataObject::ensureTransferListReadyLocked(void)
{
    AssertReturn(RTCritSectIsOwned(&m_CritSect), VERR_WRONG_ORDER);

    int rc = VINF_SUCCESS;

    if (m_enmStatus == Initialized)
    {
        LogRel2(("Shared Clipboard: Requesting data for IDataObject ...\n"));

        if (   !m_fCallbacksEnabled
            || !m_Callbacks.pfnTransferBegin)
        {
            LogRelMax(16, ("Shared Clipboard: Cannot start IDataObject transfer because no transfer-begin callback is installed\n"));
            return VERR_INVALID_POINTER;
        }
        if (m_cCallbacks != 0)
            return VERR_TRY_AGAIN;

        PFNTRANSFERBEGIN const pfnTransferBegin = m_Callbacks.pfnTransferBegin;
        CALLBACKCTX CallbackCtx = m_CallbackCtx;
        m_cCallbacks = 1;
        m_hCallbackThread = RTThreadNativeSelf();
        rc = RTSemEventMultiReset(m_EventCallbacksDrained);
        AssertFatalMsgRC(rc, ("Resetting the Windows clipboard callback-drain event failed with %Rrc\n", rc));

        /* Leave lock while requesting + waiting. */
        unlock();

        rc = pfnTransferBegin(&CallbackCtx);

        lock();
        Assert(m_cCallbacks > 0);
        if (--m_cCallbacks == 0)
        {
            m_hCallbackThread = NIL_RTNATIVETHREAD;
            int rc2 = RTSemEventMultiSignal(m_EventCallbacksDrained);
            AssertFatalMsgRC(rc2, ("Signalling the Windows clipboard callback-drain event failed with %Rrc\n", rc2));
        }
        unlock();

        if (RT_SUCCESS(rc))
        {
            LogRel2(("Shared Clipboard: Waiting for IDataObject started status ...\n"));

            /* Note: Keep the timeout low here (instead of using SHCL_TIMEOUT_DEFAULT_MS), as this will make
             *       Windows Explorer unresponsive (i.e. "ghost window") when waiting for too long. */
            rc = RTSemEventWait(m_EventStatusChanged, RT_MS_10SEC);
        }

        /* Re-acquire lock. */
        lock();

        LogFunc(("Wait resulted in %Rrc and status %#x\n", rc, m_enmStatus));

        if (RT_FAILURE(rc))
        {
            LogRelMax(16, ("Shared Clipboard: Waiting for IDataObject transfer to start failed, rc=%Rrc\n", rc));
            return rc;
        }

        if (m_enmStatus != Running)
        {
            LogRelMax(16, ("Shared Clipboard: IDataObject transfer did not enter running state (status=%#x)\n", m_enmStatus));
            return VERR_WRONG_ORDER;
        }
    }

    if (m_enmStatus != Running)
        return VERR_STATE_CHANGED;

    /* There now must be a transfer assigned. */
    AssertPtrReturn(m_pTransfer, VERR_WRONG_ORDER);

    PSHCLTRANSFER pTransfer = m_pTransfer;
    ShClTransferAcquire(pTransfer);

    bool const fHaveEntries = !m_lstEntries.empty();

    /* Leave lock while starting + waiting. */
    unlock();

    SHCLTRANSFERSTATUS const enmTransferStatus = ShClTransferGetStatus(pTransfer);

    LogFlowFunc(("enmTransferStatus=%s\n", ShClTransferStatusToStr(enmTransferStatus)));

    /* The caller can call GetData() several times, so make sure we don't do the same transfer multiple times. */
    bool fNeedListWait = !fHaveEntries;
    if (enmTransferStatus != SHCLTRANSFERSTATUS_STARTED)
    {
        /* Start the transfer + run it asynchronously in a separate thread. */
        rc = ShClTransferStart(pTransfer);
        if (RT_SUCCESS(rc))
        {
            /* The transfer worker receives a raw user pointer, so retain the
             * object until readThread() returns. */
            AddRef();
            rc = ShClTransferRun(pTransfer, &ShClWinDataObject::readThread, this /* pvUser */);
            if (RT_SUCCESS(rc))
                fNeedListWait = true;
            else
            {
                Release();
                LogRelMax(16, ("Shared Clipboard: Starting IDataObject transfer read thread failed with %Rrc\n", rc));
            }
        }
        else
            LogRelMax(16, ("Shared Clipboard: Starting IDataObject transfer failed with %Rrc\n", rc));
    }

    if (   RT_SUCCESS(rc)
        && fNeedListWait)
    {
        /* Don't block for too long here, as this also will screw other apps running on the OS. */
        LogRel2(("Shared Clipboard: Waiting for IDataObject listing to arrive ...\n"));
        rc = RTSemEventWait(m_EventListComplete, RT_MS_10SEC);
        if (RT_FAILURE(rc))
            LogRelMax(16, ("Shared Clipboard: Timed out or failed waiting for IDataObject transfer listing, rc=%Rrc\n", rc));
    }

    ShClTransferRelease(pTransfer);

    /* Re-acquire lock. */
    lock();

    if (   RT_SUCCESS(rc)
        && (   m_pTransfer == NULL
            || m_enmStatus != Running
            || m_lstEntries.empty())) /* Still in running state and with a listing? */
    {
        LogRelMax(16, ("Shared Clipboard: IDataObject transfer listing is unavailable after wait (transfer=%p, status=%#x, entries=%zu)\n",
                       m_pTransfer, m_enmStatus, m_lstEntries.size()));
        rc = VERR_SHCLPB_NO_DATA;
    }

    return rc;
}

/**
 * Retrieves the data stored in this object and store the result in pMedium.
 *
 * @return  HRESULT
 * @param   pFormatEtc          Format to retrieve.
 * @param   pMedium             Where to store the data on success.
 *
 * @thread  Windows event thread.
 */
STDMETHODIMP ShClWinDataObject::GetData(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium)
{
    AssertPtrReturn(pFormatEtc, DV_E_FORMATETC);
    AssertPtrReturn(pMedium, DV_E_FORMATETC);

    lock();

    LogFlowFunc(("lIndex=%RI32, enmStatus=%#x\n", pFormatEtc->lindex, m_enmStatus));

    /* If the object is not ready (anymore), bail out early. */
    if (   m_enmStatus != Initialized
        && m_enmStatus != Running)
    {
        unlock();
        return E_UNEXPECTED;
    }

    /*
     * Initialize default values.
     */
    RT_BZERO(pMedium, sizeof(STGMEDIUM));

    HRESULT hr = DV_E_FORMATETC; /* Play safe. */

    int rc = VINF_SUCCESS;

    if (   pFormatEtc->cfFormat == CF_UNICODETEXT
        || pFormatEtc->cfFormat == m_cfFileDescriptorA
        || pFormatEtc->cfFormat == m_cfFileDescriptorW)
    {
        rc = ensureTransferListReadyLocked();
        if (RT_SUCCESS(rc))
        {
            HGLOBAL hGlobal = NULL;
            if (pFormatEtc->cfFormat == CF_UNICODETEXT)
                rc = createUnicodeTextFromTransferRoots(m_pTransfer, &hGlobal);
            else
            {
                bool const fUnicode = pFormatEtc->cfFormat == m_cfFileDescriptorW;
                LogFlowFunc(("FormatIndex_FileDescriptor%s\n", fUnicode ? "W" : "A"));
                rc = createFileGroupDescriptorFromTransfer(m_pTransfer, fUnicode, &hGlobal);
            }

            if (RT_SUCCESS(rc))
            {
                pMedium->tymed   = TYMED_HGLOBAL;
                pMedium->hGlobal = hGlobal;
                /* Note: hGlobal now is being owned by pMedium / the caller. */

                hr = S_OK;
            }
        }

        if (RT_FAILURE(rc))
        {
            LogRelMax(16, ("Shared Clipboard: Error getting data format %#x from IDataObject, rc=%Rrc\n",
                           pFormatEtc->cfFormat, rc));
            hr = E_UNEXPECTED; /* We can't tell any better to the caller, unfortunately. */
        }
    }

    Log2Func(("enmStatus=%#x, pTransfer=%p, rc=%Rrc\n", m_enmStatus, m_pTransfer, rc));

    if (RT_SUCCESS(rc))
    {
        if (pFormatEtc->cfFormat == m_cfFileContents)
        {
            if (          pFormatEtc->lindex >= 0
                && (ULONG)pFormatEtc->lindex <  m_lstEntries.size())
            {
                m_uObjIdx = pFormatEtc->lindex; /* lIndex of FormatEtc contains the actual index to the object being handled. */

                FSOBJENTRY &fsObjEntry = m_lstEntries.at(m_uObjIdx);

                LogFlowFunc(("FormatIndex_FileContents: m_uObjIdx=%u (entry '%s')\n", m_uObjIdx, fsObjEntry.pszPath));

                LogRel2(("Shared Clipboard: Receiving object '%s' ...\n", fsObjEntry.pszPath));

                /* Hand-in the provider so that our IStream implementation can continue working with it. */
                IStream *pStream = NULL;
                hr = ShClWinStreamImpl::Create(this /* pParent */, m_pTransfer,
                                               fsObjEntry.pszPath /* File name */, &fsObjEntry.objInfo /* PSHCLFSOBJINFO */,
                                               &pStream);
                if (SUCCEEDED(hr))
                {
                    /* Hand over the stream to the caller. */
                    pMedium->tymed = TYMED_ISTREAM;
                    pMedium->pstm  = pStream;

                    registerStreamLocked((ShClWinStreamImpl *)pStream);
                }
            }
        }
        else if (pFormatEtc->cfFormat == m_cfPerformedDropEffect)
        {
            HGLOBAL hGlobal = GlobalAlloc(GHND, sizeof(DWORD));

            DWORD* pdwDropEffect = (DWORD*)GlobalLock(hGlobal);
            *pdwDropEffect = DROPEFFECT_COPY;

            GlobalUnlock(hGlobal);

            pMedium->tymed          = TYMED_HGLOBAL;
            pMedium->hGlobal        = hGlobal;
            pMedium->pUnkForRelease = NULL;
        }

        if (   FAILED(hr)
            && hr != DV_E_FORMATETC) /* Can happen if the caller queries unknown / unhandled formats. */
        {
            LogRel(("Shared Clipboard: Error returning data from data object (%Rhrc)\n", hr));
        }
    }

    unlock();

    LogFlowFunc(("LEAVE hr=%Rhrc\n", hr));
    return hr;
}

/**
 * Only required for IStream / IStorage interfaces.
 *
 * @return  IPRT status code.
 * @return  HRESULT
 * @param   pFormatEtc
 * @param   pMedium
 */
STDMETHODIMP ShClWinDataObject::GetDataHere(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium)
{
    RT_NOREF(pFormatEtc, pMedium);
    LogFlowFunc(("\n"));
    return E_NOTIMPL;
}

/**
 * Query if this objects supports a specific format.
 *
 * @return  IPRT status code.
 * @return  HRESULT
 * @param   pFormatEtc
 */
STDMETHODIMP ShClWinDataObject::QueryGetData(LPFORMATETC pFormatEtc)
{
    LogFlowFunc(("\n"));
    return lookupFormatEtc(pFormatEtc, NULL /* puIndex */) ? S_OK : DV_E_FORMATETC;
}

STDMETHODIMP ShClWinDataObject::GetCanonicalFormatEtc(LPFORMATETC pFormatEtc, LPFORMATETC pFormatEtcOut)
{
    RT_NOREF(pFormatEtc);
    LogFlowFunc(("\n"));

    /* Set this to NULL in any case. */
    pFormatEtcOut->ptd = NULL;
    return E_NOTIMPL;
}

STDMETHODIMP ShClWinDataObject::SetData(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium, BOOL fRelease)
{
    if (   pFormatEtc == NULL
        || pMedium    == NULL)
        return E_INVALIDARG;

    if (pFormatEtc->lindex != -1)
        return DV_E_LINDEX;

    if (pFormatEtc->tymed != TYMED_HGLOBAL)
        return DV_E_TYMED;

    if (pFormatEtc->dwAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;

    LogFlowFunc(("cfFormat=%RU16, lookupFormatEtc=%RTbool\n",
                 pFormatEtc->cfFormat, lookupFormatEtc(pFormatEtc, NULL /* puIndex */)));

    /* CFSTR_PERFORMEDDROPEFFECT is used by the drop target (caller of this IDataObject) to communicate
     * the outcome of the overall operation. */
    if (   pFormatEtc->cfFormat == m_cfPerformedDropEffect
        && pMedium->tymed       == TYMED_HGLOBAL)
    {
        DWORD dwEffect = *(DWORD *)GlobalLock(pMedium->hGlobal);
        GlobalUnlock(pMedium->hGlobal);

        LogFlowFunc(("dwEffect=%RI32\n", dwEffect));

        /* Did the user cancel the operation via UI (shell)? This also might happen when overwriting an existing file
         * and the user doesn't want to allow this. */
        if (dwEffect == DROPEFFECT_NONE)
        {
            LogRel2(("Shared Clipboard: Transfer canceled by user interaction\n"));

            SetStatus(Canceled);
        }
        /** @todo Detect move / overwrite actions here. */

        if (fRelease)
            ReleaseStgMedium(pMedium);

        return S_OK;
    }

    return E_NOTIMPL;
}

STDMETHODIMP ShClWinDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppEnumFormatEtc)
{
    LogFlowFunc(("dwDirection=%RI32, mcFormats=%RI32, mpFormatEtc=%p\n", dwDirection, m_cFormats, m_pFormatEtc));

    HRESULT hr;
    if (dwDirection == DATADIR_GET)
        hr = ShClWinEnumFormatEtc::CreateEnumFormatEtc(m_cFormats, m_pFormatEtc, ppEnumFormatEtc);
    else
        hr = E_NOTIMPL;

    LogFlowFunc(("hr=%Rhrc\n", hr));
    return hr;
}

STDMETHODIMP ShClWinDataObject::DAdvise(LPFORMATETC pFormatEtc, DWORD fAdvise, IAdviseSink *pAdvSink, DWORD *pdwConnection)
{
    RT_NOREF(pFormatEtc, fAdvise, pAdvSink, pdwConnection);
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP ShClWinDataObject::DUnadvise(DWORD dwConnection)
{
    RT_NOREF(dwConnection);
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP ShClWinDataObject::EnumDAdvise(IEnumSTATDATA **ppEnumAdvise)
{
    RT_NOREF(ppEnumAdvise);
    return OLE_E_ADVISENOTSUPPORTED;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_WIN_ASYNC
/*
 * IDataObjectAsyncCapability methods.
 */

STDMETHODIMP ShClWinDataObject::EndOperation(HRESULT hResult, IBindCtx *pbcReserved, DWORD dwEffects)
{
     RT_NOREF(hResult, pbcReserved, dwEffects);
     return E_NOTIMPL;
}

STDMETHODIMP ShClWinDataObject::GetAsyncMode(BOOL *pfIsOpAsync)
{
     RT_NOREF(pfIsOpAsync);
     return E_NOTIMPL;
}

STDMETHODIMP ShClWinDataObject::InOperation(BOOL *pfInAsyncOp)
{
     RT_NOREF(pfInAsyncOp);
     return E_NOTIMPL;
}

STDMETHODIMP ShClWinDataObject::SetAsyncMode(BOOL fDoOpAsync)
{
     RT_NOREF(fDoOpAsync);
     return E_NOTIMPL;
}

STDMETHODIMP ShClWinDataObject::StartOperation(IBindCtx *pbcReserved)
{
     RT_NOREF(pbcReserved);
     return E_NOTIMPL;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_WIN_ASYNC */

/*
 * Own stuff.
 */

/**
 * Assigns a transfer object for the data object, internal version.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to assign.
 *                              When set to NULL, the transfer will be released from the object.
 */
int ShClWinDataObject::setTransferLocked(PSHCLTRANSFER pTransfer, ShClWinDataObject **ppObjToRelease /* = NULL */)
{
    AssertReturn(RTCritSectIsOwned(&m_CritSect), VERR_WRONG_ORDER);
    AssertReturn(!ppObjToRelease || !*ppObjToRelease, VERR_INVALID_PARAMETER);

    LogFunc(("pTransfer=%p\n", pTransfer));

    int rc = VINF_SUCCESS;

    if (pTransfer) /* Set */
    {
        Assert(m_pTransfer == NULL); /* Transfer already set? */

        if (m_enmStatus == Initialized)
        {
            ShClWinTransferCtx *pWinURITransferCtx = (ShClWinTransferCtx *)pTransfer->pvUser;
            AssertPtr(pWinURITransferCtx);

            rc = RTCritSectEnter(&pWinURITransferCtx->CritSect);
            if (RT_SUCCESS(rc))
            {
                Assert(pWinURITransferCtx->pDataObj == NULL);
                if (!pWinURITransferCtx->pDataObj)
                {
                    AddRef();
                    pWinURITransferCtx->pDataObj = this;
                    m_pTransfer = pTransfer;
                    ShClTransferAcquire(pTransfer);
                }
                else
                    rc = VERR_WRONG_ORDER;

                int rc2 = RTCritSectLeave(&pWinURITransferCtx->CritSect);
                AssertRC(rc2);
            }
        }
        else
            AssertFailedStmt(rc = VERR_WRONG_ORDER);
    }
    else /* Unset */
    {
        if (m_pTransfer)
        {
            ShClWinTransferCtx *pWinURITransferCtx = (ShClWinTransferCtx *)m_pTransfer->pvUser;
            AssertPtr(pWinURITransferCtx);

            int rc2 = RTCritSectEnter(&pWinURITransferCtx->CritSect);
            AssertFatalMsgRC(rc2, ("Taking the Windows transfer-context lock while detaching failed with %Rrc\n", rc2));

            if (pWinURITransferCtx->pDataObj == this)
            {
                pWinURITransferCtx->pDataObj = NULL;
                if (ppObjToRelease)
                    *ppObjToRelease = this;
                else
                    AssertFailed();
            }
            else
                Assert(pWinURITransferCtx->pDataObj == NULL);

            rc2 = RTCritSectLeave(&pWinURITransferCtx->CritSect);
            AssertFatalMsgRC(rc2, ("Releasing the Windows transfer-context lock while detaching failed with %Rrc\n", rc2));

            ShClTransferRelease(m_pTransfer);
            m_pTransfer = NULL;

            /* Make sure to notify any waiters. */
            rc = RTSemEventSignal(m_EventListComplete);
            AssertRC(rc);
        }
    }

    return rc;
}

/**
 * Assigns a transfer object for the data object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to assign.
 *                              When set to NULL, the transfer will be released from the object.
 */
int ShClWinDataObject::SetTransfer(PSHCLTRANSFER pTransfer)
{
    lock();

    ShClWinDataObject *pObjToRelease = NULL;
    int rc = setTransferLocked(pTransfer, &pObjToRelease);

    unlock();

    if (pObjToRelease)
        pObjToRelease->Release();

    return rc;
}

/**
 * Registers a stream while the data-object lock is held.
 *
 * @param   pStream             Stream to register.
 */
void ShClWinDataObject::registerStreamLocked(ShClWinStreamImpl *pStream)
{
    Assert(RTCritSectIsOwned(&m_CritSect));
    AssertPtr(pStream);

    m_lstStreams.push_back(pStream); /** @todo Can this throw? */
    pStream->AddRef();
}

/**
 * Invalidates all streams and drops the references owned by this data object.
 */
void ShClWinDataObject::invalidateStreams(void)
{
    for (;;)
    {
        lock();
        if (m_lstStreams.empty())
        {
            unlock();
            break;
        }
        ShClWinStreamImpl *pStream = m_lstStreams.back();
        m_lstStreams.pop_back();
        unlock();

        pStream->Invalidate();
        pStream->Release();
    }
}

/**
 * Sets a new status to the data object and signals its waiter.
 *
 * @returns VBox status code.
 * @param   enmStatus           New status to signal.
 * @param   rcSts               Result code. Optional.
 *
 * @note    Called by the main clipboard thread + ShClWinStreamImpl.
 */
int ShClWinDataObject::SetStatus(Status enmStatus, int rcSts /* = VINF_SUCCESS */)
{
    lock();

    int rc = setStatusLocked(enmStatus, rcSts);

    unlock();
    return rc;
}

/* static */
void ShClWinDataObject::logFormat(CLIPFORMAT fmt)
{
    TCHAR szFormat[128];
    if (GetClipboardFormatName(fmt, szFormat, sizeof(szFormat)))
    {
        LogFlowFunc(("clipFormat=%RI16 -> %s\n", fmt, szFormat));
    }
    else
        LogFlowFunc(("clipFormat=%RI16 is unknown\n", fmt));
}

bool ShClWinDataObject::lookupFormatEtc(LPFORMATETC pFormatEtc, ULONG *puIndex)
{
    AssertReturn(pFormatEtc, false);
    /* puIndex is optional. */

    for (ULONG i = 0; i < m_cFormats; i++)
    {
        if(    (pFormatEtc->tymed & m_pFormatEtc[i].tymed)
            && pFormatEtc->cfFormat == m_pFormatEtc[i].cfFormat)
            /* Note: Do *not* compare dwAspect here, as this can be dynamic, depending on how the object should be represented. */
            //&& pFormatEtc->dwAspect == m_pFormatEtc[i].dwAspect)
        {
            LogRel2(("Shared Clipboard: Format found: tyMed=%RI32, cfFormat=%RI16, dwAspect=%RI32, ulIndex=%RU32\n",
                     pFormatEtc->tymed, pFormatEtc->cfFormat, pFormatEtc->dwAspect, i));
            if (puIndex)
                *puIndex = i;
            return true;
        }
    }

    LogRel2(("Shared Clipboard: Format NOT found: tyMed=%RI32, cfFormat=%RI16, dwAspect=%RI32\n",
             pFormatEtc->tymed, pFormatEtc->cfFormat, pFormatEtc->dwAspect));

    logFormat(pFormatEtc->cfFormat);

    return false;
}

void ShClWinDataObject::registerFormat(LPFORMATETC pFormatEtc, CLIPFORMAT clipFormat,
                                                  TYMED tyMed, LONG lIndex, DWORD dwAspect,
                                                  DVTARGETDEVICE *pTargetDevice)
{
    AssertPtr(pFormatEtc);

    pFormatEtc->cfFormat = clipFormat;
    pFormatEtc->tymed    = tyMed;
    pFormatEtc->lindex   = lIndex;
    pFormatEtc->dwAspect = dwAspect;
    pFormatEtc->ptd      = pTargetDevice;

    LogFlowFunc(("Registered format=%ld\n", pFormatEtc->cfFormat));

    logFormat(pFormatEtc->cfFormat);
}

/**
 * Sets a new status to the data object and signals its waiter.
 *
 * @returns VBox status code.
 * @param   enmStatus           New status to signal.
 * @param   rc                  Result code. Optional.
 *                              Errors only accepted when status also is 'Error'.
 *
 * @note    Caller must have taken the critical section.
 */
int ShClWinDataObject::setStatusLocked(Status enmStatus, int rc /* = VINF_SUCCESS */)
{
    AssertReturn(enmStatus == Error || RT_SUCCESS(rc), VERR_INVALID_PARAMETER);
    AssertReturn(RTCritSectIsOwned(&m_CritSect), VERR_WRONG_ORDER);

    LogFlowFunc(("enmStatus=%#x, rc=%Rrc (current is: %#x)\n", enmStatus, rc, m_enmStatus));

    int rc2 = VINF_SUCCESS;

    m_rcStatus = rc;

    switch (enmStatus)
    {
        case Completed:
        {
            LogFlowFunc(("m_uObjIdx=%RU32 (total: %zu)\n", m_uObjIdx, m_lstEntries.size()));

            const bool fComplete = m_uObjIdx == m_lstEntries.size() - 1 /* Object index is zero-based */;
            if (fComplete)
                m_enmStatus = Completed;
            break;
        }

        default:
        {
            m_enmStatus = enmStatus;
            break;
        }
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Data object received error %Rrc (status %#x)\n", rc, enmStatus));

    rc2 = RTSemEventSignal(m_EventStatusChanged);

    LogFlowFuncLeaveRC(rc2);
    return rc2;
}
