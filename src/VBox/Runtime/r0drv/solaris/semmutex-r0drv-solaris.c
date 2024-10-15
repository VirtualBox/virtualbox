/* $Id: semmutex-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mutex Semaphores, Ring-0 Driver, Solaris.
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
#define RTSEMMUTEX_WITHOUT_REMAPPING
#include "the-solaris-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/thread.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Wrapper for the solaris semaphore structure.
 */
typedef struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t                    u32Magic;
    /** The number of recursions. */
    uint32_t                    cRecursions;
    /** The number of threads waiting for the mutex. */
    uint32_t volatile           cWaiters;
    /** The number of threads referencing us. */
    uint32_t volatile           cRefs;
    /** The owner thread, NIL_RTNATIVETHREAD if none. */
    RTNATIVETHREAD              hOwnerThread;
    /** The mutex object for synchronization. */
    kmutex_t                    Mtx;
    /** The condition variable for synchronization. */
    kcondvar_t                  Cnd;
} RTSEMMUTEXINTERNAL, *PRTSEMMUTEXINTERNAL;


RTDECL(int) RTSemMutexCreate(PRTSEMMUTEX phMtx)
{
    /*
     * Allocate.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (RT_UNLIKELY(!pThis))
        return VERR_NO_MEMORY;

    /*
     * Initialize.
     */
    pThis->u32Magic     = RTSEMMUTEX_MAGIC;
    pThis->cRecursions  = 0;
    pThis->cWaiters     = 0;
    pThis->cRefs        = 1;
    pThis->hOwnerThread = NIL_RTNATIVETHREAD;
    mutex_init(&pThis->Mtx, "IPRT Mutex", MUTEX_DRIVER, (void *)ipltospl(DISP_LEVEL));
    cv_init(&pThis->Cnd, "IPRT CVM", CV_DRIVER, NULL);
    *phMtx = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTSemMutexDestroy(RTSEMMUTEX hMtx)
{
    PRTSEMMUTEXINTERNAL     pThis = hMtx;

    /*
     * Validate.
     */
    if (pThis == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);

    mutex_enter(&pThis->Mtx);

    ASMAtomicDecU32(&pThis->cRefs);

    /*
     * Invalidate the magic to indicate the mutex is being destroyed.
     */
    ASMAtomicIncU32(&pThis->u32Magic);
    if (pThis->cWaiters > 0)
    {
        /*
         * Wake up all waiters, last waiter thread cleans up.
         */
        cv_broadcast(&pThis->Cnd);
        mutex_exit(&pThis->Mtx);
    }
    else if (pThis->cRefs == 0)
    {
        /*
         * We're the last waiter, destroy.
         */
        mutex_exit(&pThis->Mtx);
        cv_destroy(&pThis->Cnd);
        mutex_destroy(&pThis->Mtx);
        RTMemFree(pThis);
    }
    else
    {
        /*
         * We're not the last waiting thread to be woken up. Just relinquish & bail.
         */
        mutex_exit(&pThis->Mtx);
    }

    return VINF_SUCCESS;
}


/**
 * Worker for rtSemMutexSolRequest that handles the case where we go to sleep.
 *
 * @returns VINF_SUCCESS, VERR_INTERRUPTED, or VERR_SEM_DESTROYED.
 *          Returns without owning the mutex.
 * @param   pThis           The mutex instance.
 * @param   cMillies        The timeout, must be > 0 or RT_INDEFINITE_WAIT.
 * @param   fInterruptible  The wait type.
 *
 * @remarks This needs to be called with the mutex object held!
 */
static int rtSemMutexSolRequestSleep(PRTSEMMUTEXINTERNAL pThis, RTMSINTERVAL cMillies,
                                       bool fInterruptible)
{
    int rc = VERR_GENERAL_FAILURE;
    Assert(cMillies > 0);

    /*
     * Now we wait (sleep; although might spin and then sleep) & reference the mutex.
     */
    ASMAtomicIncU32(&pThis->cWaiters);
    ASMAtomicIncU32(&pThis->cRefs);

    if (cMillies != RT_INDEFINITE_WAIT)
    {
        clock_t cTicks   = drv_usectohz((clock_t)(cMillies * 1000L));
        clock_t cTimeout = ddi_get_lbolt();
        cTimeout        += cTicks;
        if (fInterruptible)
            rc = cv_timedwait_sig(&pThis->Cnd, &pThis->Mtx, cTimeout);
        else
            rc = cv_timedwait(&pThis->Cnd, &pThis->Mtx, cTimeout);
    }
    else
    {
        if (fInterruptible)
            rc = cv_wait_sig(&pThis->Cnd, &pThis->Mtx);
        else
        {
            cv_wait(&pThis->Cnd, &pThis->Mtx);
            rc = 1;
        }
    }

    ASMAtomicDecU32(&pThis->cWaiters);
    if (rc > 0)
    {
        if (pThis->u32Magic == RTSEMMUTEX_MAGIC)
        {
            if (pThis->hOwnerThread == NIL_RTNATIVETHREAD)
            {
                /*
                 * Woken up by a release from another thread.
                 */
                Assert(pThis->cRecursions == 0);
                pThis->cRecursions = 1;
                pThis->hOwnerThread = RTThreadNativeSelf();
                rc = VINF_SUCCESS;
            }
            else
            {
                /*
                 * Interrupted by some signal.
                 */
                rc = VERR_INTERRUPTED;
            }
        }
        else
        {
            /*
             * Awakened due to the destruction-in-progress broadcast.
             * We will cleanup if we're the last waiter.
             */
            rc = VERR_SEM_DESTROYED;
        }
    }
    else if (rc == -1)
    {
        /*
         * Timed out.
         */
        rc = VERR_TIMEOUT;
    }
    else
    {
        /*
         * Condition may not have been met, returned due to pending signal.
         */
        rc = VERR_INTERRUPTED;
    }

    if (!ASMAtomicDecU32(&pThis->cRefs))
    {
        Assert(RT_FAILURE_NP(rc));
        mutex_exit(&pThis->Mtx);
        cv_destroy(&pThis->Cnd);
        mutex_destroy(&pThis->Mtx);
        RTMemFree(pThis);
        return rc;
    }

    return rc;
}


/**
 * Internal worker.
 */
DECLINLINE(int) rtSemMutexSolRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, bool fInterruptible)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    int rc = VERR_GENERAL_FAILURE;

    /*
     * Validate.
     */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs >= 1);

    /*
     * Lock it and check if it's a recursion.
     */
    mutex_enter(&pThis->Mtx);
    if (pThis->hOwnerThread == RTThreadNativeSelf())
    {
        pThis->cRecursions++;
        Assert(pThis->cRecursions > 1);
        Assert(pThis->cRecursions < 256);
        rc = VINF_SUCCESS;
    }
    /*
     * Not a recursion, claim the unowned mutex if we're there are no waiters.
     */
    else if (   pThis->hOwnerThread == NIL_RTNATIVETHREAD
             && pThis->cWaiters == 0)
    {
        pThis->cRecursions  = 1;
        pThis->hOwnerThread = RTThreadNativeSelf();
        rc = VINF_SUCCESS;
    }
    /*
     * A polling call?
     */
    else if (cMillies == 0)
        rc = VERR_TIMEOUT;
    /*
     * No, we really need to get to sleep.
     */
    else
        rc = rtSemMutexSolRequestSleep(pThis, cMillies, fInterruptible);

    mutex_exit(&pThis->Mtx);
    return rc;
}


RTDECL(int) RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtSemMutexSolRequest(hMutexSem, cMillies, false /*fInterruptible*/);
}


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return RTSemMutexRequest(hMutexSem, cMillies);
}


RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtSemMutexSolRequest(hMutexSem, cMillies, true /*fInterruptible*/);
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return RTSemMutexRequestNoResume(hMutexSem, cMillies);
}


RTDECL(int) RTSemMutexRelease(RTSEMMUTEX hMtx)
{
    PRTSEMMUTEXINTERNAL pThis = hMtx;
    int                 rc;

    /*
     * Validate.
     */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);

    /*
     * Take the lock and release one recursion.
     */
    mutex_enter(&pThis->Mtx);
    if (pThis->hOwnerThread == RTThreadNativeSelf())
    {
        Assert(pThis->cRecursions > 0);
        if (--pThis->cRecursions == 0)
        {
            pThis->hOwnerThread = NIL_RTNATIVETHREAD;

            /*
             * If there are any waiters, signal one of them.
             */
            if (pThis->cWaiters > 0)
                cv_signal(&pThis->Cnd);
        }
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NOT_OWNER;

    mutex_exit(&pThis->Mtx);
    return rc;
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    bool fOwned = false;

    /*
     * Validate.
     */
    AssertPtrReturn(pThis, false);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), false);

    /*
     * Check if this is the owner.
     */
    mutex_enter(&pThis->Mtx);
    fOwned = pThis->hOwnerThread != NIL_RTNATIVETHREAD;
    mutex_exit(&pThis->Mtx);

    return fOwned;
}

