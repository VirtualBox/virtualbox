/* $Id: UIProxyFeaturesEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProxyFeaturesEditor class implementation.
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
#include <QGridLayout>
#include <QLabel>
#include <QRadioButton>
#include <QRegularExpressionValidator>

/* GUI includes: */
#include "QILineEdit.h"
#include "UIProxyFeaturesEditor.h"


UIProxyFeaturesEditor::UIProxyFeaturesEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_enmProxyMode(KProxyMode_Max)
    , m_pButtonGroup(0)
    , m_pRadioButtonProxyAuto(0)
    , m_pRadioButtonProxyDisabled(0)
    , m_pRadioButtonProxyEnabled(0)
    , m_pWidgetSettings(0)
    , m_pLabelHost(0)
    , m_pEditorHost(0)
{
    prepare();
}

void UIProxyFeaturesEditor::setProxyMode(KProxyMode enmMode)
{
    /* Update cached value and
     * radio-buttons if value has changed: */
    if (m_enmProxyMode != enmMode)
    {
        m_enmProxyMode = enmMode;
        switch (m_enmProxyMode)
        {
            case KProxyMode_System:
                if (m_pRadioButtonProxyAuto)
                    m_pRadioButtonProxyAuto->setChecked(true);
                break;
            case KProxyMode_NoProxy:
                if (m_pRadioButtonProxyDisabled)
                    m_pRadioButtonProxyDisabled->setChecked(true);
                break;
            case KProxyMode_Manual:
                if (m_pRadioButtonProxyEnabled)
                    m_pRadioButtonProxyEnabled->setChecked(true);
                break;
            case KProxyMode_Max:
                break;
        }
    }

    /* Update widgets availability: */
    sltHandleProxyModeChanged();
}

KProxyMode UIProxyFeaturesEditor::proxyMode() const
{
    return   m_pRadioButtonProxyEnabled && m_pRadioButtonProxyEnabled->isChecked()
           ? KProxyMode_Manual
           : m_pRadioButtonProxyDisabled && m_pRadioButtonProxyDisabled->isChecked()
           ? KProxyMode_NoProxy
           : m_pRadioButtonProxyAuto && m_pRadioButtonProxyAuto->isChecked()
           ? KProxyMode_System
           : m_enmProxyMode;
}

void UIProxyFeaturesEditor::setProxyHost(const QString &strHost)
{
    /* Update cached value and
     * line-edit if value has changed: */
    if (m_strProxyHost != strHost)
    {
        m_strProxyHost = strHost;
        if (m_pEditorHost)
            m_pEditorHost->setText(m_strProxyHost);
    }
}

QString UIProxyFeaturesEditor::proxyHost() const
{
    return m_pEditorHost ? m_pEditorHost->text() : m_strProxyHost;
}

void UIProxyFeaturesEditor::sltRetranslateUI()
{
    /* Translate proxy mode editor: */
    if (m_pRadioButtonProxyAuto)
    {
        m_pRadioButtonProxyAuto->setText(tr("&Auto-detect Host Proxy Settings"));
        m_pRadioButtonProxyAuto->setToolTip(tr("When chosen, VirtualBox will try to auto-detect host proxy settings for tasks "
                                               "like downloading Guest Additions from the network or checking for updates."));
    }
    if (m_pRadioButtonProxyDisabled)
    {
        m_pRadioButtonProxyDisabled->setText(tr("&Direct Connection to the Internet"));
        m_pRadioButtonProxyDisabled->setToolTip(tr("When chosen, VirtualBox will use direct Internet connection for tasks like "
                                                   "downloading Guest Additions from the network or checking for updates."));
    }
    if (m_pRadioButtonProxyEnabled)
    {
        m_pRadioButtonProxyEnabled->setText(tr("&Manual Proxy Configuration"));
        m_pRadioButtonProxyEnabled->setToolTip(tr("When chosen, VirtualBox will use the proxy settings supplied for tasks like "
                                                  "downloading Guest Additions from the network or checking for updates."));
    }

    /* Translate proxy host editor: */
    if (m_pLabelHost)
        m_pLabelHost->setText(tr("&URL:"));
    if (m_pEditorHost)
        m_pEditorHost->setToolTip(tr("Holds the proxy URL. "
                                     "The format is: "
                                     "<table cellspacing=0 style='white-space:pre'>"
                                     "<tr><td>[{type}://][{userid}[:{password}]@]{server}[:{port}]</td></tr>"
                                     "<tr><td>http://username:password@proxy.host.com:port</td></tr>"
                                     "</table>"));
}

void UIProxyFeaturesEditor::sltHandleProxyModeChanged()
{
    /* Update widgets availability: */
    m_pWidgetSettings->setEnabled(m_pRadioButtonProxyEnabled->isChecked());

    /* Notify listeners: */
    emit sigProxyModeChanged();
}

void UIProxyFeaturesEditor::prepare()
{
    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare button-group: */
        m_pButtonGroup = new QButtonGroup(this);
        if (m_pButtonGroup)
        {
            /* Prepare 'proxy auto' button: */
            m_pRadioButtonProxyAuto = new QRadioButton(this);
            if (m_pRadioButtonProxyAuto)
            {
                m_pButtonGroup->addButton(m_pRadioButtonProxyAuto);
                pLayout->addWidget(m_pRadioButtonProxyAuto, 0, 0, 1, 2);
            }
            /* Prepare 'proxy disabled' button: */
            m_pRadioButtonProxyDisabled = new QRadioButton(this);
            if (m_pRadioButtonProxyDisabled)
            {
                m_pButtonGroup->addButton(m_pRadioButtonProxyDisabled);
                pLayout->addWidget(m_pRadioButtonProxyDisabled, 1, 0, 1, 2);
            }
            /* Prepare 'proxy enabled' button: */
            m_pRadioButtonProxyEnabled = new QRadioButton(this);
            if (m_pRadioButtonProxyEnabled)
            {
                m_pButtonGroup->addButton(m_pRadioButtonProxyEnabled);
                pLayout->addWidget(m_pRadioButtonProxyEnabled, 2, 0, 1, 2);
            }
        }

        /* Prepare 20-px shifting spacer: */
        QSpacerItem *pSpacerItem = new QSpacerItem(20, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (pSpacerItem)
            pLayout->addItem(pSpacerItem, 3, 0);

        /* Prepare settings widget: */
        m_pWidgetSettings = new QWidget(this);
        if (m_pWidgetSettings)
        {
            /* Prepare settings layout widget: */
            QHBoxLayout *pLayoutSettings = new QHBoxLayout(m_pWidgetSettings);
            if (pLayoutSettings)
            {
                pLayoutSettings->setContentsMargins(0, 0, 0, 0);

                /* Prepare host label: */
                m_pLabelHost = new QLabel(m_pWidgetSettings);
                if (m_pLabelHost)
                {
                    m_pLabelHost->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutSettings->addWidget(m_pLabelHost);
                }
                /* Prepare host editor: */
                m_pEditorHost = new QILineEdit(m_pWidgetSettings);
                if (m_pEditorHost)
                {
                    if (m_pLabelHost)
                        m_pLabelHost->setBuddy(m_pEditorHost);
                    m_pEditorHost->setValidator(new QRegularExpressionValidator(QRegularExpression("\\S+"), m_pEditorHost));

                    pLayoutSettings->addWidget(m_pEditorHost);
                }
            }

            pLayout->addWidget(m_pWidgetSettings, 3, 1);
        }
    }

    /* Prepare connections: */
    connect(m_pButtonGroup, &QButtonGroup::buttonClicked,
            this, &UIProxyFeaturesEditor::sltHandleProxyModeChanged);
    connect(m_pEditorHost, &QILineEdit::textEdited,
            this, &UIProxyFeaturesEditor::sigProxyHostChanged);

    /* Apply language settings: */
    sltRetranslateUI();
}
