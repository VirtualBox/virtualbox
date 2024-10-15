/* $Id: thread-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads, OS/2.
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
#define INCL_BASE
#include <os2.h>
#undef RT_MAX

#include <errno.h>
#include <process.h>
#include <stdlib.h>
#include <signal.h>
#include <InnoTekLIBC/FastInfoBlocks.h>
#include <InnoTekLIBC/thread.h>

#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/cpuset.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to thread local memory which points to the current thread. */
static PRTTHREADINT *g_ppCurThread;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtThreadNativeMain(void *pvArgs);


DECLHIDDEN(int) rtThreadNativeInit(void)
{
    /*
     * Allocate thread local memory.
     */
    PULONG pul;
    int rc = DosAllocThreadLocalMemory(1, &pul);
    if (rc)
        return VERR_NO_TLS_FOR_SELF;
    g_ppCurThread = (PRTTHREADINT *)(void *)pul;
    return VINF_SUCCESS;
}


static void rtThreadOs2BlockSigAlarm(void)
{
    /*
     * Block SIGALRM - required for timer-posix.cpp.
     * This is done to limit harm done by OSes which doesn't do special SIGALRM scheduling.
     * It will not help much if someone creates threads directly using pthread_create. :/
     */
    sigset_t SigSet;
    sigemptyset(&SigSet);
    sigaddset(&SigSet, SIGALRM);
    sigprocmask(SIG_BLOCK, &SigSet, NULL);
}


DECLHIDDEN(void) rtThreadNativeReInitObtrusive(void)
{
    rtThreadOs2BlockSigAlarm();
}


DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread)
{

    *g_ppCurThread = pThread;
    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    if (pThread == *g_ppCurThread)
        *g_ppCurThread = NULL;
}


/**
 * Wrapper which unpacks the params and calls thread function.
 */
static void rtThreadNativeMain(void *pvArgs)
{
    rtThreadOs2BlockSigAlarm();

    /*
     * Call common main.
     */
    PRTTHREADINT  pThread = (PRTTHREADINT)pvArgs;
    *g_ppCurThread = pThread;

#ifdef fibGetTidPid
    rtThreadMain(pThread, fibGetTidPid(), &pThread->szName[0]);
#else
    rtThreadMain(pThread, _gettid(), &pThread->szName[0]);
#endif

    *g_ppCurThread = NULL;
    _endthread();
}


DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThread, PRTNATIVETHREAD pNativeThread)
{
    /*
     * Default stack size.
     */
    if (!pThread->cbStack)
        pThread->cbStack = 512*1024;

    /*
     * Create the thread.
     */
    int iThreadId = _beginthread(rtThreadNativeMain, NULL, pThread->cbStack, pThread);
    if (iThreadId > 0)
    {
#ifdef fibGetTidPid
        *pNativeThread = iThreadId | (fibGetPid() << 16);
#else
        *pNativeThread = iThreadId;
#endif
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    PRTTHREADINT pThread = *g_ppCurThread;
    if (pThread)
        return (RTTHREAD)pThread;
    /** @todo import alien threads? */
    return NULL;
}


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
#ifdef fibGetTidPid
    return fibGetTidPid();
#else
    return _gettid();
#endif
}


RTDECL(int)   RTThreadSleep(RTMSINTERVAL cMillies)
{
    LogFlow(("RTThreadSleep: cMillies=%d\n", cMillies));
    DosSleep(cMillies);
    LogFlow(("RTThreadSleep: returning (cMillies=%d)\n", cMillies));
    return VINF_SUCCESS;
}


RTDECL(int)   RTThreadSleepNoLog(RTMSINTERVAL cMillies)
{
    DosSleep(cMillies);
    return VINF_SUCCESS;
}


RTDECL(bool) RTThreadYield(void)
{
    uint64_t u64TS = ASMReadTSC();
    DosSleep(0);
    u64TS = ASMReadTSC() - u64TS;
    bool fRc = u64TS > 1750;
    LogFlow(("RTThreadYield: returning %d (%llu ticks)\n", fRc, u64TS));
    return fRc;
}


RTR3DECL(int) RTThreadGetAffinity(PRTCPUSET pCpuSet)
{
    union
    {
        uint64_t u64;
        MPAFFINITY mpaff;
    } u;

    APIRET rc = DosQueryThreadAffinity(AFNTY_THREAD, &u.mpaff);
    if (!rc)
    {
        RTCpuSetFromU64(pCpuSet, u.u64);
        return VINF_SUCCESS;
    }
    return RTErrConvertFromOS2(rc);
}


RTR3DECL(int) RTThreadSetAffinity(PCRTCPUSET pCpuSet)
{
    union
    {
        uint64_t u64;
        MPAFFINITY mpaff;
    } u;
    u.u64 = pCpuSet ? RTCpuSetToU64(pCpuSet) : UINT64_MAX;
    int rc = DosSetThreadAffinity(&u.mpaff);
    if (!rc)
        return VINF_SUCCESS;
    return RTErrConvertFromOS2(rc);
}


RTR3DECL(RTTLS) RTTlsAlloc(void)
{
    AssertCompile(NIL_RTTLS == -1);
    return __libc_TLSAlloc();
}


RTR3DECL(int) RTTlsAllocEx(PRTTLS piTls, PFNRTTLSDTOR pfnDestructor)
{
    int rc;
    int iTls = __libc_TLSAlloc();
    if (iTls != -1)
    {
        if (    !pfnDestructor
            ||  __libc_TLSDestructor(iTls, (void (*)(void *, int, unsigned))pfnDestructor, 0) != -1)
        {
            *piTls = iTls;
            return VINF_SUCCESS;
        }

        rc = RTErrConvertFromErrno(errno);
        __libc_TLSFree(iTls);
    }
    else
        rc = RTErrConvertFromErrno(errno);

    *piTls = NIL_RTTLS;
    return rc;
}


RTR3DECL(int) RTTlsFree(RTTLS iTls)
{
    if (iTls == NIL_RTTLS)
        return VINF_SUCCESS;
    if (__libc_TLSFree(iTls) != -1)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(void *) RTTlsGet(RTTLS iTls)
{
    return __libc_TLSGet(iTls);
}


RTR3DECL(int) RTTlsGetEx(RTTLS iTls, void **ppvValue)
{
    int rc = VINF_SUCCESS;
    void *pv = __libc_TLSGet(iTls);
    if (RT_UNLIKELY(!pv))
    {
        errno = 0;
        pv = __libc_TLSGet(iTls);
        if (!pv && errno)
            rc = RTErrConvertFromErrno(errno);
    }

    *ppvValue = pv;
    return rc;
}


RTR3DECL(int) RTTlsSet(RTTLS iTls, void *pvValue)
{
    if (__libc_TLSSet(iTls, pvValue) != -1)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTThreadGetExecutionTimeMilli(uint64_t *pcMsKernelTime, uint64_t *pcMsUserTime)
{
    RT_NOREF(pcMsKernelTime, pcMsUserTime);
    return VERR_NOT_IMPLEMENTED;
}
