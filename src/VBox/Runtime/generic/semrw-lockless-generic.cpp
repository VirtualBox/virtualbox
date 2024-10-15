/* $Id: semrw-lockless-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Read-Write Semaphore, Generic, lockless variant.
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
#define RTSEMRW_WITHOUT_REMAPPING
#define RTASSERT_QUIET
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/thread.h>

#include "internal/magics.h"
#include "internal/strict.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTSEMRWINTERNAL
{
    /** Magic value (RTSEMRW_MAGIC).  */
    uint32_t volatile       u32Magic;
    /** Indicates whether hEvtRead needs resetting. */
    bool volatile           fNeedReset;

    /** The state variable.
     * All accesses are atomic and it bits are defined like this:
     *      Bits 0..14  - cReads.
     *      Bit 15      - Unused.
     *      Bits 16..31 - cWrites. - doesn't make sense here
     *      Bit 31      - fDirection; 0=Read, 1=Write.
     *      Bits 32..46 - cWaitingReads
     *      Bit 47      - Unused.
     *      Bits 48..62 - cWaitingWrites
     *      Bit 63      - Unused.
     */
    uint64_t volatile       u64State;
    /** The write owner. */
    RTNATIVETHREAD volatile hNativeWriter;
    /** The number of reads made by the current writer. */
    uint32_t volatile       cWriterReads;
    /** The number of recursions made by the current writer. (The initial grabbing
     *  of the lock counts as the first one.) */
    uint32_t volatile       cWriteRecursions;

    /** What the writer threads are blocking on. */
    RTSEMEVENT              hEvtWrite;
    /** What the read threads are blocking on when waiting for the writer to
     * finish. */
    RTSEMEVENTMULTI         hEvtRead;

#ifdef RTSEMRW_STRICT
    /** The validator record for the writer. */
    RTLOCKVALRECEXCL        ValidatorWrite;
    /** The validator record for the readers. */
    RTLOCKVALRECSHRD        ValidatorRead;
#endif
} RTSEMRWINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTSEMRW_CNT_BITS            15
#define RTSEMRW_CNT_MASK            UINT64_C(0x00007fff)

#define RTSEMRW_CNT_RD_SHIFT        0
#define RTSEMRW_CNT_RD_MASK         (RTSEMRW_CNT_MASK << RTSEMRW_CNT_RD_SHIFT)
#define RTSEMRW_CNT_WR_SHIFT        16
#define RTSEMRW_CNT_WR_MASK         (RTSEMRW_CNT_MASK << RTSEMRW_CNT_WR_SHIFT)
#define RTSEMRW_DIR_SHIFT           31
#define RTSEMRW_DIR_MASK            RT_BIT_64(RTSEMRW_DIR_SHIFT)
#define RTSEMRW_DIR_READ            UINT64_C(0)
#define RTSEMRW_DIR_WRITE           UINT64_C(1)

#define RTSEMRW_WAIT_CNT_RD_SHIFT   32
#define RTSEMRW_WAIT_CNT_RD_MASK    (RTSEMRW_CNT_MASK << RTSEMRW_WAIT_CNT_RD_SHIFT)
//#define RTSEMRW_WAIT_CNT_WR_SHIFT   48
//#define RTSEMRW_WAIT_CNT_WR_MASK    (RTSEMRW_CNT_MASK << RTSEMRW_WAIT_CNT_WR_SHIFT)


RTDECL(int) RTSemRWCreate(PRTSEMRW phRWSem)
{
    return RTSemRWCreateEx(phRWSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "RTSemRW");
}
RT_EXPORT_SYMBOL(RTSemRWCreate);


RTDECL(int) RTSemRWCreateEx(PRTSEMRW phRWSem, uint32_t fFlags,
                            RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMRW_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    RTSEMRWINTERNAL *pThis = (RTSEMRWINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = RTSemEventMultiCreate(&pThis->hEvtRead);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventCreate(&pThis->hEvtWrite);
        if (RT_SUCCESS(rc))
        {
            pThis->u32Magic             = RTSEMRW_MAGIC;
            pThis->u32Padding           = 0;
            pThis->u64State             = 0;
            pThis->hNativeWriter        = NIL_RTNATIVETHREAD;
            pThis->cWriterReads         = 0;
            pThis->cWriteRecursions     = 0;
            pThis->fNeedReset           = false;
#ifdef RTSEMRW_STRICT
            bool const fLVEnabled = !(fFlags & RTSEMRW_FLAGS_NO_LOCK_VAL);
            if (!pszNameFmt)
            {
                static uint32_t volatile s_iSemRWAnon = 0;
                uint32_t i = ASMAtomicIncU32(&s_iSemRWAnon) - 1;
                RTLockValidatorRecExclInit(&pThis->ValidatorWrite, hClass, uSubClass, pThis,
                                           fLVEnabled, "RTSemRW-%u", i);
                RTLockValidatorRecSharedInit(&pThis->ValidatorRead, hClass, uSubClass, pThis,
                                             false /*fSignaller*/, fLVEnabled, "RTSemRW-%u", i);
            }
            else
            {
                va_list va;
                va_start(va, pszNameFmt);
                RTLockValidatorRecExclInitV(&pThis->ValidatorWrite, hClass, uSubClass, pThis,
                                            fLVEnabled, pszNameFmt, va);
                va_end(va);
                va_start(va, pszNameFmt);
                RTLockValidatorRecSharedInitV(&pThis->ValidatorRead, hClass, uSubClass, pThis,
                                              false /*fSignaller*/, fLVEnabled, pszNameFmt, va);
                va_end(va);
            }
            RTLockValidatorRecMakeSiblings(&pThis->ValidatorWrite.Core, &pThis->ValidatorRead.Core);
#endif

            *phRWSem = pThis;
            return VINF_SUCCESS;
        }
        RTSemEventMultiDestroy(pThis->hEvtRead);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTSemRWCreateEx);


RTDECL(int) RTSemRWDestroy(RTSEMRW hRWSem)
{
    /*
     * Validate input.
     */
    RTSEMRWINTERNAL *pThis = hRWSem;
    if (pThis == NIL_RTSEMRW)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, VERR_INVALID_HANDLE);
    Assert(!(ASMAtomicReadU64(&pThis->u64State) & (RTSEMRW_CNT_RD_MASK | RTSEMRW_CNT_WR_MASK)));

    /*
     * Invalidate the object and free up the resources.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTSEMRW_MAGIC, RTSEMRW_MAGIC), VERR_INVALID_HANDLE);

    RTSEMEVENTMULTI hEvtRead;
    ASMAtomicXchgHandle(&pThis->hEvtRead, NIL_RTSEMEVENTMULTI, &hEvtRead);
    int rc = RTSemEventMultiDestroy(hEvtRead);
    AssertRC(rc);

    RTSEMEVENT hEvtWrite;
    ASMAtomicXchgHandle(&pThis->hEvtWrite, NIL_RTSEMEVENT, &hEvtWrite);
    rc = RTSemEventDestroy(hEvtWrite);
    AssertRC(rc);

#ifdef RTSEMRW_STRICT
    RTLockValidatorRecSharedDelete(&pThis->ValidatorRead);
    RTLockValidatorRecExclDelete(&pThis->ValidatorWrite);
#endif
    RTMemFree(pThis);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemRWDestroy);


RTDECL(uint32_t) RTSemRWSetSubClass(RTSEMRW hRWSem, uint32_t uSubClass)
{
#ifdef RTSEMRW_STRICT
    /*
     * Validate handle.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);

    RTLockValidatorRecSharedSetSubClass(&pThis->ValidatorRead, uSubClass);
    return RTLockValidatorRecExclSetSubClass(&pThis->ValidatorWrite, uSubClass);
#else
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}
RT_EXPORT_SYMBOL(RTSemRWSetSubClass);


static int rtSemRWRequestRead(RTSEMRW hRWSem, RTMSINTERVAL cMillies, bool fInterruptible, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    RTSEMRWINTERNAL *pThis = hRWSem;
    if (pThis == NIL_RTSEMRW)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSEMRW_STRICT
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
    if (cMillies > 0)
    {
        int            rc9;
        RTNATIVETHREAD hNativeWriter;
        ASMAtomicUoReadHandle(&pThis->hNativeWriter, &hNativeWriter);
        if (hNativeWriter != NIL_RTTHREAD && hNativeWriter == RTThreadNativeSelf())
            rc9 = RTLockValidatorRecExclCheckOrder(&pThis->ValidatorWrite, hThreadSelf, pSrcPos, cMillies);
        else
            rc9 = RTLockValidatorRecSharedCheckOrder(&pThis->ValidatorRead, hThreadSelf, pSrcPos, cMillies);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Get cracking...
     */
    uint64_t u64State    = ASMAtomicReadU64(&pThis->u64State);
    uint64_t u64OldState = u64State;

    for (;;)
    {
        if ((u64State & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_READ << RTSEMRW_DIR_SHIFT))
        {
            /* It flows in the right direction, try follow it before it changes. */
            uint64_t c = (u64State & RTSEMRW_CNT_RD_MASK) >> RTSEMRW_CNT_RD_SHIFT;
            c++;
            Assert(c < RTSEMRW_CNT_MASK / 2);
            u64State &= ~RTSEMRW_CNT_RD_MASK;
            u64State |= c << RTSEMRW_CNT_RD_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
#ifdef RTSEMRW_STRICT
                RTLockValidatorRecSharedAddOwner(&pThis->ValidatorRead, hThreadSelf, pSrcPos);
#endif
                break;
            }
        }
        else if ((u64State & (RTSEMRW_CNT_RD_MASK | RTSEMRW_CNT_WR_MASK)) == 0)
        {
            /* Wrong direction, but we're alone here and can simply try switch the direction. */
            u64State &= ~(RTSEMRW_CNT_RD_MASK | RTSEMRW_CNT_WR_MASK | RTSEMRW_DIR_MASK);
            u64State |= (UINT64_C(1) << RTSEMRW_CNT_RD_SHIFT) | (RTSEMRW_DIR_READ << RTSEMRW_DIR_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
                Assert(!pThis->fNeedReset);
#ifdef RTSEMRW_STRICT
                RTLockValidatorRecSharedAddOwner(&pThis->ValidatorRead, hThreadSelf, pSrcPos);
#endif
                break;
            }
        }
        else
        {
            /* Is the writer perhaps doing a read recursion? */
            RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
            RTNATIVETHREAD hNativeWriter;
            ASMAtomicUoReadHandle(&pThis->hNativeWriter, &hNativeWriter);
            if (hNativeSelf == hNativeWriter)
            {
#ifdef RTSEMRW_STRICT
                int rc9 = RTLockValidatorRecExclRecursionMixed(&pThis->ValidatorWrite, &pThis->ValidatorRead.Core, pSrcPos);
                if (RT_FAILURE(rc9))
                    return rc9;
#endif
                Assert(pThis->cWriterReads < UINT32_MAX / 2);
                ASMAtomicIncU32(&pThis->cWriterReads);
                return VINF_SUCCESS; /* don't break! */
            }

            /* If the timeout is 0, return already. */
            if (!cMillies)
                return VERR_TIMEOUT;

            /* Add ourselves to the queue and wait for the direction to change. */
            uint64_t c = (u64State & RTSEMRW_CNT_RD_MASK) >> RTSEMRW_CNT_RD_SHIFT;
            c++;
            Assert(c < RTSEMRW_CNT_MASK / 2);

            uint64_t cWait = (u64State & RTSEMRW_WAIT_CNT_RD_MASK) >> RTSEMRW_WAIT_CNT_RD_SHIFT;
            cWait++;
            Assert(cWait <= c);
            Assert(cWait < RTSEMRW_CNT_MASK / 2);

            u64State &= ~(RTSEMRW_CNT_RD_MASK | RTSEMRW_WAIT_CNT_RD_MASK);
            u64State |= (c << RTSEMRW_CNT_RD_SHIFT) | (cWait << RTSEMRW_WAIT_CNT_RD_SHIFT);

            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
                for (uint32_t iLoop = 0; ; iLoop++)
                {
                    int rc;
#ifdef RTSEMRW_STRICT
                    rc = RTLockValidatorRecSharedCheckBlocking(&pThis->ValidatorRead, hThreadSelf, pSrcPos, true,
                                                               cMillies, RTTHREADSTATE_RW_READ, false);
                    if (RT_SUCCESS(rc))
#else
                    RTTHREAD hThreadSelf = RTThreadSelf();
                    RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_READ, false);
#endif
                    {
                        if (fInterruptible)
                            rc = RTSemEventMultiWaitNoResume(pThis->hEvtRead, cMillies);
                        else
                            rc = RTSemEventMultiWait(pThis->hEvtRead, cMillies);
                        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_READ);
                        if (pThis->u32Magic != RTSEMRW_MAGIC)
                            return VERR_SEM_DESTROYED;
                    }
                    if (RT_FAILURE(rc))
                    {
                        /* Decrement the counts and return the error. */
                        for (;;)
                        {
                            u64OldState = u64State = ASMAtomicReadU64(&pThis->u64State);
                            c = (u64State & RTSEMRW_CNT_RD_MASK) >> RTSEMRW_CNT_RD_SHIFT; Assert(c > 0);
                            c--;
                            cWait = (u64State & RTSEMRW_WAIT_CNT_RD_MASK) >> RTSEMRW_WAIT_CNT_RD_SHIFT; Assert(cWait > 0);
                            cWait--;
                            u64State &= ~(RTSEMRW_CNT_RD_MASK | RTSEMRW_WAIT_CNT_RD_MASK);
                            u64State |= (c << RTSEMRW_CNT_RD_SHIFT) | (cWait << RTSEMRW_WAIT_CNT_RD_SHIFT);
                            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                                break;
                        }
                        return rc;
                    }

                    Assert(pThis->fNeedReset);
                    u64State = ASMAtomicReadU64(&pThis->u64State);
                    if ((u64State & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_READ << RTSEMRW_DIR_SHIFT))
                        break;
                    AssertMsg(iLoop < 1, ("%u\n", iLoop));
                }

                /* Decrement the wait count and maybe reset the semaphore (if we're last). */
                for (;;)
                {
                    u64OldState = u64State;

                    cWait = (u64State & RTSEMRW_WAIT_CNT_RD_MASK) >> RTSEMRW_WAIT_CNT_RD_SHIFT;
                    Assert(cWait > 0);
                    cWait--;
                    u64State &= ~RTSEMRW_WAIT_CNT_RD_MASK;
                    u64State |= cWait << RTSEMRW_WAIT_CNT_RD_SHIFT;

                    if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
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
                    u64State = ASMAtomicReadU64(&pThis->u64State);
                }

#ifdef RTSEMRW_STRICT
                RTLockValidatorRecSharedAddOwner(&pThis->ValidatorRead, hThreadSelf, pSrcPos);
#endif
                break;
            }
        }

        if (pThis->u32Magic != RTSEMRW_MAGIC)
            return VERR_SEM_DESTROYED;

        ASMNopPause();
        u64State = ASMAtomicReadU64(&pThis->u64State);
        u64OldState = u64State;
    }

    /* got it! */
    Assert((ASMAtomicReadU64(&pThis->u64State) & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_READ << RTSEMRW_DIR_SHIFT));
    return VINF_SUCCESS;

}


RTDECL(int) RTSemRWRequestRead(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestRead(hRWSem, cMillies, false, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestRead(hRWSem, cMillies, false, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemRWRequestRead);


RTDECL(int) RTSemRWRequestReadDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestRead(hRWSem, cMillies, false, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemRWRequestReadDebug);


RTDECL(int) RTSemRWRequestReadNoResume(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestRead(hRWSem, cMillies, true, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestRead(hRWSem, cMillies, true, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemRWRequestReadNoResume);


RTDECL(int) RTSemRWRequestReadNoResumeDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestRead(hRWSem, cMillies, true, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemRWRequestReadNoResumeDebug);



RTDECL(int) RTSemRWReleaseRead(RTSEMRW hRWSem)
{
    /*
     * Validate handle.
     */
    RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Check the direction and take action accordingly.
     */
    uint64_t u64State    = ASMAtomicReadU64(&pThis->u64State);
    uint64_t u64OldState = u64State;
    if ((u64State & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_READ << RTSEMRW_DIR_SHIFT))
    {
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecSharedCheckAndRelease(&pThis->ValidatorRead, NIL_RTTHREAD);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        for (;;)
        {
            uint64_t c = (u64State & RTSEMRW_CNT_RD_MASK) >> RTSEMRW_CNT_RD_SHIFT;
            AssertReturn(c > 0, VERR_NOT_OWNER);
            c--;

            if (   c > 0
                || (u64State & RTSEMRW_CNT_WD_MASK) == 0)
            {
                /* Don't change the direction. */
                u64State &= ~RTSEMRW_CNT_RD_MASK;
                u64State |= c << RTSEMRW_CNT_RD_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                    break;
            }
            else
            {
                /* Reverse the direction and signal the reader threads. */
                u64State &= ~(RTSEMRW_CNT_RD_MASK | RTSEMRW_DIR_MASK);
                u64State |= RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                {
                    int rc = RTSemEventSignal(pThis->hEvtWrite);
                    AssertRC(rc);
                    break;
                }
            }

            ASMNopPause();
            u64State = ASMAtomicReadU64(&pThis->u64State);
            u64OldState = u64State;
        }
    }
    else
    {
        RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
        RTNATIVETHREAD hNativeWriter;
        ASMAtomicUoReadHandle(&pThis->hNativeWriter, &hNativeWriter);
        AssertReturn(hNativeSelf == hNativeWriter, VERR_NOT_OWNER);
        AssertReturn(pThis->cWriterReads > 0, VERR_NOT_OWNER);
#ifdef RTSEMRW_STRICT
        int rc = RTLockValidatorRecExclUnwindMixed(&pThis->ValidatorWrite, &pThis->ValidatorRead.Core);
        if (RT_FAILURE(rc))
            return rc;
#endif
        ASMAtomicDecU32(&pThis->cWriterReads);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemRWReleaseRead);


DECL_FORCE_INLINE(int) rtSemRWRequestWrite(RTSEMRW hRWSem, RTMSINTERVAL cMillies, bool fInterruptible, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    RTSEMRWINTERNAL *pThis = hRWSem;
    if (pThis == NIL_RTSEMRW)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSEMRW_STRICT
    RTTHREAD hThreadSelf = NIL_RTTHREAD;
    if (cMillies)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecExclCheckOrder(&pThis->ValidatorWrite, hThreadSelf, pSrcPos, cMillies);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Check if we're already the owner and just recursing.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->hNativeWriter, &hNativeWriter);
    if (hNativeSelf == hNativeWriter)
    {
        Assert((ASMAtomicReadU64(&pThis->u64State) & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT));
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecExclRecursion(&pThis->ValidatorWrite, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        Assert(pThis->cWriteRecursions < UINT32_MAX / 2);
        ASMAtomicIncU32(&pThis->cWriteRecursions);
        return VINF_SUCCESS;
    }

    /*
     * Get cracking.
     */
    uint64_t u64State    = ASMAtomicReadU64(&pThis->u64State);
    uint64_t u64OldState = u64State;

    for (;;)
    {
        if (   (u64State & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT)
            || (u64State & (RTSEMRW_CNT_RD_MASK | RTSEMRW_CNT_WR_MASK)) != 0)
        {
            /* It flows in the right direction, try follow it before it changes. */
            uint64_t c = (u64State & RTSEMRW_CNT_WR_MASK) >> RTSEMRW_CNT_WR_SHIFT;
            c++;
            Assert(c < RTSEMRW_CNT_MASK / 2);
            u64State &= ~RTSEMRW_CNT_WR_MASK;
            u64State |= c << RTSEMRW_CNT_WR_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                break;
        }
        else if ((u64State & (RTSEMRW_CNT_RD_MASK | RTSEMRW_CNT_WR_MASK)) == 0)
        {
            /* Wrong direction, but we're alone here and can simply try switch the direction. */
            u64State &= ~(RTSEMRW_CNT_RD_MASK | RTSEMRW_CNT_WR_MASK | RTSEMRW_DIR_MASK);
            u64State |= (UINT64_C(1) << RTSEMRW_CNT_WR_SHIFT) | (RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                break;
        }
        else if (!cMillies)
            /* Wrong direction and we're not supposed to wait, just return. */
            return VERR_TIMEOUT;
        else
        {
            /* Add ourselves to the write count and break out to do the wait. */
            uint64_t c = (u64State & RTSEMRW_CNT_WR_MASK) >> RTSEMRW_CNT_WR_SHIFT;
            c++;
            Assert(c < RTSEMRW_CNT_MASK / 2);
            u64State &= ~RTSEMRW_CNT_WR_MASK;
            u64State |= c << RTSEMRW_CNT_WR_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                break;
        }

        if (pThis->u32Magic != RTSEMRW_MAGIC)
            return VERR_SEM_DESTROYED;

        ASMNopPause();
        u64State = ASMAtomicReadU64(&pThis->u64State);
        u64OldState = u64State;
    }

    /*
     * If we're in write mode now try grab the ownership. Play fair if there
     * are threads already waiting.
     */
    bool fDone = (u64State & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT)
              && (  ((u64State & RTSEMRW_CNT_WR_MASK) >> RTSEMRW_CNT_WR_SHIFT) == 1
                  || cMillies == 0);
    if (fDone)
        ASMAtomicCmpXchgHandle(&pThis->hNativeWriter, hNativeSelf, NIL_RTNATIVETHREAD, fDone);
    if (!fDone)
    {
        /*
         * Wait for our turn.
         */
        for (uint32_t iLoop = 0; ; iLoop++)
        {
            int rc;
#ifdef RTSEMRW_STRICT
            if (cMillies)
            {
                if (hThreadSelf == NIL_RTTHREAD)
                    hThreadSelf = RTThreadSelfAutoAdopt();
                rc = RTLockValidatorRecExclCheckBlocking(&pThis->ValidatorWrite, hThreadSelf, pSrcPos, true,
                                                         cMillies, RTTHREADSTATE_RW_WRITE, false);
            }
            else
                rc = VINF_SUCCESS;
            if (RT_SUCCESS(rc))
#else
            RTTHREAD hThreadSelf = RTThreadSelf();
            RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_WRITE, false);
#endif
            {
                if (fInterruptible)
                    rc = RTSemEventWaitNoResume(pThis->hEvtWrite, cMillies);
                else
                    rc = RTSemEventWait(pThis->hEvtWrite, cMillies);
                RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
                if (pThis->u32Magic != RTSEMRW_MAGIC)
                    return VERR_SEM_DESTROYED;
            }
            if (RT_FAILURE(rc))
            {
                /* Decrement the counts and return the error. */
                for (;;)
                {
                    u64OldState = u64State = ASMAtomicReadU64(&pThis->u64State);
                    uint64_t c = (u64State & RTSEMRW_CNT_WR_MASK) >> RTSEMRW_CNT_WR_SHIFT; Assert(c > 0);
                    c--;
                    u64State &= ~RTSEMRW_CNT_WR_MASK;
                    u64State |= c << RTSEMRW_CNT_WR_SHIFT;
                    if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                        break;
                }
                return rc;
            }

            u64State = ASMAtomicReadU64(&pThis->u64State);
            if ((u64State & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT))
            {
                ASMAtomicCmpXchgHandle(&pThis->hNativeWriter, hNativeSelf, NIL_RTNATIVETHREAD, fDone);
                if (fDone)
                    break;
            }
            AssertMsg(iLoop < 1000, ("%u\n", iLoop)); /* may loop a few times here... */
        }
    }

    /*
     * Got it!
     */
    Assert((ASMAtomicReadU64(&pThis->u64State) & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT));
    ASMAtomicWriteU32(&pThis->cWriteRecursions, 1);
    Assert(pThis->cWriterReads == 0);
#ifdef RTSEMRW_STRICT
    RTLockValidatorRecExclSetOwner(&pThis->ValidatorWrite, hThreadSelf, pSrcPos, true);
#endif

    return VINF_SUCCESS;
}


RTDECL(int) RTSemRWRequestWrite(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestWrite(hRWSem, cMillies, false, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, false, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemRWRequestWrite);


RTDECL(int) RTSemRWRequestWriteDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, false, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemRWRequestWriteDebug);


RTDECL(int) RTSemRWRequestWriteNoResume(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestWrite(hRWSem, cMillies, true, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, true, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemRWRequestWriteNoResume);


RTDECL(int) RTSemRWRequestWriteNoResumeDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, true, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemRWRequestWriteNoResumeDebug);


RTDECL(int) RTSemRWReleaseWrite(RTSEMRW hRWSem)
{

    /*
     * Validate handle.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, VERR_INVALID_HANDLE);

    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->hNativeWriter, &hNativeWriter);
    AssertReturn(hNativeSelf == hNativeWriter, VERR_NOT_OWNER);

    /*
     * Unwind a recursion.
     */
    if (pThis->cWriteRecursions == 1)
    {
        AssertReturn(pThis->cWriterReads == 0, VERR_WRONG_ORDER); /* (must release all read recursions before the final write.) */
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecExclReleaseOwner(&pThis->ValidatorWrite, true);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        /*
         * Update the state.
         */
        ASMAtomicWriteU32(&pThis->cWriteRecursions, 0);
        ASMAtomicWriteHandle(&pThis->hNativeWriter, NIL_RTNATIVETHREAD);

        for (;;)
        {
            uint64_t u64State    = ASMAtomicReadU64(&pThis->u64State);
            uint64_t u64OldState = u64State;

            uint64_t c = (u64State & RTSEMRW_CNT_WR_MASK) >> RTSEMRW_CNT_WR_SHIFT;
            Assert(c > 0);
            c--;

            if (   c > 0
                || (u64State & RTSEMRW_CNT_RD_MASK) == 0)
            {
                /* Don't change the direction, wait up the next writer if any. */
                u64State &= ~RTSEMRW_CNT_WR_MASK;
                u64State |= c << RTSEMRW_CNT_WR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
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
                u64State &= ~(RTSEMRW_CNT_WR_MASK | RTSEMRW_DIR_MASK);
                u64State |= RTSEMRW_DIR_READ << RTSEMRW_DIR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                {
                    Assert(!pThis->fNeedReset);
                    ASMAtomicWriteBool(&pThis->fNeedReset, true);
                    int rc = RTSemEventMultiSignal(pThis->hEvtRead);
                    AssertRC(rc);
                    break;
                }
            }

            ASMNopPause();
            if (pThis->u32Magic != RTSEMRW_MAGIC)
                return VERR_SEM_DESTROYED;
        }
    }
    else
    {
        Assert(pThis->cWriteRecursions != 0);
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecExclUnwind(&pThis->ValidatorWrite);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        ASMAtomicDecU32(&pThis->cWriteRecursions);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemRWReleaseWrite);


RTDECL(bool) RTSemRWIsWriteOwner(RTSEMRW hRWSem)
{
    /*
     * Validate handle.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, false);

    /*
     * Check ownership.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->hNativeWriter, &hNativeWriter);
    return hNativeWriter == hNativeSelf;
}
RT_EXPORT_SYMBOL(RTSemRWIsWriteOwner);


RTDECL(bool)  RTSemRWIsReadOwner(RTSEMRW hRWSem, bool fWannaHear)
{
    /*
     * Validate handle.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, false);

    /*
     * Inspect the state.
     */
    uint64_t u64State = ASMAtomicReadU64(&pThis->u64State);
    if ((u64State & RTSEMRW_DIR_MASK) == (RTSEMRW_DIR_WRITE << RTSEMRW_DIR_SHIFT))
    {
        /*
         * It's in write mode, so we can only be a reader if we're also the
         * current writer.
         */
        RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
        RTNATIVETHREAD hWriter;
        ASMAtomicUoReadHandle(&pThis->hNativeWriter, &hWriter);
        return hWriter == hNativeSelf;
    }

    /*
     * Read mode.  If there are no current readers, then we cannot be a reader.
     */
    if (!(u64State & RTSEMRW_CNT_RD_MASK))
        return false;

#ifdef RTSEMRW_STRICT
    /*
     * Ask the lock validator.
     */
    return RTLockValidatorRecSharedIsOwner(&pThis->ValidatorRead, NIL_RTTHREAD);
#else
    /*
     * Ok, we don't know, just tell the caller what he want to hear.
     */
    return fWannaHear;
#endif
}
RT_EXPORT_SYMBOL(RTSemRWIsReadOwner);


RTDECL(uint32_t) RTSemRWGetWriteRecursion(RTSEMRW hRWSem)
{
    /*
     * Validate handle.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    return pThis->cWriteRecursions;
}
RT_EXPORT_SYMBOL(RTSemRWGetWriteRecursion);


RTDECL(uint32_t) RTSemRWGetWriterReadRecursion(RTSEMRW hRWSem)
{
    /*
     * Validate handle.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    return pThis->cWriterReads;
}
RT_EXPORT_SYMBOL(RTSemRWGetWriterReadRecursion);


RTDECL(uint32_t) RTSemRWGetReadCount(RTSEMRW hRWSem)
{
    /*
     * Validate input.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, 0);
    AssertMsgReturn(pThis->u32Magic == RTSEMRW_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    0);

    /*
     * Return the requested data.
     */
    uint64_t u64State = ASMAtomicReadU64(&pThis->u64State);
    if ((u64State & RTSEMRW_DIR_MASK) != (RTSEMRW_DIR_READ << RTSEMRW_DIR_SHIFT))
        return 0;
    return (u64State & RTSEMRW_CNT_RD_MASK) >> RTSEMRW_CNT_RD_SHIFT;
}
RT_EXPORT_SYMBOL(RTSemRWGetReadCount);

