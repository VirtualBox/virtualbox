/* $Id: thread-r0drv-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads, Ring-0 Driver, Linux.
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
#include "the-linux-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#include <iprt/asm.h>
#if RTLNX_VER_MAX(2,5,28) || defined(CONFIG_X86_SMAP)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mp.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if  defined(CONFIG_PREEMPT_COUNT) /* since 3.1 */ \
  || defined(CONFIG_PREEMPTION)    /* since 5.3 */ \
  || defined(CONFIG_PREEMPT)       /* since 2.6.13 - preemption model choice; before that arch specific choice back to 2.5.45. */
# define IPRT_LNX_HAVE_PREEMPTION
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef IPRT_LNX_HAVE_PREEMPTION
/** Per-cpu preemption counters. */
static int32_t volatile g_acPreemptDisabled[NR_CPUS];
#endif


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)current;
}
RT_EXPORT_SYMBOL(RTThreadNativeSelf);


static int rtR0ThreadLnxSleepCommon(RTMSINTERVAL cMillies)
{
    IPRT_LINUX_SAVE_EFL_AC();
    long cJiffies = msecs_to_jiffies(cMillies);
    set_current_state(TASK_INTERRUPTIBLE);
    cJiffies = schedule_timeout(cJiffies);
    IPRT_LINUX_RESTORE_EFL_AC();
    if (!cJiffies)
        return VINF_SUCCESS;
    return VERR_INTERRUPTED;
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    return rtR0ThreadLnxSleepCommon(cMillies);
}
RT_EXPORT_SYMBOL(RTThreadSleep);


RTDECL(int) RTThreadSleepNoLog(RTMSINTERVAL cMillies)
{
    return rtR0ThreadLnxSleepCommon(cMillies);
}
RT_EXPORT_SYMBOL(RTThreadSleepNoLog);


RTDECL(bool) RTThreadYield(void)
{
    IPRT_LINUX_SAVE_EFL_AC();
#if RTLNX_VER_MIN(2,4,20)
    yield();
#else
    /** @todo r=ramshankar: Can we use cond_resched() instead?  */
    set_current_state(TASK_RUNNING);
    sys_sched_yield();
    schedule();
#endif
    IPRT_LINUX_RESTORE_EFL_AC();
    return true;
}
RT_EXPORT_SYMBOL(RTThreadYield);


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
#ifdef IPRT_LNX_HAVE_PREEMPTION
    Assert(hThread == NIL_RTTHREAD); RT_NOREF_PV(hThread);
# ifdef preemptible
    return preemptible();
# else
    return preempt_count() == 0 && !in_atomic() && !irqs_disabled();
# endif
#else
    int32_t c;

    Assert(hThread == NIL_RTTHREAD);
    c = g_acPreemptDisabled[smp_processor_id()];
    AssertMsg(c >= 0 && c < 32, ("%d\n", c));
    if (c != 0)
        return false;
# if RTLNX_VER_MIN(2,5,32)
    if (in_atomic())
        return false;
# endif
# if RTLNX_VER_MIN(2,5,28)
    if (irqs_disabled())
        return false;
# else
    if (!ASMIntAreEnabled())
        return false;
# endif
    return true;
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsEnabled);


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); RT_NOREF_PV(hThread);
#if RTLNX_VER_MIN(2,5,4)
    return !!test_tsk_thread_flag(current, TIF_NEED_RESCHED);

#elif RTLNX_VER_MIN(2,4,20)
    return !!need_resched();

#elif RTLNX_VER_MIN(2,1,110)
    return current->need_resched != 0;

#else
    return need_resched != 0;
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsPending);


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* yes, RTThreadPreemptIsPending is reliable. */
    return true;
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsPendingTrusty);


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
#ifdef IPRT_LNX_HAVE_PREEMPTION
    return true;    /* Yes, kernel preemption is possible. */
#else
    return false;   /* No kernel preemption (or just CONFIG_PREEMPT_VOLUNTARY). */
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsPossible);


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
#ifdef IPRT_LNX_HAVE_PREEMPTION
    AssertPtr(pState);
    Assert(pState->u32Reserved == 0);
    pState->u32Reserved = 42;
    preempt_disable();
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);

#else /* !IPRT_LNX_HAVE_PREEMPTION */
    int32_t c;
    AssertPtr(pState);
    Assert(pState->u32Reserved == 0);

    /* Do our own accounting. */
    c = ASMAtomicIncS32(&g_acPreemptDisabled[smp_processor_id()]);
    AssertMsg(c > 0 && c < 32, ("%d\n", c));
    pState->u32Reserved = c;
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptDisable);


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
#ifdef IPRT_LNX_HAVE_PREEMPTION
    IPRT_LINUX_SAVE_EFL_AC(); /* paranoia */
    AssertPtr(pState);
    Assert(pState->u32Reserved == 42);
    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);
    preempt_enable();
    IPRT_LINUX_RESTORE_EFL_ONLY_AC();  /* paranoia */

#else
    int32_t volatile *pc;
    AssertPtr(pState);
    AssertMsg(pState->u32Reserved > 0 && pState->u32Reserved < 32, ("%d\n", pState->u32Reserved));
    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);

    /* Do our own accounting. */
    pc = &g_acPreemptDisabled[smp_processor_id()];
    AssertMsg(pState->u32Reserved == (uint32_t)*pc, ("u32Reserved=%d *pc=%d \n", pState->u32Reserved, *pc));
    ASMAtomicUoWriteS32(pc, pState->u32Reserved - 1);
#endif
    pState->u32Reserved = 0;
}
RT_EXPORT_SYMBOL(RTThreadPreemptRestore);


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);

    return in_interrupt() != 0;
}
RT_EXPORT_SYMBOL(RTThreadIsInInterrupt);


RTDECL(int) RTThreadQueryTerminationStatus(RTTHREAD hThread)
{
    struct task_struct *pTask = current;
    AssertReturn(hThread == NIL_RTTHREAD, VERR_NOT_SUPPORTED);

    /* Check out pending signals.  ASSUMES we can get away w/o locking
       anything because we're only reading the data.  */
    if (sigismember(&pTask->pending.signal, SIGKILL))
        return VINF_THREAD_IS_TERMINATING;

#if RTLNX_VER_MIN(2,5,34)
    /* Check the pending signals shared with other threads in
       the same process/group.  ASSUME since we're alive that
       the signal_struct won't be freed while we're looking
       at it here... */
    {
# if RTLNX_VER_MIN(2,5,60)
        struct signal_struct *pSignal = current->signal;
# else
        struct signal_struct *pSignal = current->sig;
# endif
        if (   pSignal
            && sigismember(&pSignal->shared_pending.signal, SIGKILL))
            return VINF_THREAD_IS_TERMINATING;
    }
#endif

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTThreadQueryTerminationStatus);

