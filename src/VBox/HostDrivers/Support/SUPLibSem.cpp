/* $Id: SUPLibSem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Semaphores, ring-3 implementation.
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
#define LOG_GROUP LOG_GROUP_SUP
#include <VBox/sup.h>

#include <iprt/errcore.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>

#include "SUPLibInternal.h"
#include "SUPDrvIOC.h"


/**
 * Worker that makes a SUP_IOCTL_SEM_OP2 request.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle.
 * @param   uType               The semaphore type.
 * @param   hSem                The semaphore handle.
 * @param   uOp                 The operation.
 * @param   u64Arg              The argument if applicable, otherwise 0.
 */
DECLINLINE(int) supSemOp2(PSUPDRVSESSION pSession, uint32_t uType, uintptr_t hSem, uint32_t uOp, uint64_t u64Arg)
{
    NOREF(pSession);
    SUPSEMOP2 Req;
    Req.Hdr.u32Cookie           = g_u32Cookie;
    Req.Hdr.u32SessionCookie    = g_u32SessionCookie;
    Req.Hdr.cbIn                = SUP_IOCTL_SEM_OP2_SIZE_IN;
    Req.Hdr.cbOut               = SUP_IOCTL_SEM_OP2_SIZE_OUT;
    Req.Hdr.fFlags              = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc                  = VERR_INTERNAL_ERROR;
    Req.u.In.uType              = uType;
    Req.u.In.hSem               = (uint32_t)hSem;
    AssertReturn(Req.u.In.hSem == hSem, VERR_INVALID_HANDLE);
    Req.u.In.uOp                = uOp;
    Req.u.In.uReserved          = 0;
    Req.u.In.uArg.u64           = u64Arg;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_SEM_OP2, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;

    return rc;
}


/**
 * Worker that makes a SUP_IOCTL_SEM_OP3 request.
 *
 * @returns VBox status code.
 * @param   pSession            The session handle.
 * @param   uType               The semaphore type.
 * @param   hSem                The semaphore handle.
 * @param   uOp                 The operation.
 * @param   pReq                The request structure.  The caller should pick
 *                              the output data from it himself.
 */
DECLINLINE(int) supSemOp3(PSUPDRVSESSION pSession, uint32_t uType, uintptr_t hSem, uint32_t uOp, PSUPSEMOP3 pReq)
{
    NOREF(pSession);
    pReq->Hdr.u32Cookie           = g_u32Cookie;
    pReq->Hdr.u32SessionCookie    = g_u32SessionCookie;
    pReq->Hdr.cbIn                = SUP_IOCTL_SEM_OP3_SIZE_IN;
    pReq->Hdr.cbOut               = SUP_IOCTL_SEM_OP3_SIZE_OUT;
    pReq->Hdr.fFlags              = SUPREQHDR_FLAGS_DEFAULT;
    pReq->Hdr.rc                  = VERR_INTERNAL_ERROR;
    pReq->u.In.uType              = uType;
    pReq->u.In.hSem               = (uint32_t)hSem;
    AssertReturn(pReq->u.In.hSem == hSem, VERR_INVALID_HANDLE);
    pReq->u.In.uOp                = uOp;
    pReq->u.In.u32Reserved        = 0;
    pReq->u.In.u64Reserved        = 0;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_SEM_OP3, pReq, sizeof(*pReq));
    if (RT_SUCCESS(rc))
        rc = pReq->Hdr.rc;

    return rc;
}


SUPDECL(int) SUPSemEventCreate(PSUPDRVSESSION pSession, PSUPSEMEVENT phEvent)
{
    AssertPtrReturn(phEvent, VERR_INVALID_POINTER);

    int rc;
    if (!g_supLibData.fDriverless)
    {
        SUPSEMOP3 Req;
        rc = supSemOp3(pSession, SUP_SEM_TYPE_EVENT, (uintptr_t)NIL_SUPSEMEVENT, SUPSEMOP3_CREATE, &Req);
        if (RT_SUCCESS(rc))
            *phEvent = (SUPSEMEVENT)(uintptr_t)Req.u.Out.hSem;
    }
    else
    {
        RTSEMEVENT hEvent;
        rc = RTSemEventCreate(&hEvent);
        if (RT_SUCCESS(rc))
            *phEvent = (SUPSEMEVENT)hEvent;
    }
    return rc;
}


SUPDECL(int) SUPSemEventClose(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent)
{
    if (hEvent == NIL_SUPSEMEVENT)
        return VINF_SUCCESS;
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT, (uintptr_t)hEvent, SUPSEMOP2_CLOSE, 0);
    else
        rc = RTSemEventDestroy((RTSEMEVENT)hEvent);
    return rc;
}


SUPDECL(int) SUPSemEventSignal(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT, (uintptr_t)hEvent, SUPSEMOP2_SIGNAL, 0);
    else
        rc = RTSemEventSignal((RTSEMEVENT)hEvent);
    return rc;
}


SUPDECL(int) SUPSemEventWaitNoResume(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT, (uintptr_t)hEvent, SUPSEMOP2_WAIT_MS_REL, cMillies);
    else
        rc = RTSemEventWaitNoResume((RTSEMEVENT)hEvent, cMillies);
    return rc;
}


SUPDECL(int) SUPSemEventWaitNsAbsIntr(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint64_t uNsTimeout)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT, (uintptr_t)hEvent, SUPSEMOP2_WAIT_NS_ABS, uNsTimeout);
    else
    {
#if 0
        rc = RTSemEventWaitEx((RTSEMEVENT)hEvent,
                              RTSEMWAIT_FLAGS_ABSOLUTE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_NORESUME, uNsTimeout);
#else
        uint64_t nsNow = RTTimeNanoTS();
        if (nsNow < uNsTimeout)
            rc = RTSemEventWaitNoResume((RTSEMEVENT)hEvent, (uNsTimeout - nsNow + RT_NS_1MS - 1) / RT_NS_1MS);
        else
            rc = VERR_TIMEOUT;
#endif
    }
    return rc;
}


SUPDECL(int) SUPSemEventWaitNsRelIntr(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint64_t cNsTimeout)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT, (uintptr_t)hEvent, SUPSEMOP2_WAIT_NS_REL, cNsTimeout);
    else
    {
#if 0
        rc = RTSemEventWaitEx((RTSEMEVENT)hEvent,
                              RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_NORESUME, cNsTimeout);
#else
        rc = RTSemEventWaitNoResume((RTSEMEVENT)hEvent, (cNsTimeout + RT_NS_1MS - 1) / RT_NS_1MS);
#endif
    }
    return rc;
}


SUPDECL(uint32_t) SUPSemEventGetResolution(PSUPDRVSESSION pSession)
{
    if (!g_supLibData.fDriverless)
    {
        SUPSEMOP3 Req;
        int rc = supSemOp3(pSession, SUP_SEM_TYPE_EVENT, (uintptr_t)NIL_SUPSEMEVENT, SUPSEMOP3_GET_RESOLUTION, &Req);
        if (RT_SUCCESS(rc))
            return Req.u.Out.cNsResolution;
        return 1000 / 100;
    }
#if 0
    return RTSemEventGetResolution();
#else
    return RT_NS_1MS;
#endif
}





SUPDECL(int) SUPSemEventMultiCreate(PSUPDRVSESSION pSession, PSUPSEMEVENTMULTI phEventMulti)
{
    AssertPtrReturn(phEventMulti, VERR_INVALID_POINTER);

    int rc;
    if (!g_supLibData.fDriverless)
    {
        SUPSEMOP3 Req;
        rc = supSemOp3(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)NIL_SUPSEMEVENTMULTI, SUPSEMOP3_CREATE, &Req);
        if (RT_SUCCESS(rc))
            *phEventMulti = (SUPSEMEVENTMULTI)(uintptr_t)Req.u.Out.hSem;
    }
    else
    {
        RTSEMEVENTMULTI hEventMulti;
        rc = RTSemEventMultiCreate(&hEventMulti);
        if (RT_SUCCESS(rc))
            *phEventMulti = (SUPSEMEVENTMULTI)hEventMulti;
    }
    return rc;
}


SUPDECL(int) SUPSemEventMultiClose(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti)
{
    if (hEventMulti == NIL_SUPSEMEVENTMULTI)
        return VINF_SUCCESS;
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)hEventMulti, SUPSEMOP2_CLOSE, 0);
    else
        rc = RTSemEventMultiDestroy((RTSEMEVENTMULTI)hEventMulti);
    return rc;
}


SUPDECL(int) SUPSemEventMultiSignal(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)hEventMulti, SUPSEMOP2_SIGNAL, 0);
    else
        rc = RTSemEventMultiSignal((RTSEMEVENTMULTI)hEventMulti);
    return rc;
}


SUPDECL(int) SUPSemEventMultiReset(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)hEventMulti, SUPSEMOP2_RESET, 0);
    else
        rc = RTSemEventMultiReset((RTSEMEVENTMULTI)hEventMulti);
    return rc;
}


SUPDECL(int) SUPSemEventMultiWaitNoResume(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)hEventMulti, SUPSEMOP2_WAIT_MS_REL, cMillies);
    else
        rc = RTSemEventMultiWaitNoResume((RTSEMEVENTMULTI)hEventMulti, cMillies);
    return rc;
}


SUPDECL(int) SUPSemEventMultiWaitNsAbsIntr(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint64_t uNsTimeout)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)hEventMulti, SUPSEMOP2_WAIT_NS_ABS, uNsTimeout);
    else
    {
#if 0
        rc = RTSemEventMultiWaitEx((RTSEMEVENTMULTI)hEventMulti,
                                   RTSEMWAIT_FLAGS_ABSOLUTE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_NORESUME, uNsTimeout);
#else
        uint64_t nsNow = RTTimeNanoTS();
        if (nsNow < uNsTimeout)
            rc = RTSemEventMultiWaitNoResume((RTSEMEVENTMULTI)hEventMulti, (uNsTimeout - nsNow + RT_NS_1MS - 1) / RT_NS_1MS);
        else
            rc = VERR_TIMEOUT;
#endif
    }
    return rc;
}


SUPDECL(int) SUPSemEventMultiWaitNsRelIntr(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint64_t cNsTimeout)
{
    int rc;
    if (!g_supLibData.fDriverless)
        rc = supSemOp2(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)hEventMulti, SUPSEMOP2_WAIT_NS_REL, cNsTimeout);
    else
    {
#if 0
        rc = RTSemEventMultiWaitEx((RTSEMEVENTMULTI)hEventMulti,
                                   RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_NORESUME, cNsTimeout);
#else
        rc = RTSemEventMultiWaitNoResume((RTSEMEVENTMULTI)hEventMulti, (cNsTimeout + RT_NS_1MS - 1) / RT_NS_1MS);
#endif
    }
    return rc;
}


SUPDECL(uint32_t) SUPSemEventMultiGetResolution(PSUPDRVSESSION pSession)
{
    if (!g_supLibData.fDriverless)
    {
        SUPSEMOP3 Req;
        int rc = supSemOp3(pSession, SUP_SEM_TYPE_EVENT_MULTI, (uintptr_t)NIL_SUPSEMEVENTMULTI, SUPSEMOP3_GET_RESOLUTION, &Req);
        if (RT_SUCCESS(rc))
            return Req.u.Out.cNsResolution;
        return 1000 / 100;
    }
#if 0
    return RTSemEventMultiGetResolution();
#else
    return RT_NS_1MS;
#endif
}

