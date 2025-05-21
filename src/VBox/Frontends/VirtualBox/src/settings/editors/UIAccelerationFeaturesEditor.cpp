/* $Id: UIAccelerationFeaturesEditor.cpp 168951 2025-05-21 10:21:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAccelerationFeaturesEditor class implementation.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "UIAccelerationFeaturesEditor.h"


UIAccelerationFeaturesEditor::UIAccelerationFeaturesEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fEnableNestedPaging(false)
    , m_pLabel(0)
    , m_pCheckBoxEnableNestedPaging(0)
{
    prepare();
}

void UIAccelerationFeaturesEditor::setEnableNestedPaging(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableNestedPaging != fOn)
    {
        m_fEnableNestedPaging = fOn;
        if (m_pCheckBoxEnableNestedPaging)
            m_pCheckBoxEnableNestedPaging->setCheckState(m_fEnableNestedPaging ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIAccelerationFeaturesEditor::isEnabledNestedPaging() const
{
    return   m_pCheckBoxEnableNestedPaging
           ? m_pCheckBoxEnableNestedPaging->checkState() == Qt::Checked
           : m_fEnableNestedPaging;
}

void UIAccelerationFeaturesEditor::setEnableNestedPagingAvailable(bool fAvailable)
{
    m_pCheckBoxEnableNestedPaging->setEnabled(fAvailable);
}

int UIAccelerationFeaturesEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIAccelerationFeaturesEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIAccelerationFeaturesEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Hardware Virtualization"));
    if (m_pCheckBoxEnableNestedPaging)
    {
        m_pCheckBoxEnableNestedPaging->setText(tr("Nested Pa&ging"));
        m_pCheckBoxEnableNestedPaging->setToolTip(tr("Use the nested paging extension of Intel VT-x "
                                                     "and AMD-V on the virtual machine"));
    }
}

void UIAccelerationFeaturesEditor::prepare()
{
    /* Prepare main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(1, 1);

        /* Prepare virtualization label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }

        /* Prepare 'enable nested virtualization' check-box: */
        m_pCheckBoxEnableNestedPaging = new QCheckBox(this);
        if (m_pCheckBoxEnableNestedPaging)
        {
            connect(m_pCheckBoxEnableNestedPaging, &QCheckBox::stateChanged,
                    this, &UIAccelerationFeaturesEditor::sigChangedNestedPaging);
            m_pLayout->addWidget(m_pCheckBoxEnableNestedPaging, 0, 1);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
