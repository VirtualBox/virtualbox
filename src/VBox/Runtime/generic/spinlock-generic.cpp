/* $Id: spinlock-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Spinlock, generic implementation.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
 * Force cpu yields after spinning the number of times indicated by the define.
 * If 0 we will spin forever. */
#define RT_CFG_SPINLOCK_GENERIC_DO_SLEEP    100000


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/spinlock.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/errcore.h>
#include <iprt/assert.h>
#if RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
# include <iprt/thread.h>
#endif

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Generic spinlock structure.
 */
typedef struct RTSPINLOCKINTERNAL
{
    /** Spinlock magic value (RTSPINLOCK_GEN_MAGIC). */
    uint32_t            u32Magic;
    /** The spinlock creation flags. */
    uint32_t            fFlags;
    /** The spinlock. */
    uint32_t volatile   fLocked;
    /** The saved CPU interrupt. */
    uint32_t volatile   fIntSaved;
} RTSPINLOCKINTERNAL, *PRTSPINLOCKINTERNAL;


RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock, uint32_t fFlags, const char *pszName)
{
    PRTSPINLOCKINTERNAL pThis;
    AssertReturn(fFlags == RTSPINLOCK_FLAGS_INTERRUPT_SAFE || fFlags == RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, VERR_INVALID_PARAMETER);
    RT_NOREF_PV(pszName);

    /*
     * Allocate.
     */
    pThis = (PRTSPINLOCKINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    /*
     * Initialize and return.
     */
    pThis->u32Magic  = RTSPINLOCK_GEN_MAGIC;
    pThis->fFlags    = fFlags;
    pThis->fIntSaved = 0;
    ASMAtomicWriteU32(&pThis->fLocked, 0);

    *pSpinlock = pThis;
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
    if (pThis->u32Magic != RTSPINLOCK_GEN_MAGIC)
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
    AssertMsg(pThis && pThis->u32Magic == RTSPINLOCK_GEN_MAGIC,
              ("pThis=%p u32Magic=%08x\n", pThis, pThis ? (int)pThis->u32Magic : 0));

    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        uint32_t fIntSaved = ASMGetFlags();
#endif

#if RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
        for (;;)
        {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
            ASMIntDisable();
#endif
            for (int c = RT_CFG_SPINLOCK_GENERIC_DO_SLEEP; c > 0; c--)
            {
                if (ASMAtomicCmpXchgU32(&pThis->fLocked, 1, 0))
                {
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
                    pThis->fIntSaved = fIntSaved;
# endif
                    return;
                }
                ASMNopPause();
            }
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
            ASMSetFlags(fIntSaved);
#endif
            RTThreadYield();
        }
#else
        for (;;)
        {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
            ASMIntDisable();
#endif
            if (ASMAtomicCmpXchgU32(&pThis->fLocked, 1, 0))
            {
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
                pThis->fIntSaved = fIntSaved;
# endif
                return;
            }
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
            ASMSetFlags(fIntSaved);
#endif
            ASMNopPause();
        }
#endif
    }
    else
    {
#if RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
        for (;;)
        {
            for (int c = RT_CFG_SPINLOCK_GENERIC_DO_SLEEP; c > 0; c--)
            {
                if (ASMAtomicCmpXchgU32(&pThis->fLocked, 1, 0))
                    return;
                ASMNopPause();
            }
            RTThreadYield();
        }
#else
        while (!ASMAtomicCmpXchgU32(&pThis->fLocked, 1, 0))
            ASMNopPause();
#endif
    }
}
RT_EXPORT_SYMBOL(RTSpinlockAcquire);


RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock)
{
    PRTSPINLOCKINTERNAL pThis = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertMsg(pThis && pThis->u32Magic == RTSPINLOCK_GEN_MAGIC,
              ("pThis=%p u32Magic=%08x\n", pThis, pThis ? (int)pThis->u32Magic : 0));

    if (pThis->fFlags & RTSPINLOCK_FLAGS_INTERRUPT_SAFE)
    {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        uint32_t fIntSaved = pThis->fIntSaved;
        pThis->fIntSaved   = 0;
#endif

        if (!ASMAtomicCmpXchgU32(&pThis->fLocked, 0, 1))
            AssertMsgFailed(("Spinlock %p was not locked!\n", pThis));

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        ASMSetFlags(fIntSaved);
#endif
    }
    else
    {
        if (!ASMAtomicCmpXchgU32(&pThis->fLocked, 0, 1))
            AssertMsgFailed(("Spinlock %p was not locked!\n", pThis));
    }
}
RT_EXPORT_SYMBOL(RTSpinlockRelease);

