/* $Id: bs3-cmn-SlabAllocFixed.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3SlabAllocEx
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


#undef Bs3SlabAllocFixed
BS3_CMN_DEF(uint16_t, Bs3SlabAllocFixed,(PBS3SLABCTL pSlabCtl, uint32_t uFlatAddr, uint16_t cChunks))
{
    uint32_t iBit32 = (uFlatAddr - BS3_XPTR_GET_FLAT(void, pSlabCtl->pbStart)) >> pSlabCtl->cChunkShift;
    if (iBit32 < pSlabCtl->cChunks)
    {
        uint16_t iBit = (uint16_t)iBit32;
        uint16_t i;

        /* If the slab doesn't cover the entire area requested, reduce it.
           Caller can then move on to the next slab in the list to get the rest. */
        if (pSlabCtl->cChunks - iBit < cChunks)
            cChunks = pSlabCtl->cChunks - iBit;

        /* Check that all the chunks are free. */
        for (i = 0; i < cChunks; i++)
            if (ASMBitTest(&pSlabCtl->bmAllocated, iBit + i))
                return UINT16_MAX;

        /* Complete the allocation. */
        for (i = 0; i < cChunks; i++)
            ASMBitSet(&pSlabCtl->bmAllocated, iBit + i);
        pSlabCtl->cFreeChunks  -= cChunks;
        return cChunks;
    }
    return 0;
}

