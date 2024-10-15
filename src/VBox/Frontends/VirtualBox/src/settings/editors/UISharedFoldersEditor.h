/* $Id: UISharedFoldersEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISharedFoldersEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UISharedFoldersEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UISharedFoldersEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIExtraDataDefs.h"

/* Forward declartions: */
class QHBoxLayout;
class QTreeWidgetItem;
class QIToolBar;
class QITreeWidget;
class SFTreeViewItem;

/** Shared Folder data. */
struct UIDataSharedFolder
{
    /** Constructs data. */
    UIDataSharedFolder()
        : m_enmType(UISharedFolderType_Machine)
        , m_strName()
        , m_strPath()
        , m_fWritable(false)
        , m_fAutoMount(false)
        , m_strAutoMountPoint()
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSharedFolder &other) const
    {
        return true
               && m_enmType == other.m_enmType
               && m_strName == other.m_strName
               && m_strPath == other.m_strPath
               && m_fWritable == other.m_fWritable
               && m_fAutoMount == other.m_fAutoMount
               && m_strAutoMountPoint == other.m_strAutoMountPoint
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSharedFolder &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSharedFolder &other) const { return !equal(other); }

    /** Holds the shared folder type. */
    UISharedFolderType  m_enmType;
    /** Holds the shared folder name. */
    QString             m_strName;
    /** Holds the shared folder path. */
    QString             m_strPath;
    /** Holds whether the shared folder should be writeable. */
    bool                m_fWritable;
    /** Holds whether the shared folder should be auto-mounted at startup. */
    bool                m_fAutoMount;
    /** Where in the guest to try auto mount the shared folder (drive for
     * Windows & OS/2, path for unixy guests). */
    QString             m_strAutoMountPoint;
};

/** UIEditor sub-class used as a shared folders editor. */
class SHARED_LIBRARY_STUFF UISharedFoldersEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UISharedFoldersEditor(QWidget *pParent = 0);

    /** Defines editor @a strValue. */
    void setValue(const QList<UIDataSharedFolder> &guiValue);
    /** Returns editor value. */
    QList<UIDataSharedFolder> value() const;

    /** Defines whether feature @a fAvailable. */
    void setFeatureAvailable(bool fAvailable);
    /** Defines whether folders of certain @a enmType are @a fAvailable. */
    void setFoldersAvailable(UISharedFolderType enmType, bool fAvailable);

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

    /** Performs request to adjust tree. */
    void sltAdjustTree();
    /** Performs request to adjust tree fields. */
    void sltAdjustTreeFields();

    /** Handles @a pCurrentItem change. */
    void sltHandleCurrentItemChange(QTreeWidgetItem *pCurrentItem);
    /** Handles @a pItem double-click. */
    void sltHandleDoubleClick(QTreeWidgetItem *pItem);
    /** Handles context menu request for @a position. */
    void sltHandleContextMenuRequest(const QPoint &position);

    /** Handles command to add shared folder. */
    void sltAddFolder();
    /** Handles command to edit shared folder. */
    void sltEditFolder();
    /** Handles command to remove shared folder. */
    void sltRemoveFolder();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepare tree-widget. */
    void prepareTreeWidget();
    /** Prepare tool-bar. */
    void prepareToolbar();
    /** Prepares connections. */
    void prepareConnections();

    /** Returns a list of used shared folder names. */
    QStringList usedList(bool fIncludeSelected);

    /** Returns the tree-view root item for corresponding shared folder @a type. */
    SFTreeViewItem *root(UISharedFolderType type);
    /** Defines whether the root item of @a enmFoldersType is @a fVisible. */
    void setRootItemVisible(UISharedFolderType enmFoldersType, bool fVisible);
    /** Updates root item visibility. */
    void updateRootItemsVisibility();
    /** Creates shared folder item based on passed @a data. */
    void addSharedFolderItem(const UIDataSharedFolder &sharedFolderData, bool fChoose);
    /** Reloads tree. */
    void reloadTree();

    /** Holds the value to be set. */
    QList<UIDataSharedFolder>  m_guiValue;

    /** Holds whether folders of certain type are available. */
    QMap<UISharedFolderType, bool>  m_foldersAvailable;

    /** @name Widgets
     * @{ */
        /** Holds the tree layout instance. */
        QHBoxLayout      *m_pLayoutTree;
        /** Holds the tree-widget instance. */
        QITreeWidget     *m_pTreeWidget;
        /** Holds the toolbar instance. */
        QIToolBar        *m_pToolbar;
        /** Holds the 'add shared folder' action instance. */
        QAction          *m_pActionAdd;
        /** Holds the 'edit shared folder' action instance. */
        QAction          *m_pActionEdit;
        /** Holds the 'remove shared folder' action instance. */
        QAction          *m_pActionRemove;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UISharedFoldersEditor_h */
