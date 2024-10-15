/* $Id: spinlock-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Spinlocks, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/spinlock.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
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
    uint32_t volatile   u32Magic;
    /** Saved interrupt flag. */
    uint32_t volatile   fIntSaved;
    /** Creation flags. */
    uint32_t            fFlags;
    /** The Darwin spinlock structure. */
    lck_spin_t         *pSpinLock;
    /** The spinlock name. */
    const char         *pszName;
} RTSPINLOCKINTERNAL, *PRTSPINLOCKINTERNAL;



RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock, uint32_t fFlags, const char *pszName)
{
    RT_ASSERT_PREEMPTIBLE();
    AssertReturn(fFlags == RTSPINLOCK_FLAGS_INTERRUPT_SAFE || fFlags == RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, VERR_INVALID_PARAMETER);
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Allocate.
     */
    AssertCompile(sizeof(RTSPINLOCKINTERNAL) > sizeof(void *));
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        /*
         * Initialize & return.
         */
        pThis->u32Magic  = RTSPINLOCK_MAGIC;
        pThis->fIntSaved = 0;
        pThis->fFlags    = fFlags;
        pThis->pszName   = pszName;
        Assert(g_pDarwinLockGroup);
        pThis->pSpinLock = lck_spin_alloc_init(g_pDarwinLockGroup, LCK_ATTR_NULL);
        if (pThis->pSpinLock)
        {
            *pSpinlock = pThis;
            IPRT_DARWIN_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }

        RTMemFree(pThis);
    }
    IPRT_DARWIN_RESTORE_EFL_AC();
    return VERR_NO_MEMORY;
}


RTDECL(int)  RTSpinlockDestroy(RTSPINLOCK Spinlock)
{
    /*
     * Validate input.
     */
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
    IPRT_DARWIN_SAVE_EFL_AC();

    Assert(g_pDarwinLockGroup);
    lck_spin_free(pThis->pSpinLock, g_pDarwinLockGroup);
    pThis->pSpinLock = NULL;

    RTMemFree(pThis);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertPtr(pThis);
    Assert(pThis->u32Magic == RTSPINLOCK_MAGIC);

    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
        uint32_t fIntSaved = ASMGetFlags();
        ASMIntDisable();
        lck_spin_lock(pThis->pSpinLock);
        pThis->fIntSaved = fIntSaved;
        IPRT_DARWIN_RESTORE_EFL_ONLY_AC_EX(fIntSaved);
    }
    else
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        lck_spin_lock(pThis->pSpinLock);
        IPRT_DARWIN_RESTORE_EFL_ONLY_AC();
    }
}


RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertPtr(pThis);
    Assert(pThis->u32Magic == RTSPINLOCK_MAGIC);

    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
        uint32_t fIntSaved = pThis->fIntSaved;
        pThis->fIntSaved = 0;
        lck_spin_unlock(pThis->pSpinLock);
        ASMSetFlags(fIntSaved);
    }
    else
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        lck_spin_unlock(pThis->pSpinLock);
        IPRT_DARWIN_RESTORE_EFL_ONLY_AC();
    }
}

