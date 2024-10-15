/* $Id: UIWizardNewVMEditors.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUserNamePasswordEditor class implementation.
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
#include <QLabel>
#include <QVBoxLayout>

/* GUI includes: */
#include "QILineEdit.h"
#include "UIBaseMemoryEditor.h"
#include "UIFilePathSelector.h"
#include "UIHostnameDomainNameEditor.h"
#include "UIMediumTools.h"
#include "UITranslationEventListener.h"
#include "UIUserNamePasswordEditor.h"
#include "UIVirtualCPUEditor.h"
#include "UIWizardNewVM.h"
#include "UIWizardNewVMEditors.h"
#include "UIWizardNewVMUnattendedPage.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/*********************************************************************************************************************************
*   UIUserNamePasswordGroupBox implementation.                                                                                   *
*********************************************************************************************************************************/

UIUserNamePasswordGroupBox::UIUserNamePasswordGroupBox(QWidget *pParent /* = 0 */)
    : QGroupBox(pParent)
    , m_pUserNamePasswordEditor(0)
{
    prepare();
}

void UIUserNamePasswordGroupBox::prepare()
{
    QVBoxLayout *pUserNameContainerLayout = new QVBoxLayout(this);
    pUserNameContainerLayout->setContentsMargins(0, 0, 0, 0);
    m_pUserNamePasswordEditor = new UIUserNamePasswordEditor;
    AssertReturnVoid(m_pUserNamePasswordEditor);
    m_pUserNamePasswordEditor->setLabelsVisible(true);
    pUserNameContainerLayout->addWidget(m_pUserNamePasswordEditor, Qt::AlignTop);
    pUserNameContainerLayout->setStretch(0, 0);
    connect(m_pUserNamePasswordEditor, &UIUserNamePasswordEditor::sigPasswordChanged,
            this, &UIUserNamePasswordGroupBox::sigPasswordChanged);
    connect(m_pUserNamePasswordEditor, &UIUserNamePasswordEditor::sigUserNameChanged,
            this, &UIUserNamePasswordGroupBox::sigUserNameChanged);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIUserNamePasswordGroupBox::sltRetranslateUI);
    pUserNameContainerLayout->addStretch(1);
}

void UIUserNamePasswordGroupBox::sltRetranslateUI()
{
    setTitle(UIWizardNewVM::tr("Username and Password"));
}

QString UIUserNamePasswordGroupBox::userName() const
{
    if (m_pUserNamePasswordEditor)
        return m_pUserNamePasswordEditor->userName();
    return QString();
}

void UIUserNamePasswordGroupBox::setUserName(const QString &strUserName)
{
    if (m_pUserNamePasswordEditor)
        m_pUserNamePasswordEditor->setUserName(strUserName);
}

QString UIUserNamePasswordGroupBox::password() const
{
    if (m_pUserNamePasswordEditor)
        return m_pUserNamePasswordEditor->password();
    return QString();
}

void UIUserNamePasswordGroupBox::setPassword(const QString &strPassword)
{
    if (m_pUserNamePasswordEditor)
        m_pUserNamePasswordEditor->setPassword(strPassword);
}

bool UIUserNamePasswordGroupBox::isComplete()
{
    if (m_pUserNamePasswordEditor)
        return m_pUserNamePasswordEditor->isComplete();
    return false;
}

void UIUserNamePasswordGroupBox::setLabelsVisible(bool fVisible)
{
    if (m_pUserNamePasswordEditor)
        m_pUserNamePasswordEditor->setLabelsVisible(fVisible);
}


/*********************************************************************************************************************************
*   UIGAInstallationGroupBox implementation.                                                                                     *
*********************************************************************************************************************************/

UIGAInstallationGroupBox::UIGAInstallationGroupBox(QWidget *pParent /* = 0 */)
    : QGroupBox(pParent)
    , m_pGAISOPathLabel(0)
    , m_pGAISOFilePathSelector(0)

{
    prepare();
}

void UIGAInstallationGroupBox::prepare()
{
    setCheckable(true);

    QHBoxLayout *pGAInstallationISOLayout = new QHBoxLayout(this);
    AssertReturnVoid(pGAInstallationISOLayout);
    m_pGAISOPathLabel = new QLabel;
    AssertReturnVoid(m_pGAISOPathLabel);
    m_pGAISOPathLabel->setAlignment(Qt::AlignRight);
    m_pGAISOPathLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    pGAInstallationISOLayout->addWidget(m_pGAISOPathLabel);

    m_pGAISOFilePathSelector = new UIFilePathSelector;
    AssertReturnVoid(m_pGAISOFilePathSelector);

    m_pGAISOFilePathSelector->setResetEnabled(false);
    m_pGAISOFilePathSelector->setMode(UIFilePathSelector::Mode_File_Open);
    m_pGAISOFilePathSelector->setFileDialogFilters("ISO Images(*.iso *.ISO)");
    m_pGAISOFilePathSelector->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_pGAISOFilePathSelector->setInitialPath(UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_DVD));
    m_pGAISOFilePathSelector->setRecentMediaListType(UIMediumDeviceType_DVD);
    if (m_pGAISOPathLabel)
        m_pGAISOPathLabel->setBuddy(m_pGAISOFilePathSelector);

    pGAInstallationISOLayout->addWidget(m_pGAISOFilePathSelector);

    connect(m_pGAISOFilePathSelector, &UIFilePathSelector::pathChanged,
            this, &UIGAInstallationGroupBox::sigPathChanged);
    connect(this, &UIGAInstallationGroupBox::toggled,
            this, &UIGAInstallationGroupBox::sltToggleWidgetsEnabled);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIGAInstallationGroupBox::sltRetranslateUI);
}

void UIGAInstallationGroupBox::sltRetranslateUI()
{
    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->setToolTip(UIWizardNewVM::tr("Selects an installation medium (ISO file) for the Guest Additions."));
    if (m_pGAISOPathLabel)
        m_pGAISOPathLabel->setText(UIWizardNewVM::tr("Guest &Additions ISO:"));
    setTitle(UIWizardNewVM::tr("Gu&est Additions"));
    setToolTip(UIWizardNewVM::tr("When checked, the guest additions will be installed after the guest OS install."));
}

QString UIGAInstallationGroupBox::path() const
{
    if (m_pGAISOFilePathSelector)
        return m_pGAISOFilePathSelector->path();
    return QString();
}

void UIGAInstallationGroupBox::setPath(const QString &strPath, bool fRefreshText /* = true */)
{
    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->setPath(strPath, fRefreshText);
}

void UIGAInstallationGroupBox::mark()
{
    bool fError = !UIWizardNewVMUnattendedCommon::checkGAISOFile(path());
    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->mark(fError, UIWizardNewVM::tr("Invalid guest additions installation media"),
                                       UIWizardNewVM::tr("Guest additions installation media is valid"));
}

bool UIGAInstallationGroupBox::isComplete() const
{
    if (!isChecked())
        return true;
    return UIWizardNewVMUnattendedCommon::checkGAISOFile(path());
}

void UIGAInstallationGroupBox::sltToggleWidgetsEnabled(bool fEnabled)
{
    if (m_pGAISOPathLabel)
        m_pGAISOPathLabel->setEnabled(fEnabled);

    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->setEnabled(fEnabled);
}


/*********************************************************************************************************************************
*   UIAdditionalUnattendedOptions implementation.                                                                                *
*********************************************************************************************************************************/

UIAdditionalUnattendedOptions::UIAdditionalUnattendedOptions(QWidget *pParent /* = 0 */)
    : QGroupBox(pParent)
    , m_pHostnameDomainNameEditor(0)
{
    prepare();
}

void UIAdditionalUnattendedOptions::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    m_pHostnameDomainNameEditor = new UIHostnameDomainNameEditor;
    if (m_pHostnameDomainNameEditor)
        pMainLayout->addWidget(m_pHostnameDomainNameEditor, Qt::AlignTop);
    pMainLayout->setStretch(0, 0);
    if (m_pHostnameDomainNameEditor)
    {
        connect(m_pHostnameDomainNameEditor, &UIHostnameDomainNameEditor::sigHostnameDomainNameChanged,
                this, &UIAdditionalUnattendedOptions::sigHostnameDomainNameChanged);
        connect(m_pHostnameDomainNameEditor, &UIHostnameDomainNameEditor::sigProductKeyChanged,
                this, &UIAdditionalUnattendedOptions::sigProductKeyChanged);
        connect(m_pHostnameDomainNameEditor, &UIHostnameDomainNameEditor::sigStartHeadlessChanged,
                this, &UIAdditionalUnattendedOptions::sigStartHeadlessChanged);
    }

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIAdditionalUnattendedOptions::sltRetranslateUI);
    pMainLayout->addStretch(1);
}

void UIAdditionalUnattendedOptions::sltRetranslateUI()
{
    setTitle(UIWizardNewVM::tr("Additional Options"));
}

QString UIAdditionalUnattendedOptions::hostname() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->hostname();
    return QString();
}

void UIAdditionalUnattendedOptions::setHostname(const QString &strHostname)
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->setHostname(strHostname);
}

QString UIAdditionalUnattendedOptions::domainName() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->domainName();
    return QString();
}

void UIAdditionalUnattendedOptions::setDomainName(const QString &strDomainName)
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->setDomainName(strDomainName);
}

QString UIAdditionalUnattendedOptions::hostnameDomainName() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->hostnameDomainName();
    return QString();
}

bool UIAdditionalUnattendedOptions::isComplete() const
{
    return isHostnameComplete();
}

bool UIAdditionalUnattendedOptions::isHostnameComplete() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->isComplete();
    return false;
}


void UIAdditionalUnattendedOptions::mark()
{
    if (m_pHostnameDomainNameEditor)
        m_pHostnameDomainNameEditor->mark();
}

void UIAdditionalUnattendedOptions::disableEnableProductKeyWidgets(bool fEnabled)
{
    if (m_pHostnameDomainNameEditor)
        m_pHostnameDomainNameEditor->disableEnableProductKeyWidgets(fEnabled);
}

/*********************************************************************************************************************************
*   UINewVMHardwareContainer implementation.                                                                                *
*********************************************************************************************************************************/

UINewVMHardwareContainer::UINewVMHardwareContainer(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pBaseMemoryEditor(0)
    , m_pVirtualCPUEditor(0)
    , m_pEFICheckBox(0)
{
    prepare();
}

void UINewVMHardwareContainer::setMemorySize(int iSize)
{
    if (m_pBaseMemoryEditor)
        m_pBaseMemoryEditor->setValue(iSize);
}

void UINewVMHardwareContainer::setCPUCount(int iCount)
{
    if (m_pVirtualCPUEditor)
        m_pVirtualCPUEditor->setValue(iCount);
}

void UINewVMHardwareContainer::setEFIEnabled(bool fEnabled)
{
    if (m_pEFICheckBox)
        m_pEFICheckBox->setChecked(fEnabled);
}

void UINewVMHardwareContainer::prepare()
{
    QGridLayout *pHardwareLayout = new QGridLayout(this);
    pHardwareLayout->setContentsMargins(0, 0, 0, 0);

    m_pBaseMemoryEditor = new UIBaseMemoryEditor;
    m_pVirtualCPUEditor = new UIVirtualCPUEditor;
    m_pEFICheckBox      = new QCheckBox;
    pHardwareLayout->addWidget(m_pBaseMemoryEditor, 0, 0, 1, 4);
    pHardwareLayout->addWidget(m_pVirtualCPUEditor, 1, 0, 1, 4);
    pHardwareLayout->addWidget(m_pEFICheckBox, 2, 0, 1, 1);


    if (m_pBaseMemoryEditor)
        connect(m_pBaseMemoryEditor, &UIBaseMemoryEditor::sigValueChanged,
                this, &UINewVMHardwareContainer::sigMemorySizeChanged);
    if (m_pVirtualCPUEditor)
        connect(m_pVirtualCPUEditor, &UIVirtualCPUEditor::sigValueChanged,
            this, &UINewVMHardwareContainer::sigCPUCountChanged);
    if (m_pEFICheckBox)
        connect(m_pEFICheckBox, &QCheckBox::toggled,
                this, &UINewVMHardwareContainer::sigEFIEnabledChanged);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UINewVMHardwareContainer::sltRetranslateUI);
}

void UINewVMHardwareContainer::updateMinimumLayoutHint()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    if (m_pBaseMemoryEditor && !m_pBaseMemoryEditor->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pBaseMemoryEditor->minimumLabelHorizontalHint());
    if (m_pVirtualCPUEditor && !m_pVirtualCPUEditor->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pVirtualCPUEditor->minimumLabelHorizontalHint());
    if (m_pBaseMemoryEditor)
        m_pBaseMemoryEditor->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pVirtualCPUEditor)
        m_pVirtualCPUEditor->setMinimumLayoutIndent(iMinimumLayoutHint);
}

void UINewVMHardwareContainer::sltRetranslateUI()
{
    if (m_pEFICheckBox)
    {
        m_pEFICheckBox->setText(UIWizardNewVM::tr("&Enable EFI (special OSes only)"));
        m_pEFICheckBox->setToolTip(UIWizardNewVM::tr("When checked, the guest will support the Extended Firmware Interface (EFI), "
                                                     "which is required to boot certain guest OSes. Non-EFI aware OSes will not "
                                                     "be able to boot if this option is activated."));
    }

    updateMinimumLayoutHint();
}
