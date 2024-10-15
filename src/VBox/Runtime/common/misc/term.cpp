/* $Id: term.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Common Termination Code.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/initterm.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a termination callback record. */
typedef struct RTTERMCALLBACKREC *PRTTERMCALLBACKREC;
/**
 * Termination callback record.
 */
typedef struct RTTERMCALLBACKREC
{
    /** Pointer to the next record. */
    PRTTERMCALLBACKREC  pNext;
    /** Pointer to the callback. */
    PFNRTTERMCALLBACK   pfnCallback;
    /** The user argument. */
    void               *pvUser;
} RTTERMCALLBACKREC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Execute once construct protecting lazy callback initialization. */
static RTONCE               g_InitTermCallbacksOnce = RTONCE_INITIALIZER;
/** Mutex protecting the callback globals. */
static RTSEMFASTMUTEX       g_hFastMutex = NIL_RTSEMFASTMUTEX;
/** Number of registered callbacks.  */
static uint32_t             g_cCallbacks = 0;
/** The callback head. */
static PRTTERMCALLBACKREC   g_pCallbackHead = NULL;



/**
 * Initializes the globals.
 *
 * @returns IPRT status code
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int32_t) rtTermInitOnce(void *pvUser)
{
    RTSEMFASTMUTEX  hFastMutex;
    int             rc;

    Assert(!g_cCallbacks);
    Assert(!g_pCallbackHead);
    Assert(g_hFastMutex == NIL_RTSEMFASTMUTEX);

    rc = RTSemFastMutexCreate(&hFastMutex);
    if (RT_SUCCESS(rc))
        g_hFastMutex = hFastMutex;

    NOREF(pvUser);

    return rc;
}



RTDECL(int) RTTermRegisterCallback(PFNRTTERMCALLBACK pfnCallback, void *pvUser)
{
    int                 rc;
    PRTTERMCALLBACKREC  pNew;

    /*
     * Validation and lazy init.
     */
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);

    rc = RTOnce(&g_InitTermCallbacksOnce, rtTermInitOnce, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Allocate and initialize a new callback record.
     */
    pNew = (PRTTERMCALLBACKREC)RTMemAlloc(sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;
    pNew->pfnCallback = pfnCallback;
    pNew->pvUser      = pvUser;

    /*
     * Insert into the list.
     */
    rc = RTSemFastMutexRequest(g_hFastMutex);
    if (RT_SUCCESS(rc))
    {
        g_cCallbacks++;
        pNew->pNext = g_pCallbackHead;
        g_pCallbackHead = pNew;

        RTSemFastMutexRelease(g_hFastMutex);
    }
    else
        RTMemFree(pNew);

    return rc;
}
RT_EXPORT_SYMBOL(RTTermRegisterCallback);


RTDECL(int) RTTermDeregisterCallback(PFNRTTERMCALLBACK pfnCallback, void *pvUser)
{
    /*
     * g_hFastMutex will be NIL if we're not initialized.
     */
    int rc;
    RTSEMFASTMUTEX hFastMutex = g_hFastMutex;
    if (hFastMutex == NIL_RTSEMFASTMUTEX)
        return VERR_NOT_FOUND;

    rc = RTSemFastMutexRequest(hFastMutex);
    if (RT_SUCCESS(rc))
    {

        /*
         * Search for the specified pfnCallback/pvUser pair.
         */
        PRTTERMCALLBACKREC pPrev = NULL;
        PRTTERMCALLBACKREC pCur  = g_pCallbackHead;
        while (pCur)
        {
            if (    pCur->pfnCallback == pfnCallback
                &&  pCur->pvUser      == pvUser)
            {
                if (pPrev)
                    pPrev->pNext = pCur->pNext;
                else
                    g_pCallbackHead = pCur->pNext;
                g_cCallbacks--;
                RTSemFastMutexRelease(hFastMutex);

                pCur->pfnCallback = NULL;
                RTMemFree(pCur);
                return VINF_SUCCESS;
            }

            /* next */
            pPrev = pCur;
            pCur  = pCur->pNext;
        }

        RTSemFastMutexRelease(hFastMutex);
        rc = VERR_NOT_FOUND;
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTTermDeregisterCallback);


RTDECL(void) RTTermRunCallbacks(RTTERMREASON enmReason, int32_t iStatus)
{
    RTSEMFASTMUTEX  hFastMutex;
    Assert(   enmReason == RTTERMREASON_EXIT
           || enmReason == RTTERMREASON_ABEND
           || enmReason == RTTERMREASON_SIGNAL
           || enmReason == RTTERMREASON_UNLOAD);

    /*
     * Run the callback list. This is a bit paranoid in order to guard against
     * recursive calls to RTTermRunCallbacks.
     */
    while (g_hFastMutex != NIL_RTSEMFASTMUTEX)
    {
        PRTTERMCALLBACKREC  pCur;
        RTTERMCALLBACKREC   CurCopy;
        int                 rc;

        /* Unlink the head of the chain. */
        rc = RTSemFastMutexRequest(g_hFastMutex);
        AssertRCReturnVoid(rc);
        pCur = g_pCallbackHead;
        if (pCur)
        {
            g_pCallbackHead = pCur->pNext;
            g_cCallbacks--;
        }
        RTSemFastMutexRelease(g_hFastMutex);
        if (!pCur)
            break;

        /* Copy and free it. */
        CurCopy = *pCur;
        RTMemFree(pCur);

        /* Make the call. */
        CurCopy.pfnCallback(enmReason, iStatus, CurCopy.pvUser);
    }

    /*
     * Free the lock.
     */
    ASMAtomicXchgHandle(&g_hFastMutex, NIL_RTSEMFASTMUTEX, &hFastMutex);
    RTSemFastMutexDestroy(hFastMutex);
    RTOnceReset(&g_InitTermCallbacksOnce); /* for the testcase */
}
RT_EXPORT_SYMBOL(RTTermRunCallbacks);

