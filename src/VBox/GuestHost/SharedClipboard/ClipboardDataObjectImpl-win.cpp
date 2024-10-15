/* $Id: ClipboardDataObjectImpl-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ClipboardDataObjectImpl-win.cpp - Shared Clipboard IDataObject implementation.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

/** @todo Also handle Unicode entries.
 *        !!! WARNING: Buggy, doesn't work yet (some memory corruption / garbage in the file name descriptions) !!! */
//#define VBOX_CLIPBOARD_WITH_UNICODE_SUPPORT 1

ShClWinDataObject::ShClWinDataObject(void)
    : m_pCtx(NULL)
    , m_enmStatus(Uninitialized)
    , m_rcStatus(VERR_IPE_UNINITIALIZED_STATUS)
    , m_lRefCount(0)
    , m_cFormats(0)
    , m_pTransfer(NULL)
    , m_pStream(NULL)
    , m_uObjIdx(0)
    , m_EventListComplete(NIL_RTSEMEVENT)
    , m_EventStatusChanged(NIL_RTSEMEVENT)
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

    m_pCtx = pCtx; /* Save opaque context. */

    /*
     * Set up callback context + table.
     */
    memcpy(&m_Callbacks, pCallbacks, sizeof(ShClWinDataObject::CALLBACKS));
    m_CallbackCtx.pvUser = pCtx;
    m_CallbackCtx.pThis  = this;

    /*
     * Set up / register handled formats.
     */
    ULONG cFixedFormats = 3; /* CFSTR_FILEDESCRIPTORA + CFSTR_FILECONTENTS + CFSTR_PERFORMEDDROPEFFECT */
#ifdef VBOX_CLIPBOARD_WITH_UNICODE_SUPPORT
    cFixedFormats++; /* CFSTR_FILEDESCRIPTORW */
#endif
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

    LogFlowFunc(("Registering CFSTR_FILEDESCRIPTORA ...\n"));
    m_cfFileDescriptorA = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA);
    registerFormat(&m_pFormatEtc[uIdx++], m_cfFileDescriptorA);
#ifdef VBOX_CLIPBOARD_WITH_UNICODE_SUPPORT
    LogFlowFunc(("Registering CFSTR_FILEDESCRIPTORW ...\n"));
    m_cfFileDescriptorW = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
    registerFormat(&m_pFormatEtc[uIdx++], m_cfFileDescriptorW);
#endif

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
        m_cFormats  = cAllFormats;
        m_enmStatus = Initialized;

        rc = RTCritSectInit(&m_CritSect);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventCreate(&m_EventListComplete);
            if (RT_SUCCESS(rc))
                rc = RTSemEventCreate(&m_EventStatusChanged);
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
    setTransferLocked(NULL);

    unlock();
}

/**
 * Uninitialized a data object instance.
 */
void ShClWinDataObject::Uninit(void)
{
    LogFlowFuncEnter();

    uninitInternal();
}

/**
 * Destroys a data object instance.
 */
void ShClWinDataObject::Destroy(void)
{
    LogFlowFuncEnter();

    if (m_enmStatus == Uninitialized) /* Crit sect not available anymore. */
        return;

    uninitInternal();

    int rc = RTCritSectDelete(&m_CritSect);
    AssertRC(rc);

    if (m_EventListComplete != NIL_RTSEMEVENT)
    {
        rc = RTSemEventDestroy(m_EventListComplete);
        AssertRC(rc);
        m_EventListComplete = NIL_RTSEMEVENT;
    }

    if (m_EventStatusChanged != NIL_RTSEMEVENT)
    {
        rc = RTSemEventDestroy(m_EventStatusChanged);
        AssertRC(rc);
        m_EventStatusChanged = NIL_RTSEMEVENT;
    }

    if (m_pStream)
        m_pStream = NULL;

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

                    switch (pThis->m_enmStatus)
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
                            LogRel(("Shared Clipboard: Data object: Transfer error %Rrc occurred\n", pThis->m_rcStatus));
                            rc = ShClTransferError(pTransfer, pThis->m_rcStatus);
                            break;

                        default:
                            AssertFailed();
                            break;
                    }

                    if (pThis->m_Callbacks.pfnTransferEnd)
                    {
                        int rc2 = pThis->m_Callbacks.pfnTransferEnd(&pThis->m_CallbackCtx, pTransfer, pThis->m_rcStatus);
                        if (RT_SUCCESS(rc))
                            rc = rc2;
                    }

                    break;
                } /* for */
            }
        }
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Transfer read thread failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a FILEGROUPDESCRIPTOR object from a given Shared Clipboard transfer and stores the result into an HGLOBAL object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Shared Clipboard transfer to create file grou desciprtor for.
 * @param   fUnicode            Whether the FILEGROUPDESCRIPTOR object shall contain Unicode data or not.
 * @param   phGlobal            Where to store the allocated HGLOBAL object on success.
 */
int ShClWinDataObject::createFileGroupDescriptorFromTransfer(PSHCLTRANSFER pTransfer,
                                                                        bool fUnicode, HGLOBAL *phGlobal)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(phGlobal,  VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    const size_t cbFileGroupDescriptor = fUnicode ? sizeof(FILEGROUPDESCRIPTORW) : sizeof(FILEGROUPDESCRIPTORA);
    const size_t cbFileDescriptor = fUnicode ? sizeof(FILEDESCRIPTORW) : sizeof(FILEDESCRIPTORA);

    const UINT   cItems = (UINT)m_lstEntries.size(); /** UINT vs. size_t. */
    if (!cItems)
        return VERR_NOT_FOUND;

    UINT         curIdx = 0; /* Current index of the handled file group descriptor (FGD). */

    const size_t cbFGD  = cbFileGroupDescriptor + (cbFileDescriptor * (cItems - 1));

    LogFunc(("fUnicode=%RTbool, cItems=%u, cbFileDescriptor=%zu\n", fUnicode, cItems, cbFileDescriptor));

    /* FILEGROUPDESCRIPTORA / FILEGROUPDESCRIPTOR matches except the cFileName member (TCHAR vs. WCHAR). */
    FILEGROUPDESCRIPTOR *pFGD = (FILEGROUPDESCRIPTOR *)RTMemAllocZ(cbFGD);
    if (!pFGD)
        return VERR_NO_MEMORY;

    int rc = VINF_SUCCESS;

    pFGD->cItems = cItems;

    char *pszFileSpec = NULL;

    FsObjEntryList::const_iterator itRoot = m_lstEntries.cbegin();
    while (itRoot != m_lstEntries.end())
    {
        FILEDESCRIPTOR *pFD = &pFGD->fgd[curIdx];
        RT_BZERO(pFD, cbFileDescriptor);

        const char *pszFile = itRoot->pszPath;
        AssertPtr(pszFile);

        pszFileSpec = RTStrDup(pszFile);
        AssertBreakStmt(pszFileSpec != NULL, rc = VERR_NO_MEMORY);

        if (fUnicode)
        {
            PRTUTF16 pwszFileSpec;
            rc = RTStrToUtf16(pszFileSpec, &pwszFileSpec);
            if (RT_SUCCESS(rc))
            {
                rc = RTUtf16CopyEx((PRTUTF16 )pFD->cFileName, sizeof(pFD->cFileName) / sizeof(WCHAR),
                                   pwszFileSpec, RTUtf16Len(pwszFileSpec));
                RTUtf16Free(pwszFileSpec);

                LogFlowFunc(("pFD->cFileNameW=%ls\n", pFD->cFileName));
            }
        }
        else
        {
            rc = RTStrCopy(pFD->cFileName, sizeof(pFD->cFileName), pszFileSpec);
            LogFlowFunc(("pFD->cFileNameA=%s\n", pFD->cFileName));
        }

        RTStrFree(pszFileSpec);
        pszFileSpec = NULL;

        if (RT_FAILURE(rc))
            break;

        pFD->dwFlags          = FD_PROGRESSUI | FD_ATTRIBUTES;
        if (fUnicode) /** @todo Only >= Vista. */
            pFD->dwFlags     |= FD_UNICODE;
        pFD->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

        const SHCLFSOBJINFO *pObjInfo = &itRoot->objInfo;

        if (RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
        {
            pFD->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }
        else if (RTFS_IS_FILE(pObjInfo->Attr.fMode))
        {
            pFD->dwFlags |= FD_FILESIZE;

            const uint64_t cbObjSize = pObjInfo->cbObject;

            pFD->nFileSizeHigh = RT_HI_U32(cbObjSize);
            pFD->nFileSizeLow  = RT_LO_U32(cbObjSize);
        }
        else if (RTFS_IS_SYMLINK(pObjInfo->Attr.fMode))
        {
            /** @todo Implement. */
        }
#if 0 /** @todo Implement this. */
        pFD->dwFlags = FD_ATTRIBUTES | FD_CREATETIME | FD_ACCESSTIME | FD_WRITESTIME | FD_FILESIZE;
        pFD->dwFileAttributes =
        pFD->ftCreationTime   =
        pFD->ftLastAccessTime =
        pFD->ftLastWriteTime  =
#endif
        ++curIdx;
        ++itRoot;
    }

    if (pszFileSpec)
        RTStrFree(pszFileSpec);

    if (RT_SUCCESS(rc))
        rc = copyToHGlobal(pFGD, cbFGD, GMEM_MOVEABLE, phGlobal);

    RTMemFree(pFGD);

    LogFlowFuncLeaveRC(rc);
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

    if (    RT_SUCCESS(rc)
        && (   pFormatEtc->cfFormat == m_cfFileDescriptorA
#ifdef VBOX_CLIPBOARD_WITH_UNICODE_SUPPORT
            || pFormatEtc->cfFormat == m_cfFileDescriptorW
#endif
           )
       )
    {
        switch (m_enmStatus)
        {
            case Initialized:
            {
                LogRel2(("Shared Clipboard: Requesting data for IDataObject ...\n"));

                /* Leave lock while requesting + waiting. */
                unlock();

                /* Start the transfer. */
                AssertPtrBreak(m_Callbacks.pfnTransferBegin);
                rc = m_Callbacks.pfnTransferBegin(&m_CallbackCtx);
                AssertRCBreak(rc);

                LogRel2(("Shared Clipboard: Waiting for IDataObject started status ...\n"));

                /* Note: Keep the timeout low here (instead of using SHCL_TIMEOUT_DEFAULT_MS), as this will make
                 *       Windows Explorer unresponsive (i.e. "ghost window") when waiting for too long. */
                rc = RTSemEventWait(m_EventStatusChanged, RT_MS_10SEC);

                /* Re-acquire lock. */
                lock();

                LogFunc(("Wait resulted in %Rrc and status %#x\n", rc, m_enmStatus));

                if (RT_FAILURE(rc))
                {
                    LogRel(("Shared Clipboard: Waiting for IDataObject status failed, rc=%Rrc\n", rc));
                    break;
                }

                if (m_enmStatus != Running)
                {
                    LogRel(("Shared Clipboard: Received wrong IDataObject status (%#x)\n", m_enmStatus));
                    rc = VERR_WRONG_ORDER;
                    break;
                }

                /* There now must be a transfer assigned. */
                AssertPtrBreakStmt(m_pTransfer, rc = VERR_WRONG_ORDER);

                RT_FALL_THROUGH();
            }

            case Running:
            {
                const bool fUnicode = pFormatEtc->cfFormat == m_cfFileDescriptorW;

                /* Leave lock while waiting. */
                unlock();

                SHCLTRANSFERSTATUS const enmTransferStatus = ShClTransferGetStatus(m_pTransfer);

                LogFlowFunc(("FormatIndex_FileDescriptor%s, enmTransferStatus=%s\n",
                             fUnicode ? "W" : "A", ShClTransferStatusToStr(enmTransferStatus)));

                /* The caller can call GetData() several times, so make sure we don't do the same transfer multiple times. */
                if (enmTransferStatus != SHCLTRANSFERSTATUS_STARTED)
                {
                    /* Start the transfer + run it asynchronously in a separate thread. */
                    rc = ShClTransferStart(m_pTransfer);
                    if (RT_SUCCESS(rc))
                    {
                        rc = ShClTransferRun(m_pTransfer, &ShClWinDataObject::readThread, this /* pvUser */);
                        if (RT_SUCCESS(rc))
                        {
                            /* Don't block for too long here, as this also will screw other apps running on the OS. */
                            LogRel2(("Shared Clipboard: Waiting for IDataObject listing to arrive ...\n"));
                            rc = RTSemEventWait(m_EventListComplete, RT_MS_10SEC);

                            /* Re-acquire lock. */
                            lock();

                            if (   m_pTransfer == NULL
                                || m_enmStatus != Running) /* Still in running state? */
                            {
                                rc = VERR_SHCLPB_NO_DATA;
                                break;
                            }

                            unlock();
                        }
                    }
                }

                lock();

                if (RT_SUCCESS(rc))
                {
                    HGLOBAL hGlobal;
                    rc = createFileGroupDescriptorFromTransfer(m_pTransfer, fUnicode, &hGlobal);
                    if (RT_SUCCESS(rc))
                    {
                        pMedium->tymed   = TYMED_HGLOBAL;
                        pMedium->hGlobal = hGlobal;
                        /* Note: hGlobal now is being owned by pMedium / the caller. */

                        hr = S_OK;
                    }
                }

                break;
            }

            default:
                AssertFailedStmt(rc = VERR_STATE_CHANGED);
                break;
        }

        if (RT_FAILURE(rc))
        {
            LogRel(("Shared Clipboard: Error getting data for IDataObject, rc=%Rrc\n", rc));
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
                hr = ShClWinStreamImpl::Create(this /* pParent */, m_pTransfer,
                                                          fsObjEntry.pszPath /* File name */, &fsObjEntry.objInfo /* PSHCLFSOBJINFO */,
                                                          &m_pStream);
                if (SUCCEEDED(hr))
                {
                    /* Hand over the stream to the caller. */
                    pMedium->tymed = TYMED_ISTREAM;
                    pMedium->pstm  = m_pStream;
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
int ShClWinDataObject::setTransferLocked(PSHCLTRANSFER pTransfer)
{
    AssertReturn(RTCritSectIsOwned(&m_CritSect), VERR_WRONG_ORDER);

    LogFunc(("pTransfer=%p\n", pTransfer));

    int rc = VINF_SUCCESS;

    if (pTransfer) /* Set */
    {
        Assert(m_pTransfer == NULL); /* Transfer already set? */

        if (m_enmStatus == Initialized)
        {
            m_pTransfer = pTransfer;

            ShClWinTransferCtx *pWinURITransferCtx = (ShClWinTransferCtx *)pTransfer->pvUser;
            AssertPtr(pWinURITransferCtx);

            pWinURITransferCtx->pDataObj = this; /* Save a backref to this object. */

            ShClTransferAcquire(pTransfer);
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

            pWinURITransferCtx->pDataObj = NULL; /* Release backref to this object. */

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

    int rc = setTransferLocked(pTransfer);

    unlock();

    return rc;
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
    char szFormat[128];
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
