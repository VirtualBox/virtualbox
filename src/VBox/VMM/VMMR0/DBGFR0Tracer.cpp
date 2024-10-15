/* $Id: DBGFR0Tracer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, R0 tracing part.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include "DBGFInternal.h"
#include <VBox/vmm/gvm.h>
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/vmm.h>

#include <VBox/log.h>
#include <VBox/sup.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/process.h>
#include <iprt/string.h>

#include "dtrace/VBoxVMM.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Used by DBGFR0CleanupVM to destroy a tracer instance.
 *
 * This is done during VM cleanup so that we're sure there are no active threads
 * using the tracer code.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   pTracer     The device instance.
 */
DECLHIDDEN(void) dbgfR0TracerDestroy(PGVM pGVM, PDBGFTRACERINSR0 pTracer)
{
    RT_NOREF(pGVM);

    /*
     * Free the ring-3 mapping and instance memory.
     */
    RTR0MEMOBJ hMemObj = pTracer->hMapObj;
    pTracer->hMapObj = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(hMemObj, true);

    hMemObj = pTracer->hMemObj;
    pTracer->hMemObj = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(hMemObj, true);
}


/**
 * Worker for DBGFR0TracerCreate that does the actual instantiation.
 *
 * Allocates a memory object and divides it up as follows:
 * @verbatim
   --------------------------------------
   ring-0 tracerins
   --------------------------------------
   page alignment padding
   --------------------------------------
   ring-3 tracerins
   --------------------------------------
  [page alignment padding                ] -+
  [--------------------------------------]  |- Optional, only when raw-mode is enabled.
  [raw-mode tracerins                    ] -+
  [--------------------------------------]
   shared tracer data
   --------------------------------------
   @endverbatim
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   cbRingBuf       Size of the ring buffer in bytes.
 * @param   RCPtrMapping    The raw-mode context mapping address, NIL_RTGCPTR if
 *                          not to include raw-mode.
 * @param   ppTracerInsR3   Where to return the ring-3 tracer instance address.
 * @thread  EMT(0)
 */
static int dbgfR0TracerCreateWorker(PGVM pGVM, uint32_t cbRingBuf, RTRGPTR RCPtrMapping, PDBGFTRACERINSR3 *ppTracerInsR3)
{
    /*
     * Figure out how much memory we need and allocate it.
     */
    uint32_t const cbRing0     = RT_ALIGN_32(sizeof(DBGFTRACERINSR0), HOST_PAGE_SIZE);
    uint32_t const cbRing3     = RT_ALIGN_32(sizeof(DBGFTRACERINSR3), RCPtrMapping != NIL_RTRGPTR ? HOST_PAGE_SIZE : 64);
    uint32_t const cbRC        = RCPtrMapping != NIL_RTRGPTR ? 0
                               : RT_ALIGN_32(sizeof(DBGFTRACERINSRC), 64);
    uint32_t const cbShared    = RT_ALIGN_32(sizeof(DBGFTRACERSHARED) + cbRingBuf, 64);
    uint32_t const offShared   = cbRing0 + cbRing3 + cbRC;
    uint32_t const cbTotal     = RT_ALIGN_32(cbRing0 + cbRing3 + cbRC + cbShared, HOST_PAGE_SIZE);
    AssertLogRelMsgReturn(cbTotal <= DBGF_MAX_TRACER_INSTANCE_SIZE,
                          ("Instance of tracer is too big: cbTotal=%u, max %u\n", cbTotal, DBGF_MAX_TRACER_INSTANCE_SIZE),
                          VERR_OUT_OF_RANGE);

    RTR0MEMOBJ hMemObj;
    int rc = RTR0MemObjAllocPage(&hMemObj, cbTotal, false /*fExecutable*/);
    if (RT_FAILURE(rc))
        return rc;
    RT_BZERO(RTR0MemObjAddress(hMemObj), cbTotal);

    /* Map it. */
    RTR0MEMOBJ hMapObj;
    rc = RTR0MemObjMapUserEx(&hMapObj, hMemObj, (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, RTR0ProcHandleSelf(),
                             cbRing0, cbTotal - cbRing0);
    if (RT_SUCCESS(rc))
    {
        PDBGFTRACERINSR0       pTracerIns    = (PDBGFTRACERINSR0)RTR0MemObjAddress(hMemObj);
        struct DBGFTRACERINSR3 *pTracerInsR3 = (struct DBGFTRACERINSR3 *)((uint8_t *)pTracerIns + cbRing0);

        /*
         * Initialize the ring-0 instance.
         */
        pTracerIns->pGVM          = pGVM;
        pTracerIns->hMemObj       = hMemObj;
        pTracerIns->hMapObj       = hMapObj;
        pTracerIns->pSharedR0     = (PDBGFTRACERSHARED)((uint8_t *)pTracerIns + offShared);
        pTracerIns->cbRingBuf     = cbRingBuf;
        pTracerIns->pbRingBufR0   = (uint8_t *)(pTracerIns->pSharedR0 + 1);

        /*
         * Initialize the ring-3 instance data as much as we can.
         * Note! DBGFR3Tracer.cpp does this job for ring-3 only tracers.  Keep in sync.
         */
        pTracerInsR3->pVMR3       = pGVM->pVMR3;
        pTracerInsR3->fR0Enabled  = true;
        pTracerInsR3->pSharedR3   = RTR0MemObjAddressR3(hMapObj) + cbRing3 + cbRC;
        pTracerInsR3->pbRingBufR3 = RTR0MemObjAddressR3(hMapObj) + cbRing3 + cbRC + sizeof(DBGFTRACERSHARED);

        pTracerIns->pSharedR0->idEvt            = 0;
        pTracerIns->pSharedR0->cbRingBuf        = cbRingBuf;
        pTracerIns->pSharedR0->fEvtsWaiting     = false;
        pTracerIns->pSharedR0->fFlushThrdActive = false;

        /*
         * Initialize the raw-mode instance data as much as possible.
         */
        if (RCPtrMapping != NIL_RTRCPTR)
        {
            struct DBGFTRACERINSRC *pTracerInsRC = RCPtrMapping == NIL_RTRCPTR ? NULL
                                                 : (struct DBGFTRACERINSRC *)((uint8_t *)pTracerIns + cbRing0 + cbRing3);

            pTracerInsRC->pVMRC = pGVM->pVMRC;
        }

        pGVM->dbgfr0.s.pTracerR0 = pTracerIns;

        /*
         * We're done.
         */
        *ppTracerInsR3 = RTR0MemObjAddressR3(hMapObj);
        return rc;
    }

    RTR0MemObjFree(hMemObj, true);
    return rc;
}


/**
 * Used by ring-3 DBGF to create a tracer instance that operates both in ring-3
 * and ring-0.
 *
 * Creates an instance of a tracer (for both ring-3 and ring-0, and optionally
 * raw-mode context).
 *
 * @returns VBox status code.
 * @param   pGVM    The global (ring-0) VM structure.
 * @param   pReq    Pointer to the request buffer.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) DBGFR0TracerCreateReqHandler(PGVM pGVM, PDBGFTRACERCREATEREQ pReq)
{
    LogFlow(("DBGFR0TracerCreateReqHandler:\n"));

    /*
     * Validate the request.
     */
    AssertReturn(pReq->Hdr.cbReq == sizeof(*pReq), VERR_INVALID_PARAMETER);
    pReq->pTracerInsR3 = NIL_RTR3PTR;

    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0);
    AssertRCReturn(rc, rc);

    AssertReturn(pReq->cbRingBuf <= DBGF_MAX_TRACER_INSTANCE_SIZE, VERR_OUT_OF_RANGE);

    return dbgfR0TracerCreateWorker(pGVM, pReq->cbRingBuf, NIL_RTRCPTR /** @todo new raw-mode */, &pReq->pTracerInsR3);
}

