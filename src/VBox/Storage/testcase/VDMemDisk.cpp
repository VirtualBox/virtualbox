/* $Id: VDMemDisk.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox HDD container test utility, memory disk/file.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */
#define LOGGROUP LOGGROUP_DEFAULT /** @todo Log group */
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/mem.h>
#include <iprt/file.h>

#include "VDMemDisk.h"

/**
 * Memory disk/file.
 */
typedef struct VDMEMDISK
{
    /** Current size of the disk. */
    uint64_t     cbDisk;
    /** Flag whether the disk can grow. */
    bool         fGrowable;
    /** Pointer to the AVL tree holding the segments. */
    PAVLRU64TREE pTreeSegments;
} VDMEMDISK;

/**
 * A disk segment.
 */
typedef struct VDMEMDISKSEG
{
    /** AVL tree core. */
    AVLRU64NODECORE  Core;
    /** Pointer to the data. */
    void            *pvSeg;
} VDMEMDISKSEG, *PVDMEMDISKSEG;


int VDMemDiskCreate(PPVDMEMDISK ppMemDisk, uint64_t cbSize)
{
    AssertPtrReturn(ppMemDisk, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PVDMEMDISK pMemDisk = (PVDMEMDISK)RTMemAllocZ(sizeof(VDMEMDISK));
    if (pMemDisk)
    {
        pMemDisk->fGrowable = cbSize ? false : true;
        pMemDisk->cbDisk = cbSize;
        pMemDisk->pTreeSegments = (PAVLRU64TREE)RTMemAllocZ(sizeof(AVLRU64TREE));
        if (pMemDisk->pTreeSegments)
            *ppMemDisk = pMemDisk;
        else
        {
            RTMemFree(pMemDisk);
            rc = VERR_NO_MEMORY;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdMemDiskDestroy(PAVLRU64NODECORE pNode, void *pvUser)
{
    RT_NOREF1(pvUser);
    PVDMEMDISKSEG pSeg = (PVDMEMDISKSEG)pNode;
    RTMemFree(pSeg->pvSeg);
    RTMemFree(pSeg);
    return VINF_SUCCESS;
}

void VDMemDiskDestroy(PVDMEMDISK pMemDisk)
{
    AssertPtrReturnVoid(pMemDisk);

    RTAvlrU64Destroy(pMemDisk->pTreeSegments, vdMemDiskDestroy, NULL);
    RTMemFree(pMemDisk->pTreeSegments);
    RTMemFree(pMemDisk);
}

int VDMemDiskWrite(PVDMEMDISK pMemDisk, uint64_t off, size_t cbWrite, PRTSGBUF pSgBuf)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pMemDisk=%#p off=%llu cbWrite=%zu pSgBuf=%#p\n",
                 pMemDisk, off, cbWrite, pSgBuf));

    AssertPtrReturn(pMemDisk, VERR_INVALID_POINTER);
    AssertPtrReturn(pSgBuf, VERR_INVALID_POINTER);

    /* Check for a write beyond the end of a disk. */
    if (   !pMemDisk->fGrowable
        && (off + cbWrite) > pMemDisk->cbDisk)
        return VERR_INVALID_PARAMETER;

    /* Update the segments */
    size_t cbLeft    = cbWrite;
    uint64_t offCurr = off;

    while (   cbLeft
           && RT_SUCCESS(rc))
    {
        PVDMEMDISKSEG pSeg = (PVDMEMDISKSEG)RTAvlrU64RangeGet(pMemDisk->pTreeSegments, offCurr);
        size_t cbRange  = 0;
        unsigned offSeg = 0;

        if (!pSeg)
        {
            /* Get next segment */
            pSeg = (PVDMEMDISKSEG)RTAvlrU64GetBestFit(pMemDisk->pTreeSegments, offCurr, true);
            if (   !pSeg
                || offCurr + cbLeft <= pSeg->Core.Key)
                cbRange = cbLeft;
            else
                cbRange = pSeg->Core.Key - offCurr;

            /* Create new segment */
            pSeg = (PVDMEMDISKSEG)RTMemAllocZ(sizeof(VDMEMDISKSEG));
            if (pSeg)
            {
                pSeg->Core.Key      = offCurr;
                pSeg->Core.KeyLast  = offCurr + cbRange - 1;
                pSeg->pvSeg         = RTMemAllocZ(cbRange);

                if (!pSeg->pvSeg)
                {
                    RTMemFree(pSeg);
                    rc = VERR_NO_MEMORY;
                }
                else
                {
                    bool fInserted = RTAvlrU64Insert(pMemDisk->pTreeSegments, &pSeg->Core);
                    AssertMsg(fInserted, ("Bug!\n")); NOREF(fInserted);
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
        {
            offSeg  = offCurr - pSeg->Core.Key;
            cbRange = RT_MIN(cbLeft, (size_t)(pSeg->Core.KeyLast + 1 - offCurr));
        }

        if (RT_SUCCESS(rc))
        {
            AssertPtr(pSeg);
            size_t cbCopied = RTSgBufCopyToBuf(pSgBuf, (uint8_t *)pSeg->pvSeg + offSeg, cbRange);
            Assert(cbCopied == cbRange); NOREF(cbCopied);
        }

        offCurr += cbRange;
        cbLeft  -= cbRange;
    }

    /* Update size of the disk. */
    if (   RT_SUCCESS(rc)
        && pMemDisk->fGrowable
        && (off + cbWrite) > pMemDisk->cbDisk)
    {
        pMemDisk->cbDisk = off + cbWrite;
    }

    return rc;
}


int VDMemDiskRead(PVDMEMDISK pMemDisk, uint64_t off, size_t cbRead, PRTSGBUF pSgBuf)
{
    LogFlowFunc(("pMemDisk=%#p off=%llu cbRead=%zu pSgBuf=%#p\n",
                 pMemDisk, off, cbRead, pSgBuf));

    AssertPtrReturn(pMemDisk, VERR_INVALID_POINTER);
    AssertPtrReturn(pSgBuf, VERR_INVALID_POINTER);

    /* Check for a read beyond the end of a disk. */
    if ((off + cbRead) > pMemDisk->cbDisk)
        return VERR_INVALID_PARAMETER;

    /* Compare read data */
    size_t cbLeft    = cbRead;
    uint64_t offCurr = off;

    while (cbLeft)
    {
        PVDMEMDISKSEG pSeg = (PVDMEMDISKSEG)RTAvlrU64RangeGet(pMemDisk->pTreeSegments, offCurr);
        size_t cbRange  = 0;
        unsigned offSeg = 0;

        if (!pSeg)
        {
            /* Get next segment */
            pSeg = (PVDMEMDISKSEG)RTAvlrU64GetBestFit(pMemDisk->pTreeSegments, offCurr, true);
            if (   !pSeg
                || offCurr + cbLeft <= pSeg->Core.Key)
            {
                /* No data in the tree for this read. Fill with 0. */
                cbRange = cbLeft;
            }
            else
                cbRange = pSeg->Core.Key - offCurr;

            RTSgBufSet(pSgBuf, 0, cbRange);
        }
        else
        {
            offSeg  = offCurr - pSeg->Core.Key;
            cbRange = RT_MIN(cbLeft, (size_t)(pSeg->Core.KeyLast + 1 - offCurr));

            RTSgBufCopyFromBuf(pSgBuf, (uint8_t *)pSeg->pvSeg + offSeg, cbRange);
        }

        offCurr += cbRange;
        cbLeft  -= cbRange;
    }

    return VINF_SUCCESS;
}

int VDMemDiskSetSize(PVDMEMDISK pMemDisk, uint64_t cbSize)
{
    AssertPtrReturn(pMemDisk, VERR_INVALID_POINTER);

    if (!pMemDisk->fGrowable)
        return VERR_NOT_SUPPORTED;

    if (pMemDisk->cbDisk <= cbSize)
    {
        /* Increase. */
        pMemDisk->cbDisk = cbSize;
    }
    else
    {
        /* We have to delete all parts beyond the new end. */
        PVDMEMDISKSEG pSeg = (PVDMEMDISKSEG)RTAvlrU64Get(pMemDisk->pTreeSegments, cbSize);
        if (pSeg)
        {
            RTAvlrU64Remove(pMemDisk->pTreeSegments, pSeg->Core.Key);
            if (pSeg->Core.Key < cbSize)
            {
                /* Cut off the part which is not in the file anymore. */
                pSeg->pvSeg = RTMemRealloc(pSeg->pvSeg, pSeg->Core.KeyLast - cbSize + 1);
                pSeg->Core.KeyLast = cbSize - pSeg->Core.Key - 1;

                bool fInserted = RTAvlrU64Insert(pMemDisk->pTreeSegments, &pSeg->Core);
                AssertMsg(fInserted, ("Bug!\n")); NOREF(fInserted);
            }
            else
            {
                /* Free the whole block. */
                RTMemFree(pSeg->pvSeg);
                RTMemFree(pSeg);
            }
        }

        /* Kill all blocks coming after. */
        do
        {
            pSeg = (PVDMEMDISKSEG)RTAvlrU64GetBestFit(pMemDisk->pTreeSegments, cbSize, true);
            if (pSeg)
            {
                RTAvlrU64Remove(pMemDisk->pTreeSegments, pSeg->Core.Key);
                RTMemFree(pSeg->pvSeg);
                pSeg->pvSeg = NULL;
                RTMemFree(pSeg);
            }
            else
                break;
        } while (true);

        pMemDisk->cbDisk = cbSize;
    }

    return VINF_SUCCESS;
}

int VDMemDiskGetSize(PVDMEMDISK pMemDisk, uint64_t *pcbSize)
{
    AssertPtrReturn(pMemDisk, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    *pcbSize = pMemDisk->cbDisk;
    return VINF_SUCCESS;
}

/**
 * Writes a segment to the given file.
 *
 * @returns IPRT status code.
 *
 * @param pNode    The disk segment to write to the file.
 * @param pvParam  Opaque user data containing the pointer to
 *                 the file handle.
 */
static DECLCALLBACK(int) vdMemDiskSegmentWriteToFile(PAVLRU64NODECORE pNode, void *pvParam)
{
    PVDMEMDISKSEG pSeg = (PVDMEMDISKSEG)pNode;
    RTFILE hFile = *(PRTFILE)pvParam;

    return RTFileWriteAt(hFile, pSeg->Core.Key, pSeg->pvSeg, pSeg->Core.KeyLast - pSeg->Core.Key + 1, NULL);
}

int VDMemDiskWriteToFile(PVDMEMDISK pMemDisk, const char *pcszFilename)
{
    int rc = VINF_SUCCESS;
    RTFILE hFile = NIL_RTFILE;

    LogFlowFunc(("pMemDisk=%#p pcszFilename=%s\n", pMemDisk, pcszFilename));
    AssertPtrReturn(pMemDisk, VERR_INVALID_POINTER);
    AssertPtrReturn(pcszFilename, VERR_INVALID_POINTER);

    rc = RTFileOpen(&hFile, pcszFilename, RTFILE_O_DENY_NONE | RTFILE_O_CREATE | RTFILE_O_WRITE);
    if (RT_SUCCESS(rc))
    {
        rc = RTAvlrU64DoWithAll(pMemDisk->pTreeSegments, true, vdMemDiskSegmentWriteToFile, &hFile);

        RTFileClose(hFile);
        if (RT_FAILURE(rc))
            RTFileDelete(pcszFilename);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

int VDMemDiskReadFromFile(PVDMEMDISK pMemDisk, const char *pcszFilename)
{
    RT_NOREF2(pMemDisk, pcszFilename);
    return VERR_NOT_IMPLEMENTED;
}

int VDMemDiskCmp(PVDMEMDISK pMemDisk, uint64_t off, size_t cbCmp, PRTSGBUF pSgBuf)
{
    LogFlowFunc(("pMemDisk=%#p off=%llx cbCmp=%u pSgBuf=%#p\n",
                 pMemDisk, off, cbCmp, pSgBuf));

    /* Compare data */
    size_t   cbLeft   = cbCmp;
    uint64_t offCurr  = off;

    while (cbLeft)
    {
        PVDMEMDISKSEG pSeg = (PVDMEMDISKSEG)RTAvlrU64Get(pMemDisk->pTreeSegments, offCurr);
        size_t cbRange  = 0;
        bool fCmp       = false;
        unsigned offSeg = 0;

        if (!pSeg)
        {
            /* Get next segment */
            pSeg = (PVDMEMDISKSEG)RTAvlrU64GetBestFit(pMemDisk->pTreeSegments, offCurr, true);
            if (!pSeg)
            {
                /* No data in the tree for this read. Assume everything is ok. */
                cbRange = cbLeft;
            }
            else if (offCurr + cbLeft <= pSeg->Core.Key)
                cbRange = cbLeft;
            else
                cbRange = pSeg->Core.Key - offCurr;
        }
        else
        {
            fCmp    = true;
            offSeg  = offCurr - pSeg->Core.Key;
            cbRange = RT_MIN(cbLeft, (size_t)(pSeg->Core.KeyLast + 1 - offCurr));
        }

        if (fCmp)
        {
            RTSGSEG Seg;
            RTSGBUF SgBufCmp;
            size_t cbOff = 0;
            int rc = 0;

            Seg.cbSeg = cbRange;
            Seg.pvSeg = (uint8_t *)pSeg->pvSeg + offSeg;

            RTSgBufInit(&SgBufCmp, &Seg, 1);
            rc = RTSgBufCmpEx(pSgBuf, &SgBufCmp, cbRange, &cbOff, true);
            if (rc)
                return rc;
        }
        else
            RTSgBufAdvance(pSgBuf, cbRange);

        offCurr += cbRange;
        cbLeft  -= cbRange;
    }

    return 0;
}

