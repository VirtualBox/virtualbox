/* $Id: path-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Manipulation, POSIX, Part 1.
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
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/path.h"
#include "internal/process.h"
#include "internal/fs.h"



RTDECL(int) RTPathReal(const char *pszPath, char *pszRealPath, size_t cchRealPath)
{
    /*
     * Convert input.
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * On POSIX platforms the API doesn't take a length parameter, which makes it
         * a little bit more work.
         */
        char szTmpPath[PATH_MAX + 1];
        const char *psz = realpath(pszNativePath, szTmpPath);
        if (psz)
            rc = rtPathFromNativeCopy(pszRealPath, cchRealPath, szTmpPath, NULL);
        else
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTPathReal(%p:{%s}, %p:{%s}, %u): returns %Rrc\n", pszPath, pszPath,
             pszRealPath, RT_SUCCESS(rc) ? pszRealPath : "<failed>",  cchRealPath, rc));
    return rc;
}


RTR3DECL(int) RTPathSetMode(const char *pszPath, RTFMODE fMode)
{
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);

    int rc;
    fMode = rtFsModeNormalize(fMode, pszPath, 0, 0);
    if (rtFsModeIsValidPermissions(fMode))
    {
        char const *pszNativePath;
        rc = rtPathToNative(&pszNativePath, pszPath, NULL);
        if (RT_SUCCESS(rc))
        {
            if (chmod(pszNativePath, fMode & RTFS_UNIX_MASK) != 0)
                rc = RTErrConvertFromErrno(errno);
            rtPathFreeNative(pszNativePath, pszPath);
        }
    }
    else
    {
        AssertMsgFailed(("Invalid file mode! %RTfmode\n", fMode));
        rc = VERR_INVALID_FMODE;
    }
    return rc;
}


/**
 * Checks if two files are the one and same file.
 */
static bool rtPathSame(const char *pszNativeSrc, const char *pszNativeDst)
{
    struct stat SrcStat;
    if (lstat(pszNativeSrc, &SrcStat))
        return false;
    struct stat DstStat;
    if (lstat(pszNativeDst, &DstStat))
        return false;
    Assert(SrcStat.st_dev && DstStat.st_dev);
    Assert(SrcStat.st_ino && DstStat.st_ino);
    if (    SrcStat.st_dev == DstStat.st_dev
        &&  SrcStat.st_ino == DstStat.st_ino
        &&  (SrcStat.st_mode & S_IFMT) == (DstStat.st_mode & S_IFMT))
        return true;
    return false;
}


/**
 * Worker for RTPathRename, RTDirRename, RTFileRename.
 *
 * @returns IPRT status code.
 * @param   pszSrc      The source path.
 * @param   pszDst      The destination path.
 * @param   fRename     The rename flags.
 * @param   fFileType   The filetype. We use the RTFMODE filetypes here. If it's 0,
 *                      anything goes. If it's RTFS_TYPE_DIRECTORY we'll check that the
 *                      source is a directory. If Its RTFS_TYPE_FILE we'll check that it's
 *                      not a directory (we are NOT checking whether it's a file).
 */
DECLHIDDEN(int) rtPathPosixRename(const char *pszSrc, const char *pszDst, unsigned fRename, RTFMODE fFileType)
{
    /*
     * Convert the paths.
     */
    char const *pszNativeSrc;
    int rc = rtPathToNative(&pszNativeSrc, pszSrc, NULL);
    if (RT_SUCCESS(rc))
    {
        char const *pszNativeDst;
        rc = rtPathToNative(&pszNativeDst, pszDst, NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Check that the source exists and that any types that's specified matches.
             * We have to check this first to avoid getting errnous VERR_ALREADY_EXISTS
             * errors from the next step.
             *
             * There are race conditions here (perhaps unlikely ones, but still), but I'm
             * afraid there is little with can do to fix that.
             */
            struct stat SrcStat;
            if (lstat(pszNativeSrc, &SrcStat))
                rc = RTErrConvertFromErrno(errno);
            else if (!fFileType)
                rc = VINF_SUCCESS;
            else if (RTFS_IS_DIRECTORY(fFileType))
                rc = S_ISDIR(SrcStat.st_mode) ? VINF_SUCCESS : VERR_NOT_A_DIRECTORY;
            else
                rc = S_ISDIR(SrcStat.st_mode) ? VERR_IS_A_DIRECTORY : VINF_SUCCESS;
            if (RT_SUCCESS(rc))
            {
                bool fSameFile = false;

                /*
                 * Check if the target exists, rename is rather destructive.
                 * We'll have to make sure we don't overwrite the source!
                 * Another race condition btw.
                 */
                struct stat DstStat;
                if (lstat(pszNativeDst, &DstStat))
                    rc = errno == ENOENT ? VINF_SUCCESS : RTErrConvertFromErrno(errno);
                else
                {
                    Assert(SrcStat.st_dev && DstStat.st_dev);
                    Assert(SrcStat.st_ino && DstStat.st_ino);
                    if (    SrcStat.st_dev == DstStat.st_dev
                        &&  SrcStat.st_ino == DstStat.st_ino
                        &&  (SrcStat.st_mode & S_IFMT) == (DstStat.st_mode & S_IFMT))
                    {
                        /*
                         * It's likely that we're talking about the same file here.
                         * We should probably check paths or whatever, but for now this'll have to be enough.
                         */
                        fSameFile = true;
                    }
                    if (fSameFile)
                        rc = VINF_SUCCESS;
                    else if (S_ISDIR(DstStat.st_mode) || !(fRename & RTPATHRENAME_FLAGS_REPLACE))
                        rc = VERR_ALREADY_EXISTS;
                    else
                        rc = VINF_SUCCESS;

                }
                if (RT_SUCCESS(rc))
                {
                    if (!rename(pszNativeSrc, pszNativeDst))
                        rc = VINF_SUCCESS;
                    else if (   (fRename & RTPATHRENAME_FLAGS_REPLACE)
                             && (errno == ENOTDIR || errno == EEXIST))
                    {
                        /*
                         * Check that the destination isn't a directory.
                         * Yet another race condition.
                         */
                        if (rtPathSame(pszNativeSrc, pszNativeDst))
                        {
                            rc = VINF_SUCCESS;
                            Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): appears to be the same file... (errno=%d)\n",
                                 pszSrc, pszDst, fRename, fFileType, errno));
                        }
                        else
                        {
                            if (lstat(pszNativeDst, &DstStat))
                                rc = errno != ENOENT ? RTErrConvertFromErrno(errno) : VINF_SUCCESS;
                            else if (S_ISDIR(DstStat.st_mode))
                                rc = VERR_ALREADY_EXISTS;
                            else
                                rc = VINF_SUCCESS;
                            if (RT_SUCCESS(rc))
                            {
                                if (!unlink(pszNativeDst))
                                {
                                    if (!rename(pszNativeSrc, pszNativeDst))
                                        rc = VINF_SUCCESS;
                                    else
                                    {
                                        rc = RTErrConvertFromErrno(errno);
                                        Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): rename failed rc=%Rrc errno=%d\n",
                                             pszSrc, pszDst, fRename, fFileType, rc, errno));
                                    }
                                }
                                else
                                {
                                    rc = RTErrConvertFromErrno(errno);
                                    Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): failed to unlink dst rc=%Rrc errno=%d\n",
                                         pszSrc, pszDst, fRename, fFileType, rc, errno));
                                }
                            }
                            else
                                Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): dst !dir check failed rc=%Rrc\n",
                                     pszSrc, pszDst, fRename, fFileType, rc));
                        }
                    }
                    else
                    {
                        rc = RTErrConvertFromErrno(errno);
                        if (errno == ENOTDIR)
                            rc = VERR_ALREADY_EXISTS; /* unless somebody is racing us, this is the right interpretation */
                        Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): rename failed rc=%Rrc errno=%d\n",
                             pszSrc, pszDst, fRename, fFileType, rc, errno));
                    }
                }
                else
                    Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): destination check failed rc=%Rrc errno=%d\n",
                         pszSrc, pszDst, fRename, fFileType, rc, errno));
            }
            else
                Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): source type check failed rc=%Rrc errno=%d\n",
                     pszSrc, pszDst, fRename, fFileType, rc, errno));

            rtPathFreeNative(pszNativeDst, pszDst);
        }
        rtPathFreeNative(pszNativeSrc, pszSrc);
    }
    return rc;
}


RTR3DECL(int) RTPathRename(const char *pszSrc, const char *pszDst, unsigned fRename)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fRename & ~RTPATHRENAME_FLAGS_REPLACE), ("%#x\n", fRename), VERR_INVALID_PARAMETER);

    /*
     * Hand it to the worker.
     */
    int rc = rtPathPosixRename(pszSrc, pszDst, fRename, 0);

    Log(("RTPathRename(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n", pszSrc, pszSrc, pszDst, pszDst, fRename, rc));
    return rc;
}


RTR3DECL(int) RTPathUnlink(const char *pszPath, uint32_t fUnlink)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_NAME);
    AssertReturn(!(fUnlink & ~RTPATHUNLINK_FLAGS_NO_SYMLINKS), VERR_INVALID_FLAGS);

    /*
     * Convert the path.
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check if it's a directory using lstat, since unlink() may have adverse
         * side effects when running as root on some file systems (at least on
         * Solaris).
         *
         * There are of course race conditions here, but w/o a NT style removal
         * API it is not possible to do this w/o a race.
         *
         * Note! We cannot just use rmdir here, in case the final entity is a
         *       symlink pointing at a directory, as we're supposed to remove
         *       the symlink when that's the case.
         */
        struct stat Stat;
        rc = lstat(pszNativePath, &Stat);
        if (rc || !S_ISDIR(Stat.st_mode))
        {
            rc = unlink(pszNativePath);
            if (rc == 0)
                rc = VINF_SUCCESS;
            else
            {
                rc = errno;
                if (rc != ENOENT)
                    rc = RTErrConvertFromErrno(rc);
                else
                {
                    /*
                     * Make the path-not-found match windows.
                     */
                    rc = VERR_FILE_NOT_FOUND;
                    size_t cch = strlen(pszNativePath);
                    while (cch > 0 && RTPATH_IS_SLASH(pszNativePath[cch - 1]))
                        cch--;
                    while (cch > 0 && !RTPATH_IS_SLASH(pszNativePath[cch - 1]))
                        cch--;
                    while (cch > 0 && RTPATH_IS_SLASH(pszNativePath[cch - 1]))
                        cch--;
                    if (cch >= 1)
                    {
                        char *pszParent = (char *)RTMemTmpAlloc(cch + 1);
                        if (pszParent)
                        {
                            memcpy(pszParent, pszNativePath, cch);
                            pszParent[cch] = '\0';
                            if (stat(pszParent, &Stat) && errno == ENOENT)
                                rc = VERR_PATH_NOT_FOUND;
                            RTMemTmpFree(pszParent);
                        }
                    }
                }
            }
        }
        else
        {
            rc = rmdir(pszNativePath);
            if (rc)
            {
                rc = errno;
                if (rc == EEXIST) /* Solaris returns this, the rest have ENOTEMPTY. */
                    rc = VERR_DIR_NOT_EMPTY;
                else
                    rc = RTErrConvertFromErrno(rc);
            }
        }

        rtPathFreeNative(pszNativePath, pszPath);
    }
    return rc;
}


RTDECL(bool) RTPathExists(const char *pszPath)
{
    return RTPathExistsEx(pszPath, RTPATH_F_FOLLOW_LINK);
}


RTDECL(bool) RTPathExistsEx(const char *pszPath, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, false);
    AssertReturn(*pszPath, false);
    Assert(RTPATH_F_IS_VALID(fFlags, 0));

    /*
     * Convert the path and check if it exists using stat().
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat Stat;
        if (fFlags & RTPATH_F_FOLLOW_LINK)
            rc = stat(pszNativePath, &Stat);
        else
            rc = lstat(pszNativePath, &Stat);
        if (!rc)
            rc = VINF_SUCCESS;
        else
            rc = VERR_GENERAL_FAILURE;
        rtPathFreeNative(pszNativePath, pszPath);
    }
    return RT_SUCCESS(rc);
}


RTDECL(int)  RTPathGetCurrent(char *pszPath, size_t cchPath)
{
    /*
     * Try with a reasonably sized buffer first.
     */
    char szNativeCurDir[RTPATH_MAX];
    if (getcwd(szNativeCurDir, sizeof(szNativeCurDir)) != NULL)
        return rtPathFromNativeCopy(pszPath, cchPath, szNativeCurDir, NULL);

    /*
     * Retry a few times with really big buffers if we failed because CWD is unreasonably long.
     */
    int iErr = errno;
    if (iErr != ERANGE)
        return RTErrConvertFromErrno(iErr);

    size_t cbNativeTmp = RTPATH_BIG_MAX;
    for (;;)
    {
        char *pszNativeTmp = (char *)RTMemTmpAlloc(cbNativeTmp);
        if (!pszNativeTmp)
            return VERR_NO_TMP_MEMORY;
        if (getcwd(pszNativeTmp, cbNativeTmp) != NULL)
        {
            int rc = rtPathFromNativeCopy(pszPath, cchPath, pszNativeTmp, NULL);
            RTMemTmpFree(pszNativeTmp);
            return rc;
        }
        iErr = errno;
        RTMemTmpFree(pszNativeTmp);
        if (iErr != ERANGE)
            return RTErrConvertFromErrno(iErr);

        cbNativeTmp += RTPATH_BIG_MAX;
        if (cbNativeTmp > RTPATH_BIG_MAX * 4)
            return VERR_FILENAME_TOO_LONG;
    }
}


RTDECL(int) RTPathSetCurrent(const char *pszPath)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);

    /*
     * Change the directory.
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        if (chdir(pszNativePath))
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativePath, pszPath);
    }
    return rc;
}

