/* $Id: threadpreempt-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Thread Preemption, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/errcore.h>
#include <iprt/mp.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTDARWINPREEMPTHACK
{
    /** The spinlock we exploit for disabling preemption. */
    lck_spin_t         *pSpinLock;
    /** The preemption count for this CPU, to guard against nested calls. */
    uint32_t            cRecursion;
} RTDARWINPREEMPTHACK;
typedef RTDARWINPREEMPTHACK *PRTDARWINPREEMPTHACK;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTDARWINPREEMPTHACK  g_aPreemptHacks[RTCPUSET_MAX_CPUS];


/**
 * Allocates the per-cpu spin locks used to disable preemption.
 *
 * Called by rtR0InitNative.
 */
int rtThreadPreemptDarwinInit(void)
{
    Assert(g_pDarwinLockGroup);
    IPRT_DARWIN_SAVE_EFL_AC();

    for (size_t i = 0; i < RT_ELEMENTS(g_aPreemptHacks); i++)
    {
        g_aPreemptHacks[i].pSpinLock = lck_spin_alloc_init(g_pDarwinLockGroup, LCK_ATTR_NULL);
        if (!g_aPreemptHacks[i].pSpinLock)
            return VERR_NO_MEMORY; /* (The caller will invoke rtThreadPreemptDarwinTerm) */
    }
    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


/**
 * Frees the per-cpu spin locks used to disable preemption.
 *
 * Called by rtR0TermNative.
 */
void rtThreadPreemptDarwinTerm(void)
{
    IPRT_DARWIN_SAVE_EFL_AC();

    for (size_t i = 0; i < RT_ELEMENTS(g_aPreemptHacks); i++)
        if (g_aPreemptHacks[i].pSpinLock)
        {
            lck_spin_free(g_aPreemptHacks[i].pSpinLock, g_pDarwinLockGroup);
            g_aPreemptHacks[i].pSpinLock = NULL;
        }

    IPRT_DARWIN_RESTORE_EFL_AC();
}


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    RT_NOREF(hThread);
    Assert(hThread == NIL_RTTHREAD);
    return preemption_enabled();
}


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    RT_NOREF(hThread);
    if (!g_pfnR0DarwinAstPending)
        return false;
    uint32_t volatile *pfAstPending = g_pfnR0DarwinAstPending(); AssertPtr(pfAstPending);
    uint32_t  const    fAstPending = *pfAstPending;

    AssertMsg(!(fAstPending & UINT32_C(0xfffe0000)), ("%#x\n", fAstPending));
    return (fAstPending & (AST_PREEMPT | AST_QUANTUM | AST_URGENT)) != 0;
}


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* yes, we think that RTThreadPreemptIsPending is reliable... */
    return g_pfnR0DarwinAstPending != NULL;
}


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    /* yes, kernel preemption is possible. */
    return true;
}


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->u32Reserved == 0);
    pState->u32Reserved = 42;

    /*
     * Disable to prevent preemption while we grab the per-cpu spin lock.
     * Note! Only take the lock on the first call or we end up spinning for ever.
     */
    RTCCUINTREG fSavedFlags = ASMIntDisableFlags();
    RTCPUID     idCpu       = RTMpCpuId();
    if (RT_UNLIKELY(idCpu < RT_ELEMENTS(g_aPreemptHacks)))
    {
        Assert(g_aPreemptHacks[idCpu].cRecursion < UINT32_MAX / 2);
        if (++g_aPreemptHacks[idCpu].cRecursion == 1)
        {
            lck_spin_t *pSpinLock = g_aPreemptHacks[idCpu].pSpinLock;
            if (pSpinLock)
                lck_spin_lock(pSpinLock);
            else
                AssertFailed();
        }
    }
    ASMSetFlags(fSavedFlags);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);
}


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->u32Reserved == 42);
    pState->u32Reserved = 0;
    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);

    RTCPUID idCpu = RTMpCpuId();
    if (RT_UNLIKELY(idCpu < RT_ELEMENTS(g_aPreemptHacks)))
    {
        Assert(g_aPreemptHacks[idCpu].cRecursion > 0);
        if (--g_aPreemptHacks[idCpu].cRecursion == 0)
        {
            lck_spin_t *pSpinLock = g_aPreemptHacks[idCpu].pSpinLock;
            if (pSpinLock)
            {
                IPRT_DARWIN_SAVE_EFL_AC();
                lck_spin_unlock(pSpinLock);
                IPRT_DARWIN_RESTORE_EFL_AC();
            }
            else
                AssertFailed();
        }
    }
}


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);
    /** @todo Darwin: Implement RTThreadIsInInterrupt. Required for guest
     *        additions! */
    return !ASMIntAreEnabled();
}

