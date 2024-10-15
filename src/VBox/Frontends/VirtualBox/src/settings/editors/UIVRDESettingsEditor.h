/* $Id: UIVRDESettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVRDESettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIVRDESettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIVRDESettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* COM includes: */
#include "KAuthType.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QWidget;

/** VRDE Security Methods. */
enum UIVRDESecurityMethod
{
    UIVRDESecurityMethod_TLS,
    UIVRDESecurityMethod_RDP,
    UIVRDESecurityMethod_Negotiate,
    UIVRDESecurityMethod_Max
};
Q_DECLARE_METATYPE(UIVRDESecurityMethod);

/** UIEditor sub-class used as a VRDE settings editor. */
class SHARED_LIBRARY_STUFF UIVRDESettingsEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notify listeners about some status changed. */
    void sigChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIVRDESettingsEditor(QWidget *pParent = 0);

    /** Defines whether feature is @a fEnabled. */
    void setFeatureEnabled(bool fEnabled);
    /** Returns whether feature is enabled. */
    bool isFeatureEnabled() const;

    /** Defines whether VRDE options are @a fAvailable. */
    void setVRDEOptionsAvailable(bool fAvailable);

    /** Defines @a strPort. */
    void setPort(const QString &strPort);
    /** Returns port. */
    QString port() const;

    /** Defines security @a enmMethod. */
    void setSecurityMethod(const UIVRDESecurityMethod &enmMethod);
    /** Returns security method. */
    UIVRDESecurityMethod securityMethod() const;

    /** Defines auth @a enmType. */
    void setAuthType(const KAuthType &enmType);
    /** Returns auth type. */
    KAuthType authType() const;

    /** Defines @a strTimeout. */
    void setTimeout(const QString &strTimeout);
    /** Returns timeout. */
    QString timeout() const;

    /** Defines whether multiple connections @a fAllowed. */
    void setMultipleConnectionsAllowed(bool fAllowed);
    /** Returns whether multiple connections allowed. */
    bool isMultipleConnectionsAllowed() const;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles whether VRDE is @a fEnabled. */
    void sltHandleFeatureToggled(bool fEnabled);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares connections. */
    void prepareConnections();

    /** Repopulates security method combo-box. */
    void repopulateComboSecurityMethod();
    /** Repopulates auth type combo-box. */
    void repopulateComboAuthType();

    /** @name Values
     * @{ */
        /** Holds whether feature is enabled. */
        bool                  m_fFeatureEnabled;
        /** Holds the port. */
        QString               m_strPort;
        /** Holds the security method. */
        UIVRDESecurityMethod  m_enmSecurityMethod;
        /** Holds the auth type. */
        KAuthType             m_enmAuthType;
        /** Holds the timeout. */
        QString               m_strTimeout;
        /** Returns whether multiple connections allowed. */
        bool                  m_fMultipleConnectionsAllowed;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the feature check-box instance. */
        QCheckBox *m_pCheckboxFeature;
        /** Holds the settings widget instance. */
        QWidget   *m_pWidgetSettings;
        /** Holds the port label instance. */
        QLabel    *m_pLabelPort;
        /** Holds the port editor instance. */
        QLineEdit *m_pEditorPort;
        /** Holds the security method label instance. */
        QLabel    *m_pLabelSecurityMethod;
        /** Holds the security method combo instance. */
        QComboBox *m_pComboSecurityMethod;
        /** Holds the auth method label instance. */
        QLabel    *m_pLabelAuthMethod;
        /** Holds the auth method combo instance. */
        QComboBox *m_pComboAuthType;
        /** Holds the timeout label instance. */
        QLabel    *m_pLabelTimeout;
        /** Holds the timeout editor instance. */
        QLineEdit *m_pEditorTimeout;
        /** Holds the options label instance. */
        QLabel    *m_pLabelOptions;
        /** Holds the multiple connection check-box instance. */
        QCheckBox *m_pCheckboxMultipleConnections;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIVRDESettingsEditor_h */
