/* $Id: UIEmptyFilePathSelector.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIEmptyFilePathSelector class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIEmptyFilePathSelector_h
#define FEQT_INCLUDED_SRC_widgets_UIEmptyFilePathSelector_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes */
#include <QComboBox>

/* VBox forward declarations */
class QILabel;
class QILineEdit;

/* Qt forward declarations */
class QHBoxLayout;
class QAction;
class QToolButton;


class UIEmptyFilePathSelector: public QWidget
{
    Q_OBJECT;

public:

    enum Mode
    {
        Mode_Folder = 0,
        Mode_File_Open,
        Mode_File_Save
    };

    enum ButtonPosition
    {
        LeftPosition,
        RightPosition
    };

    UIEmptyFilePathSelector (QWidget *aParent = NULL);

    void setMode (UIEmptyFilePathSelector::Mode aMode);
    UIEmptyFilePathSelector::Mode mode() const;

    void setButtonPosition (ButtonPosition aPos);
    ButtonPosition buttonPosition() const;

    void setEditable (bool aOn);
    bool isEditable() const;

    void setChooserVisible (bool aOn);
    bool isChooserVisible() const;

    QString path() const;

    void setDefaultSaveExt (const QString &aExt);
    QString defaultSaveExt() const;

    bool isModified () const { return mIsModified; }
    void resetModified () { mIsModified = false; }

    void setLineEditToolTip(const QString &strToolTip);
    QString lineEditToolTip() const;
    void setChooseButtonToolTip(const QString &strToolTip);
    QString chooseButtonToolTip() const;

    void setFileDialogTitle (const QString& aTitle);
    QString fileDialogTitle() const;

    void setFileFilters (const QString& aFilters);
    QString fileFilters() const;

    void setHomeDir (const QString& aDir);
    QString homeDir() const;

signals:
    void pathChanged (QString);

public slots:
    void setPath (const QString& aPath);

private slots:

    void sltRetranslateUI();
    void choose();
    void textChanged (const QString& aPath);

private:
    /* Private member vars */
    QHBoxLayout *mMainLayout;
    QWidget *mPathWgt;
    QILabel *mLabel;
    UIEmptyFilePathSelector::Mode mMode;
    QILineEdit *mLineEdit;
    QToolButton *mSelectButton;
    bool m_fLineEditoToolTipSet;
    bool m_fButtonToolTipSet;
    QString mFileDialogTitle;
    QString mFileFilters;
    QString mDefaultSaveExt;
    QString mHomeDir;
    bool mIsModified;
    QString mPath;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIEmptyFilePathSelector_h */
