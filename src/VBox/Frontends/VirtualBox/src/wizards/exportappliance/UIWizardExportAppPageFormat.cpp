/* $Id: UIWizardExportAppPageFormat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportAppPageFormat class implementation.
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
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

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
#include "UIWizardExportApp.h"
#include "UIWizardExportAppPageFormat.h"

/* COM includes: */
#include "CMachine.h"
#include "CSystemProperties.h"

/* Namespaces: */
using namespace UIWizardExportAppFormat;


/*********************************************************************************************************************************
*   Class UIWizardExportAppFormat implementation.                                                                                *
*********************************************************************************************************************************/

void UIWizardExportAppFormat::populateFormats(QComboBox *pCombo, UINotificationCenter *pCenter, bool fExportToOCIByDefault)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* Remember current item data to be able to restore it: */
    QString strOldData;
    if (pCombo->currentIndex() != -1)
        strOldData = pCombo->currentData(FormatData_ShortName).toString();
    else
    {
        /* Otherwise "OCI" or "ovf-1.0" should be the default one: */
        if (fExportToOCIByDefault)
            strOldData = "OCI";
        else
            strOldData = "ovf-1.0";
    }

    /* Block signals while updating: */
    pCombo->blockSignals(true);

    /* Clear combo initially: */
    pCombo->clear();

    /* Compose hardcoded format list: */
    QStringList formats;
    formats << "ovf-0.9";
    formats << "ovf-1.0";
    formats << "ovf-2.0";
    /* Add that list to combo: */
    foreach (const QString &strShortName, formats)
    {
        /* Compose empty item, fill it's data: */
        pCombo->addItem(QString());
        pCombo->setItemData(pCombo->count() - 1, strShortName, FormatData_ShortName);
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
        pCombo->setItemData(pCombo->count() - 1, strProviderName,      FormatData_Name);
        pCombo->setItemData(pCombo->count() - 1, strProviderShortName, FormatData_ShortName);
        pCombo->setItemData(pCombo->count() - 1, true,                 FormatData_IsItCloudFormat);
    }

    /* Set previous/default item if possible: */
    int iNewIndex = -1;
    if (   iNewIndex == -1
        && !strOldData.isNull())
        iNewIndex = pCombo->findData(strOldData, FormatData_ShortName);
    if (   iNewIndex == -1
        && pCombo->count() > 0)
        iNewIndex = 0;
    if (iNewIndex != -1)
        pCombo->setCurrentIndex(iNewIndex);

    /* Unblock signals after update: */
    pCombo->blockSignals(false);
}

void UIWizardExportAppFormat::populateMACAddressPolicies(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);
    /* We need top-level parent as well: */
    QWidget *pParent = pCombo->window();
    AssertPtrReturnVoid(pParent);

    /* Map known export options to known MAC address export policies: */
    QMap<KExportOptions, MACAddressExportPolicy> knownOptions;
    knownOptions[KExportOptions_StripAllMACs] = MACAddressExportPolicy_StripAllMACs;
    knownOptions[KExportOptions_StripAllNonNATMACs] = MACAddressExportPolicy_StripAllNonNATMACs;
    /* Load currently supported export options: */
    const QVector<KExportOptions> supportedOptions =
        gpGlobalSession->virtualBox().GetSystemProperties().GetSupportedExportOptions();
    /* Check which of supported options/policies are known: */
    QList<MACAddressExportPolicy> supportedPolicies;
    foreach (const KExportOptions &enmOption, supportedOptions)
        if (knownOptions.contains(enmOption))
            supportedPolicies << knownOptions.value(enmOption);
    /* Remember current item data to be able to restore it: */
    MACAddressExportPolicy enmOldData = MACAddressExportPolicy_MAX;
    if (pCombo->currentIndex() != -1)
        enmOldData = pCombo->currentData().value<MACAddressExportPolicy>();
    else
    {
        if (supportedPolicies.contains(MACAddressExportPolicy_StripAllNonNATMACs))
            enmOldData = MACAddressExportPolicy_StripAllNonNATMACs;
        else
            enmOldData = MACAddressExportPolicy_KeepAllMACs;
    }

    /* Block signals while updating: */
    pCombo->blockSignals(true);

    /* Clear combo initially: */
    pCombo->clear();

    /* Add supported policies first: */
    foreach (const MACAddressExportPolicy &enmPolicy, supportedPolicies)
        pCombo->addItem(QString(), QVariant::fromValue(enmPolicy));

    /* Add hardcoded policy finally: */
    pCombo->addItem(QString(), QVariant::fromValue(MACAddressExportPolicy_KeepAllMACs));

    /* Set previous/default item if possible: */
    int iNewIndex = -1;
    if (   iNewIndex == -1
        && enmOldData != MACAddressExportPolicy_MAX)
        iNewIndex = pCombo->findData(QVariant::fromValue(enmOldData));
    if (   iNewIndex == -1
        && pCombo->count() > 0)
        iNewIndex = 0;
    if (iNewIndex != -1)
        pCombo->setCurrentIndex(iNewIndex);

    /* Unblock signals after update: */
    pCombo->blockSignals(false);
}

QString UIWizardExportAppFormat::format(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturn(pCombo, QString());

    /* Give the actual result: */
    return pCombo->currentData(FormatData_ShortName).toString();
}

bool UIWizardExportAppFormat::isFormatCloudOne(QComboBox *pCombo, int iIndex /* = -1 */)
{
    /* Sanity check: */
    AssertPtrReturn(pCombo, false);

    /* Handle special case, -1 means "current one": */
    if (iIndex == -1)
        iIndex = pCombo->currentIndex();

    /* Give the actual result: */
    return pCombo->itemData(iIndex, FormatData_IsItCloudFormat).toBool();
}

void UIWizardExportAppFormat::refreshStackedWidget(QStackedWidget *pStackedWidget, bool fIsFormatCloudOne)
{
    /* Update stack appearance according to chosen format: */
    pStackedWidget->setCurrentIndex((int)fIsFormatCloudOne);
}

void UIWizardExportAppFormat::refreshFileSelectorName(QString &strFileSelectorName,
                                                      const QStringList &machineNames,
                                                      const QString &strDefaultApplianceName,
                                                      bool fIsFormatCloudOne)
{
    /* If format is cloud one: */
    if (fIsFormatCloudOne)
    {
        /* We use no name: */
        strFileSelectorName.clear();
    }
    /* If format is local one: */
    else
    {
        /* If it's one VM only, we use the VM name as file-name: */
        if (machineNames.size() == 1)
            strFileSelectorName = machineNames.first();
        /* Otherwise => we use the default file-name: */
        else
            strFileSelectorName = strDefaultApplianceName;
    }
}

void UIWizardExportAppFormat::refreshFileSelectorExtension(QString &strFileSelectorExt,
                                                           UIEmptyFilePathSelector *pFileSelector,
                                                           bool fIsFormatCloudOne)
{
    /* If format is cloud one: */
    if (fIsFormatCloudOne)
    {
        /* We use no extension: */
        strFileSelectorExt.clear();

        /* Update file chooser accordingly: */
        pFileSelector->setFileFilters(QString());
    }
    /* If format is local one: */
    else
    {
        /* We use the default (.ova) extension: */
        strFileSelectorExt = ".ova";

        /* Update file chooser accordingly: */
        pFileSelector->setFileFilters(UIWizardExportApp::tr("Open Virtualization Format Archive (%1)").arg("*.ova") + ";;" +
                                      UIWizardExportApp::tr("Open Virtualization Format (%1)").arg("*.ovf"));
    }
}

void UIWizardExportAppFormat::refreshFileSelectorPath(UIEmptyFilePathSelector *pFileSelector,
                                                      const QString &strFileSelectorName,
                                                      const QString &strFileSelectorExt,
                                                      bool fIsFormatCloudOne)
{
    /* If format is cloud one: */
    if (fIsFormatCloudOne)
    {
        /* Clear file selector path: */
        pFileSelector->setPath(QString());
    }
    /* If format is local one: */
    else
    {
        /* Compose file selector path: */
        const QString strPath = QDir::toNativeSeparators(QString("%1/%2")
                                                         .arg(uiCommon().documentsPath())
                                                         .arg(strFileSelectorName + strFileSelectorExt));
        pFileSelector->setPath(strPath);
    }
}

void UIWizardExportAppFormat::refreshManifestCheckBoxAccess(QCheckBox *pCheckBox,
                                                            bool fIsFormatCloudOne)
{
    /* If format is cloud one: */
    if (fIsFormatCloudOne)
    {
        /* Disable manifest check-box: */
        pCheckBox->setChecked(false);
        pCheckBox->setEnabled(false);
    }
    /* If format is local one: */
    else
    {
        /* Enable and select manifest check-box: */
        pCheckBox->setChecked(true);
        pCheckBox->setEnabled(true);
    }
}

void UIWizardExportAppFormat::refreshIncludeISOsCheckBoxAccess(QCheckBox *pCheckBox,
                                                               bool fIsFormatCloudOne)
{
    /* If format is cloud one: */
    if (fIsFormatCloudOne)
    {
        /* Disable include ISO check-box: */
        pCheckBox->setChecked(false);
        pCheckBox->setEnabled(false);
    }
    /* If format is local one: */
    else
    {
        /* Enable include ISO check-box: */
        pCheckBox->setEnabled(true);
    }
}

void UIWizardExportAppFormat::refreshLocalStuff(CAppliance &comLocalAppliance,
                                                UIWizardExportApp *pWizard,
                                                const QList<QUuid> &machineIDs,
                                                const QString &strUri)
{
    /* Clear stuff: */
    comLocalAppliance = CAppliance();

    /* Create appliance: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    CAppliance comAppliance = comVBox.CreateAppliance();
    if (!comVBox.isOk())
        return UINotificationMessage::cannotCreateAppliance(comVBox, pWizard->notificationCenter());

    /* Remember appliance: */
    comLocalAppliance = comAppliance;

    /* Iterate over all the selected machine uuids: */
    foreach (const QUuid &uMachineId, machineIDs)
    {
        /* Get the machine with the uMachineId: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        CMachine comMachine = comVBox.FindMachine(uMachineId.toString());
        if (!comVBox.isOk())
            return UINotificationMessage::cannotFindMachineById(comVBox, uMachineId, pWizard->notificationCenter());
        /* Add the export description to our appliance object: */
        CVirtualSystemDescription comVsd = comMachine.ExportTo(comLocalAppliance, strUri);
        if (!comMachine.isOk())
            return UINotificationMessage::cannotExportMachine(comMachine, pWizard->notificationCenter());
        /* Add some additional fields the user may change: */
        comVsd.AddDescription(KVirtualSystemDescriptionType_Product, "", "");
        comVsd.AddDescription(KVirtualSystemDescriptionType_ProductUrl, "", "");
        comVsd.AddDescription(KVirtualSystemDescriptionType_Vendor, "", "");
        comVsd.AddDescription(KVirtualSystemDescriptionType_VendorUrl, "", "");
        comVsd.AddDescription(KVirtualSystemDescriptionType_Version, "", "");
        comVsd.AddDescription(KVirtualSystemDescriptionType_License, "", "");
    }
}

void UIWizardExportAppFormat::refreshProfileCombo(QComboBox *pCombo,
                                                  UINotificationCenter *pCenter,
                                                  const QString &strFormat,
                                                  bool fIsFormatCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* If format is cloud one: */
    if (fIsFormatCloudOne)
    {
        /* Acquire provider: */
        CCloudProvider comProvider = cloudProviderByShortName(strFormat, pCenter);
        AssertReturnVoid(comProvider.isNotNull());

        /* Remember current item data to be able to restore it: */
        QString strOldData;
        if (pCombo->currentIndex() != -1)
            strOldData = pCombo->currentData(ProfileData_Name).toString();

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
            const QString strFullProfileName = QString("/%1/%2").arg(strFormat).arg(strCurrentProfileName);
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
    /* If format is local one: */
    else
    {
        /* Block signals while updating: */
        pCombo->blockSignals(true);

        /* Clear combo: */
        pCombo->clear();

        /* Unblock signals after update: */
        pCombo->blockSignals(false);
    }
}

void UIWizardExportAppFormat::refreshCloudProfile(CCloudProfile &comCloudProfile,
                                                  UINotificationCenter *pCenter,
                                                  const QString &strShortProviderName,
                                                  const QString &strProfileName,
                                                  bool fIsFormatCloudOne)
{
    /* If format is cloud one: */
    if (fIsFormatCloudOne)
        comCloudProfile = cloudProfileByName(strShortProviderName, strProfileName, pCenter);
    /* If format is local one: */
    else
        comCloudProfile = CCloudProfile();
}

void UIWizardExportAppFormat::refreshCloudExportMode(const QMap<CloudExportMode, QAbstractButton*> &radios,
                                                     bool fIsFormatCloudOne)
{
    /* If format is cloud one: */
    if (fIsFormatCloudOne)
    {
        /* Check if something already chosen: */
        bool fSomethingChosen = false;
        foreach (QAbstractButton *pButton, radios.values())
            if (pButton->isChecked())
                fSomethingChosen = true;
        /* Choose default cloud export option: */
        if (!fSomethingChosen)
            radios.value(CloudExportMode_ExportThenAsk)->setChecked(true);
    }
    /* If format is local one: */
    else
    {
        /* Make sure nothing chosen: */
        foreach (QAbstractButton *pButton, radios.values())
            pButton->setChecked(false);
    }
}

void UIWizardExportAppFormat::refreshCloudStuff(CAppliance &comCloudAppliance,
                                                CCloudClient &comCloudClient,
                                                CVirtualSystemDescription &comCloudVsd,
                                                CVirtualSystemDescriptionForm &comCloudVsdExportForm,
                                                UIWizardExportApp *pWizard,
                                                const CCloudProfile &comCloudProfile,
                                                const QList<QUuid> &machineIDs,
                                                const QString &strUri,
                                                const CloudExportMode enmCloudExportMode)
{
    /* Clear stuff: */
    comCloudAppliance = CAppliance();
    comCloudClient = CCloudClient();
    comCloudVsd = CVirtualSystemDescription();
    comCloudVsdExportForm = CVirtualSystemDescriptionForm();

    /* Sanity check: */
    if (comCloudProfile.isNull())
        return;
    if (machineIDs.isEmpty())
        return;

    /* Perform cloud export procedure for first uuid only: */
    const QUuid uMachineId = machineIDs.first();

    /* Get the machine with the uMachineId: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    CMachine comMachine = comVBox.FindMachine(uMachineId.toString());
    if (!comVBox.isOk())
        return UINotificationMessage::cannotFindMachineById(comVBox, uMachineId, pWizard->notificationCenter());

    /* Create appliance: */
    CAppliance comAppliance = comVBox.CreateAppliance();
    if (!comVBox.isOk())
        return UINotificationMessage::cannotCreateAppliance(comVBox, pWizard->notificationCenter());

    /* Remember appliance: */
    comCloudAppliance = comAppliance;

    /* Add the export virtual system description to our appliance object: */
    CVirtualSystemDescription comVsd = comMachine.ExportTo(comCloudAppliance, strUri);
    if (!comMachine.isOk())
        return UINotificationMessage::cannotExportMachine(comMachine, pWizard->notificationCenter());

    /* Remember description: */
    comCloudVsd = comVsd;

    /* Add Launch Instance flag to virtual system description: */
    switch (enmCloudExportMode)
    {
        case CloudExportMode_AskThenExport:
        case CloudExportMode_ExportThenAsk:
            comCloudVsd.AddDescription(KVirtualSystemDescriptionType_CloudLaunchInstance, "true", QString());
            break;
        default:
            comCloudVsd.AddDescription(KVirtualSystemDescriptionType_CloudLaunchInstance, "false", QString());
            break;
    }
    if (!comCloudVsd.isOk())
        return UINotificationMessage::cannotChangeVirtualSystemDescriptionParameter(comCloudVsd, pWizard->notificationCenter());

    /* Create Cloud Client: */
    CCloudClient comClient = cloudClient(comCloudProfile);
    if (comClient.isNull())
        return;

    /* Remember client: */
    comCloudClient = comClient;

    /* Read Cloud Client Export description form: */
    CVirtualSystemDescriptionForm comVsdExportForm;
    bool fResult = exportDescriptionForm(comCloudClient, comCloudVsd, comVsdExportForm, pWizard->notificationCenter());
    if (!fResult)
        return;

    /* Remember export description form: */
    comCloudVsdExportForm = comVsdExportForm;
}

QString UIWizardExportAppFormat::profileName(QComboBox *pCombo)
{
    return pCombo->currentData(ProfileData_Name).toString();
}

void UIWizardExportAppFormat::updateFormatComboToolTip(QComboBox *pCombo)
{
    AssertPtrReturnVoid(pCombo);
    QString strCurrentToolTip;
    if (pCombo->count() != 0)
    {
        strCurrentToolTip = pCombo->currentData(Qt::ToolTipRole).toString();
        AssertMsg(!strCurrentToolTip.isEmpty(), ("Data not found!"));
    }
    pCombo->setToolTip(strCurrentToolTip);
}

void UIWizardExportAppFormat::updateMACAddressExportPolicyComboToolTip(QComboBox *pCombo)
{
    AssertPtrReturnVoid(pCombo);
    QString strCurrentToolTip;
    if (pCombo->count() != 0)
    {
        strCurrentToolTip = pCombo->currentData(Qt::ToolTipRole).toString();
        AssertMsg(!strCurrentToolTip.isEmpty(), ("Data not found!"));
    }
    pCombo->setToolTip(strCurrentToolTip);
}


/*********************************************************************************************************************************
*   Class UIWizardExportAppPageFormat implementation.                                                                            *
*********************************************************************************************************************************/

UIWizardExportAppPageFormat::UIWizardExportAppPageFormat(bool fExportToOCIByDefault)
    : m_fExportToOCIByDefault(fExportToOCIByDefault)
    , m_pLabelFormat(0)
    , m_pLabelSettings(0)
    , m_pFormatLayout(0)
    , m_pFormatComboBoxLabel(0)
    , m_pFormatComboBox(0)
    , m_pSettingsWidget1(0)
    , m_pSettingsLayout1(0)
    , m_pFileSelectorLabel(0)
    , m_pFileSelector(0)
    , m_pMACComboBoxLabel(0)
    , m_pMACComboBox(0)
    , m_pAdditionalLabel(0)
    , m_pManifestCheckbox(0)
    , m_pIncludeISOsCheckbox(0)
    , m_pSettingsLayout2(0)
    , m_pProfileLabel(0)
    , m_pProfileComboBox(0)
    , m_pProfileToolButton(0)
    , m_pExportModeLabel(0)
    , m_pExportModeButtonGroup(0)
{
    /* Create main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Create format label: */
        m_pLabelFormat = new QIRichTextLabel(this);
        if (m_pLabelFormat)
            pMainLayout->addWidget(m_pLabelFormat);

        /* Create format layout: */
        m_pFormatLayout = new QGridLayout;
        if (m_pFormatLayout)
        {
#ifdef VBOX_WS_MAC
            m_pFormatLayout->setContentsMargins(0, 10, 0, 10);
            m_pFormatLayout->setSpacing(10);
#else
            m_pFormatLayout->setContentsMargins(0, qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin),
                                                0, qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin));
#endif
            m_pFormatLayout->setColumnStretch(0, 0);
            m_pFormatLayout->setColumnStretch(1, 1);

            /* Create format combo-box label: */
            m_pFormatComboBoxLabel = new QLabel(this);
            if (m_pFormatComboBoxLabel)
            {
                m_pFormatComboBoxLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
                m_pFormatLayout->addWidget(m_pFormatComboBoxLabel, 0, 0);
            }
            /* Create format combo-box: */
            m_pFormatComboBox = new QComboBox(this);
            if (m_pFormatComboBox)
            {
                m_pFormatComboBoxLabel->setBuddy(m_pFormatComboBox);
                m_pFormatLayout->addWidget(m_pFormatComboBox, 0, 1);
            }

            /* Add into layout: */
            pMainLayout->addLayout(m_pFormatLayout);
        }

        /* Create settings label: */
        m_pLabelSettings = new QIRichTextLabel(this);
        if (m_pLabelSettings)
            pMainLayout->addWidget(m_pLabelSettings);

        /* Create settings widget 1: */
        m_pSettingsWidget1 = new QStackedWidget(this);
        if (m_pSettingsWidget1)
        {
            /* Create settings pane 1: */
            QWidget *pSettingsPane1 = new QWidget(m_pSettingsWidget1);
            if (pSettingsPane1)
            {
                /* Create settings layout 1: */
                m_pSettingsLayout1 = new QGridLayout(pSettingsPane1);
                if (m_pSettingsLayout1)
                {
#ifdef VBOX_WS_MAC
                    m_pSettingsLayout1->setContentsMargins(0, 10, 0, 10);
                    m_pSettingsLayout1->setSpacing(10);
#else
                    m_pSettingsLayout1->setContentsMargins(0, qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin),
                                                           0, qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin));
#endif
                    m_pSettingsLayout1->setColumnStretch(0, 0);
                    m_pSettingsLayout1->setColumnStretch(1, 1);

                    /* Create file selector: */
                    m_pFileSelector = new UIEmptyFilePathSelector(pSettingsPane1);
                    if (m_pFileSelector)
                    {
                        m_pFileSelector->setMode(UIEmptyFilePathSelector::Mode_File_Save);
                        m_pFileSelector->setEditable(true);
                        m_pFileSelector->setButtonPosition(UIEmptyFilePathSelector::RightPosition);
                        m_pFileSelector->setDefaultSaveExt("ova");
                        m_pSettingsLayout1->addWidget(m_pFileSelector, 0, 1, 1, 2);
                    }
                    /* Create file selector label: */
                    m_pFileSelectorLabel = new QLabel(pSettingsPane1);
                    if (m_pFileSelectorLabel)
                    {
                        m_pFileSelectorLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
                        m_pFileSelectorLabel->setBuddy(m_pFileSelector);
                        m_pSettingsLayout1->addWidget(m_pFileSelectorLabel, 0, 0);
                    }

                    /* Create MAC policy combo-box: */
                    m_pMACComboBox = new QComboBox(pSettingsPane1);
                    if (m_pMACComboBox)
                        m_pSettingsLayout1->addWidget(m_pMACComboBox, 1, 1, 1, 2);
                    /* Create format combo-box label: */
                    m_pMACComboBoxLabel = new QLabel(pSettingsPane1);
                    if (m_pMACComboBoxLabel)
                    {
                        m_pMACComboBoxLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
                        m_pMACComboBoxLabel->setBuddy(m_pMACComboBox);
                        m_pSettingsLayout1->addWidget(m_pMACComboBoxLabel, 1, 0);
                    }

                    /* Create advanced label: */
                    m_pAdditionalLabel = new QLabel(pSettingsPane1);
                    if (m_pAdditionalLabel)
                    {
                        m_pAdditionalLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
                        m_pSettingsLayout1->addWidget(m_pAdditionalLabel, 2, 0);
                    }
                    /* Create manifest check-box: */
                    m_pManifestCheckbox = new QCheckBox(pSettingsPane1);
                    if (m_pManifestCheckbox)
                        m_pSettingsLayout1->addWidget(m_pManifestCheckbox, 2, 1);
                    /* Create include ISOs check-box: */
                    m_pIncludeISOsCheckbox = new QCheckBox(pSettingsPane1);
                    if (m_pIncludeISOsCheckbox)
                        m_pSettingsLayout1->addWidget(m_pIncludeISOsCheckbox, 3, 1);

                    /* Create placeholder: */
                    QWidget *pPlaceholder = new QWidget(pSettingsPane1);
                    if (pPlaceholder)
                        m_pSettingsLayout1->addWidget(pPlaceholder, 4, 0, 1, 3);
                }

                /* Add into layout: */
                m_pSettingsWidget1->addWidget(pSettingsPane1);
            }

            /* Create settings pane 2: */
            QWidget *pSettingsPane2 = new QWidget(m_pSettingsWidget1);
            if (pSettingsPane2)
            {
                /* Create settings layout 2: */
                m_pSettingsLayout2 = new QGridLayout(pSettingsPane2);
                if (m_pSettingsLayout2)
                {
#ifdef VBOX_WS_MAC
                    m_pSettingsLayout2->setContentsMargins(0, 10, 0, 10);
                    m_pSettingsLayout2->setSpacing(10);

#else
                    m_pSettingsLayout2->setContentsMargins(0, qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin),
                                                           0, qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin));
#endif
                    m_pSettingsLayout2->setColumnStretch(0, 0);
                    m_pSettingsLayout2->setColumnStretch(1, 1);
                    m_pSettingsLayout2->setRowStretch(4, 1);

                    /* Create profile label: */
                    m_pProfileLabel = new QLabel(pSettingsPane2);
                    if (m_pProfileLabel)
                    {
                        m_pProfileLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
                        m_pSettingsLayout2->addWidget(m_pProfileLabel, 0, 0);
                    }
                    /* Create sub-layout: */
                    QHBoxLayout *pSubLayout = new QHBoxLayout;
                    if (pSubLayout)
                    {
                        pSubLayout->setContentsMargins(0, 0, 0, 0);
                        pSubLayout->setSpacing(1);

                        /* Create profile combo-box: */
                        m_pProfileComboBox = new QComboBox(pSettingsPane2);
                        if (m_pProfileComboBox)
                        {
                            m_pProfileLabel->setBuddy(m_pProfileComboBox);
                            pSubLayout->addWidget(m_pProfileComboBox);
                        }
                        /* Create profile tool-button: */
                        m_pProfileToolButton = new QIToolButton(pSettingsPane2);
                        if (m_pProfileToolButton)
                        {
                            m_pProfileToolButton->setIcon(UIIconPool::iconSet(":/cloud_profile_manager_16px.png",
                                                                              ":/cloud_profile_manager_disabled_16px.png"));
                            pSubLayout->addWidget(m_pProfileToolButton);
                        }

                        /* Add into layout: */
                        m_pSettingsLayout2->addLayout(pSubLayout, 0, 1);
                    }

                    /* Create profile label: */
                    m_pExportModeLabel = new QLabel(pSettingsPane2);
                    if (m_pExportModeLabel)
                    {
                        m_pExportModeLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
                        m_pSettingsLayout2->addWidget(m_pExportModeLabel, 1, 0);
                    }

                    /* Create button-group: */
                    m_pExportModeButtonGroup = new QButtonGroup(pSettingsPane2);
                    if (m_pExportModeButtonGroup)
                    {
                        /* Create Do Not Ask button: */
                        m_exportModeButtons[CloudExportMode_DoNotAsk] = new QRadioButton(pSettingsPane2);
                        if (m_exportModeButtons.value(CloudExportMode_DoNotAsk))
                        {
                            m_pExportModeButtonGroup->addButton(m_exportModeButtons.value(CloudExportMode_DoNotAsk));
                            m_pSettingsLayout2->addWidget(m_exportModeButtons.value(CloudExportMode_DoNotAsk), 1, 1);
                        }
                        /* Create Ask Then Export button: */
                        m_exportModeButtons[CloudExportMode_AskThenExport] = new QRadioButton(pSettingsPane2);
                        if (m_exportModeButtons.value(CloudExportMode_AskThenExport))
                        {
                            m_pExportModeButtonGroup->addButton(m_exportModeButtons.value(CloudExportMode_AskThenExport));
                            m_pSettingsLayout2->addWidget(m_exportModeButtons.value(CloudExportMode_AskThenExport), 2, 1);
                        }
                        /* Create Export Then Ask button: */
                        m_exportModeButtons[CloudExportMode_ExportThenAsk] = new QRadioButton(pSettingsPane2);
                        if (m_exportModeButtons.value(CloudExportMode_ExportThenAsk))
                        {
                            m_pExportModeButtonGroup->addButton(m_exportModeButtons.value(CloudExportMode_ExportThenAsk));
                            m_pSettingsLayout2->addWidget(m_exportModeButtons.value(CloudExportMode_ExportThenAsk), 3, 1);
                        }
                    }
                }

                /* Add into layout: */
                m_pSettingsWidget1->addWidget(pSettingsPane2);
            }

            /* Add into layout: */
            pMainLayout->addWidget(m_pSettingsWidget1);
        }
    }

    /* Setup connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIWizardExportAppPageFormat::sltHandleFormatComboChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIWizardExportAppPageFormat::sltHandleFormatComboChange);
    connect(m_pFileSelector, &UIEmptyFilePathSelector::pathChanged,
            this, &UIWizardExportAppPageFormat::sltHandleFileSelectorChange);
    connect(m_pFormatComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardExportAppPageFormat::sltHandleFormatComboChange);
    connect(m_pMACComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardExportAppPageFormat::sltHandleMACAddressExportPolicyComboChange);
    connect(m_pManifestCheckbox, &QCheckBox::stateChanged,
            this, &UIWizardExportAppPageFormat::sltHandleManifestCheckBoxChange);
    connect(m_pIncludeISOsCheckbox, &QCheckBox::stateChanged,
            this, &UIWizardExportAppPageFormat::sltHandleIncludeISOsCheckBoxChange);
    connect(m_pProfileComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardExportAppPageFormat::sltHandleProfileComboChange);
    connect(m_pExportModeButtonGroup, &QButtonGroup::buttonToggled,
            this, &UIWizardExportAppPageFormat::sltHandleRadioButtonToggled);
    connect(m_pProfileToolButton, &QIToolButton::clicked,
            this, &UIWizardExportAppPageFormat::sltHandleProfileButtonClick);
}

UIWizardExportApp *UIWizardExportAppPageFormat::wizard() const
{
    return qobject_cast<UIWizardExportApp*>(UINativeWizardPage::wizard());
}

void UIWizardExportAppPageFormat::sltRetranslateUI()
{
    /* Translate page: */
    setTitle(UIWizardExportApp::tr("Format settings"));

    /* Translate objects: */
    m_strDefaultApplianceName = UIWizardExportApp::tr("Appliance");
    refreshFileSelectorName(m_strFileSelectorName, wizard()->machineNames(), m_strDefaultApplianceName, wizard()->isFormatCloudOne());
    refreshFileSelectorPath(m_pFileSelector, m_strFileSelectorName, m_strFileSelectorExt, wizard()->isFormatCloudOne());

    /* Translate format label: */
    m_pLabelFormat->setText(UIWizardExportApp::
                            tr("<p>Please choose a format to export the virtual appliance to.</p>"
                               "<p>The <b>Open Virtualization Format</b> supports only <b>ovf</b> or <b>ova</b> extensions. "
                               "If you use the <b>ovf</b> extension, several files will be written separately. "
                               "If you use the <b>ova</b> extension, all the files will be combined into one Open "
                               "Virtualization Format archive.</p>"
                               "<p>The <b>Oracle Cloud Infrastructure</b> format supports exporting to remote cloud servers only. "
                               "Main virtual disk of each selected machine will be uploaded to remote server.</p>"));

    /* Translate settings label: */
    if (wizard()->isFormatCloudOne())
        m_pLabelSettings->setText(UIWizardExportApp::
                                  tr("Please choose one of cloud service profiles you have registered to export virtual "
                                     "machines to. It will be used to establish network connection required to upload your "
                                     "virtual machine files to a remote cloud facility."));
    else
        m_pLabelSettings->setText(UIWizardExportApp::
                                  tr("Please choose a filename to export the virtual appliance to. Besides that you can "
                                     "specify a certain amount of options which affects the size and content of resulting "
                                     "archive."));

    /* Translate file selector: */
    m_pFileSelectorLabel->setText(UIWizardExportApp::tr("&File:"));
    if (m_pFileSelector)
    {
        m_pFileSelector->setLineEditToolTip(UIWizardExportApp::tr("Holds the path of the file selected for export."));
        m_pFileSelector->setChooseButtonToolTip(UIWizardExportApp::tr("Choose a file to export the virtual appliance to..."));
        m_pFileSelector->setFileDialogTitle(UIWizardExportApp::tr("Please choose a file to export the virtual appliance to"));
    }

    /* Translate hardcoded values of Format combo-box: */
    m_pFormatComboBoxLabel->setText(UIWizardExportApp::tr("F&ormat:"));
    m_pFormatComboBox->setItemText(0, UIWizardExportApp::tr("Open Virtualization Format 0.9"));
    m_pFormatComboBox->setItemText(1, UIWizardExportApp::tr("Open Virtualization Format 1.0"));
    m_pFormatComboBox->setItemText(2, UIWizardExportApp::tr("Open Virtualization Format 2.0"));
    m_pFormatComboBox->setItemData(0, UIWizardExportApp::tr("Write in legacy OVF 0.9 format for compatibility "
                                                            "with other virtualization products."), Qt::ToolTipRole);
    m_pFormatComboBox->setItemData(1, UIWizardExportApp::tr("Write in standard OVF 1.0 format."), Qt::ToolTipRole);
    m_pFormatComboBox->setItemData(2, UIWizardExportApp::tr("Write in new OVF 2.0 format."), Qt::ToolTipRole);
    /* Translate received values of Format combo-box.
     * We are enumerating starting from 0 for simplicity: */
    for (int i = 0; i < m_pFormatComboBox->count(); ++i)
        if (isFormatCloudOne(m_pFormatComboBox, i))
        {
            m_pFormatComboBox->setItemText(i, m_pFormatComboBox->itemData(i, FormatData_Name).toString());
            m_pFormatComboBox->setItemData(i, UIWizardExportApp::tr("Export to cloud service provider."), Qt::ToolTipRole);
        }

    /* Translate MAC address policy combo-box: */
    m_pMACComboBoxLabel->setText(UIWizardExportApp::tr("MAC Address &Policy:"));
    for (int i = 0; i < m_pMACComboBox->count(); ++i)
    {
        const MACAddressExportPolicy enmPolicy = m_pMACComboBox->itemData(i).value<MACAddressExportPolicy>();
        switch (enmPolicy)
        {
            case MACAddressExportPolicy_KeepAllMACs:
            {
                m_pMACComboBox->setItemText(i, UIWizardExportApp::tr("Include all network adapter MAC addresses"));
                m_pMACComboBox->setItemData(i, UIWizardExportApp::tr("Include all network adapter MAC addresses in exported "
                                                                     "appliance archive."), Qt::ToolTipRole);
                break;
            }
            case MACAddressExportPolicy_StripAllNonNATMACs:
            {
                m_pMACComboBox->setItemText(i, UIWizardExportApp::tr("Include only NAT network adapter MAC addresses"));
                m_pMACComboBox->setItemData(i, UIWizardExportApp::tr("Include only NAT network adapter MAC addresses in exported "
                                                                     "appliance archive."), Qt::ToolTipRole);
                break;
            }
            case MACAddressExportPolicy_StripAllMACs:
            {
                m_pMACComboBox->setItemText(i, UIWizardExportApp::tr("Strip all network adapter MAC addresses"));
                m_pMACComboBox->setItemData(i, UIWizardExportApp::tr("Strip all network adapter MAC addresses from exported "
                                                                     "appliance archive."), Qt::ToolTipRole);
                break;
            }
            default:
                break;
        }
    }

    /* Translate addtional stuff: */
    m_pAdditionalLabel->setText(UIWizardExportApp::tr("Additionally:"));
    m_pManifestCheckbox->setToolTip(UIWizardExportApp::tr("Create a Manifest file for automatic data integrity checks on import."));
    m_pManifestCheckbox->setText(UIWizardExportApp::tr("&Write Manifest file"));
    m_pIncludeISOsCheckbox->setToolTip(UIWizardExportApp::tr("Include ISO image files into exported VM archive."));
    m_pIncludeISOsCheckbox->setText(UIWizardExportApp::tr("&Include ISO image files"));

    /* Translate profile stuff: */
    m_pProfileLabel->setText(UIWizardExportApp::tr("&Profile:"));
    if (m_pProfileComboBox)
        m_pProfileComboBox->setToolTip(UIWizardExportApp::tr("Selects cloud profile."));
    if (m_pProfileToolButton)
    {
        m_pProfileToolButton->setText(UIWizardExportApp::tr("Cloud Profile Manager"));
        m_pProfileToolButton->setToolTip(UIWizardExportApp::tr("Opens cloud profile manager..."));
    }

    /* Translate option label: */
    m_pExportModeLabel->setText(UIWizardExportApp::tr("Machine Creation:"));
    m_exportModeButtons.value(CloudExportMode_DoNotAsk)->setText(UIWizardExportApp::tr("Do not ask me about it, leave custom &image for future usage"));
    m_exportModeButtons.value(CloudExportMode_AskThenExport)->setText(UIWizardExportApp::tr("Ask me about it &before exporting disk as custom image"));
    m_exportModeButtons.value(CloudExportMode_ExportThenAsk)->setText(UIWizardExportApp::tr("Ask me about it &after exporting disk as custom image"));

    /* Adjust label widths: */
    QList<QWidget*> labels;
    labels << m_pFormatComboBoxLabel;
    labels << m_pFileSelectorLabel;
    labels << m_pMACComboBoxLabel;
    labels << m_pAdditionalLabel;
    labels << m_pProfileLabel;
    labels << m_pExportModeLabel;
    int iMaxWidth = 0;
    foreach (QWidget *pLabel, labels)
        iMaxWidth = qMax(iMaxWidth, pLabel->minimumSizeHint().width());
    m_pFormatLayout->setColumnMinimumWidth(0, iMaxWidth);
    m_pSettingsLayout1->setColumnMinimumWidth(0, iMaxWidth);
    m_pSettingsLayout2->setColumnMinimumWidth(0, iMaxWidth);

    /* Update tool-tips: */
    updateFormatComboToolTip(m_pFormatComboBox);
    updateMACAddressExportPolicyComboToolTip(m_pMACComboBox);
}

void UIWizardExportAppPageFormat::initializePage()
{
    /* Populate formats: */
    populateFormats(m_pFormatComboBox, wizard()->notificationCenter(), m_fExportToOCIByDefault);
    /* Populate MAC address policies: */
    populateMACAddressPolicies(m_pMACComboBox);
    /* Translate page: */
    sltRetranslateUI();

    /* Choose initially focused widget: */
    if (wizard()->isFormatCloudOne())
        m_pProfileComboBox->setFocus();
    else
        m_pFileSelector->setFocus();

    /* Fetch it, asynchronously: */
    QMetaObject::invokeMethod(this, "sltHandleFormatComboChange", Qt::QueuedConnection);
}

bool UIWizardExportAppPageFormat::isComplete() const
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud target selected: */
    if (wizard()->isFormatCloudOne())
        fResult = m_comCloudProfile.isNotNull();
    else
        fResult = UICommon::hasAllowedExtension(wizard()->path().toLower(), OVFFileExts);

    /* Return result: */
    return fResult;
}

bool UIWizardExportAppPageFormat::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud target selected: */
    if (wizard()->isFormatCloudOne())
    {
        /* Update cloud stuff: */
        updateCloudStuff();
        /* Which is required to continue to the next page: */
        fResult =    wizard()->cloudAppliance().isNotNull()
                  && wizard()->cloudClient().isNotNull()
                  && wizard()->vsd().isNotNull()
                  && wizard()->vsdExportForm().isNotNull();
    }
    else
    {
        /* Update local stuff: */
        updateLocalStuff();
        /* Which is required to continue to the next page: */
        fResult = wizard()->localAppliance().isNotNull();
    }

    /* Return result: */
    return fResult;
}

void UIWizardExportAppPageFormat::sltHandleFormatComboChange()
{
    /* Update combo tool-tip: */
    updateFormatComboToolTip(m_pFormatComboBox);

    /* Update wizard fields: */
    wizard()->setFormat(format(m_pFormatComboBox));
    wizard()->setFormatCloudOne(isFormatCloudOne(m_pFormatComboBox));

    /* Refresh settings widget state: */
    refreshStackedWidget(m_pSettingsWidget1, wizard()->isFormatCloudOne());

    /* Update export settings: */
    refreshFileSelectorExtension(m_strFileSelectorExt, m_pFileSelector, wizard()->isFormatCloudOne());
    refreshFileSelectorPath(m_pFileSelector, m_strFileSelectorName, m_strFileSelectorExt, wizard()->isFormatCloudOne());
    refreshManifestCheckBoxAccess(m_pManifestCheckbox, wizard()->isFormatCloudOne());
    refreshIncludeISOsCheckBoxAccess(m_pIncludeISOsCheckbox, wizard()->isFormatCloudOne());
    refreshProfileCombo(m_pProfileComboBox, wizard()->notificationCenter(), wizard()->format(), wizard()->isFormatCloudOne());
    refreshCloudExportMode(m_exportModeButtons, wizard()->isFormatCloudOne());

    /* Update profile: */
    sltHandleProfileComboChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardExportAppPageFormat::sltHandleFileSelectorChange()
{
    /* Skip empty paths: */
    if (m_pFileSelector->path().isEmpty())
        return;

    m_strFileSelectorName = QFileInfo(m_pFileSelector->path()).completeBaseName();
    wizard()->setPath(m_pFileSelector->path());
    emit completeChanged();
}

void UIWizardExportAppPageFormat::sltHandleMACAddressExportPolicyComboChange()
{
    updateMACAddressExportPolicyComboToolTip(m_pMACComboBox);
    wizard()->setMACAddressExportPolicy(m_pMACComboBox->currentData().value<MACAddressExportPolicy>());
    emit completeChanged();
}

void UIWizardExportAppPageFormat::sltHandleManifestCheckBoxChange()
{
    wizard()->setManifestSelected(m_pManifestCheckbox->isChecked());
    emit completeChanged();
}

void UIWizardExportAppPageFormat::sltHandleIncludeISOsCheckBoxChange()
{
    wizard()->setIncludeISOsSelected(m_pIncludeISOsCheckbox->isChecked());
    emit completeChanged();
}

void UIWizardExportAppPageFormat::sltHandleProfileComboChange()
{
    /* Update wizard fields: */
    wizard()->setProfileName(profileName(m_pProfileComboBox));

    /* Update export settings: */
    refreshCloudProfile(m_comCloudProfile,
                        wizard()->notificationCenter(),
                        wizard()->format(),
                        wizard()->profileName(),
                        wizard()->isFormatCloudOne());

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardExportAppPageFormat::sltHandleRadioButtonToggled(QAbstractButton *pButton, bool fToggled)
{
    /* Handle checked buttons only: */
    if (!fToggled)
        return;

    /* Update cloud export mode field value: */
    wizard()->setCloudExportMode(m_exportModeButtons.key(pButton));
    emit completeChanged();
}

void UIWizardExportAppPageFormat::sltHandleProfileButtonClick()
{
    /* Open Cloud Profile Manager: */
    if (gpManager)
        gpManager->openCloudProfileManager();
}

void UIWizardExportAppPageFormat::updateLocalStuff()
{
    /* Create appliance: */
    CAppliance comAppliance;
    refreshLocalStuff(comAppliance, wizard(), wizard()->machineIDs(), wizard()->uri());
    wizard()->setLocalAppliance(comAppliance);
}

void UIWizardExportAppPageFormat::updateCloudStuff()
{
    /* Create appliance, client, VSD and VSD export form: */
    CAppliance comAppliance;
    CCloudClient comClient;
    CVirtualSystemDescription comDescription;
    CVirtualSystemDescriptionForm comForm;
    refreshCloudStuff(comAppliance,
                      comClient,
                      comDescription,
                      comForm,
                      wizard(),
                      m_comCloudProfile,
                      wizard()->machineIDs(),
                      wizard()->uri(),
                      wizard()->cloudExportMode());
    wizard()->setCloudAppliance(comAppliance);
    wizard()->setCloudClient(comClient);
    wizard()->setVsd(comDescription);
    wizard()->setVsdExportForm(comForm);
}
