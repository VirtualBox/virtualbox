/* $Id: UIToolPaneMachine.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolPaneMachine class declaration.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIToolPaneMachine_h
#define FEQT_INCLUDED_SRC_manager_UIToolPaneMachine_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>
#include <QWidget>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* COM includes: */
#include "CMachine.h"

/* Forward declarations: */
class QHBoxLayout;
class QStackedLayout;
class QVBoxLayout;
class UIActionPool;
class UIDetails;
class UIErrorPane;
class UIVMActivityToolWidget;
class UISnapshotPane;
class UIVirtualMachineItem;
class UIVMLogViewerWidget;
class UIFileManager;

/** QWidget subclass representing container for tool panes. */
class UIToolPaneMachine : public QWidget
{
    Q_OBJECT;

signals:

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

    /** Notifies listeners about request to detach pane with tool type @enmToolType. */
    void sigDetachToolPane(UIToolType enmToolType);

public:

    /** Constructs tools pane passing @a pParent to the base-class. */
    UIToolPaneMachine(UIActionPool *pActionPool, QWidget *pParent = 0);
    /** Destructs tools pane. */
    virtual ~UIToolPaneMachine() RT_OVERRIDE;

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

    /** Defines error @a strDetails and switches to Error pane. */
    void setErrorDetails(const QString &strDetails);

    /** Defines the machine @a items. */
    void setItems(const QList<UIVirtualMachineItem*> &items);

    /** Returns whether current-state item of Snapshot pane is selected. */
    bool isCurrentStateItemSelected() const;

    /** Returns currently selected snapshot ID if any. */
    QUuid currentSnapshotId();

    /** Returns the help keyword of the current tool's widget. */
    QString currentHelpKeyword() const;

private slots:

    /** Handles the detach signals received from panes.*/
    void sltDetachToolPane();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares stacked-layout. */
    void prepareStackedLayout();
    /** Cleanups all. */
    void cleanup();

    /** Handles token change. */
    void handleTokenChange();

    /** Holds the action pool reference. */
    UIActionPool *m_pActionPool;

    /** Holds the stacked-layout instance. */
    QStackedLayout      *m_pLayout;
    /** Holds the Error pane instance. */
    UIErrorPane         *m_pPaneError;
    /** Holds the Details pane instance. */
    UIDetails           *m_pPaneDetails;
    /** Holds the Snapshots pane instance. */
    UISnapshotPane      *m_pPaneSnapshots;
    /** Holds the Logviewer pane instance. */
    UIVMLogViewerWidget *m_pPaneLogViewer;
    /** Holds the Performance Monitor pane instance. */
    UIVMActivityToolWidget *m_pPaneVMActivityMonitor;
    /** Holds the File Manager pane instance. */
    UIFileManager *m_pPaneFileManager;

    /** Holds whether this pane is active. */
    bool  m_fActive;

    /** Holds the cache of passed items. */
    QList<UIVirtualMachineItem*>  m_items;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIToolPaneMachine_h */
