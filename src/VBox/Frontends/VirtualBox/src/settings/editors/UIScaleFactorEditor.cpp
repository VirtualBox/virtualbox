/* $Id: UIScaleFactorEditor.cpp 168969 2025-05-23 11:22:24Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIScaleFactorEditor class implementation.
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
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QSpacerItem>
#include <QSpinBox>
#include <QWidget>

/* GUI includes: */
#include "QIAdvancedSlider.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIScaleFactorEditor.h"

/* External includes: */
#include <math.h>


UIScaleFactorEditor::UIScaleFactorEditor(QWidget *pParent)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_pLayout(0)
    , m_pLabel(0)
    , m_pMonitorComboBox(0)
    , m_pScaleSlider(0)
    , m_pScaleSpinBox(0)
    , m_pMinScaleLabel(0)
    , m_pMaxScaleLabel(0)
    , m_dDefaultScaleFactor(1.0)
{
    /* Prepare: */
    prepare();
    /* Append a default scale factor to the list as an global scale factor: */
    m_scaleFactors.append(1.0);
}

void UIScaleFactorEditor::setMonitorCount(int iMonitorCount)
{
    if (!m_pMonitorComboBox)
        return;
    /* We always have 0th for global scale factor (in combo box and scale factor list): */
    int iEndMonitorCount = iMonitorCount + 1;
    int iCurrentMonitorCount = m_pMonitorComboBox->count();
    if (iEndMonitorCount == iCurrentMonitorCount)
        return;
    m_pMonitorComboBox->setEnabled(iMonitorCount > 1);
    m_pMonitorComboBox->blockSignals(true);
    int iCurrentMonitorIndex = m_pMonitorComboBox->currentIndex();
    if (iCurrentMonitorCount < iEndMonitorCount)
    {
        for (int i = iCurrentMonitorCount; i < iEndMonitorCount; ++i)
            m_pMonitorComboBox->insertItem(i, tr("Monitor %1").arg(i));
    }
    else
    {
        for (int i = iCurrentMonitorCount - 1; i >= iEndMonitorCount; --i)
            m_pMonitorComboBox->removeItem(i);
    }
    m_pMonitorComboBox->setEnabled(iMonitorCount > 1);
    /* If we have a single monitor select the "All Monitors" item in the combo
       but make sure we retain the scale factor of the 0th monitor: */
    if (iMonitorCount <= 1)
    {
        if (m_scaleFactors.size() >= 2)
            m_scaleFactors[0] = m_scaleFactors[1];
        m_pMonitorComboBox->setCurrentIndex(0);
    }
    m_pMonitorComboBox->blockSignals(false);
    /* Update the slider and spinbox values if the combobox index has changed: */
    if (iCurrentMonitorIndex != m_pMonitorComboBox->currentIndex())
        updateValuesAfterMonitorChange();
}

void UIScaleFactorEditor::setScaleFactors(const QList<double> &scaleFactors)
{
    m_scaleFactors.clear();
    /* If we have a single value from the extra data and we treat if as a default scale factor: */
    if (scaleFactors.size() == 1)
    {
        m_dDefaultScaleFactor = scaleFactors.at(0);
        m_scaleFactors.append(m_dDefaultScaleFactor);
        setIsGlobalScaleFactor(true);
        return;
    }

    /* Insert 0th element as the global scalar value: */
    m_scaleFactors.append(m_dDefaultScaleFactor);
    m_scaleFactors.append(scaleFactors);
    setIsGlobalScaleFactor(false);
}

QList<double> UIScaleFactorEditor::scaleFactors() const
{
    QList<double> scaleFactorList;
    if (m_scaleFactors.size() == 0)
        return scaleFactorList;

    /* Decide if the users wants a global (not per monitor) scaling. */
    bool globalScaleFactor = false;

    /* if "All Monitors" item is selected in the combobox: */
    if (m_pMonitorComboBox && m_pMonitorComboBox->currentIndex() == 0)
        globalScaleFactor = true;
    /* Also check if all of the monitor scale factors equal to the global scale factor: */
    if (!globalScaleFactor)
    {
        globalScaleFactor = true;
        for (int i = 1; i < m_scaleFactors.size() && globalScaleFactor; ++i)
            if (m_scaleFactors[0] != m_scaleFactors[i])
                globalScaleFactor = false;
    }

    if (globalScaleFactor)
    {
        scaleFactorList << m_scaleFactors.at(0);
    }
    else
    {
        /* Skip the 0th scale factor: */
        for (int i = 1; i < m_scaleFactors.size(); ++i)
            scaleFactorList.append(m_scaleFactors.at(i));
    }

    return scaleFactorList;
}

void UIScaleFactorEditor::setIsGlobalScaleFactor(bool bFlag)
{
    if (!m_pMonitorComboBox)
        return;
    if (bFlag && m_pMonitorComboBox->count() >= 1)
        m_pMonitorComboBox->setCurrentIndex(0);
    else
        if(m_pMonitorComboBox->count() >= 2)
            m_pMonitorComboBox->setCurrentIndex(1);
    updateValuesAfterMonitorChange();
}

void UIScaleFactorEditor::setDefaultScaleFactor(double dDefaultScaleFactor)
{
    m_dDefaultScaleFactor = dDefaultScaleFactor;
}

int UIScaleFactorEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIScaleFactorEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIScaleFactorEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Scale &Factor"));

    if (m_pMonitorComboBox)
    {
        if (m_pMonitorComboBox->count() > 0)
        {
            m_pMonitorComboBox->setItemText(0, tr("All Monitors"));
            for (int i = 1; i < m_pMonitorComboBox->count(); ++i)
                m_pMonitorComboBox->setItemText(i, tr("Monitor %1").arg(i));
        }
        m_pMonitorComboBox->setToolTip(tr("The index of virtual monitor guest screen scale factor being defined for"));
    }

    const QString strToolTip = tr("The guest screen scale factor the virtual machine has");
    if (m_pScaleSlider)
        m_pScaleSlider->setToolTip(strToolTip);
    if (m_pScaleSpinBox)
        m_pScaleSpinBox->setToolTip(strToolTip);

    if (m_pMinScaleLabel)
    {
        m_pMinScaleLabel->setText(QString("%1%").arg(m_pScaleSlider->minimum()));
        m_pMinScaleLabel->setToolTip(tr("Minimum guest screen scale factor"));
    }
    if (m_pMaxScaleLabel)
    {
        m_pMaxScaleLabel->setText(QString("%1%").arg(m_pScaleSlider->maximum()));
        m_pMaxScaleLabel->setToolTip(tr("Maximum guest screen scale factor"));
    }
}

void UIScaleFactorEditor::sltScaleSpinBoxValueChanged(int value)
{
    setSliderValue(value);
    if (m_pMonitorComboBox)
        setScaleFactor(m_pMonitorComboBox->currentIndex(), value);
}

void UIScaleFactorEditor::sltScaleSliderValueChanged(int value)
{
    setSpinBoxValue(value);
    if (m_pMonitorComboBox)
        setScaleFactor(m_pMonitorComboBox->currentIndex(), value);
}

void UIScaleFactorEditor::sltMonitorComboIndexChanged(int)
{
    updateValuesAfterMonitorChange();
}

void UIScaleFactorEditor::prepare()
{
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(1, 1);
        m_pLayout->setColumnStretch(2, 1);

        /* Prepare label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }

        m_pMonitorComboBox = new QComboBox(this);
        if (m_pMonitorComboBox)
        {
            m_pMonitorComboBox->insertItem(0, "All Monitors");
            connect(m_pMonitorComboBox, &QComboBox::currentIndexChanged,
                    this, &UIScaleFactorEditor::sltMonitorComboIndexChanged);
            m_pLayout->addWidget(m_pMonitorComboBox, 0, 1);
        }

        m_pScaleSlider = new QIAdvancedSlider(this);
        {
            if (m_pLabel)
                m_pLabel->setBuddy(m_pScaleSlider);
            m_pScaleSlider->setPageStep(10);
            m_pScaleSlider->setSingleStep(1);
            m_pScaleSlider->setTickInterval(10);
            m_pScaleSlider->setSnappingEnabled(true);
            connect(m_pScaleSlider, &QIAdvancedSlider::valueChanged,
                    this, &UIScaleFactorEditor::sltScaleSliderValueChanged);
            m_pLayout->addWidget(m_pScaleSlider, 0, 2, 1, 2);
        }

        m_pScaleSpinBox = new QSpinBox(this);
        if (m_pScaleSpinBox)
        {
            setFocusProxy(m_pScaleSpinBox);
            m_pScaleSpinBox->setSuffix("%");
            connect(m_pScaleSpinBox, &QSpinBox::valueChanged,
                    this, &UIScaleFactorEditor::sltScaleSpinBoxValueChanged);
            m_pLayout->addWidget(m_pScaleSpinBox, 0, 4);
        }

        m_pMinScaleLabel = new QLabel(this);
        if (m_pMinScaleLabel)
            m_pLayout->addWidget(m_pMinScaleLabel, 1, 2);

        m_pMaxScaleLabel = new QLabel(this);
        if (m_pMaxScaleLabel)
            m_pLayout->addWidget(m_pMaxScaleLabel, 1, 3);
    }

    prepareScaleFactorMinMaxValues();
    sltRetranslateUI();
}

void UIScaleFactorEditor::prepareScaleFactorMinMaxValues()
{
    const int iHostScreenCount = UIDesktopWidgetWatchdog::screenCount();
    if (iHostScreenCount == 0)
        return;
    double dMaxDevicePixelRatio = UIDesktopWidgetWatchdog::devicePixelRatio(0);
    for (int i = 1; i < iHostScreenCount; ++i)
        if (dMaxDevicePixelRatio < UIDesktopWidgetWatchdog::devicePixelRatio(i))
            dMaxDevicePixelRatio = UIDesktopWidgetWatchdog::devicePixelRatio(i);

    const int iMinimum = 100;
    const int iMaximum = ceil(iMinimum + 100 * dMaxDevicePixelRatio);

    const int iStep = 25;

    m_pScaleSlider->setMinimum(iMinimum);
    m_pScaleSlider->setMaximum(iMaximum);
    m_pScaleSlider->setPageStep(iStep);
    m_pScaleSlider->setSingleStep(1);
    m_pScaleSlider->setTickInterval(iStep);
    m_pScaleSpinBox->setMinimum(iMinimum);
    m_pScaleSpinBox->setMaximum(iMaximum);
}

void UIScaleFactorEditor::setScaleFactor(int iMonitorIndex, int iScaleFactor)
{
    /* Make sure we have the corresponding scale values for all monitors: */
    if (m_pMonitorComboBox->count() > m_scaleFactors.size())
    {
        for (int i = m_scaleFactors.size(); i < m_pMonitorComboBox->count(); ++i)
            m_scaleFactors.append(m_dDefaultScaleFactor);
    }
    m_scaleFactors[iMonitorIndex] = iScaleFactor / 100.0;
}

void UIScaleFactorEditor::setSliderValue(int iValue)
{
    if (m_pScaleSlider && iValue != m_pScaleSlider->value())
    {
        m_pScaleSlider->blockSignals(true);
        m_pScaleSlider->setValue(iValue);
        m_pScaleSlider->blockSignals(false);
    }
}

void UIScaleFactorEditor::setSpinBoxValue(int iValue)
{
    if (m_pScaleSpinBox && iValue != m_pScaleSpinBox->value())
    {
        m_pScaleSpinBox->blockSignals(true);
        m_pScaleSpinBox->setValue(iValue);
        m_pScaleSpinBox->blockSignals(false);
    }
}

void UIScaleFactorEditor::updateValuesAfterMonitorChange()
{
    /* Set the spinbox value for the currently selected monitor: */
    if (m_pMonitorComboBox)
    {
        int currentMonitorIndex = m_pMonitorComboBox->currentIndex();
        while (m_scaleFactors.size() <= currentMonitorIndex)
            m_scaleFactors.append(m_dDefaultScaleFactor);

        setSpinBoxValue(100 * m_scaleFactors.at(currentMonitorIndex));
        setSliderValue(100 * m_scaleFactors.at(currentMonitorIndex));

    }
}
