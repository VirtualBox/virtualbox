/* $Id: threadctxhooks-r0drv-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Thread Context Switching Hook, Ring-0 Driver, Linux.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include "internal/thread.h"


/*
 * Linux kernel 2.6.23 introduced preemption notifiers but RedHat 2.6.18 kernels
 * got it backported.
 */
#if RTLNX_VER_MIN(2,6,18) && defined(CONFIG_PREEMPT_NOTIFIERS)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal hook object for linux.
 */
typedef struct RTTHREADCTXHOOKINT
{
    /** Magic value (RTTHREADCTXHOOKINT_MAGIC). */
    uint32_t volatile           u32Magic;
    /** The thread handle (owner) for which the hook is registered. */
    RTNATIVETHREAD              hOwner;
    /** The preemption notifier object. */
    struct preempt_notifier     LnxPreemptNotifier;
    /** Whether the hook is enabled or not.  If enabled, the LnxPreemptNotifier
     * is linked into the owning thread's list of preemption callouts. */
    bool                        fEnabled;
    /** Pointer to the user callback. */
    PFNRTTHREADCTXHOOK          pfnCallback;
    /** User argument passed to the callback. */
    void                       *pvUser;
    /** The linux callbacks. */
    struct preempt_ops          PreemptOps;
#if RTLNX_VER_MIN(3,1,19) && defined(RT_ARCH_AMD64)
    /** Starting with 3.1.19, the linux kernel doesn't restore kernel RFLAGS during
     * task switch, so we have to do that ourselves. (x86 code is not affected.) */
    RTCCUINTREG                 fSavedRFlags;
#endif
} RTTHREADCTXHOOKINT;
typedef RTTHREADCTXHOOKINT *PRTTHREADCTXHOOKINT;


/**
 * Hook function for the thread schedule out event.
 *
 * @param   pPreemptNotifier    Pointer to the preempt_notifier struct.
 * @param   pNext               Pointer to the task that is being scheduled
 *                              instead of the current thread.
 *
 * @remarks Called with the rq (runqueue) lock held and with preemption and
 *          interrupts disabled!
 */
static void rtThreadCtxHooksLnxSchedOut(struct preempt_notifier *pPreemptNotifier, struct task_struct *pNext)
{
    PRTTHREADCTXHOOKINT pThis = RT_FROM_MEMBER(pPreemptNotifier, RTTHREADCTXHOOKINT, LnxPreemptNotifier);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    RTCCUINTREG fSavedEFlags = ASMGetFlags();
    stac();
#endif
    RT_NOREF_PV(pNext);

    AssertPtr(pThis);
    AssertPtr(pThis->pfnCallback);
    Assert(pThis->fEnabled);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    pThis->pfnCallback(RTTHREADCTXEVENT_OUT, pThis->pvUser);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    ASMSetFlags(fSavedEFlags);
# if RTLNX_VER_MIN(3,1,19) && defined(RT_ARCH_AMD64)
    pThis->fSavedRFlags = fSavedEFlags;
# endif
#endif
}


/**
 * Hook function for the thread schedule in event.
 *
 * @param   pPreemptNotifier    Pointer to the preempt_notifier struct.
 * @param   iCpu                The CPU this thread is being scheduled on.
 *
 * @remarks Called without holding the rq (runqueue) lock and with preemption
 *          enabled!
 * @todo    r=bird: Preemption is of course disabled when it is called.
 */
static void rtThreadCtxHooksLnxSchedIn(struct preempt_notifier *pPreemptNotifier, int iCpu)
{
    PRTTHREADCTXHOOKINT pThis = RT_FROM_MEMBER(pPreemptNotifier, RTTHREADCTXHOOKINT, LnxPreemptNotifier);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    RTCCUINTREG fSavedEFlags = ASMGetFlags();
    stac();
#endif
    RT_NOREF_PV(iCpu);

    AssertPtr(pThis);
    AssertPtr(pThis->pfnCallback);
    Assert(pThis->fEnabled);

    pThis->pfnCallback(RTTHREADCTXEVENT_IN, pThis->pvUser);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# if RTLNX_VER_MIN(3,1,19) && defined(RT_ARCH_AMD64)
    fSavedEFlags &= ~RT_BIT_64(18) /*X86_EFL_AC*/;
    fSavedEFlags |= pThis->fSavedRFlags & RT_BIT_64(18) /*X86_EFL_AC*/;
# endif
    ASMSetFlags(fSavedEFlags);
#endif
}


/**
 * Worker function for RTThreadCtxHooks(Deregister|Release)().
 *
 * @param   pThis   Pointer to the internal thread-context object.
 */
DECLINLINE(void) rtThreadCtxHookDisable(PRTTHREADCTXHOOKINT pThis)
{
    Assert(pThis->PreemptOps.sched_out == rtThreadCtxHooksLnxSchedOut);
    Assert(pThis->PreemptOps.sched_in  == rtThreadCtxHooksLnxSchedIn);
    preempt_disable();
    preempt_notifier_unregister(&pThis->LnxPreemptNotifier);
    pThis->fEnabled = false;
    preempt_enable();
}


RTDECL(int) RTThreadCtxHookCreate(PRTTHREADCTXHOOK phCtxHook, uint32_t fFlags, PFNRTTHREADCTXHOOK pfnCallback, void *pvUser)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis;
    Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);

    /*
     * Allocate and initialize a new hook.  We don't register it yet, just
     * create it.
     */
    pThis = (PRTTHREADCTXHOOKINT)RTMemAllocZ(sizeof(*pThis));
    if (RT_UNLIKELY(!pThis))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }
    pThis->u32Magic     = RTTHREADCTXHOOKINT_MAGIC;
    pThis->hOwner       = RTThreadNativeSelf();
    pThis->fEnabled     = false;
    pThis->pfnCallback  = pfnCallback;
    pThis->pvUser       = pvUser;
    preempt_notifier_init(&pThis->LnxPreemptNotifier, &pThis->PreemptOps);
    pThis->PreemptOps.sched_out = rtThreadCtxHooksLnxSchedOut;
    pThis->PreemptOps.sched_in  = rtThreadCtxHooksLnxSchedIn;

#if RTLNX_VER_MIN(4,2,0)
    preempt_notifier_inc();
#endif

    *phCtxHook = pThis;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTThreadCtxHookCreate);


RTDECL(int ) RTThreadCtxHookDestroy(RTTHREADCTXHOOK hCtxHook)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    if (pThis == NIL_RTTHREADCTXHOOK)
        return VINF_SUCCESS;
    AssertPtr(pThis);
    AssertMsgReturn(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis),
                    VERR_INVALID_HANDLE);
    Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(!pThis->fEnabled || pThis->hOwner == RTThreadNativeSelf());

    /*
     * If there's still a registered thread-context hook, deregister it now before destroying the object.
     */
    if (pThis->fEnabled)
    {
        Assert(pThis->hOwner == RTThreadNativeSelf());
        rtThreadCtxHookDisable(pThis);
        Assert(!pThis->fEnabled); /* paranoia */
    }

#if RTLNX_VER_MIN(4,2,0)
    preempt_notifier_dec();
#endif

    ASMAtomicWriteU32(&pThis->u32Magic, ~RTTHREADCTXHOOKINT_MAGIC);
    RTMemFree(pThis);

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTThreadCtxHookDestroy);


RTDECL(int) RTThreadCtxHookEnable(RTTHREADCTXHOOK hCtxHook)
{
    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    AssertPtr(pThis);
    AssertMsgReturn(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis),
                    VERR_INVALID_HANDLE);
    Assert(pThis->hOwner == RTThreadNativeSelf());
    Assert(!pThis->fEnabled);
    if (!pThis->fEnabled)
    {
        IPRT_LINUX_SAVE_EFL_AC();
        Assert(pThis->PreemptOps.sched_out == rtThreadCtxHooksLnxSchedOut);
        Assert(pThis->PreemptOps.sched_in == rtThreadCtxHooksLnxSchedIn);

        /*
         * Register the callback.
         */
        preempt_disable();
        pThis->fEnabled = true;
        preempt_notifier_register(&pThis->LnxPreemptNotifier);
        preempt_enable();

        IPRT_LINUX_RESTORE_EFL_AC();
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTThreadCtxHookEnable);


RTDECL(int) RTThreadCtxHookDisable(RTTHREADCTXHOOK hCtxHook)
{
    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    if (pThis != NIL_RTTHREADCTXHOOK)
    {
        AssertPtr(pThis);
        AssertMsgReturn(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis),
                        VERR_INVALID_HANDLE);
        Assert(pThis->hOwner == RTThreadNativeSelf());

        /*
         * Deregister the callback.
         */
        if (pThis->fEnabled)
        {
            IPRT_LINUX_SAVE_EFL_AC();
            rtThreadCtxHookDisable(pThis);
            IPRT_LINUX_RESTORE_EFL_AC();
        }
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTThreadCtxHookDisable);


RTDECL(bool) RTThreadCtxHookIsEnabled(RTTHREADCTXHOOK hCtxHook)
{
    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    if (pThis == NIL_RTTHREADCTXHOOK)
        return false;
    AssertPtr(pThis);
    AssertMsgReturn(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis),
                    false);

    return pThis->fEnabled;
}
RT_EXPORT_SYMBOL(RTThreadCtxHookIsEnabled);

#else    /* Not supported / Not needed */
# include "../generic/threadctxhooks-r0drv-generic.cpp"
#endif   /* Not supported / Not needed */

