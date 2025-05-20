/* $Id: UIVirtualBoxWidget.cpp 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxWidget class implementation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIToolBar.h"
#include "UIActionPoolManager.h"
#include "UIChooser.h"
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIGlobalToolsWidget.h"
#include "UIMachineToolsWidget.h"
#include "UINotificationCenter.h"
#include "UIToolPane.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManager.h"
#include "UIVirtualBoxWidget.h"
#include "UIVirtualMachineItem.h"
#if defined(VBOX_WS_MAC) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32))
# include "UIGlobalSession.h"
# include "UIIconPool.h"
# include "UIVersion.h"
#endif /* VBOX_WS_MAC && (RT_ARCH_ARM64 || RT_ARCH_ARM32) */

/* COM includes: */
#if defined(VBOX_WS_MAC) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32))
# include "CSystemProperties.h"
#endif /* VBOX_WS_MAC && (RT_ARCH_ARM64 || RT_ARCH_ARM32) */


UIVirtualBoxWidget::UIVirtualBoxWidget(UIVirtualBoxManager *pParent)
    : m_pActionPool(pParent->actionPool())
    , m_pToolBar(0)
    , m_pGlobalToolsWidget(0)
{
    prepare();
}

UIVirtualBoxWidget::~UIVirtualBoxWidget()
{
    cleanup();
}

UIVirtualMachineItem *UIVirtualBoxWidget::currentItem() const
{
    AssertPtrReturn(chooser(), 0);
    return chooser()->currentItem();
}

QList<UIVirtualMachineItem*> UIVirtualBoxWidget::currentItems() const
{
    AssertPtrReturn(chooser(), QList<UIVirtualMachineItem*>());
    return chooser()->currentItems();
}

bool UIVirtualBoxWidget::isGroupItemSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isGroupItemSelected();
}

bool UIVirtualBoxWidget::isMachineItemSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isMachineItemSelected();
}

bool UIVirtualBoxWidget::isLocalMachineItemSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isLocalMachineItemSelected();
}

bool UIVirtualBoxWidget::isCloudMachineItemSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isCloudMachineItemSelected();
}

bool UIVirtualBoxWidget::isSingleGroupSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isSingleGroupSelected();
}

bool UIVirtualBoxWidget::isSingleLocalGroupSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isSingleLocalGroupSelected();
}

bool UIVirtualBoxWidget::isSingleCloudProviderGroupSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isSingleCloudProviderGroupSelected();
}

bool UIVirtualBoxWidget::isSingleCloudProfileGroupSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isSingleCloudProfileGroupSelected();
}

bool UIVirtualBoxWidget::isAllItemsOfOneGroupSelected() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isAllItemsOfOneGroupSelected();
}

QString UIVirtualBoxWidget::fullGroupName() const
{
    AssertPtrReturn(chooser(), QString());
    return chooser()->fullGroupName();
}

bool UIVirtualBoxWidget::isGroupSavingInProgress() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isGroupSavingInProgress();
}

bool UIVirtualBoxWidget::isCloudProfileUpdateInProgress() const
{
    AssertPtrReturn(chooser(), false);
    return chooser()->isCloudProfileUpdateInProgress();
}

void UIVirtualBoxWidget::openGroupNameEditor()
{
    AssertPtrReturnVoid(chooser());
    chooser()->openGroupNameEditor();
}

void UIVirtualBoxWidget::disbandGroup()
{
    AssertPtrReturnVoid(chooser());
    chooser()->disbandGroup();
}

void UIVirtualBoxWidget::removeMachine()
{
    AssertPtrReturnVoid(chooser());
    chooser()->removeMachine();
}

void UIVirtualBoxWidget::moveMachineToGroup(const QString &strName /* = QString() */)
{
    AssertPtrReturnVoid(chooser());
    chooser()->moveMachineToGroup(strName);
}

QStringList UIVirtualBoxWidget::possibleGroupsForMachineToMove(const QUuid &uId)
{
    AssertPtrReturn(chooser(), QStringList());
    return chooser()->possibleGroupsForMachineToMove(uId);
}

QStringList UIVirtualBoxWidget::possibleGroupsForGroupToMove(const QString &strFullName)
{
    AssertPtrReturn(chooser(), QStringList());
    return chooser()->possibleGroupsForGroupToMove(strFullName);
}

void UIVirtualBoxWidget::refreshMachine()
{
    AssertPtrReturnVoid(chooser());
    chooser()->refreshMachine();
}

void UIVirtualBoxWidget::sortGroup()
{
    AssertPtrReturnVoid(chooser());
    chooser()->sortGroup();
}

void UIVirtualBoxWidget::setMachineSearchWidgetVisibility(bool fVisible)
{
    AssertPtrReturnVoid(chooser());
    chooser()->setMachineSearchWidgetVisibility(fVisible);
}

void UIVirtualBoxWidget::setToolsTypeGlobal(UIToolType enmType)
{
    AssertPtrReturnVoid(globalToolsWidget());
    globalToolsWidget()->setMenuToolType(enmType);
}

UIToolType UIVirtualBoxWidget::toolsTypeGlobal() const
{
    AssertPtrReturn(globalToolsWidget(), UIToolType_Invalid);
    return globalToolsWidget()->menuToolType(UIToolClass_Global);
}

void UIVirtualBoxWidget::setToolsTypeMachine(UIToolType enmType)
{
    AssertPtrReturnVoid(machineToolsWidget());
    machineToolsWidget()->setMenuToolType(enmType);
}

UIToolType UIVirtualBoxWidget::toolsTypeMachine() const
{
    AssertPtrReturn(machineToolsWidget(), UIToolType_Invalid);
    return machineToolsWidget()->menuToolType(UIToolClass_Machine);
}

UIToolType UIVirtualBoxWidget::currentGlobalTool() const
{
    AssertPtrReturn(globalToolsWidget(), UIToolType_Invalid);
    return globalToolsWidget()->toolType();
}

UIToolType UIVirtualBoxWidget::currentMachineTool() const
{
    AssertPtrReturn(machineToolsWidget(), UIToolType_Invalid);
    return machineToolsWidget()->toolType();
}

bool UIVirtualBoxWidget::isGlobalToolOpened(UIToolType enmType) const
{
    AssertPtrReturn(globalToolsWidget(), false);
    return globalToolsWidget()->isToolOpened(enmType);
}

bool UIVirtualBoxWidget::isMachineToolOpened(UIToolType enmType) const
{
    AssertPtrReturn(machineToolsWidget(), false);
    return machineToolsWidget()->isToolOpened(enmType);
}

void UIVirtualBoxWidget::closeGlobalTool(UIToolType enmType)
{
    AssertPtrReturnVoid(globalToolsWidget());
    globalToolsWidget()->closeTool(enmType);
}

void UIVirtualBoxWidget::closeMachineTool(UIToolType enmType)
{
    AssertPtrReturnVoid(machineToolsWidget());
    machineToolsWidget()->closeTool(enmType);
}

bool UIVirtualBoxWidget::isCurrentStateItemSelected() const
{
    AssertPtrReturn(machineToolPane(), false);
    return machineToolPane()->isCurrentStateItemSelected();
}

QUuid UIVirtualBoxWidget::currentSnapshotId()
{
    AssertPtrReturn(machineToolPane(), QUuid());
    return machineToolPane()->currentSnapshotId();
}

QString UIVirtualBoxWidget::currentHelpKeyword() const
{
    AssertPtrReturn(globalToolsWidget(), QString());
    return globalToolsWidget()->currentHelpKeyword();
}

void UIVirtualBoxWidget::sltHandleToolBarContextMenuRequest(const QPoint &position)
{
    /* Populate toolbar actions: */
    QList<QAction*> actions;
    /* Add 'Show Toolbar Text' action: */
    QAction *pShowToolBarText = new QAction(UIVirtualBoxManager::tr("Show Toolbar Text"), 0);
    if (pShowToolBarText)
    {
        pShowToolBarText->setCheckable(true);
        pShowToolBarText->setChecked(m_pToolBar->toolButtonStyle() == Qt::ToolButtonTextUnderIcon);
        actions << pShowToolBarText;
    }

    /* Prepare the menu position: */
    QPoint globalPosition = position;
    QWidget *pSender = qobject_cast<QWidget*>(sender());
    if (pSender)
        globalPosition = pSender->mapToGlobal(position);

    /* Execute the menu: */
    QAction *pResult = QMenu::exec(actions, globalPosition);

    /* Handle the menu execution result: */
    if (pResult == pShowToolBarText)
    {
        m_pToolBar->setUseTextLabels(pResult->isChecked());
        gEDataManager->setSelectorWindowToolBarTextVisible(pResult->isChecked());
    }
}

void UIVirtualBoxWidget::sltHandleCommitData()
{
    cleanupConnections();
}

void UIVirtualBoxWidget::sltHandleMachineStateChange(const QUuid &uId)
{
    /* Update toolbar to show/hide corresponding actions: */
    if (currentItem()->id() == uId)
        updateToolbar();
}

void UIVirtualBoxWidget::sltUpdateToolbar()
{
    /* Update toolbar to show/hide corresponding actions: */
    updateToolbar();
}

void UIVirtualBoxWidget::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Load settings: */
    loadSettings();
}

void UIVirtualBoxWidget::prepareWidgets()
{
    /* Create layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if(pLayout)
    {
        /* Configure layout: */
        pLayout->setContentsMargins(0, 0, 0, 0);
        pLayout->setSpacing(0);

        /* Create Global Tools Widget: */
        m_pGlobalToolsWidget = new UIGlobalToolsWidget(this, actionPool());
        if (globalToolsWidget())
        {
            /* Add into layout: */
            pLayout->addWidget(globalToolsWidget());
        }

        /* Create Main toolbar: */
        m_pToolBar = new QIToolBar(this);
        if (m_pToolBar)
        {
            /* Configure toolbar: */
            const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);
            m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
            m_pToolBar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            m_pToolBar->setContextMenuPolicy(Qt::CustomContextMenu);
            m_pToolBar->setUseTextLabels(true);

#if defined(VBOX_WS_MAC) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32))
            /* Check whether we should show Dev Preview tag: */
            bool fShowDevPreviewTag = false;
            const CVirtualBox comVBox = gpGlobalSession->virtualBox();
            if (comVBox.isNotNull())
            {
                const CSystemProperties comSystemProps = comVBox.GetSystemProperties();
                if (comVBox.isOk() && comSystemProps.isNotNull())
                    fShowDevPreviewTag =
                        comSystemProps.GetSupportedPlatformArchitectures().contains(KPlatformArchitecture_x86);
            }
            /* Enable Dev Preview tag: */
            if (fShowDevPreviewTag)
            {
                m_pToolBar->emulateMacToolbar();
                m_pToolBar->enableBranding(UIIconPool::iconSet(":/explosion_hazard_32px.png"),
                                           "Dev Preview", // do we need to make it NLS?
                                           QColor(246, 179, 0),
                                           74 /* width of BETA label */);
            }
#endif /* VBOX_WS_MAC && (RT_ARCH_ARM64 || RT_ARCH_ARM32) */

            /* Add toolbar into layout: */
            m_pGlobalToolsWidget->addToolBar(m_pToolBar);
        }
    }

    /* Create notification-center: */
    UINotificationCenter::create(this);

    /* Update toolbar finally: */
    updateToolbar();

    /* Bring the VM list to the focus: */
    chooser()->setFocus();
}

void UIVirtualBoxWidget::prepareConnections()
{
    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIVirtualBoxWidget::sltHandleCommitData);

    /* Global COM event handlers: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIVirtualBoxWidget::sltHandleMachineStateChange);
    connect(gEDataManager, &UIExtraDataManager::sigSettingsExpertModeChange,
            this, &UIVirtualBoxWidget::sltUpdateToolbar);

    /* Tool-bar connections: */
    connect(m_pToolBar, &QIToolBar::customContextMenuRequested,
            this, &UIVirtualBoxWidget::sltHandleToolBarContextMenuRequest);

    /* Global Tools Widget connections: */
    connect(globalToolsWidget(), &UIGlobalToolsWidget::sigToolTypeChange,
            this, &UIVirtualBoxWidget::sltUpdateToolbar);
    connect(globalToolsWidget(), &UIGlobalToolsWidget::sigToolTypeChange,
            this, &UIVirtualBoxWidget::sigToolTypeChangeGlobal);
    /* Global Tool Pane connections: */
    connect(globalToolPane(), &UIToolPane::sigHomeTask,
            this, &UIVirtualBoxWidget::sigHomeTask);
    connect(globalToolPane(), &UIToolPane::sigCreateMedium,
            this, &UIVirtualBoxWidget::sigCreateMedium);
    connect(globalToolPane(), &UIToolPane::sigCopyMedium,
            this, &UIVirtualBoxWidget::sigCopyMedium);

    /* Machine Tools Widget connections: */
    connect(machineToolsWidget(), &UIMachineToolsWidget::sigToolTypeChange,
            this, &UIVirtualBoxWidget::sltUpdateToolbar);
    connect(machineToolsWidget(), &UIMachineToolsWidget::sigToolTypeChange,
            this, &UIVirtualBoxWidget::sigToolTypeChangeMachine);
    connect(machineToolsWidget(), &UIMachineToolsWidget::sigChooserPaneSelectionChange,
            this, &UIVirtualBoxWidget::sigChooserPaneSelectionChange);
    connect(machineToolsWidget(), &UIMachineToolsWidget::sigChooserPaneSelectionClassChange,
            this, &UIVirtualBoxWidget::sltUpdateToolbar);
    connect(machineToolsWidget(), &UIMachineToolsWidget::sigCloudMachineStateChange,
            this, &UIVirtualBoxWidget::sigCloudMachineStateChange);
    /* Machine Tool Pane connections: */
    connect(machineToolPane(), &UIToolPane::sigLinkClicked,
            this, &UIVirtualBoxWidget::sigMachineSettingsLinkClicked);
    connect(machineToolPane(), &UIToolPane::sigCurrentSnapshotItemChange,
            this, &UIVirtualBoxWidget::sigCurrentSnapshotItemChange);
    connect(machineToolPane(), &UIToolPane::sigDetachToolPane,
            this, &UIVirtualBoxWidget::sigDetachToolPane);

    /* Chooser-pane connections: */
    connect(chooser(), &UIChooser::sigGroupSavingStateChanged,
            this, &UIVirtualBoxWidget::sigGroupSavingStateChanged);
    connect(chooser(), &UIChooser::sigCloudUpdateStateChanged,
            this, &UIVirtualBoxWidget::sigCloudUpdateStateChanged);
    connect(chooser(), &UIChooser::sigStartOrShowRequest,
            this, &UIVirtualBoxWidget::sigStartOrShowRequest);
    connect(chooser(), &UIChooser::sigMachineSearchWidgetVisibilityChanged,
            this, &UIVirtualBoxWidget::sigMachineSearchWidgetVisibilityChanged);
}

void UIVirtualBoxWidget::loadSettings()
{
    /* Make sure stuff exists: */
    AssertPtrReturnVoid(m_pToolBar);
    m_pToolBar->setUseTextLabels(gEDataManager->selectorWindowToolBarTextVisible());
}

void UIVirtualBoxWidget::updateToolbar()
{
    /* Make sure stuff exists: */
    AssertPtrReturnVoid(m_pToolBar);
    AssertPtrReturnVoid(globalToolsWidget());
    AssertPtrReturnVoid(machineToolsWidget());

    /* Clear toolbar initially: */
    m_pToolBar->clear();

    /* Determine actual tool-type: */
    const UIToolType enmGlobalType = globalToolsWidget()->toolType();
    UIToolType enmType = UIToolType_Invalid;
    switch (enmGlobalType)
    {
        case UIToolType_Invalid:
        case UIToolType_Toggle:
            break;
        case UIToolType_Machines:
            enmType = machineToolsWidget()->toolType();
            break;
        default:
            enmType = enmGlobalType;
            break;
    }
    AssertReturnVoid(enmType != UIToolType_Invalid);

    /* Populate toolbar depending on actual tool-type: */
    switch (enmType)
    {
        case UIToolType_Home:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Home_S_New));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Home_S_Add));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Application_S_Preferences));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ImportAppliance));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ExportAppliance));
            break;
        }
        case UIToolType_Details:
        {
            if (isSingleGroupSelected())
            {
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_New));
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Add));
                m_pToolBar->addSeparator();
                if (isSingleLocalGroupSelected())
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Discard));
                else if (   isSingleCloudProviderGroupSelected()
                         || isSingleCloudProfileGroupSelected())
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Stop_S_Terminate));
                if (   currentItem()
                    && currentItem()->isItemPoweredOff())
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_Start));
                else
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Show));
            }
            else
            {
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_New));
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Add));
                m_pToolBar->addSeparator();
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
                if (isLocalMachineItemSelected())
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
                else if (isCloudMachineItemSelected())
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop_S_Terminate));
                if (   currentItem()
                    && currentItem()->isItemPoweredOff())
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Start));
                else
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Show));
            }
            break;
        }
        case UIToolType_Snapshots:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Snapshot_S_Take));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Snapshot_S_Delete));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Snapshot_S_Restore));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Snapshot_T_Properties));
            if (gEDataManager->isSettingsInExpertMode())
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Snapshot_S_Clone));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
            if (   currentItem()
                && currentItem()->isItemPoweredOff())
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Start));
            else
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Show));
            break;
        }
        case UIToolType_Logs:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Log_S_Save));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Log_T_Find));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Log_T_Filter));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Log_T_Bookmark));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Log_T_Preferences));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Log_S_Refresh));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Log_S_Reload));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
            if (   currentItem()
                && currentItem()->isItemPoweredOff())
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Start));
            else
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Show));
            break;
        }
        case UIToolType_VMActivity:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Activity_S_Export));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Activity_S_ToVMActivityOverview));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Activity_T_Preferences));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
            if (   currentItem()
                && currentItem()->isItemPoweredOff())
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Start));
            else
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Show));
            break;
        }
        case UIToolType_FileManager:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_FileManager_T_Preferences));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_FileManager_T_Operations));
            m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_FileManager_T_Log));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
            if (   currentItem()
                && currentItem()->isItemPoweredOff())
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_Start));
            else
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Show));
            break;
        }
        case UIToolType_Error:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_New));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Add));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Refresh));
            break;
        }
        case UIToolType_Extensions:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Extension_S_Install));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Extension_S_Uninstall));
            break;
        }
        case UIToolType_Media:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Add));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Create));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Edit));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Copy));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Move));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Remove));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Release));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Clear));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_T_Search));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_T_Details));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Medium_S_Refresh));
            break;
        }
        case UIToolType_Network:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Network_S_Create));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Network_S_Remove));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Network_T_Details));
            //m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Network_S_Refresh));
            break;
        }
        case UIToolType_Cloud:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Cloud_S_Add));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Cloud_S_Import));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Cloud_S_Remove));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Cloud_T_Details));
            m_pToolBar->addSeparator();
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Cloud_S_TryPage));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Cloud_S_Help));
            break;
        }
        case UIToolType_Activities:
        {
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_VMActivityOverview_M_Columns));
            m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_VMActivityOverview_S_SwitchToMachineActivity));
            QToolButton *pButton =
                qobject_cast<QToolButton*>(m_pToolBar->widgetForAction(actionPool()->action(UIActionIndexMN_M_VMActivityOverview_M_Columns)));
            if (pButton)
            {
                pButton->setPopupMode(QToolButton::InstantPopup);
                pButton->setAutoRaise(true);
            }
            break;
        }
        default:
            break;
    }
}

void UIVirtualBoxWidget::cleanupConnections()
{
    /* Global COM event handlers: */
    disconnect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
               this, &UIVirtualBoxWidget::sltHandleMachineStateChange);
    disconnect(gEDataManager, &UIExtraDataManager::sigSettingsExpertModeChange,
               this, &UIVirtualBoxWidget::sltUpdateToolbar);

    /* Tool-bar connections: */
    disconnect(m_pToolBar, &QIToolBar::customContextMenuRequested,
               this, &UIVirtualBoxWidget::sltHandleToolBarContextMenuRequest);

    /* Global Tools Widget connections: */
    disconnect(globalToolsWidget(), &UIGlobalToolsWidget::sigToolTypeChange,
               this, &UIVirtualBoxWidget::sltUpdateToolbar);
    disconnect(globalToolsWidget(), &UIGlobalToolsWidget::sigToolTypeChange,
               this, &UIVirtualBoxWidget::sigToolTypeChangeGlobal);
    /* Global Tool Pane connections: */
    disconnect(globalToolPane(), &UIToolPane::sigHomeTask,
               this, &UIVirtualBoxWidget::sigHomeTask);
    disconnect(globalToolPane(), &UIToolPane::sigCreateMedium,
               this, &UIVirtualBoxWidget::sigCreateMedium);
    disconnect(globalToolPane(), &UIToolPane::sigCopyMedium,
               this, &UIVirtualBoxWidget::sigCopyMedium);

    /* Machine Tools Widget connections: */
    disconnect(machineToolsWidget(), &UIMachineToolsWidget::sigToolTypeChange,
               this, &UIVirtualBoxWidget::sltUpdateToolbar);
    disconnect(machineToolsWidget(), &UIMachineToolsWidget::sigToolTypeChange,
               this, &UIVirtualBoxWidget::sigToolTypeChangeMachine);
    disconnect(machineToolsWidget(), &UIMachineToolsWidget::sigChooserPaneSelectionChange,
               this, &UIVirtualBoxWidget::sigChooserPaneSelectionChange);
    disconnect(machineToolsWidget(), &UIMachineToolsWidget::sigChooserPaneSelectionClassChange,
               this, &UIVirtualBoxWidget::sltUpdateToolbar);
    disconnect(machineToolsWidget(), &UIMachineToolsWidget::sigCloudMachineStateChange,
               this, &UIVirtualBoxWidget::sigCloudMachineStateChange);
    /* Machine Tool Pane connections: */
    disconnect(machineToolPane(), &UIToolPane::sigLinkClicked,
               this, &UIVirtualBoxWidget::sigMachineSettingsLinkClicked);
    disconnect(machineToolPane(), &UIToolPane::sigCurrentSnapshotItemChange,
               this, &UIVirtualBoxWidget::sigCurrentSnapshotItemChange);
    disconnect(machineToolPane(), &UIToolPane::sigDetachToolPane,
               this, &UIVirtualBoxWidget::sigDetachToolPane);

    /* Chooser-pane connections: */
    disconnect(chooser(), &UIChooser::sigGroupSavingStateChanged,
               this, &UIVirtualBoxWidget::sigGroupSavingStateChanged);
    disconnect(chooser(), &UIChooser::sigCloudUpdateStateChanged,
               this, &UIVirtualBoxWidget::sigCloudUpdateStateChanged);
    disconnect(chooser(), &UIChooser::sigStartOrShowRequest,
               this, &UIVirtualBoxWidget::sigStartOrShowRequest);
    disconnect(chooser(), &UIChooser::sigMachineSearchWidgetVisibilityChanged,
               this, &UIVirtualBoxWidget::sigMachineSearchWidgetVisibilityChanged);
}

void UIVirtualBoxWidget::cleanup()
{
    /* Destroy notification-center: */
    UINotificationCenter::destroy();
}

UIGlobalToolsWidget *UIVirtualBoxWidget::globalToolsWidget() const
{
    return m_pGlobalToolsWidget;
}

UIToolPane *UIVirtualBoxWidget::globalToolPane() const
{
    return globalToolsWidget()->toolPane();
}

UIMachineToolsWidget *UIVirtualBoxWidget::machineToolsWidget() const
{
    return globalToolsWidget()->machineToolsWidget();
}

UIToolPane *UIVirtualBoxWidget::machineToolPane() const
{
    return machineToolsWidget()->toolPane();
}

UIChooser *UIVirtualBoxWidget::chooser() const
{
    return machineToolsWidget()->chooser();
}
