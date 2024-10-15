/* $Id: spinlock-r0drv-haiku.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Spinlocks, Ring-0 Driver, Haiku.
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
#include <iprt/spinlock.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
#include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/thread.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Wrapper for the KSPIN_LOCK type.
 */
typedef struct RTSPINLOCKINTERNAL
{
    /** Spinlock magic value (RTSPINLOCK_MAGIC). */
    uint32_t volatile     u32Magic;
    /** Spinlock creation flags */
    uint32_t              fFlags;
    /** Saved interrupt CPU status. */
    cpu_status volatile   fIntSaved;
    /** The Haiku spinlock structure. */
    spinlock              hSpinLock;
} RTSPINLOCKINTERNAL, *PRTSPINLOCKINTERNAL;



RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock, uint32_t fFlags, const char *pszName)
{
    RT_ASSERT_PREEMPTIBLE();
    NOREF(pszName);

    /*
     * Allocate.
     */
    AssertCompile(sizeof(RTSPINLOCKINTERNAL) > sizeof(void *));
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)RTMemAllocZ(sizeof(*pSpinlockInt));
    if (RT_UNLIKELY(!pSpinlockInt))
        return VERR_NO_MEMORY;

    /*
     * Initialize & return.
     */
    pSpinlockInt->u32Magic  = RTSPINLOCK_MAGIC;
    pSpinlockInt->fFlags    = fFlags;
    pSpinlockInt->fIntSaved = 0;
    B_INITIALIZE_SPINLOCK(&pSpinlockInt->hSpinLock);

    *pSpinlock = pSpinlockInt;
    return VINF_SUCCESS;
}


RTDECL(int)  RTSpinlockDestroy(RTSPINLOCK Spinlock)
{
    /*
     * Validate input.
     */
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    if (RT_UNLIKELY(!pSpinlockInt))
        return VERR_INVALID_PARAMETER;
    AssertMsgReturn(pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC,
                    ("Invalid spinlock %p magic=%#x\n", pSpinlockInt, pSpinlockInt->u32Magic),
                    VERR_INVALID_PARAMETER);

    /*
     * Make the lock invalid and release the memory.
     */
    ASMAtomicIncU32(&pSpinlockInt->u32Magic);

    B_INITIALIZE_SPINLOCK(&pSpinlockInt->hSpinLock);

    RTMemFree(pSpinlockInt);
    return VINF_SUCCESS;
}


RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertPtr(pSpinlockInt);
    Assert(pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC);

    /* Haiku cannot take spinlocks without disabling interrupts. Ignore our spinlock creation flags. */
    pSpinlockInt->fIntSaved = disable_interrupts();
    acquire_spinlock(&pSpinlockInt->hSpinLock);
}


RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertPtr(pSpinlockInt);
    Assert(pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC);

    release_spinlock(&pSpinlockInt->hSpinLock);
    restore_interrupts(pSpinlockInt->fIntSaved);
}

