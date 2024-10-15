/* $Id: QIProcess.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIProcess class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIProcess_h
#define FEQT_INCLUDED_SRC_extensions_QIProcess_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QProcess>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QProcess extension for VBox GUI needs. */
class SHARED_LIBRARY_STUFF QIProcess : public QProcess
{
    Q_OBJECT;

    /** Constructs our own file-dialog passing @a pParent to the base-class.
      * Doesn't mean to be used directly, cause this subclass is a bunch of statics. */
    QIProcess(QObject *pParent = 0);

public:

    /** Execute certain script specified by @a strProcessName
      * and wait up to specified @a iTimeout amount of time for responce. */
    static QByteArray singleShot(const QString &strProcessName,
                                 int iTimeout = 5000);
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIProcess_h */
