/* $Id: QIRichTextLabel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIRichTextLabel class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIRichTextLabel_h
#define FEQT_INCLUDED_SRC_extensions_QIRichTextLabel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTextBrowser>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QAction;

/** QLabel analog to reflect rich-text,
 ** based on private QTextBrowser functionality. */
class SHARED_LIBRARY_STUFF QIRichTextLabel : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(QString text READ text WRITE setText);

signals:

    /** Notifies listeners about @a link clicked. */
    void sigLinkClicked(const QUrl &link);

public:

    /** Constructs rich text-label passing @a pParent to the base-class. */
    QIRichTextLabel(QWidget *pParent = 0);

    /** Returns text. */
    QString text() const;
    /** Returns plain text. */
    QString plainText() const;

    /** Registers @a image under a passed @a strName. */
    void registerImage(const QImage &image, const QString &strName);
    /** Registers @a pixmap under a passed @a strName. */
    void registerPixmap(const QPixmap &pixmap, const QString &strName);

    /** Returns word wrapping policy. */
    QTextOption::WrapMode wordWrapMode() const;
    /** Defines word wrapping @a policy. */
    void setWordWrapMode(QTextOption::WrapMode policy);

    /** Installs event filter for a passed @ pFilterObj. */
    void installEventFilter(QObject *pFilterObj);

    /** Returns browser font. */
    QFont browserFont() const;
    /** Defines @a newFont for browser. */
    void setBrowserFont(const QFont &newFont);

public slots:

    /** Returns minimum text width. */
    int minimumTextWidth() const;
    /** Defines @a iMinimumTextWidth. */
    void setMinimumTextWidth(int iMinimumTextWidth);

    /** Defines @a strText. */
    void setText(const QString &strText);

    /** Copies text-browser text into clipboard. */
    void copy();

private slots:

    /** Handles the fact of text-browser text copy available.
      * @param  fYes  Brings whether some text is selected and can
      *               be copied directly by QTextBrowser::copy() call. */
    void sltHandleCopyAvailable(bool fYes) { m_fCopyAvailable = fYes; }
    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Holds the text-browser instance. */
    QTextBrowser *m_pTextBrowser;

    /** Holds the context-menu Copy action instance. */
    QAction *m_pActionCopy;
    /** Holds whether text-browser text copy is available. */
    bool     m_fCopyAvailable;

    /** Holds the minimum text-width. */
    int m_iMinimumTextWidth;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIRichTextLabel_h */
