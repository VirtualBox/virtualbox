/* $Id: UIBaseMemoryEditor.cpp 168951 2025-05-21 10:21:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIBaseMemoryEditor class implementation.
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
#include "UIBaseMemoryEditor.h"
#include "UIGlobalSession.h"

/* COM includes: */
#include "CSystemProperties.h"


/** QIAdvancedSlider subclass used as a base memory slider. */
class SHARED_LIBRARY_STUFF UIBaseMemorySlider : public QIAdvancedSlider
{
    Q_OBJECT;

public:

    /** Constructs guest RAM slider passing @a pParent to the base-class. */
    UIBaseMemorySlider(QWidget *pParent = 0);
    /** Constructs guest RAM slider passing @a pParent and @a enmOrientation to the base-class. */
    UIBaseMemorySlider(Qt::Orientation enmOrientation, QWidget *pParent = 0);

    /** Returns the minimum RAM. */
    uint minRAM() const;
    /** Returns the maximum optimal RAM. */
    uint maxRAMOpt() const;
    /** Returns the maximum allowed RAM. */
    uint maxRAMAlw() const;
    /** Returns the maximum possible RAM. */
    uint maxRAM() const;

private:

    /** Prepares all. */
    void prepare();

    /** Calculates page step for passed @a iMaximum value. */
    int calcPageStep(int iMaximum) const;

    /** Holds the minimum RAM. */
    uint  m_uMinRAM;
    /** Holds the maximum optimal RAM. */
    uint  m_uMaxRAMOpt;
    /** Holds the maximum allowed RAM. */
    uint  m_uMaxRAMAlw;
    /** Holds the maximum possible RAM. */
    uint  m_uMaxRAM;
};


/*********************************************************************************************************************************
*   Class UIBaseMemorySlider implementation.                                                                                     *
*********************************************************************************************************************************/

UIBaseMemorySlider::UIBaseMemorySlider(QWidget *pParent /* = 0 */)
  : QIAdvancedSlider(pParent)
  , m_uMinRAM(0)
  , m_uMaxRAMOpt(0)
  , m_uMaxRAMAlw(0)
  , m_uMaxRAM(0)
{
    prepare();
}

UIBaseMemorySlider::UIBaseMemorySlider(Qt::Orientation enmOrientation, QWidget *pParent /* = 0 */)
  : QIAdvancedSlider(enmOrientation, pParent)
  , m_uMinRAM(0)
  , m_uMaxRAMOpt(0)
  , m_uMaxRAMAlw(0)
  , m_uMaxRAM(0)
{
    prepare();
}

uint UIBaseMemorySlider::minRAM() const
{
    return m_uMinRAM;
}

uint UIBaseMemorySlider::maxRAMOpt() const
{
    return m_uMaxRAMOpt;
}

uint UIBaseMemorySlider::maxRAMAlw() const
{
    return m_uMaxRAMAlw;
}

uint UIBaseMemorySlider::maxRAM() const
{
    return m_uMaxRAM;
}

void UIBaseMemorySlider::prepare()
{
    ulong uFullSize = gpGlobalSession->host().GetMemorySize();
    CSystemProperties sys = gpGlobalSession->virtualBox().GetSystemProperties();
    m_uMinRAM = sys.GetMinGuestRAM();
    m_uMaxRAM = RT_MIN(RT_ALIGN(uFullSize, _1G / _1M), sys.GetMaxGuestRAM());

    /* Come up with some nice round percent boundaries relative to
     * the system memory. A max of 75% on a 256GB config is ridiculous,
     * even on an 8GB rig reserving 2GB for the OS is way to conservative.
     * The max numbers can be estimated using the following program:
     *
     *      double calcMaxPct(uint64_t cbRam)
     *      {
     *          double cbRamOverhead = cbRam * 0.0390625; // 160 bytes per page.
     *          double cbRamForTheOS = RT_MAX(RT_MIN(_512M, cbRam * 0.25), _64M);
     *          double OSPct  = (cbRamOverhead + cbRamForTheOS) * 100.0 / cbRam;
     *          double MaxPct = 100 - OSPct;
     *          return MaxPct;
     *      }
     *
     *      int main()
     *      {
     *          uint64_t cbRam = _1G;
     *          for (; !(cbRam >> 33); cbRam += _1G)
     *              printf("%8lluGB %.1f%% %8lluKB\n", cbRam >> 30, calcMaxPct(cbRam),
     *                     (uint64_t)(cbRam * calcMaxPct(cbRam) / 100.0) >> 20);
     *          for (; !(cbRam >> 51); cbRam <<= 1)
     *              printf("%8lluGB %.1f%% %8lluKB\n", cbRam >> 30, calcMaxPct(cbRam),
     *                     (uint64_t)(cbRam * calcMaxPct(cbRam) / 100.0) >> 20);
     *          return 0;
     *      }
     *
     * Note. We might wanna put these calculations somewhere global later. */

    /* System RAM amount test: */
    m_uMaxRAMAlw = (uint)(0.75 * uFullSize);
    m_uMaxRAMOpt = (uint)(0.50 * uFullSize);
    if (uFullSize < 3072)
        /* done */;
    else if (uFullSize < 4096)   /* 3GB */
        m_uMaxRAMAlw = (uint)(0.80 * uFullSize);
    else if (uFullSize < 6144)   /* 4-5GB */
    {
        m_uMaxRAMAlw = (uint)(0.84 * uFullSize);
        m_uMaxRAMOpt = (uint)(0.60 * uFullSize);
    }
    else if (uFullSize < 8192)   /* 6-7GB */
    {
        m_uMaxRAMAlw = (uint)(0.88 * uFullSize);
        m_uMaxRAMOpt = (uint)(0.65 * uFullSize);
    }
    else if (uFullSize < 16384)  /* 8-15GB */
    {
        m_uMaxRAMAlw = (uint)(0.90 * uFullSize);
        m_uMaxRAMOpt = (uint)(0.70 * uFullSize);
    }
    else if (uFullSize < 32768)  /* 16-31GB */
    {
        m_uMaxRAMAlw = (uint)(0.93 * uFullSize);
        m_uMaxRAMOpt = (uint)(0.75 * uFullSize);
    }
    else if (uFullSize < 65536)  /* 32-63GB */
    {
        m_uMaxRAMAlw = (uint)(0.94 * uFullSize);
        m_uMaxRAMOpt = (uint)(0.80 * uFullSize);
    }
    else if (uFullSize < 131072) /* 64-127GB */
    {
        m_uMaxRAMAlw = (uint)(0.95 * uFullSize);
        m_uMaxRAMOpt = (uint)(0.85 * uFullSize);
    }
    else                        /* 128GB- */
    {
        m_uMaxRAMAlw = (uint)(0.96 * uFullSize);
        m_uMaxRAMOpt = (uint)(0.90 * uFullSize);
    }
    /* Now check the calculated maximums are out of the range for the guest
     * RAM. If so change it accordingly. */
    m_uMaxRAMAlw = RT_MIN(m_uMaxRAMAlw, m_uMaxRAM);
    m_uMaxRAMOpt = RT_MIN(m_uMaxRAMOpt, m_uMaxRAM);

    setPageStep(calcPageStep(m_uMaxRAM));
    setSingleStep(pageStep() / 4);
    setTickInterval(pageStep());
    /* Setup the scale so that ticks are at page step boundaries */
    if (m_uMinRAM >= static_cast<uint>(pageStep()))
        setMinimum((m_uMinRAM / pageStep()) * pageStep());
    else
        setMinimum(m_uMinRAM);

    setMaximum(m_uMaxRAM);
    setSnappingEnabled(true);
    setOptimalHint(m_uMinRAM, m_uMaxRAMOpt);
    setWarningHint(m_uMaxRAMOpt, m_uMaxRAMAlw);
    setErrorHint(m_uMaxRAMAlw, m_uMaxRAM);
}

int UIBaseMemorySlider::calcPageStep(int iMaximum) const
{
    /* Calculate a suitable page step size for the given max value.
     * The returned size is so that there will be no more than 32
     * pages. The minimum returned page size is 4. */

    /* Reasonable max. number of page steps is 32: */
    uint uPage = ((uint)iMaximum + 31) / 32;
    /* Make it a power of 2: */
    uint p = uPage, p2 = 0x1;
    while ((p >>= 1))
        p2 <<= 1;
    if (uPage != p2)
        p2 <<= 1;
    if (p2 < 4)
        p2 = 4;
    return (int) p2;
}


/*********************************************************************************************************************************
*   Class UIBaseMemoryEditor implementation.                                                                                     *
*********************************************************************************************************************************/

UIBaseMemoryEditor::UIBaseMemoryEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_iValue(0)
    , m_pLayout(0)
    , m_pLabelMemory(0)
    , m_pSlider(0)
    , m_pLabelMemoryMin(0)
    , m_pLabelMemoryMax(0)
    , m_pSpinBox(0)
{
    prepare();
}

void UIBaseMemoryEditor::setValue(int iValue)
{
    /* Update cached value and
     * slider if value has changed: */
    if (m_iValue != iValue)
    {
        m_iValue = iValue;
        if (m_pSlider)
            m_pSlider->setValue(m_iValue);
    }
}

int UIBaseMemoryEditor::value() const
{
    return m_pSlider ? m_pSlider->value() : m_iValue;
}

uint UIBaseMemoryEditor::maxRAMOpt() const
{
    return m_pSlider ? m_pSlider->maxRAMOpt() : 0;
}

uint UIBaseMemoryEditor::maxRAMAlw() const
{
    return m_pSlider ? m_pSlider->maxRAMAlw() : 0;
}

int UIBaseMemoryEditor::minimumLabelHorizontalHint() const
{
    return m_pLabelMemory ? m_pLabelMemory->minimumSizeHint().width() : 0;
}

void UIBaseMemoryEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIBaseMemoryEditor::sltRetranslateUI()
{
    if (m_pLabelMemory)
        m_pLabelMemory->setText(tr("Base &Memory"));

    const QString strToolTip(tr("VM's installed RAM"));
    if (m_pSlider)
        m_pSlider->setToolTip(strToolTip);
    if (m_pSpinBox)
    {
        m_pSpinBox->setSuffix(QString(" %1").arg(tr("MB")));
        m_pSpinBox->setToolTip(strToolTip);
    }

    if (m_pLabelMemoryMin)
    {
        m_pLabelMemoryMin->setText(tr("%1 MB").arg(m_pSlider->minRAM()));
        m_pLabelMemoryMin->setToolTip(tr("Minimum RAM"));
    }
    if (m_pLabelMemoryMax)
    {
        m_pLabelMemoryMax->setText(tr("%1 MB").arg(m_pSlider->maxRAM()));
        m_pLabelMemoryMax->setToolTip(tr("Maximum RAM"));
    }
}

void UIBaseMemoryEditor::sltHandleSliderChange()
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

void UIBaseMemoryEditor::sltHandleSpinBoxChange()
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

void UIBaseMemoryEditor::prepare()
{
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
            m_pSlider = new UIBaseMemorySlider(this);
            if (m_pSlider)
            {
                m_pSlider->setMinimumWidth(150);
                connect(m_pSlider, &UIBaseMemorySlider::valueChanged,
                        this, &UIBaseMemoryEditor::sltHandleSliderChange);
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
            m_pSpinBox->setMinimum(m_pSlider->minRAM());
            m_pSpinBox->setMaximum(m_pSlider->maxRAM());
            connect(m_pSpinBox, &QSpinBox::valueChanged,
                    this, &UIBaseMemoryEditor::sltHandleSpinBoxChange);
            m_pLayout->addWidget(m_pSpinBox, 0, 2);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIBaseMemoryEditor::revalidate()
{
    if (m_pSlider)
    {
        emit sigValidChanged(m_pSlider->value() < (int)m_pSlider->maxRAMAlw());
        emit sigValueChanged(m_pSlider->value());
    }
}


#include "UIBaseMemoryEditor.moc"
