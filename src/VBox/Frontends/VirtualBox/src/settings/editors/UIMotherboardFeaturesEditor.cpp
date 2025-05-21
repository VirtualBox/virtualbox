/* $Id: UIMotherboardFeaturesEditor.cpp 168951 2025-05-21 10:21:17Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMotherboardFeaturesEditor class implementation.
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
#include <QPushButton>

/* GUI includes: */
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UIMotherboardFeaturesEditor.h"

/* COM includes: */
#include "KPlatformArchitecture.h"


UIMotherboardFeaturesEditor::UIMotherboardFeaturesEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fEnableIoApic(false)
    , m_fEnableUtcTime(false)
    , m_fEnableEfi(false)
    , m_fEnableSecureBoot(false)
    , m_pLabel(0)
    , m_pCheckBoxEnableIoApic(0)
    , m_pCheckBoxEnableUtcTime(0)
    , m_pCheckBoxEnableEfi(0)
    , m_pCheckBoxEnableSecureBoot(0)
    , m_pPushButtonResetSecureBoot(0)
{
    prepare();
}

void UIMotherboardFeaturesEditor::setEnableIoApic(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableIoApic != fOn)
    {
        m_fEnableIoApic = fOn;
        if (m_pCheckBoxEnableIoApic)
            m_pCheckBoxEnableIoApic->setCheckState(m_fEnableIoApic ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIMotherboardFeaturesEditor::isEnabledIoApic() const
{
    return   m_pCheckBoxEnableIoApic
           ? m_pCheckBoxEnableIoApic->checkState() == Qt::Checked
           : m_fEnableIoApic;
}

void UIMotherboardFeaturesEditor::setEnableUtcTime(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableUtcTime != fOn)
    {
        m_fEnableUtcTime = fOn;
        if (m_pCheckBoxEnableUtcTime)
            m_pCheckBoxEnableUtcTime->setCheckState(m_fEnableUtcTime ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIMotherboardFeaturesEditor::isEnabledUtcTime() const
{
    return   m_pCheckBoxEnableUtcTime
           ? m_pCheckBoxEnableUtcTime->checkState() == Qt::Checked
           : m_fEnableUtcTime;
}

void UIMotherboardFeaturesEditor::setEnableEfi(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableEfi != fOn)
    {
        m_fEnableEfi = fOn;
        if (m_pCheckBoxEnableEfi)
            m_pCheckBoxEnableEfi->setCheckState(m_fEnableEfi ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIMotherboardFeaturesEditor::isEnabledEfi() const
{
    return   m_pCheckBoxEnableEfi
           ? m_pCheckBoxEnableEfi->checkState() == Qt::Checked
           : m_fEnableEfi;
}

void UIMotherboardFeaturesEditor::setEnableSecureBoot(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fEnableSecureBoot != fOn)
    {
        m_fEnableSecureBoot = fOn;
        if (m_pCheckBoxEnableSecureBoot)
            m_pCheckBoxEnableSecureBoot->setCheckState(m_fEnableSecureBoot ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIMotherboardFeaturesEditor::isEnabledSecureBoot() const
{
    return   m_pCheckBoxEnableSecureBoot
           ? m_pCheckBoxEnableSecureBoot->checkState() == Qt::Checked
           : m_fEnableSecureBoot;
}

bool UIMotherboardFeaturesEditor::isResetSecureBoot() const
{
    return   m_pPushButtonResetSecureBoot
           ? m_pPushButtonResetSecureBoot->property("clicked_once").toBool()
           : false;
}

int UIMotherboardFeaturesEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIMotherboardFeaturesEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIMotherboardFeaturesEditor::handleFilterChange()
{
    /* Some options hidden for ARM machines: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_x86;
    const bool fARMMachine = enmArch == KPlatformArchitecture_ARM;
    if (fARMMachine)
    {
        if (m_pCheckBoxEnableIoApic)
            m_pCheckBoxEnableIoApic->hide();
        rebuildLayout();
    }
}

void UIMotherboardFeaturesEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Use These Features"));
    if (m_pCheckBoxEnableIoApic)
    {
        m_pCheckBoxEnableIoApic->setText(tr("&I/O APIC"));
        m_pCheckBoxEnableIoApic->setToolTip(tr("Use I/O APIC. Performance may be slower."));
    }
    if (m_pCheckBoxEnableUtcTime)
    {
        m_pCheckBoxEnableUtcTime->setText(tr("Hardware Clock in &UTC"));
        m_pCheckBoxEnableUtcTime->setToolTip(tr("Emulated RTC device reports time in UTC "
                                                "rather than local time on the host"));
    }
    if (m_pCheckBoxEnableEfi)
    {
        m_pCheckBoxEnableEfi->setText(tr("U&EFI"));
        m_pCheckBoxEnableEfi->setToolTip(tr("VM uses UEFI to boot OS instead of BIOS"));
    }
    if (m_pCheckBoxEnableSecureBoot)
    {
        m_pCheckBoxEnableSecureBoot->setText(tr("&Secure Boot"));
        m_pCheckBoxEnableSecureBoot->setToolTip(tr("Use secure boot emulation"));
    }
    if (m_pPushButtonResetSecureBoot)
    {
        m_pPushButtonResetSecureBoot->setText(tr("&Reset Secure Boot Keys"));
        m_pPushButtonResetSecureBoot->setToolTip(tr("Resets secure boot keys to default"));
    }
}

void UIMotherboardFeaturesEditor::sltHandleEnableEfiToggling()
{
    /* Acquire actual feature state: */
    const bool fOn = m_pCheckBoxEnableEfi
                   ? m_pCheckBoxEnableEfi->isChecked()
                   : false;

    /* Update corresponding controls: */
    if (m_pCheckBoxEnableSecureBoot)
        m_pCheckBoxEnableSecureBoot->setEnabled(fOn);

    /* Notify listeners: */
    emit sigChangedEfi();
    sltHandleEnableSecureBootToggling();
}

void UIMotherboardFeaturesEditor::sltHandleEnableSecureBootToggling()
{
    /* Acquire actual feature state: */
    const bool fOn =    m_pCheckBoxEnableEfi
                     && m_pCheckBoxEnableSecureBoot
                     && m_pPushButtonResetSecureBoot
                   ?    m_pCheckBoxEnableEfi->isChecked()
                     && m_pCheckBoxEnableSecureBoot->isChecked()
                     && !m_pPushButtonResetSecureBoot->property("clicked_once").toBool()
                   : false;

    /* Update corresponding controls: */
    if (m_pPushButtonResetSecureBoot)
        m_pPushButtonResetSecureBoot->setEnabled(fOn);

    /* Notify listeners: */
    emit sigChangedSecureBoot();
}

void UIMotherboardFeaturesEditor::sltResetSecureBoot()
{
    if (!m_pPushButtonResetSecureBoot->property("clicked_once").toBool())
    {
        if (msgCenter().confirmRestoringDefaultKeys())
        {
            m_pPushButtonResetSecureBoot->setProperty("clicked_once", true);
            sltHandleEnableSecureBootToggling();
        }
    }
}

void UIMotherboardFeaturesEditor::prepare()
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
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        /* Prepare 'enable IO APIC' check-box: */
        m_pCheckBoxEnableIoApic = new QCheckBox(this);
        if (m_pCheckBoxEnableIoApic)
            connect(m_pCheckBoxEnableIoApic, &QCheckBox::stateChanged,
                    this, &UIMotherboardFeaturesEditor::sigChangedIoApic);
        /* Prepare 'enable UTC time' check-box: */
        m_pCheckBoxEnableUtcTime = new QCheckBox(this);
        if (m_pCheckBoxEnableUtcTime)
            connect(m_pCheckBoxEnableUtcTime, &QCheckBox::stateChanged,
                    this, &UIMotherboardFeaturesEditor::sigChangedUtcTime);
        /* Prepare 'enable EFI' check-box: */
        m_pCheckBoxEnableEfi = new QCheckBox(this);
        if (m_pCheckBoxEnableEfi)
            connect(m_pCheckBoxEnableEfi, &QCheckBox::stateChanged,
                    this, &UIMotherboardFeaturesEditor::sltHandleEnableEfiToggling);
        /* Prepare 'enable secure boot' check-box: */
        m_pCheckBoxEnableSecureBoot = new QCheckBox(this);
        if (m_pCheckBoxEnableSecureBoot)
            connect(m_pCheckBoxEnableSecureBoot, &QCheckBox::stateChanged,
                    this, &UIMotherboardFeaturesEditor::sltHandleEnableSecureBootToggling);
        /* Prepare 'reset secure boot' tool-button: */
        m_pPushButtonResetSecureBoot = new QPushButton(this);
        if (m_pPushButtonResetSecureBoot)
        {
            m_pPushButtonResetSecureBoot->setIcon(UIIconPool::iconSet(":/refresh_16px.png"));
            connect(m_pPushButtonResetSecureBoot, &QPushButton::clicked,
                    this, &UIMotherboardFeaturesEditor::sltResetSecureBoot);
        }

        rebuildLayout();
    }

    /* Fetch states: */
    sltHandleEnableEfiToggling();
    sltHandleEnableSecureBootToggling();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMotherboardFeaturesEditor::rebuildLayout()
{
    if (m_pLayout)
    {
        /* Remove all the widgets from the layout if any: */
        m_pLayout->removeWidget(m_pLabel);
        m_pLayout->removeWidget(m_pCheckBoxEnableIoApic);
        m_pLayout->removeWidget(m_pCheckBoxEnableUtcTime);
        m_pLayout->removeWidget(m_pCheckBoxEnableEfi);
        m_pLayout->removeWidget(m_pCheckBoxEnableSecureBoot);
        m_pLayout->removeWidget(m_pPushButtonResetSecureBoot);

        /* Put them back only if they are visible: */
        int i = 0;
        if (m_pLabel)
            m_pLayout->addWidget(m_pLabel, i, 0);
        if (m_pCheckBoxEnableIoApic && !m_pCheckBoxEnableIoApic->isHidden())
            m_pLayout->addWidget(m_pCheckBoxEnableIoApic, i++, 1);
        if (m_pCheckBoxEnableUtcTime && !m_pCheckBoxEnableUtcTime->isHidden())
            m_pLayout->addWidget(m_pCheckBoxEnableUtcTime, i++, 1);
        if (m_pCheckBoxEnableEfi && !m_pCheckBoxEnableEfi->isHidden())
            m_pLayout->addWidget(m_pCheckBoxEnableEfi, i++, 1);
        if (m_pCheckBoxEnableSecureBoot && !m_pCheckBoxEnableSecureBoot->isHidden())
            m_pLayout->addWidget(m_pCheckBoxEnableSecureBoot, i++, 1);
        if (m_pPushButtonResetSecureBoot && !m_pPushButtonResetSecureBoot->isHidden())
            m_pLayout->addWidget(m_pPushButtonResetSecureBoot, i++, 1);
    }
}
