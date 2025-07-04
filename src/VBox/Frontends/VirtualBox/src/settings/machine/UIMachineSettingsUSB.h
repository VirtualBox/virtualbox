/* $Id: UIMachineSettingsUSB.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsUSB class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsUSB_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsUSB_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
class UIUSBSettingsEditor;
struct UIDataSettingsMachineUSB;
struct UIDataSettingsMachineUSBFilter;
typedef UISettingsCache<UIDataSettingsMachineUSBFilter> UISettingsCacheMachineUSBFilter;
typedef UISettingsCachePool<UIDataSettingsMachineUSB, UISettingsCacheMachineUSBFilter> UISettingsCacheMachineUSB;

/** Machine settings: USB page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsUSB : public UISettingsPageMachine
{
    Q_OBJECT;

public:

    /** Constructs USB settings page. */
    UIMachineSettingsUSB();
    /** Destructs USB settings page. */
    virtual ~UIMachineSettingsUSB() RT_OVERRIDE;

    /** Returns whether the USB is enabled. */
    bool isUSBEnabled() const;

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
    /** Removes USB controllers of passed @a types. */
    bool removeUSBControllers(const QSet<KUSBControllerType> &types = QSet<KUSBControllerType>());
    /** Creates USB controllers of passed @a enmType. */
    bool createUSBControllers(KUSBControllerType enmType);
    /** Removes USB filter at passed @a iPosition of the @a filtersObject. */
    bool removeUSBFilter(CUSBDeviceFilters &comFiltersObject, int iPosition);
    /** Creates USB filter at passed @a iPosition of the @a filtersObject using the @a filterData. */
    bool createUSBFilter(CUSBDeviceFilters &comFiltersObject, int iPosition, const UIDataSettingsMachineUSBFilter &filterData);

    /** Holds the page data cache instance. */
    UISettingsCacheMachineUSB *m_pCache;

    /** @name Widgets
     * @{ */
        /** Holds the USB settings editor instance. */
        UIUSBSettingsEditor *m_pEditorUsbSettings;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsUSB_h */
