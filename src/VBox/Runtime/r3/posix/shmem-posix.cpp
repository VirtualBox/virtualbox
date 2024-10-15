/* $Id: shmem-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Named shared memory object, POSIX Implementation.
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
#include <iprt/shmem.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include "internal/magics.h"

#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

/* Workaround on systems which do not provide this. */
#ifndef NAME_MAX
# define NAME_MAX 255
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
    /** File descriptor for the underlying shared memory object. */
    int                 iFdShm;
    /** Pointer to the shared memory object name. */
    char               *pszName;
    /** Flag whether this instance created the named shared memory object. */
    bool                fCreate;
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

    size_t cchName = strlen(pszName);
    AssertReturn(cchName, VERR_INVALID_PARAMETER);
    AssertReturn(cchName < NAME_MAX - 1, VERR_INVALID_PARAMETER); /* account for the / we add later on. */
    cMappingsHint = cMappingsHint == 0 ? 5 : cMappingsHint;
    int rc = VINF_SUCCESS;
    PRTSHMEMINT pThis = (PRTSHMEMINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTSHMEMINT, aMappingDescs[cMappingsHint]) + cchName + 2); /* '/' + terminator. */
    if (RT_LIKELY(pThis))
    {
        pThis->u32Magic            = RTSHMEM_MAGIC;
        pThis->pszName             = (char *)&pThis->aMappingDescs[cMappingsHint];
        /*pThis->fCreate           = false; */
        /*pThis->cMappings         = 0; */
        pThis->cMappingDescsMax    = cMappingsHint;
        /*pThis->cMappingDescsUsed = 0; */
        pThis->pszName[0]          = '/';
        memcpy(&pThis->pszName[1], pszName, cchName);
        int fShmFlags = 0;
        if (fFlags & RTSHMEM_O_F_CREATE)
        {
            fShmFlags |= O_CREAT;
            pThis->fCreate = true;
        }
        if ((fFlags & RTSHMEM_O_F_CREATE_EXCL) == RTSHMEM_O_F_CREATE_EXCL)
            fShmFlags |= O_EXCL;
        if (   (fFlags & RTSHMEM_O_F_READWRITE) == RTSHMEM_O_F_READWRITE
            || (fFlags & RTSHMEM_O_F_WRITE))
            fShmFlags |= O_RDWR;
        else
            fShmFlags |= O_RDONLY;
        if (fFlags & RTSHMEM_O_F_TRUNCATE)
            fShmFlags |= O_TRUNC;
        pThis->iFdShm = shm_open(pThis->pszName, fShmFlags , 0600);
        if (pThis->iFdShm > 0)
        {
            if (cbMax)
                rc = RTShMemSetSize(pThis, cbMax);
            if (RT_SUCCESS(rc))
            {
                *phShMem = pThis;
                return VINF_SUCCESS;
            }

            close(pThis->iFdShm);
        }
        else
            rc = RTErrConvertFromErrno(errno);

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
    if (!close(pThis->iFdShm))
    {
        if (pThis->fCreate)
            shm_unlink(pThis->pszName); /* Ignore any error here. */
        pThis->u32Magic = RTSHMEM_MAGIC_DEAD;
        RTMemFree(pThis);
    }
    else
        rc = RTErrConvertFromErrno(errno);

    return rc;
}


RTDECL(int) RTShMemDelete(const char *pszName)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);

    size_t cchName = strlen(pszName);
    AssertReturn(cchName, VERR_INVALID_PARAMETER);
    AssertReturn(cchName < NAME_MAX - 1, VERR_INVALID_PARAMETER); /* account for the / we add later on. */
    char *psz = NULL;

    int rc = RTStrAllocEx(&psz, cchName + 2); /* '/' + terminator */
    if (RT_SUCCESS(rc))
    {
        psz[0] = '/';
        memcpy(&psz[1], pszName, cchName + 1);
        if (shm_unlink(psz))
            rc = RTErrConvertFromErrno(errno);
        RTStrFree(psz);
    }

    return rc;
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

    int rc = VINF_SUCCESS;
    if (ftruncate(pThis->iFdShm, (off_t)cbMem))
        rc = RTErrConvertFromErrno(errno);

    return rc;
}


RTDECL(int) RTShMemQuerySize(RTSHMEM hShMem, size_t *pcbMem)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pcbMem, VERR_INVALID_PARAMETER);

    struct stat st;
    if (!fstat(pThis->iFdShm, &st))
    {
        *pcbMem = st.st_size;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTDECL(int) RTShMemMapRegion(RTSHMEM hShMem, size_t offRegion, size_t cbRegion, uint32_t fFlags, void **ppv)
{
    PRTSHMEMINT pThis = hShMem;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSHMEM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(ppv, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTSHMEM_MAP_F_VALID_MASK), VERR_INVALID_PARAMETER);

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
            int fMmapFlags = 0;
            int fProt = 0;
            if (fFlags & RTSHMEM_MAP_F_READ)
                fProt |= PROT_READ;
            if (fFlags & RTSHMEM_MAP_F_WRITE)
                fProt |= PROT_WRITE;
            if (fFlags & RTSHMEM_MAP_F_EXEC)
                fProt |= PROT_EXEC;
            if (fFlags & RTSHMEM_MAP_F_COW)
                fMmapFlags |= MAP_PRIVATE;
            else
                fMmapFlags |= MAP_SHARED;

            void *pv = mmap(NULL, cbRegion, fProt, fMmapFlags, pThis->iFdShm, (off_t)offRegion);
            if (pv != MAP_FAILED)
            {
                pMappingDesc->pvMapping = pv;
                pMappingDesc->offRegion = offRegion;
                pMappingDesc->cbRegion  = cbRegion;
                pMappingDesc->fFlags    = fFlags;
            }
            else
            {
                rc = RTErrConvertFromErrno(errno);
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
    size_t cbRegion = pMappingDesc->cMappings;
    if (!ASMAtomicDecU32(&pMappingDesc->cMappings))
    {
        /* Last mapping of this region was unmapped, so do the real unmapping now. */
        if (munmap(pv, cbRegion))
        {
            ASMAtomicIncU32(&pMappingDesc->cMappings);
            rc = RTErrConvertFromErrno(errno);
        }
        else
        {
            ASMAtomicDecU32(&pThis->cMappingDescsUsed);
            ASMAtomicDecU32(&pThis->cMappings);
        }
    }

    return rc;
}

