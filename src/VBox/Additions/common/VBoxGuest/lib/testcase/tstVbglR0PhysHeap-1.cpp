/* $Id: tstVbglR0PhysHeap-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Offset Based Heap.
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
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/test.h>
#include <iprt/time.h>

#define IN_TESTCASE
#define IN_RING0 /* pretend we're in ring-0 so we get access to the functions */
#include <iprt/memobj.h>
#include "../VBoxGuestR0LibInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct
{
    uint32_t cb;
    void    *pv;
} TSTHISTORYENTRY;


typedef struct TSTMEMOBJ
{
    size_t cb;
} TSTMEMOBJ;
typedef TSTMEMOBJ *PTSTMEMOBJ;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
VBGLDATA g_vbgldata;

int      g_cChunks  = 0;
size_t   g_cbChunks = 0;

/** Drop-in replacement for RTMemContAlloc   */
static int tstMemObjContAllocTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, bool fExecutable, const char *pszTag)
{
    RT_NOREF(pszTag, PhysHighest, fExecutable);

    RTTESTI_CHECK(cb > 0);

#define TST_MAX_CHUNKS 24
    if (g_cChunks < TST_MAX_CHUNKS)
    {
        PTSTMEMOBJ pMem = (PTSTMEMOBJ)RTMemAlloc(sizeof(TSTMEMOBJ) + cb);
        if (pMem)
        {
            pMem->cb = cb;

            g_cChunks++;
            g_cbChunks += cb;
            *pMemObj = (RTR0MEMOBJ)pMem;
            return VINF_SUCCESS;
        }
    }

    return VERR_NO_MEMORY;
}


/** Drop-in replacement for RTR0MemObjAddress   */
static void *tstMemObjAddress(RTR0MEMOBJ hMemObj)
{
    return (void *)((PTSTMEMOBJ)hMemObj + 1);
}


/** Drop-in replacement for RTR0MemObjGetPagePhysAddr   */
static RTHCPHYS tstMemObjGetPagePhysAddr(RTR0MEMOBJ hMemObj, uint32_t iPage)
{
    RTTESTI_CHECK(iPage == 0);

    PTSTMEMOBJ pMemObj = (PTSTMEMOBJ)hMemObj;
    uintptr_t PtrMem = (uintptr_t)(pMemObj + 1);
    RTHCPHYS Phys = (uint32_t)(uintptr_t)PtrMem ^ (UINT32_C(0xf0f0f0f0) & ~(uint32_t)PAGE_OFFSET_MASK);

    /* Avoid problematic values that won't happen in real life:  */
    if (!Phys)
        Phys = 4U << PAGE_SHIFT;
    if (UINT32_MAX - Phys < pMemObj->cb)
        Phys -= RT_ALIGN_32(pMemObj->cb, PAGE_SIZE);

    return Phys;
}


/** Drop-in replacement for RTR0MemObjFree   */
static void tstMemObjFree(RTR0MEMOBJ hMemObj, bool fFreeMappings)
{
    RT_NOREF(fFreeMappings);

    PTSTMEMOBJ pMemObj = (PTSTMEMOBJ)hMemObj;
    RTTESTI_CHECK(RT_VALID_PTR(pMemObj));
    RTTESTI_CHECK(pMemObj->cb > 0);
    RTTESTI_CHECK(g_cChunks > 0);
    g_cChunks--;
    g_cbChunks -= pMemObj->cb;
    RTMemFree(pMemObj);
}


#define RTR0MemObjAllocContTag      tstMemObjContAllocTag
#define RTR0MemObjAddress           tstMemObjAddress
#define RTR0MemObjGetPagePhysAddr   tstMemObjGetPagePhysAddr
#define RTR0MemObjFree              tstMemObjFree
#include "../VBoxGuestR0LibPhysHeap.cpp"


static void PrintStats(TSTHISTORYENTRY const *paHistory, size_t cHistory, const char *pszDesc)
{
    size_t   cbAllocated  = 0;
    unsigned cLargeBlocks = 0;
    unsigned cAllocated   = 0;
    for (size_t i = 0; i < cHistory; i++)
        if (paHistory[i].pv)
        {
            cAllocated   += 1;
            cbAllocated  += paHistory[i].cb;
            cLargeBlocks += paHistory[i].cb > _1K;
        }

    size_t const cbOverhead      = g_cChunks * sizeof(VBGLPHYSHEAPCHUNK) + cAllocated * sizeof(VBGLPHYSHEAPBLOCK);
    size_t const cbFragmentation = g_cbChunks - cbOverhead - cbAllocated;
    RTTestIPrintf(RTTESTLVL_ALWAYS,
                  "%s: %'9zu bytes in %2d chunks; %'9zu bytes in %4u blocks (%2u large)\n"
                  " => int-frag  %'9zu (%2zu.%1zu%%)    overhead %'9zu (%1zu.%02zu%%)\n",
                  pszDesc,
                  g_cbChunks, g_cChunks,
                  cbAllocated, cAllocated, cLargeBlocks,
                  cbFragmentation, cbFragmentation * 100 / g_cbChunks, (cbFragmentation * 1000 / g_cbChunks) % 10,
                  cbOverhead, cbOverhead * 100 / g_cbChunks, (cbOverhead * 10000 / g_cbChunks) % 100);
}


int main(int argc, char **argv)
{
    RT_NOREF_PV(argc); RT_NOREF_PV(argv);

    /*
     * Init runtime.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstVbglR0PhysHeap-1", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Arguments are taken to be random seeding.
     */
    uint64_t uRandSeed = RTTimeNanoTS();
    for (int i = 1; i < argc; i++)
    {
        rc = RTStrToUInt64Full(argv[i], 0, &uRandSeed);
        if (rc != VINF_SUCCESS)
        {
            RTTestIFailed("Invalid parameter: %Rrc: %s\n", rc, argv[i]);
            return RTTestSummaryAndDestroy(hTest);
        }
    }

    /*
     * Create a heap.
     */
    RTTestSub(hTest, "Basics");
    RTTESTI_CHECK_RC(rc = VbglR0PhysHeapInit(NIL_RTHCPHYS), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);
    RTTESTI_CHECK_RC_OK(VbglR0PhysHeapCheck(NULL));

#define CHECK_PHYS_ADDR(a_pv) do { \
        uint32_t const uPhys = VbglR0PhysHeapGetPhysAddr(a_pv); \
        if (uPhys == 0 || uPhys == UINT32_MAX || (uPhys & PAGE_OFFSET_MASK) != ((uintptr_t)(a_pv) & PAGE_OFFSET_MASK)) \
            RTTestIFailed("line %u: %s=%p: uPhys=%#x\n", __LINE__, #a_pv, (a_pv), uPhys); \
    } while (0)

    /*
     * Try allocate.
     */
    static struct TstPhysHeapOps
    {
        uint32_t    cb;
        unsigned    iFreeOrder;
        void       *pvAlloc;
    } s_aOps[] =
    {
        {        16,  0,  NULL },  // 0
        {        16,  1,  NULL },
        {        16,  2,  NULL },
        {        16,  5,  NULL },
        {        16,  4,  NULL },
        {        32,  3,  NULL },  // 5
        {        31,  6,  NULL },
        {      1024,  8,  NULL },
        {      1024, 10,  NULL },
        {      1024, 12,  NULL },
        { PAGE_SIZE, 13,  NULL },  // 10
        {      1024,  9,  NULL },
        { PAGE_SIZE, 11,  NULL },
        { PAGE_SIZE, 14,  NULL },
        {        16, 15,  NULL },
        {         9,  7,  NULL },  // 15
        {        16,  7,  NULL },
        {        36,  7,  NULL },
        {        16,  7,  NULL },
        {     12344,  7,  NULL },
        {        50,  7,  NULL },  // 20
        {        16,  7,  NULL },
    };
    uint32_t i;
    //RTHeapOffsetDump(Heap, (PFNRTHEAPOFFSETPRINTF)(uintptr_t)RTPrintf); /** @todo Add some detail info output with a signature identical to RTPrintf. */
    //size_t cbBefore = VbglR0PhysHeapGetFreeSize();
    static char const s_szFill[] = "01234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    /* allocate */
    for (i = 0; i < RT_ELEMENTS(s_aOps); i++)
    {
        s_aOps[i].pvAlloc = VbglR0PhysHeapAlloc(s_aOps[i].cb);
        RTTESTI_CHECK_MSG(s_aOps[i].pvAlloc, ("VbglR0PhysHeapAlloc(%#x) -> NULL i=%d\n", s_aOps[i].cb, i));
        if (!s_aOps[i].pvAlloc)
            return RTTestSummaryAndDestroy(hTest);

        memset(s_aOps[i].pvAlloc, s_szFill[i], s_aOps[i].cb);
        RTTESTI_CHECK_MSG(RT_ALIGN_P(s_aOps[i].pvAlloc, sizeof(void *)) == s_aOps[i].pvAlloc,
                          ("VbglR0PhysHeapAlloc(%#x) -> %p\n", s_aOps[i].cb, i));

        CHECK_PHYS_ADDR(s_aOps[i].pvAlloc);

        /* Check heap integrity: */
        RTTESTI_CHECK_RC_OK(VbglR0PhysHeapCheck(NULL));
    }

    /* free and allocate the same node again. */
    for (i = 0; i < RT_ELEMENTS(s_aOps); i++)
    {
        if (!s_aOps[i].pvAlloc)
            continue;
        //RTPrintf("debug: i=%d pv=%#x cb=%#zx align=%#zx cbReal=%#zx\n", i, s_aOps[i].pvAlloc,
        //         s_aOps[i].cb, s_aOps[i].uAlignment, RTHeapOffsetSize(Heap, s_aOps[i].pvAlloc));
        size_t cbBeforeSub = VbglR0PhysHeapGetFreeSize();
        VbglR0PhysHeapFree(s_aOps[i].pvAlloc);
        size_t cbAfterSubFree = VbglR0PhysHeapGetFreeSize();
        RTTESTI_CHECK_RC_OK(VbglR0PhysHeapCheck(NULL));

        void *pv;
        pv = VbglR0PhysHeapAlloc(s_aOps[i].cb);
        RTTESTI_CHECK_MSG(pv, ("VbglR0PhysHeapAlloc(%#x) -> NULL i=%d\n", s_aOps[i].cb, i));
        if (!pv)
            return RTTestSummaryAndDestroy(hTest);
        CHECK_PHYS_ADDR(pv);
        RTTESTI_CHECK_RC_OK(VbglR0PhysHeapCheck(NULL));

        //RTPrintf("debug: i=%d pv=%p cbReal=%#zx cbBeforeSub=%#zx cbAfterSubFree=%#zx cbAfterSubAlloc=%#zx \n", i, pv, RTHeapOffsetSize(Heap, pv),
        //         cbBeforeSub, cbAfterSubFree, VbglR0PhysHeapGetFreeSize());

        if (pv != s_aOps[i].pvAlloc)
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Warning: Free+Alloc returned different address. new=%p old=%p i=%d\n", pv, s_aOps[i].pvAlloc, i);
        s_aOps[i].pvAlloc = pv;
        size_t cbAfterSubAlloc = VbglR0PhysHeapGetFreeSize();
        if (cbBeforeSub != cbAfterSubAlloc)
        {
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Warning: cbBeforeSub=%#zx cbAfterSubFree=%#zx cbAfterSubAlloc=%#zx. i=%d\n",
                          cbBeforeSub, cbAfterSubFree, cbAfterSubAlloc, i);
            //return 1; - won't work correctly until we start creating free block instead of donating memory on alignment.
        }
    }

    VbglR0PhysHeapTerminate();
    RTTESTI_CHECK_MSG(g_cChunks == 0, ("g_cChunks=%d\n", g_cChunks));


    /*
     * Use random allocation pattern
     */
    RTTestSub(hTest, "Random Test");
    RTTESTI_CHECK_RC(rc = VbglR0PhysHeapInit(NIL_RTHCPHYS), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);

    RTRAND hRand;
    RTTESTI_CHECK_RC(rc = RTRandAdvCreateParkMiller(&hRand), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);
    RTRandAdvSeed(hRand, uRandSeed);
    RTTestValue(hTest, "RandSeed", uRandSeed, RTTESTUNIT_NONE);

    static TSTHISTORYENTRY s_aHistory[3072];
    RT_ZERO(s_aHistory);

    for (unsigned iTest = 0; iTest < 131072; iTest++)
    {
        i = RTRandAdvU32Ex(hRand, 0, RT_ELEMENTS(s_aHistory) - 1);
        if (!s_aHistory[i].pv)
        {
            s_aHistory[i].cb = RTRandAdvU32Ex(hRand, 8, 1024);
            s_aHistory[i].pv = VbglR0PhysHeapAlloc(s_aHistory[i].cb);
            if (!s_aHistory[i].pv)
            {
                s_aHistory[i].cb = 9;
                s_aHistory[i].pv = VbglR0PhysHeapAlloc(s_aHistory[i].cb);
            }
            if (s_aHistory[i].pv)
            {
                memset(s_aHistory[i].pv, 0xbb, s_aHistory[i].cb);
                CHECK_PHYS_ADDR(s_aHistory[i].pv);
            }
        }
        else
        {
            VbglR0PhysHeapFree(s_aHistory[i].pv);
            s_aHistory[i].pv = NULL;
        }

#if 1
        /* Check heap integrity: */
        RTTESTI_CHECK_RC_OK(VbglR0PhysHeapCheck(NULL));
        int cChunks = 0;
        for (VBGLPHYSHEAPCHUNK *pCurChunk = g_vbgldata.pChunkHead; pCurChunk; pCurChunk = pCurChunk->pNext)
            cChunks++;
        RTTESTI_CHECK_MSG(cChunks == g_cChunks, ("g_cChunks=%u, but only %u chunks in the list!\n", g_cChunks, cChunks));
#endif

        if ((iTest % 7777) == 7776)
        {
            /* exhaust the heap */
            PrintStats(s_aHistory, RT_ELEMENTS(s_aHistory), "Exhaust-pre ");

            for (i = 0; i < RT_ELEMENTS(s_aHistory) && (VbglR0PhysHeapGetFreeSize() >= 256 || g_cChunks < TST_MAX_CHUNKS); i++)
                if (!s_aHistory[i].pv)
                {
                    s_aHistory[i].cb = RTRandAdvU32Ex(hRand, VBGL_PH_CHUNKSIZE / 8, VBGL_PH_CHUNKSIZE / 2 + VBGL_PH_CHUNKSIZE / 4);
                    s_aHistory[i].pv = VbglR0PhysHeapAlloc(s_aHistory[i].cb);
                    if (s_aHistory[i].pv)
                    {
                        memset(s_aHistory[i].pv, 0x55, s_aHistory[i].cb);
                        CHECK_PHYS_ADDR(s_aHistory[i].pv);
                    }
                }

            size_t cbFree = VbglR0PhysHeapGetFreeSize();
            if (cbFree)
                for (i = 0; i < RT_ELEMENTS(s_aHistory); i++)
                    if (!s_aHistory[i].pv)
                    {
                        s_aHistory[i].cb = RTRandAdvU32Ex(hRand, 1, (uint32_t)cbFree);
                        s_aHistory[i].pv = VbglR0PhysHeapAlloc(s_aHistory[i].cb);
                        while (s_aHistory[i].pv == NULL && s_aHistory[i].cb > 2)
                        {
                            s_aHistory[i].cb >>= 1;
                            s_aHistory[i].pv = VbglR0PhysHeapAlloc(s_aHistory[i].cb);
                        }
                        if (s_aHistory[i].pv)
                        {
                            memset(s_aHistory[i].pv, 0x55, s_aHistory[i].cb);
                            CHECK_PHYS_ADDR(s_aHistory[i].pv);
                        }

                        cbFree = VbglR0PhysHeapGetFreeSize();
                        if (!cbFree)
                            break;
                    }

            RTTESTI_CHECK_MSG(VbglR0PhysHeapGetFreeSize() == 0, ("%zu\n", VbglR0PhysHeapGetFreeSize()));
            PrintStats(s_aHistory, RT_ELEMENTS(s_aHistory), "Exhaust-post");
        }
        else if ((iTest % 7777) == 1111)
        {
            /* free all */
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Free-all-pre:  cFreeBlocks=%u cAllocedBlocks=%u in %u chunk(s)\n",
                          g_vbgldata.cFreeBlocks, g_vbgldata.cBlocks - g_vbgldata.cFreeBlocks, g_cChunks);
            for (i = 0; i < RT_ELEMENTS(s_aHistory); i++)
            {
                VbglR0PhysHeapFree(s_aHistory[i].pv);
                s_aHistory[i].pv = NULL;
            }
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Free-all-post: cFreeBlocks=%u in %u chunk(s)\n", g_vbgldata.cFreeBlocks, g_cChunks);
            RTTESTI_CHECK_MSG(g_cChunks == 1, ("g_cChunks=%d\n", g_cChunks));
            RTTESTI_CHECK_MSG(g_vbgldata.cFreeBlocks == g_vbgldata.cBlocks,
                              ("g_vbgldata.cFreeBlocks=%d cBlocks=%d\n", g_vbgldata.cFreeBlocks, g_vbgldata.cBlocks));

            //size_t cbAfterRand = VbglR0PhysHeapGetFreeSize();
            //RTTESTI_CHECK_MSG(cbAfterRand == cbAfter, ("cbAfterRand=%zu cbAfter=%zu\n", cbAfterRand, cbAfter));
        }
    }

    /* free the rest. */
    for (i = 0; i < RT_ELEMENTS(s_aHistory); i++)
    {
        VbglR0PhysHeapFree(s_aHistory[i].pv);
        s_aHistory[i].pv = NULL;
    }

    RTTESTI_CHECK_MSG(g_cChunks == 1, ("g_cChunks=%d\n", g_cChunks));

    VbglR0PhysHeapTerminate();
    RTTESTI_CHECK_MSG(g_cChunks == 0, ("g_cChunks=%d\n", g_cChunks));

    RTTESTI_CHECK_RC(rc = RTRandAdvDestroy(hRand), VINF_SUCCESS);
    return RTTestSummaryAndDestroy(hTest);
}

