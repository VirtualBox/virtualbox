/* $Id: UIWizardNewCloudVMPageExpert.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewCloudVMPageExpert class implementation.
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
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>

/* GUI includes: */
#include "QIListWidget.h"
#include "QIToolButton.h"
#include "UICloudNetworkingStuff.h"
#include "UIFormEditorWidget.h"
#include "UIIconPool.h"
#include "UINotificationCenter.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManager.h"
#include "UIWizardNewCloudVM.h"
#include "UIWizardNewCloudVMPageExpert.h"

/* Namespaces: */
using namespace UIWizardNewCloudVMSource;
using namespace UIWizardNewCloudVMProperties;


UIWizardNewCloudVMPageExpert::UIWizardNewCloudVMPageExpert()
    : m_pProviderLabel(0)
    , m_pProviderComboBox(0)
    , m_pProfileLabel(0)
    , m_pProfileComboBox(0)
    , m_pProfileToolButton(0)
    , m_pSourceImageLabel(0)
    , m_pSourceTabBar(0)
    , m_pSourceImageList(0)
    , m_pLabelOptions(0)
    , m_pFormEditor(0)
{
    /* Prepare main layout: */
    QGridLayout *pLayoutMain = new QGridLayout(this);
    if (pLayoutMain)
    {
        pLayoutMain->setContentsMargins(0, 0, 0, 0);
        pLayoutMain->setColumnStretch(0, 0);
        pLayoutMain->setColumnStretch(1, 1);
        pLayoutMain->setRowStretch(2, 0);
        pLayoutMain->setRowStretch(3, 1);
        pLayoutMain->setRowStretch(4, 0);
        pLayoutMain->setRowStretch(5, 1);

        /* Prepare provider label: */
        m_pProviderLabel = new QLabel(this);
        if (m_pProviderLabel)
            pLayoutMain->addWidget(m_pProviderLabel, 0, 0, Qt::AlignRight);

        /* Prepare provider combo-box: */
        m_pProviderComboBox = new QComboBox(this);
        if (m_pProviderComboBox)
        {
            m_pProviderLabel->setBuddy(m_pProviderComboBox);
            pLayoutMain->addWidget(m_pProviderComboBox, 0, 1);
        }

        /* Prepare profile label: */
        m_pProfileLabel = new QLabel(this);
        if (m_pProfileLabel)
            pLayoutMain->addWidget(m_pProfileLabel, 1, 0, Qt::AlignRight);

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
            pLayoutMain->addLayout(pLayoutProfile, 1, 1);
        }

        /* Prepare source image label: */
        m_pSourceImageLabel = new QLabel(this);
        if (m_pSourceImageLabel)
            pLayoutMain->addWidget(m_pSourceImageLabel, 2, 0, Qt::AlignRight);

        /* Prepare source widget: */
        QWidget *pWidgetSource = new QWidget(this);
        if (pWidgetSource)
        {
            /* Prepare source layout: */
            QVBoxLayout *pLayoutSource = new QVBoxLayout(pWidgetSource);
            if (pLayoutSource)
            {
                pLayoutSource->setContentsMargins(0, 0, 0, 0);
                pLayoutSource->setSpacing(0);

                /* Prepare source tab-bar: */
                m_pSourceTabBar = new QTabBar(pWidgetSource);
                if (m_pSourceTabBar)
                {
                    m_pSourceTabBar->addTab(QString());
                    m_pSourceTabBar->addTab(QString());

                    /* Add into layout: */
                    pLayoutSource->addWidget(m_pSourceTabBar);
                }

                /* Prepare source image list: */
                m_pSourceImageList = new QIListWidget(pWidgetSource);
                if (m_pSourceImageList)
                {
                    m_pSourceImageLabel->setBuddy(m_pSourceImageList);
                    /* Make source image list fit 50 symbols
                     * horizontally and 8 lines vertically: */
                    const QFontMetrics fm(m_pSourceImageList->font());
                    const int iFontWidth = fm.horizontalAdvance('x');
                    const int iTotalWidth = 50 * iFontWidth;
                    const int iFontHeight = fm.height();
                    const int iTotalHeight = 8 * iFontHeight;
                    m_pSourceImageList->setMinimumSize(QSize(iTotalWidth, iTotalHeight));
                    /* We want to have sorting enabled: */
                    m_pSourceImageList->setSortingEnabled(true);
                    /* A bit of look&feel: */
                    m_pSourceImageList->setAlternatingRowColors(true);

                    /* Add into layout: */
                    pLayoutSource->addWidget(m_pSourceImageList);
                }
            }

            /* Add into layout: */
            pLayoutMain->addWidget(pWidgetSource, 2, 1, 2, 1);
        }

        /* Prepare label: */
        m_pLabelOptions = new QLabel(this);
        if (m_pLabelOptions)
            pLayoutMain->addWidget(m_pLabelOptions, 4, 0);

        /* Prepare form editor widget: */
        m_pFormEditor = new UIFormEditorWidget(this);
        if (m_pFormEditor)
        {
            m_pLabelOptions->setBuddy(m_pSourceImageList);
            /* Make form-editor fit 6 sections in height by default: */
            const int iDefaultSectionHeight = m_pFormEditor->verticalHeader()
                                            ? m_pFormEditor->verticalHeader()->defaultSectionSize()
                                            : 0;
            if (iDefaultSectionHeight > 0)
                m_pFormEditor->setMinimumHeight(6 * iDefaultSectionHeight);

            /* Add into layout: */
            pLayoutMain->addWidget(m_pFormEditor, 4, 1, 2, 1);
        }
    }

    /* Setup connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIWizardNewCloudVMPageExpert::sltHandleProviderComboChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIWizardNewCloudVMPageExpert::sltHandleProviderComboChange);
    connect(m_pProviderComboBox, &QComboBox::activated,
            this, &UIWizardNewCloudVMPageExpert::sltHandleProviderComboChange);
    connect(m_pProfileComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardNewCloudVMPageExpert::sltHandleProfileComboChange);
    connect(m_pProfileToolButton, &QIToolButton::clicked,
            this, &UIWizardNewCloudVMPageExpert::sltHandleProfileButtonClick);
    connect(m_pSourceTabBar, &QTabBar::currentChanged,
            this, &UIWizardNewCloudVMPageExpert::sltHandleSourceTabBarChange);
    connect(m_pSourceImageList, &QIListWidget::currentRowChanged,
            this, &UIWizardNewCloudVMPageExpert::sltHandleSourceImageChange);
}

UIWizardNewCloudVM *UIWizardNewCloudVMPageExpert::wizard() const
{
    return qobject_cast<UIWizardNewCloudVM*>(UINativeWizardPage::wizard());
}

void UIWizardNewCloudVMPageExpert::sltRetranslateUI()
{
    /* Translate provider label: */
    if (m_pProviderLabel)
        m_pProviderLabel->setText(UIWizardNewCloudVM::tr("&Provider:"));
    /* Translate received values of Provider combo-box.
     * We are enumerating starting from 0 for simplicity: */
    if (m_pProviderComboBox)
    {
        m_pProviderComboBox->setToolTip(UIWizardNewCloudVM::tr("Selects cloud service provider."));
        for (int i = 0; i < m_pProviderComboBox->count(); ++i)
            m_pProviderComboBox->setItemText(i, m_pProviderComboBox->itemData(i, ProviderData_Name).toString());
    }

    /* Translate profile stuff: */
    if (m_pProfileLabel)
        m_pProfileLabel->setText(UIWizardNewCloudVM::tr("P&rofile:"));
    if (m_pProfileComboBox)
        m_pProfileComboBox->setToolTip(UIWizardNewCloudVM::tr("Selects cloud profile."));
    if (m_pProfileToolButton)
    {
        m_pProfileToolButton->setText(UIWizardNewCloudVM::tr("Cloud Profile Manager"));
        m_pProfileToolButton->setToolTip(UIWizardNewCloudVM::tr("Opens cloud profile manager..."));
    }

    /* Translate source tab-bar: */
    if (m_pSourceImageLabel)
        m_pSourceImageLabel->setText(UIWizardNewCloudVM::tr("&Source:"));
    if (m_pSourceTabBar)
    {
        m_pSourceTabBar->setTabText(0, UIWizardNewCloudVM::tr("&Images"));
        m_pSourceTabBar->setTabText(1, UIWizardNewCloudVM::tr("&Boot Volumes"));
    }

    /* Translate source image list: */
    if (m_pSourceImageList)
        m_pSourceImageList->setWhatsThis(UIWizardNewCloudVM::tr("Lists all the source images or boot volumes."));

    /* Translate cloud VM properties table: */
    if (m_pLabelOptions)
        m_pLabelOptions->setText(UIWizardNewCloudVM::tr("&Options:"));
    if (m_pFormEditor)
        m_pFormEditor->setWhatsThis(UIWizardNewCloudVM::tr("Lists all the cloud VM properties."));
}

void UIWizardNewCloudVMPageExpert::initializePage()
{
    /* Make sure form-editor knows notification-center: */
    m_pFormEditor->setNotificationCenter(wizard()->notificationCenter());
    /* Populate providers: */
    populateProviders(m_pProviderComboBox, wizard()->notificationCenter());
    /* Translate providers: */
    sltRetranslateUI();
    /* Make image list focused by default: */
    m_pSourceImageList->setFocus();
    /* Fetch it, asynchronously: */
    QMetaObject::invokeMethod(this, "sltHandleProviderComboChange", Qt::QueuedConnection);
}

bool UIWizardNewCloudVMPageExpert::isComplete() const
{
    /* Initial result: */
    bool fResult = true;

    /* Check cloud settings: */
    fResult =    wizard()->client().isNotNull()
              && wizard()->vsd().isNotNull();

    /* Return result: */
    return fResult;
}

bool UIWizardNewCloudVMPageExpert::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Make sure table has own data committed: */
    m_pFormEditor->makeSureEditorDataCommitted();

    /* Check whether we have proper VSD form: */
    CVirtualSystemDescriptionForm comForm = wizard()->vsdForm();
    /* Give changed VSD back: */
    if (comForm.isNotNull())
    {
        comForm.GetVirtualSystemDescription();
        fResult = comForm.isOk();
        if (!fResult)
            UINotificationMessage::cannotAcquireVirtualSystemDescriptionFormParameter(comForm, wizard()->notificationCenter());
    }

    /* Try to create cloud VM: */
    if (fResult)
    {
        fResult = wizard()->createCloudVM();

        /* If the final step failed we could try
         * sugest user more valid form this time: */
        if (!fResult)
        {
            wizard()->setVSDForm(CVirtualSystemDescriptionForm());
            wizard()->createVSDForm();
            updatePropertiesTable();
            emit completeChanged();
        }
    }

    /* Return result: */
    return fResult;
}

void UIWizardNewCloudVMPageExpert::sltHandleProviderComboChange()
{
    /* Update wizard fields: */
    wizard()->setProviderShortName(m_pProviderComboBox->currentData(ProviderData_ShortName).toString());

    /* Update profiles: */
    populateProfiles(m_pProfileComboBox, wizard()->notificationCenter(), wizard()->providerShortName(), wizard()->profileName());
    sltHandleProfileComboChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardNewCloudVMPageExpert::sltHandleProfileComboChange()
{
    /* Update wizard fields: */
    wizard()->setProfileName(m_pProfileComboBox->currentData(ProfileData_Name).toString());
    wizard()->setClient(cloudClientByName(wizard()->providerShortName(), wizard()->profileName(), wizard()->notificationCenter()));

    /* Update source: */
    sltHandleSourceTabBarChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardNewCloudVMPageExpert::sltHandleProfileButtonClick()
{
    if (gpManager)
        gpManager->openCloudProfileManager();
}

void UIWizardNewCloudVMPageExpert::sltHandleSourceTabBarChange()
{
    /* Update source type: */
    populateSourceImages(m_pSourceImageList, m_pSourceTabBar, wizard()->notificationCenter(), wizard()->client());
    sltHandleSourceImageChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardNewCloudVMPageExpert::sltHandleSourceImageChange()
{
    /* Update source image & VSD form: */
    m_strSourceImageId = currentListWidgetData(m_pSourceImageList);
    wizard()->setVSD(createVirtualSystemDescription(wizard()->notificationCenter()));
    populateFormProperties(wizard()->vsd(), wizard(), m_pSourceTabBar, m_strSourceImageId);
    wizard()->createVSDForm();
    updatePropertiesTable();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardNewCloudVMPageExpert::updatePropertiesTable()
{
    refreshFormPropertiesTable(m_pFormEditor, wizard()->vsdForm());
}
