/* $Id: UIDnDDataObject_win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDnDDrag class implementation (implements IDataObject).
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/log.h>

#include <iprt/win/windows.h>
#include <new> /* For bad_alloc. */
#include <iprt/win/shlobj.h>

#include <iprt/mem.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/uri.h>
#include <iprt/utf16.h>

#include <QStringList>

#include "UIDnDHandler.h"
#include "UIDnDDataObject_win.h"
#include "UIDnDEnumFormat_win.h"


UIDnDDataObject::UIDnDDataObject(UIDnDHandler *pDnDHandler, const QStringList &lstFormats)
    : m_pDnDHandler(pDnDHandler)
    , m_enmStatus(Status_Uninitialized)
    , m_cRefs(1)
    , m_cFormats(0)
    , m_pFormatEtc(NULL)
    , m_pStgMedium(NULL)
    , m_SemEvent(NIL_RTSEMEVENT)
    , m_fDataRetrieved(false)
    , m_pvData(NULL)
    , m_cbData(0)
{
    HRESULT hr;

    int   cMaxFormats        = 16; /* Maximum number of registered formats. */
    ULONG cRegisteredFormats = 0;

    try
    {
        m_pFormatEtc = new FORMATETC[cMaxFormats];
        RT_BZERO(m_pFormatEtc, sizeof(FORMATETC) * cMaxFormats);
        m_pStgMedium = new STGMEDIUM[cMaxFormats];
        RT_BZERO(m_pStgMedium, sizeof(STGMEDIUM) * cMaxFormats);

        for (int i = 0; i < lstFormats.size() && i < cMaxFormats; i++)
        {
            const QString &strFormat = lstFormats.at(i);
            if (m_lstFormats.contains(strFormat))
                continue;

            /* URI data ("text/uri-list"). */
            if (strFormat.contains("text/uri-list", Qt::CaseInsensitive))
            {
                RegisterFormat(&m_pFormatEtc[cRegisteredFormats], CF_TEXT);
                m_pStgMedium[cRegisteredFormats++].tymed = TYMED_HGLOBAL;
                RegisterFormat(&m_pFormatEtc[cRegisteredFormats], CF_UNICODETEXT);
                m_pStgMedium[cRegisteredFormats++].tymed = TYMED_HGLOBAL;
                RegisterFormat(&m_pFormatEtc[cRegisteredFormats], CF_HDROP);
                m_pStgMedium[cRegisteredFormats++].tymed = TYMED_HGLOBAL;

                m_lstFormats << strFormat;
            }
            /* Plain text ("text/plain"). */
            if (strFormat.contains("text/plain", Qt::CaseInsensitive))
            {
                RegisterFormat(&m_pFormatEtc[cRegisteredFormats], CF_TEXT);
                m_pStgMedium[cRegisteredFormats++].tymed = TYMED_HGLOBAL;
                RegisterFormat(&m_pFormatEtc[cRegisteredFormats], CF_UNICODETEXT);
                m_pStgMedium[cRegisteredFormats++].tymed = TYMED_HGLOBAL;

                m_lstFormats << strFormat;
            }
        }

        LogRel3(("DnD: Total registered native formats: %RU32 (for %d formats from guest)\n",
                 cRegisteredFormats, lstFormats.size()));
        hr = S_OK;
    }
    catch (std::bad_alloc &)
    {
        hr = E_OUTOFMEMORY;
    }

    if (SUCCEEDED(hr))
    {
        int rc2 = RTSemEventCreate(&m_SemEvent);
        AssertRC(rc2);

        /*
         * Other (not so common) formats.
         */
#if 0
        /* IStream. */
        RegisterFormat(&mpFormatEtc[cFormats++],
                       RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR));
        RegisterFormat(&mpFormatEtc[cFormats++],
                       RegisterClipboardFormat(CFSTR_FILECONTENTS),
                       TYMED_ISTREAM, 0 /* lIndex */);

        /* Required for e.g. Windows Media Player. */
        RegisterFormat(&mpFormatEtc[cFormats++],
                       RegisterClipboardFormat(CFSTR_FILENAME));
        RegisterFormat(&mpFormatEtc[cFormats++],
                       RegisterClipboardFormat(CFSTR_FILENAMEW));
        RegisterFormat(&mpFormatEtc[cFormats++],
                       RegisterClipboardFormat(CFSTR_SHELLIDLIST));
        RegisterFormat(&mpFormatEtc[cFormats++],
                       RegisterClipboardFormat(CFSTR_SHELLIDLISTOFFSET));
#endif
        m_cFormats  = cRegisteredFormats;
        m_enmStatus = Status_Dropping;
    }

    LogFlowFunc(("hr=%Rhrc\n", hr));
}

UIDnDDataObject::~UIDnDDataObject(void)
{
    if (m_pFormatEtc)
        delete[] m_pFormatEtc;

    if (m_pStgMedium)
        delete[] m_pStgMedium;

    if (m_pvData)
        RTMemFree(m_pvData);

    if (m_SemEvent != NIL_RTSEMEVENT)
        RTSemEventDestroy(m_SemEvent);

    LogFlowFunc(("mRefCount=%RI32\n", m_cRefs));
}

/*
 * IUnknown methods.
 */

STDMETHODIMP_(ULONG) UIDnDDataObject::AddRef(void)
{
    return InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG) UIDnDDataObject::Release(void)
{
    LONG lCount = InterlockedDecrement(&m_cRefs);
    if (lCount == 0)
    {
        delete this;
        return 0;
    }

    return lCount;
}

STDMETHODIMP UIDnDDataObject::QueryInterface(REFIID iid, void **ppvObject)
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

STDMETHODIMP UIDnDDataObject::GetData(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium)
{
    AssertPtrReturn(pFormatEtc, DV_E_FORMATETC);
    AssertPtrReturn(pMedium, DV_E_FORMATETC);

    HRESULT hr = DV_E_FORMATETC;

    LPFORMATETC pThisFormat = NULL;
    LPSTGMEDIUM pThisMedium = NULL;

    LogFlowThisFunc(("\n"));

    /* Format supported? */
    ULONG lIndex;
    if (   LookupFormatEtc(pFormatEtc, &lIndex)
        && lIndex < m_cFormats) /* Paranoia. */
    {
        pThisMedium = &m_pStgMedium[lIndex];
        AssertPtr(pThisMedium);
        pThisFormat = &m_pFormatEtc[lIndex];
        AssertPtr(pThisFormat);

        LogFlowThisFunc(("pThisMedium=%p, pThisFormat=%p\n", pThisMedium, pThisFormat));
        LogFlowThisFunc(("mStatus=%RU32\n", m_enmStatus));
        switch (m_enmStatus)
        {
            case Status_Dropping:
            {
#if 0
                LogRel3(("DnD: Dropping\n"));
                LogFlowFunc(("Waiting for event ...\n"));
                int rc2 = RTSemEventWait(m_SemEvent, RT_INDEFINITE_WAIT);
                LogFlowFunc(("rc=%Rrc, mStatus=%RU32\n", rc2, m_enmStatus));
#endif
                break;
            }

            case Status_Dropped:
            {
                LogRel3(("DnD: Dropped\n"));
                LogRel3(("DnD: cfFormat=%RI16, sFormat=%s, tyMed=%RU32, dwAspect=%RU32\n",
                         pThisFormat->cfFormat, UIDnDDataObject::ClipboardFormatToString(pFormatEtc->cfFormat),
                         pThisFormat->tymed, pThisFormat->dwAspect));
                LogRel3(("DnD: Got strFormat=%s, pvData=%p, cbData=%RU32\n",
                         m_strFormat.toUtf8().constData(), m_pvData, m_cbData));

                QMetaType::Type vaType = QMetaType::UnknownType; /* MSC: Might be used uninitialized otherwise! */
                QString strMIMEType;
                if (    (pFormatEtc->tymed & TYMED_HGLOBAL)
                     && pFormatEtc->dwAspect == DVASPECT_CONTENT
                     && (   pFormatEtc->cfFormat == CF_TEXT
                         || pFormatEtc->cfFormat == CF_UNICODETEXT)
                   )
                {
                    /* Use UTF-8, always. */
                    strMIMEType = "text/plain;charset=utf-8";
                    vaType      = QMetaType::QString;
                }
                else if (   (pFormatEtc->tymed & TYMED_HGLOBAL)
                         && pFormatEtc->dwAspect == DVASPECT_CONTENT
                         && pFormatEtc->cfFormat == CF_HDROP)
                {
                    strMIMEType = "text/uri-list";
                    vaType = QMetaType::QStringList;
                }
#if 0 /* More formats; not needed right now. */
                else if (   (pFormatEtc->tymed & TYMED_ISTREAM)
                        && (pFormatEtc->dwAspect == DVASPECT_CONTENT)
                        && (pFormatEtc->cfFormat == CF_FILECONTENTS))
                {

                }
                else if  (   (pFormatEtc->tymed & TYMED_HGLOBAL)
                          && (pFormatEtc->dwAspect == DVASPECT_CONTENT)
                          && (pFormatEtc->cfFormat == CF_FILEDESCRIPTOR))
                {

                }
                else if (   (pFormatEtc->tymed & TYMED_HGLOBAL)
                         && (pFormatEtc->cfFormat == CF_PREFERREDDROPEFFECT))
                {
                    HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, sizeof(DWORD));
                    DWORD *pdwEffect = (DWORD *)GlobalLock(hData);
                    AssertPtr(pdwEffect);
                    *pdwEffect = DROPEFFECT_COPY;
                    GlobalUnlock(hData);

                    pMedium->hGlobal = hData;
                    pMedium->tymed = TYMED_HGLOBAL;
                }
#endif
                LogRel3(("DnD: strMIMEType=%s\n", strMIMEType.toUtf8().constData()));

                int rc;

                if (!m_fDataRetrieved)
                {
                    if (m_pDnDHandler)
                        rc = m_pDnDHandler->retrieveData(Qt::CopyAction, strMIMEType, vaType, m_vaData);
                    else
                        rc = VERR_NOT_FOUND;

                    m_fDataRetrieved = true;
                    LogFlowFunc(("Retrieving data ended with %Rrc\n", rc));
                }
                else /* Data already been retrieved. */
                    rc = VINF_SUCCESS;

                if (   RT_SUCCESS(rc)
                    && m_vaData.isValid())
                {
                    if (   strMIMEType.startsWith("text/uri-list")
                               /* One item. */
                        && (   m_vaData.canConvert(QMetaType::QString)
                               /* Multiple items. */
                            || m_vaData.canConvert(QMetaType::QStringList))
                       )
                    {
                        QStringList lstFilesURI = m_vaData.toStringList();
                        QStringList lstFiles;
                        for (int i = 0; i < lstFilesURI.size(); i++)
                        {
                            char *pszFilePath = RTUriFilePath(lstFilesURI.at(i).toUtf8().constData());
                            if (pszFilePath)
                            {
                                lstFiles.append(pszFilePath);
                                RTStrFree(pszFilePath);
                            }
                            else /* Unable to parse -- refuse entire request. */
                            {
                                lstFiles.clear();
                                rc = VERR_INVALID_PARAMETER;
                                break;
                            }
                        }

                        int cFiles = lstFiles.size();
                        LogFlowThisFunc(("Files (%d)\n", cFiles));
                        if (   RT_SUCCESS(rc)
                            && cFiles)
                        {
                            size_t cchFiles = 0; /* Number of characters. */
                            for (int i = 0; i < cFiles; i++)
                            {
                                const char *pszFile = lstFiles.at(i).toUtf8().constData();
                                cchFiles += strlen(pszFile);
                                cchFiles += 1; /* Terminating '\0'. */
                                LogFlowThisFunc(("\tFile: %s (cchFiles=%zu)\n", pszFile, cchFiles));
                            }

                            /* List termination with '\0'. */
                            cchFiles++;

                            size_t cbBuf = sizeof(DROPFILES) + (cchFiles * sizeof(RTUTF16));
                            DROPFILES *pDropFiles = (DROPFILES *)RTMemAllocZ(cbBuf);
                            if (pDropFiles)
                            {
                                /* Put the files list right after our DROPFILES structure. */
                                pDropFiles->pFiles = sizeof(DROPFILES); /* Offset to file list. */
                                pDropFiles->fWide  = 1;                 /* We use Unicode. Always. */

                                uint8_t *pCurFile = (uint8_t *)pDropFiles + pDropFiles->pFiles;
                                AssertPtr(pCurFile);

                                LogFlowThisFunc(("Encoded:\n"));
                                for (int i = 0; i < cFiles; i++)
                                {
                                    const char *pszFile = lstFiles.at(i).toUtf8().constData();
                                    Assert(strlen(pszFile));

                                    size_t cchCurFile;
                                    PRTUTF16 pwszFile;
                                    rc = RTStrToUtf16(pszFile, &pwszFile);
                                    if (RT_SUCCESS(rc))
                                    {
                                        cchCurFile = RTUtf16Len(pwszFile);
                                        Assert(cchCurFile);
                                        memcpy(pCurFile, pwszFile, cchCurFile * sizeof(RTUTF16));
                                        RTUtf16Free(pwszFile);
                                    }
                                    else
                                        break;

                                    pCurFile += cchCurFile * sizeof(RTUTF16);

                                    /* Terminate current file name. */
                                    *pCurFile = L'\0';
                                    pCurFile += sizeof(RTUTF16);

                                    LogFlowThisFunc(("\t#%d: cchCurFile=%zu\n", i, cchCurFile));
                                }

                                if (RT_SUCCESS(rc))
                                {
                                    *pCurFile = L'\0'; /* Final list terminator. */

                                    /*
                                     * Fill out the medium structure we're going to report back.
                                     */
                                    pMedium->tymed          = TYMED_HGLOBAL;
                                    pMedium->pUnkForRelease = NULL;
                                    pMedium->hGlobal        = GlobalAlloc(  GMEM_ZEROINIT
                                                                          | GMEM_MOVEABLE
                                                                          | GMEM_DDESHARE, cbBuf);
                                    if (pMedium->hGlobal)
                                    {
                                        LPVOID pvMem = GlobalLock(pMedium->hGlobal);
                                        if (pvMem)
                                        {
                                            memcpy(pvMem, pDropFiles, cbBuf);
                                            GlobalUnlock(pMedium->hGlobal);

                                            hr = S_OK;
                                        }
                                        else
                                            rc = VERR_ACCESS_DENIED;
                                    }
                                    else
                                        rc = VERR_NO_MEMORY;

                                    LogFlowThisFunc(("Copying to TYMED_HGLOBAL (%zu bytes): %Rrc\n", cbBuf, rc));
                                }

                                RTMemFree(pDropFiles);
                            }
                            else
                                rc = VERR_NO_MEMORY;

                            if (RT_FAILURE(rc))
                                LogFlowThisFunc(("Failed with %Rrc\n", rc));
                        }
                    }
                    else if (   strMIMEType.startsWith("text/plain;charset=utf-8") /* Use UTF-8, always. */
                             && m_vaData.canConvert(QMetaType::QString))
                    {
                        const bool fUnicode = pFormatEtc->cfFormat == CF_UNICODETEXT;
                        const size_t cbCh   = fUnicode
                                            ? sizeof(WCHAR) : sizeof(char);

                        QString strText = m_vaData.toString();
                        size_t cbSrc    = strText.length() * cbCh;
                        LPCVOID pvSrc   = fUnicode
                                        ? (void *)strText.unicode()
                                        : (void *)strText.toUtf8().constData();

                        AssertMsg(cbSrc, ("pvSrc=0x%p, cbSrc=%zu, cbCh=%zu\n", pvSrc, cbSrc, cbCh));
                        AssertPtr(pvSrc);

                        LogFlowFunc(("pvSrc=0x%p, cbSrc=%zu, cbCh=%zu, fUnicode=%RTbool\n",
                                     pvSrc, cbSrc, cbCh, fUnicode));

                        pMedium->tymed          = TYMED_HGLOBAL;
                        pMedium->pUnkForRelease = NULL;
                        pMedium->hGlobal        = GlobalAlloc(GHND | GMEM_SHARE, cbSrc);
                        if (pMedium->hGlobal)
                        {
                            LPVOID pvDst = GlobalLock(pMedium->hGlobal);
                            if (pvDst)
                            {
                                memcpy(pvDst, pvSrc, cbSrc);
                                GlobalUnlock(pMedium->hGlobal);
                            }
                            else
                                rc = VERR_ACCESS_DENIED;

                            hr = S_OK;
                        }
                        else
                            hr  = VERR_NO_MEMORY;
                    }
                    else
                        LogRel2(("DnD: MIME type '%s' not supported\n", strMIMEType.toUtf8().constData()));

                    LogFlowThisFunc(("Handling formats ended with rc=%Rrc\n", rc));
                }

                break;
            }

            default:
                break;
        }
    }

    /*
     * Fallback in error case.
     */
    if (FAILED(hr))
    {
        if (pThisMedium)
        {
            switch (pThisMedium->tymed)
            {

            case TYMED_HGLOBAL:
                pMedium->hGlobal = (HGLOBAL)OleDuplicateData(pThisMedium->hGlobal,
                                                             pThisFormat->cfFormat,
                                                             0 /* Flags */);
                break;

            default:
                break;
            }
        }

        if (pFormatEtc)
            pMedium->tymed = pFormatEtc->tymed;

        pMedium->pUnkForRelease = NULL;
    }

    LogFlowThisFunc(("Returning hr=%Rhrc\n", hr));
    return hr;
}

STDMETHODIMP UIDnDDataObject::GetDataHere(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium)
{
    RT_NOREF(pFormatEtc, pMedium);
    LogFlowFunc(("\n"));
    return DATA_E_FORMATETC;
}

STDMETHODIMP UIDnDDataObject::QueryGetData(LPFORMATETC pFormatEtc)
{
    return LookupFormatEtc(pFormatEtc, NULL /* puIndex */) ? S_OK : DV_E_FORMATETC;
}

STDMETHODIMP UIDnDDataObject::GetCanonicalFormatEtc(LPFORMATETC pFormatEtc, LPFORMATETC pFormatEtcOut)
{
    RT_NOREF(pFormatEtc);
    LogFlowFunc(("\n"));

    /* Set this to NULL in any case. */
    pFormatEtcOut->ptd = NULL;
    return E_NOTIMPL;
}

STDMETHODIMP UIDnDDataObject::SetData(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium, BOOL fRelease)
{
    RT_NOREF(pFormatEtc, pMedium, fRelease);
    return E_NOTIMPL;
}

STDMETHODIMP UIDnDDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppEnumFormatEtc)
{
    LogFlowFunc(("dwDirection=%RI32, mcFormats=%RI32, mpFormatEtc=%p\n",
                 dwDirection, m_cFormats, m_pFormatEtc));

    HRESULT hr;
    if (dwDirection == DATADIR_GET)
    {
        hr = UIDnDEnumFormatEtc::CreateEnumFormatEtc(m_cFormats, m_pFormatEtc, ppEnumFormatEtc);
    }
    else
        hr = E_NOTIMPL;

    LogFlowFunc(("hr=%Rhrc\n", hr));
    return hr;
}

STDMETHODIMP UIDnDDataObject::DAdvise(LPFORMATETC pFormatEtc, DWORD fAdvise, IAdviseSink *pAdvSink, DWORD *pdwConnection)
{
    RT_NOREF(pFormatEtc, fAdvise, pAdvSink, pdwConnection);
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP UIDnDDataObject::DUnadvise(DWORD dwConnection)
{
    RT_NOREF(dwConnection);
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP UIDnDDataObject::EnumDAdvise(IEnumSTATDATA **ppEnumAdvise)
{
    RT_NOREF(ppEnumAdvise);
    return OLE_E_ADVISENOTSUPPORTED;
}

/*
 * Own stuff.
 */

/**
 * Aborts waiting for data being "dropped".
 *
 * @returns VBox status code.
 */
int UIDnDDataObject::Abort(void)
{
    LogFlowFunc(("Aborting ...\n"));
    m_enmStatus = Status_Aborted;
    return RTSemEventSignal(m_SemEvent);
}

/**
 * Static helper function to convert a CLIPFORMAT to a string and return it.
 *
 * @returns Pointer to converted stringified CLIPFORMAT, or "unknown" if not found / invalid.
 * @param   fmt                 CLIPFORMAT to return string for.
 */
/* static */
const char* UIDnDDataObject::ClipboardFormatToString(CLIPFORMAT fmt)
{
    WCHAR wszFormat[128];
    if (GetClipboardFormatNameW(fmt, wszFormat, sizeof(wszFormat) / sizeof(WCHAR)))
        LogFlowFunc(("wFormat=%RI16, szName=%ls\n", fmt, wszFormat));

    switch (fmt)
    {

    case 1:
        return "CF_TEXT";
    case 2:
        return "CF_BITMAP";
    case 3:
        return "CF_METAFILEPICT";
    case 4:
        return "CF_SYLK";
    case 5:
        return "CF_DIF";
    case 6:
        return "CF_TIFF";
    case 7:
        return "CF_OEMTEXT";
    case 8:
        return "CF_DIB";
    case 9:
        return "CF_PALETTE";
    case 10:
        return "CF_PENDATA";
    case 11:
        return "CF_RIFF";
    case 12:
        return "CF_WAVE";
    case 13:
        return "CF_UNICODETEXT";
    case 14:
        return "CF_ENHMETAFILE";
    case 15:
        return "CF_HDROP";
    case 16:
        return "CF_LOCALE";
    case 17:
        return "CF_DIBV5";
    case 18:
        return "CF_MAX";
    case 49158:
        return "FileName";
    case 49159:
        return "FileNameW";
    case 49161:
        return "DATAOBJECT";
    case 49171:
        return "Ole Private Data";
    case 49314:
        return "Shell Object Offsets";
    case 49316:
        return "File Contents";
    case 49317:
        return "File Group Descriptor";
    case 49323:
        return "Preferred Drop Effect";
    case 49380:
        return "Shell Object Offsets";
    case 49382:
        return "FileContents";
    case 49383:
        return "FileGroupDescriptor";
    case 49389:
        return "Preferred DropEffect";
    case 49268:
        return "Shell IDList Array";
    case 49619:
        return "RenPrivateFileAttachments";
    default:
        break;
    }

    return "unknown";
}

/**
 * Checks whether a given FORMATETC is supported by this data object and returns its index.
 *
 * @returns \c true if format is supported, \c false if not.
 * @param   pFormatEtc          Pointer to FORMATETC to check for.
 * @param   puIndex             Where to store the index if format is supported.
 */
bool UIDnDDataObject::LookupFormatEtc(LPFORMATETC pFormatEtc, ULONG *puIndex)
{
    AssertReturn(pFormatEtc, false);
    /* puIndex is optional. */

    for (ULONG i = 0; i < m_cFormats; i++)
    {
        if(    (pFormatEtc->tymed & m_pFormatEtc[i].tymed)
            && pFormatEtc->cfFormat == m_pFormatEtc[i].cfFormat
            && pFormatEtc->dwAspect == m_pFormatEtc[i].dwAspect)
        {
            LogRel3(("DnD: Format found: tyMed=%RI32, cfFormat=%RI16, sFormats=%s, dwAspect=%RI32, ulIndex=%RU32\n",
                     pFormatEtc->tymed, pFormatEtc->cfFormat, UIDnDDataObject::ClipboardFormatToString(m_pFormatEtc[i].cfFormat),
                     pFormatEtc->dwAspect, i));

            if (puIndex)
                *puIndex = i;
            return true;
        }
    }

#if 0
    LogRel3(("DnD: Format NOT found: tyMed=%RI32, cfFormat=%RI16, sFormats=%s, dwAspect=%RI32\n",
             pFormatEtc->tymed, pFormatEtc->cfFormat, UIDnDDataObject::ClipboardFormatToString(pFormatEtc->cfFormat),
             pFormatEtc->dwAspect));
#endif

    return false;
}

/**
 * Registers a new format with this data object.
 *
 * @param   pFormatEtc          Where to store the new format into.
 * @param   clipFormat          Clipboard format to register.
 * @param   tyMed               Format medium type to register.
 * @param   lIndex              Format index to register.
 * @param   dwAspect            Format aspect to register.
 * @param   pTargetDevice       Format target device to register.
 */
void UIDnDDataObject::RegisterFormat(LPFORMATETC pFormatEtc, CLIPFORMAT clipFormat,
                                     TYMED tyMed, LONG lIndex, DWORD dwAspect,
                                     DVTARGETDEVICE *pTargetDevice)
{
    AssertPtr(pFormatEtc);

    pFormatEtc->cfFormat = clipFormat;
    pFormatEtc->tymed    = tyMed;
    pFormatEtc->lindex   = lIndex;
    pFormatEtc->dwAspect = dwAspect;
    pFormatEtc->ptd      = pTargetDevice;

    LogFlowFunc(("Registered format=%ld, sFormat=%s\n",
                 pFormatEtc->cfFormat, UIDnDDataObject::ClipboardFormatToString(pFormatEtc->cfFormat)));
}

/**
 * Sets the current status of this data object.
 *
 * @param   enmStatus           New status to set.
 */
void UIDnDDataObject::SetStatus(Status enmStatus)
{
    LogFlowFunc(("Setting status to %RU32\n", enmStatus));
    m_enmStatus = enmStatus;
}

/**
 * Signals that data has been "dropped".
 */
void UIDnDDataObject::Signal(void)
{
    SetStatus(Status_Dropped);
}

/**
 * Signals that data has been "dropped".
 *
 * @returns VBox status code.
 * @param   strFormat           Format of data (MIME string).
 * @param   pvData              Pointer to data.
 * @param   cbData              Size (in bytes) of data.
 */
int UIDnDDataObject::Signal(const QString &strFormat,
                            const void *pvData, uint32_t cbData)
{
    LogFlowFunc(("Signalling ...\n"));

    int rc;

    if (cbData)
    {
        m_pvData = RTMemAlloc(cbData);
        if (m_pvData)
        {
            memcpy(m_pvData, pvData, cbData);
            m_cbData = cbData;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        m_strFormat = strFormat;
        SetStatus(Status_Dropped);
    }
    else
        SetStatus(Status_Aborted);

    /* Signal in any case. */
    int rc2 = RTSemEventSignal(m_SemEvent);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

