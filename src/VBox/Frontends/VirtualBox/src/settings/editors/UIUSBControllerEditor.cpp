/* $Id: UIUSBControllerEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBControllerEditor class implementation.
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
#include <QButtonGroup>
#include <QRadioButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIGlobalSession.h"
#include "UIUSBControllerEditor.h"

/* COM includes: */
#include "CPlatformProperties.h"


UIUSBControllerEditor::UIUSBControllerEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_enmValue(KUSBControllerType_Max)
    , m_pRadioButtonUSB1(0)
    , m_pRadioButtonUSB2(0)
    , m_pRadioButtonUSB3(0)
{
    prepare();
}

void UIUSBControllerEditor::setValue(KUSBControllerType enmValue)
{
    /* Update cached value and
     * combo if value has changed: */
    if (m_enmValue != enmValue)
    {
        m_enmValue = enmValue;
        updateButtonSet();
    }
}

KUSBControllerType UIUSBControllerEditor::value() const
{
    if (   m_pRadioButtonUSB1
        && m_pRadioButtonUSB1->isChecked())
        return KUSBControllerType_OHCI;
    else if (   m_pRadioButtonUSB2
             && m_pRadioButtonUSB2->isChecked())
        return KUSBControllerType_EHCI;
    else if (   m_pRadioButtonUSB3
             && m_pRadioButtonUSB3->isChecked())
        return KUSBControllerType_XHCI;
    return m_enmValue;
}

void UIUSBControllerEditor::sltRetranslateUI()
{
    if (m_pRadioButtonUSB1)
    {
        m_pRadioButtonUSB1->setText(tr("USB &1.1 (OHCI) Controller"));
        m_pRadioButtonUSB1->setToolTip(tr("When chosen, enables the virtual USB OHCI controller of "
                                          "this machine. The USB OHCI controller provides USB 1.0 support."));
    }
    if (m_pRadioButtonUSB2)
    {
        m_pRadioButtonUSB2->setText(tr("USB &2.0 (OHCI + EHCI) Controller"));
        m_pRadioButtonUSB2->setToolTip(tr("When chosen, enables the virtual USB OHCI and EHCI "
                                          "controllers of this machine. Together they provide USB 2.0 support."));
    }
    if (m_pRadioButtonUSB3)
    {
        m_pRadioButtonUSB3->setText(tr("USB &3.0 (xHCI) Controller"));
        m_pRadioButtonUSB3->setToolTip(tr("When chosen, enables the virtual USB xHCI controller of "
                                          "this machine. The USB xHCI controller provides USB 3.0 support."));
    }
}

void UIUSBControllerEditor::handleFilterChange()
{
    updateButtonSet();
}

void UIUSBControllerEditor::prepare()
{
    /* Create main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare button-group: */
        QButtonGroup *pButtonGroup = new QButtonGroup(this);
        if (pButtonGroup)
        {
            /* Prepare USB1 radio-button: */
            m_pRadioButtonUSB1 = new QRadioButton(this);
            if (m_pRadioButtonUSB1)
            {
                m_pRadioButtonUSB1->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
                m_pRadioButtonUSB1->setVisible(false);
                pLayout->addWidget(m_pRadioButtonUSB1);
            }
            /* Prepare USB2 radio-button: */
            m_pRadioButtonUSB2 = new QRadioButton(this);
            if (m_pRadioButtonUSB2)
            {
                m_pRadioButtonUSB2->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
                m_pRadioButtonUSB2->setVisible(false);
                pLayout->addWidget(m_pRadioButtonUSB2);
            }
            /* Prepare USB3 radio-button: */
            m_pRadioButtonUSB3 = new QRadioButton(this);
            if (m_pRadioButtonUSB3)
            {
                m_pRadioButtonUSB3->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
                m_pRadioButtonUSB3->setVisible(false);
                pLayout->addWidget(m_pRadioButtonUSB3);
            }

            connect(pButtonGroup, &QButtonGroup::buttonClicked,
                    this, &UIUSBControllerEditor::sigValueChanged);
        }
    }

    /* Update button set: */
    updateButtonSet();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIUSBControllerEditor::updateButtonSet()
{
    /* Load currently supported types: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_x86;
    CPlatformProperties comProperties = gpGlobalSession->virtualBox().GetPlatformProperties(enmArch);
    m_supportedValues = comProperties.GetSupportedUSBControllerTypes();

    /* Make sure requested value if sane is present as well: */
    if (   m_enmValue != KUSBControllerType_Max
        && !m_supportedValues.contains(m_enmValue))
        m_supportedValues.prepend(m_enmValue);

    /* Update visibility for all values: */
    if (m_pRadioButtonUSB1)
        m_pRadioButtonUSB1->setVisible(m_supportedValues.contains(KUSBControllerType_OHCI));
    if (m_pRadioButtonUSB2)
        m_pRadioButtonUSB2->setVisible(m_supportedValues.contains(KUSBControllerType_EHCI));
    if (m_pRadioButtonUSB3)
        m_pRadioButtonUSB3->setVisible(m_supportedValues.contains(KUSBControllerType_XHCI));

    /* Look for proper button to choose: */
    switch (m_enmValue)
    {
        default:
        case KUSBControllerType_OHCI:
        {
            if (m_pRadioButtonUSB1)
                m_pRadioButtonUSB1->setChecked(true);
            break;
        }
        case KUSBControllerType_EHCI:
        {
            if (m_pRadioButtonUSB2)
                m_pRadioButtonUSB2->setChecked(true);
            break;
        }
        case KUSBControllerType_XHCI:
        {
            if (m_pRadioButtonUSB3)
                m_pRadioButtonUSB3->setChecked(true);
            break;
        }
    }
}
