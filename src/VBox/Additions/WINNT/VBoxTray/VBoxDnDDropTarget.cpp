/* $Id: VBoxDnDDropTarget.cpp 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxDnDTarget.cpp - IDropTarget implementation.
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
#include <iprt/win/shlobj.h> /* For DROPFILES and friends. */

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"
#include "VBoxHelpers.h"
#include "VBoxDnD.h"

#include "VBox/GuestHost/DragAndDrop.h"
#include "VBox/HostServices/DragAndDropSvc.h"

#include <iprt/path.h>
#include <iprt/utf16.h>
#include <iprt/uri.h>


VBoxDnDDropTarget::VBoxDnDDropTarget(VBoxDnDWnd *pParent)
    : m_cRefs(1),
      m_pWndParent(pParent),
      m_dwCurEffect(0),
      m_pvData(NULL),
      m_cbData(0),
      m_EvtDrop(NIL_RTSEMEVENT)
{
    int rc = RTSemEventCreate(&m_EvtDrop);
    LogFlowFunc(("rc=%Rrc\n", rc)); NOREF(rc);
}

VBoxDnDDropTarget::~VBoxDnDDropTarget(void)
{
    reset();

    int rc2 = RTSemEventDestroy(m_EvtDrop);
    AssertRC(rc2);

    LogFlowFunc(("rc=%Rrc, mRefCount=%RI32\n", rc2, m_cRefs));
}

/*
 * IUnknown methods.
 */

STDMETHODIMP_(ULONG) VBoxDnDDropTarget::AddRef(void)
{
    return InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG) VBoxDnDDropTarget::Release(void)
{
    LONG lCount = InterlockedDecrement(&m_cRefs);
    if (lCount == 0)
    {
        delete this;
        return 0;
    }

    return lCount;
}

STDMETHODIMP VBoxDnDDropTarget::QueryInterface(REFIID iid, void **ppvObject)
{
    AssertPtrReturn(ppvObject, E_INVALIDARG);

    if (   iid == IID_IDropSource
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
 * Static helper function to dump supported formats of a data object.
 *
 * @param   pDataObject         Pointer to data object to dump formats for.
 */
/* static */
void VBoxDnDDropTarget::DumpFormats(IDataObject *pDataObject)
{
    AssertPtrReturnVoid(pDataObject);

    /* Enumerate supported source formats. This shouldn't happen too often
     * on day to day use, but still keep it in here. */
    IEnumFORMATETC *pEnumFormats;
    HRESULT hr2 = pDataObject->EnumFormatEtc(DATADIR_GET, &pEnumFormats);
    if (SUCCEEDED(hr2))
    {
        VBoxTrayVerbose(1, "DnD: The following formats were offered to us:\n");

        FORMATETC curFormatEtc;
        while (pEnumFormats->Next(1, &curFormatEtc,
                                  NULL /* pceltFetched */) == S_OK)
        {
            WCHAR wszCfName[128]; /* 128 chars should be enough, rest will be truncated. */
            hr2 = GetClipboardFormatNameW(curFormatEtc.cfFormat, wszCfName,
                                          sizeof(wszCfName) / sizeof(WCHAR));
            VBoxTrayVerbose(1, "\tcfFormat=%RI16 (%s), tyMed=%RI32, dwAspect=%RI32, strCustomName=%ls, hr=%Rhrc\n",
                            curFormatEtc.cfFormat,
                            VBoxDnDDataObject::ClipboardFormatToString(curFormatEtc.cfFormat),
                            curFormatEtc.tymed,
                            curFormatEtc.dwAspect,
                            wszCfName, hr2);
        }

        pEnumFormats->Release();
    }
}

/*
 * IDropTarget methods.
 */

STDMETHODIMP VBoxDnDDropTarget::DragEnter(IDataObject *pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    RT_NOREF(pt);
    AssertPtrReturn(pDataObject, E_INVALIDARG);
    AssertPtrReturn(pdwEffect, E_INVALIDARG);

    LogFlowFunc(("pDataObject=0x%p, grfKeyState=0x%x, x=%ld, y=%ld, dwEffect=%RU32\n",
                 pDataObject, grfKeyState, pt.x, pt.y, *pdwEffect));

    reset();

    /** @todo At the moment we only support one DnD format at a time. */

#ifdef DEBUG
    VBoxDnDDropTarget::DumpFormats(pDataObject);
#endif

    /* Try different formats.
     * CF_HDROP is the most common one, so start with this. */
    FORMATETC fmtEtc = { CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    HRESULT hr = pDataObject->QueryGetData(&fmtEtc);
    if (hr == S_OK)
    {
        m_strFormat = "text/uri-list";
    }
    else
    {
        LogFlowFunc(("CF_HDROP not wanted, hr=%Rhrc\n", hr));

        /* So we couldn't retrieve the data in CF_HDROP format; try with
         * CF_UNICODETEXT + CF_TEXT formats now. Rest stays the same. */
        fmtEtc.cfFormat = CF_UNICODETEXT;
        hr = pDataObject->QueryGetData(&fmtEtc);
        if (hr == S_OK)
        {
            m_strFormat = "text/plain;charset=utf-8";
        }
        else
        {
            LogFlowFunc(("CF_UNICODETEXT not wanted, hr=%Rhrc\n", hr));

            fmtEtc.cfFormat = CF_TEXT;
            hr = pDataObject->QueryGetData(&fmtEtc);
            if (hr == S_OK)
            {
                m_strFormat = "text/plain;charset=utf-8";
            }
            else
            {
                LogFlowFunc(("CF_TEXT not wanted, hr=%Rhrc\n", hr));
                fmtEtc.cfFormat = 0; /* Set it to non-supported. */

                /* Clean up. */
                reset();
            }
        }
    }

    /* Did we find a format that we support? */
    if (fmtEtc.cfFormat)
    {
        LogFlowFunc(("Found supported format %RI16 (%s)\n",
                     fmtEtc.cfFormat, VBoxDnDDataObject::ClipboardFormatToString(fmtEtc.cfFormat)));

        /* Make a copy of the FORMATETC structure so that we later can
         * use this for comparrison and stuff. */
        /** @todo The DVTARGETDEVICE member only is a shallow copy for now! */
        memcpy(&m_FormatEtc, &fmtEtc, sizeof(FORMATETC));

        /* Which drop effect we're going to use? */
        /* Note: pt is not used since we don't need to differentiate within our
         *       proxy window. */
        *pdwEffect = VBoxDnDDropTarget::GetDropEffect(grfKeyState, *pdwEffect);
    }
    else
    {
        /* No or incompatible data -- so no drop effect required. */
        *pdwEffect = DROPEFFECT_NONE;

        switch (hr)
        {
            case ERROR_INVALID_FUNCTION:
            {
                VBoxTrayError("DnD: Drag and drop format is not supported by VBoxTray\n");
                VBoxDnDDropTarget::DumpFormats(pDataObject);
                break;
            }

            default:
                break;
        }
    }

    LogFlowFunc(("Returning mstrFormats=%s, cfFormat=%RI16, pdwEffect=%ld, hr=%Rhrc\n",
                 m_strFormat.c_str(), fmtEtc.cfFormat, *pdwEffect, hr));
    return hr;
}

STDMETHODIMP VBoxDnDDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    RT_NOREF(pt);
    AssertPtrReturn(pdwEffect, E_INVALIDARG);

#ifdef DEBUG_andy
    LogFlowFunc(("cfFormat=%RI16, grfKeyState=0x%x, x=%ld, y=%ld\n",
                 m_FormatEtc.cfFormat, grfKeyState, pt.x, pt.y));
#endif

    if (m_FormatEtc.cfFormat)
    {
        /* Note: pt is not used since we don't need to differentiate within our
         *       proxy window. */
        *pdwEffect = VBoxDnDDropTarget::GetDropEffect(grfKeyState, *pdwEffect);
    }
    else
    {
        *pdwEffect = DROPEFFECT_NONE;
    }

#ifdef DEBUG_andy
    LogFlowFunc(("Returning *pdwEffect=%ld\n", *pdwEffect));
#endif
    return S_OK;
}

STDMETHODIMP VBoxDnDDropTarget::DragLeave(void)
{
#ifdef DEBUG_andy
    LogFlowFunc(("cfFormat=%RI16\n", m_FormatEtc.cfFormat));
#endif

    if (m_pWndParent)
        m_pWndParent->Hide();

    return S_OK;
}

STDMETHODIMP VBoxDnDDropTarget::Drop(IDataObject *pDataObject, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    RT_NOREF(pt);
    AssertPtrReturn(pDataObject, E_INVALIDARG);
    AssertPtrReturn(pdwEffect,   E_INVALIDARG);

    LogFlowFunc(("mFormatEtc.cfFormat=%RI16 (%s), pDataObject=0x%p, grfKeyState=0x%x, x=%ld, y=%ld\n",
                 m_FormatEtc.cfFormat, VBoxDnDDataObject::ClipboardFormatToString(m_FormatEtc.cfFormat),
                 pDataObject, grfKeyState, pt.x, pt.y));

    HRESULT hr = S_OK;

    if (m_FormatEtc.cfFormat) /* Did we get a supported format yet? */
    {
        /* Make sure the data object's data format is still valid. */
        hr = pDataObject->QueryGetData(&m_FormatEtc);
        AssertMsg(SUCCEEDED(hr),
                  ("Data format changed to invalid between DragEnter() and Drop(), cfFormat=%RI16 (%s), hr=%Rhrc\n",
                  m_FormatEtc.cfFormat, VBoxDnDDataObject::ClipboardFormatToString(m_FormatEtc.cfFormat), hr));
    }

    int rc = VINF_SUCCESS;

    if (SUCCEEDED(hr))
    {
        STGMEDIUM stgMed;
        hr = pDataObject->GetData(&m_FormatEtc, &stgMed);
        if (SUCCEEDED(hr))
        {
            /*
             * First stage: Prepare the access to the storage medium.
             *              For now we only support HGLOBAL stuff.
             */
            PVOID pvData = NULL; /** @todo Put this in an own union? */

            switch (m_FormatEtc.tymed)
            {
                case TYMED_HGLOBAL:
                    pvData = GlobalLock(stgMed.hGlobal);
                    if (!pvData)
                    {
                        LogFlowFunc(("Locking HGLOBAL storage failed with %Rrc\n",
                                     RTErrConvertFromWin32(GetLastError())));
                        rc = VERR_INVALID_HANDLE;
                        hr = E_INVALIDARG; /* Set special hr for OLE. */
                    }
                    break;

                default:
                    AssertMsgFailed(("Storage medium type %RI32 supported\n",
                                     m_FormatEtc.tymed));
                    rc = VERR_NOT_SUPPORTED;
                    hr = DV_E_TYMED; /* Set special hr for OLE. */
                    break;
            }

            if (RT_SUCCESS(rc))
            {
                /*
                 * Second stage: Do the actual copying of the data object's data,
                 *               based on the storage medium type.
                 */
                switch (m_FormatEtc.cfFormat)
                {
                    case CF_TEXT:
                        RT_FALL_THROUGH();
                    case CF_UNICODETEXT:
                    {
                        AssertPtr(pvData);
                        size_t cbSize = GlobalSize(pvData);

                        VBoxTrayVerbose(2, "DnD: Got %zu bytes of %s\n", cbSize,
                                          m_FormatEtc.cfFormat == CF_TEXT
                                        ? "ANSI text" : "Unicode text");
                        if (cbSize)
                        {
                            char *pszText = NULL;

                            rc = m_FormatEtc.cfFormat == CF_TEXT
                               /* ANSI codepage -> UTF-8 */
                               ? RTStrCurrentCPToUtf8(&pszText, (char *)pvData)
                               /* Unicode  -> UTF-8 */
                               : RTUtf16ToUtf8((PCRTUTF16)pvData, &pszText);

                            if (RT_SUCCESS(rc))
                            {
                                AssertPtr(pszText);

                                size_t cbText = strlen(pszText) + 1; /* Include termination. */

                                m_pvData = RTMemDup((void *)pszText, cbText);
                                m_cbData = cbText;

                                RTStrFree(pszText);
                                pszText = NULL;
                            }
                        }

                        break;
                    }

                    case CF_HDROP:
                    {
                        AssertPtr(pvData);

                        /* Convert to a string list, separated by \r\n. */
                        DROPFILES *pDropFiles = (DROPFILES *)pvData;
                        AssertPtr(pDropFiles);

                        /** @todo Replace / merge the following code with VBoxShClWinDropFilesToStringList(). */

                        /* Do we need to do Unicode stuff? */
                        const bool fUnicode = RT_BOOL(pDropFiles->fWide);

                        /* Get the offset of the file list. */
                        Assert(pDropFiles->pFiles >= sizeof(DROPFILES));

                        /* Note: This is *not* pDropFiles->pFiles! DragQueryFile only
                         *       will work with the plain storage medium pointer! */
                        HDROP hDrop = (HDROP)(pvData);

                        /* First, get the file count. */
                        /** @todo Does this work on Windows 2000 / NT4? */
                        char  *pszFiles = NULL;
                        size_t cchFiles = 0;
                        UINT cFiles = DragQueryFile(hDrop, UINT32_MAX /* iFile */, NULL /* lpszFile */, 0 /* cchFile */);

                        VBoxTrayVerbose(1, "DnD: Got %RU16 file(s), fUnicode=%RTbool\n", cFiles, fUnicode);

                        for (UINT i = 0; i < cFiles; i++)
                        {
                            UINT cchFile = DragQueryFile(hDrop, i /* File index */, NULL /* Query size first */, 0 /* cchFile */);
                            Assert(cchFile);

                            if (RT_FAILURE(rc))
                                break;

                            char *pszFileUtf8 = NULL; /* UTF-8 version. */
                            UINT cchFileUtf8 = 0;
                            if (fUnicode)
                            {
                                /* Allocate enough space (including terminator). */
                                WCHAR *pwszFile = (WCHAR *)RTMemAlloc((cchFile + 1) * sizeof(WCHAR));
                                if (pwszFile)
                                {
                                    const UINT cwcFileUtf16 = DragQueryFileW(hDrop, i /* File index */,
                                                                             pwszFile, cchFile + 1 /* Include terminator */);

                                    AssertMsg(cwcFileUtf16 == cchFile, ("cchFileUtf16 (%RU16) does not match cchFile (%RU16)\n",
                                                                        cwcFileUtf16, cchFile));
                                    RT_NOREF(cwcFileUtf16);

                                    rc = RTUtf16ToUtf8(pwszFile, &pszFileUtf8);
                                    if (RT_SUCCESS(rc))
                                    {
                                        cchFileUtf8 = (UINT)strlen(pszFileUtf8);
                                        Assert(cchFileUtf8);
                                    }

                                    RTMemFree(pwszFile);
                                }
                                else
                                    rc = VERR_NO_MEMORY;
                            }
                            else /* ANSI */
                            {
                                /* Allocate enough space (including terminator). */
                                pszFileUtf8 = (char *)RTMemAlloc((cchFile + 1) * sizeof(char));
                                if (pszFileUtf8)
                                {
                                    cchFileUtf8 = DragQueryFileA(hDrop, i /* File index */,
                                                                 pszFileUtf8, cchFile + 1 /* Include terminator */);

                                    AssertMsg(cchFileUtf8 == cchFile, ("cchFileUtf8 (%RU16) does not match cchFile (%RU16)\n",
                                                                       cchFileUtf8, cchFile));
                                }
                                else
                                    rc = VERR_NO_MEMORY;
                            }

                            if (RT_SUCCESS(rc))
                            {
                                LogFlowFunc(("\tFile: %s (cchFile=%RU16)\n", pszFileUtf8, cchFileUtf8));

                                VBoxTrayVerbose(1, "DnD: Adding guest file '%s'\n", pszFileUtf8);

                                if (RT_SUCCESS(rc))
                                {
                                    char *pszFileURI = RTUriFileCreate(pszFileUtf8);
                                    if (pszFileURI)
                                    {
                                        const size_t cchFileURI = RTStrNLen(pszFileURI, RTPATH_MAX);
                                        rc = RTStrAAppendExN(&pszFiles, 1 /* cPairs */, pszFileURI, cchFileURI);
                                        if (RT_SUCCESS(rc))
                                            cchFiles += cchFileURI;

                                        RTStrFree(pszFileURI);
                                    }
                                    else
                                        rc = VERR_NO_MEMORY;
                                }
                            }

                            if (RT_FAILURE(rc))
                                VBoxTrayError("DnD: Error handling file entry #%u, rc=%Rrc\n", i, rc);

                            RTStrFree(pszFileUtf8);

                            if (RT_SUCCESS(rc))
                            {
                                /* Add separation between filenames.
                                 * Note: Also do this for the last element of the list. */
                                rc = RTStrAAppendExN(&pszFiles, 1 /* cPairs */, DND_PATH_SEPARATOR_STR, 2 /* Bytes */);
                                if (RT_SUCCESS(rc))
                                    cchFiles += 2; /* Include \r\n */
                            }
                        }

                        if (RT_SUCCESS(rc))
                        {
                            cchFiles += 1; /* Add string termination. */

                            const size_t cbFiles = cchFiles * sizeof(char);

                            LogFlowFunc(("cFiles=%u, cchFiles=%zu, cbFiles=%zu, pszFiles=0x%p\n",
                                         cFiles, cchFiles, cbFiles, pszFiles));

                            m_pvData = pszFiles;
                            m_cbData = cbFiles;
                        }
                        else
                        {
                            RTStrFree(pszFiles);
                            pszFiles = NULL;
                        }

                        LogFlowFunc(("Building CF_HDROP list rc=%Rrc, cFiles=%RU16, cchFiles=%RU32\n",
                                     rc, cFiles, cchFiles));
                        break;
                    }

                    default:
                        /* Note: Should not happen due to the checks done in DragEnter(). */
                        AssertMsgFailed(("Format of type %RI16 (%s) not supported\n",
                                         m_FormatEtc.cfFormat, VBoxDnDDataObject::ClipboardFormatToString(m_FormatEtc.cfFormat)));
                        hr = DV_E_CLIPFORMAT; /* Set special hr for OLE. */
                        break;
                }

                /*
                 * Third stage: Unlock + release access to the storage medium again.
                 */
                switch (m_FormatEtc.tymed)
                {
                    case TYMED_HGLOBAL:
                        GlobalUnlock(stgMed.hGlobal);
                        break;

                    default:
                        AssertMsgFailed(("Really should not happen -- see init stage!\n"));
                        break;
                }
            }

            /* Release storage medium again. */
            ReleaseStgMedium(&stgMed);

            /* Signal waiters. */
            m_rcDropped = rc;
            RTSemEventSignal(m_EvtDrop);
        }
    }

    if (RT_SUCCESS(rc))
    {
        /* Note: pt is not used since we don't need to differentiate within our
         *       proxy window. */
        *pdwEffect = VBoxDnDDropTarget::GetDropEffect(grfKeyState, *pdwEffect);
    }
    else
        *pdwEffect = DROPEFFECT_NONE;

    if (m_pWndParent)
        m_pWndParent->Hide();

    LogFlowFunc(("Returning with hr=%Rhrc (%Rrc), mFormatEtc.cfFormat=%RI16 (%s), *pdwEffect=%RI32\n",
                 hr, rc, m_FormatEtc.cfFormat, VBoxDnDDataObject::ClipboardFormatToString(m_FormatEtc.cfFormat),
                 *pdwEffect));

    return hr;
}

/**
 * Static helper function to return a drop effect for a given key state and allowed effects.
 *
 * @returns Resolved drop effect.
 * @param   grfKeyState         Key state to determine drop effect for.
 * @param   dwAllowedEffects    Allowed drop effects to determine drop effect for.
 */
/* static */
DWORD VBoxDnDDropTarget::GetDropEffect(DWORD grfKeyState, DWORD dwAllowedEffects)
{
    DWORD dwEffect = DROPEFFECT_NONE;

    if(grfKeyState & MK_CONTROL)
        dwEffect = dwAllowedEffects & DROPEFFECT_COPY;
    else if(grfKeyState & MK_SHIFT)
        dwEffect = dwAllowedEffects & DROPEFFECT_MOVE;

    /* If there still was no drop effect assigned, check for the handed-in
     * allowed effects and assign one of them.
     *
     * Note: A move action has precendence over a copy action! */
    if (dwEffect == DROPEFFECT_NONE)
    {
        if (dwAllowedEffects & DROPEFFECT_COPY)
            dwEffect = DROPEFFECT_COPY;
        if (dwAllowedEffects & DROPEFFECT_MOVE)
            dwEffect = DROPEFFECT_MOVE;
    }

#ifdef DEBUG_andy
    LogFlowFunc(("grfKeyState=0x%x, dwAllowedEffects=0x%x, dwEffect=0x%x\n",
                 grfKeyState, dwAllowedEffects, dwEffect));
#endif
    return dwEffect;
}

/**
 * Resets a drop target object.
 */
void VBoxDnDDropTarget::reset(void)
{
    LogFlowFuncEnter();

    if (m_pvData)
    {
        RTMemFree(m_pvData);
        m_pvData = NULL;
    }

    m_cbData = 0;

    RT_ZERO(m_FormatEtc);
    m_strFormat = "";
}

/**
 * Returns the currently supported formats of a drop target.
 *
 * @returns Supported formats.
 */
RTCString VBoxDnDDropTarget::Formats(void) const
{
    return m_strFormat;
}

/**
 * Waits for a drop event to happen.
 *
 * @returns VBox status code.
 * @param   msTimeout           Timeout (in ms) to wait for drop event.
 */
int VBoxDnDDropTarget::WaitForDrop(RTMSINTERVAL msTimeout)
{
    LogFlowFunc(("msTimeout=%RU32\n", msTimeout));

    int rc = RTSemEventWait(m_EvtDrop, msTimeout);
    if (RT_SUCCESS(rc))
        rc = m_rcDropped;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

