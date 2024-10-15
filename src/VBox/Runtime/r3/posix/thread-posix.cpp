/* $Id: thread-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads, POSIX.
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
#define LOG_GROUP RTLOGGROUP_THREAD
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#if defined(RT_OS_LINUX)
# include <unistd.h>
# include <sys/syscall.h>
#endif
#if defined(RT_OS_SOLARIS)
# include <sched.h>
# include <sys/resource.h>
#endif
#if defined(RT_OS_DARWIN)
# include <mach/thread_act.h>
# include <mach/thread_info.h>
# include <mach/host_info.h>
# include <mach/mach_init.h>
# include <mach/mach_host.h>
#endif
#if defined(RT_OS_DARWIN) /*|| defined(RT_OS_FREEBSD) - later */ \
 || (defined(RT_OS_LINUX) && !defined(IN_RT_STATIC) /* static + dlsym = trouble */) \
 || defined(IPRT_MAY_HAVE_PTHREAD_SET_NAME_NP)
# define IPRT_MAY_HAVE_PTHREAD_SET_NAME_NP
# include <dlfcn.h>
#endif
#if defined(RT_OS_HAIKU)
# include <OS.h>
#endif
#if defined(RT_OS_DARWIN)
# define sigprocmask pthread_sigmask /* On xnu sigprocmask works on the process, not the calling thread as elsewhere. */
#endif

#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/list.h>
#include <iprt/once.h>
#include <iprt/critsect.h>
#include <iprt/req.h>
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*#ifndef IN_GUEST - shouldn't need to exclude this now with the non-obtrusive init option. */
/** Includes RTThreadPoke. */
# define RTTHREAD_POSIX_WITH_POKE
/*#endif*/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The pthread key in which we store the pointer to our own PRTTHREAD structure.
 * @note There is a defined NIL value here, nor can we really assume this is an
 *       integer.  However, zero is a valid key on Linux, so we get into trouble
 *       if we accidentally use it uninitialized.
 *
 *       So, we ASSUME it's a integer value and the valid range is in approx 0
 *       to PTHREAD_KEYS_MAX.  Solaris has at least one negative value (-1)
 *       defined.  Thus, we go for 16 MAX values below zero and keep our fingers
 *       cross that it will always be an invalid key value everywhere...
 *
 *       See also NIL_RTTLS, which is -1.
 */
static pthread_key_t    g_SelfKey = (pthread_key_t)(-PTHREAD_KEYS_MAX * 16);
#ifdef RTTHREAD_POSIX_WITH_POKE
/** The signal we use for poking threads.
 * This is set to -1 if no available signal was found. */
static int volatile     g_iSigPokeThread = -1;
#endif

#ifdef IPRT_MAY_HAVE_PTHREAD_SET_NAME_NP
# if defined(RT_OS_DARWIN)
/**
 * The Mac OS X (10.6 and later) variant of pthread_setname_np.
 *
 * @returns errno.h
 * @param   pszName         The new thread name.
 */
typedef int (*PFNPTHREADSETNAME)(const char *pszName);
# else
/**
 * The variant of pthread_setname_np most other unix-like systems implement.
 *
 * @returns errno.h
 * @param   hThread         The thread.
 * @param   pszName         The new thread name.
 */
typedef int (*PFNPTHREADSETNAME)(pthread_t hThread, const char *pszName);
# endif

/** Pointer to pthread_setname_np if found. */
static PFNPTHREADSETNAME g_pfnThreadSetName = NULL;
#endif /* IPRT_MAY_HAVE_PTHREAD_SET_NAME_NP */

#ifdef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY
/** Atomic indicator of whether the priority proxy thread has been (attempted) started.
 *
 * The priority proxy thread is started under these circumstances:
 *      - RTThreadCreate
 *      - RTThreadSetType
 *      - RTProcSetPriority
 *
 * Which means that we'll be single threaded when this is modified.
 *
 * Speical values:
 *      - VERR_TRY_AGAIN:           Not yet started.
 *      - VERR_WRONG_ORDER:         Starting.
 *      - VINF_SUCCESS:             Started successfully.
 *      - VERR_PROCESS_NOT_FOUND:   Stopping or stopped
 *      - Other error status if failed to start.
 *
 * @note We could potentially optimize this by only start it when we lower the
 *       priority of ourselves, the process, or a newly created thread.  But
 *       that would means we would need to take multi-threading into account, so
 *       let's not do that for now.
 */
static int32_t volatile g_rcPriorityProxyThreadStart            = VERR_TRY_AGAIN;
/** The IPRT thread handle for the priority proxy. */
static RTTHREAD         g_hRTThreadPosixPriorityProxyThread     = NIL_RTTHREAD;
/** The priority proxy queue. */
static RTREQQUEUE       g_hRTThreadPosixPriorityProxyQueue      = NIL_RTREQQUEUE;
#endif /* RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void *rtThreadNativeMain(void *pvArgs);
static void rtThreadKeyDestruct(void *pvValue);
#ifdef RTTHREAD_POSIX_WITH_POKE
static void rtThreadPosixPokeSignal(int iSignal);
#endif


#ifdef RTTHREAD_POSIX_WITH_POKE
/**
 * Try register the dummy signal handler for RTThreadPoke.
 */
static void rtThreadPosixSelectPokeSignal(void)
{
    /*
     * Note! Avoid SIGRTMIN thru SIGRTMIN+2 because of LinuxThreads.
     */
# if !defined(RT_OS_LINUX) && !defined(RT_OS_SOLARIS) /* glibc defines SIGRTMAX to __libc_current_sigrtmax() and Solaris libc defines it relying on _sysconf(), causing compiler to deploy serialization here. */
    static
# endif
    const int s_aiSigCandidates[] =
    {
# ifdef SIGRTMAX
        SIGRTMAX-3,
        SIGRTMAX-2,
        SIGRTMAX-1,
# endif
# ifndef RT_OS_SOLARIS
        SIGUSR2,
# endif
        SIGWINCH
    };

    g_iSigPokeThread = -1;
    if (!RTR3InitIsUnobtrusive())
    {
        for (unsigned iSig = 0; iSig < RT_ELEMENTS(s_aiSigCandidates); iSig++)
        {
            struct sigaction SigActOld;
            if (!sigaction(s_aiSigCandidates[iSig], NULL, &SigActOld))
            {
                if (   SigActOld.sa_handler == SIG_DFL
                    || SigActOld.sa_handler == rtThreadPosixPokeSignal)
                {
                    struct sigaction SigAct;
                    RT_ZERO(SigAct);
                    SigAct.sa_handler = rtThreadPosixPokeSignal;
                    SigAct.sa_flags   = 0;      /* no SA_RESTART! */
                    sigfillset(&SigAct.sa_mask);

                    /* ASSUMES no sigaction race... (lazy bird) */
                    if (!sigaction(s_aiSigCandidates[iSig], &SigAct, NULL))
                    {
                        g_iSigPokeThread = s_aiSigCandidates[iSig];
                        break;
                    }
                    AssertMsgFailed(("rc=%Rrc errno=%d\n", RTErrConvertFromErrno(errno), errno));
                }
            }
            else
                AssertMsgFailed(("rc=%Rrc errno=%d\n", RTErrConvertFromErrno(errno), errno));
        }
    }
}
#endif /* RTTHREAD_POSIX_WITH_POKE */


DECLHIDDEN(int) rtThreadNativeInit(void)
{
    /*
     * Allocate the TLS (key in posix terms) where we store the pointer to
     * a threads RTTHREADINT structure.
     */
    int rc = pthread_key_create(&g_SelfKey, rtThreadKeyDestruct);
    if (rc)
        return VERR_NO_TLS_FOR_SELF;

#ifdef RTTHREAD_POSIX_WITH_POKE
    rtThreadPosixSelectPokeSignal();
#endif

#ifdef IPRT_MAY_HAVE_PTHREAD_SET_NAME_NP
    if (RT_SUCCESS(rc))
        g_pfnThreadSetName = (PFNPTHREADSETNAME)(uintptr_t)dlsym(RTLD_DEFAULT, "pthread_setname_np");
#endif
    return rc;
}

static void rtThreadPosixBlockSignals(PRTTHREADINT pThread)
{
    /*
     * Mask all signals, including the poke one, if requested.
     */
    if (   pThread
        && (pThread->fFlags & RTTHREADFLAGS_NO_SIGNALS))
    {
        sigset_t SigSet;
        sigfillset(&SigSet);
        sigdelset(&SigSet, SIGILL);  /* On the m1 we end up spinning on UDF ... */
        sigdelset(&SigSet, SIGTRAP); /* ... and BRK instruction if these signals are masked. */
        sigdelset(&SigSet, SIGFPE);  /* Just adding the rest here to be on the safe side. */
        sigdelset(&SigSet, SIGBUS);
        sigdelset(&SigSet, SIGSEGV);
        int rc = sigprocmask(SIG_BLOCK, &SigSet, NULL);
        AssertMsg(rc == 0, ("rc=%Rrc errno=%d\n", RTErrConvertFromErrno(errno), errno)); RT_NOREF(rc);
    }
    /*
     * Block SIGALRM - required for timer-posix.cpp.
     * This is done to limit harm done by OSes which doesn't do special SIGALRM scheduling.
     * It will not help much if someone creates threads directly using pthread_create. :/
     */
    else if (!RTR3InitIsUnobtrusive())
    {
        sigset_t SigSet;
        sigemptyset(&SigSet);
        sigaddset(&SigSet, SIGALRM);
        sigprocmask(SIG_BLOCK, &SigSet, NULL);
    }

#ifdef RTTHREAD_POSIX_WITH_POKE
    /*
     * bird 2020-10-28: Not entirely sure why we do this, but it makes sure the signal works
     *                  on the new thread.  Probably some pre-NPTL linux reasons.
     */
    if (g_iSigPokeThread != -1)
    {
# if 1 /* siginterrupt() is typically implemented as two sigaction calls, this should be faster and w/o deprecations: */
        struct sigaction SigActOld;
        RT_ZERO(SigActOld);

        struct sigaction SigAct;
        RT_ZERO(SigAct);
        SigAct.sa_handler = rtThreadPosixPokeSignal;
        SigAct.sa_flags   = 0;          /* no SA_RESTART! */
        sigfillset(&SigAct.sa_mask);

        int rc = sigaction(g_iSigPokeThread, &SigAct, &SigActOld);
        AssertMsg(rc == 0, ("rc=%Rrc errno=%d\n", RTErrConvertFromErrno(errno), errno)); RT_NOREF(rc);
        AssertMsg(rc || SigActOld.sa_handler == rtThreadPosixPokeSignal, ("%p\n", SigActOld.sa_handler));
# else
        siginterrupt(g_iSigPokeThread, 1);
# endif
    }
#endif
}

DECLHIDDEN(void) rtThreadNativeReInitObtrusive(void)
{
#ifdef RTTHREAD_POSIX_WITH_POKE
    Assert(!RTR3InitIsUnobtrusive());
    rtThreadPosixSelectPokeSignal();
#endif
    rtThreadPosixBlockSignals(NULL);
}


/**
 * Destructor called when a thread terminates.
 * @param   pvValue     The key value. PRTTHREAD in our case.
 */
static void rtThreadKeyDestruct(void *pvValue)
{
    /*
     * Deal with alien threads.
     */
    PRTTHREADINT pThread = (PRTTHREADINT)pvValue;
    if (pThread->fIntFlags & RTTHREADINT_FLAGS_ALIEN)
    {
        pthread_setspecific(g_SelfKey, pThread);
        rtThreadTerminate(pThread, 0);
        pthread_setspecific(g_SelfKey, NULL);
    }
}


#ifdef RTTHREAD_POSIX_WITH_POKE
/**
 * Dummy signal handler for the poke signal.
 *
 * @param   iSignal     The signal number.
 */
static void rtThreadPosixPokeSignal(int iSignal)
{
    Assert(iSignal == g_iSigPokeThread);
    NOREF(iSignal);
}
#endif


/**
 * Adopts a thread, this is called immediately after allocating the
 * thread structure.
 *
 * @param   pThread     Pointer to the thread structure.
 */
DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    rtThreadPosixBlockSignals(pThread);

    int rc = pthread_setspecific(g_SelfKey, pThread);
    if (!rc)
        return VINF_SUCCESS;
    return VERR_FAILED_TO_SET_SELF_TLS;
}


DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    if (pThread == (PRTTHREADINT)pthread_getspecific(g_SelfKey))
        pthread_setspecific(g_SelfKey, NULL);
}


/**
 * Wrapper which unpacks the params and calls thread function.
 */
static void *rtThreadNativeMain(void *pvArgs)
{
    PRTTHREADINT  pThread = (PRTTHREADINT)pvArgs;
    pthread_t     Self    = pthread_self();
#if !defined(RT_OS_SOLARIS) /* On Solaris sizeof(pthread_t) = 4 and sizeof(NIL_RTNATIVETHREAD) = 8 */
    Assert((uintptr_t)Self != NIL_RTNATIVETHREAD);
#endif
    Assert(Self == (pthread_t)(RTNATIVETHREAD)Self);

#if defined(RT_OS_LINUX)
    /*
     * Set the TID.
     */
    pThread->tid = syscall(__NR_gettid);
    ASMMemoryFence();
#endif

    rtThreadPosixBlockSignals(pThread);

    /*
     * Set the TLS entry and, if possible, the thread name.
     */
    int rc = pthread_setspecific(g_SelfKey, pThread);
    AssertReleaseMsg(!rc, ("failed to set self TLS. rc=%d thread '%s'\n", rc, pThread->szName));

#ifdef IPRT_MAY_HAVE_PTHREAD_SET_NAME_NP
    if (g_pfnThreadSetName)
# ifdef RT_OS_DARWIN
        g_pfnThreadSetName(pThread->szName);
# else
        g_pfnThreadSetName(Self, pThread->szName);
# endif
#endif

    /*
     * Call common main.
     */
    rc = rtThreadMain(pThread, (uintptr_t)Self, &pThread->szName[0]);

    pthread_setspecific(g_SelfKey, NULL);
    pthread_exit((void *)(intptr_t)rc);
    return (void *)(intptr_t)rc;
}

#ifdef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY

/**
 * @callback_method_impl{FNRTTHREAD,
 *  Priority proxy thread that services g_hRTThreadPosixPriorityProxyQueue.}
 */
static DECLCALLBACK(int) rtThreadPosixPriorityProxyThread(PRTTHREADINT, void *)
{
    for (;;)
    {
        RTREQQUEUE hReqQueue = g_hRTThreadPosixPriorityProxyQueue;
        if (hReqQueue != NIL_RTREQQUEUE)
            RTReqQueueProcess(hReqQueue, RT_INDEFINITE_WAIT);
        else
            break;

        int32_t rc = ASMAtomicUoReadS32(&g_rcPriorityProxyThreadStart);
        if (rc != VINF_SUCCESS && rc != VERR_WRONG_ORDER)
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Just returns a non-success status codes to force the thread to re-evaluate
 * the global shutdown variable.
 */
static DECLCALLBACK(int) rtThreadPosixPriorityProxyStopper(void)
{
    return VERR_CANCELLED;
}


/**
 * An atexit() callback that stops the proxy creation/priority thread.
 */
static void rtThreadStopProxyThread(void)
{
    /*
     * Signal to the thread that it's time to shut down.
     */
    int32_t rc = ASMAtomicXchgS32(&g_rcPriorityProxyThreadStart, VERR_PROCESS_NOT_FOUND);
    if (RT_SUCCESS(rc))
    {
        /*
         * Grab the associated handles.
         */
        RTTHREAD   hThread = g_hRTThreadPosixPriorityProxyThread;
        RTREQQUEUE hQueue  = g_hRTThreadPosixPriorityProxyQueue;
        g_hRTThreadPosixPriorityProxyQueue  = NIL_RTREQQUEUE;
        g_hRTThreadPosixPriorityProxyThread = NIL_RTTHREAD;
        ASMCompilerBarrier(); /* paranoia */

        AssertReturnVoid(hThread != NIL_RTTHREAD);
        AssertReturnVoid(hQueue != NIL_RTREQQUEUE);

        /*
         * Kick the thread so it gets out of any pending RTReqQueueProcess call ASAP.
         */
        rc = RTReqQueueCallEx(hQueue, NULL, 0 /*cMillies*/, RTREQFLAGS_IPRT_STATUS | RTREQFLAGS_NO_WAIT,
                              (PFNRT)rtThreadPosixPriorityProxyStopper, 0);

        /*
         * Wait for the thread to complete.
         */
        rc = RTThreadWait(hThread, RT_SUCCESS(rc) ? RT_MS_1SEC * 5 : 32, NULL);
        if (RT_SUCCESS(rc))
            RTReqQueueDestroy(hQueue);
        /* else: just leak the stuff, we're exitting, so nobody cares... */
    }
}


/**
 * Ensure that the proxy priority proxy thread has been started.
 *
 * Since we will always start a proxy thread when asked to create a thread,
 * there is no need for serialization here.
 *
 * @retval  true if started
 * @retval  false if it failed to start (caller must handle this scenario).
 */
DECLHIDDEN(bool) rtThreadPosixPriorityProxyStart(void)
{
    /*
     * Read the result.
     */
    int rc = ASMAtomicUoReadS32(&g_rcPriorityProxyThreadStart);
    if (rc != VERR_TRY_AGAIN)
        return RT_SUCCESS(rc);

    /* If this triggers then there is a very unexpected race somewhere.  It
       should be harmless though. */
    AssertReturn(ASMAtomicCmpXchgS32(&g_rcPriorityProxyThreadStart, VERR_WRONG_ORDER, VERR_TRY_AGAIN), false);

    /*
     * Not yet started, so do that.
     */
    rc = RTReqQueueCreate(&g_hRTThreadPosixPriorityProxyQueue);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&g_hRTThreadPosixPriorityProxyThread, rtThreadPosixPriorityProxyThread, NULL, 0 /*cbStack*/,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "RTThrdPP");
        if (RT_SUCCESS(rc))
        {
            ASMAtomicWriteS32(&g_rcPriorityProxyThreadStart, VINF_SUCCESS);

            atexit(rtThreadStopProxyThread);
            return true;
        }
        RTReqQueueCreate(&g_hRTThreadPosixPriorityProxyQueue);
    }
    ASMAtomicWriteS32(&g_rcPriorityProxyThreadStart, rc != VERR_WRONG_ORDER ? rc : VERR_PROCESS_NOT_FOUND);
    return false;
}


/**
 * Calls @a pfnFunction from the priority proxy thread.
 *
 * Caller must have called rtThreadPosixStartProxy() to check that the priority
 * proxy thread is running.
 *
 * @returns
 * @param   pTargetThread   The target thread, NULL if not applicable.  This is
 *                          so we can skip calls pertaining to the priority
 *                          proxy thread itself.
 * @param   pfnFunction     The function to call.  Must return IPRT status code.
 * @param   cArgs           Number of arguments (see also RTReqQueueCall).
 * @param   ...             Arguments (see also RTReqQueueCall).
 */
DECLHIDDEN(int) rtThreadPosixPriorityProxyCall(PRTTHREADINT pTargetThread, PFNRT pfnFunction, int cArgs, ...)
{
    int rc;
    if (   !pTargetThread
        || pTargetThread->pfnThread != rtThreadPosixPriorityProxyThread)
    {
        va_list va;
        va_start(va, cArgs);
        PRTREQ pReq;
        rc = RTReqQueueCallV(g_hRTThreadPosixPriorityProxyQueue, &pReq, RT_INDEFINITE_WAIT, RTREQFLAGS_IPRT_STATUS,
                             pfnFunction, cArgs, va);
        va_end(va);
        RTReqRelease(pReq);
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}

#endif /* !RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY */

/**
 * Worker for rtThreadNativeCreate that's either called on the priority proxy
 * thread or directly on the calling thread depending on the proxy state.
 */
static DECLCALLBACK(int) rtThreadNativeInternalCreate(PRTTHREADINT pThread, PRTNATIVETHREAD pNativeThread)
{
    /*
     * Set the default stack size.
     */
    if (!pThread->cbStack)
        pThread->cbStack = 512*1024;

#ifdef RT_OS_LINUX
    pThread->tid = -1;
#endif

    /*
     * Setup thread attributes.
     */
    pthread_attr_t  ThreadAttr;
    int rc = pthread_attr_init(&ThreadAttr);
    if (!rc)
    {
        rc = pthread_attr_setdetachstate(&ThreadAttr, PTHREAD_CREATE_DETACHED);
        if (!rc)
        {
            rc = pthread_attr_setstacksize(&ThreadAttr, pThread->cbStack);
            if (!rc)
            {
                /*
                 * Create the thread.
                 */
                pthread_t ThreadId;
                rc = pthread_create(&ThreadId, &ThreadAttr, rtThreadNativeMain, pThread);
                if (!rc)
                {
                    pthread_attr_destroy(&ThreadAttr);
                    *pNativeThread = (uintptr_t)ThreadId;
                    return VINF_SUCCESS;
                }
            }
        }
        pthread_attr_destroy(&ThreadAttr);
    }
    return RTErrConvertFromErrno(rc);
}


DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThread, PRTNATIVETHREAD pNativeThread)
{
#ifdef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY
    /*
     * If we have a priority proxy thread, use it.  Make sure to ignore the
     * staring of the proxy thread itself.
     */
    if (   pThread->pfnThread != rtThreadPosixPriorityProxyThread
        && rtThreadPosixPriorityProxyStart())
    {
        PRTREQ pReq;
        int rc = RTReqQueueCall(g_hRTThreadPosixPriorityProxyQueue, &pReq, RT_INDEFINITE_WAIT,
                                (PFNRT)rtThreadNativeInternalCreate, 2, pThread, pNativeThread);
        if (RT_SUCCESS(rc))
            rc = RTReqGetStatus(pReq);
        RTReqRelease(pReq);
        return rc;
    }

    /*
     * Fall back on creating it directly without regard to priority proxying.
     */
#endif
    return rtThreadNativeInternalCreate(pThread, pNativeThread);
}


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    /** @todo import alien threads? */
#if defined(RT_OS_DARWIN)
    /* On darwin, there seems to be input checking with pthread_getspecific.
       So, we must prevent using g_SelfKey before rtThreadNativeInit has run,
       otherwise we might crash or starting working with total garbage pointer
       values here (see _os_tsd_get_direct in znu/libsyscall/os/tsd.h).

       Now, since the init value is a "negative" one, we just have to check
       that it's positive or zero before calling the API. */
    if (RT_LIKELY((intptr_t)g_SelfKey >= 0))
        return (PRTTHREADINT)pthread_getspecific(g_SelfKey);
    return NIL_RTTHREAD;
#else
    return (PRTTHREADINT)pthread_getspecific(g_SelfKey);
#endif
}


#ifdef RTTHREAD_POSIX_WITH_POKE

RTDECL(int) RTThreadPoke(RTTHREAD hThread)
{
    AssertReturn(hThread != RTThreadSelf(), VERR_INVALID_PARAMETER);
    PRTTHREADINT pThread = rtThreadGet(hThread);
    AssertReturn(pThread, VERR_INVALID_HANDLE);

    int rc;
    if (g_iSigPokeThread != -1)
    {
        rc = pthread_kill((pthread_t)(uintptr_t)pThread->Core.Key, g_iSigPokeThread);
        rc = RTErrConvertFromErrno(rc);
    }
    else
        rc = VERR_NOT_SUPPORTED;

    rtThreadRelease(pThread);
    return rc;
}


RTDECL(int) RTThreadControlPokeSignal(RTTHREAD hThread, bool fEnable)
{
    AssertReturn(hThread == RTThreadSelf() && hThread != NIL_RTTHREAD, VERR_INVALID_PARAMETER);
    int rc;
    if (g_iSigPokeThread != -1)
    {
        sigset_t SigSet;
        sigemptyset(&SigSet);
        sigaddset(&SigSet, g_iSigPokeThread);

        int rc2 = sigprocmask(fEnable ? SIG_UNBLOCK : SIG_BLOCK, &SigSet, NULL);
        if (rc2 == 0)
            rc = VINF_SUCCESS;
        else
        {
            rc = RTErrConvertFromErrno(errno);
            AssertMsgFailed(("rc=%Rrc errno=%d (rc2=%d)\n", rc, errno, rc2));
        }
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}


#endif

/** @todo move this into platform specific files. */
RTR3DECL(int) RTThreadGetExecutionTimeMilli(uint64_t *pcMsKernelTime, uint64_t *pcMsUserTime)
{
#if defined(RT_OS_SOLARIS)
    struct rusage ts;
    int const rc = getrusage(RUSAGE_LWP, &ts);
    AssertReturn(rc == 0, RTErrConvertFromErrno(rc));

    *pcMsKernelTime = ts.ru_stime.tv_sec * 1000 + ts.ru_stime.tv_usec / 1000;
    *pcMsUserTime   = ts.ru_utime.tv_sec * 1000 + ts.ru_utime.tv_usec / 1000;
    return VINF_SUCCESS;

#elif defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
    /* on Linux, getrusage(RUSAGE_THREAD, ...) is available since 2.6.26. maybe it's slower? */
    struct timespec ts;
    int const rc = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    AssertReturn(rc == 0, RTErrConvertFromErrno(rc));

    *pcMsKernelTime = 0;
    *pcMsUserTime   = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return VINF_SUCCESS;

#elif defined(RT_OS_DARWIN)
    thread_basic_info       ThreadInfo;
    mach_msg_type_number_t  Count = THREAD_BASIC_INFO_COUNT;
    kern_return_t krc = thread_info(mach_thread_self(), THREAD_BASIC_INFO, (thread_info_t)&ThreadInfo, &Count);
    AssertReturn(krc == KERN_SUCCESS, RTErrConvertFromDarwinKern(krc));

    *pcMsKernelTime = ThreadInfo.system_time.seconds * 1000 + ThreadInfo.system_time.microseconds / 1000;
    *pcMsUserTime   = ThreadInfo.user_time.seconds   * 1000 + ThreadInfo.user_time.microseconds   / 1000;
    return VINF_SUCCESS;

#elif defined(RT_OS_HAIKU)
    thread_info       ThreadInfo;
    status_t status = get_thread_info(find_thread(NULL), &ThreadInfo);
    AssertReturn(status == B_OK, RTErrConvertFromErrno(status));

    *pcMsKernelTime = ThreadInfo.kernel_time / 1000;
    *pcMsUserTime   = ThreadInfo.user_time / 1000;
    return VINF_SUCCESS;

#else
    RT_NOREF(pcMsKernelTime, pcMsUserTime);
    return VERR_NOT_IMPLEMENTED;
#endif
}

