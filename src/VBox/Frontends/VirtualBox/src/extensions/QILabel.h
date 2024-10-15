/* $Id: QILabel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QILabel class declaration.
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

/*
 * This class is based on the original QLabel implementation.
 */

#ifndef FEQT_INCLUDED_SRC_extensions_QILabel_h
#define FEQT_INCLUDED_SRC_extensions_QILabel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QLabel>
#include <QRegularExpression>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QLabel subclass extending it with advanced functionality. */
class SHARED_LIBRARY_STUFF QILabel : public QLabel
{
    Q_OBJECT;

public:

    /** Constructs label passing @a pParent and @a enmFlags to the base-class. */
    QILabel(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());
    /** Constructs label passing @a strText, @a pParent and @a enmFlags to the base-class. */
    QILabel(const QString &strText, QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());

    /** Returns whether label full-size focusing selection is enabled. */
    bool fullSizeSelection() const { return m_fFullSizeSelection; }
    /** Defines whether label full-size focusing selection is @a fEnabled. */
    void setFullSizeSelection(bool fEnabled);

    /** Defines whether label should use size-hint based on passed @a iWidthHint. */
    void useSizeHintForWidth(int iWidthHint) const;
    /** Returns size-hint. */
    QSize sizeHint() const RT_OVERRIDE RT_FINAL;
    /** Returns minimum size-hint. */
    QSize minimumSizeHint() const  RT_OVERRIDE RT_FINAL;

    /** Returns text. */
    QString text() const { return m_strText; }

public slots:

    /** Clears text. */
    void clear();
    /** Defines text. */
    void setText(const QString &strText);
    /** Copies text into clipboard. */
    void copy();

protected:

    /** Handles resize @a pEvent. */
    void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles mouse-press @a pEvent. */
    void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles mouse-release @a pEvent. */
    void mouseReleaseEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles mouse-move @a pEvent. */
    void mouseMoveEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles context-menu @a pEvent. */
    void contextMenuEvent(QContextMenuEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles focus-in @a pEvent. */
    void focusInEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles focus-out @a pEvent. */
    void focusOutEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles paint @a pEvent. */
    void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:

    /** Performs initialization. */
    void init();

    /** Updates size-hint. */
    void updateSizeHint() const;
    /** Defines full-text. */
    void setFullText(const QString &strText);
    /** Updates text. */
    void updateText();
    /** Compresses passed @a strText. */
    QString compressText(const QString &strText) const;
    /** Returns text without HTML tags. */
    static QString removeHtmlTags(const QString &strText);
    /** Converts passed @a strType to text-elide mode flag. */
    static Qt::TextElideMode toTextElideMode(const QString& strType);

    /** Holds the text. */
    QString m_strText;

    /** Holds whether label full-size focusing selection is enabled. */
    bool m_fFullSizeSelection;
    /** Holds whether we started D&D. */
    bool m_fStartDragging;

    /** Holds whether the size-hint is valid. */
    mutable bool  m_fHintValid;
    /** Holds the width-hint. */
    mutable int   m_iWidthHint;
    /** Holds the size-hint. */
    mutable QSize m_ownSizeHint;

    /** Holds the Copy action instance. */
    QAction *m_pCopyAction;

    /** Holds text-copy reg-exp. */
    static const QRegularExpression s_regExpCopy;
    /** Holds text-elide reg-exp. */
    static const QRegularExpression s_regExpElide;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QILabel_h */
