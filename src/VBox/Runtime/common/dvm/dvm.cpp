/* $Id: dvm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Disk Volume Management API (DVM) - generic code.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/dvm.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/list.h>
#include "internal/dvm.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * The internal volume manager structure.
 */
typedef struct RTDVMINTERNAL
{
    /** The DVM magic (RTDVM_MAGIC). */
    uint32_t          u32Magic;
    /** The disk descriptor. */
    RTDVMDISK         DvmDisk;
    /** Pointer to the backend operations table after a successful probe. */
    PCRTDVMFMTOPS     pDvmFmtOps;
    /** The format specific volume manager data. */
    RTDVMFMT          hVolMgrFmt;
    /** Flags passed on manager creation. */
    uint32_t          fFlags;
    /** Reference counter. */
    uint32_t volatile cRefs;
    /** List of recognised volumes (RTDVMVOLUMEINTERNAL). */
    RTLISTANCHOR      VolumeList;
} RTDVMINTERNAL;
/** Pointer to an internal volume manager. */
typedef RTDVMINTERNAL *PRTDVMINTERNAL;

/**
 * The internal volume structure.
 */
typedef struct RTDVMVOLUMEINTERNAL
{
    /** The DVM volume magic (RTDVMVOLUME_MAGIC). */
    uint32_t                      u32Magic;
    /** Node for the volume list. */
    RTLISTNODE                    VolumeNode;
    /** Pointer to the owning volume manager. */
    PRTDVMINTERNAL                pVolMgr;
    /** Format specific volume data. */
    RTDVMVOLUMEFMT                hVolFmt;
    /** Set block status.callback */
    PFNDVMVOLUMEQUERYBLOCKSTATUS  pfnQueryBlockStatus;
    /** Opaque user data. */
    void                         *pvUser;
    /** Reference counter. */
    uint32_t volatile             cRefs;
} RTDVMVOLUMEINTERNAL;
/** Pointer to an internal volume. */
typedef RTDVMVOLUMEINTERNAL *PRTDVMVOLUMEINTERNAL;


/*********************************************************************************************************************************
*   Global variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Supported volume formats.
 */
static PCRTDVMFMTOPS const g_aDvmFmts[] =
{
    &g_rtDvmFmtMbr,
    &g_rtDvmFmtGpt,
    &g_rtDvmFmtBsdLbl
};

/**
 * Descriptions of the volume types.
 *
 * This is indexed by RTDVMVOLTYPE.
 */
static const char * const g_apszDvmVolTypes[] =
{
    "Invalid",
    "Unknown",
    "NTFS",
    "FAT12",
    "FAT16",
    "FAT32",

    "EFI system partition",

    "Mac OS X HFS or HFS+",
    "Mac OS X APFS",

    "Linux swap",
    "Linux native",
    "Linux LVM",
    "Linux SoftRaid",

    "FreeBSD",
    "NetBSD",
    "OpenBSD",
    "Solaris",

    "Basic data partition",
    "Microsoft reserved partition",
    "Windows LDM metadata",
    "Windows LDM data",
    "Windows recovery partition",
    "Windows storage spaces",

    "IBM GPFS",

    "OS/2",
};
AssertCompile(RT_ELEMENTS(g_apszDvmVolTypes) == RTDVMVOLTYPE_END);


/**
 * Read from the disk at the given offset, neither the offset nor the size is
 * necessary sector aligned.
 *
 * @returns IPRT status code.
 * @param   pDisk    The disk descriptor to read from.
 * @param   off      Start offset.
 * @param   pvBuf    Destination buffer.
 * @param   cbRead   How much to read.
 */
DECLHIDDEN(int) rtDvmDiskReadUnaligned(PCRTDVMDISK pDisk, uint64_t off, void *pvBuf, size_t cbRead)
{
    size_t const cbSector = (size_t)pDisk->cbSector;
    size_t const offDelta = off    % cbSector;
    size_t const cbDelta  = cbRead % cbSector;
    if (!cbDelta && !offDelta)
        return rtDvmDiskRead(pDisk, off, pvBuf, cbRead);

    int rc;
    size_t cbExtra = offDelta + (cbDelta ? cbSector - cbDelta: 0);
    uint8_t *pbTmpBuf = (uint8_t *)RTMemTmpAlloc(cbRead + cbExtra);
    if (pbTmpBuf)
    {
        rc = rtDvmDiskRead(pDisk, off - offDelta, pbTmpBuf, cbRead + cbExtra);
        if (RT_SUCCESS(rc))
            memcpy(pvBuf, &pbTmpBuf[offDelta], cbRead);
        else
            RT_BZERO(pvBuf, cbRead);
        RTMemTmpFree(pbTmpBuf);
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    return rc;
}


/**
 * Creates a new volume.
 *
 * @returns IPRT status code.
 * @param   pThis    The DVM map instance.
 * @param   hVolFmt  The format specific volume handle.
 * @param   phVol    Where to store the generic volume handle on success.
 */
static int rtDvmVolumeCreate(PRTDVMINTERNAL pThis, RTDVMVOLUMEFMT hVolFmt, PRTDVMVOLUME phVol)
{
    PRTDVMVOLUMEINTERNAL pVol = (PRTDVMVOLUMEINTERNAL)RTMemAllocZ(sizeof(RTDVMVOLUMEINTERNAL));
    if (pVol)
    {
        pVol->u32Magic = RTDVMVOLUME_MAGIC;
        pVol->cRefs    = 0;
        pVol->pVolMgr  = pThis;
        pVol->hVolFmt  = hVolFmt;

        *phVol = pVol;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}

/**
 * Destroys a volume handle.
 *
 * @param   pThis   The volume manager instance.
 * @param   pVol    The volume to destroy.
 */
static void rtDvmVolumeDestroy(PRTDVMINTERNAL pThis, PRTDVMVOLUMEINTERNAL pVol)
{
    AssertPtr(pThis);
    AssertPtr(pThis->pDvmFmtOps);
    Assert(pVol->pVolMgr == pThis);

    /* Close the volume. */
    pThis->pDvmFmtOps->pfnVolumeClose(pVol->hVolFmt);

    pVol->u32Magic = RTDVMVOLUME_MAGIC_DEAD;
    pVol->pVolMgr  = NULL;
    pVol->hVolFmt  = NIL_RTDVMVOLUMEFMT;
    RTMemFree(pVol);
}


RTDECL(int) RTDvmCreate(PRTDVM phVolMgr, RTVFSFILE hVfsFile, uint32_t cbSector, uint32_t fFlags)
{
    AssertMsgReturn(!(fFlags & ~DVM_FLAGS_VALID_MASK), ("Invalid flags given %#x\n", fFlags), VERR_INVALID_FLAGS);
    uint32_t cRefs = RTVfsFileRetain(hVfsFile);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    uint64_t cbDisk;
    int rc = RTVfsFileQuerySize(hVfsFile, &cbDisk);
    if (RT_SUCCESS(rc))
    {
        PRTDVMINTERNAL pThis = (PRTDVMINTERNAL)RTMemAllocZ(sizeof(RTDVMINTERNAL));
        if (pThis)
        {
            pThis->u32Magic         = RTDVM_MAGIC;
            pThis->DvmDisk.cbDisk   = cbDisk;
            pThis->DvmDisk.cbSector = cbSector;
            pThis->DvmDisk.hVfsFile = hVfsFile;

            pThis->pDvmFmtOps       = NULL;
            pThis->hVolMgrFmt       = NIL_RTDVMFMT;
            pThis->fFlags           = fFlags;
            pThis->cRefs            = 1;
            RTListInit(&pThis->VolumeList);

            *phVolMgr = pThis;
            return VINF_SUCCESS;
        }
        rc = VERR_NO_MEMORY;
    }
    RTVfsFileRelease(hVfsFile);
    return rc;
}


RTDECL(uint32_t) RTDvmRetain(RTDVM hVolMgr)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}

/**
 * Destroys a volume manager handle.
 *
 * @param   pThis               The volume manager to destroy.
 */
static void rtDvmDestroy(PRTDVMINTERNAL pThis)
{
    pThis->u32Magic = RTDVM_MAGIC_DEAD;

    if (pThis->hVolMgrFmt != NIL_RTDVMFMT)
    {
        AssertPtr(pThis->pDvmFmtOps);

        /* */
        PRTDVMVOLUMEINTERNAL pItNext, pIt;
        RTListForEachSafe(&pThis->VolumeList, pIt, pItNext, RTDVMVOLUMEINTERNAL, VolumeNode)
        {
            RTListNodeRemove(&pIt->VolumeNode);
            rtDvmVolumeDestroy(pThis, pIt);
        }

        /* Let the backend do it's own cleanup first. */
        pThis->pDvmFmtOps->pfnClose(pThis->hVolMgrFmt);
        pThis->hVolMgrFmt = NIL_RTDVMFMT;
        pThis->pDvmFmtOps = NULL;
    }

    pThis->DvmDisk.cbDisk = 0;
    pThis->DvmDisk.cbSector = 0;
    if (pThis->DvmDisk.hVfsFile != NIL_RTVFSFILE)
    {
        RTVfsFileRelease(pThis->DvmDisk.hVfsFile);
        pThis->DvmDisk.hVfsFile = NIL_RTVFSFILE;
    }

    RTMemFree(pThis);
}

RTDECL(uint32_t) RTDvmRelease(RTDVM hVolMgr)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    if (pThis == NIL_RTDVM)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
        rtDvmDestroy(pThis);
    return cRefs;
}

RTDECL(int) RTDvmMapOpen(RTDVM hVolMgr)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->hVolMgrFmt == NIL_RTDVMFMT, VERR_WRONG_ORDER);

    Assert(!pThis->pDvmFmtOps);

    /*
     * Let each format backend have a go at the disk, pick the one which scores the highest.
     */
    int            rc              = VINF_SUCCESS;
    uint32_t       uScoreMax       = RTDVM_MATCH_SCORE_UNSUPPORTED;
    PCRTDVMFMTOPS  pDvmFmtOpsMatch = NULL;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aDvmFmts); i++)
    {
        uint32_t uScore = 0;
        PCRTDVMFMTOPS pDvmFmtOps = g_aDvmFmts[i];

        rc = pDvmFmtOps->pfnProbe(&pThis->DvmDisk, &uScore);
        if (RT_SUCCESS(rc))
        {
            if (uScore > uScoreMax)
            {
                pDvmFmtOpsMatch = pDvmFmtOps;
                uScoreMax       = uScore;
            }
        }
        else
            return rc;
    }
    if (uScoreMax > RTDVM_MATCH_SCORE_UNSUPPORTED)
    {
        AssertPtr(pDvmFmtOpsMatch);

        /*
         * Open the format.
         */
        rc = pDvmFmtOpsMatch->pfnOpen(&pThis->DvmDisk, &pThis->hVolMgrFmt);
        if (RT_SUCCESS(rc))
        {
            pThis->pDvmFmtOps = pDvmFmtOpsMatch;

            /*
             * Construct volume list (we're done if none).
             */
            uint32_t cVols = pThis->pDvmFmtOps->pfnGetValidVolumes(pThis->hVolMgrFmt);
            if (cVols == 0)
                return VINF_SUCCESS;

            /* First volume. */
            RTDVMVOLUMEFMT hVolFmt = NIL_RTDVMVOLUMEFMT;
            rc = pThis->pDvmFmtOps->pfnQueryFirstVolume(pThis->hVolMgrFmt, &hVolFmt);
            if (RT_SUCCESS(rc))
            {
                for (;;)
                {
                    PRTDVMVOLUMEINTERNAL pVol = NULL;
                    rc = rtDvmVolumeCreate(pThis, hVolFmt, &pVol);
                    if (RT_FAILURE(rc))
                    {
                        pThis->pDvmFmtOps->pfnVolumeClose(hVolFmt);
                        break;
                    }
                    RTListAppend(&pThis->VolumeList, &pVol->VolumeNode);

                    /* Done?*/
                    cVols--;
                    if (cVols < 1)
                        return VINF_SUCCESS;

                    /* Next volume. */
                    rc = pThis->pDvmFmtOps->pfnQueryNextVolume(pThis->hVolMgrFmt, pVol->hVolFmt, &hVolFmt);
                    if (RT_FAILURE(rc))
                        break;
                }

                /* Bail out. */
                PRTDVMVOLUMEINTERNAL pItNext, pIt;
                RTListForEachSafe(&pThis->VolumeList, pIt, pItNext, RTDVMVOLUMEINTERNAL, VolumeNode)
                {
                    RTListNodeRemove(&pIt->VolumeNode);
                    rtDvmVolumeDestroy(pThis, pIt);
                }
            }

            pDvmFmtOpsMatch->pfnClose(pThis->hVolMgrFmt);
        }
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}

RTDECL(int) RTDvmMapInitialize(RTDVM hVolMgr, const char *pszFmt)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszFmt, VERR_INVALID_POINTER);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->hVolMgrFmt == NIL_RTDVMFMT, VERR_WRONG_ORDER);

    for (unsigned i = 0; i < RT_ELEMENTS(g_aDvmFmts); i++)
    {
        PCRTDVMFMTOPS pDvmFmtOps = g_aDvmFmts[i];
        if (!RTStrCmp(pDvmFmtOps->pszFmt, pszFmt))
        {
            int rc = pDvmFmtOps->pfnInitialize(&pThis->DvmDisk, &pThis->hVolMgrFmt);
            if (RT_SUCCESS(rc))
                pThis->pDvmFmtOps = pDvmFmtOps;
            return rc;
        }
    }
    return VERR_NOT_SUPPORTED;
}

RTDECL(const char *) RTDvmMapGetFormatName(RTDVM hVolMgr)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, NULL);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, NULL);

    return pThis->pDvmFmtOps->pszFmt;
}

RTDECL(RTDVMFORMATTYPE) RTDvmMapGetFormatType(RTDVM hVolMgr)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, RTDVMFORMATTYPE_INVALID);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, RTDVMFORMATTYPE_INVALID);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, RTDVMFORMATTYPE_INVALID);

    return pThis->pDvmFmtOps->enmFormat;
}

RTDECL(int) RTDvmMapQueryDiskUuid(RTDVM hVolMgr, PRTUUID pUuid)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, VERR_INVALID_HANDLE);
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);

    if (pThis->pDvmFmtOps->pfnQueryDiskUuid)
        return pThis->pDvmFmtOps->pfnQueryDiskUuid(pThis->hVolMgrFmt, pUuid);
    return VERR_NOT_SUPPORTED;
}

RTDECL(uint32_t) RTDvmMapGetValidVolumes(RTDVM hVolMgr)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, UINT32_MAX);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, UINT32_MAX);

    return pThis->pDvmFmtOps->pfnGetValidVolumes(pThis->hVolMgrFmt);
}

RTDECL(uint32_t) RTDvmMapGetMaxVolumes(RTDVM hVolMgr)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, UINT32_MAX);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, UINT32_MAX);

    return pThis->pDvmFmtOps->pfnGetMaxVolumes(pThis->hVolMgrFmt);
}

RTDECL(int) RTDvmMapQueryFirstVolume(RTDVM hVolMgr, PRTDVMVOLUME phVol)
{
    PRTDVMINTERNAL pThis = hVolMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, VERR_INVALID_HANDLE);
    AssertPtrReturn(phVol, VERR_INVALID_POINTER);

    int rc = VERR_DVM_MAP_EMPTY;
    PRTDVMVOLUMEINTERNAL pVol = RTListGetFirst(&pThis->VolumeList, RTDVMVOLUMEINTERNAL, VolumeNode);
    if (pVol)
    {
        rc = VINF_SUCCESS;
        RTDvmVolumeRetain(pVol);
        *phVol = pVol;
    }

    return rc;
}

RTDECL(int) RTDvmMapQueryNextVolume(RTDVM hVolMgr, RTDVMVOLUME hVol, PRTDVMVOLUME phVolNext)
{
    PRTDVMINTERNAL       pThis = hVolMgr;
    PRTDVMVOLUMEINTERNAL pVol = hVol;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, VERR_INVALID_HANDLE);
    AssertPtrReturn(pVol, VERR_INVALID_HANDLE);
    AssertReturn(pVol->u32Magic == RTDVMVOLUME_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(phVolNext, VERR_INVALID_POINTER);

    int rc = VERR_DVM_MAP_NO_VOLUME;
    PRTDVMVOLUMEINTERNAL pVolNext = RTListGetNext(&pThis->VolumeList, pVol, RTDVMVOLUMEINTERNAL, VolumeNode);
    if (pVolNext)
    {
        rc = VINF_SUCCESS;
        RTDvmVolumeRetain(pVolNext);
        *phVolNext = pVolNext;
    }

    return rc;
}

RTDECL(int) RTDvmMapQueryBlockStatus(RTDVM hVolMgr, uint64_t off, uint64_t cb, bool *pfAllocated)
{
    PRTDVMINTERNAL pThis = hVolMgr;

    /*
     * Input validation.
     */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfAllocated, VERR_INVALID_POINTER);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->hVolMgrFmt != NIL_RTDVMFMT, VERR_WRONG_ORDER);
    AssertMsgReturn(   off      <= pThis->DvmDisk.cbDisk
                    || cb       <= pThis->DvmDisk.cbDisk
                    || off + cb <= pThis->DvmDisk.cbDisk,
                    ("off=%#RX64 cb=%#RX64 cbDisk=%#RX64\n", off, cb, pThis->DvmDisk.cbDisk),
                    VERR_OUT_OF_RANGE);

    /*
     * Check whether the range is inuse by the volume manager metadata first.
     */
    int rc = pThis->pDvmFmtOps->pfnQueryRangeUse(pThis->hVolMgrFmt, off, cb, pfAllocated);
    if (RT_FAILURE(rc) || *pfAllocated)
        return rc;

    /*
     * Not used by volume manager metadata, so work thru the specified range one
     * volume / void (free space) at a time.  All must be unallocated for us to
     * reach the end, we return immediately if any portion is allocated.
     */
    while (cb > 0)
    {
        /*
         * Search through all volumes.
         *
         * It is not possible to get all start sectors and sizes of all volumes
         * here because volumes can be scattered around the disk for certain formats.
         * Linux LVM is one example, it extents of logical volumes don't need to be
         * contiguous on the medium.
         */
        bool                 fVolFound = false;
        PRTDVMVOLUMEINTERNAL pVol;
        RTListForEach(&pThis->VolumeList, pVol, RTDVMVOLUMEINTERNAL, VolumeNode)
        {
            uint64_t cbIntersect;
            uint64_t offVol;
            bool fIntersect = pThis->pDvmFmtOps->pfnVolumeIsRangeIntersecting(pVol->hVolFmt, off, cb, &offVol, &cbIntersect);
            if (fIntersect)
            {
                fVolFound = true;
                if (pVol->pfnQueryBlockStatus)
                {
                    bool fVolAllocated = true;
                    rc = pVol->pfnQueryBlockStatus(pVol->pvUser, offVol, cbIntersect, &fVolAllocated);
                    if (RT_FAILURE(rc) || fVolAllocated)
                    {
                        *pfAllocated = true;
                        return rc;
                    }
                }
                else if (!(pThis->fFlags & DVM_FLAGS_NO_STATUS_CALLBACK_MARK_AS_UNUSED))
                {
                    *pfAllocated = true;
                    return VINF_SUCCESS;
                }
                /* else, flag is set, continue. */

                cb  -= cbIntersect;
                off += cbIntersect;
                break;
            }
        }

        if (!fVolFound)
        {
            if (pThis->fFlags & DVM_FLAGS_UNUSED_SPACE_MARK_AS_USED)
            {
                *pfAllocated = true;
                return VINF_SUCCESS;
            }

            cb  -= pThis->DvmDisk.cbSector;
            off += pThis->DvmDisk.cbSector;
        }
    }

    *pfAllocated = false;
    return rc;
}

RTDECL(int) RTDvmMapQueryTableLocations(RTDVM hVolMgr, uint32_t fFlags,
                                        PRTDVMTABLELOCATION paLocations, size_t cLocations, size_t *pcActual)
{
    PRTDVMINTERNAL pThis = hVolMgr;

    /*
     * Input validation.
     */
    if (cLocations)
    {
        AssertPtrReturn(paLocations, VERR_INVALID_POINTER);
        if (pcActual)
        {
            AssertPtrReturn(pcActual, VERR_INVALID_POINTER);
            *pcActual = 0;
        }
    }
    else
    {
        AssertPtrReturn(pcActual, VERR_INVALID_POINTER);
        *pcActual = 0;
    }
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(fFlags & ~RTDVMMAPQTABLOC_F_VALID_MASK), VERR_INVALID_FLAGS);

    /*
     * Pass it down to the format backend.
     */
    return pThis->pDvmFmtOps->pfnQueryTableLocations(pThis->hVolMgrFmt, fFlags, paLocations, cLocations, pcActual);
}

RTDECL(uint32_t) RTDvmVolumeRetain(RTDVMVOLUME hVol)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs >= 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 1)
        RTDvmRetain(pThis->pVolMgr);
    return cRefs;
}

RTDECL(uint32_t) RTDvmVolumeRelease(RTDVMVOLUME hVol)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    if (pThis == NIL_RTDVMVOLUME)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
    {
        /* Release the volume manager. */
        pThis->pfnQueryBlockStatus = NULL;
        RTDvmRelease(pThis->pVolMgr);
    }
    return cRefs;
}

RTDECL(void) RTDvmVolumeSetQueryBlockStatusCallback(RTDVMVOLUME hVol,
                                                    PFNDVMVOLUMEQUERYBLOCKSTATUS pfnQueryBlockStatus,
                                                    void *pvUser)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTDVMVOLUME_MAGIC);

    pThis->pfnQueryBlockStatus = pfnQueryBlockStatus;
    pThis->pvUser = pvUser;
}

RTDECL(uint64_t) RTDvmVolumeGetSize(RTDVMVOLUME hVol)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, 0);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeGetSize(pThis->hVolFmt);
}

RTDECL(int) RTDvmVolumeQueryName(RTDVMVOLUME hVol, char **ppszVolName)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(ppszVolName, VERR_INVALID_POINTER);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeQueryName(pThis->hVolFmt, ppszVolName);
}

RTDECL(RTDVMVOLTYPE) RTDvmVolumeGetType(RTDVMVOLUME hVol)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, RTDVMVOLTYPE_INVALID);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, RTDVMVOLTYPE_INVALID);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeGetType(pThis->hVolFmt);
}

RTDECL(uint64_t) RTDvmVolumeGetFlags(RTDVMVOLUME hVol)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, UINT64_MAX);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, UINT64_MAX);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeGetFlags(pThis->hVolFmt);
}

RTDECL(int) RTDvmVolumeQueryRange(RTDVMVOLUME hVol, uint64_t *poffStart, uint64_t *poffLast)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(poffStart, VERR_INVALID_POINTER);
    AssertPtrReturn(poffLast, VERR_INVALID_POINTER);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeQueryRange(pThis->hVolFmt, poffStart, poffLast);
}

RTDECL(int) RTDvmVolumeQueryTableLocation(RTDVMVOLUME hVol, uint64_t *poffTable, uint64_t *pcbTable)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(poffTable, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbTable, VERR_INVALID_POINTER);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeQueryTableLocation(pThis->hVolFmt, poffTable, pcbTable);
}

RTDECL(uint32_t) RTDvmVolumeGetIndex(RTDVMVOLUME hVol, RTDVMVOLIDX enmIndex)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, UINT32_MAX);
    AssertReturn(enmIndex > RTDVMVOLIDX_INVALID && enmIndex < RTDVMVOLIDX_END, UINT32_MAX);

    if (enmIndex == RTDVMVOLIDX_HOST)
    {
#ifdef RT_OS_WINDOWS
        enmIndex = RTDVMVOLIDX_USER_VISIBLE;
#elif defined(RT_OS_LINUX) \
   || defined(RT_OS_FREEBSD) \
   || defined(RT_OS_NETBSD) \
   || defined(RT_OS_SOLARIS) \
   || defined(RT_OS_DARWIN) \
   || defined(RT_OS_OS2) /*whatever*/
/* Darwing and freebsd matches the linux algo. Solaris matches linux algo partially, at least, in the part we use. */
        enmIndex = RTDVMVOLIDX_LINUX;
#else
# error "PORTME"
#endif
    }

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeGetIndex(pThis->hVolFmt, enmIndex);
}

/**
 * Helper for RTDvmVolumeQueryProp.
 */
static void rtDvmReturnInteger(void *pvDst, size_t cbDst, PRTUINT64U pSrc, size_t cbSrc)
{
    /* Read the source: */
    uint64_t uSrc;
    switch (cbSrc)
    {
        case sizeof(uint8_t):  uSrc = (uint8_t)pSrc->Words.w0; break;
        case sizeof(uint16_t): uSrc = pSrc->Words.w0; break;
        case sizeof(uint32_t): uSrc = pSrc->s.Lo; break;
        default: AssertFailed(); RT_FALL_THROUGH();
        case sizeof(uint64_t): uSrc = pSrc->u; break;
    }

    /* Write the destination: */
    switch (cbDst)
    {
        default: AssertFailed(); RT_FALL_THROUGH();
        case sizeof(uint8_t):  *(uint8_t  *)pvDst = (uint8_t)uSrc; break;
        case sizeof(uint16_t): *(uint16_t *)pvDst = (uint16_t)uSrc; break;
        case sizeof(uint32_t): *(uint32_t *)pvDst = (uint32_t)uSrc; break;
        case sizeof(uint64_t): *(uint64_t *)pvDst = uSrc; break;
    }
}

RTDECL(int) RTDvmVolumeQueryProp(RTDVMVOLUME hVol, RTDVMVOLPROP enmProperty, void *pvBuf, size_t cbBuf, size_t *pcbBuf)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, VERR_INVALID_HANDLE);
    size_t cbBufFallback = 0;
    if (pcbBuf == NULL)
        pcbBuf = &cbBufFallback;
    AssertReturnStmt(enmProperty > RTDVMVOLPROP_INVALID && enmProperty < RTDVMVOLPROP_END, *pcbBuf = 0, VERR_INVALID_FUNCTION);

    switch (enmProperty)
    {
        /* 8, 16, 32 or 64 bit sized integers: */
        case RTDVMVOLPROP_MBR_FIRST_HEAD:
        case RTDVMVOLPROP_MBR_FIRST_SECTOR:
        case RTDVMVOLPROP_MBR_LAST_HEAD:
        case RTDVMVOLPROP_MBR_LAST_SECTOR:
        case RTDVMVOLPROP_MBR_TYPE:
        {
            *pcbBuf = sizeof(uint8_t);
            AssertReturn(   cbBuf == sizeof(uint8_t)
                         || cbBuf == sizeof(uint16_t)
                         || cbBuf == sizeof(uint32_t)
                         || cbBuf == sizeof(uint64_t), VERR_INVALID_PARAMETER);
            AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

            RTUINT64U Union64 = {0};
            int rc = pThis->pVolMgr->pDvmFmtOps->pfnVolumeQueryProp(pThis->hVolFmt, enmProperty, &Union64, cbBuf, pcbBuf);
            rtDvmReturnInteger(pvBuf, cbBuf, &Union64, *pcbBuf);
            return rc;
        }

        /* 16, 32 or 64 bit sized integers: */
        case RTDVMVOLPROP_MBR_FIRST_CYLINDER:
        case RTDVMVOLPROP_MBR_LAST_CYLINDER:
        {
            *pcbBuf = sizeof(uint16_t);
            AssertReturn(   cbBuf == sizeof(uint16_t)
                         || cbBuf == sizeof(uint32_t)
                         || cbBuf == sizeof(uint64_t), VERR_INVALID_PARAMETER);
            AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

            RTUINT64U Union64 = {0};
            int rc = pThis->pVolMgr->pDvmFmtOps->pfnVolumeQueryProp(pThis->hVolFmt, enmProperty, &Union64, cbBuf, pcbBuf);
            rtDvmReturnInteger(pvBuf, cbBuf, &Union64, *pcbBuf);
            return rc;
        }

        /* RTUUIDs: */
        case RTDVMVOLPROP_GPT_TYPE:
        case RTDVMVOLPROP_GPT_UUID:
        {
            *pcbBuf = sizeof(RTUUID);
            AssertReturn(cbBuf == sizeof(RTUUID), VERR_INVALID_PARAMETER);
            AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

            RTUUID Uuid = RTUUID_INITIALIZE_NULL;
            int rc = pThis->pVolMgr->pDvmFmtOps->pfnVolumeQueryProp(pThis->hVolFmt, enmProperty, &Uuid, sizeof(RTUUID), pcbBuf);
            memcpy(pvBuf, &Uuid, sizeof(Uuid));
            return rc;
        }

        case RTDVMVOLPROP_INVALID:
        case RTDVMVOLPROP_END:
        case RTDVMVOLPROP_32BIT_HACK:
            break;
        /* No default case! */
    }
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}

RTDECL(uint64_t) RTDvmVolumeGetPropU64(RTDVMVOLUME hVol, RTDVMVOLPROP enmProperty, uint64_t uDefault)
{
    uint64_t uValue = uDefault;
    int rc = RTDvmVolumeQueryProp(hVol, enmProperty, &uValue, sizeof(uValue), NULL);
    if (RT_SUCCESS(rc))
        return uValue;
    AssertMsg(rc == VERR_NOT_SUPPORTED || rc == VERR_NOT_FOUND, ("%Rrc enmProperty=%d\n", rc, enmProperty));
    return uDefault;
}

RTDECL(int) RTDvmVolumeRead(RTDVMVOLUME hVol, uint64_t off, void *pvBuf, size_t cbRead)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbRead > 0, VERR_INVALID_PARAMETER);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeRead(pThis->hVolFmt, off, pvBuf, cbRead);
}

RTDECL(int) RTDvmVolumeWrite(RTDVMVOLUME hVol, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    PRTDVMVOLUMEINTERNAL pThis = hVol;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDVMVOLUME_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbWrite > 0, VERR_INVALID_PARAMETER);

    return pThis->pVolMgr->pDvmFmtOps->pfnVolumeWrite(pThis->hVolFmt, off, pvBuf, cbWrite);
}

RTDECL(const char *) RTDvmVolumeTypeGetDescr(RTDVMVOLTYPE enmVolType)
{
    AssertReturn(enmVolType >= RTDVMVOLTYPE_INVALID && enmVolType < RTDVMVOLTYPE_END, NULL);

    return g_apszDvmVolTypes[enmVolType];
}

