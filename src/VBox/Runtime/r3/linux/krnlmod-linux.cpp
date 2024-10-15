/* $Id: krnlmod-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <iprt/linux/sysfs.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/types.h>


/**
 * Internal kernel information record state.
 */
typedef struct RTKRNLMODINFOINT
{
    /** Reference counter. */
    volatile uint32_t   cRefs;
    /** Reference count for the kernel module. */
    uint32_t            cRefKrnlMod;
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


static int rtKrnlModLinuxReadIntFileDef(unsigned uBase, int64_t *pi64, int64_t i64Def,
                                        const char *pszName, const char *pszPath)
{
    int rc = RTLinuxSysFsReadIntFile(uBase, pi64, "module/%s/%s", pszName, pszPath);
    if (rc == VERR_FILE_NOT_FOUND)
    {
        *pi64 = i64Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}

/**
 * Creates a new kernel module information record for the given module.
 *
 * @returns IPRT status code.
 * @param   pszName          The kernel module name.
 * @param   phKrnlModInfo    Where to store the handle to the kernel module information record
 *                           on success.
 */
static int rtKrnlModLinuxInfoCreate(const char *pszName, PRTKRNLMODINFO phKrnlModInfo)
{
    int rc = VINF_SUCCESS;
    size_t cchName = strlen(pszName) + 1;
    PRTKRNLMODINFOINT pThis = (PRTKRNLMODINFOINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTKRNLMODINFOINT, achName[cchName]));
    if (RT_LIKELY(pThis))
    {
        memcpy(&pThis->achName[0], pszName, cchName);
        pThis->cchName     = cchName;
        pThis->cRefs       = 1;

        int64_t iTmp = 0;
        rc = rtKrnlModLinuxReadIntFileDef(10, &iTmp, 0, pszName, "refcnt");
        if (RT_SUCCESS(rc))
            pThis->cRefKrnlMod = (uint32_t)iTmp;

        rc = rtKrnlModLinuxReadIntFileDef(10, &iTmp, 0, pszName, "coresize");
        if (RT_SUCCESS(rc))
            pThis->cbKrnlMod = iTmp;

        rc = rtKrnlModLinuxReadIntFileDef(16, &iTmp, 0, pszName, "sections/.text");
        if (RT_SUCCESS(rc))
            pThis->uLoadAddr = iTmp;

        if (RT_SUCCESS(rc))
            *phKrnlModInfo = pThis;
        else
            RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTKrnlModQueryLoaded(const char *pszName, bool *pfLoaded)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pfLoaded, VERR_INVALID_POINTER);

    int rc = RTLinuxSysFsExists("module/%s", pszName);
    if (rc == VINF_SUCCESS)
        *pfLoaded = true;
    else if (rc == VERR_FILE_NOT_FOUND)
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

    int rc = RTLinuxSysFsExists("module/%s", pszName);
    if (rc == VINF_SUCCESS)
        rc = rtKrnlModLinuxInfoCreate(pszName, phKrnlModInfo);
    else if (rc == VERR_FILE_NOT_FOUND)
        rc = VERR_NOT_FOUND;

    return rc;
}


RTDECL(uint32_t) RTKrnlModLoadedGetCount(void)
{
    uint32_t cKmodsLoaded = 0;

    RTDIR hDir = NULL;
    int rc = RTDirOpen(&hDir, "/sys/module");
    if (RT_SUCCESS(rc))
    {
        RTDIRENTRY DirEnt;
        rc = RTDirRead(hDir, &DirEnt, NULL);
        while (RT_SUCCESS(rc))
        {
            if (!RTDirEntryIsStdDotLink(&DirEnt))
                cKmodsLoaded++;
            rc = RTDirRead(hDir, &DirEnt, NULL);
        }

        RTDirClose(hDir);
    }


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

    RTDIR hDir = NULL;
    int rc = RTDirOpen(&hDir, "/sys/module");
    if (RT_SUCCESS(rc))
    {
        unsigned idxKrnlModInfo = 0;
        RTDIRENTRY DirEnt;

        rc = RTDirRead(hDir, &DirEnt, NULL);
        while (RT_SUCCESS(rc))
        {
            if (!RTDirEntryIsStdDotLink(&DirEnt))
            {
                rc = rtKrnlModLinuxInfoCreate(DirEnt.szName, &pahKrnlModInfo[idxKrnlModInfo]);
                if (RT_SUCCESS(rc))
                    idxKrnlModInfo++;
            }

            if (RT_SUCCESS(rc))
                rc = RTDirRead(hDir, &DirEnt, NULL);
        }

        if (rc == VERR_NO_MORE_FILES)
            rc = VINF_SUCCESS;
        else if (RT_FAILURE(rc))
        {
            /* Rollback */
            while (idxKrnlModInfo-- > 0)
                RTKrnlModInfoRelease(pahKrnlModInfo[idxKrnlModInfo]);
        }

        if (*pcEntries)
            *pcEntries = cKmodsLoaded;

        RTDirClose(hDir);
    }

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

    return pThis->cRefKrnlMod;
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
