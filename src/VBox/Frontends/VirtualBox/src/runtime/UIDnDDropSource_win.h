/* $Id: UIDnDDropSource_win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDnDDropSource class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIDnDDropSource_win_h
#define FEQT_INCLUDED_SRC_runtime_UIDnDDropSource_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* COM includes: */

class UIDnDDataObject;

/**
 * Implementation of IDropSource for drag and drop on the host.
 */
class UIDnDDropSource : public IDropSource
{
public:

    UIDnDDropSource(QWidget *pParent, UIDnDDataObject *pDataObject);
    virtual ~UIDnDDropSource(void);

public:

    uint32_t GetCurrentAction(void) const { return m_uCurAction; }

public: /* IUnknown methods. */

    STDMETHOD(QueryInterface)(REFIID iid, void ** ppvObject);
    STDMETHOD_(ULONG, AddRef)(void);
    STDMETHOD_(ULONG, Release)(void);

public: /* IDropSource methods. */

    STDMETHOD(QueryContinueDrag)(BOOL fEscapePressed, DWORD dwKeyState);
    STDMETHOD(GiveFeedback)(DWORD dwEffect);

protected:

    /** Pointer to parent widget. */
    QWidget         *m_pParent;
    /** Pointer to current data object. */
    UIDnDDataObject *m_pDataObject;
    /** The current reference count. */
    LONG             m_cRefCount;
    /** Current (last) drop effect issued. */
    DWORD            m_dwCurEffect;
    /** Current drop action to perform in case of a successful drop. */
    Qt::DropActions  m_uCurAction;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIDnDDropSource_win_h */

