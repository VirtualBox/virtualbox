/* $Id: UINetworkAttachmentEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkAttachmentEditor class implementation.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <QHBoxLayout>
#include <QLabel>

/* GUI includes: */
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UINetworkAttachmentEditor.h"

/* COM includes: */
#include "CHostNetworkInterface.h"
#include "CNATNetwork.h"
#include "CSystemProperties.h"
#ifdef VBOX_WITH_CLOUD_NET
# include "CCloudNetwork.h"
#endif
#ifdef VBOX_WITH_VMNET
# include "CHostOnlyNetwork.h"
#endif


/* static */
QString UINetworkAttachmentEditor::s_strEmptyItemId = QString("#empty#");

UINetworkAttachmentEditor::UINetworkAttachmentEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_enmRestrictedNetworkAttachmentTypes(UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid)
    , m_enmType(KNetworkAttachmentType_Max)
    , m_pLayout(0)
    , m_pLabelType(0)
    , m_pComboType(0)
    , m_pLabelName(0)
    , m_pComboName(0)
{
    prepare();
}

void UINetworkAttachmentEditor::setValueType(KNetworkAttachmentType enmType)
{
    if (m_enmType != enmType)
    {
        m_enmType = enmType;
        populateTypeCombo();
    }
}

KNetworkAttachmentType UINetworkAttachmentEditor::valueType() const
{
    return m_pComboType ? m_pComboType->currentData().value<KNetworkAttachmentType>() : m_enmType;
}

void UINetworkAttachmentEditor::setValueNames(KNetworkAttachmentType enmType, const QStringList &names)
{
    /* Save possible names for passed type: */
    m_names[enmType] = names;

    /* If value type is the same, update the combo as well: */
    if (valueType() == enmType)
        populateNameCombo();
}

void UINetworkAttachmentEditor::setValueName(KNetworkAttachmentType enmType, const QString &strName)
{
    /* Save current name for passed type: */
    m_name[enmType] = strName;

    /* If value type is the same, update the combo as well: */
    if (valueType() == enmType)
    {
        /* Make sure combo is there: */
        if (m_pComboName)
        {
            const int iIndex = m_pComboName->findText(strName);
            if (iIndex != -1)
                m_pComboName->setCurrentIndex(iIndex);
        }
    }
}

QString UINetworkAttachmentEditor::valueName(KNetworkAttachmentType enmType) const
{
    return m_name.value(enmType);
}

int UINetworkAttachmentEditor::minimumLabelHorizontalHint() const
{
    int iMinimumLabelHorizontalHint = 0;
    if (m_pLabelType)
        iMinimumLabelHorizontalHint = qMax(iMinimumLabelHorizontalHint, m_pLabelType->minimumSizeHint().width());
    if (m_pLabelName)
        iMinimumLabelHorizontalHint = qMax(iMinimumLabelHorizontalHint, m_pLabelName->minimumSizeHint().width());
    return iMinimumLabelHorizontalHint;
}

void UINetworkAttachmentEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

/* static */
QStringList UINetworkAttachmentEditor::bridgedAdapters()
{
    QStringList bridgedAdapterList;
    foreach (const CHostNetworkInterface &comInterface, gpGlobalSession->host().GetNetworkInterfaces())
    {
        if (   comInterface.GetInterfaceType() == KHostNetworkInterfaceType_Bridged
            && !bridgedAdapterList.contains(comInterface.GetName()))
            bridgedAdapterList << comInterface.GetName();
    }
    return bridgedAdapterList;
}

/* static */
QStringList UINetworkAttachmentEditor::internalNetworks()
{
    return QList<QString>::fromVector(gpGlobalSession->virtualBox().GetInternalNetworks());
}

/* static */
QStringList UINetworkAttachmentEditor::hostInterfaces()
{
    QStringList hostInterfaceList;
    foreach (const CHostNetworkInterface &comInterface, gpGlobalSession->host().GetNetworkInterfaces())
    {
        if (   comInterface.GetInterfaceType() == KHostNetworkInterfaceType_HostOnly
            && !hostInterfaceList.contains(comInterface.GetName()))
            hostInterfaceList << comInterface.GetName();
    }
    return hostInterfaceList;
}

/* static */
QStringList UINetworkAttachmentEditor::genericDrivers()
{
    return QList<QString>::fromVector(gpGlobalSession->virtualBox().GetGenericNetworkDrivers());
}

/* static */
QStringList UINetworkAttachmentEditor::natNetworks()
{
    QStringList natNetworkList;
    foreach (const CNATNetwork &comNetwork, gpGlobalSession->virtualBox().GetNATNetworks())
        natNetworkList << comNetwork.GetNetworkName();
    return natNetworkList;
}

#ifdef VBOX_WITH_CLOUD_NET
/* static */
QStringList UINetworkAttachmentEditor::cloudNetworks()
{
    QStringList cloudNetworkList;
    foreach (const CCloudNetwork &comNetwork, gpGlobalSession->virtualBox().GetCloudNetworks())
        cloudNetworkList << comNetwork.GetNetworkName();
    return cloudNetworkList;
}
#endif /* VBOX_WITH_CLOUD_NET */

#ifdef VBOX_WITH_VMNET
/* static */
QStringList UINetworkAttachmentEditor::hostOnlyNetworks()
{
    QStringList hostOnlyNetworkList;
    foreach (const CHostOnlyNetwork &comNetwork, gpGlobalSession->virtualBox().GetHostOnlyNetworks())
        hostOnlyNetworkList << comNetwork.GetNetworkName();
    return hostOnlyNetworkList;
}
#endif /* VBOX_WITH_VMNET */

void UINetworkAttachmentEditor::sltRetranslateUI()
{
    /* Translate type label: */
    if (m_pLabelType)
        m_pLabelType->setText(tr("&Attached to:"));

    /* Translate name label: */
    if (m_pLabelName)
        m_pLabelName->setText(tr("&Name:"));

    /* Translate type combo: */
    if (m_pComboType)
    {
        for (int i = 0; i < m_pComboType->count(); ++i)
        {
            const KNetworkAttachmentType enmType = m_pComboType->itemData(i).value<KNetworkAttachmentType>();
            m_pComboType->setItemText(i, gpConverter->toString(enmType));
        }
        m_pComboType->setToolTip(tr("Holds how this virtual adapter is attached to the real network of the Host OS."));
    }

    /* Translate name combo: */
    retranslateNameDescription();
}

void UINetworkAttachmentEditor::handleFilterChange()
{
    /* Repopulate type combo to make
     * sure excessive types removed: */
    populateTypeCombo();
}

void UINetworkAttachmentEditor::sltHandleCurrentTypeChanged()
{
    /* Update name label & combo: */
    if (m_pLabelName)
        m_pLabelName->setEnabled(   valueType() != KNetworkAttachmentType_Null
                                 && valueType() != KNetworkAttachmentType_NAT);
    if (m_pComboName)
    {
        m_pComboName->setEnabled(   valueType() != KNetworkAttachmentType_Null
                                 && valueType() != KNetworkAttachmentType_NAT);
        m_pComboName->setEditable(   valueType() == KNetworkAttachmentType_Internal
                                  || valueType() == KNetworkAttachmentType_Generic);
    }

    /* Update name combo description: */
    retranslateNameDescription();

    /* Notify listeners: */
    emit sigValueTypeChanged();

    /* Update name combo: */
    populateNameCombo();

    /* Revalidate: */
    revalidate();
}

void UINetworkAttachmentEditor::sltHandleCurrentNameChanged()
{
    if (m_pComboName)
    {
        /* Acquire new value name: */
        QString strNewName;
        /* Make sure that's not a name of 'empty' item: */
        if (m_pComboName->currentData().toString() != s_strEmptyItemId)
            strNewName = m_pComboName->currentText().simplified();
        /* Make sure that's not an empty name itself: */
        if (strNewName.isEmpty())
            strNewName = QString();
        /* If name is really changed: */
        if (m_name[valueType()] != strNewName)
        {
            /* Store it: */
            m_name[valueType()] = strNewName;
            /* Notify listeners: */
            emit sigValueNameChanged();
        }
    }

    /* Revalidate: */
    revalidate();
}

void UINetworkAttachmentEditor::prepare()
{
    /* Read current limitations: */
    m_enmRestrictedNetworkAttachmentTypes = gEDataManager->restrictedNetworkAttachmentTypes();

    /* Create main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create type label: */
        m_pLabelType = new QLabel(this);
        if (m_pLabelType)
        {
            m_pLabelType->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
            m_pLayout->addWidget(m_pLabelType, 0, 0);
        }
        /* Create type combo layout: */
        QHBoxLayout *pComboLayout = new QHBoxLayout;
        if (pComboLayout)
        {
            /* Create type combo: */
            m_pComboType = new QComboBox(this);
            if (m_pComboType)
            {
                if (m_pLabelType)
                    m_pLabelType->setBuddy(m_pComboType);
                connect(m_pComboType, &QComboBox::currentIndexChanged,
                        this, &UINetworkAttachmentEditor::sltHandleCurrentTypeChanged);
                pComboLayout->addWidget(m_pComboType);
            }

            /* Add stretch: */
            pComboLayout->addStretch();

            /* Add combo-layout into main-layout: */
            m_pLayout->addLayout(pComboLayout, 0, 1);
        }

        /* Create name label: */
        m_pLabelName = new QLabel(this);
        m_pLabelName->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
        if (m_pLabelName)
            m_pLayout->addWidget(m_pLabelName, 1, 0);
        /* Create name combo: */
        m_pComboName = new QComboBox(this);
        if (m_pComboName)
        {
            if (m_pLabelName)
                m_pLabelName->setBuddy(m_pComboName);
            m_pComboName->setInsertPolicy(QComboBox::NoInsert);
            connect(m_pComboName, &QComboBox::currentIndexChanged,
                    this, &UINetworkAttachmentEditor::sltHandleCurrentNameChanged);
            connect(m_pComboName, &QComboBox::editTextChanged,
                    this, &UINetworkAttachmentEditor::sltHandleCurrentNameChanged);
            m_pLayout->addWidget(m_pComboName, 1, 1);
        }
    }

    /* Populate type combo: */
    populateTypeCombo();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UINetworkAttachmentEditor::populateTypeCombo()
{
    /* Make sure combo is there: */
    if (!m_pComboType)
        return;

    /* Block signals initially: */
    m_pComboType->blockSignals(true);

    /* Clear the type combo-box: */
    m_pComboType->clear();

    /* Load currently supported network attachment types (system-properties getter): */
    CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    QVector<KNetworkAttachmentType> supportedTypes = comProperties.GetSupportedNetworkAttachmentTypes();
    /* Filter out types unrelated to current experience mode: */
    if (!m_fInExpertMode)
    {
        /* Keep only allowed types but in the same order they came from CSystemProperties: */
        const QVector<KNetworkAttachmentType> allowedTypes =  QVector<KNetworkAttachmentType>()
                                                           << KNetworkAttachmentType_NAT
                                                           << KNetworkAttachmentType_Bridged;
        QVector<KNetworkAttachmentType> resultingTypes;
        foreach (KNetworkAttachmentType enmType, supportedTypes)
            if (allowedTypes.contains(enmType))
                resultingTypes << enmType;
        supportedTypes = resultingTypes;
    }
    /* Take currently requested type into account if it's different from initial one: */
    if (!supportedTypes.contains(m_enmType) && m_enmType != KNetworkAttachmentType_Max)
        supportedTypes.prepend(m_enmType);

    /* Populate attachment types: */
    int iAttachmentTypeIndex = 0;
    foreach (const KNetworkAttachmentType &enmType, supportedTypes)
    {
        /* Filter currently restricted network attachment types (extra-data getter): */
        if (m_enmRestrictedNetworkAttachmentTypes & toUiNetworkAdapterEnum(enmType))
            continue;
        m_pComboType->insertItem(iAttachmentTypeIndex, gpConverter->toString(enmType));
        m_pComboType->setItemData(iAttachmentTypeIndex, QVariant::fromValue(enmType));
        ++iAttachmentTypeIndex;
    }

    /* Restore previously selected type if possible: */
    const int iIndex = m_pComboType->findData(QVariant::fromValue(m_enmType));
    m_pComboType->setCurrentIndex(iIndex != -1 ? iIndex : 0);

    /* Handle combo item change: */
    sltHandleCurrentTypeChanged();

    /* Unblock signals finally: */
    m_pComboType->blockSignals(false);
}

void UINetworkAttachmentEditor::populateNameCombo()
{
    /* Make sure combo is there: */
    if (!m_pComboName)
        return;

    /* Block signals initially: */
    m_pComboName->blockSignals(true);

    /* Clear the name combo: */
    m_pComboName->clear();

    /* Add corresponding names to combo: */
    m_pComboName->addItems(m_names.value(valueType()));

    /* Prepend 'empty' or 'default' item to combo: */
    if (m_pComboName->count() == 0)
    {
        switch (valueType())
        {
            case KNetworkAttachmentType_Bridged:
            case KNetworkAttachmentType_HostOnly:
            case KNetworkAttachmentType_NATNetwork:
#ifdef VBOX_WITH_CLOUD_NET
            case KNetworkAttachmentType_Cloud:
#endif
#ifdef VBOX_WITH_VMNET
            case KNetworkAttachmentType_HostOnlyNetwork:
#endif
            {
                /* If adapter list is empty => add 'Not selected' item: */
                const int iIndex = m_pComboName->findData(s_strEmptyItemId);
                if (iIndex == -1)
                    m_pComboName->insertItem(0, tr("Not selected", "network adapter name"), s_strEmptyItemId);
                else
                    m_pComboName->setItemText(iIndex, tr("Not selected", "network adapter name"));
                break;
            }
            case KNetworkAttachmentType_Internal:
            {
                /* Internal network list should have a default item: */
                if (m_pComboName->findText("intnet") == -1)
                    m_pComboName->insertItem(0, "intnet");
                break;
            }
            default:
                break;
        }
    }

    /* Restore previously selected name: */
    const int iIndex = m_pComboName->findText(m_name.value(valueType()));
    if (iIndex != -1)
        m_pComboName->setCurrentIndex(iIndex);

    /* Handle combo item change: */
    sltHandleCurrentNameChanged();

    /* Unblock signals finally: */
    m_pComboName->blockSignals(false);
}

void UINetworkAttachmentEditor::retranslateNameDescription()
{
    /* Update name combo description: */
    switch (valueType())
    {
        case KNetworkAttachmentType_Bridged:
            m_pComboName->setToolTip(tr("Holds the network adapter on the host system that traffic "
                                        "to and from this network card will go through."));
            break;
        case KNetworkAttachmentType_Internal:
            m_pComboName->setToolTip(tr("Holds the name of the internal network that this network card "
                                        "will be connected to. You can create a new internal network by "
                                        "choosing a name which is not used by any other network cards "
                                        "in this virtual machine or others."));
            break;
        case KNetworkAttachmentType_HostOnly:
            m_pComboName->setToolTip(tr("Holds the virtual network adapter on the host system that traffic "
                                        "to and from this network card will go through. "
                                        "You can create and remove adapters using the Network Manager "
                                        "tool in the virtual machine manager window."));
            break;
        case KNetworkAttachmentType_Generic:
            m_pComboName->setToolTip(tr("Holds the driver to be used with this network card."));
            break;
        case KNetworkAttachmentType_NATNetwork:
            m_pComboName->setToolTip(tr("Holds the name of the NAT network that this network card "
                                        "will be connected to. You can create and remove networks "
                                        "using the Network Manager tool in the virtual machine "
                                        "manager window."));
            break;
#ifdef VBOX_WITH_CLOUD_NET
        case KNetworkAttachmentType_Cloud:
            m_pComboName->setToolTip(tr("(experimental) Holds the name of the cloud network that this network card "
                                        "will be connected to. You can add and remove networks "
                                        "using the Network Manager tool in the virtual machine "
                                        "manager window."));
            break;
#endif /* VBOX_WITH_CLOUD_NET */
#ifdef VBOX_WITH_VMNET
        case KNetworkAttachmentType_HostOnlyNetwork:
            m_pComboName->setToolTip(tr("Holds the name of the host-only network that this network card "
                                        "will be connected to. You can add and remove networks "
                                        "using the Network Manager tool in the virtual machine "
                                        "manager window."));
            break;
#endif /* VBOX_WITH_VMNET */
        default:
            m_pComboName->setToolTip(QString());
            break;
    }
}

void UINetworkAttachmentEditor::revalidate()
{
    bool fSuccess = false;
    switch (valueType())
    {
        case KNetworkAttachmentType_Bridged:
        case KNetworkAttachmentType_Internal:
        case KNetworkAttachmentType_HostOnly:
        case KNetworkAttachmentType_Generic:
        case KNetworkAttachmentType_NATNetwork:
#ifdef VBOX_WITH_CLOUD_NET
        case KNetworkAttachmentType_Cloud:
#endif
#ifdef VBOX_WITH_VMNET
        case KNetworkAttachmentType_HostOnlyNetwork:
#endif
            fSuccess = !valueName(valueType()).isEmpty();
            break;
        default:
            fSuccess = true;
            break;
    }
    emit sigValidChanged(fSuccess);
}

/* static */
UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork UINetworkAttachmentEditor::toUiNetworkAdapterEnum(KNetworkAttachmentType comEnum)
{
    switch (comEnum)
    {
        case KNetworkAttachmentType_NAT:             return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NAT;
        case KNetworkAttachmentType_Bridged:         return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_BridgedAdapter;
        case KNetworkAttachmentType_Internal:        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_InternalNetwork;
        case KNetworkAttachmentType_HostOnly:        return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyAdapter;
        case KNetworkAttachmentType_Generic:         return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_GenericDriver;
        case KNetworkAttachmentType_NATNetwork:      return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_NATNetwork;
#ifdef VBOX_WITH_CLOUD_NET
        case KNetworkAttachmentType_Cloud:           return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_CloudNetwork;
#endif
#ifdef VBOX_WITH_VMNET
        case KNetworkAttachmentType_HostOnlyNetwork: return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_HostOnlyNetwork;
#endif
        default:                                     return UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid;
    }
}
