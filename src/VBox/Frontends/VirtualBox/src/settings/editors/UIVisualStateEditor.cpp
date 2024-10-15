/* $Id: UIVisualStateEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVisualStateEditor class implementation.
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

/* GUI includes: */
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIVisualStateEditor.h"


UIVisualStateEditor::UIVisualStateEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_enmValue(UIVisualStateType_Invalid)
    , m_pLayout(0)
    , m_pLabel(0)
    , m_pCombo(0)
{
    prepare();
}

void UIVisualStateEditor::setMachineId(const QUuid &uMachineId)
{
    /* Update cached value and
     * combo if value has changed: */
    if (m_uMachineId != uMachineId)
    {
        m_uMachineId = uMachineId;
        populateCombo();
    }
}

void UIVisualStateEditor::setValue(UIVisualStateType enmValue)
{
    /* Update cached value and
     * combo if value has changed: */
    if (m_enmValue != enmValue)
    {
        m_enmValue = enmValue;
        populateCombo();
    }
}

UIVisualStateType UIVisualStateEditor::value() const
{
    return m_pCombo ? m_pCombo->currentData().value<UIVisualStateType>() : m_enmValue;
}

int UIVisualStateEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIVisualStateEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIVisualStateEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Visual &State:"));
    if (m_pCombo)
    {
        for (int i = 0; i < m_pCombo->count(); ++i)
        {
            const UIVisualStateType enmType = m_pCombo->itemData(i).value<UIVisualStateType>();
            m_pCombo->setItemText(i, gpConverter->toString(enmType));
        }
        m_pCombo->setToolTip(tr("Selects the visual state. If machine is running it will be applied "
                                "as soon as possible, otherwise desired one will be defined."));
    }
}

void UIVisualStateEditor::prepare()
{
    /* Create main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }

        /* Create combo layout: */
        QHBoxLayout *pComboLayout = new QHBoxLayout;
        if (pComboLayout)
        {
            /* Create combo: */
            m_pCombo = new QComboBox(this);
            if (m_pCombo)
            {
                /* This is necessary since contents is dynamical now: */
                m_pCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
                if (m_pLabel)
                    m_pLabel->setBuddy(m_pCombo);
                pComboLayout->addWidget(m_pCombo);
            }

            /* Add stretch: */
            pComboLayout->addStretch();

            /* Add combo-layout into main-layout: */
            m_pLayout->addLayout(pComboLayout, 0, 1);
        }
    }

    /* Populate combo: */
    populateCombo();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIVisualStateEditor::populateCombo()
{
    if (m_pCombo)
    {
        /* Clear combo first of all: */
        m_supportedValues.clear();
        m_pCombo->clear();

        /* Get possible values: */
        QList<UIVisualStateType> possibleValues;
        possibleValues << UIVisualStateType_Normal
                       << UIVisualStateType_Fullscreen
                       << UIVisualStateType_Seamless
                       << UIVisualStateType_Scale;

        /* Load currently supported visual state types: */
        const UIVisualStateType enmRestrictedTypes = m_uMachineId.isNull()
                                                   ? UIVisualStateType_Invalid
                                                   : gEDataManager->restrictedVisualStates(m_uMachineId);
        foreach (const UIVisualStateType &enmPossibleValue, possibleValues)
            if (!(enmPossibleValue & enmRestrictedTypes))
                m_supportedValues << enmPossibleValue;

        /* Make sure requested value if sane is present as well: */
        if (   possibleValues.contains(m_enmValue)
            && !m_supportedValues.contains(m_enmValue))
            m_supportedValues.prepend(m_enmValue);

        /* Update combo with all the supported values: */
        foreach (const UIVisualStateType &enmType, m_supportedValues)
            m_pCombo->addItem(QString(), QVariant::fromValue(enmType));

        /* Look for proper index to choose: */
        const int iIndex = m_pCombo->findData(QVariant::fromValue(m_enmValue));
        if (iIndex != -1)
            m_pCombo->setCurrentIndex(iIndex);

        /* Retranslate finally: */
        sltRetranslateUI();
    }
}
