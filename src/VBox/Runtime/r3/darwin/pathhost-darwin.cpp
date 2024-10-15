/* $Id: pathhost-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Conversions, Darwin.
 *
 * On darwin path names on the disk are decomposed using normalization
 * form D (NFD).  Since this behavior is unique for the Mac, we will precompose
 * the path name strings we get from the XNU kernel.
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
#include <iprt/assert.h>
#include <iprt/string.h>
#include "internal/iprt.h"

#include "internal/path.h"


int rtPathToNative(const char **ppszNativePath, const char *pszPath, const char *pszBasePath)
{
    /** @todo We should decompose the string here, but the file system will do
     *        that for us if we don't, so why bother. */
    *ppszNativePath = (char *)pszPath;
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return VINF_SUCCESS;
}


void rtPathFreeNative(char const *pszNativePath, const char *pszPath)
{
    Assert(pszNativePath == pszPath || !pszNativePath);
    NOREF(pszNativePath);
    NOREF(pszPath);
}


int rtPathFromNative(char const **ppszPath, const char *pszNativePath, const char *pszBasePath)
{
    /** @todo We must compose the codepoints in the string here.  We get file names
     *        in normalization form D so we'll end up with normalization form C
     *        whatever approach we take. */
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
    /** @todo We must compose the codepoints in the string here.  We get file names
     *        in normalization form D so we'll end up with normalization form C
     *        whatever approach we take. */
    int rc = RTStrValidateEncodingEx(pszNativePath, RTSTR_MAX, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
        rc = RTStrCopyEx(pszPath, cbPath, pszNativePath, RTSTR_MAX);
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}


int rtPathFromNativeDup(char **ppszPath, const char *pszNativePath, const char *pszBasePath)
{
    /** @todo We must compose the codepoints in the string here.  We get file names
     *        in normalization form D so we'll end up with normalization form C
     *        whatever approach we take. */
    int rc = RTStrValidateEncodingEx(pszNativePath, RTSTR_MAX, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
        rc = RTStrDupEx(ppszPath, pszNativePath);
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}

