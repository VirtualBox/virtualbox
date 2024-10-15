/* $Id: DnDPath.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD - Path handling.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/GuestHost/DragAndDrop.h>

#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/uri.h>


/**
 * Sanitizes the file name portion of a path so that unsupported characters will be replaced by an underscore ("_").
 *
 * @return  IPRT status code.
 * @param   pszFileName         File name to sanitize.
 * @param   cbFileName          Size (in bytes) of file name to sanitize.
 */
int DnDPathSanitizeFileName(char *pszFileName, size_t cbFileName)
{
    if (!pszFileName) /* No path given? Bail out early. */
        return VINF_SUCCESS;

    AssertReturn(cbFileName, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    RT_NOREF1(cbFileName);
    /* Replace out characters not allowed on Windows platforms, put in by RTTimeSpecToString(). */
    /** @todo Use something like RTPathSanitize() if available later some time. */
    static const RTUNICP s_uszValidRangePairs[] =
    {
        ' ', ' ',
        '(', ')',
        '-', '.',
        '0', '9',
        'A', 'Z',
        'a', 'z',
        '_', '_',
        0xa0, 0xd7af,
        '\0'
    };

    ssize_t cReplaced = RTStrPurgeComplementSet(pszFileName, s_uszValidRangePairs, '_' /* chReplacement */);
    if (cReplaced < 0)
        rc = VERR_INVALID_UTF8_ENCODING;
#else
    RT_NOREF2(pszFileName, cbFileName);
#endif
    return rc;
}

/**
 * Validates whether a given path matches our set of rules or not.
 *
 * Rules:
 * - An empty path is allowed.
 * - Dot components ("." or "..") are forbidden.
 * - If \a fMustExist is \c true, the path either has to be a file or a directory and must exist.
 * - Symbolic links are forbidden.
 *
 * @returns VBox status code.
 * @param   pcszPath            Path to validate.
 * @param   fMustExist          Whether the path to validate also must exist.
 * @sa      shClTransferValidatePath().
 */
int DnDPathValidate(const char *pcszPath, bool fMustExist)
{
    if (!pcszPath)
        return VERR_INVALID_POINTER;

    int rc = VINF_SUCCESS;

    if (   RT_SUCCESS(rc)
        && !RTStrIsValidEncoding(pcszPath))
    {
        rc = VERR_INVALID_UTF8_ENCODING;
    }

    if (   RT_SUCCESS(rc)
        && RTStrStr(pcszPath, ".."))
    {
        rc = VERR_INVALID_PARAMETER;
    }

    if (   RT_SUCCESS(rc)
        && fMustExist)
    {
        RTFSOBJINFO objInfo;
        rc = RTPathQueryInfo(pcszPath, &objInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
            if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
            {
                if (!RTDirExists(pcszPath)) /* Path must exist. */
                    rc = VERR_PATH_NOT_FOUND;
            }
            else if (RTFS_IS_FILE(objInfo.Attr.fMode))
            {
                if (!RTFileExists(pcszPath)) /* File must exist. */
                    rc = VERR_FILE_NOT_FOUND;
            }
            else /* Everything else (e.g. symbolic links) are not supported. */
                rc = VERR_NOT_SUPPORTED;
        }
    }

    return rc;
}

/**
 * Converts a DnD path.
 *
 * @returns VBox status code.
 * @param   pszPath             Path to convert.
 * @param   cbPath              Size (in bytes) of path to convert.
 * @param   fFlags              Conversion flags of type DNDPATHCONVERT_FLAGS_.
 */
int DnDPathConvert(char *pszPath, size_t cbPath, DNDPATHCONVERTFLAGS fFlags)
{
    RT_NOREF(cbPath);
    AssertReturn(!(fFlags & ~DNDPATHCONVERT_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);

    if (fFlags & DNDPATHCONVERT_FLAGS_TO_DOS)
        RTPathChangeToDosSlashes(pszPath, true /* fForce */);
    else
        RTPathChangeToUnixSlashes(pszPath, true /* fForce */);

    return VINF_SUCCESS;
}

/**
 * Rebases an absolute path from an old path base to a new path base.
 * Note: Does *not* do any path conversion.
 *
 * @return  IPRT status code.
 * @param   pcszPath            Path to rebase.
 * @param   strBaseOld          Old base path to rebase from. Optional and can be NULL.
 * @param   strBaseNew          New base path to rebase to.
 * @param   ppszPath            Where to store the allocated rebased path on success. Needs to be free'd with RTStrFree().
 */
int DnDPathRebase(const char *pcszPath, const char *pcszBaseOld, const char *pcszBaseNew,
                  char **ppszPath)
{
    AssertPtrReturn(pcszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(pcszBaseOld, VERR_INVALID_POINTER);
    AssertPtrReturn(pcszBaseNew, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszPath, VERR_INVALID_POINTER);

    char szPath[RTPATH_MAX];

    /* Do we need to see if the given path is part of the old base? */
    size_t idxBase;
    if (RTPathStartsWith(pcszPath, pcszBaseOld))
        idxBase = strlen(pcszBaseOld);
    else
        idxBase = 0;

    int rc = RTStrCopy(szPath, sizeof(szPath), pcszBaseNew);
    if (RT_SUCCESS(rc))
    {
        rc = RTPathAppend(szPath, sizeof(szPath), &pcszPath[idxBase]);
        if (RT_SUCCESS(rc))
            rc = DnDPathValidate(szPath, false /* fMustExist */);
    }

    if (RT_SUCCESS(rc))
    {
        char *pszPath = RTStrDup(szPath);
        if (pszPath)
            *ppszPath = pszPath;
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}

