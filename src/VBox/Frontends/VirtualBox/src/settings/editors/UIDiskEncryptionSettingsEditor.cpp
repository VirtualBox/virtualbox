/* $Id: UIDiskEncryptionSettingsEditor.cpp 168930 2025-05-19 15:06:14Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDiskEncryptionSettingsEditor class implementation.
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
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>

/* GUI includes: */
#include "UIConverter.h"
#include "UIDiskEncryptionSettingsEditor.h"


UIDiskEncryptionSettingsEditor::UIDiskEncryptionSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fFeatureEnabled(false)
    , m_enmCipherType(UIDiskEncryptionCipherType_Max)
    , m_pCheckboxFeature(0)
    , m_pWidgetSettings(0)
    , m_pLabelCipherType(0)
    , m_pComboCipherType(0)
    , m_pLabelPassword1(0)
    , m_pEditorPassword1(0)
    , m_pLabelPassword2(0)
    , m_pEditorPassword2(0)
{
    prepare();
}

void UIDiskEncryptionSettingsEditor::setFeatureEnabled(bool fEnabled)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fFeatureEnabled != fEnabled)
    {
        m_fFeatureEnabled = fEnabled;
        if (m_pCheckboxFeature)
        {
            m_pCheckboxFeature->setChecked(m_fFeatureEnabled);
            sltHandleFeatureToggled(m_pCheckboxFeature->isChecked());
        }
    }
}

bool UIDiskEncryptionSettingsEditor::isFeatureEnabled() const
{
    return m_pCheckboxFeature ? m_pCheckboxFeature->isChecked() : m_fFeatureEnabled;
}

void UIDiskEncryptionSettingsEditor::setCipherType(const UIDiskEncryptionCipherType &enmType)
{
    /* Update cached value and
     * combo if value has changed: */
    if (m_enmCipherType != enmType)
    {
        m_enmCipherType = enmType;
        repopulateCombo();
    }
}

UIDiskEncryptionCipherType UIDiskEncryptionSettingsEditor::cipherType() const
{
    return m_pComboCipherType ? m_pComboCipherType->currentData().value<UIDiskEncryptionCipherType>() : m_enmCipherType;
}

QString UIDiskEncryptionSettingsEditor::password1() const
{
    return m_pEditorPassword1 ? m_pEditorPassword1->text() : m_strPassword1;
}

QString UIDiskEncryptionSettingsEditor::password2() const
{
    return m_pEditorPassword2 ? m_pEditorPassword2->text() : m_strPassword2;
}

void UIDiskEncryptionSettingsEditor::sltRetranslateUI()
{
    if (m_pCheckboxFeature)
    {
        m_pCheckboxFeature->setText(tr("&Encrypt Disks"));
        m_pCheckboxFeature->setToolTip(tr("Encrypt the disks attached to this VM"));
    }

    if (m_pLabelCipherType)
        m_pLabelCipherType->setText(tr("Disk Encryption C&ipher"));
    if (m_pComboCipherType)
    {
        for (int iIndex = 0; iIndex < m_pComboCipherType->count(); ++iIndex)
        {
            const UIDiskEncryptionCipherType enmType = m_pComboCipherType->itemData(iIndex).value<UIDiskEncryptionCipherType>();
            m_pComboCipherType->setItemText(iIndex, gpConverter->toString(enmType));
        }
        m_pComboCipherType->setToolTip(tr("Cipher type used to encrypt the disks"));
    }

    if (m_pLabelPassword1)
        m_pLabelPassword1->setText(tr("E&nter New Encryption Password"));
    if (m_pEditorPassword1)
        m_pEditorPassword1->setToolTip(tr("Password to access disks attached to this VM"));
    if (m_pLabelPassword2)
        m_pLabelPassword2->setText(tr("C&onfirm New Encryption Password"));
    if (m_pEditorPassword2)
        m_pEditorPassword2->setToolTip(tr("Re-enter the password to confirm it matches"));

    /* Translate Cipher type combo: */
    m_pComboCipherType->setItemText(0, tr("Leave Unchanged", "cipher type"));
}

void UIDiskEncryptionSettingsEditor::sltHandleFeatureToggled(bool fEnabled)
{
    /* Update widget availability: */
    if (m_pWidgetSettings)
        m_pWidgetSettings->setEnabled(fEnabled);

    /* Notify listeners: */
    emit sigStatusChanged();
}

void UIDiskEncryptionSettingsEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIDiskEncryptionSettingsEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);
        pLayout->setColumnStretch(1, 1);

        /* Prepare 'feature' check-box: */
        m_pCheckboxFeature = new QCheckBox(this);
        if (m_pCheckboxFeature)
            pLayout->addWidget(m_pCheckboxFeature, 0, 0, 1, 2);

        /* Prepare 20-px shifting spacer: */
        QSpacerItem *pSpacerItem = new QSpacerItem(20, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (pSpacerItem)
            pLayout->addItem(pSpacerItem, 1, 0);

        /* Prepare 'settings' widget: */
        m_pWidgetSettings = new QWidget(this);
        if (m_pWidgetSettings)
        {
            /* Prepare encryption settings widget layout: */
            QGridLayout *m_pLayoutSettings = new QGridLayout(m_pWidgetSettings);
            if (m_pLayoutSettings)
            {
                m_pLayoutSettings->setContentsMargins(0, 0, 0, 0);

                /* Prepare encryption cipher label: */
                m_pLabelCipherType = new QLabel(m_pWidgetSettings);
                if (m_pLabelCipherType)
                {
                    m_pLabelCipherType->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    m_pLayoutSettings->addWidget(m_pLabelCipherType, 0, 0);
                }
                /* Prepare encryption cipher combo: */
                m_pComboCipherType = new QComboBox(m_pWidgetSettings);
                if (m_pComboCipherType)
                {
                    if (m_pLabelCipherType)
                        m_pLabelCipherType->setBuddy(m_pComboCipherType);
                    m_pLayoutSettings->addWidget(m_pComboCipherType, 0, 1);
                }

                /* Prepare encryption password label: */
                m_pLabelPassword1 = new QLabel(m_pWidgetSettings);
                if (m_pLabelPassword1)
                {
                    m_pLabelPassword1->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    m_pLayoutSettings->addWidget(m_pLabelPassword1, 1, 0);
                }
                /* Prepare encryption password editor: */
                m_pEditorPassword1 = new QLineEdit(m_pWidgetSettings);
                if (m_pEditorPassword1)
                {
                    if (m_pLabelPassword1)
                        m_pLabelPassword1->setBuddy(m_pEditorPassword1);
                    m_pEditorPassword1->setEchoMode(QLineEdit::Password);

                    m_pLayoutSettings->addWidget(m_pEditorPassword1, 1, 1);
                }

                /* Prepare encryption confirm password label: */
                m_pLabelPassword2 = new QLabel(m_pWidgetSettings);
                if (m_pLabelPassword2)
                {
                    m_pLabelPassword2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    m_pLayoutSettings->addWidget(m_pLabelPassword2, 2, 0);
                }
                /* Prepare encryption confirm password editor: */
                m_pEditorPassword2 = new QLineEdit(m_pWidgetSettings);
                if (m_pEditorPassword2)
                {
                    if (m_pLabelPassword2)
                        m_pLabelPassword2->setBuddy(m_pEditorPassword2);
                    m_pEditorPassword2->setEchoMode(QLineEdit::Password);

                    m_pLayoutSettings->addWidget(m_pEditorPassword2, 2, 1);
                }
            }

            pLayout->addWidget(m_pWidgetSettings, 1, 1, 1, 2);
        }
    }

    /* Update widget availability: */
    if (m_pCheckboxFeature)
        sltHandleFeatureToggled(m_pCheckboxFeature->isChecked());
}

void UIDiskEncryptionSettingsEditor::prepareConnections()
{
    if (m_pCheckboxFeature)
        connect(m_pCheckboxFeature, &QCheckBox::toggled,
                this, &UIDiskEncryptionSettingsEditor::sltHandleFeatureToggled);
    if (m_pComboCipherType)
        connect(m_pComboCipherType, &QComboBox::currentIndexChanged,
                this, &UIDiskEncryptionSettingsEditor::sigCipherChanged);
    if (m_pEditorPassword1)
        connect(m_pEditorPassword1, &QLineEdit::textEdited,
                this, &UIDiskEncryptionSettingsEditor::sigPasswordChanged);
    if (m_pEditorPassword2)
        connect(m_pEditorPassword2, &QLineEdit::textEdited,
                this, &UIDiskEncryptionSettingsEditor::sigPasswordChanged);
}

void UIDiskEncryptionSettingsEditor::repopulateCombo()
{
    if (m_pComboCipherType)
    {
        /* Clear combo first of all: */
        m_pComboCipherType->clear();

        /// @todo get supported auth types (API not implemented), not hardcoded!
        QVector<UIDiskEncryptionCipherType> cipherTypes =
            QVector<UIDiskEncryptionCipherType>() << UIDiskEncryptionCipherType_Unchanged
                                                  << UIDiskEncryptionCipherType_XTS256
                                                  << UIDiskEncryptionCipherType_XTS128;

        /* Take into account currently cached value: */
        if (!cipherTypes.contains(m_enmCipherType))
            cipherTypes.prepend(m_enmCipherType);

        /* Populate combo finally: */
        foreach (const UIDiskEncryptionCipherType &enmType, cipherTypes)
            m_pComboCipherType->addItem(gpConverter->toString(enmType), QVariant::fromValue(enmType));

        /* Look for proper index to choose: */
        const int iIndex = m_pComboCipherType->findData(QVariant::fromValue(m_enmCipherType));
        if (iIndex != -1)
            m_pComboCipherType->setCurrentIndex(iIndex);
    }
}
