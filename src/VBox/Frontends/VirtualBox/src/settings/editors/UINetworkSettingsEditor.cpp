/* $Id: UINetworkSettingsEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkSettingsEditor class implementation.
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
#include <QCheckBox>
#include <QGridLayout>
#include <QVBoxLayout>

/* GUI includes: */
#include "UINetworkAttachmentEditor.h"
#include "UINetworkFeaturesEditor.h"
#include "UINetworkSettingsEditor.h"


UINetworkSettingsEditor::UINetworkSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fFeatureEnabled(false)
    , m_pCheckboxFeature(0)
    , m_pWidgetSettings(0)
    , m_pEditorNetworkAttachment(0)
    , m_pEditorNetworkFeatures(0)
{
    prepare();
}

void UINetworkSettingsEditor::setFeatureEnabled(bool fEnabled)
{
    if (m_fFeatureEnabled != fEnabled)
    {
        m_fFeatureEnabled = fEnabled;
        if (m_pCheckboxFeature)
            m_pCheckboxFeature->setChecked(m_fFeatureEnabled);
    }
}

bool UINetworkSettingsEditor::isFeatureEnabled() const
{
    return m_pCheckboxFeature ? m_pCheckboxFeature->isChecked() : m_fFeatureEnabled;
}

void UINetworkSettingsEditor::setFeatureAvailable(bool fAvailable)
{
    if (m_pCheckboxFeature)
        m_pCheckboxFeature->setEnabled(fAvailable);
}

void UINetworkSettingsEditor::setValueType(KNetworkAttachmentType enmType)
{
    if (m_pEditorNetworkAttachment)
        m_pEditorNetworkAttachment->setValueType(enmType);
}

KNetworkAttachmentType UINetworkSettingsEditor::valueType() const
{
    return m_pEditorNetworkAttachment ? m_pEditorNetworkAttachment->valueType() : KNetworkAttachmentType_Null;
}

void UINetworkSettingsEditor::setValueNames(KNetworkAttachmentType enmType, const QStringList &names)
{
    if (m_pEditorNetworkAttachment)
        m_pEditorNetworkAttachment->setValueNames(enmType, names);
}

void UINetworkSettingsEditor::setValueName(KNetworkAttachmentType enmType, const QString &strName)
{
    if (m_pEditorNetworkAttachment)
        m_pEditorNetworkAttachment->setValueName(enmType, strName);
}

QString UINetworkSettingsEditor::valueName(KNetworkAttachmentType enmType) const
{
    return m_pEditorNetworkAttachment ? m_pEditorNetworkAttachment->valueName(enmType) : QString();
}

void UINetworkSettingsEditor::setAttachmentOptionsAvailable(bool fAvailable)
{
    if (m_pEditorNetworkAttachment)
        m_pEditorNetworkAttachment->setEnabled(fAvailable);
}

void UINetworkSettingsEditor::setAdapterType(const KNetworkAdapterType &enmType)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setAdapterType(enmType);
}

KNetworkAdapterType UINetworkSettingsEditor::adapterType() const
{
    return m_pEditorNetworkFeatures ? m_pEditorNetworkFeatures->adapterType() : KNetworkAdapterType_Null;
}

void UINetworkSettingsEditor::setPromiscuousMode(const KNetworkAdapterPromiscModePolicy &enmMode)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setPromiscuousMode(enmMode);
}

KNetworkAdapterPromiscModePolicy UINetworkSettingsEditor::promiscuousMode() const
{
    return m_pEditorNetworkFeatures ? m_pEditorNetworkFeatures->promiscuousMode() : KNetworkAdapterPromiscModePolicy_Deny;
}

void UINetworkSettingsEditor::setMACAddress(const QString &strAddress)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setMACAddress(strAddress);
}

QString UINetworkSettingsEditor::macAddress() const
{
    return m_pEditorNetworkFeatures ? m_pEditorNetworkFeatures->macAddress() : QString();
}

void UINetworkSettingsEditor::setGenericProperties(const QString &strProperties)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setGenericProperties(strProperties);
}

QString UINetworkSettingsEditor::genericProperties() const
{
    return m_pEditorNetworkFeatures ? m_pEditorNetworkFeatures->genericProperties() : QString();
}

void UINetworkSettingsEditor::setCableConnected(bool fConnected)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setCableConnected(fConnected);
}

bool UINetworkSettingsEditor::cableConnected() const
{
    return m_pEditorNetworkFeatures ? m_pEditorNetworkFeatures->cableConnected() : false;
}

void UINetworkSettingsEditor::setPortForwardingRules(const UIPortForwardingDataList &rules)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setPortForwardingRules(rules);
}

UIPortForwardingDataList UINetworkSettingsEditor::portForwardingRules() const
{
    return m_pEditorNetworkFeatures ? m_pEditorNetworkFeatures->portForwardingRules() : UIPortForwardingDataList();
}

void UINetworkSettingsEditor::setAdapterOptionsAvailable(bool fAvailable)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setAdapterOptionsAvailable(fAvailable);
}

void UINetworkSettingsEditor::setPromiscuousOptionsAvailable(bool fAvailable)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setPromiscuousOptionsAvailable(fAvailable);
}

void UINetworkSettingsEditor::setMACOptionsAvailable(bool fAvailable)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setMACOptionsAvailable(fAvailable);
}

void UINetworkSettingsEditor::setGenericPropertiesAvailable(bool fAvailable)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setGenericPropertiesAvailable(fAvailable);
}

void UINetworkSettingsEditor::setCableOptionsAvailable(bool fAvailable)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setCableOptionsAvailable(fAvailable);
}

void UINetworkSettingsEditor::setForwardingOptionsAvailable(bool fAvailable)
{
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setForwardingOptionsAvailable(fAvailable);
}

void UINetworkSettingsEditor::sltRetranslateUI()
{
    if (m_pCheckboxFeature)
    {
        m_pCheckboxFeature->setText(tr("&Enable Network Adapter"));
        m_pCheckboxFeature->setToolTip(tr("When checked, plugs this virtual network adapter into the virtual machine."));
    }

    updateMinimumLayoutHint();
}

void UINetworkSettingsEditor::handleFilterChange()
{
    updateMinimumLayoutHint();
}

void UINetworkSettingsEditor::sltHandleFeatureToggled()
{
    /* Update widget availability: */
    updateFeatureAvailability();

    /* Generate a new MAC address in case it's currently empty: */
    if (   m_pCheckboxFeature->isChecked()
        && m_pEditorNetworkFeatures->macAddress().isEmpty())
        m_pEditorNetworkFeatures->generateMac();

    /* Notify listeners: */
    emit sigFeatureStateChanged();
}

void UINetworkSettingsEditor::sltHandleAttachmentTypeChange()
{
    /* Update widget availability: */
    const KNetworkAttachmentType enmType = m_pEditorNetworkAttachment->valueType();
    m_pEditorNetworkFeatures->setPromiscuousOptionsAvailable(   enmType != KNetworkAttachmentType_Null
                                                             && enmType != KNetworkAttachmentType_Generic
                                                             && enmType != KNetworkAttachmentType_NAT);
    m_pEditorNetworkFeatures->setGenericPropertiesAvailable(enmType == KNetworkAttachmentType_Generic);
    m_pEditorNetworkFeatures->setForwardingOptionsAvailable(enmType == KNetworkAttachmentType_NAT);

    /* Notify listeners: */
    emit sigAttachmentTypeChanged();
}

void UINetworkSettingsEditor::prepare()
{
    /* Prepare stuff: */
    prepareWidgets();
    prepareConnections();

    /* Update widget availability: */
    updateFeatureAvailability();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UINetworkSettingsEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare adapter check-box: */
        m_pCheckboxFeature = new QCheckBox(this);
        if (m_pCheckboxFeature)
            pLayout->addWidget(m_pCheckboxFeature, 0, 0, 1, 2);

        /* Prepare 20-px shifting spacer: */
        QSpacerItem *pSpacerItem = new QSpacerItem(20, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (pSpacerItem)
            pLayout->addItem(pSpacerItem, 1, 0);

        /* Prepare adapter settings widget: */
        m_pWidgetSettings = new QWidget(this);
        if (m_pWidgetSettings)
        {
            /* Prepare adapter settings widget layout: */
            QVBoxLayout *pLayoutAdapterSettings = new QVBoxLayout(m_pWidgetSettings);
            if (pLayoutAdapterSettings)
            {
                pLayoutAdapterSettings->setContentsMargins(0, 0, 0, 0);

                /* Prepare attachment type editor: */
                m_pEditorNetworkAttachment = new UINetworkAttachmentEditor(m_pWidgetSettings);
                if (m_pEditorNetworkAttachment)
                {
                    addEditor(m_pEditorNetworkAttachment);
                    pLayoutAdapterSettings->addWidget(m_pEditorNetworkAttachment);
                }

                /* Prepare advanced settingseditor: */
                m_pEditorNetworkFeatures = new UINetworkFeaturesEditor(m_pWidgetSettings);
                if (m_pEditorNetworkFeatures)
                {
                    addEditor(m_pEditorNetworkFeatures);
                    pLayoutAdapterSettings->addWidget(m_pEditorNetworkFeatures);
                }
            }

            pLayout->addWidget(m_pWidgetSettings, 1, 1);
        }
    }
}

void UINetworkSettingsEditor::prepareConnections()
{
    if (m_pCheckboxFeature)
        connect(m_pCheckboxFeature, &QCheckBox::stateChanged,
                this, &UINetworkSettingsEditor::sltHandleFeatureToggled);
    if (m_pEditorNetworkAttachment)
        connect(m_pEditorNetworkAttachment, &UINetworkAttachmentEditor::sigValueTypeChanged,
                this, &UINetworkSettingsEditor::sltHandleAttachmentTypeChange);
    if (m_pEditorNetworkAttachment)
        connect(m_pEditorNetworkAttachment, &UINetworkAttachmentEditor::sigValueNameChanged,
                this, &UINetworkSettingsEditor::sigAlternativeNameChanged);
    if (m_pEditorNetworkFeatures)
        connect(m_pEditorNetworkFeatures, &UINetworkFeaturesEditor::sigMACAddressChanged,
                this, &UINetworkSettingsEditor::sigMACAddressChanged);
}

void UINetworkSettingsEditor::updateFeatureAvailability()
{
    m_pWidgetSettings->setEnabled(m_pCheckboxFeature->isChecked());
}

void UINetworkSettingsEditor::updateMinimumLayoutHint()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    if (m_pEditorNetworkAttachment && !m_pEditorNetworkAttachment->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorNetworkAttachment->minimumLabelHorizontalHint());
    if (m_pEditorNetworkFeatures && !m_pEditorNetworkFeatures->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorNetworkFeatures->minimumLabelHorizontalHint());
    if (m_pEditorNetworkAttachment)
        m_pEditorNetworkAttachment->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorNetworkFeatures)
        m_pEditorNetworkFeatures->setMinimumLayoutIndent(iMinimumLayoutHint);
}
