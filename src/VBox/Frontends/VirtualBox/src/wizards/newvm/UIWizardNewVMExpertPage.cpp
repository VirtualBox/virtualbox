/* $Id: UIWizardNewVMExpertPage.cpp 108994 2025-03-28 13:45:53Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMExpertPage class implementation.
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
#include <QButtonGroup>
#include <QCheckBox>
#include <QRadioButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UIGlobalSession.h"
#include "UIMediaComboBox.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UINameAndSystemEditor.h"
#include "UINotificationCenter.h"
#include "UIToolBox.h"
#include "UIWizardNewVM.h"
#include "UIWizardDiskEditors.h"
#include "UIWizardNewVMDiskPage.h"
#include "UIWizardNewVMEditors.h"
#include "UIWizardNewVMExpertPage.h"
#include "UIWizardNewVMNameOSTypePage.h"

/* COM includes: */
#include "CSystemProperties.h"

UIWizardNewVMExpertPage::UIWizardNewVMExpertPage(UIActionPool *pActionPool)
    : m_pToolBox(0)
    , m_pDiskFormatVariantGroupBox(0)
    , m_pDiskVariantWidget(0)
    , m_pFormatComboBox(0)
    , m_pSizeAndLocationGroup(0)
    , m_pNameAndSystemEditor(0)
    , m_pSkipUnattendedCheckBox(0)
    , m_pNameAndSystemLayout(0)
    , m_pHardwareWidgetContainer(0)
    , m_pAdditionalOptionsContainer(0)
    , m_pGAInstallationISOContainer(0)
    , m_pDiskSourceButtonGroup(0)
    , m_pDiskEmpty(0)
    , m_pDiskNew(0)
    , m_pDiskExisting(0)
    , m_pDiskSelector(0)
    , m_pDiskSelectionButton(0)
    , m_fRecommendedNoDisk(false)
    , m_uMediumSizeMin(_4M)
    , m_uMediumSizeMax(gpGlobalSession->virtualBox().GetSystemProperties().GetInfoVDSize())
    , m_pActionPool(pActionPool)
{
    /* Create widgets: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    {
        m_pToolBox = new UIToolBox;
        m_pToolBox->insertPage(ExpertToolboxItems_NameAndOSType, createNameOSTypeWidgets(), "");
        m_pToolBox->insertPage(ExpertToolboxItems_Unattended, createUnattendedWidgets(), "");
        m_pHardwareWidgetContainer = new UINewVMHardwareContainer;
        m_pToolBox->insertPage(ExpertToolboxItems_Hardware, m_pHardwareWidgetContainer, "");
        m_pToolBox->insertPage(ExpertToolboxItems_Disk, createDiskWidgets(), "");
        m_pToolBox->setCurrentPage(ExpertToolboxItems_NameAndOSType);
        pMainLayout->addWidget(m_pToolBox);
        pMainLayout->addStretch();
    }

    createConnections();

    /* Register classes: */
    qRegisterMetaType<CMedium>();
}

void UIWizardNewVMExpertPage::setISOFilePath(const QString &strISOFilePath)
{
    QFileInfo isoFileInfo(strISOFilePath);
    if (isoFileInfo.exists() && m_pNameAndSystemEditor)
        m_pNameAndSystemEditor->setISOImagePath(strISOFilePath);
}

void UIWizardNewVMExpertPage::sltNameChanged(const QString &strNewName)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    /* Allow type guessing from name only if an OS type from ISO could not be detected: */
    if (!m_userModifiedParameters.contains("GuestOSTypeFromISO") && m_pNameAndSystemEditor)
    {
        m_pNameAndSystemEditor->blockSignals(true);
        if (UIWizardNewVMNameOSTypeCommon::guessOSTypeFromName(m_pNameAndSystemEditor, strNewName))
        {
            wizardWindow<UIWizardNewVM>()->setGuestOSTypeId(m_pNameAndSystemEditor->typeId());
            /* Since the type `possibly` changed: */
            setOSTypeDependedValues();
            m_userModifiedParameters << "GuestOSTypeFromName";
        }
        m_pNameAndSystemEditor->blockSignals(false);
    }
    UIWizardNewVMNameOSTypeCommon::composeMachineFilePath(m_pNameAndSystemEditor, wizardWindow<UIWizardNewVM>());
    if (!m_userModifiedParameters.contains("MediumPath"))
        updateVirtualMediumPathFromMachinePathName();
    if (!m_userModifiedParameters.contains("HostnameDomainName"))
        updateHostnameDomainNameFromMachineName();
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltPathChanged(const QString &strNewPath)
{
    Q_UNUSED(strNewPath);
    UIWizardNewVMNameOSTypeCommon::composeMachineFilePath(m_pNameAndSystemEditor, wizardWindow<UIWizardNewVM>());
    if (!m_userModifiedParameters.contains("MediumPath"))
        updateVirtualMediumPathFromMachinePathName();
}

void UIWizardNewVMExpertPage::sltOsTypeChanged()
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    /* Don't add GuestOSType to the set since we want to adjust os type when installation ISO changes. No matter if user
     * has already set the os type explicitly or not: */
    //m_userModifiedParameters << "GuestOSType";
    if (m_pNameAndSystemEditor)
        wizardWindow<UIWizardNewVM>()->setGuestOSTypeId(m_pNameAndSystemEditor->typeId());
    setOSTypeDependedValues();
}

void UIWizardNewVMExpertPage::sltGetWithFileOpenDialog()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    QUuid uMediumId = UIWizardNewVMDiskCommon::getWithFileOpenDialog(pWizard->guestOSTypeId(),
                                                                     pWizard->machineFolder(),
                                                                     this, m_pActionPool);
    if (!uMediumId.isNull())
    {
        m_pDiskSelector->setCurrentItem(uMediumId);
        m_pDiskSelector->setFocus();
    }
}

void UIWizardNewVMExpertPage::sltISOPathChanged(const QString &strISOPath)
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);

    pWizard->setISOFilePath(strISOPath);

    bool const fOsTypeFixed = UIWizardNewVMNameOSTypeCommon::guessOSTypeDetectedOSTypeString(m_pNameAndSystemEditor,
                                                                                             pWizard->detectedOSTypeId());
    if (fOsTypeFixed)
        m_userModifiedParameters << "GuestOSTypeFromISO";
    else /* Remove GuestOSTypeFromISO from the set if it is there: */
        m_userModifiedParameters.remove("GuestOSTypeFromISO");

    /* Update the global recent ISO path: */
    QFileInfo fileInfo(strISOPath);
    if (fileInfo.exists() && fileInfo.isReadable())
        gpMediumEnumerator->updateRecentlyUsedMediumListAndFolder(UIMediumDeviceType_DVD, strISOPath);

    /* Populate the editions selector: */
    if (m_pNameAndSystemEditor)
        m_pNameAndSystemEditor->setEditionNameAndIndices(pWizard->detectedWindowsImageNames(),
                                                         pWizard->detectedWindowsImageIndices());
    setSkipCheckBoxEnable();
    disableEnableUnattendedRelatedWidgets(isUnattendedEnabled());

    /* Redetect the OS type using the name if detection or the step above failed: */
    if (!fOsTypeFixed && m_pNameAndSystemEditor)
        sltNameChanged(m_pNameAndSystemEditor->name());

    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltGAISOPathChanged(const QString &strPath)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "GuestAdditionsISOPath";
    wizardWindow<UIWizardNewVM>()->setGuestAdditionsISOPath(strPath);
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltInstallGACheckBoxToggle(bool fEnabled)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setInstallGuestAdditions(fEnabled);
    m_userModifiedParameters << "InstallGuestAdditions";
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltOSFamilyTypeChanged(const QString &strGuestOSFamilyType)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    if (m_pAdditionalOptionsContainer)
        m_pAdditionalOptionsContainer->disableEnableProductKeyWidgets(isProductKeyWidgetEnabled());
    m_userModifiedParameters << "GuestOSFamilyId";
    wizardWindow<UIWizardNewVM>()->setGuestOSFamilyId(strGuestOSFamilyType);
}

void UIWizardNewVMExpertPage::sltRetranslateUI()
{
    if (m_pSkipUnattendedCheckBox)
    {
        m_pSkipUnattendedCheckBox->setText(UIWizardNewVM::tr("&Skip Unattended Installation"));
        m_pSkipUnattendedCheckBox->setToolTip(UIWizardNewVM::tr("When checked, the unattended install is disabled and the selected ISO "
                                                                "is mounted on the vm."));
    }

    if (m_pToolBox)
    {
        m_pToolBox->setPageTitle(ExpertToolboxItems_NameAndOSType, QString(UIWizardNewVM::tr("Name and &Operating System")));
        m_pToolBox->setPageTitle(ExpertToolboxItems_Unattended, UIWizardNewVM::tr("&Unattended Install"));
        m_pToolBox->setPageTitle(ExpertToolboxItems_Disk, UIWizardNewVM::tr("Hard Dis&k"));
        m_pToolBox->setPageTitle(ExpertToolboxItems_Hardware, UIWizardNewVM::tr("H&ardware"));
    }

    if (m_pDiskEmpty)
        m_pDiskEmpty->setText(UIWizardNewVM::tr("&Do Not Add a Virtual Hard Disk"));
    if (m_pDiskNew)
        m_pDiskNew->setText(UIWizardNewVM::tr("&Create a Virtual Hard Disk Now"));
    if (m_pDiskExisting)
        m_pDiskExisting->setText(UIWizardNewVM::tr("U&se an Existing Virtual Hard Disk File"));
    if (m_pDiskSelectionButton)
        m_pDiskSelectionButton->setToolTip(UIWizardNewVM::tr("Chooses a Virtual Hard Fisk File..."));
    if (m_pDiskSelectionButton)
        m_pDiskSelectionButton->setText(UIWizardNewVM::tr("Choose a Virtual Hard Fisk File"));

    if (m_pNameAndSystemLayout && m_pNameAndSystemEditor)
        m_pNameAndSystemLayout->setColumnMinimumWidth(0, m_pNameAndSystemEditor->firstColumnWidth());

    if (m_pDiskFormatVariantGroupBox)
        m_pDiskFormatVariantGroupBox->setTitle(UIWizardNewVM::tr("Hard Disk File &Type and Variant"));
}

void UIWizardNewVMExpertPage::createConnections()
{
    /* Connections for Name, OS Type, and unattended install stuff: */
    if (m_pNameAndSystemEditor)
    {
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigNameChanged,
                this, &UIWizardNewVMExpertPage::sltNameChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigPathChanged,
                this, &UIWizardNewVMExpertPage::sltPathChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigOsTypeChanged,
                this, &UIWizardNewVMExpertPage::sltOsTypeChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigOSFamilyChanged,
                this, &UIWizardNewVMExpertPage::sltOSFamilyTypeChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigImageChanged,
                this, &UIWizardNewVMExpertPage::sltISOPathChanged);
        connect(m_pNameAndSystemEditor, &UINameAndSystemEditor::sigEditionChanged,
                this, &UIWizardNewVMExpertPage::sltSelectedEditionChanged);
    }

    if (m_pHardwareWidgetContainer)
    {
        connect(m_pHardwareWidgetContainer, &UINewVMHardwareContainer::sigMemorySizeChanged,
                this, &UIWizardNewVMExpertPage::sltMemorySizeChanged);
        connect(m_pHardwareWidgetContainer, &UINewVMHardwareContainer::sigCPUCountChanged,
                this, &UIWizardNewVMExpertPage::sltCPUCountChanged);
        connect(m_pHardwareWidgetContainer, &UINewVMHardwareContainer::sigEFIEnabledChanged,
                this, &UIWizardNewVMExpertPage::sltEFIEnabledChanged);
    }
    /* Connections for username, password, and hostname, etc: */
    if (m_pGAInstallationISOContainer)
    {
        connect(m_pGAInstallationISOContainer, &UIGAInstallationGroupBox::sigPathChanged,
                this, &UIWizardNewVMExpertPage::sltGAISOPathChanged);
        connect(m_pGAInstallationISOContainer, &UIGAInstallationGroupBox::toggled,
                this, &UIWizardNewVMExpertPage::sltInstallGACheckBoxToggle);
    }

    if (m_pUserNamePasswordGroupBox)
    {
        connect(m_pUserNamePasswordGroupBox, &UIUserNamePasswordGroupBox::sigPasswordChanged,
                this, &UIWizardNewVMExpertPage::sltPasswordChanged);
        connect(m_pUserNamePasswordGroupBox, &UIUserNamePasswordGroupBox::sigUserNameChanged,
                this, &UIWizardNewVMExpertPage::sltUserNameChanged);
    }

    if (m_pAdditionalOptionsContainer)
    {
        connect(m_pAdditionalOptionsContainer, &UIAdditionalUnattendedOptions::sigHostnameDomainNameChanged,
                this, &UIWizardNewVMExpertPage::sltHostnameDomainNameChanged);
        connect(m_pAdditionalOptionsContainer, &UIAdditionalUnattendedOptions::sigProductKeyChanged,
                this, &UIWizardNewVMExpertPage::sltProductKeyChanged);
        connect(m_pAdditionalOptionsContainer, &UIAdditionalUnattendedOptions::sigStartHeadlessChanged,
                this, &UIWizardNewVMExpertPage::sltStartHeadlessChanged);
    }

    /* Virtual disk related connections: */
    if (m_pDiskSourceButtonGroup)
        connect(m_pDiskSourceButtonGroup, &QButtonGroup::buttonClicked,
                this, &UIWizardNewVMExpertPage::sltSelectedDiskSourceChanged);

    if (m_pSkipUnattendedCheckBox)
        connect(m_pSkipUnattendedCheckBox, &QCheckBox::toggled,
                this, &UIWizardNewVMExpertPage::sltSkipUnattendedCheckBoxChecked);

    if (m_pSizeAndLocationGroup)
    {
        connect(m_pSizeAndLocationGroup, &UIMediumSizeAndPathGroupBox::sigMediumSizeChanged,
                this, &UIWizardNewVMExpertPage::sltMediumSizeChanged);
        connect(m_pSizeAndLocationGroup, &UIMediumSizeAndPathGroupBox::sigMediumPathChanged,
                this, &UIWizardNewVMExpertPage::sltMediumPathChanged);
        connect(m_pSizeAndLocationGroup, &UIMediumSizeAndPathGroupBox::sigMediumLocationButtonClicked,
                this, &UIWizardNewVMExpertPage::sltMediumLocationButtonClicked);
    }

    if (m_pDiskSelectionButton)
        connect(m_pDiskSelectionButton, &QIToolButton::clicked,
                this, &UIWizardNewVMExpertPage::sltGetWithFileOpenDialog);

    if (m_pDiskSelector)
        connect(m_pDiskSelector, &UIMediaComboBox::currentIndexChanged,
                this, &UIWizardNewVMExpertPage::sltMediaComboBoxIndexChanged);

    if (m_pFormatComboBox)
        connect(m_pFormatComboBox, &UIDiskFormatsComboBox::sigMediumFormatChanged,
                this, &UIWizardNewVMExpertPage::sltMediumFormatChanged);

    if (m_pDiskVariantWidget)
        connect(m_pDiskVariantWidget, &UIDiskVariantWidget::sigMediumVariantChanged,
                this, &UIWizardNewVMExpertPage::sltMediumVariantChanged);
}

void UIWizardNewVMExpertPage::setOSTypeDependedValues()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);

    QString strTypeId = pWizard->guestOSTypeId();
    /* Get recommended 'ram' field value: */
    ULONG recommendedRam = gpGlobalSession->guestOSTypeManager().getRecommendedRAM(strTypeId);

    if (m_pHardwareWidgetContainer)
    {
        m_pHardwareWidgetContainer->blockSignals(true);

        /* Set memory size of the widget and the wizard: */
        if (!m_userModifiedParameters.contains("MemorySize"))
        {
            m_pHardwareWidgetContainer->setMemorySize(recommendedRam);
            pWizard->setMemorySize(recommendedRam);
        }

        /* Set Firmware Type of the widget and the wizard: */
        KFirmwareType fwType = gpGlobalSession->guestOSTypeManager().getRecommendedFirmware(strTypeId);
        if (!m_userModifiedParameters.contains("EFIEnabled"))
        {
            m_pHardwareWidgetContainer->setEFIEnabled(fwType != KFirmwareType_BIOS);
            pWizard->setEFIEnabled(fwType != KFirmwareType_BIOS);
        }

        /* Initialize CPU count:*/
        int iCPUCount = gpGlobalSession->guestOSTypeManager().getRecommendedCPUCount(strTypeId);
        if (!m_userModifiedParameters.contains("CPUCount"))
        {
            m_pHardwareWidgetContainer->setCPUCount(iCPUCount);
            pWizard->setCPUCount(iCPUCount);
        }
        m_pHardwareWidgetContainer->blockSignals(false);
    }

    LONG64 iRecommendedDiskSize = gpGlobalSession->guestOSTypeManager().getRecommendedHDD(strTypeId);
    /* Prepare initial disk choice: */
    if (!m_userModifiedParameters.contains("SelectedDiskSource"))
    {
        if (iRecommendedDiskSize != 0)
        {
            if (m_pDiskNew)
                m_pDiskNew->setChecked(true);
            pWizard->setDiskSource(SelectedDiskSource_New);
            setEnableDiskSelectionWidgets(false);
            setEnableNewDiskWidgets(true);
            m_fRecommendedNoDisk = false;
        }
        else
        {
            if (m_pDiskEmpty)
                m_pDiskEmpty->setChecked(true);
            pWizard->setDiskSource(SelectedDiskSource_Empty);
            setEnableDiskSelectionWidgets(false);
            setEnableNewDiskWidgets(false);
            m_fRecommendedNoDisk = true;
        }
        if (m_pDiskSelector)
            m_pDiskSelector->setCurrentIndex(0);
    }
    /* Initialize the medium size widgets and the member parameter of the wizard: */
    if (m_pSizeAndLocationGroup  && !m_userModifiedParameters.contains("MediumSize"))
    {
        m_pSizeAndLocationGroup->setMediumSize(iRecommendedDiskSize);
        pWizard->setMediumSize(iRecommendedDiskSize);
    }
}

void UIWizardNewVMExpertPage::initializePage()
{
    /* We need not to check existence of parameter within m_userModifiedParameters since initializePage() runs
       once the page loads before user has a chance to modify parameters explicitly: */
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    /* Initialize wizard properties: */
    {
        if (m_pNameAndSystemEditor)
        {
            /* Guest OS type: */
            pWizard->setGuestOSFamilyId(m_pNameAndSystemEditor->familyId());
            pWizard->setGuestOSTypeId(m_pNameAndSystemEditor->typeId());
            /* Vm name, folder, file path etc. will be initilized by composeMachineFilePath: */
        }

        /* Medium related properties: */
        if (m_pFormatComboBox)
            pWizard->setMediumFormat(m_pFormatComboBox->mediumFormat());
        updateVirtualMediumPathFromMachinePathName();
    }

    /* Initialize user/password if they are not modified by the user: */
    if (m_pUserNamePasswordGroupBox)
    {
        m_pUserNamePasswordGroupBox->blockSignals(true);
        m_pUserNamePasswordGroupBox->setUserName(pWizard->userName());
        m_pUserNamePasswordGroupBox->setPassword(pWizard->password());
        m_pUserNamePasswordGroupBox->blockSignals(false);
    }
    updateHostnameDomainNameFromMachineName();

    if (m_pGAInstallationISOContainer)
    {
        m_pGAInstallationISOContainer->blockSignals(true);
        m_pGAInstallationISOContainer->setChecked(pWizard->installGuestAdditions());
        m_pGAInstallationISOContainer->setPath(pWizard->guestAdditionsISOPath());
        m_pGAInstallationISOContainer->blockSignals(false);
    }

    setOSTypeDependedValues();
    setSkipCheckBoxEnable();
    disableEnableUnattendedRelatedWidgets(isUnattendedEnabled());
    updateDiskWidgetsAfterMediumFormatChange();
    sltRetranslateUI();

    /* Focus on the name field (rather than the help button): */
    if (m_pNameAndSystemEditor)
        m_pNameAndSystemEditor->setFocus();
}

void UIWizardNewVMExpertPage::markWidgets() const
{
    if (m_pNameAndSystemEditor)
    {
        m_pNameAndSystemEditor->markNameEditor(m_pNameAndSystemEditor->name().isEmpty(),
                                               UIWizardNewVM::tr("Guest machine name cannot be empty"),
                                               UIWizardNewVM::tr("Guest machine name is valid"));
        m_pNameAndSystemEditor->markImageEditor(!UIWizardNewVMNameOSTypeCommon::checkISOFile(m_pNameAndSystemEditor),
                                                UIWizardNewVM::tr("Invalid file path or unreadable file"),
                                                UIWizardNewVM::tr("File path is valid"));
        m_pNameAndSystemEditor->markNameEditor((QDir(m_pNameAndSystemEditor->fullPath()).exists()),
                                               UIWizardNewVM::tr("Guest machine path is not unique"),
                                               UIWizardNewVM::tr("Guest machine name is valid"));
    }
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    if (pWizard && pWizard->installGuestAdditions() && m_pGAInstallationISOContainer)
        m_pGAInstallationISOContainer->mark();
    if (isUnattendedEnabled())
        m_pAdditionalOptionsContainer->mark();
}

QWidget *UIWizardNewVMExpertPage::createUnattendedWidgets()
{
    QWidget *pContainerWidget = new QWidget;
    QGridLayout *pLayout = new QGridLayout(pContainerWidget);
    pLayout->setContentsMargins(0, 0, 0, 0);
    int iRow = 0;
    m_pUserNamePasswordGroupBox = new UIUserNamePasswordGroupBox;
    AssertReturn(m_pUserNamePasswordGroupBox, 0);
    pLayout->addWidget(m_pUserNamePasswordGroupBox, iRow, 0, 1, 2);

    m_pAdditionalOptionsContainer = new UIAdditionalUnattendedOptions;
    AssertReturn(m_pAdditionalOptionsContainer, 0);
    pLayout->addWidget(m_pAdditionalOptionsContainer, iRow, 2, 1, 2);

    ++iRow;

    /* Guest additions installation: */
    m_pGAInstallationISOContainer = new UIGAInstallationGroupBox;
    AssertReturn(m_pGAInstallationISOContainer, 0);
    pLayout->addWidget(m_pGAInstallationISOContainer, iRow, 0, 1, 4);

    return pContainerWidget;
}

QWidget *UIWizardNewVMExpertPage::createNewDiskWidgets()
{
    QWidget *pNewDiskContainerWidget = new QWidget;
    QGridLayout *pDiskContainerLayout = new QGridLayout(pNewDiskContainerWidget);
    pDiskContainerLayout->setContentsMargins(0, 0, 0, 0);

    m_pSizeAndLocationGroup = new UIMediumSizeAndPathGroupBox(true, 0 /* parent */, _4M /* minimum size */);
    pDiskContainerLayout->addWidget(m_pSizeAndLocationGroup, 0, 0, 2, 2);

    m_pDiskFormatVariantGroupBox = new QGroupBox;
    QHBoxLayout *pDiskFormatVariantLayout = new QHBoxLayout(m_pDiskFormatVariantGroupBox);

    m_pFormatComboBox = new UIDiskFormatsComboBox(true, KDeviceType_HardDisk, 0);
    pDiskFormatVariantLayout->addWidget(m_pFormatComboBox, 0 /* stretch */, Qt::AlignTop);

    m_pDiskVariantWidget  = new UIDiskVariantWidget(0);
    pDiskFormatVariantLayout->addWidget(m_pDiskVariantWidget);

    pDiskContainerLayout->addWidget(m_pDiskFormatVariantGroupBox, 2, 0, 2, 2);
    return pNewDiskContainerWidget;
}

QWidget *UIWizardNewVMExpertPage::createDiskWidgets()
{
    QWidget *pDiskContainer = new QWidget;
    QGridLayout *pDiskLayout = new QGridLayout(pDiskContainer);
    pDiskLayout->setContentsMargins(0, 0, 0, 0);
    m_pDiskSourceButtonGroup = new QButtonGroup(this);
    m_pDiskEmpty = new QRadioButton;
    m_pDiskNew = new QRadioButton;
    m_pDiskExisting = new QRadioButton;
    m_pDiskSourceButtonGroup->addButton(m_pDiskEmpty);
    m_pDiskSourceButtonGroup->addButton(m_pDiskNew);
    m_pDiskSourceButtonGroup->addButton(m_pDiskExisting);
    QStyleOptionButton options;
    options.initFrom(m_pDiskExisting);
    int iWidth = m_pDiskExisting->style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, &options, m_pDiskExisting);
    pDiskLayout->setColumnMinimumWidth(0, iWidth);
    m_pDiskSelector = new UIMediaComboBox;
    {
        m_pDiskSelector->setType(UIMediumDeviceType_HardDisk);
        m_pDiskSelector->repopulate();
    }
    m_pDiskSelectionButton = new QIToolButton;
    {
        m_pDiskSelectionButton->setAutoRaise(true);
        m_pDiskSelectionButton->setIcon(UIIconPool::iconSet(":/select_file_16px.png", ":/select_file_disabled_16px.png"));
    }
    pDiskLayout->addWidget(m_pDiskNew,             0, 0, 1, 6);
    pDiskLayout->addWidget(createNewDiskWidgets(), 1, 1, 3, 5);
    pDiskLayout->addWidget(m_pDiskExisting,        4, 0, 1, 6);
    pDiskLayout->addWidget(m_pDiskSelector,        5, 1, 1, 4);
    pDiskLayout->addWidget(m_pDiskSelectionButton, 5, 5, 1, 1);
    pDiskLayout->addWidget(m_pDiskEmpty,           6, 0, 1, 6);
    return pDiskContainer;
}

bool UIWizardNewVMExpertPage::isComplete() const
{
    markWidgets();
    bool fIsComplete = true;
    m_pToolBox->setPageTitleIcon(ExpertToolboxItems_NameAndOSType, QIcon());
    m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Unattended, QIcon());
    m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Disk, QIcon());
    m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Hardware, QIcon());

    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);

    /* Check unattended install related stuff: */
    if (isUnattendedEnabled())
    {
        /* Check the installation medium: */
        if (!UIWizardNewVMNameOSTypeCommon::checkISOFile(m_pNameAndSystemEditor))
        {
            m_pToolBox->setPageTitleIcon(ExpertToolboxItems_NameAndOSType,
                                         UIIconPool::iconSet(":/status_error_16px.png"),
                                         UIWizardNewVM::tr("Invalid path or unreadable ISO file"));
            fIsComplete = false;
        }
        /* Check the GA installation medium: */
        if (m_pGAInstallationISOContainer && !m_pGAInstallationISOContainer->isComplete())
        {
            m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Unattended,
                                         UIIconPool::iconSet(":/status_error_16px.png"),
                                         UIWizardNewVM::tr("Invalid path or unreadable ISO file"));

            fIsComplete = false;
        }
        if (m_pUserNamePasswordGroupBox)
        {
            if (!m_pUserNamePasswordGroupBox->isComplete())
            {
                m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Unattended,
                                             UIIconPool::iconSet(":/status_error_16px.png"),
                                             UIWizardNewVM::tr("Invalid username and/or password"));
                fIsComplete = false;
            }
        }
        if (m_pAdditionalOptionsContainer)
        {
            if (!m_pAdditionalOptionsContainer->isComplete())
            {
                m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Unattended,
                                             UIIconPool::iconSet(":/status_error_16px.png"),
                                             UIWizardNewVM::tr("Invalid hostname or domain name"));
                fIsComplete = false;
            }
        }
    }

    if (m_pNameAndSystemEditor)
    {
        if (m_pNameAndSystemEditor->name().isEmpty())
        {
            m_pToolBox->setPageTitleIcon(ExpertToolboxItems_NameAndOSType,
                                         UIIconPool::iconSet(":/status_error_16px.png"),
                                         UIWizardNewVM::tr("Virtual machine name is invalid"));
            fIsComplete = false;
        }
        if (!UIWizardNewVMNameOSTypeCommon::checkISOFile(m_pNameAndSystemEditor))
        {
            m_pToolBox->setPageTitleIcon(ExpertToolboxItems_NameAndOSType,
                                         UIIconPool::iconSet(":/status_error_16px.png"),
                                         UIWizardNewVM::tr("Invalid ISO file"));
            fIsComplete = false;
        }
        if (QDir(m_pNameAndSystemEditor->fullPath()).exists())
        {
            m_pToolBox->setPageTitleIcon(ExpertToolboxItems_NameAndOSType,
                                         UIIconPool::iconSet(":/status_error_16px.png"),
                                         UIWizardNewVM::tr("Guest machine path is not unique"));
            fIsComplete = false;
        }
    }

    if (pWizard->diskSource() == SelectedDiskSource_Existing && gpMediumEnumerator->medium(m_pDiskSelector->id()).isNull())
    {
        m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Disk,
                                     UIIconPool::iconSet(":/status_error_16px.png"), UIWizardNewVM::tr("No valid disk is selected"));
        fIsComplete = false;
    }

    if (pWizard->diskSource() == SelectedDiskSource_New)
    {
        qulonglong uSize = pWizard->mediumSize();
        if( uSize < m_uMediumSizeMin || uSize > m_uMediumSizeMax)
        {
            m_pToolBox->setPageTitleIcon(ExpertToolboxItems_Disk,
                                         UIIconPool::iconSet(":/status_error_16px.png"), UIWizardNewVM::tr("Invalid disk size"));
            fIsComplete = false;
        }
        /* Check only the uniqueness of the file and not path existence since the vm folder (default medium path)
         * has not been created yet: */
        if (!m_pSizeAndLocationGroup->filePathUnique())
            fIsComplete = false;
    }
    return fIsComplete;
}

bool UIWizardNewVMExpertPage::validatePage()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);
    bool fResult = UIWizardNewVMNameOSTypeCommon::createMachineFolder(m_pNameAndSystemEditor, wizardWindow<UIWizardNewVM>());
    if (!fResult)
        return false;

    if (pWizard->diskSource() == SelectedDiskSource_New)
    {
        /* Check if the path we will be using for hard drive creation exists: */
        const QString &strMediumPath = pWizard->mediumPath();
        fResult = !QFileInfo(strMediumPath).exists();
        if (!fResult)
        {
            UINotificationMessage::cannotOverwriteMediumStorage(strMediumPath, wizard()->notificationCenter());
            return fResult;
        }
        qulonglong uSize = pWizard->mediumSize();
        qulonglong uVariant = pWizard->mediumVariant();
        /* Check FAT size limitation of the host hard drive: */
        fResult =  UIWizardDiskEditors::checkFATSizeLimitation(uVariant, strMediumPath, uSize);
        if (!fResult)
        {
            UINotificationMessage::cannotCreateMediumStorageInFAT(strMediumPath, wizard()->notificationCenter());
            return fResult;
        }
        /* Try to create the hard drive:*/
        fResult = pWizard->createVirtualDisk();
        /*Don't show any error message here since UIWizardNewVM::createVirtualDisk already does so: */
        if (!fResult)
            return fResult;
    }

    return pWizard->createVM();
}

bool UIWizardNewVMExpertPage::isProductKeyWidgetEnabled() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    if (!pWizard || !isUnattendedEnabled() || !pWizard->isGuestOSTypeWindows())
        return false;
    return true;
}

void UIWizardNewVMExpertPage::disableEnableUnattendedRelatedWidgets(bool fEnabled)
{
    if (m_pUserNamePasswordGroupBox)
        m_pUserNamePasswordGroupBox->setEnabled(fEnabled);
    if (m_pAdditionalOptionsContainer)
        m_pAdditionalOptionsContainer->setEnabled(fEnabled);
    if (m_pGAInstallationISOContainer)
        m_pGAInstallationISOContainer->setEnabled(fEnabled);
    if (m_pNameAndSystemEditor)
        m_pNameAndSystemEditor->setEditionSelectorEnabled(fEnabled && !m_pNameAndSystemEditor->isEditionsSelectorEmpty());
    m_pAdditionalOptionsContainer->disableEnableProductKeyWidgets(isProductKeyWidgetEnabled());
}

void UIWizardNewVMExpertPage::sltSkipUnattendedCheckBoxChecked(bool fSkip)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "SkipUnattendedInstall";
    wizardWindow<UIWizardNewVM>()->setSkipUnattendedInstall(fSkip);
    disableEnableUnattendedRelatedWidgets(isUnattendedEnabled());
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltMediumFormatChanged()
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    if (!m_pFormatComboBox)
        return;

    m_userModifiedParameters << "MediumFormat";
    wizardWindow<UIWizardNewVM>()->setMediumFormat(m_pFormatComboBox->mediumFormat());
    updateDiskWidgetsAfterMediumFormatChange();
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltMediumSizeChanged(qulonglong uSize)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "MediumSize";
    wizardWindow<UIWizardNewVM>()->setMediumSize(uSize);
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltMediumPathChanged(const QString &strPath)
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    AssertReturnVoid(!strPath.isEmpty());
    m_userModifiedParameters << "MediumPath";
    QString strMediumPath =
        UIWizardDiskEditors::appendExtension(strPath,
                                             UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), KDeviceType_HardDisk));
    pWizard->setMediumPath(strMediumPath);
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltMediumLocationButtonClicked()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    CMediumFormat comMediumFormat(pWizard->mediumFormat());

    QString strMediumPath =
        UIWizardDiskEditors::appendExtension(m_pSizeAndLocationGroup->mediumFilePath(),
                                             UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), KDeviceType_HardDisk));
    QString strSelectedPath =
        UIWizardDiskEditors::openFileDialogForDiskFile(strMediumPath, comMediumFormat, KDeviceType_HardDisk, pWizard);
    if (strSelectedPath.isEmpty())
        return;
    strMediumPath =
        UIWizardDiskEditors::appendExtension(strSelectedPath,
                                             UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), KDeviceType_HardDisk));
    QFileInfo mediumPath(strMediumPath);
    m_pSizeAndLocationGroup->setMediumFilePath(QDir::toNativeSeparators(mediumPath.absoluteFilePath()));
}

void UIWizardNewVMExpertPage::sltMediumVariantChanged(qulonglong uVariant)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "MediumVariant";
    wizardWindow<UIWizardNewVM>()->setMediumVariant(uVariant);
}

void UIWizardNewVMExpertPage::sltMediaComboBoxIndexChanged()
{
    AssertReturnVoid(m_pDiskSelector);
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);

    /* Make sure to set m_virtualDisk: */
    pWizard->setVirtualDisk(m_pDiskSelector->id());
    pWizard->setMediumPath(m_pDiskSelector->location());
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltSelectedDiskSourceChanged()
{
    AssertReturnVoid(m_pDiskSelector && m_pDiskSourceButtonGroup);
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    m_userModifiedParameters << "SelectedDiskSource";

    if (m_pDiskSourceButtonGroup->checkedButton() == m_pDiskEmpty)
    {
        pWizard->setDiskSource(SelectedDiskSource_Empty);
        pWizard->setVirtualDisk(QUuid());
        pWizard->setMediumPath(QString());
    }
    else if (m_pDiskSourceButtonGroup->checkedButton() == m_pDiskExisting)
    {
        pWizard->setDiskSource(SelectedDiskSource_Existing);
        pWizard->setVirtualDisk(m_pDiskSelector->id());
        pWizard->setMediumPath(m_pDiskSelector->location());
    }
    else
    {
        pWizard->setDiskSource(SelectedDiskSource_New);
        pWizard->setVirtualDisk(QUuid());
        pWizard->setMediumPath(QString());
    }

    setEnableDiskSelectionWidgets(pWizard->diskSource() == SelectedDiskSource_Existing);
    setEnableNewDiskWidgets(pWizard->diskSource() == SelectedDiskSource_New);

    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltMemorySizeChanged(int iValue)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setMemorySize(iValue);
    m_userModifiedParameters << "MemorySize";
}

void UIWizardNewVMExpertPage::sltCPUCountChanged(int iCount)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setCPUCount(iCount);
    m_userModifiedParameters << "CPUCount";
}

void UIWizardNewVMExpertPage::sltEFIEnabledChanged(bool fEnabled)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setEFIEnabled(fEnabled);
    m_userModifiedParameters << "EFIEnabled";
}

void UIWizardNewVMExpertPage::sltPasswordChanged(const QString &strPassword)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setPassword(strPassword);
    m_userModifiedParameters << "Password";
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltUserNameChanged(const QString &strUserName)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setUserName(strUserName);
    m_userModifiedParameters << "UserName";
    emit completeChanged();
}

void UIWizardNewVMExpertPage::sltHostnameDomainNameChanged(const QString &strHostnameDomainName, bool fIsComplete)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    emit completeChanged();

    if (fIsComplete)
    {
        wizardWindow<UIWizardNewVM>()->setHostnameDomainName(strHostnameDomainName);
        m_userModifiedParameters << "HostnameDomainName";
    }
}

void UIWizardNewVMExpertPage::sltProductKeyChanged(const QString &strProductKey)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "ProductKey";
    wizardWindow<UIWizardNewVM>()->setProductKey(strProductKey);
}

void UIWizardNewVMExpertPage::sltStartHeadlessChanged(bool fStartHeadless)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    m_userModifiedParameters << "StartHeadless";
    wizardWindow<UIWizardNewVM>()->setStartHeadless(fStartHeadless);
}

void UIWizardNewVMExpertPage::sltSelectedEditionChanged(ulong uEditionIndex)
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    pWizard->setSelectedWindowImageIndex(uEditionIndex);
    /* Update the OS type since IUnattended updates the detected OS type after edition (image index) changes: */
    UIWizardNewVMNameOSTypeCommon::guessOSTypeDetectedOSTypeString(m_pNameAndSystemEditor, pWizard->detectedOSTypeId());
}

void UIWizardNewVMExpertPage::updateVirtualMediumPathFromMachinePathName()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);
    QString strDiskFileName = pWizard->machineFileName().isEmpty() ? QString("NewVirtualDisk1") : pWizard->machineFileName();
    QString strMediumPath = pWizard->machineFolder();
    if (strMediumPath.isEmpty())
    {
        if (m_pNameAndSystemEditor)
            strMediumPath = m_pNameAndSystemEditor->path();
        else
            strMediumPath = gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();
    }
    QString strExtension = UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), KDeviceType_HardDisk);
    if (m_pSizeAndLocationGroup)
    {
        QString strMediumFilePath =
            UIWizardDiskEditors::constructMediumFilePath(UIWizardDiskEditors::appendExtension(strDiskFileName,
                                                                                              strExtension), strMediumPath);
        m_pSizeAndLocationGroup->blockSignals(true);
        m_pSizeAndLocationGroup->setMediumFilePath(strMediumFilePath);
        m_pSizeAndLocationGroup->blockSignals(false);
        pWizard->setMediumPath(m_pSizeAndLocationGroup->mediumFilePath());
    }
}

void UIWizardNewVMExpertPage::updateDiskWidgetsAfterMediumFormatChange()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard && m_pDiskVariantWidget && m_pSizeAndLocationGroup && m_pFormatComboBox);
    const CMediumFormat &comMediumFormat = pWizard->mediumFormat();
    AssertReturnVoid(!comMediumFormat.isNull());

    /* Block signals of the updated widgets to avoid calling corresponding slots since they add the parameters to m_userModifiedParameters: */
    m_pDiskVariantWidget->blockSignals(true);
    m_pDiskVariantWidget->updateMediumVariantWidgetsAfterFormatChange(comMediumFormat);
    m_pDiskVariantWidget->blockSignals(false);

    m_pSizeAndLocationGroup->blockSignals(true);
    m_pSizeAndLocationGroup->updateMediumPath(comMediumFormat, m_pFormatComboBox->formatExtensions(), KDeviceType_HardDisk);
    m_pSizeAndLocationGroup->blockSignals(false);
    /* Update the wizard parameters explicitly since we blocked th signals: */
    pWizard->setMediumPath(m_pSizeAndLocationGroup->mediumFilePath());
    pWizard->setMediumVariant(m_pDiskVariantWidget->mediumVariant());
}

void UIWizardNewVMExpertPage::setEnableNewDiskWidgets(bool fEnable)
{
    if (m_pSizeAndLocationGroup)
        m_pSizeAndLocationGroup->setEnabled(fEnable);
    if (m_pFormatComboBox)
        m_pFormatComboBox->setEnabled(fEnable);
    if (m_pDiskVariantWidget)
        m_pDiskVariantWidget->setEnabled(fEnable);
}

QWidget *UIWizardNewVMExpertPage::createNameOSTypeWidgets()
{
    QWidget *pContainerWidget = new QWidget;
    AssertReturn(pContainerWidget, 0);
    m_pNameAndSystemLayout = new QGridLayout(pContainerWidget);
    AssertReturn(m_pNameAndSystemLayout, 0);
    m_pNameAndSystemLayout->setContentsMargins(0, 0, 0, 0);
    m_pNameAndSystemEditor = new UINameAndSystemEditor(0,
                                                       true /* fChooseName? */,
                                                       true /* fChoosePath? */,
                                                       true /* fChooseImage? */,
                                                       true /* fChoseEdition? */,
                                                       true /* fChooseType? */);
    if (m_pNameAndSystemEditor)
        m_pNameAndSystemLayout->addWidget(m_pNameAndSystemEditor, 0, 0, 1, 2);
    m_pSkipUnattendedCheckBox = new QCheckBox;
    if (m_pSkipUnattendedCheckBox)
        m_pNameAndSystemLayout->addWidget(m_pSkipUnattendedCheckBox, 1, 1);
    return pContainerWidget;
}

void UIWizardNewVMExpertPage::setSkipCheckBoxEnable()
{
    AssertReturnVoid(m_pSkipUnattendedCheckBox && m_pNameAndSystemEditor);
    const QString &strPath = m_pNameAndSystemEditor->ISOImagePath();
    if (strPath.isEmpty())
    {
        m_pSkipUnattendedCheckBox->setEnabled(false);
        return;
    }
    if (!isUnattendedInstallSupported())
    {
        m_pSkipUnattendedCheckBox->setEnabled(false);
        return;
    }

    m_pSkipUnattendedCheckBox->setEnabled(UIWizardNewVMNameOSTypeCommon::checkISOFile(m_pNameAndSystemEditor));
}

void UIWizardNewVMExpertPage::updateHostnameDomainNameFromMachineName()
{
    if (!m_pAdditionalOptionsContainer)
        return;
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);

    m_pAdditionalOptionsContainer->blockSignals(true);
    m_pAdditionalOptionsContainer->setHostname(pWizard->machineBaseName());
    m_pAdditionalOptionsContainer->setDomainName("myguest.virtualbox.org");
    /* Initialize unattended hostname here since we cannot get the default value from CUnattended this early (unlike username etc): */
    if (m_pAdditionalOptionsContainer->isHostnameComplete())
        pWizard->setHostnameDomainName(m_pAdditionalOptionsContainer->hostnameDomainName());

    m_pAdditionalOptionsContainer->blockSignals(false);
}

bool UIWizardNewVMExpertPage::isUnattendedEnabled() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);
    return pWizard->isUnattendedEnabled();
}

bool UIWizardNewVMExpertPage::isUnattendedInstallSupported() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);
    return pWizard->isUnattendedInstallSupported();
}

void UIWizardNewVMExpertPage::setEnableDiskSelectionWidgets(bool fEnabled)
{
    if (!m_pDiskSelector || !m_pDiskSelectionButton)
        return;

    m_pDiskSelector->setEnabled(fEnabled);
    m_pDiskSelectionButton->setEnabled(fEnabled);
}
