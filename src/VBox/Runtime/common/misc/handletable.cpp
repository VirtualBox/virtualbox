/* $Id: handletable.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include "internal/magics.h"
#include "handletable.h"



RTDECL(int) RTHandleTableCreateEx(PRTHANDLETABLE phHandleTable, uint32_t fFlags, uint32_t uBase, uint32_t cMax,
                                  PFNRTHANDLETABLERETAIN pfnRetain, void *pvUser)
{
    PRTHANDLETABLEINT   pThis;
    uint32_t            cLevel1;
    size_t              cb;

    /*
     * Validate input.
     */
    AssertPtrReturn(phHandleTable, VERR_INVALID_POINTER);
    *phHandleTable = NIL_RTHANDLETABLE;
    AssertPtrNullReturn(pfnRetain, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTHANDLETABLE_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(RT_BOOL(fFlags & RTHANDLETABLE_FLAGS_LOCKED) + RT_BOOL(fFlags & RTHANDLETABLE_FLAGS_LOCKED_IRQ_SAFE) < 2,
                 VERR_INVALID_PARAMETER);
    AssertReturn(cMax > 0, VERR_INVALID_PARAMETER);
    AssertReturn(UINT32_MAX - cMax >= uBase, VERR_INVALID_PARAMETER);

    /*
     * Adjust the cMax value so it is a multiple of the 2nd level tables.
     */
    if (cMax >= UINT32_MAX - RTHT_LEVEL2_ENTRIES)
        cMax = UINT32_MAX - RTHT_LEVEL2_ENTRIES + 1;
    cMax = ((cMax + RTHT_LEVEL2_ENTRIES - 1) / RTHT_LEVEL2_ENTRIES) * RTHT_LEVEL2_ENTRIES;

    cLevel1 = cMax / RTHT_LEVEL2_ENTRIES;
    Assert(cLevel1 * RTHT_LEVEL2_ENTRIES == cMax);

    /*
     * Allocate the structure, include the 1st level lookup table
     * if it's below the threshold size.
     */
    cb = sizeof(RTHANDLETABLEINT);
    if (cLevel1 < RTHT_LEVEL1_DYN_ALLOC_THRESHOLD)
        cb = RT_ALIGN(cb, sizeof(void *)) + cLevel1 * sizeof(void *);
    pThis = (PRTHANDLETABLEINT)RTMemAllocZ(cb);
    if (!pThis)
        return VERR_NO_MEMORY;

    /*
     * Initialize it.
     */
    pThis->u32Magic = RTHANDLETABLE_MAGIC;
    pThis->fFlags = fFlags;
    pThis->uBase = uBase;
    pThis->cCur = 0;
    pThis->hSpinlock = NIL_RTSPINLOCK;
    if (cLevel1 < RTHT_LEVEL1_DYN_ALLOC_THRESHOLD)
        pThis->papvLevel1 = (void **)((uint8_t *)pThis + RT_ALIGN(sizeof(*pThis), sizeof(void *)));
    else
        pThis->papvLevel1 = NULL;
    pThis->pfnRetain = pfnRetain;
    pThis->pvRetainUser = pvUser;
    pThis->cMax = cMax;
    pThis->cCurAllocated = 0;
    pThis->cLevel1 = cLevel1 < RTHT_LEVEL1_DYN_ALLOC_THRESHOLD ? cLevel1 : 0;
    pThis->iFreeHead = NIL_RTHT_INDEX;
    pThis->iFreeTail = NIL_RTHT_INDEX;
    if (fFlags & (RTHANDLETABLE_FLAGS_LOCKED | RTHANDLETABLE_FLAGS_LOCKED_IRQ_SAFE))
    {
        int rc;
        if (fFlags & RTHANDLETABLE_FLAGS_LOCKED_IRQ_SAFE)
            rc = RTSpinlockCreate(&pThis->hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "RTHandleTableCreateEx");
        else
            rc = RTSpinlockCreate(&pThis->hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, "RTHandleTableCreateEx");
        if (RT_FAILURE(rc))
        {
            RTMemFree(pThis);
            return rc;
        }
    }

    *phHandleTable = pThis;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTHandleTableCreateEx);


RTDECL(int) RTHandleTableCreate(PRTHANDLETABLE phHandleTable)
{
    return RTHandleTableCreateEx(phHandleTable, RTHANDLETABLE_FLAGS_LOCKED, 1, 65534, NULL, NULL);
}
RT_EXPORT_SYMBOL(RTHandleTableCreate);


RTDECL(int) RTHandleTableDestroy(RTHANDLETABLE hHandleTable, PFNRTHANDLETABLEDELETE pfnDelete, void *pvUser)
{
    PRTHANDLETABLEINT   pThis;
    uint32_t            i1;
    uint32_t            i;

    /*
     * Validate input, quietly ignore the NIL handle.
     */
    if (hHandleTable == NIL_RTHANDLETABLE)
        return VINF_SUCCESS;
    pThis = (PRTHANDLETABLEINT)hHandleTable;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTHANDLETABLE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pfnDelete, VERR_INVALID_POINTER);

    /*
     * Mark the thing as invalid / deleted.
     * Then kill the lock.
     */
    rtHandleTableLock(pThis);
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTHANDLETABLE_MAGIC);
    rtHandleTableUnlock(pThis);

    if (pThis->hSpinlock != NIL_RTSPINLOCK)
    {
        rtHandleTableLock(pThis);
        rtHandleTableUnlock(pThis);

        RTSpinlockDestroy(pThis->hSpinlock);
        pThis->hSpinlock = NIL_RTSPINLOCK;
    }

    if (pfnDelete)
    {
        /*
         * Walk all the tables looking for used handles.
         */
        uint32_t cLeft = pThis->cCurAllocated;
        if (pThis->fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
        {
            for (i1 = 0; cLeft > 0 && i1 < pThis->cLevel1; i1++)
            {
                PRTHTENTRYCTX paTable = (PRTHTENTRYCTX)pThis->papvLevel1[i1];
                if (paTable)
                    for (i = 0; i < RTHT_LEVEL2_ENTRIES; i++)
                        if (!RTHT_IS_FREE(paTable[i].pvObj))
                        {
                            pfnDelete(hHandleTable, pThis->uBase + i + i1 * RTHT_LEVEL2_ENTRIES,
                                      paTable[i].pvObj, paTable[i].pvCtx, pvUser);
                            Assert(cLeft > 0);
                            cLeft--;
                        }
            }
        }
        else
        {
            for (i1 = 0; cLeft > 0 && i1 < pThis->cLevel1; i1++)
            {
                PRTHTENTRY paTable = (PRTHTENTRY)pThis->papvLevel1[i1];
                if (paTable)
                    for (i = 0; i < RTHT_LEVEL2_ENTRIES; i++)
                        if (!RTHT_IS_FREE(paTable[i].pvObj))
                        {
                            pfnDelete(hHandleTable, pThis->uBase + i + i1 * RTHT_LEVEL2_ENTRIES,
                                      paTable[i].pvObj, NULL, pvUser);
                            Assert(cLeft > 0);
                            cLeft--;
                        }
            }
        }
        Assert(!cLeft);
    }

    /*
     * Free the memory.
     */
    for (i1 = 0; i1 < pThis->cLevel1; i1++)
        if (pThis->papvLevel1[i1])
        {
            RTMemFree(pThis->papvLevel1[i1]);
            pThis->papvLevel1[i1] = NULL;
        }

    if (pThis->cMax / RTHT_LEVEL2_ENTRIES >= RTHT_LEVEL1_DYN_ALLOC_THRESHOLD)
        RTMemFree(pThis->papvLevel1);

    RTMemFree(pThis);

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTHandleTableDestroy);

