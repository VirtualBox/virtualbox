/* $Id: semmutex-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mutex Semaphores, Windows.
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
#define LOG_GROUP RTLOGGROUP_SEMAPHORE
#include <iprt/win/windows.h>

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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Posix internal representation of a Mutex semaphore. */
struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t                u32Magic;
    /** Recursion count. */
    uint32_t volatile       cRecursions;
    /** The owner thread. */
    RTNATIVETHREAD volatile hNativeOwner;
    /** The mutex handle. */
    HANDLE                  hMtx;
#ifdef RTSEMMUTEX_STRICT
    /** Lock validator record associated with this mutex. */
    RTLOCKVALRECEXCL        ValidatorRec;
#endif
};



#undef RTSemMutexCreate
RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    return RTSemMutexCreateEx(phMutexSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
}


RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMMUTEX_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Create the semaphore.
     */
    int rc;
    HANDLE hMtx = CreateMutex(NULL, FALSE, NULL);
    if (hMtx)
    {
        RTSEMMUTEXINTERNAL *pThis = (RTSEMMUTEXINTERNAL *)RTMemAlloc(sizeof(*pThis));
        if (pThis)
        {
            pThis->u32Magic     = RTSEMMUTEX_MAGIC;
            pThis->hMtx         = hMtx;
            pThis->hNativeOwner = NIL_RTNATIVETHREAD;
            pThis->cRecursions  = 0;
#ifdef RTSEMMUTEX_STRICT
            if (!pszNameFmt)
            {
                static uint32_t volatile s_iMutexAnon = 0;
                RTLockValidatorRecExclInit(&pThis->ValidatorRec, hClass, uSubClass, pThis,
                                           !(fFlags & RTSEMMUTEX_FLAGS_NO_LOCK_VAL),
                                           "RTSemMutex-%u", ASMAtomicIncU32(&s_iMutexAnon) - 1);
            }
            else
            {
                va_list va;
                va_start(va, pszNameFmt);
                RTLockValidatorRecExclInitV(&pThis->ValidatorRec, hClass, uSubClass, pThis,
                                            !(fFlags & RTSEMMUTEX_FLAGS_NO_LOCK_VAL), pszNameFmt, va);
                va_end(va);
            }
#else
            RT_NOREF_PV(hClass); RT_NOREF_PV(uSubClass); RT_NOREF_PV(pszNameFmt);
#endif
            *phMutexSem = pThis;
            return VINF_SUCCESS;
        }

        rc = VERR_NO_MEMORY;
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    return rc;
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    if (pThis == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Close semaphore handle.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD, RTSEMMUTEX_MAGIC), VERR_INVALID_HANDLE);
    HANDLE hMtx = pThis->hMtx;
    ASMAtomicWritePtr(&pThis->hMtx, INVALID_HANDLE_VALUE);

    int rc = VINF_SUCCESS;
    if (!CloseHandle(hMtx))
    {
        rc = RTErrConvertFromWin32(GetLastError());
        AssertMsgFailed(("%p rc=%d lasterr=%d\n", pThis->hMtx, rc, GetLastError()));
    }

#ifdef RTSEMMUTEX_STRICT
    RTLockValidatorRecExclDelete(&pThis->ValidatorRec);
#endif
    RTMemFree(pThis);
    return rc;
}


RTDECL(uint32_t) RTSemMutexSetSubClass(RTSEMMUTEX hMutexSem, uint32_t uSubClass)
{
#ifdef RTSEMMUTEX_STRICT
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);

    return RTLockValidatorRecExclSetSubClass(&pThis->ValidatorRec, uSubClass);
#else
    RT_NOREF_PV(hMutexSem); RT_NOREF_PV(uSubClass);
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}


/**
 * Internal worker for RTSemMutexRequestNoResume and it's debug companion.
 *
 * @returns Same as RTSEmMutexRequestNoResume
 * @param   hMutexSem            The mutex handle.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   pSrcPos             The source position of the caller.
 */
DECL_FORCE_INLINE(int) rtSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Check for recursive entry.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeOwner;
    ASMAtomicReadHandle(&pThis->hNativeOwner, &hNativeOwner);
    if (hNativeOwner == hNativeSelf)
    {
#ifdef RTSEMMUTEX_STRICT
        int rc9 = RTLockValidatorRecExclRecursion(&pThis->ValidatorRec, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        ASMAtomicIncU32(&pThis->cRecursions);
        return VINF_SUCCESS;
    }

    /*
     * Lock mutex semaphore.
     */
    RTTHREAD        hThreadSelf = NIL_RTTHREAD;
    if (cMillies > 0)
    {
#ifdef RTSEMMUTEX_STRICT
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecExclCheckOrderAndBlocking(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true,
                                                              cMillies, RTTHREADSTATE_MUTEX, true);
        if (RT_FAILURE(rc9))
            return rc9;
#else
        hThreadSelf = RTThreadSelf();
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_MUTEX, true);
        RT_NOREF_PV(pSrcPos);
#endif
    }
    DWORD rc = WaitForSingleObjectEx(pThis->hMtx,
                                     cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies,
                                     TRUE /*fAlertable*/);
    RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_MUTEX);
    switch (rc)
    {
        case WAIT_OBJECT_0:
#ifdef RTSEMMUTEX_STRICT
            RTLockValidatorRecExclSetOwner(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true);
#endif
            ASMAtomicWriteHandle(&pThis->hNativeOwner, hNativeSelf);
            ASMAtomicWriteU32(&pThis->cRecursions, 1);
            return VINF_SUCCESS;

        case WAIT_TIMEOUT:          return VERR_TIMEOUT;
        case WAIT_IO_COMPLETION:    return VERR_INTERRUPTED;
        case WAIT_ABANDONED:        return VERR_SEM_OWNER_DIED;
        default:
            AssertMsgFailed(("%u\n",  rc));
        case WAIT_FAILED:
        {
            int rc2 = RTErrConvertFromWin32(GetLastError());
            AssertMsgFailed(("Wait on hMutexSem %p failed, rc=%d lasterr=%d\n", hMutexSem, rc, GetLastError()));
            if (rc2 != VINF_SUCCESS)
                return rc2;

            AssertMsgFailed(("WaitForSingleObject(event) -> rc=%d while converted lasterr=%d\n", rc, rc2));
            return VERR_INTERNAL_ERROR;
        }
    }
}


#undef RTSemMutexRequestNoResume
RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMMUTEX_STRICT
    return rtSemMutexRequestNoResume(hMutexSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemMutexRequestNoResume(hMutexSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemMutexRequestNoResume(hMutexSem, cMillies, &SrcPos);
}


RTDECL(int) RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Check ownership and recursions.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeOwner;
    ASMAtomicReadHandle(&pThis->hNativeOwner, &hNativeOwner);
    if (RT_UNLIKELY(hNativeOwner != hNativeSelf))
    {
        AssertMsgFailed(("Not owner of mutex %p!! hNativeSelf=%RTntrd Owner=%RTntrd cRecursions=%d\n",
                         pThis, hNativeSelf, hNativeOwner, pThis->cRecursions));
        return VERR_NOT_OWNER;
    }
    if (pThis->cRecursions > 1)
    {
#ifdef RTSEMMUTEX_STRICT
        int rc9 = RTLockValidatorRecExclUnwind(&pThis->ValidatorRec);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        ASMAtomicDecU32(&pThis->cRecursions);
        return VINF_SUCCESS;
    }

    /*
     * Unlock mutex semaphore.
     */
#ifdef RTSEMMUTEX_STRICT
    int rc9 = RTLockValidatorRecExclReleaseOwner(&pThis->ValidatorRec, false);
    if (RT_FAILURE(rc9))
        return rc9;
#endif
    ASMAtomicWriteU32(&pThis->cRecursions, 0);
    ASMAtomicWriteHandle(&pThis->hNativeOwner, NIL_RTNATIVETHREAD);

    if (ReleaseMutex(pThis->hMtx))
        return VINF_SUCCESS;

    int rc = RTErrConvertFromWin32(GetLastError());
    AssertMsgFailed(("%p/%p, rc=%Rrc lasterr=%d\n", pThis, pThis->hMtx, rc, GetLastError()));
    return rc;
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, false);

    RTNATIVETHREAD hNativeOwner;
    ASMAtomicReadHandle(&pThis->hNativeOwner, &hNativeOwner);
    return hNativeOwner != NIL_RTNATIVETHREAD;
}

