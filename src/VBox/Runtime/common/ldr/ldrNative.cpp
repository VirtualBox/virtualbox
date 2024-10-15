/* $Id: ldrNative.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, Native interface.
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
#include <iprt/ldr.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include "internal/ldr.h"


/** @copydoc RTLDROPS::pfnEnumSymbols */
static DECLCALLBACK(int) rtldrNativeEnumSymbols(PRTLDRMODINTERNAL pMod, unsigned fFlags, const void *pvBits,
                                                RTUINTPTR BaseAddress, PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    NOREF(pMod); NOREF(fFlags); NOREF(pvBits); NOREF(BaseAddress); NOREF(pfnCallback); NOREF(pvUser);
    return VERR_NOT_SUPPORTED;
}


/** @copydoc RTLDROPS::pfnDone */
static DECLCALLBACK(int) rtldrNativeDone(PRTLDRMODINTERNAL pMod)
{
    NOREF(pMod);
    return VINF_SUCCESS;
}


/**
 * Operations for a native module.
 */
static const RTLDROPS g_rtldrNativeOps =
{
    "native",
    rtldrNativeClose,
    rtldrNativeGetSymbol,
    rtldrNativeDone,
    rtldrNativeEnumSymbols,
    /* ext: */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL /*pfnQueryForwarderInfo*/,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL /*pfnUnwindFrame*/,
    42
};



RTDECL(int) RTLdrLoad(const char *pszFilename, PRTLDRMOD phLdrMod)
{
    return RTLdrLoadEx(pszFilename, phLdrMod, RTLDRLOAD_FLAGS_LOCAL, NULL);
}
RT_EXPORT_SYMBOL(RTLdrLoad);


RTDECL(int) RTLdrLoadEx(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    LogFlow(("RTLdrLoadEx: pszFilename=%p:{%s} phLdrMod=%p fFlags=%#x pErrInfo=%p\n", pszFilename, pszFilename, phLdrMod, fFlags, pErrInfo));

    /*
     * Validate and massage the input.
     */
    RTErrInfoClear(pErrInfo);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(phLdrMod, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTLDRLOAD_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Allocate and initialize module structure.
     */
    int rc = VERR_NO_MEMORY;
    PRTLDRMODNATIVE pMod = (PRTLDRMODNATIVE)RTMemAlloc(sizeof(*pMod));
    if (pMod)
    {
        pMod->Core.u32Magic     = RTLDRMOD_MAGIC;
        pMod->Core.eState       = LDR_STATE_LOADED;
        pMod->Core.pOps         = &g_rtldrNativeOps;
        pMod->Core.pReader      = NULL;
        pMod->Core.enmFormat    = RTLDRFMT_NATIVE;
        pMod->Core.enmType      = RTLDRTYPE_SHARED_LIBRARY_RELOCATABLE; /* approx */
#ifdef RT_BIG_ENDIAN
        pMod->Core.enmEndian    = RTLDRENDIAN_BIG;
#else
        pMod->Core.enmEndian    = RTLDRENDIAN_LITTLE;
#endif
#ifdef RT_ARCH_AMD64
        pMod->Core.enmArch      = RTLDRARCH_AMD64;
#elif defined(RT_ARCH_X86)
        pMod->Core.enmArch      = RTLDRARCH_X86_32;
#else
        pMod->Core.enmArch      = RTLDRARCH_HOST;
#endif
        pMod->hNative           = ~(uintptr_t)0;
        pMod->fFlags            = fFlags;

        /*
         * Attempt to open the module.
         */
        rc = rtldrNativeLoad(pszFilename, &pMod->hNative, fFlags, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            if (fFlags & RTLDRLOAD_FLAGS_NO_UNLOAD)
                RTMEM_MAY_LEAK(pMod);

            *phLdrMod = &pMod->Core;
            LogFlow(("RTLdrLoad: returns %Rrc *phLdrMod=%RTldrm\n", rc,*phLdrMod));
            return rc;
        }

        RTMemFree(pMod);
    }
    else
        RTErrInfoSetF(pErrInfo, rc, "Failed to allocate %zu bytes for the module handle", sizeof(*pMod));
    *phLdrMod = NIL_RTLDRMOD;
    LogFlow(("RTLdrLoad: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrLoadEx);


RTDECL(int) RTLdrLoadSystem(const char *pszFilename, bool fNoUnload, PRTLDRMOD phLdrMod)
{
    return RTLdrLoadSystemEx(pszFilename, fNoUnload ? RTLDRLOAD_FLAGS_NO_UNLOAD : 0, phLdrMod);
}


RTDECL(int) RTLdrLoadSystemEx(const char *pszFilename, uint32_t fFlags, PRTLDRMOD phLdrMod)
{
    LogFlow(("RTLdrLoadSystemEx: pszFilename=%p:{%s} fFlags=%#RX32 phLdrMod=%p\n", pszFilename, pszFilename, fFlags, phLdrMod));

    /*
     * Validate input.
     */
    AssertPtrReturn(phLdrMod, VERR_INVALID_PARAMETER);
    *phLdrMod = NIL_RTLDRMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_PARAMETER);
    AssertMsgReturn(!RTPathHasPath(pszFilename), ("%s\n", pszFilename), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fFlags & ~(RTLDRLOAD_FLAGS_VALID_MASK | RTLDRLOAD_FLAGS_SO_VER_BEGIN_MASK | RTLDRLOAD_FLAGS_SO_VER_END_MASK)),
                    ("fFlags=%#RX32\n", fFlags), VERR_INVALID_FLAGS);

    /*
     * Check the filename.
     */
    size_t cchFilename = strlen(pszFilename);
    AssertMsgReturn(cchFilename < (RTPATH_MAX / 4) * 3, ("%zu\n", cchFilename), VERR_INVALID_PARAMETER);

    const char *pszSuffix = NULL;
    if (!RTPathHasSuffix(pszFilename))
        pszSuffix = RTLdrGetSuff();

    /*
     * Let the platform specific code do the rest.
     */
    int rc = rtldrNativeLoadSystem(pszFilename, pszSuffix, fFlags, phLdrMod);
    LogFlow(("RTLdrLoadSystem: returns %Rrc\n", rc));
    return rc;
}


RTDECL(void *) RTLdrGetSystemSymbol(const char *pszFilename, const char *pszSymbol)
{
    return RTLdrGetSystemSymbolEx(pszFilename, pszSymbol, RTLDRLOAD_FLAGS_NO_UNLOAD);
}


RTDECL(void *) RTLdrGetSystemSymbolEx(const char *pszFilename, const char *pszSymbol, uint32_t fFlags)
{
    void    *pvRet = NULL;
    RTLDRMOD hLdrMod;
    int rc = RTLdrLoadSystemEx(pszFilename, fFlags | RTLDRLOAD_FLAGS_NO_UNLOAD, &hLdrMod);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hLdrMod, pszSymbol, &pvRet);
        if (RT_FAILURE(rc))
            pvRet = NULL; /* paranoia */
        RTLdrClose(hLdrMod);
    }
    return pvRet;
}


RTDECL(int) RTLdrLoadAppPriv(const char *pszFilename, PRTLDRMOD phLdrMod)
{
    LogFlow(("RTLdrLoadAppPriv: pszFilename=%p:{%s} phLdrMod=%p\n", pszFilename, pszFilename, phLdrMod));

    /*
     * Validate input.
     */
    AssertPtrReturn(phLdrMod, VERR_INVALID_PARAMETER);
    *phLdrMod = NIL_RTLDRMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_PARAMETER);
    AssertMsgReturn(!RTPathHasPath(pszFilename), ("%s\n", pszFilename), VERR_INVALID_PARAMETER);

    /*
     * Check the filename.
     */
    size_t cchFilename = strlen(pszFilename);
    AssertMsgReturn(cchFilename < (RTPATH_MAX / 4) * 3, ("%zu\n", cchFilename), VERR_INVALID_PARAMETER);

    const char *pszSuffix = "";
    size_t cchSuffix = 0;
    if (!RTPathHasSuffix(pszFilename))
    {
        pszSuffix = RTLdrGetSuff();
        cchSuffix = strlen(pszSuffix);
    }

    /*
     * Construct the private arch path and check if the file exists.
     */
    char szPath[RTPATH_MAX];
    int rc = RTPathAppPrivateArch(szPath, sizeof(szPath) - 1 - cchSuffix - cchFilename);
    AssertRCReturn(rc, rc);

    char *psz = strchr(szPath, '\0');
    *psz++ = RTPATH_SLASH;
    memcpy(psz, pszFilename, cchFilename);
    psz += cchFilename;
    memcpy(psz, pszSuffix, cchSuffix + 1);

    if (!RTPathExists(szPath))
    {
        LogRel(("RTLdrLoadAppPriv: \"%s\" not found\n", szPath));
        return VERR_FILE_NOT_FOUND;
    }

    /*
     * Pass it on to RTLdrLoad.
     */
    rc = RTLdrLoad(szPath, phLdrMod);

    LogFlow(("RTLdrLoadAppPriv: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTLdrLoadAppPriv);


RTDECL(const char *) RTLdrGetSuff(void)
{
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    static const char s_szSuff[] = ".DLL";
#elif defined(RT_OS_L4)
    static const char s_szSuff[] = ".s.so";
#elif defined(RT_OS_DARWIN)
    static const char s_szSuff[] = ".dylib";
#else
    static const char s_szSuff[] = ".so";
#endif

    return s_szSuff;
}
RT_EXPORT_SYMBOL(RTLdrGetSuff);


RTDECL(uintptr_t) RTLdrGetNativeHandle(RTLDRMOD hLdrMod)
{
    PRTLDRMODNATIVE pThis = (PRTLDRMODNATIVE)hLdrMod;
    AssertPtrReturn(pThis, ~(uintptr_t)0);
    AssertReturn(pThis->Core.u32Magic == RTLDRMOD_MAGIC, ~(uintptr_t)0);
    AssertReturn(pThis->Core.pOps == &g_rtldrNativeOps, ~(uintptr_t)0);
    return pThis->hNative;
}
RT_EXPORT_SYMBOL(RTLdrGetNativeHandle);


RTDECL(bool) RTLdrIsLoadable(const char *pszFilename)
{
    /*
     * Try to load the library.
     */
    RTLDRMOD hLib;
    int rc = RTLdrLoad(pszFilename, &hLib);
    if (RT_SUCCESS(rc))
    {
        RTLdrClose(hLib);
        return true;
    }
    return false;
}
RT_EXPORT_SYMBOL(RTLdrIsLoadable);

