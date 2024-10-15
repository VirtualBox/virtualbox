/* $Id: spinlock-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Spinlocks, Ring-0 Driver, Solaris.
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
#include "the-solaris-kernel.h"
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
 * Wrapper for the struct mutex type.
 */
typedef struct RTSPINLOCKINTERNAL
{
    /** Spinlock magic value (RTSPINLOCK_MAGIC). */
    uint32_t volatile   u32Magic;
    /** Spinlock creation flags.  */
    uint32_t            fFlags;
    /** Saved interrupt flag. */
    uint32_t volatile   fIntSaved;
    /** A Solaris spinlock. */
    kmutex_t            Mtx;
#ifdef RT_MORE_STRICT
    /** The idAssertCpu variable before acquring the lock for asserting after
     *  releasing the spinlock. */
    RTCPUID volatile    idAssertCpu;
    /** The CPU that owns the lock. */
    RTCPUID volatile    idCpuOwner;
#endif
} RTSPINLOCKINTERNAL, *PRTSPINLOCKINTERNAL;



RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock, uint32_t fFlags, const char *pszName)
{
    RT_ASSERT_PREEMPTIBLE();
    AssertReturn(fFlags == RTSPINLOCK_FLAGS_INTERRUPT_SAFE || fFlags == RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, VERR_INVALID_PARAMETER);

    /*
     * Allocate.
     */
    AssertCompile(sizeof(RTSPINLOCKINTERNAL) > sizeof(void *));
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    /*
     * Initialize & return.
     */
    pThis->u32Magic  = RTSPINLOCK_MAGIC;
    pThis->fFlags    = fFlags;
    pThis->fIntSaved = 0;
    /** @todo Consider different PIL when not interrupt safe requirement. */
    mutex_init(&pThis->Mtx, "IPRT Spinlock", MUTEX_SPIN, (void *)ipltospl(PIL_MAX));
    *pSpinlock = pThis;
    return VINF_SUCCESS;
}


RTDECL(int)  RTSpinlockDestroy(RTSPINLOCK Spinlock)
{
    /*
     * Validate input.
     */
    RT_ASSERT_INTS_ON();
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertMsgReturn(pThis->u32Magic == RTSPINLOCK_MAGIC,
                    ("Invalid spinlock %p magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_PARAMETER);

    /*
     * Make the lock invalid and release the memory.
     */
    ASMAtomicIncU32(&pThis->u32Magic);
    mutex_destroy(&pThis->Mtx);
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    RT_ASSERT_PREEMPT_CPUID_VAR();
    AssertPtr(pThis);
    Assert(pThis->u32Magic == RTSPINLOCK_MAGIC);

    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        uint32_t fIntSaved = ASMIntDisableFlags();
#endif
        mutex_enter(&pThis->Mtx);

        /*
         * Solaris 10 doesn't preserve the interrupt flag, but since we're at PIL_MAX we should be
         * fine and not get interrupts while lock is held. Re-disable interrupts to not upset
         * assertions & assumptions callers might have.
         */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        ASMIntDisable();
#endif

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        Assert(!ASMIntAreEnabled());
#endif
        pThis->fIntSaved = fIntSaved;
    }
    else
    {
#if defined(RT_STRICT) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
        bool fIntsOn = ASMIntAreEnabled();
#endif

        mutex_enter(&pThis->Mtx);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        AssertMsg(fIntsOn == ASMIntAreEnabled(), ("fIntsOn=%RTbool\n", fIntsOn));
#endif
    }

    RT_ASSERT_PREEMPT_CPUID_SPIN_ACQUIRED(pThis);
}


RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE_VARS();

    AssertPtr(pThis);
    Assert(pThis->u32Magic == RTSPINLOCK_MAGIC);
    RT_ASSERT_PREEMPT_CPUID_SPIN_RELEASE(pThis);

    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        uint32_t fIntSaved = pThis->fIntSaved;
        pThis->fIntSaved = 0;
#endif
        mutex_exit(&pThis->Mtx);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        ASMSetFlags(fIntSaved);
#endif
    }
    else
    {
#if defined(RT_STRICT) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
        bool fIntsOn = ASMIntAreEnabled();
#endif

        mutex_exit(&pThis->Mtx);

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        AssertMsg(fIntsOn == ASMIntAreEnabled(), ("fIntsOn=%RTbool\n", fIntsOn));
#endif
    }

    RT_ASSERT_PREEMPT_CPUID();
}

