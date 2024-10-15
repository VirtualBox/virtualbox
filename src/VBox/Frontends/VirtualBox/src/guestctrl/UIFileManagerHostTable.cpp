/* $Id: UIFileManagerHostTable.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManagerHostTable class implementation.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <QAction>
#include <QDateTime>
#include <QDir>

/* GUI includes: */
#include "QILabel.h"
#include "UIActionPool.h"
#include "UICommon.h"
#include "UIFileManager.h"
#include "UIFileTableNavigationWidget.h"
#include "UIFileSystemModel.h"
#include "UIFileManagerHostTable.h"
#include "UIPathOperations.h"
#include "QIToolBar.h"
#include "UITranslationEventListener.h"


/*********************************************************************************************************************************
*   UIHostDirectoryDiskUsageComputer definition.                                                                                 *
*********************************************************************************************************************************/

/** Open directories recursively and sum the disk usage. Don't block the GUI thread while doing this */
class UIHostDirectoryDiskUsageComputer : public UIDirectoryDiskUsageComputer
{
    Q_OBJECT;

public:

    UIHostDirectoryDiskUsageComputer(QObject *parent, QStringList strStartPath);

protected:

    virtual void directoryStatisticsRecursive(const QString &path, UIDirectoryStatistics &statistics) RT_OVERRIDE;
};


/*********************************************************************************************************************************
*   UIHostDirectoryDiskUsageComputer implementation.                                                                             *
*********************************************************************************************************************************/

UIHostDirectoryDiskUsageComputer::UIHostDirectoryDiskUsageComputer(QObject *parent, QStringList pathList)
    :UIDirectoryDiskUsageComputer(parent, pathList)
{
}

void UIHostDirectoryDiskUsageComputer::directoryStatisticsRecursive(const QString &path, UIDirectoryStatistics &statistics)
{
    /* Prevent modification of the continue flag while reading: */
    m_mutex.lock();
    /* Check if m_fOkToContinue is set to false. if so just end recursion: */
    if (!isOkToContinue())
    {
        m_mutex.unlock();
        return;
    }
    m_mutex.unlock();

    QFileInfo fileInfo(path);
    if (!fileInfo.exists())
        return;
    /* if the object is a file or a symlink then read the size and return: */
    if (fileInfo.isFile())
    {
        statistics.m_totalSize += fileInfo.size();
        ++statistics.m_uFileCount;
        sigResultUpdated(statistics);
        return;
    }
    else if (fileInfo.isSymLink())
    {
        statistics.m_totalSize += fileInfo.size();
        ++statistics.m_uSymlinkCount;
        sigResultUpdated(statistics);
        return;
    }

    /* if it is a directory then read the content: */
    QDir dir(path);
    if (!dir.exists())
        return;

    QFileInfoList entryList = dir.entryInfoList();
    for (int i = 0; i < entryList.size(); ++i)
    {
        const QFileInfo &entryInfo = entryList.at(i);
        if (entryInfo.baseName().isEmpty() || entryInfo.baseName() == "." ||
            entryInfo.baseName() == UIFileSystemModel::strUpDirectoryString)
            continue;
        statistics.m_totalSize += entryInfo.size();
        if (entryInfo.isSymLink())
            statistics.m_uSymlinkCount++;
        else if(entryInfo.isFile())
            statistics.m_uFileCount++;
        else if (entryInfo.isDir())
        {
            statistics.m_uDirectoryCount++;
            directoryStatisticsRecursive(entryInfo.absoluteFilePath(), statistics);
        }
    }
    sigResultUpdated(statistics);
}


/*********************************************************************************************************************************
*   UIFileManagerHostTable implementation.                                                                                       *
*********************************************************************************************************************************/

UIFileManagerHostTable::UIFileManagerHostTable(UIActionPool *pActionPool, QWidget *pParent /* = 0 */)
    :UIFileManagerTable(pActionPool, pParent)
    , m_pModifierActionSeparator(0)
{
    setModelFileSystem(isWindowsFileSystem());
    initializeFileTree();
    prepareToolbar();
    prepareActionConnections();
    determinePathSeparator();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIFileManagerHostTable::sltRetranslateUI);
}

/* static */ bool UIFileManagerHostTable::scanDirectory(const QString& strPath, UIFileSystemItem *parent,
                                                        QMap<QString, UIFileSystemItem*> &fileObjects)
{

    QDir directory(UIPathOperations::addTrailingDelimiters(strPath));
    /* For some reason when this filter is applied, folder content  QDir::entryInfoList()
       returns an empty list: */
    /*directory.setFilter(QDir::NoDotAndDotDot);*/
    if (!directory.exists() || !directory.isReadable())
        return false;
    QFileInfoList entries = directory.entryInfoList(QDir::Hidden|QDir::AllEntries|QDir::NoDotAndDotDot);

    parent->setIsOpened(true);
    for (int i = 0; i < entries.size(); ++i)
    {
        const QFileInfo &fileInfo = entries.at(i);
        UIFileSystemItem *item = new UIFileSystemItem(fileInfo.fileName(), parent, fileType(fileInfo));
        if (!item)
            continue;

        item->setData(fileInfo.size(),         UIFileSystemModelData_Size);
        item->setData(fileInfo.lastModified(), UIFileSystemModelData_ChangeTime);
        item->setData(fileInfo.owner(),        UIFileSystemModelData_Owner);
        item->setData(permissionString(fileInfo.permissions()),  UIFileSystemModelData_Permissions);

        /* if the item is a symlink set the target path and
           check the target if it is a directory: */
        if (fileInfo.isSymLink()) /** @todo No symlinks here on windows, while fsObjectPropertyString() does see them.  RTDirReadEx works wrt symlinks, btw. */
        {
            item->setTargetPath(fileInfo.symLinkTarget());
            item->setIsSymLinkToADirectory(QFileInfo(fileInfo.symLinkTarget()).isDir());
        }
        item->setIsHidden(fileInfo.isHidden());
        fileObjects.insert(fileInfo.fileName(), item);
        item->setIsOpened(false);
    }
    return true;
}

void UIFileManagerHostTable::sltRetranslateUI()
{
    if (m_pLocationLabel)
        m_pLocationLabel->setText(UIFileManager::tr("Host File System:"));
    m_strTableName = UIFileManager::tr("Host");
    UIFileManagerTable::sltRetranslateUI();
}

void UIFileManagerHostTable::prepareToolbar()
{
    if (m_pToolBar && m_pActionPool)
    {
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoBackward));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoForward));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoUp));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoHome));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Refresh));

        m_pModifierActionSeparator = m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Delete));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Rename));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_CreateNewDirectory));

        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Copy));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Cut));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Paste));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_SelectAll));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_InvertSelection));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_ShowProperties));

        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Delete));
        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Rename));
        m_selectionDependentActions.insert(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_ShowProperties));

        /* Hide cut, copy, and paste for now. We will implement those
           when we have an API for host file operations: */
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Copy)->setVisible(false);
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Cut)->setVisible(false);
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Paste)->setVisible(false);
    }
    setSelectionDependentActionsEnabled(false);
}

void UIFileManagerHostTable::createFileViewContextMenu(const QWidget *pWidget, const QPoint &point)
{
    if (!pWidget)
        return;

    QMenu menu;
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoUp));

    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoHome));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Refresh));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Delete));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Rename));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_CreateNewDirectory));
    // menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Copy));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Cut));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Paste));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_SelectAll));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_InvertSelection));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_ShowProperties));
    menu.exec(pWidget->mapToGlobal(point));
}

void UIFileManagerHostTable::toggleForwardBackwardActions()
{
    if (!m_pNavigationWidget)
        return;
    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoForward))
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoForward)->setEnabled(m_pNavigationWidget->canGoForward());
    if (m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoBackward))
        m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoBackward)->setEnabled(m_pNavigationWidget->canGoBackward());
}

bool UIFileManagerHostTable::isWindowsFileSystem() const
{
    return uiCommon().hostOperatingSystem().contains("windows", Qt::CaseInsensitive);
}

bool UIFileManagerHostTable::readDirectory(const QString& strPath, UIFileSystemItem *parent, bool isStartDir /*= false*/)
{
    if (!parent)
        return false;

    QMap<QString, UIFileSystemItem*> fileObjects;
    if (!scanDirectory(strPath, parent, fileObjects))
        return false;
    checkDotDot(fileObjects, parent, isStartDir);
    return true;
}

void UIFileManagerHostTable::deleteByItem(UIFileSystemItem *item)
{
    if (item->isUpDirectory())
        return;
    if (!item->isDirectory())
    {
        QDir itemToDelete;
        itemToDelete.remove(UIPathOperations::removeTrailingDelimiters(item->path()));
    }
    QDir itemToDelete(item->path());
    itemToDelete.setFilter(QDir::NoDotAndDotDot);
    /* Try to delete item recursively (in case of directories).
       note that this is no good way of deleting big directory
       trees. We need a better error reporting and a kind of progress
       indicator: */
    /** @todo replace this recursive delete by a better implementation: */
    bool deleteSuccess = itemToDelete.removeRecursively();

    if (!deleteSuccess)
        emit sigLogOutput(QString(item->path()).append(" could not be deleted"), m_strTableName, FileManagerLogType_Error);
}

void UIFileManagerHostTable::goToHomeDirectory()
{
    if (!rootItem() || rootItem()->childCount() <= 0)
        return;
    UIFileSystemItem *startDirItem = rootItem()->child(0);
    if (!startDirItem)
        return;

    QString userHome = UIPathOperations::sanitize(QDir::homePath());
    goIntoDirectory(UIPathOperations::pathTrail(userHome));
}

bool UIFileManagerHostTable::renameItem(UIFileSystemItem *item, const QString &strOldPath)
{
    if (!item || item->isUpDirectory())
        return false;
    QDir tempDir;
    if (tempDir.rename(strOldPath, item->path()))
        return true;

    return false;
}

bool UIFileManagerHostTable::createDirectory(const QString &path, const QString &directoryName)
{
    QDir parentDir(path);
    if (!parentDir.mkdir(directoryName))
    {
        emit sigLogOutput(UIPathOperations::mergePaths(path, directoryName).append(" could not be created"), m_strTableName, FileManagerLogType_Error);
        return false;
    }

    return true;
}

/* static */
KFsObjType UIFileManagerHostTable::fileType(const QFileInfo &fsInfo)
{
    if (!fsInfo.exists())
        return KFsObjType_Unknown;
    /* first check if it is symlink becacuse for Qt
       being smylin and directory/file is not mutually exclusive: */
    if (fsInfo.isSymLink())
        return KFsObjType_Symlink;
    else if (fsInfo.isFile())
        return KFsObjType_File;
    else if (fsInfo.isDir())
        return KFsObjType_Directory;
    return KFsObjType_Unknown;
}

/* static */
KFsObjType  UIFileManagerHostTable::fileType(const QString &strPath)
{
    return fileType(QFileInfo(strPath));
}

QString UIFileManagerHostTable::fsObjectPropertyString()
{
    QStringList selectedObjects = selectedItemPathList();
    if (selectedObjects.isEmpty())
        return QString();
    if (selectedObjects.size() == 1)
    {
        if (selectedObjects.at(0).isNull())
            return QString();
        QFileInfo fileInfo(selectedObjects.at(0));
        if (!fileInfo.exists())
            return QString();
        QStringList propertyStringList;
        /* Name: */
        propertyStringList << UIFileManager::tr("<b>Name:</b> %1<br/>").arg(fileInfo.fileName());
        /* Size: */
        propertyStringList << UIFileManager::tr("<b>Size:</b> %1 bytes").arg(QString::number(fileInfo.size()));
        if (fileInfo.size() >= m_iKiloByte)
            propertyStringList << QString(" (%1)").arg(humanReadableSize(fileInfo.size()));
        propertyStringList << "<br/>";
        /* Type: */
        propertyStringList << UIFileManager::tr("<b>Type:</b> %1<br/>").arg(fileTypeString(fileType(fileInfo)));
        /* Creation Date: */
        propertyStringList << UIFileManager::tr("<b>Created:</b> %1<br/>").arg(fileInfo.birthTime().toString());
        /* Last Modification Date: */
        propertyStringList << UIFileManager::tr("<b>Modified:</b> %1<br/>").arg(fileInfo.lastModified().toString());
        /* Owner: */
        propertyStringList << UIFileManager::tr("<b>Owner:</b> %1").arg(fileInfo.owner());

        return propertyStringList.join(QString());
    }

    int fileCount = 0;
    int directoryCount = 0;
    ULONG64 totalSize = 0;

    for(int i = 0; i < selectedObjects.size(); ++i)
    {
        QFileInfo fileInfo(selectedObjects.at(i));
        if (!fileInfo.exists())
            continue;
        if (fileInfo.isFile())
            ++fileCount;
        if (fileInfo.isDir())
            ++directoryCount;
        totalSize += fileInfo.size();
    }
    QStringList propertyStringList;
    propertyStringList << UIFileManager::tr("<b>Selected:</b> %1 files and %2 directories<br/>").
        arg(QString::number(fileCount)).arg(QString::number(directoryCount));
    propertyStringList << UIFileManager::tr("<b>Size:</b> %1 bytes").arg(QString::number(totalSize));
    if (totalSize >= m_iKiloByte)
        propertyStringList << QString(" (%1)").arg(humanReadableSize(totalSize));

    return propertyStringList.join(QString());
}

void  UIFileManagerHostTable::showProperties()
{
    qRegisterMetaType<UIDirectoryStatistics>();
    QString fsPropertyString = fsObjectPropertyString();
    if (fsPropertyString.isEmpty())
        return;
    if (!m_pPropertiesDialog)
        m_pPropertiesDialog = new UIPropertiesDialog(this);
    if (!m_pPropertiesDialog)
        return;

    UIHostDirectoryDiskUsageComputer *directoryThread = 0;

    QStringList selectedObjects = selectedItemPathList();
    if ((selectedObjects.size() == 1 && QFileInfo(selectedObjects.at(0)).isDir())
        || selectedObjects.size() > 1)
    {
        directoryThread = new UIHostDirectoryDiskUsageComputer(this, selectedObjects);
        if (directoryThread)
        {
            connect(directoryThread, &UIHostDirectoryDiskUsageComputer::sigResultUpdated,
                    this, &UIFileManagerHostTable::sltReceiveDirectoryStatistics/*, Qt::DirectConnection*/);
            directoryThread->start();
        }
    }
    m_pPropertiesDialog->setWindowTitle("Properties");
    m_pPropertiesDialog->setPropertyText(fsPropertyString);
    m_pPropertiesDialog->execute();
    if (directoryThread)
    {
        if (directoryThread->isRunning())
            directoryThread->stopRecursion();
        disconnect(directoryThread, &UIHostDirectoryDiskUsageComputer::sigResultUpdated,
                this, &UIFileManagerHostTable::sltReceiveDirectoryStatistics/*, Qt::DirectConnection*/);
        directoryThread->wait();
    }
}

void UIFileManagerHostTable::determineDriveLetters()
{
    QFileInfoList drive = QDir::drives();
    m_driveLetterList.clear();
    for (int i = 0; i < drive.size(); ++i)
    {
        if (UIPathOperations::doesPathStartWithDriveLetter(drive[i].filePath()))
            m_driveLetterList.push_back(drive[i].filePath());
    }
}

void UIFileManagerHostTable::determinePathSeparator()
{
    setPathSeparator(QDir::separator());
}

/* static */QString UIFileManagerHostTable::permissionString(QFileDevice::Permissions permissions)
{
    QString strPermissions;
    if (permissions & QFileDevice::ReadOwner)
        strPermissions += 'r';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::WriteOwner)
        strPermissions += 'w';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::ExeOwner)
        strPermissions += 'x';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::ReadGroup)
        strPermissions += 'r';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::WriteGroup)
        strPermissions += 'w';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::ExeGroup)
        strPermissions += 'x';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::ReadOther)
        strPermissions += 'r';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::WriteOther)
        strPermissions += 'w';
    else
        strPermissions += '-';

    if (permissions & QFileDevice::ExeOther)
        strPermissions += 'x';
    else
        strPermissions += '-';
    return strPermissions;
}

void UIFileManagerHostTable::prepareActionConnections()
{
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoUp), &QAction::triggered,
            this, &UIFileManagerTable::sltGoUp);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoHome), &QAction::triggered,
            this, &UIFileManagerTable::sltGoHome);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoForward), &QAction::triggered,
            this, &UIFileManagerTable::sltGoForward);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoBackward), &QAction::triggered,
            this, &UIFileManagerTable::sltGoBackward);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Refresh), &QAction::triggered,
            this, &UIFileManagerTable::sltRefresh);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Delete), &QAction::triggered,
            this, &UIFileManagerTable::sltDelete);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Rename), &QAction::triggered,
            this, &UIFileManagerTable::sltRename);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Copy), &QAction::triggered,
            this, &UIFileManagerTable::sltCopy);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Cut), &QAction::triggered,
            this, &UIFileManagerTable::sltCut);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Paste), &QAction::triggered,
            this, &UIFileManagerTable::sltPaste);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_SelectAll), &QAction::triggered,
            this, &UIFileManagerTable::sltSelectAll);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_InvertSelection), &QAction::triggered,
            this, &UIFileManagerTable::sltInvertSelection);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_ShowProperties), &QAction::triggered,
            this, &UIFileManagerTable::sltShowProperties);
    connect(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_CreateNewDirectory), &QAction::triggered,
            this, &UIFileManagerTable::sltCreateNewDirectory);
}

#include "UIFileManagerHostTable.moc"
