/* $Id: UIUSBSettingsEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBSettingsEditor class implementation.
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
#include "UIUSBControllerEditor.h"
#include "UIUSBSettingsEditor.h"


UIUSBSettingsEditor::UIUSBSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fFeatureEnabled(false)
    , m_pCheckboxFeature(0)
    , m_pWidgetSettings(0)
    , m_pEditorController(0)
    , m_pEditorFilters(0)
{
    prepare();
}

void UIUSBSettingsEditor::setFeatureEnabled(bool fEnabled)
{
    if (m_fFeatureEnabled != fEnabled)
    {
        m_fFeatureEnabled = fEnabled;
        if (m_pCheckboxFeature)
            m_pCheckboxFeature->setChecked(m_fFeatureEnabled);
    }
}

bool UIUSBSettingsEditor::isFeatureEnabled() const
{
    return m_pCheckboxFeature ? m_pCheckboxFeature->isChecked() : m_fFeatureEnabled;
}

void UIUSBSettingsEditor::setFeatureAvailable(bool fAvailable)
{
    if (m_pCheckboxFeature)
        m_pCheckboxFeature->setEnabled(fAvailable);
}

void UIUSBSettingsEditor::setUsbControllerType(KUSBControllerType enmType)
{
    if (m_pEditorController)
        m_pEditorController->setValue(enmType);
}

KUSBControllerType UIUSBSettingsEditor::usbControllerType() const
{
    return m_pEditorController ? m_pEditorController->value() : KUSBControllerType_Max;
}

void UIUSBSettingsEditor::setUsbControllerOptionAvailable(bool fAvailable)
{
    if (m_pEditorController)
        m_pEditorController->setEnabled(fAvailable);
}

void UIUSBSettingsEditor::setUsbFilters(const QList<UIDataUSBFilter> &filters)
{
    if (m_pEditorFilters)
        m_pEditorFilters->setValue(filters);
}

QList<UIDataUSBFilter> UIUSBSettingsEditor::usbFilters() const
{
    return m_pEditorFilters ? m_pEditorFilters->value() : QList<UIDataUSBFilter>();
}

void UIUSBSettingsEditor::setUsbFiltersOptionAvailable(bool fAvailable)
{
    if (m_pEditorFilters)
        m_pEditorFilters->setEnabled(fAvailable);
}

void UIUSBSettingsEditor::sltRetranslateUI()
{
    if (m_pCheckboxFeature)
    {
        m_pCheckboxFeature->setText(tr("Enable &USB Controller"));
        m_pCheckboxFeature->setToolTip(tr("When checked, enables the virtual USB controller of this machine."));
    }
}

void UIUSBSettingsEditor::sltHandleFeatureToggled()
{
    /* Update widget availability: */
    updateFeatureAvailability();
}

void UIUSBSettingsEditor::prepare()
{
    /* Prepare stuff: */
    prepareWidgets();
    prepareConnections();

    /* Update widget availability: */
    updateFeatureAvailability();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIUSBSettingsEditor::prepareWidgets()
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

        /* Prepare settings widget: */
        m_pWidgetSettings = new QWidget(this);
        if (m_pWidgetSettings)
        {
            /* Prepare settings layout: */
            QVBoxLayout *pLayoutSettings = new QVBoxLayout(m_pWidgetSettings);
            if (pLayoutSettings)
            {
                pLayoutSettings->setContentsMargins(0, 0, 0, 0);

                /* Prepare USB controller editor: */
                m_pEditorController = new UIUSBControllerEditor(m_pWidgetSettings);
                if (m_pEditorController)
                {
                    addEditor(m_pEditorController);
                    pLayoutSettings->addWidget(m_pEditorController);
                }

                /* Prepare USB filters editor: */
                m_pEditorFilters = new UIUSBFiltersEditor(m_pWidgetSettings);
                if (m_pEditorFilters)
                {
                    addEditor(m_pEditorFilters);
                    pLayoutSettings->addWidget(m_pEditorFilters);
                }
            }

            pLayout->addWidget(m_pWidgetSettings, 1, 1);
        }
    }
}

void UIUSBSettingsEditor::prepareConnections()
{
    if (m_pCheckboxFeature)
    {
        connect(m_pCheckboxFeature, &QCheckBox::stateChanged,
                this, &UIUSBSettingsEditor::sltHandleFeatureToggled);
        connect(m_pCheckboxFeature, &QCheckBox::stateChanged,
                this, &UIUSBSettingsEditor::sigValueChanged);
    }
    if (m_pEditorController)
        connect(m_pEditorController, &UIUSBControllerEditor::sigValueChanged,
                this, &UIUSBSettingsEditor::sigValueChanged);
    if (m_pEditorFilters)
        connect(m_pEditorFilters, &UIUSBFiltersEditor::sigValueChanged,
                this, &UIUSBSettingsEditor::sigValueChanged);
}

void UIUSBSettingsEditor::updateFeatureAvailability()
{
    m_pWidgetSettings->setEnabled(m_pCheckboxFeature->isChecked());
}
