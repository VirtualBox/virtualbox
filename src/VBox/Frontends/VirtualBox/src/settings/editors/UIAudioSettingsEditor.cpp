/* $Id: UIAudioSettingsEditor.cpp 168997 2025-05-26 16:35:29Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAudioSettingsEditor class implementation.
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
#include "UIAudioControllerEditor.h"
#include "UIAudioFeaturesEditor.h"
#include "UIAudioHostDriverEditor.h"
#include "UIAudioSettingsEditor.h"


UIAudioSettingsEditor::UIAudioSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fFeatureEnabled(false)
    , m_pCheckboxFeature(0)
    , m_pWidgetSettings(0)
    , m_pEditorAudioHostDriver(0)
    , m_pEditorAudioController(0)
    , m_pEditorAudioFeatures(0)
{
    prepare();
}

void UIAudioSettingsEditor::setFeatureEnabled(bool fEnabled)
{
    if (m_fFeatureEnabled != fEnabled)
    {
        m_fFeatureEnabled = fEnabled;
        if (m_pCheckboxFeature)
            m_pCheckboxFeature->setChecked(m_fFeatureEnabled);
    }
}

bool UIAudioSettingsEditor::isFeatureEnabled() const
{
    return m_pCheckboxFeature ? m_pCheckboxFeature->isChecked() : m_fFeatureEnabled;
}

void UIAudioSettingsEditor::setFeatureAvailable(bool fAvailable)
{
    if (m_pCheckboxFeature)
        m_pCheckboxFeature->setEnabled(fAvailable);
}

void UIAudioSettingsEditor::setHostDriverType(KAudioDriverType enmType)
{
    if (m_pEditorAudioHostDriver)
        m_pEditorAudioHostDriver->setValue(enmType);
}

KAudioDriverType UIAudioSettingsEditor::hostDriverType() const
{
    return m_pEditorAudioHostDriver ? m_pEditorAudioHostDriver->value() : KAudioDriverType_Max;
}

void UIAudioSettingsEditor::setHostDriverOptionAvailable(bool fAvailable)
{
    if (m_pEditorAudioHostDriver)
        m_pEditorAudioHostDriver->setEnabled(fAvailable);
}

void UIAudioSettingsEditor::setControllerType(KAudioControllerType enmType)
{
    if (m_pEditorAudioController)
        m_pEditorAudioController->setValue(enmType);
}

KAudioControllerType UIAudioSettingsEditor::controllerType() const
{
    return m_pEditorAudioController ? m_pEditorAudioController->value() : KAudioControllerType_Max;
}

void UIAudioSettingsEditor::setControllerOptionAvailable(bool fAvailable)
{
    if (m_pEditorAudioController)
        m_pEditorAudioController->setEnabled(fAvailable);
}

void UIAudioSettingsEditor::setEnableOutput(bool fConnected)
{
    if (m_pEditorAudioFeatures)
        m_pEditorAudioFeatures->setEnableOutput(fConnected);
}

bool UIAudioSettingsEditor::outputEnabled() const
{
    return m_pEditorAudioFeatures ? m_pEditorAudioFeatures->outputEnabled() : false;
}

void UIAudioSettingsEditor::setEnableInput(bool fConnected)
{
    if (m_pEditorAudioFeatures)
        m_pEditorAudioFeatures->setEnableInput(fConnected);
}

bool UIAudioSettingsEditor::inputEnabled() const
{
    return m_pEditorAudioFeatures ? m_pEditorAudioFeatures->inputEnabled() : false;
}

void UIAudioSettingsEditor::setFeatureOptionsAvailable(bool fAvailable)
{
    if (m_pEditorAudioFeatures)
        m_pEditorAudioFeatures->setEnabled(fAvailable);
}

void UIAudioSettingsEditor::sltRetranslateUI()
{
    if (m_pCheckboxFeature)
    {
        m_pCheckboxFeature->setText(tr("Enable &Audio"));
        m_pCheckboxFeature->setToolTip(tr("A virtual PCI audio card will be plugged into the virtual machine "
                                          "and will communicate with the host audio system using the specified driver"));
    }

    updateMinimumLayoutHint();
}

void UIAudioSettingsEditor::handleFilterChange()
{
    updateMinimumLayoutHint();
}

void UIAudioSettingsEditor::sltHandleFeatureToggled()
{
    /* Update widget availability: */
    updateFeatureAvailability();
}

void UIAudioSettingsEditor::prepare()
{
    /* Prepare stuff: */
    prepareWidgets();
    prepareConnections();

    /* Update widget availability: */
    updateFeatureAvailability();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIAudioSettingsEditor::prepareWidgets()
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
            QVBoxLayout *pLayoutAudioSettings = new QVBoxLayout(m_pWidgetSettings);
            if (pLayoutAudioSettings)
            {
                pLayoutAudioSettings->setContentsMargins(0, 0, 0, 0);

                /* Prepare host driver editor: */
                m_pEditorAudioHostDriver = new UIAudioHostDriverEditor(m_pWidgetSettings);
                if (m_pEditorAudioHostDriver)
                {
                    addEditor(m_pEditorAudioHostDriver);
                    pLayoutAudioSettings->addWidget(m_pEditorAudioHostDriver);
                }

                /* Prepare controller editor: */
                m_pEditorAudioController = new UIAudioControllerEditor(m_pWidgetSettings);
                if (m_pEditorAudioController)
                {
                    addEditor(m_pEditorAudioController);
                    pLayoutAudioSettings->addWidget(m_pEditorAudioController);
                }

                /* Prepare features editor: */
                m_pEditorAudioFeatures = new UIAudioFeaturesEditor(m_pWidgetSettings);
                if (m_pEditorAudioFeatures)
                {
                    addEditor(m_pEditorAudioFeatures);
                    pLayoutAudioSettings->addWidget(m_pEditorAudioFeatures);
                }
            }

            pLayout->addWidget(m_pWidgetSettings, 1, 1);
        }
    }
}

void UIAudioSettingsEditor::prepareConnections()
{
    if (m_pCheckboxFeature)
        connect(m_pCheckboxFeature, &QCheckBox::stateChanged,
                this, &UIAudioSettingsEditor::sltHandleFeatureToggled);
}

void UIAudioSettingsEditor::updateFeatureAvailability()
{
    m_pWidgetSettings->setEnabled(m_pCheckboxFeature->isChecked());
}

void UIAudioSettingsEditor::updateMinimumLayoutHint()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    if (m_pEditorAudioHostDriver && !m_pEditorAudioHostDriver->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorAudioHostDriver->minimumLabelHorizontalHint());
    if (m_pEditorAudioController && !m_pEditorAudioController->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorAudioController->minimumLabelHorizontalHint());
    if (m_pEditorAudioFeatures && !m_pEditorAudioFeatures->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorAudioFeatures->minimumLabelHorizontalHint());
    if (m_pEditorAudioHostDriver)
        m_pEditorAudioHostDriver->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorAudioController)
        m_pEditorAudioController->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorAudioFeatures)
        m_pEditorAudioFeatures->setMinimumLayoutIndent(iMinimumLayoutHint);
}
