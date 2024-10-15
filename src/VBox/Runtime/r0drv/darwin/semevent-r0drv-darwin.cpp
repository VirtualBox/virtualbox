/* $Id: semevent-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Single Release Event Semaphores, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
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
#include <iprt/time.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Waiter entry.  Lives on the stack.
 */
typedef struct RTSEMEVENTDARWINENTRY
{
    /** The list node. */
    RTLISTNODE  Node;
    /** Flag set when waking up the thread by signal or destroy. */
    bool volatile fWokenUp;
} RTSEMEVENTDARWINENTRY;
/** Pointer to waiter entry. */
typedef RTSEMEVENTDARWINENTRY *PRTSEMEVENTDARWINENTRY;


/**
 * Darwin event semaphore.
 */
typedef struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t volatile   u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** Set if there are blocked threads. */
    bool volatile       fHaveBlockedThreads;
    /** Set if the event object is signaled. */
    bool volatile       fSignaled;
    /** List of waiting and woken up threads. */
    RTLISTANCHOR        WaitList;
    /** The spinlock protecting us. */
    lck_spin_t         *pSpinlock;
} RTSEMEVENTINTERNAL, *PRTSEMEVENTINTERNAL;



RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    RT_NOREF(hClass, pszNameFmt);
    AssertCompile(sizeof(RTSEMEVENTINTERNAL) > sizeof(void *));
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));
    AssertPtrReturn(phEventSem, VERR_INVALID_POINTER);
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic = RTSEMEVENT_MAGIC;
        pThis->cRefs                = 1;
        pThis->fHaveBlockedThreads  = false;
        pThis->fSignaled            = false;
        RTListInit(&pThis->WaitList);
        Assert(g_pDarwinLockGroup);
        pThis->pSpinlock = lck_spin_alloc_init(g_pDarwinLockGroup, LCK_ATTR_NULL);
        if (pThis->pSpinlock)
        {
            *phEventSem = pThis;
            IPRT_DARWIN_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }

        pThis->u32Magic = 0;
        RTMemFree(pThis);
    }
    IPRT_DARWIN_RESTORE_EFL_AC();
    return VERR_NO_MEMORY;
}


/**
 * Retain a reference to the semaphore.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventDarwinRetain(PRTSEMEVENTINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000); RT_NOREF_PV(cRefs);
}


/**
 * Release a reference, destroy the thing if necessary.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventDarwinRelease(PRTSEMEVENTINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
    {
        Assert(pThis->u32Magic != RTSEMEVENT_MAGIC);
        IPRT_DARWIN_SAVE_EFL_AC();

        lck_spin_destroy(pThis->pSpinlock, g_pDarwinLockGroup);
        RTMemFree(pThis);

        IPRT_DARWIN_RESTORE_EFL_AC();
    }
}

RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    PRTSEMEVENTINTERNAL pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();

    RTCCUINTREG const fIntSaved = ASMIntDisableFlags();
    lck_spin_lock(pThis->pSpinlock);

    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENT_MAGIC); /* make the handle invalid */
    ASMAtomicWriteBool(&pThis->fSignaled, false);

    /* abort waiting threads. */
    PRTSEMEVENTDARWINENTRY pWaiter;
    RTListForEach(&pThis->WaitList, pWaiter, RTSEMEVENTDARWINENTRY, Node)
    {
        pWaiter->fWokenUp = true;
        thread_wakeup_prim((event_t)pWaiter, FALSE /* all threads */, THREAD_RESTART);
    }

    lck_spin_unlock(pThis->pSpinlock);
    ASMSetFlags(fIntSaved);
    rtR0SemEventDarwinRelease(pThis);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);
    RT_ASSERT_PREEMPT_CPUID_VAR();

    /*
     * Coming here with interrupts disabled should be okay.  The thread_wakeup_prim KPI is used
     * by the interrupt handler IOFilterInterruptEventSource::disableInterruptOccurred() via
     * signalWorkAvailable().  The only problem is if we have to destroy the event structure,
     * as RTMemFree does not work with interrupts disabled (IOFree/kfree takes zone mutex).
     */
    //RT_ASSERT_INTS_ON(); - we may be called from interrupt context, which seems to be perfectly fine.
    IPRT_DARWIN_SAVE_EFL_AC();

    RTCCUINTREG const fIntSaved = ASMIntDisableFlags();
    rtR0SemEventDarwinRetain(pThis);
    lck_spin_lock(pThis->pSpinlock);

    /*
     * Wake up one thread.
     */
    ASMAtomicWriteBool(&pThis->fSignaled, true);

    PRTSEMEVENTDARWINENTRY pWaiter;
    RTListForEach(&pThis->WaitList, pWaiter, RTSEMEVENTDARWINENTRY, Node)
    {
        if (!pWaiter->fWokenUp)
        {
            pWaiter->fWokenUp = true;
            thread_wakeup_prim((event_t)pWaiter, FALSE /* all threads */, THREAD_AWAKENED);
            ASMAtomicWriteBool(&pThis->fSignaled, false);
            break;
        }
    }

    lck_spin_unlock(pThis->pSpinlock);
    ASMSetFlags(fIntSaved);
    rtR0SemEventDarwinRelease(pThis);

    RT_ASSERT_PREEMPT_CPUID();
    AssertMsg((fSavedEfl & X86_EFL_IF) == (ASMGetFlags() & X86_EFL_IF), ("fSavedEfl=%#x cur=%#x\n",(uint32_t)fSavedEfl, ASMGetFlags()));
    IPRT_DARWIN_RESTORE_EFL_AC();
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
static int rtR0SemEventDarwinWait(PRTSEMEVENTINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                                  PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF(pSrcPos);

    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);
    IPRT_DARWIN_SAVE_EFL_AC();

    RTCCUINTREG const fIntSaved = ASMIntDisableFlags();
    rtR0SemEventDarwinRetain(pThis);
    lck_spin_lock(pThis->pSpinlock);

    /*
     * In the signaled state?
     */
    int rc;
    if (ASMAtomicCmpXchgBool(&pThis->fSignaled, false, true))
        rc = VINF_SUCCESS;
    else
    {
        /*
         * We have to wait. So, we'll need to convert the timeout and figure
         * out if it's indefinite or not.
         */
        uint64_t uNsAbsTimeout = 1;
        if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
        {
            if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
                uTimeout = uTimeout < UINT64_MAX / UINT32_C(1000000) * UINT32_C(1000000)
                         ? uTimeout * UINT32_C(1000000)
                         : UINT64_MAX;
            if (uTimeout == UINT64_MAX)
                fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            else
            {
                uint64_t u64Now;
                if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
                {
                    if (uTimeout != 0)
                    {
                        u64Now = RTTimeSystemNanoTS();
                        uNsAbsTimeout = u64Now + uTimeout;
                        if (uNsAbsTimeout < u64Now) /* overflow */
                            fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
                    }
                }
                else
                {
                    uNsAbsTimeout = uTimeout;
                    u64Now        = RTTimeSystemNanoTS();
                    uTimeout      = u64Now < uTimeout ? uTimeout - u64Now : 0;
                }
            }
        }

        if (   !(fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
            && uTimeout == 0)
        {
            /*
             * Poll call, we already checked the condition above so no need to
             * wait for anything.
             */
            rc = VERR_TIMEOUT;
        }
        else
        {
            RTSEMEVENTDARWINENTRY Waiter;
            Waiter.fWokenUp = false;
            RTListAppend(&pThis->WaitList, &Waiter.Node);

            for (;;)
            {
                /*
                 * Do the actual waiting.
                 */
                ASMAtomicWriteBool(&pThis->fHaveBlockedThreads, true);
                wait_interrupt_t fInterruptible = fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE ? THREAD_ABORTSAFE : THREAD_UNINT;
                wait_result_t    rcWait;
                if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
                    rcWait = lck_spin_sleep(pThis->pSpinlock, LCK_SLEEP_DEFAULT, (event_t)&Waiter, fInterruptible);
                else
                {
                    uint64_t u64AbsTime;
                    nanoseconds_to_absolutetime(uNsAbsTimeout, &u64AbsTime);
                    rcWait = lck_spin_sleep_deadline(pThis->pSpinlock, LCK_SLEEP_DEFAULT,
                                                     (event_t)&Waiter, fInterruptible, u64AbsTime);
                }

                /*
                 * Deal with the wait result.
                 */
                if (RT_LIKELY(pThis->u32Magic == RTSEMEVENT_MAGIC))
                {
                    switch (rcWait)
                    {
                        case THREAD_AWAKENED:
                            if (RT_LIKELY(Waiter.fWokenUp))
                                rc = VINF_SUCCESS;
                            else if (fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE)
                                rc = VERR_INTERRUPTED;
                            else
                                continue; /* Seen this happen after fork/exec/something. */
                            break;

                        case THREAD_TIMED_OUT:
                            Assert(!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE));
                            rc = !Waiter.fWokenUp ? VERR_TIMEOUT : VINF_SUCCESS;
                            break;

                        case THREAD_INTERRUPTED:
                            Assert(fInterruptible != THREAD_UNINT);
                            rc = !Waiter.fWokenUp ? VERR_INTERRUPTED : VINF_SUCCESS;
                            break;

                        case THREAD_RESTART:
                            AssertMsg(pThis->u32Magic == ~RTSEMEVENT_MAGIC, ("%#x\n", pThis->u32Magic));
                            rc = VERR_SEM_DESTROYED;
                            break;

                        default:
                            AssertMsgFailed(("rcWait=%d\n", rcWait));
                            rc = VERR_INTERNAL_ERROR_3;
                            break;
                    }
                }
                else
                    rc = VERR_SEM_DESTROYED;
                break;
            }

            RTListNodeRemove(&Waiter.Node);
        }
    }

    lck_spin_unlock(pThis->pSpinlock);
    ASMSetFlags(fIntSaved);
    rtR0SemEventDarwinRelease(pThis);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
}


RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventDarwinWait(hEventSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventDarwinWait(hEventSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventDarwinWait(hEventSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    uint64_t cNs;
    absolutetime_to_nanoseconds(1, &cNs);
    return (uint32_t)cNs ? (uint32_t)cNs : 0;
}


RTR0DECL(bool) RTSemEventIsSignalSafe(void)
{
    /** @todo check the code...   */
    return false;
}
RT_EXPORT_SYMBOL(RTSemEventIsSignalSafe);

