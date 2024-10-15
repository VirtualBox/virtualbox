/* $Id: VBoxMPLegacy.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM Miniport driver. The legacy VBoxVGA adapter support with 2D software unaccelerated
 * framebuffer operations.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include "VBoxMPWddm.h"
#include "common/VBoxMPCommon.h"
#include "common/VBoxMPHGSMI.h"
#ifdef VBOX_WITH_VIDEOHWACCEL
# include "VBoxMPVhwa.h"
#endif
#include "VBoxMPVidPn.h"
#include "VBoxMPLegacy.h"

#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/param.h>
#include <iprt/initterm.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/VMMDev.h> /* for VMMDevVideoSetVisibleRegion */
#include <VBoxVideo.h>
#include <wingdi.h> /* needed for RGNDATA definition */
#include <VBoxDisplay.h> /* this is from Additions/WINNT/include/ to include escape codes */
#include <VBoxVideoVBE.h>
#include <VBox/Version.h>


/* ddi dma command queue handling */
typedef enum
{
    VBOXVDMADDI_STATE_UNCKNOWN = 0,
    VBOXVDMADDI_STATE_NOT_DX_CMD,
    VBOXVDMADDI_STATE_NOT_QUEUED,
    VBOXVDMADDI_STATE_PENDING,
    VBOXVDMADDI_STATE_SUBMITTED,
    VBOXVDMADDI_STATE_COMPLETED
} VBOXVDMADDI_STATE;

typedef struct VBOXVDMADDI_CMD *PVBOXVDMADDI_CMD;
typedef DECLCALLBACKTYPE(VOID, FNVBOXVDMADDICMDCOMPLETE_DPC,(PVBOXMP_DEVEXT pDevExt, PVBOXVDMADDI_CMD pCmd, PVOID pvContext));
typedef FNVBOXVDMADDICMDCOMPLETE_DPC *PFNVBOXVDMADDICMDCOMPLETE_DPC;

typedef struct VBOXVDMADDI_CMD
{
    LIST_ENTRY QueueEntry;
    VBOXVDMADDI_STATE enmState;
    uint32_t u32NodeOrdinal;
    uint32_t u32FenceId;
    DXGK_INTERRUPT_TYPE enmComplType;
    PFNVBOXVDMADDICMDCOMPLETE_DPC pfnComplete;
    PVOID pvComplete;
} VBOXVDMADDI_CMD, *PVBOXVDMADDI_CMD;

#define VBOXVDMADDI_CMD_FROM_ENTRY(_pEntry) ((PVBOXVDMADDI_CMD)(((uint8_t*)(_pEntry)) - RT_OFFSETOF(VBOXVDMADDI_CMD, QueueEntry)))

typedef struct VBOXWDDM_DMA_ALLOCINFO
{
    PVBOXWDDM_ALLOCATION pAlloc;
    VBOXVIDEOOFFSET offAlloc;
    UINT segmentIdAlloc : 31;
    UINT fWriteOp : 1;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId;
} VBOXWDDM_DMA_ALLOCINFO, *PVBOXWDDM_DMA_ALLOCINFO;

typedef struct VBOXVDMAPIPE_RECTS
{
    RECT ContextRect;
    VBOXWDDM_RECTS_INFO UpdateRects;
} VBOXVDMAPIPE_RECTS, *PVBOXVDMAPIPE_RECTS;

typedef struct VBOXVDMA_CLRFILL
{
    VBOXWDDM_DMA_ALLOCINFO Alloc;
    UINT Color;
    VBOXWDDM_RECTS_INFO Rects;
} VBOXVDMA_CLRFILL, *PVBOXVDMA_CLRFILL;

typedef struct VBOXVDMA_BLT
{
    VBOXWDDM_DMA_ALLOCINFO SrcAlloc;
    VBOXWDDM_DMA_ALLOCINFO DstAlloc;
    RECT SrcRect;
    VBOXVDMAPIPE_RECTS DstRects;
} VBOXVDMA_BLT, *PVBOXVDMA_BLT;

typedef struct VBOXVDMA_FLIP
{
    VBOXWDDM_DMA_ALLOCINFO Alloc;
} VBOXVDMA_FLIP, *PVBOXVDMA_FLIP;

typedef struct VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR
{
    VBOXWDDM_DMA_PRIVATEDATA_BASEHDR BaseHdr;
}VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR, *PVBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR;

typedef struct VBOXWDDM_DMA_PRIVATEDATA_BLT
{
    VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    VBOXVDMA_BLT Blt;
} VBOXWDDM_DMA_PRIVATEDATA_BLT, *PVBOXWDDM_DMA_PRIVATEDATA_BLT;

typedef struct VBOXWDDM_DMA_PRIVATEDATA_FLIP
{
    VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    VBOXVDMA_FLIP Flip;
} VBOXWDDM_DMA_PRIVATEDATA_FLIP, *PVBOXWDDM_DMA_PRIVATEDATA_FLIP;

typedef struct VBOXWDDM_DMA_PRIVATEDATA_CLRFILL
{
    VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    VBOXVDMA_CLRFILL ClrFill;
} VBOXWDDM_DMA_PRIVATEDATA_CLRFILL, *PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL;

typedef enum
{
    VBOXWDDM_HGSMICMD_TYPE_UNDEFINED = 0,
    VBOXWDDM_HGSMICMD_TYPE_CTL       = 1,
} VBOXWDDM_HGSMICMD_TYPE;

VBOXWDDM_HGSMICMD_TYPE vboxWddmHgsmiGetCmdTypeFromOffset(PVBOXMP_DEVEXT pDevExt, HGSMIOFFSET offCmd)
{
    if (HGSMIAreaContainsOffset(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx.Heap.area, offCmd))
        return VBOXWDDM_HGSMICMD_TYPE_CTL;
    return VBOXWDDM_HGSMICMD_TYPE_UNDEFINED;
}

VOID vboxVdmaDdiNodesInit(PVBOXMP_DEVEXT pDevExt)
{
    for (UINT i = 0; i < RT_ELEMENTS(pDevExt->aNodes); ++i)
    {
        pDevExt->aNodes[i].uLastCompletedFenceId = 0;
        PVBOXVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[i].CmdQueue;
        pQueue->cQueuedCmds = 0;
        InitializeListHead(&pQueue->CmdQueue);
    }
    InitializeListHead(&pDevExt->DpcCmdQueue);
}

static VOID vboxVdmaDdiCmdNotifyCompletedIrq(PVBOXMP_DEVEXT pDevExt, UINT u32NodeOrdinal, UINT u32FenceId, DXGK_INTERRUPT_TYPE enmComplType)
{
    PVBOXVDMADDI_NODE pNode = &pDevExt->aNodes[u32NodeOrdinal];
    DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
    memset(&notify, 0, sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA));
    switch (enmComplType)
    {
        case DXGK_INTERRUPT_DMA_COMPLETED:
            notify.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
            notify.DmaCompleted.SubmissionFenceId = u32FenceId;
            notify.DmaCompleted.NodeOrdinal = u32NodeOrdinal;
            pNode->uLastCompletedFenceId = u32FenceId;
            break;

        case DXGK_INTERRUPT_DMA_PREEMPTED:
            Assert(0);
            notify.InterruptType = DXGK_INTERRUPT_DMA_PREEMPTED;
            notify.DmaPreempted.PreemptionFenceId = u32FenceId;
            notify.DmaPreempted.NodeOrdinal = u32NodeOrdinal;
            notify.DmaPreempted.LastCompletedFenceId = pNode->uLastCompletedFenceId;
            break;

        case DXGK_INTERRUPT_DMA_FAULTED:
            Assert(0);
            notify.InterruptType = DXGK_INTERRUPT_DMA_FAULTED;
            notify.DmaFaulted.FaultedFenceId = u32FenceId;
            notify.DmaFaulted.Status = STATUS_UNSUCCESSFUL; /** @todo better status ? */
            notify.DmaFaulted.NodeOrdinal = u32NodeOrdinal;
            break;

        default:
            Assert(0);
            break;
    }

    pDevExt->u.primary.DxgkInterface.DxgkCbNotifyInterrupt(pDevExt->u.primary.DxgkInterface.DeviceHandle, &notify);
}

static VOID vboxVdmaDdiCmdProcessCompletedIrq(PVBOXMP_DEVEXT pDevExt, PVBOXVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType)
{
    vboxVdmaDdiCmdNotifyCompletedIrq(pDevExt, pCmd->u32NodeOrdinal, pCmd->u32FenceId, enmComplType);
    switch (enmComplType)
    {
        case DXGK_INTERRUPT_DMA_COMPLETED:
            InsertTailList(&pDevExt->DpcCmdQueue, &pCmd->QueueEntry);
            break;
        default:
            AssertFailed();
            break;
    }
}

DECLINLINE(VOID) vboxVdmaDdiCmdDequeueIrq(PVBOXMP_DEVEXT pDevExt, PVBOXVDMADDI_CMD pCmd)
{
    PVBOXVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[pCmd->u32NodeOrdinal].CmdQueue;
    ASMAtomicDecU32(&pQueue->cQueuedCmds);
    RemoveEntryList(&pCmd->QueueEntry);
}

DECLINLINE(VOID) vboxVdmaDdiCmdEnqueueIrq(PVBOXMP_DEVEXT pDevExt, PVBOXVDMADDI_CMD pCmd)
{
    PVBOXVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[pCmd->u32NodeOrdinal].CmdQueue;
    ASMAtomicIncU32(&pQueue->cQueuedCmds);
    InsertTailList(&pQueue->CmdQueue, &pCmd->QueueEntry);
}

static BOOLEAN vboxVdmaDdiCmdCompletedIrq(PVBOXMP_DEVEXT pDevExt, PVBOXVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType)
{
    if (VBOXVDMADDI_STATE_NOT_DX_CMD == pCmd->enmState)
    {
        InsertTailList(&pDevExt->DpcCmdQueue, &pCmd->QueueEntry);
        return FALSE;
    }

    PVBOXVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[pCmd->u32NodeOrdinal].CmdQueue;
    BOOLEAN bQueued = pCmd->enmState > VBOXVDMADDI_STATE_NOT_QUEUED;
    BOOLEAN bComplete = FALSE;
    Assert(!bQueued || pQueue->cQueuedCmds);
    Assert(!bQueued || !IsListEmpty(&pQueue->CmdQueue));
    pCmd->enmState = VBOXVDMADDI_STATE_COMPLETED;
    if (bQueued)
    {
        if (pQueue->CmdQueue.Flink == &pCmd->QueueEntry)
        {
            vboxVdmaDdiCmdDequeueIrq(pDevExt, pCmd);
            bComplete = TRUE;
        }
    }
    else if (IsListEmpty(&pQueue->CmdQueue))
    {
        bComplete = TRUE;
    }
    else
    {
        vboxVdmaDdiCmdEnqueueIrq(pDevExt, pCmd);
    }

    if (bComplete)
    {
        vboxVdmaDdiCmdProcessCompletedIrq(pDevExt, pCmd, enmComplType);

        while (!IsListEmpty(&pQueue->CmdQueue))
        {
            pCmd = VBOXVDMADDI_CMD_FROM_ENTRY(pQueue->CmdQueue.Flink);
            if (pCmd->enmState == VBOXVDMADDI_STATE_COMPLETED)
            {
                vboxVdmaDdiCmdDequeueIrq(pDevExt, pCmd);
                vboxVdmaDdiCmdProcessCompletedIrq(pDevExt, pCmd, pCmd->enmComplType);
            }
            else
                break;
        }
    }
    else
    {
        pCmd->enmState = VBOXVDMADDI_STATE_COMPLETED;
        pCmd->enmComplType = enmComplType;
    }

    return bComplete;
}

typedef struct VBOXVDMADDI_CMD_COMPLETED_CB
{
    PVBOXMP_DEVEXT pDevExt;
    PVBOXVDMADDI_CMD pCmd;
    DXGK_INTERRUPT_TYPE enmComplType;
} VBOXVDMADDI_CMD_COMPLETED_CB, *PVBOXVDMADDI_CMD_COMPLETED_CB;

static BOOLEAN vboxVdmaDdiCmdCompletedCb(PVOID Context)
{
    PVBOXVDMADDI_CMD_COMPLETED_CB pdc = (PVBOXVDMADDI_CMD_COMPLETED_CB)Context;
    PVBOXMP_DEVEXT pDevExt = pdc->pDevExt;
    BOOLEAN bNeedDpc = vboxVdmaDdiCmdCompletedIrq(pDevExt, pdc->pCmd, pdc->enmComplType);
    pDevExt->bNotifyDxDpc |= bNeedDpc;

    if (bNeedDpc)
    {
        pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);
    }

    return bNeedDpc;
}

static NTSTATUS vboxVdmaDdiCmdCompleted(PVBOXMP_DEVEXT pDevExt, PVBOXVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType)
{
    VBOXVDMADDI_CMD_COMPLETED_CB context;
    context.pDevExt = pDevExt;
    context.pCmd = pCmd;
    context.enmComplType = enmComplType;
    BOOLEAN bNeedDps;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            vboxVdmaDdiCmdCompletedCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bNeedDps);
    AssertNtStatusSuccess(Status);
    return Status;
}

DECLINLINE(VOID) vboxVdmaDdiCmdInit(PVBOXVDMADDI_CMD pCmd, uint32_t u32NodeOrdinal, uint32_t u32FenceId,
                                    PFNVBOXVDMADDICMDCOMPLETE_DPC pfnComplete, PVOID pvComplete)
{
    pCmd->QueueEntry.Blink = NULL;
    pCmd->QueueEntry.Flink = NULL;
    pCmd->enmState = VBOXVDMADDI_STATE_NOT_QUEUED;
    pCmd->u32NodeOrdinal = u32NodeOrdinal;
    pCmd->u32FenceId = u32FenceId;
    pCmd->pfnComplete = pfnComplete;
    pCmd->pvComplete = pvComplete;
}

static DECLCALLBACK(VOID) vboxVdmaDdiCmdCompletionCbFree(PVBOXMP_DEVEXT pDevExt, PVBOXVDMADDI_CMD pCmd, PVOID pvContext)
{
    RT_NOREF(pDevExt, pvContext);
    vboxWddmMemFree(pCmd);
}

DECLINLINE(BOOLEAN) vboxVdmaDdiCmdCanComplete(PVBOXMP_DEVEXT pDevExt, UINT u32NodeOrdinal)
{
    PVBOXVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[u32NodeOrdinal].CmdQueue;
    return ASMAtomicUoReadU32(&pQueue->cQueuedCmds) == 0;
}

typedef struct VBOXVDMADDI_CMD_COMPLETE_CB
{
    PVBOXMP_DEVEXT pDevExt;
    UINT u32NodeOrdinal;
    uint32_t u32FenceId;
} VBOXVDMADDI_CMD_COMPLETE_CB, *PVBOXVDMADDI_CMD_COMPLETE_CB;

static BOOLEAN vboxVdmaDdiCmdFenceCompleteCb(PVOID Context)
{
    PVBOXVDMADDI_CMD_COMPLETE_CB pdc = (PVBOXVDMADDI_CMD_COMPLETE_CB)Context;
    PVBOXMP_DEVEXT pDevExt = pdc->pDevExt;

    vboxVdmaDdiCmdNotifyCompletedIrq(pDevExt, pdc->u32NodeOrdinal, pdc->u32FenceId, DXGK_INTERRUPT_DMA_COMPLETED);

    pDevExt->bNotifyDxDpc = TRUE;
    pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);

    return TRUE;
}

static NTSTATUS vboxVdmaDdiCmdFenceNotifyComplete(PVBOXMP_DEVEXT pDevExt, uint32_t u32NodeOrdinal, uint32_t u32FenceId)
{
    VBOXVDMADDI_CMD_COMPLETE_CB context;
    context.pDevExt = pDevExt;
    context.u32NodeOrdinal = u32NodeOrdinal;
    context.u32FenceId = u32FenceId;
    BOOLEAN bRet;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            vboxVdmaDdiCmdFenceCompleteCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    AssertNtStatusSuccess(Status);
    return Status;
}

static NTSTATUS vboxVdmaDdiCmdFenceComplete(PVBOXMP_DEVEXT pDevExt, uint32_t u32NodeOrdinal, uint32_t u32FenceId, DXGK_INTERRUPT_TYPE enmComplType)
{
    if (vboxVdmaDdiCmdCanComplete(pDevExt, u32NodeOrdinal))
        return vboxVdmaDdiCmdFenceNotifyComplete(pDevExt, u32NodeOrdinal, u32FenceId);

    PVBOXVDMADDI_CMD pCmd = (PVBOXVDMADDI_CMD)vboxWddmMemAlloc(sizeof (VBOXVDMADDI_CMD));
    Assert(pCmd);
    if (pCmd)
    {
        vboxVdmaDdiCmdInit(pCmd, u32NodeOrdinal, u32FenceId, vboxVdmaDdiCmdCompletionCbFree, NULL);
        NTSTATUS Status = vboxVdmaDdiCmdCompleted(pDevExt, pCmd, enmComplType);
        AssertNtStatusSuccess(Status);
        if (Status == STATUS_SUCCESS)
            return STATUS_SUCCESS;
        vboxWddmMemFree(pCmd);
        return Status;
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS vboxVdmaGgDmaBltPerform(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOC_DATA pSrcAlloc, RECT* pSrcRect,
        PVBOXWDDM_ALLOC_DATA pDstAlloc, RECT* pDstRect)
{
    uint8_t* pvVramBase = pDevExt->pvVisibleVram;
    /* we do not support stretching */
    uint32_t srcWidth = pSrcRect->right - pSrcRect->left;
    uint32_t srcHeight = pSrcRect->bottom - pSrcRect->top;
    uint32_t dstWidth = pDstRect->right - pDstRect->left;
    uint32_t dstHeight = pDstRect->bottom - pDstRect->top;
    Assert(srcHeight == dstHeight);
    Assert(dstWidth == srcWidth);
    Assert(pDstAlloc->Addr.offVram != VBOXVIDEOOFFSET_VOID);
    Assert(pSrcAlloc->Addr.offVram != VBOXVIDEOOFFSET_VOID);
    D3DDDIFORMAT enmSrcFormat, enmDstFormat;

    enmSrcFormat = pSrcAlloc->SurfDesc.format;
    enmDstFormat = pDstAlloc->SurfDesc.format;

    if (pDstAlloc->Addr.SegmentId && pDstAlloc->Addr.SegmentId != 1)
    {
        WARN(("request to collor blit invalid allocation"));
        return STATUS_INVALID_PARAMETER;
    }

    if (pSrcAlloc->Addr.SegmentId && pSrcAlloc->Addr.SegmentId != 1)
    {
        WARN(("request to collor blit invalid allocation"));
        return STATUS_INVALID_PARAMETER;
    }

    if (enmSrcFormat != enmDstFormat)
    {
        /* just ignore the alpha component
         * this is ok since our software-based stuff can not handle alpha channel in any way */
        enmSrcFormat = vboxWddmFmtNoAlphaFormat(enmSrcFormat);
        enmDstFormat = vboxWddmFmtNoAlphaFormat(enmDstFormat);
        if (enmSrcFormat != enmDstFormat)
        {
            WARN(("color conversion src(%d), dst(%d) not supported!", pSrcAlloc->SurfDesc.format, pDstAlloc->SurfDesc.format));
            return STATUS_INVALID_PARAMETER;
        }
    }
    if (srcHeight != dstHeight)
            return STATUS_INVALID_PARAMETER;
    if (srcWidth != dstWidth)
            return STATUS_INVALID_PARAMETER;
    if (pDstAlloc->Addr.offVram == VBOXVIDEOOFFSET_VOID)
        return STATUS_INVALID_PARAMETER;
    if (pSrcAlloc->Addr.offVram == VBOXVIDEOOFFSET_VOID)
        return STATUS_INVALID_PARAMETER;

    uint8_t *pvDstSurf = pDstAlloc->Addr.SegmentId ? pvVramBase + pDstAlloc->Addr.offVram : (uint8_t*)pDstAlloc->Addr.pvMem;
    uint8_t *pvSrcSurf = pSrcAlloc->Addr.SegmentId ? pvVramBase + pSrcAlloc->Addr.offVram : (uint8_t*)pSrcAlloc->Addr.pvMem;

    if (pDstAlloc->SurfDesc.width == dstWidth
            && pSrcAlloc->SurfDesc.width == srcWidth
            && pSrcAlloc->SurfDesc.width == pDstAlloc->SurfDesc.width)
    {
        Assert(!pDstRect->left);
        Assert(!pSrcRect->left);
        uint32_t cbDstOff = vboxWddmCalcOffXYrd(0 /* x */, pDstRect->top, pDstAlloc->SurfDesc.pitch, pDstAlloc->SurfDesc.format);
        uint32_t cbSrcOff = vboxWddmCalcOffXYrd(0 /* x */, pSrcRect->top, pSrcAlloc->SurfDesc.pitch, pSrcAlloc->SurfDesc.format);
        uint32_t cbSize = vboxWddmCalcSize(pDstAlloc->SurfDesc.pitch, dstHeight, pDstAlloc->SurfDesc.format);
        memcpy(pvDstSurf + cbDstOff, pvSrcSurf + cbSrcOff, cbSize);
    }
    else
    {
        uint32_t cbDstLine =  vboxWddmCalcRowSize(pDstRect->left, pDstRect->right, pDstAlloc->SurfDesc.format);
        uint32_t offDstStart = vboxWddmCalcOffXYrd(pDstRect->left, pDstRect->top, pDstAlloc->SurfDesc.pitch, pDstAlloc->SurfDesc.format);
        Assert(cbDstLine <= pDstAlloc->SurfDesc.pitch);
        uint32_t cbDstSkip = pDstAlloc->SurfDesc.pitch;
        uint8_t * pvDstStart = pvDstSurf + offDstStart;

        uint32_t cbSrcLine = vboxWddmCalcRowSize(pSrcRect->left, pSrcRect->right, pSrcAlloc->SurfDesc.format);
        uint32_t offSrcStart = vboxWddmCalcOffXYrd(pSrcRect->left, pSrcRect->top, pSrcAlloc->SurfDesc.pitch, pSrcAlloc->SurfDesc.format);
        Assert(cbSrcLine <= pSrcAlloc->SurfDesc.pitch); NOREF(cbSrcLine);
        uint32_t cbSrcSkip = pSrcAlloc->SurfDesc.pitch;
        const uint8_t * pvSrcStart = pvSrcSurf + offSrcStart;

        uint32_t cRows = vboxWddmCalcNumRows(pDstRect->top, pDstRect->bottom, pDstAlloc->SurfDesc.format);

        Assert(cbDstLine == cbSrcLine);

        for (uint32_t i = 0; i < cRows; ++i)
        {
            memcpy(pvDstStart, pvSrcStart, cbDstLine);
            pvDstStart += cbDstSkip;
            pvSrcStart += cbSrcSkip;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS vboxVdmaGgDmaColorFill(PVBOXMP_DEVEXT pDevExt, VBOXVDMA_CLRFILL *pCF)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    Assert (pDevExt->pvVisibleVram);
    if (pDevExt->pvVisibleVram)
    {
        PVBOXWDDM_ALLOCATION pAlloc = pCF->Alloc.pAlloc;
        if (pAlloc->AllocData.Addr.SegmentId && pAlloc->AllocData.Addr.SegmentId != 1)
        {
            WARN(("request to collor fill invalid allocation"));
            return STATUS_INVALID_PARAMETER;
        }

        VBOXVIDEOOFFSET offVram = vboxWddmAddrFramOffset(&pAlloc->AllocData.Addr);
        if (offVram != VBOXVIDEOOFFSET_VOID)
        {
            RECT UnionRect = {0};
            uint8_t *pvMem = pDevExt->pvVisibleVram + offVram;
            UINT bpp = pAlloc->AllocData.SurfDesc.bpp;
            Assert(bpp);
            Assert(((bpp * pAlloc->AllocData.SurfDesc.width) >> 3) == pAlloc->AllocData.SurfDesc.pitch);
            switch (bpp)
            {
                case 32:
                {
                    uint8_t bytestPP = bpp >> 3;
                    for (UINT i = 0; i < pCF->Rects.cRects; ++i)
                    {
                        RECT *pRect = &pCF->Rects.aRects[i];
                        for (LONG ir = pRect->top; ir < pRect->bottom; ++ir)
                        {
                            uint32_t * pvU32Mem = (uint32_t*)(pvMem + (ir * pAlloc->AllocData.SurfDesc.pitch) + (pRect->left * bytestPP));
                            uint32_t cRaw = pRect->right - pRect->left;
                            Assert(pRect->left >= 0);
                            Assert(pRect->right <= (LONG)pAlloc->AllocData.SurfDesc.width);
                            Assert(pRect->top >= 0);
                            Assert(pRect->bottom <= (LONG)pAlloc->AllocData.SurfDesc.height);
                            for (UINT j = 0; j < cRaw; ++j)
                            {
                                *pvU32Mem = pCF->Color;
                                ++pvU32Mem;
                            }
                        }
                        vboxWddmRectUnited(&UnionRect, &UnionRect, pRect);
                    }
                    Status = STATUS_SUCCESS;
                    break;
                }
                case 16:
                case 8:
                default:
                    AssertBreakpoint();
                    break;
            }

            if (Status == STATUS_SUCCESS)
            {
                if (pAlloc->AllocData.SurfDesc.VidPnSourceId != D3DDDI_ID_UNINITIALIZED
                        && VBOXWDDM_IS_FB_ALLOCATION(pDevExt, pAlloc)
                        && pAlloc->bVisible
                        )
                {
                    if (!vboxWddmRectIsEmpty(&UnionRect))
                    {
                        PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pCF->Alloc.pAlloc->AllocData.SurfDesc.VidPnSourceId];
                        uint32_t cUnlockedVBVADisabled = ASMAtomicReadU32(&pDevExt->cUnlockedVBVADisabled);
                        if (!cUnlockedVBVADisabled)
                        {
                            VBOXVBVA_OP(ReportDirtyRect, pDevExt, pSource, &UnionRect);
                        }
                        else
                        {
                            VBOXVBVA_OP_WITHLOCK(ReportDirtyRect, pDevExt, pSource, &UnionRect);
                        }
                    }
                }
                else
                {
                    AssertBreakpoint();
                }
            }
        }
        else
            WARN(("invalid offVram"));
    }

    return Status;
}

static void vboxVdmaBltDirtyRectsUpdate(PVBOXMP_DEVEXT pDevExt, VBOXWDDM_SOURCE *pSource, uint32_t cRects, const RECT *paRects)
{
    if (!cRects)
    {
        WARN(("vboxVdmaBltDirtyRectsUpdate: no rects specified"));
        return;
    }

    RECT rect;
    rect = paRects[0];
    for (UINT i = 1; i < cRects; ++i)
    {
        vboxWddmRectUnited(&rect, &rect, &paRects[i]);
    }

    uint32_t cUnlockedVBVADisabled = ASMAtomicReadU32(&pDevExt->cUnlockedVBVADisabled);
    if (!cUnlockedVBVADisabled)
    {
        VBOXVBVA_OP(ReportDirtyRect, pDevExt, pSource, &rect);
    }
    else
    {
        VBOXVBVA_OP_WITHLOCK_ATDPC(ReportDirtyRect, pDevExt, pSource, &rect);
    }
}

/*
 * @return on success the number of bytes the command contained, otherwise - VERR_xxx error code
 */
static NTSTATUS vboxVdmaGgDmaBlt(PVBOXMP_DEVEXT pDevExt, PVBOXVDMA_BLT pBlt)
{
    /* we do not support stretching for now */
    Assert(pBlt->SrcRect.right - pBlt->SrcRect.left == pBlt->DstRects.ContextRect.right - pBlt->DstRects.ContextRect.left);
    Assert(pBlt->SrcRect.bottom - pBlt->SrcRect.top == pBlt->DstRects.ContextRect.bottom - pBlt->DstRects.ContextRect.top);
    if (pBlt->SrcRect.right - pBlt->SrcRect.left != pBlt->DstRects.ContextRect.right - pBlt->DstRects.ContextRect.left)
        return STATUS_INVALID_PARAMETER;
    if (pBlt->SrcRect.bottom - pBlt->SrcRect.top != pBlt->DstRects.ContextRect.bottom - pBlt->DstRects.ContextRect.top)
        return STATUS_INVALID_PARAMETER;
    Assert(pBlt->DstRects.UpdateRects.cRects);

    NTSTATUS Status = STATUS_SUCCESS;

    if (pBlt->DstRects.UpdateRects.cRects)
    {
        for (uint32_t i = 0; i < pBlt->DstRects.UpdateRects.cRects; ++i)
        {
            RECT SrcRect;
            vboxWddmRectTranslated(&SrcRect, &pBlt->DstRects.UpdateRects.aRects[i], -pBlt->DstRects.ContextRect.left, -pBlt->DstRects.ContextRect.top);

            Status = vboxVdmaGgDmaBltPerform(pDevExt, &pBlt->SrcAlloc.pAlloc->AllocData, &SrcRect,
                    &pBlt->DstAlloc.pAlloc->AllocData, &pBlt->DstRects.UpdateRects.aRects[i]);
            AssertNtStatusSuccess(Status);
            if (Status != STATUS_SUCCESS)
                return Status;
        }
    }
    else
    {
        Status = vboxVdmaGgDmaBltPerform(pDevExt, &pBlt->SrcAlloc.pAlloc->AllocData, &pBlt->SrcRect,
                &pBlt->DstAlloc.pAlloc->AllocData, &pBlt->DstRects.ContextRect);
        AssertNtStatusSuccess(Status);
        if (Status != STATUS_SUCCESS)
            return Status;
    }

    return Status;
}

static NTSTATUS vboxVdmaProcessBltCmd(PVBOXMP_DEVEXT pDevExt, VBOXWDDM_CONTEXT *pContext, VBOXWDDM_DMA_PRIVATEDATA_BLT *pBlt)
{
    RT_NOREF(pContext);
    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_ALLOCATION pDstAlloc = pBlt->Blt.DstAlloc.pAlloc;
//    PVBOXWDDM_ALLOCATION pSrcAlloc = pBlt->Blt.SrcAlloc.pAlloc;
    {
        /* the allocations contain a real data in VRAM, do blitting */
        vboxVdmaGgDmaBlt(pDevExt, &pBlt->Blt);

        if (pDstAlloc->bAssigned && pDstAlloc->bVisible)
        {
            /* Only for visible framebuffer allocations. */
            VBOXWDDM_SOURCE *pSource = &pDevExt->aSources[pDstAlloc->AllocData.SurfDesc.VidPnSourceId];
            /* Assert but otherwise ignore wrong allocations. */
            AssertReturn(pDstAlloc->AllocData.SurfDesc.VidPnSourceId < VBOX_VIDEO_MAX_SCREENS, STATUS_SUCCESS);
            AssertReturn(pSource->pPrimaryAllocation == pDstAlloc, STATUS_SUCCESS);
            vboxVdmaBltDirtyRectsUpdate(pDevExt, pSource, pBlt->Blt.DstRects.UpdateRects.cRects, pBlt->Blt.DstRects.UpdateRects.aRects);
        }
    }
    return Status;
}

static NTSTATUS vboxVdmaProcessFlipCmd(PVBOXMP_DEVEXT pDevExt, VBOXWDDM_CONTEXT *pContext, VBOXWDDM_DMA_PRIVATEDATA_FLIP *pFlip)
{
    RT_NOREF(pContext);
    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_ALLOCATION pAlloc = pFlip->Flip.Alloc.pAlloc;
    VBOXWDDM_SOURCE *pSource = &pDevExt->aSources[pAlloc->AllocData.SurfDesc.VidPnSourceId];
    vboxWddmAssignPrimary(pSource, pAlloc, pAlloc->AllocData.SurfDesc.VidPnSourceId);
    {
        WARN(("unexpected flip request"));
    }

    return Status;
}

static NTSTATUS vboxVdmaProcessClrFillCmd(PVBOXMP_DEVEXT pDevExt, VBOXWDDM_CONTEXT *pContext, VBOXWDDM_DMA_PRIVATEDATA_CLRFILL *pCF)
{
    RT_NOREF(pContext);
    NTSTATUS Status = STATUS_SUCCESS;
//    PVBOXWDDM_ALLOCATION pAlloc = pCF->ClrFill.Alloc.pAlloc;
    {
        Status = vboxVdmaGgDmaColorFill(pDevExt, &pCF->ClrFill);
        if (!NT_SUCCESS(Status))
            WARN(("vboxVdmaGgDmaColorFill failed Status 0x%x", Status));
    }

    return Status;
}

static void vboxWddmPatchLocationInit(D3DDDI_PATCHLOCATIONLIST *pPatchLocationListOut, UINT idx, UINT offPatch)
{
    memset(pPatchLocationListOut, 0, sizeof (*pPatchLocationListOut));
    pPatchLocationListOut->AllocationIndex = idx;
    pPatchLocationListOut->PatchOffset = offPatch;
}

static void vboxWddmPopulateDmaAllocInfo(PVBOXWDDM_DMA_ALLOCINFO pInfo, PVBOXWDDM_ALLOCATION pAlloc, DXGK_ALLOCATIONLIST *pDmaAlloc)
{
    pInfo->pAlloc = pAlloc;
    if (pDmaAlloc->SegmentId)
    {
        pInfo->offAlloc = (VBOXVIDEOOFFSET)pDmaAlloc->PhysicalAddress.QuadPart;
        pInfo->segmentIdAlloc = pDmaAlloc->SegmentId;
    }
    else
        pInfo->segmentIdAlloc = 0;
    pInfo->srcId = pAlloc->AllocData.SurfDesc.VidPnSourceId;
}

/*
 *
 * DxgkDdi
 *
 */

NTSTATUS
APIENTRY
DxgkDdiBuildPagingBufferLegacy(
    CONST HANDLE  hAdapter,
    DXGKARG_BUILDPAGINGBUFFER*  pBuildPagingBuffer)
{
    /* DxgkDdiBuildPagingBuffer should be made pageable. */
    PAGED_CODE();

    vboxVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    RT_NOREF(hAdapter);
//    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    LOGF(("ENTER, context(0x%x)", hAdapter));

    uint32_t cbCmdDma = 0;

    /** @todo */
    switch (pBuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
            cbCmdDma = VBOXWDDM_DUMMY_DMABUFFER_SIZE;
#ifdef VBOX_WITH_VDMA
            PVBOXWDDM_ALLOCATION pAlloc = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->Transfer.hAllocation;
            Assert(pAlloc);
            if (pAlloc
                    && !pAlloc->fRcFlags.Overlay /* overlay surfaces actually contain a valid data */
                    && pAlloc->enmType != VBOXWDDM_ALLOC_TYPE_STD_SHADOWSURFACE  /* shadow primary - also */
                    && pAlloc->enmType != VBOXWDDM_ALLOC_TYPE_UMD_HGSMI_BUFFER /* hgsmi buffer - also */
                    )
            {
                /* we do not care about the others for now */
                Status = STATUS_SUCCESS;
                break;
            }
            UINT cbCmd = VBOXVDMACMD_SIZE(VBOXVDMACMD_DMA_BPB_TRANSFER);
            VBOXVDMACBUF_DR RT_UNTRUSTED_VOLATILE_HOST *pDr = vboxVdmaCBufDrCreate(&pDevExt->u.primary.Vdma, cbCmd);
            Assert(pDr);
            if (pDr)
            {
                SIZE_T cbTransfered = 0;
                SIZE_T cbTransferSize = pBuildPagingBuffer->Transfer.TransferSize;
                VBOXVDMACMD RT_UNTRUSTED_VOLATILE_HOST *pHdr = VBOXVDMACBUF_DR_TAIL(pDr, VBOXVDMACMD);
                do
                {
                    // vboxVdmaCBufDrCreate zero initializes the pDr
                    pDr->fFlags = VBOXVDMACBUF_FLAG_BUF_FOLLOWS_DR;
                    pDr->cbBuf = cbCmd;
                    pDr->rc = VERR_NOT_IMPLEMENTED;

                    pHdr->enmType = VBOXVDMACMD_TYPE_DMA_BPB_TRANSFER;
                    pHdr->u32CmdSpecific = 0;
                    VBOXVDMACMD_DMA_BPB_TRANSFER RT_UNTRUSTED_VOLATILE_HOST *pBody
                        = VBOXVDMACMD_BODY(pHdr, VBOXVDMACMD_DMA_BPB_TRANSFER);
//                    pBody->cbTransferSize = (uint32_t)pBuildPagingBuffer->Transfer.TransferSize;
                    pBody->fFlags = 0;
                    SIZE_T cSrcPages = (cbTransferSize + 0xfff ) >> 12;
                    SIZE_T cDstPages = cSrcPages;

                    if (pBuildPagingBuffer->Transfer.Source.SegmentId)
                    {
                        uint64_t off = pBuildPagingBuffer->Transfer.Source.SegmentAddress.QuadPart;
                        off += pBuildPagingBuffer->Transfer.TransferOffset + cbTransfered;
                        pBody->Src.offVramBuf = off;
                        pBody->fFlags |= VBOXVDMACMD_DMA_BPB_TRANSFER_F_SRC_VRAMOFFSET;
                    }
                    else
                    {
                        UINT index = pBuildPagingBuffer->Transfer.MdlOffset + (UINT)(cbTransfered>>12);
                        pBody->Src.phBuf = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Source.pMdl)[index] << PAGE_SHIFT;
                        PFN_NUMBER num = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Source.pMdl)[index];
                        cSrcPages = 1;
                        for (UINT i = 1; i < ((cbTransferSize - cbTransfered + 0xfff) >> 12); ++i)
                        {
                            PFN_NUMBER cur = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Source.pMdl)[index+i];
                            if(cur != ++num)
                            {
                                cSrcPages+= i-1;
                                break;
                            }
                        }
                    }

                    if (pBuildPagingBuffer->Transfer.Destination.SegmentId)
                    {
                        uint64_t off = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.QuadPart;
                        off += pBuildPagingBuffer->Transfer.TransferOffset;
                        pBody->Dst.offVramBuf = off + cbTransfered;
                        pBody->fFlags |= VBOXVDMACMD_DMA_BPB_TRANSFER_F_DST_VRAMOFFSET;
                    }
                    else
                    {
                        UINT index = pBuildPagingBuffer->Transfer.MdlOffset + (UINT)(cbTransfered>>12);
                        pBody->Dst.phBuf = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Destination.pMdl)[index] << PAGE_SHIFT;
                        PFN_NUMBER num = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Destination.pMdl)[index];
                        cDstPages = 1;
                        for (UINT i = 1; i < ((cbTransferSize - cbTransfered + 0xfff) >> 12); ++i)
                        {
                            PFN_NUMBER cur = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Destination.pMdl)[index+i];
                            if(cur != ++num)
                            {
                                cDstPages+= i-1;
                                break;
                            }
                        }
                    }

                    SIZE_T cbCurTransfer;
                    cbCurTransfer = RT_MIN(cbTransferSize - cbTransfered, (SIZE_T)cSrcPages << PAGE_SHIFT);
                    cbCurTransfer = RT_MIN(cbCurTransfer, (SIZE_T)cDstPages << PAGE_SHIFT);

                    pBody->cbTransferSize = (UINT)cbCurTransfer;
                    Assert(!(cbCurTransfer & 0xfff));

                    int rc = vboxVdmaCBufDrSubmitSynch(pDevExt, &pDevExt->u.primary.Vdma, pDr);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        Status = STATUS_SUCCESS;
                        cbTransfered += cbCurTransfer;
                    }
                    else
                        Status = STATUS_UNSUCCESSFUL;
                } while (cbTransfered < cbTransferSize);
                Assert(cbTransfered == cbTransferSize);
                vboxVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);
            }
            else
            {
                /** @todo try flushing.. */
                LOGREL(("vboxVdmaCBufDrCreate returned NULL"));
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
#endif /* #ifdef VBOX_WITH_VDMA */
            break;
        }
        case DXGK_OPERATION_FILL:
        {
            cbCmdDma = VBOXWDDM_DUMMY_DMABUFFER_SIZE;
            Assert(pBuildPagingBuffer->Fill.FillPattern == 0);
            /*PVBOXWDDM_ALLOCATION pAlloc = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->Fill.hAllocation; - unused. Incomplete code? */
//            pBuildPagingBuffer->pDmaBuffer = (uint8_t*)pBuildPagingBuffer->pDmaBuffer + VBOXVDMACMD_SIZE(VBOXVDMACMD_DMA_BPB_FILL);
            break;
        }
        case DXGK_OPERATION_DISCARD_CONTENT:
        {
//            AssertBreakpoint();
            break;
        }
        default:
        {
            WARN(("unsupported op (%d)", pBuildPagingBuffer->Operation));
            break;
        }
    }

    if (cbCmdDma)
    {
        pBuildPagingBuffer->pDmaBuffer = ((uint8_t*)pBuildPagingBuffer->pDmaBuffer) + cbCmdDma;
    }

    LOGF(("LEAVE, context(0x%x)", hAdapter));

    return Status;

}

/**
 * DxgkDdiPresent
 */
NTSTATUS
APIENTRY
DxgkDdiPresentLegacy(
    CONST HANDLE  hContext,
    DXGKARG_PRESENT  *pPresent)
{
    RT_NOREF(hContext);
    PAGED_CODE();

//    LOGF(("ENTER, hContext(0x%x)", hContext));

    vboxVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
#ifdef VBOX_STRICT
    PVBOXWDDM_CONTEXT pContext = (PVBOXWDDM_CONTEXT)hContext;
    PVBOXWDDM_DEVICE pDevice = pContext->pDevice;
    PVBOXMP_DEVEXT pDevExt = pDevice->pAdapter;
#endif

    Assert(pPresent->DmaBufferPrivateDataSize >= sizeof (VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR));
    if (pPresent->DmaBufferPrivateDataSize < sizeof (VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR))
    {
        LOGREL(("Present->DmaBufferPrivateDataSize(%d) < sizeof VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR (%d)", pPresent->DmaBufferPrivateDataSize , sizeof (VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR)));
        /** @todo can this actually happen? what status tu return? */
        return STATUS_INVALID_PARAMETER;
    }

    PVBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR pPrivateData = (PVBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR)pPresent->pDmaBufferPrivateData;
    pPrivateData->BaseHdr.fFlags.Value = 0;
    /*uint32_t cContexts2D = ASMAtomicReadU32(&pDevExt->cContexts2D); - unused */

    if (pPresent->Flags.Blt)
    {
        Assert(pPresent->Flags.Value == 1); /* only Blt is set, we do not support anything else for now */
        DXGK_ALLOCATIONLIST *pSrc =  &pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX];
        DXGK_ALLOCATIONLIST *pDst =  &pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX];
        PVBOXWDDM_ALLOCATION pSrcAlloc = vboxWddmGetAllocationFromAllocList(pSrc);
        if (!pSrcAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get Src Allocation info for hDeviceSpecificAllocation(0x%x)",pSrc->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }

        PVBOXWDDM_ALLOCATION pDstAlloc = vboxWddmGetAllocationFromAllocList(pDst);
        if (!pDstAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get Dst Allocation info for hDeviceSpecificAllocation(0x%x)",pDst->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }


        UINT cbCmd = pPresent->DmaBufferPrivateDataSize;
        pPrivateData->BaseHdr.enmCmd = VBOXVDMACMD_TYPE_DMA_PRESENT_BLT;

        PVBOXWDDM_DMA_PRIVATEDATA_BLT pBlt = (PVBOXWDDM_DMA_PRIVATEDATA_BLT)pPrivateData;

        vboxWddmPopulateDmaAllocInfo(&pBlt->Blt.SrcAlloc, pSrcAlloc, pSrc);
        vboxWddmPopulateDmaAllocInfo(&pBlt->Blt.DstAlloc, pDstAlloc, pDst);

        ASSERT_WARN(!pSrcAlloc->fRcFlags.SharedResource, ("Shared Allocatoin used in Present!"));

        pBlt->Blt.SrcRect = pPresent->SrcRect;
        pBlt->Blt.DstRects.ContextRect = pPresent->DstRect;
        pBlt->Blt.DstRects.UpdateRects.cRects = 0;
        UINT cbHead = RT_UOFFSETOF(VBOXWDDM_DMA_PRIVATEDATA_BLT, Blt.DstRects.UpdateRects.aRects[0]);
        Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);
        UINT cbRects = (pPresent->SubRectCnt - pPresent->MultipassOffset) * sizeof (RECT);
        pPresent->pDmaBuffer = ((uint8_t*)pPresent->pDmaBuffer) + VBOXWDDM_DUMMY_DMABUFFER_SIZE;
        Assert(pPresent->DmaSize >= VBOXWDDM_DUMMY_DMABUFFER_SIZE);
        cbCmd -= cbHead;
        Assert(cbCmd < UINT32_MAX/2);
        Assert(cbCmd > sizeof (RECT));
        if (cbCmd >= cbRects)
        {
            cbCmd -= cbRects;
            memcpy(&pBlt->Blt.DstRects.UpdateRects.aRects[0], &pPresent->pDstSubRects[pPresent->MultipassOffset], cbRects);
            pBlt->Blt.DstRects.UpdateRects.cRects += cbRects/sizeof (RECT);

            pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbHead + cbRects;
        }
        else
        {
            UINT cbFitingRects = (cbCmd/sizeof (RECT)) * sizeof (RECT);
            Assert(cbFitingRects);
            memcpy(&pBlt->Blt.DstRects.UpdateRects.aRects[0], &pPresent->pDstSubRects[pPresent->MultipassOffset], cbFitingRects);
            cbCmd -= cbFitingRects;
            pPresent->MultipassOffset += cbFitingRects/sizeof (RECT);
            pBlt->Blt.DstRects.UpdateRects.cRects += cbFitingRects/sizeof (RECT);
            Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);

            pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbHead + cbFitingRects;
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }

        memset(pPresent->pPatchLocationListOut, 0, 2*sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = 0;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
        ++pPresent->pPatchLocationListOut;
        pPresent->pPatchLocationListOut->PatchOffset = 4;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
        ++pPresent->pPatchLocationListOut;
    }
    else if (pPresent->Flags.Flip)
    {
        Assert(pPresent->Flags.Value == 4); /* only Blt is set, we do not support anything else for now */
        //Assert(pContext->enmType == VBOXWDDM_CONTEXT_TYPE_CUSTOM_3D);
        DXGK_ALLOCATIONLIST *pSrc =  &pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX];
        PVBOXWDDM_ALLOCATION pSrcAlloc = vboxWddmGetAllocationFromAllocList(pSrc);

        if (!pSrcAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get pSrc Allocation info for hDeviceSpecificAllocation(0x%x)",pSrc->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }

        Assert(pDevExt->cContexts3D);
        pPrivateData->BaseHdr.enmCmd = VBOXVDMACMD_TYPE_DMA_PRESENT_FLIP;
        PVBOXWDDM_DMA_PRIVATEDATA_FLIP pFlip = (PVBOXWDDM_DMA_PRIVATEDATA_FLIP)pPrivateData;

        vboxWddmPopulateDmaAllocInfo(&pFlip->Flip.Alloc, pSrcAlloc, pSrc);

        UINT cbCmd = sizeof (VBOXWDDM_DMA_PRIVATEDATA_FLIP);
        pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbCmd;
        pPresent->pDmaBuffer = ((uint8_t*)pPresent->pDmaBuffer) + VBOXWDDM_DUMMY_DMABUFFER_SIZE;
        Assert(pPresent->DmaSize >= VBOXWDDM_DUMMY_DMABUFFER_SIZE);

        memset(pPresent->pPatchLocationListOut, 0, sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = 0;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
        ++pPresent->pPatchLocationListOut;
    }
    else if (pPresent->Flags.ColorFill)
    {
        //Assert(pContext->enmType == VBOXWDDM_CONTEXT_TYPE_CUSTOM_2D);
        Assert(pPresent->Flags.Value == 2); /* only ColorFill is set, we do not support anything else for now */
        DXGK_ALLOCATIONLIST *pDst =  &pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX];
        PVBOXWDDM_ALLOCATION pDstAlloc = vboxWddmGetAllocationFromAllocList(pDst);
        if (!pDstAlloc)
        {

            /* this should not happen actually */
            WARN(("failed to get pDst Allocation info for hDeviceSpecificAllocation(0x%x)",pDst->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }

        UINT cbCmd = pPresent->DmaBufferPrivateDataSize;
        pPrivateData->BaseHdr.enmCmd = VBOXVDMACMD_TYPE_DMA_PRESENT_CLRFILL;
        PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL pCF = (PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL)pPrivateData;

        vboxWddmPopulateDmaAllocInfo(&pCF->ClrFill.Alloc, pDstAlloc, pDst);

        pCF->ClrFill.Color = pPresent->Color;
        pCF->ClrFill.Rects.cRects = 0;
        UINT cbHead = RT_UOFFSETOF(VBOXWDDM_DMA_PRIVATEDATA_CLRFILL, ClrFill.Rects.aRects[0]);
        Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);
        UINT cbRects = (pPresent->SubRectCnt - pPresent->MultipassOffset) * sizeof (RECT);
        pPresent->pDmaBuffer = ((uint8_t*)pPresent->pDmaBuffer) + VBOXWDDM_DUMMY_DMABUFFER_SIZE;
        Assert(pPresent->DmaSize >= VBOXWDDM_DUMMY_DMABUFFER_SIZE);
        cbCmd -= cbHead;
        Assert(cbCmd < UINT32_MAX/2);
        Assert(cbCmd > sizeof (RECT));
        if (cbCmd >= cbRects)
        {
            cbCmd -= cbRects;
            memcpy(&pCF->ClrFill.Rects.aRects[pPresent->MultipassOffset], pPresent->pDstSubRects, cbRects);
            pCF->ClrFill.Rects.cRects += cbRects/sizeof (RECT);

            pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbHead + cbRects;
        }
        else
        {
            UINT cbFitingRects = (cbCmd/sizeof (RECT)) * sizeof (RECT);
            Assert(cbFitingRects);
            memcpy(&pCF->ClrFill.Rects.aRects[0], pPresent->pDstSubRects, cbFitingRects);
            cbCmd -= cbFitingRects;
            pPresent->MultipassOffset += cbFitingRects/sizeof (RECT);
            pCF->ClrFill.Rects.cRects += cbFitingRects/sizeof (RECT);
            Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);

            pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbHead + cbFitingRects;
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }

        memset(pPresent->pPatchLocationListOut, 0, sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = 0;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
        ++pPresent->pPatchLocationListOut;
    }
    else
    {
        WARN(("cmd NOT IMPLEMENTED!! Flags(0x%x)", pPresent->Flags.Value));
        Status = STATUS_NOT_SUPPORTED;
    }

done:
//    LOGF(("LEAVE, hContext(0x%x), Status(0x%x)", hContext, Status));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiRenderLegacy(
    CONST HANDLE  hContext,
    DXGKARG_RENDER  *pRender)
{
    RT_NOREF(hContext);
//    LOGF(("ENTER, hContext(0x%x)", hContext));

    if (pRender->DmaBufferPrivateDataSize < sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        WARN(("Present->DmaBufferPrivateDataSize(%d) < sizeof VBOXWDDM_DMA_PRIVATEDATA_BASEHDR (%d)",
                pRender->DmaBufferPrivateDataSize , sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->CommandLength < sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        WARN(("Present->DmaBufferPrivateDataSize(%d) < sizeof VBOXWDDM_DMA_PRIVATEDATA_BASEHDR (%d)",
                pRender->DmaBufferPrivateDataSize , sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->DmaSize < pRender->CommandLength)
    {
        WARN(("pRender->DmaSize(%d) < pRender->CommandLength(%d)",
                pRender->DmaSize, pRender->CommandLength));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->PatchLocationListOutSize < pRender->PatchLocationListInSize)
    {
        WARN(("pRender->PatchLocationListOutSize(%d) < pRender->PatchLocationListInSize(%d)",
                pRender->PatchLocationListOutSize, pRender->PatchLocationListInSize));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->AllocationListSize != pRender->PatchLocationListInSize)
    {
        WARN(("pRender->AllocationListSize(%d) != pRender->PatchLocationListInSize(%d)",
                pRender->AllocationListSize, pRender->PatchLocationListInSize));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;

    __try
    {
        PVBOXWDDM_DMA_PRIVATEDATA_BASEHDR pInputHdr = (PVBOXWDDM_DMA_PRIVATEDATA_BASEHDR)pRender->pCommand;
        switch (pInputHdr->enmCmd)
        {
            case VBOXVDMACMD_TYPE_DMA_NOP:
            {
                PVBOXWDDM_DMA_PRIVATEDATA_BASEHDR pPrivateData = (PVBOXWDDM_DMA_PRIVATEDATA_BASEHDR)pRender->pDmaBufferPrivateData;
                pPrivateData->enmCmd = VBOXVDMACMD_TYPE_DMA_NOP;
                pRender->pDmaBufferPrivateData = (uint8_t*)pRender->pDmaBufferPrivateData + sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR);
                pRender->pDmaBuffer = ((uint8_t*)pRender->pDmaBuffer) + pRender->CommandLength;
                for (UINT i = 0; i < pRender->PatchLocationListInSize; ++i)
                {
                    UINT offPatch = i * 4;
                    if (offPatch + 4 > pRender->CommandLength)
                    {
                        WARN(("wrong offPatch"));
                        return STATUS_INVALID_PARAMETER;
                    }
                    if (offPatch != pRender->pPatchLocationListIn[i].PatchOffset)
                    {
                        WARN(("wrong PatchOffset"));
                        return STATUS_INVALID_PARAMETER;
                    }
                    if (i != pRender->pPatchLocationListIn[i].AllocationIndex)
                    {
                        WARN(("wrong AllocationIndex"));
                        return STATUS_INVALID_PARAMETER;
                    }
                    vboxWddmPatchLocationInit(&pRender->pPatchLocationListOut[i], i, offPatch);
                }
                break;
            }
            default:
            {
                WARN(("unsupported command %d", pInputHdr->enmCmd));
                return STATUS_INVALID_PARAMETER;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
        WARN(("invalid parameter"));
    }
//    LOGF(("LEAVE, hContext(0x%x)", hContext));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiPatchLegacy(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_PATCH*  pPatch)
{
    RT_NOREF(hAdapter);
    /* DxgkDdiPatch should be made pageable. */
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    /* Value == 2 is Present
     * Value == 4 is RedirectedPresent
     * we do not expect any other flags to be set here */
//    Assert(pPatch->Flags.Value == 2 || pPatch->Flags.Value == 4);
    if (pPatch->DmaBufferPrivateDataSubmissionEndOffset - pPatch->DmaBufferPrivateDataSubmissionStartOffset >= sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        Assert(pPatch->DmaBufferPrivateDataSize >= sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR));
        VBOXWDDM_DMA_PRIVATEDATA_BASEHDR *pPrivateDataBase = (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR*)((uint8_t*)pPatch->pDmaBufferPrivateData + pPatch->DmaBufferPrivateDataSubmissionStartOffset);
        switch (pPrivateDataBase->enmCmd)
        {
            case VBOXVDMACMD_TYPE_DMA_PRESENT_BLT:
            {
                PVBOXWDDM_DMA_PRIVATEDATA_BLT pBlt = (PVBOXWDDM_DMA_PRIVATEDATA_BLT)pPrivateDataBase;
                Assert(pPatch->PatchLocationListSubmissionLength == 2);
                const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_SOURCE_INDEX);
                Assert(pPatchList->PatchOffset == 0);
                const DXGK_ALLOCATIONLIST *pSrcAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pSrcAllocationList->SegmentId);
                pBlt->Blt.SrcAlloc.segmentIdAlloc = pSrcAllocationList->SegmentId;
                pBlt->Blt.SrcAlloc.offAlloc = (VBOXVIDEOOFFSET)pSrcAllocationList->PhysicalAddress.QuadPart;

                pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart + 1];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_DESTINATION_INDEX);
                Assert(pPatchList->PatchOffset == 4);
                const DXGK_ALLOCATIONLIST *pDstAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pDstAllocationList->SegmentId);
                pBlt->Blt.DstAlloc.segmentIdAlloc = pDstAllocationList->SegmentId;
                pBlt->Blt.DstAlloc.offAlloc = (VBOXVIDEOOFFSET)pDstAllocationList->PhysicalAddress.QuadPart;
                break;
            }
            case VBOXVDMACMD_TYPE_DMA_PRESENT_FLIP:
            {
                PVBOXWDDM_DMA_PRIVATEDATA_FLIP pFlip = (PVBOXWDDM_DMA_PRIVATEDATA_FLIP)pPrivateDataBase;
                Assert(pPatch->PatchLocationListSubmissionLength == 1);
                const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_SOURCE_INDEX);
                Assert(pPatchList->PatchOffset == 0);
                const DXGK_ALLOCATIONLIST *pSrcAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pSrcAllocationList->SegmentId);
                pFlip->Flip.Alloc.segmentIdAlloc = pSrcAllocationList->SegmentId;
                pFlip->Flip.Alloc.offAlloc = (VBOXVIDEOOFFSET)pSrcAllocationList->PhysicalAddress.QuadPart;
                break;
            }
            case VBOXVDMACMD_TYPE_DMA_PRESENT_CLRFILL:
            {
                PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL pCF = (PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL)pPrivateDataBase;
                Assert(pPatch->PatchLocationListSubmissionLength == 1);
                const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_DESTINATION_INDEX);
                Assert(pPatchList->PatchOffset == 0);
                const DXGK_ALLOCATIONLIST *pDstAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pDstAllocationList->SegmentId);
                pCF->ClrFill.Alloc.segmentIdAlloc = pDstAllocationList->SegmentId;
                pCF->ClrFill.Alloc.offAlloc = (VBOXVIDEOOFFSET)pDstAllocationList->PhysicalAddress.QuadPart;
                break;
            }
            case VBOXVDMACMD_TYPE_DMA_NOP:
                break;
            case VBOXVDMACMD_TYPE_CHROMIUM_CMD:
            {
                uint8_t * pPrivateBuf = (uint8_t*)pPrivateDataBase;
                for (UINT i = pPatch->PatchLocationListSubmissionStart; i < pPatch->PatchLocationListSubmissionLength; ++i)
                {
                    const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[i];
                    Assert(pPatchList->AllocationIndex < pPatch->AllocationListSize);
                    const DXGK_ALLOCATIONLIST *pAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                    Assert(pAllocationList->SegmentId);
                    if (pAllocationList->SegmentId)
                    {
                        DXGK_ALLOCATIONLIST *pAllocation2Patch = (DXGK_ALLOCATIONLIST*)(pPrivateBuf + pPatchList->PatchOffset);
                        pAllocation2Patch->SegmentId = pAllocationList->SegmentId;
                        pAllocation2Patch->PhysicalAddress.QuadPart = pAllocationList->PhysicalAddress.QuadPart + pPatchList->AllocationOffset;
                        Assert(!(pAllocationList->PhysicalAddress.QuadPart & 0xfffUL)); /* <- just a check to ensure allocation offset does not go here */
                    }
                }
                break;
            }
            default:
            {
                AssertBreakpoint();
                uint8_t *pBuf = ((uint8_t *)pPatch->pDmaBuffer) + pPatch->DmaBufferSubmissionStartOffset;
                for (UINT i = pPatch->PatchLocationListSubmissionStart; i < pPatch->PatchLocationListSubmissionLength; ++i)
                {
                    const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[i];
                    Assert(pPatchList->AllocationIndex < pPatch->AllocationListSize);
                    const DXGK_ALLOCATIONLIST *pAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                    if (pAllocationList->SegmentId)
                    {
                        Assert(pPatchList->PatchOffset < (pPatch->DmaBufferSubmissionEndOffset - pPatch->DmaBufferSubmissionStartOffset));
                        *((VBOXVIDEOOFFSET*)(pBuf+pPatchList->PatchOffset)) = (VBOXVIDEOOFFSET)pAllocationList->PhysicalAddress.QuadPart;
                    }
                    else
                    {
                        /* sanity */
                        if (pPatch->Flags.Value == 2 || pPatch->Flags.Value == 4)
                            Assert(i == 0);
                    }
                }
                break;
            }
        }
    }
    else if (pPatch->DmaBufferPrivateDataSubmissionEndOffset == pPatch->DmaBufferPrivateDataSubmissionStartOffset)
    {
        /* this is a NOP, just return success */
//        LOG(("null data size, treating as NOP"));
        return STATUS_SUCCESS;
    }
    else
    {
        WARN(("DmaBufferPrivateDataSubmissionEndOffset (%d) - DmaBufferPrivateDataSubmissionStartOffset (%d) < sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR) (%d)",
                pPatch->DmaBufferPrivateDataSubmissionEndOffset,
                pPatch->DmaBufferPrivateDataSubmissionStartOffset,
                sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }

    LOGF(("LEAVE, context(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiSubmitCommandLegacy(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SUBMITCOMMAND*  pSubmitCommand)
{
    /* DxgkDdiSubmitCommand runs at dispatch, should not be pageable. */
    NTSTATUS Status = STATUS_SUCCESS;

//    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    PVBOXWDDM_CONTEXT pContext = (PVBOXWDDM_CONTEXT)pSubmitCommand->hContext;
    PVBOXWDDM_DMA_PRIVATEDATA_BASEHDR pPrivateDataBase = NULL;
    VBOXVDMACMD_TYPE enmCmd = VBOXVDMACMD_TYPE_UNDEFINED;
    Assert(pContext);
    Assert(pContext->pDevice);
    Assert(pContext->pDevice->pAdapter == pDevExt);
    Assert(!pSubmitCommand->DmaBufferSegmentId);

    /* the DMA command buffer is located in system RAM, the host will need to pick it from there */
    //BufInfo.fFlags = 0; /* see VBOXVDMACBUF_FLAG_xx */
    if (pSubmitCommand->DmaBufferPrivateDataSubmissionEndOffset - pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset >= sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        pPrivateDataBase = (PVBOXWDDM_DMA_PRIVATEDATA_BASEHDR)((uint8_t*)pSubmitCommand->pDmaBufferPrivateData + pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset);
        Assert(pPrivateDataBase);
        enmCmd = pPrivateDataBase->enmCmd;
    }
    else if (pSubmitCommand->DmaBufferPrivateDataSubmissionEndOffset == pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset)
    {
        enmCmd = VBOXVDMACMD_TYPE_DMA_NOP;
    }
    else
    {
        WARN(("DmaBufferPrivateDataSubmissionEndOffset (%d) - DmaBufferPrivateDataSubmissionStartOffset (%d) < sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR) (%d)",
                pSubmitCommand->DmaBufferPrivateDataSubmissionEndOffset,
                pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset,
                sizeof (VBOXWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }

    switch (enmCmd)
    {
        case VBOXVDMACMD_TYPE_DMA_PRESENT_BLT:
        {
            VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR *pPrivateData = (VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR*)pPrivateDataBase;
            PVBOXWDDM_DMA_PRIVATEDATA_BLT pBlt = (PVBOXWDDM_DMA_PRIVATEDATA_BLT)pPrivateData;
            PVBOXWDDM_ALLOCATION pDstAlloc = pBlt->Blt.DstAlloc.pAlloc;
            PVBOXWDDM_ALLOCATION pSrcAlloc = pBlt->Blt.SrcAlloc.pAlloc;
            BOOLEAN fSrcChanged;
            BOOLEAN fDstChanged;

            fDstChanged = vboxWddmAddrSetVram(&pDstAlloc->AllocData.Addr, pBlt->Blt.DstAlloc.segmentIdAlloc, pBlt->Blt.DstAlloc.offAlloc);
            fSrcChanged = vboxWddmAddrSetVram(&pSrcAlloc->AllocData.Addr, pBlt->Blt.SrcAlloc.segmentIdAlloc, pBlt->Blt.SrcAlloc.offAlloc);

            if (VBOXWDDM_IS_FB_ALLOCATION(pDevExt, pDstAlloc))
            {
                Assert(pDstAlloc->AllocData.SurfDesc.VidPnSourceId < VBOX_VIDEO_MAX_SCREENS);
            }

            Status = vboxVdmaProcessBltCmd(pDevExt, pContext, pBlt);
            if (!NT_SUCCESS(Status))
                WARN(("vboxVdmaProcessBltCmd failed, Status 0x%x", Status));

            Status = vboxVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId,
                    NT_SUCCESS(Status) ? DXGK_INTERRUPT_DMA_COMPLETED : DXGK_INTERRUPT_DMA_FAULTED);
            break;
        }
        case VBOXVDMACMD_TYPE_DMA_PRESENT_FLIP:
        {
            VBOXWDDM_DMA_PRIVATEDATA_FLIP *pFlip = (VBOXWDDM_DMA_PRIVATEDATA_FLIP*)pPrivateDataBase;
            PVBOXWDDM_ALLOCATION pAlloc = pFlip->Flip.Alloc.pAlloc;
            VBOXWDDM_SOURCE *pSource = &pDevExt->aSources[pAlloc->AllocData.SurfDesc.VidPnSourceId];
            vboxWddmAddrSetVram(&pAlloc->AllocData.Addr, pFlip->Flip.Alloc.segmentIdAlloc, pFlip->Flip.Alloc.offAlloc);
            vboxWddmAssignPrimary(pSource, pAlloc, pAlloc->AllocData.SurfDesc.VidPnSourceId);
            vboxWddmGhDisplayCheckSetInfoFromSource(pDevExt, pSource);

            Status = vboxVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId,
                    NT_SUCCESS(Status) ? DXGK_INTERRUPT_DMA_COMPLETED : DXGK_INTERRUPT_DMA_FAULTED);
            break;
        }
        case VBOXVDMACMD_TYPE_DMA_PRESENT_CLRFILL:
        {
            PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL pCF = (PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL)pPrivateDataBase;
            vboxWddmAddrSetVram(&pCF->ClrFill.Alloc.pAlloc->AllocData.Addr, pCF->ClrFill.Alloc.segmentIdAlloc, pCF->ClrFill.Alloc.offAlloc);

            Status = vboxVdmaProcessClrFillCmd(pDevExt, pContext, pCF);
            if (!NT_SUCCESS(Status))
                WARN(("vboxVdmaProcessClrFillCmd failed, Status 0x%x", Status));

            Status = vboxVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId,
                    NT_SUCCESS(Status) ? DXGK_INTERRUPT_DMA_COMPLETED : DXGK_INTERRUPT_DMA_FAULTED);
            break;
        }
        case VBOXVDMACMD_TYPE_DMA_NOP:
        {
            Status = vboxVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId, DXGK_INTERRUPT_DMA_COMPLETED);
            AssertNtStatusSuccess(Status);
            break;
        }
        default:
        {
            WARN(("unexpected command %d", enmCmd));
            break;
        }
    }
//    LOGF(("LEAVE, context(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiPreemptCommandLegacy(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_PREEMPTCOMMAND*  pPreemptCommand)
{
    RT_NOREF(hAdapter, pPreemptCommand);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertFailed();
    /** @todo fixme: implement */

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

typedef struct VBOXWDDM_QUERYCURFENCE_CB
{
    PVBOXMP_DEVEXT pDevExt;
    ULONG MessageNumber;
    ULONG uLastCompletedCmdFenceId;
} VBOXWDDM_QUERYCURFENCE_CB, *PVBOXWDDM_QUERYCURFENCE_CB;

static BOOLEAN vboxWddmQueryCurrentFenceCb(PVOID Context)
{
    PVBOXWDDM_QUERYCURFENCE_CB pdc = (PVBOXWDDM_QUERYCURFENCE_CB)Context;
    PVBOXMP_DEVEXT pDevExt = pdc->pDevExt;
    BOOL bRc = DxgkDdiInterruptRoutineLegacy(pDevExt, pdc->MessageNumber);
    pdc->uLastCompletedCmdFenceId = pDevExt->u.primary.uLastCompletedPagingBufferCmdFenceId;
    return bRc;
}

NTSTATUS
APIENTRY
DxgkDdiQueryCurrentFenceLegacy(
    CONST HANDLE  hAdapter,
    DXGKARG_QUERYCURRENTFENCE*  pCurrentFence)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    vboxVDbgBreakF();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    VBOXWDDM_QUERYCURFENCE_CB context = {0};
    context.pDevExt = pDevExt;
    BOOLEAN bRet;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            vboxWddmQueryCurrentFenceCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        pCurrentFence->CurrentFence = context.uLastCompletedCmdFenceId;
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

BOOLEAN DxgkDdiInterruptRoutineLegacy(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG MessageNumber
    )
{
    RT_NOREF(MessageNumber);
//    LOGF(("ENTER, context(0x%p), msg(0x%x)", MiniportDeviceContext, MessageNumber));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;
    BOOLEAN bOur = FALSE;
    BOOLEAN bNeedDpc = FALSE;
    if (VBoxCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags) /* If HGSMI is enabled at all. */
    {
        VBOXVTLIST CtlList;
        vboxVtListInit(&CtlList);

#ifdef VBOX_WITH_VIDEOHWACCEL
        VBOXVTLIST VhwaCmdList;
        vboxVtListInit(&VhwaCmdList);
#endif

        uint32_t flags = VBoxCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;
        bOur = (flags & HGSMIHOSTFLAGS_IRQ);

        if (bOur)
            VBoxHGSMIClearIrq(&VBoxCommonFromDeviceExt(pDevExt)->hostCtx);

        do
        {
            if (flags & HGSMIHOSTFLAGS_GCOMMAND_COMPLETED)
            {
                /* read the command offset */
                HGSMIOFFSET offCmd = VBVO_PORT_READ_U32(VBoxCommonFromDeviceExt(pDevExt)->guestCtx.port);
                Assert(offCmd != HGSMIOFFSET_VOID);
                if (offCmd != HGSMIOFFSET_VOID)
                {
                    VBOXWDDM_HGSMICMD_TYPE enmType = vboxWddmHgsmiGetCmdTypeFromOffset(pDevExt, offCmd);
                    PVBOXVTLIST pList;
                    PVBOXSHGSMI pHeap;
                    switch (enmType)
                    {
                        case VBOXWDDM_HGSMICMD_TYPE_CTL:
                            pList = &CtlList;
                            pHeap = &VBoxCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx;
                            break;
                        default:
                            AssertBreakpoint();
                            pList = NULL;
                            pHeap = NULL;
                            break;
                    }

                    if (pHeap)
                    {
                        uint16_t chInfo;
                        uint8_t RT_UNTRUSTED_VOLATILE_GUEST *pvCmd =
                            HGSMIBufferDataAndChInfoFromOffset(&pHeap->Heap.area, offCmd, &chInfo);
                        Assert(pvCmd);
                        if (pvCmd)
                        {
                            switch (chInfo)
                            {
#ifdef VBOX_WITH_VIDEOHWACCEL
                                case VBVA_VHWA_CMD:
                                {
                                    vboxVhwaPutList(&VhwaCmdList, (VBOXVHWACMD*)pvCmd);
                                    break;
                                }
#endif /* # ifdef VBOX_WITH_VIDEOHWACCEL */
                                default:
                                    AssertBreakpoint();
                            }
                        }
                    }
                }
            }
            else if (flags & HGSMIHOSTFLAGS_COMMANDS_PENDING)
            {
                AssertBreakpoint();
                /** @todo FIXME: implement !!! */
            }
            else
                break;

            flags = VBoxCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;
        } while (1);

        if (!vboxVtListIsEmpty(&CtlList))
        {
            vboxVtListCat(&pDevExt->CtlList, &CtlList);
            bNeedDpc = TRUE;
        }
#ifdef VBOX_WITH_VIDEOHWACCEL
        if (!vboxVtListIsEmpty(&VhwaCmdList))
        {
            vboxVtListCat(&pDevExt->VhwaCmdList, &VhwaCmdList);
            bNeedDpc = TRUE;
        }
#endif

        if (pDevExt->bNotifyDxDpc)
        {
            bNeedDpc = TRUE;
        }

        if (bOur)
        {
            if (flags & HGSMIHOSTFLAGS_VSYNC)
            {
                Assert(0);
                DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
                for (UINT i = 0; i < (UINT)VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
                    PVBOXWDDM_TARGET pTarget = &pDevExt->aTargets[i];
                    if (pTarget->fConnected)
                    {
                        memset(&notify, 0, sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA));
                        notify.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
                        notify.CrtcVsync.VidPnTargetId = i;
                        pDevExt->u.primary.DxgkInterface.DxgkCbNotifyInterrupt(pDevExt->u.primary.DxgkInterface.DeviceHandle, &notify);
                        bNeedDpc = TRUE;
                    }
                }
            }

            if (pDevExt->bNotifyDxDpc)
            {
                bNeedDpc = TRUE;
            }

#if 0 //def DEBUG_misha
            /* this is not entirely correct since host may concurrently complete some commands and raise a new IRQ while we are here,
             * still this allows to check that the host flags are correctly cleared after the ISR */
            Assert(VBoxCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags);
            uint32_t flags = VBoxCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;
            Assert(flags == 0);
#endif
        }

        if (bNeedDpc)
        {
            pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);
        }
    }

//    LOGF(("LEAVE, context(0x%p), bOur(0x%x)", MiniportDeviceContext, (ULONG)bOur));

    return bOur;
}


typedef struct VBOXWDDM_DPCDATA
{
    VBOXVTLIST CtlList;
#ifdef VBOX_WITH_VIDEOHWACCEL
    VBOXVTLIST VhwaCmdList;
#endif
    LIST_ENTRY CompletedDdiCmdQueue;
    BOOL bNotifyDpc;
} VBOXWDDM_DPCDATA, *PVBOXWDDM_DPCDATA;

typedef struct VBOXWDDM_GETDPCDATA_CONTEXT
{
    PVBOXMP_DEVEXT pDevExt;
    VBOXWDDM_DPCDATA data;
} VBOXWDDM_GETDPCDATA_CONTEXT, *PVBOXWDDM_GETDPCDATA_CONTEXT;

BOOLEAN vboxWddmGetDPCDataCallback(PVOID Context)
{
    PVBOXWDDM_GETDPCDATA_CONTEXT pdc = (PVBOXWDDM_GETDPCDATA_CONTEXT)Context;
    PVBOXMP_DEVEXT pDevExt = pdc->pDevExt;
    vboxVtListDetach2List(&pDevExt->CtlList, &pdc->data.CtlList);
#ifdef VBOX_WITH_VIDEOHWACCEL
    vboxVtListDetach2List(&pDevExt->VhwaCmdList, &pdc->data.VhwaCmdList);
#endif

    pdc->data.bNotifyDpc = pDevExt->bNotifyDxDpc;
    pDevExt->bNotifyDxDpc = FALSE;

    ASMAtomicWriteU32(&pDevExt->fCompletingCommands, 0);

    return TRUE;
}

VOID DxgkDdiDpcRoutineLegacy(
    IN CONST PVOID  MiniportDeviceContext
    )
{
//    LOGF(("ENTER, context(0x%p)", MiniportDeviceContext));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;

    VBOXWDDM_GETDPCDATA_CONTEXT context = {0};
    BOOLEAN bRet;

    context.pDevExt = pDevExt;

    /* get DPC data at IRQL */
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            vboxWddmGetDPCDataCallback,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    AssertNtStatusSuccess(Status); NOREF(Status);

    if (!vboxVtListIsEmpty(&context.data.CtlList))
    {
        int rc = VBoxSHGSMICommandPostprocessCompletion (&VBoxCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, &context.data.CtlList);
        AssertRC(rc);
    }
#ifdef VBOX_WITH_VIDEOHWACCEL
    if (!vboxVtListIsEmpty(&context.data.VhwaCmdList))
    {
        vboxVhwaCompletionListProcess(pDevExt, &context.data.VhwaCmdList);
    }
#endif

//    LOGF(("LEAVE, context(0x%p)", MiniportDeviceContext));
}
