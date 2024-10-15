/* $Id: semeventmulti-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT -  Multiple Release Event Semaphores, Ring-0 Driver, NT.
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
#ifdef IN_RING0
# include "../r0drv/nt/the-nt-kernel.h"
#else
# include <iprt/nt/nt.h>
#endif
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#include <iprt/timer.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * NT event semaphore.
 */
typedef struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value (RTSEMEVENTMULTI_MAGIC). */
    uint32_t volatile   u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
#ifdef IN_RING0
    /** The NT event object. */
    KEVENT              Event;
#elif defined(IN_RING3)
    /** Handle to the NT event object. */
    HANDLE              hEvent;
#endif
#if defined(RTSEMEVENT_STRICT) && defined(IN_RING3)
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
} RTSEMEVENTMULTIINTERNAL, *PRTSEMEVENTMULTIINTERNAL;


RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);
    RT_NOREF2(hClass, pszNameFmt);

    AssertCompile(sizeof(RTSEMEVENTMULTIINTERNAL) > sizeof(void *));
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic = RTSEMEVENTMULTI_MAGIC;
        pThis->cRefs    = 1;
#ifdef IN_RING0
        KeInitializeEvent(&pThis->Event, NotificationEvent, FALSE /* not signalled */);
#else
        NTSTATUS rcNt = NtCreateEvent(&pThis->hEvent, EVENT_ALL_ACCESS, NULL /*pObjAttr*/,
                                      NotificationEvent, FALSE /*not signalled*/);
        if (NT_SUCCESS(rcNt))
#endif
        {
#if defined(RTSEMEVENT_STRICT) && defined(IN_RING3)
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
            RT_NOREF_PV(hClass); RT_NOREF_PV(pszNameFmt);
#endif

            *phEventMultiSem = pThis;
            return VINF_SUCCESS;
        }
#ifdef IN_RING3
        RTMemFree(pThis);
        return RTErrConvertFromNtStatus(rcNt);
#endif
    }
    return VERR_NO_MEMORY;
}


/**
 * Retains a reference to the semaphore.
 *
 * @param   pThis       The semaphore to retain.
 */
DECLINLINE(void) rtR0SemEventMultiNtRetain(PRTSEMEVENTMULTIINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < 100000); NOREF(cRefs);
}


/**
 * Releases a reference to the semaphore.
 *
 * @param   pThis       The semaphore to release
 */
DECLINLINE(void) rtR0SemEventMultiNtRelease(PRTSEMEVENTMULTIINTERNAL pThis)
{
    if (ASMAtomicDecU32(&pThis->cRefs) == 0)
    {
#ifdef IN_RING3
        NTSTATUS rcNt = NtClose(pThis->hEvent);
        AssertMsg(NT_SUCCESS(rcNt), ("%#x\n", rcNt)); RT_NOREF(rcNt);
        pThis->hEvent = NULL;
#endif
#if defined(RTSEMEVENT_STRICT) && defined(IN_RING3)
        RTLockValidatorRecSharedDelete(&pThis->Signallers);
#endif
        RTMemFree(pThis);
    }
}


RTDECL(int) RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (pThis == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);

    /*
     * Invalidate it and signal the object just in case.
     */
    ASMAtomicIncU32(&pThis->u32Magic);
#ifdef IN_RING0
    KeSetEvent(&pThis->Event, 0xfff, FALSE);
#else
    NtSetEvent(pThis->hEvent, NULL);
#endif

    rtR0SemEventMultiNtRelease(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiNtRetain(pThis);

#if defined(RTSEMEVENT_STRICT) && defined(IN_RING3)
    if (pThis->fEverHadSignallers)
    {
        int rc9 = RTLockValidatorRecSharedCheckSignaller(&pThis->Signallers, NIL_RTTHREAD);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Signal the event object.
     */
#ifdef IN_RING0
    KeSetEvent(&pThis->Event, 1, FALSE);
#else
    NTSTATUS rcNt = NtSetEvent(pThis->hEvent, NULL);
#endif

    rtR0SemEventMultiNtRelease(pThis);
#ifdef IN_RING3
    AssertMsgReturn(NT_SUCCESS(rcNt), ("Signaling hEventMultiSem %p failed: %#x\n", pThis, rcNt), RTErrConvertFromNtStatus(rcNt));
#endif
    return VINF_SUCCESS;
}


RTDECL(int) RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiNtRetain(pThis);

    /*
     * Reset the event object.
     */
#ifdef IN_RING0
    KeResetEvent(&pThis->Event);
#else
    NTSTATUS rcNt = NtResetEvent(pThis->hEvent, NULL);
#endif

    rtR0SemEventMultiNtRelease(pThis);
#ifdef IN_RING3
    AssertMsgReturn(NT_SUCCESS(rcNt), ("Resetting hEventMultiSem %p failed: %#x\n", pThis, rcNt), RTErrConvertFromNtStatus(rcNt));
#endif
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
DECLINLINE(int) rtR0SemEventMultiNtWait(PRTSEMEVENTMULTIINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                                        PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);
    RT_NOREF1(pSrcPos);

    rtR0SemEventMultiNtRetain(pThis);

    /*
     * Lock validation needs to be done only when not polling.
     */
#if defined(RTSEMEVENT_STRICT) && defined(IN_RING3)
    RTTHREAD const hThreadSelf = RTThreadSelfAutoAdopt();
    if (   pThis->fEverHadSignallers
        && (   uTimeout != 0
            || (fFlags & (RTSEMWAIT_FLAGS_INDEFINITE | RTSEMWAIT_FLAGS_ABSOLUTE))) )
    {
        int rc9 = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, NULL /*pSrcPos*/, false,
                                                        fFlags & RTSEMWAIT_FLAGS_INDEFINITE
                                                        ? RT_INDEFINITE_WAIT : RT_MS_30SEC /*whatever*/,
                                                        RTTHREADSTATE_EVENT_MULTI, true);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#elif defined(IN_RING3)
    RTTHREAD const hThreadSelf = RTThreadSelf();
#endif

    /*
     * Convert the timeout to a relative one because KeWaitForSingleObject
     * takes system time instead of interrupt time as input for absolute
     * timeout specifications.  So, we're best of by giving it relative time.
     *
     * Lazy bird converts uTimeout to relative nanoseconds and then to Nt time.
     */
#ifdef IN_RING3
    uint64_t nsStartNow = 0;
#endif
    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
        if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
            uTimeout = uTimeout < UINT64_MAX / RT_NS_1MS
                     ? uTimeout * RT_NS_1MS
                     : UINT64_MAX;
        if (uTimeout == UINT64_MAX)
            fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
        else
        {
#ifdef IN_RING3
            if (fFlags & (RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_ABSOLUTE))
                nsStartNow = RTTimeSystemNanoTS();
#endif
            if (fFlags & RTSEMWAIT_FLAGS_ABSOLUTE)
            {
#ifdef IN_RING0
                uint64_t const nsStartNow = RTTimeSystemNanoTS();
#endif
                uTimeout = nsStartNow < uTimeout
                         ? uTimeout - nsStartNow
                         : 0;
            }
        }
    }

    /*
     * Wait for it.
     * We're assuming interruptible waits should happen at UserMode level.
     */
    int rc;
#ifdef IN_RING3
    for (;;)
#endif
    {
#ifdef IN_RING0
        BOOLEAN         fInterruptible = !!(fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE);
        KPROCESSOR_MODE WaitMode       = fInterruptible ? UserMode : KernelMode;
#endif
        NTSTATUS        rcNt;
#ifdef IN_RING3
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT_MULTI, true);
#endif
        if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
#ifdef IN_RING0
            rcNt = KeWaitForSingleObject(&pThis->Event, Executive, WaitMode, fInterruptible, NULL);
#else
            rcNt = NtWaitForSingleObject(pThis->hEvent, TRUE /*Alertable*/, NULL);
#endif
        else
        {
            LARGE_INTEGER Timeout;
            Timeout.QuadPart = -(int64_t)(uTimeout / 100);
#ifdef IN_RING0
            rcNt = KeWaitForSingleObject(&pThis->Event, Executive, WaitMode, fInterruptible, &Timeout);
#else
            rcNt = NtWaitForSingleObject(pThis->hEvent, TRUE /*Alertable*/, &Timeout);
#endif
        }
#ifdef IN_RING3
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT_MULTI);
#endif
        if (pThis->u32Magic == RTSEMEVENTMULTI_MAGIC)
        {
            switch (rcNt)
            {
                case STATUS_SUCCESS:
                    rc = VINF_SUCCESS;
                    break;

                case STATUS_TIMEOUT:
                    Assert(!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE));
                    rc = VERR_TIMEOUT;
                    break;

                case STATUS_USER_APC:
                case STATUS_ALERTED:
                    rc = VERR_INTERRUPTED;
#ifdef IN_RING3
                    /* Loop if when automatically resuming on interruption, adjusting the timeout. */
                    if (fFlags & RTSEMWAIT_FLAGS_RESUME)
                    {
                        if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE) && uTimeout > 0)
                        {
                            uint64_t const nsNewNow   = RTTimeSystemNanoTS();
                            uint64_t const cNsElapsed = nsNewNow - nsStartNow;
                            if (cNsElapsed < uTimeout)
                                uTimeout -= cNsElapsed;
                            else
                                uTimeout = 0;
                            nsStartNow = nsNewNow;
                        }
                        continue;
                    }
#endif
                    break;

#ifdef IN_RING3
                case STATUS_ABANDONED_WAIT_0:
                    rc = VERR_SEM_OWNER_DIED;
                    break;
#endif
                default:
                    AssertMsgFailed(("pThis->u32Magic=%RX32 pThis=%p: wait returned %x!\n", pThis->u32Magic, pThis, rcNt));
                    rc = VERR_INTERNAL_ERROR_4;
                    break;
            }
        }
        else
            rc = VERR_SEM_DESTROYED;
#ifdef IN_RING3
        break;
#endif
    }

    rtR0SemEventMultiNtRelease(pThis);
    return rc;
}


RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventMultiNtWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventMultiNtWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventMultiNtWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}


#ifdef IN_RING0
RTR0DECL(bool) RTSemEventMultiIsSignalSafe(void)
{
    return KeGetCurrentIrql() <= DISPATCH_LEVEL;
}
#endif

#ifdef IN_RING3

RTDECL(void) RTSemEventMultiSetSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
# ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedResetOwner(&pThis->Signallers, hThread, NULL);
# else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
# endif
}


RTDECL(void) RTSemEventMultiAddSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
# ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedAddOwner(&pThis->Signallers, hThread, NULL);
# else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
# endif
}


RTDECL(void) RTSemEventMultiRemoveSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
# ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    RTLockValidatorRecSharedRemoveOwner(&pThis->Signallers, hThread);
# else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
# endif
}

#endif /* IN_RING3 */
