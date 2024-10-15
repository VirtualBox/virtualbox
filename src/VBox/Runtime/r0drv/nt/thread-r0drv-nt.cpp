/* $Id: thread-r0drv-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads, Ring-0 Driver, NT.
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
#include "the-nt-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mp.h>
#include "internal-r0drv-nt.h"



RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)PsGetCurrentThread();
}


static int rtR0ThreadNtSleepCommon(RTMSINTERVAL cMillies)
{
    LARGE_INTEGER Interval;
    Interval.QuadPart = -(int64_t)cMillies * 10000;
    NTSTATUS rcNt = KeDelayExecutionThread(KernelMode, TRUE, &Interval);
    switch (rcNt)
    {
        case STATUS_SUCCESS:
            return VINF_SUCCESS;
        case STATUS_ALERTED:
        case STATUS_USER_APC:
            return VERR_INTERRUPTED;
        default:
            return RTErrConvertFromNtStatus(rcNt);
    }
}


RTDECL(int)   RTThreadSleep(RTMSINTERVAL cMillies)
{
    return rtR0ThreadNtSleepCommon(cMillies);
}


RTDECL(bool) RTThreadYield(void)
{
    return ZwYieldExecution() != STATUS_NO_YIELD_PERFORMED;
}


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); RT_NOREF1(hThread);
    KIRQL Irql = KeGetCurrentIrql();
    if (Irql > APC_LEVEL)
        return false;
    if (!ASMIntAreEnabled())
        return false;
    return true;
}


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); RT_NOREF1(hThread);

    /*
     * The KeShouldYieldProcessor API introduced in Windows 10 looks like exactly
     * what we want.  But of course there is a snag.  It may return with interrupts
     * enabled when called with them disabled.  Let's just hope it doesn't get upset
     * by disabled interrupts in other ways...
     */
    if (g_pfnrtKeShouldYieldProcessor)
    {
        RTCCUINTREG fSavedFlags = ASMGetFlags();
        bool fReturn = g_pfnrtKeShouldYieldProcessor() != FALSE;
        ASMSetFlags(fSavedFlags);
        return fReturn;
    }

    /*
     * Fallback approach for pre W10 kernels.
     *
     * If W10 is anything to go by, we should also check and yield when:
     *      - pPrcb->NextThread != NULL && pPrcb->NextThread != pPrcb->CurrentThread
     *        when QuantumEnd is zero.
     *      - pPrcb->DpcRequestSummary & 1
     *      - pPrcb->DpcRequestSummary & 0x1e
     */

    /*
     * Read the globals and check if they are useful.
     */
/** @todo Should we check KPRCB.InterruptRequest and KPRCB.DpcInterruptRequested (older kernels).  */
    uint32_t const offQuantumEnd     = g_offrtNtPbQuantumEnd;
    uint32_t const cbQuantumEnd      = g_cbrtNtPbQuantumEnd;
    uint32_t const offDpcQueueDepth  = g_offrtNtPbDpcQueueDepth;
    if (!offQuantumEnd && !cbQuantumEnd && !offDpcQueueDepth)
        return false;
    Assert((offQuantumEnd && cbQuantumEnd) || (!offQuantumEnd && !cbQuantumEnd));

    /*
     * Disable interrupts so we won't be messed around.
     */
    bool            fPending;
    RTCCUINTREG     fSavedFlags  = ASMIntDisableFlags();

#ifdef RT_ARCH_X86
    PKPCR       pPcr   = (PKPCR)__readfsdword(RT_UOFFSETOF(KPCR,SelfPcr));
    uint8_t    *pbPrcb = (uint8_t *)pPcr->Prcb;

#elif defined(RT_ARCH_AMD64)
    /* HACK ALERT! The offset is from windbg/vista64. */
    PKPCR       pPcr   = (PKPCR)__readgsqword(RT_UOFFSETOF(KPCR,Self));
    uint8_t    *pbPrcb = (uint8_t *)pPcr->CurrentPrcb;

#else
# error "port me"
#endif

    /* Check QuantumEnd. */
    if (cbQuantumEnd == 1)
    {
        uint8_t volatile *pbQuantumEnd = (uint8_t volatile *)(pbPrcb + offQuantumEnd);
        fPending = *pbQuantumEnd == TRUE;
    }
    else if (cbQuantumEnd == sizeof(uint32_t))
    {
        uint32_t volatile *pu32QuantumEnd = (uint32_t volatile *)(pbPrcb + offQuantumEnd);
        fPending = *pu32QuantumEnd != 0;
    }
    else
        fPending = false;

    /* Check DpcQueueDepth. */
    if (    !fPending
        &&  offDpcQueueDepth)
    {
        uint32_t volatile *pu32DpcQueueDepth = (uint32_t volatile *)(pbPrcb + offDpcQueueDepth);
        fPending = *pu32DpcQueueDepth > 0;
    }

    ASMSetFlags(fSavedFlags);
    return fPending;
}


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    if (g_pfnrtKeShouldYieldProcessor)
        return true;
#if 0 /** @todo RTThreadPreemptIsPending isn't good enough on w7 and possibly elsewhere. */
    /* RTThreadPreemptIsPending is only reliable if we've got both offsets and size. */
    return g_offrtNtPbQuantumEnd    != 0
        && g_cbrtNtPbQuantumEnd     != 0
        && g_offrtNtPbDpcQueueDepth != 0;
#else
    return false;
#endif
}


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    /* yes, kernel preemption is possible. */
    return true;
}


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->uchOldIrql == 255);
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeRaiseIrql(DISPATCH_LEVEL, &pState->uchOldIrql);
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);
}


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);

    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);
    KeLowerIrql(pState->uchOldIrql);
    pState->uchOldIrql = 255;
}


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);

    KIRQL CurIrql = KeGetCurrentIrql();
    return CurIrql > PASSIVE_LEVEL; /** @todo Is there a more correct way? */
}


RTDECL(int) RTThreadQueryTerminationStatus(RTTHREAD hThread)
{
    AssertReturn(hThread == NIL_RTTHREAD, VERR_INVALID_HANDLE);
    if (RT_LIKELY(g_pfnrtPsIsThreadTerminating))
    {
        BOOLEAN fRc = g_pfnrtPsIsThreadTerminating(PsGetCurrentThread());
        return !fRc ? VINF_SUCCESS : VINF_THREAD_IS_TERMINATING;
    }
    return VERR_NOT_SUPPORTED;
}

