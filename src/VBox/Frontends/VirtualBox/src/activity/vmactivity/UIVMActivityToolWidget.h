/* $Id: UIVMActivityToolWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMActivityToolWidget class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityToolWidget_h
#define FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityToolWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QColor>
#include <QWidget>
#include <QUuid>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QIToolBar;
class UIActionPool;
class UIVirtualMachineItem;
class UIVMActivityMonitorContainer;

/** QTabWidget extension host machine activity widget(s) in the Manager UI. */
class UIVMActivityToolWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigSwitchToActivityOverviewPane();

public:

    UIVMActivityToolWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                               bool fShowToolbar = true, QWidget *pParent = 0);
    QMenu *menu() const;

    bool isCurrentTool() const;
    void setIsCurrentTool(bool fIsCurrentTool);

    void setSelectedVMListItems(const QList<UIVirtualMachineItem*> &items);

#ifdef VBOX_WS_MAC
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif

private:

    void setMachines(const QList<UIVirtualMachineItem*> &machines);
    /** @name Prepare/cleanup cascade.
      * @{ */
        void prepare();
        void prepareToolBar();
        void prepareActions();
        void updateColumnsMenu();
    /** @} */

    /** Add new tabs for each QUuid in @machineIdsToAdd. Does not check for duplicates. */
    void addTabs(const QList<UIVirtualMachineItem*> &machines);
    void setExportActionEnabled(bool fEnabled);

    /** @name General variables.
      * @{ */
        const EmbedTo m_enmEmbedding;
        UIActionPool *m_pActionPool;
        const bool    m_fShowToolbar;
    /** @} */

    QIToolBar *m_pToolBar;
    /** Indicates if this widget's host tool is current tool. */
    bool    m_fIsCurrentTool;
    QVector<QUuid> m_machineIds;
    UIVMActivityMonitorContainer *m_pMonitorContainer;
};


#endif /* !FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityToolWidget_h */
