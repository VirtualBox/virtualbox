/* $Id: ldrNative-posix.cpp 109313 2025-04-23 11:36:21Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, POSIX native.
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
#include <dlfcn.h>

#include <iprt/ldr.h>
#include <iprt/assert.h>
#include <iprt/path.h>
#include <iprt/alloca.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/ldr.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
static const char g_szSuff[] = ".DLL";
#elif defined(RT_OS_L4)
static const char g_szSuff[] = ".s.so";
#elif defined(RT_OS_DARWIN)
static const char g_szSuff[] = ".dylib";
#else
static const char g_szSuff[] = ".so";
#endif


DECLHIDDEN(int) rtldrNativeLoad(const char *pszFilename, uintptr_t *phHandle, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    /*
     * Do we need to add an extension?
     */
    if (!RTPathHasSuffix(pszFilename) && !(fFlags & RTLDRLOAD_FLAGS_NO_SUFFIX))
    {
        size_t cch = strlen(pszFilename);
        char *psz = (char *)alloca(cch + sizeof(g_szSuff));
        if (!psz)
            return RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "alloca failed");
        memcpy(psz, pszFilename, cch);
        memcpy(psz + cch, g_szSuff, sizeof(g_szSuff));
        pszFilename = psz;
    }

    /*
     * Attempt load.
     */
    int fFlagsNative = RTLD_NOW;
    if (fFlags & RTLDRLOAD_FLAGS_GLOBAL)
        fFlagsNative |= RTLD_GLOBAL;
    else
        fFlagsNative |= RTLD_LOCAL;
    void *pvMod = dlopen(pszFilename, fFlagsNative);
    if (pvMod)
    {
        *phHandle = (uintptr_t)pvMod;
        return VINF_SUCCESS;
    }

    const char *pszDlError = dlerror();
    RTErrInfoSet(pErrInfo, VERR_FILE_NOT_FOUND, RT_VALID_PTR(pszDlError) ? pszDlError : "unknown dlopen error");
    LogRel(("rtldrNativeLoad: dlopen('%s', RTLD_NOW | RTLD_LOCAL) failed: %s\n", pszFilename, pszDlError));
    return VERR_FILE_NOT_FOUND;
}


DECLCALLBACK(int) rtldrNativeGetSymbol(PRTLDRMODINTERNAL pMod, const char *pszSymbol, void **ppvValue)
{
    PRTLDRMODNATIVE pModNative = (PRTLDRMODNATIVE)pMod;
#ifdef RT_OS_OS2
    /* Prefix the symbol with an underscore (assuming __cdecl/gcc-default). */
    size_t cch = strlen(pszSymbol);
    char *psz = (char *)alloca(cch + 2);
    psz[0] = '_';
    memcpy(psz + 1, pszSymbol, cch + 1);
    pszSymbol = psz;
#endif
    *ppvValue = dlsym((void *)pModNative->hNative, pszSymbol);
    if (*ppvValue)
        return VINF_SUCCESS;
    return VERR_SYMBOL_NOT_FOUND;
}


DECLCALLBACK(int) rtldrNativeClose(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODNATIVE pModNative = (PRTLDRMODNATIVE)pMod;
#ifdef __SANITIZE_ADDRESS__
    /* If we are compiled with enabled address sanitizer (gcc/llvm), don't
     * unload the module to prevent <unknown module> in the stack trace */
    pModNative->fFlags |= RTLDRLOAD_FLAGS_NO_UNLOAD;
#endif
    if (   (pModNative->fFlags & RTLDRLOAD_FLAGS_NO_UNLOAD)
        || !dlclose((void *)pModNative->hNative))
    {
        pModNative->hNative = (uintptr_t)0;
        return VINF_SUCCESS;
    }
    Log(("rtldrNativeFree: dlclose(%p) failed: %s\n", pModNative->hNative, dlerror()));
    return VERR_GENERAL_FAILURE;
}


DECLHIDDEN(int) rtldrNativeLoadSystem(const char *pszFilename, const char *pszExt, uint32_t fFlags, PRTLDRMOD phLdrMod)
{
    /*
     * For the present we ASSUME that we can trust dlopen to load what we want
     * when not specifying a path.  There seems to be very little we can do to
     * restrict the places dlopen will search for library without doing
     * auditing (linux) or something like that.
     */
    Assert(strchr(pszFilename, '/') == NULL);

    uint32_t const fFlags2 = fFlags & ~(RTLDRLOAD_FLAGS_SO_VER_BEGIN_MASK | RTLDRLOAD_FLAGS_SO_VER_END_MASK);

    /*
     * If no suffix is given and we haven't got any RTLDRLOAD_FLAGS_SO_VER_ range to work
     * with, we can call RTLdrLoadEx directly.
     */
    if (!pszExt)
    {
#if !defined(RT_OS_DARWIN) && !defined(RT_OS_OS2) && !defined(RT_OS_WINDOWS)
        if (    (fFlags & RTLDRLOAD_FLAGS_SO_VER_BEGIN_MASK) >> RTLDRLOAD_FLAGS_SO_VER_BEGIN_SHIFT
             == (fFlags & RTLDRLOAD_FLAGS_SO_VER_END_MASK)   >> RTLDRLOAD_FLAGS_SO_VER_END_SHIFT)
#endif
            return RTLdrLoadEx(pszFilename, phLdrMod, fFlags2, NULL);
        pszExt = "";
    }

    /*
     * Combine filename and suffix and then do the loading.
     */
    size_t const cchFilename = strlen(pszFilename);
    size_t const cchSuffix   = strlen(pszExt);
    char *pszTmp = (char *)alloca(cchFilename + cchSuffix + 16 + 1);
    memcpy(pszTmp, pszFilename, cchFilename);
    memcpy(&pszTmp[cchFilename], pszExt, cchSuffix);
    pszTmp[cchFilename + cchSuffix] = '\0';

    int rc = RTLdrLoadEx(pszTmp, phLdrMod, fFlags2, NULL);

#if !defined(RT_OS_DARWIN) && !defined(RT_OS_OS2) && !defined(RT_OS_WINDOWS)
    /*
     * If no version was given after the .so and do .so.MAJOR search according
     * to the range in the fFlags.
     */
    if (RT_FAILURE(rc) && !(fFlags & RTLDRLOAD_FLAGS_NO_SUFFIX))
    {
        const char *pszActualSuff = RTPathSuffix(pszTmp);
        if (pszActualSuff && strcmp(pszActualSuff, ".so") == 0)
        {
            int32_t const iBegin    = (fFlags & RTLDRLOAD_FLAGS_SO_VER_BEGIN_MASK) >> RTLDRLOAD_FLAGS_SO_VER_BEGIN_SHIFT;
            int32_t const iEnd      = (fFlags & RTLDRLOAD_FLAGS_SO_VER_END_MASK)   >> RTLDRLOAD_FLAGS_SO_VER_END_SHIFT;
            int32_t const iIncr     = iBegin <= iEnd ? 1 : -1;
            for (int32_t  iMajorVer = iBegin; iMajorVer != iEnd; iMajorVer += iIncr)
            {
                RTStrPrintf(&pszTmp[cchFilename + cchSuffix], 16 + 1, ".%d", iMajorVer);
                rc = RTLdrLoadEx(pszTmp, phLdrMod, fFlags2, NULL);
                if (RT_SUCCESS(rc))
                    break;
            }
        }
    }
#endif

    return rc;
}

