/* $Id: UIToolPane.h 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolPane class declaration.
 */

/*
 * Copyright (C) 2017-2025 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIToolPane_h
#define FEQT_INCLUDED_SRC_manager_UIToolPane_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>
#include <QWidget>

/* GUI includes: */
#include "UIExtraDataDefs.h"
#include "UIManagerDefs.h"

/* Forward declarations: */
class QStackedLayout;
class UIActionPool;
class UICloudProfileManagerWidget;
class UIDetails;
class UIErrorPane;
class UIExtensionPackManagerWidget;
class UIFileManager;
class UIHomePane;
class UIMachineToolsWidget;
class UIMediumManagerWidget;
class UINetworkManagerWidget;
class UISnapshotPane;
class UIVMActivityOverviewWidget;
class UIVMActivityToolWidget;
class UIVMLogViewerWidget;
class UIVirtualMachineItem;
class UIVirtualMachineItemCloud;

/** QWidget subclass representing container for Global tool panes. */
class UIToolPane : public QWidget
{
    Q_OBJECT;

signals:

    /** @name Global tool stuff.
      * @{ */
        /** Notifies listeners about request to detach pane with tool type @enmToolType. */
        void sigDetachToolPane(UIToolType enmToolType);

        /** Notify listeners about home @a enmTask was requested. */
        void sigHomeTask(HomeTask enmTask);

        /** Notifies listeners about medium creation procedure was requested. */
        void sigCreateMedium();
        /** Notifies listeners about copy procedure was requested for medium with specified @a uMediumId. */
        void sigCopyMedium(const QUuid &uMediumId);

        /** Notifies listeners about request to switch to Activity pane of machine with @a uMachineId. */
        void sigSwitchToMachineActivityPane(const QUuid &uMachineId);
    /** @} */

    /** @name Machine tool stuff.
      * @{ */
        /** Redirects signal from UIVirtualBoxManager to UIDetails. */
        void sigToggleStarted();
        /** Redirects signal from UIVirtualBoxManager to UIDetails. */
        void sigToggleFinished();
        /** Redirects signal from UIDetails to UIVirtualBoxManager. */
        void sigLinkClicked(const QString &strCategory, const QString &strControl, const QUuid &uId);

        /** Notifies listeners about current Snapshot pane item change. */
        void sigCurrentSnapshotItemChange();

        /** Notifies listeners about request to switch to Activity Overview pane. */
        void sigSwitchToActivityOverviewPane();
    /** @} */

public:

    /** Constructs tool-pane passing @a pParent to the base-class.
      * @param  enmClass     Brings the tool-pane class.
      * @param  pActionPool  Brings the action-pool reference. */
    UIToolPane(QWidget *pParent, UIToolClass enmClass, UIActionPool *pActionPool);
    /** Destructs tool-pane. */
    virtual ~UIToolPane() RT_OVERRIDE;

    /** Returns the action-pool reference. */
    UIActionPool *actionPool() const { return m_pActionPool; }

    /** Defines whether this pane is @a fActive. */
    void setActive(bool fActive);
    /** Returns whether this pane is active. */
    bool active() const { return m_fActive; }

    /** Returns type of tool currently opened. */
    UIToolType currentTool() const;
    /** Returns whether tool of particular @a enmType is opened. */
    bool isToolOpened(UIToolType enmType) const;
    /** Activates tool of passed @a enmType, creates new one if necessary. */
    void openTool(UIToolType enmType);
    /** Closes tool of passed @a enmType, deletes one if exists. */
    void closeTool(UIToolType enmType);

    /** Returns the help keyword of the current tool's widget. */
    QString currentHelpKeyword() const;

    /** @name Global tool stuff.
      * @{ */
        /** Returns the Machine Tools Widget instance. */
        UIMachineToolsWidget *machineToolsWidget() const;
    /** @} */

    /** @name Machine tool stuff.
      * @{ */
        /** Defines error @a strDetails and switches to Error pane. */
        void setErrorDetails(const QString &strDetails);

        /** Defines the machine @a items. */
        void setItems(const QList<UIVirtualMachineItem*> &items);

        /** Returns whether current-state item of Snapshot pane is selected. */
        bool isCurrentStateItemSelected() const;
        /** Returns currently selected snapshot ID if any. */
        QUuid currentSnapshotId();
    /** @} */

    /** @name Manager tool stuff.
      * @{ */
        /** Defines the @a cloudItems. */
        void setCloudMachineItems(const QList<UIVirtualMachineItemCloud*> &cloudItems);
    /** @} */

private slots:

    /** @name Global tool stuff.
      * @{ */
        /** Handles the detach signals received from panes.*/
        void sltDetachToolPane();
    /** @} */

private:

    /** Prepares all. */
    void prepare();
    /** Prepares stacked-layout. */
    void prepareStackedLayout();
    /** Cleanups all. */
    void cleanup();

    /** Handles token change. */
    void handleTokenChange();

    /** Holds the tool-pane class. */
    UIToolClass   m_enmClass;
    /** Holds the action pool reference. */
    UIActionPool *m_pActionPool;

    /** Holds whether this pane is active. */
    bool  m_fActive;

    /** Holds the stacked-layout instance. */
    QStackedLayout *m_pLayout;

    /** @name Global tool stuff.
      * @{ */
        /** Holds the Home pane instance. */
        UIHomePane *m_pPaneHome;

        /** Holds the Machine Tools Widget instance. */
        UIMachineToolsWidget *m_pPaneMachines;
    /** @} */

    /** @name Machine tool stuff.
      * @{ */
        /** Holds the Error pane instance. */
        UIErrorPane            *m_pPaneError;
        /** Holds the Details pane instance. */
        UIDetails              *m_pPaneDetails;
        /** Holds the Snapshots pane instance. */
        UISnapshotPane         *m_pPaneSnapshots;
        /** Holds the Logviewer pane instance. */
        UIVMLogViewerWidget    *m_pPaneLogViewer;
        /** Holds the Performance Monitor pane instance. */
        UIVMActivityToolWidget *m_pPaneVMActivityMonitor;
        /** Holds the File Manager pane instance. */
        UIFileManager          *m_pPaneFileManager;

        /** Holds the cache of passed machine items. */
        QList<UIVirtualMachineItem*>  m_items;
    /** @} */

    /** @name Manager tool stuff.
      * @{ */
        /** Holds the Extension Pack Manager instance. */
        UIExtensionPackManagerWidget *m_pPaneExtensions;
        /** Holds the Virtual Media Manager instance. */
        UIMediumManagerWidget        *m_pPaneMedia;
        /** Holds the Network Manager instance. */
        UINetworkManagerWidget       *m_pPaneNetwork;
        /** Holds the Cloud Profile Manager instance. */
        UICloudProfileManagerWidget  *m_pPaneCloud;
        /** Holds the VM Activity Overview instance. */
        UIVMActivityOverviewWidget   *m_pPaneActivities;

        /** Holds the cache of passed cloud machine items. */
        QList<UIVirtualMachineItemCloud*>  m_cloudItems;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIToolPane_h */
