/* $Id: UIToolPaneGlobal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolPaneGlobal class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIToolPaneGlobal_h
#define FEQT_INCLUDED_SRC_manager_UIToolPaneGlobal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class QHBoxLayout;
class QStackedLayout;
class QUuid;
class QVBoxLayout;
class UIActionPool;
class UICloudProfileManagerWidget;
class UIExtensionPackManagerWidget;
class UIMediumManagerWidget;
class UINetworkManagerWidget;
class UIVMActivityOverviewWidget;
class UIVirtualMachineItemCloud;
class UIWelcomePane;
class CMachine;


/** QWidget subclass representing container for tool panes. */
class UIToolPaneGlobal : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about creation procedure was requested. */
    void sigCreateMedium();
    /** Notifies listeners about copy procedure was requested for medium with specified @a uMediumId. */
    void sigCopyMedium(const QUuid &uMediumId);

    /** Notifies listeners about request to switch to Activity pane of machine with @a uMachineId. */
    void sigSwitchToMachineActivityPane(const QUuid &uMachineId);

public:

    /** Constructs tools pane passing @a pParent to the base-class. */
    UIToolPaneGlobal(UIActionPool *pActionPool, QWidget *pParent = 0);
    /** Destructs tools pane. */
    virtual ~UIToolPaneGlobal() RT_OVERRIDE;

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

    /** Defines the @a cloudItems. */
    void setCloudMachineItems(const QList<UIVirtualMachineItemCloud*> &cloudItems);

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
    QStackedLayout               *m_pLayout;
    /** Holds the Welcome pane instance. */
    UIWelcomePane                *m_pPaneWelcome;
    /** Holds the Extension Pack Manager instance. */
    UIExtensionPackManagerWidget *m_pPaneExtensions;
    /** Holds the Virtual Media Manager instance. */
    UIMediumManagerWidget        *m_pPaneMedia;
    /** Holds the Network Manager instance. */
    UINetworkManagerWidget       *m_pPaneNetwork;
    /** Holds the Cloud Profile Manager instance. */
    UICloudProfileManagerWidget  *m_pPaneCloud;
    /** Holds the VM Activity Overview instance. */
    UIVMActivityOverviewWidget   *m_pPaneVMActivityOverview;

    /** Holds whether this pane is active. */
    bool  m_fActive;

    /** Holds the cache of passed cloud machine items. */
    QList<UIVirtualMachineItemCloud*>  m_cloudItems;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIToolPaneGlobal_h */
