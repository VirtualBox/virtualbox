/* $Id: UIVirtualMachineItem.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualMachineItem class declarations.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIVirtualMachineItem_h
#define FEQT_INCLUDED_SRC_manager_UIVirtualMachineItem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>
#include <QMimeData>
#include <QPixmap>
#include <QUuid>

/* GUI includes: */
#include "UIManagerDefs.h"
#include "UISettingsDefs.h"

/* Forward declarations: */
class UIVirtualMachineItemCloud;
class UIVirtualMachineItemLocal;

/* Using declarations: */
using namespace UISettingsDefs;

/** Virtual Machine item interface. A wrapper caching VM data. */
class UIVirtualMachineItem : public QObject
{
    Q_OBJECT;

public:

    /** Constructs VM item on the basis of taken @a enmType. */
    UIVirtualMachineItem(UIVirtualMachineItemType enmType);
    /** Destructs VM item. */
    virtual ~UIVirtualMachineItem();

    /** @name RTTI stuff.
      * @{ */
        /** Returns item type. */
        UIVirtualMachineItemType itemType() const { return m_enmType; }
        /** Returns item casted to local type. */
        UIVirtualMachineItemLocal *toLocal();
        /** Returns item casted to cloud type. */
        UIVirtualMachineItemCloud *toCloud();
    /** @} */

    /** @name VM access attributes.
      * @{ */
        /** Returns whether VM was accessible. */
        bool accessible() const { return m_fAccessible; }
        /** Returns the last cached access error. */
        QString accessError() const { return m_strAccessError; }
    /** @} */

    /** @name Basic attributes.
      * @{ */
        /** Returns cached machine id. */
        QUuid id() const { return m_uId; }
        /** Returns cached machine name. */
        QString name() const { return m_strName; }
        /** Returns cached machine OS type id. */
        QString osTypeId() const { return m_strOSTypeId; }
        /** Returns cached machine OS type pixmap.
          * @param  pLogicalSize  Argument to assign actual pixmap size to. */
        QPixmap osPixmap(QSize *pLogicalSize = 0) const;
    /** @} */

    /** @name State attributes.
      * @{ */
        /** Returns cached machine state name. */
        QString machineStateName() const { return m_strMachineStateName; }
        /** Returns cached machine state icon. */
        QIcon machineStateIcon() const { return m_machineStateIcon; }

        /** Returns cached configuration access level. */
        ConfigurationAccessLevel configurationAccessLevel() const { return m_enmConfigurationAccessLevel; }
    /** @} */

    /** @name Visual attributes.
      * @{ */
        /** Returns cached machine tool-tip. */
        QString toolTipText() const { return m_strToolTipText; }
    /** @} */

    /** @name Extra-data options.
      * @{ */
        /** Returns whether we should show machine details. */
        bool hasDetails() const { return m_fHasDetails; }
    /** @} */

    /** @name Update stuff.
      * @{ */
        /** Recaches machine data. */
        virtual void recache() = 0;
        /** Recaches machine item pixmap. */
        virtual void recachePixmap() = 0;
    /** @} */

    /** @name Validation stuff.
      * @{ */
        /** Returns whether this item is editable. */
        virtual bool isItemEditable() const = 0;
        /** Returns whether this item is removable. */
        virtual bool isItemRemovable() const = 0;
        /** Returns whether this item is saved. */
        virtual bool isItemSaved() const = 0;
        /** Returns whether this item is powered off. */
        virtual bool isItemPoweredOff() const = 0;
        /** Returns whether this item is started. */
        virtual bool isItemStarted() const = 0;
        /** Returns whether this item is running. */
        virtual bool isItemRunning() const = 0;
        /** Returns whether this item is running headless. */
        virtual bool isItemRunningHeadless() const = 0;
        /** Returns whether this item is paused. */
        virtual bool isItemPaused() const = 0;
        /** Returns whether this item is stuck. */
        virtual bool isItemStuck() const = 0;
        /** Returns whether this item can be switched to. */
        virtual bool isItemCanBeSwitchedTo() const = 0;
    /** @} */

protected:

    /** @name RTTI stuff.
      * @{ */
        /** Holds item type. */
        UIVirtualMachineItemType  m_enmType;
    /** @} */

    /** @name VM access attributes.
      * @{ */
        /** Holds whether VM was accessible. */
        bool     m_fAccessible;
        /** Holds the last cached access error. */
        QString  m_strAccessError;
    /** @} */

    /** @name Basic attributes.
      * @{ */
        /** Holds cached machine id. */
        QUuid    m_uId;
        /** Holds cached machine name. */
        QString  m_strName;
        /** Holds cached machine OS type id. */
        QString  m_strOSTypeId;
        /** Holds cached machine OS type pixmap. */
        QPixmap  m_pixmap;
        /** Holds cached machine OS type pixmap size. */
        QSize    m_logicalPixmapSize;
    /** @} */

    /** @name State attributes.
      * @{ */
        /** Holds cached machine state name. */
        QString        m_strMachineStateName;
        /** Holds cached machine state name. */
        QIcon          m_machineStateIcon;

        /** Holds configuration access level. */
        ConfigurationAccessLevel  m_enmConfigurationAccessLevel;
    /** @} */

    /** @name Visual attributes.
      * @{ */
        /** Holds cached machine tool-tip. */
        QString  m_strToolTipText;
    /** @} */

    /** @name Extra-data options.
      * @{ */
        /** Holds whether we should show machine details. */
        bool  m_fHasDetails;
    /** @} */
};

/* Make the pointer of this class public to the QVariant framework */
Q_DECLARE_METATYPE(UIVirtualMachineItem *);

/** QMimeData subclass for handling UIVirtualMachineItem mime data. */
class UIVirtualMachineItemMimeData : public QMimeData
{
    Q_OBJECT;

public:

    /** Constructs mime data for passed VM @a pItem. */
    UIVirtualMachineItemMimeData(UIVirtualMachineItem *pItem);

    /** Returns cached VM item. */
    UIVirtualMachineItem *item() const { return m_pItem; }

    /** Returns supported format list. */
    virtual QStringList formats() const RT_OVERRIDE;

    /** Returns UIVirtualMachineItem mime data type. */
    static QString type() { return m_type; }

private:

    /** Holds cached VM item. */
    UIVirtualMachineItem *m_pItem;

    /** Holds UIVirtualMachineItem mime data type. */
    static QString  m_type;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIVirtualMachineItem_h */
