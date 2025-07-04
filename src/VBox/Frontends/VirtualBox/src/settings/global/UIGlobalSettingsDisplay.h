/* $Id: UIGlobalSettingsDisplay.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsDisplay class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_global_UIGlobalSettingsDisplay_h
#define FEQT_INCLUDED_SRC_settings_global_UIGlobalSettingsDisplay_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
class UIDisplayFeaturesEditor;
class UIFontScaleEditor;
class UIMaximumGuestScreenSizeEditor;
class UIScaleFactorEditor;
struct UIDataSettingsGlobalDisplay;
typedef UISettingsCache<UIDataSettingsGlobalDisplay> UISettingsCacheGlobalDisplay;

/** Global settings: Display page. */
class SHARED_LIBRARY_STUFF UIGlobalSettingsDisplay : public UISettingsPageGlobal
{
    Q_OBJECT;

public:

    /** Constructs Display settings page. */
    UIGlobalSettingsDisplay();
    /** Destructs Display settings page. */
    virtual ~UIGlobalSettingsDisplay() RT_OVERRIDE;

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

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Cleanups all. */
    void cleanup();

    /** Saves existing data from cache. */
    bool saveData();

    /** Updates minimum layout hint. */
    void updateMinimumLayoutHint();

    /** Holds the page data cache instance. */
    UISettingsCacheGlobalDisplay *m_pCache;

    /** @name Widgets
     * @{ */
        /** Holds the 'maximum guest screen size' editor instance. */
        UIMaximumGuestScreenSizeEditor *m_pEditorMaximumGuestScreenSize;
        /** Holds the 'scale-factor' editor instance. */
        UIScaleFactorEditor            *m_pEditorScaleFactor;
        /** Holds the 'font scale' editor instance. */
        UIFontScaleEditor              *m_pFontScaleEditor;
        /** Holds the 'display features' editor instance. */
        UIDisplayFeaturesEditor        *m_pEditorDisplayFeatures;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_global_UIGlobalSettingsDisplay_h */
