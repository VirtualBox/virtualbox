/* $Id: UIMachineLogicScale.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineLogicScale class implementation.
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
#endif

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIMachine.h"
#include "UIMachineLogicScale.h"
#include "UIMachineWindow.h"
#include "UIMessageCenter.h"
#include "UIShortcutPool.h"
#ifndef VBOX_WS_MAC
# include "QIMenu.h"
#else
# include "VBoxUtils.h"
#endif

/* COM includes: */
#include "CGraphicsAdapter.h"


UIMachineLogicScale::UIMachineLogicScale(UIMachine *pMachine)
    : UIMachineLogic(pMachine)
#ifndef VBOX_WS_MAC
    , m_pPopupMenu(0)
#endif /* !VBOX_WS_MAC */
{
}

bool UIMachineLogicScale::checkAvailability()
{
    /* Show the info message. */
    const UIShortcut &shortcut =
            gShortcutPool->shortcut(actionPool()->shortcutsExtraDataID(),
                                    actionPool()->action(UIActionIndexRT_M_View_T_Scale)->shortcutExtraDataID());
    const QString strHotKey = QString("Host+%1").arg(shortcut.primaryToPortableText());
    if (!msgCenter().confirmGoingScale(strHotKey))
        return false;

    return true;
}

#ifndef VBOX_WS_MAC
void UIMachineLogicScale::sltInvokePopupMenu()
{
    /* Popup main-menu if present: */
    if (m_pPopupMenu && !m_pPopupMenu->isEmpty())
    {
        m_pPopupMenu->popup(activeMachineWindow()->geometry().center());
        QTimer::singleShot(0, m_pPopupMenu, SLOT(sltHighlightFirstAction()));
    }
}
#endif /* !VBOX_WS_MAC */

void UIMachineLogicScale::sltHostScreenAvailableAreaChange()
{
#ifdef VBOX_WS_NIX
    /* Prevent handling if fake screen detected: */
    if (UIDesktopWidgetWatchdog::isFakeScreenDetected())
        return;

    /* Make sure all machine-window(s) have previous but normalized geometry: */
    foreach (UIMachineWindow *pMachineWindow, machineWindows())
        pMachineWindow->restoreCachedGeometry();
#endif /* VBOX_WS_NIX */

    /* Call to base-class: */
    UIMachineLogic::sltHostScreenAvailableAreaChange();
}

void UIMachineLogicScale::prepareActionGroups()
{
    /* Call to base-class: */
    UIMachineLogic::prepareActionGroups();

    /* Restrict 'Adjust Window', 'Guest Autoresize', 'Status Bar', 'Resize' and 'Rescale' actions for 'View' menu: */
    actionPool()->toRuntime()->setRestrictionForMenuView(UIActionRestrictionLevel_Logic,
                                                         (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
                                                         (UIExtraDataMetaDefs::RuntimeMenuViewActionType_AdjustWindow |
                                                          UIExtraDataMetaDefs::RuntimeMenuViewActionType_GuestAutoresize |
                                                          UIExtraDataMetaDefs::RuntimeMenuViewActionType_MenuBar |
                                                          UIExtraDataMetaDefs::RuntimeMenuViewActionType_StatusBar |
                                                          UIExtraDataMetaDefs::RuntimeMenuViewActionType_Resize |
                                                          UIExtraDataMetaDefs::RuntimeMenuViewActionType_Rescale));

    /* Take care of view-action toggle state: */
    UIAction *pActionScale = actionPool()->action(UIActionIndexRT_M_View_T_Scale);
    if (!pActionScale->isChecked())
    {
        pActionScale->blockSignals(true);
        pActionScale->setChecked(true);
        pActionScale->blockSignals(false);
    }
}

void UIMachineLogicScale::prepareActionConnections()
{
    /* Call to base-class: */
    UIMachineLogic::prepareActionConnections();

    /* Prepare 'View' actions connections: */
    connect(actionPool()->action(UIActionIndexRT_M_View_T_Scale), &QAction::triggered,
            this, &UIMachineLogicScale::sltChangeVisualStateToNormal);
    connect(actionPool()->action(UIActionIndexRT_M_View_T_Fullscreen), &QAction::triggered,
            this, &UIMachineLogicScale::sltChangeVisualStateToFullscreen);
    connect(actionPool()->action(UIActionIndexRT_M_View_T_Seamless), &QAction::triggered,
            this, &UIMachineLogicScale::sltChangeVisualStateToSeamless);
}

void UIMachineLogicScale::prepareMachineWindows()
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
void UIMachineLogicScale::prepareMenu()
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
void UIMachineLogicScale::cleanupMenu()
{
    /* Cleanup popup-menu: */
    delete m_pPopupMenu;
    m_pPopupMenu = 0;
}
#endif /* !VBOX_WS_MAC */

void UIMachineLogicScale::cleanupMachineWindows()
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

void UIMachineLogicScale::cleanupActionConnections()
{
    /* "View" actions disconnections: */
    disconnect(actionPool()->action(UIActionIndexRT_M_View_T_Scale), &QAction::triggered,
               this, &UIMachineLogicScale::sltChangeVisualStateToNormal);
    disconnect(actionPool()->action(UIActionIndexRT_M_View_T_Fullscreen), &QAction::triggered,
               this, &UIMachineLogicScale::sltChangeVisualStateToFullscreen);
    disconnect(actionPool()->action(UIActionIndexRT_M_View_T_Seamless), &QAction::triggered,
               this, &UIMachineLogicScale::sltChangeVisualStateToSeamless);

    /* Call to base-class: */
    UIMachineLogic::cleanupActionConnections();

}

void UIMachineLogicScale::cleanupActionGroups()
{
    /* Take care of view-action toggle state: */
    UIAction *pActionScale = actionPool()->action(UIActionIndexRT_M_View_T_Scale);
    if (pActionScale->isChecked())
    {
        pActionScale->blockSignals(true);
        pActionScale->setChecked(false);
        pActionScale->blockSignals(false);
    }

    /* Allow 'Adjust Window', 'Guest Autoresize', 'Status Bar' and 'Resize' actions for 'View' menu: */
    actionPool()->toRuntime()->setRestrictionForMenuView(UIActionRestrictionLevel_Logic,
                                                         UIExtraDataMetaDefs::RuntimeMenuViewActionType_Invalid);

    /* Call to base-class: */
    UIMachineLogic::cleanupActionGroups();
}
