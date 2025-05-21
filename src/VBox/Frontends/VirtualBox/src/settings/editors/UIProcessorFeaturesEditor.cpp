/* $Id: UIProcessorFeaturesEditor.cpp 168951 2025-05-21 10:21:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProcessorFeaturesEditor class implementation.
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
#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>

/* GUI includes: */
#include "UIProcessorFeaturesEditor.h"


UIProcessorFeaturesEditor::UIProcessorFeaturesEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fEnablePae(false)
    , m_fEnableNestedVirtualization(false)
    , m_pLabel(0)
    , m_pCheckBoxEnablePae(0)
    , m_pCheckBoxEnableNestedVirtualization(0)
{
    prepare();
}

void UIProcessorFeaturesEditor::setEnablePae(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnablePae != fOn)
    {
        m_fEnablePae = fOn;
        if (m_pCheckBoxEnablePae)
            m_pCheckBoxEnablePae->setCheckState(m_fEnablePae ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIProcessorFeaturesEditor::isEnabledPae() const
{
    return   m_pCheckBoxEnablePae
           ? m_pCheckBoxEnablePae->checkState() == Qt::Checked
           : m_fEnablePae;
}

void UIProcessorFeaturesEditor::setEnablePaeAvailable(bool fAvailable)
{
    m_pCheckBoxEnablePae->setEnabled(fAvailable);
}

void UIProcessorFeaturesEditor::setEnableNestedVirtualization(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableNestedVirtualization != fOn)
    {
        m_fEnableNestedVirtualization = fOn;
        if (m_pCheckBoxEnableNestedVirtualization)
            m_pCheckBoxEnableNestedVirtualization->setCheckState(m_fEnableNestedVirtualization ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIProcessorFeaturesEditor::isEnabledNestedVirtualization() const
{
    return   m_pCheckBoxEnableNestedVirtualization
           ? m_pCheckBoxEnableNestedVirtualization->checkState() == Qt::Checked
           : m_fEnableNestedVirtualization;
}

void UIProcessorFeaturesEditor::setEnableNestedVirtualizationAvailable(bool fAvailable)
{
    m_pCheckBoxEnableNestedVirtualization->setEnabled(fAvailable);
}

int UIProcessorFeaturesEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIProcessorFeaturesEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIProcessorFeaturesEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("CPU Features"));
    if (m_pCheckBoxEnablePae)
    {
        m_pCheckBoxEnablePae->setText(tr("PA&E/NX"));
        m_pCheckBoxEnablePae->setToolTip(tr("Use the Physical Address Extension (PAE) feature "
                                            "of the host CPU on the virtual machine"));
    }
    if (m_pCheckBoxEnableNestedVirtualization)
    {
        m_pCheckBoxEnableNestedVirtualization->setText(tr("Nested &VT-x/AMD-V"));
        m_pCheckBoxEnableNestedVirtualization->setToolTip(tr("Use the nested hardware virtualization "
                                                             "CPU feature on the virtual machine"));
    }
}

void UIProcessorFeaturesEditor::prepare()
{
    /* Prepare main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(1, 1);

        /* Prepare label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }
        /* Prepare 'enable PAE' check-box: */
        m_pCheckBoxEnablePae = new QCheckBox(this);
        if (m_pCheckBoxEnablePae)
        {
            connect(m_pCheckBoxEnablePae, &QCheckBox::stateChanged,
                    this, &UIProcessorFeaturesEditor::sigChangedPae);
            m_pLayout->addWidget(m_pCheckBoxEnablePae, 0, 1);
        }
        /* Prepare 'enable nested virtualization' check-box: */
        m_pCheckBoxEnableNestedVirtualization = new QCheckBox(this);
        if (m_pCheckBoxEnableNestedVirtualization)
        {
            connect(m_pCheckBoxEnableNestedVirtualization, &QCheckBox::stateChanged,
                    this, &UIProcessorFeaturesEditor::sigChangedNestedVirtualization);
            m_pLayout->addWidget(m_pCheckBoxEnableNestedVirtualization, 1, 1);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
