/* $Id: UINetworkFeaturesEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkFeaturesEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UINetworkFeaturesEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UINetworkFeaturesEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIMachineSettingsPortForwardingDlg.h"

/* COM includes: */
#include "KNetworkAdapterType.h"
#include "KNetworkAdapterPromiscModePolicy.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QTextEdit;
class QILineEdit;
class QIToolButton;

/** UIEditor sub-class used as a network features editor. */
class SHARED_LIBRARY_STUFF UINetworkFeaturesEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies about MAC address changed. */
    void sigMACAddressChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UINetworkFeaturesEditor(QWidget *pParent = 0);

    /** Defines adapter @a enmType. */
    void setAdapterType(const KNetworkAdapterType &enmType);
    /** Returns adapter type. */
    KNetworkAdapterType adapterType() const;

    /** Defines promiscuous @a enmMode. */
    void setPromiscuousMode(const KNetworkAdapterPromiscModePolicy &enmMode);
    /** Returns promiscuous mode. */
    KNetworkAdapterPromiscModePolicy promiscuousMode() const;

    /** Defines MAC @a strAddress. */
    void setMACAddress(const QString &strAddress);
    /** Returns MAC address. */
    QString macAddress() const;

    /** Defines generic @a strProperties. */
    void setGenericProperties(const QString &strProperties);
    /** Returns generic properties. */
    QString genericProperties() const;

    /** Defines whether cable is @a fConnected. */
    void setCableConnected(bool fConnected);
    /** Returns whether cable is connected. */
    bool cableConnected() const;

    /** Defines list of port forwarding @a rules. */
    void setPortForwardingRules(const UIPortForwardingDataList &rules);
    /** Returns list of port forwarding rules. */
    UIPortForwardingDataList portForwardingRules() const;

    /** Defines whether adapter options @a fAvailable. */
    void setAdapterOptionsAvailable(bool fAvailable);
    /** Defines whether promiscuous options @a fAvailable. */
    void setPromiscuousOptionsAvailable(bool fAvailable);
    /** Defines whether MAC options @a fAvailable. */
    void setMACOptionsAvailable(bool fAvailable);
    /** Defines whether generic properties @a fAvailable. */
    void setGenericPropertiesAvailable(bool fAvailable);
    /** Defines whether cable options @a fAvailable. */
    void setCableOptionsAvailable(bool fAvailable);
    /** Defines whether forwarding options @a fAvailable. */
    void setForwardingOptionsAvailable(bool fAvailable);

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

public slots:

    /** Generates MAC address. */
    void generateMac();

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

    /** Handles request to open port forwarding dialog. */
    void sltOpenPortForwardingDlg();

private:

    /** Prepares all. */
    void prepare();

    /** Populates adapter type combo. */
    void populateAdapterTypeCombo();
    /** Populates promiscuous mode combo. */
    void populatePromiscuousModeCombo();

    /** @name Values
     * @{ */
        /** Holds the adapter type to be set. */
        KNetworkAdapterType               m_enmAdapterType;
        /** Holds the promisc mode to be set. */
        KNetworkAdapterPromiscModePolicy  m_enmPromiscuousMode;
        /** Holds the MAC address to be set. */
        QString                           m_strMACAddress;
        /** Holds the generic properties to be set. */
        QString                           m_strGenericProperties;
        /** Holds whether cable is connected. */
        bool                              m_fCableConnected;
        /** Holds the list of port forwarding rules. */
        UIPortForwardingDataList          m_portForwardingRules;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the settings layout instance. */
        QGridLayout  *m_pLayoutSettings;
        /** Holds the adapter type label instance. */
        QLabel       *m_pLabelAdapterType;
        /** Holds the adapter type editor instance. */
        QComboBox    *m_pComboAdapterType;
        /** Holds the promiscuous mode label instance. */
        QLabel       *m_pLabelPromiscuousMode;
        /** Holds the promiscuous mode combo instance. */
        QComboBox    *m_pComboPromiscuousMode;
        /** Holds the MAC label instance. */
        QLabel       *m_pLabelMAC;
        /** Holds the MAC editor instance. */
        QILineEdit   *m_pEditorMAC;
        /** Holds the MAC button instance. */
        QIToolButton *m_pButtonMAC;
        /** Holds the generic properties label instance. */
        QLabel       *m_pLabelGenericProperties;
        /** Holds the generic properties editor instance. */
        QTextEdit    *m_pEditorGenericProperties;
        /** Holds the cable connected check-box instance. */
        QCheckBox    *m_pCheckBoxCableConnected;
        /** Holds the port forwarding button instance. */
        QPushButton  *m_pButtonPortForwarding;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UINetworkFeaturesEditor_h */
