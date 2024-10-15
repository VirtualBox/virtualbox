/* $Id: critsectrw-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Read/Write Critical Section, Generic.
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
#define RTCRITSECTRW_WITHOUT_REMAPPING
#define RTASSERT_QUIET
#include <iprt/critsect.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include "internal/magics.h"
#include "internal/strict.h"

/* Two issues here, (1) the tracepoint generator uses IPRT, and (2) only one .d
   file per module. */
#ifdef IPRT_WITH_DTRACE
# include IPRT_DTRACE_INCLUDE
# ifdef IPRT_DTRACE_PREFIX
#  define IPRT_CRITSECTRW_EXCL_ENTERED          RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_ENTERED)
#  define IPRT_CRITSECTRW_EXCL_ENTERED_ENABLED  RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_ENTERED_ENABLED)
#  define IPRT_CRITSECTRW_EXCL_LEAVING          RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_LEAVING)
#  define IPRT_CRITSECTRW_EXCL_LEAVING_ENABLED  RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_LEAVING_ENABLED)
#  define IPRT_CRITSECTRW_EXCL_BUSY             RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_BUSY)
#  define IPRT_CRITSECTRW_EXCL_WAITING          RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_WAITING)
#  define IPRT_CRITSECTRW_EXCL_ENTERED_SHARED   RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_ENTERED_SHARED)
#  define IPRT_CRITSECTRW_EXCL_LEAVING_SHARED   RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_EXCL_LEAVING_SHARED)
#  define IPRT_CRITSECTRW_SHARED_ENTERED        RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_SHARED_ENTERED)
#  define IPRT_CRITSECTRW_SHARED_LEAVING        RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_SHARED_LEAVING)
#  define IPRT_CRITSECTRW_SHARED_BUSY           RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_SHARED_BUSY)
#  define IPRT_CRITSECTRW_SHARED_WAITING        RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECTRW_SHARED_WAITING)
# endif
#else
# define IPRT_CRITSECTRW_EXCL_ENTERED(a_pvCritSect, a_pszName, a_cNestings, a_cWaitingReaders, a_cWriters) do {} while (0)
# define IPRT_CRITSECTRW_EXCL_ENTERED_ENABLED() (false)
# define IPRT_CRITSECTRW_EXCL_LEAVING(a_pvCritSect, a_pszName, a_cNestings, a_cWaitingReaders, a_cWriters) do {} while (0)
# define IPRT_CRITSECTRW_EXCL_LEAVING_ENABLED() (false)
# define IPRT_CRITSECTRW_EXCL_BUSY(   a_pvCritSect, a_pszName, a_fWriteMode, a_cWaitingReaders, a_cReaders, cWriters, a_pvNativeOwnerThread) do {} while (0)
# define IPRT_CRITSECTRW_EXCL_WAITING(a_pvCritSect, a_pszName, a_fWriteMode, a_cWaitingReaders, a_cReaders, cWriters, a_pvNativeOwnerThread) do {} while (0)
# define IPRT_CRITSECTRW_EXCL_ENTERED_SHARED(a_pvCritSect, a_pszName, a_cNestings, a_cWaitingReaders, a_cWriters) do {} while (0)
# define IPRT_CRITSECTRW_EXCL_LEAVING_SHARED(a_pvCritSect, a_pszName, a_cNestings, a_cWaitingReaders, a_cWriters) do {} while (0)
# define IPRT_CRITSECTRW_SHARED_ENTERED(a_pvCritSect, a_pszName, a_cReaders, a_cWaitingWriters)     do {} while (0)
# define IPRT_CRITSECTRW_SHARED_LEAVING(a_pvCritSect, a_pszName, a_cReaders, a_cWaitingWriters)     do {} while (0)
# define IPRT_CRITSECTRW_SHARED_BUSY(   a_pvCritSect, a_pszName, a_pvNativeOwnerThread, a_cWaitingReaders, a_cWriters) do {} while (0)
# define IPRT_CRITSECTRW_SHARED_WAITING(a_pvCritSect, a_pszName, a_pvNativeOwnerThread, a_cWaitingReaders, a_cWriters) do {} while (0)
#endif



RTDECL(int) RTCritSectRwInit(PRTCRITSECTRW pThis)
{
    return RTCritSectRwInitEx(pThis, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "RTCritSectRw");
}
RT_EXPORT_SYMBOL(RTCritSectRwInit);


RTDECL(int) RTCritSectRwInitNamed(PRTCRITSECTRW pThis, const char *pszName)
{
    return RTCritSectRwInitEx(pThis, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "%s", pszName);
}
RT_EXPORT_SYMBOL(RTCritSectRwInitNamed);


RTDECL(int) RTCritSectRwInitEx(PRTCRITSECTRW pThis, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    int rc;
    AssertReturn(!(fFlags & ~( RTCRITSECT_FLAGS_NO_NESTING | RTCRITSECT_FLAGS_NO_LOCK_VAL | RTCRITSECT_FLAGS_BOOTSTRAP_HACK
                              | RTCRITSECT_FLAGS_NOP )),
                 VERR_INVALID_PARAMETER);
    RT_NOREF_PV(hClass); RT_NOREF_PV(uSubClass); RT_NOREF_PV(pszNameFmt);


    /*
     * Initialize the structure, allocate the lock validator stuff and sems.
     */
    pThis->u32Magic         = RTCRITSECTRW_MAGIC_DEAD;
    pThis->fNeedReset       = false;
#ifdef IN_RING0
    pThis->fFlags           = (uint16_t)(fFlags | RTCRITSECT_FLAGS_RING0);
#else
    pThis->fFlags           = (uint16_t)(fFlags & ~RTCRITSECT_FLAGS_RING0);
#endif
    pThis->u.u128.s.Hi      = 0;
    pThis->u.u128.s.Lo      = 0;
    pThis->u.s.hNativeWriter= NIL_RTNATIVETHREAD;
    AssertCompile(sizeof(pThis->u.u128) >= sizeof(pThis->u.s));
    pThis->cWriterReads     = 0;
    pThis->cWriteRecursions = 0;
    pThis->hEvtWrite        = NIL_RTSEMEVENT;
    pThis->hEvtRead         = NIL_RTSEMEVENTMULTI;
    pThis->pValidatorWrite  = NULL;
    pThis->pValidatorRead   = NULL;

#ifdef RTCRITSECTRW_STRICT
    bool const fLVEnabled = !(fFlags & RTCRITSECT_FLAGS_NO_LOCK_VAL);
    if (!pszNameFmt)
    {
        static uint32_t volatile s_iAnon = 0;
        uint32_t i = ASMAtomicIncU32(&s_iAnon) - 1;
        rc = RTLockValidatorRecExclCreate(&pThis->pValidatorWrite, hClass, uSubClass, pThis,
                                          fLVEnabled, "RTCritSectRw-%u", i);
        if (RT_SUCCESS(rc))
            rc = RTLockValidatorRecSharedCreate(&pThis->pValidatorRead, hClass, uSubClass, pThis,
                                                false /*fSignaller*/, fLVEnabled, "RTCritSectRw-%u", i);
    }
    else
    {
        va_list va;
        va_start(va, pszNameFmt);
        rc = RTLockValidatorRecExclCreateV(&pThis->pValidatorWrite, hClass, uSubClass, pThis,
                                           fLVEnabled, pszNameFmt, va);
        va_end(va);
        if (RT_SUCCESS(rc))
        {
            va_start(va, pszNameFmt);
            RTLockValidatorRecSharedCreateV(&pThis->pValidatorRead, hClass, uSubClass, pThis,
                                            false /*fSignaller*/, fLVEnabled, pszNameFmt, va);
            va_end(va);
        }
    }
    if (RT_SUCCESS(rc))
        rc = RTLockValidatorRecMakeSiblings(&pThis->pValidatorWrite->Core, &pThis->pValidatorRead->Core);

    if (RT_SUCCESS(rc))
#endif
    {
        rc = RTSemEventMultiCreate(&pThis->hEvtRead);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventCreate(&pThis->hEvtWrite);
            if (RT_SUCCESS(rc))
            {
                pThis->u32Magic = RTCRITSECTRW_MAGIC;
                return VINF_SUCCESS;
            }
            RTSemEventMultiDestroy(pThis->hEvtRead);
        }
    }

#ifdef RTCRITSECTRW_STRICT
    RTLockValidatorRecSharedDestroy(&pThis->pValidatorRead);
    RTLockValidatorRecExclDestroy(&pThis->pValidatorWrite);
#endif
    return rc;
}
RT_EXPORT_SYMBOL(RTCritSectRwInitEx);


RTDECL(uint32_t) RTCritSectRwSetSubClass(PRTCRITSECTRW pThis, uint32_t uSubClass)
{
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif
#ifdef RTCRITSECTRW_STRICT
    AssertReturn(!(pThis->fFlags & RTCRITSECT_FLAGS_NOP), RTLOCKVAL_SUB_CLASS_INVALID);

    RTLockValidatorRecSharedSetSubClass(pThis->pValidatorRead, uSubClass);
    return RTLockValidatorRecExclSetSubClass(pThis->pValidatorWrite, uSubClass);
#else
    NOREF(uSubClass);
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}
RT_EXPORT_SYMBOL(RTCritSectRwSetSubClass);


static int rtCritSectRwEnterShared(PRTCRITSECTRW pThis, PCRTLOCKVALSRCPOS pSrcPos, bool fTryOnly)
{
    /*
     * Validate input.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif
    RT_NOREF_PV(pSrcPos);

#ifdef RTCRITSECTRW_STRICT
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
    if (!fTryOnly)
    {
        int            rc9;
        RTNATIVETHREAD hNativeWriter;
        ASMAtomicUoReadHandle(&pThis->u.s.hNativeWriter, &hNativeWriter);
        if (hNativeWriter != NIL_RTTHREAD && hNativeWriter == RTThreadNativeSelf())
            rc9 = RTLockValidatorRecExclCheckOrder(pThis->pValidatorWrite, hThreadSelf, pSrcPos, RT_INDEFINITE_WAIT);
        else
            rc9 = RTLockValidatorRecSharedCheckOrder(pThis->pValidatorRead, hThreadSelf, pSrcPos, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Get cracking...
     */
    uint64_t u64State    = ASMAtomicReadU64(&pThis->u.s.u64State);
    uint64_t u64OldState = u64State;

    for (;;)
    {
        if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
        {
            /* It flows in the right direction, try follow it before it changes. */
            uint64_t c = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
            c++;
            Assert(c < RTCSRW_CNT_MASK / 2);
            u64State &= ~RTCSRW_CNT_RD_MASK;
            u64State |= c << RTCSRW_CNT_RD_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
            {
#ifdef RTCRITSECTRW_STRICT
                RTLockValidatorRecSharedAddOwner(pThis->pValidatorRead, hThreadSelf, pSrcPos);
#endif
                break;
            }
        }
        else if ((u64State & (RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK)) == 0)
        {
            /* Wrong direction, but we're alone here and can simply try switch the direction. */
            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK | RTCSRW_DIR_MASK);
            u64State |= (UINT64_C(1) << RTCSRW_CNT_RD_SHIFT) | (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
            {
                Assert(!pThis->fNeedReset);
#ifdef RTCRITSECTRW_STRICT
                RTLockValidatorRecSharedAddOwner(pThis->pValidatorRead, hThreadSelf, pSrcPos);
#endif
                break;
            }
        }
        else
        {
            /* Is the writer perhaps doing a read recursion? */
            RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
            RTNATIVETHREAD hNativeWriter;
            ASMAtomicUoReadHandle(&pThis->u.s.hNativeWriter, &hNativeWriter);
            if (hNativeSelf == hNativeWriter)
            {
#ifdef RTCRITSECTRW_STRICT
                int rc9 = RTLockValidatorRecExclRecursionMixed(pThis->pValidatorWrite, &pThis->pValidatorRead->Core, pSrcPos);
                if (RT_FAILURE(rc9))
                    return rc9;
#endif
                Assert(pThis->cWriterReads < UINT32_MAX / 2);
                uint32_t const cReads = ASMAtomicIncU32(&pThis->cWriterReads); NOREF(cReads);
                IPRT_CRITSECTRW_EXCL_ENTERED_SHARED(pThis, NULL,
                                                    cReads + pThis->cWriteRecursions,
                                                    (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                                    (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));

                return VINF_SUCCESS; /* don't break! */
            }

            /* If we're only trying, return already. */
            if (fTryOnly)
            {
                IPRT_CRITSECTRW_SHARED_BUSY(pThis, NULL,
                                            (void *)pThis->u.s.hNativeWriter,
                                            (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                            (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));
                return VERR_SEM_BUSY;
            }

            /* Add ourselves to the queue and wait for the direction to change. */
            uint64_t c = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
            c++;
            Assert(c < RTCSRW_CNT_MASK / 2);

            uint64_t cWait = (u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT;
            cWait++;
            Assert(cWait <= c);
            Assert(cWait < RTCSRW_CNT_MASK / 2);

            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_WAIT_CNT_RD_MASK);
            u64State |= (c << RTCSRW_CNT_RD_SHIFT) | (cWait << RTCSRW_WAIT_CNT_RD_SHIFT);

            if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
            {
                IPRT_CRITSECTRW_SHARED_WAITING(pThis, NULL,
                                               (void *)pThis->u.s.hNativeWriter,
                                               (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                               (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));
                for (uint32_t iLoop = 0; ; iLoop++)
                {
                    int rc;
#ifdef RTCRITSECTRW_STRICT
                    rc = RTLockValidatorRecSharedCheckBlocking(pThis->pValidatorRead, hThreadSelf, pSrcPos, true,
                                                               RT_INDEFINITE_WAIT, RTTHREADSTATE_RW_READ, false);
                    if (RT_SUCCESS(rc))
#elif defined(IN_RING3)
                    RTTHREAD hThreadSelf = RTThreadSelf();
                    RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_READ, false);
#endif
                    {
                        rc = RTSemEventMultiWait(pThis->hEvtRead, RT_INDEFINITE_WAIT);
#ifdef IN_RING3
                        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_READ);
#endif
                        if (pThis->u32Magic != RTCRITSECTRW_MAGIC)
                            return VERR_SEM_DESTROYED;
                    }
                    if (RT_FAILURE(rc))
                    {
                        /* Decrement the counts and return the error. */
                        for (;;)
                        {
                            u64OldState = u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
                            c = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT; Assert(c > 0);
                            c--;
                            cWait = (u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT; Assert(cWait > 0);
                            cWait--;
                            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_WAIT_CNT_RD_MASK);
                            u64State |= (c << RTCSRW_CNT_RD_SHIFT) | (cWait << RTCSRW_WAIT_CNT_RD_SHIFT);
                            if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                                break;
                        }
                        return rc;
                    }

                    Assert(pThis->fNeedReset);
                    u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
                    if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
                        break;
                    AssertMsg(iLoop < 1, ("%u\n", iLoop));
                }

                /* Decrement the wait count and maybe reset the semaphore (if we're last). */
                for (;;)
                {
                    u64OldState = u64State;

                    cWait = (u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT;
                    Assert(cWait > 0);
                    cWait--;
                    u64State &= ~RTCSRW_WAIT_CNT_RD_MASK;
                    u64State |= cWait << RTCSRW_WAIT_CNT_RD_SHIFT;

                    if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                    {
                        if (cWait == 0)
                        {
                            if (ASMAtomicXchgBool(&pThis->fNeedReset, false))
                            {
                                int rc = RTSemEventMultiReset(pThis->hEvtRead);
                                AssertRCReturn(rc, rc);
                            }
                        }
                        break;
                    }
                    u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
                }

#ifdef RTCRITSECTRW_STRICT
                RTLockValidatorRecSharedAddOwner(pThis->pValidatorRead, hThreadSelf, pSrcPos);
#endif
                break;
            }
        }

        if (pThis->u32Magic != RTCRITSECTRW_MAGIC)
            return VERR_SEM_DESTROYED;

        ASMNopPause();
        u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
        u64OldState = u64State;
    }

    /* got it! */
    Assert((ASMAtomicReadU64(&pThis->u.s.u64State) & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT));
    IPRT_CRITSECTRW_SHARED_ENTERED(pThis, NULL,
                                   (uint32_t)((u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT),
                                   (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));
    return VINF_SUCCESS;
}


RTDECL(int) RTCritSectRwEnterShared(PRTCRITSECTRW pThis)
{
#ifndef RTCRITSECTRW_STRICT
    return rtCritSectRwEnterShared(pThis, NULL, false /*fTryOnly*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtCritSectRwEnterShared(pThis, &SrcPos, false /*fTryOnly*/);
#endif
}
RT_EXPORT_SYMBOL(RTCritSectRwEnterShared);


RTDECL(int) RTCritSectRwEnterSharedDebug(PRTCRITSECTRW pThis, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtCritSectRwEnterShared(pThis, &SrcPos, false /*fTryOnly*/);
}
RT_EXPORT_SYMBOL(RTCritSectRwEnterSharedDebug);


RTDECL(int) RTCritSectRwTryEnterShared(PRTCRITSECTRW pThis)
{
#ifndef RTCRITSECTRW_STRICT
    return rtCritSectRwEnterShared(pThis, NULL, true /*fTryOnly*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtCritSectRwEnterShared(pThis, &SrcPos, true /*fTryOnly*/);
#endif
}
RT_EXPORT_SYMBOL(RTCritSectRwEnterShared);


RTDECL(int) RTCritSectRwTryEnterSharedDebug(PRTCRITSECTRW pThis, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtCritSectRwEnterShared(pThis, &SrcPos, true /*fTryOnly*/);
}
RT_EXPORT_SYMBOL(RTCritSectRwEnterSharedDebug);



RTDECL(int) RTCritSectRwLeaveShared(PRTCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif

    /*
     * Check the direction and take action accordingly.
     */
    uint64_t u64State    = ASMAtomicReadU64(&pThis->u.s.u64State);
    uint64_t u64OldState = u64State;
    if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
    {
#ifdef RTCRITSECTRW_STRICT
        int rc9 = RTLockValidatorRecSharedCheckAndRelease(pThis->pValidatorRead, NIL_RTTHREAD);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        IPRT_CRITSECTRW_SHARED_LEAVING(pThis, NULL,
                                       (uint32_t)((u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT) - 1,
                                       (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));

        for (;;)
        {
            uint64_t c = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
            AssertReturn(c > 0, VERR_NOT_OWNER);
            c--;

            if (   c > 0
                || (u64State & RTCSRW_CNT_WR_MASK) == 0)
            {
                /* Don't change the direction. */
                u64State &= ~RTCSRW_CNT_RD_MASK;
                u64State |= c << RTCSRW_CNT_RD_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                    break;
            }
            else
            {
                /* Reverse the direction and signal the reader threads. */
                u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_DIR_MASK);
                u64State |= RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                {
                    int rc = RTSemEventSignal(pThis->hEvtWrite);
                    AssertRC(rc);
                    break;
                }
            }

            ASMNopPause();
            u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
            u64OldState = u64State;
        }
    }
    else
    {
        RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
        RTNATIVETHREAD hNativeWriter;
        ASMAtomicUoReadHandle(&pThis->u.s.hNativeWriter, &hNativeWriter);
        AssertReturn(hNativeSelf == hNativeWriter, VERR_NOT_OWNER);
        AssertReturn(pThis->cWriterReads > 0, VERR_NOT_OWNER);
#ifdef RTCRITSECTRW_STRICT
        int rc = RTLockValidatorRecExclUnwindMixed(pThis->pValidatorWrite, &pThis->pValidatorRead->Core);
        if (RT_FAILURE(rc))
            return rc;
#endif
        uint32_t cReads = ASMAtomicDecU32(&pThis->cWriterReads); NOREF(cReads);
        IPRT_CRITSECTRW_EXCL_LEAVING_SHARED(pThis, NULL,
                                            cReads + pThis->cWriteRecursions,
                                            (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                            (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTCritSectRwLeaveShared);


static int rtCritSectRwEnterExcl(PRTCRITSECTRW pThis, PCRTLOCKVALSRCPOS pSrcPos, bool fTryOnly)
{
    /*
     * Validate input.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif
    RT_NOREF_PV(pSrcPos);

#ifdef RTCRITSECTRW_STRICT
    RTTHREAD hThreadSelf = NIL_RTTHREAD;
    if (!fTryOnly)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecExclCheckOrder(pThis->pValidatorWrite, hThreadSelf, pSrcPos, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Check if we're already the owner and just recursing.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->u.s.hNativeWriter, &hNativeWriter);
    if (hNativeSelf == hNativeWriter)
    {
        Assert((ASMAtomicReadU64(&pThis->u.s.u64State) & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT));
#ifdef RTCRITSECTRW_STRICT
        int rc9 = RTLockValidatorRecExclRecursion(pThis->pValidatorWrite, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        Assert(pThis->cWriteRecursions < UINT32_MAX / 2);
        uint32_t cNestings = ASMAtomicIncU32(&pThis->cWriteRecursions); NOREF(cNestings);

#ifdef IPRT_WITH_DTRACE
        if (IPRT_CRITSECTRW_EXCL_ENTERED_ENABLED())
        {
            uint64_t u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
            IPRT_CRITSECTRW_EXCL_ENTERED(pThis, NULL, cNestings + pThis->cWriterReads,
                                         (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                         (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));
        }
#endif
        return VINF_SUCCESS;
    }

    /*
     * Get cracking.
     */
    uint64_t u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
    uint64_t u64OldState = u64State;

    for (;;)
    {
        if (   (u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT)
            || (u64State & (RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK)) != 0)
        {
            /* It flows in the right direction, try follow it before it changes. */
            uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT;
            c++;
            Assert(c < RTCSRW_CNT_MASK / 2);
            u64State &= ~RTCSRW_CNT_WR_MASK;
            u64State |= c << RTCSRW_CNT_WR_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                break;
        }
        else if ((u64State & (RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK)) == 0)
        {
            /* Wrong direction, but we're alone here and can simply try switch the direction. */
            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK | RTCSRW_DIR_MASK);
            u64State |= (UINT64_C(1) << RTCSRW_CNT_WR_SHIFT) | (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                break;
        }
        else if (fTryOnly)
            /* Wrong direction and we're not supposed to wait, just return. */
            return VERR_SEM_BUSY;
        else
        {
            /* Add ourselves to the write count and break out to do the wait. */
            uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT;
            c++;
            Assert(c < RTCSRW_CNT_MASK / 2);
            u64State &= ~RTCSRW_CNT_WR_MASK;
            u64State |= c << RTCSRW_CNT_WR_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                break;
        }

        if (pThis->u32Magic != RTCRITSECTRW_MAGIC)
            return VERR_SEM_DESTROYED;

        ASMNopPause();
        u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
        u64OldState = u64State;
    }

    /*
     * If we're in write mode now try grab the ownership. Play fair if there
     * are threads already waiting.
     */
    bool fDone = (u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT)
              && (  ((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT) == 1
                  || fTryOnly);
    if (fDone)
        ASMAtomicCmpXchgHandle(&pThis->u.s.hNativeWriter, hNativeSelf, NIL_RTNATIVETHREAD, fDone);
    if (!fDone)
    {
        /*
         * If only trying, undo the above writer incrementation and return.
         */
        if (fTryOnly)
        {
            for (;;)
            {
                u64OldState = u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
                uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT; Assert(c > 0);
                c--;
                u64State &= ~RTCSRW_CNT_WR_MASK;
                u64State |= c << RTCSRW_CNT_WR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                    break;
            }
            IPRT_CRITSECTRW_EXCL_BUSY(pThis, NULL,
                                      (u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT) /*fWrite*/,
                                      (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                      (uint32_t)((u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT),
                                      (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT),
                                      (void *)pThis->u.s.hNativeWriter);
            return VERR_SEM_BUSY;
        }

        /*
         * Wait for our turn.
         */
        IPRT_CRITSECTRW_EXCL_WAITING(pThis, NULL,
                                     (u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT) /*fWrite*/,
                                     (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                     (uint32_t)((u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT),
                                     (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT),
                                     (void *)pThis->u.s.hNativeWriter);
        for (uint32_t iLoop = 0; ; iLoop++)
        {
            int rc;
#ifdef RTCRITSECTRW_STRICT
            if (hThreadSelf == NIL_RTTHREAD)
                hThreadSelf = RTThreadSelfAutoAdopt();
            rc = RTLockValidatorRecExclCheckBlocking(pThis->pValidatorWrite, hThreadSelf, pSrcPos, true,
                                                     RT_INDEFINITE_WAIT, RTTHREADSTATE_RW_WRITE, false);
            if (RT_SUCCESS(rc))
#elif defined(IN_RING3)
            RTTHREAD hThreadSelf = RTThreadSelf();
            RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_WRITE, false);
#endif
            {
                rc = RTSemEventWait(pThis->hEvtWrite, RT_INDEFINITE_WAIT);
#ifdef IN_RING3
                RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
#endif
                if (pThis->u32Magic != RTCRITSECTRW_MAGIC)
                    return VERR_SEM_DESTROYED;
            }
            if (RT_FAILURE(rc))
            {
                /* Decrement the counts and return the error. */
                for (;;)
                {
                    u64OldState = u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
                    uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT; Assert(c > 0);
                    c--;
                    u64State &= ~RTCSRW_CNT_WR_MASK;
                    u64State |= c << RTCSRW_CNT_WR_SHIFT;
                    if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                        break;
                }
                return rc;
            }

            u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
            if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT))
            {
                ASMAtomicCmpXchgHandle(&pThis->u.s.hNativeWriter, hNativeSelf, NIL_RTNATIVETHREAD, fDone);
                if (fDone)
                    break;
            }
            AssertMsg(iLoop < 1000, ("%u\n", iLoop)); /* may loop a few times here... */
        }
    }

    /*
     * Got it!
     */
    Assert((ASMAtomicReadU64(&pThis->u.s.u64State) & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT));
    ASMAtomicWriteU32(&pThis->cWriteRecursions, 1);
    Assert(pThis->cWriterReads == 0);
#ifdef RTCRITSECTRW_STRICT
    RTLockValidatorRecExclSetOwner(pThis->pValidatorWrite, hThreadSelf, pSrcPos, true);
#endif
    IPRT_CRITSECTRW_EXCL_ENTERED(pThis, NULL, 1,
                                 (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                 (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));

    return VINF_SUCCESS;
}


RTDECL(int) RTCritSectRwEnterExcl(PRTCRITSECTRW pThis)
{
#ifndef RTCRITSECTRW_STRICT
    return rtCritSectRwEnterExcl(pThis, NULL, false /*fTryAgain*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtCritSectRwEnterExcl(pThis, &SrcPos, false /*fTryAgain*/);
#endif
}
RT_EXPORT_SYMBOL(RTCritSectRwEnterExcl);


RTDECL(int) RTCritSectRwEnterExclDebug(PRTCRITSECTRW pThis, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtCritSectRwEnterExcl(pThis, &SrcPos, false /*fTryAgain*/);
}
RT_EXPORT_SYMBOL(RTCritSectRwEnterExclDebug);


RTDECL(int) RTCritSectRwTryEnterExcl(PRTCRITSECTRW pThis)
{
#ifndef RTCRITSECTRW_STRICT
    return rtCritSectRwEnterExcl(pThis, NULL, true /*fTryAgain*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtCritSectRwEnterExcl(pThis, &SrcPos, true /*fTryAgain*/);
#endif
}
RT_EXPORT_SYMBOL(RTCritSectRwTryEnterExcl);


RTDECL(int) RTCritSectRwTryEnterExclDebug(PRTCRITSECTRW pThis, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtCritSectRwEnterExcl(pThis, &SrcPos, true /*fTryAgain*/);
}
RT_EXPORT_SYMBOL(RTCritSectRwTryEnterExclDebug);


RTDECL(int) RTCritSectRwLeaveExcl(PRTCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif

    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->u.s.hNativeWriter, &hNativeWriter);
    AssertReturn(hNativeSelf == hNativeWriter, VERR_NOT_OWNER);

    /*
     * Unwind a recursion.
     */
    if (pThis->cWriteRecursions == 1)
    {
        AssertReturn(pThis->cWriterReads == 0, VERR_WRONG_ORDER); /* (must release all read recursions before the final write.) */
#ifdef RTCRITSECTRW_STRICT
        int rc9 = RTLockValidatorRecExclReleaseOwner(pThis->pValidatorWrite, true);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        /*
         * Update the state.
         */
        ASMAtomicWriteU32(&pThis->cWriteRecursions, 0);
        ASMAtomicWriteHandle(&pThis->u.s.hNativeWriter, NIL_RTNATIVETHREAD);

        uint64_t u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
        IPRT_CRITSECTRW_EXCL_LEAVING(pThis, NULL, 0,
                                     (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                     (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));

        for (;;)
        {
            uint64_t u64OldState = u64State;

            uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT;
            Assert(c > 0);
            c--;

            if (   c > 0
                || (u64State & RTCSRW_CNT_RD_MASK) == 0)
            {
                /* Don't change the direction, wait up the next writer if any. */
                u64State &= ~RTCSRW_CNT_WR_MASK;
                u64State |= c << RTCSRW_CNT_WR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                {
                    if (c > 0)
                    {
                        int rc = RTSemEventSignal(pThis->hEvtWrite);
                        AssertRC(rc);
                    }
                    break;
                }
            }
            else
            {
                /* Reverse the direction and signal the reader threads. */
                u64State &= ~(RTCSRW_CNT_WR_MASK | RTCSRW_DIR_MASK);
                u64State |= RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u.s.u64State, u64State, u64OldState))
                {
                    Assert(!pThis->fNeedReset);
                    ASMAtomicWriteBool(&pThis->fNeedReset, true);
                    int rc = RTSemEventMultiSignal(pThis->hEvtRead);
                    AssertRC(rc);
                    break;
                }
            }

            ASMNopPause();
            if (pThis->u32Magic != RTCRITSECTRW_MAGIC)
                return VERR_SEM_DESTROYED;
            u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
        }
    }
    else
    {
        Assert(pThis->cWriteRecursions != 0);
#ifdef RTCRITSECTRW_STRICT
        int rc9 = RTLockValidatorRecExclUnwind(pThis->pValidatorWrite);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        uint32_t cNestings = ASMAtomicDecU32(&pThis->cWriteRecursions); NOREF(cNestings);
#ifdef IPRT_WITH_DTRACE
        if (IPRT_CRITSECTRW_EXCL_LEAVING_ENABLED())
        {
            uint64_t u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
            IPRT_CRITSECTRW_EXCL_LEAVING(pThis, NULL, cNestings + pThis->cWriterReads,
                                         (uint32_t)((u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT),
                                         (uint32_t)((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT));
        }
#endif
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTCritSectRwLeaveExcl);


RTDECL(bool) RTCritSectRwIsWriteOwner(PRTCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, false);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif

    /*
     * Check ownership.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->u.s.hNativeWriter, &hNativeWriter);
    return hNativeWriter == hNativeSelf;
}
RT_EXPORT_SYMBOL(RTCritSectRwIsWriteOwner);


RTDECL(bool) RTCritSectRwIsReadOwner(PRTCRITSECTRW pThis, bool fWannaHear)
{
    RT_NOREF_PV(fWannaHear);

    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, false);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif

    /*
     * Inspect the state.
     */
    uint64_t u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
    if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT))
    {
        /*
         * It's in write mode, so we can only be a reader if we're also the
         * current writer.
         */
        RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
        RTNATIVETHREAD hWriter;
        ASMAtomicUoReadHandle(&pThis->u.s.hNativeWriter, &hWriter);
        return hWriter == hNativeSelf;
    }

    /*
     * Read mode.  If there are no current readers, then we cannot be a reader.
     */
    if (!(u64State & RTCSRW_CNT_RD_MASK))
        return false;

#ifdef RTCRITSECTRW_STRICT
    /*
     * Ask the lock validator.
     */
    return RTLockValidatorRecSharedIsOwner(pThis->pValidatorRead, NIL_RTTHREAD);
#else
    /*
     * Ok, we don't know, just tell the caller what he want to hear.
     */
    return fWannaHear;
#endif
}
RT_EXPORT_SYMBOL(RTCritSectRwIsReadOwner);


RTDECL(uint32_t) RTCritSectRwGetWriteRecursion(PRTCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    return pThis->cWriteRecursions;
}
RT_EXPORT_SYMBOL(RTCritSectRwGetWriteRecursion);


RTDECL(uint32_t) RTCritSectRwGetWriterReadRecursion(PRTCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    return pThis->cWriterReads;
}
RT_EXPORT_SYMBOL(RTCritSectRwGetWriterReadRecursion);


RTDECL(uint32_t) RTCritSectRwGetReadCount(PRTCRITSECTRW pThis)
{
    /*
     * Validate input.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->u32Magic == RTCRITSECTRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    uint64_t u64State = ASMAtomicReadU64(&pThis->u.s.u64State);
    if ((u64State & RTCSRW_DIR_MASK) != (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
        return 0;
    return (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
}
RT_EXPORT_SYMBOL(RTCritSectRwGetReadCount);


RTDECL(int) RTCritSectRwDelete(PRTCRITSECTRW pThis)
{
    /*
     * Assert free waiters and so on.
     */
    AssertPtr(pThis);
    Assert(pThis->u32Magic == RTCRITSECTRW_MAGIC);
    //Assert(pThis->cNestings == 0);
    //Assert(pThis->cLockers == -1);
    Assert(pThis->u.s.hNativeWriter == NIL_RTNATIVETHREAD);
#ifdef IN_RING0
    Assert(pThis->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pThis->fFlags & RTCRITSECT_FLAGS_RING0));
#endif

    /*
     * Invalidate the structure and free the semaphores.
     */
    if (!ASMAtomicCmpXchgU32(&pThis->u32Magic, RTCRITSECTRW_MAGIC_DEAD, RTCRITSECTRW_MAGIC))
        return VERR_INVALID_PARAMETER;

    pThis->fFlags   = 0;
    pThis->u.s.u64State = 0;

    RTSEMEVENT      hEvtWrite = pThis->hEvtWrite;
    pThis->hEvtWrite = NIL_RTSEMEVENT;
    RTSEMEVENTMULTI hEvtRead  = pThis->hEvtRead;
    pThis->hEvtRead  = NIL_RTSEMEVENTMULTI;

    int rc1 = RTSemEventDestroy(hEvtWrite);     AssertRC(rc1);
    int rc2 = RTSemEventMultiDestroy(hEvtRead); AssertRC(rc2);

#ifndef IN_RING0
    RTLockValidatorRecSharedDestroy(&pThis->pValidatorRead);
    RTLockValidatorRecExclDestroy(&pThis->pValidatorWrite);
#endif

    return RT_SUCCESS(rc1) ? rc2 : rc1;
}
RT_EXPORT_SYMBOL(RTCritSectRwDelete);

