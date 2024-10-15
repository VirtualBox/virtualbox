/* $Id: time-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time, Windows.
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
#include "internal-r3-nt.h"

#include <iprt/time.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/uint128.h>
#include "internal/time.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Whether we've tried to resolve g_pfnRtlGetSystemTimePrecise or not. */
static bool                         g_fInitialized = false;
/** Pointer to RtlGetSystemTimePrecise, added in 6.2 (windows 8).   */
static PFNRTLGETSYSTEMTIMEPRECISE   g_pfnRtlGetSystemTimePrecise = NULL;


/**
 * Initializes globals.
 */
static void rtTimeNtInitialize(void)
{
    /*
     * Make sure we don't recurse here when calling into RTLdr.
     */
    if (ASMAtomicCmpXchgBool(&g_fInitialized, true, false))
    {
        void *pvFunc = RTLdrGetSystemSymbol("ntdll.dll", "RtlGetSystemTimePrecise");
        if (pvFunc)
            ASMAtomicWritePtr((void * volatile *)&g_pfnRtlGetSystemTimePrecise, pvFunc);
        ASMCompilerBarrier();
    }
}


static uint64_t rtTimeGetSystemNanoTS(void)
{
    if (RT_UNLIKELY(!g_fInitialized))
        rtTimeNtInitialize();

    KUSER_SHARED_DATA volatile *pUserSharedData = (KUSER_SHARED_DATA volatile *)MM_SHARED_USER_DATA_VA;

#if 1
    /*
     * If there is precise time, get the precise system time and calculate the
     * interrupt time from it.  (Microsoft doesn't expose interrupt time to user
     * application, which is very unfortunate as there are a lot place where
     * monotonic time is applicable but developer is "forced" to use wall clock.)
     */
    if (g_pfnRtlGetSystemTimePrecise)
    {
        for (;;)
        {
            uint64_t uUpdateLockBefore;
            while ((uUpdateLockBefore = pUserSharedData->TimeUpdateLock) & 1)
                ASMNopPause();

            uint64_t        uInterruptTime                  = *(uint64_t volatile *)&pUserSharedData->InterruptTime;
            uint64_t        uBaselineInterruptTimeQpc       = pUserSharedData->BaselineInterruptTimeQpc;
            uint64_t        uQpcInterruptTimeIncrement      = pUserSharedData->QpcInterruptTimeIncrement;
            uint8_t         uQpcInterruptTimeIncrementShift = pUserSharedData->QpcInterruptTimeIncrementShift;
            LARGE_INTEGER   QpcValue;
            RtlQueryPerformanceCounter(&QpcValue);

            if (pUserSharedData->TimeUpdateLock == uUpdateLockBefore)
            {
                uint64_t uQpcValue = QpcValue.QuadPart;
                if (uQpcValue <= uBaselineInterruptTimeQpc)
                    return uInterruptTime * 100;

                /* Calc QPC delta since base line. */
                uQpcValue -= uBaselineInterruptTimeQpc;
                uQpcValue--;

                /* Multiply by 10 million. */
                uQpcValue *= UINT32_C(10000000);

                /* Multiply by QPC interrupt time increment value. */
                RTUINT128U Tmp128;
                RTUInt128MulU64ByU64(&Tmp128, uQpcValue, uQpcInterruptTimeIncrement);

                /* Shift the upper 64 bits by the increment shift factor. */
                uint64_t uResult = Tmp128.s.Hi >> uQpcInterruptTimeIncrementShift;

                /* Add to base interrupt time value. */
                uResult += uInterruptTime;

                /* Convert from NT unit to nano seconds. */
                return uResult * 100;
            }

            ASMNopPause();
        }
    }
#endif

    /*
     * Just read interrupt time.
     */
#if ARCH_BITS >= 64
    uint64_t uRet = *(uint64_t volatile *)&pUserSharedData->InterruptTime; /* This is what KeQueryInterruptTime does. */
    uRet *= 100;
    return uRet;
#else

    LARGE_INTEGER NtTime;
    do
    {
        NtTime.HighPart = pUserSharedData->InterruptTime.High1Time;
        NtTime.LowPart  = pUserSharedData->InterruptTime.LowPart;
    } while (pUserSharedData->InterruptTime.High2Time != NtTime.HighPart);

    return (uint64_t)NtTime.QuadPart * 100;
#endif
}


RTDECL(uint64_t) RTTimeSystemNanoTS(void)
{
    return rtTimeGetSystemNanoTS();
}


RTDECL(uint64_t) RTTimeSystemMilliTS(void)
{
    return rtTimeGetSystemNanoTS() / RT_NS_1MS;
}


RTDECL(PRTTIMESPEC) RTTimeNow(PRTTIMESPEC pTime)
{
    /*
     * Get the precise time if possible.
     */
    if (RT_UNLIKELY(!g_fInitialized))
        rtTimeNtInitialize();
    if (g_pfnRtlGetSystemTimePrecise != NULL)
        return RTTimeSpecSetNtTime(pTime, g_pfnRtlGetSystemTimePrecise());

    /*
     * Just read system time.
     */
    KUSER_SHARED_DATA volatile *pUserSharedData = (KUSER_SHARED_DATA volatile *)MM_SHARED_USER_DATA_VA;
#ifdef RT_ARCH_AMD64
    uint64_t uRet = *(uint64_t volatile *)&pUserSharedData->SystemTime; /* This is what KeQuerySystemTime does. */
    return RTTimeSpecSetNtTime(pTime, uRet);
#else

    LARGE_INTEGER NtTime;
    do
    {
        NtTime.HighPart = pUserSharedData->SystemTime.High1Time;
        NtTime.LowPart  = pUserSharedData->SystemTime.LowPart;
    } while (pUserSharedData->SystemTime.High2Time != NtTime.HighPart);
    return RTTimeSpecSetNtTime(pTime, NtTime.QuadPart);
#endif
}


RTDECL(PRTTIMESPEC) RTTimeLocalNow(PRTTIMESPEC pTime)
{
    return RTTimeSpecAddNano(RTTimeNow(pTime), RTTimeLocalDeltaNano());
}


RTDECL(int64_t) RTTimeLocalDeltaNano(void)
{
    /*
     * UTC = local + TimeZoneBias; The bias is given in NT units.
     */
    KUSER_SHARED_DATA volatile *pUserSharedData = (KUSER_SHARED_DATA volatile *)MM_SHARED_USER_DATA_VA;
    LARGE_INTEGER               Delta;
#if ARCH_BITS == 64
    Delta.QuadPart = *(int64_t volatile *)&pUserSharedData->TimeZoneBias;
#else
    do
    {
        Delta.HighPart = pUserSharedData->TimeZoneBias.High1Time;
        Delta.LowPart  = pUserSharedData->TimeZoneBias.LowPart;
    } while (pUserSharedData->TimeZoneBias.High2Time != Delta.HighPart);
#endif
    return Delta.QuadPart * -100;
}

