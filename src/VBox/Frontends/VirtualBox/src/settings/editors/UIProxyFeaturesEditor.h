/* $Id: UIProxyFeaturesEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProxyFeaturesEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIProxyFeaturesEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIProxyFeaturesEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* COM includes: */
#include "KProxyMode.h"

/* GUI includes: */
#include "UIEditor.h"


/* Forward declarations: */
class QButtonGroup;
class QLabel;
class QRadioButton;
class QILineEdit;

/** UIEditor sub-class used as global proxy features editor. */
class SHARED_LIBRARY_STUFF UIProxyFeaturesEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about proxy mode changed. */
    void sigProxyModeChanged();
    /** Notifies listeners about proxy host changed. */
    void sigProxyHostChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIProxyFeaturesEditor(QWidget *pParent = 0);

    /** Defines proxy @a enmMode. */
    void setProxyMode(KProxyMode enmMode);
    /** Returns proxy mode. */
    KProxyMode proxyMode() const;

    /** Defines proxy @a strHost. */
    void setProxyHost(const QString &strHost);
    /** Returns proxy host. */
    QString proxyHost() const;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles proxy mode change. */
    void sltHandleProxyModeChanged();

private:

    /** Prepares all. */
    void prepare();

    /** @name Values
     * @{ */
        /** Holds the proxy mode. */
        KProxyMode  m_enmProxyMode;
        /** Holds the proxy host. */
        QString     m_strProxyHost;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the button-group instance. */
        QButtonGroup *m_pButtonGroup;
        /** Holds the 'proxy auto' radio-button instance. */
        QRadioButton *m_pRadioButtonProxyAuto;
        /** Holds the 'proxy disabled' radio-button instance. */
        QRadioButton *m_pRadioButtonProxyDisabled;
        /** Holds the 'proxy enabled' radio-button instance. */
        QRadioButton *m_pRadioButtonProxyEnabled;
        /** Holds the settings widget instance. */
        QWidget      *m_pWidgetSettings;
        /** Holds the host label instance. */
        QLabel       *m_pLabelHost;
        /** Holds the host editor instance. */
        QILineEdit   *m_pEditorHost;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIProxyFeaturesEditor_h */
