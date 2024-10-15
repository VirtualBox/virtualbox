/* $Id: krnlmod-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Kernel module, Linux.
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
#include <iprt/dir.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/types.h>

#include <iprt/stream.h>

#include <sys/modctl.h>
#include <errno.h>

/**
 * Internal kernel information record state.
 */
typedef struct RTKRNLMODINFOINT
{
    /** Reference counter. */
    volatile uint32_t   cRefs;
    /** Load address of the kernel module. */
    RTR0UINTPTR         uLoadAddr;
    /** Size of the kernel module. */
    size_t              cbKrnlMod;
    /** Size of the name in characters including the zero terminator. */
    size_t              cchName;
    /** Module name - variable in size. */
    char                achName[1];
} RTKRNLMODINFOINT;
/** Pointer to the internal kernel module information record. */
typedef RTKRNLMODINFOINT *PRTKRNLMODINFOINT;
/** Pointer to a const internal kernel module information record. */
typedef const RTKRNLMODINFOINT *PCRTKRNLMODINFOINT;



/**
 * Destroy the given kernel module information record.
 *
 * @param   pThis            The record to destroy.
 */
static void rtKrnlModInfoDestroy(PRTKRNLMODINFOINT pThis)
{
    RTMemFree(pThis);
}


/**
 * Creates a new kernel module information record for the given module.
 *
 * @returns IPRT status code.
 * @param   pModInfo         The Solaris kernel module information.
 * @param   phKrnlModInfo    Where to store the handle to the kernel module information record
 *                           on success.
 */
static int rtKrnlModSolInfoCreate(struct modinfo *pModInfo, PRTKRNLMODINFO phKrnlModInfo)
{
    int rc = VINF_SUCCESS;
    size_t cchName = strlen(&pModInfo->mi_name[0]) + 1;
    PRTKRNLMODINFOINT pThis = (PRTKRNLMODINFOINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTKRNLMODINFOINT, achName[cchName]));
    if (RT_LIKELY(pThis))
    {
        memcpy(&pThis->achName[0], &pModInfo->mi_name[0], cchName);
        pThis->cchName     = cchName;
        pThis->cRefs       = 1;
        pThis->cbKrnlMod   = pModInfo->mi_size;
        pThis->uLoadAddr   = (RTR0UINTPTR)pModInfo->mi_base;

        *phKrnlModInfo = pThis;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTKrnlModQueryLoaded(const char *pszName, bool *pfLoaded)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pfLoaded, VERR_INVALID_POINTER);

    RTKRNLMODINFO hKrnlModInfo = NIL_RTKRNLMODINFO;
    int rc = RTKrnlModLoadedQueryInfo(pszName, &hKrnlModInfo);
    if (RT_SUCCESS(rc))
    {
        *pfLoaded = true;
        RTKrnlModInfoRelease(hKrnlModInfo);
    }
    else if (rc == VERR_NOT_FOUND)
    {
        *pfLoaded = false;
        rc = VINF_SUCCESS;
    }

    return rc;
}


RTDECL(int) RTKrnlModLoadedQueryInfo(const char *pszName, PRTKRNLMODINFO phKrnlModInfo)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(phKrnlModInfo, VERR_INVALID_POINTER);

    int rc = VERR_NOT_FOUND;
    int iId = -1;
    struct modinfo ModInfo;

    ModInfo.mi_info   = MI_INFO_ALL | MI_INFO_CNT;
    ModInfo.mi_id     = iId;
    ModInfo.mi_nextid = iId;
    do
    {
        int rcSol = modctl(MODINFO, iId, &ModInfo);
        if (rcSol < 0)
        {
            rc = RTErrConvertFromErrno(errno);
            break;
        }

        if (ModInfo.mi_id != -1)
        {
            ModInfo.mi_name[MODMAXNAMELEN - 1] = '\0'; /* Paranoia. */
            if (!RTStrCmp(pszName, &ModInfo.mi_name[0]))
            {
                rc = rtKrnlModSolInfoCreate(&ModInfo, phKrnlModInfo);
                break;
            }
        }

        iId = ModInfo.mi_id;
    } while (iId != -1);

    return rc;
}


RTDECL(uint32_t) RTKrnlModLoadedGetCount(void)
{
    uint32_t cKmodsLoaded = 0;
    int iId = -1;
    struct modinfo ModInfo;

    ModInfo.mi_info   = MI_INFO_ALL | MI_INFO_CNT;
    ModInfo.mi_id     = iId;
    ModInfo.mi_nextid = iId;
    do
    {
        int rcSol = modctl(MODINFO, iId, &ModInfo);
        if (rcSol < 0)
            break;

        cKmodsLoaded++;

        iId = ModInfo.mi_id;
    } while (iId != -1);

    return cKmodsLoaded;
}


RTDECL(int) RTKrnlModLoadedQueryInfoAll(PRTKRNLMODINFO pahKrnlModInfo, uint32_t cEntriesMax,
                                        uint32_t *pcEntries)
{
    if (cEntriesMax > 0)
        AssertPtrReturn(pahKrnlModInfo, VERR_INVALID_POINTER);

    uint32_t cKmodsLoaded = RTKrnlModLoadedGetCount();
    if (cEntriesMax < cKmodsLoaded)
    {
        if (*pcEntries)
            *pcEntries = cKmodsLoaded;
        return VERR_BUFFER_OVERFLOW;
    }

    int rc = VINF_SUCCESS;
    int iId = -1;
    unsigned idxKrnlModInfo = 0;
    struct modinfo ModInfo;

    ModInfo.mi_info   = MI_INFO_ALL | MI_INFO_CNT;
    ModInfo.mi_id     = iId;
    ModInfo.mi_nextid = iId;
    do
    {
        int rcSol = modctl(MODINFO, iId, &ModInfo);
        if (rcSol < 0)
        {
            rc = RTErrConvertFromErrno(errno);
            if (rc == VERR_INVALID_PARAMETER && idxKrnlModInfo > 0)
                rc = VINF_SUCCESS;
            break;
        }

        ModInfo.mi_name[MODMAXNAMELEN - 1] = '\0'; /* Paranoia. */
        rc = rtKrnlModSolInfoCreate(&ModInfo, &pahKrnlModInfo[idxKrnlModInfo]);
        if (RT_SUCCESS(rc))
            idxKrnlModInfo++;

        iId = ModInfo.mi_id;
    } while (iId != -1);

    if (RT_FAILURE(rc))
    {
        /* Rollback */
        while (idxKrnlModInfo-- > 0)
            RTKrnlModInfoRelease(pahKrnlModInfo[idxKrnlModInfo]);
    }
    else if (pcEntries)
        *pcEntries = idxKrnlModInfo;

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

    return 0;
}


RTDECL(const char *) RTKrnlModInfoGetName(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, NULL);

    return &pThis->achName[0];
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

    return pThis->cbKrnlMod;
}


RTDECL(RTR0UINTPTR) RTKrnlModInfoGetLoadAddr(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, 0);

    return pThis->uLoadAddr;
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

    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTKrnlModLoadByPath(const char *pszPath)
{
    AssertPtrReturn(pszPath, VERR_INVALID_PARAMETER);

    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTKrnlModUnloadByName(const char *pszName)
{
    AssertPtrReturn(pszName, VERR_INVALID_PARAMETER);

    return VERR_NOT_SUPPORTED;
}
