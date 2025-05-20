/* $Id: UIGlobalToolsWidget.h 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalToolsWidget class declaration.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIGlobalToolsWidget_h
#define FEQT_INCLUDED_SRC_manager_UIGlobalToolsWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class QGridLayout;
class QIToolBar;
class UIActionPool;
class UIChooser;
class UIMachineToolsWidget;
class UIToolPane;
class UITools;

/** QWidget extension used as Global Tools Widget instance. */
class UIGlobalToolsWidget : public QWidget
{
    Q_OBJECT;

signals:

    /** @name Tools pane stuff.
      * @{ */
        /** Notifies about required tool menu update. */
        void sigToolMenuUpdate();

        /** Notifies about Tool type change. */
        void sigToolTypeChange();
    /** @} */

public:

    /** Constructs Global Tools Widget passing @a pParent to the base-class.
      * @param  pActionPool  Brings the action-pool reference.  */
    UIGlobalToolsWidget(QWidget *pParent, UIActionPool *pActionPool);

    /** @name Tool-bar stuff.
      * @{ */
        /** Adds tool-bar into grid layout. */
        void addToolBar(QIToolBar *pToolBar);
    /** @} */

    /** @name Tools pane stuff.
      * @{ */
        /** Returns tool-pane instance. */
        UIToolPane *toolPane() const;
        /** Returns Machine Tools Widget reference. */
        UIMachineToolsWidget *machineToolsWidget() const;

        /** Returns menu tool type for the @a enmClass specified. */
        UIToolType menuToolType(UIToolClass enmClass) const;
        /** Defines menu tool @a enmType. */
        void setMenuToolType(UIToolType enmType);

        /** Returns pane tool type. */
        UIToolType toolType() const;
        /** Returns whether pane has tool of passed @a enmType. */
        bool isToolOpened(UIToolType enmType) const;
        /** Switches pane to passed tool @a enmType. */
        void switchToolTo(UIToolType enmType);
        /** Closes pane tool of passed @a enmType. */
        void closeTool(UIToolType enmType);
    /** @} */

    /** @name Help browser stuff.
      * @{ */
        /** Returns the current help key word. */
        QString currentHelpKeyword() const;
    /** @} */

private slots:

    /** @name General stuff.
      * @{ */
        /** Handles request to commit data. */
        void sltHandleCommitData();
    /** @} */

    /** @name COM event handling stuff.
      * @{ */
        /** Handles signal about machine registration change.
          * @param  uId          Brings [un]registered machine id.
          * @param  fRegistered  Brings whether machine was registered, unregistered otherwise. */
        void sltHandleMachineRegistrationChanged(const QUuid &uId, const bool fRegistered);

        /** Handles signal about settings expert mode change. */
        void sltHandleSettingsExpertModeChange();
    /** @} */

    /** @name Chooser pane stuff.
      * @{ */
        /** Handles Chooser-pane navigation list change. */
        void sltHandleChooserPaneNavigationListChange();

        /** Handles state change for cloud profile with certain @a strProviderShortName and @a strProfileName. */
        void sltHandleCloudProfileStateChange(const QString &strProviderShortName,
                                              const QString &strProfileName);
    /** @} */

    /** @name Tools pane stuff.
      * @{ */
        /** Handles request for tool menu update. */
        void sltHandleToolMenuUpdate();

        /** Handles signal about Tools-menu index change.
          * @param  enmType  Brings current tool type. */
        void sltHandleToolsMenuIndexChange(UIToolType enmType);

        /** Handles signal requesting switch to Activities tool. */
        void sltSwitchToActivitiesTool();
        /** Handles signal requesting switch to VM Activity tool. */
        void sltSwitchToVMActivityTool(const QUuid &uMachineId);
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares widgets. */
        void prepareWidgets();
        /** Prepares connections. */
        void prepareConnections();
        /** Loads settings. */
        void loadSettings();

        /** Cleanups connections. */
        void cleanupConnections();
    /** @} */

    /** @name Common stuff.
      * @{ */
        /** Returns the action-pool reference. */
        UIActionPool *actionPool() const { return m_pActionPool; }
    /** @} */

    /** @name Tools stuff.
      * @{ */
        /** Returns tool-menu instance. */
        UITools *toolMenu() const;
        /** Returns Machine Tools Widget's chooser-pane reference. */
        UIChooser *chooser() const;
        /** Returns Machine Tools Widget's tool-pane instance. */
        UIToolPane *toolPaneMachine() const;
    /** @} */

    /** Holds the action-pool reference. */
    UIActionPool *m_pActionPool;

    /** Holds the grid-layout instance. */
    QGridLayout *m_pLayout;

    /** Holds the tools-menu instance. */
    UITools    *m_pMenu;
    /** Holds the tool-pane instance. */
    UIToolPane *m_pPane;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIGlobalToolsWidget_h */
