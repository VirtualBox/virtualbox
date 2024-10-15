/* $Id: UISharedFolderDetailsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISharedFolderDetailsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UISharedFolderDetailsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UISharedFolderDetailsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIDialog.h"

/* Forward declarations: */
class QCheckBox;
class QLabel;
class QLineEdit;
class QIDialogButtonBox;
class UIFilePathSelector;

/** QIDialog subclass used as a shared folders editor. */
class SHARED_LIBRARY_STUFF UISharedFolderDetailsEditor : public QIDialog
{
    Q_OBJECT;

public:

    /** Editor types. */
    enum EditorType
    {
        EditorType_Add,
        EditorType_Edit
    };

    /** Constructs editor passing @a pParent to the base-class.
      * @param  enmType        Brings editor type.
      * @param  fUsePermanent  Brings whether folder can be permanent.
      * @param  usedNames      Brings existing folder names. */
    UISharedFolderDetailsEditor(EditorType enmType,
                                bool fUsePermanent,
                                const QStringList &usedNames,
                                QWidget *pParent = 0);

    /** Defines folder @a strPath. */
    void setPath(const QString &strPath);
    /** Returns folder path. */
    QString path() const;

    /** Defines folder @a strName. */
    void setName(const QString &strName);
    /** Returns folder name. */
    QString name() const;

    /** Defines whether folder is @a fWritable. */
    void setWriteable(bool fWriteable);
    /** Returns whether folder is writable. */
    bool isWriteable() const;

    /** Defines whether folder supports @a fAutoMount. */
    void setAutoMount(bool fAutoMount);
    /** Returns whether folder supports auto mount. */
    bool isAutoMounted() const;

    /** Defines folder @a strAutoMountPoint. */
    void setAutoMountPoint(const QString &strAutoMountPoint);
    /** Returns folder auto mount point. */
    QString autoMountPoint() const;

    /** Defines whether folder is @a fPermanent. */
    void setPermanent(bool fPermanent);
    /** Returns whether folder is permanent. */
    bool isPermanent() const;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();
    /** Holds signal about folder path selected. */
    void sltSelectPath();
    /** Checks editor validness. */
    void sltValidate();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares connections. */
    void prepareConnections();

    /** @name Arguments
      * @{ */
        /** Holds editor type. */
        EditorType   m_enmType;
        /** Holds whether folder can be permanent. */
        bool         m_fUsePermanent;
        /** Holds existing folder names. */
        QStringList  m_usedNames;
    /** @} */

    /** @name Widgets
      * @{ */
        /** Holds the path label instance. */
        QLabel             *m_pLabelPath;
        /** Holds the path selector instance. */
        UIFilePathSelector *m_pSelectorPath;
        /** Holds the name label instance. */
        QLabel             *m_pLabelName;
        /** Holds the name editor instance. */
        QLineEdit          *m_pEditorName;
        /** Holds the auto-mount point label instance. */
        QLabel             *m_pLabelAutoMountPoint;
        /** Holds the auto-mount point editor instance. */
        QLineEdit          *m_pEditorAutoMountPoint;
        /** Holds the read-only check-box instance. */
        QCheckBox          *m_pCheckBoxReadonly;
        /** Holds the auto-mount check-box instance. */
        QCheckBox          *m_pCheckBoxAutoMount;
        /** Holds the permanent check-box instance. */
        QCheckBox          *m_pCheckBoxPermanent;
        /** Holds the button-box instance. */
        QIDialogButtonBox  *m_pButtonBox;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UISharedFolderDetailsEditor_h */
