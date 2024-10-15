/* $Id: semeventmulti-r0drv-haiku.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiple Release Event Semaphores, Ring-0 Driver, Haiku.
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

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/lockvalidator.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Haiku multiple release event semaphore.
 */
typedef struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value (RTSEMEVENTMULTI_MAGIC). */
    uint32_t volatile   u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** The semaphore Id. */
    sem_id              SemId;
} RTSEMEVENTMULTIINTERNAL, *PRTSEMEVENTMULTIINTERNAL;


RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /* fFlags */, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    PRTSEMEVENTMULTIINTERNAL pThis;

    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);
    pThis = (PRTSEMEVENTMULTIINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic  = RTSEMEVENTMULTI_MAGIC;
    pThis->cRefs     = 1;
    pThis->SemId     = create_sem(0, "IPRT Semaphore Event Multi");
    if (pThis->SemId < B_OK)
    {
        set_sem_owner(pThis->SemId, B_SYSTEM_TEAM);
        *phEventMultiSem = pThis;
        return VINF_SUCCESS;
    }

    RTMemFree(pThis);
    return VERR_TOO_MANY_SEMAPHORES;  /** @todo r=ramshankar: use RTErrConvertFromHaikuKernReturn */
}


/**
 * Retain a reference to the semaphore.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventMultiHkuRetain(PRTSEMEVENTMULTIINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000);
}


/**
 * Release a reference, destroy the thing if necessary.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventMultiHkuRelease(PRTSEMEVENTMULTIINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
    {
        Assert(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC);
        RTMemFree(pThis);
    }
}


RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (pThis == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    Assert(pThis->cRefs > 0);

    /*
     * Invalidate it and signal the object just in case.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENTMULTI_MAGIC);
    delete_sem(pThis->SemId);
    pThis->SemId = -1;
    rtR0SemEventMultiHkuRelease(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiHkuRetain(pThis);

    /*
     * Signal the event object.
     * We must use B_DO_NOT_RESCHEDULE since we are being used from an irq handler.
     */
    release_sem_etc(pThis->SemId, 1, B_RELEASE_ALL | B_DO_NOT_RESCHEDULE);
    rtR0SemEventMultiHkuRelease(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiHkuRetain(pThis);

    /*
     * Reset it.
     */
    //FIXME: what should I do ???
    // delete_sem + create_sem ??
    rtR0SemEventMultiHkuRelease(pThis);
    return VINF_SUCCESS;
}


/**
 * Worker for RTSemEventMultiWaitEx and RTSemEventMultiWaitExDebug.
 *
 * @returns VBox status code.
 * @param   pThis           The event semaphore.
 * @param   fFlags          See RTSemEventMultiWaitEx.
 * @param   uTimeout        See RTSemEventMultiWaitEx.
 * @param   pSrcPos         The source code position of the wait.
 */
static int rtR0SemEventMultiHkuWait(PRTSEMEVENTMULTIINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                                    PCRTLOCKVALSRCPOS pSrcPos)
{
    status_t    status;
    int         rc;
    int32     flags = 0;
    bigtime_t timeout; /* in microseconds */

    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
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

    rtR0SemEventMultiHkuRetain(pThis);

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
            /* fallthrough? */
        case B_TIMED_OUT:
            rc = VERR_TIMEOUT;
            break;
        default:
            rc = RTErrConvertFromHaikuKernReturn(status);
            break;
    }

    rtR0SemEventMultiHkuRelease(pThis);
    return rc;
}


RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventMultiHkuWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventMultiHkuWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemEventMultiWaitEx);


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventMultiHkuWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemEventMultiWaitExDebug);


RTDECL(uint32_t) RTSemEventMultiGetResolution(void)
{
    /* At least that's what the API supports. */
    return 1000;
}
RT_EXPORT_SYMBOL(RTSemEventMultiGetResolution);


RTR0DECL(bool) RTSemEventMultiIsSignalSafe(void)
{
    /** @todo check the code...   */
    return false;
}
RT_EXPORT_SYMBOL(RTSemEventMultiIsSignalSafe);

