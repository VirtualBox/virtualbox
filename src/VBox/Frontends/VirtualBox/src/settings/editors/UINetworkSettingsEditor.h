/* $Id: UINetworkSettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkSettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UINetworkSettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UINetworkSettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIPortForwardingTable.h"

/* COM includes: */
#include "KNetworkAdapterType.h"
#include "KNetworkAttachmentType.h"
#include "KNetworkAdapterPromiscModePolicy.h"


/* Forward declarations: */
class QCheckBox;
class UINetworkAttachmentEditor;
class UINetworkFeaturesEditor;

/** UIEditor sub-class used as a network settings editor. */
class SHARED_LIBRARY_STUFF UINetworkSettingsEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** @name Attachment editor stuff
     * @{ */
        /** Notifies about feature state changed. */
        void sigFeatureStateChanged();
        /** Notifies about attachment type changed. */
        void sigAttachmentTypeChanged();
        /** Notifies about alternative name changed. */
        void sigAlternativeNameChanged();
    /** @} */

    /** @name Features editor stuff
     * @{ */
        /** Notifies about MAC address changed. */
        void sigMACAddressChanged();
    /** @} */

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UINetworkSettingsEditor(QWidget *pParent = 0);

    /** @name General stuff
     * @{ */
        /** Defines whether feature is @a fEnabled. */
        void setFeatureEnabled(bool fEnabled);
        /** Returns whether feature is enabled. */
        bool isFeatureEnabled() const;

        /** Defines whether feature @a fAvailable. */
        void setFeatureAvailable(bool fAvailable);
    /** @} */

    /** @name Attachment editor stuff
     * @{ */
        /** Defines value @a enmType. */
        void setValueType(KNetworkAttachmentType enmType);
        /** Returns value type. */
        KNetworkAttachmentType valueType() const;

        /** Defines value @a names for specified @a enmType. */
        void setValueNames(KNetworkAttachmentType enmType, const QStringList &names);
        /** Defines value @a strName for specified @a enmType. */
        void setValueName(KNetworkAttachmentType enmType, const QString &strName);
        /** Returns current name for specified @a enmType. */
        QString valueName(KNetworkAttachmentType enmType) const;

        /** Defines whether attachment options @a fAvailable. */
        void setAttachmentOptionsAvailable(bool fAvailable);
    /** @} */

    /** @name Features editor stuff
     * @{ */
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
    /** @} */

protected:

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;
    /** Handles feature toggling. */
    void sltHandleFeatureToggled();
    /** Handles adapter attachment type change. */
    void sltHandleAttachmentTypeChange();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares connections. */
    void prepareConnections();

    /** Updates feature availability. */
    void updateFeatureAvailability();
    /** Updates minimum layout hint. */
    void updateMinimumLayoutHint();

    /** @name Values
     * @{ */
        /** Holds whether feature is enabled. */
        bool  m_fFeatureEnabled;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the feature check-box instance. */
        QCheckBox                 *m_pCheckboxFeature;
        /** Holds the settings widget instance. */
        QWidget                   *m_pWidgetSettings;
        /** Holds the network attachment editor instance. */
        UINetworkAttachmentEditor *m_pEditorNetworkAttachment;
        /** Holds the network features editor instance. */
        UINetworkFeaturesEditor   *m_pEditorNetworkFeatures;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UINetworkSettingsEditor_h */
