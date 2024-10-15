/* $Id: UIFileManagerHostTable.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManagerHostTable class declaration.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIFileManagerHostTable_h
#define FEQT_INCLUDED_SRC_guestctrl_UIFileManagerHostTable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QFileDevice>

/* GUI includes: */
#include "UIFileManagerTable.h"

/* Forward declarations: */
class UIActionPool;
class UIFileSystemItem;

/** This class scans the host file system by using the Qt API
    and connects to the UIFileSystemModel*/
class UIFileManagerHostTable : public UIFileManagerTable
{
    Q_OBJECT;

public:

    UIFileManagerHostTable(UIActionPool *pActionPool, QWidget *pParent = 0);

    static KFsObjType  fileType(const QFileInfo &fsInfo);
    static KFsObjType  fileType(const QString &strPath);
    virtual bool    isWindowsFileSystem() const override final;

protected:

    /** Scans the directory with the path @strPath and inserts items to the
     *  tree under the @p parent. */
    static bool scanDirectory(const QString& strPath, UIFileSystemItem *parent,
                              QMap<QString, UIFileSystemItem*> &fileObjects);
    virtual bool    readDirectory(const QString& strPath, UIFileSystemItem *parent, bool isStartDir = false) override final;
    virtual void    deleteByItem(UIFileSystemItem *item) override final;
    virtual void    goToHomeDirectory() override final;
    virtual bool    renameItem(UIFileSystemItem *item, const QString &strOldPath) override final;
    virtual bool    createDirectory(const QString &path, const QString &directoryName) override final;
    virtual QString fsObjectPropertyString() override final;
    virtual void    showProperties() override final;
    virtual void    determineDriveLetters() override final;
    virtual void    determinePathSeparator() override final;
    virtual void    prepareToolbar() override final;
    virtual void    createFileViewContextMenu(const QWidget *pWidget, const QPoint &point) override;
    virtual void    toggleForwardBackwardActions() override final;

    /** @name Copy/Cut host-to-host stuff. Currently not implemented.
     * @{ */
        /** Disable/enable paste action depending on the m_eFileOperationType. */
        virtual void  setPasteActionEnabled(bool)  override final {}
        virtual void  pasteCutCopiedObjects()  override final {}
    /** @} */

protected slots:

    virtual void sltRetranslateUI() RT_OVERRIDE;

private:

    static QString permissionString(QFileDevice::Permissions permissions);
    void    prepareActionConnections();

    QAction *m_pModifierActionSeparator;
};

#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIFileManagerHostTable_h */
