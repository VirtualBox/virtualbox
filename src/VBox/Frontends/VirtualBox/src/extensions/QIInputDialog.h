/* $Id: QIInputDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIInputDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIInputDialog_h
#define FEQT_INCLUDED_SRC_extensions_QIInputDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>
#include <QPointer>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QLineEdit;
class QIDialogButtonBox;

/** QDialog extension providing the GUI with
  * the advanced input dialog capabilities. */
class SHARED_LIBRARY_STUFF QIInputDialog : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs the dialog passing @a pParent and @a enmFlags to the base-class. */
    QIInputDialog(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());

    /** Returns label text. */
    QString labelText() const;
    /** Undefines label text. */
    void resetLabelText();
    /** Defines label @a strText. */
    void setLabelText(const QString &strText);

    /** Returns text value. */
    QString textValue() const;
    /** Defines @a strText value. */
    void setTextValue(const QString &strText);

protected:

    /** Handles translation event. */
    virtual void retranslateUi();

private slots:

    /** Handles text value change. */
    void sltTextChanged();

private:

    /** Prepared all. */
    void prepare();

    /** Holds whether label text redefined. */
    bool  m_fDefaultLabelTextRedefined;

    /** Holds the label instance. */
    QLabel            *m_pLabel;
    /** Holds the text value editor instance. */
    QLineEdit         *m_pTextValueEditor;
    /** Holds the button-box instance. */
    QIDialogButtonBox *m_pButtonBox;
};

/** Safe pointer to the QIInputDialog class. */
typedef QPointer<QIInputDialog> QISafePointerInputDialog;

#endif /* !FEQT_INCLUDED_SRC_extensions_QIInputDialog_h */
