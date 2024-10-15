/* $Id: semevent-r0drv-haiku.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Single Release Event Semaphores, Ring-0 Driver, Haiku.
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
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Haiku event semaphore.
 */
typedef struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t volatile   u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** The semaphore Id. */
    sem_id              SemId;
} RTSEMEVENTINTERNAL, *PRTSEMEVENTINTERNAL;


RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertCompile(sizeof(RTSEMEVENTINTERNAL) > sizeof(void *));
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));
    AssertPtrReturn(phEventSem, VERR_INVALID_POINTER);

    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic  = RTSEMEVENT_MAGIC;
    pThis->cRefs     = 1;
    pThis->SemId     = create_sem(0, "IPRT Semaphore Event");
    if (pThis->SemId >= B_OK)
    {
        set_sem_owner(pThis->SemId, B_SYSTEM_TEAM);
        *phEventSem = pThis;
        return VINF_SUCCESS;
    }

    RTMemFree(pThis);
    return VERR_TOO_MANY_SEMAPHORES; /** @todo r=ramshankar: use RTErrConvertFromHaikuKernReturn */
}


/**
 * Retains a reference to the event semaphore.
 *
 * @param   pThis       The event semaphore.
 */
DECLINLINE(void) rtR0SemEventHkuRetain(PRTSEMEVENTINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < 100000); NOREF(cRefs);
}


/**
 * Releases a reference to the event semaphore.
 *
 * @param   pThis       The event semaphore.
 */
DECLINLINE(void) rtR0SemEventHkuRelease(PRTSEMEVENTINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
        RTMemFree(pThis);
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs > 0);

    /*
     * Invalidate it and delete the semaphore to unblock everyone.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENT_MAGIC);
    delete_sem(pThis->SemId);
    pThis->SemId = -1;
    rtR0SemEventHkuRelease(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    rtR0SemEventHkuRetain(pThis);

    /*
     * Signal the event object.
     * We must use B_DO_NOT_RESCHEDULE since we are being used from an irq handler.
     */
    release_sem_etc(pThis->SemId, 1, B_DO_NOT_RESCHEDULE);
    rtR0SemEventHkuRelease(pThis);
    return VINF_SUCCESS;
}


/**
 * Worker for RTSemEventWaitEx and RTSemEventWaitExDebug.
 *
 * @returns VBox status code.
 * @param   pThis           The event semaphore.
 * @param   fFlags          See RTSemEventWaitEx.
 * @param   uTimeout        See RTSemEventWaitEx.
 * @param   pSrcPos         The source code position of the wait.
 */
static int rtR0SemEventWait(PRTSEMEVENTINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                            PCRTLOCKVALSRCPOS pSrcPos)
{
    status_t  status;
    int       rc;
    int32     flags = 0;
    bigtime_t timeout; /* in microseconds */

    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

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
    // likely not:
    //else
    //    flags |= B_KILL_CAN_INTERRUPT;

    rtR0SemEventHkuRetain(pThis);

    status = acquire_sem_etc(pThis->SemId, 1, flags, timeout);

    switch (status)
    {
        case B_OK:
            rc = VINF_SUCCESS;
            break;
        case B_BAD_SEM_ID:
            rc = VERR_SEM_DESTROYED;
            break;
        case B_INTERRUPTED:
            rc = VERR_INTERRUPTED;
            break;
        case B_WOULD_BLOCK:
            /* fallthrough ? */
        case B_TIMED_OUT:
            rc = VERR_TIMEOUT;
            break;
        default:
            rc = RTErrConvertFromHaikuKernReturn(status);
            break;
    }

    rtR0SemEventHkuRelease(pThis);
    return rc;
}


RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventWait(hEventSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventWait(hEventSem, fFlags, uTimeout, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemEventWaitEx);


RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventWait(hEventSem, fFlags, uTimeout, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemEventWaitExDebug);


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    /* At least that's what the API supports. */
    return 1000;
}


RTR0DECL(bool) RTSemEventIsSignalSafe(void)
{
    /** @todo check the code...   */
    return false;
}
RT_EXPORT_SYMBOL(RTSemEventIsSignalSafe);

