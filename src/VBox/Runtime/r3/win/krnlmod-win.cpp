/* $Id: krnlmod-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Kernel module, Windows.
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
#include <iprt/nt/nt.h>

#include <iprt/krnlmod.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/errcore.h>
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
    /** Pointer to the driver name. */
    const char          *pszName;
    /** Size of the name in characters including the zero terminator. */
    size_t              cchFilePath;
    /** Module name - variable in size. */
    char                achFilePath[1];
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
 * Queries the complete kernel modules structure and returns a pointer to it.
 *
 * @returns IPRT status code.
 * @param   ppKrnlMods       Where to store the pointer to the kernel module list on success.
 *                           Free with RTMemFree().
 */
static int rtKrnlModWinQueryKrnlMods(PRTL_PROCESS_MODULES *ppKrnlMods)
{
    int rc = VINF_SUCCESS;
    RTL_PROCESS_MODULES KrnlModsSize;

    NTSTATUS rcNt = NtQuerySystemInformation(SystemModuleInformation, &KrnlModsSize, sizeof(KrnlModsSize), NULL);
    if (NT_SUCCESS(rcNt) || rcNt == STATUS_INFO_LENGTH_MISMATCH)
    {
        ULONG cbKrnlMods = RT_UOFFSETOF_DYN(RTL_PROCESS_MODULES, Modules[KrnlModsSize.NumberOfModules]);
        PRTL_PROCESS_MODULES pKrnlMods = (PRTL_PROCESS_MODULES)RTMemAllocZ(cbKrnlMods);
        if (RT_LIKELY(pKrnlMods))
        {
            rcNt = NtQuerySystemInformation(SystemModuleInformation, pKrnlMods, cbKrnlMods, NULL);
            if (NT_SUCCESS(rcNt))
                *ppKrnlMods = pKrnlMods;
            else
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);

    return rc;
}

/**
 * Creates a new kernel module information record for the given module.
 *
 * @returns IPRT status code.
 * @param   pModInfo         The kernel module information.
 * @param   phKrnlModInfo    Where to store the handle to the kernel module information record
 *                           on success.
 */
static int rtKrnlModWinInfoCreate(PRTL_PROCESS_MODULE_INFORMATION pModInfo, PRTKRNLMODINFO phKrnlModInfo)
{
    int rc = VINF_SUCCESS;
    RT_NOREF2(pModInfo, phKrnlModInfo);
    size_t cchFilePath = strlen((const char *)&pModInfo->FullPathName[0]) + 1;
    PRTKRNLMODINFOINT pThis = (PRTKRNLMODINFOINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTKRNLMODINFOINT, achFilePath[cchFilePath]));
    if (RT_LIKELY(pThis))
    {
        memcpy(&pThis->achFilePath[0], &pModInfo->FullPathName[0], cchFilePath);
        pThis->cchFilePath = cchFilePath;
        pThis->cRefs       = 1;
        pThis->cbKrnlMod   = pModInfo->ImageSize;
        pThis->uLoadAddr   = (RTR0UINTPTR)pModInfo->ImageBase;
        pThis->pszName     =   pModInfo->OffsetToFileName >= cchFilePath
                             ? NULL
                             : pThis->achFilePath + pModInfo->OffsetToFileName;

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

    int rc = VERR_NOT_IMPLEMENTED;

    return rc;
}


RTDECL(int) RTKrnlModLoadedQueryInfo(const char *pszName, PRTKRNLMODINFO phKrnlModInfo)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(phKrnlModInfo, VERR_INVALID_POINTER);

    int rc = VERR_NOT_IMPLEMENTED;

    return rc;
}


RTDECL(uint32_t) RTKrnlModLoadedGetCount(void)
{
    uint32_t cKrnlMods = 0;
    RTL_PROCESS_MODULES ProcMods;

    NTSTATUS rcNt = NtQuerySystemInformation(SystemModuleInformation, &ProcMods, sizeof(ProcMods), NULL);
    if (NT_SUCCESS(rcNt) || rcNt == STATUS_INFO_LENGTH_MISMATCH)
        cKrnlMods = ProcMods.NumberOfModules;

    return cKrnlMods;
}


RTDECL(int) RTKrnlModLoadedQueryInfoAll(PRTKRNLMODINFO pahKrnlModInfo, uint32_t cEntriesMax,
                                        uint32_t *pcEntries)
{
    if (cEntriesMax > 0)
        AssertPtrReturn(pahKrnlModInfo, VERR_INVALID_POINTER);

    PRTL_PROCESS_MODULES pKrnlMods = NULL;
    int rc = rtKrnlModWinQueryKrnlMods(&pKrnlMods);
    if (RT_SUCCESS(rc))
    {
        if (pKrnlMods->NumberOfModules <= cEntriesMax)
        {
            for (unsigned i = 0; i < pKrnlMods->NumberOfModules; i++)
            {
                pKrnlMods->Modules[i].FullPathName[255] = '\0'; /* Paranoia */
                rc = rtKrnlModWinInfoCreate(&pKrnlMods->Modules[i], &pahKrnlModInfo[i]);
                if (RT_FAILURE(rc))
                {
                    while (i-- > 0)
                        RTKrnlModInfoRelease(pahKrnlModInfo[i]);
                    break;
                }
            }
        }
        else
            rc = VERR_BUFFER_OVERFLOW;

        if (pcEntries)
            *pcEntries = pKrnlMods->NumberOfModules;

        RTMemFree(pKrnlMods);
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

    return pThis->pszName;
}


RTDECL(const char *) RTKrnlModInfoGetFilePath(RTKRNLMODINFO hKrnlModInfo)
{
    PRTKRNLMODINFOINT pThis = hKrnlModInfo;
    AssertPtrReturn(pThis, NULL);

    return &pThis->achFilePath[0];
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
    return VERR_NOT_SUPPORTED;
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
