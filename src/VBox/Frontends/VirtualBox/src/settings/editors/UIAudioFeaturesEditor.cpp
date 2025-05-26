/* $Id: UIAudioFeaturesEditor.cpp 168997 2025-05-26 16:35:29Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAudioFeaturesEditor class implementation.
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
#include "UIAudioFeaturesEditor.h"


UIAudioFeaturesEditor::UIAudioFeaturesEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_fEnableOutput(false)
    , m_fEnableInput(false)
    , m_pLabel(0)
    , m_pCheckBoxEnableOutput(0)
    , m_pCheckBoxEnableInput(0)
{
    prepare();
}

void UIAudioFeaturesEditor::setEnableOutput(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableOutput != fOn)
    {
        m_fEnableOutput = fOn;
        if (m_pCheckBoxEnableOutput)
            m_pCheckBoxEnableOutput->setCheckState(m_fEnableOutput ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIAudioFeaturesEditor::outputEnabled() const
{
    return   m_pCheckBoxEnableOutput
           ? m_pCheckBoxEnableOutput->checkState() == Qt::Checked
           : m_fEnableOutput;
}

void UIAudioFeaturesEditor::setEnableInput(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableInput != fOn)
    {
        m_fEnableInput = fOn;
        if (m_pCheckBoxEnableInput)
            m_pCheckBoxEnableInput->setCheckState(m_fEnableInput ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIAudioFeaturesEditor::inputEnabled() const
{
    return   m_pCheckBoxEnableInput
           ? m_pCheckBoxEnableInput->checkState() == Qt::Checked
           : m_fEnableInput;
}

int UIAudioFeaturesEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIAudioFeaturesEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIAudioFeaturesEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Audio Features"));
    if (m_pCheckBoxEnableOutput)
    {
        m_pCheckBoxEnableOutput->setText(tr("Audio &Output"));
        m_pCheckBoxEnableOutput->setToolTip(tr("Output to the virtual audio device will reach the host. "
                                               "Otherwise the guest is muted."));
    }
    if (m_pCheckBoxEnableInput)
    {
        m_pCheckBoxEnableInput->setText(tr("Audio &Input"));
        m_pCheckBoxEnableInput->setToolTip(tr("The guest will be able to capture audio input from the host. "
                                              "Otherwise the guest will capture only silence."));
    }
}

void UIAudioFeaturesEditor::prepare()
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
        /* Prepare 'enable output' check-box: */
        m_pCheckBoxEnableOutput = new QCheckBox(this);
        if (m_pCheckBoxEnableOutput)
            m_pLayout->addWidget(m_pCheckBoxEnableOutput, 0, 1);
        /* Prepare 'enable input' check-box: */
        m_pCheckBoxEnableInput = new QCheckBox(this);
        if (m_pCheckBoxEnableInput)
            m_pLayout->addWidget(m_pCheckBoxEnableInput, 1, 1);
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
