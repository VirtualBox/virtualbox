/* $Id: UIMachineSettingsStorage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsStorage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsStorage_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsStorage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* COM includes: */
#include "KChipsetType.h"

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
class UIActionPool;
class UIStorageSettingsEditor;
struct UIDataSettingsMachineStorage;
struct UIDataSettingsMachineStorageController;
struct UIDataSettingsMachineStorageAttachment;
typedef UISettingsCache<UIDataSettingsMachineStorageAttachment> UISettingsCacheMachineStorageAttachment;
typedef UISettingsCachePool<UIDataSettingsMachineStorageController, UISettingsCacheMachineStorageAttachment> UISettingsCacheMachineStorageController;
typedef UISettingsCachePool<UIDataSettingsMachineStorage, UISettingsCacheMachineStorageController> UISettingsCacheMachineStorage;

/** Machine settings: Storage page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsStorage : public UISettingsPageMachine
{
    Q_OBJECT;

signals:

    /** Notifies listeners about storage changed. */
    void sigStorageChanged();

public:

    /** Constructs Storage settings page. */
    UIMachineSettingsStorage(UIActionPool *pActionPool);
    /** Destructs Storage settings page. */
    virtual ~UIMachineSettingsStorage() RT_OVERRIDE;

    /** Defines chipset @a enmType. */
    void setChipsetType(KChipsetType enmType);

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

    /** Defines the configuration access @a enmLevel. */
    virtual void setConfigurationAccessLevel(ConfigurationAccessLevel enmLevel) RT_OVERRIDE;

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
    /** Removes existing storage controller described by the @a controllerCache. */
    bool removeStorageController(const UISettingsCacheMachineStorageController &controllerCache);
    /** Creates existing storage controller described by the @a controllerCache. */
    bool createStorageController(const UISettingsCacheMachineStorageController &controllerCache);
    /** Updates existing storage controller described by the @a controllerCache. */
    bool updateStorageController(const UISettingsCacheMachineStorageController &controllerCache,
                                 bool fRemovingStep);
    /** Removes existing storage attachment described by the @a controllerCache and @a attachmentCache. */
    bool removeStorageAttachment(const UISettingsCacheMachineStorageController &controllerCache,
                                 const UISettingsCacheMachineStorageAttachment &attachmentCache);
    /** Creates existing storage attachment described by the @a controllerCache and @a attachmentCache. */
    bool createStorageAttachment(const UISettingsCacheMachineStorageController &controllerCache,
                                 const UISettingsCacheMachineStorageAttachment &attachmentCache);
    /** Updates existing storage attachment described by the @a controllerCache and @a attachmentCache. */
    bool updateStorageAttachment(const UISettingsCacheMachineStorageController &controllerCache,
                                 const UISettingsCacheMachineStorageAttachment &attachmentCache);
    /** Returns whether the controller described by the @a controllerCache could be updated or recreated otherwise. */
    bool isControllerCouldBeUpdated(const UISettingsCacheMachineStorageController &controllerCache) const;
    /** Returns whether the attachment described by the @a attachmentCache could be updated or recreated otherwise. */
    bool isAttachmentCouldBeUpdated(const UISettingsCacheMachineStorageAttachment &attachmentCache) const;

    /** Holds the action pool instance. */
    UIActionPool *m_pActionPool;

    /** Holds the machine ID. */
    QUuid    m_uMachineId;
    /** Holds the machine name. */
    QString  m_strMachineName;
    /** Holds the machine settings file-path. */
    QString  m_strMachineSettingsFilePath;
    /** Holds the machine guest OS type ID. */
    QString  m_strMachineGuestOSTypeId;

    /** Holds the page data cache instance. */
    UISettingsCacheMachineStorage *m_pCache;

    /** @name Widgets
      * @{ */
        /** Holds the storage settings editor instance. */
        UIStorageSettingsEditor *m_pEditorStorageSettings;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsStorage_h */
