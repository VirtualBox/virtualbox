/* $Id: pathint-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Windows, Internal Path stuff.
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
#define LOG_GROUP RTLOGGROUP_FS
#include <iprt/path.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include <iprt/nt/nt-and-windows.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max number of non-null characters we pass to an Win32 API.
 * You would think that MAX_PATH gives this length, however CreateDirectoryW was
 * found to fail on Windows 10 (1803++) if given a perfectly formed path argument
 * of 248 or more characters.  Same when going thru UNC.
 *
 * So, to be conservative, we put the max number of characters in a non-\\?\
 * path to 243, not counting the terminator.
 */
#define ACTUAL_MAX_PATH     243


DECL_NO_INLINE(static, bool) rtPathWinTryConvertToAbs(PRTUTF16 *ppwszPath)
{
    RTUTF16 wszFullPath[MAX_PATH + 1];
    DWORD cwcFull = GetFullPathNameW(*ppwszPath, MAX_PATH + 1, wszFullPath, NULL);
    if (cwcFull <= ACTUAL_MAX_PATH)
    {
        RTUtf16Free(*ppwszPath);
        PRTUTF16 const pwszCopy = RTUtf16Dup(wszFullPath);
        *ppwszPath = pwszCopy;
        if (pwszCopy)
            return true;
    }
    return false;
}


RTDECL(int)  RTPathWinFromUtf8(PRTUTF16 *ppwszPath, const char *pszPath, uint32_t fFlags)
{
    Assert(fFlags == 0);
    RT_NOREF(fFlags);

    /*
     * Do a straight conversion first.
     */
    *ppwszPath = NULL;
    size_t cwcResult = 0;
    int rc = RTStrToUtf16Ex(pszPath, RTSTR_MAX, ppwszPath, 0, &cwcResult);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check the resulting length.  This is straight forward for absolute
         * paths, but gets complicated for relative ones.
         */
        if (cwcResult <= ACTUAL_MAX_PATH)
        {
            if (RT_C_IS_ALPHA(pszPath[0]) && pszPath[1] == ':')
            {
                if (RTPATH_IS_SLASH(pszPath[2]))
                    return VINF_SUCCESS;

                /* Drive relative path.  Found no simple way of getting the current
                   path of a drive, so we try convert it to an absolute path and see
                   how that works out.  It is what the API we're calling will have to
                   do anyway, so this should perform just as well. */
                if (rtPathWinTryConvertToAbs(ppwszPath))
                    return VINF_SUCCESS;
            }
            else if (RTPATH_IS_SLASH(pszPath[0]))
            {
                if (   RTPATH_IS_SLASH(pszPath[1])
                    && !RTPATH_IS_SLASH(pszPath[2])
                    && pszPath[2] != '\0')
                {
                    /* Passthru prefix '\\?\' is fine. */
                    if (   pszPath[2] == '?'
                        && !RTPATH_IS_SLASH(pszPath[3]))
                        return VINF_SUCCESS;

                    /* UNC requires a longer prefix ('\??\UNC\' instead of '\??\'), so
                       subtract 3 chars from the max limit to be on the safe side.  */
                    if (cwcResult <= ACTUAL_MAX_PATH - 3)
                        return VINF_SUCCESS;
                }
                else
                {
                    /* Drive relative. Win32 will prepend a two letter drive specification. */
                    if (cwcResult <= ACTUAL_MAX_PATH - 2)
                        return VINF_SUCCESS;
                }
            }
            else
            {
                /* Relative to CWD.  We can use the API to get it's current length.
                   Any race conditions here is entirely  the caller's problem. */
                size_t cwcCwd = GetCurrentDirectoryW(0, NULL);
                if (cwcCwd + cwcResult <= ACTUAL_MAX_PATH - 1)
                    return VINF_SUCCESS;
            }
        }
        /*
         * We're good if the caller already supplied the passthru/length prefix: '\\?\'
         */
        else if (   pszPath[1] == '?'
                 && RTPATH_IS_SLASH(pszPath[3])
                 && RTPATH_IS_SLASH(pszPath[1])
                 && RTPATH_IS_SLASH(pszPath[0]))
                 return VINF_SUCCESS;

        /*
         * Long path requiring \\?\ prefixing.
         *
         * We piggy back on the NT conversion here and ASSUME RTUtf16Free is the right
         * way to free the result.
         */
        RTUtf16Free(*ppwszPath);
        *ppwszPath = NULL;

        struct _UNICODE_STRING  NtName   = { 0, 0, NULL };
        HANDLE                  hRootDir = NULL;
        rc = RTNtPathFromWinUtf8(&NtName, &hRootDir, pszPath);
        if (RT_SUCCESS(rc))
        {
            /* No root dir handle. */
            if (hRootDir == NULL)
            {
                /* Convert the NT '\??\' prefix to a win32 passthru prefix '\\?\' */
                if (   NtName.Buffer[0] == '\\'
                    && NtName.Buffer[1] == '?'
                    && NtName.Buffer[2] == '?'
                    && NtName.Buffer[3] == '\\')
                {
                    NtName.Buffer[1] = '\\';

                    /* Zero termination paranoia. */
                    if (NtName.Buffer[NtName.Length / sizeof(RTUTF16)] == '\0')
                    {
                        *ppwszPath = NtName.Buffer;
                        return VINF_SUCCESS;
                    }
                    AssertMsgFailed(("Length=%u %.*ls\n", NtName.Length, NtName.Length / sizeof(RTUTF16), NtName.Buffer));
                }
                else
                    AssertMsgFailed(("%ls\n", NtName.Buffer));
            }
            else
                AssertMsgFailed(("%s\n", pszPath));
            RTNtPathFree(&NtName, &hRootDir);
        }
    }
    return rc;
}


RTDECL(void) RTPathWinFree(PRTUTF16 pwszPath)
{
    RTUtf16Free(pwszPath);
}

