/* $Id: RTPathUserHome-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Manipulation, POSIX.
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
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>

#include <iprt/path.h>
#include <iprt/env.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/path.h"
#include "internal/fs.h"


#ifndef RT_OS_L4
/**
 * Worker for RTPathUserHome that looks up the home directory
 * using the getpwuid_r api.
 *
 * @returns IPRT status code.
 * @param   pszPath     The path buffer.
 * @param   cchPath     The size of the buffer.
 * @param   uid         The User ID to query the home directory of.
 */
static int rtPathUserHomeByPasswd(char *pszPath, size_t cchPath, uid_t uid)
{
    /*
     * The getpwuid_r function uses the passed in buffer to "allocate" any
     * extra memory it needs. On some systems we should probably use the
     * sysconf function to find the appropriate buffer size, but since it won't
     * work everywhere we'll settle with a 5KB buffer and ASSUME that it'll
     * suffice for even the lengthiest user descriptions...
     */
    char            achBuffer[5120];
    struct passwd   Passwd;
    struct passwd  *pPasswd;
    memset(&Passwd, 0, sizeof(Passwd));
    int rc = getpwuid_r(uid, &Passwd, &achBuffer[0], sizeof(achBuffer), &pPasswd);
    if (rc != 0)
        return RTErrConvertFromErrno(rc);
    if (!pPasswd) /* uid not found in /etc/passwd */
        return VERR_PATH_NOT_FOUND;

    /*
     * Check that it isn't empty and that it exists.
     */
    struct stat st;
    if (    !pPasswd->pw_dir
        ||  !*pPasswd->pw_dir
        ||  stat(pPasswd->pw_dir, &st)
        ||  !S_ISDIR(st.st_mode))
        return VERR_PATH_NOT_FOUND;

    /*
     * Convert it to UTF-8 and copy it to the return buffer.
     */
    return rtPathFromNativeCopy(pszPath, cchPath, pPasswd->pw_dir, NULL);
}
#endif


/**
 * Worker for RTPathUserHome that looks up the home directory
 * using the HOME environment variable.
 *
 * @returns IPRT status code.
 * @param   pszPath     The path buffer.
 * @param   cchPath     The size of the buffer.
 */
static int rtPathUserHomeByEnv(char *pszPath, size_t cchPath)
{
    /*
     * Get HOME env. var it and validate it's existance.
     */
    int         rc      = VERR_PATH_NOT_FOUND;
    const char *pszHome = RTEnvGet("HOME"); /** @todo Codeset confusion in RTEnv. */
    if (pszHome)

    {
        struct stat st;
        if (    !stat(pszHome, &st)
            &&  S_ISDIR(st.st_mode))
            rc = rtPathFromNativeCopy(pszPath, cchPath, pszHome, NULL);
    }
    return rc;
}


RTDECL(int) RTPathUserHome(char *pszPath, size_t cchPath)
{
    int rc;
#ifndef RT_OS_L4
    /*
     * We make an exception for the root user and use the system call
     * getpwuid_r to determine their initial home path instead of
     * reading it from the $HOME variable.  This is because the $HOME
     * variable does not get changed by sudo (and possibly su and others)
     * which can cause root-owned files to appear in user's home folders.
     */
     uid_t uid = geteuid();
     if (!uid)
         rc = rtPathUserHomeByPasswd(pszPath, cchPath, uid);
     else
         rc = rtPathUserHomeByEnv(pszPath, cchPath);

     /*
      * On failure, retry using the alternative method.
      * (Should perhaps restrict the retry cases a bit more here...)
      */
     if (   RT_FAILURE(rc)
         && rc != VERR_BUFFER_OVERFLOW)
     {
         if (!uid)
             rc = rtPathUserHomeByEnv(pszPath, cchPath);
         else
             rc = rtPathUserHomeByPasswd(pszPath, cchPath, uid);
     }
#else  /* RT_OS_L4 */
    rc = rtPathUserHomeByEnv(pszPath, cchPath);
#endif /* RT_OS_L4 */

    LogFlow(("RTPathUserHome(%p:{%s}, %u): returns %Rrc\n", pszPath,
             RT_SUCCESS(rc) ? pszPath : "<failed>",  cchPath, rc));
    return rc;
}

