/* $Id: timer-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Timer, POSIX.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Enables the use of POSIX RT timers. */
#ifndef RT_OS_SOLARIS /* Solaris 10 doesn't have SIGEV_THREAD */
# define IPRT_WITH_POSIX_TIMERS
#endif /* !RT_OS_SOLARIS */

/** @def RT_TIMER_SIGNAL
 * The signal number that the timers use.
 * We currently use SIGALRM for both setitimer and posix real time timers
 * out of simplicity, but we might want change this later for the posix ones. */
#ifdef IPRT_WITH_POSIX_TIMERS
# define RT_TIMER_SIGNAL    SIGALRM
#else
# define RT_TIMER_SIGNAL    SIGALRM
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   RTLOGGROUP_TIMER
#include <iprt/timer.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/once.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/critsect.h>
#include "internal/magics.h"

#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#ifdef RT_OS_LINUX
# include <linux/rtc.h>
#endif
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#if defined(RT_OS_DARWIN)
# define sigprocmask pthread_sigmask /* On xnu sigprocmask works on the process, not the calling thread as elsewhere. */
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IPRT_WITH_POSIX_TIMERS
/** Init the critsect on first call. */
static RTONCE g_TimerOnce = RTONCE_INITIALIZER;
/** Global critsect that serializes timer creation and destruction.
 * This is lazily created on the first RTTimerCreateEx call and will not be
 * freed up (I'm afraid).  */
static RTCRITSECT g_TimerCritSect;
/**
 * Global counter of RTTimer instances. The signal thread is
 * started when it changes from 0 to 1. The signal thread
 * terminates when it becomes 0 again.
 */
static uint32_t volatile g_cTimerInstances;
/** The signal handling thread. */
static RTTHREAD g_TimerThread;
#endif /* IPRT_WITH_POSIX_TIMERS */


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
    uint8_t volatile        fSuspended;
    /** Flag indicating that the timer has been destroyed. */
    uint8_t volatile        fDestroyed;
#ifndef IPRT_WITH_POSIX_TIMERS /** @todo We have to take the signals on a dedicated timer thread as
                                * we (might) have code assuming that signals doesn't screw around
                                * on existing threads. (It would be sufficient to have one thread
                                * per signal of course since the signal will be masked while it's
                                * running, however, it may just cause more complications than its
                                * worth - sigwait/sigwaitinfo work atomically anyway...)
                                * Also, must block the signal in the thread main procedure too. */
    /** The timer thread. */
    RTTHREAD                Thread;
    /** Event semaphore on which the thread is blocked. */
    RTSEMEVENT              Event;
#endif /* !IPRT_WITH_POSIX_TIMERS */
    /** User argument. */
    void                   *pvUser;
    /** Callback. */
    PFNRTTIMER              pfnTimer;
    /** The timer interval. 0 if one-shot. */
    uint64_t                u64NanoInterval;
#ifndef IPRT_WITH_POSIX_TIMERS
    /** The first shot interval. 0 if ASAP. */
    uint64_t volatile       u64NanoFirst;
#endif /* !IPRT_WITH_POSIX_TIMERS */
    /** The current timer tick. */
    uint64_t volatile       iTick;
#ifndef IPRT_WITH_POSIX_TIMERS
    /** The error/status of the timer.
     * Initially -1, set to 0 when the timer have been successfully started, and
     * to errno on failure in starting the timer. */
    int volatile            iError;
#else /* IPRT_WITH_POSIX_TIMERS */
    timer_t                 NativeTimer;
#endif /* IPRT_WITH_POSIX_TIMERS */

} RTTIMER;



#ifdef IPRT_WITH_POSIX_TIMERS

/**
 * RTOnce callback that initializes the critical section.
 *
 * @returns RTCritSectInit return code.
 * @param   pvUser      NULL, ignored.
 *
 */
static DECLCALLBACK(int) rtTimerOnce(void *pvUser)
{
    NOREF(pvUser);
    return RTCritSectInit(&g_TimerCritSect);
}
#endif


/**
 * Signal handler which ignore everything it gets.
 *
 * @param   iSignal     The signal number.
 */
static void rttimerSignalIgnore(int iSignal)
{
    //AssertBreakpoint();
    NOREF(iSignal);
}


/**
 * RT_TIMER_SIGNAL wait thread.
 */
static DECLCALLBACK(int) rttimerThread(RTTHREAD hThreadSelf, void *pvArg)
{
    NOREF(hThreadSelf); NOREF(pvArg);
#ifndef IPRT_WITH_POSIX_TIMERS
    PRTTIMER pTimer = (PRTTIMER)pvArg;
    RTTIMER Timer = *pTimer;
    Assert(pTimer->u32Magic == RTTIMER_MAGIC);
#endif /* !IPRT_WITH_POSIX_TIMERS */

    /*
     * Install signal handler.
     */
    struct sigaction SigAct;
    memset(&SigAct, 0, sizeof(SigAct));
    SigAct.sa_flags = SA_RESTART;
    sigemptyset(&SigAct.sa_mask);
    SigAct.sa_handler = rttimerSignalIgnore;
    if (sigaction(RT_TIMER_SIGNAL, &SigAct, NULL))
    {
        SigAct.sa_flags &= ~SA_RESTART;
        if (sigaction(RT_TIMER_SIGNAL, &SigAct, NULL))
            AssertMsgFailed(("sigaction failed, errno=%d\n", errno));
    }

    /*
     * Mask most signals except those which might be used by the pthread implementation (linux).
     */
    sigset_t SigSet;
    sigfillset(&SigSet);
    sigdelset(&SigSet, SIGTERM);
    sigdelset(&SigSet, SIGHUP);
    sigdelset(&SigSet, SIGINT);
    sigdelset(&SigSet, SIGABRT);
    sigdelset(&SigSet, SIGKILL);
#ifdef SIGRTMIN
    for (int iSig = SIGRTMIN; iSig < SIGRTMAX; iSig++)
        sigdelset(&SigSet, iSig);
#endif
    if (sigprocmask(SIG_SETMASK, &SigSet, NULL))
    {
#ifdef IPRT_WITH_POSIX_TIMERS
        int rc = RTErrConvertFromErrno(errno);
#else
        int rc = pTimer->iError = RTErrConvertFromErrno(errno);
#endif
        AssertMsgFailed(("sigprocmask -> errno=%d\n", errno));
        return rc;
    }

    /*
     * The work loop.
     */
    RTThreadUserSignal(hThreadSelf);

#ifndef IPRT_WITH_POSIX_TIMERS
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
         * Start the timer.
         *
         * For some SunOS (/SysV?) threading compatibility Linux will only
         * deliver the RT_TIMER_SIGNAL to the thread calling setitimer(). Therefore
         * we have to call it here.
         *
         * It turns out this might not always be the case, see RT_TIMER_SIGNAL killing
         * processes on RH 2.4.21.
         */
        struct itimerval TimerVal;
        if (pTimer->u64NanoFirst)
        {
            uint64_t u64 = RT_MAX(1000, pTimer->u64NanoFirst);
            TimerVal.it_value.tv_sec     = u64 / 1000000000;
            TimerVal.it_value.tv_usec    = (u64 % 1000000000) / 1000;
        }
        else
        {
            TimerVal.it_value.tv_sec     = 0;
            TimerVal.it_value.tv_usec    = 10;
        }
        if (pTimer->u64NanoInterval)
        {
            uint64_t u64 = RT_MAX(1000, pTimer->u64NanoInterval);
            TimerVal.it_interval.tv_sec  = u64 / 1000000000;
            TimerVal.it_interval.tv_usec = (u64 % 1000000000) / 1000;
        }
        else
        {
            TimerVal.it_interval.tv_sec  = 0;
            TimerVal.it_interval.tv_usec = 0;
        }

        if (setitimer(ITIMER_REAL, &TimerVal, NULL))
        {
            ASMAtomicXchgU8(&pTimer->fSuspended, true);
            pTimer->iError = RTErrConvertFromErrno(errno);
            RTThreadUserSignal(hThreadSelf);
            continue; /* back to suspended mode. */
        }
        pTimer->iError = 0;
        RTThreadUserSignal(hThreadSelf);

        /*
         * Timer Service Loop.
         */
        sigemptyset(&SigSet);
        sigaddset(&SigSet, RT_TIMER_SIGNAL);
        do
        {
            siginfo_t SigInfo;
            RT_ZERO(SigInfo);
#ifdef RT_OS_DARWIN
            if (RT_LIKELY(sigwait(&SigSet, &SigInfo.si_signo) >= 0))
            {
#else
            if (RT_LIKELY(sigwaitinfo(&SigSet, &SigInfo) >= 0))
            {
                if (RT_LIKELY(SigInfo.si_signo == RT_TIMER_SIGNAL))
#endif
                {
                    if (RT_UNLIKELY(    pTimer->fSuspended
                                    ||  pTimer->fDestroyed
                                    ||  pTimer->u32Magic != RTTIMER_MAGIC))
                        break;

                    pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pTimer->iTick);

                    /* auto suspend one-shot timers. */
                    if (RT_UNLIKELY(!pTimer->u64NanoInterval))
                    {
                        ASMAtomicWriteU8(&pTimer->fSuspended, true);
                        break;
                    }
                }
            }
            else if (errno != EINTR)
                AssertMsgFailed(("sigwaitinfo -> errno=%d\n", errno));
        } while (RT_LIKELY(   !pTimer->fSuspended
                           && !pTimer->fDestroyed
                           &&  pTimer->u32Magic == RTTIMER_MAGIC));

        /*
         * Disable the timer.
         */
        struct itimerval TimerVal2 = {{0,0}, {0,0}};
        if (setitimer(ITIMER_REAL, &TimerVal2, NULL))
            AssertMsgFailed(("setitimer(ITIMER_REAL,&{0}, NULL) failed, errno=%d\n", errno));

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

#else /* IPRT_WITH_POSIX_TIMERS */

    sigemptyset(&SigSet);
    sigaddset(&SigSet, RT_TIMER_SIGNAL);
    while (g_cTimerInstances)
    {
        siginfo_t SigInfo;
        RT_ZERO(SigInfo);
        if (RT_LIKELY(sigwaitinfo(&SigSet, &SigInfo) >= 0))
        {
            LogFlow(("rttimerThread: signo=%d pTimer=%p\n", SigInfo.si_signo, SigInfo.si_value.sival_ptr));
            if (RT_LIKELY(   SigInfo.si_signo == RT_TIMER_SIGNAL
                          && SigInfo.si_code == SI_TIMER)) /* The SI_TIMER check is *essential* because of the pthread_kill. */
            {
                PRTTIMER pTimer = (PRTTIMER)SigInfo.si_value.sival_ptr;
                AssertPtr(pTimer);
                if (RT_UNLIKELY(    !RT_VALID_PTR(pTimer)
                                ||  ASMAtomicUoReadU8(&pTimer->fSuspended)
                                ||  ASMAtomicUoReadU8(&pTimer->fDestroyed)
                                ||  pTimer->u32Magic != RTTIMER_MAGIC))
                    continue;

                pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pTimer->iTick);

                /* auto suspend one-shot timers. */
                if (RT_UNLIKELY(!pTimer->u64NanoInterval))
                    ASMAtomicWriteU8(&pTimer->fSuspended, true);
            }
        }
    }
#endif /* IPRT_WITH_POSIX_TIMERS */

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
     * We need the signal masks to be set correctly, which they won't be in
     * unobtrusive mode.
     */
    if (RTR3InitIsUnobtrusive())
        return VERR_NOT_SUPPORTED;

#ifndef IPRT_WITH_POSIX_TIMERS
    /*
     * Check if timer is busy.
     */
    struct itimerval TimerVal;
    if (getitimer(ITIMER_REAL, &TimerVal))
    {
        AssertMsgFailed(("getitimer() -> errno=%d\n", errno));
        return VERR_NOT_IMPLEMENTED;
    }
    if (    TimerVal.it_value.tv_usec
        ||  TimerVal.it_value.tv_sec
        ||  TimerVal.it_interval.tv_usec
        ||  TimerVal.it_interval.tv_sec)
    {
        AssertMsgFailed(("A timer is running. System limit is one timer per process!\n"));
        return VERR_TIMER_BUSY;
    }
#endif /* !IPRT_WITH_POSIX_TIMERS */

    /*
     * Block RT_TIMER_SIGNAL from calling thread.
     */
    sigset_t SigSet;
    sigemptyset(&SigSet);
    sigaddset(&SigSet, RT_TIMER_SIGNAL);
    sigprocmask(SIG_BLOCK, &SigSet, NULL);

#ifndef IPRT_WITH_POSIX_TIMERS /** @todo combine more of the setitimer/timer_create code. setitimer could also use the global thread. */
    /** @todo Move this RTC hack else where... */
    static bool fDoneRTC;
    if (!fDoneRTC)
    {
        fDoneRTC = true;
        /* check resolution. */
        TimerVal.it_interval.tv_sec = 0;
        TimerVal.it_interval.tv_usec = 1000;
        TimerVal.it_value = TimerVal.it_interval;
        if (    setitimer(ITIMER_REAL, &TimerVal, NULL)
            ||  getitimer(ITIMER_REAL, &TimerVal)
            ||  TimerVal.it_interval.tv_usec > 1000)
        {
            /*
             * Try open /dev/rtc to set the irq rate to 1024 and
             * turn periodic
             */
            Log(("RTTimerCreate: interval={%ld,%ld} trying to adjust /dev/rtc!\n", TimerVal.it_interval.tv_sec, TimerVal.it_interval.tv_usec));
# ifdef RT_OS_LINUX
            int fh = open("/dev/rtc", O_RDONLY);
            if (fh >= 0)
            {
                if (    ioctl(fh, RTC_IRQP_SET, 1024) < 0
                    ||  ioctl(fh, RTC_PIE_ON, 0) < 0)
                    Log(("RTTimerCreate: couldn't configure rtc! errno=%d\n", errno));
                ioctl(fh, F_SETFL, O_ASYNC);
                ioctl(fh, F_SETOWN, getpid());
                /* not so sure if closing it is a good idea... */
                //close(fh);
            }
            else
                Log(("RTTimerCreate: couldn't configure rtc! open failed with errno=%d\n", errno));
# endif
        }
        /* disable it */
        TimerVal.it_interval.tv_sec = 0;
        TimerVal.it_interval.tv_usec = 0;
        TimerVal.it_value = TimerVal.it_interval;
        setitimer(ITIMER_REAL, &TimerVal, NULL);
    }

    /*
     * Create a new timer.
     */
    int rc;
    PRTTIMER pTimer = (PRTTIMER)RTMemAlloc(sizeof(*pTimer));
    if (pTimer)
    {
        pTimer->u32Magic    = RTTIMER_MAGIC;
        pTimer->fSuspended  = true;
        pTimer->fDestroyed  = false;
        pTimer->Thread      = NIL_RTTHREAD;
        pTimer->Event       = NIL_RTSEMEVENT;
        pTimer->pfnTimer    = pfnTimer;
        pTimer->pvUser      = pvUser;
        pTimer->u64NanoInterval = u64NanoInterval;
        pTimer->u64NanoFirst = 0;
        pTimer->iTick       = 0;
        pTimer->iError      = 0;
        rc = RTSemEventCreate(&pTimer->Event);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadCreate(&pTimer->Thread, rttimerThread, pTimer, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "Timer");
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Wait for the timer thread to initialize it self.
                 * This might take a little while...
                 */
                rc = RTThreadUserWait(pTimer->Thread, 45*1000);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    rc = RTThreadUserReset(pTimer->Thread); AssertRC(rc);
                    rc = pTimer->iError;
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        RTThreadYield(); /* <-- Horrible hack to make tstTimer work. (linux 2.6.12) */
                        *ppTimer = pTimer;
                        return VINF_SUCCESS;
                    }
                }

                /* bail out */
                ASMAtomicXchgU8(&pTimer->fDestroyed, true);
                ASMAtomicXchgU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);
                RTThreadWait(pTimer->Thread, 45*1000, NULL);
            }
            RTSemEventDestroy(pTimer->Event);
            pTimer->Event = NIL_RTSEMEVENT;
        }
        RTMemFree(pTimer);
    }
    else
        rc = VERR_NO_MEMORY;

#else /* IPRT_WITH_POSIX_TIMERS */

    /*
     * Do the global init first.
     */
    int rc = RTOnce(&g_TimerOnce, rtTimerOnce, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Create a new timer structure.
     */
    LogFlow(("RTTimerCreateEx: u64NanoInterval=%llu fFlags=%lu\n", u64NanoInterval, fFlags));
    PRTTIMER pTimer = (PRTTIMER)RTMemAlloc(sizeof(*pTimer));
    if (pTimer)
    {
        /* Initialize timer structure. */
        pTimer->u32Magic        = RTTIMER_MAGIC;
        pTimer->fSuspended      = true;
        pTimer->fDestroyed      = false;
        pTimer->pfnTimer        = pfnTimer;
        pTimer->pvUser          = pvUser;
        pTimer->u64NanoInterval = u64NanoInterval;
        pTimer->iTick           = 0;

        /*
         * Create a timer that deliver RT_TIMER_SIGNAL upon timer expiration.
         */
        struct sigevent SigEvt;
        SigEvt.sigev_notify = SIGEV_SIGNAL;
        SigEvt.sigev_signo  = RT_TIMER_SIGNAL;
        SigEvt.sigev_value.sival_ptr = pTimer; /* sigev_value gets copied to siginfo. */
        int err = timer_create(CLOCK_REALTIME, &SigEvt, &pTimer->NativeTimer);
        if (!err)
        {
            /*
             * Increment the timer count, do this behind the critsect to avoid races.
             */
            RTCritSectEnter(&g_TimerCritSect);

            if (ASMAtomicIncU32(&g_cTimerInstances) != 1)
            {
                Assert(g_cTimerInstances > 1);
                RTCritSectLeave(&g_TimerCritSect);

                LogFlow(("RTTimerCreateEx: rc=%Rrc pTimer=%p (thread already running)\n", rc, pTimer));
                *ppTimer = pTimer;
                return VINF_SUCCESS;
            }

            /*
             * Create the signal handling thread. It will wait for the signal
             * and execute the timer functions.
             */
            rc = RTThreadCreate(&g_TimerThread, rttimerThread, NULL, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "Timer");
            if (RT_SUCCESS(rc))
            {
                rc = RTThreadUserWait(g_TimerThread, 45*1000); /* this better not fail... */
                if (RT_SUCCESS(rc))
                {
                    RTCritSectLeave(&g_TimerCritSect);

                    LogFlow(("RTTimerCreateEx: rc=%Rrc pTimer=%p (thread already running)\n", rc, pTimer));
                    *ppTimer = pTimer;
                    return VINF_SUCCESS;
                }
                /* darn, what do we do here? */
            }

            /* bail out */
            ASMAtomicDecU32(&g_cTimerInstances);
            Assert(!g_cTimerInstances);

            RTCritSectLeave(&g_TimerCritSect);

            timer_delete(pTimer->NativeTimer);
        }
        else
        {
            rc = RTErrConvertFromErrno(err);
            Log(("RTTimerCreateEx: err=%d (%Rrc)\n", err, rc));
        }

        RTMemFree(pTimer);
    }
    else
        rc = VERR_NO_MEMORY;

#endif /* IPRT_WITH_POSIX_TIMERS */
    return rc;
}


RTR3DECL(int) RTTimerDestroy(PRTTIMER pTimer)
{
    LogFlow(("RTTimerDestroy: pTimer=%p\n", pTimer));

    /*
     * Validate input.
     */
    /* NULL is ok. */
    if (!pTimer)
        return VINF_SUCCESS;
    int rc = VINF_SUCCESS;
    AssertPtrReturn(pTimer, VERR_INVALID_POINTER);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_MAGIC);
#ifdef IPRT_WITH_POSIX_TIMERS
    AssertReturn(g_TimerThread != RTThreadSelf(), VERR_INTERNAL_ERROR);
#else
    AssertReturn(pTimer->Thread != RTThreadSelf(), VERR_INTERNAL_ERROR);
#endif

    /*
     * Mark the semaphore as destroyed.
     */
    ASMAtomicWriteU8(&pTimer->fDestroyed, true);
    ASMAtomicWriteU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);

#ifdef IPRT_WITH_POSIX_TIMERS
    /*
     * Suspend the timer if it's running.
     */
    if (!pTimer->fSuspended)
    {
        struct itimerspec TimerSpec;
        TimerSpec.it_value.tv_sec     = 0;
        TimerSpec.it_value.tv_nsec    = 0;
        TimerSpec.it_interval.tv_sec  = 0;
        TimerSpec.it_interval.tv_nsec = 0;
        int err = timer_settime(pTimer->NativeTimer, 0, &TimerSpec, NULL); NOREF(err);
        AssertMsg(!err, ("%d / %d\n", err, errno));
    }
#endif

    /*
     * Poke the thread and wait for it to finish.
     * This is only done for the last timer when using posix timers.
     */
#ifdef IPRT_WITH_POSIX_TIMERS
    RTTHREAD Thread = NIL_RTTHREAD;
    RTCritSectEnter(&g_TimerCritSect);
    if (ASMAtomicDecU32(&g_cTimerInstances) == 0)
    {
        Thread = g_TimerThread;
        g_TimerThread = NIL_RTTHREAD;
    }
    RTCritSectLeave(&g_TimerCritSect);
#else  /* IPRT_WITH_POSIX_TIMERS */
    RTTHREAD Thread = pTimer->Thread;
    rc = RTSemEventSignal(pTimer->Event);
    AssertRC(rc);
#endif /* IPRT_WITH_POSIX_TIMERS */
    if (Thread != NIL_RTTHREAD)
    {
        /* Signal it so it gets out of the sigwait if it's stuck there... */
        pthread_kill((pthread_t)RTThreadGetNative(Thread), RT_TIMER_SIGNAL);

        /*
         * Wait for the thread to complete.
         */
        rc = RTThreadWait(Thread, 30 * 1000, NULL);
        AssertRC(rc);
    }


    /*
     * Free up the resources associated with the timer.
     */
#ifdef IPRT_WITH_POSIX_TIMERS
    timer_delete(pTimer->NativeTimer);
#else
    RTSemEventDestroy(pTimer->Event);
    pTimer->Event = NIL_RTSEMEVENT;
#endif /* !IPRT_WITH_POSIX_TIMERS */
    if (RT_SUCCESS(rc))
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
#ifndef IPRT_WITH_POSIX_TIMERS
    AssertReturn(pTimer->Thread != RTThreadSelf(), VERR_INTERNAL_ERROR);
#endif

    /*
     * Already running?
     */
    if (!ASMAtomicXchgU8(&pTimer->fSuspended, false))
        return VERR_TIMER_ACTIVE;
    LogFlow(("RTTimerStart: pTimer=%p u64First=%llu u64NanoInterval=%llu\n", pTimer, u64First, pTimer->u64NanoInterval));

#ifndef IPRT_WITH_POSIX_TIMERS
    /*
     * Tell the thread to start servicing the timer.
     * Wait for it to ACK the request to avoid reset races.
     */
    RTThreadUserReset(pTimer->Thread);
    ASMAtomicUoWriteU64(&pTimer->u64NanoFirst, u64First);
    ASMAtomicUoWriteU64(&pTimer->iTick, 0);
    ASMAtomicWriteU8(&pTimer->fSuspended, false);
    int rc = RTSemEventSignal(pTimer->Event);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadUserWait(pTimer->Thread, 45*1000);
        AssertRC(rc);
        RTThreadUserReset(pTimer->Thread);
    }
    else
        AssertRC(rc);

#else /* IPRT_WITH_POSIX_TIMERS */
    /*
     * Start the timer.
     */
    struct itimerspec TimerSpec;
    TimerSpec.it_value.tv_sec     = u64First / 1000000000; /* nanosec => sec */
    TimerSpec.it_value.tv_nsec    = u64First ? u64First % 1000000000 : 10; /* 0 means disable, replace it with 10. */
    TimerSpec.it_interval.tv_sec  = pTimer->u64NanoInterval / 1000000000;
    TimerSpec.it_interval.tv_nsec = pTimer->u64NanoInterval % 1000000000;
    int err = timer_settime(pTimer->NativeTimer, 0, &TimerSpec, NULL);
    int rc = err == 0 ? VINF_SUCCESS : RTErrConvertFromErrno(errno);
#endif /* IPRT_WITH_POSIX_TIMERS */

    if (RT_FAILURE(rc))
        ASMAtomicXchgU8(&pTimer->fSuspended, false);
    return rc;
}


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pTimer, VERR_INVALID_POINTER);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_MAGIC);

    /*
     * Already running?
     */
    if (ASMAtomicXchgU8(&pTimer->fSuspended, true))
        return VERR_TIMER_SUSPENDED;
    LogFlow(("RTTimerStop: pTimer=%p\n", pTimer));

#ifndef IPRT_WITH_POSIX_TIMERS
    /*
     * Tell the thread to stop servicing the timer.
     */
    RTThreadUserReset(pTimer->Thread);
    ASMAtomicXchgU8(&pTimer->fSuspended, true);
    int rc = VINF_SUCCESS;
    if (RTThreadSelf() != pTimer->Thread)
    {
        pthread_kill((pthread_t)RTThreadGetNative(pTimer->Thread), RT_TIMER_SIGNAL);
        rc = RTThreadUserWait(pTimer->Thread, 45*1000);
        AssertRC(rc);
        RTThreadUserReset(pTimer->Thread);
    }

#else /* IPRT_WITH_POSIX_TIMERS */
    /*
     * Stop the timer.
     */
    struct itimerspec TimerSpec;
    TimerSpec.it_value.tv_sec     = 0;
    TimerSpec.it_value.tv_nsec    = 0;
    TimerSpec.it_interval.tv_sec  = 0;
    TimerSpec.it_interval.tv_nsec = 0;
    int err = timer_settime(pTimer->NativeTimer, 0, &TimerSpec, NULL);
    int rc = err == 0 ? VINF_SUCCESS : RTErrConvertFromErrno(errno);
#endif /* IPRT_WITH_POSIX_TIMERS */

    return rc;
}


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    AssertPtrReturn(pTimer, VERR_INVALID_POINTER);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_MAGIC);
    NOREF(u64NanoInterval);
    return VERR_NOT_SUPPORTED;
}

