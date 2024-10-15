/* $Id: UIMachineSettingsSF.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsSF class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSF_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSF_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UISettingsPage.h"

/* COM includes: */
#include "CSharedFolder.h"

/* Forward declarations: */
class UISharedFoldersEditor;

struct UIDataSettingsSharedFolder;
struct UIDataSettingsSharedFolders;
typedef UISettingsCache<UIDataSettingsSharedFolder> UISettingsCacheSharedFolder;
typedef UISettingsCachePool<UIDataSettingsSharedFolders, UISettingsCacheSharedFolder> UISettingsCacheSharedFolders;


/** Machine settings: Shared Folders page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsSF : public UISettingsPageMachine
{
    Q_OBJECT;

public:

    /** Constructs Shared Folders settings page. */
    UIMachineSettingsSF();
    /** Destructs Shared Folders settings page. */
    virtual ~UIMachineSettingsSF() RT_OVERRIDE;

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

    /** Returns whether the corresponding @a enmFoldersType supported. */
    bool isSharedFolderTypeSupported(UISharedFolderType enmFoldersType) const;

    /** Gathers a vector of shared folders of the passed @a enmFoldersType. */
    CSharedFolderVector getSharedFolders(UISharedFolderType enmFoldersType);
    /** Gathers a vector of shared folders of the passed @a enmFoldersType. */
    bool getSharedFolders(UISharedFolderType enmFoldersType, CSharedFolderVector &folders);
    /** Look for a folder with the the passed @a strFolderName. */
    bool getSharedFolder(const QString &strFolderName, const CSharedFolderVector &folders, CSharedFolder &comFolder);

    /** Saves existing data from cache. */
    bool saveData();
    /** Removes shared folder defined by a @a folderCache. */
    bool removeSharedFolder(const UISettingsCacheSharedFolder &folderCache);
    /** Creates shared folder defined by a @a folderCache. */
    bool createSharedFolder(const UISettingsCacheSharedFolder &folderCache);

    /** Holds the page data cache instance. */
    UISettingsCacheSharedFolders *m_pCache;

    /** @name Widgets
      * @{ */
        /** Holds the shared folders editor instance. */
        UISharedFoldersEditor *m_pEditorSharedFolders;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsSF_h */
