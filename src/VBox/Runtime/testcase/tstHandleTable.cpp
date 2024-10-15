/* $Id: tstHandleTable.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Handle Tables.
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
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/alloca.h>
#include <iprt/thread.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static unsigned g_cErrors;

static DECLCALLBACK(void) tstHandleTableTest1Delete(RTHANDLETABLE hHandleTable, uint32_t h, void *pvObj, void *pvCtx, void *pvUser)
{
    uint32_t *pcCalls = (uint32_t *)pvUser;
    (*pcCalls)++;
    RT_NOREF_PV(hHandleTable); RT_NOREF_PV(h); RT_NOREF_PV(pvCtx); RT_NOREF_PV(pvObj);
}

static DECLCALLBACK(int) tstHandleTableTest1Retain(RTHANDLETABLE hHandleTable, void *pvObj, void *pvCtx, void *pvUser)
{
    uint32_t *pcCalls = (uint32_t *)pvUser;
    (*pcCalls)++;
    RT_NOREF_PV(hHandleTable); RT_NOREF_PV(pvCtx); RT_NOREF_PV(pvObj);
    return VINF_SUCCESS;
}

static int tstHandleTableTest1(uint32_t uBase, uint32_t cMax, uint32_t cDelta, uint32_t cUnitsPerDot, bool fCallbacks, uint32_t fFlags)
{
    const char *pszWithCtx = fFlags & RTHANDLETABLE_FLAGS_CONTEXT ? "WithCtx" : "";
    uint32_t cRetainerCalls = 0;
    int rc;

    RTPrintf("tstHandleTable: TESTING RTHandleTableCreateEx(, 0");
    if (fFlags & RTHANDLETABLE_FLAGS_LOCKED)    RTPrintf(" | LOCKED");
    if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)   RTPrintf(" | CONTEXT");
    RTPrintf(", %#x, %#x,,)...\n", uBase, cMax);

    RTHANDLETABLE hHT;
    rc = RTHandleTableCreateEx(&hHT, fFlags, uBase, cMax,
                               fCallbacks ? tstHandleTableTest1Retain : NULL,
                               fCallbacks ? &cRetainerCalls : NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("\ntstHandleTable: FAILURE - RTHandleTableCreateEx failed, %Rrc!\n", rc);
        return 1;
    }

    /* fill it */
    RTPrintf("tstHandleTable: TESTING   RTHandleTableAlloc%s..", pszWithCtx); RTStrmFlush(g_pStdOut);
    uint32_t i = uBase;
    for (;; i++)
    {
        uint32_t h;
        if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
            rc = RTHandleTableAllocWithCtx(hHT, (void *)((uintptr_t)&i + (uintptr_t)i * 4), NULL, &h);
        else
            rc = RTHandleTableAlloc(hHT, (void *)((uintptr_t)&i + (uintptr_t)i * 4), &h);
        if (RT_SUCCESS(rc))
        {
            if (h != i)
            {
                RTPrintf("\ntstHandleTable: FAILURE (%d) - h=%d, expected %d!\n", __LINE__, h, i);
                g_cErrors++;
            }
        }
        else if (rc == VERR_NO_MORE_HANDLES)
        {
            if (i < cMax)
            {
                RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, expected > 65534!\n", __LINE__, i);
                g_cErrors++;
            }
            break;
        }
        else
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, rc=%Rrc!\n", __LINE__, i, rc);
            g_cErrors++;
        }
        if (!(i % cUnitsPerDot))
        {
            RTPrintf(".");
            RTStrmFlush(g_pStdOut);
        }
    }
    uint32_t const c = i;
    RTPrintf(" c=%#x\n", c);
    if (fCallbacks && cRetainerCalls != 0)
    {
        RTPrintf("tstHandleTable: FAILURE (%d) - cRetainerCalls=%#x expected 0!\n", __LINE__, cRetainerCalls);
        g_cErrors++;
    }

    /* look up all the entries */
    RTPrintf("tstHandleTable: TESTING   RTHandleTableLookup%s..", pszWithCtx); RTStrmFlush(g_pStdOut);
    cRetainerCalls = 0;
    for (i = uBase; i < c; i++)
    {
        void *pvExpect = (void *)((uintptr_t)&i + (uintptr_t)i * 4);
        void *pvObj;
        if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
            pvObj = RTHandleTableLookupWithCtx(hHT, i, NULL);
        else
            pvObj = RTHandleTableLookup(hHT, i);
        if (!pvObj)
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, RTHandleTableLookup%s failed!\n", __LINE__, i, pszWithCtx);
            g_cErrors++;
        }
        else if (pvObj != pvExpect)
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, pvObj=%p expected %p\n", __LINE__, i, pvObj, pvExpect);
            g_cErrors++;
        }
        if (!(i % cUnitsPerDot))
        {
            RTPrintf(".");
            RTStrmFlush(g_pStdOut);
        }
    }
    RTPrintf("\n");
    if (fCallbacks && cRetainerCalls != c - uBase)
    {
        RTPrintf("tstHandleTable: FAILURE (%d) - cRetainerCalls=%#x expected %#x!\n", __LINE__, cRetainerCalls, c - uBase);
        g_cErrors++;
    }

    /* remove all the entries (in order) */
    RTPrintf("tstHandleTable: TESTING   RTHandleTableFree%s..", pszWithCtx); RTStrmFlush(g_pStdOut);
    cRetainerCalls = 0;
    for (i = uBase; i < c; i++)
    {
        void *pvExpect = (void *)((uintptr_t)&i + (uintptr_t)i * 4);
        void *pvObj;
        if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
            pvObj = RTHandleTableFreeWithCtx(hHT, i, NULL);
        else
            pvObj = RTHandleTableFree(hHT, i);
        if (!pvObj)
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, RTHandleTableLookup%s failed!\n", __LINE__, i, pszWithCtx);
            g_cErrors++;
        }
        else if (pvObj != pvExpect)
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, pvObj=%p expected %p\n", __LINE__, i, pvObj, pvExpect);
            g_cErrors++;
        }
        else if (   fFlags & RTHANDLETABLE_FLAGS_CONTEXT
                 ?  RTHandleTableLookupWithCtx(hHT, i, NULL)
                 :  RTHandleTableLookup(hHT, i))
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, RTHandleTableLookup%s succeeded after free!\n", __LINE__, i, pszWithCtx);
            g_cErrors++;
        }
        if (!(i % cUnitsPerDot))
        {
            RTPrintf(".");
            RTStrmFlush(g_pStdOut);
        }
    }
    RTPrintf("\n");
    if (fCallbacks && cRetainerCalls != c - uBase)
    {
        RTPrintf("tstHandleTable: FAILURE (%d) - cRetainerCalls=%#x expected %#x!\n", __LINE__, cRetainerCalls, c - uBase);
        g_cErrors++;
    }

    /* do a mix of alloc, lookup and free where there is a constant of cDelta handles in the table. */
    RTPrintf("tstHandleTable: TESTING   Alloc,Lookup,Free mix [cDelta=%#x]..", cDelta); RTStrmFlush(g_pStdOut);
    for (i = uBase; i < c * 2; i++)
    {
        /* alloc */
        uint32_t hExpect = ((i - uBase) % (c - uBase)) + uBase;
        uint32_t h;
        if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
            rc = RTHandleTableAllocWithCtx(hHT, (void *)((uintptr_t)&i + (uintptr_t)hExpect * 4), NULL, &h);
        else
            rc = RTHandleTableAlloc(hHT, (void *)((uintptr_t)&i + (uintptr_t)hExpect * 4), &h);
        if (RT_FAILURE(rc))
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, RTHandleTableAlloc%s: rc=%Rrc!\n", __LINE__, i, pszWithCtx, rc);
            g_cErrors++;
        }
        else if (h != hExpect)
        {
            RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, RTHandleTableAlloc%s: h=%u hExpect=%u! - abort sub-test\n", __LINE__, i, pszWithCtx, h, hExpect);
            g_cErrors++;
            break;
        }

        if (i >= cDelta + uBase)
        {
            /* lookup */
            for (uint32_t j = i - cDelta; j <= i; j++)
            {
                uint32_t hLookup = ((j - uBase) % (c - uBase)) + uBase;
                void *pvExpect = (void *)((uintptr_t)&i + (uintptr_t)hLookup * 4);
                void *pvObj;
                if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
                    pvObj = RTHandleTableLookupWithCtx(hHT, hLookup, NULL);
                else
                    pvObj = RTHandleTableLookup(hHT, hLookup);
                if (pvObj != pvExpect)
                {
                    RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, j=%d, RTHandleTableLookup%s(,%u,): pvObj=%p expected %p!\n",
                             __LINE__, i, j, pszWithCtx, hLookup, pvObj, pvExpect);
                    g_cErrors++;
                }
                else if (   (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
                         &&  RTHandleTableLookupWithCtx(hHT, hLookup, &i))
                {
                    RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, j=%d, RTHandleTableLookupWithCtx: succeeded with bad context\n",
                             __LINE__, i, j);
                    g_cErrors++;
                }
            }

            /* free */
            uint32_t hFree = ((i - uBase - cDelta) % (c - uBase)) + uBase;
            void *pvExpect = (void *)((uintptr_t)&i + (uintptr_t)hFree * 4);
            void *pvObj;
            if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
                pvObj = RTHandleTableFreeWithCtx(hHT, hFree, NULL);
            else
                pvObj = RTHandleTableFree(hHT, hFree);
            if (pvObj != pvExpect)
            {
                RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, RTHandleTableFree%s: pvObj=%p expected %p!\n",
                         __LINE__, i, pszWithCtx, pvObj, pvExpect);
                g_cErrors++;
            }
            else if (fFlags & RTHANDLETABLE_FLAGS_CONTEXT
                     ?      RTHandleTableLookupWithCtx(hHT, hFree, NULL)
                        ||  RTHandleTableFreeWithCtx(hHT, hFree, NULL)
                     :      RTHandleTableLookup(hHT, hFree)
                        ||  RTHandleTableFree(hHT, hFree))
            {
                RTPrintf("\ntstHandleTable: FAILURE (%d) - i=%d, RTHandleTableLookup/Free%s: succeeded after free\n",
                         __LINE__, i, pszWithCtx);
                g_cErrors++;
            }
        }
        if (!(i % (cUnitsPerDot * 2)))
        {
            RTPrintf(".");
            RTStrmFlush(g_pStdOut);
        }
    }
    RTPrintf("\n");

    /* finally, destroy the table (note that there are 128 entries in it). */
    cRetainerCalls = 0;
    uint32_t cDeleteCalls = 0;
    rc = RTHandleTableDestroy(hHT,
                              fCallbacks ? tstHandleTableTest1Delete : NULL,
                              fCallbacks ? &cDeleteCalls : NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstHandleTable: FAILURE (%d) - RTHandleTableDestroy failed, %Rrc!\n", __LINE__, rc);
        g_cErrors++;
    }

    return 0;
}


typedef struct TSTHTTEST2ARGS
{
    /** The handle table. */
    RTHANDLETABLE hHT;
    /** The thread handle. */
    RTTHREAD hThread;
    /** Thread index. */
    uint32_t iThread;
    /** the max number of handles the thread should allocate. */
    uint32_t cMax;
} TSTHTTEST2ARGS, *PTSTHTTEST2ARGS;


static DECLCALLBACK(int) tstHandleTableTest2Thread(RTTHREAD hThread, void *pvUser)
{
    RTHANDLETABLE const hHT = ((PTSTHTTEST2ARGS)pvUser)->hHT;
    uint32_t const iThread = ((PTSTHTTEST2ARGS)pvUser)->iThread;
    uint32_t const cMax = ((PTSTHTTEST2ARGS)pvUser)->cMax;
    uint32_t *pah = (uint32_t *)RTMemAllocZ(sizeof(uint32_t) * cMax);
    if (!pah)
    {
        RTPrintf("tstHandleTable: FAILURE (%d) - failed to allocate %zu bytes\n", __LINE__, sizeof(uint32_t) * cMax);
        return VERR_NO_MEMORY;
    }

    /*
     * Allocate our quota.
     */
    for (uint32_t i = 0; i < cMax; i++)
    {
        int rc = RTHandleTableAllocWithCtx(hHT, pvUser, hThread, &pah[i]);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstHandleTable: FAILURE (%d) - t=%d i=%d: RTHandleTableAllocWithCtx failed, rc=%Rrc\n",
                     __LINE__, iThread, i, rc);
            return rc;
        }
    }

    /*
     * Look them up.
     */
    for (uint32_t i = 0; i < cMax; i++)
    {
        void *pvObj = RTHandleTableLookupWithCtx(hHT, pah[i], hThread);
        if (pvObj != pvUser)
        {
            RTPrintf("tstHandleTable: FAILURE (%d) - t=%d i=%d: RTHandleTableLookupWithCtx failed, pvObj=%p\n",
                     __LINE__, iThread, i, pvObj);
            return VERR_INTERNAL_ERROR;
        }
    }

    /*
     * Free them all.
     */
    for (uint32_t i = 0; i < cMax; i++)
    {
        void *pvObj = RTHandleTableFreeWithCtx(hHT, pah[i], hThread);
        if (pvObj != pvUser)
        {
            RTPrintf("tstHandleTable: FAILURE (%d) - t=%d i=%d: RTHandleTableFreeWithCtx failed, pvObj=%p\n",
                     __LINE__, iThread, i, pvObj);
            return VERR_INTERNAL_ERROR;
        }
    }

    RTMemFree(pah);
    return VINF_SUCCESS;
}

static int tstHandleTableTest2(uint32_t uBase, uint32_t cMax, uint32_t cThreads)
{
    /*
     * Create the table.
     */
    RTPrintf("tstHandleTable: TESTING %u threads: uBase=%u, cMax=%u\n", cThreads, uBase, cMax);
    RTHANDLETABLE hHT;
    int rc = RTHandleTableCreateEx(&hHT, RTHANDLETABLE_FLAGS_LOCKED | RTHANDLETABLE_FLAGS_CONTEXT, uBase, cMax, NULL, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstHandleTable: FAILURE - RTHandleTableCreateEx failed, %Rrc!\n", rc);
        return 1;
    }
    /// @todo there must be a race somewhere in the thread code, I keep hitting a duplicate insert id here...
    // Or perhaps it just barcelona B2 bugs?
    RTThreadSleep(50);

    /*
     * Spawn the threads.
     */
    PTSTHTTEST2ARGS paThread = (PTSTHTTEST2ARGS)alloca(sizeof(*paThread) * cThreads);
    for (uint32_t i = 0; i < cThreads; i++)
    {
        paThread[i].hHT = hHT;
        paThread[i].hThread = NIL_RTTHREAD;
        paThread[i].iThread = i;
        paThread[i].cMax = cMax / cThreads;
    }
    for (uint32_t i = 0; i < cThreads; i++)
    {
        char szName[32];
        RTStrPrintf(szName, sizeof(szName), "TEST2-%x/%x", i, cMax);
        rc = RTThreadCreate(&paThread[i].hThread, tstHandleTableTest2Thread, &paThread[i], 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, szName);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstHandleTable: FAILURE - RTThreadCreate failed, %Rrc!\n", rc);
            g_cErrors++;
            break;
        }
    }

    /*
     * Wait for them to complete.
     */
    uint32_t cRunning = cThreads;
    do /** @todo Remove when RTSemEventWait (linux) has been fixed. */
    {
        if (cRunning != cThreads)
            RTThreadSleep(10);
        cRunning = 0;
        for (uint32_t i = 0; i < cThreads; i++)
            if (paThread[i].hThread != NIL_RTTHREAD)
            {
                rc = RTThreadWait(paThread[i].hThread, RT_INDEFINITE_WAIT, NULL);
                if (RT_SUCCESS(rc))
                    paThread[i].hThread = NIL_RTTHREAD;
                else
                    cRunning++;
            }
    } while (cRunning);

    /*
     * Destroy the handle table.
     */
    rc = RTHandleTableDestroy(hHT, NULL, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstHandleTable: FAILURE (%d) - RTHandleTableDestroy failed, %Rrc!\n", __LINE__, rc);
        g_cErrors++;
    }

    return 0;
}


int main(int argc, char **argv)
{
    /*
     * Init the runtime and parse the arguments.
     */
    RTR3InitExe(argc, &argv, 0);

    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--base",         'b', RTGETOPT_REQ_UINT32 },
        { "--max",          'm', RTGETOPT_REQ_UINT32 },
        { "--threads",      't', RTGETOPT_REQ_UINT32 },
    };

    uint32_t uBase    = 0;
    uint32_t cMax     = 0;
    uint32_t cThreads = 0;

    int ch;
    RTGETOPTUNION Value;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &Value)))
        switch (ch)
        {
            case 'b':
                uBase = Value.u32;
                break;

            case 'm':
                cMax = Value.u32;
                break;

            case 't':
                cThreads = Value.u32;
                if (!cThreads)
                    cThreads = 1;
                break;

            case 'h':
                RTPrintf("syntax: tstHandleTable [-b <base>] [-m <max>] [-t <threads>]\n");
                return 1;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return 0;

            default:
                return RTGetOptPrintError(ch, &Value);
        }

    /*
     * If any argument was specified, run the requested test setup.
     * Otherwise run a bunch of default tests.
     */
    if (cThreads || cMax || uBase)
    {
        if (!cMax)
            cMax = 65535;
        if (!cThreads)
            tstHandleTableTest1(uBase, cMax,  128, cMax / 32, false, RTHANDLETABLE_FLAGS_CONTEXT | RTHANDLETABLE_FLAGS_LOCKED);
        else
            tstHandleTableTest2(uBase, cMax,  128);
    }
    else
    {
        /*
         * Do a simple warmup / smoke test first.
         */
        tstHandleTableTest1(1,          65534,  128,           2048, false, 0);
        tstHandleTableTest1(1,          65534,  128,           2048, false, RTHANDLETABLE_FLAGS_CONTEXT);
        tstHandleTableTest1(1,          65534,   63,           2048, false, RTHANDLETABLE_FLAGS_LOCKED);
        tstHandleTableTest1(1,          65534,   63,           2048, false, RTHANDLETABLE_FLAGS_CONTEXT | RTHANDLETABLE_FLAGS_LOCKED);
        /* Test that the retain and delete functions work. */
        tstHandleTableTest1(1,           1024,  256,            256,  true, RTHANDLETABLE_FLAGS_LOCKED);
        tstHandleTableTest1(1,           1024,  256,            256,  true, RTHANDLETABLE_FLAGS_CONTEXT | RTHANDLETABLE_FLAGS_LOCKED);
        /* check that the base works. */
        tstHandleTableTest1(0x7ffff000, 65534,    4,           2048, false, RTHANDLETABLE_FLAGS_CONTEXT | RTHANDLETABLE_FLAGS_LOCKED);
        tstHandleTableTest1(0xeffff000, 65534,    4,           2048, false, RTHANDLETABLE_FLAGS_CONTEXT | RTHANDLETABLE_FLAGS_LOCKED);
        tstHandleTableTest1(0,           4097,    4,            256, false, RTHANDLETABLE_FLAGS_CONTEXT | RTHANDLETABLE_FLAGS_LOCKED);
        tstHandleTableTest1(0,           1024,    4,            128, false, RTHANDLETABLE_FLAGS_CONTEXT | RTHANDLETABLE_FLAGS_LOCKED);
        /* For testing 1st level expansion / reallocation. */
        tstHandleTableTest1(1,    1024*1024*8,    3,         150000, false, 0);
        tstHandleTableTest1(1,    1024*1024*8,    3,         150000, false, RTHANDLETABLE_FLAGS_CONTEXT);

        /*
         * Threaded tests.
         */
        tstHandleTableTest2(0x80000000,       32768, 2);
        tstHandleTableTest2(0x00010000,        2048, 4);
        tstHandleTableTest2(0x00010000,        3072, 8);
        tstHandleTableTest2(0x00000000, 1024*1024*8, 3);
    }

    /*
     * Summary.
     */
    if (!g_cErrors)
        RTPrintf("tstHandleTable: SUCCESS\n");
    else
        RTPrintf("tstHandleTable: FAILURE - %d errors\n", g_cErrors);

    return !!g_cErrors;
}
