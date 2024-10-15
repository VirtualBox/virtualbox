/* $Id: ioqueuebase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - I/O queue, Base/Public API.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_IOQUEUE
#include <iprt/ioqueue.h>

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#include "internal/ioqueue.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Internal I/O queue instance data.
 */
typedef struct RTIOQUEUEINT
{
    /** Magic identifying the I/O queue structure. */
    uint32_t                    u32Magic;
    /** Pointer to the provider vtable. */
    PCRTIOQUEUEPROVVTABLE       pVTbl;
    /** I/O queue provider instance handle. */
    RTIOQUEUEPROV               hIoQueueProv;
    /** Maximum number of submission queue entries - constant. */
    uint32_t                    cSqEntries;
    /** Maximum number of completion queue entries - constant. */
    uint32_t                    cCqEntries;
    /** Number of currently committed and not completed requests. */
    volatile uint32_t           cReqsCommitted;
    /** Number of prepared requests. */
    volatile uint32_t           cReqsPrepared;
    /** Start of the provider specific instance data - vvariable in size. */
    uint8_t                     abInst[1];
} RTIOQUEUEINT;
/** Pointer to the internal I/O queue instance data. */
typedef RTIOQUEUEINT *PRTIOQUEUEINT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Array of I/O queue providers we know about, order is important for each type.
 * The best suited ones for each platform should come first.
 */
static PCRTIOQUEUEPROVVTABLE g_apIoQueueProviders[] =
{
#if defined(RT_OS_LINUX)
    &g_RTIoQueueLnxIoURingProv,
#endif
#ifndef RT_OS_OS2
    &g_RTIoQueueAioFileProv,
#endif
    &g_RTIoQueueStdFileProv
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


RTDECL(PCRTIOQUEUEPROVVTABLE) RTIoQueueProviderGetBestForHndType(RTHANDLETYPE enmHnd)
{
    /* Go through the array and pick the first supported provider for the given handle type. */
    for (unsigned i = 0; i < RT_ELEMENTS(g_apIoQueueProviders); i++)
    {
        PCRTIOQUEUEPROVVTABLE pIoQueueProv = g_apIoQueueProviders[i];
        if (   pIoQueueProv->enmHnd == enmHnd
            && pIoQueueProv->pfnIsSupported())
            return pIoQueueProv;
    }

    return NULL;
}


RTDECL(PCRTIOQUEUEPROVVTABLE) RTIoQueueProviderGetById(const char *pszId)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_apIoQueueProviders); i++)
    {
        PCRTIOQUEUEPROVVTABLE pIoQueueProv = g_apIoQueueProviders[i];
        if (!strcmp(pIoQueueProv->pszId, pszId))
            return pIoQueueProv;
    }

    return NULL;
}


RTDECL(int) RTIoQueueCreate(PRTIOQUEUE phIoQueue, PCRTIOQUEUEPROVVTABLE pProvVTable,
                            uint32_t fFlags, uint32_t cSqEntries, uint32_t cCqEntries)
{
    AssertPtrReturn(phIoQueue, VERR_INVALID_POINTER);
    AssertPtrReturn(pProvVTable, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(cSqEntries > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cCqEntries > 0, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    PRTIOQUEUEINT pThis = (PRTIOQUEUEINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTIOQUEUEINT, abInst[pProvVTable->cbIoQueueProv]));
    if (RT_LIKELY(pThis))
    {
        pThis->pVTbl          = pProvVTable;
        pThis->hIoQueueProv   = (RTIOQUEUEPROV)&pThis->abInst[0];
        pThis->cSqEntries     = cSqEntries;
        pThis->cCqEntries     = cCqEntries;
        pThis->cReqsCommitted = 0;
        pThis->cReqsPrepared  = 0;

        rc = pThis->pVTbl->pfnQueueInit(pThis->hIoQueueProv, fFlags, cSqEntries, cCqEntries);
        if (RT_SUCCESS(rc))
        {
            *phIoQueue = pThis;
            return VINF_SUCCESS;
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTIoQueueDestroy(RTIOQUEUE hIoQueue)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(ASMAtomicReadU32(&pThis->cReqsCommitted) == 0, VERR_IOQUEUE_BUSY);

    pThis->pVTbl->pfnQueueDestroy(pThis->hIoQueueProv);
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTIoQueueHandleRegister(RTIOQUEUE hIoQueue, PCRTHANDLE pHandle)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    /** @todo Efficiently check that handle wasn't registered previously. */
    return pThis->pVTbl->pfnHandleRegister(pThis->hIoQueueProv, pHandle);
}


RTDECL(int) RTIoQueueHandleDeregister(RTIOQUEUE hIoQueue, PCRTHANDLE pHandle)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    /** @todo Efficiently check that handle was registered previously. */
    return pThis->pVTbl->pfnHandleDeregister(pThis->hIoQueueProv, pHandle);
}


RTDECL(int) RTIoQueueRequestPrepare(RTIOQUEUE hIoQueue, PCRTHANDLE pHandle, RTIOQUEUEOP enmOp,
                                    uint64_t off, void *pvBuf, size_t cbBuf, uint32_t fReqFlags,
                                    void *pvUser)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pHandle->enmType == pThis->pVTbl->enmHnd, VERR_INVALID_HANDLE);

    /** @todo Efficiently check that handle was registered previously. */
    int rc = pThis->pVTbl->pfnReqPrepare(pThis->hIoQueueProv, pHandle, enmOp, off, pvBuf, cbBuf,
                                         fReqFlags, pvUser);
    if (RT_SUCCESS(rc))
        ASMAtomicIncU32(&pThis->cReqsPrepared);

    return rc;
}


RTDECL(int) RTIoQueueRequestPrepareSg(RTIOQUEUE hIoQueue, PCRTHANDLE pHandle, RTIOQUEUEOP enmOp,
                                      uint64_t off, PCRTSGBUF pSgBuf, size_t cbSg, uint32_t fReqFlags,
                                      void *pvUser)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pHandle->enmType == pThis->pVTbl->enmHnd, VERR_INVALID_HANDLE);

    /** @todo Efficiently check that handle was registered previously. */
    int rc = pThis->pVTbl->pfnReqPrepareSg(pThis->hIoQueueProv, pHandle, enmOp, off, pSgBuf, cbSg,
                                           fReqFlags, pvUser);
    if (RT_SUCCESS(rc))
        ASMAtomicIncU32(&pThis->cReqsPrepared);

    return rc;
}


RTDECL(int) RTIoQueueCommit(RTIOQUEUE hIoQueue)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(ASMAtomicReadU32(&pThis->cReqsPrepared) > 0, VERR_IOQUEUE_EMPTY);

    uint32_t cReqsPreparedOld = 0;
    uint32_t cReqsCommitted = 0;
    int rc = VINF_SUCCESS;
    do
    {
        rc = pThis->pVTbl->pfnCommit(pThis->hIoQueueProv, &cReqsCommitted);
        if (RT_SUCCESS(rc))
        {
            ASMAtomicAddU32(&pThis->cReqsCommitted, cReqsCommitted);
            cReqsPreparedOld = ASMAtomicSubU32(&pThis->cReqsPrepared, cReqsCommitted);
        }
    } while (RT_SUCCESS(rc) && cReqsPreparedOld - cReqsCommitted > 0);

    return rc;
}


RTDECL(int) RTIoQueueEvtWait(RTIOQUEUE hIoQueue, PRTIOQUEUECEVT paCEvt, uint32_t cCEvt, uint32_t cMinWait,
                             uint32_t *pcCEvt, uint32_t fFlags)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(paCEvt, VERR_INVALID_POINTER);
    AssertReturn(cCEvt > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cMinWait > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcCEvt, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(ASMAtomicReadU32(&pThis->cReqsCommitted) > 0, VERR_IOQUEUE_EMPTY);

    *pcCEvt = 0;
    int rc = pThis->pVTbl->pfnEvtWait(pThis->hIoQueueProv, paCEvt, cCEvt, cMinWait, pcCEvt, fFlags);
    if (   (RT_SUCCESS(rc) || rc == VERR_INTERRUPTED)
        && *pcCEvt > 0)
        ASMAtomicSubU32(&pThis->cReqsCommitted, *pcCEvt);

    return rc;
}


RTDECL(int) RTIoQueueEvtWaitWakeup(RTIOQUEUE hIoQueue)
{
    PRTIOQUEUEINT pThis = hIoQueue;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    return pThis->pVTbl->pfnEvtWaitWakeup(pThis->hIoQueueProv);
}

