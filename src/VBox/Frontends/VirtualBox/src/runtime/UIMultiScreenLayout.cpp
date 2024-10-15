/* $Id: UIMultiScreenLayout.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMultiScreenLayout class implementation.
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

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMessageCenter.h"
#include "UIMultiScreenLayout.h"


UIMultiScreenLayout::UIMultiScreenLayout(UIMachineLogic *pMachineLogic)
    : m_pMachineLogic(pMachineLogic)
    , m_cGuestScreens(0)
    , m_cHostMonitors(0)
{
    prepare();
}

bool UIMultiScreenLayout::hasHostScreenForGuestScreen(int iScreenId) const
{
    return m_screenMap.contains(iScreenId);
}

int UIMultiScreenLayout::hostScreenForGuestScreen(int iScreenId) const
{
    return m_screenMap.value(iScreenId, 0);
}

quint64 UIMultiScreenLayout::memoryRequirements() const
{
    return memoryRequirements(m_screenMap);
}

void UIMultiScreenLayout::update()
{
    LogRelFlow(("UIMultiScreenLayout::update: Started...\n"));

    /* Clear screen-map initially: */
    m_screenMap.clear();

    /* Make a pool of available host screens: */
    QList<int> availableScreens;
    for (int i = 0; i < m_cHostMonitors; ++i)
        availableScreens << i;

    /* Load all combinations stored in the settings file.
     * We have to make sure they are valid, which means there have to be unique combinations
     * and all guests screens need there own host-monitor. */
    const bool fShouldWeAutoMountGuestScreens = gEDataManager->autoMountGuestScreensEnabled(uiCommon().managedVMUuid());
    LogRel(("GUI: UIMultiScreenLayout::update: GUI/AutomountGuestScreens is %s\n", fShouldWeAutoMountGuestScreens ? "enabled" : "disabled"));
    foreach (int iGuestScreen, m_guestScreens)
    {
        /* Initialize variables: */
        bool fValid = false;
        int iHostScreen = -1;

        if (!fValid)
        {
            /* If the user ever selected a combination in the view menu, we have the following entry: */
            iHostScreen = gEDataManager->hostScreenForPassedGuestScreen(iGuestScreen, uiCommon().managedVMUuid());
            /* Revalidate: */
            fValid =    iHostScreen >= 0 && iHostScreen < m_cHostMonitors /* In the host-monitor bounds? */
                     && m_screenMap.key(iHostScreen, -1) == -1; /* Not taken already? */
        }

        if (!fValid)
        {
            /* Check the position of the guest window in normal mode.
             * This makes sure that on first use fullscreen/seamless window opens on the same host-screen as the normal window was before.
             * This even works with multi-screen. The user just have to move all the normal windows to the target host-screens
             * and they will magically open there in fullscreen/seamless also. */
            QRect geo = gEDataManager->machineWindowGeometry(UIVisualStateType_Normal, iGuestScreen, uiCommon().managedVMUuid());
            /* If geometry is valid: */
            if (!geo.isNull())
            {
                /* Get top-left corner position: */
                QPoint topLeftPosition(geo.topLeft());
                /* Check which host-screen the position belongs to: */
                iHostScreen = UIDesktopWidgetWatchdog::screenNumber(topLeftPosition);
                /* Revalidate: */
                fValid =    iHostScreen >= 0 && iHostScreen < m_cHostMonitors /* In the host-monitor bounds? */
                         && m_screenMap.key(iHostScreen, -1) == -1; /* Not taken already? */
            }
        }

        if (!fValid)
        {
            /* If still not valid, pick the next one
             * if there is still available host-monitor: */
            if (!availableScreens.isEmpty())
            {
                iHostScreen = availableScreens.first();
                fValid = true;
            }
        }

        if (fValid)
        {
            /* Register host-monitor for the guest-screen: */
            m_screenMap.insert(iGuestScreen, iHostScreen);
            /* Remove it from the list of available host screens: */
            availableScreens.removeOne(iHostScreen);
        }
        /* Do we have opinion about what to do with excessive guest-screen? */
        else if (fShouldWeAutoMountGuestScreens)
        {
            /* Then we have to disable excessive guest-screen: */
            LogRel(("GUI: UIMultiScreenLayout::update: Disabling excessive guest-screen %d\n", iGuestScreen));
            uimachine()->setScreenVisibleHostDesires(iGuestScreen, false);
            uimachine()->setVideoModeHint(iGuestScreen,
                                          false /* enabled? */,
                                          false /* change origin? */,
                                          0 /* origin x */, 0 /* origin y */,
                                          0 /* width */, 0 /* height*/,
                                          0 /* bits per pixel */,
                                          true /* notify? */);
        }
    }

    /* Are we still have available host-screens
     * and have opinion about what to do with disabled guest-screens? */
    if (!availableScreens.isEmpty() && fShouldWeAutoMountGuestScreens)
    {
        /* How many excessive host-screens do we have? */
        int cExcessiveHostScreens = availableScreens.size();
        /* How many disabled guest-screens do we have? */
        int cDisabledGuestScreens = m_disabledGuestScreens.size();
        /* We have to try to enable disabled guest-screens if any: */
        int cGuestScreensToEnable = qMin(cExcessiveHostScreens, cDisabledGuestScreens);
        for (int iGuestScreenIndex = 0; iGuestScreenIndex < cGuestScreensToEnable; ++iGuestScreenIndex)
        {
            /* Defaults: */
            ULONG uWidth = 800;
            ULONG uHeight = 600;
            /* Try to get previous guest-screen arguments: */
            const int iGuestScreen = m_disabledGuestScreens.at(iGuestScreenIndex);
            const QSize guestScreenSize = uimachine()->guestScreenSize(iGuestScreen);
            {
                if (guestScreenSize.width() > 0)
                    uWidth = guestScreenSize.width();
                if (guestScreenSize.height() > 0)
                    uHeight = guestScreenSize.height();
            }
            /* Re-enable guest-screen with proper resolution: */
            LogRel(("GUI: UIMultiScreenLayout::update: Enabling guest-screen %d with following resolution: %dx%d\n",
                    iGuestScreen, uWidth, uHeight));
            uimachine()->setScreenVisibleHostDesires(iGuestScreen, true);
            uimachine()->setVideoModeHint(iGuestScreen,
                                          true/* enabled? */,
                                          false /* change origin? */,
                                          0 /* origin x */, 0 /* origin y */,
                                          uWidth, uHeight,
                                          32/* bits per pixel */,
                                          true /* notify? */);
        }
    }

    /* Make sure action-pool knows whether multi-screen layout has host-screen for guest-screen: */
    actionPool()->toRuntime()->setHostScreenForGuestScreenMap(m_screenMap);

    LogRelFlow(("UIMultiScreenLayout::update: Finished!\n"));
}

void UIMultiScreenLayout::rebuild()
{
    LogRelFlow(("UIMultiScreenLayout::rebuild: Started...\n"));

    /* Recalculate host/guest screen count: */
    calculateHostMonitorCount();
    calculateGuestScreenCount();

    /* Update layout: */
    update();

    LogRelFlow(("UIMultiScreenLayout::rebuild: Finished!\n"));
}

void UIMultiScreenLayout::sltHandleScreenLayoutChange(int iRequestedGuestScreen, int iRequestedHostMonitor)
{
    /* Search for the virtual screen which is currently displayed on the
     * requested host-monitor. When there is one found, we swap both. */
    QMap<int,int> tmpMap(m_screenMap);
    const int iCurrentGuestScreen = tmpMap.key(iRequestedHostMonitor, -1);
    if (iCurrentGuestScreen != -1 && tmpMap.contains(iRequestedGuestScreen))
        tmpMap.insert(iCurrentGuestScreen, tmpMap.value(iRequestedGuestScreen));
    else
        tmpMap.remove(iCurrentGuestScreen);
    tmpMap.insert(iRequestedGuestScreen, iRequestedHostMonitor);

    /* Check the memory requirements first: */
    bool fSuccess = true;
    if (uimachine()->isGuestSupportsGraphics())
    {
        ulong uVRAMSize = 0;
        uimachine()->acquireVRAMSize(uVRAMSize);
        const quint64 uAvailBits = uVRAMSize * _1M * 8;
        const quint64 uUsedBits = memoryRequirements(tmpMap);
        fSuccess = uAvailBits >= uUsedBits;
        if (!fSuccess)
        {
            /* We have too little video memory for the new layout, so say it to the user and revert all the changes: */
            if (machineLogic()->visualStateType() == UIVisualStateType_Seamless)
                msgCenter().cannotSwitchScreenInSeamless((((uUsedBits + 7) / 8 + _1M - 1) / _1M) * _1M);
            else
                fSuccess = msgCenter().cannotSwitchScreenInFullscreen((((uUsedBits + 7) / 8 + _1M - 1) / _1M) * _1M);
        }
    }
    /* Make sure memory requirements matched: */
    if (!fSuccess)
        return;

    /* Swap the maps: */
    m_screenMap = tmpMap;

    /* Make sure action-pool knows whether multi-screen layout has host-screen for guest-screen: */
    actionPool()->toRuntime()->setHostScreenForGuestScreenMap(m_screenMap);

    /* Save guest-to-host mapping: */
    foreach (const int &iGuestScreen, m_guestScreens)
    {
        const int iHostScreen = m_screenMap.value(iGuestScreen, -1);
        gEDataManager->setHostScreenForPassedGuestScreen(iGuestScreen, iHostScreen, uiCommon().managedVMUuid());
    }

    /* Notifies about layout change: */
    emit sigScreenLayoutChange();
}

void UIMultiScreenLayout::prepare()
{
    /* Make sure logic is always set: */
    AssertPtrReturnVoid(machineLogic());

    /* Recalculate host/guest screen count: */
    calculateHostMonitorCount();
    calculateGuestScreenCount();

    /* Prepare connections: */
    prepareConnections();
}

void UIMultiScreenLayout::prepareConnections()
{
    /* Connect action-pool: */
    connect(actionPool()->toRuntime(), &UIActionPoolRuntime::sigNotifyAboutTriggeringViewScreenRemap,
            this, &UIMultiScreenLayout::sltHandleScreenLayoutChange);
}

UIMachine *UIMultiScreenLayout::uimachine() const
{
    return machineLogic() ? machineLogic()->uimachine() : 0;
}

UIActionPool *UIMultiScreenLayout::actionPool() const
{
    return machineLogic() ? machineLogic()->actionPool() : 0;
}

void UIMultiScreenLayout::calculateHostMonitorCount()
{
    m_cHostMonitors = UIDesktopWidgetWatchdog::screenCount();
}

void UIMultiScreenLayout::calculateGuestScreenCount()
{
    m_guestScreens.clear();
    m_disabledGuestScreens.clear();
    uimachine()->acquireMonitorCount(m_cGuestScreens);
    for (uint iGuestScreen = 0; iGuestScreen < m_cGuestScreens; ++iGuestScreen)
        if (uimachine()->isScreenVisible(iGuestScreen))
            m_guestScreens << iGuestScreen;
        else
            m_disabledGuestScreens << iGuestScreen;
}

quint64 UIMultiScreenLayout::memoryRequirements(const QMap<int, int> &screenLayout) const
{
    quint64 uUsedBits = 0;
    const UIVisualStateType enmVisualStateType = machineLogic()->visualStateType();
    foreach (int iGuestScreen, m_guestScreens)
    {
        /* Make sure corresponding screen is valid: */
        const QRect screen = enmVisualStateType == UIVisualStateType_Fullscreen
                           ? gpDesktop->screenGeometry(screenLayout.value(iGuestScreen, 0))
                           : enmVisualStateType == UIVisualStateType_Seamless
                           ? gpDesktop->availableGeometry(screenLayout.value(iGuestScreen, 0))
                           : QRect();
        AssertReturn(screen.isValid(), 0);

        /* Get some useful screen info: */
        ulong uDummy = 0, uGuestBpp = 0;
        long iDummy = 0;
        KGuestMonitorStatus enmDummy = KGuestMonitorStatus_Disabled;
        uimachine()->acquireGuestScreenParameters(iGuestScreen, uDummy, uDummy, uGuestBpp,
                                                  iDummy, iDummy, enmDummy);
        uUsedBits += screen.width() * /* display width */
                     screen.height() * /* display height */
                     uGuestBpp + /* guest bits per pixel */
                     _1M * 8; /* current cache per screen - may be changed in future */
    }
    uUsedBits += 4096 * 8; /* adapter info */
    return uUsedBits;
}
