/* $Id: timer-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Timer.
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
#define LOG_GROUP RTLOGGROUP_TIMER
#define _WIN32_WINNT 0x0500
#include <iprt/win/windows.h>

#include <iprt/timer.h>
#ifdef USE_CATCH_UP
# include <iprt/time.h>
#endif
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/err.h>
#include "internal/magics.h"
#include "internal-r3-win.h"


/** Define the flag for creating a manual reset timer if not available in the SDK we are compiling with. */
#ifndef CREATE_WAITABLE_TIMER_MANUAL_RESET
# define CREATE_WAITABLE_TIMER_MANUAL_RESET    0x00000001
#endif
/** Define the flag for high resolution timers, available since Windows 10 RS4 if not available. */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
# define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif


RT_C_DECLS_BEGIN
/* from sysinternals. */
NTSYSAPI LONG NTAPI NtSetTimerResolution(IN ULONG DesiredResolution, IN BOOLEAN SetResolution, OUT PULONG CurrentResolution);
NTSYSAPI LONG NTAPI NtQueryTimerResolution(OUT PULONG MaximumResolution, OUT PULONG MinimumResolution, OUT PULONG CurrentResolution);
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal representation of a timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Flag indicating the timer is suspended. */
    bool    volatile        fSuspended;
    /** Flag indicating that the timer has been destroyed. */
    bool    volatile        fDestroyed;
    /** User argument. */
    void                   *pvUser;
    /** Callback. */
    PFNRTTIMER              pfnTimer;
    /** The current tick. */
    uint64_t                iTick;
    /** The timer interval. 0 if one-shot. */
    uint64_t                u64NanoInterval;
    /** The first shot interval. 0 if ASAP. */
    uint64_t volatile       u64NanoFirst;
    /** Time handle. */
    HANDLE                  hTimer;
    /** USE_CATCH_UP: ns time of the next tick.
     * !USE_CATCH_UP: -uMilliesInterval * 10000 */
    LARGE_INTEGER           llNext;
    /** The thread handle of the timer thread. */
    RTTHREAD                Thread;
    /** Event semaphore on which the thread is blocked. */
    RTSEMEVENT              Event;
    /** The error/status of the timer.
     * Initially -1, set to 0 when the timer have been successfully started, and
     * to errno on failure in starting the timer. */
    volatile int            iError;
} RTTIMER;



/**
 * Timer thread.
 */
static DECLCALLBACK(int) rttimerCallback(RTTHREAD hThreadSelf, void *pvArg)
{
    PRTTIMER pTimer = (PRTTIMER)(void *)pvArg;
    Assert(pTimer->u32Magic == RTTIMER_MAGIC);

    /*
     * Bounce our priority up quite a bit.
     */
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
    {
        int rc = GetLastError();
        AssertMsgFailed(("Failed to set priority class lasterror %d.\n", rc));
        pTimer->iError = RTErrConvertFromWin32(rc);
        RTThreadUserSignal(hThreadSelf);
        return rc;
    }

    /*
     * The work loop.
     */
    RTThreadUserSignal(hThreadSelf);

    while (     !pTimer->fDestroyed
           &&   pTimer->u32Magic == RTTIMER_MAGIC)
    {
        /*
         * Wait for a start or destroy event.
         */
        if (pTimer->fSuspended)
        {
            int rc = RTSemEventWait(pTimer->Event, RT_INDEFINITE_WAIT);
            if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED)
            {
                AssertRC(rc);
                if (pTimer->fDestroyed)
                    continue;
                RTThreadSleep(1000); /* Don't cause trouble! */
            }
            if (    pTimer->fSuspended
                ||  pTimer->fDestroyed)
                continue;
        }

        /*
         * Start the waitable timer.
         */
        pTimer->llNext.QuadPart = -(int64_t)pTimer->u64NanoInterval / 100;
        LARGE_INTEGER ll;
        if (pTimer->u64NanoFirst)
        {
            GetSystemTimeAsFileTime((LPFILETIME)&ll);
            ll.QuadPart += pTimer->u64NanoFirst / 100;
            pTimer->u64NanoFirst = 0;
        }
        else
            ll.QuadPart = -(int64_t)pTimer->u64NanoInterval / 100;
        if (!SetWaitableTimer(pTimer->hTimer, &ll, 0, NULL, NULL, FALSE))
        {
            ASMAtomicXchgBool(&pTimer->fSuspended, true);
            int rc = GetLastError();
            AssertMsgFailed(("Failed to set timer, lasterr %d.\n", rc));
            pTimer->iError = RTErrConvertFromWin32(rc);
            RTThreadUserSignal(hThreadSelf);
            continue; /* back to suspended mode. */
        }
        pTimer->iError = 0;
        RTThreadUserSignal(hThreadSelf);

        /*
         * Timer Service Loop.
         */
        do
        {
            int rc = WaitForSingleObjectEx(pTimer->hTimer, INFINITE, FALSE);
            if (pTimer->u32Magic != RTTIMER_MAGIC)
                break;
            if (rc == WAIT_OBJECT_0)
            {
                /*
                 * Callback the handler.
                 */
                pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pTimer->iTick);

                /*
                 * Rearm the timer handler.
                 */
                ll = pTimer->llNext;
                BOOL fRc = SetWaitableTimer(pTimer->hTimer, &ll, 0, NULL, NULL, FALSE);
                AssertMsg(fRc || pTimer->u32Magic != RTTIMER_MAGIC, ("last error %d\n", GetLastError())); NOREF(fRc);
            }
            else
            {
                /*
                 * We failed during wait, so just signal the destructor and exit.
                 */
                int rc2 = GetLastError();
                RTThreadUserSignal(hThreadSelf);
                AssertMsgFailed(("Wait on hTimer failed, rc=%d lasterr=%d\n", rc, rc2)); NOREF(rc2);
                return -1;
            }
        } while (RT_LIKELY(   !pTimer->fSuspended
                           && !pTimer->fDestroyed
                           &&  pTimer->u32Magic == RTTIMER_MAGIC));

        /*
         * Disable the timer.
         */
        int rc = CancelWaitableTimer (pTimer->hTimer); RT_NOREF(rc);
        AssertMsg(rc, ("CancelWaitableTimer lasterr=%d\n", GetLastError()));

        /*
         * ACK any pending suspend request.
         */
        if (!pTimer->fDestroyed)
        {
            pTimer->iError = 0;
            RTThreadUserSignal(hThreadSelf);
        }
    }

    /*
     * Exit.
     */
    pTimer->iError = 0;
    RTThreadUserSignal(hThreadSelf);
    return VINF_SUCCESS;
}


/**
 * Tries to set the NT timer resolution to a value matching the given timer interval.
 *
 * @returns IPRT status code.
 * @param   u64NanoInterval             The timer interval in nano seconds.
 */
static int rtTimerNtSetTimerResolution(uint64_t u64NanoInterval)
{
    /*
     * On windows we'll have to set the timer resolution before
     * we start the timer.
     */
    ULONG ulMax = UINT32_MAX;
    ULONG ulMin = UINT32_MAX;
    ULONG ulCur = UINT32_MAX;
    ULONG ulReq = (ULONG)(u64NanoInterval / 100);
    NtQueryTimerResolution(&ulMax, &ulMin, &ulCur);
    Log(("NtQueryTimerResolution -> ulMax=%lu00ns ulMin=%lu00ns ulCur=%lu00ns\n", ulMax, ulMin, ulCur));
    if (ulCur > ulMin && ulCur > ulReq)
    {
        ulReq = RT_MIN(ulMin, ulReq);
        if (NtSetTimerResolution(ulReq, TRUE, &ulCur) >= 0)
            Log(("Changed timer resolution to %lu*100ns.\n", ulReq));
        else if (NtSetTimerResolution(10000, TRUE, &ulCur) >= 0)
            Log(("Changed timer resolution to 1ms.\n"));
        else if (NtSetTimerResolution(20000, TRUE, &ulCur) >= 0)
            Log(("Changed timer resolution to 2ms.\n"));
        else if (NtSetTimerResolution(40000, TRUE, &ulCur) >= 0)
            Log(("Changed timer resolution to 4ms.\n"));
        else if (ulMin <= 50000 && NtSetTimerResolution(ulMin, TRUE, &ulCur) >= 0)
            Log(("Changed timer resolution to %lu *100ns.\n", ulMin));
        else
        {
            AssertMsgFailed(("Failed to configure timer resolution!\n"));
            return VERR_INTERNAL_ERROR;
        }
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    /*
     * We don't support the fancy MP features.
     */
    if (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        return VERR_NOT_SUPPORTED;

    /*
     * Create new timer.
     */
    int rc = VERR_IPE_UNINITIALIZED_STATUS;
    PRTTIMER pTimer = (PRTTIMER)RTMemAlloc(sizeof(*pTimer));
    if (pTimer)
    {
        pTimer->u32Magic        = RTTIMER_MAGIC;
        pTimer->fSuspended      = true;
        pTimer->fDestroyed      = false;
        pTimer->Thread          = NIL_RTTHREAD;
        pTimer->pfnTimer        = pfnTimer;
        pTimer->pvUser          = pvUser;
        pTimer->u64NanoInterval = u64NanoInterval;

        rc = RTSemEventCreate(&pTimer->Event);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create Win32 waitable timer.
             * We will first try the undocumented CREATE_WAITABLE_TIMER_HIGH_RESOLUTION which
             * exists since some Windows 10 version (RS4). If this fails we resort to the old
             * method of setting the timer resolution before creating a timer which will probably
             * not give us the accuracy for intervals below the system tick resolution.
             */
            pTimer->iError = 0;
            if (g_pfnCreateWaitableTimerExW)
                pTimer->hTimer = g_pfnCreateWaitableTimerExW(NULL, NULL,
                                                             CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                             TIMER_ALL_ACCESS);
            if (!pTimer->hTimer)
            {
                rc = rtTimerNtSetTimerResolution(u64NanoInterval);
                if (RT_SUCCESS(rc))
                    pTimer->hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
            }

            if (pTimer->hTimer)
            {
                /*
                 * Kick off the timer thread.
                 */
                rc = RTThreadCreate(&pTimer->Thread, rttimerCallback, pTimer, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "Timer");
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Wait for the timer to successfully create the timer
                     * If we don't get a response in 10 secs, then we assume we're screwed.
                     */
                    rc = RTThreadUserWait(pTimer->Thread, 10000);
                    if (RT_SUCCESS(rc))
                    {
                        rc = pTimer->iError;
                        if (RT_SUCCESS(rc))
                        {
                            *ppTimer = pTimer;
                            return VINF_SUCCESS;
                        }
                    }

                    /* bail out */
                    ASMAtomicXchgBool(&pTimer->fDestroyed, true);
                    ASMAtomicXchgU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);
                    RTThreadWait(pTimer->Thread, 45*1000, NULL);
                    CancelWaitableTimer(pTimer->hTimer);
                }
                CloseHandle(pTimer->hTimer);
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());
            RTSemEventDestroy(pTimer->Event);
            pTimer->Event = NIL_RTSEMEVENT;
        }

        RTMemFree(pTimer);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


RTR3DECL(int)     RTTimerDestroy(PRTTIMER pTimer)
{
    /* NULL is ok. */
    if (!pTimer)
        return VINF_SUCCESS;

    int rc = VINF_SUCCESS;
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pTimer->Thread != RTThreadSelf(), VERR_INTERNAL_ERROR);

    /*
     * Signal that we want the thread to exit.
     */
    ASMAtomicWriteBool(&pTimer->fDestroyed, true);
    ASMAtomicWriteU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);

    /*
     * Suspend the timer if it's running.
     */
    if (!pTimer->fSuspended)
    {
        LARGE_INTEGER ll = {0};
        ll.LowPart = 100;
        rc = SetWaitableTimer(pTimer->hTimer, &ll, 0, NULL, NULL, FALSE);
        AssertMsg(rc, ("CancelWaitableTimer lasterr=%d\n", GetLastError()));
    }

    rc = RTSemEventSignal(pTimer->Event);
    AssertRC(rc);

    /*
     * Wait for the thread to exit.
     * And if it don't wanna exit, we'll get kill it.
     */
    rc = RTThreadWait(pTimer->Thread, 30 * 1000, NULL);
    if (RT_FAILURE(rc))
        TerminateThread((HANDLE)RTThreadGetNative(pTimer->Thread), UINT32_MAX);

    /*
     * Free resource.
     */
    rc = CloseHandle(pTimer->hTimer);
    AssertMsg(rc, ("CloseHandle lasterr=%d\n", GetLastError()));

    RTSemEventDestroy(pTimer->Event);
    pTimer->Event = NIL_RTSEMEVENT;

    RTMemFree(pTimer);
    return rc;
}


RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pTimer, VERR_INVALID_POINTER);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pTimer->Thread != RTThreadSelf(), VERR_INTERNAL_ERROR);

    RTThreadUserReset(pTimer->Thread);

    /*
     * Already running?
     */
    if (!ASMAtomicXchgBool(&pTimer->fSuspended, false))
        return VERR_TIMER_ACTIVE;
    LogFlow(("RTTimerStart: pTimer=%p u64First=%llu u64NanoInterval=%llu\n", pTimer, u64First, pTimer->u64NanoInterval));

    /*
     * Tell the thread to start servicing the timer.
     * Wait for it to ACK the request to avoid reset races.
     */
    ASMAtomicUoWriteU64(&pTimer->u64NanoFirst, u64First);
    ASMAtomicUoWriteU64(&pTimer->iTick, 0);
    int rc = RTSemEventSignal(pTimer->Event);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadUserWait(pTimer->Thread, 45*1000);
        AssertRC(rc);
        RTThreadUserReset(pTimer->Thread);
    }
    else
        AssertRC(rc);

    if (RT_FAILURE(rc))
        ASMAtomicXchgBool(&pTimer->fSuspended, true);
    return rc;
}


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pTimer, VERR_INVALID_POINTER);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_MAGIC);

    RTThreadUserReset(pTimer->Thread);

    /*
     * Already running?
     */
    if (ASMAtomicXchgBool(&pTimer->fSuspended, true))
        return VERR_TIMER_SUSPENDED;
    LogFlow(("RTTimerStop: pTimer=%p\n", pTimer));

    /*
     * Tell the thread to stop servicing the timer.
     */
    int rc = VINF_SUCCESS;
    if (RTThreadSelf() != pTimer->Thread)
    {
        LARGE_INTEGER ll = {0};
        ll.LowPart = 100;
        rc = SetWaitableTimer(pTimer->hTimer, &ll, 0, NULL, NULL, FALSE);
        AssertMsg(rc, ("SetWaitableTimer lasterr=%d\n", GetLastError()));
        rc = RTThreadUserWait(pTimer->Thread, 45*1000);
        AssertRC(rc);
        RTThreadUserReset(pTimer->Thread);
    }

    return rc;
}


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    AssertPtrReturn(pTimer, VERR_INVALID_POINTER);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_MAGIC);
    NOREF(u64NanoInterval);
    return VERR_NOT_SUPPORTED;
}
