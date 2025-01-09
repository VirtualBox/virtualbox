/* $Id: QIManagerDialog.cpp 107666 2025-01-09 15:52:29Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIManagerDialog class implementation.
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
#include <QMenuBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIManagerDialog.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIHelpBrowserDialog.h"
#include "UIMessageCenter.h"
#include "UIShortcutPool.h"
#ifdef VBOX_WS_MAC
# include "QIToolBar.h"
# include "UIWindowMenuManager.h"
#endif


/*********************************************************************************************************************************
*   Class QIManagerDialogFactory implementation.                                                                                 *
*********************************************************************************************************************************/

void QIManagerDialogFactory::prepare(QIManagerDialog *&pDialog, QWidget *pCenterWidget /* = 0 */)
{
    create(pDialog, pCenterWidget);
    pDialog->prepare();
}

void QIManagerDialogFactory::cleanup(QIManagerDialog *&pDialog)
{
    if (pDialog)
    {
        pDialog->cleanup();
        delete pDialog;
        pDialog = 0;
    }
}


/*********************************************************************************************************************************
*   Class QIManagerDialog implementation.                                                                                        *
*********************************************************************************************************************************/

QIManagerDialog::QIManagerDialog(QWidget *pCenterWidget)
    : m_pCenterWidget(pCenterWidget)
    , m_fCloseEmitted(false)
    , m_pWidget(0)
#ifdef VBOX_WS_MAC
    , m_pWidgetToolbar(0)
#endif
    , m_pButtonBox(0)
{
}

void QIManagerDialog::closeEvent(QCloseEvent *pEvent)
{
    /* Ignore the event itself: */
    pEvent->ignore();
    /* But tell the listener to close us (once): */
    if (!m_fCloseEmitted)
    {
        m_fCloseEmitted = true;
        emit sigClose();
    }
}

void QIManagerDialog::sltHandleHelpRequested()
{
    UIHelpBrowserDialog::findManualFileAndShow(uiCommon().helpKeyword(m_pWidget));
}

void QIManagerDialog::prepare()
{
    /* Tell the application we are not that important: */
    setAttribute(Qt::WA_QuitOnClose, false);

    /* Invent initial size: */
    QSize proposedSize;
    const int iHostScreen = UIDesktopWidgetWatchdog::screenNumber(m_pCenterWidget);
    if (iHostScreen >= 0 && iHostScreen < UIDesktopWidgetWatchdog::screenCount())
    {
        /* On the basis of current host-screen geometry if possible: */
        const QRect screenGeometry = gpDesktop->screenGeometry(iHostScreen);
        if (screenGeometry.isValid())
            proposedSize = screenGeometry.size() * 7 / 15;
    }
    /* Fallback to default size if we failed: */
    if (proposedSize.isNull())
        proposedSize = QSize(800, 600);
    /* Resize to initial size: */
    resize(proposedSize);

    /* Configure: */
    configure();

    /* Prepare central-widget: */
    prepareCentralWidget();
    /* Prepare menu-bar: */
    prepareMenuBar();
#ifdef VBOX_WS_MAC
    /* Prepare toolbar: */
    prepareToolBar();
#endif

    /* Finalize: */
    finalize();

    /* Center according requested widget: */
    gpDesktop->centerWidget(this, m_pCenterWidget, false);

    /* Load the dialog's settings from extradata */
    loadSettings();
}

void QIManagerDialog::prepareCentralWidget()
{
    /* Create central-widget: */
    setCentralWidget(new QWidget);
    AssertPtrReturnVoid(centralWidget());
    {
        /* Create main-layout: */
        new QVBoxLayout(centralWidget());
        AssertPtrReturnVoid(centralWidget()->layout());
        {
            /* Configure layout: */
            const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 2;
            const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) / 2;
            const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) / 2;
            const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) / 2;
            centralWidget()->layout()->setContentsMargins(iL, iT, iR, iB);

            /* Configure central-widget: */
            configureCentralWidget();

            /* Prepare button-box: */
            prepareButtonBox();
        }
    }
}

void QIManagerDialog::prepareButtonBox()
{
    /* Create button-box: */
    m_pButtonBox = new QIDialogButtonBox;
    AssertPtrReturnVoid(m_pButtonBox);
    {
        /* Configure button-box: */
        m_pButtonBox->setStandardButtons(  QDialogButtonBox::Reset
#ifdef VBOX_WS_WIN
                                         | QDialogButtonBox::Save
#else
                                         | QDialogButtonBox::Apply
#endif
                                         | QDialogButtonBox::Cancel
                                         | QDialogButtonBox::Close
                                         | QDialogButtonBox::Help);
        m_buttons[ButtonType_Reset] = m_pButtonBox->button(QDialogButtonBox::Reset);
#ifdef VBOX_WS_WIN
        m_buttons[ButtonType_Apply] = m_pButtonBox->button(QDialogButtonBox::Save);
#else
        m_buttons[ButtonType_Apply] = m_pButtonBox->button(QDialogButtonBox::Apply);
#endif
        m_buttons[ButtonType_Embed] = m_pButtonBox->button(QDialogButtonBox::Cancel);
        m_buttons[ButtonType_Close] = m_pButtonBox->button(QDialogButtonBox::Close);
        m_buttons[ButtonType_Help] = m_pButtonBox->button(QDialogButtonBox::Help);

        /* Hide some of buttons initially: */
        button(ButtonType_Reset)->hide();
        button(ButtonType_Apply)->hide();
        button(ButtonType_Embed)->hide();
        /* Disable some of buttons initially: */
        button(ButtonType_Reset)->setEnabled(false);
        button(ButtonType_Apply)->setEnabled(false);
        button(ButtonType_Embed)->setEnabled(false);

        /* Configure connections: */
        connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &QIManagerDialog::close);
        connect(m_pButtonBox, &QIDialogButtonBox::helpRequested, this, &QIManagerDialog::sltHandleHelpRequested);

        /* Configure button-box: */
        configureButtonBox();

        /* Add into layout: */
        centralWidget()->layout()->addWidget(m_pButtonBox);
    }
}

void QIManagerDialog::prepareMenuBar()
{
    /* Skip the call if there are no menus to add: */
    if (m_widgetMenus.isEmpty())
        return;

    /* Add all the widget menus: */
    foreach (QMenu *pMenu, m_widgetMenus)
        menuBar()->addMenu(pMenu);

#ifdef VBOX_WS_MAC
    /* Prepare 'Window' menu: */
    if (gpWindowMenuManager)
    {
        menuBar()->addMenu(gpWindowMenuManager->createMenu(this));
        gpWindowMenuManager->addWindow(this);
    }
#endif
}

#ifdef VBOX_WS_MAC
void QIManagerDialog::prepareToolBar()
{
    if (!m_pWidgetToolbar)
        return;
    /* Enable unified toolbar on macOS: */
    addToolBar(m_pWidgetToolbar);
    m_pWidgetToolbar->enableMacToolbar();
}
#endif

void QIManagerDialog::cleanupMenuBar()
{
#ifdef VBOX_WS_MAC
    /* Cleanup 'Window' menu: */
    if (gpWindowMenuManager)
    {
        gpWindowMenuManager->removeWindow(this);
        gpWindowMenuManager->destroyMenu(this);
    }
#endif
}

void QIManagerDialog::cleanup()
{
    saveSettings();
    /* Cleanup menu-bar: */
    cleanupMenuBar();
}
