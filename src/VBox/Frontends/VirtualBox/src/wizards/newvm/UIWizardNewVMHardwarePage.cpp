/* $Id: UIWizardNewVMHardwarePage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMHardwarePage class implementation.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIBaseMemoryEditor.h"
#include "UIGlobalSession.h"
#include "UIGuestOSType.h"
#include "UIVirtualCPUEditor.h"
#include "UIWizardNewVM.h"
#include "UIWizardNewVMEditors.h"
#include "UIWizardNewVMHardwarePage.h"


UIWizardNewVMHardwarePage::UIWizardNewVMHardwarePage()
    : m_pLabel(0)
    , m_pHardwareWidgetContainer(0)
{
    prepare();
    qRegisterMetaType<CMedium>();
}

void UIWizardNewVMHardwarePage::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);

    m_pLabel = new QIRichTextLabel(this);
    pMainLayout->addWidget(m_pLabel);
    m_pHardwareWidgetContainer = new UINewVMHardwareContainer;
    AssertReturnVoid(m_pHardwareWidgetContainer);
    pMainLayout->addWidget(m_pHardwareWidgetContainer);

    pMainLayout->addStretch();
    createConnections();
}

void UIWizardNewVMHardwarePage::createConnections()
{
    if (m_pHardwareWidgetContainer)
    {
        connect(m_pHardwareWidgetContainer, &UINewVMHardwareContainer::sigMemorySizeChanged,
                this, &UIWizardNewVMHardwarePage::sltMemorySizeChanged);
        connect(m_pHardwareWidgetContainer, &UINewVMHardwareContainer::sigCPUCountChanged,
                this, &UIWizardNewVMHardwarePage::sltCPUCountChanged);
        connect(m_pHardwareWidgetContainer, &UINewVMHardwareContainer::sigEFIEnabledChanged,
                this, &UIWizardNewVMHardwarePage::sltEFIEnabledChanged);
    }
}

void UIWizardNewVMHardwarePage::sltRetranslateUI()
{
    setTitle(UIWizardNewVM::tr("Hardware"));

    if (m_pLabel)
        m_pLabel->setText(UIWizardNewVM::tr("You can modify virtual machine's hardware by changing amount of RAM and "
                                            "virtual CPU count. Enabling EFI is also possible."));
}

void UIWizardNewVMHardwarePage::initializePage()
{
    sltRetranslateUI();

    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    if (pWizard && m_pHardwareWidgetContainer)
    {
        const QString &strTypeId = pWizard->guestOSTypeId();

        m_pHardwareWidgetContainer->blockSignals(true);
        if (!m_userModifiedParameters.contains("MemorySize"))
        {
            ULONG recommendedRam = gpGlobalSession->guestOSTypeManager().getRecommendedRAM(strTypeId);
            m_pHardwareWidgetContainer->setMemorySize(recommendedRam);
            pWizard->setMemorySize(recommendedRam);
        }
        if (!m_userModifiedParameters.contains("CPUCount"))
        {
            ULONG recommendedCPUs = gpGlobalSession->guestOSTypeManager().getRecommendedCPUCount(strTypeId);
            m_pHardwareWidgetContainer->setCPUCount(recommendedCPUs);
            pWizard->setCPUCount(recommendedCPUs);
        }
        if (!m_userModifiedParameters.contains("EFIEnabled"))
        {
            KFirmwareType fwType = gpGlobalSession->guestOSTypeManager().getRecommendedFirmware(strTypeId);
            m_pHardwareWidgetContainer->setEFIEnabled(fwType != KFirmwareType_BIOS);
            pWizard->setEFIEnabled(fwType != KFirmwareType_BIOS);
        }
        m_pHardwareWidgetContainer->blockSignals(false);
    }
}

bool UIWizardNewVMHardwarePage::isComplete() const
{
    return true;
}

void UIWizardNewVMHardwarePage::sltMemorySizeChanged(int iValue)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setMemorySize(iValue);
    m_userModifiedParameters << "MemorySize";
}

void UIWizardNewVMHardwarePage::sltCPUCountChanged(int iCount)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setCPUCount(iCount);
    m_userModifiedParameters << "CPUCount";
}

void UIWizardNewVMHardwarePage::sltEFIEnabledChanged(bool fEnabled)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setEFIEnabled(fEnabled);
    m_userModifiedParameters << "EFIEnabled";
}
