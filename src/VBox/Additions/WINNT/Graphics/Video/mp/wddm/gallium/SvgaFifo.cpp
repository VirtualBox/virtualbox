/* $Id: SvgaFifo.cpp 110045 2025-06-30 10:34:59Z vitali.pelenjow@oracle.com $ */
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
 * The miniport driver submits buffers in one of 3 case (VMSVGACBTYPE):
 * 1) SVGA_CB_CONTEXT_DEVICE commands.
 *      Small amount of memory.
 *      Synchronous.
 * 2) Submitting commands from the miniport.
 *      Memory for the command data must be allocated.
 *      The host processes the buffer asynchronously, updated the buffer status and generates an interrupt.
 * 3) Submitting command buffers generated by the user mode driver.
 *      Memory for the commands is provided by WDDM (DXGKARG_SUBMITCOMMAND::DmaBufferPhysicalAddress).
 *      Asynchronous processing.
 *
 * A pool of command headers is used to avoid allocation of command headers.
 * The pool space is allocate page by page as necessary. Each page is an array of SVGACBHeader.
 * A bitmask is used in order to track headers. Headers are allocated only for submitted comamnd buffers,
 * in order to minimize consumption.
 *
 * Total size of command buffers must not exceed SVGA_CB_MAX_SIZE.
 * One buffer can be up to SVGA_CB_MAX_COMMAND_SIZE.
 * Up to SVGA_CB_MAX_QUEUED_PER_CONTEXT buffers cane be queued for one command buffer context simultaneously.
 *
 * The miniport allocates page size memory buffers for VMSVGACB_CONTEXT_DEVICE and VMSVGACB_MINIPORT.
 * Initially the memory is allocated on demand and free upom buffer completion.
 * Later a growing and automatically shrinking pool can be used.
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


static void svgaCBHeaderPoolDestroy(PVMSVGACBHEADERPOOL pHeaderPool)
{
    for (unsigned i = 0; i < RT_ELEMENTS(pHeaderPool->aHeaderPoolPages); ++i)
        svgaCBFreePage(&pHeaderPool->aHeaderPoolPages[i]);

    RT_ZERO(*pHeaderPool);
}


static NTSTATUS svgaCBHeaderPoolInit(PVMSVGACBHEADERPOOL pHeaderPool)
{
    /* The pool is already initialized to 0 be the caller. */
    NTSTATUS Status = STATUS_SUCCESS;
    for (unsigned i = 0; i < RT_ELEMENTS(pHeaderPool->aHeaderPoolPages); ++i)
    {
        Status = svgaCBAllocPage(&pHeaderPool->aHeaderPoolPages[i], PAGE_SIZE);
        AssertBreak(NT_SUCCESS(Status));
    }

    if (NT_SUCCESS(Status))
        KeInitializeSpinLock(&pHeaderPool->SpinLock);
    else
        svgaCBHeaderPoolDestroy(pHeaderPool);

    return Status;
}


static void svgaCBHeaderPoolFree(PVMSVGACBHEADERPOOL pHeaderPool,
                                 VMSVGACBHEADERHANDLE hHeader)
{
    if (hHeader != VMSVGACBHEADER_NIL)
    {
        KIRQL OldIrql;
        KeAcquireSpinLock(&pHeaderPool->SpinLock, &OldIrql);
        NTSTATUS Status = GaIdFree(pHeaderPool->au32HeaderBits, sizeof(pHeaderPool->au32HeaderBits),
                                   VMSVGA_CB_HEADER_POOL_NUM_HANDLES, hHeader);
        KeReleaseSpinLock(&pHeaderPool->SpinLock, OldIrql);
        Assert(NT_SUCCESS(Status)); RT_NOREF(Status);
    }
}

static NTSTATUS svgaCBHeaderPoolAlloc(PVMSVGACBHEADERPOOL pHeaderPool,
                                      VMSVGACBHEADERHANDLE *phHeader,
                                      SVGACBHeader **ppCBHeader,
                                      PHYSICAL_ADDRESS *pPhysAddr)
{
    NTSTATUS Status;
    uint32_t id;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pHeaderPool->SpinLock, &OldIrql);
    Status = GaIdAlloc(pHeaderPool->au32HeaderBits, sizeof(pHeaderPool->au32HeaderBits),
                       VMSVGA_CB_HEADER_POOL_NUM_HANDLES, &id);
    KeReleaseSpinLock(&pHeaderPool->SpinLock, OldIrql);
    AssertReturn(NT_SUCCESS(Status), Status);

    int const idxPage = id / VMSVGA_CB_HEADER_POOL_HANDLES_PER_PAGE;
    Assert(idxPage < RT_ELEMENTS(pHeaderPool->aHeaderPoolPages));

    PVMSVGACBPAGE pPage = &pHeaderPool->aHeaderPoolPages[idxPage];
    Assert(pPage->hMemObjMapping != NIL_RTR0MEMOBJ);

    uint32_t const offPage = (id - idxPage * VMSVGA_CB_HEADER_POOL_HANDLES_PER_PAGE) * sizeof(SVGACBHeader);
    *ppCBHeader = (SVGACBHeader *)((uint8_t *)pPage->pvR0 + offPage);
    (*pPhysAddr).QuadPart = pPage->PhysAddr + offPage;
    *phHeader = id;
    return Status;
}


static NTSTATUS svgaCBFree(PVMSVGACBSTATE pCBState, PVMSVGACB pCB)
{
    GALOG(("CB: %p\n", pCB));
    if (pCB->enmType != VMSVGACB_UMD)
        svgaCBFreePage(&pCB->commands.page);

    svgaCBHeaderPoolFree(&pCBState->HeaderPool, pCB->hHeader);

    GaMemFree(pCB);
    return STATUS_SUCCESS;
}


/** Allocate one command buffer.
 * @param pCBState    Command buffers manager.
 * @param enmType     Kind of the buffer.
 * @param idDXContext DX context of the commands in the buffer.
 * @param cbRequired  How many bytes are required for MINIPORT or CONTEXT_DEVICE buffers.
 * @param ppCB        Where to store the allocated buffer pointer.
 */
static NTSTATUS svgaCBAlloc(PVMSVGACBSTATE pCBState, VMSVGACBTYPE enmType, uint32_t idDXContext, uint32_t cbRequired, PVMSVGACB *ppCB)
{
    RT_NOREF(pCBState);

    PVMSVGACB pCB = (PVMSVGACB)GaMemAllocZero(sizeof(VMSVGACB));
    AssertReturn(pCB, STATUS_INSUFFICIENT_RESOURCES);
    GALOG(("CB: %p\n", pCB));

    NTSTATUS Status;

    RT_ZERO(pCB->nodeQueue);
    pCB->enmType = enmType;
    pCB->idDXContext = idDXContext;
    pCB->cbReservedCmdHeader = 0;
    pCB->cbReservedCmd = 0;
    pCB->u32ReservedCmd = 0;
    if (enmType != VMSVGACB_UMD)
    {
        pCB->cbBuffer = RT_ALIGN_32(cbRequired, PAGE_SIZE);
        pCB->cbCommand = 0;
        Status = svgaCBAllocPage(&pCB->commands.page, pCB->cbBuffer);
        AssertReturnStmt(NT_SUCCESS(Status),
                         GaMemFree(pCB),
                         STATUS_INSUFFICIENT_RESOURCES);
    }
    else
    {
        pCB->cbBuffer = 0;
        pCB->cbCommand = 0;
        pCB->commands.DmaBufferPhysicalAddress.QuadPart = 0;
    }

    /* Buffer header is not allocated. */
    pCB->hHeader = VMSVGACBHEADER_NIL;
    RTListInit(&pCB->listCompletion);

    *ppCB = pCB;
    return STATUS_SUCCESS;
}

DECLINLINE(void) svgaCBSubmitHeaderLocked(PVBOXWDDM_EXT_VMSVGA pSvga, PHYSICAL_ADDRESS CBHeaderPhysAddr, SVGACBContext CBContext)
{
    SVGARegWrite(pSvga, SVGA_REG_COMMAND_HIGH, CBHeaderPhysAddr.HighPart);
    SVGARegWrite(pSvga, SVGA_REG_COMMAND_LOW, CBHeaderPhysAddr.LowPart | CBContext);
}

static void svgaCBSubmitHeader(PVBOXWDDM_EXT_VMSVGA pSvga, PHYSICAL_ADDRESS CBHeaderPhysAddr, SVGACBContext CBContext)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);

    svgaCBSubmitHeaderLocked(pSvga, CBHeaderPhysAddr, CBContext);

    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);
}

static NTSTATUS svgaCBSubmit(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACB pCB)
{
    NTSTATUS Status;

    PVMSVGACBSTATE pCBState = pSvga->pCBState;
    GALOG(("CB: %p\n", pCB));

    /* Allocate a header for the buffer. */
    Status = svgaCBHeaderPoolAlloc(&pCBState->HeaderPool, &pCB->hHeader, &pCB->pCBHeader, &pCB->CBHeaderPhysAddr);
    AssertReturn(NT_SUCCESS(Status), Status);

    /* Initialize the header. */
    SVGACBHeader *pCBHeader = pCB->pCBHeader;
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

    /* Select appropriate comamnd buffer context. */
    SVGACBContext CBContext;
    if (pCB->enmType != VMSVGACB_CONTEXT_DEVICE)
    {
        CBContext = SVGA_CB_CONTEXT_0;

        KIRQL OldIrql;
        KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);

        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[CBContext];
        if (pCBCtx->cSubmitted >= SVGA_CB_MAX_QUEUED_PER_CONTEXT - 1)
        {
            /* Can't submit the buffer. Put it into pending queue. */
            RTListAppend(&pCBCtx->QueuePending, &pCB->nodeQueue);

            KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);
            return STATUS_SUCCESS;
        }

        RTListAppend(&pCBCtx->QueueSubmitted, &pCB->nodeQueue);
        ++pCBCtx->cSubmitted;
#ifdef DEBUG
        Assert(!pCB->fSubmitted);
        if (pCB->fSubmitted)
            GALOG(("CB: %p already submitted\n", pCB));
        pCB->fSubmitted = true;
#endif

        KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);
    }
    else
        CBContext = SVGA_CB_CONTEXT_DEVICE;

    svgaCBSubmitHeader(pSvga, pCB->CBHeaderPhysAddr, CBContext);
    return STATUS_SUCCESS;
}

NTSTATUS SvgaCmdBufDeviceCommand(PVBOXWDDM_EXT_VMSVGA pSvga, void const *pvCmd, uint32_t cbCmd)
{
    PVMSVGACBSTATE pCBState = pSvga->pCBState;

    PVMSVGACB pCB;
    NTSTATUS Status = svgaCBAlloc(pCBState, VMSVGACB_CONTEXT_DEVICE, SVGA3D_INVALID_ID, cbCmd, &pCB);
    AssertReturn(NT_SUCCESS(Status), Status);

    memcpy(pCB->commands.page.pvR0, pvCmd, cbCmd);
    pCB->cbCommand = cbCmd;

    Status = svgaCBSubmit(pSvga, pCB);
    if (NT_SUCCESS(Status))
    {
        if (pCB->pCBHeader->status != SVGA_CB_STATUS_COMPLETED)
            Status = STATUS_INVALID_PARAMETER;
    }
    svgaCBFree(pCBState, pCB);
    return Status;
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

    KIRQL OldIrql;
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);
    for (unsigned i = 0; i < RT_ELEMENTS(pCBState->aCBContexts); ++i)
    {
        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[i];

        PVMSVGACB pIter, pNext;
        pCBCtx->cCompleted = 0;
        RTListForEachSafe(&pCBCtx->QueueSubmitted, pIter, pNext, VMSVGACB, nodeQueue)
        {
            /* Buffers are processed sequentially, so if this one has not been processed,
             * then the consequent buffers are too.
             */
            if (pIter->pCBHeader->status == SVGA_CB_STATUS_NONE)
                break;

            /* Remove the command buffer from the submitted queue and add to the local queue. */
            RTListNodeRemove(&pIter->nodeQueue);
            RTListAppend(&listCompleted, &pIter->nodeQueue);
            ++pCBCtx->cCompleted;
        }

        /* Try to submit pending buffers. */
        while (!RTListIsEmpty(&pCBCtx->QueuePending))
        {
            if (pCBCtx->cSubmitted >= SVGA_CB_MAX_QUEUED_PER_CONTEXT - 1)
                break;

            PVMSVGACB pCB = RTListGetFirst(&pCBCtx->QueuePending, VMSVGACB, nodeQueue);
            RTListNodeRemove(&pCB->nodeQueue);

            RTListAppend(&pCBCtx->QueueSubmitted, &pCB->nodeQueue);
            ++pCBCtx->cSubmitted;
            svgaCBSubmitHeaderLocked(pSvga, pCB->CBHeaderPhysAddr, (SVGACBContext)i);
            GALOG(("Submitted pending %p\n", pCB));
        }
    }
    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);

    /* Process the completed buffers without the spinlock. */
    PVMSVGACB pIter, pNext;
    RTListForEachSafe(&listCompleted, pIter, pNext, VMSVGACB, nodeQueue)
    {
        switch (pIter->pCBHeader->status)
        {
            case SVGA_CB_STATUS_COMPLETED:
                /* Just delete the buffer. */
                RTListNodeRemove(&pIter->nodeQueue);
                svgaCBCallCompletion(pSvga, pIter);
                svgaCBFree(pCBState, pIter);
                break;

            case SVGA_CB_STATUS_PREEMPTED:
                /* Delete the buffer. */
                GALOG(("SVGA_CB_STATUS_PREEMPTED %p\n", pIter));
                RTListNodeRemove(&pIter->nodeQueue);
                svgaCBFree(pCBState, pIter);
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
                svgaCBFree(pCBState, pIter);
                break;
        }
    }

    /* Decrement 'cSubmitted' after freeing completed command buffers
     * in order to make sure that SvgaCmdBufIsIdle returns true only
     * if there are no buffers being processed.
     */
    KeAcquireSpinLock(&pCBState->SpinLock, &OldIrql);
    for (unsigned i = 0; i < RT_ELEMENTS(pCBState->aCBContexts); ++i)
    {
        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[i];
        if (pCBCtx->cCompleted <= pCBCtx->cSubmitted)
            pCBCtx->cSubmitted -= pCBCtx->cCompleted;
        else
            AssertFailedStmt(pCBCtx->cSubmitted = 0);
    }
    KeReleaseSpinLock(&pCBState->SpinLock, OldIrql);
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
        if (pCBCtx->cSubmitted > 0)
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

    for (unsigned i = 0; i < RT_ELEMENTS(pCBState->aCBContexts); ++i)
    {
        PVMSVGACBCONTEXT pCBCtx = &pCBState->aCBContexts[i];
        PVMSVGACB pIter, pNext;
        RTListForEachSafe(&pCBCtx->QueueSubmitted, pIter, pNext, VMSVGACB, nodeQueue)
        {
            RTListNodeRemove(&pIter->nodeQueue);
            svgaCBFree(pCBState, pIter);
        }
        RTListForEachSafe(&pCBCtx->QueuePending, pIter, pNext, VMSVGACB, nodeQueue)
        {
            RTListNodeRemove(&pIter->nodeQueue);
            svgaCBFree(pCBState, pIter);
        }
    }

    if (pCBState->pCBCurrent)
    {
        svgaCBFree(pCBState, pCBState->pCBCurrent);
        pCBState->pCBCurrent = NULL;
    }

    svgaCBHeaderPoolDestroy(&pCBState->HeaderPool);

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
        //pCBCtx->cSubmitted = 0;
    }

    Status = svgaCBHeaderPoolInit(&pCBState->HeaderPool);
    AssertReturn(NT_SUCCESS(Status), Status);

    ExInitializeFastMutex(&pCBState->CBCurrentMutex);
    KeInitializeSpinLock(&pCBState->SpinLock);
    return STATUS_SUCCESS;
}
