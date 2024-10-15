/* $Id: UIMediaComboBox.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediaComboBox class declaration.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIMediaComboBox_h
#define FEQT_INCLUDED_SRC_widgets_UIMediaComboBox_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QComboBox>
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIMedium.h"
#include "UIMediumDefs.h"

/** QComboBox subclass representing a list of registered media. */
class SHARED_LIBRARY_STUFF UIMediaComboBox : public QComboBox
{
    Q_OBJECT;

public:

    /** Base-to-diff media map. */
    typedef QMap<QString, QString> BaseToDiffMap;

    /** Constructs media combo-box passing @a pParent to the base-class. */
    UIMediaComboBox(QWidget *pParent = 0);

    /** Performs refresh. */
    void refresh();
    /** Performs repopulation. */
    void repopulate();

    /** Defines @a enmMediaType. */
    void setType(UIMediumDeviceType enmMediaType) { m_enmMediaType = enmMediaType; }
    /** Returns media type. */
    UIMediumDeviceType type() const { return m_enmMediaType; }

    /** Defines @a uMachineId. */
    void setMachineId(const QUuid &uMachineId) { m_uMachineId = uMachineId; }

    /** Defines current item through @a uItemId. */
    void setCurrentItem(const QUuid &uItemId);

    /** Returns id of item with certain @a iIndex. */
    QUuid id(int iIndex = -1) const;
    /** Returns location of item with certain @a iIndex. */
    QString location(int iIndex = -1) const;

protected slots:

    /** Habdles medium-created signal for medium with @a uMediumId. */
    void sltHandleMediumCreated(const QUuid &uMediumId);
    /** Habdles medium-enumerated signal for medium with @a uMediumId. */
    void sltHandleMediumEnumerated(const QUuid &uMediumId);
    /** Habdles medium-deleted signal for medium with @a uMediumId. */
    void sltHandleMediumDeleted(const QUuid &uMediumId);

    /** Handles medium-enumeration start. */
    void sltHandleMediumEnumerationStart();

    /** Handles combo activation for item with certain @a iIndex. */
    void sltHandleComboActivated(int iIndex);

    /** Handles combo hovering for item with certain @a index. */
    void sltHandleComboHovered(const QModelIndex &index);

protected:

    /** Prepares all. */
    void prepare();

    /** Uses the tool-tip of the item with @a iIndex. */
    void updateToolTip(int iIndex);

    /** Appends item for certain @a guiMedium. */
    void appendItem(const UIMedium &guiMedium);
    /** Replases item on certain @a iPosition with new item based on @a guiMedium. */
    void replaceItem(int iPosition, const UIMedium &guiMedium);

    /** Searches for a @a iIndex of medium with certain @a uId. */
    bool findMediaIndex(const QUuid &uId, int &iIndex);

    /** Holds the media type. */
    UIMediumDeviceType  m_enmMediaType;

    /** Holds the machine ID. */
    QUuid  m_uMachineId;

    /** Simplified media description. */
    struct Medium
    {
        Medium() {}
        Medium(const QUuid &uId,
               const QString &strLocation,
               const QString &strToolTip)
            : id(uId), location(strLocation), toolTip(strToolTip)
        {}

        QUuid    id;
        QString  location;
        QString  toolTip;
    };
    /** Vector of simplified media descriptions. */
    typedef QVector<Medium> Media;

    /** Holds currently cached media descriptions. */
    Media  m_media;

    /** Holds the last chosen medium ID. */
    QUuid  m_uLastItemId;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIMediaComboBox_h */
