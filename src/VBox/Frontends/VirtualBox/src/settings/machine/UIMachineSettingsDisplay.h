/* $Id: UIMachineSettingsDisplay.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsDisplay class declaration.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsDisplay_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsDisplay_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* COM includes: */
#include "KGraphicsControllerType.h"

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
class QITabWidget;
class UIGraphicsControllerEditor;
#ifdef VBOX_WITH_3D_ACCELERATION
class UIDisplayScreenFeaturesEditor;
#endif
class UIMonitorCountEditor;
class UIRecordingSettingsEditor;
class UIScaleFactorEditor;
class UIVideoMemoryEditor;
class UIVRDESettingsEditor;
struct UIDataSettingsMachineDisplay;
typedef UISettingsCache<UIDataSettingsMachineDisplay> UISettingsCacheMachineDisplay;

/** Machine settings: Display page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsDisplay : public UISettingsPageMachine
{
    Q_OBJECT;

public:

    /** Constructs Display settings page. */
    UIMachineSettingsDisplay();
    /** Destructs Display settings page. */
    virtual ~UIMachineSettingsDisplay() RT_OVERRIDE;

    /** Defines @a comGuestOSType. */
    void setGuestOSTypeId(const QString &strGuestOSTypeId);

#ifdef VBOX_WITH_3D_ACCELERATION
    /** Returns whether 3D Acceleration is enabled. */
    bool isAcceleration3DSelected() const;
#endif

    /** Returns recommended graphics controller type. */
    KGraphicsControllerType graphicsControllerTypeRecommended() const;
    /** Returns current graphics controller type. */
    KGraphicsControllerType graphicsControllerTypeCurrent() const;

protected:

    /** Returns whether the page content was changed. */
    virtual bool changed() const RT_OVERRIDE;

    /** Loads settings from external object(s) packed inside @a data to cache.
      * @note  This task WILL be performed in other than the GUI thread, no widget interactions! */
    virtual void loadToCacheFrom(QVariant &data) RT_OVERRIDE;
    /** Loads data from cache to corresponding widgets.
      * @note  This task WILL be performed in the GUI thread only, all widget interactions here! */
    virtual void getFromCache() RT_OVERRIDE;

    /** Saves data from corresponding widgets to cache.
      * @note  This task WILL be performed in the GUI thread only, all widget interactions here! */
    virtual void putToCache() RT_OVERRIDE;
    /** Saves settings from cache to external object(s) packed inside @a data.
      * @note  This task WILL be performed in other than the GUI thread, no widget interactions! */
    virtual void saveFromCacheTo(QVariant &data) RT_OVERRIDE;

    /** Performs validation, updates @a messages list if something is wrong. */
    virtual bool validate(QList<UIValidationMessage> &messages) RT_OVERRIDE;

    /** Defines TAB order for passed @a pWidget. */
    virtual void setOrderAfter(QWidget *pWidget) RT_OVERRIDE;

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

    /** Performs final page polishing. */
    virtual void polishPage() RT_OVERRIDE;

private slots:

    /** Handles monitor count change. */
    void sltHandleMonitorCountChange();
    /** Handles Graphics Controller combo change. */
    void sltHandleGraphicsControllerComboChange();
#ifdef VBOX_WITH_3D_ACCELERATION
    /** Handles 3D Acceleration feature state change. */
    void sltHandle3DAccelerationFeatureStateChange();
#endif
    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares 'Screen' tab. */
    void prepareTabScreen();
    /** Prepares 'Remote Display' tab. */
    void prepareTabRemoteDisplay();
    /** Prepares 'Recording' tab. */
    void prepareTabRecording();
    /** Prepares connections. */
    void prepareConnections();
    /** Cleanups all. */
    void cleanup();

    /** Returns whether the VRAM requirements are important. */
    bool shouldWeWarnAboutLowVRAM();

    /** Updates guest-screen count. */
    void updateGuestScreenCount();
    /** Saves existing data from cache. */
    bool saveData();
    /** Saves existing 'Screen' data from cache. */
    bool saveScreenData();
    /** Saves existing 'Remote Display' data from cache. */
    bool saveRemoteDisplayData();
    /** Saves existing 'Recording' data from cache. */
    bool saveRecordingData();

    /** Updates minimum layout hint. */
    void updateMinimumLayoutHint();

    /** Holds the guest OS type. */
    QString m_strGuestOSTypeId;
#ifdef VBOX_WITH_3D_ACCELERATION
    /** Holds whether the guest OS supports WDDM. */
    bool          m_fWddmModeSupported;
#endif
    /** Holds recommended graphics controller type. */
    KGraphicsControllerType  m_enmGraphicsControllerTypeRecommended;

    /** Holds the page data cache instance. */
    UISettingsCacheMachineDisplay *m_pCache;

    /** @name Widgets
     * @{ */
        /** Holds the tab-widget instance. */
        QITabWidget *m_pTabWidget;

        /** Holds the 'Screen' tab instance. */
        UIEditor                      *m_pTabScreen;
        /** Holds the video memory size editor instance. */
        UIVideoMemoryEditor           *m_pEditorVideoMemorySize;
        /** Holds the monitor count spinbox instance. */
        UIMonitorCountEditor          *m_pEditorMonitorCount;
        /** Holds the scale factor editor instance. */
        UIScaleFactorEditor           *m_pEditorScaleFactor;
        /** Holds the graphics controller editor instance. */
        UIGraphicsControllerEditor    *m_pEditorGraphicsController;
#ifdef VBOX_WITH_3D_ACCELERATION
        /** Holds the display screen features editor instance. */
        UIDisplayScreenFeaturesEditor *m_pEditorDisplayScreenFeatures;
#endif

        /** Holds the 'Remote Display' tab instance. */
        UIEditor             *m_pTabRemoteDisplay;
        /** Holds the VRDE settings editor instance. */
        UIVRDESettingsEditor *m_pEditorVRDESettings;

        /** Holds the 'Recording' tab instance. */
        UIEditor                  *m_pTabRecording;
        /** Holds the Recording settings editor instance. */
        UIRecordingSettingsEditor *m_pEditorRecordingSettings;
   /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsDisplay_h */
