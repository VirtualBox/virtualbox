/* $Id: UIMachineSettingsGeneral.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsGeneral class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsGeneral_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsGeneral_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UISettingsPage.h"

/* Forward declarations: */
class QITabWidget;
class UIDescriptionEditor;
class UIDiskEncryptionSettingsEditor;
class UIDragAndDropEditor;
class UINameAndSystemEditor;
class UISharedClipboardEditor;
class UISnapshotFolderEditor;
struct UIDataSettingsMachineGeneral;
typedef UISettingsCache<UIDataSettingsMachineGeneral> UISettingsCacheMachineGeneral;

/** Machine settings: General page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsGeneral : public UISettingsPageMachine
{
    Q_OBJECT;

public:

    /** Constructs General settings page. */
    UIMachineSettingsGeneral();
    /** Destructs General settings page. */
    virtual ~UIMachineSettingsGeneral() RT_OVERRIDE;

    /** Returns the VM OS type Id. */
    QString guestOSTypeId() const;

protected:

    /** Returns whether the page content was changed. */
    virtual bool changed() const RT_OVERRIDE;

    /** Loads data into the cache from the corresponding external object(s).
      * @note This task COULD be performed in other than GUI thread. */
    virtual void loadToCacheFrom(QVariant &data) RT_OVERRIDE;
    /** Loads data into the corresponding widgets from cache,
      * @note This task SHOULD be performed in GUI thread only! */
    virtual void getFromCache() RT_OVERRIDE;

    /** Saves the data from the corresponding widgets into the cache,
      * @note This task SHOULD be performed in GUI thread only! */
    virtual void putToCache() RT_OVERRIDE;
    /** Save data from cache into the corresponding external object(s).
      * @note This task COULD be performed in other than GUI thread. */
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

    /** Handles encryption cipher change. */
    void sltHandleEncryptionCipherChanged();
    /** Handles encryption password change. */
    void sltHandleEncryptionPasswordChanged();
    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares 'Basic' tab. */
    void prepareTabBasic();
    /** Prepares 'Advanced' tab. */
    void prepareTabAdvanced();
    /** Prepares 'Description' tab. */
    void prepareTabDescription();
    /** Prepares 'Encryption' tab. */
    void prepareTabEncryption();
    /** Prepares connections. */
    void prepareConnections();
    /** Cleanups all. */
    void cleanup();

    /** Saves existing general data from cache. */
    bool saveData();
    /** Saves existing 'Basic' data from cache. */
    bool saveBasicData();
    /** Saves existing 'Advanced' data from cache. */
    bool saveAdvancedData();
    /** Saves existing 'Description' data from cache. */
    bool saveDescriptionData();
    /** Saves existing 'Encryption' data from cache. */
    bool saveEncryptionData();

    /** Updates minimum layout hint. */
    void updateMinimumLayoutHint();

    /** Holds whether the encryption cipher was changed.
      * We are holding that argument here because we do not know
      * the old <i>cipher</i> for sure to compare the new one with. */
    bool  m_fEncryptionCipherChanged;
    /** Holds whether the encryption password was changed.
      * We are holding that argument here because we do not know
      * the old <i>password</i> at all to compare the new one with. */
    bool  m_fEncryptionPasswordChanged;

    /** Holds the page data cache instance. */
    UISettingsCacheMachineGeneral *m_pCache;

    /** @name Widgets
     * @{ */
        /** Holds the tab-widget instance. */
        QITabWidget *m_pTabWidget;

        /** Holds the 'Basic' tab instance. */
        UIEditor              *m_pTabBasic;
        /** Holds the name and system editor instance. */
        UINameAndSystemEditor *m_pEditorNameAndSystem;

        /** Holds the 'Advanced' tab instance. */
        UIEditor                *m_pTabAdvanced;
        /** Holds the snapshot folder editor instance. */
        UISnapshotFolderEditor  *m_pEditorSnapshotFolder;
        /** Holds the shared clipboard editor instance. */
        UISharedClipboardEditor *m_pEditorClipboard;
        /** Holds the drag and drop editor instance. */
        UIDragAndDropEditor     *m_pEditorDragAndDrop;

        /** Holds the 'Description' tab instance. */
        UIEditor            *m_pTabDescription;
        /** Holds the description editor instance. */
        UIDescriptionEditor *m_pEditorDescription;

        /** Holds the 'Encryption' tab instance. */
        UIEditor                       *m_pTabEncryption;
        /** Holds the cipher settings editor instance. */
        UIDiskEncryptionSettingsEditor *m_pEditorDiskEncryptionSettings;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsGeneral_h */
