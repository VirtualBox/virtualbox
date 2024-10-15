/* $Id: RTPathGetCurrentDrive-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathGetCurrentDrive, generic implementation.
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
#define LOG_GROUP RTLOGGROUP_PATH
#include <iprt/path.h>
#include "internal/iprt.h"

#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include "internal/path.h"


RTDECL(int) RTPathGetCurrentDrive(char *pszPath, size_t cbPath)
{
#ifdef HAVE_DRIVE
    /*
     * Query the current directroy and extract the wanted information from it.
     */
    char *pszPathFree = NULL;
    char *pszCwd = pszPath;
    int rc = RTPathGetCurrent(pszCwd, cbPath);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else if (rc != VERR_BUFFER_OVERFLOW)
        return rc;
    else
    {
        pszPathFree = pszCwd = (char *)RTMemTmpAlloc(RTPATH_BIG_MAX);
        AssertReturn(pszPathFree, VERR_NO_TMP_MEMORY);
        rc = RTPathGetCurrent(pszCwd, RTPATH_BIG_MAX);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Drive letter? Chop off at root slash.
         */
        if (pszCwd[0] && RTPATH_IS_VOLSEP(pszCwd[1]))
            pszCwd[2] = '\0';
        /*
         * UNC? Chop off after share.
         */
        else if (   RTPATH_IS_SLASH(pszCwd[0])
                 && RTPATH_IS_SLASH(pszCwd[1])
                 && !RTPATH_IS_SLASH(pszCwd[2])
                 && pszCwd[2])
        {
            /* Work thru the server name. */
            size_t off = 3;
            while (!RTPATH_IS_SLASH(pszCwd[off]) && pszCwd[off])
                off++;
            size_t offServerSlash = off;

            /* Is there a share name? */
            if (RTPATH_IS_SLASH(pszCwd[off]))
            {
                while (RTPATH_IS_SLASH(pszCwd[off]))
                    off++;
                if (pszCwd[off])
                {
                    /* Work thru the share name. */
                    while (!RTPATH_IS_SLASH(pszCwd[off]) && pszCwd[off])
                        off++;
                }
                /* No share name, chop at server name. */
                else
                    off = offServerSlash;
            }
            pszCwd[off] = '\0';
        }
        else
            rc = VERR_INTERNAL_ERROR_4;
    }
    if (pszPathFree)
    {
        if (RT_SUCCESS(rc))
            rc = RTStrCopy(pszPath, cbPath, pszCwd);
        RTMemTmpFree(pszPathFree);
    }
    return rc;

#else  /* !HAVE_DRIVE */
    /*
     * No drive letters on this system, return empty string.
     */
    if (cbPath > 0)
    {
        *pszPath = '\0';
        return VINF_SUCCESS;
    }
    return VERR_BUFFER_OVERFLOW;
#endif /* !HAVE_DRIVE */
}
RT_EXPORT_SYMBOL(RTPathGetCurrentDrive);

