/* $Id: VBoxDnDEnumFormat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDnDEnumFormat.cpp - IEnumFORMATETC ("Format et cetera") implementation.
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
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/log.h>

#include <iprt/win/windows.h>
#include <new> /* For bad_alloc. */

#include "VBoxTray.h"
#include "VBoxHelpers.h"
#include "VBoxDnD.h"


VBoxDnDEnumFormatEtc::VBoxDnDEnumFormatEtc(LPFORMATETC pFormatEtc, ULONG uIdx, ULONG cToCopy, ULONG cTotal)
    : m_cRefs(1)
    , m_uIdxCur(0)
    , m_cFormats(0)
    , m_paFormatEtc(NULL)

{
    int rc2 = Init(pFormatEtc, uIdx, cToCopy, cTotal);
    AssertRC(rc2);
}

VBoxDnDEnumFormatEtc::~VBoxDnDEnumFormatEtc(void)
{
    if (m_paFormatEtc)
    {
        for (ULONG i = 0; i < m_cFormats; i++)
            if (m_paFormatEtc[i].ptd)
            {
                CoTaskMemFree(m_paFormatEtc[i].ptd);
                m_paFormatEtc[i].ptd = NULL;
            }

        RTMemFree(m_paFormatEtc);
        m_paFormatEtc = NULL;
    }

    LogFlowFunc(("m_lRefCount=%RI32\n", m_cRefs));
}

/**
 * Initializes the class by copying the required formats.
 *
 * @returns VBox status code.
 * @param   pFormatEtc          Format Etc to use for initialization.
 * @param   uIdx                Index (zero-based) of format
 * @param   cToCopy             Number of formats \a pFormatEtc to copy, starting from \a uIdx.
 * @param   cTotal              Number of total formats \a pFormatEtc holds.
 */
int VBoxDnDEnumFormatEtc::Init(LPFORMATETC pFormatEtc, ULONG uIdx, ULONG cToCopy, ULONG cTotal)
{
    AssertPtrReturn(pFormatEtc,            VERR_INVALID_POINTER);
    AssertReturn(uIdx <= cTotal,           VERR_INVALID_PARAMETER);
    AssertReturn(uIdx + cToCopy <= cTotal, VERR_INVALID_PARAMETER);
    /* cFormats can be 0. */

    if (!cToCopy)
        return VINF_SUCCESS;

    AssertReturn(m_paFormatEtc == NULL && m_cFormats == 0, VERR_WRONG_ORDER);

    int rc = VINF_SUCCESS;

    m_paFormatEtc = (LPFORMATETC)RTMemAllocZ(sizeof(FORMATETC) * cToCopy);
    if (m_paFormatEtc)
    {
        for (ULONG i = 0; i < cToCopy; i++)
        {
            LPFORMATETC const pFormatCur = &pFormatEtc[uIdx + i];

            LogFlowFunc(("Format %RU32 (index %RU32): cfFormat=%RI16, sFormat=%s, tyMed=%RU32, dwAspect=%RU32\n",
                         i, uIdx + i, pFormatCur->cfFormat, VBoxDnDDataObject::ClipboardFormatToString(pFormatCur->cfFormat),
                         pFormatCur->tymed, pFormatCur->dwAspect));
            VBoxDnDEnumFormatEtc::CopyFormat(&m_paFormatEtc[i], pFormatCur);
        }

        m_cFormats = cToCopy;
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}

/*
 * IUnknown methods.
 */

STDMETHODIMP_(ULONG) VBoxDnDEnumFormatEtc::AddRef(void)
{
    return InterlockedIncrement(&m_cRefs);
}

STDMETHODIMP_(ULONG) VBoxDnDEnumFormatEtc::Release(void)
{
    LONG lCount = InterlockedDecrement(&m_cRefs);
    if (lCount == 0)
    {
        delete this;
        return 0;
    }

    return lCount;
}

STDMETHODIMP VBoxDnDEnumFormatEtc::QueryInterface(REFIID iid, void **ppvObject)
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

STDMETHODIMP VBoxDnDEnumFormatEtc::Next(ULONG cFormats, LPFORMATETC pFormatEtc, ULONG *pcFetched)
{
    ULONG ulCopied  = 0;

    if(cFormats == 0 || pFormatEtc == 0)
        return E_INVALIDARG;

    while (   m_uIdxCur < m_cFormats
           && ulCopied < cFormats)
    {
        VBoxDnDEnumFormatEtc::CopyFormat(&pFormatEtc[ulCopied],
                                         &m_paFormatEtc[m_uIdxCur]);
        ulCopied++;
        m_uIdxCur++;
    }

    if (pcFetched)
        *pcFetched = ulCopied;

    return (ulCopied == cFormats) ? S_OK : S_FALSE;
}

STDMETHODIMP VBoxDnDEnumFormatEtc::Skip(ULONG cFormats)
{
    m_uIdxCur += cFormats;
    return (m_uIdxCur <= m_cFormats) ? S_OK : S_FALSE;
}

STDMETHODIMP VBoxDnDEnumFormatEtc::Reset(void)
{
    m_uIdxCur = 0;
    return S_OK;
}

STDMETHODIMP VBoxDnDEnumFormatEtc::Clone(IEnumFORMATETC **ppEnumFormatEtc)
{
    HRESULT hrc = CreateEnumFormatEtc(m_cFormats, m_paFormatEtc, ppEnumFormatEtc);

    if (hrc == S_OK)
        ((VBoxDnDEnumFormatEtc *) *ppEnumFormatEtc)->m_uIdxCur = m_uIdxCur;

    return hrc;
}

/**
 * Copies a format etc from \a pSource to \a aDest (deep copy).
 *
 * @returns VBox status code.
 * @param   pDest               Where to copy \a pSource to.
 * @param   pSource             Source to copy.
 */
/* static */
int VBoxDnDEnumFormatEtc::CopyFormat(LPFORMATETC pDest, LPFORMATETC pSource)
{
    AssertPtrReturn(pDest  , VERR_INVALID_POINTER);
    AssertPtrReturn(pSource, VERR_INVALID_POINTER);

    *pDest = *pSource;

    if (pSource->ptd)
    {
        pDest->ptd = (DVTARGETDEVICE*)CoTaskMemAlloc(sizeof(DVTARGETDEVICE));
        AssertPtrReturn(pDest->ptd, VERR_NO_MEMORY);
        *(pDest->ptd) = *(pSource->ptd);
    }

    return VINF_SUCCESS;
}

/**
 * Creates an IEnumFormatEtc interface from a given format etc structure.
 *
 * @returns HRESULT
 * @param   nNumFormats         Number of formats to copy from \a pFormatEtc.
 * @param   pFormatEtc          Format etc to use for creation.
 * @param   ppEnumFormatEtc     Where to return the created IEnumFormatEtc interface on success.
 */
/* static */
HRESULT VBoxDnDEnumFormatEtc::CreateEnumFormatEtc(UINT nNumFormats, LPFORMATETC pFormatEtc, IEnumFORMATETC **ppEnumFormatEtc)
{
    /* cNumFormats can be 0. */
    AssertPtrReturn(pFormatEtc, E_INVALIDARG);
    AssertPtrReturn(ppEnumFormatEtc, E_INVALIDARG);

#ifdef RT_EXCEPTIONS_ENABLED
    try { *ppEnumFormatEtc = new VBoxDnDEnumFormatEtc(pFormatEtc,
                                                      0 /* uIdx */,  nNumFormats /* cToCopy */, nNumFormats /* cTotal */); }
    catch (std::bad_alloc &)
#else
    *ppEnumFormatEtc = new VBoxDnDEnumFormatEtc(pFormatEtc,
                                                0 /* uIdx */, nNumFormats /* cToCopy */, nNumFormats /* cTotal */);
    if (!*ppEnumFormatEtc)
#endif
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

