/* $Id: UIDetailsGroup.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsGroup class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetailsGroup_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetailsGroup_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIDetailsItem.h"

/* Forward declarations: */
class QGraphicsLinearLayout;
class QGraphicsScene;
class UIGraphicsScrollArea;
class UIVirtualMachineItem;

/** UIDetailsItem extension implementing group item. */
class UIDetailsGroup : public UIDetailsItem
{
    Q_OBJECT;

signals:

    /** @name Layout stuff.
      * @{ */
        /** Notifies listeners about @a iMinimumWidthHint changed. */
        void sigMinimumWidthHintChanged(int iMinimumWidthHint);
    /** @} */

public:

    /** RTTI item type. */
    enum { Type = UIDetailsItemType_Group };

    /** Constructs group item, passing pScene to the base-class. */
    UIDetailsGroup(QGraphicsScene *pScene);
    /** Destructs group item. */
    virtual ~UIDetailsGroup() RT_OVERRIDE;

    /** @name Item stuff.
      * @{ */
        /** Builds group based on passed @a machineItems. */
        void buildGroup(const QList<UIVirtualMachineItem*> &machineItems);
        /** Builds group based on cached machine items. */
        void rebuildGroup();
        /** Stops currently building group. */
        void stopBuildingGroup();

        /** Installs event-filter for @a pSource object. */
        virtual void installEventFilterHelper(QObject *pSource) RT_OVERRIDE;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Returns children items of certain @a enmType. */
        virtual QList<UIDetailsItem*> items(UIDetailsItemType enmType = UIDetailsItemType_Set) const RT_OVERRIDE;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Updates layout. */
        virtual void updateLayout() RT_OVERRIDE;

        /** Returns minimum width-hint. */
        virtual int minimumWidthHint() const RT_OVERRIDE;
        /** Returns minimum height-hint. */
        virtual int minimumHeightHint() const RT_OVERRIDE;
    /** @} */

protected slots:

    /** @name Item stuff.
      * @{ */
        /** Handles request about starting step build.
          * @param  uStepId    Brings the step ID.
          * @param  iStepNumber  Brings the step number. */
    /** @} */
    virtual void sltBuildStep(const QUuid &uStepId, int iStepNumber) RT_OVERRIDE;

protected:

    /** @name Item stuff.
      * @{ */
        /** Returns RTTI item type. */
        virtual int type() const RT_OVERRIDE { return Type; }

        /** Returns the description of the item. */
        virtual QString description() const RT_OVERRIDE { return QString(); }
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Adds child @a pItem. */
        virtual void addItem(UIDetailsItem *pItem) RT_OVERRIDE;
        /** Removes child @a pItem. */
        virtual void removeItem(UIDetailsItem *pItem) RT_OVERRIDE;

        /** Returns whether there are children items of certain @a enmType. */
        virtual bool hasItems(UIDetailsItemType enmType = UIDetailsItemType_Set) const RT_OVERRIDE;
        /** Clears children items of certain @a enmType. */
        virtual void clearItems(UIDetailsItemType enmType = UIDetailsItemType_Set) RT_OVERRIDE;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Updates geometry. */
        virtual void updateGeometry() RT_OVERRIDE;
    /** @} */

private:

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Prepares connections. */
        void prepareConnections();
    /** @} */

    /** @name Item stuff.
      * @{ */
        /** Holds the build step instance. */
        UIPrepareStep *m_pBuildStep;
        /** Holds the generated group ID. */
        QUuid          m_uGroupId;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Holds the children scroll-area instance. */
        UIGraphicsScrollArea  *m_pScrollArea;
        /** Holds the children container instance. */
        QIGraphicsWidget      *m_pContainer;
        /** Holds the children layout instance. */
        QGraphicsLinearLayout *m_pLayout;

        /** Holds the cached machine item list. */
        QList<UIVirtualMachineItem*> m_machineItems;

        /** Holds the child list (a list of sets). */
        QList<UIDetailsItem*> m_items;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Holds previous minimum width hint. */
        int m_iPreviousMinimumWidthHint;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetailsGroup_h */
