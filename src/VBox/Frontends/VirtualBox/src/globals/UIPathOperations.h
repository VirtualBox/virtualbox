/* $Id: UIPathOperations.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManagerTable class declaration.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIPathOperations_h
#define FEQT_INCLUDED_SRC_globals_UIPathOperations_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"

/** A collection of simple utility functions to manipulate path strings */
class SHARED_LIBRARY_STUFF UIPathOperations
{
public:
    static QString removeMultipleDelimiters(const QString &path);
    static QString removeTrailingDelimiters(const QString &path);
    static QString addTrailingDelimiters(const QString &path);
    static QString addStartDelimiter(const QString &path);
    static QString sanitize(const QString &path);
    /** Merges prefix and suffix by making sure they have a single '/' in between */
    static QString mergePaths(const QString &path, const QString &baseName);
    /** Returns the last part of the @p path. That is the filename or directory name without the path */
    static QString getObjectName(const QString &path);
    /** Removes the object name and return the path */
    static QString getPathExceptObjectName(const QString &path);
    /** Replaces the last part of the @p previusPath with newBaseName */
    static QString constructNewItemPath(const QString &previousPath, const QString &newBaseName);
    /** Splits the path and return it as a QStringList, top most being the 0th element. No delimiters */
    static QStringList pathTrail(const QString &path);
    static const QChar delimiter;
    static const QChar dosDelimiter;
    /** Tries to determine if the path starts with DOS style drive letters. */
    static bool doesPathStartWithDriveLetter(const QString &path);
    static const QString replaceDosDelimeter(const QString &path);
};

#endif /* !FEQT_INCLUDED_SRC_globals_UIPathOperations_h */
