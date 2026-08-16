/* $Id: SvgaFifo.cpp 115043 2026-08-16 20:32:42Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - VMSVGA FIFO.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#define GALOG_GROUP GALOG_GROUP_SVGA_FIFO

#include "SvgaFifo.h"
#include "SvgaHw.h"

#include <iprt/alloc.h>
#include <iprt/errcore.h>
#include <iprt/memobj.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/x86.h>

NTSTATUS SvgaFifoInit(PVBOXWDDM_EXT_VMSVGA pSvga)
{
// ASMBreakpoint();
    PVMSVGAFIFO pFifo = &pSvga->fifo;

    GALOG(("FIFO: resolution %dx%dx%d\n",
           SVGARegRead(pSvga, SVGA_REG_WIDTH),
           SVGARegRead(pSvga, SVGA_REG_HEIGHT),
           SVGARegRead(pSvga, SVGA_REG_BITS_PER_PIXEL)));

    memset(pFifo, 0, sizeof(*pFifo));

    ExInitializeFastMutex(&pFifo->FifoMutex);

    /** @todo Why these are read here? */
    uint32_t u32EnableState = SVGARegRead(pSvga, SVGA_REG_ENABLE);
    uint32_t u32ConfigDone = SVGARegRead(pSvga, SVGA_REG_CONFIG_DONE);
    uint32_t u32TracesState = SVGARegRead(pSvga, SVGA_REG_TRACES);
    GALOG(("enable %d, config done %d, traces %d\n",
           u32EnableState, u32ConfigDone, u32TracesState));

    SVGARegWrite(pSvga, SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE | SVGA_REG_ENABLE_HIDE);
    SVGARegWrite(pSvga, SVGA_REG_TRACES, 0);

    uint32_t offMin = 4;
    if (pSvga->u32Caps & SVGA_CAP_EXTENDED_FIFO)
    {
        offMin = SVGARegRead(pSvga, SVGA_REG_MEM_REGS);
    }
    /* Minimum offset in bytes. */
    offMin *= sizeof(uint32_t);
    if (offMin < PAGE_SIZE)
    {
        offMin = PAGE_SIZE;
    }

    SVGAFifoWrite(pSvga, SVGA_FIFO_MIN, offMin);
    SVGAFifoWrite(pSvga, SVGA_FIFO_MAX, pSvga->u32FifoSize);
    ASMCompilerBarrier();

    SVGAFifoWrite(pSvga, SVGA_FIFO_NEXT_CMD, offMin);
    SVGAFifoWrite(pSvga, SVGA_FIFO_STOP, offMin);
    SVGAFifoWrite(pSvga, SVGA_FIFO_BUSY, 0);
    ASMCompilerBarrier();

    SVGARegWrite(pSvga, SVGA_REG_CONFIG_DONE, 1);

    pFifo->u32FifoCaps = SVGAFifoRead(pSvga, SVGA_FIFO_CAPABILITIES);

    GALOG(("FIFO: min 0x%08X, max 0x%08X, caps  0x%08X\n",
           SVGAFifoRead(pSvga, SVGA_FIFO_MIN),
           SVGAFifoRead(pSvga, SVGA_FIFO_MAX),
           pFifo->u32FifoCaps));

    SVGAFifoWrite(pSvga, SVGA_FIFO_FENCE, 0);

    return STATUS_SUCCESS;
}

void *SvgaFifoReserve(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbReserve)
{
    Assert(!pSvga->pCBState);
    Assert((cbReserve & 0x3) == 0);

    PVMSVGAFIFO pFifo = &pSvga->fifo;
    void *pvRet = NULL;

    ExAcquireFastMutex(&pFifo->FifoMutex);
    /** @todo The code in SvgaFifoReserve/SvgaFifoCommit runs at IRQL = APC_LEVEL. */

    const uint32_t offMin = SVGAFifoRead(pSvga, SVGA_FIFO_MIN);
    const uint32_t offMax = SVGAFifoRead(pSvga, SVGA_FIFO_MAX);
    const uint32_t offNextCmd = SVGAFifoRead(pSvga, SVGA_FIFO_NEXT_CMD);
    GALOG(("cb %d offMin 0x%08X, offMax 0x%08X, offNextCmd 0x%08X\n",
           cbReserve, offMin, offMax, offNextCmd));

    if (cbReserve < offMax - offMin)
    {
        Assert(pFifo->cbReserved == 0);
        Assert(pFifo->pvBuffer == NULL);

        pFifo->cbReserved = cbReserve;

        for (;;)
        {
            bool fNeedBuffer = false;

            const uint32_t offStop = SVGAFifoRead(pSvga, SVGA_FIFO_STOP);
            GALOG(("    offStop 0x%08X\n", offStop));

            if (offNextCmd >= offStop)
            {
                if (   offNextCmd + cbReserve < offMax
                    || (offNextCmd + cbReserve == offMax && offStop > offMin))
                {
                    /* Enough space for command in FIFO. */
                }
                else if ((offMax - offNextCmd) + (offStop - offMin) <= cbReserve)
                {
                    /* FIFO full. */
                    /** @todo Implement waiting for FIFO space. */
                    RTThreadSleep(10);
                    continue;
                }
                else
                {
                    fNeedBuffer = true;
                }
            }
            else
            {
                if (offNextCmd + cbReserve < offStop)
                {
                    /* Enough space in FIFO. */
                }
                else
                {
                    /* FIFO full. */
                    /** @todo Implement waiting for FIFO space. */
                    RTThreadSleep(10);
                    continue;
                }
            }

            if (!fNeedBuffer)
            {
                if (pFifo->u32FifoCaps & SVGA_FIFO_CAP_RESERVE)
                {
                    SVGAFifoWrite(pSvga, SVGA_FIFO_RESERVED, cbReserve);
                }

                pvRet = (void *)SVGAFifoPtrFromOffset(pSvga, offNextCmd); /** @todo Return ptr to volatile data? */
                GALOG(("    in place %p\n", pvRet));
                break;
            }

            if (fNeedBuffer)
            {
                pvRet = RTMemAlloc(cbReserve);
                pFifo->pvBuffer = pvRet;
                GALOG(("     %p\n", pvRet));
                break;
            }
        }

    }

    if (pvRet)
    {
        return pvRet;
    }

    pFifo->cbReserved = 0;
    ExReleaseFastMutex(&pFifo->FifoMutex);
    return NULL;
}

static void svgaFifoPingHost(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32Reason)
{
    if (ASMAtomicCmpXchgU32(&pSvga->pu32FIFO[SVGA_FIFO_BUSY], 1, 0))
    {
        SVGARegWrite(pSvga, SVGA_REG_SYNC, u32Reason);
    }
}

void SvgaFifoCommit(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbActual)
{
    Assert((cbActual & 0x3) == 0);

    PVMSVGAFIFO pFifo = &pSvga->fifo;

    const uint32_t offMin = SVGAFifoRead(pSvga, SVGA_FIFO_MIN);
    const uint32_t offMax = SVGAFifoRead(pSvga, SVGA_FIFO_MAX);
    uint32_t offNextCmd = SVGAFifoRead(pSvga, SVGA_FIFO_NEXT_CMD);
    GALOG(("cb %d, offMin 0x%08X, offMax 0x%08X, offNextCmd 0x%08X\n",
           cbActual, offMin, offMax, offNextCmd));

    pFifo->cbReserved = 0;

    if (pFifo->pvBuffer)
    {
        if (pFifo->u32FifoCaps & SVGA_FIFO_CAP_RESERVE)
        {
            SVGAFifoWrite(pSvga, SVGA_FIFO_RESERVED, cbActual);
        }

        const uint32_t cbToWrite = RT_MIN(offMax - offNextCmd, cbActual);
        memcpy((void *)SVGAFifoPtrFromOffset(pSvga, offNextCmd), pFifo->pvBuffer, cbToWrite);
        if (cbActual > cbToWrite)
        {
            memcpy((void *)SVGAFifoPtrFromOffset(pSvga, offMin),
                   (uint8_t *)pFifo->pvBuffer + cbToWrite, cbActual - cbToWrite);
        }
        ASMCompilerBarrier();
    }

    offNextCmd += cbActual;
    if (offNextCmd >= offMax)
    {
        offNextCmd -= offMax - offMin;
    }
    SVGAFifoWrite(pSvga, SVGA_FIFO_NEXT_CMD, offNextCmd);

    RTMemFree(pFifo->pvBuffer);
    pFifo->pvBuffer = NULL;

    if (pFifo->u32FifoCaps & SVGA_FIFO_CAP_RESERVE)
    {
        SVGAFifoWrite(pSvga, SVGA_FIFO_RESERVED, 0);
    }

    svgaFifoPingHost(pSvga, SVGA_SYNC_GENERIC);

    ExReleaseFastMutex(&pFifo->FifoMutex);
}


/*
 * Command buffers are supported by the host if SVGA_CAP_COMMAND_BUFFERS is set.
 *
 * A command buffer consists of command data and a buffer header (SVGACBHeader), which contains
 * buffer physical address. The memory is allocated from non paged pool.
 *
 * The guest submits a command buffer by writing 64 bit physical address in
 * SVGA_REG_COMMAND_HIGH and SVGA_REG_COMMAND_LOW registers.
 *
 * The physical address of the header must be 64 bytes aligned and the lower 6 bits
 * contain command buffer context id. Each command buffer context is a queue of submitted
 * buffers. Id 0x3f is SVGA_CB_CONTEXT_DEVICE, which is used to send synchronous commands
 * to the host, which are used to setup and control other buffer contexts (queues).
 *
 * The miniport driver submits buffers in one of 3 cases:
 * 1) SVGA_CB_CONTEXT_DEVICE commands.
 *      Small amount of memory.
 *      VMSVGACBHEADERS::contextDeviceCBHeader and VMSVGACBHEADERS::au8ContextDeviceData is used.
 *      Synchronous.
 * 2) Submitting commands from the miniport (VMSVGACB_MINIPORT).
 *      Memory for the command data must be allocated.
 *      VMSVGACB is allocated to track buffer submission.
 *      The host processes the buffer asynchronously, updated the buffer status and generates an interrupt.
 * 3) Submitting command buffers generated by the user mode driver.
 *      Memory for the commands is provided by WDDM (DXGKARG_SUBMITCOMMAND::DmaBufferPhysicalAddress).
 *      VMSVGACB is allocated to track buffer submission.
 *      Asynchronous processing.
 *
 * A ring buffer (VMSVGACBHEADERS::aContext0CBHeaders) is used to avoid allocation of command headers.
 *
 * Total size of command buffers must not exceed SVGA_CB_MAX_SIZE.
 * One buffer can be up to SVGA_CB_MAX_COMMAND_SIZE.
 * Up to SVGA_CB_MAX_QUEUED_PER_CONTEXT buffers cane be queued for one command buffer context simultaneously.
 *
 * The miniport allocates page aligned size memory buffers for VMSVGACB_MINIPORT.
 * A lookaside list of VMSVGACB structures (VMSVGACBSTATE::ListCB) is used to avoid re-allocation.
 *
 * Command buffer can be tied to a DX context, which the driver creates on the host. I.e. all commands
 * are submitted for this DX context. In this case SVGA_CB_FLAG_DX_CONTEXT bit is set in the header 'flags'
 * and 'dxContext' field is set to the DX context id.
 */

static NTSTATUS svgaCBFreePage(PVMSVGACBPAGE pPage)
{
    if (pPage->hMemObjMapping != NIL_RTR0MEMOBJ)
    {
        int rc = RTR0MemObjFree(pPage->hMemObjMapping, /* fFreeMappings */ true);
        Assert(RT_SUCCESS(rc)); RT_NOREF(rc);
    }

    if (pPage->hMemObjPages != NIL_RTR0MEMOBJ)
    {
        int rc = RTR0MemObjFree(pPage->hMemObjPages, /* fFreeMappings */ true);
        Assert(RT_SUCCESS(rc)); RT_NOREF(rc);
    }
    RT_ZERO(*pPage);
    return STATUS_SUCCESS;
}


static NTSTATUS svgaCBAllocPage(PVMSVGACBPAGE pPage, uint32_t cb)
{
    int rc = RTR0MemObjAllocPhysTag(&pPage->hMemObjPages, cb, NIL_RTHCPHYS, "VMSVGACB");
    AssertReturn(RT_SUCCESS(rc), STATUS_INSUFFICIENT_RESOURCES);

    rc = RTR0MemObjMapKernelTag(&pPage->hMemObjMapping, pPage->hMemObjPages, (void *)-1,
                                PAGE_SIZE, RTMEM_PROT_READ | RTMEM_PROT_WRITE, "VMSVGACB");
    AssertReturnStmt(RT_SUCCESS(rc), svgaCBFreePage(pPage), STATUS_INSUFFICIENT_RESOURCES);

    pPage->pvR0     = RTR0MemObjAddress(pPage->hMemObjMapping);
    pPage->PhysAddr = RTR0MemObjGetPagePhysAddr(pPage->hMemObjPages, /* iPage */ 0);
    return STATUS_SUCCESS;
}


static void svgaCBReset(PVMSVGACB pCB, uint32_t idDXContext)
{
    /* Re-initialize a command buffer. It will be re-used.
     *
     * Keep the following fields:
     *   pCB->enmType
     *   pCB->cbBuffer, if pCB->enmType != VMSVGACB_UMD
     *   pCB->commands.page, if pCB->enmType != VMSVGACB_UMD
     */
    RT_ZERO(pCB->nodeQueue);
    pCB->idDXContext         = idDXContext;
    pCB->cbCommand           = 0;
    pCB->cbReservedCmdHeader = 0;
    pCB->cbReservedCmd       = 0;
    pCB->u32ReservedCmd      = 0;
    pCB->pCBHeader           = NULL;
    pCB->status              = SVGA_CB_STATUS_NONE;
    if (pCB->enmType == VMSVGACB_UMD)
    {
        pCB->cbBuffer = 0;
        pCB->commands.DmaBufferPhysicalAddress.QuadPart = 0;
    }

    RTListInit(&pCB->listCompletion);
#ifdef DEBUG
    pCB->fSubmitted = false;
#endif
}

static void svgaCBFree(PVMSVGACB pCB)
{
    GALOG(("CB: %p\n", pCB));
    if (pCB->enmType != VMSVGACB_UMD)
        svgaCBFreePage(&pCB->commands.page);

    GaMemFree(pCB);
}

/* Command Buffers (VMSVGACB) are stored in lookaside lists in order to reduce re-allocation.
 * Lookaside list adjusts its size based on exponential moving average (EMA) of miss rate.
 * See comment in svgaCBLookasideListMissRateUpdate for explanation of EMA parameters.
 */
#define CB_EMA_LOG2_SCALE  8 /* 8 bit precision is enough in this case. */
#define CB_EMA_LOG2_RALPHA 5 /* 1/(2^5) = 1/32 = 0.03125, a slow rate of adaptation. */
#define CB_EMA_RATE_LOW    ((1 << CB_EMA_LOG2_SCALE) / 16) /* 1/16 = 0.0625 */
#define CB_EMA_RATE_HIGH   ((1 << CB_EMA_LOG2_SCALE) / 8)  /* 1/8  = 0.125 */

static void svgaCBLookasideListInit(PVMSVGACBLOOKASIDELIST pList, uint32_t cMinEntries)
{
    RTListInit(&pList->ListCB);
    pList->cEntries    = 0;
    pList->cMinEntries = cMinEntries;
    pList->cMaxEntries = cMinEntries;
    pList->u32MissRate = 1 << CB_EMA_LOG2_SCALE; /* High miss rate, favor allocations initially. */
}

static void svgaCBLookasideListFree(PVMSVGACBLOOKASIDELIST pList)
{
    PVMSVGACB pIter, pNext;
    RTListForEachSafe(&pList->ListCB, pIter, pNext, VMSVGACB, nodeQueue)
    {
        RTListNodeRemove(&pIter->nodeQueue);
        svgaCBFree(pIter);
    }
}

static void svgaCBLookasideListMissRateUpdate(PVMSVGACBLOOKASIDELIST pList, uint32_t sample)
{
    /* Fixed point integer calculation of exponential moving average (EMA):
     *
     *   ema = alpha * sample + (1 - alpha) * ema
     *       = alpha * sample + ema - alpha * ema
     *
     * 'sample' is either 0 (hit) or 1 (miss). 'alpha' is between 0 and 1.
     *
     * Let 'alpha' be the reciprocal value of a power of 2: 1 / (2^log2ralpha)
     * Then '* alpha' is equal to '>> log2ralpha'.
     *
     *   ema = sample >> log2ralpha + ema - ema >> log2ralpha
     *
     * In order to keep precision, the addition and multiplication must be done before '>>'.
     *
     *   ema = (sample + ema << log2ralpha - ema) >> log2ralpha
     *
     * 'sample' must be (pre)scaled in order to do integer calculation. Use '1 << scale' as 1.
     * The scaled 'sample' will produce 'ema' between 0 and (1 << scale).
     */
    pList->u32MissRate = (sample + ((pList->u32MissRate << CB_EMA_LOG2_RALPHA) - pList->u32MissRate)) >> CB_EMA_LOG2_RALPHA;
}

static void svgaCBRetire(PVMSVGACBSTATE pCBState, PVMSVGACB pCB)
{
    GALOG(("CB: %p\n", pCB));
    Assert(pCB->nodeQueue.pNext == NULL && pCB->nodeQueue.pPrev == NULL);

    PVMSVGACBLOOKASIDELIST pList;
    if (pCB->enmType == VMSVGACB_UMD)
        pList = &pCBState->ListCBUMD;
    else
        pList = &pCBState->ListCB;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);

    /* If the miss rate is too low, decrease the allowed number of entries. */
    Assert(pList->cMaxEntries > 0);
    if (pList->u32MissRate < CB_EMA_RATE_LOW && pList->cMaxEntries > pList->cMinEntries)
        --pList->cMaxEntries;

    if (pList->cEntries <= pList->cMaxEntries)
    {
        /* Keep the entry. */
        ++pList->cEntries;
        RTListAppend(&pList->ListCB, &pCB->nodeQueue);
        pCB = NULL;
    }

    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);

    if (pCB)
        svgaCBFree(pCB);
}

/** Allocate one command buffer.
 * @param pCBState    Command buffers manager.
 * @param enmType     Kind of the buffer.
 * @param idDXContext DX context of the commands in the buffer.
 * @param cbRequired  How many bytes are required for MINIPORT buffers.
 * @param ppCB        Where to store the allocated buffer pointer.
 */
static NTSTATUS svgaCBAlloc(PVMSVGACBSTATE pCBState, VMSVGACBTYPE enmType, uint32_t idDXContext, uint32_t cbRequired, PVMSVGACB *ppCB)
{
    PVMSVGACB pCB;
    PVMSVGACBLOOKASIDELIST pList;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);

    if (enmType == VMSVGACB_UMD)
    {
        pList = &pCBState->ListCBUMD;
        pCB = RTListRemoveFirst(&pList->ListCB, VMSVGACB, nodeQueue);
    }
    else
    {
        pList = &pCBState->ListCB;
        pCB = NULL;

        PVMSVGACB pIter, pNext;
        RTListForEachSafe(&pList->ListCB, pIter, pNext, VMSVGACB, nodeQueue)
        {
            if (pIter->cbBuffer >= cbRequired)
            {
                RTListNodeRemove(&pIter->nodeQueue);
                pCB = pIter;
                break;
            }
        }
    }

    if (pCB)
    {
        Assert(pList->cEntries > 0);
        --pList->cEntries;
        svgaCBLookasideListMissRateUpdate(pList, 0);

        KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);

        GALOG(("CB: %p reuse\n", pCB));
        svgaCBReset(pCB, idDXContext);
        *ppCB = pCB;
        return STATUS_SUCCESS;
    }

    svgaCBLookasideListMissRateUpdate(pList, 1 << CB_EMA_LOG2_SCALE);

    /* If the miss rate is too high, increase the allowed number of entries */
    if (pList->u32MissRate > CB_EMA_RATE_HIGH)
        ++pList->cMaxEntries;

    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);

    pCB = (PVMSVGACB)GaMemAllocZero(sizeof(VMSVGACB));
    AssertReturn(pCB, STATUS_INSUFFICIENT_RESOURCES);
    GALOG(("CB: %p\n", pCB));

    pCB->enmType = enmType;
    if (enmType != VMSVGACB_UMD)
    {
        pCB->cbBuffer = RT_ALIGN_32(cbRequired, PAGE_SIZE);
        NTSTATUS Status = svgaCBAllocPage(&pCB->commands.page, pCB->cbBuffer);
        AssertReturnStmt(NT_SUCCESS(Status),
                         GaMemFree(pCB),
                         STATUS_INSUFFICIENT_RESOURCES);
    }
    svgaCBReset(pCB, idDXContext);

    *ppCB = pCB;
    return STATUS_SUCCESS;
}

DECLINLINE(void) svgaCBSubmitHeaderLocked(PVBOXWDDM_EXT_VMSVGA pSvga, PHYSICAL_ADDRESS CBHeaderPhysAddr, SVGACBContext CBContext)
{
    SVGARegWrite(pSvga, SVGA_REG_COMMAND_HIGH, CBHeaderPhysAddr.HighPart);
    SVGARegWrite(pSvga, SVGA_REG_COMMAND_LOW, CBHeaderPhysAddr.LowPart | CBContext);
}

static NTSTATUS svgaCBSubmitLocked(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACB pCB, PVMSVGACBCONTEXT pCBCtx)
{
    GALOG(("CB: %p\n", pCB));

    PVMSVGACBSTATE pCBState = pSvga->pCBState;
    PVMSVGACBHEADERS pCBHeaders = cbStateHeaders(pCBState);

    /* Allocate a header for the buffer. */
    Assert(pCBCtx->cHeaders <= RT_ELEMENTS(pCBHeaders->aContext0CBHeaders));
    if (pCBCtx->cHeaders == 0)
        return STATUS_PENDING;

#ifdef DEBUG
    Assert(!pCB->fSubmitted);
    if (pCB->fSubmitted)
        GALOG(("CB: %p already submitted\n", pCB));
    pCB->fSubmitted = true;
#endif

    SVGACBHeader *pCBHeader = &pCBHeaders->aContext0CBHeaders[pCBCtx->idxNextHeader];
    pCBCtx->idxNextHeader = (pCBCtx->idxNextHeader + 1) % RT_ELEMENTS(pCBHeaders->aContext0CBHeaders);
    --pCBCtx->cHeaders;

    /* Initialize the header. */
    pCBHeader->status      = SVGA_CB_STATUS_NONE;
    pCBHeader->errorOffset = 0;
    if (pCB->enmType != VMSVGACB_UMD)
        pCBHeader->id      = 0;
    else
        pCBHeader->id      = 1; /* An arbitrary not zero value. SVGA_DC_CMD_PREEMPT will preempt such buffers. */
    if (pCB->idDXContext != SVGA3D_INVALID_ID)
        pCBHeader->flags   = SVGA_CB_FLAG_DX_CONTEXT;
    else
        pCBHeader->flags   = SVGA_CB_FLAG_NONE;
    pCBHeader->length      = pCB->cbCommand;
    if (pCB->enmType != VMSVGACB_UMD)
        pCBHeader->ptr.pa  = pCB->commands.page.PhysAddr;
    else
        pCBHeader->ptr.pa  = pCB->commands.DmaBufferPhysicalAddress.QuadPart;
    pCBHeader->offset      = 0;
    pCBHeader->dxContext   = pCB->idDXContext;
    RT_ZERO(pCBHeader->mustBeZero);
    Assert(pCBHeader->ptr.pa != 0);

    /* Remember which header is associated with the buffer. */
    pCB->pCBHeader = pCBHeader;

    uintptr_t const off = (uintptr_t)pCBHeader - (uintptr_t)&pCBHeaders->aContext0CBHeaders[0];
    svgaCBSubmitHeaderLocked(pSvga, cbStateHeadersPA(pCBState, (uint32_t)off), SVGA_CB_CONTEXT_0);

    LogRel3(("WDDM: cb: submit @%u %u\n", (uint32_t)off, pCB->cbCommand));

    return STATUS_SUCCESS;
}

static NTSTATUS svgaCBSubmit(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACB pCB)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;
    PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[SVGA_CB_CONTEXT_0];
    GALOG(("CB: %p\n", pCB));

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);

    NTSTATUS SubmitStatus = svgaCBSubmitLocked(pSvga, pCB, pCBCtx);
    if (SubmitStatus != STATUS_SUCCESS)
    {
        /* Can't submit the buffer. Put it into pending queue. */
        RTListAppend(&pCBCtx->QueuePending, &pCB->nodeQueue);

        KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);
        return STATUS_SUCCESS;
    }

    RTListAppend(&pCBCtx->QueueSubmitted, &pCB->nodeQueue);

    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS SvgaCmdBufDeviceCommand(PVBOXWDDM_EXT_VMSVGA pSvga, void const *pvCmd, uint32_t cbCmd)
{
    /* Synchronous register like access. */
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    /* This is rarely used, not worth optimizing. */
    PVMSVGACBHEADERS pCBHeaders = cbStateHeaders(pCBState);
    AssertReturn(cbCmd <= sizeof(pCBHeaders->au8ContextDeviceData), STATUS_INVALID_PARAMETER);

    memcpy(pCBHeaders->au8ContextDeviceData, pvCmd, cbCmd);

    SVGACBHeader *pCBHeader = &pCBHeaders->contextDeviceCBHeader;
    pCBHeader->status      = SVGA_CB_STATUS_NONE;
    pCBHeader->errorOffset = 0;
    pCBHeader->id          = 0;
    pCBHeader->flags       = SVGA_CB_FLAG_NONE;
    pCBHeader->length      = cbCmd;
    pCBHeader->ptr.pa      = cbStateHeadersPA(pCBState, RT_UOFFSETOF(VMSVGACBHEADERS, au8ContextDeviceData)).QuadPart;
    pCBHeader->offset      = 0;
    pCBHeader->dxContext   = SVGA3D_INVALID_ID;
    RT_ZERO(pCBHeader->mustBeZero);

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);

    svgaCBSubmitHeaderLocked(pSvga, cbStateHeadersPA(pCBState, RT_UOFFSETOF(VMSVGACBHEADERS, contextDeviceCBHeader)),
                             SVGA_CB_CONTEXT_DEVICE);

    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);

    if (pCBHeader->status != SVGA_CB_STATUS_COMPLETED)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}


NTSTATUS SvgaCmdBufSubmitMiniportCommand(PVBOXWDDM_EXT_VMSVGA pSvga, void const *pvCmd, uint32_t cbCmd)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    PVMSVGACB pCB;
    NTSTATUS Status = svgaCBAlloc(pCBState, VMSVGACB_MINIPORT, SVGA3D_INVALID_ID, cbCmd, &pCB);
    AssertReturn(NT_SUCCESS(Status), Status);

    memcpy(pCB->commands.page.pvR0, pvCmd, cbCmd);
    pCB->cbCommand = cbCmd;

    return svgaCBSubmit(pSvga, pCB);
}


/** Reserve space for a command in the current miniport command buffer.
 * The current buffer will be submitted to the host if either the command does not fit
 * or if the command is for another DX context than the commands in the buffer.
 *
 * @param pSvga            The device instance.
 * @param u32CmdId         Command identifier.
 * @param cbReserveHeader  Size of the command header.
 * @param cbReserveCmd     Expected size of the command data.
 * @param idDXContext      DX context of the command.
 * @return Pointer to the command data.
 */
static void *svgaCBReserve(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32CmdId, uint32_t cbReserveHeader, uint32_t cbReserveCmd, uint32_t idDXContext)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;
    NTSTATUS Status;

    /* Required space for the command header and the command. */
    uint32_t const cbRequired = cbReserveHeader + cbReserveCmd;

    /* Current command buffer is locked until SvgaCmdBufCommit is called. */
    ExAcquireFastMutex(&pCBState->CBCurrentMutex);

    PVMSVGACB pCB = pCBState->pCBCurrent;
    if (   pCB
        && (   pCB->cbBuffer - pCB->cbCommand < cbRequired
            || idDXContext != pCB->idDXContext))
    {
        /* If the command does not fit or is for a different context, then submit the current buffer. */
        Status = svgaCBSubmit(pSvga, pCB);
        Assert(NT_SUCCESS(Status));

        /* A new current buffer must be allocated. */
        pCB = NULL;
    }

    if (!pCB)
    {
        /* Allocate a new command buffer. */
        Status = svgaCBAlloc(pCBState, VMSVGACB_MINIPORT, idDXContext, cbRequired, &pCBState->pCBCurrent);
        AssertReturnStmt(NT_SUCCESS(Status), ExReleaseFastMutex(&pCBState->CBCurrentMutex), NULL);
        pCB = pCBState->pCBCurrent;
        AssertReturnStmt(pCB->cbBuffer - pCB->cbCommand >= cbRequired, ExReleaseFastMutex(&pCBState->CBCurrentMutex), NULL);
    }

    /* Remember the size and id of the command. */
    pCB->cbReservedCmdHeader = cbReserveHeader;
    pCB->cbReservedCmd = cbReserveCmd;
    pCB->u32ReservedCmd = u32CmdId;

    /* Return pointer to the command data. */
    return (uint8_t *)pCB->commands.page.pvR0 + pCB->cbCommand + cbReserveHeader;
}


/** Reserve space for a 3D command in the current miniport command buffer.
 * This function reserves space for a command header and for the command.
 *
 * @param pSvga            The device instance.
 * @param enmCmd           Command identifier.
 * @param cbReserve        Expected size of the command data.
 * @param idDXContext      DX context of the command.
 * @return Pointer to the command data.
 */
void *SvgaCmdBuf3dCmdReserve(PVBOXWDDM_EXT_VMSVGA pSvga, SVGAFifo3dCmdId enmCmd, uint32_t cbReserve, uint32_t idDXContext)
{
    return svgaCBReserve(pSvga, enmCmd, sizeof(SVGA3dCmdHeader), cbReserve, idDXContext);
}


/** Reserve space for a FIFO command in the current miniport command buffer.
 * This function reserves space for the command id and for the command.
 *
 * @param pSvga            The device instance.
 * @param enmCmd           Command identifier.
 * @param cbReserve        Expected size of the command data.
 * @return Pointer to the command data.
 */
void *SvgaCmdBufFifoCmdReserve(PVBOXWDDM_EXT_VMSVGA pSvga, SVGAFifoCmdId enmCmd, uint32_t cbReserve)
{
    return svgaCBReserve(pSvga, enmCmd, sizeof(uint32_t), cbReserve, SVGA3D_INVALID_ID);
}


/** Reserve space for a raw command in the current miniport command buffer.
 * The command already includes any headers.
 *
 * @param pSvga            The device instance.
 * @param cbReserve        Expected size of the command data.
 * @param idDXContext      DX context of the command.
 * @return Pointer to the command data.
 */
void *SvgaCmdBufReserve(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbReserve, uint32_t idDXContext)
{
    return svgaCBReserve(pSvga, SVGA_CMD_INVALID_CMD, 0, cbReserve, idDXContext);
}


/** Commit space for the current command in the current miniport command buffer.
 *
 * @param pSvga            The device instance.
 * @param cbActual         Actual size of the command data. Must be not greater than the reserved size.
 */
void SvgaCmdBufCommit(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbActual)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    PVMSVGACB pCB = pCBState->pCBCurrent;
    AssertReturnVoidStmt(pCB, ExReleaseFastMutex(&pCBState->CBCurrentMutex));

    Assert(cbActual <= pCB->cbReservedCmd);
    cbActual = RT_MIN(cbActual, pCB->cbReservedCmd);

    /* Initialize the command header. */
    if (pCB->cbReservedCmdHeader == sizeof(SVGA3dCmdHeader))
    {
        SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)((uint8_t *)pCB->commands.page.pvR0 + pCB->cbCommand);
        pHeader->id = pCB->u32ReservedCmd;
        pHeader->size = cbActual;
    }
    else if (pCB->cbReservedCmdHeader == sizeof(uint32_t))
    {
        uint32_t *pHeader = (uint32_t *)((uint8_t *)pCB->commands.page.pvR0 + pCB->cbCommand);
        *pHeader = pCB->u32ReservedCmd;
    }
    else
        Assert(pCB->cbReservedCmdHeader == 0);

    pCB->cbCommand += pCB->cbReservedCmdHeader + cbActual;
    pCB->cbReservedCmdHeader = 0;
    pCB->cbReservedCmd = 0;
    pCB->u32ReservedCmd = 0;

    ExReleaseFastMutex(&pCBState->CBCurrentMutex);
}


/** Commit space for the current command in the current miniport command buffer
 *  and attach a completion callback to the command buffer.
 *
 * @param pSvga            The device instance.
 * @param cbActual         Actual size of the command data. Must be not greater than the reserved size.
 * @param pfn              Callback to invoke when the command buffer has been processed by the host.
 * @param pv               Callback parameters.
 * @param cb               Size of callback parameters.
 */
void SvgaCmdBufCommitWithCompletionCallback(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbActual,
    PFNCBCOMPLETION pfn, void const *pv, uint32_t cb)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    VMSVGACBCOMPLETION *p = (VMSVGACBCOMPLETION *)RTMemAlloc(sizeof(VMSVGACBCOMPLETION) + cb);
    AssertReturnVoidStmt(p, ExReleaseFastMutex(&pCBState->CBCurrentMutex));

    p->pfn = pfn;
    p->cb = cb;
    memcpy(&p[1], pv, cb);

    RTListAppend(&pCBState->pCBCurrent->listCompletion, &p->nodeCompletion);

    SvgaCmdBufCommit(pSvga, cbActual);
}


/** Submit the current miniport command buffer to the host.
 * If the buffer contains no command data, then this function does nothing.
 *
 * @param pSvga            The device instance.
 */
void SvgaCmdBufFlush(PVBOXWDDM_EXT_VMSVGA pSvga)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    ExAcquireFastMutex(&pCBState->CBCurrentMutex);

    PVMSVGACB pCB = pCBState->pCBCurrent;
    GALOG(("CB: %p\n", pCB));
    if (pCB && pCB->cbCommand)
    {
        NTSTATUS Status = svgaCBSubmit(pSvga, pCB);
        Assert(NT_SUCCESS(Status)); RT_NOREF(Status);

        pCBState->pCBCurrent = NULL;
    }

    ExReleaseFastMutex(&pCBState->CBCurrentMutex);
}


NTSTATUS SvgaCmdBufSubmitUMD(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACB pCB)
{
    AssertReturn(pCB && pCB->enmType == VMSVGACB_UMD, STATUS_INVALID_PARAMETER);
    return svgaCBSubmit(pSvga, pCB);
}


NTSTATUS SvgaCmdBufAllocUMD(PVBOXWDDM_EXT_VMSVGA pSvga, PHYSICAL_ADDRESS DmaBufferPhysicalAddress,
                            uint32_t cbBuffer, uint32_t cbCommands, uint32_t idDXContext, PVMSVGACB *ppCB)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;
    NTSTATUS Status = svgaCBAlloc(pCBState, VMSVGACB_UMD, idDXContext, cbBuffer, ppCB);
    AssertReturn(NT_SUCCESS(Status), Status);
    GALOG(("CB: %p, cbBuffer %d\n", *ppCB, cbBuffer));

    (*ppCB)->cbBuffer = cbBuffer;
    (*ppCB)->cbCommand = cbCommands;
    (*ppCB)->commands.DmaBufferPhysicalAddress = DmaBufferPhysicalAddress;
    return STATUS_SUCCESS;
}


static void svgaCBCallCompletion(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACB pCB)
{
    PVMSVGACBCOMPLETION pIter, pNext;
    RTListForEachSafe(&pCB->listCompletion, pIter, pNext, VMSVGACBCOMPLETION, nodeCompletion)
    {
        pIter->pfn(pSvga, &pIter[1], pIter->cb);
        RTListNodeRemove(&pIter->nodeCompletion);
        RTMemFree(pIter);
    }
}


/** Process command buffers processed by the host at DPC level.
 *
 * @param pSvga            The device instance.
 */
void SvgaCmdBufProcess(PVBOXWDDM_EXT_VMSVGA pSvga)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    /* Look at submitted queue for buffers which has been completed by the host. */
    RTLISTANCHOR listCompleted;
    RTListInit(&listCompleted);

    PVMSVGACB pIter, pNext;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);
    for (unsigned i = 0; i < RT_ELEMENTS(pCBState->aCBContexts); ++i)
    {
        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[i];
        RTListForEachSafe(&pCBCtx->QueueSubmitted, pIter, pNext, VMSVGACB, nodeQueue)
        {
            /* Buffers are processed sequentially, so if this one has not been processed,
             * then the consequent buffers are too.
             */
            SVGACBStatus const status = pIter->pCBHeader->status;
            if (status == SVGA_CB_STATUS_NONE)
                break;

            /* Remove the command buffer from the submitted queue and add to the local queue. */
            RTListNodeRemove(&pIter->nodeQueue);
            RTListAppend(&listCompleted, &pIter->nodeQueue);

            PVMSVGACBHEADERS pCBHeaders = cbStateHeaders(pCBState);
            uintptr_t const off = (uintptr_t)pIter->pCBHeader - (uintptr_t)&pCBHeaders->aContext0CBHeaders[0];
            LogRel3(("WDDM: cb: finish @%u %u\n", (uint32_t)off, pIter->cbCommand));

            /* Disassociate from CB header which can be used for another CB after spinlock is released. */
            pIter->pCBHeader = NULL;
            pIter->status = status;

            ++pCBCtx->cHeaders;
            Assert(pCBCtx->cHeaders <= RT_ELEMENTS(cbStateHeaders(pCBState)->aContext0CBHeaders));
        }

        /* Try to submit pending buffers. */
        while (!RTListIsEmpty(&pCBCtx->QueuePending))
        {
            PVMSVGACB pCB = RTListGetFirst(&pCBCtx->QueuePending, VMSVGACB, nodeQueue);
            NTSTATUS SubmitStatus = svgaCBSubmitLocked(pSvga, pCB, pCBCtx);
            if (SubmitStatus != STATUS_SUCCESS)
                break;

            RTListNodeRemove(&pCB->nodeQueue);
            RTListAppend(&pCBCtx->QueueSubmitted, &pCB->nodeQueue);
            GALOG(("Submitted pending %p\n", pCB));
        }
    }
    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);

    /* Process the completed buffers without the spinlock. */
    RTListForEachSafe(&listCompleted, pIter, pNext, VMSVGACB, nodeQueue)
    {
        switch (pIter->status)
        {
            case SVGA_CB_STATUS_COMPLETED:
                /* Just delete the buffer. */
                RTListNodeRemove(&pIter->nodeQueue);
                svgaCBCallCompletion(pSvga, pIter);
                svgaCBRetire(pCBState, pIter);
                break;

            case SVGA_CB_STATUS_PREEMPTED:
                /* Delete the buffer. */
                GALOG(("SVGA_CB_STATUS_PREEMPTED %p\n", pIter));
                RTListNodeRemove(&pIter->nodeQueue);
                svgaCBRetire(pCBState, pIter);
                break;

            case SVGA_CB_STATUS_NONE:
            case SVGA_CB_STATUS_QUEUE_FULL:
            case SVGA_CB_STATUS_COMMAND_ERROR:
            case SVGA_CB_STATUS_CB_HEADER_ERROR:
            case SVGA_CB_STATUS_SUBMISSION_ERROR:
            case SVGA_CB_STATUS_PARTIAL_COMPLETE:
            default:
                /** @todo Figure this out later. */
                AssertFailed();

                /* Just delete the buffer. */
                RTListNodeRemove(&pIter->nodeQueue);
                svgaCBRetire(pCBState, pIter);
                break;
        }
    }
}


bool SvgaCmdBufIsIdle(PVBOXWDDM_EXT_VMSVGA pSvga)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    bool fIdle = true;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);
    for (unsigned i = 0; i < RT_ELEMENTS(pCBState->aCBContexts); ++i)
    {
        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[i];
        Assert(pCBCtx->cHeaders <= RT_ELEMENTS(cbStateHeaders(pCBState)->aContext0CBHeaders));
        if (pCBCtx->cHeaders < RT_ELEMENTS(cbStateHeaders(pCBState)->aContext0CBHeaders))
        {
            fIdle = false;
            break;
        }
    }
    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);

    return fIdle;
}


NTSTATUS SvgaCmdBufDestroy(PVBOXWDDM_EXT_VMSVGA pSvga)
{
    /** PVMSVGACBSTATE pCBState as parameter. */
    PVMSVGACBSTATE pCBState = pSvga->pCBState;
    if (pCBState == NULL)
        return STATUS_SUCCESS;
    pSvga->pCBState = NULL;

    PVMSVGACB pIter, pNext;
    for (unsigned i = 0; i < RT_ELEMENTS(pCBState->aCBContexts); ++i)
    {
        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[i];
        RTListForEachSafe(&pCBCtx->QueueSubmitted, pIter, pNext, VMSVGACB, nodeQueue)
        {
            RTListNodeRemove(&pIter->nodeQueue);
            svgaCBFree(pIter);
        }
        RTListForEachSafe(&pCBCtx->QueuePending, pIter, pNext, VMSVGACB, nodeQueue)
        {
            RTListNodeRemove(&pIter->nodeQueue);
            svgaCBFree(pIter);
        }
    }

    svgaCBLookasideListFree(&pCBState->ListCB);
    svgaCBLookasideListFree(&pCBState->ListCBUMD);

    pCBState->pCBCurrent = NULL;

    svgaCBFreePage(&pCBState->CBHeadersPage);

    GaMemFree(pCBState);
    return STATUS_SUCCESS;
}

NTSTATUS SvgaCmdBufInit(PVBOXWDDM_EXT_VMSVGA pSvga)
{
    /** PVMSVGACBSTATE *ppCBState as parameter. */
    NTSTATUS Status;

    PVMSVGACBSTATE pCBState = (PVMSVGACBSTATE)GaMemAllocZero(sizeof(VMSVGACBSTATE));
    AssertReturn(pCBState, STATUS_INSUFFICIENT_RESOURCES);
    pSvga->pCBState = pCBState;

    for (unsigned i = 0; i < RT_ELEMENTS(pCBState->aCBContexts); ++i)
    {
        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[i];
        RTListInit(&pCBCtx->QueuePending);
        RTListInit(&pCBCtx->QueueSubmitted);
        pCBCtx->idxNextHeader = 0;
        pCBCtx->cHeaders = RT_ELEMENTS(cbStateHeaders(pCBState)->aContext0CBHeaders);
    }

    Status = svgaCBAllocPage(&pCBState->CBHeadersPage, sizeof(VMSVGACBHEADERS));
    AssertReturn(NT_SUCCESS(Status), Status);

    ExInitializeFastMutex(&pCBState->CBCurrentMutex);
    KeInitializeSpinLock(&pCBState->SpinLock);
    svgaCBLookasideListInit(&pCBState->ListCB, 3);
    svgaCBLookasideListInit(&pCBState->ListCBUMD, 5);
    return STATUS_SUCCESS;
}
