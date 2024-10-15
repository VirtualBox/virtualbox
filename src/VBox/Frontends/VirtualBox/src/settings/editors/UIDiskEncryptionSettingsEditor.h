/* $Id: UIDiskEncryptionSettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDiskEncryptionSettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIDiskEncryptionSettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIDiskEncryptionSettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"

/* COM includes: */
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

/** UIEditor sub-class used as a disk encryption settings editor. */
class SHARED_LIBRARY_STUFF UIDiskEncryptionSettingsEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notify listeners about status changed. */
    void sigStatusChanged();
    /** Notify listeners about cipher changed. */
    void sigCipherChanged();
    /** Notify listeners about password changed. */
    void sigPasswordChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIDiskEncryptionSettingsEditor(QWidget *pParent = 0);

    /** Defines whether feature is @a fEnabled. */
    void setFeatureEnabled(bool fEnabled);
    /** Returns whether feature is enabled. */
    bool isFeatureEnabled() const;

    /** Defines cipher @a enmType. */
    void setCipherType(const UIDiskEncryptionCipherType &enmType);
    /** Returns cipher type. */
    UIDiskEncryptionCipherType cipherType() const;

    /** Returns password 1. */
    QString password1() const;
    /** Returns password 2. */
    QString password2() const;

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

    /** Repopulates combo-box. */
    void repopulateCombo();

    /** @name Values
     * @{ */
        /** Holds whether feature is enabled. */
        bool                        m_fFeatureEnabled;
        /** Holds the cipher type. */
        UIDiskEncryptionCipherType  m_enmCipherType;
        /** Holds the password 1. */
        QString                     m_strPassword1;
        /** Holds the password 2. */
        QString                     m_strPassword2;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the feature check-box instance. */
        QCheckBox *m_pCheckboxFeature;
        /** Holds the settings widget instance. */
        QWidget   *m_pWidgetSettings;
        /** Holds the cipher type label instance. */
        QLabel    *m_pLabelCipherType;
        /** Holds the cipher type combo instance. */
        QComboBox *m_pComboCipherType;
        /** Holds the password 1 label instance. */
        QLabel    *m_pLabelPassword1;
        /** Holds the password 1 editor instance. */
        QLineEdit *m_pEditorPassword1;
        /** Holds the password 2 label instance. */
        QLabel    *m_pLabelPassword2;
        /** Holds the password 2 editor instance. */
        QLineEdit *m_pEditorPassword2;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIDiskEncryptionSettingsEditor_h */
