/* $Id: UIFilePathSelector.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFilePathSelector class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIFilePathSelector_h
#define FEQT_INCLUDED_SRC_widgets_UIFilePathSelector_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QComboBox>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIMediumDefs.h"

/* Forward declarations: */
class QAction;
class QFocusEvent;
class QHBoxLayout;
class QObject;
class QResizeEvent;
class QWidget;
class QString;
class QILabel;
class QILineEdit;
class QIToolButton;

/** QComboBox subclass providing GUI with the
  * possibility to choose/reflect file/folder path. */
class SHARED_LIBRARY_STUFF UIFilePathSelector : public QComboBox
{
    Q_OBJECT;

signals:

    /** Notify listeners about @a strPath changed. */
    void pathChanged(const QString &strPath);

public:

    /** Modes file-path selector operates in. */
    enum Mode
    {
        Mode_Folder = 0,
        Mode_File_Open,
        Mode_File_Save
    };

    /** Combo-box field IDs file-path selector uses. */
    enum
    {
        PathId = 0,
        SelectId,
        ResetId
    };

    /** Constructs file-path selector passing @a pParent to QComboBox base-class. */
    UIFilePathSelector(QWidget *pParent = 0);

    /** Defines the @a enmMode to operate in. */
    void setMode(Mode enmMode);
    /** Returns the mode to operate in. */
    Mode mode() const { return m_enmMode; }

    /** Defines whether the path is @a fEditable. */
    void setEditable(bool fEditable);
    /** Returns whether the path is editable. */
    bool isEditable() const { return m_fEditable; }
    /** Calls QILineEdit member's setMarkable API. */
    void setMarkable(bool fMarkable);
    /** Marks the line edit of the combobox. Refer to QILineEdit::mark(..). */
    void mark(bool fError, const QString &strErrorMessage, const QString &strNoErrorMessage);

    /** Defines whether the reseting to defauilt path is @a fEnabled. */
    void setResetEnabled(bool fEnabled);
    /** Returns whether the reseting to defauilt path is enabled. */
    bool isResetEnabled() const { return count() - 1  == ResetId; }

    /** Defines the file-dialog @a strTitle. */
    void setFileDialogTitle(const QString &strTitle) { m_strFileDialogTitle = strTitle; }
    /** Returns the file-dialog title. */
    QString fileDialogTitle() const { return m_strFileDialogTitle; }

    /** Defines the file-dialog @a strFilters. */
    void setFileDialogFilters(const QString &strFilters) { m_strFileDialogFilters = strFilters; }
    /** Returns the file-dialog filters. */
    QString fileDialogFilters() const { return m_strFileDialogFilters; }

    /** Defines the file-dialog @a strDefaultSaveExtension. */
    void setFileDialogDefaultSaveExtension(const QString &strDefaultSaveExtension) { m_strFileDialogDefaultSaveExtension = strDefaultSaveExtension; }
    /** Returns the file-dialog default save extension. */
    QString fileDialogDefaultSaveExtension() const { return m_strFileDialogDefaultSaveExtension; }

    /** Resets path modified state to false. */
    void resetModified() { m_fModified = false; }
    /** Returns whether the path is modified. */
    bool isModified() const { return m_fModified; }
    /** Returns whether the path is selected. */
    bool isPathSelected() const { return currentIndex() == PathId; }

    /** Returns the path. */
    QString path() const { return m_strPath; }
    /** Returns the path which we pass to QFileDialog as initial path. */
    QString initialPath() const { return m_strInitialPath; }

    /** Returns true if the selected path points to an existing/readable file. */
    bool isValid() const;

    /** Sets overriden widget's @a strToolTip.
      * @note If nothing set it's generated automatically. */
    void setToolTip(const QString &strToolTip);

    void setDefaultPath(const QString &strDefaultPath);
    const QString& defaultPath() const;

    void setRecentMediaListType(UIMediumDeviceType enmMediumType);
    UIMediumDeviceType recentMediaListType() const;

public slots:

    /** Defines the @a strPath and @a fRefreshText after that. */
    void setPath(const QString &strPath, bool fRefreshText = true);

    /** Defines the @a strInitialPath. */
    void setInitialPath(const QString &strInitialPath) { m_strInitialPath = strInitialPath; }

protected:

    /** Preprocesses every @a pEvent sent to @a pObject. */
    bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles resize @a pEvent. */
    void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles focus-in @a pEvent. */
    void focusInEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles focus-out @a pEvent. */
    void focusOutEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles combo-box @a iIndex activation. */
    void onActivated(int iIndex);

    /** Handles combo-box @a strText editing. */
    void onTextEdited(const QString &strText);

    /** Handles combo-box text copying. */
    void copyToClipboard();

    /** Refreshes combo-box text according to chosen path. */
    void refreshText();

    void sltRecentMediaListUpdated(UIMediumDeviceType enmMediumType);

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Provokes change to @a strPath and @a fRefreshText after that. */
    void changePath(const QString &strPath, bool fRefreshText = true);

    /** Call for file-dialog to choose path. */
    void selectPath();

    /** Returns default icon. */
    QIcon defaultIcon() const;

    /** Returns full path @a fAbsolute if necessary. */
    QString fullPath(bool fAbsolute = true) const;

    /** Shrinks the reflected text to @a iWidth pixels. */
    QString shrinkText(int iWidth) const;

    /** Holds the mode to operate in. */
    Mode     m_enmMode;

    /** Holds the path. */
    QString  m_strPath;
    /** Holds the path which we pass to QFileDialog as initial path. */
    QString  m_strInitialPath;

    /** Holds the file-dialog title. */
    QString  m_strFileDialogTitle;
    /** Holds the file-dialog filters. */
    QString  m_strFileDialogFilters;
    /** Holds the file-dialog default save extension. */
    QString  m_strFileDialogDefaultSaveExtension;

    /** Holds the cached text for empty path. */
    QString  m_strNoneText;
    /** Holds the cached tool-tip for empty path. */
    QString  m_strNoneToolTip;

    /** Holds whether editor has Reset action. */
    bool     m_fResetEnabled;

    /** Holds whether the path is editable. */
    bool     m_fEditable;
    /** Holds whether the path is modified. */
    bool     m_fModified;

    /** Holds whether we are in editable mode. */
    bool     m_fEditableMode;
    /** Holds whether we are expecting mouse events. */
    bool     m_fMouseAwaited;

    /** Holds whether the tool-tip overriden. */
    bool     m_fToolTipOverriden;

    /** Holds the copy action instance. */
    QAction *m_pCopyAction;

    /** Path is set to m_strDefaultPath when it is reset. */
    QString m_strDefaultPath;

    /** Holds the recent list separator position. */
    int                 m_iRecentListSeparatorPosition;
    /** Holds whether medium type for recent media list. If it is UIMediumDeviceType_Invalid the recent list is not shown. */
    UIMediumDeviceType  m_enmRecentMediaListType;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIFilePathSelector_h */
