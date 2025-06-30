/* $Id: UIWizardCloneVMExpertPage.cpp 110043 2025-06-30 10:09:23Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardCloneVMExpertPage class implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>

/* GUI includes: */
#include "QILineEdit.h"
#include "UIFilePathSelector.h"
#include "UIWizardCloneVMExpertPage.h"
#include "UIWizardCloneVM.h"
#include "UIWizardCloneVMNamePathPage.h"

/* COM includes: */
#include "CSystemProperties.h"


UIWizardCloneVMExpertPage::UIWizardCloneVMExpertPage(const QString &strOriginalName, const QString &strDefaultPath,
                                                     bool /*fAdditionalInfo*/, bool fShowChildsOption, const QString &strGroup)
    : m_pNamePathGroupBox(0)
    , m_pCloneTypeGroupBox(0)
    , m_pCloneModeGroupBox(0)
    , m_pAdditionalOptionsGroupBox(0)
    , m_strGroup(strGroup)
{
    prepare(strOriginalName, strDefaultPath, fShowChildsOption);
}

void UIWizardCloneVMExpertPage::prepare(const QString &strOriginalName, const QString &strDefaultPath, bool fShowChildsOption)
{
    QGridLayout *pMainLayout = new QGridLayout(this);
    AssertReturnVoid(pMainLayout);
    m_pNamePathGroupBox = new UICloneVMNamePathEditor(strOriginalName, strDefaultPath);
    if (m_pNamePathGroupBox)
    {
        pMainLayout->addWidget(m_pNamePathGroupBox, 0, 0, 3, 2);
        connect(m_pNamePathGroupBox, &UICloneVMNamePathEditor::sigCloneNameChanged,
                this, &UIWizardCloneVMExpertPage::sltCloneNameChanged);
        connect(m_pNamePathGroupBox, &UICloneVMNamePathEditor::sigClonePathChanged,
                this, &UIWizardCloneVMExpertPage::sltClonePathChanged);
    }

    m_pCloneTypeGroupBox = new UICloneVMCloneTypeGroupBox;
    if (m_pCloneTypeGroupBox)
        pMainLayout->addWidget(m_pCloneTypeGroupBox, 3, 0, 2, 1);

    m_pCloneModeGroupBox = new UICloneVMCloneModeGroupBox(fShowChildsOption);
    if (m_pCloneModeGroupBox)
        pMainLayout->addWidget(m_pCloneModeGroupBox, 3, 1, 2, 1);

    m_pAdditionalOptionsGroupBox = new UICloneVMAdditionalOptionsEditor;
    if (m_pAdditionalOptionsGroupBox)
    {
        pMainLayout->addWidget(m_pAdditionalOptionsGroupBox, 5, 0, 2, 2);
        connect(m_pAdditionalOptionsGroupBox, &UICloneVMAdditionalOptionsEditor::sigMACAddressClonePolicyChanged,
                this, &UIWizardCloneVMExpertPage::sltMACAddressClonePolicyChanged);
        connect(m_pAdditionalOptionsGroupBox, &UICloneVMAdditionalOptionsEditor::sigKeepDiskNamesToggled,
                this, &UIWizardCloneVMExpertPage::sltKeepDiskNamesToggled);
        connect(m_pAdditionalOptionsGroupBox, &UICloneVMAdditionalOptionsEditor::sigKeepHardwareUUIDsToggled,
                this, &UIWizardCloneVMExpertPage::sltKeepHardwareUUIDsToggled);
    }
    if (m_pCloneTypeGroupBox)
        connect(m_pCloneTypeGroupBox, &UICloneVMCloneTypeGroupBox::sigFullCloneSelected,
                this, &UIWizardCloneVMExpertPage::sltCloneTypeChanged);
    if (m_pCloneModeGroupBox)
        connect(m_pCloneModeGroupBox, &UICloneVMCloneModeGroupBox::sigCloneModeChanged,
                this, &UIWizardCloneVMExpertPage::sltCloneModeChanged);
    sltRetranslateUI();
}

void UIWizardCloneVMExpertPage::sltRetranslateUI()
{
    /* Translate widgets: */
    if (m_pNamePathGroupBox)
        m_pNamePathGroupBox->setTitle(UIWizardCloneVM::tr("New Machine Name and Path"));
    if (m_pCloneTypeGroupBox)
        m_pCloneTypeGroupBox->setTitle(UIWizardCloneVM::tr("Clone Type"));
    if (m_pCloneModeGroupBox)
        m_pCloneModeGroupBox->setTitle(UIWizardCloneVM::tr("Snapshots"));
    if (m_pAdditionalOptionsGroupBox)
        m_pAdditionalOptionsGroupBox->setTitle(UIWizardCloneVM::tr("Additional Options"));
}

void UIWizardCloneVMExpertPage::initializePage()
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);
    if (m_pNamePathGroupBox)
    {
        m_pNamePathGroupBox->setFocus();
        pWizard->setCloneName(m_pNamePathGroupBox->cloneName());
        pWizard->setCloneFilePath(
                                 UIWizardCloneVMNamePathCommon::composeCloneFilePath(m_pNamePathGroupBox->cloneName(), m_strGroup, m_pNamePathGroupBox->clonePath()));
    }
    if (m_pAdditionalOptionsGroupBox)
    {
        pWizard->setMacAddressPolicy(m_pAdditionalOptionsGroupBox->macAddressClonePolicy());
        pWizard->setKeepDiskNames(m_pAdditionalOptionsGroupBox->keepDiskNames());
        pWizard->setKeepHardwareUUIDs(m_pAdditionalOptionsGroupBox->keepHardwareUUIDs());
    }
    if (m_pCloneTypeGroupBox)
        pWizard->setLinkedClone(!m_pCloneTypeGroupBox->isFullClone());
    if (m_pCloneModeGroupBox)
        pWizard->setCloneMode(m_pCloneModeGroupBox->cloneMode());

    setCloneModeGroupBoxEnabled();

    sltRetranslateUI();
}

void UIWizardCloneVMExpertPage::setCloneModeGroupBoxEnabled()
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);

    if (m_pCloneModeGroupBox)
        m_pCloneModeGroupBox->setEnabled(pWizard->machineHasSnapshot() && !pWizard->linkedClone());
}

bool UIWizardCloneVMExpertPage::isComplete() const
{
    return m_pNamePathGroupBox && m_pNamePathGroupBox->isComplete(m_strGroup);
}

bool UIWizardCloneVMExpertPage::validatePage()
{
    AssertReturn(wizardWindow<UIWizardCloneVM>(), false);
    return wizardWindow<UIWizardCloneVM>()->cloneVM();
}

void UIWizardCloneVMExpertPage::sltCloneNameChanged(const QString &strCloneName)
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);
    AssertReturnVoid(m_pNamePathGroupBox);
    pWizard->setCloneName(strCloneName);
    pWizard->setCloneFilePath(
                             UIWizardCloneVMNamePathCommon::composeCloneFilePath(strCloneName, m_strGroup, m_pNamePathGroupBox->clonePath()));
    emit completeChanged();
}

void UIWizardCloneVMExpertPage::sltClonePathChanged(const QString &strClonePath)
{
    AssertReturnVoid(m_pNamePathGroupBox && wizardWindow<UIWizardCloneVM>());
    wizardWindow<UIWizardCloneVM>()->setCloneFilePath(
                             UIWizardCloneVMNamePathCommon::composeCloneFilePath(m_pNamePathGroupBox->cloneName(), m_strGroup, strClonePath));
    emit completeChanged();
}

void UIWizardCloneVMExpertPage::sltMACAddressClonePolicyChanged(MACAddressClonePolicy enmMACAddressClonePolicy)
{
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    wizardWindow<UIWizardCloneVM>()->setMacAddressPolicy(enmMACAddressClonePolicy);
}

void UIWizardCloneVMExpertPage::sltKeepDiskNamesToggled(bool fKeepDiskNames)
{
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    wizardWindow<UIWizardCloneVM>()->setKeepDiskNames(fKeepDiskNames);
}

void UIWizardCloneVMExpertPage::sltKeepHardwareUUIDsToggled(bool fKeepHardwareUUIDs)
{
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    wizardWindow<UIWizardCloneVM>()->setKeepHardwareUUIDs(fKeepHardwareUUIDs);
}

void UIWizardCloneVMExpertPage::sltCloneTypeChanged(bool fIsFullClone)
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);
    pWizard->setLinkedClone(!fIsFullClone);
    setCloneModeGroupBoxEnabled();
    emit completeChanged();
}

void UIWizardCloneVMExpertPage::sltCloneModeChanged(KCloneMode enmCloneMode)
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);
    pWizard->setCloneMode(enmCloneMode);
    emit completeChanged();
}
