/* $Id: mp-freebsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, FreeBSD.
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
#define LOG_GROUP RTLOGGROUP_SYSTEM
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/sysctl.h>

#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/log.h>
#include <iprt/once.h>
#include <iprt/critsect.h>


/**
 * Internal worker that determines the max possible CPU count.
 *
 * @returns Max cpus.
 */
static RTCPUID rtMpFreeBsdMaxCpus(void)
{
    int aiMib[2];
    aiMib[0] = CTL_HW;
    aiMib[1] = HW_NCPU;
    int cCpus = -1;
    size_t cb = sizeof(cCpus);
    int rc = sysctl(aiMib, RT_ELEMENTS(aiMib), &cCpus, &cb, NULL, 0);
    if (rc != -1 && cCpus >= 1)
        return cCpus;
    AssertFailed();
    return 1;
}


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS && idCpu < rtMpFreeBsdMaxCpus() ? idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu < rtMpFreeBsdMaxCpus() ? iCpu : NIL_RTCPUID;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return rtMpFreeBsdMaxCpus() - 1;
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    /*
     * FreeBSD doesn't support CPU hotplugging so every CPU which appears
     * in the tree is also online.
     */
    char    szName[40];
    RTStrPrintf(szName, sizeof(szName), "dev.cpu.%d.%%driver", (int)idCpu);

    char    szDriver[10];
    size_t  cbDriver = sizeof(szDriver);
    RT_ZERO(szDriver);                  /* this shouldn't be necessary. */
    int rcBsd = sysctlbyname(szName, szDriver, &cbDriver, NULL, 0);
    if (rcBsd == 0)
        return true;

    return false;
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return idCpu != NIL_RTCPUID
        && idCpu < rtMpFreeBsdMaxCpus();
}


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    RTCpuSetEmpty(pSet);
    RTCPUID cMax = rtMpFreeBsdMaxCpus();
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
        if (RTMpIsCpuPossible(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    return rtMpFreeBsdMaxCpus();
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    RTCpuSetEmpty(pSet);
    RTCPUID cMax = rtMpFreeBsdMaxCpus();
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
        if (RTMpIsCpuOnline(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    /*
     * FreeBSD has sysconf.
     */
    return sysconf(_SC_NPROCESSORS_ONLN);
}


RTDECL(uint32_t) RTMpGetCurFrequency(RTCPUID idCpu)
{
    int uFreqCurr = 0;
    size_t cbParameter = sizeof(uFreqCurr);

    if (!RTMpIsCpuOnline(idCpu))
        return 0;

    /* CPU's have a common frequency. */
    int rc = sysctlbyname("dev.cpu.0.freq", &uFreqCurr, &cbParameter, NULL, 0);
    if (rc)
        return 0;

    return (uint32_t)uFreqCurr;
}


RTDECL(uint32_t) RTMpGetMaxFrequency(RTCPUID idCpu)
{
    char szFreqLevels[20]; /* Should be enough to get the highest level which is always the first. */
    size_t cbFreqLevels = sizeof(szFreqLevels);

    if (!RTMpIsCpuOnline(idCpu))
        return 0;

    memset(szFreqLevels, 0, sizeof(szFreqLevels));

    /*
     * CPU 0 has the freq levels entry. ENOMEM is ok as we don't need all supported
     * levels but only the first one.
     */
    int rc = sysctlbyname("dev.cpu.0.freq_levels", szFreqLevels, &cbFreqLevels, NULL, 0);
    if (   (rc && (errno != ENOMEM))
        || (cbFreqLevels == 0))
        return 0;

    /* Clear everything starting from the '/' */
    unsigned i = 0;

    do
    {
        if (szFreqLevels[i] == '/')
        {
            memset(&szFreqLevels[i], 0, sizeof(szFreqLevels) - i);
            break;
        }
        i++;
    } while (i < sizeof(szFreqLevels));

    /* Returns 0 on failure. */
    return RTStrToUInt32(szFreqLevels);
}

