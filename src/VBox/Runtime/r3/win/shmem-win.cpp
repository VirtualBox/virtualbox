/* $Id: shmem-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Named shared memory object, Windows Implementation.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/nt/nt-and-windows.h>

#include <iprt/shmem.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include "internal/magics.h"
#include "internal/path.h"
#include "internal-r3-win.h" /* For g_enmWinVer + kRTWinOSType_XXX */

/*
 * Define values ourselves in case the compiling host is too old.
 * See https://docs.microsoft.com/en-us/windows/desktop/api/winbase/nf-winbase-createfilemappinga
 * for when these were introduced.
 */
#ifndef PAGE_EXECUTE_READ
# define PAGE_EXECUTE_READ 0x20
#endif
#ifndef PAGE_EXECUTE_READWRITE
# define PAGE_EXECUTE_READWRITE 0x40
#endif
#ifndef PAGE_EXECUTE_WRITECOPY
# define PAGE_EXECUTE_WRITECOPY 0x80
#endif
#ifndef FILE_MAP_EXECUTE
# define FILE_MAP_EXECUTE 0x20
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Shared memory object mapping descriptor.
 */
typedef struct RTSHMEMMAPPINGDESC
{
    /** Number of references held to this mapping, 0 if the descriptor is free. */
    volatile uint32_t   cMappings;
    /** Pointer to the region mapping. */
    void               *pvMapping;
    /** Start offset */
    size_t              offRegion;
    /** Size of the region. */
    size_t              cbRegion;
    /** Access flags for this region .*/
    uint32_t            fFlags;
} RTSHMEMMAPPINGDESC;
/** Pointer to a shared memory object mapping descriptor. */
typedef RTSHMEMMAPPINGDESC *PRTSHMEMMAPPINGDESC;
/** Pointer to a constant shared memory object mapping descriptor. */
typedef const RTSHMEMMAPPINGDESC *PCRTSHMEMMAPPINGDESC;


/**
 * Internal shared memory object state.
 */
typedef struct RTSHMEMINT
{
    /** Magic value (RTSHMEM_MAGIC). */
    uint32_t            u32Magic;
    /** Flag whether this instance created the named shared memory object. */
    bool                fCreate;
    /** Handle to the underlying mapping object. */
    HANDLE              hShmObj;
    /** Size of the mapping object in bytes. */
    size_t              cbMax;
    /** Overall number of mappings active for this shared memory object. */
    volatile uint32_t   cMappings;
    /** Maximum number of mapping descriptors allocated. */
    uint32_t            cMappingDescsMax;
    /** Number of mapping descriptors used. */
    volatile uint32_t   cMappingDescsUsed;
    /** Array of mapping descriptors - variable in size. */
    RTSHMEMMAPPINGDESC  aMappingDescs[1];
} RTSHMEMINT;
/** Pointer to the internal shared memory object state. */
typedef RTSHMEMINT *PRTSHMEMINT;




/**
 * Returns a mapping descriptor matching the given region properties or NULL if none was found.
 *
 * @returns Pointer to the matching mapping descriptor or NULL if not found.
 * @param   pThis           Pointer to the shared memory object instance.
 * @param   offRegion       Offset into the shared memory object to start mapping at.
 * @param   cbRegion        Size of the region to map.
 * @param   fFlags          Desired properties of the mapped region, combination of RTSHMEM_MAP_F_* defines.
 */
DECLINLINE(PRTSHMEMMAPPINGDESC) rtShMemMappingDescFindByProp(PRTSHMEMINT pThis, size_t offRegion, size_t cbRegion, uint32_t fFlags)
{
    for (uint32_t i = 0; i < pThis->cMappingDescsMax; i++)
    {
        if (   pThis->aMappingDescs[i].offRegion == offRegion
            && pThis->aMappingDescs[i].cbRegion == cbRegion
            && pThis->aMappingDescs[i].fFlags == fFlags)
            return &pThis->aMappingDescs[i];
    }

    return NULL;
}


RTDECL(int) RTShMemOpen(PRTSHMEM phShMem, const char *pszName, uint32_t fFlags, size_t cbMax, uint32_t cMappingsHint)
{
    AssertPtrReturn(phShMem, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszName, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTSHMEM_O_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(cMappingsHint < 64, VERR_OUT_OF_RANGE);
    AssertReturn(cbMax > 0 || !(fFlags & RTSHMEM_O_F_CREATE), VERR_NOT_SUPPORTED);

    if (fFlags & RTSHMEM_O_F_TRUNCATE)
        return VERR_NOT_SUPPORTED;

    /*
     * The executable access was introduced with Windows XP SP2 and Windows Server 2003 SP1,
     * PAGE_EXECUTE_WRITECOPY was not available until Windows Vista SP1.
     * Allow execute mappings only starting from Windows 7 to keep the checks simple here (lazy coder).
     */
    if (   (fFlags & RTSHMEM_O_F_MAYBE_EXEC)
        && g_enmWinVer < kRTWinOSType_7)
        return VERR_NOT_SUPPORTED;

    cMappingsHint = cMappingsHint == 0 ? 5 : cMappingsHint;
    int rc = VINF_SUCCESS;
    PRTSHMEMINT pThis = (PRTSHMEMINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTSHMEMINT, aMappingDescs[cMappingsHint]));
    if (RT_LIKELY(pThis))
    {
        pThis->u32Magic            = RTSHMEM_MAGIC;
        /*pThis->fCreate           = false; */
        /*pThis->cMappings         = 0; */
        pThis->cMappingDescsMax    = cMappingsHint;
        /*pThis->cMappingDescsUsed = 0; */
        /* Construct the filename, always use the local namespace, global requires special privileges. */
        char szName[RTPATH_MAX];
        ssize_t cch = RTStrPrintf2(&szName[0], sizeof(szName), "Local\\%s", pszName);
        if (cch > 0)
        {
            PRTUTF16 pwszName = NULL;
            rc = RTStrToUtf16Ex(&szName[0], RTSTR_MAX, &pwszName, 0, NULL);
            if (RT_SUCCESS(rc))
            {
                if (fFlags & RTSHMEM_O_F_CREATE)
                {
#if HC_ARCH_BITS == 64
                    DWORD dwSzMaxHigh = cbMax >> 32;
#elif HC_ARCH_BITS == 32
                    DWORD dwSzMaxHigh = 0;
#else
# error "Port me"
#endif
                    DWORD dwSzMaxLow = cbMax & UINT32_C(0xffffffff);
                    DWORD fProt = 0;

                    if (fFlags & RTSHMEM_O_F_MAYBE_EXEC)
                    {
                        if ((fFlags & RTSHMEM_O_F_READWRITE) == RTSHMEM_O_F_READ)
                            fProt |= PAGE_EXECUTE_READ;
                        else
                            fProt |= PAGE_EXECUTE_READWRITE;
                    }
                    else
                    {
                        if ((fFlags & RTSHMEM_O_F_READWRITE) == RTSHMEM_O_F_READ)
                            fProt |= PAGE_READONLY;
                        else
                            fProt |= PAGE_READWRITE;
                    }
                    pThis->hShmObj = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, fProt,
                                                        dwSzMaxHigh, dwSzMaxLow, pwszName);
                }
                else
                {
                    DWORD fProt = SECTION_QUERY;
                    if (fFlags & RTSHMEM_O_F_MAYBE_EXEC)
                        fProt |= FILE_MAP_EXECUTE;
                    if (fFlags & RTSHMEM_O_F_READ)
                        fProt |= FILE_MAP_READ;
                    if (fFlags & RTSHMEM_O_F_WRITE)
                        fProt |= FILE_MAP_WRITE;

                    pThis->hShmObj = OpenFileMappingW(fProt, FALSE, pwszName);
                }
                RTUtf16Free(pwszName);
                if (pThis->hShmObj != NULL)
                {
                    *phShMem = pThis;
                    return VINF_SUCCESS;
                }
                else
                    rc = RTErrConvertFromWin32(GetLastError());
            }
        }
        else
            rc = VERR_BUFFER_OVERFLOW;

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTShMemClose(RTSHMEM hShMem)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->cMappings, VERR_INVALID_STATE);

    int rc = VINF_SUCCESS;
    if (CloseHandle(pThis->hShmObj))
    {
        pThis->u32Magic = RTSHMEM_MAGIC_DEAD;
        RTMemFree(pThis);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());

    return rc;
}


RTDECL(int) RTShMemDelete(const char *pszName)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(*pszName != '\0', VERR_INVALID_PARAMETER);

    return VERR_NOT_SUPPORTED;
}


RTDECL(uint32_t) RTShMemRefCount(RTSHMEM hShMem)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, 0);

    return pThis->cMappings;
}


RTDECL(int) RTShMemSetSize(RTSHMEM hShMem, size_t cbMem)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->cMappings, VERR_INVALID_STATE);
    AssertReturn(cbMem, VERR_NOT_SUPPORTED);

    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTShMemQuerySize(RTSHMEM hShMem, size_t *pcbMem)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pcbMem, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    SECTION_BASIC_INFORMATION SecInf;
    SIZE_T cbRet;
    NTSTATUS rcNt = NtQuerySection(pThis->hShmObj, SectionBasicInformation, &SecInf, sizeof(SecInf), &cbRet);
    if (NT_SUCCESS(rcNt))
    {
        AssertReturn(cbRet == sizeof(SecInf), VERR_INTERNAL_ERROR);
#if HC_ARCH_BITS == 32
        AssertReturn(SecInf.MaximumSize.HighPart == 0, VERR_INTERNAL_ERROR_2);
        *pcbMem = SecInf.MaximumSize.LowPart;
#elif HC_ARCH_BITS == 64
        *pcbMem = SecInf.MaximumSize.QuadPart;
#else
# error "Port me"
#endif
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);

    return rc;
}


RTDECL(int) RTShMemMapRegion(RTSHMEM hShMem, size_t offRegion, size_t cbRegion, uint32_t fFlags, void **ppv)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(ppv, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTSHMEM_MAP_F_VALID_MASK), VERR_INVALID_PARAMETER);

    /* See comment in RTShMemOpen(). */
    if (   (fFlags & RTSHMEM_MAP_F_EXEC)
        && g_enmWinVer < kRTWinOSType_7)
        return VERR_NOT_SUPPORTED;

    /* Try to find a mapping with compatible parameters first. */
    PRTSHMEMMAPPINGDESC pMappingDesc = NULL;
    for (uint32_t iTry = 0; iTry < 10; iTry++)
    {
        pMappingDesc = rtShMemMappingDescFindByProp(pThis, offRegion, cbRegion, fFlags);
        if (!pMappingDesc)
            break;

        /* Increase the mapping count and check that the region is still accessible by us. */
        if (   ASMAtomicIncU32(&pMappingDesc->cMappings) > 1
            && pMappingDesc->offRegion == offRegion
            && pMappingDesc->cbRegion  == cbRegion
            && pMappingDesc->fFlags    == fFlags)
            break;
        /* Mapping was freed inbetween, next round. */
    }

    int rc = VINF_SUCCESS;
    if (!pMappingDesc)
    {
        /* Find an empty region descriptor and map the region. */
        for (uint32_t i = 0; i < pThis->cMappingDescsMax && !pMappingDesc; i++)
        {
            if (!pThis->aMappingDescs[i].cMappings)
            {
                pMappingDesc = &pThis->aMappingDescs[i];

                /* Try to grab this one. */
                if (ASMAtomicIncU32(&pMappingDesc->cMappings) == 1)
                    break;

                /* Somebody raced us, drop reference and continue. */
                ASMAtomicDecU32(&pMappingDesc->cMappings);
                pMappingDesc = NULL;
            }
        }

        if (RT_LIKELY(pMappingDesc))
        {
            /* Try to map it. */
            DWORD fProt = 0;
            DWORD offLow = offRegion & UINT32_C(0xffffffff);
#if HC_ARCH_BITS == 64
            DWORD offHigh = offRegion >> 32;
#elif HC_ARCH_BITS == 32
            DWORD offHigh = 0;
#else
# error "Port me"
#endif
            if (fFlags & RTSHMEM_MAP_F_READ)
                fProt |= FILE_MAP_READ;
            if (fFlags & RTSHMEM_MAP_F_WRITE)
                fProt |= FILE_MAP_WRITE;
            if (fFlags & RTSHMEM_MAP_F_EXEC)
                fProt |= FILE_MAP_EXECUTE;
            if (fFlags & RTSHMEM_MAP_F_COW)
                fProt |= FILE_MAP_COPY;

            void *pv = MapViewOfFile(pThis->hShmObj, fProt, offHigh, offLow, cbRegion);
            if (pv != NULL)
            {
                pMappingDesc->pvMapping = pv;
                pMappingDesc->offRegion = offRegion;
                pMappingDesc->cbRegion  = cbRegion;
                pMappingDesc->fFlags    = fFlags;
            }
            else
            {
                rc = RTErrConvertFromWin32(GetLastError());
                ASMAtomicDecU32(&pMappingDesc->cMappings);
            }
        }
        else
            rc = VERR_SHMEM_MAXIMUM_MAPPINGS_REACHED;
    }

    if (RT_SUCCESS(rc))
    {
        *ppv = pMappingDesc->pvMapping;
        ASMAtomicIncU32(&pThis->cMappings);
    }

    return rc;
}


RTDECL(int) RTShMemUnmapRegion(RTSHMEM hShMem, void *pv)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pv, VERR_INVALID_PARAMETER);

    /* Find the mapping descriptor by the given region address. */
    PRTSHMEMMAPPINGDESC pMappingDesc = NULL;
    for (uint32_t i = 0; i < pThis->cMappingDescsMax && !pMappingDesc; i++)
    {
        if (pThis->aMappingDescs[i].pvMapping == pv)
        {
            pMappingDesc = &pThis->aMappingDescs[i];
            break;
        }
    }

    AssertPtrReturn(pMappingDesc, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    if (!ASMAtomicDecU32(&pMappingDesc->cMappings))
    {
        /* Last mapping of this region was unmapped, so do the real unmapping now. */
        if (UnmapViewOfFile(pv))
        {
            ASMAtomicDecU32(&pThis->cMappingDescsUsed);
            ASMAtomicDecU32(&pThis->cMappings);
        }
        else
        {
            ASMAtomicIncU32(&pMappingDesc->cMappings);
            rc = RTErrConvertFromWin32(GetLastError());
        }
    }

    return rc;
}

