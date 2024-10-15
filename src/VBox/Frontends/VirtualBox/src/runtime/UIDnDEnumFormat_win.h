/* $Id: UIDnDEnumFormat_win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDnDEnumFormat class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIDnDEnumFormat_win_h
#define FEQT_INCLUDED_SRC_runtime_UIDnDEnumFormat_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


class UIDnDEnumFormatEtc : public IEnumFORMATETC
{
public:

    UIDnDEnumFormatEtc(FORMATETC *pFormatEtc, ULONG cFormats);
    virtual ~UIDnDEnumFormatEtc(void);

public:

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject);
    STDMETHOD_(ULONG, AddRef)(void);
    STDMETHOD_(ULONG, Release)(void);

    STDMETHOD(Next)(ULONG cFormats, FORMATETC *pFormatEtc, ULONG *pcFetched);
    STDMETHOD(Skip)(ULONG cFormats);
    STDMETHOD(Reset)(void);
    STDMETHOD(Clone)(IEnumFORMATETC ** ppEnumFormatEtc);

public:

    static void CopyFormat(FORMATETC *pFormatDest, FORMATETC *pFormatSource);
    static HRESULT CreateEnumFormatEtc(UINT cFormats, FORMATETC *pFormatEtc, IEnumFORMATETC **ppEnumFormatEtc);

private:

    LONG        m_lRefCount;
    ULONG       m_nIndex;
    ULONG       m_nNumFormats;
    FORMATETC * m_pFormatEtc;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIDnDEnumFormat_win_h */

