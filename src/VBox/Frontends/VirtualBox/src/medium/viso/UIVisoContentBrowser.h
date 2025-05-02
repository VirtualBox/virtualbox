/* $Id: UIVisoContentBrowser.h 109396 2025-05-02 14:26:17Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVisoContentBrowser class declaration.
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

#ifndef FEQT_INCLUDED_SRC_medium_viso_UIVisoContentBrowser_h
#define FEQT_INCLUDED_SRC_medium_viso_UIVisoContentBrowser_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIActionPool.h"

/* Qt includes: */
#include <QItemSelection>
#include <QPointer>
#include <QWidget>

/* COM includes: */
#include "KFsObjType.h"

/* Forward declarations: */
class QFileInfo;
class QGridLayout;
class QLabel;
class QIToolBar;
class UIFileSystemItem;
class UIFileSystemModel;
class UIFileSystemProxyModel;
class UIFileTableNavigationWidget;
class UIVisoContentTableView;

/** A UIVisoBrowserBase extension to view content of a VISO as a file tree. */
class UIVisoContentBrowser : public QWidget
{
    Q_OBJECT;

signals:

    void sigTableSelectionChanged(bool fIsSelectionEmpty);
    void sigISOContentImportedOrRemoved(bool fImportedr);
    void sigVolumeIdParsed(const QString &strVoludId);

public:

    UIVisoContentBrowser(UIActionPool *pActionPool, QWidget *pParent = 0);
    ~UIVisoContentBrowser();
    /* Imports pathList (relative to ISO file's root) to VISO content. */
    void importISOContentToViso(const QString &strISOFilePath,
                                UIFileSystemItem *pParentItem = 0,
                                const QString &strDirPath = QString());
    void removeISOContentFromViso();
    /** Adds file objests from the host file system. @p pathList consists of list of paths to there objects. */
    void addObjectsToViso(const QStringList &pathList);
    /** Returns the content of the VISO as a string list. Each element of the list becomes a line in the
      * .viso file. */
    QStringList entryList();
    void showHideHiddenObjects(bool bShow);

    void parseVisoFileContent(const QString &strFileName);
    void prepareMainMenu(QMenu *pMenu);

    const QString &importedISOPath() const;
    void setImportedISOPath(const QString &strPath = QString());

    bool hasContent() const;

    void setSortCaseSensitive(bool fCaseSensitive);
    static QString sanitizePath(const QString &strOriginal);

public slots:

    void sltCreateNewDirectory();
    /** Handles the signal we get from the model during setData call. Restores the old name of the file object
     *  to @p strOldName if need be (if rename fails for some reason). */
    void sltItemRenameAttempt(UIFileSystemItem *pItem, const QString &strOldPath,
                              const QString &strOldName, const QString &strNewName);
    void sltRemoveItems();
    void sltRestoreItems();
    void sltResetAction();
    void sltItemRenameAction();
    void sltGoUp();
    void sltNavigationWidgetPathChange(const QString &strPath);
    void sltTableViewItemDoubleClick(const QModelIndex &index);
    void sltGoForward();
    void sltGoBackward();

protected:

    void tableViewItemDoubleClick(const QModelIndex &index);
    void setPathFromNavigationWidget(const QString &strPath);
    void setTableRootIndex(QModelIndex index = QModelIndex());

private slots:

    void sltTableSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    /** Adds the dragged-dropped items to VISO. */
    void sltDroppedItems(QStringList pathList);
    void sltShowContextMenu(const QPoint &point);
    void sltRetranslateUI();

private:

    void              prepareObjects();
    void              prepareConnections();
    void              prepareToolBar();
    void              initializeModel();
    UIFileSystemItem *rootItem();
    /* Child of root. */
    UIFileSystemItem *startItem();
    QModelIndex       convertIndexToTableIndex(const QModelIndex &index);
    /** Lists the content of the host file system directory by using Qt file system API. */
    void              scanHostDirectory(UIFileSystemItem *directory, bool fRecursive);
    KFsObjType        fileType(const QFileInfo &fsInfo);
    /** Renames the starts item's name as VISO name changes. */
    void              updateStartItemName();
    void              renameFileObject(UIFileSystemItem *pItem);
    void              removeItems(const QList<UIFileSystemItem*> itemList);
    void              restoreItems(const QList<UIFileSystemItem*> itemList);
    /** Creates and entry for pItem consisting of a map item (key is viso path and value is host file system path)
     *  if @p bRemove is true then the value is the string ":remove:" which effectively removes the file object
     *  from the iso image. */
    void              createVisoEntry(const QString &strPath, const QString &strLocalPath, bool bRemove = false);
    QString           currentPath() const;
    UIFileSystemItem* searchItemByPath(const QString &strPath);
    void              goToStart();
    /** Returns a list of items which are currecntly selected
     *  in the table view. */
    QList<UIFileSystemItem*> tableSelectedItems();
    const UIFileSystemItem*  currentDirectoryItem() const;

    /* Names of the file objects of the current directory. */
    QStringList currentDirectoryListing() const;
    bool        onStartItem();
    void        goUp();
    void        createLoadedFileEntries(const QMap<QString, QString> &fileEntries);
    /* Processes a list of VISO paths that are loaded from a file and indicate file object to be removed from VISO content. */
    void           processRemovedEntries(const QStringList &removedEntries);
    void           markRemovedUnremovedItemParents(UIFileSystemItem *pItem, bool fRemoved);
    void           enableDisableSelectionDependentActions();
    void           updateNavigationWidgetPath(const QString &strPath);
    void           setFileTableLabelText(const QString &strText);
    void           enableForwardBackwardActions();

    UIVisoContentTableView *m_pTableView;
    UIFileSystemModel      *m_pModel;
    UIFileSystemProxyModel *m_pProxyModel;
    QPointer<QMenu>        m_pSubMenu;
    QString                m_strImportedISOPath;
    /** keys of m_entryMap are iso locations and values are
     *  local location of file objects. these keys and values are
     *  concatenated and passed to the client to create ad-hoc.viso entries. */
    QMap<QString, QString> m_entryMap;
    QGridLayout           *m_pMainLayout;
    QIToolBar             *m_pToolBar;
    UIFileTableNavigationWidget *m_pNavigationWidget;
    QLabel *m_pFileTableLabel;
    QPointer<UIActionPool> m_pActionPool;
    QAction  *m_pRemoveAction;
    QAction  *m_pRestoreAction;
    QAction  *m_pCreateNewDirectoryAction;
    QAction  *m_pRenameAction;
    QAction  *m_pResetAction;
    QAction  *m_pGoUp;
    QAction  *m_pGoForward;
    QAction  *m_pGoBackward;
};

#endif /* !FEQT_INCLUDED_SRC_medium_viso_UIVisoContentBrowser_h */
