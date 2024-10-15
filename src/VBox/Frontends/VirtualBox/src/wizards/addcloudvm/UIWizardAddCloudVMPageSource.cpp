/* $Id: UIWizardAddCloudVMPageSource.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardAddCloudVMPageSource class implementation.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "QIListWidget.h"
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UICloudNetworkingStuff.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManager.h"
#include "UIWizardAddCloudVM.h"
#include "UIWizardAddCloudVMPageSource.h"

/* COM includes: */
#include "CStringArray.h"

/* Namespaces: */
using namespace UIWizardAddCloudVMSource;


/*********************************************************************************************************************************
*   Namespace UIWizardAddCloudVMSource implementation.                                                                           *
*********************************************************************************************************************************/

void UIWizardAddCloudVMSource::populateProviders(QComboBox *pCombo, UINotificationCenter *pCenter)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* Remember current item data to be able to restore it: */
    QString strOldData;
    if (pCombo->currentIndex() != -1)
        strOldData = pCombo->currentData(ProviderData_ShortName).toString();
    /* Otherwise "OCI" should be the default one: */
    else
        strOldData = "OCI";

    /* Block signals while updating: */
    pCombo->blockSignals(true);

    /* Clear combo initially: */
    pCombo->clear();

    /* Iterate through existing providers: */
    foreach (const CCloudProvider &comProvider, listCloudProviders(pCenter))
    {
        /* Skip if we have nothing to populate (file missing?): */
        if (comProvider.isNull())
            continue;
        /* Acquire provider name: */
        QString strProviderName;
        if (!cloudProviderName(comProvider, strProviderName, pCenter))
            continue;
        /* Acquire provider short name: */
        QString strProviderShortName;
        if (!cloudProviderShortName(comProvider, strProviderShortName, pCenter))
            continue;

        /* Compose empty item, fill the data: */
        pCombo->addItem(QString());
        pCombo->setItemData(pCombo->count() - 1, strProviderName,      ProviderData_Name);
        pCombo->setItemData(pCombo->count() - 1, strProviderShortName, ProviderData_ShortName);
    }

    /* Set previous/default item if possible: */
    int iNewIndex = -1;
    if (   iNewIndex == -1
        && !strOldData.isNull())
        iNewIndex = pCombo->findData(strOldData, ProviderData_ShortName);
    if (   iNewIndex == -1
        && pCombo->count() > 0)
        iNewIndex = 0;
    if (iNewIndex != -1)
        pCombo->setCurrentIndex(iNewIndex);

    /* Unblock signals after update: */
    pCombo->blockSignals(false);
}

void UIWizardAddCloudVMSource::populateProfiles(QComboBox *pCombo,
                                                UINotificationCenter *pCenter,
                                                const QString &strProviderShortName,
                                                const QString &strProfileName)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);
    /* Acquire provider: */
    CCloudProvider comProvider = cloudProviderByShortName(strProviderShortName, pCenter);
    AssertReturnVoid(comProvider.isNotNull());

    /* Remember current item data to be able to restore it: */
    QString strOldData;
    if (pCombo->currentIndex() != -1)
        strOldData = pCombo->currentData(ProfileData_Name).toString();
    else if (!strProfileName.isEmpty())
        strOldData = strProfileName;

    /* Block signals while updating: */
    pCombo->blockSignals(true);

    /* Clear combo initially: */
    pCombo->clear();

    /* Acquire restricted accounts: */
    const QStringList restrictedProfiles = gEDataManager->cloudProfileManagerRestrictions();

    /* Iterate through existing profiles: */
    QStringList allowedProfileNames;
    QStringList restrictedProfileNames;
    foreach (const CCloudProfile &comProfile, listCloudProfiles(comProvider, pCenter))
    {
        /* Skip if we have nothing to populate (wtf happened?): */
        if (comProfile.isNull())
            continue;
        /* Acquire current profile name: */
        QString strCurrentProfileName;
        if (!cloudProfileName(comProfile, strCurrentProfileName, pCenter))
            continue;

        /* Compose full profile name: */
        const QString strFullProfileName = QString("/%1/%2").arg(strProviderShortName).arg(strCurrentProfileName);
        /* Append to appropriate list: */
        if (restrictedProfiles.contains(strFullProfileName))
            restrictedProfileNames.append(strCurrentProfileName);
        else
            allowedProfileNames.append(strCurrentProfileName);
    }

    /* Add allowed items: */
    foreach (const QString &strAllowedProfileName, allowedProfileNames)
    {
        /* Compose item, fill it's data: */
        pCombo->addItem(strAllowedProfileName);
        pCombo->setItemData(pCombo->count() - 1, strAllowedProfileName, ProfileData_Name);
        QFont fnt = pCombo->font();
        fnt.setBold(true);
        pCombo->setItemData(pCombo->count() - 1, fnt, Qt::FontRole);
    }
    /* Add restricted items: */
    foreach (const QString &strRestrictedProfileName, restrictedProfileNames)
    {
        /* Compose item, fill it's data: */
        pCombo->addItem(strRestrictedProfileName);
        pCombo->setItemData(pCombo->count() - 1, strRestrictedProfileName, ProfileData_Name);
        QBrush brsh;
        brsh.setColor(Qt::gray);
        pCombo->setItemData(pCombo->count() - 1, brsh, Qt::ForegroundRole);
    }

    /* Set previous/default item if possible: */
    int iNewIndex = -1;
    if (   iNewIndex == -1
        && !strOldData.isNull())
        iNewIndex = pCombo->findData(strOldData, ProfileData_Name);
    if (   iNewIndex == -1
        && pCombo->count() > 0)
        iNewIndex = 0;
    if (iNewIndex != -1)
        pCombo->setCurrentIndex(iNewIndex);

    /* Unblock signals after update: */
    pCombo->blockSignals(false);
}

void UIWizardAddCloudVMSource::populateProfileInstances(QIListWidget *pList, UINotificationCenter *pCenter, const CCloudClient &comClient)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pList);
    AssertReturnVoid(comClient.isNotNull());

    /* Block signals while updating: */
    pList->blockSignals(true);

    /* Clear list initially: */
    pList->clear();

    /* Gather instance names and ids: */
    CStringArray comNames;
    CStringArray comIDs;
    if (listCloudSourceInstances(comClient, comNames, comIDs, pCenter))
    {
        /* Push acquired names to list rows: */
        const QVector<QString> names = comNames.GetValues();
        const QVector<QString> ids = comIDs.GetValues();
        for (int i = 0; i < names.size(); ++i)
        {
            /* Create list item: */
            QIListWidgetItem *pItem = new QIListWidgetItem(names.at(i), pList);
            if (pItem)
            {
                pItem->setFlags(pItem->flags() & ~Qt::ItemIsEditable);
                pItem->setData(Qt::UserRole, ids.at(i));
            }
        }
    }

    /* Choose the 1st one by default if possible: */
    if (pList->count())
        pList->setCurrentRow(0);

    /* Unblock signals after update: */
    pList->blockSignals(false);
}

QStringList UIWizardAddCloudVMSource::currentListWidgetData(QIListWidget *pList)
{
    QStringList result;
    foreach (QIListWidgetItem *pItem, pList->selectedItems())
        result << pItem->data(Qt::UserRole).toString();
    return result;
}


/*********************************************************************************************************************************
*   Class UIWizardAddCloudVMPageSource implementation.                                                                           *
*********************************************************************************************************************************/

UIWizardAddCloudVMPageSource::UIWizardAddCloudVMPageSource()
    : m_pLabelMain(0)
    , m_pProviderLayout(0)
    , m_pProviderLabel(0)
    , m_pProviderComboBox(0)
    , m_pLabelDescription(0)
    , m_pOptionsLayout(0)
    , m_pProfileLabel(0)
    , m_pProfileComboBox(0)
    , m_pProfileToolButton(0)
    , m_pSourceInstanceLabel(0)
    , m_pSourceInstanceList(0)
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* Prepare main label: */
        m_pLabelMain = new QIRichTextLabel(this);
        if (m_pLabelMain)
            pLayoutMain->addWidget(m_pLabelMain);

        /* Prepare provider layout: */
        m_pProviderLayout = new QGridLayout;
        if (m_pProviderLayout)
        {
            m_pProviderLayout->setContentsMargins(0, 0, 0, 0);
            m_pProviderLayout->setColumnStretch(0, 0);
            m_pProviderLayout->setColumnStretch(1, 1);

            /* Prepare provider label: */
            m_pProviderLabel = new QLabel(this);
            if (m_pProviderLabel)
                m_pProviderLayout->addWidget(m_pProviderLabel, 0, 0, Qt::AlignRight);

            /* Prepare provider combo-box: */
            m_pProviderComboBox = new QComboBox(this);
            if (m_pProviderComboBox)
            {
                m_pProviderLabel->setBuddy(m_pProviderComboBox);
                m_pProviderLayout->addWidget(m_pProviderComboBox, 0, 1);
            }

            /* Add into layout: */
            pLayoutMain->addLayout(m_pProviderLayout);
        }

        /* Prepare description label: */
        m_pLabelDescription = new QIRichTextLabel(this);
        if (m_pLabelDescription)
            pLayoutMain->addWidget(m_pLabelDescription);

        /* Prepare options layout: */
        m_pOptionsLayout = new QGridLayout;
        if (m_pOptionsLayout)
        {
            m_pOptionsLayout->setContentsMargins(0, 0, 0, 0);
            m_pOptionsLayout->setColumnStretch(0, 0);
            m_pOptionsLayout->setColumnStretch(1, 1);
            m_pOptionsLayout->setRowStretch(1, 0);
            m_pOptionsLayout->setRowStretch(2, 1);

            /* Prepare profile label: */
            m_pProfileLabel = new QLabel(this);
            if (m_pProfileLabel)
                m_pOptionsLayout->addWidget(m_pProfileLabel, 0, 0, Qt::AlignRight);

            /* Prepare profile layout: */
            QHBoxLayout *pProfileLayout = new QHBoxLayout;
            if (pProfileLayout)
            {
                pProfileLayout->setContentsMargins(0, 0, 0, 0);
                pProfileLayout->setSpacing(1);

                /* Prepare profile combo-box: */
                m_pProfileComboBox = new QComboBox(this);
                if (m_pProfileComboBox)
                {
                    m_pProfileLabel->setBuddy(m_pProfileComboBox);
                    pProfileLayout->addWidget(m_pProfileComboBox);
                }

                /* Prepare profile tool-button: */
                m_pProfileToolButton = new QIToolButton(this);
                if (m_pProfileToolButton)
                {
                    m_pProfileToolButton->setIcon(UIIconPool::iconSet(":/cloud_profile_manager_16px.png",
                                                                      ":/cloud_profile_manager_disabled_16px.png"));
                    pProfileLayout->addWidget(m_pProfileToolButton);
                }

                /* Add into layout: */
                m_pOptionsLayout->addLayout(pProfileLayout, 0, 1);
            }

            /* Prepare source instance label: */
            m_pSourceInstanceLabel = new QLabel(this);
            if (m_pSourceInstanceLabel)
                m_pOptionsLayout->addWidget(m_pSourceInstanceLabel, 1, 0, Qt::AlignRight);

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
                m_pOptionsLayout->addWidget(m_pSourceInstanceList, 1, 1, 2, 1);
            }

            /* Add into layout: */
            pLayoutMain->addLayout(m_pOptionsLayout);
        }
    }

    /* Setup connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIWizardAddCloudVMPageSource::sltHandleProviderComboChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIWizardAddCloudVMPageSource::sltHandleProviderComboChange);
    connect(m_pProviderComboBox, &QComboBox::activated,
            this, &UIWizardAddCloudVMPageSource::sltHandleProviderComboChange);
    connect(m_pProfileComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardAddCloudVMPageSource::sltHandleProfileComboChange);
    connect(m_pProfileToolButton, &QIToolButton::clicked,
            this, &UIWizardAddCloudVMPageSource::sltHandleProfileButtonClick);
    connect(m_pSourceInstanceList, &QIListWidget::itemSelectionChanged,
            this, &UIWizardAddCloudVMPageSource::sltHandleSourceInstanceChange);
}

UIWizardAddCloudVM *UIWizardAddCloudVMPageSource::wizard() const
{
    return qobject_cast<UIWizardAddCloudVM*>(UINativeWizardPage::wizard());
}

void UIWizardAddCloudVMPageSource::sltRetranslateUI()
{
    /* Translate page: */
    setTitle(UIWizardAddCloudVM::tr("Source to add from"));

    /* Translate main label: */
    if (m_pLabelMain)
        m_pLabelMain->setText(UIWizardAddCloudVM::tr("Please choose the source to add cloud virtual machine from.  This can "
                                                     "be one of known cloud service providers below."));

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

    /* Translate description label: */
    if (m_pLabelDescription)
        m_pLabelDescription->setText(UIWizardAddCloudVM::tr("Please choose one of cloud service profiles you have registered to "
                                                            "add virtual machine from.  Existing instance list will be "
                                                            "updated.  To continue, select at least one instance to add virtual "
                                                            "machine on the basis of it."));

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

    /* Adjust label widths: */
    QList<QWidget*> labels;
    if (m_pProviderLabel)
        labels << m_pProviderLabel;
    if (m_pProfileLabel)
        labels << m_pProfileLabel;
    if (m_pSourceInstanceLabel)
        labels << m_pSourceInstanceLabel;
    int iMaxWidth = 0;
    foreach (QWidget *pLabel, labels)
        iMaxWidth = qMax(iMaxWidth, pLabel->minimumSizeHint().width());
    if (m_pProviderLayout)
        m_pProviderLayout->setColumnMinimumWidth(0, iMaxWidth);
    if (m_pOptionsLayout)
        m_pOptionsLayout->setColumnMinimumWidth(0, iMaxWidth);
}

void UIWizardAddCloudVMPageSource::initializePage()
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

bool UIWizardAddCloudVMPageSource::isComplete() const
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

bool UIWizardAddCloudVMPageSource::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Try to add cloud VMs: */
    fResult = wizard()->addCloudVMs();

    /* Return result: */
    return fResult;
}

void UIWizardAddCloudVMPageSource::sltHandleProviderComboChange()
{
    /* Update wizard fields: */
    wizard()->setProviderShortName(m_pProviderComboBox->currentData(ProviderData_ShortName).toString());

    /* Update profiles: */
    populateProfiles(m_pProfileComboBox, wizard()->notificationCenter(), wizard()->providerShortName(), wizard()->profileName());
    sltHandleProfileComboChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardAddCloudVMPageSource::sltHandleProfileComboChange()
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

void UIWizardAddCloudVMPageSource::sltHandleProfileButtonClick()
{
    if (gpManager)
        gpManager->openCloudProfileManager();
}

void UIWizardAddCloudVMPageSource::sltHandleSourceInstanceChange()
{
    /* Update wizard fields: */
    wizard()->setInstanceIds(currentListWidgetData(m_pSourceInstanceList));

    /* Notify about changes: */
    emit completeChanged();
}
