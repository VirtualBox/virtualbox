/* $Id: ldrNative-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, Win32 native.
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
#define LOG_GROUP RTLOGGROUP_LDR
#include <iprt/nt/nt-and-windows.h>

#include <iprt/ldr.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "internal/ldr.h"
#include "internal-r3-win.h"

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
# define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR       UINT32_C(0x100)
# define LOAD_LIBRARY_SEARCH_APPLICATION_DIR    UINT32_C(0x200)
# define LOAD_LIBRARY_SEARCH_SYSTEM32           UINT32_C(0x800)
#endif


DECLHIDDEN(int) rtldrNativeLoad(const char *pszFilename, uintptr_t *phHandle, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    Assert(sizeof(*phHandle) >= sizeof(HMODULE));
    AssertReturn(!(fFlags & RTLDRLOAD_FLAGS_GLOBAL), VERR_INVALID_FLAGS);
    AssertLogRelMsgReturn(RTPathStartsWithRoot(pszFilename),  /* Relative names will still be applied to the search path. */
                          ("pszFilename='%s'\n", pszFilename),
                          VERR_INTERNAL_ERROR_2);
    AssertReleaseMsg(g_hModKernel32,
                     ("rtldrNativeLoad(%s,,) is called before IPRT has configured the windows loader!\n", pszFilename));

    /*
     * Convert to UTF-16 and make sure it got a .DLL suffix.
     */
    /** @todo Implement long path support for native DLL loading on windows. @bugref{9248} */
    int rc;
    RTUTF16 *pwszNative = NULL;
    if (RTPathHasSuffix(pszFilename) || (fFlags & RTLDRLOAD_FLAGS_NO_SUFFIX))
        rc = RTStrToUtf16(pszFilename, &pwszNative);
    else
    {
        size_t cwcAlloc;
        rc = RTStrCalcUtf16LenEx(pszFilename, RTSTR_MAX, &cwcAlloc);
        if (RT_SUCCESS(rc))
        {
            cwcAlloc += sizeof(".DLL");
            pwszNative = RTUtf16Alloc(cwcAlloc * sizeof(RTUTF16));
            if (pwszNative)
            {
                size_t cwcNative;
                rc = RTStrToUtf16Ex(pszFilename, RTSTR_MAX, &pwszNative, cwcAlloc, &cwcNative);
                if (RT_SUCCESS(rc))
                    rc = RTUtf16CopyAscii(&pwszNative[cwcNative], cwcAlloc - cwcNative, ".DLL");
            }
            else
                rc = VERR_NO_UTF16_MEMORY;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /* Convert slashes just to be on the safe side. */
        for (size_t off = 0;; off++)
        {
            RTUTF16 wc = pwszNative[off];
            if (wc == '/')
                pwszNative[off] = '\\';
            else if (!wc)
                break;
        }

        /*
         * Attempt load.
         */
        HMODULE     hmod;
        static int  s_iSearchDllLoadDirSupported = 0;
        if (   !(fFlags & RTLDRLOAD_FLAGS_NT_SEARCH_DLL_LOAD_DIR)
            || s_iSearchDllLoadDirSupported < 0)
            hmod = LoadLibraryExW(pwszNative, NULL, 0);
        else
        {
            hmod = LoadLibraryExW(pwszNative, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32
                                  | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
            if (s_iSearchDllLoadDirSupported == 0)
            {
                if (hmod != NULL || GetLastError() != ERROR_INVALID_PARAMETER)
                    s_iSearchDllLoadDirSupported = 1;
                else
                {
                    s_iSearchDllLoadDirSupported = -1;
                    hmod = LoadLibraryExW(pwszNative, NULL, 0);
                }
            }
        }
        if (hmod)
        {
            *phHandle = (uintptr_t)hmod;
            RTUtf16Free(pwszNative);
            return VINF_SUCCESS;
        }

        /*
         * Try figure why it failed to load.
         */
        DWORD dwErr = GetLastError();
        rc = RTErrConvertFromWin32(dwErr);
        rc = RTErrInfoSetF(pErrInfo, rc, "GetLastError=%u", dwErr);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, rc, "Error converting UTF-8 to UTF-16 string.");
    RTUtf16Free(pwszNative);
    return rc;
}


DECLCALLBACK(int) rtldrNativeGetSymbol(PRTLDRMODINTERNAL pMod, const char *pszSymbol, void **ppvValue)
{
    PRTLDRMODNATIVE pModNative = (PRTLDRMODNATIVE)pMod;
    FARPROC pfn = GetProcAddress((HMODULE)pModNative->hNative, pszSymbol);
    if (pfn)
    {
        *ppvValue = (void *)pfn;
        return VINF_SUCCESS;
    }
    *ppvValue = NULL;
    return RTErrConvertFromWin32(GetLastError());
}


DECLCALLBACK(int) rtldrNativeClose(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODNATIVE pModNative = (PRTLDRMODNATIVE)pMod;
    if (   (pModNative->fFlags & RTLDRLOAD_FLAGS_NO_UNLOAD)
        || FreeLibrary((HMODULE)pModNative->hNative))
    {
        pModNative->hNative = (uintptr_t)INVALID_HANDLE_VALUE;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromWin32(GetLastError());
}


DECLHIDDEN(int) rtldrNativeLoadSystem(const char *pszFilename, const char *pszExt, uint32_t fFlags, PRTLDRMOD phLdrMod)
{
    AssertReleaseMsg(g_hModKernel32,
                     ("rtldrNativeLoadSystem(%s,,) is called before IPRT has configured the windows loader!\n", pszFilename));

    /*
     * Resolve side-by-side resolver API.
     */
    static bool volatile s_fInitialized = false;
    static decltype(RtlDosApplyFileIsolationRedirection_Ustr) *s_pfnApplyRedir = NULL;
    if (!s_fInitialized)
    {
        s_pfnApplyRedir = (decltype(s_pfnApplyRedir))GetProcAddress(g_hModNtDll,
                                                                    "RtlDosApplyFileIsolationRedirection_Ustr");
        ASMCompilerBarrier();
        s_fInitialized = true;
    }

    /*
     * We try WinSxS via undocumented NTDLL API and flal back on the System32
     * directory. No other locations are supported.
     */
    int rc = VERR_TRY_AGAIN;
    char  szPath[RTPATH_MAX];
    char *pszPath = szPath;

    /* Get the windows system32 directory so we can sanity check the WinSxS result. */
    WCHAR wszSysDir[MAX_PATH];
    UINT cwcSysDir = GetSystemDirectoryW(wszSysDir, MAX_PATH);
    if (cwcSysDir >= MAX_PATH)
        return VERR_FILENAME_TOO_LONG;

    /* Try side-by-side first (see COMCTL32.DLL). */
    if (s_pfnApplyRedir)
    {
        size_t   cwcName = 0;
        RTUTF16  wszName[MAX_PATH];
        PRTUTF16 pwszName = wszName;
        int rc2 = RTStrToUtf16Ex(pszFilename, RTSTR_MAX, &pwszName, RT_ELEMENTS(wszName), &cwcName);
        if (RT_SUCCESS(rc2))
        {
            static UNICODE_STRING const s_DefaultSuffix = RTNT_CONSTANT_UNISTR(L".dll");
            WCHAR           wszPath[MAX_PATH];
            UNICODE_STRING  UniStrStatic   = { 0, (USHORT)sizeof(wszPath) - sizeof(WCHAR), wszPath };
            UNICODE_STRING  UniStrDynamic  = { 0, 0, NULL };
            PUNICODE_STRING pUniStrResult  = NULL;
            UNICODE_STRING  UniStrName     =
            { (USHORT)(cwcName * sizeof(RTUTF16)), (USHORT)((cwcName + 1) * sizeof(RTUTF16)), wszName };

            NTSTATUS rcNt = s_pfnApplyRedir(1 /*fFlags*/,
                                            &UniStrName,
                                            (PUNICODE_STRING)&s_DefaultSuffix,
                                            &UniStrStatic,
                                            &UniStrDynamic,
                                            &pUniStrResult,
                                            NULL /*pNewFlags*/,
                                            NULL /*pcbFilename*/,
                                            NULL /*pcbNeeded*/);
            if (NT_SUCCESS(rcNt))
            {
                /*
                 * Check that the resolved path has similarities to the
                 * system directory.
                 *
                 * ASSUMES the windows directory is a root directory and
                 * that both System32 and are on the same level.  So, we'll
                 * have 2 matching components (or more if the resolver
                 * returns a system32 path for some reason).
                 */
                unsigned cMatchingComponents = 0;
                size_t   off = 0;
                while (off < pUniStrResult->Length)
                {
                    RTUTF16 wc1 = wszSysDir[off];
                    RTUTF16 wc2 = pUniStrResult->Buffer[off];
                    if (!RTPATH_IS_SLASH(wc1))
                    {
                        if (wc1 == wc2)
                            off++;
                        else if (   wc1 < 127
                                 && wc2 < 127
                                 && RT_C_TO_LOWER(wc1) == RT_C_TO_LOWER(wc2) )
                            off++;
                        else
                            break;
                    }
                    else if (RTPATH_IS_SLASH(wc2))
                    {
                        cMatchingComponents += off > 0;
                        do
                            off++;
                        while (   off < pUniStrResult->Length
                               && RTPATH_IS_SLASH(wszSysDir[off])
                               && RTPATH_IS_SLASH(pUniStrResult->Buffer[off]));
                    }
                    else
                        break;
                }
                if (cMatchingComponents >= 2)
                {
                    pszPath = szPath;
                    rc2 = RTUtf16ToUtf8Ex(pUniStrResult->Buffer, pUniStrResult->Length / sizeof(RTUTF16),
                                          &pszPath, sizeof(szPath), NULL);
                    if (RT_SUCCESS(rc2))
                        rc = VINF_SUCCESS;
                }
                else
                    AssertMsgFailed(("%s -> '%*.ls'\n", pszFilename, pUniStrResult->Length, pUniStrResult->Buffer));
                RtlFreeUnicodeString(&UniStrDynamic);
            }
        }
        else
            AssertMsgFailed(("%Rrc\n", rc));
    }

    /* If the above didn't succeed, create a system32 path. */
    if (RT_FAILURE(rc))
    {
        rc = RTUtf16ToUtf8Ex(wszSysDir, RTSTR_MAX, &pszPath, sizeof(szPath), NULL);
        if (RT_SUCCESS(rc))
        {
            rc = RTPathAppend(szPath, sizeof(szPath), pszFilename);
            if (pszExt && RT_SUCCESS(rc))
                rc = RTStrCat(szPath, sizeof(szPath), pszExt);
        }
    }

    /* Do the actual loading, if we were successful constructing a name. */
    if (RT_SUCCESS(rc))
    {
        if (RTFileExists(szPath))
            rc = RTLdrLoadEx(szPath, phLdrMod, fFlags, NULL);
        else
            rc = VERR_MODULE_NOT_FOUND;
    }

    return rc;
}

