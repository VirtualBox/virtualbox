/* $Id: UIFontScaleEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFontScaleEditor class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <QLineEdit>
#include <QSpacerItem>
#include <QSpinBox>
#include <QWidget>

/* GUI includes: */
#include "QIAdvancedSlider.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataDefs.h"
#include "UIFontScaleEditor.h"

/* External includes: */
#include <math.h>

class UIFontScaleFactorSpinBox : public QSpinBox
{
public:
    UIFontScaleFactorSpinBox(QWidget *parent = 0);
};

UIFontScaleFactorSpinBox::UIFontScaleFactorSpinBox(QWidget *pParent /* = 0*/)
    :QSpinBox(pParent)
{
    QLineEdit *pLineEdit = new QLineEdit;
    if (pLineEdit)
    {
        pLineEdit->setReadOnly(true);
        setLineEdit(pLineEdit);
    }
}

UIFontScaleEditor::UIFontScaleEditor(QWidget *pParent)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_pLayout(0)
    , m_pLabel(0)
    , m_pScaleSlider(0)
    , m_pScaleSpinBox(0)
    , m_pMinScaleLabel(0)
    , m_pMaxScaleLabel(0)
    , m_iSliderRangeDivisor(10)
{
    /* Prepare: */
    prepare();
}

int UIFontScaleEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIFontScaleEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIFontScaleEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("F&ont Scaling:"));

    if (m_pScaleSlider)
        m_pScaleSlider->setToolTip(tr("Holds the scaling factor for the font size."));
    if (m_pScaleSpinBox)
        m_pScaleSpinBox->setToolTip(tr("Holds the scaling factor for the font size."));

    if (m_pMinScaleLabel)
    {
        m_pMinScaleLabel->setText(QString("%1%").arg(m_pScaleSpinBox->minimum()));
        m_pMinScaleLabel->setToolTip(tr("Minimum possible scale factor."));
    }
    if (m_pMaxScaleLabel)
    {
        m_pMaxScaleLabel->setText(QString("%1%").arg(m_pScaleSpinBox->maximum()));
        m_pMaxScaleLabel->setToolTip(tr("Maximum possible scale factor."));
    }
}

void UIFontScaleEditor::sltScaleSpinBoxValueChanged(int value)
{
    setSliderValue(value / m_iSliderRangeDivisor);
}

void UIFontScaleEditor::sltScaleSliderValueChanged(int value)
{
    setSpinBoxValue(m_iSliderRangeDivisor * value);
    setFontScaleFactor(m_iSliderRangeDivisor * value);
}

void UIFontScaleEditor::setFontScaleFactor(int iFontScaleFactor)
{
    setSliderValue(iFontScaleFactor / m_iSliderRangeDivisor);
    setSpinBoxValue(iFontScaleFactor);
}

int UIFontScaleEditor::fontScaleFactor() const
{
    return m_pScaleSpinBox->value();
}

void UIFontScaleEditor::sltMonitorComboIndexChanged(int)
{
}

void UIFontScaleEditor::prepare()
{
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

        m_pScaleSlider = new QIAdvancedSlider(this);
        {
            if (m_pLabel)
                m_pLabel->setBuddy(m_pScaleSlider);
            m_pScaleSlider->setSnappingEnabled(true);
            connect(m_pScaleSlider, &QIAdvancedSlider::valueChanged,
                    this, &UIFontScaleEditor::sltScaleSliderValueChanged);
            m_pLayout->addWidget(m_pScaleSlider, 0, 1, 1, 4);
        }

        m_pScaleSpinBox = new UIFontScaleFactorSpinBox(this);
        if (m_pScaleSpinBox)
        {
            setFocusProxy(m_pScaleSpinBox);
            m_pScaleSpinBox->setSuffix("%");
            connect(m_pScaleSpinBox, &QSpinBox::valueChanged,
                    this, &UIFontScaleEditor::sltScaleSpinBoxValueChanged);
            m_pLayout->addWidget(m_pScaleSpinBox, 0, 5);
        }

        m_pMinScaleLabel = new QLabel(this);
        if (m_pMinScaleLabel)
            m_pLayout->addWidget(m_pMinScaleLabel, 1, 1);

        m_pMaxScaleLabel = new QLabel(this);
        if (m_pMaxScaleLabel)
            m_pLayout->addWidget(m_pMaxScaleLabel, 1, 4);
    }

    prepareScaleFactorMinMax();
    sltRetranslateUI();
}

void UIFontScaleEditor::prepareScaleFactorMinMax()
{
    m_pScaleSlider->blockSignals(true);
    m_pScaleSpinBox->blockSignals(true);
    const int iMinimum = UIExtraDataDefs::iFontScaleMin;
    const int iMaximum = UIExtraDataDefs::iFontScaleMax;

    /* Set slider min, max, and intervals so to make sure mouse drag stops only on ticks: */
    m_pScaleSlider->setMinimum(iMinimum / m_iSliderRangeDivisor);
    m_pScaleSlider->setMaximum(iMaximum / m_iSliderRangeDivisor);

    m_pScaleSlider->setPageStep(2);
    m_pScaleSlider->setSingleStep(1);
    m_pScaleSlider->setTickInterval(1);
    m_pScaleSpinBox->setSingleStep(10);

    m_pScaleSpinBox->setMinimum(iMinimum);
    m_pScaleSpinBox->setMaximum(iMaximum);

    m_pScaleSlider->blockSignals(false);
    m_pScaleSpinBox->blockSignals(false);
}

void UIFontScaleEditor::setSliderValue(int iValue)
{
    if (m_pScaleSlider && iValue != m_pScaleSlider->value())
    {
        m_pScaleSlider->blockSignals(true);
        m_pScaleSlider->setValue(iValue);
        m_pScaleSlider->blockSignals(false);
    }
}

void UIFontScaleEditor::setSpinBoxValue(int iValue)
{
    if (m_pScaleSpinBox && iValue != m_pScaleSpinBox->value())
    {
        m_pScaleSpinBox->blockSignals(true);
        m_pScaleSpinBox->setValue(iValue);
        m_pScaleSpinBox->blockSignals(false);
    }
}
