/* $Id: ClipboardPath.cpp 115090 2026-08-19 13:39:39Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard - Path handling.
 */

/*
 * Copyright (C) 2019-2025 Oracle and/or its affiliates.
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

#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/string.h>


#ifdef RT_OS_WINDOWS
/**
 * Returns whether a filename uses a reserved Windows device name.
 *
 * Matching is case-insensitive and only considers the part before the first
 * dot, as reserved device names remain reserved when followed by an extension.
 *
 * @returns Whether @a pszName is a reserved Windows device filename.
 * @param   pszName             Zero-terminated UTF-8 filename to inspect.
 */
static bool shClPathIsWindowsReservedFilename(const char *pszName)
{
    size_t const cchBase = strcspn(pszName, ".");
    if (cchBase == 3)
    {
        if (   RTStrNICmp(pszName, "CON", 3) == 0
            || RTStrNICmp(pszName, "PRN", 3) == 0
            || RTStrNICmp(pszName, "AUX", 3) == 0
            || RTStrNICmp(pszName, "NUL", 3) == 0)
            return true;
    }
    else if (   cchBase >= 4
             && (   RTStrNICmp(pszName, "COM", 3) == 0
                 || RTStrNICmp(pszName, "LPT", 3) == 0))
    {
        size_t const cbSuffix = cchBase - 3;
        if (   (   cbSuffix == 1
                && pszName[3] >= '1'
                && pszName[3] <= '9')
            || (   cbSuffix == 2
                && (   !memcmp(&pszName[3], "\xc2\xb9", 2) /* SUPERSCRIPT ONE */
                    || !memcmp(&pszName[3], "\xc2\xb2", 2) /* SUPERSCRIPT TWO */
                    || !memcmp(&pszName[3], "\xc2\xb3", 2) /* SUPERSCRIPT THREE */)))
            return true;
    }

    return false;
}
#endif /* RT_OS_WINDOWS */


/**
 * Sanitizes the file name component so that unsupported characters and
 * reserved Windows device names will be replaced by an underscore ("_").
 *
 * @returns IPRT status code.
 * @param   pszPath             Path to sanitize.
 * @param   cbPath              Size (in bytes) of path to sanitize.
 */
int ShClPathSanitizeFilename(char *pszPath, size_t cbPath)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    RT_NOREF1(cbPath);
    /* Replace characters not allowed on Windows platforms, put in by RTTimeSpecToString(). */
    /** @todo Use something like RTPathSanitize() if available later some time. */
    static const RTUNICP s_aValidRangePairs[] =
    {
        ' ', '!',
        '#', ')',
        '+', '.',
        '0', '9',
        ';', ';',
        '=', '=',
        '@', '[',
        ']', '{',
        '}', '~',
        0x80, 0xd7ff,
        0xe000, 0x10ffff,
        '\0'
    };
    ssize_t cReplaced = RTStrPurgeComplementSet(pszPath, s_aValidRangePairs, '_' /* chReplacement */);
    if (cReplaced < 0)
        rc = VERR_INVALID_UTF8_ENCODING;
    else if (shClPathIsWindowsReservedFilename(pszPath))
        pszPath[0] = '_';
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

