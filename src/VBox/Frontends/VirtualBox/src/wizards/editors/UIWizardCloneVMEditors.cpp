/* $Id: UIWizardCloneVMEditors.cpp 110419 2025-07-27 09:13:08Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUserNamePasswordEditor class implementation.
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
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QRadioButton>
#include <QGridLayout>

/* GUI includes: */
#include "QILineEdit.h"
#include "UIFilePathSelector.h"
#include "UIGlobalSession.h"
#include "UITranslationEventListener.h"
#include "UIWizardCloneVM.h"
#include "UIWizardCloneVMEditors.h"

/* Other VBox includes: */
#include "iprt/assert.h"
#include "CSystemProperties.h"


/*********************************************************************************************************************************
*   UICloneVMNamePathEditor implementation.                                                                                      *
*********************************************************************************************************************************/

UICloneVMNamePathEditor::UICloneVMNamePathEditor(const QString &strOriginalName, const QString &strDefaultPath, QWidget *pParent /* = 0 */)
    : QGroupBox(pParent)
    , m_pContainerLayout(0)
    , m_pNameLineEdit(0)
    , m_pPathSelector(0)
    , m_pNameLabel(0)
    , m_pPathLabel(0)
    , m_strOriginalName(strOriginalName)
    , m_strDefaultPath(strDefaultPath)
{
    prepare();
}

bool UICloneVMNamePathEditor::isComplete(const QString &strMachineGroup)
{
    AssertReturn(m_pNameLineEdit && m_pPathSelector, false);

    bool fInvalidName = m_pNameLineEdit->text().isEmpty();
    m_pNameLineEdit->mark(fInvalidName, UIWizardCloneVM::tr("Clone name cannot be empty"), UIWizardCloneVM::tr("Clone name is valid"));

    const QString &strPath = m_pPathSelector->path();
    QDir dir(strPath);
    bool fInvalidPath = strPath.isEmpty() || !dir.exists() || !dir.isReadable();
    m_pPathSelector->mark(fInvalidPath, UIWizardCloneVM::tr("Path is invalid"), UIWizardCloneVM::tr("Path is valid"));

    /* Check if there is already a machine folder for this name and path: */
    bool fExists = false;
    if (!fInvalidName)
    {
        CVirtualBox vbox = gpGlobalSession->virtualBox();
        QString strCloneFilePath =
            vbox.ComposeMachineFilename(m_pNameLineEdit->text(), strMachineGroup, QString(), m_pPathSelector->path());
        fExists = QDir(QDir::toNativeSeparators(QFileInfo(strCloneFilePath).absolutePath())).exists();
        m_pNameLineEdit->mark(fExists, UIWizardCloneVM::tr("Clone name is not unique"), UIWizardCloneVM::tr("Clone name is valid"));
    }

    return !fInvalidName && !fInvalidPath && !fExists;
}

QString UICloneVMNamePathEditor::cloneName() const
{
    if (m_pNameLineEdit)
        return m_pNameLineEdit->text();
    return QString();
}

void UICloneVMNamePathEditor::setCloneName(const QString &strName)
{
    if (m_pNameLineEdit)
        m_pNameLineEdit->setText(strName);
}

QString UICloneVMNamePathEditor::clonePath() const
{
    if (m_pPathSelector)
        return m_pPathSelector->path();
    return QString();
}

void UICloneVMNamePathEditor::setClonePath(const QString &strPath)
{
    if (m_pPathSelector)
        m_pPathSelector->setPath(strPath);
}

void UICloneVMNamePathEditor::setFirstColumnWidth(int iWidth)
{
    if (m_pContainerLayout)
        m_pContainerLayout->setColumnMinimumWidth(0, iWidth);
}

int UICloneVMNamePathEditor::firstColumnWidth() const
{
    int iMaxWidth = 0;
    if (m_pNameLabel)
        iMaxWidth = qMax(iMaxWidth, m_pNameLabel->minimumSizeHint().width());
    if (m_pPathLabel)
        iMaxWidth = qMax(iMaxWidth, m_pPathLabel->minimumSizeHint().width());
    return iMaxWidth;
}

void UICloneVMNamePathEditor::setLayoutContentsMargins(int iLeft, int iTop, int iRight, int iBottom)
{
    if (m_pContainerLayout)
        m_pContainerLayout->setContentsMargins(iLeft, iTop, iRight, iBottom);
}

void UICloneVMNamePathEditor::prepare()
{
    m_pContainerLayout = new QGridLayout(this);


    m_pNameLabel = new QLabel;
    if (m_pNameLabel)
    {
        m_pNameLabel->setAlignment(Qt::AlignRight);
        m_pNameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_pContainerLayout->addWidget(m_pNameLabel, 0, 0, 1, 1);
    }

    m_pNameLineEdit = new QILineEdit();
    if (m_pNameLineEdit)
    {
        m_pNameLineEdit->setMarkable(true);
        m_pContainerLayout->addWidget(m_pNameLineEdit, 0, 1, 1, 1);
        m_pNameLineEdit->setText(UIWizardCloneVM::tr("%1 Clone").arg(m_strOriginalName));
        connect(m_pNameLineEdit, &QILineEdit::textChanged,
                this, &UICloneVMNamePathEditor::sigCloneNameChanged);
        if (m_pNameLabel)
            m_pNameLabel->setBuddy(m_pNameLineEdit);
    }

    m_pPathLabel = new QLabel(this);
    if (m_pPathLabel)
    {
        m_pPathLabel->setAlignment(Qt::AlignRight);
        m_pPathLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_pContainerLayout->addWidget(m_pPathLabel, 1, 0, 1, 1);
    }

    m_pPathSelector = new UIFilePathSelector(this);
    if (m_pPathSelector)
    {
        m_pContainerLayout->addWidget(m_pPathSelector, 1, 1, 1, 1);
        m_pPathSelector->setPath(m_strDefaultPath);
        connect(m_pPathSelector, &UIFilePathSelector::pathChanged,
                this, &UICloneVMNamePathEditor::sigClonePathChanged);
    if (m_pPathLabel)
        m_pPathLabel->setBuddy(m_pPathSelector);

    }

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloneVMNamePathEditor::sltRetranslateUI);
}

void UICloneVMNamePathEditor::sltRetranslateUI()
{
    if (m_pNameLabel)
        m_pNameLabel->setText(UIWizardCloneVM::tr("&Name:"));
    if (m_pPathLabel)
        m_pPathLabel->setText(UIWizardCloneVM::tr("&Path:"));
    if (m_pNameLineEdit)
        m_pNameLineEdit->setToolTip(UIWizardCloneVM::tr("Holds a name for the new virtual machine."));
    if (m_pPathSelector)
        m_pPathSelector->setToolTip(UIWizardCloneVM::tr("Specifies The location of the new virtual machine in host's storage."));
}


/*********************************************************************************************************************************
*   UICloneVMAdditionalOptionsEditor implementation.                                                                             *
*********************************************************************************************************************************/


UICloneVMAdditionalOptionsEditor::UICloneVMAdditionalOptionsEditor(QWidget *pParent /* = 0 */)
    : QGroupBox(pParent)
    , m_pContainerLayout(0)
    , m_pMACComboBoxLabel(0)
    , m_pMACComboBox(0)
    , m_pAdditionalOptionsLabel(0)
    , m_pKeepDiskNamesCheckBox(0)
    , m_pKeepHWUUIDsCheckBox(0)
{
    prepare();
}

void UICloneVMAdditionalOptionsEditor::setLayoutContentsMargins(int iLeft, int iTop, int iRight, int iBottom)
{
    if (m_pContainerLayout)
        m_pContainerLayout->setContentsMargins(iLeft, iTop, iRight, iBottom);
}

void UICloneVMAdditionalOptionsEditor::setFirstColumnWidth(int iWidth)
{
    if (m_pContainerLayout)
        m_pContainerLayout->setColumnMinimumWidth(0, iWidth);
}

int UICloneVMAdditionalOptionsEditor::firstColumnWidth() const
{
    int iMaxWidth = 0;
    if (m_pMACComboBoxLabel)
        iMaxWidth = qMax(iMaxWidth, m_pMACComboBoxLabel->minimumSizeHint().width());
    if (m_pAdditionalOptionsLabel)
        iMaxWidth = qMax(iMaxWidth, m_pAdditionalOptionsLabel->minimumSizeHint().width());
    return iMaxWidth;
}

MACAddressClonePolicy UICloneVMAdditionalOptionsEditor::macAddressClonePolicy() const
{
    return m_pMACComboBox->currentData().value<MACAddressClonePolicy>();
}

void UICloneVMAdditionalOptionsEditor::setMACAddressClonePolicy(MACAddressClonePolicy enmMACAddressClonePolicy)
{
    const int iIndex = m_pMACComboBox->findData(QVariant::fromValue(enmMACAddressClonePolicy));
    AssertMsg(iIndex != -1, ("Data not found!"));
    m_pMACComboBox->setCurrentIndex(iIndex);
}

bool UICloneVMAdditionalOptionsEditor::keepHardwareUUIDs() const
{
    if (m_pKeepHWUUIDsCheckBox)
        return m_pKeepHWUUIDsCheckBox->isChecked();
    return false;
}

bool UICloneVMAdditionalOptionsEditor::keepDiskNames() const
{
    if (m_pKeepDiskNamesCheckBox)
        m_pKeepDiskNamesCheckBox->isChecked();
    return false;
}

void UICloneVMAdditionalOptionsEditor::prepare()
{
    m_pContainerLayout = new QGridLayout(this);

    m_pMACComboBoxLabel = new QLabel;
    if (m_pMACComboBoxLabel)
    {
        m_pMACComboBoxLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
        m_pContainerLayout->addWidget(m_pMACComboBoxLabel, 2, 0, 1, 1);
    }

    m_pMACComboBox = new QComboBox;
    if (m_pMACComboBox)
    {
        m_pContainerLayout->addWidget(m_pMACComboBox, 2, 1, 1, 1);
        connect(m_pMACComboBox, &QComboBox::currentIndexChanged,
                this, &UICloneVMAdditionalOptionsEditor::sltMACAddressClonePolicyChanged);
        if (m_pMACComboBoxLabel)
            m_pMACComboBoxLabel->setBuddy(m_pMACComboBox);
    }
    m_pMACComboBox->blockSignals(true);
    populateMACAddressClonePolicies();
    m_pMACComboBox->blockSignals(false);

    /* Load currently supported clone options: */
    CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    const QVector<KCloneOptions> supportedOptions = comProperties.GetSupportedCloneOptions();
    /* Check whether we support additional clone options at all: */
    int iVerticalPosition = 3;
    const bool fSupportedKeepDiskNames = supportedOptions.contains(KCloneOptions_KeepDiskNames);
    const bool fSupportedKeepHWUUIDs = supportedOptions.contains(KCloneOptions_KeepHwUUIDs);
    if (fSupportedKeepDiskNames || fSupportedKeepHWUUIDs)
    {
        m_pAdditionalOptionsLabel = new QLabel;
        if (m_pAdditionalOptionsLabel)
        {
            m_pAdditionalOptionsLabel->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
            m_pContainerLayout->addWidget(m_pAdditionalOptionsLabel, iVerticalPosition, 0, 1, 1);
        }
    }
    if (fSupportedKeepDiskNames)
    {
        m_pKeepDiskNamesCheckBox = new QCheckBox;
        if (m_pKeepDiskNamesCheckBox)
        {
            m_pContainerLayout->addWidget(m_pKeepDiskNamesCheckBox, iVerticalPosition++, 1, 1, 1);
            connect(m_pKeepDiskNamesCheckBox, &QCheckBox::toggled,
                    this, &UICloneVMAdditionalOptionsEditor::sigKeepDiskNamesToggled);
        }
    }
    if (fSupportedKeepHWUUIDs)
    {
        m_pKeepHWUUIDsCheckBox = new QCheckBox;
        if (m_pKeepHWUUIDsCheckBox)
        {
            m_pContainerLayout->addWidget(m_pKeepHWUUIDsCheckBox, iVerticalPosition++, 1, 1, 1);
            connect(m_pKeepHWUUIDsCheckBox, &QCheckBox::toggled,
                    this, &UICloneVMAdditionalOptionsEditor::sigKeepHardwareUUIDsToggled);
        }
    }

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloneVMAdditionalOptionsEditor::sltRetranslateUI);
}

void UICloneVMAdditionalOptionsEditor::sltRetranslateUI()
{
    m_pMACComboBoxLabel->setText(UIWizardCloneVM::tr("MAC Address P&olicy:"));
    m_pMACComboBox->setToolTip(UIWizardCloneVM::tr("Determines MAC address policy for cloning."));
    for (int i = 0; i < m_pMACComboBox->count(); ++i)
    {
        const MACAddressClonePolicy enmPolicy = m_pMACComboBox->itemData(i).value<MACAddressClonePolicy>();
        switch (enmPolicy)
        {
            case MACAddressClonePolicy_KeepAllMACs:
                {
                    m_pMACComboBox->setItemText(i, UIWizardCloneVM::tr("Include all network adapter MAC addresses"));
                    m_pMACComboBox->setItemData(i, UIWizardCloneVM::tr("Include all network adapter MAC addresses during "
                                                                       "cloning."), Qt::ToolTipRole);
                    break;
                }
            case MACAddressClonePolicy_KeepNATMACs:
                {
                    m_pMACComboBox->setItemText(i, UIWizardCloneVM::tr("Include only NAT network adapter MAC addresses"));
                    m_pMACComboBox->setItemData(i, UIWizardCloneVM::tr("Include only NAT network adapter MAC addresses during "
                                                                       "cloning."), Qt::ToolTipRole);
                    break;
                }
            case MACAddressClonePolicy_StripAllMACs:
                {
                    m_pMACComboBox->setItemText(i, UIWizardCloneVM::tr("Generate new MAC addresses for all network adapters"));
                    m_pMACComboBox->setItemData(i, UIWizardCloneVM::tr("Generate new MAC addresses for all network adapters during "
                                                                       "cloning."), Qt::ToolTipRole);
                    break;
                }
            default:
                break;
        }
    }

    if (m_pAdditionalOptionsLabel)
        m_pAdditionalOptionsLabel->setText(UIWizardCloneVM::tr("OS Installation Options:"));
    if (m_pKeepDiskNamesCheckBox)
    {
        m_pKeepDiskNamesCheckBox->setToolTip(UIWizardCloneVM::tr("When checked, disk names will be preserved during cloning."));
        m_pKeepDiskNamesCheckBox->setText(UIWizardCloneVM::tr("Keep &Disk Names"));
    }
    if (m_pKeepHWUUIDsCheckBox)
    {
        m_pKeepHWUUIDsCheckBox->setToolTip(UIWizardCloneVM::tr("When checked, hardware UUIDs will be preserved during cloning."));
        m_pKeepHWUUIDsCheckBox->setText(UIWizardCloneVM::tr("Keep Hard&ware UUIDs"));
    }
}

void UICloneVMAdditionalOptionsEditor::sltMACAddressClonePolicyChanged()
{
    emit sigMACAddressClonePolicyChanged(macAddressClonePolicy());
    updateMACAddressClonePolicyComboToolTip();
}

void UICloneVMAdditionalOptionsEditor::updateMACAddressClonePolicyComboToolTip()
{
    if (!m_pMACComboBox)
        return;
    const QString strCurrentToolTip = m_pMACComboBox->currentData(Qt::ToolTipRole).toString();
    AssertMsg(!strCurrentToolTip.isEmpty(), ("Tool-tip data not found!"));
    m_pMACComboBox->setToolTip(strCurrentToolTip);
}

void UICloneVMAdditionalOptionsEditor::populateMACAddressClonePolicies()
{
    AssertReturnVoid(m_pMACComboBox && m_pMACComboBox->count() == 0);

    /* Map known clone options to known MAC address export policies: */
    QMap<KCloneOptions, MACAddressClonePolicy> knownOptions;
    knownOptions[KCloneOptions_KeepAllMACs] = MACAddressClonePolicy_KeepAllMACs;
    knownOptions[KCloneOptions_KeepNATMACs] = MACAddressClonePolicy_KeepNATMACs;

    /* Load currently supported clone options: */
    CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    const QVector<KCloneOptions> supportedOptions = comProperties.GetSupportedCloneOptions();

    /* Check which of supported options/policies are known: */
    QList<MACAddressClonePolicy> supportedPolicies;
    foreach (const KCloneOptions &enmOption, supportedOptions)
        if (knownOptions.contains(enmOption))
            supportedPolicies << knownOptions.value(enmOption);

    /* Add supported policies first: */
    foreach (const MACAddressClonePolicy &enmPolicy, supportedPolicies)
        m_pMACComboBox->addItem(QString(), QVariant::fromValue(enmPolicy));

    /* Add hardcoded policy finally: */
    m_pMACComboBox->addItem(QString(), QVariant::fromValue(MACAddressClonePolicy_StripAllMACs));

    /* Set default: */
    if (supportedPolicies.contains(MACAddressClonePolicy_KeepNATMACs))
        setMACAddressClonePolicy(MACAddressClonePolicy_KeepNATMACs);
    else
        setMACAddressClonePolicy(MACAddressClonePolicy_StripAllMACs);
}


/*********************************************************************************************************************************
*   UICloneVMAdditionalOptionsEditor implementation.                                                                             *
*********************************************************************************************************************************/

UICloneVMCloneTypeGroupBox::UICloneVMCloneTypeGroupBox(QWidget *pParent /* = 0 */)
    : QGroupBox(pParent)
    , m_pButtonGroup(0)
    , m_pFullCloneRadio(0)
    , m_pLinkedCloneRadio(0)
{
    prepare();
}

bool UICloneVMCloneTypeGroupBox::isFullClone() const
{
    if (m_pFullCloneRadio)
        return m_pFullCloneRadio->isChecked();
    return true;
}

void UICloneVMCloneTypeGroupBox::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(pMainLayout);
    /* Prepare clone-type options button-group: */
    m_pButtonGroup = new QButtonGroup(this);
    if (m_pButtonGroup)
    {
        /* Prepare full clone option radio-button: */
        m_pFullCloneRadio = new QRadioButton(this);
        if (m_pFullCloneRadio)
        {
            m_pFullCloneRadio->setChecked(true);
            m_pButtonGroup->addButton(m_pFullCloneRadio);
            pMainLayout->addWidget(m_pFullCloneRadio);
        }

        /* Load currently supported clone options: */
        CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
        const QVector<KCloneOptions> supportedOptions = comProperties.GetSupportedCloneOptions();
        /* Check whether we support linked clone option at all: */
        const bool fSupportedLinkedClone = supportedOptions.contains(KCloneOptions_Link);

        /* Prepare linked clone option radio-button: */
        if (fSupportedLinkedClone)
        {
            m_pLinkedCloneRadio = new QRadioButton(this);
            if (m_pLinkedCloneRadio)
            {
                m_pButtonGroup->addButton(m_pLinkedCloneRadio);
                pMainLayout->addWidget(m_pLinkedCloneRadio);
            }
        }
    }

    connect(m_pButtonGroup, &QButtonGroup::buttonClicked,
            this, &UICloneVMCloneTypeGroupBox::sltButtonClicked);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloneVMCloneTypeGroupBox::sltRetranslateUI);
}

void UICloneVMCloneTypeGroupBox::sltRetranslateUI()
{
    if (m_pFullCloneRadio)
    {
        m_pFullCloneRadio->setText(UIWizardCloneVM::tr("&Full Clone"));
        m_pFullCloneRadio->setToolTip(UIWizardCloneVM::tr("When chosen, all the virtual disks of the source vm are also cloned."));
    }
    if (m_pLinkedCloneRadio)
    {
        m_pLinkedCloneRadio->setText(UIWizardCloneVM::tr("&Linked Clone"));
        m_pLinkedCloneRadio->setToolTip(UIWizardCloneVM::tr("When chosen, the cloned vm will save space by sharing the source VM's disk images."));
    }
}

void UICloneVMCloneTypeGroupBox::sltButtonClicked(QAbstractButton *)
{
    emit sigFullCloneSelected(m_pFullCloneRadio && m_pFullCloneRadio->isChecked());
}


/*********************************************************************************************************************************
*   UICloneVMAdditionalOptionsEditor implementation.                                                                             *
*********************************************************************************************************************************/

UICloneVMCloneModeGroupBox::UICloneVMCloneModeGroupBox(bool fShowChildsOption, QWidget *pParent /* = 0 */)
    : QGroupBox(pParent)
    , m_fShowChildsOption(fShowChildsOption)
    , m_pMachineRadio(0)
    , m_pMachineAndChildsRadio(0)
    , m_pAllRadio(0)
{
    prepare();
}

void UICloneVMCloneModeGroupBox::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(pMainLayout);

    QButtonGroup *pButtonGroup = new QButtonGroup(this);
    m_pMachineRadio = new QRadioButton(this);
    if (m_pMachineRadio)
    {
        m_pMachineRadio->setChecked(true);
        pButtonGroup->addButton(m_pMachineRadio);
    }
    m_pMachineAndChildsRadio = new QRadioButton(this);
    if (m_pMachineAndChildsRadio)
    {
        if (!m_fShowChildsOption)
            m_pMachineAndChildsRadio->hide();
        pButtonGroup->addButton(m_pMachineAndChildsRadio);
    }

    m_pAllRadio = new QRadioButton(this);
    if (m_pAllRadio)
        pButtonGroup->addButton(m_pAllRadio);

    pMainLayout->addWidget(m_pMachineRadio);
    pMainLayout->addWidget(m_pMachineAndChildsRadio);
    pMainLayout->addWidget(m_pAllRadio);
    pMainLayout->addStretch();

    connect(pButtonGroup, &QButtonGroup::buttonClicked,
            this, &UICloneVMCloneModeGroupBox::sltButtonClicked);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UICloneVMCloneModeGroupBox::sltRetranslateUI);
}

void UICloneVMCloneModeGroupBox::sltRetranslateUI()
{
    if (m_pMachineRadio)
    {
        m_pMachineRadio->setText(UIWizardCloneVM::tr("Current &Machine State"));
        m_pMachineRadio->setToolTip(UIWizardCloneVM::tr("When chosen, only the current state of the source vm is cloned."));
    }
    if (m_pMachineAndChildsRadio)
        m_pMachineAndChildsRadio->setText(UIWizardCloneVM::tr("Current &Snapshot Tree Branch"));
    if (m_pAllRadio)
    {
        m_pAllRadio->setText(UIWizardCloneVM::tr("&Everything"));
        m_pAllRadio->setToolTip(UIWizardCloneVM::tr("When chosen, all the saved states of the source vm are also cloned."));
    }
}


void UICloneVMCloneModeGroupBox::sltButtonClicked()
{
    emit sigCloneModeChanged(cloneMode());
}

KCloneMode UICloneVMCloneModeGroupBox::cloneMode() const
{
    KCloneMode enmCloneMode = KCloneMode_MachineState;
    if (m_pMachineAndChildsRadio && m_pMachineAndChildsRadio->isChecked())
        enmCloneMode =  KCloneMode_MachineAndChildStates;
    else if (m_pAllRadio && m_pAllRadio->isChecked())
        enmCloneMode = KCloneMode_AllStates;
    return enmCloneMode;
}
