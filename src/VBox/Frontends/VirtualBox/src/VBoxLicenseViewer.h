/* $Id: VBoxLicenseViewer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - VBoxLicenseViewer class declaration.
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

#ifndef FEQT_INCLUDED_SRC_VBoxLicenseViewer_h
#define FEQT_INCLUDED_SRC_VBoxLicenseViewer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QTextBrowser;
class QPushButton;

/** QDialog subclass used to show a user license under linux. */
class SHARED_LIBRARY_STUFF VBoxLicenseViewer : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs license viewer passing @a pParent to the base-class. */
    VBoxLicenseViewer(QWidget *pParent = 0);

    /** Shows license from passed @a strLicenseText. */
    int showLicenseFromString(const QString &strLicenseText);
    /** Shows license from file with passed @a strLicenseFileName. */
    int showLicenseFromFile(const QString &strLicenseFileName);

protected:

    /** Preprocesses Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

    /** Handles Qt show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Executes the dialog. */
    int exec() RT_OVERRIDE;

    /** Handles scroll-bar moving by a certain @a iValue. */
    void sltHandleScrollBarMoved(int iValue);

    /** Uplocks buttons. */
    void sltUnlockButtons();

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Holds the licence text browser instance. */
    QTextBrowser *m_pLicenseBrowser;

    /** Holds the licence agree button instance. */
    QPushButton *m_pButtonAgree;
    /** Holds the licence disagree button instance. */
    QPushButton *m_pButtonDisagree;
};

#endif /* !FEQT_INCLUDED_SRC_VBoxLicenseViewer_h */
