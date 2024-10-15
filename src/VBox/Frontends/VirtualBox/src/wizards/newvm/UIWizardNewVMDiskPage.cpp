/* $Id: UIWizardNewVMDiskPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVMDiskPage class implementation.
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
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UIGuestOSType.h"
#include "UIMediaComboBox.h"
#include "UIMediumSelector.h"
#include "UIMediumSizeEditor.h"
#include "UIGlobalSession.h"
#include "UIWizardNewVMDiskPage.h"
#include "UIWizardDiskEditors.h"
#include "UIWizardNewVM.h"

/* COM includes: */
#include "CSystemProperties.h"

QUuid UIWizardNewVMDiskCommon::getWithFileOpenDialog(const QString &strOSTypeID,
                                                     const QString &strMachineFolder,
                                                     QWidget *pCaller, UIActionPool *pActionPool)
{
    QUuid uMediumId;
    int returnCode = UIMediumSelector::openMediumSelectorDialog(pCaller, UIMediumDeviceType_HardDisk,
                                                         QUuid() /* current medium id */,
                                                         uMediumId,
                                                         strMachineFolder,
                                                         QString() /* strMachineName */,
                                                         strOSTypeID,
                                                         false /* don't show/enable the create action: */,
                                                         QUuid() /* Machinie Id */, pActionPool);
    if (returnCode != static_cast<int>(UIMediumSelector::ReturnCode_Accepted))
        return QUuid();
    return uMediumId;
}

UIWizardNewVMDiskPage::UIWizardNewVMDiskPage(UIActionPool *pActionPool)
    : m_pDiskSourceButtonGroup(0)
    , m_pDiskEmpty(0)
    , m_pDiskNew(0)
    , m_pDiskExisting(0)
    , m_pDiskSelector(0)
    , m_pDiskSelectionButton(0)
    , m_pLabel(0)
    , m_pMediumSizeEditorLabel(0)
    , m_pMediumSizeEditor(0)
    , m_pDescriptionLabel(0)
    , m_pDynamicLabel(0)
    , m_pFixedLabel(0)
    , m_pFixedCheckBox(0)
    , m_fVDIFormatFound(false)
    , m_uMediumSizeMin(_4M)
    , m_uMediumSizeMax(gpGlobalSession->virtualBox().GetSystemProperties().GetInfoVDSize())
    , m_pActionPool(pActionPool)
{
    prepare();
}

void UIWizardNewVMDiskPage::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);

    m_pLabel = new QIRichTextLabel(this);
    pMainLayout->addWidget(m_pLabel);
    pMainLayout->addWidget(createDiskWidgets());

    pMainLayout->addStretch();

    createConnections();
}

QWidget *UIWizardNewVMDiskPage::createNewDiskWidgets()
{
    QWidget *pWidget = new QWidget;
    if (pWidget)
    {
        QVBoxLayout *pLayout = new QVBoxLayout(pWidget);
        if (pLayout)
        {
            pLayout->setContentsMargins(0, 0, 0, 0);

            /* Prepare size layout: */
            QGridLayout *pSizeLayout = new QGridLayout;
            if (pSizeLayout)
            {
                pSizeLayout->setContentsMargins(0, 0, 0, 0);

                /* Prepare Hard disk size label: */
                m_pMediumSizeEditorLabel = new QLabel(pWidget);
                if (m_pMediumSizeEditorLabel)
                {
                    m_pMediumSizeEditorLabel->setAlignment(Qt::AlignRight);
                    m_pMediumSizeEditorLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                    pSizeLayout->addWidget(m_pMediumSizeEditorLabel, 0, 0, Qt::AlignBottom);
                }
                /* Prepare Hard disk size editor: */
                m_pMediumSizeEditor = new UIMediumSizeEditor(pWidget);
                if (m_pMediumSizeEditor)
                {
                    m_pMediumSizeEditorLabel->setBuddy(m_pMediumSizeEditor);
                    pSizeLayout->addWidget(m_pMediumSizeEditor, 0, 1, 2, 1);
                }
                pLayout->addLayout(pSizeLayout);
            }
            /* Hard disk variant (dynamic vs. fixed) widgets: */
            pLayout->addWidget(createMediumVariantWidgets(false /* bool fWithLabels */));
        }
    }
    return pWidget;
}

void UIWizardNewVMDiskPage::createConnections()
{
    if (m_pDiskSourceButtonGroup)
        connect(m_pDiskSourceButtonGroup, &QButtonGroup::buttonClicked,
                this, &UIWizardNewVMDiskPage::sltSelectedDiskSourceChanged);
    if (m_pDiskSelector)
        connect(m_pDiskSelector, &UIMediaComboBox::currentIndexChanged,
                this, &UIWizardNewVMDiskPage::sltMediaComboBoxIndexChanged);
    if (m_pDiskSelectionButton)
        connect(m_pDiskSelectionButton, &QIToolButton::clicked,
                this, &UIWizardNewVMDiskPage::sltGetWithFileOpenDialog);
    if (m_pMediumSizeEditor)
        connect(m_pMediumSizeEditor, &UIMediumSizeEditor::sigSizeChanged,
                this, &UIWizardNewVMDiskPage::sltHandleSizeEditorChange);
    if (m_pFixedCheckBox)
        connect(m_pFixedCheckBox, &QCheckBox::toggled,
                this, &UIWizardNewVMDiskPage::sltFixedCheckBoxToggled);
}

void UIWizardNewVMDiskPage::sltSelectedDiskSourceChanged()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(m_pDiskSelector && m_pDiskSourceButtonGroup && pWizard);
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

void UIWizardNewVMDiskPage::sltMediaComboBoxIndexChanged()
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard && m_pDiskSelector);
    m_userModifiedParameters << "SelectedExistingMediumIndex";
    pWizard->setVirtualDisk(m_pDiskSelector->id());
    pWizard->setMediumPath(m_pDiskSelector->location());
    emit completeChanged();
}

void UIWizardNewVMDiskPage::sltGetWithFileOpenDialog()
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

void UIWizardNewVMDiskPage::sltRetranslateUI()
{
    setTitle(UIWizardNewVM::tr("Virtual Hard disk"));

    if (m_pLabel)
        m_pLabel->setText(UIWizardNewVM::tr("If you wish you can add a virtual hard disk to the new machine. "
                                            "You can either create a new hard disk file or select an existing one. "
                                            "Alternatively you can create a virtual machine without a virtual hard disk."));

    if (m_pDiskEmpty)
        m_pDiskEmpty->setText(UIWizardNewVM::tr("&Do Not Add a Virtual Hard Disk"));
    if (m_pDiskNew)
        m_pDiskNew->setText(UIWizardNewVM::tr("&Create a Virtual Hard Disk Now"));
    if (m_pDiskExisting)
        m_pDiskExisting->setText(UIWizardNewVM::tr("U&se an Existing Virtual Hard Disk File"));
    if (m_pDiskSelectionButton)
        m_pDiskSelectionButton->setToolTip(UIWizardNewVM::tr("Chooses a Virtual Hard Fisk File..."));

    if (m_pMediumSizeEditorLabel)
        m_pMediumSizeEditorLabel->setText(UIWizardNewVM::tr("D&isk Size:"));

    if (m_pFixedCheckBox)
    {
        m_pFixedCheckBox->setText(UIWizardNewVM::tr("Pre-allocate &Full Size"));
        m_pFixedCheckBox->setToolTip(UIWizardNewVM::tr("When checked, the virtual disk image is allocated with its full size during VM creation time"));
    }

    /* Translate rich text labels: */
    if (m_pDescriptionLabel)
        m_pDescriptionLabel->setText(UIWizardNewVM::tr("Please choose whether the new virtual hard disk file should grow as it is used "
                                                       "(dynamically allocated) or if it should be created at its maximum size (fixed size)."));
    if (m_pDynamicLabel)
        m_pDynamicLabel->setText(UIWizardNewVM::tr("<p>A <b>dynamically allocated</b> hard disk file will only use space "
                                                   "on your physical hard disk as it fills up (up to a maximum <b>fixed size</b>), "
                                                   "although it will not shrink again automatically when space on it is freed.</p>"));
    if (m_pFixedLabel)
        m_pFixedLabel->setText(UIWizardNewVM::tr("<p>A <b>fixed size</b> hard disk file may take longer to create on some "
                                                 "systems but is often faster to use.</p>"));
}

void UIWizardNewVMDiskPage::initializePage()
{
    sltRetranslateUI();

    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturnVoid(pWizard);

    LONG64 iRecommendedSize = 0;

    if (!m_userModifiedParameters.contains("SelectedDiskSource"))
    {
        iRecommendedSize = gpGlobalSession->guestOSTypeManager().getRecommendedHDD(pWizard->guestOSTypeId());
        if (iRecommendedSize != 0)
        {
            if (m_pDiskNew)
            {
                m_pDiskNew->setFocus();
                m_pDiskNew->setChecked(true);
            }
            pWizard->setDiskSource(SelectedDiskSource_New);
            pWizard->setEmptyDiskRecommended(false);
        }
        else
        {
            if (m_pDiskEmpty)
            {
                m_pDiskEmpty->setFocus();
                m_pDiskEmpty->setChecked(true);
            }
            pWizard->setDiskSource(SelectedDiskSource_Empty);
            pWizard->setEmptyDiskRecommended(true);
        }
    }

    if (m_pDiskSelector && !m_userModifiedParameters.contains("SelectedExistingMediumIndex"))
        m_pDiskSelector->setCurrentIndex(0);
    setEnableDiskSelectionWidgets(pWizard->diskSource() == SelectedDiskSource_Existing);
    setEnableNewDiskWidgets(pWizard->diskSource() == SelectedDiskSource_New);

    if (!m_fVDIFormatFound)
    {
        /* We do not have any UI elements for HDD format selection since we default to VDI in case of guided wizard mode: */
        CSystemProperties properties = gpGlobalSession->virtualBox().GetSystemProperties();
        const QVector<CMediumFormat> &formats = properties.GetMediumFormats();
        foreach (const CMediumFormat &format, formats)
        {
            if (format.GetName() == "VDI")
            {
                pWizard->setMediumFormat(format);
                m_fVDIFormatFound = true;
            }
        }
        if (!m_fVDIFormatFound)
            AssertMsgFailed(("No medium format corresponding to VDI could be found!"));
        setWidgetVisibility(pWizard->mediumFormat());
    }
    QString strDefaultExtension =  UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), KDeviceType_HardDisk);

    /* We set the medium name and path according to machine name/path and do not allow user change these in the guided mode: */
    QString strDefaultName = pWizard->machineFileName().isEmpty() ? QString("NewVirtualDisk1") : pWizard->machineFileName();
    const QString &strMachineFolder = pWizard->machineFolder();
    QString strMediumPath =
        UIWizardDiskEditors::constructMediumFilePath(UIWizardDiskEditors::appendExtension(strDefaultName,
                                                                                          strDefaultExtension), strMachineFolder);
    pWizard->setMediumPath(strMediumPath);

    /* Set the recommended disk size if user has already not done so: */
    if (m_pMediumSizeEditor && !m_userModifiedParameters.contains("MediumSize"))
    {
        m_pMediumSizeEditor->blockSignals(true);
        m_pMediumSizeEditor->setMediumSize(iRecommendedSize);
        m_pMediumSizeEditor->blockSignals(false);
        pWizard->setMediumSize(iRecommendedSize);
    }

    /* Initialize medium variant parameter of the wizard (only if user has not touched the checkbox yet): */
    if (!m_userModifiedParameters.contains("MediumVariant"))
    {
        if (m_pFixedCheckBox)
        {
            if (m_pFixedCheckBox->isChecked())
                pWizard->setMediumVariant((qulonglong)KMediumVariant_Fixed);
            else
                pWizard->setMediumVariant((qulonglong)KMediumVariant_Standard);
        }
        else
            pWizard->setMediumVariant((qulonglong)KMediumVariant_Standard);
    }
}

bool UIWizardNewVMDiskPage::isComplete() const
{
    UIWizardNewVM *pWizard = wizardWindow<UIWizardNewVM>();
    AssertReturn(pWizard, false);

    const qulonglong uSize = pWizard->mediumSize();
    if (pWizard->diskSource() == SelectedDiskSource_New)
        return uSize >= m_uMediumSizeMin && uSize <= m_uMediumSizeMax;

    if (pWizard->diskSource() == SelectedDiskSource_Existing)
        return !pWizard->virtualDisk().isNull();

    return true;
}

void UIWizardNewVMDiskPage::sltHandleSizeEditorChange(qulonglong uSize)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    wizardWindow<UIWizardNewVM>()->setMediumSize(uSize);
    m_userModifiedParameters << "MediumSize";
    emit completeChanged();
}

void UIWizardNewVMDiskPage::sltFixedCheckBoxToggled(bool fChecked)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVM>());
    qulonglong uMediumVariant = (qulonglong)KMediumVariant_Max;
    if (fChecked)
        uMediumVariant = (qulonglong)KMediumVariant_Fixed;
    else
        uMediumVariant = (qulonglong)KMediumVariant_Standard;
    wizardWindow<UIWizardNewVM>()->setMediumVariant(uMediumVariant);
    m_userModifiedParameters << "MediumVariant";
}

void UIWizardNewVMDiskPage::setEnableNewDiskWidgets(bool fEnable)
{
    if (m_pMediumSizeEditor)
        m_pMediumSizeEditor->setEnabled(fEnable);
    if (m_pMediumSizeEditorLabel)
        m_pMediumSizeEditorLabel->setEnabled(fEnable);
    if (m_pFixedCheckBox)
        m_pFixedCheckBox->setEnabled(fEnable);
}

QWidget *UIWizardNewVMDiskPage::createDiskWidgets()
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
    pDiskLayout->addWidget(m_pDiskNew, 0, 0, 1, 3);
    pDiskLayout->addWidget(createNewDiskWidgets(), 1, 1, 3, 2);
    pDiskLayout->addWidget(m_pDiskExisting, 4, 0, 1, 3);
    pDiskLayout->addWidget(m_pDiskSelector, 5, 1);
    pDiskLayout->addWidget(m_pDiskSelectionButton, 5, 2);
    pDiskLayout->addWidget(m_pDiskEmpty, 6, 0, 1, 3);
    return pDiskContainer;
}

QWidget *UIWizardNewVMDiskPage::createMediumVariantWidgets(bool fWithLabels)
{
    QWidget *pContainerWidget = new QWidget;
    QVBoxLayout *pMainLayout = new QVBoxLayout(pContainerWidget);
    if (pMainLayout)
    {
        QVBoxLayout *pVariantLayout = new QVBoxLayout;
        if (pVariantLayout)
        {
            m_pFixedCheckBox = new QCheckBox;
            pVariantLayout->addWidget(m_pFixedCheckBox);
        }
        if (fWithLabels)
        {
            m_pDescriptionLabel = new QIRichTextLabel(pContainerWidget);
            m_pDynamicLabel = new QIRichTextLabel(pContainerWidget);
            m_pFixedLabel = new QIRichTextLabel(pContainerWidget);

            pMainLayout->addWidget(m_pDescriptionLabel);
            pMainLayout->addWidget(m_pDynamicLabel);
            pMainLayout->addWidget(m_pFixedLabel);
        }
        pMainLayout->addLayout(pVariantLayout);
        pMainLayout->addStretch();
        pMainLayout->setContentsMargins(0, 0, 0, 0);
    }
    return pContainerWidget;
}

void UIWizardNewVMDiskPage::setEnableDiskSelectionWidgets(bool fEnabled)
{
    if (!m_pDiskSelector || !m_pDiskSelectionButton)
        return;

    m_pDiskSelector->setEnabled(fEnabled);
    m_pDiskSelectionButton->setEnabled(fEnabled);
}

void UIWizardNewVMDiskPage::setWidgetVisibility(const CMediumFormat &mediumFormat)
{
    ULONG uCapabilities = 0;
    QVector<KMediumFormatCapabilities> capabilities;
    capabilities = mediumFormat.GetCapabilities();
    for (int i = 0; i < capabilities.size(); i++)
        uCapabilities |= capabilities[i];

    bool fIsCreateDynamicPossible = uCapabilities & KMediumFormatCapabilities_CreateDynamic;
    bool fIsCreateFixedPossible = uCapabilities & KMediumFormatCapabilities_CreateFixed;
    if (m_pFixedCheckBox)
    {
        if (!fIsCreateDynamicPossible)
        {
            m_pFixedCheckBox->setChecked(true);
            m_pFixedCheckBox->setEnabled(false);
        }
        if (!fIsCreateFixedPossible)
        {
            m_pFixedCheckBox->setChecked(false);
            m_pFixedCheckBox->setEnabled(false);
        }
    }
    if (m_pDynamicLabel)
        m_pDynamicLabel->setHidden(!fIsCreateDynamicPossible);
    if (m_pFixedLabel)
        m_pFixedLabel->setHidden(!fIsCreateFixedPossible);
    if (m_pFixedCheckBox)
        m_pFixedCheckBox->setHidden(!fIsCreateFixedPossible);
}
