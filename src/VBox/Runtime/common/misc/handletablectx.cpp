/* $Id: handletablectx.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Handle Tables.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/handletable.h>
#include "internal/iprt.h"

#include <iprt/mem.h>
#include <iprt/spinlock.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/system.h>
#include "internal/magics.h"
#include "handletable.h"


RTDECL(int)     RTHandleTableAllocWithCtx(RTHANDLETABLE hHandleTable, void *pvObj, void *pvCtx, uint32_t *ph)
{
    PRTHANDLETABLEINT   pThis;
    int                 rc;

    /* validate the input */
    pThis = (PRTHANDLETABLEINT)hHandleTable;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTHANDLETABLE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fFlags & RTHANDLETABLE_FLAGS_CONTEXT, VERR_INVALID_FUNCTION);
    AssertReturn(!RTHT_IS_FREE(pvObj), VERR_INVALID_PARAMETER);
    AssertPtrReturn(ph, VERR_INVALID_POINTER);
    *ph = pThis->uBase - 1;

    /*
     * Allocation loop.
     */
    rtHandleTableLock(pThis);

    do
    {
        /*
         * Try grab a free entry from the head of the free list.
         */
        uint32_t i = pThis->iFreeHead;
        if (i != NIL_RTHT_INDEX)
        {
            PRTHTENTRYCTX   pEntry;
            PRTHTENTRYFREE  pFree = (PRTHTENTRYFREE)rtHandleTableLookupWithCtxIdx(pThis, i);
            Assert(pFree);
            if (i == pThis->iFreeTail)
                pThis->iFreeTail = pThis->iFreeHead = NIL_RTHT_INDEX;
            else
                pThis->iFreeHead = RTHT_GET_FREE_IDX(pFree);
            pThis->cCurAllocated++;
            Assert(pThis->cCurAllocated <= pThis->cCur);

            /*
             * Setup the entry and return.
             */
            pEntry = (PRTHTENTRYCTX)pFree;
            pEntry->pvObj = pvObj;
            pEntry->pvCtx = pvCtx;
            *ph = i + pThis->uBase;
            rc = VINF_SUCCESS;
        }
        /*
         * Must expand the handle table, unless it's full.
         */
        else if (pThis->cCur >= pThis->cMax)
        {
            rc = VERR_NO_MORE_HANDLES;
            Assert(pThis->cCur == pThis->cCurAllocated);
        }
        else
        {
            void          **papvLevel1;
            uint32_t        iLevel1New;
            PRTHTENTRYCTX   paTable;

            /*
             * Do we have to expand the 1st level table too?
             */
            uint32_t const  iLevel1 = pThis->cCur / RTHT_LEVEL2_ENTRIES;
            uint32_t        cLevel1 = iLevel1 >= pThis->cLevel1
                                    ? pThis->cLevel1 + RTSystemGetPageSize() / sizeof(void *)
                                    : 0;
            if (cLevel1 > pThis->cMax / RTHT_LEVEL2_ENTRIES)
                cLevel1 = pThis->cMax / RTHT_LEVEL2_ENTRIES;
            Assert(!cLevel1 || pThis->cMax / RTHT_LEVEL2_ENTRIES >= RTHT_LEVEL1_DYN_ALLOC_THRESHOLD);

            /* leave the lock (never do fancy stuff from behind a spinlock). */
            rtHandleTableUnlock(pThis);

            /*
             * Do the allocation(s).
             */
            rc = VERR_TRY_AGAIN;
            papvLevel1 = NULL;
            if (cLevel1)
            {
                papvLevel1 = (void **)RTMemAlloc(sizeof(void *) * cLevel1);
                if (!papvLevel1)
                    return VERR_NO_MEMORY;
            }

            paTable = (PRTHTENTRYCTX)RTMemAlloc(sizeof(*paTable) * RTHT_LEVEL2_ENTRIES);
            if (!paTable)
            {
                RTMemFree(papvLevel1);
                return VERR_NO_MEMORY;
            }

            /* re-enter the lock. */
            rtHandleTableLock(pThis);

            /*
             * Insert the new bits, but be a bit careful as someone might have
             * raced us expanding the table.
             */
            /* deal with the 1st level lookup expansion first */
            if (cLevel1)
            {
                Assert(papvLevel1);
                if (cLevel1 > pThis->cLevel1)
                {
                    void **papvTmp;

                    /* Replace the 1st level table. */
                    memcpy(papvLevel1, pThis->papvLevel1, sizeof(void *) * pThis->cLevel1);
                    memset(&papvLevel1[pThis->cLevel1], 0, sizeof(void *) * (cLevel1 - pThis->cLevel1));
                    pThis->cLevel1 = cLevel1;
                    papvTmp = pThis->papvLevel1;
                    pThis->papvLevel1 = papvLevel1;
                    papvLevel1 = papvTmp;
                }

                /* free the obsolete one (outside the lock of course) */
                rtHandleTableUnlock(pThis);
                RTMemFree(papvLevel1);
                rtHandleTableLock(pThis);
            }

            /* insert the table we allocated. */
            iLevel1New = pThis->cCur / RTHT_LEVEL2_ENTRIES;
            if (    iLevel1New < pThis->cLevel1
                &&  pThis->cCur < pThis->cMax)
            {
                pThis->papvLevel1[iLevel1New] = paTable;

                /* link all entries into a free list. */
                Assert(!(pThis->cCur % RTHT_LEVEL2_ENTRIES));
                for (i = 0; i < RTHT_LEVEL2_ENTRIES - 1; i++)
                {
                    RTHT_SET_FREE_IDX((PRTHTENTRYFREE)&paTable[i], i + 1 + pThis->cCur);
                    paTable[i].pvCtx = (void *)~(uintptr_t)7;
                }
                RTHT_SET_FREE_IDX((PRTHTENTRYFREE)&paTable[RTHT_LEVEL2_ENTRIES - 1], NIL_RTHT_INDEX);
                paTable[RTHT_LEVEL2_ENTRIES - 1].pvCtx = (void *)~(uintptr_t)7;

                /* join the free list with the other. */
                if (pThis->iFreeTail == NIL_RTHT_INDEX)
                    pThis->iFreeHead = pThis->cCur;
                else
                {
                    PRTHTENTRYFREE pPrev = (PRTHTENTRYFREE)rtHandleTableLookupWithCtxIdx(pThis, pThis->iFreeTail);
                    Assert(pPrev);
                    RTHT_SET_FREE_IDX(pPrev, pThis->cCur);
                }
                pThis->iFreeTail = pThis->cCur + RTHT_LEVEL2_ENTRIES - 1;

                pThis->cCur += RTHT_LEVEL2_ENTRIES;
            }
            else
            {
                /* free the table (raced someone, and we lost). */
                rtHandleTableUnlock(pThis);
                RTMemFree(paTable);
                rtHandleTableLock(pThis);
            }

            rc = VERR_TRY_AGAIN;
        }
    } while (rc == VERR_TRY_AGAIN);

    rtHandleTableUnlock(pThis);

    return rc;
}
RT_EXPORT_SYMBOL(RTHandleTableAllocWithCtx);


RTDECL(void *)  RTHandleTableLookupWithCtx(RTHANDLETABLE hHandleTable, uint32_t h, void *pvCtx)
{
    void               *pvObj = NULL;
    PRTHTENTRYCTX       pEntry;
    PRTHANDLETABLEINT   pThis;

    /* validate the input */
    pThis = (PRTHANDLETABLEINT)hHandleTable;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->u32Magic == RTHANDLETABLE_MAGIC, NULL);
    AssertReturn(pThis->fFlags & RTHANDLETABLE_FLAGS_CONTEXT, NULL);


    /* acquire the lock */
    rtHandleTableLock(pThis);

    /*
     * Perform the lookup and retaining.
     */
    pEntry = rtHandleTableLookupWithCtx(pThis, h);
    if (pEntry && pEntry->pvCtx == pvCtx)
    {
        pvObj = pEntry->pvObj;
        if (!RTHT_IS_FREE(pvObj))
        {
            if (pThis->pfnRetain)
            {
                int rc = pThis->pfnRetain(hHandleTable, pEntry->pvObj, pvCtx, pThis->pvRetainUser);
                if (RT_FAILURE(rc))
                    pvObj = NULL;
            }
        }
        else
            pvObj = NULL;
    }

    /* release the lock */
    rtHandleTableUnlock(pThis);
    return pvObj;
}
RT_EXPORT_SYMBOL(RTHandleTableLookupWithCtx);


RTDECL(void *)  RTHandleTableFreeWithCtx(RTHANDLETABLE hHandleTable, uint32_t h, void *pvCtx)
{
    void               *pvObj = NULL;
    PRTHTENTRYCTX       pEntry;
    PRTHANDLETABLEINT   pThis;

    /* validate the input */
    pThis = (PRTHANDLETABLEINT)hHandleTable;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->u32Magic == RTHANDLETABLE_MAGIC, NULL);
    AssertReturn(pThis->fFlags & RTHANDLETABLE_FLAGS_CONTEXT, NULL);


    /* acquire the lock */
    rtHandleTableLock(pThis);

    /*
     * Perform the lookup and retaining.
     */
    pEntry = rtHandleTableLookupWithCtx(pThis, h);
    if (pEntry && pEntry->pvCtx == pvCtx)
    {
        pvObj = pEntry->pvObj;
        if (!RTHT_IS_FREE(pvObj))
        {
            if (pThis->pfnRetain)
            {
                int rc = pThis->pfnRetain(hHandleTable, pEntry->pvObj, pvCtx, pThis->pvRetainUser);
                if (RT_FAILURE(rc))
                    pvObj = NULL;
            }

            /*
             * Link it into the free list.
             */
            if (pvObj)
            {
                PRTHTENTRYFREE  pFree;
                uint32_t        i;

                pEntry->pvCtx = (void *)~(uintptr_t)7;

                pFree = (PRTHTENTRYFREE)pEntry;
                RTHT_SET_FREE_IDX(pFree, NIL_RTHT_INDEX);

                i = h - pThis->uBase;
                if (pThis->iFreeTail == NIL_RTHT_INDEX)
                    pThis->iFreeHead = pThis->iFreeTail = i;
                else
                {
                    PRTHTENTRYFREE pPrev = (PRTHTENTRYFREE)rtHandleTableLookupWithCtxIdx(pThis, pThis->iFreeTail);
                    Assert(pPrev);
                    RTHT_SET_FREE_IDX(pPrev, i);
                    pThis->iFreeTail = i;
                }

                Assert(pThis->cCurAllocated > 0);
                pThis->cCurAllocated--;
            }
        }
        else
            pvObj = NULL;
    }

    /* release the lock */
    rtHandleTableUnlock(pThis);
    return pvObj;
}
RT_EXPORT_SYMBOL(RTHandleTableFreeWithCtx);

