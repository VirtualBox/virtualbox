/* $Id: semmutex-r0drv-freebsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mutex Semaphores, Ring-0 Driver, FreeBSD.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define RTSEMMUTEX_WITHOUT_REMAPPING
#include "the-freebsd-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Wrapper for the FreeBSD (sleep) mutex.
 */
typedef struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t            u32Magic;
    /** The FreeBSD shared/exclusive lock mutex. */
    struct sx           SxLock;
} RTSEMMUTEXINTERNAL, *PRTSEMMUTEXINTERNAL;


RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    AssertCompile(sizeof(RTSEMMUTEXINTERNAL) > sizeof(void *));
    AssertPtrReturn(phMutexSem, VERR_INVALID_POINTER);

    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic = RTSEMMUTEX_MAGIC;
        sx_init_flags(&pThis->SxLock, "IPRT Mutex Semaphore", SX_RECURSE);

        *phMutexSem = pThis;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    if (pThis == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);

    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD, RTSEMMUTEX_MAGIC), VERR_INVALID_HANDLE);

    sx_destroy(&pThis->SxLock);
    RTMemFree(pThis);

    return VINF_SUCCESS;
}


RTDECL(int)  RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    int                 rc;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);

    if (cMillies == RT_INDEFINITE_WAIT)
    {
        sx_xlock(&pThis->SxLock);
        rc = VINF_SUCCESS;
    }
    else if (!cMillies)
    {
        if (sx_try_xlock(&pThis->SxLock))
            rc = VINF_SUCCESS;
        else
            rc = VERR_TIMEOUT;
    }
    /*
     * GROSS HACK: poll implementation of timeout.
     */
    /** @todo Implement timeouts in RTSemMutexRequest. */
    else if (sx_try_xlock(&pThis->SxLock))
        rc = VINF_SUCCESS;
    else
    {
        uint64_t StartTS = RTTimeSystemMilliTS();
        rc = VERR_TIMEOUT;
        do
        {
            RTThreadSleep(1);
            if (sx_try_xlock(&pThis->SxLock))
            {
                rc = VINF_SUCCESS;
                break;
            }
        } while (RTTimeSystemMilliTS() - StartTS < cMillies);
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return RTSemMutexRequest(hMutexSem, cMillies);
}


RTDECL(int)  RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    int                 rc;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);

    if (cMillies == RT_INDEFINITE_WAIT)
    {
        if (!sx_xlock_sig(&pThis->SxLock))
            rc = VINF_SUCCESS;
        else
            rc = VERR_INTERRUPTED;
    }
    else if (!cMillies)
    {
        if (sx_try_xlock(&pThis->SxLock))
            rc = VINF_SUCCESS;
        else
            rc = VERR_TIMEOUT;
    }
    /*
     * GROSS HACK: poll implementation of timeout.
     */
    /** @todo Implement timeouts and interrupt checks in
     *        RTSemMutexRequestNoResume. */
    else if (sx_try_xlock(&pThis->SxLock))
        rc = VINF_SUCCESS;
    else
    {
        uint64_t StartTS = RTTimeSystemMilliTS();
        rc = VERR_TIMEOUT;
        do
        {
            RTThreadSleep(1);
            if (sx_try_xlock(&pThis->SxLock))
            {
                rc = VINF_SUCCESS;
                break;
            }
        } while (RTTimeSystemMilliTS() - StartTS < cMillies);
    }

    return VINF_SUCCESS;
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

    sx_xunlock(&pThis->SxLock);
    return VINF_SUCCESS;
}



RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    AssertPtrReturn(pThis, false);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), false);

    return sx_xlocked(&pThis->SxLock);
}

