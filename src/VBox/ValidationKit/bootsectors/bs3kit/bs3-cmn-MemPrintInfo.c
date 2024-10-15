/* $Id: bs3-cmn-MemPrintInfo.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3MemPrintInfo
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
#include <iprt/asm.h>


/**
 * Prints a slab control structure with allocation map.
 *
 * @param   pCtl                The slab control structure to print.
 * @param   pszPrefix           The output prefix.
 */
static void Bs3MemPrintInfoSlabCtl(PBS3SLABCTL pCtl, const char BS3_FAR *pszPrefix)
{
    unsigned iChunk;
    Bs3TestPrintf("%s / %#06x: %u of %u chunks free", pszPrefix, pCtl->cbChunk, pCtl->cFreeChunks, pCtl->cChunks);
    for (iChunk = 0; iChunk < pCtl->cChunks; iChunk++)
    {
        if ((iChunk & 63) == 0)
            Bs3TestPrintf("\n%s:", pszPrefix);
        if (ASMBitTest(pCtl->bmAllocated, iChunk))
            Bs3TestPrintf((iChunk & 7) != 0 ? "x" : " x");
        else
            Bs3TestPrintf((iChunk & 7) != 0 ? "-" : " -");
    }
    Bs3TestPrintf("\n");
}



/**
 * Prints a summary of a slab allocation list (i.e. the heap).
 *
 * @param   paLists             Array of BS3_MEM_SLAB_LIST_COUNT lists.
 * @param   pszPrefix           The output prefix.
 */
static void Bs3MemPrintInfoSlabList(PBS3SLABHEAD paLists, const char BS3_FAR *pszPrefix)
{
    unsigned iSlab;
    for (iSlab = 0; iSlab < BS3_MEM_SLAB_LIST_COUNT; iSlab++)
        if (paLists[iSlab].cSlabs)
            Bs3TestPrintf("%s / %#06x: %u slabs, %RU32 of %RU32 chunks free\n",
                          pszPrefix, paLists[iSlab].cbChunk, paLists[iSlab].cSlabs,
                          paLists[iSlab].cFreeChunks, paLists[iSlab].cChunks);
}


#undef Bs3MemPrintInfo
BS3_CMN_DEF(void, Bs3MemPrintInfo,(void))
{
    Bs3MemPrintInfoSlabList(g_aBs3LowSlabLists,        "Lower");
    Bs3MemPrintInfoSlabList(g_aBs3LowSlabLists,        "Upper");
    Bs3MemPrintInfoSlabCtl(&g_Bs3Mem4KLow.Core,        "4KLow");
    Bs3MemPrintInfoSlabCtl(&g_Bs3Mem4KUpperTiled.Core, "Tiled");
}

