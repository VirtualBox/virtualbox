/* $Id: UIUpdateSettingsEditor.cpp 106831 2024-11-01 11:33:26Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUpdateSettingsEditor class implementation.
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
#include <QGridLayout>
#include <QLabel>
#include <QRadioButton>

/* GUI includes: */
#include "UIUpdateSettingsEditor.h"


UIUpdateSettingsEditor::UIUpdateSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_pRadioButtonGroup(0)
    , m_pCheckBox(0)
    , m_pWidgetUpdateSettings(0)
    , m_pLabelUpdatePeriod(0)
    , m_pComboUpdatePeriod(0)
    , m_pLabelUpdateDate(0)
    , m_pFieldUpdateDate(0)
{
    prepare();
}

void UIUpdateSettingsEditor::setValue(const VBoxUpdateData &guiValue)
{
    /* Update cached value and fetch it: */
    if (m_guiValue != guiValue)
    {
        m_guiValue = guiValue;
        fetchValue();
    }
}

VBoxUpdateData UIUpdateSettingsEditor::value() const
{
    return VBoxUpdateData(isCheckEnabled(), updatePeriod(), updateChannel());
}

void UIUpdateSettingsEditor::sltRetranslateUI()
{
    /* Translate branch widgets: */
    if (m_mapRadioButtons.value(KUpdateChannel_Stable))
    {
        m_mapRadioButtons.value(KUpdateChannel_Stable)->setText(tr("&Stable Release Versions"));
        m_mapRadioButtons.value(KUpdateChannel_Stable)->setToolTip(tr("When chosen, you will be notified "
                                                                      "about stable updates to VirtualBox."));
    }
    if (m_mapRadioButtons.value(KUpdateChannel_All))
    {
        m_mapRadioButtons.value(KUpdateChannel_All)->setText(tr("&All New Releases"));
        m_mapRadioButtons.value(KUpdateChannel_All)->setToolTip(tr("When chosen, you will be notified "
                                                                   "about all new VirtualBox releases."));
    }
    if (m_mapRadioButtons.value(KUpdateChannel_WithBetas))
    {
        m_mapRadioButtons.value(KUpdateChannel_WithBetas)->setText(tr("All New Releases and &Pre-Releases"));
        m_mapRadioButtons.value(KUpdateChannel_WithBetas)->setToolTip(tr("When chosen, you will be notified "
                                                                         "about all new VirtualBox releases and "
                                                                         "pre-release versions of VirtualBox."));
    }
    if (m_mapRadioButtons.value(KUpdateChannel_WithTesting))
    {
        m_mapRadioButtons.value(KUpdateChannel_WithTesting)->setText(tr("All New Releases, &Pre-Releases and Testing Builds"));
        m_mapRadioButtons.value(KUpdateChannel_WithTesting)->setToolTip(tr("When chosen, you will be notified "
                                                                           "about all new VirtualBox releases, "
                                                                           "pre-release versions and testing builds of "
                                                                           "VirtualBox."));
    }

    /* Translate check-box: */
    if (m_pCheckBox)
    {
        m_pCheckBox->setToolTip(tr("When checked, the application will periodically connect to the VirtualBox "
                                   "website and check whether a new VirtualBox version is available."));
        m_pCheckBox->setText(tr("&Check for Updates"));
    }

    /* Translate period widgets: */
    if (m_pLabelUpdatePeriod)
        m_pLabelUpdatePeriod->setText(tr("&Once per:"));
    if (m_pComboUpdatePeriod)
    {
        m_pComboUpdatePeriod->setToolTip(tr("Selects how often the new version check should be performed."));
        const int iCurrenIndex = m_pComboUpdatePeriod->currentIndex();
        m_pComboUpdatePeriod->clear();
        VBoxUpdateData::populate();
        m_pComboUpdatePeriod->insertItems(0, VBoxUpdateData::list());
        m_pComboUpdatePeriod->setCurrentIndex(iCurrenIndex == -1 ? 0 : iCurrenIndex);
    }
    if (m_pLabelUpdateDate)
        m_pLabelUpdateDate->setText(tr("Next Check:"));
}

void UIUpdateSettingsEditor::handleFilterChange()
{
    /* This stuff is for Expert mode only: */
    m_pWidgetUpdateSettings->setVisible(m_fInExpertMode);
}

void UIUpdateSettingsEditor::sltHandleUpdateToggle(bool fEnabled)
{
    /* Update activity status: */
    if (m_pWidgetUpdateSettings)
        m_pWidgetUpdateSettings->setEnabled(fEnabled);

    /* Update time of next check: */
    sltHandleUpdatePeriodChange();

    /* Choose stable branch if update enabled but branch isn't chosen: */
    if (   fEnabled
        && m_pRadioButtonGroup
        && !m_pRadioButtonGroup->checkedButton()
        && m_mapRadioButtons.value(KUpdateChannel_Stable))
        m_mapRadioButtons.value(KUpdateChannel_Stable)->setChecked(true);
}

void UIUpdateSettingsEditor::sltHandleUpdatePeriodChange()
{
    if (m_pFieldUpdateDate)
        m_pFieldUpdateDate->setText(VBoxUpdateData(isCheckEnabled(), updatePeriod(), updateChannel()).dateToString());
}

void UIUpdateSettingsEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIUpdateSettingsEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);
        pLayout->setColumnStretch(1, 1);

        /* Prepare radio-button group: */
        m_pRadioButtonGroup = new QButtonGroup(m_pWidgetUpdateSettings);
        if (m_pRadioButtonGroup)
        {
            /* Prepare 'update to "stable"' radio-button: */
            m_mapRadioButtons[KUpdateChannel_Stable] = new QRadioButton(m_pWidgetUpdateSettings);
            if (m_mapRadioButtons.value(KUpdateChannel_Stable))
            {
                m_mapRadioButtons.value(KUpdateChannel_Stable)->setVisible(false);
                m_pRadioButtonGroup->addButton(m_mapRadioButtons.value(KUpdateChannel_Stable));
                pLayout->addWidget(m_mapRadioButtons.value(KUpdateChannel_Stable), 0, 0, 1, 2);
            }
            /* Prepare 'update to "all release"' radio-button: */
            m_mapRadioButtons[KUpdateChannel_All] = new QRadioButton(m_pWidgetUpdateSettings);
            if (m_mapRadioButtons.value(KUpdateChannel_All))
            {
                m_mapRadioButtons.value(KUpdateChannel_All)->setVisible(false);
                m_pRadioButtonGroup->addButton(m_mapRadioButtons.value(KUpdateChannel_All));
                pLayout->addWidget(m_mapRadioButtons.value(KUpdateChannel_All), 1, 0, 1, 2);
            }
            /* Prepare 'update to "with betas"' radio-button: */
            m_mapRadioButtons[KUpdateChannel_WithBetas] = new QRadioButton(m_pWidgetUpdateSettings);
            if (m_mapRadioButtons.value(KUpdateChannel_WithBetas))
            {
                m_mapRadioButtons.value(KUpdateChannel_WithBetas)->setVisible(false);
                m_pRadioButtonGroup->addButton(m_mapRadioButtons.value(KUpdateChannel_WithBetas));
                pLayout->addWidget(m_mapRadioButtons.value(KUpdateChannel_WithBetas), 2, 0, 1, 2);
            }
            /* Prepare 'update to "with testing"' radio-button: */
            m_mapRadioButtons[KUpdateChannel_WithTesting] = new QRadioButton(m_pWidgetUpdateSettings);
            if (m_mapRadioButtons.value(KUpdateChannel_WithTesting))
            {
                m_mapRadioButtons.value(KUpdateChannel_WithTesting)->setVisible(false);
                m_pRadioButtonGroup->addButton(m_mapRadioButtons.value(KUpdateChannel_WithTesting));
                pLayout->addWidget(m_mapRadioButtons.value(KUpdateChannel_WithTesting), 3, 0, 1, 2);
            }
        }

        /* Prepare update check-box: */
        m_pCheckBox = new QCheckBox(this);
        if (m_pCheckBox)
            pLayout->addWidget(m_pCheckBox, 4, 0, 1, 2);

        /* Prepare 20-px shifting spacer: */
        QSpacerItem *pSpacerItem = new QSpacerItem(20, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (pSpacerItem)
            pLayout->addItem(pSpacerItem, 5, 0);

        /* Prepare update settings widget: */
        m_pWidgetUpdateSettings = new QWidget(this);
        if (m_pWidgetUpdateSettings)
        {
            /* Prepare update settings widget layout: */
            QGridLayout *pLayoutUpdateSettings = new QGridLayout(m_pWidgetUpdateSettings);
            if (pLayoutUpdateSettings)
            {
                pLayoutUpdateSettings->setContentsMargins(0, 0, 0, 0);
                pLayoutUpdateSettings->setColumnStretch(2, 1);
                pLayoutUpdateSettings->setRowStretch(5, 1);

                /* Prepare update period label: */
                m_pLabelUpdatePeriod = new QLabel(m_pWidgetUpdateSettings);
                if (m_pLabelUpdatePeriod)
                {
                    m_pLabelUpdatePeriod->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutUpdateSettings->addWidget(m_pLabelUpdatePeriod, 0, 0);
                }
                /* Prepare update period combo: */
                m_pComboUpdatePeriod = new QComboBox(m_pWidgetUpdateSettings);
                if (m_pComboUpdatePeriod)
                {
                    if (m_pLabelUpdatePeriod)
                        m_pLabelUpdatePeriod->setBuddy(m_pComboUpdatePeriod);
                    m_pComboUpdatePeriod->setSizeAdjustPolicy(QComboBox::AdjustToContents);
                    m_pComboUpdatePeriod->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));

                    pLayoutUpdateSettings->addWidget(m_pComboUpdatePeriod, 0, 1);
                }

                /* Prepare update date label: */
                m_pLabelUpdateDate = new QLabel(m_pWidgetUpdateSettings);
                if (m_pLabelUpdateDate)
                {
                    m_pLabelUpdateDate->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutUpdateSettings->addWidget(m_pLabelUpdateDate, 1, 0);
                }
                /* Prepare update date field: */
                m_pFieldUpdateDate = new QLabel(m_pWidgetUpdateSettings);
                if (m_pFieldUpdateDate)
                {
                    m_pLabelUpdateDate->setBuddy(m_pFieldUpdateDate);
                    pLayoutUpdateSettings->addWidget(m_pFieldUpdateDate, 1, 1);
                }
            }

            pLayout->addWidget(m_pWidgetUpdateSettings, 5, 1);
        }
    }
}

void UIUpdateSettingsEditor::prepareConnections()
{
    if (m_pCheckBox)
        connect(m_pCheckBox, &QCheckBox::toggled, this, &UIUpdateSettingsEditor::sltHandleUpdateToggle);
    if (m_pComboUpdatePeriod)
        connect(m_pComboUpdatePeriod, &QComboBox::activated,
                this, &UIUpdateSettingsEditor::sltHandleUpdatePeriodChange);
}

bool UIUpdateSettingsEditor::isCheckEnabled() const
{
    return m_pCheckBox ? m_pCheckBox->isChecked() : m_guiValue.isCheckEnabled();
}

UpdatePeriodType UIUpdateSettingsEditor::updatePeriod() const
{
    return m_pComboUpdatePeriod ? (UpdatePeriodType)m_pComboUpdatePeriod->currentIndex() : m_guiValue.updatePeriod();
}

KUpdateChannel UIUpdateSettingsEditor::updateChannel() const
{
    QAbstractButton *pCheckedButton = m_pRadioButtonGroup ? m_pRadioButtonGroup->checkedButton() : 0;
    return m_mapRadioButtons.key(pCheckedButton, m_guiValue.updateChannel());
}

void UIUpdateSettingsEditor::fetchValue()
{
    if (m_pCheckBox)
    {
        m_pCheckBox->setChecked(m_guiValue.isCheckEnabled());

        foreach (KUpdateChannel enmChannel, m_mapRadioButtons.keys())
            if (m_mapRadioButtons.value(enmChannel))
                m_mapRadioButtons.value(enmChannel)
                    ->setVisible(   m_guiValue.updateChannel() == enmChannel
                                 || m_guiValue.supportedUpdateChannels().contains(enmChannel));

        if (m_pCheckBox->isChecked())
        {
            if (m_pComboUpdatePeriod)
                m_pComboUpdatePeriod->setCurrentIndex(m_guiValue.updatePeriod());
        }

        if (m_mapRadioButtons.value(m_guiValue.updateChannel()))
            m_mapRadioButtons.value(m_guiValue.updateChannel())->setChecked(true);

        sltHandleUpdateToggle(m_pCheckBox->isChecked());
    }
}
