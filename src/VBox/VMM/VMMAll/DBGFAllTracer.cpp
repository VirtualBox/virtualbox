/* $Id: DBGFAllTracer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, All Context Code tracing part.
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
#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/vmm/dbgf.h>
#if defined(IN_RING3)
# include <VBox/vmm/uvm.h>
# include <VBox/vmm/vm.h>
#elif defined(IN_RING0)
# include <VBox/vmm/gvm.h>
#else
# error "Invalid environment"
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Returns the current context tracer instance of the given VM instance.
 *
 * @returns Current context pointer to the DBGF tracer instance.
 */
DECLINLINE(PDBGFTRACERINSCC) dbgfTracerGetInstance(PVMCC pVM)
{
#if defined(IN_RING0)
     return pVM->dbgfr0.s.pTracerR0;
#elif defined(IN_RING3)
     PUVM pUVM = pVM->pUVM;
     return pUVM->dbgf.s.pTracerR3;
#elif defined(IN_RC)
# error "Not implemented"
#else
# error "No/Invalid context specified"
#endif
}


/**
 * Returns the size of the tracing ring buffer.
 *
 * @returns Size of the ring buffer in bytes.
 * @param   pThisCC                 The event tracer instance current context data.
 */
DECLINLINE(size_t) dbgfTracerGetRingBufSz(PDBGFTRACERINSCC pThisCC)
{
#if defined(IN_RING0) /* For R0 we are extra cautious and use the ring buffer size stored in R0 memory so R3 can't corrupt it. */
    return pThisCC->cbRingBuf;
#else
    return pThisCC->CTX_SUFF(pShared)->cbRingBuf;
#endif
}


/**
 * Posts a single event descriptor to the ring buffer of the given tracer instance - extended version.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   pThisCC                 The event tracer instance current context data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   enmTraceEvt             The trace event type posted.
 * @param   idEvtPrev               The previous event ID the posted event links to.
 * @param   pvEvtDesc               The event descriptor to copy after the header.
 * @param   cbEvtDesc               Size of the event descriptor.
 * @param   pidEvt                  Where to store the assigned event ID, optional.
 */
static int dbgfTracerEvtPostEx(PVMCC pVM, PDBGFTRACERINSCC pThisCC, DBGFTRACEREVTSRC hEvtSrc,
                               DBGFTRACEREVT enmTraceEvt, uint64_t idEvtPrev, const void *pvEvtDesc,
                               size_t cbEvtDesc, uint64_t *pidEvt)
{
    LogFlowFunc(("pVM=%p pThisCC=%p hEvtSrc=%llu enmTraceEvt=%u idEvtPrev=%llu pvEvtDesc=%p cbEvtDesc=%zu pidEvt=%p\n",
                 pVM, pThisCC, hEvtSrc, enmTraceEvt, idEvtPrev, pvEvtDesc, cbEvtDesc, pidEvt));

    PDBGFTRACERSHARED pSharedCC = pThisCC->CTX_SUFF(pShared);
    size_t cRingBufEvts = dbgfTracerGetRingBufSz(pThisCC) / DBGF_TRACER_EVT_SZ;
    AssertReturn(cRingBufEvts, VERR_DBGF_TRACER_IPE_1);
    AssertReturn(cbEvtDesc <= DBGF_TRACER_EVT_PAYLOAD_SZ, VERR_DBGF_TRACER_IPE_1);

    /* Grab a new event ID first. */
    uint64_t idEvt = ASMAtomicIncU64(&pSharedCC->idEvt) - 1;
    uint64_t idxRingBuf = idEvt % cRingBufEvts; /* This gives the index in the ring buffer for the event. */
    PDBGFTRACEREVTHDR pEvtHdr = (PDBGFTRACEREVTHDR)(pThisCC->CTX_SUFF(pbRingBuf) + idxRingBuf * DBGF_TRACER_EVT_SZ);

    if (RT_UNLIKELY(ASMAtomicReadU64(&pEvtHdr->idEvt) != DBGF_TRACER_EVT_HDR_ID_INVALID))
    {
        /** @todo The event ring buffer is full and we need to go back (from R0 to R3) and wait for the flusher thread to
         * get its act together.
         */
        AssertMsgFailed(("Flush thread can't keep up with event amount!\n"));
    }

    /* Write the event and kick the flush thread if necessary. */
    if (cbEvtDesc)
        memcpy(pEvtHdr + 1, pvEvtDesc, cbEvtDesc);
    pEvtHdr->idEvtPrev   = idEvtPrev;
    pEvtHdr->hEvtSrc     = hEvtSrc;
    pEvtHdr->enmEvt      = enmTraceEvt;
    pEvtHdr->fFlags      = DBGF_TRACER_EVT_HDR_F_DEFAULT;
    ASMAtomicWriteU64(&pEvtHdr->idEvt, idEvt);

    int rc = VINF_SUCCESS;
    if (!ASMAtomicXchgBool(&pSharedCC->fEvtsWaiting, true))
    {
        if (!ASMAtomicXchgBool(&pSharedCC->fFlushThrdActive, true))
            rc = SUPSemEventSignal(pVM->pSession, pSharedCC->hSupSemEvtFlush);
    }

    if (pidEvt)
        *pidEvt = idEvt;
    return rc;
}


/**
 * Posts a single event descriptor to the ring buffer of the given tracer instance.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   pThisCC                 The event tracer instance current context data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   enmTraceEvt             The trace event type posted.
 * @param   pvEvtDesc               The event descriptor to copy after the header.
 * @param   pidEvt                  Where to store the assigned event ID, optional.
 */
DECLINLINE(int) dbgfTracerEvtPostSingle(PVMCC pVM, PDBGFTRACERINSCC pThisCC, DBGFTRACEREVTSRC hEvtSrc,
                                        DBGFTRACEREVT enmTraceEvt, const void *pvEvtDesc, uint64_t *pidEvt)
{
    return dbgfTracerEvtPostEx(pVM, pThisCC, hEvtSrc, enmTraceEvt, DBGF_TRACER_EVT_HDR_ID_INVALID,
                               pvEvtDesc, DBGF_TRACER_EVT_PAYLOAD_SZ, pidEvt);
}


#ifdef IN_RING3
/**
 * Posts a single event descriptor to the ring buffer of the given tracer instance - R3 only variant
 * (used for the register/deregister event source events currently).
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   pThisCC                 The event tracer instance current context data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   enmTraceEvt             The trace event type posted.
 * @param   pvEvtDesc               The event descriptor to copy after the header.
 * @param   cbEvtDesc               Event descriptor size in bytes.
 * @param   pidEvt                  Where to store the assigned event ID, optional.
 */
DECLHIDDEN(int) dbgfTracerR3EvtPostSingle(PVMCC pVM, PDBGFTRACERINSCC pThisCC, DBGFTRACEREVTSRC hEvtSrc,
                                          DBGFTRACEREVT enmTraceEvt, const void *pvEvtDesc, size_t cbEvtDesc,
                                          uint64_t *pidEvt)
{
    return dbgfTracerEvtPostEx(pVM, pThisCC, hEvtSrc, enmTraceEvt, DBGF_TRACER_EVT_HDR_ID_INVALID,
                               pvEvtDesc, cbEvtDesc, pidEvt);
}
#endif

/**
 * Copies the given MMIO value into the event descriptor based on the given size.
 *
 * @param   pEvtMmio                Pointer to the MMIO event descriptor to fill.
 * @param   pvVal                   The value to copy.
 * @param   cbVal                   Size of the value in bytes.
 */
static void dbgfTracerEvtMmioCopyVal(PDBGFTRACEREVTMMIO pEvtMmio, const void *pvVal, size_t cbVal)
{
    switch (cbVal)
    {
        case 1:
            pEvtMmio->u64Val = *(uint8_t *)pvVal;
            break;
        case 2:
            pEvtMmio->u64Val = *(uint16_t *)pvVal;
            break;
        case 4:
            pEvtMmio->u64Val = *(uint32_t *)pvVal;
            break;
        case 8:
            pEvtMmio->u64Val = *(uint64_t *)pvVal;
            break;
        default:
            AssertMsgFailed(("The value size %zu is not supported!\n", cbVal));
    }
}


/**
 * Copies the given I/O port value into the event descriptor based on the given size.
 *
 * @param   pEvtIoPort              Pointer to the I/O port read/write event descriptor to fill.
 * @param   pvVal                   The value to copy.
 * @param   cbVal                   Size of the value in bytes.
 */
static void dbgfTracerEvtIoPortCopyVal(PDBGFTRACEREVTIOPORT pEvtIoPort, const void *pvVal, size_t cbVal)
{
    switch (cbVal)
    {
        case 1:
            pEvtIoPort->u32Val = *(uint8_t *)pvVal;
            break;
        case 2:
            pEvtIoPort->u32Val = *(uint16_t *)pvVal;
            break;
        case 4:
            pEvtIoPort->u32Val = *(uint32_t *)pvVal;
            break;
        default:
            AssertMsgFailed(("The value size %zu is not supported!\n", cbVal));
    }
}


/**
 * Handles a guest memory transfer event.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   pThisCC                 The event tracer instance current context data.
 * @param   enmTraceEvt             The trace event type posted.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   GCPhys                  The guest physical address the transfer starts at.
 * @param   pvBuf                   The data being transfered.
 * @param   cbXfer                  Number of bytes being transfered.
 */
static int dbgfTracerEvtGCPhys(PVMCC pVM, PDBGFTRACERINSCC pThisCC, DBGFTRACEREVT enmTraceEvt, DBGFTRACEREVTSRC hEvtSrc,
                               RTGCPHYS GCPhys, const void *pvBuf, size_t cbXfer)
{
    /* Fast path for really small transfers where everything fits into the descriptor. */
    DBGFTRACEREVTGCPHYS EvtGCPhys;
    EvtGCPhys.GCPhys = GCPhys;
    EvtGCPhys.cbXfer = cbXfer;
    if (cbXfer <= sizeof(EvtGCPhys.abData))
    {
        memcpy(&EvtGCPhys.abData[0], pvBuf, cbXfer);
        return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, enmTraceEvt, &EvtGCPhys, NULL /*pidEvt*/);
    }

    /*
     * Slow path where we have to split the data into multiple entries.
     * Each one is linked to the previous one by the previous event ID.
     */
    const uint8_t *pbBuf = (const uint8_t *)pvBuf;
    size_t cbLeft = cbXfer;
    uint64_t idEvtPrev = 0;
    memcpy(&EvtGCPhys.abData[0], pbBuf, sizeof(EvtGCPhys.abData));
    pbBuf  += sizeof(EvtGCPhys.abData);
    cbLeft -= sizeof(EvtGCPhys.abData);

    int rc = dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, enmTraceEvt, &EvtGCPhys, &idEvtPrev);
    while (   RT_SUCCESS(rc)
           && cbLeft)
    {
        size_t cbThisXfer = RT_MIN(cbLeft, DBGF_TRACER_EVT_PAYLOAD_SZ);
        rc = dbgfTracerEvtPostEx(pVM, pThisCC, hEvtSrc, enmTraceEvt, idEvtPrev,
                                 pbBuf, cbThisXfer, &idEvtPrev);

        pbBuf  += cbThisXfer;
        cbLeft -= cbThisXfer;
    }

    return rc;
}


/**
 * Handles a I/O port string transfer event.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   pThisCC                 The event tracer instance current context data.
 * @param   enmTraceEvt             The trace event type posted.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hIoPorts                The I/O port region handle for the transfer.
 * @param   offPort                 The offset into the region where the transfer happened.
 * @param   pv                      The data being transfered.
 * @param   cb                      Number of bytes of valid data in the buffer.
 * @param   cbItem                  Item size in bytes.
 * @param   cTransfersReq           Number of transfers requested.
 * @param   cTransfersRet           Number of transfers done.
 */
static int dbgfTracerEvtIoPortStr(PVMCC pVM, PDBGFTRACERINSCC pThisCC, DBGFTRACEREVT enmTraceEvt, DBGFTRACEREVTSRC hEvtSrc,
                                  uint64_t hIoPorts, RTIOPORT offPort, const void *pv, size_t cb, size_t cbItem, uint32_t cTransfersReq,
                                  uint32_t cTransfersRet)
{
    /* Fast path for really small transfers where everything fits into the descriptor. */
    DBGFTRACEREVTIOPORTSTR EvtIoPortStr;
    EvtIoPortStr.hIoPorts      = hIoPorts;
    EvtIoPortStr.cbItem        = (uint32_t)cbItem;
    EvtIoPortStr.cTransfersReq = cTransfersReq;
    EvtIoPortStr.cTransfersRet = cTransfersRet;
    EvtIoPortStr.offPort       = offPort;
    if (cb <= sizeof(EvtIoPortStr.abData))
    {
        memcpy(&EvtIoPortStr.abData[0], pv, cb);
        return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, enmTraceEvt, &EvtIoPortStr, NULL /*pidEvt*/);
    }

    /*
     * Slow path where we have to split the data into multiple entries.
     * Each one is linked to the previous one by the previous event ID.
     */
    const uint8_t *pbBuf = (const uint8_t *)pv;
    size_t cbLeft = cb;
    uint64_t idEvtPrev = 0;
    memcpy(&EvtIoPortStr.abData[0], pbBuf, sizeof(EvtIoPortStr.abData));
    pbBuf  += sizeof(EvtIoPortStr.abData);
    cbLeft -= sizeof(EvtIoPortStr.abData);

    int rc = dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, enmTraceEvt, &EvtIoPortStr, &idEvtPrev);
    while (   RT_SUCCESS(rc)
           && cbLeft)
    {
        size_t cbThisXfer = RT_MIN(cbLeft, DBGF_TRACER_EVT_PAYLOAD_SZ);
        rc = dbgfTracerEvtPostEx(pVM, pThisCC, hEvtSrc, enmTraceEvt, idEvtPrev,
                                 pbBuf, cbThisXfer, &idEvtPrev);

        pbBuf  += cbThisXfer;
        cbLeft -= cbThisXfer;
    }

    return rc;
}


/**
 * Registers an MMIO region mapping event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hRegion                 The MMIO region handle being mapped.
 * @param   GCPhysMmio              The guest physical address where the region is mapped.
 */
VMM_INT_DECL(int) DBGFTracerEvtMmioMap(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hRegion, RTGCPHYS GCPhysMmio)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTMMIOMAP EvtMmioMap;
    EvtMmioMap.hMmioRegion     = hRegion;
    EvtMmioMap.GCPhysMmioBase  = GCPhysMmio;
    EvtMmioMap.au64Pad0[0]     = 0;
    EvtMmioMap.au64Pad0[1]     = 0;

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_MMIO_MAP, &EvtMmioMap, NULL /*pidEvt*/);
}


/**
 * Registers an MMIO region unmap event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hRegion                 The MMIO region handle being unmapped.
 */
VMM_INT_DECL(int) DBGFTracerEvtMmioUnmap(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hRegion)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTMMIOUNMAP EvtMmioUnmap;
    EvtMmioUnmap.hMmioRegion = hRegion;
    EvtMmioUnmap.au64Pad0[0] = 0;
    EvtMmioUnmap.au64Pad0[1] = 0;
    EvtMmioUnmap.au64Pad0[2] = 0;

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_MMIO_UNMAP, &EvtMmioUnmap, NULL /*pidEvt*/);
}


/**
 * Registers an MMIO region read event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hRegion                 The MMIO region handle being read.
 * @param   offMmio                 The MMIO offset into the region where the read happened.
 * @param   pvVal                   The value being read.
 * @param   cbVal                   Value size in bytes.
 */
VMM_INT_DECL(int) DBGFTracerEvtMmioRead(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hRegion, RTGCPHYS offMmio, const void *pvVal, size_t cbVal)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTMMIO EvtMmio;
    EvtMmio.hMmioRegion = hRegion;
    EvtMmio.offMmio     = offMmio;
    EvtMmio.cbXfer      = cbVal;
    dbgfTracerEvtMmioCopyVal(&EvtMmio, pvVal, cbVal);

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_MMIO_READ, &EvtMmio, NULL /*pidEvt*/);
}


/**
 * Registers an MMIO region write event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hRegion                 The MMIO region handle being written to.
 * @param   offMmio                 The MMIO offset into the region where the write happened.
 * @param   pvVal                   The value being written.
 * @param   cbVal                   Value size in bytes.
 */
VMM_INT_DECL(int) DBGFTracerEvtMmioWrite(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hRegion, RTGCPHYS offMmio, const void *pvVal, size_t cbVal)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTMMIO EvtMmio;
    EvtMmio.hMmioRegion = hRegion;
    EvtMmio.offMmio     = offMmio;
    EvtMmio.cbXfer      = cbVal;
    dbgfTracerEvtMmioCopyVal(&EvtMmio, pvVal, cbVal);

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_MMIO_WRITE, &EvtMmio, NULL /*pidEvt*/);
}


/**
 * Registers an MMIO region fill event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hRegion                 The MMIO region handle being filled.
 * @param   offMmio                 The MMIO offset into the region where the fill starts.
 * @param   u32Item                 The value being used for filling.
 * @param   cbItem                  Item size in bytes.
 * @param   cItems                  Number of items being written.
 */
VMM_INT_DECL(int) DBGFTracerEvtMmioFill(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hRegion, RTGCPHYS offMmio,
                                        uint32_t u32Item, uint32_t cbItem, uint32_t cItems)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTMMIOFILL EvtMmioFill;
    EvtMmioFill.hMmioRegion = hRegion;
    EvtMmioFill.offMmio     = offMmio;
    EvtMmioFill.cbItem      = cbItem;
    EvtMmioFill.cItems      = cItems;
    EvtMmioFill.u32Item     = u32Item;
    EvtMmioFill.u32Pad0     = 0;

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_MMIO_FILL, &EvtMmioFill, NULL /*pidEvt*/);
}


/**
 * Registers an I/O region mapping event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hIoPorts                The I/O port region handle being mapped.
 * @param   IoPortBase              The I/O port base where the region is mapped.
 */
VMM_INT_DECL(int) DBGFTracerEvtIoPortMap(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hIoPorts, RTIOPORT IoPortBase)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTIOPORTMAP EvtIoPortMap;
    RT_ZERO(EvtIoPortMap);
    EvtIoPortMap.hIoPorts    = hIoPorts;
    EvtIoPortMap.IoPortBase  = IoPortBase;

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_IOPORT_MAP, &EvtIoPortMap, NULL /*pidEvt*/);
}


/**
 * Registers an I/O region unmap event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hIoPorts                The I/O port region handle being unmapped.
 */
VMM_INT_DECL(int) DBGFTracerEvtIoPortUnmap(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hIoPorts)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTIOPORTUNMAP EvtIoPortUnmap;
    EvtIoPortUnmap.hIoPorts    = hIoPorts;
    EvtIoPortUnmap.au64Pad0[0] = 0;
    EvtIoPortUnmap.au64Pad0[1] = 0;
    EvtIoPortUnmap.au64Pad0[2] = 0;

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_IOPORT_UNMAP, &EvtIoPortUnmap, NULL /*pidEvt*/);
}


/**
 * Registers an I/O region read event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hIoPorts                The I/O port region handle being read from.
 * @param   offPort                 The offset into the region where the read happened.
 * @param   pvVal                   The value being read.
 * @param   cbVal                   Value size in bytes.
 */
VMM_INT_DECL(int) DBGFTracerEvtIoPortRead(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hIoPorts, RTIOPORT offPort, const void *pvVal, size_t cbVal)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTIOPORT EvtIoPort;
    RT_ZERO(EvtIoPort);
    EvtIoPort.hIoPorts = hIoPorts;
    EvtIoPort.offPort  = offPort;
    EvtIoPort.cbXfer   = cbVal;
    dbgfTracerEvtIoPortCopyVal(&EvtIoPort, pvVal, cbVal);

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_IOPORT_READ, &EvtIoPort, NULL /*pidEvt*/);
}


/**
 * Registers an I/O region string read event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hIoPorts                The I/O port region handle being read from.
 * @param   offPort                 The offset into the region where the read happened.
 * @param   pv                      The data being read.
 * @param   cb                      Item size in bytes.
 * @param   cTransfersReq           Number of transfers requested.
 * @param   cTransfersRet           Number of transfers done.
 */
VMM_INT_DECL(int) DBGFTracerEvtIoPortReadStr(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hIoPorts, RTIOPORT offPort, const void *pv, size_t cb,
                                             uint32_t cTransfersReq, uint32_t cTransfersRet)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    return dbgfTracerEvtIoPortStr(pVM, pThisCC, DBGFTRACEREVT_IOPORT_READ_STR, hEvtSrc, hIoPorts, offPort, pv, cTransfersRet * cb,
                                  cb, cTransfersReq, cTransfersRet);
}


/**
 * Registers an I/O region write event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hIoPorts                The I/O port region handle being written to.
 * @param   offPort                 The offset into the region where the write happened.
 * @param   pvVal                   The value being written.
 * @param   cbVal                   Value size in bytes.
 */
VMM_INT_DECL(int) DBGFTracerEvtIoPortWrite(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hIoPorts, RTIOPORT offPort, const void *pvVal, size_t cbVal)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTIOPORT EvtIoPort;
    RT_ZERO(EvtIoPort);
    EvtIoPort.hIoPorts = hIoPorts;
    EvtIoPort.offPort  = offPort;
    EvtIoPort.cbXfer   = cbVal;
    dbgfTracerEvtIoPortCopyVal(&EvtIoPort, pvVal, cbVal);

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_IOPORT_WRITE, &EvtIoPort, NULL /*pidEvt*/);
}


/**
 * Registers an I/O region string write event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   hIoPorts                The I/O port region handle being written to.
 * @param   offPort                 The offset into the region where the write happened.
 * @param   pv                      The data being written.
 * @param   cb                      Item size in bytes.
 * @param   cTransfersReq           Number of transfers requested.
 * @param   cTransfersRet           Number of transfers done.
 */
VMM_INT_DECL(int) DBGFTracerEvtIoPortWriteStr(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, uint64_t hIoPorts, RTIOPORT offPort, const void *pv, size_t cb,
                                              uint32_t cTransfersReq, uint32_t cTransfersRet)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    return dbgfTracerEvtIoPortStr(pVM, pThisCC, DBGFTRACEREVT_IOPORT_WRITE_STR, hEvtSrc, hIoPorts, offPort, pv, cTransfersReq * cb,
                                  cb, cTransfersReq, cTransfersRet);
}


/**
 * Registers an IRQ change event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   iIrq                    The IRQ line changed.
 * @param   fIrqLvl                 The new IRQ level mask.
 */
VMM_INT_DECL(int) DBGFTracerEvtIrq(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, int32_t iIrq, int32_t fIrqLvl)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTIRQ EvtIrq;
    RT_ZERO(EvtIrq);
    EvtIrq.iIrq    = iIrq;
    EvtIrq.fIrqLvl = fIrqLvl;

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_IRQ, &EvtIrq, NULL /*pidEvt*/);
}


/**
 * Registers an I/O APIC MSI event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   GCPhys                  Guest physical address where the value is written to.
 * @param   u32Val                  The MSI event value being written.
 */
VMM_INT_DECL(int) DBGFTracerEvtIoApicMsi(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, RTGCPHYS GCPhys, uint32_t u32Val)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    DBGFTRACEREVTIOAPICMSI EvtMsi;
    RT_ZERO(EvtMsi);
    EvtMsi.GCPhys = GCPhys;
    EvtMsi.u32Val = u32Val;

    return dbgfTracerEvtPostSingle(pVM, pThisCC, hEvtSrc, DBGFTRACEREVT_IOAPIC_MSI, &EvtMsi, NULL /*pidEvt*/);
}


/**
 * Registers an guest physical memory read event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   GCPhys                  Guest physical address the read started at.
 * @param   pvBuf                   The read data.
 * @param   cbRead                  Number of bytes read.
 */
VMM_INT_DECL(int) DBGFTracerEvtGCPhysRead(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, RTGCPHYS GCPhys, const void *pvBuf, size_t cbRead)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    return dbgfTracerEvtGCPhys(pVM, pThisCC, DBGFTRACEREVT_GCPHYS_READ, hEvtSrc, GCPhys, pvBuf, cbRead);
}


/**
 * Registers an guest physical memory write event for the given event source.
 *
 * @returns VBox status code.
 * @param   pVM                     The current context VM instance data.
 * @param   hEvtSrc                 The event source for the posted event.
 * @param   GCPhys                  Guest physical address the write started at.
 * @param   pvBuf                   The written data.
 * @param   cbWrite                 Number of bytes written.
 */
VMM_INT_DECL(int) DBGFTracerEvtGCPhysWrite(PVMCC pVM, DBGFTRACEREVTSRC hEvtSrc, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    PDBGFTRACERINSCC pThisCC = dbgfTracerGetInstance(pVM);
    AssertReturn(pThisCC, VERR_DBGF_TRACER_IPE_1);

    return dbgfTracerEvtGCPhys(pVM, pThisCC, DBGFTRACEREVT_GCPHYS_WRITE, hEvtSrc, GCPhys, pvBuf, cbWrite);
}

