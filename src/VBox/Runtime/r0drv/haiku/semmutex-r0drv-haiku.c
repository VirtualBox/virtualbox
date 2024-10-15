/* $Id: semmutex-r0drv-haiku.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mutex Semaphores, Ring-0 Driver, Haiku.
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
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Wrapper for the Haiku (sleep) mutex.
 */
/* XXX: not optimal, maybe should use the (private)
   kernel recursive_lock ? (but it's not waitable) */
typedef struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t            u32Magic;
    /** Kernel semaphore. */
    sem_id              SemId;
    /** Current holder */
    volatile thread_id  OwnerId;
    /** Recursion count */
    int32               cRecursion;
} RTSEMMUTEXINTERNAL, *PRTSEMMUTEXINTERNAL;


RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    AssertCompile(sizeof(RTSEMMUTEXINTERNAL) > sizeof(void *));
    AssertPtrReturn(phMutexSem, VERR_INVALID_POINTER);

    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)RTMemAllocZ(sizeof(*pThis));
    if (RT_UNLIKELY(!pThis))
        return VERR_NO_MEMORY;

    pThis->u32Magic = RTSEMMUTEX_MAGIC;
    pThis->SemId = create_sem(0, "IPRT Mutex Semaphore");
    if (pThis->SemId < B_OK)
    {
        pThis->OwnerId = -1;
        pThis->cRecursion = 0;
        *phMutexSem = pThis;
        return VINF_SUCCESS;
    }
    RTMemFree(pThis);
    return VERR_TOO_MANY_SEMAPHORES;    /** @todo r=ramshankar: use RTErrConvertFromHaikuKernReturn */
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    if (pThis == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);

    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD, RTSEMMUTEX_MAGIC), VERR_INVALID_HANDLE);

    delete_sem(pThis->SemId);
    RTMemFree(pThis);

    return VINF_SUCCESS;
}


/**
 * Worker function for acquiring the mutex.
 *
 * @param hMutexSem     The mutex object.
 * @param fFlags        Mutex flags (see RTSEMWAIT_FLAGS_*)
 * @param uTimeout      Timeout in units specified by the flags.
 *
 * @return IPRT status code.
 */
static int rtSemMutexRequestEx(RTSEMMUTEX hMutexSem, uint32_t fFlags, uint64_t uTimeout)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    int                 rc;
    status_t            status;
    int32               flags = 0;
    bigtime_t           timeout; /* in microseconds */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);

    if (pThis->OwnerId == find_thread(NULL))
    {
        pThis->OwnerId++;
        return VINF_SUCCESS;
    }

    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
        timeout = B_INFINITE_TIMEOUT;
    else
    {
        if (fFlags & RTSEMWAIT_FLAGS_NANOSECS)
            timeout = uTimeout / 1000;
        else if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
            timeout = uTimeout * 1000;
        else
            return VERR_INVALID_PARAMETER;

        if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
            flags |= B_RELATIVE_TIMEOUT;
        else if (fFlags & RTSEMWAIT_FLAGS_ABSOLUTE)
            flags |= B_ABSOLUTE_TIMEOUT;
        else
            return VERR_INVALID_PARAMETER;
    }

    if (fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE)
        flags |= B_CAN_INTERRUPT;

    status = acquire_sem_etc(pThis->SemId, 1, flags, timeout);

    switch (status)
    {
        case B_OK:
            rc = VINF_SUCCESS;
            pThis->cRecursion = 1;
            pThis->OwnerId = find_thread(NULL);
            break;
        case B_BAD_SEM_ID:
            rc = VERR_SEM_DESTROYED;
            break;
        case B_INTERRUPTED:
            rc = VERR_INTERRUPTED;
            break;
        case B_WOULD_BLOCK:
            /* fallthrough? */
        case B_TIMED_OUT:
            rc = VERR_TIMEOUT;
            break;
        default:
            rc = VERR_INVALID_PARAMETER;
            break;
    }

    return rc;
}


RTDECL(int)  RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtSemMutexRequestEx(hMutexSem, RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_MILLISECS, cMillies);
}


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return RTSemMutexRequest(hMutexSem, cMillies);
}


RTDECL(int)  RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtSemMutexRequestEx(hMutexSem, RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_MILLISECS, cMillies);
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return RTSemMutexRequestNoResume(hMutexSem, cMillies);
}


RTDECL(int)  RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);

    if (pThis->OwnerId != find_thread(NULL))
        return VERR_INVALID_HANDLE;

    if (--pThis->cRecursion == 0)
    {
        pThis->OwnerId == -1;
        release_sem(pThis->SemId);
    }

    return VINF_SUCCESS;
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    AssertPtrReturn(pThis, false);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), false);

    return pThis->OwnerId != -1;
}

