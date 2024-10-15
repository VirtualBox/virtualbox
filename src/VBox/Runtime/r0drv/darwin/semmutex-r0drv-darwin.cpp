/* $Id: semmutex-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mutex Semaphores, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/thread.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Darwin mutex semaphore.
 */
typedef struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The number of waiting threads. */
    uint32_t            cWaiters;
    /** The number of references. */
    uint32_t volatile   cRefs;
    /** The number of recursions. */
    uint32_t            cRecursions;
    /** The handle of the owner thread. */
    RTNATIVETHREAD      hNativeOwner;
    /** The spinlock protecting us. */
    lck_spin_t         *pSpinlock;
} RTSEMMUTEXINTERNAL, *PRTSEMMUTEXINTERNAL;



RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    return RTSemMutexCreateEx(phMutexSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
}


RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    RT_NOREF(hClass, uSubClass, pszNameFmt);
    AssertReturn(!(fFlags & ~RTSEMMUTEX_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    AssertCompile(sizeof(RTSEMMUTEXINTERNAL) > sizeof(void *));
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic     = RTSEMMUTEX_MAGIC;
        pThis->cWaiters     = 0;
        pThis->cRefs        = 1;
        pThis->cRecursions  = 0;
        pThis->hNativeOwner = NIL_RTNATIVETHREAD;
        Assert(g_pDarwinLockGroup);
        pThis->pSpinlock = lck_spin_alloc_init(g_pDarwinLockGroup, LCK_ATTR_NULL);
        if (pThis->pSpinlock)
        {
            *phMutexSem = pThis;
            IPRT_DARWIN_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }

        RTMemFree(pThis);
    }
    IPRT_DARWIN_RESTORE_EFL_AC();
    return VERR_NO_MEMORY;
}


/**
 * Called when the refcount reaches zero.
 */
static void rtSemMutexDarwinFree(PRTSEMMUTEXINTERNAL pThis)
{
    IPRT_DARWIN_SAVE_EFL_AC();

    lck_spin_unlock(pThis->pSpinlock);
    lck_spin_destroy(pThis->pSpinlock, g_pDarwinLockGroup);
    RTMemFree(pThis);

    IPRT_DARWIN_RESTORE_EFL_AC();
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;
    if (pThis == NIL_RTSEMMUTEX)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Kill it, wake up all waiting threads and release the reference.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTSEMMUTEX_MAGIC, RTSEMMUTEX_MAGIC), VERR_INVALID_HANDLE);
    lck_spin_lock(pThis->pSpinlock);

    if (pThis->cWaiters > 0)
        thread_wakeup_prim((event_t)pThis, FALSE /* one_thread */, THREAD_RESTART);

    if (ASMAtomicDecU32(&pThis->cRefs) == 0)
        rtSemMutexDarwinFree(pThis);
    else
        lck_spin_unlock(pThis->pSpinlock);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


/**
 * Internal worker for the sleep scenario.
 *
 * Called owning the spinlock, returns without it.
 *
 * @returns IPRT status code.
 * @param   pThis               The mutex instance.
 * @param   cMillies            The timeout.
 * @param   fInterruptible      Whether it's interruptible
 *                              (RTSemMutexRequestNoResume) or not
 *                              (RTSemMutexRequest).
 * @param   hNativeSelf         The thread handle of the caller.
 */
static int rtR0SemMutexDarwinRequestSleep(PRTSEMMUTEXINTERNAL pThis, RTMSINTERVAL cMillies,
                                          wait_interrupt_t fInterruptible, RTNATIVETHREAD hNativeSelf)
{
    /*
     * Grab a reference and indicate that we're waiting.
     */
    pThis->cWaiters++;
    ASMAtomicIncU32(&pThis->cRefs);

    /*
     * Go to sleep, use the address of the mutex instance as sleep/blocking/event id.
     */
    wait_result_t rcWait;
    if (cMillies == RT_INDEFINITE_WAIT)
        rcWait = lck_spin_sleep(pThis->pSpinlock, LCK_SLEEP_DEFAULT, (event_t)pThis, fInterruptible);
    else
    {
        uint64_t u64AbsTime;
        nanoseconds_to_absolutetime(cMillies * UINT64_C(1000000), &u64AbsTime);
        u64AbsTime += mach_absolute_time();

        rcWait = lck_spin_sleep_deadline(pThis->pSpinlock, LCK_SLEEP_DEFAULT,
                                         (event_t)pThis, fInterruptible, u64AbsTime);
    }

    /*
     * Translate the rc.
     */
    int rc;
    switch (rcWait)
    {
        case THREAD_AWAKENED:
            if (RT_LIKELY(pThis->u32Magic == RTSEMMUTEX_MAGIC))
            {
                if (RT_LIKELY(   pThis->cRecursions  == 0
                              && pThis->hNativeOwner == NIL_RTNATIVETHREAD))
                {
                    pThis->cRecursions  = 1;
                    pThis->hNativeOwner = hNativeSelf;
                    rc = VINF_SUCCESS;
                }
                else
                {
                    Assert(pThis->cRecursions  == 0);
                    Assert(pThis->hNativeOwner == NIL_RTNATIVETHREAD);
                    rc = VERR_INTERNAL_ERROR_3;
                }
            }
            else
                rc = VERR_SEM_DESTROYED;
            break;

        case THREAD_TIMED_OUT:
            Assert(cMillies != RT_INDEFINITE_WAIT);
            rc = VERR_TIMEOUT;
            break;

        case THREAD_INTERRUPTED:
            Assert(fInterruptible);
            rc = VERR_INTERRUPTED;
            break;

        case THREAD_RESTART:
            Assert(pThis->u32Magic == ~RTSEMMUTEX_MAGIC);
            rc = VERR_SEM_DESTROYED;
            break;

        default:
            AssertMsgFailed(("rcWait=%d\n", rcWait));
            rc = VERR_GENERAL_FAILURE;
            break;
    }

    /*
     * Dereference it and quit the lock.
     */
    Assert(pThis->cWaiters > 0);
    pThis->cWaiters--;

    Assert(pThis->cRefs > 0);
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
        rtSemMutexDarwinFree(pThis);
    else
        lck_spin_unlock(pThis->pSpinlock);
    return rc;
}


/**
 * Internal worker for RTSemMutexRequest and RTSemMutexRequestNoResume
 *
 * @returns IPRT status code.
 * @param   hMutexSem           The mutex handle.
 * @param   cMillies            The timeout.
 * @param   fInterruptible      Whether it's interruptible
 *                              (RTSemMutexRequestNoResume) or not
 *                              (RTSemMutexRequest).
 */
DECLINLINE(int) rtR0SemMutexDarwinRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, wait_interrupt_t fInterruptible)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Grab the lock and check out the state.
     */
    RTNATIVETHREAD  hNativeSelf = RTThreadNativeSelf();
    int             rc          = VINF_SUCCESS;
    lck_spin_lock(pThis->pSpinlock);

    /* Recursive call? */
    if (pThis->hNativeOwner == hNativeSelf)
    {
        Assert(pThis->cRecursions > 0);
        Assert(pThis->cRecursions < 256);
        pThis->cRecursions++;
    }

    /* Is it free and nobody ahead of us in the queue? */
    else if (   pThis->hNativeOwner == NIL_RTNATIVETHREAD
             && pThis->cWaiters     == 0)
    {
        pThis->hNativeOwner = hNativeSelf;
        pThis->cRecursions  = 1;
    }

    /* Polling call? */
    else if (cMillies == 0)
        rc = VERR_TIMEOUT;

    /* Yawn, time for a nap... */
    else
    {
        rc = rtR0SemMutexDarwinRequestSleep(pThis, cMillies, fInterruptible, hNativeSelf);
        IPRT_DARWIN_RESTORE_EFL_ONLY_AC();
        return rc;
    }

    lck_spin_unlock(pThis->pSpinlock);
    IPRT_DARWIN_RESTORE_EFL_ONLY_AC();
    return rc;
}


RTDECL(int) RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtR0SemMutexDarwinRequest(hMutexSem, cMillies, THREAD_UNINT);
}


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RT_SRC_POS_NOREF(); RT_NOREF(uId);
    return RTSemMutexRequest(hMutexSem, cMillies);
}


RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtR0SemMutexDarwinRequest(hMutexSem, cMillies, THREAD_ABORTSAFE);
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RT_SRC_POS_NOREF(); RT_NOREF(uId);
    return RTSemMutexRequestNoResume(hMutexSem, cMillies);
}


RTDECL(int)  RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Take the lock and do the job.
     */
    RTNATIVETHREAD  hNativeSelf = RTThreadNativeSelf();
    int             rc          = VINF_SUCCESS;
    lck_spin_lock(pThis->pSpinlock);

    if (pThis->hNativeOwner == hNativeSelf)
    {
        Assert(pThis->cRecursions > 0);
        if (--pThis->cRecursions == 0)
        {
            pThis->hNativeOwner = NIL_RTNATIVETHREAD;
            if (pThis->cWaiters > 0)
                thread_wakeup_prim((event_t)pThis, TRUE /* one_thread */, THREAD_AWAKENED);

        }
    }
    else
        rc = VERR_NOT_OWNER;

    lck_spin_unlock(pThis->pSpinlock);

    AssertRC(rc);
    IPRT_DARWIN_RESTORE_EFL_ONLY_AC();
    return VINF_SUCCESS;
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, false);
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Take the lock and do the check.
     */
    lck_spin_lock(pThis->pSpinlock);
    bool fRc = pThis->hNativeOwner != NIL_RTNATIVETHREAD;
    lck_spin_unlock(pThis->pSpinlock);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return fRc;
}

