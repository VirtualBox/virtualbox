/* $Id: UIDnDHandler.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDnDHandler class declaration..
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIDnDHandler_h
#define FEQT_INCLUDED_SRC_runtime_UIDnDHandler_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMimeData>
#include <QMutex>
#include <QStringList>

/* COM includes: */
#include "CDnDTarget.h"
#include "CDnDSource.h"

/* Forward declarations: */
class QMimeData;

class UIDnDMIMEData;
class UIMachine;

/**
 * Main class for implementing Drag'n'Drop in the frontend.
 */
class UIDnDHandler: public QObject
{
    Q_OBJECT;

public:

    UIDnDHandler(UIMachine *pMachine, QWidget *pParent);
    virtual ~UIDnDHandler(void);

    /**
     * Drag and drop data set from the source.
     */
    typedef struct UIDnDDataSource
    {
        /** List of formats supported by the source. */
        QStringList         lstFormats;
        /** List of allowed drop actions from the source. */
        QVector<KDnDAction> vecActions;
        /** Default drop action from the source. */
        KDnDAction          defaultAction;

    } UIDnDDataSource;

    int                        init(void);
    void                       reset(void);

    /* Frontend -> Target. */
    Qt::DropAction             dragEnter(ulong screenId, int x, int y, Qt::DropAction proposedAction, Qt::DropActions possibleActions, const QMimeData *pMimeData);
    Qt::DropAction             dragMove (ulong screenId, int x, int y, Qt::DropAction proposedAction, Qt::DropActions possibleActions, const QMimeData *pMimeData);
    Qt::DropAction             dragDrop (ulong screenId, int x, int y, Qt::DropAction proposedAction, Qt::DropActions possibleActions, const QMimeData *pMimeData);
    void                       dragLeave(ulong screenId);

    /* Source -> Frontend. */
    int                        dragCheckPending(ulong screenId);
    int                        dragStart(ulong screenId);
    int                        dragStop(ulong screenID);

    /* Data access. */
    int                        retrieveData(Qt::DropAction dropAction, const QString &strMIMEType, QVector<uint8_t> &vecData);
    int                        retrieveData(Qt::DropAction dropAction, const QString &strMIMEType, QMetaType::Type vaType, QVariant &vaData);

public:

    static KDnDAction          toVBoxDnDAction(Qt::DropAction action);
    static QVector<KDnDAction> toVBoxDnDActions(Qt::DropActions actions);
    static Qt::DropAction      toQtDnDAction(KDnDAction action);
    static Qt::DropActions     toQtDnDActions(const QVector<KDnDAction> &vecActions);

public slots:

    /**
     * Called by UIDnDMIMEData (Linux, OS X, Solaris) to start retrieving the actual data
     * from the guest. This function will block and show a modal progress dialog until
     * the data transfer is complete.
     *
     * @return IPRT status code.
     * @param dropAction            Drop action to perform.
     * @param strMIMEType           MIME data type.
     * @param vaType                Qt's variant type of the MIME data.
     * @param vaData                Reference to QVariant where to store the retrieved data.
     */
    int                        sltGetData(Qt::DropAction dropAction, const QString &strMIMEType, QMetaType::Type vaType, QVariant &vaData);

protected:

#ifdef DEBUG
    static void                debugOutputQt(QtMsgType type, const char *pszMsg);
#endif

protected:

    int                        dragStartInternal(const QStringList &lstFormats, Qt::DropAction defAction, Qt::DropActions actions);
    int                        retrieveDataInternal(Qt::DropAction dropAction, const QString &strMIMEType, QVector<uint8_t> &vecData);

protected:

#ifdef RT_OS_WINDOWS
    static int                 getProcessIntegrityLevel(DWORD *pdwIntegrityLevel);
#endif

protected:

    /** Pointer to machine UI. */
    UIMachine        *m_pMachine;
    /** Pointer to parent widget. */
    QWidget          *m_pParent;
    /** Drag and drop source instance. */
    CDnDSource        m_dndSource;
    /** Drag and drop target instance. */
    CDnDTarget        m_dndTarget;
    /** Current data from the source (if any).
     *  At the momenet we only support one source at a time. */
    UIDnDDataSource   m_dataSource;
    /** Flag indicating whether data has been retrieved from
     *  the guest already or not. */
    bool              m_fDataRetrieved;
    QMutex            m_ReadLock;
    QMutex            m_WriteLock;
    /** Data received from the guest. */
    QVector<uint8_t>  m_vecData;

#ifdef RT_OS_WINDOWS
    /** Process integrity level we're running with. Needed for UIPI detection + logging.
     *  Set to 0 if not set yet or unavailable. */
    DWORD             m_dwIntegrityLevel;
#else /* !RT_OS_WINDOWS */
    /** Pointer to MIMEData instance used for handling
     *  own MIME times on non-Windows host OSes. */
    UIDnDMIMEData    *m_pMIMEData;
    friend class UIDnDMIMEData;
#endif /* RT_OS_WINDOWS */
};
#endif /* !FEQT_INCLUDED_SRC_runtime_UIDnDHandler_h */

