/* $Id: UIStarter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIStarter class implementation.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIStarter.h"
#ifndef VBOX_RUNTIME_UI
# include "UIVirtualBoxManager.h"
#else
# include "UIMachine.h"
#endif


UIStarter::UIStarter()
{
    /* Listen for UICommon signals: */
    connect(&uiCommon(), &UICommon::sigAskToRestartUI,
            this, &UIStarter::sltRestartUI);
    connect(&uiCommon(), &UICommon::sigAskToCloseUI,
            this, &UIStarter::sltCloseUI);
}

UIStarter::~UIStarter()
{
    /* Listen for UICommon signals no more: */
    disconnect(&uiCommon(), &UICommon::sigAskToRestartUI,
               this, &UIStarter::sltRestartUI);
    disconnect(&uiCommon(), &UICommon::sigAskToCloseUI,
               this, &UIStarter::sltCloseUI);
}

void UIStarter::sltStartUI()
{
    /* Exit if UICommon is not valid: */
    if (!uiCommon().isValid())
        return;

#ifndef VBOX_RUNTIME_UI

    /* Make sure Manager UI is permitted, quit if not: */
    if (gEDataManager->guiFeatureEnabled(GUIFeatureType_NoSelector))
    {
        msgCenter().cannotStartSelector();
        return QApplication::quit();
    }

    /* Create/show manager-window: */
    UIVirtualBoxManager::create();

# ifdef VBOX_BLEEDING_EDGE
    /* Show EXPERIMENTAL BUILD warning: */
    UINotificationMessage::remindAboutExperimentalBuild();
# else /* !VBOX_BLEEDING_EDGE */
#  ifndef DEBUG
    /* Show BETA warning if necessary: */
    const QString vboxVersion(gpGlobalSession->virtualBox().GetVersion());
    if (   vboxVersion.contains("BETA")
        && gEDataManager->preventBetaBuildWarningForVersion() != vboxVersion)
        UINotificationMessage::remindAboutBetaBuild();
#  endif /* !DEBUG */
# endif /* !VBOX_BLEEDING_EDGE */

#else /* VBOX_RUNTIME_UI */

    /* Make sure Runtime UI is possible at all, quit if not: */
    if (uiCommon().managedVMUuid().isNull())
    {
        msgCenter().cannotStartRuntime();
        return QApplication::quit();
    }

    /* Try to start virtual machine, quit if failed: */
    if (!UIMachine::startMachine())
        return QApplication::quit();

#endif /* VBOX_RUNTIME_UI */
}

void UIStarter::sltRestartUI()
{
#ifndef VBOX_RUNTIME_UI
    /* Recreate/show manager-window: */
    UIVirtualBoxManager::destroy();
    UIVirtualBoxManager::create();
#endif
}

void UIStarter::sltCloseUI()
{
#ifndef VBOX_RUNTIME_UI
    /* Destroy Manager UI: */
    UIVirtualBoxManager::destroy();
#else
    /* Destroy Runtime UI: */
    UIMachine::destroy();
#endif
}
