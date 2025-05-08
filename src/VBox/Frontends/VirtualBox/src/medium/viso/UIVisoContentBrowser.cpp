/* $Id: UIVisoContentBrowser.cpp 109467 2025-05-08 09:04:41Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVisoContentBrowser class implementation.
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


/* Qt includes: */
#include <QApplication>
#include <QDragMoveEvent>
#include <QDir>
#include <QItemDelegate>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QMimeData>
#include <QLabel>
#include <QTableView>
#include <QTextStream>

/* GUI includes: */
#include "QIToolBar.h"
#include "UIFileSystemModel.h"
#include "UIFileTableNavigationWidget.h"
#include "UIPathOperations.h"
#include "UITranslationEventListener.h"
#include "UIVisoContentBrowser.h"

/* iprt includes: */
#include <iprt/assert.h>
#include <iprt/path.h>
#include <iprt/vfs.h>
#include <iprt/file.h>
#include <iprt/fsvfs.h>
#include <iprt/mem.h>
#include <iprt/err.h>

const qint64 iAllowedFileSize = _4K;
const char *cRemoveText = ":remove:";

struct ISOFileObject
{
    QString strName;
    KFsObjType enmObjectType;
};


static void readISODir(RTVFSDIR &hVfsDir, QList<ISOFileObject> &fileObjectList)
{
    size_t cbDirEntry = sizeof(RTDIRENTRYEX);
    PRTDIRENTRYEX pDirEntry = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntry);
    size_t cbDirEntryAlloced = cbDirEntry;
    for(;;)
    {
        if (pDirEntry)
        {
            int vrc = RTVfsDirReadEx(hVfsDir, pDirEntry, &cbDirEntry, RTFSOBJATTRADD_UNIX);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_BUFFER_OVERFLOW)
                {
                    RTMemTmpFree(pDirEntry);
                    cbDirEntryAlloced = RT_ALIGN_Z(RT_MIN(cbDirEntry, cbDirEntryAlloced) + 64, 64);
                    pDirEntry  = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
                    if (pDirEntry)
                        continue;
                    /// @todo log error
                    //rcExit = RTMsgErrorExitFailure("Out of memory (direntry buffer)");
                }
                /// @todo log error
                // else if (rc != VERR_NO_MORE_FILES)
                //     rcExit = RTMsgErrorExitFailure("RTVfsDirReadEx failed: %Rrc\n", rc);
                break;
            }
            else
            {
                ISOFileObject fileObject;

                if (RTFS_IS_DIRECTORY(pDirEntry->Info.Attr.fMode))
                    fileObject.enmObjectType =  KFsObjType_Directory;
                else
                    fileObject.enmObjectType = KFsObjType_File;
                fileObject.strName = pDirEntry->szName;
                fileObjectList << fileObject;
            }
        }
    }
    RTMemTmpFree(pDirEntry);
}

static QList<ISOFileObject> openAndReadISODir(const QString &strISOFilePath, QString strDirPath = QString())
{
    QList<ISOFileObject> fileObjectList;

    RTVFSFILE hVfsFileIso;
    int vrc = RTVfsFileOpenNormal(strISOFilePath.toUtf8().constData(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFileIso);
    if (RT_SUCCESS(vrc))
    {
        RTERRINFOSTATIC ErrInfo;
        RTVFS hVfsIso;
        vrc = RTFsIso9660VolOpen(hVfsFileIso, 0 /*fFlags*/, &hVfsIso, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(vrc))
        {
            RTVFSDIR hVfsSrcRootDir;
            vrc = RTVfsOpenRoot(hVfsIso, &hVfsSrcRootDir);
            if (RT_SUCCESS(vrc))
            {
                if (strDirPath.isEmpty())
                    readISODir(hVfsSrcRootDir, fileObjectList);
                else
                {
                    RTVFSDIR hVfsDir;
                    vrc = RTVfsDirOpenDir(hVfsSrcRootDir, strDirPath.toUtf8().constData(), 0 /* fFlags */, &hVfsDir);
                    if (RT_SUCCESS(vrc))
                    {
                        readISODir(hVfsDir, fileObjectList);
                        RTVfsDirRelease(hVfsDir);
                    }
                }

                RTVfsDirRelease(hVfsSrcRootDir);
            }
            RTVfsRelease(hVfsIso);
        }
        RTVfsFileRelease(hVfsFileIso);
    }
    return fileObjectList;
}


/*********************************************************************************************************************************
*   UIContentBrowserDelegate definition.                                                                                         *
*********************************************************************************************************************************/
/** A QItemDelegate child class to disable dashed lines drawn around selected cells in QTableViews */
class UIContentBrowserDelegate : public QItemDelegate
{

    Q_OBJECT;

public:

    UIContentBrowserDelegate(QObject *pParent)
        : QItemDelegate(pParent){}

protected:

    virtual void drawFocus ( QPainter * /*painter*/, const QStyleOptionViewItem & /*option*/, const QRect & /*rect*/ ) const  RT_OVERRIDE RT_FINAL{}
};


/*********************************************************************************************************************************
*   UIVisoContentTableView definition.                                                                                      *
*********************************************************************************************************************************/

/** An QTableView extension mainly used to handle dropeed file objects from the host browser. */
class UIVisoContentTableView : public QTableView
{
    Q_OBJECT;

signals:

    void sigNewItemsDropped(QStringList pathList);


public:

    UIVisoContentTableView(QWidget *pParent = 0);
    void dragEnterEvent(QDragEnterEvent *event) RT_OVERRIDE RT_FINAL;
    void dropEvent(QDropEvent *event) RT_OVERRIDE RT_FINAL;
    void dragMoveEvent(QDragMoveEvent *event) RT_OVERRIDE RT_FINAL;
    bool hasSelection() const;
};


/*********************************************************************************************************************************
*   UIVisoContentTableView implementation.                                                                                       *
*********************************************************************************************************************************/
UIVisoContentTableView::UIVisoContentTableView(QWidget *pParent /* = 0 */)
    :QTableView(pParent)
{
}

void UIVisoContentTableView::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

bool UIVisoContentTableView::hasSelection() const
{
    if (!selectionModel())
        return false;
    return selectionModel()->hasSelection();
}

void UIVisoContentTableView::dragEnterEvent(QDragEnterEvent *pEvent)
{
    if (pEvent->mimeData()->hasFormat("application/vnd.text.list"))
        pEvent->accept();
    else
        pEvent->ignore();
}

void UIVisoContentTableView::dropEvent(QDropEvent *pEvent)
{
    if (pEvent->mimeData()->hasFormat("application/vnd.text.list"))
    {
        QByteArray itemData = pEvent->mimeData()->data("application/vnd.text.list");
        QDataStream stream(&itemData, QIODevice::ReadOnly);
        QStringList pathList;

        while (!stream.atEnd()) {
            QString text;
            stream >> text;
            pathList << text;
        }
        emit sigNewItemsDropped(pathList);
    }
}


/*********************************************************************************************************************************
*   UIVisoContentBrowser implementation.                                                                                         *
*********************************************************************************************************************************/

UIVisoContentBrowser::UIVisoContentBrowser(UIActionPool *pActionPool, QWidget *pParent)
    : QWidget(pParent)
    , m_pTableView(0)
    , m_pModel(0)
    , m_pProxyModel(0)
    , m_pMainLayout(0)
    , m_pToolBar(0)
    , m_pNavigationWidget(0)
    , m_pFileTableLabel(0)
    , m_pActionPool(pActionPool)
    , m_pRemoveAction(0)
    , m_pRestoreAction(0)
    , m_pCreateNewDirectoryAction(0)
    , m_pRenameAction(0)
    , m_pResetAction(0)
    , m_pGoUp(0)
    , m_pGoForward(0)
    , m_pGoBackward(0)
{
    prepareObjects();
    prepareToolBar();
    prepareConnections();
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVisoContentBrowser::sltRetranslateUI);

    /* Assuming the root items only child is the one with the path '/', navigate into it. */
    /* Hack alert. for some reason without invalidating proxy models mapFromSource return invalid index. */
    if (m_pProxyModel)
        m_pProxyModel->invalidate();

    if (rootItem() && rootItem()->childCount() > 0)
    {
        UIFileSystemItem *pStartItem = startItem();
        if (pStartItem)
        {
            QModelIndex iindex = m_pModel->index(pStartItem);
            if (iindex.isValid())
                tableViewItemDoubleClick(convertIndexToTableIndex(iindex));
        }
    }
    updateNavigationWidgetPath(currentPath());
}

UIVisoContentBrowser::~UIVisoContentBrowser()
{
}

void UIVisoContentBrowser::importISOContentToViso(const QString &strISOFilePath,
                                                  UIFileSystemItem *pParentItem /* = 0 */,
                                                  const QString &strDirPath /* = QString() */)
{
    if (!pParentItem)
    {
        pParentItem = startItem();
        setTableRootIndex(m_pModel->index(pParentItem));
    }
    if (!m_pTableView || !pParentItem)
        return;
    if (pParentItem->isOpened())
        return;
    /* If this is not the root directory add an "up" file object explicity since RTVfsDirReadEx does not return one:*/
    if (!strDirPath.isEmpty())
    {
        UIFileSystemItem* pAddedItem = new UIFileSystemItem(UIFileSystemModel::strUpDirectoryString,
                                                                        pParentItem,
                                                                        KFsObjType_Directory);
        pAddedItem->setData(strISOFilePath, UIFileSystemModelData_ISOFilePath);
    }
    QList<ISOFileObject> objectList = openAndReadISODir(strISOFilePath, strDirPath);

    /* Update import ISO path and effectively add it to VISO file content if ISO reading has succeeds: */
    if (!objectList.isEmpty())
        setImportedISOPath(strISOFilePath);

    for (int i = 0; i < objectList.size(); ++i)
    {
        if (objectList[i].strName == "." || objectList[i].strName == "..")
            continue;

        QFileInfo fileInfo(objectList[i].strName);
        if (pParentItem->child(fileInfo.fileName()))
            continue;

        UIFileSystemItem* pAddedItem = new UIFileSystemItem(fileInfo.fileName(), pParentItem,
                                                                        objectList[i].enmObjectType);

        QString path = UIPathOperations::mergePaths(pParentItem->path(), fileInfo.fileName());
        pAddedItem->setData(path, UIFileSystemModelData_LocalPath);
        pAddedItem->setData(strISOFilePath, UIFileSystemModelData_ISOFilePath);
        pAddedItem->setIsOpened(false);

    }
    if (m_pProxyModel)
        m_pProxyModel->invalidate();
    pParentItem->setIsOpened(true);
    emit sigISOContentImportedOrRemoved(true /* imported*/);
}

void UIVisoContentBrowser::removeISOContentFromViso()
{
    UIFileSystemItem* pParentItem = startItem();
    AssertReturnVoid(pParentItem);
    AssertReturnVoid(m_pModel);

    QList<UIFileSystemItem*> itemsToDelete;
    /* Delete all children of startItem that were imported from an ISO: */
    for (int i = 0; i < pParentItem->childCount(); ++i)
    {
        UIFileSystemItem* pItem = pParentItem->child(i);
        if (!pItem || pItem->data(UIFileSystemModelData_ISOFilePath).toString().isEmpty())
            continue;
        itemsToDelete << pItem;
    }

    foreach (UIFileSystemItem *pItem, itemsToDelete)
            m_pModel->deleteItem(pItem);
    if (m_pProxyModel)
        m_pProxyModel->invalidate();

    setImportedISOPath();
    emit sigISOContentImportedOrRemoved(false /* imported*/);
}

void UIVisoContentBrowser::addObjectsToViso(const QStringList &pathList)
{
    if (!m_pTableView)
        return;

    /* Insert items to the current directory shown in the table view: */
    QModelIndex parentIndex = m_pProxyModel->mapToSource(m_pTableView->rootIndex());
    if (!parentIndex.isValid())
         return;

    UIFileSystemItem *pParentItem = static_cast<UIFileSystemItem*>(parentIndex.internalPointer());
    if (!pParentItem)
        return;

    foreach (const QString &strPath, pathList)
    {
        QFileInfo fileInfo(strPath);
        if (!fileInfo.exists())
            continue;
        if (pParentItem->child(fileInfo.fileName()))
            continue;

        UIFileSystemItem* pAddedItem = new UIFileSystemItem(fileInfo.fileName(), pParentItem,
                                                                        fileType(fileInfo));
        pAddedItem->setData(strPath, UIFileSystemModelData_LocalPath);

        pAddedItem->setIsOpened(false);
        if (fileInfo.isSymLink())
        {
            pAddedItem->setTargetPath(fileInfo.symLinkTarget());
            pAddedItem->setIsSymLinkToADirectory(QFileInfo(fileInfo.symLinkTarget()).isDir());
        }
        createVisoEntry(pAddedItem->path(), pAddedItem->data(UIFileSystemModelData_LocalPath).toString(), false);
    }
    if (m_pProxyModel)
        m_pProxyModel->invalidate();
}

void UIVisoContentBrowser::createVisoEntry(const QString &strPath, const QString &strLocalPath, bool bRemove /* = false */)
{
    if (strPath.isEmpty())
        return;

    if (!bRemove)
    {
        if(strLocalPath.isEmpty())
            return;
        m_entryMap.insert(strPath, strLocalPath);
    }
    else
        m_entryMap.insert(strPath, cRemoveText);
}

/* static */
QString UIVisoContentBrowser::sanitizePath(const QString &strOriginal)
{
    QString strNew(strOriginal);
    strNew.replace("\"", "\\\"");
    strNew.append("\"");
    strNew.prepend("\"");
    return strNew;
}

QStringList UIVisoContentBrowser::entryList()
{
    QStringList entryList;
    for (QMap<QString, QString>::const_iterator iterator = m_entryMap.begin(); iterator != m_entryMap.end(); ++iterator)
    {
        if (iterator.value().isEmpty())
            continue;
        QString strEntry = QString("%1=%2").arg(iterator.key()).arg(iterator.value());
        entryList << sanitizePath(strEntry);
    }
    return entryList;
}

void UIVisoContentBrowser::sltRetranslateUI()
{
    UIFileSystemItem *pRootItem = rootItem();
    if (pRootItem)
    {
        pRootItem->setData(QApplication::translate("UIVisoCreatorWidget", "Name"), UIFileSystemModelData_Name);
        pRootItem->setData(QApplication::translate("UIVisoCreatorWidget", "Size"), UIFileSystemModelData_Size);
        pRootItem->setData(QApplication::translate("UIVisoCreatorWidget", "Change Time"), UIFileSystemModelData_ChangeTime);
        pRootItem->setData(QApplication::translate("UIVisoCreatorWidget", "Owner"), UIFileSystemModelData_Owner);
        pRootItem->setData(QApplication::translate("UIVisoCreatorWidget", "Permissions"), UIFileSystemModelData_Permissions);
        pRootItem->setData(QApplication::translate("UIVisoCreatorWidget", "Local Path"), UIFileSystemModelData_LocalPath);
        pRootItem->setData(QApplication::translate("UIVisoCreatorWidget", "Has Removed Child"), UIFileSystemModelData_DescendantRemovedFromVISO);
    }
    if (m_pSubMenu)
        m_pSubMenu->setTitle(QApplication::translate("UIVisoCreatorWidget", "VISO Browser"));

    setFileTableLabelText(QApplication::translate("UIVisoCreatorWidget","VISO Content"));
}

void UIVisoContentBrowser::tableViewItemDoubleClick(const QModelIndex &index)
{
    if (!index.isValid() || !m_pProxyModel)
        return;
    UIFileSystemItem *pClickedItem =
        static_cast<UIFileSystemItem*>(m_pProxyModel->mapToSource(index).internalPointer());
    if (!pClickedItem)
        return;
    if (!pClickedItem->isDirectory() && !pClickedItem->isSymLinkToADirectory())
        return;
    /* Don't navigate into removed directories: */
    if (pClickedItem->isRemovedFromViso())
        return;
    QString strISOPath = pClickedItem->data(UIFileSystemModelData_ISOFilePath).toString();
    if (pClickedItem->isUpDirectory())
        goUp();
    else if (!strISOPath.isEmpty())
    {
        importISOContentToViso(strISOPath, pClickedItem, pClickedItem->data(UIFileSystemModelData_LocalPath).toString());
        setTableRootIndex(index);
    }
    else
    {
        scanHostDirectory(pClickedItem, false /* not recursive */);
        setTableRootIndex(index);
    }
}

void UIVisoContentBrowser::sltCreateNewDirectory()
{
    if (!m_pTableView)
        return;
    QString strBaseName("NewDirectory");
    QString strNewDirectoryName(strBaseName);
    QStringList currentListing = currentDirectoryListing();
    int iSuffix = 1;
    while (currentListing.contains(strNewDirectoryName))
        strNewDirectoryName = QString("%1_%2").arg(strBaseName).arg(QString::number(iSuffix++));

    QModelIndex parentIndex = m_pProxyModel->mapToSource(m_pTableView->rootIndex());
    if (!parentIndex.isValid())
         return;

    UIFileSystemItem *pParentItem = static_cast<UIFileSystemItem*>(parentIndex.internalPointer());
    if (!pParentItem)
        return;

    /*  Check to see if we already have a directory named strNewDirectoryName: */
    const QList<UIFileSystemItem*> children = pParentItem->children();
    foreach (const UIFileSystemItem *item, children)
    {
        if (item->fileObjectName() == strNewDirectoryName)
            return;
    }

    UIFileSystemItem* pAddedItem = new UIFileSystemItem(strNewDirectoryName, pParentItem,
                                                                    KFsObjType_Directory);

    pAddedItem->setIsOpened(false);
    if (m_pProxyModel)
        m_pProxyModel->invalidate();

    renameFileObject(pAddedItem);
}

void UIVisoContentBrowser::sltRemoveItems()
{
    removeItems(tableSelectedItems());
}

void UIVisoContentBrowser::sltRestoreItems()
{
    restoreItems(tableSelectedItems());
}

void UIVisoContentBrowser::removeItems(const QList<UIFileSystemItem*> itemList)
{
    AssertReturnVoid(m_pModel);
    AssertReturnVoid(m_pProxyModel);
    foreach(UIFileSystemItem *pItem, itemList)
    {
        if (!pItem || pItem->isUpDirectory())
            continue;
        if (pItem->isRemovedFromViso())
            continue;
        QString strVisoPath = pItem->path();
        if (strVisoPath.isEmpty())
            continue;

        bool bFoundInMap = m_entryMap.remove(strVisoPath) > 0;
        if (!bFoundInMap)
            createVisoEntry(pItem->path(), pItem->data(UIFileSystemModelData_LocalPath).toString(), true /* bool bRemove */);

        markRemovedUnremovedItemParents(pItem, true);
        m_pModel->deleteItem(pItem);
    }

    m_pProxyModel->invalidate();
}

void UIVisoContentBrowser::restoreItems(const QList<UIFileSystemItem*> itemList)
{
    foreach(UIFileSystemItem *pItem, itemList)
    {
        if (!pItem || pItem->isUpDirectory())
            continue;
        if (!pItem->isRemovedFromViso())
            continue;
        QString strVisoPath = pItem->path();
        if (strVisoPath.isEmpty())
            continue;

        bool bFoundInMap = m_entryMap.remove(strVisoPath) > 0;
        if (!bFoundInMap)
            createVisoEntry(pItem->path(), pItem->data(UIFileSystemModelData_LocalPath).toString(), false /* bool bRemove */);

        markRemovedUnremovedItemParents(pItem, false);
    }
    if (m_pProxyModel)
        m_pProxyModel->invalidate();
}

void UIVisoContentBrowser::markRemovedUnremovedItemParents(UIFileSystemItem *pItem, bool fRemoved)
{
    pItem->setRemovedFromViso(fRemoved);
    UIFileSystemItem *pRoot = rootItem();
    UIFileSystemItem *pParent = pItem->parentItem();

    while (pParent && pParent != pRoot)
    {
        pParent->setToolTip(QApplication::translate("UIVisoCreatorWidget", "Child/children removed"));
        pParent->setData(true, UIFileSystemModelData_DescendantRemovedFromVISO);
        pParent = pParent->parentItem();
    }
}

void UIVisoContentBrowser::prepareObjects()
{
    m_pMainLayout = new QGridLayout;
    AssertPtrReturnVoid(m_pMainLayout);
    setLayout(m_pMainLayout);

    QHBoxLayout *pTopLayout = new QHBoxLayout;
    AssertPtrReturnVoid(pTopLayout);

    m_pToolBar = new QIToolBar;
    m_pNavigationWidget = new UIFileTableNavigationWidget;
    m_pFileTableLabel = new QLabel;

    AssertReturnVoid(m_pToolBar);
    AssertReturnVoid(m_pNavigationWidget);
    AssertReturnVoid(m_pFileTableLabel);

    pTopLayout->addWidget(m_pFileTableLabel);
    pTopLayout->addWidget(m_pNavigationWidget);

    m_pMainLayout->addWidget(m_pToolBar, 0, 0, 1, 4);
    m_pMainLayout->addLayout(pTopLayout, 1, 0, 1, 4);

    m_pMainLayout->setRowStretch(2, 2);


    m_pModel = new UIFileSystemModel(this);
    m_pProxyModel = new UIFileSystemProxyModel(this);
    if (m_pProxyModel)
    {
        m_pProxyModel->setSourceModel(m_pModel);
        m_pProxyModel->setListDirectoriesOnTop(true);
    }

    initializeModel();

    m_pTableView = new UIVisoContentTableView;
    AssertReturnVoid(m_pTableView);
    m_pMainLayout->addWidget(m_pTableView, 2, 0, 6, 4);
    m_pTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pTableView->setShowGrid(false);
    m_pTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pTableView->setAlternatingRowColors(true);
    m_pTableView->setTabKeyNavigation(false);
    m_pTableView->setItemDelegate(new UIContentBrowserDelegate(this));

    QHeaderView *pVerticalHeader = m_pTableView->verticalHeader();
    AssertReturnVoid(pVerticalHeader);

    m_pTableView->verticalHeader()->setVisible(false);
    /* Minimize the row height: */
    m_pTableView->verticalHeader()->setDefaultSectionSize(m_pTableView->verticalHeader()->minimumSectionSize());

    QHeaderView *pHorizontalHeader = m_pTableView->horizontalHeader();
    AssertReturnVoid(pHorizontalHeader);

    pHorizontalHeader->setHighlightSections(false);
    pHorizontalHeader->setSectionResizeMode(QHeaderView::Stretch);

    m_pTableView->setModel(m_pProxyModel);
    setTableRootIndex();
    m_pTableView->hideColumn(UIFileSystemModelData_Owner);
    m_pTableView->hideColumn(UIFileSystemModelData_Permissions);
    m_pTableView->hideColumn(UIFileSystemModelData_Size);
    m_pTableView->hideColumn(UIFileSystemModelData_ChangeTime);
    m_pTableView->hideColumn(UIFileSystemModelData_ISOFilePath);
    m_pTableView->hideColumn(UIFileSystemModelData_RemovedFromVISO);

    m_pTableView->setSortingEnabled(true);
    m_pTableView->sortByColumn(0, Qt::AscendingOrder);

    m_pTableView->setDragEnabled(false);
    m_pTableView->setAcceptDrops(true);
    m_pTableView->setDropIndicatorShown(true);
    m_pTableView->setDragDropMode(QAbstractItemView::DropOnly);
    m_pTableView->setMouseTracking(true);
}

void UIVisoContentBrowser::prepareToolBar()
{
    m_pRemoveAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_Remove);
    m_pRestoreAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_Restore);
    m_pCreateNewDirectoryAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_CreateNewDirectory);
    m_pRenameAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_Rename);
    m_pResetAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_Reset);
    m_pGoUp = m_pActionPool->action(UIActionIndex_M_VISOCreator_VisoContent_GoUp);
    m_pGoForward = m_pActionPool->action(UIActionIndex_M_VISOCreator_VisoContent_GoForward);
    m_pGoBackward = m_pActionPool->action(UIActionIndex_M_VISOCreator_VisoContent_GoBackward);

    AssertReturnVoid(m_pRemoveAction);
    AssertReturnVoid(m_pRestoreAction);
    AssertReturnVoid(m_pCreateNewDirectoryAction);
    AssertReturnVoid(m_pRenameAction);
    AssertReturnVoid(m_pResetAction);
    AssertReturnVoid(m_pToolBar);
    AssertReturnVoid(m_pGoUp);
    AssertReturnVoid(m_pGoForward);
    AssertReturnVoid(m_pGoBackward);

    m_pRemoveAction->setEnabled(m_pTableView->hasSelection());
    m_pRestoreAction->setEnabled(m_pTableView->hasSelection());
    m_pRenameAction->setEnabled(m_pTableView->hasSelection());

    m_pToolBar->addAction(m_pGoBackward);
    m_pToolBar->addAction(m_pGoForward);
    m_pToolBar->addAction(m_pGoUp);
    m_pToolBar->addSeparator();
    m_pToolBar->addAction(m_pRemoveAction);
    m_pToolBar->addAction(m_pRestoreAction);
    m_pToolBar->addAction(m_pCreateNewDirectoryAction);
    m_pToolBar->addAction(m_pRenameAction);
    m_pToolBar->addAction(m_pResetAction);

    enableForwardBackwardActions();
}

void UIVisoContentBrowser::prepareMainMenu(QMenu *pMenu)
{
    AssertReturnVoid(pMenu);
    QMenu *pSubMenu = new QMenu(QApplication::translate("UIVisoCreatorWidget", "VISO Browser"), pMenu);
    pMenu->addMenu(pSubMenu);
    AssertReturnVoid(pSubMenu);
    m_pSubMenu = pSubMenu;

    m_pSubMenu->addAction(m_pGoBackward);
    m_pSubMenu->addAction(m_pGoForward);
    m_pSubMenu->addAction(m_pGoUp);

    m_pSubMenu->addSeparator();

    m_pSubMenu->addAction(m_pRemoveAction);
    m_pSubMenu->addAction(m_pRestoreAction);
    m_pSubMenu->addAction(m_pRenameAction);
    m_pSubMenu->addAction(m_pCreateNewDirectoryAction);
    m_pSubMenu->addAction(m_pResetAction);
}

const QString &UIVisoContentBrowser::importedISOPath() const
{
    return m_strImportedISOPath;
}

void UIVisoContentBrowser::setImportedISOPath(const QString &strPath /* = QString() */)
{
    if (m_strImportedISOPath == strPath)
        return;
    m_strImportedISOPath = strPath;
}

bool UIVisoContentBrowser::hasContent() const
{
    if (m_strImportedISOPath.isEmpty() && m_entryMap.isEmpty())
        return false;
    return true;
}

void UIVisoContentBrowser::prepareConnections()
{
    if (m_pNavigationWidget)
        connect(m_pNavigationWidget, &UIFileTableNavigationWidget::sigPathChanged,
                this, &UIVisoContentBrowser::sltNavigationWidgetPathChange);

    if (m_pTableView)
    {
        connect(m_pTableView, &UIVisoContentTableView::doubleClicked,
                this, &UIVisoContentBrowser::sltTableViewItemDoubleClick);
        connect(m_pTableView, &UIVisoContentTableView::sigNewItemsDropped,
                this, &UIVisoContentBrowser::sltDroppedItems);
        connect(m_pTableView, &QTableView::customContextMenuRequested,
                this, &UIVisoContentBrowser::sltShowContextMenu);
    }

    if (m_pTableView->selectionModel())
        connect(m_pTableView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &UIVisoContentBrowser::sltTableSelectionChanged);
    if (m_pModel)
        connect(m_pModel, &UIFileSystemModel::sigItemRenamed,
                this, &UIVisoContentBrowser::sltItemRenameAttempt);

    if (m_pCreateNewDirectoryAction)
        connect(m_pCreateNewDirectoryAction, &QAction::triggered,
                this, &UIVisoContentBrowser::sltCreateNewDirectory);
    if (m_pRemoveAction)
        connect(m_pRemoveAction, &QAction::triggered,
                this, &UIVisoContentBrowser::sltRemoveItems);
    if (m_pRestoreAction)
        connect(m_pRestoreAction, &QAction::triggered,
                this, &UIVisoContentBrowser::sltRestoreItems);
    if (m_pResetAction)
        connect(m_pResetAction, &QAction::triggered,
                this, &UIVisoContentBrowser::sltResetAction);
    if (m_pRenameAction)
        connect(m_pRenameAction, &QAction::triggered,
                this,&UIVisoContentBrowser::sltItemRenameAction);

    if (m_pGoUp)
        connect(m_pGoUp, &QAction::triggered, this, &UIVisoContentBrowser::sltGoUp);
    if (m_pGoForward)
        connect(m_pGoForward, &QAction::triggered, this, &UIVisoContentBrowser::sltGoForward);
    if (m_pGoBackward)
        connect(m_pGoBackward, &QAction::triggered, this, &UIVisoContentBrowser::sltGoBackward);
}

UIFileSystemItem* UIVisoContentBrowser::rootItem()
{
    if (!m_pModel)
        return 0;
    return m_pModel->rootItem();
}

UIFileSystemItem* UIVisoContentBrowser::startItem()
{
    UIFileSystemItem* pRoot = rootItem();

    if (!pRoot || pRoot->childCount() <= 0)
        return 0;
    return pRoot->child(0);
}

void UIVisoContentBrowser::initializeModel()
{
    if (m_pModel)
        m_pModel->reset();
    if (!rootItem())
        return;

    const QString startPath = QString("/");

    UIFileSystemItem *pStartItem = new UIFileSystemItem(startPath, rootItem(), KFsObjType_Directory);

    pStartItem->setIsOpened(false);
}

void UIVisoContentBrowser::setTableRootIndex(QModelIndex index /* = QModelIndex */)
{
    if (!m_pTableView || !index.isValid())
        return;

    QModelIndex tableIndex;
    tableIndex = convertIndexToTableIndex(index);
    if (tableIndex.isValid())
        m_pTableView->setRootIndex(tableIndex);
    updateNavigationWidgetPath(currentPath());
    if (m_pGoUp)
        m_pGoUp->setEnabled(!onStartItem());
    m_pTableView->clearSelection();
    enableDisableSelectionDependentActions();
}

void UIVisoContentBrowser::setPathFromNavigationWidget(const QString &strPath)
{
    if (strPath == currentPath())
        return;
    UIFileSystemItem *pItem = searchItemByPath(strPath);

    if (pItem && pItem->isDirectory() && !pItem->isRemovedFromViso())
    {
        QModelIndex index = convertIndexToTableIndex(m_pModel->index(pItem));
        if (index.isValid())
            setTableRootIndex(index);
    }
}

UIFileSystemItem* UIVisoContentBrowser::searchItemByPath(const QString &strPath)
{
    UIFileSystemItem *pItem = startItem();
    QStringList path = UIPathOperations::pathTrail(strPath);

    foreach (const QString strName, path)
    {
        if (!pItem)
            return 0;
        pItem = pItem->child(strName);
    }
    return pItem;
}

void UIVisoContentBrowser::showHideHiddenObjects(bool bShow)
{
    Q_UNUSED(bShow);
}

void UIVisoContentBrowser::parseVisoFileContent(const QString &strFileName)
{
    sltResetAction();
    QFile file(strFileName);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    if (file.size() > iAllowedFileSize)
        return;
    QTextStream stream(&file);
    QString strFileContent = stream.readAll();
    QMap<QString, QString> fileEntries;
    strFileContent.replace('\'', '\"');
    bool fInsideQuotes = false;
    /* Replace spaces with new lines expect those within quotes: */
    for (int i = 0; i < strFileContent.length(); ++i)
    {
        if (strFileContent[i] == '\"')
            fInsideQuotes = !fInsideQuotes;
        if (strFileContent[i] == ' ' && !fInsideQuotes)
            strFileContent[i] = '\n';
    }
    QStringList removedEntries;
    QStringList lines = strFileContent.split('\n', Qt::SkipEmptyParts);
    for (int i = 0; i < lines.size(); ++i)
    {
        const QString &strLine = lines[i];
        if (strLine.count('=') == 1)
        {
            /* file-mode and dir-mode. something for later to deal with: */
            if (strLine.contains("file-mode") || strLine.contains("dir-mode"))
                continue;
            QStringList lineParts = strLine.split('=');
            if (lineParts.size() == 2)
            {
                if (lineParts[1].contains("must-remove"))
                {
                    removedEntries << lineParts[0];
                }
                else
                {
                    lineParts[0].remove('\"');
                    lineParts[1].remove('\"');
                    /* Check if target file structure exists: */
                    if (QFileInfo(lineParts[1]).exists())
                        fileEntries[lineParts[0]] = lineParts[1];
                }
            }
            continue;
        }
        if (strLine.contains("import-iso") && i + 1 < lines.size())
        {
            if (QFileInfo(lines[i+1]).exists())
                importISOContentToViso(lines[i+1]);
        }
    }
    createLoadedFileEntries(fileEntries);
    processRemovedEntries(removedEntries);
}

void UIVisoContentBrowser::createLoadedFileEntries(const QMap<QString, QString> &fileEntries)
{
    for (QMap<QString, QString>::const_iterator iterator = fileEntries.begin();
         iterator != fileEntries.end(); ++iterator)
    {
        QStringList pathList = UIPathOperations::pathTrail(iterator.key());
        QString strPath;
        const QString &strLocalPath = iterator.value();
        QFileInfo localFileObjectInfo(strLocalPath);
        if (!localFileObjectInfo.exists())
            continue;

        UIFileSystemItem *pParent = startItem();
        /* Make sure all the parents from start item until the immediate parent are created: */
        for (int i = 0; i < pathList.size(); ++i)
        {
            strPath.append("/");
            strPath.append(pathList[i]);

            UIFileSystemItem *pItem = searchItemByPath(strPath);
            KFsObjType enmObjectType;
            /* All objects, except possibly the last one, are directories:*/
            if (i == pathList.size() - 1)
                enmObjectType = fileType(localFileObjectInfo);
            else
                enmObjectType = KFsObjType_Directory;
            if (!pItem)
            {
                pItem = new UIFileSystemItem(pathList[i], pParent, enmObjectType);
                if (!pItem)
                    continue;

                if (i == pathList.size() - 1)
                    pItem->setData(strLocalPath, UIFileSystemModelData_LocalPath);
                /* Pre-scan and populate the directory since we may need its content while processing removed items: */
                if (enmObjectType == KFsObjType_Directory)
                    scanHostDirectory(pItem, false  /* recursive */);
            }
            if (i == pathList.size() - 1)
                createVisoEntry(pItem->path(), pItem->data(UIFileSystemModelData_LocalPath).toString(), false);
            pParent = pItem;
        }
    }

    if (m_pProxyModel)
        m_pProxyModel->invalidate();

}

void UIVisoContentBrowser::processRemovedEntries(const QStringList &removedEntries)
{
    QList<UIFileSystemItem*> itemList;
    foreach (const QString &strPath, removedEntries)
    {
        QFileInfo fileInfo(strPath);
        UIFileSystemItem *pItem = searchItemByPath(strPath);
        if (pItem)
            itemList << pItem;
    }
    removeItems(itemList);
}

QModelIndex UIVisoContentBrowser::convertIndexToTableIndex(const QModelIndex &index)
{
    if (!index.isValid())
        return QModelIndex();

    if (index.model() == m_pProxyModel)
        return index;
    else if (index.model() == m_pModel)
        return m_pProxyModel->mapFromSource(index);
    return QModelIndex();
}

void UIVisoContentBrowser::scanHostDirectory(UIFileSystemItem *directoryItem, bool fRecursive)
{
    if (!directoryItem)
        return;
    /* the clicked item can be a directory created with the VISO content. in that case local path data
       should be empty: */
    if ((directoryItem->type() != KFsObjType_Directory && !directoryItem->isSymLinkToADirectory()) ||
        directoryItem->data(UIFileSystemModelData_LocalPath).toString().isEmpty())
        return;
    QDir directory(directoryItem->data(UIFileSystemModelData_LocalPath).toString());
    if (directory.exists() && !directoryItem->isOpened())
    {
        QFileInfoList directoryContent = directory.entryInfoList();

        for (int i = 0; i < directoryContent.size(); ++i)
        {
            const QFileInfo &fileInfo = directoryContent[i];
            if (fileInfo.fileName() == ".")
                continue;
            KFsObjType enmType = fileType(fileInfo);
            UIFileSystemItem *newItem = new UIFileSystemItem(fileInfo.fileName(),
                                                                         directoryItem,
                                                                         enmType);
            newItem->setData(fileInfo.filePath(), UIFileSystemModelData_LocalPath);
            if (fileInfo.isSymLink())
            {
                newItem->setTargetPath(fileInfo.symLinkTarget());
                newItem->setIsSymLinkToADirectory(QFileInfo(fileInfo.symLinkTarget()).isDir());
            }
            /* Do not recurse into sysmlinks since it may end up infinite recursion: */
            if (fRecursive && enmType == KFsObjType_Directory && !newItem->isUpDirectory())
                scanHostDirectory(newItem, true);
        }
        directoryItem->setIsOpened(true);
    }
}

/* static */ KFsObjType UIVisoContentBrowser::fileType(const QFileInfo &fsInfo)
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

void UIVisoContentBrowser::updateStartItemName()
{
    UIFileSystemItem *pRoot = rootItem();
    if (!pRoot)
        return;
    UIFileSystemItem *pChild = pRoot->child(0);
    if (!pChild)
        return;
    const QString strName(QDir::toNativeSeparators("/"));

    pChild->setData(strName, UIFileSystemModelData_Name);
    /* If the table root index is the start item then we have to update the location selector text here: */
    // if (m_pProxyModel->mapToSource(m_pTableView->rootIndex()).internalPointer() == pChild)
    //     updateLocationSelectorText(strName);
    m_pProxyModel->invalidate();
}

void UIVisoContentBrowser::renameFileObject(UIFileSystemItem *pItem)
{
    m_pTableView->edit(m_pProxyModel->mapFromSource(m_pModel->index(pItem)));
}

void UIVisoContentBrowser::sltItemRenameAction()
{
    QList<UIFileSystemItem*> selectedItems = tableSelectedItems();
    if (selectedItems.empty())
        return;
    renameFileObject(selectedItems.at(0));
}

void UIVisoContentBrowser::sltItemRenameAttempt(UIFileSystemItem *pItem, const QString &strOldPath,
                                                const QString &strOldName, const QString &strNewName)
{
    if (!pItem || !pItem->parentItem())
        return;
    QList<UIFileSystemItem*> children = pItem->parentItem()->children();
    bool bDuplicate = false;
    foreach (const UIFileSystemItem *item, children)
    {
        if (item->fileObjectName() == strNewName && item != pItem)
            bDuplicate = true;
    }

    QString strNewPath = UIPathOperations::mergePaths(pItem->parentItem()->path(), pItem->fileObjectName());

    if (bDuplicate)
    {
        /* Restore the previous name in case the @strNewName is a duplicate: */
        pItem->setData(strOldName, static_cast<int>(UIFileSystemModelData_Name));
    }
    else
    {
        /* In case renaming is done (no dublicates) then modify the map that holds VISO items by:
           adding the renamed item, removing the old one (if it exists) and also add a :remove: to
           VISO file for the old path since in some cases, when remaned item is not top level, it still
           appears in ISO. So we remove it explicitly: */
        m_entryMap.insert(strNewPath, pItem->data(UIFileSystemModelData_LocalPath).toString());
        m_entryMap.remove(strOldPath);
        if (!pItem->data(UIFileSystemModelData_LocalPath).toString().isEmpty())
            m_entryMap.insert(strOldPath, cRemoveText);
    }

    if (m_pProxyModel)
        m_pProxyModel->invalidate();
}

void UIVisoContentBrowser::sltTableSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);
    emit sigTableSelectionChanged(selected.isEmpty());

    enableDisableSelectionDependentActions();
}

void UIVisoContentBrowser::sltResetAction()
{
    UIFileSystemItem *pRoot = rootItem();
    if (!pRoot)
        return;
    UIFileSystemItem *pChild = pRoot->child(0);
    if (!pChild)
        return;
    goToStart();
    pChild->removeChildren();
    m_entryMap.clear();
    if (m_pProxyModel)
        m_pProxyModel->invalidate();
    m_strImportedISOPath.clear();
}

void UIVisoContentBrowser::sltDroppedItems(QStringList pathList)
{
    addObjectsToViso(pathList);
}

void UIVisoContentBrowser::sltShowContextMenu(const QPoint &point)
{
    QWidget *pSender = qobject_cast<QWidget*>(sender());
    AssertReturnVoid(pSender);

    QMenu menu;

    menu.addAction(m_pRemoveAction);
    menu.addAction(m_pRestoreAction);
    menu.addAction(m_pCreateNewDirectoryAction);
    menu.addAction(m_pResetAction);
    menu.exec(pSender->mapToGlobal(point));
}

void UIVisoContentBrowser::sltGoUp()
{
    goUp();
}

void UIVisoContentBrowser::sltNavigationWidgetPathChange(const QString &strPath)
{
    setPathFromNavigationWidget(strPath);
    enableForwardBackwardActions();
}

void UIVisoContentBrowser::sltTableViewItemDoubleClick(const QModelIndex &index)
{
    tableViewItemDoubleClick(index);
}

void UIVisoContentBrowser::sltGoForward()
{
    if (m_pNavigationWidget)
        m_pNavigationWidget->goForwardInHistory();
}

void UIVisoContentBrowser::sltGoBackward()
{
    if (m_pNavigationWidget)
        m_pNavigationWidget->goBackwardInHistory();
}

QList<UIFileSystemItem*> UIVisoContentBrowser::tableSelectedItems()
{
    QList<UIFileSystemItem*> selectedItems;
    if (!m_pProxyModel)
        return selectedItems;
    QItemSelectionModel *selectionModel = m_pTableView->selectionModel();
    if (!selectionModel || selectionModel->selectedIndexes().isEmpty())
        return selectedItems;
    QModelIndexList list = selectionModel->selectedRows();
    foreach (QModelIndex index, list)
    {
        UIFileSystemItem *pItem =
            static_cast<UIFileSystemItem*>(m_pProxyModel->mapToSource(index).internalPointer());
        if (pItem)
            selectedItems << pItem;
    }
    return selectedItems;
}

QString UIVisoContentBrowser::currentPath() const
{
    if (!m_pTableView || !m_pTableView->rootIndex().isValid())
        return QString();
    QModelIndex index = m_pProxyModel->mapToSource(m_pTableView->rootIndex());
    UIFileSystemItem *pItem = static_cast<UIFileSystemItem*>((index).internalPointer());
    if (!pItem)
        return QString();
    return pItem->path();
}

bool UIVisoContentBrowser::onStartItem()
{
    if (!m_pTableView || !m_pModel)
        return false;
    QModelIndex index = m_pProxyModel->mapToSource(m_pTableView->rootIndex());
    UIFileSystemItem *pItem = static_cast<UIFileSystemItem*>((index).internalPointer());
    if (!index.isValid() || !pItem)
        return false;
    if (pItem != startItem())
        return false;
    return true;
}

void UIVisoContentBrowser::goUp()
{
    AssertReturnVoid(m_pProxyModel);
    AssertReturnVoid(m_pTableView);
    QModelIndex currentRoot = m_pProxyModel->mapToSource(m_pTableView->rootIndex());
    if (!currentRoot.isValid())
        return;
    /* Go up if we are not already in root: */
    if (!onStartItem())
        setTableRootIndex(currentRoot.parent());
}

void UIVisoContentBrowser::goToStart()
{
    while(!onStartItem())
        goUp();
}

const UIFileSystemItem* UIVisoContentBrowser::currentDirectoryItem() const
{
    if (!m_pTableView || !m_pTableView->rootIndex().isValid())
        return 0;
    QModelIndex currentRoot = m_pProxyModel->mapToSource(m_pTableView->rootIndex());

    return static_cast<UIFileSystemItem*>(currentRoot.internalPointer());
}

QStringList UIVisoContentBrowser::currentDirectoryListing() const
{
    const UIFileSystemItem *pCurrentDirectoryItem = currentDirectoryItem();
    if (!pCurrentDirectoryItem)
        return QStringList();
    QStringList nameList;
    foreach (const UIFileSystemItem *pChild, pCurrentDirectoryItem->children())
    {
        if (pChild)
            nameList << pChild->fileObjectName();
    }
    return nameList;
}

void UIVisoContentBrowser::enableDisableSelectionDependentActions()
{
    AssertReturnVoid(m_pTableView);

    bool fSelection = m_pTableView->hasSelection();
    if (m_pRemoveAction)
        m_pRemoveAction->setEnabled(fSelection);
    if (m_pRestoreAction)
        m_pRestoreAction->setEnabled(fSelection);
    if (m_pRenameAction)
        m_pRenameAction->setEnabled(fSelection);
}

void UIVisoContentBrowser::updateNavigationWidgetPath(const QString &strPath)
{
    if (!m_pNavigationWidget)
        return;
    m_pNavigationWidget->setPath(strPath);
}

void UIVisoContentBrowser::setFileTableLabelText(const QString &strText)
{
    if (m_pFileTableLabel)
        m_pFileTableLabel->setText(strText);
}

void UIVisoContentBrowser::enableForwardBackwardActions()
{
    AssertReturnVoid(m_pNavigationWidget);
    if (m_pGoForward)
        m_pGoForward->setEnabled(m_pNavigationWidget->canGoForward());
    if (m_pGoBackward)
        m_pGoBackward->setEnabled(m_pNavigationWidget->canGoBackward());
}

void UIVisoContentBrowser::setSortCaseSensitive(bool fCaseSensitive)
{
    if (!m_pProxyModel)
        return;
    if (fCaseSensitive)
        m_pProxyModel->setSortCaseSensitivity(Qt::CaseSensitive);
    else
        m_pProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
}

#include "UIVisoContentBrowser.moc"
