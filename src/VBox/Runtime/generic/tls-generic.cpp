/* $Id: tls-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Thread Local Storage (TSL), Generic Implementation.
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
#define LOG_GROUP RTLOGGROUP_THREAD
#include <iprt/thread.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Allocation bitmap. Set bits indicates allocated entries. */
static uint32_t volatile    g_au32AllocatedBitmap[(RTTHREAD_TLS_ENTRIES + 31) / 32];
/** Destructors for each of the TLS entries. */
static PFNRTTLSDTOR         g_apfnDestructors[RTTHREAD_TLS_ENTRIES];


RTR3DECL(RTTLS) RTTlsAlloc(void)
{
    RTTLS iTls;
    int rc = RTTlsAllocEx(&iTls, NULL);
    return RT_SUCCESS(rc) ? iTls : NIL_RTTLS;
}


RTR3DECL(int) RTTlsAllocEx(PRTTLS piTls, PFNRTTLSDTOR pfnDestructor)
{
    for (unsigned i = 0; i < 128; i++)
    {
        int iTls = ASMBitFirstClear(&g_au32AllocatedBitmap[0], RTTHREAD_TLS_ENTRIES);
        if (iTls < 0)
        {
            *piTls = NIL_RTTLS;
            return VERR_NO_MEMORY;
        }
        if (!ASMAtomicBitTestAndSet(&g_au32AllocatedBitmap[0], iTls))
        {
            g_apfnDestructors[iTls] = pfnDestructor;
            *piTls = iTls;
            return VINF_SUCCESS;
        }
    }

    AssertFailed();
    return VERR_NO_MEMORY;
}


RTR3DECL(int) RTTlsFree(RTTLS iTls)
{
    if (iTls == NIL_RTTLS)
        return VINF_SUCCESS;
    if (    iTls < 0
        ||  iTls >= RTTHREAD_TLS_ENTRIES
        ||  !ASMBitTest(&g_au32AllocatedBitmap[0], iTls))
        return VERR_INVALID_PARAMETER;

    ASMAtomicWriteNullPtr(&g_apfnDestructors[iTls]);
    rtThreadClearTlsEntry(iTls);
    ASMAtomicBitClear(&g_au32AllocatedBitmap[0], iTls);
    return VINF_SUCCESS;
}


RTR3DECL(void *) RTTlsGet(RTTLS iTls)
{
    void *pv;
    int rc = RTTlsGetEx(iTls, &pv);
    return RT_SUCCESS(rc) ? pv : NULL;
}


RTR3DECL(int) RTTlsGetEx(RTTLS iTls, void **ppvValue)
{
    if (RT_UNLIKELY(    iTls < 0
                    ||  iTls >= RTTHREAD_TLS_ENTRIES
                    ||  !ASMBitTest(&g_au32AllocatedBitmap[0], iTls)))
        return VERR_INVALID_PARAMETER;

    PRTTHREADINT pThread = rtThreadGet(RTThreadSelf());
    AssertReturn(pThread, VERR_NOT_SUPPORTED);
    void *pv = pThread->apvTlsEntries[iTls];
    rtThreadRelease(pThread);
    *ppvValue = pv;
    return VINF_SUCCESS;
}


RTR3DECL(int) RTTlsSet(RTTLS iTls, void *pvValue)
{
    if (RT_UNLIKELY(    iTls < 0
                    ||  iTls >= RTTHREAD_TLS_ENTRIES
                    ||  !ASMBitTest(&g_au32AllocatedBitmap[0], iTls)))
        return VERR_INVALID_PARAMETER;

    PRTTHREADINT pThread = rtThreadGet(RTThreadSelf());
    AssertReturn(pThread, VERR_NOT_SUPPORTED);
    pThread->apvTlsEntries[iTls] = pvValue;
    rtThreadRelease(pThread);
    return VINF_SUCCESS;
}



/**
 * Called at thread termination to invoke TLS destructors.
 *
 * @param   pThread     The current thread.
 */
DECLHIDDEN(void) rtThreadTlsDestruction(PRTTHREADINT pThread)
{
    for (RTTLS iTls = 0; iTls < RTTHREAD_TLS_ENTRIES; iTls++)
    {
        void *pv = pThread->apvTlsEntries[iTls];
        if (pv)
        {
            PFNRTTLSDTOR pfnDestructor = (PFNRTTLSDTOR)(uintptr_t)ASMAtomicUoReadPtr((void * volatile *)(uintptr_t)&g_apfnDestructors[iTls]);
            if (pfnDestructor)
            {
                pThread->apvTlsEntries[iTls] = NULL;
                pfnDestructor(pv);
            }
        }
    }
}

