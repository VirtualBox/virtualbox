/* $Id: ClipboardEnumFormatEtcImpl-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ClipboardEnumFormatEtcImpl-win.cpp - Shared Clipboard IEnumFORMATETC ("Format et cetera") implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <new> /* For bad_alloc. */

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/GuestHost/SharedClipboard-win.h>

#include <iprt/win/windows.h>

#include <VBox/log.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Static variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef VBOX_SHARED_CLIPBOARD_DEBUG_OBJECT_COUNTS
 extern int g_cDbgDataObj;
 extern int g_cDbgStreamObj;
 extern int g_cDbgEnumFmtObj;
#endif


ShClWinEnumFormatEtc::ShClWinEnumFormatEtc(void)
    : m_lRefCount(1),
      m_nIndex(0)
{
#ifdef VBOX_SHARED_CLIPBOARD_DEBUG_OBJECT_COUNTS
    g_cDbgEnumFmtObj++;
    LogFlowFunc(("g_cDataObj=%d, g_cStreamObj=%d, g_cEnumFmtObj=%d\n", g_cDbgDataObj, g_cDbgStreamObj, g_cDbgEnumFmtObj));
#endif
}

ShClWinEnumFormatEtc::~ShClWinEnumFormatEtc(void)
{
    Destroy();

    LogFlowFunc(("m_lRefCount=%RI32\n", m_lRefCount));

#ifdef VBOX_SHARED_CLIPBOARD_DEBUG_OBJECT_COUNTS
    g_cDbgEnumFmtObj--;
    LogFlowFunc(("g_cDataObj=%d, g_cStreamObj=%d, g_cEnumFmtObj=%d\n", g_cDbgDataObj, g_cDbgStreamObj, g_cDbgEnumFmtObj));
#endif
}

/**
 * Initializes an IEnumFORMATETC instance.
 *
 * @returns VBox status code.
 * @param   pFormatEtc          Array of formats to use for initialization.
 * @param   cFormats            Number of formats in \a pFormatEtc.
 */
int ShClWinEnumFormatEtc::Init(LPFORMATETC pFormatEtc, ULONG cFormats)
{
    LogFlowFunc(("pFormatEtc=%p, cFormats=%RU32\n", pFormatEtc, cFormats));
    m_pFormatEtc = new FORMATETC[cFormats];
    AssertPtrReturn(m_pFormatEtc, VERR_NO_MEMORY);

    for (ULONG i = 0; i < cFormats; i++)
    {
        LogFlowFunc(("Format %RU32: cfFormat=%RI16, tyMed=%RU32, dwAspect=%RU32\n",
                     i, pFormatEtc[i].cfFormat, pFormatEtc[i].tymed, pFormatEtc[i].dwAspect));

        ShClWinDataObject::logFormat(pFormatEtc[i].cfFormat);

        ShClWinEnumFormatEtc::CopyFormat(&m_pFormatEtc[i], &pFormatEtc[i]);
    }

    m_nNumFormats = cFormats;

    return VINF_SUCCESS;
}

/**
 * Destroys an IEnumFORMATETC instance.
 */
void ShClWinEnumFormatEtc::Destroy(void)
{
    if (m_pFormatEtc)
    {
        for (ULONG i = 0; i < m_nNumFormats; i++)
        {
            if(m_pFormatEtc[i].ptd)
                CoTaskMemFree(m_pFormatEtc[i].ptd);
        }

        delete[] m_pFormatEtc;
        m_pFormatEtc = NULL;
    }

    m_nNumFormats = 0;
}

/*
 * IUnknown methods.
 */

STDMETHODIMP_(ULONG) ShClWinEnumFormatEtc::AddRef(void)
{
    return InterlockedIncrement(&m_lRefCount);
}

STDMETHODIMP_(ULONG) ShClWinEnumFormatEtc::Release(void)
{
    LONG lCount = InterlockedDecrement(&m_lRefCount);
    if (lCount == 0)
    {
        LogFlowFunc(("Delete\n"));
        delete this;
        return 0;
    }

    return lCount;
}

STDMETHODIMP ShClWinEnumFormatEtc::QueryInterface(REFIID iid, void **ppvObject)
{
    if (   iid == IID_IEnumFORMATETC
        || iid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = 0;
    return E_NOINTERFACE;
}

STDMETHODIMP ShClWinEnumFormatEtc::Next(ULONG cFormats, LPFORMATETC pFormatEtc, ULONG *pcFetched)
{
    ULONG ulCopied  = 0;

    if(cFormats == 0 || pFormatEtc == 0)
        return E_INVALIDARG;

    while (   m_nIndex < m_nNumFormats
           && ulCopied < cFormats)
    {
        ShClWinEnumFormatEtc::CopyFormat(&pFormatEtc[ulCopied], &m_pFormatEtc[m_nIndex]);
        ulCopied++;
        m_nIndex++;
    }

    if (pcFetched)
        *pcFetched = ulCopied;

    return (ulCopied == cFormats) ? S_OK : S_FALSE;
}

STDMETHODIMP ShClWinEnumFormatEtc::Skip(ULONG cFormats)
{
    m_nIndex += cFormats;
    return (m_nIndex <= m_nNumFormats) ? S_OK : S_FALSE;
}

STDMETHODIMP ShClWinEnumFormatEtc::Reset(void)
{
    m_nIndex = 0;
    return S_OK;
}

STDMETHODIMP ShClWinEnumFormatEtc::Clone(IEnumFORMATETC **ppEnumFormatEtc)
{
    HRESULT hResult = CreateEnumFormatEtc(m_nNumFormats, m_pFormatEtc, ppEnumFormatEtc);
    if (hResult == S_OK)
        ((ShClWinEnumFormatEtc *) *ppEnumFormatEtc)->m_nIndex = m_nIndex;

    return hResult;
}

/* static */
void ShClWinEnumFormatEtc::CopyFormat(LPFORMATETC pDest, LPFORMATETC pSource)
{
    AssertPtrReturnVoid(pDest);
    AssertPtrReturnVoid(pSource);

    *pDest = *pSource;

    if (pSource->ptd)
    {
        pDest->ptd = (DVTARGETDEVICE*)CoTaskMemAlloc(sizeof(DVTARGETDEVICE));
        *(pDest->ptd) = *(pSource->ptd);
    }
}

/* static */
HRESULT ShClWinEnumFormatEtc::CreateEnumFormatEtc(UINT nNumFormats, LPFORMATETC pFormatEtc, IEnumFORMATETC **ppEnumFormatEtc)
{
    AssertReturn(nNumFormats, E_INVALIDARG);
    AssertPtrReturn(pFormatEtc, E_INVALIDARG);
    AssertPtrReturn(ppEnumFormatEtc, E_INVALIDARG);

    HRESULT hr;

    ShClWinEnumFormatEtc *pEnumFormatEtc = new ShClWinEnumFormatEtc();
    if (pEnumFormatEtc)
    {
        hr = pEnumFormatEtc->Init(pFormatEtc, nNumFormats);
        if (SUCCEEDED(hr))
            *ppEnumFormatEtc = pEnumFormatEtc;
    }
    else
        hr = E_OUTOFMEMORY;

    return hr;
}

