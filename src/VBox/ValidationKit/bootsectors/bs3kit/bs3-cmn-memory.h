/* $Id: bs3-cmn-memory.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Internal Memory Structures, Variables and Functions.
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

#ifndef BS3KIT_INCLUDED_bs3_cmn_memory_h
#define BS3KIT_INCLUDED_bs3_cmn_memory_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "bs3kit.h"
#include <iprt/asm.h>

RT_C_DECLS_BEGIN;


typedef union BS3SLABCTLLOW
{
    BS3SLABCTL  Core;
    uint32_t    au32Alloc[(sizeof(BS3SLABCTL) + (0xA0000 / _4K / 8) ) / 4];
} BS3SLABCTLLOW;
#ifndef DOXYGEN_RUNNING
# define g_Bs3Mem4KLow          BS3_DATA_NM(g_Bs3Mem4KLow)
#endif
extern BS3SLABCTLLOW            g_Bs3Mem4KLow;


typedef union BS3SLABCTLUPPERTILED
{
    BS3SLABCTL  Core;
    uint32_t    au32Alloc[(sizeof(BS3SLABCTL) + ((BS3_SEL_TILED_AREA_SIZE - _1M) / _4K / 8) ) / 4];
} BS3SLABCTLUPPERTILED;
#ifndef DOXYGEN_RUNNING
# define g_Bs3Mem4KUpperTiled   BS3_DATA_NM(g_Bs3Mem4KUpperTiled)
#endif
extern BS3SLABCTLUPPERTILED     g_Bs3Mem4KUpperTiled;


/** The number of chunk sizes used by the slab list arrays
 * (g_aBs3LowSlabLists, g_aBs3UpperTiledSlabLists, more?). */
#define BS3_MEM_SLAB_LIST_COUNT 6

#ifndef DOXYGEN_RUNNING
# define g_aiBs3SlabListsByPowerOfTwo   BS3_DATA_NM(g_aiBs3SlabListsByPowerOfTwo)
# define g_acbBs3SlabLists              BS3_DATA_NM(g_acbBs3SlabLists)
# define g_aBs3LowSlabLists             BS3_DATA_NM(g_aBs3LowSlabLists)
# define g_aBs3UpperTiledSlabLists      BS3_DATA_NM(g_aBs3UpperTiledSlabLists)
# define g_cbBs3SlabCtlSizesforLists    BS3_DATA_NM(g_cbBs3SlabCtlSizesforLists)
#endif
extern uint8_t const            g_aiBs3SlabListsByPowerOfTwo[12];
extern uint16_t const           g_acbBs3SlabLists[BS3_MEM_SLAB_LIST_COUNT];
extern BS3SLABHEAD              g_aBs3LowSlabLists[BS3_MEM_SLAB_LIST_COUNT];
extern BS3SLABHEAD              g_aBs3UpperTiledSlabLists[BS3_MEM_SLAB_LIST_COUNT];
extern uint16_t const           g_cbBs3SlabCtlSizesforLists[BS3_MEM_SLAB_LIST_COUNT];


/**
 * Translates a allocation request size to a slab list index.
 *
 * @returns Slab list index if small request, UINT8_MAX if large.
 * @param   cbRequest   The number of bytes requested.
 */
DECLINLINE(uint8_t) bs3MemSizeToSlabListIndex(size_t cbRequest)
{
    if (cbRequest <= g_acbBs3SlabLists[BS3_MEM_SLAB_LIST_COUNT - 1])
    {
        unsigned idx = cbRequest ? ASMBitLastSetU16((uint16_t)(cbRequest - 1)) : 0;
        return g_aiBs3SlabListsByPowerOfTwo[idx];
    }
    return UINT8_MAX;
}


RT_C_DECLS_END;

#endif /* !BS3KIT_INCLUDED_bs3_cmn_memory_h */

