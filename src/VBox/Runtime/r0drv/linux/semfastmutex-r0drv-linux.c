/* $Id: semfastmutex-r0drv-linux.c 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Fast Mutex Semaphores, Ring-0 Driver, Linux.
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
#include "the-linux-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>
#if defined(RT_STRICT) || defined(IPRT_DEBUG_SEMS)
# include <iprt/thread.h>
#endif

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Wrapper for the linux semaphore structure.
 */
typedef struct RTSEMFASTMUTEXINTERNAL
{
    /** Magic value (RTSEMFASTMUTEX_MAGIC). */
    uint32_t            u32Magic;
    /** the linux semaphore. */
    struct semaphore    Semaphore;
#if defined(RT_STRICT) || defined(IPRT_DEBUG_SEMS)
    /** For check. */
    RTNATIVETHREAD volatile Owner;
#endif
} RTSEMFASTMUTEXINTERNAL, *PRTSEMFASTMUTEXINTERNAL;


RTDECL(int)  RTSemFastMutexCreate(PRTSEMFASTMUTEX phFastMtx)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Allocate.
     */
    PRTSEMFASTMUTEXINTERNAL pThis;
    pThis = (PRTSEMFASTMUTEXINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    /*
     * Initialize.
     */
    pThis->u32Magic = RTSEMFASTMUTEX_MAGIC;
    sema_init(&pThis->Semaphore, 1);
#if defined(RT_STRICT) || defined(IPRT_DEBUG_SEMS)
    pThis->Owner = NIL_RTNATIVETHREAD;
#endif

    *phFastMtx = pThis;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemFastMutexCreate);


RTDECL(int)  RTSemFastMutexDestroy(RTSEMFASTMUTEX hFastMtx)
{
    /*
     * Validate.
     */
    PRTSEMFASTMUTEXINTERNAL pThis = hFastMtx;
    if (pThis == NIL_RTSEMFASTMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMFASTMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);

    ASMAtomicWriteU32(&pThis->u32Magic, RTSEMFASTMUTEX_MAGIC_DEAD);
    RTMemFree(pThis);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemFastMutexDestroy);


RTDECL(int)  RTSemFastMutexRequest(RTSEMFASTMUTEX hFastMtx)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate.
     */
    PRTSEMFASTMUTEXINTERNAL pThis = hFastMtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMFASTMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);

    IPRT_DEBUG_SEMS_STATE(pThis, 'd');
    down(&pThis->Semaphore);
#if defined(RT_STRICT) || defined(IPRT_DEBUG_SEMS)
    IPRT_DEBUG_SEMS_STATE(pThis, 'o');
    AssertRelease(pThis->Owner == NIL_RTNATIVETHREAD);
    ASMAtomicUoWriteSize(&pThis->Owner, RTThreadNativeSelf());
#endif

    IPRT_LINUX_RESTORE_EFL_ONLY_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemFastMutexRequest);


RTDECL(int)  RTSemFastMutexRelease(RTSEMFASTMUTEX hFastMtx)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate.
     */
    PRTSEMFASTMUTEXINTERNAL pThis = hFastMtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMFASTMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);

#if defined(RT_STRICT) || defined(IPRT_DEBUG_SEMS)
    AssertRelease(pThis->Owner == RTThreadNativeSelf());
    ASMAtomicUoWriteSize(&pThis->Owner, NIL_RTNATIVETHREAD);
#endif
    up(&pThis->Semaphore);
    IPRT_DEBUG_SEMS_STATE(pThis, 'u');

    IPRT_LINUX_RESTORE_EFL_ONLY_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemFastMutexRelease);

