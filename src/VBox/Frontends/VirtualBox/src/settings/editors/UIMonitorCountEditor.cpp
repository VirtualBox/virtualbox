/* $Id: UIMonitorCountEditor.cpp 168969 2025-05-23 11:22:24Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMonitorCountEditor class implementation.
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
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>

/* GUI includes: */
#include "QIAdvancedSlider.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIGlobalSession.h"
#include "UIMonitorCountEditor.h"

/* COM includes: */
#include "CSystemProperties.h"


UIMonitorCountEditor::UIMonitorCountEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_iValue(1)
    , m_pLayout(0)
    , m_pLabel(0)
    , m_pSlider(0)
    , m_pSpinBox(0)
    , m_pLabelMin(0)
    , m_pLabelMax(0)
{
    prepare();
}

void UIMonitorCountEditor::setValue(int iValue)
{
    if (m_iValue != iValue)
    {
        m_iValue = iValue;
        if (m_pSlider)
            m_pSlider->setValue(m_iValue);
        if (m_pSpinBox)
            m_pSpinBox->setValue(m_iValue);
    }
}

int UIMonitorCountEditor::value() const
{
    return m_pSpinBox ? m_pSpinBox->value() : m_iValue;
}

int UIMonitorCountEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIMonitorCountEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIMonitorCountEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Number of Virtual Mo&nitors"));

    const QString strToolTip = tr("The number of virtual monitors the virtual machine has");
    if (m_pSlider)
        m_pSlider->setToolTip(strToolTip);
    if (m_pSpinBox)
        m_pSpinBox->setToolTip(strToolTip);

    if (m_pLabelMin)
        m_pLabelMin->setToolTip(tr("Minimum virtual monitor number"));
    if (m_pLabelMax)
        m_pLabelMax->setToolTip(tr("Maximum virtual monitor number"));
}

void UIMonitorCountEditor::sltHandleSliderChange()
{
    /* Apply spin-box value keeping signals disabled: */
    if (m_pSpinBox && m_pSlider)
    {
        m_pSpinBox->blockSignals(true);
        m_pSpinBox->setValue(m_pSlider->value());
        m_pSpinBox->blockSignals(false);
    }

    /* Notify listeners about value changed: */
    emit sigValidChanged();
}

void UIMonitorCountEditor::sltHandleSpinBoxChange()
{
    /* Apply slider value keeping signals disabled: */
    if (m_pSlider && m_pSpinBox)
    {
        m_pSlider->blockSignals(true);
        m_pSlider->setValue(m_pSpinBox->value());
        m_pSlider->blockSignals(false);
    }

    /* Notify listeners about value changed: */
    emit sigValidChanged();
}

void UIMonitorCountEditor::prepare()
{
    /* Prepare common variables: */
    const CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();

    /* Prepare main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(2, 1); // spacer between min&max labels

        /* Prepare main label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }

        /* Prepare slider: */
        m_pSlider = new QIAdvancedSlider(this);
        if (m_pSlider)
        {
            const uint cHostScreens = UIDesktopWidgetWatchdog::screenCount();
            const uint cMinGuestScreens = 1;
            const uint cMaxGuestScreens = comProperties.GetMaxGuestMonitors();
            const uint cMaxGuestScreensForSlider = qMin(cMaxGuestScreens, (uint)8);
            m_pSlider->setOrientation(Qt::Horizontal);
            m_pSlider->setMinimum(cMinGuestScreens);
            m_pSlider->setMaximum(cMaxGuestScreensForSlider);
            m_pSlider->setPageStep(1);
            m_pSlider->setSingleStep(1);
            m_pSlider->setTickInterval(1);
            m_pSlider->setOptimalHint(cMinGuestScreens, cHostScreens);
            m_pSlider->setWarningHint(cHostScreens, cMaxGuestScreensForSlider);

            m_pLayout->addWidget(m_pSlider, 0, 1, 1, 3);
        }

        /* Prepare spin-box: */
        m_pSpinBox = new QSpinBox(this);
        if (m_pSpinBox)
        {
            if (m_pLabel)
                m_pLabel->setBuddy(m_pSpinBox);
            m_pSpinBox->setMinimum(1);
            m_pSpinBox->setMaximum(comProperties.GetMaxGuestMonitors());

            m_pLayout->addWidget(m_pSpinBox, 0, 4);
        }

        /* Prepare min label: */
        m_pLabelMin = new QLabel(this);
        if (m_pLabelMin)
        {
            m_pLabelMin->setText(QString::number(1));
            m_pLayout->addWidget(m_pLabelMin, 1, 1);
        }

        /* Prepare max label: */
        m_pLabelMax = new QLabel(this);
        if (m_pLabelMax)
        {
            m_pLabelMax->setText(QString::number(qMin(comProperties.GetMaxGuestMonitors(), (ULONG)8)));
            m_pLayout->addWidget(m_pLabelMax, 1, 3);
        }
    }

    /* Prepare connections: */
    if (m_pSlider)
        connect(m_pSlider, &QIAdvancedSlider::valueChanged,
                this, &UIMonitorCountEditor::sltHandleSliderChange);
    if (m_pSpinBox)
        connect(m_pSpinBox, &QSpinBox::valueChanged,
                this, &UIMonitorCountEditor::sltHandleSpinBoxChange);

    /* Apply language settings: */
    sltRetranslateUI();
}
