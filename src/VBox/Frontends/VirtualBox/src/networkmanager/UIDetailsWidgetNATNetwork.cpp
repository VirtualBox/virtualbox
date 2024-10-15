/* $Id: UIDetailsWidgetNATNetwork.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsWidgetNATNetwork class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStyleOption>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QILineEdit.h"
#include "QITabWidget.h"
#include "UIIconPool.h"
#include "UIDetailsWidgetNATNetwork.h"
#include "UIMessageCenter.h"
#include "UINetworkManager.h"
#include "UINetworkManagerUtils.h"
#include "UINotificationCenter.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/cidr.h"


UIDetailsWidgetNATNetwork::UIDetailsWidgetNATNetwork(EmbedTo enmEmbedding, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pTabWidget(0)
    , m_pLabelNetworkName(0)
    , m_pEditorNetworkName(0)
    , m_pLabelNetworkIPv4Prefix(0)
    , m_pEditorNetworkIPv4Prefix(0)
    , m_pCheckboxSupportsDHCP(0)
    , m_pCheckboxIPv6(0)
    , m_pLabelNetworkIPv6Prefix(0)
    , m_pEditorNetworkIPv6Prefix(0)
    , m_pCheckboxAdvertiseDefaultIPv6Route(0)
    , m_pButtonBoxOptions(0)
    , m_pTabWidgetForwarding(0)
    , m_pForwardingTableIPv4(0)
    , m_pForwardingTableIPv6(0)
    , m_pButtonBoxForwarding(0)
    , m_fHoldPosition(false)
{
    prepare();
}

void UIDetailsWidgetNATNetwork::setData(const UIDataNATNetwork &data,
                                        const QStringList &busyNames /* = QStringList() */,
                                        bool fHoldPosition /* = false */)
{
    /* Cache old/new data: */
    m_oldData = data;
    m_newData = m_oldData;
    m_busyNames = busyNames;
    m_fHoldPosition = fHoldPosition;

    /* Load 'Options' data: */
    loadDataForOptions();
    /* Load 'Forwarding' data: */
    loadDataForForwarding();
}

bool UIDetailsWidgetNATNetwork::revalidate() const
{
    /* Make sure network name isn't empty: */
    if (m_newData.m_strName.isEmpty())
    {
        UINotificationMessage::warnAboutNoNameSpecified(m_oldData.m_strName);
        return false;
    }
    else
    {
        /* Make sure item names are unique: */
        if (m_busyNames.contains(m_newData.m_strName))
        {
            UINotificationMessage::warnAboutNameAlreadyBusy(m_newData.m_strName);
            return false;
        }
    }

    /* Make sure IPv4 prefix isn't empty: */
    if (m_newData.m_strPrefixIPv4.isEmpty())
    {
        UINotificationMessage::warnAboutNoIPv4PrefixSpecified(m_newData.m_strName);
        return false;
    }
    /* Make sure IPv6 prefix isn't empty if IPv6 is supported: */
    if (m_newData.m_fSupportsIPv6 && m_newData.m_strPrefixIPv6.isEmpty())
    {
        UINotificationMessage::warnAboutNoIPv6PrefixSpecified(m_newData.m_strName);
        return false;
    }

    /* Validate 'Forwarding' tab content: */
    return m_pForwardingTableIPv4->validate() && m_pForwardingTableIPv6->validate();
}

void UIDetailsWidgetNATNetwork::updateButtonStates()
{
//    if (m_oldData != m_newData)
//        printf("Network: %s, %s, %s, %d, %d, %d\n",
//               m_newData.m_strName.toUtf8().constData(),
//               m_newData.m_strPrefixIPv4.toUtf8().constData(),
//               m_newData.m_strPrefixIPv6.toUtf8().constData(),
//               m_newData.m_fSupportsDHCP,
//               m_newData.m_fSupportsIPv6,
//               m_newData.m_fAdvertiseDefaultIPv6Route);

    /* Update 'Apply' / 'Reset' button states: */
    if (m_pButtonBoxOptions)
    {
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setEnabled(m_oldData != m_newData);
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setEnabled(m_oldData != m_newData);
    }
    if (m_pButtonBoxForwarding)
    {
        m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel)->setEnabled(m_oldData != m_newData);
        m_pButtonBoxForwarding->button(QDialogButtonBox::Ok)->setEnabled(m_oldData != m_newData);
    }

    /* Notify listeners as well: */
    emit sigDataChanged(m_oldData != m_newData);
}

void UIDetailsWidgetNATNetwork::sltRetranslateUI()
{
    /* Translate tab-widget: */
    if (m_pTabWidget)
    {
        m_pTabWidget->setTabText(0, UINetworkManager::tr("&General Options"));
        m_pTabWidget->setTabText(1, UINetworkManager::tr("&Port Forwarding"));
    }

    if (m_pLabelNetworkName)
        m_pLabelNetworkName->setText(UINetworkManager::tr("N&ame:"));
    if (m_pEditorNetworkName)
        m_pEditorNetworkName->setToolTip(UINetworkManager::tr("Holds the name for this network."));
    if (m_pLabelNetworkIPv4Prefix)
        m_pLabelNetworkIPv4Prefix->setText(UINetworkManager::tr("IPv&4 Prefix:"));
    if (m_pEditorNetworkIPv4Prefix)
        m_pEditorNetworkIPv4Prefix->setToolTip(UINetworkManager::tr("Holds the IPv4 prefix for this network."));
    if (m_pLabelNetworkIPv6Prefix)
        m_pLabelNetworkIPv6Prefix->setText(UINetworkManager::tr("IPv&6 Prefix:"));
    if (m_pEditorNetworkIPv6Prefix)
        m_pEditorNetworkIPv6Prefix->setToolTip(UINetworkManager::tr("Holds the IPv6 prefix for this network."));
    if (m_pCheckboxSupportsDHCP)
    {
        m_pCheckboxSupportsDHCP->setText(UINetworkManager::tr("Enable &DHCP"));
        m_pCheckboxSupportsDHCP->setToolTip(UINetworkManager::tr("When checked, this network will support DHCP."));
    }
    if (m_pCheckboxIPv6)
    {
        m_pCheckboxIPv6->setText(UINetworkManager::tr("&Enable IPv6"));
        m_pCheckboxIPv6->setToolTip(UINetworkManager::tr("When checked, this network will support IPv6."));
    }
    if (m_pCheckboxAdvertiseDefaultIPv6Route)
    {
        m_pCheckboxAdvertiseDefaultIPv6Route->setText(UINetworkManager::tr("Advertise Default IPv6 &Route"));
        m_pCheckboxAdvertiseDefaultIPv6Route->setToolTip(UINetworkManager::tr("When checked, this network will be advertised "
                                                                              "as the default IPv6 route."));
    }
    if (m_pButtonBoxOptions)
    {
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setText(UINetworkManager::tr("Reset"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setText(UINetworkManager::tr("Apply"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setShortcut(QString("Ctrl+Return"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setStatusTip(UINetworkManager::tr("Reset changes in current "
                                                                                                 "interface details"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setStatusTip(UINetworkManager::tr("Apply changes in current "
                                                                                             "interface details"));
        m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->
            setToolTip(UINetworkManager::tr("Reset Changes (%1)").arg(m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->shortcut().toString()));
        m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->
            setToolTip(UINetworkManager::tr("Apply Changes (%1)").arg(m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->shortcut().toString()));
    }

    /* Translate 'Forwarding' tab content: */
    if (m_pTabWidgetForwarding)
    {
        m_pTabWidgetForwarding->setTabText(0, UINetworkManager::tr("IPv&4"));
        m_pTabWidgetForwarding->setTabText(1, UINetworkManager::tr("IPv&6"));
    }
    if (m_pButtonBoxForwarding)
    {
        m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel)->setText(UINetworkManager::tr("Reset"));
        m_pButtonBoxForwarding->button(QDialogButtonBox::Ok)->setText(UINetworkManager::tr("Apply"));
        m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
        m_pButtonBoxForwarding->button(QDialogButtonBox::Ok)->setShortcut(QString("Ctrl+Return"));
        m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel)->setStatusTip(UINetworkManager::tr("Reset changes in current "
                                                                                                    "interface details"));
        m_pButtonBoxForwarding->button(QDialogButtonBox::Ok)->setStatusTip(UINetworkManager::tr("Apply changes in current "
                                                                                                "interface details"));
        m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel)->
            setToolTip(UINetworkManager::tr("Reset Changes (%1)").arg(m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel)->shortcut().toString()));
        m_pButtonBoxForwarding->button(QDialogButtonBox::Ok)->
            setToolTip(UINetworkManager::tr("Apply Changes (%1)").arg(m_pButtonBoxForwarding->button(QDialogButtonBox::Ok)->shortcut().toString()));
    }
}

void UIDetailsWidgetNATNetwork::sltNetworkNameChanged(const QString &strText)
{
    m_newData.m_strName = strText;
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltNetworkIPv4PrefixChanged(const QString &strText)
{
    m_newData.m_strPrefixIPv4 = strText;
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltNetworkIPv6PrefixChanged(const QString &strText)
{
    m_newData.m_strPrefixIPv6 = strText;
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltSupportsDHCPChanged(bool fChecked)
{
    m_newData.m_fSupportsDHCP = fChecked;
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltSupportsIPv6Changed(bool fChecked)
{
    m_newData.m_fSupportsIPv6 = fChecked;
    loadDataForOptions();
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltAdvertiseDefaultIPv6RouteChanged(bool fChecked)
{
    m_newData.m_fAdvertiseDefaultIPv6Route = fChecked;
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltForwardingRulesIPv4Changed()
{
    m_newData.m_rules4 = m_pForwardingTableIPv4->rules();
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltForwardingRulesIPv6Changed()
{
    m_newData.m_rules6 = m_pForwardingTableIPv6->rules();
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::sltHandleButtonBoxClick(QAbstractButton *pButton)
{
    /* Make sure button-boxes exist: */
    if (!m_pButtonBoxOptions || !m_pButtonBoxForwarding)
        return;

    /* Disable buttons first of all: */
    m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)->setEnabled(false);
    m_pButtonBoxOptions->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel)->setEnabled(false);
    m_pButtonBoxForwarding->button(QDialogButtonBox::Ok)->setEnabled(false);

    /* Compare with known buttons: */
    if (   pButton == m_pButtonBoxOptions->button(QDialogButtonBox::Cancel)
        || pButton == m_pButtonBoxForwarding->button(QDialogButtonBox::Cancel))
        emit sigDataChangeRejected();
    else
    if (   pButton == m_pButtonBoxOptions->button(QDialogButtonBox::Ok)
        || pButton == m_pButtonBoxForwarding->button(QDialogButtonBox::Ok))
        emit sigDataChangeAccepted();
}

void UIDetailsWidgetNATNetwork::prepare()
{
    /* Prepare this: */
    prepareThis();

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDetailsWidgetNATNetwork::sltRetranslateUI);

    /* Update button states finally: */
    updateButtonStates();
}

void UIDetailsWidgetNATNetwork::prepareThis()
{
    /* Create layout: */
    new QVBoxLayout(this);
    if (layout())
    {
        /* Configure layout: */
        layout()->setContentsMargins(0, 0, 0, 0);

        /* Prepare tab-widget: */
        prepareTabWidget();
    }
}

void UIDetailsWidgetNATNetwork::prepareTabWidget()
{
    /* Create tab-widget: */
    m_pTabWidget = new QITabWidget(this);
    if (m_pTabWidget)
    {
        /* Prepare 'Options' tab: */
        prepareTabOptions();
        /* Prepare 'Forwarding' tab: */
        prepareTabForwarding();

        /* Add into layout: */
        layout()->addWidget(m_pTabWidget);
    }
}

void UIDetailsWidgetNATNetwork::prepareTabOptions()
{
    /* Prepare 'Options' tab: */
    QWidget *pTabOptions = new QWidget(m_pTabWidget);
    if (pTabOptions)
    {
        /* Prepare 'Options' layout: */
        QGridLayout *pLayoutOptions = new QGridLayout(pTabOptions);
        if (pLayoutOptions)
        {
            pLayoutOptions->setColumnStretch(0, 0);
            pLayoutOptions->setColumnStretch(1, 0);
            pLayoutOptions->setColumnStretch(2, 1);
            pLayoutOptions->setColumnStretch(3, 1);
#ifdef VBOX_WS_MAC
            pLayoutOptions->setSpacing(10);
            pLayoutOptions->setContentsMargins(10, 10, 10, 10);
#endif

            /* Prepare network name label: */
            m_pLabelNetworkName = new QLabel(pTabOptions);
            if (m_pLabelNetworkName)
            {
                m_pLabelNetworkName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                pLayoutOptions->addWidget(m_pLabelNetworkName, 0, 0, 1, 2);
            }
            /* Prepare network name editor: */
            m_pEditorNetworkName = new QLineEdit(pTabOptions);
            if (m_pEditorNetworkName)
            {
                if (m_pLabelNetworkName)
                    m_pLabelNetworkName->setBuddy(m_pEditorNetworkName);
                connect(m_pEditorNetworkName, &QLineEdit::textEdited,
                        this, &UIDetailsWidgetNATNetwork::sltNetworkNameChanged);

                pLayoutOptions->addWidget(m_pEditorNetworkName, 0, 2, 1, 2);
            }

            /* Prepare network IPv4 prefix label: */
            m_pLabelNetworkIPv4Prefix = new QLabel(pTabOptions);
            if (m_pLabelNetworkIPv4Prefix)
            {
                m_pLabelNetworkIPv4Prefix->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                pLayoutOptions->addWidget(m_pLabelNetworkIPv4Prefix, 1, 0, 1, 2);
            }
            /* Prepare network IPv4 prefix editor: */
            m_pEditorNetworkIPv4Prefix = new QLineEdit(pTabOptions);
            if (m_pEditorNetworkIPv4Prefix)
            {
                if (m_pLabelNetworkIPv4Prefix)
                    m_pLabelNetworkIPv4Prefix->setBuddy(m_pEditorNetworkIPv4Prefix);
                connect(m_pEditorNetworkIPv4Prefix, &QLineEdit::textEdited,
                        this, &UIDetailsWidgetNATNetwork::sltNetworkIPv4PrefixChanged);

                pLayoutOptions->addWidget(m_pEditorNetworkIPv4Prefix, 1, 2, 1, 2);
            }
            /* Prepare 'supports DHCP' check-box: */
            m_pCheckboxSupportsDHCP = new QCheckBox(pTabOptions);
            if (m_pCheckboxSupportsDHCP)
            {
                connect(m_pCheckboxSupportsDHCP, &QCheckBox::toggled,
                        this, &UIDetailsWidgetNATNetwork::sltSupportsDHCPChanged);
                pLayoutOptions->addWidget(m_pCheckboxSupportsDHCP, 2, 2);
            }

            /* Prepare IPv6 check-box: */
            m_pCheckboxIPv6 = new QCheckBox(pTabOptions);
            if (m_pCheckboxIPv6)
            {
                connect(m_pCheckboxIPv6, &QCheckBox::toggled,
                        this, &UIDetailsWidgetNATNetwork::sltSupportsIPv6Changed);
                pLayoutOptions->addWidget(m_pCheckboxIPv6, 3, 0, 1, 3);
            }
            /* Prepare shifting spacer: */
            QSpacerItem *pSpacer = new QSpacerItem(20, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
            if (pSpacer)
                pLayoutOptions->addItem(pSpacer, 4, 0);
            /* Prepare network IPv6 prefix label: */
            m_pLabelNetworkIPv6Prefix = new QLabel(pTabOptions);
            if (m_pLabelNetworkIPv6Prefix)
            {
                m_pLabelNetworkIPv6Prefix->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                pLayoutOptions->addWidget(m_pLabelNetworkIPv6Prefix, 4, 1);
            }
            /* Prepare network IPv6 prefix editor: */
            m_pEditorNetworkIPv6Prefix = new QLineEdit(pTabOptions);
            if (m_pEditorNetworkIPv6Prefix)
            {
                if (m_pLabelNetworkIPv6Prefix)
                    m_pLabelNetworkIPv6Prefix->setBuddy(m_pEditorNetworkIPv6Prefix);
                connect(m_pEditorNetworkIPv6Prefix, &QLineEdit::textEdited,
                        this, &UIDetailsWidgetNATNetwork::sltNetworkIPv6PrefixChanged);

                pLayoutOptions->addWidget(m_pEditorNetworkIPv6Prefix, 4, 2, 1, 2);
            }
            /* Prepare 'advertise default IPv6 route' check-box: */
            m_pCheckboxAdvertiseDefaultIPv6Route = new QCheckBox(pTabOptions);
            if (m_pCheckboxAdvertiseDefaultIPv6Route)
            {
                connect(m_pCheckboxAdvertiseDefaultIPv6Route, &QCheckBox::toggled,
                        this, &UIDetailsWidgetNATNetwork::sltAdvertiseDefaultIPv6RouteChanged);
                pLayoutOptions->addWidget(m_pCheckboxAdvertiseDefaultIPv6Route, 5, 2);
            }

            /* If parent embedded into stack: */
            if (m_enmEmbedding == EmbedTo_Stack)
            {
                /* Prepare button-box: */
                m_pButtonBoxOptions = new QIDialogButtonBox(pTabOptions);
                if (m_pButtonBoxOptions)
                {
                    m_pButtonBoxOptions->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
                    connect(m_pButtonBoxOptions, &QIDialogButtonBox::clicked, this, &UIDetailsWidgetNATNetwork::sltHandleButtonBoxClick);

                    pLayoutOptions->addWidget(m_pButtonBoxOptions, 7, 0, 1, 4);
                }
            }
        }

        m_pTabWidget->addTab(pTabOptions, QString());
    }
}

void UIDetailsWidgetNATNetwork::prepareTabForwarding()
{
    /* Prepare 'Forwarding' tab: */
    QWidget *pTabForwarding = new QWidget(m_pTabWidget);
    if (pTabForwarding)
    {
        /* Prepare 'Forwarding' layout: */
        QGridLayout *pLayoutForwarding = new QGridLayout(pTabForwarding);
        if (pLayoutForwarding)
        {
#ifdef VBOX_WS_MAC
            /* Configure layout: */
            pLayoutForwarding->setSpacing(10);
            pLayoutForwarding->setContentsMargins(10, 10, 10, 10);
#endif

            /* Prepare forwarding tab-widget: */
            m_pTabWidgetForwarding = new QITabWidget(pTabForwarding);
            if (m_pTabWidgetForwarding)
            {
                /* Prepare IPv4 forwarding table: */
                m_pForwardingTableIPv4 = new UIPortForwardingTable(UIPortForwardingDataList(),
                                                                   false /* ip IPv6 protocol */,
                                                                   false /* allow empty guest IPs */);
                if (m_pForwardingTableIPv4)
                {
                    connect(m_pForwardingTableIPv4, &UIPortForwardingTable::sigDataChanged,
                            this, &UIDetailsWidgetNATNetwork::sltForwardingRulesIPv4Changed);
                    m_pTabWidgetForwarding->addTab(m_pForwardingTableIPv4, QString());
                }
                /* Prepare IPv6 forwarding table: */
                m_pForwardingTableIPv6 = new UIPortForwardingTable(UIPortForwardingDataList(),
                                                                   true /* ip IPv6 protocol */,
                                                                   false /* allow empty guest IPs */);
                if (m_pForwardingTableIPv6)
                {
                    connect(m_pForwardingTableIPv4, &UIPortForwardingTable::sigDataChanged,
                            this, &UIDetailsWidgetNATNetwork::sltForwardingRulesIPv6Changed);
                    m_pTabWidgetForwarding->addTab(m_pForwardingTableIPv6, QString());
                }

                pLayoutForwarding->addWidget(m_pTabWidgetForwarding, 0, 0);
            }

            /* If parent embedded into stack: */
            if (m_enmEmbedding == EmbedTo_Stack)
            {
                /* Prepare button-box: */
                m_pButtonBoxForwarding = new QIDialogButtonBox(pTabForwarding);
                if (m_pButtonBoxForwarding)
                {
                    m_pButtonBoxForwarding->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
                    connect(m_pButtonBoxForwarding, &QIDialogButtonBox::clicked, this, &UIDetailsWidgetNATNetwork::sltHandleButtonBoxClick);

                    pLayoutForwarding->addWidget(m_pButtonBoxForwarding, 1, 0);
                }
            }
        }

        m_pTabWidget->addTab(pTabForwarding, QString());
    }
}

void UIDetailsWidgetNATNetwork::loadDataForOptions()
{
    /* Check whether network exists and enabled: */
    const bool fIsNetworkExists = m_newData.m_fExists;
    const bool fIsIPv6Supported = m_newData.m_fSupportsIPv6;

    /* Update 'Options' field availability: */
    m_pLabelNetworkName->setEnabled(fIsNetworkExists);
    m_pEditorNetworkName->setEnabled(fIsNetworkExists);
    m_pLabelNetworkIPv4Prefix->setEnabled(fIsNetworkExists);
    m_pEditorNetworkIPv4Prefix->setEnabled(fIsNetworkExists);
    m_pCheckboxSupportsDHCP->setEnabled(fIsNetworkExists);
    m_pCheckboxIPv6->setEnabled(fIsNetworkExists);
    m_pLabelNetworkIPv6Prefix->setEnabled(fIsNetworkExists && fIsIPv6Supported);
    m_pEditorNetworkIPv6Prefix->setEnabled(fIsNetworkExists && fIsIPv6Supported);
    m_pCheckboxAdvertiseDefaultIPv6Route->setEnabled(fIsNetworkExists && fIsIPv6Supported);

    /* Load 'Options' fields: */
    m_pEditorNetworkName->setText(m_newData.m_strName);
    m_pEditorNetworkIPv4Prefix->setText(m_newData.m_strPrefixIPv4);
    m_pCheckboxSupportsDHCP->setChecked(m_newData.m_fSupportsDHCP);
    m_pCheckboxIPv6->setChecked(m_newData.m_fSupportsIPv6);
    m_pEditorNetworkIPv6Prefix->setText(m_newData.m_strPrefixIPv6);
    m_pCheckboxAdvertiseDefaultIPv6Route->setChecked(m_newData.m_fAdvertiseDefaultIPv6Route);
}

void UIDetailsWidgetNATNetwork::loadDataForForwarding()
{
    /* Check whether network exists and enabled: */
    const bool fIsNetworkExists = m_newData.m_fExists;

    /* Update 'Forwarding' field availability: */
    m_pForwardingTableIPv4->setEnabled(fIsNetworkExists);
    m_pForwardingTableIPv6->setEnabled(fIsNetworkExists);

    /* Calculate/load guest address hints: */
    char szTmpIp[16];
    RTNETADDRIPV4 rtNetwork4;
    int iPrefix4;
    const int rc = RTNetStrToIPv4Cidr(m_newData.m_strPrefixIPv4.toUtf8().constData(), &rtNetwork4, &iPrefix4);
    RTStrPrintf(szTmpIp, sizeof(szTmpIp), "%RTnaipv4", rtNetwork4);
    if (RT_SUCCESS(rc))
        m_pForwardingTableIPv4->setGuestAddressHint(QString(szTmpIp));

    /* Load 'Forwarding' fields: */
    m_pForwardingTableIPv4->setRules(m_newData.m_rules4, m_fHoldPosition);
    m_pForwardingTableIPv6->setRules(m_newData.m_rules6, m_fHoldPosition);
    m_fHoldPosition = false;
}
