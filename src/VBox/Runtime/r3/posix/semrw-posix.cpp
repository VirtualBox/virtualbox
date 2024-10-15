/* $Id: semrw-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Read-Write Semaphore, POSIX.
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
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/thread.h>

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include "internal/magics.h"
#include "internal/strict.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @todo move this to r3/posix/something.h. */
#ifdef RT_OS_SOLARIS
# define ATOMIC_GET_PTHREAD_T(ppvVar, pThread) ASMAtomicReadSize(ppvVar, pThread)
# define ATOMIC_SET_PTHREAD_T(ppvVar, pThread) ASMAtomicWriteSize(ppvVar, pThread)
#else
AssertCompileSize(pthread_t, sizeof(void *));
# define ATOMIC_GET_PTHREAD_T(ppvVar, pThread) do { *(pThread) = (pthread_t)ASMAtomicReadPtr((void * volatile *)ppvVar); } while (0)
# define ATOMIC_SET_PTHREAD_T(ppvVar, pThread) ASMAtomicWritePtr((void * volatile *)ppvVar, (void *)pThread)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Posix internal representation of a read-write semaphore. */
struct RTSEMRWINTERNAL
{
    /** The usual magic. (RTSEMRW_MAGIC) */
    uint32_t            u32Magic;
    /** The number of readers.
     * (For preventing screwing up the lock on linux). */
    uint32_t volatile   cReaders;
    /** Number of write recursions. */
    uint32_t            cWrites;
    /** Number of read recursions by the writer. */
    uint32_t            cWriterReads;
    /** The write owner of the lock. */
    volatile pthread_t  Writer;
    /** pthread rwlock. */
    pthread_rwlock_t    RWLock;
#ifdef RTSEMRW_STRICT
    /** The validator record for the writer. */
    RTLOCKVALRECEXCL    ValidatorWrite;
    /** The validator record for the readers. */
    RTLOCKVALRECSHRD    ValidatorRead;
#endif
};



#undef RTSemRWCreate
RTDECL(int) RTSemRWCreate(PRTSEMRW phRWSem)
{
    return RTSemRWCreateEx(phRWSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "RTSemRW");
}


RTDECL(int) RTSemRWCreateEx(PRTSEMRW phRWSem, uint32_t fFlags,
                            RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMRW_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Allocate handle.
     */
    int rc;
    struct RTSEMRWINTERNAL *pThis = (struct RTSEMRWINTERNAL *)RTMemAlloc(sizeof(struct RTSEMRWINTERNAL));
    if (pThis)
    {
        /*
         * Create the rwlock.
         */
        rc = pthread_rwlock_init(&pThis->RWLock, NULL);
        if (!rc)
        {
            pThis->u32Magic     = RTSEMRW_MAGIC;
            pThis->cReaders     = 0;
            pThis->cWrites      = 0;
            pThis->cWriterReads = 0;
            pThis->Writer       = (pthread_t)-1;
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
#else
            RT_NOREF_PV(hClass); RT_NOREF_PV(uSubClass); RT_NOREF_PV(pszNameFmt);
#endif
            *phRWSem = pThis;
            return VINF_SUCCESS;
        }

        rc = RTErrConvertFromErrno(rc);
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTSemRWDestroy(RTSEMRW hRWSem)
{
    /*
     * Validate input, nil handle is fine.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    if (pThis == NIL_RTSEMRW)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMRW_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);
    Assert(pThis->Writer == (pthread_t)-1);
    Assert(!pThis->cReaders);
    Assert(!pThis->cWrites);
    Assert(!pThis->cWriterReads);

    /*
     * Try destroy it.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTSEMRW_MAGIC, RTSEMRW_MAGIC), VERR_INVALID_HANDLE);
    int rc = pthread_rwlock_destroy(&pThis->RWLock);
    if (!rc)
    {
#ifdef RTSEMRW_STRICT
        RTLockValidatorRecSharedDelete(&pThis->ValidatorRead);
        RTLockValidatorRecExclDelete(&pThis->ValidatorWrite);
#endif
        RTMemFree(pThis);
        rc = VINF_SUCCESS;
    }
    else
    {
        ASMAtomicWriteU32(&pThis->u32Magic, RTSEMRW_MAGIC);
        AssertMsgFailed(("Failed to destroy read-write sem %p, rc=%d.\n", hRWSem, rc));
        rc = RTErrConvertFromErrno(rc);
    }

    return rc;
}


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
    RT_NOREF_PV(hRWSem); RT_NOREF_PV(uSubClass);
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}


DECL_FORCE_INLINE(int) rtSemRWRequestRead(RTSEMRW hRWSem, RTMSINTERVAL cMillies, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMRW_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);

    /*
     * Check if it's the writer (implement write+read recursion).
     */
    pthread_t Self = pthread_self();
    pthread_t Writer;
    ATOMIC_GET_PTHREAD_T(&pThis->Writer, &Writer);
    if (Writer == Self)
    {
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecExclRecursionMixed(&pThis->ValidatorWrite, &pThis->ValidatorRead.Core, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        Assert(pThis->cWriterReads < INT32_MAX);
        pThis->cWriterReads++;
        return VINF_SUCCESS;
    }

    /*
     * Try lock it.
     */
    RTTHREAD hThreadSelf = NIL_RTTHREAD;
    if (cMillies > 0)
    {
#ifdef RTSEMRW_STRICT
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecSharedCheckOrderAndBlocking(&pThis->ValidatorRead, hThreadSelf, pSrcPos, true,
                                                                cMillies, RTTHREADSTATE_RW_READ, true);
        if (RT_FAILURE(rc9))
            return rc9;
#else
        hThreadSelf = RTThreadSelf();
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_READ, true);
        RT_NOREF_PV(pSrcPos);
#endif
    }

    if (cMillies == RT_INDEFINITE_WAIT)
    {
        /* take rwlock */
        int rc = pthread_rwlock_rdlock(&pThis->RWLock);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_READ);
        if (rc)
        {
            AssertMsgFailed(("Failed read lock read-write sem %p, rc=%d.\n", hRWSem, rc));
            return RTErrConvertFromErrno(rc);
        }
    }
    else
    {
#ifdef RT_OS_DARWIN
        AssertMsgFailed(("Not implemented on Darwin yet because of incomplete pthreads API."));
        return VERR_NOT_IMPLEMENTED;

#else /* !RT_OS_DARWIN */
        /*
         * Get current time and calc end of wait time.
         */
        struct timespec     ts = {0,0};
        clock_gettime(CLOCK_REALTIME, &ts);
        if (cMillies != 0)
        {
            ts.tv_nsec += (cMillies % 1000) * 1000000;
            ts.tv_sec  += cMillies / 1000;
            if (ts.tv_nsec >= 1000000000)
            {
                ts.tv_nsec -= 1000000000;
                ts.tv_sec++;
            }
        }

        /* take rwlock */
        int rc = pthread_rwlock_timedrdlock(&pThis->RWLock, &ts);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_READ);
        if (rc)
        {
            AssertMsg(rc == ETIMEDOUT, ("Failed read lock read-write sem %p, rc=%d.\n", hRWSem, rc));
            return RTErrConvertFromErrno(rc);
        }
#endif /* !RT_OS_DARWIN */
    }

    ASMAtomicIncU32(&pThis->cReaders);
#ifdef RTSEMRW_STRICT
    RTLockValidatorRecSharedAddOwner(&pThis->ValidatorRead, hThreadSelf, pSrcPos);
#endif
    return VINF_SUCCESS;
}


#undef RTSemRWRequestRead
RTDECL(int) RTSemRWRequestRead(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestRead(hRWSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestRead(hRWSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemRWRequestReadDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestRead(hRWSem, cMillies, &SrcPos);
}


#undef RTSemRWRequestReadNoResume
RTDECL(int) RTSemRWRequestReadNoResume(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
    /* EINTR isn't returned by the wait functions we're using. */
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestRead(hRWSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestRead(hRWSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemRWRequestReadNoResumeDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestRead(hRWSem, cMillies, &SrcPos);
}


RTDECL(int) RTSemRWReleaseRead(RTSEMRW hRWSem)
{
    /*
     * Validate input.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMRW_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);

    /*
     * Check if it's the writer.
     */
    pthread_t Self = pthread_self();
    pthread_t Writer;
    ATOMIC_GET_PTHREAD_T(&pThis->Writer, &Writer);
    if (Writer == Self)
    {
        AssertMsgReturn(pThis->cWriterReads > 0, ("pThis=%p\n", pThis), VERR_NOT_OWNER);
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecExclUnwindMixed(&pThis->ValidatorWrite, &pThis->ValidatorRead.Core);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        pThis->cWriterReads--;
        return VINF_SUCCESS;
    }

    /*
     * Try unlock it.
     */
#ifdef RTSEMRW_STRICT
    int rc9 = RTLockValidatorRecSharedCheckAndRelease(&pThis->ValidatorRead, RTThreadSelf());
    if (RT_FAILURE(rc9))
        return rc9;
#endif
#ifdef RT_OS_LINUX /* glibc (at least 2.8) may screw up when unlocking a lock we don't own. */
    if (ASMAtomicReadU32(&pThis->cReaders) == 0)
    {
        AssertMsgFailed(("Not owner of %p\n", pThis));
        return VERR_NOT_OWNER;
    }
#endif
    ASMAtomicDecU32(&pThis->cReaders);
    int rc = pthread_rwlock_unlock(&pThis->RWLock);
    if (rc)
    {
        ASMAtomicIncU32(&pThis->cReaders);
        AssertMsgFailed(("Failed read unlock read-write sem %p, rc=%d.\n", hRWSem, rc));
        return RTErrConvertFromErrno(rc);
    }
    return VINF_SUCCESS;
}


DECL_FORCE_INLINE(int) rtSemRWRequestWrite(RTSEMRW hRWSem, RTMSINTERVAL cMillies, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMRW_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);

    /*
     * Recursion?
     */
    pthread_t Self = pthread_self();
    pthread_t Writer;
    ATOMIC_GET_PTHREAD_T(&pThis->Writer, &Writer);
    if (Writer == Self)
    {
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecExclRecursion(&pThis->ValidatorWrite, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        Assert(pThis->cWrites < INT32_MAX);
        pThis->cWrites++;
        return VINF_SUCCESS;
    }

    /*
     * Try lock it.
     */
    RTTHREAD hThreadSelf = NIL_RTTHREAD;
    if (cMillies)
    {
#ifdef RTSEMRW_STRICT
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecExclCheckOrderAndBlocking(&pThis->ValidatorWrite, hThreadSelf, pSrcPos, true,
                                                              cMillies, RTTHREADSTATE_RW_WRITE, true);
        if (RT_FAILURE(rc9))
            return rc9;
#else
        hThreadSelf = RTThreadSelf();
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_WRITE, true);
        RT_NOREF_PV(pSrcPos);
#endif
    }

    if (cMillies == RT_INDEFINITE_WAIT)
    {
        /* take rwlock */
        int rc = pthread_rwlock_wrlock(&pThis->RWLock);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
        if (rc)
        {
            AssertMsgFailed(("Failed write lock read-write sem %p, rc=%d.\n", hRWSem, rc));
            return RTErrConvertFromErrno(rc);
        }
    }
    else
    {
#ifdef RT_OS_DARWIN
        AssertMsgFailed(("Not implemented on Darwin yet because of incomplete pthreads API."));
        return VERR_NOT_IMPLEMENTED;
#else /* !RT_OS_DARWIN */
        /*
         * Get current time and calc end of wait time.
         */
        struct timespec     ts = {0,0};
        clock_gettime(CLOCK_REALTIME, &ts);
        if (cMillies != 0)
        {
            ts.tv_nsec += (cMillies % 1000) * 1000000;
            ts.tv_sec  += cMillies / 1000;
            if (ts.tv_nsec >= 1000000000)
            {
                ts.tv_nsec -= 1000000000;
                ts.tv_sec++;
            }
        }

        /* take rwlock */
        int rc = pthread_rwlock_timedwrlock(&pThis->RWLock, &ts);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
        if (rc)
        {
            AssertMsg(rc == ETIMEDOUT, ("Failed read lock read-write sem %p, rc=%d.\n", hRWSem, rc));
            return RTErrConvertFromErrno(rc);
        }
#endif /* !RT_OS_DARWIN */
    }

    ATOMIC_SET_PTHREAD_T(&pThis->Writer, Self);
    pThis->cWrites = 1;
    Assert(!pThis->cReaders);
#ifdef RTSEMRW_STRICT
    RTLockValidatorRecExclSetOwner(&pThis->ValidatorWrite, hThreadSelf, pSrcPos, true);
#endif
    return VINF_SUCCESS;
}


#undef RTSemRWRequestWrite
RTDECL(int) RTSemRWRequestWrite(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestWrite(hRWSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemRWRequestWriteDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, &SrcPos);
}


#undef RTSemRWRequestWriteNoResume
RTDECL(int) RTSemRWRequestWriteNoResume(RTSEMRW hRWSem, RTMSINTERVAL cMillies)
{
    /* EINTR isn't returned by the wait functions we're using. */
#ifndef RTSEMRW_STRICT
    return rtSemRWRequestWrite(hRWSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemRWRequestWriteNoResumeDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    /* EINTR isn't returned by the wait functions we're using. */
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemRWRequestWrite(hRWSem, cMillies, &SrcPos);
}


RTDECL(int) RTSemRWReleaseWrite(RTSEMRW hRWSem)
{
    /*
     * Validate input.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMRW_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);

    /*
     * Verify ownership and implement recursion.
     */
    pthread_t Self = pthread_self();
    pthread_t Writer;
    ATOMIC_GET_PTHREAD_T(&pThis->Writer, &Writer);
    AssertMsgReturn(Writer == Self, ("pThis=%p\n", pThis), VERR_NOT_OWNER);
    AssertReturn(pThis->cWriterReads == 0 || pThis->cWrites > 1, VERR_WRONG_ORDER);

    if (pThis->cWrites > 1)
    {
#ifdef RTSEMRW_STRICT
        int rc9 = RTLockValidatorRecExclUnwind(&pThis->ValidatorWrite);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        pThis->cWrites--;
        return VINF_SUCCESS;
    }

    /*
     * Try unlock it.
     */
#ifdef RTSEMRW_STRICT
    int rc9 = RTLockValidatorRecExclReleaseOwner(&pThis->ValidatorWrite, true);
    if (RT_FAILURE(rc9))
        return rc9;
#endif

    pThis->cWrites--;
    ATOMIC_SET_PTHREAD_T(&pThis->Writer, (pthread_t)-1);
    int rc = pthread_rwlock_unlock(&pThis->RWLock);
    if (rc)
    {
        AssertMsgFailed(("Failed write unlock read-write sem %p, rc=%d.\n", hRWSem, rc));
        return RTErrConvertFromErrno(rc);
    }

    return VINF_SUCCESS;
}


RTDECL(bool) RTSemRWIsWriteOwner(RTSEMRW hRWSem)
{
    /*
     * Validate input.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, false);
    AssertMsgReturn(pThis->u32Magic == RTSEMRW_MAGIC,
                    ("pThis=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    false);

    /*
     * Check ownership.
     */
    pthread_t Self = pthread_self();
    pthread_t Writer;
    ATOMIC_GET_PTHREAD_T(&pThis->Writer, &Writer);
    return Writer == Self;
}


RTDECL(bool)  RTSemRWIsReadOwner(RTSEMRW hRWSem, bool fWannaHear)
{
    /*
     * Validate handle.
     */
    struct RTSEMRWINTERNAL *pThis = hRWSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMRW_MAGIC, false);

    /*
     * Check write ownership.  The writer is also a valid reader.
     */
    pthread_t Self = pthread_self();
    pthread_t Writer;
    ATOMIC_GET_PTHREAD_T(&pThis->Writer, &Writer);
    if (Writer == Self)
        return true;
    if (Writer != (pthread_t)-1)
        return false;

    /*
     * If there are no readers, we cannot be one of them, can we?
     */
    if (ASMAtomicReadU32(&pThis->cReaders) == 0)
        return false;

#ifdef RTSEMRW_STRICT
    /*
     * Ask the lock validator.
     */
    NOREF(fWannaHear);
    return RTLockValidatorRecSharedIsOwner(&pThis->ValidatorRead, NIL_RTTHREAD);
#else
    /*
     * Just tell the caller what he want to hear.
     */
    return fWannaHear;
#endif
}
RT_EXPORT_SYMBOL(RTSemRWIsReadOwner);


RTDECL(uint32_t) RTSemRWGetWriteRecursion(RTSEMRW hRWSem)
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
    return pThis->cWrites;
}


RTDECL(uint32_t) RTSemRWGetWriterReadRecursion(RTSEMRW hRWSem)
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
    return pThis->cWriterReads;
}


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
    return pThis->cReaders;
}

