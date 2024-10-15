/* $Id: UIDetailsWidgetHostNetwork.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsWidgetHostNetwork class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetHostNetwork_h
#define FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetHostNetwork_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QAbstractButton;
class QCheckBox;
class QLabel;
class QRadioButton;
class QIDialogButtonBox;
class QILineEdit;
class QITabWidget;


#ifdef VBOX_WS_MAC
/** Network Manager: Host network data structure. */
struct UIDataHostNetwork
{
    /** Constructs data. */
    UIDataHostNetwork()
        : m_fExists(false)
        , m_strName(QString())
        , m_strMask(QString())
        , m_strLBnd(QString())
        , m_strUBnd(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataHostNetwork &other) const
    {
        return true
               && (m_fExists == other.m_fExists)
               && (m_strName == other.m_strName)
               && (m_strMask == other.m_strMask)
               && (m_strLBnd == other.m_strLBnd)
               && (m_strUBnd == other.m_strUBnd)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataHostNetwork &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataHostNetwork &other) const { return !equal(other); }

    /** Holds this interface is not NULL. */
    bool     m_fExists;
    /** Holds network name. */
    QString  m_strName;
    /** Holds network mask. */
    QString  m_strMask;
    /** Holds lower bound. */
    QString  m_strLBnd;
    /** Holds upper bound. */
    QString  m_strUBnd;
};

#else /* !VBOX_WS_MAC */

/** Network Manager: Host Network Interface data structure. */
struct UIDataHostNetworkInterface
{
    /** Constructs data. */
    UIDataHostNetworkInterface()
        : m_fExists(false)
        , m_strName(QString())
        , m_fDHCPEnabled(false)
        , m_strAddress(QString())
        , m_strMask(QString())
        , m_fSupportedIPv6(false)
        , m_strAddress6(QString())
        , m_strPrefixLength6(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataHostNetworkInterface &other) const
    {
        return true
               && (m_fExists == other.m_fExists)
               && (m_strName == other.m_strName)
               && (m_fDHCPEnabled == other.m_fDHCPEnabled)
               && (m_strAddress == other.m_strAddress)
               && (m_strMask == other.m_strMask)
               && (m_fSupportedIPv6 == other.m_fSupportedIPv6)
               && (m_strAddress6 == other.m_strAddress6)
               && (m_strPrefixLength6 == other.m_strPrefixLength6)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataHostNetworkInterface &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataHostNetworkInterface &other) const { return !equal(other); }

    /** Holds this interface is not NULL. */
    bool     m_fExists;
    /** Holds interface name. */
    QString  m_strName;
    /** Holds whether DHCP is enabled for that interface. */
    bool     m_fDHCPEnabled;
    /** Holds IPv4 interface address. */
    QString  m_strAddress;
    /** Holds IPv4 interface mask. */
    QString  m_strMask;
    /** Holds whether IPv6 protocol supported. */
    bool     m_fSupportedIPv6;
    /** Holds IPv6 interface address. */
    QString  m_strAddress6;
    /** Holds IPv6 interface prefix length. */
    QString  m_strPrefixLength6;
};

/** Network Manager: DHCP Server data structure. */
struct UIDataDHCPServer
{
    /** Constructs data. */
    UIDataDHCPServer()
        : m_fEnabled(false)
        , m_strAddress(QString())
        , m_strMask(QString())
        , m_strLowerAddress(QString())
        , m_strUpperAddress(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataDHCPServer &other) const
    {
        return true
               && (m_fEnabled == other.m_fEnabled)
               && (m_strAddress == other.m_strAddress)
               && (m_strMask == other.m_strMask)
               && (m_strLowerAddress == other.m_strLowerAddress)
               && (m_strUpperAddress == other.m_strUpperAddress)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataDHCPServer &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataDHCPServer &other) const { return !equal(other); }

    /** Holds whether DHCP server enabled. */
    bool     m_fEnabled;
    /** Holds DHCP server address. */
    QString  m_strAddress;
    /** Holds DHCP server mask. */
    QString  m_strMask;
    /** Holds DHCP server lower address. */
    QString  m_strLowerAddress;
    /** Holds DHCP server upper address. */
    QString  m_strUpperAddress;
};

/** Network Manager: Host network data structure. */
struct UIDataHostNetwork
{
    /** Constructs data. */
    UIDataHostNetwork()
        : m_interface(UIDataHostNetworkInterface())
        , m_dhcpserver(UIDataDHCPServer())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataHostNetwork &other) const
    {
        return true
               && (m_interface == other.m_interface)
               && (m_dhcpserver == other.m_dhcpserver)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataHostNetwork &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataHostNetwork &other) const { return !equal(other); }

    /** Holds the interface data. */
    UIDataHostNetworkInterface  m_interface;
    /** Holds the DHCP server data. */
    UIDataDHCPServer            m_dhcpserver;
};
#endif /* !VBOX_WS_MAC */


/** Network Manager: Host network details-widget. */
class UIDetailsWidgetHostNetwork : public QWidget
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
    UIDetailsWidgetHostNetwork(EmbedTo enmEmbedding, QWidget *pParent = 0);

    /** Returns the host network data. */
    const UIDataHostNetwork &data() const { return m_newData; }
#ifdef VBOX_WS_MAC
    /** Defines the host network @a data.
      * @param  busyNames  Holds the list of names busy by other networks. */
    void setData(const UIDataHostNetwork &data,
                 const QStringList &busyNames = QStringList());
#else /* !VBOX_WS_MAC */
    /** Defines the host network @a data. */
    void setData(const UIDataHostNetwork &data);
#endif /* !VBOX_WS_MAC */

    /** @name Change handling stuff.
      * @{ */
        /** Revalidates changes for passed @a pWidget. */
        bool revalidate() const;

        /** Updates button states. */
        void updateButtonStates();
    /** @} */

private slots:

    /** @name Change handling stuff.
      * @{ */
#ifdef VBOX_WS_MAC
        /** Handles network name text change. */
        void sltTextChangedName(const QString &strText);
        /** Handles network mask text change. */
        void sltTextChangedMask(const QString &strText);
        /** Handles network lower bound text change. */
        void sltTextChangedLBnd(const QString &strText);
        /** Handles network upper bound text change. */
        void sltTextChangedUBnd(const QString &strText);

#else /* !VBOX_WS_MAC */

        /** Handles interface automatic configuration choice change. */
        void sltToggledButtonAutomatic(bool fChecked);
        /** Handles interface manual configuration choice change. */
        void sltToggledButtonManual(bool fChecked);
        /** Handles interface IPv4 text change. */
        void sltTextChangedIPv4(const QString &strText);
        /** Handles interface NMv4 text change. */
        void sltTextChangedNMv4(const QString &strText);
        /** Handles interface IPv6 text change. */
        void sltTextChangedIPv6(const QString &strText);
        /** Handles interface NMv6 text change. */
        void sltTextChangedNMv6(const QString &strText);

        /** Handles DHCP server status change. */
        void sltStatusChangedServer(int iChecked);
        /** Handles DHCP server address text change. */
        void sltTextChangedAddress(const QString &strText);
        /** Handles DHCP server mask text change. */
        void sltTextChangedMask(const QString &strText);
        /** Handles DHCP server lower address text change. */
        void sltTextChangedLowerAddress(const QString &strText);
        /** Handles DHCP server upper address text change. */
        void sltTextChangedUpperAddress(const QString &strText);
#endif /* !VBOX_WS_MAC */

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
#ifdef VBOX_WS_MAC
        /** Prepares options. */
        void prepareOptions();
#else /* !VBOX_WS_MAC */
        /** Prepares tab-widget. */
        void prepareTabWidget();
        /** Prepares 'Interface' tab. */
        void prepareTabInterface();
        /** Prepares 'DHCP server' tab. */
        void prepareTabDHCPServer();
#endif /* !VBOX_WS_MAC */
    /** @} */

    /** @name Loading stuff.
      * @{ */
#ifdef VBOX_WS_MAC
        /** Loads data. */
        void loadData();
#else /* !VBOX_WS_MAC */
        /** Loads interface data. */
        void loadDataForInterface();
        /** Loads server data. */
        void loadDataForDHCPServer();
#endif /* !VBOX_WS_MAC */
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the parent widget embedding type. */
        const EmbedTo m_enmEmbedding;

        /** Holds the old data copy. */
        UIDataHostNetwork  m_oldData;
        /** Holds the new data copy. */
        UIDataHostNetwork  m_newData;

#ifndef VBOX_WS_MAC
        /** Holds the tab-widget. */
        QITabWidget *m_pTabWidget;
#endif /* !VBOX_WS_MAC */
    /** @} */

#ifdef VBOX_WS_MAC
    /** @name Network variables.
      * @{ */
        /** Holds the name label. */
        QLabel       *m_pLabelName;
        /** Holds the name editor. */
        QILineEdit   *m_pEditorName;

        /** Holds the mask label. */
        QLabel       *m_pLabelMask;
        /** Holds the mask editor. */
        QILineEdit   *m_pEditorMask;

        /** Holds the lower bound label. */
        QLabel       *m_pLabelLBnd;
        /** Holds the lower bound editor. */
        QILineEdit   *m_pEditorLBnd;

        /** Holds the upper bound label. */
        QLabel       *m_pLabelUBnd;
        /** Holds the upper bound editor. */
        QILineEdit   *m_pEditorUBnd;

        /** Holds the button-box instance. */
        QIDialogButtonBox *m_pButtonBox;

        /** Holds the list of names busy by other networks. */
        QStringList  m_busyNames;
    /** @} */

#else /* !VBOX_WS_MAC */

    /** @name Interface variables.
      * @{ */
        /** Holds the automatic interface configuration button. */
        QRadioButton *m_pButtonAutomatic;

        /** Holds the manual interface configuration button. */
        QRadioButton *m_pButtonManual;

        /** Holds the IPv4 address label. */
        QLabel       *m_pLabelIPv4;
        /** Holds the IPv4 address editor. */
        QILineEdit   *m_pEditorIPv4;

        /** Holds the IPv4 network mask label. */
        QLabel       *m_pLabelNMv4;
        /** Holds the IPv4 network mask editor. */
        QILineEdit   *m_pEditorNMv4;

        /** Holds the IPv6 address label. */
        QLabel       *m_pLabelIPv6;
        /** Holds the IPv6 address editor. */
        QILineEdit   *m_pEditorIPv6;

        /** Holds the IPv6 network mask label. */
        QLabel       *m_pLabelNMv6;
        /** Holds the IPv6 network mask editor. */
        QILineEdit   *m_pEditorNMv6;

        /** Holds the interface button-box instance. */
        QIDialogButtonBox *m_pButtonBoxInterface;
    /** @} */

    /** @name DHCP server variables.
      * @{ */
        /** Holds the DHCP server status chack-box. */
        QCheckBox  *m_pCheckBoxDHCP;

        /** Holds the DHCP address label. */
        QLabel     *m_pLabelDHCPAddress;
        /** Holds the DHCP address editor. */
        QILineEdit *m_pEditorDHCPAddress;

        /** Holds the DHCP network mask label. */
        QLabel     *m_pLabelDHCPMask;
        /** Holds the DHCP network mask editor. */
        QILineEdit *m_pEditorDHCPMask;

        /** Holds the DHCP lower address label. */
        QLabel     *m_pLabelDHCPLowerAddress;
        /** Holds the DHCP lower address editor. */
        QILineEdit *m_pEditorDHCPLowerAddress;

        /** Holds the DHCP upper address label. */
        QLabel     *m_pLabelDHCPUpperAddress;
        /** Holds the DHCP upper address editor. */
        QILineEdit *m_pEditorDHCPUpperAddress;

        /** Holds the server button-box instance. */
        QIDialogButtonBox *m_pButtonBoxServer;
    /** @} */
#endif /* !VBOX_WS_MAC */
};

#endif /* !FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetHostNetwork_h */
