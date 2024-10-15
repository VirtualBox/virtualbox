/* $Id: UIMachineSettingsInterface.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsInterface class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsInterface_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsInterface_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
class QLabel;
class UIActionPool;
class UIMenuBarEditorWidget;
class UIMiniToolbarSettingsEditor;
class UIStatusBarEditorWidget;
class UIVisualStateEditor;
struct UIDataSettingsMachineInterface;
typedef UISettingsCache<UIDataSettingsMachineInterface> UISettingsCacheMachineInterface;

/** Machine settings: User Interface page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsInterface : public UISettingsPageMachine
{
    Q_OBJECT;

public:

    /** Constructs User Interface settings page. */
    UIMachineSettingsInterface(const QUuid &uMachineId);
    /** Destructs User Interface settings page. */
    virtual ~UIMachineSettingsInterface() RT_OVERRIDE;

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

    /** Performs final page polishing. */
    virtual void polishPage() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares connections. */
    void prepareConnections();
    /** Cleanups all. */
    void cleanup();

    /** Saves existing data from cache. */
    bool saveData();
    /** Saves existing 'Menu-bar' data from cache. */
    bool saveMenuBarData();
    /** Saves existing 'Status-bar' data from cache. */
    bool saveStatusBarData();
    /** Saves existing 'Mini-toolbar' data from cache. */
    bool saveMiniToolbarData();
    /** Saves existing 'Visual State' data from cache. */
    bool saveVisualStateData();

    /** Updates minimum layout hint. */
    void updateMinimumLayoutHint();

    /** Holds the machine ID copy. */
    const QUuid   m_uMachineId;
    /** Holds the action-pool instance. */
    UIActionPool *m_pActionPool;

    /** Holds the page data cache instance. */
    UISettingsCacheMachineInterface *m_pCache;

    /** @name Widgets
     * @{ */
        /** Holds the menu-bar editor instance. */
        UIMenuBarEditorWidget       *m_pEditorMenuBar;
        /** Holds the visual state editor instance. */
        UIVisualStateEditor         *m_pEditorVisualState;
        /** Holds the mini-toolbar settings editor instance. */
        UIMiniToolbarSettingsEditor *m_pEditorMiniToolabSettings;
        /** Holds the status-bar editor instance. */
        UIStatusBarEditorWidget     *m_pEditorStatusBar;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsInterface_h */
