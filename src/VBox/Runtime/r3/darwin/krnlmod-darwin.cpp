/* $Id: krnlmod-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Kernel module, Darwin.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_SYSTEM
#include <iprt/krnlmod.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/string.h>
#include <iprt/types.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <libkern/OSReturn.h>


/** @name Missing/private IOKitLib declarations and definitions.
 * @{  */
/** OSKextCopyLoadedKextInfo in IOKit. */
typedef CFDictionaryRef (* PFNOSKEXTCOPYLOADEDKEXTINFO)(CFArrayRef, CFArrayRef);
/** KextManagerLoadKextWithURL in IOKit. */
typedef OSReturn (* PFNKEXTMANAGERLOADKEXTWITHURL)(CFURLRef, CFArrayRef);
/** KextManagerLoadKextWithIdentifier in IOKit */
typedef OSReturn (* PFNKEXTMANAGERLOADKEXTWITHIDENTIFIER)(CFStringRef, CFArrayRef);
/** KextManagerUnloadKextWithIdentifier in IOKit */
typedef OSReturn (* PFNKEXTMANAGERUNLOADKEXTWITHIDENTIFIER)(CFStringRef);

#ifndef kOSBundleRetainCountKey
# define kOSBundleRetainCountKey CFSTR("OSBundleRetainCount")
#endif
#ifndef kOSBundleLoadSizeKey
# define kOSBundleLoadSizeKey CFSTR("OSBundleLoadSize")
#endif
#ifndef kOSBundleLoadAddressKey
# define kOSBundleLoadAddressKey CFSTR("OSBundleLoadAddress")
#endif
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal kernel information record state.
 */
typedef struct RTKRNLMODINFOINT
{
    /** Reference counter. */
    volatile uint32_t   cRefs;
    /** The dictionary containing our data. */
    CFDictionaryRef     hDictKext;
} RTKRNLMODINFOINT;
/** Pointer to the internal kernel module information record. */
typedef RTKRNLMODINFOINT *PRTKRNLMODINFOINT;
/** Pointer to a const internal kernel module information record. */
typedef const RTKRNLMODINFOINT *PCRTKRNLMODINFOINT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTONCE                                 g_GetIoKitApisOnce = RTONCE_INITIALIZER;
static PFNOSKEXTCOPYLOADEDKEXTINFO            g_pfnOSKextCopyLoadedKextInfo = NULL;
static PFNKEXTMANAGERLOADKEXTWITHURL          g_pfnKextManagerLoadKextWithUrl = NULL;
static PFNKEXTMANAGERLOADKEXTWITHIDENTIFIER   g_pfnKextManagerLoadKextWithIdentifier = NULL;
static PFNKEXTMANAGERUNLOADKEXTWITHIDENTIFIER g_pfnKextManagerUnloadKextWithIdentifier = NULL;

/** Do-once callback for setting g_pfnOSKextCopyLoadedKextInfo.   */
static DECLCALLBACK(int) rtKrnlModDarwinResolveIoKitApis(void *pvUser)
{
    RTLDRMOD hMod;
//    int rc = RTLdrLoad("/System/Library/Frameworks/IOKit.framework/Versions/Current/IOKit", &hMod);
    int rc = RTLdrLoadEx("/System/Library/Frameworks/IOKit.framework/Versions/Current/IOKit", &hMod, RTLDRLOAD_FLAGS_NO_SUFFIX, NULL);
    if (RT_SUCCESS(rc))
    {
        RTLdrGetSymbol(hMod, "OSKextCopyLoadedKextInfo",  (void **)&g_pfnOSKextCopyLoadedKextInfo);
        RTLdrGetSymbol(hMod, "KextManagerLoadKextWithURL",  (void **)&g_pfnKextManagerLoadKextWithUrl);
        RTLdrGetSymbol(hMod, "KextManagerLoadKextWithIdentifier",  (void **)&g_pfnKextManagerLoadKextWithIdentifier);
        RTLdrGetSymbol(hMod, "KextManagerUnloadKextWithIdentifier",  (void **)&g_pfnKextManagerUnloadKextWithIdentifier);
    }

    RT_NOREF(pvUser);
    return VINF_SUCCESS;
}

/**
 * Returns the kext information dictionary structure matching the given name.
 *
 * @returns Pointer to the matching module information record on success or NULL if not found.
 * @param   pszName          The name to look for.
 */
static CFDictionaryRef rtKrnlModDarwinGetKextInfoByName(const char *pszName)
{
    CFDictionaryRef hDictKext = NULL;

    RTOnce(&g_GetIoKitApisOnce, rtKrnlModDarwinResolveIoKitApis, NULL);
    if (g_pfnOSKextCopyLoadedKextInfo)
    {
        CFStringRef hKextName = CFStringCreateWithCString(kCFAllocatorDefault, pszName, kCFStringEncodingUTF8);
        if (hKextName)
        {
            CFArrayRef hArrKextIdRef = CFArrayCreate(kCFAllocatorDefault, (const void **)&hKextName, 1, &kCFTypeArrayCallBacks);
            if (hArrKextIdRef)
            {
                CFDictionaryRef hLoadedKexts = g_pfnOSKextCopyLoadedKextInfo(hArrKextIdRef, NULL /* all info */);
                if (hLoadedKexts)
                {
                    if (CFDictionaryGetCount(hLoadedKexts) > 0)
                    {
                        hDictKext = (CFDictionaryRef)CFDictionaryGetValue(hLoadedKexts, hKextName);
                        CFRetain(hDictKext);
                    }

                    CFRelease(hLoadedKexts);
                }
                CFRelease(hArrKextIdRef);
            }
            CFRelease(hKextName);
        }
    }

    return hDictKext;
}

/**
 * Destroy the given kernel module information record.
 *
 * @param   pThis            The record to destroy.
 */
static void rtKrnlModInfoDestroy(PRTKRNLMODINFOINT pThis)
{
    CFRelease(pThis->hDictKext);
    RTMemFree(pThis);
}


RTDECL(int) RTKrnlModQueryLoaded(const char *pszName, bool *pfLoaded)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pfLoaded, VERR_INVALID_POINTER);

    CFDictionaryRef hDictKext = rtKrnlModDarwinGetKextInfoByName(pszName);
    *pfLoaded = hDictKext != NULL;
    if (hDictKext)
        CFRelease(hDictKext);

    return VINF_SUCCESS;
}


RTDECL(int) RTKrnlModLoadedQueryInfo(const char *pszName, PRTKRNLMODINFO phKrnlModInfo)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(phKrnlModInfo, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    CFDictionaryRef hDictKext = rtKrnlModDarwinGetKextInfoByName(pszName);
    if (hDictKext)
    {
        PRTKRNLMODINFOINT pThis = (PRTKRNLMODINFOINT)RTMemAllocZ(sizeof(RTKRNLMODINFOINT));
        if (pThis)
        {
            pThis->cRefs     = 1;
            pThis->hDictKext = hDictKext;

            *phKrnlModInfo = pThis;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_NOT_FOUND;

    return rc;
}


RTDECL(uint32_t) RTKrnlModLoadedGetCount(void)
{
    uint32_t cLoadedKexts = 0;
    RTOnce(&g_GetIoKitApisOnce, rtKrnlModDarwinResolveIoKitApis, NULL);
    if (g_pfnOSKextCopyLoadedKextInfo)
    {
        CFDictionaryRef hLoadedKexts = g_pfnOSKextCopyLoadedKextInfo(NULL, NULL /* all info */);
        if (hLoadedKexts)
        {
            cLoadedKexts = CFDictionaryGetCount(hLoadedKexts);
            CFRelease(hLoadedKexts);
        }
    }

    return cLoadedKexts;
}


RTDECL(int) RTKrnlModLoadedQueryInfoAll(PRTKRNLMODINFO pahKrnlModInfo, uint32_t cEntriesMax,
                                        uint32_t *pcEntries)
{
    if (cEntriesMax > 0)
        AssertPtrReturn(pahKrnlModInfo, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    RTOnce(&g_GetIoKitApisOnce, rtKrnlModDarwinResolveIoKitApis, NULL);
    if (g_pfnOSKextCopyLoadedKextInfo)
    {
        CFDictionaryRef hLoadedKexts = g_pfnOSKextCopyLoadedKextInfo(NULL, NULL /* all info */);
        if (hLoadedKexts)
        {
            uint32_t cLoadedKexts = CFDictionaryGetCount(hLoadedKexts);
            if (cLoadedKexts <= cEntriesMax)
            {
                CFDictionaryRef *pahDictKext = (CFDictionaryRef *)RTMemTmpAllocZ(cLoadedKexts * sizeof(CFDictionaryRef));
                if (pahDictKext)
                {
                    CFDictionaryGetKeysAndValues(hLoadedKexts, NULL, (const void **)pahDictKext);
                    for (uint32_t i = 0; i < cLoadedKexts; i++)
                    {
                        PRTKRNLMODINFOINT pThis = (PRTKRNLMODINFOINT)RTMemAllocZ(sizeof(RTKRNLMODINFOINT));
                        if (RT_LIKELY(pThis))
                        {
                            pThis->cRefs = 1;
                            pThis->hDictKext = pahDictKext[i];
                            CFRetain(pThis->hDictKext);
                            pahKrnlModInfo[i] = pThis;
                        }
                        else
                        {
                            rc = VERR_NO_MEMORY;
                            /* Rollback. */
                            while (i-- > 0)
                            {
                                CFRelease(pahKrnlModInfo[i]->hDictKext);
                                RTMemFree(pahKrnlModInfo[i]);
                            }
                        }
                    }

                    if (   RT_SUCCESS(rc)
                        && pcEntries)
                        *pcEntries = cLoadedKexts;

                    RTMemTmpFree(pahDictKext);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
            {
                rc = VERR_BUFFER_OVERFLOW;

                if (pcEntries)
                    *pcEntries = cLoadedKexts;
            }

            CFRelease(hLoadedKexts);
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}


RTDECL(uint32_t) RTKrnlModInfoRetain(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}


RTDECL(uint32_t) RTKrnlModInfoRelease(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    if (!pThis)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
        rtKrnlModInfoDestroy(pThis);
    return cRefs;
}


RTDECL(uint32_t) RTKrnlModInfoGetRefCnt(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, 0);

    uint32_t cRefCnt = 0;
    CFNumberRef hRetainCnt = (CFNumberRef)CFDictionaryGetValue(pThis->hDictKext,
                                                               kOSBundleRetainCountKey);
    if (hRetainCnt)
        CFNumberGetValue(hRetainCnt, kCFNumberSInt32Type, &cRefCnt);

    return cRefCnt;
}


RTDECL(const char *) RTKrnlModInfoGetName(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, NULL);

    const char *pszName = NULL;
    CFStringRef hBundleId = (CFStringRef)CFDictionaryGetValue(pThis->hDictKext,
                                                              kCFBundleIdentifierKey);
    if (hBundleId)
        pszName = CFStringGetCStringPtr(hBundleId, kCFStringEncodingUTF8);

    return pszName;
}


RTDECL(const char *) RTKrnlModInfoGetFilePath(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, NULL);

    return NULL;
}


RTDECL(size_t) RTKrnlModInfoGetSize(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, 0);

    size_t cbKrnlMod = 0;
    CFNumberRef hKrnlModSize = (CFNumberRef)CFDictionaryGetValue(pThis->hDictKext,
                                                                 kOSBundleLoadSizeKey);
    if (hKrnlModSize)
    {
        uint32_t cbTmp = 0;
        CFNumberGetValue(hKrnlModSize, kCFNumberSInt32Type, &cbTmp);
        cbKrnlMod = cbTmp;
    }

    return cbKrnlMod;
}


RTDECL(RTR0UINTPTR) RTKrnlModInfoGetLoadAddr(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, 0);

    RTR0UINTPTR uKrnlModLoadAddr = 0;
    CFNumberRef hKrnlModLoadAddr = (CFNumberRef)CFDictionaryGetValue(pThis->hDictKext,
                                                                     kOSBundleLoadAddressKey);
    if (hKrnlModLoadAddr)
    {
        uint64_t uAddrTmp = 0;
        CFNumberGetValue(hKrnlModLoadAddr, kCFNumberSInt64Type, &uAddrTmp);
        uKrnlModLoadAddr = uAddrTmp;
    }

    return uKrnlModLoadAddr;
}


RTDECL(int) RTKrnlModInfoQueryRefModInfo(RTKRNLMODINFO hKrnlModInfo, uint32_t idx,
                                         PRTKRNLMODINFO phKrnlModInfoRef)
{
    RT_NOREF3(hKrnlModInfo, idx, phKrnlModInfoRef);
    return VERR_NOT_IMPLEMENTED;
}


RTDECL(int) RTKrnlModLoadByName(const char *pszName)
{
    AssertPtrReturn(pszName, VERR_INVALID_PARAMETER);

    RTOnce(&g_GetIoKitApisOnce, rtKrnlModDarwinResolveIoKitApis, NULL);
    if (!g_pfnKextManagerLoadKextWithIdentifier) return VERR_NOT_SUPPORTED;

    int rc = VINF_SUCCESS;
    CFStringRef hKextName = CFStringCreateWithCString(kCFAllocatorDefault, pszName, kCFStringEncodingUTF8);
    if (hKextName)
    {
        OSReturn rcOsx = g_pfnKextManagerLoadKextWithIdentifier(hKextName, NULL /*dependencyKextAndFolderURLs*/);
        if (rcOsx != kOSReturnSuccess)
            rc = VERR_NOT_SUPPORTED; /** @todo Convert OSReturn values. */
        CFRelease(hKextName);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTKrnlModLoadByPath(const char *pszPath)
{
    AssertPtrReturn(pszPath, VERR_INVALID_PARAMETER);

    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTKrnlModUnloadByName(const char *pszName)
{
    AssertPtrReturn(pszName, VERR_INVALID_PARAMETER);

    RTOnce(&g_GetIoKitApisOnce, rtKrnlModDarwinResolveIoKitApis, NULL);
    if (!g_pfnKextManagerUnloadKextWithIdentifier) return VERR_NOT_SUPPORTED;

    int rc = VINF_SUCCESS;
    CFStringRef hKextName = CFStringCreateWithCString(kCFAllocatorDefault, pszName, kCFStringEncodingUTF8);
    if (hKextName)
    {
        OSReturn rcOsx = g_pfnKextManagerUnloadKextWithIdentifier(hKextName);
        if (rcOsx != kOSReturnSuccess)
            rc = VERR_NOT_SUPPORTED; /** @todo Convert OSReturn values. */
        CFRelease(hKextName);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}
