/* $Id: UIMaximumGuestScreenSizeEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMaximumGuestScreenSizeEditor class implementation.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>

/* GUI includes: */
#include "UIConverter.h"
#include "UIMaximumGuestScreenSizeEditor.h"


/*********************************************************************************************************************************
*   Class UIMaximumGuestScreenSizeValue implementation.                                                                          *
*********************************************************************************************************************************/

UIMaximumGuestScreenSizeValue::UIMaximumGuestScreenSizeValue(MaximumGuestScreenSizePolicy enmPolicy /* = MaximumGuestScreenSizePolicy_Any */,
                                                             const QSize &size /* = QSize() */)
    : m_enmPolicy(enmPolicy)
    , m_size(size)
{
}

bool UIMaximumGuestScreenSizeValue::equal(const UIMaximumGuestScreenSizeValue &other) const
{
    return true
           && (   (   m_enmPolicy != MaximumGuestScreenSizePolicy_Fixed
                   && m_enmPolicy == other.m_enmPolicy)
               || (   m_enmPolicy == MaximumGuestScreenSizePolicy_Fixed
                   && m_enmPolicy == other.m_enmPolicy
                   && m_size == other.m_size))
           ;
}


/*********************************************************************************************************************************
*   Class UIMaximumGuestScreenSizeEditor implementation.                                                                         *
*********************************************************************************************************************************/

UIMaximumGuestScreenSizeEditor::UIMaximumGuestScreenSizeEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_pLayout(0)
    , m_pLabelPolicy(0)
    , m_pComboPolicy(0)
    , m_pLabelMaxWidth(0)
    , m_pSpinboxMaxWidth(0)
    , m_pLabelMaxHeight(0)
    , m_pSpinboxMaxHeight(0)
{
    prepare();
}

void UIMaximumGuestScreenSizeEditor::setValue(const UIMaximumGuestScreenSizeValue &guiValue)
{
    /* Update cached value if value has changed: */
    if (m_guiValue != guiValue)
        m_guiValue = guiValue;

    /* Look for proper policy index to choose: */
    if (m_pComboPolicy)
    {
        const int iIndex = m_pComboPolicy->findData(QVariant::fromValue(m_guiValue.m_enmPolicy));
        if (iIndex != -1)
        {
            m_pComboPolicy->setCurrentIndex(iIndex);
            sltHandleCurrentPolicyIndexChanged();
        }
    }
    /* Load size as well: */
    if (m_pSpinboxMaxWidth && m_pSpinboxMaxHeight)
    {
        if (m_guiValue.m_enmPolicy == MaximumGuestScreenSizePolicy_Fixed)
        {
            m_pSpinboxMaxWidth->setValue(m_guiValue.m_size.width());
            m_pSpinboxMaxHeight->setValue(m_guiValue.m_size.height());
        }
    }
}

UIMaximumGuestScreenSizeValue UIMaximumGuestScreenSizeEditor::value() const
{
    return   m_pComboPolicy && m_pSpinboxMaxWidth && m_pSpinboxMaxHeight
           ? UIMaximumGuestScreenSizeValue(m_pComboPolicy->currentData().value<MaximumGuestScreenSizePolicy>(),
                                           QSize(m_pSpinboxMaxWidth->value(), m_pSpinboxMaxHeight->value()))
           : m_guiValue;
}

int UIMaximumGuestScreenSizeEditor::minimumLabelHorizontalHint() const
{
    int iMinimumHint = 0;
    iMinimumHint = qMax(iMinimumHint, m_pLabelPolicy->minimumSizeHint().width());
    iMinimumHint = qMax(iMinimumHint, m_pLabelMaxWidth->minimumSizeHint().width());
    iMinimumHint = qMax(iMinimumHint, m_pLabelMaxHeight->minimumSizeHint().width());
    return iMinimumHint;
}

void UIMaximumGuestScreenSizeEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIMaximumGuestScreenSizeEditor::sltRetranslateUI()
{
    if (m_pLabelPolicy)
        m_pLabelPolicy->setText(tr("Maximum Guest Screen &Size:"));
    if (m_pLabelMaxWidth)
        m_pLabelMaxWidth->setText(tr("&Width:"));
    if (m_pSpinboxMaxWidth)
        m_pSpinboxMaxWidth->setToolTip(tr("Holds the maximum width which we would like the guest to use."));
    if (m_pLabelMaxHeight)
        m_pLabelMaxHeight->setText(tr("&Height:"));
    if (m_pSpinboxMaxHeight)
        m_pSpinboxMaxHeight->setToolTip(tr("Holds the maximum height which we would like the guest to use."));

    if (m_pComboPolicy)
    {
        for (int i = 0; i < m_pComboPolicy->count(); ++i)
        {
            const MaximumGuestScreenSizePolicy enmType = m_pComboPolicy->itemData(i).value<MaximumGuestScreenSizePolicy>();
            m_pComboPolicy->setItemText(i, gpConverter->toString(enmType));
        }
        m_pComboPolicy->setToolTip(tr("Selects maximum guest screen size policy."));
    }
}

void UIMaximumGuestScreenSizeEditor::sltHandleCurrentPolicyIndexChanged()
{
    if (m_pComboPolicy)
    {
        /* Get current size-combo item data: */
        const MaximumGuestScreenSizePolicy enmPolicy = m_pComboPolicy->currentData().value<MaximumGuestScreenSizePolicy>();
        /* Should be combo-level widgets enabled? */
        const bool fComboLevelWidgetsEnabled = enmPolicy == MaximumGuestScreenSizePolicy_Fixed;
        /* Enable/disable combo-level widgets: */
        if (m_pLabelMaxWidth)
            m_pLabelMaxWidth->setEnabled(fComboLevelWidgetsEnabled);
        if (m_pSpinboxMaxWidth)
            m_pSpinboxMaxWidth->setEnabled(fComboLevelWidgetsEnabled);
        if (m_pLabelMaxHeight)
            m_pLabelMaxHeight->setEnabled(fComboLevelWidgetsEnabled);
        if (m_pSpinboxMaxHeight)
            m_pSpinboxMaxHeight->setEnabled(fComboLevelWidgetsEnabled);
    }
}

void UIMaximumGuestScreenSizeEditor::prepare()
{
    /* Create main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(1, 1);

        const int iMinWidth = 640;
        const int iMinHeight = 480;
        const int iMaxSize = 16 * _1K;

        /* Prepare policy label: */
        m_pLabelPolicy = new QLabel(this);
        if (m_pLabelPolicy)
        {
           m_pLabelPolicy->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
           m_pLayout->addWidget(m_pLabelPolicy, 0, 0);
        }
        /* Prepare policy combo: */
        m_pComboPolicy = new QComboBox(this);
        if (m_pComboPolicy)
        {
            if (m_pLabelPolicy)
                m_pLabelPolicy->setBuddy(m_pComboPolicy);
            connect(m_pComboPolicy, &QComboBox::activated,
                    this, &UIMaximumGuestScreenSizeEditor::sltHandleCurrentPolicyIndexChanged);

            m_pLayout->addWidget(m_pComboPolicy, 0, 1);
        }

        /* Prepare max width label: */
        m_pLabelMaxWidth = new QLabel(this);
        if (m_pLabelMaxWidth)
        {
            m_pLabelMaxWidth->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabelMaxWidth, 1, 0);
        }
        /* Prepare max width spinbox: */
        m_pSpinboxMaxWidth = new QSpinBox(this);
        if (m_pSpinboxMaxWidth)
        {
            if (m_pLabelMaxWidth)
                m_pLabelMaxWidth->setBuddy(m_pSpinboxMaxWidth);
            m_pSpinboxMaxWidth->setMinimum(iMinWidth);
            m_pSpinboxMaxWidth->setMaximum(iMaxSize);

            m_pLayout->addWidget(m_pSpinboxMaxWidth, 1, 1);
        }

        /* Prepare max height label: */
        m_pLabelMaxHeight = new QLabel(this);
        if (m_pLabelMaxHeight)
        {
            m_pLabelMaxHeight->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabelMaxHeight, 2, 0);
        }
        /* Prepare max width spinbox: */
        m_pSpinboxMaxHeight = new QSpinBox(this);
        if (m_pSpinboxMaxHeight)
        {
            if (m_pLabelMaxHeight)
                m_pLabelMaxHeight->setBuddy(m_pSpinboxMaxHeight);
            m_pSpinboxMaxHeight->setMinimum(iMinHeight);
            m_pSpinboxMaxHeight->setMaximum(iMaxSize);

            m_pLayout->addWidget(m_pSpinboxMaxHeight, 2, 1);
        }
    }

    /* Populate combo: */
    populateCombo();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMaximumGuestScreenSizeEditor::populateCombo()
{
    if (m_pComboPolicy)
    {
        /* Clear combo first of all: */
        m_pComboPolicy->clear();

        /* Init currently supported maximum guest size policy types: */
        const QList<MaximumGuestScreenSizePolicy> supportedValues  = QList<MaximumGuestScreenSizePolicy>()
                                                                  << MaximumGuestScreenSizePolicy_Automatic
                                                                  << MaximumGuestScreenSizePolicy_Any
                                                                  << MaximumGuestScreenSizePolicy_Fixed;

        /* Update combo with all the supported values: */
        foreach (const MaximumGuestScreenSizePolicy &enmType, supportedValues)
            m_pComboPolicy->addItem(QString(), QVariant::fromValue(enmType));

        /* Retranslate finally: */
        sltRetranslateUI();
    }
}
