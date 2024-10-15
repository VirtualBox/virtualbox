/* $Id: ASMBitNextSet-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASMBitNextSet - generic C implementation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/asm.h>
#include "internal/iprt.h"

#include <iprt/assert.h>


RTDECL(int) ASMBitNextSet(const volatile void RT_FAR *pvBitmap, uint32_t cBits, uint32_t iBitPrev) RT_NOTHROW_DEF
{
    const volatile uint32_t RT_FAR *pau32Bitmap = (const volatile uint32_t RT_FAR *)pvBitmap;
    int                             iBit = ++iBitPrev & 31;
    Assert(!(cBits & 31));
    Assert(!((uintptr_t)pvBitmap & 3));
    if (iBit)
    {
        /*
         * Inspect the 32-bit word containing the unaligned bit.
         */
        uint32_t u32 = RT_LE2H_U32(pau32Bitmap[iBitPrev / 32]) >> iBit;
        if (u32)
            return iBitPrev + ASMBitFirstSetU32(u32) - 1;

        /*
         * Skip ahead and see if there is anything left to search.
         */
        iBitPrev |= 31;
        iBitPrev++;
        if (cBits <= (uint32_t)iBitPrev)
            return -1;
    }

    /*
     * 32-bit aligned search, let ASMBitFirstSet do the dirty work.
     */
    iBit = ASMBitFirstSet(&pau32Bitmap[iBitPrev / 32], cBits - iBitPrev);
    if (iBit >= 0)
        iBit += iBitPrev;
    return iBit;
}

