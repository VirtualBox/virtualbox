/* $Id$ */
/** @file
 * IPRT - ASMMultU64ByU32DivByU64 - generic C implementation.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <iprt/asm-math.h>
#include "internal/iprt.h"


RT_DECL_ASM(uint64_t) ASMMultU64ByU32DivByU64(uint64_t u64A, uint32_t u32B, uint64_t u64C)
{
    uint64_t u64Rem;
    uint64_t u64Dividend = ASMMult2xU64Ret2xU64(u64A, u32B, &u64Rem);
    uint64_t u64Result   = 0;

     /* Not sure if we should assert here because ASMMultU64ByU32DivByU32() has the same requirement for divisor. */
#if 0
    Assert(u64C);
    Assert(u64Rem < u64C); /* Result must fit in 64 bits. */
#endif

    /* Binary long division. */
    for (unsigned i = 0; i < 64; i++)
    {
        /* Reminder * 2 + next-divident-bit can require 65-bits, fCarry holds the 65th bit here. */
        bool const fCarry = RT_BOOL(u64Rem & RT_BIT_64(63));
        u64Rem = (u64Rem << 1) | (u64Dividend >> 63);
        u64Dividend <<= 1;
        u64Result   <<= 1;
        if (   fCarry
            || u64Rem >= u64C)
        {
            u64Rem    -= u64C;
            u64Result |= 1;
        }
    }
    return u64Result;
}
