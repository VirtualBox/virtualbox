/* $Id: semspinmutex-r3-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Spinning Mutex Semaphores, Ring-3, Generic.
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

#include <iprt/alloc.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>



RTDECL(int) RTSemSpinMutexCreate(PRTSEMSPINMUTEX phSpinMtx, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~RTSEMSPINMUTEX_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtr(phSpinMtx);

    PRTCRITSECT pCritSect = (PRTCRITSECT)RTMemAlloc(sizeof(RTCRITSECT));
    if (!pCritSect)
        return VERR_NO_MEMORY;
    int rc = RTCritSectInitEx(pCritSect, RTCRITSECT_FLAGS_NO_NESTING | RTCRITSECT_FLAGS_NO_LOCK_VAL,
                              NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "RTSemSpinMutex");
    if (RT_SUCCESS(rc))
        *phSpinMtx = (RTSEMSPINMUTEX)pCritSect;
    else
        RTMemFree(pCritSect);
    return rc;
}
RT_EXPORT_SYMBOL(RTSemSpinMutexCreate);


RTDECL(int) RTSemSpinMutexDestroy(RTSEMSPINMUTEX hSpinMtx)
{
    if (hSpinMtx == NIL_RTSEMSPINMUTEX)
        return VERR_INVALID_PARAMETER;
    PRTCRITSECT pCritSect = (PRTCRITSECT)hSpinMtx;
    int rc = RTCritSectDelete(pCritSect);
    if (RT_SUCCESS(rc))
        RTMemFree(pCritSect);
    return rc;
}
RT_EXPORT_SYMBOL(RTSemSpinMutexDestroy);


RTDECL(int) RTSemSpinMutexTryRequest(RTSEMSPINMUTEX hSpinMtx)
{
    return RTCritSectTryEnter((PRTCRITSECT)hSpinMtx);

}
RT_EXPORT_SYMBOL(RTSemSpinMutexTryRequest);


RTDECL(int) RTSemSpinMutexRequest(RTSEMSPINMUTEX hSpinMtx)
{
    return RTCritSectEnter((PRTCRITSECT)hSpinMtx);
}
RT_EXPORT_SYMBOL(RTSemSpinMutexRequest);


RTDECL(int) RTSemSpinMutexRelease(RTSEMSPINMUTEX hSpinMtx)
{
    return RTCritSectLeave((PRTCRITSECT)hSpinMtx);
}
RT_EXPORT_SYMBOL(RTSemSpinMutexRelease);

