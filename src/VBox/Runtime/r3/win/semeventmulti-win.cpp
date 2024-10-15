/* $Id: semeventmulti-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiple Release Event Semaphore, Windows.
 *
 * @remarks This file is identical to semevent-win.cpp except for the 2nd
 *          CreateEvent parameter, the reset function and the "Multi" infix.
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
#define LOG_GROUP RTLOGGROUP_SEMAPHORE
#include <iprt/win/windows.h>

#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include "internal/magics.h"
#include "internal/strict.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value (RTSEMEVENTMULTI_MAGIC). */
    uint32_t            u32Magic;
    /** The event handle. */
    HANDLE              hev;
#ifdef RTSEMEVENT_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
};



RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    struct RTSEMEVENTMULTIINTERNAL *pThis = (struct RTSEMEVENTMULTIINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    /*
     * Create the semaphore.
     * (Manual reset, not signaled, private event object.)
     */
    pThis->hev = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (pThis->hev != NULL) /* not INVALID_HANDLE_VALUE */
    {
        pThis->u32Magic = RTSEMEVENTMULTI_MAGIC;
#ifdef RTSEMEVENT_STRICT
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

    DWORD dwErr = GetLastError();
    RTMemFree(pThis);
    return RTErrConvertFromWin32(dwErr);
}


RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    if (pThis == NIL_RTSEMEVENT)        /* don't bitch */
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Invalidate the handle and close the semaphore.
     */
    int rc = VINF_SUCCESS;
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTSEMEVENTMULTI_MAGIC, RTSEMEVENTMULTI_MAGIC), VERR_INVALID_HANDLE);
    if (CloseHandle(pThis->hev))
    {
#ifdef RTSEMEVENT_STRICT
        RTLockValidatorRecSharedDelete(&pThis->Signallers);
#endif
        RTMemFree(pThis);
    }
    else
    {
        DWORD dwErr = GetLastError();
        rc = RTErrConvertFromWin32(dwErr);
        AssertMsgFailed(("Destroy hEventMultiSem %p failed, lasterr=%u (%Rrc)\n", pThis, dwErr, rc));
        /* Leak it. */
    }

    return rc;
}


RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSEMEVENT_STRICT
    if (pThis->fEverHadSignallers)
    {
        int rc9 = RTLockValidatorRecSharedCheckSignaller(&pThis->Signallers, NIL_RTTHREAD);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Signal the object.
     */
    if (SetEvent(pThis->hev))
        return VINF_SUCCESS;
    DWORD dwErr = GetLastError();
    AssertMsgFailed(("Signaling hEventMultiSem %p failed, lasterr=%d\n", pThis, dwErr));
    return RTErrConvertFromWin32(dwErr);
}


RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Reset the object.
     */
    if (ResetEvent(pThis->hev))
        return VINF_SUCCESS;
    DWORD dwErr = GetLastError();
    AssertMsgFailed(("Resetting hEventMultiSem %p failed, lasterr=%d\n", pThis, dwErr));
    return RTErrConvertFromWin32(dwErr);
}


/** Goto avoidance. */
DECL_FORCE_INLINE(int)
rtSemEventWaitHandleStatus(struct RTSEMEVENTMULTIINTERNAL *pThis, uint32_t fFlags, DWORD rc)
{
    switch (rc)
    {
        case WAIT_OBJECT_0:         return VINF_SUCCESS;
        case WAIT_TIMEOUT:          return VERR_TIMEOUT;
        case WAIT_IO_COMPLETION:    return fFlags & RTSEMWAIT_FLAGS_RESUME ? VERR_TIMEOUT : VERR_INTERRUPTED;
        case WAIT_ABANDONED:        return VERR_SEM_OWNER_DIED;
        default:
            AssertMsgFailed(("%u\n", rc));
        case WAIT_FAILED:
        {
            int rc2 = RTErrConvertFromWin32(GetLastError());
            AssertMsgFailed(("Wait on hEventMultiSem %p failed, rc=%d lasterr=%d\n", pThis, rc, GetLastError()));
            if (rc2)
                return rc2;

            AssertMsgFailed(("WaitForSingleObject(event) -> rc=%d while converted lasterr=%d\n", rc, rc2));
            RT_NOREF_PV(pThis);
            return VERR_INTERNAL_ERROR;
        }
    }
}


DECLINLINE(int) rtSemEventMultiWinWait(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                       PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

    /*
     * Convert the timeout to a millisecond count.
     */
    uint64_t uAbsDeadline;
    DWORD    dwMsTimeout;
    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
    {
        dwMsTimeout  = INFINITE;
        uAbsDeadline = UINT64_MAX;
    }
    else
    {
        if (fFlags & RTSEMWAIT_FLAGS_NANOSECS)
            uTimeout = uTimeout < UINT64_MAX - UINT32_C(1000000) / 2
                     ? (uTimeout + UINT32_C(1000000) / 2) / UINT32_C(1000000)
                     : UINT64_MAX / UINT32_C(1000000);
        if (fFlags & RTSEMWAIT_FLAGS_ABSOLUTE)
        {
            uAbsDeadline = uTimeout;
            uint64_t u64Now = RTTimeSystemMilliTS();
            if (u64Now < uTimeout)
                uTimeout -= u64Now;
            else
                uTimeout = 0;
        }
        else if (fFlags & RTSEMWAIT_FLAGS_RESUME)
            uAbsDeadline = RTTimeSystemMilliTS() + uTimeout;
        else
            uAbsDeadline = UINT64_MAX;

        dwMsTimeout = uTimeout < UINT32_MAX
                    ? (DWORD)uTimeout
                    : INFINITE;
    }

    /*
     * Do the wait.
     */
    DWORD rc;
#ifdef RTSEMEVENT_STRICT
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
    if (pThis->fEverHadSignallers)
    {
        do
            rc = WaitForSingleObjectEx(pThis->hev, 0 /*Timeout*/, TRUE /*fAlertable*/);
        while (rc == WAIT_IO_COMPLETION && (fFlags & RTSEMWAIT_FLAGS_RESUME));
        if (rc != WAIT_TIMEOUT || dwMsTimeout == 0)
            return rtSemEventWaitHandleStatus(pThis, fFlags, rc);
        int rc9 = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                        dwMsTimeout, RTTHREADSTATE_EVENT_MULTI, true);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
    RT_NOREF_PV(pSrcPos);
#endif
    RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT_MULTI, true);
    rc = WaitForSingleObjectEx(pThis->hev, dwMsTimeout, TRUE /*fAlertable*/);
    if (rc == WAIT_IO_COMPLETION && (fFlags & RTSEMWAIT_FLAGS_RESUME))
    {
        while (   rc == WAIT_IO_COMPLETION
               && RTTimeSystemMilliTS() < uAbsDeadline)
            rc = WaitForSingleObjectEx(pThis->hev, dwMsTimeout, TRUE /*fAlertable*/);

    }
    RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT_MULTI);
    return rtSemEventWaitHandleStatus(pThis, fFlags, rc);
}



#undef RTSemEventMultiWaitEx
RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtSemEventMultiWinWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemEventMultiWinWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemEventMultiWinWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}



RTDECL(void) RTSemEventMultiSetSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedResetOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventMultiAddSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedAddOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventMultiRemoveSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC);

    RTLockValidatorRecSharedRemoveOwner(&pThis->Signallers, hThread);
#else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
#endif
}

