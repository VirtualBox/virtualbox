/* $Id: DnDMIME.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD - Path list class.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/GuestHost/DragAndDrop.h>

#include <iprt/string.h>


/**
 * Returns whether a given MIME format contains file URLs we can work with or not.
 *
 * @returns \c true if the format contains file URLs, \c false if not.
 * @param   pcszFormat          MIME format to check.
 * @param   cchFormatMax        Maximum characters of MIME format to check.
 */
bool DnDMIMEHasFileURLs(const char *pcszFormat, size_t cchFormatMax)
{
    /** @todo "text/uri" also an official variant? */
    return (   RTStrNICmp(pcszFormat, "text/uri-list", cchFormatMax)             == 0
            || RTStrNICmp(pcszFormat, "x-special/gnome-icon-list", cchFormatMax) == 0);
}

/**
 * Returns whether a given MIME format needs an own "Dropped Files" directory or not.
 *
 * @returns \c true if the format needs an own "Dropped Files" directory, \c false if not.
 * @param   pcszFormat          MIME format to check.
 * @param   cchFormatMax        Maximum characters of MIME format to check.
 */
bool DnDMIMENeedsDropDir(const char *pcszFormat, size_t cchFormatMax)
{
    return DnDMIMEHasFileURLs(pcszFormat, cchFormatMax);
}

