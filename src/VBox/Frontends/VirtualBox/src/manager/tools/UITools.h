/* $Id: UITools.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITools class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_tools_UITools_h
#define FEQT_INCLUDED_SRC_manager_tools_UITools_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI icludes: */
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class QVBoxLayout;
class UIActionPool;
class UIToolsItem;
class UIToolsModel;
class UIToolsView;
class UIVirtualBoxManagerWidget;

/** QWidget extension used as VM Tools-pane. */
class UITools : public QWidget
{
    Q_OBJECT;

signals:

    /** @name General stuff.
      * @{ */
        /** Notifies listeners about selection changed.
          * @param  enmType  Brings current tool type. */
        void sigSelectionChanged(UIToolType enmType);

        /** Notifies listeners about expanding started. */
        void sigExpandingStarted();
        /** Notifies listeners about expanding finished. */
        void sigExpandingFinished();
    /** @} */

public:

    /** Constructs Tools-pane passing @a pParent to the base-class.
      * @param  Brings the tools class, it will be fixed one. */
    UITools(UIToolClass enmClass, UIVirtualBoxManagerWidget *pParent = 0);

    /** @name General stuff.
      * @{ */
        /** Returns the manager-widget reference. */
        UIVirtualBoxManagerWidget *managerWidget() const { return m_pManagerWidget; }

        /** Returns the action-pool reference. */
        UIActionPool *actionPool() const;

        /** Return the Tools-model instance. */
        UIToolsModel *model() const { return m_pToolsModel; }
        /** Return the Tools-view instance. */
        UIToolsView *view() const { return m_pToolsView; }

        /** Defines current tools @a enmType. */
        void setToolsType(UIToolType enmType);
        /** Returns current tools type. */
        UIToolType toolsType() const;

        /** Defines whether tool items @a fEnabled.*/
        void setItemsEnabled(bool fEnabled);
        /** Returns whether tool items enabled.*/
        bool isItemsEnabled() const;

        /** Defines restructed tool @a types. */
        void setRestrictedToolTypes(const QList<UIToolType> &types);
        /** Returns restricted tool types. */
        QList<UIToolType> restrictedToolTypes() const;
    /** @} */

    /** @name Current item stuff.
      * @{ */
        /** Returns current item. */
        UIToolsItem *currentItem() const;
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares contents. */
        void prepareContents();
        /** Prepares model. */
        void prepareModel();
        /** Prepares view. */
        void prepareView();
        /** Prepares connections. */
        void prepareConnections();
        /** Inits model. */
        void initModel();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the tools class. */
        const UIToolClass  m_enmClass;

        /** Holds the manager-widget reference. */
        UIVirtualBoxManagerWidget *m_pManagerWidget;

        /** Holds the main layout instane. */
        QVBoxLayout  *m_pMainLayout;
        /** Holds the Tools-model instane. */
        UIToolsModel *m_pToolsModel;
        /** Holds the Tools-view instane. */
        UIToolsView  *m_pToolsView;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_tools_UITools_h */
