/* $Id: spinlock-r0drv-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Spinlocks, Ring-0 Driver, Linux.
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
#include <iprt/spinlock.h>

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/thread.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Wrapper for the spinlock_t structure.
 */
typedef struct RTSPINLOCKINTERNAL
{
    /** Spinlock magic value (RTSPINLOCK_MAGIC). */
    uint32_t volatile       u32Magic;
    /** The spinlock creation flags.  */
    uint32_t                fFlags;
    /** The saved interrupt flag. */
    unsigned long volatile  fIntSaved;
    /** The linux spinlock structure. */
    spinlock_t              Spinlock;
#ifdef RT_MORE_STRICT
    /** The idAssertCpu variable before acquring the lock for asserting after
     *  releasing the spinlock. */
    RTCPUID volatile        idAssertCpu;
    /** The CPU that owns the lock. */
    RTCPUID volatile        idCpuOwner;
#endif
} RTSPINLOCKINTERNAL, *PRTSPINLOCKINTERNAL;



RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock, uint32_t fFlags, const char *pszName)
{
    IPRT_LINUX_SAVE_EFL_AC();
    PRTSPINLOCKINTERNAL pThis;
    AssertReturn(fFlags == RTSPINLOCK_FLAGS_INTERRUPT_SAFE || fFlags == RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, VERR_INVALID_PARAMETER);
    RT_NOREF_PV(pszName);

    /*
     * Allocate.
     */
    Assert(sizeof(RTSPINLOCKINTERNAL) > sizeof(void *));
    pThis = (PRTSPINLOCKINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    /*
     * Initialize and return.
     */
    pThis->u32Magic     = RTSPINLOCK_MAGIC;
    pThis->fFlags       = fFlags;
    pThis->fIntSaved    = 0;
#ifdef RT_MORE_STRICT
    pThis->idCpuOwner   = NIL_RTCPUID;
    pThis->idAssertCpu  = NIL_RTCPUID;
#endif

    spin_lock_init(&pThis->Spinlock);

    *pSpinlock = pThis;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSpinlockCreate);


RTDECL(int)  RTSpinlockDestroy(RTSPINLOCK Spinlock)
{
    /*
     * Validate input.
     */
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    if (pThis->u32Magic != RTSPINLOCK_MAGIC)
    {
        AssertMsgFailed(("Invalid spinlock %p magic=%#x\n", pThis, pThis->u32Magic));
        return VERR_INVALID_PARAMETER;
    }

    ASMAtomicIncU32(&pThis->u32Magic);
    RTMemFree(pThis);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSpinlockDestroy);


RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    IPRT_LINUX_SAVE_EFL_AC();
    RT_ASSERT_PREEMPT_CPUID_VAR();
    AssertMsg(pThis && pThis->u32Magic == RTSPINLOCK_MAGIC,
              ("pThis=%p u32Magic=%08x\n", pThis, pThis ? (int)pThis->u32Magic : 0));

#ifdef CONFIG_PROVE_LOCKING
    lockdep_off();
#endif
    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
        unsigned long fIntSaved;
        spin_lock_irqsave(&pThis->Spinlock, fIntSaved);
        pThis->fIntSaved = fIntSaved;
    }
    else
        spin_lock(&pThis->Spinlock);
#ifdef CONFIG_PROVE_LOCKING
    lockdep_on();
#endif

    IPRT_LINUX_RESTORE_EFL_ONLY_AC();
    RT_ASSERT_PREEMPT_CPUID_SPIN_ACQUIRED(pThis);
}
RT_EXPORT_SYMBOL(RTSpinlockAcquire);


RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    IPRT_LINUX_SAVE_EFL_AC();           /* spin_unlock* may preempt and trash eflags.ac. */
    RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE_VARS();
    AssertMsg(pThis && pThis->u32Magic == RTSPINLOCK_MAGIC,
              ("pThis=%p u32Magic=%08x\n", pThis, pThis ? (int)pThis->u32Magic : 0));
    RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE(pThis);

#ifdef CONFIG_PROVE_LOCKING
    lockdep_off();
#endif
    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
        unsigned long fIntSaved = pThis->fIntSaved;
        pThis->fIntSaved = 0;
        spin_unlock_irqrestore(&pThis->Spinlock, fIntSaved);
    }
    else
        spin_unlock(&pThis->Spinlock);
#ifdef CONFIG_PROVE_LOCKING
    lockdep_on();
#endif

    IPRT_LINUX_RESTORE_EFL_ONLY_AC();
    RT_ASSERT_PREEMPT_CPUID();
}
RT_EXPORT_SYMBOL(RTSpinlockRelease);

