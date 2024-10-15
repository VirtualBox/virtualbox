/* $Id: UIWizardCloneVMNamePathPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardCloneVMNamePathPage class implementation.
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
#include <QDir>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIGlobalSession.h"
#include "UIWizardCloneVM.h"
#include "UIWizardCloneVMNamePathPage.h"

/* COM includes: */
#include "CVirtualBox.h"

QString UIWizardCloneVMNamePathCommon::composeCloneFilePath(const QString &strCloneName, const QString &strGroup, const QString &strFolderPath)
{
    CVirtualBox vbox = gpGlobalSession->virtualBox();
    return QDir::toNativeSeparators(vbox.ComposeMachineFilename(strCloneName, strGroup, QString(), strFolderPath));
}

UIWizardCloneVMNamePathPage::UIWizardCloneVMNamePathPage(const QString &strOriginalName, const QString &strDefaultPath, const QString &strGroup)
    : m_pNamePathEditor(0)
    , m_pAdditionalOptionsEditor(0)
    , m_strOriginalName(strOriginalName)
    , m_strGroup(strGroup)
{
    prepare(strDefaultPath);
}

void UIWizardCloneVMNamePathPage::sltRetranslateUI()
{
    setTitle(UIWizardCloneVM::tr("New machine name and path"));

    if (m_pMainLabel)
        m_pMainLabel->setText(UIWizardCloneVM::tr("<p>Please choose a name and optionally a folder for the new virtual machine. "
                                                  "The new machine will be a clone of the machine <b>%1</b>.</p>")
                              .arg(m_strOriginalName));

    int iMaxWidth = 0;
    if (m_pNamePathEditor)
        iMaxWidth = qMax(iMaxWidth, m_pNamePathEditor->firstColumnWidth());
    if (m_pAdditionalOptionsEditor)
        iMaxWidth = qMax(iMaxWidth, m_pAdditionalOptionsEditor->firstColumnWidth());

    if (m_pNamePathEditor)
        m_pNamePathEditor->setFirstColumnWidth(iMaxWidth);
    if (m_pAdditionalOptionsEditor)
        m_pAdditionalOptionsEditor->setFirstColumnWidth(iMaxWidth);
}

void UIWizardCloneVMNamePathPage::initializePage()
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);
    sltRetranslateUI();
    if (m_pNamePathEditor)
    {
        m_pNamePathEditor->setFocus();
        if (!m_userModifiedParameters.contains("CloneName"))
            pWizard->setCloneName(m_pNamePathEditor->cloneName());
        if (!m_userModifiedParameters.contains("CloneFilePath"))
            pWizard->setCloneFilePath(UIWizardCloneVMNamePathCommon::composeCloneFilePath(m_pNamePathEditor->cloneName(),
                                                                                        m_strGroup, m_pNamePathEditor->clonePath()));

    }
    if (m_pAdditionalOptionsEditor)
    {
        if (!m_userModifiedParameters.contains("MacAddressPolicy"))
            pWizard->setMacAddressPolicy(m_pAdditionalOptionsEditor->macAddressClonePolicy());
        if (!m_userModifiedParameters.contains("KeepDiskNames"))
            pWizard->setKeepDiskNames(m_pAdditionalOptionsEditor->keepDiskNames());
        if (!m_userModifiedParameters.contains("KeepHardwareUUIDs"))
            pWizard->setKeepHardwareUUIDs(m_pAdditionalOptionsEditor->keepHardwareUUIDs());
    }
}

void UIWizardCloneVMNamePathPage::prepare(const QString &strDefaultClonePath)
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);

    AssertReturnVoid(pMainLayout);

    m_pMainLabel = new QIRichTextLabel(this);
    if (m_pMainLabel)
        pMainLayout->addWidget(m_pMainLabel);

    m_pNamePathEditor = new UICloneVMNamePathEditor(m_strOriginalName, strDefaultClonePath);
    if (m_pNamePathEditor)
    {
        m_pNamePathEditor->setFlat(true);
        m_pNamePathEditor->setLayoutContentsMargins(0, 0, 0, 0);
        pMainLayout->addWidget(m_pNamePathEditor);
        connect(m_pNamePathEditor, &UICloneVMNamePathEditor::sigCloneNameChanged,
                this, &UIWizardCloneVMNamePathPage::sltCloneNameChanged);
        connect(m_pNamePathEditor, &UICloneVMNamePathEditor::sigClonePathChanged,
                this, &UIWizardCloneVMNamePathPage::sltClonePathChanged);
    }

    m_pAdditionalOptionsEditor = new UICloneVMAdditionalOptionsEditor;
    if (m_pAdditionalOptionsEditor)
    {
        m_pAdditionalOptionsEditor->setFlat(true);
        pMainLayout->addWidget(m_pAdditionalOptionsEditor);
        connect(m_pAdditionalOptionsEditor, &UICloneVMAdditionalOptionsEditor::sigMACAddressClonePolicyChanged,
                this, &UIWizardCloneVMNamePathPage::sltMACAddressClonePolicyChanged);
        connect(m_pAdditionalOptionsEditor, &UICloneVMAdditionalOptionsEditor::sigKeepDiskNamesToggled,
                this, &UIWizardCloneVMNamePathPage::sltKeepDiskNamesToggled);
        connect(m_pAdditionalOptionsEditor, &UICloneVMAdditionalOptionsEditor::sigKeepHardwareUUIDsToggled,
                this, &UIWizardCloneVMNamePathPage::sltKeepHardwareUUIDsToggled);
    }

    pMainLayout->addStretch();

    sltRetranslateUI();
}

bool UIWizardCloneVMNamePathPage::isComplete() const
{
    return m_pNamePathEditor && m_pNamePathEditor->isComplete(m_strGroup);
}

void UIWizardCloneVMNamePathPage::sltCloneNameChanged(const QString &strCloneName)
{
    UIWizardCloneVM *pWizard = wizardWindow<UIWizardCloneVM>();
    AssertReturnVoid(pWizard);
    AssertReturnVoid(m_pNamePathEditor);
    m_userModifiedParameters << "CloneName";
    m_userModifiedParameters << "CloneFilePath";
    pWizard->setCloneName(strCloneName);
    pWizard->setCloneFilePath(UIWizardCloneVMNamePathCommon::composeCloneFilePath(strCloneName, m_strGroup, m_pNamePathEditor->clonePath()));
    emit completeChanged();
}

void UIWizardCloneVMNamePathPage::sltClonePathChanged(const QString &strClonePath)
{
    AssertReturnVoid(m_pNamePathEditor);
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    m_userModifiedParameters << "CloneFilePath";
    wizardWindow<UIWizardCloneVM>()->setCloneFilePath(UIWizardCloneVMNamePathCommon::composeCloneFilePath(m_pNamePathEditor->cloneName(), m_strGroup, strClonePath));
    emit completeChanged();
}

void UIWizardCloneVMNamePathPage::sltMACAddressClonePolicyChanged(MACAddressClonePolicy enmMACAddressClonePolicy)
{
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    m_userModifiedParameters << "MacAddressPolicy";
    wizardWindow<UIWizardCloneVM>()->setMacAddressPolicy(enmMACAddressClonePolicy);
    emit completeChanged();
}

void UIWizardCloneVMNamePathPage::sltKeepDiskNamesToggled(bool fKeepDiskNames)
{
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    m_userModifiedParameters << "KeepDiskNames";
    wizardWindow<UIWizardCloneVM>()->setKeepDiskNames(fKeepDiskNames);
    emit completeChanged();
}

void UIWizardCloneVMNamePathPage::sltKeepHardwareUUIDsToggled(bool fKeepHardwareUUIDs)
{
    AssertReturnVoid(wizardWindow<UIWizardCloneVM>());
    m_userModifiedParameters << "KeepHardwareUUIDs";
    wizardWindow<UIWizardCloneVM>()->setKeepHardwareUUIDs(fKeepHardwareUUIDs);
    emit completeChanged();
}
