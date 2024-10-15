/* $Id: VBoxLicenseViewer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - VBoxLicenseViewer class implementation.
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
#include <QEvent>
#include <QFile>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "VBoxLicenseViewer.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UITranslationEventListener.h"

VBoxLicenseViewer::VBoxLicenseViewer(QWidget *pParent /* = 0 */)
    : QDialog(pParent)
    , m_pLicenseBrowser(0)
    , m_pButtonAgree(0)
    , m_pButtonDisagree(0)
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/log_viewer_find_32px.png", ":/log_viewer_find_16px.png"));
#endif

    /* Create main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Create license browser: */
        m_pLicenseBrowser = new QTextBrowser(this);
        if (m_pLicenseBrowser)
        {
            /* Configure license browser: */
            m_pLicenseBrowser->verticalScrollBar()->installEventFilter(this);
            connect(m_pLicenseBrowser->verticalScrollBar(), &QScrollBar::valueChanged,
                    this, &VBoxLicenseViewer::sltHandleScrollBarMoved);

            /* Add into layout: */
            pMainLayout->addWidget(m_pLicenseBrowser);
        }

        /* Create agree button: */
        /** @todo rework buttons to be a part of button-box itself */
        QDialogButtonBox *pDialogButtonBox = new QIDialogButtonBox;
        if (pDialogButtonBox)
        {
            /* Create agree button: */
            m_pButtonAgree = new QPushButton;
            if (m_pButtonAgree)
            {
                /* Configure button: */
                connect(m_pButtonAgree, &QPushButton::clicked, this, &QDialog::accept);

                /* Add into button-box: */
                pDialogButtonBox->addButton(m_pButtonAgree, QDialogButtonBox::AcceptRole);
            }

            /* Create agree button: */
            m_pButtonDisagree = new QPushButton;
            if (m_pButtonDisagree)
            {
                /* Configure button: */
                connect(m_pButtonDisagree, &QPushButton::clicked, this, &QDialog::reject);

                /* Add into button-box: */
                pDialogButtonBox->addButton(m_pButtonDisagree, QDialogButtonBox::RejectRole);
            }
        }

        /* Add into layout: */
        pMainLayout->addWidget(pDialogButtonBox);
    }

    /* Configure self: */
    resize(600, 450);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &VBoxLicenseViewer::sltRetranslateUI);
}

int VBoxLicenseViewer::showLicenseFromString(const QString &strLicenseText)
{
    /* Set license text: */
    m_pLicenseBrowser->setText(strLicenseText);
    return exec();
}

int VBoxLicenseViewer::showLicenseFromFile(const QString &strLicenseFileName)
{
    /* Read license file: */
    QFile file(strLicenseFileName);
    if (file.open(QIODevice::ReadOnly))
        return showLicenseFromString(file.readAll());
    else
    {
        UINotificationMessage::cannotOpenLicenseFile(strLicenseFileName);
        return QDialog::Rejected;
    }
}

bool VBoxLicenseViewer::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Handle known event types: */
    switch (pEvent->type())
    {
        case QEvent::Hide:
        {
            if (pObject == m_pLicenseBrowser->verticalScrollBar())
                /* Doesn't work on wm's like ion3 where the window starts maximized: isActiveWindow() */
                sltUnlockButtons();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QDialog::eventFilter(pObject, pEvent);
}

void VBoxLicenseViewer::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QDialog::showEvent(pEvent);

    /* Enable/disable buttons accordingly: */
    bool fScrollBarHidden =    !m_pLicenseBrowser->verticalScrollBar()->isVisible()
                            && !(windowState() & Qt::WindowMinimized);
    m_pButtonAgree->setEnabled(fScrollBarHidden);
    m_pButtonDisagree->setEnabled(fScrollBarHidden);
}

void VBoxLicenseViewer::sltRetranslateUI()
{
    /* Translate dialog title: */
    setWindowTitle(tr("VirtualBox License"));

    /* Translate buttons: */
    m_pButtonAgree->setText(tr("I &Agree"));
    m_pButtonDisagree->setText(tr("I &Disagree"));
}

int VBoxLicenseViewer::exec()
{
    /* Nothing wrong with that, just hiding slot: */
    return QDialog::exec();
}

void VBoxLicenseViewer::sltHandleScrollBarMoved(int iValue)
{
    if (iValue == m_pLicenseBrowser->verticalScrollBar()->maximum())
        sltUnlockButtons();
}

void VBoxLicenseViewer::sltUnlockButtons()
{
    m_pButtonAgree->setEnabled(true);
    m_pButtonDisagree->setEnabled(true);
}
