/* $Id: UIVMInformationDialog.h 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMInformationDialog class declaration.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_runtime_information_UIVMInformationDialog_h
#define FEQT_INCLUDED_SRC_runtime_information_UIVMInformationDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMainWindow>
#include <QUuid>

/* GUI includes: */
#include "QIWithRestorableGeometry.h"

/* COM includes: */
#include "KMachineState.h"

/* Forward declarations: */
class QITabWidget;
class QIDialogButtonBox;
class UIActionPool;

/* Type definitions: */
typedef QIWithRestorableGeometry<QMainWindow> QMainWindowWithRestorableGeometry;

/** QMainWindow subclass providing user
  * with the dialog unifying VM details and statistics. */
class UIVMInformationDialog : public QMainWindowWithRestorableGeometry
{
    Q_OBJECT;

signals:

    void sigClose();

public:

    /** Constructs information dialog. */
    UIVMInformationDialog(UIActionPool *pActionPool);

    /** Returns whether the dialog should be maximized when geometry being restored. */
    virtual bool shouldBeMaximized() const RT_OVERRIDE;

protected:

    virtual void closeEvent(QCloseEvent *pEvent) RT_OVERRIDE;
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();
    /** Handles tab-widget page change. */
    void sltHandlePageChanged(int iIndex);
    void sltMachineStateChange(const QUuid &uMachineId, const KMachineState state);
    void sltAdditionsStateChange();

private:

    enum Tabs
    {
        Tabs_ConfigurationDetails = 0,
        Tabs_RuntimeInformation,
        Tabs_ActivityMonitor,
        Tabs_GuestControl
    };
    /** Prepares all. */
    void prepare();
    /** Prepares central-widget. */
    void prepareCentralWidget();
    /** Prepares tab-widget. */
    void prepareTabWidget();
    /** Prepares tab with @a iTabIndex. */
    void prepareTab(int iTabIndex);
    /** Prepares button-box. */
    void prepareButtonBox();
    /** Prepares connections: */
    void prepareConnections();
    void loadDialogGeometry();
    void saveDialogGeometry();

    /** @name Widget variables.
     * @{ */
       /** Holds the dialog tab-widget instance. */
       QITabWidget                  *m_pTabWidget;
       /** Holds the map of dialog tab instances. */
       QMap<int, QWidget*>           m_tabs;
       /** Holds the dialog button-box instance. */
       QIDialogButtonBox            *m_pButtonBox;
    /** @} */
    bool m_fCloseEmitted;
    int m_iGeometrySaveTimerId;
    QUuid m_uMachineId;
    QString m_strMachineName;
    UIActionPool *m_pActionPool;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_information_UIVMInformationDialog_h */
