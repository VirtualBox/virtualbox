/* $Id: UIPathOperations.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPathOperations class implementation.
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

/* Qt includes: */
#include <QList>

/* GUI includes: */
#include "UIPathOperations.h"

const QChar UIPathOperations::delimiter = QChar('/');
const QChar UIPathOperations::dosDelimiter = QChar('\\');

/* static */ QString UIPathOperations::removeMultipleDelimiters(const QString &path)
{
    QString newPath(path);
    QString doubleDelimiter(2, delimiter);

    while (newPath.contains(doubleDelimiter) && !newPath.isEmpty())
        newPath = newPath.replace(doubleDelimiter, delimiter);
    return newPath;
}

/* static */ QString UIPathOperations::removeTrailingDelimiters(const QString &path)
{
    if (path.isNull() || path.isEmpty())
        return QString();
    QString newPath(path);
    /* Make sure for we dont have any trailing delimiters: */
    while (newPath.length() > 1 && newPath.at(newPath.length() - 1) == UIPathOperations::delimiter)
        newPath.chop(1);
    return newPath;
}

/* static */ QString UIPathOperations::addTrailingDelimiters(const QString &path)
{
    if (path.isNull() || path.isEmpty())
        return QString();
    QString newPath(path);
    while (newPath.length() > 1 && newPath.at(newPath.length() - 1) != UIPathOperations::delimiter)
        newPath += UIPathOperations::delimiter;
    return newPath;
}

/* static */ QString UIPathOperations::addStartDelimiter(const QString &path)
{
    if (path.isEmpty())
        return QString(path);
    QString newPath(path);

    if (doesPathStartWithDriveLetter(newPath))
    {
        if (newPath.length() == 2)
        {
            newPath += delimiter;
            return newPath;
        }
        if (newPath.at(2) != delimiter)
            newPath.insert(2, delimiter);
        return newPath;
    }
    if (newPath.at(0) != delimiter)
        newPath.insert(0, delimiter);
    return newPath;
}

/* static */ QString UIPathOperations::sanitize(const QString &path)
{
    QString newPath = addStartDelimiter(removeTrailingDelimiters(removeMultipleDelimiters(path))).replace(dosDelimiter, delimiter);
    return newPath;
}

/* static */ QString UIPathOperations::mergePaths(const QString &path, const QString &baseName)
{
    QString newBase(baseName);
    newBase = newBase.remove(delimiter);

    /* make sure we have one and only one trailing '/': */
    QString newPath(sanitize(path));
    if(newPath.isEmpty())
        newPath = delimiter;
    if(newPath.at(newPath.length() - 1) != delimiter)
        newPath += UIPathOperations::delimiter;
    newPath += newBase;
    return sanitize(newPath);
}

/* static */ QString UIPathOperations::getObjectName(const QString &path)
{
    if (path.length() <= 1)
        return QString(path);

    QString strTemp(sanitize(path));
    if (strTemp.length() < 2)
        return strTemp;
    int lastSlashPosition = strTemp.lastIndexOf(UIPathOperations::delimiter);
    if (lastSlashPosition == -1)
        return QString();
    return strTemp.right(strTemp.length() - lastSlashPosition - 1);
}

/* static */ QString UIPathOperations::getPathExceptObjectName(const QString &path)
{
    if (path.length() <= 1)
        return QString(path);

    QString strTemp(sanitize(path));
    int lastSlashPosition = strTemp.lastIndexOf(UIPathOperations::delimiter);
    if (lastSlashPosition == -1)
        return QString();
    return strTemp.left(lastSlashPosition + 1);
}

/* static */ QString UIPathOperations::constructNewItemPath(const QString &previousPath, const QString &newBaseName)
{
    if (previousPath.length() <= 1)
         return QString(previousPath);
    return sanitize(mergePaths(getPathExceptObjectName(previousPath), newBaseName));
}

/* static */ QStringList UIPathOperations::pathTrail(const QString &path)
{
    return path.split(UIPathOperations::delimiter, Qt::SkipEmptyParts);
}

/* static */ bool UIPathOperations::doesPathStartWithDriveLetter(const QString &path)
{
    if (path.length() < 2)
        return false;
    /* search for ':' with the path: */
    if (!path[0].isLetter())
        return false;
    if (path[1] != ':')
        return false;
    return true;
}

/* static */ const QString UIPathOperations::replaceDosDelimeter(const QString &path)
{
    QString newPath(path);
    return newPath.replace(dosDelimiter, delimiter);
}
