/* $Id: UIVirtualBoxManagerWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxManagerWidget class implementation.
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
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QISplitter.h"
#include "UIActionPoolManager.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIChooser.h"
#include "UICommon.h"
#include "UIGlobalSession.h"
#include "UILoggingDefs.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIVirtualBoxManager.h"
#include "UIVirtualBoxManagerWidget.h"
#include "UITabBar.h"
#include "QIToolBar.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"
#include "UITools.h"
#if defined(VBOX_WS_MAC) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32))
# include "UIIconPool.h"
# include "UIVersion.h"
#endif /* VBOX_WS_MAC && (RT_ARCH_ARM64 || RT_ARCH_ARM32) */
#ifndef VBOX_WS_MAC
# include "UIMenuBar.h"
#endif

/* COM includes: */
#if defined(VBOX_WS_MAC) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32))
# include "CSystemProperties.h"
#endif /* VBOX_WS_MAC && (RT_ARCH_ARM64 || RT_ARCH_ARM32) */


UIVirtualBoxManagerWidget::UIVirtualBoxManagerWidget(UIVirtualBoxManager *pParent)
    : m_pActionPool(pParent->actionPool())
    , m_pSplitter(0)
    , m_pToolBar(0)
    , m_pPaneChooser(0)
    , m_pStackedWidget(0)
    , m_pPaneToolsGlobal(0)
    , m_pPaneToolsMachine(0)
    , m_pSlidingAnimation(0)
    , m_pMenuToolsGlobal(0)
    , m_pMenuToolsMachine(0)
    , m_enmSelectionType(SelectionType_Invalid)
    , m_fSelectedMachineItemAccessible(false)
    , m_pSplitterSettingsSaveTimer(0)
{
    prepare();
}

UIVirtualBoxManagerWidget::~UIVirtualBoxManagerWidget()
{
    cleanup();
}

UIVirtualMachineItem *UIVirtualBoxManagerWidget::currentItem() const
{
    return m_pPaneChooser->currentItem();
}

QList<UIVirtualMachineItem*> UIVirtualBoxManagerWidget::currentItems() const
{
    return m_pPaneChooser->currentItems();
}

bool UIVirtualBoxManagerWidget::isGroupItemSelected() const
{
    return m_pPaneChooser->isGroupItemSelected();
}

bool UIVirtualBoxManagerWidget::isGlobalItemSelected() const
{
    return m_pPaneChooser->isGlobalItemSelected();
}

bool UIVirtualBoxManagerWidget::isMachineItemSelected() const
{
    return m_pPaneChooser->isMachineItemSelected();
}

bool UIVirtualBoxManagerWidget::isLocalMachineItemSelected() const
{
    return m_pPaneChooser->isLocalMachineItemSelected();
}

bool UIVirtualBoxManagerWidget::isCloudMachineItemSelected() const
{
    return m_pPaneChooser->isCloudMachineItemSelected();
}

bool UIVirtualBoxManagerWidget::isSingleGroupSelected() const
{
    return m_pPaneChooser->isSingleGroupSelected();
}

bool UIVirtualBoxManagerWidget::isSingleLocalGroupSelected() const
{
    return m_pPaneChooser->isSingleLocalGroupSelected();
}

bool UIVirtualBoxManagerWidget::isSingleCloudProviderGroupSelected() const
{
    return m_pPaneChooser->isSingleCloudProviderGroupSelected();
}

bool UIVirtualBoxManagerWidget::isSingleCloudProfileGroupSelected() const
{
    return m_pPaneChooser->isSingleCloudProfileGroupSelected();
}

bool UIVirtualBoxManagerWidget::isAllItemsOfOneGroupSelected() const
{
    return m_pPaneChooser->isAllItemsOfOneGroupSelected();
}

QString UIVirtualBoxManagerWidget::fullGroupName() const
{
    return m_pPaneChooser->fullGroupName();
}

bool UIVirtualBoxManagerWidget::isGroupSavingInProgress() const
{
    return m_pPaneChooser->isGroupSavingInProgress();
}

bool UIVirtualBoxManagerWidget::isCloudProfileUpdateInProgress() const
{
    return m_pPaneChooser->isCloudProfileUpdateInProgress();
}

void UIVirtualBoxManagerWidget::switchToGlobalItem()
{
    AssertPtrReturnVoid(m_pPaneChooser);
    m_pPaneChooser->setCurrentGlobal();
}

void UIVirtualBoxManagerWidget::openGroupNameEditor()
{
    m_pPaneChooser->openGroupNameEditor();
}

void UIVirtualBoxManagerWidget::disbandGroup()
{
    m_pPaneChooser->disbandGroup();
}

void UIVirtualBoxManagerWidget::removeMachine()
{
    m_pPaneChooser->removeMachine();
}

void UIVirtualBoxManagerWidget::moveMachineToGroup(const QString &strName /* = QString() */)
{
    m_pPaneChooser->moveMachineToGroup(strName);
}

QStringList UIVirtualBoxManagerWidget::possibleGroupsForMachineToMove(const QUuid &uId)
{
    return m_pPaneChooser->possibleGroupsForMachineToMove(uId);
}

QStringList UIVirtualBoxManagerWidget::possibleGroupsForGroupToMove(const QString &strFullName)
{
    return m_pPaneChooser->possibleGroupsForGroupToMove(strFullName);
}

void UIVirtualBoxManagerWidget::refreshMachine()
{
    m_pPaneChooser->refreshMachine();
}

void UIVirtualBoxManagerWidget::sortGroup()
{
    m_pPaneChooser->sortGroup();
}

void UIVirtualBoxManagerWidget::setMachineSearchWidgetVisibility(bool fVisible)
{
    m_pPaneChooser->setMachineSearchWidgetVisibility(fVisible);
}

void UIVirtualBoxManagerWidget::setToolsTypeGlobal(UIToolType enmType)
{
    m_pMenuToolsGlobal->setToolsType(enmType);
}

UIToolType UIVirtualBoxManagerWidget::toolsTypeGlobal() const
{
    return m_pMenuToolsGlobal ? m_pMenuToolsGlobal->toolsType() : UIToolType_Invalid;
}

void UIVirtualBoxManagerWidget::setToolsTypeMachine(UIToolType enmType)
{
    m_pMenuToolsMachine->setToolsType(enmType);
}

UIToolType UIVirtualBoxManagerWidget::toolsTypeMachine() const
{
    return m_pMenuToolsMachine ? m_pMenuToolsMachine->toolsType() : UIToolType_Invalid;
}

UIToolType UIVirtualBoxManagerWidget::currentGlobalTool() const
{
    return m_pPaneToolsGlobal ? m_pPaneToolsGlobal->currentTool() : UIToolType_Invalid;
}

UIToolType UIVirtualBoxManagerWidget::currentMachineTool() const
{
    return m_pPaneToolsMachine ? m_pPaneToolsMachine->currentTool() : UIToolType_Invalid;
}

bool UIVirtualBoxManagerWidget::isGlobalToolOpened(UIToolType enmType) const
{
    return m_pPaneToolsGlobal ? m_pPaneToolsGlobal->isToolOpened(enmType) : false;
}

bool UIVirtualBoxManagerWidget::isMachineToolOpened(UIToolType enmType) const
{
    return m_pPaneToolsMachine ? m_pPaneToolsMachine->isToolOpened(enmType) : false;
}

void UIVirtualBoxManagerWidget::switchGlobalToolTo(UIToolType enmType)
{
    /* Open corresponding tool: */
    m_pPaneToolsGlobal->openTool(enmType);

    /* Let the parent know: */
    emit sigToolTypeChangeGlobal();

    /* Update toolbar: */
    updateToolbar();

    /* Handle current tool type change: */
    handleCurrentToolTypeChange(enmType);
}

void UIVirtualBoxManagerWidget::switchMachineToolTo(UIToolType enmType)
{
    /* Open corresponding tool: */
    m_pPaneToolsMachine->openTool(enmType);

    /* Let the parent know: */
    emit sigToolTypeChangeMachine();

    /* Update toolbar: */
    updateToolbar();

    /* Handle current tool type change: */
    handleCurrentToolTypeChange(enmType);
}

void UIVirtualBoxManagerWidget::closeGlobalTool(UIToolType enmType)
{
    m_pPaneToolsGlobal->closeTool(enmType);
}

void UIVirtualBoxManagerWidget::closeMachineTool(UIToolType enmType)
{
    m_pPaneToolsMachine->closeTool(enmType);
}

bool UIVirtualBoxManagerWidget::isCurrentStateItemSelected() const
{
    return m_pPaneToolsMachine->isCurrentStateItemSelected();
}

QUuid UIVirtualBoxManagerWidget::currentSnapshotId()
{
    return m_pPaneToolsMachine->currentSnapshotId();
}

void UIVirtualBoxManagerWidget::updateToolBarMenuButtons(bool fSeparateMenuSection)
{
    QToolButton *pButton = qobject_cast<QToolButton*>(m_pToolBar->widgetForAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow)));
    if (pButton)
        pButton->setPopupMode(fSeparateMenuSection ? QToolButton::MenuButtonPopup : QToolButton::DelayedPopup);
}

QString UIVirtualBoxManagerWidget::currentHelpKeyword() const
{
    QString strHelpKeyword;
    if (isGlobalItemSelected())
        strHelpKeyword = m_pPaneToolsGlobal->currentHelpKeyword();
    else if (isMachineItemSelected())
        strHelpKeyword = m_pPaneToolsMachine->currentHelpKeyword();
    return strHelpKeyword;
}

void UIVirtualBoxManagerWidget::sltHandleToolBarContextMenuRequest(const QPoint &position)
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

void UIVirtualBoxManagerWidget::sltRetranslateUI()
{
    /* Make sure chosen item fetched: */
    sltHandleChooserPaneIndexChange();
}

void UIVirtualBoxManagerWidget::sltHandleCommitData()
{
    // WORKAROUND:
    // This will be fixed proper way during session management cleanup for Qt6.
    // But for now we will just cleanup connections which is Ok anyway.
    cleanupConnections();
}

void UIVirtualBoxManagerWidget::sltHandleStateChange(const QUuid &uId)
{
    // WORKAROUND:
    // In certain intermediate states VM info can be NULL which
    // causing annoying assertions, such updates can be ignored?
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    CMachine comMachine = comVBox.FindMachine(uId.toString());
    if (comVBox.isOk() && comMachine.isNotNull())
    {
        switch (comMachine.GetState())
        {
            case KMachineState_DeletingSnapshot:
                return;
            default:
                break;
        }
    }

    /* Recache current machine item information: */
    recacheCurrentMachineItemInformation();
}

void UIVirtualBoxManagerWidget::sltHandleSettingsExpertModeChange()
{
    /* Update toolbar to show/hide corresponding actions: */
    updateToolbar();

    /* Update tools restrictions for currently selected item: */
    if (currentItem())
        updateToolsMenuMachine(currentItem());
    else
        updateToolsMenuGlobal();
}

void UIVirtualBoxManagerWidget::sltHandleSplitterMove()
{
    /* Create timer if isn't exist already: */
    if (!m_pSplitterSettingsSaveTimer)
    {
        m_pSplitterSettingsSaveTimer = new QTimer(this);
        if (m_pSplitterSettingsSaveTimer)
        {
            m_pSplitterSettingsSaveTimer->setInterval(300);
            m_pSplitterSettingsSaveTimer->setSingleShot(true);
            connect(m_pSplitterSettingsSaveTimer, &QTimer::timeout,
                    this, &UIVirtualBoxManagerWidget::sltSaveSplitterSettings);
        }
    }
    /* [Re]start timer finally: */
    m_pSplitterSettingsSaveTimer->start();
}

void UIVirtualBoxManagerWidget::sltSaveSplitterSettings()
{
    const QList<int> splitterSizes = m_pSplitter->sizes();
    LogRel2(("GUI: UIVirtualBoxManagerWidget: Saving splitter as: Size=%d,%d\n",
             splitterSizes.at(0), splitterSizes.at(1)));
    gEDataManager->setSelectorWindowSplitterHints(splitterSizes);
}

void UIVirtualBoxManagerWidget::sltHandleToolBarResize(const QSize &newSize)
{
    emit sigToolBarHeightChange(newSize.height());
}

void UIVirtualBoxManagerWidget::sltHandleChooserPaneIndexChange()
{
    /* Let the parent know: */
    emit sigChooserPaneIndexChange();

    /* If global item is selected and we are on machine tools pane => switch to global tools pane: */
    if (   isGlobalItemSelected()
        && m_pStackedWidget->currentWidget() != m_pPaneToolsGlobal)
    {
        /* Just start animation and return, do nothing else.. */
        m_pStackedWidget->setCurrentWidget(m_pPaneToolsGlobal); // rendering w/a
        m_pStackedWidget->setCurrentWidget(m_pSlidingAnimation);
        m_pSlidingAnimation->animate(SlidingDirection_Reverse);
        return;
    }

    else

    /* If machine or group item is selected and we are on global tools pane => switch to machine tools pane: */
    if (   (isMachineItemSelected() || isGroupItemSelected())
        && m_pStackedWidget->currentWidget() != m_pPaneToolsMachine)
    {
        /* Just start animation and return, do nothing else.. */
        m_pStackedWidget->setCurrentWidget(m_pPaneToolsMachine); // rendering w/a
        m_pStackedWidget->setCurrentWidget(m_pSlidingAnimation);
        m_pSlidingAnimation->animate(SlidingDirection_Forward);
        return;
    }

    /* Update tools restrictions for currently selected item: */
    UIVirtualMachineItem *pItem = currentItem();
    if (pItem)
        updateToolsMenuMachine(pItem);
    else
        updateToolsMenuGlobal();

    /* Recache current machine item information: */
    recacheCurrentMachineItemInformation();

    /* Calculate selection type: */
    const SelectionType enmSelectedItemType = isSingleLocalGroupSelected()
                                            ? SelectionType_SingleLocalGroupItem
                                            : isSingleCloudProviderGroupSelected() || isSingleCloudProfileGroupSelected()
                                            ? SelectionType_SingleCloudGroupItem
                                            : isGlobalItemSelected()
                                            ? SelectionType_FirstIsGlobalItem
                                            : isLocalMachineItemSelected()
                                            ? SelectionType_FirstIsLocalMachineItem
                                            : isCloudMachineItemSelected()
                                            ? SelectionType_FirstIsCloudMachineItem
                                            : SelectionType_Invalid;

    /* Update toolbar if selection type or item accessibility got changed: */
    const bool fCurrentItemIsOk = pItem && pItem->accessible();
    if (   m_enmSelectionType != enmSelectedItemType
        || m_fSelectedMachineItemAccessible != fCurrentItemIsOk)
        updateToolbar();

    /* Remember the last selection type: */
    m_enmSelectionType = enmSelectedItemType;
    /* Remember whether the last selected item was accessible: */
    m_fSelectedMachineItemAccessible = fCurrentItemIsOk;
}

void UIVirtualBoxManagerWidget::sltHandleSlidingAnimationComplete(SlidingDirection enmDirection)
{
    /* First switch the panes: */
    switch (enmDirection)
    {
        case SlidingDirection_Forward:
        {
            /* Switch stacked widget to machine tool pane: */
            m_pStackedWidget->setCurrentWidget(m_pPaneToolsMachine);
            m_pPaneToolsGlobal->setActive(false);
            m_pPaneToolsMachine->setActive(true);
            /* Handle current tool type change: */
            handleCurrentToolTypeChange(m_pMenuToolsMachine->toolsType());
            break;
        }
        case SlidingDirection_Reverse:
        {
            /* Switch stacked widget to global tool pane: */
            m_pStackedWidget->setCurrentWidget(m_pPaneToolsGlobal);
            m_pPaneToolsMachine->setActive(false);
            m_pPaneToolsGlobal->setActive(true);
            /* Handle current tool type change: */
            handleCurrentToolTypeChange(m_pMenuToolsGlobal->toolsType());
            break;
        }
    }
    /* Then handle current item change (again!): */
    sltHandleChooserPaneIndexChange();
}

void UIVirtualBoxManagerWidget::sltHandleCloudProfileStateChange(const QString &strProviderShortName,
                                                                 const QString &strProfileName)
{
    RT_NOREF(strProviderShortName, strProfileName);

    /* If Global Activity Overview tool is currently chosen: */
    if (   m_pStackedWidget->currentWidget() == m_pPaneToolsGlobal
        && m_pPaneToolsGlobal->currentTool() == UIToolType_VMActivityOverview)
        m_pPaneToolsGlobal->setCloudMachineItems(m_pPaneChooser->cloudMachineItems());
}

void UIVirtualBoxManagerWidget::sltHandleCloudMachineStateChange(const QUuid &uId)
{
    /* Not for global items: */
    if (!isGlobalItemSelected())
    {
        /* Acquire current item: */
        UIVirtualMachineItem *pItem = currentItem();
        const bool fCurrentItemIsOk = pItem && pItem->accessible();

        /* If current item is Ok: */
        if (fCurrentItemIsOk)
        {
            /* If Error-pane is chosen currently => switch to tool currently chosen in Tools-menu: */
            if (m_pPaneToolsMachine->currentTool() == UIToolType_Error)
                switchMachineToolTo(m_pMenuToolsMachine->toolsType());

            /* If we still have same item selected: */
            if (pItem && pItem->id() == uId)
            {
                /* Propagate current items to update the Details-pane: */
                m_pPaneToolsMachine->setItems(currentItems());
            }
        }
        else
        {
            /* Make sure Error pane raised: */
            if (m_pPaneToolsMachine->currentTool() != UIToolType_Error)
                m_pPaneToolsMachine->openTool(UIToolType_Error);

            /* If we still have same item selected: */
            if (pItem && pItem->id() == uId)
            {
                /* Propagate current items to update the Details-pane (in any case): */
                m_pPaneToolsMachine->setItems(currentItems());
                /* Propagate last access error to update the Error-pane (if machine selected but inaccessible): */
                m_pPaneToolsMachine->setErrorDetails(pItem->accessError());
            }
        }

        /* Pass the signal further: */
        emit sigCloudMachineStateChange(uId);
    }
}

void UIVirtualBoxManagerWidget::sltHandleToolMenuRequested(const QPoint &position, UIVirtualMachineItem *pItem)
{
    /* Update tools menu beforehand: */
    UITools *pMenu = pItem ? m_pMenuToolsMachine : m_pMenuToolsGlobal;
    AssertPtrReturnVoid(pMenu);
    if (pItem)
        updateToolsMenuMachine(pItem);
    else
        updateToolsMenuGlobal();

    /* Compose popup-menu geometry first of all: */
    QRect ourGeo = QRect(position, pMenu->minimumSizeHint());
    /* Adjust location only to properly fit into available geometry space: */
    const QRect availableGeo = gpDesktop->availableGeometry(position);
    ourGeo = gpDesktop->normalizeGeometry(ourGeo, availableGeo, false /* resize? */);

    /* Move, resize and show: */
    pMenu->move(ourGeo.topLeft());
    pMenu->show();
    // WORKAROUND:
    // Don't want even to think why, but for Qt::Popup resize to
    // smaller size often being ignored until it is actually shown.
    pMenu->resize(ourGeo.size());
}

void UIVirtualBoxManagerWidget::sltSwitchToMachineActivityPane(const QUuid &uMachineId)
{
    AssertPtrReturnVoid(m_pPaneChooser);
    AssertPtrReturnVoid(m_pMenuToolsMachine);
    m_pPaneChooser->setCurrentMachine(uMachineId);
    m_pMenuToolsMachine->setToolsType(UIToolType_VMActivity);
}

void UIVirtualBoxManagerWidget::sltSwitchToActivityOverviewPane()
{
    AssertPtrReturnVoid(m_pPaneChooser);
    AssertPtrReturnVoid(m_pMenuToolsGlobal);
    m_pMenuToolsGlobal->setToolsType(UIToolType_VMActivityOverview);
    m_pPaneChooser->setCurrentGlobal();
}

void UIVirtualBoxManagerWidget::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Load settings: */
    loadSettings();

    /* Translate UI: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVirtualBoxManagerWidget::sltRetranslateUI);

    /* Make sure current Chooser-pane index fetched: */
    sltHandleChooserPaneIndexChange();
}

void UIVirtualBoxManagerWidget::prepareWidgets()
{
    /* Create main-layout: */
    QHBoxLayout *pLayoutMain = new QHBoxLayout(this);
    if (pLayoutMain)
    {
        /* Configure layout: */
        pLayoutMain->setSpacing(0);
        pLayoutMain->setContentsMargins(0, 0, 0, 0);

        /* Create splitter: */
        m_pSplitter = new QISplitter;
        if (m_pSplitter)
        {
            /* Create Chooser-pane: */
            m_pPaneChooser = new UIChooser(this, actionPool());
            if (m_pPaneChooser)
            {
                /* Add into splitter: */
                m_pSplitter->addWidget(m_pPaneChooser);
            }

            /* Create right widget: */
            QWidget *pWidgetRight = new QWidget;
            if (pWidgetRight)
            {
                /* Create right-layout: */
                QVBoxLayout *pLayoutRight = new QVBoxLayout(pWidgetRight);
                if(pLayoutRight)
                {
                    /* Configure layout: */
                    pLayoutRight->setSpacing(0);
                    pLayoutRight->setContentsMargins(0, 0, 0, 0);

                    /* Create Main toolbar: */
                    m_pToolBar = new QIToolBar;
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
                        pLayoutRight->addWidget(m_pToolBar);
                    }

                    /* Create stacked-widget: */
                    m_pStackedWidget = new QStackedWidget;
                    if (m_pStackedWidget)
                    {
                        /* Create Global Tools-pane: */
                        m_pPaneToolsGlobal = new UIToolPaneGlobal(actionPool());
                        if (m_pPaneToolsGlobal)
                        {
                            if (m_pPaneChooser->isGlobalItemSelected())
                                m_pPaneToolsGlobal->setActive(true);
                            connect(m_pPaneToolsGlobal, &UIToolPaneGlobal::sigCreateMedium,
                                    this, &UIVirtualBoxManagerWidget::sigCreateMedium);
                            connect(m_pPaneToolsGlobal, &UIToolPaneGlobal::sigCopyMedium,
                                    this, &UIVirtualBoxManagerWidget::sigCopyMedium);
                            connect(m_pPaneToolsGlobal, &UIToolPaneGlobal::sigSwitchToMachineActivityPane,
                                    this, &UIVirtualBoxManagerWidget::sltSwitchToMachineActivityPane);

                            /* Add into stack: */
                            m_pStackedWidget->addWidget(m_pPaneToolsGlobal);
                        }

                        /* Create Machine Tools-pane: */
                        m_pPaneToolsMachine = new UIToolPaneMachine(actionPool());
                        if (m_pPaneToolsMachine)
                        {
                            if (!m_pPaneChooser->isGlobalItemSelected())
                                m_pPaneToolsMachine->setActive(true);
                            connect(m_pPaneToolsMachine, &UIToolPaneMachine::sigCurrentSnapshotItemChange,
                                    this, &UIVirtualBoxManagerWidget::sigCurrentSnapshotItemChange);
                            connect(m_pPaneToolsMachine, &UIToolPaneMachine::sigSwitchToActivityOverviewPane,
                                    this, &UIVirtualBoxManagerWidget::sltSwitchToActivityOverviewPane);
                            connect(m_pPaneToolsMachine, &UIToolPaneMachine::sigDetachToolPane,
                                    this, &UIVirtualBoxManagerWidget::sigDetachToolPane);

                            /* Add into stack: */
                            m_pStackedWidget->addWidget(m_pPaneToolsMachine);
                        }

                        /* Create sliding-widget: */
                        // Reverse initial animation direction if group or machine selected!
                        const bool fReverse = !m_pPaneChooser->isGlobalItemSelected();
                        m_pSlidingAnimation = new UISlidingAnimation(Qt::Vertical, fReverse);
                        if (m_pSlidingAnimation)
                        {
                            /* Add first/second widgets into sliding animation: */
                            m_pSlidingAnimation->setWidgets(m_pPaneToolsGlobal, m_pPaneToolsMachine);
                            connect(m_pSlidingAnimation, &UISlidingAnimation::sigAnimationComplete,
                                    this, &UIVirtualBoxManagerWidget::sltHandleSlidingAnimationComplete);

                            /* Add into stack: */
                            m_pStackedWidget->addWidget(m_pSlidingAnimation);
                        }

                        /* Choose which pane should be active initially: */
                        if (m_pPaneChooser->isGlobalItemSelected())
                            m_pStackedWidget->setCurrentWidget(m_pPaneToolsGlobal);
                        else
                            m_pStackedWidget->setCurrentWidget(m_pPaneToolsMachine);

                        /* Add into layout: */
                        pLayoutRight->addWidget(m_pStackedWidget, 1);
                    }
                }

                /* Add into splitter: */
                m_pSplitter->addWidget(pWidgetRight);
            }

            /* Set the initial distribution. The right site is bigger. */
            m_pSplitter->setStretchFactor(0, 2);
            m_pSplitter->setStretchFactor(1, 3);

            /* Add into layout: */
            pLayoutMain->addWidget(m_pSplitter);
        }

        /* Create Global Tools-menu: */
        m_pMenuToolsGlobal = new UITools(UIToolClass_Global, this);
        /* Create Machine Tools-menu: */
        m_pMenuToolsMachine = new UITools(UIToolClass_Machine, this);
    }

    /* Create notification-center: */
    UINotificationCenter::create(this);

    /* Update toolbar finally: */
    updateToolbar();

    /* Bring the VM list to the focus: */
    m_pPaneChooser->setFocus();
}

void UIVirtualBoxManagerWidget::prepareConnections()
{
    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIVirtualBoxManagerWidget::sltHandleCommitData);

    /* Global VBox event handlers: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIVirtualBoxManagerWidget::sltHandleStateChange);

    /* Global VBox extra-data event handlers: */
    connect(gEDataManager, &UIExtraDataManager::sigSettingsExpertModeChange,
            this, &UIVirtualBoxManagerWidget::sltHandleSettingsExpertModeChange);

    /* Splitter connections: */
    connect(m_pSplitter, &QISplitter::splitterMoved,
            this, &UIVirtualBoxManagerWidget::sltHandleSplitterMove);

    /* Tool-bar connections: */
    connect(m_pToolBar, &QIToolBar::customContextMenuRequested,
            this, &UIVirtualBoxManagerWidget::sltHandleToolBarContextMenuRequest);
    connect(m_pToolBar, &QIToolBar::sigResized,
            this, &UIVirtualBoxManagerWidget::sltHandleToolBarResize);

    /* Chooser-pane connections: */
    connect(this, &UIVirtualBoxManagerWidget::sigToolBarHeightChange,
            m_pPaneChooser, &UIChooser::setGlobalItemHeightHint);
    connect(m_pPaneChooser, &UIChooser::sigSelectionChanged,
            this, &UIVirtualBoxManagerWidget::sltHandleChooserPaneIndexChange);
    connect(m_pPaneChooser, &UIChooser::sigSelectionInvalidated,
            this, &UIVirtualBoxManagerWidget::sltHandleChooserPaneSelectionInvalidated);
    connect(m_pPaneChooser, &UIChooser::sigToggleStarted,
            m_pPaneToolsMachine, &UIToolPaneMachine::sigToggleStarted);
    connect(m_pPaneChooser, &UIChooser::sigToggleFinished,
            m_pPaneToolsMachine, &UIToolPaneMachine::sigToggleFinished);
    connect(m_pPaneChooser, &UIChooser::sigGroupSavingStateChanged,
            this, &UIVirtualBoxManagerWidget::sigGroupSavingStateChanged);
    connect(m_pPaneChooser, &UIChooser::sigCloudUpdateStateChanged,
            this, &UIVirtualBoxManagerWidget::sigCloudUpdateStateChanged);
    connect(m_pPaneChooser, &UIChooser::sigToolMenuRequested,
            this, &UIVirtualBoxManagerWidget::sltHandleToolMenuRequested);
    connect(m_pPaneChooser, &UIChooser::sigCloudProfileStateChange,
            this, &UIVirtualBoxManagerWidget::sltHandleCloudProfileStateChange);
    connect(m_pPaneChooser, &UIChooser::sigCloudMachineStateChange,
            this, &UIVirtualBoxManagerWidget::sltHandleCloudMachineStateChange);
    connect(m_pPaneChooser, &UIChooser::sigStartOrShowRequest,
            this, &UIVirtualBoxManagerWidget::sigStartOrShowRequest);
    connect(m_pPaneChooser, &UIChooser::sigMachineSearchWidgetVisibilityChanged,
            this, &UIVirtualBoxManagerWidget::sigMachineSearchWidgetVisibilityChanged);

    /* Details-pane connections: */
    connect(m_pPaneToolsMachine, &UIToolPaneMachine::sigLinkClicked,
            this, &UIVirtualBoxManagerWidget::sigMachineSettingsLinkClicked);

    /* Tools-pane connections: */
    connect(m_pMenuToolsGlobal, &UITools::sigSelectionChanged,
            this, &UIVirtualBoxManagerWidget::sltHandleGlobalToolsMenuIndexChange);
    connect(m_pMenuToolsMachine, &UITools::sigSelectionChanged,
            this, &UIVirtualBoxManagerWidget::sltHandleMachineToolsMenuIndexChange);
}

void UIVirtualBoxManagerWidget::loadSettings()
{
    /* Restore splitter handle position: */
    {
        QList<int> sizes = gEDataManager->selectorWindowSplitterHints();
        /* If both hints are zero, we have the 'default' case: */
        if (sizes.at(0) == 0 && sizes.at(1) == 0)
        {
            sizes[0] = (int)(width() * .9 * (1.0 / 3));
            sizes[1] = (int)(width() * .9 * (2.0 / 3));
        }
        LogRel2(("GUI: UIVirtualBoxManagerWidget: Restoring splitter to: Size=%d,%d\n",
                 sizes.at(0), sizes.at(1)));
        m_pSplitter->setSizes(sizes);
    }

    /* Restore toolbar settings: */
    {
        m_pToolBar->setUseTextLabels(gEDataManager->selectorWindowToolBarTextVisible());
    }

    /* Open tools last chosen in Tools-menu: */
    switchGlobalToolTo(m_pMenuToolsGlobal->toolsType());
    switchMachineToolTo(m_pMenuToolsMachine->toolsType());
}

void UIVirtualBoxManagerWidget::updateToolbar()
{
    /* Make sure toolbar exists: */
    AssertPtrReturnVoid(m_pToolBar);

    /* Clear initially: */
    m_pToolBar->clear();

    /* If global item selected: */
    if (isGlobalItemSelected())
    {
        switch (currentGlobalTool())
        {
            case UIToolType_Welcome:
            {
                m_pToolBar->addAction(actionPool()->action(UIActionIndex_M_Application_S_Preferences));
                m_pToolBar->addSeparator();
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ImportAppliance));
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ExportAppliance));
                m_pToolBar->addSeparator();
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Welcome_S_New));
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Welcome_S_Add));
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
            case UIToolType_VMActivityOverview:
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

    else

    /* If machine or group item selected: */
    if (isMachineItemSelected() || isGroupItemSelected())
    {
        switch (currentMachineTool())
        {
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
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_StartOrShow));
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
                    m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow));
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
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow));
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
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow));
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
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow));
                break;
            }
            case UIToolType_FileManager:
            {
                m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_T_Preferences));
                m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_T_Operations));
                m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_T_Log));
                m_pToolBar->addSeparator();
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
                m_pToolBar->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow));
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
            default:
                break;
        }
    }
}

void UIVirtualBoxManagerWidget::cleanupConnections()
{
    /* Tool-bar connections: */
    disconnect(m_pToolBar, &QIToolBar::customContextMenuRequested,
               this, &UIVirtualBoxManagerWidget::sltHandleToolBarContextMenuRequest);
    disconnect(m_pToolBar, &QIToolBar::sigResized,
               this, &UIVirtualBoxManagerWidget::sltHandleToolBarResize);

    /* Chooser-pane connections: */
    disconnect(this, &UIVirtualBoxManagerWidget::sigToolBarHeightChange,
               m_pPaneChooser, &UIChooser::setGlobalItemHeightHint);
    disconnect(m_pPaneChooser, &UIChooser::sigSelectionChanged,
               this, &UIVirtualBoxManagerWidget::sltHandleChooserPaneIndexChange);
    disconnect(m_pPaneChooser, &UIChooser::sigSelectionInvalidated,
               this, &UIVirtualBoxManagerWidget::sltHandleChooserPaneSelectionInvalidated);
    disconnect(m_pPaneChooser, &UIChooser::sigToggleStarted,
               m_pPaneToolsMachine, &UIToolPaneMachine::sigToggleStarted);
    disconnect(m_pPaneChooser, &UIChooser::sigToggleFinished,
               m_pPaneToolsMachine, &UIToolPaneMachine::sigToggleFinished);
    disconnect(m_pPaneChooser, &UIChooser::sigGroupSavingStateChanged,
               this, &UIVirtualBoxManagerWidget::sigGroupSavingStateChanged);
    disconnect(m_pPaneChooser, &UIChooser::sigCloudUpdateStateChanged,
               this, &UIVirtualBoxManagerWidget::sigCloudUpdateStateChanged);
    disconnect(m_pPaneChooser, &UIChooser::sigToolMenuRequested,
               this, &UIVirtualBoxManagerWidget::sltHandleToolMenuRequested);
    disconnect(m_pPaneChooser, &UIChooser::sigCloudProfileStateChange,
               this, &UIVirtualBoxManagerWidget::sltHandleCloudProfileStateChange);
    disconnect(m_pPaneChooser, &UIChooser::sigCloudMachineStateChange,
               this, &UIVirtualBoxManagerWidget::sltHandleCloudMachineStateChange);
    disconnect(m_pPaneChooser, &UIChooser::sigStartOrShowRequest,
               this, &UIVirtualBoxManagerWidget::sigStartOrShowRequest);
    disconnect(m_pPaneChooser, &UIChooser::sigMachineSearchWidgetVisibilityChanged,
               this, &UIVirtualBoxManagerWidget::sigMachineSearchWidgetVisibilityChanged);

    /* Details-pane connections: */
    disconnect(m_pPaneToolsMachine, &UIToolPaneMachine::sigLinkClicked,
               this, &UIVirtualBoxManagerWidget::sigMachineSettingsLinkClicked);

    /* Tools-pane connections: */
    disconnect(m_pMenuToolsGlobal, &UITools::sigSelectionChanged,
               this, &UIVirtualBoxManagerWidget::sltHandleGlobalToolsMenuIndexChange);
    disconnect(m_pMenuToolsMachine, &UITools::sigSelectionChanged,
               this, &UIVirtualBoxManagerWidget::sltHandleMachineToolsMenuIndexChange);
}

void UIVirtualBoxManagerWidget::cleanupWidgets()
{
    UINotificationCenter::destroy();
}

void UIVirtualBoxManagerWidget::cleanup()
{
    /* Ask sub-dialogs to commit data: */
    sltHandleCommitData();

    /* Cleanup everything: */
    cleanupWidgets();
}

void UIVirtualBoxManagerWidget::recacheCurrentMachineItemInformation(bool fDontRaiseErrorPane /* = false */)
{
    /* Sanity check, this method is for machine or group of machine items: */
    if (!isMachineItemSelected() && !isGroupItemSelected())
        return;

    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    const bool fCurrentItemIsOk = pItem && pItem->accessible();

    /* If current item is Ok: */
    if (fCurrentItemIsOk)
    {
        /* If Error-pane is chosen currently => switch to tool currently chosen in Tools-menu: */
        if (m_pPaneToolsMachine->currentTool() == UIToolType_Error)
            switchMachineToolTo(m_pMenuToolsMachine->toolsType());

        /* Propagate current items to the Tools pane: */
        m_pPaneToolsMachine->setItems(currentItems());
    }
    /* Otherwise if we were not asked separately to calm down: */
    else if (!fDontRaiseErrorPane)
    {
        /* Make sure Error pane raised: */
        if (m_pPaneToolsMachine->currentTool() != UIToolType_Error)
            m_pPaneToolsMachine->openTool(UIToolType_Error);

        /* Propagate last access error to the Error-pane: */
        if (pItem)
            m_pPaneToolsMachine->setErrorDetails(pItem->accessError());
    }
}

void UIVirtualBoxManagerWidget::updateToolsMenuGlobal()
{
    /* Update global tools restrictions: */
    QSet<UIToolType> restrictedTypes;
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    if (!fExpertMode)
        restrictedTypes << UIToolType_Media
                        << UIToolType_Network;
    if (restrictedTypes.contains(m_pMenuToolsGlobal->toolsType()))
        m_pMenuToolsGlobal->setToolsType(UIToolType_Welcome);
    const QList restrictions(restrictedTypes.begin(), restrictedTypes.end());
    m_pMenuToolsGlobal->setRestrictedToolTypes(restrictions);

    /* Take restrictions into account, closing all restricted tools: */
    foreach (const UIToolType &enmRestrictedType, restrictedTypes)
        m_pPaneToolsGlobal->closeTool(enmRestrictedType);
}

void UIVirtualBoxManagerWidget::updateToolsMenuMachine(UIVirtualMachineItem *pItem)
{
    /* Get current item state: */
    const bool fCurrentItemIsOk = pItem && pItem->accessible();

    /* Update machine tools restrictions: */
    QSet<UIToolType> restrictedTypes;
    const bool fExpertMode = gEDataManager->isSettingsInExpertMode();
    if (!fExpertMode)
        restrictedTypes << UIToolType_FileManager;
    if (pItem && pItem->itemType() != UIVirtualMachineItemType_Local)
        restrictedTypes << UIToolType_Snapshots
                        << UIToolType_Logs
                        << UIToolType_FileManager;
    if (restrictedTypes.contains(m_pMenuToolsMachine->toolsType()))
        m_pMenuToolsMachine->setToolsType(UIToolType_Details);
    const QList restrictions(restrictedTypes.begin(), restrictedTypes.end());
    m_pMenuToolsMachine->setRestrictedToolTypes(restrictions);
    /* Update machine menu items availability: */
    m_pMenuToolsMachine->setItemsEnabled(fCurrentItemIsOk);

    /* Take restrictions into account, closing all restricted tools: */
    foreach (const UIToolType &enmRestrictedType, restrictedTypes)
        m_pPaneToolsMachine->closeTool(enmRestrictedType);
}

void UIVirtualBoxManagerWidget::handleCurrentToolTypeChange(UIToolType enmType)
{
    /* This method's behavior depends first of all of currently selected tool class.
     * But keep in mind, it is called for both Global and Machine type changes. */

    /* If Global tools currently chosen: */
    if (m_pStackedWidget->currentWidget() == m_pPaneToolsGlobal)
    {
        /* For the Global tool type changes,
         * start unconditionally updating all cloud VMs,
         * if Activity Overview tool currently chosen (even if VMs are not selected): */
        if (UIToolStuff::isTypeOfClass(enmType, UIToolClass_Global))
        {
            bool fActivityOverviewActive = enmType == UIToolType_VMActivityOverview;
            m_pPaneChooser->setKeepCloudNodesUpdated(fActivityOverviewActive);
            if (fActivityOverviewActive)
                m_pPaneToolsGlobal->setCloudMachineItems(m_pPaneChooser->cloudMachineItems());
        }
    }
    /* If Machine tools currently chosen: */
    else
    {
        /* Stop unconditionally updating all cloud VMs,
         * (tho they will still be updated if selected): */
        m_pPaneChooser->setKeepCloudNodesUpdated(false);
    }
}
