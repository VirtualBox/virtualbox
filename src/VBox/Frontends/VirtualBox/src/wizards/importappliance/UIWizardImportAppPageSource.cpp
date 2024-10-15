/* $Id: UIWizardImportAppPageSource.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardImportAppPageSource class implementation.
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
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UICloudNetworkingStuff.h"
#include "UICommon.h"
#include "UIEmptyFilePathSelector.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UINotificationCenter.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManager.h"
#include "UIWizardImportApp.h"
#include "UIWizardImportAppPageSource.h"

/* COM includes: */
#include "CAppliance.h"
#include "CVirtualSystemDescription.h"
#include "CVirtualSystemDescriptionForm.h"

/* Namespaces: */
using namespace UIWizardImportAppSource;


/*********************************************************************************************************************************
*   Class UIWizardImportAppSource implementation.                                                                                *
*********************************************************************************************************************************/

void UIWizardImportAppSource::populateSources(QComboBox *pCombo,
                                              UINotificationCenter *pCenter,
                                              bool fImportFromOCIByDefault,
                                              const QString &strSource)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* Remember current item data to be able to restore it: */
    QString strOldData;
    if (pCombo->currentIndex() != -1)
        strOldData = pCombo->currentData(SourceData_ShortName).toString();
    else
    {
        /* Otherwise "OCI" or "local" should be the default one: */
        if (fImportFromOCIByDefault)
            strOldData = strSource.isEmpty() ? "OCI" : strSource;
        else
            strOldData = "local";
    }

    /* Block signals while updating: */
    pCombo->blockSignals(true);

    /* Clear combo initially: */
    pCombo->clear();

    /* Compose hardcoded sources list: */
    QStringList sources;
    sources << "local";
    /* Add that list to combo: */
    foreach (const QString &strShortName, sources)
    {
        /* Compose empty item, fill it's data: */
        pCombo->addItem(QString());
        pCombo->setItemData(pCombo->count() - 1, strShortName, SourceData_ShortName);
    }

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

        /* Compose empty item, fill it's data: */
        pCombo->addItem(QString());
        pCombo->setItemData(pCombo->count() - 1, strProviderName,      SourceData_Name);
        pCombo->setItemData(pCombo->count() - 1, strProviderShortName, SourceData_ShortName);
        pCombo->setItemData(pCombo->count() - 1, true,                 SourceData_IsItCloudFormat);
    }

    /* Set previous/default item if possible: */
    int iNewIndex = -1;
    if (   iNewIndex == -1
        && !strOldData.isNull())
        iNewIndex = pCombo->findData(strOldData, SourceData_ShortName);
    if (   iNewIndex == -1
        && pCombo->count() > 0)
        iNewIndex = 0;
    if (iNewIndex != -1)
        pCombo->setCurrentIndex(iNewIndex);

    /* Unblock signals after update: */
    pCombo->blockSignals(false);
}

QString UIWizardImportAppSource::source(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturn(pCombo, QString());

    /* Give the actual result: */
    return pCombo->currentData(SourceData_ShortName).toString();
}

bool UIWizardImportAppSource::isSourceCloudOne(QComboBox *pCombo, int iIndex /* = -1 */)
{
    /* Sanity check: */
    AssertPtrReturn(pCombo, false);

    /* Handle special case, -1 means "current one": */
    if (iIndex == -1)
        iIndex = pCombo->currentIndex();

    /* Give the actual result: */
    return pCombo->itemData(iIndex, SourceData_IsItCloudFormat).toBool();
}

void UIWizardImportAppSource::refreshStackedWidget(QStackedWidget *pStackedWidget,
                                                   bool fIsSourceCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pStackedWidget);

    /* Update stack appearance according to chosen source: */
    pStackedWidget->setCurrentIndex((int)fIsSourceCloudOne);
}

void UIWizardImportAppSource::refreshProfileCombo(QComboBox *pCombo,
                                                  UINotificationCenter *pCenter,
                                                  const QString &strSource,
                                                  const QString &strProfileName,
                                                  bool fIsSourceCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* If source is cloud one: */
    if (fIsSourceCloudOne)
    {
        /* Acquire provider: */
        CCloudProvider comProvider = cloudProviderByShortName(strSource, pCenter);
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

        /* Iterate through existing profile names: */
        QStringList allowedProfileNames;
        QStringList restrictedProfileNames;
        foreach (const CCloudProfile &comProfile, listCloudProfiles(comProvider, pCenter))
        {
            /* Skip if we have nothing to populate (wtf happened?): */
            if (comProfile.isNull())
                continue;
            /* Acquire profile name: */
            QString strCurrentProfileName;
            if (!cloudProfileName(comProfile, strCurrentProfileName, pCenter))
                continue;

            /* Compose full profile name: */
            const QString strFullProfileName = QString("/%1/%2").arg(strSource).arg(strCurrentProfileName);
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
    /* If source is local one: */
    else
    {
        /* Block signals while updating: */
        pCombo->blockSignals(true);

        /* Clear combo initially: */
        pCombo->clear();

        /* Unblock signals after update: */
        pCombo->blockSignals(false);
    }
}

void UIWizardImportAppSource::refreshCloudProfileInstances(QListWidget *pListWidget,
                                                           UINotificationCenter *pCenter,
                                                           const QString &strSource,
                                                           const QString &strProfileName,
                                                           bool fIsSourceCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pListWidget);

    /* If source is cloud one: */
    if (fIsSourceCloudOne)
    {
        /* We need top-level parent as well: */
        QWidget *pParent = pListWidget->window();
        AssertPtrReturnVoid(pParent);
        /* Acquire client: */
        CCloudClient comClient = cloudClientByName(strSource, strProfileName, pCenter);
        AssertReturnVoid(comClient.isNotNull());

        /* Block signals while updating: */
        pListWidget->blockSignals(true);

        /* Clear list initially: */
        pListWidget->clear();

        /* Gather instance names and ids: */
        CStringArray comNames;
        CStringArray comIDs;
        if (listCloudInstances(comClient, comNames, comIDs, pCenter))
        {
            /* Push acquired names to list rows: */
            const QVector<QString> names = comNames.GetValues();
            const QVector<QString> ids = comIDs.GetValues();
            for (int i = 0; i < names.size(); ++i)
            {
                /* Create list item: */
                QListWidgetItem *pItem = new QListWidgetItem(names.at(i), pListWidget);
                if (pItem)
                {
                    pItem->setFlags(pItem->flags() & ~Qt::ItemIsEditable);
                    pItem->setData(Qt::UserRole, ids.at(i));
                }
            }
        }

        /* Choose the 1st one by default if possible: */
        if (pListWidget->count())
            pListWidget->setCurrentRow(0);

        /* Unblock signals after update: */
        pListWidget->blockSignals(false);
    }
    /* If source is local one: */
    else
    {
        /* Block signals while updating: */
        pListWidget->blockSignals(true);

        /* Clear combo initially: */
        pListWidget->clear();

        /* Unblock signals after update: */
        pListWidget->blockSignals(false);
    }
}

void UIWizardImportAppSource::refreshCloudStuff(CAppliance &comCloudAppliance,
                                                CVirtualSystemDescriptionForm &comCloudVsdImportForm,
                                                UIWizardImportApp *pWizard,
                                                const QString &strMachineId,
                                                const QString &strSource,
                                                const QString &strProfileName,
                                                bool fIsSourceCloudOne)
{
    /* Clear stuff: */
    comCloudAppliance = CAppliance();
    comCloudVsdImportForm = CVirtualSystemDescriptionForm();

    /* If source is NOT cloud one: */
    if (!fIsSourceCloudOne)
        return;

    /* We need top-level parent as well: */
    AssertPtrReturnVoid(pWizard);
    /* Acquire client: */
    CCloudClient comClient = cloudClientByName(strSource, strProfileName, pWizard->notificationCenter());
    AssertReturnVoid(comClient.isNotNull());

    /* Create appliance: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    CAppliance comAppliance = comVBox.CreateAppliance();
    if (!comVBox.isOk())
        return UINotificationMessage::cannotCreateAppliance(comVBox, pWizard->notificationCenter());

    /* Remember appliance: */
    comCloudAppliance = comAppliance;

    /* Read cloud instance info: */
    UINotificationProgressApplianceRead *pNotification = new UINotificationProgressApplianceRead(comCloudAppliance,
                                                                                                 QString("OCI://%1/%2").arg(strProfileName,
                                                                                                                            strMachineId));
    if (!pWizard->handleNotificationProgressNow(pNotification))
        return;

    /* Acquire virtual system description: */
    QVector<CVirtualSystemDescription> descriptions = comCloudAppliance.GetVirtualSystemDescriptions();
    if (!comCloudAppliance.isOk())
        return UINotificationMessage::cannotAcquireApplianceParameter(comCloudAppliance, pWizard->notificationCenter());

    /* Make sure there is at least one virtual system description created: */
    AssertReturnVoid(!descriptions.isEmpty());
    CVirtualSystemDescription comDescription = descriptions.at(0);

    /* Read Cloud Client description form: */
    CVirtualSystemDescriptionForm comVsdImportForm;
    bool fSuccess = importDescriptionForm(comClient, comDescription, comVsdImportForm, pWizard->notificationCenter());
    if (!fSuccess)
        return;

    /* Remember form: */
    comCloudVsdImportForm = comVsdImportForm;
}

QString UIWizardImportAppSource::path(UIEmptyFilePathSelector *pFileSelector)
{
    /* Sanity check: */
    AssertPtrReturn(pFileSelector, QString());

    /* Give the actual result: */
    return pFileSelector->path();
}

QString UIWizardImportAppSource::profileName(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturn(pCombo, QString());

    /* Give the actual result: */
    return pCombo->currentData(ProfileData_Name).toString();
}

QString UIWizardImportAppSource::machineId(QListWidget *pListWidget)
{
    /* Sanity check: */
    AssertPtrReturn(pListWidget, QString());

    /* Give the actual result: */
    QListWidgetItem *pItem = pListWidget->currentItem();
    return pItem ? pItem->data(Qt::UserRole).toString() : QString();
}

void UIWizardImportAppSource::updateSourceComboToolTip(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* Update tool-tip: */
    const QString strCurrentToolTip = pCombo->currentData(Qt::ToolTipRole).toString();
    pCombo->setToolTip(strCurrentToolTip);
}


/*********************************************************************************************************************************
*   Class UIWizardImportAppPageSource implementation.                                                                            *
*********************************************************************************************************************************/

UIWizardImportAppPageSource::UIWizardImportAppPageSource(bool fImportFromOCIByDefault, const QString &strFileName)
    : m_fImportFromOCIByDefault(fImportFromOCIByDefault)
    , m_strFileName(strFileName)
    , m_pLabelMain(0)
    , m_pLabelDescription(0)
    , m_pSourceLayout(0)
    , m_pSourceLabel(0)
    , m_pSourceComboBox(0)
    , m_pSettingsWidget1(0)
    , m_pLocalContainerLayout(0)
    , m_pFileLabel(0)
    , m_pFileSelector(0)
    , m_pCloudContainerLayout(0)
    , m_pProfileLabel(0)
    , m_pProfileComboBox(0)
    , m_pProfileToolButton(0)
    , m_pProfileInstanceLabel(0)
    , m_pProfileInstanceList(0)
{
    /* Prepare main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Prepare main label: */
        m_pLabelMain = new QIRichTextLabel(this);
        if (m_pLabelMain)
            pMainLayout->addWidget(m_pLabelMain);

        /* Prepare source layout: */
        m_pSourceLayout = new QGridLayout;
        if (m_pSourceLayout)
        {
            m_pSourceLayout->setContentsMargins(0, 0, 0, 0);
            m_pSourceLayout->setColumnStretch(0, 0);
            m_pSourceLayout->setColumnStretch(1, 1);

            /* Prepare source label: */
            m_pSourceLabel = new QLabel(this);
            if (m_pSourceLabel)
                m_pSourceLayout->addWidget(m_pSourceLabel, 0, 0, Qt::AlignRight);
            /* Prepare source selector: */
            m_pSourceComboBox = new QComboBox(this);
            if (m_pSourceComboBox)
            {
                m_pSourceLabel->setBuddy(m_pSourceComboBox);
                m_pSourceLayout->addWidget(m_pSourceComboBox, 0, 1);
            }

            /* Add into layout: */
            pMainLayout->addLayout(m_pSourceLayout);
        }

        /* Prepare description label: */
        m_pLabelDescription = new QIRichTextLabel(this);
        if (m_pLabelDescription)
            pMainLayout->addWidget(m_pLabelDescription);

        /* Prepare settings widget: */
        m_pSettingsWidget1 = new QStackedWidget(this);
        if (m_pSettingsWidget1)
        {
            /* Prepare local container: */
            QWidget *pContainerLocal = new QWidget(m_pSettingsWidget1);
            if (pContainerLocal)
            {
                /* Prepare local container layout: */
                m_pLocalContainerLayout = new QGridLayout(pContainerLocal);
                if (m_pLocalContainerLayout)
                {
                    m_pLocalContainerLayout->setContentsMargins(0, 0, 0, 0);
                    m_pLocalContainerLayout->setColumnStretch(0, 0);
                    m_pLocalContainerLayout->setColumnStretch(1, 1);
                    m_pLocalContainerLayout->setRowStretch(1, 1);

                    /* Prepare file label: */
                    m_pFileLabel = new QLabel(pContainerLocal);
                    if (m_pFileLabel)
                        m_pLocalContainerLayout->addWidget(m_pFileLabel, 0, 0, Qt::AlignRight);
                    /* Prepare file-path selector: */
                    m_pFileSelector = new UIEmptyFilePathSelector(pContainerLocal);
                    if (m_pFileSelector)
                    {
                        m_pFileLabel->setBuddy(m_pFileSelector);
                        m_pFileSelector->setHomeDir(uiCommon().documentsPath());
                        m_pFileSelector->setMode(UIEmptyFilePathSelector::Mode_File_Open);
                        m_pFileSelector->setButtonPosition(UIEmptyFilePathSelector::RightPosition);
                        m_pFileSelector->setEditable(true);
                        m_pLocalContainerLayout->addWidget(m_pFileSelector, 0, 1);
                    }
                }

                /* Add into widget: */
                m_pSettingsWidget1->addWidget(pContainerLocal);
            }

            /* Prepare cloud container: */
            QWidget *pContainerCloud = new QWidget(m_pSettingsWidget1);
            if (pContainerCloud)
            {
                /* Prepare cloud container layout: */
                m_pCloudContainerLayout = new QGridLayout(pContainerCloud);
                if (m_pCloudContainerLayout)
                {
                    m_pCloudContainerLayout->setContentsMargins(0, 0, 0, 0);
                    m_pCloudContainerLayout->setColumnStretch(0, 0);
                    m_pCloudContainerLayout->setColumnStretch(1, 1);
                    m_pCloudContainerLayout->setRowStretch(1, 0);
                    m_pCloudContainerLayout->setRowStretch(2, 1);

                    /* Prepare profile label: */
                    m_pProfileLabel = new QLabel(pContainerCloud);
                    if (m_pProfileLabel)
                        m_pCloudContainerLayout->addWidget(m_pProfileLabel, 0, 0, Qt::AlignRight);
                    /* Prepare sub-layout: */
                    QHBoxLayout *pSubLayout = new QHBoxLayout;
                    if (pSubLayout)
                    {
                        pSubLayout->setContentsMargins(0, 0, 0, 0);
                        pSubLayout->setSpacing(1);

                        /* Prepare profile combo-box: */
                        m_pProfileComboBox = new QComboBox(pContainerCloud);
                        if (m_pProfileComboBox)
                        {
                            m_pProfileLabel->setBuddy(m_pProfileComboBox);
                            pSubLayout->addWidget(m_pProfileComboBox);
                        }
                        /* Prepare profile tool-button: */
                        m_pProfileToolButton = new QIToolButton(pContainerCloud);
                        if (m_pProfileToolButton)
                        {
                            m_pProfileToolButton->setIcon(UIIconPool::iconSet(":/cloud_profile_manager_16px.png",
                                                                              ":/cloud_profile_manager_disabled_16px.png"));
                            pSubLayout->addWidget(m_pProfileToolButton);
                        }

                        /* Add into layout: */
                        m_pCloudContainerLayout->addLayout(pSubLayout, 0, 1);
                    }

                    /* Prepare profile instance label: */
                    m_pProfileInstanceLabel = new QLabel(pContainerCloud);
                    if (m_pProfileInstanceLabel)
                        m_pCloudContainerLayout->addWidget(m_pProfileInstanceLabel, 1, 0, Qt::AlignRight);
                    /* Prepare profile instances table: */
                    m_pProfileInstanceList = new QListWidget(pContainerCloud);
                    if (m_pProfileInstanceList)
                    {
                        m_pProfileInstanceLabel->setBuddy(m_pProfileInstanceLabel);
                        const QFontMetrics fm(m_pProfileInstanceList->font());
                        const int iFontWidth = fm.horizontalAdvance('x');
                        const int iTotalWidth = 50 * iFontWidth;
                        const int iFontHeight = fm.height();
                        const int iTotalHeight = 4 * iFontHeight;
                        m_pProfileInstanceList->setMinimumSize(QSize(iTotalWidth, iTotalHeight));
//                        m_pProfileInstanceList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
                        m_pProfileInstanceList->setAlternatingRowColors(true);
                        m_pCloudContainerLayout->addWidget(m_pProfileInstanceList, 1, 1, 2, 1);
                    }
                }

                /* Add into widget: */
                m_pSettingsWidget1->addWidget(pContainerCloud);
            }

            /* Add into layout: */
            pMainLayout->addWidget(m_pSettingsWidget1);
        }
    }

    /* Setup connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIWizardImportAppPageSource::sltHandleSourceComboChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIWizardImportAppPageSource::sltHandleSourceComboChange);
    connect(m_pSourceComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardImportAppPageSource::sltHandleSourceComboChange);
    connect(m_pFileSelector, &UIEmptyFilePathSelector::pathChanged,
            this, &UIWizardImportAppPageSource::completeChanged);
    connect(m_pProfileComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardImportAppPageSource::sltHandleProfileComboChange);
    connect(m_pProfileToolButton, &QIToolButton::clicked,
            this, &UIWizardImportAppPageSource::sltHandleProfileButtonClick);
    connect(m_pProfileInstanceList, &QListWidget::currentRowChanged,
            this, &UIWizardImportAppPageSource::completeChanged);

    /* Parse passed full group name if any: */
    if (   m_fImportFromOCIByDefault
        && !m_strFileName.isEmpty())
    {
        const QString strProviderShortName = m_strFileName.section('/', 1, 1);
        const QString strProfileName = m_strFileName.section('/', 2, 2);
        if (!strProviderShortName.isEmpty() && !strProfileName.isEmpty())
        {
            m_strSource = strProviderShortName;
            m_strProfileName = strProfileName;
        }
    }
}

UIWizardImportApp *UIWizardImportAppPageSource::wizard() const
{
    return qobject_cast<UIWizardImportApp*>(UINativeWizardPage::wizard());
}

void UIWizardImportAppPageSource::sltRetranslateUI()
{
    /* Translate page: */
    setTitle(UIWizardImportApp::tr("Appliance to import"));

    /* Translate main label: */
    if (m_pLabelMain)
        m_pLabelMain->setText(UIWizardImportApp::tr("Please choose the source to import appliance from.  This can be a "
                                                    "local file system to import OVF archive or one of known cloud "
                                                    "service providers to import cloud VM from."));

    /* Translate description label: */
    if (m_pLabelDescription)
    {
        if (wizard()->isSourceCloudOne())
            m_pLabelDescription->setText(UIWizardImportApp::
                                         tr("Please choose one of cloud service profiles you have registered to import virtual "
                                            "machine from.  Corresponding machines list will be updated.  To continue, "
                                            "select one of machines to import below."));
        else
            m_pLabelDescription->setText(UIWizardImportApp::
                                         tr("Please choose a file to import the virtual appliance from.  VirtualBox currently "
                                            "supports importing appliances saved in the Open Virtualization Format (OVF).  "
                                            "To continue, select the file to import below."));
    }

    /* Translate source label: */
    if (m_pSourceLabel)
        m_pSourceLabel->setText(UIWizardImportApp::tr("&Source:"));
    if (m_pSourceComboBox)
    {
        /* Translate hardcoded values of Source combo-box: */
        m_pSourceComboBox->setItemText(0, UIWizardImportApp::tr("Local File System"));
        m_pSourceComboBox->setItemData(0, UIWizardImportApp::tr("Import from local file system."), Qt::ToolTipRole);

        /* Translate received values of Source combo-box.
         * We are enumerating starting from 0 for simplicity: */
        for (int i = 0; i < m_pSourceComboBox->count(); ++i)
            if (isSourceCloudOne(m_pSourceComboBox, i))
            {
                m_pSourceComboBox->setItemText(i, m_pSourceComboBox->itemData(i, SourceData_Name).toString());
                m_pSourceComboBox->setItemData(i, UIWizardImportApp::tr("Import from cloud service provider."), Qt::ToolTipRole);
            }
    }

    /* Translate local stuff: */
    if (m_pFileLabel)
        m_pFileLabel->setText(UIWizardImportApp::tr("&File:"));
    if (m_pFileSelector)
    {
        m_pFileSelector->setLineEditToolTip(UIWizardImportApp::tr("Holds the path of the file selected for import."));
        m_pFileSelector->setChooseButtonToolTip(UIWizardImportApp::tr("Choose a virtual appliance file to import..."));
        m_pFileSelector->setFileDialogTitle(UIWizardImportApp::tr("Please choose a virtual appliance file to import"));
        m_pFileSelector->setFileFilters(UIWizardImportApp::tr("Open Virtualization Format (%1)").arg("*.ova *.ovf"));
    }

    /* Translate profile stuff: */
    if (m_pProfileLabel)
        m_pProfileLabel->setText(UIWizardImportApp::tr("&Profile:"));
    if (m_pProfileComboBox)
        m_pProfileComboBox->setToolTip(UIWizardImportApp::tr("Selects cloud profile."));
    if (m_pProfileToolButton)
    {
        m_pProfileToolButton->setText(UIWizardImportApp::tr("Cloud Profile Manager"));
        m_pProfileToolButton->setToolTip(UIWizardImportApp::tr("Opens cloud profile manager..."));
    }
    if (m_pProfileInstanceLabel)
        m_pProfileInstanceLabel->setText(UIWizardImportApp::tr("&Machines:"));

    /* Adjust label widths: */
    QList<QWidget*> labels;
    if (m_pSourceLabel)
        labels << m_pSourceLabel;
    if (m_pFileLabel)
        labels << m_pFileLabel;
    if (m_pProfileLabel)
        labels << m_pProfileLabel;
    if (m_pProfileInstanceLabel)
        labels << m_pProfileInstanceLabel;
    int iMaxWidth = 0;
    foreach (QWidget *pLabel, labels)
        iMaxWidth = qMax(iMaxWidth, pLabel->minimumSizeHint().width());
    if (m_pSourceLayout)
        m_pSourceLayout->setColumnMinimumWidth(0, iMaxWidth);
    if (   m_pLocalContainerLayout
        && m_pCloudContainerLayout)
    {
        m_pLocalContainerLayout->setColumnMinimumWidth(0, iMaxWidth);
        m_pCloudContainerLayout->setColumnMinimumWidth(0, iMaxWidth);
    }

    /* Update tool-tips: */
    updateSourceComboToolTip(m_pSourceComboBox);
}

void UIWizardImportAppPageSource::initializePage()
{
    /* Populate sources: */
    populateSources(m_pSourceComboBox,
                    wizard()->notificationCenter(),
                    m_fImportFromOCIByDefault,
                    m_strSource);
    /* Translate page: */
    sltRetranslateUI();

    /* Choose initially focused widget: */
    if (wizard()->isSourceCloudOne())
        m_pProfileInstanceList->setFocus();
    else
        m_pFileSelector->setFocus();

    /* Fetch it, asynchronously: */
    QMetaObject::invokeMethod(this, "sltHandleSourceComboChange", Qt::QueuedConnection);
}

bool UIWizardImportAppPageSource::isComplete() const
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud source selected: */
    if (wizard()->isSourceCloudOne())
        fResult = !machineId(m_pProfileInstanceList).isEmpty();
    else
        fResult =    UICommon::hasAllowedExtension(path(m_pFileSelector), OVFFileExts)
                  && QFile::exists(path(m_pFileSelector));

    /* Return result: */
    return fResult;
}

bool UIWizardImportAppPageSource::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud source selected: */
    if (wizard()->isSourceCloudOne())
    {
        /* Update cloud stuff: */
        updateCloudStuff();
        /* Which is required to continue to the next page: */
        fResult =    wizard()->cloudAppliance().isNotNull()
                  && wizard()->vsdImportForm().isNotNull();
    }
    else
    {
        /* Update local stuff (only if something changed): */
        if (m_pFileSelector->isModified())
        {
            updateLocalStuff();
            m_pFileSelector->resetModified();
        }
        /* Which is required to continue to the next page: */
        fResult = wizard()->localAppliance().isNotNull();
    }

    /* Return result: */
    return fResult;
}

void UIWizardImportAppPageSource::sltHandleSourceComboChange()
{
    /* Update combo tool-tip: */
    updateSourceComboToolTip(m_pSourceComboBox);

    /* Update wizard fields: */
    wizard()->setSourceCloudOne(isSourceCloudOne(m_pSourceComboBox));

    /* Refresh page widgets: */
    refreshStackedWidget(m_pSettingsWidget1,
                         wizard()->isSourceCloudOne());
    refreshProfileCombo(m_pProfileComboBox,
                        wizard()->notificationCenter(),
                        source(m_pSourceComboBox),
                        m_strProfileName,
                        wizard()->isSourceCloudOne());

    /* Update profile instances: */
    sltHandleProfileComboChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardImportAppPageSource::sltHandleProfileComboChange()
{
    /* Refresh required settings: */
    refreshCloudProfileInstances(m_pProfileInstanceList,
                                 wizard()->notificationCenter(),
                                 source(m_pSourceComboBox),
                                 profileName(m_pProfileComboBox),
                                 wizard()->isSourceCloudOne());

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardImportAppPageSource::sltHandleProfileButtonClick()
{
    /* Open Cloud Profile Manager: */
    if (gpManager)
        gpManager->openCloudProfileManager();
}

void UIWizardImportAppPageSource::updateLocalStuff()
{
    /* Create local appliance: */
    wizard()->setFile(path(m_pFileSelector));
}

void UIWizardImportAppPageSource::updateCloudStuff()
{
    /* Create cloud appliance and VSD import form: */
    CAppliance comAppliance;
    CVirtualSystemDescriptionForm comForm;
    refreshCloudStuff(comAppliance,
                      comForm,
                      wizard(),
                      machineId(m_pProfileInstanceList),
                      source(m_pSourceComboBox),
                      profileName(m_pProfileComboBox),
                      wizard()->isSourceCloudOne());
    wizard()->setCloudAppliance(comAppliance);
    wizard()->setVsdImportForm(comForm);
}
