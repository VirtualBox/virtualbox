/* $Id: UIPopupPaneButtonPane.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupPaneButtonPane class implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <QKeyEvent>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIMessageBox.h"
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UIPopupPaneButtonPane.h"


UIPopupPaneButtonPane::UIPopupPaneButtonPane(QWidget *pParent /* = 0*/)
    : QWidget(pParent)
    , m_iDefaultButton(0)
    , m_iEscapeButton(0)
{
    /* Prepare: */
    prepare();
}

void UIPopupPaneButtonPane::setButtons(const QMap<int, QString> &buttonDescriptions)
{
    /* Make sure something changed: */
    if (m_buttonDescriptions == buttonDescriptions)
        return;

    /* Assign new button-descriptions: */
    m_buttonDescriptions = buttonDescriptions;

    /* Recreate buttons: */
    cleanupButtons();
    prepareButtons();
}

void UIPopupPaneButtonPane::sltButtonClicked()
{
    /* Make sure the slot is called by the button: */
    QIToolButton *pButton = qobject_cast<QIToolButton*>(sender());
    if (!pButton)
        return;

    /* Make sure we still have that button: */
    int iButtonID = m_buttons.key(pButton, 0);
    if (!iButtonID)
        return;

    /* Notify listeners button was clicked: */
    emit sigButtonClicked(iButtonID);
}

void UIPopupPaneButtonPane::prepare()
{
    /* Prepare layouts: */
    prepareLayouts();
}

void UIPopupPaneButtonPane::prepareLayouts()
{
    /* Create main-layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Configure layout: */
        pMainLayout->setSpacing(0);
        pMainLayout->setContentsMargins(0, 0, 0, 0);

        /* Create button-layout: */
        m_pButtonLayout = new QHBoxLayout;
        if (m_pButtonLayout)
        {
            /* Configure layout: */
            m_pButtonLayout->setSpacing(0);
            m_pButtonLayout->setContentsMargins(0, 0, 0, 0);

            /* Add into layout: */
            pMainLayout->addLayout(m_pButtonLayout);
        }

        /* Add stretch: */
        pMainLayout->addStretch();
    }
}

void UIPopupPaneButtonPane::prepareButtons()
{
    /* Add all the buttons: */
    const QList<int> &buttonsIDs = m_buttonDescriptions.keys();
    foreach (int iButtonID, buttonsIDs)
        if (QIToolButton *pButton = addButton(iButtonID, m_buttonDescriptions[iButtonID]))
        {
            m_pButtonLayout->addWidget(pButton);
            m_buttons[iButtonID] = pButton;
            connect(pButton, &QIToolButton::clicked, this, &UIPopupPaneButtonPane::sltButtonClicked);
            if (pButton->property("default").toBool())
                m_iDefaultButton = iButtonID;
            if (pButton->property("escape").toBool())
                m_iEscapeButton = iButtonID;
        }
}

void UIPopupPaneButtonPane::cleanupButtons()
{
    /* Remove all the buttons: */
    const QList<int> &buttonsIDs = m_buttons.keys();
    foreach (int iButtonID, buttonsIDs)
    {
        delete m_buttons[iButtonID];
        m_buttons.remove(iButtonID);
    }
}

void UIPopupPaneButtonPane::keyPressEvent(QKeyEvent *pEvent)
{
    /* Depending on pressed key: */
    switch (pEvent->key())
    {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        {
            if (m_iDefaultButton)
            {
                pEvent->accept();
                emit sigButtonClicked(m_iDefaultButton);
                return;
            }
            break;
        }
        case Qt::Key_Escape:
        {
            if (m_iEscapeButton)
            {
                pEvent->accept();
                emit sigButtonClicked(m_iEscapeButton);
                return;
            }
            break;
        }
        default:
            break;
    }
    /* Call to base-class: */
    QWidget::keyPressEvent(pEvent);
}

/* static */
QIToolButton *UIPopupPaneButtonPane::addButton(int iButtonID, const QString &strToolTip)
{
    /* Create button: */
    QIToolButton *pButton = new QIToolButton;
    if (pButton)
    {
        /* Configure button: */
        pButton->removeBorder();
        pButton->setToolTip(strToolTip.isEmpty() ? defaultToolTip(iButtonID) : strToolTip);
        pButton->setIcon(defaultIcon(iButtonID));

        /* Sign the 'default' button: */
        if (iButtonID & AlertButtonOption_Default)
            pButton->setProperty("default", true);
        /* Sign the 'escape' button: */
        if (iButtonID & AlertButtonOption_Escape)
            pButton->setProperty("escape", true);
    }

    /* Return button: */
    return pButton;
}

/* static */
QString UIPopupPaneButtonPane::defaultToolTip(int iButtonID)
{
    QString strToolTip;
    switch (iButtonID & AlertButtonMask)
    {
        case AlertButton_Ok: strToolTip = QIMessageBox::tr("OK"); break;
        case AlertButton_Cancel:
        {
            switch (iButtonID & AlertOptionMask)
            {
                case AlertOption_AutoConfirmed: strToolTip = QApplication::translate("UIMessageCenter", "Do not show this message again"); break;
                default: strToolTip = QIMessageBox::tr("Cancel"); break;
            }
            break;
        }
        case AlertButton_Choice1: strToolTip = QIMessageBox::tr("Yes"); break;
        case AlertButton_Choice2: strToolTip = QIMessageBox::tr("No"); break;
        default: strToolTip = QString(); break;
    }
    return strToolTip;
}

/* static */
QIcon UIPopupPaneButtonPane::defaultIcon(int iButtonID)
{
    QIcon icon;
    switch (iButtonID & AlertButtonMask)
    {
        case AlertButton_Ok: icon = UIIconPool::iconSet(":/ok_16px.png"); break;
        case AlertButton_Cancel:
        {
            switch (iButtonID & AlertOptionMask)
            {
                case AlertOption_AutoConfirmed: icon = UIIconPool::iconSet(":/close_popup_16px.png"); break;
                default: icon = UIIconPool::iconSet(":/cancel_16px.png"); break;
            }
            break;
        }
        case AlertButton_Choice1: break;
        case AlertButton_Choice2: break;
        default: break;
    }
    return icon;
}
