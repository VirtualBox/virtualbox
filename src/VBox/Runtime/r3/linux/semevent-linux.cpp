/* $Id: semevent-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Event Semaphore, Linux (2.6.0 and later).
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
 * linux specific event semaphores code in order to work around the bug. We
 * will fall back on the pthread-based implementation if glibc is known to
 * contain the bug fix.
 *
 * The external reference to epoll_pwait is a hack which prevents that we link
 * against glibc < 2.6.
 */
# include "../posix/semevent-posix.cpp"
__asm__ (".global epoll_pwait");

#else /* glibc < 2.6 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#include "internal/magics.h"
#include "internal/mem.h"
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
 * Linux (single wakup) event semaphore.
 */
struct RTSEMEVENTINTERNAL
{
    /** Magic value. */
    intptr_t volatile   iMagic;
    /** The futex state variable.
     * 0 means not signalled.
       1 means signalled. */
    uint32_t volatile   fSignalled;
    /** The number of waiting threads */
    int32_t volatile    cWaiters;
#ifdef RTSEMEVENT_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
    /** The creation flags. */
    uint32_t            fFlags;
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Whether we can use FUTEX_WAIT_BITSET. */
static int volatile g_fCanUseWaitBitSet = -1;




RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));

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
    struct RTSEMEVENTINTERNAL *pThis;
    if (!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
        pThis = (struct RTSEMEVENTINTERNAL *)RTMemAlloc(sizeof(struct RTSEMEVENTINTERNAL));
    else
        pThis = (struct RTSEMEVENTINTERNAL *)rtMemBaseAlloc(sizeof(struct RTSEMEVENTINTERNAL));
    if (pThis)
    {
        pThis->iMagic     = RTSEMEVENT_MAGIC;
        pThis->cWaiters   = 0;
        pThis->fSignalled = 0;
        pThis->fFlags     = fFlags;
#ifdef RTSEMEVENT_STRICT
        if (!pszNameFmt)
        {
            static uint32_t volatile s_iSemEventAnon = 0;
            RTLockValidatorRecSharedInit(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                         true /*fSignaller*/, !(fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL),
                                         "RTSemEvent-%u", ASMAtomicIncU32(&s_iSemEventAnon) - 1);
        }
        else
        {
            va_list va;
            va_start(va, pszNameFmt);
            RTLockValidatorRecSharedInitV(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                          true /*fSignaller*/, !(fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL),
                                          pszNameFmt, va);
            va_end(va);
        }
        pThis->fEverHadSignallers = false;
#else
        RT_NOREF(hClass, pszNameFmt);
#endif

        *phEventSem = pThis;
        return VINF_SUCCESS;
    }
    return  VERR_NO_MEMORY;
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->iMagic == RTSEMEVENT_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Invalidate the semaphore and wake up anyone waiting on it.
     */
    ASMAtomicXchgSize(&pThis->iMagic, RTSEMEVENT_MAGIC | UINT32_C(0x80000000));
    if (ASMAtomicXchgS32(&pThis->cWaiters, INT32_MIN / 2) > 0)
    {
        sys_futex(&pThis->fSignalled, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
        usleep(1000);
    }

    /*
     * Free the semaphore memory and be gone.
     */
#ifdef RTSEMEVENT_STRICT
    RTLockValidatorRecSharedDelete(&pThis->Signallers);
#endif
    if (!(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
        RTMemFree(pThis);
    else
        rtMemBaseFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->iMagic == RTSEMEVENT_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSEMEVENT_STRICT
    if (pThis->fEverHadSignallers)
    {
        int rc9 = RTLockValidatorRecSharedCheckSignaller(&pThis->Signallers, NIL_RTTHREAD);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    ASMAtomicWriteU32(&pThis->fSignalled, 1);
    if (ASMAtomicReadS32(&pThis->cWaiters) < 1)
        return VINF_SUCCESS;

    /* somebody is waiting, try wake up one of them. */
    long cWoken = sys_futex(&pThis->fSignalled, FUTEX_WAKE, 1, NULL, NULL, 0);
    if (RT_LIKELY(cWoken >= 0))
        return VINF_SUCCESS;

    if (RT_UNLIKELY(pThis->iMagic != RTSEMEVENT_MAGIC))
        return VERR_SEM_DESTROYED;

    return VERR_INVALID_PARAMETER;
}


/**
 * Performs an indefinite wait on the event.
 */
static int rtSemEventLinuxWaitIndefinite(struct RTSEMEVENTINTERNAL *pThis, uint32_t fFlags, PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF_PV(pSrcPos);

    /*
     * Quickly check whether it's signaled and there are no other waiters.
     */
    uint32_t cWaiters = ASMAtomicIncS32(&pThis->cWaiters);
    if (   cWaiters == 1
        && ASMAtomicCmpXchgU32(&pThis->fSignalled, 0, 1))
    {
        ASMAtomicDecS32(&pThis->cWaiters);
        return VINF_SUCCESS;
    }

    /*
     * The wait loop.
     */
#ifdef RTSEMEVENT_STRICT
    RTTHREAD hThreadSelf = !(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)
                         ? RTThreadSelfAutoAdopt()
                         : RTThreadSelf();
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
#endif
    int rc = VINF_SUCCESS;
    for (;;)
    {
#ifdef RTSEMEVENT_STRICT
        if (pThis->fEverHadSignallers)
        {
            rc = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                       RT_INDEFINITE_WAIT, RTTHREADSTATE_EVENT, true);
            if (RT_FAILURE(rc))
                break;
        }
#endif
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT, true);
        long lrc = sys_futex(&pThis->fSignalled, FUTEX_WAIT, 0, NULL /*pTimeout*/, NULL, 0);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT);
        if (RT_UNLIKELY(pThis->iMagic != RTSEMEVENT_MAGIC))
        {
            rc = VERR_SEM_DESTROYED;
            break;
        }

        if (RT_LIKELY(lrc == 0 || lrc == -EWOULDBLOCK))
        {
            /* successful wakeup or fSignalled > 0 in the meantime */
            if (ASMAtomicCmpXchgU32(&pThis->fSignalled, 0, 1))
                break;
        }
        else if (lrc == -ETIMEDOUT)
        {
            rc = VERR_TIMEOUT;
            break;
        }
        else if (lrc == -EINTR)
        {
            if (fFlags & RTSEMWAIT_FLAGS_NORESUME)
            {
                rc = VERR_INTERRUPTED;
                break;
            }
        }
        else
        {
            /* this shouldn't happen! */
            AssertMsgFailed(("rc=%ld errno=%d\n", lrc, errno));
            rc = RTErrConvertFromErrno(lrc);
            break;
        }
    }

    ASMAtomicDecS32(&pThis->cWaiters);
    return rc;
}


/**
 * Handle polling (timeout already expired at the time of the call).
 *
 * @returns VINF_SUCCESS, VERR_TIMEOUT, VERR_SEM_DESTROYED.
 * @param   pThis               The semaphore.
 */
static int rtSemEventLinuxWaitPoll(struct RTSEMEVENTINTERNAL *pThis)
{
    /*
     * What we do here is isn't quite fair to anyone else waiting on it, however
     * it might not be as bad as all that for callers making repeated poll calls
     * because they cannot block, as that would be a virtual wait but without the
     * chance of a permanept queue position.   So, I hope we can live with this.
     */
    if (ASMAtomicCmpXchgU32(&pThis->fSignalled, 0, 1))
        return VINF_SUCCESS;
    return VERR_TIMEOUT;
}


/**
 * Performs an timed wait on the event.
 */
static int rtSemEventLinuxWaitTimed(struct RTSEMEVENTINTERNAL *pThis, uint32_t fFlags,
                                    uint64_t uTimeout, PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF_PV(pSrcPos);

    /*
     * Convert the timeout value.
     */
    struct timespec TsTimeout;
    int             iWaitOp;
    uint32_t        uWaitVal3;
    uint64_t        nsAbsTimeout = uTimeout; /* (older gcc maybe used uninitialized) */
    uTimeout = rtSemLinuxCalcDeadline(fFlags, uTimeout, g_fCanUseWaitBitSet, &TsTimeout, &iWaitOp, &uWaitVal3, &nsAbsTimeout);
    if (uTimeout == 0)
        return rtSemEventLinuxWaitPoll(pThis);
    if (uTimeout == UINT64_MAX)
        return rtSemEventLinuxWaitIndefinite(pThis, fFlags, pSrcPos);

    /*
     * Quickly check whether it's signaled and there are no other waiters.
     */
    uint32_t cWaiters = ASMAtomicIncS32(&pThis->cWaiters);
    if (   cWaiters == 1
        && ASMAtomicCmpXchgU32(&pThis->fSignalled, 0, 1))
    {
        ASMAtomicDecS32(&pThis->cWaiters);
        return VINF_SUCCESS;
    }

    /*
     * The wait loop.
     */
#ifdef RTSEMEVENT_STRICT
    RTTHREAD hThreadSelf = !(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)
                         ? RTThreadSelfAutoAdopt()
                         : RTThreadSelf();
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
#endif
    int rc = VINF_SUCCESS;
    for (;;)
    {
#ifdef RTSEMEVENT_STRICT
        if (pThis->fEverHadSignallers)
        {
            rc = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                       iWaitOp == FUTEX_WAIT ? uTimeout / RT_NS_1MS : RT_MS_1HOUR /*whatever*/,
                                                       RTTHREADSTATE_EVENT, true);
            if (RT_FAILURE(rc))
                break;
        }
#endif
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT, true);
        long lrc = sys_futex(&pThis->fSignalled, iWaitOp, 0, &TsTimeout, NULL, uWaitVal3);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT);
        if (RT_UNLIKELY(pThis->iMagic != RTSEMEVENT_MAGIC))
        {
            rc = VERR_SEM_DESTROYED;
            break;
        }

        if (RT_LIKELY(lrc == 0 || lrc == -EWOULDBLOCK))
        {
            /* successful wakeup or fSignalled > 0 in the meantime */
            if (ASMAtomicCmpXchgU32(&pThis->fSignalled, 0, 1))
                break;
        }
        else if (lrc == -ETIMEDOUT)
        {
#ifdef RT_STRICT
            uint64_t const uNow = RTTimeNanoTS();
            AssertMsg(uNow >= nsAbsTimeout || nsAbsTimeout - uNow < RT_NS_1MS,
                      ("%#RX64 - %#RX64 => %#RX64 (%RI64)\n", nsAbsTimeout, uNow, nsAbsTimeout - uNow, nsAbsTimeout - uNow));
#endif
            rc = VERR_TIMEOUT;
            break;
        }
        else if (lrc == -EINTR)
        {
            if (fFlags & RTSEMWAIT_FLAGS_NORESUME)
            {
                rc = VERR_INTERRUPTED;
                break;
            }
        }
        else
        {
            /* this shouldn't happen! */
            AssertMsgFailed(("rc=%ld errno=%d\n", lrc, errno));
            rc = RTErrConvertFromErrno(lrc);
            break;
        }

        /* adjust the relative timeout */
        if (iWaitOp == FUTEX_WAIT)
        {
            int64_t i64Diff = nsAbsTimeout - RTTimeSystemNanoTS();
            if (i64Diff < 1000)
            {
                rc = VERR_TIMEOUT;
                break;
            }
            TsTimeout.tv_sec  = (uint64_t)i64Diff / RT_NS_1SEC;
            TsTimeout.tv_nsec = (uint64_t)i64Diff % RT_NS_1SEC;
        }
    }

    ASMAtomicDecS32(&pThis->cWaiters);
    return rc;
}


/**
 * Internal wait worker function.
 */
DECLINLINE(int) rtSemEventLinuxWait(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->iMagic == RTSEMEVENT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);
#ifdef RT_STRICT
    uint32_t const fSignalled = pThis->fSignalled;
    Assert(fSignalled == false || fSignalled == true);
#endif

    /*
     * Timed or indefinite wait?
     */
    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
        return rtSemEventLinuxWaitIndefinite(pThis, fFlags, pSrcPos);
    return rtSemEventLinuxWaitTimed(hEventSem, fFlags, uTimeout, pSrcPos);
}


RTDECL(int) RTSemEventWait(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies)
{
    int rc;
#ifndef RTSEMEVENT_STRICT
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, NULL);
    else
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, &SrcPos);
    else
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, &SrcPos);
#endif
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


RTDECL(int)  RTSemEventWaitNoResume(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies)
{
    int rc;
#ifndef RTSEMEVENT_STRICT
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, NULL);
    else
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, &SrcPos);
    else
        rc = rtSemEventLinuxWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, &SrcPos);
#endif
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtSemEventLinuxWait(hEventSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemEventLinuxWait(hEventSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemEventLinuxWait(hEventSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    /** @todo we have 1ns parameter resolution, but need to verify that this is what
     *        the kernel actually will use when setting the timer.  Most likely
     *        it's rounded a little, but hopefully not to a multiple of HZ. */
    return 1;
}


RTDECL(void) RTSemEventSetSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->iMagic == RTSEMEVENT_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedResetOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF(hEventSem, hThread);
#endif
}


RTDECL(void) RTSemEventAddSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->iMagic == RTSEMEVENT_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedAddOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF(hEventSem, hThread);
#endif
}


RTDECL(void) RTSemEventRemoveSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->iMagic == RTSEMEVENT_MAGIC);

    RTLockValidatorRecSharedRemoveOwner(&pThis->Signallers, hThread);
#else
    RT_NOREF(hEventSem, hThread);
#endif
}

#endif /* glibc < 2.6 || IPRT_WITH_FUTEX_BASED_SEMS */

