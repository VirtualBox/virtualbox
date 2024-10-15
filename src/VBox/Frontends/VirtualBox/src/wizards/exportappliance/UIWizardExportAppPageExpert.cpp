/* $Id: UIWizardExportAppPageExpert.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportAppPageExpert class implementation.
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
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIListWidget.h"
#include "QIToolButton.h"
#include "UICommon.h"
#include "UIApplianceExportEditorWidget.h"
#include "UIConverter.h"
#include "UIEmptyFilePathSelector.h"
#include "UIFormEditorWidget.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIToolBox.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManager.h"
#include "UIWizardExportApp.h"
#include "UIWizardExportAppPageExpert.h"
#include "UIWizardExportAppPageFormat.h"
#include "UIWizardExportAppPageSettings.h"
#include "UIWizardExportAppPageVMs.h"

/* Namespaces: */
using namespace UIWizardExportAppFormat;
using namespace UIWizardExportAppSettings;
using namespace UIWizardExportAppVMs;


/*********************************************************************************************************************************
*   Class UIWizardExportAppPageExpert implementation.                                                                            *
*********************************************************************************************************************************/

UIWizardExportAppPageExpert::UIWizardExportAppPageExpert(const QStringList &selectedVMNames, bool fExportToOCIByDefault)
    : m_selectedVMNames(selectedVMNames)
    , m_fExportToOCIByDefault(fExportToOCIByDefault)
    , m_pToolBox(0)
    , m_pVMSelector(0)
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
    , m_pSettingsWidget2(0)
    , m_pApplianceWidget(0)
    , m_pFormEditor(0)
    , m_fLaunching(false)
{
    /* Create widgets: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Create tool-box: */
        m_pToolBox = new UIToolBox(this);
        if (m_pToolBox)
        {
            /* Create VM selector: */
            m_pVMSelector = new QIListWidget(m_pToolBox);
            if (m_pVMSelector)
            {
                m_pVMSelector->setAlternatingRowColors(true);
                m_pVMSelector->setSelectionMode(QAbstractItemView::ExtendedSelection);

                /* Add into tool-box: */
                m_pToolBox->insertPage(0, m_pVMSelector, QString());
            }

            /* Create settings widget container: */
            QWidget *pWidgetSettings = new QWidget(m_pToolBox);
            if (pWidgetSettings)
            {
                /* Create settings widget container layout: */
                QVBoxLayout *pSettingsCntLayout = new QVBoxLayout(pWidgetSettings);
                if (pSettingsCntLayout)
                {
                    pSettingsCntLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
                    pSettingsCntLayout->setSpacing(5);
#endif

                    /* Create format layout: */
                    m_pFormatLayout = new QGridLayout;
                    if (m_pFormatLayout)
                    {
                        m_pFormatLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
                        m_pFormatLayout->setSpacing(10);
#endif
                        m_pFormatLayout->setColumnStretch(0, 0);
                        m_pFormatLayout->setColumnStretch(1, 1);

                        /* Create format combo-box label: */
                        m_pFormatComboBoxLabel = new QLabel(pWidgetSettings);
                        if (m_pFormatComboBoxLabel)
                        {
                            m_pFormatComboBoxLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                            m_pFormatLayout->addWidget(m_pFormatComboBoxLabel, 0, 0);
                        }
                        /* Create format combo-box: */
                        m_pFormatComboBox = new QComboBox(pWidgetSettings);
                        if (m_pFormatComboBox)
                        {
                            m_pFormatComboBoxLabel->setBuddy(m_pFormatComboBox);
                            m_pFormatLayout->addWidget(m_pFormatComboBox, 0, 1);
                        }

                        /* Add into layout: */
                        pSettingsCntLayout->addLayout(m_pFormatLayout);
                    }

                    /* Create 1st settings widget: */
                    m_pSettingsWidget1 = new QStackedWidget(pWidgetSettings);
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
                                m_pSettingsLayout1->setSpacing(10);
#endif
                                m_pSettingsLayout1->setContentsMargins(0, 0, 0, 0);
                                m_pSettingsLayout1->setColumnStretch(0, 0);
                                m_pSettingsLayout1->setColumnStretch(1, 1);

                                /* Create file selector label: */
                                m_pFileSelectorLabel = new QLabel(pSettingsPane1);
                                if (m_pFileSelectorLabel)
                                {
                                    m_pFileSelectorLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                                    m_pSettingsLayout1->addWidget(m_pFileSelectorLabel, 0, 0);
                                }
                                /* Create file selector: */
                                m_pFileSelector = new UIEmptyFilePathSelector(pSettingsPane1);
                                if (m_pFileSelector)
                                {
                                    m_pFileSelectorLabel->setBuddy(m_pFileSelector);
                                    m_pFileSelector->setMode(UIEmptyFilePathSelector::Mode_File_Save);
                                    m_pFileSelector->setEditable(true);
                                    m_pFileSelector->setButtonPosition(UIEmptyFilePathSelector::RightPosition);
                                    m_pFileSelector->setDefaultSaveExt("ova");
                                    m_pSettingsLayout1->addWidget(m_pFileSelector, 0, 1, 1, 2);
                                }

                                /* Create MAC policy combo-box label: */
                                m_pMACComboBoxLabel = new QLabel(pSettingsPane1);
                                if (m_pMACComboBoxLabel)
                                {
                                    m_pMACComboBoxLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                                    m_pSettingsLayout1->addWidget(m_pMACComboBoxLabel, 1, 0);
                                }
                                /* Create MAC policy combo-box: */
                                m_pMACComboBox = new QComboBox(pSettingsPane1);
                                if (m_pMACComboBox)
                                {
                                    m_pMACComboBoxLabel->setBuddy(m_pMACComboBox);
                                    m_pSettingsLayout1->addWidget(m_pMACComboBox, 1, 1, 1, 2);
                                }

                                /* Create advanced label: */
                                m_pAdditionalLabel = new QLabel(pSettingsPane1);
                                if (m_pAdditionalLabel)
                                {
                                    m_pAdditionalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                                    m_pSettingsLayout1->addWidget(m_pAdditionalLabel, 2, 0);
                                }
                                /* Create manifest check-box editor: */
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
                                m_pSettingsLayout2->setSpacing(10);
#endif
                                m_pSettingsLayout2->setContentsMargins(0, 0, 0, 0);
                                m_pSettingsLayout2->setColumnStretch(0, 0);
                                m_pSettingsLayout2->setColumnStretch(1, 1);

                                /* Create profile label: */
                                m_pProfileLabel = new QLabel(pSettingsPane2);
                                if (m_pProfileLabel)
                                {
                                    m_pProfileLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
                                    m_pExportModeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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

                                /* Create placeholder: */
                                QWidget *pPlaceholder = new QWidget(pSettingsPane2);
                                if (pPlaceholder)
                                    m_pSettingsLayout2->addWidget(pPlaceholder, 4, 0, 1, 3);
                            }

                            /* Add into layout: */
                            m_pSettingsWidget1->addWidget(pSettingsPane2);
                        }

                        /* Add into layout: */
                        pSettingsCntLayout->addWidget(m_pSettingsWidget1);
                    }
                }

                /* Add into tool-box: */
                m_pToolBox->insertPage(1, pWidgetSettings, QString());
            }

            /* Create 2nd settings widget: */
            m_pSettingsWidget2 = new QStackedWidget(m_pToolBox);
            if (m_pSettingsWidget2)
            {
                /* Create appliance widget container: */
                QWidget *pApplianceWidgetCnt = new QWidget(this);
                if (pApplianceWidgetCnt)
                {
                    /* Create appliance widget layout: */
                    QVBoxLayout *pApplianceWidgetLayout = new QVBoxLayout(pApplianceWidgetCnt);
                    if (pApplianceWidgetLayout)
                    {
                        pApplianceWidgetLayout->setContentsMargins(0, 0, 0, 0);

                        /* Create appliance widget: */
                        m_pApplianceWidget = new UIApplianceExportEditorWidget(pApplianceWidgetCnt);
                        if (m_pApplianceWidget)
                        {
                            m_pApplianceWidget->setMinimumHeight(250);
                            m_pApplianceWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
                            pApplianceWidgetLayout->addWidget(m_pApplianceWidget);
                        }
                    }

                    /* Add into layout: */
                    m_pSettingsWidget2->addWidget(pApplianceWidgetCnt);
                }

                /* Create form editor container: */
                QWidget *pFormEditorCnt = new QWidget(this);
                if (pFormEditorCnt)
                {
                    /* Create form editor layout: */
                    QVBoxLayout *pFormEditorLayout = new QVBoxLayout(pFormEditorCnt);
                    if (pFormEditorLayout)
                    {
                        pFormEditorLayout->setContentsMargins(0, 0, 0, 0);

                        /* Create form editor widget: */
                        m_pFormEditor = new UIFormEditorWidget(pFormEditorCnt);
                        if (m_pFormEditor)
                            pFormEditorLayout->addWidget(m_pFormEditor);
                    }

                    /* Add into layout: */
                    m_pSettingsWidget2->addWidget(pFormEditorCnt);
                }

                /* Add into tool-box: */
                m_pToolBox->insertPage(2, m_pSettingsWidget2, QString());
            }

            /* Add into layout: */
            pMainLayout->addWidget(m_pToolBox);
        }

        /* Add stretch: */
        pMainLayout->addStretch();
    }

    /* Setup connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIWizardExportAppPageExpert::sltHandleFormatComboChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIWizardExportAppPageExpert::sltHandleFormatComboChange);
    connect(m_pVMSelector, &QIListWidget::itemSelectionChanged,
            this, &UIWizardExportAppPageExpert::sltHandleVMItemSelectionChanged);
    connect(m_pFileSelector, &UIEmptyFilePathSelector::pathChanged,
            this, &UIWizardExportAppPageExpert::sltHandleFileSelectorChange);
    connect(m_pFormatComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardExportAppPageExpert::sltHandleFormatComboChange);
    connect(m_pMACComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardExportAppPageExpert::sltHandleMACAddressExportPolicyComboChange);
    connect(m_pManifestCheckbox, &QCheckBox::stateChanged,
            this, &UIWizardExportAppPageExpert::sltHandleManifestCheckBoxChange);
    connect(m_pIncludeISOsCheckbox, &QCheckBox::stateChanged,
            this, &UIWizardExportAppPageExpert::sltHandleIncludeISOsCheckBoxChange);
    connect(m_pProfileComboBox, &QComboBox::currentIndexChanged,
            this, &UIWizardExportAppPageExpert::sltHandleProfileComboChange);
    connect(m_pExportModeButtonGroup, &QButtonGroup::buttonToggled,
            this, &UIWizardExportAppPageExpert::sltHandleRadioButtonToggled);
    connect(m_pProfileToolButton, &QIToolButton::clicked,
            this, &UIWizardExportAppPageExpert::sltHandleProfileButtonClick);
}

UIWizardExportApp *UIWizardExportAppPageExpert::wizard() const
{
    return qobject_cast<UIWizardExportApp*>(UINativeWizardPage::wizard());
}

void UIWizardExportAppPageExpert::sltRetranslateUI()
{
    /* Translate objects: */
    m_strDefaultApplianceName = UIWizardExportApp::tr("Appliance");
    refreshFileSelectorName(m_strFileSelectorName, wizard()->machineNames(), m_strDefaultApplianceName, wizard()->isFormatCloudOne());
    refreshFileSelectorPath(m_pFileSelector, m_strFileSelectorName, m_strFileSelectorExt, wizard()->isFormatCloudOne());

    /* Translate tool-box: */
    m_pToolBox->setPageTitle(0, UIWizardExportApp::tr("Virtual &machines"));
    m_pToolBox->setPageTitle(1, UIWizardExportApp::tr("Format &settings"));
    m_pToolBox->setPageTitle(2, UIWizardExportApp::tr("&Appliance settings"));

    /* Translate Machine list: */
    m_pVMSelector->setWhatsThis(UIWizardExportApp::tr("Contains a list of Virtual Machines"));

    /* Translate File selector: */
    m_pFileSelectorLabel->setText(UIWizardExportApp::tr("&File:"));
    if (m_pFileSelector)
    {
        m_pFileSelector->setLineEditToolTip(UIWizardExportApp::tr("Holds the path of the file selected for export."));
        m_pFileSelector->setChooseButtonToolTip(UIWizardExportApp::tr("Choose a file to export the virtual appliance to..."));
        m_pFileSelector->setFileDialogTitle(UIWizardExportApp::tr("Please choose a file to export the virtual appliance to"));
    }

    /* Translate hard-coded values of Format combo-box: */
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

void UIWizardExportAppPageExpert::initializePage()
{
    /* Make sure form-editor knows notification-center: */
    m_pFormEditor->setNotificationCenter(wizard()->notificationCenter());
    /* Choose 1st tool to be chosen initially: */
    m_pToolBox->setCurrentPage(0);
    /* Populate VM items: */
    populateVMItems(m_pVMSelector, m_selectedVMNames);
    /* Populate formats: */
    populateFormats(m_pFormatComboBox, wizard()->notificationCenter(), m_fExportToOCIByDefault);
    /* Populate MAC address policies: */
    populateMACAddressPolicies(m_pMACComboBox);
    /* Translate page: */
    sltRetranslateUI();

    /* Fetch it, asynchronously: */
    QMetaObject::invokeMethod(this, "sltHandleFormatComboChange", Qt::QueuedConnection);
}

bool UIWizardExportAppPageExpert::isComplete() const
{
    /* Initial result: */
    bool fResult = true;

    /* There should be at least one vm selected: */
    if (fResult)
        fResult = wizard()->machineNames().size() > 0;

    /* Check appliance settings: */
    if (fResult)
    {
        const bool fOVF =    wizard()->format() == "ovf-0.9"
                          || wizard()->format() == "ovf-1.0"
                          || wizard()->format() == "ovf-2.0";
        const bool fCSP =    wizard()->isFormatCloudOne();

        fResult =    (   fOVF
                      && UICommon::hasAllowedExtension(wizard()->path().toLower(), OVFFileExts))
                  || (   fCSP
                      && wizard()->cloudAppliance().isNotNull()
                      && wizard()->cloudClient().isNotNull()
                      && wizard()->vsd().isNotNull()
                      && wizard()->vsdExportForm().isNotNull());
    }

    /* Return result: */
    return fResult;
}

bool UIWizardExportAppPageExpert::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud target selected: */
    if (wizard()->isFormatCloudOne())
    {
        /* Make sure table has own data committed: */
        m_pFormEditor->makeSureEditorDataCommitted();

        /* Init VSD form: */
        CVirtualSystemDescriptionForm comForm;
        /* Check whether we have proper VSD form: */
        if (!m_fLaunching)
        {
            /* We are going to upload image: */
            comForm = wizard()->vsdExportForm();
            fResult = comForm.isNotNull();
        }
        else
        {
            /* We are going to launch VM: */
            comForm = wizard()->vsdLaunchForm();
            fResult = comForm.isNotNull();
        }
        /* Give changed VSD back: */
        if (fResult)
        {
            comForm.GetVirtualSystemDescription();
            fResult = comForm.isOk();
            if (!fResult)
                UINotificationMessage::cannotAcquireVirtualSystemDescriptionFormParameter(comForm, wizard()->notificationCenter());
        }

        /* Final stage? */
        if (fResult)
        {
            if (!m_fLaunching)
            {
                /* For modes other than AskThenExport, try to export appliance first: */
                if (wizard()->cloudExportMode() != CloudExportMode_AskThenExport)
                    fResult = wizard()->exportAppliance();

                /* For modes other than DoNotAsk, switch from uploading image to launching VM: */
                if (   fResult
                    && wizard()->cloudExportMode() != CloudExportMode_DoNotAsk)
                {
                    /* Invert flags: */
                    fResult = false;
                    m_fLaunching = true;

                    /* Disable wizard buttons: */
                    wizard()->disableButtons();

                    /* Disable unrelated widgets: */
                    m_pToolBox->setCurrentPage(2);
                    m_pToolBox->setPageEnabled(0, false);
                    m_pToolBox->setPageEnabled(1, false);

                    /* Refresh corresponding widgets: */
                    wizard()->createVsdLaunchForm();
                    refreshFormPropertiesTable(m_pFormEditor, wizard()->vsdLaunchForm(), wizard()->isFormatCloudOne());
                }
            }
            else
            {
                /* For AskThenExport mode, try to export appliance in the end: */
                if (wizard()->cloudExportMode() == CloudExportMode_AskThenExport)
                    fResult = wizard()->exportAppliance();

                /* Try to create cloud VM: */
                if (fResult)
                    fResult = wizard()->createCloudVM();
            }
        }
    }
    /* Otherwise if there was local target selected: */
    else
    {
        /* Ask user about machines which are in Saved state currently: */
        QStringList savedMachines;
        refreshSavedMachines(savedMachines, m_pVMSelector);
        if (!savedMachines.isEmpty())
            fResult = msgCenter().confirmExportMachinesInSaveState(savedMachines, this);

        /* Prepare export: */
        if (fResult)
            m_pApplianceWidget->prepareExport();

        /* Try to export appliance: */
        if (fResult)
            fResult = wizard()->exportAppliance();
    }

    /* Return result: */
    return fResult;
}

void UIWizardExportAppPageExpert::sltHandleVMItemSelectionChanged()
{
    /* Update wizard fields: */
    wizard()->setMachineNames(machineNames(m_pVMSelector));
    wizard()->setMachineIDs(machineIDs(m_pVMSelector));

    /* Refresh required settings: */
    refreshFileSelectorName(m_strFileSelectorName, wizard()->machineNames(), m_strDefaultApplianceName, wizard()->isFormatCloudOne());
    refreshFileSelectorPath(m_pFileSelector, m_strFileSelectorName, m_strFileSelectorExt, wizard()->isFormatCloudOne());

    /* Update local stuff: */
    updateLocalStuff();
    /* Update cloud stuff: */
    updateCloudStuff();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleFormatComboChange()
{
    /* Update combo tool-tip: */
    updateFormatComboToolTip(m_pFormatComboBox);

    /* Update wizard fields: */
    wizard()->setFormat(format(m_pFormatComboBox));
    wizard()->setFormatCloudOne(isFormatCloudOne(m_pFormatComboBox));

    /* Refresh settings widget state: */
    UIWizardExportAppFormat::refreshStackedWidget(m_pSettingsWidget1, wizard()->isFormatCloudOne());
    UIWizardExportAppSettings::refreshStackedWidget(m_pSettingsWidget2, wizard()->isFormatCloudOne());

    /* Update export settings: */
    refreshFileSelectorExtension(m_strFileSelectorExt, m_pFileSelector, wizard()->isFormatCloudOne());
    refreshFileSelectorPath(m_pFileSelector, m_strFileSelectorName, m_strFileSelectorExt, wizard()->isFormatCloudOne());
    refreshManifestCheckBoxAccess(m_pManifestCheckbox, wizard()->isFormatCloudOne());
    refreshIncludeISOsCheckBoxAccess(m_pIncludeISOsCheckbox, wizard()->isFormatCloudOne());
    refreshProfileCombo(m_pProfileComboBox, wizard()->notificationCenter(), wizard()->format(), wizard()->isFormatCloudOne());
    refreshCloudExportMode(m_exportModeButtons, wizard()->isFormatCloudOne());

    /* Update local stuff: */
    updateLocalStuff();
    /* Update profile: */
    sltHandleProfileComboChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleFileSelectorChange()
{
    /* Skip empty paths: */
    if (m_pFileSelector->path().isEmpty())
        return;

    m_strFileSelectorName = QFileInfo(m_pFileSelector->path()).completeBaseName();
    wizard()->setPath(m_pFileSelector->path());
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleMACAddressExportPolicyComboChange()
{
    updateMACAddressExportPolicyComboToolTip(m_pMACComboBox);
    wizard()->setMACAddressExportPolicy(m_pMACComboBox->currentData().value<MACAddressExportPolicy>());
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleManifestCheckBoxChange()
{
    wizard()->setManifestSelected(m_pManifestCheckbox->isChecked());
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleIncludeISOsCheckBoxChange()
{
    wizard()->setIncludeISOsSelected(m_pIncludeISOsCheckbox->isChecked());
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleProfileComboChange()
{
    /* Update wizard fields: */
    wizard()->setProfileName(profileName(m_pProfileComboBox));

    /* Update export settings: */
    refreshCloudProfile(m_comCloudProfile,
                        wizard()->notificationCenter(),
                        wizard()->format(),
                        wizard()->profileName(),
                        wizard()->isFormatCloudOne());

    /* Update cloud stuff: */
    updateCloudStuff();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleRadioButtonToggled(QAbstractButton *pButton, bool fToggled)
{
    /* Handle checked buttons only: */
    if (!fToggled)
        return;

    /* Update cloud export mode field value: */
    wizard()->setCloudExportMode(m_exportModeButtons.key(pButton));
    emit completeChanged();
}

void UIWizardExportAppPageExpert::sltHandleProfileButtonClick()
{
    /* Open Cloud Profile Manager: */
    if (gpManager)
        gpManager->openCloudProfileManager();
}

void UIWizardExportAppPageExpert::updateLocalStuff()
{
    /* Create appliance: */
    CAppliance comAppliance;
    refreshLocalStuff(comAppliance, wizard(), wizard()->machineIDs(), wizard()->uri());
    wizard()->setLocalAppliance(comAppliance);
}

void UIWizardExportAppPageExpert::updateCloudStuff()
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

    /* Refresh corresponding widgets: */
    refreshApplianceSettingsWidget(m_pApplianceWidget, wizard()->localAppliance(), wizard()->isFormatCloudOne());
    refreshFormPropertiesTable(m_pFormEditor, wizard()->vsdExportForm(), wizard()->isFormatCloudOne());
}
