/* $Id: UIVideoMemoryEditor.cpp 168969 2025-05-23 11:22:24Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVideoMemoryEditor class implementation.
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
#include "UIGuestOSType.h"
#include "UIVideoMemoryEditor.h"

/* COM includes: */
#include "CPlatformProperties.h"
#include "CSystemProperties.h"


UIVideoMemoryEditor::UIVideoMemoryEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_iValue(0)
    , m_cGuestScreenCount(1)
    , m_enmGraphicsControllerType(KGraphicsControllerType_Null)
#ifdef VBOX_WITH_3D_ACCELERATION
    , m_f3DAccelerationSupported(false)
    , m_f3DAccelerationEnabled(false)
#endif
    , m_iMaxVRAM(0)
    , m_pLayout(0)
    , m_pLabelMemory(0)
    , m_pSlider(0)
    , m_pLabelMemoryMin(0)
    , m_pLabelMemoryMax(0)
    , m_pSpinBox(0)
{
    prepare();
}

void UIVideoMemoryEditor::setValue(int iValue)
{
    /* Update cached value and
     * slider if value has changed: */
    if (m_iValue != iValue)
    {
        m_iValue = qMin(iValue, m_iMaxVRAM);
        if (m_pSlider)
            m_pSlider->setValue(m_iValue);

        /* Update requirements: */
        updateRequirements();
    }
}

int UIVideoMemoryEditor::value() const
{
    return m_pSlider ? m_pSlider->value() : m_iValue;
}

void UIVideoMemoryEditor::setGuestOSTypeId(const QString &strGuestOSTypeId)
{
    /* Update cached value and
     * requirements if value has changed: */
    if (m_strGuestOSTypeId != strGuestOSTypeId)
    {
        /* Remember new guest OS type: */
        m_strGuestOSTypeId = strGuestOSTypeId;

        /* Update requirements: */
        updateRequirements();
    }
}

void UIVideoMemoryEditor::setGuestScreenCount(int cGuestScreenCount)
{
    /* Update cached value and
     * requirements if value has changed: */
    if (m_cGuestScreenCount != cGuestScreenCount)
    {
        /* Remember new guest screen count: */
        m_cGuestScreenCount = cGuestScreenCount;

        /* Update requirements: */
        updateRequirements();
    }
}

void UIVideoMemoryEditor::setGraphicsControllerType(const KGraphicsControllerType &enmGraphicsControllerType)
{
    /* Update cached value and
     * requirements if value has changed: */
    if (m_enmGraphicsControllerType != enmGraphicsControllerType)
    {
        /* Remember new graphics controller type: */
        m_enmGraphicsControllerType = enmGraphicsControllerType;

        /* Update requirements: */
        updateRequirements();
    }
}

#ifdef VBOX_WITH_3D_ACCELERATION
void UIVideoMemoryEditor::set3DAccelerationSupported(bool fSupported)
{
    /* Update cached value and
     * requirements if value has changed: */
    if (m_f3DAccelerationSupported != fSupported)
    {
        /* Remember new 3D acceleration: */
        m_f3DAccelerationSupported = fSupported;

        /* Update requirements: */
        updateRequirements();
    }
}

void UIVideoMemoryEditor::set3DAccelerationEnabled(bool fEnabled)
{
    /* Update cached value and
     * requirements if value has changed: */
    if (m_f3DAccelerationEnabled != fEnabled)
    {
        /* Remember new 3D acceleration: */
        m_f3DAccelerationEnabled = fEnabled;

        /* Update requirements: */
        updateRequirements();
    }
}
#endif /* VBOX_WITH_3D_ACCELERATION */

int UIVideoMemoryEditor::minimumLabelHorizontalHint() const
{
    return m_pLabelMemory->minimumSizeHint().width();
}

void UIVideoMemoryEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIVideoMemoryEditor::sltRetranslateUI()
{
    if (m_pLabelMemory)
        m_pLabelMemory->setText(tr("Video &Memory"));

    const QString strToolTip(tr("The amount of video memory the virtual machine has"));
    if (m_pSlider)
        m_pSlider->setToolTip(strToolTip);
    if (m_pSpinBox)
    {
        m_pSpinBox->setSuffix(QString(" %1").arg(tr("MB")));
        m_pSpinBox->setToolTip(strToolTip);
    }

    if (m_pLabelMemoryMin)
    {
        m_pLabelMemoryMin->setText(tr("%1 MB").arg(0));
        m_pLabelMemoryMin->setToolTip(tr("Minimum video memory size"));
    }
    if (m_pLabelMemoryMax)
    {
        m_pLabelMemoryMax->setText(tr("%1 MB").arg(m_iMaxVRAM));
        m_pLabelMemoryMax->setToolTip(tr("Maximum video memory size"));
    }
}

void UIVideoMemoryEditor::sltHandleSliderChange()
{
    /* Apply spin-box value keeping it's signals disabled: */
    if (m_pSpinBox && m_pSlider)
    {
        m_pSpinBox->blockSignals(true);
        m_pSpinBox->setValue(m_pSlider->value());
        m_pSpinBox->blockSignals(false);
    }

    /* Revalidate to send signal to listener: */
    revalidate();
}

void UIVideoMemoryEditor::sltHandleSpinBoxChange()
{
    /* Apply slider value keeping it's signals disabled: */
    if (m_pSpinBox && m_pSlider)
    {
        m_pSlider->blockSignals(true);
        m_pSlider->setValue(m_pSpinBox->value());
        m_pSlider->blockSignals(false);
    }

    /* Revalidate to send signal to listener: */
    revalidate();
}

void UIVideoMemoryEditor::prepare()
{
    /* Prepare common variables: */
    const CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    m_iMaxVRAM = comProperties.GetMaxGuestVRAM();

    /* Create main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create memory label: */
        m_pLabelMemory = new QLabel(this);
        if (m_pLabelMemory)
        {
            m_pLabelMemory->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabelMemory, 0, 0);
        }

        /* Create slider layout: */
        QVBoxLayout *pSliderLayout = new QVBoxLayout;
        if (pSliderLayout)
        {
            pSliderLayout->setContentsMargins(0, 0, 0, 0);

            /* Create memory slider: */
            m_pSlider = new QIAdvancedSlider(this);
            if (m_pSlider)
            {
                m_pSlider->setMinimum(0);
                m_pSlider->setMaximum(m_iMaxVRAM);
                m_pSlider->setPageStep(calculatePageStep(m_iMaxVRAM));
                m_pSlider->setSingleStep(m_pSlider->pageStep() / 4);
                m_pSlider->setTickInterval(m_pSlider->pageStep());
                m_pSlider->setSnappingEnabled(true);
                m_pSlider->setErrorHint(0, 1);
                m_pSlider->setMinimumWidth(150);
                connect(m_pSlider, &QIAdvancedSlider::valueChanged,
                        this, &UIVideoMemoryEditor::sltHandleSliderChange);
                pSliderLayout->addWidget(m_pSlider);
            }

            /* Create legend layout: */
            QHBoxLayout *pLegendLayout = new QHBoxLayout;
            if (pLegendLayout)
            {
                pLegendLayout->setContentsMargins(0, 0, 0, 0);

                /* Create min label: */
                m_pLabelMemoryMin = new QLabel(this);
                if (m_pLabelMemoryMin)
                    pLegendLayout->addWidget(m_pLabelMemoryMin);

                /* Push labels from each other: */
                pLegendLayout->addStretch();

                /* Create max label: */
                m_pLabelMemoryMax = new QLabel(this);
                if (m_pLabelMemoryMax)
                    pLegendLayout->addWidget(m_pLabelMemoryMax);

                /* Add legend layout to slider layout: */
                pSliderLayout->addLayout(pLegendLayout);
            }

            /* Add slider layout to main layout: */
            m_pLayout->addLayout(pSliderLayout, 0, 1, 2, 1);
        }

        /* Create memory spin-box: */
        m_pSpinBox = new QSpinBox(this);
        if (m_pSpinBox)
        {
            setFocusProxy(m_pSpinBox);
            if (m_pLabelMemory)
                m_pLabelMemory->setBuddy(m_pSpinBox);
            m_pSpinBox->setMinimum(0);
            m_pSpinBox->setMaximum(m_iMaxVRAM);
            connect(m_pSpinBox, &QSpinBox::valueChanged,
                    this, &UIVideoMemoryEditor::sltHandleSpinBoxChange);
            m_pLayout->addWidget(m_pSpinBox, 0, 2);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIVideoMemoryEditor::updateRequirements()
{
    /* Make sure guest OS type is set: */
    if (m_strGuestOSTypeId.isEmpty())
        return;

    /* Acquire minimum and maximum visible VRAM size on the basis of platform data: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_x86;
    CPlatformProperties comPlatformProperties = gpGlobalSession->virtualBox().GetPlatformProperties(enmArch);
    bool f3DAccelerationEnabled = false;
#ifdef VBOX_WITH_3D_ACCELERATION
    f3DAccelerationEnabled = m_f3DAccelerationEnabled;
#endif
    ULONG uMinVRAM = 0, uMaxVRAM = 0;
    comPlatformProperties.GetSupportedVRAMRange(m_enmGraphicsControllerType, f3DAccelerationEnabled, uMinVRAM, uMaxVRAM);

    /* Init recommended VRAM according to guest OS type and screen count: */
    int iNeedMBytes = UIGuestOSTypeHelpers::requiredVideoMemory(m_strGuestOSTypeId, m_cGuestScreenCount) / _1M;

    /* Init visible maximum VRAM to be no less than 32MB per screen: */
    int iMaxVRAMVisible = m_cGuestScreenCount * 32;
    /* Adjust visible maximum VRAM to be no less than 128MB (if possible): */
    if (iMaxVRAMVisible < 128 && uMaxVRAM >= 128)
        iMaxVRAMVisible = 128;

#ifdef VBOX_WITH_3D_ACCELERATION
    if (m_f3DAccelerationEnabled && m_f3DAccelerationSupported)
    {
        /* Adjust recommended VRAM to be no less than 128MB: */
        iNeedMBytes = qMax(iNeedMBytes, 128);

        /* Adjust visible maximum VRAM to be no less than 256MB (if possible): */
        if (iMaxVRAMVisible < 256 && uMaxVRAM >= 256)
            iMaxVRAMVisible = 256;
    }
#endif /* VBOX_WITH_3D_ACCELERATION */

    /* Adjust recommended VRAM to be no more than actual maximum VRAM: */
    iNeedMBytes = qMin(iNeedMBytes, (int)uMaxVRAM);

    /* Adjust visible maximum VRAM to be no less than initial VRAM: */
    iMaxVRAMVisible = qMax(iMaxVRAMVisible, m_iValue);
    /* Adjust visible maximum VRAM to be no less than recommended VRAM: */
    iMaxVRAMVisible = qMax(iMaxVRAMVisible, iNeedMBytes);
    /* Adjust visible maximum VRAM to be no more than actual maximum VRAM: */
    iMaxVRAMVisible = qMin(iMaxVRAMVisible, (int)uMaxVRAM);

    if (m_pSpinBox)
        m_pSpinBox->setMaximum(iMaxVRAMVisible);
    if (m_pSlider)
    {
        m_pSlider->setMaximum(iMaxVRAMVisible);
        m_pSlider->setPageStep(calculatePageStep(iMaxVRAMVisible));
        m_pSlider->setErrorHint(0, uMinVRAM);
        m_pSlider->setWarningHint(uMinVRAM, iNeedMBytes);
        m_pSlider->setOptimalHint(iNeedMBytes, iMaxVRAMVisible);
    }
    if (m_pLabelMemoryMax)
        m_pLabelMemoryMax->setText(tr("%1 MB").arg(iMaxVRAMVisible));
}

void UIVideoMemoryEditor::revalidate()
{
    if (m_pSlider)
        emit sigValidChanged(   m_enmGraphicsControllerType == KGraphicsControllerType_Null
                             || m_pSlider->value() > 0);
}

/* static */
int UIVideoMemoryEditor::calculatePageStep(int iMax)
{
    /* Reasonable max. number of page steps is 32. */
    const uint uPage = ((uint)iMax + 31) / 32;
    /* Make it a power of 2: */
    uint uP = uPage, p2 = 0x1;
    while ((uP >>= 1))
        p2 <<= 1;
    if (uPage != p2)
        p2 <<= 1;
    if (p2 < 4)
        p2 = 4;
    return (int)p2;
}
