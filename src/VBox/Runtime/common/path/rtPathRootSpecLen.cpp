/* $Id: rtPathRootSpecLen.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - rtPathRootSpecLen (internal).
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/path.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include "internal/path.h"

/**
 * Figures out the length of the root (or drive) specifier in @a pszPath.
 *
 * For UNC names, we consider the root specifier to include both the server and
 * share names.
 *
 * @returns The length including all slashes.  0 if relative path.
 *
 * @param   pszPath         The path to examine.
 */
DECLHIDDEN(size_t) rtPathRootSpecLen(const char *pszPath)
{
    /*
     * If it's an absolute path, threat the root or volume specification as
     * component 0.  UNC is making this extra fun on OS/2 and Windows as usual.
     */
    size_t off = 0;
    if (RTPATH_IS_SLASH(pszPath[0]))
    {
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
        if (   RTPATH_IS_SLASH(pszPath[1])
            && !RTPATH_IS_SLASH(pszPath[2])
            && pszPath[2])
        {
            /* UNC server name */
            off = 2;
            while (!RTPATH_IS_SLASH(pszPath[off]) && pszPath[off])
                off++;
            while (RTPATH_IS_SLASH(pszPath[off]))
                off++;

            /* UNC share */
            while (!RTPATH_IS_SLASH(pszPath[off]) && pszPath[off])
                off++;
        }
        else
#endif
        {
            off = 1;
        }
        while (RTPATH_IS_SLASH(pszPath[off]))
            off++;
    }
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    else if (RT_C_IS_ALPHA(pszPath[0]) && pszPath[1] == ':')
    {
        off = 2;
        while (RTPATH_IS_SLASH(pszPath[off]))
            off++;
    }
#endif
    Assert(!RTPATH_IS_SLASH(pszPath[off]));

    return off;
}

