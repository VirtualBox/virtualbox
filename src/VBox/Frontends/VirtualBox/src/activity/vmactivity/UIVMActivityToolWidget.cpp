/* $Id: UIVMActivityToolWidget.cpp 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMActivityToolWidget class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QVBoxLayout>
#include <QStyle>

/* GUI includes: */
#include "QIToolBar.h"
#include "UIActionPoolManager.h"
#include "UICommon.h"
#include "UIVMActivityMonitor.h"
#include "UIVMActivityToolWidget.h"
#include "UIMessageCenter.h"
#include "UIVirtualMachineItem.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"
#include "UIVMActivityMonitorContainer.h"
#ifdef VBOX_WS_MAC
# include "UIWindowMenuManager.h"
#endif /* VBOX_WS_MAC */

/* COM includes: */
#include "CMachine.h"

UIVMActivityToolWidget::UIVMActivityToolWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                                                 bool fShowToolbar /* = true */, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pActionPool(pActionPool)
    , m_fShowToolbar(fShowToolbar)
    , m_pToolBar(0)
    , m_pMonitorContainer(0)
{
    uiCommon().setHelpKeyword(this, "tk_vm-activity-session-information" /* help keyword */);
    prepare();
    prepareActions();
    prepareToolBar();
}


QMenu *UIVMActivityToolWidget::menu() const
{
    return NULL;
}

bool UIVMActivityToolWidget::isCurrentTool() const
{
    return m_fIsCurrentTool;
}

void UIVMActivityToolWidget::setIsCurrentTool(bool fIsCurrentTool)
{
    m_fIsCurrentTool = fIsCurrentTool;
}

void UIVMActivityToolWidget::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    m_pMonitorContainer = new UIVMActivityMonitorContainer(this, m_pActionPool, m_enmEmbedding);
    pMainLayout->addWidget(m_pMonitorContainer);
}

void UIVMActivityToolWidget::setSelectedVMListItems(const QList<UIVirtualMachineItem*> &items)
{
    setMachines(items);
}

void UIVMActivityToolWidget::setMachines(const QList<UIVirtualMachineItem*> &machines)
{
    QVector<QUuid> machineIds;
    foreach (const UIVirtualMachineItem* pMachine, machines)
    {
        if (!pMachine)
            continue;
        machineIds << pMachine->id();
    }
    /* List of machines that are newly added to selected machine list: */
    QList<UIVirtualMachineItem*> newSelections;
    QVector<QUuid> unselectedMachines(m_machineIds);

    foreach (UIVirtualMachineItem* pMachine, machines)
    {
        if (!pMachine)
            continue;
        QUuid id = pMachine->id();
        unselectedMachines.removeAll(id);
        if (!m_machineIds.contains(id))
            newSelections << pMachine;
    }
    m_machineIds = machineIds;

    m_pMonitorContainer->removeTabs(unselectedMachines);
    addTabs(newSelections);
}

void UIVMActivityToolWidget::prepareActions()
{
    QAction *pToResourcesAction =
        m_pActionPool->action(UIActionIndex_M_Activity_S_ToVMActivityOverview);
    if (pToResourcesAction)
        connect(pToResourcesAction, &QAction::triggered, this, &UIVMActivityToolWidget::sigSwitchToActivityOverviewPane);
}

void UIVMActivityToolWidget::prepareToolBar()
{
    /* Create toolbar: */
    m_pToolBar = new QIToolBar(parentWidget());
    AssertPtrReturnVoid(m_pToolBar);
    {
        /* Configure toolbar: */
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

#ifdef VBOX_WS_MAC
        /* Check whether we are embedded into a stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Add into layout: */
            layout()->addWidget(m_pToolBar);
        }
#else
        /* Add into layout: */
        layout()->addWidget(m_pToolBar);
#endif
    }
}

void UIVMActivityToolWidget::addTabs(const QList<UIVirtualMachineItem*> &machines)
{
    AssertReturnVoid(m_pMonitorContainer);
    foreach (UIVirtualMachineItem* pMachine, machines)
    {
        if (!pMachine)
            continue;
        if (pMachine->toLocal())
        {
            CMachine comMachine = pMachine->toLocal()->machine();
            if (!comMachine.isOk())
                continue;
            m_pMonitorContainer->addLocalMachine(comMachine);
            continue;
        }
        if (pMachine->toCloud())
        {
            CCloudMachine comMachine = pMachine->toCloud()->machine();
            if (!comMachine.isOk())
                continue;
            m_pMonitorContainer->addCloudMachine(comMachine);
            continue;
        }
    }
}
