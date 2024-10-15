/* $Id: QIDialogButtonBox.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIDialogButtonBox class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <QBoxLayout>
#include <QEvent>
#include <QPushButton>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UICommon.h"
#include "UIHelpBrowserDialog.h"
#include "UISpecialControls.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/assert.h>


QIDialogButtonBox::QIDialogButtonBox(QWidget *pParent /* = 0 */)
    : QDialogButtonBox(pParent)
    , m_fDoNotPickDefaultButton(false)
{
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &QIDialogButtonBox::sltRetranslateUI);
}

QIDialogButtonBox::QIDialogButtonBox(Qt::Orientation enmOrientation, QWidget *pParent /* = 0 */)
    : QDialogButtonBox(pParent)
    , m_fDoNotPickDefaultButton(false)
{
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &QIDialogButtonBox::sltRetranslateUI);
    setOrientation(enmOrientation);
}

QIDialogButtonBox::QIDialogButtonBox(StandardButtons enmButtonTypes, Qt::Orientation enmOrientation, QWidget *pParent)
    : QDialogButtonBox(pParent)
    , m_fDoNotPickDefaultButton(false)
{
    setOrientation(enmOrientation);
    setStandardButtons(enmButtonTypes);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &QIDialogButtonBox::sltRetranslateUI);
}

QPushButton *QIDialogButtonBox::button(StandardButton enmButtonType) const
{
    QPushButton *pButton = QDialogButtonBox::button(enmButtonType);
    if (   !pButton
        && enmButtonType == QDialogButtonBox::Help)
        pButton = m_pHelpButton;
    return pButton;
}

QPushButton *QIDialogButtonBox::addButton(const QString &strText, ButtonRole enmRole)
{
    QPushButton *pButton = QDialogButtonBox::addButton(strText, enmRole);
    sltRetranslateUI();
    return pButton;
}

QPushButton *QIDialogButtonBox::addButton(StandardButton enmButtonType)
{
    QPushButton *pButton = QDialogButtonBox::addButton(enmButtonType);
    sltRetranslateUI();
    return pButton;
}

void QIDialogButtonBox::setStandardButtons(StandardButtons enmButtonTypes)
{
    QDialogButtonBox::setStandardButtons(enmButtonTypes);
    sltRetranslateUI();
}

void QIDialogButtonBox::addExtraWidget(QWidget *pInsertedWidget)
{
    QBoxLayout *pLayout = boxLayout();
    if (pLayout)
    {
        int iIndex = findEmptySpace(pLayout);
        pLayout->insertWidget(iIndex + 1, pInsertedWidget);
        pLayout->insertStretch(iIndex + 2);
    }
}

void QIDialogButtonBox::addExtraLayout(QLayout *pInsertedLayout)
{
    QBoxLayout *pLayout = boxLayout();
    if (pLayout)
    {
        int iIndex = findEmptySpace(pLayout);
        pLayout->insertLayout(iIndex + 1, pInsertedLayout);
        pLayout->insertStretch(iIndex + 2);
    }
}

void QIDialogButtonBox::setDoNotPickDefaultButton(bool fDoNotPickDefaultButton)
{
    m_fDoNotPickDefaultButton = fDoNotPickDefaultButton;
}

void QIDialogButtonBox::sltRetranslateUI()
{
    QPushButton *pButton = QDialogButtonBox::button(QDialogButtonBox::Help);
    if (pButton)
    {
        /* Use our very own help button if the user requested for one. */
        if (!m_pHelpButton)
            m_pHelpButton = new UIHelpButton;
        m_pHelpButton->initFrom(pButton);
        removeButton(pButton);
        QDialogButtonBox::addButton(m_pHelpButton, QDialogButtonBox::HelpRole);
    }
}

void QIDialogButtonBox::showEvent(QShowEvent *pEvent)
{
    // WORKAROUND:
    // QDialogButtonBox has embedded functionality we'd like to avoid.
    // It auto-picks default button if non is set, based on button role.
    // Qt documentation states that happens in showEvent, so here we are.
    // In rare case we'd like to have dialog with no default button at all.
    if (m_fDoNotPickDefaultButton)
    {
        /* Unset all default-buttons in the dialog: */
        foreach (QPushButton *pButton, findChildren<QPushButton*>())
            if (pButton->isDefault())
                pButton->setDefault(false);
    }

    /* Call to base-class: */
    return QDialogButtonBox::showEvent(pEvent);
}

QBoxLayout *QIDialogButtonBox::boxLayout() const
{
    QBoxLayout *pLayout = qobject_cast<QBoxLayout*>(layout());
    AssertMsg(RT_VALID_PTR(pLayout), ("Layout of the QDialogButtonBox isn't a box layout."));
    return pLayout;
}

int QIDialogButtonBox::findEmptySpace(QBoxLayout *pLayout) const
{
    /* Search for the first occurrence of QSpacerItem and return the index. */
    int i = 0;
    for (; i < pLayout->count(); ++i)
    {
        QLayoutItem *pItem = pLayout->itemAt(i);
        if (pItem && pItem->spacerItem())
            break;
    }
    return i;
}

void QIDialogButtonBox::sltHandleHelpRequest()
{
    AssertReturnVoid(sender());
    UIHelpBrowserDialog::findManualFileAndShow(uiCommon().helpKeyword(sender()));
}
