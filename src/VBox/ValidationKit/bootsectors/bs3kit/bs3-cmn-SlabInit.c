/* $Id: bs3-cmn-SlabInit.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3SlabInit
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
#include <iprt/asm.h>


#undef Bs3SlabInit
BS3_CMN_DEF(void, Bs3SlabInit,(PBS3SLABCTL pSlabCtl, size_t cbSlabCtl, uint32_t uFlatSlabPtr, uint32_t cbSlab, uint16_t cbChunk))
{
    uint16_t cBits;
    BS3_ASSERT(RT_IS_POWER_OF_TWO(cbChunk));
    BS3_ASSERT(cbSlab >= cbChunk * 4);
    BS3_ASSERT(!(uFlatSlabPtr & (cbChunk - 1)));

    BS3_XPTR_SET_FLAT(BS3SLABCTL, pSlabCtl->pNext, 0);
    BS3_XPTR_SET_FLAT(BS3SLABCTL, pSlabCtl->pHead, 0);
    BS3_XPTR_SET_FLAT(BS3SLABCTL, pSlabCtl->pbStart, uFlatSlabPtr);
    pSlabCtl->cbChunk           = cbChunk;
    pSlabCtl->cChunkShift       = ASMBitFirstSetU16(cbChunk) - 1;
    pSlabCtl->cChunks           = cbSlab >> pSlabCtl->cChunkShift;
    pSlabCtl->cFreeChunks       = pSlabCtl->cChunks;
    cBits                       = RT_ALIGN_T(pSlabCtl->cChunks, 32, uint16_t);
    BS3_ASSERT(cbSlabCtl >= RT_UOFFSETOF_DYN(BS3SLABCTL, bmAllocated[cBits >> 3]));
    Bs3MemZero(&pSlabCtl->bmAllocated, cBits >> 3);

    /* Mark excess bitmap padding bits as allocated. */
    if (cBits != pSlabCtl->cChunks)
    {
        uint16_t iBit;
        for (iBit = pSlabCtl->cChunks; iBit < cBits; iBit++)
            ASMBitSet(pSlabCtl->bmAllocated, iBit);
    }
}

