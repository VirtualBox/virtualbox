/* $Id: QILabelSeparator.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QILabelSeparator class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QILabelSeparator_h
#define FEQT_INCLUDED_SRC_extensions_QILabelSeparator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI inlcudes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QString;
class QWidget;

/** QWidget extension providing GUI with label-separator. */
class SHARED_LIBRARY_STUFF QILabelSeparator : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs label-separator passing @a pParent and @a enmFlags to the base-class. */
    QILabelSeparator(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());
    /** Constructs label-separator passing @a pParent and @a enmFlags to the base-class.
      * @param  strText  Brings the label text. */
    QILabelSeparator(const QString &strText, QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());

    /** Returns the label text. */
    QString text() const;
    /** Defines the label buddy. */
    void setBuddy(QWidget *pBuddy);

public slots:

    /** Clears the label text. */
    void clear();
    /** Defines the label @a strText. */
    void setText(const QString &strText);

protected:

    /** Prepares all. */
    virtual void prepare();

    /** Holds the label instance. */
    QLabel *m_pLabel;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QILabelSeparator_h */
