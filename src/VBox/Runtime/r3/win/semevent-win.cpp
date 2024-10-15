/* $Id: semevent-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Event Semaphore, Windows.
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
#include "internal/magics.h"
#include "internal/mem.h"
#include "internal/strict.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t            u32Magic;
    /** The event handle. */
    HANDLE              hev;
#ifdef RTSEMEVENT_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
    /** The creation flags. */
    uint32_t            fFlags;
};



RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));

    struct RTSEMEVENTINTERNAL *pThis;
    if (!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
        pThis = (struct RTSEMEVENTINTERNAL *)RTMemAlloc(sizeof(*pThis));
    else
        pThis = (struct RTSEMEVENTINTERNAL *)rtMemBaseAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    /*
     * Create the semaphore.
     * (Auto reset, not signaled, private event object.)
     */
    pThis->hev = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (pThis->hev != NULL) /* not INVALID_HANDLE_VALUE */
    {
        pThis->u32Magic = RTSEMEVENT_MAGIC;
        pThis->fFlags   = fFlags;
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
        RT_NOREF_PV(hClass); RT_NOREF_PV(pszNameFmt);
#endif

        *phEventSem = pThis;
        return VINF_SUCCESS;
    }

    DWORD dwErr = GetLastError();
    if (!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
        RTMemFree(pThis);
    else
        rtMemBaseFree(pThis);
    return RTErrConvertFromWin32(dwErr);
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Invalidate the handle and close the semaphore.
     */
    int rc = VINF_SUCCESS;
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTSEMEVENT_MAGIC, RTSEMEVENT_MAGIC), VERR_INVALID_HANDLE);
    if (CloseHandle(pThis->hev))
    {
#ifdef RTSEMEVENT_STRICT
        RTLockValidatorRecSharedDelete(&pThis->Signallers);
#endif
        if (!(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
            RTMemFree(pThis);
        else
            rtMemBaseFree(pThis);
    }
    else
    {
        DWORD dwErr = GetLastError();
        rc = RTErrConvertFromWin32(dwErr);
        AssertMsgFailed(("Destroy hEventSem %p failed, lasterr=%u (%Rrc)\n", pThis, dwErr, rc));
        /* Leak it. */
    }

    return rc;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, VERR_INVALID_HANDLE);

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
    AssertMsgFailed(("Signaling hEventSem %p failed, lasterr=%d\n", pThis, dwErr));
    return RTErrConvertFromWin32(dwErr);
}


/** Goto avoidance. */
DECL_FORCE_INLINE(int) rtSemEventWaitHandleStatus(struct RTSEMEVENTINTERNAL *pThis, DWORD rc)
{
    switch (rc)
    {
        case WAIT_OBJECT_0:         return VINF_SUCCESS;
        case WAIT_TIMEOUT:          return VERR_TIMEOUT;
        case WAIT_IO_COMPLETION:    return VERR_INTERRUPTED;
        case WAIT_ABANDONED:        return VERR_SEM_OWNER_DIED;
        default:
            AssertMsgFailed(("%u\n", rc));
        case WAIT_FAILED:
        {
            int rc2 = RTErrConvertFromWin32(GetLastError());
            AssertMsgFailed(("Wait on hEventSem %p failed, rc=%d lasterr=%d\n", pThis, rc, GetLastError()));
            if (rc2)
                return rc2;

            AssertMsgFailed(("WaitForSingleObject(event) -> rc=%d while converted lasterr=%d\n", rc, rc2));
            RT_NOREF_PV(pThis);
            return VERR_INTERNAL_ERROR;
        }
    }
}


#undef RTSemEventWaitNoResume
RTDECL(int)   RTSemEventWaitNoResume(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Wait for condition.
     */
#ifdef RTSEMEVENT_STRICT
    RTTHREAD hThreadSelf = !(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)
                         ? RTThreadSelfAutoAdopt()
                         : RTThreadSelf();
    if (pThis->fEverHadSignallers)
    {
        DWORD rc = WaitForSingleObjectEx(pThis->hev,
                                         0 /*Timeout*/,
                                         TRUE /*fAlertable*/);
        if (rc != WAIT_TIMEOUT || cMillies == 0)
            return rtSemEventWaitHandleStatus(pThis, rc);
        int rc9 = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, NULL /*pSrcPos*/, false,
                                                        cMillies, RTTHREADSTATE_EVENT, true);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
#endif
    RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT, true);
    DWORD rc = WaitForSingleObjectEx(pThis->hev,
                                     cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies,
                                     TRUE /*fAlertable*/);
    RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT);
    return rtSemEventWaitHandleStatus(pThis, rc);
}


RTDECL(void) RTSemEventSetSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENT_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedResetOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventAddSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENT_MAGIC);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedAddOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventRemoveSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTSEMEVENT_MAGIC);

    RTLockValidatorRecSharedRemoveOwner(&pThis->Signallers, hThread);
#else
    RT_NOREF_PV(hEventSem); RT_NOREF_PV(hThread);
#endif
}

