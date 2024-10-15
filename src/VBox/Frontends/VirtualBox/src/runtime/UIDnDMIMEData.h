/* $Id: UIDnDMIMEData.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDnDMIMEData class declaration.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIDnDMIMEData_h
#define FEQT_INCLUDED_SRC_runtime_UIDnDMIMEData_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMimeData>

/* COM includes: */
#include "CConsole.h"
#include "CDnDSource.h"
#include "CGuest.h"
#include "CSession.h"

#include "UIDnDHandler.h"

/** @todo Subclass QWindowsMime / QMacPasteboardMime
 *  to register own/more MIME types. */

/**
 * Own implementation of QMimeData for starting and
 * handling all guest-to-host transfers.
 */
class UIDnDMIMEData: public QMimeData
{
    Q_OBJECT;

    enum State
    {
        /** Host is in dragging state, without
         *  having retrieved the metadata from the guest yet. */
        Dragging = 0,
        /** There has been a "dropped" action which indicates
         *  that the guest can continue sending more data (if any)
         *  over to the host, based on the (MIME) metadata. */
        Dropped,
        /** The operation has been canceled. */
        Canceled,
        /** An error occurred. */
        Error,
        /** The usual 32-bit type blow up. */
        State_32BIT_Hack = 0x7fffffff
    };

public:

    UIDnDMIMEData(UIDnDHandler *pDnDHandler, QStringList formats, Qt::DropAction defAction, Qt::DropActions actions);

signals:

    /**
     * Signal which gets emitted if this object is ready to retrieve data
     * in the specified MIME type.
     *
     * @returns IPRT status code.
     * @param dropAction            Drop action to perform.
     * @param strMimeType           MIME data type.
     * @param vaType                Qt's variant type of the MIME data.
     * @param vaData                Reference to QVariant where to store the retrieved data.
     */
    int sigGetData(Qt::DropAction dropAction, const QString &strMIMEType, QMetaType::Type vaType, QVariant &vaData) const;

public slots:

    /**
     * Slot indicating that the current drop target has been changed.
     * @note Does not work on OS X.
     */
    void sltDropActionChanged(Qt::DropAction dropAction);

protected:
    /** @name Overridden functions of QMimeData.
     * @{ */
    virtual QStringList formats(void) const RT_OVERRIDE;

    virtual bool hasFormat(const QString &mimeType) const RT_OVERRIDE;

    virtual QVariant retrieveData(const QString &strMIMEType, QMetaType metaType) const RT_OVERRIDE;
    /** @}  */

public:

    /** @name Internal helper functions.
     * @{ */

    /**
     * Returns the matching meta type of a given MIME type.
     *
     * @returns Meta type.
     * @param strMIMEType               MIME type to retrieve variant type for.
     */
    static QMetaType::Type getMetaType(const QString &strMIMEType);

    /**
     * Fills a QVariant with data according to the given type and data.
     *
     * @returns IPRT status code.
     * @param   vecData                 Bytes data to set.
     * @param   strMIMEType             MIME type to handle.
     * @param   vaType                  Variant type to set the variant to.
     * @param   vaData                  Variant holding the transformed result.
     *                                  Note: The variant's type might be different from the input vaType!
     */
    static int getDataAsVariant(const QVector<uint8_t> &vecData, const QString &strMIMEType, QMetaType::Type vaType, QVariant &vaData);
    /** @}  */

protected:

    /** Pointer to the parent. */
    UIDnDHandler     *m_pDnDHandler;
    /** Available formats. */
    QStringList       m_lstFormats;
    /** Default action on successful drop operation. */
    Qt::DropAction    m_defAction;
    /** Current action, based on QDrag's status. */
    Qt::DropAction    m_curAction;
    /** Available actions. */
    Qt::DropActions   m_actions;
    /** The current dragging state. */
    mutable State     m_enmState;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIDnDMIMEData_h */

