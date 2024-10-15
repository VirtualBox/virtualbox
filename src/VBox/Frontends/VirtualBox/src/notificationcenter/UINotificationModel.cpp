/* $Id: UINotificationModel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINotificationModel class implementation.
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

/* Qt includes: */
#include <QSet>

/* GUI includes: */
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UINotificationModel.h"
#include "UINotificationObject.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UINotificationModel::UINotificationModel(QObject *pParent)
    : QObject(pParent)
{
    prepare();
}

UINotificationModel::~UINotificationModel()
{
    cleanup();
}

QUuid UINotificationModel::appendObject(UINotificationObject *pObject)
{
    /* [Re]generate ID until unique: */
    QUuid uId = QUuid::createUuid();
    while (m_ids.contains(uId))
        uId = QUuid::createUuid();
    /* Append ID and object: */
    m_ids << uId;
    m_objects[uId] = pObject;
    /* Connect object close signal: */
    connect(pObject, &UINotificationObject::sigAboutToClose,
            this, &UINotificationModel::sltHandleAboutToClose);
    /* Notify listeners: */
    emit sigItemAdded(uId);
    /* Handle object: */
    pObject->handle();
    /* Return ID: */
    return uId;
}

void UINotificationModel::revokeObject(const QUuid &uId)
{
    /* Remove id first of all: */
    m_ids.removeAll(uId);
    /* Notify listeners before object is deleted: */
    emit sigItemRemoved(uId);
    /* Delete object itself finally: */
    delete m_objects.take(uId);
}

bool UINotificationModel::hasObject(const QUuid &uId) const
{
    return m_objects.contains(uId);
}

void UINotificationModel::revokeFinishedObjects()
{
    /* Check whether there are done objects: */
    foreach (const QUuid &uId, m_ids)
    {
        UINotificationObject *pObject = m_objects.value(uId);
        AssertPtrReturnVoid(pObject);
        if (pObject->isDone())
            revokeObject(uId);
    }
}

QList<QUuid> UINotificationModel::ids() const
{
    return m_ids;
}

UINotificationObject *UINotificationModel::objectById(const QUuid &uId)
{
    return m_objects.value(uId);
}

void UINotificationModel::sltHandleAboutToClose(bool fDismiss)
{
    /* Determine sender: */
    UINotificationObject *pSender = qobject_cast<UINotificationObject*>(sender());
    AssertPtrReturnVoid(pSender);

    /* Dismiss message if requested: */
    if (fDismiss && !pSender->internalName().isEmpty())
    {
        QStringList suppressedMessages = gEDataManager->suppressedMessages();
        if (!suppressedMessages.contains(pSender->internalName()))
        {
            suppressedMessages.push_back(pSender->internalName());
            gEDataManager->setSuppressedMessages(suppressedMessages);
        }
    }

    /* Revoke it from internal storage: */
    const QUuid uId = m_objects.key(pSender);
    AssertReturnVoid(!uId.isNull());
    revokeObject(uId);
}

void UINotificationModel::sltDetachCOM()
{
    cleanup();
}

void UINotificationModel::prepare()
{
    connect(&uiCommon(), &UICommon::sigAskToDetachCOM,
            this, &UINotificationModel::sltDetachCOM);
}

void UINotificationModel::cleanup()
{
    /* Wipe out all the objects: */
    foreach (const QUuid &uId, m_ids)
        revokeObject(uId);
    m_objects.clear();
    m_ids.clear();
}
