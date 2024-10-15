/* $Id: tstRTHeapSimple.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Simple Heap.
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
#include <iprt/heap.h>
#include <iprt/initterm.h>
#include <iprt/errcore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/test.h>


int main(int argc, char **argv)
{
    RT_NOREF_PV(argc); RT_NOREF_PV(argv);

    /*
     * Init runtime.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTHeapSimple", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Create a heap.
     */
    RTTestSub(hTest, "Basics");
    static uint8_t s_abMem[128*1024];
    RTHEAPSIMPLE Heap;
    RTTESTI_CHECK_RC(rc = RTHeapSimpleInit(&Heap, &s_abMem[1], sizeof(s_abMem) - 1), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);

    /*
     * Try allocate.
     */
    static struct TstHeapSimpleOps
    {
        size_t      cb;
        unsigned    uAlignment;
        void       *pvAlloc;
        unsigned    iFreeOrder;
    } s_aOps[] =
    {
        {        16,          0,    NULL,  0 },  // 0
        {        16,          4,    NULL,  1 },
        {        16,          8,    NULL,  2 },
        {        16,         16,    NULL,  5 },
        {        16,         32,    NULL,  4 },
        {        32,          0,    NULL,  3 },  // 5
        {        31,          0,    NULL,  6 },
        {      1024,          0,    NULL,  8 },
        {      1024,         32,    NULL, 10 },
        {      1024,         32,    NULL, 12 },
        { PAGE_SIZE,  PAGE_SIZE,    NULL, 13 },  // 10
        {      1024,         32,    NULL,  9 },
        { PAGE_SIZE,         32,    NULL, 11 },
        { PAGE_SIZE,  PAGE_SIZE,    NULL, 14 },
        {        16,          0,    NULL, 15 },
        {        9,           0,    NULL,  7 },  // 15
        {        16,          0,    NULL,  7 },
        {        36,          0,    NULL,  7 },
        {        16,          0,    NULL,  7 },
        {     12344,          0,    NULL,  7 },
        {        50,          0,    NULL,  7 },  // 20
        {        16,          0,    NULL,  7 },
    };
    unsigned i;
    RTHeapSimpleDump(Heap, (PFNRTHEAPSIMPLEPRINTF)(uintptr_t)RTPrintf); /** @todo Add some detail info output with a signature identical to RTPrintf. */
    size_t cbBefore = RTHeapSimpleGetFreeSize(Heap);
    static char szFill[] = "01234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    /* allocate */
    for (i = 0; i < RT_ELEMENTS(s_aOps); i++)
    {
        s_aOps[i].pvAlloc = RTHeapSimpleAlloc(Heap, s_aOps[i].cb, s_aOps[i].uAlignment);
        RTTESTI_CHECK_MSG(s_aOps[i].pvAlloc, ("RTHeapSimpleAlloc(%p, %#x, %#x,) -> NULL i=%d\n", (void *)Heap, s_aOps[i].cb, s_aOps[i].uAlignment, i));
        if (!s_aOps[i].pvAlloc)
            return RTTestSummaryAndDestroy(hTest);

        memset(s_aOps[i].pvAlloc, szFill[i], s_aOps[i].cb);
        RTTESTI_CHECK_MSG(RT_ALIGN_P(s_aOps[i].pvAlloc, (s_aOps[i].uAlignment ? s_aOps[i].uAlignment : 8)) == s_aOps[i].pvAlloc,
                          ("RTHeapSimpleAlloc(%p, %#x, %#x,) -> %p\n", (void *)Heap, s_aOps[i].cb, s_aOps[i].uAlignment, i));
        if (!s_aOps[i].pvAlloc)
            return RTTestSummaryAndDestroy(hTest);
    }

    /* free and allocate the same node again. */
    for (i = 0; i < RT_ELEMENTS(s_aOps); i++)
    {
        if (!s_aOps[i].pvAlloc)
            continue;
        //RTPrintf("debug: i=%d pv=%#x cb=%#zx align=%#zx cbReal=%#zx\n", i, s_aOps[i].pvAlloc,
        //         s_aOps[i].cb, s_aOps[i].uAlignment, RTHeapSimpleSize(Heap, s_aOps[i].pvAlloc));
        size_t cbBeforeSub = RTHeapSimpleGetFreeSize(Heap);
        RTHeapSimpleFree(Heap, s_aOps[i].pvAlloc);
        size_t cbAfterSubFree = RTHeapSimpleGetFreeSize(Heap);

        void *pv;
        pv = RTHeapSimpleAlloc(Heap, s_aOps[i].cb, s_aOps[i].uAlignment);
        RTTESTI_CHECK_MSG(pv, ("RTHeapSimpleAlloc(%p, %#x, %#x,) -> NULL i=%d\n", (void *)Heap, s_aOps[i].cb, s_aOps[i].uAlignment, i));
        if (!pv)
            return RTTestSummaryAndDestroy(hTest);
        //RTPrintf("debug: i=%d pv=%p cbReal=%#zx cbBeforeSub=%#zx cbAfterSubFree=%#zx cbAfterSubAlloc=%#zx \n", i, pv, RTHeapSimpleSize(Heap, pv),
        //         cbBeforeSub, cbAfterSubFree, RTHeapSimpleGetFreeSize(Heap));
        if (pv != s_aOps[i].pvAlloc)
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Warning: Free+Alloc returned different address. new=%p old=%p i=%d\n", pv, s_aOps[i].pvAlloc, i);
        s_aOps[i].pvAlloc = pv;
        size_t cbAfterSubAlloc = RTHeapSimpleGetFreeSize(Heap);
        if (cbBeforeSub != cbAfterSubAlloc)
        {
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Warning: cbBeforeSub=%#zx cbAfterSubFree=%#zx cbAfterSubAlloc=%#zx. i=%d\n",
                          cbBeforeSub, cbAfterSubFree, cbAfterSubAlloc, i);
            //return 1; - won't work correctly until we start creating free block instead of donating memory on alignment.
        }
    }

    /* make a copy of the heap and the to-be-freed list. */
    static uint8_t s_abMemCopy[sizeof(s_abMem)];
    memcpy(s_abMemCopy, s_abMem, sizeof(s_abMem));
    uintptr_t    offDelta  = (uintptr_t)&s_abMemCopy[0] - (uintptr_t)&s_abMem[0];
    RTHEAPSIMPLE hHeapCopy = (RTHEAPSIMPLE)((uintptr_t)Heap + offDelta);
    static struct TstHeapSimpleOps s_aOpsCopy[RT_ELEMENTS(s_aOps)];
    memcpy(&s_aOpsCopy[0], &s_aOps[0], sizeof(s_aOps));

    /* free it in a specific order. */
    int cFreed = 0;
    for (i = 0; i < RT_ELEMENTS(s_aOps); i++)
    {
        unsigned j;
        for (j = 0; j < RT_ELEMENTS(s_aOps); j++)
        {
            if (    s_aOps[j].iFreeOrder != i
                ||  !s_aOps[j].pvAlloc)
                continue;
            //RTPrintf("j=%d i=%d free=%d cb=%d pv=%p\n", j, i, RTHeapSimpleGetFreeSize(Heap), s_aOps[j].cb, s_aOps[j].pvAlloc);
            RTHeapSimpleFree(Heap, s_aOps[j].pvAlloc);
            s_aOps[j].pvAlloc = NULL;
            cFreed++;
        }
    }
    RTTESTI_CHECK(cFreed == RT_ELEMENTS(s_aOps));
    RTTestIPrintf(RTTESTLVL_ALWAYS, "i=done free=%d\n", RTHeapSimpleGetFreeSize(Heap));

    /* check that we're back at the right amount of free memory. */
    size_t cbAfter = RTHeapSimpleGetFreeSize(Heap);
    if (cbBefore != cbAfter)
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS,
                      "Warning: Either we've split out an alignment chunk at the start, or we've got\n"
                      "         an alloc/free accounting bug: cbBefore=%d cbAfter=%d\n", cbBefore, cbAfter);
        RTHeapSimpleDump(Heap, (PFNRTHEAPSIMPLEPRINTF)(uintptr_t)RTPrintf);
    }

    /* relocate and free the bits in heap2 now. */
    RTTestSub(hTest, "RTHeapSimpleRelocate");
    rc = RTHeapSimpleRelocate(hHeapCopy, offDelta);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        /* free it in a specific order. */
        int cFreed2 = 0;
        for (i = 0; i < RT_ELEMENTS(s_aOpsCopy); i++)
        {
            unsigned j;
            for (j = 0; j < RT_ELEMENTS(s_aOpsCopy); j++)
            {
                if (    s_aOpsCopy[j].iFreeOrder != i
                    ||  !s_aOpsCopy[j].pvAlloc)
                    continue;
                //RTPrintf("j=%d i=%d free=%d cb=%d pv=%p\n", j, i, RTHeapSimpleGetFreeSize(hHeapCopy), s_aOpsCopy[j].cb, s_aOpsCopy[j].pvAlloc);
                RTHeapSimpleFree(hHeapCopy, (uint8_t *)s_aOpsCopy[j].pvAlloc + offDelta);
                s_aOpsCopy[j].pvAlloc = NULL;
                cFreed2++;
            }
        }
        RTTESTI_CHECK(cFreed2 == RT_ELEMENTS(s_aOpsCopy));

        /* check that we're back at the right amount of free memory. */
        size_t cbAfterCopy = RTHeapSimpleGetFreeSize(hHeapCopy);
        RTTESTI_CHECK_MSG(cbAfterCopy == cbAfter, ("cbAfterCopy=%zu cbAfter=%zu\n", cbAfterCopy, cbAfter));
    }


    return RTTestSummaryAndDestroy(hTest);
}

