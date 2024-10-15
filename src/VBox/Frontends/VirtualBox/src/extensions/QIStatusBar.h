/* $Id: QIStatusBar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIStatusBar class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIStatusBar_h
#define FEQT_INCLUDED_SRC_extensions_QIStatusBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QStatusBar>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QStatusBar extension with advanced functionality. */
class SHARED_LIBRARY_STUFF QIStatusBar : public QStatusBar
{
    Q_OBJECT;

public:

    /** Constructs status-bar passing @a pParent to the base-class. */
    QIStatusBar(QWidget *pParent = 0);

protected slots:

    /** Remembers the last status @a strMessage. */
    void sltRememberLastMessage(const QString &strMessage) { m_strMessage = strMessage; }

protected:

    /** Holds the last status message. */
    QString m_strMessage;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIStatusBar_h */
