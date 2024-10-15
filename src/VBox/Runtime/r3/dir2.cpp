/* $Id: dir2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Directory Manipulation, Part 2.
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

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include "internal/path.h"


/**
 * Recursion worker for RTDirRemoveRecursive.
 *
 * @returns IPRT status code.
 * @param   pszBuf              The path buffer.  Contains the abs path to the
 *                              directory to recurse into.  Trailing slash.
 * @param   cchDir              The length of the directory we're recursing into,
 *                              including the trailing slash.
 * @param   cbBuf               Size of the buffer @a pszBuf points to.
 * @param   pDirEntry           The dir entry buffer.  (Shared to save stack.)
 * @param   pObjInfo            The object info buffer.  (ditto)
 * @param   fFlags              RTDIRRMREC_F_XXX.
 */
static int rtDirRemoveRecursiveSub(char *pszBuf, size_t cchDir, size_t cbBuf, PRTDIRENTRY pDirEntry, PRTFSOBJINFO pObjInfo,
                                   uint32_t fFlags)
{
    AssertReturn(RTPATH_IS_SLASH(pszBuf[cchDir - 1]), VERR_INTERNAL_ERROR_4);

    /*
     * Enumerate the directory content and dispose of it.
     */
    RTDIR hDir;
    int rc = RTDirOpenFiltered(&hDir, pszBuf, RTDIRFILTER_NONE, fFlags & RTDIRRMREC_F_NO_ABS_PATH ? RTDIR_F_NO_ABS_PATH : 0);
    if (RT_FAILURE(rc))
        return rc;
    while (RT_SUCCESS(rc = RTDirRead(hDir, pDirEntry, NULL)))
    {
        if (!RTDirEntryIsStdDotLink(pDirEntry))
        {
            /* Construct the full name of the entry. */
            if (cchDir + pDirEntry->cbName + 1 /* dir slash */ >= cbBuf)
            {
                rc = VERR_FILENAME_TOO_LONG;
                break;
            }
            memcpy(&pszBuf[cchDir], pDirEntry->szName, pDirEntry->cbName + 1);

            /* Deal with the unknown type. */
            if (pDirEntry->enmType == RTDIRENTRYTYPE_UNKNOWN)
            {
                rc = RTPathQueryInfoEx(pszBuf, pObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                if (RT_SUCCESS(rc) && RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
                    pDirEntry->enmType = RTDIRENTRYTYPE_DIRECTORY;
                else if (RT_SUCCESS(rc) && RTFS_IS_FILE(pObjInfo->Attr.fMode))
                    pDirEntry->enmType = RTDIRENTRYTYPE_FILE;
                else if (RT_SUCCESS(rc) && RTFS_IS_SYMLINK(pObjInfo->Attr.fMode))
                    pDirEntry->enmType = RTDIRENTRYTYPE_SYMLINK;
            }

            /* Try the delete the fs object. */
            switch (pDirEntry->enmType)
            {
                case RTDIRENTRYTYPE_FILE:
                    rc = RTFileDelete(pszBuf);
                    break;

                case RTDIRENTRYTYPE_DIRECTORY:
                {
                    size_t cchSubDir = cchDir + pDirEntry->cbName;
                    pszBuf[cchSubDir++] = '/';
                    pszBuf[cchSubDir]   = '\0';
                    rc = rtDirRemoveRecursiveSub(pszBuf, cchSubDir, cbBuf, pDirEntry, pObjInfo, fFlags);
                    if (RT_SUCCESS(rc))
                    {
                        pszBuf[cchSubDir] = '\0';
                        rc = RTDirRemove(pszBuf);
                    }
                    break;
                }

                //case RTDIRENTRYTYPE_SYMLINK:
                //    rc = RTSymlinkDelete(pszBuf, 0);
                //    break;

                default:
                    /** @todo not implemented yet. */
                    rc = VINF_SUCCESS;
                    break;
            }
            if (RT_FAILURE(rc))
               break;
        }
    }
    if (rc == VERR_NO_MORE_FILES)
        rc = VINF_SUCCESS;
    RTDirClose(hDir);
    return rc;
}


RTDECL(int) RTDirRemoveRecursive(const char *pszPath, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~RTDIRRMREC_F_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Allocate path buffer.
     */
    char  *pszAbsPath;
    size_t cbAbsPathBuf   = RTPATH_BIG_MAX;
    char  *pszAbsPathFree = pszAbsPath = (char *)RTMemTmpAlloc(cbAbsPathBuf);
    if (!pszAbsPath)
    {
        cbAbsPathBuf = RTPATH_MAX;
        pszAbsPath   = (char *)alloca(RTPATH_MAX);
    }

    /*
     * Get an absolute path because this is easier to work with and
     * eliminates any races with changing CWD.
     */
    int rc;
    if (!(fFlags & RTDIRRMREC_F_NO_ABS_PATH))
        rc = RTPathAbs(pszPath, pszAbsPath, cbAbsPathBuf);
    else if (*pszPath != '\0')
        rc = RTStrCopy(pszAbsPath, cbAbsPathBuf, pszPath);
    else
        rc = VERR_PATH_ZERO_LENGTH;
    if (RT_SUCCESS(rc))
    {
        /*
         * This API is not permitted applied to the root of anything.
         */
        union
        {
            RTPATHPARSED    Parsed;
            uint8_t         abParsed[RTPATHPARSED_MIN_SIZE];
        } uBuf;
        RTPathParse(pszPath, &uBuf.Parsed, sizeof(uBuf), RTPATH_STR_F_STYLE_HOST);
        if (   uBuf.Parsed.cComps <= 1
            && (uBuf.Parsed.fProps & RTPATH_PROP_ROOT_SLASH))
            rc = VERR_ACCESS_DENIED;
        else
        {
            /*
             * Because of the above restriction, we never have to deal with the root
             * slash problem and can safely strip any trailing slashes and add a
             * definite one.
             */
            RTPathStripTrailingSlash(pszAbsPath);
            size_t cchAbsPath = strlen(pszAbsPath);
            if (cchAbsPath + 1 < cbAbsPathBuf)
            {
                pszAbsPath[cchAbsPath++] = RTPATH_SLASH;
                pszAbsPath[cchAbsPath]   = '\0';

                /*
                 * Check if it exists so we can return quietly if it doesn't.
                 */
                RTFSOBJINFO SharedObjInfoBuf;
                rc = RTPathQueryInfoEx(pszAbsPath, &SharedObjInfoBuf, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                if (   rc == VERR_PATH_NOT_FOUND
                    || rc == VERR_FILE_NOT_FOUND)
                    rc = VINF_SUCCESS;
                else if (   RT_SUCCESS(rc)
                         && RTFS_IS_DIRECTORY(SharedObjInfoBuf.Attr.fMode))
                {
                    /*
                     * We're all set for the recursion now, so get going.
                     */
                    RTDIRENTRY SharedDirEntryBuf;
                    rc = rtDirRemoveRecursiveSub(pszAbsPath, cchAbsPath, cbAbsPathBuf,
                                                 &SharedDirEntryBuf, &SharedObjInfoBuf, fFlags);

                    /*
                     * Remove the specified directory if desired and removing the content was successful.
                     */
                    if (   RT_SUCCESS(rc)
                        && !(fFlags & RTDIRRMREC_F_CONTENT_ONLY))
                    {
                        pszAbsPath[cchAbsPath] = 0;
                        rc = RTDirRemove(pszAbsPath);
                    }
                }
                else if (RT_SUCCESS(rc))
                    rc = VERR_NOT_A_DIRECTORY;

            }
            else
                rc = VERR_FILENAME_TOO_LONG;
        }
    }
    if (pszAbsPathFree)
        RTMemTmpFree(pszAbsPathFree);
    return rc;
}

