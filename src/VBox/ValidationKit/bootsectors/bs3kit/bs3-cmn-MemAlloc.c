/* $Id: bs3-cmn-MemAlloc.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3MemAlloc
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include "bs3kit-template-header.h"
#include "bs3-cmn-memory.h"


#undef Bs3MemAlloc
BS3_CMN_DEF(void BS3_FAR *, Bs3MemAlloc,(BS3MEMKIND enmKind, size_t cb))
{
    void BS3_FAR   *pvRet;
    uint8_t         idxSlabList;

#if ARCH_BITS == 16
    /* Don't try allocate memory which address we cannot return. */
    if (   enmKind != BS3MEMKIND_REAL
        && BS3_MODE_IS_RM_OR_V86(g_bBs3CurrentMode))
        enmKind = BS3MEMKIND_REAL;
#endif

    idxSlabList = bs3MemSizeToSlabListIndex(cb);
    if (idxSlabList < BS3_MEM_SLAB_LIST_COUNT)
    {
        /*
         * Try allocate a chunk from the list.
         */
        PBS3SLABHEAD pHead = enmKind == BS3MEMKIND_REAL
                           ? &g_aBs3LowSlabLists[idxSlabList]
                           : &g_aBs3UpperTiledSlabLists[idxSlabList];

        BS3_ASSERT(g_aBs3LowSlabLists[idxSlabList].cbChunk >= cb);
        pvRet = Bs3SlabListAlloc(pHead);
        if (pvRet)
        { /* likely */ }
        else
        {
            /*
             * Grow the list.
             */
            PBS3SLABCTL pNew = (PBS3SLABCTL)Bs3SlabAlloc(  enmKind == BS3MEMKIND_REAL
                                                         ? &g_Bs3Mem4KLow.Core
                                                         : &g_Bs3Mem4KUpperTiled.Core);
            BS3_ASSERT(((uintptr_t)pNew & 0xfff) == 0);
            if (pNew)
            {
                uint16_t const      cbHdr = g_cbBs3SlabCtlSizesforLists[idxSlabList];
                BS3_XPTR_AUTO(void, pvNew);
                BS3_XPTR_SET(void, pvNew, pNew);

                Bs3SlabInit(pNew, cbHdr, BS3_XPTR_GET_FLAT(void, pvNew) + cbHdr, _4K - cbHdr, pHead->cbChunk);
                Bs3SlabListAdd(pHead, pNew);

                pvRet = Bs3SlabListAlloc(pHead);
            }
        }
    }
    else
    {
        /*
         * Allocate one or more pages.
         */
        size_t   const cbAligned = RT_ALIGN_Z(cb, _4K);
        uint16_t const cPages    = cbAligned >> 12 /* div _4K */;
        PBS3SLABCTL    pSlabCtl  = enmKind == BS3MEMKIND_REAL
                                 ? &g_Bs3Mem4KLow.Core : &g_Bs3Mem4KUpperTiled.Core;

        pvRet = Bs3SlabAllocEx(pSlabCtl,
                               cPages,
                               cPages <= _64K / _4K ? BS3_SLAB_ALLOC_F_SAME_TILE : 0);
    }
    return pvRet;
}

