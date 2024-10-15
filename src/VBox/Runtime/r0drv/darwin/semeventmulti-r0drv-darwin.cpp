/* $Id: semeventmulti-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiple Release Event Semaphores, Ring-0 Driver, Darwin.
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
#define RTSEMEVENTMULTI_WITHOUT_REMAPPING
#define RTMEM_NO_WRAP_TO_EF_APIS /* rtR0MemObjNativeProtect depends on this code, so no electrical fences here or we'll \#DF. */
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name fStateAndGen values
 * @{ */
/** The state bit number. */
#define RTSEMEVENTMULTIDARWIN_STATE_BIT         0
/** The state mask. */
#define RTSEMEVENTMULTIDARWIN_STATE_MASK        RT_BIT_32(RTSEMEVENTMULTIDARWIN_STATE_BIT)
/** The generation mask. */
#define RTSEMEVENTMULTIDARWIN_GEN_MASK          ~RTSEMEVENTMULTIDARWIN_STATE_MASK
/** The generation shift. */
#define RTSEMEVENTMULTIDARWIN_GEN_SHIFT         1
/** The initial variable value. */
#define RTSEMEVENTMULTIDARWIN_STATE_GEN_INIT    UINT32_C(0xfffffffc)
/** @}  */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Darwin multiple release event semaphore.
 */
typedef struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value (RTSEMEVENTMULTI_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The object state bit and generation counter.
     * The generation counter is incremented every time the object is
     * signalled. */
    uint32_t volatile   fStateAndGen;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** Set if there are blocked threads. */
    bool volatile       fHaveBlockedThreads;
    /** The spinlock protecting us. */
    lck_spin_t         *pSpinlock;
} RTSEMEVENTMULTIINTERNAL, *PRTSEMEVENTMULTIINTERNAL;



RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    RT_NOREF(hClass, pszNameFmt);
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);
    AssertCompile(sizeof(RTSEMEVENTMULTIINTERNAL) > sizeof(void *));
    AssertPtrReturn(phEventMultiSem, VERR_INVALID_POINTER);
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic             = RTSEMEVENTMULTI_MAGIC;
        pThis->fStateAndGen         = RTSEMEVENTMULTIDARWIN_STATE_GEN_INIT;
        pThis->cRefs                = 1;
        pThis->fHaveBlockedThreads  = false;
        Assert(g_pDarwinLockGroup);
        pThis->pSpinlock = lck_spin_alloc_init(g_pDarwinLockGroup, LCK_ATTR_NULL);
        if (pThis->pSpinlock)
        {
            *phEventMultiSem = pThis;
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
DECLINLINE(void) rtR0SemEventMultiDarwinRetain(PRTSEMEVENTMULTIINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000);
    RT_NOREF_PV(cRefs);
}


/**
 * Release a reference, destroy the thing if necessary.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventMultiDarwinRelease(PRTSEMEVENTMULTIINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        Assert(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC);

        lck_spin_destroy(pThis->pSpinlock, g_pDarwinLockGroup);
        RTMemFree(pThis);

        IPRT_DARWIN_RESTORE_EFL_AC();
    }
}


RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (pThis == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs > 0);
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();

    RTCCUINTREG const fIntSaved = ASMIntDisableFlags();
    lck_spin_lock(pThis->pSpinlock);

    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENTMULTI_MAGIC); /* make the handle invalid */
    ASMAtomicAndU32(&pThis->fStateAndGen, RTSEMEVENTMULTIDARWIN_GEN_MASK);
    if (pThis->fHaveBlockedThreads)
    {
        /* abort waiting threads. */
        thread_wakeup_prim((event_t)pThis, FALSE /* all threads */, THREAD_RESTART);
    }

    lck_spin_unlock(pThis->pSpinlock);
    ASMSetFlags(fIntSaved);
    rtR0SemEventMultiDarwinRelease(pThis);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    RT_ASSERT_PREEMPT_CPUID_VAR();

    /*
     * Coming here with interrupts disabled should be okay.  The thread_wakeup_prim KPI is used
     * by the interrupt handler IOFilterInterruptEventSource::disableInterruptOccurred() via
     * signalWorkAvailable().  The only problem is if we have to destroy the event structure,
     * as RTMemFree does not work with interrupts disabled (IOFree/kfree takes zone mutex).
     */
    //RT_ASSERT_INTS_ON(); - we may be called from interrupt context, which seems to be perfectly fine if we disable interrupts.

    IPRT_DARWIN_SAVE_EFL_AC();

    RTCCUINTREG const fIntSaved = ASMIntDisableFlags();
    rtR0SemEventMultiDarwinRetain(pThis);
    lck_spin_lock(pThis->pSpinlock);

    /*
     * Set the signal and increment the generation counter.
     */
    uint32_t fNew = ASMAtomicUoReadU32(&pThis->fStateAndGen);
    fNew += 1 << RTSEMEVENTMULTIDARWIN_GEN_SHIFT;
    fNew |= RTSEMEVENTMULTIDARWIN_STATE_MASK;
    ASMAtomicWriteU32(&pThis->fStateAndGen, fNew);

    /*
     * Wake up all sleeping threads.
     */
    if (pThis->fHaveBlockedThreads)
    {
        ASMAtomicWriteBool(&pThis->fHaveBlockedThreads, false);
        thread_wakeup_prim((event_t)pThis, FALSE /* all threads */, THREAD_AWAKENED);
    }

    lck_spin_unlock(pThis->pSpinlock);
    ASMSetFlags(fIntSaved);
    rtR0SemEventMultiDarwinRelease(pThis);

    RT_ASSERT_PREEMPT_CPUID();
    AssertMsg((fSavedEfl & X86_EFL_IF) == (ASMGetFlags() & X86_EFL_IF), ("fSavedEfl=%#x cur=%#x\n",(uint32_t)fSavedEfl, ASMGetFlags()));
    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    RT_ASSERT_PREEMPT_CPUID_VAR();
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();

    RTCCUINTREG const fIntSaved = ASMIntDisableFlags();
    rtR0SemEventMultiDarwinRetain(pThis);
    lck_spin_lock(pThis->pSpinlock);

    ASMAtomicAndU32(&pThis->fStateAndGen, ~RTSEMEVENTMULTIDARWIN_STATE_MASK);

    lck_spin_unlock(pThis->pSpinlock);
    ASMSetFlags(fIntSaved);
    rtR0SemEventMultiDarwinRelease(pThis);

    RT_ASSERT_PREEMPT_CPUID();
    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


/**
 * Worker for RTSemEventMultiWaitEx and RTSemEventMultiWaitExDebug.
 *
 * @returns VBox status code.
 * @param   pThis           The event semaphore.
 * @param   fFlags          See RTSemEventMultiWaitEx.
 * @param   uTimeout        See RTSemEventMultiWaitEx.
 * @param   pSrcPos         The source code position of the wait.
 */
static int rtR0SemEventMultiDarwinWait(PRTSEMEVENTMULTIINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                                       PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF(pSrcPos);

    /*
     * Validate input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);
    if (uTimeout != 0 || (fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
        RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    RTCCUINTREG const fIntSaved = ASMIntDisableFlags();
    rtR0SemEventMultiDarwinRetain(pThis);
    lck_spin_lock(pThis->pSpinlock);

    /*
     * Is the event already signalled or do we have to wait?
     */
    int rc;
    uint32_t const fOrgStateAndGen = ASMAtomicUoReadU32(&pThis->fStateAndGen);
    if (fOrgStateAndGen & RTSEMEVENTMULTIDARWIN_STATE_MASK)
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
            for (;;)
            {
                /*
                 * Do the actual waiting.
                 */
                ASMAtomicWriteBool(&pThis->fHaveBlockedThreads, true);
                wait_interrupt_t fInterruptible = fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE ? THREAD_ABORTSAFE : THREAD_UNINT;
                wait_result_t    rcWait;
                if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
                    rcWait = lck_spin_sleep(pThis->pSpinlock, LCK_SLEEP_DEFAULT, (event_t)pThis, fInterruptible);
                else
                {
                    uint64_t u64AbsTime;
                    nanoseconds_to_absolutetime(uNsAbsTimeout, &u64AbsTime);
                    rcWait = lck_spin_sleep_deadline(pThis->pSpinlock, LCK_SLEEP_DEFAULT,
                                                     (event_t)pThis, fInterruptible, u64AbsTime);
                }

                /*
                 * Deal with the wait result.
                 */
                if (RT_LIKELY(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC))
                {
                    switch (rcWait)
                    {
                        case THREAD_AWAKENED:
                            if (RT_LIKELY(ASMAtomicUoReadU32(&pThis->fStateAndGen) != fOrgStateAndGen))
                                rc = VINF_SUCCESS;
                            else if (fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE)
                                rc = VERR_INTERRUPTED;
                            else
                                continue; /* Seen this happen after fork/exec/something. */
                            break;

                        case THREAD_TIMED_OUT:
                            Assert(!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE));
                            rc = VERR_TIMEOUT;
                            break;

                        case THREAD_INTERRUPTED:
                            Assert(fInterruptible != THREAD_UNINT);
                            rc = VERR_INTERRUPTED;
                            break;

                        case THREAD_RESTART:
                            AssertMsg(pThis->u32Magic == ~RTSEMEVENTMULTI_MAGIC, ("%#x\n", pThis->u32Magic));
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
        }
    }

    lck_spin_unlock(pThis->pSpinlock);
    ASMSetFlags(fIntSaved);
    rtR0SemEventMultiDarwinRelease(pThis);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
}

RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventMultiDarwinWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventMultiDarwinWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventMultiDarwinWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(uint32_t) RTSemEventMultiGetResolution(void)
{
    uint64_t cNs;
    absolutetime_to_nanoseconds(1, &cNs);
    return (uint32_t)cNs ? (uint32_t)cNs : 0;
}


RTR0DECL(bool) RTSemEventMultiIsSignalSafe(void)
{
    /** @todo check the code...   */
    return false;
}

