/* $Id: QILineEdit.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QILineEdit class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QILineEdit_h
#define FEQT_INCLUDED_SRC_extensions_QILineEdit_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes */
#include <QLineEdit>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;

/** QLineEdit extension with advanced functionality. */
class SHARED_LIBRARY_STUFF QILineEdit : public QLineEdit
{
    Q_OBJECT;

public:

    /** Constructs line-edit passing @a pParent to the base-class. */
    QILineEdit(QWidget *pParent = 0);
    /** Constructs line-edit passing @a pParent to the base-class.
      * @param  strText  Brings the line-edit text. */
    QILineEdit(const QString &strText, QWidget *pParent = 0);

    /** Defines whether this is @a fAllowed to copy contents when disabled. */
    void setAllowToCopyContentsWhenDisabled(bool fAllowed);

    /** Forces line-edit to adjust minimum width acording to passed @a strText. */
    void setMinimumWidthByText(const QString &strText);
    /** Forces line-edit to adjust fixed width acording to passed @a strText. */
    void setFixedWidthByText(const QString &strText);

    /** Defines whether line-edit is @a fMarkable. */
    void setMarkable(bool fMarkable);
    /** Puts an icon to mark some error on the right hand side of the line edit. @p is used as tooltip of the icon. */
    void mark(bool fError, const QString &strErrorMessage, const QString &strNoErrorMessage);

protected:

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Copies text into clipboard. */
    void copy();

private:

    /** Prepares all. */
    void prepare();

    /** Calculates suitable @a strText size. */
    QSize fitTextWidth(const QString &strText) const;

    /** Sets the geometry of the icon label. */
    void moveIconLabel();

    /** Holds whether this is allowed to copy contents when disabled. */
    bool     m_fAllowToCopyContentsWhenDisabled;
    /** Holds the copy to clipboard action. */
    QAction *m_pCopyAction;

    /** Holds whether line-edit is markable. */
    bool     m_fMarkable;
    /** Holds whether line-edit is marked for error. */
    bool     m_fMarkForError;
    /** Holds the icon label instance. */
    QLabel  *m_pLabelIcon;
    /** Holds last error message. */
    QString  m_strErrorMessage;
    /** Holds cached icon margin. */
    int      m_iIconMargin;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QILineEdit_h */
