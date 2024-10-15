/* $Id: ClipboardMIME.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard - Path list class.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/GuestHost/SharedClipboard-transfers.h>

#include <iprt/string.h>


bool ShClMIMEHasFileURLs(const char *pcszFormat, size_t cchFormatMax)
{
    /** @todo "text/uri" also an official variant? */
    return (   RTStrNICmp(pcszFormat, "text/uri-list", cchFormatMax)             == 0
            || RTStrNICmp(pcszFormat, "x-special/gnome-icon-list", cchFormatMax) == 0);
}

bool ShClMIMENeedsCache(const char *pcszFormat, size_t cchFormatMax)
{
    bool fNeedsDropDir = false;
    if (!RTStrNICmp(pcszFormat, "text/uri-list", cchFormatMax)) /** @todo Add "x-special/gnome-icon-list"? */
        fNeedsDropDir = true;

    return fNeedsDropDir;
}

