/* $Id: UIWizardNewVMUnattendedPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMUnattendedPage class implementation.
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
#include <QFileInfo>
#include <QGridLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIWizardNewVMEditors.h"
#include "UIWizardNewVMUnattendedPage.h"
#include "UIWizardNewVM.h"

bool UIWizardNewVMUnattendedCommon::checkGAISOFile(const QString &strPath)
{
    if (strPath.isNull() || strPath.isEmpty())
        return false;
    QFileInfo fileInfo(strPath);
    if (!fileInfo.exists() || !fileInfo.isReadable())
        return false;
    return true;
}

UIWizardNewVMUnattendedPage::UIWizardNewVMUnattendedPage()
    : m_pLabel(0)
    , m_pAdditionalOptionsContainer(0)
    , m_pGAInstallationISOContainer(0)
    , m_pUserNamePasswordGroupBox(0)
{
    prepare();
}

void UIWizardNewVMUnattendedPage::prepare()
{
    QGridLayout *pMainLayout = new QGridLayout(this);

    m_pLabel = new QIRichTextLabel(this);
    if (m_pLabel)
        pMainLayout->addWidget(m_pLabel, 0, 0, 1, 2);

    m_pUserNamePasswordGroupBox = new UIUserNamePasswordGroupBox;
    AssertReturnVoid(m_pUserNamePasswordGroupBox);
    pMainLayout->addWidget(m_pUserNamePasswordGroupBox, 1, 0, 1, 1);

    m_pAdditionalOptionsContainer = new UIAdditionalUnattendedOptions;
    AssertReturnVoid(m_pAdditionalOptionsContainer);
    pMainLayout->addWidget(m_pAdditionalOptionsContainer, 1, 1, 1, 1);

    m_pGAInstallationISOContainer = new UIGAInstallationGroupBox;
    AssertReturnVoid(m_pGAInstallationISOContainer);
    pMainLayout->addWidget(m_pGAInstallationISOContainer, 2, 0, 1, 2);

    pMainLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding), 4, 0, 1, 2);

    createConnections();
}

void UIWizardNewVMUnattendedPage::createConnections()
{
    if (m_pUserNamePasswordGroupBox)
    {
        connect(m_pUserNamePasswordGroupBox, &UIUserNamePasswordGroupBox::sigPasswordChanged,
                this, &UIWizardNewVMUnattendedPage::sltPasswordChanged);
        connect(m_pUserNamePasswordGroupBox, &UIUserNamePasswordGroupBox::sigUserNameChanged,
                this, &UIWizardNewVMUnattendedPage::sltUserNameChanged);
    }
    if (m_pGAInstallationISOContainer)
    {
        connect(m_pGAInstallationISOContainer, &UIGAInstallationGroupBox::toggled,
                this, &UIWizardNewVMUnattendedPage::sltInstallGACheckBoxToggle);
        connect(m_pGAInstallationISOContainer, &UIGAInstallationGroupBox::sigPathChanged,
                this, &UIWizardNewVMUnattendedPage::sltGAISOPathChanged);
    }

    if (m_pAdditionalOptionsContainer)
    {
        connect(m_pAdditionalOptionsContainer, &UIAdditionalUnattendedOptions::sigHostnameDomainNameChanged,
                this, &UIWizardNewVMUnattendedPage::sltHostnameDomainNameChanged);
        connect(m_pAdditionalOptionsContainer, &UIAdditionalUnattendedOptions::sigProductKeyChanged,
                this, &UIWizardNewVMUnattendedPage::sltProductKeyChanged);
        connect(m_pAdditionalOptionsContainer, &UIAdditionalUnattendedOptions::sigStartHeadlessChanged,
                this, &UIWizardNewVMUnattendedPage::sltStartHeadlessChanged);
    }
}


void UIWizardNewVMUnattendedPage::sltRetranslateUI()
{
    setTitle(UIWizardNewVM::tr("Unattended Guest OS Install Setup"));
    if (m_pLabel)
        m_pLabel->setText(UIWizardNewVM::tr("You can configure the unattended guest OS install by modifying username, password, "
                                            "and hostname. Additionally you can enable guest additions install. "
                                            "For Microsoft Windows guests it is possible to provide a product key."));
    if (m_pUserNamePasswordGroupBox)
        m_pUserNamePasswordGroupBox->setTitle(UIWizardNewVM::tr("Username and Password"));
}


void UIWizardNewVMUnattendedPage::initializePage()
{
    if (m_pAdditionalOptionsContainer)
        m_pAdditionalOptionsContainer->disableEnableProductKeyWidgets(isProductKeyWidgetEnabled());
    sltRetranslateUI();

    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    /* Initialize user/password if they are not modified by the user: */
    if (m_pUserNamePasswordGroupBox)
    {
        m_pUserNamePasswordGroupBox->blockSignals(true);
        if (!m_userModifiedParameters.contains("UserName"))
            m_pUserNamePasswordGroupBox->setUserName(pWizard->userName());
        if (!m_userModifiedParameters.contains("Password"))
            m_pUserNamePasswordGroupBox->setPassword(pWizard->password());
        m_pUserNamePasswordGroupBox->blockSignals(false);
    }
    if (m_pAdditionalOptionsContainer)
    {
        m_pAdditionalOptionsContainer->blockSignals(true);

        if (!m_userModifiedParameters.contains("HostnameDomainName"))
        {
            m_pAdditionalOptionsContainer->setHostname(pWizard->machineBaseName());
            m_pAdditionalOptionsContainer->setDomainName("myguest.virtualbox.org");
            /* Initialize unattended hostname here since we cannot get the efault value from CUnattended this early (unlike username etc): */
            if (m_pAdditionalOptionsContainer->isHostnameComplete())
                pWizard->setHostnameDomainName(m_pAdditionalOptionsContainer->hostnameDomainName());
        }
        m_pAdditionalOptionsContainer->blockSignals(false);
    }
    if (m_pGAInstallationISOContainer && !m_userModifiedParameters.contains("InstallGuestAdditions"))
    {
        m_pGAInstallationISOContainer->blockSignals(true);
        m_pGAInstallationISOContainer->setChecked(pWizard->installGuestAdditions());
        m_pGAInstallationISOContainer->blockSignals(false);
    }

    if (m_pGAInstallationISOContainer && !m_userModifiedParameters.contains("GuestAdditionsISOPath"))
    {
        m_pGAInstallationISOContainer->blockSignals(true);
        m_pGAInstallationISOContainer->setPath(pWizard->guestAdditionsISOPath());
        m_pGAInstallationISOContainer->blockSignals(false);
    }
}

bool UIWizardNewVMUnattendedPage::isComplete() const
{
    markWidgets();
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    if (pWizard && pWizard->installGuestAdditions() &&
        m_pGAInstallationISOContainer &&
        !UIWizardNewVMUnattendedCommon::checkGAISOFile(m_pGAInstallationISOContainer->path()))
        return false;
    if (m_pUserNamePasswordGroupBox && !m_pUserNamePasswordGroupBox->isComplete())
        return false;
    if (m_pAdditionalOptionsContainer && !m_pAdditionalOptionsContainer->isComplete())
        return false;
    return true;
}

void UIWizardNewVMUnattendedPage::sltInstallGACheckBoxToggle(bool fEnabled)
{
    wizardWindow<UIWizardNewVM>()->setInstallGuestAdditions(fEnabled);
    m_userModifiedParameters << "InstallGuestAdditions";
    emit completeChanged();
}

void UIWizardNewVMUnattendedPage::sltGAISOPathChanged(const QString &strPath)
{
    wizardWindow<UIWizardNewVM>()->setGuestAdditionsISOPath(strPath);
    m_userModifiedParameters << "GuestAdditionsISOPath";
    emit completeChanged();
}

void UIWizardNewVMUnattendedPage::sltPasswordChanged(const QString &strPassword)
{
    wizardWindow<UIWizardNewVM>()->setPassword(strPassword);
    m_userModifiedParameters << "Password";
    emit completeChanged();
}

void UIWizardNewVMUnattendedPage::sltUserNameChanged(const QString &strUserName)
{
    wizardWindow<UIWizardNewVM>()->setUserName(strUserName);
    m_userModifiedParameters << "UserName";
    emit completeChanged();
}

bool UIWizardNewVMUnattendedPage::isProductKeyWidgetEnabled() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    if (!pWizard || !pWizard->isUnattendedEnabled() || !pWizard->isGuestOSTypeWindows())
        return false;
    return true;
}

void UIWizardNewVMUnattendedPage::sltHostnameDomainNameChanged(const QString &strHostnameDomainName, bool fIsComplete)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    emit completeChanged();

    if (fIsComplete)
    {
        wizardWindow<UIWizardNewVM>()->setHostnameDomainName(strHostnameDomainName);
        m_userModifiedParameters << "HostnameDomainName";
    }
}

void UIWizardNewVMUnattendedPage::sltProductKeyChanged(const QString &strProductKey)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "ProductKey";
    wizardWindow<UIWizardNewVM>()->setProductKey(strProductKey);
}

void UIWizardNewVMUnattendedPage::sltStartHeadlessChanged(bool fStartHeadless)
{
    m_userModifiedParameters << "StartHeadless";
    wizardWindow<UIWizardNewVM>()->setStartHeadless(fStartHeadless);
}

void UIWizardNewVMUnattendedPage::markWidgets() const
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    if (pWizard && pWizard->installGuestAdditions() && m_pGAInstallationISOContainer)
        m_pGAInstallationISOContainer->mark();
}

void UIWizardNewVMUnattendedPage::sltSelectedWindowsImageChanged(ulong uImageIndex)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setSelectedWindowImageIndex(uImageIndex);
}
