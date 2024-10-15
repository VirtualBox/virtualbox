/* $Id: mp-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Windows.
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
#include <iprt/win/windows.h>

#include <iprt/mp.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/param.h>
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# include <iprt/asm-amd64-x86.h>
#endif
#if defined(VBOX) && !defined(IN_GUEST) && !defined(IN_RT_STATIC)
# include <VBox/sup.h>
# define IPRT_WITH_GIP_MP_INFO
#else
# undef  IPRT_WITH_GIP_MP_INFO
#endif

#include "internal-r3-win.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RTMPWIN_UPDATE_GIP_GLOBALS
 * Does lazy (re-)initialization using information provieded by GIP. */
#ifdef IPRT_WITH_GIP_MP_INFO
# define RTMPWIN_UPDATE_GIP_GLOBALS() \
    do { RTMPWIN_UPDATE_GIP_GLOBALS_AND_GET_PGIP(); } while (0)
#else
# define RTMPWIN_UPDATE_GIP_GLOBALS() do { } while (0)
#endif

/** @def RTMPWIN_UPDATE_GIP_GLOBALS_AND_GET_PGIP
 * Does lazy (re-)initialization using information provieded by GIP and
 * declare and initalize a pGip local variable. */
#ifdef IPRT_WITH_GIP_MP_INFO
#define RTMPWIN_UPDATE_GIP_GLOBALS_AND_GET_PGIP() \
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage; \
    if (pGip) \
    { \
        if (   pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC \
            && RTOnce(&g_MpInitOnceGip, rtMpWinInitOnceGip, NULL) == VINF_SUCCESS) \
        { \
            if (g_cRtMpWinActiveCpus >= pGip->cOnlineCpus) \
            { /* likely */ } \
            else \
                rtMpWinRefreshGip(); \
        } \
        else \
            pGip = NULL; \
    } else do { } while (0)
#else
# define RTMPWIN_UPDATE_GIP_GLOBALS_AND_GET_PGIP() do { } while (0)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Initialize once. */
static RTONCE                                       g_MpInitOnce = RTONCE_INITIALIZER;
#ifdef IPRT_WITH_GIP_MP_INFO
/** Initialize once using GIP. */
static RTONCE                                       g_MpInitOnceGip = RTONCE_INITIALIZER;
#endif

static decltype(GetMaximumProcessorCount)          *g_pfnGetMaximumProcessorCount;
//static decltype(GetActiveProcessorCount)           *g_pfnGetActiveProcessorCount;
static decltype(GetCurrentProcessorNumber)         *g_pfnGetCurrentProcessorNumber;
static decltype(GetCurrentProcessorNumberEx)       *g_pfnGetCurrentProcessorNumberEx;
static decltype(GetLogicalProcessorInformation)    *g_pfnGetLogicalProcessorInformation;
static decltype(GetLogicalProcessorInformationEx)  *g_pfnGetLogicalProcessorInformationEx;


/** The required buffer size for getting group relations. */
static uint32_t     g_cbRtMpWinGrpRelBuf;
/** The max number of CPUs (RTMpGetCount). */
static uint32_t     g_cRtMpWinMaxCpus;
/** The max number of CPU cores (RTMpGetCoreCount). */
static uint32_t     g_cRtMpWinMaxCpuCores;
/** The max number of groups. */
static uint32_t     g_cRtMpWinMaxCpuGroups;
/** The number of active CPUs the last time we checked. */
static uint32_t volatile g_cRtMpWinActiveCpus;
/** Static per group info.
 * @remarks  With 256 entries this takes up 33KB.
 * @sa g_aRtMpNtCpuGroups */
static struct
{
    /** The max CPUs in the group. */
    uint16_t    cMaxCpus;
    /** The number of active CPUs at the time of initialization. */
    uint16_t    cActiveCpus;
    /** CPU set indexes for each CPU in the group. */
    int16_t     aidxCpuSetMembers[64];
}                   g_aRtMpWinCpuGroups[256];
/** Maps CPU set indexes to RTCPUID.
 * @sa g_aidRtMpNtByCpuSetIdx  */
RTCPUID             g_aidRtMpWinByCpuSetIdx[RTCPUSET_MAX_CPUS];


/**
 * @callback_method_impl{FNRTONCE,
 *      Resolves dynamic imports and initializes globals.}
 */
static DECLCALLBACK(int32_t) rtMpWinInitOnce(void *pvUser)
{
    RT_NOREF(pvUser);

    Assert(g_WinOsInfoEx.dwOSVersionInfoSize != 0);
    Assert(g_hModKernel32 != NULL);

    /*
     * Resolve dynamic APIs.
     */
#define RESOLVE_API(a_szMod, a_FnName) \
        do { \
            RT_CONCAT(g_pfn,a_FnName) = (decltype(a_FnName) *)GetProcAddress(g_hModKernel32, #a_FnName); \
        } while (0)
    RESOLVE_API("kernel32.dll", GetMaximumProcessorCount);
    //RESOLVE_API("kernel32.dll", GetActiveProcessorCount); - slow :/
    RESOLVE_API("kernel32.dll", GetCurrentProcessorNumber);
    RESOLVE_API("kernel32.dll", GetCurrentProcessorNumberEx);
    RESOLVE_API("kernel32.dll", GetLogicalProcessorInformation);
    RESOLVE_API("kernel32.dll", GetLogicalProcessorInformationEx);

    /*
     * Reset globals.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aidRtMpWinByCpuSetIdx); i++)
        g_aidRtMpWinByCpuSetIdx[i] = NIL_RTCPUID;
    for (unsigned idxGroup = 0; idxGroup < RT_ELEMENTS(g_aRtMpWinCpuGroups); idxGroup++)
    {
        g_aRtMpWinCpuGroups[idxGroup].cMaxCpus    = 0;
        g_aRtMpWinCpuGroups[idxGroup].cActiveCpus = 0;
        for (unsigned idxMember = 0; idxMember < RT_ELEMENTS(g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers); idxMember++)
            g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = -1;
    }

    /*
     * Query group information, partitioning CPU IDs and CPU set indexes.
     *
     * We ASSUME the GroupInfo index is the same as the group number.
     *
     * We CANNOT ASSUME that the kernel CPU indexes are assigned in any given
     * way, though they usually are in group order by active processor.  So,
     * we do that to avoid trouble.  We must use information provided thru GIP
     * if we want the kernel CPU set indexes.  Even there, the inactive CPUs
     * wont have sensible indexes.  Sigh.
     *
     * We try to assign IDs to inactive CPUs in the same manner as mp-r0drv-nt.cpp
     *
     * Note! We will die (AssertFatal) if there are too many processors!
     */
    union
    {
        SYSTEM_INFO                                 SysInfo;
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX     Info;
        uint8_t                                     abPaddingG[  sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)
                                                               + sizeof(PROCESSOR_GROUP_INFO) * 256];
        uint8_t                                     abPaddingC[  sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)
                                                               +   (sizeof(PROCESSOR_RELATIONSHIP) + sizeof(GROUP_AFFINITY))
                                                                 * RTCPUSET_MAX_CPUS];
    } uBuf;
    if (g_pfnGetLogicalProcessorInformationEx)
    {
        /* Query the information. */
        DWORD cbData = sizeof(uBuf);
        AssertFatalMsg(g_pfnGetLogicalProcessorInformationEx(RelationGroup, &uBuf.Info, &cbData) != FALSE,
                       ("last error = %u, cbData = %u (in %u)\n", GetLastError(), cbData, sizeof(uBuf)));
        AssertFatalMsg(uBuf.Info.Relationship == RelationGroup,
                       ("Relationship = %u, expected %u!\n", uBuf.Info.Relationship, RelationGroup));
        AssertFatalMsg(uBuf.Info.Group.MaximumGroupCount <= RT_ELEMENTS(g_aRtMpWinCpuGroups),
                       ("MaximumGroupCount is %u, we only support up to %u!\n",
                        uBuf.Info.Group.MaximumGroupCount, RT_ELEMENTS(g_aRtMpWinCpuGroups)));

        AssertMsg(uBuf.Info.Group.MaximumGroupCount == uBuf.Info.Group.ActiveGroupCount, /* 2nd assumption mentioned above. */
                  ("%u vs %u\n", uBuf.Info.Group.MaximumGroupCount, uBuf.Info.Group.ActiveGroupCount));
        AssertFatal(uBuf.Info.Group.MaximumGroupCount >= uBuf.Info.Group.ActiveGroupCount);

        g_cRtMpWinMaxCpuGroups = uBuf.Info.Group.MaximumGroupCount;

        /* Count max cpus (see mp-r0drv0-nt.cpp) why we don't use GetMaximumProcessorCount(ALL). */
        uint32_t idxGroup;
        g_cRtMpWinMaxCpus = 0;
        for (idxGroup = 0; idxGroup < uBuf.Info.Group.ActiveGroupCount; idxGroup++)
            g_cRtMpWinMaxCpus += uBuf.Info.Group.GroupInfo[idxGroup].MaximumProcessorCount;

        /* Process the active groups. */
        uint32_t cActive   = 0;
        uint32_t cInactive = 0;
        uint32_t idxCpu    = 0;
        uint32_t idxCpuSetNextInactive = g_cRtMpWinMaxCpus - 1;
        for (idxGroup = 0; idxGroup < uBuf.Info.Group.ActiveGroupCount; idxGroup++)
        {
            PROCESSOR_GROUP_INFO const *pGroupInfo = &uBuf.Info.Group.GroupInfo[idxGroup];
            g_aRtMpWinCpuGroups[idxGroup].cMaxCpus    = pGroupInfo->MaximumProcessorCount;
            g_aRtMpWinCpuGroups[idxGroup].cActiveCpus = pGroupInfo->ActiveProcessorCount;
            for (uint32_t idxMember = 0; idxMember < pGroupInfo->MaximumProcessorCount; idxMember++)
            {
                if (pGroupInfo->ActiveProcessorMask & RT_BIT_64(idxMember))
                {
                    g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxCpu;
                    g_aidRtMpWinByCpuSetIdx[idxCpu] = idxCpu;
                    idxCpu++;
                    cActive++;
                }
                else
                {
                    if (idxCpuSetNextInactive >= idxCpu)
                    {
                        g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxCpuSetNextInactive;
                        g_aidRtMpWinByCpuSetIdx[idxCpuSetNextInactive] = idxCpuSetNextInactive;
                        idxCpuSetNextInactive--;
                    }
                    cInactive++;
                }
            }
        }
        g_cRtMpWinActiveCpus = cActive;
        Assert(cActive + cInactive <= g_cRtMpWinMaxCpus);
        Assert(idxCpu <= idxCpuSetNextInactive + 1);
        Assert(idxCpu <= g_cRtMpWinMaxCpus);

        /* Just in case the 2nd assumption doesn't hold true and there are inactive groups. */
        for (; idxGroup < uBuf.Info.Group.MaximumGroupCount; idxGroup++)
        {
            DWORD cMaxMembers = g_pfnGetMaximumProcessorCount(idxGroup);
            g_aRtMpWinCpuGroups[idxGroup].cMaxCpus    = cMaxMembers;
            g_aRtMpWinCpuGroups[idxGroup].cActiveCpus = 0;
            for (uint32_t idxMember = 0; idxMember < cMaxMembers; idxMember++)
            {
                if (idxCpuSetNextInactive >= idxCpu)
                {
                    g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxCpuSetNextInactive;
                    g_aidRtMpWinByCpuSetIdx[idxCpuSetNextInactive] = idxCpuSetNextInactive;
                    idxCpuSetNextInactive--;
                }
                cInactive++;
            }
        }
        Assert(cActive + cInactive <= g_cRtMpWinMaxCpus);
        Assert(idxCpu <= idxCpuSetNextInactive + 1);
    }
    else
    {
        /* Legacy: */
        GetSystemInfo(&uBuf.SysInfo);
        g_cRtMpWinMaxCpuGroups              = 1;
        g_cRtMpWinMaxCpus                   = uBuf.SysInfo.dwNumberOfProcessors;
        g_aRtMpWinCpuGroups[0].cMaxCpus     = uBuf.SysInfo.dwNumberOfProcessors;
        g_aRtMpWinCpuGroups[0].cActiveCpus  = uBuf.SysInfo.dwNumberOfProcessors;

        for (uint32_t idxMember = 0; idxMember < uBuf.SysInfo.dwNumberOfProcessors; idxMember++)
        {
            g_aRtMpWinCpuGroups[0].aidxCpuSetMembers[idxMember] = idxMember;
            g_aidRtMpWinByCpuSetIdx[idxMember] = idxMember;
        }
    }

    AssertFatalMsg(g_cRtMpWinMaxCpus <= RTCPUSET_MAX_CPUS,
                   ("g_cRtMpWinMaxCpus=%u (%#x); RTCPUSET_MAX_CPUS=%u\n", g_cRtMpWinMaxCpus, g_cRtMpWinMaxCpus, RTCPUSET_MAX_CPUS));

    g_cbRtMpWinGrpRelBuf = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)
                         + (g_cRtMpWinMaxCpuGroups + 2) * sizeof(PROCESSOR_GROUP_INFO);

    /*
     * Get information about cores.
     *
     * Note! This will only give us info about active processors according to
     *       MSDN, we'll just have to hope that CPUs aren't hotplugged after we
     *       initialize here (or that the API consumers doesn't care too much).
     */
    /** @todo A hot CPU plug event would be nice. */
    g_cRtMpWinMaxCpuCores = g_cRtMpWinMaxCpus;
    if (g_pfnGetLogicalProcessorInformationEx)
    {
        /* Query the information. */
        DWORD cbData = sizeof(uBuf);
        AssertFatalMsg(g_pfnGetLogicalProcessorInformationEx(RelationProcessorCore, &uBuf.Info, &cbData) != FALSE,
                       ("last error = %u, cbData = %u (in %u)\n", GetLastError(), cbData, sizeof(uBuf)));
        g_cRtMpWinMaxCpuCores = 0;
        for (uint32_t off = 0; off < cbData; )
        {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *pCur = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)&uBuf.abPaddingG[off];
            AssertFatalMsg(pCur->Relationship == RelationProcessorCore,
                           ("off = %#x, Relationship = %u, expected %u!\n", off, pCur->Relationship, RelationProcessorCore));
            g_cRtMpWinMaxCpuCores++;
            off += pCur->Size;
        }

#if ARCH_BITS == 32
        if (g_cRtMpWinMaxCpuCores > g_cRtMpWinMaxCpus)
        {
            /** @todo WOW64 trouble where the emulation environment has folded the high
             *        processor masks (63..32) into the low (31..0), hiding some
             *        processors from us.  Currently we don't deal with that. */
            g_cRtMpWinMaxCpuCores = g_cRtMpWinMaxCpus;
        }
        else
            AssertStmt(g_cRtMpWinMaxCpuCores > 0, g_cRtMpWinMaxCpuCores = g_cRtMpWinMaxCpus);
#else
        AssertStmt(g_cRtMpWinMaxCpuCores > 0 && g_cRtMpWinMaxCpuCores <= g_cRtMpWinMaxCpus,
                   g_cRtMpWinMaxCpuCores = g_cRtMpWinMaxCpus);
#endif
    }
    else
    {
        /*
         * Sadly, on XP and Server 2003, even if the API is present, it does not tell us
         * how many physical cores there are (any package will look like a single core).
         * That is worse than not using the API at all, so just skip it unless it's Vista+.
         */
        if (   g_pfnGetLogicalProcessorInformation
            && g_WinOsInfoEx.dwPlatformId == VER_PLATFORM_WIN32_NT
            && g_WinOsInfoEx.dwMajorVersion >= 6)
        {
            /* Query the info. */
            DWORD                                   cbSysProcInfo = _4K;
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION   paSysInfo = NULL;
            BOOL                                    fRc = FALSE;
            do
            {
                cbSysProcInfo = RT_ALIGN_32(cbSysProcInfo, 256);
                void *pv = RTMemRealloc(paSysInfo, cbSysProcInfo);
                if (!pv)
                    break;
                paSysInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)pv;
                fRc = g_pfnGetLogicalProcessorInformation(paSysInfo, &cbSysProcInfo);
            } while (!fRc && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
            if (fRc)
            {
                /* Count the cores in the result. */
                g_cRtMpWinMaxCpuCores = 0;
                uint32_t i = cbSysProcInfo / sizeof(paSysInfo[0]);
                while (i-- > 0)
                    if (paSysInfo[i].Relationship == RelationProcessorCore)
                        g_cRtMpWinMaxCpuCores++;

                AssertStmt(g_cRtMpWinMaxCpuCores > 0 && g_cRtMpWinMaxCpuCores <= g_cRtMpWinMaxCpus,
                           g_cRtMpWinMaxCpuCores = g_cRtMpWinMaxCpus);
            }
            RTMemFree(paSysInfo);
        }
    }

    return VINF_SUCCESS;
}


#ifdef IPRT_WITH_GIP_MP_INFO
/**
 * @callback_method_impl{FNRTONCE, Updates globals with information from GIP.}
 */
static DECLCALLBACK(int32_t) rtMpWinInitOnceGip(void *pvUser)
{
    RT_NOREF(pvUser);
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);

    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (   pGip
        && pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC)
    {
        /*
         * Update globals.
         */
        if (g_cRtMpWinMaxCpus != pGip->cPossibleCpus)
            g_cRtMpWinMaxCpus = pGip->cPossibleCpus;
        if (g_cRtMpWinActiveCpus != pGip->cOnlineCpus)
            g_cRtMpWinActiveCpus = pGip->cOnlineCpus;
        Assert(g_cRtMpWinMaxCpuGroups == pGip->cPossibleCpuGroups);
        if (g_cRtMpWinMaxCpuGroups != pGip->cPossibleCpuGroups)
        {
            g_cRtMpWinMaxCpuGroups = pGip->cPossibleCpuGroups;
            g_cbRtMpWinGrpRelBuf   = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)
                                   + (g_cRtMpWinMaxCpuGroups + 2) * sizeof(PROCESSOR_GROUP_INFO);
        }

        /*
         * Update CPU set IDs.
         */
        for (unsigned i = g_cRtMpWinMaxCpus; i < RT_ELEMENTS(g_aidRtMpWinByCpuSetIdx); i++)
            g_aidRtMpWinByCpuSetIdx[i] = NIL_RTCPUID;

        unsigned const cbGip = pGip->cPages * PAGE_SIZE;
        for (uint32_t idxGroup = 0; idxGroup < g_cRtMpWinMaxCpuGroups; idxGroup++)
        {
            uint32_t idxMember;
            uint32_t offCpuGroup = pGip->aoffCpuGroup[idxGroup];
            if (offCpuGroup < cbGip)
            {
                PSUPGIPCPUGROUP pGipCpuGrp  = (PSUPGIPCPUGROUP)((uintptr_t)pGip + offCpuGroup);
                uint32_t        cMaxMembers = pGipCpuGrp->cMaxMembers;
                AssertStmt(cMaxMembers <= RT_ELEMENTS(g_aRtMpWinCpuGroups[0].aidxCpuSetMembers),
                           cMaxMembers  = RT_ELEMENTS(g_aRtMpWinCpuGroups[0].aidxCpuSetMembers));
                g_aRtMpWinCpuGroups[idxGroup].cMaxCpus     = cMaxMembers;
                g_aRtMpWinCpuGroups[idxGroup].cActiveCpus  = RT_MIN(pGipCpuGrp->cMembers, cMaxMembers);

                for (idxMember = 0; idxMember < cMaxMembers; idxMember++)
                {
                    int16_t idxSet = pGipCpuGrp->aiCpuSetIdxs[idxMember];
                    g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxSet;
                    if ((unsigned)idxSet < RT_ELEMENTS(g_aidRtMpWinByCpuSetIdx))
# ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
                        g_aidRtMpWinByCpuSetIdx[idxSet] = RTMPCPUID_FROM_GROUP_AND_NUMBER(idxGroup, idxMember);
# else
                        g_aidRtMpWinByCpuSetIdx[idxSet] = idxSet;
# endif
                }
            }
            else
                idxMember = 0;
            for (; idxMember < RT_ELEMENTS(g_aRtMpWinCpuGroups[0].aidxCpuSetMembers); idxMember++)
                g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = -1;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Refreshes globals from GIP after one or more CPUs were added.
 *
 * There are potential races here.  We might race other threads and we may race
 * more CPUs being added.
 */
static void rtMpWinRefreshGip(void)
{
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (   pGip
        && pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC)
    {
        /*
         * Since CPUs cannot be removed, we only have to update the IDs and
         * indexes of CPUs that we think are inactive and the group member counts.
         */
        for (;;)
        {
            unsigned const cbGip          = pGip->cPages * PAGE_SIZE;
            uint32_t const cGipActiveCpus = pGip->cOnlineCpus;
            uint32_t const cMyActiveCpus  = ASMAtomicReadU32(&g_cRtMpWinActiveCpus);
            ASMCompilerBarrier();

            for (uint32_t idxGroup = 0; idxGroup < g_cRtMpWinMaxCpuGroups; idxGroup++)
            {
                uint32_t offCpuGroup = pGip->aoffCpuGroup[idxGroup];
                if (offCpuGroup < cbGip)
                {
                    PSUPGIPCPUGROUP pGipCpuGrp  = (PSUPGIPCPUGROUP)((uintptr_t)pGip + offCpuGroup);
                    uint32_t        cMaxMembers = pGipCpuGrp->cMaxMembers;
                    AssertStmt(cMaxMembers <= RT_ELEMENTS(g_aRtMpWinCpuGroups[0].aidxCpuSetMembers),
                               cMaxMembers  = RT_ELEMENTS(g_aRtMpWinCpuGroups[0].aidxCpuSetMembers));
                    for (uint32_t idxMember = g_aRtMpWinCpuGroups[idxGroup].cActiveCpus; idxMember < cMaxMembers; idxMember++)
                    {
                        int16_t idxSet = pGipCpuGrp->aiCpuSetIdxs[idxMember];
                        g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxSet;
                        if ((unsigned)idxSet < RT_ELEMENTS(g_aidRtMpWinByCpuSetIdx))
# ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
                            g_aidRtMpWinByCpuSetIdx[idxSet] = RTMPCPUID_FROM_GROUP_AND_NUMBER(idxGroup, idxMember);
# else
                            g_aidRtMpWinByCpuSetIdx[idxSet] = idxSet;
# endif
                    }
                    g_aRtMpWinCpuGroups[idxGroup].cMaxCpus    = RT_MIN(pGipCpuGrp->cMembers, cMaxMembers);
                    g_aRtMpWinCpuGroups[idxGroup].cActiveCpus = RT_MIN(pGipCpuGrp->cMembers, cMaxMembers);
                }
                else
                    Assert(g_aRtMpWinCpuGroups[idxGroup].cActiveCpus == 0);
            }

            ASMCompilerBarrier();
            if (cGipActiveCpus == pGip->cOnlineCpus)
                if (ASMAtomicCmpXchgU32(&g_cRtMpWinActiveCpus, cGipActiveCpus, cMyActiveCpus))
                    break;
        }
    }
}

#endif /* IPRT_WITH_GIP_MP_INFO */


/*
 * Conversion between CPU ID and set index.
 */

RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    if (idCpu != NIL_RTCPUID)
        return RTMpSetIndexFromCpuGroupMember(rtMpCpuIdGetGroup(idCpu), rtMpCpuIdGetGroupMember(idCpu));
    return -1;

#else
    /* 1:1 mapping, just do range checking. */
    return idCpu < g_cRtMpWinMaxCpus ? idCpu : -1;
#endif
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    if ((unsigned)iCpu < RT_ELEMENTS(g_aidRtMpWinByCpuSetIdx))
    {
        RTCPUID idCpu = g_aidRtMpWinByCpuSetIdx[iCpu];

#if defined(IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER) && defined(RT_STRICT)
        /* Check the correctness of the mapping table. */
        RTCPUID idCpuGip = NIL_RTCPUID;
        if (   pGip
            && (unsigned)iCpu < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx))
        {
            unsigned idxSupCpu = pGip->aiCpuFromCpuSetIdx[idxGuess];
            if (idxSupCpu < pGip->cCpus)
                if (pGip->aCPUs[idxSupCpu].enmState != SUPGIPCPUSTATE_INVALID)
                    idCpuGip = pGip->aCPUs[idxSupCpu].idCpu;
        }
        AssertMsg(idCpu == idCpuGip, ("table:%#x  gip:%#x\n", idCpu, idCpuGip));
#endif

        return idCpu;
    }
    return NIL_RTCPUID;
}


RTDECL(int) RTMpSetIndexFromCpuGroupMember(uint32_t idxGroup, uint32_t idxMember)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    if (idxGroup < g_cRtMpWinMaxCpuGroups)
        if (idxMember < g_aRtMpWinCpuGroups[idxGroup].cMaxCpus)
            return g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember];
    return -1;
}


RTDECL(uint32_t) RTMpGetCpuGroupCounts(uint32_t idxGroup, uint32_t *pcActive)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    if (idxGroup < g_cRtMpWinMaxCpuGroups)
    {
        if (pcActive)
            *pcActive = g_aRtMpWinCpuGroups[idxGroup].cActiveCpus;
        return g_aRtMpWinCpuGroups[idxGroup].cMaxCpus;
    }
    if (pcActive)
        *pcActive = 0;
    return 0;
}


RTDECL(uint32_t) RTMpGetMaxCpuGroupCount(void)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    return g_cRtMpWinMaxCpuGroups;
}



/*
 * Get current CPU.
 */

RTDECL(RTCPUID) RTMpCpuId(void)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    PROCESSOR_NUMBER ProcNum;
    ProcNum.Group = 0;
    ProcNum.Number = 0xff;
    if (g_pfnGetCurrentProcessorNumberEx)
        g_pfnGetCurrentProcessorNumberEx(&ProcNum);
    else if (g_pfnGetCurrentProcessorNumber)
    {
        DWORD iCpu = g_pfnGetCurrentProcessorNumber();
        Assert(iCpu < g_cRtMpWinMaxCpus);
        ProcNum.Number = iCpu;
    }
    else
    {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        ProcNum.Number = ASMGetApicId();
#else
# error "Not ported to this architecture."
        return NIL_RTCPUID;
#endif
    }

#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    return RTMPCPUID_FROM_GROUP_AND_NUMBER(ProcNum.Group, ProcNum.Number);
#else
    return RTMpSetIndexFromCpuGroupMember(ProcNum.Group, ProcNum.Number);
#endif
}


/*
 * Possible CPUs and cores.
 */

RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    return RTMPCPUID_FROM_GROUP_AND_NUMBER(g_cRtMpWinMaxCpuGroups - 1,
                                           g_aRtMpWinCpuGroups[g_cRtMpWinMaxCpuGroups - 1].cMaxCpus - 1);
#else
    return g_cRtMpWinMaxCpus - 1;
#endif
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    /* Any CPU between 0 and g_cRtMpWinMaxCpus are possible. */
    return idCpu < g_cRtMpWinMaxCpus;
}


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    RTCPUID iCpu = RTMpGetCount();
    RTCpuSetEmpty(pSet);
    while (iCpu-- > 0)
        RTCpuSetAddByIndex(pSet, iCpu);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    return g_cRtMpWinMaxCpus;
}


RTDECL(RTCPUID) RTMpGetCoreCount(void)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);
    RTMPWIN_UPDATE_GIP_GLOBALS();

    return g_cRtMpWinMaxCpuCores;
}


/*
 * Online CPUs and cores.
 */

RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    RTOnce(&g_MpInitOnce, rtMpWinInitOnce, NULL);

#ifdef IPRT_WITH_GIP_MP_INFO
    RTMPWIN_UPDATE_GIP_GLOBALS_AND_GET_PGIP();
    if (pGip)
    {
        *pSet = pGip->OnlineCpuSet;
        return pSet;
    }
#endif

    if (g_pfnGetLogicalProcessorInformationEx)
    {
        /*
         * Get the group relation info.
         *
         * In addition to the ASSUMPTIONS that are documented in rtMpWinInitOnce,
         * we ASSUME that PROCESSOR_GROUP_INFO::MaximumProcessorCount gives the
         * active processor mask width.
         */
        /** @todo this is not correct for WOW64   */
        DWORD                                    cbInfo = g_cbRtMpWinGrpRelBuf;
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *pInfo = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)alloca(cbInfo);
        AssertFatalMsg(g_pfnGetLogicalProcessorInformationEx(RelationGroup, pInfo, &cbInfo) != FALSE,
                       ("last error = %u, cbInfo = %u (in %u)\n", GetLastError(), cbInfo, g_cbRtMpWinGrpRelBuf));
        AssertFatalMsg(pInfo->Relationship == RelationGroup,
                       ("Relationship = %u, expected %u!\n", pInfo->Relationship, RelationGroup));
        AssertFatalMsg(pInfo->Group.MaximumGroupCount == g_cRtMpWinMaxCpuGroups,
                       ("MaximumGroupCount is %u, expected %u!\n", pInfo->Group.MaximumGroupCount, g_cRtMpWinMaxCpuGroups));

        RTCpuSetEmpty(pSet);
        for (uint32_t idxGroup = 0; idxGroup < pInfo->Group.MaximumGroupCount; idxGroup++)
        {
            Assert(pInfo->Group.GroupInfo[idxGroup].MaximumProcessorCount == g_aRtMpWinCpuGroups[idxGroup].cMaxCpus);
            Assert(pInfo->Group.GroupInfo[idxGroup].ActiveProcessorCount  <= g_aRtMpWinCpuGroups[idxGroup].cMaxCpus);

            KAFFINITY fActive = pInfo->Group.GroupInfo[idxGroup].ActiveProcessorMask;
            if (fActive != 0)
            {
#ifdef RT_STRICT
                uint32_t    cMembersLeft = pInfo->Group.GroupInfo[idxGroup].ActiveProcessorCount;
#endif
                int const   cMembers  = g_aRtMpWinCpuGroups[idxGroup].cMaxCpus;
                for (int idxMember = 0; idxMember < cMembers; idxMember++)
                {
                    if (fActive & 1)
                    {
#ifdef RT_STRICT
                        cMembersLeft--;
#endif
                        RTCpuSetAddByIndex(pSet, g_aRtMpWinCpuGroups[idxGroup].aidxCpuSetMembers[idxMember]);
                        fActive >>= 1;
                        if (!fActive)
                            break;
                    }
                    else
                    {
                        fActive >>= 1;
                    }
                }
                Assert(cMembersLeft == 0);
            }
            else
                Assert(pInfo->Group.GroupInfo[idxGroup].ActiveProcessorCount == 0);
        }

        return pSet;
    }

    /*
     * Legacy fallback code.
     */
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    return RTCpuSetFromU64(pSet, SysInfo.dwActiveProcessorMask);
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    RTCPUSET Set;
    return RTCpuSetIsMember(RTMpGetOnlineSet(&Set), idCpu);
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
#ifdef IPRT_WITH_GIP_MP_INFO
    RTMPWIN_UPDATE_GIP_GLOBALS_AND_GET_PGIP();
    if (pGip)
        return pGip->cOnlineCpus;
#endif

    RTCPUSET Set;
    RTMpGetOnlineSet(&Set);
    return RTCpuSetCount(&Set);
}


RTDECL(RTCPUID) RTMpGetOnlineCoreCount(void)
{
    /** @todo this isn't entirely correct, but whatever. */
    return RTMpGetCoreCount();
}

