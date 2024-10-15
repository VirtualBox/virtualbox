/* $Id: QIRichToolButton.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIRichToolButton class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIRichToolButton_h
#define FEQT_INCLUDED_SRC_extensions_QIRichToolButton_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QString;
class QIToolButton;

/** QWidget extension
  * representing tool-button with separate text-label. */
class SHARED_LIBRARY_STUFF QIRichToolButton : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about button click. */
    void sigClicked();

public:

    /** Constructs rich tool-button passing @a pParent to the base-class. */
    QIRichToolButton(QWidget *pParent = 0);

    /** Defines tool-button @a iconSize. */
    void setIconSize(const QSize &iconSize);
    /** Defines tool-button @a icon. */
    void setIcon(const QIcon &icon);
    /** Animates tool-button click. */
    void animateClick();

    /** Defines text-label @a strText. */
    void setText(const QString &strText);

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;
    /** Handles key-press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;

protected slots:

    /** Handles button-click. */
    virtual void sltButtonClicked() {}

private:

    /** Prepares all. */
    void prepare();

    /** Holds the tool-button instance. */
    QIToolButton *m_pButton;
    /** Holds the text-label instance. */
    QLabel       *m_pLabel;
    /** Holds the text for text-label instance. */
    QString       m_strName;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIRichToolButton_h */
