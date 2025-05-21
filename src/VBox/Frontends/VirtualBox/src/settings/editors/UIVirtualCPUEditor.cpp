/* $Id: UIVirtualCPUEditor.cpp 168951 2025-05-21 10:21:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualCPUEditor class implementation.
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
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIAdvancedSlider.h"
#include "UIGlobalSession.h"
#include "UIVirtualCPUEditor.h"

/* COM includes */
#include "CSystemProperties.h"

UIVirtualCPUEditor::UIVirtualCPUEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_uMinVCPUCount(1)
    , m_uMaxVCPUCount(1)
    , m_pLabelVCPU(0)
    , m_pSlider(0)
    , m_pSpinBox(0)
    , m_pLabelVCPUMin(0)
    , m_pLabelVCPUMax(0)
{
    prepare();
}

int UIVirtualCPUEditor::maxVCPUCount() const
{
    return (int)m_uMaxVCPUCount;
}

void UIVirtualCPUEditor::setValue(int iValue)
{
    if (m_pSlider)
        m_pSlider->setValue(iValue);
}

int UIVirtualCPUEditor::value() const
{
    return m_pSlider ? m_pSlider->value() : 0;
}

int UIVirtualCPUEditor::minimumLabelHorizontalHint() const
{
    return m_pLabelVCPU->minimumSizeHint().width();
}

void UIVirtualCPUEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIVirtualCPUEditor::sltRetranslateUI()
{
    if (m_pLabelVCPU)
        m_pLabelVCPU->setText(tr("&Number of CPUs"));

    QString strToolTip(tr("Number of virtual CPUs in this VM"));

    if (m_pSlider)
        m_pSlider->setToolTip(strToolTip);
    if (m_pSpinBox)
        m_pSpinBox->setToolTip(strToolTip);

    if (m_pLabelVCPUMin)
    {
        m_pLabelVCPUMin->setText(tr("%1 CPU", "%1 is 1 for now").arg(m_uMinVCPUCount));
        m_pLabelVCPUMin->setToolTip(tr("Minimum number of virtual CPUs"));
    }
    if (m_pLabelVCPUMax)
    {
        m_pLabelVCPUMax->setText(tr("%1 CPUs", "%1 is host cpu count * 2 for now").arg(m_uMaxVCPUCount));
        m_pLabelVCPUMax->setToolTip(tr("Maximum number of virtual CPUs"));
    }
}

void UIVirtualCPUEditor::sltHandleSliderChange()
{
    /* Apply spin-box value keeping it's signals disabled: */
    if (m_pSpinBox && m_pSlider)
    {
        m_pSpinBox->blockSignals(true);
        m_pSpinBox->setValue(m_pSlider->value());
        m_pSpinBox->blockSignals(false);
    }

    /* Send signal to listener: */
    emit sigValueChanged(m_pSlider->value());
}

void UIVirtualCPUEditor::sltHandleSpinBoxChange()
{
    /* Apply slider value keeping it's signals disabled: */
    if (m_pSpinBox && m_pSlider)
    {
        m_pSlider->blockSignals(true);
        m_pSlider->setValue(m_pSpinBox->value());
        m_pSlider->blockSignals(false);
    }

    /* Send signal to listener: */
    emit sigValueChanged(m_pSpinBox->value());
}

void UIVirtualCPUEditor::prepare()
{
    /* Prepare common variables: */
    const CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    const uint uHostCPUs = gpGlobalSession->host().GetProcessorOnlineCoreCount();
    m_uMinVCPUCount = comProperties.GetMinGuestCPUCount();
    m_uMaxVCPUCount = qMin(2 * uHostCPUs, (uint)comProperties.GetMaxGuestCPUCount());

    /* Create main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create main label: */
        m_pLabelVCPU = new QLabel(this);
        if (m_pLabelVCPU)
        {
            m_pLabelVCPU->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabelVCPU, 0, 0);
        }

        /* Create slider layout: */
        QVBoxLayout *pSliderLayout = new QVBoxLayout;
        if (pSliderLayout)
        {
            pSliderLayout->setContentsMargins(0, 0, 0, 0);

            /* Create VCPU slider: */
            m_pSlider = new QIAdvancedSlider(this);
            if (m_pSlider)
            {
                m_pSlider->setMinimumWidth(150);
                m_pSlider->setMinimum(m_uMinVCPUCount);
                m_pSlider->setMaximum(m_uMaxVCPUCount);
                m_pSlider->setPageStep(1);
                m_pSlider->setSingleStep(1);
                m_pSlider->setTickInterval(1);
                m_pSlider->setOptimalHint(1, uHostCPUs);
                m_pSlider->setWarningHint(uHostCPUs, m_uMaxVCPUCount);
                connect(m_pSlider, &QIAdvancedSlider::valueChanged,
                        this, &UIVirtualCPUEditor::sltHandleSliderChange);
                pSliderLayout->addWidget(m_pSlider);
            }

            /* Create legend layout: */
            QHBoxLayout *pLegendLayout = new QHBoxLayout;
            if (pLegendLayout)
            {
                pLegendLayout->setContentsMargins(0, 0, 0, 0);

                /* Create min label: */
                m_pLabelVCPUMin = new QLabel(this);
                if (m_pLabelVCPUMin)
                    pLegendLayout->addWidget(m_pLabelVCPUMin);

                /* Push labels from each other: */
                pLegendLayout->addStretch();

                /* Create max label: */
                m_pLabelVCPUMax = new QLabel(this);
                if (m_pLabelVCPUMax)
                    pLegendLayout->addWidget(m_pLabelVCPUMax);

                /* Add legend layout to slider layout: */
                pSliderLayout->addLayout(pLegendLayout);
            }

            /* Add slider layout to main layout: */
            m_pLayout->addLayout(pSliderLayout, 0, 1, 2, 1);
        }

        /* Create VCPU spin-box: */
        m_pSpinBox = new QSpinBox(this);
        if (m_pSpinBox)
        {
            setFocusProxy(m_pSpinBox);
            if (m_pLabelVCPU)
                m_pLabelVCPU->setBuddy(m_pSpinBox);
            m_pSpinBox->setMinimum(m_uMinVCPUCount);
            m_pSpinBox->setMaximum(m_uMaxVCPUCount);
            connect(m_pSpinBox, &QSpinBox::valueChanged,
                    this, &UIVirtualCPUEditor::sltHandleSpinBoxChange);
            m_pLayout->addWidget(m_pSpinBox, 0, 2);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
