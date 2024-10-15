/* $Id: nocrt-per-thread-2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-Crt - Per Thread Data, Managment code
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#include "internal/nocrt.h"
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init/term once state. */
static RTONCE               g_NoCrtPerThreadOnce = RTONCE_INITIALIZER;

/** List of heap allocations (PRTNOCRTTHREADDATA).   */
static RTLISTANCHOR         g_NoCrtPerThreadHeapList;
/** Critical section protecting g_NoCrtPerThreadHeapList. */
static RTCRITSECT           g_NoCrtPerThreadCritSect;

/** Allocation bitmap for g_aNoCrtPerThreadStatic.
 *
 * In debug builds we only have one slot here, so we have a better chance of
 * testing the heap code path. */
#ifdef DEBUG
static uint32_t volatile    g_fNoCrtPerThreadStaticAlloc = UINT32_C(0xffffefff);
#else
static uint32_t volatile    g_fNoCrtPerThreadStaticAlloc = 0;
#endif
/* Static allocations to avoid the heap and associate slowness. */
static RTNOCRTTHREADDATA    g_aNoCrtPerThreadStatic[32];


/**
 * @callback_method_impl{FNRTTLSDTOR}
 */
static DECLCALLBACK(void) rtNoCrtPerThreadDtor(void *pvValue)
{
    PRTNOCRTTHREADDATA pNoCrtData = (PRTNOCRTTHREADDATA)pvValue;
    if (pNoCrtData->enmAllocType == RTNOCRTTHREADDATA::kAllocType_Heap)
    {
        AssertReturnVoid(RTOnceWasInitialized(&g_NoCrtPerThreadOnce));

        RTCritSectEnter(&g_NoCrtPerThreadCritSect); /* timeout? */

        RTListNodeRemove(&pNoCrtData->ListEntry);
        pNoCrtData->enmAllocType = RTNOCRTTHREADDATA::kAllocType_End;

        RTCritSectLeave(&g_NoCrtPerThreadCritSect);

        RTMemFree(pNoCrtData);
    }
    else if (pNoCrtData->enmAllocType == RTNOCRTTHREADDATA::kAllocType_Static)
    {
        size_t iSlot = (size_t)(pNoCrtData - &g_aNoCrtPerThreadStatic[0]);
        AssertReturnVoid(iSlot < RT_ELEMENTS(g_aNoCrtPerThreadStatic));

        pNoCrtData->enmAllocType = RTNOCRTTHREADDATA::kAllocType_Invalid;
        ASMAtomicAndU32(&g_fNoCrtPerThreadStaticAlloc, ~(uint32_t)iSlot);
    }
}


/**
 * @callback_method_impl{FNRTONCE}
 */
static DECLCALLBACK(int32_t) rtNoCrtPerThreadInit(void *pvUser)
{
    RTListInit(&g_NoCrtPerThreadHeapList);

    RTTLS iTls = NIL_RTTLS;
    int rc = RTTlsAllocEx(&iTls, rtNoCrtPerThreadDtor);
    if (iTls != NIL_RTTLS)
    {
        rc = RTCritSectInit(&g_NoCrtPerThreadCritSect);
        if (RT_SUCCESS(rc))
        {
            g_iTlsRtNoCrtPerThread = iTls;
            return VINF_SUCCESS;
        }
        RTTlsFree(iTls);
    }
    RT_NOREF(pvUser);
    return rc;
}


/**
 * @callback_method_impl{FNRTONCECLEANUP}
 */
static DECLCALLBACK(void) rtNoCrtPerThreadCleanup(void *pvUser, bool fLazyCleanUpOk)
{
    RT_NOREF(pvUser);
    if (fLazyCleanUpOk)
        return;

    /*
     * First destroy the TLS entry.
     */
    RTTLS iTls = g_iTlsRtNoCrtPerThread;
    g_iTlsRtNoCrtPerThread = NIL_RTTLS;
    int rc = RTTlsFree(iTls);
    AssertRC(rc);

    /*
     * Then destroy the critical section and free all entries in the list.
     */
    RTCritSectDelete(&g_NoCrtPerThreadCritSect);

    PRTNOCRTTHREADDATA pNoCrtData;
    while ((pNoCrtData = RTListRemoveFirst(&g_NoCrtPerThreadHeapList, RTNOCRTTHREADDATA, ListEntry)) != NULL)
    {
        AssertContinue(pNoCrtData->enmAllocType == RTNOCRTTHREADDATA::kAllocType_Heap);
        pNoCrtData->enmAllocType = RTNOCRTTHREADDATA::kAllocType_End;
        RTMemFree(pNoCrtData);
    }
}

PRTNOCRTTHREADDATA rtNoCrtThreadDataGet(void)
{
    int rc = RTOnceEx(&g_NoCrtPerThreadOnce, rtNoCrtPerThreadInit, rtNoCrtPerThreadCleanup, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * We typically have an entry already.
         */
        PRTNOCRTTHREADDATA pNoCrtData = (PRTNOCRTTHREADDATA)RTTlsGet(g_iTlsRtNoCrtPerThread);
        if (pNoCrtData)
        {
            AssertReturn(   pNoCrtData->enmAllocType > RTNOCRTTHREADDATA::kAllocType_Invalid
                         && pNoCrtData->enmAllocType < RTNOCRTTHREADDATA::kAllocType_End,
                         NULL);
            return pNoCrtData;
        }

        /*
         * Okay, allocate a new entry first using some of the statically allocated
         * ones then falling back on heap allocations.
         */
        for (;;)
        {
            uint32_t const  fAlloc = ASMAtomicUoReadU32(&g_fNoCrtPerThreadStaticAlloc);
            uint32_t        iSlot  = ASMBitFirstSetU32(~fAlloc);
            if (iSlot != 0)
                iSlot--;
            else
                break;
            if (ASMAtomicCmpXchgU32(&g_fNoCrtPerThreadStaticAlloc, fAlloc | RT_BIT_32(iSlot), fAlloc))
            {
                pNoCrtData = &g_aNoCrtPerThreadStatic[iSlot];

                /* Init the entry in case it's being re-used: */
                Assert(pNoCrtData->enmAllocType == RTNOCRTTHREADDATA::kAllocType_Invalid);
                rc = RTTlsSet(g_iTlsRtNoCrtPerThread, pNoCrtData);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    pNoCrtData->enmAllocType = RTNOCRTTHREADDATA::kAllocType_Static;
                    RTListInit(&pNoCrtData->ListEntry);
                    pNoCrtData->iErrno      = 0;
                    pNoCrtData->pszStrToken = NULL;
                    return pNoCrtData;
                }

                ASMAtomicOrU32(&g_fNoCrtPerThreadStaticAlloc, RT_BIT_32(iSlot));
                return NULL;
            }
            ASMNopPause();
        }

        /*
         * Heap.
         */
        pNoCrtData = (PRTNOCRTTHREADDATA)RTMemAllocZ(sizeof(*pNoCrtData));
        if (pNoCrtData)
        {
            pNoCrtData->enmAllocType = RTNOCRTTHREADDATA::kAllocType_Heap;

            RTCritSectEnter(&g_NoCrtPerThreadCritSect);
            RTListAppend(&g_NoCrtPerThreadHeapList, &pNoCrtData->ListEntry);
            RTCritSectLeave(&g_NoCrtPerThreadCritSect);

            rc = RTTlsSet(g_iTlsRtNoCrtPerThread, pNoCrtData);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
                return pNoCrtData;

            RTCritSectEnter(&g_NoCrtPerThreadCritSect);
            RTListNodeRemove(&pNoCrtData->ListEntry);
            RTCritSectLeave(&g_NoCrtPerThreadCritSect);

            pNoCrtData->enmAllocType = RTNOCRTTHREADDATA::kAllocType_End;
            RTMemFree(pNoCrtData);
        }
    }
    return NULL;
}

