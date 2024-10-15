/* $Id: pathint-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Native NT, Internal Path stuff.
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
#include "internal-r3-nt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static char const g_szPrefixUnc[]      = "\\??\\UNC\\";
static char const g_szPrefix[]         = "\\??\\";
static char const g_szPrefixNt3xUnc[]  = "\\DosDevices\\UNC\\";
static char const g_szPrefixNt3x[]     = "\\DosDevices\\";


/**
 * Handles the pass thru case for UTF-8 input.
 * Win32 path uses "\\?\" prefix which is converted to "\??\" NT prefix.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.
 * @param   phRootDir           Where to return the root handle, if applicable.
 * @param   pszPath             The UTF-8 path.
 */
static int rtNtPathFromWinUtf8PassThru(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir, const char *pszPath)
{
    PRTUTF16 pwszPath = NULL;
    size_t   cwcLen;
    int rc = RTStrToUtf16Ex(pszPath, RTSTR_MAX, &pwszPath, 0, &cwcLen);
    if (RT_SUCCESS(rc))
    {
        if (cwcLen < _32K - 1)
        {
            *phRootDir = NULL;
            if (RT_MAKE_U64(RTNtCurrentPeb()->OSMinorVersion, RTNtCurrentPeb()->OSMajorVersion) >= RT_MAKE_U64(0, 4))
            {
                pwszPath[0] = '\\';
                pwszPath[1] = '?';
                pwszPath[2] = '?';
                pwszPath[3] = '\\';

                pNtName->Buffer = pwszPath;
                pNtName->Length = (uint16_t)(cwcLen * sizeof(RTUTF16));
                pNtName->MaximumLength = pNtName->Length + sizeof(RTUTF16);
                return VINF_SUCCESS;
            }

            rc = RTUtf16Realloc(&pwszPath, cwcLen + sizeof(g_szPrefixNt3x));
            if (RT_SUCCESS(rc))
            {
                memmove(&pwszPath[sizeof(g_szPrefixNt3x) - 1], &pwszPath[4], (cwcLen - 4 + 1) * sizeof(RTUTF16));
                for (uint32_t i = 0; i < sizeof(g_szPrefixNt3x) - 1; i++)
                    pwszPath[i] = g_szPrefixNt3x[i];

                pNtName->Buffer = pwszPath;
                pNtName->Length = (uint16_t)((cwcLen - 4 + sizeof(g_szPrefixNt3x) - 1) * sizeof(RTUTF16));
                pNtName->MaximumLength = pNtName->Length + sizeof(RTUTF16);
                return VINF_SUCCESS;
            }
        }

        RTUtf16Free(pwszPath);
        rc = VERR_FILENAME_TOO_LONG;
    }
    return rc;
}


/**
 * Handles the pass thru case for UTF-16 input.
 * Win32 path uses "\\?\" prefix which is converted to "\??\" NT prefix.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.
 * @param   phRootDir           Stores NULL here, as we don't use it.
 * @param   pwszWinPath         The UTF-16 windows-style path.
 * @param   cwcWinPath          The length of the windows-style input path.
 */
static int rtNtPathFromWinUtf16PassThru(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir,
                                        PCRTUTF16 pwszWinPath, size_t cwcWinPath)
{
    /* Check length and allocate memory for it. */
    int rc;
    if (cwcWinPath < _32K - 1)
    {

        size_t const cwcExtraPrefix =    RT_MAKE_U64(RTNtCurrentPeb()->OSMinorVersion, RTNtCurrentPeb()->OSMajorVersion)
                                      >= RT_MAKE_U64(0, 4)
                                    ? 0 : sizeof(g_szPrefixNt3x) - 1 - 4;
        PRTUTF16 pwszNtPath = (PRTUTF16)RTUtf16Alloc((cwcExtraPrefix + cwcWinPath + 1) * sizeof(RTUTF16));
        if (pwszNtPath)
        {
            /* Intialize the path. */
            if (!cwcExtraPrefix)
            {
                pwszNtPath[0] = '\\';
                pwszNtPath[1] = '?';
                pwszNtPath[2] = '?';
                pwszNtPath[3] = '\\';
            }
            else
                for (uint32_t i = 0; i < sizeof(g_szPrefixNt3x) - 1; i++)
                    pwszNtPath[i] = g_szPrefixNt3x[i];
            memcpy(pwszNtPath + cwcExtraPrefix + 4, pwszWinPath + 4, (cwcWinPath - 4) * sizeof(RTUTF16));
            pwszNtPath[cwcExtraPrefix + cwcWinPath] = '\0';

            /* Initialize the return values. */
            pNtName->Buffer = pwszNtPath;
            pNtName->Length = (uint16_t)(cwcExtraPrefix + cwcWinPath * sizeof(RTUTF16));
            pNtName->MaximumLength = pNtName->Length + sizeof(RTUTF16);
            *phRootDir = NULL;

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    else
        rc = VERR_FILENAME_TOO_LONG;
    return rc;
}





/**
 * Converts the path to UTF-16 and sets all the return values.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.
 * @param   phRootDir           Where to return the root handle, if applicable.
 * @param   pszPath             The UTF-8 path.
 */
static int rtNtPathUtf8ToUniStr(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir, const char *pszPath)
{
    PRTUTF16 pwszPath = NULL;
    size_t   cwcLen;
    int rc = RTStrToUtf16Ex(pszPath, RTSTR_MAX, &pwszPath, 0, &cwcLen);
    if (RT_SUCCESS(rc))
    {
        if (cwcLen < _32K - 1)
        {
            pNtName->Buffer = pwszPath;
            pNtName->Length = (uint16_t)(cwcLen * sizeof(RTUTF16));
            pNtName->MaximumLength = pNtName->Length + sizeof(RTUTF16);
            *phRootDir = NULL;
            return VINF_SUCCESS;
        }

        RTUtf16Free(pwszPath);
        rc = VERR_FILENAME_TOO_LONG;
    }
    return rc;
}


/**
 * Converts a windows-style path to NT format and encoding.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.  Free using
 *                              rtTNtPathToNative.
 * @param   phRootDir           Where to return the root handle, if applicable.
 * @param   pszPath             The UTF-8 path.
 */
static int rtNtPathToNative(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir, const char *pszPath)
{
/** @todo This code sucks a bit performance wise, esp. calling
 *        generic RTPathAbs.  Too many buffers involved, I think. */

    /*
     * Very simple conversion of a win32-like path into an NT path.
     */
    const char *pszPrefix;
    size_t      cchPrefix;
    if (RT_MAKE_U64(RTNtCurrentPeb()->OSMinorVersion, RTNtCurrentPeb()->OSMajorVersion) >= RT_MAKE_U64(0, 4))
    {
        pszPrefix = g_szPrefix;
        cchPrefix = sizeof(g_szPrefix) - 1;
    }
    else
    {
        pszPrefix = g_szPrefixNt3x;
        cchPrefix = sizeof(g_szPrefixNt3x) - 1;
    }

    size_t      cchSkip   = 0;
    if (   RTPATH_IS_SLASH(pszPath[0])
        && RTPATH_IS_SLASH(pszPath[1])
        && !RTPATH_IS_SLASH(pszPath[2])
        && pszPath[2])
    {
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
        /*
         * Special trick: The path starts with RTPATH_NT_PASSTHRU_PREFIX, we will skip past the bang and pass it thru.
         */
        if (   pszPath[2] == ':'
            && pszPath[3] == 'i'
            && pszPath[4] == 'p'
            && pszPath[5] == 'r'
            && pszPath[6] == 't'
            && pszPath[7] == 'n'
            && pszPath[8] == 't'
            && pszPath[9] == ':'
            && RTPATH_IS_SLASH(pszPath[10]))
            return rtNtPathUtf8ToUniStr(pNtName, phRootDir, pszPath + 10);
#endif

        if (   pszPath[2] == '?'
            && RTPATH_IS_SLASH(pszPath[3]))
            return rtNtPathFromWinUtf8PassThru(pNtName, phRootDir, pszPath);

        if (   pszPath[2] == '.'
            && RTPATH_IS_SLASH(pszPath[3]))
        {
            /*
             * Device path.
             * Note! I suspect \\.\stuff\..\otherstuff may be handled differently by windows.
             */
            cchSkip   = 4;
        }
        else
        {
            /* UNC */
            if (RT_MAKE_U64(RTNtCurrentPeb()->OSMinorVersion, RTNtCurrentPeb()->OSMajorVersion) >= RT_MAKE_U64(0, 4))
            {
                pszPrefix = g_szPrefixUnc;
                cchPrefix = sizeof(g_szPrefixUnc) - 1;
            }
            else
            {
                pszPrefix = g_szPrefixNt3xUnc;
                cchPrefix = sizeof(g_szPrefixNt3xUnc) - 1;
            }
            cchSkip   = 2;
        }
    }

    /*
     * Straighten out all .. and uncessary . references and convert slashes.
     */
    char    szAbsPathBuf[RTPATH_MAX];
    size_t  cbAbsPath      = sizeof(szAbsPathBuf) - (cchPrefix - cchSkip);
    char   *pszAbsPath     = szAbsPathBuf;
    char   *pszAbsPathFree = NULL;
    int rc = RTPathAbsEx(NULL, pszPath, RTPATH_STR_F_STYLE_DOS, &pszAbsPath[cchPrefix - cchSkip], &cbAbsPath);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        unsigned cTries       = 8;
        size_t   cbAbsPathBuf = RTPATH_MAX;
        for (;;)
        {
            cbAbsPathBuf = RT_MAX(RT_ALIGN_Z((cchPrefix - cchSkip) + cbAbsPath + 32, 64), cbAbsPathBuf + 256);
            if (cTries == 1)
                cbAbsPathBuf = RT_MAX(cbAbsPathBuf, RTPATH_BIG_MAX * 2);
            pszAbsPathFree = pszAbsPath = (char *)RTMemTmpAlloc(cbAbsPathBuf);
            if (!pszAbsPath)
                return VERR_NO_TMP_MEMORY;

            cbAbsPath = cbAbsPathBuf - (cchPrefix - cchSkip);
            rc = RTPathAbsEx(NULL, pszPath, RTPATH_STR_F_STYLE_DOS, &pszAbsPath[cchPrefix - cchSkip], &cbAbsPath);
            if (RT_SUCCESS(rc))
                break;
            RTMemTmpFree(pszAbsPathFree);
            pszAbsPathFree = NULL;
            if (rc != VERR_BUFFER_OVERFLOW)
                return rc;
            if (--cTries == 0)
                return VERR_FILENAME_TOO_LONG;
        }
    }
    else
        return rc;

    /*
     * Add prefix and convert it to UTF16.
     */
    memcpy(pszAbsPath, pszPrefix, cchPrefix);
    rc = rtNtPathUtf8ToUniStr(pNtName, phRootDir, pszAbsPath);

    if (pszAbsPathFree)
        RTMemTmpFree(pszAbsPathFree);
    return rc;
}


/**
 * Converts a windows-style path to NT format and encoding.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.  Free using
 *                              RTNtPathToNative.
 * @param   phRootDir           Where to return the root handle, if applicable.
 * @param   pszPath             The UTF-8 path.
 */
RTDECL(int) RTNtPathFromWinUtf8(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir, const char *pszPath)
{
    return rtNtPathToNative(pNtName, phRootDir, pszPath);
}


/**
 * Converts a UTF-16 windows-style path to NT format.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.  Free using
 *                              RTNtPathFree.
 * @param   phRootDir           Where to return the root handle, if applicable.
 * @param   pwszWinPath         The UTF-16 windows-style path.
 * @param   cwcWinPath          The max length of the windows-style path in
 *                              RTUTF16 units.  Use RTSTR_MAX if unknown and @a
 *                              pwszWinPath is correctly terminated.
 */
RTDECL(int) RTNtPathFromWinUtf16Ex(struct _UNICODE_STRING *pNtName, HANDLE *phRootDir, PCRTUTF16 pwszWinPath, size_t cwcWinPath)
{
    /*
     * Validate the input, calculating the correct length.
     */
    if (cwcWinPath == 0 || *pwszWinPath == '\0')
        return VERR_INVALID_NAME;

    RTUtf16NLenEx(pwszWinPath, cwcWinPath, &cwcWinPath);
    int rc = RTUtf16ValidateEncodingEx(pwszWinPath, cwcWinPath, 0);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Very simple conversion of a win32-like path into an NT path.
     */
    const char *pszPrefix = g_szPrefix;
    size_t      cchPrefix = sizeof(g_szPrefix) - 1;
    size_t      cchSkip   = 0;

    if (   RTPATH_IS_SLASH(pwszWinPath[0])
        && cwcWinPath >= 3
        && RTPATH_IS_SLASH(pwszWinPath[1])
        && !RTPATH_IS_SLASH(pwszWinPath[2]) )
    {
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
        /*
         * Special trick: The path starts with RTPATH_NT_PASSTHRU_PREFIX, we will skip past the bang and pass it thru.
         */
        if (   cwcWinPath >= sizeof(RTPATH_NT_PASSTHRU_PREFIX) - 1U
            && pwszWinPath[2] == ':'
            && pwszWinPath[3] == 'i'
            && pwszWinPath[4] == 'p'
            && pwszWinPath[5] == 'r'
            && pwszWinPath[6] == 't'
            && pwszWinPath[7] == 'n'
            && pwszWinPath[8] == 't'
            && pwszWinPath[9] == ':'
            && RTPATH_IS_SLASH(pwszWinPath[10]) )
        {
            pwszWinPath += 10;
            cwcWinPath  -= 10;
            if (cwcWinPath < _32K - 1)
            {
                PRTUTF16 pwszNtPath = RTUtf16Alloc((cwcWinPath + 1) * sizeof(RTUTF16));
                if (pwszNtPath)
                {
                    memcpy(pwszNtPath, pwszWinPath, cwcWinPath * sizeof(RTUTF16));
                    pwszNtPath[cwcWinPath] = '\0';
                    pNtName->Buffer = pwszNtPath;
                    pNtName->Length = (uint16_t)(cwcWinPath * sizeof(RTUTF16));
                    pNtName->MaximumLength = pNtName->Length + sizeof(RTUTF16);
                    *phRootDir = NULL;
                    return VINF_SUCCESS;
                }
                rc = VERR_NO_UTF16_MEMORY;
            }
            else
                rc = VERR_FILENAME_TOO_LONG;
            return rc;
        }
#endif

        if (   pwszWinPath[2] == '?'
            && cwcWinPath >= 4
            && RTPATH_IS_SLASH(pwszWinPath[3]))
            return rtNtPathFromWinUtf16PassThru(pNtName, phRootDir, pwszWinPath, cwcWinPath);

        if (   pwszWinPath[2] == '.'
            && cwcWinPath >= 4
            && RTPATH_IS_SLASH(pwszWinPath[3]))
        {
            /*
             * Device path.
             * Note! I suspect \\.\stuff\..\otherstuff may be handled differently by windows.
             */
            cchSkip   = 4;
        }
        else
        {
            /* UNC */
            pszPrefix = g_szPrefixUnc;
            cchPrefix = sizeof(g_szPrefixUnc) - 1;
            cchSkip   = 2;
        }
    }

    /*
     * Straighten out all .. and unnecessary . references and convert slashes.
     */
    /* UTF-16 -> UTF-8 (relative path) */
    char   szRelPath[RTPATH_MAX];
    char  *pszRelPathFree = NULL;
    char  *pszRelPath = szRelPath;
    size_t cchRelPath;
    rc = RTUtf16ToUtf8Ex(pwszWinPath, cwcWinPath, &pszRelPath, sizeof(szRelPath), &cchRelPath);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        pszRelPath = NULL;
        rc = RTUtf16ToUtf8Ex(pwszWinPath, cwcWinPath, &pszRelPath, 0, &cchRelPath);
        if (RT_SUCCESS(rc))
            pszRelPathFree = pszRelPath;
    }
    if (RT_SUCCESS(rc))
    {
        /* Relative -> Absolute. */
        char   szAbsPathBuf[RTPATH_MAX];
        char  *pszAbsPathFree = NULL;
        char  *pszAbsPath     = szAbsPathBuf;
        size_t cbAbsPath      = sizeof(szAbsPathBuf) - (cchPrefix - cchSkip);
        rc = RTPathAbsEx(NULL, szRelPath, RTPATH_STR_F_STYLE_DOS, &pszAbsPath[cchPrefix - cchSkip], &cbAbsPath);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else if (rc == VERR_BUFFER_OVERFLOW)
        {
            unsigned cTries = 8;
            size_t   cbAbsPathBuf = RTPATH_MAX;
            for (;;)
            {
                cbAbsPathBuf = RT_MAX(RT_ALIGN_Z((cchPrefix - cchSkip) + cbAbsPath + 32, 64), cbAbsPathBuf + 256);
                if (cTries == 1)
                    cbAbsPathBuf = RT_MAX(cbAbsPathBuf, RTPATH_BIG_MAX * 2);
                pszAbsPathFree = pszAbsPath = (char *)RTMemTmpAlloc(cbAbsPathBuf);
                if (!pszAbsPath)
                {
                    rc = VERR_NO_TMP_MEMORY;
                    break;
                }

                cbAbsPath = cbAbsPathBuf - (cchPrefix - cchSkip);
                rc = RTPathAbsEx(NULL, szRelPath, RTPATH_STR_F_STYLE_DOS, &pszAbsPath[cchPrefix - cchSkip], &cbAbsPath);
                if (RT_SUCCESS(rc))
                    break;

                RTMemTmpFree(pszAbsPathFree);
                pszAbsPathFree = NULL;
                if (rc != VERR_BUFFER_OVERFLOW)
                    break;
                if (--cTries == 0)
                {
                    rc = VERR_FILENAME_TOO_LONG;
                    break;
                }
            }

        }
        if (pszRelPathFree)
            RTStrFree(pszRelPathFree);

        if (RT_SUCCESS(rc))
        {
            /*
             * Add prefix
             */
            memcpy(pszAbsPath, pszPrefix, cchPrefix);

            /*
             * Remove trailing '.' that is used to specify no extension in the Win32/DOS world.
             */
            size_t cchAbsPath = strlen(pszAbsPath);
            if (   cchAbsPath > 2
                && pszAbsPath[cchAbsPath - 1] == '.')
            {
                char const ch = pszAbsPath[cchAbsPath - 2];
                if (   ch != '/'
                    && ch != '\\'
                    && ch != ':'
                    && ch != '.')
                    pszAbsPath[--cchAbsPath] = '\0';
            }

            /*
             * Finally convert to UNICODE_STRING.
             */
            rc = rtNtPathUtf8ToUniStr(pNtName, phRootDir, pszAbsPath);

            if (pszAbsPathFree)
                RTMemTmpFree(pszAbsPathFree);
        }
    }
    return rc;
}


/**
 * Ensures that the NT string has sufficient storage to hold @a cwcMin RTUTF16
 * chars plus a terminator.
 *
 * The NT string must have been returned by RTNtPathFromWinUtf8 or
 * RTNtPathFromWinUtf16Ex.
 *
 * @returns IPRT status code.
 * @param   pNtName             The NT path string.
 * @param   cwcMin              The minimum number of RTUTF16 chars. Max 32767.
 * @sa      RTNtPathFree
 */
RTDECL(int) RTNtPathEnsureSpace(struct _UNICODE_STRING *pNtName, size_t cwcMin)
{
    if (pNtName->MaximumLength / sizeof(RTUTF16) > cwcMin)
        return VINF_SUCCESS;

    AssertReturn(cwcMin < _64K / sizeof(RTUTF16), VERR_OUT_OF_RANGE);

    size_t const cbMin = (cwcMin + 1) * sizeof(RTUTF16);
    int rc = RTUtf16Realloc(&pNtName->Buffer, cbMin);
    if (RT_SUCCESS(rc))
        pNtName->MaximumLength = (uint16_t)cbMin;
    return rc;
}


/**
 * Gets the NT path to the object represented by the given handle.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT path.  Free using
 *                              RTNtPathFree.
 * @param   hHandle             The handle.
 * @param   cwcExtra            How much extra space is needed.
 */
RTDECL(int) RTNtPathFromHandle(struct _UNICODE_STRING *pNtName, HANDLE hHandle, size_t cwcExtra)
{
    /*
     * Query the name into a buffer.
     */
    ULONG cbBuf = _2K;
    PUNICODE_STRING pUniStrBuf = (PUNICODE_STRING)RTMemTmpAllocZ(cbBuf);
    if (!pUniStrBuf)
        return VERR_NO_TMP_MEMORY;

    ULONG cbNameBuf = cbBuf;
    NTSTATUS rcNt = NtQueryObject(hHandle, ObjectNameInformation, pUniStrBuf, cbBuf, &cbNameBuf);
    while (   rcNt == STATUS_BUFFER_OVERFLOW
           || rcNt == STATUS_BUFFER_TOO_SMALL)
    {
        do
            cbBuf *= 2;
        while (cbBuf <= cbNameBuf);
        RTMemTmpFree(pUniStrBuf);
        pUniStrBuf = (PUNICODE_STRING)RTMemTmpAllocZ(cbBuf);
        if (!pUniStrBuf)
            return VERR_NO_TMP_MEMORY;

        cbNameBuf = cbBuf;
        rcNt = NtQueryObject(hHandle, ObjectNameInformation, pUniStrBuf, cbBuf, &cbNameBuf);
    }
    int rc;
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Copy the result into the return string.
         */
        size_t cbNeeded = cwcExtra * sizeof(RTUTF16) + pUniStrBuf->Length + sizeof(RTUTF16);
        if (cbNeeded < _64K)
        {
            pNtName->Length        = pUniStrBuf->Length;
            pNtName->MaximumLength = (uint16_t)cbNeeded;
            pNtName->Buffer        = RTUtf16Alloc(cbNeeded);
            if (pNtName->Buffer)
            {
                memcpy(pNtName->Buffer, pUniStrBuf->Buffer, pUniStrBuf->Length);
                pNtName->Buffer[pUniStrBuf->Length / sizeof(RTUTF16)] = '\0';
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_NO_UTF16_MEMORY;
        }
        else
            rc = VERR_FILENAME_TOO_LONG;
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);
    RTMemTmpFree(pUniStrBuf);
    return rc;
}

static int rtNtPathRelativeToAbs(struct _UNICODE_STRING *pNtName, HANDLE *phRootDir)
{
    int rc;
    if (pNtName->Length == 0)
    {
        RTUtf16Free(pNtName->Buffer);
        rc = RTNtPathFromHandle(pNtName, *phRootDir, pNtName->Length / sizeof(RTUTF16) + 2);
        if (RT_SUCCESS(rc))
        {
            *phRootDir = NULL;
            return VINF_SUCCESS;
        }
    }
    else
    {

        UNICODE_STRING RootDir;
        size_t const   cwcAppend = pNtName->Length / sizeof(RTUTF16);
        rc = RTNtPathFromHandle(&RootDir, *phRootDir, cwcAppend + 2);
        if (RT_SUCCESS(rc))
        {
            size_t cwcRoot = RootDir.Length / sizeof(RTUTF16);
            if (RootDir.Buffer[cwcRoot - 1] != '\\')
                RootDir.Buffer[cwcRoot++] = '\\';
            memcpy(&RootDir.Buffer[cwcRoot], pNtName->Buffer, cwcAppend * sizeof(RTUTF16));
            RTUtf16Free(pNtName->Buffer);
            pNtName->Length        = (uint16_t)((cwcRoot + cwcAppend) * sizeof(RTUTF16));
            pNtName->MaximumLength = RootDir.MaximumLength;
            pNtName->Buffer        = RootDir.Buffer;

            *phRootDir = NULL;
            return VINF_SUCCESS;
        }
        RTUtf16Free(pNtName->Buffer);
    }
    pNtName->Length        = 0;
    pNtName->MaximumLength = 0;
    pNtName->Buffer        = NULL;
    return rc;
}


/**
 * Rewinds the path back to the start of the previous component.
 *
 * Will preserve root slash.
 *
 * @returns Pointer to character after the start-of-component slash, or
 *          pwszStart.
 * @param   pwcEnd              The current end of the path.
 * @param   pwszStart           The start of the path.
 */
static PRTUTF16 rtNtPathGetPrevComponent(PRTUTF16 pwcEnd, PRTUTF16 pwszStart)
{
    if ((uintptr_t)pwcEnd > (uintptr_t)pwszStart)
    {
        RTUTF16 wc = pwcEnd[-1];
        if (   (wc == '\\' || wc == '/')
            && (uintptr_t)(pwcEnd - pwszStart) != 1)
            pwcEnd--;

        while (   (uintptr_t)pwcEnd > (uintptr_t)pwszStart
               && (wc = pwcEnd[-1]) != '\\'
               && (wc = pwcEnd[-1]) != '/')
            pwcEnd--;
    }
    return pwcEnd;
}


/**
 * Converts a relative windows-style path to relative NT format and encoding.
 *
 * @returns IPRT status code.
 * @param   pNtName             Where to return the NT name.  Free using
 *                              rtTNtPathToNative with phRootDir set to NULL.
 * @param   phRootDir           On input, the handle to the directory the path
 *                              is relative to.  On output, the handle to
 *                              specify as root directory in the object
 *                              attributes when accessing the path.  If
 *                              enmAscent is kRTNtPathRelativeAscent_Allow, it
 *                              may have been set to NULL.
 * @param   pszPath             The relative UTF-8 path.
 * @param   enmAscent           How to handle ascent.
 * @param   fMustReturnAbsolute Must convert to an absolute path.  This
 *                              is necessary if the root dir is a NT directory
 *                              object (e.g. /Devices) since they cannot parse
 *                              relative paths it seems.
 */
RTDECL(int) RTNtPathRelativeFromUtf8(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir, const char *pszPath,
                                     RTNTPATHRELATIVEASCENT enmAscent, bool fMustReturnAbsolute)
{
    size_t cwcMax;
    int rc = RTStrCalcUtf16LenEx(pszPath, RTSTR_MAX, &cwcMax);
    if (RT_FAILURE(rc))
        return rc;
    if (cwcMax + 2 >= _32K)
        return VERR_FILENAME_TOO_LONG;

    PRTUTF16 pwszDst;
    pNtName->Length        = 0;
    pNtName->MaximumLength = (uint16_t)((cwcMax + 2) * sizeof(RTUTF16));
    pNtName->Buffer        = pwszDst = RTUtf16Alloc((cwcMax + 2) * sizeof(RTUTF16));
    if (!pwszDst)
        return VERR_NO_UTF16_MEMORY;

    PRTUTF16 pwszDstCur  = pwszDst;
    PRTUTF16 pwszDstComp = pwszDst;
    for (;;)
    {
        RTUNICP uc;
        rc = RTStrGetCpEx(&pszPath, &uc);
        if (RT_SUCCESS(rc))
        {
            switch (uc)
            {
                default:
                    pwszDstCur = RTUtf16PutCp(pwszDstCur, uc);
                    break;

                case '\\':
                case '/':
                    if (pwszDstCur != pwszDstComp)
                        pwszDstComp = pwszDstCur = RTUtf16PutCp(pwszDstCur, '\\');
                    /* else: only one slash between components. */
                    break;

                case '.':
                    if (pwszDstCur == pwszDstComp)
                    {
                        /*
                         * Single dot changes nothing.
                         */
                        char ch2 = *pszPath;
                        if (ch2 == '\0')
                        {
                            /* Trailing single dot means we need to drop trailing slash. */
                            if (pwszDstCur != pwszDst)
                                pwszDstCur--;
                            *pwszDstCur = '\0';
                            pNtName->Length = (uint16_t)((uintptr_t)pwszDstCur - (uintptr_t)pwszDst);
                            if (!fMustReturnAbsolute || *phRootDir == NULL)
                                return VINF_SUCCESS;
                            return rtNtPathRelativeToAbs(pNtName, phRootDir);
                        }

                        if (ch2 == '\\' || ch2 == '/')
                        {
                            pszPath++; /* Ignore lone dot followed but another component. */
                            break;
                        }

                        /*
                         * Two dots drops off the last directory component.  This gets complicated
                         * when we start out without any path and we need to consult enmAscent.
                         */
                        if (ch2 == '.')
                        {
                            char ch3 = pszPath[1];
                            if (   ch3 == '\\'
                                || ch3 == '/'
                                || ch3 == '\0')
                            {
                                /* Drop a path component. */
                                if (pwszDstComp != pwszDst)
                                    pwszDstComp = pwszDstCur = rtNtPathGetPrevComponent(pwszDstCur, pwszDst);
                                /* Hit the start, which is a bit complicated. */
                                else
                                    switch (enmAscent)
                                    {
                                        case kRTNtPathRelativeAscent_Allow:
                                            if (*phRootDir != NULL)
                                            {
                                                RTUtf16Free(pwszDst);
                                                rc = RTNtPathFromHandle(pNtName, *phRootDir, cwcMax + 2);
                                                if (RT_FAILURE(rc))
                                                    return rc;

                                                *phRootDir = NULL;
                                                pwszDst    = pNtName->Buffer;
                                                pwszDstCur = &pwszDst[pNtName->Length / sizeof(RTUTF16)];
                                                if (   pwszDst != pwszDstCur
                                                    && pwszDstCur[-1] != '\\'
                                                    && pwszDstCur[-1] != '/')
                                                    *pwszDstCur++ = '\\';
                                                pwszDstComp = pwszDstCur = rtNtPathGetPrevComponent(pwszDstCur, pwszDst);
                                            }
                                            /* else: ignore attempt to ascend beyond the NT root (won't get here). */
                                            break;

                                        case kRTNtPathRelativeAscent_Ignore:
                                            /* nothing to do here */
                                            break;

                                        default:
                                        case kRTNtPathRelativeAscent_Fail:
                                            RTUtf16Free(pwszDst);
                                            return VERR_PATH_NOT_FOUND;
                                    }

                                if (ch3 == '\0')
                                {
                                    *pwszDstCur = '\0';
                                    pNtName->Length = (uint16_t)((uintptr_t)pwszDstCur - (uintptr_t)pwszDst);
                                    if (!fMustReturnAbsolute || *phRootDir == NULL)
                                        return VINF_SUCCESS;
                                    return rtNtPathRelativeToAbs(pNtName, phRootDir);
                                }
                                pszPath += 2;
                                break;
                            }
                        }
                    }

                    /* Neither '.' nor '..'. */
                    pwszDstCur = RTUtf16PutCp(pwszDstCur, '.');
                    break;

                case '\0':
                    *pwszDstCur = '\0';
                    pNtName->Length = (uint16_t)((uintptr_t)pwszDstCur - (uintptr_t)pwszDst);
                    if (!fMustReturnAbsolute || *phRootDir == NULL)
                        return VINF_SUCCESS;
                    return rtNtPathRelativeToAbs(pNtName, phRootDir);
            }
        }
    }
}


/**
 * Frees the native path and root handle.
 *
 * @param   pNtName             The NT path after a successful rtNtPathToNative
 *                              call or RTNtPathRelativeFromUtf8.
 * @param   phRootDir           The root handle variable from rtNtPathToNative,
 *                              but NOT RTNtPathRelativeFromUtf8.
 */
static void rtNtPathFreeNative(struct _UNICODE_STRING *pNtName, PHANDLE phRootDir)
{
    RTUtf16Free(pNtName->Buffer);
    pNtName->Buffer = NULL;

    RT_NOREF_PV(phRootDir); /* never returned by rtNtPathToNative, shouldn't be freed in connection with RTNtPathRelativeFromUtf8 */
}


/**
 * Frees the native path and root handle.
 *
 * @param   pNtName             The NT path after a successful rtNtPathToNative
 *                              call or RTNtPathRelativeFromUtf8.
 * @param   phRootDir           The root handle variable from rtNtPathToNative,
 */
RTDECL(void) RTNtPathFree(struct _UNICODE_STRING *pNtName, HANDLE *phRootDir)
{
    rtNtPathFreeNative(pNtName, phRootDir);
}


/**
 * Wrapper around NtCreateFile.
 *
 * @returns IPRT status code.
 * @param   pszPath             The UTF-8 path.
 * @param   fDesiredAccess      See NtCreateFile.
 * @param   fFileAttribs        See NtCreateFile.
 * @param   fShareAccess        See NtCreateFile.
 * @param   fCreateDisposition  See NtCreateFile.
 * @param   fCreateOptions      See NtCreateFile.
 * @param   fObjAttribs         The OBJECT_ATTRIBUTES::Attributes value, see
 *                              NtCreateFile and InitializeObjectAttributes.
 * @param   phHandle            Where to return the handle.
 * @param   puAction            Where to return the action taken. Optional.
 */
RTDECL(int) RTNtPathOpen(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fFileAttribs, ULONG fShareAccess,
                         ULONG fCreateDisposition, ULONG fCreateOptions, ULONG fObjAttribs,
                         PHANDLE phHandle, PULONG_PTR puAction)
{
    *phHandle = RTNT_INVALID_HANDLE_VALUE;

    HANDLE         hRootDir;
    UNICODE_STRING NtName;
    int rc = rtNtPathToNative(&NtName, &hRootDir, pszPath);
    if (RT_SUCCESS(rc))
    {
        HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, fObjAttribs, hRootDir, NULL);

        NTSTATUS rcNt = NtCreateFile(&hFile,
                                     fDesiredAccess,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /* AllocationSize*/,
                                     fFileAttribs,
                                     fShareAccess,
                                     fCreateDisposition,
                                     fCreateOptions,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
        {
            if (puAction)
                *puAction = Ios.Information;
            *phHandle = hFile;
            rc = VINF_SUCCESS;
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);
        rtNtPathFreeNative(&NtName, &hRootDir);
    }
    return rc;
}


/**
 * Wrapper around NtCreateFile.
 *
 * @returns IPRT status code.
 * @param   pszPath             The UTF-8 path.
 * @param   fDesiredAccess      See NtCreateFile.
 * @param   fShareAccess        See NtCreateFile.
 * @param   fCreateOptions      See NtCreateFile.
 * @param   fObjAttribs         The OBJECT_ATTRIBUTES::Attributes value, see
 *                              NtCreateFile and InitializeObjectAttributes.
 * @param   phHandle            Where to return the handle.
 * @param   pfObjDir            If not NULL, the variable pointed to will be set
 *                              to @c true if we opened an object directory and
 *                              @c false if we opened an directory file (normal
 *                              directory).
 */
RTDECL(int) RTNtPathOpenDir(const char *pszPath, ACCESS_MASK fDesiredAccess, ULONG fShareAccess, ULONG fCreateOptions,
                            ULONG fObjAttribs, PHANDLE phHandle, bool *pfObjDir)
{
    *phHandle = RTNT_INVALID_HANDLE_VALUE;

    HANDLE         hRootDir;
    UNICODE_STRING NtName;
    int rc = rtNtPathToNative(&NtName, &hRootDir, pszPath);
    if (RT_SUCCESS(rc))
    {
        if (pfObjDir)
        {
            *pfObjDir = false;
#ifdef IPRT_WITH_NT_PATH_PASSTHRU
            if (   !RTPATH_IS_SLASH(pszPath[0])
                || !RTPATH_IS_SLASH(pszPath[1])
                || pszPath[2] != ':'
                || pszPath[3] != 'i'
                || pszPath[4] != 'p'
                || pszPath[5] != 'r'
                || pszPath[6] != 't'
                || pszPath[7] != 'n'
                || pszPath[8] != 't'
                || pszPath[9] != ':'
                || !RTPATH_IS_SLASH(pszPath[10]) )
#endif
                pfObjDir = NULL;
        }
        rc = RTNtPathOpenDirEx(hRootDir, &NtName, fDesiredAccess, fShareAccess, fCreateOptions, fObjAttribs, phHandle, pfObjDir);
        rtNtPathFreeNative(&NtName, &hRootDir);
    }
    return rc;
}



/**
 * Wrapper around NtCreateFile, extended version.
 *
 * @returns IPRT status code.
 * @param   hRootDir            The root director the path is relative to.  NULL
 *                              if none.
 * @param   pNtName             The NT path.
 * @param   fDesiredAccess      See NtCreateFile.
 * @param   fShareAccess        See NtCreateFile.
 * @param   fCreateOptions      See NtCreateFile.
 * @param   fObjAttribs         The OBJECT_ATTRIBUTES::Attributes value, see
 *                              NtCreateFile and InitializeObjectAttributes.
 * @param   phHandle            Where to return the handle.
 * @param   pfObjDir            If not NULL, the variable pointed to will be set
 *                              to @c true if we opened an object directory and
 *                              @c false if we opened an directory file (normal
 *                              directory).
 */
RTDECL(int) RTNtPathOpenDirEx(HANDLE hRootDir, struct _UNICODE_STRING *pNtName, ACCESS_MASK fDesiredAccess, ULONG fShareAccess,
                              ULONG fCreateOptions, ULONG fObjAttribs, PHANDLE phHandle, bool *pfObjDir)
{
    *phHandle = RTNT_INVALID_HANDLE_VALUE;

    HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    OBJECT_ATTRIBUTES   ObjAttr;
    InitializeObjectAttributes(&ObjAttr, pNtName, fObjAttribs, hRootDir, NULL);

    NTSTATUS rcNt = NtCreateFile(&hFile,
                                 fDesiredAccess,
                                 &ObjAttr,
                                 &Ios,
                                 NULL /* AllocationSize*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 fShareAccess,
                                 FILE_OPEN,
                                 fCreateOptions,
                                 NULL /*EaBuffer*/,
                                 0 /*EaLength*/);
    if (NT_SUCCESS(rcNt))
    {
        if (pfObjDir)
            *pfObjDir = false;
        *phHandle = hFile;
        return VINF_SUCCESS;
    }

    /*
     * Try add a slash in case this is a device object with a file system attached.
     */
    if (   rcNt == STATUS_INVALID_PARAMETER
        && pNtName->Length < _64K - 4
        && (   pNtName->Length == 0
            || pNtName->Buffer[pNtName->Length / sizeof(RTUTF16)] != '\\') )
    {
        UNICODE_STRING NtTmp;
        NtTmp.Length        = pNtName->Length + 2;
        NtTmp.MaximumLength = NtTmp.Length + 2;
        NtTmp.Buffer        = (PRTUTF16)RTMemTmpAlloc(NtTmp.MaximumLength);
        if (NtTmp.Buffer)
        {
            memcpy(NtTmp.Buffer, pNtName->Buffer, pNtName->Length);
            NtTmp.Buffer[pNtName->Length / sizeof(RTUTF16)] = '\\';
            NtTmp.Buffer[pNtName->Length / sizeof(RTUTF16) + 1] = '\0';

            hFile = RTNT_INVALID_HANDLE_VALUE;
            Ios.Status = -1;
            Ios.Information = 0;
            ObjAttr.ObjectName = &NtTmp;

            rcNt = NtCreateFile(&hFile,
                                fDesiredAccess,
                                &ObjAttr,
                                &Ios,
                                NULL /* AllocationSize*/,
                                FILE_ATTRIBUTE_NORMAL,
                                fShareAccess,
                                FILE_OPEN,
                                fCreateOptions,
                                NULL /*EaBuffer*/,
                                0 /*EaLength*/);
            RTMemTmpFree(NtTmp.Buffer);
            if (NT_SUCCESS(rcNt))
            {
                if (pfObjDir)
                    *pfObjDir = false;
                *phHandle = hFile;
                return VINF_SUCCESS;
            }
            ObjAttr.ObjectName = pNtName;
        }
    }

    /*
     * Try open it as a directory object if it makes sense.
     */
    if (   pfObjDir
        && (   rcNt == STATUS_OBJECT_NAME_INVALID
            || rcNt == STATUS_OBJECT_TYPE_MISMATCH ))
    {
        /* Strip trailing slash. */
        struct _UNICODE_STRING NtName2 = *pNtName;
        if (   NtName2.Length > 2
            && RTPATH_IS_SLASH(NtName2.Buffer[(NtName2.Length / 2) - 1]))
            NtName2.Length -= 2;
        ObjAttr.ObjectName = &NtName2;

        /* Rought conversion of the access flags. */
        ULONG fObjDesiredAccess = 0;
        if (   (fDesiredAccess & GENERIC_ALL)
            || (fDesiredAccess & STANDARD_RIGHTS_ALL) == STANDARD_RIGHTS_ALL)
            fObjDesiredAccess = DIRECTORY_ALL_ACCESS;
        else
        {
            if (fDesiredAccess & (GENERIC_WRITE | STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA))
                fObjDesiredAccess |= DIRECTORY_CREATE_OBJECT | DIRECTORY_CREATE_OBJECT | DIRECTORY_CREATE_SUBDIRECTORY;

            if (   (fDesiredAccess & (GENERIC_READ | STANDARD_RIGHTS_READ | FILE_LIST_DIRECTORY))
                || !fObjDesiredAccess)
                fObjDesiredAccess |= DIRECTORY_QUERY;

            if (fDesiredAccess & FILE_TRAVERSE)
                fObjDesiredAccess |= DIRECTORY_TRAVERSE;
        }

        rcNt = NtOpenDirectoryObject(&hFile, fObjDesiredAccess, &ObjAttr);
        if (NT_SUCCESS(rcNt))
        {
            *pfObjDir = true;
            *phHandle = hFile;
            return VINF_SUCCESS;
        }
    }

    return RTErrConvertFromNtStatus(rcNt);
}



/**
 * Closes an handled open by rtNtPathOpen.
 *
 * @returns IPRT status code
 * @param   hHandle             The handle value.
 */
RTDECL(int) RTNtPathClose(HANDLE hHandle)
{
    NTSTATUS rcNt = NtClose(hHandle);
    if (NT_SUCCESS(rcNt))
        return VINF_SUCCESS;
    return RTErrConvertFromNtStatus(rcNt);
}

