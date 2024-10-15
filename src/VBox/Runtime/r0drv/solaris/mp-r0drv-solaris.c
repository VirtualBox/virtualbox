/* $Id: mp-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, Solaris.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include "the-solaris-kernel.h"
#include "internal/iprt.h"
#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/thread.h>

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/err.h>
#include "r0drv/mp-r0drv.h"

typedef int FNRTMPSOLWORKER(void *pvUser1, void *pvUser2, void *pvUser3);
typedef FNRTMPSOLWORKER *PFNRTMPSOLWORKER;


RTDECL(bool) RTMpIsCpuWorkPending(void)
{
    return false;
}


RTDECL(RTCPUID) RTMpCpuId(void)
{
    return CPU->cpu_id;
}


RTDECL(int) RTMpCurSetIndex(void)
{
    return CPU->cpu_id;
}


RTDECL(int) RTMpCurSetIndexAndId(PRTCPUID pidCpu)
{
    return *pidCpu = CPU->cpu_id;
}


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS && idCpu <= max_cpuid ? idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu <= max_cpuid ? iCpu : NIL_RTCPUID;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return max_cpuid;
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    /*
     * We cannot query CPU status recursively, check cpu member from cached set.
     */
    if (idCpu >= ncpus)
        return false;

    return RTCpuSetIsMember(&g_rtMpSolCpuSet, idCpu);
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return idCpu < ncpus;
}


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    RTCPUID idCpu;

    RTCpuSetEmpty(pSet);
    idCpu = RTMpGetMaxCpuId(); /* it's inclusive */
    do
    {
        if (RTMpIsCpuPossible(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    } while (idCpu-- > 0);

    return pSet;
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    return ncpus;
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    /*
     * We cannot query CPU status recursively, return the cached set.
     */
    *pSet = g_rtMpSolCpuSet;
    return pSet;
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    RTCPUSET Set;
    RTMpGetOnlineSet(&Set);
    return RTCpuSetCount(&Set);
}


/**
 * Wrapper to Solaris IPI infrastructure.
 *
 * @returns Solaris error code.
 * @param   pCpuSet        Pointer to Solaris CPU set.
 * @param   pfnSolWorker   Function to execute on target CPU(s).
 * @param   pArgs          Pointer to RTMPARGS to pass to @a pfnSolWorker.
 */
static void rtMpSolCrossCall(PRTSOLCPUSET pCpuSet, PFNRTMPSOLWORKER pfnSolWorker, PRTMPARGS pArgs)
{
    AssertPtrReturnVoid(pCpuSet);
    AssertPtrReturnVoid(pfnSolWorker);
    AssertPtrReturnVoid(pCpuSet);

    if (g_frtSolOldIPI)
    {
        if (g_frtSolOldIPIUlong)
        {
            g_rtSolXcCall.u.pfnSol_xc_call_old_ulong((xc_arg_t)pArgs,          /* Arg to IPI function */
                                                     0,                        /* Arg2, ignored */
                                                     0,                        /* Arg3, ignored */
                                                     IPRT_SOL_X_CALL_HIPRI,    /* IPI priority */
                                                     pCpuSet->auCpus[0],       /* Target CPU(s) */
                                                     (xc_func_t)pfnSolWorker); /* Function to execute on target(s) */
        }
        else
        {
            g_rtSolXcCall.u.pfnSol_xc_call_old((xc_arg_t)pArgs,          /* Arg to IPI function */
                                               0,                        /* Arg2, ignored */
                                               0,                        /* Arg3, ignored */
                                               IPRT_SOL_X_CALL_HIPRI,    /* IPI priority */
                                               *pCpuSet,                 /* Target CPU set */
                                               (xc_func_t)pfnSolWorker); /* Function to execute on target(s) */
        }
    }
    else
    {
        g_rtSolXcCall.u.pfnSol_xc_call((xc_arg_t)pArgs,          /* Arg to IPI function */
                                       0,                        /* Arg2 */
                                       0,                        /* Arg3 */
                                       &pCpuSet->auCpus[0],      /* Target CPU set */
                                       (xc_func_t)pfnSolWorker); /* Function to execute on target(s) */
    }
}


/**
 * Wrapper between the native solaris per-cpu callback and PFNRTWORKER
 * for the RTMpOnAll API.
 *
 * @returns Solaris error code.
 * @param   uArgs       Pointer to the RTMPARGS package.
 * @param   pvIgnored1  Ignored.
 * @param   pvIgnored2  Ignored.
 */
static int rtMpSolOnAllCpuWrapper(void *uArg, void *pvIgnored1, void *pvIgnored2)
{
    PRTMPARGS pArgs = (PRTMPARGS)(uArg);

    /*
     * Solaris CPU cross calls execute on offline CPUs too. Check our CPU cache
     * set and ignore if it's offline.
     */
    if (!RTMpIsCpuOnline(RTMpCpuId()))
        return 0;

    pArgs->pfnWorker(RTMpCpuId(), pArgs->pvUser1, pArgs->pvUser2);

    NOREF(pvIgnored1);
    NOREF(pvIgnored2);
    return 0;
}


RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RTMPARGS Args;
    RTSOLCPUSET CpuSet;
    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
    RT_ASSERT_INTS_ON();

    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = NIL_RTCPUID;
    Args.cHits = 0;

    for (int i = 0; i < IPRT_SOL_SET_WORDS; i++)
        CpuSet.auCpus[i] = (ulong_t)-1L;

    RTThreadPreemptDisable(&PreemptState);

    rtMpSolCrossCall(&CpuSet, rtMpSolOnAllCpuWrapper, &Args);

    RTThreadPreemptRestore(&PreemptState);

    return VINF_SUCCESS;
}


/**
 * Wrapper between the native solaris per-cpu callback and PFNRTWORKER
 * for the RTMpOnOthers API.
 *
 * @returns Solaris error code.
 * @param   uArgs       Pointer to the RTMPARGS package.
 * @param   pvIgnored1  Ignored.
 * @param   pvIgnored2  Ignored.
 */
static int rtMpSolOnOtherCpusWrapper(void *uArg, void *pvIgnored1, void *pvIgnored2)
{
    PRTMPARGS pArgs = (PRTMPARGS)(uArg);
    RTCPUID idCpu = RTMpCpuId();

    Assert(idCpu != pArgs->idCpu);
    pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);

    NOREF(pvIgnored1);
    NOREF(pvIgnored2);
    return 0;
}


RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RTMPARGS Args;
    RTSOLCPUSET CpuSet;
    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
    RT_ASSERT_INTS_ON();

    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = RTMpCpuId();
    Args.cHits = 0;

    /* The caller is supposed to have disabled preemption, but take no chances. */
    RTThreadPreemptDisable(&PreemptState);

    for (int i = 0; i < IPRT_SOL_SET_WORDS; i++)
        CpuSet.auCpus[0] = (ulong_t)-1L;
    BT_CLEAR(CpuSet.auCpus, RTMpCpuId());

    rtMpSolCrossCall(&CpuSet, rtMpSolOnOtherCpusWrapper, &Args);

    RTThreadPreemptRestore(&PreemptState);

    return VINF_SUCCESS;
}



/**
 * Wrapper between the native solaris per-cpu callback and PFNRTWORKER
 * for the RTMpOnPair API.
 *
 * @returns Solaris error code.
 * @param   uArgs       Pointer to the RTMPARGS package.
 * @param   pvIgnored1  Ignored.
 * @param   pvIgnored2  Ignored.
 */
static int rtMpSolOnPairCpuWrapper(void *uArg, void *pvIgnored1, void *pvIgnored2)
{
    PRTMPARGS pArgs = (PRTMPARGS)(uArg);
    RTCPUID idCpu = RTMpCpuId();

    Assert(idCpu == pArgs->idCpu || idCpu == pArgs->idCpu2);
    pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
    ASMAtomicIncU32(&pArgs->cHits);

    NOREF(pvIgnored1);
    NOREF(pvIgnored2);
    return 0;
}


RTDECL(int) RTMpOnPair(RTCPUID idCpu1, RTCPUID idCpu2, uint32_t fFlags, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    int rc;
    RTMPARGS Args;
    RTSOLCPUSET CpuSet;
    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;

    AssertReturn(idCpu1 != idCpu2, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & RTMPON_F_VALID_MASK), VERR_INVALID_FLAGS);

    Args.pfnWorker = pfnWorker;
    Args.pvUser1   = pvUser1;
    Args.pvUser2   = pvUser2;
    Args.idCpu     = idCpu1;
    Args.idCpu2    = idCpu2;
    Args.cHits     = 0;

    for (int i = 0; i < IPRT_SOL_SET_WORDS; i++)
        CpuSet.auCpus[i] = 0;
    BT_SET(CpuSet.auCpus, idCpu1);
    BT_SET(CpuSet.auCpus, idCpu2);

    /*
     * Check that both CPUs are online before doing the broadcast call.
     */
    RTThreadPreemptDisable(&PreemptState);
    if (   RTMpIsCpuOnline(idCpu1)
        && RTMpIsCpuOnline(idCpu2))
    {
        rtMpSolCrossCall(&CpuSet, rtMpSolOnPairCpuWrapper, &Args);

        Assert(Args.cHits <= 2);
        if (Args.cHits == 2)
            rc = VINF_SUCCESS;
        else if (Args.cHits == 1)
            rc = VERR_NOT_ALL_CPUS_SHOWED;
        else if (Args.cHits == 0)
            rc = VERR_CPU_OFFLINE;
        else
            rc = VERR_CPU_IPE_1;
    }
    /*
     * A CPU must be present to be considered just offline.
     */
    else if (   RTMpIsCpuPresent(idCpu1)
             && RTMpIsCpuPresent(idCpu2))
        rc = VERR_CPU_OFFLINE;
    else
        rc = VERR_CPU_NOT_FOUND;

    RTThreadPreemptRestore(&PreemptState);
    return rc;
}


RTDECL(bool) RTMpOnPairIsConcurrentExecSupported(void)
{
    return true;
}


/**
 * Wrapper between the native solaris per-cpu callback and PFNRTWORKER
 * for the RTMpOnSpecific API.
 *
 * @returns Solaris error code.
 * @param   uArgs       Pointer to the RTMPARGS package.
 * @param   pvIgnored1  Ignored.
 * @param   pvIgnored2  Ignored.
 */
static int rtMpSolOnSpecificCpuWrapper(void *uArg, void *pvIgnored1, void *pvIgnored2)
{
    PRTMPARGS pArgs = (PRTMPARGS)(uArg);
    RTCPUID idCpu = RTMpCpuId();

    Assert(idCpu == pArgs->idCpu);
    pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
    ASMAtomicIncU32(&pArgs->cHits);

    NOREF(pvIgnored1);
    NOREF(pvIgnored2);
    return 0;
}


RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RTMPARGS Args;
    RTSOLCPUSET CpuSet;
    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
    RT_ASSERT_INTS_ON();

    if (idCpu >= ncpus)
        return VERR_CPU_NOT_FOUND;

    if (RT_UNLIKELY(!RTMpIsCpuOnline(idCpu)))
        return RTMpIsCpuPresent(idCpu) ? VERR_CPU_OFFLINE : VERR_CPU_NOT_FOUND;

    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = idCpu;
    Args.cHits = 0;

    for (int i = 0; i < IPRT_SOL_SET_WORDS; i++)
        CpuSet.auCpus[i] = 0;
    BT_SET(CpuSet.auCpus, idCpu);

    RTThreadPreemptDisable(&PreemptState);

    rtMpSolCrossCall(&CpuSet, rtMpSolOnSpecificCpuWrapper, &Args);

    RTThreadPreemptRestore(&PreemptState);

    Assert(ASMAtomicUoReadU32(&Args.cHits) <= 1);

    return ASMAtomicUoReadU32(&Args.cHits) == 1
         ? VINF_SUCCESS
         : VERR_CPU_NOT_FOUND;
}


RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    return true;
}

