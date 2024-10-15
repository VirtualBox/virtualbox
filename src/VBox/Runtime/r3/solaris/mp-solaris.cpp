/* $Id: mp-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Solaris.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <kstat.h>
#include <sys/processor.h>

#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/log.h>
#include <iprt/once.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Initialization serializing (rtMpSolarisOnce). */
static RTONCE       g_MpSolarisOnce = RTONCE_INITIALIZER;
/** Critical section serializing access to kstat. */
static RTCRITSECT   g_MpSolarisCritSect;
/** The kstat handle. */
static kstat_ctl_t *g_pKsCtl;
/** Array pointing to the cpu_info instances. */
static kstat_t    **g_papCpuInfo;
/** The number of entries in g_papCpuInfo */
static RTCPUID      g_capCpuInfo;
/** Array of core ids.  */
static uint64_t    *g_pu64CoreIds;
/** Number of entries in g_pu64CoreIds. */
static size_t       g_cu64CoreIds;
/** Number of cores in the system. */
static size_t       g_cCores;


/**
 * Helper for getting the core ID for a given CPU/strand/hyperthread.
 *
 * @returns The core ID.
 * @param   idCpu       The CPU ID instance.
 */
static inline uint64_t rtMpSolarisGetCoreId(RTCPUID idCpu)
{
    kstat_named_t *pStat = (kstat_named_t *)kstat_data_lookup(g_papCpuInfo[idCpu], (char *)"core_id");
    Assert(pStat->data_type == KSTAT_DATA_LONG);
    Assert(pStat->value.l >= 0);
    AssertCompile(sizeof(uint64_t) >= sizeof(long));    /* Paranoia. */
    return (uint64_t)pStat->value.l;
}


/**
 * Populates 'g_pu64CoreIds' array with unique core identifiers in the system.
 *
 * @returns VBox status code.
 */
static int rtMpSolarisGetCoreIds(void)
{
    for (RTCPUID idCpu = 0; idCpu < g_capCpuInfo; idCpu++)
    {
        /*
         * It is possible that the number of cores don't match the maximum number
         * of cores possible on the system. Hence check if we have a valid cpu_info
         * object. We don't want to break out if it's NULL, the array may be sparse
         * in theory, see @bugref{8469}.
         */
        if (g_papCpuInfo[idCpu])
        {
            if (kstat_read(g_pKsCtl, g_papCpuInfo[idCpu], 0) != -1)
            {
                /* Strands/Hyperthreads share the same core ID. */
                uint64_t u64CoreId  = rtMpSolarisGetCoreId(idCpu);
                bool     fAddedCore = false;
                for (RTCPUID i = 0; i < g_cCores; i++)
                {
                    if (g_pu64CoreIds[i] == u64CoreId)
                    {
                        fAddedCore = true;
                        break;
                    }
                }

                if (!fAddedCore)
                {
                    g_pu64CoreIds[g_cCores] = u64CoreId;
                    ++g_cCores;
                }
            }
            else
                return VERR_INTERNAL_ERROR_2;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Run once function that initializes the kstats we need here.
 *
 * @returns IPRT status code.
 * @param   pvUser      Unused.
 */
static DECLCALLBACK(int) rtMpSolarisOnce(void *pvUser)
{
    int rc = VINF_SUCCESS;
    NOREF(pvUser);

    /*
     * Open kstat and find the cpu_info entries for each of the CPUs.
     */
    g_pKsCtl = kstat_open();
    if (g_pKsCtl)
    {
        g_capCpuInfo = RTMpGetCount();
        if (RT_LIKELY(g_capCpuInfo > 0))
        {
            g_papCpuInfo = (kstat_t **)RTMemAllocZ(g_capCpuInfo * sizeof(kstat_t *));
            if (g_papCpuInfo)
            {
                g_cu64CoreIds = g_capCpuInfo;
                g_pu64CoreIds = (uint64_t *)RTMemAllocZ(g_cu64CoreIds * sizeof(uint64_t));
                if (g_pu64CoreIds)
                {
                    rc = RTCritSectInit(&g_MpSolarisCritSect);
                    if (RT_SUCCESS(rc))
                    {
                        RTCPUID i = 0;
                        for (kstat_t *pKsp = g_pKsCtl->kc_chain; pKsp != NULL; pKsp = pKsp->ks_next)
                        {
                            if (!RTStrCmp(pKsp->ks_module, "cpu_info"))
                            {
                                AssertBreak(i < g_capCpuInfo);
                                g_papCpuInfo[i++] = pKsp;
                                /** @todo ks_instance == cpu_id (/usr/src/uts/common/os/cpu.c)? Check this and fix it ASAP. */
                            }
                        }

                        rc = rtMpSolarisGetCoreIds();
                        if (RT_SUCCESS(rc))
                            return VINF_SUCCESS;
                        else
                            Log(("rtMpSolarisGetCoreIds failed. rc=%Rrc\n", rc));
                    }

                    RTMemFree(g_pu64CoreIds);
                    g_pu64CoreIds = NULL;
                }
                else
                    rc = VERR_NO_MEMORY;

                /* bail out, we failed. */
                RTMemFree(g_papCpuInfo);
                g_papCpuInfo = NULL;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_CPU_IPE_1;
        kstat_close(g_pKsCtl);
        g_pKsCtl = NULL;
    }
    else
    {
        rc = RTErrConvertFromErrno(errno);
        if (RT_SUCCESS(rc))
            rc = VERR_INTERNAL_ERROR;
        Log(("kstat_open() -> %d (%Rrc)\n", errno, rc));
    }

    return rc;
}


/**
 * RTOnceEx() cleanup function.
 *
 * @param   pvUser              Unused.
 * @param   fLazyCleanUpOk      Whether lazy cleanup is okay or not.
 */
static DECLCALLBACK(void) rtMpSolarisCleanUp(void *pvUser, bool fLazyCleanUpOk)
{
    if (g_pKsCtl)
        kstat_close(g_pKsCtl);
    RTMemFree(g_pu64CoreIds);
    RTMemFree(g_papCpuInfo);
}


/**
 * Worker for RTMpGetCurFrequency and RTMpGetMaxFrequency.
 *
 * @returns The desired frequency on success, 0 on failure.
 *
 * @param   idCpu           The CPU ID.
 * @param   pszStatName     The cpu_info stat name.
 */
static uint64_t rtMpSolarisGetFrequency(RTCPUID idCpu, const char *pszStatName)
{
    uint64_t u64 = 0;
    int rc = RTOnceEx(&g_MpSolarisOnce, rtMpSolarisOnce, rtMpSolarisCleanUp, NULL /* pvUser */);
    if (RT_SUCCESS(rc))
    {
        if (    idCpu < g_capCpuInfo
            &&  g_papCpuInfo[idCpu])
        {
            rc = RTCritSectEnter(&g_MpSolarisCritSect);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                if (kstat_read(g_pKsCtl, g_papCpuInfo[idCpu], 0) != -1)
                {
                    /* Solaris really need to fix their APIs. Explicitly cast for now. */
                    kstat_named_t *pStat = (kstat_named_t *)kstat_data_lookup(g_papCpuInfo[idCpu], (char*)pszStatName);
                    if (pStat)
                    {
                        Assert(pStat->data_type == KSTAT_DATA_UINT64 || pStat->data_type == KSTAT_DATA_LONG);
                        switch (pStat->data_type)
                        {
                            case KSTAT_DATA_UINT64: u64 = pStat->value.ui64; break; /* current_clock_Hz */
                            case KSTAT_DATA_INT32:  u64 = pStat->value.i32;  break; /* clock_MHz */

                            /* just in case... */
                            case KSTAT_DATA_UINT32: u64 = pStat->value.ui32; break;
                            case KSTAT_DATA_INT64:  u64 = pStat->value.i64;  break;
                            default:
                                AssertMsgFailed(("%d\n", pStat->data_type));
                                break;
                        }
                    }
                    else
                        Log(("kstat_data_lookup(%s) -> %d\n", pszStatName, errno));
                }
                else
                    Log(("kstat_read() -> %d\n", errno));
                RTCritSectLeave(&g_MpSolarisCritSect);
            }
        }
        else
            Log(("invalid idCpu: %d (g_capCpuInfo=%d)\n", (int)idCpu, (int)g_capCpuInfo));
    }

    return u64;
}


RTDECL(uint32_t) RTMpGetCurFrequency(RTCPUID idCpu)
{
    return rtMpSolarisGetFrequency(idCpu, "current_clock_Hz") / 1000000;
}


RTDECL(uint32_t) RTMpGetMaxFrequency(RTCPUID idCpu)
{
    return rtMpSolarisGetFrequency(idCpu, "clock_MHz");
}


#if defined(RT_ARCH_SPARC) || defined(RT_ARCH_SPARC64)
RTDECL(RTCPUID) RTMpCpuId(void)
{
    /** @todo implement RTMpCpuId on solaris/r3! */
    return NIL_RTCPUID;
}
#endif


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS ? (int)idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu < RTCPUSET_MAX_CPUS ? iCpu : NIL_RTCPUID;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return RTMpGetCount() - 1;
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return idCpu != NIL_RTCPUID
        && idCpu < (RTCPUID)RTMpGetCount();
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    int iStatus = p_online(idCpu, P_STATUS);
    return iStatus == P_ONLINE
        || iStatus == P_NOINTR;
}


RTDECL(bool) RTMpIsCpuPresent(RTCPUID idCpu)
{
    int iStatus = p_online(idCpu, P_STATUS);
    return iStatus != -1;
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    /*
     * Solaris has sysconf.
     */
    int cCpus = sysconf(_SC_NPROCESSORS_MAX);
    if (cCpus < 0)
        cCpus = sysconf(_SC_NPROCESSORS_CONF);
    return cCpus;
}


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    RTCpuSetEmpty(pSet);
    int idCpu = RTMpGetCount();
    while (idCpu-- > 0)
        RTCpuSetAdd(pSet, idCpu);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    /*
     * Solaris has sysconf.
     */
    return sysconf(_SC_NPROCESSORS_ONLN);
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    RTCpuSetEmpty(pSet);
    RTCPUID cCpus = RTMpGetCount();
    for (RTCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        if (RTMpIsCpuOnline(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    return pSet;
}


RTDECL(PRTCPUSET) RTMpGetPresentSet(PRTCPUSET pSet)
{
#ifdef RT_STRICT
    RTCPUID cCpusPresent = 0;
#endif
    RTCpuSetEmpty(pSet);
    RTCPUID cCpus = RTMpGetCount();
    for (RTCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        if (RTMpIsCpuPresent(idCpu))
        {
            RTCpuSetAdd(pSet, idCpu);
#ifdef RT_STRICT
            cCpusPresent++;
#endif
        }
    Assert(cCpusPresent == RTMpGetPresentCount());
    return pSet;
}


RTDECL(RTCPUID) RTMpGetPresentCount(void)
{
    /*
     * Solaris has sysconf.
     */
    return sysconf(_SC_NPROCESSORS_CONF);
}


RTDECL(RTCPUID) RTMpGetPresentCoreCount(void)
{
    return RTMpGetCoreCount();
}


RTDECL(RTCPUID) RTMpGetCoreCount(void)
{
    int rc = RTOnceEx(&g_MpSolarisOnce, rtMpSolarisOnce, rtMpSolarisCleanUp, NULL /* pvUser */);
    if (RT_SUCCESS(rc))
        return g_cCores;

    return 0;
}


RTDECL(RTCPUID) RTMpGetOnlineCoreCount(void)
{
    RTCPUID uOnlineCores = 0;
    int rc = RTOnceEx(&g_MpSolarisOnce, rtMpSolarisOnce, rtMpSolarisCleanUp, NULL /* pvUser */);
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectEnter(&g_MpSolarisCritSect);
        AssertRC(rc);

        /*
         * For each core in the system, count how many are currently online.
         */
        for (RTCPUID j = 0; j < g_cCores; j++)
        {
            uint64_t u64CoreId = g_pu64CoreIds[j];
            for (RTCPUID idCpu = 0; idCpu < g_capCpuInfo; idCpu++)
            {
                rc = kstat_read(g_pKsCtl, g_papCpuInfo[idCpu], 0);
                AssertReturn(rc != -1, 0 /* rc */);
                uint64_t u64ThreadCoreId = rtMpSolarisGetCoreId(idCpu);
                if (u64ThreadCoreId == u64CoreId)
                {
                    kstat_named_t *pStat = (kstat_named_t *)kstat_data_lookup(g_papCpuInfo[idCpu], (char *)"state");
                    Assert(pStat->data_type == KSTAT_DATA_CHAR);
                    if(   !RTStrNCmp(pStat->value.c, PS_ONLINE, sizeof(PS_ONLINE) - 1)
                       || !RTStrNCmp(pStat->value.c, PS_NOINTR, sizeof(PS_NOINTR) - 1))
                    {
                        uOnlineCores++;
                        break;      /* Move to the next core. We have at least 1 hyperthread online in the current core. */
                    }
                }
            }
        }

        RTCritSectLeave(&g_MpSolarisCritSect);
    }

    return uOnlineCores;
}

