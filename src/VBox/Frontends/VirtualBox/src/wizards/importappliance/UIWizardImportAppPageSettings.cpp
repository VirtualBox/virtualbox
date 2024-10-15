/* $Id: UIWizardImportAppPageSettings.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardImportAppPageSettings class implementation.
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
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIApplianceImportEditorWidget.h"
#include "UIApplianceUnverifiedCertificateViewer.h"
#include "UIFilePathSelector.h"
#include "UIFormEditorWidget.h"
#include "UIGlobalSession.h"
#include "UINotificationCenter.h"
#include "UIWizardImportApp.h"
#include "UIWizardImportAppPageSettings.h"

/* COM includes: */
#include "CAppliance.h"
#include "CCertificate.h"
#include "CSystemProperties.h"
#include "CVirtualSystemDescriptionForm.h"

/* Namespaces: */
using namespace UIWizardImportAppSettings;


/*********************************************************************************************************************************
*   Class UIWizardImportAppSettings implementation.                                                                              *
*********************************************************************************************************************************/

void UIWizardImportAppSettings::refreshStackedWidget(QStackedWidget *pStackedWidget,
                                                     bool fIsSourceCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pStackedWidget);

    /* Update stack appearance according to chosen source: */
    pStackedWidget->setCurrentIndex((int)fIsSourceCloudOne);
}

void UIWizardImportAppSettings::refreshApplianceWidget(UIApplianceImportEditorWidget *pApplianceWidget,
                                                       const CAppliance &comAppliance,
                                                       bool fIsSourceCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pApplianceWidget);

    /* If source is cloud one: */
    if (fIsSourceCloudOne)
    {
        /* Clear form: */
        pApplianceWidget->clear();
    }
    /* If source is local one: */
    else
    {
        /* Make sure appliance widget get new appliance: */
        if (comAppliance.isNotNull())
            pApplianceWidget->setAppliance(comAppliance);
    }
}

void UIWizardImportAppSettings::refreshMACAddressImportPolicies(QComboBox *pCombo,
                                                                bool fIsSourceCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* If source is cloud one: */
    if (fIsSourceCloudOne)
    {
        /* Block signals while updating: */
        pCombo->blockSignals(true);

        /* Clear combo: */
        pCombo->clear();

        /* Unblock signals after update: */
        pCombo->blockSignals(false);
    }
    /* If source is local one: */
    else
    {
        /* We need top-level parent as well: */
        QWidget *pParent = pCombo->window();
        AssertPtrReturnVoid(pParent);

        /* Map known import options to known MAC address import policies: */
        QMap<KImportOptions, MACAddressImportPolicy> knownOptions;
        knownOptions[KImportOptions_KeepAllMACs] = MACAddressImportPolicy_KeepAllMACs;
        knownOptions[KImportOptions_KeepNATMACs] = MACAddressImportPolicy_KeepNATMACs;
        /* Load currently supported import options: */
        const QVector<KImportOptions> supportedOptions =
            gpGlobalSession->virtualBox().GetSystemProperties().GetSupportedImportOptions();
        /* Check which of supported options/policies are known: */
        QList<MACAddressImportPolicy> supportedPolicies;
        foreach (const KImportOptions &enmOption, supportedOptions)
            if (knownOptions.contains(enmOption))
                supportedPolicies << knownOptions.value(enmOption);
        /* Remember current item data to be able to restore it: */
        MACAddressImportPolicy enmOldData = MACAddressImportPolicy_MAX;
        if (pCombo->currentIndex() != -1)
            enmOldData = pCombo->currentData().value<MACAddressImportPolicy>();
        else
        {
            if (supportedPolicies.contains(MACAddressImportPolicy_KeepNATMACs))
                enmOldData = MACAddressImportPolicy_KeepNATMACs;
            else
                enmOldData = MACAddressImportPolicy_StripAllMACs;
        }

        /* Block signals while updating: */
        pCombo->blockSignals(true);

        /* Cleanup combo: */
        pCombo->clear();

        /* Add supported policies first: */
        foreach (const MACAddressImportPolicy &enmPolicy, supportedPolicies)
            pCombo->addItem(QString(), QVariant::fromValue(enmPolicy));

        /* Add hardcoded policy finally: */
        pCombo->addItem(QString(), QVariant::fromValue(MACAddressImportPolicy_StripAllMACs));

        /* Set previous/default item if possible: */
        int iNewIndex = -1;
        if (   iNewIndex == -1
            && enmOldData != MACAddressImportPolicy_MAX)
            iNewIndex = pCombo->findData(QVariant::fromValue(enmOldData));
        if (   iNewIndex == -1
            && pCombo->count() > 0)
            iNewIndex = 0;
        if (iNewIndex != -1)
            pCombo->setCurrentIndex(iNewIndex);

        /* Unblock signals after update: */
        pCombo->blockSignals(false);

        /* Translate finally: */
        retranslateMACImportPolicyCombo(pCombo);
    }
}

void UIWizardImportAppSettings::refreshFormPropertiesTable(UIFormEditorWidget *pFormEditor,
                                                           const CVirtualSystemDescriptionForm &comForm,
                                                           bool fIsSourceCloudOne)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pFormEditor);

    /* If source is cloud one: */
    if (fIsSourceCloudOne)
    {
        /* Make sure properties table get new description form: */
        if (comForm.isNotNull())
            pFormEditor->setVirtualSystemDescriptionForm(comForm);
    }
    /* If source is local one: */
    else
    {
        /* Clear form: */
        pFormEditor->clearForm();
    }
}

MACAddressImportPolicy UIWizardImportAppSettings::macAddressImportPolicy(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturn(pCombo, MACAddressImportPolicy_MAX);

    /* Give the actual result: */
    return pCombo->currentData().value<MACAddressImportPolicy>();
}

bool UIWizardImportAppSettings::isImportHDsAsVDI(QCheckBox *pCheckBox)
{
    /* Sanity check: */
    AssertPtrReturn(pCheckBox, false);

    /* Give the actual result: */
    return pCheckBox->isChecked();
}

void UIWizardImportAppSettings::retranslateMACImportPolicyCombo(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* Enumerate combo items: */
    for (int i = 0; i < pCombo->count(); ++i)
    {
        const MACAddressImportPolicy enmPolicy = pCombo->itemData(i).value<MACAddressImportPolicy>();
        switch (enmPolicy)
        {
            case MACAddressImportPolicy_KeepAllMACs:
            {
                pCombo->setItemText(i, UIWizardImportApp::tr("Include all network adapter MAC addresses"));
                pCombo->setItemData(i, UIWizardImportApp::tr("Include all network adapter MAC addresses "
                                                             "during importing."), Qt::ToolTipRole);
                break;
            }
            case MACAddressImportPolicy_KeepNATMACs:
            {
                pCombo->setItemText(i, UIWizardImportApp::tr("Include only NAT network adapter MAC addresses"));
                pCombo->setItemData(i, UIWizardImportApp::tr("Include only NAT network adapter MAC addresses "
                                                             "during importing."), Qt::ToolTipRole);
                break;
            }
            case MACAddressImportPolicy_StripAllMACs:
            {
                pCombo->setItemText(i, UIWizardImportApp::tr("Generate new MAC addresses for all network adapters"));
                pCombo->setItemData(i, UIWizardImportApp::tr("Generate new MAC addresses for all network adapters "
                                                             "during importing."), Qt::ToolTipRole);
                break;
            }
            default:
                break;
        }
    }
}

void UIWizardImportAppSettings::retranslateCertificateLabel(QLabel *pLabel, const kCertText &enmType, const QString &strSignedBy)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pLabel);

    /* Handle known types: */
    switch (enmType)
    {
        case kCertText_Unsigned:
            pLabel->setText(UIWizardImportApp::tr("Appliance is not signed"));
            break;
        case kCertText_IssuedTrusted:
            pLabel->setText(UIWizardImportApp::tr("Appliance signed by %1 (trusted)").arg(strSignedBy));
            break;
        case kCertText_IssuedExpired:
            pLabel->setText(UIWizardImportApp::tr("Appliance signed by %1 (expired!)").arg(strSignedBy));
            break;
        case kCertText_IssuedUnverified:
            pLabel->setText(UIWizardImportApp::tr("Unverified signature by %1!").arg(strSignedBy));
            break;
        case kCertText_SelfSignedTrusted:
            pLabel->setText(UIWizardImportApp::tr("Self signed by %1 (trusted)").arg(strSignedBy));
            break;
        case kCertText_SelfSignedExpired:
            pLabel->setText(UIWizardImportApp::tr("Self signed by %1 (expired!)").arg(strSignedBy));
            break;
        case kCertText_SelfSignedUnverified:
            pLabel->setText(UIWizardImportApp::tr("Unverified self signed signature by %1!").arg(strSignedBy));
            break;
        default:
            AssertFailed();
            RT_FALL_THRU();
        case kCertText_Uninitialized:
            pLabel->setText("<uninitialized page>");
            break;
    }
}

void UIWizardImportAppSettings::updateMACImportPolicyComboToolTip(QComboBox *pCombo)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pCombo);

    /* Update tool-tip: */
    const QString strCurrentToolTip = pCombo->currentData(Qt::ToolTipRole).toString();
    pCombo->setToolTip(strCurrentToolTip);
}


/*********************************************************************************************************************************
*   Class UIWizardImportAppPageSettings implementation.                                                                          *
*********************************************************************************************************************************/

UIWizardImportAppPageSettings::UIWizardImportAppPageSettings(const QString &strFileName)
    : m_strFileName(strFileName)
    , m_pLabelDescription(0)
    , m_pSettingsWidget2(0)
    , m_pApplianceWidget(0)
    , m_pLabelImportFilePath(0)
    , m_pEditorImportFilePath(0)
    , m_pLabelMACImportPolicy(0)
    , m_pComboMACImportPolicy(0)
    , m_pLabelAdditionalOptions(0)
    , m_pCheckboxImportHDsAsVDI(0)
    , m_pCertLabel(0)
    , m_enmCertText(kCertText_Uninitialized)
    , m_pFormEditor(0)
{
    /* Create main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Prepare label: */
        m_pLabelDescription = new QIRichTextLabel(this);
        if (m_pLabelDescription)
            pMainLayout->addWidget(m_pLabelDescription);

        /* Prepare settings widget 2: */
        m_pSettingsWidget2 = new QStackedWidget(this);
        if (m_pSettingsWidget2)
        {
            /* Prepare appliance container: */
            QWidget *pContainerAppliance = new QWidget(m_pSettingsWidget2);
            if (pContainerAppliance)
            {
                /* Prepare appliance layout: */
                QGridLayout *pLayoutAppliance = new QGridLayout(pContainerAppliance);
                if (pLayoutAppliance)
                {
                    pLayoutAppliance->setContentsMargins(0, 0, 0, 0);
                    pLayoutAppliance->setColumnStretch(0, 0);
                    pLayoutAppliance->setColumnStretch(1, 1);

                    /* Prepare appliance widget: */
                    m_pApplianceWidget = new UIApplianceImportEditorWidget(pContainerAppliance);
                    if (m_pApplianceWidget)
                    {
                        m_pApplianceWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
                        pLayoutAppliance->addWidget(m_pApplianceWidget, 0, 0, 1, 3);
                    }

                    /* Prepare path selector label: */
                    m_pLabelImportFilePath = new QLabel(pContainerAppliance);
                    if (m_pLabelImportFilePath)
                    {
                        m_pLabelImportFilePath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        pLayoutAppliance->addWidget(m_pLabelImportFilePath, 1, 0);
                    }
                    /* Prepare path selector editor: */
                    m_pEditorImportFilePath = new UIFilePathSelector(pContainerAppliance);
                    if (m_pEditorImportFilePath)
                    {
                        m_pEditorImportFilePath->setResetEnabled(true);
                        m_pEditorImportFilePath->setDefaultPath(gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder());
                        m_pEditorImportFilePath->setPath(gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder());
                        m_pLabelImportFilePath->setBuddy(m_pEditorImportFilePath);
                        pLayoutAppliance->addWidget(m_pEditorImportFilePath, 1, 1, 1, 2);
                    }

                    /* Prepare MAC address policy label: */
                    m_pLabelMACImportPolicy = new QLabel(pContainerAppliance);
                    if (m_pLabelMACImportPolicy)
                    {
                        m_pLabelMACImportPolicy->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        pLayoutAppliance->addWidget(m_pLabelMACImportPolicy, 2, 0);
                    }
                    /* Prepare MAC address policy combo: */
                    m_pComboMACImportPolicy = new QComboBox(pContainerAppliance);
                    if (m_pComboMACImportPolicy)
                    {
                        m_pComboMACImportPolicy->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                        m_pLabelMACImportPolicy->setBuddy(m_pComboMACImportPolicy);
                        pLayoutAppliance->addWidget(m_pComboMACImportPolicy, 2, 1, 1, 2);
                    }

                    /* Prepare additional options label: */
                    m_pLabelAdditionalOptions = new QLabel(pContainerAppliance);
                    if (m_pLabelAdditionalOptions)
                    {
                        m_pLabelAdditionalOptions->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        pLayoutAppliance->addWidget(m_pLabelAdditionalOptions, 3, 0);
                    }
                    /* Prepare import HDs as VDIs checkbox: */
                    m_pCheckboxImportHDsAsVDI = new QCheckBox(pContainerAppliance);
                    {
                        m_pCheckboxImportHDsAsVDI->setCheckState(Qt::Checked);
                        pLayoutAppliance->addWidget(m_pCheckboxImportHDsAsVDI, 3, 1);
                    }

                    /* Prepare certificate label: */
                    m_pCertLabel = new QLabel(pContainerAppliance);
                    if (m_pCertLabel)
                        pLayoutAppliance->addWidget(m_pCertLabel, 4, 0, 1, 3);
                }

                /* Add into widget: */
                m_pSettingsWidget2->addWidget(pContainerAppliance);
            }

            /* Prepare form editor container: */
            QWidget *pContainerFormEditor = new QWidget(m_pSettingsWidget2);
            if (pContainerFormEditor)
            {
                /* Prepare form editor layout: */
                QVBoxLayout *pLayoutFormEditor = new QVBoxLayout(pContainerFormEditor);
                if (pLayoutFormEditor)
                {
                    pLayoutFormEditor->setContentsMargins(0, 0, 0, 0);

                    /* Prepare form editor widget: */
                    m_pFormEditor = new UIFormEditorWidget(pContainerFormEditor);
                    if (m_pFormEditor)
                        pLayoutFormEditor->addWidget(m_pFormEditor);
                }

                /* Add into widget: */
                m_pSettingsWidget2->addWidget(pContainerFormEditor);
            }

            /* Add into layout: */
            pMainLayout->addWidget(m_pSettingsWidget2);
        }
    }

    /* Setup connections: */
    connect(m_pEditorImportFilePath, &UIFilePathSelector::pathChanged,
            this, &UIWizardImportAppPageSettings::sltHandleImportPathEditorChange);
    connect(m_pComboMACImportPolicy, &QComboBox::currentIndexChanged,
            this, &UIWizardImportAppPageSettings::sltHandleMACImportPolicyComboChange);
    connect(m_pCheckboxImportHDsAsVDI, &QCheckBox::stateChanged,
            this, &UIWizardImportAppPageSettings::sltHandleImportHDsAsVDICheckBoxChange);
}

UIWizardImportApp *UIWizardImportAppPageSettings::wizard() const
{
    return qobject_cast<UIWizardImportApp*>(UINativeWizardPage::wizard());
}

void UIWizardImportAppPageSettings::sltRetranslateUI()
{
    /* Translate page: */
    setTitle(UIWizardImportApp::tr("Appliance settings"));

    /* Translate description label: */
    if (m_pLabelDescription)
    {
        if (wizard()->isSourceCloudOne())
            m_pLabelDescription->setText(UIWizardImportApp::tr("These are the the suggested settings of the cloud VM import "
                                                               "procedure, they are influencing the resulting local VM instance. "
                                                               "You can change many of the properties shown by double-clicking "
                                                               "on the items and disable others using the check boxes below."));
        else
            m_pLabelDescription->setText(UIWizardImportApp::tr("These are the virtual machines contained in the appliance "
                                                               "and the suggested settings of the imported VirtualBox machines. "
                                                               "You can change many of the properties shown by double-clicking "
                                                               "on the items and disable others using the check boxes below."));
    }

    /* Translate path selector stuff: */
    if (m_pLabelImportFilePath)
        m_pLabelImportFilePath->setText(UIWizardImportApp::tr("&Machine Base Folder:"));
    if (m_pEditorImportFilePath)
        m_pEditorImportFilePath->setToolTip(UIWizardImportApp::tr("Holds the path to the base virtual machine folder. "
                                                                  "This folder is used when creating new virtual machine."));

    /* Translate MAC import policy label: */
    if (m_pLabelMACImportPolicy)
        m_pLabelMACImportPolicy->setText(UIWizardImportApp::tr("MAC Address &Policy:"));

    /* Translate additional options label: */
    if (m_pLabelAdditionalOptions)
        m_pLabelAdditionalOptions->setText(UIWizardImportApp::tr("Additional Options:"));
    /* Translate additional option check-box: */
    if (m_pCheckboxImportHDsAsVDI)
    {
        m_pCheckboxImportHDsAsVDI->setText(UIWizardImportApp::tr("&Import hard drives as VDI"));
        m_pCheckboxImportHDsAsVDI->setToolTip(UIWizardImportApp::tr("When checked, all the hard drives that belong to this "
                                                                    "appliance will be imported in VDI format."));
    }

    /* Translate separate stuff: */
    retranslateMACImportPolicyCombo(m_pComboMACImportPolicy);
    retranslateCertificateLabel(m_pCertLabel, m_enmCertText, m_strSignedBy);

    /* Update tool-tips: */
    updateMACImportPolicyComboToolTip(m_pComboMACImportPolicy);
}

void UIWizardImportAppPageSettings::initializePage()
{
    /* Make sure form-editor knows notification-center: */
    m_pFormEditor->setNotificationCenter(wizard()->notificationCenter());
    /* Translate page: */
    sltRetranslateUI();

    /* Choose initially focused widget: */
    if (wizard()->isSourceCloudOne())
        m_pFormEditor->setFocus();
    else
        m_pApplianceWidget->setFocus();

    /* Fetch it, asynchronously: */
    QMetaObject::invokeMethod(this, "sltAsyncInit", Qt::QueuedConnection);
}

bool UIWizardImportAppPageSettings::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud source selected: */
    if (wizard()->isSourceCloudOne())
    {
        /* Make sure table has own data committed: */
        m_pFormEditor->makeSureEditorDataCommitted();

        /* Check whether we have proper VSD form: */
        CVirtualSystemDescriptionForm comForm = wizard()->vsdImportForm();
        fResult = comForm.isNotNull();

        /* Give changed VSD back to appliance: */
        if (fResult)
        {
            comForm.GetVirtualSystemDescription();
            fResult = comForm.isOk();
            if (!fResult)
                UINotificationMessage::cannotAcquireVirtualSystemDescriptionFormParameter(comForm, wizard()->notificationCenter());
        }
    }
    else
    {
        /* Make sure widget has own data committed: */
        m_pApplianceWidget->prepareImport();
    }

    /* Try to import appliance: */
    if (fResult)
        fResult = wizard()->importAppliance();

    /* Return result: */
    return fResult;
}

void UIWizardImportAppPageSettings::sltAsyncInit()
{
    /* If we have local source and file name passed,
     * check if specified file contains valid appliance: */
    if (   !wizard()->isSourceCloudOne()
        && !m_strFileName.isEmpty()
        && !wizard()->setFile(m_strFileName))
    {
        wizard()->reject();
        return;
    }

    /* Refresh page widgets: */
    refreshStackedWidget(m_pSettingsWidget2,
                         wizard()->isSourceCloudOne());
    refreshApplianceWidget(m_pApplianceWidget,
                           wizard()->localAppliance(),
                           wizard()->isSourceCloudOne());
    refreshMACAddressImportPolicies(m_pComboMACImportPolicy,
                                    wizard()->isSourceCloudOne());
    refreshFormPropertiesTable(m_pFormEditor,
                               wizard()->vsdImportForm(),
                               wizard()->isSourceCloudOne());

    /* Init wizard fields: */
    sltHandleImportPathEditorChange();
    sltHandleMACImportPolicyComboChange();
    sltHandleImportHDsAsVDICheckBoxChange();

    /* Handle appliance certificate: */
    if (!wizard()->isSourceCloudOne())
        handleApplianceCertificate();
}

void UIWizardImportAppPageSettings::sltHandleImportPathEditorChange()
{
    AssertPtrReturnVoid(m_pApplianceWidget);
    AssertPtrReturnVoid(m_pEditorImportFilePath);
    m_pApplianceWidget->setVirtualSystemBaseFolder(m_pEditorImportFilePath->path());
}

void UIWizardImportAppPageSettings::sltHandleMACImportPolicyComboChange()
{
    /* Update combo tool-tip: */
    updateMACImportPolicyComboToolTip(m_pComboMACImportPolicy);

    /* Update wizard fields: */
    wizard()->setMACAddressImportPolicy(macAddressImportPolicy(m_pComboMACImportPolicy));
}

void UIWizardImportAppPageSettings::sltHandleImportHDsAsVDICheckBoxChange()
{
    /* Update wizard fields: */
    wizard()->setImportHDsAsVDI(isImportHDsAsVDI(m_pCheckboxImportHDsAsVDI));
}

void UIWizardImportAppPageSettings::handleApplianceCertificate()
{
    /* Handle certificate: */
    CAppliance comAppliance = wizard()->localAppliance();
    CCertificate comCertificate = comAppliance.GetCertificate();
    if (comCertificate.isNull())
        m_enmCertText = kCertText_Unsigned;
    else
    {
        /* Pick a 'signed-by' name: */
        m_strSignedBy = comCertificate.GetFriendlyName();

        /* If trusted, just select the right message: */
        if (comCertificate.GetTrusted())
        {
            if (comCertificate.GetSelfSigned())
                m_enmCertText = !comCertificate.GetExpired() ? kCertText_SelfSignedTrusted : kCertText_SelfSignedExpired;
            else
                m_enmCertText = !comCertificate.GetExpired() ? kCertText_IssuedTrusted     : kCertText_IssuedExpired;
        }
        else
        {
            /* Not trusted!  Must ask the user whether to continue in this case: */
            m_enmCertText = comCertificate.GetSelfSigned() ? kCertText_SelfSignedUnverified : kCertText_IssuedUnverified;

            /* Translate page early: */
            sltRetranslateUI();

            /* Instantiate the dialog: */
            QPointer<UIApplianceUnverifiedCertificateViewer> pDialog =
                new UIApplianceUnverifiedCertificateViewer(this, comCertificate);

            /* Show viewer in modal mode: */
            const int iResultCode = pDialog->exec();
            /* Leave if viewer destroyed prematurely: */
            if (!pDialog)
                return;
            /* Delete viewer finally: */
            delete pDialog;

            /* Dismiss the entire import-appliance wizard if user rejects certificate: */
            if (iResultCode == QDialog::Rejected)
                wizard()->reject();
        }
    }

    /* Translate certificate label: */
    retranslateCertificateLabel(m_pCertLabel, m_enmCertText, m_strSignedBy);
}
