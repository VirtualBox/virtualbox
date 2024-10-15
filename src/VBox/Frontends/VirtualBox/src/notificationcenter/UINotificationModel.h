/* $Id: UINotificationModel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINotificationModel class declaration.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_notificationcenter_UINotificationModel_h
#define FEQT_INCLUDED_SRC_notificationcenter_UINotificationModel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QList>
#include <QMap>
#include <QObject>
#include <QUuid>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class UINotificationObject;

/** QObject-based notification-center model. */
class SHARED_LIBRARY_STUFF UINotificationModel : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about item with specified @a uId was added. */
    void sigItemAdded(const QUuid &uId);
    /** Notifies listeners about item with specified @a uId was removed. */
    void sigItemRemoved(const QUuid &uId);

public:

    /** Constructs notification-center model passing @a pParent to the base-class. */
    UINotificationModel(QObject *pParent);
    /** Destructs notification-center model. */
    virtual ~UINotificationModel() RT_OVERRIDE RT_FINAL;

    /** Appens a notification @a pObject to internal storage. */
    QUuid appendObject(UINotificationObject *pObject);
    /** Revokes a notification object referenced by @a uId from intenal storage. */
    void revokeObject(const QUuid &uId);
    /** Returns whether there is a notification object referenced by @a uId. */
    bool hasObject(const QUuid &uId) const;
    /** Revokes finished notification objects. */
    void revokeFinishedObjects();

    /** Returns a list of registered notification object IDs. */
    QList<QUuid> ids() const;
    /** Returns a notification object referenced by specified @a uId. */
    UINotificationObject *objectById(const QUuid &uId);

private slots:

    /** Handles request about to close sender() notification object.
      * @param  fDismiss  Brings whether message closed as dismissed. */
    void sltHandleAboutToClose(bool fDismiss);

    /** Handles broadcast request to detach COM stuff. */
    void sltDetachCOM();

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds the list of registered notification object IDs. */
    QList<QUuid>                        m_ids;
    /** Holds the map of notification objects registered by ID. */
    QMap<QUuid, UINotificationObject*>  m_objects;
};

#endif /* !FEQT_INCLUDED_SRC_notificationcenter_UINotificationModel_h */
