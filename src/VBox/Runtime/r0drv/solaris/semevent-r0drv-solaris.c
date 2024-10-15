/* $Id: semevent-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Single Release Event Semaphores, Ring-0 Driver, Solaris.
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
#define RTSEMEVENT_WITHOUT_REMAPPING
#include "the-solaris-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/thread.h>
#include "internal/magics.h"
#include "semeventwait-r0drv-solaris.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Waiter entry.  Lives on the stack.
 *
 * @remarks Unfortunately, we cannot easily use cv_signal because we cannot
 *          distinguish between it and the spurious wakeups we get after fork.
 *          So, we keep an unprioritized FIFO with the sleeping threads.
 */
typedef struct RTSEMEVENTSOLENTRY
{
    /** The list node. */
    RTLISTNODE          Node;
    /** The thread. */
    kthread_t          *pThread;
    /** Set to @c true when waking up the thread by signal or destroy. */
    uint32_t volatile  fWokenUp;
} RTSEMEVENTSOLENTRY;
/** Pointer to waiter entry. */
typedef RTSEMEVENTSOLENTRY *PRTSEMEVENTSOLENTRY;


/**
 * Solaris event semaphore.
 */
typedef struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The number of threads referencing this object. */
    uint32_t volatile   cRefs;
    /** Set if the object is signalled when there are no waiters. */
    bool                fSignaled;
    /** List of waiting and woken up threads. */
    RTLISTANCHOR        WaitList;
    /** The Solaris mutex protecting this structure and pairing up the with the cv. */
    kmutex_t            Mtx;
    /** The Solaris condition variable. */
    kcondvar_t          Cnd;
} RTSEMEVENTINTERNAL, *PRTSEMEVENTINTERNAL;



RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertCompile(sizeof(RTSEMEVENTINTERNAL) > sizeof(void *));
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));
    AssertPtrReturn(phEventSem, VERR_INVALID_POINTER);
    RT_ASSERT_PREEMPTIBLE();

    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic       = RTSEMEVENT_MAGIC;
    pThis->cRefs          = 1;
    pThis->fSignaled      = false;
    RTListInit(&pThis->WaitList);
    mutex_init(&pThis->Mtx, "IPRT Event Semaphore", MUTEX_DRIVER, (void *)ipltospl(DISP_LEVEL));
    cv_init(&pThis->Cnd, "IPRT CV", CV_DRIVER, NULL);

    *phEventSem = pThis;
    return VINF_SUCCESS;
}


/**
 * Retain a reference to the semaphore.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventSolRetain(PRTSEMEVENTINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000);
    NOREF(cRefs);
}


/**
 * The destruct.
 *
 * @param   pThis       The semaphore.
 */
static void rtR0SemEventSolDtor(PRTSEMEVENTINTERNAL pThis)
{
    Assert(pThis->u32Magic != RTSEMEVENT_MAGIC);
    cv_destroy(&pThis->Cnd);
    mutex_destroy(&pThis->Mtx);
    RTMemFree(pThis);
}


/**
 * Release a reference, destroy the thing if necessary.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventSolRelease(PRTSEMEVENTINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
        rtR0SemEventSolDtor(pThis);
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs > 0);
    RT_ASSERT_INTS_ON();

    mutex_enter(&pThis->Mtx);

    /*
     * Invalidate the semaphore.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENT_MAGIC);
    ASMAtomicWriteBool(&pThis->fSignaled, false);

    /*
     * Abort and wake up all threads.
     */
    PRTSEMEVENTSOLENTRY pWaiter;
    RTListForEach(&pThis->WaitList, pWaiter, RTSEMEVENTSOLENTRY, Node)
    {
        pWaiter->fWokenUp = true;
    }
    cv_broadcast(&pThis->Cnd);

    /*
     * Release the reference from RTSemEventCreateEx.
     */
    mutex_exit(&pThis->Mtx);
    rtR0SemEventSolRelease(pThis);

    return VINF_SUCCESS;
}


RTDECL(int) RTSemEventSignal(RTSEMEVENT hEventSem)
{
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;
    RT_ASSERT_PREEMPT_CPUID_VAR();
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    RT_ASSERT_INTS_ON();

    rtR0SemEventSolRetain(pThis);
    rtR0SemSolWaitEnterMutexWithUnpinningHack(&pThis->Mtx);

    /*
     * Wake up one thread.
     */
    ASMAtomicWriteBool(&pThis->fSignaled, true);

    PRTSEMEVENTSOLENTRY pWaiter;
    RTListForEach(&pThis->WaitList, pWaiter, RTSEMEVENTSOLENTRY, Node)
    {
        if (!pWaiter->fWokenUp)
        {
            pWaiter->fWokenUp = true;
            setrun(pWaiter->pThread);
            ASMAtomicWriteBool(&pThis->fSignaled, false);
            break;
        }
    }

    mutex_exit(&pThis->Mtx);
    rtR0SemEventSolRelease(pThis);

#ifdef DEBUG_ramshankar
    /** See @bugref{6318} comment#11 */
    return VINF_SUCCESS;
#endif
    RT_ASSERT_PREEMPT_CPUID();
    return VINF_SUCCESS;
}


/**
 * Worker for RTSemEventWaitEx and RTSemEventWaitExDebug.
 *
 * @returns VBox status code.
 * @param   pThis           The event semaphore.
 * @param   fFlags          See RTSemEventWaitEx.
 * @param   uTimeout        See RTSemEventWaitEx.
 * @param   pSrcPos         The source code position of the wait.
 */
static int rtR0SemEventSolWait(PRTSEMEVENTINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                               PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

    rtR0SemEventSolRetain(pThis);
    mutex_enter(&pThis->Mtx);

    /*
     * In the signaled state?
     */
    int rc;
    if (ASMAtomicCmpXchgBool(&pThis->fSignaled, false, true))
        rc = VINF_SUCCESS;
    else
    {
        /*
         * We have to wait.
         */
        RTR0SEMSOLWAIT Wait;
        rc = rtR0SemSolWaitInit(&Wait, fFlags, uTimeout);
        if (RT_SUCCESS(rc))
        {
            RTSEMEVENTSOLENTRY Waiter;  /* ASSUMES we won't get swapped out while waiting (TS_DONT_SWAP). */
            Waiter.pThread  = curthread;
            Waiter.fWokenUp = false;
            RTListAppend(&pThis->WaitList, &Waiter.Node);

            for (;;)
            {
                /* The destruction test. */
                if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENT_MAGIC))
                    rc = VERR_SEM_DESTROYED;
                else
                {
                    /* Check the exit conditions. */
                    if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENT_MAGIC))
                        rc = VERR_SEM_DESTROYED;
                    else if (Waiter.fWokenUp)
                        rc = VINF_SUCCESS;
                    else if (rtR0SemSolWaitHasTimedOut(&Wait))
                        rc = VERR_TIMEOUT;
                    else if (rtR0SemSolWaitWasInterrupted(&Wait))
                        rc = VERR_INTERRUPTED;
                    else
                    {
                        /* Do the wait and then recheck the conditions. */
                        rtR0SemSolWaitDoIt(&Wait, &pThis->Cnd, &pThis->Mtx, &Waiter.fWokenUp, false);
                        continue;
                    }
                }
                break;
            }

            rtR0SemSolWaitDelete(&Wait);
            RTListNodeRemove(&Waiter.Node);
        }
    }

    mutex_exit(&pThis->Mtx);
    rtR0SemEventSolRelease(pThis);
    return rc;
}


RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventSolWait(hEventSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventSolWait(hEventSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventSolWait(hEventSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    return rtR0SemSolWaitGetResolution();
}


RTR0DECL(bool) RTSemEventIsSignalSafe(void)
{
    /* I don't trust Solaris not to preempt us. */
    return false;
}

