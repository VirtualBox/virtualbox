/* $Id: mp-r0drv-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, NT.
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
#include "the-nt-kernel.h"

#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#include "r0drv/mp-r0drv.h"
#include "symdb.h"
#include "internal-r0drv-nt.h"
#include "internal/mp.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum
{
    RT_NT_CPUID_SPECIFIC,
    RT_NT_CPUID_PAIR,
    RT_NT_CPUID_OTHERS,
    RT_NT_CPUID_ALL
} RT_NT_CPUID;


/**
 * Used by the RTMpOnSpecific.
 */
typedef struct RTMPNTONSPECIFICARGS
{
    /** Set if we're executing. */
    bool volatile       fExecuting;
    /** Set when done executing. */
    bool volatile       fDone;
    /** Number of references to this heap block. */
    uint32_t volatile   cRefs;
    /** Event that the calling thread is waiting on. */
    KEVENT              DoneEvt;
    /** The deferred procedure call object. */
    KDPC                Dpc;
    /** The callback argument package. */
    RTMPARGS            CallbackArgs;
} RTMPNTONSPECIFICARGS;
/** Pointer to an argument/state structure for RTMpOnSpecific on NT. */
typedef RTMPNTONSPECIFICARGS *PRTMPNTONSPECIFICARGS;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Inactive bit for g_aidRtMpNtByCpuSetIdx. */
#define RTMPNT_ID_F_INACTIVE    RT_BIT_32(31)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Maximum number of processor groups. */
uint32_t                                g_cRtMpNtMaxGroups;
/** Maximum number of processors. */
uint32_t                                g_cRtMpNtMaxCpus;
/** Number of active processors. */
uint32_t volatile                       g_cRtMpNtActiveCpus;
/** The NT CPU set.
 * KeQueryActiveProcssors() cannot be called at all IRQLs and therefore we'll
 * have to cache it.  Fortunately, NT doesn't really support taking CPUs offline,
 * and taking them online was introduced with W2K8 where it is intended for virtual
 * machines and not real HW.  We update this, g_cRtMpNtActiveCpus and
 * g_aidRtMpNtByCpuSetIdx from the rtR0NtMpProcessorChangeCallback.
 */
RTCPUSET                                g_rtMpNtCpuSet;

/** Static per group info.
 * @remarks  With 256 groups this takes up 33KB.  */
static struct
{
    /** The max CPUs in the group. */
    uint16_t    cMaxCpus;
    /** The number of active CPUs at the time of initialization. */
    uint16_t    cActiveCpus;
    /** CPU set indexes for each CPU in the group. */
    int16_t     aidxCpuSetMembers[64];
}                                       g_aRtMpNtCpuGroups[256];
/** Maps CPU set indexes to RTCPUID.
 * Inactive CPUs has bit 31 set (RTMPNT_ID_F_INACTIVE) so we can identify them
 * and shuffle duplicates during CPU hotplugging.  We assign temporary IDs to
 * the inactive CPUs starting at g_cRtMpNtMaxCpus - 1, ASSUMING that active
 * CPUs has IDs from 0 to g_cRtMpNtActiveCpus. */
RTCPUID                                 g_aidRtMpNtByCpuSetIdx[RTCPUSET_MAX_CPUS];
/** The handle of the rtR0NtMpProcessorChangeCallback registration. */
static PVOID                            g_pvMpCpuChangeCallback = NULL;
/** Size of the KAFFINITY_EX structure.
 * This increased from 20 to 32 bitmap words in the 2020 H2 windows 10 release
 * (i.e. 1280 to 2048 CPUs).  We expect this to increase in the future. */
static size_t                           g_cbRtMpNtKaffinityEx = RT_UOFFSETOF(KAFFINITY_EX, Bitmap)
                                                              + RT_SIZEOFMEMB(KAFFINITY_EX, Bitmap[0]) * 256;
/** The size value of the KAFFINITY_EX structure. */
static uint16_t                         g_cRtMpNtKaffinityExEntries = 256;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static VOID __stdcall rtR0NtMpProcessorChangeCallback(void *pvUser, PKE_PROCESSOR_CHANGE_NOTIFY_CONTEXT pChangeCtx,
                                                      PNTSTATUS prcOperationStatus);
static int rtR0NtInitQueryGroupRelations(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX **ppInfo);



/**
 * Initalizes multiprocessor globals (called by rtR0InitNative).
 *
 * @returns IPRT status code.
 * @param   pOsVerInfo          Version information.
 */
DECLHIDDEN(int) rtR0MpNtInit(RTNTSDBOSVER const *pOsVerInfo)
{
#define MY_CHECK_BREAK(a_Check, a_DbgPrintArgs) \
        AssertMsgBreakStmt(a_Check, a_DbgPrintArgs, DbgPrint a_DbgPrintArgs; rc = VERR_INTERNAL_ERROR_4 )
#define MY_CHECK_RETURN(a_Check, a_DbgPrintArgs, a_rcRet) \
        AssertMsgReturnStmt(a_Check, a_DbgPrintArgs, DbgPrint a_DbgPrintArgs, a_rcRet)
#define MY_CHECK(a_Check, a_DbgPrintArgs) \
        AssertMsgStmt(a_Check, a_DbgPrintArgs, DbgPrint a_DbgPrintArgs; rc = VERR_INTERNAL_ERROR_4 )

    /*
     * API combination checks.
     */
    MY_CHECK_RETURN(!g_pfnrtKeSetTargetProcessorDpcEx || g_pfnrtKeGetProcessorNumberFromIndex,
                    ("IPRT: Fatal: Missing KeSetTargetProcessorDpcEx without KeGetProcessorNumberFromIndex!\n"),
                    VERR_SYMBOL_NOT_FOUND);

    /*
     * Get max number of processor groups.
     *
     * We may need to upadjust this number below, because windows likes to keep
     * all options open when it comes to hotplugged CPU group assignments.  A
     * server advertising up to 64 CPUs in the ACPI table will get a result of
     * 64 from KeQueryMaximumGroupCount.  That makes sense.  However, when windows
     * server 2012 does a two processor group setup for it, the sum of the
     * GroupInfo[*].MaximumProcessorCount members below is 128.  This is probably
     * because windows doesn't want to make decisions grouping of hotpluggable CPUs.
     * So, we need to bump the maximum count to 128 below do deal with this as we
     * want to have valid CPU set indexes for all potential CPUs - how could we
     * otherwise use the RTMpGetSet() result and also RTCpuSetCount(RTMpGetSet())
     * should equal RTMpGetCount().
     */
    if (g_pfnrtKeQueryMaximumGroupCount)
    {
        g_cRtMpNtMaxGroups = g_pfnrtKeQueryMaximumGroupCount();
        MY_CHECK_RETURN(g_cRtMpNtMaxGroups <= RTCPUSET_MAX_CPUS && g_cRtMpNtMaxGroups > 0,
                        ("IPRT: Fatal: g_cRtMpNtMaxGroups=%u, max %u\n", g_cRtMpNtMaxGroups, RTCPUSET_MAX_CPUS),
                        VERR_MP_TOO_MANY_CPUS);
    }
    else
        g_cRtMpNtMaxGroups = 1;

    /*
     * Get max number CPUs.
     * This also defines the range of NT CPU indexes, RTCPUID and index into RTCPUSET.
     */
    if (g_pfnrtKeQueryMaximumProcessorCountEx)
    {
        g_cRtMpNtMaxCpus = g_pfnrtKeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
        MY_CHECK_RETURN(g_cRtMpNtMaxCpus <= RTCPUSET_MAX_CPUS && g_cRtMpNtMaxCpus > 0,
                        ("IPRT: Fatal: g_cRtMpNtMaxCpus=%u, max %u [KeQueryMaximumProcessorCountEx]\n",
                         g_cRtMpNtMaxGroups, RTCPUSET_MAX_CPUS),
                        VERR_MP_TOO_MANY_CPUS);
    }
    else if (g_pfnrtKeQueryMaximumProcessorCount)
    {
        g_cRtMpNtMaxCpus = g_pfnrtKeQueryMaximumProcessorCount();
        MY_CHECK_RETURN(g_cRtMpNtMaxCpus <= RTCPUSET_MAX_CPUS && g_cRtMpNtMaxCpus > 0,
                        ("IPRT: Fatal: g_cRtMpNtMaxCpus=%u, max %u [KeQueryMaximumProcessorCount]\n",
                         g_cRtMpNtMaxGroups, RTCPUSET_MAX_CPUS),
                        VERR_MP_TOO_MANY_CPUS);
    }
    else if (g_pfnrtKeQueryActiveProcessors)
    {
        KAFFINITY fActiveProcessors = g_pfnrtKeQueryActiveProcessors();
        MY_CHECK_RETURN(fActiveProcessors != 0,
                        ("IPRT: Fatal: KeQueryActiveProcessors returned 0!\n"),
                        VERR_INTERNAL_ERROR_2);
        g_cRtMpNtMaxCpus = 0;
        do
        {
            g_cRtMpNtMaxCpus++;
            fActiveProcessors >>= 1;
        } while (fActiveProcessors);
    }
    else
        g_cRtMpNtMaxCpus = KeNumberProcessors;

    /*
     * Just because we're a bit paranoid about getting something wrong wrt to the
     * kernel interfaces, we try 16 times to get the KeQueryActiveProcessorCountEx
     * and KeQueryLogicalProcessorRelationship information to match up.
     */
    for (unsigned cTries = 0;; cTries++)
    {
        /*
         * Get number of active CPUs.
         */
        if (g_pfnrtKeQueryActiveProcessorCountEx)
        {
            g_cRtMpNtActiveCpus = g_pfnrtKeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
            MY_CHECK_RETURN(g_cRtMpNtActiveCpus <= g_cRtMpNtMaxCpus && g_cRtMpNtActiveCpus > 0,
                            ("IPRT: Fatal: g_cRtMpNtMaxGroups=%u, max %u [KeQueryActiveProcessorCountEx]\n",
                             g_cRtMpNtMaxGroups, g_cRtMpNtMaxCpus),
                            VERR_MP_TOO_MANY_CPUS);
        }
        else if (g_pfnrtKeQueryActiveProcessorCount)
        {
            g_cRtMpNtActiveCpus = g_pfnrtKeQueryActiveProcessorCount(NULL);
            MY_CHECK_RETURN(g_cRtMpNtActiveCpus <= g_cRtMpNtMaxCpus && g_cRtMpNtActiveCpus > 0,
                            ("IPRT: Fatal: g_cRtMpNtMaxGroups=%u, max %u [KeQueryActiveProcessorCount]\n",
                             g_cRtMpNtMaxGroups, g_cRtMpNtMaxCpus),
                            VERR_MP_TOO_MANY_CPUS);
        }
        else
            g_cRtMpNtActiveCpus = g_cRtMpNtMaxCpus;

        /*
         * Query the details for the groups to figure out which CPUs are online as
         * well as the NT index limit.
         */
        for (unsigned i = 0; i < RT_ELEMENTS(g_aidRtMpNtByCpuSetIdx); i++)
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
            g_aidRtMpNtByCpuSetIdx[i] = NIL_RTCPUID;
#else
            g_aidRtMpNtByCpuSetIdx[i] = i < g_cRtMpNtMaxCpus ? i : NIL_RTCPUID;
#endif
        for (unsigned idxGroup = 0; idxGroup < RT_ELEMENTS(g_aRtMpNtCpuGroups); idxGroup++)
        {
            g_aRtMpNtCpuGroups[idxGroup].cMaxCpus    = 0;
            g_aRtMpNtCpuGroups[idxGroup].cActiveCpus = 0;
            for (unsigned idxMember = 0; idxMember < RT_ELEMENTS(g_aRtMpNtCpuGroups[idxGroup].aidxCpuSetMembers); idxMember++)
                g_aRtMpNtCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = -1;
        }

        if (g_pfnrtKeQueryLogicalProcessorRelationship)
        {
            MY_CHECK_RETURN(g_pfnrtKeGetProcessorIndexFromNumber,
                            ("IPRT: Fatal: Found KeQueryLogicalProcessorRelationship but not KeGetProcessorIndexFromNumber!\n"),
                            VERR_SYMBOL_NOT_FOUND);
            MY_CHECK_RETURN(g_pfnrtKeGetProcessorNumberFromIndex,
                            ("IPRT: Fatal: Found KeQueryLogicalProcessorRelationship but not KeGetProcessorIndexFromNumber!\n"),
                            VERR_SYMBOL_NOT_FOUND);
            MY_CHECK_RETURN(g_pfnrtKeSetTargetProcessorDpcEx,
                            ("IPRT: Fatal: Found KeQueryLogicalProcessorRelationship but not KeSetTargetProcessorDpcEx!\n"),
                            VERR_SYMBOL_NOT_FOUND);

            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *pInfo = NULL;
            int rc = rtR0NtInitQueryGroupRelations(&pInfo);
            if (RT_FAILURE(rc))
                return rc;

            MY_CHECK(pInfo->Group.MaximumGroupCount == g_cRtMpNtMaxGroups,
                     ("IPRT: Fatal: MaximumGroupCount=%u != g_cRtMpNtMaxGroups=%u!\n",
                      pInfo->Group.MaximumGroupCount, g_cRtMpNtMaxGroups));
            MY_CHECK(pInfo->Group.ActiveGroupCount > 0 && pInfo->Group.ActiveGroupCount <= g_cRtMpNtMaxGroups,
                     ("IPRT: Fatal: ActiveGroupCount=%u != g_cRtMpNtMaxGroups=%u!\n",
                      pInfo->Group.ActiveGroupCount, g_cRtMpNtMaxGroups));

            /*
             * First we need to recalc g_cRtMpNtMaxCpus (see above).
             */
            uint32_t cMaxCpus = 0;
            uint32_t idxGroup;
            for (idxGroup = 0; RT_SUCCESS(rc) && idxGroup < pInfo->Group.ActiveGroupCount; idxGroup++)
            {
                const PROCESSOR_GROUP_INFO *pGrpInfo = &pInfo->Group.GroupInfo[idxGroup];
                MY_CHECK_BREAK(pGrpInfo->MaximumProcessorCount <= MAXIMUM_PROC_PER_GROUP,
                               ("IPRT: Fatal: MaximumProcessorCount=%u\n", pGrpInfo->MaximumProcessorCount));
                MY_CHECK_BREAK(pGrpInfo->ActiveProcessorCount <= pGrpInfo->MaximumProcessorCount,
                               ("IPRT: Fatal: ActiveProcessorCount=%u > MaximumProcessorCount=%u\n",
                                pGrpInfo->ActiveProcessorCount, pGrpInfo->MaximumProcessorCount));
                cMaxCpus += pGrpInfo->MaximumProcessorCount;
            }
            if (cMaxCpus > g_cRtMpNtMaxCpus && RT_SUCCESS(rc))
            {
                DbgPrint("IPRT: g_cRtMpNtMaxCpus=%u -> %u\n", g_cRtMpNtMaxCpus, cMaxCpus);
#ifndef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
                uint32_t i = RT_MIN(cMaxCpus, RT_ELEMENTS(g_aidRtMpNtByCpuSetIdx));
                while (i-- > g_cRtMpNtMaxCpus)
                    g_aidRtMpNtByCpuSetIdx[i] = i;
#endif
                g_cRtMpNtMaxCpus = cMaxCpus;
                if (g_cRtMpNtMaxGroups > RTCPUSET_MAX_CPUS)
                {
                    MY_CHECK(g_cRtMpNtMaxGroups <= RTCPUSET_MAX_CPUS && g_cRtMpNtMaxGroups > 0,
                             ("IPRT: Fatal: g_cRtMpNtMaxGroups=%u, max %u\n", g_cRtMpNtMaxGroups, RTCPUSET_MAX_CPUS));
                    rc = VERR_MP_TOO_MANY_CPUS;
                }
            }

            /*
             * Calc online mask, partition IDs and such.
             *
             * Also check ASSUMPTIONS:
             *
             *      1. Processor indexes going from 0 and up to
             *         KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS) - 1.
             *
             *      2. Currently valid processor indexes, i.e. accepted by
             *         KeGetProcessorIndexFromNumber & KeGetProcessorNumberFromIndex, goes
             *         from 0 thru KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS) - 1.
             *
             *      3. PROCESSOR_GROUP_INFO::MaximumProcessorCount gives the number of
             *         relevant bits in the ActiveProcessorMask (from LSB).
             *
             *      4. Active processor count found in KeQueryLogicalProcessorRelationship
             *         output matches what KeQueryActiveProcessorCountEx(ALL) returns.
             *
             *      5. Active + inactive processor counts in same does not exceed
             *         KeQueryMaximumProcessorCountEx(ALL).
             *
             * Note! Processor indexes are assigned as CPUs come online and are not
             *       preallocated according to group maximums.  Since CPUS are only taken
             *       online and never offlined, this means that internal CPU bitmaps are
             *       never sparse and no time is wasted scanning unused bits.
             *
             *       Unfortunately, it means that ring-3 cannot easily guess the index
             *       assignments when hotswapping is used, and must use GIP when available.
             */
            RTCpuSetEmpty(&g_rtMpNtCpuSet);
            uint32_t cInactive = 0;
            uint32_t cActive   = 0;
            uint32_t idxCpuMax = 0;
            uint32_t idxCpuSetNextInactive = g_cRtMpNtMaxCpus - 1;
            for (idxGroup = 0; RT_SUCCESS(rc) && idxGroup < pInfo->Group.ActiveGroupCount; idxGroup++)
            {
                const PROCESSOR_GROUP_INFO *pGrpInfo = &pInfo->Group.GroupInfo[idxGroup];
                MY_CHECK_BREAK(pGrpInfo->MaximumProcessorCount <= MAXIMUM_PROC_PER_GROUP,
                               ("IPRT: Fatal: MaximumProcessorCount=%u\n", pGrpInfo->MaximumProcessorCount));
                MY_CHECK_BREAK(pGrpInfo->ActiveProcessorCount <= pGrpInfo->MaximumProcessorCount,
                               ("IPRT: Fatal: ActiveProcessorCount=%u > MaximumProcessorCount=%u\n",
                                pGrpInfo->ActiveProcessorCount, pGrpInfo->MaximumProcessorCount));

                g_aRtMpNtCpuGroups[idxGroup].cMaxCpus    = pGrpInfo->MaximumProcessorCount;
                g_aRtMpNtCpuGroups[idxGroup].cActiveCpus = pGrpInfo->ActiveProcessorCount;

                for (uint32_t idxMember = 0; idxMember < pGrpInfo->MaximumProcessorCount; idxMember++)
                {
                    PROCESSOR_NUMBER ProcNum;
                    ProcNum.Group    = (USHORT)idxGroup;
                    ProcNum.Number   = (UCHAR)idxMember;
                    ProcNum.Reserved = 0;
                    ULONG idxCpu = g_pfnrtKeGetProcessorIndexFromNumber(&ProcNum);
                    if (idxCpu != INVALID_PROCESSOR_INDEX)
                    {
                        MY_CHECK_BREAK(idxCpu < g_cRtMpNtMaxCpus && idxCpu < RTCPUSET_MAX_CPUS, /* ASSUMPTION #1 */
                                       ("IPRT: Fatal: idxCpu=%u >= g_cRtMpNtMaxCpus=%u (RTCPUSET_MAX_CPUS=%u)\n",
                                        idxCpu, g_cRtMpNtMaxCpus, RTCPUSET_MAX_CPUS));
                        if (idxCpu > idxCpuMax)
                            idxCpuMax = idxCpu;
                        g_aRtMpNtCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxCpu;
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
                        g_aidRtMpNtByCpuSetIdx[idxCpu] = RTMPCPUID_FROM_GROUP_AND_NUMBER(idxGroup, idxMember);
#endif

                        ProcNum.Group    = UINT16_MAX;
                        ProcNum.Number   = UINT8_MAX;
                        ProcNum.Reserved = UINT8_MAX;
                        NTSTATUS rcNt = g_pfnrtKeGetProcessorNumberFromIndex(idxCpu, &ProcNum);
                        MY_CHECK_BREAK(NT_SUCCESS(rcNt),
                                       ("IPRT: Fatal: KeGetProcessorNumberFromIndex(%u,) -> %#x!\n", idxCpu, rcNt));
                        MY_CHECK_BREAK(ProcNum.Group == idxGroup && ProcNum.Number == idxMember,
                                       ("IPRT: Fatal: KeGetProcessorXxxxFromYyyy roundtrip error for %#x! Group: %u vs %u, Number: %u vs %u\n",
                                        idxCpu, ProcNum.Group, idxGroup, ProcNum.Number, idxMember));

                        if (pGrpInfo->ActiveProcessorMask & RT_BIT_64(idxMember))
                        {
                            RTCpuSetAddByIndex(&g_rtMpNtCpuSet, idxCpu);
                            cActive++;
                        }
                        else
                            cInactive++; /* (This is a little unexpected, but not important as long as things add up below.) */
                    }
                    else
                    {
                        /* Must be not present / inactive when KeGetProcessorIndexFromNumber fails. */
                        MY_CHECK_BREAK(!(pGrpInfo->ActiveProcessorMask & RT_BIT_64(idxMember)),
                                       ("IPRT: Fatal: KeGetProcessorIndexFromNumber(%u/%u) failed but CPU is active! cMax=%u cActive=%u fActive=%p\n",
                                        idxGroup, idxMember, pGrpInfo->MaximumProcessorCount, pGrpInfo->ActiveProcessorCount,
                                        pGrpInfo->ActiveProcessorMask));
                        cInactive++;
                        if (idxCpuSetNextInactive >= g_cRtMpNtActiveCpus)
                        {
                            g_aRtMpNtCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxCpuSetNextInactive;
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
                            g_aidRtMpNtByCpuSetIdx[idxCpuSetNextInactive] = RTMPCPUID_FROM_GROUP_AND_NUMBER(idxGroup, idxMember)
                                                                          | RTMPNT_ID_F_INACTIVE;
#endif
                            idxCpuSetNextInactive--;
                        }
                    }
                }
            }

            MY_CHECK(cInactive + cActive <= g_cRtMpNtMaxCpus, /* ASSUMPTION #5 (not '==' because of inactive groups) */
                     ("IPRT: Fatal: cInactive=%u + cActive=%u > g_cRtMpNtMaxCpus=%u\n", cInactive, cActive, g_cRtMpNtMaxCpus));

            /* Deal with inactive groups using KeQueryMaximumProcessorCountEx or as
               best as we can by as best we can by stipulating maximum member counts
               from the previous group. */
            if (   RT_SUCCESS(rc)
                && idxGroup < pInfo->Group.MaximumGroupCount)
            {
                uint16_t cInactiveLeft = g_cRtMpNtMaxCpus - (cInactive + cActive);
                while (idxGroup < pInfo->Group.MaximumGroupCount)
                {
                    uint32_t cMaxMembers = 0;
                    if (g_pfnrtKeQueryMaximumProcessorCountEx)
                        cMaxMembers = g_pfnrtKeQueryMaximumProcessorCountEx(idxGroup);
                    if (cMaxMembers != 0 || cInactiveLeft == 0)
                        AssertStmt(cMaxMembers <= cInactiveLeft, cMaxMembers = cInactiveLeft);
                    else
                    {
                        uint16_t cGroupsLeft = pInfo->Group.MaximumGroupCount - idxGroup;
                        cMaxMembers = pInfo->Group.GroupInfo[idxGroup - 1].MaximumProcessorCount;
                        while (cMaxMembers * cGroupsLeft < cInactiveLeft)
                            cMaxMembers++;
                        if (cMaxMembers > cInactiveLeft)
                            cMaxMembers = cInactiveLeft;
                    }

                    g_aRtMpNtCpuGroups[idxGroup].cMaxCpus    = (uint16_t)cMaxMembers;
                    g_aRtMpNtCpuGroups[idxGroup].cActiveCpus = 0;
                    for (uint16_t idxMember = 0; idxMember < cMaxMembers; idxMember++)
                        if (idxCpuSetNextInactive >= g_cRtMpNtActiveCpus)
                        {
                            g_aRtMpNtCpuGroups[idxGroup].aidxCpuSetMembers[idxMember] = idxCpuSetNextInactive;
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
                            g_aidRtMpNtByCpuSetIdx[idxCpuSetNextInactive] = RTMPCPUID_FROM_GROUP_AND_NUMBER(idxGroup, idxMember)
                                                                          | RTMPNT_ID_F_INACTIVE;
#endif
                            idxCpuSetNextInactive--;
                        }
                    cInactiveLeft -= cMaxMembers;
                    idxGroup++;
                }
            }

            /* We're done with pInfo now, free it so we can start returning when assertions fail. */
            RTMemFree(pInfo);
            if (RT_FAILURE(rc)) /* MY_CHECK_BREAK sets rc. */
                return rc;
            MY_CHECK_RETURN(cActive >= g_cRtMpNtActiveCpus,
                            ("IPRT: Fatal: cActive=%u < g_cRtMpNtActiveCpus=%u - CPUs removed?\n", cActive, g_cRtMpNtActiveCpus),
                            VERR_INTERNAL_ERROR_3);
            MY_CHECK_RETURN(idxCpuMax < cActive, /* ASSUMPTION #2 */
                            ("IPRT: Fatal: idCpuMax=%u >= cActive=%u! Unexpected CPU index allocation. CPUs removed?\n",
                             idxCpuMax, cActive),
                            VERR_INTERNAL_ERROR_4);

            /* Retry if CPUs were added. */
            if (   cActive != g_cRtMpNtActiveCpus
                && cTries < 16)
                continue;
            MY_CHECK_RETURN(cActive == g_cRtMpNtActiveCpus, /* ASSUMPTION #4 */
                            ("IPRT: Fatal: cActive=%u != g_cRtMpNtActiveCpus=%u\n", cActive, g_cRtMpNtActiveCpus),
                            VERR_INTERNAL_ERROR_5);
        }
        else
        {
            /* Legacy: */
            MY_CHECK_RETURN(g_cRtMpNtMaxGroups == 1, ("IPRT: Fatal: Missing KeQueryLogicalProcessorRelationship!\n"),
                            VERR_SYMBOL_NOT_FOUND);

            /** @todo Is it possible that the affinity mask returned by
             *        KeQueryActiveProcessors is sparse? */
            if (g_pfnrtKeQueryActiveProcessors)
                RTCpuSetFromU64(&g_rtMpNtCpuSet, g_pfnrtKeQueryActiveProcessors());
            else if (g_cRtMpNtMaxCpus < 64)
                RTCpuSetFromU64(&g_rtMpNtCpuSet, (UINT64_C(1) << g_cRtMpNtMaxCpus) - 1);
            else
            {
                MY_CHECK_RETURN(g_cRtMpNtMaxCpus == 64, ("IPRT: Fatal: g_cRtMpNtMaxCpus=%u, expect 64 or less\n", g_cRtMpNtMaxCpus),
                                VERR_MP_TOO_MANY_CPUS);
                RTCpuSetFromU64(&g_rtMpNtCpuSet, UINT64_MAX);
            }

            g_aRtMpNtCpuGroups[0].cMaxCpus    = g_cRtMpNtMaxCpus;
            g_aRtMpNtCpuGroups[0].cActiveCpus = g_cRtMpNtMaxCpus;
            for (unsigned i = 0; i < g_cRtMpNtMaxCpus; i++)
            {
                g_aRtMpNtCpuGroups[0].aidxCpuSetMembers[i] = i;
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
                g_aidRtMpNtByCpuSetIdx[i] = RTMPCPUID_FROM_GROUP_AND_NUMBER(0, i);
#endif
            }
        }

        /*
         * Register CPU hot plugging callback (it also counts active CPUs).
         */
        Assert(g_pvMpCpuChangeCallback == NULL);
        if (g_pfnrtKeRegisterProcessorChangeCallback)
        {
            MY_CHECK_RETURN(g_pfnrtKeDeregisterProcessorChangeCallback,
                            ("IPRT: Fatal: KeRegisterProcessorChangeCallback without KeDeregisterProcessorChangeCallback!\n"),
                            VERR_SYMBOL_NOT_FOUND);

            RTCPUSET const ActiveSetCopy = g_rtMpNtCpuSet;
            RTCpuSetEmpty(&g_rtMpNtCpuSet);
            uint32_t const cActiveCpus   = g_cRtMpNtActiveCpus;
            g_cRtMpNtActiveCpus = 0;

            g_pvMpCpuChangeCallback = g_pfnrtKeRegisterProcessorChangeCallback(rtR0NtMpProcessorChangeCallback, NULL /*pvUser*/,
                                                                               KE_PROCESSOR_CHANGE_ADD_EXISTING);
            if (g_pvMpCpuChangeCallback)
            {
                if (cActiveCpus == g_cRtMpNtActiveCpus)
                { /* likely */ }
                else
                {
                    g_pfnrtKeDeregisterProcessorChangeCallback(g_pvMpCpuChangeCallback);
                    if (cTries < 16)
                    {
                        /* Retry if CPUs were added. */
                        MY_CHECK_RETURN(g_cRtMpNtActiveCpus >= cActiveCpus,
                                        ("IPRT: Fatal: g_cRtMpNtActiveCpus=%u < cActiveCpus=%u! CPUs removed?\n",
                                         g_cRtMpNtActiveCpus, cActiveCpus),
                                        VERR_INTERNAL_ERROR_2);
                        MY_CHECK_RETURN(g_cRtMpNtActiveCpus <= g_cRtMpNtMaxCpus,
                                        ("IPRT: Fatal: g_cRtMpNtActiveCpus=%u > g_cRtMpNtMaxCpus=%u!\n",
                                         g_cRtMpNtActiveCpus, g_cRtMpNtMaxCpus),
                                        VERR_INTERNAL_ERROR_2);
                        continue;
                    }
                    MY_CHECK_RETURN(0, ("IPRT: Fatal: g_cRtMpNtActiveCpus=%u cActiveCpus=%u\n", g_cRtMpNtActiveCpus, cActiveCpus),
                                    VERR_INTERNAL_ERROR_3);
                }
            }
            else
            {
                AssertFailed();
                g_rtMpNtCpuSet      = ActiveSetCopy;
                g_cRtMpNtActiveCpus = cActiveCpus;
            }
        }
        break;
    } /* Retry loop for stable active CPU count. */

#undef MY_CHECK_RETURN

    /*
     * Special IPI fun for RTMpPokeCpu.
     *
     * On Vista and later the DPC method doesn't seem to reliably send IPIs,
     * so we have to use alternative methods.
     *
     * On AMD64 We used to use the HalSendSoftwareInterrupt API (also x86 on
     * W10+), it looks faster and more convenient to use, however we're either
     * using it wrong or it doesn't reliably do what we want (see @bugref{8343}).
     *
     * The HalRequestIpip API is thus far the only alternative to KeInsertQueueDpc
     * for doing targetted IPIs.  Trouble with this API is that it changed
     * fundamentally in Window 7 when they added support for lots of processors.
     *
     * If we really think we cannot use KeInsertQueueDpc, we use the broadcast IPI
     * API KeIpiGenericCall.
     */
    if (   pOsVerInfo->uMajorVer > 6
        || (pOsVerInfo->uMajorVer == 6 && pOsVerInfo->uMinorVer > 0))
        g_pfnrtHalRequestIpiPreW7 = NULL;
    else
        g_pfnrtHalRequestIpiW7Plus = NULL;

    if (   g_pfnrtHalRequestIpiW7Plus
        && g_pfnrtKeInitializeAffinityEx
        && g_pfnrtKeAddProcessorAffinityEx
        && g_pfnrtKeGetProcessorIndexFromNumber)
    {
        /* Determine the real size of the KAFFINITY_EX structure. */
        size_t const  cbAffinity = _8K;
        PKAFFINITY_EX pAffinity  = (PKAFFINITY_EX)RTMemAllocZ(cbAffinity);
        AssertReturn(pAffinity, VERR_NO_MEMORY);
        size_t const cMaxEntries = (cbAffinity - RT_UOFFSETOF(KAFFINITY_EX, Bitmap[0])) / sizeof(pAffinity->Bitmap[0]);
        g_pfnrtKeInitializeAffinityEx(pAffinity);
        if (pAffinity->Size > 1 && pAffinity->Size <= cMaxEntries)
        {
            g_cRtMpNtKaffinityExEntries = pAffinity->Size;
            g_cbRtMpNtKaffinityEx       = pAffinity->Size * sizeof(pAffinity->Bitmap[0]) + RT_UOFFSETOF(KAFFINITY_EX, Bitmap[0]);
            g_pfnrtMpPokeCpuWorker      = rtMpPokeCpuUsingHalRequestIpiW7Plus;
            RTMemFree(pAffinity);
            DbgPrint("IPRT: RTMpPoke => rtMpPokeCpuUsingHalRequestIpiW7Plus\n");
            return VINF_SUCCESS;
        }
        DbgPrint("IPRT: RTMpPoke can't use rtMpPokeCpuUsingHalRequestIpiW7Plus! pAffinity->Size=%u\n", pAffinity->Size);
        AssertReleaseMsg(pAffinity->Size <= cMaxEntries, ("%#x\n", pAffinity->Size)); /* stack is toast if larger (32768 CPUs). */
        RTMemFree(pAffinity);
    }

    if (pOsVerInfo->uMajorVer >= 6 && g_pfnrtKeIpiGenericCall)
    {
        DbgPrint("IPRT: RTMpPoke => rtMpPokeCpuUsingBroadcastIpi\n");
        g_pfnrtMpPokeCpuWorker = rtMpPokeCpuUsingBroadcastIpi;
    }
    else if (g_pfnrtKeSetTargetProcessorDpc)
    {
        DbgPrint("IPRT: RTMpPoke => rtMpPokeCpuUsingDpc\n");
        g_pfnrtMpPokeCpuWorker = rtMpPokeCpuUsingDpc;
        /* Windows XP should send always send an IPI -> VERIFY */
    }
    else
    {
        DbgPrint("IPRT: RTMpPoke => rtMpPokeCpuUsingFailureNotSupported\n");
        Assert(pOsVerInfo->uMajorVer == 3 && pOsVerInfo->uMinorVer <= 50);
        g_pfnrtMpPokeCpuWorker = rtMpPokeCpuUsingFailureNotSupported;
    }

    return VINF_SUCCESS;
}


/**
 * Called by rtR0TermNative.
 */
DECLHIDDEN(void) rtR0MpNtTerm(void)
{
    /*
     * Deregister the processor change callback.
     */
    PVOID pvMpCpuChangeCallback = g_pvMpCpuChangeCallback;
    g_pvMpCpuChangeCallback = NULL;
    if (pvMpCpuChangeCallback)
    {
        AssertReturnVoid(g_pfnrtKeDeregisterProcessorChangeCallback);
        g_pfnrtKeDeregisterProcessorChangeCallback(pvMpCpuChangeCallback);
    }
}


DECLHIDDEN(int) rtR0MpNotificationNativeInit(void)
{
    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtR0MpNotificationNativeTerm(void)
{
}


/**
 * Implements the NT PROCESSOR_CALLBACK_FUNCTION callback function.
 *
 * This maintains the g_rtMpNtCpuSet and works MP notification callbacks.  When
 * registered, it's called for each active CPU in the system, avoiding racing
 * CPU hotplugging (as well as testing the callback).
 *
 * @param   pvUser              User context (not used).
 * @param   pChangeCtx          Change context (in).
 * @param   prcOperationStatus  Operation status (in/out).
 *
 * @remarks ASSUMES no concurrent execution of KeProcessorAddCompleteNotify
 *          notification callbacks.  At least during callback registration
 *          callout, we're owning KiDynamicProcessorLock.
 *
 * @remarks When registering the handler, we first get KeProcessorAddStartNotify
 *          callbacks for all active CPUs, and after they all succeed we get the
 *          KeProcessorAddCompleteNotify callbacks.
 */
static VOID __stdcall rtR0NtMpProcessorChangeCallback(void *pvUser, PKE_PROCESSOR_CHANGE_NOTIFY_CONTEXT pChangeCtx,
                                                      PNTSTATUS prcOperationStatus)
{
    RT_NOREF(pvUser, prcOperationStatus);
    switch (pChangeCtx->State)
    {
        /*
         * Check whether we can deal with the CPU, failing the start operation if we
         * can't.  The checks we are doing here are to avoid complicated/impossible
         * cases in KeProcessorAddCompleteNotify.  They are really just verify specs.
         */
        case KeProcessorAddStartNotify:
        {
            NTSTATUS rcNt = STATUS_SUCCESS;
            if (pChangeCtx->NtNumber < RTCPUSET_MAX_CPUS)
            {
                if (pChangeCtx->NtNumber >= g_cRtMpNtMaxCpus)
                {
                    DbgPrint("IPRT: KeProcessorAddStartNotify failure: NtNumber=%u is higher than the max CPU count (%u)!\n",
                             pChangeCtx->NtNumber, g_cRtMpNtMaxCpus);
                    rcNt = STATUS_INTERNAL_ERROR;
                }

                /* The ProcessNumber field was introduced in Windows 7. */
                PROCESSOR_NUMBER ProcNum;
                if (g_pfnrtKeGetProcessorIndexFromNumber)
                {
                    ProcNum = pChangeCtx->ProcNumber;
                    KEPROCESSORINDEX idxCpu = g_pfnrtKeGetProcessorIndexFromNumber(&ProcNum);
                    if (idxCpu != pChangeCtx->NtNumber)
                    {
                        DbgPrint("IPRT: KeProcessorAddStartNotify failure: g_pfnrtKeGetProcessorIndexFromNumber(%u.%u) -> %u, expected %u!\n",
                                 ProcNum.Group, ProcNum.Number, idxCpu, pChangeCtx->NtNumber);
                        rcNt = STATUS_INTERNAL_ERROR;
                    }
                }
                else
                {
                    ProcNum.Group  = 0;
                    ProcNum.Number = pChangeCtx->NtNumber;
                }

                if (   ProcNum.Group  < RT_ELEMENTS(g_aRtMpNtCpuGroups)
                    && ProcNum.Number < RT_ELEMENTS(g_aRtMpNtCpuGroups[0].aidxCpuSetMembers))
                {
                    if (ProcNum.Group >= g_cRtMpNtMaxGroups)
                    {
                        DbgPrint("IPRT: KeProcessorAddStartNotify failure: %u.%u is out of range - max groups: %u!\n",
                                 ProcNum.Group, ProcNum.Number, g_cRtMpNtMaxGroups);
                        rcNt = STATUS_INTERNAL_ERROR;
                    }

                    if (ProcNum.Number < g_aRtMpNtCpuGroups[ProcNum.Group].cMaxCpus)
                    {
                        Assert(g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number] != -1);
                        if (g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number] == -1)
                        {
                            DbgPrint("IPRT: KeProcessorAddStartNotify failure: Internal error! %u.%u was assigned -1 as set index!\n",
                                     ProcNum.Group, ProcNum.Number);
                            rcNt = STATUS_INTERNAL_ERROR;
                        }

                        Assert(g_aidRtMpNtByCpuSetIdx[pChangeCtx->NtNumber] != NIL_RTCPUID);
                        if (g_aidRtMpNtByCpuSetIdx[pChangeCtx->NtNumber] == NIL_RTCPUID)
                        {
                            DbgPrint("IPRT: KeProcessorAddStartNotify failure: Internal error! %u (%u.%u) translates to NIL_RTCPUID!\n",
                                     pChangeCtx->NtNumber, ProcNum.Group, ProcNum.Number);
                            rcNt = STATUS_INTERNAL_ERROR;
                        }
                    }
                    else
                    {
                        DbgPrint("IPRT: KeProcessorAddStartNotify failure: max processors in group %u is %u, cannot add %u to it!\n",
                                 ProcNum.Group, g_aRtMpNtCpuGroups[ProcNum.Group].cMaxCpus, ProcNum.Group, ProcNum.Number);
                        rcNt = STATUS_INTERNAL_ERROR;
                    }
                }
                else
                {
                    DbgPrint("IPRT: KeProcessorAddStartNotify failure: %u.%u is out of range (max %u.%u)!\n",
                             ProcNum.Group, ProcNum.Number, RT_ELEMENTS(g_aRtMpNtCpuGroups), RT_ELEMENTS(g_aRtMpNtCpuGroups[0].aidxCpuSetMembers));
                    rcNt = STATUS_INTERNAL_ERROR;
                }
            }
            else
            {
                DbgPrint("IPRT: KeProcessorAddStartNotify failure: NtNumber=%u is outside RTCPUSET_MAX_CPUS (%u)!\n",
                         pChangeCtx->NtNumber, RTCPUSET_MAX_CPUS);
                rcNt = STATUS_INTERNAL_ERROR;
            }
            if (!NT_SUCCESS(rcNt))
                *prcOperationStatus = rcNt;
            break;
        }

        /*
         * Update the globals.  Since we've checked out range limits and other
         * limitations already we just AssertBreak here.
         */
        case KeProcessorAddCompleteNotify:
        {
            /*
             * Calc the processor number and assert conditions checked in KeProcessorAddStartNotify.
             */
            AssertBreak(pChangeCtx->NtNumber < RTCPUSET_MAX_CPUS);
            AssertBreak(pChangeCtx->NtNumber < g_cRtMpNtMaxCpus);
            Assert(pChangeCtx->NtNumber == g_cRtMpNtActiveCpus); /* light assumption */
            PROCESSOR_NUMBER ProcNum;
            if (g_pfnrtKeGetProcessorIndexFromNumber)
            {
                ProcNum = pChangeCtx->ProcNumber;
                AssertBreak(g_pfnrtKeGetProcessorIndexFromNumber(&ProcNum) == pChangeCtx->NtNumber);
                AssertBreak(ProcNum.Group < RT_ELEMENTS(g_aRtMpNtCpuGroups));
                AssertBreak(ProcNum.Group < g_cRtMpNtMaxGroups);
            }
            else
            {
                ProcNum.Group  = 0;
                ProcNum.Number = pChangeCtx->NtNumber;
            }
            AssertBreak(ProcNum.Number < RT_ELEMENTS(g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers));
            AssertBreak(ProcNum.Number < g_aRtMpNtCpuGroups[ProcNum.Group].cMaxCpus);
            AssertBreak(g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number] != -1);
            AssertBreak(g_aidRtMpNtByCpuSetIdx[pChangeCtx->NtNumber] != NIL_RTCPUID);

            /*
             * Add ourselves to the online CPU set and update the active CPU count.
             */
            RTCpuSetAddByIndex(&g_rtMpNtCpuSet, pChangeCtx->NtNumber);
            ASMAtomicIncU32(&g_cRtMpNtActiveCpus);

            /*
             * Update the group info.
             *
             * If the index prediction failed (real hotplugging callbacks only) we
             * have to switch it around.  This is particularly annoying when we
             * use the index as the ID.
             */
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
            RTCPUID idCpu = RTMPCPUID_FROM_GROUP_AND_NUMBER(ProcNum.Group, ProcNum.Number);
            RTCPUID idOld = g_aidRtMpNtByCpuSetIdx[pChangeCtx->NtNumber];
            if ((idOld & ~RTMPNT_ID_F_INACTIVE) != idCpu)
            {
                Assert(idOld & RTMPNT_ID_F_INACTIVE);
                int idxDest = g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number];
                g_aRtMpNtCpuGroups[rtMpCpuIdGetGroup(idOld)].aidxCpuSetMembers[rtMpCpuIdGetGroupMember(idOld)] = idxDest;
                g_aidRtMpNtByCpuSetIdx[idxDest] = idOld;
            }
            g_aidRtMpNtByCpuSetIdx[pChangeCtx->NtNumber] = idCpu;
#else
            Assert(g_aidRtMpNtByCpuSetIdx[pChangeCtx->NtNumber] == pChangeCtx->NtNumber);
            int idxDest = g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number];
            if ((ULONG)idxDest != pChangeCtx->NtNumber)
            {
                bool     fFound = false;
                uint32_t idxOldGroup = g_cRtMpNtMaxGroups;
                while (idxOldGroup-- > 0 && !fFound)
                {
                    uint32_t idxMember = g_aRtMpNtCpuGroups[idxOldGroup].cMaxCpus;
                    while (idxMember-- > 0)
                        if (g_aRtMpNtCpuGroups[idxOldGroup].aidxCpuSetMembers[idxMember] == (int)pChangeCtx->NtNumber)
                        {
                            g_aRtMpNtCpuGroups[idxOldGroup].aidxCpuSetMembers[idxMember] = idxDest;
                            fFound = true;
                            break;
                        }
                }
                Assert(fFound);
            }
#endif
            g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number] = pChangeCtx->NtNumber;

            /*
             * Do MP notification callbacks.
             */
            rtMpNotificationDoCallbacks(RTMPEVENT_ONLINE, pChangeCtx->NtNumber);
            break;
        }

        case KeProcessorAddFailureNotify:
            /* ignore */
            break;

        default:
            AssertMsgFailed(("State=%u\n", pChangeCtx->State));
    }
}


/**
 * Wrapper around KeQueryLogicalProcessorRelationship.
 *
 * @returns IPRT status code.
 * @param   ppInfo  Where to return the info. Pass to RTMemFree when done.
 */
static int rtR0NtInitQueryGroupRelations(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX **ppInfo)
{
    ULONG    cbInfo = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)
                    + g_cRtMpNtMaxGroups * sizeof(GROUP_RELATIONSHIP);
    NTSTATUS rcNt;
    do
    {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *pInfo = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)RTMemAlloc(cbInfo);
        if (pInfo)
        {
            rcNt = g_pfnrtKeQueryLogicalProcessorRelationship(NULL /*pProcNumber*/, RelationGroup, pInfo, &cbInfo);
            if (NT_SUCCESS(rcNt))
            {
                *ppInfo = pInfo;
                return VINF_SUCCESS;
            }

            RTMemFree(pInfo);
            pInfo = NULL;
        }
        else
            rcNt = STATUS_NO_MEMORY;
    } while (rcNt == STATUS_INFO_LENGTH_MISMATCH);
    DbgPrint("IPRT: Fatal: KeQueryLogicalProcessorRelationship failed: %#x\n", rcNt);
    AssertMsgFailed(("KeQueryLogicalProcessorRelationship failed: %#x\n", rcNt));
    return RTErrConvertFromNtStatus(rcNt);
}





RTDECL(RTCPUID) RTMpCpuId(void)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    PROCESSOR_NUMBER ProcNum;
    ProcNum.Group = 0;
    if (g_pfnrtKeGetCurrentProcessorNumberEx)
    {
        ProcNum.Number = 0;
        g_pfnrtKeGetCurrentProcessorNumberEx(&ProcNum);
    }
    else
        ProcNum.Number = KeGetCurrentProcessorNumber(); /* Number is 8-bit, so we're not subject to BYTE -> WORD upgrade in WDK.  */
    return RTMPCPUID_FROM_GROUP_AND_NUMBER(ProcNum.Group, ProcNum.Number);

#else

    if (g_pfnrtKeGetCurrentProcessorNumberEx)
    {
        KEPROCESSORINDEX idxCpu = g_pfnrtKeGetCurrentProcessorNumberEx(NULL);
        Assert(idxCpu < RTCPUSET_MAX_CPUS);
        return idxCpu;
    }

    return (uint8_t)KeGetCurrentProcessorNumber(); /* PCR->Number was changed from BYTE to WORD in the WDK, thus the cast. */
#endif
}


RTDECL(int) RTMpCurSetIndex(void)
{
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    if (g_pfnrtKeGetCurrentProcessorNumberEx)
    {
        KEPROCESSORINDEX idxCpu = g_pfnrtKeGetCurrentProcessorNumberEx(NULL);
        Assert(idxCpu < RTCPUSET_MAX_CPUS);
        return idxCpu;
    }
    return (uint8_t)KeGetCurrentProcessorNumber(); /* PCR->Number was changed from BYTE to WORD in the WDK, thus the cast. */
#else
    return (int)RTMpCpuId();
#endif
}


RTDECL(int) RTMpCurSetIndexAndId(PRTCPUID pidCpu)
{
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    PROCESSOR_NUMBER ProcNum = { 0 , 0,  0 };
    KEPROCESSORINDEX idxCpu = g_pfnrtKeGetCurrentProcessorNumberEx(&ProcNum);
    Assert(idxCpu < RTCPUSET_MAX_CPUS);
    *pidCpu = RTMPCPUID_FROM_GROUP_AND_NUMBER(ProcNum.Group, ProcNum.Number);
    return idxCpu;
#else
    return *pidCpu = RTMpCpuId();
#endif
}


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    if (idCpu != NIL_RTCPUID)
    {
        if (g_pfnrtKeGetProcessorIndexFromNumber)
        {
            PROCESSOR_NUMBER ProcNum;
            ProcNum.Group    = rtMpCpuIdGetGroup(idCpu);
            ProcNum.Number   = rtMpCpuIdGetGroupMember(idCpu);
            ProcNum.Reserved = 0;
            KEPROCESSORINDEX idxCpu = g_pfnrtKeGetProcessorIndexFromNumber(&ProcNum);
            if (idxCpu != INVALID_PROCESSOR_INDEX)
            {
                Assert(idxCpu < g_cRtMpNtMaxCpus);
                Assert((ULONG)g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number] == idxCpu);
                return idxCpu;
            }

            /* Since NT assigned indexes as the CPUs come online, we cannot produce an ID <-> index
               mapping for not-yet-onlined CPUS that is consistent.  We just have to do our best... */
            if (   ProcNum.Group < g_cRtMpNtMaxGroups
                && ProcNum.Number < g_aRtMpNtCpuGroups[ProcNum.Group].cMaxCpus)
                return g_aRtMpNtCpuGroups[ProcNum.Group].aidxCpuSetMembers[ProcNum.Number];
        }
        else if (rtMpCpuIdGetGroup(idCpu) == 0)
            return rtMpCpuIdGetGroupMember(idCpu);
    }
    return -1;
#else
    /* 1:1 mapping, just do range checks. */
    return idCpu < RTCPUSET_MAX_CPUS ? (int)idCpu : -1;
#endif
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    if ((unsigned)iCpu < g_cRtMpNtMaxCpus)
    {
        if (g_pfnrtKeGetProcessorIndexFromNumber)
        {
            PROCESSOR_NUMBER ProcNum = { 0, 0, 0 };
            NTSTATUS rcNt = g_pfnrtKeGetProcessorNumberFromIndex(iCpu, &ProcNum);
            if (NT_SUCCESS(rcNt))
            {
                Assert(ProcNum.Group <= g_cRtMpNtMaxGroups);
                Assert(   (g_aidRtMpNtByCpuSetIdx[iCpu] & ~RTMPNT_ID_F_INACTIVE)
                       == RTMPCPUID_FROM_GROUP_AND_NUMBER(ProcNum.Group, ProcNum.Number));
                return RTMPCPUID_FROM_GROUP_AND_NUMBER(ProcNum.Group, ProcNum.Number);
            }
        }
        return g_aidRtMpNtByCpuSetIdx[iCpu];
    }
    return NIL_RTCPUID;
#else
    /* 1:1 mapping, just do range checks. */
    return (unsigned)iCpu < RTCPUSET_MAX_CPUS ? iCpu : NIL_RTCPUID;
#endif
}


RTDECL(int) RTMpSetIndexFromCpuGroupMember(uint32_t idxGroup, uint32_t idxMember)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    if (idxGroup < g_cRtMpNtMaxGroups)
        if (idxMember < g_aRtMpNtCpuGroups[idxGroup].cMaxCpus)
            return g_aRtMpNtCpuGroups[idxGroup].aidxCpuSetMembers[idxMember];
    return -1;
}


RTDECL(uint32_t) RTMpGetCpuGroupCounts(uint32_t idxGroup, uint32_t *pcActive)
{
    if (idxGroup < g_cRtMpNtMaxGroups)
    {
        if (pcActive)
            *pcActive = g_aRtMpNtCpuGroups[idxGroup].cActiveCpus;
        return g_aRtMpNtCpuGroups[idxGroup].cMaxCpus;
    }
    if (pcActive)
        *pcActive = 0;
    return 0;
}


RTDECL(uint32_t) RTMpGetMaxCpuGroupCount(void)
{
    return g_cRtMpNtMaxGroups;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    return RTMPCPUID_FROM_GROUP_AND_NUMBER(g_cRtMpNtMaxGroups - 1, g_aRtMpNtCpuGroups[g_cRtMpNtMaxGroups - 1].cMaxCpus - 1);
#else
    /* According to MSDN the processor indexes goes from 0 to the maximum
       number of CPUs in the system.  We've check this in initterm-r0drv-nt.cpp. */
    return g_cRtMpNtMaxCpus - 1;
#endif
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */
    return RTCpuSetIsMember(&g_rtMpNtCpuSet, idCpu);
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

#ifdef IPRT_WITH_RTCPUID_AS_GROUP_AND_NUMBER
    if (idCpu != NIL_RTCPUID)
    {
        unsigned idxGroup = rtMpCpuIdGetGroup(idCpu);
        if (idxGroup < g_cRtMpNtMaxGroups)
            return rtMpCpuIdGetGroupMember(idCpu) < g_aRtMpNtCpuGroups[idxGroup].cMaxCpus;
    }
    return false;

#else
    /* A possible CPU ID is one with a value lower than g_cRtMpNtMaxCpus (see
       comment in RTMpGetMaxCpuId). */
    return idCpu < g_cRtMpNtMaxCpus;
#endif
}



RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    /* The set of possible CPU IDs(/indexes) are from 0 up to
       g_cRtMpNtMaxCpus (see comment in RTMpGetMaxCpuId). */
    RTCpuSetEmpty(pSet);
    int idxCpu = g_cRtMpNtMaxCpus;
    while (idxCpu-- > 0)
        RTCpuSetAddByIndex(pSet, idxCpu);
    return pSet;
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */
    return g_cRtMpNtMaxCpus;
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    *pSet = g_rtMpNtCpuSet;
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
    /** @todo fix me */
    return RTMpGetOnlineCount();
}



#if 0
/* Experiment with checking the undocumented KPRCB structure
 * 'dt nt!_kprcb 0xaddress' shows the layout
 */
typedef struct
{
    LIST_ENTRY     DpcListHead;
    ULONG_PTR      DpcLock;
    volatile ULONG DpcQueueDepth;
    ULONG          DpcQueueCount;
} KDPC_DATA, *PKDPC_DATA;

RTDECL(bool) RTMpIsCpuWorkPending(void)
{
    uint8_t *pkprcb;
    PKDPC_DATA pDpcData;

    _asm {
        mov eax, fs:0x20
        mov pkprcb, eax
    }
    pDpcData = (PKDPC_DATA)(pkprcb + 0x19e0);
    if (pDpcData->DpcQueueDepth)
        return true;

    pDpcData++;
    if (pDpcData->DpcQueueDepth)
        return true;
    return false;
}
#else
RTDECL(bool) RTMpIsCpuWorkPending(void)
{
    /** @todo not implemented */
    return false;
}
#endif


/**
 * Wrapper between the native KIPI_BROADCAST_WORKER and IPRT's PFNRTMPWORKER for
 * the RTMpOnAll case.
 *
 * @param   uUserCtx            The user context argument (PRTMPARGS).
 */
static ULONG_PTR rtmpNtOnAllBroadcastIpiWrapper(ULONG_PTR uUserCtx)
{
    PRTMPARGS pArgs = (PRTMPARGS)uUserCtx;
    /*ASMAtomicIncU32(&pArgs->cHits); - not needed */
    pArgs->pfnWorker(RTMpCpuId(), pArgs->pvUser1, pArgs->pvUser2);
    return 0;
}


/**
 * Wrapper between the native KIPI_BROADCAST_WORKER and IPRT's PFNRTMPWORKER for
 * the RTMpOnOthers case.
 *
 * @param   uUserCtx            The user context argument (PRTMPARGS).
 */
static ULONG_PTR rtmpNtOnOthersBroadcastIpiWrapper(ULONG_PTR uUserCtx)
{
    PRTMPARGS pArgs = (PRTMPARGS)uUserCtx;
    RTCPUID idCpu = RTMpCpuId();
    if (pArgs->idCpu != idCpu)
    {
        /*ASMAtomicIncU32(&pArgs->cHits); - not needed */
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
    }
    return 0;
}


/**
 * Wrapper between the native KIPI_BROADCAST_WORKER and IPRT's PFNRTMPWORKER for
 * the RTMpOnPair case.
 *
 * @param   uUserCtx            The user context argument (PRTMPARGS).
 */
static ULONG_PTR rtmpNtOnPairBroadcastIpiWrapper(ULONG_PTR uUserCtx)
{
    PRTMPARGS pArgs = (PRTMPARGS)uUserCtx;
    RTCPUID idCpu = RTMpCpuId();
    if (   pArgs->idCpu  == idCpu
        || pArgs->idCpu2 == idCpu)
    {
        ASMAtomicIncU32(&pArgs->cHits);
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
    }
    return 0;
}


/**
 * Wrapper between the native KIPI_BROADCAST_WORKER and IPRT's PFNRTMPWORKER for
 * the RTMpOnSpecific case.
 *
 * @param   uUserCtx            The user context argument (PRTMPARGS).
 */
static ULONG_PTR rtmpNtOnSpecificBroadcastIpiWrapper(ULONG_PTR uUserCtx)
{
    PRTMPARGS pArgs = (PRTMPARGS)uUserCtx;
    RTCPUID idCpu = RTMpCpuId();
    if (pArgs->idCpu == idCpu)
    {
        ASMAtomicIncU32(&pArgs->cHits);
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
    }
    return 0;
}


/**
 * Internal worker for the RTMpOn* APIs using KeIpiGenericCall.
 *
 * @returns VINF_SUCCESS.
 * @param   pfnWorker           The callback.
 * @param   pvUser1             User argument 1.
 * @param   pvUser2             User argument 2.
 * @param   pfnNativeWrapper    The wrapper between the NT and IPRT callbacks.
 * @param   idCpu               First CPU to match, ultimately specific to the
 *                              pfnNativeWrapper used.
 * @param   idCpu2              Second CPU to match, ultimately specific to the
 *                              pfnNativeWrapper used.
 * @param   pcHits              Where to return the number of this. Optional.
 */
static int rtMpCallUsingBroadcastIpi(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2,
                                     PKIPI_BROADCAST_WORKER pfnNativeWrapper, RTCPUID idCpu, RTCPUID idCpu2,
                                     uint32_t *pcHits)
{
    RTMPARGS Args;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1   = pvUser1;
    Args.pvUser2   = pvUser2;
    Args.idCpu     = idCpu;
    Args.idCpu2    = idCpu2;
    Args.cRefs     = 0;
    Args.cHits     = 0;

    AssertPtr(g_pfnrtKeIpiGenericCall);
    g_pfnrtKeIpiGenericCall(pfnNativeWrapper, (uintptr_t)&Args);
    if (pcHits)
        *pcHits = Args.cHits;
    return VINF_SUCCESS;
}


/**
 * Wrapper between the native nt per-cpu callbacks and PFNRTWORKER
 *
 * @param   Dpc                 DPC object
 * @param   DeferredContext     Context argument specified by KeInitializeDpc
 * @param   SystemArgument1     Argument specified by KeInsertQueueDpc
 * @param   SystemArgument2     Argument specified by KeInsertQueueDpc
 */
static VOID rtmpNtDPCWrapper(IN PKDPC Dpc, IN PVOID DeferredContext, IN PVOID SystemArgument1, IN PVOID SystemArgument2)
{
    PRTMPARGS pArgs = (PRTMPARGS)DeferredContext;
    RT_NOREF3(Dpc, SystemArgument1, SystemArgument2);

    ASMAtomicIncU32(&pArgs->cHits);
    pArgs->pfnWorker(RTMpCpuId(), pArgs->pvUser1, pArgs->pvUser2);

    /* Dereference the argument structure. */
    int32_t cRefs = ASMAtomicDecS32(&pArgs->cRefs);
    Assert(cRefs >= 0);
    if (cRefs == 0)
        RTMemFree(pArgs);
}


/**
 * Wrapper around KeSetTargetProcessorDpcEx / KeSetTargetProcessorDpc.
 *
 * This is shared with the timer code.
 *
 * @returns IPRT status code (errors are asserted).
 * @retval  VERR_CPU_NOT_FOUND if impossible CPU. Not asserted.
 * @param   pDpc                The DPC.
 * @param   idCpu               The ID of the new target CPU.
 * @note    Callable at any IRQL.
 */
DECLHIDDEN(int) rtMpNtSetTargetProcessorDpc(KDPC *pDpc, RTCPUID idCpu)
{
    if (g_pfnrtKeSetTargetProcessorDpcEx)
    {
        /* Convert to stupid process number (bet KeSetTargetProcessorDpcEx does
           the reverse conversion internally). */
        PROCESSOR_NUMBER ProcNum;
        NTSTATUS rcNt = g_pfnrtKeGetProcessorNumberFromIndex(RTMpCpuIdToSetIndex(idCpu), &ProcNum);
        if (NT_SUCCESS(rcNt))
        {
            rcNt = g_pfnrtKeSetTargetProcessorDpcEx(pDpc, &ProcNum);
            AssertLogRelMsgReturn(NT_SUCCESS(rcNt),
                                  ("KeSetTargetProcessorDpcEx(,%u(%u/%u)) -> %#x\n", idCpu, ProcNum.Group, ProcNum.Number, rcNt),
                                  RTErrConvertFromNtStatus(rcNt));
        }
        else if (rcNt == STATUS_INVALID_PARAMETER)
            return VERR_CPU_NOT_FOUND;
        else
            AssertLogRelMsgReturn(NT_SUCCESS(rcNt), ("KeGetProcessorNumberFromIndex(%u) -> %#x\n", idCpu, rcNt),
                                  RTErrConvertFromNtStatus(rcNt));

    }
    else if (g_pfnrtKeSetTargetProcessorDpc)
        g_pfnrtKeSetTargetProcessorDpc(pDpc, RTMpCpuIdToSetIndex(idCpu));
    else
        return VERR_NOT_SUPPORTED;
    return VINF_SUCCESS;
}


/**
 * Internal worker for the RTMpOn* APIs.
 *
 * @returns IPRT status code.
 * @param   pfnWorker   The callback.
 * @param   pvUser1     User argument 1.
 * @param   pvUser2     User argument 2.
 * @param   enmCpuid    What to do / is idCpu valid.
 * @param   idCpu       Used if enmCpuid is RT_NT_CPUID_SPECIFIC or
 *                      RT_NT_CPUID_PAIR, otherwise ignored.
 * @param   idCpu2      Used if enmCpuid is RT_NT_CPUID_PAIR, otherwise ignored.
 * @param   pcHits      Where to return the number of this. Optional.
 */
static int rtMpCallUsingDpcs(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2,
                             RT_NT_CPUID enmCpuid, RTCPUID idCpu, RTCPUID idCpu2, uint32_t *pcHits)
{
#if 0
    /* KeFlushQueuedDpcs must be run at IRQL PASSIVE_LEVEL according to MSDN, but the
     * driver verifier doesn't complain...
     */
    AssertMsg(KeGetCurrentIrql() == PASSIVE_LEVEL, ("%d != %d (PASSIVE_LEVEL)\n", KeGetCurrentIrql(), PASSIVE_LEVEL));
#endif
    /* KeFlushQueuedDpcs is not present in Windows 2000; import it dynamically so we can just fail this call. */
    if (!g_pfnrtNtKeFlushQueuedDpcs)
        return VERR_NOT_SUPPORTED;

    /*
     * Make a copy of the active CPU set and figure out how many KDPCs we really need.
     * We must not try setup DPCs for CPUs which aren't there, because that may fail.
     */
    RTCPUSET  OnlineSet = g_rtMpNtCpuSet;
    uint32_t  cDpcsNeeded;
    switch (enmCpuid)
    {
        case RT_NT_CPUID_SPECIFIC:
            cDpcsNeeded = 1;
            break;
        case RT_NT_CPUID_PAIR:
            cDpcsNeeded = 2;
            break;
        default:
            do
            {
                cDpcsNeeded = g_cRtMpNtActiveCpus;
                OnlineSet   = g_rtMpNtCpuSet;
            } while (cDpcsNeeded != g_cRtMpNtActiveCpus);
            break;
    }

    /*
     * Allocate an RTMPARGS structure followed by cDpcsNeeded KDPCs
     * and initialize them.
     */
    PRTMPARGS pArgs = (PRTMPARGS)RTMemAllocZ(sizeof(RTMPARGS) + cDpcsNeeded * sizeof(KDPC));
    if (!pArgs)
        return VERR_NO_MEMORY;

    pArgs->pfnWorker = pfnWorker;
    pArgs->pvUser1   = pvUser1;
    pArgs->pvUser2   = pvUser2;
    pArgs->idCpu     = NIL_RTCPUID;
    pArgs->idCpu2    = NIL_RTCPUID;
    pArgs->cHits     = 0;
    pArgs->cRefs     = 1;

    int rc;
    KDPC *paExecCpuDpcs = (KDPC *)(pArgs + 1);
    if (enmCpuid == RT_NT_CPUID_SPECIFIC)
    {
        KeInitializeDpc(&paExecCpuDpcs[0], rtmpNtDPCWrapper, pArgs);
        if (g_pfnrtKeSetImportanceDpc)
            g_pfnrtKeSetImportanceDpc(&paExecCpuDpcs[0], HighImportance);
        rc = rtMpNtSetTargetProcessorDpc(&paExecCpuDpcs[0], idCpu);
        pArgs->idCpu = idCpu;
    }
    else if (enmCpuid == RT_NT_CPUID_PAIR)
    {
        KeInitializeDpc(&paExecCpuDpcs[0], rtmpNtDPCWrapper, pArgs);
        if (g_pfnrtKeSetImportanceDpc)
            g_pfnrtKeSetImportanceDpc(&paExecCpuDpcs[0], HighImportance);
        rc = rtMpNtSetTargetProcessorDpc(&paExecCpuDpcs[0], idCpu);
        pArgs->idCpu = idCpu;

        KeInitializeDpc(&paExecCpuDpcs[1], rtmpNtDPCWrapper, pArgs);
        if (g_pfnrtKeSetImportanceDpc)
            g_pfnrtKeSetImportanceDpc(&paExecCpuDpcs[1], HighImportance);
        if (RT_SUCCESS(rc))
            rc = rtMpNtSetTargetProcessorDpc(&paExecCpuDpcs[1], (int)idCpu2);
        pArgs->idCpu2 = idCpu2;
    }
    else
    {
        rc = VINF_SUCCESS;
        for (uint32_t i = 0; i < cDpcsNeeded && RT_SUCCESS(rc); i++)
            if (RTCpuSetIsMemberByIndex(&OnlineSet, i))
            {
                KeInitializeDpc(&paExecCpuDpcs[i], rtmpNtDPCWrapper, pArgs);
                if (g_pfnrtKeSetImportanceDpc)
                    g_pfnrtKeSetImportanceDpc(&paExecCpuDpcs[i], HighImportance);
                rc = rtMpNtSetTargetProcessorDpc(&paExecCpuDpcs[i], RTMpCpuIdFromSetIndex(i));
            }
    }
    if (RT_FAILURE(rc))
    {
        RTMemFree(pArgs);
        return rc;
    }

    /*
     * Raise the IRQL to DISPATCH_LEVEL so we can't be rescheduled to another cpu.
     * KeInsertQueueDpc must also be executed at IRQL >= DISPATCH_LEVEL.
     */
    KIRQL oldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);

    /*
     * We cannot do other than assume a 1:1 relationship between the
     * affinity mask and the process despite the warnings in the docs.
     * If someone knows a better way to get this done, please let bird know.
     */
    ASMCompilerBarrier(); /* paranoia */
    if (enmCpuid == RT_NT_CPUID_SPECIFIC)
    {
        ASMAtomicIncS32(&pArgs->cRefs);
        BOOLEAN fRc = KeInsertQueueDpc(&paExecCpuDpcs[0], 0, 0);
        Assert(fRc); NOREF(fRc);
    }
    else if (enmCpuid == RT_NT_CPUID_PAIR)
    {
        ASMAtomicIncS32(&pArgs->cRefs);
        BOOLEAN fRc = KeInsertQueueDpc(&paExecCpuDpcs[0], 0, 0);
        Assert(fRc); NOREF(fRc);

        ASMAtomicIncS32(&pArgs->cRefs);
        fRc = KeInsertQueueDpc(&paExecCpuDpcs[1], 0, 0);
        Assert(fRc); NOREF(fRc);
    }
    else
    {
        uint32_t iSelf = RTMpCurSetIndex();
        for (uint32_t i = 0; i < cDpcsNeeded; i++)
        {
            if (   (i != iSelf)
                && RTCpuSetIsMemberByIndex(&OnlineSet, i))
            {
                ASMAtomicIncS32(&pArgs->cRefs);
                BOOLEAN fRc = KeInsertQueueDpc(&paExecCpuDpcs[i], 0, 0);
                Assert(fRc); NOREF(fRc);
            }
        }
        if (enmCpuid != RT_NT_CPUID_OTHERS)
            pfnWorker(iSelf, pvUser1, pvUser2);
    }

    KeLowerIrql(oldIrql);

    /*
     * Flush all DPCs and wait for completion. (can take long!)
     */
    /** @todo Consider changing this to an active wait using some atomic inc/dec
     *  stuff (and check for the current cpu above in the specific case). */
    /** @todo Seems KeFlushQueuedDpcs doesn't wait for the DPCs to be completely
     *        executed. Seen pArgs being freed while some CPU was using it before
     *        cRefs was added. */
    if (g_pfnrtNtKeFlushQueuedDpcs)
        g_pfnrtNtKeFlushQueuedDpcs();

    if (pcHits)
        *pcHits = pArgs->cHits;

    /* Dereference the argument structure. */
    int32_t cRefs = ASMAtomicDecS32(&pArgs->cRefs);
    Assert(cRefs >= 0);
    if (cRefs == 0)
        RTMemFree(pArgs);

    return VINF_SUCCESS;
}


RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    if (g_pfnrtKeIpiGenericCall)
        return rtMpCallUsingBroadcastIpi(pfnWorker, pvUser1, pvUser2, rtmpNtOnAllBroadcastIpiWrapper,
                                         NIL_RTCPUID, NIL_RTCPUID, NULL);
    return rtMpCallUsingDpcs(pfnWorker, pvUser1, pvUser2, RT_NT_CPUID_ALL, NIL_RTCPUID, NIL_RTCPUID, NULL);
}


RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    if (g_pfnrtKeIpiGenericCall)
        return rtMpCallUsingBroadcastIpi(pfnWorker, pvUser1, pvUser2, rtmpNtOnOthersBroadcastIpiWrapper,
                                         NIL_RTCPUID, NIL_RTCPUID, NULL);
    return rtMpCallUsingDpcs(pfnWorker, pvUser1, pvUser2, RT_NT_CPUID_OTHERS, NIL_RTCPUID, NIL_RTCPUID, NULL);
}


RTDECL(int) RTMpOnPair(RTCPUID idCpu1, RTCPUID idCpu2, uint32_t fFlags, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    int rc;
    AssertReturn(idCpu1 != idCpu2, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & RTMPON_F_VALID_MASK), VERR_INVALID_FLAGS);
    if ((fFlags & RTMPON_F_CONCURRENT_EXEC) && !g_pfnrtKeIpiGenericCall)
        return VERR_NOT_SUPPORTED;

    /*
     * Check that both CPUs are online before doing the broadcast call.
     */
    if (   RTMpIsCpuOnline(idCpu1)
        && RTMpIsCpuOnline(idCpu2))
    {
        /*
         * The broadcast IPI isn't quite as bad as it could have been, because
         * it looks like windows doesn't synchronize CPUs on the way out, they
         * seems to get back to normal work while the pair is still busy.
         */
        uint32_t cHits = 0;
        if (g_pfnrtKeIpiGenericCall)
            rc = rtMpCallUsingBroadcastIpi(pfnWorker, pvUser1, pvUser2, rtmpNtOnPairBroadcastIpiWrapper, idCpu1, idCpu2, &cHits);
        else
            rc = rtMpCallUsingDpcs(pfnWorker, pvUser1, pvUser2, RT_NT_CPUID_PAIR, idCpu1, idCpu2, &cHits);
        if (RT_SUCCESS(rc))
        {
            Assert(cHits <= 2);
            if (cHits == 2)
                rc = VINF_SUCCESS;
            else if (cHits == 1)
                rc = VERR_NOT_ALL_CPUS_SHOWED;
            else if (cHits == 0)
                rc = VERR_CPU_OFFLINE;
            else
                rc = VERR_CPU_IPE_1;
        }
    }
    /*
     * A CPU must be present to be considered just offline.
     */
    else if (   RTMpIsCpuPresent(idCpu1)
             && RTMpIsCpuPresent(idCpu2))
        rc = VERR_CPU_OFFLINE;
    else
        rc = VERR_CPU_NOT_FOUND;
    return rc;
}


RTDECL(bool) RTMpOnPairIsConcurrentExecSupported(void)
{
    return g_pfnrtKeIpiGenericCall != NULL;
}


/**
 * Releases a reference to a RTMPNTONSPECIFICARGS heap allocation, freeing it
 * when the last reference is released.
 */
DECLINLINE(void) rtMpNtOnSpecificRelease(PRTMPNTONSPECIFICARGS pArgs)
{
    uint32_t cRefs = ASMAtomicDecU32(&pArgs->cRefs);
    AssertMsg(cRefs <= 1, ("cRefs=%#x\n", cRefs));
    if (cRefs == 0)
        RTMemFree(pArgs);
}


/**
 * Wrapper between the native nt per-cpu callbacks and PFNRTWORKER
 *
 * @param   Dpc                 DPC object
 * @param   DeferredContext     Context argument specified by KeInitializeDpc
 * @param   SystemArgument1     Argument specified by KeInsertQueueDpc
 * @param   SystemArgument2     Argument specified by KeInsertQueueDpc
 */
static VOID rtMpNtOnSpecificDpcWrapper(IN PKDPC Dpc, IN PVOID DeferredContext,
                                       IN PVOID SystemArgument1, IN PVOID SystemArgument2)
{
    PRTMPNTONSPECIFICARGS pArgs = (PRTMPNTONSPECIFICARGS)DeferredContext;
    RT_NOREF3(Dpc, SystemArgument1, SystemArgument2);

    ASMAtomicWriteBool(&pArgs->fExecuting, true);

    pArgs->CallbackArgs.pfnWorker(RTMpCpuId(), pArgs->CallbackArgs.pvUser1, pArgs->CallbackArgs.pvUser2);

    ASMAtomicWriteBool(&pArgs->fDone, true);
    KeSetEvent(&pArgs->DoneEvt, 1 /*PriorityIncrement*/, FALSE /*Wait*/);

    rtMpNtOnSpecificRelease(pArgs);
}


RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    /*
     * Don't try mess with an offline CPU.
     */
    if (!RTMpIsCpuOnline(idCpu))
        return !RTMpIsCpuPossible(idCpu)
              ? VERR_CPU_NOT_FOUND
              : VERR_CPU_OFFLINE;

    /*
     * Use the broadcast IPI routine if there are no more than two CPUs online,
     * or if the current IRQL is unsuitable for KeWaitForSingleObject.
     */
    int rc;
    uint32_t cHits = 0;
    if (   g_pfnrtKeIpiGenericCall
        && (   RTMpGetOnlineCount() <= 2
            || KeGetCurrentIrql()   > APC_LEVEL)
       )
    {
        rc = rtMpCallUsingBroadcastIpi(pfnWorker, pvUser1, pvUser2, rtmpNtOnSpecificBroadcastIpiWrapper,
                                       idCpu, NIL_RTCPUID, &cHits);
        if (RT_SUCCESS(rc))
        {
            if (cHits == 1)
                return VINF_SUCCESS;
            rc = cHits == 0 ? VERR_CPU_OFFLINE : VERR_CPU_IPE_1;
        }
        return rc;
    }

#if 0
    rc = rtMpCallUsingDpcs(pfnWorker, pvUser1, pvUser2, RT_NT_CPUID_SPECIFIC, idCpu, NIL_RTCPUID, &cHits);
    if (RT_SUCCESS(rc))
    {
        if (cHits == 1)
            return VINF_SUCCESS;
        rc = cHits == 0 ? VERR_CPU_OFFLINE : VERR_CPU_IPE_1;
    }
    return rc;

#else
    /*
     * Initialize the argument package and the objects within it.
     * The package is referenced counted to avoid unnecessary spinning to
     * synchronize cleanup and prevent stack corruption.
     */
    PRTMPNTONSPECIFICARGS pArgs = (PRTMPNTONSPECIFICARGS)RTMemAllocZ(sizeof(*pArgs));
    if (!pArgs)
        return VERR_NO_MEMORY;
    pArgs->cRefs                  = 2;
    pArgs->fExecuting             = false;
    pArgs->fDone                  = false;
    pArgs->CallbackArgs.pfnWorker = pfnWorker;
    pArgs->CallbackArgs.pvUser1   = pvUser1;
    pArgs->CallbackArgs.pvUser2   = pvUser2;
    pArgs->CallbackArgs.idCpu     = idCpu;
    pArgs->CallbackArgs.cHits     = 0;
    pArgs->CallbackArgs.cRefs     = 2;
    KeInitializeEvent(&pArgs->DoneEvt, SynchronizationEvent, FALSE /* not signalled */);
    KeInitializeDpc(&pArgs->Dpc, rtMpNtOnSpecificDpcWrapper, pArgs);
    if (g_pfnrtKeSetImportanceDpc)
        g_pfnrtKeSetImportanceDpc(&pArgs->Dpc, HighImportance);
    rc = rtMpNtSetTargetProcessorDpc(&pArgs->Dpc, idCpu);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pArgs);
        return rc;
    }

    /*
     * Disable preemption while we check the current processor and inserts the DPC.
     */
    KIRQL bOldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &bOldIrql);
    ASMCompilerBarrier(); /* paranoia */

    if (RTMpCpuId() == idCpu)
    {
        /* Just execute the callback on the current CPU. */
        pfnWorker(idCpu, pvUser1, pvUser2);
        KeLowerIrql(bOldIrql);

        RTMemFree(pArgs);
        return VINF_SUCCESS;
    }

    /* Different CPU, so queue it if the CPU is still online. */
    if (RTMpIsCpuOnline(idCpu))
    {
        BOOLEAN fRc = KeInsertQueueDpc(&pArgs->Dpc, 0, 0);
        Assert(fRc); NOREF(fRc);
        KeLowerIrql(bOldIrql);

        uint64_t const nsRealWaitTS = RTTimeNanoTS();

        /*
         * Wait actively for a while in case the CPU/thread responds quickly.
         */
        uint32_t cLoopsLeft = 0x20000;
        while (cLoopsLeft-- > 0)
        {
            if (pArgs->fDone)
            {
                rtMpNtOnSpecificRelease(pArgs);
                return VINF_SUCCESS;
            }
            ASMNopPause();
        }

        /*
         * It didn't respond, so wait on the event object, poking the CPU if it's slow.
         */
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = -10000; /* 1ms */
        NTSTATUS rcNt = KeWaitForSingleObject(&pArgs->DoneEvt, Executive, KernelMode, FALSE /* Alertable */, &Timeout);
        if (rcNt == STATUS_SUCCESS)
        {
            rtMpNtOnSpecificRelease(pArgs);
            return VINF_SUCCESS;
        }

        /* If it hasn't respondend yet, maybe poke it and wait some more. */
        if (rcNt == STATUS_TIMEOUT)
        {
            if (   !pArgs->fExecuting
                && (   g_pfnrtMpPokeCpuWorker == rtMpPokeCpuUsingHalRequestIpiW7Plus
                    || g_pfnrtMpPokeCpuWorker == rtMpPokeCpuUsingHalRequestIpiPreW7))
                RTMpPokeCpu(idCpu);

            Timeout.QuadPart = -1280000; /* 128ms */
            rcNt = KeWaitForSingleObject(&pArgs->DoneEvt, Executive, KernelMode, FALSE /* Alertable */, &Timeout);
            if (rcNt == STATUS_SUCCESS)
            {
                rtMpNtOnSpecificRelease(pArgs);
                return VINF_SUCCESS;
            }
        }

        /*
         * Something weird is happening, try bail out.
         */
        if (KeRemoveQueueDpc(&pArgs->Dpc))
        {
            RTMemFree(pArgs); /* DPC was still queued, so we can return without further ado. */
            LogRel(("RTMpOnSpecific(%#x): Not processed after %llu ns: rcNt=%#x\n", idCpu, RTTimeNanoTS() - nsRealWaitTS, rcNt));
        }
        else
        {
            /* DPC is running, wait a good while for it to complete. */
            LogRel(("RTMpOnSpecific(%#x): Still running after %llu ns: rcNt=%#x\n", idCpu, RTTimeNanoTS() - nsRealWaitTS, rcNt));

            Timeout.QuadPart = -30*1000*1000*10; /* 30 seconds */
            rcNt = KeWaitForSingleObject(&pArgs->DoneEvt, Executive, KernelMode, FALSE /* Alertable */, &Timeout);
            if (rcNt != STATUS_SUCCESS)
                LogRel(("RTMpOnSpecific(%#x): Giving up on running worker after %llu ns: rcNt=%#x\n", idCpu, RTTimeNanoTS() - nsRealWaitTS, rcNt));
        }
        rc = RTErrConvertFromNtStatus(rcNt);
    }
    else
    {
        /* CPU is offline.*/
        KeLowerIrql(bOldIrql);
        rc = !RTMpIsCpuPossible(idCpu) ? VERR_CPU_NOT_FOUND : VERR_CPU_OFFLINE;
    }

    rtMpNtOnSpecificRelease(pArgs);
    return rc;
#endif
}




static VOID rtMpNtPokeCpuDummy(IN PKDPC Dpc, IN PVOID DeferredContext, IN PVOID SystemArgument1, IN PVOID SystemArgument2)
{
    NOREF(Dpc);
    NOREF(DeferredContext);
    NOREF(SystemArgument1);
    NOREF(SystemArgument2);
}


/** Callback used by rtMpPokeCpuUsingBroadcastIpi. */
static ULONG_PTR rtMpIpiGenericCall(ULONG_PTR Argument)
{
    NOREF(Argument);
    return 0;
}


/**
 * RTMpPokeCpu worker that uses broadcast IPIs for doing the work.
 *
 * @returns VINF_SUCCESS
 * @param   idCpu           The CPU identifier.
 */
int rtMpPokeCpuUsingBroadcastIpi(RTCPUID idCpu)
{
    NOREF(idCpu);
    g_pfnrtKeIpiGenericCall(rtMpIpiGenericCall, 0);
    return VINF_SUCCESS;
}


/**
 * RTMpPokeCpu worker that uses the Windows 7 and later version of
 * HalRequestIpip to get the job done.
 *
 * @returns VINF_SUCCESS
 * @param   idCpu           The CPU identifier.
 */
int rtMpPokeCpuUsingHalRequestIpiW7Plus(RTCPUID idCpu)
{
    /* idCpu is an HAL processor index, so we can use it directly. */
    PKAFFINITY_EX pTarget = (PKAFFINITY_EX)alloca(g_cbRtMpNtKaffinityEx);
    pTarget->Size = g_cRtMpNtKaffinityExEntries; /* (just in case KeInitializeAffinityEx starts using it) */
    g_pfnrtKeInitializeAffinityEx(pTarget);
    g_pfnrtKeAddProcessorAffinityEx(pTarget, idCpu);

    g_pfnrtHalRequestIpiW7Plus(0, pTarget);
    return VINF_SUCCESS;
}


/**
 * RTMpPokeCpu worker that uses the Vista and earlier version of HalRequestIpip
 * to get the job done.
 *
 * @returns VINF_SUCCESS
 * @param   idCpu           The CPU identifier.
 */
int rtMpPokeCpuUsingHalRequestIpiPreW7(RTCPUID idCpu)
{
    __debugbreak(); /** @todo this code needs testing!!  */
    KAFFINITY Target = 1;
    Target <<= idCpu;
    g_pfnrtHalRequestIpiPreW7(Target);
    return VINF_SUCCESS;
}


int rtMpPokeCpuUsingFailureNotSupported(RTCPUID idCpu)
{
    NOREF(idCpu);
    return VERR_NOT_SUPPORTED;
}


int rtMpPokeCpuUsingDpc(RTCPUID idCpu)
{
    Assert(g_cRtMpNtMaxCpus > 0 && g_cRtMpNtMaxGroups > 0); /* init order */

    /*
     * APC fallback.
     */
    static KDPC s_aPokeDpcs[RTCPUSET_MAX_CPUS] = {{0}};
    static bool s_fPokeDPCsInitialized = false;

    if (!s_fPokeDPCsInitialized)
    {
        for (unsigned i = 0; i < g_cRtMpNtMaxCpus; i++)
        {
            KeInitializeDpc(&s_aPokeDpcs[i], rtMpNtPokeCpuDummy, NULL);
            if (g_pfnrtKeSetImportanceDpc)
                g_pfnrtKeSetImportanceDpc(&s_aPokeDpcs[i], HighImportance);
            int rc = rtMpNtSetTargetProcessorDpc(&s_aPokeDpcs[i], idCpu);
            if (RT_FAILURE(rc) && rc != VERR_CPU_NOT_FOUND)
                return rc;
        }

        s_fPokeDPCsInitialized = true;
    }

    /* Raise the IRQL to DISPATCH_LEVEL so we can't be rescheduled to another cpu.
       KeInsertQueueDpc must also be executed at IRQL >= DISPATCH_LEVEL. */
    KIRQL oldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);

    if (g_pfnrtKeSetImportanceDpc)
        g_pfnrtKeSetImportanceDpc(&s_aPokeDpcs[idCpu], HighImportance);
    g_pfnrtKeSetTargetProcessorDpc(&s_aPokeDpcs[idCpu], (int)idCpu);

    /* Assuming here that high importance DPCs will be delivered immediately; or at least an IPI will be sent immediately.
       Note! Not true on at least Vista & Windows 7 */
    BOOLEAN fRet = KeInsertQueueDpc(&s_aPokeDpcs[idCpu], 0, 0);

    KeLowerIrql(oldIrql);
    return fRet == TRUE ? VINF_SUCCESS : VERR_ACCESS_DENIED /* already queued */;
}


RTDECL(int) RTMpPokeCpu(RTCPUID idCpu)
{
    if (!RTMpIsCpuOnline(idCpu))
        return !RTMpIsCpuPossible(idCpu)
              ? VERR_CPU_NOT_FOUND
              : VERR_CPU_OFFLINE;
    /* Calls rtMpPokeCpuUsingDpc, rtMpPokeCpuUsingHalRequestIpiW7Plus or rtMpPokeCpuUsingBroadcastIpi. */
    return g_pfnrtMpPokeCpuWorker(idCpu);
}


RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    return false;
}

