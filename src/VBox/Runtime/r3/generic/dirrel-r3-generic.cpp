/* $Id: dirrel-r3-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Directory relative base APIs, generic implementation.
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
#define LOG_GROUP RTLOGGROUP_DIR
#include <iprt/dir.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#define RTDIR_AGNOSTIC
#include "internal/dir.h"



/**
 * Helper that builds a full path for a directory relative path.
 *
 * @returns IPRT status code.
 * @param   pThis               The directory.
 * @param   pszPathDst          The destination buffer.
 * @param   cbPathDst           The size of the destination buffer.
 * @param   pszRelPath          The relative path.
 */
static int rtDirRelBuildFullPath(PRTDIRINTERNAL pThis, char *pszPathDst, size_t cbPathDst, const char *pszRelPath)
{
    AssertMsgReturn(!RTPathStartsWithRoot(pszRelPath), ("pszRelPath='%s'\n", pszRelPath), VERR_PATH_IS_NOT_RELATIVE);

    /*
     * Let's hope we can avoid checking for ascension.
     *
     * Note! We don't take symbolic links into account here.  That can be
     *       done later if desired.
     */
    if (   !(pThis->fFlags & RTDIR_F_DENY_ASCENT)
        || strstr(pszRelPath, "..") == NULL)
    {
        size_t const cchRelPath = strlen(pszRelPath);
        size_t const cchDirPath = pThis->cchPath;
        if (cchDirPath + cchRelPath < cbPathDst)
        {
            memcpy(pszPathDst, pThis->pszPath, cchDirPath);
            memcpy(&pszPathDst[cchDirPath], pszRelPath, cchRelPath);
            pszPathDst[cchDirPath + cchRelPath] = '\0';
            return VINF_SUCCESS;
        }
        return VERR_FILENAME_TOO_LONG;
    }

    /*
     * Calc the absolute path using the directory as a base, then check if the result
     * still starts with the full directory path.
     *
     * This ASSUMES that pThis->pszPath is an absolute path.
     */
    int rc = RTPathAbsEx(pThis->pszPath, pszRelPath, RTPATH_STR_F_STYLE_HOST, pszPathDst, &cbPathDst);
    if (RT_SUCCESS(rc))
    {
        if (RTPathStartsWith(pszPathDst, pThis->pszPath))
            return VINF_SUCCESS;
        return VERR_PATH_NOT_FOUND;
    }
    return rc;
}


/*
 *
 *
 * RTFile stuff.
 * RTFile stuff.
 * RTFile stuff.
 *
 *
 */




RTDECL(int)  RTDirRelFileOpen(RTDIR hDir, const char *pszRelFilename, uint64_t fOpen, PRTFILE phFile)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelFilename);
    if (RT_SUCCESS(rc))
        rc = RTFileOpen(phFile, szPath, fOpen);
    return rc;
}



/*
 *
 *
 * RTDir stuff.
 * RTDir stuff.
 * RTDir stuff.
 *
 *
 */



RTDECL(int) RTDirRelDirOpen(RTDIR hDir, const char *pszDir, RTDIR *phDir)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszDir);
    if (RT_SUCCESS(rc))
        rc = RTDirOpen(phDir, szPath);
    return rc;

}


RTDECL(int) RTDirRelDirOpenFiltered(RTDIR hDir, const char *pszDirAndFilter, RTDIRFILTER enmFilter,
                                    uint32_t fFlags, RTDIR *phDir)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszDirAndFilter);
    if (RT_SUCCESS(rc))
        rc = RTDirOpenFiltered(phDir, szPath, enmFilter, fFlags);
    return rc;
}


RTDECL(int) RTDirRelDirCreate(RTDIR hDir, const char *pszRelPath, RTFMODE fMode, uint32_t fCreate, RTDIR *phSubDir)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
    {
        rc = RTDirCreate(szPath, fMode, fCreate);
        if (RT_SUCCESS(rc) && phSubDir)
            rc = RTDirOpen(phSubDir, szPath);
    }
    return rc;
}


RTDECL(int) RTDirRelDirRemove(RTDIR hDir, const char *pszRelPath)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
        rc = RTDirRemove(szPath);
    return rc;
}


/*
 *
 * RTPath stuff.
 * RTPath stuff.
 * RTPath stuff.
 *
 *
 */


RTDECL(int) RTDirRelPathQueryInfo(RTDIR hDir, const char *pszRelPath, PRTFSOBJINFO pObjInfo,
                                  RTFSOBJATTRADD enmAddAttr, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
        rc = RTPathQueryInfoEx(szPath, pObjInfo, enmAddAttr, fFlags);
    return rc;
}


RTDECL(int) RTDirRelPathSetMode(RTDIR hDir, const char *pszRelPath, RTFMODE fMode, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_FLAGS);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
    {
#ifndef RT_OS_WINDOWS
        rc = RTPathSetMode(szPath, fMode); /** @todo fFlags is currently ignored. */
#else
        rc = VERR_NOT_IMPLEMENTED; /** @todo implement RTPathSetMode on windows. */
        RT_NOREF(fMode);
#endif
    }
    return rc;
}


RTDECL(int) RTDirRelPathSetTimes(RTDIR hDir, const char *pszRelPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
        rc = RTPathSetTimesEx(szPath, pAccessTime, pModificationTime, pChangeTime, pBirthTime, fFlags);
    return rc;
}


RTDECL(int) RTDirRelPathSetOwner(RTDIR hDir, const char *pszRelPath, uint32_t uid, uint32_t gid, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
    {
#ifndef RT_OS_WINDOWS
        rc = RTPathSetOwnerEx(szPath, uid, gid, fFlags);
#else
        rc = VERR_NOT_IMPLEMENTED;
        RT_NOREF(uid, gid, fFlags);
#endif
    }
    return rc;
}


RTDECL(int) RTDirRelPathRename(RTDIR hDirSrc, const char *pszSrc, RTDIR hDirDst, const char *pszDst, unsigned fRename)
{
    PRTDIRINTERNAL pThis = hDirSrc;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    PRTDIRINTERNAL pThat = hDirDst;
    if (pThat != pThis)
    {
        AssertPtrReturn(pThat, VERR_INVALID_HANDLE);
        AssertReturn(pThat->u32Magic != RTDIR_MAGIC, VERR_INVALID_HANDLE);
    }

    char szSrcPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szSrcPath, sizeof(szSrcPath), pszSrc);
    if (RT_SUCCESS(rc))
    {
        char szDstPath[RTPATH_MAX];
        rc = rtDirRelBuildFullPath(pThis, szDstPath, sizeof(szDstPath), pszDst);
        if (RT_SUCCESS(rc))
            rc = RTPathRename(szSrcPath, szDstPath, fRename);
    }
    return rc;
}


RTDECL(int) RTDirRelPathUnlink(RTDIR hDir, const char *pszRelPath, uint32_t fUnlink)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
        rc = RTPathUnlink(szPath, fUnlink);
    return rc;
}


/*
 *
 * RTSymlink stuff.
 * RTSymlink stuff.
 * RTSymlink stuff.
 *
 *
 */


RTDECL(int) RTDirRelSymlinkCreate(RTDIR hDir, const char *pszSymlink, const char *pszTarget,
                                  RTSYMLINKTYPE enmType, uint32_t fCreate)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszSymlink);
    if (RT_SUCCESS(rc))
        rc = RTSymlinkCreate(szPath, pszTarget, enmType, fCreate);
    return rc;
}


RTDECL(int) RTDirRelSymlinkRead(RTDIR hDir, const char *pszSymlink, char *pszTarget, size_t cbTarget, uint32_t fRead)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszSymlink);
    if (RT_SUCCESS(rc))
        rc = RTSymlinkRead(szPath, pszTarget, cbTarget, fRead);
    return rc;
}

