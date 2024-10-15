/* $Id: UIUserNamePasswordEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUserNamePasswordEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_editors_UIUserNamePasswordEditor_h
#define FEQT_INCLUDED_SRC_wizards_editors_UIUserNamePasswordEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>
#include <QLineEdit>
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QILineEdit;
class QIToolButton;
class UIPasswordLineEdit;

class SHARED_LIBRARY_STUFF UIPasswordLineEdit : public QLineEdit
{
    Q_OBJECT;

signals:

    void sigTextVisibilityToggled(bool fTextVisible);

public:

    UIPasswordLineEdit(QWidget *pParent = 0);
    void toggleTextVisibility(bool fTextVisible);
    void mark(bool fError, const QString &strErrorToolTip);

protected:

    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltHandleTextVisibilityChange();

private:

    void prepare();
    void adjustTextVisibilityButtonGeometry();

    QIToolButton *m_pTextVisibilityButton;
    QIcon m_markIcon;
    QLabel *m_pErrorIconLabel;
    QString m_strErrorToolTip;
    /** When true the line edit is marked with some icon to indicate some error. */
    bool m_fMarkForError;
};

class SHARED_LIBRARY_STUFF UIUserNamePasswordEditor : public QWidget
{

    Q_OBJECT;

signals:

    void sigUserNameChanged(const QString &strUserName);
    void sigPasswordChanged(const QString &strPassword);

public:

    UIUserNamePasswordEditor(QWidget *pParent = 0);

    QString userName() const;
    void setUserName(const QString &strUserName);

    QString password() const;
    void setPassword(const QString &strPassword);

    /** Returns false if username or password fields are empty, or password fields do not match. */
    bool isComplete();

    /** When fEnabled true place holder texts for the line edits are shown. */
    void setPlaceholderTextEnabled(bool fEnabled);
    void setLabelsVisible(bool fVisible);

private slots:

    void sltHandlePasswordVisibility(bool fPasswordVisible);
    void sltUserNameChanged();
    void sltPasswordChanged();
    void sltRetranslateUI();

private:

    void prepare();
    template <class T>
    void addLineEdit(int &iRow, QLabel *&pLabel, T *&pLineEdit, QGridLayout *pLayout);

    bool isUserNameComplete();
    bool isPasswordComplete();

    QILineEdit         *m_pUserNameLineEdit;
    UIPasswordLineEdit *m_pPasswordLineEdit;
    UIPasswordLineEdit *m_pPasswordRepeatLineEdit;

    QLabel *m_pUserNameLabel;
    QLabel *m_pPasswordLabel;
    QLabel *m_pPasswordRepeatLabel;

    bool m_fShowPlaceholderText;
    bool m_fLabelsVisible;

    QString m_strPasswordError;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_editors_UIUserNamePasswordEditor_h */
