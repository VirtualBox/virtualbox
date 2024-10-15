/* $Id: utf8-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UTF8 helpers.
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
#define LOG_GROUP RTLOGGROUP_UTF8
#include <iprt/win/windows.h>

#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/utf16.h>



RTR3DECL(int)  RTStrUtf8ToCurrentCPTag(char **ppszString, const char *pszString, const char *pszTag)
{
    return RTStrUtf8ToCurrentCPExTag(ppszString, pszString, RTSTR_MAX, pszTag);
}


RTR3DECL(int)  RTStrUtf8ToCurrentCPExTag(char **ppszString, const char *pszString, size_t cchString, const char *pszTag)
{
    Assert(ppszString);
    Assert(pszString);
    *ppszString = NULL;

    /*
     * If the ANSI codepage (CP_ACP) is UTF-8, no translation is needed.
     * Same goes for empty strings.
     */
    if (   cchString == 0
        || *pszString == '\0')
        return RTStrDupNExTag(ppszString, pszString, 0, pszTag);
    if (GetACP() == CP_UTF8)
    {
        int rc = RTStrValidateEncodingEx(pszString, cchString, 0);
        AssertRCReturn(rc, rc);
        return RTStrDupNExTag(ppszString, pszString, cchString, pszTag);
    }

    /*
     * Convert to wide char first.
     */
    PRTUTF16 pwszString = NULL;
    int rc = RTStrToUtf16Ex(pszString, cchString, &pwszString, 0, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * First calc result string length.
     */
    int cbResult = WideCharToMultiByte(CP_ACP, 0, pwszString, -1, NULL, 0, NULL, NULL);
    if (cbResult > 0)
    {
        /*
         * Alloc space for result buffer.
         */
        LPSTR lpString = (LPSTR)RTMemTmpAllocTag(cbResult, pszTag);
        if (lpString)
        {
            /*
             * Do the translation.
             */
            if (WideCharToMultiByte(CP_ACP, 0, pwszString, -1, lpString, cbResult, NULL, NULL) > 0)
            {
                /* ok */
                *ppszString = lpString;
                RTMemTmpFree(pwszString);
                return VINF_SUCCESS;
            }

            /* translation error */
            int iLastErr = GetLastError();
            AssertMsgFailed(("Unicode to ACP translation failed. lasterr=%d\n", iLastErr));
            rc = RTErrConvertFromWin32(iLastErr);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
        RTMemTmpFree(lpString);
    }
    else
    {
        /* translation error */
        int iLastErr = GetLastError();
        AssertMsgFailed(("Unicode to ACP translation failed lasterr=%d\n", iLastErr));
        rc = RTErrConvertFromWin32(iLastErr);
    }
    RTMemTmpFree(pwszString);
    return rc;
}

static int rtStrCPToUtf8Tag(char **ppszString, const char *pszString, uint32_t uCodePage, const char *pszTag)
{
    Assert(ppszString);
    Assert(pszString);
    *ppszString = NULL;

    /*
     * If the ANSI codepage (CP_ACP) is UTF-8, no translation is needed.
     * Same goes for empty strings.
     */
    if (*pszString == '\0')
        return RTStrDupExTag(ppszString, pszString, pszTag);
    if (GetACP() == CP_UTF8)
    {
        int rc = RTStrValidateEncoding(pszString);
        AssertRCReturn(rc, rc);
        return RTStrDupExTag(ppszString, pszString, pszTag);
    }

    /** @todo is there a quicker way? Currently: ACP -> UTF-16 -> UTF-8 */

    /*
     * First calc result string length.
     */
    int rc;
    int cwc = MultiByteToWideChar((UINT)uCodePage, 0, pszString, -1, NULL, 0);
    if (cwc > 0)
    {
        /*
         * Alloc space for result buffer.
         */
        PRTUTF16 pwszString = (PRTUTF16)RTMemTmpAlloc(cwc * sizeof(RTUTF16));
        if (pwszString)
        {
            /*
             * Do the translation.
             */
            if (MultiByteToWideChar((UINT)uCodePage, 0, pszString, -1, pwszString, cwc) > 0)
            {
                /*
                 * Now we got UTF-16, convert it to UTF-8
                 */
                rc = RTUtf16ToUtf8(pwszString, ppszString);
                RTMemTmpFree(pwszString);
                return rc;
            }
            RTMemTmpFree(pwszString);
            /* translation error */
            int iLastErr = GetLastError();
            AssertMsgFailed(("ACP to Unicode translation failed. lasterr=%d\n", iLastErr));
            rc = RTErrConvertFromWin32(iLastErr);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    else
    {
        /* translation error */
        int iLastErr = GetLastError();
        AssertMsgFailed(("Unicode to ACP translation failed lasterr=%d\n", iLastErr));
        rc = RTErrConvertFromWin32(iLastErr);
    }
    return rc;
}


RTR3DECL(int)  RTStrCurrentCPToUtf8Tag(char **ppszString, const char *pszString, const char *pszTag)
{
    return rtStrCPToUtf8Tag(ppszString, pszString, CP_ACP, pszTag);
}


RTR3DECL(int)  RTStrConsoleCPToUtf8Tag(char **ppszString, const char *pszString, const char *pszTag)
{
    return rtStrCPToUtf8Tag(ppszString, pszString, GetConsoleCP(), pszTag);
}
