/* $Id: UIMachineSettingsPortForwardingDlg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsPortForwardingDlg class implementation.
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
#include <QVBoxLayout>
#include <QPushButton>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIMachineSettingsPortForwardingDlg.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UITranslationEventListener.h"

UIMachineSettingsPortForwardingDlg::UIMachineSettingsPortForwardingDlg(QWidget *pParent,
                                                                       const UIPortForwardingDataList &rules)
    : QIDialog(pParent)
    , m_pTable(0)
    , m_pButtonBox(0)
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/nw_32px.png", ":/nw_16px.png"));
#endif

    /* Create layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    {
        /* Create table: */
        m_pTable = new UIPortForwardingTable(rules, false, true);
        {
            /* Configure table: */
            m_pTable->layout()->setContentsMargins(0, 0, 0, 0);
        }
        /* Create button-box: */
        m_pButtonBox = new QIDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal);
        {
            /* Configure button-box: */
            connect(m_pButtonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked,
                    this, &UIMachineSettingsPortForwardingDlg::accept);
            connect(m_pButtonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
                    this, &UIMachineSettingsPortForwardingDlg::reject);
        }
        /* Add widgets into layout: */
        pMainLayout->addWidget(m_pTable);
        pMainLayout->addWidget(m_pButtonBox);
    }

    /* Retranslate dialog: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMachineSettingsPortForwardingDlg::sltRetranslateUI);

    /* Limit the minimum size to 33% of screen size: */
    setMinimumSize(gpDesktop->screenGeometry(this).size() / 3);
}

const UIPortForwardingDataList UIMachineSettingsPortForwardingDlg::rules() const
{
    return m_pTable->rules();
}

void UIMachineSettingsPortForwardingDlg::accept()
{
    /* Make sure table has own data committed: */
    m_pTable->makeSureEditorDataCommitted();
    /* Validate table: */
    bool fPassed = m_pTable->validate();
    if (!fPassed)
        return;
    /* Call to base-class: */
    QIDialog::accept();
}

void UIMachineSettingsPortForwardingDlg::reject()
{
    /* Ask user to discard table changes if necessary: */
    if (   m_pTable->isChanged()
        && !msgCenter().confirmCancelingPortForwardingDialog(window()))
        return;
    /* Call to base-class: */
    QIDialog::reject();
}

void UIMachineSettingsPortForwardingDlg::sltRetranslateUI()
{
    /* Set window title: */
    setWindowTitle(tr("Port Forwarding Rules"));
}
