/* $Id: ASMBitFirstSet-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASMBitFirstSet - generic C implementation.
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


RTDECL(int32_t) ASMBitFirstSet(const volatile void RT_FAR *pvBitmap, uint32_t cBits) RT_NOTHROW_DEF
{
    const volatile size_t RT_FAR *pu = (const volatile size_t RT_FAR *)pvBitmap;
    Assert(!(cBits & 31));
    Assert(!((uintptr_t)pvBitmap & 3));

#if ARCH_BITS > 32
    /* Deal with misaligned bitmaps (happens all the time via ASMBitNextClear()). */
    if (!((uintptr_t)pvBitmap & 7) && cBits >= 32)
    {
        uint32_t u32 = *(const volatile uint32_t RT_FAR *)pu;
        if (u32 != 0)
        {
            size_t const iBaseBit = ((uintptr_t)pu - (uintptr_t)pvBitmap) * 8;
            return iBaseBit + ASMBitFirstSetU32(RT_LE2H_U32(u32)) - 1;
        }
        pu     = (const volatile size_t RT_FAR *)((uintptr_t)pu + sizeof(uint32_t));
        cBits -= 32;
    }
#endif

    /* Main search loop: */
    while (cBits >= sizeof(size_t) * 8)
    {
        size_t u = *pu;
        if (u == 0)
        { }
        else
        {
            size_t const iBaseBit = ((uintptr_t)pu - (uintptr_t)pvBitmap) * 8;
#if ARCH_BITS == 32
            return iBaseBit + ASMBitFirstSetU32(RT_LE2H_U32(u)) - 1;
#elif ARCH_BITS == 64
            return iBaseBit + ASMBitFirstSetU64(RT_LE2H_U64(u)) - 1;
#else
# error "ARCH_BITS is not supported"
#endif
        }

        pu++;
        cBits -= sizeof(size_t) * 8;
    }

#if ARCH_BITS > 32
    /* Final 32-bit item (unlikely)? */
    if (cBits < 32)
    { }
    else
    {
        uint32_t u32 = *(const volatile uint32_t RT_FAR *)pu;
        if (u32 != 0)
        {
            size_t const iBaseBit = ((uintptr_t)pu - (uintptr_t)pvBitmap) * 8;
            return iBaseBit + ASMBitFirstSetU32(RT_LE2H_U32(u32)) - 1;
        }
    }
#endif

    return -1;
}

