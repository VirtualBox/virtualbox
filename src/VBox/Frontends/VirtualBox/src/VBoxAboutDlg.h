/* $Id: VBoxAboutDlg.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - VBoxAboutDlg class declaration.
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

#ifndef FEQT_INCLUDED_SRC_VBoxAboutDlg_h
#define FEQT_INCLUDED_SRC_VBoxAboutDlg_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>
#include <QPixmap>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QVBoxLayout;

/** QDialog extension
  * used to show the About-VirtualBox dialog. */
class SHARED_LIBRARY_STUFF VBoxAboutDlg : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs dialog passing @a pParent to the base-class.
      * @param  strVersion  Brings the version number of VirtualBox. */
    VBoxAboutDlg(QWidget *pParent, const QString &strVersion);

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares main-layout. */
    void prepareMainLayout();
    /** Prepares label. */
    void prepareLabel();
    /** Prepares close-button. */
    void prepareCloseButton();

    /** Holds the pseudo-parent widget reference. */
    QWidget *m_pPseudoParent;

    /** Holds whether window is polished. */
    bool  m_fPolished;

    /** Holds the About-VirtualBox text. */
    QString  m_strAboutText;
    /** Holds the VirtualBox version number. */
    QString  m_strVersion;

    /** Holds the About-VirtualBox image. */
    QPixmap  m_pixmap;
    /** Holds the About-VirtualBox dialog size. */
    QSize    m_size;

    /** Holds About-VirtualBox main-layout instance. */
    QVBoxLayout *m_pMainLayout;
    /** Holds About-VirtualBox text-label instance. */
    QLabel      *m_pLabel;
};

#endif /* !FEQT_INCLUDED_SRC_VBoxAboutDlg_h */
