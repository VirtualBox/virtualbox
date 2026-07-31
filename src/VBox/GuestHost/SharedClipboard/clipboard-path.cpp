/* $Id: clipboard-path.cpp 114830 2026-07-31 10:02:47Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard - Path handling.
 */

/*
 * Copyright (C) 2019-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/GuestHost/SharedClipboard-transfers.h>

#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/fs.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/symlink.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif


/**
 * Sanitizes the file name component so that unsupported characters
 * will be replaced by an underscore ("_").
 *
 * @return  IPRT status code.
 * @param   pszPath             Path to sanitize.
 * @param   cbPath              Size (in bytes) of path to sanitize.
 */
int ShClPathSanitizeFilename(char *pszPath, size_t cbPath)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    RT_NOREF1(cbPath);
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
    ssize_t cReplaced = RTStrPurgeComplementSet(pszPath, s_uszValidRangePairs, '_' /* chReplacement */);
    if (cReplaced < 0)
        rc = VERR_INVALID_UTF8_ENCODING;
#else
    RT_NOREF2(pszPath, cbPath);
#endif
    return rc;
}

/**
 * Sanitizes a given path regarding invalid / unhandled characters and rejects
 * dot path components.
 *
 * @returns VBox status code.
 * @param   pszPath             Path to sanitize. UTF-8.
 * @param   cbPath              Size (in bytes) of the path to sanitize.
 */
int ShClPathSanitize(char *pszPath, size_t cbPath)
{
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath, VERR_INVALID_PARAMETER);

    size_t const cchPath = RTStrNLen(pszPath, cbPath);
    if (cchPath >= cbPath)
        return VERR_BUFFER_OVERFLOW;

    int rc = RTStrValidateEncodingEx(pszPath, cchPath + 1, RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
    if (RT_FAILURE(rc))
        return rc;

    if (!cchPath)
        return VINF_SUCCESS;

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    RTPathChangeToUnixSlashes(pszPath, true /* fForce */);
#endif

    char *pszComponent = pszPath;
    for (size_t off = 0; off <= cchPath; off++)
    {
        if (   pszPath[off] == '/'
            || pszPath[off] == '\0')
        {
            char const chSaved = pszPath[off];
            pszPath[off] = '\0';

            if (*pszComponent)
            {
                if (   !RTStrCmp(pszComponent, ".")
                    || !RTStrCmp(pszComponent, ".."))
                {
                    pszPath[off] = chSaved;
                    return VERR_INVALID_PARAMETER;
                }

                rc = ShClPathSanitizeFilename(pszComponent, strlen(pszComponent) + 1);
                if (RT_FAILURE(rc))
                {
                    pszPath[off] = chSaved;
                    return rc;
                }
            }

            pszPath[off] = chSaved;
            pszComponent = &pszPath[off + 1];
        }
    }

    return VINF_SUCCESS;
}


static int shClPathQueryIsSymlink(const char *pszPath, PCRTFSOBJINFO pObjInfo, bool *pfIsSymlink)
{
    *pfIsSymlink = RTFS_IS_SYMLINK(pObjInfo->Attr.fMode);
#ifdef RT_OS_WINDOWS
    int rc = VINF_SUCCESS;
    if (!*pfIsSymlink)
    {
        PRTUTF16 pwszPath = NULL;
        rc = RTPathWinFromUtf8(&pwszPath, pszPath, 0 /* fFlags */);
        if (RT_SUCCESS(rc))
        {
            DWORD const fAttributes = GetFileAttributesW(pwszPath);
            if (fAttributes == INVALID_FILE_ATTRIBUTES)
                rc = RTErrConvertFromWin32(GetLastError());
            else
                *pfIsSymlink = RT_BOOL(fAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
            RTPathWinFree(pwszPath);
        }
    }
    return rc;
#elif defined(RT_OS_LINUX) || defined(RT_OS_DARWIN)
    /* Keep a positive result from either the portable query or the native backend. */
    if (RTSymlinkExists(pszPath))
        *pfIsSymlink = true;
    return VINF_SUCCESS;
#else
    RT_NOREF(pszPath);
    return VINF_SUCCESS;
#endif
}


/**
 * Checks whether a path is a symbolic link or equivalent.
 *
 * On Windows this includes all reparse points, such as directory junctions.
 *
 * @returns Whether @a pszPath is a symbolic link or equivalent.
 * @param   pszPath             Path to inspect.
 */
bool ShClPathIsSymlink(const char *pszPath)
{
    AssertPtrReturn(pszPath, false);

    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    bool fIsSymlink = false;
    if (RT_SUCCESS(rc))
        rc = shClPathQueryIsSymlink(pszPath, &ObjInfo, &fIsSymlink);
    return RT_SUCCESS(rc) && fIsSymlink;
}


static int shClPathValidateDirectory(const char *pszPath)
{
    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    bool fIsSymlink = false;
    if (RT_SUCCESS(rc))
        rc = shClPathQueryIsSymlink(pszPath, &ObjInfo, &fIsSymlink);
    if (RT_SUCCESS(rc))
    {
        if (fIsSymlink)
            rc = VERR_IS_A_SYMLINK;
        else if (!RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            rc = VERR_NOT_A_DIRECTORY;
    }
    return rc;
}


/**
 * Checks whether a path is a real directory rather than a symbolic link or
 * Windows reparse point.
 *
 * @returns Whether @a pszPath is a real directory.
 * @param   pszPath             Path to inspect.
 */
bool ShClPathIsDirectory(const char *pszPath)
{
    AssertPtrReturn(pszPath, false);
    return RT_SUCCESS(shClPathValidateDirectory(pszPath));
}


/**
 * Creates missing components of an absolute directory path.
 *
 * Every component is validated without accepting symbolic links or Windows
 * reparse points.  A concurrent creator is accepted only if it created the
 * expected real directory.
 *
 * @returns VBox status code.
 * @param   pszPath             Absolute directory path to create.
 */
int ShClDirectoryCreate(const char *pszPath)
{
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);

    int rc = RTStrValidateEncoding(pszPath);
    if (RT_FAILURE(rc))
        return rc;

    PRTPATHSPLIT pSplit = NULL;
    rc = RTPathSplitA(pszPath, &pSplit, RTPATH_STR_F_STYLE_HOST);
    if (RT_FAILURE(rc))
        return rc;
    if (   !(pSplit->fProps & RTPATH_PROP_ABSOLUTE)
        || !pSplit->cComps)
    {
        RTPathSplitFree(pSplit);
        return VERR_INVALID_NAME;
    }

    char szPath[RTPATH_MAX];
    rc = RTStrCopy(szPath, sizeof(szPath), pSplit->apszComps[0]);
    for (uint16_t i = 0; RT_SUCCESS(rc) && i < pSplit->cComps; ++i)
    {
        if (i)
            rc = RTPathAppend(szPath, sizeof(szPath), pSplit->apszComps[i]);
        if (RT_FAILURE(rc))
            break;

        rc = shClPathValidateDirectory(szPath);
        if (   RT_FAILURE(rc)
            && i > 0
            && (   rc == VERR_FILE_NOT_FOUND
                || rc == VERR_PATH_NOT_FOUND))
        {
            rc = RTDirCreate(szPath, 0700, 0 /* fCreate */);
            if (RT_FAILURE(rc))
            {
                int const rcCreate = rc;
                rc = shClPathValidateDirectory(szPath);
                if (   rc == VERR_FILE_NOT_FOUND
                    || rc == VERR_PATH_NOT_FOUND)
                    rc = rcCreate;
            }
            else
                rc = shClPathValidateDirectory(szPath);
        }
    }

    RTPathSplitFree(pSplit);
    return rc;
}


/**
 * Builds a host path below a transfer destination from a validated
 * transfer-relative path.
 *
 * @returns VBox status code.
 * @param   pszDestination      Absolute host destination directory.
 * @param   pszTransferPath     Transfer-relative path.
 * @param   pszHostPath         Where to return the host path.
 * @param   cbHostPath          Size of @a pszHostPath.
 */
int ShClHlpTransferPathToHostPath(const char *pszDestination, const char *pszTransferPath,
                                  char *pszHostPath, size_t cbHostPath)
{
    AssertPtrReturn(pszDestination, VERR_INVALID_POINTER);
    AssertPtrReturn(pszTransferPath, VERR_INVALID_POINTER);
    AssertPtrReturn(pszHostPath, VERR_INVALID_POINTER);
    AssertReturn(cbHostPath, VERR_INVALID_PARAMETER);
    if (   !*pszDestination
        || !*pszTransferPath
        || pszTransferPath[0] == '/'
        || pszTransferPath[0] == '\\'
        || strchr(pszTransferPath, '\\')
        || strchr(pszTransferPath, ':'))
        return VERR_INVALID_NAME;

    int rc = RTStrValidateEncoding(pszTransferPath);
    if (RT_FAILURE(rc))
        return rc;
    for (const char *pszCur = pszTransferPath; *pszCur; pszCur = RTStrNextCp(pszCur))
    {
        RTUNICP const uc = RTStrGetCp(pszCur);
        if (   uc < 0x20
            || (uc >= 0x7f && uc <= 0x9f))
            return VERR_INVALID_NAME;
    }
    rc = RTStrValidateEncoding(pszDestination);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTStrCopy(pszHostPath, cbHostPath, pszDestination);
    if (RT_FAILURE(rc))
        return rc;

    char *pszCopy = RTStrDup(pszTransferPath);
    if (!pszCopy)
        return VERR_NO_MEMORY;

    char *psz = pszCopy;
    while (RT_SUCCESS(rc) && *psz)
    {
        char *pszSlash = strchr(psz, '/');
        if (pszSlash)
            *pszSlash = '\0';
        if (   !*psz
            || !strcmp(psz, ".")
            || !strcmp(psz, "..")
            || (pszSlash && !pszSlash[1]))
        {
            rc = VERR_INVALID_NAME;
            break;
        }
        rc = RTPathAppend(pszHostPath, cbHostPath, psz);
        if (!pszSlash)
            break;
        psz = pszSlash + 1;
    }

    RTStrFree(pszCopy);
    return rc;
}
