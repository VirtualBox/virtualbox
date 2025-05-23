/* $Id: UIDisplayScreenFeaturesEditor.cpp 168969 2025-05-23 11:22:24Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDisplayScreenFeaturesEditor class implementation.
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
#include "UIDisplayScreenFeaturesEditor.h"


UIDisplayScreenFeaturesEditor::UIDisplayScreenFeaturesEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fEnable3DAcceleration(false)
    , m_pLabel(0)
    , m_pCheckBoxEnable3DAcceleration(0)
{
    prepare();
}

void UIDisplayScreenFeaturesEditor::setEnable3DAcceleration(bool fOn)
{
    if (m_pCheckBoxEnable3DAcceleration)
    {
        /* Update cached value and
         * check-box if value has changed: */
        if (m_fEnable3DAcceleration != fOn)
        {
            m_fEnable3DAcceleration = fOn;
            m_pCheckBoxEnable3DAcceleration->setCheckState(m_fEnable3DAcceleration ? Qt::Checked : Qt::Unchecked);
        }
    }
}

bool UIDisplayScreenFeaturesEditor::isEnabled3DAcceleration() const
{
    return   m_pCheckBoxEnable3DAcceleration
           ? m_pCheckBoxEnable3DAcceleration->checkState() == Qt::Checked
           : m_fEnable3DAcceleration;
}

int UIDisplayScreenFeaturesEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIDisplayScreenFeaturesEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIDisplayScreenFeaturesEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Use These Features"));
    if (m_pCheckBoxEnable3DAcceleration)
    {
        m_pCheckBoxEnable3DAcceleration->setText(tr("&3D Acceleration"));
        m_pCheckBoxEnable3DAcceleration->setToolTip(tr("The virtual machine will be given access "
                                                       "to the 3D graphics capabilities available on the host"));
    }
}

void UIDisplayScreenFeaturesEditor::prepare()
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
        /* Prepare 'enable 3D acceleration' check-box: */
        m_pCheckBoxEnable3DAcceleration = new QCheckBox(this);
        if (m_pCheckBoxEnable3DAcceleration)
            m_pLayout->addWidget(m_pCheckBoxEnable3DAcceleration, 0, 1);
    }

    /* Prepare connections: */
    connect(m_pCheckBoxEnable3DAcceleration, &QCheckBox::stateChanged,
            this, &UIDisplayScreenFeaturesEditor::sig3DAccelerationFeatureStatusChange);

    /* Apply language settings: */
    sltRetranslateUI();
}
