/* $Id: VBoxMPDX.cpp 110488 2025-07-30 18:59:09Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Graphics Driver - Direct3D (DX) driver function.
 */

/*
 * Copyright (C) 2022-2025 Oracle and/or its affiliates.
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

#define GALOG_GROUP GALOG_GROUP_DXGK

#include "VBoxMPGaWddm.h"
#include "../VBoxMPVidPn.h"
#include "VBoxMPGaExt.h"

#include <iprt/asm-mem.h> /* must be included before SvgaHw.h */

#include "Svga.h"
#include "SvgaFifo.h"
#include "SvgaCmd.h"
#include "SvgaHw.h"

#include <iprt/memobj.h>

bool SvgaIsDXSupported(PVBOXMP_DEVEXT pDevExt)
{
    return    pDevExt->pGa
           && pDevExt->pGa->hw.pSvga
           && RT_BOOL(pDevExt->pGa->hw.pSvga->u32Caps & SVGA_CAP_DX);
}


void SvgaCursorSetVisibility(PVBOXMP_DEVEXT pDevExt, bool fVisible)
{
    /** @todo Implement SVGA_CAP2_EXTRA_REGS with SVGA_REG_CURSOR4_* */
    PVBOXWDDM_EXT_VMSVGA pSvga = pDevExt->pGa->hw.pSvga;
    uint32_t const u32CursorOn = fVisible
                               ? SVGA_CURSOR_ON_SHOW
                               : SVGA_CURSOR_ON_HIDE;
    SVGARegWrite(pSvga, SVGA_REG_CURSOR_ON, u32CursorOn);
}


void SvgaCursorUpdatePosition(PVBOXMP_DEVEXT pDevExt, int xPos, int yPos)
{
    PVBOXWDDM_EXT_VMSVGA pSvga = pDevExt->pGa->hw.pSvga;
    SVGARegWrite(pSvga, SVGA_REG_CURSOR_X, xPos);
    SVGARegWrite(pSvga, SVGA_REG_CURSOR_Y, yPos);
}


static NTSTATUS svgaCreateSurfaceForAllocation(VBOXWDDM_EXT_VMSVGA *pSvga, PVBOXWDDM_ALLOCATION pAllocation)
{
    NTSTATUS Status = SvgaSurfaceIdAlloc(pSvga, &pAllocation->dx.sid);
    Assert(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        void *pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DEFINE_GB_SURFACE_V4, sizeof(SVGA3dCmdDefineGBSurface_v4), SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SVGA3dCmdDefineGBSurface_v4 *pCmd = (SVGA3dCmdDefineGBSurface_v4 *)pvCmd;
            pCmd->sid              = pAllocation->dx.sid;
            pCmd->surfaceFlags     = pAllocation->dx.desc.surfaceInfo.surfaceFlags;
            pCmd->format           = pAllocation->dx.desc.surfaceInfo.format;
            pCmd->numMipLevels     = pAllocation->dx.desc.surfaceInfo.numMipLevels;
            pCmd->multisampleCount = pAllocation->dx.desc.surfaceInfo.multisampleCount;
            pCmd->autogenFilter    = pAllocation->dx.desc.surfaceInfo.autogenFilter;
            pCmd->size             = pAllocation->dx.desc.surfaceInfo.size;
            pCmd->arraySize        = pAllocation->dx.desc.surfaceInfo.arraySize;
            pCmd->bufferByteStride = pAllocation->dx.desc.surfaceInfo.bufferByteStride;
            SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdDefineGBSurface_v4));
        }
        else
            AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);

        if (NT_SUCCESS(Status))
        {
            if (   pAllocation->dx.SegmentId == 3
                || pAllocation->dx.desc.fPrimary)
            {
                pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_BIND_GB_SURFACE, sizeof(SVGA3dCmdBindGBSurface), SVGA3D_INVALID_ID);
                if (pvCmd)
                {
                    SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pvCmd;
                    pCmd->sid = pAllocation->dx.sid;
                    pCmd->mobid = pAllocation->dx.mobid;
                    SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdBindGBSurface));
                }
                else
                    AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);
            }
        }
    }

    if (!NT_SUCCESS(Status))
        SvgaSurfaceIdFree(pSvga, pAllocation->dx.sid);

    return Status;
}


static void svgaDestroyMobForAllocation(VBOXWDDM_EXT_VMSVGA *pSvga, PVBOXWDDM_ALLOCATION pAllocation)
{
    if (pAllocation->dx.mobid != SVGA3D_INVALID_ID)
    {
        uint32_t cbRequired = 0;
        SvgaMobDestroy(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);
        void *pvCmd = SvgaCmdBufReserve(pSvga, cbRequired, SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SvgaMobDestroy(pSvga, pAllocation->dx.mobid, pvCmd, cbRequired, &cbRequired);
            SvgaCmdBufCommit(pSvga, cbRequired);
        }

        pAllocation->dx.mobid = SVGA3D_INVALID_ID;
    }
}


static NTSTATUS svgaDefineMobForAllocation(VBOXWDDM_EXT_VMSVGA *pSvga, PVBOXWDDM_ALLOCATION pAllocation)
{
    AssertReturn(pAllocation->dx.SegmentId == 3 || pAllocation->dx.desc.fPrimary, STATUS_INVALID_PARAMETER);

    /* Allocate a mobid. */
    NTSTATUS Status = SvgaMobAlloc(pSvga, &pAllocation->dx.mobid, pAllocation->dx.pGbo);
    if (NT_SUCCESS(Status))
    {
        /* Inform the host about the mob. */
        uint32_t cbCmd = 0;
        SvgaMobDefine(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbCmd);
        void *pvCmd = SvgaCmdBufReserve(pSvga, cbCmd, SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SvgaMobDefine(pSvga, pAllocation->dx.mobid, pvCmd, cbCmd, &cbCmd);
            SvgaCmdBufCommit(pSvga, cbCmd);
            return STATUS_SUCCESS;
        }

        AssertFailedStmt(Status = STATUS_INSUFFICIENT_RESOURCES);
        /* Deallocate mobid. */
        SvgaMobFree(pSvga, &pAllocation->dx.mobid);
    }

    return Status;
}


static NTSTATUS svgaCreateAllocationSurface(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAllocation, DXGK_ALLOCATIONINFO *pAllocationInfo)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;

    /* Fill data for WDDM. */
    pAllocationInfo->Alignment                       = 0;
    pAllocationInfo->Size                            = pAllocation->dx.desc.cbAllocation;
    pAllocationInfo->PitchAlignedSize                = 0;
    pAllocationInfo->HintedBank.Value                = 0;
    pAllocationInfo->Flags.Value                     = 0;
    if (pAllocation->dx.desc.surfaceInfo.surfaceFlags
        & (SVGA3D_SURFACE_HINT_INDIRECT_UPDATE | SVGA3D_SURFACE_HINT_STATIC))
    {
        /* USAGE_DEFAULT */
        if (pAllocation->dx.desc.fPrimary)
        {
            /* Put primaries to the CPU visible segment. Because VidPn code currently assumes that they are there. */
            pAllocationInfo->PreferredSegment.Value      = 0;
            pAllocationInfo->SupportedReadSegmentSet     = 1; /* VRAM */
            pAllocationInfo->SupportedWriteSegmentSet    = 1; /* VRAM */
            pAllocationInfo->Flags.CpuVisible            = 1;

            pAllocation->dx.SegmentId = 1;
        }
        else
        {
            pAllocationInfo->PreferredSegment.Value      = 0;
            pAllocationInfo->SupportedReadSegmentSet     = 4; /* Host */
            pAllocationInfo->SupportedWriteSegmentSet    = 4; /* Host */

            pAllocation->dx.SegmentId = 3;
        }
    }
    else if (pAllocation->dx.desc.surfaceInfo.surfaceFlags
             & SVGA3D_SURFACE_HINT_DYNAMIC)
    {
        /* USAGE_DYNAMIC */
        pAllocationInfo->PreferredSegment.Value      = 0;
        pAllocationInfo->SupportedReadSegmentSet     = 2; /* Aperture */
        pAllocationInfo->SupportedWriteSegmentSet    = 2; /* Aperture */
        pAllocationInfo->Flags.CpuVisible            = 1;

        pAllocation->dx.SegmentId = 2;
    }
    else if (pAllocation->dx.desc.surfaceInfo.surfaceFlags
             & (SVGA3D_SURFACE_STAGING_UPLOAD | SVGA3D_SURFACE_STAGING_DOWNLOAD))
    {
        /* USAGE_STAGING */
        pAllocationInfo->PreferredSegment.Value      = 0;
        pAllocationInfo->SupportedReadSegmentSet     = 2; /* Aperture */
        pAllocationInfo->SupportedWriteSegmentSet    = 2; /* Aperture */
        pAllocationInfo->Flags.CpuVisible            = 1;

        pAllocation->dx.SegmentId = 2;
    }
    else
        AssertFailedReturn(STATUS_INVALID_PARAMETER);

    pAllocationInfo->EvictionSegmentSet              = 0;
    pAllocationInfo->MaximumRenamingListLength       = 1;
    pAllocationInfo->hAllocation                     = pAllocation;
    pAllocationInfo->pAllocationUsageHint            = NULL;
    pAllocationInfo->AllocationPriority              = D3DDDI_ALLOCATIONPRIORITY_NORMAL;

    /* Allocations in the host VRAM still need guest backing. */
    NTSTATUS Status;
    if (pAllocation->dx.SegmentId == 3 || pAllocation->dx.desc.fPrimary)
    {
        uint32_t const cbGB = RT_ALIGN_32(pAllocation->dx.desc.cbAllocation, PAGE_SIZE);

        Status = SvgaGboCreate(pSvga, &pAllocation->dx.pGbo, cbGB, "VMSVGAGB");
        if (NT_SUCCESS(Status))
        {
            Status = svgaDefineMobForAllocation(pSvga, pAllocation);
            if (NT_SUCCESS(Status))
            {
                Status = svgaCreateSurfaceForAllocation(pSvga, pAllocation);
                if (!NT_SUCCESS(Status))
                    svgaDestroyMobForAllocation(pSvga, pAllocation);
            }

            if (!NT_SUCCESS(Status))
                SvgaGboUnreference(pSvga, &pAllocation->dx.pGbo);
        }
    }
    else
        Status = svgaCreateSurfaceForAllocation(pSvga, pAllocation);

    return Status;
}


static NTSTATUS svgaCreateAllocationShaders(PVBOXWDDM_ALLOCATION pAllocation, DXGK_ALLOCATIONINFO *pAllocationInfo)
{
    /* Fill data for WDDM. */
    pAllocationInfo->Alignment                       = 0;
    pAllocationInfo->Size                            = pAllocation->dx.desc.cbAllocation;
    pAllocationInfo->PitchAlignedSize                = 0;
    pAllocationInfo->HintedBank.Value                = 0;
    pAllocationInfo->Flags.Value                     = 0;
    pAllocationInfo->Flags.CpuVisible                = 1;
    pAllocationInfo->PreferredSegment.Value          = 0;
    pAllocationInfo->SupportedReadSegmentSet         = 2; /* Aperture */
    pAllocationInfo->SupportedWriteSegmentSet        = 2; /* Aperture */
    pAllocationInfo->EvictionSegmentSet              = 0;
    pAllocationInfo->MaximumRenamingListLength       = 0;
    pAllocationInfo->hAllocation                     = pAllocation;
    pAllocationInfo->pAllocationUsageHint            = NULL;
    pAllocationInfo->AllocationPriority              = D3DDDI_ALLOCATIONPRIORITY_MAXIMUM;
    return STATUS_SUCCESS;
}


static NTSTATUS svgaDestroyAllocationSurface(VBOXWDDM_EXT_VMSVGA *pSvga, PVBOXWDDM_ALLOCATION pAllocation)
{
    NTSTATUS Status = STATUS_SUCCESS;
    if (pAllocation->dx.sid != SVGA3D_INVALID_ID)
    {
        void *pvCmd;
        if (pAllocation->dx.mobid != SVGA3D_INVALID_ID)
        {
            /* Unbind mob */
            Assert(pAllocation->dx.SegmentId == 3 || pAllocation->dx.desc.fPrimary);

            pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_BIND_GB_SURFACE, sizeof(SVGA3dCmdBindGBSurface), SVGA3D_INVALID_ID);
            if (pvCmd)
            {
                SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pvCmd;
                pCmd->sid = pAllocation->dx.sid;
                pCmd->mobid = SVGA3D_INVALID_ID;
                SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdBindGBSurface));
            }
        }

        pvCmd = SvgaCmdBuf3dCmdReserve(pSvga, SVGA_3D_CMD_DESTROY_GB_SURFACE, sizeof(SVGA3dCmdDestroyGBSurface), SVGA3D_INVALID_ID);
        if (pvCmd)
        {
            SVGA3dCmdDestroyGBSurface *pCmd = (SVGA3dCmdDestroyGBSurface *)pvCmd;
            pCmd->sid = pAllocation->dx.sid;
            SvgaCmdBufCommit(pSvga, sizeof(SVGA3dCmdDestroyGBSurface));
        }

        Status = SvgaSurfaceIdFree(pSvga, pAllocation->dx.sid);
        pAllocation->dx.sid = SVGA3D_INVALID_ID;

        svgaDestroyMobForAllocation(pSvga, pAllocation);
    }
    return Status;
}


static NTSTATUS svgaDestroyAllocationShaders(VBOXWDDM_EXT_VMSVGA *pSvga, PVBOXWDDM_ALLOCATION pAllocation)
{
    svgaDestroyMobForAllocation(pSvga, pAllocation);
    return STATUS_SUCCESS;
}


NTSTATUS APIENTRY DxgkDdiDXCreateAllocation(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEALLOCATION *pCreateAllocation)
{
    DXGK_ALLOCATIONINFO *pAllocationInfo = &pCreateAllocation->pAllocationInfo[0];
    AssertReturn(   pCreateAllocation->PrivateDriverDataSize == 0
                 && pCreateAllocation->NumAllocations == 1
                 && pCreateAllocation->pAllocationInfo[0].PrivateDriverDataSize == sizeof(VBOXDXALLOCATIONDESC),
                 STATUS_INVALID_PARAMETER);

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    NTSTATUS Status = STATUS_SUCCESS;

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)GaMemAllocZero(sizeof(VBOXWDDM_ALLOCATION));
    AssertReturn(pAllocation, STATUS_INSUFFICIENT_RESOURCES);

    /* Init allocation data. */
    pAllocation->enmType = VBOXWDDM_ALLOC_TYPE_D3D;
    pAllocation->dx.desc = *(PVBOXDXALLOCATIONDESC)pAllocationInfo->pPrivateDriverData;
    pAllocation->dx.sid = SVGA3D_INVALID_ID;
    pAllocation->dx.mobid = SVGA3D_INVALID_ID;
    //pAllocation->dx.SegmentId = 0;
    //pAllocation->dx.pGbo = 0;

    KeInitializeSpinLock(&pAllocation->OpenLock);
    InitializeListHead(&pAllocation->OpenList);
    pAllocation->CurVidPnSourceId = -1;

    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
        Status = svgaCreateAllocationSurface(pDevExt, pAllocation, pAllocationInfo);
    else if (   pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SHADERS
             || pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_CO)
        Status = svgaCreateAllocationShaders(pAllocation, pAllocationInfo);
    else
        Status = STATUS_INVALID_PARAMETER;
    AssertReturnStmt(NT_SUCCESS(Status), GaMemFree(pAllocation), Status);

    /* Legacy fields for VidPn code. */
    pAllocation->AllocData.SurfDesc.VidPnSourceId = pAllocation->dx.desc.fPrimary
                                                  ? pAllocation->dx.desc.PrimaryDesc.VidPnSourceId
                                                  : 0;
    pAllocation->AllocData.hostID = pAllocation->dx.sid;
    pAllocation->AllocData.Addr.SegmentId = pAllocation->dx.SegmentId;
    pAllocation->AllocData.Addr.offVram = VBOXVIDEOOFFSET_VOID;

    return Status;
}


NTSTATUS APIENTRY DxgkDdiDXDestroyAllocation(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_DESTROYALLOCATION*  pDestroyAllocation)
{
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    NTSTATUS Status = STATUS_SUCCESS;

    AssertReturn(pDestroyAllocation->NumAllocations == 1, STATUS_INVALID_PARAMETER);

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pDestroyAllocation->pAllocationList[0];
    AssertReturn(pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_D3D, STATUS_INVALID_PARAMETER);

    Assert(pAllocation->cOpens == 0);

    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
        Status = svgaDestroyAllocationSurface(pDevExt->pGa->hw.pSvga, pAllocation);
    else if (   pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SHADERS
             || pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_CO)
        Status = svgaDestroyAllocationShaders(pDevExt->pGa->hw.pSvga, pAllocation);
    else
        AssertFailedReturn(STATUS_INVALID_PARAMETER);

    SvgaGboUnreference(pDevExt->pGa->hw.pSvga, &pAllocation->dx.pGbo);

    RT_ZERO(*pAllocation);
    GaMemFree(pAllocation);

    return Status;
}


NTSTATUS APIENTRY DxgkDdiDXDescribeAllocation(
    CONST HANDLE  hAdapter,
    DXGKARG_DESCRIBEALLOCATION*  pDescribeAllocation)
{
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    RT_NOREF(pDevExt);

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pDescribeAllocation->hAllocation;
    AssertReturn(pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_D3D, STATUS_INVALID_PARAMETER);
    AssertReturn(pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE, STATUS_INVALID_PARAMETER);

    pDescribeAllocation->Width                        = pAllocation->dx.desc.surfaceInfo.size.width;
    pDescribeAllocation->Height                       = pAllocation->dx.desc.surfaceInfo.size.height;
    pDescribeAllocation->Format                       = pAllocation->dx.desc.enmDDIFormat;
    pDescribeAllocation->MultisampleMethod.NumSamples       = 0; /** @todo Multisample. */
    pDescribeAllocation->MultisampleMethod.NumQualityLevels = 0;
    if (pAllocation->dx.desc.fPrimary)
    {
        pDescribeAllocation->RefreshRate.Numerator    = pAllocation->dx.desc.PrimaryDesc.ModeDesc.RefreshRate.Numerator;
        pDescribeAllocation->RefreshRate.Denominator  = pAllocation->dx.desc.PrimaryDesc.ModeDesc.RefreshRate.Denominator;
    }
    else
    {
        pDescribeAllocation->RefreshRate.Numerator    = 0;
        pDescribeAllocation->RefreshRate.Denominator  = 0;
    }
    pDescribeAllocation->PrivateDriverFormatAttribute = 0;
    pDescribeAllocation->Flags.Value                  = 0;
    pDescribeAllocation->Rotation                     = D3DDDI_ROTATION_IDENTITY;

    return STATUS_SUCCESS;
}


static NTSTATUS svgaRenderPatches(PVBOXWDDM_CONTEXT pContext, DXGKARG_RENDER *pRender, void *pvDmaBuffer, uint32_t cbDmaBuffer)
{
    RT_NOREF(pContext);
    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t cOut = 0;
    for (unsigned i = 0; i < pRender->PatchLocationListInSize; ++i)
    {
        if (cOut >= pRender->PatchLocationListOutSize)
        {
            /** @todo Merge generation of patches witht SvgaRenderCommandsD3D in order to correctly
             * split a command buffer in case of STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER?
             */
            DEBUG_BREAKPOINT_TEST();
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            break;
        }

        D3DDDI_PATCHLOCATIONLIST const *pIn = &pRender->pPatchLocationListIn[i];
        if (pIn->PatchOffset >= cbDmaBuffer)
            break; /* Filled all patches which describe locations in the processed commands. */

        void * const pPatchAddress = (uint8_t *)pvDmaBuffer + pIn->PatchOffset;
        VBOXDXALLOCATIONTYPE const enmAllocationType = (VBOXDXALLOCATIONTYPE)pIn->DriverId;

        /* "Even though the driver's DxgkDdiRender function pre-patches the DMA buffer, the driver
         *  must still insert all of the references to allocations into the output patch-location list
         *  that the pPatchLocationListOut member of DXGKARG_RENDER specifies."
         */
        pRender->pPatchLocationListOut[cOut] = *pIn;

        DXGK_ALLOCATIONLIST *pAllocationListEntry = &pRender->pAllocationList[pIn->AllocationIndex];
        PVBOXWDDM_OPENALLOCATION pOA = (PVBOXWDDM_OPENALLOCATION)pAllocationListEntry->hDeviceSpecificAllocation;
        if (pOA)
        {
            PVBOXWDDM_ALLOCATION pAllocation = pOA->pAllocation;
            /* Allocation type determines what the patch is about. */
            Assert(pAllocation->dx.desc.enmAllocationType == enmAllocationType);
            if (enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
            {
                if (   pAllocation->dx.sid != SVGA3D_INVALID_ID
                    && pAllocation->dx.mobid != SVGA3D_INVALID_ID)
                    *(uint32_t *)pPatchAddress = pAllocation->dx.sid;
            }
            else if (   enmAllocationType == VBOXDXALLOCATIONTYPE_SHADERS
                     || enmAllocationType == VBOXDXALLOCATIONTYPE_CO)
            {
                if (pAllocation->dx.mobid != SVGA3D_INVALID_ID)
                    *(uint32_t *)pPatchAddress = pAllocation->dx.mobid;
            }
        }
        else
        {
            if (   enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE
                || enmAllocationType == VBOXDXALLOCATIONTYPE_SHADERS
                || enmAllocationType == VBOXDXALLOCATIONTYPE_CO)
                *(uint32_t *)pPatchAddress = SVGA3D_INVALID_ID;
        }

        ++cOut;
    }

    GALOG(("pvDmaBuffer = %p, cbDmaBuffer = %u, cOut = %u\n", pvDmaBuffer, cbDmaBuffer, cOut));

    pRender->pPatchLocationListOut = &pRender->pPatchLocationListOut[cOut];
    return Status;
}


NTSTATUS APIENTRY DxgkDdiDXRender(PVBOXWDDM_CONTEXT pContext, DXGKARG_RENDER *pRender)
{
    PVBOXWDDM_DEVICE pDevice = pContext->pDevice;
    PVBOXMP_DEVEXT pDevExt = pDevice->pAdapter;
    VBOXWDDM_EXT_GA *pGaDevExt = pDevExt->pGa;

    GALOG(("[%p] Command %p/%d, Dma %p/%d, Private %p/%d, MO %d, S %d, Phys 0x%RX64, AL %p/%d, PLLIn %p/%d, PLLOut %p/%d\n",
           pContext,
           pRender->pCommand, pRender->CommandLength,
           pRender->pDmaBuffer, pRender->DmaSize,
           pRender->pDmaBufferPrivateData, pRender->DmaBufferPrivateDataSize,
           pRender->MultipassOffset, pRender->DmaBufferSegmentId, pRender->DmaBufferPhysicalAddress.QuadPart,
           pRender->pAllocationList, pRender->AllocationListSize,
           pRender->pPatchLocationListIn, pRender->PatchLocationListInSize,
           pRender->pPatchLocationListOut, pRender->PatchLocationListOutSize
         ));

    AssertReturn(pRender->DmaBufferPrivateDataSize >= sizeof(GARENDERDATA), STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);

    GARENDERDATA *pRenderData = NULL;  /* Pointer to the DMA buffer description. */
    uint32_t cbPrivateData = 0;        /* Bytes to place into the private data buffer. */
    uint32_t u32TargetLength = 0;      /* Bytes to place into the DMA buffer. */
    uint32_t u32ProcessedLength = 0;   /* Bytes consumed from command buffer. */

    /* Calculate where the commands start. */
    void const *pvSource = (uint8_t *)pRender->pCommand + pRender->MultipassOffset;
    uint32_t cbSource = pRender->CommandLength - pRender->MultipassOffset;

    NTSTATUS Status = STATUS_SUCCESS;
    __try
    {
        /* Generate DMA buffer from the supplied command buffer.
         * Store the command buffer descriptor to pDmaBufferPrivateData.
         *
         * The display miniport driver must validate the command buffer.
         *
         * Copy commands to the pDmaBuffer.
         */
        Status = SvgaRenderCommandsD3D(pGaDevExt->hw.pSvga, pContext->pSvgaContext,
                                       pRender->pDmaBuffer, pRender->DmaSize, pvSource, cbSource,
                                       &u32TargetLength, &u32ProcessedLength);
        if (NT_SUCCESS(Status))
        {
            Status = svgaRenderPatches(pContext, pRender, pRender->pDmaBuffer, u32ProcessedLength);
        }

        /* Fill RenderData description in any case, it will be ignored if the above code failed. */
        pRenderData = (GARENDERDATA *)pRender->pDmaBufferPrivateData;
        pRenderData->u32DataType  = GARENDERDATA_TYPE_RENDER;
        pRenderData->cbData       = u32TargetLength;
        pRenderData->pFenceObject = NULL;
        pRenderData->pvDmaBuffer  = pRender->pDmaBuffer; /** @todo Should not be needed for D3D context. */
        pRenderData->pHwRenderData = NULL;
        cbPrivateData = sizeof(GARENDERDATA);
        GALOG(("Status = 0x%x\n", Status));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    switch (Status)
    {
        case STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER:
            pRender->MultipassOffset += u32ProcessedLength;
            RT_FALL_THRU();
        case STATUS_SUCCESS:
        {
            Assert(pRenderData);
            if (u32TargetLength == 0)
            {
                DEBUG_BREAKPOINT_TEST();
                /* Trigger command submission anyway by increasing pDmaBufferPrivateData */
                /* Update the DMA buffer description. */
                pRenderData->u32DataType  = GARENDERDATA_TYPE_FENCE;
                pRenderData->cbData       = u32TargetLength;
                /* pRenderData->pFenceObject stays */
                pRenderData->pvDmaBuffer  = NULL; /* Not used */
            }
            pRender->pDmaBuffer = (uint8_t *)pRender->pDmaBuffer + u32TargetLength;
            pRender->pDmaBufferPrivateData = (uint8_t *)pRender->pDmaBufferPrivateData + cbPrivateData;
        } break;
        default: break;
    }

    return Status;
}


SIZE_T svgaGetAllocationSize(PVBOXWDDM_ALLOCATION pAllocation)
{
    if (pAllocation->enmType != VBOXWDDM_ALLOC_TYPE_D3D)
        return pAllocation->AllocData.SurfDesc.cbSize;
    return pAllocation->dx.desc.cbAllocation;
}


static NTSTATUS svgaPTSysMem2VRAM(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAllocation, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    /* This is a simple memcopy. */
    RT_NOREF(pAllocation);

    uint64_t const offVRAM = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.QuadPart;
    UINT const TransferOffset = pBuildPagingBuffer->Transfer.TransferOffset;
    SIZE_T const TransferSize = pBuildPagingBuffer->Transfer.TransferSize;
    AssertReturn(   offVRAM <= pDevExt->cbVRAMCpuVisible
                 && TransferOffset <= pDevExt->cbVRAMCpuVisible - offVRAM
                 && TransferSize <= pDevExt->cbVRAMCpuVisible - offVRAM - TransferOffset,
                 STATUS_INVALID_PARAMETER);

    void *pvSrc = MmGetSystemAddressForMdlSafe(pBuildPagingBuffer->Transfer.Source.pMdl, NormalPagePriority);

    memcpy((uint8_t *)pDevExt->pvVisibleVram + offVRAM + TransferOffset,
           (uint8_t *)pvSrc + TransferOffset,
           TransferSize);

    return STATUS_SUCCESS;
}


static NTSTATUS svgaPTVRAM2SysMem(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAllocation, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    /* This is a simple memcopy. */
    RT_NOREF(pAllocation);

    uint64_t const offVRAM = pBuildPagingBuffer->Transfer.Source.SegmentAddress.QuadPart;
    UINT const TransferOffset = pBuildPagingBuffer->Transfer.TransferOffset;
    SIZE_T const TransferSize = pBuildPagingBuffer->Transfer.TransferSize;
    AssertReturn(   offVRAM <= pDevExt->cbVRAMCpuVisible
                 && TransferOffset <= pDevExt->cbVRAMCpuVisible - offVRAM
                 && TransferSize <= pDevExt->cbVRAMCpuVisible - offVRAM - TransferOffset,
                 STATUS_INVALID_PARAMETER);

    void *pvDst = MmGetSystemAddressForMdlSafe(pBuildPagingBuffer->Transfer.Destination.pMdl, NormalPagePriority);

    memcpy((uint8_t *)pvDst + TransferOffset,
           (uint8_t *)pDevExt->pvVisibleVram + offVRAM + TransferOffset,
           TransferSize);

    return STATUS_SUCCESS;
}


static NTSTATUS svgaPagingTransfer(PVBOXMP_DEVEXT pDevExt, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;
    RT_NOREF(pSvga);

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->Transfer.hAllocation;
    AssertReturn(pAllocation, STATUS_INVALID_PARAMETER);

    /* "The size value is expanded to a multiple of the native host page size (for example, 4 KB on the x86 architecture)."
     * I.e. TransferOffset and TransferSize are within the aligned size.
     */
    SIZE_T const cbAllocation = RT_ALIGN_32(svgaGetAllocationSize(pAllocation), PAGE_SIZE);
    AssertReturn(   pBuildPagingBuffer->Transfer.TransferOffset <= cbAllocation
                 && pBuildPagingBuffer->Transfer.TransferSize <= cbAllocation - pBuildPagingBuffer->Transfer.TransferOffset,
                 STATUS_INVALID_PARAMETER);

    if (pBuildPagingBuffer->Transfer.TransferSize == 0)
        return STATUS_SUCCESS;

    NTSTATUS Status = STATUS_SUCCESS;

    RT_NOREF(pcbCommands);

    switch (pBuildPagingBuffer->Transfer.Source.SegmentId)
    {
        case 0: /* From system memory. */
        {
            if (pBuildPagingBuffer->Transfer.Destination.SegmentId == 1) /* To VRAM */
            {
                if (!pAllocation->dx.desc.fPrimary)
                    Status = svgaPTSysMem2VRAM(pDevExt, pAllocation, pBuildPagingBuffer);
                else
                {
                    /* D3D driver primary is a host resource with guest backing storage. */
                    /** @todo Copy to the backing storage and UPDATE_GB_SURFACE. */
                    Status = svgaPTSysMem2VRAM(pDevExt, pAllocation, pBuildPagingBuffer);
                }
            }
            else
                DEBUG_BREAKPOINT_TEST();
            break;
        }
        case 1: /* From VRAM. */
        {
            if (pBuildPagingBuffer->Transfer.Destination.SegmentId == 0) /* To system memory */
            {
                if (!pAllocation->dx.desc.fPrimary)
                    Status = svgaPTVRAM2SysMem(pDevExt, pAllocation, pBuildPagingBuffer);
                else
                {
                    /* D3D driver primary is a host resource with guest backing storage. */
                    /** @todo Issue READBACK, wait and then copy. */
                    Status = svgaPTVRAM2SysMem(pDevExt, pAllocation, pBuildPagingBuffer);
                }
            }
            else
                DEBUG_BREAKPOINT_TEST();
            break;
        }

        default:
            DEBUG_BREAKPOINT_TEST();
    }

    return Status;
}


static NTSTATUS svgaPagingFill(PVBOXMP_DEVEXT pDevExt, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;
    RT_NOREF(pSvga);

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->Fill.hAllocation;
    AssertReturn(pAllocation, STATUS_INVALID_PARAMETER);

    AssertReturn((pBuildPagingBuffer->Fill.FillSize & 3) == 0, STATUS_INVALID_PARAMETER);
    AssertReturn(   pAllocation->enmType != VBOXWDDM_ALLOC_TYPE_D3D
                 || pBuildPagingBuffer->Fill.Destination.SegmentId == pAllocation->dx.SegmentId, STATUS_INVALID_PARAMETER);

    /* "The size value is expanded to a multiple of the native host page size (for example, 4 KB on the x86 architecture)."
     * I.e. TransferOffset and TransferSize are within the aligned size.
     */
    SIZE_T const cbAllocation = RT_ALIGN_32(svgaGetAllocationSize(pAllocation), PAGE_SIZE);

    NTSTATUS Status = STATUS_SUCCESS;
    switch (pBuildPagingBuffer->Fill.Destination.SegmentId)
    {
        case 1: /* VRAM */
        {
            if (!pAllocation->dx.desc.fPrimary)
            {
                uint64_t const offVRAM = pBuildPagingBuffer->Fill.Destination.SegmentAddress.QuadPart;
                AssertReturn(   offVRAM < pDevExt->cbVRAMCpuVisible
                             && pBuildPagingBuffer->Fill.FillSize <= pDevExt->cbVRAMCpuVisible - offVRAM, STATUS_INVALID_PARAMETER);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
                ASMMemFill32((uint8_t *)pDevExt->pvVisibleVram + offVRAM, pBuildPagingBuffer->Fill.FillSize, pBuildPagingBuffer->Fill.FillPattern);
#else
                uint32_t *pu32 = (uint32_t *)((uint8_t *)pDevExt->pvVisibleVram + offVRAM);
                for (unsigned i = 0; i < pBuildPagingBuffer->Fill.FillSize / 4; ++i)
                    *pu32++ = pBuildPagingBuffer->Fill.FillPattern;
#endif
                break;
            }
        }
        RT_FALL_THRU();
        case 2: /* Aperture */
        case 3: /* Host */
        {
            if (pAllocation->enmType != VBOXWDDM_ALLOC_TYPE_D3D)
                break;

            void *pvDst;
            if (pBuildPagingBuffer->Fill.Destination.SegmentId == 3 || pAllocation->dx.desc.fPrimary)
            {
                AssertReturn(pAllocation->dx.pGbo->hMemObj != NIL_RTR0MEMOBJ, STATUS_INVALID_PARAMETER);
                pvDst = RTR0MemObjAddress(pAllocation->dx.pGbo->hMemObj);
            }
            else
            {
                AssertReturn(pAllocation->dx.pGbo->flags.fMdl && pAllocation->dx.pGbo->pMdl != NULL, STATUS_INVALID_PARAMETER);
                DEBUG_BREAKPOINT_TEST();
                pvDst = MmGetSystemAddressForMdlSafe(pAllocation->dx.pGbo->pMdl, NormalPagePriority);
                AssertReturn(pvDst, STATUS_INSUFFICIENT_RESOURCES);
            }

            /* Fill the guest backing pages. */
            uint32_t const cbFill = RT_MIN(pBuildPagingBuffer->Fill.FillSize, cbAllocation);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
            ASMMemFill32(pvDst, cbFill, pBuildPagingBuffer->Fill.FillPattern);
#else
            uint32_t *pu32 = (uint32_t *)pvDst;
            for (unsigned i = 0; i < cbFill / 4; ++i)
                *pu32++ = pBuildPagingBuffer->Fill.FillPattern;
#endif

            /* Emit UPDATE_GB_SURFACE */
            uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;
            uint32_t cbRequired = sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdUpdateGBSurface);
            if (pBuildPagingBuffer->DmaSize < cbRequired)
            {
                Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
                break;
            }

            SVGA3dCmdHeader *pHdr = (SVGA3dCmdHeader *)pu8Cmd;
            pHdr->id   = SVGA_3D_CMD_UPDATE_GB_SURFACE;
            pHdr->size = sizeof(SVGA3dCmdUpdateGBSurface);
            pu8Cmd += sizeof(*pHdr);

            SVGA3dCmdUpdateGBSurface *pCmd = (SVGA3dCmdUpdateGBSurface *)pu8Cmd;
            pCmd->sid = pAllocation->dx.sid;
            pu8Cmd += sizeof(*pCmd);

            *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;
            break;
        }
        default:
            AssertFailedReturn(STATUS_INVALID_PARAMETER);
    }
    return Status;
}


static NTSTATUS svgaPagingDiscardContent(PVBOXMP_DEVEXT pDevExt, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;
    RT_NOREF(pSvga);

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->DiscardContent.hAllocation;
    AssertReturn(pAllocation, STATUS_INVALID_PARAMETER);
    AssertReturn(   pAllocation->enmType != VBOXWDDM_ALLOC_TYPE_D3D
                 || pBuildPagingBuffer->DiscardContent.SegmentId == pAllocation->dx.SegmentId, STATUS_INVALID_PARAMETER);

    if (pAllocation->enmType != VBOXWDDM_ALLOC_TYPE_D3D)
        return STATUS_SUCCESS;

    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
    {
        /* Emit INVALIDATE_GB_SURFACE */
        uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;
        uint32_t cbRequired = sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdInvalidateGBSurface);
        if (pBuildPagingBuffer->DmaSize < cbRequired)
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

        SVGA3dCmdHeader *pHdr = (SVGA3dCmdHeader *)pu8Cmd;
        pHdr->id   = SVGA_3D_CMD_INVALIDATE_GB_SURFACE;
        pHdr->size = sizeof(SVGA3dCmdInvalidateGBSurface);
        pu8Cmd += sizeof(*pHdr);

        SVGA3dCmdUpdateGBSurface *pCmd = (SVGA3dCmdUpdateGBSurface *)pu8Cmd;
        pCmd->sid = pAllocation->dx.sid;
        pu8Cmd += sizeof(*pCmd);

        *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;
    }

    return STATUS_SUCCESS;
}


static NTSTATUS svgaPagingMapApertureSegment(PVBOXMP_DEVEXT pDevExt, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;

    /* Define a MOB for the supplied MDL and bind the allocation to the MOB. */

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->MapApertureSegment.hAllocation;
    AssertReturn(pAllocation, STATUS_INVALID_PARAMETER);
    AssertReturn(pBuildPagingBuffer->MapApertureSegment.SegmentId == 2, STATUS_INVALID_PARAMETER);

    /** @todo Mobs require locked pages. Could DX provide a Mdl without locked pages? */
    Assert(pBuildPagingBuffer->MapApertureSegment.pMdl->MdlFlags & MDL_PAGES_LOCKED);

    if (pAllocation->dx.mobid != SVGA3D_INVALID_ID)
    {
        DEBUG_BREAKPOINT_TEST();
        return STATUS_SUCCESS;
    }

    uint32_t cbRequired = 0;
    SvgaMobDefine(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);
    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
    {
        cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdBindGBSurface);
        cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdUpdateGBSurface);
    }

    if (pBuildPagingBuffer->DmaSize < cbRequired)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    Assert(pAllocation->dx.pGbo == NULL);

    NTSTATUS Status = SvgaGboCreateForMdl(pSvga, &pAllocation->dx.pGbo,
                                          pBuildPagingBuffer->MapApertureSegment.NumberOfPages,
                                          pBuildPagingBuffer->MapApertureSegment.pMdl,
                                          pBuildPagingBuffer->MapApertureSegment.MdlOffset);
    AssertReturn(NT_SUCCESS(Status), Status);

    Status = SvgaMobAlloc(pSvga, &pAllocation->dx.mobid, pAllocation->dx.pGbo);
    AssertReturnStmt(NT_SUCCESS(Status), SvgaGboUnreference(pSvga, &pAllocation->dx.pGbo), Status);

    uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;

    uint32_t cbCmd = 0;
    SvgaMobDefine(pSvga, pAllocation->dx.mobid, pu8Cmd,
                  cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer),
                  &cbCmd);
    pu8Cmd += cbCmd;

    SVGA3dCmdHeader *pHdr;

    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
    {
        /* Bind. */
        pHdr = (SVGA3dCmdHeader *)pu8Cmd;
        pHdr->id   = SVGA_3D_CMD_BIND_GB_SURFACE;
        pHdr->size = sizeof(SVGA3dCmdBindGBSurface);
        pu8Cmd += sizeof(*pHdr);

        {
        SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pu8Cmd;
        pCmd->sid         = pAllocation->dx.sid;
        pCmd->mobid       = pAllocation->dx.mobid;
        pu8Cmd += sizeof(*pCmd);
        }

        /* Update */
        pHdr = (SVGA3dCmdHeader *)pu8Cmd;
        pHdr->id   = SVGA_3D_CMD_UPDATE_GB_SURFACE;
        pHdr->size = sizeof(SVGA3dCmdUpdateGBSurface);
        pu8Cmd += sizeof(*pHdr);

        {
        SVGA3dCmdUpdateGBSurface *pCmd = (SVGA3dCmdUpdateGBSurface *)pu8Cmd;
        pCmd->sid         = pAllocation->dx.sid;
        pu8Cmd += sizeof(*pCmd);
        }
    }

    *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;

    return STATUS_SUCCESS;
}

static NTSTATUS svgaPagingUnmapApertureSegment(PVBOXMP_DEVEXT pDevExt, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer, uint32_t *pcbCommands)
{
    VBOXWDDM_EXT_VMSVGA *pSvga = pDevExt->pGa->hw.pSvga;

    /* Unbind the allocation from the MOB and destroy the MOB which is bound to the allocation. */

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pBuildPagingBuffer->UnmapApertureSegment.hAllocation;
    AssertReturn(pAllocation, STATUS_INVALID_PARAMETER);
    AssertReturn(pBuildPagingBuffer->UnmapApertureSegment.SegmentId == 2, STATUS_INVALID_PARAMETER);

    if (pAllocation->dx.mobid == SVGA3D_INVALID_ID)
    {
        DEBUG_BREAKPOINT_TEST();
        return STATUS_SUCCESS;
    }

    uint32_t cbRequired = 0;
    SvgaMobDestroy(pSvga, SVGA3D_INVALID_ID, NULL, 0, &cbRequired);
    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
        cbRequired += sizeof(SVGA3dCmdHeader) + sizeof(SVGA3dCmdBindGBSurface);

    if (pBuildPagingBuffer->DmaSize < cbRequired)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    uint8_t *pu8Cmd = (uint8_t *)pBuildPagingBuffer->pDmaBuffer;

    SVGA3dCmdHeader *pHdr;
    if (pAllocation->dx.desc.enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
    {
        /* Unbind. */
        pHdr = (SVGA3dCmdHeader *)pu8Cmd;
        pHdr->id   = SVGA_3D_CMD_BIND_GB_SURFACE;
        pHdr->size = sizeof(SVGA3dCmdBindGBSurface);
        pu8Cmd += sizeof(*pHdr);

        {
        SVGA3dCmdBindGBSurface *pCmd = (SVGA3dCmdBindGBSurface *)pu8Cmd;
        pCmd->sid   = pAllocation->dx.sid;
        pCmd->mobid = SVGA3D_INVALID_ID;
        pu8Cmd += sizeof(*pCmd);
        }
    }

    uint32_t cbCmd = 0;
    NTSTATUS Status = SvgaMobDestroy(pSvga, pAllocation->dx.mobid, pu8Cmd,
                                     cbRequired - ((uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer),
                                     &cbCmd);
    AssertReturn(NT_SUCCESS(Status), Status);
    pu8Cmd += cbCmd;

    pAllocation->dx.mobid = SVGA3D_INVALID_ID;
    SvgaGboUnreference(pSvga, &pAllocation->dx.pGbo);

    *pcbCommands = (uintptr_t)pu8Cmd - (uintptr_t)pBuildPagingBuffer->pDmaBuffer;
    return STATUS_SUCCESS;
}


NTSTATUS DxgkDdiDXBuildPagingBuffer(PVBOXMP_DEVEXT pDevExt, DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    if (pBuildPagingBuffer->DmaBufferPrivateDataSize < sizeof(GARENDERDATA))
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t cbCommands = 0;
    switch (pBuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
            Status = svgaPagingTransfer(pDevExt, pBuildPagingBuffer, &cbCommands);
            break;
        }
        case DXGK_OPERATION_FILL:
        {
            Status = svgaPagingFill(pDevExt, pBuildPagingBuffer, &cbCommands);
            break;
        }
        case DXGK_OPERATION_DISCARD_CONTENT:
        {
            Status = svgaPagingDiscardContent(pDevExt, pBuildPagingBuffer, &cbCommands);
            break;
        }
        case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
        {
            Status = svgaPagingMapApertureSegment(pDevExt, pBuildPagingBuffer, &cbCommands);
            break;
        }
        case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
        {
            Status = svgaPagingUnmapApertureSegment(pDevExt, pBuildPagingBuffer, &cbCommands);
            break;
        }
        default:
            AssertFailedStmt(Status = STATUS_NOT_IMPLEMENTED);
    }

    if (   Status == STATUS_SUCCESS
        || Status == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
    {
        if (cbCommands)
        {
            GARENDERDATA *pRenderData = (GARENDERDATA *)pBuildPagingBuffer->pDmaBufferPrivateData;
            pRenderData->u32DataType   = GARENDERDATA_TYPE_PAGING;
            pRenderData->cbData        = cbCommands;
            pRenderData->pFenceObject  = NULL;
            pRenderData->pvDmaBuffer   = pBuildPagingBuffer->pDmaBuffer;
            pRenderData->pHwRenderData = NULL;

            pBuildPagingBuffer->pDmaBuffer = (uint8_t *)pBuildPagingBuffer->pDmaBuffer + cbCommands;
            pBuildPagingBuffer->pDmaBufferPrivateData = (uint8_t *)pBuildPagingBuffer->pDmaBufferPrivateData + sizeof(GARENDERDATA);
        }
    }

    return Status;
}


NTSTATUS APIENTRY DxgkDdiDXPatch(PVBOXMP_DEVEXT pDevExt, const DXGKARG_PATCH *pPatch)
{
    //DEBUG_BREAKPOINT_TEST();

    for (UINT i = 0; i < pPatch->PatchLocationListSubmissionLength; ++i)
    {
        D3DDDI_PATCHLOCATIONLIST const *pPatchListEntry
            = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart + i];
        void * const pPatchAddress = (uint8_t *)pPatch->pDmaBuffer + pPatchListEntry->PatchOffset;
        VBOXDXALLOCATIONTYPE const enmAllocationType = (VBOXDXALLOCATIONTYPE)pPatchListEntry->DriverId;

        /* Ignore a dummy patch request. */
        if (pPatchListEntry->PatchOffset == ~0UL)
            continue;

        AssertReturn(   pPatchListEntry->PatchOffset >= pPatch->DmaBufferSubmissionStartOffset
                     && pPatchListEntry->PatchOffset < pPatch->DmaBufferSubmissionEndOffset, STATUS_INVALID_PARAMETER);
        AssertReturn(pPatchListEntry->AllocationIndex < pPatch->AllocationListSize, STATUS_INVALID_PARAMETER);

        DXGK_ALLOCATIONLIST const *pAllocationListEntry = &pPatch->pAllocationList[pPatchListEntry->AllocationIndex];
        AssertContinue(pAllocationListEntry->SegmentId != 0);

        PVBOXWDDM_OPENALLOCATION pOA = (PVBOXWDDM_OPENALLOCATION)pAllocationListEntry->hDeviceSpecificAllocation;
        if (pOA)
        {
            PVBOXWDDM_ALLOCATION pAllocation = pOA->pAllocation;
            /* Allocation type determines what the patch is about. */
            Assert(pAllocation->dx.desc.enmAllocationType == enmAllocationType);
            if (enmAllocationType == VBOXDXALLOCATIONTYPE_SURFACE)
            {
                Assert(pAllocation->dx.sid != SVGA3D_INVALID_ID);
                *(uint32_t *)pPatchAddress = pAllocation->dx.sid;
            }
            else if (   enmAllocationType == VBOXDXALLOCATIONTYPE_SHADERS
                     || enmAllocationType == VBOXDXALLOCATIONTYPE_CO)
            {
                Assert(pAllocation->dx.mobid != SVGA3D_INVALID_ID);
                *(uint32_t *)pPatchAddress = pAllocation->dx.mobid;
            }
            else if (   pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_STD_SHADOWSURFACE
                     || pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_STD_STAGINGSURFACE)
            {
                uint32_t *poffVRAM = (uint32_t *)pPatchAddress;
                *poffVRAM = pAllocationListEntry->PhysicalAddress.LowPart + pPatchListEntry->AllocationOffset;
            }
            else
                AssertFailed();
        }
        else
            AssertFailed(); /* Render should have already filtered out such patches. */
    }

#ifdef DEBUG
    if (!pPatch->Flags.Paging && !pPatch->Flags.Present)
    {
        SvgaDebugCommandsD3D(pDevExt->pGa->hw.pSvga,
                             ((PVBOXWDDM_CONTEXT)pPatch->hContext)->pSvgaContext,
                             (uint8_t *)pPatch->pDmaBuffer + pPatch->DmaBufferSubmissionStartOffset,
                             pPatch->DmaBufferSubmissionEndOffset - pPatch->DmaBufferSubmissionStartOffset);
    }
#else
    RT_NOREF(pDevExt);
#endif
    return STATUS_SUCCESS;
}
