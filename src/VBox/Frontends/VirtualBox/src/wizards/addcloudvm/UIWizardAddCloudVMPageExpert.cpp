/* $Id: UIWizardAddCloudVMPageExpert.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardAddCloudVMPageExpert class implementation.
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
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIListWidget.h"
#include "QIToolButton.h"
#include "UICloudNetworkingStuff.h"
#include "UIIconPool.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManager.h"
#include "UIWizardAddCloudVM.h"
#include "UIWizardAddCloudVMPageExpert.h"

/* Namespaces: */
using namespace UIWizardAddCloudVMSource;


UIWizardAddCloudVMPageExpert::UIWizardAddCloudVMPageExpert()
    : m_pLayoutProvider(0)
    , m_pProviderLabel(0)
    , m_pProviderComboBox(0)
    , m_pProfileLabel(0)
    , m_pProfileComboBox(0)
    , m_pProfileToolButton(0)
    , m_pSourceInstanceLabel(0)
    , m_pSourceInstanceList(0)
{
    /* Prepare provider layout: */
    m_pLayoutProvider = new QGridLayout(this);
    if (m_pLayoutProvider)
    {
        m_pLayoutProvider->setContentsMargins(0, 0, 0, 0);
        m_pLayoutProvider->setColumnStretch(0, 0);
        m_pLayoutProvider->setColumnStretch(1, 1);
        m_pLayoutProvider->setRowStretch(2, 0);
        m_pLayoutProvider->setRowStretch(3, 1);

        /* Prepare provider label: */
        m_pProviderLabel = new QLabel(this);
        if (m_pProviderLabel)
            m_pLayoutProvider->addWidget(m_pProviderLabel, 0, 0, Qt::AlignRight);

        /* Prepare provider combo-box: */
        m_pProviderComboBox = new QComboBox(this);
        if (m_pProviderComboBox)
        {
            m_pProviderLabel->setBuddy(m_pProviderComboBox);
            m_pLayoutProvider->addWidget(m_pProviderComboBox, 0, 1);
        }

        /* Prepare profile label: */
        m_pProfileLabel = new QLabel(this);
        if (m_pProfileLabel)
            m_pLayoutProvider->addWidget(m_pProfileLabel, 1, 0, Qt::AlignRight);

        /* Prepare profile layout: */
        QHBoxLayout *pLayoutProfile = new QHBoxLayout;
        if (pLayoutProfile)
        {
            pLayoutProfile->setContentsMargins(0, 0, 0, 0);
            pLayoutProfile->setSpacing(1);

            /* Prepare profile combo-box: */
            m_pProfileComboBox = new QComboBox(this);
            if (m_pProfileComboBox)
            {
                m_pProfileLabel->setBuddy(m_pProfileComboBox);
                pLayoutProfile->addWidget(m_pProfileComboBox);
            }

            /* Prepare profile tool-button: */
            m_pProfileToolButton = new QIToolButton(this);
            if (m_pProfileToolButton)
            {
                m_pProfileToolButton->setIcon(UIIconPool::iconSet(":/cloud_profile_manager_16px.png",
                                                                  ":/cloud_profile_manager_disabled_16px.png"));
                pLayoutProfile->addWidget(m_pProfileToolButton);
            }

            /* Add into layout: */
            m_pLayoutProvider->addLayout(pLayoutProfile, 1, 1);
        }

        /* Prepare source instance label: */
        m_pSourceInstanceLabel = new QLabel(this);
        if (m_pSourceInstanceLabel)
            m_pLayoutProvider->addWidget(m_pSourceInstanceLabel, 2, 0, Qt::AlignRight);

        /* Prepare source instances table: */
        m_pSourceInstanceList = new QIListWidget(this);
        if (m_pSourceInstanceList)
        {
            m_pSourceInstanceLabel->setBuddy(m_pSourceInstanceLabel);
            /* Make source image list fit 50 symbols
             * horizontally and 8 lines vertically: */
            const QFontMetrics fm(m_pSourceInstanceList->font());
            const int iFontWidth = fm.horizontalAdvance('x');
            const int iTotalWidth = 50 * iFontWidth;
            const int iFontHeight = fm.height();
            const int iTotalHeight = 8 * iFontHeight;
            m_pSourceInstanceList->setMinimumSize(QSize(iTotalWidth, iTotalHeight));
            /* A bit of look&feel: */
            m_pSourceInstanceList->setAlternatingRowColors(true);
            /* Allow to select more than one item to add: */
            m_pSourceInstanceList->setSelectionMode(QAbstractItemView::ExtendedSelection);

            /* Add into layout: */
            m_pLayoutProvider->addWidget(m_pSourceInstanceList, 2, 1, 2, 1);
        }
    }

    /* Setup connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIWizardAddCloudVMPageExpert::sltHandleProviderComboChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIWizardAddCloudVMPageExpert::sltHandleProviderComboChange);
    connect(m_pProviderComboBox, &QComboBox::activated,
            this, &UIWizardAddCloudVMPageExpert::sltHandleProviderComboChange);
    connect(m_pProfileComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardAddCloudVMPageExpert::sltHandleProfileComboChange);
    connect(m_pProfileToolButton, &QIToolButton::clicked,
            this, &UIWizardAddCloudVMPageExpert::sltHandleProfileButtonClick);
    connect(m_pSourceInstanceList, &QIListWidget::itemSelectionChanged,
            this, &UIWizardAddCloudVMPageExpert::sltHandleSourceInstanceChange);
}

UIWizardAddCloudVM *UIWizardAddCloudVMPageExpert::wizard() const
{
    return qobject_cast<UIWizardAddCloudVM*>(UINativeWizardPage::wizard());
}

void UIWizardAddCloudVMPageExpert::sltRetranslateUI()
{
    /* Translate provider label: */
    if (m_pProviderLabel)
        m_pProviderLabel->setText(UIWizardAddCloudVM::tr("&Provider:"));
    /* Translate received values of Provider combo-box.
     * We are enumerating starting from 0 for simplicity: */
    if (m_pProviderComboBox)
    {
        m_pProviderComboBox->setToolTip(UIWizardAddCloudVM::tr("Selects cloud service provider."));
        for (int i = 0; i < m_pProviderComboBox->count(); ++i)
            m_pProviderComboBox->setItemText(i, m_pProviderComboBox->itemData(i, ProviderData_Name).toString());
    }

    /* Translate profile stuff: */
    if (m_pProfileLabel)
        m_pProfileLabel->setText(UIWizardAddCloudVM::tr("P&rofile:"));
    if (m_pProfileComboBox)
        m_pProfileComboBox->setToolTip(UIWizardAddCloudVM::tr("Selects cloud profile."));
    if (m_pProfileToolButton)
    {
        m_pProfileToolButton->setText(UIWizardAddCloudVM::tr("Cloud Profile Manager"));
        m_pProfileToolButton->setToolTip(UIWizardAddCloudVM::tr("Opens cloud profile manager..."));
    }

    /* Translate instances stuff: */
    if (m_pSourceInstanceLabel)
        m_pSourceInstanceLabel->setText(UIWizardAddCloudVM::tr("&Instances:"));
    if (m_pSourceInstanceList)
        m_pSourceInstanceList->setWhatsThis(UIWizardAddCloudVM::tr("Lists all the cloud VM instances."));
}

void UIWizardAddCloudVMPageExpert::initializePage()
{
    /* Populate providers: */
    populateProviders(m_pProviderComboBox, wizard()->notificationCenter());
    /* Translate providers: */
    sltRetranslateUI();
    /* Fetch it, asynchronously: */
    QMetaObject::invokeMethod(this, "sltHandleProviderComboChange", Qt::QueuedConnection);
    /* Make image list focused by default: */
    m_pSourceInstanceList->setFocus();
}

bool UIWizardAddCloudVMPageExpert::isComplete() const
{
    /* Initial result: */
    bool fResult = true;

    /* Make sure client is not NULL and
     * at least one instance is selected: */
    fResult =    wizard()->client().isNotNull()
              && !wizard()->instanceIds().isEmpty();

    /* Return result: */
    return fResult;
}

bool UIWizardAddCloudVMPageExpert::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Try to add cloud VMs: */
    fResult = wizard()->addCloudVMs();

    /* Return result: */
    return fResult;
}

void UIWizardAddCloudVMPageExpert::sltHandleProviderComboChange()
{
    /* Update wizard fields: */
    wizard()->setProviderShortName(m_pProviderComboBox->currentData(ProviderData_ShortName).toString());

    /* Update profiles: */
    populateProfiles(m_pProfileComboBox, wizard()->notificationCenter(), wizard()->providerShortName(), wizard()->profileName());
    sltHandleProfileComboChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardAddCloudVMPageExpert::sltHandleProfileComboChange()
{
    /* Update wizard fields: */
    wizard()->setProfileName(m_pProfileComboBox->currentData(ProfileData_Name).toString());
    wizard()->setClient(cloudClientByName(wizard()->providerShortName(), wizard()->profileName(), wizard()->notificationCenter()));

    /* Update profile instances: */
    populateProfileInstances(m_pSourceInstanceList, wizard()->notificationCenter(), wizard()->client());
    sltHandleSourceInstanceChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardAddCloudVMPageExpert::sltHandleProfileButtonClick()
{
    if (gpManager)
        gpManager->openCloudProfileManager();
}

void UIWizardAddCloudVMPageExpert::sltHandleSourceInstanceChange()
{
    /* Update wizard fields: */
    wizard()->setInstanceIds(currentListWidgetData(m_pSourceInstanceList));

    /* Notify about changes: */
    emit completeChanged();
}
