/* $Id: UIDesktopServices_darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt GUI - Utility Classes and Functions specific to darwin..
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

/* VBox includes */
#include "UIDesktopServices.h"
#include "UIDesktopServices_darwin_p.h"
#include "VBoxUtils-darwin.h"

/* Qt includes */
#include <QString>

bool UIDesktopServices::createMachineShortcut(const QString &strSrcFile, const QString &strDstPath, const QString &strName, const QUuid &uUuid)
{
    return ::darwinCreateMachineShortcut(::darwinToNativeString(strSrcFile.toUtf8().constData()),
                                         ::darwinToNativeString(strDstPath.toUtf8().constData()),
                                         ::darwinToNativeString(strName.toUtf8().constData()),
                                         ::darwinToNativeString(uUuid.toString().toUtf8().constData()));
}

bool UIDesktopServices::openInFileManager(const QString &strFile)
{
    return ::darwinOpenInFileManager(::darwinToNativeString(strFile.toUtf8().constData()));
}

