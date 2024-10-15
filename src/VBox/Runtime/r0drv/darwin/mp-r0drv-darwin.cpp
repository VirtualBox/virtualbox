/* $Id: mp-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/mp.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include "r0drv/mp-r0drv.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int32_t volatile g_cMaxCpus = -1;


static int rtMpDarwinInitMaxCpus(void)
{
    IPRT_DARWIN_SAVE_EFL_AC();

    int32_t cCpus = -1;
    size_t  oldLen = sizeof(cCpus);
    int rc = sysctlbyname("hw.ncpu", &cCpus, &oldLen, NULL, NULL);
    if (rc)
    {
        printf("IPRT: sysctlbyname(hw.ncpu) failed with rc=%d!\n", rc);
        cCpus = 64; /* whatever */
    }

    ASMAtomicWriteS32(&g_cMaxCpus, cCpus);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return cCpus;
}


DECLINLINE(int) rtMpDarwinMaxCpus(void)
{
    int cCpus = g_cMaxCpus;
    if (RT_UNLIKELY(cCpus <= 0))
        return rtMpDarwinInitMaxCpus();
    return cCpus;
}


RTDECL(RTCPUID) RTMpCpuId(void)
{
    return cpu_number();
}


RTDECL(int) RTMpCurSetIndex(void)
{
    return cpu_number();
}


RTDECL(int) RTMpCurSetIndexAndId(PRTCPUID pidCpu)
{
    return *pidCpu = cpu_number();
}


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS ? (int)idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu < RTCPUSET_MAX_CPUS ? (RTCPUID)iCpu : NIL_RTCPUID;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return rtMpDarwinMaxCpus() - 1;
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS
        && idCpu < (RTCPUID)rtMpDarwinMaxCpus();
}


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    RTCPUID idCpu;

    RTCpuSetEmpty(pSet);
    idCpu = RTMpGetMaxCpuId();
    do
    {
        if (RTMpIsCpuPossible(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    } while (idCpu-- > 0);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    return rtMpDarwinMaxCpus();
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    /** @todo darwin R0 MP */
    return RTMpGetSet(pSet);
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    /** @todo darwin R0 MP */
    return RTMpGetCount();
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    /** @todo darwin R0 MP */
    return RTMpIsCpuPossible(idCpu);
}


RTDECL(uint32_t) RTMpGetCurFrequency(RTCPUID idCpu)
{
    /** @todo darwin R0 MP (rainy day) */
    RT_NOREF(idCpu);
    return 0;
}


RTDECL(uint32_t) RTMpGetMaxFrequency(RTCPUID idCpu)
{
    /** @todo darwin R0 MP (rainy day) */
    RT_NOREF(idCpu);
    return 0;
}


RTDECL(bool) RTMpIsCpuWorkPending(void)
{
    /** @todo (not used on non-Windows platforms yet). */
    return false;
}


/**
 * Wrapper between the native darwin per-cpu callback and PFNRTWORKER
 * for the RTMpOnAll API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
static void rtmpOnAllDarwinWrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    IPRT_DARWIN_SAVE_EFL_AC();
    pArgs->pfnWorker(cpu_number(), pArgs->pvUser1, pArgs->pvUser2);
    IPRT_DARWIN_RESTORE_EFL_AC();
}


RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();

    RTMPARGS Args;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = NIL_RTCPUID;
    Args.cHits = 0;
    mp_rendezvous_no_intrs(rtmpOnAllDarwinWrapper, &Args);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


/**
 * Wrapper between the native darwin per-cpu callback and PFNRTWORKER
 * for the RTMpOnOthers API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
static void rtmpOnOthersDarwinWrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    RTCPUID idCpu = cpu_number();
    if (pArgs->idCpu != idCpu)
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
        IPRT_DARWIN_RESTORE_EFL_AC();
    }
}


RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();

    RTMPARGS Args;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = RTMpCpuId();
    Args.cHits = 0;
    mp_rendezvous_no_intrs(rtmpOnOthersDarwinWrapper, &Args);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


/**
 * Wrapper between the native darwin per-cpu callback and PFNRTWORKER
 * for the RTMpOnSpecific API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
static void rtmpOnSpecificDarwinWrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    RTCPUID idCpu = cpu_number();
    if (pArgs->idCpu == idCpu)
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
        ASMAtomicIncU32(&pArgs->cHits);
        IPRT_DARWIN_RESTORE_EFL_AC();
    }
}


RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RT_ASSERT_INTS_ON();
    IPRT_DARWIN_SAVE_EFL_AC();

    RTMPARGS Args;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = idCpu;
    Args.cHits = 0;
    mp_rendezvous_no_intrs(rtmpOnSpecificDarwinWrapper, &Args);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return Args.cHits == 1
         ? VINF_SUCCESS
         : VERR_CPU_NOT_FOUND;
}


RTDECL(int) RTMpPokeCpu(RTCPUID idCpu)
{
    RT_ASSERT_INTS_ON();

    if (g_pfnR0DarwinCpuInterrupt == NULL)
        return VERR_NOT_SUPPORTED;
    IPRT_DARWIN_SAVE_EFL_AC(); /* paranoia */
    /// @todo use mp_cpus_kick() when available (since 10.10)?  It's probably slower (locks, mask iteration, checks), though...
    g_pfnR0DarwinCpuInterrupt(idCpu);
    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    return true;
}

