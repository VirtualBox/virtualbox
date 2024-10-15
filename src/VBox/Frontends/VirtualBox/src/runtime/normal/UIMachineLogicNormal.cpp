/* $Id: UIMachineLogicNormal.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineLogicNormal class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#ifndef VBOX_WS_MAC
# include <QTimer>
#endif /* !VBOX_WS_MAC */

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogicNormal.h"
#include "UIMachineWindow.h"
#include "UIMenuBarEditor.h"
#include "UIMessageCenter.h"
#include "UIStatusBarEditor.h"
#ifndef VBOX_WS_MAC
# include "QIMenu.h"
#else
# include "VBoxUtils.h"
#endif

/* COM includes: */
#include "CConsole.h"
#include "CGraphicsAdapter.h"


UIMachineLogicNormal::UIMachineLogicNormal(UIMachine *pMachine)
    : UIMachineLogic(pMachine)
#ifndef VBOX_WS_MAC
    , m_pPopupMenu(0)
#endif /* !VBOX_WS_MAC */
{
}

bool UIMachineLogicNormal::checkAvailability()
{
    /* Normal mode is always available: */
    return true;
}

void UIMachineLogicNormal::sltCheckForRequestedVisualStateType()
{
    LogRel(("GUI: UIMachineLogicNormal::sltCheckForRequestedVisualStateType: Requested-state=%d, Machine-state=%d\n",
            uimachine()->requestedVisualState(), uimachine()->machineState()));

    /* Do not try to change visual-state type if machine was not started yet: */
    if (!uimachine()->isRunning() && !uimachine()->isPaused())
        return;

    /* Do not try to change visual-state type in 'manual override' mode: */
    if (uimachine()->isManualOverrideMode())
        return;

    /* Check requested visual-state types: */
    switch (uimachine()->requestedVisualState())
    {
        /* If 'seamless' visual-state type is requested: */
        case UIVisualStateType_Seamless:
        {
            /* And supported: */
            if (uimachine()->isGuestSupportsSeamless())
            {
                LogRel(("GUI: UIMachineLogicNormal::sltCheckForRequestedVisualStateType: "
                        "Going 'seamless' as requested...\n"));
                uimachine()->setRequestedVisualState(UIVisualStateType_Invalid);
                uimachine()->asyncChangeVisualState(UIVisualStateType_Seamless);
            }
            else
                LogRel(("GUI: UIMachineLogicNormal::sltCheckForRequestedVisualStateType: "
                        "Rejecting 'seamless' as is it not yet supported...\n"));
            break;
        }
        default:
            break;
    }
}

#ifndef RT_OS_DARWIN
void UIMachineLogicNormal::sltInvokePopupMenu()
{
    /* Popup main-menu if present: */
    if (m_pPopupMenu && !m_pPopupMenu->isEmpty())
    {
        m_pPopupMenu->popup(activeMachineWindow()->geometry().center());
        QTimer::singleShot(0, m_pPopupMenu, SLOT(sltHighlightFirstAction()));
    }
}
#endif /* RT_OS_DARWIN */

void UIMachineLogicNormal::sltOpenMenuBarSettings()
{
    /* Do not process if window(s) missed! */
    AssertReturnVoid(isMachineWindowsCreated());

#ifndef VBOX_WS_MAC
    /* Make sure menu-bar is enabled: */
    const bool fEnabled = actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility)->isChecked();
    AssertReturnVoid(fEnabled);
#endif /* !VBOX_WS_MAC */

    /* Prevent user from opening another one editor or toggle menu-bar: */
    actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_S_Settings)->setEnabled(false);
#ifndef VBOX_WS_MAC
    actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility)->setEnabled(false);
#endif /* !VBOX_WS_MAC */
    /* Create menu-bar editor: */
    UIMenuBarEditorWindow *pMenuBarEditor = new UIMenuBarEditorWindow(activeMachineWindow(), actionPool());
    AssertPtrReturnVoid(pMenuBarEditor);
    {
        /* Configure menu-bar editor: */
        connect(pMenuBarEditor, &UIMenuBarEditorWindow::destroyed,
                this, &UIMachineLogicNormal::sltMenuBarSettingsClosed);
        /* Show window: */
        pMenuBarEditor->show();
    }
}

void UIMachineLogicNormal::sltMenuBarSettingsClosed()
{
#ifndef VBOX_WS_MAC
    /* Make sure menu-bar is enabled: */
    const bool fEnabled = actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility)->isChecked();
    AssertReturnVoid(fEnabled);
#endif /* !VBOX_WS_MAC */

    /* Allow user to open editor and toggle menu-bar again: */
    actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_S_Settings)->setEnabled(true);
#ifndef VBOX_WS_MAC
    actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility)->setEnabled(true);
#endif /* !VBOX_WS_MAC */
}

#ifndef RT_OS_DARWIN
void UIMachineLogicNormal::sltToggleMenuBar()
{
    /* Do not process if window(s) missed! */
    AssertReturnVoid(isMachineWindowsCreated());

    /* Invert menu-bar availability option: */
    const bool fEnabled = gEDataManager->menuBarEnabled(uiCommon().managedVMUuid());
    gEDataManager->setMenuBarEnabled(!fEnabled, uiCommon().managedVMUuid());
}
#endif /* !RT_OS_DARWIN */

void UIMachineLogicNormal::sltOpenStatusBarSettings()
{
    /* Do not process if window(s) missed! */
    AssertReturnVoid(isMachineWindowsCreated());

    /* Make sure status-bar is enabled: */
    const bool fEnabled = actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility)->isChecked();
    AssertReturnVoid(fEnabled);

    /* Prevent user from opening another one editor or toggle status-bar: */
    actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_S_Settings)->setEnabled(false);
    actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility)->setEnabled(false);
    /* Create status-bar editor: */
    UIStatusBarEditorWindow *pStatusBarEditor = new UIStatusBarEditorWindow(activeMachineWindow());
    AssertPtrReturnVoid(pStatusBarEditor);
    {
        /* Configure status-bar editor: */
        connect(pStatusBarEditor, &UIStatusBarEditorWindow::destroyed,
                this, &UIMachineLogicNormal::sltStatusBarSettingsClosed);
        /* Show window: */
        pStatusBarEditor->show();
    }
}

void UIMachineLogicNormal::sltStatusBarSettingsClosed()
{
    /* Make sure status-bar is enabled: */
    const bool fEnabled = actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility)->isChecked();
    AssertReturnVoid(fEnabled);

    /* Allow user to open editor and toggle status-bar again: */
    actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_S_Settings)->setEnabled(true);
    actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility)->setEnabled(true);
}

void UIMachineLogicNormal::sltToggleStatusBar()
{
    /* Do not process if window(s) missed! */
    AssertReturnVoid(isMachineWindowsCreated());

    /* Invert status-bar availability option: */
    const bool fEnabled = gEDataManager->statusBarEnabled(uiCommon().managedVMUuid());
    gEDataManager->setStatusBarEnabled(!fEnabled, uiCommon().managedVMUuid());
}

void UIMachineLogicNormal::sltHostScreenAvailableAreaChange()
{
#if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    /* Prevent handling if fake screen detected: */
    if (UIDesktopWidgetWatchdog::isFakeScreenDetected())
        return;

    /* Make sure all machine-window(s) have previous but normalized geometry: */
    foreach (UIMachineWindow *pMachineWindow, machineWindows())
        if (!pMachineWindow->isMaximized())
            pMachineWindow->restoreCachedGeometry();
#endif /* VBOX_WS_NIX && !VBOX_GUI_WITH_CUSTOMIZATIONS1 */

    /* Call to base-class: */
    UIMachineLogic::sltHostScreenAvailableAreaChange();
}

void UIMachineLogicNormal::prepareActionGroups()
{
    /* Call to base-class: */
    UIMachineLogic::prepareActionGroups();

    /* Restrict 'Remap' actions for 'View' menu: */
    actionPool()->toRuntime()->setRestrictionForMenuView(UIActionRestrictionLevel_Logic,
                                                         (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
                                                         (UIExtraDataMetaDefs::RuntimeMenuViewActionType_Remap));
}

void UIMachineLogicNormal::prepareActionConnections()
{
    /* Call to base-class: */
    UIMachineLogic::prepareActionConnections();

    /* Prepare 'View' actions connections: */
    connect(actionPool()->action(UIActionIndexRT_M_View_T_Fullscreen), &UIAction::triggered,
            this, &UIMachineLogicNormal::sltChangeVisualStateToFullscreen);
    connect(actionPool()->action(UIActionIndexRT_M_View_T_Seamless), &UIAction::triggered,
            this, &UIMachineLogicNormal::sltChangeVisualStateToSeamless);
    connect(actionPool()->action(UIActionIndexRT_M_View_T_Scale), &UIAction::triggered,
            this, &UIMachineLogicNormal::sltChangeVisualStateToScale);
    connect(actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_S_Settings), &UIAction::triggered,
            this, &UIMachineLogicNormal::sltOpenMenuBarSettings);
#ifndef VBOX_WS_MAC
    connect(actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility), &UIAction::triggered,
            this, &UIMachineLogicNormal::sltToggleMenuBar);
#endif /* !VBOX_WS_MAC */
    connect(actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_S_Settings), &UIAction::triggered,
            this, &UIMachineLogicNormal::sltOpenStatusBarSettings);
    connect(actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility), &UIAction::triggered,
            this, &UIMachineLogicNormal::sltToggleStatusBar);
}

void UIMachineLogicNormal::prepareMachineWindows()
{
    /* Do not create machine-window(s) if they created already: */
    if (isMachineWindowsCreated())
        return;

#ifdef VBOX_WS_MAC /// @todo Is that really need here?
    /* We have to make sure that we are getting the front most process.
     * This is necessary for Qt versions > 4.3.3: */
    ::darwinSetFrontMostProcess();
#endif /* VBOX_WS_MAC */

    /* Acquire monitor count: */
    ulong cMonitorCount = 0;
    uimachine()->acquireMonitorCount(cMonitorCount);

    /* Create machine window(s): */
    for (ulong uScreenId = 0; uScreenId < cMonitorCount; ++ uScreenId)
        addMachineWindow(UIMachineWindow::create(this, uScreenId));
    /* Order machine window(s): */
    for (ulong uScreenId = cMonitorCount; uScreenId > 0; -- uScreenId)
        machineWindows()[uScreenId - 1]->raise();

    /* Mark machine-window(s) created: */
    setMachineWindowsCreated(true);
}

#ifndef VBOX_WS_MAC
void UIMachineLogicNormal::prepareMenu()
{
    /* Prepare popup-menu: */
    m_pPopupMenu = new QIMenu;
    AssertPtrReturnVoid(m_pPopupMenu);
    {
        /* Prepare popup-menu: */
        foreach (QMenu *pMenu, actionPool()->menus())
            m_pPopupMenu->addMenu(pMenu);
    }
}
#endif /* !VBOX_WS_MAC */

#ifndef VBOX_WS_MAC
void UIMachineLogicNormal::cleanupMenu()
{
    /* Cleanup popup-menu: */
    delete m_pPopupMenu;
    m_pPopupMenu = 0;
}
#endif /* !VBOX_WS_MAC */

void UIMachineLogicNormal::cleanupMachineWindows()
{
    /* Do not destroy machine-window(s) if they destroyed already: */
    if (!isMachineWindowsCreated())
        return;

    /* Mark machine-window(s) destroyed: */
    setMachineWindowsCreated(false);

    /* Cleanup machine-window(s): */
    foreach (UIMachineWindow *pMachineWindow, machineWindows())
        UIMachineWindow::destroy(pMachineWindow);
}

void UIMachineLogicNormal::cleanupActionConnections()
{
    /* "View" actions disconnections: */
    disconnect(actionPool()->action(UIActionIndexRT_M_View_T_Fullscreen), &UIAction::triggered,
               this, &UIMachineLogicNormal::sltChangeVisualStateToFullscreen);
    disconnect(actionPool()->action(UIActionIndexRT_M_View_T_Seamless), &UIAction::triggered,
               this, &UIMachineLogicNormal::sltChangeVisualStateToSeamless);
    disconnect(actionPool()->action(UIActionIndexRT_M_View_T_Scale), &UIAction::triggered,
               this, &UIMachineLogicNormal::sltChangeVisualStateToScale);
    disconnect(actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_S_Settings), &UIAction::triggered,
               this, &UIMachineLogicNormal::sltOpenMenuBarSettings);
#ifndef VBOX_WS_MAC
    disconnect(actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility), &UIAction::triggered,
               this, &UIMachineLogicNormal::sltToggleMenuBar);
#endif /* !VBOX_WS_MAC */
    disconnect(actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_S_Settings), &UIAction::triggered,
               this, &UIMachineLogicNormal::sltOpenStatusBarSettings);
    disconnect(actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility), &UIAction::triggered,
               this, &UIMachineLogicNormal::sltToggleStatusBar);

    /* Call to base-class: */
    UIMachineLogic::cleanupActionConnections();
}
