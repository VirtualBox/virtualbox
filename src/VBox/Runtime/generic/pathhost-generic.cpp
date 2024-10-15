/* $Id: pathhost-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Conversions, generic pass through.
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
#include "internal/iprt.h"
#include "internal/path.h"

#include <iprt/assert.h>
#include <iprt/string.h>


int rtPathToNative(char const **ppszNativePath, const char *pszPath, const char *pszBasePath)
{
    *ppszNativePath = pszPath;
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return VINF_SUCCESS;
}


void rtPathFreeNative(char const *pszNativePath, const char *pszPath)
{
    Assert(pszNativePath == pszPath || !pszNativePath);
    NOREF(pszNativePath); NOREF(pszPath);
}


int rtPathFromNative(const char **ppszPath, const char *pszNativePath, const char *pszBasePath)
{
    int rc = RTStrValidateEncodingEx(pszNativePath, RTSTR_MAX, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
        *ppszPath = pszNativePath;
    else
        *ppszPath = NULL;
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}


void rtPathFreeIprt(const char *pszPath, const char *pszNativePath)
{
    Assert(pszPath == pszNativePath || !pszPath);
    NOREF(pszPath); NOREF(pszNativePath);
}


int rtPathFromNativeCopy(char *pszPath, size_t cbPath, const char *pszNativePath, const char *pszBasePath)
{
    int rc = RTStrValidateEncodingEx(pszNativePath, RTSTR_MAX, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
        rc = RTStrCopy(pszPath, cbPath, pszNativePath);
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}


int rtPathFromNativeDup(char **ppszPath, const char *pszNativePath, const char *pszBasePath)
{
    int rc = RTStrValidateEncodingEx(pszNativePath, RTSTR_MAX, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
        rc = RTStrDupEx(ppszPath, pszNativePath);
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}

