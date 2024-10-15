/* $Id: UITakeSnapshotDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITakeSnapshotDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_UITakeSnapshotDialog_h
#define FEQT_INCLUDED_SRC_UITakeSnapshotDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>

/* GUI includes: */
#include "QIDialog.h"
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QLineEdit;
class QTextEdit;
class QIDialogButtonBox;
class QILabel;

/** QIDialog subclass for taking snapshot name/description. */
class SHARED_LIBRARY_STUFF UITakeSnapshotDialog : public QIDialog
{
    Q_OBJECT;

public:

    /** Constructs take snapshot dialog passing @ pParent to the base-class.
      * @param  cImmutableMedia  Brings the amount of immutable mediums. */
    UITakeSnapshotDialog(QWidget *pParent, ulong cImmutableMedia);

    /** Defines snapshot @a icon. */
    void setIcon(const QIcon &icon);

    /** Defines snapshot @a strName. */
    void setName(const QString &strName);

    /** Returns snapshot name. */
    QString name() const;
    /** Returns snapshot description. */
    QString description() const;

protected:

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles @a strName change signal. */
    void sltHandleNameChanged(const QString &strName);

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares contents. */
    void prepareContents();

    /** Updates pixmap. */
    void updatePixmap();

    /** Holds the amount of immutable mediums. */
    const ulong  m_cImmutableMedia;

    /** Holds the snapshot icon. */
    QIcon m_icon;

    /** Holds the icon label instance. */
    QLabel *m_pLabelIcon;

    /** Holds the name label instance. */
    QLabel    *m_pLabelName;
    /** Holds the name editor instance. */
    QLineEdit *m_pEditorName;

    /** Holds the description label instance. */
    QLabel    *m_pLabelDescription;
    /** Holds the description editor instance. */
    QTextEdit *m_pEditorDescription;

    /** Holds the information label instance. */
    QILabel *m_pLabelInfo;

    /** Holds the dialog button-box instance. */
    QIDialogButtonBox *m_pButtonBox;
};

#endif /* !FEQT_INCLUDED_SRC_UITakeSnapshotDialog_h */
