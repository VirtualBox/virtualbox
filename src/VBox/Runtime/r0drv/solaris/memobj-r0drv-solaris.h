/* $Id: memobj-r0drv-solaris.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Ring-0 Memory Objects - Segment driver, Solaris.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_solaris_memobj_r0drv_solaris_h
#define IPRT_INCLUDED_SRC_r0drv_solaris_memobj_r0drv_solaris_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-solaris-kernel.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
typedef struct SEGVBOX_CRARGS
{
    uint64_t *paPhysAddrs;
    size_t    cbPageSize;
    uint_t    fPageAccess;
} SEGVBOX_CRARGS;
typedef SEGVBOX_CRARGS *PSEGVBOX_CRARGS;

typedef struct SEGVBOX_DATA
{
    uint_t    fPageAccess;
    size_t    cbPageSize;
} SEGVBOX_DATA;
typedef SEGVBOX_DATA *PSEGVBOX_DATA;

static struct seg_ops s_SegVBoxOps;
static vnode_t s_segVBoxVnode;


DECLINLINE(int) rtR0SegVBoxSolCreate(seg_t *pSeg, void *pvArgs)
{
    struct as      *pAddrSpace = pSeg->s_as;
    PSEGVBOX_CRARGS pArgs      = pvArgs;
    PSEGVBOX_DATA   pData      = kmem_zalloc(sizeof(*pData), KM_SLEEP);

    AssertPtr(pAddrSpace);
    AssertPtr(pArgs);
    AssertPtr(pData);

    /*
     * Currently we only map _4K pages but this segment driver can handle any size
     * supported by the Solaris HAT layer.
     */
    size_t cbPageSize  = pArgs->cbPageSize;
    size_t uPageShift  = 0;
    switch (cbPageSize)
    {
        case _4K: uPageShift = 12; break;
        case _2M: uPageShift = 21; break;
        default:  AssertReleaseMsgFailed(("Unsupported page size for mapping cbPageSize=%llx\n", cbPageSize)); break;
    }

    hat_map(pAddrSpace->a_hat, pSeg->s_base, pSeg->s_size, HAT_MAP);
    pData->fPageAccess = pArgs->fPageAccess | PROT_USER;
    pData->cbPageSize  = cbPageSize;

    pSeg->s_ops  = &s_SegVBoxOps;
    pSeg->s_data = pData;

    /*
     * Now load and lock down the mappings to the physical addresses.
     */
    caddr_t virtAddr = pSeg->s_base;
    pgcnt_t cPages   = (pSeg->s_size + cbPageSize - 1) >> uPageShift;
    for (pgcnt_t iPage = 0; iPage < cPages; ++iPage, virtAddr += cbPageSize)
    {
        hat_devload(pAddrSpace->a_hat, virtAddr, cbPageSize, pArgs->paPhysAddrs[iPage] >> uPageShift,
                    pData->fPageAccess | HAT_UNORDERED_OK, HAT_LOAD_LOCK);
    }

    return 0;
}


static int rtR0SegVBoxSolDup(seg_t *pSrcSeg, seg_t *pDstSeg)
{
    /*
     * Duplicate a segment and return the new segment in 'pDstSeg'.
     */
    PSEGVBOX_DATA pSrcData = pSrcSeg->s_data;
    PSEGVBOX_DATA pDstData = kmem_zalloc(sizeof(*pDstData), KM_SLEEP);

    AssertPtr(pDstData);
    AssertPtr(pSrcData);

    pDstData->fPageAccess  = pSrcData->fPageAccess;
    pDstData->cbPageSize   = pSrcData->cbPageSize;
    pDstSeg->s_ops         = &s_SegVBoxOps;
    pDstSeg->s_data        = pDstData;

    return 0;
}


static int rtR0SegVBoxSolUnmap(seg_t *pSeg, caddr_t virtAddr, size_t cb)
{
    PSEGVBOX_DATA pData = pSeg->s_data;

    AssertRelease(pData);
    AssertReleaseMsg(virtAddr >= pSeg->s_base, ("virtAddr=%p s_base=%p\n", virtAddr, pSeg->s_base));
    AssertReleaseMsg(virtAddr + cb <= pSeg->s_base + pSeg->s_size, ("virtAddr=%p cb=%llu s_base=%p s_size=%llu\n", virtAddr,
                                                                    cb, pSeg->s_base, pSeg->s_size));
    size_t cbPageOffset = pData->cbPageSize - 1;
    AssertRelease(!(cb & cbPageOffset));
    AssertRelease(!((uintptr_t)virtAddr & cbPageOffset));

    if (   virtAddr != pSeg->s_base
        || cb       != pSeg->s_size)
    {
        return ENOTSUP;
    }

    hat_unload(pSeg->s_as->a_hat, virtAddr, cb, HAT_UNLOAD_UNMAP | HAT_UNLOAD_UNLOCK);

    seg_free(pSeg);
    return 0;
}


static void rtR0SegVBoxSolFree(seg_t *pSeg)
{
    PSEGVBOX_DATA pData = pSeg->s_data;
    kmem_free(pData, sizeof(*pData));
}


static int rtR0SegVBoxSolFault(struct hat *pHat, seg_t *pSeg, caddr_t virtAddr, size_t cb, enum fault_type FaultType,
                               enum seg_rw ReadWrite)
{
    /*
     * We would demand fault if the (u)read() path would SEGOP_FAULT() on buffers mapped in via our
     * segment driver i.e. prefaults before DMA. Don't fail in such case where we're called directly,
     * see @bugref{5047}.
     */
    return 0;
}


static int rtR0SegVBoxSolFaultA(seg_t *pSeg, caddr_t virtAddr)
{
    return 0;
}


static int rtR0SegVBoxSolSetProt(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t fPageAccess)
{
    return EACCES;
}


static int rtR0SegVBoxSolCheckProt(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t fPageAccess)
{
    return EINVAL;
}


static int rtR0SegVBoxSolKluster(seg_t *pSeg, caddr_t virtAddr, ssize_t Delta)
{
    return -1;
}


static int rtR0SegVBoxSolSync(seg_t *pSeg, caddr_t virtAddr, size_t cb, int Attr, uint_t fFlags)
{
    return 0;
}


static size_t rtR0SegVBoxSolInCore(seg_t *pSeg, caddr_t virtAddr, size_t cb, char *pVec)
{
    PSEGVBOX_DATA pData = pSeg->s_data;
    AssertRelease(pData);
    size_t uPageOffset  = pData->cbPageSize - 1;
    size_t uPageMask    = ~uPageOffset;
    size_t cbLen        = (cb + uPageOffset) & uPageMask;
    for (virtAddr = 0; cbLen != 0; cbLen -= pData->cbPageSize, virtAddr += pData->cbPageSize)
        *pVec++ = 1;
    return cbLen;
}


static int rtR0SegVBoxSolLockOp(seg_t *pSeg, caddr_t virtAddr, size_t cb, int Attr, int Op, ulong_t *pLockMap, size_t off)
{
    return 0;
}


static int rtR0SegVBoxSolGetProt(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t *pafPageAccess)
{
    PSEGVBOX_DATA pData = pSeg->s_data;
    size_t iPage = seg_page(pSeg, virtAddr + cb) - seg_page(pSeg, virtAddr) + 1;
    if (iPage)
    {
        do
        {
            iPage--;
            pafPageAccess[iPage] = pData->fPageAccess;
        } while (iPage);
    }
    return 0;
}


static u_offset_t rtR0SegVBoxSolGetOffset(seg_t *pSeg, caddr_t virtAddr)
{
    return ((uintptr_t)virtAddr - (uintptr_t)pSeg->s_base);
}


static int rtR0SegVBoxSolGetType(seg_t *pSeg, caddr_t virtAddr)
{
    return MAP_SHARED;
}


static int rtR0SegVBoxSolGetVp(seg_t *pSeg, caddr_t virtAddr, vnode_t **ppVnode)
{
    *ppVnode = &s_segVBoxVnode;
    return 0;
}


static int rtR0SegVBoxSolAdvise(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t Behav /* wut? */)
{
    return 0;
}


#if defined(VBOX_NEW_CRASH_DUMP_FORMAT)
static void rtR0SegVBoxSolDump(seg_t *pSeg, dump_addpage_f Func)
#else
static void rtR0SegVBoxSolDump(seg_t *pSeg)
#endif
{
    /* Nothing to do. */
}


static int rtR0SegVBoxSolPageLock(seg_t *pSeg, caddr_t virtAddr, size_t cb, page_t ***pppPage, enum lock_type LockType, enum seg_rw ReadWrite)
{
    return ENOTSUP;
}


static int rtR0SegVBoxSolSetPageSize(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t SizeCode)
{
    return ENOTSUP;
}


static int rtR0SegVBoxSolGetMemId(seg_t *pSeg, caddr_t virtAddr, memid_t *pMemId)
{
    return ENODEV;
}


#ifdef SEGOP_CAPABLE
static int rtR0SegVBoxSolCapable(seg_t *pSeg, segcapability_t Capab)
{
    return 0;
}
#endif


static struct seg_ops s_SegVBoxOps =
{
    rtR0SegVBoxSolDup,
    rtR0SegVBoxSolUnmap,
    rtR0SegVBoxSolFree,
    rtR0SegVBoxSolFault,
    rtR0SegVBoxSolFaultA,
    rtR0SegVBoxSolSetProt,
    rtR0SegVBoxSolCheckProt,
    rtR0SegVBoxSolKluster,
    NULL,                       /* swapout */
    rtR0SegVBoxSolSync,
    rtR0SegVBoxSolInCore,
    rtR0SegVBoxSolLockOp,
    rtR0SegVBoxSolGetProt,
    rtR0SegVBoxSolGetOffset,
    rtR0SegVBoxSolGetType,
    rtR0SegVBoxSolGetVp,
    rtR0SegVBoxSolAdvise,
    rtR0SegVBoxSolDump,
    rtR0SegVBoxSolPageLock,
    rtR0SegVBoxSolSetPageSize,
    rtR0SegVBoxSolGetMemId,
    NULL,                       /* getpolicy() */
#ifdef SEGOP_CAPABLE
    rtR0SegVBoxSolCapable
#endif
};

#endif /* !IPRT_INCLUDED_SRC_r0drv_solaris_memobj_r0drv_solaris_h */

