/* $Id: tstRTR0ThreadPreemption.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Thread Preemption.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/thread.h>

#include <iprt/asm-amd64-x86.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#include <iprt/string.h>
#include <VBox/sup.h>
#include "tstRTR0ThreadPreemption.h"


#define TSTRTR0THREADCTXDATA_MAGIC      0xc01a50da

/**
 * Thread-context hook data.
 */
typedef struct TSTRTR0THREADCTXDATA
{
    uint32_t volatile   u32Magic;
    RTCPUID             uSourceCpuId;
    RTNATIVETHREAD      hSourceThread;

    /* For RTTHREADCTXEVENT_PREEMPTING. */
    bool                fPreemptingSuccess;
    volatile bool       fPreemptingInvoked;

    /* For RTTHREADCTXEVENT_RESUMED. */
    bool                fResumedSuccess;
    volatile bool       fResumedInvoked;

    char                achResult[512];
} TSTRTR0THREADCTXDATA, *PTSTRTR0THREADCTXDATA;


/**
 * Thread-context hook function.
 *
 * @param   enmEvent    The thread-context event.
 * @param   pvUser      Pointer to the user argument.
 */
static DECLCALLBACK(void) tstRTR0ThreadCtxHook(RTTHREADCTXEVENT enmEvent, void *pvUser)
{
    PTSTRTR0THREADCTXDATA pData = (PTSTRTR0THREADCTXDATA)pvUser;
    AssertPtrReturnVoid(pData);

    if (pData->u32Magic != TSTRTR0THREADCTXDATA_MAGIC)
    {
        RTStrPrintf(pData->achResult, sizeof(pData->achResult), "!tstRTR0ThreadCtxHook: Invalid magic.");
        return;
    }

    switch (enmEvent)
    {
        case RTTHREADCTXEVENT_OUT:
        {
            ASMAtomicWriteBool(&pData->fPreemptingInvoked, true);

            /* We've already been called once, we now might very well be on another CPU. Nothing to do here. */
            if (pData->fPreemptingSuccess)
                return;

            if (RTThreadPreemptIsEnabled(NIL_RTTHREAD))
            {
                RTStrPrintf(pData->achResult, sizeof(pData->achResult),
                            "!tstRTR0ThreadCtxHook[RTTHREADCTXEVENT_PREEMPTING]: Called with preemption enabled");
                break;
            }

            RTNATIVETHREAD hCurrentThread = RTThreadNativeSelf();
            if (pData->hSourceThread != hCurrentThread)
            {
                RTStrPrintf(pData->achResult, sizeof(pData->achResult),
                            "!tstRTR0ThreadCtxHook[RTTHREADCTXEVENT_PREEMPTING]: Thread switched! Source=%RTnthrd Current=%RTnthrd.",
                            pData->hSourceThread, hCurrentThread);
                break;
            }

            RTCPUID uCurrentCpuId = RTMpCpuId();
            if (pData->uSourceCpuId != uCurrentCpuId)
            {
                RTStrPrintf(pData->achResult, sizeof(pData->achResult),
                            "!tstRTR0ThreadCtxHook[RTTHREADCTXEVENT_PREEMPTING]: migrated uSourceCpuId=%RU32 uCurrentCpuId=%RU32",
                            pData->uSourceCpuId, uCurrentCpuId);
                break;
            }

            pData->fPreemptingSuccess = true;
            break;
        }

        case RTTHREADCTXEVENT_IN:
        {
            ASMAtomicWriteBool(&pData->fResumedInvoked, true);

            /* We've already been called once successfully, nothing more to do. */
            if (ASMAtomicReadBool(&pData->fResumedSuccess))
                return;

            if (!pData->fPreemptingSuccess)
            {
                RTStrPrintf(pData->achResult, sizeof(pData->achResult),
                            "!tstRTR0ThreadCtxHook[RTTHREADCTXEVENT_RESUMED]: Called before preempting callback was invoked.");
                break;
            }

            RTNATIVETHREAD hCurrentThread = RTThreadNativeSelf();
            if (pData->hSourceThread != hCurrentThread)
            {
                RTStrPrintf(pData->achResult, sizeof(pData->achResult),
                            "!tstRTR0ThreadCtxHook[RTTHREADCTXEVENT_RESUMED]: Thread switched! Source=%RTnthrd Current=%RTnthrd.",
                            pData->hSourceThread, hCurrentThread);
                break;
            }

            ASMAtomicWriteBool(&pData->fResumedSuccess, true);
            break;
        }

        default:
            AssertMsgFailed(("Invalid event %#x\n", enmEvent));
            break;
    }
}


/**
 * Service request callback function.
 *
 * @returns VBox status code.
 * @param   pSession    The caller's session.
 * @param   u64Arg      64-bit integer argument.
 * @param   pReqHdr     The request header. Input / Output. Optional.
 */
DECLEXPORT(int) TSTRTR0ThreadPreemptionSrvReqHandler(PSUPDRVSESSION pSession, uint32_t uOperation,
                                                     uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr)
{
    NOREF(pSession);
    if (u64Arg)
        return VERR_INVALID_PARAMETER;
    if (!RT_VALID_PTR(pReqHdr))
        return VERR_INVALID_PARAMETER;
    char   *pszErr = (char *)(pReqHdr + 1);
    size_t  cchErr = pReqHdr->cbReq - sizeof(*pReqHdr);
    if (cchErr < 32 || cchErr >= 0x10000)
        return VERR_INVALID_PARAMETER;
    *pszErr = '\0';

    /*
     * The big switch.
     */
    switch (uOperation)
    {
        case TSTRTR0THREADPREEMPTION_SANITY_OK:
            break;

        case TSTRTR0THREADPREEMPTION_SANITY_FAILURE:
            RTStrPrintf(pszErr, cchErr, "!42failure42%1024s", "");
            break;

        case TSTRTR0THREADPREEMPTION_BASIC:
        {
            if (!ASMIntAreEnabled())
                RTStrPrintf(pszErr, cchErr, "!Interrupts disabled");
            else if (!RTThreadPreemptIsEnabled(NIL_RTTHREAD))
                RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns false by default");
            else
            {
                RTTHREADPREEMPTSTATE State = RTTHREADPREEMPTSTATE_INITIALIZER;
                RTThreadPreemptDisable(&State);
                if (RTThreadPreemptIsEnabled(NIL_RTTHREAD))
                    RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns true after RTThreadPreemptDisable");
                else if (!ASMIntAreEnabled())
                    RTStrPrintf(pszErr, cchErr, "!Interrupts disabled");
                RTThreadPreemptRestore(&State);
            }
            break;
        }

        case TSTRTR0THREADPREEMPTION_IS_TRUSTY:
            if (!RTThreadPreemptIsPendingTrusty())
                RTStrPrintf(pszErr, cchErr, "!Untrusty");
            break;

        case TSTRTR0THREADPREEMPTION_IS_PENDING:
        {
            RTTHREADPREEMPTSTATE State = RTTHREADPREEMPTSTATE_INITIALIZER;
            RTThreadPreemptDisable(&State);
            if (!RTThreadPreemptIsEnabled(NIL_RTTHREAD))
            {
#ifdef RT_OS_DARWIN
                uint64_t const cNsMax = UINT64_C(8)*1000U*1000U*1000U;
#else
                uint64_t const cNsMax = UINT64_C(2)*1000U*1000U*1000U;
#endif
                if (ASMIntAreEnabled())
                {
                    uint64_t    u64StartTS    = RTTimeNanoTS();
                    uint64_t    u64StartSysTS = RTTimeSystemNanoTS();
                    uint64_t    cLoops        = 0;
                    uint64_t    cNanosSysElapsed;
                    uint64_t    cNanosElapsed;
                    bool        fPending;
                    do
                    {
                        fPending         = RTThreadPreemptIsPending(NIL_RTTHREAD);
                        cNanosElapsed    = RTTimeNanoTS()       - u64StartTS;
                        cNanosSysElapsed = RTTimeSystemNanoTS() - u64StartSysTS;
                        cLoops++;
                    } while (   !fPending
                             && cNanosElapsed    < cNsMax
                             && cNanosSysElapsed < cNsMax
                             && cLoops           < 100U*_1M);
                    if (!fPending)
                        RTStrPrintf(pszErr, cchErr, "!Preempt not pending after %'llu loops / %'llu ns / %'llu ns (sys)",
                                    cLoops, cNanosElapsed, cNanosSysElapsed);
                    else if (cLoops == 1)
                        RTStrPrintf(pszErr, cchErr, "!cLoops=1\n");
                    else
                        RTStrPrintf(pszErr, cchErr, "RTThreadPreemptIsPending returned true after %'llu loops / %'llu ns / %'llu ns (sys)",
                                    cLoops, cNanosElapsed, cNanosSysElapsed);
                }
                else
                    RTStrPrintf(pszErr, cchErr, "!Interrupts disabled");
            }
            else
                RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns true after RTThreadPreemptDisable");
            RTThreadPreemptRestore(&State);
            break;
        }

        case TSTRTR0THREADPREEMPTION_NESTED:
        {
            bool const fDefault = RTThreadPreemptIsEnabled(NIL_RTTHREAD);
            RTTHREADPREEMPTSTATE State1 = RTTHREADPREEMPTSTATE_INITIALIZER;
            RTThreadPreemptDisable(&State1);
            if (!RTThreadPreemptIsEnabled(NIL_RTTHREAD))
            {
                RTTHREADPREEMPTSTATE State2 = RTTHREADPREEMPTSTATE_INITIALIZER;
                RTThreadPreemptDisable(&State2);
                if (!RTThreadPreemptIsEnabled(NIL_RTTHREAD))
                {
                    RTTHREADPREEMPTSTATE State3 = RTTHREADPREEMPTSTATE_INITIALIZER;
                    RTThreadPreemptDisable(&State3);
                    if (RTThreadPreemptIsEnabled(NIL_RTTHREAD))
                        RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns true after 3rd RTThreadPreemptDisable");

                    RTThreadPreemptRestore(&State3);
                    if (RTThreadPreemptIsEnabled(NIL_RTTHREAD) && !*pszErr)
                        RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns true after 1st RTThreadPreemptRestore");
                }
                else
                    RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns true after 2nd RTThreadPreemptDisable");

                RTThreadPreemptRestore(&State2);
                if (RTThreadPreemptIsEnabled(NIL_RTTHREAD) && !*pszErr)
                    RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns true after 2nd RTThreadPreemptRestore");
            }
            else
                RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns true after 1st RTThreadPreemptDisable");
            RTThreadPreemptRestore(&State1);
            if (RTThreadPreemptIsEnabled(NIL_RTTHREAD) != fDefault && !*pszErr)
                RTStrPrintf(pszErr, cchErr, "!RTThreadPreemptIsEnabled returns false after 3rd RTThreadPreemptRestore");
            break;
        }

        case TSTRTR0THREADPREEMPTION_CTXHOOKS:
        {
            if (!RTThreadPreemptIsEnabled(NIL_RTTHREAD))
            {
                RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHooksCreate must be called with preemption enabled");
                break;
            }

            bool fRegistered = RTThreadCtxHookIsEnabled(NIL_RTTHREADCTXHOOK);
            if (fRegistered)
            {
                RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHookIsEnabled returns true before creating any hooks");
                break;
            }

            PTSTRTR0THREADCTXDATA pCtxData = (PTSTRTR0THREADCTXDATA)RTMemAllocZ(sizeof(*pCtxData));
            AssertReturn(pCtxData, VERR_NO_MEMORY);
            pCtxData->u32Magic           = TSTRTR0THREADCTXDATA_MAGIC;
            pCtxData->fPreemptingSuccess = false;
            pCtxData->fPreemptingInvoked = false;
            pCtxData->fResumedInvoked    = false;
            pCtxData->fResumedSuccess    = false;
            pCtxData->hSourceThread      = RTThreadNativeSelf();
            RT_ZERO(pCtxData->achResult);

            RTTHREADCTXHOOK hThreadCtx;
            int rc = RTThreadCtxHookCreate(&hThreadCtx, 0, tstRTR0ThreadCtxHook, pCtxData);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_NOT_SUPPORTED)
                    RTStrPrintf(pszErr, cchErr, "RTThreadCtxHooksCreate returns VERR_NOT_SUPPORTED");
                else
                    RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHooksCreate returns %Rrc", rc);
                RTMemFree(pCtxData);
                break;
            }

            fRegistered = RTThreadCtxHookIsEnabled(hThreadCtx);
            if (fRegistered)
            {
                RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHookIsEnabled returns true before registering any hooks");
                RTThreadCtxHookDestroy(hThreadCtx);
                break;
            }

            RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
            RTThreadPreemptDisable(&PreemptState);
            Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

            pCtxData->uSourceCpuId       = RTMpCpuId();

            rc = RTThreadCtxHookEnable(hThreadCtx);
            if (RT_FAILURE(rc))
            {
                RTThreadPreemptRestore(&PreemptState);
                RTMemFree(pCtxData);
                RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHookEnable returns %Rrc", rc);
                break;
            }

            fRegistered = RTThreadCtxHookIsEnabled(hThreadCtx);
            if (!fRegistered)
            {
                RTThreadPreemptRestore(&PreemptState);
                RTThreadCtxHookDestroy(hThreadCtx);
                RTMemFree(pCtxData);
                RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHookIsEnabled return false when hooks are supposed to be enabled");
                break;
            }

            RTThreadPreemptRestore(&PreemptState);

            /* Check if the preempting callback has/will been invoked. */
            const uint32_t  cMsTimeout           = 10000;
            const uint32_t  cMsSleepGranularity  = 50;
            uint32_t        cMsSlept             = 0;
            RTCPUID         uCurrentCpuId        = NIL_RTCPUID;
            for (;;)
            {
                RTThreadYield();
                RTThreadPreemptDisable(&PreemptState);
                uCurrentCpuId = RTMpCpuId();
                RTThreadPreemptRestore(&PreemptState);

                if (   pCtxData->uSourceCpuId != uCurrentCpuId
                    || cMsSlept >= cMsTimeout)
                {
                    break;
                }

                RTThreadSleep(cMsSleepGranularity);
                cMsSlept += cMsSleepGranularity;
            }

            if (!ASMAtomicReadBool(&pCtxData->fPreemptingInvoked))
            {
                if (pCtxData->uSourceCpuId != uCurrentCpuId)
                {
                    RTStrPrintf(pszErr, cchErr,
                                "!tstRTR0ThreadCtxHooks[RTTHREADCTXEVENT_OUT] not invoked before migrating from CPU %RU32 to %RU32",
                                pCtxData->uSourceCpuId, uCurrentCpuId);
                }
                else
                {
                    RTStrPrintf(pszErr, cchErr, "!tstRTR0ThreadCtxHooks[RTTHREADCTXEVENT_OUT] not invoked after ca. %u ms",
                                cMsSlept);
                }
            }
            else if (!pCtxData->fPreemptingSuccess)
                RTStrCopy(pszErr, cchErr, pCtxData->achResult);
            else
            {
                /* Preempting callback succeeded, now check if the resumed callback has/will been invoked. */
                cMsSlept = 0;
                for (;;)
                {
                    if (   ASMAtomicReadBool(&pCtxData->fResumedInvoked)
                        || cMsSlept >= cMsTimeout)
                    {
                        break;
                    }

                    RTThreadSleep(cMsSleepGranularity);
                    cMsSlept += cMsSleepGranularity;
                }

                if (!ASMAtomicReadBool(&pCtxData->fResumedInvoked))
                {
                    RTStrPrintf(pszErr, cchErr, "!tstRTR0ThreadCtxHooks[RTTHREADCTXEVENT_IN] not invoked after ca. %u ms",
                                cMsSlept);
                }
                else if (!pCtxData->fResumedSuccess)
                    RTStrCopy(pszErr, cchErr, pCtxData->achResult);
            }

            rc = RTThreadCtxHookDisable(hThreadCtx);
            if (RT_SUCCESS(rc))
            {
                fRegistered = RTThreadCtxHookIsEnabled(hThreadCtx);
                if (fRegistered)
                {
                    RTThreadCtxHookDestroy(hThreadCtx);
                    RTMemFree(pCtxData);
                    RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHookIsEnabled return true when hooks are disabled");
                    break;
                }
            }
            else
                RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHookDisable failed, returns %Rrc!", rc);

            Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));
            rc = RTThreadCtxHookDestroy(hThreadCtx);
            if (RT_FAILURE(rc))
                RTStrPrintf(pszErr, cchErr, "!RTThreadCtxHooksRelease returns %Rrc!", rc);

            RTMemFree(pCtxData);
            break;
        }

        default:
            RTStrPrintf(pszErr, cchErr, "!Unknown test #%d", uOperation);
            break;
    }

    /* The error indicator is the '!' in the message buffer. */
    return VINF_SUCCESS;
}

