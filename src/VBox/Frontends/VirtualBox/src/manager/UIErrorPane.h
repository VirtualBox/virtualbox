/* $Id: UIErrorPane.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIErrorPane class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIErrorPane_h
#define FEQT_INCLUDED_SRC_manager_UIErrorPane_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* Forward declarations: */
class QTextBrowser;

/** QWidget subclass representing error pane reflecting
  * information about currently chosen inaccessible VM. */
class UIErrorPane : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs error pane passing @a pParent to the base-class. */
    UIErrorPane(QWidget *pParent = 0);

    /** Defines error @a strDetails. */
    void setErrorDetails(const QString &strDetails);

private:

    /** Prepares all. */
    void prepare();

    /** Holds the text-browser instance. */
    QTextBrowser *m_pBrowserDetails;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIErrorPane_h */
