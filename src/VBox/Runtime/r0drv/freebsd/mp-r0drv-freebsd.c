/* $Id: mp-r0drv-freebsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, FreeBSD.
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
#include "the-freebsd-kernel.h"

#include <iprt/mp.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/cpuset.h>
#include "r0drv/mp-r0drv.h"


#if __FreeBSD_version < 1200028
# define smp_no_rendezvous_barrier smp_no_rendevous_barrier
#endif

RTDECL(RTCPUID) RTMpCpuId(void)
{
    return curcpu;
}


RTDECL(int) RTMpCurSetIndex(void)
{
    return curcpu;
}


RTDECL(int) RTMpCurSetIndexAndId(PRTCPUID pidCpu)
{
    return *pidCpu = curcpu;
}


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS && idCpu <= mp_maxid ? (int)idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu <= mp_maxid ? (RTCPUID)iCpu : NIL_RTCPUID;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return mp_maxid;
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return idCpu <= mp_maxid;
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
    return mp_maxid + 1;
}


RTDECL(RTCPUID) RTMpGetCoreCount(void)
{
    return mp_maxid + 1;
}

RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    return idCpu <= mp_maxid
        && !CPU_ABSENT(idCpu);
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    RTCPUID idCpu;

    RTCpuSetEmpty(pSet);
    idCpu = RTMpGetMaxCpuId();
    do
    {
        if (RTMpIsCpuOnline(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    } while (idCpu-- > 0);

    return pSet;
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    return mp_ncpus;
}


/**
 * Wrapper between the native FreeBSD per-cpu callback and PFNRTWORKER
 * for the RTMpOnAll API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
static void rtmpOnAllFreeBSDWrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    pArgs->pfnWorker(curcpu, pArgs->pvUser1, pArgs->pvUser2);
}


RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RTMPARGS Args;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = NIL_RTCPUID;
    Args.cHits = 0;
    smp_rendezvous(NULL, rtmpOnAllFreeBSDWrapper, smp_no_rendezvous_barrier, &Args);
    return VINF_SUCCESS;
}


/**
 * Wrapper between the native FreeBSD per-cpu callback and PFNRTWORKER
 * for the RTMpOnOthers API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
static void rtmpOnOthersFreeBSDWrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    RTCPUID idCpu = curcpu;
    if (pArgs->idCpu != idCpu)
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
}


RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    /* Will panic if no rendezvousing cpus, so check up front. */
    if (RTMpGetOnlineCount() > 1)
    {
#if __FreeBSD_version >= 900000
        cpuset_t    Mask;
#elif  __FreeBSD_version >= 700000
        cpumask_t   Mask;
#endif
        RTMPARGS    Args;

        Args.pfnWorker = pfnWorker;
        Args.pvUser1 = pvUser1;
        Args.pvUser2 = pvUser2;
        Args.idCpu = RTMpCpuId();
        Args.cHits = 0;
#if __FreeBSD_version >= 700000
# if __FreeBSD_version >= 900000
        Mask = all_cpus;
        CPU_CLR(curcpu, &Mask);
# else
        Mask = ~(cpumask_t)curcpu;
# endif
        smp_rendezvous_cpus(Mask, NULL, rtmpOnOthersFreeBSDWrapper, smp_no_rendezvous_barrier, &Args);
#else
        smp_rendezvous(NULL, rtmpOnOthersFreeBSDWrapper, NULL, &Args);
#endif
    }
    return VINF_SUCCESS;
}


/**
 * Wrapper between the native FreeBSD per-cpu callback and PFNRTWORKER
 * for the RTMpOnSpecific API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
static void rtmpOnSpecificFreeBSDWrapper(void *pvArg)
{
    PRTMPARGS   pArgs = (PRTMPARGS)pvArg;
    RTCPUID     idCpu = curcpu;
    if (pArgs->idCpu == idCpu)
    {
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
        ASMAtomicIncU32(&pArgs->cHits);
    }
}


RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
#if __FreeBSD_version >= 900000
    cpuset_t    Mask;
#elif  __FreeBSD_version >= 700000
    cpumask_t   Mask;
#endif
    RTMPARGS    Args;

    /* Will panic if no rendezvousing cpus, so make sure the cpu is online. */
    if (!RTMpIsCpuOnline(idCpu))
        return VERR_CPU_NOT_FOUND;

    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = idCpu;
    Args.cHits = 0;
#if __FreeBSD_version >= 700000
# if __FreeBSD_version >= 900000
    CPU_SETOF(idCpu, &Mask);
# else
    Mask = (cpumask_t)1 << idCpu;
# endif
    smp_rendezvous_cpus(Mask, NULL, rtmpOnSpecificFreeBSDWrapper, smp_no_rendezvous_barrier, &Args);
#else
    smp_rendezvous(NULL, rtmpOnSpecificFreeBSDWrapper, NULL, &Args);
#endif
    return Args.cHits == 1
         ? VINF_SUCCESS
         : VERR_CPU_NOT_FOUND;
}


#if __FreeBSD_version >= 700000
/**
 * Dummy callback for RTMpPokeCpu.
 * @param   pvArg   Ignored
 */
static void rtmpFreeBSDPokeCallback(void *pvArg)
{
    NOREF(pvArg);
}


RTDECL(int) RTMpPokeCpu(RTCPUID idCpu)
{
#if __FreeBSD_version >= 900000
    cpuset_t    Mask;
#elif  __FreeBSD_version >= 700000
    cpumask_t   Mask;
#endif

    /* Will panic if no rendezvousing cpus, so make sure the cpu is online. */
    if (!RTMpIsCpuOnline(idCpu))
        return VERR_CPU_NOT_FOUND;

# if __FreeBSD_version >= 900000
    CPU_SETOF(idCpu, &Mask);
# else
    Mask = (cpumask_t)1 << idCpu;
# endif
    smp_rendezvous_cpus(Mask, NULL, rtmpFreeBSDPokeCallback, smp_no_rendezvous_barrier, NULL);

    return VINF_SUCCESS;
}

#else  /* < 7.0 */
RTDECL(int) RTMpPokeCpu(RTCPUID idCpu)
{
    return VERR_NOT_SUPPORTED;
}
#endif /* < 7.0 */


RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    return true;
}

