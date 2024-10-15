/* $Id: atexit-vcc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Visual C++ Compiler - Simple atexit implementation.
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

#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/nocrt/stdlib.h>

#include "internal/compiler-vcc.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTNOCRTATEXITCHUNK
{
    PFNRTNOCRTATEXITCALLBACK apfnCallbacks[256];
} RTNOCRTATEXITCHUNK;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The first atexit() registration chunk. */
static RTNOCRTATEXITCHUNK   g_aAtExitPrealloc;
/** Array of atexit() callback chunk pointers. */
static RTNOCRTATEXITCHUNK  *g_apAtExit[8192 / 256] = { &g_aAtExitPrealloc, };
/** Chunk and callback index in one. */
static volatile uint32_t    g_idxNextAtExit        = 0;


/* Note! not using atexit here because it'll clash with built-in prototype.  */
extern "C" int nocrt_atexit(PFNRTNOCRTATEXITCALLBACK pfnCallback) RT_NOEXCEPT
{
    AssertPtr(pfnCallback);

    /*
     * Allocate a table index.
     */
    uint32_t idx = ASMAtomicIncU32(&g_idxNextAtExit) - 1;
    AssertReturnStmt(idx < RT_ELEMENTS(g_apAtExit) * RT_ELEMENTS(g_apAtExit[0]->apfnCallbacks),
                     ASMAtomicDecU32(&g_idxNextAtExit), -1);

    /*
     * Make sure the table chunk is there.
     */
    uint32_t            idxChunk = idx / RT_ELEMENTS(g_apAtExit[0]->apfnCallbacks);
    RTNOCRTATEXITCHUNK *pChunk   = ASMAtomicReadPtrT(&g_apAtExit[idxChunk], RTNOCRTATEXITCHUNK *);
    if (!pChunk)
    {
        pChunk = (RTNOCRTATEXITCHUNK *)RTMemAllocZ(sizeof(*pChunk)); /* ASSUMES that the allocator works w/o initialization! */
        AssertReturn(pChunk, -1); /* don't try decrement, someone could be racing us... */

        if (!ASMAtomicCmpXchgPtr(&g_apAtExit[idxChunk], pChunk, NULL))
        {
            RTMemFree(pChunk);

            pChunk = ASMAtomicReadPtrT(&g_apAtExit[idxChunk], RTNOCRTATEXITCHUNK *);
            Assert(pChunk);
        }
    }

    /*
     * Add our callback.
     */
    pChunk->apfnCallbacks[idxChunk % RT_ELEMENTS(pChunk->apfnCallbacks)] = pfnCallback;
    return 0;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(atexit);


void rtVccTermRunAtExit(void) RT_NOEXCEPT
{
    uint32_t idxAtExit = ASMAtomicReadU32(&g_idxNextAtExit);
    if (idxAtExit-- > 0)
    {
        uint32_t idxChunk    = idxAtExit / RT_ELEMENTS(g_apAtExit[0]->apfnCallbacks);
        uint32_t idxCallback = idxAtExit % RT_ELEMENTS(g_apAtExit[0]->apfnCallbacks);
        for (;;)
        {
            RTNOCRTATEXITCHUNK *pChunk = ASMAtomicReadPtrT(&g_apAtExit[idxChunk], RTNOCRTATEXITCHUNK *);
            if (pChunk)
            {
                do
                {
                    g_idxNextAtExit = idxAtExit--; /* Make sure we don't try problematic atexit callbacks! */

                    PFNRTNOCRTATEXITCALLBACK pfnCallback = pChunk->apfnCallbacks[idxCallback];
                    if (pfnCallback) /* Can be NULL see registration code */
                    {
                        pfnCallback();
                        pChunk->apfnCallbacks[idxCallback] = NULL;
                    }
                } while (idxCallback-- > 0);
            }
            else
                idxAtExit -= RT_ELEMENTS(g_apAtExit[0]->apfnCallbacks);
            if (idxChunk == 0)
                break;
            idxChunk--;
            idxCallback = RT_ELEMENTS(g_apAtExit[0]->apfnCallbacks) - 1;
        }

        g_idxNextAtExit = 0;
    }
}

