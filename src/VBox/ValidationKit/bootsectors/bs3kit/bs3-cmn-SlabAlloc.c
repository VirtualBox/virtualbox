/* $Id: bs3-cmn-SlabAlloc.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3SlabAlloc
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
#include <iprt/asm-mem.h>
#include <iprt/asm.h>


#undef Bs3SlabAlloc
BS3_CMN_DEF(void BS3_FAR *, Bs3SlabAlloc,(PBS3SLABCTL pSlabCtl))
{
    if (pSlabCtl->cFreeChunks)
    {
        int32_t iBit = ASMBitFirstClear(&pSlabCtl->bmAllocated, pSlabCtl->cChunks);
        if (iBit >= 0)
        {
            BS3_XPTR_AUTO(void, pvRet);
            ASMBitSet(&pSlabCtl->bmAllocated, iBit);
            pSlabCtl->cFreeChunks  -= 1;

            BS3_XPTR_SET_FLAT(void, pvRet,
                              BS3_XPTR_GET_FLAT(uint8_t, pSlabCtl->pbStart) + ((uint32_t)iBit << pSlabCtl->cChunkShift));
            return BS3_XPTR_GET(void, pvRet);
        }
    }
    return NULL;
}

