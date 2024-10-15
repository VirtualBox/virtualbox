/* $Id: memset.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CRT Strings, memset().
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
#include "internal/iprt.h"
#include <iprt/string.h>


/**
 * Fill a memory block with specific byte.
 *
 * @returns pvDst.
 * @param   pvDst      Pointer to the block.
 * @param   ch      The filler char.
 * @param   cb      The size of the block.
 */
#undef memset
#ifdef _MSC_VER
# if _MSC_VER >= 1400
void *  __cdecl RT_NOCRT(memset)(__out_bcount_full_opt(_Size) void *pvDst, __in int ch, __in size_t cb)
# else
void *RT_NOCRT(memset)(void *pvDst, int ch, size_t cb)
# endif
#else
void *RT_NOCRT(memset)(void *pvDst, int ch, size_t cb)
#endif
{
    union
    {
        uint8_t  *pu8;
        uint32_t *pu32;
        void     *pvDst;
    } u;
    u.pvDst = pvDst;

    /* 32-bit word moves. */
    uint32_t u32 = ch | (ch << 8);
    u32 |= u32 << 16;
    size_t c = cb >> 2;
    while (c-- > 0)
        *u.pu32++ = u32;

    /* Remaining byte moves. */
    c = cb & 3;
    while (c-- > 0)
        *u.pu8++ = (uint8_t)u32;

    return pvDst;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(memset);
