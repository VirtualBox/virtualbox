/* $Id: thread-r0drv-haiku.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads, Ring-0 Driver, Haiku.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include "the-haiku-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
#include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mp.h>


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)find_thread(NULL);
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    RT_ASSERT_PREEMPTIBLE();
    snooze((bigtime_t)cMillies * 1000);
    return VINF_SUCCESS;
}


RTDECL(bool) RTThreadYield(void)
{
    RT_ASSERT_PREEMPTIBLE();
    //FIXME
    //snooze(0);
    thread_yield(true);
    return true; /* this is fishy */
}


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);

    //XXX: can't do this, it might actually be held by another cpu
    //return !B_SPINLOCK_IS_LOCKED(&gThreadSpinlock);
    return ASMIntAreEnabled(); /** @todo find a better way. */
}


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);
    /** @todo check if Thread::next_priority or
     *        cpu_ent::invoke_scheduler could do. */
    return false;
}


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* RTThreadPreemptIsPending is not reliable yet. */
    return false;
}


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    /* yes, kernel preemption is possible. */
    return true;
}


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->uOldCpuState == 0);

    pState->uOldCpuState = (uint32_t)disable_interrupts();
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);
}


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);

    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);
    restore_interrupts((cpu_status)pState->uOldCpuState);
    pState->uOldCpuState = 0;
}


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);
    /** @todo Implement RTThreadIsInInterrupt. Required for guest
     *        additions! */
    return !ASMIntAreEnabled();
}

