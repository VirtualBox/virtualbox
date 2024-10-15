/* $Id: symlink-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Symbolic Links, POSIX.
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
#define LOG_GROUP RTLOGGROUP_SYMLINK

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iprt/symlink.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include "internal/path.h"



RTDECL(bool) RTSymlinkExists(const char *pszSymlink)
{
    bool fRc = false;
    char const *pszNativeSymlink;
    int rc = rtPathToNative(&pszNativeSymlink, pszSymlink, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat s;
        fRc = !lstat(pszNativeSymlink, &s)
            && S_ISLNK(s.st_mode);

        rtPathFreeNative(pszNativeSymlink, pszSymlink);
    }

    LogFlow(("RTSymlinkExists(%p={%s}): returns %RTbool\n", pszSymlink, pszSymlink, fRc));
    return fRc;
}


RTDECL(bool) RTSymlinkIsDangling(const char *pszSymlink)
{
    bool fRc = false;
    char const *pszNativeSymlink;
    int rc = rtPathToNative(&pszNativeSymlink, pszSymlink, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat s;
        fRc = !lstat(pszNativeSymlink, &s)
            && S_ISLNK(s.st_mode);
        if (fRc)
        {
            errno = 0;
            fRc = stat(pszNativeSymlink, &s) != 0
               && (   errno == ENOENT
                   || errno == ENOTDIR
                   || errno == ELOOP);
        }

        rtPathFreeNative(pszNativeSymlink, pszSymlink);
    }

    LogFlow(("RTSymlinkIsDangling(%p={%s}): returns %RTbool\n", pszSymlink, pszSymlink, fRc));
    return fRc;
}


RTDECL(int) RTSymlinkCreate(const char *pszSymlink, const char *pszTarget, RTSYMLINKTYPE enmType, uint32_t fCreate)
{
    RT_NOREF_PV(fCreate);

    /*
     * Validate the input.
     */
    AssertReturn(enmType > RTSYMLINKTYPE_INVALID && enmType < RTSYMLINKTYPE_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszSymlink, VERR_INVALID_POINTER);
    AssertPtrReturn(pszTarget, VERR_INVALID_POINTER);

    /*
     * Convert the paths.
     */
    char const *pszNativeSymlink;
    int rc = rtPathToNative(&pszNativeSymlink, pszSymlink, NULL);
    if (RT_SUCCESS(rc))
    {
        const char *pszNativeTarget;
        rc = rtPathToNative(&pszNativeTarget, pszTarget, NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create the link.
             */
            if (symlink(pszNativeTarget, pszNativeSymlink) == 0)
                rc = VINF_SUCCESS;
            else
                rc = RTErrConvertFromErrno(errno);

            rtPathFreeNative(pszNativeTarget, pszTarget);
        }
        rtPathFreeNative(pszNativeSymlink, pszSymlink);
    }

    LogFlow(("RTSymlinkCreate(%p={%s}, %p={%s}, %d, %#x): returns %Rrc\n", pszSymlink, pszSymlink, pszTarget, pszTarget, enmType, fCreate, rc));
    return rc;
}


RTDECL(int) RTSymlinkDelete(const char *pszSymlink, uint32_t fDelete)
{
    RT_NOREF_PV(fDelete);

    char const *pszNativeSymlink;
    int rc = rtPathToNative(&pszNativeSymlink, pszSymlink, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat s;
        if (!lstat(pszNativeSymlink, &s))
        {
            if (S_ISLNK(s.st_mode))
            {
                if (unlink(pszNativeSymlink) == 0)
                    rc = VINF_SUCCESS;
                else
                    rc = RTErrConvertFromErrno(errno);
            }
            else
                rc = VERR_NOT_SYMLINK;
        }
        else
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativeSymlink, pszSymlink);
    }

    LogFlow(("RTSymlinkDelete(%p={%s}, #%x): returns %Rrc\n", pszSymlink, pszSymlink, fDelete, rc));
    return rc;
}


RTDECL(int) RTSymlinkRead(const char *pszSymlink, char *pszTarget, size_t cbTarget, uint32_t fRead)
{
    RT_NOREF_PV(fRead);

    char *pszMyTarget;
    int rc = RTSymlinkReadA(pszSymlink, &pszMyTarget);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrCopy(pszTarget, cbTarget, pszMyTarget);
        RTStrFree(pszMyTarget);
    }
    LogFlow(("RTSymlinkRead(%p={%s}): returns %Rrc\n", pszSymlink, pszSymlink, rc));
    return rc;
}


RTDECL(int) RTSymlinkReadA(const char *pszSymlink, char **ppszTarget)
{
    AssertPtr(ppszTarget);
    char const *pszNativeSymlink;
    int rc = rtPathToNative(&pszNativeSymlink, pszSymlink, NULL);
    if (RT_SUCCESS(rc))
    {
        /* Guess the initial buffer size. */
        ssize_t     cbBuf;
        struct stat s;
        if (!lstat(pszNativeSymlink, &s))
            cbBuf = RT_MAX(RT_ALIGN_Z(s.st_size, 64), 64);
        else
            cbBuf = 1024;

        /* Read loop that grows the buffer. */
        char *pszBuf = NULL;
        for (;;)
        {
            RTMemTmpFree(pszBuf);
            pszBuf = (char *)RTMemTmpAlloc(cbBuf);
            if (pszBuf)
            {
                ssize_t cbReturned = readlink(pszNativeSymlink, pszBuf, cbBuf);
                if (cbReturned >= cbBuf)
                {
                    /* Increase the buffer size and try again */
                    cbBuf *= 2;
                    continue;
                }

                if (cbReturned > 0)
                {
                    pszBuf[cbReturned] = '\0';
                    rc = rtPathFromNativeDup(ppszTarget, pszBuf, pszSymlink);
                }
                else if (errno == EINVAL)
                    rc = VERR_NOT_SYMLINK;
                else
                    rc = RTErrConvertFromErrno(errno);
            }
            else
                rc = VERR_NO_TMP_MEMORY;
            break;
        } /* for loop */

        RTMemTmpFree(pszBuf);
        rtPathFreeNative(pszNativeSymlink, pszSymlink);
    }

    if (RT_SUCCESS(rc))
        LogFlow(("RTSymlinkReadA(%p={%s},%p): returns %Rrc *ppszTarget=%p:{%s}\n", pszSymlink, pszSymlink, ppszTarget, rc, *ppszTarget, *ppszTarget));
    else
        LogFlow(("RTSymlinkReadA(%p={%s},%p): returns %Rrc\n", pszSymlink, pszSymlink, ppszTarget, rc));
    return rc;
}

