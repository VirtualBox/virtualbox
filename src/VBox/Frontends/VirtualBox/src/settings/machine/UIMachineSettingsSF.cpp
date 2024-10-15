/* $Id: UIMachineSettingsSF.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsSF class implementation.
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

/* Qt includes: */
#include <QVBoxLayout>

/* GUI includes: */
#include "UIErrorString.h"
#include "UIMachineSettingsSF.h"
#include "UISharedFoldersEditor.h"


/** Machine settings: Shared Folder data structure. */
struct UIDataSettingsSharedFolder
{
    /** Constructs data. */
    UIDataSettingsSharedFolder() {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsSharedFolder &other) const
    {
        return true
               && m_guiData == other.m_guiData
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsSharedFolder &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsSharedFolder &other) const { return !equal(other); }

    /** Holds the shared folder data. */
    UIDataSharedFolder  m_guiData;
};


/** Machine settings: Shared Folders page data structure. */
struct UIDataSettingsSharedFolders
{
    /** Constructs data. */
    UIDataSettingsSharedFolders() {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsSharedFolders & /* other */) const { return true; }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsSharedFolders & /* other */) const { return false; }
};


UIMachineSettingsSF::UIMachineSettingsSF()
    : m_pCache(0)
    , m_pEditorSharedFolders(0)
{
    prepare();
}

UIMachineSettingsSF::~UIMachineSettingsSF()
{
    cleanup();
}

bool UIMachineSettingsSF::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIMachineSettingsSF::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Prepare old data: */
    UIDataSettingsSharedFolders oldFoldersData;

    /* Get actual folders: */
    QMultiMap<UISharedFolderType, CSharedFolder> folders;
    /* Load machine (permanent) folders if allowed: */
    if (isSharedFolderTypeSupported(UISharedFolderType_Machine))
    {
        foreach (const CSharedFolder &folder, getSharedFolders(UISharedFolderType_Machine))
            folders.insert(UISharedFolderType_Machine, folder);
    }
    /* Load console (temporary) folders if allowed: */
    if (isSharedFolderTypeSupported(UISharedFolderType_Console))
    {
        foreach (const CSharedFolder &folder, getSharedFolders(UISharedFolderType_Console))
            folders.insert(UISharedFolderType_Console, folder);
    }

    /* For each folder type: */
    foreach (const UISharedFolderType &enmFolderType, folders.keys())
    {
        /* For each folder of current type: */
        const QList<CSharedFolder> &currentTypeFolders = folders.values(enmFolderType);
        for (int iFolderIndex = 0; iFolderIndex < currentTypeFolders.size(); ++iFolderIndex)
        {
            /* Prepare old data & cache key: */
            UIDataSettingsSharedFolder oldFolderData;
            QString strFolderKey = QString::number(iFolderIndex);

            /* Check whether folder is valid:  */
            const CSharedFolder &comFolder = currentTypeFolders.at(iFolderIndex);
            if (!comFolder.isNull())
            {
                /* Gather old data: */
                oldFolderData.m_guiData.m_enmType = enmFolderType;
                oldFolderData.m_guiData.m_strName = comFolder.GetName();
                oldFolderData.m_guiData.m_strPath = comFolder.GetHostPath();
                oldFolderData.m_guiData.m_fWritable = comFolder.GetWritable();
                oldFolderData.m_guiData.m_fAutoMount = comFolder.GetAutoMount();
                oldFolderData.m_guiData.m_strAutoMountPoint = comFolder.GetAutoMountPoint();
                /* Override folder cache key: */
                strFolderKey = oldFolderData.m_guiData.m_strName;
            }

            /* Cache old data: */
            m_pCache->child(strFolderKey).cacheInitialData(oldFolderData);
        }
    }

    /* Cache old data: */
    m_pCache->cacheInitialData(oldFoldersData);

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsSF::getFromCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pEditorSharedFolders)
        return;

    /* Load old data from cache: */
    QList<UIDataSharedFolder> folders;
    for (int iFolderIndex = 0; iFolderIndex < m_pCache->childCount(); ++iFolderIndex)
        folders << m_pCache->child(iFolderIndex).base().m_guiData;
    m_pEditorSharedFolders->setValue(folders);

    /* Polish page finally: */
    polishPage();
}

void UIMachineSettingsSF::putToCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pEditorSharedFolders)
        return;

    /* Prepare new data: */
    UIDataSettingsSharedFolders newFoldersData;

    /* Cache new data: */
    foreach (const UIDataSharedFolder &guiData, m_pEditorSharedFolders->value())
    {
        /* Gather and cache new data: */
        UIDataSettingsSharedFolder newFolderData;
        newFolderData.m_guiData = guiData;
        m_pCache->child(newFolderData.m_guiData.m_strName).cacheCurrentData(newFolderData);
    }
    m_pCache->cacheCurrentData(newFoldersData);
}

void UIMachineSettingsSF::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsSF::sltRetranslateUI()
{
}

void UIMachineSettingsSF::polishPage()
{
    /* Polish availability: */
    m_pEditorSharedFolders->setFeatureAvailable(isMachineInValidMode());
    m_pEditorSharedFolders->setFoldersAvailable(UISharedFolderType_Machine, isSharedFolderTypeSupported(UISharedFolderType_Machine));
    m_pEditorSharedFolders->setFoldersAvailable(UISharedFolderType_Console, isSharedFolderTypeSupported(UISharedFolderType_Console));
}

void UIMachineSettingsSF::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheSharedFolders;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMachineSettingsSF::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare settings editor: */
        m_pEditorSharedFolders = new UISharedFoldersEditor(this);
        if (m_pEditorSharedFolders)
        {
            addEditor(m_pEditorSharedFolders);
            pLayout->addWidget(m_pEditorSharedFolders);
        }
    }
}

void UIMachineSettingsSF::prepareConnections()
{
}

void UIMachineSettingsSF::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIMachineSettingsSF::isSharedFolderTypeSupported(UISharedFolderType enmSharedFolderType) const
{
    switch (enmSharedFolderType)
    {
        case UISharedFolderType_Machine: return isMachineInValidMode();
        case UISharedFolderType_Console: return isMachineOnline();
        default: return false;
    }
}

CSharedFolderVector UIMachineSettingsSF::getSharedFolders(UISharedFolderType enmFoldersType)
{
    /* Wrap up the getter below: */
    CSharedFolderVector folders;
    getSharedFolders(enmFoldersType, folders);
    return folders;
}

bool UIMachineSettingsSF::getSharedFolders(UISharedFolderType enmFoldersType, CSharedFolderVector &folders)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Load folders of passed type: */
    if (fSuccess)
    {
        /* Make sure folder type is supported: */
        AssertReturn(isSharedFolderTypeSupported(enmFoldersType), false);
        switch (enmFoldersType)
        {
            case UISharedFolderType_Machine:
            {
                /* Make sure machine was specified: */
                AssertReturn(!m_machine.isNull(), false);
                /* Load machine folders: */
                folders = m_machine.GetSharedFolders();
                fSuccess = m_machine.isOk();

                /* Show error message if necessary: */
                if (!fSuccess)
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));

                break;
            }
            case UISharedFolderType_Console:
            {
                /* Make sure console was specified: */
                AssertReturn(!m_console.isNull(), false);
                /* Load console folders: */
                folders = m_console.GetSharedFolders();
                fSuccess = m_console.isOk();

                /* Show error message if necessary: */
                if (!fSuccess)
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(m_console));

                break;
            }
            default:
                AssertFailedReturn(false);
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSF::getSharedFolder(const QString &strFolderName, const CSharedFolderVector &folders, CSharedFolder &comFolder)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Look for a folder with passed name: */
    for (int iFolderIndex = 0; fSuccess && iFolderIndex < folders.size(); ++iFolderIndex)
    {
        /* Get current folder: */
        const CSharedFolder &comCurrentFolder = folders.at(iFolderIndex);

        /* Get current folder name for further activities: */
        QString strCurrentFolderName;
        if (fSuccess)
        {
            strCurrentFolderName = comCurrentFolder.GetName();
            fSuccess = comCurrentFolder.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(comCurrentFolder));

        /* If that's the folder we are looking for => take it: */
        if (fSuccess && strCurrentFolderName == strFolderName)
            comFolder = folders[iFolderIndex];
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSF::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save folders settings from cache: */
    if (fSuccess && isMachineInValidMode() && m_pCache->wasChanged())
    {
        /* For each folder: */
        for (int iFolderIndex = 0; fSuccess && iFolderIndex < m_pCache->childCount(); ++iFolderIndex)
        {
            /* Get folder cache: */
            const UISettingsCacheSharedFolder &folderCache = m_pCache->child(iFolderIndex);

            /* Remove folder marked for 'remove' or 'update': */
            if (fSuccess && (folderCache.wasRemoved() || folderCache.wasUpdated()))
                fSuccess = removeSharedFolder(folderCache);

            /* Create folder marked for 'create' or 'update': */
            if (fSuccess && (folderCache.wasCreated() || folderCache.wasUpdated()))
                fSuccess = createSharedFolder(folderCache);
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSF::removeSharedFolder(const UISettingsCacheSharedFolder &folderCache)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Remove folder: */
    if (fSuccess)
    {
        /* Get folder data: */
        const UIDataSettingsSharedFolder &newFolderData = folderCache.base();
        const UISharedFolderType enmFoldersType = newFolderData.m_guiData.m_enmType;
        const QString strFolderName = newFolderData.m_guiData.m_strName;

        /* Get current folders: */
        CSharedFolderVector folders;
        if (fSuccess)
            fSuccess = getSharedFolders(enmFoldersType, folders);

        /* Search for a folder with the same name: */
        CSharedFolder comFolder;
        if (fSuccess)
            fSuccess = getSharedFolder(strFolderName, folders, comFolder);

        /* Make sure such folder really exists: */
        if (fSuccess && !comFolder.isNull())
        {
            /* Remove existing folder: */
            switch (enmFoldersType)
            {
                case UISharedFolderType_Machine:
                {
                    /* Remove existing folder: */
                    m_machine.RemoveSharedFolder(strFolderName);
                    /* Check that machine is OK: */
                    fSuccess = m_machine.isOk();
                    if (!fSuccess)
                    {
                        /* Show error message: */
                        notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
                    }
                    break;
                }
                case UISharedFolderType_Console:
                {
                    /* Remove existing folder: */
                    m_console.RemoveSharedFolder(strFolderName);
                    /* Check that console is OK: */
                    fSuccess = m_console.isOk();
                    if (!fSuccess)
                    {
                        /* Show error message: */
                        notifyOperationProgressError(UIErrorString::formatErrorInfo(m_console));
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSF::createSharedFolder(const UISettingsCacheSharedFolder &folderCache)
{
    /* Get folder data: */
    const UIDataSettingsSharedFolder &newFolderData = folderCache.data();
    const UISharedFolderType enmFoldersType = newFolderData.m_guiData.m_enmType;
    const QString strFolderName = newFolderData.m_guiData.m_strName;
    const QString strFolderPath = newFolderData.m_guiData.m_strPath;
    const bool fIsWritable = newFolderData.m_guiData.m_fWritable;
    const bool fIsAutoMount = newFolderData.m_guiData.m_fAutoMount;
    const QString strAutoMountPoint = newFolderData.m_guiData.m_strAutoMountPoint;

    /* Get current folders: */
    CSharedFolderVector folders;
    bool fSuccess = getSharedFolders(enmFoldersType, folders);

    /* Search for a folder with the same name: */
    CSharedFolder comFolder;
    if (fSuccess)
        fSuccess = getSharedFolder(strFolderName, folders, comFolder);

    /* Make sure such folder doesn't exist: */
    if (fSuccess && comFolder.isNull())
    {
        /* Create new folder: */
        switch (enmFoldersType)
        {
            case UISharedFolderType_Machine:
            {
                /* Create new folder: */
                m_machine.CreateSharedFolder(strFolderName, strFolderPath, fIsWritable, fIsAutoMount, strAutoMountPoint);
                /* Show error if the operation failed: */
                fSuccess = m_machine.isOk();
                if (!fSuccess)
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
                break;
            }
            case UISharedFolderType_Console:
            {
                /* Create new folder: */
                m_console.CreateSharedFolder(strFolderName, strFolderPath, fIsWritable, fIsAutoMount, strAutoMountPoint);
                /* Show error if the operation failed: */
                fSuccess = m_console.isOk();
                if (!fSuccess)
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(m_console));
                break;
            }
            default:
                break;
        }
    }

    /* Return result: */
    return fSuccess;
}
