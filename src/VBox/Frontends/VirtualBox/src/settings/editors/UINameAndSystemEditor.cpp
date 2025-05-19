/* $Id: UINameAndSystemEditor.cpp 168930 2025-05-19 15:06:14Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UINameAndSystemEditor class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

/* GUI includes: */
#include "QILineEdit.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIFilePathSelector.h"
#include "UIMediumTools.h"
#include "UINameAndSystemEditor.h"

/* COM includes: */
#include "CSystemProperties.h"
#include <VBox/com/VirtualBox.h> /* Need GUEST_OS_ID_STR_X86 and friends. */


UINameAndSystemEditor::UINameAndSystemEditor(QWidget *pParent,
                                             bool fChooseName /* = true */,
                                             bool fChoosePath /* = false */,
                                             bool fChooseImage /* = false */,
                                             bool fChooseEdition /* = false */,
                                             bool fChooseType /* = true */)
    : UIEditor(pParent, true /* show in basic mode? */)
    // options
    , m_fChooseName(fChooseName)
    , m_fChoosePath(fChoosePath)
    , m_fChooseImage(fChooseImage)
    , m_fChooseEdition(fChooseEdition)
    , m_fChooseType(fChooseType)
    // widgets
    , m_pLayout(0)
    // widgets: name
    , m_pLabelName(0)
    , m_pEditorName(0)
    // widgets: path
    , m_pLabelPath(0)
    , m_pSelectorPath(0)
    // widgets: image
    , m_pLabelImage(0)
    , m_pSelectorImage(0)
    // widgets: edition
    , m_pLabelEdition(0)
    , m_pComboEdition(0)
    // widgets/ family, distribution, type
    , m_pLabelFamily(0)
    , m_pComboFamily(0)
    , m_pLabelDistribution(0)
    , m_pComboDistribution(0)
    , m_pLabelType(0)
    , m_pComboType(0)
    , m_pIconType(0)
{
    prepare();
}

void UINameAndSystemEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UINameAndSystemEditor::setNameStuffEnabled(bool fEnabled)
{
    if (m_pLabelName)
        m_pLabelName->setEnabled(fEnabled);
    if (m_pEditorName)
        m_pEditorName->setEnabled(fEnabled);
}

void UINameAndSystemEditor::setPathStuffEnabled(bool fEnabled)
{
    if (m_pLabelPath)
        m_pLabelPath->setEnabled(fEnabled);
    if (m_pSelectorPath)
        m_pSelectorPath->setEnabled(fEnabled);
}

void UINameAndSystemEditor::setOSTypeStuffEnabled(bool fEnabled)
{
    if (m_pLabelFamily)
        m_pLabelFamily->setEnabled(fEnabled);
    if (m_pLabelDistribution)
        setEnabledByReason(m_pLabelDistribution, 1, fEnabled);
    if (m_pLabelType)
        m_pLabelType->setEnabled(fEnabled);
    if (m_pComboFamily)
        m_pComboFamily->setEnabled(fEnabled);
    if (m_pComboDistribution)
        setEnabledByReason(m_pComboDistribution, 1, fEnabled);
    if (m_pComboType)
        m_pComboType->setEnabled(fEnabled);
    if (m_pIconType)
        m_pIconType->setEnabled(fEnabled);
}

void UINameAndSystemEditor::setEditionSelectorEnabled(bool fEnabled)
{
    if (m_pLabelEdition)
        m_pLabelEdition->setEnabled(fEnabled);
    if (m_pComboEdition)
        m_pComboEdition->setEnabled(fEnabled);
}

bool UINameAndSystemEditor::isEditionsSelectorEmpty() const
{
    if (m_pComboEdition)
        return m_pComboEdition->count() == 0;
    return true;
}

void UINameAndSystemEditor::setName(const QString &strName)
{
    if (!m_pEditorName)
        return;
    m_pEditorName->setText(strName);
}

QString UINameAndSystemEditor::name() const
{
    if (!m_pEditorName)
        return QString();
    return m_pEditorName->text();
}

void UINameAndSystemEditor::setPath(const QString &strPath)
{
    if (!m_pSelectorPath)
        return;
    m_pSelectorPath->setPath(strPath);
}

QString UINameAndSystemEditor::path() const
{
    if (!m_pSelectorPath)
        return gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();
    return m_pSelectorPath->path();
}

QString UINameAndSystemEditor::fullPath() const
{
    QDir dir(path());
    QString strFullPath = dir.filePath(name());
    return QDir::cleanPath(strFullPath);
}

void UINameAndSystemEditor::setISOImagePath(const QString &strPath)
{
    if (m_pSelectorImage)
        m_pSelectorImage->setPath(strPath);
    emit sigImageChanged(strPath);
}

QString UINameAndSystemEditor::ISOImagePath() const
{
    if (!m_pSelectorImage)
        return QString();
    return m_pSelectorImage->path();
}

bool UINameAndSystemEditor::setGuestOSTypeByTypeId(const QString &strTypeId)
{
    /* Cache passed values locally, they will be required for the final result check: */
    const QString strFamilyId = gpGlobalSession->guestOSTypeManager().getFamilyId(strTypeId);
    const QString strDistribution = gpGlobalSession->guestOSTypeManager().getSubtype(strTypeId);

    /* Save passed values, but they can be overridden
     * in the below populateFamilyCombo() call: */
    m_strFamilyId = strFamilyId;
    if (!strDistribution.isEmpty())
        m_familyToDistribution[familyId()] = strDistribution;
    if (distribution().isEmpty())
        m_familyToType[familyId()] = strTypeId;
    else
        m_distributionToType[distribution()] = strTypeId;

    /* Repopulate VM OS family/distribution/type combo(s): */
    populateFamilyCombo();

    /* Family check: */
    AssertPtrReturn(m_pComboFamily, false);
    if (m_pComboFamily->currentData().toString() != strFamilyId)
        return false;
    /* Distribution check: */
    AssertPtrReturn(m_pComboDistribution, false);
    if (m_pComboDistribution->currentText() != strDistribution)
        return false;
    /* Type check: */
    AssertPtrReturn(m_pComboType, false);
    if (m_pComboType->currentData().toString() != strTypeId)
        return false;

    /* Success by default: */
    return true;
}

QString UINameAndSystemEditor::familyId() const
{
    return m_strFamilyId;
}

QString UINameAndSystemEditor::distribution() const
{
    return m_familyToDistribution.value(familyId());
}

QString UINameAndSystemEditor::typeId() const
{
    return   !m_familyToDistribution.contains(familyId())
           ? m_familyToType.value(familyId())
           : m_distributionToType.value(distribution());
}

void UINameAndSystemEditor::markNameEditor(bool fError, const QString &strErrorMessage, const QString &strNoErrorMessage)
{
    if (m_pEditorName)
        m_pEditorName->mark(fError, strErrorMessage, strNoErrorMessage);
}

void UINameAndSystemEditor::markImageEditor(bool fError, const QString &strErrorMessage, const QString &strNoErrorMessage)
{
    if (m_pSelectorImage)
        m_pSelectorImage->mark(fError, strErrorMessage, strNoErrorMessage);
}

void UINameAndSystemEditor::setEditionNameAndIndices(const QVector<QString> &names, const QVector<ulong> &ids)
{
    AssertReturnVoid(m_pComboEdition && names.size() == ids.size());
    m_pComboEdition->clear();
    for (int i = 0; i < names.size(); ++i)
        m_pComboEdition->addItem(names[i], QVariant::fromValue(ids[i]) /* user data */);
}

int UINameAndSystemEditor::firstColumnWidth() const
{
    int iWidth = 0;
    if (m_pLabelName)
        iWidth = qMax(iWidth, m_pLabelName->width());
    if (m_pLabelPath)
        iWidth = qMax(iWidth, m_pLabelPath->width());
    if (m_pLabelImage)
        iWidth = qMax(iWidth, m_pLabelImage->width());
    if (m_pLabelEdition)
        iWidth = qMax(iWidth, m_pLabelEdition->width());
    if (m_pLabelFamily)
        iWidth = qMax(iWidth, m_pLabelFamily->width());
    if (m_pLabelDistribution)
        iWidth = qMax(iWidth, m_pLabelDistribution->width());
    if (m_pLabelType)
        iWidth = qMax(iWidth, m_pLabelType->width());
    return iWidth;
}

void UINameAndSystemEditor::sltRetranslateUI()
{
    if (m_pLabelName)
        m_pLabelName->setText(tr("VM &Name"));
    if (m_pLabelPath)
        m_pLabelPath->setText(tr("VM &Folder"));
    if (m_pLabelImage)
        m_pLabelImage->setText(tr("&ISO Image"));
    if (m_pLabelEdition)
        m_pLabelEdition->setText(tr("OS &Edition"));
    if (m_pLabelFamily)
        m_pLabelFamily->setText(tr("&OS"));
    if (m_pLabelDistribution)
        m_pLabelDistribution->setText(tr("OS &Distribution"));
    if (m_pLabelType)
        m_pLabelType->setText(tr("OS &Version"));

    if (m_pEditorName)
        m_pEditorName->setToolTip(tr("Descriptive name to uniquely identify the virtual machine"));
    if (m_pSelectorPath)
        m_pSelectorPath->setToolTip(tr("Storage location for virtual machine files"));
    if (m_pComboEdition)
        m_pComboEdition->setToolTip(tr("Edition of guest operating system to install"));

    if (m_pComboFamily)
        m_pComboFamily->setToolTip(tr("Operating system, for example Linux, Windows"));
    if (m_pComboDistribution)
        m_pComboDistribution->setToolTip(tr("OS distribution, if applicable"));
    if (m_pComboType)
        m_pComboType->setToolTip(tr("Version of OS"));

    if (m_pSelectorImage)
        m_pSelectorImage->setToolTip(tr("ISO image to be attached to the "
                                        "virtual machine and possibly used in unattended install"));
}

void UINameAndSystemEditor::handleFilterChange()
{
    populateFamilyCombo();
}

void UINameAndSystemEditor::sltSelectedEditionsChanged(int)
{
    emit sigEditionChanged(selectedEditionIndex());
}

void UINameAndSystemEditor::sltFamilyChanged(int iIndex)
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboFamily);

    /* Acquire new family ID: */
    m_strFamilyId = m_pComboFamily->itemData(iIndex).toString();
//    printf("Saving current family as: %s\n",
//           familyId().toUtf8().constData());
    AssertReturnVoid(!familyId().isEmpty());

    /* Notify listeners about this change: */
    emit sigOSFamilyChanged(familyId());

    /* Pupulate distribution combo: */
    populateDistributionCombo();
}

void UINameAndSystemEditor::sltDistributionChanged(const QString &strDistribution)
{
    /* Save the most recently used distribution: */
    if (!strDistribution.isEmpty())
    {
//        printf("Saving current distribution [for family=%s] as: %s\n",
//               familyId().toUtf8().constData(),
//               strDistribution.toUtf8().constData());
        m_familyToDistribution[familyId()] = strDistribution;
    }

    /* Get current arch type, usually we'd default to x86, but here 'None' meaningful as well: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_None;

    /* If distribution list is empty, all the types of the family are added to type combo: */
    const UIGuestOSTypeManager::UIGuestOSTypeInfo types
         = strDistribution.isEmpty()
         ? gpGlobalSession->guestOSTypeManager().getTypesForFamilyId(familyId(),
                                                                     false /* including restricted? */,
                                                                     QStringList() << typeId(),
                                                                     enmArch)
         : gpGlobalSession->guestOSTypeManager().getTypesForSubtype(distribution(),
                                                                    false /* including restricted? */,
                                                                    QStringList() << typeId(),
                                                                    enmArch);

    /* Populate type combo: */
    populateTypeCombo(types);
}

void UINameAndSystemEditor::sltTypeChanged(int iIndex)
{
    /* Acquire new type ID: */
    AssertPtrReturnVoid(m_pComboType);
    const QString strTypeId = m_pComboType->itemData(iIndex).toString();
    AssertReturnVoid(!strTypeId.isEmpty());

    /* Save the most recently used type: */
    if (distribution().isEmpty())
    {
//        printf("Saving current type [for family=%s] as: %s\n",
//               familyId().toUtf8().constData(),
//               strTypeId.toUtf8().constData());
        m_familyToType[familyId()] = strTypeId;
    }
    else
    {
//        printf("Saving current type [for distribution=%s] as: %s\n",
//               distribution().toUtf8().constData(),
//               strTypeId.toUtf8().constData());
        m_distributionToType[distribution()] = strTypeId;
    }
    AssertReturnVoid(!typeId().isEmpty());

    /* Update selected type pixmap: */
    m_pIconType->setPixmap(generalIconPool().guestOSTypePixmapDefault(strTypeId));

    /* Notifies listeners about this change: */
    emit sigOsTypeChanged();
}

void UINameAndSystemEditor::prepare()
{
    prepareWidgets();
    prepareConnections();
    sltRetranslateUI();
}

void UINameAndSystemEditor::prepareWidgets()
{
    /* Prepare main-layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(0, 0);
        m_pLayout->setColumnStretch(1, 1);

        int iRow = 0;

        if (m_fChooseName)
        {
            /* Prepare name label: */
            m_pLabelName = new QLabel(this);
            if (m_pLabelName)
            {
                m_pLabelName->setAlignment(Qt::AlignRight);
                m_pLabelName->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                m_pLayout->addWidget(m_pLabelName, iRow, 0);
            }
            /* Prepare name editor: */
            m_pEditorName = new QILineEdit(this);
            if (m_pEditorName)
            {
                m_pLabelName->setBuddy(m_pEditorName);
                m_pEditorName->setMarkable(true);
                m_pLayout->addWidget(m_pEditorName, iRow, 1, 1, 2);
            }
            ++iRow;
        }

        if (m_fChoosePath)
        {
            /* Prepare path label: */
            m_pLabelPath = new QLabel(this);
            if (m_pLabelPath)
            {
                m_pLabelPath->setAlignment(Qt::AlignRight);
                m_pLabelPath->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                m_pLayout->addWidget(m_pLabelPath, iRow, 0);
            }
            /* Prepare path selector: */
            m_pSelectorPath = new UIFilePathSelector(this);
            if (m_pSelectorPath)
            {
                m_pLabelPath->setBuddy(m_pSelectorPath);
                QString strDefaultMachineFolder = gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();
                m_pSelectorPath->setPath(strDefaultMachineFolder);
                m_pSelectorPath->setDefaultPath(strDefaultMachineFolder);
                m_pLayout->addWidget(m_pSelectorPath, iRow, 1, 1, 2);
            }
            ++iRow;
        }

        if (m_fChooseImage)
        {
            /* Prepare image label: */
            m_pLabelImage = new QLabel(this);
            if (m_pLabelImage)
            {
                m_pLabelImage->setAlignment(Qt::AlignRight);
                m_pLabelImage->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                m_pLayout->addWidget(m_pLabelImage, iRow, 0);
            }
            /* Prepare image selector: */
            m_pSelectorImage = new UIFilePathSelector(this);
            if (m_pSelectorImage)
            {
                m_pLabelImage->setBuddy(m_pSelectorImage);
                m_pSelectorImage->setResetEnabled(false);
                m_pSelectorImage->setMode(UIFilePathSelector::Mode_File_Open);
                m_pSelectorImage->setFileDialogFilters("ISO Images(*.iso *.ISO)");
                m_pSelectorImage->setInitialPath(UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_DVD));
                m_pSelectorImage->setRecentMediaListType(UIMediumDeviceType_DVD);
                m_pLayout->addWidget(m_pSelectorImage, iRow, 1, 1, 2);
            }
            ++iRow;
        }

        if (m_fChooseEdition)
        {
            /* Prepare edition label: */
            m_pLabelEdition = new QLabel(this);
            if (m_pLabelEdition)
            {
                m_pLabelEdition->setAlignment(Qt::AlignRight);
                m_pLabelEdition->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                m_pLayout->addWidget(m_pLabelEdition, iRow, 0);
            }
            /* Prepare edition combo: */
            m_pComboEdition = new QComboBox(this);
            if (m_pComboEdition)
            {
                m_pLabelEdition->setBuddy(m_pComboEdition);
                m_pLayout->addWidget(m_pComboEdition, iRow, 1, 1, 2);
            }
            ++iRow;
        }

        if (m_fChooseType)
        {
            const int iIconRow = iRow;

            /* Prepare VM OS family label: */
            m_pLabelFamily = new QLabel(this);
            if (m_pLabelFamily)
            {
                m_pLabelFamily->setAlignment(Qt::AlignRight);
                m_pLabelFamily->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                m_pLayout->addWidget(m_pLabelFamily, iRow, 0);
            }
            /* Prepare VM OS family combo: */
            m_pComboFamily = new QComboBox(this);
            if (m_pComboFamily)
            {
                m_pLabelFamily->setBuddy(m_pComboFamily);
                m_pLayout->addWidget(m_pComboFamily, iRow, 1);
            }
            ++iRow;

            /* Prepare VM OS distribution label: */
            m_pLabelDistribution = new QLabel(this);
            if (m_pLabelDistribution)
            {
                m_pLabelDistribution->setAlignment(Qt::AlignRight);
                m_pLabelDistribution->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                m_pLayout->addWidget(m_pLabelDistribution, iRow, 0);
            }
            /* Prepare VM OS distribution combo: */
            m_pComboDistribution = new QComboBox(this);
            if (m_pComboDistribution)
            {
                m_pLabelDistribution->setBuddy(m_pComboDistribution);
                m_pLayout->addWidget(m_pComboDistribution, iRow, 1);
            }
            ++iRow;

            /* Prepare VM OS type label: */
            m_pLabelType = new QLabel(this);
            if (m_pLabelType)
            {
                m_pLabelType->setAlignment(Qt::AlignRight);
                m_pLabelType->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                m_pLayout->addWidget(m_pLabelType, iRow, 0);
            }
            /* Prepare VM OS type combo: */
            m_pComboType = new QComboBox(this);
            if (m_pComboType)
            {
                m_pLabelType->setBuddy(m_pComboType);
                m_pLayout->addWidget(m_pComboType, iRow, 1);
            }
            ++iRow;

            /* Prepare sub-layout: */
            QVBoxLayout *pLayoutIcon = new QVBoxLayout;
            if (pLayoutIcon)
            {
                /* Prepare VM OS type icon: */
                m_pIconType = new QLabel(this);
                if (m_pIconType)
                {
                    m_pIconType->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                    pLayoutIcon->addWidget(m_pIconType);
                }

                /* Add stretch to sub-layout: */
                pLayoutIcon->addStretch();

                /* Add into layout: */
                m_pLayout->addLayout(pLayoutIcon, iIconRow, 2, 3, 1);
            }

            /* Initialize VM OS family combo
             * after all widgets were created: */
            populateFamilyCombo();
        }
    }
    /* Set top most widget of the 2nd column as focus proxy: */
    for (int i = 0; i < m_pLayout->rowCount(); ++i)
    {
        QLayoutItem *pItem = m_pLayout->itemAtPosition(i, 1);
        if (pItem && pItem->widget())
        {
            setFocusProxy(pItem->widget());
            break;
        }
    }
}

void UINameAndSystemEditor::prepareConnections()
{
    if (m_pEditorName)
        connect(m_pEditorName, &QILineEdit::textChanged,
                this, &UINameAndSystemEditor::sigNameChanged);
    if (m_pSelectorPath)
        connect(m_pSelectorPath, &UIFilePathSelector::pathChanged,
                this, &UINameAndSystemEditor::sigPathChanged);
    if (m_pSelectorImage)
        connect(m_pSelectorImage, &UIFilePathSelector::pathChanged,
                this, &UINameAndSystemEditor::sigImageChanged);
    if (m_pComboEdition)
        connect(m_pComboEdition, &QComboBox::currentIndexChanged,
                this, &UINameAndSystemEditor::sltSelectedEditionsChanged);
    if (m_pComboFamily)
        connect(m_pComboFamily, &QComboBox::currentIndexChanged,
                this, &UINameAndSystemEditor::sltFamilyChanged);
    if (m_pComboDistribution)
        connect(m_pComboDistribution, &QComboBox::currentTextChanged,
                this, &UINameAndSystemEditor::sltDistributionChanged);
    if (m_pComboType)
        connect(m_pComboType, &QComboBox::currentIndexChanged,
                this, &UINameAndSystemEditor::sltTypeChanged);
}

ulong UINameAndSystemEditor::selectedEditionIndex() const
{
    /* Sanity check: */
    AssertPtrReturn(m_pComboEdition, 0);

    return m_pComboEdition->count() == 0 ? 0 : m_pComboEdition->currentData().value<ulong>();
}

void UINameAndSystemEditor::populateFamilyCombo()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboFamily);

    /* Get current arch type, usually we'd default to x86, but here 'None' meaningful as well: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_None;

    /* Acquire family IDs: */
    const UIGuestOSTypeManager::UIGuestOSFamilyInfo families
        = gpGlobalSession->guestOSTypeManager().getFamilies(false /* including restricted? */,
                                                            QStringList() << familyId(),
                                                            enmArch);

    /* Block signals initially and clear the combo: */
    m_pComboFamily->blockSignals(true);
    m_pComboFamily->clear();

    /* Populate family combo: */
    for (int i = 0; i < families.size(); ++i)
    {
        const UIFamilyInfo fi = families.at(i);
        m_pComboFamily->addItem(fi.m_strDescription);
        m_pComboFamily->setItemData(i, fi.m_strId);
    }

    /* Select preferred OS family: */
    selectPreferredFamily();

    /* Unblock signals finally: */
    m_pComboFamily->blockSignals(false);

    /* Trigger family change handler manually: */
    sltFamilyChanged(m_pComboFamily->currentIndex());
}

void UINameAndSystemEditor::populateDistributionCombo()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboDistribution);

    /* Get current arch type, usually we'd default to x86, but here 'None' meaningful as well: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_None;

    /* Acquire a list of suitable distributions: */
    const UIGuestOSTypeManager::UIGuestOSSubtypeInfo distributions
        = gpGlobalSession->guestOSTypeManager().getSubtypesForFamilyId(familyId(),
                                                                       false /* including restricted? */,
                                                                       QStringList() << distribution(),
                                                                       enmArch);
    setEnabledByReason(m_pLabelDistribution, 2, !distributions.isEmpty());
    setEnabledByReason(m_pComboDistribution, 2, !distributions.isEmpty());

    /* Block signals initially and clear the combo: */
    m_pComboDistribution->blockSignals(true);
    m_pComboDistribution->clear();

    /* Populate distribution combo: */
    foreach (const UISubtypeInfo &distribution, distributions)
        m_pComboDistribution->addItem(distribution.m_strName);

    /* Select preferred OS distribution: */
    selectPreferredDistribution();

    /* Unblock signals finally: */
    m_pComboDistribution->blockSignals(false);

    /* Trigger distribution change handler manually: */
    sltDistributionChanged(m_pComboDistribution->currentText());
}

void UINameAndSystemEditor::populateTypeCombo(const UIGuestOSTypeManager::UIGuestOSTypeInfo &types)
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboType);
    AssertReturnVoid(!types.isEmpty());

    /* Block signals initially and clear the combo: */
    m_pComboType->blockSignals(true);
    m_pComboType->clear();

    /* Populate type combo: */
    for (int i = 0; i < types.size(); ++i)
    {
        m_pComboType->addItem(types[i].second);
        m_pComboType->setItemData(i, types[i].first);
    }

    /* Select preferred OS type: */
    selectPreferredType();

    /* Unblock signals finally: */
    m_pComboType->blockSignals(false);

    /* Trigger type change handler manually: */
    sltTypeChanged(m_pComboType->currentIndex());
}

void UINameAndSystemEditor::selectPreferredFamily()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboFamily);

    /* What will be the choice? */
    int iChosenIndex = -1;

    /* Try to restore previous family if possible: */
    if (   iChosenIndex == -1
        && !familyId().isEmpty())
    {
//        printf("Restoring family to: %s\n",
//               familyId().toUtf8().constData());
        iChosenIndex = m_pComboFamily->findData(familyId());
    }

    /* Choose the item under the index we found or 1st one item otherwise: */
    m_pComboFamily->setCurrentIndex(iChosenIndex != -1 ? iChosenIndex : 0);
}

void UINameAndSystemEditor::selectPreferredDistribution()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboDistribution);

    /* What will be the choice? */
    int iChosenIndex = -1;

    /* Try to restore previous distribution if possible: */
    if (   iChosenIndex == -1
        && !distribution().isEmpty())
    {
//        printf("Restoring distribution [for family=%s] to: %s\n",
//               familyId().toUtf8().constData(),
//               distribution().toUtf8().constData());
        iChosenIndex = m_pComboDistribution->findText(distribution());
    }

    /* Try to choose Oracle Linux for Linux family: */
    if (   iChosenIndex == -1
        && familyId() == "Linux")
        iChosenIndex = m_pComboDistribution->findText("Oracle", Qt::MatchContains);

    /* Choose the item under the index we found or 1st one item otherwise: */
    m_pComboDistribution->setCurrentIndex(iChosenIndex != -1 ? iChosenIndex : 0);
}

void UINameAndSystemEditor::selectPreferredType()
{
    /* Sanity check: */
    AssertPtrReturnVoid(m_pComboType);

    /* What will be the choice? */
    int iChosenIndex = -1;

    /* Try to restore previous type if possible: */
    if (   iChosenIndex == -1
        && !typeId().isEmpty())
    {
//        printf("Restoring type [for family=%s/distribution=%s] to: %s\n",
//               familyId().toUtf8().constData(),
//               distribution().toUtf8().constData(),
//               typeId().toUtf8().constData());
        iChosenIndex = m_pComboType->findData(typeId());
    }

    /* Try to choose Windows 11 for Windows family: */
    if (   iChosenIndex == -1
        && familyId() == "Windows")
    {
        const QString strDefaultID = GUEST_OS_ID_STR_X64("Windows11");
        iChosenIndex = m_pComboType->findData(strDefaultID);
    }

    /* Try to choose Oracle Linux x64 for Oracle distribution: */
    if (   iChosenIndex == -1
        && distribution() == "Oracle")
    {
        const QString strDefaultID = GUEST_OS_ID_STR_X64("Oracle");
        iChosenIndex = m_pComboType->findData(strDefaultID);
    }

    /* Else try to pick the first 64-bit one if it exists: */
    if (iChosenIndex == -1)
    {
        const QString strDefaultID = GUEST_OS_ID_STR_X64("");
        iChosenIndex = m_pComboType->findData(strDefaultID, Qt::UserRole, Qt::MatchContains);
    }

    /* Choose the item under the index we found or 1st one item otherwise: */
    m_pComboType->setCurrentIndex(iChosenIndex != -1 ? iChosenIndex : 0);
}

void UINameAndSystemEditor::setEnabledByReason(QWidget *pWidget, uint uReason, bool fEnabled)
{
    /* Some widgets can be enabled by two independent reasons;
     * and we want to actually enable them only by both. */

    /* Make sure passed widget is valid: */
    AssertPtrReturnVoid(pWidget);
    /* Make sure uReason provided is equal to 1 or 2: */
    AssertReturnVoid(uReason == 1 || uReason == 2);

    /* Property template: */
    QString strPropertyTemplate("enabledByReason_%1");

    /* Update value for passed uReason: */
    pWidget->setProperty(strPropertyTemplate.arg(uReason).toUtf8().constData(), fEnabled);

    /* Make sure widget enabled only if requested by both reasons: */
    const QVariant property1 = pWidget->property(strPropertyTemplate.arg(1).toUtf8().constData());
    const QVariant property2 = pWidget->property(strPropertyTemplate.arg(2).toUtf8().constData());
    const bool fEnabledByReason1 = !property1.isValid() || property1.toBool();
    const bool fEnabledByReason2 = !property2.isValid() || property2.toBool();
    pWidget->setEnabled(fEnabledByReason1 && fEnabledByReason2);
}
