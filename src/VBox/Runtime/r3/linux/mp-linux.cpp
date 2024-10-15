/* $Id: mp-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Linux.
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
#define LOG_GROUP RTLOGGROUP_SYSTEM
#include <stdio.h>
#include <errno.h>

#include <iprt/mp.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/cpuset.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/linux/sysfs.h>


#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
# include <sched.h>

RTDECL(RTCPUID) RTMpCpuId(void)
{
    int rc = sched_getcpu();
    if (rc >= 0)
        return (RTCPUID)rc;

    return NIL_RTCPUID;
}
#endif

/**
 * Internal worker that determines the max possible CPU count.
 *
 * @returns Max cpus.
 */
static RTCPUID rtMpLinuxMaxCpus(void)
{
#if 0 /* this doesn't do the right thing :-/ */
    int cMax = sysconf(_SC_NPROCESSORS_CONF);
    Assert(cMax >= 1);
    return cMax;
#else
    static uint32_t s_cMax = 0;
    if (!s_cMax)
    {
        int cMax = 1;
        for (unsigned iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
            if (RTLinuxSysFsExists("devices/system/cpu/cpu%d", iCpu))
                cMax = iCpu + 1;
        ASMAtomicUoWriteU32((uint32_t volatile *)&s_cMax, cMax);
        return cMax;
    }
    return s_cMax;
#endif
}

/**
 * Internal worker that picks the processor speed in MHz from /proc/cpuinfo.
 *
 * @returns CPU frequency.
 */
static uint32_t rtMpLinuxGetFrequency(RTCPUID idCpu)
{
    FILE *pFile = fopen("/proc/cpuinfo", "r");
    if (!pFile)
        return 0;

    char sz[256];
    RTCPUID idCpuFound = NIL_RTCPUID;
    uint32_t Frequency = 0;
    while (fgets(sz, sizeof(sz), pFile))
    {
        char *psz;
        if (   !strncmp(sz, RT_STR_TUPLE("processor"))
            && (sz[10] == ' ' || sz[10] == '\t' || sz[10] == ':')
            && (psz = strchr(sz, ':')))
        {
            psz += 2;
            int64_t iCpu;
            int rc = RTStrToInt64Ex(psz, NULL, 0, &iCpu);
            if (RT_SUCCESS(rc))
                idCpuFound = iCpu;
        }
        else if (   idCpu == idCpuFound
                 && !strncmp(sz, RT_STR_TUPLE("cpu MHz"))
                 && (sz[10] == ' ' || sz[10] == '\t' || sz[10] == ':')
                 && (psz = strchr(sz, ':')))
        {
            psz += 2;
            int64_t v;
            int rc = RTStrToInt64Ex(psz, &psz, 0, &v);
            if (RT_SUCCESS(rc))
            {
                Frequency = v;
                break;
            }
        }
    }
    fclose(pFile);
    return Frequency;
}


/** @todo RTmpCpuId(). */

RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < rtMpLinuxMaxCpus() ? (int)idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu < rtMpLinuxMaxCpus() ? iCpu : NIL_RTCPUID;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return rtMpLinuxMaxCpus() - 1;
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    /** @todo check if there is a simpler interface than this... */
    int64_t i = 0;
    int rc = RTLinuxSysFsReadIntFile(0, &i, "devices/system/cpu/cpu%d/online", (int)idCpu);
    if (    RT_FAILURE(rc)
        &&  RTLinuxSysFsExists("devices/system/cpu/cpu%d", (int)idCpu))
    {
        /** @todo Assert(!RTLinuxSysFsExists("devices/system/cpu/cpu%d/online",
         *               (int)idCpu));
         * Unfortunately, the online file wasn't always world readable (centos
         * 2.6.18-164). */
        i = 1;
        rc = VINF_SUCCESS;
    }

    AssertMsg(i == 0 || i == -1 || i == 1, ("i=%d\n", i));
    return RT_SUCCESS(rc) && i != 0;
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    /** @todo check this up with hotplugging! */
    return RTLinuxSysFsExists("devices/system/cpu/cpu%d", (int)idCpu);
}


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    RTCpuSetEmpty(pSet);
    RTCPUID cMax = rtMpLinuxMaxCpus();
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
        if (RTMpIsCpuPossible(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    RTCPUSET Set;
    RTMpGetSet(&Set);
    return RTCpuSetCount(&Set);
}


RTDECL(RTCPUID) RTMpGetCoreCount(void)
{
    RTCPUID     cMax      = rtMpLinuxMaxCpus();
    uint32_t   *paidCores = (uint32_t *)alloca(sizeof(paidCores[0]) * (cMax + 1));
    uint32_t   *paidPckgs = (uint32_t *)alloca(sizeof(paidPckgs[0]) * (cMax + 1));
    uint32_t    cCores    = 0;
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
    {
        if (RTMpIsCpuPossible(idCpu))
        {
            int64_t idCore = 0;
            int64_t idPckg = 0;

            int rc = RTLinuxSysFsReadIntFile(0, &idCore, "devices/system/cpu/cpu%d/topology/core_id", (int)idCpu);
            if (RT_SUCCESS(rc))
                rc = RTLinuxSysFsReadIntFile(0, &idPckg, "devices/system/cpu/cpu%d/topology/physical_package_id", (int)idCpu);

            if (RT_SUCCESS(rc))
            {
                uint32_t i;

                for (i = 0; i < cCores; i++)
                    if (   paidCores[i] == (uint32_t)idCore
                        && paidPckgs[i] == (uint32_t)idPckg)
                        break;
                if (i >= cCores)
                {
                    paidCores[cCores] = (uint32_t)idCore;
                    paidPckgs[cCores] = (uint32_t)idPckg;
                    cCores++;
                }
            }
        }
    }
    Assert(cCores > 0);
    return cCores;
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    RTCpuSetEmpty(pSet);
    RTCPUID cMax = rtMpLinuxMaxCpus();
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
        if (RTMpIsCpuOnline(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    RTCPUSET Set;
    RTMpGetOnlineSet(&Set);
    return RTCpuSetCount(&Set);
}


RTDECL(RTCPUID) RTMpGetOnlineCoreCount(void)
{
    RTCPUID     cMax      = rtMpLinuxMaxCpus();
    uint32_t   *paidCores = (uint32_t *)alloca(sizeof(paidCores[0]) * (cMax + 1));
    uint32_t   *paidPckgs = (uint32_t *)alloca(sizeof(paidPckgs[0]) * (cMax + 1));
    uint32_t    cCores    = 0;
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
    {
        if (RTMpIsCpuOnline(idCpu))
        {
            int64_t idCore = 0;
            int64_t idPckg = 0;

            int rc = RTLinuxSysFsReadIntFile(0, &idCore, "devices/system/cpu/cpu%d/topology/core_id", (int)idCpu);
            if (RT_SUCCESS(rc))
                rc = RTLinuxSysFsReadIntFile(0, &idPckg, "devices/system/cpu/cpu%d/topology/physical_package_id", (int)idCpu);

            if (RT_SUCCESS(rc))
            {
                uint32_t i;

                for (i = 0; i < cCores; i++)
                    if (   paidCores[i] == idCore
                        && paidPckgs[i] == idPckg)
                        break;
                if (i >= cCores)
                {
                    paidCores[cCores] = idCore;
                    paidPckgs[cCores] = idPckg;
                    cCores++;
                }
            }
        }
    }
    Assert(cCores > 0);
    return cCores;
}



RTDECL(uint32_t) RTMpGetCurFrequency(RTCPUID idCpu)
{
    int64_t kHz = 0;
    int rc = RTLinuxSysFsReadIntFile(0, &kHz, "devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq", (int)idCpu);
    if (RT_FAILURE(rc))
    {
        /*
         * The file may be just unreadable - in that case use plan B, i.e.
         * /proc/cpuinfo to get the data we want. The assumption is that if
         * cpuinfo_cur_freq doesn't exist then the speed won't change, and
         * thus cur == max. If it does exist then cpuinfo contains the
         * current frequency.
         */
        kHz = rtMpLinuxGetFrequency(idCpu) * 1000;
    }
    return (kHz + 999) / 1000;
}


RTDECL(uint32_t) RTMpGetMaxFrequency(RTCPUID idCpu)
{
    int64_t kHz = 0;
    int rc = RTLinuxSysFsReadIntFile(0, &kHz, "devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", (int)idCpu);
    if (RT_FAILURE(rc))
    {
        /*
         * Check if the file isn't there - if it is there, then /proc/cpuinfo
         * would provide current frequency information, which is wrong.
         */
        if (!RTLinuxSysFsExists("devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", (int)idCpu))
            kHz = rtMpLinuxGetFrequency(idCpu) * 1000;
        else
            kHz = 0;
    }
    return (kHz + 999) / 1000;
}
