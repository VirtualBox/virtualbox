/* $Id: UIToolPaneMachine.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolPaneMachine class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QStackedLayout>
#ifndef VBOX_WS_MAC
# include <QStyle>
#endif
#include <QUuid>

/* GUI includes */
#include "UIActionPoolManager.h"
#include "UICommon.h"
#include "UIDetails.h"
#include "UIErrorPane.h"
#include "UIFileManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UISnapshotPane.h"
#include "UIToolPaneMachine.h"
#include "UIVirtualMachineItem.h"
#include "UIVMActivityToolWidget.h"
#include "UIVMLogViewerWidget.h"


/* Other VBox includes: */
#include <iprt/assert.h>


UIToolPaneMachine::UIToolPaneMachine(UIActionPool *pActionPool, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pActionPool(pActionPool)
    , m_pLayout(0)
    , m_pPaneError(0)
    , m_pPaneDetails(0)
    , m_pPaneSnapshots(0)
    , m_pPaneLogViewer(0)
    , m_pPaneVMActivityMonitor(0)
    , m_pPaneFileManager(0)
    , m_fActive(false)
{
    /* Prepare: */
    prepare();
}

UIToolPaneMachine::~UIToolPaneMachine()
{
    /* Cleanup: */
    cleanup();
}

void UIToolPaneMachine::setActive(bool fActive)
{
    /* Save activity: */
    if (m_fActive != fActive)
    {
        m_fActive = fActive;

        /* Handle token change: */
        handleTokenChange();
    }
}

UIToolType UIToolPaneMachine::currentTool() const
{
    return   m_pLayout && m_pLayout->currentWidget()
           ? m_pLayout->currentWidget()->property("ToolType").value<UIToolType>()
           : UIToolType_Invalid;
}

bool UIToolPaneMachine::isToolOpened(UIToolType enmType) const
{
    /* Search through the stacked widgets: */
    for (int iIndex = 0; iIndex < m_pLayout->count(); ++iIndex)
        if (m_pLayout->widget(iIndex)->property("ToolType").value<UIToolType>() == enmType)
            return true;
    return false;
}

void UIToolPaneMachine::openTool(UIToolType enmType)
{
    /* Search through the stacked widgets: */
    int iActualIndex = -1;
    for (int iIndex = 0; iIndex < m_pLayout->count(); ++iIndex)
        if (m_pLayout->widget(iIndex)->property("ToolType").value<UIToolType>() == enmType)
            iActualIndex = iIndex;

    /* If widget with such type exists: */
    if (iActualIndex != -1)
    {
        /* Activate corresponding index: */
        m_pLayout->setCurrentIndex(iActualIndex);
    }
    /* Otherwise: */
    else
    {
        /* Create, remember, append corresponding stacked widget: */
        switch (enmType)
        {
            case UIToolType_Error:
            {
                /* Create Error pane: */
                m_pPaneError = new UIErrorPane;
                if (m_pPaneError)
                {
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneError->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Configure pane: */
                    m_pPaneError->setProperty("ToolType", QVariant::fromValue(UIToolType_Error));

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneError);
                    m_pLayout->setCurrentWidget(m_pPaneError);
                }
                break;
            }
            case UIToolType_Details:
            {
                /* Create Details pane: */
                m_pPaneDetails = new UIDetails;
                AssertPtrReturnVoid(m_pPaneDetails);
                {
                    /* Configure pane: */
                    m_pPaneDetails->setProperty("ToolType", QVariant::fromValue(UIToolType_Details));
                    connect(this, &UIToolPaneMachine::sigToggleStarted,  m_pPaneDetails, &UIDetails::sigToggleStarted);
                    connect(this, &UIToolPaneMachine::sigToggleFinished, m_pPaneDetails, &UIDetails::sigToggleFinished);
                    connect(m_pPaneDetails, &UIDetails::sigLinkClicked,  this, &UIToolPaneMachine::sigLinkClicked);
                    m_pPaneDetails->setItems(m_items);

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneDetails);
                    m_pLayout->setCurrentWidget(m_pPaneDetails);
                }
                break;
            }
            case UIToolType_Snapshots:
            {
                /* Create Snapshots pane: */
                m_pPaneSnapshots = new UISnapshotPane(m_pActionPool, false /* show toolbar? */);
                AssertPtrReturnVoid(m_pPaneSnapshots);
                {
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneSnapshots->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Configure pane: */
                    m_pPaneSnapshots->setProperty("ToolType", QVariant::fromValue(UIToolType_Snapshots));
                    connect(m_pPaneSnapshots, &UISnapshotPane::sigCurrentItemChange,
                            this, &UIToolPaneMachine::sigCurrentSnapshotItemChange);
                    m_pPaneSnapshots->setMachineItems(m_items);

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneSnapshots);
                    m_pLayout->setCurrentWidget(m_pPaneSnapshots);
                }
                break;
            }
            case UIToolType_Logs:
            {
                /* Create the Logviewer pane: */
                m_pPaneLogViewer = new UIVMLogViewerWidget(EmbedTo_Stack, m_pActionPool, false /* show toolbar */);
                AssertPtrReturnVoid(m_pPaneLogViewer);
                {
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneLogViewer->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Configure pane: */
                    m_pPaneLogViewer->setProperty("ToolType", QVariant::fromValue(UIToolType_Logs));
                    connect(m_pPaneLogViewer, &UIVMLogViewerWidget::sigDetach,
                            this, &UIToolPaneMachine::sltDetachToolPane);
                    m_pPaneLogViewer->setSelectedVMListItems(m_items);

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneLogViewer);
                    m_pLayout->setCurrentWidget(m_pPaneLogViewer);
                }
                break;
            }
            case UIToolType_VMActivity:
            {
                m_pPaneVMActivityMonitor = new UIVMActivityToolWidget(EmbedTo_Stack, m_pActionPool,
                                                                      false /* Show toolbar */, 0 /* Parent */);
                AssertPtrReturnVoid(m_pPaneVMActivityMonitor);
#ifndef VBOX_WS_MAC
                const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                m_pPaneVMActivityMonitor->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                /* Configure pane: */
                m_pPaneVMActivityMonitor->setProperty("ToolType", QVariant::fromValue(UIToolType_VMActivity));
                m_pPaneVMActivityMonitor->setSelectedVMListItems(m_items);
                /* Add into layout: */
                m_pLayout->addWidget(m_pPaneVMActivityMonitor);
                m_pLayout->setCurrentWidget(m_pPaneVMActivityMonitor);

                connect(m_pPaneVMActivityMonitor, &UIVMActivityToolWidget::sigSwitchToActivityOverviewPane,
                        this, &UIToolPaneMachine::sigSwitchToActivityOverviewPane);
                break;
            }
            case UIToolType_FileManager:
            {
                if (!m_items.isEmpty())
                    m_pPaneFileManager = new UIFileManager(EmbedTo_Stack, m_pActionPool,
                                                           gpGlobalSession->virtualBox().FindMachine(m_items[0]->id().toString()),
                                                           0, false /* fShowToolbar */);
                else
                    m_pPaneFileManager = new UIFileManager(EmbedTo_Stack, m_pActionPool, CMachine(),
                                                           0, false /* fShowToolbar */);
                AssertPtrReturnVoid(m_pPaneFileManager);
#ifndef VBOX_WS_MAC
                const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                m_pPaneFileManager->setContentsMargins(iMargin, 0, iMargin, 0);
#endif
                /* Configure pane: */
                m_pPaneFileManager->setProperty("ToolType", QVariant::fromValue(UIToolType_FileManager));
                m_pPaneFileManager->setSelectedVMListItems(m_items);
                /* Add into layout: */
                m_pLayout->addWidget(m_pPaneFileManager);
                m_pLayout->setCurrentWidget(m_pPaneFileManager);
                break;
            }
            default:
                AssertFailedReturnVoid();
        }
    }

    /* Handle token change: */
    handleTokenChange();
}

void UIToolPaneMachine::closeTool(UIToolType enmType)
{
    /* Search through the stacked widgets: */
    int iActualIndex = -1;
    for (int iIndex = 0; iIndex < m_pLayout->count(); ++iIndex)
        if (m_pLayout->widget(iIndex)->property("ToolType").value<UIToolType>() == enmType)
            iActualIndex = iIndex;

    /* If widget with such type doesn't exist: */
    if (iActualIndex != -1)
    {
        /* Forget corresponding widget: */
        switch (enmType)
        {
            case UIToolType_Error:       m_pPaneError = 0; break;
            case UIToolType_Details:     m_pPaneDetails = 0; break;
            case UIToolType_Snapshots:   m_pPaneSnapshots = 0; break;
            case UIToolType_Logs:        m_pPaneLogViewer = 0; break;
            case UIToolType_VMActivity:  m_pPaneVMActivityMonitor = 0; break;
            default: break;
        }
        /* Delete corresponding widget: */
        QWidget *pWidget = m_pLayout->widget(iActualIndex);
        m_pLayout->removeWidget(pWidget);
        delete pWidget;
    }

    /* Handle token change: */
    handleTokenChange();
}

void UIToolPaneMachine::setErrorDetails(const QString &strDetails)
{
    /* Update Error pane: */
    if (m_pPaneError)
        m_pPaneError->setErrorDetails(strDetails);
}

void UIToolPaneMachine::setItems(const QList<UIVirtualMachineItem*> &items)
{
    /* Cache passed value: */
    m_items = items;

    /* Update details pane if it is open: */
    if (isToolOpened(UIToolType_Details))
    {
        AssertPtrReturnVoid(m_pPaneDetails);
        m_pPaneDetails->setItems(m_items);
    }
    /* Update snapshots pane if it is open: */
    if (isToolOpened(UIToolType_Snapshots))
    {
        AssertPtrReturnVoid(m_pPaneSnapshots);
        m_pPaneSnapshots->setMachineItems(m_items);
    }
    /* Update logs pane if it is open: */
    if (isToolOpened(UIToolType_Logs))
    {
        AssertPtrReturnVoid(m_pPaneLogViewer);
        m_pPaneLogViewer->setSelectedVMListItems(m_items);
    }
    /* Update performance monitor pane if it is open: */
    if (isToolOpened(UIToolType_VMActivity))
    {
        AssertPtrReturnVoid(m_pPaneVMActivityMonitor);
        m_pPaneVMActivityMonitor->setSelectedVMListItems(m_items);
    }
    /* Update file manager pane if it is open: */
    if (isToolOpened(UIToolType_FileManager))
    {
        AssertPtrReturnVoid(m_pPaneFileManager);
        if (!m_items.isEmpty() && m_items[0])
            m_pPaneFileManager->setSelectedVMListItems(m_items);
    }
}

bool UIToolPaneMachine::isCurrentStateItemSelected() const
{
    return m_pPaneSnapshots ? m_pPaneSnapshots->isCurrentStateItemSelected() : false;
}

QUuid UIToolPaneMachine::currentSnapshotId()
{
    return m_pPaneSnapshots ? m_pPaneSnapshots->currentSnapshotId() : QUuid();
}

QString UIToolPaneMachine::currentHelpKeyword() const
{
    QWidget *pCurrentToolWidget = 0;
    switch (currentTool())
    {
        case UIToolType_Error:
            pCurrentToolWidget = m_pPaneError;
            break;
        case UIToolType_Details:
            pCurrentToolWidget = m_pPaneDetails;
            break;
        case UIToolType_Snapshots:
            pCurrentToolWidget = m_pPaneSnapshots;
            break;
        case UIToolType_Logs:
            pCurrentToolWidget = m_pPaneLogViewer;
            break;
        case UIToolType_VMActivity:
            pCurrentToolWidget = m_pPaneVMActivityMonitor;
            break;
        case UIToolType_FileManager:
            pCurrentToolWidget = m_pPaneFileManager;
            break;
        default:
            break;
    }
    return uiCommon().helpKeyword(pCurrentToolWidget);
}

void UIToolPaneMachine::prepare()
{
    /* Create stacked-layout: */
    m_pLayout = new QStackedLayout(this);

    /* Create Details pane: */
    openTool(UIToolType_Details);
}

void UIToolPaneMachine::cleanup()
{
    /* Remove all widgets prematurelly: */
    while (m_pLayout->count())
    {
        QWidget *pWidget = m_pLayout->widget(0);
        m_pLayout->removeWidget(pWidget);
        delete pWidget;
    }
}

void UIToolPaneMachine::handleTokenChange()
{
    // printf("UIToolPaneMachine::handleTokenChange: Active = %d, current tool = %d\n", m_fActive, currentTool());
}

void UIToolPaneMachine::sltDetachToolPane()
{
    AssertPtrReturnVoid(sender());
    UIToolType enmToolType = UIToolType_Invalid;
    if (sender() == m_pPaneLogViewer)
        enmToolType = UIToolType_Logs;

    if (enmToolType != UIToolType_Invalid)
        emit sigDetachToolPane(enmToolType);
}
