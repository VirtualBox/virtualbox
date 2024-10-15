/* $Id: UIGuestProcessControlDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestProcessControlDialog class implementation.
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
#include <QPushButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIGuestControlConsole.h"
#include "UIGuestProcessControlDialog.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UISession.h"
#include "UITranslationEventListener.h"


/*********************************************************************************************************************************
*   Class UIGuestProcessControlDialogFactory implementation.                                                                     *
*********************************************************************************************************************************/

UIGuestProcessControlDialogFactory::UIGuestProcessControlDialogFactory()
{
}

void UIGuestProcessControlDialogFactory::create(QIManagerDialog *&pDialog, QWidget *pCenterWidget)
{
    pDialog = new UIGuestProcessControlDialog(pCenterWidget);
}


/*********************************************************************************************************************************
*   Class UIGuestProcessControlDialog implementation.                                                                            *
*********************************************************************************************************************************/

UIGuestProcessControlDialog::UIGuestProcessControlDialog(QWidget *pCenterWidget)
    : QIManagerDialog(pCenterWidget)
    , m_comGuest(gpMachine->uisession()->guest())
    , m_strMachineName(gpMachine->machineName())
{
}

void UIGuestProcessControlDialog::sltRetranslateUI()
{
    /* Translate window title: */
    setWindowTitle(tr("%1 - Guest Control").arg(m_strMachineName));
    /* Translate buttons: */
    button(ButtonType_Close)->setText(tr("Close"));
}

void UIGuestProcessControlDialog::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/performance_monitor_32px.png" ,":/performance_monitor_16px.png"));
#endif
}

void UIGuestProcessControlDialog::configureCentralWidget()
{
    /* Create widget: */
    UIGuestControlConsole *pConsole = new UIGuestControlConsole(m_comGuest);

    if (pConsole)
    {
        /* Configure widget: */
        setWidget(pConsole);
        //setWidgetMenu(pWidget->menu());
#ifdef VBOX_WS_MAC
        //setWidgetToolbar(pWidget->toolbar());
#endif
        /* Add into layout: */
        centralWidget()->layout()->addWidget(pConsole);
    }
}

void UIGuestProcessControlDialog::finalize()
{
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIGuestProcessControlDialog::sltRetranslateUI);
}

void UIGuestProcessControlDialog::loadSettings()
{
    /* Invent default window geometry: */
    const QRect availableGeo = gpDesktop->availableGeometry(this);
    const int iDefaultWidth = availableGeo.width() / 2;
    const int iDefaultHeight = availableGeo.height() * 3 / 4;
    QRect defaultGeo(0, 0, iDefaultWidth, iDefaultHeight);

    /* Load geometry from extradata: */
    QRect geo = gEDataManager->guestProcessControlDialogGeometry(this, centerWidget(), defaultGeo);
    LogRel2(("GUI: UIGuestProcessControlDialog: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));
    restoreGeometry(geo);
}

void UIGuestProcessControlDialog::saveSettings()
{
    /* Save geometry to extradata: */
    const QRect geo = currentGeometry();
    LogRel2(("GUI: UIGuestProcessControlDialog: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));
    gEDataManager->setGuestProcessControlDialogGeometry(geo, isCurrentlyMaximized());
}

bool UIGuestProcessControlDialog::shouldBeMaximized() const
{
    return gEDataManager->guestProcessControlDialogShouldBeMaximized();
}

void UIGuestProcessControlDialog::sltSetCloseButtonShortCut(QKeySequence shortcut)
{
    if (button(ButtonType_Close))
        button(ButtonType_Close)->setShortcut(shortcut);
}
