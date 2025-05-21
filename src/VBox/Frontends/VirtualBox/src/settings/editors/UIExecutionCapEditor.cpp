/* $Id: UIExecutionCapEditor.cpp 168951 2025-05-21 10:21:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExecutionCapEditor class implementation.
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
#include "UICommon.h"
#include "UIExecutionCapEditor.h"

UIExecutionCapEditor::UIExecutionCapEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_uMinExecCap(1)
    , m_uMedExecCap(40)
    , m_uMaxExecCap(100)
    , m_pLabelExecCap(0)
    , m_pSlider(0)
    , m_pSpinBox(0)
    , m_pLabelExecCapMin(0)
    , m_pLabelExecCapMax(0)
{
    prepare();
}

int UIExecutionCapEditor::medExecCap() const
{
    return (int)m_uMedExecCap;
}

void UIExecutionCapEditor::setValue(int iValue)
{
    if (m_pSlider)
        m_pSlider->setValue(iValue);
}

int UIExecutionCapEditor::value() const
{
    return m_pSlider ? m_pSlider->value() : 0;
}

int UIExecutionCapEditor::minimumLabelHorizontalHint() const
{
    return m_pLabelExecCap->minimumSizeHint().width();
}

void UIExecutionCapEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIExecutionCapEditor::sltRetranslateUI()
{
    if (m_pLabelExecCap)
        m_pLabelExecCap->setText(tr("&Processing Cap"));

    const QString strToolTip(tr("Percentage of processing time allowed per virtual CPU"));
    if (m_pSlider)
        m_pSlider->setToolTip(strToolTip);
    if (m_pSpinBox)
    {
        m_pSpinBox->setSuffix(QString("%"));
        m_pSpinBox->setToolTip(strToolTip);
    }

    if (m_pLabelExecCapMin)
    {
        m_pLabelExecCapMin->setText(QString("%1%").arg(m_uMinExecCap));
        m_pLabelExecCapMin->setToolTip(tr("Minimum execution cap"));
    }
    if (m_pLabelExecCapMax)
    {
        m_pLabelExecCapMax->setText(QString("%1%").arg(m_uMaxExecCap));
        m_pLabelExecCapMax->setToolTip(tr("Maximum execution cap"));
    }
}

void UIExecutionCapEditor::sltHandleSliderChange()
{
    /* Apply spin-box value keeping it's signals disabled: */
    if (m_pSpinBox && m_pSlider)
    {
        m_pSpinBox->blockSignals(true);
        m_pSpinBox->setValue(m_pSlider->value());
        m_pSpinBox->blockSignals(false);
    }

    /* Send signal to listener: */
    emit sigValueChanged();
}

void UIExecutionCapEditor::sltHandleSpinBoxChange()
{
    /* Apply slider value keeping it's signals disabled: */
    if (m_pSpinBox && m_pSlider)
    {
        m_pSlider->blockSignals(true);
        m_pSlider->setValue(m_pSpinBox->value());
        m_pSlider->blockSignals(false);
    }

    /* Send signal to listener: */
    emit sigValueChanged();
}

void UIExecutionCapEditor::prepare()
{
    /* Create main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create main label: */
        m_pLabelExecCap = new QLabel(this);
        if (m_pLabelExecCap)
        {
            m_pLabelExecCap->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabelExecCap, 0, 0);
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
                m_pSlider->setOrientation(Qt::Horizontal);
                m_pSlider->setPageStep(10);
                m_pSlider->setSingleStep(1);
                m_pSlider->setTickInterval(10);
                m_pSlider->setMinimum(m_uMinExecCap);
                m_pSlider->setMaximum(m_uMaxExecCap);
                m_pSlider->setWarningHint(m_uMinExecCap, m_uMedExecCap);
                m_pSlider->setOptimalHint(m_uMedExecCap, m_uMaxExecCap);
                connect(m_pSlider, &QIAdvancedSlider::valueChanged,
                        this, &UIExecutionCapEditor::sltHandleSliderChange);
                pSliderLayout->addWidget(m_pSlider);
            }

            /* Create legend layout: */
            QHBoxLayout *pLegendLayout = new QHBoxLayout;
            if (pLegendLayout)
            {
                pLegendLayout->setContentsMargins(0, 0, 0, 0);

                /* Create min label: */
                m_pLabelExecCapMin = new QLabel(this);
                if (m_pLabelExecCapMin)
                    pLegendLayout->addWidget(m_pLabelExecCapMin);

                /* Push labels from each other: */
                pLegendLayout->addStretch();

                /* Create max label: */
                m_pLabelExecCapMax = new QLabel(this);
                if (m_pLabelExecCapMax)
                    pLegendLayout->addWidget(m_pLabelExecCapMax);

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
            if (m_pLabelExecCap)
                m_pLabelExecCap->setBuddy(m_pSpinBox);
            m_pSpinBox->setMinimum(m_uMinExecCap);
            m_pSpinBox->setMaximum(m_uMaxExecCap);
            uiCommon().setMinimumWidthAccordingSymbolCount(m_pSpinBox, 4);
            connect(m_pSpinBox, &QSpinBox::valueChanged,
                    this, &UIExecutionCapEditor::sltHandleSpinBoxChange);
            m_pLayout->addWidget(m_pSpinBox, 0, 2);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
