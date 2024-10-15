/* $Id: timesup.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time using SUPLib.
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
#define LOG_GROUP RTLOGGROUP_TIME
#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
# include <iprt/asm.h>
# include <iprt/asm-amd64-x86.h>
# include <iprt/x86.h>
# include <VBox/sup.h>
#endif
#include "internal/time.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
static DECLCALLBACK(void)     rtTimeNanoTSInternalBitch(PRTTIMENANOTSDATA pData, uint64_t u64NanoTS, uint64_t u64DeltaPrev, uint64_t u64PrevNanoTS);
static DECLCALLBACK(uint64_t) rtTimeNanoTSInternalFallback(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra);
static DECLCALLBACK(uint64_t) rtTimeNanoTSInternalRediscover(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra);
static DECLCALLBACK(uint64_t) rtTimeNanoTSInternalBadCpuIndex(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra,
                                                              uint16_t idApic, uint16_t iCpuSet, uint16_t iGipCpu);
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
/** The previous timestamp value returned by RTTimeNanoTS. */
static uint64_t         g_TimeNanoTSPrev = 0;

/** The RTTimeNanoTS data structure that's passed down to the worker functions.  */
static RTTIMENANOTSDATA g_TimeNanoTSData =
{
    /* .pu64Prev       = */ &g_TimeNanoTSPrev,
    /* .pfnBad         = */ rtTimeNanoTSInternalBitch,
    /* .pfnRediscover  = */ rtTimeNanoTSInternalRediscover,
    /* .pfnBadCpuIndex = */ rtTimeNanoTSInternalBadCpuIndex,
    /* .c1nsSteps      = */ 0,
    /* .cExpired       = */ 0,
    /* .cBadPrev       = */ 0,
    /* .cUpdateRaces   = */ 0
};

# ifdef IN_RC
/** Array of rtTimeNanoTSInternal worker functions.
 * This array is indexed by g_iWorker. */
static const PFNTIMENANOTSINTERNAL g_apfnWorkers[] =
{
#  define RTTIMENANO_WORKER_DETECT                                      0
    rtTimeNanoTSInternalRediscover,

#  define RTTIMENANO_WORKER_LEGACY_SYNC_INVAR_NO_DELTA                  1
    RTTimeNanoTSLegacySyncInvarNoDelta,
#  define RTTIMENANO_WORKER_LEGACY_SYNC_INVAR_WITH_DELTA                2
    RTTimeNanoTSLegacySyncInvarWithDelta,
#  define RTTIMENANO_WORKER_LEGACY_ASYNC                                3
    RTTimeNanoTSLegacyAsync,

#  define RTTIMENANO_WORKER_LFENCE_SYNC_INVAR_NO_DELTA                  4
    RTTimeNanoTSLFenceSyncInvarNoDelta,
#  define RTTIMENANO_WORKER_LFENCE_SYNC_INVAR_WITH_DELTA                5
    RTTimeNanoTSLFenceSyncInvarWithDelta,
#  define RTTIMENANO_WORKER_LFENCE_ASYNC                                6
    RTTimeNanoTSLFenceAsync,

#  define RTTIMENANO_WORKER_FALLBACK                                    7
    rtTimeNanoTSInternalFallback,
};
/** The index into g_apfnWorkers for the function to use.
 * @remarks This cannot be a pointer because that'll break down in RC due to
 *          code relocation. */
static uint32_t                 g_iWorker   = RTTIMENANO_WORKER_DETECT;
# else
/** Pointer to the worker */
static PFNTIMENANOTSINTERNAL    g_pfnWorker = rtTimeNanoTSInternalRediscover;
# endif /* IN_RC */


/**
 * @interface_method_impl{RTTIMENANOTSDATA,pfnBad}
 */
static DECLCALLBACK(void) rtTimeNanoTSInternalBitch(PRTTIMENANOTSDATA pData, uint64_t u64NanoTS, uint64_t u64DeltaPrev,
                                                    uint64_t u64PrevNanoTS)
{
    pData->cBadPrev++;
    if ((int64_t)u64DeltaPrev < 0)
        LogRel(("TM: u64DeltaPrev=%RI64 u64PrevNanoTS=0x%016RX64 u64NanoTS=0x%016RX64\n",
                u64DeltaPrev, u64PrevNanoTS, u64NanoTS));
    else
        Log(("TM: u64DeltaPrev=%RI64 u64PrevNanoTS=0x%016RX64 u64NanoTS=0x%016RX64 (debugging?)\n",
             u64DeltaPrev, u64PrevNanoTS, u64NanoTS));
}

/**
 * @interface_method_impl{RTTIMENANOTSDATA,pfnBadCpuIndex}
 */
static DECLCALLBACK(uint64_t) rtTimeNanoTSInternalBadCpuIndex(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra,
                                                              uint16_t idApic, uint16_t iCpuSet, uint16_t iGipCpu)
{
    RT_NOREF_PV(pData); RT_NOREF_PV(idApic); RT_NOREF_PV(iCpuSet); RT_NOREF_PV(iGipCpu);
# ifndef IN_RC
    AssertMsgFailed(("idApic=%#x iCpuSet=%#x iGipCpu=%#x\n", idApic, iCpuSet, iGipCpu));
    if (pExtra)
        pExtra->uTSCValue = ASMReadTSC();
    return RTTimeSystemNanoTS();
# else
    RTAssertReleasePanic();
    return 0;
# endif
}


/**
 * Fallback function.
 */
static DECLCALLBACK(uint64_t) rtTimeNanoTSInternalFallback(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra)
{
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (    pGip
        &&  pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC
        &&  (   pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC
             || pGip->u32Mode == SUPGIPMODE_SYNC_TSC
             || pGip->u32Mode == SUPGIPMODE_ASYNC_TSC))
        return rtTimeNanoTSInternalRediscover(pData, pExtra);
    NOREF(pData);
# ifndef IN_RC
    if (pExtra)
        pExtra->uTSCValue = ASMReadTSC();
    return RTTimeSystemNanoTS();
# else
    RTAssertReleasePanic();
    return 0;
# endif
}


/**
 * Called the first time somebody asks for the time or when the GIP
 * is mapped/unmapped.
 */
static DECLCALLBACK(uint64_t) rtTimeNanoTSInternalRediscover(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra)
{
    PSUPGLOBALINFOPAGE      pGip = g_pSUPGlobalInfoPage;
# ifdef IN_RC
    uint32_t                iWorker;
# else
    PFNTIMENANOTSINTERNAL   pfnWorker;
# endif
    if (    pGip
        &&  pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC
        &&  (   pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC
             || pGip->u32Mode == SUPGIPMODE_SYNC_TSC
             || pGip->u32Mode == SUPGIPMODE_ASYNC_TSC))
    {
        if (ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_SSE2)
        {
# ifdef IN_RC
            iWorker   = pGip->u32Mode == SUPGIPMODE_ASYNC_TSC
                      ? RTTIMENANO_WORKER_LFENCE_ASYNC
                      : pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO
                      ? RTTIMENANO_WORKER_LFENCE_SYNC_INVAR_NO_DELTA
                      : RTTIMENANO_WORKER_LFENCE_SYNC_INVAR_WITH_DELTA;
# elif defined(IN_RING0)
            pfnWorker = pGip->u32Mode == SUPGIPMODE_ASYNC_TSC
                      ? RTTimeNanoTSLFenceAsync
                      : pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO
                      ? RTTimeNanoTSLFenceSyncInvarNoDelta
                      : RTTimeNanoTSLFenceSyncInvarWithDelta;
# else
            if (pGip->u32Mode == SUPGIPMODE_ASYNC_TSC)
            {
                if (     pGip->fGetGipCpu & SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS)
                    pfnWorker = RTTimeNanoTSLFenceAsyncUseIdtrLim;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS)
                    pfnWorker = RTTimeNanoTSLFenceAsyncUseRdtscp;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL)
                    pfnWorker = RTTimeNanoTSLFenceAsyncUseRdtscpGroupChNumCl;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_0B)
                    pfnWorker = RTTimeNanoTSLFenceAsyncUseApicIdExt0B;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_8000001E)
                    pfnWorker = RTTimeNanoTSLFenceAsyncUseApicIdExt8000001E;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID)
                    pfnWorker = RTTimeNanoTSLFenceAsyncUseApicId;
                else
                    pfnWorker = rtTimeNanoTSInternalFallback;
            }
            else
            {
                if (pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_PRACTICALLY_ZERO)
                    pfnWorker = RTTimeNanoTSLFenceSyncInvarNoDelta;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS)
                    pfnWorker = RTTimeNanoTSLFenceSyncInvarWithDeltaUseIdtrLim;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS)
                    pfnWorker = RTTimeNanoTSLFenceSyncInvarWithDeltaUseRdtscp;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_0B)
                    pfnWorker = RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt0B;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_8000001E)
                    pfnWorker = RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt8000001E;
                else if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID)
                    pfnWorker = RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicId;
                else
                    pfnWorker = rtTimeNanoTSInternalFallback;
            }
# endif
        }
        else
        {
# ifdef IN_RC
            iWorker = pGip->u32Mode == SUPGIPMODE_ASYNC_TSC
                    ? RTTIMENANO_WORKER_LEGACY_ASYNC
                    : pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO
                    ? RTTIMENANO_WORKER_LEGACY_SYNC_INVAR_NO_DELTA : RTTIMENANO_WORKER_LEGACY_SYNC_INVAR_WITH_DELTA;
# elif defined(IN_RING0)
            pfnWorker = pGip->u32Mode == SUPGIPMODE_ASYNC_TSC
                      ? RTTimeNanoTSLegacyAsync
                      : pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO
                      ? RTTimeNanoTSLegacySyncInvarNoDelta
                      : RTTimeNanoTSLegacySyncInvarWithDelta;
# else
            if (pGip->u32Mode == SUPGIPMODE_ASYNC_TSC)
                pfnWorker = pGip->fGetGipCpu & SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS
                          ? RTTimeNanoTSLegacyAsyncUseRdtscp
                          : pGip->fGetGipCpu & SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL
                          ? RTTimeNanoTSLegacyAsyncUseRdtscpGroupChNumCl
                          : pGip->fGetGipCpu & SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
                          ? RTTimeNanoTSLegacyAsyncUseIdtrLim
                          : pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_0B
                          ? RTTimeNanoTSLegacyAsyncUseApicIdExt0B
                          : pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_8000001E
                          ? RTTimeNanoTSLegacyAsyncUseApicIdExt8000001E
                          : pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID
                          ? RTTimeNanoTSLegacyAsyncUseApicId
                          : rtTimeNanoTSInternalFallback;
           else
               pfnWorker = pGip->fGetGipCpu & SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS
                         ?   pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_PRACTICALLY_ZERO
                           ? RTTimeNanoTSLegacySyncInvarNoDelta
                           : RTTimeNanoTSLegacySyncInvarWithDeltaUseRdtscp
                         : pGip->fGetGipCpu & SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
                         ? pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_PRACTICALLY_ZERO
                           ? RTTimeNanoTSLegacySyncInvarNoDelta
                           : RTTimeNanoTSLegacySyncInvarWithDeltaUseIdtrLim
                         : pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_0B
                         ? pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO
                           ? RTTimeNanoTSLegacySyncInvarNoDelta
                           : RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt0B
                         : pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_8000001E
                         ? pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO
                           ? RTTimeNanoTSLegacySyncInvarNoDelta
                           : RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt8000001E
                         : pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID
                         ? pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO
                           ? RTTimeNanoTSLegacySyncInvarNoDelta
                           : RTTimeNanoTSLegacySyncInvarWithDeltaUseApicId
                         : rtTimeNanoTSInternalFallback;
# endif
        }
    }
    else
# ifdef IN_RC
        iWorker = RTTIMENANO_WORKER_FALLBACK;
# else
        pfnWorker = rtTimeNanoTSInternalFallback;
# endif

# ifdef IN_RC
    ASMAtomicWriteU32((uint32_t volatile *)&g_iWorker, iWorker);
    return g_apfnWorkers[iWorker](pData, pExtra);
# else
    ASMAtomicWritePtr((void * volatile *)&g_pfnWorker, (void *)(uintptr_t)pfnWorker);
    return pfnWorker(pData, pExtra);
# endif
}

# if defined(IN_RING3) || defined(IN_RING0)
RTDECL(const char *) RTTimeNanoTSWorkerName(void)
{
    static const struct { PFNTIMENANOTSINTERNAL pfnWorker; const char *pszName; } s_aWorkersAndNames[] =
    {
#  define ENTRY(a_fn) { a_fn, #a_fn }
        ENTRY(RTTimeNanoTSLegacySyncInvarNoDelta),
        ENTRY(RTTimeNanoTSLFenceSyncInvarNoDelta),
#  ifdef IN_RING3
        ENTRY(RTTimeNanoTSLegacyAsyncUseApicId),
        ENTRY(RTTimeNanoTSLegacyAsyncUseApicIdExt0B),
        ENTRY(RTTimeNanoTSLegacyAsyncUseApicIdExt8000001E),
        ENTRY(RTTimeNanoTSLegacyAsyncUseRdtscp),
        ENTRY(RTTimeNanoTSLegacyAsyncUseRdtscpGroupChNumCl),
        ENTRY(RTTimeNanoTSLegacyAsyncUseIdtrLim),
        ENTRY(RTTimeNanoTSLegacySyncInvarWithDeltaUseApicId),
        ENTRY(RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt0B),
        ENTRY(RTTimeNanoTSLegacySyncInvarWithDeltaUseApicIdExt8000001E),
        ENTRY(RTTimeNanoTSLegacySyncInvarWithDeltaUseRdtscp),
        ENTRY(RTTimeNanoTSLegacySyncInvarWithDeltaUseIdtrLim),
        ENTRY(RTTimeNanoTSLFenceAsyncUseApicId),
        ENTRY(RTTimeNanoTSLFenceAsyncUseApicIdExt0B),
        ENTRY(RTTimeNanoTSLFenceAsyncUseApicIdExt8000001E),
        ENTRY(RTTimeNanoTSLFenceAsyncUseRdtscp),
        ENTRY(RTTimeNanoTSLFenceAsyncUseRdtscpGroupChNumCl),
        ENTRY(RTTimeNanoTSLFenceAsyncUseIdtrLim),
        ENTRY(RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicId),
        ENTRY(RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt0B),
        ENTRY(RTTimeNanoTSLFenceSyncInvarWithDeltaUseApicIdExt8000001E),
        ENTRY(RTTimeNanoTSLFenceSyncInvarWithDeltaUseRdtscp),
        ENTRY(RTTimeNanoTSLFenceSyncInvarWithDeltaUseIdtrLim),
#  else
        ENTRY(RTTimeNanoTSLegacyAsync),
        ENTRY(RTTimeNanoTSLegacySyncInvarWithDelta),
        ENTRY(RTTimeNanoTSLFenceAsync),
        ENTRY(RTTimeNanoTSLFenceSyncInvarWithDelta),
#  endif
        ENTRY(rtTimeNanoTSInternalFallback),
#  undef ENTRY
    };
    PFNTIMENANOTSINTERNAL pfnWorker = g_pfnWorker;
    if (pfnWorker == rtTimeNanoTSInternalRediscover)
    {
        RTTimeNanoTS();
        pfnWorker = g_pfnWorker;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(s_aWorkersAndNames); i++)
        if (s_aWorkersAndNames[i].pfnWorker == pfnWorker)
            return s_aWorkersAndNames[i].pszName;
    AssertFailed();
    return NULL;
}
# endif /* IN_RING3 || IN_RING0 */

#endif /* !IN_GUEST && !RT_NO_GIP */


/**
 * Internal worker for getting the current nanosecond timestamp.
 */
DECLINLINE(uint64_t) rtTimeNanoTSInternal(void)
{
#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
# ifdef IN_RC
    return g_apfnWorkers[g_iWorker](&g_TimeNanoTSData, NULL /*pExtra*/);
# else
    return g_pfnWorker(&g_TimeNanoTSData, NULL /*pExtra*/);
# endif
#else
    return RTTimeSystemNanoTS();
#endif
}


/**
 * Gets the current nanosecond timestamp.
 *
 * @returns nanosecond timestamp.
 */
RTDECL(uint64_t) RTTimeNanoTS(void)
{
    return rtTimeNanoTSInternal();
}
RT_EXPORT_SYMBOL(RTTimeNanoTS);


/**
 * Gets the current millisecond timestamp.
 *
 * @returns millisecond timestamp.
 */
RTDECL(uint64_t) RTTimeMilliTS(void)
{
    return rtTimeNanoTSInternal() / 1000000;
}
RT_EXPORT_SYMBOL(RTTimeMilliTS);


#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
/**
 * Debugging the time api.
 *
 * @returns the number of 1ns steps which has been applied by RTTimeNanoTS().
 */
RTDECL(uint32_t) RTTimeDbgSteps(void)
{
    return g_TimeNanoTSData.c1nsSteps;
}
RT_EXPORT_SYMBOL(RTTimeDbgSteps);


/**
 * Debugging the time api.
 *
 * @returns the number of times the TSC interval expired RTTimeNanoTS().
 */
RTDECL(uint32_t) RTTimeDbgExpired(void)
{
    return g_TimeNanoTSData.cExpired;
}
RT_EXPORT_SYMBOL(RTTimeDbgExpired);


/**
 * Debugging the time api.
 *
 * @returns the number of bad previous values encountered by RTTimeNanoTS().
 */
RTDECL(uint32_t) RTTimeDbgBad(void)
{
    return g_TimeNanoTSData.cBadPrev;
}
RT_EXPORT_SYMBOL(RTTimeDbgBad);


/**
 * Debugging the time api.
 *
 * @returns the number of update races in RTTimeNanoTS().
 */
RTDECL(uint32_t) RTTimeDbgRaces(void)
{
    return g_TimeNanoTSData.cUpdateRaces;
}
RT_EXPORT_SYMBOL(RTTimeDbgRaces);
#endif /* !IN_GUEST && !RT_NO_GIP */

