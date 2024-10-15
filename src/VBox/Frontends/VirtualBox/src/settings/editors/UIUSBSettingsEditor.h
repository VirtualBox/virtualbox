/* $Id: UIUSBSettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBSettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIUSBSettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIUSBSettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIUSBFiltersEditor.h"

/* COM includes: */
#include "KUSBControllerType.h"

/* Forward declarations: */
class QCheckBox;
class UIUSBControllerEditor;

/** UIEditor sub-class used as a USB settings editor. */
class SHARED_LIBRARY_STUFF UIUSBSettingsEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value change. */
    void sigValueChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIUSBSettingsEditor(QWidget *pParent = 0);

    /** @name General stuff
     * @{ */
        /** Defines whether feature is @a fEnabled. */
        void setFeatureEnabled(bool fEnabled);
        /** Returns whether feature is enabled. */
        bool isFeatureEnabled() const;

        /** Defines whether feature @a fAvailable. */
        void setFeatureAvailable(bool fAvailable);
    /** @} */

    /** @name USB controller editor stuff
     * @{ */
        /** Defines USB controller @a enmType. */
        void setUsbControllerType(KUSBControllerType enmType);
        /** Returns USB controller type. */
        KUSBControllerType usbControllerType() const;

        /** Defines whether USB controller option @a fAvailable. */
        void setUsbControllerOptionAvailable(bool fAvailable);
    /** @} */

    /** @name USB filters editor stuff
     * @{ */
        /** Defines a list of USB @a filters. */
        void setUsbFilters(const QList<UIDataUSBFilter> &filters);
        /** Returns a list of USB filters. */
        QList<UIDataUSBFilter> usbFilters() const;

        /** Defines whether USB filters option @a fAvailable. */
        void setUsbFiltersOptionAvailable(bool fAvailable);
    /** @} */

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Handles feature toggling. */
    void sltHandleFeatureToggled();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares connections. */
    void prepareConnections();

    /** Updates feature availability. */
    void updateFeatureAvailability();

    /** @name Values
     * @{ */
        /** Holds whether feature is enabled. */
        bool  m_fFeatureEnabled;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the feature check-box instance. */
        QCheckBox             *m_pCheckboxFeature;
        /** Holds the settings widget instance. */
        QWidget               *m_pWidgetSettings;
        /** Holds the controller editor instance. */
        UIUSBControllerEditor *m_pEditorController;
        /** Holds the filters editor instance. */
        UIUSBFiltersEditor    *m_pEditorFilters;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIUSBSettingsEditor_h */
