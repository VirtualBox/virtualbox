/* $Id: UILineTextEdit.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UILineTextEdit class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UILineTextEdit_h
#define FEQT_INCLUDED_SRC_widgets_UILineTextEdit_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* VBox includes */
#include "QIDialog.h"

/* Qt includes */
#include <QPushButton>

/* Qt forward declarations */
class QTextEdit;
class QDialogButtonBox;

////////////////////////////////////////////////////////////////////////////////
// UITextEditor

class UITextEditor: public QIDialog
{
    Q_OBJECT;

public:
    UITextEditor(QWidget *pParent = NULL);

    void setText(const QString& strText);
    QString text() const;

private slots:

    void sltRetranslateUI();
    void open() RT_OVERRIDE;

private:
    /* Private member vars */
    QTextEdit        *m_pTextEdit;
    QDialogButtonBox *m_pButtonBox;
    QPushButton      *m_pOpenButton;
};

////////////////////////////////////////////////////////////////////////////////
// UILineTextEdit

class UILineTextEdit: public QPushButton
{
    Q_OBJECT;

signals:

    /* Notifier: Editing stuff: */
    void sigFinished(QWidget *pThis);

public:
    UILineTextEdit(QWidget *pParent = NULL);

    void setText(const QString& strText) { m_strText = strText; }
    QString text() const { return m_strText; }

private slots:

    void sltRetranslateUI();
    void edit();

private:
    /* Private member vars */
    QString m_strText;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UILineTextEdit_h */
