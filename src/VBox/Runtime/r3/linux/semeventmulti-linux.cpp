/* $Id: semeventmulti-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiple Release Event Semaphore, Linux (2.6.x+).
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


#include <features.h>
#if __GLIBC_PREREQ(2,6) && !defined(IPRT_WITH_FUTEX_BASED_SEMS)

/*
 * glibc 2.6 fixed a serious bug in the mutex implementation. We wrote this
 * linux specific event semaphores code in order to work around the bug. As it
 * turns out, this code seems to have an unresolved issue (@bugref{2599}), so we'll
 * fall back on the pthread based implementation if glibc is known to contain
 * the bug fix.
 *
 * The external reference to epoll_pwait is a hack which prevents that we link
 * against glibc < 2.6.
 */
#include "../posix/semeventmulti-posix.cpp"
__asm__ (".global epoll_pwait");

#else /* glibc < 2.6 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#include "internal/magics.h"
#include "internal/strict.h"


#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/syscall.h>

#include "semwait-linux.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Linux multiple wakup event semaphore.
 */
struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value. */
    uint32_t volatile   u32Magic;
    /** The futex state variable, see RTSEMEVENTMULTI_LNX_XXX. */
    uint32_t volatile   uState;
#ifdef RT_STRICT
    /** Increased on every signalling call. */
    uint32_t volatile   uSignalSerialNo;
#endif
#ifdef RTSEMEVENTMULTI_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
};


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name RTSEMEVENTMULTI_LNX_XXX - state
 * @{ */
#define RTSEMEVENTMULTI_LNX_NOT_SIGNALED            UINT32_C(0x00000000)
#define RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS    UINT32_C(0x00000001)
#define RTSEMEVENTMULTI_LNX_SIGNALED                UINT32_C(0x00000003)
/** @} */

#define ASSERT_VALID_STATE(a_uState) \
    AssertMsg(   (a_uState) == RTSEMEVENTMULTI_LNX_NOT_SIGNALED \
              || (a_uState) == RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS \
              || (a_uState) == RTSEMEVENTMULTI_LNX_SIGNALED, \
              (#a_uState "=%s\n", a_uState))


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Whether we can use FUTEX_WAIT_BITSET. */
static int volatile g_fCanUseWaitBitSet = -1;


RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Make sure we know whether FUTEX_WAIT_BITSET works.
     */
    rtSemLinuxCheckForFutexWaitBitSet(&g_fCanUseWaitBitSet);
#if defined(DEBUG_bird) && !defined(IN_GUEST)
    Assert(g_fCanUseWaitBitSet == true);
#endif

    /*
     * Allocate semaphore handle.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = (struct RTSEMEVENTMULTIINTERNAL *)RTMemAlloc(sizeof(struct RTSEMEVENTMULTIINTERNAL));
    if (pThis)
    {
        pThis->u32Magic        = RTSEMEVENTMULTI_MAGIC;
        pThis->uState          = RTSEMEVENTMULTI_LNX_NOT_SIGNALED;
#ifdef RT_STRICT
        pThis->uSignalSerialNo = 0;
#endif
#ifdef RTSEMEVENTMULTI_STRICT
        if (!pszNameFmt)
        {
            static uint32_t volatile s_iSemEventMultiAnon = 0;
            RTLockValidatorRecSharedInit(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                         true /*fSignaller*/, !(fFlags & RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL),
                                         "RTSemEventMulti-%u", ASMAtomicIncU32(&s_iSemEventMultiAnon) - 1);
        }
        else
        {
            va_list va;
            va_start(va, pszNameFmt);
            RTLockValidatorRecSharedInitV(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                          true /*fSignaller*/, !(fFlags & RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL),
                                          pszNameFmt, va);
            va_end(va);
        }
        pThis->fEverHadSignallers = false;
#else
        RT_NOREF(hClass, pszNameFmt);
#endif

        *phEventMultiSem = pThis;
        return VINF_SUCCESS;
    }
    return  VERR_NO_MEMORY;
}


RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    if (pThis == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Invalidate the semaphore and wake up anyone waiting on it.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, RTSEMEVENTMULTI_MAGIC + 1);
    if (ASMAtomicXchgU32(&pThis->uState, RTSEMEVENTMULTI_LNX_SIGNALED) == RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS)
    {
        sys_futex(&pThis->uState, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
        usleep(1000);
    }

    /*
     * Free the semaphore memory and be gone.
     */
#ifdef RTSEMEVENTMULTI_STRICT
    RTLockValidatorRecSharedDelete(&pThis->Signallers);
#endif
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSEMEVENTMULTI_STRICT
    if (pThis->fEverHadSignallers)
    {
        int rc9 = RTLockValidatorRecSharedCheckSignaller(&pThis->Signallers, NIL_RTTHREAD);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Signal it.
     */
#ifdef RT_STRICT
    ASMAtomicIncU32(&pThis->uSignalSerialNo);
#endif
    uint32_t uOld = ASMAtomicXchgU32(&pThis->uState, RTSEMEVENTMULTI_LNX_SIGNALED);
    if (uOld == RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS)
    {
        /* wake up sleeping threads. */
        long cWoken = sys_futex(&pThis->uState, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
        AssertMsg(cWoken >= 0, ("%ld\n", cWoken)); NOREF(cWoken);
    }
    ASSERT_VALID_STATE(uOld);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);
#ifdef RT_STRICT
    uint32_t const uState = pThis->uState;
    ASSERT_VALID_STATE(uState);
#endif

    /*
     * Reset it.
     */
    ASMAtomicCmpXchgU32(&pThis->uState, RTSEMEVENTMULTI_LNX_NOT_SIGNALED, RTSEMEVENTMULTI_LNX_SIGNALED);
    return VINF_SUCCESS;
}


/**
 * Performs an indefinite wait on the event.
 */
static int rtSemEventMultiLinuxWaitIndefinite(struct RTSEMEVENTMULTIINTERNAL *pThis, uint32_t fFlags, PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF(pSrcPos);

    /*
     * Quickly check whether it's signaled.
     */
    uint32_t uState = ASMAtomicUoReadU32(&pThis->uState);
    if (uState == RTSEMEVENTMULTI_LNX_SIGNALED)
        return VINF_SUCCESS;
    ASSERT_VALID_STATE(uState);

    /*
     * The wait loop.
     */
#ifdef RTSEMEVENTMULTI_STRICT
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
#endif
    for (unsigned i = 0;; i++)
    {
        /*
         * Start waiting. We only account for there being or having been
         * threads waiting on the semaphore to keep things simple.
         */
        uState = ASMAtomicUoReadU32(&pThis->uState);
        if (   uState == RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS
            || (   uState == RTSEMEVENTMULTI_LNX_NOT_SIGNALED
                && ASMAtomicCmpXchgU32(&pThis->uState, RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS,
                                       RTSEMEVENTMULTI_LNX_NOT_SIGNALED)))
        {
#ifdef RTSEMEVENTMULTI_STRICT
            if (pThis->fEverHadSignallers)
            {
                int rc9 = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                                RT_INDEFINITE_WAIT, RTTHREADSTATE_EVENT_MULTI, true);
                if (RT_FAILURE(rc9))
                    return rc9;
            }
#endif
#ifdef RT_STRICT
            uint32_t const uPrevSignalSerialNo = ASMAtomicReadU32(&pThis->uSignalSerialNo);
#endif
            RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT_MULTI, true);
            long rc = sys_futex(&pThis->uState, FUTEX_WAIT, 1, NULL /*pTimeout*/, NULL, 0);
            RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT_MULTI);

            /* Check that the structure is still alive before continuing. */
            if (RT_LIKELY(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC))
            { /*likely*/ }
            else
                return VERR_SEM_DESTROYED;

            /*
             * Return if success.
             */
            if (rc == 0)
            {
                Assert(uPrevSignalSerialNo != ASMAtomicReadU32(&pThis->uSignalSerialNo));
                return VINF_SUCCESS;
            }

            /*
             * Act on the wakup code.
             */
            if (rc == -EWOULDBLOCK)
                /* retry, the value changed. */;
            else if (rc == -EINTR)
            {
                if (fFlags & RTSEMWAIT_FLAGS_NORESUME)
                    return VERR_INTERRUPTED;
            }
            else
            {
                /* this shouldn't happen! */
                AssertMsgFailed(("rc=%ld errno=%d\n", rc, errno));
                return RTErrConvertFromErrno(rc);
            }
        }
        else if (uState == RTSEMEVENTMULTI_LNX_SIGNALED)
            return VINF_SUCCESS;
        else
            ASSERT_VALID_STATE(uState);
    }
}


/**
 * Handle polling (timeout already expired at the time of the call).
 *
 * @returns VINF_SUCCESS, VERR_TIMEOUT, VERR_SEM_DESTROYED.
 * @param   pThis               The semaphore.
 */
static int rtSemEventMultiLinuxWaitPoll(struct RTSEMEVENTMULTIINTERNAL *pThis)
{
    uint32_t uState = ASMAtomicUoReadU32(&pThis->uState);
    if (uState == RTSEMEVENTMULTI_LNX_SIGNALED)
        return VINF_SUCCESS;
    return VERR_TIMEOUT;
}


/**
 * Performs an indefinite wait on the event.
 */
static int rtSemEventMultiLinuxWaitTimed(struct RTSEMEVENTMULTIINTERNAL *pThis, uint32_t fFlags, uint64_t uTimeout,
                                         PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF(pSrcPos);

    /*
     * Quickly check whether it's signaled.
     */
    uint32_t uState = ASMAtomicUoReadU32(&pThis->uState);
    if (uState == RTSEMEVENTMULTI_LNX_SIGNALED)
        return VINF_SUCCESS;
    ASSERT_VALID_STATE(uState);

    /*
     * Convert the timeout value.
     */
    struct timespec TsTimeout;
    int             iWaitOp;
    uint32_t        uWaitVal3;
    uint64_t        nsAbsTimeout = uTimeout; /* (older gcc maybe used uninitialized) */
    uTimeout = rtSemLinuxCalcDeadline(fFlags, uTimeout, g_fCanUseWaitBitSet, &TsTimeout, &iWaitOp, &uWaitVal3, &nsAbsTimeout);
    if (uTimeout == 0)
        return rtSemEventMultiLinuxWaitPoll(pThis);
    if (uTimeout == UINT64_MAX)
        return rtSemEventMultiLinuxWaitIndefinite(pThis, fFlags, pSrcPos);

    /*
     * The wait loop.
     */
#ifdef RTSEMEVENTMULTI_STRICT
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
#endif
    for (unsigned i = 0;; i++)
    {
        /*
         * Start waiting. We only account for there being or having been
         * threads waiting on the semaphore to keep things simple.
         */
        uState = ASMAtomicUoReadU32(&pThis->uState);
        if (   uState == RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS
            || (   uState == RTSEMEVENTMULTI_LNX_NOT_SIGNALED
                && ASMAtomicCmpXchgU32(&pThis->uState, RTSEMEVENTMULTI_LNX_NOT_SIGNALED_WAITERS,
                                       RTSEMEVENTMULTI_LNX_NOT_SIGNALED)))
        {
#ifdef RTSEMEVENTMULTI_STRICT
            if (pThis->fEverHadSignallers)
            {
                int rc9 = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                                uTimeout / UINT32_C(1000000), RTTHREADSTATE_EVENT_MULTI, true);
                if (RT_FAILURE(rc9))
                    return rc9;
            }
#endif
#ifdef RT_STRICT
            uint32_t const uPrevSignalSerialNo = ASMAtomicReadU32(&pThis->uSignalSerialNo);
#endif
            RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT_MULTI, true);
            long rc = sys_futex(&pThis->uState, iWaitOp, 1, &TsTimeout, NULL, uWaitVal3);
            RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT_MULTI);

            /* Check that the structure is still alive before continuing. */
            if (RT_LIKELY(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC))
            { /*likely*/ }
            else
                return VERR_SEM_DESTROYED;

            /*
             * Return if success.
             */
            if (rc == 0)
            {
                Assert(uPrevSignalSerialNo != ASMAtomicReadU32(&pThis->uSignalSerialNo));
                return VINF_SUCCESS;
            }

            /*
             * Act on the wakup code.
             */
            if (rc == -ETIMEDOUT)
            {
                /** @todo something is broken here. shows up every now and again in the ata
                 *        code. Should try to run the timeout against RTTimeMilliTS to
                 *        check that it's doing the right thing... */
#ifdef RT_STRICT
                uint64_t const uNow = RTTimeNanoTS();
                AssertMsg(uNow >= nsAbsTimeout || nsAbsTimeout - uNow < RT_NS_1MS,
                          ("%#RX64 - %#RX64 => %#RX64 (%RI64)\n", nsAbsTimeout, uNow, nsAbsTimeout - uNow, nsAbsTimeout - uNow));
#endif
                return VERR_TIMEOUT;
            }
            if (rc == -EWOULDBLOCK)
            {
                /* retry, the value changed. */;
            }
            else if (rc == -EINTR)
            {
                if (fFlags & RTSEMWAIT_FLAGS_NORESUME)
                    return VERR_INTERRUPTED;
            }
            else
            {
                /* this shouldn't happen! */
                AssertMsgFailed(("rc=%ld errno=%d\n", rc, errno));
                return RTErrConvertFromErrno(rc);
            }
        }
        else if (uState == RTSEMEVENTMULTI_LNX_SIGNALED)
            return VINF_SUCCESS;
        else
            ASSERT_VALID_STATE(uState);

        /* adjust the relative timeout if relative */
        if (iWaitOp == FUTEX_WAIT)
        {
            int64_t i64Diff = nsAbsTimeout - RTTimeSystemNanoTS();
            if (i64Diff < 1000)
                return VERR_TIMEOUT;
            TsTimeout.tv_sec  = (uint64_t)i64Diff / RT_NS_1SEC;
            TsTimeout.tv_nsec = (uint64_t)i64Diff % RT_NS_1SEC;
        }
    }
}

/**
 * Internal wait worker function.
 */
DECLINLINE(int) rtSemEventLnxMultiWait(RTSEMEVENTMULTI hEventSem, uint32_t fFlags, uint64_t uTimeout, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

    /*
     * Timed or indefinite wait?
     */
    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
        return rtSemEventMultiLinuxWaitIndefinite(pThis, fFlags, pSrcPos);
    return rtSemEventMultiLinuxWaitTimed(hEventSem, fFlags, uTimeout, pSrcPos);
}


#undef RTSemEventMultiWaitEx
RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtSemEventLnxMultiWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemEventLnxMultiWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemEventLnxMultiWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(void) RTSemEventMultiSetSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENTMULTI_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedResetOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF(hEventMultiSem, hThread);
#endif
}


RTDECL(void) RTSemEventMultiAddSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENTMULTI_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedAddOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF(hEventMultiSem, hThread);
#endif
}


RTDECL(void) RTSemEventMultiRemoveSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENTMULTI_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    RTLockValidatorRecSharedRemoveOwner(&pThis->Signallers, hThread);
#else
    RT_NOREF(hEventMultiSem, hThread);
#endif
}

#endif /* glibc < 2.6 || IPRT_WITH_FUTEX_BASED_SEMS */

