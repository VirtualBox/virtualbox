/* $Id: semeventmulti-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiple Release Event Semaphores, Ring-0 Driver, Solaris.
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
#include "the-solaris-kernel.h"
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
#include "semeventwait-r0drv-solaris.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name fStateAndGen values
 * @{ */
/** The state bit number. */
#define RTSEMEVENTMULTISOL_STATE_BIT        0
/** The state mask. */
#define RTSEMEVENTMULTISOL_STATE_MASK       RT_BIT_32(RTSEMEVENTMULTISOL_STATE_BIT)
/** The generation mask. */
#define RTSEMEVENTMULTISOL_GEN_MASK         ~RTSEMEVENTMULTISOL_STATE_MASK
/** The generation shift. */
#define RTSEMEVENTMULTISOL_GEN_SHIFT        1
/** The initial variable value. */
#define RTSEMEVENTMULTISOL_STATE_GEN_INIT   UINT32_C(0xfffffffc)
/** @}  */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Solaris multiple release event semaphore.
 */
typedef struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value (RTSEMEVENTMULTI_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The number of references. */
    uint32_t volatile   cRefs;
    /** The object state bit and generation counter.
     * The generation counter is incremented every time the object is
     * signalled. */
    uint32_t volatile   fStateAndGen;
    /** The Solaris mutex protecting this structure and pairing up the with the cv. */
    kmutex_t            Mtx;
    /** The Solaris condition variable. */
    kcondvar_t          Cnd;
} RTSEMEVENTMULTIINTERNAL, *PRTSEMEVENTMULTIINTERNAL;



RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);
    AssertPtrReturn(phEventMultiSem, VERR_INVALID_POINTER);
    RT_ASSERT_PREEMPTIBLE();

    AssertCompile(sizeof(RTSEMEVENTMULTIINTERNAL) > sizeof(void *));
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic     = RTSEMEVENTMULTI_MAGIC;
        pThis->cRefs        = 1;
        pThis->fStateAndGen = RTSEMEVENTMULTISOL_STATE_GEN_INIT;
        mutex_init(&pThis->Mtx, "IPRT Multiple Release Event Semaphore", MUTEX_DRIVER, (void *)ipltospl(DISP_LEVEL));
        cv_init(&pThis->Cnd, "IPRT CV", CV_DRIVER, NULL);

        *phEventMultiSem = pThis;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Retain a reference to the semaphore.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventMultiSolRetain(PRTSEMEVENTMULTIINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000);
    NOREF(cRefs);
}


/**
 * Destructor that is called when cRefs == 0.
 *
 * @param   pThis       The instance to destroy.
 */
static void rtSemEventMultiDtor(PRTSEMEVENTMULTIINTERNAL pThis)
{
    Assert(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC);
    cv_destroy(&pThis->Cnd);
    mutex_destroy(&pThis->Mtx);
    RTMemFree(pThis);
}


/**
 * Release a reference, destroy the thing if necessary.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventMultiSolRelease(PRTSEMEVENTMULTIINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
        rtSemEventMultiDtor(pThis);
}



RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (pThis == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->cRefs > 0, ("pThis=%p cRefs=%d\n", pThis, pThis->cRefs), VERR_INVALID_HANDLE);
    RT_ASSERT_INTS_ON();

    mutex_enter(&pThis->Mtx);

    /* Invalidate the handle and wake up all threads that might be waiting on the semaphore. */
    Assert(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);
    ASMAtomicWriteU32(&pThis->u32Magic, RTSEMEVENTMULTI_MAGIC_DEAD);
    ASMAtomicAndU32(&pThis->fStateAndGen, RTSEMEVENTMULTISOL_GEN_MASK);
    cv_broadcast(&pThis->Cnd);

    /* Drop the reference from RTSemEventMultiCreateEx. */
    mutex_exit(&pThis->Mtx);
    rtR0SemEventMultiSolRelease(pThis);

    return VINF_SUCCESS;
}


RTDECL(int) RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    RT_ASSERT_PREEMPT_CPUID_VAR();

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);
    RT_ASSERT_INTS_ON();
    rtR0SemEventMultiSolRetain(pThis);
    rtR0SemSolWaitEnterMutexWithUnpinningHack(&pThis->Mtx);
    Assert(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    /*
     * Do the job.
     */
    uint32_t fNew = ASMAtomicUoReadU32(&pThis->fStateAndGen);
    fNew += 1 << RTSEMEVENTMULTISOL_GEN_SHIFT;
    fNew |= RTSEMEVENTMULTISOL_STATE_MASK;
    ASMAtomicWriteU32(&pThis->fStateAndGen, fNew);

    cv_broadcast(&pThis->Cnd);

    mutex_exit(&pThis->Mtx);

    rtR0SemEventMultiSolRelease(pThis);
#ifdef DEBUG_ramshankar
    /** See @bugref{6318#c11}. */
    return VINF_SUCCESS;
#endif
    RT_ASSERT_PREEMPT_CPUID();
    return VINF_SUCCESS;
}


RTDECL(int) RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    RT_ASSERT_PREEMPT_CPUID_VAR();

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);
    RT_ASSERT_INTS_ON();

    rtR0SemEventMultiSolRetain(pThis);
    rtR0SemSolWaitEnterMutexWithUnpinningHack(&pThis->Mtx);
    Assert(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    /*
     * Do the job (could be done without the lock, but play safe).
     */
    ASMAtomicAndU32(&pThis->fStateAndGen, ~RTSEMEVENTMULTISOL_STATE_MASK);

    mutex_exit(&pThis->Mtx);
    rtR0SemEventMultiSolRelease(pThis);

#ifdef DEBUG_ramshankar
    /** See @bugref{6318#c11}. */
    return VINF_SUCCESS;
#endif
    RT_ASSERT_PREEMPT_CPUID();
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
static int rtR0SemEventMultiSolWait(PRTSEMEVENTMULTIINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                                    PCRTLOCKVALSRCPOS pSrcPos)
{
    uint32_t    fOrgStateAndGen;
    int         rc;

    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiSolRetain(pThis);
    mutex_enter(&pThis->Mtx); /* this could be moved down to the else, but play safe for now. */

    /*
     * Is the event already signalled or do we have to wait?
     */
    fOrgStateAndGen = ASMAtomicUoReadU32(&pThis->fStateAndGen);
    if (fOrgStateAndGen & RTSEMEVENTMULTISOL_STATE_MASK)
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
            for (;;)
            {
                /* The destruction test. */
                if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC))
                    rc = VERR_SEM_DESTROYED;
                else
                {
                    /* Check the exit conditions. */
                    if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC))
                        rc = VERR_SEM_DESTROYED;
                    else if (ASMAtomicUoReadU32(&pThis->fStateAndGen) != fOrgStateAndGen)
                        rc = VINF_SUCCESS;
                    else if (rtR0SemSolWaitHasTimedOut(&Wait))
                        rc = VERR_TIMEOUT;
                    else if (rtR0SemSolWaitWasInterrupted(&Wait))
                        rc = VERR_INTERRUPTED;
                    else
                    {
                        /* Do the wait and then recheck the conditions. */
                        rtR0SemSolWaitDoIt(&Wait, &pThis->Cnd, &pThis->Mtx, &pThis->fStateAndGen, fOrgStateAndGen);
                        continue;
                    }
                }
                break;
            }
            rtR0SemSolWaitDelete(&Wait);
        }
    }

    mutex_exit(&pThis->Mtx);
    rtR0SemEventMultiSolRelease(pThis);
    return rc;
}



RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventMultiSolWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventMultiSolWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventMultiSolWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(uint32_t) RTSemEventMultiGetResolution(void)
{
    return rtR0SemSolWaitGetResolution();
}


RTR0DECL(bool) RTSemEventMultiIsSignalSafe(void)
{
    /* Don't trust solaris not to preempt us. */
    return false;
}

