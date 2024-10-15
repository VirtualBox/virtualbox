/* $Id: SUPDrvSem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - Common OS agnostic.
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
#define LOG_GROUP LOG_GROUP_SUP_DRV
#define SUPDRV_AGNOSTIC
#include "SUPDrvInternal.h"

/** @todo trim this down. */
#include <iprt/param.h>
#include <iprt/alloc.h>
#include <iprt/cpuset.h>
#include <iprt/handletable.h>
#include <iprt/mp.h>
#include <iprt/power.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>

#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/errcore.h>


/**
 * Destructor for objects created by SUPSemEventCreate.
 *
 * @param   pvObj               The object handle.
 * @param   pvUser1             The IPRT event handle.
 * @param   pvUser2             NULL.
 */
static DECLCALLBACK(void) supR0SemEventDestructor(void *pvObj, void *pvUser1, void *pvUser2)
{
    Assert(pvUser2 == NULL);
    RT_NOREF2(pvObj, pvUser2);
    RTSemEventDestroy((RTSEMEVENT)pvUser1);
}


SUPDECL(int) SUPSemEventCreate(PSUPDRVSESSION pSession, PSUPSEMEVENT phEvent)
{
    int         rc;
    RTSEMEVENT  hEventReal;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    AssertPtrReturn(phEvent, VERR_INVALID_POINTER);

    /*
     * Create the event semaphore object.
     */
    rc = RTSemEventCreate(&hEventReal);
    if (RT_SUCCESS(rc))
    {
        void *pvObj = SUPR0ObjRegister(pSession, SUPDRVOBJTYPE_SEM_EVENT, supR0SemEventDestructor, hEventReal, NULL);
        if (pvObj)
        {
            uint32_t h32;
            rc = RTHandleTableAllocWithCtx(pSession->hHandleTable, pvObj, SUPDRV_HANDLE_CTX_EVENT, &h32);
            if (RT_SUCCESS(rc))
            {
                *phEvent = (SUPSEMEVENT)(uintptr_t)h32;
                return VINF_SUCCESS;
            }
            SUPR0ObjRelease(pvObj, pSession);
        }
        else
            RTSemEventDestroy(hEventReal);
    }
    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPSemEventCreate);


SUPDECL(int) SUPSemEventClose(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent)
{
    uint32_t    h32;
    PSUPDRVOBJ  pObj;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    if (hEvent == NIL_SUPSEMEVENT)
        return VINF_SUCCESS;
    h32 = (uint32_t)(uintptr_t)hEvent;
    if (h32 != (uintptr_t)hEvent)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    pObj = (PSUPDRVOBJ)RTHandleTableFreeWithCtx(pSession->hHandleTable, h32, SUPDRV_HANDLE_CTX_EVENT);
    if (!pObj)
        return VERR_INVALID_HANDLE;

    Assert(pObj->cUsage >= 2);
    SUPR0ObjRelease(pObj, pSession);        /* The free call above. */
    return SUPR0ObjRelease(pObj, pSession); /* The handle table reference. */
}
SUPR0_EXPORT_SYMBOL(SUPSemEventClose);


SUPDECL(int) SUPSemEventSignal(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent)
{
    int         rc;
    uint32_t    h32;
    PSUPDRVOBJ  pObj;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    h32 = (uint32_t)(uintptr_t)hEvent;
    if (h32 != (uintptr_t)hEvent)
        return VERR_INVALID_HANDLE;
    pObj = (PSUPDRVOBJ)RTHandleTableLookupWithCtx(pSession->hHandleTable, h32, SUPDRV_HANDLE_CTX_EVENT);
    if (!pObj)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    rc = RTSemEventSignal((RTSEMEVENT)pObj->pvUser1);

    SUPR0ObjRelease(pObj, pSession);
    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPSemEventSignal);


static int supR0SemEventWaitEx(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint32_t fFlags, uint64_t uTimeout)
{
    int         rc;
    uint32_t    h32;
    PSUPDRVOBJ  pObj;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    h32 = (uint32_t)(uintptr_t)hEvent;
    if (h32 != (uintptr_t)hEvent)
        return VERR_INVALID_HANDLE;
    pObj = (PSUPDRVOBJ)RTHandleTableLookupWithCtx(pSession->hHandleTable, h32, SUPDRV_HANDLE_CTX_EVENT);
    if (!pObj)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    rc = RTSemEventWaitEx((RTSEMEVENT)pObj->pvUser1, fFlags, uTimeout);

    SUPR0ObjRelease(pObj, pSession);
    return rc;
}


SUPDECL(int) SUPSemEventWait(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_UNINTERRUPTIBLE;
    if (cMillies == RT_INDEFINITE_WAIT)
        fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
    return supR0SemEventWaitEx(pSession, hEvent, fFlags, cMillies);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventWait);


SUPDECL(int) SUPSemEventWaitNoResume(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_INTERRUPTIBLE;
    if (cMillies == RT_INDEFINITE_WAIT)
        fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
    return supR0SemEventWaitEx(pSession, hEvent, fFlags, cMillies);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventWaitNoResume);


SUPDECL(int) SUPSemEventWaitNsAbsIntr(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint64_t uNsTimeout)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_ABSOLUTE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_INTERRUPTIBLE;
    return supR0SemEventWaitEx(pSession, hEvent, fFlags, uNsTimeout);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventWaitNsAbsIntr);


SUPDECL(int) SUPSemEventWaitNsRelIntr(PSUPDRVSESSION pSession, SUPSEMEVENT hEvent, uint64_t cNsTimeout)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_INTERRUPTIBLE;
    return supR0SemEventWaitEx(pSession, hEvent, fFlags, cNsTimeout);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventWaitNsRelIntr);


SUPDECL(uint32_t) SUPSemEventGetResolution(PSUPDRVSESSION pSession)
{
    RT_NOREF1(pSession);
    Assert(SUP_IS_SESSION_VALID(pSession));
    return RTSemEventGetResolution();
}
SUPR0_EXPORT_SYMBOL(SUPSemEventGetResolution);


/**
 * Destructor for objects created by SUPSemEventMultiCreate.
 *
 * @param   pvObj               The object handle.
 * @param   pvUser1             The IPRT event handle.
 * @param   pvUser2             NULL.
 */
static DECLCALLBACK(void) supR0SemEventMultiDestructor(void *pvObj, void *pvUser1, void *pvUser2)
{
    Assert(pvUser2 == NULL);
    RT_NOREF2(pvObj, pvUser2);
    RTSemEventMultiDestroy((RTSEMEVENTMULTI)pvUser1);
}


SUPDECL(int) SUPSemEventMultiCreate(PSUPDRVSESSION pSession, PSUPSEMEVENTMULTI phEventMulti)
{
    int             rc;
    RTSEMEVENTMULTI hEventMultReal;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    AssertPtrReturn(phEventMulti, VERR_INVALID_POINTER);

    /*
     * Create the event semaphore object.
     */
    rc = RTSemEventMultiCreate(&hEventMultReal);
    if (RT_SUCCESS(rc))
    {
        void *pvObj = SUPR0ObjRegister(pSession, SUPDRVOBJTYPE_SEM_EVENT_MULTI, supR0SemEventMultiDestructor, hEventMultReal, NULL);
        if (pvObj)
        {
            uint32_t h32;
            rc = RTHandleTableAllocWithCtx(pSession->hHandleTable, pvObj, SUPDRV_HANDLE_CTX_EVENT_MULTI, &h32);
            if (RT_SUCCESS(rc))
            {
                *phEventMulti = (SUPSEMEVENTMULTI)(uintptr_t)h32;
                return VINF_SUCCESS;
            }
            SUPR0ObjRelease(pvObj, pSession);
        }
        else
            RTSemEventMultiDestroy(hEventMultReal);
    }
    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiCreate);


SUPDECL(int) SUPSemEventMultiClose(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti)
{
    uint32_t    h32;
    PSUPDRVOBJ  pObj;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    if (hEventMulti == NIL_SUPSEMEVENTMULTI)
        return VINF_SUCCESS;
    h32 = (uint32_t)(uintptr_t)hEventMulti;
    if (h32 != (uintptr_t)hEventMulti)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    pObj = (PSUPDRVOBJ)RTHandleTableFreeWithCtx(pSession->hHandleTable, h32, SUPDRV_HANDLE_CTX_EVENT_MULTI);
    if (!pObj)
        return VERR_INVALID_HANDLE;

    Assert(pObj->cUsage >= 2);
    SUPR0ObjRelease(pObj, pSession);        /* The free call above. */
    return SUPR0ObjRelease(pObj, pSession); /* The handle table reference. */
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiClose);


SUPDECL(int) SUPSemEventMultiSignal(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti)
{
    int         rc;
    uint32_t    h32;
    PSUPDRVOBJ  pObj;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    h32 = (uint32_t)(uintptr_t)hEventMulti;
    if (h32 != (uintptr_t)hEventMulti)
        return VERR_INVALID_HANDLE;
    pObj = (PSUPDRVOBJ)RTHandleTableLookupWithCtx(pSession->hHandleTable, h32, SUPDRV_HANDLE_CTX_EVENT_MULTI);
    if (!pObj)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    rc = RTSemEventMultiSignal((RTSEMEVENTMULTI)pObj->pvUser1);

    SUPR0ObjRelease(pObj, pSession);
    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiSignal);


SUPDECL(int) SUPSemEventMultiReset(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti)
{
    int         rc;
    uint32_t    h32;
    PSUPDRVOBJ  pObj;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    h32 = (uint32_t)(uintptr_t)hEventMulti;
    if (h32 != (uintptr_t)hEventMulti)
        return VERR_INVALID_HANDLE;
    pObj = (PSUPDRVOBJ)RTHandleTableLookupWithCtx(pSession->hHandleTable, h32, SUPDRV_HANDLE_CTX_EVENT_MULTI);
    if (!pObj)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    rc = RTSemEventMultiReset((RTSEMEVENTMULTI)pObj->pvUser1);

    SUPR0ObjRelease(pObj, pSession);
    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiReset);


static int supR0SemEventMultiWaitEx(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint32_t fFlags, uint64_t uTimeout)
{
    int         rc;
    uint32_t    h32;
    PSUPDRVOBJ  pObj;

    /*
     * Input validation.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    h32 = (uint32_t)(uintptr_t)hEventMulti;
    if (h32 != (uintptr_t)hEventMulti)
        return VERR_INVALID_HANDLE;
    pObj = (PSUPDRVOBJ)RTHandleTableLookupWithCtx(pSession->hHandleTable, h32, SUPDRV_HANDLE_CTX_EVENT_MULTI);
    if (!pObj)
        return VERR_INVALID_HANDLE;

    /*
     * Do the job.
     */
    rc = RTSemEventMultiWaitEx((RTSEMEVENTMULTI)pObj->pvUser1, fFlags, uTimeout);

    SUPR0ObjRelease(pObj, pSession);
    return rc;
}


SUPDECL(int) SUPSemEventMultiWait(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_UNINTERRUPTIBLE;
    if (cMillies == RT_INDEFINITE_WAIT)
        fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
    return supR0SemEventMultiWaitEx(pSession, hEventMulti, fFlags, cMillies);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiWait);



SUPDECL(int) SUPSemEventMultiWaitNoResume(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS | RTSEMWAIT_FLAGS_INTERRUPTIBLE;
    if (cMillies == RT_INDEFINITE_WAIT)
        fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
    return supR0SemEventMultiWaitEx(pSession, hEventMulti, fFlags, cMillies);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiWaitNoResume);


SUPDECL(int) SUPSemEventMultiWaitNsAbsIntr(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint64_t uNsTimeout)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_ABSOLUTE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_INTERRUPTIBLE;
    return supR0SemEventMultiWaitEx(pSession, hEventMulti, fFlags, uNsTimeout);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiWaitNsAbsIntr);


SUPDECL(int) SUPSemEventMultiWaitNsRelIntr(PSUPDRVSESSION pSession, SUPSEMEVENTMULTI hEventMulti, uint64_t cNsTimeout)
{
    uint32_t fFlags = RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_INTERRUPTIBLE;
    return supR0SemEventMultiWaitEx(pSession, hEventMulti, fFlags, cNsTimeout);
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiWaitNsRelIntr);


SUPDECL(uint32_t) SUPSemEventMultiGetResolution(PSUPDRVSESSION pSession)
{
    RT_NOREF1(pSession);
    Assert(SUP_IS_SESSION_VALID(pSession));
    return RTSemEventMultiGetResolution();
}
SUPR0_EXPORT_SYMBOL(SUPSemEventMultiGetResolution);
