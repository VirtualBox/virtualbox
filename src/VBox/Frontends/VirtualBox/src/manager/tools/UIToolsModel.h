/* $Id: UIToolsModel.h 109275 2025-04-18 13:26:41Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolsModel class declaration.
 */

/*
 * Copyright (C) 2012-2025 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_tools_UIToolsModel_h
#define FEQT_INCLUDED_SRC_manager_tools_UIToolsModel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QPointer>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Forward declaration: */
class QGraphicsItem;
class QGraphicsScene;
class QPaintDevice;
class UIToolsItem;
class UIToolsView;

/** QObject extension used as VM Tools-pane model: */
class UIToolsModel : public QObject
{
    Q_OBJECT;

signals:

    /** @name Selection stuff.
      * @{ */
        /** Notifies about selection changed.
          * @param  enmType  Brings current tool type. */
        void sigSelectionChanged(UIToolType enmType);
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Notifies about item minimum width @a iHint changed. */
        void sigItemMinimumWidthHintChanged(int iHint);
        /** Notifies about item minimum height @a iHint changed. */
        void sigItemMinimumHeightHintChanged(int iHint);
    /** @} */

public:

    /** Data field types. */
    enum ToolsModelData
    {
        /* Layout hints: */
        ToolsModelData_Margin,
        ToolsModelData_Spacing,
    };

    /** Constructs Tools-model passing @a pParent to the base-class.
      * @param  enmClass  Brings the tool class. */
    UIToolsModel(QObject *pParent,
                 UIToolClass enmClass);
    /** Destructs Tools-model. */
    virtual ~UIToolsModel() RT_OVERRIDE;

    /** @name General stuff.
      * @{ */
        /** Inits model. */
        void init();

        /** Returns the scene reference. */
        QGraphicsScene *scene() const;
        /** Returns the paint device reference. */
        QPaintDevice *paintDevice() const;

        /** Returns item at @a position. */
        QGraphicsItem *itemAt(const QPointF &position) const;

        /** Returns tools-view reference. */
        UIToolsView *view() const;
        /** Defines tools @a pView reference. */
        void setView(UIToolsView *pView);

        /** Defines current tools @a enmType. */
        void setToolsType(UIToolType enmType);
        /** Returns current tools type for the @a enmClass specified. */
        UIToolType toolsType(UIToolClass enmClass) const;

        /** Defines whether tool items @a fEnabled.*/
        void setItemsEnabled(bool fEnabled);
        /** Returns whether tool items enabled.*/
        bool isItemsEnabled() const;

        /** Defines restricted tool @a types for the @a enmClass specified. */
        void setRestrictedToolTypes(UIToolClass enmClass, const QList<UIToolType> &types);

        /** Returns restricted tool types for the @a enmClass specified. */
        QList<UIToolType> restrictedToolTypes(UIToolClass enmClass) const;

        /** Returns abstractly stored data value for certain @a iKey. */
        QVariant data(int iKey) const;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Returns the item list. */
        QList<UIToolsItem*> items() const;

        /** Returns the item of passed @a enmType. */
        UIToolsItem *item(UIToolType enmType) const;

        /** Returns whether we should show item names. */
        bool showItemNames() const;
    /** @} */

    /** @name Selection stuff.
      * @{ */
        /** Defines current @a pItem. */
        void setCurrentItem(UIToolsItem *pItem);
        /** Returns current item for the @a enmClass specified. */
        UIToolsItem *currentItem(UIToolClass enmClass) const;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Updates layout. */
        void updateLayout();
    /** @} */

public slots:

    /** @name Children stuff.
      * @{ */
        /** Handles minimum width hint change. */
        void sltItemMinimumWidthHintChanged();
        /** Handles minimum height hint change. */
        void sltItemMinimumHeightHintChanged();
    /** @} */

protected:

    /** @name Event handling stuff.
      * @{ */
        /** Preprocesses Qt @a pEvent for passed @a pObject. */
        virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;
    /** @} */

private slots:

    /** @name Event handling stuff.
     * @{ */
        /** Handles request to commit data. */
        void sltHandleCommitData();

        /** Handles translation event. */
        void sltRetranslateUI();

        /** Handles tool label visibility change event. */
        void sltHandleToolLabelsVisibilityChange(bool fVisible);
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares scene. */
        void prepareScene();
        /** Prepares items. */
        void prepareItems();
        /** Prepare connections. */
        void prepareConnections();

        /** Loads current items from extra-data. */
        void loadCurrentItems();
        /** Saves current items to extra-data. */
        void saveCurrentItems();

        /** Cleanups items. */
        void cleanupItems();
        /** Cleanups scene. */
        void cleanupScene();
        /** Cleanups all. */
        void cleanup();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the tool class. */
        UIToolClass  m_enmClass;

        /** Holds the layout alignment. */
        Qt::Alignment  m_enmAlignment;

        /** Holds the view reference. */
        UIToolsView    *m_pView;
        /** Holds the scene reference. */
        QGraphicsScene *m_pScene;

        /** Holds whether items enabled. */
        bool  m_fItemsEnabled;

        /** Holds a map of restricted tool types. */
        QMap<UIToolClass, QList<UIToolType> >  m_mapRestrictedToolTypes;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Holds the root stack. */
        QList<UIToolsItem*>  m_items;

        /** Holds whether children should show names. */
        bool  m_fShowItemNames;
    /** @} */

    /** @name Selection stuff.
      * @{ */
        /** Holds the selected item map reference. */
        QMap<UIToolClass, QPointer<UIToolsItem> >  m_mapCurrentItems;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_tools_UIToolsModel_h */
