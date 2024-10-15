/* $Id: UIDnDDataObject_win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDnDDataObject class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIDnDDataObject_win_h
#define FEQT_INCLUDED_SRC_runtime_UIDnDDataObject_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/critsect.h>

#include <QString>
#include <QStringList>
#include <QVariant>

/* COM includes: */
#include "CDndSource.h"
#include "CSession.h"

/* Forward declarations: */
class UIDnDHandler;

class UIDnDDataObject : public IDataObject
{
public:

    enum Status
    {
        Status_Uninitialized = 0,
        Status_Initialized,
        Status_Dropping,
        Status_Dropped,
        Status_Aborted,
        Status_32Bit_Hack = 0x7fffffff
    };

public:

    UIDnDDataObject(UIDnDHandler *pDnDHandler, const QStringList &lstFormats);
    virtual ~UIDnDDataObject(void);

public: /* IUnknown methods. */

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject);
    STDMETHOD_(ULONG, AddRef)(void);
    STDMETHOD_(ULONG, Release)(void);

public: /* IDataObject methods. */

    STDMETHOD(GetData)(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium);
    STDMETHOD(GetDataHere)(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium);
    STDMETHOD(QueryGetData)(LPFORMATETC pFormatEtc);
    STDMETHOD(GetCanonicalFormatEtc)(LPFORMATETC pFormatEtc,  LPFORMATETC pFormatEtcOut);
    STDMETHOD(SetData)(LPFORMATETC pFormatEtc, LPSTGMEDIUM pMedium, BOOL fRelease);
    STDMETHOD(EnumFormatEtc)(DWORD dwDirection, IEnumFORMATETC **ppEnumFormatEtc);
    STDMETHOD(DAdvise)(LPFORMATETC pFormatEtc, DWORD fAdvise, IAdviseSink *pAdvSink, DWORD *pdwConnection);
    STDMETHOD(DUnadvise)(DWORD dwConnection);
    STDMETHOD(EnumDAdvise)(IEnumSTATDATA **ppEnumAdvise);

public:

    static const char *ClipboardFormatToString(CLIPFORMAT fmt);

    int Abort(void);
    void Signal(void);
    int Signal(const QString &strFormat, const void *pvData, uint32_t cbData);

protected:

    void SetStatus(Status enmStatus);

    bool LookupFormatEtc(LPFORMATETC pFormatEtc, ULONG *puIndex);
    void RegisterFormat(LPFORMATETC pFormatEtc, CLIPFORMAT clipFormat, TYMED tyMed = TYMED_HGLOBAL,
                        LONG lindex = -1, DWORD dwAspect = DVASPECT_CONTENT, DVTARGETDEVICE *pTargetDevice = NULL);

    /** Pointe rto drag and drop handler. */
    UIDnDHandler           *m_pDnDHandler;
    /** Current drag and drop status. */
    Status                  m_enmStatus;
    /** Internal reference count of this object. */
    LONG                    m_cRefs;
    /** Number of native formats registered. This can be a different number than supplied with m_lstFormats. */
    ULONG                   m_cFormats;
    /** Array of registered FORMATETC structs. Matches m_cFormats. */
    FORMATETC              *m_pFormatEtc;
    /** Array of registered STGMEDIUM structs. Matches m_cFormats. */
    STGMEDIUM              *m_pStgMedium;
    /** Event semaphore used for waiting on status changes. */
    RTSEMEVENT              m_SemEvent;
    /** List of supported formats. */
    QStringList             m_lstFormats;
    /** Format of currently retrieved data. */
    QString                 m_strFormat;
    /** The retrieved data as a QVariant. Needed for buffering in case a second format needs the same data,
     *  e.g. CF_TEXT and CF_UNICODETEXT. */
    QVariant                m_vaData;
    /** Whether the data already was retrieved or not. */
    bool                    m_fDataRetrieved;
    /** The retrieved data as a raw buffer. */
    void                   *m_pvData;
    /** Raw buffer size (in bytes). */
    uint32_t                m_cbData;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIDnDDataObject_win_h */

