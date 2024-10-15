/* $Id: bs3-cmn-MemFree.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3MemFree
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


#undef Bs3MemFree
BS3_CMN_DEF(void, Bs3MemFree,(void BS3_FAR *pv, size_t cb))
{
    if (pv != NULL)
    {
        uint16_t    cChunks;
        PBS3SLABCTL pCtl;
        BS3_XPTR_AUTO(void, pvFlat);
        BS3_XPTR_SET(void, pvFlat, pv);

        if (BS3_XPTR_GET_FLAT(void, pvFlat) & 0xfffU)
        {
            /* Use an XPTR here in case we're in real mode and the caller has
               messed around with the pointer. */
            BS3_XPTR_AUTO(BS3SLABCTL, pTmp);
            BS3_XPTR_SET_FLAT(BS3SLABCTL, pTmp, BS3_XPTR_GET_FLAT(void, pvFlat) & ~(uint32_t)0xfff);
            pCtl = BS3_XPTR_GET(BS3SLABCTL, pTmp);
            BS3_ASSERT(pCtl->cbChunk >= cb);
            cChunks = 1;
        }
        else
        {
            pCtl = BS3_XPTR_GET_FLAT(void, pvFlat) < _1M ? &g_Bs3Mem4KLow.Core : &g_Bs3Mem4KUpperTiled.Core;
            cChunks = RT_ALIGN_Z(cb, _4K) >> 12;
        }
        Bs3SlabFree(pCtl, BS3_XPTR_GET_FLAT(void, pvFlat), cChunks);
    }
}

