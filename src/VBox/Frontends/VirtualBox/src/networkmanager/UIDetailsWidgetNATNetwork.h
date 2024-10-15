/* $Id: UIDetailsWidgetNATNetwork.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsWidgetNATNetwork class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetNATNetwork_h
#define FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetNATNetwork_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "QIManagerDialog.h"
#include "UIPortForwardingTable.h"

/* Forward declarations: */
class QAbstractButton;
class QCheckBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QIDialogButtonBox;
class QILineEdit;
class QITabWidget;


/** Network Manager: NAT network data structure. */
struct UIDataNATNetwork
{
    /** Constructs data. */
    UIDataNATNetwork()
        : m_fExists(false)
        , m_strName(QString())
        , m_strPrefixIPv4(QString())
        , m_strPrefixIPv6(QString())
        , m_fSupportsDHCP(false)
        , m_fSupportsIPv6(false)
        , m_fAdvertiseDefaultIPv6Route(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataNATNetwork &other) const
    {
        return true
               && (m_fExists == other.m_fExists)
               && (m_strName == other.m_strName)
               && (m_strPrefixIPv4 == other.m_strPrefixIPv4)
               && (m_strPrefixIPv6 == other.m_strPrefixIPv6)
               && (m_fSupportsDHCP == other.m_fSupportsDHCP)
               && (m_fSupportsIPv6 == other.m_fSupportsIPv6)
               && (m_fAdvertiseDefaultIPv6Route == other.m_fAdvertiseDefaultIPv6Route)
               && (m_rules4 == other.m_rules4)
               && (m_rules6 == other.m_rules6)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataNATNetwork &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataNATNetwork &other) const { return !equal(other); }

    /** Holds whether this network is not NULL. */
    bool                      m_fExists;
    /** Holds network name. */
    QString                   m_strName;
    /** Holds network IPv4 prefix. */
    QString                   m_strPrefixIPv4;
    /** Holds network IPv6 prefix. */
    QString                   m_strPrefixIPv6;
    /** Holds whether this network supports DHCP. */
    bool                      m_fSupportsDHCP;
    /** Holds whether this network supports IPv6. */
    bool                      m_fSupportsIPv6;
    /** Holds whether this network advertised as default IPv6 route. */
    bool                      m_fAdvertiseDefaultIPv6Route;
    /** Holds IPv4 port forwarding rules. */
    UIPortForwardingDataList  m_rules4;
    /** Holds IPv6 port forwarding rules. */
    UIPortForwardingDataList  m_rules6;
};


/** Network Manager: NAT network details-widget. */
class UIDetailsWidgetNATNetwork : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about data changed and whether it @a fDiffers. */
    void sigDataChanged(bool fDiffers);

    /** Notifies listeners about data change rejected and should be reseted. */
    void sigDataChangeRejected();
    /** Notifies listeners about data change accepted and should be applied. */
    void sigDataChangeAccepted();

public:

    /** Constructs medium details dialog passing @a pParent to the base-class.
      * @param  enmEmbedding  Brings embedding type. */
    UIDetailsWidgetNATNetwork(EmbedTo enmEmbedding, QWidget *pParent = 0);

    /** Returns the host network data. */
    const UIDataNATNetwork &data() const { return m_newData; }
    /** Defines the host network @a data.
      * @param  busyNames      Holds the list of names busy by other
      *                        NAT networks.
      * @param  fHoldPosition  Holds whether we should try to keep
      *                        port forwarding rule position intact. */
    void setData(const UIDataNATNetwork &data,
                 const QStringList &busyNames = QStringList(),
                 bool fHoldPosition = false);

    /** @name Change handling stuff.
      * @{ */
        /** Revalidates changes. */
        bool revalidate() const;

        /** Updates button states. */
        void updateButtonStates();
    /** @} */

private slots:

    /** @name Change handling stuff.
      * @{ */
        /** Handles network name text change. */
        void sltNetworkNameChanged(const QString &strText);
        /** Handles network IPv4 prefix text change. */
        void sltNetworkIPv4PrefixChanged(const QString &strText);
        /** Handles network IPv6 prefix text change. */
        void sltNetworkIPv6PrefixChanged(const QString &strText);
        /** Handles network supports DHCP choice change. */
        void sltSupportsDHCPChanged(bool fChecked);
        /** Handles network supports IPv6 choice change. */
        void sltSupportsIPv6Changed(bool fChecked);
        /** Handles network advertised as default IPv6 route choice change. */
        void sltAdvertiseDefaultIPv6RouteChanged(bool fChecked);

        /** Handles IPv4 forwarding rules table change. */
        void sltForwardingRulesIPv4Changed();
        /** Handles IPv6 forwarding rules table change. */
        void sltForwardingRulesIPv6Changed();

        /** Handles button-box button click. */
        void sltHandleButtonBoxClick(QAbstractButton *pButton);
    /** @} */
    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares this. */
        void prepareThis();
        /** Prepares tab-widget. */
        void prepareTabWidget();
        /** Prepares 'Options' tab. */
        void prepareTabOptions();
        /** Prepares 'Forwarding' tab. */
        void prepareTabForwarding();
    /** @} */

    /** @name Loading stuff.
      * @{ */
        /** Loads 'Options' data. */
        void loadDataForOptions();
        /** Loads 'Forwarding' data. */
        void loadDataForForwarding();
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the parent widget embedding type. */
        const EmbedTo m_enmEmbedding;

        /** Holds the old data copy. */
        UIDataNATNetwork  m_oldData;
        /** Holds the new data copy. */
        UIDataNATNetwork  m_newData;

        /** Holds the tab-widget. */
        QITabWidget *m_pTabWidget;
    /** @} */

    /** @name Network variables.
      * @{ */
        /** Holds the network name label instance. */
        QLabel            *m_pLabelNetworkName;
        /** Holds the network name editor instance. */
        QLineEdit         *m_pEditorNetworkName;
        /** Holds the network IPv4 prefix label instance. */
        QLabel            *m_pLabelNetworkIPv4Prefix;
        /** Holds the network IPv4 prefix editor instance. */
        QLineEdit         *m_pEditorNetworkIPv4Prefix;
        /** Holds the 'supports DHCP' check-box instance. */
        QCheckBox         *m_pCheckboxSupportsDHCP;
        /** Holds the IPv4 group-box instance. */
        QCheckBox         *m_pCheckboxIPv6;
        /** Holds the network IPv6 prefix label instance. */
        QLabel            *m_pLabelNetworkIPv6Prefix;
        /** Holds the network IPv6 prefix editor instance. */
        QLineEdit         *m_pEditorNetworkIPv6Prefix;
        /** Holds the 'advertise default IPv6 route' check-box instance. */
        QCheckBox         *m_pCheckboxAdvertiseDefaultIPv6Route;
        /** Holds the 'Options' button-box instance. */
        QIDialogButtonBox *m_pButtonBoxOptions;
        /** Holds the list of names busy by other
          * NAT networks. */
        QStringList        m_busyNames;
    /** @} */

    /** @name Forwarding variables.
      * @{ */
        /** */
        QITabWidget           *m_pTabWidgetForwarding;
        /** */
        UIPortForwardingTable *m_pForwardingTableIPv4;
        /** */
        UIPortForwardingTable *m_pForwardingTableIPv6;
        /** Holds the 'Forwarding' button-box instance. */
        QIDialogButtonBox     *m_pButtonBoxForwarding;
        /** Holds whether we should try to keep
          * port forwarding rule position intact. */
        bool                   m_fHoldPosition;
    /** @} */
};


#endif /* !FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetNATNetwork_h */
