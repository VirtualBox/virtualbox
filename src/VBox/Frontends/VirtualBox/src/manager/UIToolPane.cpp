/* $Id: UIToolPane.cpp 168993 2025-05-26 13:42:51Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolPane class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QStackedLayout>
#ifndef VBOX_WS_MAC
# include <QStyle>
#endif

/* GUI includes */
#include "UICloudProfileManager.h"
#include "UICommon.h"
#include "UIDetails.h"
#include "UIErrorPane.h"
#include "UIExtensionPackManager.h"
#include "UIFileManager.h"
#include "UIGlobalSession.h"
#include "UIHomePane.h"
#include "UIMachineToolsWidget.h"
#include "UIMediumManager.h"
#include "UINetworkManager.h"
#include "UISnapshotPane.h"
#include "UIToolPane.h"
#include "UIVirtualMachineItem.h"
#include "UIVMActivityOverviewWidget.h"
#include "UIVMActivityToolWidget.h"
#include "UIVMLogViewerWidget.h"

/* Other VBox includes: */
#include <iprt/assert.h>


UIToolPane::UIToolPane(QWidget *pParent, UIToolClass enmClass, UIActionPool *pActionPool)
    : QWidget(pParent)
    , m_enmClass(enmClass)
    , m_pActionPool(pActionPool)
    , m_fActive(false)
    , m_pLayout(0)
    , m_pPaneHome(0)
    , m_pPaneMachines(0)
    , m_pPaneExtensions(0)
    , m_pPaneMedia(0)
    , m_pPaneNetwork(0)
    , m_pPaneCloud(0)
    , m_pPaneActivities(0)
{
    prepare();
}

UIToolPane::~UIToolPane()
{
    cleanup();
}

void UIToolPane::setActive(bool fActive)
{
    /* Save activity: */
    if (m_fActive != fActive)
    {
        m_fActive = fActive;

        /* Handle token change: */
        handleTokenChange();
    }
}

UIToolType UIToolPane::currentTool() const
{
    return   m_pLayout && m_pLayout->currentWidget()
           ? m_pLayout->currentWidget()->property("ToolType").value<UIToolType>()
           : UIToolType_Invalid;
}

bool UIToolPane::isToolOpened(UIToolType enmType) const
{
    for (int iIndex = 0; iIndex < m_pLayout->count(); ++iIndex)
        if (m_pLayout->widget(iIndex)->property("ToolType").value<UIToolType>() == enmType)
            return true;
    return false;
}

void UIToolPane::openTool(UIToolType enmType)
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
            case UIToolType_Home:
            {
                /* Create Home pane: */
                m_pPaneHome = new UIHomePane;
                AssertPtrReturnVoid(m_pPaneHome);
                {
                    /* Configure pane: */
                    m_pPaneHome->setProperty("ToolType", QVariant::fromValue(UIToolType_Home));
                    connect(m_pPaneHome, &UIHomePane::sigHomeTask, this, &UIToolPane::sigHomeTask);

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneHome);
                    m_pLayout->setCurrentWidget(m_pPaneHome);
                }
                break;
            }
            case UIToolType_Machines:
            {
                /* Create Machine Tools Widget: */
                m_pPaneMachines = new UIMachineToolsWidget(this, m_pActionPool);
                AssertPtrReturnVoid(m_pPaneMachines);
                {
                    /* Configure pane: */
                    m_pPaneMachines->setProperty("ToolType", QVariant::fromValue(UIToolType_Machines));
                    /// @todo connect!
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneMachines->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneMachines);
                    m_pLayout->setCurrentWidget(m_pPaneMachines);
                }
                break;
            }
            case UIToolType_Extensions:
            {
                /* Create Extension Pack Manager: */
                m_pPaneExtensions = new UIExtensionPackManagerWidget(EmbedTo_Stack, m_pActionPool, false /* show toolbar */);
                AssertPtrReturnVoid(m_pPaneExtensions);
                {
                    /* Configure pane: */
                    m_pPaneExtensions->setProperty("ToolType", QVariant::fromValue(UIToolType_Extensions));
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneExtensions->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneExtensions);
                    m_pLayout->setCurrentWidget(m_pPaneExtensions);
                }
                break;
            }
            case UIToolType_Media:
            {
                /* Create Virtual Media Manager: */
                m_pPaneMedia = new UIMediumManagerWidget(EmbedTo_Stack, m_pActionPool, false /* show toolbar */);
                AssertPtrReturnVoid(m_pPaneMedia);
                {
                    /* Configure pane: */
                    m_pPaneMedia->setProperty("ToolType", QVariant::fromValue(UIToolType_Media));
                    connect(m_pPaneMedia, &UIMediumManagerWidget::sigCreateMedium,
                            this, &UIToolPane::sigCreateMedium);
                    connect(m_pPaneMedia, &UIMediumManagerWidget::sigCopyMedium,
                            this, &UIToolPane::sigCopyMedium);
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneMedia->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneMedia);
                    m_pLayout->setCurrentWidget(m_pPaneMedia);
                }
                break;
            }
            case UIToolType_Network:
            {
                /* Create Network Manager: */
                m_pPaneNetwork = new UINetworkManagerWidget(EmbedTo_Stack, m_pActionPool, false /* show toolbar */);
                AssertPtrReturnVoid(m_pPaneNetwork);
                {
                    /* Configure pane: */
                    m_pPaneNetwork->setProperty("ToolType", QVariant::fromValue(UIToolType_Network));
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneNetwork->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneNetwork);
                    m_pLayout->setCurrentWidget(m_pPaneNetwork);
                }
                break;
            }
            case UIToolType_Cloud:
            {
                /* Create Cloud Profile Manager: */
                m_pPaneCloud = new UICloudProfileManagerWidget(EmbedTo_Stack, m_pActionPool, false /* show toolbar */);
                AssertPtrReturnVoid(m_pPaneCloud);
                {
                    /* Configure pane: */
                    m_pPaneCloud->setProperty("ToolType", QVariant::fromValue(UIToolType_Cloud));
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneCloud->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneCloud);
                    m_pLayout->setCurrentWidget(m_pPaneCloud);
                }
                break;
            }
            case UIToolType_Resources:
            {
                /* Create VM Activity Overview: */
                m_pPaneActivities = new UIVMActivityOverviewWidget(EmbedTo_Stack, m_pActionPool, false /* show toolbar */);
                AssertPtrReturnVoid(m_pPaneActivities);
                {
                    /* Configure pane: */
                    m_pPaneActivities->setProperty("ToolType", QVariant::fromValue(UIToolType_Resources));
                    connect(m_pPaneActivities, &UIVMActivityOverviewWidget::sigSwitchToMachineActivityPane,
                            this, &UIToolPane::sigSwitchToMachineActivityPane);
                    m_pPaneActivities->setCloudMachineItems(m_cloudItems);
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneActivities->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneActivities);
                    m_pLayout->setCurrentWidget(m_pPaneActivities);
                }
                break;
            }
            case UIToolType_Error:
            {
                /* Create Error pane: */
                m_pPaneError = new UIErrorPane;
                AssertPtrReturnVoid(m_pPaneError);
                {
                    /* Configure pane: */
                    m_pPaneError->setProperty("ToolType", QVariant::fromValue(UIToolType_Error));
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneError->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

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
                    connect(this, &UIToolPane::sigToggleStarted,  m_pPaneDetails, &UIDetails::sigToggleStarted);
                    connect(this, &UIToolPane::sigToggleFinished, m_pPaneDetails, &UIDetails::sigToggleFinished);
                    connect(m_pPaneDetails, &UIDetails::sigLinkClicked,  this, &UIToolPane::sigLinkClicked);
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
                    /* Configure pane: */
                    m_pPaneSnapshots->setProperty("ToolType", QVariant::fromValue(UIToolType_Snapshots));
                    connect(m_pPaneSnapshots, &UISnapshotPane::sigCurrentItemChange,
                            this, &UIToolPane::sigCurrentSnapshotItemChange);
                    m_pPaneSnapshots->setMachineItems(m_items);
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneSnapshots->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneSnapshots);
                    m_pLayout->setCurrentWidget(m_pPaneSnapshots);
                }
                break;
            }
            case UIToolType_Logs:
            {
                /* Create Logviewer pane: */
                m_pPaneLogViewer = new UIVMLogViewerWidget(EmbedTo_Stack, m_pActionPool, false /* show toolbar */);
                AssertPtrReturnVoid(m_pPaneLogViewer);
                {
                    /* Configure pane: */
                    m_pPaneLogViewer->setProperty("ToolType", QVariant::fromValue(UIToolType_Logs));
                    connect(m_pPaneLogViewer, &UIVMLogViewerWidget::sigDetach,
                            this, &UIToolPane::sltDetachToolPane);
                    m_pPaneLogViewer->setSelectedVMListItems(m_items);
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneLogViewer->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneLogViewer);
                    m_pLayout->setCurrentWidget(m_pPaneLogViewer);
                }
                break;
            }
            case UIToolType_ResourceUse:
            {
                /* Create VM Activity Monitor pane: */
                m_pPaneVMActivityMonitor = new UIVMActivityToolWidget(EmbedTo_Stack, m_pActionPool,
                                                                      false /* Show toolbar */, 0 /* Parent */);
                AssertPtrReturnVoid(m_pPaneVMActivityMonitor);
                {
                    /* Configure pane: */
                    m_pPaneVMActivityMonitor->setProperty("ToolType", QVariant::fromValue(UIToolType_ResourceUse));
                    m_pPaneVMActivityMonitor->setSelectedVMListItems(m_items);
                    connect(m_pPaneVMActivityMonitor, &UIVMActivityToolWidget::sigSwitchToActivityOverviewPane,
                            this, &UIToolPane::sigSwitchToActivityOverviewPane);
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneVMActivityMonitor->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneVMActivityMonitor);
                    m_pLayout->setCurrentWidget(m_pPaneVMActivityMonitor);
                }
                break;
            }
            case UIToolType_FileManager:
            {
                /* Create File Manager pane: */
                if (!m_items.isEmpty())
                    m_pPaneFileManager = new UIFileManager(EmbedTo_Stack, m_pActionPool,
                                                           gpGlobalSession->virtualBox().FindMachine(m_items[0]->id().toString()),
                                                           0, false /* fShowToolbar */);
                else
                    m_pPaneFileManager = new UIFileManager(EmbedTo_Stack, m_pActionPool, CMachine(),
                                                           0, false /* fShowToolbar */);
                AssertPtrReturnVoid(m_pPaneFileManager);
                {
                    /* Configure pane: */
                    m_pPaneFileManager->setProperty("ToolType", QVariant::fromValue(UIToolType_FileManager));
                    m_pPaneFileManager->setSelectedVMListItems(m_items);
#ifndef VBOX_WS_MAC
                    const int iMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 4;
                    m_pPaneFileManager->setContentsMargins(iMargin, 0, iMargin, 0);
#endif

                    /* Add into layout: */
                    m_pLayout->addWidget(m_pPaneFileManager);
                    m_pLayout->setCurrentWidget(m_pPaneFileManager);
                }
                break;
            }
            default:
                AssertFailedReturnVoid();
        }
    }

    /* Handle token change: */
    handleTokenChange();
}

void UIToolPane::closeTool(UIToolType enmType)
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
            case UIToolType_Home:        m_pPaneHome = 0; break;
            case UIToolType_Machines:    m_pPaneMachines = 0; break;
            case UIToolType_Extensions:  m_pPaneExtensions = 0; break;
            case UIToolType_Media:       m_pPaneMedia = 0; break;
            case UIToolType_Network:     m_pPaneNetwork = 0; break;
            case UIToolType_Cloud:       m_pPaneCloud = 0; break;
            case UIToolType_Resources:   m_pPaneActivities = 0; break;
            case UIToolType_Error:       m_pPaneError = 0; break;
            case UIToolType_Details:     m_pPaneDetails = 0; break;
            case UIToolType_Snapshots:   m_pPaneSnapshots = 0; break;
            case UIToolType_Logs:        m_pPaneLogViewer = 0; break;
            case UIToolType_ResourceUse: m_pPaneVMActivityMonitor = 0; break;
            case UIToolType_FileManager: m_pPaneFileManager = 0; break;
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

QString UIToolPane::currentHelpKeyword() const
{
    QWidget *pCurrentToolWidget = 0;
    switch (currentTool())
    {
        case UIToolType_Home:
            pCurrentToolWidget = m_pPaneHome;
            break;
        case UIToolType_Machines:
            pCurrentToolWidget = m_pPaneMachines;
            break;
        case UIToolType_Extensions:
            pCurrentToolWidget = m_pPaneExtensions;
            break;
        case UIToolType_Media:
            pCurrentToolWidget = m_pPaneMedia;
            break;
        case UIToolType_Network:
            pCurrentToolWidget = m_pPaneNetwork;
            break;
        case UIToolType_Cloud:
            pCurrentToolWidget = m_pPaneCloud;
            break;
        case UIToolType_Resources:
            pCurrentToolWidget = m_pPaneActivities;
            break;
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
        case UIToolType_ResourceUse:
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

UIMachineToolsWidget *UIToolPane::machineToolsWidget() const
{
    return m_pPaneMachines;
}

void UIToolPane::setErrorDetails(const QString &strDetails)
{
    /* Update Error pane: */
    if (m_pPaneError)
        m_pPaneError->setErrorDetails(strDetails);
}

void UIToolPane::setItems(const QList<UIVirtualMachineItem*> &items)
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
    /* Update VM activity monitor pane if it is open: */
    if (isToolOpened(UIToolType_ResourceUse))
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

bool UIToolPane::isCurrentStateItemSelected() const
{
    return m_pPaneSnapshots ? m_pPaneSnapshots->isCurrentStateItemSelected() : false;
}

QUuid UIToolPane::currentSnapshotId()
{
    return m_pPaneSnapshots ? m_pPaneSnapshots->currentSnapshotId() : QUuid();
}

void UIToolPane::setCloudMachineItems(const QList<UIVirtualMachineItemCloud*> &cloudItems)
{
    /* Cache passed value: */
    m_cloudItems = cloudItems;

    /* Update VM Activity Overview pane if it is open: */
    if (isToolOpened(UIToolType_Resources))
    {
        AssertPtrReturnVoid(m_pPaneActivities);
        m_pPaneActivities->setCloudMachineItems(m_cloudItems);
    }
}

void UIToolPane::sltDetachToolPane()
{
    AssertPtrReturnVoid(sender());
    UIToolType enmToolType = UIToolType_Invalid;
    if (sender() == m_pPaneLogViewer)
        enmToolType = UIToolType_Logs;
    if (enmToolType != UIToolType_Invalid)
        emit sigDetachToolPane(enmToolType);
}

void UIToolPane::prepare()
{
    /* Create stacked-layout: */
    m_pLayout = new QStackedLayout(this);

    /* Open default tools: */
    switch (m_enmClass)
    {
        case UIToolClass_Global:
        {
            /* Create welcome pane: */
            openTool(UIToolType_Home);
            /* Create machines pane: */
            openTool(UIToolType_Machines);

            break;
        }
        case UIToolClass_Machine:
        {
            /* Create Details pane: */
            openTool(UIToolType_Details);

            break;
        }
        default:
            break;
    }
}

void UIToolPane::cleanup()
{
    /* Remove all widgets prematurelly: */
    while (m_pLayout->count())
    {
        QWidget *pWidget = m_pLayout->widget(0);
        m_pLayout->removeWidget(pWidget);
        delete pWidget;
    }
}

void UIToolPane::handleTokenChange()
{
    /* Determine whether VM Activity Overview pane is currently active tool: */
    if (m_pPaneActivities)
        m_pPaneActivities->setIsCurrentTool(m_fActive && currentTool() == UIToolType_Resources);
}
