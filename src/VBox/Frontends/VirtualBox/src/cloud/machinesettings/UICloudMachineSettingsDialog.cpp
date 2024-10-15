/* $Id: UICloudMachineSettingsDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudMachineSettingsDialog class implementation.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <QCloseEvent>
#include <QPushButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UICloudMachineSettingsDialog.h"
#include "UICloudMachineSettingsDialogPage.h"
#include "UICloudNetworkingStuff.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UINotificationCenter.h"
#include "UITranslationEventListener.h"


UICloudMachineSettingsDialog::UICloudMachineSettingsDialog(QWidget *pParent, const CCloudMachine &comCloudMachine)
    : QWidget(pParent, Qt::Window)
    , m_fPolished(false)
    , m_fClosable(true)
    , m_fClosed(false)
    , m_comCloudMachine(comCloudMachine)
    , m_pPage(0)
    , m_pButtonBox(0)
    , m_pNotificationCenter(0)
{
    prepare();
}

UICloudMachineSettingsDialog::~UICloudMachineSettingsDialog()
{
    cleanup();
}

void UICloudMachineSettingsDialog::setCloudMachine(const CCloudMachine &comCloudMachine)
{
    m_comCloudMachine = comCloudMachine;
    load();
}

void UICloudMachineSettingsDialog::sltRetranslateUI()
{
    /* Translate title: */
    const QString strCaption = tr("Settings");
    if (m_strName.isNull())
        setWindowTitle(strCaption);
    else
        setWindowTitle(QString("%1 - %2").arg(m_strName, strCaption));
}

void UICloudMachineSettingsDialog::showEvent(QShowEvent *pEvent)
{
    /* Polish stuff: */
    if (!m_fPolished)
    {
        m_fPolished = true;
        polishEvent(pEvent);
    }

    /* Call to base-class: */
    QWidget::showEvent(pEvent);
}

void UICloudMachineSettingsDialog::polishEvent(QShowEvent*)
{
    /* Explicit centering according to our parent: */
    gpDesktop->centerWidget(this, parentWidget(), false);
    /* Call for load stuff asynchronously: */
    QMetaObject::invokeMethod(this, "init", Qt::QueuedConnection);
}

void UICloudMachineSettingsDialog::closeEvent(QCloseEvent *pEvent)
{
    /* Ignore event initially: */
    pEvent->ignore();

    /* Do not close if NOT closable: */
    if (!m_fClosable)
        return;

    /* Tell the listener to close us (once): */
    if (!m_fClosed)
    {
        m_fClosed = true;
        emit sigClose();
    }
}

void UICloudMachineSettingsDialog::setOkButtonEnabled(bool fEnabled)
{
    AssertPtrReturnVoid(m_pButtonBox);
    AssertPtrReturnVoid(m_pButtonBox->button(QDialogButtonBox::Ok));
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(fEnabled);
}

void UICloudMachineSettingsDialog::prepare()
{
    /* Prepare local notification-center (parent to be assigned in the end): */
    m_pNotificationCenter = new UINotificationCenter(0);

    /* Prepare layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare page: */
        m_pPage = new UICloudMachineSettingsDialogPage(this);
        if (m_pPage)
        {
            connect(m_pPage.data(), &UICloudMachineSettingsDialogPage::sigValidChanged,
                    this, &UICloudMachineSettingsDialog::setOkButtonEnabled);
            /* Add into layout: */
            pLayout->addWidget(m_pPage);
        }

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox;
        if (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            m_pButtonBox->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
            connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &UICloudMachineSettingsDialog::accept);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &UICloudMachineSettingsDialog::close);
            setOkButtonEnabled(false);
            /* Add into layout: */
            pLayout->addWidget(m_pButtonBox);
        }
    }

    /* Assign notification-center parent (after everything else is done): */
    m_pNotificationCenter->setParent(this);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloudMachineSettingsDialog::sltRetranslateUI);
}

void UICloudMachineSettingsDialog::cleanup()
{
    /* Cleanup local notification-center: */
    delete m_pNotificationCenter;
    m_pNotificationCenter = 0;
}

void UICloudMachineSettingsDialog::load()
{
    /* Prevent closing: */
    m_fClosable = false;

    /* Update name: */
    if (!cloudMachineName(m_comCloudMachine, m_strName, notificationCenter()))
        close();

    /* Retranslate title: */
    sltRetranslateUI();

    /* Update form: */
    if (!cloudMachineSettingsForm(m_comCloudMachine, m_comForm, notificationCenter()))
        close();

    /* Assign page with form: */
    m_pPage->setForm(m_comForm);

    /* Allow closing again: */
    m_fClosable = true;
}

void UICloudMachineSettingsDialog::save()
{
    /* Makes sure page data committed: */
    if (m_pPage)
        m_pPage->makeSureDataCommitted();

    /* Apply form: */
    AssertReturnVoid(m_comForm.isNotNull());
    if (!applyCloudMachineSettingsForm(m_comCloudMachine, m_comForm, notificationCenter()))
        return;

    /* Just close for now: */
    close();
}
